// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nmea2000.h
 * @brief NMEA 2000 codec (PC_ENABLE_NMEA2000) - the marine instrumentation network, built on
 *        J1939 over CAN.
 *
 * NMEA 2000 is J1939 at the transport layer (the same 29-bit priority / PGN / source /
 * destination identifier), so this codec reuses the J1939 id encode / decode
 * (`PC_ENABLE_NMEA2000` force-enables `PC_ENABLE_J1939`). What it adds is the
 * NMEA-specific **Fast Packet** transport: messages of 9..223 octets are split across CAN
 * frames using a per-frame control octet (sequence counter + frame counter) instead of the
 * J1939 BAM/CMDT protocol. The first frame carries the total length; continuations carry 7
 * data octets each.
 *
 * Pure and host-tested. Drive it from the ESP32 TWAI peripheral or an MCP2515 over SPI to
 * bridge an NMEA 2000 backbone (GPS, wind, depth, engine PGNs) onto Wi-Fi.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NMEA2000_H
#define PROTOCORE_NMEA2000_H

#include "protocore_config.h"

#if PC_ENABLE_NMEA2000

#include "services/fieldbus/j1939/j1939.h" // reuses the J1939 29-bit identifier codec
#include "shared_primitives/can.h"

#define N2K_FP_SEQ_SHIFT 5      ///< control octet: sequence counter in bits 7..5
#define N2K_FP_FRAME_MASK 0x1Fu ///< control octet: frame counter in bits 4..0
#define N2K_FP_F0_DATA 6u       ///< data octets in the first frame (after control + length octets)
#define N2K_FP_FN_DATA 7u       ///< data octets in a continuation frame

/** @brief Result of feeding a frame to the Fast Packet reassembler. */
typedef enum PROTO_ENUM_PACKED
{
    N2K_FP_IGNORED = 0, ///< not part of the active sequence
    N2K_FP_STARTED,     ///< first frame opened a sequence
    N2K_FP_PROGRESS,    ///< a continuation frame was accepted
    N2K_FP_COMPLETE,    ///< the message is fully reassembled
    N2K_FP_ERR,         ///< out-of-order / too large
} N2kFpResult;

/** @brief Fast Packet reassembly context (one in-flight message). */
typedef struct
{
    proto_bool active;
    uint8_t seq;        ///< sequence counter of the in-progress message
    uint8_t sa;         ///< source address
    uint32_t pgn;       ///< the message PGN
    uint16_t total_len; ///< announced total length
    uint16_t received;  ///< octets stored so far
    uint8_t next_frame; ///< next expected frame counter
    uint8_t buf[PC_N2K_FP_MAX];
} N2kFastPacketRx;

/** @brief Number of Fast Packet frames needed for @p total_len octets. */
uint8_t pc_n2k_fastpacket_num_frames(uint16_t total_len);

/**
 * @brief Build Fast Packet frame @p frame_idx (0-based) of a message.
 * @p seq is the 0..7 sequence counter for this message; @p total_len is the whole payload.
 */
proto_bool pc_n2k_fastpacket_build_frame(CanFrame *out, uint8_t seq, uint8_t frame_idx, uint8_t priority, uint32_t pgn,
                                         uint8_t sa, uint8_t da, const uint8_t *data, uint16_t total_len);

/** @brief Reset a Fast Packet reassembly context to idle. */
void pc_n2k_fastpacket_reset(N2kFastPacketRx *rx);

/** @brief Feed a received frame to the Fast Packet reassembler; see @ref N2kFpResult. */
N2kFpResult pc_n2k_fastpacket_feed(N2kFastPacketRx *rx, const CanFrame *f);

/** @brief Build a single-frame (<= 8 octet) NMEA 2000 message (a thin wrap of J1939). */
proto_bool pc_n2k_build_single(CanFrame *out, uint8_t priority, uint32_t pgn, uint8_t sa, uint8_t da,
                               const uint8_t *data, uint8_t len);

