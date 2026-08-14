// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file auth.c
 * @brief RFC 4252 user authentication: service request, publickey, password, keyboard-interactive.
 */

#include "network_drivers/presentation/ssh/auth/auth.h"
#include "crypto/asymmetric/ecdsa.h"   // protocore_ecdsa_p256_verify() (ecdsa-sha2-nistp256)
#include "crypto/asymmetric/ed25519.h" // protocore_ed25519_verify() (ssh-ed25519 client keys)
#include "mmgr/bytes.h"                // bytes.rd_str() - the RFC 4251 sec 5 string reader
#include "mmgr/endian.h"               // endian.wr32be() - the one source of truth for wire integers
#include "mmgr/plaintext.h"            // protocore_plaintext_span() for the verify buffers
#include "mmgr/protomem.h"
#include "mmgr/protostr.h" // str.eq() - the bounded string compare the wire fields use
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/connection/connection.h" // ssh_connection_dispatch()
#include "network_drivers/presentation/ssh/network/network.h"       // SshNetwork.emit()
#include "network_drivers/presentation/ssh/transport/transport.h"   // ssh_sess[], SshPhase
#include "network_drivers/presentation/ssh/transport/ssh_rsa.h"                             // protocore_rsa_verify(), PROTOCORE_RSA_KEY_BYTES
#include "server/clock/clock.h" // protocore_millis(): the password-change cooldown clock
#if PROTOCORE_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/comp.h" // ssh_comp_on_auth_success()
#endif

// Defined below; the RFC 4252 sec 7 handler above it frames a PK_OK through this.
static int build_pk_ok(const SshAuthReq *req, uint8_t *out, size_t *out_len, size_t cap);

// Defined below; the sec 5.1 reply paths above them build through these.
void protocore_ssh_auth_build_failure(struct SshAuthInternal *restrict ctx);
void protocore_ssh_auth_build_success(struct SshAuthInternal *restrict ctx);

// ---------------------------------------------------------------------------
// Application password callback
// ---------------------------------------------------------------------------

// All SSH auth state, owned by one instance (internal linkage). One named owner, unreachable from
// any other translation unit.
struct SshAuthStorage
{
    // When the last change started, server-wide: a reconnect does not clear it, so the cooldown
    // bounds changes per box rather than per connection.
    uint32_t pw_change_last_ms;
    SshPwChange pw_change[MAX_SSH_CONNS]; ///< Per-slot flight state; OK/FAIL is what a poll drains.
    // sec 4: "the implementation SHOULD limit the number of failed authentication attempts a client
    // may perform in a single session". Per slot, counted only on an actual USERAUTH_FAILURE.
    uint8_t failures[MAX_SSH_CONNS];
    // sec 4: "The server SHOULD have a timeout for authentication and disconnect if the
    // authentication has not been accepted within the timeout period." Stamped when the slot is
    // first polled, which is the pass after it was accepted.
    uint32_t started_ms[MAX_SSH_CONNS];
    proto_bool started[MAX_SSH_CONNS];
    // sec 5: the user name and service name a slot's accumulated state belongs to. Both are checked
    // on every request, and the state is flushed when either changes.
    struct
    {
        char user[SSH_AUTH_USER_MAX];
        char service[32];
        proto_bool known; ///< false until the slot's first USERAUTH_REQUEST names a pair
    } ident[MAX_SSH_CONNS];
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    // Per-slot keyboard-interactive exchange state: armed by a "keyboard-interactive" USERAUTH_REQUEST
    // (we send one INFO_REQUEST), consumed by the matching INFO_RESPONSE. The user is remembered across
    // the round-trip since the INFO_RESPONSE does not carry it.
    struct
    {
        proto_bool pending;
        char user[SSH_AUTH_USER_MAX];
    } ki[MAX_SSH_CONNS];
#endif
};

/**
 * @brief The layer's state and the calls that reach it - what SshAuthNs points at.
 *
 * @var SshAuthInternal::store         the per-slot failure counts, timers, identities and flights
 * @var SshAuthInternal::ns            the handle a caller sets a call's members on
 * @var SshAuthInternal::pw_cb         the application password verifier (RFC 4252 sec 8)
 * @var SshAuthInternal::pw_change_cb  what a change request is handed to (sec 8)
 * @var SshAuthInternal::pk_cb         the application public-key verifier (sec 7)
 */
struct SshAuthInternal
{
    struct SshAuthStorage *store;
    SshAuthNs *ns;
    SshPasswordCb pw_cb;
    SshPasswordChangeCb pw_change_cb;
    SshPubkeyCb pk_cb;
};

static struct SshAuthStorage s_store;

static struct SshAuthInternal s_auth = {.store = &s_store, .ns = &SshAuth};

// Count one failure for slot @p i. At the threshold, emit the sec 4 DISCONNECT into @p out and
// report that the caller should close. SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE is the reason
// RFC 4250 sec 4.2.2 assigns to an exhausted authentication.
static proto_bool auth_failure_over_threshold(uint8_t i, protocore_span out)
{
    if (i >= MAX_SSH_CONNS || ++s_store.failures[i] < SSH_MAX_AUTH_ATTEMPTS)
    {
        return PROTO_FALSE;
    }
    static const char desc[] = "too many authentication failures";
    size_t n = 0;
    if (ssh_pkt_build_disconnect(SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE, desc, sizeof(desc) - 1, out.buf, &n,
                                 out.cap) == 0)
    {
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = out.buf;
        SshNetwork.msg.len = n;
        SshNetwork.emit(SshNetwork.internal);
    }
    return PROTO_TRUE;
}

