// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmmd.h
 * @brief Waveshare HMMD 24 GHz mmWave human micro-motion radar codec (PC_ENABLE_HMMD).
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
 * A report's intra-frame length is @ref PC_HMMD_REPORT_LEN (1 + 2 + 16*2 = 35), so a whole report
 * frame is 4 + 2 + 35 + 4 = @ref PC_HMMD_FRAME_MAX octets. Everything multi-octet is LITTLE-endian.
 * Unlike the LD2410 the payload carries no head/tail marker or check byte - the header, footer, and
 * the length agreeing with the buffer are the whole of the validation.
 *
 * Where the LD2410 reports a moving/stationary split with 9 range gates, the HMMD reports a single
 * detection flag, one distance, and the per-gate energy of 16 gates - it is a micro-motion detector,
 * so "still person breathing" is the case it is built to catch.
 *
 * This codec is pure and host-tested: ::pc_hmmd_parse_report decodes one report frame and
 * ::HmmdStream reassembles frames byte-by-byte from a UART with resync on noise (no heap, fixed
 * buffer), mirroring `Ld2410Stream`. The command encoders build the config frames, and
 * ::pc_hmmd_parse_ack decodes the module's replies.
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

#include "protocore_config.h"

#if PC_ENABLE_HMMD

/** @brief Range gates the HMMD reports energy for (gate 0..15). */
#define PC_HMMD_GATES 16

/** @brief Intra-frame length of a report: detect(1) + distance(2) + 16 gates x 2. */
#define PC_HMMD_REPORT_LEN 35

/** @brief Largest assembled frame: header(4) + len(2) + payload(35) + footer(4). */
#define PC_HMMD_FRAME_MAX 45

/** @brief A decoded HMMD target report. */
typedef struct
{
    uint8_t detected;                    ///< 1 if a target is present.
    uint16_t distance_cm;                ///< target distance (cm); meaningless unless detected.
    uint16_t gate_energy[PC_HMMD_GATES]; ///< per-gate energy, gate 0..15.
} HmmdReport;

/**
 * @brief Decode one whole HMMD report frame (header `F4 F3 F2 F1` .. footer `F8 F7 F6 F5`).
 * Pure - no I/O. Validates the header, the footer, and that the intra-frame length is exactly
 * @ref PC_HMMD_REPORT_LEN and frames the buffer.
 * @return true on a valid frame; false on any mismatch or a short buffer.
 */
proto_bool pc_hmmd_parse_report(const uint8_t *frame, size_t len, HmmdReport *out);

/** @brief Byte-by-byte report-frame reassembler (fixed buffer, resyncs on noise). */
typedef struct
{
    uint8_t buf[PC_HMMD_FRAME_MAX]; ///< frame under construction
    uint16_t pos;                   ///< octets collected so far
    uint16_t total;                 ///< expected full-frame length (known after the length field)
    uint8_t hdr_match;              ///< header octets matched while syncing
    uint8_t phase;                  ///< 0 sync, 1 length, 2 body
} HmmdStream;

/** @brief Reset a stream to the syncing state. */
void pc_hmmd_stream_reset(HmmdStream *s);

/**
 * @brief Feed one received octet. When it completes a valid report frame, fills @p out and returns
 * true; otherwise false (still syncing / mid-frame / bad frame - it resyncs).
 */
proto_bool pc_hmmd_stream_push(HmmdStream *s, uint8_t byte, HmmdReport *out);

/** @brief true if @p r shows a target. */
proto_bool pc_hmmd_present(const HmmdReport *r);

/** @brief Target distance (cm), or 0 when nothing is detected. */
uint16_t pc_hmmd_distance_cm(const HmmdReport *r);

// --- Config-command encoders (build a full `FD FC FB FA` .. `04 03 02 01` frame) ---------------
// Each returns the frame length written, or 0 if @p cap is too small. Commands other than
// open/close must be bracketed by open/close command mode.

/**
 * @brief Build an arbitrary command frame: @p word plus @p vlen octets of @p value.
 *
 * The named encoders below are thin wrappers over this. It is public because the module's register
 * and parameter maps are larger than the handful of commands worth naming here.
 */
size_t pc_hmmd_cmd_build(uint8_t *buf, size_t cap, uint16_t word, const uint8_t *value, size_t vlen);

/** @brief "Open command mode" (word 0x00FF, value 0x0001). */
size_t pc_hmmd_cmd_open(uint8_t *buf, size_t cap);
/** @brief "Close command mode" (word 0x00FE, no value). */
size_t pc_hmmd_cmd_close(uint8_t *buf, size_t cap);
/** @brief Read the radar firmware version (word 0x0000). */
size_t pc_hmmd_cmd_read_firmware(uint8_t *buf, size_t cap);
/** @brief Read the module serial number (word 0x0011). */
size_t pc_hmmd_cmd_read_serial(uint8_t *buf, size_t cap);
/** @brief Read the parameter configuration (word 0x0008). */
size_t pc_hmmd_cmd_read_config(uint8_t *buf, size_t cap);

/**
 * @brief Read a register (word 0x0002), with @p vlen octets of caller-supplied selector @p value.
 *
 * The selector payload is passed through verbatim rather than modelled: the reference this codec was
 * built against does not specify the register map, so inventing a layout here would be a guess. The
 * framing is exact; what goes in the value is the caller's per their module documentation.
 */
size_t pc_hmmd_cmd_read_register(uint8_t *buf, size_t cap, const uint8_t *value, size_t vlen);

// --- Command-ACK decoding ----------------------------------------------------------------------

/** @brief A decoded command-ACK frame. @ref payload points into the caller's frame (not copied). */
typedef struct
{
    uint16_t command;       ///< the ACK's command word, as sent on the wire.
    const uint8_t *payload; ///< octets following the command word (nullptr if none).
    size_t payload_len;     ///< octets at @ref payload.
} HmmdAck;

/**
 * @brief Decode one command-ACK frame (header, intra-frame length, footer and length agreement all
 *        checked). @return false if @p frame is not a well-formed ACK.
 *
 * The ACK's payload is handed back whole rather than split into a status word: the reference only
 * establishes that the ACK echoes the request's command octet, so how many leading octets are a
 * status is not something this codec asserts.
 */
proto_bool pc_hmmd_parse_ack(const uint8_t *frame, size_t len, HmmdAck *out);

/**
 * @brief True if @p ack is the reply to request @p word.
 *
 * Matches on the low octet, which is what the reference verifies. This family conventionally sets
 * bit 8 in the reply (0x0008 -> 0x0108), but that is not asserted here.
 */
proto_bool pc_hmmd_ack_matches(const HmmdAck *ack, uint16_t word);

// --- Binding (no-ops on a host build) ----------------------------------------------------------

/** @brief Open UART2 at PC_HMMD_BAUD on @p rx_pin / @p tx_pin. @return true on ESP32. */
proto_bool pc_hmmd_begin(int rx_pin, int tx_pin);

/** @brief Pump the UART through the stream. @return true if a fresh report was decoded. */
proto_bool pc_hmmd_poll();

/** @brief The most recently decoded report, or nullptr before the first one arrives. */
const HmmdReport *pc_hmmd_last();

#endif // PC_ENABLE_HMMD
#endif // PROTOCORE_HMMD_H
