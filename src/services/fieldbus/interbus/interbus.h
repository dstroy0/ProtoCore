// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file interbus.h
 * @brief INTERBUS summation-frame fieldbus codec (PC_ENABLE_INTERBUS).
 *
 * INTERBUS (Phoenix Contact) is a ring fieldbus with a distinctive **summation frame**: instead of
 * addressing each device, one frame circulates the whole ring and every device is a shift-register slice
 * of it - the master clocks the frame around, each device reads its input slot and writes its output
 * slot as the bits pass through. A cycle frame is:
 *
 *   [loopback word : 2][device data words...][FCS : 2 (CRC-16/CCITT)]
 *
 * The loopback word (0xFFFF -> 0x0000) detects the ring is closed; each device slice is a fixed number of
 * 16-bit words (its process image). This codec assembles the summation frame from a list of per-device
 * word slices and disassembles a received frame back into those slices, plus the CRC. The physical ring
 * (the shift-register clocking) is hardware-gated; this is the summation-frame + process-image layer.
 * Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_INTERBUS_H
#define PROTOCORE_INTERBUS_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_INTERBUS

/** @brief the loopback word that opens a summation frame. */
#define PC_INTERBUS_LOOPBACK 0xFFFF

/** @brief CRC-16/CCITT-FALSE (the INTERBUS FCS) over @p len bytes. */
uint16_t pc_interbus_fcs(const uint8_t *bytes, size_t len);

/**
 * @brief Assemble a summation frame from per-device 16-bit word slices.
 * @param words     the concatenated device data words (big-endian on the wire).
 * @param word_count number of 16-bit words across all device slices.
 * @param out       output byte buffer.
 * @param cap       its capacity.
 * @return the frame length (2 + word_count*2 + 2), or 0 on overflow.
 *
 * Layout: loopback(2) + words(word_count*2, big-endian) + FCS(2). The FCS covers loopback + words.
 */
size_t pc_interbus_build(const uint16_t *words, size_t word_count, uint8_t *out, size_t cap);

/**
 * @brief Disassemble a summation frame back into device data words.
 * @param frame     the received frame.
 * @param len       its length.
 * @param out_words buffer for the decoded 16-bit words.
 * @param max_words its capacity (in words).
 * @param out_count set to the number of words decoded.
 * @return true if the loopback word + FCS are valid and the words fit @p max_words.
 */
bool pc_interbus_parse(const uint8_t *frame, size_t len, uint16_t *out_words, size_t max_words, size_t *out_count);

#endif // PC_ENABLE_INTERBUS
#endif // PROTOCORE_INTERBUS_H
