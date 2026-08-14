// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for IKEv2 NAT traversal (services/security/ikev2/ikev2_natt.h).
//
// test_rfc7296_detection_notify_layout is the load-bearing case: RFC 7296 sec 3.10 fixes the Notify
// payload's octets and sec 3.10.1 assigns NAT_DETECTION_SOURCE_IP 16388 and
// NAT_DETECTION_DESTINATION_IP 16389, so the whole 28-octet payload can be written out from the
// registry and the field diagram alone. RFC 3948 sec 2.2 gives the four-zero-octet Non-ESP Marker
// and sec 2.3 the one-octet 0xFF keepalive.
//
// No standard publishes a worked NAT-detection digest, so the SHA-1 cases here are properties: the
// digest is 20 octets, it covers SPIi then SPIr then the address then the port big endian, and every
// one of those four inputs changes it. That is stated rather than glossed - the digest value itself
// is not pinned to a published number.

#include "services/security/ikev2/ikev2_natt.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t SPI_I[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
static const uint8_t SPI_R[8] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
static const uint8_t IP4[4] = {192, 0, 2, 1};    // RFC 5737 TEST-NET-1
static const uint8_t IP6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};

static size_t digest_of(const uint8_t *spi_i, const uint8_t *spi_r, const uint8_t *ip, size_t ip_len, uint16_t port,
                        uint8_t *out)
{
    IkeNatt.spi.init_spi = spi_i;
    IkeNatt.spi.resp_spi = spi_r;
    IkeNatt.addr.ip = ip;
    IkeNatt.addr.ip_len = ip_len;
    IkeNatt.addr.port = port;
    IkeNatt.digest.out = out;
    IkeNatt.hash(IkeNatt.internal);
    return IkeNatt.n;
}

// RFC 7296 sec 3.10: Next Payload, C+RESERVED, Payload Length, Protocol ID, SPI Size, Notify Message
// Type, then the Notification Data. A detection payload names no SA, so Protocol ID and SPI Size are
// zero and the payload is 4 + 4 + 20 = 28 octets. Sec 3.10.1 assigns 16388 = 0x4004 to
// NAT_DETECTION_SOURCE_IP and 16389 = 0x4005 to NAT_DETECTION_DESTINATION_IP.
void test_rfc7296_detection_notify_layout(void)
{
    TEST_ASSERT_EQUAL_INT(16388, PROTOCORE_IKE_N_NAT_DETECTION_SOURCE_IP);
    TEST_ASSERT_EQUAL_INT(16389, PROTOCORE_IKE_N_NAT_DETECTION_DESTINATION_IP);
    TEST_ASSERT_EQUAL_INT(20, PROTOCORE_IKE_NATD_HASH_LEN);

    uint8_t want_hash[PROTOCORE_IKE_NATD_HASH_LEN];
    TEST_ASSERT_EQUAL_size_t(20, digest_of(SPI_I, SPI_R, IP4, sizeof(IP4), 500, want_hash));

    uint8_t out[64];
    IkeNatt.spi.init_spi = SPI_I;
    IkeNatt.spi.resp_spi = SPI_R;
    IkeNatt.addr.ip = IP4;
    IkeNatt.addr.ip_len = sizeof(IP4);
    IkeNatt.addr.port = 500;
    IkeNatt.out.buf = out;
    IkeNatt.out.cap = sizeof(out);
    IkeNatt.out.next_payload = IKE_PL_NOTIFY; // another Notify follows this one
    IkeNatt.source_build(IkeNatt.internal);

    TEST_ASSERT_EQUAL_size_t(28, IkeNatt.n);
    TEST_ASSERT_EQUAL_HEX8(IKE_PL_NOTIFY, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]); // the Critical bit is clear
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]); // Payload Length, big endian
    TEST_ASSERT_EQUAL_HEX8(0x1C, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[4]); // Protocol ID: no SA named
    TEST_ASSERT_EQUAL_HEX8(0x00, out[5]); // SPI Size
    TEST_ASSERT_EQUAL_HEX8(0x40, out[6]); // Notify Message Type 16388
    TEST_ASSERT_EQUAL_HEX8(0x04, out[7]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_hash, out + 8, 20);

    // The destination payload is the same shape with type 16389, over the address sent to.
    IkeNatt.out.next_payload = IKE_PL_NONE;
    IkeNatt.dest_build(IkeNatt.internal);
    TEST_ASSERT_EQUAL_size_t(28, IkeNatt.n);
    TEST_ASSERT_EQUAL_HEX8(IKE_PL_NONE, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x40, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0x05, out[7]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_hash, out + 8, 20);
}

