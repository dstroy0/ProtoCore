// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Modbus TCP / RTU slave core (services/fieldbus/modbus/modbus.h).
//
// The load-bearing case is test_map_published_pdu_examples: Modbus Application Protocol V1.1b3
// prints, for every function code this server implements, a request PDU and the exact response PDU
// a conforming server returns (sections 6.1 to 6.12, and section 7 for the exception form). Each of
// those examples is loaded into the data model here and the server's answer is compared octet for
// octet against the published one. Bit packing order, big-endian register order, byte counts and
// the +0x80 exception function code are all pinned by that single table.

#include "services/fieldbus/modbus/modbus.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
    protocore_modbus_server_init();
}
void tearDown(void)
{
}

// Wrap a PDU in an MBAP header and run it, returning the response PDU length. The MBAP Length field
// counts the Unit Identifier plus the PDU (Modbus Messaging on TCP/IP V1.0b section 3.1.3).
static uint8_t g_resp[MODBUS_ADU_MAX + 16];

static size_t run_pdu(const uint8_t *pdu, size_t pdu_len, const uint8_t **out_pdu)
{
    uint8_t adu[MODBUS_ADU_MAX + 16];
    adu[0] = 0x12; // Transaction Identifier
    adu[1] = 0x34;
    adu[2] = 0x00; // Protocol Identifier = 0
    adu[3] = 0x00;
    adu[4] = (uint8_t)((1 + pdu_len) >> 8);
    adu[5] = (uint8_t)((1 + pdu_len) & 0xFF);
    adu[6] = 0x07; // Unit Identifier
    memcpy(adu + 7, pdu, pdu_len);

    size_t n = protocore_modbus_process_adu(adu, 7 + pdu_len, g_resp, sizeof(g_resp));
    if (n == 0)
    {
        return 0;
    }
    TEST_ASSERT_EQUAL_HEX8(0x12, g_resp[0]); // the Transaction Identifier is recopied
    TEST_ASSERT_EQUAL_HEX8(0x34, g_resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_resp[3]);
    TEST_ASSERT_EQUAL_HEX8(0x07, g_resp[6]); // and so is the Unit Identifier
    size_t declared = (size_t)((g_resp[4] << 8) | g_resp[5]);
    TEST_ASSERT_EQUAL_UINT(n - 6, declared); // Length counts the unit id and the response PDU
    if (out_pdu)
    {
        *out_pdu = g_resp + 7;
    }
    return n - 7;
}

// Set a run of coils from a packed bitfield, LSB of the first byte being the lowest address.
static void load_coils(uint16_t start, uint16_t qty, const uint8_t *packed)
{
    for (uint16_t i = 0; i < qty; i++)
    {
        protocore_modbus_set_coil((uint16_t)(start + i), (packed[i >> 3] >> (i & 7)) & 1u);
    }
}

static void load_discrete(uint16_t start, uint16_t qty, const uint8_t *packed)
{
    for (uint16_t i = 0; i < qty; i++)
    {
        protocore_modbus_set_discrete_input((uint16_t)(start + i), (packed[i >> 3] >> (i & 7)) & 1u);
    }
}

