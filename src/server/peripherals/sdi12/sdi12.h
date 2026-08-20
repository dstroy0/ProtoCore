// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sdi12.h
 * @brief SDI-12 sensor-bus command / response codec (PROTOCORE_ENABLE_SDI12).
 *
 * SDI-12 is the 1200-baud single-wire ASCII bus used by environmental / agricultural sensors
 * (soil moisture, water level, weather). A recorder addresses a sensor by a single character
 * (0-9, A-Z, a-z) and sends `<addr><command>!`; the sensor replies `<addr>...<CR><LF>`. This
 * codec builds the standard commands, parses the measurement response (`atttn`: seconds until
 * ready + value count), splits the data values, and does the SDI-12 CRC (the `aMC!` / `aCC!`
 * CRC-protected variants).
 *
 * The wire is a single 1200-baud 7E1 line with a 5 V break / marking convention; on an ESP32
 * it needs a small level / direction circuit, and the UART transport is the application's.
 * Pure codec, host-tested. Bridge a sensor string onto Wi-Fi by polling `aM!` / `aD0!` and
 * publishing the values.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SDI12_H
#define PROTOCORE_SDI12_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SDI12

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define SDI12_CRC_POLY 0xA001u ///< CRC-16 polynomial (reflected 0x8005), init 0x0000

#define SDI12_CRC_CHARS 3 ///< the CRC is appended as 3 printable ASCII octets

/** @brief A decoded identify (aI!) response. Each field is fixed-width per the SDI-12 spec and
 *  NUL-terminated (the spec pads short values with spaces, which are left in place). */
typedef struct
{
    char addr;              ///< sensor address
    char sdi_version[3];    ///< SDI-12 version "ll" (e.g. "14" = version 1.4) + NUL
    char vendor[9];         ///< 8-character vendor identification + NUL
    char model[7];          ///< 6-character sensor model number + NUL
    char sensor_version[4]; ///< 3-character sensor version + NUL
} Sdi12Identity;

/** @brief What build takes: buf, cap, addr, body. */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
    const char *body;
} Sdi12BuildArgs;

/** @brief What build_ack takes: buf, cap, addr. */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
} Sdi12BuildAckArgs;

/** @brief What build_identify takes: buf, cap, addr. */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
} Sdi12BuildIdentifyArgs;

/** @brief What build_measure takes: buf, cap, addr, with_crc. */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
    proto_bool with_crc;
} Sdi12BuildMeasureArgs;

/** @brief What build_concurrent takes: buf, cap, addr, with_crc. */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
    proto_bool with_crc;
} Sdi12BuildConcurrentArgs;

/** @brief What build_measure_additional takes: buf, cap, addr, ... */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
    uint8_t m_index;
    proto_bool with_crc;
} Sdi12BuildMeasureAdditionalArgs;

/** @brief What build_concurrent_additional takes: buf, cap, addr, ... */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
    uint8_t c_index;
    proto_bool with_crc;
} Sdi12BuildConcurrentAdditionalArgs;

/** @brief What build_continuous takes: buf, cap, addr, r_index, ... */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
    uint8_t r_index;
    proto_bool with_crc;
} Sdi12BuildContinuousArgs;

/** @brief What build_verify takes: buf, cap, addr. */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
} Sdi12BuildVerifyArgs;

/** @brief What build_data takes: buf, cap, addr, d_index. */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
    uint8_t d_index;
} Sdi12BuildDataArgs;

/** @brief What build_change_address takes: buf, cap, addr, new_addr. */
typedef struct
{
    char *buf;
    size_t cap;
    char addr;
    char new_addr;
} Sdi12BuildChangeAddressArgs;

/** @brief What build_query_address takes: buf, cap. */
typedef struct
{
    char *buf;
    size_t cap;
} Sdi12BuildQueryAddressArgs;

/** @brief What parse_measure takes: resp, len, addr, ready_sec, ... */
typedef struct
{
    const char *resp;
    size_t len;
    char *addr;
    uint16_t *ready_sec;
    uint8_t *num_values;
} Sdi12ParseMeasureArgs;

