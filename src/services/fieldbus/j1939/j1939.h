// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file j1939.h
 * @brief SAE J1939 message codec (PC_ENABLE_J1939) - the heavy-duty-vehicle / agriculture /
 *        marine / genset CAN higher-layer protocol, over 29-bit extended CAN frames.
 *
 * J1939 packs a 29-bit extended identifier as:
 * @code
 *   bits 28-26 Priority | 25 EDP | 24 DP | 23-16 PF | 15-8 PS | 7-0 SA
 * @endcode
 * The 18-bit Parameter Group Number (PGN) is EDP|DP|PF|PS, where PS is part of the PGN only
 * for PDU2 (broadcast, PF >= 240); for PDU1 (PF < 240) PS is the destination address (DA) and
 * the PGN's low octet is 0. This codec encodes / decodes that id, builds single-frame
 * messages, runs the Transport Protocol (BAM broadcast + RTS/CTS connection mode) with a
 * reassembler for messages up to `PC_J1939_TP_MAX` octets, and builds the Address Claimed
 * (with a 64-bit NAME) and Request PGN messages.
 *
 * Pure and host-tested. Drive it from the ESP32 TWAI peripheral (or an MCP2515 over SPI) to
 * bridge a J1939 bus onto Wi-Fi - decode engine / transmission / genset PGNs and publish them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_J1939_H
#define PROTOCORE_J1939_H

#include "protocore_config.h"

#if PC_ENABLE_J1939

#include "shared_primitives/can.h"
#include <stddef.h>
#include <stdint.h>

// Well-known PGNs and addresses.
#define J1939_PGN_TP_CM 0x00EC00u         ///< Transport Protocol - Connection Management (60416)
#define J1939_PGN_TP_DT 0x00EB00u         ///< Transport Protocol - Data Transfer (60160)
#define J1939_PGN_ADDRESS_CLAIM 0x00EE00u ///< Address Claimed / Cannot Claim (60928)
#define J1939_PGN_REQUEST 0x00EA00u       ///< Request PGN (59904)
#define J1939_ADDR_GLOBAL 0xFFu           ///< broadcast destination address
#define J1939_ADDR_NULL 0xFEu             ///< null / unclaimed source address
#define J1939_PDU2_THRESHOLD 240u         ///< PF >= 240 is PDU2 (broadcast); < 240 is PDU1 (peer)

// TP.CM control bytes (data[0] of a TP.CM frame).
#define J1939_TP_CM_RTS 0x10u     ///< Request To Send (connection mode)
#define J1939_TP_CM_CTS 0x11u     ///< Clear To Send
#define J1939_TP_CM_EOM_ACK 0x13u ///< End Of Message Acknowledge
#define J1939_TP_CM_BAM 0x20u     ///< Broadcast Announce Message
#define J1939_TP_CM_ABORT 0xFFu   ///< Connection Abort

#define J1939_TP_DT_LEN 7u ///< data octets carried per TP.DT packet (1 seq byte + 7 data)

/** @brief A decoded J1939 identifier. */
struct J1939Id
{
    uint8_t priority; ///< 0 (highest) .. 7
    uint32_t pgn;     ///< 18-bit Parameter Group Number
    uint8_t sa;       ///< source address
    uint8_t da;       ///< destination address (PDU1), or J1939_ADDR_GLOBAL (PDU2)
    uint8_t pf;       ///< PDU format
    uint8_t ps;       ///< PDU specific (DA for PDU1, group extension for PDU2)
    bool pdu1;        ///< true => peer-to-peer (PF < 240); false => broadcast
};

/** @brief Result of feeding a frame to the TP reassembler. */
enum class J1939TpResult : uint8_t
{
    J1939_TP_IGNORED = 0, ///< not a TP frame for the active session
    J1939_TP_STARTED,     ///< a BAM / RTS opened a session
    J1939_TP_PROGRESS,    ///< a data packet was accepted, more to come
    J1939_TP_COMPLETE,    ///< the message is fully reassembled (see fields below)
    J1939_TP_ERROR,       ///< malformed / out-of-sequence / too large
};

/** @brief Transport-Protocol reassembly context (one in-flight message). */
struct J1939TpRx
{
    bool active;
    uint8_t sa;          ///< source of the session
    uint32_t pgn;        ///< the transported PGN
    uint16_t total_size; ///< announced message size
    uint8_t num_packets; ///< announced packet count
    uint8_t next_seq;    ///< next expected sequence number (1-based)
    uint16_t received;   ///< octets stored so far
    uint8_t buf[PC_J1939_TP_MAX];
};

