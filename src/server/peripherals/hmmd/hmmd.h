// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmmd.h
 * @brief Waveshare HMMD 24 GHz mmWave human micro-motion radar codec (PROTOCORE_ENABLE_HMMD).
 *
 * The HMMD (Waveshare's FMCW micro-motion detection module, built on the S3KM1110 / SXKMxxx0 class
 * of radar SoC) reports human presence and range over a 115200-baud UART, and additionally drives a
 * bare GPIO **OUT** pin. It is a close relative of the HLK-LD2410 (`services/ld2410`) and shares its
 * framing exactly - two frame kinds, the same magic sequences, a little-endian intra-frame length:
 *
 * @code
 *   report:  F4 F3 F2 F1 | len(2) | detect(1) | distance(2) | gate_energy[16](2 each) | F8 F7 F6 F5
 *   command: FD FC FB FA | len(2) | word(2)   | [value]                               | 04 03 02 01
 * @endcode
 *
 * A report's intra-frame length is @ref PROTOCORE_HMMD_REPORT_LEN (1 + 2 + 16*2 = 35), so a whole report
 * frame is 4 + 2 + 35 + 4 = @ref PROTOCORE_HMMD_FRAME_MAX octets. Everything multi-octet is LITTLE-endian.
 * Unlike the LD2410 the payload carries no head/tail marker or check byte - the header, footer, and
 * the length agreeing with the buffer are the whole of the validation.
 *
 * Where the LD2410 reports a moving/stationary split with 9 range gates, the HMMD reports a single
 * detection flag, one distance, and the per-gate energy of 16 gates - it is a micro-motion detector,
 * so "still person breathing" is the case it is built to catch.
 *
 * This codec is pure and host-tested: ::protocore_hmmd_parse_report decodes one report frame and
 * ::HmmdStream reassembles frames byte-by-byte from a UART with resync on noise (no heap, fixed
 * buffer), mirroring `Ld2410Stream`. The command encoders build the config frames, and
 * ::protocore_hmmd_parse_ack decodes the module's replies.
 *
 * The module's GPIO OUT pin is a bare active-high presence line with no protocol at all. Feed it to
 * @ref PresenceCore from `services/rcwl0516` (the shared one-GPIO presence facade) to get the same
 * debounced, hold-extended presence the RCWL-0516 gets; that is an application-level wiring choice,
 * so this service deliberately does not depend on that one.
 *
 * Framing, the report payload layout, the command words, and the open/close command-mode encoding
 * were taken from the public `2Grey/s3km1110` reference library and cross-checked for internal
 * consistency (its `kMaxFrameLength` of 45 and `kDistanceGateCount` of 16 agree exactly with the
 * 35-octet report payload derived here). No vendor SDK is used or required.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HMMD_H
#define PROTOCORE_HMMD_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HMMD

PROTOCORE_BEGIN_DECLS

// PROTOCORE_HMMD_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define PROTOCORE_HMMD_GATES 16

#define PROTOCORE_HMMD_REPORT_LEN 35

#define PROTOCORE_HMMD_FRAME_MAX 45

/** @brief A decoded HMMD target report. */
typedef struct
{
    uint8_t detected;                           ///< 1 if a target is present.
    uint16_t distance_cm;                       ///< target distance (cm); meaningless unless detected.
    uint16_t gate_energy[PROTOCORE_HMMD_GATES]; ///< per-gate energy, gate 0..15.
} HmmdReport;

/** @brief Byte-by-byte report-frame reassembler (fixed buffer, resyncs on noise). */
typedef struct
{
    uint8_t buf[PROTOCORE_HMMD_FRAME_MAX]; ///< frame under construction
    uint16_t pos;                          ///< octets collected so far
    uint16_t total;                        ///< expected full-frame length (known after the length field)
    uint8_t hdr_match;                     ///< header octets matched while syncing
    uint8_t phase;                         ///< 0 sync, 1 length, 2 body
} HmmdStream;

