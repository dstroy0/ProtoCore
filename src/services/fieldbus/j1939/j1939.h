// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file j1939.h
 * @brief SAE J1939 message codec (PROTOCORE_ENABLE_J1939) - the heavy-duty-vehicle / agriculture /
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
 * reassembler for messages up to `PROTOCORE_J1939_TP_MAX` octets, and builds the Address Claimed
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_NEED_J1939

PROTOCORE_BEGIN_DECLS

// PROTOCORE_J1939_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

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

#define J1939_PGN_EEC1 0x00F004u ///< Electronic Engine Controller 1 (61444): engine speed + torque
#define J1939_PGN_ET1 0x00FEEEu  ///< Engine Temperature 1 (65262): coolant / fuel / oil temperature
#define J1939_PGN_LFE 0x00FEF2u  ///< Fuel Economy (65266): fuel rate + instantaneous / average economy
#define J1939_PGN_AMB 0x00FEF5u  ///< Ambient Conditions (65269): barometric pressure + air / road temperatures
#define J1939_PGN_IC1 0x00FEF6u  ///< Inlet/Exhaust Conditions 1 (65270): boost + intake / exhaust + filter pressures
#define J1939_PGN_VD 0x00FEE0u   ///< Vehicle Distance (65248): trip + total vehicle distance
#define J1939_PGN_CCVS 0x00FEF1u ///< Cruise Control/Vehicle Speed (65265): wheel-based vehicle speed + cruise state
#define J1939_PGN_DM1 0x00FECAu  ///< Active Diagnostic Trouble Codes (65226): lamp status + DTC list

/** @brief Sentinel percent-torque value meaning "not available". */
#define J1939_TORQUE_NA ((int16_t)0x7FFF)

/** @brief A decoded J1939 identifier. */
typedef struct
{
    uint8_t priority; ///< 0 (highest) .. 7
    uint32_t pgn;     ///< 18-bit Parameter Group Number
    uint8_t sa;       ///< source address
    uint8_t da;       ///< destination address (PDU1), or J1939_ADDR_GLOBAL (PDU2)
    uint8_t pf;       ///< PDU format
    uint8_t ps;       ///< PDU specific (DA for PDU1, group extension for PDU2)
    proto_bool pdu1;  ///< true => peer-to-peer (PF < 240); false => broadcast
} J1939Id;

/** @brief Result of feeding a frame to the TP reassembler. */
typedef enum PROTO_ENUM_PACKED
{
    J1939_TP_IGNORED = 0, ///< not a TP frame for the active session
    J1939_TP_STARTED,     ///< a BAM / RTS opened a session
    J1939_TP_PROGRESS,    ///< a data packet was accepted, more to come
    J1939_TP_COMPLETE,    ///< the message is fully reassembled (see fields below)
    J1939_TP_ERROR,       ///< malformed / out-of-sequence / too large
} J1939TpResult;

/** @brief Transport-Protocol reassembly context (one in-flight message). */
typedef struct
{
    proto_bool active;
    uint8_t sa;          ///< source of the session
    uint32_t pgn;        ///< the transported PGN
    uint16_t total_size; ///< announced message size
    uint8_t num_packets; ///< announced packet count
    uint8_t next_seq;    ///< next expected sequence number (1-based)
    uint16_t received;   ///< octets stored so far
    uint8_t buf[PROTOCORE_J1939_TP_MAX];
} J1939TpRx;

/** @brief Decoded EEC1 (PGN 61444). Percent-torque fields are @ref J1939_TORQUE_NA when not available. */
typedef struct
{
    uint8_t torque_mode;               ///< engine torque mode (data[0] low nibble)
    int16_t drivers_demand_torque_pct; ///< driver's demand percent torque (-125..125), or J1939_TORQUE_NA
    int16_t actual_engine_torque_pct;  ///< actual engine percent torque (-125..125), or J1939_TORQUE_NA
    proto_bool engine_speed_valid;     ///< false when the raw speed is in the not-available range
    float engine_speed_rpm;            ///< engine speed (rpm, 0.125 rpm/bit)
} J1939Eec1;

