// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file thread.h
 * @brief Thread spinel / HDLC-lite framing codec (PROTOCORE_ENABLE_THREAD) - OpenThread RCP.
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
 * protocore_spinel_frame_encode() wraps a payload; protocore_spinel_frame_decode() finds the flag, removes the
 * stuffing, and verifies the FCS. protocore_spinel_fcs() is the shared checksum. The spinel command
 * inside (a property get/set/insert, an 802.15.4 stream) is the application's. Pure - you
 * carry the bytes over your UART - so it is fully host-testable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_THREAD_H
#define PROTOCORE_THREAD_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_THREAD

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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
#define SPINEL_STATUS_RESET_POWER_ON 112 ///< first of the reset-cause block
#define SPINEL_STATUS_RESET_END 128      ///< one past the block: 112..127 are all reset causes

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

/** @brief A registry entry: a property id, its human name, and its primary spinel datatype char. */
typedef struct
{
    uint32_t id;
    const char *name;
    char type; ///< the leading spinel datatype ('U','i','C','c','S','E','6','b','D', or '.')
} SpinelPropInfo;

/** @brief Build a spinel header byte for interface @p iid and transaction @p tid (tid 0 = no response wanted). */
static inline uint8_t protocore_spinel_header(uint8_t iid, uint8_t tid)
{
    return (uint8_t)(0x80 | ((iid & 0x03) << 4) | (tid & 0x0F));
}

/** @brief The transaction id carried in header byte @p h. */
static inline uint8_t protocore_spinel_header_tid(uint8_t h)
{
    return (uint8_t)(h & 0x0F);
}

/** @brief The interface id carried in header byte @p h. */
static inline uint8_t protocore_spinel_header_iid(uint8_t h)
{
    return (uint8_t)((h >> 4) & 0x03);
}

/** @brief What spinel_fcs takes: buf, len. */
typedef struct
{
    const uint8_t *buf;
    uint16_t len;
} ThreadSpinelFcsArgs;

/** @brief What spinel_pack_uint takes: value, out, cap. */
typedef struct
{
    uint32_t value;
    uint8_t *out;
    uint8_t cap;
} ThreadSpinelPackUintArgs;

/** @brief What spinel_unpack_uint takes: raw, len, value. */
typedef struct
{
    const uint8_t *raw;
    uint8_t len;
    uint32_t *value;
} ThreadSpinelUnpackUintArgs;

/** @brief What spinel_command_build takes: header, cmd, prop, value, ... */
typedef struct
{
    uint8_t header;
    uint32_t cmd;
    uint32_t prop;
    const uint8_t *value;
    uint16_t value_len;
    uint8_t *out;
    uint16_t cap;
} ThreadSpinelCommandBuildArgs;

/** @brief What spinel_command_parse takes: payload, len, header, cmd, ... */
typedef struct
{
    const uint8_t *payload;
    uint16_t len;
    uint8_t *header;
    uint32_t *cmd;
    uint32_t *prop;
    const uint8_t **value;
    uint16_t *value_len;
} ThreadSpinelCommandParseArgs;

/** @brief What spinel_reader_init takes: r, value, len. */
typedef struct
{
    SpinelReader *r;
    const uint8_t *value;
    uint16_t len;
} ThreadSpinelReaderInitArgs;

/** @brief What spinel_get_bool takes: r, out. */
typedef struct
{
    SpinelReader *r;
    proto_bool *out;
} ThreadSpinelGetBoolArgs;

/** @brief What spinel_get_u8 takes: r, out. */
typedef struct
{
    SpinelReader *r;
    uint8_t *out;
} ThreadSpinelGetU8Args;

/** @brief What spinel_get_i8 takes: r, out. */
typedef struct
{
    SpinelReader *r;
    int8_t *out;
} ThreadSpinelGetI8Args;

/** @brief What spinel_get_u16 takes: r, out. */
typedef struct
{
    SpinelReader *r;
    uint16_t *out;
} ThreadSpinelGetU16Args;

