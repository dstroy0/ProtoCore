// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file thread.h
 * @brief Thread spinel / HDLC-lite framing codec (PC_ENABLE_THREAD) - OpenThread RCP.
 *
 * The HDLC-lite framing that carries spinel frames to an OpenThread radio co-processor (an
 * nRF52840 / EFR32 RCP) over UART - an 802.15.4 / Thread mesh bridged to IP and the web.
 * HDLC-lite wraps each spinel frame by appending an FCS, byte-stuffing the reserved bytes,
 * and terminating with a Flag:
 *
 *   [spinel payload | FCS(lo,hi)] --byte-stuffed--> ... | 0x7E
 *
 * The FCS is the HDLC frame check sequence, **CRC-16/X-25** (poly 0x1021 reflected, init
 * 0xFFFF, reflected in/out, final XOR 0xFFFF), transmitted low byte first. The reserved
 * bytes stuffed (as 0x7D, byte XOR 0x20) are the Flag 0x7E, the Escape 0x7D, XON 0x11, and
 * XOFF 0x13.
 *
 * pc_spinel_frame_encode() wraps a payload; pc_spinel_frame_decode() finds the flag, removes the
 * stuffing, and verifies the FCS. pc_spinel_fcs() is the shared checksum. The spinel command
 * inside (a property get/set/insert, an 802.15.4 stream) is the application's. Pure - you
 * carry the bytes over your UART - so it is fully host-testable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_THREAD_H
#define PROTOCORE_THREAD_H

#include "protocore_config.h"

#if PC_ENABLE_THREAD

/** @brief HDLC-lite markers. */
#define HDLC_FLAG 0x7E   ///< frame delimiter
#define HDLC_ESCAPE 0x7D ///< byte-stuffing escape

/** @brief Common spinel commands (the property accessors a gateway uses). */
#define SPINEL_CMD_NOOP 0
#define SPINEL_CMD_RESET 1
#define SPINEL_CMD_PROP_VALUE_GET 2
#define SPINEL_CMD_PROP_VALUE_SET 3
#define SPINEL_CMD_PROP_VALUE_INSERT 4
#define SPINEL_CMD_PROP_VALUE_REMOVE 5
#define SPINEL_CMD_PROP_VALUE_IS 6       ///< an async property update from the NCP
#define SPINEL_CMD_PROP_VALUE_INSERTED 7 ///< a list property gained an entry
#define SPINEL_CMD_PROP_VALUE_REMOVED 8  ///< a list property lost an entry

/**
 * @brief The spinel property ids a Thread/802.15.4 gateway reads or writes (subset of the
 *        spinel property registry, grouped CORE / PHY / MAC / NET / IPv6 / STREAM).
 */
