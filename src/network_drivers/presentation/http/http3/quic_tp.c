// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_tp.cpp
 * @brief QUIC transport parameters codec (see pc_quic_tp.h).
 */

#include "network_drivers/presentation/http/http3/quic_tp.h"

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_varint.h"
#include <string.h>

void pc_quic_tp_defaults(QuicTransportParams *tp)
{
    memset(tp, 0, sizeof(*tp));
    tp->max_udp_payload_size = 65527;
    tp->ack_delay_exponent = 3;
    tp->max_ack_delay = 25;
    tp->active_connection_id_limit = 2;
}

// Append one parameter: ID (varint) || Length (varint) || raw value bytes.
static proto_bool put_param(uint8_t *out, size_t cap, size_t *p, uint64_t id, const uint8_t *val, size_t val_len)
{
    size_t n = pc_quic_varint_encode(out + *p, cap - *p, id);
    if (!n)
    {
        return PROTO_FALSE;
    }
    *p += n;
    n = pc_quic_varint_encode(out + *p, cap - *p, val_len);
    if (!n)
    {
        return PROTO_FALSE;
    }
    *p += n;
    if (val_len)
    {
        // Overflow-safe check (*p <= cap holds after the varint encodes): avoids the *p + val_len
        // size_t wraparound an analyzer flags as a possible destination overflow (cpp:S3519).
        if (val_len > cap - *p)
        {
            return PROTO_FALSE;
        }
        memcpy(out + *p, val, val_len);
        *p += val_len;
    }
    return PROTO_TRUE;
}

// Append a varint-valued parameter (Value is itself a varint).
static proto_bool put_varint_param(uint8_t *out, size_t cap, size_t *p, uint64_t id, uint64_t value)
{
    uint8_t v[8];
    size_t vlen = pc_quic_varint_encode(v, sizeof(v), value);
    if (!vlen)
    {
        return PROTO_FALSE;
    }
    return put_param(out, cap, p, id, v, vlen);
}

size_t pc_quic_tp_encode(const QuicTransportParams *tp, uint8_t *out, size_t cap)
{
    size_t p = 0;
    proto_bool ok = PROTO_TRUE;
    if (tp->has_original_dcid)
    {
        // This is the first assignment in the ok-chain (ok is still its line-64 default), so the
        // "ok already false" arm of this && can never fire.
        ok = ok && put_param(out, cap, &p, QuicTp::QUIC_TP_ORIGINAL_DCID, tp->original_dcid, // GCOVR_EXCL_BR_LINE
                             tp->original_dcid_len);
    }
    if (tp->has_initial_scid)
    {
        ok = ok && put_param(out, cap, &p, QuicTp::QUIC_TP_INITIAL_SCID, tp->initial_scid, tp->initial_scid_len);
    }
    if (tp->has_retry_scid)
    {
        ok = ok && put_param(out, cap, &p, QuicTp::QUIC_TP_RETRY_SCID, tp->retry_scid, tp->retry_scid_len);
    }

    ok = ok && put_varint_param(out, cap, &p, QuicTp::QUIC_TP_INITIAL_MAX_DATA, tp->initial_max_data);
    ok = ok && put_varint_param(out, cap, &p, QuicTp::QUIC_TP_INITIAL_MAX_SD_BIDI_LOCAL, tp->initial_max_sd_bidi_local);
    ok = ok &&
         put_varint_param(out, cap, &p, QuicTp::QUIC_TP_INITIAL_MAX_SD_BIDI_REMOTE, tp->initial_max_sd_bidi_remote);
    ok = ok && put_varint_param(out, cap, &p, QuicTp::QUIC_TP_INITIAL_MAX_SD_UNI, tp->initial_max_sd_uni);
    ok = ok && put_varint_param(out, cap, &p, QuicTp::QUIC_TP_INITIAL_MAX_STREAMS_BIDI, tp->initial_max_streams_bidi);
    ok = ok && put_varint_param(out, cap, &p, QuicTp::QUIC_TP_INITIAL_MAX_STREAMS_UNI, tp->initial_max_streams_uni);
    ok = ok && put_varint_param(out, cap, &p, QuicTp::QUIC_TP_MAX_IDLE_TIMEOUT, tp->max_idle_timeout);
    ok = ok && put_varint_param(out, cap, &p, QuicTp::QUIC_TP_MAX_UDP_PAYLOAD_SIZE, tp->max_udp_payload_size);
    ok = ok && put_varint_param(out, cap, &p, QuicTp::QUIC_TP_ACTIVE_CID_LIMIT, tp->active_connection_id_limit);
    if (tp->disable_active_migration)
    {
        ok = ok && put_param(out, cap, &p, QuicTp::QUIC_TP_DISABLE_ACTIVE_MIGRATION, NULL, 0);
    }

    return ok ? p : 0;
}

// Decode the varint that IS the whole value of a varint-valued parameter (must consume exactly len).
static proto_bool value_varint(const uint8_t *val, size_t len, uint64_t *out)
{
    size_t consumed = 0;
    if (!pc_quic_varint_decode(val, len, out, &consumed))
    {
        return PROTO_FALSE;
    }
    return consumed == len;
}

// Copy a connection-ID value (<= QUIC_MAX_CID_LEN) into a fixed field.
static proto_bool copy_cid(const uint8_t *val, size_t len, uint8_t *dst, uint8_t *dst_len, proto_bool *has)
{
    if (len > QUIC_MAX_CID_LEN)
    {
        return PROTO_FALSE;
    }
    memcpy(dst, val, len);
    *dst_len = (uint8_t)len;
    *has = PROTO_TRUE;
    return PROTO_TRUE;
}

