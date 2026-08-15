// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the ADI high-speed-ADC SPI configuration-port codec
// (server/peripherals/ad9238/ad9238.h).
//
// The governing document is Analog Devices AN-877 Rev. A, "Interfacing to High Speed ADCs via SPI"
// (20 pages, fetched this session). Its FORMAT section publishes the instruction word field by field
// (Figure 7 "Instruction Phase Bit Field", plus READ/WRITE, WORD LENGTH, Table 2 and ADDRESS BITS),
// and its CHIP PROGRAMMING section publishes the register addresses verbatim as section headings.
//
// The part the module is named for does not have this port. The AD9238 Data Sheet Rev. D (46 pages,
// 6/2021, fetched this session) has no Serial Port Interface section, no SCLK / SDIO / CSB pin and no
// register map: the strings "SPI", "SCLK", "SDIO" and "CSB" occur zero times in it, and the data
// format is a pin, "20 DFS Data Output Format Select Bit (Low for Offset Binary, High for Twos
// Complement)". ad9238.h cites that data sheet's "Serial Port Interface (SPI)" section, which does
// not exist, so AN-877 is the only published source for anything this codec builds.
//
// Load-bearing: test_an877_transfer_register_write and test_an877_output_mode_write_transaction.
// Each builds a whole transaction from an address AN-877 prints as a section heading (0x0FF, 0x014)
// and a data byte AN-877 prints (Bit 0 software transfer; Table 11 code 01 = twos complement).
//
// Failing by design: the eight test_an877_*_register_is_* cases. AN-877's CHIP PROGRAMMING section
// publishes Modes (0x008), Output Test Modes (0x00D), Analog Input (0x00F), Offset Adjust (0x010),
// Output Mode (0x014), Output Delay Adjust (0x017), Reference Adjust (0x018) and Device Indexing
// (0x004 and 0x005). ad9238.h carries 0x09, 0x15, 0x14, 0x18, 0x0A, 0x0F, 0x10 and 0x08 for those
// same names, with VREF and OFFSET_ADJUST transposed against each other.
//
// AD9238_REG_OUTPUT_PHASE (0x0D) has no counterpart in AN-877 at all - the note's only phase register
// is Clock Divider Phase (0x016) - so no case asserts it.

#include "server/peripherals/ad9238/ad9238.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Build one instruction word and return it as the 16-bit value the two octets carry, MSB first.
static uint16_t word_of(proto_bool read, uint16_t addr, uint8_t nbytes)
{
    uint8_t w[2] = {0xA5u, 0x5Au};
    TEST_ASSERT_TRUE(protocore_ad9238_build_instruction(read, addr, nbytes, w));
    return (uint16_t)(((uint16_t)w[0] << 8) | w[1]);
}

// AN-877 FORMAT, Figure 7 "Instruction Phase Bit Field", the 16 bits in transmission order:
//     R/W W1 W0 A12 A11 A10 A9 A8 A7 A6 A5 A4 A3 A2 A1 A0
// and Bit Order: "In MSB-first mode, the bit order is highest-order bit to lowest-order bit",
// MSB first being the power-up default. So the first octet on the wire is bits 15..8.
//
// Each expected word is that field list laid out as a number:
//   R/W = 1        -> bit 15         -> 0x8000
//   W1:W0 = nbytes-1 -> bits 14:13   -> 0x2000 per step
//   A12..A0        -> bits 12:0      -> the address itself
void test_an877_instruction_phase_bit_field(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000u, word_of(PROTO_FALSE, 0x0000u, 1u));
    TEST_ASSERT_EQUAL_HEX16(0x8000u, word_of(PROTO_TRUE, 0x0000u, 1u));
    TEST_ASSERT_EQUAL_HEX16(0x0001u, word_of(PROTO_FALSE, 0x0001u, 1u));
    TEST_ASSERT_EQUAL_HEX16(0x80FFu, word_of(PROTO_TRUE, 0x00FFu, 1u));
    TEST_ASSERT_EQUAL_HEX16(0x2000u, word_of(PROTO_FALSE, 0x0000u, 2u));
    TEST_ASSERT_EQUAL_HEX16(0x4000u, word_of(PROTO_FALSE, 0x0000u, 3u));
    TEST_ASSERT_EQUAL_HEX16(0x6000u, word_of(PROTO_FALSE, 0x0000u, 4u));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, word_of(PROTO_TRUE, 0x1FFFu, 4u));
}

