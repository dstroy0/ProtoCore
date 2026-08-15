// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Omron FINS frame codec (services/fieldbus/fins/fins.h).
//
// The governing text is Omron's FINS command reference (W342, section 5): a 10-octet routing header
// ICF RSV GCT DNA DA1 DA2 SNA SA1 SA2 SID, then MRC/SRC, then big-endian command parameters.
// test_memory_area_read_octets is the load-bearing case: MEMORY AREA READ (command code 0101) is
// the message every FINS master sends, and its parameter block is published as memory-area code(1)
// + beginning address(3, a 2-octet word address then a bit) + number of items(2, big-endian). Each
// expected octet below is placed from that table, and the address arithmetic is shown in-line.

#include "services/fieldbus/fins/fins.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define FINS_AREA_DM_WORD 0x82u ///< DM Area, word access - the memory-area code from W342 section 5-2

// A master addressing node 1 unit 0 on the local network from node 2, service id 0.
static FinsHeader master_header(void)
{
    FinsHeader h;
    memset(&h, 0, sizeof(h));
    h.icf = FINS_ICF_COMMAND;
    h.rsv = 0x00;
    h.gct = 0x02;
    h.dna = 0x00;
    h.da1 = 0x01;
    h.da2 = 0x00;
    h.sna = 0x00;
    h.sa1 = 0x02;
    h.sa2 = 0x00;
    h.sid = 0x00;
    return h;
}

// The header is exactly ten octets, the ICF bit assignments are published, and the gateway count
// octet is fixed at 0x02.
void test_published_header_constants(void)
{
    TEST_ASSERT_EQUAL_size_t(10u, (size_t)FINS_HEADER_SIZE);
    // ICF bit 6 selects command(0) / response(1); bit 7 requests gateway use.
    TEST_ASSERT_EQUAL_HEX8(0x80u, FINS_ICF_COMMAND);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, FINS_ICF_RESPONSE);
    TEST_ASSERT_EQUAL_HEX8(0x40u, (uint8_t)(FINS_ICF_RESPONSE ^ FINS_ICF_COMMAND));
    // ICF bit 0 set means no response is required.
    TEST_ASSERT_EQUAL_HEX8(0x01u, FINS_ICF_NO_RESPONSE);

    TEST_ASSERT_EQUAL_HEX8(0x01u, FINS_MRC_MEMORY_AREA);
    TEST_ASSERT_EQUAL_HEX8(0x01u, FINS_SRC_MEMORY_AREA_READ);
    TEST_ASSERT_EQUAL_HEX8(0x02u, FINS_SRC_MEMORY_AREA_WRITE);
    TEST_ASSERT_EQUAL_HEX8(0x04u, FINS_MRC_OPERATING_MODE);
    TEST_ASSERT_EQUAL_HEX8(0x01u, FINS_SRC_RUN);
    TEST_ASSERT_EQUAL_HEX8(0x02u, FINS_SRC_STOP);
    TEST_ASSERT_EQUAL_HEX8(0x02u, (uint8_t)FINS_RUN_MODE_MONITOR);
    TEST_ASSERT_EQUAL_HEX8(0x04u, (uint8_t)FINS_RUN_MODE_RUN);
}

