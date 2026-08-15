// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/extension.c (RFC 8308): the SSH_MSG_EXT_INFO message and the
// "server-sig-algs" name-list, in both host-key preference orders.

#include "network_drivers/presentation/ssh/transport/extension.h"
#include "network_drivers/presentation/ssh/transport/transport.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// A length-prefixed string at @p off: hands back its body and advances past it (RFC 4251 sec 5).
static const uint8_t *take_str(const uint8_t *p, size_t *off, uint32_t *out_len)
{
    *out_len = rd32(p + *off);
    const uint8_t *body = p + *off + 4u;
    *off += 4u + *out_len;
    return body;
}

static proto_bool body_equals(const uint8_t *body, uint32_t len, const char *s)
{
    uint32_t k = 0;
    for (; k < len; k++)
    {
        if (s[k] == 0 || body[k] != (uint8_t)s[k])
        {
            return PROTO_FALSE;
        }
    }
    return s[k] == 0;
}

// ---------------------------------------------------------------------------
// sec 2.3 SSH_MSG_EXT_INFO message
// ---------------------------------------------------------------------------
// "byte SSH_MSG_EXT_INFO (value 7) / uint32 nr-extensions / repeat ... string extension-name,
// string extension-value (binary)"

static void test_sec2_3_message_starts_with_ext_info_and_a_count(void)
{
    uint8_t out[512];
    size_t len = 0;

    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &len, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_EXT_INFO, out[0]);
    TEST_ASSERT_EQUAL_UINT8(7u, out[0]); // the section fixes the value at 7
    TEST_ASSERT_EQUAL_UINT32(1u, rd32(out + 1));
}

// The name/value pairs sit immediately after the count, and the message ends exactly where the last
// one does: nothing trailing, nothing short.
static void test_sec2_3_one_name_value_pair_spans_the_whole_message(void)
{
    uint8_t out[512];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &len, sizeof(out)));

    size_t off = 5u; // past the type byte and nr-extensions
    uint32_t nlen = 0;
    const uint8_t *name = take_str(out, &off, &nlen);
    uint32_t vlen = 0;
    (void)take_str(out, &off, &vlen);

    TEST_ASSERT_TRUE(body_equals(name, nlen, "server-sig-algs"));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)len, (uint32_t)off);
}

// ---------------------------------------------------------------------------
// sec 3.1 "server-sig-algs"
// ---------------------------------------------------------------------------
// "string 'server-sig-algs' / name-list public-key-algorithms-accepted"

static void test_sec3_1_extension_name_is_server_sig_algs(void)
{
    uint8_t out[512];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &len, sizeof(out)));

    size_t off = 5u;
    uint32_t nlen = 0;
    const uint8_t *name = take_str(out, &off, &nlen);

    TEST_ASSERT_EQUAL_UINT32(15u, nlen);
    TEST_ASSERT_TRUE(body_equals(name, nlen, "server-sig-algs"));
}

// A name-list is comma-separated with no empty entries (RFC 4251 sec 5), so no leading, trailing or
// doubled comma may appear in the value.
static void test_sec3_1_value_is_a_well_formed_name_list(void)
{
    uint8_t out[512];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &len, sizeof(out)));

    size_t off = 5u;
    uint32_t nlen = 0;
    (void)take_str(out, &off, &nlen);
    uint32_t vlen = 0;
    const uint8_t *val = take_str(out, &off, &vlen);

    TEST_ASSERT_TRUE(vlen > 0u);
    TEST_ASSERT_NOT_EQUAL(',', val[0]);
    TEST_ASSERT_NOT_EQUAL(',', val[vlen - 1u]);
    for (uint32_t k = 1; k < vlen; k++)
    {
        if (val[k] == ',')
        {
            TEST_ASSERT_NOT_EQUAL(',', val[k - 1u]);
        }
    }
}

