// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for HKDF-SHA384 (crypto/kdf/hkdf_sha384.h), the KDF the TLS 1.3 SHA-384 cipher suites
// run their whole key schedule through.
//
// RFC 5869 tabulates HKDF only for SHA-256 and SHA-1, and RFC 8448's worked TLS 1.3 trace is
// SHA-256 throughout, so there is no published SHA-384 answer to read. The vectors below are the
// RFC's own Appendix A input triples re-run at SHA-384, answered by openssl - an implementation
// outside this tree - and the generator refuses to emit a row unless it first reproduces RFC 5869
// A.1 and the RFC 8448 sec 3 "derived" secret at SHA-256 (tools/harness.py crypto hkdf384 --check).
//
// A.2's 82-octet request is the row that matters for Expand: at a 48-octet block that is two T(i)
// blocks and a partial third, so it runs the chain rather than one HMAC.
//
// The Expand-Label rows are the TLS 1.3 labels the schedule actually derives under, at the widths
// the SHA-384 suites use, so the HkdfLabel encoding (RFC 8446 sec 7.1) is checked against openssl's
// TLS13-KDF rather than against itself.

#include "crypto/kdf/hkdf_sha384/hkdf_sha384.h"
#include <string.h>

#include <unity.h>

#define MAXB 512
#define ROWS(a) (sizeof(a) / sizeof((a)[0]))

typedef struct
{
    int tc;
    const char *salt;
    const char *ikm;
    const char *prk;
} KatHkdf384Extract;

typedef struct
{
    int tc;
    const char *prk;
    const char *info;
    uint32_t l;
    const char *okm;
} KatHkdf384Expand;

typedef struct
{
    int tc;
    const char *secret;
    const char *label;
    const char *context;
    uint32_t l;
    const char *out;
} KatHkdf384Label;

#include "hkdf_sha384_kat_data.inc"

static uint8_t g_work[PROTOCORE_HKDF_SHA384_BORROW] __attribute__((aligned(8)));

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t nib(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : ((c | 0x20) - 'a' + 10));
}

static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2)
    {
        out[n++] = (uint8_t)((nib(h[0]) << 4) | nib(h[1]));
    }
    return n;
}

// Extract: PRK = HMAC-SHA384(salt, IKM). Row 3's salt is empty, which HMAC keys with a zero block.
void test_extract(void)
{
    for (size_t i = 0; i < ROWS(KAT_HKDF384_EXTRACT); i++)
    {
        const KatHkdf384Extract *v = &KAT_HKDF384_EXTRACT[i];
        uint8_t salt[MAXB], ikm[MAXB], want[PROTOCORE_HKDF_SHA384_HASH_LEN], got[PROTOCORE_HKDF_SHA384_HASH_LEN];
        size_t slen = unhex(v->salt, salt);
        size_t ilen = unhex(v->ikm, ikm);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(PROTOCORE_HKDF_SHA384_HASH_LEN, (unsigned)unhex(v->prk, want), v->prk);
        HkdfSha384.extract_args.salt = slen ? salt : NULL;
        HkdfSha384.extract_args.salt_len = slen;
        HkdfSha384.extract_args.ikm = ikm;
        HkdfSha384.extract_args.ikm_len = ilen;
        HkdfSha384.extract_args.prk = got;
        HkdfSha384.extract(g_work);
        TEST_ASSERT_TRUE(HkdfSha384.ok);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, PROTOCORE_HKDF_SHA384_HASH_LEN, v->prk);
    }
}

// Expand: OKM = T(1) | T(2) | ..., T(i) = HMAC(PRK, T(i-1) | info | i).
void test_expand(void)
{
    for (size_t i = 0; i < ROWS(KAT_HKDF384_EXPAND); i++)
    {
        const KatHkdf384Expand *v = &KAT_HKDF384_EXPAND[i];
        uint8_t prk[PROTOCORE_HKDF_SHA384_HASH_LEN], info[MAXB], want[MAXB], got[MAXB];
        unhex(v->prk, prk);
        size_t ilen = unhex(v->info, info);
        size_t wlen = unhex(v->okm, want);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->l, (uint32_t)wlen, v->okm);
        memset(got, 0, sizeof(got));
        HkdfSha384.expand_args.prk = prk;
        HkdfSha384.expand_args.info = ilen ? info : NULL;
        HkdfSha384.expand_args.info_len = ilen;
        HkdfSha384.expand_args.out = got;
        HkdfSha384.expand_args.out_len = wlen;
        HkdfSha384.expand(g_work);
        TEST_ASSERT_TRUE(HkdfSha384.ok);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, wlen, v->okm);
    }
}

