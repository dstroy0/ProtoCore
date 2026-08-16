// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// auth/auth.c (RFC 4252, RFC 4256): the authentication protocol - its request and reply shapes,
// the "publickey" method, and the "keyboard-interactive" exchange.

#include "network_drivers/presentation/ssh/auth/auth.h"
#include "network_drivers/presentation/ssh/transport/transport.h"
#include "server/clock/clock.h" // Clock.millis(): refresh the stamp auth.c judges the cooldown against
#include <Arduino.h>            // set_millis(): time travel past the change cooldown
#include <stdint.h>
#include <unity.h>

static int auth_parse_request(const uint8_t *payload, size_t len, SshAuthReq *req)
{
    SshAuth.msg.payload = payload;
    SshAuth.msg.len = len;
    SshAuth.req = req;
    SshAuth.parse_request(SshAuth.internal);
    return SshAuth.i32;
}

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
// RFC 4256 sec 3.4. The member exists only when the method is compiled in, so the helper is gated
// the same way the calls to it are.
static int auth_handle_info_response(uint8_t slot, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                     size_t cap)
{
    SshAuth.slot = slot;
    SshAuth.msg.payload = payload;
    SshAuth.msg.len = len;
    SshAuth.out_args.out = out;
    SshAuth.out_args.cap = cap;
    SshAuth.handle_info_response(SshAuth.internal);
    if (out_len)
    {
        *out_len = SshAuth.out_args.out_len;
    }
    return SshAuth.i32;
}
#endif // PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE

static int auth_build_failure(uint8_t *out, size_t *out_len, size_t cap, proto_bool partial)
{
    SshAuth.out_args.out = out;
    SshAuth.out_args.cap = cap;
    SshAuth.partial = partial;
    SshAuth.build_failure(SshAuth.internal);
    if (out_len)
    {
        *out_len = SshAuth.out_args.out_len;
    }
    return SshAuth.i32;
}

static int auth_build_success(uint8_t *out, size_t *out_len, size_t cap)
{
    SshAuth.out_args.out = out;
    SshAuth.out_args.cap = cap;
    SshAuth.build_success(SshAuth.internal);
    if (out_len)
    {
        *out_len = SshAuth.out_args.out_len;
    }
    return SshAuth.i32;
}

// The publickey USERAUTH_REQUEST body writer, reached through the auth namespace.
static void auth_write_publickey_request(protocore_span *w, const uint8_t *sid, size_t sid_len, const char *user,
                                         const char *service, const char *pk_algo, const uint8_t *pk_blob,
                                         size_t pk_len)
{
    SshAuth.out_args.w = w;
    SshAuth.userauth.sid = sid;
    SshAuth.userauth.sid_len = sid_len;
    SshAuth.userauth.user = user;
    SshAuth.userauth.service = service;
    SshAuth.userauth.pk_algo = pk_algo;
    SshAuth.userauth.pk_blob = pk_blob;
    SshAuth.userauth.pk_len = pk_len;
    SshAuth.write_publickey_request(SshAuth.internal);
}

// The userauth layer, reached through its namespace: set the members a call reads, invoke it, take
// the outcome off the same handle.
static void auth_reset(uint8_t slot)
{
    SshAuth.slot = slot;
    SshAuth.reset(SshAuth.internal);
}

static void auth_set_password_cb(SshPasswordCb cb)
{
    SshAuth.cbs.password_cb = cb;
    SshAuth.set_password_cb(SshAuth.internal);
}

static void auth_set_password_change_cb(SshPasswordChangeCb cb)
{
    SshAuth.cbs.password_change_cb = cb;
    SshAuth.set_password_change_cb(SshAuth.internal);
}

static void auth_pw_change_report(uint8_t slot, proto_bool ok)
{
    SshAuth.slot = slot;
    SshAuth.ok = ok;
    SshAuth.pw_change_report(SshAuth.internal);
}

static int auth_handle_request(uint8_t slot, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                               size_t cap)
{
    SshAuth.slot = slot;
    SshAuth.msg.payload = payload;
    SshAuth.msg.len = len;
    SshAuth.out_args.out = out;
    SshAuth.out_args.cap = cap;
    SshAuth.handle_request(SshAuth.internal);
    if (out_len)
    {
        *out_len = SshAuth.out_args.out_len;
    }
    return SshAuth.i32;
}

static proto_bool s_change_seen;
static uint32_t s_clock;

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
static char s_seen_user[SSH_AUTH_USER_MAX];
static char s_seen_pass[SSH_AUTH_PASS_MAX];
static proto_bool s_answer;
static int s_calls;
#endif

static size_t put_str(uint8_t *p, size_t off, const char *s, uint32_t n)
{
    p[off] = (uint8_t)(n >> 24);
    p[off + 1] = (uint8_t)(n >> 16);
    p[off + 2] = (uint8_t)(n >> 8);
    p[off + 3] = (uint8_t)n;
    for (uint32_t k = 0; k < n; k++)
    {
        p[off + 4 + k] = (uint8_t)s[k];
    }
    return off + 4 + n;
}