// Core (SPINEL_PROP_CORE__BEGIN = 0)
#define SPINEL_PROP_LAST_STATUS 0      ///< 'i'  last operation status
#define SPINEL_PROP_PROTOCOL_VERSION 1 ///< 'ii' major, minor
#define SPINEL_PROP_NCP_VERSION 2      ///< 'U'  NCP version string
#define SPINEL_PROP_INTERFACE_TYPE 3   ///< 'i'  3 = Thread
#define SPINEL_PROP_VENDOR_ID 4        ///< 'i'
#define SPINEL_PROP_CAPS 5             ///< 'A(i)' capability list
#define SPINEL_PROP_INTERFACE_COUNT 6  ///< 'C'
#define SPINEL_PROP_HWADDR 8           ///< 'E'  factory EUI64
#define SPINEL_PROP_LOCK 9             ///< 'b'
// PHY (SPINEL_PROP_PHY__BEGIN = 0x20)
#define SPINEL_PROP_PHY_ENABLED 0x20        ///< 'b'
#define SPINEL_PROP_PHY_CHAN 0x21           ///< 'C'  802.15.4 channel
#define SPINEL_PROP_PHY_CHAN_SUPPORTED 0x22 ///< 'A(C)'
#define SPINEL_PROP_PHY_FREQ 0x23           ///< 'L'  kHz
#define SPINEL_PROP_PHY_TX_POWER 0x25       ///< 'c'  dBm
#define SPINEL_PROP_PHY_RSSI 0x26           ///< 'c'  dBm
// MAC (SPINEL_PROP_MAC__BEGIN = 0x30)
#define SPINEL_PROP_MAC_SCAN_STATE 0x30  ///< 'C'
#define SPINEL_PROP_MAC_SCAN_MASK 0x31   ///< 'A(C)'
#define SPINEL_PROP_MAC_SCAN_PERIOD 0x32 ///< 'S'  ms/channel
#define SPINEL_PROP_MAC_15_4_LADDR 0x34  ///< 'E'  extended (long) address
#define SPINEL_PROP_MAC_15_4_SADDR 0x35  ///< 'S'  short address
#define SPINEL_PROP_MAC_15_4_PANID 0x36  ///< 'S'  PAN id
// NET (SPINEL_PROP_NET__BEGIN = 0x40)
#define SPINEL_PROP_NET_SAVED 0x40        ///< 'b'
#define SPINEL_PROP_NET_IF_UP 0x41        ///< 'b'
#define SPINEL_PROP_NET_STACK_UP 0x42     ///< 'b'
#define SPINEL_PROP_NET_ROLE 0x43         ///< 'C'  0 detached,1 child,2 router,3 leader
#define SPINEL_PROP_NET_NETWORK_NAME 0x44 ///< 'U'
#define SPINEL_PROP_NET_XPANID 0x45       ///< 'D'  8-byte extended PAN id
#define SPINEL_PROP_NET_NETWORK_KEY 0x46  ///< 'D'  16-byte network key
// IPv6 (SPINEL_PROP_IPV6__BEGIN = 0x60)
#define SPINEL_PROP_IPV6_LL_ADDR 0x60 ///< '6'  link-local
#define SPINEL_PROP_IPV6_ML_ADDR 0x61 ///< '6'  mesh-local
// Stream (SPINEL_PROP_STREAM__BEGIN = 0x70)
#define SPINEL_PROP_STREAM_DEBUG 0x70 ///< 'U'  debug text
#define SPINEL_PROP_STREAM_RAW 0x71   ///< 'dD' raw 802.15.4 frame + metadata
#define SPINEL_PROP_STREAM_NET 0x72   ///< 'dD' IPv6 datagram + metadata

/** @brief spinel `LAST_STATUS` codes (a subset - the ones a gateway acts on). */
#define SPINEL_STATUS_OK 0
#define SPINEL_STATUS_FAILURE 1
#define SPINEL_STATUS_UNIMPLEMENTED 2
#define SPINEL_STATUS_INVALID_ARGUMENT 3
#define SPINEL_STATUS_INVALID_STATE 4
#define SPINEL_STATUS_INVALID_COMMAND 5
#define SPINEL_STATUS_INVALID_INTERFACE 6
#define SPINEL_STATUS_INTERNAL_ERROR 7
#define SPINEL_STATUS_SECURITY_ERROR 8
#define SPINEL_STATUS_PARSE_ERROR 9
#define SPINEL_STATUS_IN_PROGRESS 10
#define SPINEL_STATUS_NOMEM 11
#define SPINEL_STATUS_BUSY 12
#define SPINEL_STATUS_PROP_NOT_FOUND 13
#define SPINEL_STATUS_DROPPED 14
#define SPINEL_STATUS_EMPTY 15
#define SPINEL_STATUS_RESET_POWER_ON 112 ///< 0x70..0x77 are reset causes

// --- Header byte (bit7 = flag, bits6-4 = interface id, bits3-0 = transaction id) --------

/** @brief Build a spinel header byte for interface @p iid and transaction @p tid (tid 0 = no response wanted). */
inline uint8_t pc_spinel_header(uint8_t iid, uint8_t tid)
{
    return (uint8_t)(0x80 | ((iid & 0x03) << 4) | (tid & 0x0F));
}
/** @brief The transaction id carried in header byte @p h. */
inline uint8_t pc_spinel_header_tid(uint8_t h)
{
    return (uint8_t)(h & 0x0F);
}
/** @brief The interface id carried in header byte @p h. */
inline uint8_t pc_spinel_header_iid(uint8_t h)
{
    return (uint8_t)((h >> 4) & 0x03);
}

/** @brief HDLC frame check sequence: CRC-16/X-25 over @p buf. */
uint16_t pc_spinel_fcs(const uint8_t *buf, uint16_t len);

