// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mms.c
 * @brief IEC 61850 MMS PDU codec (see mms.h).
 */

#include "services/energy/mms/mms.h"

#if PC_ENABLE_MMS

#include <string.h>

// BER definite-length octet count for a length value < 64 KiB.
static size_t len_octets(size_t len)
{
    if (len < 0x80)
    {
        return 1;
    }
    if (len < 0x100)
    {
        return 2;
    }
    // Reachable: tlv() calls this on the raw, still-unvalidated val_len (a caller-supplied data_len can be
    // >=256) and only rejects the total afterwards, so the 3-octet form is computed before the cap check.
    return 3;
}

// Write a BER length; returns octets written.
static size_t write_len(uint8_t *p, size_t len)
{
    if (len < 0x80)
    {
        p[0] = (uint8_t)len;
        return 1;
    }
    if (len < 0x100)
    {
        p[0] = 0x81;
        p[1] = (uint8_t)len;
        return 2;
    }
    // Reachable: the OUTERMOST wrap's value is the assembled body, which is exactly sizeof(body) == 256
    // octets when the caller's payload fills it, and its cap is the caller's buffer rather than a fixed
    // 256B scratch - so a 256-octet value with a large enough cap reaches the 3-octet (0x82) form.
    p[0] = 0x82;
    p[1] = (uint8_t)(len >> 8);
    p[2] = (uint8_t)len;
    return 3;
}

// Write a full TLV (tag + length + value) at out; returns total length, or 0 on overflow.
static size_t tlv(uint8_t tag, const uint8_t *val, size_t val_len, uint8_t *out, size_t cap)
{
    // One source of truth for the length-octet count: the value offset (k) and the total (n) both derive
    // from it, so k + val_len == n is provable (write_len writes exactly this many octets).
    size_t k = 1 + len_octets(val_len); // value offset: tag + length octets
    size_t n = k + val_len;             // total = offset + value
    if (n > cap)
    {
        return 0;
    }
    out[0] = tag;
    write_len(out + 1, val_len); // writes exactly len_octets(val_len) octets
    // Copy length expressed as n - k (== val_len) so the destination bound flows straight from the
    // n <= cap check above: out + k + (n - k) = out + n <= out + cap, and it reads n - k == val_len bytes
    // from the val_len-sized val. Provably in bounds; all three callers pass val buffers >= val_len
    // (data[data_len], scratch[256] with n<=256, idc[5] with idlen<=5). S3519 here explores an infeasible
    // path (it assumes val_len >= cap - k, e.g. 5 >= 254) that the n <= cap guard rules out.
    if (n > k)
    {
        memcpy(out + k, val, n - k); // NOSONAR - see above: bound proven, analyzer follows an infeasible path
    }
    return n;
}

// Minimal big-endian unsigned INTEGER content (with a leading 0 if the MSB is set), into buf; returns len.
static size_t int_content(uint32_t v, uint8_t *buf)
{
    uint8_t tmp[4];
    size_t t = 0;
    if (v == 0)
    {
        tmp[t++] = 0;
    }
    else
    {
        for (uint32_t x = v; x; x >>= 8)
        {
            tmp[t++] = (uint8_t)x;
        }
    }
    size_t k = 0;
    if (tmp[t - 1] & 0x80)
    {
        buf[k++] = 0x00;
    }
    for (size_t i = 0; i < t; i++)
    {
        buf[k++] = tmp[t - 1 - i];
    }
    return k;
}

// Decode a BER definite length at pdu[at]. Sets *value and *hdr (octets the length field spans: 1 for
// short form, 1+nb for long form). Returns false for an out-of-bounds field or an unsupported long form
// (nb outside 1..2). Only short/2-byte lengths occur in the PDUs this codec accepts.
static proto_bool pc_ber_len(const uint8_t *pdu, size_t len, size_t at, size_t *value, size_t *hdr)
{
    if (at >= len)
    {
        return PROTO_FALSE;
    }
    size_t v = pdu[at];
    if (v & 0x80)
    {
        size_t nb = v & 0x7F;
        if (nb == 0 || nb > 2 || at + 1 + nb > len)
        {
            return PROTO_FALSE;
        }
        v = 0;
        for (size_t i = 0; i < nb; i++)
        {
            v = (v << 8) | pdu[at + 1 + i];
        }
        *hdr = 1 + nb;
    }
    else
    {
        *hdr = 1;
    }
    *value = v;
    return PROTO_TRUE;
}

