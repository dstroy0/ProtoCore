// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ubx.h
 * @brief u-blox UBX binary protocol codec (PC_ENABLE_UBX) - the GNSS receiver control/nav protocol.
 *
 * A UBX frame is `B5 62 <class> <id> <len-LE:2> <payload...> <CK_A> <CK_B>`: two sync chars, a
 * class/id message selector, a little-endian payload length, the payload, and an 8-bit Fletcher
 * checksum computed over everything between the sync chars and the checksum (class..payload end).
 * This codec builds a frame (adding the sync chars, length, and checksum), builds a zero-length
 * poll request (how UBX asks a receiver to emit a message), parses one complete frame (validating
 * the length and checksum), and - because a u-blox receiver multiplexes UBX with ASCII NMEA on the
 * same UART - provides a streaming demultiplexer that pulls UBX frames out of a byte stream and
 * hands every non-UBX byte back to the caller (for an NMEA line assembler). Little-endian payload
 * readers and an ACK helper decode the common replies. Pure codec, host-tested; the UART is the
 * application's (a plain HardwareSerial link, commonly 9600 baud).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_UBX_H
#define PROTOCORE_UBX_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_UBX

#define PC_UBX_SYNC1 0xB5u ///< first sync char
#define PC_UBX_SYNC2 0x62u ///< second sync char

/** @brief A parsed UBX frame. @p payload aliases the caller's / stream's buffer (@p len octets). */
typedef struct
{
    uint8_t cls;            ///< message class
    uint8_t id;             ///< message id
    uint16_t len;           ///< payload length
    const uint8_t *payload; ///< payload (len octets); references an external buffer
} pc_ubx;

/**
 * @brief 8-bit Fletcher checksum over @p body (the class..payload-end span - everything the frame
 * checksums). Writes @p ck_a / @p ck_b.
 */
void pc_ubx_checksum(const uint8_t *body, size_t len, uint8_t *ck_a, uint8_t *ck_b);

/**
 * @brief Build a UBX frame into @p buf: `B5 62 cls id len(LE) payload CK_A CK_B`.
 * @return total frame length (8 + @p len) or 0 on overflow / bad args (@p payload may be NULL
 *         only when @p len is 0).
 */
size_t pc_ubx_build(uint8_t *buf, size_t cap, uint8_t cls, uint8_t id, const uint8_t *payload, uint16_t len);

/** @brief Build a zero-length poll request for (@p cls, @p id) - how UBX asks for a message. */
size_t pc_ubx_build_poll(uint8_t *buf, size_t cap, uint8_t cls, uint8_t id);

/**
 * @brief Parse exactly one complete UBX frame at the front of @p s. Validates the sync chars, the
 * declared length against @p len, and the checksum; fills @p out (payload aliases @p s). Returns
 * false on a short / malformed / bad-checksum frame.
 */
proto_bool pc_ubx_parse(const uint8_t *s, size_t len, pc_ubx *out);

/**
 * @brief ACK helper. Returns 1 for UBX-ACK-ACK, 0 for UBX-ACK-NAK, -1 if @p m is not an ACK frame.
 * When it is an ACK, writes the acknowledged class/id (the payload's two octets) to @p acked_cls /
 * @p acked_id.
 */
int pc_ubx_ack(const pc_ubx *m, uint8_t *acked_cls, uint8_t *acked_id);

/** @brief Little-endian readers for a payload at byte offset @p off (caller bounds-checks @p off). */
uint16_t pc_ubx_u16(const uint8_t *p, size_t off);
uint32_t pc_ubx_u32(const uint8_t *p, size_t off);
int16_t pc_ubx_i16(const uint8_t *p, size_t off);
int32_t pc_ubx_i32(const uint8_t *p, size_t off);

// -- NAV-PVT: the u-blox all-in-one navigation solution (position / velocity / time) --

#define PC_UBX_CLASS_NAV 0x01   ///< navigation-results message class
#define PC_UBX_NAV_PVT 0x07     ///< NAV-PVT message id (class NAV)
#define PC_UBX_NAV_PVT_LEN 92   ///< NAV-PVT payload length (u-blox 8 / M8)
#define PC_UBX_PVT_FIX_OK 0x01u ///< NAV-PVT flags bit 0: a valid fix (within DOP / accuracy masks)