static size_t put_cstr(uint8_t *p, size_t off, const char *s)
{
    uint32_t n = 0;
    while (s[n] != 0)
    {
        n++;
    }
    return put_str(p, off, s, n);
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t build_req_head(uint8_t *p, const char *user, const char *service, const char *method)
{
    p[0] = SSH_MSG_USERAUTH_REQUEST;
    size_t n = put_cstr(p, 1, user);
    n = put_cstr(p, n, service);
    return put_cstr(p, n, method);
}

static size_t put_sig_blob(uint8_t *p, size_t off, const char *fmt, const char *raw, uint32_t raw_len)
{
    uint8_t inner[128];
    size_t io = put_cstr(inner, 0, fmt);
    io = put_str(inner, io, raw, raw_len);
    return put_str(p, off, (const char *)inner, (uint32_t)io);
}

static void change_cb(uint8_t slot, const char *user, const char *oldpw, const char *newpw)
{
    (void)slot;
    (void)user;
    (void)oldpw;
    (void)newpw;
    s_change_seen = PROTO_TRUE; // returns at once; the outcome arrives via pw_change_report()
}

static size_t build_pw_change(uint8_t *p, const char *oldpw, const char *newpw)
{
    size_t n = build_req_head(p, "root", "ssh-connection", "password");
    p[n++] = 1; // boolean TRUE: change request
    n = put_cstr(p, n, oldpw);
    return put_cstr(p, n, newpw);
}

static void clock_past_cooldown(void)
{
    s_clock += PROTOCORE_SSH_PW_CHANGE_COOLDOWN_MS + 1u;
    set_millis(s_clock);
    // auth.c judges the cooldown against Clock.ms, the stamp one read of the source leaves behind.
    // The dispatch loop refreshes it once a pass; there is no loop here, so moving the mock counter
    // alone leaves the library reading the previous instant.
    Clock.millis(Clock.internal);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t blob_of(uint8_t *p, const char *type, uint32_t type_len)
{
    size_t n = put_str(p, 0, type, type_len);
    return put_str(p, n, "\x01\x02\x03\x04", 4); // stand-in for the type's remaining fields
}

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE

static proto_bool pw_cb(const char *user, const char *pass)
{
    s_calls++;
    size_t k = 0;
    for (; user != NULL && user[k] != '\0' && k + 1 < sizeof(s_seen_user); k++)
    {
        s_seen_user[k] = user[k];
    }
    s_seen_user[k] = '\0';
    for (k = 0; pass != NULL && pass[k] != '\0' && k + 1 < sizeof(s_seen_pass); k++)
    {
        s_seen_pass[k] = pass[k];
    }
    s_seen_pass[k] = '\0';
    return s_answer;
}

static size_t put_u32(uint8_t *p, size_t off, uint32_t v)
{
    p[off] = (uint8_t)(v >> 24);
    p[off + 1] = (uint8_t)(v >> 16);
    p[off + 2] = (uint8_t)(v >> 8);
    p[off + 3] = (uint8_t)v;
    return off + 4;
}

static size_t strlen_of(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0')
    {
        n++;
    }
    return n;
}

static size_t build_kbdint_request(uint8_t *p, const char *user, const char *submethods)
{
    size_t n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    n = put_str(p, n, user, (uint32_t)strlen_of(user));
    n = put_str(p, n, "ssh-connection", 14);
    n = put_str(p, n, "keyboard-interactive", 20);
    n = put_str(p, n, "", 0); // language tag
    return put_str(p, n, submethods, (uint32_t)strlen_of(submethods));
}

static size_t build_info_response(uint8_t *p, uint32_t num, const char *const *responses)
{
    size_t n = 0;
    p[n++] = SSH_MSG_USERAUTH_INFO_RESPONSE;
    n = put_u32(p, n, num);
    for (uint32_t k = 0; k < num; k++)
    {
        n = put_str(p, n, responses[k], (uint32_t)strlen_of(responses[k]));
    }
    return n;
}

static size_t arm(const char *user, uint8_t *out, size_t cap)
{
    uint8_t req[128];
    const size_t rn = build_kbdint_request(req, user, "");
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_handle_request(0, req, rn, out, &olen, cap));
    return olen;
}

#endif

void setUp(void)
{
    ssh_transport_init(0);
    ssh_pkt_init(0);
    auth_reset(0);
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    auth_set_password_cb(pw_cb);
    s_seen_user[0] = '\0';
    s_seen_pass[0] = '\0';
    s_answer = PROTO_TRUE;
    s_calls = 0;
#endif
}
void tearDown(void)
{
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    auth_set_password_cb(NULL);
#endif
}

static void test_sec5_common_fields_are_parsed(void)
{
    uint8_t p[128];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "none");

    TEST_ASSERT_EQUAL_INT(0, auth_parse_request(p, n, &req));
    TEST_ASSERT_EQUAL_STRING("root", req.user);
    TEST_ASSERT_EQUAL_STRING("ssh-connection", req.service);
    TEST_ASSERT_EQUAL_STRING("none", req.method);
}

static void test_sec5_wrong_message_number_is_refused(void)
{
    uint8_t p[128];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "none");
    p[0] = SSH_MSG_USERAUTH_SUCCESS;

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, n, &req));
}

static void test_sec5_truncation_at_each_field_is_refused(void)
{
    uint8_t p[128];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "none");

    for (size_t cut = 1; cut < n; cut++)
    {
        TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, cut, &req));
    }
}

