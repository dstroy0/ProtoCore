// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the MAC-derived device UUID (server/signaling/device_id.h).
//
// A name-based UUIDv5 is SHA-1(namespace_bytes || name) with the version nibble overwritten to 5
// and the top two bits of octet 8 set to 0b10 (RFC 9562 sec 5.5, formerly RFC 4122 sec 4.3). The
// load-bearing case is test_rfc9562_published_uuidv5_vector: RFC 9562 App. A.4 publishes one worked
// example of that construction - DNS namespace 6ba7b810-9dad-11d1-80b4-00c04fd430c8, name
// "www.example.com", SHA-1 2ed6657de927468b55e12665a8aea6a22dee3e35, final UUID
// 2ed6657d-e927-568b-95e1-2665a8aea6a2 - and reproducing it with the same SHA-1 the module uses is
// what proves the reference construction below is the RFC's and not a second guess at it.
//
// Every other expectation is then the module's own name rule measured against that reference: the
// name is the six MAC octets as twelve lowercase hex characters with no separators.

#include "crypto/hash/sha1.h"
#include "server/signaling/device_id.h"
#include <string.h>

#include <unity.h>

static uint8_t device_id_work[16]; // the borrow an entry takes; DeviceId never reads it

static uint8_t tw[4096]; // the borrow every namespace call in this suite runs out of

// RFC 9562 Table 3: the DNS namespace ID, as its sixteen octets.
static const uint8_t NS_DNS[16] = {0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
                                   0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};

void setUp(void)
{
}
void tearDown(void)
{
}

static const char HEX[] = "0123456789abcdef";

// RFC 9562 sec 5.5: hash the namespace octets followed by the name, keep the first 16 octets of the
// digest, overwrite the version nibble with 5 and the variant bits with 0b10, then print 8-4-4-4-12.
static void uuid5(const uint8_t ns[16], const char *name, size_t name_len, char out[PROTOCORE_UUID_STR_LEN])
{
    uint8_t in[16 + 64];
    TEST_ASSERT_TRUE(name_len <= sizeof(in) - 16u);
    memcpy(in, ns, 16);
    memcpy(in + 16, name, name_len);

    uint8_t h[PROTOCORE_SHA1_DIGEST_LEN];
    Sha1.hash_args.data = in;
    Sha1.hash_args.len = 16 + name_len;
    Sha1.hash_args.out = h;
    Sha1.hash(tw);
    h[6] = (uint8_t)((h[6] & 0x0Fu) | 0x50u);
    h[8] = (uint8_t)((h[8] & 0x3Fu) | 0x80u);

    static const int GROUP[5] = {4, 2, 2, 2, 6};
    int hi = 0;
    int oi = 0;
    for (int g = 0; g < 5; g++)
    {
        if (g)
        {
            out[oi++] = '-';
        }
        for (int b = 0; b < GROUP[g]; b++)
        {
            out[oi++] = HEX[(h[hi] >> 4) & 0x0Fu];
            out[oi++] = HEX[h[hi] & 0x0Fu];
            hi++;
        }
    }
    out[oi] = '\0';
}

// The module's UUID for @p mac.
static void uuid_of(const uint8_t mac[6], char out[PROTOCORE_UUID_STR_LEN])
{
    memset(out, '#', PROTOCORE_UUID_STR_LEN);
    DeviceId.args.mac = mac;
    DeviceId.args.out = out;
    DeviceId.from_mac(device_id_work);
}

// The twelve lowercase hex characters the module names a MAC by.
static void mac_name(const uint8_t mac[6], char out[13])
{
    for (int i = 0; i < 6; i++)
    {
        out[2 * i] = HEX[(mac[i] >> 4) & 0x0Fu];
        out[2 * i + 1] = HEX[mac[i] & 0x0Fu];
    }
    out[12] = '\0';
}

// RFC 9562 App. A.4, reproduced through the same SHA-1 the module hashes with.
void test_rfc9562_published_uuidv5_vector(void)
{
    // the published SHA-1, before the version and variant octets are overwritten
    static const uint8_t WANT_SHA1[PROTOCORE_SHA1_DIGEST_LEN] = {0x2e, 0xd6, 0x65, 0x7d, 0xe9, 0x27, 0x46,
                                                                 0x8b, 0x55, 0xe1, 0x26, 0x65, 0xa8, 0xae,
                                                                 0xa6, 0xa2, 0x2d, 0xee, 0x3e, 0x35};
    uint8_t in[16 + 15];
    memcpy(in, NS_DNS, 16);
    memcpy(in + 16, "www.example.com", 15);
    uint8_t got[PROTOCORE_SHA1_DIGEST_LEN];
    Sha1.hash_args.data = in;
    Sha1.hash_args.len = sizeof(in);
    Sha1.hash_args.out = got;
    Sha1.hash(tw);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_SHA1, got, sizeof(WANT_SHA1));

    char out[PROTOCORE_UUID_STR_LEN];
    uuid5(NS_DNS, "www.example.com", 15, out);
    TEST_ASSERT_EQUAL_STRING("2ed6657d-e927-568b-95e1-2665a8aea6a2", out);
}

