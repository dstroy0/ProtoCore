// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_conn.c
 * @brief TCP-transport ↔ SSH-protocol glue.
 */

#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h" // pc_ssh_auth_reset on teardown
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_forward.h"
#include "network_drivers/presentation/ssh/ssh_server.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#if PC_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/ssh_comp.h"
#endif
#include "mmgr/plaintext.h"
#include "mmgr/secure.h"
#include "network_drivers/session/proto_handler.h"
#include "network_drivers/session/worker.h" // Workers.wake(): the owner drains the flagged packet
#include "network_drivers/transport/tcp.h"
#include "server/clock/clock.h" // pc_millis() for the server-initiated re-key timer

// The connection's span, laid out constants | kmt | control packet | data packet. Each region is a
// named offset from the base, and every byte a connection uses sits in one of them.
//
// constants: the values that persist across messages to compute the exchange hash H, and the
// session id the first KEX fixes (RFC 4253 sec 7.2).
#define SSH_OFF_V_C 0u
#define SSH_OFF_BANNER (SSH_OFF_V_C + SSH_VERSION_MAX)
#define SSH_OFF_I_C (SSH_OFF_BANNER + SSH_VERSION_MAX)
#define SSH_OFF_I_S (SSH_OFF_I_C + SSH_KEXINIT_MAX)
#define SSH_OFF_SESSION_ID (SSH_OFF_I_S + PC_SSH_KEXINIT_S_MAX)
#define SSH_OFF_ECDH_SK (SSH_OFF_SESSION_ID + SSH_KEXHASH_MAX_LEN)
#define SSH_OFF_ECDH_PK (SSH_OFF_ECDH_SK + 32u)
#define SSH_REGION_CONSTANTS (SSH_OFF_ECDH_PK + 32u)

// kmt: one key epoch's six RFC 4253 sec 7.2 keys in every cipher mode the negotiation can pick,
// then the DH ephemeral. The GCM contexts are 8-aligned, so the epoch starts and ends 8-aligned.
#define SSH_OFF_GCM_C2S 0u
#define SSH_OFF_GCM_S2C (SSH_OFF_GCM_C2S + PC_WORK_AESGCM)
#define SSH_OFF_CHACHA_C2S (SSH_OFF_GCM_S2C + PC_WORK_AESGCM)
#define SSH_OFF_CHACHA_S2C (SSH_OFF_CHACHA_C2S + PC_CHACHAPOLY_KEY_LEN)
#define SSH_OFF_MAC_C2S (SSH_OFF_CHACHA_S2C + PC_CHACHAPOLY_KEY_LEN)
#define SSH_OFF_MAC_S2C (SSH_OFF_MAC_C2S + 64u)
#define SSH_OFF_AES_KEY_C2S (SSH_OFF_MAC_S2C + 64u)
#define SSH_OFF_AES_KEY_S2C (SSH_OFF_AES_KEY_C2S + PC_AES256CTR_KEY_LEN)
#define SSH_OFF_AES_IV_C2S (SSH_OFF_AES_KEY_S2C + PC_AES256CTR_KEY_LEN)
#define SSH_OFF_AES_IV_S2C (SSH_OFF_AES_IV_C2S + PC_AES256CTR_CTR_LEN)
#define SSH_EPOCH_STRIDE (SSH_OFF_AES_IV_S2C + PC_AES256CTR_CTR_LEN)

// One epoch is resident. The second is reserved for the re-key window and released once both
// directions have switched (RFC 4253 sec 7.3), so it costs the wipe and not the footprint - a
// SSH_EPOCH_STRIDE borrow, not a region. The DH ephemeral follows the resident epoch.
#define SSH_OFF_EPOCH_0 0u
#define SSH_OFF_DH_Y (SSH_OFF_EPOCH_0 + SSH_EPOCH_STRIDE)
#define SSH_OFF_DH_F (SSH_OFF_DH_Y + sizeof(pc_bignum))
#define SSH_OFF_DH_K (SSH_OFF_DH_F + sizeof(pc_bignum))
#define SSH_REGION_KMT (SSH_OFF_DH_K + sizeof(pc_bignum))

// control packet: the wire buffer the codec frames into, the bytes the packet MAC works out of, and
// the ones the key exchange does. Knowable size - two framed packets is the worst case (RFC 4253
// sec 6), which SSH_WIRE_CAP already states.
#define SSH_OFF_WIRE 0u
#define SSH_OFF_MAC_WORK (SSH_OFF_WIRE + SSH_WIRE_CAP)
#define SSH_OFF_CRYPTO_WORK (SSH_OFF_MAC_WORK + PC_HMAC_SHA256_BORROW)
#define SSH_REGION_CONTROL (SSH_OFF_CRYPTO_WORK + PC_CRYPTO_BORROW_MAX)

