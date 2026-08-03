// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_conn.c
 * @brief TCP-transport ↔ SSH-protocol glue.
 */

#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_forward.h"
#include "network_drivers/presentation/ssh/connection/ssh_server.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#if PC_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/ssh_comp.h"
#endif
#include "mmgr/plaintext.h"
#include "mmgr/secure.h"
#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/tcp.h"
#include "server/clock/clock.h" // pc_millis() for the server-initiated re-key timer
#include <string.h>

// All SSH connection-layer state, owned by one instance (internal linkage): the SSH-slot ->
// TCP-conn-slot mapping (0xFF = free), the one-time init flag, and the per-slot deferred-close
// flags. Grouped so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    uint8_t conn_for_ssh[MAX_SSH_CONNS];
    proto_bool init_done;
    volatile proto_bool close[MAX_SSH_CONNS];
} SshConnCtx;
static SshConnCtx s_sshc;

static void ensure_init()
{
    if (s_sshc.init_done)
    {
        return;
    }
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        s_sshc.conn_for_ssh[j] = 0xFF;
    }
    s_sshc.init_done = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Outbound: frame + (encrypt/MAC) one SSH message and write it to the socket.
// ---------------------------------------------------------------------------

static void ssh_emit(uint8_t i, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS || s_sshc.conn_for_ssh[i] == 0xFF) // GCOVR_EXCL_LINE  i is always in range: every caller
    {
        return; // GCOVR_EXCL_LINE  defensive: ssh_emit is only invoked by dispatch/poll with a live, mapped slot
    }
    TcpConn *conn = &conn_pool[s_sshc.conn_for_ssh[i]];
    // NOT defensive - this fires in practice. pc_ssh_conn_rx checks only the slot mapping, never
    // liveness, so a socket that dies between the inbound read and the reply reaches here with a
    // mapped but dead conn. (An earlier GCOVR_EXCL here claimed "the invoking dispatch/poll already
    // verified the conn is ACTIVE with a pcb"; that is not true of the rx path, and the exclusion
    // was hiding a live branch. test_conn_emit_drops_reply_on_dead_socket now covers it.)
    if (!pc_conn_active(conn->id))
    {
        return;
    }

    // Borrow the wire buffer from the shared scratch arena (released on return).
    const size_t wire_cap = SSH_WIRE_CAP;
    size_t mark = pc_plaintext_mark();
    uint8_t *wire = (uint8_t *)pc_plaintext_alloc(wire_cap, 16);
    if (!wire)
    {
        pc_plaintext_release(mark);
        return; // arena exhausted: drop the outbound message (fail closed)
    }
    size_t wlen = 0;
    if (ssh_pkt_send(i, payload, len, wire, &wlen, wire_cap) != 0)
    {
        pc_plaintext_release(mark);
        return;
    }
    pc_conn_send(conn->id, wire, (proto_u16)wlen);
    pc_conn_flush(conn->id);
    pc_plaintext_release(mark);
}

// ssh_pkt_recv handler: dispatch one decrypted message, remember fatal results.
static void ssh_msg_handler(uint8_t i, uint8_t msg_type, const uint8_t *payload, size_t len)
{
    if (pc_ssh_server_dispatch(i, msg_type, payload, len) < 0)
    {
        s_sshc.close[i] = PROTO_TRUE;
    }
}

void pc_ssh_conn_setup()
{
    ensure_init();
    pc_ssh_server_set_emit_cb(ssh_emit);
}

// The SSH connection ProtoHandler (Layer 5 dispatch seam) - installed by proto_register_builtins()
// via this accessor, so this module carries no dependency on the session layer.
static const ProtoHandler s_ssh_handler = {pc_ssh_conn_accept, pc_ssh_conn_rx, pc_ssh_conn_close, pc_ssh_conn_poll};
const ProtoHandler *ssh_proto_handler(void)
{
    // Wire the dispatcher's binary-packet emit callback here, at the one seam every consumer must go
    // through to install SSH: a consumer that registers this handler can then never be left with the
    // emit callback unset. Without it the server sends its identification banner (emitted directly by
    // pc_ssh_conn_accept) but every framed SSH packet after it - KEXINIT, KEXDH_REPLY, everything - is
    // silently dropped, so the handshake stalls and the client is reset on the idle timeout. Idempotent.
    pc_ssh_conn_setup();
    return &s_ssh_handler;
}