// Apply a connection-ID transport parameter. *handled is set true if id names a CID param (whether or
// not the copy succeeded); false leaves it for another category. Returns false only on a bad value.
static proto_bool pc_quic_tp_apply_cid(uint64_t id, const uint8_t *val, size_t vlen, QuicTransportParams *tp,
                                       proto_bool *handled)
{
    *handled = PROTO_TRUE;
    switch (id)
    {
    case QuicTp::QUIC_TP_ORIGINAL_DCID:
        return copy_cid(val, vlen, tp->original_dcid, &tp->original_dcid_len, &tp->has_original_dcid);
    case QuicTp::QUIC_TP_INITIAL_SCID:
        return copy_cid(val, vlen, tp->initial_scid, &tp->initial_scid_len, &tp->has_initial_scid);
    case QuicTp::QUIC_TP_RETRY_SCID:
        return copy_cid(val, vlen, tp->retry_scid, &tp->retry_scid_len, &tp->has_retry_scid);
    default:
        *handled = PROTO_FALSE;
        return PROTO_TRUE;
    }
}

// Apply a varint-valued transport parameter with its RFC 9000 range checks. *handled is set as above.
static proto_bool pc_quic_tp_apply_varint(uint64_t id, const uint8_t *val, size_t vlen, QuicTransportParams *tp,
                                          proto_bool *handled)
{
    *handled = PROTO_TRUE;
    switch (id)
    {
    case QuicTp::QUIC_TP_MAX_IDLE_TIMEOUT:
        return value_varint(val, vlen, &tp->max_idle_timeout);
    case QuicTp::QUIC_TP_MAX_UDP_PAYLOAD_SIZE:
        return value_varint(val, vlen, &tp->max_udp_payload_size) && tp->max_udp_payload_size >= 1200;
    case QuicTp::QUIC_TP_INITIAL_MAX_DATA:
        return value_varint(val, vlen, &tp->initial_max_data);
    case QuicTp::QUIC_TP_INITIAL_MAX_SD_BIDI_LOCAL:
        return value_varint(val, vlen, &tp->initial_max_sd_bidi_local);
    case QuicTp::QUIC_TP_INITIAL_MAX_SD_BIDI_REMOTE:
        return value_varint(val, vlen, &tp->initial_max_sd_bidi_remote);
    case QuicTp::QUIC_TP_INITIAL_MAX_SD_UNI:
        return value_varint(val, vlen, &tp->initial_max_sd_uni);
    case QuicTp::QUIC_TP_INITIAL_MAX_STREAMS_BIDI:
        return value_varint(val, vlen, &tp->initial_max_streams_bidi);
    case QuicTp::QUIC_TP_INITIAL_MAX_STREAMS_UNI:
        return value_varint(val, vlen, &tp->initial_max_streams_uni);
    case QuicTp::QUIC_TP_ACK_DELAY_EXPONENT:
        return value_varint(val, vlen, &tp->ack_delay_exponent) && tp->ack_delay_exponent <= 20;
    case QuicTp::QUIC_TP_MAX_ACK_DELAY:
        return value_varint(val, vlen, &tp->max_ack_delay) && tp->max_ack_delay < (1u << 14);
    case QuicTp::QUIC_TP_ACTIVE_CID_LIMIT:
        return value_varint(val, vlen, &tp->active_connection_id_limit) && tp->active_connection_id_limit >= 2;
    default:
        *handled = PROTO_FALSE;
        return PROTO_TRUE;
    }
}

// Dispatch one parsed transport parameter to tp; false on a malformed / out-of-range value. Unknown
// (GREASE) IDs are silently ignored, matching RFC 9000 §7.4.1.
static proto_bool pc_quic_tp_apply(uint64_t id, const uint8_t *val, size_t vlen, QuicTransportParams *tp)
{
    proto_bool handled = PROTO_FALSE;
    if (!pc_quic_tp_apply_cid(id, val, vlen, tp, &handled))
    {
        return PROTO_FALSE;
    }
    if (handled)
    {
        return PROTO_TRUE;
    }
    if (!pc_quic_tp_apply_varint(id, val, vlen, tp, &handled))
    {
        return PROTO_FALSE;
    }
    if (handled)
    {
        return PROTO_TRUE;
    }
    if (id == QuicTp::QUIC_TP_DISABLE_ACTIVE_MIGRATION)
    {
        if (vlen != 0)
        {
            return PROTO_FALSE;
        }
        tp->disable_active_migration = PROTO_TRUE;
    }
    return PROTO_TRUE; // unknown / GREASE: skip
}

proto_bool pc_quic_tp_parse(const uint8_t *buf, size_t len, QuicTransportParams *tp)
{
    pc_quic_tp_defaults(tp);
    uint32_t seen = 0; // dup-guard bitmask over the known IDs (all < 32)

    size_t off = 0;
    while (off < len)
    {
        uint64_t id = 0;
        uint64_t vlen = 0;
        size_t c = 0;
        if (!pc_quic_varint_decode(buf + off, len - off, &id, &c))
        {
            return PROTO_FALSE;
        }
        off += c;
        if (!pc_quic_varint_decode(buf + off, len - off, &vlen, &c))
        {
            return PROTO_FALSE;
        }
        off += c;
        if (off + vlen > len)
        {
            return PROTO_FALSE;
        }
        const uint8_t *val = buf + off;
        off += vlen;

        if (id < 32)
        {
            uint32_t bit = 1u << id;
            if (seen & bit)
            {
                return PROTO_FALSE; // a known parameter must not appear twice
            }
            seen |= bit;
        }

        if (!pc_quic_tp_apply(id, val, vlen, tp))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_HTTP3