// data packet: the bytes drained off the transport ring, then the reassembly they feed.
#define SSH_OFF_RX_READ 0u
#define SSH_OFF_RX_ASM (SSH_OFF_RX_READ + RX_BUF_SIZE)
#define SSH_REGION_DATA (SSH_OFF_RX_ASM + (size_t)SSH_PKT_BUF_SIZE)

// The whole span, in layout order.
#define SSH_OFF_CONSTANTS 0u
#define SSH_OFF_KMT (SSH_OFF_CONSTANTS + SSH_REGION_CONSTANTS)
#define SSH_OFF_CONTROL (SSH_OFF_KMT + SSH_REGION_KMT)
#define SSH_OFF_DATA (SSH_OFF_CONTROL + SSH_REGION_CONTROL)
#define SSH_SLOT_BORROW (SSH_OFF_DATA + SSH_REGION_DATA)

// The secure-pool term this file declares against PC_SECURE_ARENA_SIZE, proved against what is
// actually borrowed: one whole span per slot held on the persistent end for the life of the
// program, plus the payload and wire a single outbound call holds at the same time.
static_assert(PC_WORK_SSH_CONN >= ((size_t)MAX_SSH_CONNS * SSH_SLOT_BORROW) + (size_t)SSH_PKT_BUF_SIZE + SSH_WIRE_CAP,
              "PC_WORK_SSH_CONN must cover one connection span per SSH slot plus one transient payload "
              "and wire: raise it in protocore_config.h");

