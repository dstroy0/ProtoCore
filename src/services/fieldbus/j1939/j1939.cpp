// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file j1939.cpp
 * @brief SAE J1939 message codec (pure, host-tested).
 */

#include "services/fieldbus/j1939/j1939.h"

#if PC_ENABLE_J1939

#include <string.h>

bool pc_j1939_encode_id(uint32_t *id, uint8_t priority, uint32_t pgn, uint8_t sa, uint8_t da)
{
    if (!id || priority > 7 || pgn > 0x3FFFFu)
    {
        return false;
    }
    uint8_t edp = (uint8_t)((pgn >> 17) & 1u);
    uint8_t dp = (uint8_t)((pgn >> 16) & 1u);
    uint8_t pf = (uint8_t)((pgn >> 8) & 0xFFu);
    // PDU1 (peer-to-peer) carries the destination address in PS; PDU2 (broadcast) carries
    // the PGN's group-extension low octet there.
    uint8_t ps = (pf < J1939_PDU2_THRESHOLD) ? da : (uint8_t)(pgn & 0xFFu);
    *id = ((uint32_t)(priority & 7u) << 26) | ((uint32_t)edp << 25) | ((uint32_t)dp << 24) | ((uint32_t)pf << 16) |
          ((uint32_t)ps << 8) | (uint32_t)sa;
    return true;
}

bool pc_j1939_decode_id(uint32_t id, J1939Id *out)
{
    if (!out)
    {
        return false;
    }
    id &= PC_CAN_EXT_ID_MASK;
    out->priority = (uint8_t)((id >> 26) & 7u);
    uint8_t edp = (uint8_t)((id >> 25) & 1u);
    uint8_t dp = (uint8_t)((id >> 24) & 1u);
    out->pf = (uint8_t)((id >> 16) & 0xFFu);
    out->ps = (uint8_t)((id >> 8) & 0xFFu);
    out->sa = (uint8_t)(id & 0xFFu);
    out->pdu1 = out->pf < J1939_PDU2_THRESHOLD;
    if (out->pdu1)
    {
        out->da = out->ps;
        out->pgn = ((uint32_t)edp << 17) | ((uint32_t)dp << 16) | ((uint32_t)out->pf << 8);
    }
    else
    {
        out->da = J1939_ADDR_GLOBAL;
        out->pgn = ((uint32_t)edp << 17) | ((uint32_t)dp << 16) | ((uint32_t)out->pf << 8) | out->ps;
    }
    return true;
}

// Fill a CanFrame as a 29-bit extended frame.
static bool ext_frame(CanFrame *f, uint8_t priority, uint32_t pgn, uint8_t sa, uint8_t da, uint8_t dlc)
{
    uint32_t id;
    if (!pc_j1939_encode_id(&id, priority, pgn, sa, da))
    {
        return false;
    }
    f->id = id;
    f->extended = true;
    f->rtr = false;
    f->dlc = dlc;
    memset(f->data, 0xFF, sizeof(f->data)); // J1939 pads unused octets with 0xFF (not available)
    return true;
}

bool pc_j1939_build_message(CanFrame *out, uint8_t priority, uint32_t pgn, uint8_t sa, uint8_t da, const uint8_t *data,
                            uint8_t len)
{
    if (!out || len > PC_CAN_MAX_DLC || (len && !data))
    {
        return false;
    }
    if (!ext_frame(out, priority, pgn, sa, da, len))
    {
        return false;
    }
    if (len)
    {
        memcpy(out->data, data, len);
    }
    return true;
}

bool pc_j1939_build_request(CanFrame *out, uint8_t sa, uint8_t da, uint32_t requested_pgn)
{
    if (!out || requested_pgn > 0x3FFFFu)
    {
        return false;
    }
    // Request PGN (priority 6): 3-octet little-endian requested PGN.
    if (!ext_frame(out, 6, J1939_PGN_REQUEST, sa, da, 3)) // GCOVR_EXCL_LINE  unreachable: fixed priority 6 +
                                                          // J1939_PGN_REQUEST (<=0x3FFFF), encode can't fail
    {
        return false; // GCOVR_EXCL_LINE  unreachable: fixed priority 6 + J1939_PGN_REQUEST (<=0x3FFFF), encode can't
                      // fail
    }
    out->data[0] = (uint8_t)requested_pgn;
    out->data[1] = (uint8_t)(requested_pgn >> 8);
    out->data[2] = (uint8_t)(requested_pgn >> 16);
    return true;
}