// AN-877 WORD LENGTH: "The value represented by W1:W0 + 1 is the number of bytes to transfer", and
// Table 2 spells the four settings: 00 one byte, 01 two bytes, 10 three bytes, 11 four or more.
// Inverting that, W1:W0 = nbytes - 1 for every documented count.
void test_an877_word_length_is_w1w0_plus_one(void)
{
    for (uint8_t n = 1u; n <= 4u; n++)
    {
        uint16_t word = word_of(PROTO_FALSE, 0x0123u, n);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)(n - 1u), (uint16_t)((word >> 13) & 0x3u));
        TEST_ASSERT_EQUAL_HEX16(0x0123u, (uint16_t)(word & 0x1FFFu));
    }
}

// AN-877 ADDRESS BITS: "The remaining 13 bits represent the starting address of the data sent."
// 13 bits hold 0x0000 through 0x1FFF, so 0x2000 is the first value the field cannot carry and a
// codec that truncated it would address a different register.
void test_an877_address_field_is_thirteen_bits(void)
{
    uint8_t w[2];
    TEST_ASSERT_TRUE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x1FFFu, 1u, w));
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x2000u, 1u, w));
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_TRUE, 0xFFFFu, 1u, w));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_write(0x2000u, 0x00u, w, 3u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_read(0x2000u, w, 2u));
}

// AN-877 READ/WRITE: "The first bit in the stream is the read/write indicator bit (R/W). When this
// bit is high, a read is being requested." One bit carries the direction, so two words for the same
// address and byte count differ in exactly bit 15 whatever the address is.
void test_an877_read_write_bit_is_the_only_difference(void)
{
    for (uint16_t addr = 0u; addr <= 0x1FFFu; addr = (uint16_t)(addr + 0x0111u))
    {
        uint16_t r = word_of(PROTO_TRUE, addr, 1u);
        uint16_t w = word_of(PROTO_FALSE, addr, 1u);
        TEST_ASSERT_EQUAL_HEX16(0x8000u, (uint16_t)(r ^ w));
    }
}

// AN-877 "TRANSFER REGISTER (MASTER-SLAVE LATCHING) (0x0FF)" and, under it, "Bit 0 - Software
// Transfer: A software transfer is initiated by setting Bit 0 of this register."
//
// The transaction that sentence describes, through Figure 7:
//   R/W = 0, W1:W0 = 00 (one data byte), A12..A0 = 0x0FF -> word 0x00FF -> octets 0x00 0xFF
//   data byte = bit 0 set                                                -> octet 0x01
void test_an877_transfer_register_write(void)
{
    uint8_t out[4] = {0u, 0u, 0u, 0xEEu};
    TEST_ASSERT_EQUAL_size_t(3u, protocore_ad9238_build_transfer(out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out[3]);
    TEST_ASSERT_EQUAL_HEX16(0x00FFu, (uint16_t)AD9238_REG_DEVICE_UPDATE);
}

// AN-877 CHIP PROGRAMMING, three addresses printed as section headings:
//   "CONFIGURATION REGISTER (0X000)" - SDO active, LSB first, soft reset, mirrored into the low nibble
//   "Chip ID (0x001)"                - read-only chip identifier
//   "Chip Grade (0x002)"             - optional grade register
void test_an877_configuration_chip_id_and_grade_addresses(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000u, (uint16_t)AD9238_REG_CHIP_PORT_CONFIG);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, (uint16_t)AD9238_REG_CHIP_ID);
    TEST_ASSERT_EQUAL_HEX16(0x0002u, (uint16_t)AD9238_REG_CHIP_GRADE);
}