int pc_ssh_conn_send(uint8_t ssh_slot, uint32_t channel, const uint8_t *data, size_t len)
{
    if (ssh_slot >= MAX_SSH_CONNS || s_sshc.conn_for_ssh[ssh_slot] == 0xFF)
    {
        return -1;
    }
    TcpConn *conn = &conn_pool[s_sshc.conn_for_ssh[ssh_slot]];
    if (!pc_conn_active(conn->id))
    {
        return -1;
    }

    // Frame the application bytes as SSH_MSG_CHANNEL_DATA (bounded by the peer
    // window / max packet), then encrypt+MAC and write to the socket.
    // Borrow the payload + wire buffers from the shared scratch arena (released on
    // return); an exhausted arena fails closed.
    const size_t wire_cap = SSH_WIRE_CAP;
    size_t mark = pc_plaintext_mark();
    uint8_t *payload = (uint8_t *)pc_plaintext_alloc(SSH_PKT_BUF_SIZE, 16);
    uint8_t *wire = (uint8_t *)pc_plaintext_alloc(wire_cap, 16);
    if (!payload || !wire)
    {
        pc_plaintext_release(mark);
        return -1;
    }
    size_t plen = 0;
    if (pc_ssh_channel_build_data(ssh_slot, channel, data, len, payload, &plen, SSH_PKT_BUF_SIZE) != 0)
    {
        pc_plaintext_release(mark);
        return -1;
    }
    size_t wlen = 0;
    if (ssh_pkt_send(ssh_slot, payload, plen, wire, &wlen, wire_cap) != 0)
    {
        pc_plaintext_release(mark);
        return -1;
    }
    pc_conn_send(conn->id, wire, (proto_u16)wlen);
    pc_conn_flush(conn->id);
    pc_plaintext_release(mark);
    return (int)len;
    pc_plaintext_release(mark);
}