// The module's UUID for a MAC is the UUIDv5 of that MAC's lowercase hex under the DNS namespace.
void test_the_uuid_is_uuidv5_of_the_mac_hex(void)
{
    static const uint8_t MACS[4][6] = {
        {0x24, 0x0a, 0xc4, 0x11, 0x22, 0x33},
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
        {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02},
    };
    for (size_t i = 0; i < 4; i++)
    {
        char name[13];
        char want[PROTOCORE_UUID_STR_LEN];
        char got[PROTOCORE_UUID_STR_LEN];
        mac_name(MACS[i], name);
        uuid5(NS_DNS, name, 12, want);
        uuid_of(MACS[i], got);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, name);
    }
}

// RFC 9562 sec 4: the version nibble is the first character of the third group, and the variant is
// the top bits of the first character of the fourth group - 8, 9, a or b.
void test_the_version_and_variant_nibbles(void)
{
    for (unsigned k = 0; k < 64u; k++)
    {
        uint8_t mac[6] = {(uint8_t)k, (uint8_t)(k * 7u), (uint8_t)(k * 13u), 0x11, 0x22, 0x33};
        char out[PROTOCORE_UUID_STR_LEN];
        uuid_of(mac, out);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('5', out[14], out); // version 5
        char v = out[19];
        TEST_ASSERT_TRUE_MESSAGE(v == '8' || v == '9' || v == 'a' || v == 'b', out); // variant 0b10
    }
}

// The text form is 8-4-4-4-12 lowercase hex with dashes, 36 characters plus the terminator.
void test_the_text_form(void)
{
    static const uint8_t MAC[6] = {0x24, 0x0a, 0xc4, 0x11, 0x22, 0x33};
    char out[PROTOCORE_UUID_STR_LEN];
    uuid_of(MAC, out);

    TEST_ASSERT_EQUAL_UINT(36u, (unsigned)strlen(out));
    TEST_ASSERT_EQUAL_UINT(37u, (unsigned)PROTOCORE_UUID_STR_LEN);
    TEST_ASSERT_EQUAL_CHAR('\0', out[36]);

    static const int DASH[4] = {8, 13, 18, 23};
    for (int i = 0; i < 36; i++)
    {
        proto_bool is_dash = PROTO_FALSE;
        for (int d = 0; d < 4; d++)
        {
            is_dash = is_dash || (i == DASH[d]);
        }
        if (is_dash)
        {
            TEST_ASSERT_EQUAL_CHAR_MESSAGE('-', out[i], out);
        }
        else
        {
            TEST_ASSERT_TRUE_MESSAGE((out[i] >= '0' && out[i] <= '9') || (out[i] >= 'a' && out[i] <= 'f'), out);
        }
    }
}

// The same MAC always yields the same UUID, which is what makes it a stable identity that needs no
// storage. Formatting a different one in between does not change it.
void test_the_uuid_is_stable_for_a_mac(void)
{
    static const uint8_t A[6] = {0x24, 0x0a, 0xc4, 0x11, 0x22, 0x33};
    static const uint8_t B[6] = {0x24, 0x0a, 0xc4, 0x11, 0x22, 0x34};

    char first[PROTOCORE_UUID_STR_LEN];
    char other[PROTOCORE_UUID_STR_LEN];
    char again[PROTOCORE_UUID_STR_LEN];
    uuid_of(A, first);
    uuid_of(B, other);
    uuid_of(A, again);
    TEST_ASSERT_EQUAL_STRING(first, again);
    TEST_ASSERT_TRUE(strcmp(first, other) != 0);
}

// Every octet of the MAC reaches the name, so no two addresses that differ anywhere share a UUID.
void test_every_mac_octet_changes_the_uuid(void)
{
    static const uint8_t BASE[6] = {0x24, 0x0a, 0xc4, 0x11, 0x22, 0x33};
    char base[PROTOCORE_UUID_STR_LEN];
    uuid_of(BASE, base);

    for (int i = 0; i < 6; i++)
    {
        uint8_t mac[6];
        memcpy(mac, BASE, sizeof(mac));
        mac[i] ^= 0x01u;
        char out[PROTOCORE_UUID_STR_LEN];
        uuid_of(mac, out);
        TEST_ASSERT_TRUE_MESSAGE(strcmp(base, out) != 0, out);
    }

    // and a single nibble is enough, so the hex name is not being truncated
    uint8_t nib[6];
    memcpy(nib, BASE, sizeof(nib));
    nib[5] ^= 0xF0u;
    char out[PROTOCORE_UUID_STR_LEN];
    uuid_of(nib, out);
    TEST_ASSERT_TRUE(strcmp(base, out) != 0);
}

// The MAC hex is lowercase, so a name built with uppercase digits is a different name and would
// yield a different UUID. This pins the case the module names an address in.
void test_the_name_is_lowercase_hex(void)
{
    static const uint8_t MAC[6] = {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45};
    char lower[PROTOCORE_UUID_STR_LEN];
    char upper[PROTOCORE_UUID_STR_LEN];
    char got[PROTOCORE_UUID_STR_LEN];

    uuid5(NS_DNS, "abcdef012345", 12, lower);
    uuid5(NS_DNS, "ABCDEF012345", 12, upper);
    TEST_ASSERT_TRUE(strcmp(lower, upper) != 0); // the two names are not the same name

    uuid_of(MAC, got);
    TEST_ASSERT_EQUAL_STRING(lower, got);
}