/** @brief NAV-PVT fixType values. */
enum pc_ubx_fix_type
{
    PC_UBX_FIX_NONE = 0,    ///< no fix
    PC_UBX_FIX_DR = 1,      ///< dead-reckoning only
    PC_UBX_FIX_2D = 2,      ///< 2D fix
    PC_UBX_FIX_3D = 3,      ///< 3D fix
    PC_UBX_FIX_GNSS_DR = 4, ///< GNSS + dead reckoning
    PC_UBX_FIX_TIME = 5     ///< time-only fix
};

/** @brief Decoded UBX-NAV-PVT payload (the fields an application usually needs; native integer scales). */
typedef struct // NOSONAR(cpp:S1820): UBX-NAV-PVT is one fixed protocol message; its ~30 fields mirror the
               // wire layout, so splitting the struct would be artificial, not clearer
{
    uint32_t itow_ms;      ///< GPS time of week of the solution (ms)
    uint16_t year;         ///< UTC year
    uint8_t month;         ///< UTC month (1..12)
    uint8_t day;           ///< UTC day (1..31)
    uint8_t hour;          ///< UTC hour (0..23)
    uint8_t minute;        ///< UTC minute (0..59)
    uint8_t second;        ///< UTC second (0..60)
    uint8_t valid;         ///< validity flags (validDate / validTime / fullyResolved / validMag)
    int32_t nano;          ///< fraction of second, -1e9..1e9 (ns)
    uint32_t time_acc_ns;  ///< time accuracy estimate (ns)
    uint8_t fix_type;      ///< @ref pc_ubx_fix_type
    uint8_t flags;         ///< fix status flags (bit 0 = @ref PC_UBX_PVT_FIX_OK)
    uint8_t num_sv;        ///< number of satellites used in the solution
    int32_t lon_1e7;       ///< longitude (1e-7 deg)
    int32_t lat_1e7;       ///< latitude (1e-7 deg)
    int32_t height_mm;     ///< height above the ellipsoid (mm)
    int32_t hmsl_mm;       ///< height above mean sea level (mm)
    uint32_t h_acc_mm;     ///< horizontal accuracy estimate (mm)
    uint32_t v_acc_mm;     ///< vertical accuracy estimate (mm)
    int32_t vel_n_mm_s;    ///< NED north velocity (mm/s)
    int32_t vel_e_mm_s;    ///< NED east velocity (mm/s)
    int32_t vel_d_mm_s;    ///< NED down velocity (mm/s)
    int32_t gspeed_mm_s;   ///< 2-D ground speed (mm/s)
    int32_t head_mot_1e5;  ///< heading of motion (1e-5 deg)
    uint32_t s_acc_mm_s;   ///< speed accuracy estimate (mm/s)
    uint32_t head_acc_1e5; ///< heading accuracy estimate (1e-5 deg)
    uint16_t pdop_1e2;     ///< position DOP (0.01)
} pc_ubx_nav_pvt;

/**
 * @brief Decode a UBX-NAV-PVT frame into @p out (per the u-blox interface description).
 * @return true iff @p m is a NAV-PVT frame (class 0x01 / id 0x07) of at least @ref PC_UBX_NAV_PVT_LEN
 *         octets; false (and @p out untouched) otherwise.
 */
proto_bool pc_ubx_parse_nav_pvt(const pc_ubx *m, pc_ubx_nav_pvt *out);

// -- NAV-SAT: per-satellite signal + usage info (variable length) --

#define PC_UBX_NAV_SAT 0x35           ///< NAV-SAT message id (class NAV)
#define PC_UBX_NAV_SAT_HDR_LEN 8      ///< NAV-SAT fixed header (iTOW + version + numSvs + reserved)
#define PC_UBX_NAV_SAT_ENTRY_LEN 12   ///< NAV-SAT per-satellite block length
#define PC_UBX_SAT_QUALITY_MASK 0x07u ///< flags bits 0..2: signal quality indicator
#define PC_UBX_SAT_USED 0x08u         ///< flags bit 3: this satellite is used in the navigation solution

