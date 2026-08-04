// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_sniff.h
 * @brief Receive-only radio channel sniffer -> pcap capture records (PC_ENABLE_RADIO_SNIFF).
 *
 * The RF gateway drivers (CC1101, LoRa, the Thread 802.15.4 RCP) can run receive-only - sniff a channel
 * without joining - and the frames they pull off the air belong in the same capture pipeline as the CAN
 * and Wi-Fi captures (shared_primitives/pc_pcap). For 802.15.4 that means wrapping each frame in the
 * Wireshark IEEE 802.15.4 **TAP** pseudo-header so the per-frame RSSI and channel travel with it, then a
 * standard pcap record, giving a `.pcap` a wired Wireshark opens with the radio metadata intact.
 *
 * This is that pure framing: ::pc_radiosniff_global writes the pcap global header (DLT TAP), and
 * ::pc_radiosniff_tap_record writes one record (TAP header + RSSI/channel TLVs + the MAC frame). The
 * radio drivers own the receive; this owns the on-wire capture bytes. No heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_RADIO_SNIFF_H
#define PROTOCORE_RADIO_SNIFF_H

#include "protocore_config.h"
#include "shared_primitives/pcap.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_RADIO_SNIFF

/** @brief The TAP pseudo-header length this codec emits: 4 header + RSSI TLV(8) + channel TLV(8). */
#define RADIO_SNIFF_TAP_LEN 20

/** @brief Write the pcap global header for a TAP-framed 802.15.4 sniff. @return 24, or 0 on overflow. */
size_t pc_radiosniff_global(uint8_t *out, size_t cap);

/**
 * @brief Encode a signed integer dBm value as an IEEE-754 float32 (little-endian bit pattern).
 *
 * The 802.15.4 TAP RSSI TLV carries a float; RSSI here is an integer dBm, so this builds the exact
 * float bits by hand (e.g. -40 -> 0xC2200000). Exposed for testing.
 */
uint32_t pc_radiosniff_i2f32(int32_t dbm);

/**
 * @brief Write one capture record: a pcap record header, the 802.15.4 TAP pseudo-header carrying @p
 *        rssi_dbm and @p channel, then the raw MAC @p frame.
 * @return total bytes written, or 0 on overflow / bad args.
 */
size_t pc_radiosniff_tap_record(uint8_t *out, size_t cap, const uint8_t *frame, size_t flen, int32_t rssi_dbm,
                                uint16_t channel, uint32_t ts_sec, uint32_t ts_usec);

#endif // PC_ENABLE_RADIO_SNIFF

PROTO_END_DECLS

#endif // PROTOCORE_RADIO_SNIFF_H