// Modbus Application Protocol V1.1b3 sections 6.1 - 6.12 and 7: one worked request/response pair
// per implemented function code, transcribed from the spec's own tables.
void test_map_published_pdu_examples(void)
{
    const uint8_t *rsp = NULL;

    // section 6.1, "read discrete outputs 20-38": start 0013h, quantity 0013h. The published
    // response packs the coil states as CD 6B 05.
    static const uint8_t COILS_1[3] = {0xCD, 0x6B, 0x05};
    load_coils(0x0013, 0x0013, COILS_1);
    static const uint8_t REQ_1[5] = {0x01, 0x00, 0x13, 0x00, 0x13};
    static const uint8_t RSP_1[5] = {0x01, 0x03, 0xCD, 0x6B, 0x05};
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_1), run_pdu(REQ_1, sizeof(REQ_1), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP_1, rsp, sizeof(RSP_1));

    // section 6.2, "read discrete inputs 197-218": start 00C4h, quantity 0016h, answer AC DB 35.
    static const uint8_t INPUTS_2[3] = {0xAC, 0xDB, 0x35};
    load_discrete(0x00C4, 0x0016, INPUTS_2);
    static const uint8_t REQ_2[5] = {0x02, 0x00, 0xC4, 0x00, 0x16};
    static const uint8_t RSP_2[5] = {0x02, 0x03, 0xAC, 0xDB, 0x35};
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_2), run_pdu(REQ_2, sizeof(REQ_2), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP_2, rsp, sizeof(RSP_2));

    // section 6.3, "read registers 108-110": 022Bh (555), 0000h (0), 0064h (100).
    protocore_modbus_set_holding_reg(0x006B, 0x022B);
    protocore_modbus_set_holding_reg(0x006C, 0x0000);
    protocore_modbus_set_holding_reg(0x006D, 0x0064);
    static const uint8_t REQ_3[5] = {0x03, 0x00, 0x6B, 0x00, 0x03};
    static const uint8_t RSP_3[8] = {0x03, 0x06, 0x02, 0x2B, 0x00, 0x00, 0x00, 0x64};
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_3), run_pdu(REQ_3, sizeof(REQ_3), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP_3, rsp, sizeof(RSP_3));

    // section 6.4, "read input register 9": contents 000Ah (10 decimal).
    protocore_modbus_set_input_reg(0x0008, 0x000A);
    static const uint8_t REQ_4[5] = {0x04, 0x00, 0x08, 0x00, 0x01};
    static const uint8_t RSP_4[4] = {0x04, 0x02, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_4), run_pdu(REQ_4, sizeof(REQ_4), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP_4, rsp, sizeof(RSP_4));

    // section 6.5, "write Coil 173 ON": the normal response echoes the request.
    static const uint8_t REQ_5[5] = {0x05, 0x00, 0xAC, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_UINT(sizeof(REQ_5), run_pdu(REQ_5, sizeof(REQ_5), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_5, rsp, sizeof(REQ_5));
    TEST_ASSERT_TRUE(protocore_modbus_get_coil(0x00AC));

    // section 6.6, "write register 2 to 0003h": the normal response echoes the request.
    static const uint8_t REQ_6[5] = {0x06, 0x00, 0x01, 0x00, 0x03};
    TEST_ASSERT_EQUAL_UINT(sizeof(REQ_6), run_pdu(REQ_6, sizeof(REQ_6), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_6, rsp, sizeof(REQ_6));
    TEST_ASSERT_EQUAL_HEX16(0x0003, protocore_modbus_get_holding_reg(0x0001));

    // section 6.11, "write a series of 10 coils starting at coil 20": data CD 01, and the response
    // is the function code, starting address and quantity only.
    static const uint8_t REQ_15[8] = {0x0F, 0x00, 0x13, 0x00, 0x0A, 0x02, 0xCD, 0x01};
    static const uint8_t RSP_15[5] = {0x0F, 0x00, 0x13, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_15), run_pdu(REQ_15, sizeof(REQ_15), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP_15, rsp, sizeof(RSP_15));
    // the spec's own bit table for this example reads
    //   Bit:     1 1 0 0 1 1 0 1   0 0 0 0 0 0 0 1
    //   Output: 27 26 25 24 23 22 21 20   -  -  -  -  -  - 29 28
    // so outputs 20..29, which are PDU addresses 19..28, are 1 0 1 1 0 0 1 1 1 0
    static const uint8_t WANT_BITS[10] = {1, 0, 1, 1, 0, 0, 1, 1, 1, 0};
    for (uint16_t i = 0; i < 10; i++)
    {
        TEST_ASSERT_EQUAL_INT(WANT_BITS[i], protocore_modbus_get_coil((uint16_t)(0x0013 + i)) ? 1 : 0);
    }

    // section 6.12, "write two registers starting at 2 to 000A and 0102 hex".
    static const uint8_t REQ_16[10] = {0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x01, 0x02};
    static const uint8_t RSP_16[5] = {0x10, 0x00, 0x01, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_16), run_pdu(REQ_16, sizeof(REQ_16), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP_16, rsp, sizeof(RSP_16));
    TEST_ASSERT_EQUAL_HEX16(0x000A, protocore_modbus_get_holding_reg(0x0001));
    TEST_ASSERT_EQUAL_HEX16(0x0102, protocore_modbus_get_holding_reg(0x0002));

    // section 7, the exception example: a Read Coils of address 04A1h (1185) that does not exist in
    // the server answers function code 81h with exception code 02 (ILLEGAL DATA ADDRESS).
    static const uint8_t REQ_EX[5] = {0x01, 0x04, 0xA1, 0x00, 0x01};
    static const uint8_t RSP_EX[2] = {0x81, 0x02};
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP_EX), run_pdu(REQ_EX, sizeof(REQ_EX), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP_EX, rsp, sizeof(RSP_EX));
}

// Modbus Application Protocol V1.1b3 section 7: "the server sets the MSB of the function code to 1
// ... exactly 80 hexadecimal higher". An unsupported function draws exception 01.
void test_unsupported_function_returns_illegal_function(void)
{
    const uint8_t *rsp = NULL;
    static const uint8_t UNSUPPORTED[6] = {0x07, 0x08, 0x0B, 0x0C, 0x11, 0x14};
    for (size_t i = 0; i < sizeof(UNSUPPORTED); i++)
    {
        uint8_t pdu[5] = {UNSUPPORTED[i], 0x00, 0x00, 0x00, 0x01};
        TEST_ASSERT_EQUAL_UINT(2u, run_pdu(pdu, sizeof(pdu), &rsp));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(UNSUPPORTED[i] + 0x80), rsp[0]);
        TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_FUNCTION, rsp[1]);
    }
}