/** @brief NAV-SAT fixed header. */
typedef struct
{
    uint32_t itow_ms; ///< GPS time of week (ms)
    uint8_t version;  ///< message version (1)
    uint8_t num_svs;  ///< number of satellite blocks that follow
} pc_ubx_nav_sat_hdr;

/** @brief One NAV-SAT satellite block. */
typedef struct
{
    uint8_t gnss_id;    ///< GNSS identifier (0 GPS, 2 Galileo, 3 BeiDou, 5 QZSS, 6 GLONASS, ...)
    uint8_t sv_id;      ///< satellite identifier within the GNSS
    uint8_t cno_dbhz;   ///< carrier-to-noise density ratio (dB-Hz)
    int8_t elev_deg;    ///< elevation (deg, -90..90; out of range if unknown)
    int16_t azim_deg;   ///< azimuth (deg, 0..360)
    int16_t pr_res_01m; ///< pseudorange residual (0.1 m)
    uint32_t flags;     ///< bitfield: quality (@ref PC_UBX_SAT_QUALITY_MASK), used (@ref PC_UBX_SAT_USED), ...
} pc_ubx_sat;

/**
 * @brief Decode a UBX-NAV-SAT frame's fixed header (per the u-blox interface description).
 * @return true iff @p m is a NAV-SAT frame (class 0x01 / id 0x35) whose declared length holds the header
 *         plus its @c num_svs blocks; false otherwise. Walk the blocks with @ref pc_ubx_nav_sat_get.
 */
proto_bool pc_ubx_parse_nav_sat(const pc_ubx *m, pc_ubx_nav_sat_hdr *out);

/**
 * @brief Decode satellite block @p index (0-based) from a NAV-SAT frame into @p out.
 * @return true on success, false if @p index is out of range or @p m is not a valid NAV-SAT frame.
 */
proto_bool pc_ubx_nav_sat_get(const pc_ubx *m, uint8_t index, pc_ubx_sat *out);

// -- NAV-TIMEUTC: the validated UTC time solution (for a GNSS time source) --

#define PC_UBX_NAV_TIMEUTC 0x21        ///< NAV-TIMEUTC message id (class NAV)
#define PC_UBX_NAV_TIMEUTC_LEN 20      ///< NAV-TIMEUTC payload length
#define PC_UBX_TIMEUTC_VALID_TOW 0x01u ///< valid flags bit 0: the time of week is valid
#define PC_UBX_TIMEUTC_VALID_WKN 0x02u ///< valid flags bit 1: the week number is valid
#define PC_UBX_TIMEUTC_VALID_UTC 0x04u ///< valid flags bit 2: the UTC time is valid (leap seconds known)

/** @brief Decoded UBX-NAV-TIMEUTC payload. */
typedef struct
{
    uint32_t itow_ms;     ///< GPS time of week (ms)
    uint32_t time_acc_ns; ///< time accuracy estimate (ns)
    int32_t nano;         ///< fraction of second, -1e9..1e9 (ns)
    uint16_t year;        ///< UTC year (1999..2099)
    uint8_t month;        ///< UTC month (1..12)
    uint8_t day;          ///< UTC day (1..31)
    uint8_t hour;         ///< UTC hour (0..23)
    uint8_t minute;       ///< UTC minute (0..59)
    uint8_t second;       ///< UTC second (0..60)
    uint8_t valid;        ///< validity flags (@ref PC_UBX_TIMEUTC_VALID_TOW / _WKN / _UTC + utcStandard)
    proto_bool utc_valid; ///< convenience: the UTC-valid bit is set (leap seconds resolved)
} pc_ubx_nav_time_utc;

/**
 * @brief Decode a UBX-NAV-TIMEUTC frame into @p out (the receiver's UTC time, for a GNSS time source).
 * @return true iff @p m is a NAV-TIMEUTC frame (class 0x01 / id 0x21) of at least 20 octets; false otherwise.
 */
