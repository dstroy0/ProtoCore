// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file daq_protocol.h
 * @brief The binary wire format between the reverse_engineering firmware and the Python
 *        analysis engine (dpa_cpa_network_engine.py) - one struct, mirrored exactly by the
 *        Python side's `struct.Struct("<4sHBBIIHBBffffIIQHH")`.
 *
 * Binary, memcpy-framed, no text formatting anywhere in the per-window hot path (an
 * snprintf-built header would burn 3x the cycles a raw struct copy does - the packet is
 * built once, per completed window, not per sample, but the DMA-chunk copy upstream of it
 * runs at the sample rate, so the whole path stays snprintf-free end to end). Every window
 * is fully self-describing (sample width, trigger split, per-sample physical scaling) so
 * the Python side never hardcodes a front end's assumptions - a SCPI oscilloscope pull and
 * a raw ADC/DMA capture look identical on the wire past this header.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef REVERSE_ENGINEERING_DAQ_PROTOCOL_H
#define REVERSE_ENGINEERING_DAQ_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define DAQ_PROTO_VERSION 2 // v2 added wall_clock_us (48 -> 56-byte header)

/**
 * @brief Which front end produced a window - carried on the wire so the receiver can tell,
 *        and #define'd (not enum'd) so platformio.ini can also select main.cpp's compiled-in
 *        front end with `-DDAQ_FRONTEND=DAQ_FRONTEND_SCPI_SCOPE` (a preprocessor #if needs a
 *        macro, not an enumerator).
 */
#define DAQ_FRONTEND_ADC_DMA 0 ///< raw ADC (e.g. AD9226/AD9238-class) drained via services/system/dma + trace_capture
#define DAQ_FRONTEND_SCPI_SCOPE 1 ///< a real bench oscilloscope's :WAVeform:DATA? pulled over SCPI

enum : uint8_t
{
    DAQ_MSG_WINDOW = 1,   ///< one completed sample window follows the header
    DAQ_MSG_HEARTBEAT = 2 ///< no payload - liveness + telemetry only
};

#pragma pack(push, 1)
/**
 * @brief Fixed 56-byte packet header. Little-endian (native on ESP32/Xtensa and on every
 *        Python host struct.Struct("<...") targets), no padding (`pack(1)`).
 */
struct DaqPacketHeader
{
    char magic[4];               ///< "DAQ1"
    uint16_t version;            ///< DAQ_PROTO_VERSION
    uint8_t msg_type;            ///< DAQ_MSG_WINDOW / DAQ_MSG_HEARTBEAT
    uint8_t frontend;            ///< DAQ_FRONTEND_ADC_DMA / DAQ_FRONTEND_SCPI_SCOPE
    uint32_t trace_id;           ///< monotonic capture sequence (wraps)
    uint32_t n_samples;          ///< samples in the payload (per channel)
    uint16_t pretrigger_samples; ///< how many of n_samples precede the trigger instant
    uint8_t sample_bytes;        ///< bytes per sample (1 or 2)
    uint8_t channel_count;       ///< interleaved channels per sample slot (1, or 2 for AD9238 dual)
    float x_increment_s;  ///< seconds/sample from the *source* (scope :WAV:XINC?); 0 if sample_rate_hz applies instead
    float sample_rate_hz; ///< the ADC/DMA front end's fixed sample rate; 0 if x_increment_s applies instead
    float y_increment;    ///< volts/code (scope :WAV:YINC?, or the ADC engine's v_ref / full-scale-codes)
    float y_origin;       ///< volts at code 0 after y_increment scaling (scope :WAV:YOR?/:YREF?; 0 for a raw ADC)
    uint32_t windows_dropped; ///< triggers_dropped + samples_dropped telemetry since link-up
    uint32_t assembly_ns;     ///< trigger-to-window latency (pc_cycles_to_ns); 0 when not applicable
    uint64_t wall_clock_us;   ///< Unix epoch microseconds this window's trigger landed at (NTP/GNSS-anchored;
                              ///< see main.cpp's wall_clock_us_now()); 0 if no time source has synced yet
    uint16_t payload_len;     ///< bytes following this header (n_samples * sample_bytes * channel_count)
    uint16_t header_crc16;    ///< CRC16/CCITT-FALSE over every byte above (this field itself = 0 during calc)
};
#pragma pack(pop)

#define DAQ_MAGIC "DAQ1"
#define DAQ_HEADER_SIZE ((uint16_t)sizeof(DaqPacketHeader))

/**
 * @brief On-wire packet layout: [DaqPacketHeader][payload_len bytes of samples][uint16_t
 *        payload_crc16, little-endian]. header_crc16 covers only the header (computed with
 *        that field itself zeroed); payload_crc16 covers only the payload - two independently
 *        checkable pieces, so a receiver can reject a corrupt header before it even trusts
 *        payload_len enough to size a read.
 */
#define DAQ_TRAILER_SIZE 2u

/**
 * @brief CRC16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, no xorout) - the same
 *        framing checksum used elsewhere in embedded serial protocols. Bytewise (no table):
 *        this runs once per packet (header, then again over the payload), never per sample,
 *        so the O(n) bit-loop cost is negligible next to the network write it guards.
 */
inline uint16_t daq_crc16(const uint8_t *data, size_t len, uint16_t crc = 0xFFFF)
{
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

#endif // REVERSE_ENGINEERING_DAQ_PROTOCOL_H