// --- identifier ---

/** @brief Encode a 29-bit J1939 id. @p da is used only for a PDU1 (PF < 240) PGN. */
bool pc_j1939_encode_id(uint32_t *id, uint8_t priority, uint32_t pgn, uint8_t sa, uint8_t da);

/** @brief Decode a 29-bit J1939 id into its fields. */
bool pc_j1939_decode_id(uint32_t id, J1939Id *out);

// --- single-frame messages ---

/** @brief Build a single-frame (<= 8 octet) J1939 message. */
bool pc_j1939_build_message(CanFrame *out, uint8_t priority, uint32_t pgn, uint8_t sa, uint8_t da, const uint8_t *data,
                            uint8_t len);

/** @brief Build a Request-PGN frame asking @p da for @p requested_pgn. */
bool pc_j1939_build_request(CanFrame *out, uint8_t sa, uint8_t da, uint32_t requested_pgn);

/** @brief Build an Address-Claimed frame announcing @p sa with the 64-bit @p name. */
bool pc_j1939_build_address_claim(CanFrame *out, uint8_t sa, uint64_t name);

/** @brief Compose a 64-bit J1939 NAME from its fields (see J1939-81). */
uint64_t pc_j1939_build_name(bool arbitrary_address_capable, uint8_t industry_group, uint8_t vehicle_system_instance,
                             uint8_t vehicle_system, uint8_t function, uint8_t function_instance, uint8_t ecu_instance,
                             uint16_t manufacturer_code, uint32_t identity_number);

// --- transport protocol (multi-packet) ---

/** @brief Octet count -> TP packet count (ceil(size / 7)). */
uint8_t pc_j1939_tp_num_packets(uint16_t total_size);

/** @brief Build the BAM (broadcast) TP.CM announce frame for @p pgn / @p total_size. */
bool pc_j1939_build_bam_cm(CanFrame *out, uint8_t sa, uint32_t pgn, uint16_t total_size);

/** @brief Build TP.DT data packet @p seq (1-based) carrying @p chunk_len (1..7) octets. */
bool pc_j1939_build_tp_dt(CanFrame *out, uint8_t sa, uint8_t da, uint8_t seq, const uint8_t *chunk, uint8_t chunk_len);

/** @brief Reset a reassembly context to idle. */
void pc_j1939_tp_reset(J1939TpRx *rx);

/** @brief Feed a received frame to the reassembler; see @ref J1939TpResult. */
J1939TpResult pc_j1939_tp_feed(J1939TpRx *rx, const CanFrame *f);

// --- typed decoders for common engine PGNs (SAE J1939-71) ---
//
// These lift a raw single-frame CAN message into engineering units, applying the J1939 scale + offset and
// the "not available" ranges (a 1-octet SPN is valid 0x00..0xFA, a 2-octet SPN 0x0000..0xFAFF; the rest
// is error / not-available and clears the corresponding valid flag).

#define J1939_PGN_EEC1 0x00F004u ///< Electronic Engine Controller 1 (61444): engine speed + torque
#define J1939_PGN_ET1 0x00FEEEu  ///< Engine Temperature 1 (65262): coolant / fuel / oil temperature
#define J1939_PGN_LFE 0x00FEF2u  ///< Fuel Economy (65266): fuel rate + instantaneous / average economy
#define J1939_PGN_AMB 0x00FEF5u  ///< Ambient Conditions (65269): barometric pressure + air / road temperatures
#define J1939_PGN_IC1 0x00FEF6u  ///< Inlet/Exhaust Conditions 1 (65270): boost + intake / exhaust + filter pressures
#define J1939_PGN_VD 0x00FEE0u   ///< Vehicle Distance (65248): trip + total vehicle distance
#define J1939_PGN_CCVS 0x00FEF1u ///< Cruise Control/Vehicle Speed (65265): wheel-based vehicle speed + cruise state
#define J1939_PGN_DM1 0x00FECAu  ///< Active Diagnostic Trouble Codes (65226): lamp status + DTC list