// A read of AN-877's Chip ID (0x001) is the instruction word alone: the note's READ/WRITE section has
// the device turn SDIO around and drive the data out, so the controller sends 2 octets and clocks the
// answer back. R/W = 1, W1:W0 = 00, A12..A0 = 0x001 -> 0x8001 -> octets 0x80 0x01.
void test_an877_chip_id_read_transaction(void)
{
    uint8_t out[3] = {0u, 0u, 0xEEu};
    TEST_ASSERT_EQUAL_size_t(2u, protocore_ad9238_build_read(0x001u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x80u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out[2]);
}

// AN-877 "Output Mode (0x014)" with Table 11 "Output Data Format", whose code 01 is twos complement.
// The write that selects it, through Figure 7:
//   R/W = 0, W1:W0 = 00, A12..A0 = 0x014 -> word 0x0014 -> octets 0x00 0x14
//   data byte = Table 11 code 01 in Bits[1:0]           -> octet 0x01
// The address is written as a literal here, not taken from ad9238.h, because the header's own value
// for that name is 0x0A (see test_an877_output_mode_register_is_0x014).
void test_an877_output_mode_write_transaction(void)
{
    uint8_t out[4] = {0u, 0u, 0u, 0xEEu};
    TEST_ASSERT_EQUAL_size_t(3u, protocore_ad9238_build_write(0x014u, 0x01u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out[3]);
}

// AN-877 Table 11, "Output Data Format", Bit 1 to Bit 0:
//   00 offset binary, 01 twos complement, 10 gray code, 11 reserved.
void test_an877_output_data_format_codes(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)AD9238_FORMAT_OFFSET_BINARY);
    TEST_ASSERT_EQUAL_HEX8(0x01u, (uint8_t)AD9238_FORMAT_TWOS_COMPLEMENT);
    TEST_ASSERT_EQUAL_HEX8(0x02u, (uint8_t)AD9238_FORMAT_GRAY_CODE);
}

// AN-877 "Output Test Modes (0x00D)", Bit 3 to Bit 0, the eight settings the note lists in order:
//   0000 normal ADC, 0001 digital midscale, 0010 +FS, 0011 -FS, 0100 alternating checkerboard,
//   0101 PN23 (X23 + X18 + 1), 0110 PN9 (X9 + X5 + 1), 0111 output words toggle between all 1s and 0s.
void test_an877_output_test_mode_codes(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)AD9238_TEST_OFF);
    TEST_ASSERT_EQUAL_HEX8(0x01u, (uint8_t)AD9238_TEST_MIDSCALE_SHORT);
    TEST_ASSERT_EQUAL_HEX8(0x02u, (uint8_t)AD9238_TEST_POS_FULLSCALE);
    TEST_ASSERT_EQUAL_HEX8(0x03u, (uint8_t)AD9238_TEST_NEG_FULLSCALE);
    TEST_ASSERT_EQUAL_HEX8(0x04u, (uint8_t)AD9238_TEST_CHECKERBOARD);
    TEST_ASSERT_EQUAL_HEX8(0x05u, (uint8_t)AD9238_TEST_PN23);
    TEST_ASSERT_EQUAL_HEX8(0x06u, (uint8_t)AD9238_TEST_PN9);
    TEST_ASSERT_EQUAL_HEX8(0x07u, (uint8_t)AD9238_TEST_ONE_ZERO_TOGGLE);
}

// AN-877 WORD LENGTH and Table 2 define W1:W0 over two bits, so 1 through 4 bytes is the whole range
// the field can name. A count outside it has no encoding.
void test_byte_counts_outside_one_to_four_are_refused(void)
{
    uint8_t w[2];
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0000u, 0u, w));
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0000u, 5u, w));
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0000u, 255u, w));
}