uint64_t pc_j1939_build_name(bool arbitrary_address_capable, uint8_t industry_group, uint8_t vehicle_system_instance,
                             uint8_t vehicle_system, uint8_t function, uint8_t function_instance, uint8_t ecu_instance,
                             uint16_t manufacturer_code, uint32_t identity_number)
{
    // NAME bit layout (J1939-81), LSB first:
    //  [0..20] identity number, [21..31] manufacturer code, [32..34] ECU instance,
    //  [35..39] function instance, [40..47] function, [48] reserved, [49..55] vehicle system,
    //  [56..59] vehicle system instance, [60..62] industry group, [63] arbitrary-address-capable.
    uint64_t n = 0;
    n |= (uint64_t)(identity_number & 0x1FFFFFu);
    n |= (uint64_t)(manufacturer_code & 0x7FFu) << 21;
    n |= (uint64_t)(ecu_instance & 0x7u) << 32;
    n |= (uint64_t)(function_instance & 0x1Fu) << 35;
    n |= (uint64_t)(function & 0xFFu) << 40;
    n |= (uint64_t)(vehicle_system & 0x7Fu) << 49;
    n |= (uint64_t)(vehicle_system_instance & 0xFu) << 56;
    n |= (uint64_t)(industry_group & 0x7u) << 60;
    n |= (uint64_t)(arbitrary_address_capable ? 1u : 0u) << 63;
    return n;
}

bool pc_j1939_build_address_claim(CanFrame *out, uint8_t sa, uint64_t name)
{
    // Address Claimed (priority 6, broadcast): NAME as 8 octets, little-endian.
    if (!ext_frame(out, 6, J1939_PGN_ADDRESS_CLAIM, sa, J1939_ADDR_GLOBAL, 8)) // GCOVR_EXCL_LINE  unreachable: fixed
                                                                               // priority 6 + PGN_ADDRESS_CLAIM
                                                                               // (<=0x3FFFF), encode can't fail
    {
        return false; // GCOVR_EXCL_LINE  unreachable: fixed priority 6 + J1939_PGN_ADDRESS_CLAIM (<=0x3FFFF), encode
                      // can't fail
    }
    for (int i = 0; i < 8; i++)
    {
        out->data[i] = (uint8_t)(name >> (8 * i));
    }
    return true;
}

uint8_t pc_j1939_tp_num_packets(uint16_t total_size)
{
    return (uint8_t)((total_size + (J1939_TP_DT_LEN - 1)) / J1939_TP_DT_LEN);
}

bool pc_j1939_build_bam_cm(CanFrame *out, uint8_t sa, uint32_t pgn, uint16_t total_size)
{
    if (!out || total_size < 9 || total_size > PC_J1939_TP_MAX || pgn > 0x3FFFFu)
    {
        return false; // BAM is for 9..1785 octet messages
    }
    if (!ext_frame(out, 7, J1939_PGN_TP_CM, sa, J1939_ADDR_GLOBAL, 8)) // GCOVR_EXCL_LINE  unreachable: fixed priority
                                                                       // 7 + J1939_PGN_TP_CM (<=0x3FFFF), encode
                                                                       // can't fail
    {
        return false; // GCOVR_EXCL_LINE  unreachable: fixed priority 7 + J1939_PGN_TP_CM (<=0x3FFFF), encode can't fail
    }
    out->data[0] = J1939_TP_CM_BAM;
    out->data[1] = (uint8_t)total_size; // message size, little-endian
    out->data[2] = (uint8_t)(total_size >> 8);
    out->data[3] = pc_j1939_tp_num_packets(total_size); // total packets
    out->data[4] = 0xFF;                                // reserved
    out->data[5] = (uint8_t)pgn;                        // transported PGN, little-endian
    out->data[6] = (uint8_t)(pgn >> 8);
    out->data[7] = (uint8_t)(pgn >> 16);
    return true;
}

bool pc_j1939_build_tp_dt(CanFrame *out, uint8_t sa, uint8_t da, uint8_t seq, const uint8_t *chunk, uint8_t chunk_len)
{
    if (!out || seq == 0 || chunk_len == 0 || chunk_len > J1939_TP_DT_LEN || !chunk)
    {
        return false;
    }
    if (!ext_frame(out, 7, J1939_PGN_TP_DT, sa, da, 8)) // GCOVR_EXCL_LINE  unreachable: fixed priority 7 +
                                                        // J1939_PGN_TP_DT (<=0x3FFFF), encode can't fail
    {
        return false; // GCOVR_EXCL_LINE  unreachable: fixed priority 7 + J1939_PGN_TP_DT (<=0x3FFFF), encode can't fail
    }
    out->data[0] = seq;                      // sequence number, 1-based
    memcpy(out->data + 1, chunk, chunk_len); // remaining octets stay 0xFF padding
    return true;
}