// The list names the algorithms a "publickey" request may be signed with, so every one this end can
// verify is present regardless of which host key it happens to hold.
static void test_sec3_1_value_names_every_verifiable_algorithm(void)
{
    uint8_t out[512];
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &len, sizeof(out)));

    // The value body, NUL-terminated into a scratch buffer so a substring search is well defined.
    size_t off = 5u;
    uint32_t nlen = 0;
    (void)take_str(out, &off, &nlen);
    uint32_t vlen = 0;
    const uint8_t *val = take_str(out, &off, &vlen);
    char buf[256];
    TEST_ASSERT_TRUE(vlen < sizeof(buf));
    for (uint32_t k = 0; k < vlen; k++)
    {
        buf[k] = (char)val[k];
    }
    buf[vlen] = 0;

    TEST_ASSERT_NOT_NULL(strstr(buf, "ssh-ed25519"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "ecdsa-sha2-nistp256"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rsa-sha2-256"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rsa-sha2-512"));
}

// The one branch in the file: preference reorders the list without changing its membership. RFC
// 8332 puts rsa-sha2-512 ahead of rsa-sha2-256 either way.
static void test_sec3_1_preference_reorders_but_keeps_the_same_members(void)
{
    uint8_t a[512];
    uint8_t b[512];
    size_t alen = 0;
    size_t blen = 0;

    ssh_kex_set_prefer_rsa(PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(a, &alen, sizeof(a)));
    ssh_kex_set_prefer_rsa(PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(b, &blen, sizeof(b)));
    ssh_kex_set_prefer_rsa(PROTO_FALSE); // leave the module as it was found

    TEST_ASSERT_EQUAL_UINT32((uint32_t)alen, (uint32_t)blen); // same names, same total length
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, alen));             // different order
}

static void test_sec3_1_rsa_preference_puts_rsa_first(void)
{
    uint8_t out[512];
    size_t len = 0;

    ssh_kex_set_prefer_rsa(PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &len, sizeof(out)));
    ssh_kex_set_prefer_rsa(PROTO_FALSE);

    size_t off = 5u;
    uint32_t nlen = 0;
    (void)take_str(out, &off, &nlen);
    uint32_t vlen = 0;
    const uint8_t *val = take_str(out, &off, &vlen);

    TEST_ASSERT_TRUE(body_equals(val, 12u, "rsa-sha2-512"));
}

static void test_sec3_1_default_preference_puts_ed25519_first(void)
{
    uint8_t out[512];
    size_t len = 0;

    ssh_kex_set_prefer_rsa(PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &len, sizeof(out)));

    size_t off = 5u;
    uint32_t nlen = 0;
    (void)take_str(out, &off, &nlen);
    uint32_t vlen = 0;
    const uint8_t *val = take_str(out, &off, &vlen);

    TEST_ASSERT_TRUE(body_equals(val, 11u, "ssh-ed25519"));
}

// ---------------------------------------------------------------------------
// The failure path
// ---------------------------------------------------------------------------

// A buffer too small for the whole message fails rather than emitting a truncated one, which the
// peer would read as a malformed extension list.
static void test_undersized_buffer_is_refused(void)
{
    uint8_t out[512];
    size_t full = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &full, sizeof(out)));

    for (size_t cap = 0; cap < full; cap += 8u)
    {
        size_t len = 0xFFFFu;
        TEST_ASSERT_EQUAL_INT(-1, ssh_extinfo_build(out, &len, cap));
    }
    // One byte short still fails; exactly enough succeeds.
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_extinfo_build(out, &len, full - 1u));
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &len, full));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)full, (uint32_t)len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec2_3_message_starts_with_ext_info_and_a_count);
    RUN_TEST(test_sec2_3_one_name_value_pair_spans_the_whole_message);
    RUN_TEST(test_sec3_1_extension_name_is_server_sig_algs);
    RUN_TEST(test_sec3_1_value_is_a_well_formed_name_list);
    RUN_TEST(test_sec3_1_value_names_every_verifiable_algorithm);
    RUN_TEST(test_sec3_1_preference_reorders_but_keeps_the_same_members);
    RUN_TEST(test_sec3_1_rsa_preference_puts_rsa_first);
    RUN_TEST(test_sec3_1_default_preference_puts_ed25519_first);
    RUN_TEST(test_undersized_buffer_is_refused);
    return UNITY_END();
}
