// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_sniff.h
 * @brief Receive-only radio channel sniffer -> pcap capture records (PROTOCORE_ENABLE_RADIO_SNIFF).
 *
 * The RF gateway drivers (CC1101, LoRa, the Thread 802.15.4 RCP) can run receive-only - sniff a channel
 * without joining - and the frames they pull off the air belong in the same capture pipeline as the CAN
 * and Wi-Fi captures (shared/protocore_pcap). For 802.15.4 that means wrapping each frame in the
 * Wireshark IEEE 802.15.4 **TAP** pseudo-header so the per-frame RSSI and channel travel with it, then a
 * standard pcap record, giving a `.pcap` a wired Wireshark opens with the radio metadata intact.
 *
 * This is that pure framing: ::protocore_radiosniff_global writes the pcap global header (DLT TAP), and
 * ::protocore_radiosniff_tap_record writes one record (TAP header + RSSI/channel TLVs + the MAC frame). The
 * radio drivers own the receive; this owns the on-wire capture bytes. No heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_RADIO_SNIFF_H
#define PROTOCORE_RADIO_SNIFF_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_RADIO_SNIFF

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief The TAP pseudo-header length this codec emits: 4 header + RSSI TLV(8) + channel TLV(8). */
#define RADIO_SNIFF_TAP_LEN 20

/** @brief What global_header takes: out, cap. */
typedef struct
{
    uint8_t *out;
    size_t cap;
} RadioSniffGlobalHeaderArgs;
/** @brief What i2f32 takes: dbm. */
typedef struct
{
    int32_t dbm;
} RadioSniffI2f32Args;
/** @brief What tap_record takes: out, cap, frame, flen, rssi_dbm, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *frame;
    size_t flen;
    int32_t rssi_dbm;
    uint16_t channel;
    uint32_t ts_sec;
    uint32_t ts_usec;
} RadioSniffTapRecordArgs;
/**
 * @brief Receive-only radio channel sniffer -> pcap capture records (PROTOCORE_ENABLE_RADIO_SNIFF).
 *
 * A caller sets the members a call takes, invokes it through ::RadioSniff with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   RadioSniff.global_header_args.out = ...;
 *   RadioSniff.global_header_args.cap = ...;
 *   RadioSniff.global_header(work);
 *   // RadioSniff.n is what the call reports
 *
 * @var RadioSniffNs::global_header_args  what global_header takes: out, cap
 * @var RadioSniffNs::i2f32_args  what i2f32 takes: dbm
 * @var RadioSniffNs::tap_record_args  what tap_record takes: out, cap, frame, flen, rssi_dbm,
 * @var RadioSniffNs::ok  a call's true/false outcome
 * @var RadioSniffNs::n  total bytes written, or 0 on overflow / bad args
 * @var RadioSniffNs::u32  what a call reports
 * @var RadioSniffNs::global_header  write the pcap global header for a TAP-framed 802.15.4 sniff. 24, ...
 * @var RadioSniffNs::i2f32  encode a signed integer dBm value as an IEEE-754 float32 ...
 * @var RadioSniffNs::tap_record  write one capture record: a pcap record header, the 802.15.4 TAP ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    RadioSniffGlobalHeaderArgs global_header_args;
    RadioSniffI2f32Args i2f32_args;
    RadioSniffTapRecordArgs tap_record_args;
    proto_bool ok;
    size_t n;
    uint32_t u32;
} RadioSniffVars;

/** @brief The operands and the outcome. */
extern RadioSniffVars RadioSniffV;

/** @brief The entries. */
typedef struct
{
    void (*const global_header)(uint8_t *restrict work);
    void (*const i2f32)(uint8_t *restrict work);
    void (*const tap_record)(uint8_t *restrict work);
} RadioSniffNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in RadioSniffV or a region of the borrow at a fixed offset.
void protocore_radio_sniff_global_header(uint8_t *restrict work);
void protocore_radio_sniff_i2f32(uint8_t *restrict work);
void protocore_radio_sniff_tap_record(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `RadioSniff.global_header(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const RadioSniffNs RadioSniff __attribute__((unused)) = {
    .global_header = protocore_radio_sniff_global_header,
    .i2f32 = protocore_radio_sniff_i2f32,
    .tap_record = protocore_radio_sniff_tap_record,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RADIO_SNIFF

#endif // PROTOCORE_RADIO_SNIFF_H