// Everything one slot accumulated across authentication attempts: the armed keyboard-interactive
// exchange and the deferred password change. Wiped rather than cleared - both hold a user name.
static void auth_flush_state(uint8_t i)
{
    s_store.pw_change[i] = PROTOCORE_SSH_PW_CHANGE_NONE;
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    s_store.ki[i].pending = PROTO_FALSE;
    protocore_secure_wipe(s_store.ki[i].user, sizeof(s_store.ki[i].user));
#endif
}

// RFC 4252 sec 5: check the user and service this request names against the pair the slot's state
// belongs to, and flush that state when either changes.
static void auth_identity_check(uint8_t i, const char *user, const char *service)
{
    const proto_bool same = s_store.ident[i].known &&
                            str.eq(s_store.ident[i].user, user, sizeof(s_store.ident[i].user), PROTO_FALSE) &&
                            str.eq(s_store.ident[i].service, service, sizeof(s_store.ident[i].service), PROTO_FALSE);
    if (same)
    {
        return;
    }
    if (s_store.ident[i].known)
    {
        auth_flush_state(i);
    }
    str.copy(s_store.ident[i].user, user, sizeof(s_store.ident[i].user));
    str.copy(s_store.ident[i].service, service, sizeof(s_store.ident[i].service));
    s_store.ident[i].known = PROTO_TRUE;
}

void protocore_ssh_auth_write_publickey_request(struct SshAuthInternal *restrict ctx)
{
    protocore_span *w = ctx->ns->out_args.w;
    const uint8_t *sid = ctx->ns->userauth.sid;
    const size_t sid_len = ctx->ns->userauth.sid_len;
    const char *user = ctx->ns->userauth.user;
    const char *service = ctx->ns->userauth.service;
    const char *pk_algo = ctx->ns->userauth.pk_algo;
    const uint8_t *pk_blob = ctx->ns->userauth.pk_blob;
    const size_t pk_len = ctx->ns->userauth.pk_len;
    if (sid != NULL)
    {
        protocore_ssh_wr_str(w, sid, sid_len);
    }
    bytes.put(w, SSH_MSG_USERAUTH_REQUEST);
    protocore_ssh_wr_cstr(w, user);
    protocore_ssh_wr_cstr(w, service);
    protocore_ssh_wr_cstr(w, "publickey");
    bytes.put(w, 1); // signature present
    protocore_ssh_wr_cstr(w, pk_algo);
    protocore_ssh_wr_str(w, pk_blob, pk_len);
}