/** @brief What spinel_get_i16 takes: r, out. */
typedef struct
{
    SpinelReader *r;
    int16_t *out;
} ThreadSpinelGetI16Args;

/** @brief What spinel_get_u32 takes: r, out. */
typedef struct
{
    SpinelReader *r;
    uint32_t *out;
} ThreadSpinelGetU32Args;

/** @brief What spinel_get_i32 takes: r, out. */
typedef struct
{
    SpinelReader *r;
    int32_t *out;
} ThreadSpinelGetI32Args;

/** @brief What spinel_get_uint takes: r, out. */
typedef struct
{
    SpinelReader *r;
    uint32_t *out;
} ThreadSpinelGetUintArgs;

/** @brief What spinel_get_eui64 takes: r, out8. */
typedef struct
{
    SpinelReader *r;
    const uint8_t **out8;
} ThreadSpinelGetEui64Args;

/** @brief What spinel_get_ipv6 takes: r, out16. */
typedef struct
{
    SpinelReader *r;
    const uint8_t **out16;
} ThreadSpinelGetIpv6Args;

/** @brief What spinel_get_utf8 takes: r, out, out_len. */
typedef struct
{
    SpinelReader *r;
    const char **out;
    uint16_t *out_len;
} ThreadSpinelGetUtf8Args;

/** @brief What spinel_get_data takes: r, out, out_len. */
typedef struct
{
    SpinelReader *r;
    const uint8_t **out;
    uint16_t *out_len;
} ThreadSpinelGetDataArgs;

/** @brief What spinel_get_data_wlen takes: r, out, out_len. */
typedef struct
{
    SpinelReader *r;
    const uint8_t **out;
    uint16_t *out_len;
} ThreadSpinelGetDataWlenArgs;

/** @brief What spinel_reader_ok takes: r. */
typedef struct
{
    const SpinelReader *r;
} ThreadSpinelReaderOkArgs;

/** @brief What spinel_writer_init takes: w, out, cap. */
typedef struct
{
    SpinelWriter *w;
    uint8_t *out;
    uint16_t cap;
} ThreadSpinelWriterInitArgs;

/** @brief What spinel_put_bool takes: w, v. */
typedef struct
{
    SpinelWriter *w;
    proto_bool v;
} ThreadSpinelPutBoolArgs;

/** @brief What spinel_put_u8 takes: w, v. */
typedef struct
{
    SpinelWriter *w;
    uint8_t v;
} ThreadSpinelPutU8Args;

/** @brief What spinel_put_i8 takes: w, v. */
typedef struct
{
    SpinelWriter *w;
    int8_t v;
} ThreadSpinelPutI8Args;

/** @brief What spinel_put_u16 takes: w, v. */
typedef struct
{
    SpinelWriter *w;
    uint16_t v;
} ThreadSpinelPutU16Args;

/** @brief What spinel_put_i16 takes: w, v. */
typedef struct
{
    SpinelWriter *w;
    int16_t v;
} ThreadSpinelPutI16Args;

/** @brief What spinel_put_u32 takes: w, v. */
typedef struct
{
    SpinelWriter *w;
    uint32_t v;
} ThreadSpinelPutU32Args;

/** @brief What spinel_put_i32 takes: w, v. */
typedef struct
{
    SpinelWriter *w;
    int32_t v;
} ThreadSpinelPutI32Args;

/** @brief What spinel_put_uint takes: w, v. */
typedef struct
{
    SpinelWriter *w;
    uint32_t v;
} ThreadSpinelPutUintArgs;

/** @brief What spinel_put_eui64 takes: w, v8. */
typedef struct
{
    SpinelWriter *w;
    const uint8_t *v8;
} ThreadSpinelPutEui64Args;

/** @brief What spinel_put_ipv6 takes: w, v16. */
typedef struct
{
    SpinelWriter *w;
    const uint8_t *v16;
} ThreadSpinelPutIpv6Args;

/** @brief What spinel_put_utf8 takes: w, s. */
typedef struct
{
    SpinelWriter *w;
    const char *s;
} ThreadSpinelPutUtf8Args;