static void test_sec5_empty_payload_is_refused(void)
{
    SshAuthReq req;
    uint8_t p[1] = {0};

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, 0, &req));
}

static void test_sec5_overlong_user_name_is_refused(void)
{
    uint8_t p[1024];
    SshAuthReq req;
    char big[SSH_AUTH_USER_MAX + 64];
    for (size_t k = 0; k < sizeof(big) - 1u; k++)
    {
        big[k] = 'u';
    }
    big[sizeof(big) - 1u] = 0;

    size_t n = build_req_head(p, big, "ssh-connection", "none");
    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, n, &req));
}

static void test_sec5_length_header_beyond_payload_is_refused(void)
{
    uint8_t p[64];
    SshAuthReq req;
    p[0] = SSH_MSG_USERAUTH_REQUEST;
    p[1] = 0x7Fu; // a user-name length near 2 GB
    p[2] = 0xFFu;
    p[3] = 0xFFu;
    p[4] = 0xFFu;

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, sizeof(p), &req));
}

static void test_sec5_2_none_carries_no_method_fields(void)
{
    uint8_t p[128];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "none");

    TEST_ASSERT_EQUAL_INT(0, auth_parse_request(p, n, &req));
    TEST_ASSERT_EQUAL_STRING("none", req.method);
    TEST_ASSERT_FALSE(req.is_password);
    TEST_ASSERT_FALSE(req.is_pubkey);
    TEST_ASSERT_FALSE(req.is_kbdint);
}

static void test_sec8_password_request_is_parsed(void)
{
    uint8_t p[192];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "password");
    p[n++] = 0; // boolean FALSE: not a change request
    n = put_cstr(p, n, "hunter2");

    TEST_ASSERT_EQUAL_INT(0, auth_parse_request(p, n, &req));
    TEST_ASSERT_TRUE(req.is_password);
    TEST_ASSERT_FALSE(req.is_pw_change);
    TEST_ASSERT_EQUAL_STRING("hunter2", req.password);
}

static void test_sec8_password_change_carries_both_passwords(void)
{
    uint8_t p[256];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "password");
    p[n++] = 1; // boolean TRUE: change request
    n = put_cstr(p, n, "oldpass");
    n = put_cstr(p, n, "newpass");

    TEST_ASSERT_EQUAL_INT(0, auth_parse_request(p, n, &req));
    TEST_ASSERT_TRUE(req.is_password);
    TEST_ASSERT_TRUE(req.is_pw_change);
    TEST_ASSERT_EQUAL_STRING("oldpass", req.password);
    TEST_ASSERT_EQUAL_STRING("newpass", req.new_password);
}

static void test_sec8_change_without_new_password_is_refused(void)
{
    uint8_t p[192];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "password");
    p[n++] = 1;
    n = put_cstr(p, n, "oldpass");

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, n, &req));
}

static void test_sec8_password_method_without_fields_is_refused(void)
{
    uint8_t p[128];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "password");

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, n, &req));
}

static void test_sec8_empty_password_parses(void)
{
    uint8_t p[128];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "password");
    p[n++] = 0;
    n = put_cstr(p, n, "");

    TEST_ASSERT_EQUAL_INT(0, auth_parse_request(p, n, &req));
    TEST_ASSERT_TRUE(req.is_password);
    TEST_ASSERT_EQUAL_STRING("", req.password);
}

static void test_sec7_query_form_has_no_signature(void)
{
    uint8_t p[256];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "publickey");
    p[n++] = 0; // boolean FALSE: the "is this key acceptable" query
    n = put_cstr(p, n, "ssh-ed25519");
    n = put_str(p, n, "\x01\x02\x03\x04", 4);

    TEST_ASSERT_EQUAL_INT(0, auth_parse_request(p, n, &req));
    TEST_ASSERT_TRUE(req.is_pubkey);
    TEST_ASSERT_FALSE(req.has_signature);
    TEST_ASSERT_EQUAL_STRING("ssh-ed25519", req.pk_algo);
    TEST_ASSERT_EQUAL_UINT32(4u, req.pk_blob_len);
}

static void test_sec7_signed_form_reports_signature_and_covered_prefix(void)
{
    uint8_t p[512];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "publickey");
    p[n++] = 1; // boolean TRUE: a real attempt
    n = put_cstr(p, n, "ssh-ed25519");
    n = put_str(p, n, "\x01\x02\x03\x04", 4);
    size_t before_sig = n;
    n = put_sig_blob(p, n, "ssh-ed25519", "\x09\x08\x07\x06\x05", 5);

    TEST_ASSERT_EQUAL_INT(0, auth_parse_request(p, n, &req));
    TEST_ASSERT_TRUE(req.is_pubkey);
    TEST_ASSERT_TRUE(req.has_signature);
    TEST_ASSERT_EQUAL_UINT32(5u, req.signature_len);
    TEST_ASSERT_EQUAL_PTR(p, req.signed_prefix);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)before_sig, (uint32_t)req.signed_prefix_len);
}

static void test_sec6_6_signature_without_a_format_identifier_is_refused(void)
{
    uint8_t p[512];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "publickey");
    p[n++] = 1;
    n = put_cstr(p, n, "ssh-ed25519");
    n = put_str(p, n, "\x01\x02\x03\x04", 4);
    n = put_str(p, n, "\x09\x08\x07", 3); // three raw bytes, not string || string

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, n, &req));
}