// RFC 7296 sec 2.23: the digest is over the SPIs in the order they appear in the header, then the IP
// address, then the port. Each of those four inputs is therefore load-bearing, and the two SPIs are
// not interchangeable.
void test_every_digest_input_matters(void)
{
    uint8_t base[20];
    uint8_t other[20];
    TEST_ASSERT_EQUAL_size_t(20, digest_of(SPI_I, SPI_R, IP4, sizeof(IP4), 500, base));

    // A different initiator SPI.
    uint8_t spi[8];
    memcpy(spi, SPI_I, 8);
    spi[7] = (uint8_t)(spi[7] ^ 0x01);
    digest_of(spi, SPI_R, IP4, sizeof(IP4), 500, other);
    TEST_ASSERT_TRUE(memcmp(base, other, 20) != 0);

    // A different responder SPI.
    memcpy(spi, SPI_R, 8);
    spi[7] = (uint8_t)(spi[7] ^ 0x01);
    digest_of(SPI_I, spi, IP4, sizeof(IP4), 500, other);
    TEST_ASSERT_TRUE(memcmp(base, other, 20) != 0);

    // The SPIs swapped: the header order is part of the digest, so this is a different value.
    digest_of(SPI_R, SPI_I, IP4, sizeof(IP4), 500, other);
    TEST_ASSERT_TRUE(memcmp(base, other, 20) != 0);

    // A different address, which is what a NAT rewrote.
    uint8_t ip[4];
    memcpy(ip, IP4, 4);
    ip[3] = (uint8_t)(ip[3] ^ 0x01);
    digest_of(SPI_I, SPI_R, ip, sizeof(ip), 500, other);
    TEST_ASSERT_TRUE(memcmp(base, other, 20) != 0);

    // A different port, which is what a port-translating NAT rewrote.
    digest_of(SPI_I, SPI_R, IP4, sizeof(IP4), 4500, other);
    TEST_ASSERT_TRUE(memcmp(base, other, 20) != 0);

    // The port goes in big endian, so 0x0100 and 0x0001 are not the same input.
    uint8_t a[20];
    uint8_t b[20];
    digest_of(SPI_I, SPI_R, IP4, sizeof(IP4), 0x0100, a);
    digest_of(SPI_I, SPI_R, IP4, sizeof(IP4), 0x0001, b);
    TEST_ASSERT_TRUE(memcmp(a, b, 20) != 0);

    // The same inputs always give the same digest.
    digest_of(SPI_I, SPI_R, IP4, sizeof(IP4), 500, other);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(base, other, 20);
}

// An IPv6 address is 16 octets where an IPv4 one is 4, and nothing else is an address.
void test_address_length_is_four_or_sixteen(void)
{
    uint8_t out[20];
    TEST_ASSERT_EQUAL_size_t(20, digest_of(SPI_I, SPI_R, IP4, 4, 500, out));
    TEST_ASSERT_EQUAL_size_t(20, digest_of(SPI_I, SPI_R, IP6, 16, 500, out));

    TEST_ASSERT_EQUAL_size_t(0, digest_of(SPI_I, SPI_R, IP4, 0, 500, out));
    TEST_ASSERT_EQUAL_size_t(0, digest_of(SPI_I, SPI_R, IP4, 3, 500, out));
    TEST_ASSERT_EQUAL_size_t(0, digest_of(SPI_I, SPI_R, IP6, 8, 500, out));
    TEST_ASSERT_EQUAL_size_t(0, digest_of(SPI_I, SPI_R, IP6, 17, 500, out));
    TEST_ASSERT_EQUAL_size_t(0, digest_of(NULL, SPI_R, IP4, 4, 500, out));
    TEST_ASSERT_EQUAL_size_t(0, digest_of(SPI_I, NULL, IP4, 4, 500, out));
    TEST_ASSERT_EQUAL_size_t(0, digest_of(SPI_I, SPI_R, NULL, 4, 500, out));

    // A build over an address it cannot digest emits no payload.
    uint8_t buf[64];
    IkeNatt.spi.init_spi = SPI_I;
    IkeNatt.spi.resp_spi = SPI_R;
    IkeNatt.addr.ip = IP4;
    IkeNatt.addr.ip_len = 5;
    IkeNatt.addr.port = 500;
    IkeNatt.out.buf = buf;
    IkeNatt.out.cap = sizeof(buf);
    IkeNatt.out.next_payload = IKE_PL_NONE;
    IkeNatt.source_build(IkeNatt.internal);
    TEST_ASSERT_EQUAL_size_t(0, IkeNatt.n);
    IkeNatt.dest_build(IkeNatt.internal);
    TEST_ASSERT_EQUAL_size_t(0, IkeNatt.n);
}

