// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the GPIB-over-LAN controller codec (services/instrumentation/gpib/gpib.h).
//
// The load-bearing case is test_prologix_published_escape_example. The Prologix GPIB-ETHERNET
// Controller User Manual, "Sending Binary Data", prints one worked vector: the decimal payload
//     00 01 02 13 03 10 04 27 05 43 06
// "must be escaped as follows"
//     00 01 02 27 13 03 27 10 04 27 27 05 27 43 06
// which names all four octets that take a leading ESC (CR 13, LF 10, ESC 27, '+' 43) and shows
// that the escape is inserted before, never substituted for, the octet. A codec that misses one of
// the four sends a payload the adapter reads as a line terminator or as a command prefix, and the
// instrument receives a truncated program message.
//
// Every command spelling below is the SYNTAX line of the corresponding manual section, and the
// address ranges (PAD 0-30, SAD 96-126) are the manual's stated ranges for ++addr and ++spoll.

#include "services/instrumentation/gpib/gpib.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The manual's "Sending Binary Data" vector, octet for octet, plus the unescaped '\n' the codec
// appends as the network line terminator.
void test_prologix_published_escape_example(void)
{
    static const uint8_t SRC[] = {0, 1, 2, 13, 3, 10, 4, 27, 5, 43, 6};
    static const uint8_t WANT[] = {0, 1, 2, 27, 13, 3, 27, 10, 4, 27, 27, 5, 27, 43, 6, '\n'};
    uint8_t buf[32];
    size_t n = protocore_gpib_build_data(buf, sizeof(buf), SRC, sizeof(SRC));
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// Exactly the four octets the manual names are escaped and nothing else: every other value in
// 0..255 passes through as one octet.
void test_only_the_four_named_octets_are_escaped(void)
{
    for (int v = 0; v <= 255; v++)
    {
        uint8_t src = (uint8_t)v;
        uint8_t buf[8];
        size_t n = protocore_gpib_build_data(buf, sizeof(buf), &src, 1);
        proto_bool named = (v == 13 || v == 10 || v == 27 || v == 43);
        TEST_ASSERT_EQUAL_UINT(named ? 3u : 2u, n);
        if (named)
        {
            TEST_ASSERT_EQUAL_HEX8(27, buf[0]);
            TEST_ASSERT_EQUAL_HEX8(src, buf[1]);
        }
        else
        {
            TEST_ASSERT_EQUAL_HEX8(src, buf[0]);
        }
        TEST_ASSERT_EQUAL_HEX8('\n', buf[n - 1]); // the terminator is never escaped
    }
}

// An empty payload still emits the line terminator: the adapter needs a line to act on.
void test_empty_payload_is_a_bare_terminator(void)
{
    uint8_t buf[4];
    TEST_ASSERT_EQUAL_UINT(1u, protocore_gpib_build_data(buf, sizeof(buf), NULL, 0));
    TEST_ASSERT_EQUAL_HEX8('\n', buf[0]);
}

// The manual: "SYNTAX: ++addr [<PAD> [<SAD>]] ... PAD (Primary Address) is a decimal value between
// 0 and 30. SAD (Secondary Address) is a decimal value between 96 and 126."
// EXAMPLES: "++addr 5" and "++addr 9 96".
void test_addr_command_matches_the_manual(void)
{
    char buf[32];
    TEST_ASSERT_EQUAL_UINT(9u, protocore_gpib_addr(buf, sizeof(buf), 5, -1));
    TEST_ASSERT_EQUAL_STRING("++addr 5\n", buf);

    TEST_ASSERT_EQUAL_UINT(12u, protocore_gpib_addr(buf, sizeof(buf), 9, 96));
    TEST_ASSERT_EQUAL_STRING("++addr 9 96\n", buf);

    // the ends of the primary range are addressable, one past it is not
    TEST_ASSERT_TRUE(protocore_gpib_addr(buf, sizeof(buf), 0, -1) > 0);
    TEST_ASSERT_EQUAL_STRING("++addr 0\n", buf);
    TEST_ASSERT_TRUE(protocore_gpib_addr(buf, sizeof(buf), 30, -1) > 0);
    TEST_ASSERT_EQUAL_STRING("++addr 30\n", buf);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_addr(buf, sizeof(buf), 31, -1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_addr(buf, sizeof(buf), 255, -1));
}

// The manual: "SYNTAX: ++read [eoi|<char>] where <char> is a decimal value less than 256".
// EXAMPLES: "++read", "++read eoi", "++read 10" (read until LF).
void test_read_command_matches_the_manual(void)
{
    char buf[32];
    TEST_ASSERT_EQUAL_UINT(7u, protocore_gpib_read(buf, sizeof(buf), UNTIL_TIMEOUT, 0));
    TEST_ASSERT_EQUAL_STRING("++read\n", buf);

    TEST_ASSERT_EQUAL_UINT(11u, protocore_gpib_read(buf, sizeof(buf), UNTIL_EOI, 0));
    TEST_ASSERT_EQUAL_STRING("++read eoi\n", buf);

    TEST_ASSERT_EQUAL_UINT(10u, protocore_gpib_read(buf, sizeof(buf), UNTIL_CHAR, 10));
    TEST_ASSERT_EQUAL_STRING("++read 10\n", buf);

    // the <char> field is decimal, so 255 renders as three digits and not as an octet
    TEST_ASSERT_TRUE(protocore_gpib_read(buf, sizeof(buf), UNTIL_CHAR, 255) > 0);
    TEST_ASSERT_EQUAL_STRING("++read 255\n", buf);
}

// The manual: "SYNTAX: ++spoll [<PAD> [<SAD>]]".
// EXAMPLES: "++spoll 5", "++spoll 9 96", and a bare "++spoll" for the currently addressed one.
void test_spoll_command_matches_the_manual(void)
{
    char buf[32];
    TEST_ASSERT_EQUAL_UINT(8u, protocore_gpib_spoll(buf, sizeof(buf), -1, -1));
    TEST_ASSERT_EQUAL_STRING("++spoll\n", buf);

    TEST_ASSERT_EQUAL_UINT(10u, protocore_gpib_spoll(buf, sizeof(buf), 5, -1));
    TEST_ASSERT_EQUAL_STRING("++spoll 5\n", buf);

    TEST_ASSERT_EQUAL_UINT(13u, protocore_gpib_spoll(buf, sizeof(buf), 9, 96));
    TEST_ASSERT_EQUAL_STRING("++spoll 9 96\n", buf);

    // a secondary without a primary is not a form the syntax allows, so it is dropped
    TEST_ASSERT_TRUE(protocore_gpib_spoll(buf, sizeof(buf), -1, 96) > 0);
    TEST_ASSERT_EQUAL_STRING("++spoll\n", buf);
}

// The manual: "SYNTAX: ++eos [0|1|2|3] where: 0 - CR+LF, 1 - CR, 2 - LF, 3 - None", with the
// enumerator's decimal value going on the wire.
void test_eos_command_matches_the_manual(void)
{
    char buf[16];
    TEST_ASSERT_EQUAL_INT(0, GPIB_EOS_CRLF);
    TEST_ASSERT_EQUAL_INT(1, GPIB_EOS_CR);
    TEST_ASSERT_EQUAL_INT(2, GPIB_EOS_LF);
    TEST_ASSERT_EQUAL_INT(3, GPIB_EOS_PROTOCORE_NONE);

    TEST_ASSERT_EQUAL_UINT(8u, protocore_gpib_eos(buf, sizeof(buf), GPIB_EOS_CRLF));
    TEST_ASSERT_EQUAL_STRING("++eos 0\n", buf);
    TEST_ASSERT_TRUE(protocore_gpib_eos(buf, sizeof(buf), GPIB_EOS_CR) > 0);
    TEST_ASSERT_EQUAL_STRING("++eos 1\n", buf);
    TEST_ASSERT_TRUE(protocore_gpib_eos(buf, sizeof(buf), GPIB_EOS_LF) > 0);
    TEST_ASSERT_EQUAL_STRING("++eos 2\n", buf);
    TEST_ASSERT_TRUE(protocore_gpib_eos(buf, sizeof(buf), GPIB_EOS_PROTOCORE_NONE) > 0);
    TEST_ASSERT_EQUAL_STRING("++eos 3\n", buf);
}

// A generic command is "++" + the text + the line terminator, which is how every remaining section
// of the manual is spelled (++mode, ++eoi, ++clr, ++ver, ++read_tmo_ms ...).
void test_generic_command_form(void)
{
    static const char *const CMDS[] = {"mode 1", "eoi 1", "clr", "trg", "ver", "read_tmo_ms 500", "auto 0"};
    for (size_t i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++)
    {
        char buf[64];
        char want[64];
        size_t n = protocore_gpib_command(buf, sizeof(buf), CMDS[i]);
        memcpy(want, "++", 2);
        memcpy(want + 2, CMDS[i], strlen(CMDS[i]));
        want[2 + strlen(CMDS[i])] = '\n';
        want[3 + strlen(CMDS[i])] = '\0';
        TEST_ASSERT_EQUAL_STRING(want, buf);
        TEST_ASSERT_EQUAL_UINT(strlen(CMDS[i]) + 3u, n);
    }
}

// The manual: "Any ETHERNET input that starts with the unescaped '++' character sequence" is a
// controller command; anything else is data for the instrument. One '+' is data, and so is a line
// whose '+' is escaped.
void test_command_versus_data_classification(void)
{
    TEST_ASSERT_TRUE(protocore_gpib_is_command("++ver\n", 6));
    TEST_ASSERT_TRUE(protocore_gpib_is_command("++", 2));
    TEST_ASSERT_FALSE(protocore_gpib_is_command("+", 1));
    TEST_ASSERT_FALSE(protocore_gpib_is_command("+x+", 3));
    TEST_ASSERT_FALSE(protocore_gpib_is_command("*IDN?\n", 6));
    TEST_ASSERT_FALSE(protocore_gpib_is_command("", 0));
    TEST_ASSERT_FALSE(protocore_gpib_is_command(NULL, 2));

    // a '+' the codec escaped is preceded by ESC, so the line no longer starts with "++"
    static const uint8_t PLUS[] = {'+', '+'};
    uint8_t line[8];
    size_t n = protocore_gpib_build_data(line, sizeof(line), PLUS, sizeof(PLUS));
    TEST_ASSERT_FALSE(protocore_gpib_is_command((const char *)line, n));
}

// The ++spoll response is the status byte as a decimal string, and the ++srq response is 0 or 1.
// Surrounding spaces and the line terminator are trimmed off first.
void test_decimal_response_parsing(void)
{
    uint32_t v = 0;
    TEST_ASSERT_TRUE(protocore_gpib_parse_decimal("0\r\n", 3, &v));
    TEST_ASSERT_EQUAL_UINT32(0u, v);
    TEST_ASSERT_TRUE(protocore_gpib_parse_decimal("1\n", 2, &v));
    TEST_ASSERT_EQUAL_UINT32(1u, v);
    TEST_ASSERT_TRUE(protocore_gpib_parse_decimal("  64  \r\n", 8, &v));
    TEST_ASSERT_EQUAL_UINT32(64u, v); // RQS, bit 6 of the status byte
    TEST_ASSERT_TRUE(protocore_gpib_parse_decimal("255", 3, &v));
    TEST_ASSERT_EQUAL_UINT32(255u, v);

    static const char *const BAD[] = {"", "   ", "\r\n", "12a", "a12", "1 2", "-1", "+1", "0x40"};
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(protocore_gpib_parse_decimal(BAD[i], strlen(BAD[i]), &v), BAD[i]);
    }
    TEST_ASSERT_FALSE(protocore_gpib_parse_decimal(NULL, 3, &v));
}