static void test_sec7_signature_promised_but_absent_is_refused(void)
{
    uint8_t p[256];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "publickey");
    p[n++] = 1;
    n = put_cstr(p, n, "ssh-ed25519");
    n = put_str(p, n, "\x01\x02\x03\x04", 4);

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, n, &req));
}

static void test_sec7_missing_key_blob_is_refused(void)
{
    uint8_t p[256];
    SshAuthReq req;
    size_t n = build_req_head(p, "root", "ssh-connection", "publickey");
    p[n++] = 0;
    n = put_cstr(p, n, "ssh-ed25519");

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, n, &req));
}

static void test_sec7_overlong_algorithm_name_is_refused(void)
{
    uint8_t p[512];
    SshAuthReq req;
    char big[SSH_AUTH_ALGO_MAX + 32];
    for (size_t k = 0; k < sizeof(big) - 1u; k++)
    {
        big[k] = 'a';
    }
    big[sizeof(big) - 1u] = 0;

    size_t n = build_req_head(p, "root", "ssh-connection", "publickey");
    p[n++] = 0;
    n = put_cstr(p, n, big);
    n = put_str(p, n, "\x01", 1);

    TEST_ASSERT_EQUAL_INT(-1, auth_parse_request(p, n, &req));
}

static void test_sec5_1_failure_has_type_namelist_and_partial_flag(void)
{
    uint8_t out[128];
    size_t len = 0;

    TEST_ASSERT_EQUAL_INT(0, auth_build_failure(out, &len, sizeof(out), PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_FAILURE, out[0]);

    uint32_t nl = rd32(out + 1);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)len, 1u + 4u + nl + 1u); // type, name-list, boolean
    TEST_ASSERT_EQUAL_UINT8(0u, out[len - 1u]);
}

static void test_sec5_1_partial_success_flag_is_carried(void)
{
    uint8_t out[128];
    size_t len = 0;

    TEST_ASSERT_EQUAL_INT(0, auth_build_failure(out, &len, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT8(1u, out[len - 1u]);

    TEST_ASSERT_EQUAL_INT(0, auth_build_failure(out, &len, sizeof(out), PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT8(0u, out[len - 1u]);
}

static void test_sec5_2_none_is_not_offered_in_the_continue_list(void)
{
    uint8_t out[128];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_build_failure(out, &len, sizeof(out), PROTO_FALSE));

    uint32_t nl = rd32(out + 1);
    char buf[96];
    TEST_ASSERT_TRUE(nl < sizeof(buf));
    for (uint32_t k = 0; k < nl; k++)
    {
        buf[k] = (char)out[5 + k];
    }
    buf[nl] = 0;

    // Walk the comma-separated names; none of them may be "none".
    const char *start = buf;
    for (uint32_t k = 0; k <= nl; k++)
    {
        if (buf[k] == ',' || buf[k] == 0)
        {
            size_t seg = (size_t)(&buf[k] - start);
            proto_bool is_none =
                (seg == 4u) && start[0] == 'n' && start[1] == 'o' && start[2] == 'n' && start[3] == 'e';
            TEST_ASSERT_FALSE(is_none);
            start = &buf[k + 1];
        }
    }
}

static void test_sec5_1_continue_list_is_a_well_formed_name_list(void)
{
    uint8_t out[128];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_build_failure(out, &len, sizeof(out), PROTO_FALSE));

    uint32_t nl = rd32(out + 1);
    const uint8_t *names = out + 5;
    TEST_ASSERT_TRUE(nl > 0u);
    TEST_ASSERT_NOT_EQUAL(',', names[0]);
    TEST_ASSERT_NOT_EQUAL(',', names[nl - 1u]);
    for (uint32_t k = 1; k < nl; k++)
    {
        if (names[k] == ',')
        {
            TEST_ASSERT_NOT_EQUAL(',', names[k - 1u]);
        }
    }
}

static void test_sec5_1_failure_into_an_undersized_buffer_is_refused(void)
{
    uint8_t out[128];
    size_t full = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_build_failure(out, &full, sizeof(out), PROTO_FALSE));

    for (size_t cap = 0; cap < full; cap++)
    {
        size_t len = 0;
        TEST_ASSERT_EQUAL_INT(-1, auth_build_failure(out, &len, cap, PROTO_FALSE));
    }
}

static void test_sec5_1_success_is_a_single_byte(void)
{
    uint8_t out[16];
    size_t len = 0;

    TEST_ASSERT_EQUAL_INT(0, auth_build_success(out, &len, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)len);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_SUCCESS, out[0]);
}

static void test_sec5_1_success_needs_one_byte_of_room(void)
{
    uint8_t out[16];
    size_t len = 0;

    TEST_ASSERT_EQUAL_INT(-1, auth_build_success(out, &len, 0));
    TEST_ASSERT_EQUAL_INT(0, auth_build_success(out, &len, 1));
}

static void test_reset_clears_any_pending_password_change(void)
{
    auth_reset(0);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_NONE, protocore_ssh_auth_pw_change_take(0));
}