// All SSH connection-layer state, owned by one instance (internal linkage): the SSH-slot ->
// TCP-conn-slot mapping (0xFF = free), the one-time init flag, the per-slot deferred-close
// flags, and each slot's four regions. Grouped so it is one named owner, unreachable from any
// other translation unit.
typedef struct
{
    uint8_t conn_for_ssh[MAX_SSH_CONNS];
    proto_bool init_done;
    volatile proto_bool close[MAX_SSH_CONNS];
    SshConnNs region[MAX_SSH_CONNS];
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

// Point one key epoch's six RFC 4253 sec 7.2 keys at @p base, the kmt bytes reserved for it. Both
// epochs are laid out the same way, so a re-key binds the second with the same call.
static void bind_epoch(SshKeyMat *km, uint8_t *base)
{
    km->gcm_ctx_c2s = base + SSH_OFF_GCM_C2S;
    km->gcm_ctx_s2c = base + SSH_OFF_GCM_S2C;
    km->chacha_key_c2s = base + SSH_OFF_CHACHA_C2S;
    km->chacha_key_s2c = base + SSH_OFF_CHACHA_S2C;
    km->mac_key_c2s = base + SSH_OFF_MAC_C2S;
    km->mac_key_s2c = base + SSH_OFF_MAC_S2C;
    km->aes_key_c2s = base + SSH_OFF_AES_KEY_C2S;
    km->aes_key_s2c = base + SSH_OFF_AES_KEY_S2C;
    km->aes_iv_c2s = base + SSH_OFF_AES_IV_C2S;
    km->aes_iv_s2c = base + SSH_OFF_AES_IV_S2C;
}

// Take slot @p j's one borrow if it has none yet, then point every one of the slot's members at its
// own offset in it. From the secure pool's persistent end - the end no mark walks - so it is held
// for the slot's life and no release reclaims one slot's bytes out from under another. The borrow
// happens once; the binding happens on every call, because a connection's teardown zeroes the
// members. False only when the pool cannot cover the slot.
static proto_bool slot_storage(uint8_t j)
{
    if (s_sshc.region[j].constants == NULL)
    {
        pc_span b = pc_secure_persist_span(SSH_SLOT_BORROW);
        if (!pc_span_ok(b))
        {
            return PROTO_FALSE;
        }
        s_sshc.region[j].constants = b.buf + SSH_OFF_CONSTANTS;
        s_sshc.region[j].kmt = b.buf + SSH_OFF_KMT;
        s_sshc.region[j].control = b.buf + SSH_OFF_CONTROL;
        s_sshc.region[j].data = b.buf + SSH_OFF_DATA;
    }

    // constants: the handshake values that outlive the message that carried them.
    SshSession *s = &ssh_sess[j];
    uint8_t *k = s_sshc.region[j].constants;
    s->v_c = (char *)(k + SSH_OFF_V_C);
    s->banner_buf = k + SSH_OFF_BANNER;
    s->i_c = k + SSH_OFF_I_C;
    s->i_s = k + SSH_OFF_I_S;
    s->session_id = k + SSH_OFF_SESSION_ID;
    s->ecdh_sk = k + SSH_OFF_ECDH_SK;
    s->ecdh_pk = k + SSH_OFF_ECDH_PK;

    // kmt: the live epoch, then the DH ephemeral. The second epoch sits one stride along and is
    // bound when a re-key claims it.
    bind_epoch(&ssh_keys[j], s_sshc.region[j].kmt + SSH_OFF_EPOCH_0);
    SshDhState *dh = &ssh_dh[j];
    dh->y = (pc_bignum *)(s_sshc.region[j].kmt + SSH_OFF_DH_Y);
    dh->f = (pc_bignum *)(s_sshc.region[j].kmt + SSH_OFF_DH_F);
    dh->K = (pc_bignum *)(s_sshc.region[j].kmt + SSH_OFF_DH_K);

    // control packet and data packet.
    SshPacketState *p = &ssh_pkt[j];
    p->tx_wire = s_sshc.region[j].control + SSH_OFF_WIRE;
    p->mac_work = s_sshc.region[j].control + SSH_OFF_MAC_WORK;
    p->crypto_work = s_sshc.region[j].control + SSH_OFF_CRYPTO_WORK;
    p->rx_buf = s_sshc.region[j].data + SSH_OFF_RX_ASM;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Outbound: frame + (encrypt/MAC) one SSH message and write it to the socket.
// ---------------------------------------------------------------------------

static void ssh_emit(uint8_t i, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS || s_sshc.conn_for_ssh[i] == 0xFF)
    {
        return;
    }
    TcpConn *conn = &conn_pool[s_sshc.conn_for_ssh[i]];
    // pc_ssh_conn_rx checks the slot mapping, never liveness, so a socket that died between the
    // inbound read and this reply arrives here mapped but dead. Drop the reply.
    if (!pc_conn_active(conn->id))
    {
        return;
    }

    // Frame it into the secure pool and raise the flag, then wake the worker that owns this slot.
    // It owns the connection, the session and the pool the packet sits in, so it drains the packet
    // itself - woken when there is one rather than walking slots that have nothing.
    if (ssh_pkt_emit(i, payload, len) == 0)
    {
        Workers.wake(conn->owner);
    }
}

// Put the packet the codec left flagged on the wire. The worker owns this connection, this session
// and the pool the packet sits in, so it moves the bytes itself: as many as the send window takes
// now, the rest on a later pass, and the release - which wipes the packet - once the last byte is
// out. Runs after the codec, so a packet framed during this pass leaves during it.
static void ssh_tx_drain(uint8_t conn_slot, uint8_t j)
{
    SshPacketState *pkt = &ssh_pkt[j];
    if (!pkt->tx_ready || !pc_conn_active(conn_slot))
    {
        return;
    }
    size_t room = (size_t)Tcp.conn->sndbuf(conn_slot);
    size_t n = pkt->tx_len - pkt->tx_off;
    if (n > room)
    {
        n = room;
    }
    if (n > 0 && Tcp.conn->send(conn_slot, pkt->tx_wire + pkt->tx_off, (proto_u16)n))
    {
        Tcp.conn->flush(conn_slot);
        pkt->tx_off += n;
    }
    if (pkt->tx_off >= pkt->tx_len)
    {
        pkt->tx_ready = PROTO_FALSE; // the buffer stays borrowed for the next packet
    }
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

// The SSH connection ProtoHandler (Layer 5 dispatch seam) - installed by Session.proto->register_builtins()
// via this accessor, so this module carries no dependency on the session layer. Designated, so a
// member's position in the struct does not decide what it binds to.
static const ProtoHandler s_ssh_handler = {.on_accept = pc_ssh_conn_accept,
                                           .on_data = pc_ssh_conn_rx,
                                           .on_close = pc_ssh_conn_close,
                                           .on_poll = pc_ssh_conn_poll};
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
    // window / max packet), then encrypt+MAC and write to the socket. Both buffers come from the
    // secure pool: the payload is session plaintext until the cipher runs, and release wipes.
    const size_t wire_cap = SSH_WIRE_CAP;
    size_t mark = pc_secure_mark();
    uint8_t *payload = (uint8_t *)pc_secure_alloc(SSH_PKT_BUF_SIZE, 16);
    uint8_t *wire = (uint8_t *)pc_secure_alloc(wire_cap, 16);
    if (!payload || !wire)
    {
        pc_secure_release(mark);
        return -1;
    }
    size_t plen = 0;
    if (pc_ssh_channel_build_data(ssh_slot, channel, data, len, payload, &plen, SSH_PKT_BUF_SIZE) != 0)
    {
        pc_secure_release(mark);
        return -1;
    }
    size_t wlen = 0;
    if (ssh_pkt_send(ssh_slot, payload, plen, wire, &wlen, wire_cap) != 0)
    {
        pc_secure_release(mark);
        return -1;
    }
    // ssh_pkt_send has already advanced the sequence number and the cipher for this packet.
    const proto_bool queued = Tcp.conn->send(conn->id, wire, (proto_u16)wlen);
    Tcp.conn->flush(conn->id);
    pc_secure_release(mark);
    if (!queued)
    {
        return -1;
    }
    return (int)len;
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

    // The two messages and the wire they are framed into come from the secure pool, which wipes
    // them on release. close_msgs holds CHANNEL_EOF then CHANNEL_CLOSE; each is its own SSH
    // message, so the two halves go out as two binary packets (RFC 4253 sec 6).
    const size_t wire_cap = SSH_WIRE_CAP;
    size_t mark = pc_secure_mark();
    uint8_t *close_msgs = (uint8_t *)pc_secure_alloc(10, 16);
    uint8_t *wire = (uint8_t *)pc_secure_alloc(wire_cap, 16);
    if (!close_msgs || !wire)
    {
        pc_secure_release(mark);
        return -1;
    }
    size_t clen = 0;
    if (pc_ssh_channel_build_close(ssh_slot, channel, close_msgs, &clen, 10) != 0 || clen != 10)
    {
        pc_secure_release(mark);
        return -1;
    }
    for (size_t off = 0; off < 10; off += 5)
    {
        size_t wlen = 0;
        if (ssh_pkt_send(ssh_slot, close_msgs + off, 5, wire, &wlen, wire_cap) != 0)
        {
            pc_secure_release(mark);
            return -1;
        }
        Tcp.conn->send(conn->id, wire, (proto_u16)wlen);
    }
    Tcp.conn->flush(conn->id);
    pc_secure_release(mark);
    return 0;
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

    // Payload and wire come from the secure pool, which wipes them on release; an exhausted pool
    // fails closed.
    const size_t wire_cap = SSH_WIRE_CAP;
    size_t mark = pc_secure_mark();
    uint8_t *payload = (uint8_t *)pc_secure_alloc(SSH_PKT_BUF_SIZE, 16);
    uint8_t *wire = (uint8_t *)pc_secure_alloc(wire_cap, 16);
    if (!payload || !wire)
    {
        pc_secure_release(mark);
        return -1;
    }
    size_t plen = 0;
    int ch = pc_ssh_channel_open_forwarded(ssh_slot, conn_addr, conn_port, orig_addr, orig_port, payload, &plen,
                                           SSH_PKT_BUF_SIZE);
    if (ch < 0)
    {
        pc_secure_release(mark);
        return -1; // channel pool full / build failed
    }
    size_t wlen = 0;
    if (ssh_pkt_send(ssh_slot, payload, plen, wire, &wlen, wire_cap) != 0)
    {
        pc_secure_release(mark);
        return -1;
    }
    Tcp.conn->send(conn->id, wire, (proto_u16)wlen);
    Tcp.conn->flush(conn->id);
    pc_secure_release(mark);
    return ch;
}

void pc_ssh_conn_poll(uint8_t conn_slot)
{
    // Skip a slot that is not ACTIVE.
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
        if (SshTransport.rekey_due(ssh_pkt[j].seq_no_send, ssh_pkt[j].seq_no_recv, elapsed, SSH_REKEY_PACKET_THRESHOLD,
                          SSH_REKEY_TIME_MS))
        {
            size_t mark = pc_secure_mark();
            uint8_t *buf = (uint8_t *)pc_secure_alloc(SSH_PKT_BUF_SIZE, 16);
            size_t n = 0;
            if (buf && SshTransport.begin_rekey(j, buf, &n, SSH_PKT_BUF_SIZE) == 0)
            {
                ssh_emit(j, buf, n);
            }
            pc_secure_release(mark);
        }
    }

    // A password change (RFC 4252 sec 8) the application has finished: send the reply its
    // USERAUTH_REQUEST deferred. pc_ssh_auth_pw_change_take marks the session open on an OK.
    SshPwChange pw = pc_ssh_auth_pw_change_take(j);
    if (pw != PC_SSH_PW_CHANGE_NONE)
    {
        size_t pw_mark = pc_plaintext_mark();
        pc_span reply = pc_plaintext_span(SSH_PKT_BUF_SIZE, 4);
        size_t n = 0;
        int built = -1;
        if (pc_span_ok(reply) && pw == PC_SSH_PW_CHANGE_OK)
        {
            built = pc_ssh_auth_build_success(reply.buf, &n, reply.cap);
        }
        else if (pc_span_ok(reply))
        {
            built = pc_ssh_auth_build_failure(reply.buf, &n, reply.cap, PROTO_FALSE);
        }
        if (built == 0)
        {
            ssh_emit(j, reply.buf, n);
        }
        pc_plaintext_release(pw_mark);
    }

#if PC_SSH_PORT_FORWARD
    pc_ssh_forward_pump(j);
#endif

    ssh_tx_drain(conn_slot, j); // after the codec, so a packet framed on this pass leaves on it
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
        Tcp.conn->close(conn->id);
        return;
    }

    // The slot's bytes come before its state: every region below is reached through this borrow, so
    // a pool that cannot cover the slot fails the connection rather than handing out null regions.
    if (!slot_storage(j))
    {
        Tcp.conn->close(conn->id);
        return;
    }

    s_sshc.conn_for_ssh[j] = conn_slot;
    conn->proto_slot = j;
    s_sshc.close[j] = PROTO_FALSE;

    ssh_transport_init(j);
    ssh_pkt_init(j);
    // Role is a property of what was acquired: this slot was claimed by an inbound accept, so it is
    // the server end. It selects which direction's keys each cipher and MAC site reads.
    ssh_pkt[j].is_client = PROTO_FALSE;
    pc_ssh_channel_init(j);