/** @brief Decoded ET1 (PGN 65262). Each temperature has its own validity flag. */
typedef struct
{
    proto_bool coolant_valid;
    float coolant_temp_c; ///< engine coolant temperature (degC, 1 degC/bit, -40 offset)
    proto_bool fuel_valid;
    float fuel_temp_c; ///< fuel temperature (degC, 1 degC/bit, -40 offset)
    proto_bool oil_valid;
    float oil_temp_c; ///< engine oil temperature (degC, 0.03125 degC/bit, -273 offset)
} J1939Et1;

/** @brief Decoded LFE (PGN 65266). Each value has its own validity flag (cleared for a not-available raw). */
typedef struct
{
    proto_bool fuel_rate_valid;
    float fuel_rate_lph; ///< engine fuel rate (L/h, 0.05 L/h per bit)
    proto_bool instant_econ_valid;
    float instant_econ_kmpl; ///< instantaneous fuel economy (km/L, 1/512 km/L per bit)
    proto_bool avg_econ_valid;
    float avg_econ_kmpl; ///< average fuel economy (km/L, 1/512 km/L per bit)
    proto_bool throttle_valid;
    float throttle_pct; ///< throttle valve 1 position (percent, 0.4 %/bit)
} J1939Lfe;

/** @brief Decoded AMB (PGN 65269). Each measurement has its own validity flag (cleared for a
 *  not-available raw). Barometric pressure is a 1-octet SPN; the temperatures are 2-octet except the
 *  air inlet, which is a 1-octet SPN. */
typedef struct
{
    proto_bool baro_valid;
    float baro_kpa; ///< barometric pressure (kPa, 0.5 kPa/bit) - SPN 108
    proto_bool cab_temp_valid;
    float cab_temp_c; ///< cab interior temperature (degC, 0.03125 degC/bit, -273 offset) - SPN 170
    proto_bool ambient_temp_valid;
    float ambient_temp_c; ///< ambient air temperature (degC, 0.03125 degC/bit, -273 offset) - SPN 171
    proto_bool inlet_temp_valid;
    float inlet_temp_c; ///< engine air inlet temperature (degC, 1 degC/bit, -40 offset) - SPN 172
    proto_bool road_temp_valid;
    float road_temp_c; ///< road surface temperature (degC, 0.03125 degC/bit, -273 offset) - SPN 79
} J1939Amb;

/** @brief Decoded IC1 (PGN 65270). Each measurement has its own validity flag (cleared for a
 *  not-available raw). Exhaust gas temperature is a 2-octet SPN; the rest are 1-octet. */
typedef struct
{
    proto_bool trap_inlet_valid;
    float trap_inlet_kpa; ///< particulate trap inlet pressure (kPa, 0.5 kPa/bit) - SPN 81
    proto_bool boost_valid;
    float boost_kpa; ///< boost pressure (kPa, 2 kPa/bit) - SPN 102
    proto_bool intake_temp_valid;
    float intake_temp_c; ///< intake manifold 1 temperature (degC, 1 degC/bit, -40 offset) - SPN 105
    proto_bool air_inlet_valid;
    float air_inlet_kpa; ///< air inlet pressure (kPa, 2 kPa/bit) - SPN 106
    proto_bool air_filter_valid;
    float air_filter_kpa; ///< air filter 1 differential pressure (kPa, 0.05 kPa/bit) - SPN 107
    proto_bool exhaust_temp_valid;
    float exhaust_temp_c; ///< exhaust gas temperature (degC, 0.03125 degC/bit, -273 offset) - SPN 173
    proto_bool coolant_filter_valid;
    float coolant_filter_kpa; ///< coolant filter differential pressure (kPa, 0.5 kPa/bit) - SPN 112
} J1939Ic1;

/** @brief Decoded VD (PGN 65248). The distances are held as double: at 0.125 km/bit a 32-bit odometer
 *  spans hundreds of millions of km, beyond float's ~7-digit precision. */
typedef struct
{
    proto_bool trip_valid;
    double trip_km; ///< trip distance (km, 0.125 km/bit) - SPN 244
    proto_bool total_valid;
    double total_km; ///< total vehicle distance (km, 0.125 km/bit) - SPN 245
} J1939Vd;

/** @brief Decoded CCVS (PGN 65265): the wheel-based vehicle speed plus the cruise-control-active state.
 *  Only the two signals with cross-source-verified positions are decoded; the many discrete switches in
 *  this PGN are left to the caller (their bit positions vary between vendor definitions). */