/** @brief What spinel_put_data takes: w, d, n. */
typedef struct
{
    SpinelWriter *w;
    const uint8_t *d;
    uint16_t n;
} ThreadSpinelPutDataArgs;

/** @brief What spinel_put_data_wlen takes: w, d, n. */
typedef struct
{
    SpinelWriter *w;
    const uint8_t *d;
    uint16_t n;
} ThreadSpinelPutDataWlenArgs;

/** @brief What spinel_writer_len takes: w. */
typedef struct
{
    const SpinelWriter *w;
} ThreadSpinelWriterLenArgs;

/** @brief What spinel_prop_lookup takes: id. */
typedef struct
{
    uint32_t id;
} ThreadSpinelPropLookupArgs;

/** @brief What spinel_prop_name takes: id. */
typedef struct
{
    uint32_t id;
} ThreadSpinelPropNameArgs;

/** @brief What spinel_status_name takes: status. */
typedef struct
{
    uint32_t status;
} ThreadSpinelStatusNameArgs;

/** @brief What spinel_frame_encode takes: payload, len, out, cap. */
typedef struct
{
    const uint8_t *payload;
    uint16_t len;
    uint8_t *out;
    uint16_t cap;
} ThreadSpinelFrameEncodeArgs;

/** @brief What spinel_frame_decode takes: raw, len, payload, pay_cap, ... */
typedef struct
{
    const uint8_t *raw;
    uint16_t len;
    uint8_t *payload;
    uint16_t pay_cap;
    uint16_t *pay_len;
} ThreadSpinelFrameDecodeArgs;

