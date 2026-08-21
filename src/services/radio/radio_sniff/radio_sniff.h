// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_RADIO_SNIFF_H
#define PROTOCORE_RADIO_SNIFF_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */

// PROTOCORE_RADIO_SNIFF_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief The TAP pseudo-header length this codec emits: 4 header + RSSI TLV(8) + channel TLV(8). */
#define RADIO_SNIFF_TAP_LEN 20

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*global_header)(uint8_t *restrict, uint8_t *, size_t);
    uint32_t (*i2f32)(uint8_t *restrict, int32_t);
    size_t (*tap_record)(uint8_t *restrict, uint8_t *, size_t, const uint8_t *, size_t, int32_t, uint16_t, uint32_t,
                         uint32_t);
} RadioSniffNs;
PROTOCORE_NS_LAYOUT(RadioSniffNs, global_header, i2f32, tap_record);

/**
 * @brief Write the pcap global header for a TAP-framed 802.15.4 sniff. 24, .
 * @param work PROTOCORE_RADIO_SNIFF_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_radio_sniff_global_header(uint8_t *restrict work, uint8_t *out, size_t cap);
/**
 * @brief Encode a signed integer dBm value as an IEEE-754 float32 .
 * @param work PROTOCORE_RADIO_SNIFF_BORROW bytes the caller took. Not held past the call.
 * @param dbm Dbm
 * @return The uint32_t.
 */
uint32_t protocore_radio_sniff_i2f32(uint8_t *restrict work, int32_t dbm);
/**
 * @brief Write one capture record: a pcap record header, the 802.15.4 TAP .
 * @param work PROTOCORE_RADIO_SNIFF_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param frame Frame
 * @param flen Flen
 * @param rssi_dbm Rssi dbm
 * @param channel Channel
 * @param ts_sec Ts sec
 * @param ts_usec Ts usec
 * @return The size_t.
 */
size_t protocore_radio_sniff_tap_record(uint8_t *restrict work, uint8_t *out, size_t cap, const uint8_t *frame,
                                        size_t flen, int32_t rssi_dbm, uint16_t channel, uint32_t ts_sec,
                                        uint32_t ts_usec);

/** @brief Module namespace. */
PROTOCORE_NS RadioSniffNs RadioSniff PROTOCORE_UNUSED = {.global_header = protocore_radio_sniff_global_header,
                                                         .i2f32 = protocore_radio_sniff_i2f32,
                                                         .tap_record = protocore_radio_sniff_tap_record};

PROTOCORE_END_DECLS

#endif // PROTOCORE_RADIO_SNIFF_H