/** @brief Decoded EEC1 (PGN 61444). Percent-torque fields are @ref J1939_TORQUE_NA when not available. */
struct J1939Eec1
{
    uint8_t torque_mode;               ///< engine torque mode (data[0] low nibble)
    int16_t drivers_demand_torque_pct; ///< driver's demand percent torque (-125..125), or J1939_TORQUE_NA
    int16_t actual_engine_torque_pct;  ///< actual engine percent torque (-125..125), or J1939_TORQUE_NA
    bool engine_speed_valid;           ///< false when the raw speed is in the not-available range
    float engine_speed_rpm;            ///< engine speed (rpm, 0.125 rpm/bit)
};

/** @brief Sentinel percent-torque value meaning "not available". */
#define J1939_TORQUE_NA ((int16_t)0x7FFF)

/** @brief Decoded ET1 (PGN 65262). Each temperature has its own validity flag. */
struct J1939Et1
{
    bool coolant_valid;
    float coolant_temp_c; ///< engine coolant temperature (degC, 1 degC/bit, -40 offset)
    bool fuel_valid;
    float fuel_temp_c; ///< fuel temperature (degC, 1 degC/bit, -40 offset)
    bool oil_valid;
    float oil_temp_c; ///< engine oil temperature (degC, 0.03125 degC/bit, -273 offset)
};

/**
 * @brief Decode an EEC1 (PGN 61444) single frame into @p out.
 * @return true iff @p f decodes to PGN 61444 and carries 8 data octets; false otherwise.
 */
bool pc_j1939_decode_eec1(const CanFrame *f, J1939Eec1 *out);

/**
 * @brief Decode an ET1 (PGN 65262) single frame into @p out.
 * @return true iff @p f decodes to PGN 65262 and carries 8 data octets; false otherwise.
 */
bool pc_j1939_decode_et1(const CanFrame *f, J1939Et1 *out);

/** @brief Decoded LFE (PGN 65266). Each value has its own validity flag (cleared for a not-available raw). */
struct J1939Lfe
{
    bool fuel_rate_valid;
    float fuel_rate_lph; ///< engine fuel rate (L/h, 0.05 L/h per bit)
    bool instant_econ_valid;
    float instant_econ_kmpl; ///< instantaneous fuel economy (km/L, 1/512 km/L per bit)
    bool avg_econ_valid;
    float avg_econ_kmpl; ///< average fuel economy (km/L, 1/512 km/L per bit)
    bool throttle_valid;
    float throttle_pct; ///< throttle valve 1 position (percent, 0.4 %/bit)
};

/**
 * @brief Decode an LFE (PGN 65266) single frame into @p out.
 * @return true iff @p f decodes to PGN 65266 and carries 8 data octets; false otherwise.
 */
bool pc_j1939_decode_lfe(const CanFrame *f, J1939Lfe *out);

/** @brief Decoded AMB (PGN 65269). Each measurement has its own validity flag (cleared for a
 *  not-available raw). Barometric pressure is a 1-octet SPN; the temperatures are 2-octet except the
 *  air inlet, which is a 1-octet SPN. */
struct J1939Amb
{
    bool baro_valid;
    float baro_kpa; ///< barometric pressure (kPa, 0.5 kPa/bit) - SPN 108
    bool cab_temp_valid;
    float cab_temp_c; ///< cab interior temperature (degC, 0.03125 degC/bit, -273 offset) - SPN 170
    bool ambient_temp_valid;
    float ambient_temp_c; ///< ambient air temperature (degC, 0.03125 degC/bit, -273 offset) - SPN 171
    bool inlet_temp_valid;
    float inlet_temp_c; ///< engine air inlet temperature (degC, 1 degC/bit, -40 offset) - SPN 172
    bool road_temp_valid;
    float road_temp_c; ///< road surface temperature (degC, 0.03125 degC/bit, -273 offset) - SPN 79
};

/**
 * @brief Decode an AMB (PGN 65269) single frame into @p out.
 * @return true iff @p f decodes to PGN 65269 and carries 8 data octets; false otherwise.
 */
bool pc_j1939_decode_amb(const CanFrame *f, J1939Amb *out);

/** @brief Decoded IC1 (PGN 65270). Each measurement has its own validity flag (cleared for a
 *  not-available raw). Exhaust gas temperature is a 2-octet SPN; the rest are 1-octet. */