static void test_outcome_for_a_slot_with_no_change_in_flight_is_ignored(void)
{
    auth_reset(0);
    auth_pw_change_report(0, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_NONE, protocore_ssh_auth_pw_change_take(0));

    auth_reset(0);
    auth_pw_change_report(0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_NONE, protocore_ssh_auth_pw_change_take(0));
}

static void test_sec8_change_outcome_round_trip(void)
{
    uint8_t p[256];
    uint8_t out[256];
    size_t out_len = 0;

    auth_reset(0);
    auth_set_password_change_cb(change_cb);
    s_change_seen = PROTO_FALSE;
    clock_past_cooldown();

    size_t n = build_pw_change(p, "oldpass", "newpass");
    (void)auth_handle_request(0, p, n, out, &out_len, sizeof(out));
    TEST_ASSERT_TRUE(s_change_seen);

    // Now that a change is in flight the outcome is accepted, and taking it consumes it so one
    // change produces one reply rather than a reply on every poll.
    auth_pw_change_report(0, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_OK, protocore_ssh_auth_pw_change_take(0));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_NONE, protocore_ssh_auth_pw_change_take(0));

    auth_set_password_change_cb(NULL);
    auth_reset(0);
}

static void test_sec8_change_refusal_round_trip(void)
{
    uint8_t p[256];
    uint8_t out[256];
    size_t out_len = 0;

    auth_reset(0);
    auth_set_password_change_cb(change_cb);
    clock_past_cooldown();

    size_t n = build_pw_change(p, "oldpass", "newpass");
    (void)auth_handle_request(0, p, n, out, &out_len, sizeof(out));

    auth_pw_change_report(0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_FAIL, protocore_ssh_auth_pw_change_take(0));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_NONE, protocore_ssh_auth_pw_change_take(0));

    auth_set_password_change_cb(NULL);
    auth_reset(0);
}

static void test_sec8_second_change_inside_the_cooldown_is_refused(void)
{
    uint8_t p[256];
    uint8_t out[256];
    size_t out_len = 0;

    auth_reset(0);
    auth_set_password_change_cb(change_cb);
    clock_past_cooldown();

    size_t n = build_pw_change(p, "oldpass", "newpass");
    s_change_seen = PROTO_FALSE;
    (void)auth_handle_request(0, p, n, out, &out_len, sizeof(out));
    TEST_ASSERT_TRUE(s_change_seen);

    // Answer the first one so the slot is idle again, then retry without advancing the clock.
    auth_pw_change_report(0, PROTO_TRUE);
    (void)protocore_ssh_auth_pw_change_take(0);

    s_change_seen = PROTO_FALSE;
    (void)auth_handle_request(0, p, n, out, &out_len, sizeof(out));
    TEST_ASSERT_FALSE(s_change_seen);

    auth_set_password_change_cb(NULL);
    auth_reset(0);
}

static void test_sec8_change_while_one_is_in_flight_is_refused(void)
{
    uint8_t p[256];
    uint8_t out[256];
    size_t out_len = 0;

    auth_reset(0);
    auth_set_password_change_cb(change_cb);
    clock_past_cooldown();

    size_t n = build_pw_change(p, "oldpass", "newpass");
    (void)auth_handle_request(0, p, n, out, &out_len, sizeof(out));

    clock_past_cooldown(); // the cooldown is not what refuses this one
    s_change_seen = PROTO_FALSE;
    (void)auth_handle_request(0, p, n, out, &out_len, sizeof(out));
    TEST_ASSERT_FALSE(s_change_seen);

    auth_set_password_change_cb(NULL);
    auth_reset(0);
}

static void test_sec8_change_without_a_callback_is_refused(void)
{
    uint8_t p[256];
    uint8_t out[256];
    size_t out_len = 0;

    auth_reset(0);
    auth_set_password_change_cb(NULL);
    clock_past_cooldown();

    size_t n = build_pw_change(p, "oldpass", "newpass");
    (void)auth_handle_request(0, p, n, out, &out_len, sizeof(out));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_NONE, protocore_ssh_auth_pw_change_take(0));
    TEST_ASSERT_TRUE(out_len > 0u); // an answer went out
    auth_reset(0);
}

static void test_out_of_range_slot_is_ignored(void)
{
    auth_reset((uint8_t)MAX_SSH_CONNS);
    auth_pw_change_report((uint8_t)MAX_SSH_CONNS, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SSH_PW_CHANGE_NONE, protocore_ssh_auth_pw_change_take((uint8_t)MAX_SSH_CONNS));
}

static void test_sec7_ed25519_name_with_its_own_blob(void)
{
    uint8_t b[64];
    const size_t n = blob_of(b, "ssh-ed25519", 11);
    TEST_ASSERT_TRUE(ssh_pubkey_algo_supported("ssh-ed25519", b, (uint32_t)n));
}

static void test_sec7_ecdsa_name_with_its_own_blob(void)
{
    uint8_t b[64];
    const size_t n = blob_of(b, "ecdsa-sha2-nistp256", 19);
    TEST_ASSERT_TRUE(ssh_pubkey_algo_supported("ecdsa-sha2-nistp256", b, (uint32_t)n));
}