// MEMORY AREA READ of 10 words from D100:
//   header  80 00 02 00 01 00 00 02 00 00
//   command 01 01                                       (MRC/SRC = 0101)
//   params  82        memory-area code, DM word
//           00 64     beginning word address, big-endian: D100 -> 100 -> 0x0064
//           00        bit number, 0 for a word read
//           00 0A     number of items, big-endian: 10 -> 0x000A
void test_memory_area_read_octets(void)
{
    static const uint8_t WANT[18] = {
        0x80, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, // routing header
        0x01, 0x01,                                                 // MRC, SRC
        0x82, 0x00, 0x64, 0x00, 0x00, 0x0A,                         // area, address, bit, count
    };
    FinsHeader h = master_header();
    uint8_t buf[32];
    size_t n = protocore_fins_build_memory_area_read(buf, sizeof(buf), &h, FINS_AREA_DM_WORD, 100, 0, 10);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// MEMORY AREA WRITE (0102) repeats the read's six parameter octets and appends the write data, two
// big-endian octets per word item. Writing 0x1234 and 0x5678 to D100 is a count of 2 and 4 octets.
void test_memory_area_write_octets(void)
{
    static const uint8_t DATA[4] = {0x12, 0x34, 0x56, 0x78};
    static const uint8_t WANT[22] = {
        0x80, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, //
        0x01, 0x02,                                                 // MRC/SRC = 0102
        0x82, 0x00, 0x64, 0x00, 0x00, 0x02,                         // DM word, D100, bit 0, 2 items
        0x12, 0x34, 0x56, 0x78,                                     // the two words
    };
    FinsHeader h = master_header();
    uint8_t buf[32];
    size_t n =
        protocore_fins_build_memory_area_write(buf, sizeof(buf), &h, FINS_AREA_DM_WORD, 100, 0, 2, DATA, sizeof(DATA));
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// RUN (0401) carries the program number 0xFFFF (all programs) then the one-octet mode code; STOP
// (0402) carries no parameters at all, so it is header + MRC + SRC = 12 octets.
void test_operating_mode_commands(void)
{
    FinsHeader h = master_header();
    uint8_t buf[32];

    TEST_ASSERT_EQUAL_size_t(15u, protocore_fins_build_run(buf, sizeof(buf), &h, FINS_RUN_MODE_RUN));
    static const uint8_t RUN[5] = {0x04, 0x01, 0xFF, 0xFF, 0x04};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RUN, buf + FINS_HEADER_SIZE, sizeof(RUN));

    TEST_ASSERT_EQUAL_size_t(15u, protocore_fins_build_run(buf, sizeof(buf), &h, FINS_RUN_MODE_MONITOR));
    TEST_ASSERT_EQUAL_HEX8(0x02u, buf[14]);

    TEST_ASSERT_EQUAL_size_t(12u, protocore_fins_build_stop(buf, sizeof(buf), &h));
    static const uint8_t STOP[2] = {0x04, 0x02};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(STOP, buf + FINS_HEADER_SIZE, sizeof(STOP));
}

// Every routing octet survives build then parse, in its own position. A header whose fields are
// transposed still round-trips through a codec that swapped two of them, so each value here is
// distinct.
void test_header_round_trip(void)
{
    FinsHeader h;
    h.icf = 0x81;
    h.rsv = 0x00;
    h.gct = 0x02;
    h.dna = 0x03;
    h.da1 = 0x04;
    h.da2 = 0x05;
    h.sna = 0x06;
    h.sa1 = 0x07;
    h.sa2 = 0x08;
    h.sid = 0x09;

    static const uint8_t PARAMS[3] = {0xAA, 0xBB, 0xCC};
    uint8_t buf[32];
    size_t n = protocore_fins_build_command(buf, sizeof(buf), &h, 0x05, 0x01, PARAMS, sizeof(PARAMS));
    TEST_ASSERT_EQUAL_size_t(15u, n);

    FinsCommand c;
    memset(&c, 0, sizeof(c));
    TEST_ASSERT_TRUE(protocore_fins_parse_command(buf, n, &c));
    TEST_ASSERT_EQUAL_HEX8(h.icf, c.header.icf);
    TEST_ASSERT_EQUAL_HEX8(h.rsv, c.header.rsv);
    TEST_ASSERT_EQUAL_HEX8(h.gct, c.header.gct);
    TEST_ASSERT_EQUAL_HEX8(h.dna, c.header.dna);
    TEST_ASSERT_EQUAL_HEX8(h.da1, c.header.da1);
    TEST_ASSERT_EQUAL_HEX8(h.da2, c.header.da2);
    TEST_ASSERT_EQUAL_HEX8(h.sna, c.header.sna);
    TEST_ASSERT_EQUAL_HEX8(h.sa1, c.header.sa1);
    TEST_ASSERT_EQUAL_HEX8(h.sa2, c.header.sa2);
    TEST_ASSERT_EQUAL_HEX8(h.sid, c.header.sid);
    TEST_ASSERT_EQUAL_HEX8(0x05u, c.mrc);
    TEST_ASSERT_EQUAL_HEX8(0x01u, c.src);
    TEST_ASSERT_EQUAL_size_t(sizeof(PARAMS), c.params_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PARAMS, c.params, sizeof(PARAMS));
}

// A response inserts a two-octet end code between the echoed command code and the data. MRES = 0
// and SRES = 0 is the only normal completion; anything else is an error the caller must see
// verbatim rather than have folded into a single flag.
void test_response_end_code(void)
{
    static const uint8_t OK[16] = {
        0xC0, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, // response header, source/destination swapped
        0x01, 0x01,                                                 // echoed MRC/SRC
        0x00, 0x00,                                                 // MRES/SRES: normal completion
        0x12, 0x34,                                                 // one DM word
    };
    FinsResponse r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(protocore_fins_parse_response(OK, sizeof(OK), &r));
    TEST_ASSERT_EQUAL_HEX8(FINS_ICF_RESPONSE, r.header.icf);
    TEST_ASSERT_EQUAL_HEX8(0x01u, r.mrc);
    TEST_ASSERT_EQUAL_HEX8(0x01u, r.src);
    TEST_ASSERT_EQUAL_HEX8(0x00u, r.mres);
    TEST_ASSERT_EQUAL_HEX8(0x00u, r.sres);
    TEST_ASSERT_EQUAL_size_t(2u, r.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x12u, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, r.data[1]);

    uint8_t err[14];
    memcpy(err, OK, sizeof(err));
    err[12] = 0x11;
    err[13] = 0x01; // a non-zero end code, surfaced as its two octets
    TEST_ASSERT_TRUE(protocore_fins_parse_response(err, sizeof(err), &r));
    TEST_ASSERT_EQUAL_HEX8(0x11u, r.mres);
    TEST_ASSERT_EQUAL_HEX8(0x01u, r.sres);
    TEST_ASSERT_EQUAL_size_t(0u, r.data_len);
}

// A response is at least header + MRC + SRC + MRES + SRES = 14 octets, and a command at least
// header + MRC + SRC = 12. One octet short of either is refused rather than read past.
void test_parsers_refuse_short_frames(void)
{
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));

    FinsCommand c;
    TEST_ASSERT_FALSE(protocore_fins_parse_command(buf, 11, &c));
    TEST_ASSERT_TRUE(protocore_fins_parse_command(buf, 12, &c));
    TEST_ASSERT_EQUAL_size_t(0u, c.params_len);
    TEST_ASSERT_FALSE(protocore_fins_parse_command(NULL, 12, &c));
    TEST_ASSERT_FALSE(protocore_fins_parse_command(buf, 12, NULL));

    FinsResponse r;
    TEST_ASSERT_FALSE(protocore_fins_parse_response(buf, 13, &r));
    TEST_ASSERT_TRUE(protocore_fins_parse_response(buf, 14, &r));
    TEST_ASSERT_EQUAL_size_t(0u, r.data_len);
    TEST_ASSERT_FALSE(protocore_fins_parse_response(NULL, 14, &r));
    TEST_ASSERT_FALSE(protocore_fins_parse_response(buf, 14, NULL));
}