// --- typed decoders for common single-frame PGNs ---
//
// These decode a raw PGN payload (a single frame's data[] or a reassembled Fast Packet buffer) into
// engineering units. The caller matches the PGN off the CAN id first, then calls the matching decoder.
// NMEA 2000 marks a field "not available" with an all-ones raw (0xFFFF for a U2, 0x7FFFFFFF for the
// signed lat/lon), which clears the field's validity flag.

#define N2K_PGN_POSITION_RAPID 129025u ///< Position, Rapid Update: latitude + longitude
#define N2K_PGN_COG_SOG_RAPID 129026u  ///< COG & SOG, Rapid Update: course + speed over ground
#define N2K_PGN_ENGINE_RAPID 127488u   ///< Engine Parameters, Rapid Update: speed + boost + tilt/trim
#define N2K_PGN_ENGINE_DYNAMIC 127489u ///< Engine Parameters, Dynamic: oil / coolant / hours / load / ...
#define N2K_PGN_WIND_DATA 130306u      ///< Wind Data: speed + angle + reference
#define N2K_PGN_SPEED 128259u          ///< Speed: water-referenced + ground-referenced speed
#define N2K_PGN_WATER_DEPTH 128267u    ///< Water Depth: depth below transducer + offset
#define N2K_PGN_VESSEL_HEADING 127250u ///< Vessel Heading: heading + deviation + variation
#define N2K_PGN_RUDDER 127245u         ///< Rudder: direction order + angle order + position
#define N2K_PGN_ATTITUDE 127257u       ///< Attitude: yaw + pitch + roll
#define N2K_PGN_TEMPERATURE 130312u    ///< Temperature: instance + source + actual / set temperature

// Temperature source (PGN 130312 byte 2).
#define N2K_TEMP_SRC_SEA 0
#define N2K_TEMP_SRC_OUTSIDE 1
#define N2K_TEMP_SRC_INSIDE 2
#define N2K_TEMP_SRC_ENGINE_ROOM 3
#define N2K_TEMP_SRC_MAIN_CABIN 4
#define N2K_TEMP_SRC_LIVE_WELL 5
#define N2K_TEMP_SRC_BAIT_WELL 6
#define N2K_TEMP_SRC_REFRIGERATION 7
#define N2K_TEMP_SRC_HEATING_SYSTEM 8
#define N2K_TEMP_SRC_FREEZER 13
#define N2K_TEMP_SRC_EXHAUST_GAS 14

// COG reference (PGN 129026 byte 1, low 2 bits).
#define N2K_COG_REF_TRUE 0     ///< course over ground referenced to True North
#define N2K_COG_REF_MAGNETIC 1 ///< course over ground referenced to Magnetic North
#define N2K_COG_REF_ERROR 2    ///< reference in error
#define N2K_COG_REF_NULL 3     ///< reference not available / null

// Heading reference (PGN 127250 byte 7, low 2 bits).
#define N2K_HEADING_REF_TRUE 0     ///< true heading
#define N2K_HEADING_REF_MAGNETIC 1 ///< magnetic heading

// Wind reference (PGN 130306 byte 5, low 3 bits).
#define N2K_WIND_REF_TRUE_NORTH 0 ///< true, referenced to North
#define N2K_WIND_REF_MAGNETIC 1   ///< magnetic, referenced to North
#define N2K_WIND_REF_APPARENT 2   ///< apparent
#define N2K_WIND_REF_TRUE_BOAT 3  ///< true, referenced to the vessel (boat)
#define N2K_WIND_REF_TRUE_WATER 4 ///< true, referenced to the water

/** @brief Decoded Position Rapid Update (PGN 129025). */
typedef struct
{
    proto_bool valid; ///< false if either coordinate is not-available
    double lat_deg;   ///< latitude in decimal degrees (1e-7 deg/bit)
    double lon_deg;   ///< longitude in decimal degrees
} N2kPositionRapid;