// A queried "++addr" answers with the primary address, or with the primary and secondary the
// manual's ranges allow. Anything outside those ranges is not a valid response.
void test_addr_response_parsing(void)
{
    uint8_t pad = 0xFF;
    int sad = 0;

    TEST_ASSERT_TRUE(protocore_gpib_parse_addr("5\r\n", 3, &pad, &sad));
    TEST_ASSERT_EQUAL_UINT8(5, pad);
    TEST_ASSERT_EQUAL_INT(-1, sad); // no secondary present

    TEST_ASSERT_TRUE(protocore_gpib_parse_addr("9 96\r\n", 6, &pad, &sad));
    TEST_ASSERT_EQUAL_UINT8(9, pad);
    TEST_ASSERT_EQUAL_INT(96, sad);

    TEST_ASSERT_TRUE(protocore_gpib_parse_addr("30 126\n", 7, &pad, &sad));
    TEST_ASSERT_EQUAL_UINT8(30, pad);
    TEST_ASSERT_EQUAL_INT(126, sad);

    static const char *const BAD[] = {
        "31\r\n",     // primary past 30
        "5 95\r\n",   // secondary below 96
        "5 127\r\n",  // secondary past 126
        "5 x\r\n",    // secondary not decimal
        "x\r\n", "", "  \r\n", "5 96 97\r\n",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(protocore_gpib_parse_addr(BAD[i], strlen(BAD[i]), &pad, &sad), BAD[i]);
    }
    TEST_ASSERT_FALSE(protocore_gpib_parse_addr(NULL, 3, &pad, &sad));
}