// --- Spinel command layer (rides inside a decoded HDLC frame's payload) ---------------

/**
 * @brief Encode a spinel packed unsigned integer (7 bits/byte, little-endian, high bit =
 *        continuation) into @p out.
 * @return the number of bytes written (1..5), or 0 if it would not fit @p cap.
 */
uint8_t pc_spinel_pack_uint(uint32_t value, uint8_t *out, uint8_t cap);

/**
 * @brief Decode a spinel packed unsigned integer from the front of @p raw.
 * @return the bytes consumed (> 0, value in @p value), 0 if more bytes are needed, or -1 if
 *         the encoding overflows a uint32.
 */
int pc_spinel_unpack_uint(const uint8_t *raw, uint8_t len, uint32_t *value);

/**
 * @brief Build a spinel property-command payload (`header | CMD | PROP | value`) - the
 *        content of an HDLC frame - into @p out. CMD and PROP are packed integers.
 * @return the payload length, or 0 if it would not fit @p cap.
 */
uint16_t pc_spinel_command_build(uint8_t header, uint32_t cmd, uint32_t prop, const uint8_t *value, uint16_t value_len,
                                 uint8_t *out, uint16_t cap);

/**
 * @brief Parse a spinel property-command payload (from a decoded HDLC frame).
 * @param[out] header    the flag/iid/tid header byte.
 * @param[out] cmd       the command (unpacked).
 * @param[out] prop      the property id (unpacked).
 * @param[out] value     the first value byte (points into @p payload).
 * @param[out] value_len the value length.
 * @return the value offset (> 0), or -1 if the header / command / property is malformed.
 */
int pc_spinel_command_parse(const uint8_t *payload, uint16_t len, uint8_t *header, uint32_t *cmd, uint32_t *prop,
                            const uint8_t **value, uint16_t *value_len);

// --- Spinel value semantics (the typed datatypes carried in a property value) -----------
//
// spinel encodes a property value as a sequence of typed fields (spinel datatype format):
//   b bool, C uint8, c int8, S uint16-LE, s int16-LE, L uint32-LE, l int32-LE, i packed-uint,
//   E EUI64 (8B), 6 IPv6 (16B), U UTF8 (NUL-terminated), D data (rest), d data (uint16-LE len).
// A cursor reads/writes those fields in order (an array A(t) is just a field read in a loop,
// a struct T(...) is its fields in sequence). Any out-of-bounds read/write latches `err` so a
// caller can run a whole sequence and check once at the end.

/** @brief A read cursor over a spinel property value. */
typedef struct
{
    const uint8_t *buf; ///< the value bytes
    uint16_t len;       ///< value length
    uint16_t off;       ///< next unread offset
    proto_bool err;     ///< set once any read runs past the end / is malformed
} SpinelReader;

/** @brief A write cursor building a spinel property value into a caller buffer. */
typedef struct
{
    uint8_t *buf;   ///< output buffer
    uint16_t cap;   ///< output capacity
    uint16_t off;   ///< bytes written so far
    proto_bool err; ///< set once any write would overflow @c cap
} SpinelWriter;

void pc_spinel_reader_init(SpinelReader *r, const uint8_t *value, uint16_t len);
proto_bool pc_spinel_get_bool(SpinelReader *r, proto_bool *out);
proto_bool pc_spinel_get_u8(SpinelReader *r, uint8_t *out);
proto_bool pc_spinel_get_i8(SpinelReader *r, int8_t *out);
proto_bool pc_spinel_get_u16(SpinelReader *r, uint16_t *out);
proto_bool pc_spinel_get_i16(SpinelReader *r, int16_t *out);
proto_bool pc_spinel_get_u32(SpinelReader *r, uint32_t *out);
proto_bool pc_spinel_get_i32(SpinelReader *r, int32_t *out);
proto_bool pc_spinel_get_uint(SpinelReader *r, uint32_t *out); ///< packed 'i'
proto_bool pc_spinel_get_eui64(SpinelReader *r, const uint8_t **out8);
proto_bool pc_spinel_get_ipv6(SpinelReader *r, const uint8_t **out16);
/** @brief UTF8 'U': @p out points into the value, @p out_len excludes the NUL; advances past it. */
proto_bool pc_spinel_get_utf8(SpinelReader *r, const char **out, uint16_t *out_len);
/** @brief Data 'D' (to end of value): @p out points into the value, @p out_len is the remainder. */
proto_bool pc_spinel_get_data(SpinelReader *r, const uint8_t **out, uint16_t *out_len);
/** @brief Data 'd' (uint16-LE length prefix): reads the count, then that many bytes. */
proto_bool pc_spinel_get_data_wlen(SpinelReader *r, const uint8_t **out, uint16_t *out_len);
/** @brief True if every read so far stayed in bounds. */
proto_bool pc_spinel_reader_ok(const SpinelReader *r);