static void test_sec7_both_rsa_names_take_an_ssh_rsa_blob(void)
{
    uint8_t b[64];
    const size_t n = blob_of(b, "ssh-rsa", 7);
    TEST_ASSERT_TRUE(ssh_pubkey_algo_supported("rsa-sha2-256", b, (uint32_t)n));
    TEST_ASSERT_TRUE(ssh_pubkey_algo_supported("rsa-sha2-512", b, (uint32_t)n));
}

static void test_sec7_unsupported_algorithm_is_rejected(void)
{
    uint8_t b[64];
    const size_t n = blob_of(b, "ssh-dss", 7);
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("ssh-dss", b, (uint32_t)n));
}

static void test_sec7_bare_ssh_rsa_signature_name_is_rejected(void)
{
    uint8_t b[64];
    const size_t n = blob_of(b, "ssh-rsa", 7);
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("ssh-rsa", b, (uint32_t)n));
}

static void test_sec7_name_and_blob_must_agree(void)
{
    uint8_t rsa[64], ed[64];
    const size_t rn = blob_of(rsa, "ssh-rsa", 7);
    const size_t en = blob_of(ed, "ssh-ed25519", 11);

    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("ssh-ed25519", rsa, (uint32_t)rn));
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("rsa-sha2-256", ed, (uint32_t)en));
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("ecdsa-sha2-nistp256", ed, (uint32_t)en));
}

static void test_sec7_truncated_blob_is_rejected(void)
{
    uint8_t b[64];
    const size_t n = blob_of(b, "ssh-ed25519", 11);
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("ssh-ed25519", b, 8u)); // inside the type string
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("ssh-ed25519", b, 3u)); // inside the length prefix
    (void)n;
}

static void test_sec7_wrong_type_length_is_rejected(void)
{
    uint8_t b[64];
    (void)blob_of(b, "ssh-ed25519", 11);
    b[3] = 10; // claim a ten-byte type where an eleven-byte name was offered
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("ssh-ed25519", b, 32u));
}

static void test_sec7_null_arguments_are_rejected(void)
{
    uint8_t b[64];
    const size_t n = blob_of(b, "ssh-ed25519", 11);
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported(NULL, b, (uint32_t)n));
    TEST_ASSERT_FALSE(ssh_pubkey_algo_supported("ssh-ed25519", NULL, (uint32_t)n));
}

static void test_sec7_signed_data_field_order(void)
{
    uint8_t pk[16];
    const size_t pkn = blob_of(pk, "ssh-ed25519", 11);
    const uint8_t sid[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    uint8_t buf[256];
    protocore_span w = protocore_span_from(buf, sizeof(buf));
    auth_write_publickey_request(&w, sid, sizeof(sid), "root", "ssh-connection", "ssh-ed25519", pk, pkn);
    TEST_ASSERT_TRUE(protocore_span_ok(w));

    size_t o = 0;
    TEST_ASSERT_EQUAL_UINT32(4u, rd_u32(buf + o)); // string session identifier
    o += 4;
    TEST_ASSERT_EQUAL_HEX8(0xDE, buf[o]);
    o += 4;
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_REQUEST, buf[o]); // byte
    o += 1;
    TEST_ASSERT_EQUAL_UINT32(4u, rd_u32(buf + o)); // string user name
    o += 4 + 4;
    TEST_ASSERT_EQUAL_UINT32(14u, rd_u32(buf + o)); // string service name
    o += 4 + 14;
    TEST_ASSERT_EQUAL_UINT32(9u, rd_u32(buf + o)); // string "publickey"
    o += 4 + 9;
    TEST_ASSERT_EQUAL_UINT8(1u, buf[o]); // boolean TRUE
    o += 1;
    TEST_ASSERT_EQUAL_UINT32(11u, rd_u32(buf + o)); // string algorithm name
    o += 4 + 11;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)pkn, rd_u32(buf + o)); // string public key
    o += 4 + pkn;
    TEST_ASSERT_EQUAL_size_t(o, w.pos);
}