// The manual: "++ver ... returns the version string of the Prologix GPIB-ETHERNET controller."
// The token after "version " is located inside the response, with the terminator trimmed.
void test_version_response_parsing(void)
{
    static const char RESP[] = "Prologix GPIB-ETHERNET Controller version 1.6.6\r\n";
    const char *ver = NULL;
    size_t vlen = 0;
    TEST_ASSERT_TRUE(protocore_gpib_parse_version(RESP, sizeof(RESP) - 1, &ver, &vlen));
    TEST_ASSERT_EQUAL_UINT(5u, vlen);
    TEST_ASSERT_EQUAL_MEMORY("1.6.6", ver, 5);
    TEST_ASSERT_TRUE(ver >= RESP && ver < RESP + sizeof(RESP)); // points into the caller's buffer

    static const char *const BAD[] = {
        "Prologix GPIB-USB Controller\r\n", // no "version " key
        "version ",                         // key present, token empty
        "version \r\n",
        "vers",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(protocore_gpib_parse_version(BAD[i], strlen(BAD[i]), &ver, &vlen), BAD[i]);
    }
    TEST_ASSERT_FALSE(protocore_gpib_parse_version(NULL, 8, &ver, &vlen));
}

// The manual gives the raw-socket TCP port and the NetFinder discovery UDP port.
void test_published_ports(void)
{
    TEST_ASSERT_EQUAL_INT(1234, PROTOCORE_GPIB_PORT);
    TEST_ASSERT_EQUAL_INT(3040, PROTOCORE_GPIB_DISCOVERY_PORT);
}