typedef struct
{
    proto_bool speed_valid; ///< false when the raw wheel-based speed is in the not-available range
    float wheel_speed_kmh;  ///< wheel-based vehicle speed (km/h, 1/256 km/h per bit) - SPN 84
    uint8_t
        cruise_active; ///< cruise control active state, a 2-bit value (0 off / 1 active / 2 error / 3 n/a) - SPN 595
} J1939Ccvs;

/** @brief One decoded Diagnostic Trouble Code (J1939-73 SPN conversion method 4). */
typedef struct
{
    uint32_t spn; ///< suspect parameter number (19-bit)
    uint8_t fmi;  ///< failure mode identifier (5-bit)
    uint8_t cm;   ///< SPN conversion method (1-bit)
    uint8_t oc;   ///< occurrence count (7-bit)
} J1939Dtc;

/** @brief Decoded DM1 lamp status (each field 0 = off, 1 = on; 2/3 reserved / not available). */
typedef struct
{
    uint8_t mil;           ///< malfunction indicator lamp
    uint8_t red_stop;      ///< red stop lamp
    uint8_t amber_warning; ///< amber warning lamp
    uint8_t protect;       ///< protect lamp
    uint8_t dtc_count;     ///< number of active DTCs decoded into the caller's array
} J1939Dm1;

#include "shared/can/can.h" // CanFrame: the type a parameter points at

/** @brief What encode_id takes: id, priority, pgn, sa, da. */
typedef struct
{
    uint32_t *id;
    uint8_t priority;
    uint32_t pgn;
    uint8_t sa;
    uint8_t da;
} J1939EncodeIdArgs;

/** @brief What decode_id takes: id, out. */
typedef struct
{
    uint32_t id;
    J1939Id *out;
} J1939DecodeIdArgs;

/** @brief What build_message takes: out, priority, pgn, sa, da, data, ... */
typedef struct
{
    CanFrame *out;
    uint8_t priority;
    uint32_t pgn;
    uint8_t sa;
    uint8_t da;
    const uint8_t *data;
    uint8_t len;
} J1939BuildMessageArgs;

/** @brief What build_request takes: out, sa, da, requested_pgn. */
typedef struct
{
    CanFrame *out;
    uint8_t sa;
    uint8_t da;
    uint32_t requested_pgn;
} J1939BuildRequestArgs;

/** @brief What build_address_claim takes: out, sa, name. */
typedef struct
{
    CanFrame *out;
    uint8_t sa;
    uint64_t name;
} J1939BuildAddressClaimArgs;

/** @brief What build_name takes: arbitrary_address_capable, ... */
typedef struct
{
    proto_bool arbitrary_address_capable;
    uint8_t industry_group;
    uint8_t vehicle_system_instance;
    uint8_t vehicle_system;
    uint8_t function;
    uint8_t function_instance;
    uint8_t ecu_instance;
    uint16_t manufacturer_code;
    uint32_t identity_number;
} J1939BuildNameArgs;

/** @brief What tp_num_packets takes: total_size. */
typedef struct
{
    uint16_t total_size;
} J1939TpNumPacketsArgs;

/** @brief What build_bam_cm takes: out, sa, pgn, total_size. */
typedef struct
{
    CanFrame *out;
    uint8_t sa;
    uint32_t pgn;
    uint16_t total_size;
} J1939BuildBamCmArgs;

/** @brief What build_tp_dt takes: out, sa, da, seq, chunk, chunk_len. */
typedef struct
{
    CanFrame *out;
    uint8_t sa;
    uint8_t da;
    uint8_t seq;
    const uint8_t *chunk;
    uint8_t chunk_len;
} J1939BuildTpDtArgs;

/** @brief What tp_reset takes: rx. */
typedef struct
{
    J1939TpRx *rx;
} J1939TpResetArgs;

/** @brief What tp_feed takes: rx, f. */
typedef struct
{
    J1939TpRx *rx;
    const CanFrame *f;
} J1939TpFeedArgs;

/** @brief What decode_eec1 takes: f, out. */
typedef struct
{
    const CanFrame *f;
    J1939Eec1 *out;
} J1939DecodeEec1Args;

/** @brief What decode_et1 takes: f, out. */
typedef struct
{
    const CanFrame *f;
    J1939Et1 *out;
} J1939DecodeEt1Args;

