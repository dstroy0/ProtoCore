// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file j1939.c
 * @brief SAE J1939 message codec (pure, host-tested).
 */

#include "services/fieldbus/j1939/j1939.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "shared/can/can.h"

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_NEED_J1939

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_J1939_BORROW persistent bytes
} J1939OwnCtx;
static J1939OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_j1939_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_J1939_BORROW).buf;
    }
    return s_own.span;
}

void protocore_j1939_decode_id(uint8_t *restrict work);
void protocore_j1939_tp_num_packets(uint8_t *restrict work);
void protocore_j1939_tp_reset(uint8_t *restrict work);

void protocore_j1939_encode_id(uint8_t *restrict work)
{
    (void)work;
    uint32_t *id = J1939V.encode_id_args.id;
    uint8_t priority = J1939V.encode_id_args.priority;
    uint32_t pgn = J1939V.encode_id_args.pgn;
    uint8_t sa = J1939V.encode_id_args.sa;
    uint8_t da = J1939V.encode_id_args.da;

    if (!id || priority > 7 || pgn > 0x3FFFFu)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    uint8_t edp = (uint8_t)((pgn >> 17) & 1u);
    uint8_t dp = (uint8_t)((pgn >> 16) & 1u);
    uint8_t pf = (uint8_t)((pgn >> 8) & 0xFFu);
    // PDU1 (peer-to-peer) carries the destination address in PS; PDU2 (broadcast) carries
    // the PGN's group-extension low octet there.
    uint8_t ps = (pf < J1939_PDU2_THRESHOLD) ? da : (uint8_t)(pgn & 0xFFu);
    *id = ((uint32_t)(priority & 7u) << 26) | ((uint32_t)edp << 25) | ((uint32_t)dp << 24) | ((uint32_t)pf << 16) |
          ((uint32_t)ps << 8) | (uint32_t)sa;
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_decode_id(uint8_t *restrict work)
{
    (void)work;
    uint32_t id = J1939V.decode_id_args.id;
    J1939Id *out = J1939V.decode_id_args.out;

    if (!out)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    id &= PROTOCORE_CAN_EXT_ID_MASK;
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
    J1939V.ok = PROTO_TRUE;
}

// The operands the private helper below reads, all of them: the widest set any one caller supplies.
typedef struct
{
    CanFrame *f;      ///< the frame being filled
    uint32_t pgn;     ///< its parameter group number
    uint8_t priority; ///< its 3-bit priority
    uint8_t sa;       ///< its source address
    uint8_t da;       ///< its destination address
    uint8_t dlc;      ///< its data length
} J1939Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define J1939_OFF_CTX 0u
static_assert(J1939_OFF_CTX + sizeof(J1939Ctx) <= PROTOCORE_J1939_BORROW,
              "PROTOCORE_J1939_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(J1939_OFF_CTX % _Alignof(J1939Ctx) == 0,
              "J1939_OFF_CTX is not a multiple of alignof(J1939Ctx) - J1939_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define J1939_CTX(w) ((J1939Ctx *)(void *)((w) + J1939_OFF_CTX))

// Fill a CanFrame as a 29-bit extended frame, from the operands on the context.
static proto_bool ext_frame(uint8_t *restrict work)
{
    uint32_t id;
    J1939V.encode_id_args.id = &id;
    J1939V.encode_id_args.priority = J1939_CTX(work)->priority;
    J1939V.encode_id_args.pgn = J1939_CTX(work)->pgn;
    J1939V.encode_id_args.sa = J1939_CTX(work)->sa;
    J1939V.encode_id_args.da = J1939_CTX(work)->da;
    protocore_j1939_encode_id(work);
    if (!J1939V.ok)
    {
        return PROTO_FALSE;
    }
    J1939_CTX(work)->f->id = id;
    J1939_CTX(work)->f->extended = PROTO_TRUE;
    J1939_CTX(work)->f->rtr = PROTO_FALSE;
    J1939_CTX(work)->f->dlc = J1939_CTX(work)->dlc;
    mem.set(J1939_CTX(work)->f->data, 0xFF,
            sizeof(J1939_CTX(work)->f->data)); // J1939 pads unused octets with 0xFF (not available)
    return PROTO_TRUE;
}

void protocore_j1939_build_message(uint8_t *restrict work)
{
    CanFrame *out = J1939V.build_message_args.out;
    uint8_t priority = J1939V.build_message_args.priority;
    uint32_t pgn = J1939V.build_message_args.pgn;
    uint8_t sa = J1939V.build_message_args.sa;
    uint8_t da = J1939V.build_message_args.da;
    const uint8_t *data = J1939V.build_message_args.data;
    uint8_t len = J1939V.build_message_args.len;

    if (!out || len > PROTOCORE_CAN_MAX_DLC || (len && !data))
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939_CTX(work)->f = out;
    J1939_CTX(work)->priority = priority;
    J1939_CTX(work)->pgn = pgn;
    J1939_CTX(work)->sa = sa;
    J1939_CTX(work)->da = da;
    J1939_CTX(work)->dlc = len;
    if (!ext_frame(work))
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    if (len)
    {
        mem.cpy(out->data, data, len);
    }
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_build_request(uint8_t *restrict work)
{
    CanFrame *out = J1939V.build_request_args.out;
    uint8_t sa = J1939V.build_request_args.sa;
    uint8_t da = J1939V.build_request_args.da;
    uint32_t requested_pgn = J1939V.build_request_args.requested_pgn;

    if (!out || requested_pgn > 0x3FFFFu)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    // Request PGN (priority 6): 3-octet little-endian requested PGN.
    J1939_CTX(work)->f = out;
    J1939_CTX(work)->priority = 6;
    J1939_CTX(work)->pgn = J1939_PGN_REQUEST;
    J1939_CTX(work)->sa = sa;
    J1939_CTX(work)->da = da;
    J1939_CTX(work)->dlc = 3;
    if (!ext_frame(work))
    // J1939_PGN_REQUEST (<=0x3FFFF), encode can't fail
    {
        J1939V.ok = PROTO_FALSE;
        return;
        // can't fail
    }
    out->data[0] = (uint8_t)requested_pgn;
    out->data[1] = (uint8_t)(requested_pgn >> 8);
    out->data[2] = (uint8_t)(requested_pgn >> 16);
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_build_name(uint8_t *restrict work)
{
    (void)work;
    proto_bool arbitrary_address_capable = J1939V.build_name_args.arbitrary_address_capable;
    uint8_t industry_group = J1939V.build_name_args.industry_group;
    uint8_t vehicle_system_instance = J1939V.build_name_args.vehicle_system_instance;
    uint8_t vehicle_system = J1939V.build_name_args.vehicle_system;
    uint8_t function = J1939V.build_name_args.function;
    uint8_t function_instance = J1939V.build_name_args.function_instance;
    uint8_t ecu_instance = J1939V.build_name_args.ecu_instance;
    uint16_t manufacturer_code = J1939V.build_name_args.manufacturer_code;
    uint32_t identity_number = J1939V.build_name_args.identity_number;

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
    J1939V.value = n;
}

void protocore_j1939_build_address_claim(uint8_t *restrict work)
{
    CanFrame *out = J1939V.build_address_claim_args.out;
    uint8_t sa = J1939V.build_address_claim_args.sa;
    uint64_t name = J1939V.build_address_claim_args.name;

    // Address Claimed (priority 6, broadcast): NAME as 8 octets, little-endian.
    J1939_CTX(work)->f = out;
    J1939_CTX(work)->priority = 6;
    J1939_CTX(work)->pgn = J1939_PGN_ADDRESS_CLAIM;
    J1939_CTX(work)->sa = sa;
    J1939_CTX(work)->da = J1939_ADDR_GLOBAL;
    J1939_CTX(work)->dlc = 8;
    if (!ext_frame(work))
    // priority 6 + PGN_ADDRESS_CLAIM
    // (<=0x3FFFF), encode can't fail
    {
        J1939V.ok = PROTO_FALSE;
        return;
        // encode can't fail
    }
    for (int i = 0; i < 8; i++)
    {
        out->data[i] = (uint8_t)(name >> (8 * i));
    }
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_tp_num_packets(uint8_t *restrict work)
{
    (void)work;
    uint16_t total_size = J1939V.tp_num_packets_args.total_size;

    J1939V.u8 = (uint8_t)((total_size + (J1939_TP_DT_LEN - 1)) / J1939_TP_DT_LEN);
}

void protocore_j1939_build_bam_cm(uint8_t *restrict work)
{
    CanFrame *out = J1939V.build_bam_cm_args.out;
    uint8_t sa = J1939V.build_bam_cm_args.sa;
    uint32_t pgn = J1939V.build_bam_cm_args.pgn;
    uint16_t total_size = J1939V.build_bam_cm_args.total_size;

    if (!out || total_size < 9 || total_size > PROTOCORE_J1939_TP_MAX || pgn > 0x3FFFFu)
    {
        J1939V.ok = PROTO_FALSE; // BAM is for 9..1785 octet messages
        return;
    }
    J1939_CTX(work)->f = out;
    J1939_CTX(work)->priority = 7;
    J1939_CTX(work)->pgn = J1939_PGN_TP_CM;
    J1939_CTX(work)->sa = sa;
    J1939_CTX(work)->da = J1939_ADDR_GLOBAL;
    J1939_CTX(work)->dlc = 8;
    if (!ext_frame(work))
    // 7 + J1939_PGN_TP_CM (<=0x3FFFF), encode
    // can't fail
    {
        J1939V.ok = PROTO_FALSE;
        return;
        // can't fail
    }
    out->data[0] = J1939_TP_CM_BAM;
    out->data[1] = (uint8_t)total_size; // message size, little-endian
    out->data[2] = (uint8_t)(total_size >> 8);
    J1939V.tp_num_packets_args.total_size = total_size;
    protocore_j1939_tp_num_packets(work);
    out->data[3] = J1939V.u8;    // total packets
    out->data[4] = 0xFF;         // reserved
    out->data[5] = (uint8_t)pgn; // transported PGN, little-endian
    out->data[6] = (uint8_t)(pgn >> 8);
    out->data[7] = (uint8_t)(pgn >> 16);
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_build_tp_dt(uint8_t *restrict work)
{
    CanFrame *out = J1939V.build_tp_dt_args.out;
    uint8_t sa = J1939V.build_tp_dt_args.sa;
    uint8_t da = J1939V.build_tp_dt_args.da;
    uint8_t seq = J1939V.build_tp_dt_args.seq;
    const uint8_t *chunk = J1939V.build_tp_dt_args.chunk;
    uint8_t chunk_len = J1939V.build_tp_dt_args.chunk_len;

    if (!out || seq == 0 || chunk_len == 0 || chunk_len > J1939_TP_DT_LEN || !chunk)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939_CTX(work)->f = out;
    J1939_CTX(work)->priority = 7;
    J1939_CTX(work)->pgn = J1939_PGN_TP_DT;
    J1939_CTX(work)->sa = sa;
    J1939_CTX(work)->da = da;
    J1939_CTX(work)->dlc = 8;
    if (!ext_frame(work))
    // J1939_PGN_TP_DT (<=0x3FFFF), encode can't fail
    {
        J1939V.ok = PROTO_FALSE;
        return;
        // can't fail
    }
    out->data[0] = seq;                       // sequence number, 1-based
    mem.cpy(out->data + 1, chunk, chunk_len); // remaining octets stay 0xFF padding
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_tp_reset(uint8_t *restrict work)
{
    (void)work;
    J1939TpRx *rx = J1939V.tp_reset_args.rx;

    if (rx)
    {
        mem.set(rx, 0, sizeof(*rx));
    }
}

void protocore_j1939_tp_feed(uint8_t *restrict work)
{
    J1939TpRx *rx = J1939V.tp_feed_args.rx;
    const CanFrame *f = J1939V.tp_feed_args.f;

    if (!rx || !f || !f->extended)
    {
        J1939V.tp = J1939_TP_IGNORED;
        return;
    }
    J1939Id id;
    J1939V.decode_id_args.id = f->id;
    J1939V.decode_id_args.out = &id;
    protocore_j1939_decode_id(work);
    if (!J1939V.ok)
    // is non-null
    {
        J1939V.tp = J1939_TP_IGNORED;
        return;
        // &id is non-null
    }

    if (id.pgn == J1939_PGN_TP_CM && f->dlc >= 8)
    {
        uint8_t control = f->data[0];
        if (control != J1939_TP_CM_BAM && control != J1939_TP_CM_RTS)
        {
            J1939V.tp = J1939_TP_IGNORED; // CTS / EOM / Abort are not receiver-side session starts
            return;
        }
        uint16_t total = (uint16_t)(f->data[1] | (f->data[2] << 8));
        uint8_t packets = f->data[3];
        uint32_t pgn = (uint32_t)f->data[5] | ((uint32_t)f->data[6] << 8) | ((uint32_t)f->data[7] << 16);
        if (total < 9 || total > PROTOCORE_J1939_TP_MAX)
        {
            J1939V.tp = J1939_TP_ERROR;
            return;
        }
        // Split from the bounds above rather than hoisted above them: the packet count is only
        // defined for a size in range.
        J1939V.tp_num_packets_args.total_size = total;
        protocore_j1939_tp_num_packets(work);
        if (packets != J1939V.u8)
        {
            J1939V.tp = J1939_TP_ERROR;
            return;
        }
        rx->active = PROTO_TRUE;
        rx->sa = id.sa;
        rx->pgn = pgn;
        rx->total_size = total;
        rx->num_packets = packets;
        rx->next_seq = 1;
        rx->received = 0;
        J1939V.tp = J1939_TP_STARTED;
        return;
    }

    if (id.pgn == J1939_PGN_TP_DT && f->dlc >= 1)
    {
        if (!rx->active || id.sa != rx->sa)
        {
            J1939V.tp = J1939_TP_IGNORED;
            return;
        }
        uint8_t seq = f->data[0];
        if (seq != rx->next_seq)
        {
            J1939V.tp_reset_args.rx = rx;
            protocore_j1939_tp_reset(work);
            J1939V.tp = J1939_TP_ERROR; // out-of-sequence: abort the session
            return;
        }
        uint16_t remaining = (uint16_t)(rx->total_size - rx->received);
        uint8_t take = remaining < J1939_TP_DT_LEN ? (uint8_t)remaining : (uint8_t)J1939_TP_DT_LEN;
        mem.cpy(rx->buf + rx->received, f->data + 1, take);
        rx->received = (uint16_t)(rx->received + take);
        rx->next_seq++;
        if (rx->received >= rx->total_size)
        {
            rx->active = PROTO_FALSE;
            J1939V.tp = J1939_TP_COMPLETE;
            return;
        }
        J1939V.tp = J1939_TP_PROGRESS;
        return;
    }

    J1939V.tp = J1939_TP_IGNORED;
}

// --- typed decoders (SAE J1939-71) ---

void protocore_j1939_decode_eec1(uint8_t *restrict work)
{
    const CanFrame *f = J1939V.decode_eec1_args.f;
    J1939Eec1 *out = J1939V.decode_eec1_args.out;

    if (!f || !out || f->dlc < 8)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939Id id;
    J1939V.decode_id_args.id = f->id;
    J1939V.decode_id_args.out = &id;
    protocore_j1939_decode_id(work);
    if (!J1939V.ok || id.pgn != J1939_PGN_EEC1)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    out->torque_mode = (uint8_t)(f->data[0] & 0x0Fu);
    // percent torque: raw 0..250 maps to -125..+125 %; 0xFB..0xFF is error / not-available.
    out->drivers_demand_torque_pct = (f->data[1] <= 0xFAu) ? (int16_t)((int)f->data[1] - 125) : J1939_TORQUE_NA;
    out->actual_engine_torque_pct = (f->data[2] <= 0xFAu) ? (int16_t)((int)f->data[2] - 125) : J1939_TORQUE_NA;
    uint16_t raw = (uint16_t)(f->data[3] | ((uint16_t)f->data[4] << 8)); // little-endian
    out->engine_speed_valid = (raw <= 0xFAFFu);                          // >= 0xFB00 is error / not-available
    out->engine_speed_rpm = (float)raw * 0.125f;
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_decode_et1(uint8_t *restrict work)
{
    const CanFrame *f = J1939V.decode_et1_args.f;
    J1939Et1 *out = J1939V.decode_et1_args.out;

    if (!f || !out || f->dlc < 8)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939Id id;
    J1939V.decode_id_args.id = f->id;
    J1939V.decode_id_args.out = &id;
    protocore_j1939_decode_id(work);
    if (!J1939V.ok || id.pgn != J1939_PGN_ET1)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    out->coolant_valid = (f->data[0] <= 0xFAu);
    out->coolant_temp_c = (float)((int)f->data[0] - 40); // 1 degC/bit, -40 offset
    out->fuel_valid = (f->data[1] <= 0xFAu);
    out->fuel_temp_c = (float)((int)f->data[1] - 40);
    uint16_t oilraw = (uint16_t)(f->data[2] | ((uint16_t)f->data[3] << 8));
    out->oil_valid = (oilraw <= 0xFAFFu);
    out->oil_temp_c = (float)oilraw * 0.03125f - 273.0f; // 0.03125 degC/bit, -273 offset
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_decode_lfe(uint8_t *restrict work)
{
    const CanFrame *f = J1939V.decode_lfe_args.f;
    J1939Lfe *out = J1939V.decode_lfe_args.out;

    if (!f || !out || f->dlc < 8)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939Id id;
    J1939V.decode_id_args.id = f->id;
    J1939V.decode_id_args.out = &id;
    protocore_j1939_decode_id(work);
    if (!J1939V.ok || id.pgn != J1939_PGN_LFE)
    {
        J1939V.ok = PROTO_FALSE;
        return;
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
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_decode_amb(uint8_t *restrict work)
{
    const CanFrame *f = J1939V.decode_amb_args.f;
    J1939Amb *out = J1939V.decode_amb_args.out;

    if (!f || !out || f->dlc < 8)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939Id id;
    J1939V.decode_id_args.id = f->id;
    J1939V.decode_id_args.out = &id;
    protocore_j1939_decode_id(work);
    if (!J1939V.ok || id.pgn != J1939_PGN_AMB)
    {
        J1939V.ok = PROTO_FALSE;
        return;
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
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_decode_ic1(uint8_t *restrict work)
{
    const CanFrame *f = J1939V.decode_ic1_args.f;
    J1939Ic1 *out = J1939V.decode_ic1_args.out;

    if (!f || !out || f->dlc < 8)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939Id id;
    J1939V.decode_id_args.id = f->id;
    J1939V.decode_id_args.out = &id;
    protocore_j1939_decode_id(work);
    if (!J1939V.ok || id.pgn != J1939_PGN_IC1)
    {
        J1939V.ok = PROTO_FALSE;
        return;
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
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_decode_vd(uint8_t *restrict work)
{
    const CanFrame *f = J1939V.decode_vd_args.f;
    J1939Vd *out = J1939V.decode_vd_args.out;

    if (!f || !out || f->dlc < 8)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939Id id;
    J1939V.decode_id_args.id = f->id;
    J1939V.decode_id_args.out = &id;
    protocore_j1939_decode_id(work);
    if (!J1939V.ok || id.pgn != J1939_PGN_VD)
    {
        J1939V.ok = PROTO_FALSE;
        return;
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
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_decode_ccvs(uint8_t *restrict work)
{
    const CanFrame *f = J1939V.decode_ccvs_args.f;
    J1939Ccvs *out = J1939V.decode_ccvs_args.out;

    if (!f || !out || f->dlc < 8)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    J1939Id id;
    J1939V.decode_id_args.id = f->id;
    J1939V.decode_id_args.out = &id;
    protocore_j1939_decode_id(work);
    if (!J1939V.ok || id.pgn != J1939_PGN_CCVS)
    {
        J1939V.ok = PROTO_FALSE;
        return;
    }
    // SPN 84 Wheel-Based Vehicle Speed: bytes 2-3, little-endian, 1/256 km/h per bit, 0 offset.
    uint16_t ws = (uint16_t)(f->data[1] | ((uint16_t)f->data[2] << 8));
    out->speed_valid = (ws <= 0xFAFFu); // >= 0xFB00 is error / not-available
    out->wheel_speed_kmh = (float)ws * (1.0f / 256.0f);
    // SPN 595 Cruise Control Active: byte 4, bits 1-2 (the low 2 bits) - a 2-bit state.
    out->cruise_active = (uint8_t)(f->data[3] & 0x03u);
    J1939V.ok = PROTO_TRUE;
}

void protocore_j1939_decode_dm1(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *body = J1939V.decode_dm1_args.body;
    size_t len = J1939V.decode_dm1_args.len;
    J1939Dm1 *out = J1939V.decode_dm1_args.out;
    J1939Dtc *out_dtcs = J1939V.decode_dm1_args.out_dtcs;
    size_t max = J1939V.decode_dm1_args.max;

    if (!body || !out || len < 2) // the lamp-status + flash-status octets
    {
        J1939V.ok = PROTO_FALSE;
        return;
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
    J1939V.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
J1939Vars J1939V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_NEED_J1939