// Every builder refuses a buffer that cannot hold the whole line plus its NUL, rather than emitting
// a truncated command the adapter would act on.
void test_builders_refuse_a_short_buffer(void)
{
    char buf[32];
    TEST_ASSERT_EQUAL_UINT(6u, protocore_gpib_command(buf, 7, "ver"));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_command(buf, 6, "ver"));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_command(buf, 0, "ver"));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_command(NULL, 32, "ver"));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_command(buf, 32, NULL));

    TEST_ASSERT_EQUAL_UINT(9u, protocore_gpib_addr(buf, 10, 5, -1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_addr(buf, 9, 5, -1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_addr(NULL, 32, 5, -1));

    TEST_ASSERT_EQUAL_UINT(7u, protocore_gpib_read(buf, 8, UNTIL_TIMEOUT, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_read(buf, 7, UNTIL_TIMEOUT, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_read(NULL, 32, UNTIL_EOI, 0));

    TEST_ASSERT_EQUAL_UINT(8u, protocore_gpib_spoll(buf, 9, -1, -1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_spoll(buf, 8, -1, -1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_spoll(NULL, 32, -1, -1));

    TEST_ASSERT_EQUAL_UINT(8u, protocore_gpib_eos(buf, 9, GPIB_EOS_CRLF));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_eos(buf, 8, GPIB_EOS_CRLF));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_eos(NULL, 32, GPIB_EOS_CRLF));
}

// The data builder reserves room for the terminator, so a payload that would exactly fill the
// buffer is refused rather than sent without its line ending.
void test_data_builder_reserves_the_terminator(void)
{
    static const uint8_t SRC[] = {'A', 'B', 'C'};
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_UINT(4u, protocore_gpib_build_data(buf, 4, SRC, sizeof(SRC)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_build_data(buf, 3, SRC, sizeof(SRC)));

    // an escaped octet needs two, so the same payload length needs more room
    static const uint8_t ESCAPED[] = {'A', 13, 'C'};
    TEST_ASSERT_EQUAL_UINT(5u, protocore_gpib_build_data(buf, 5, ESCAPED, sizeof(ESCAPED)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_build_data(buf, 4, ESCAPED, sizeof(ESCAPED)));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_build_data(NULL, 8, SRC, sizeof(SRC)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_build_data(buf, 0, SRC, sizeof(SRC)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_gpib_build_data(buf, 8, NULL, 3));
}