// HKDF-Expand-Label (RFC 8446 sec 7.1) under the "tls13 " prefix, in both forms: rows with a
// 48-octet context go through expand_label_ctx, rows with none through expand_label.
void test_expand_label(void)
{
    for (size_t i = 0; i < ROWS(KAT_HKDF384_LABEL); i++)
    {
        const KatHkdf384Label *v = &KAT_HKDF384_LABEL[i];
        uint8_t secret[PROTOCORE_HKDF_SHA384_HASH_LEN], ctx[MAXB], want[MAXB], got[MAXB];
        unhex(v->secret, secret);
        size_t clen = unhex(v->context, ctx);
        size_t wlen = unhex(v->out, want);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->l, (uint32_t)wlen, v->out);
        memset(got, 0, sizeof(got));
        if (clen)
        {
            HkdfSha384.expand_label_ctx_args.secret = secret;
            HkdfSha384.expand_label_ctx_args.label = v->label;
            HkdfSha384.expand_label_ctx_args.context = ctx;
            HkdfSha384.expand_label_ctx_args.context_len = clen;
            HkdfSha384.expand_label_ctx_args.out = got;
            HkdfSha384.expand_label_ctx_args.out_len = wlen;
            HkdfSha384.expand_label_ctx_args.label_prefix = PROTOCORE_HKDF_SHA384_LABEL_PREFIX;
            HkdfSha384.expand_label_ctx(g_work);
        }
        else
        {
            HkdfSha384.expand_label_args.secret = secret;
            HkdfSha384.expand_label_args.label = v->label;
            HkdfSha384.expand_label_args.out = got;
            HkdfSha384.expand_label_args.out_len = wlen;
            HkdfSha384.expand_label_args.label_prefix = PROTOCORE_HKDF_SHA384_LABEL_PREFIX;
            HkdfSha384.expand_label(g_work);
        }
        TEST_ASSERT_TRUE(HkdfSha384.ok);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, wlen, v->out);
    }
}

// The empty-context form is not the same as a zero-length context passed explicitly through the
// other entry - both encode an HkdfLabel with a zero context length, so they must agree.
void test_the_two_label_forms_agree_on_an_empty_context(void)
{
    uint8_t secret[PROTOCORE_HKDF_SHA384_HASH_LEN];
    uint8_t plain[PROTOCORE_HKDF_SHA384_HASH_LEN], with_ctx[PROTOCORE_HKDF_SHA384_HASH_LEN];
    unhex(KAT_HKDF384_LABEL[0].secret, secret);

    HkdfSha384.expand_label_args.secret = secret;
    HkdfSha384.expand_label_args.label = "finished";
    HkdfSha384.expand_label_args.out = plain;
    HkdfSha384.expand_label_args.out_len = sizeof(plain);
    HkdfSha384.expand_label_args.label_prefix = PROTOCORE_HKDF_SHA384_LABEL_PREFIX;
    HkdfSha384.expand_label(g_work);

    HkdfSha384.expand_label_ctx_args.secret = secret;
    HkdfSha384.expand_label_ctx_args.label = "finished";
    HkdfSha384.expand_label_ctx_args.context = NULL;
    HkdfSha384.expand_label_ctx_args.context_len = 0;
    HkdfSha384.expand_label_ctx_args.out = with_ctx;
    HkdfSha384.expand_label_ctx_args.out_len = sizeof(with_ctx);
    HkdfSha384.expand_label_ctx_args.label_prefix = PROTOCORE_HKDF_SHA384_LABEL_PREFIX;
    HkdfSha384.expand_label_ctx(g_work);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(plain, with_ctx, sizeof(plain));
}