int pc_ssh_conn_close_channel(uint8_t ssh_slot, uint32_t channel)
{
    if (ssh_slot >= MAX_SSH_CONNS || s_sshc.conn_for_ssh[ssh_slot] == 0xFF)
    {
        return -1;
    }
    TcpConn *conn = &conn_pool[s_sshc.conn_for_ssh[ssh_slot]];
    if (!pc_conn_active(conn->id))
    {
        return -1;
    }

    uint8_t close_msgs[10];
    size_t clen = 0;
    // GCOVR_EXCL_START  clen != 10 cannot be true: build_close only returns 0 after writing exactly the ten
    // bytes of CHANNEL_EOF + CHANNEL_CLOSE, so that half of the guard is unreachable.
    if (pc_ssh_channel_build_close(ssh_slot, channel, close_msgs, &clen, sizeof(close_msgs)) != 0 || clen != 10)
    {
        return -1;
    }
    // GCOVR_EXCL_STOP

    // close_msgs holds CHANNEL_EOF then CHANNEL_CLOSE; each is its own SSH message,
    // so frame and send the two halves as two binary packets (RFC 4253 6). Borrow
    // the wire buffer from the shared scratch arena (released on return).
    const size_t wire_cap = SSH_WIRE_CAP;
    size_t mark = pc_plaintext_mark();
    uint8_t *wire = (uint8_t *)pc_plaintext_alloc(wire_cap, 16);
    if (!wire)
    {
        pc_plaintext_release(mark);
        return -1;
    }
    for (size_t off = 0; off < 10; off += 5)
    {
        size_t wlen = 0;
        if (ssh_pkt_send(ssh_slot, close_msgs + off, 5, wire, &wlen, wire_cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        pc_conn_send(conn->id, wire, (proto_u16)wlen);
    }
    pc_conn_flush(conn->id);
    pc_plaintext_release(mark);
    return 0;
    pc_plaintext_release(mark);
}

int pc_ssh_conn_open_forwarded(uint8_t ssh_slot, const char *conn_addr, uint16_t conn_port, const char *orig_addr,
                               uint16_t orig_port)
{
    if (ssh_slot >= MAX_SSH_CONNS || s_sshc.conn_for_ssh[ssh_slot] == 0xFF)
    {
        return -1;
    }
    TcpConn *conn = &conn_pool[s_sshc.conn_for_ssh[ssh_slot]];
    if (!pc_conn_active(conn->id))
    {
        return -1;
    }

    // Borrow the payload + wire buffers from the shared scratch arena (released on
    // return); an exhausted arena fails closed.
    const size_t wire_cap = SSH_WIRE_CAP;
    size_t mark = pc_plaintext_mark();
    uint8_t *payload = (uint8_t *)pc_plaintext_alloc(SSH_PKT_BUF_SIZE, 16);
    uint8_t *wire = (uint8_t *)pc_plaintext_alloc(wire_cap, 16);
    if (!payload || !wire)
    {
        pc_plaintext_release(mark);
        return -1;
    }
    size_t plen = 0;
    int ch = pc_ssh_channel_open_forwarded(ssh_slot, conn_addr, conn_port, orig_addr, orig_port, payload, &plen,
                                           SSH_PKT_BUF_SIZE);
    if (ch < 0)
    {
        pc_plaintext_release(mark);
        return -1; // channel pool full / build failed
    }
    size_t wlen = 0;
    if (ssh_pkt_send(ssh_slot, payload, plen, wire, &wlen, wire_cap) != 0)
    {
        pc_plaintext_release(mark);
        return -1;
    }
    pc_conn_send(conn->id, wire, (proto_u16)wlen);
    pc_conn_flush(conn->id);
    pc_plaintext_release(mark);
    return ch;
    pc_plaintext_release(mark);
}

void pc_ssh_conn_poll(uint8_t conn_slot)
{
    // The dispatch loop calls on_poll for every slot uniformly (no per-protocol gate); it used to poll
    // only ACTIVE slots, so keep that here to preserve behavior.
    TcpConn *conn = &conn_pool[conn_slot];
    if (!pc_conn_active(conn_slot))
    {
        return;
    }
    uint8_t j = conn->proto_slot;
    if (j >= MAX_SSH_CONNS || s_sshc.conn_for_ssh[j] != conn_slot)
    {
        return;
    }

    // Server-initiated re-key (RFC 4253 §9): once the volume (packet-count proxy) or time budget since
    // the last KEX is spent and the channel is not already re-keying, emit a fresh KEXINIT so a
    // long-lived / high-throughput session re-keys in place instead of being dropped at the
    // sequence-number wrap. The existing KEXINIT dispatch carries it to completion.
    SshSession *s = &ssh_sess[j];
    if (s->phase == SSH_PHASE_OPEN && !ssh_pkt[j].kex_active)
    {
        uint32_t elapsed = pc_millis() - s->last_kex_ms;
        if (ssh_rekey_due(ssh_pkt[j].seq_no_send, ssh_pkt[j].seq_no_recv, elapsed, SSH_REKEY_PACKET_THRESHOLD,
                          SSH_REKEY_TIME_MS))
        {
            uint8_t buf[SSH_PKT_BUF_SIZE];
            size_t n = 0;
            // GCOVR_EXCL_START  begin_rekey cannot fail here: j < MAX_SSH_CONNS is checked above, buf is
            // SSH_PKT_BUF_SIZE, and generating the new ephemeral is infallible for a valid slot.
            if (ssh_transport_begin_rekey(j, buf, &n, sizeof(buf)) == 0)
            {
                ssh_emit(j, buf, n);
            }
            // GCOVR_EXCL_STOP
        }
    }

#if PC_SSH_PORT_FORWARD
    pc_ssh_forward_pump(j);
#endif
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

void pc_ssh_conn_accept(uint8_t conn_slot)
{
    ensure_init();
    TcpConn *conn = &conn_pool[conn_slot];

    // Allocate a free SSH session slot.
    uint8_t j = 0xFF;
    for (uint8_t k = 0; k < MAX_SSH_CONNS; k++)
    {
        if (s_sshc.conn_for_ssh[k] == 0xFF)
        {
            j = k;
            break;
        }
    }
    if (j == 0xFF)
    {
        // No SSH capacity: drop the connection (transport owns the teardown).
        pc_conn_close(conn->id);
        return;
    }

    s_sshc.conn_for_ssh[j] = conn_slot;
    conn->proto_slot = j;
    s_sshc.close[j] = PROTO_FALSE;

    ssh_transport_init(j);
    ssh_pkt_init(j);
    pc_ssh_channel_init(j);
#if PC_ENABLE_SSH_ZLIB
    ssh_comp_reset(j); // clear compression state for the new connection (not run on a re-key)
#endif

    // Send the server identification banner (raw, before any binary packet).
    uint8_t banner[64];
    size_t blen = 0;
    // GCOVR_EXCL_START  the banner build cannot fail: SSH_SERVER_VERSION + CRLF is 17 bytes into a 64-byte
    // buffer, so only the liveness half of this guard is exercisable.
    if (ssh_transport_server_banner(banner, &blen, sizeof(banner)) == 0 && pc_conn_active(conn->id))
    {
        pc_conn_send(conn->id, banner, (proto_u16)blen);
        pc_conn_flush(conn->id);
    }
    // GCOVR_EXCL_STOP
}

static void close_conn(uint8_t conn_slot)
{
    pc_conn_close(conn_slot); // transport owns detach + slot reset + close
    pc_ssh_conn_close(conn_slot);
}

void pc_ssh_conn_rx(uint8_t conn_slot)
{
    TcpConn *conn = &conn_pool[conn_slot];
    uint8_t j = conn->proto_slot;
    if (j >= MAX_SSH_CONNS || s_sshc.conn_for_ssh[j] != conn_slot)
    {
        return;
    }

    // Drain the ring into a linear scratch buffer via the transport read API.
    static uint8_t buf[RX_BUF_SIZE];
    size_t n = pc_conn_read(conn_slot, buf, sizeof(buf));
    if (n == 0)
    {
        return;
    }

    size_t off = 0;
    if (ssh_sess[j].phase == SSH_PHASE_BANNER)
    {
        size_t consumed = 0;
        int rc = ssh_transport_recv_banner(j, buf, n, &consumed);
        if (rc < 0)
        {
            close_conn(conn_slot);
            return;
        }
        if (rc == 0)
        {
            return; // need more banner bytes
        }
        off = consumed;
        ssh_sess[j].phase = SSH_PHASE_KEXINIT;
    }

    if (off < n)
    {
        ssh_pkt_recv(j, buf + off, n - off, ssh_msg_handler);
    }

    pc_secure_wipe(buf, n);

    if (s_sshc.close[j])
    {
        close_conn(conn_slot);
    }
}

void pc_ssh_conn_close(uint8_t conn_slot)
{
    TcpConn *conn = &conn_pool[conn_slot];
    uint8_t j = conn->proto_slot;
    if (j < MAX_SSH_CONNS)
    {
#if PC_SSH_PORT_FORWARD
        pc_ssh_forward_reset(j); // close any forwarded TCP sockets this connection owned
#endif
        // Zero all key material and session state for this slot.
        ssh_keymat_wipe(j);
        ssh_dh_wipe(j);
        pc_secure_wipe(&ssh_sess[j], sizeof(SshSession));
        s_sshc.conn_for_ssh[j] = 0xFF;
    }
    conn->proto_slot = PC_PROTO_SLOT_NONE;
}
