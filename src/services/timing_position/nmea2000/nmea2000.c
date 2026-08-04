// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nmea2000.c
 * @brief NMEA 2000 codec (Fast Packet over J1939; pure, host-tested).
 */

#include "services/timing_position/nmea2000/nmea2000.h"

#if PC_ENABLE_NMEA2000

#include <string.h>

uint8_t pc_n2k_fastpacket_num_frames(uint16_t total_len)
{
    if (total_len <= N2K_FP_F0_DATA)
    {
        return 1;
    }
    return (uint8_t)(1u + (total_len - N2K_FP_F0_DATA + (N2K_FP_FN_DATA - 1)) / N2K_FP_FN_DATA);
}

proto_bool pc_n2k_fastpacket_build_frame(CanFrame *out, uint8_t seq, uint8_t frame_idx, uint8_t priority, uint32_t pgn,
                                         uint8_t sa, uint8_t da, const uint8_t *data, uint16_t total_len)
{
    if (!out || !data || seq > 7 || total_len == 0 || total_len > PC_N2K_FP_MAX)
    {
        return PROTO_FALSE;
    }
    if (frame_idx >= pc_n2k_fastpacket_num_frames(total_len))
    {
        return PROTO_FALSE;
    }
    uint32_t id;
    if (!pc_j1939_encode_id(&id, priority, pgn, sa, da))
    {
        return PROTO_FALSE;
    }
    out->id = id;
    out->extended = PROTO_TRUE;
    out->rtr = PROTO_FALSE;
    out->dlc = PC_CAN_MAX_DLC;                  // Fast Packet frames are full 8-octet frames
    memset(out->data, 0xFF, sizeof(out->data)); // pad unused octets with 0xFF

    out->data[0] = (uint8_t)((seq << N2K_FP_SEQ_SHIFT) | (frame_idx & N2K_FP_FRAME_MASK));
    if (frame_idx == 0)
    {
        out->data[1] = (uint8_t)total_len;
        uint8_t n = total_len < N2K_FP_F0_DATA ? (uint8_t)total_len : (uint8_t)N2K_FP_F0_DATA;
        memcpy(out->data + 2, data, n);
    }
    else
    {
        uint16_t off = (uint16_t)(N2K_FP_F0_DATA + (frame_idx - 1) * N2K_FP_FN_DATA);
        uint16_t remaining = (uint16_t)(total_len - off);
        uint8_t n = remaining < N2K_FP_FN_DATA ? (uint8_t)remaining : (uint8_t)N2K_FP_FN_DATA;
        memcpy(out->data + 1, data + off, n);
    }
    return PROTO_TRUE;
}

void pc_n2k_fastpacket_reset(N2kFastPacketRx *rx)
{
    if (rx)
    {
        memset(rx, 0, sizeof(*rx));
    }
}

N2kFpResult pc_n2k_fastpacket_feed(N2kFastPacketRx *rx, const CanFrame *f)
{
    if (!rx || !f || !f->extended || f->dlc < 2)
    {
        return N2K_FP_IGNORED;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id))
    // out, and &id is non-null
    {
        return N2K_FP_IGNORED;
        // out, and &id is non-null
    }

    uint8_t seq = (uint8_t)(f->data[0] >> N2K_FP_SEQ_SHIFT);
    uint8_t frame_idx = (uint8_t)(f->data[0] & N2K_FP_FRAME_MASK);

    if (frame_idx == 0) // first frame: total length + first 6 data octets
    {
        uint16_t total = f->data[1];
        if (total == 0 || total > PC_N2K_FP_MAX)
        {
            return N2K_FP_ERR;
        }
        pc_n2k_fastpacket_reset(rx);
        rx->active = PROTO_TRUE;
        rx->seq = seq;
        rx->sa = id.sa;
        rx->pgn = id.pgn;
        rx->total_len = total;
        uint8_t n = total < N2K_FP_F0_DATA ? (uint8_t)total : (uint8_t)N2K_FP_F0_DATA;
        memcpy(rx->buf, f->data + 2, n);
        rx->received = n;
        rx->next_frame = 1;
        if (rx->received >= total)
        {
            rx->active = PROTO_FALSE;
            return N2K_FP_COMPLETE;
        }
        return N2K_FP_STARTED;
    }

    // continuation frame: must match the active sequence / source / PGN and be in order.
    if (!rx->active || seq != rx->seq || id.sa != rx->sa || id.pgn != rx->pgn)
    {
        return N2K_FP_IGNORED;
    }
    if (frame_idx != rx->next_frame)
    {
        pc_n2k_fastpacket_reset(rx);
        return N2K_FP_ERR;
    }
    uint16_t remaining = (uint16_t)(rx->total_len - rx->received);
    uint8_t n = remaining < N2K_FP_FN_DATA ? (uint8_t)remaining : (uint8_t)N2K_FP_FN_DATA;
    memcpy(rx->buf + rx->received, f->data + 1, n);
    rx->received = (uint16_t)(rx->received + n);
    rx->next_frame++;
    if (rx->received >= rx->total_len)
    {
        rx->active = PROTO_FALSE;
        return N2K_FP_COMPLETE;
    }
    return N2K_FP_PROGRESS;
}