// RFC 7296 sec 2.23: a recipient recomputes the digest over the addresses it actually observes. A
// match means nothing on that axis was translated; no match means a NAT rewrote it.
void test_nat_detection_verdicts(void)
{
    uint8_t sent[20];
    digest_of(SPI_I, SPI_R, IP4, sizeof(IP4), 500, sent);

    // The observed source is what the peer said it was: no NAT on that axis.
    IkeNatt.spi.init_spi = SPI_I;
    IkeNatt.spi.resp_spi = SPI_R;
    IkeNatt.addr.ip = IP4;
    IkeNatt.addr.ip_len = sizeof(IP4);
    IkeNatt.addr.port = 500;
    IkeNatt.digest.received = sent;
    IkeNatt.match(IkeNatt.internal);
    TEST_ASSERT_TRUE(IkeNatt.ok);
    IkeNatt.peer_behind_nat(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);

    // The datagram arrived from port 4500 instead: the source was translated.
    IkeNatt.addr.port = 4500;
    IkeNatt.match(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);
    IkeNatt.peer_behind_nat(IkeNatt.internal);
    TEST_ASSERT_TRUE(IkeNatt.ok);

    // The same test on the destination axis says this system is behind a NAT.
    static const uint8_t OUR_IP[4] = {198, 51, 100, 7}; // RFC 5737 TEST-NET-2
    uint8_t peer_saw[20];
    digest_of(SPI_I, SPI_R, IP4, sizeof(IP4), 500, peer_saw); // the peer sent to a translated address
    IkeNatt.addr.ip = OUR_IP;
    IkeNatt.addr.ip_len = sizeof(OUR_IP);
    IkeNatt.addr.port = 500;
    IkeNatt.digest.received = peer_saw;
    IkeNatt.self_behind_nat(IkeNatt.internal);
    TEST_ASSERT_TRUE(IkeNatt.ok);

    uint8_t our_own[20];
    digest_of(SPI_I, SPI_R, OUR_IP, sizeof(OUR_IP), 500, our_own);
    IkeNatt.addr.ip = OUR_IP;
    IkeNatt.addr.ip_len = sizeof(OUR_IP);
    IkeNatt.addr.port = 500;
    IkeNatt.digest.received = our_own;
    IkeNatt.self_behind_nat(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);

    // Nothing received is no match, so it reads as a NAT rather than as agreement.
    IkeNatt.digest.received = NULL;
    IkeNatt.match(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);
    IkeNatt.peer_behind_nat(IkeNatt.internal);
    TEST_ASSERT_TRUE(IkeNatt.ok);
}