void pc_j1939_tp_reset(J1939TpRx *rx)
{
    if (rx)
    {
        memset(rx, 0, sizeof(*rx));
    }
}

J1939TpResult pc_j1939_tp_feed(J1939TpRx *rx, const CanFrame *f)
{
    if (!rx || !f || !f->extended)
    {
        return J1939TpResult::J1939_TP_IGNORED;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id)) // GCOVR_EXCL_LINE  unreachable: decode_id only fails on a null out, and &id
                                         // is non-null
    {
        return J1939TpResult::J1939_TP_IGNORED; // GCOVR_EXCL_LINE  unreachable: decode_id only fails on a null out, and
                                                // &id is non-null
    }

    if (id.pgn == J1939_PGN_TP_CM && f->dlc >= 8)
    {
        uint8_t control = f->data[0];
        if (control != J1939_TP_CM_BAM && control != J1939_TP_CM_RTS)
        {
            return J1939TpResult::J1939_TP_IGNORED; // CTS / EOM / Abort are not receiver-side session starts
        }
        uint16_t total = (uint16_t)(f->data[1] | (f->data[2] << 8));
        uint8_t packets = f->data[3];
        uint32_t pgn = (uint32_t)f->data[5] | ((uint32_t)f->data[6] << 8) | ((uint32_t)f->data[7] << 16);
        if (total < 9 || total > PC_J1939_TP_MAX || packets != pc_j1939_tp_num_packets(total))
        {
            return J1939TpResult::J1939_TP_ERROR;
        }
        rx->active = true;
        rx->sa = id.sa;
        rx->pgn = pgn;
        rx->total_size = total;
        rx->num_packets = packets;
        rx->next_seq = 1;
        rx->received = 0;
        return J1939TpResult::J1939_TP_STARTED;
    }

    if (id.pgn == J1939_PGN_TP_DT && f->dlc >= 1)
    {
        if (!rx->active || id.sa != rx->sa)
        {
            return J1939TpResult::J1939_TP_IGNORED;
        }
        uint8_t seq = f->data[0];
        if (seq != rx->next_seq)
        {
            pc_j1939_tp_reset(rx);
            return J1939TpResult::J1939_TP_ERROR; // out-of-sequence: abort the session
        }
        uint16_t remaining = (uint16_t)(rx->total_size - rx->received);
        uint8_t take = remaining < J1939_TP_DT_LEN ? (uint8_t)remaining : (uint8_t)J1939_TP_DT_LEN;
        memcpy(rx->buf + rx->received, f->data + 1, take);
        rx->received = (uint16_t)(rx->received + take);
        rx->next_seq++;
        if (rx->received >= rx->total_size)
        {
            rx->active = false;
            return J1939TpResult::J1939_TP_COMPLETE;
        }
        return J1939TpResult::J1939_TP_PROGRESS;
    }

    return J1939TpResult::J1939_TP_IGNORED;
}

// --- typed decoders (SAE J1939-71) ---

bool pc_j1939_decode_eec1(const CanFrame *f, J1939Eec1 *out)
{
    if (!f || !out || f->dlc < 8)
    {
        return false;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id) || id.pgn != J1939_PGN_EEC1)
    {
        return false;
    }
    out->torque_mode = (uint8_t)(f->data[0] & 0x0Fu);
    // percent torque: raw 0..250 maps to -125..+125 %; 0xFB..0xFF is error / not-available.
    out->drivers_demand_torque_pct = (f->data[1] <= 0xFAu) ? (int16_t)((int)f->data[1] - 125) : J1939_TORQUE_NA;
    out->actual_engine_torque_pct = (f->data[2] <= 0xFAu) ? (int16_t)((int)f->data[2] - 125) : J1939_TORQUE_NA;
    uint16_t raw = (uint16_t)(f->data[3] | ((uint16_t)f->data[4] << 8)); // little-endian
    out->engine_speed_valid = (raw <= 0xFAFFu);                          // >= 0xFB00 is error / not-available
    out->engine_speed_rpm = (float)raw * 0.125f;
    return true;
}