proto_bool pc_n2k_build_single(CanFrame *out, uint8_t priority, uint32_t pgn, uint8_t sa, uint8_t da,
                               const uint8_t *data, uint8_t len)
{
    return pc_j1939_build_message(out, priority, pgn, sa, da, data, len);
}

// --- typed PGN decoders ---

static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static int32_t rd_i32le(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int16_t rd_i16le(const uint8_t *p)
{
    return (int16_t)rd_u16le(p);
}

proto_bool pc_n2k_decode_position_rapid(const uint8_t *payload, size_t len, N2kPositionRapid *out)
{
    if (!payload || !out || len < 8)
    {
        return PROTO_FALSE;
    }
    int32_t lat = rd_i32le(payload); // 1e-7 deg/bit
    int32_t lon = rd_i32le(payload + 4);
    out->valid = (lat != (int32_t)0x7FFFFFFF && lon != (int32_t)0x7FFFFFFF); // 0x7FFFFFFF = not available
    out->lat_deg = (double)lat * 1e-7;
    out->lon_deg = (double)lon * 1e-7;
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_cog_sog_rapid(const uint8_t *payload, size_t len, N2kCogSogRapid *out)
{
    if (!payload || !out || len < 6) // SID(1) + ref(1) + COG(2) + SOG(2)
    {
        return PROTO_FALSE;
    }
    out->sid = payload[0];
    out->cog_ref = (uint8_t)(payload[1] & 0x03u);
    uint16_t cog = rd_u16le(payload + 2); // 0.0001 rad per bit
    uint16_t sog = rd_u16le(payload + 4); // 0.01 m/s per bit
    out->cog_valid = (cog != 0xFFFFu);
    out->cog_rad = (float)cog * 0.0001f;
    out->sog_valid = (sog != 0xFFFFu);
    out->sog_mps = (float)sog * 0.01f;
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_engine_rapid(const uint8_t *payload, size_t len, N2kEngineRapid *out)
{
    if (!payload || !out || len < 6) // instance(1) + speed(2) + boost(2) + tilt(1)
    {
        return PROTO_FALSE;
    }
    out->instance = payload[0];
    uint16_t rpm = rd_u16le(payload + 1); // 0.25 rpm per bit
    out->speed_valid = (rpm != 0xFFFFu);
    out->speed_rpm = (float)rpm * 0.25f;
    uint16_t boost = rd_u16le(payload + 3); // 100 Pa per bit
    out->boost_valid = (boost != 0xFFFFu);
    out->boost_pa = (float)boost * 100.0f;
    out->tilt_valid = (payload[5] != 0x7Fu); // 0x7F = not-available for a signed 1-octet field
    out->tilt_pct = (int8_t)payload[5];
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_temperature(const uint8_t *payload, size_t len, N2kTemperature *out)
{
    if (!payload || !out || len < 7) // sid(1) + instance(1) + source(1) + actual(2) + set(2)
    {
        return PROTO_FALSE;
    }
    out->sid = payload[0];
    out->instance = payload[1];
    out->source = payload[2];
    uint16_t act = rd_u16le(payload + 3); // 0.01 K per bit
    out->actual_valid = (act != 0xFFFFu);
    out->actual_c = (float)act * 0.01f - 273.15f; // Kelvin -> Celsius
    uint16_t set = rd_u16le(payload + 5);
    out->set_valid = (set != 0xFFFFu);
    out->set_c = (float)set * 0.01f - 273.15f;
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_battery_status(const uint8_t *payload, size_t len, N2kBatteryStatus *out)
{
    if (!payload || !out || len < 8) // instance(1) + voltage(2) + current(2) + temperature(2) + sid(1)
    {
        return PROTO_FALSE;
    }
    out->instance = payload[0];
    int16_t v = rd_i16le(payload + 1); // 0.01 V per bit, signed (0x7FFF = not available)
    out->voltage_valid = (v != (int16_t)0x7FFF);
    out->voltage_v = (float)v * 0.01f;
    int16_t c = rd_i16le(payload + 3); // 0.1 A per bit, signed
    out->current_valid = (c != (int16_t)0x7FFF);
    out->current_a = (float)c * 0.1f;
    uint16_t t = rd_u16le(payload + 5); // 0.01 K per bit, unsigned (0xFFFF = not available)
    out->temp_valid = (t != 0xFFFFu);
    out->temp_c = (float)t * 0.01f - 273.15f; // Kelvin -> Celsius
    out->sid = payload[7];
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_fluid_level(const uint8_t *payload, size_t len, N2kFluidLevel *out)
{
    if (!payload || !out || len < 7) // instance/type(1) + level(2) + capacity(4)
    {
        return PROTO_FALSE;
    }
    out->instance = (uint8_t)(payload[0] & 0x0F);          // instance in the low nibble
    out->fluid_type = (uint8_t)((payload[0] >> 4) & 0x0F); // fluid type in the high nibble
    int16_t lv = rd_i16le(payload + 1);                    // 0.004 % per bit, signed (0x7FFF = not available)
    out->level_valid = (lv != (int16_t)0x7FFF);
    out->level_pct = (float)lv * 0.004f;
    uint32_t cap = rd_u32le(payload + 3); // 0.1 L per bit, unsigned (0xFFFFFFFF = not available)
    out->capacity_valid = (cap != 0xFFFFFFFFu);
    out->capacity_l = (float)cap * 0.1f;
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_actual_pressure(const uint8_t *payload, size_t len, N2kActualPressure *out)
{
    if (!payload || !out || len < 7) // sid(1) + instance(1) + source(1) + pressure(4)
    {
        return PROTO_FALSE;
    }
    out->sid = payload[0];
    out->instance = payload[1];
    out->source = payload[2];
    int32_t p = rd_i32le(payload + 3); // 0.1 Pa per bit, signed (0x7FFFFFFF = not available)
    out->pressure_valid = (p != (int32_t)0x7FFFFFFF);
    out->pressure_pa = (float)p * 0.1f;
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_rudder(const uint8_t *payload, size_t len, N2kRudder *out)
{
    if (!payload || !out || len < 6) // instance(1) + direction(1) + angle order(2) + position(2)
    {
        return PROTO_FALSE;
    }
    out->instance = payload[0];
    out->direction_order = (uint8_t)(payload[1] & 0x07u); // low 3 bits
    int16_t angle = rd_i16le(payload + 2);                // 0.0001 rad per bit, signed
    out->angle_order_valid = ((uint16_t)angle != 0x7FFFu);
    out->angle_order_rad = (float)angle * 0.0001f;
    int16_t pos = rd_i16le(payload + 4);
    out->position_valid = ((uint16_t)pos != 0x7FFFu);
    out->position_rad = (float)pos * 0.0001f;
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_attitude(const uint8_t *payload, size_t len, N2kAttitude *out)
{
    if (!payload || !out || len < 7) // sid(1) + yaw(2) + pitch(2) + roll(2)
    {
        return PROTO_FALSE;
    }
    out->sid = payload[0];
    int16_t yaw = rd_i16le(payload + 1); // 0.0001 rad per bit, signed
    out->yaw_valid = ((uint16_t)yaw != 0x7FFFu);
    out->yaw_rad = (float)yaw * 0.0001f;
    int16_t pitch = rd_i16le(payload + 3);
    out->pitch_valid = ((uint16_t)pitch != 0x7FFFu);
    out->pitch_rad = (float)pitch * 0.0001f;
    int16_t roll = rd_i16le(payload + 5);
    out->roll_valid = ((uint16_t)roll != 0x7FFFu);
    out->roll_rad = (float)roll * 0.0001f;
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_engine_dynamic(const uint8_t *payload, size_t len, N2kEngineDynamic *out)
{
    if (!payload || !out || len < 26)
    {
        return PROTO_FALSE;
    }
    out->instance = payload[0];
    uint16_t oilp = rd_u16le(payload + 1); // 100 Pa per bit
    out->oil_pressure_valid = (oilp != 0xFFFFu);
    out->oil_pressure_pa = (float)oilp * 100.0f;
    uint16_t oilt = rd_u16le(payload + 3); // 0.1 K per bit
    out->oil_temp_valid = (oilt != 0xFFFFu);
    out->oil_temp_c = (float)oilt * 0.1f - 273.15f;
    uint16_t clt = rd_u16le(payload + 5); // 0.01 K per bit
    out->coolant_temp_valid = (clt != 0xFFFFu);
    out->coolant_temp_c = (float)clt * 0.01f - 273.15f;
    int16_t alt = rd_i16le(payload + 7); // 0.01 V per bit, signed
    out->alt_voltage_valid = ((uint16_t)alt != 0x7FFFu);
    out->alt_voltage_v = (float)alt * 0.01f;
    int16_t fr = rd_i16le(payload + 9); // 0.1 L/h per bit, signed
    out->fuel_rate_valid = ((uint16_t)fr != 0x7FFFu);
    out->fuel_rate_lph = (float)fr * 0.1f;
    uint32_t hrs = rd_u32le(payload + 11); // 1 s per bit
    out->engine_hours_valid = (hrs != 0xFFFFFFFFu);
    out->engine_hours_s = hrs;
    uint16_t clp = rd_u16le(payload + 15); // 100 Pa per bit
    out->coolant_pressure_valid = (clp != 0xFFFFu);
    out->coolant_pressure_pa = (float)clp * 100.0f;
    uint16_t fp = rd_u16le(payload + 17); // 1000 Pa per bit
    out->fuel_pressure_valid = (fp != 0xFFFFu);
    out->fuel_pressure_pa = (float)fp * 1000.0f;
    // payload[19] is reserved
    out->discrete_status_1 = rd_u16le(payload + 20);
    out->discrete_status_2 = rd_u16le(payload + 22);
    out->load_valid = (payload[24] != 0x7Fu); // signed 1-octet, 0x7F = not-available
    out->load_pct = (int8_t)payload[24];
    out->torque_valid = (payload[25] != 0x7Fu);
    out->torque_pct = (int8_t)payload[25];
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_wind_data(const uint8_t *payload, size_t len, N2kWindData *out)
{
    if (!payload || !out || len < 6)
    {
        return PROTO_FALSE;
    }
    out->sid = payload[0];
    uint16_t speed = rd_u16le(payload + 1); // 0.01 m/s per bit
    uint16_t angle = rd_u16le(payload + 3); // 0.0001 rad per bit
    out->speed_valid = (speed != 0xFFFFu);
    out->speed_mps = (float)speed * 0.01f;
    out->angle_valid = (angle != 0xFFFFu);
    out->angle_rad = (float)angle * 0.0001f;
    out->reference = (uint8_t)(payload[5] & 0x07u);
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_speed(const uint8_t *payload, size_t len, N2kSpeed *out)
{
    if (!payload || !out || len < 6) // sid(1) + water(2) + ground(2) + type(1)
    {
        return PROTO_FALSE;
    }
    out->sid = payload[0];
    uint16_t w = rd_u16le(payload + 1); // 0.01 m/s per bit
    out->water_valid = (w != 0xFFFFu);
    out->water_mps = (float)w * 0.01f;
    uint16_t g = rd_u16le(payload + 3);
    out->ground_valid = (g != 0xFFFFu);
    out->ground_mps = (float)g * 0.01f;
    out->water_ref_type = payload[5];
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_water_depth(const uint8_t *payload, size_t len, N2kWaterDepth *out)
{
    if (!payload || !out || len < 7) // SID(1) + depth(4) + offset(2)
    {
        return PROTO_FALSE;
    }
    out->sid = payload[0];
    uint32_t depth = rd_u32le(payload + 1); // 0.01 m per bit
    out->depth_valid = (depth != 0xFFFFFFFFu);
    out->depth_m = (float)depth * 0.01f;
    out->offset_m = (float)rd_i16le(payload + 5) * 0.001f; // 0.001 m per bit
    return PROTO_TRUE;
}

proto_bool pc_n2k_decode_vessel_heading(const uint8_t *payload, size_t len, N2kVesselHeading *out)
{
    if (!payload || !out || len < 8)
    {
        return PROTO_FALSE;
    }
    out->sid = payload[0];
    uint16_t heading = rd_u16le(payload + 1); // 0.0001 rad per bit
    out->heading_valid = (heading != 0xFFFFu);
    out->heading_rad = (float)heading * 0.0001f;
    out->deviation_rad = (float)rd_i16le(payload + 3) * 0.0001f;
    out->variation_rad = (float)rd_i16le(payload + 5) * 0.0001f;
    out->reference = (uint8_t)(payload[7] & 0x03u);
    return PROTO_TRUE;
}

#endif // PC_ENABLE_NMEA2000