// Modbus Application Protocol V1.1b3 figures 11-14: the quantity of coils/inputs must be 0001h to
// 07D0h and the quantity of registers 0001h to 007Dh, else exception 03 (ILLEGAL DATA VALUE).
void test_quantity_bounds(void)
{
    const uint8_t *rsp = NULL;
    struct
    {
        uint8_t fc;
        uint16_t hi_ok;  // the largest quantity the spec allows
        uint16_t hi_bad; // one past it
        uint16_t limit;  // how many of that item this build's data model holds
    } static const CASES[] = {
        {MODBUS_FC_READ_COILS, 2000, 2001, PROTOCORE_MODBUS_COILS},
        {MODBUS_FC_READ_DISCRETE_INPUTS, 2000, 2001, PROTOCORE_MODBUS_DISCRETE_INPUTS},
        {MODBUS_FC_READ_HOLDING_REGS, 125, 126, PROTOCORE_MODBUS_HOLDING_REGS},
        {MODBUS_FC_READ_INPUT_REGS, 125, 126, PROTOCORE_MODBUS_INPUT_REGS},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t pdu[5] = {CASES[i].fc, 0x00, 0x00, 0x00, 0x00};
        proto_bool is_bit = (CASES[i].fc == MODBUS_FC_READ_COILS || CASES[i].fc == MODBUS_FC_READ_DISCRETE_INPUTS);

        pdu[3] = 0x00; // a quantity of zero is below the 0001h floor
        pdu[4] = 0x00;
        TEST_ASSERT_EQUAL_UINT(2u, run_pdu(pdu, sizeof(pdu), &rsp));
        TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_VALUE, rsp[1]);

        pdu[3] = (uint8_t)(CASES[i].hi_bad >> 8);
        pdu[4] = (uint8_t)CASES[i].hi_bad;
        TEST_ASSERT_EQUAL_UINT(2u, run_pdu(pdu, sizeof(pdu), &rsp));
        TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_VALUE, rsp[1]);

        // the top of the spec's range is never a data-value problem: it either reads, or it runs
        // off the end of this build's data model and is a data-address problem instead
        pdu[3] = (uint8_t)(CASES[i].hi_ok >> 8);
        pdu[4] = (uint8_t)CASES[i].hi_ok;
        size_t n = run_pdu(pdu, sizeof(pdu), &rsp);
        if (CASES[i].hi_ok > CASES[i].limit)
        {
            TEST_ASSERT_EQUAL_UINT(2u, n);
            TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, rsp[1]);
        }
        else
        {
            uint8_t bytes = is_bit ? (uint8_t)((CASES[i].hi_ok + 7) / 8) : (uint8_t)(CASES[i].hi_ok * 2);
            TEST_ASSERT_EQUAL_UINT((size_t)2 + bytes, n);
            TEST_ASSERT_EQUAL_HEX8(CASES[i].fc, rsp[0]);
            TEST_ASSERT_EQUAL_HEX8(bytes, rsp[1]);
        }
    }
}