/** @brief Decoded COG & SOG Rapid Update (PGN 129026). */
typedef struct
{
    uint8_t sid;          ///< sequence id
    uint8_t cog_ref;      ///< course reference (@ref N2K_COG_REF_TRUE / _MAGNETIC)
    proto_bool cog_valid; ///< false if the course over ground is not-available
    float cog_rad;        ///< course over ground (radians, 0.0001 rad per bit)
    proto_bool sog_valid; ///< false if the speed over ground is not-available
    float sog_mps;        ///< speed over ground (m/s, 0.01 m/s per bit)
} N2kCogSogRapid;

/** @brief Decoded Engine Parameters, Rapid Update (PGN 127488). */
typedef struct
{
    uint8_t instance;       ///< engine instance (0 = single / port, 1 = starboard, ...)
    proto_bool speed_valid; ///< false if the engine speed is not-available
    float speed_rpm;        ///< engine speed (rpm, 0.25 rpm per bit)
    proto_bool boost_valid; ///< false if the boost pressure is not-available
    float boost_pa;         ///< engine boost pressure (Pa, 100 Pa per bit)
    proto_bool tilt_valid;  ///< false if the tilt/trim is not-available
    int8_t tilt_pct;        ///< engine tilt / trim (percent, 1 %/bit, signed)
} N2kEngineRapid;

// Rudder direction order (PGN 127245 byte 1, low 3 bits).
#define N2K_RUDDER_NO_ORDER 0
#define N2K_RUDDER_MOVE_TO_STARBOARD 1
#define N2K_RUDDER_MOVE_TO_PORT 2

/** @brief Decoded Rudder (PGN 127245): the commanded and actual rudder state. */
typedef struct
{
    uint8_t instance;             ///< rudder instance
    uint8_t direction_order;      ///< commanded direction (@ref N2K_RUDDER_NO_ORDER etc.)
    proto_bool angle_order_valid; ///< false if the angle order is not-available
    float angle_order_rad;        ///< commanded rudder angle (radians, 0.0001 rad per bit)
    proto_bool position_valid;    ///< false if the position is not-available
    float position_rad;           ///< actual rudder position (radians, 0.0001 rad per bit)
} N2kRudder;

/** @brief Decoded Attitude (PGN 127257): the vessel's orientation. Each angle is signed at 0.0001 rad/bit. */
typedef struct
{
    uint8_t sid;            ///< sequence id
    proto_bool yaw_valid;   ///< false if yaw is not-available
    float yaw_rad;          ///< yaw (radians); + = bow rotating to starboard
    proto_bool pitch_valid; ///< false if pitch is not-available
    float pitch_rad;        ///< pitch (radians); + = bow up
    proto_bool roll_valid;  ///< false if roll is not-available
    float roll_rad;         ///< roll (radians); + = starboard side down
} N2kAttitude;

/** @brief Decoded Temperature (PGN 130312). Temperatures are carried in Kelvin (0.01 K/bit) on the wire and
 *  exposed here in Celsius. */
typedef struct
{
    uint8_t sid;             ///< sequence id
    uint8_t instance;        ///< temperature instance
    uint8_t source;          ///< temperature source (@ref N2K_TEMP_SRC_SEA etc.)
    proto_bool actual_valid; ///< false if the actual temperature is not-available
    float actual_c;          ///< actual temperature (degrees Celsius)
    proto_bool set_valid;    ///< false if the set/target temperature is not-available
    float set_c;             ///< set / target temperature (degrees Celsius)
} N2kTemperature;

/** @brief Decoded Engine Parameters, Dynamic (PGN 127489): the full engine-monitoring picture. Each measured
 *  field clears its validity flag for a not-available raw; the two discrete-status words are raw bitfields. */