/** @brief A decoded command-ACK frame. @ref payload points into the caller's frame (not copied). */
typedef struct
{
    uint16_t command;       ///< the ACK's command word, as sent on the wire.
    const uint8_t *payload; ///< octets following the command word (nullptr if none).
    size_t payload_len;     ///< octets at @ref payload.
} HmmdAck;

/** @brief What parse_report takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    HmmdReport *out;
} HmmdParseReportArgs;

/** @brief What stream_reset takes: s. */
typedef struct
{
    HmmdStream *s;
} HmmdStreamResetArgs;

/** @brief What stream_push takes: s, byte, out. */
typedef struct
{
    HmmdStream *s;
    uint8_t byte;
    HmmdReport *out;
} HmmdStreamPushArgs;

/** @brief What present takes: r. */
typedef struct
{
    const HmmdReport *r;
} HmmdPresentArgs;

/** @brief What distance_cm takes: r. */
typedef struct
{
    const HmmdReport *r;
} HmmdDistanceCmArgs;

/** @brief What cmd_build takes: buf, cap, word, value, vlen. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t word;
    const uint8_t *value;
    size_t vlen;
} HmmdCmdBuildArgs;

/** @brief What cmd_open takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} HmmdCmdOpenArgs;

/** @brief What cmd_close takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} HmmdCmdCloseArgs;

/** @brief What cmd_read_firmware takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} HmmdCmdReadFirmwareArgs;

/** @brief What cmd_read_serial takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} HmmdCmdReadSerialArgs;

/** @brief What cmd_read_config takes: buf, cap. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
} HmmdCmdReadConfigArgs;

/** @brief What cmd_read_register takes: buf, cap, value, vlen. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *value;
    size_t vlen;
} HmmdCmdReadRegisterArgs;

/** @brief What parse_ack takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    HmmdAck *out;
} HmmdParseAckArgs;

/** @brief What ack_matches takes: ack, word. */
typedef struct
{
    const HmmdAck *ack;
    uint16_t word;
} HmmdAckMatchesArgs;

/** @brief What begin takes: rx_pin, tx_pin. */
typedef struct
{
    int rx_pin;
    int tx_pin;
} HmmdBeginArgs;

