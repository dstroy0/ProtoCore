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

// The bus cases at the bottom drive a conformant SMBus slave (test/core_setup/hal/host/devices/
// smbus_device.h) that recomputes the PEC independently over the whole message. A checksum taken
// over the payload alone still produces an octet, and a hand-computed expectation can agree with a
// driver that computed the same wrong thing; the slave cannot, because it folds in the address
// octets itself and refuses the message when they do not match.

#include "server/peripherals/smbus/smbus.h"
#include "shared/crc/crc.h"

#include "devices/smbus_device.h"

#include <unity.h>

static uint8_t crc_work[16]; // the borrow an entry takes; Crc never reads it

// The command codes this slave answers, as a real one's datasheet would fix them. The wire-shape
// cases above use 0x40 for a byte and 0x50 for a process call, so the block sits clear of both.
#define TEST_SMBUS_ADDR 0x2Au
#define TEST_SMBUS_BLOCK_CMD 0x60u
#define TEST_SMBUS_PROCESS_CMD 0x50u

static protocore_smbus_dev s_part;

void setUp(void)
{
    protocore_bus_host_reset();
    protocore_bus_host_detach_all();
    protocore_smbus_dev_place(&s_part, TEST_SMBUS_ADDR);
    s_part.block_cmd = TEST_SMBUS_BLOCK_CMD;
    s_part.process_cmd = TEST_SMBUS_PROCESS_CMD;
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
}
void tearDown(void)
{
    protocore_bus_host_detach_all();
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
}

// Turn the PEC on for both ends at once: 6.4.1 has the slave prepared either way, and a suite that
// moved only one of them would be testing a mismatch rather than the checksum.
static void use_pec(proto_bool on)
{
    Smbus.set_pec(protocore_smbus_span(), on);
    s_part.pec = on ? 1u : 0u;
}

// CRC-8/SMBUS over one contiguous span, through the shared engine.
static uint8_t crc8_smbus(const uint8_t *data, size_t len)
{
    CrcV.args.params = &PROTOCORE_CRC8_SMBUS;
    CrcV.args.data = data;
    CrcV.args.len = len;
    Crc.compute(crc_work);
    return (uint8_t)CrcV.value;
}

// SMBus 3.1 sec 5.5.1: the transaction opens with the 7-bit slave address in bits 7:1 and the
// direction in bit 0, 0 = write, 1 = read.
void test_addr_octet_carries_the_direction_bit(void)
{
    uint8_t smbus_value = Smbus.addr_byte(protocore_smbus_span(), 0x28, PROTOCORE_SMBUS_WRITE);
    TEST_ASSERT_EQUAL_HEX8(0x50, smbus_value);
    smbus_value = Smbus.addr_byte(protocore_smbus_span(), 0x28, PROTOCORE_SMBUS_READ);
    TEST_ASSERT_EQUAL_HEX8(0x51, smbus_value);
    smbus_value = Smbus.addr_byte(protocore_smbus_span(), 0x00, PROTOCORE_SMBUS_WRITE);
    TEST_ASSERT_EQUAL_HEX8(0x00, smbus_value);
    smbus_value = Smbus.addr_byte(protocore_smbus_span(), 0x7F, PROTOCORE_SMBUS_READ);
    TEST_ASSERT_EQUAL_HEX8(0xFF, smbus_value);
    // The address is 7 bits, so bit 7 of the argument is dropped rather than shifted into bit 8.
    smbus_value = Smbus.addr_byte(protocore_smbus_span(), 0xA8, PROTOCORE_SMBUS_WRITE);
    TEST_ASSERT_EQUAL_HEX8(0x50, smbus_value);
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
    uint8_t smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2A, NULL, 0);
    TEST_ASSERT_EQUAL_HEX8(0xAB, smbus_value);
}

// A write transaction's covered octets are the write address octet then the command and data, in
// the order they leave the master.
void test_pec_write_covers_address_then_payload(void)
{
    static const uint8_t payload[3] = {0x04, 0x12, 0x34};
    const uint8_t seq[4] = {0x54, 0x04, 0x12, 0x34}; // 0x2A << 1 | 0
    uint8_t smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2A, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_HEX8(crc8_smbus(seq, sizeof(seq)), smbus_value);
}