#if PC_ENABLE_SSH_ZLIB
    ssh_comp_reset(j); // clear compression state for the new connection (not run on a re-key)
#endif

    // The server identification string, sent raw before any binary packet (RFC 4253 sec 4.2): the
    // version literal and the CRLF that ends it, which is a compile-time length. It carries nothing
    // secret, so it comes from the plaintext pool rather than the secure one.
    const size_t banner_cap = sizeof(SSH_SERVER_VERSION) + 1;
    size_t mark = pc_plaintext_mark();
    uint8_t *banner = (uint8_t *)pc_plaintext_alloc(banner_cap, 16);
    size_t blen = 0;
    if (banner && SshTransport.send_banner(banner, &blen, banner_cap) == 0 && pc_conn_active(conn->id))
    {
        Tcp.conn->send(conn->id, banner, (proto_u16)blen);
        Tcp.conn->flush(conn->id);
    }
    pc_plaintext_release(mark);
}

static void close_conn(uint8_t conn_slot)
{
    Tcp.conn->close(conn_slot); // transport owns detach + slot reset + close
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

    // Drain the ring into this slot's own read scratch via the transport read API.
    uint8_t *buf = s_sshc.region[j].data + SSH_OFF_RX_READ;
    size_t n = pc_conn_read(conn_slot, buf, RX_BUF_SIZE);
    if (n == 0)
    {
        return;
    }

    size_t off = 0;
    if (ssh_sess[j].phase == SSH_PHASE_BANNER)
    {
        size_t consumed = 0;
        int rc = SshTransport.recv_banner(j, buf, n, &consumed);
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

    ssh_tx_drain(conn_slot, j); // the reply the dispatch framed leaves on this pass

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
        // Zero all key material and session state for this slot. The span holds every byte the
        // connection used, so wiping it covers all four regions in one pass; the region pointers
        // stay, because the borrow is the slot's for the life of the program and asking the
        // persistent end for a second one would never give the first back.
        ssh_keymat_wipe(j);
        pc_ssh_auth_reset(j);
        pc_secure_wipe(s_sshc.region[j].constants, SSH_SLOT_BORROW);
        pc_secure_wipe(&ssh_sess[j], sizeof(SshSession));
        s_sshc.conn_for_ssh[j] = 0xFF;
    }
    conn->proto_slot = PC_PROTO_SLOT_NONE;
}