/** @brief What decode_lfe takes: f, out. */
typedef struct
{
    const CanFrame *f;
    J1939Lfe *out;
} J1939DecodeLfeArgs;

/** @brief What decode_amb takes: f, out. */
typedef struct
{
    const CanFrame *f;
    J1939Amb *out;
} J1939DecodeAmbArgs;

/** @brief What decode_ic1 takes: f, out. */
typedef struct
{
    const CanFrame *f;
    J1939Ic1 *out;
} J1939DecodeIc1Args;

/** @brief What decode_vd takes: f, out. */
typedef struct
{
    const CanFrame *f;
    J1939Vd *out;
} J1939DecodeVdArgs;

/** @brief What decode_ccvs takes: f, out. */
typedef struct
{
    const CanFrame *f;
    J1939Ccvs *out;
} J1939DecodeCcvsArgs;

/** @brief What decode_dm1 takes: body, len, out, out_dtcs, max. */
typedef struct
{
    const uint8_t *body;
    size_t len;
    J1939Dm1 *out;
    J1939Dtc *out_dtcs; ///< caller array receiving up to max decoded DTCs (may be null to only read the lamps)
    size_t max;
} J1939DecodeDm1Args;

/**
 * @brief SAE J1939 message codec (PROTOCORE_ENABLE_J1939) - the heavy-duty-vehicle / agriculture / marine / genset CAN
 * higher-layer protocol, over 29-bit extended CAN frames.
 *
 * A caller sets the members a call takes, invokes it through ::J1939 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   J1939.encode_id_args.id = ...;
 *   J1939.encode_id_args.priority = ...;
 *   J1939.encode_id_args.pgn = ...;
 *   J1939.encode_id_args.sa = ...;
 *   J1939.encode_id_args.da = ...;
 *   J1939.encode_id(work);
 *   // J1939.ok is what the call reports
 *
 * @var J1939Ns::encode_id_args  what encode_id takes: id, priority, pgn, sa, da
 * @var J1939Ns::decode_id_args  what decode_id takes: id, out
 * @var J1939Ns::build_message_args  what build_message takes: out, priority, pgn, sa, da, data,
 * @var J1939Ns::build_request_args  what build_request takes: out, sa, da, requested_pgn
 * @var J1939Ns::build_address_claim_args  what build_address_claim takes: out, sa, name
 * @var J1939Ns::build_name_args  what build_name takes: arbitrary_address_capable,
 * @var J1939Ns::tp_num_packets_args  what tp_num_packets takes: total_size
 * @var J1939Ns::build_bam_cm_args  what build_bam_cm takes: out, sa, pgn, total_size
 * @var J1939Ns::build_tp_dt_args  what build_tp_dt takes: out, sa, da, seq, chunk, chunk_len
 * @var J1939Ns::tp_reset_args  what tp_reset takes: rx
 * @var J1939Ns::tp_feed_args  what tp_feed takes: rx, f
 * @var J1939Ns::decode_eec1_args  what decode_eec1 takes: f, out
 * @var J1939Ns::decode_et1_args  what decode_et1 takes: f, out
 * @var J1939Ns::decode_lfe_args  what decode_lfe takes: f, out
 * @var J1939Ns::decode_amb_args  what decode_amb takes: f, out
 * @var J1939Ns::decode_ic1_args  what decode_ic1 takes: f, out
 * @var J1939Ns::decode_vd_args  what decode_vd takes: f, out
 * @var J1939Ns::decode_ccvs_args  what decode_ccvs takes: f, out
 * @var J1939Ns::decode_dm1_args  what decode_dm1 takes: body, len, out, out_dtcs, max
 * @var J1939Ns::ok  true iff f decodes to PGN 61444 and carries 8 data octets; false ...
 * @var J1939Ns::value  the value a call reports
 * @var J1939Ns::u8  what a call reports
 * @var J1939Ns::tp  what a call reports
 * @var J1939Ns::encode_id  encode a 29-bit J1939 id. da is used only for a PDU1 (PF < 240) PGN
 * @var J1939Ns::decode_id  decode a 29-bit J1939 id into its fields
 * @var J1939Ns::build_message  build a single-frame (<= 8 octet) J1939 message
 * @var J1939Ns::build_request  build a Request-PGN frame asking da for requested_pgn
 * @var J1939Ns::build_address_claim  build an Address-Claimed frame announcing sa with the 64-bit name
 * @var J1939Ns::build_name  compose a 64-bit J1939 NAME from its fields (see J1939-81)
 * @var J1939Ns::tp_num_packets  octet count -> TP packet count (ceil(size / 7))
 * @var J1939Ns::build_bam_cm  build the BAM (broadcast) TP.CM announce frame for pgn / total_size
 * @var J1939Ns::build_tp_dt  build TP.DT data packet seq (1-based) carrying chunk_len (1..7) ...
 * @var J1939Ns::tp_reset  reset a reassembly context to idle
 * @var J1939Ns::tp_feed  feed a received frame to the reassembler; see J1939TpResult
 * @var J1939Ns::decode_eec1  decode an EEC1 (PGN 61444) single frame into out
 * @var J1939Ns::decode_et1  decode an ET1 (PGN 65262) single frame into out
 * @var J1939Ns::decode_lfe  decode an LFE (PGN 65266) single frame into out
 * @var J1939Ns::decode_amb  decode an AMB (PGN 65269) single frame into out
 * @var J1939Ns::decode_ic1  decode an IC1 (PGN 65270) single frame into out
 * @var J1939Ns::decode_vd  decode a VD (PGN 65248) single frame into out
 * @var J1939Ns::decode_ccvs  decode a CCVS (PGN 65265) single frame into out
 * @var J1939Ns::decode_dm1  decode a DM1 (PGN 65226) body: the lamp-status octet, the ...
 *
 * @c work is PROTOCORE_J1939_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    J1939EncodeIdArgs encode_id_args;
    J1939DecodeIdArgs decode_id_args;
    J1939BuildMessageArgs build_message_args;
    J1939BuildRequestArgs build_request_args;
    J1939BuildAddressClaimArgs build_address_claim_args;
    J1939BuildNameArgs build_name_args;
    J1939TpNumPacketsArgs tp_num_packets_args;
    J1939BuildBamCmArgs build_bam_cm_args;
    J1939BuildTpDtArgs build_tp_dt_args;
    J1939TpResetArgs tp_reset_args;
    J1939TpFeedArgs tp_feed_args;
    J1939DecodeEec1Args decode_eec1_args;
    J1939DecodeEt1Args decode_et1_args;
    J1939DecodeLfeArgs decode_lfe_args;
    J1939DecodeAmbArgs decode_amb_args;
    J1939DecodeIc1Args decode_ic1_args;
    J1939DecodeVdArgs decode_vd_args;
    J1939DecodeCcvsArgs decode_ccvs_args;
    J1939DecodeDm1Args decode_dm1_args;

    proto_bool ok;
    uint64_t value;
    uint8_t u8;
    J1939TpResult tp;

    void (*const encode_id)(uint8_t *restrict work);
    void (*const decode_id)(uint8_t *restrict work);
    void (*const build_message)(uint8_t *restrict work);
    void (*const build_request)(uint8_t *restrict work);
    void (*const build_address_claim)(uint8_t *restrict work);
    void (*const build_name)(uint8_t *restrict work);
    void (*const tp_num_packets)(uint8_t *restrict work);
    void (*const build_bam_cm)(uint8_t *restrict work);
    void (*const build_tp_dt)(uint8_t *restrict work);
    void (*const tp_reset)(uint8_t *restrict work);
    void (*const tp_feed)(uint8_t *restrict work);
    void (*const decode_eec1)(uint8_t *restrict work);
    void (*const decode_et1)(uint8_t *restrict work);
    void (*const decode_lfe)(uint8_t *restrict work);
    void (*const decode_amb)(uint8_t *restrict work);
    void (*const decode_ic1)(uint8_t *restrict work);
    void (*const decode_vd)(uint8_t *restrict work);
    void (*const decode_ccvs)(uint8_t *restrict work);
    void (*const decode_dm1)(uint8_t *restrict work);
} J1939Ns;

/** @brief The one symbol this module exports. */
extern J1939Ns J1939;

/**
 * @brief The PROTOCORE_J1939_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_j1939_span(void);

PROTOCORE_END_DECLS

#endif //

#endif // PROTOCORE_J1939_H