typedef struct // NOSONAR(cpp:S1820): one decoded PGN is one logical message; the field count mirrors the
               // protocol's signals, so splitting the struct would be artificial, not clearer
{
    uint8_t instance; ///< engine instance
    proto_bool oil_pressure_valid;
    float oil_pressure_pa; ///< engine oil pressure (Pa, 100 Pa per bit)
    proto_bool oil_temp_valid;
    float oil_temp_c; ///< engine oil temperature (degrees Celsius; 0.1 K/bit on the wire)
    proto_bool coolant_temp_valid;
    float coolant_temp_c; ///< engine coolant temperature (degrees Celsius; 0.01 K/bit on the wire)
    proto_bool alt_voltage_valid;
    float alt_voltage_v; ///< alternator potential (V, 0.01 V per bit, signed)
    proto_bool fuel_rate_valid;
    float fuel_rate_lph; ///< fuel rate (L/h, 0.1 L/h per bit, signed)
    proto_bool engine_hours_valid;
    uint32_t engine_hours_s; ///< total engine hours (seconds, 1 s per bit)
    proto_bool coolant_pressure_valid;
    float coolant_pressure_pa; ///< coolant pressure (Pa, 100 Pa per bit)
    proto_bool fuel_pressure_valid;
    float fuel_pressure_pa;     ///< fuel pressure (Pa, 1000 Pa per bit)
    uint16_t discrete_status_1; ///< discrete status 1 (raw bitfield)
    uint16_t discrete_status_2; ///< discrete status 2 (raw bitfield)
    proto_bool load_valid;
    int8_t load_pct; ///< percent engine load (%, 1 %/bit, signed)
    proto_bool torque_valid;
    int8_t torque_pct; ///< percent engine torque (%, 1 %/bit, signed)
} N2kEngineDynamic;

/** @brief Decoded Wind Data (PGN 130306). */
typedef struct
{
    uint8_t sid;            ///< sequence id
    proto_bool speed_valid; ///< false if the wind speed is not-available
    float speed_mps;        ///< wind speed (m/s, 0.01 m/s per bit)
    proto_bool angle_valid; ///< false if the wind angle is not-available
    float angle_rad;        ///< wind angle (radians, 0.0001 rad per bit)
    uint8_t reference;      ///< wind reference (@ref N2K_WIND_REF_TRUE_NORTH etc.)
} N2kWindData;

// Speed water-referenced sensor type (PGN 128259 byte 5).
#define N2K_SPEED_TYPE_PADDLE_WHEEL 0
#define N2K_SPEED_TYPE_PITOT_TUBE 1
#define N2K_SPEED_TYPE_DOPPLER 2
#define N2K_SPEED_TYPE_CORRELATION 3     ///< correlation / ultrasound
#define N2K_SPEED_TYPE_ELECTROMAGNETIC 4 ///< electromagnetic

/** @brief Decoded Speed (PGN 128259): through-water and over-ground speed. */
typedef struct
{
    uint8_t sid;             ///< sequence id
    proto_bool water_valid;  ///< false if the water-referenced speed is not-available
    float water_mps;         ///< speed through water (m/s, 0.01 m/s per bit)
    proto_bool ground_valid; ///< false if the ground-referenced speed is not-available
    float ground_mps;        ///< speed over ground (m/s, 0.01 m/s per bit)
    uint8_t water_ref_type;  ///< water-speed sensor type (@ref N2K_SPEED_TYPE_PADDLE_WHEEL etc.)
} N2kSpeed;

/** @brief Decoded Water Depth (PGN 128267). */
typedef struct
{
    uint8_t sid;            ///< sequence id
    proto_bool depth_valid; ///< false if the depth is not-available
    float depth_m;          ///< water depth below the transducer (metres, 0.01 m per bit)
    float offset_m;         ///< transducer offset (m); positive = distance to waterline, negative = to keel
} N2kWaterDepth;

/** @brief Decoded Vessel Heading (PGN 127250). */
typedef struct
{
    uint8_t sid;              ///< sequence id
    proto_bool heading_valid; ///< false if the heading is not-available
    float heading_rad;        ///< heading (radians, 0.0001 rad per bit)
    float deviation_rad;      ///< magnetic deviation (radians)
    float variation_rad;      ///< magnetic variation (radians)
    uint8_t reference;        ///< heading reference (@ref N2K_HEADING_REF_TRUE / _MAGNETIC)
} N2kVesselHeading;

