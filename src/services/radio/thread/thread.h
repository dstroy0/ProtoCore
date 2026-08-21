// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_THREAD_H
#define PROTOCORE_THREAD_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file thread.h
 * @brief Thread spinel / HDLC-lite framing codec (PROTOCORE_ENABLE_THREAD) - OpenThread RCP.
 *
 * The HDLC-lite framing that carries spinel frames to an OpenThread radio co-processor (an
 * nRF52840 / EFR32 RCP) over UART - an 802.15.4 / Thread mesh bridged to IP and the web.
 * HDLC-lite wraps each spinel frame by appending an FCS, byte-stuffing the reserved bytes,
 * and terminating with a Flag:
 *
 * [spinel payload | FCS(lo,hi)] --byte-stuffed--> ... | 0x7E
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
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_THREAD_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    uint16_t (*spinel_fcs)(uint8_t *restrict, const uint8_t *, uint16_t);
    uint8_t (*spinel_pack_uint)(uint8_t *restrict, uint32_t, uint8_t *, uint8_t);
    int (*spinel_unpack_uint)(uint8_t *restrict, const uint8_t *, uint8_t, uint32_t *);
    uint16_t (*spinel_command_build)(uint8_t *restrict, uint8_t, uint32_t, uint32_t, const uint8_t *, uint16_t,
                                     uint8_t *, uint16_t);
    int (*spinel_command_parse)(uint8_t *restrict, const uint8_t *, uint16_t, uint8_t *, uint32_t *, uint32_t *,
                                const uint8_t **, uint16_t *);
    void (*spinel_reader_init)(uint8_t *restrict, SpinelReader *, const uint8_t *, uint16_t);
    proto_bool (*spinel_get_bool)(uint8_t *restrict, SpinelReader *, proto_bool *);
    proto_bool (*spinel_get_u8)(uint8_t *restrict, SpinelReader *, uint8_t *);
    proto_bool (*spinel_get_i8)(uint8_t *restrict, SpinelReader *, int8_t *);
    proto_bool (*spinel_get_u16)(uint8_t *restrict, SpinelReader *, uint16_t *);
    proto_bool (*spinel_get_i16)(uint8_t *restrict, SpinelReader *, int16_t *);
    proto_bool (*spinel_get_u32)(uint8_t *restrict, SpinelReader *, uint32_t *);
    proto_bool (*spinel_get_i32)(uint8_t *restrict, SpinelReader *, int32_t *);
    proto_bool (*spinel_get_uint)(uint8_t *restrict, SpinelReader *, uint32_t *);
    proto_bool (*spinel_get_eui64)(uint8_t *restrict, SpinelReader *, const uint8_t **);
    proto_bool (*spinel_get_ipv6)(uint8_t *restrict, SpinelReader *, const uint8_t **);
    proto_bool (*spinel_get_utf8)(uint8_t *restrict, SpinelReader *, const char **, uint16_t *);
    proto_bool (*spinel_get_data)(uint8_t *restrict, SpinelReader *, const uint8_t **, uint16_t *);
    proto_bool (*spinel_get_data_wlen)(uint8_t *restrict, SpinelReader *, const uint8_t **, uint16_t *);
    proto_bool (*spinel_reader_ok)(uint8_t *restrict, const SpinelReader *);
    void (*spinel_writer_init)(uint8_t *restrict, SpinelWriter *, uint8_t *, uint16_t);
    proto_bool (*spinel_put_bool)(uint8_t *restrict, SpinelWriter *, proto_bool);
    proto_bool (*spinel_put_u8)(uint8_t *restrict, SpinelWriter *, uint8_t);
    void (*spinel_put_i8)(uint8_t *restrict, SpinelWriter *, int8_t);
    proto_bool (*spinel_put_u16)(uint8_t *restrict, SpinelWriter *, uint16_t);
    void (*spinel_put_i16)(uint8_t *restrict, SpinelWriter *, int16_t);
    proto_bool (*spinel_put_u32)(uint8_t *restrict, SpinelWriter *, uint32_t);
    void (*spinel_put_i32)(uint8_t *restrict, SpinelWriter *, int32_t);
    proto_bool (*spinel_put_uint)(uint8_t *restrict, SpinelWriter *, uint32_t);
    proto_bool (*spinel_put_eui64)(uint8_t *restrict, SpinelWriter *, const uint8_t *);
    proto_bool (*spinel_put_ipv6)(uint8_t *restrict, SpinelWriter *, const uint8_t *);
    proto_bool (*spinel_put_utf8)(uint8_t *restrict, SpinelWriter *, const char *);
    proto_bool (*spinel_put_data)(uint8_t *restrict, SpinelWriter *, const uint8_t *, uint16_t);
    proto_bool (*spinel_put_data_wlen)(uint8_t *restrict, SpinelWriter *, const uint8_t *, uint16_t);
    uint16_t (*spinel_writer_len)(uint8_t *restrict, const SpinelWriter *);
    const SpinelPropInfo *(*spinel_prop_lookup)(uint8_t *restrict, uint32_t);
    const char *(*spinel_prop_name)(uint8_t *restrict, uint32_t);
    const char *(*spinel_status_name)(uint8_t *restrict, uint32_t);
    uint16_t (*spinel_frame_encode)(uint8_t *restrict, const uint8_t *, uint16_t, uint8_t *, uint16_t);
    int (*spinel_frame_decode)(uint8_t *restrict, const uint8_t *, uint16_t, uint8_t *, uint16_t, uint16_t *);
} ThreadNs;
PROTOCORE_NS_LAYOUT(ThreadNs, spinel_fcs, spinel_pack_uint, spinel_unpack_uint, spinel_command_build,
                    spinel_command_parse, spinel_reader_init, spinel_get_bool, spinel_get_u8, spinel_get_i8,
                    spinel_get_u16, spinel_get_i16, spinel_get_u32, spinel_get_i32, spinel_get_uint, spinel_get_eui64,
                    spinel_get_ipv6, spinel_get_utf8, spinel_get_data, spinel_get_data_wlen, spinel_reader_ok,
                    spinel_writer_init, spinel_put_bool, spinel_put_u8, spinel_put_i8, spinel_put_u16, spinel_put_i16,
                    spinel_put_u32, spinel_put_i32, spinel_put_uint, spinel_put_eui64, spinel_put_ipv6, spinel_put_utf8,
                    spinel_put_data, spinel_put_data_wlen, spinel_writer_len, spinel_prop_lookup, spinel_prop_name,
                    spinel_status_name, spinel_frame_encode, spinel_frame_decode);