size_t pc_mms_read_request(uint32_t invoke_id, const char *item_name, uint8_t *out, size_t cap)
{
    if (!out || !item_name)
    {
        return 0;
    }
    size_t name_len = strnlen(item_name, 128 + 1);
    if (name_len > 128)
    {
        return 0;
    }

    // Each wrap checks its tlv() result. name_len is capped at 128 above and every buffer here is 256
    // bytes, so the running length maxes ~146.
    uint8_t scratch[256];
    // innermost: objectName VisibleString (0x1A) with the item name.
    size_t n = tlv(0x1A, (const uint8_t *)item_name, name_len, scratch, sizeof(scratch));
    if (!n)
    {
        return 0;
    }
    // A0 name [0]
    uint8_t a0[256];
    n = tlv(0xA0, scratch, n, a0, sizeof(a0));
    if (!n)
    {
        return 0;
    }
    // 30 SEQUENCE (one VariableSpecification)
    n = tlv(0x30, a0, n, scratch, sizeof(scratch));
    if (!n)
    {
        return 0;
    }
    // A0 listOfVariable [0]
    n = tlv(0xA0, scratch, n, a0, sizeof(a0));
    if (!n)
    {
        return 0;
    }
    // A1 variableAccessSpecification [1]
    n = tlv(0xA1, a0, n, scratch, sizeof(scratch));
    if (!n)
    {
        return 0;
    }
    // A4 read [4]
    n = tlv(MMS_SERVICE_READ, scratch, n, a0, sizeof(a0));
    if (!n)
    {
        return 0;
    }

    // Prepend the invokeID INTEGER, then wrap in the confirmed-request PDU.
    uint8_t idc[5];
    size_t idlen = int_content(invoke_id, idc);
    uint8_t body[256];
    size_t bn = tlv(MMS_TAG_INVOKE_ID, idc, idlen, body, sizeof(body));
    // because name_len is capped at 128 above. Unreachable, kept defensively.
    if (!bn || bn + n > sizeof(body))
    {
        return 0;
    }
    memcpy(body + bn, a0, n); // append the A4 read
    bn += n;
    return tlv(MMS_PDU_CONFIRMED_REQUEST, body, bn, out, cap);
}

size_t pc_mms_read_response(uint32_t invoke_id, const uint8_t *data, size_t data_len, uint8_t *out, size_t cap)
{
    if (!out || (data_len && !data))
    {
        return 0;
    }
    uint8_t scratch[256];
    // listOfAccessResult SEQUENCE (0xA1 in Read response) wrapping the caller's Data value(s).
    size_t n = tlv(0xA1, data, data_len, scratch, sizeof(scratch));
    if (!n)
    {
        return 0;
    }
    // A4 read response [4]
    uint8_t svc[256];
    n = tlv(MMS_SERVICE_READ, scratch, n, svc, sizeof(svc));
    if (!n)
    {
        return 0;
    }

    uint8_t idc[5];
    size_t idlen = int_content(invoke_id, idc);
    uint8_t body[256];
    size_t bn = tlv(MMS_TAG_INVOKE_ID, idc, idlen, body, sizeof(body));
    // The bn+n overflow half IS reachable (a large caller data_len trips it) and is covered by a test, but
    // !bn is not: int_content always yields idlen 1..5, so this tlv always writes 2+idlen (2..7) octets into
    // a 256-byte buffer and can never return 0. gcovr cannot exclude one operand, so the line is excluded.
    if (!bn || bn + n > sizeof(body))
    {
        return 0;
    }
    memcpy(body + bn, svc, n);
    bn += n;
    return tlv(MMS_PDU_CONFIRMED_RESPONSE, body, bn, out, cap);
}

proto_bool pc_mms_parse(const uint8_t *pdu, size_t len, MmsPdu *out)
{
    if (!pdu || !out || len < 2)
    {
        return PROTO_FALSE;
    }
    out->pdu_tag = pdu[0];
    if (out->pdu_tag != MMS_PDU_CONFIRMED_REQUEST && out->pdu_tag != MMS_PDU_CONFIRMED_RESPONSE &&
        out->pdu_tag != MMS_PDU_CONFIRMED_ERROR)
    {
        return PROTO_FALSE;
    }
    // Decode the outer length.
    size_t off = 1;
    size_t body_len = 0;
    size_t lhdr = 0;
    if (!pc_ber_len(pdu, len, off, &body_len, &lhdr))
    {
        return PROTO_FALSE;
    }
    off += lhdr;
    if (off + body_len > len)
    {
        return PROTO_FALSE;
    }

    // First inner element must be the invokeID INTEGER.
    if (off + 2 > len || pdu[off] != MMS_TAG_INVOKE_ID)
    {
        return PROTO_FALSE;
    }
    size_t idlen = pdu[off + 1];
    if (idlen > 4 || off + 2 + idlen > len)
    {
        return PROTO_FALSE;
    }
    uint32_t id = 0;
    for (size_t i = 0; i < idlen; i++)
    {
        id = (id << 8) | pdu[off + 2 + i];
    }
    out->invoke_id = id;
    size_t p = off + 2 + idlen;

    // The next element is the confirmedService (A4 read, A5 write, ...). The p < off + body_len half is
    // reachable both ways (a PDU carrying only the invokeID takes the else); p < len can never be false
    // because off + body_len <= len was already checked above, so p < off + body_len implies p < len.
    // gcovr cannot exclude one operand, so the whole line is excluded.
    if (p < off + body_len && p < len)
    {
        out->service_tag = pdu[p];
        size_t sp = p + 1;
        size_t slen = 0;
        size_t hdr = 0;
        if (!pc_ber_len(pdu, len, sp, &slen, &hdr))
        {
            return PROTO_FALSE;
        }
        if (sp + hdr + slen > len)
        {
            return PROTO_FALSE;
        }
        out->service_body = pdu + sp + hdr;
        out->service_len = slen;
    }
    else
    {
        out->service_tag = 0;
        out->service_body = NULL;
        out->service_len = 0;
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_MMS