/**
 * @brief Waveshare HMMD 24 GHz mmWave human micro-motion radar codec (PROTOCORE_ENABLE_HMMD). The HMMD (Waveshare's ...
 *
 * A caller sets the members a call takes, invokes it through ::Hmmd with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Hmmd.parse_report_args.frame = ...;
 *   Hmmd.parse_report_args.len = ...;
 *   Hmmd.parse_report_args.out = ...;
 *   Hmmd.parse_report(work);
 *   // Hmmd.ok is what the call reports
 *
 * @var HmmdNs::parse_report_args  what parse_report takes: frame, len, out
 * @var HmmdNs::stream_reset_args  what stream_reset takes: s
 * @var HmmdNs::stream_push_args  what stream_push takes: s, byte, out
 * @var HmmdNs::present_args  what present takes: r
 * @var HmmdNs::distance_cm_args  what distance_cm takes: r
 * @var HmmdNs::cmd_build_args  what cmd_build takes: buf, cap, word, value, vlen
 * @var HmmdNs::cmd_open_args  what cmd_open takes: buf, cap
 * @var HmmdNs::cmd_close_args  what cmd_close takes: buf, cap
 * @var HmmdNs::cmd_read_firmware_args  what cmd_read_firmware takes: buf, cap
 * @var HmmdNs::cmd_read_serial_args  what cmd_read_serial takes: buf, cap
 * @var HmmdNs::cmd_read_config_args  what cmd_read_config takes: buf, cap
 * @var HmmdNs::cmd_read_register_args  what cmd_read_register takes: buf, cap, value, vlen
 * @var HmmdNs::parse_ack_args  what parse_ack takes: frame, len, out
 * @var HmmdNs::ack_matches_args  what ack_matches takes: ack, word
 * @var HmmdNs::begin_args  what begin takes: rx_pin, tx_pin
 * @var HmmdNs::ok  true on a valid frame; false on any mismatch or a short buffer
 * @var HmmdNs::cm  what a call reports
 * @var HmmdNs::n  the count a call reports
 * @var HmmdNs::report  what a call reports
 * @var HmmdNs::parse_report  decode one whole HMMD report frame (header `F4 F3 F2 F1` .. footer ...
 * @var HmmdNs::stream_reset  reset a stream to the syncing state
 * @var HmmdNs::stream_push  feed one received octet. When it completes a valid report frame, ...
 * @var HmmdNs::present  true if r shows a target
 * @var HmmdNs::distance_cm  target distance (cm), or 0 when nothing is detected
 * @var HmmdNs::cmd_build  build an arbitrary command frame: word plus vlen octets of value. ...
 * @var HmmdNs::cmd_open  "Open command mode" (word 0x00FF, value 0x0001)
 * @var HmmdNs::cmd_close  "Close command mode" (word 0x00FE, no value)
 * @var HmmdNs::cmd_read_firmware  read the radar firmware version (word 0x0000)
 * @var HmmdNs::cmd_read_serial  read the module serial number (word 0x0011)
 * @var HmmdNs::cmd_read_config  read the parameter configuration (word 0x0008)
 * @var HmmdNs::cmd_read_register  read a register (word 0x0002), with vlen octets of caller-supplied ...
 * @var HmmdNs::parse_ack  decode one command-ACK frame (header, intra-frame length, footer ...
 * @var HmmdNs::ack_matches  true if ack is the reply to request word. Matches on the low octet, ...
 * @var HmmdNs::begin  open PROTOCORE_HMMD_UART at PROTOCORE_HMMD_BAUD on rx_pin / tx_pin. ...
 * @var HmmdNs::poll  pump the UART through the stream. true if a fresh report was decoded
 * @var HmmdNs::last  the most recently decoded report, or NULL before the first one ...
 *
 * @c work is PROTOCORE_HMMD_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    HmmdParseReportArgs parse_report_args;
    HmmdStreamResetArgs stream_reset_args;
    HmmdStreamPushArgs stream_push_args;
    HmmdPresentArgs present_args;
    HmmdDistanceCmArgs distance_cm_args;
    HmmdCmdBuildArgs cmd_build_args;
    HmmdCmdOpenArgs cmd_open_args;
    HmmdCmdCloseArgs cmd_close_args;
    HmmdCmdReadFirmwareArgs cmd_read_firmware_args;
    HmmdCmdReadSerialArgs cmd_read_serial_args;
    HmmdCmdReadConfigArgs cmd_read_config_args;
    HmmdCmdReadRegisterArgs cmd_read_register_args;
    HmmdParseAckArgs parse_ack_args;
    HmmdAckMatchesArgs ack_matches_args;
    HmmdBeginArgs begin_args;

    proto_bool ok;
    uint16_t cm;
    size_t n;
    const HmmdReport *report;

    void (*const parse_report)(uint8_t *restrict work);
    void (*const stream_reset)(uint8_t *restrict work);
    void (*const stream_push)(uint8_t *restrict work);
    void (*const present)(uint8_t *restrict work);
    void (*const distance_cm)(uint8_t *restrict work);
    void (*const cmd_build)(uint8_t *restrict work);
    void (*const cmd_open)(uint8_t *restrict work);
    void (*const cmd_close)(uint8_t *restrict work);
    void (*const cmd_read_firmware)(uint8_t *restrict work);
    void (*const cmd_read_serial)(uint8_t *restrict work);
    void (*const cmd_read_config)(uint8_t *restrict work);
    void (*const cmd_read_register)(uint8_t *restrict work);
    void (*const parse_ack)(uint8_t *restrict work);
    void (*const ack_matches)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const poll)(uint8_t *restrict work);
    void (*const last)(uint8_t *restrict work);
} HmmdNs;

/** @brief The one symbol this module exports. */
extern HmmdNs Hmmd;

/**
 * @brief The PROTOCORE_HMMD_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_hmmd_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMMD

#endif // PROTOCORE_HMMD_H