// Modbus Application Protocol V1.1b3 figures 11-14: "Starting Address + Quantity == OK" is one
// check, so the last in-range element passes and one past it does not.
void test_address_plus_quantity_boundary(void)
{
    const uint8_t *rsp = NULL;
    uint16_t last = PROTOCORE_MODBUS_HOLDING_REGS - 1;

    uint8_t ok[5] = {MODBUS_FC_READ_HOLDING_REGS, (uint8_t)(last >> 8), (uint8_t)last, 0x00, 0x01};
    TEST_ASSERT_EQUAL_UINT(4u, run_pdu(ok, sizeof(ok), &rsp));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_FC_READ_HOLDING_REGS, rsp[0]);

    uint8_t over[5] = {MODBUS_FC_READ_HOLDING_REGS, (uint8_t)(last >> 8), (uint8_t)last, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT(2u, run_pdu(over, sizeof(over), &rsp));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, rsp[1]);
}

// Modbus Application Protocol V1.1b3 section 6.5: "A value of FF 00 hex requests the output to be
// ON. A value of 00 00 requests it to be OFF. All other values are illegal."
void test_write_single_coil_value_is_ff00_or_0000(void)
{
    const uint8_t *rsp = NULL;
    protocore_modbus_set_coil(5, PROTO_TRUE);

    static const uint8_t OFF[5] = {0x05, 0x00, 0x05, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT(5u, run_pdu(OFF, sizeof(OFF), &rsp));
    TEST_ASSERT_FALSE(protocore_modbus_get_coil(5));

    static const uint8_t ILLEGAL[5] = {0x05, 0x00, 0x05, 0x00, 0x01};
    TEST_ASSERT_EQUAL_UINT(2u, run_pdu(ILLEGAL, sizeof(ILLEGAL), &rsp));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_VALUE, rsp[1]);
    TEST_ASSERT_FALSE(protocore_modbus_get_coil(5)); // "will not affect the coil"
}

// Modbus Application Protocol V1.1b3 figures 21-22: the Byte Count must equal the quantity's own
// packing, N for coils and 2N for registers, else exception 03.
void test_write_multiple_byte_count_must_match_quantity(void)
{
    const uint8_t *rsp = NULL;

    static const uint8_t COIL_BC_WRONG[8] = {0x0F, 0x00, 0x00, 0x00, 0x0A, 0x03, 0xCD, 0x01};
    TEST_ASSERT_EQUAL_UINT(2u, run_pdu(COIL_BC_WRONG, sizeof(COIL_BC_WRONG), &rsp));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_VALUE, rsp[1]);

    static const uint8_t REG_BC_WRONG[10] = {0x10, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x0A, 0x01, 0x02};
    TEST_ASSERT_EQUAL_UINT(2u, run_pdu(REG_BC_WRONG, sizeof(REG_BC_WRONG), &rsp));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_VALUE, rsp[1]);

    // and the declared byte count must actually be present in the PDU
    static const uint8_t REG_SHORT[8] = {0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_UINT(2u, run_pdu(REG_SHORT, sizeof(REG_SHORT), &rsp));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_VALUE, rsp[1]);
}