void protocore_ssh_auth_timed_out(struct SshAuthInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->slot;
    if (i >= MAX_SSH_CONNS || SSH_AUTH_TIMEOUT_MS == 0u || ssh_phase_auth_complete(i))
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    Clock.millis(Clock.internal);
    const uint32_t now = Clock.ms;
    if (!s_store.started[i])
    {
        s_store.started_ms[i] = now;
        s_store.started[i] = PROTO_TRUE;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    // Unsigned subtraction, so a wrap of the millisecond counter yields the true elapsed time.
    ctx->ns->ok = (uint32_t)(now - s_store.started_ms[i]) >= (uint32_t)SSH_AUTH_TIMEOUT_MS;
    return;
}

void protocore_ssh_auth_reset(struct SshAuthInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->slot;
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    auth_flush_state(i); // the cooldown stamp survives: it is server-wide
    s_store.failures[i] = 0;
    s_store.started[i] = PROTO_FALSE;
    s_store.ident[i].known = PROTO_FALSE;
    protocore_secure_wipe(s_store.ident[i].user, sizeof(s_store.ident[i].user));
    protocore_secure_wipe(s_store.ident[i].service, sizeof(s_store.ident[i].service));
}

void protocore_ssh_auth_set_password_cb(struct SshAuthInternal *restrict ctx)
{
    SshPasswordCb cb = ctx->ns->cbs.password_cb;
    s_auth.pw_cb = cb;
}

void protocore_ssh_auth_set_password_change_cb(struct SshAuthInternal *restrict ctx)
{
    SshPasswordChangeCb cb = ctx->ns->cbs.password_change_cb;
    s_auth.pw_change_cb = cb;
}

void protocore_ssh_auth_set_pubkey_cb(struct SshAuthInternal *restrict ctx)
{
    SshPubkeyCb cb = ctx->ns->cbs.pubkey_cb;
    s_auth.pk_cb = cb;
}

// ---------------------------------------------------------------------------
// publickey method (RFC 4252 sec 7)
// ---------------------------------------------------------------------------

// Validate the offered key (a signature-less probe -> PK_OK) or verify the signature over
// string(session_id) || signed_prefix, keying success to connection i.
static int protocore_ssh_auth_handle_pubkey(uint8_t i, const SshAuthReq *req, uint8_t *out, size_t *out_len, size_t cap)
{
    // sec 7: the offered algorithm name is checked before the key it came with. "If the server does
    // not support some algorithm, it MUST simply reject the request" - so an unsupported name is a
    // FAILURE, never a PK_OK echoing a name this end cannot then verify a signature under.
    proto_bool key_ok = ssh_pubkey_algo_supported(req->pk_algo, req->pk_blob, req->pk_blob_len) &&
                        ssh_pubkey_blob_valid(req->pk_blob, req->pk_blob_len) && s_auth.pk_cb &&
                        s_auth.pk_cb(req->user, req->pk_blob, req->pk_blob_len);
    if (!key_ok)
    {
        s_auth.ns->out_args.out = out;
        s_auth.ns->out_args.cap = cap;
        s_auth.ns->partial = PROTO_FALSE;
        protocore_ssh_auth_build_failure(&s_auth);
        *out_len = s_auth.ns->out_args.out_len;
        return s_auth.ns->i32;
    }

    if (!req->has_signature)
    {
        return build_pk_ok(req, out, out_len, cap); // probe: ask for a signature
    }

    // The signature covers string(session_id) || signed_prefix. The session_id is the first KEX's
    // exchange hash: 32 bytes (SHA-256 methods) or 64 (sntrup761x25519-sha512). Without a completed KEX
    // there is no session id, and signing over an empty one binds the signature to no session at all
    // (RFC 4252 sec 7), so the signature is refused rather than verified against that.
    size_t sid_len = 0;
    const uint8_t *sid = ssh_session_id(i, &sid_len);
    if (sid == NULL)
    {
        s_auth.ns->out_args.out = out;
        s_auth.ns->out_args.cap = cap;
        s_auth.ns->partial = PROTO_FALSE;
        protocore_ssh_auth_build_failure(&s_auth);
        *out_len = s_auth.ns->out_args.out_len;
        return s_auth.ns->i32;
    }
    size_t mark = protocore_plaintext_mark();
    protocore_span signed_data = protocore_plaintext_span(SSH_PKT_BUF_SIZE + 4 + SSH_KEXHASH_MAX_LEN, 4);
    if (!span.ok(signed_data))
    {
        protocore_plaintext_release(mark);
        s_auth.ns->out_args.out = out;
        s_auth.ns->out_args.cap = cap;
        s_auth.ns->partial = PROTO_FALSE;
        protocore_ssh_auth_build_failure(&s_auth);
        *out_len = s_auth.ns->out_args.out_len;
        return s_auth.ns->i32; // arena exhausted: fail closed
    }
    if (req->signed_prefix_len > SSH_PKT_BUF_SIZE || 4 + sid_len + req->signed_prefix_len > signed_data.cap)
    {
        protocore_plaintext_release(mark);
        s_auth.ns->out_args.out = out;
        s_auth.ns->out_args.cap = cap;
        s_auth.ns->partial = PROTO_FALSE;
        protocore_ssh_auth_build_failure(&s_auth);
        *out_len = s_auth.ns->out_args.out_len;
        return s_auth.ns->i32;
    }
    size_t sd = 0;
    endian.wr32be(signed_data.buf + sd, (uint32_t)sid_len);
    sd += 4;
    mem.cpy(signed_data.buf + sd, sid, sid_len);
    sd += sid_len;
    mem.cpy(signed_data.buf + sd, req->signed_prefix, req->signed_prefix_len);
    sd += req->signed_prefix_len;

    proto_bool sig_ok = ssh_pubkey_verify(i, req->pk_algo, req->pk_blob, req->pk_blob_len, req->signature,
                                          req->signature_len, signed_data.buf, sd);
    protocore_plaintext_release(mark);
    if (sig_ok)
    {
        ssh_phase_auth_done(i);
        s_auth.ns->out_args.out = out;
        s_auth.ns->out_args.cap = cap;
        protocore_ssh_auth_build_success(&s_auth);
        *out_len = s_auth.ns->out_args.out_len;
        return s_auth.ns->i32;
    }
    s_auth.ns->out_args.out = out;
    s_auth.ns->out_args.cap = cap;
    s_auth.ns->partial = PROTO_FALSE;
    protocore_ssh_auth_build_failure(&s_auth);
    *out_len = s_auth.ns->out_args.out_len;
    return s_auth.ns->i32;
}

// ---------------------------------------------------------------------------
// USERAUTH_REQUEST parse (RFC 4252 §5)
// ---------------------------------------------------------------------------

void protocore_ssh_auth_parse_request(struct SshAuthInternal *restrict ctx)
{
    const uint8_t *payload = ctx->ns->msg.payload;
    const size_t len = ctx->ns->msg.len;
    SshAuthReq *req = ctx->ns->req;
    mem.set(req, 0, sizeof(*req));
    if (len < 1 || payload[0] != SSH_MSG_USERAUTH_REQUEST)
    {
        ctx->ns->i32 = -1;
        return;
    }

    size_t off = 1;
    if (!read_string(payload, len, &off, req->user, sizeof(req->user)))
    {
        ctx->ns->i32 = -1;
        return;
    }
    if (!read_string(payload, len, &off, req->service, sizeof(req->service)))
    {
        ctx->ns->i32 = -1;
        return;
    }
    // RFC 4252 sec 5: the service to start after auth must be one the server offers, and it sits
    // inside the signed blob, so a service the server never checks is one the signature does not bind.
    if (!str.eq(req->service, "ssh-connection", sizeof(req->service), PROTO_FALSE))
    {
        ctx->ns->i32 = -1;
        return;
    }
    if (!read_string(payload, len, &off, req->method, sizeof(req->method)))
    {
        ctx->ns->i32 = -1;
        return;
    }

    if (str.eq(req->method, "password", sizeof(req->method), PROTO_FALSE))
    {
        // boolean (FALSE = not a password change) || string password [|| string new-password]
        if (off >= len)
        {
            ctx->ns->i32 = -1;
            return;
        }
        req->is_pw_change = payload[off] != 0; // RFC 4252 sec 8: TRUE means old || new
        off++;
        if (!read_string(payload, len, &off, req->password, sizeof(req->password)))
        {
            ctx->ns->i32 = -1;
            return;
        }
        // A change request carries the new password too; the handler routes it to the change callback.
        if (req->is_pw_change && !read_string(payload, len, &off, req->new_password, sizeof(req->new_password)))
        {
            ctx->ns->i32 = -1;
            return;
        }
        req->is_password = PROTO_TRUE;
    }
    else if (str.eq(req->method, "publickey", sizeof(req->method), PROTO_FALSE))
    {
        // boolean has_signature || string algo || string pubkey-blob [|| string signature]
        if (off >= len)
        {
            ctx->ns->i32 = -1;
            return;
        }
        req->has_signature = payload[off++] != 0;
        if (!read_string(payload, len, &off, req->pk_algo, sizeof(req->pk_algo)))
        {
            ctx->ns->i32 = -1;
            return;
        }
        if (!bytes.rd_str(payload, len, &off, &req->pk_blob, &req->pk_blob_len))
        {
            ctx->ns->i32 = -1;
            return;
        }

        // Everything parsed so far is exactly the data the signature covers.
        req->signed_prefix = payload;
        req->signed_prefix_len = off;

        if (req->has_signature)
        {
            const uint8_t *sigblob;
            uint32_t sigblob_len;
            if (!bytes.rd_str(payload, len, &off, &sigblob, &sigblob_len))
            {
                ctx->ns->i32 = -1;
                return;
            }
            // signature blob = string(sig-algo) || string(raw-signature)
            size_t so = 0;
            const uint8_t *salgo;
            uint32_t salgo_len;
            if (!bytes.rd_str(sigblob, sigblob_len, &so, &salgo, &salgo_len))
            {
                ctx->ns->i32 = -1;
                return;
            }
            if (!bytes.rd_str(sigblob, sigblob_len, &so, &req->signature, &req->signature_len))
            {
                ctx->ns->i32 = -1;
                return;
            }
        }
        req->is_pubkey = PROTO_TRUE;
    }
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    else if (str.eq(req->method, "keyboard-interactive", sizeof(req->method), PROTO_FALSE))
    {
        // RFC 4256 §3.1: string(language tag, deprecated) || string(submethods). Both are ignored -
        // this server always drives a single "Password:" prompt.
        req->is_kbdint = PROTO_TRUE;
    }
#endif
    ctx->ns->i32 = 0;
    return;
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

void protocore_ssh_auth_build_failure(struct SshAuthInternal *restrict ctx)
{
    uint8_t *out = ctx->ns->out_args.out;
    size_t *out_len = &ctx->ns->out_args.out_len;
    const size_t cap = ctx->ns->out_args.cap;
    const proto_bool partial = ctx->ns->partial;
    // SSH_MSG_USERAUTH_FAILURE || name-list(authentications) || boolean(partial)
#if PROTOCORE_SSH_ALLOW_PASSWORD
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    static const char methods[] = "publickey,password,keyboard-interactive";
#else
    static const char methods[] = "publickey,password";
#endif
#else
    static const char methods[] = "publickey"; // password auth disabled for hardening
#endif
    uint32_t ml = (uint32_t)(sizeof(methods) - 1);
    if (cap < 1 + 4 + ml + 1)
    {
        ctx->ns->i32 = -1;
        return;
    }
    out[0] = SSH_MSG_USERAUTH_FAILURE;
    endian.wr32be(out + 1, ml);
    mem.cpy(out + 5, methods, ml);
    out[5 + ml] = partial ? 1 : 0;
    *out_len = 5 + ml + 1;
    ctx->ns->i32 = 0;
    return;
}

void protocore_ssh_auth_build_success(struct SshAuthInternal *restrict ctx)
{
    uint8_t *out = ctx->ns->out_args.out;
    size_t *out_len = &ctx->ns->out_args.out_len;
    const size_t cap = ctx->ns->out_args.cap;
    if (cap < 1)
    {
        ctx->ns->i32 = -1;
        return;
    }
    out[0] = SSH_MSG_USERAUTH_SUCCESS;
    *out_len = 1;
    ctx->ns->i32 = 0;
    return;
}

// SSH_MSG_USERAUTH_PK_OK || string(algo) || string(blob) - the "this key would
// be accepted, send a signature" probe response (RFC 4252 §7).
static int build_pk_ok(const SshAuthReq *req, uint8_t *out, size_t *out_len, size_t cap)
{
    uint32_t al = (uint32_t)str.len(req->pk_algo, sizeof(req->pk_algo));
    if (cap < (size_t)1 + 4 + al + 4 + req->pk_blob_len)
    {
        return -1;
    }
    size_t o = 0;
    out[o++] = SSH_MSG_USERAUTH_PK_OK;
    endian.wr32be(out + o, al);
    o += 4;
    mem.cpy(out + o, req->pk_algo, al);
    o += al;
    endian.wr32be(out + o, req->pk_blob_len);
    o += 4;
    mem.cpy(out + o, req->pk_blob, req->pk_blob_len);
    o += req->pk_blob_len;
    *out_len = o;
    return 0;
}

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
// SSH_MSG_USERAUTH_INFO_REQUEST (RFC 4256 §3.2): empty name/instruction/language, one non-echoed
// "Password: " prompt. This is the challenge-response face of password auth (a single prompt).
static int build_info_request(uint8_t *out, size_t *out_len, size_t cap)
{
    static const char prompt[] = "Password: ";
    const uint32_t pl = (uint32_t)(sizeof(prompt) - 1);
    const size_t need = 1 + 4 + 4 + 4 + 4 + 4 + pl + 1; // msg,name,instr,lang,num-prompts,prompt,echo
    if (cap < need)
    {
        return -1;
    }
    size_t o = 0;
    out[o++] = SSH_MSG_USERAUTH_INFO_REQUEST;
    endian.wr32be(out + o, 0); // name = ""
    o += 4;
    endian.wr32be(out + o, 0); // instruction = ""
    o += 4;
    endian.wr32be(out + o, 0); // language tag = "" (deprecated)
    o += 4;
    endian.wr32be(out + o, 1); // num-prompts = 1
    o += 4;
    endian.wr32be(out + o, pl);
    o += 4;
    mem.cpy(out + o, prompt, pl);
    o += pl;
    out[o++] = 0; // echo = FALSE (the response is a password)
    *out_len = o;
    return 0;
}
#endif

// ---------------------------------------------------------------------------
// Orchestration

void protocore_ssh_auth_handle_request(struct SshAuthInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->slot;
    const uint8_t *payload = ctx->ns->msg.payload;
    const size_t len = ctx->ns->msg.len;
    uint8_t *out = ctx->ns->out_args.out;
    size_t *out_len = &ctx->ns->out_args.out_len;
    const size_t cap = ctx->ns->out_args.cap;
    if (i >= MAX_SSH_CONNS)
    {
        ctx->ns->i32 = -1;
        return;
    }

    SshAuthReq req;
    ctx->ns->msg.payload = payload;
    ctx->ns->msg.len = len;
    ctx->ns->req = &req;
    protocore_ssh_auth_parse_request(ctx);
    if (ctx->ns->i32 != 0)
    {
        ctx->ns->i32 = -1;
        return;
    }

    // sec 5: "The 'user name' and 'service name' are repeated in every new authentication attempt,
    // and MAY change. The server implementation MUST carefully check them in every message, and
    // MUST flush any accumulated authentication states if they change."
    auth_identity_check(i, req.user, req.service);

    // ---- publickey method (RFC 4252 §7) ----
    if (req.is_pubkey)
    {
        ctx->ns->i32 = protocore_ssh_auth_handle_pubkey(i, &req, out, out_len, cap);
        return;
    }

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    // ---- keyboard-interactive method (RFC 4256): arm the exchange and send one "Password:" prompt.
    if (req.is_kbdint)
    {
        if (!s_auth.pw_cb) // no verifier installed -> cannot challenge
        {
            ctx->ns->out_args.out = out;
    ctx->ns->out_args.cap = cap;
    ctx->ns->partial = PROTO_FALSE;
    protocore_ssh_auth_build_failure(ctx);
    *out_len = ctx->ns->out_args.out_len;
            return;
        }
        s_store.ki[i].pending = PROTO_TRUE;
        size_t ul = str.len(req.user, sizeof(s_store.ki[i].user) - 1);
        mem.cpy(s_store.ki[i].user, req.user, ul);
        s_store.ki[i].user[ul] = '\0';
        ctx->ns->i32 = build_info_request(out, out_len, cap);
        return;
    }
#endif

    // ---- password method (RFC 4252 §8) ----
    // Password auth can be compiled out for publickey-only hardening.
#if PROTOCORE_SSH_ALLOW_PASSWORD
    // Password change (RFC 4252 sec 8): hand it to the start callback, which returns at once, and
    // leave the reply for the poll that drains the reported outcome. A change still in flight, or one
    // inside the cooldown, is refused now. The unsigned difference wraps with the millis clock.
    if (req.is_password && req.is_pw_change)
    {
        Clock.millis(Clock.internal);
        uint32_t now = Clock.ms;
        uint32_t elapsed = now - s_store.pw_change_last_ms;
        if (s_store.pw_change[i] == PROTOCORE_SSH_PW_CHANGE_NONE && elapsed >= PROTOCORE_SSH_PW_CHANGE_COOLDOWN_MS &&
            s_auth.pw_change_cb != NULL)
        {
            s_store.pw_change_last_ms = now;
            s_store.pw_change[i] = PROTOCORE_SSH_PW_CHANGE_BUSY;
            s_auth.pw_change_cb(i, req.user, req.password, req.new_password);
            protocore_secure_wipe(req.password, sizeof(req.password));
            protocore_secure_wipe(req.new_password, sizeof(req.new_password));
            *out_len = 0; // the reply follows once the application reports
            ctx->ns->i32 = 0;
            return;
        }
        protocore_secure_wipe(req.password, sizeof(req.password));
        protocore_secure_wipe(req.new_password, sizeof(req.new_password));
        ctx->ns->out_args.out = out;
    ctx->ns->out_args.cap = cap;
    ctx->ns->partial = PROTO_FALSE;
    protocore_ssh_auth_build_failure(ctx);
    *out_len = ctx->ns->out_args.out_len;
        return;
    }

    proto_bool ok = PROTO_FALSE;
    if (req.is_password)
    {
        ok = s_auth.pw_cb != NULL && s_auth.pw_cb(req.user, req.password);
    }
#else
    proto_bool ok = PROTO_FALSE;
#endif

    // Wipe both passwords from the stack regardless of the outcome.
    protocore_secure_wipe(req.password, sizeof(req.password));
    protocore_secure_wipe(req.new_password, sizeof(req.new_password));

    if (ok)
    {
        ssh_phase_auth_done(i);
        ctx->ns->out_args.out = out;
    ctx->ns->out_args.cap = cap;
    protocore_ssh_auth_build_success(ctx);
    *out_len = ctx->ns->out_args.out_len;
        return;
    }
    ctx->ns->out_args.out = out;
    ctx->ns->out_args.cap = cap;
    ctx->ns->partial = PROTO_FALSE;
    protocore_ssh_auth_build_failure(ctx);
    *out_len = ctx->ns->out_args.out_len;
    return;
}

void protocore_ssh_auth_pw_change_report(struct SshAuthInternal *restrict ctx)
{
    const uint8_t slot = ctx->ns->slot;
    const proto_bool ok = ctx->ns->ok;
    if (slot >= MAX_SSH_CONNS || s_store.pw_change[slot] != PROTOCORE_SSH_PW_CHANGE_BUSY)
    {
        return;
    }
    if (ok)
    {
        s_store.pw_change[slot] = PROTOCORE_SSH_PW_CHANGE_OK;
    }
    else
    {
        s_store.pw_change[slot] = PROTOCORE_SSH_PW_CHANGE_FAIL;
    }
}

void protocore_ssh_auth_pw_change_clear(struct SshAuthInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->slot;
    if (i < MAX_SSH_CONNS)
    {
        s_store.pw_change[i] = PROTOCORE_SSH_PW_CHANGE_NONE;
    }
}

SshPwChange protocore_ssh_auth_pw_change_take(uint8_t i)
{
    if (i >= MAX_SSH_CONNS || s_store.pw_change[i] == PROTOCORE_SSH_PW_CHANGE_NONE ||
        s_store.pw_change[i] == PROTOCORE_SSH_PW_CHANGE_BUSY)
    {
        return PROTOCORE_SSH_PW_CHANGE_NONE;
    }
    SshPwChange r = s_store.pw_change[i];
    s_store.pw_change[i] = PROTOCORE_SSH_PW_CHANGE_NONE;
    if (r == PROTOCORE_SSH_PW_CHANGE_OK)
    {
        ssh_phase_auth_done(i);
    }
    return r;
}

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
void protocore_ssh_auth_handle_info_response(struct SshAuthInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->slot;
    const uint8_t *payload = ctx->ns->msg.payload;
    const size_t len = ctx->ns->msg.len;
    uint8_t *out = ctx->ns->out_args.out;
    size_t *out_len = &ctx->ns->out_args.out_len;
    const size_t cap = ctx->ns->out_args.cap;
    if (i >= MAX_SSH_CONNS)
    {
        ctx->ns->i32 = -1;
        return;
    }
    if (!s_store.ki[i].pending) // no keyboard-interactive exchange armed for this slot
    {
        ctx->ns->i32 = -1;
        return;
    }
    s_store.ki[i].pending = PROTO_FALSE; // consume the exchange regardless of outcome

    // SSH_MSG_USERAUTH_INFO_RESPONSE (RFC 4256 §3.4): byte(61) || uint32 num-responses || string[num].
    // We sent one prompt, so exactly one response is expected.
    if (len < 1 || payload[0] != SSH_MSG_USERAUTH_INFO_RESPONSE)
    {
        ctx->ns->i32 = -1;
        return;
    }
    size_t off = 1;
    if (off + 4 > len)
    {
        ctx->ns->i32 = -1;
        return;
    }
    uint32_t nr = ((uint32_t)payload[off] << 24) | ((uint32_t)payload[off + 1] << 16) |
                  ((uint32_t)payload[off + 2] << 8) | (uint32_t)payload[off + 3];
    off += 4;

    char resp[SSH_AUTH_PASS_MAX];
    proto_bool ok = PROTO_FALSE;
    if (nr == 1 && read_string(payload, len, &off, resp, sizeof(resp)))
    {
        ok = s_auth.pw_cb && s_auth.pw_cb(s_store.ki[i].user, resp);
    }

    // Wipe the response and the remembered user from memory regardless of outcome.
    protocore_secure_wipe(resp, sizeof(resp));
    protocore_secure_wipe(s_store.ki[i].user, sizeof(s_store.ki[i].user));

    if (ok)
    {
        ssh_phase_auth_done(i);
        ctx->ns->out_args.out = out;
    ctx->ns->out_args.cap = cap;
    protocore_ssh_auth_build_success(ctx);
    *out_len = ctx->ns->out_args.out_len;
        return;
    }
    ctx->ns->out_args.out = out;
    ctx->ns->out_args.cap = cap;
    ctx->ns->partial = PROTO_FALSE;
    protocore_ssh_auth_build_failure(ctx);
    *out_len = ctx->ns->out_args.out_len;
    return;
}
#endif

// ---------------------------------------------------------------------------
// RFC 4252 - message numbers 50 to 79, and the privilege to go higher
// ---------------------------------------------------------------------------

void ssh_auth_dispatch(struct SshAuthInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->slot;
    const uint8_t msg_type = ctx->ns->msg_type;
    const uint8_t *payload = ctx->ns->msg.payload;
    const size_t len = ctx->ns->msg.len;
    if (i >= MAX_SSH_CONNS)
    {
        ctx->ns->i32 = -1;
        return;
    }
    // The reply buffer is borrowed for this dispatch, not carried on the worker stack: it is the
    // single largest frame on the SSH path and the handshake below it is the deepest call chain in
    // the library. protocore_plaintext_span binds the capacity to the allocation.
    size_t mark = protocore_plaintext_mark();
    protocore_span reply = protocore_plaintext_span(SSH_PKT_BUF_SIZE, 16);
    if (!span.ok(reply))
    {
        protocore_plaintext_release(mark);
        ctx->ns->i32 = -1;
        return; // arena exhausted: fail closed, the caller drops the connection
    }
    size_t n = 0;

    switch (msg_type)
    {
    case SSH_MSG_USERAUTH_REQUEST:
        // RFC 4252 sec 5.1: a request that arrives after SUCCESS is silently ignored, not an error.
        if (ssh_phase_auth_complete(i))
        {
            protocore_plaintext_release(mark);
            ctx->ns->i32 = 0;
            return;
        }
        if (!ssh_phase_admits_userauth(i))
        {
            protocore_plaintext_release(mark);
            ctx->ns->i32 = -1;
            return;
        }
        ctx->ns->slot = i;
    ctx->ns->msg.payload = payload;
    ctx->ns->msg.len = len;
    ctx->ns->out_args.out = reply.buf;
    ctx->ns->out_args.cap = reply.cap;
    protocore_ssh_auth_handle_request(ctx);
    n = ctx->ns->out_args.out_len;
    if (ctx->ns->i32 != 0)
        {
            protocore_plaintext_release(mark);
            ctx->ns->i32 = -1;
            return;
        }
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = reply.buf;
        SshNetwork.msg.len = n;
        SshNetwork.emit(SshNetwork.internal); // SUCCESS (→ phase OPEN), PK_OK probe, or FAILURE
#if PROTOCORE_ENABLE_SSH_ZLIB
        // zlib@openssh.com: the compression stream starts on the FIRST packet AFTER USERAUTH_SUCCESS
        // (which itself just went out uncompressed). Idempotent - a later re-auth cannot restart it.
        if (n > 0 && reply.buf[0] == SSH_MSG_USERAUTH_SUCCESS)
        {
            ssh_comp_on_auth_success(i); // returns 0 has written a reply
        }
#endif
        // sec 4: bound failed attempts per session. Only an actual USERAUTH_FAILURE counts - a
        // SUCCESS or the publickey PK_OK probe does not. Past the threshold, disconnect.
        if (n > 0 && reply.buf[0] == SSH_MSG_USERAUTH_FAILURE && auth_failure_over_threshold(i, reply))
        {
            protocore_plaintext_release(mark);
            ctx->ns->i32 = -1;
            return; // close the connection
        }
        protocore_plaintext_release(mark);
        ctx->ns->i32 = 0;
        return;

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    case SSH_MSG_USERAUTH_INFO_RESPONSE:
        // RFC 4256 §3.4: the client's answer to our keyboard-interactive prompt. Only valid mid-auth,
        // and only when an exchange was armed (the handler enforces the latter). Same SUCCESS/FAILURE
        // accounting as a USERAUTH_REQUEST: SUCCESS starts s2c compression and advances the phase; a
        // FAILURE counts toward the brute-force limit.
        //
        // RFC 4252 sec 5.1 covers this one too: an answer arriving after SUCCESS is one of the
        // "further authentication requests" that is silently ignored, not a reason to disconnect.
        if (ssh_phase_auth_complete(i))
        {
            protocore_plaintext_release(mark);
            ctx->ns->i32 = 0;
            return;
        }
        if (!ssh_phase_admits_userauth(i))
        {
            protocore_plaintext_release(mark);
            ctx->ns->i32 = -1;
            return;
        }
        if (protocore_ssh_auth_handle_info_response(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            protocore_plaintext_release(mark);
            ctx->ns->i32 = -1;
            return;
        }
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = reply.buf;
        SshNetwork.msg.len = n;
        SshNetwork.emit(SshNetwork.internal);
#if PROTOCORE_ENABLE_SSH_ZLIB
        if (n > 0 && reply.buf[0] == SSH_MSG_USERAUTH_SUCCESS)
        {
            ssh_comp_on_auth_success(i);
        }
#endif
        if (n > 0 && reply.buf[0] == SSH_MSG_USERAUTH_FAILURE && auth_failure_over_threshold(i, reply))
        {
            protocore_plaintext_release(mark);
            ctx->ns->i32 = -1;
            return;
        }
        protocore_plaintext_release(mark);
        ctx->ns->i32 = 0;
        return;
#endif

    default:
        break;
    }

    // sec 6: 80 and above belong to the service that runs after this one. "Receiving one of them
    // before authentication is complete is an error, to which the server MUST respond by
    // disconnecting, preferably with a proper disconnect message sent to ease troubleshooting."
    if (msg_type >= SSH_MSG_GLOBAL_REQUEST)
    {
        if (!ssh_phase_auth_complete(i))
        {
            static const char desc[] = "connection protocol message before authentication";
            size_t dn = 0;
            if (ssh_pkt_build_disconnect(SSH_DISCONNECT_PROTOCOL_ERROR, desc, sizeof(desc) - 1, reply.buf, &dn,
                                         reply.cap) == 0)
            {
                SshNetwork.ssh_slot = i;
                SshNetwork.msg.payload = reply.buf;
                SshNetwork.msg.len = dn;
                SshNetwork.emit(SshNetwork.internal);
            }
            protocore_plaintext_release(mark);
            ctx->ns->i32 = -1;
            return;
        }
        protocore_plaintext_release(mark);
        SshConnection.chan.slot = i;
        SshConnection.msg_type = msg_type;
        SshConnection.chan.payload = payload;
        SshConnection.chan.len = len;
        SshConnection.dispatch(SshConnection.internal);
        ctx->ns->i32 = SshConnection.i32;
        return;
    }

    // Anything else is a message number this end does not recognize, which RFC 4253 sec 11.4
    // answers rather than treats as fatal.
    size_t un = 0;
    if (ssh_pkt_unimplemented(i, reply.buf, &un, reply.cap) == 0)
    {
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = reply.buf;
        SshNetwork.msg.len = un;
        SshNetwork.emit(SshNetwork.internal);
    }
    protocore_plaintext_release(mark);
    ctx->ns->i32 = 0;
    return;
}


// ---------------------------------------------------------------------------
// Password change reply (RFC 4252 sec 8)
// ---------------------------------------------------------------------------

// A change the application has finished: send the reply its USERAUTH_REQUEST deferred.
// protocore_ssh_auth_pw_change_take marks the session open on an OK.
void ssh_auth_passwd_change_reply(struct SshAuthInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->slot;
    // sec 5.1: "SSH_MSG_USERAUTH_SUCCESS MUST be sent only once. When SSH_MSG_USERAUTH_SUCCESS has
    // been sent, any further authentication requests received after that SHOULD be silently
    // ignored." Another method may have completed while this change was parked. Checked before the
    // take, which advances the phase itself on an OK.
    if (ssh_phase_auth_complete(i))
    {
        protocore_ssh_auth_pw_change_clear(i);
        return;
    }
    SshPwChange pw = protocore_ssh_auth_pw_change_take(i);
    if (pw == PROTOCORE_SSH_PW_CHANGE_NONE)
    {
        return;
    }
    size_t mark = protocore_plaintext_mark();
    protocore_span reply = protocore_plaintext_span(SSH_PKT_BUF_SIZE, 4);
    size_t n = 0;
    int built = -1;
    ctx->ns->out_args.out = reply.buf;
    ctx->ns->out_args.cap = reply.cap;
    if (span.ok(reply) && pw == PROTOCORE_SSH_PW_CHANGE_OK)
    {
        protocore_ssh_auth_build_success(ctx);
        built = ctx->ns->i32;
        n = ctx->ns->out_args.out_len;
    }
    else if (span.ok(reply))
    {
        ctx->ns->partial = PROTO_FALSE;
        protocore_ssh_auth_build_failure(ctx);
        built = ctx->ns->i32;
        n = ctx->ns->out_args.out_len;
    }
    if (built == 0)
    {
        SshNetwork.ssh_slot = i;
        SshNetwork.msg.payload = reply.buf;
        SshNetwork.msg.len = n;
        SshNetwork.emit(SshNetwork.internal);
    }
    protocore_plaintext_release(mark);
}

// Designated, so a member's position in the struct does not decide what it binds to.
SshAuthNs SshAuth = {.set_password_cb = protocore_ssh_auth_set_password_cb,
                     .set_password_change_cb = protocore_ssh_auth_set_password_change_cb,
                     .set_pubkey_cb = protocore_ssh_auth_set_pubkey_cb,
                     .pw_change_report = protocore_ssh_auth_pw_change_report,
                     .pw_change_clear = protocore_ssh_auth_pw_change_clear,
                     .passwd_change_reply = ssh_auth_passwd_change_reply,
                     .write_publickey_request = protocore_ssh_auth_write_publickey_request,
                     .timed_out = protocore_ssh_auth_timed_out,
                     .reset = protocore_ssh_auth_reset,
                     .parse_request = protocore_ssh_auth_parse_request,
                     .build_failure = protocore_ssh_auth_build_failure,
                     .build_success = protocore_ssh_auth_build_success,
                     .handle_request = protocore_ssh_auth_handle_request,
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
                     .handle_info_response = protocore_ssh_auth_handle_info_response,
#endif
                     .dispatch = ssh_auth_dispatch,
                     .internal = &s_auth};
