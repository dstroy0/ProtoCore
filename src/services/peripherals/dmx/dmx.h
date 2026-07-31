// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dmx.h
 * @brief DMX512 framing + RDM (ANSI E1.20) management codec (PC_ENABLE_DMX).
 *
 * DMX512 (lighting / stage control over RS-485) is positional: after a break, a start code
 * octet (0x00 for dimmer data) is followed by up to 512 channel slots, with no checksum or
 * in-frame addressing. This codec assembles / reads that slot array, and implements **RDM**
 * (Remote Device Management, ANSI E1.20) - the addressed management layer that shares the
 * DMX wire: a real packet with 48-bit source / destination UIDs, a command class + parameter
 * id, and a 16-bit additive checksum.
 *
 * The break + RS-485 direction are the application's (a `MAX485`-class transceiver on a UART
 * at 250 kbit/s, 8N2). This is the byte-level framing layer. Pure and host-tested. Bridge a
 * lighting rig onto Wi-Fi: drive DMX slots or discover / configure RDM fixtures from the web.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DMX_H
#define PROTOCORE_DMX_H

#include "protocore_config.h"

#if PC_ENABLE_DMX

#include <stddef.h>
#include <stdint.h>

#define DMX_MAX_CHANNELS 512u ///< slots per DMX512 universe
#define DMX_SC_DIMMER 0x00u   ///< start code for standard dimmer data

#define RDM_SC 0xCCu     ///< RDM start code (SC_RDM)
#define RDM_SUB_SC 0x01u ///< RDM sub-start code (SC_SUB_MESSAGE)
#define RDM_OVERHEAD 26u ///< full packet octets with PDL 0 (24-octet message + 2 checksum)

// RDM command classes.
#define RDM_CC_DISCOVERY 0x10u
#define RDM_CC_DISCOVERY_RESPONSE 0x11u
#define RDM_CC_GET 0x20u
#define RDM_CC_GET_RESPONSE 0x21u
#define RDM_CC_SET 0x30u
#define RDM_CC_SET_RESPONSE 0x31u

// RDM response types (carried in the port-id / response-type octet of a response).
#define RDM_RESPONSE_ACK 0x00u
#define RDM_RESPONSE_ACK_TIMER 0x01u
#define RDM_RESPONSE_NACK_REASON 0x02u
#define RDM_RESPONSE_ACK_OVERFLOW 0x03u

// A few common RDM parameter ids (PIDs).
#define RDM_PID_DISC_UNIQUE_BRANCH 0x0001u
#define RDM_PID_DISC_MUTE 0x0002u
#define RDM_PID_DISC_UN_MUTE 0x0003u
#define RDM_PID_SUPPORTED_PARAMETERS 0x0050u
#define RDM_PID_DEVICE_INFO 0x0060u
#define RDM_PID_DMX_START_ADDRESS 0x00F0u
#define RDM_PID_IDENTIFY_DEVICE 0x1000u

// --- DMX512 ---

/**
 * @brief Assemble a DMX512 packet body: [start code][channel slots]. @p n <= 512.
 * Returns the byte count (1 + n) or 0 on overflow. The break is the transport's job.
 */
size_t pc_dmx_build(uint8_t *buf, size_t cap, uint8_t start_code, const uint8_t *channels, uint16_t n);

/**
 * @brief Read channel @p ch (1-based, per DMX convention) from a received packet body.
 * Returns the slot value, or 0 if @p ch is out of range / not present.
 */
uint8_t pc_dmx_get_channel(const uint8_t *buf, size_t len, uint16_t ch);

// --- RDM (ANSI E1.20) ---

/** @brief A parsed / to-be-built RDM packet. UIDs are 48-bit (manufacturer<<32 | device). */
struct RdmPacket
{
    uint64_t dest_uid;
    uint64_t src_uid;
    uint8_t tn;           ///< transaction number
    uint8_t port_id;      ///< port id (request) / response type (response)
    uint8_t msg_count;    ///< queued message count
    uint16_t sub_device;  ///< sub-device (0 = root)
    uint8_t cc;           ///< command class (RDM_CC_*)
    uint16_t pid;         ///< parameter id
    uint8_t pdl;          ///< parameter data length
    const uint8_t *pdata; ///< parameter data (points into the parsed buffer); nullptr when pdl 0
};

/** @brief Compose a 48-bit RDM UID from a manufacturer id and a device id. */
uint64_t pc_rdm_uid(uint16_t manufacturer, uint32_t device);