static void test_sec7_request_is_the_signed_data_without_the_session_id(void)
{
    uint8_t pk[16];
    const size_t pkn = blob_of(pk, "ssh-ed25519", 11);
    const uint8_t sid[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    uint8_t signed_data[256];
    protocore_span sd = protocore_span_from(signed_data, sizeof(signed_data));
    auth_write_publickey_request(&sd, sid, sizeof(sid), "user", "ssh-connection", "ssh-ed25519", pk, pkn);

    uint8_t request[256];
    protocore_span rq = protocore_span_from(request, sizeof(request));
    auth_write_publickey_request(&rq, NULL, 0, "user", "ssh-connection", "ssh-ed25519", pk, pkn);

    TEST_ASSERT_TRUE(protocore_span_ok(sd));
    TEST_ASSERT_TRUE(protocore_span_ok(rq));

    const size_t sid_field = 4u + sizeof(sid);
    TEST_ASSERT_EQUAL_size_t(rq.pos + sid_field, sd.pos);
    for (size_t k = 0; k < rq.pos; k++)
    {
        TEST_ASSERT_EQUAL_HEX8(signed_data[sid_field + k], request[k]);
    }
}

static void test_sec7_long_session_identifier_is_carried_whole(void)
{
    uint8_t pk[16];
    const size_t pkn = blob_of(pk, "ssh-ed25519", 11);
    uint8_t sid[64];
    for (size_t k = 0; k < sizeof(sid); k++)
    {
        sid[k] = (uint8_t)k;
    }

    uint8_t buf[256];
    protocore_span w = protocore_span_from(buf, sizeof(buf));
    auth_write_publickey_request(&w, sid, sizeof(sid), "u", "ssh-connection", "ssh-ed25519", pk, pkn);
    TEST_ASSERT_TRUE(protocore_span_ok(w));
    TEST_ASSERT_EQUAL_UINT32(64u, rd_u32(buf));
    TEST_ASSERT_EQUAL_HEX8(63u, buf[4 + 63]);
}

static void test_sec7_undersized_span_is_reported(void)
{
    uint8_t pk[16];
    const size_t pkn = blob_of(pk, "ssh-ed25519", 11);
    uint8_t buf[16];
    protocore_span w = protocore_span_from(buf, sizeof(buf));
    auth_write_publickey_request(&w, NULL, 0, "user", "ssh-connection", "ssh-ed25519", pk, pkn);
    TEST_ASSERT_FALSE(protocore_span_ok(w));
}

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE

static void test_sec5_message_numbers(void)
{
    TEST_ASSERT_EQUAL_UINT32(60u, (uint32_t)SSH_MSG_USERAUTH_INFO_REQUEST);
    TEST_ASSERT_EQUAL_UINT32(61u, (uint32_t)SSH_MSG_USERAUTH_INFO_RESPONSE);
}

static void test_sec3_2_request_field_order(void)
{
    uint8_t out[128];
    const size_t n = arm("root", out, sizeof(out));

    size_t o = 0;
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_INFO_REQUEST, out[o]);
    o += 1;
    TEST_ASSERT_EQUAL_UINT32(0u, rd_u32(out + o)); // name
    o += 4;
    TEST_ASSERT_EQUAL_UINT32(0u, rd_u32(out + o)); // instruction
    o += 4;
    TEST_ASSERT_EQUAL_UINT32(0u, rd_u32(out + o)); // language tag
    o += 4;
    TEST_ASSERT_EQUAL_UINT32(1u, rd_u32(out + o)); // num-prompts: this end asks one
    o += 4;
    const uint32_t prompt_len = rd_u32(out + o);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, prompt_len);
    o += 4 + prompt_len;
    // "boolean echo[n]" - a password prompt is not echoed.
    TEST_ASSERT_EQUAL_UINT8(0u, out[o]);
    o += 1;
    TEST_ASSERT_EQUAL_size_t(o, n);
}

static void test_sec3_4_the_one_response_reaches_the_verifier(void)
{
    uint8_t out[128];
    (void)arm("alice", out, sizeof(out));

    const char *r[] = {"hunter2"};
    uint8_t pkt[64];
    const size_t pn = build_info_response(pkt, 1, r);

    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_handle_info_response(0, pkt, pn, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(1, s_calls);
    TEST_ASSERT_EQUAL_STRING("alice", s_seen_user); // the user is remembered across the round trip
    TEST_ASSERT_EQUAL_STRING("hunter2", s_seen_pass);
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, out[0]);
}

