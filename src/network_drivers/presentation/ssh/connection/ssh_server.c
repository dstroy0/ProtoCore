// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_server.c
 * @brief SSH message dispatcher implementation.
 */

#include "network_drivers/presentation/ssh/connection/ssh_server.h"
#include "mmgr/plaintext.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/transport/ssh_dh.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#if PC_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/ssh_comp.h"
#endif
#include <string.h>

// All SSH server-layer state, owned by one instance (internal linkage): the packet-emit
// callback. One named owner, unreachable from any other translation unit.
typedef struct
{
    SshEmitCb emit_cb = NULL;
} SshServerCtx;
static SshServerCtx s_srv;

void pc_ssh_server_set_emit_cb(SshEmitCb cb)
{
    s_srv.emit_cb = cb;
}

static inline void emit(uint8_t i, const uint8_t *p, size_t n)
{
    if (s_srv.emit_cb && n > 0)
    {
        s_srv.emit_cb(i, p, n);
    }
}

// Build and emit SSH_MSG_DISCONNECT("too many authentication failures") - the reason code, the
// description string, and an empty language tag (RFC 4253 §11.1) - then the caller closes.
static void emit_auth_failure_disconnect(uint8_t i, pc_span out)
{
    static const char desc[] = "too many authentication failures";
    const uint32_t reason = SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE;
    const uint32_t dl = (uint32_t)(sizeof(desc) - 1);
    const size_t need = 13 + dl; // 1 msg + 4 reason + 4 desc length + desc + 4 language tag
    if (out.cap < need)
    {
        return; // fail closed: no room for the whole message, so emit none of it
    }
    size_t o = 0;
    out.buf[o++] = SSH_MSG_DISCONNECT;
    out.buf[o++] = (uint8_t)(reason >> 24); // reason code (uint32, big-endian)
    out.buf[o++] = (uint8_t)(reason >> 16);
    out.buf[o++] = (uint8_t)(reason >> 8);
    out.buf[o++] = (uint8_t)(reason);
    out.buf[o++] = (uint8_t)(dl >> 24); // description (uint32 length + bytes)
    out.buf[o++] = (uint8_t)(dl >> 16);
    out.buf[o++] = (uint8_t)(dl >> 8);
    out.buf[o++] = (uint8_t)(dl);
    memcpy(out.buf + o, desc, dl);
    o += dl;
    out.buf[o++] = 0; // language tag: empty string (4-byte length 0)
    out.buf[o++] = 0;
    out.buf[o++] = 0;
    out.buf[o++] = 0;
    emit(i, out.buf, o);
}

