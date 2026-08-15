// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SMBus 3.1 Packet Error Code (server/peripherals/smbus.h).
//
// SMBus 3.1 sec 6.4.1.3 defines the PEC as a CRC-8 over EVERY octet of the transaction, the slave
// address octets and their R/W bits included, with the generator x^8 + x^2 + x + 1 and a zero
// initial register. The load-bearing case is test_pec_is_crc8_of_the_address_octet: its expected
// octet is the eight shift-and-XOR steps of that polynomial worked out by hand in the comment above
// it, so a wrong polynomial, a nonzero seed, or an address octet assembled the wrong way cannot
// reproduce it. Everything after that pins how the transaction is LAID OUT (which octets, in which
// order) by checksumming the same sequence through the shared CRC engine, whose CRC-8/SMBUS preset
// carries the catalogue check value 0xF4 asserted in test_crc.

#include "server/peripherals/smbus.h"
#include "shared/crc/crc.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// CRC-8/SMBUS over one contiguous span, through the shared engine.
static uint8_t crc8_smbus(const uint8_t *data, size_t len)
{
    Crc.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.args.data = data;
    Crc.args.len = len;
    Crc.compute(Crc.internal);
    return (uint8_t)Crc.value;
}

// SMBus 3.1 sec 5.5.1: the transaction opens with the 7-bit slave address in bits 7:1 and the
// direction in bit 0, 0 = write, 1 = read.
void test_addr_octet_carries_the_direction_bit(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x50, protocore_smbus_addr_byte(0x28, PROTOCORE_SMBUS_WRITE));
    TEST_ASSERT_EQUAL_HEX8(0x51, protocore_smbus_addr_byte(0x28, PROTOCORE_SMBUS_READ));
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_smbus_addr_byte(0x00, PROTOCORE_SMBUS_WRITE));
    TEST_ASSERT_EQUAL_HEX8(0xFF, protocore_smbus_addr_byte(0x7F, PROTOCORE_SMBUS_READ));
    // The address is 7 bits, so bit 7 of the argument is dropped rather than shifted into bit 8.
    TEST_ASSERT_EQUAL_HEX8(0x50, protocore_smbus_addr_byte(0xA8, PROTOCORE_SMBUS_WRITE));
}

// SMBus 3.1 sec 6.4.1.3: PEC = CRC-8, generator x^8 + x^2 + x + 1 (0x07), register seeded to 0,
// no input or output reflection, no final XOR.
//
// A write to slave 0x2A with no data octets checksums exactly one octet, the write address octet
// 0x2A << 1 = 0x54. Seeding the register with it and running the eight shift steps by hand - shift
// left, and XOR 0x07 whenever the octet shifted out was 1:
//
//   0x54 -> 0xA8            (msb 0)
//   0xA8 -> 0x50 ^ 07 = 57  (msb 1)
//   0x57 -> 0xAE            (msb 0)
//   0xAE -> 0x5C ^ 07 = 5B  (msb 1)
//   0x5B -> 0xB6            (msb 0)
//   0xB6 -> 0x6C ^ 07 = 6B  (msb 1)
//   0x6B -> 0xD6            (msb 0)
//   0xD6 -> 0xAC ^ 07 = AB  (msb 1)
//
// so the PEC is 0xAB.
void test_pec_is_crc8_of_the_address_octet(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xAB, protocore_smbus_pec_write(0x2A, NULL, 0));
}

// A write transaction's covered octets are the write address octet then the command and data, in
// the order they leave the master.
void test_pec_write_covers_address_then_payload(void)
{
    static const uint8_t payload[3] = {0x04, 0x12, 0x34};
    const uint8_t seq[4] = {0x54, 0x04, 0x12, 0x34}; // 0x2A << 1 | 0
    TEST_ASSERT_EQUAL_HEX8(crc8_smbus(seq, sizeof(seq)), protocore_smbus_pec_write(0x2A, payload, sizeof(payload)));
}