/** @brief What parse_values takes: resp, len, out, max, n. */
typedef struct
{
    const char *resp;
    size_t len;
    float *out;
    size_t max;
    size_t *n;
} Sdi12ParseValuesArgs;

/** @brief What parse_identify takes: resp, len, out. */
typedef struct
{
    const char *resp;
    size_t len;
    Sdi12Identity *out;
} Sdi12ParseIdentifyArgs;

/** @brief What crc16 takes: data, len. */
typedef struct
{
    const uint8_t *data;
    size_t len;
} Sdi12Crc16Args;

/** @brief What crc_encode takes: crc, out. */
typedef struct
{
    uint16_t crc;
    char *out; ///< SDI12_CRC_CHARS bytes.
} Sdi12CrcEncodeArgs;

/** @brief What check_crc takes: resp, len. */
typedef struct
{
    const char *resp;
    size_t len;
} Sdi12CheckCrcArgs;

/**
 * @brief SDI-12 sensor-bus command / response codec (PROTOCORE_ENABLE_SDI12). SDI-12 is the 1200-baud single-wire ...
 *
 * A caller sets the members a call takes, invokes it through ::Sdi12 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Sdi12.build_args.buf = ...;
 *   Sdi12.build_args.cap = ...;
 *   Sdi12.build_args.addr = ...;
 *   Sdi12.build_args.body = ...;
 *   Sdi12.build(work);
 *   // Sdi12.n is what the call reports
 *
 * @var Sdi12Ns::build_args  what build takes: buf, cap, addr, body
 * @var Sdi12Ns::build_ack_args  what build_ack takes: buf, cap, addr
 * @var Sdi12Ns::build_identify_args  what build_identify takes: buf, cap, addr
 * @var Sdi12Ns::build_measure_args  what build_measure takes: buf, cap, addr, with_crc
 * @var Sdi12Ns::build_concurrent_args  what build_concurrent takes: buf, cap, addr, with_crc
 * @var Sdi12Ns::build_measure_additional_args  what build_measure_additional takes: buf, cap, addr,
 * @var Sdi12Ns::build_concurrent_additional_args  what build_concurrent_additional takes: buf, cap, addr,
 * @var Sdi12Ns::build_continuous_args  what build_continuous takes: buf, cap, addr, r_index,
 * @var Sdi12Ns::build_verify_args  what build_verify takes: buf, cap, addr
 * @var Sdi12Ns::build_data_args  what build_data takes: buf, cap, addr, d_index
 * @var Sdi12Ns::build_change_address_args  what build_change_address takes: buf, cap, addr, new_addr
 * @var Sdi12Ns::build_query_address_args  what build_query_address takes: buf, cap
 * @var Sdi12Ns::parse_measure_args  what parse_measure takes: resp, len, addr, ready_sec,
 * @var Sdi12Ns::parse_values_args  what parse_values takes: resp, len, out, max, n
 * @var Sdi12Ns::parse_identify_args  what parse_identify takes: resp, len, out
 * @var Sdi12Ns::crc16_args  what crc16 takes: data, len
 * @var Sdi12Ns::crc_encode_args  what crc_encode takes: crc, out
 * @var Sdi12Ns::check_crc_args  what check_crc takes: resp, len
 * @var Sdi12Ns::ok  true iff len covers the 20 fixed octets; false otherwise
 * @var Sdi12Ns::n  the count a call reports
 * @var Sdi12Ns::crc  what a call reports
 * @var Sdi12Ns::build  generic `<addr><body>!` command (body is the command letters, e.g. ...
 * @var Sdi12Ns::build_ack  acknowledge-active command `a!`
 * @var Sdi12Ns::build_identify  send-identification command `aI!`
 * @var Sdi12Ns::build_measure  start-measurement command `aM!` (or `aMC!` when with_crc)
 * @var Sdi12Ns::build_concurrent  concurrent-measurement command `aC!` (or `aCC!` when with_crc)
 * @var Sdi12Ns::build_measure_additional  additional-measurement command `aM<n>!` (or `aMC<n>!` when ...
 * @var Sdi12Ns::build_concurrent_additional  additional-concurrent command `aC<n>!` (or `aCC<n>!` when ...
 * @var Sdi12Ns::build_continuous  continuous-measurement command `aR<n>!` (or `aRC<n>!` when ...
 * @var Sdi12Ns::build_verify  start-verification command `aV!`; the response uses the same ...
 * @var Sdi12Ns::build_data  send-data command `aD<n>!` (d_index 0..9)
 * @var Sdi12Ns::build_change_address  change-address command `aA<b>!` (new_addr is the new sensor address)
 * @var Sdi12Ns::build_query_address  address-query command `?!` (asks the single connected sensor for ...
 * @var Sdi12Ns::parse_measure  parse a measurement response `atttn<CR><LF>`: ready_sec = seconds ...
 * @var Sdi12Ns::parse_values  split a data response `a<+/-value...><CR><LF>` into floats. Skips ...
 * @var Sdi12Ns::parse_identify  parse an identify (aI!) response: address + 2-char SDI-12 version + ...
 * @var Sdi12Ns::crc16  compute the SDI-12 CRC-16 over data
 * @var Sdi12Ns::crc_encode  encode a CRC into its 3 printable ASCII octets (out[0..2])
 * @var Sdi12Ns::check_crc  verify a CRC-protected response: the 3 octets before the trailing ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Sdi12BuildArgs build_args;
    Sdi12BuildAckArgs build_ack_args;
    Sdi12BuildIdentifyArgs build_identify_args;
    Sdi12BuildMeasureArgs build_measure_args;
    Sdi12BuildConcurrentArgs build_concurrent_args;
    Sdi12BuildMeasureAdditionalArgs build_measure_additional_args;
    Sdi12BuildConcurrentAdditionalArgs build_concurrent_additional_args;
    Sdi12BuildContinuousArgs build_continuous_args;
    Sdi12BuildVerifyArgs build_verify_args;
    Sdi12BuildDataArgs build_data_args;
    Sdi12BuildChangeAddressArgs build_change_address_args;
    Sdi12BuildQueryAddressArgs build_query_address_args;
    Sdi12ParseMeasureArgs parse_measure_args;
    Sdi12ParseValuesArgs parse_values_args;
    Sdi12ParseIdentifyArgs parse_identify_args;
    Sdi12Crc16Args crc16_args;
    Sdi12CrcEncodeArgs crc_encode_args;
    Sdi12CheckCrcArgs check_crc_args;

    proto_bool ok;
    size_t n;
    uint16_t crc;

    void (*const build)(uint8_t *restrict work);
    void (*const build_ack)(uint8_t *restrict work);
    void (*const build_identify)(uint8_t *restrict work);
    void (*const build_measure)(uint8_t *restrict work);
    void (*const build_concurrent)(uint8_t *restrict work);
    void (*const build_measure_additional)(uint8_t *restrict work);
    void (*const build_concurrent_additional)(uint8_t *restrict work);
    void (*const build_continuous)(uint8_t *restrict work);
    void (*const build_verify)(uint8_t *restrict work);
    void (*const build_data)(uint8_t *restrict work);
    void (*const build_change_address)(uint8_t *restrict work);
    void (*const build_query_address)(uint8_t *restrict work);
    void (*const parse_measure)(uint8_t *restrict work);
    void (*const parse_values)(uint8_t *restrict work);
    void (*const parse_identify)(uint8_t *restrict work);
    void (*const crc16)(uint8_t *restrict work);
    void (*const crc_encode)(uint8_t *restrict work);
    void (*const check_crc)(uint8_t *restrict work);
} Sdi12Ns;

/** @brief The one symbol this module exports. */
extern Sdi12Ns Sdi12;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SDI12

#endif // PROTOCORE_SDI12_H