// SMBus 3.1 sec 6.4.1.3: a read's PEC spans both halves of the transaction - the write address
// octet and the command that selected the register, then the repeated-start read address octet and
// the octets the slave returned.
void test_pec_read_spans_both_halves_and_the_repeated_start(void)
{
    static const uint8_t sent[1] = {0x08};
    static const uint8_t got[2] = {0xAB, 0xCD};
    const uint8_t seq[5] = {0x54, 0x08, 0x55, 0xAB, 0xCD}; // write addr, cmd, read addr, data
    uint8_t smbus_value = Smbus.pec_read(protocore_smbus_span(), 0x2A, sent, sizeof(sent), got, sizeof(got));
    TEST_ASSERT_EQUAL_HEX8(crc8_smbus(seq, sizeof(seq)), smbus_value);
}

// Receive Byte (sec 6.5.3) sends no command, so the covered octets are the write address octet, the
// read address octet, and the one octet returned.
void test_pec_read_without_a_command(void)
{
    static const uint8_t got[1] = {0x5A};
    const uint8_t seq[3] = {0x54, 0x55, 0x5A};
    uint8_t smbus_value = Smbus.pec_read(protocore_smbus_span(), 0x2A, NULL, 0, got, sizeof(got));
    TEST_ASSERT_EQUAL_HEX8(crc8_smbus(seq, sizeof(seq)), smbus_value);
}

// The address octets are inside the checksum, so the same payload addressed to a different slave
// gets a different PEC. A driver that checksums the payload alone passes every other case here.
void test_pec_binds_to_the_address(void)
{
    static const uint8_t payload[2] = {0x01, 0x02};
    // The first address is captured before the second runs: both report through the one namespace,
    // so comparing them in a single expression would compare the second with itself.
    uint8_t smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2A, payload, sizeof(payload));
    const uint8_t at_2a = smbus_value;
    smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2B, payload, sizeof(payload));
    TEST_ASSERT_NOT_EQUAL(at_2a, smbus_value);
}

// The direction bit is inside the checksum too, so the same octet read and written differ.
void test_pec_binds_to_the_direction(void)
{
    static const uint8_t one[1] = {0x77};
    // The write direction is captured before the read runs: both report through the one namespace.
    uint8_t smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2A, one, 1);
    const uint8_t written = smbus_value;
    smbus_value = Smbus.pec_read(protocore_smbus_span(), 0x2A, NULL, 0, one, 1);
    TEST_ASSERT_NOT_EQUAL(written, smbus_value);
}

// A zero-length payload still checksums the address octet: the same value whether the caller passes
// a null pointer or a pointer with a zero length.
void test_pec_empty_payload_still_covers_the_address(void)
{
    static const uint8_t none[1] = {0x00};
    uint8_t smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2A, none, 0);
    TEST_ASSERT_EQUAL_HEX8(0xAB, smbus_value);
    smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2A, NULL, 0);
    TEST_ASSERT_EQUAL_HEX8(0xAB, smbus_value);
}

// Every transaction is checksummed from the seed, never from the register the previous one left.
void test_pec_holds_nothing_between_transactions(void)
{
    static const uint8_t payload[2] = {0xDE, 0xAD};
    uint8_t smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2A, payload, sizeof(payload));
    uint8_t first = smbus_value;
    smbus_value = Smbus.pec_read(protocore_smbus_span(), 0x11, payload, sizeof(payload), payload, sizeof(payload));
    (void)smbus_value;
    smbus_value = Smbus.pec_write(protocore_smbus_span(), 0x2A, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_HEX8(first, smbus_value);
}

// The flag starts off, and reads back whichever way it was last set.
void test_pec_flag_round_trips(void)
{
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
    proto_bool smbus_ok = Smbus.pec_enabled(protocore_smbus_span());
    TEST_ASSERT_FALSE(smbus_ok);
    Smbus.set_pec(protocore_smbus_span(), PROTO_TRUE);
    smbus_ok = Smbus.pec_enabled(protocore_smbus_span());
    TEST_ASSERT_TRUE(smbus_ok);
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
    smbus_ok = Smbus.pec_enabled(protocore_smbus_span());
    TEST_ASSERT_FALSE(smbus_ok);
}