/** @brief 16-bit additive checksum over @p len octets (RDM message block). */
uint16_t pc_rdm_checksum(const uint8_t *buf, size_t len);

/**
 * @brief Build a full RDM packet (incl. the trailing 16-bit checksum) from @p p and its
 * parameter data. Returns the total length (26 + pdl) or 0 on overflow.
 */
size_t pc_rdm_build(uint8_t *buf, size_t cap, const RdmPacket *p, const uint8_t *pdata, uint8_t pdl);

/**
 * @brief Parse an RDM packet: validates the start codes, the message length vs PDL, and the
 * checksum. Fills @p out and @p consumed (the whole packet length).
 */
bool pc_rdm_parse(const uint8_t *buf, size_t len, RdmPacket *out, size_t *consumed);

/**
 * @brief Decode a DISC_UNIQUE_BRANCH discovery response into the responder's 48-bit UID. This reply is not a
 *        normal RDM packet: it is an optional 0xFE preamble (0..7 octets) + a 0xAA separator, then the 6 UID
 *        octets each sent as two copies OR'd with 0xAA / 0x55, then a 2-octet checksum sent the same way.
 *        Each original octet is recovered as the AND of its two encoded copies, and the checksum (the 16-bit
 *        additive sum of the 12 encoded UID octets) is verified.
 * @return true iff the separator is present, the 16 encoded octets fit, and the checksum matches.
 */
bool pc_rdm_decode_disc_response(const uint8_t *buf, size_t len, uint64_t *uid);

/**
 * @brief Build the DISC_UNIQUE_BRANCH discovery response a responder sends for its 48-bit @p uid (the
 *        complement of pc_rdm_decode_disc_response): @p preamble_len octets of 0xFE (0..7) + the 0xAA
 *        separator + the 6 UID octets each as two copies OR'd with 0xAA / 0x55 + the 2-octet checksum (the
 *        16-bit additive sum of the 12 encoded UID octets) sent the same way.
 * @return octets written (@p preamble_len + 17), or 0 on a null buffer, @p preamble_len > 7, or overflow.
 */
size_t pc_rdm_build_disc_response(uint8_t *buf, size_t cap, uint64_t uid, uint8_t preamble_len);

#define PC_RDM_DEVICE_INFO_PDL 19 ///< octets in a DEVICE_INFO (PID 0x0060) GET-response parameter block

/** @brief Decoded DEVICE_INFO (PID 0x0060) parameter data - the descriptor every RDM responder must
 *  answer, carrying the fields a controller needs to patch and identify the device. */
struct RdmDeviceInfo
{
    uint8_t proto_major;          ///< RDM protocol version major (1 for E1.20)
    uint8_t proto_minor;          ///< RDM protocol version minor
    uint16_t device_model_id;     ///< manufacturer-specific device model id
    uint16_t product_category;    ///< E1.20 product category code
    uint32_t software_version_id; ///< manufacturer-specific software version id
    uint16_t dmx_footprint;       ///< number of DMX512 slots the current personality occupies
    uint8_t current_personality;  ///< current DMX personality (1-based)
    uint8_t personality_count;    ///< total number of DMX personalities
    uint16_t dmx_start_address;   ///< DMX512 start address (1-512; 0xFFFF if the device uses no DMX)
    uint16_t sub_device_count;    ///< number of sub-devices (0 = none)
    uint8_t sensor_count;         ///< number of sensors
};

/**
 * @brief Pack a DEVICE_INFO (PID 0x0060) GET-response parameter block from @p info into @p pdata: the
 *        19-octet big-endian descriptor (protocol version, device model, product category, software
 *        version, DMX footprint / personality / start address, sub-device and sensor counts). Hand the
 *        result to pc_rdm_build as the pdata of a GET-response with pid RDM_PID_DEVICE_INFO.
 * @return PC_RDM_DEVICE_INFO_PDL (19), or 0 on a null argument or @p cap < 19.
 */
size_t pc_rdm_build_device_info(uint8_t *pdata, size_t cap, const RdmDeviceInfo *info);

/**
 * @brief Decode a DEVICE_INFO (PID 0x0060) GET-response parameter block into @p out (the complement of
 *        pc_rdm_build_device_info).
 * @return true iff @p pdl is at least 19 octets; false on a null argument or a short block.
 */
bool pc_rdm_parse_device_info(const uint8_t *pdata, uint8_t pdl, RdmDeviceInfo *out);

#endif // PC_ENABLE_DMX
#endif // PROTOCORE_DMX_H