// The label and the prefix are both inside the HkdfLabel, so changing either changes the output.
void test_the_label_and_prefix_are_bound_in(void)
{
    uint8_t secret[PROTOCORE_HKDF_SHA384_HASH_LEN];
    uint8_t a[32], b[32], c[32];
    unhex(KAT_HKDF384_LABEL[0].secret, secret);

    HkdfSha384.expand_label_args.secret = secret;
    HkdfSha384.expand_label_args.label = "key";
    HkdfSha384.expand_label_args.out = a;
    HkdfSha384.expand_label_args.out_len = sizeof(a);
    HkdfSha384.expand_label_args.label_prefix = PROTOCORE_HKDF_SHA384_LABEL_PREFIX;
    HkdfSha384.expand_label(g_work);

    HkdfSha384.expand_label_args.label = "iv";
    HkdfSha384.expand_label_args.out = b;
    HkdfSha384.expand_label(g_work);

    HkdfSha384.expand_label_args.label = "key";
    HkdfSha384.expand_label_args.label_prefix = "dtls13";
    HkdfSha384.expand_label_args.out = c;
    HkdfSha384.expand_label(g_work);

    TEST_ASSERT_TRUE(memcmp(a, b, sizeof(a)) != 0);
    TEST_ASSERT_TRUE(memcmp(a, c, sizeof(a)) != 0);
}

// The block is 48 octets, so the requested length caps at 255*48 and not 255*32. The header states
// that past the cap out is zeroed and ok comes back false; the counter has no encoding there.
void test_the_expand_cap_is_at_the_sha384_block(void)
{
    TEST_ASSERT_EQUAL_UINT(48u, (unsigned)PROTOCORE_HKDF_SHA384_HASH_LEN);

    uint8_t prk[PROTOCORE_HKDF_SHA384_HASH_LEN];
    static uint8_t out[256 * 48];
    unhex(KAT_HKDF384_EXTRACT[0].prk, prk);

    // One octet past the cap: refused, and the buffer is left zeroed rather than half-filled.
    memset(out, 0xA5, sizeof(out));
    HkdfSha384.expand_args.prk = prk;
    HkdfSha384.expand_args.info = NULL;
    HkdfSha384.expand_args.info_len = 0;
    HkdfSha384.expand_args.out = out;
    HkdfSha384.expand_args.out_len = (size_t)255 * 48 + 1;
    HkdfSha384.expand(g_work);
    TEST_ASSERT_FALSE(HkdfSha384.ok);
    for (size_t i = 0; i < (size_t)255 * 48 + 1; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0u, out[i]);
    }

    // A length that would be over a 32-octet block but is under this one still derives.
    HkdfSha384.expand_args.out_len = (size_t)255 * 32 + 16;
    HkdfSha384.expand(g_work);
    TEST_ASSERT_TRUE(HkdfSha384.ok);
}

// The null operands each entry names leave ok false. The BORROW is not among them: it comes from
// the arena and is never null, so the branch that refused one was dead and has been deleted.
void test_null_operands_are_refused(void)
{
    uint8_t prk[PROTOCORE_HKDF_SHA384_HASH_LEN], out[32];
    unhex(KAT_HKDF384_EXTRACT[0].prk, prk);

    HkdfSha384.extract_args.salt = NULL;
    HkdfSha384.extract_args.salt_len = 0;
    HkdfSha384.extract_args.ikm = prk;
    HkdfSha384.extract_args.ikm_len = sizeof(prk);
    HkdfSha384.extract_args.prk = NULL;
    HkdfSha384.extract(g_work);
    TEST_ASSERT_FALSE(HkdfSha384.ok);

    HkdfSha384.expand_args.prk = NULL;
    HkdfSha384.expand_args.out = out;
    HkdfSha384.expand_args.out_len = sizeof(out);
    HkdfSha384.expand(g_work);
    TEST_ASSERT_FALSE(HkdfSha384.ok);

    HkdfSha384.expand_label_args.secret = prk;
    HkdfSha384.expand_label_args.label = NULL;
    HkdfSha384.expand_label_args.out = out;
    HkdfSha384.expand_label_args.out_len = sizeof(out);
    HkdfSha384.expand_label_args.label_prefix = PROTOCORE_HKDF_SHA384_LABEL_PREFIX;
    HkdfSha384.expand_label(g_work);
    TEST_ASSERT_FALSE(HkdfSha384.ok);
}