// SMBus 3.1 sec 6.5.4/6.5.5/6.5.7: Send Byte puts one octet on the wire, Write Byte a command and
// one, Write Word a command and two with the low octet first.
void test_write_shapes_put_their_own_octets_on_the_wire(void)
{
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);

    protocore_bus_host_reset();
    proto_bool smbus_ok = Smbus.send_byte(protocore_smbus_span(), 0x2A, 0x5A);
    TEST_ASSERT_TRUE(smbus_ok);
    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(1u, n);
    TEST_ASSERT_EQUAL_HEX8(0x5A, tx[0]);

    protocore_bus_host_reset();
    smbus_ok = Smbus.write_byte(protocore_smbus_span(), 0x2A, 0x10, 0x5A);
    TEST_ASSERT_TRUE(smbus_ok);
    tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(2u, n);
    TEST_ASSERT_EQUAL_HEX8(0x10, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, tx[1]);

    protocore_bus_host_reset();
    smbus_ok = Smbus.write_word(protocore_smbus_span(), 0x2A, 0x20, 0xBEEF);
    TEST_ASSERT_TRUE(smbus_ok);
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
    Smbus.set_pec(protocore_smbus_span(), PROTO_TRUE);
    protocore_bus_host_reset();
    proto_bool smbus_ok = Smbus.write_byte(protocore_smbus_span(), 0x2A, 0x10, 0x5A);
    TEST_ASSERT_TRUE(smbus_ok);

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(3u, n);
    const uint8_t seq[3] = {0x54, 0x10, 0x5A};
    TEST_ASSERT_EQUAL_HEX8(crc8_smbus(seq, sizeof(seq)), tx[2]);
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
}

// SMBus 3.1 sec 6.5.9: Block Write puts a byte count between the command and the payload.
void test_block_write_counts_the_payload(void)
{
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
    protocore_bus_host_reset();
    static const uint8_t payload[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    proto_bool smbus_ok = Smbus.write_block(protocore_smbus_span(), 0x2A, 0x30, payload, sizeof(payload));
    TEST_ASSERT_TRUE(smbus_ok);

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
    proto_bool smbus_ok = Smbus.write_block(protocore_smbus_span(), 0x2A, 0x30, big, sizeof(big));
    TEST_ASSERT_FALSE(smbus_ok);
    uint32_t n = 1;
    (void)protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(0u, n);

    // A zero-length block and a null payload are refused the same way.
    smbus_ok = Smbus.write_block(protocore_smbus_span(), 0x2A, 0x30, big, 0);
    TEST_ASSERT_FALSE(smbus_ok);
    smbus_ok = Smbus.write_block(protocore_smbus_span(), 0x2A, 0x30, NULL, 4);
    TEST_ASSERT_FALSE(smbus_ok);
    (void)protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(0u, n);
}

// sec 6.5.6/6.5.8: a read answers with the octets the slave returned, word reads low octet first.
void test_read_shapes_take_their_octets_back(void)
{
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);

    s_part.reg[0x40] = 0x7E;
    uint8_t b = 0;
    proto_bool smbus_ok = Smbus.read_byte(protocore_smbus_span(), 0x2A, 0x40, &b);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX8(0x7E, b);

    s_part.reg[0x41] = 0xBEEF;
    uint16_t w = 0;
    smbus_ok = Smbus.read_word(protocore_smbus_span(), 0x2A, 0x41, &w);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, w); // low octet arrived first
}

// A null destination is refused rather than written through.
void test_reads_refuse_a_null_destination(void)
{
    proto_bool smbus_ok = Smbus.receive_byte(protocore_smbus_span(), 0x2A, NULL);
    TEST_ASSERT_FALSE(smbus_ok);
    smbus_ok = Smbus.read_byte(protocore_smbus_span(), 0x2A, 0x40, NULL);
    TEST_ASSERT_FALSE(smbus_ok);
    smbus_ok = Smbus.read_word(protocore_smbus_span(), 0x2A, 0x41, NULL);
    TEST_ASSERT_FALSE(smbus_ok);
    smbus_ok = Smbus.process_call(protocore_smbus_span(), 0x2A, 0x42, 0, NULL);
    TEST_ASSERT_FALSE(smbus_ok);

    size_t len = 1;
    uint8_t buf[4];
    smbus_ok = Smbus.read_block(protocore_smbus_span(), 0x2A, 0x43, NULL, sizeof(buf), &len);
    TEST_ASSERT_FALSE(smbus_ok);
    smbus_ok = Smbus.read_block(protocore_smbus_span(), 0x2A, 0x43, buf, sizeof(buf), NULL);
    TEST_ASSERT_FALSE(smbus_ok);
}

// A count the slave reports over the caller's capacity yields no payload, rather than a copy past
// the end of the buffer.
void test_block_read_refuses_a_count_over_the_capacity(void)
{
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
    protocore_bus_host_reset();
    static const uint8_t reply[6] = {5, 1, 2, 3, 4, 5}; // count 5, then its five octets
    protocore_bus_host_preload(reply, sizeof(reply));

    uint8_t out[4];
    size_t len = 99;
    proto_bool smbus_ok = Smbus.read_block(protocore_smbus_span(), 0x2A, 0x43, out, sizeof(out), &len);
    TEST_ASSERT_FALSE(smbus_ok);
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)len);
}