/**
 * @brief HDLC frame check sequence: CRC-16/X-25 over buf.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @return The uint16_t.
 */
uint16_t protocore_thread_spinel_fcs(uint8_t *restrict work, const uint8_t *buf, uint16_t len);
/**
 * @brief Encode a spinel packed unsigned integer (7 bits/byte, .
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param value Value
 * @param out Out
 * @param cap Cap
 * @return The uint8_t.
 */
uint8_t protocore_thread_spinel_pack_uint(uint8_t *restrict work, uint32_t value, uint8_t *out, uint8_t cap);
/**
 * @brief Decode a spinel packed unsigned integer from the front of raw.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param raw Raw
 * @param len Len
 * @param value Value
 * @return The int.
 */
int protocore_thread_spinel_unpack_uint(uint8_t *restrict work, const uint8_t *raw, uint8_t len, uint32_t *value);
/**
 * @brief Build a spinel property-command payload (`header | CMD | PROP | .
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param header Header
 * @param cmd Cmd
 * @param prop Prop
 * @param value Value
 * @param value_len Value len
 * @param out Out
 * @param cap Cap
 * @return The uint16_t.
 */
uint16_t protocore_thread_spinel_command_build(uint8_t *restrict work, uint8_t header, uint32_t cmd, uint32_t prop,
                                               const uint8_t *value, uint16_t value_len, uint8_t *out, uint16_t cap);
/**
 * @brief Parse a spinel property-command payload (from a decoded HDLC frame).
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param payload Payload
 * @param len Len
 * @param header Header
 * @param cmd Cmd
 * @param prop Prop
 * @param value Value
 * @param value_len Value len
 * @return The int.
 */
int protocore_thread_spinel_command_parse(uint8_t *restrict work, const uint8_t *payload, uint16_t len, uint8_t *header,
                                          uint32_t *cmd, uint32_t *prop, const uint8_t **value, uint16_t *value_len);
/**
 * @brief Spinel_reader_init.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param value Value
 * @param len Len
 */