bool pc_j1939_decode_et1(const CanFrame *f, J1939Et1 *out)
{
    if (!f || !out || f->dlc < 8)
    {
        return false;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id) || id.pgn != J1939_PGN_ET1)
    {
        return false;
    }
    out->coolant_valid = (f->data[0] <= 0xFAu);
    out->coolant_temp_c = (float)((int)f->data[0] - 40); // 1 degC/bit, -40 offset
    out->fuel_valid = (f->data[1] <= 0xFAu);
    out->fuel_temp_c = (float)((int)f->data[1] - 40);
    uint16_t oilraw = (uint16_t)(f->data[2] | ((uint16_t)f->data[3] << 8));
    out->oil_valid = (oilraw <= 0xFAFFu);
    out->oil_temp_c = (float)oilraw * 0.03125f - 273.0f; // 0.03125 degC/bit, -273 offset
    return true;
}

bool pc_j1939_decode_lfe(const CanFrame *f, J1939Lfe *out)
{
    if (!f || !out || f->dlc < 8)
    {
        return false;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id) || id.pgn != J1939_PGN_LFE)
    {
        return false;
    }
    uint16_t fr = (uint16_t)(f->data[0] | ((uint16_t)f->data[1] << 8)); // SPN 183, 0.05 L/h/bit
    out->fuel_rate_valid = (fr <= 0xFAFFu);
    out->fuel_rate_lph = (float)fr * 0.05f;
    uint16_t ie = (uint16_t)(f->data[2] | ((uint16_t)f->data[3] << 8)); // SPN 184, 1/512 km/L per bit
    out->instant_econ_valid = (ie <= 0xFAFFu);
    out->instant_econ_kmpl = (float)ie * (1.0f / 512.0f);
    uint16_t ae = (uint16_t)(f->data[4] | ((uint16_t)f->data[5] << 8)); // SPN 185, 1/512 km/L per bit
    out->avg_econ_valid = (ae <= 0xFAFFu);
    out->avg_econ_kmpl = (float)ae * (1.0f / 512.0f);
    out->throttle_valid = (f->data[6] <= 0xFAu); // SPN 51, 0.4 %/bit
    out->throttle_pct = (float)f->data[6] * 0.4f;
    return true;
}

bool pc_j1939_decode_amb(const CanFrame *f, J1939Amb *out)
{
    if (!f || !out || f->dlc < 8)
    {
        return false;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id) || id.pgn != J1939_PGN_AMB)
    {
        return false;
    }
    out->baro_valid = (f->data[0] <= 0xFAu); // SPN 108, 0.5 kPa/bit
    out->baro_kpa = (float)f->data[0] * 0.5f;
    uint16_t cab = (uint16_t)(f->data[1] | ((uint16_t)f->data[2] << 8)); // SPN 170
    out->cab_temp_valid = (cab <= 0xFAFFu);
    out->cab_temp_c = (float)cab * 0.03125f - 273.0f;                    // 0.03125 degC/bit, -273 offset
    uint16_t amb = (uint16_t)(f->data[3] | ((uint16_t)f->data[4] << 8)); // SPN 171
    out->ambient_temp_valid = (amb <= 0xFAFFu);
    out->ambient_temp_c = (float)amb * 0.03125f - 273.0f;
    out->inlet_temp_valid = (f->data[5] <= 0xFAu); // SPN 172, 1 degC/bit, -40 offset
    out->inlet_temp_c = (float)((int)f->data[5] - 40);
    uint16_t road = (uint16_t)(f->data[6] | ((uint16_t)f->data[7] << 8)); // SPN 79
    out->road_temp_valid = (road <= 0xFAFFu);
    out->road_temp_c = (float)road * 0.03125f - 273.0f;
    return true;
}

bool pc_j1939_decode_ic1(const CanFrame *f, J1939Ic1 *out)
{
    if (!f || !out || f->dlc < 8)
    {
        return false;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id) || id.pgn != J1939_PGN_IC1)
    {
        return false;
    }
    out->trap_inlet_valid = (f->data[0] <= 0xFAu); // SPN 81, 0.5 kPa/bit
    out->trap_inlet_kpa = (float)f->data[0] * 0.5f;
    out->boost_valid = (f->data[1] <= 0xFAu); // SPN 102, 2 kPa/bit
    out->boost_kpa = (float)f->data[1] * 2.0f;
    out->intake_temp_valid = (f->data[2] <= 0xFAu); // SPN 105, 1 degC/bit, -40 offset
    out->intake_temp_c = (float)((int)f->data[2] - 40);
    out->air_inlet_valid = (f->data[3] <= 0xFAu); // SPN 106, 2 kPa/bit
    out->air_inlet_kpa = (float)f->data[3] * 2.0f;
    out->air_filter_valid = (f->data[4] <= 0xFAu); // SPN 107, 0.05 kPa/bit
    out->air_filter_kpa = (float)f->data[4] * 0.05f;
    uint16_t egt = (uint16_t)(f->data[5] | ((uint16_t)f->data[6] << 8)); // SPN 173
    out->exhaust_temp_valid = (egt <= 0xFAFFu);
    out->exhaust_temp_c = (float)egt * 0.03125f - 273.0f; // 0.03125 degC/bit, -273 offset
    out->coolant_filter_valid = (f->data[7] <= 0xFAu);    // SPN 112, 0.5 kPa/bit
    out->coolant_filter_kpa = (float)f->data[7] * 0.5f;
    return true;
}