// A count of zero is not a block: sec 6.5.10 says a block carries 1..32 octets.
void test_block_read_refuses_a_zero_count(void)
{
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
    protocore_bus_host_reset();
    static const uint8_t reply[1] = {0};
    protocore_bus_host_preload(reply, sizeof(reply));

    uint8_t out[8];
    size_t len = 99;
    proto_bool smbus_ok = Smbus.read_block(protocore_smbus_span(), 0x2A, 0x43, out, sizeof(out), &len);
    TEST_ASSERT_FALSE(smbus_ok);
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)len);
}

// sec 6.5.11: Process Call writes a word and reads a word back, both low octet first.
void test_process_call_exchanges_a_word(void)
{
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
    // 6.5.6: the slave answers with a word it computed, not the one it was sent.
    s_part.process_reply = 0x1234;
    protocore_bus_host_reset();

    uint16_t out = 0;
    proto_bool smbus_ok = Smbus.process_call(protocore_smbus_span(), 0x2A, TEST_SMBUS_PROCESS_CMD, 0xBEEF, &out);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX16(0x1234, out);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, s_part.reg[TEST_SMBUS_PROCESS_CMD]); // and it got what was sent

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
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
    protocore_bus_host_reset();
    protocore_bus_host_fail_next(1);
    proto_bool smbus_ok = Smbus.send_byte(protocore_smbus_span(), 0x2A, 0x5A);
    TEST_ASSERT_FALSE(smbus_ok);

    protocore_bus_host_reset();
    protocore_bus_host_fail_next(1);
    uint8_t b = 0xEE;
    smbus_ok = Smbus.read_byte(protocore_smbus_span(), 0x2A, 0x40, &b);
    TEST_ASSERT_FALSE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX8(0xEE, b); // untouched
}

// With PEC on, a reply whose checksum octet does not match the recomputed one is rejected: the
// whole point of the octet is that a corrupted reply is refused, not delivered.
void test_a_wrong_pec_on_a_read_is_rejected(void)
{
    Smbus.set_pec(protocore_smbus_span(), PROTO_TRUE);
    protocore_bus_host_reset();

    // The slave supplies a corrupted checksum, which is the line noise 6.4 exists to catch.
    s_part.pec = 1u;
    s_part.reg[0x40] = 0x7E;
    s_part.corrupt_pec = 1u;
    uint8_t b = 0;
    proto_bool smbus_ok = Smbus.read_byte(protocore_smbus_span(), 0x2A, 0x40, &b);
    TEST_ASSERT_FALSE(smbus_ok);

    // The same reply with the correct checksum octet is accepted.
    s_part.corrupt_pec = 0u;
    smbus_ok = Smbus.read_byte(protocore_smbus_span(), 0x2A, 0x40, &b);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX8(0x7E, b);
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE);
}

// ---------------------------------------------------------------------------
// Over the bus, against a conformant slave
// ---------------------------------------------------------------------------

// A byte written to a command code is the byte read back from it. Without a PEC this is the plain
// 6.5.4 / 6.5.5 pair.
void test_a_byte_round_trips_through_a_command_code(void)
{
    proto_bool smbus_ok = Smbus.write_byte(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x10u, 0xA5u);
    TEST_ASSERT_TRUE(smbus_ok);

    uint8_t got = 0;
    smbus_ok = Smbus.read_byte(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x10u, &got);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, got);
}

// 6.5.5: a word goes out low octet first, so a round trip that swapped them would come back with
// the halves exchanged rather than equal.
void test_a_word_round_trips_low_octet_first(void)
{
    proto_bool smbus_ok = Smbus.write_word(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x11u, 0x1234u);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, s_part.reg[0x11u]);

    uint16_t got = 0;
    smbus_ok = Smbus.read_word(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x11u, &got);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, got);
}

// 6.5.7: a block write is a command, a count and that many octets, and the count does not include
// the PEC. A block read gets the count back before the payload.
void test_a_block_round_trips_with_its_count(void)
{
    static const uint8_t payload[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    proto_bool smbus_ok =
        Smbus.write_block(protocore_smbus_span(), TEST_SMBUS_ADDR, TEST_SMBUS_BLOCK_CMD, payload, sizeof(payload));
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_UINT8(5u, s_part.block_len);

    uint8_t got[PROTOCORE_SMBUS_BLOCK_MAX] = {0};
    size_t len = 0;
    smbus_ok = Smbus.read_block(protocore_smbus_span(), TEST_SMBUS_ADDR, TEST_SMBUS_BLOCK_CMD, got, sizeof(got), &len);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, got, sizeof(payload));
}