/**
 * @brief Thread spinel / HDLC-lite framing codec (PROTOCORE_ENABLE_THREAD) - OpenThread RCP.
 *
 * A caller sets the members a call takes, invokes it through ::Thread with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Thread.spinel_fcs_args.buf = ...;
 *   Thread.spinel_fcs_args.len = ...;
 *   Thread.spinel_fcs(work);
 *   // Thread.value is what the call reports
 *
 * @var ThreadNs::spinel_fcs_args  what spinel_fcs takes: buf, len
 * @var ThreadNs::spinel_pack_uint_args  what spinel_pack_uint takes: value, out, cap
 * @var ThreadNs::spinel_unpack_uint_args  what spinel_unpack_uint takes: raw, len, value
 * @var ThreadNs::spinel_command_build_args  what spinel_command_build takes: header, cmd, prop, value,
 * @var ThreadNs::spinel_command_parse_args  what spinel_command_parse takes: payload, len, header, cmd,
 * @var ThreadNs::spinel_reader_init_args  what spinel_reader_init takes: r, value, len
 * @var ThreadNs::spinel_get_bool_args  what spinel_get_bool takes: r, out
 * @var ThreadNs::spinel_get_u8_args  what spinel_get_u8 takes: r, out
 * @var ThreadNs::spinel_get_i8_args  what spinel_get_i8 takes: r, out
 * @var ThreadNs::spinel_get_u16_args  what spinel_get_u16 takes: r, out
 * @var ThreadNs::spinel_get_i16_args  what spinel_get_i16 takes: r, out
 * @var ThreadNs::spinel_get_u32_args  what spinel_get_u32 takes: r, out
 * @var ThreadNs::spinel_get_i32_args  what spinel_get_i32 takes: r, out
 * @var ThreadNs::spinel_get_uint_args  what spinel_get_uint takes: r, out
 * @var ThreadNs::spinel_get_eui64_args  what spinel_get_eui64 takes: r, out8
 * @var ThreadNs::spinel_get_ipv6_args  what spinel_get_ipv6 takes: r, out16
 * @var ThreadNs::spinel_get_utf8_args  what spinel_get_utf8 takes: r, out, out_len
 * @var ThreadNs::spinel_get_data_args  what spinel_get_data takes: r, out, out_len
 * @var ThreadNs::spinel_get_data_wlen_args  what spinel_get_data_wlen takes: r, out, out_len
 * @var ThreadNs::spinel_reader_ok_args  what spinel_reader_ok takes: r
 * @var ThreadNs::spinel_writer_init_args  what spinel_writer_init takes: w, out, cap
 * @var ThreadNs::spinel_put_bool_args  what spinel_put_bool takes: w, v
 * @var ThreadNs::spinel_put_u8_args  what spinel_put_u8 takes: w, v
 * @var ThreadNs::spinel_put_i8_args  what spinel_put_i8 takes: w, v
 * @var ThreadNs::spinel_put_u16_args  what spinel_put_u16 takes: w, v
 * @var ThreadNs::spinel_put_i16_args  what spinel_put_i16 takes: w, v
 * @var ThreadNs::spinel_put_u32_args  what spinel_put_u32 takes: w, v
 * @var ThreadNs::spinel_put_i32_args  what spinel_put_i32 takes: w, v
 * @var ThreadNs::spinel_put_uint_args  what spinel_put_uint takes: w, v
 * @var ThreadNs::spinel_put_eui64_args  what spinel_put_eui64 takes: w, v8
 * @var ThreadNs::spinel_put_ipv6_args  what spinel_put_ipv6 takes: w, v16
 * @var ThreadNs::spinel_put_utf8_args  what spinel_put_utf8 takes: w, s
 * @var ThreadNs::spinel_put_data_args  what spinel_put_data takes: w, d, n
 * @var ThreadNs::spinel_put_data_wlen_args  what spinel_put_data_wlen takes: w, d, n
 * @var ThreadNs::spinel_writer_len_args  what spinel_writer_len takes: w
 * @var ThreadNs::spinel_prop_lookup_args  what spinel_prop_lookup takes: id
 * @var ThreadNs::spinel_prop_name_args  what spinel_prop_name takes: id
 * @var ThreadNs::spinel_status_name_args  what spinel_status_name takes: status
 * @var ThreadNs::spinel_frame_encode_args  what spinel_frame_encode takes: payload, len, out, cap
 * @var ThreadNs::spinel_frame_decode_args  what spinel_frame_decode takes: raw, len, payload, pay_cap,
 * @var ThreadNs::ok  a call's true/false outcome
 * @var ThreadNs::value  the payload length, or 0 if it would not fit cap
 * @var ThreadNs::u8  the number of bytes written (1..5), or 0 if it would not fit cap
 * @var ThreadNs::n  the bytes consumed (> 0, value in value), 0 if more bytes are ...
 * @var ThreadNs::ptr  the pointer a call reports
 * @var ThreadNs::text  the string a call reports
 * @var ThreadNs::spinel_fcs  HDLC frame check sequence: CRC-16/X-25 over buf
 * @var ThreadNs::spinel_pack_uint  encode a spinel packed unsigned integer (7 bits/byte, ...
 * @var ThreadNs::spinel_unpack_uint  decode a spinel packed unsigned integer from the front of raw
 * @var ThreadNs::spinel_command_build  build a spinel property-command payload (`header | CMD | PROP | ...
 * @var ThreadNs::spinel_command_parse  parse a spinel property-command payload (from a decoded HDLC frame)
 * @var ThreadNs::spinel_reader_init  spinel_reader_init
 * @var ThreadNs::spinel_get_bool  spinel_get_bool
 * @var ThreadNs::spinel_get_u8  spinel_get_u8
 * @var ThreadNs::spinel_get_i8  spinel_get_i8
 * @var ThreadNs::spinel_get_u16  spinel_get_u16
 * @var ThreadNs::spinel_get_i16  spinel_get_i16
 * @var ThreadNs::spinel_get_u32  spinel_get_u32
 * @var ThreadNs::spinel_get_i32  spinel_get_i32
 * @var ThreadNs::spinel_get_uint  spinel_get_uint
 * @var ThreadNs::spinel_get_eui64  spinel_get_eui64
 * @var ThreadNs::spinel_get_ipv6  spinel_get_ipv6
 * @var ThreadNs::spinel_get_utf8  UTF8 'U': out points into the value, out_len excludes the NUL; ...
 * @var ThreadNs::spinel_get_data  data 'D' (to end of value): out points into the value, out_len is ...
 * @var ThreadNs::spinel_get_data_wlen  data 'd' (uint16-LE length prefix): reads the count, then that many ...
 * @var ThreadNs::spinel_reader_ok  true if every read so far stayed in bounds
 * @var ThreadNs::spinel_writer_init  spinel_writer_init
 * @var ThreadNs::spinel_put_bool  spinel_put_bool
 * @var ThreadNs::spinel_put_u8  spinel_put_u8
 * @var ThreadNs::spinel_put_i8  spinel_put_i8
 * @var ThreadNs::spinel_put_u16  spinel_put_u16
 * @var ThreadNs::spinel_put_i16  spinel_put_i16
 * @var ThreadNs::spinel_put_u32  spinel_put_u32
 * @var ThreadNs::spinel_put_i32  spinel_put_i32
 * @var ThreadNs::spinel_put_uint  spinel_put_uint
 * @var ThreadNs::spinel_put_eui64  spinel_put_eui64
 * @var ThreadNs::spinel_put_ipv6  spinel_put_ipv6
 * @var ThreadNs::spinel_put_utf8  spinel_put_utf8
 * @var ThreadNs::spinel_put_data  spinel_put_data
 * @var ThreadNs::spinel_put_data_wlen  spinel_put_data_wlen
 * @var ThreadNs::spinel_writer_len  the finished value length, or 0 if any write overflowed
 * @var ThreadNs::spinel_prop_lookup  look up a property's registry entry, or nullptr if it is not in the ...
 * @var ThreadNs::spinel_prop_name  A property's human name, or "UNKNOWN" if unregistered
 * @var ThreadNs::spinel_status_name  A `LAST_STATUS` code's human name, or "UNKNOWN" if unregistered
 * @var ThreadNs::spinel_frame_encode  encode an HDLC-lite frame: payload + FCS, byte-stuffed, ...
 * @var ThreadNs::spinel_frame_decode  decode one HDLC-lite frame from the front of raw: find the flag, ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ThreadSpinelFcsArgs spinel_fcs_args;
    ThreadSpinelPackUintArgs spinel_pack_uint_args;
    ThreadSpinelUnpackUintArgs spinel_unpack_uint_args;
    ThreadSpinelCommandBuildArgs spinel_command_build_args;
    ThreadSpinelCommandParseArgs spinel_command_parse_args;
    ThreadSpinelReaderInitArgs spinel_reader_init_args;
    ThreadSpinelGetBoolArgs spinel_get_bool_args;
    ThreadSpinelGetU8Args spinel_get_u8_args;
    ThreadSpinelGetI8Args spinel_get_i8_args;
    ThreadSpinelGetU16Args spinel_get_u16_args;
    ThreadSpinelGetI16Args spinel_get_i16_args;
    ThreadSpinelGetU32Args spinel_get_u32_args;
    ThreadSpinelGetI32Args spinel_get_i32_args;
    ThreadSpinelGetUintArgs spinel_get_uint_args;
    ThreadSpinelGetEui64Args spinel_get_eui64_args;
    ThreadSpinelGetIpv6Args spinel_get_ipv6_args;
    ThreadSpinelGetUtf8Args spinel_get_utf8_args;
    ThreadSpinelGetDataArgs spinel_get_data_args;
    ThreadSpinelGetDataWlenArgs spinel_get_data_wlen_args;
    ThreadSpinelReaderOkArgs spinel_reader_ok_args;
    ThreadSpinelWriterInitArgs spinel_writer_init_args;
    ThreadSpinelPutBoolArgs spinel_put_bool_args;
    ThreadSpinelPutU8Args spinel_put_u8_args;
    ThreadSpinelPutI8Args spinel_put_i8_args;
    ThreadSpinelPutU16Args spinel_put_u16_args;
    ThreadSpinelPutI16Args spinel_put_i16_args;
    ThreadSpinelPutU32Args spinel_put_u32_args;
    ThreadSpinelPutI32Args spinel_put_i32_args;
    ThreadSpinelPutUintArgs spinel_put_uint_args;
    ThreadSpinelPutEui64Args spinel_put_eui64_args;
    ThreadSpinelPutIpv6Args spinel_put_ipv6_args;
    ThreadSpinelPutUtf8Args spinel_put_utf8_args;
    ThreadSpinelPutDataArgs spinel_put_data_args;
    ThreadSpinelPutDataWlenArgs spinel_put_data_wlen_args;
    ThreadSpinelWriterLenArgs spinel_writer_len_args;
    ThreadSpinelPropLookupArgs spinel_prop_lookup_args;
    ThreadSpinelPropNameArgs spinel_prop_name_args;
    ThreadSpinelStatusNameArgs spinel_status_name_args;
    ThreadSpinelFrameEncodeArgs spinel_frame_encode_args;
    ThreadSpinelFrameDecodeArgs spinel_frame_decode_args;

    proto_bool ok;
    uint16_t value;
    uint8_t u8;
    int n;
    const SpinelPropInfo *ptr;
    const char *text;

    void (*const spinel_fcs)(uint8_t *restrict work);
    void (*const spinel_pack_uint)(uint8_t *restrict work);
    void (*const spinel_unpack_uint)(uint8_t *restrict work);
    void (*const spinel_command_build)(uint8_t *restrict work);
    void (*const spinel_command_parse)(uint8_t *restrict work);
    void (*const spinel_reader_init)(uint8_t *restrict work);
    void (*const spinel_get_bool)(uint8_t *restrict work);
    void (*const spinel_get_u8)(uint8_t *restrict work);
    void (*const spinel_get_i8)(uint8_t *restrict work);
    void (*const spinel_get_u16)(uint8_t *restrict work);
    void (*const spinel_get_i16)(uint8_t *restrict work);
    void (*const spinel_get_u32)(uint8_t *restrict work);
    void (*const spinel_get_i32)(uint8_t *restrict work);
    void (*const spinel_get_uint)(uint8_t *restrict work);
    void (*const spinel_get_eui64)(uint8_t *restrict work);
    void (*const spinel_get_ipv6)(uint8_t *restrict work);
    void (*const spinel_get_utf8)(uint8_t *restrict work);
    void (*const spinel_get_data)(uint8_t *restrict work);
    void (*const spinel_get_data_wlen)(uint8_t *restrict work);
    void (*const spinel_reader_ok)(uint8_t *restrict work);
    void (*const spinel_writer_init)(uint8_t *restrict work);
    void (*const spinel_put_bool)(uint8_t *restrict work);
    void (*const spinel_put_u8)(uint8_t *restrict work);
    void (*const spinel_put_i8)(uint8_t *restrict work);
    void (*const spinel_put_u16)(uint8_t *restrict work);
    void (*const spinel_put_i16)(uint8_t *restrict work);
    void (*const spinel_put_u32)(uint8_t *restrict work);
    void (*const spinel_put_i32)(uint8_t *restrict work);
    void (*const spinel_put_uint)(uint8_t *restrict work);
    void (*const spinel_put_eui64)(uint8_t *restrict work);
    void (*const spinel_put_ipv6)(uint8_t *restrict work);
    void (*const spinel_put_utf8)(uint8_t *restrict work);
    void (*const spinel_put_data)(uint8_t *restrict work);
    void (*const spinel_put_data_wlen)(uint8_t *restrict work);
    void (*const spinel_writer_len)(uint8_t *restrict work);
    void (*const spinel_prop_lookup)(uint8_t *restrict work);
    void (*const spinel_prop_name)(uint8_t *restrict work);
    void (*const spinel_status_name)(uint8_t *restrict work);
    void (*const spinel_frame_encode)(uint8_t *restrict work);
    void (*const spinel_frame_decode)(uint8_t *restrict work);
} ThreadNs;

/** @brief The one symbol this module exports. */
extern ThreadNs Thread;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_THREAD

#endif // PROTOCORE_THREAD_H