// SMBus 3.1 sec 6.4.1.3: a read's PEC spans both halves of the transaction - the write address
// octet and the command that selected the register, then the repeated-start read address octet and
// the octets the slave returned.
void test_pec_read_spans_both_halves_and_the_repeated_start(void)
{
    static const uint8_t sent[1] = {0x08};
    static const uint8_t got[2] = {0xAB, 0xCD};
    const uint8_t seq[5] = {0x54, 0x08, 0x55, 0xAB, 0xCD}; // write addr, cmd, read addr, data
    TEST_ASSERT_EQUAL_HEX8(crc8_smbus(seq, sizeof(seq)),
                           protocore_smbus_pec_read(0x2A, sent, sizeof(sent), got, sizeof(got)));
}

// Receive Byte (sec 6.5.3) sends no command, so the covered octets are the write address octet, the
// read address octet, and the one octet returned.
void test_pec_read_without_a_command(void)
{
    static const uint8_t got[1] = {0x5A};
    const uint8_t seq[3] = {0x54, 0x55, 0x5A};
    TEST_ASSERT_EQUAL_HEX8(crc8_smbus(seq, sizeof(seq)), protocore_smbus_pec_read(0x2A, NULL, 0, got, sizeof(got)));
}

// The address octets are inside the checksum, so the same payload addressed to a different slave
// gets a different PEC. A driver that checksums the payload alone passes every other case here.
void test_pec_binds_to_the_address(void)
{
    static const uint8_t payload[2] = {0x01, 0x02};
    TEST_ASSERT_NOT_EQUAL(protocore_smbus_pec_write(0x2A, payload, sizeof(payload)),
                          protocore_smbus_pec_write(0x2B, payload, sizeof(payload)));
}

// The direction bit is inside the checksum too, so the same octet read and written differ.
void test_pec_binds_to_the_direction(void)
{
    static const uint8_t one[1] = {0x77};
    TEST_ASSERT_NOT_EQUAL(protocore_smbus_pec_write(0x2A, one, 1), protocore_smbus_pec_read(0x2A, NULL, 0, one, 1));
}

// A zero-length payload still checksums the address octet: the same value whether the caller passes
// a null pointer or a pointer with a zero length.
void test_pec_empty_payload_still_covers_the_address(void)
{
    static const uint8_t none[1] = {0x00};
    TEST_ASSERT_EQUAL_HEX8(0xAB, protocore_smbus_pec_write(0x2A, none, 0));
    TEST_ASSERT_EQUAL_HEX8(0xAB, protocore_smbus_pec_write(0x2A, NULL, 0));
}

// Every transaction is checksummed from the seed, never from the register the previous one left.
void test_pec_holds_nothing_between_transactions(void)
{
    static const uint8_t payload[2] = {0xDE, 0xAD};
    uint8_t first = protocore_smbus_pec_write(0x2A, payload, sizeof(payload));
    (void)protocore_smbus_pec_read(0x11, payload, sizeof(payload), payload, sizeof(payload));
    TEST_ASSERT_EQUAL_HEX8(first, protocore_smbus_pec_write(0x2A, payload, sizeof(payload)));
}

// The flag starts off, and reads back whichever way it was last set.
void test_pec_flag_round_trips(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);
    TEST_ASSERT_FALSE(protocore_smbus_pec_enabled());
    protocore_smbus_set_pec(PROTO_TRUE);
    TEST_ASSERT_TRUE(protocore_smbus_pec_enabled());
    protocore_smbus_set_pec(PROTO_FALSE);
    TEST_ASSERT_FALSE(protocore_smbus_pec_enabled());
}