// A buffer one octet short of the whole frame yields 0, never a truncated command.
void test_builders_refuse_a_short_buffer(void)
{
    FinsHeader h = master_header();
    uint8_t buf[32];
    static const uint8_t DATA[4] = {0x12, 0x34, 0x56, 0x78};

    TEST_ASSERT_EQUAL_size_t(0u, protocore_fins_build_memory_area_read(buf, 17, &h, FINS_AREA_DM_WORD, 100, 0, 10));
    TEST_ASSERT_EQUAL_size_t(18u, protocore_fins_build_memory_area_read(buf, 18, &h, FINS_AREA_DM_WORD, 100, 0, 10));

    // The prefix fits but the write data does not: still 0, not a headerless partial write.
    TEST_ASSERT_EQUAL_size_t(
        0u, protocore_fins_build_memory_area_write(buf, 21, &h, FINS_AREA_DM_WORD, 100, 0, 2, DATA, sizeof(DATA)));
    TEST_ASSERT_EQUAL_size_t(
        0u, protocore_fins_build_memory_area_write(buf, sizeof(buf), &h, FINS_AREA_DM_WORD, 100, 0, 2, NULL, 4));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_fins_build_stop(buf, 11, &h));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_fins_build_run(buf, 14, &h, FINS_RUN_MODE_RUN));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_fins_build_command(NULL, sizeof(buf), &h, 1, 1, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_fins_build_command(buf, sizeof(buf), NULL, 1, 1, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_fins_build_command(buf, sizeof(buf), &h, 1, 1, NULL, 3));
}