// Modbus Application Protocol V1.1b3 section 6.11 / 6.12: the quantity limits for the multiple
// writes are 07B0h coils (1968) and 007Bh registers (123).
void test_write_multiple_quantity_limits(void)
{
    const uint8_t *rsp = NULL;

    uint8_t coils[6 + 247] = {0x0F, 0x00, 0x00, 0x07, 0xB1, 0xF7};
    TEST_ASSERT_EQUAL_UINT(2u, run_pdu(coils, sizeof(coils), &rsp)); // 1969 coils
    TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_VALUE, rsp[1]);

    uint8_t regs[6 + 248] = {0x10, 0x00, 0x00, 0x00, 0x7C, 0xF8};
    TEST_ASSERT_EQUAL_UINT(2u, run_pdu(regs, sizeof(regs), &rsp)); // 124 registers
    TEST_ASSERT_EQUAL_HEX8(MODBUS_EX_ILLEGAL_DATA_VALUE, rsp[1]);
}

// Modbus Application Protocol V1.1b3 section 6.16: Mask Write Register computes
//   Result = (Current AND And_Mask) OR (Or_Mask AND (NOT And_Mask))
// and the spec's own worked line is Current 12h, And F2h, Or 25h giving 17h. The response echoes
// the request.
void test_mask_write_register(void)
{
    const uint8_t *rsp = NULL;
    protocore_modbus_set_holding_reg(4, 0x0012);
    static const uint8_t REQ[7] = {0x16, 0x00, 0x04, 0x00, 0xF2, 0x00, 0x25};
    TEST_ASSERT_EQUAL_UINT(sizeof(REQ), run_pdu(REQ, sizeof(REQ), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ, rsp, sizeof(REQ));
    TEST_ASSERT_EQUAL_HEX16(0x0017, protocore_modbus_get_holding_reg(4));
}

// Modbus Application Protocol V1.1b3 section 6.17: the published example reads six registers
// starting at register 4 (address 0003h) and writes three starting at register 15 (address 000Eh).
// The spec also states "The write operation is performed before the read", so a second case reads
// back through the span it just wrote.
void test_read_write_multiple_registers(void)
{
    const uint8_t *rsp = NULL;
    static const uint16_t READ_BACK[6] = {0x00FE, 0x0ACD, 0x0001, 0x0003, 0x000D, 0x00FF};
    for (uint16_t i = 0; i < 6; i++)
    {
        protocore_modbus_set_holding_reg((uint16_t)(0x0003 + i), READ_BACK[i]);
    }
    static const uint8_t REQ[16] = {0x17, 0x00, 0x03, 0x00, 0x06, 0x00, 0x0E, 0x00,
                                    0x03, 0x06, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
    static const uint8_t RSP[14] = {0x17, 0x0C, 0x00, 0xFE, 0x0A, 0xCD, 0x00, 0x01, 0x00, 0x03, 0x00, 0x0D, 0x00, 0xFF};
    TEST_ASSERT_EQUAL_UINT(sizeof(RSP), run_pdu(REQ, sizeof(REQ), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP, rsp, sizeof(RSP));
    for (uint16_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_HEX16(0x00FF, protocore_modbus_get_holding_reg((uint16_t)(0x000E + i)));
    }

    // an overlapping transaction reads what the write in the same PDU just placed there
    protocore_modbus_set_holding_reg(20, 0x1111);
    protocore_modbus_set_holding_reg(21, 0x2222);
    static const uint8_t OVERLAP[14] = {0x17, 0x00, 0x14, 0x00, 0x02, 0x00, 0x14,
                                        0x00, 0x02, 0x04, 0x00, 0xAA, 0x00, 0xBB};
    static const uint8_t WANT[6] = {0x17, 0x04, 0x00, 0xAA, 0x00, 0xBB};
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), run_pdu(OVERLAP, sizeof(OVERLAP), &rsp));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, rsp, sizeof(WANT));
}