proto_bool pc_ubx_parse_nav_timeutc(const pc_ubx *m, pc_ubx_nav_time_utc *out);

// -- CFG: configure the receiver (which messages to emit, and how fast) --

#define PC_UBX_CLASS_CFG 0x06 ///< configuration-input message class
#define PC_UBX_CFG_MSG 0x01   ///< CFG-MSG: set a message's output rate
#define PC_UBX_CFG_RATE 0x08  ///< CFG-RATE: set the measurement / navigation rate
#define PC_UBX_TIME_REF_UTC 0 ///< CFG-RATE timeRef: align measurements to UTC
#define PC_UBX_TIME_REF_GPS 1 ///< CFG-RATE timeRef: align measurements to GPS time

/**
 * @brief Build a CFG-MSG that sets how often (@p cls, @p id) is emitted on the current port.
 *
 * @p rate is in navigation solutions: 0 disables the message, 1 emits it every solution, N every Nth.
 * This is the short (3-octet) form that targets the port the command arrives on.
 * @return the frame length (11) or 0 on overflow / a null buffer.
 */
size_t pc_ubx_build_cfg_msg(uint8_t *buf, size_t cap, uint8_t cls, uint8_t id, uint8_t rate);

/**
 * @brief Build a CFG-RATE that sets the measurement + navigation rate.
 * @param meas_rate_ms  the measurement period in ms (e.g. 200 for 5 Hz).
 * @param nav_rate      navigation solutions per measurement cycle (usually 1).
 * @param time_ref      the reference the measurements align to (@ref PC_UBX_TIME_REF_UTC / _GPS).
 * @return the frame length (14) or 0 on overflow / a null buffer.
 */
size_t pc_ubx_build_cfg_rate(uint8_t *buf, size_t cap, uint16_t meas_rate_ms, uint16_t nav_rate, uint16_t time_ref);

/** @brief Result of feeding one byte to the streaming demultiplexer. */
enum pc_ubx_feed
{
    PC_UBX_NONE = 0,        ///< byte consumed inside a (partial or discarded) UBX frame
    PC_UBX_FRAME = 1,       ///< a complete, checksum-valid frame is in @p out
    PC_UBX_PASSTHROUGH = 2, ///< byte is not part of any UBX frame (returned in @p passthrough)
    PC_UBX_OVERFLOW = 3     ///< a frame whose declared length exceeds PC_UBX_MAX_PAYLOAD; skipped
};

/** @brief Streaming demultiplexer state for a mixed NMEA + UBX byte stream. Zero-initialize / init. */
typedef struct
{
    uint8_t state;                   ///< internal parser state
    uint8_t cls;                     ///< class of the frame in progress
    uint8_t id;                      ///< id of the frame in progress
    uint16_t len;                    ///< declared payload length of the frame in progress
    uint16_t idx;                    ///< payload bytes received so far
    uint32_t skip;                   ///< bytes left to discard for an over-long frame
    uint8_t ck_a;                    ///< running checksum A
    uint8_t ck_b;                    ///< running checksum B
    uint8_t rx_ck_a;                 ///< received checksum A (awaiting B to compare)
    uint8_t buf[PC_UBX_MAX_PAYLOAD]; ///< payload accumulator
} pc_ubx_stream;

/** @brief Reset a demux to the idle (hunting-for-sync) state. */
void pc_ubx_stream_init(pc_ubx_stream *st);

/**
 * @brief Feed one byte. Returns a ::pc_ubx_feed code: PC_UBX_FRAME (@p out filled, payload aliases
 * the stream buffer, valid until the next feed), PC_UBX_PASSTHROUGH (@p passthrough set to a
 * non-UBX byte - hand it to your NMEA assembler), PC_UBX_OVERFLOW (an over-long frame was skipped),
 * or PC_UBX_NONE.
 */
int pc_ubx_stream_feed(pc_ubx_stream *st, uint8_t b, pc_ubx *out, uint8_t *passthrough);

#endif // PC_ENABLE_UBX

PROTO_END_DECLS

#endif // PROTOCORE_UBX_H