void protocore_thread_spinel_reader_init(uint8_t *restrict work, SpinelReader *r, const uint8_t *value, uint16_t len);
/**
 * @brief Spinel_get_bool.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_bool(uint8_t *restrict work, SpinelReader *r, proto_bool *out);
/**
 * @brief Spinel_get_u8.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_u8(uint8_t *restrict work, SpinelReader *r, uint8_t *out);
/**
 * @brief Spinel_get_i8.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_i8(uint8_t *restrict work, SpinelReader *r, int8_t *out);
/**
 * @brief Spinel_get_u16.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_u16(uint8_t *restrict work, SpinelReader *r, uint16_t *out);
/**
 * @brief Spinel_get_i16.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_i16(uint8_t *restrict work, SpinelReader *r, int16_t *out);
/**
 * @brief Spinel_get_u32.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_u32(uint8_t *restrict work, SpinelReader *r, uint32_t *out);
/**
 * @brief Spinel_get_i32.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_i32(uint8_t *restrict work, SpinelReader *r, int32_t *out);
/**
 * @brief Spinel_get_uint.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_uint(uint8_t *restrict work, SpinelReader *r, uint32_t *out);
/**
 * @brief Spinel_get_eui64.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out8 Out8
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_eui64(uint8_t *restrict work, SpinelReader *r, const uint8_t **out8);
/**
 * @brief Spinel_get_ipv6.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out16 Out16
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_ipv6(uint8_t *restrict work, SpinelReader *r, const uint8_t **out16);
/**
 * @brief UTF8 'U': out points into the value, out_len excludes the NUL; .
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @param out_len Out len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_utf8(uint8_t *restrict work, SpinelReader *r, const char **out,
                                            uint16_t *out_len);
/**
 * @brief Data 'D' (to end of value): out points into the value, out_len is .
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @param out_len Out len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_data(uint8_t *restrict work, SpinelReader *r, const uint8_t **out,
                                            uint16_t *out_len);
/**
 * @brief Data 'd' (uint16-LE length prefix): reads the count, then that many .
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param out Out
 * @param out_len Out len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_get_data_wlen(uint8_t *restrict work, SpinelReader *r, const uint8_t **out,
                                                 uint16_t *out_len);
/**
 * @brief True if every read so far stayed in bounds.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_reader_ok(uint8_t *restrict work, const SpinelReader *r);
/**
 * @brief Spinel_writer_init.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param out Out
 * @param cap Cap
 */
void protocore_thread_spinel_writer_init(uint8_t *restrict work, SpinelWriter *w, uint8_t *out, uint16_t cap);
/**
 * @brief Spinel_put_bool.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v V
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_bool(uint8_t *restrict work, SpinelWriter *w, proto_bool v);
/**
 * @brief Spinel_put_u8.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v V
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_u8(uint8_t *restrict work, SpinelWriter *w, uint8_t v);
/**
 * @brief Spinel_put_i8.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v V
 */
void protocore_thread_spinel_put_i8(uint8_t *restrict work, SpinelWriter *w, int8_t v);
/**
 * @brief Spinel_put_u16.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v V
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_u16(uint8_t *restrict work, SpinelWriter *w, uint16_t v);
/**
 * @brief Spinel_put_i16.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v V
 */
void protocore_thread_spinel_put_i16(uint8_t *restrict work, SpinelWriter *w, int16_t v);
/**
 * @brief Spinel_put_u32.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v V
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_u32(uint8_t *restrict work, SpinelWriter *w, uint32_t v);
/**
 * @brief Spinel_put_i32.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v V
 */
void protocore_thread_spinel_put_i32(uint8_t *restrict work, SpinelWriter *w, int32_t v);
/**
 * @brief Spinel_put_uint.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v V
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_uint(uint8_t *restrict work, SpinelWriter *w, uint32_t v);
/**
 * @brief Spinel_put_eui64.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v8 V8
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_eui64(uint8_t *restrict work, SpinelWriter *w, const uint8_t *v8);
/**
 * @brief Spinel_put_ipv6.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param v16 V16
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_ipv6(uint8_t *restrict work, SpinelWriter *w, const uint8_t *v16);
/**
 * @brief Spinel_put_utf8.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param s S
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_utf8(uint8_t *restrict work, SpinelWriter *w, const char *s);
/**
 * @brief Spinel_put_data.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param d D
 * @param n N
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_data(uint8_t *restrict work, SpinelWriter *w, const uint8_t *d, uint16_t n);
/**
 * @brief Spinel_put_data_wlen.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @param d D
 * @param n N
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_thread_spinel_put_data_wlen(uint8_t *restrict work, SpinelWriter *w, const uint8_t *d, uint16_t n);
/**
 * @brief The finished value length, or 0 if any write overflowed.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param w W
 * @return The uint16_t.
 */
uint16_t protocore_thread_spinel_writer_len(uint8_t *restrict work, const SpinelWriter *w);
/**
 * @brief Look up a property's registry entry, or nullptr if it is not in the .
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param id Id
 * @return The const SpinelPropInfo *.
 */
const SpinelPropInfo *protocore_thread_spinel_prop_lookup(uint8_t *restrict work, uint32_t id);
/**
 * @brief A property's human name, or "UNKNOWN" if unregistered.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param id Id
 * @return The const char *.
 */