void pc_spinel_writer_init(SpinelWriter *w, uint8_t *out, uint16_t cap);
proto_bool pc_spinel_put_bool(SpinelWriter *w, proto_bool v);
proto_bool pc_spinel_put_u8(SpinelWriter *w, uint8_t v);
proto_bool pc_spinel_put_i8(SpinelWriter *w, int8_t v);
proto_bool pc_spinel_put_u16(SpinelWriter *w, uint16_t v);
proto_bool pc_spinel_put_i16(SpinelWriter *w, int16_t v);
proto_bool pc_spinel_put_u32(SpinelWriter *w, uint32_t v);
proto_bool pc_spinel_put_i32(SpinelWriter *w, int32_t v);
proto_bool pc_spinel_put_uint(SpinelWriter *w, uint32_t v); ///< packed 'i'
proto_bool pc_spinel_put_eui64(SpinelWriter *w, const uint8_t *v8);
proto_bool pc_spinel_put_ipv6(SpinelWriter *w, const uint8_t *v16);
proto_bool pc_spinel_put_utf8(SpinelWriter *w, const char *s);                     ///< 'U' incl. the NUL
proto_bool pc_spinel_put_data(SpinelWriter *w, const uint8_t *d, uint16_t n);      ///< 'D' raw bytes
proto_bool pc_spinel_put_data_wlen(SpinelWriter *w, const uint8_t *d, uint16_t n); ///< 'd' uint16-LE len + bytes
/** @brief The finished value length, or 0 if any write overflowed. */
uint16_t pc_spinel_writer_len(const SpinelWriter *w);

// --- Property registry (id -> name + primary datatype) ----------------------------------

/** @brief A registry entry: a property id, its human name, and its primary spinel datatype char. */
typedef struct
{
    uint32_t id;
    const char *name;
    char type; ///< the leading spinel datatype ('U','i','C','c','S','E','6','b','D', or '.')
} SpinelPropInfo;

/** @brief Look up a property's registry entry, or nullptr if it is not in the registry. */
const SpinelPropInfo *pc_spinel_prop_lookup(uint32_t id);
/** @brief A property's human name, or "UNKNOWN" if unregistered. */
const char *pc_spinel_prop_name(uint32_t id);
/** @brief A `LAST_STATUS` code's human name, or "UNKNOWN" if unregistered. */
const char *pc_spinel_status_name(uint32_t status);

/**
 * @brief Encode an HDLC-lite frame: @p payload + FCS, byte-stuffed, flag-terminated.
 * @return the encoded frame length, or 0 if @p len exceeds PC_THREAD_MAX_DATA or the
 *         stuffed frame would not fit @p cap.
 */
uint16_t pc_spinel_frame_encode(const uint8_t *payload, uint16_t len, uint8_t *out, uint16_t cap);

/**
 * @brief Decode one HDLC-lite frame from the front of @p raw: find the flag, remove the
 *        stuffing, verify the FCS, and copy the spinel payload to @p payload.
 * @param[out] pay_len set to the payload length.
 * @return the bytes consumed up to and including the flag (> 0), 0 if no flag is present yet
 *         (need more), or -1 if the frame is malformed (too short, bad FCS, dangling escape,
 *         or the payload overflows @p pay_cap) - the caller drops one byte and retries.
 */
int pc_spinel_frame_decode(const uint8_t *raw, uint16_t len, uint8_t *payload, uint16_t pay_cap, uint16_t *pay_len);

#endif // PC_ENABLE_THREAD

#endif // PROTOCORE_THREAD_H