// Modbus Messaging on TCP/IP V1.0b section 3.1.3: "The MODBUS protocol is identified by the value
// 0", and the Length field is a byte count of the following fields. A frame that breaks either is
// not a Modbus frame, so nothing is sent.
void test_mbap_header_validation(void)
{
    // Read Holding Registers, address 0, one register: MBAP Length 0006h = unit id + a 5-octet PDU.
    uint8_t adu[16] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t out[MODBUS_ADU_MAX];
    TEST_ASSERT_EQUAL_UINT(11u, protocore_modbus_process_adu(adu, 12, out, sizeof(out))); // MBAP + 4

    uint8_t bad[16];
    memcpy(bad, adu, sizeof(adu));
    bad[3] = 0x01; // Protocol Identifier is not 0
    TEST_ASSERT_EQUAL_UINT(0u, protocore_modbus_process_adu(bad, 12, out, sizeof(out)));

    memcpy(bad, adu, sizeof(adu));
    bad[5] = 0x20; // Length claims more octets than the frame carries
    TEST_ASSERT_EQUAL_UINT(0u, protocore_modbus_process_adu(bad, 12, out, sizeof(out)));

    memcpy(bad, adu, sizeof(adu));
    bad[5] = 0x01; // Length below the unit id + one function code octet
    TEST_ASSERT_EQUAL_UINT(0u, protocore_modbus_process_adu(bad, 12, out, sizeof(out)));

    // a frame shorter than MBAP + a function code is not addressable at all
    for (size_t n = 0; n < 8; n++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_modbus_process_adu(adu, n, out, sizeof(out)));
    }
}

// The write callback reports the function code, the first address and the count written, for each
// of the four write function codes.
static uint8_t g_cb_fc;
static uint16_t g_cb_start;
static uint16_t g_cb_count;
static int g_cb_hits;

static void write_cb(uint8_t fc, uint16_t start, uint16_t count)
{
    g_cb_fc = fc;
    g_cb_start = start;
    g_cb_count = count;
    g_cb_hits++;
}

void test_write_callback_reports_each_write(void)
{
    protocore_modbus_on_write(write_cb);
    g_cb_hits = 0;

    static const uint8_t COIL[5] = {0x05, 0x00, 0x09, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_UINT(5u, run_pdu(COIL, sizeof(COIL), NULL));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_FC_WRITE_SINGLE_COIL, g_cb_fc);
    TEST_ASSERT_EQUAL_UINT16(9, g_cb_start);
    TEST_ASSERT_EQUAL_UINT16(1, g_cb_count);

    static const uint8_t REGS[10] = {0x10, 0x00, 0x02, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT(5u, run_pdu(REGS, sizeof(REGS), NULL));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_FC_WRITE_MULTIPLE_REGS, g_cb_fc);
    TEST_ASSERT_EQUAL_UINT16(2, g_cb_start);
    TEST_ASSERT_EQUAL_UINT16(2, g_cb_count);
    TEST_ASSERT_EQUAL_INT(2, g_cb_hits);

    // a read fires nothing, and a rejected write fires nothing
    static const uint8_t READ[5] = {0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_UINT(4u, run_pdu(READ, sizeof(READ), NULL));
    static const uint8_t REJECT[5] = {0x05, 0x00, 0x09, 0x00, 0x01};
    TEST_ASSERT_EQUAL_UINT(2u, run_pdu(REJECT, sizeof(REJECT), NULL));
    TEST_ASSERT_EQUAL_INT(2, g_cb_hits);

    protocore_modbus_on_write(NULL);
}

// The data model refuses out-of-range addresses instead of writing past its tables, and
// protocore_modbus_server_init() zeroes every one of them.
void test_data_model_bounds_and_reset(void)
{
    protocore_modbus_set_coil(PROTOCORE_MODBUS_COILS, PROTO_TRUE);
    protocore_modbus_set_discrete_input(PROTOCORE_MODBUS_DISCRETE_INPUTS, PROTO_TRUE);
    protocore_modbus_set_holding_reg(PROTOCORE_MODBUS_HOLDING_REGS, 0xBEEF);
    protocore_modbus_set_input_reg(PROTOCORE_MODBUS_INPUT_REGS, 0xBEEF);
    TEST_ASSERT_FALSE(protocore_modbus_get_coil(PROTOCORE_MODBUS_COILS));
    TEST_ASSERT_FALSE(protocore_modbus_get_discrete_input(PROTOCORE_MODBUS_DISCRETE_INPUTS));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_modbus_get_holding_reg(PROTOCORE_MODBUS_HOLDING_REGS));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_modbus_get_input_reg(PROTOCORE_MODBUS_INPUT_REGS));

    protocore_modbus_set_coil(1, PROTO_TRUE);
    protocore_modbus_set_holding_reg(1, 0x1234);
    protocore_modbus_server_init();
    TEST_ASSERT_FALSE(protocore_modbus_get_coil(1));
    TEST_ASSERT_EQUAL_HEX16(0, protocore_modbus_get_holding_reg(1));
}