// SMBus 3.1 sec 6.5.4/6.5.5/6.5.7: Send Byte puts one octet on the wire, Write Byte a command and
// one, Write Word a command and two with the low octet first.
void test_write_shapes_put_their_own_octets_on_the_wire(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);

    protocore_bus_host_reset();
    TEST_ASSERT_TRUE(protocore_smbus_send_byte(0x2A, 0x5A));
    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(1u, n);
    TEST_ASSERT_EQUAL_HEX8(0x5A, tx[0]);

    protocore_bus_host_reset();
    TEST_ASSERT_TRUE(protocore_smbus_write_byte(0x2A, 0x10, 0x5A));
    tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(2u, n);
    TEST_ASSERT_EQUAL_HEX8(0x10, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, tx[1]);

    protocore_bus_host_reset();
    TEST_ASSERT_TRUE(protocore_smbus_write_word(0x2A, 0x20, 0xBEEF));
    tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(3u, n);
    TEST_ASSERT_EQUAL_HEX8(0x20, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, tx[1]); // sec 6.5.7: low octet first
    TEST_ASSERT_EQUAL_HEX8(0xBE, tx[2]);
}

// With PEC on, the checksum octet is appended to what the shape already composed, and it is the
// CRC-8 over the address octet plus those octets.
void test_pec_octet_is_appended_to_a_write(void)
{
    protocore_smbus_set_pec(PROTO_TRUE);
    protocore_bus_host_reset();
    TEST_ASSERT_TRUE(protocore_smbus_write_byte(0x2A, 0x10, 0x5A));

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(3u, n);
    const uint8_t seq[3] = {0x54, 0x10, 0x5A};
    TEST_ASSERT_EQUAL_HEX8(crc8_smbus(seq, sizeof(seq)), tx[2]);
    protocore_smbus_set_pec(PROTO_FALSE);
}

// SMBus 3.1 sec 6.5.9: Block Write puts a byte count between the command and the payload.
void test_block_write_counts_the_payload(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);
    protocore_bus_host_reset();
    static const uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    TEST_ASSERT_TRUE(protocore_smbus_write_block(0x2A, 0x30, payload, sizeof(payload)));

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(2u + sizeof(payload), n);
    TEST_ASSERT_EQUAL_HEX8(0x30, tx[0]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)sizeof(payload), tx[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, &tx[2], sizeof(payload));
}

// sec 6.5.9 caps a block at 32 octets, so a longer one is refused before anything reaches the bus.
void test_block_write_refuses_over_the_protocol_cap(void)
{
    TEST_ASSERT_EQUAL_UINT(32u, (unsigned)PROTOCORE_SMBUS_BLOCK_MAX);

    protocore_bus_host_reset();
    uint8_t big[PROTOCORE_SMBUS_BLOCK_MAX + 1] = {0};
    TEST_ASSERT_FALSE(protocore_smbus_write_block(0x2A, 0x30, big, sizeof(big)));
    uint32_t n = 1;
    (void)protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(0u, n);

    // A zero-length block and a null payload are refused the same way.
    TEST_ASSERT_FALSE(protocore_smbus_write_block(0x2A, 0x30, big, 0));
    TEST_ASSERT_FALSE(protocore_smbus_write_block(0x2A, 0x30, NULL, 4));
    (void)protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(0u, n);
}

// sec 6.5.6/6.5.8: a read answers with the octets the slave returned, word reads low octet first.
void test_read_shapes_take_their_octets_back(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);

    protocore_bus_host_reset();
    static const uint8_t one[1] = {0x7E};
    protocore_bus_host_preload(one, sizeof(one));
    uint8_t b = 0;
    TEST_ASSERT_TRUE(protocore_smbus_read_byte(0x2A, 0x40, &b));
    TEST_ASSERT_EQUAL_HEX8(0x7E, b);

    protocore_bus_host_reset();
    static const uint8_t two[2] = {0xEF, 0xBE};
    protocore_bus_host_preload(two, sizeof(two));
    uint16_t w = 0;
    TEST_ASSERT_TRUE(protocore_smbus_read_word(0x2A, 0x41, &w));
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, w); // low octet arrived first
}

// A null destination is refused rather than written through.
void test_reads_refuse_a_null_destination(void)
{
    TEST_ASSERT_FALSE(protocore_smbus_receive_byte(0x2A, NULL));
    TEST_ASSERT_FALSE(protocore_smbus_read_byte(0x2A, 0x40, NULL));
    TEST_ASSERT_FALSE(protocore_smbus_read_word(0x2A, 0x41, NULL));
    TEST_ASSERT_FALSE(protocore_smbus_process_call(0x2A, 0x42, 0, NULL));

    size_t len = 1;
    uint8_t buf[4];
    TEST_ASSERT_FALSE(protocore_smbus_read_block(0x2A, 0x43, NULL, sizeof(buf), &len));
    TEST_ASSERT_FALSE(protocore_smbus_read_block(0x2A, 0x43, buf, sizeof(buf), NULL));
}