bool pc_j1939_decode_vd(const CanFrame *f, J1939Vd *out)
{
    if (!f || !out || f->dlc < 8)
    {
        return false;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id) || id.pgn != J1939_PGN_VD)
    {
        return false;
    }
    // SPN 244 trip distance + SPN 245 total distance: 4-octet little-endian, 0.125 km/bit.
    uint32_t trip = (uint32_t)f->data[0] | ((uint32_t)f->data[1] << 8) | ((uint32_t)f->data[2] << 16) |
                    ((uint32_t)f->data[3] << 24);
    out->trip_valid = (trip <= 0xFAFFFFFFu); // >= 0xFB000000 is error / not-available
    out->trip_km = (double)trip * 0.125;
    uint32_t total = (uint32_t)f->data[4] | ((uint32_t)f->data[5] << 8) | ((uint32_t)f->data[6] << 16) |
                     ((uint32_t)f->data[7] << 24);
    out->total_valid = (total <= 0xFAFFFFFFu);
    out->total_km = (double)total * 0.125;
    return true;
}

bool pc_j1939_decode_ccvs(const CanFrame *f, J1939Ccvs *out)
{
    if (!f || !out || f->dlc < 8)
    {
        return false;
    }
    J1939Id id;
    if (!pc_j1939_decode_id(f->id, &id) || id.pgn != J1939_PGN_CCVS)
    {
        return false;
    }
    // SPN 84 Wheel-Based Vehicle Speed: bytes 2-3, little-endian, 1/256 km/h per bit, 0 offset.
    uint16_t ws = (uint16_t)(f->data[1] | ((uint16_t)f->data[2] << 8));
    out->speed_valid = (ws <= 0xFAFFu); // >= 0xFB00 is error / not-available
    out->wheel_speed_kmh = (float)ws * (1.0f / 256.0f);
    // SPN 595 Cruise Control Active: byte 4, bits 1-2 (the low 2 bits) - a 2-bit state.
    out->cruise_active = (uint8_t)(f->data[3] & 0x03u);
    return true;
}

bool pc_j1939_decode_dm1(const uint8_t *body, size_t len, J1939Dm1 *out, J1939Dtc *out_dtcs, size_t max)
{
    if (!body || !out || len < 2) // the lamp-status + flash-status octets
    {
        return false;
    }
    // Lamp status (J1939-73): 2 bits each, protect (0-1) / amber (2-3) / red-stop (4-5) / MIL (6-7).
    out->protect = (uint8_t)(body[0] & 0x03u);
    out->amber_warning = (uint8_t)((body[0] >> 2) & 0x03u);
    out->red_stop = (uint8_t)((body[0] >> 4) & 0x03u);
    out->mil = (uint8_t)((body[0] >> 6) & 0x03u);
    // body[1] is the flash status (same 2-bit layout); not decoded here.
    size_t ndtc = (len - 2) / 4; // whole 4-octet DTC blocks after the two status octets
    uint8_t stored = 0;
    for (size_t i = 0; i < ndtc; i++)
    {
        if (stored >= max) // the output buffer is full; stop scanning further DTC blocks
        {
            break;
        }
        const uint8_t *d = body + 2 + i * 4;
        uint32_t spn = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | (((uint32_t)d[2] >> 5) << 16);
        uint8_t fmi = (uint8_t)(d[2] & 0x1Fu);
        if (spn == 0 && fmi == 0) // the "no active DTC" placeholder
        {
            continue;
        }
        if (out_dtcs)
        {
            out_dtcs[stored].spn = spn;
            out_dtcs[stored].fmi = fmi;
            out_dtcs[stored].cm = (uint8_t)((d[3] >> 7) & 0x01u);
            out_dtcs[stored].oc = (uint8_t)(d[3] & 0x7Fu);
        }
        stored++;
    }
    out->dtc_count = stored;
    return true;
}

#endif // PC_ENABLE_J1939