/**
 * @brief Decode a Position Rapid Update (PGN 129025) payload into @p out.
 * @return true iff @p len is at least 8 octets; false otherwise.
 */
proto_bool pc_n2k_decode_position_rapid(const uint8_t *payload, size_t len, N2kPositionRapid *out);

/**
 * @brief Decode a COG & SOG Rapid Update (PGN 129026) payload into @p out.
 * @return true iff @p len is at least 6 octets (SID + reference + COG + SOG); false otherwise.
 */
proto_bool pc_n2k_decode_cog_sog_rapid(const uint8_t *payload, size_t len, N2kCogSogRapid *out);

/**
 * @brief Decode an Engine Parameters Rapid Update (PGN 127488) payload into @p out.
 * @return true iff @p len is at least 6 octets (instance + speed + boost + tilt); false otherwise.
 */
proto_bool pc_n2k_decode_engine_rapid(const uint8_t *payload, size_t len, N2kEngineRapid *out);

/**
 * @brief Decode an Engine Parameters Dynamic (PGN 127489) payload into @p out. This is a Fast Packet PGN, so
 *        @p payload is the reassembled message body.
 * @return true iff @p len is at least 26 octets (the full engine-parameters record); false otherwise.
 */
proto_bool pc_n2k_decode_engine_dynamic(const uint8_t *payload, size_t len, N2kEngineDynamic *out);

/**
 * @brief Decode a Rudder (PGN 127245) payload into @p out.
 * @return true iff @p len is at least 6 octets (instance + direction + angle order + position); false otherwise.
 */
proto_bool pc_n2k_decode_rudder(const uint8_t *payload, size_t len, N2kRudder *out);

/**
 * @brief Decode an Attitude (PGN 127257) payload into @p out.
 * @return true iff @p len is at least 7 octets (SID + yaw + pitch + roll); false otherwise.
 */
proto_bool pc_n2k_decode_attitude(const uint8_t *payload, size_t len, N2kAttitude *out);

/**
 * @brief Decode a Temperature (PGN 130312) payload into @p out.
 * @return true iff @p len is at least 7 octets (SID + instance + source + actual + set); false otherwise.
 */
proto_bool pc_n2k_decode_temperature(const uint8_t *payload, size_t len, N2kTemperature *out);

/** @brief Decoded Battery Status (PGN 127508): a battery bank's voltage, current, and temperature. Each
 *  measurement clears a validity flag for a not-available raw. */
typedef struct
{
    uint8_t instance;         ///< battery instance
    proto_bool voltage_valid; ///< false when the raw voltage is in the not-available range
    float voltage_v;          ///< battery voltage (V, 0.01 V/bit)
    proto_bool current_valid;
    float current_a; ///< battery current (A, 0.1 A/bit, signed)
    proto_bool temp_valid;
    float temp_c; ///< battery temperature (degrees C; carried as Kelvin at 0.01 K/bit on the wire)
    uint8_t sid;  ///< sequence id (correlates related messages)
} N2kBatteryStatus;

/**
 * @brief Decode a Battery Status (PGN 127508) payload into @p out: instance + voltage (2, signed, 0.01 V) +
 *        current (2, signed, 0.1 A) + temperature (2, unsigned, 0.01 K) + SID.
 * @return true iff @p len is at least 8 octets; false otherwise.
 */
proto_bool pc_n2k_decode_battery_status(const uint8_t *payload, size_t len, N2kBatteryStatus *out);

// Fluid / tank type (PGN 127505 byte 0, high nibble).
#define PC_N2K_FLUID_FUEL 0
#define PC_N2K_FLUID_WATER 1
#define PC_N2K_FLUID_GRAY_WATER 2
#define PC_N2K_FLUID_LIVE_WELL 3
#define PC_N2K_FLUID_OIL 4
#define PC_N2K_FLUID_BLACK_WATER 5