// A count the slave reports over the caller's capacity yields no payload, rather than a copy past
// the end of the buffer.
void test_block_read_refuses_a_count_over_the_capacity(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);
    protocore_bus_host_reset();
    static const uint8_t reply[6] = {5, 1, 2, 3, 4, 5}; // count 5, then its five octets
    protocore_bus_host_preload(reply, sizeof(reply));

    uint8_t out[4];
    size_t len = 99;
    TEST_ASSERT_FALSE(protocore_smbus_read_block(0x2A, 0x43, out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)len);
}

// A count of zero is not a block: sec 6.5.10 says a block carries 1..32 octets.
void test_block_read_refuses_a_zero_count(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);
    protocore_bus_host_reset();
    static const uint8_t reply[1] = {0};
    protocore_bus_host_preload(reply, sizeof(reply));

    uint8_t out[8];
    size_t len = 99;
    TEST_ASSERT_FALSE(protocore_smbus_read_block(0x2A, 0x43, out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)len);
}

// sec 6.5.11: Process Call writes a word and reads a word back, both low octet first.
void test_process_call_exchanges_a_word(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);
    protocore_bus_host_reset();
    static const uint8_t reply[2] = {0x34, 0x12};
    protocore_bus_host_preload(reply, sizeof(reply));

    uint16_t out = 0;
    TEST_ASSERT_TRUE(protocore_smbus_process_call(0x2A, 0x50, 0xBEEF, &out));
    TEST_ASSERT_EQUAL_HEX16(0x1234, out);

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(3u, n);
    TEST_ASSERT_EQUAL_HEX8(0x50, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, tx[1]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, tx[2]);
}

// A slave that does not acknowledge fails the shape rather than reporting a value it never sent.
void test_a_slave_that_does_not_acknowledge_fails_the_shape(void)
{
    protocore_smbus_set_pec(PROTO_FALSE);
    protocore_bus_host_reset();
    protocore_bus_host_fail_next(1);
    TEST_ASSERT_FALSE(protocore_smbus_send_byte(0x2A, 0x5A));

    protocore_bus_host_reset();
    protocore_bus_host_fail_next(1);
    uint8_t b = 0xEE;
    TEST_ASSERT_FALSE(protocore_smbus_read_byte(0x2A, 0x40, &b));
    TEST_ASSERT_EQUAL_HEX8(0xEE, b); // untouched
}

// With PEC on, a reply whose checksum octet does not match the recomputed one is rejected: the
// whole point of the octet is that a corrupted reply is refused, not delivered.
void test_a_wrong_pec_on_a_read_is_rejected(void)
{
    protocore_smbus_set_pec(PROTO_TRUE);
    protocore_bus_host_reset();

    const uint8_t good = protocore_smbus_pec_read(0x2A, (const uint8_t[]){0x40}, 1, (const uint8_t[]){0x7E}, 1);
    const uint8_t bad[2] = {0x7E, (uint8_t)(good ^ 0xFFu)};
    protocore_bus_host_preload(bad, sizeof(bad));
    uint8_t b = 0;
    TEST_ASSERT_FALSE(protocore_smbus_read_byte(0x2A, 0x40, &b));

    // The same reply with the correct checksum octet is accepted.
    protocore_bus_host_reset();
    const uint8_t ok[2] = {0x7E, good};
    protocore_bus_host_preload(ok, sizeof(ok));
    TEST_ASSERT_TRUE(protocore_smbus_read_byte(0x2A, 0x40, &b));
    TEST_ASSERT_EQUAL_HEX8(0x7E, b);
    protocore_smbus_set_pec(PROTO_FALSE);
}