static void test_sec3_4_rejected_response_fails(void)
{
    uint8_t out[128];
    (void)arm("alice", out, sizeof(out));
    s_answer = PROTO_FALSE;

    const char *r[] = {"wrong"};
    uint8_t pkt[64];
    const size_t pn = build_info_response(pkt, 1, r);

    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_handle_info_response(0, pkt, pn, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
}

static void test_sec3_4_zero_responses_against_one_prompt_fails(void)
{
    uint8_t out[128];
    (void)arm("alice", out, sizeof(out));

    uint8_t pkt[64];
    const size_t pn = build_info_response(pkt, 0, NULL);

    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_handle_info_response(0, pkt, pn, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_EQUAL_INT(0, s_calls); // nothing was verified
}

static void test_sec3_4_two_responses_against_one_prompt_fails(void)
{
    uint8_t out[128];
    (void)arm("alice", out, sizeof(out));

    const char *r[] = {"a", "b"};
    uint8_t pkt[64];
    const size_t pn = build_info_response(pkt, 2, r);

    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_handle_info_response(0, pkt, pn, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_EQUAL_INT(0, s_calls);
}

static void test_sec3_4_count_past_the_packet_fails(void)
{
    uint8_t out[128];
    (void)arm("alice", out, sizeof(out));

    uint8_t pkt[16];
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_INFO_RESPONSE;
    n = put_u32(pkt, n, 1);
    n = put_u32(pkt, n, 0xFFFFu); // a string longer than anything that follows

    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_EQUAL_INT(0, s_calls);
}

static void test_sec3_4_response_without_a_prompt_is_refused(void)
{
    const char *r[] = {"hunter2"};
    uint8_t pkt[64];
    const size_t pn = build_info_response(pkt, 1, r);

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(-1, auth_handle_info_response(0, pkt, pn, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, s_calls);
}

static void test_sec3_4_the_exchange_is_single_use(void)
{
    uint8_t out[128];
    (void)arm("alice", out, sizeof(out));

    const char *r[] = {"hunter2"};
    uint8_t pkt[64];
    const size_t pn = build_info_response(pkt, 1, r);

    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, auth_handle_info_response(0, pkt, pn, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, auth_handle_info_response(0, pkt, pn, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(1, s_calls);
}

static void test_sec3_4_wrong_message_number_is_refused(void)
{
    uint8_t out[128];
    (void)arm("alice", out, sizeof(out));

    uint8_t pkt[16];
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST; // 50, not 61
    n = put_u32(pkt, n, 1);

    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(-1, auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));
}

static void test_slot_past_the_pool_is_refused(void)
{
    const char *r[] = {"x"};
    uint8_t pkt[32];
    const size_t pn = build_info_response(pkt, 1, r);
    uint8_t out[32];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(-1, auth_handle_info_response(MAX_SSH_CONNS, pkt, pn, out, &olen, sizeof(out)));
}

#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec5_common_fields_are_parsed);
    RUN_TEST(test_sec5_wrong_message_number_is_refused);
    RUN_TEST(test_sec5_truncation_at_each_field_is_refused);
    RUN_TEST(test_sec5_empty_payload_is_refused);
    RUN_TEST(test_sec5_overlong_user_name_is_refused);
    RUN_TEST(test_sec5_length_header_beyond_payload_is_refused);
    RUN_TEST(test_sec5_2_none_carries_no_method_fields);
    RUN_TEST(test_sec8_password_request_is_parsed);
    RUN_TEST(test_sec8_password_change_carries_both_passwords);
    RUN_TEST(test_sec8_change_without_new_password_is_refused);
    RUN_TEST(test_sec8_password_method_without_fields_is_refused);
    RUN_TEST(test_sec8_empty_password_parses);
    RUN_TEST(test_sec7_query_form_has_no_signature);
    RUN_TEST(test_sec7_signed_form_reports_signature_and_covered_prefix);
    RUN_TEST(test_sec6_6_signature_without_a_format_identifier_is_refused);
    RUN_TEST(test_sec7_signature_promised_but_absent_is_refused);
    RUN_TEST(test_sec7_missing_key_blob_is_refused);
    RUN_TEST(test_sec7_overlong_algorithm_name_is_refused);
    RUN_TEST(test_sec5_1_failure_has_type_namelist_and_partial_flag);
    RUN_TEST(test_sec5_1_partial_success_flag_is_carried);
    RUN_TEST(test_sec5_2_none_is_not_offered_in_the_continue_list);
    RUN_TEST(test_sec5_1_continue_list_is_a_well_formed_name_list);
    RUN_TEST(test_sec5_1_failure_into_an_undersized_buffer_is_refused);
    RUN_TEST(test_sec5_1_success_is_a_single_byte);
    RUN_TEST(test_sec5_1_success_needs_one_byte_of_room);
    RUN_TEST(test_reset_clears_any_pending_password_change);
    RUN_TEST(test_outcome_for_a_slot_with_no_change_in_flight_is_ignored);
    RUN_TEST(test_sec8_change_outcome_round_trip);
    RUN_TEST(test_sec8_change_refusal_round_trip);
    RUN_TEST(test_sec8_second_change_inside_the_cooldown_is_refused);
    RUN_TEST(test_sec8_change_while_one_is_in_flight_is_refused);
    RUN_TEST(test_sec8_change_without_a_callback_is_refused);
    RUN_TEST(test_out_of_range_slot_is_ignored);
    RUN_TEST(test_sec7_ed25519_name_with_its_own_blob);
    RUN_TEST(test_sec7_ecdsa_name_with_its_own_blob);
    RUN_TEST(test_sec7_both_rsa_names_take_an_ssh_rsa_blob);
    RUN_TEST(test_sec7_unsupported_algorithm_is_rejected);
    RUN_TEST(test_sec7_bare_ssh_rsa_signature_name_is_rejected);
    RUN_TEST(test_sec7_name_and_blob_must_agree);
    RUN_TEST(test_sec7_truncated_blob_is_rejected);
    RUN_TEST(test_sec7_wrong_type_length_is_rejected);
    RUN_TEST(test_sec7_null_arguments_are_rejected);
    RUN_TEST(test_sec7_signed_data_field_order);
    RUN_TEST(test_sec7_request_is_the_signed_data_without_the_session_id);
    RUN_TEST(test_sec7_long_session_identifier_is_carried_whole);
    RUN_TEST(test_sec7_undersized_span_is_reported);
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    RUN_TEST(test_sec5_message_numbers);
    RUN_TEST(test_sec3_2_request_field_order);
    RUN_TEST(test_sec3_4_the_one_response_reaches_the_verifier);
    RUN_TEST(test_sec3_4_rejected_response_fails);
    RUN_TEST(test_sec3_4_zero_responses_against_one_prompt_fails);
    RUN_TEST(test_sec3_4_two_responses_against_one_prompt_fails);
    RUN_TEST(test_sec3_4_count_past_the_packet_fails);
    RUN_TEST(test_sec3_4_response_without_a_prompt_is_refused);
    RUN_TEST(test_sec3_4_the_exchange_is_single_use);
    RUN_TEST(test_sec3_4_wrong_message_number_is_refused);
    RUN_TEST(test_slot_past_the_pool_is_refused);
#endif
    return UNITY_END();
}