struct J1939Ic1
{
    bool trap_inlet_valid;
    float trap_inlet_kpa; ///< particulate trap inlet pressure (kPa, 0.5 kPa/bit) - SPN 81
    bool boost_valid;
    float boost_kpa; ///< boost pressure (kPa, 2 kPa/bit) - SPN 102
    bool intake_temp_valid;
    float intake_temp_c; ///< intake manifold 1 temperature (degC, 1 degC/bit, -40 offset) - SPN 105
    bool air_inlet_valid;
    float air_inlet_kpa; ///< air inlet pressure (kPa, 2 kPa/bit) - SPN 106
    bool air_filter_valid;
    float air_filter_kpa; ///< air filter 1 differential pressure (kPa, 0.05 kPa/bit) - SPN 107
    bool exhaust_temp_valid;
    float exhaust_temp_c; ///< exhaust gas temperature (degC, 0.03125 degC/bit, -273 offset) - SPN 173
    bool coolant_filter_valid;
    float coolant_filter_kpa; ///< coolant filter differential pressure (kPa, 0.5 kPa/bit) - SPN 112
};

/**
 * @brief Decode an IC1 (PGN 65270) single frame into @p out.
 * @return true iff @p f decodes to PGN 65270 and carries 8 data octets; false otherwise.
 */
bool pc_j1939_decode_ic1(const CanFrame *f, J1939Ic1 *out);

/** @brief Decoded VD (PGN 65248). The distances are held as double: at 0.125 km/bit a 32-bit odometer
 *  spans hundreds of millions of km, beyond float's ~7-digit precision. */
struct J1939Vd
{
    bool trip_valid;
    double trip_km; ///< trip distance (km, 0.125 km/bit) - SPN 244
    bool total_valid;
    double total_km; ///< total vehicle distance (km, 0.125 km/bit) - SPN 245
};

/**
 * @brief Decode a VD (PGN 65248) single frame into @p out.
 * @return true iff @p f decodes to PGN 65248 and carries 8 data octets; false otherwise.
 */
bool pc_j1939_decode_vd(const CanFrame *f, J1939Vd *out);

/** @brief Decoded CCVS (PGN 65265): the wheel-based vehicle speed plus the cruise-control-active state.
 *  Only the two signals with cross-source-verified positions are decoded; the many discrete switches in
 *  this PGN are left to the caller (their bit positions vary between vendor definitions). */
struct J1939Ccvs
{
    bool speed_valid;      ///< false when the raw wheel-based speed is in the not-available range
    float wheel_speed_kmh; ///< wheel-based vehicle speed (km/h, 1/256 km/h per bit) - SPN 84
    uint8_t
        cruise_active; ///< cruise control active state, a 2-bit value (0 off / 1 active / 2 error / 3 n/a) - SPN 595
};

/**
 * @brief Decode a CCVS (PGN 65265) single frame into @p out.
 * @return true iff @p f decodes to PGN 65265 and carries 8 data octets; false otherwise.
 */
bool pc_j1939_decode_ccvs(const CanFrame *f, J1939Ccvs *out);

/** @brief One decoded Diagnostic Trouble Code (J1939-73 SPN conversion method 4). */
struct J1939Dtc
{
    uint32_t spn; ///< suspect parameter number (19-bit)
    uint8_t fmi;  ///< failure mode identifier (5-bit)
    uint8_t cm;   ///< SPN conversion method (1-bit)
    uint8_t oc;   ///< occurrence count (7-bit)
};

/** @brief Decoded DM1 lamp status (each field 0 = off, 1 = on; 2/3 reserved / not available). */
struct J1939Dm1
{
    uint8_t mil;           ///< malfunction indicator lamp
    uint8_t red_stop;      ///< red stop lamp
    uint8_t amber_warning; ///< amber warning lamp
    uint8_t protect;       ///< protect lamp
    uint8_t dtc_count;     ///< number of active DTCs decoded into the caller's array
};

/**
 * @brief Decode a DM1 (PGN 65226) body: the lamp-status octet, the flash-status octet, then 4-octet DTCs.
 *        DM1 may arrive single-frame or reassembled over the Transport Protocol, so this takes the raw body
 *        (a frame's data[] or a TP buffer). An all-zero DTC (the "no active fault" placeholder) is skipped.
 * @param out_dtcs  caller array receiving up to @p max decoded DTCs (may be null to only read the lamps).
 * @return true iff @p len is at least 2 octets (the two status octets); false otherwise.
 */
bool pc_j1939_decode_dm1(const uint8_t *body, size_t len, J1939Dm1 *out, J1939Dtc *out_dtcs, size_t max);

#endif // PC_ENABLE_J1939
#endif // PROTOCORE_J1939_H