// A payload this side built is the payload the other side matches against: the two halves agree.
void test_built_payload_carries_the_matching_digest(void)
{
    uint8_t out[64];
    IkeNatt.spi.init_spi = SPI_I;
    IkeNatt.spi.resp_spi = SPI_R;
    IkeNatt.addr.ip = IP6;
    IkeNatt.addr.ip_len = sizeof(IP6);
    IkeNatt.addr.port = PROTOCORE_NATT_PORT;
    IkeNatt.out.buf = out;
    IkeNatt.out.cap = sizeof(out);
    IkeNatt.out.next_payload = IKE_PL_NONE;
    IkeNatt.source_build(IkeNatt.internal);
    TEST_ASSERT_EQUAL_size_t(28, IkeNatt.n);

    IkeNatt.digest.received = out + 8; // the Notification Data, straight off the wire
    IkeNatt.match(IkeNatt.internal);
    TEST_ASSERT_TRUE(IkeNatt.ok);

    // A buffer too small for the whole payload emits nothing.
    IkeNatt.out.cap = 27;
    IkeNatt.source_build(IkeNatt.internal);
    TEST_ASSERT_EQUAL_size_t(0, IkeNatt.n);
    IkeNatt.out.buf = NULL;
    IkeNatt.out.cap = sizeof(out);
    IkeNatt.source_build(IkeNatt.internal);
    TEST_ASSERT_EQUAL_size_t(0, IkeNatt.n);
}

// RFC 3948 sec 2.3: a NAT-keepalive is one octet of value 0xFF. Nothing longer and nothing else is.
void test_rfc3948_keepalive(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xFF, PROTOCORE_NATT_KEEPALIVE_BYTE);

    static const uint8_t KEEPALIVE[1] = {0xFF};
    IkeNatt.pkt.p = KEEPALIVE;
    IkeNatt.pkt.len = 1;
    IkeNatt.is_keepalive(IkeNatt.internal);
    TEST_ASSERT_TRUE(IkeNatt.ok);

    static const uint8_t TWO[2] = {0xFF, 0xFF};
    IkeNatt.pkt.p = TWO;
    IkeNatt.pkt.len = 2;
    IkeNatt.is_keepalive(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);

    static const uint8_t OTHER[1] = {0x00};
    IkeNatt.pkt.p = OTHER;
    IkeNatt.pkt.len = 1;
    IkeNatt.is_keepalive(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);

    IkeNatt.pkt.p = KEEPALIVE;
    IkeNatt.pkt.len = 0;
    IkeNatt.is_keepalive(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);

    IkeNatt.pkt.p = NULL;
    IkeNatt.pkt.len = 1;
    IkeNatt.is_keepalive(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);
}

// RFC 3948 sec 2.2: an IKE message on port 4500 carries four zero octets aligned with the ESP SPI
// field, and sec 2.1 forbids a zero ESP SPI, so a leading zero word tells the two apart.
void test_rfc3948_non_esp_marker(void)
{
    TEST_ASSERT_EQUAL_INT(4500, PROTOCORE_NATT_PORT);
    TEST_ASSERT_EQUAL_INT(4, PROTOCORE_NATT_NON_ESP_MARKER_LEN);

    // Marker then an IKE header whose Initiator's SPI leads.
    static const uint8_t IKE_MSG[12] = {0, 0, 0, 0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    IkeNatt.pkt.p = IKE_MSG;
    IkeNatt.pkt.len = sizeof(IKE_MSG);
    IkeNatt.is_ike(IkeNatt.internal);
    TEST_ASSERT_TRUE(IkeNatt.ok);

    // An ESP packet leads with its non-zero SPI, so any of the four octets being set says ESP.
    for (size_t i = 0; i < 4; i++)
    {
        uint8_t esp[12] = {0, 0, 0, 0, 0x00, 0x00, 0x00, 0x01, 0, 0, 0, 0};
        esp[i] = 0x01;
        IkeNatt.pkt.p = esp;
        IkeNatt.pkt.len = sizeof(esp);
        IkeNatt.is_ike(IkeNatt.internal);
        TEST_ASSERT_FALSE(IkeNatt.ok);
    }

    // A datagram too short to hold the marker is neither.
    static const uint8_t SHORT[3] = {0, 0, 0};
    IkeNatt.pkt.p = SHORT;
    IkeNatt.pkt.len = sizeof(SHORT);
    IkeNatt.is_ike(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);

    IkeNatt.pkt.p = NULL;
    IkeNatt.pkt.len = 8;
    IkeNatt.is_ike(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);

    // A keepalive is not an IKE message: one octet cannot hold the marker.
    static const uint8_t KEEPALIVE[1] = {0xFF};
    IkeNatt.pkt.p = KEEPALIVE;
    IkeNatt.pkt.len = 1;
    IkeNatt.is_ike(IkeNatt.internal);
    TEST_ASSERT_FALSE(IkeNatt.ok);
}