/** @brief Decoded Fluid Level (PGN 127505): a tank's fill level and total capacity. */
typedef struct
{
    uint8_t instance;       ///< tank instance (0..15)
    uint8_t fluid_type;     ///< fluid / tank type (PC_N2K_FLUID_*)
    proto_bool level_valid; ///< false when the raw level is in the not-available range
    float level_pct;        ///< tank level as a percentage full (0.004 %/bit)
    proto_bool capacity_valid;
    float capacity_l; ///< total tank capacity (litres, 0.1 L/bit)
} N2kFluidLevel;

/**
 * @brief Decode a Fluid Level (PGN 127505) payload into @p out: the instance + fluid type (packed in byte 0),
 *        the level (2, signed, 0.004 %) and the total capacity (4, unsigned, 0.1 L).
 * @return true iff @p len is at least 7 octets; false otherwise.
 */
proto_bool pc_n2k_decode_fluid_level(const uint8_t *payload, size_t len, N2kFluidLevel *out);

// Pressure source (PGN 130314 byte 2).
#define PC_N2K_PRESSURE_ATMOSPHERIC 0
#define PC_N2K_PRESSURE_WATER 1
#define PC_N2K_PRESSURE_STEAM 2
#define PC_N2K_PRESSURE_COMPRESSED_AIR 3
#define PC_N2K_PRESSURE_HYDRAULIC 4
#define PC_N2K_PRESSURE_FILTER 5
#define PC_N2K_PRESSURE_ALTIMETER_SETTING 6
#define PC_N2K_PRESSURE_OIL 7
#define PC_N2K_PRESSURE_FUEL 8

/** @brief Decoded Actual Pressure (PGN 130314): a measured pressure from one of several sources. */
typedef struct
{
    uint8_t sid;               ///< sequence id (correlates related messages)
    uint8_t instance;          ///< pressure instance
    uint8_t source;            ///< pressure source (PC_N2K_PRESSURE_*)
    proto_bool pressure_valid; ///< false when the raw pressure is in the not-available range
    float pressure_pa;         ///< measured pressure (Pa, 0.1 Pa/bit, signed)
} N2kActualPressure;

/**
 * @brief Decode an Actual Pressure (PGN 130314) payload into @p out: SID + instance + source + pressure
 *        (4, signed, 0.1 Pa/bit). The trailing reserved octet is not required.
 * @return true iff @p len is at least 7 octets (SID + instance + source + pressure); false otherwise.
 */
proto_bool pc_n2k_decode_actual_pressure(const uint8_t *payload, size_t len, N2kActualPressure *out);

/**
 * @brief Decode a Wind Data (PGN 130306) payload into @p out.
 * @return true iff @p len is at least 6 octets; false otherwise.
 */
proto_bool pc_n2k_decode_wind_data(const uint8_t *payload, size_t len, N2kWindData *out);

/**
 * @brief Decode a Speed (PGN 128259) payload into @p out.
 * @return true iff @p len is at least 6 octets (SID + water speed + ground speed + type); false otherwise.
 */
proto_bool pc_n2k_decode_speed(const uint8_t *payload, size_t len, N2kSpeed *out);

/**
 * @brief Decode a Water Depth (PGN 128267) payload into @p out.
 * @return true iff @p len is at least 7 octets (SID + depth + offset); false otherwise.
 */
proto_bool pc_n2k_decode_water_depth(const uint8_t *payload, size_t len, N2kWaterDepth *out);

/**
 * @brief Decode a Vessel Heading (PGN 127250) payload into @p out.
 * @return true iff @p len is at least 8 octets; false otherwise.
 */
proto_bool pc_n2k_decode_vessel_heading(const uint8_t *payload, size_t len, N2kVesselHeading *out);

#endif // PC_ENABLE_NMEA2000
#endif // PROTOCORE_NMEA2000_H