// MODBUS over Serial Line V1.02 section 2.5.1: the RTU ADU is
//   [Address][PDU][CRC lo][CRC hi]
// with the CRC-16 (init FFFFh, reflected polynomial A001h) over the address and the PDU, sent low
// byte first. Feeding the server a frame it built itself must validate, and the address round trip
// plus the low-byte-first order are what an RS-485 peer sees.
#if PROTOCORE_ENABLE_MODBUS_RTU
static uint16_t rtu_crc(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

void test_rtu_frame_round_trip(void)
{
    protocore_modbus_set_holding_reg(0, 0x1234);

    uint8_t req[8] = {0x11, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc = rtu_crc(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t resp[MODBUS_ADU_MAX];
    size_t n = protocore_modbus_rtu_process_adu(req, sizeof(req), resp, sizeof(resp), 0x11);
    TEST_ASSERT_EQUAL_UINT(7u, n); // addr + fc + byte count + 2 data + 2 CRC
    TEST_ASSERT_EQUAL_HEX8(0x11, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[3]);
    TEST_ASSERT_EQUAL_HEX8(0x34, resp[4]);
    uint16_t rcrc = rtu_crc(resp, n - 2);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(rcrc & 0xFF), resp[n - 2]); // low byte first on the wire
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(rcrc >> 8), resp[n - 1]);
}

// MODBUS over Serial Line V1.02 section 2.4.1: a frame failing the CRC is discarded with no reply,
// a frame addressed to another slave is ignored, and a broadcast (address 0) is executed but draws
// no response.
void test_rtu_crc_address_and_broadcast(void)
{
    uint8_t resp[MODBUS_ADU_MAX];

    uint8_t bad[8] = {0x11, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc = rtu_crc(bad, 6);
    bad[6] = (uint8_t)((crc & 0xFF) ^ 0x01); // one bit wrong
    bad[7] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_modbus_rtu_process_adu(bad, sizeof(bad), resp, sizeof(resp), 0x11));

    uint8_t other[8] = {0x12, 0x03, 0x00, 0x00, 0x00, 0x01};
    crc = rtu_crc(other, 6);
    other[6] = (uint8_t)(crc & 0xFF);
    other[7] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_modbus_rtu_process_adu(other, sizeof(other), resp, sizeof(resp), 0x11));

    uint8_t bcast[8] = {0x00, 0x06, 0x00, 0x03, 0xAB, 0xCD};
    crc = rtu_crc(bcast, 6);
    bcast[6] = (uint8_t)(crc & 0xFF);
    bcast[7] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_modbus_rtu_process_adu(bcast, sizeof(bcast), resp, sizeof(resp), 0x11));
    TEST_ASSERT_EQUAL_HEX16(0xABCD, protocore_modbus_get_holding_reg(3)); // executed all the same
}
#endif // PROTOCORE_ENABLE_MODBUS_RTU