int pc_ssh_server_dispatch(uint8_t i, uint8_t msg_type, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshSession *s = &ssh_sess[i];

    // The reply buffer is borrowed for this dispatch, not carried on the worker stack: it is the
    // single largest frame on the SSH path and the handshake below it (curve25519 / ed25519) is
    // already the deepest call chain in the library. pc_plaintext_span binds the capacity to the
    // allocation, so every `reply.cap` below is the number that was actually reserved.
    size_t mark = pc_plaintext_mark();
    pc_span reply = pc_plaintext_span(SSH_PKT_BUF_SIZE, 16);
    if (!pc_span_ok(reply))
    {
        pc_plaintext_release(mark);
        return -1; // arena exhausted: fail closed, the caller drops the connection
    }
    size_t n = 0;

    switch (msg_type)
    {
    case SSH_MSG_IGNORE:
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_DISCONNECT:
        pc_plaintext_release(mark);
        return -1; // peer is closing

    case SSH_MSG_KEXINIT:
        // Initial KEX or an in-session re-key request. Negotiate, reply with our
        // own KEXINIT, generate a fresh ephemeral, and await KEXDH_INIT.
        if (ssh_kexinit_parse(i, payload, len) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (ssh_kexinit_build(i, reply.buf, &n, reply.cap) != 0) // GCOVR_EXCL_LINE  cannot fail: i is checked above and
        {
            pc_plaintext_release(mark);
            return -1; // GCOVR_EXCL_LINE  buf is SSH_PKT_BUF_SIZE
        }
        emit(i, reply.buf, n);
        if (ssh_kex_generate(i) != 0) // GCOVR_EXCL_LINE  cannot fail: i is checked above and every method's
        {
            pc_plaintext_release(mark);
            return -1; // GCOVR_EXCL_LINE  ephemeral generation is infallible for i < MAX_SSH_CONNS
        }
        s->phase = SSH_PHASE_DH_INIT;
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_KEXDH_INIT:
        if (s->phase != SSH_PHASE_DH_INIT)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (ssh_kexdh_handle(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        emit(i, reply.buf, n); // KEXDH_REPLY
        {
            uint8_t newkeys = SSH_MSG_NEWKEYS;
            emit(i, &newkeys, 1); // server NEWKEYS (this one still goes out unencrypted)
            ssh_newkeys_sent(i);  // ...but our outbound is encrypted from the next packet on
        }
        pc_plaintext_release(mark);
        return 0; // ssh_kexdh_handle advanced phase to NEWKEYS

    case SSH_MSG_NEWKEYS:
        ssh_newkeys_complete(i); // activate encryption; → SERVICE or OPEN
        // RFC 8308: with encryption now active, advertise the signature algorithms
        // we accept for pubkey userauth (server-sig-algs) so a modern client will
        // sign an RSA key - it otherwise reports "no mutual signature algorithm".
        // First encrypted message, before the client's SERVICE_REQUEST.
        if (s->ext_info_c && ssh_extinfo_build(reply.buf, &n, reply.cap) == 0) // GCOVR_EXCL_LINE  the build half cannot
        {
            emit(i, reply.buf, n); // fail: EXT_INFO is ~90 bytes and buf is SSH_PKT_BUF_SIZE
        }
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_EXT_INFO:
        pc_plaintext_release(mark);
        return 0; // RFC 8308: a client may send its own EXT_INFO; we ignore it

    case SSH_MSG_SERVICE_REQUEST:
        // RFC 4253 §10: a service request is only valid once the key exchange has completed (NEWKEYS
        // advances the phase to SSH_PHASE_SERVICE and turns on encryption). Rejecting it in any earlier
        // phase stops a client from jumping from DH_INIT straight to userauth in cleartext, skipping the
        // whole key exchange + host-key verification. Found by the pentest's ssh_msgtype_abuse.
        if (s->phase != SSH_PHASE_SERVICE)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (pc_ssh_auth_handle_service_request(payload, len, reply.buf, &n, reply.cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        emit(i, reply.buf, n);
        s->phase = SSH_PHASE_AUTH;
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_USERAUTH_REQUEST:
        if (s->phase != SSH_PHASE_AUTH)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (pc_ssh_auth_handle_request(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        emit(i, reply.buf, n); // SUCCESS (→ phase OPEN), PK_OK probe, or FAILURE
#if PC_ENABLE_SSH_ZLIB
        // zlib@openssh.com: the compression stream starts on the FIRST packet AFTER USERAUTH_SUCCESS
        // (which itself just went out uncompressed). Idempotent - a later re-auth cannot restart it.
        if (n > 0 && reply.buf[0] == SSH_MSG_USERAUTH_SUCCESS) // GCOVR_EXCL_LINE  n > 0 always: every handler that
        {
            ssh_comp_on_auth_success(i); // returns 0 has written a reply
        }
#endif
        // Brute-force defense (RFC 4252 §4): bound failed attempts per connection. Only an actual
        // USERAUTH_FAILURE counts - a SUCCESS or the publickey PK_OK probe does not. Too many ->
        // DISCONNECT then close.
        if (n > 0 && reply.buf[0] == SSH_MSG_USERAUTH_FAILURE) // GCOVR_EXCL_LINE  n > 0 always (see above)
        {
            if (++s->auth_failures >= SSH_MAX_AUTH_ATTEMPTS)
            {
                emit_auth_failure_disconnect(i, reply);
                pc_plaintext_release(mark);
                return -1; // close the connection
            }
        }
        pc_plaintext_release(mark);
        return 0;

#if PC_ENABLE_SSH_KEYBOARD_INTERACTIVE
    case SSH_MSG_USERAUTH_INFO_RESPONSE:
        // RFC 4256 §3.4: the client's answer to our keyboard-interactive prompt. Only valid mid-auth,
        // and only when an exchange was armed (the handler enforces the latter). Same SUCCESS/FAILURE
        // accounting as a USERAUTH_REQUEST: SUCCESS starts s2c compression and advances the phase; a
        // FAILURE counts toward the brute-force limit.
        if (s->phase != SSH_PHASE_AUTH)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (pc_ssh_auth_handle_info_response(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        emit(i, reply.buf, n);
#if PC_ENABLE_SSH_ZLIB
        if (n > 0 && reply.buf[0] == SSH_MSG_USERAUTH_SUCCESS) // GCOVR_EXCL_LINE  n > 0 always (see above)
        {
            ssh_comp_on_auth_success(i);
        }
#endif
        if (n > 0 && reply.buf[0] == SSH_MSG_USERAUTH_FAILURE) // GCOVR_EXCL_LINE  n > 0 always (see above)
        {
            if (++s->auth_failures >= SSH_MAX_AUTH_ATTEMPTS)
            {
                emit_auth_failure_disconnect(i, reply);
                pc_plaintext_release(mark);
                return -1;
            }
        }
        pc_plaintext_release(mark);
        return 0;
#endif

    case SSH_MSG_GLOBAL_REQUEST:
        // RFC 4254 §4: connection-wide request (e.g. tcpip-forward for ssh -R). Only
        // meaningful post-auth; reply REQUEST_SUCCESS/FAILURE when want_reply is set.
        if (!s->authed)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (ssh_global_request_handle(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (n > 0)
        {
            emit(i, reply.buf, n);
        }
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_OPEN:
        if (!s->authed)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (pc_ssh_channel_handle_open(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        emit(i, reply.buf, n);
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_OPEN_CONFIRM:
        // The client's reply to a server-initiated forwarded-tcpip open (ssh -R): record
        // the peer window and start the bridge. A stray confirm is ignored (not fatal).
        if (!s->authed)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        pc_ssh_channel_handle_open_confirm(i, payload, len);
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_OPEN_FAILURE:
        // The client refused a server-initiated forwarded-tcpip open: tear the bridge down.
        if (!s->authed)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        pc_ssh_channel_handle_open_failure(i, payload, len);
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_REQUEST:
        if (!s->authed)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (pc_ssh_channel_handle_request(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        emit(i, reply.buf, n); // SUCCESS/FAILURE only when want_reply was set
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_DATA:
        if (!s->authed)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        if (pc_ssh_channel_handle_data(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            pc_plaintext_release(mark);
            return -1;
        }
        emit(i, reply.buf, n); // WINDOW_ADJUST when the receive window is replenished
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_WINDOW_ADJUST:
        pc_ssh_channel_handle_window_adjust(i, payload, len);
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_EOF:
        pc_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_CLOSE:
        // handle_close frames CHANNEL_EOF + CHANNEL_CLOSE back to back, but each SSH
        // message must travel in its own binary packet (RFC 4253 6): a strict peer
        // runs packet_check_eom() after every message and rejects two in one. Emit
        // the two halves as separate packets.
        // GCOVR_EXCL_START  n == 10 cannot be false: handle_close only returns 0 after writing exactly the
        // ten bytes of CHANNEL_EOF + CHANNEL_CLOSE, so that half of the guard is unreachable.
        if (pc_ssh_channel_handle_close(i, payload, len, reply.buf, &n, reply.cap) == 0 && n == 10)
        {
            emit(i, reply.buf, 5);     // CHANNEL_EOF
            emit(i, reply.buf + 5, 5); // CHANNEL_CLOSE
        }
        // GCOVR_EXCL_STOP
        pc_plaintext_release(mark);
        return 0;

    default: {
        // RFC 4253 §11.4: reply to an unrecognized message with
        // SSH_MSG_UNIMPLEMENTED carrying the rejected packet's sequence number.
        // ssh_pkt_recv() has already incremented seq_no_recv past this packet,
        // so the rejected packet's number is seq_no_recv - 1.
        uint32_t rej = ssh_pkt[i].seq_no_recv - 1u;
        reply.buf[0] = SSH_MSG_UNIMPLEMENTED;
        reply.buf[1] = (uint8_t)(rej >> 24);
        reply.buf[2] = (uint8_t)(rej >> 16);
        reply.buf[3] = (uint8_t)(rej >> 8);
        reply.buf[4] = (uint8_t)(rej);
        emit(i, reply.buf, 5);
        pc_plaintext_release(mark);
        return 0;
    }
    }
    pc_plaintext_release(mark);
}