// A builder that cannot write the whole transaction writes none of it: a partial instruction word is
// a different address, and a caller that clocked it out would program the wrong register.
void test_null_and_undersized_buffers_fail_closed(void)
{
    uint8_t out[3];
    TEST_ASSERT_FALSE(protocore_ad9238_build_instruction(PROTO_FALSE, 0x0000u, 1u, NULL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_write(0x0009u, 0x01u, NULL, 3u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_write(0x0009u, 0x01u, out, 2u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_read(0x0009u, NULL, 2u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_read(0x0009u, out, 1u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_transfer(NULL, 3u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ad9238_build_transfer(out, 2u));
}

// Every named register addresses a different byte, and every one fits the 13-bit field, so a write to
// one name can never land on another. Pairwise, not name by name.
void test_named_register_addresses_are_distinct(void)
{
    static const uint16_t REG[] = {
        (uint16_t)AD9238_REG_CHIP_PORT_CONFIG, (uint16_t)AD9238_REG_CHIP_ID,      (uint16_t)AD9238_REG_CHIP_GRADE,
        (uint16_t)AD9238_REG_CHANNEL_INDEX,    (uint16_t)AD9238_REG_POWER_DOWN,   (uint16_t)AD9238_REG_OUTPUT_MODE,
        (uint16_t)AD9238_REG_OUTPUT_PHASE,     (uint16_t)AD9238_REG_OUTPUT_DELAY, (uint16_t)AD9238_REG_VREF,
        (uint16_t)AD9238_REG_ANALOG_INPUT,     (uint16_t)AD9238_REG_TEST_IO,      (uint16_t)AD9238_REG_OFFSET_ADJUST,
        (uint16_t)AD9238_REG_DEVICE_UPDATE,
    };
    const size_t n = sizeof(REG) / sizeof(REG[0]);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_TRUE(REG[i] <= 0x1FFFu);
        uint8_t out[3];
        TEST_ASSERT_EQUAL_size_t(3u, protocore_ad9238_build_write(REG[i], 0x00u, out, sizeof(out)));
        uint16_t word = (uint16_t)(((uint16_t)out[0] << 8) | out[1]);
        TEST_ASSERT_EQUAL_HEX16(REG[i], (uint16_t)(word & 0x1FFFu));
        for (size_t j = i + 1; j < n; j++)
        {
            TEST_ASSERT_NOT_EQUAL_HEX16(REG[i], REG[j]);
        }
    }
}

// AN-877 CHIP PROGRAMMING: "Modes (0x008) ... Register 0x008 controls the mode of the chip", with
// Bit 2 to Bit 0 the internal power-down mode and Table 5 the chip power modes.
void test_an877_modes_register_is_0x008(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0008u, (uint16_t)AD9238_REG_POWER_DOWN);
}

// AN-877: "Output Test Modes (0x00D) ... Register 0x00D enables available test modes", Bit 3 to
// Bit 0 carrying the pattern codes test_an877_output_test_mode_codes checks.
void test_an877_output_test_modes_register_is_0x00d(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x000Du, (uint16_t)AD9238_REG_TEST_IO);
}

// AN-877: "Analog Input (0x00F) ... Register 0x00F configures the analog input."
void test_an877_analog_input_register_is_0x00f(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x000Fu, (uint16_t)AD9238_REG_ANALOG_INPUT);
}

// AN-877: "Offset Adjust (0x010)". ad9238.h carries 0x18 for this name and 0x10 for VREF, which is
// the pair transposed - AN-877 puts Reference Adjust at 0x018.
void test_an877_offset_adjust_register_is_0x010(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0010u, (uint16_t)AD9238_REG_OFFSET_ADJUST);
}

// AN-877: "Output Mode (0x014)", the register Table 11's data format field lives in and the one
// "Output Test Modes (0x00D)" refers back to for the format of Test Modes 1, 2, 3, 5 and 6.
void test_an877_output_mode_register_is_0x014(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0014u, (uint16_t)AD9238_REG_OUTPUT_MODE);
}

// AN-877: "Output Delay Adjust (0x017) ... Register 0x017 sets the fine delay in the output latch."
void test_an877_output_delay_adjust_register_is_0x017(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0017u, (uint16_t)AD9238_REG_OUTPUT_DELAY);
}

// AN-877: "Reference Adjust (0x018) ... Register 0x018 allows the internal reference voltage to be
// selected and/or adjusted", Bit 7 to Bit 6 being VREF Select.
void test_an877_reference_adjust_register_is_0x018(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0018u, (uint16_t)AD9238_REG_VREF);
}

// AN-877: "Device Indexing (0x004 and 0x005) ... Register 0x005 references the lower-order devices
// ADC0 through ADC3, while 0x004 references the upper order devices ADC4 through ADC7." A two-channel
// part indexes out of that pair, so the channel-select register is one of those two addresses.
void test_an877_device_index_register_is_0x004_or_0x005(void)
{
    const uint16_t idx = (uint16_t)AD9238_REG_CHANNEL_INDEX;
    TEST_ASSERT_TRUE_MESSAGE(idx == 0x0004u || idx == 0x0005u, "AN-877 indexes devices at 0x004 / 0x005");
}