// 6.4: "The PEC is a CRC-8 error-checking byte, calculated on all the message bytes (including
// addresses and read/write bits)." The slave folds the address octets in itself, so a driver that
// checksummed only the payload is refused - and 6.4.1 says a message with a wrong PEC is not
// processed at all, which is what the rejection count records.
void test_smbus31_the_pec_spans_the_address_octets(void)
{
    use_pec(PROTO_TRUE);
    proto_bool smbus_ok = Smbus.write_byte(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x20u, 0x5Au);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_UINT8(0u, s_part.rejected); // the slave agreed with the driver's checksum
    TEST_ASSERT_EQUAL_HEX16(0x5Au, s_part.reg[0x20u]);
}

// And the same in the other direction: the slave supplies a PEC over the whole message and the
// driver verifies it, so a reading only comes back when both computed the same span.
void test_smbus31_a_read_verifies_the_pec_the_slave_supplied(void)
{
    use_pec(PROTO_TRUE);
    s_part.reg[0x21u] = 0x77u;
    uint8_t got = 0;
    proto_bool smbus_ok = Smbus.read_byte(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x21u, &got);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX8(0x77u, got);
}

// 6.4.1: "verify the correctness of the PEC if present, and only process the message if the PEC is
// correct." With the checksum on at one end only, the octet counts still line up but the value does
// not, so nothing is stored.
void test_smbus31_a_wrong_pec_is_not_processed(void)
{
    s_part.pec = 1u;                                    // the slave expects a PEC
    Smbus.set_pec(protocore_smbus_span(), PROTO_FALSE); // the driver does not send one
    Smbus.write_word(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x22u, 0xBEEFu);
    TEST_ASSERT_EQUAL_UINT8(1u, s_part.rejected);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, s_part.reg[0x22u]); // refused, so nothing landed
}

// A word and a block both round trip with the checksum on, so the PEC spans hold for a transaction
// longer than one octet as well.
void test_smbus31_a_word_and_a_block_round_trip_with_the_pec_on(void)
{
    use_pec(PROTO_TRUE);
    proto_bool smbus_ok = Smbus.write_word(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x23u, 0xCAFEu);
    TEST_ASSERT_TRUE(smbus_ok);
    uint16_t word = 0;
    smbus_ok = Smbus.read_word(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x23u, &word);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX16(0xCAFEu, word);

    static const uint8_t payload[3] = {0x01, 0x02, 0x03};
    smbus_ok =
        Smbus.write_block(protocore_smbus_span(), TEST_SMBUS_ADDR, TEST_SMBUS_BLOCK_CMD, payload, sizeof(payload));
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_UINT8(0u, s_part.rejected);

    uint8_t got[PROTOCORE_SMBUS_BLOCK_MAX] = {0};
    size_t len = 0;
    smbus_ok = Smbus.read_block(protocore_smbus_span(), TEST_SMBUS_ADDR, TEST_SMBUS_BLOCK_CMD, got, sizeof(got), &len);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, got, sizeof(payload));
}

// 6.4: the checksum covers the address, so the same command and value sent to a different address
// is a different message. Two slaves on one bus keep their own command codes.
void test_smbus31_two_slaves_keep_their_own_command_codes(void)
{
    static protocore_smbus_dev other;
    protocore_smbus_dev_place(&other, 0x2Bu);

    Smbus.write_byte(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x30u, 0x11u);
    Smbus.write_byte(protocore_smbus_span(), 0x2Bu, 0x30u, 0x22u);

    uint8_t got = 0;
    proto_bool smbus_ok = Smbus.read_byte(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x30u, &got);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX8(0x11u, got);
    smbus_ok = Smbus.read_byte(protocore_smbus_span(), 0x2Bu, 0x30u, &got);
    TEST_ASSERT_TRUE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX8(0x22u, got);
}

// A refused transfer is reported rather than passed off as a completed transaction.
void test_a_refused_transfer_fails_the_write(void)
{
    protocore_bus_host_fail = 1u;
    proto_bool smbus_ok = Smbus.write_byte(protocore_smbus_span(), TEST_SMBUS_ADDR, 0x31u, 0x99u);
    TEST_ASSERT_FALSE(smbus_ok);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, s_part.reg[0x31u]);
}