const char *protocore_thread_spinel_prop_name(uint8_t *restrict work, uint32_t id);
/**
 * @brief A `LAST_STATUS` code's human name, or "UNKNOWN" if unregistered.
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param status Status
 * @return The const char *.
 */
const char *protocore_thread_spinel_status_name(uint8_t *restrict work, uint32_t status);
/**
 * @brief Encode an HDLC-lite frame: payload + FCS, byte-stuffed, .
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param payload Payload
 * @param len Len
 * @param out Out
 * @param cap Cap
 * @return The uint16_t.
 */
uint16_t protocore_thread_spinel_frame_encode(uint8_t *restrict work, const uint8_t *payload, uint16_t len,
                                              uint8_t *out, uint16_t cap);
/**
 * @brief Decode one HDLC-lite frame from the front of raw: find the flag, .
 * @param work PROTOCORE_THREAD_BORROW bytes the caller took. Not held past the call.
 * @param raw Raw
 * @param len Len
 * @param payload Payload
 * @param pay_cap Pay cap
 * @param pay_len Pay len
 * @return The int.
 */
int protocore_thread_spinel_frame_decode(uint8_t *restrict work, const uint8_t *raw, uint16_t len, uint8_t *payload,
                                         uint16_t pay_cap, uint16_t *pay_len);

/** @brief Module namespace. */
PROTOCORE_NS ThreadNs Thread PROTOCORE_UNUSED = {.spinel_fcs = protocore_thread_spinel_fcs,
                                                 .spinel_pack_uint = protocore_thread_spinel_pack_uint,
                                                 .spinel_unpack_uint = protocore_thread_spinel_unpack_uint,
                                                 .spinel_command_build = protocore_thread_spinel_command_build,
                                                 .spinel_command_parse = protocore_thread_spinel_command_parse,
                                                 .spinel_reader_init = protocore_thread_spinel_reader_init,
                                                 .spinel_get_bool = protocore_thread_spinel_get_bool,
                                                 .spinel_get_u8 = protocore_thread_spinel_get_u8,
                                                 .spinel_get_i8 = protocore_thread_spinel_get_i8,
                                                 .spinel_get_u16 = protocore_thread_spinel_get_u16,
                                                 .spinel_get_i16 = protocore_thread_spinel_get_i16,
                                                 .spinel_get_u32 = protocore_thread_spinel_get_u32,
                                                 .spinel_get_i32 = protocore_thread_spinel_get_i32,
                                                 .spinel_get_uint = protocore_thread_spinel_get_uint,
                                                 .spinel_get_eui64 = protocore_thread_spinel_get_eui64,
                                                 .spinel_get_ipv6 = protocore_thread_spinel_get_ipv6,
                                                 .spinel_get_utf8 = protocore_thread_spinel_get_utf8,
                                                 .spinel_get_data = protocore_thread_spinel_get_data,
                                                 .spinel_get_data_wlen = protocore_thread_spinel_get_data_wlen,
                                                 .spinel_reader_ok = protocore_thread_spinel_reader_ok,
                                                 .spinel_writer_init = protocore_thread_spinel_writer_init,
                                                 .spinel_put_bool = protocore_thread_spinel_put_bool,
                                                 .spinel_put_u8 = protocore_thread_spinel_put_u8,
                                                 .spinel_put_i8 = protocore_thread_spinel_put_i8,
                                                 .spinel_put_u16 = protocore_thread_spinel_put_u16,
                                                 .spinel_put_i16 = protocore_thread_spinel_put_i16,
                                                 .spinel_put_u32 = protocore_thread_spinel_put_u32,
                                                 .spinel_put_i32 = protocore_thread_spinel_put_i32,
                                                 .spinel_put_uint = protocore_thread_spinel_put_uint,
                                                 .spinel_put_eui64 = protocore_thread_spinel_put_eui64,
                                                 .spinel_put_ipv6 = protocore_thread_spinel_put_ipv6,
                                                 .spinel_put_utf8 = protocore_thread_spinel_put_utf8,
                                                 .spinel_put_data = protocore_thread_spinel_put_data,
                                                 .spinel_put_data_wlen = protocore_thread_spinel_put_data_wlen,
                                                 .spinel_writer_len = protocore_thread_spinel_writer_len,
                                                 .spinel_prop_lookup = protocore_thread_spinel_prop_lookup,
                                                 .spinel_prop_name = protocore_thread_spinel_prop_name,
                                                 .spinel_status_name = protocore_thread_spinel_status_name,
                                                 .spinel_frame_encode = protocore_thread_spinel_frame_encode,
                                                 .spinel_frame_decode = protocore_thread_spinel_frame_decode};

PROTOCORE_END_DECLS

#endif // PROTOCORE_THREAD_H
