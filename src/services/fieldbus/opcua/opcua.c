// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file opcua.c
 * @brief OPC UA Binary server: handshake + SecureChannel + Session + Read/Write + Browse.
 *
 * Pure little-endian codec, handshake, OpenSecureChannel, CreateSession/
 * ActivateSession, GetEndpoints, Read/Write (Variant/DataValue), Browse
 * (ReferenceDescription), CloseSession and a ServiceFault fallback; the ESP32 section
 * pumps the ConnProto::PROTO_OPCUA rx ring and answers HEL with ACK, OPN with an
 * OpenSecureChannelResponse, the MSG service calls, and closes on CLO (SecurityPolicy
 * None). No heap, no stdlib.
 */

#include "services/fieldbus/opcua/opcua.h"

#if PC_ENABLE_OPCUA

// ProtoHandler is named by pc_opcua_proto_handler() in BOTH build arms, so it cannot sit behind
// the PROTOCORE_HOT guard below.
#include "network_drivers/session/proto_handler.h"

// ---------------------------------------------------------------------------
// Built-in type codec
// ---------------------------------------------------------------------------
#if PROTOCORE_HOT
#include "network_drivers/transport/tcp.h"
#include <time.h>
#endif
static void w_bytes(UaWriter *w, const void *src, size_t n)
{
    if (!w->ok || w->n + n > w->cap)
    {
        w->ok = PROTO_FALSE;
        return;
    }
    memcpy(w->o + w->n, src, n); // NOSONAR - bound proven above; analyzer follows an infeasible path
    w->n += n;
}

void pc_ua_w_u8(UaWriter *w, uint8_t v)
{
    w_bytes(w, &v, 1);
}
void pc_ua_w_u16(UaWriter *w, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    w_bytes(w, b, 2);
}
void pc_ua_w_u32(UaWriter *w, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    w_bytes(w, b, 4);
}
void pc_ua_w_u64(UaWriter *w, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; i++)
    {
        b[i] = (uint8_t)(v >> (8 * i));
    }
    w_bytes(w, b, 8);
}
void pc_ua_w_i32(UaWriter *w, int32_t v)
{
    pc_ua_w_u32(w, (uint32_t)v);
}
void pc_ua_w_f32(UaWriter *w, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    pc_ua_w_u32(w, u);
}
void pc_ua_w_f64(UaWriter *w, double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    pc_ua_w_u64(w, u);
}
void pc_ua_w_bool(UaWriter *w, proto_bool v)
{
    pc_ua_w_u8(w, v ? 1 : 0);
}
void pc_ua_w_string(UaWriter *w, const char *s, int32_t len)
{
    pc_ua_w_i32(w, len);
    if (len > 0 && s)
    {
        w_bytes(w, s, (size_t)len);
    }
}

static proto_bool r_take(UaReader *r, void *dst, size_t n)
{
    if (r->err || r->off + n > r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    memcpy(dst, r->p + r->off, n);
    r->off += n;
    return PROTO_TRUE;
}

uint8_t pc_ua_r_u8(UaReader *r)
{
    uint8_t v = 0;
    r_take(r, &v, 1);
    return v;
}
uint16_t pc_ua_r_u16(UaReader *r)
{
    uint8_t b[2] = {0, 0};
    r_take(r, b, 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}
uint32_t pc_ua_r_u32(UaReader *r)
{
    uint8_t b[4] = {0, 0, 0, 0};
    r_take(r, b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
uint64_t pc_ua_r_u64(UaReader *r)
{
    uint8_t b[8];
    if (!r_take(r, b, 8))
    {
        return 0;
    }
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
    {
        v |= (uint64_t)b[i] << (8 * i);
    }
    return v;
}
int32_t pc_ua_r_i32(UaReader *r)
{
    return (int32_t)pc_ua_r_u32(r);
}
float pc_ua_r_f32(UaReader *r)
{
    uint32_t u = pc_ua_r_u32(r);
    float v;
    memcpy(&v, &u, 4);
    return v;
}
double pc_ua_r_f64(UaReader *r)
{
    uint64_t u = pc_ua_r_u64(r);
    double v;
    memcpy(&v, &u, 8);
    return v;
}
proto_bool pc_ua_r_bool(UaReader *r)
{
    return pc_ua_r_u8(r) != 0;
}
proto_bool pc_ua_r_string(UaReader *r, char *out, size_t cap, int32_t *out_len)
{
    int32_t len = pc_ua_r_i32(r);
    if (r->err)
    {
        return PROTO_FALSE;
    }
    if (out_len)
    {
        *out_len = len;
    }
    if (len < 0) // null string
    {
        if (cap)
        {
            out[0] = '\0';
        }
        return PROTO_TRUE;
    }
    if ((size_t)len + 1 > cap || r->off + (size_t)len > r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    memcpy(out, r->p + r->off, (size_t)len);
    out[len] = '\0';
    r->off += (size_t)len;
    return PROTO_TRUE;
}

static void r_skip(UaReader *r, size_t n)
{
    if (r->err || r->off + n > r->len)
    {
        r->err = PROTO_TRUE;
        return;
    }
    r->off += n;
}

// ---------------------------------------------------------------------------
// NodeId / ExtensionObject / DateTime
// ---------------------------------------------------------------------------
void pc_ua_w_nodeid_numeric(UaWriter *w, uint16_t ns, uint32_t id)
{
    if (ns == 0 && id <= 0xFF) // TwoByte
    {
        pc_ua_w_u8(w, 0x00);
        pc_ua_w_u8(w, (uint8_t)id);
    }
    else if (ns <= 0xFF && id <= 0xFFFF) // FourByte
    {
        pc_ua_w_u8(w, 0x01);
        pc_ua_w_u8(w, (uint8_t)ns);
        pc_ua_w_u16(w, (uint16_t)id);
    }
    else // Numeric
    {
        pc_ua_w_u8(w, 0x02);
        pc_ua_w_u16(w, ns);
        pc_ua_w_u32(w, id);
    }
}

proto_bool pc_ua_r_nodeid(UaReader *r, UaNodeId *out)
{
    uint8_t enc = pc_ua_r_u8(r);
    uint8_t kind = enc & 0x0F; // strip the NamespaceUri (0x80) / ServerIndex (0x40) flags
    out->ns = 0;
    out->id = 0;
    out->numeric = PROTO_TRUE;
    switch (kind)
    {
    case 0x00: // TwoByte
        out->id = pc_ua_r_u8(r);
        break;
    case 0x01: // FourByte
        out->ns = pc_ua_r_u8(r);
        out->id = pc_ua_r_u16(r);
        break;
    case 0x02: // Numeric
        out->ns = pc_ua_r_u16(r);
        out->id = pc_ua_r_u32(r);
        break;
    case 0x03: // String
    case 0x05: // ByteString
    {
        out->ns = pc_ua_r_u16(r);
        out->numeric = PROTO_FALSE;
        int32_t l = pc_ua_r_i32(r);
        if (l > 0)
        {
            r_skip(r, (size_t)l);
        }
        break;
    }
    case 0x04: // Guid
        out->ns = pc_ua_r_u16(r);
        out->numeric = PROTO_FALSE;
        r_skip(r, 16);
        break;
    default:
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    if (enc & 0x80) // NamespaceUri (String)
    {
        int32_t l = pc_ua_r_i32(r);
        if (l > 0)
        {
            r_skip(r, (size_t)l);
        }
    }
    if (enc & 0x40) // ServerIndex (UInt32)
    {
        (void)pc_ua_r_u32(r);
    }
    return !r->err;
}

// Skip an ExtensionObject: NodeId TypeId + encoding byte (+ ByteString/XML body).
static proto_bool r_ext_object_skip(UaReader *r)
{
    UaNodeId tid;
    if (!pc_ua_r_nodeid(r, &tid))
    {
        return PROTO_FALSE;
    }
    uint8_t body_enc = pc_ua_r_u8(r);
    if (body_enc == 0x00) // no body
    {
        return !r->err;
    }
    int32_t l = pc_ua_r_i32(r); // ByteString (0x01) or XmlElement (0x02) body
    if (l > 0)
    {
        r_skip(r, (size_t)l);
    }
    return !r->err;
}

// Read a RequestHeader (the prefix of every service request), capturing the
// RequestHandle. The AuthenticationToken / Timestamp / diagnostics / audit id /
// timeout / AdditionalHeader are consumed and discarded.
static proto_bool r_request_header(UaReader *r, uint32_t *request_handle)
{
    UaNodeId auth;
    pc_ua_r_nodeid(r, &auth);     // AuthenticationToken
    (void)pc_ua_r_u64(r);         // Timestamp (DateTime)
    uint32_t rh = pc_ua_r_u32(r); // RequestHandle
    if (request_handle)
    {
        *request_handle = rh;
    }
    (void)pc_ua_r_u32(r);         // ReturnDiagnostics
    int32_t aid = pc_ua_r_i32(r); // AuditEntryId (String)
    if (aid > 0)
    {
        r_skip(r, (size_t)aid);
    }
    (void)pc_ua_r_u32(r);        // TimeoutHint
    return r_ext_object_skip(r); // AdditionalHeader (ExtensionObject)
}

// Parse a MSG-envelope preamble: security + sequence headers, body TypeId, and the RequestHeader.
// On success r is positioned at the service body and m is filled; false on a malformed frame.
static proto_bool r_msg_preamble(const uint8_t *msg, size_t len, UaReader *r, OpcUaMsg *m)
{
    UaMsgHeader h;
    if (!pc_opcua_parse_header(msg, len, &h) || memcmp(h.type, "MSG", 3) != 0)
    {
        return PROTO_FALSE;
    }
    if (h.size != len)
    {
        return PROTO_FALSE;
    }

    UaReader rr = {msg + 8, len - 8, 0, PROTO_FALSE};
    *r = rr;
    m->secure_channel_id = pc_ua_r_u32(r); // SecureChannelId
    m->token_id = pc_ua_r_u32(r);
    m->sequence_number = pc_ua_r_u32(r);
    m->request_id = pc_ua_r_u32(r);

    UaNodeId tid;
    if (!pc_ua_r_nodeid(r, &tid)) // body TypeId
    {
        return PROTO_FALSE;
    }
    m->type_id = tid.numeric ? tid.id : 0;
    return r_request_header(r, &m->request_handle);
}

int64_t pc_opcua_filetime_from_unix(int64_t unix_seconds)
{
    if (unix_seconds <= 0)
    {
        return 0;
    }
    return (unix_seconds + 11644473600LL) * 10000000LL; // 1601->1970 offset, seconds -> 100 ns ticks
}

// ---------------------------------------------------------------------------
// UACP framing + handshake
// ---------------------------------------------------------------------------
proto_bool pc_opcua_parse_header(const uint8_t *buf, size_t len, UaMsgHeader *h)
{
    if (!buf || len < 8 || !h)
    {
        return PROTO_FALSE;
    }
    h->type[0] = (char)buf[0];
    h->type[1] = (char)buf[1];
    h->type[2] = (char)buf[2];
    h->chunk = (char)buf[3];
    h->size = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    return PROTO_TRUE;
}

proto_bool pc_opcua_parse_hello(const uint8_t *msg, size_t len, OpcUaHello *out)
{
    UaMsgHeader h;
    if (!pc_opcua_parse_header(msg, len, &h) || memcmp(h.type, "HEL", 3) != 0)
    {
        return PROTO_FALSE;
    }
    if (h.size != len || h.size < 8 + 20) // 8-byte header + at least the five sizes
    {
        return PROTO_FALSE;
    }
    UaReader r = {msg + 8, len - 8, 0, PROTO_FALSE};
    out->protocol_version = pc_ua_r_u32(&r);
    out->recv_buf_size = pc_ua_r_u32(&r);
    out->send_buf_size = pc_ua_r_u32(&r);
    out->max_msg_size = pc_ua_r_u32(&r);
    out->max_chunk_count = pc_ua_r_u32(&r);
    return !r.err; // EndpointUrl (a String) follows; not needed for negotiation
}

static uint32_t neg(uint32_t client, uint32_t server)
{
    if (client == 0)
    {
        return server;
    }
    return client < server ? client : server;
}

size_t pc_opcua_build_ack(const OpcUaHello *hello, uint8_t *out, size_t cap)
{
    if (!hello || !out)
    {
        return 0;
    }
    const uint32_t total = 8 + 20; // header + 5 x UInt32
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    pc_ua_w_u8(&w, 'A');
    pc_ua_w_u8(&w, 'C');
    pc_ua_w_u8(&w, 'K');
    pc_ua_w_u8(&w, 'F');
    pc_ua_w_u32(&w, total);
    pc_ua_w_u32(&w, 0);                                       // ProtocolVersion
    pc_ua_w_u32(&w, neg(hello->send_buf_size, PC_OPCUA_BUF)); // our ReceiveBufferSize
    pc_ua_w_u32(&w, neg(hello->recv_buf_size, PC_OPCUA_BUF)); // our SendBufferSize
    pc_ua_w_u32(&w, neg(hello->max_msg_size, PC_OPCUA_BUF));  // MaxMessageSize
    pc_ua_w_u32(&w, 1);                                       // MaxChunkCount (single-chunk)
    return w.ok ? w.n : 0;
}

size_t pc_opcua_build_error(uint32_t error_code, const char *reason, uint8_t *out, size_t cap)
{
    if (!out)
    {
        return 0;
    }
    int32_t rlen = reason ? (int32_t)strnlen(reason, cap) : -1;
    // the wire layout is the four type octets, the total-size UInt32, the error-code UInt32, then the reason
    // string carried as an int32 length followed by its bytes
    const uint32_t total = 8 + 4 + 4 + (rlen > 0 ? (uint32_t)rlen : 0);
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    pc_ua_w_u8(&w, 'E');
    pc_ua_w_u8(&w, 'R');
    pc_ua_w_u8(&w, 'R');
    pc_ua_w_u8(&w, 'F');
    pc_ua_w_u32(&w, total);
    pc_ua_w_u32(&w, error_code);
    pc_ua_w_string(&w, reason, rlen);
    return w.ok ? w.n : 0;
}

// ---------------------------------------------------------------------------
// SecureChannel - OpenSecureChannel (OPN), SecurityPolicy None
// ---------------------------------------------------------------------------
proto_bool pc_opcua_parse_open(const uint8_t *msg, size_t len, OpcUaOpenChannel *out)
{
    UaMsgHeader h;
    if (!pc_opcua_parse_header(msg, len, &h) || memcmp(h.type, "OPN", 3) != 0)
    {
        return PROTO_FALSE;
    }
    if (h.size != len)
    {
        return PROTO_FALSE;
    }

    UaReader r = {msg + 8, len - 8, 0, PROTO_FALSE};

    // Asymmetric security header (SecurityPolicy None -> no certs).
    out->secure_channel_id = pc_ua_r_u32(&r);
    int32_t pol = pc_ua_r_i32(&r); // SecurityPolicyUri (String)
    if (pol > 0)
    {
        r_skip(&r, (size_t)pol);
    }
    int32_t sc = pc_ua_r_i32(&r); // SenderCertificate (ByteString)
    if (sc > 0)
    {
        r_skip(&r, (size_t)sc);
    }
    int32_t rt = pc_ua_r_i32(&r); // ReceiverCertificateThumbprint (ByteString)
    if (rt > 0)
    {
        r_skip(&r, (size_t)rt);
    }

    // Sequence header.
    out->sequence_number = pc_ua_r_u32(&r);
    out->request_id = pc_ua_r_u32(&r);

    // Body: TypeId NodeId (must be OpenSecureChannelRequest).
    UaNodeId tid;
    if (!pc_ua_r_nodeid(&r, &tid))
    {
        return PROTO_FALSE;
    }
    if (!(tid.numeric && tid.ns == 0 && tid.id == OPCUA_ID_OPEN_REQ))
    {
        return PROTO_FALSE;
    }

    // RequestHeader.
    if (!r_request_header(&r, &out->request_handle))
    {
        return PROTO_FALSE;
    }

    // OpenSecureChannelRequest body.
    out->client_protocol_version = pc_ua_r_u32(&r);
    out->security_token_request_type = pc_ua_r_u32(&r);
    out->message_security_mode = pc_ua_r_u32(&r);
    int32_t nonce = pc_ua_r_i32(&r); // ClientNonce (ByteString)
    if (nonce > 0)
    {
        r_skip(&r, (size_t)nonce);
    }
    out->requested_lifetime = pc_ua_r_u32(&r);
    return !r.err;
}

size_t pc_opcua_build_open_response(const OpcUaOpenChannel *req, uint32_t channel_id, uint32_t token_id,
                                    uint32_t seq_number, int64_t now_ft, uint32_t lifetime, uint8_t *out, size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};

    // Message header (size patched after).
    pc_ua_w_u8(&w, 'O');
    pc_ua_w_u8(&w, 'P');
    pc_ua_w_u8(&w, 'N');
    pc_ua_w_u8(&w, 'F');
    pc_ua_w_u32(&w, 0); // size placeholder

    // Asymmetric security header (SecurityPolicy None: null sender cert + thumbprint).
    pc_ua_w_u32(&w, channel_id);
    pc_ua_w_string(&w, OPCUA_POLICY_NONE_URI, (int32_t)(sizeof(OPCUA_POLICY_NONE_URI) - 1));
    pc_ua_w_string(&w, NULL, -1); // SenderCertificate
    pc_ua_w_string(&w, NULL, -1); // ReceiverCertificateThumbprint

    // Sequence header.
    pc_ua_w_u32(&w, seq_number);
    pc_ua_w_u32(&w, req->request_id); // RequestId echoed

    // Body: TypeId = OpenSecureChannelResponse.
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_OPEN_RESP);

    // ResponseHeader.
    pc_ua_w_u64(&w, (uint64_t)now_ft);    // Timestamp
    pc_ua_w_u32(&w, req->request_handle); // RequestHandle echoed
    pc_ua_w_u32(&w, 0);                   // ServiceResult = Good
    pc_ua_w_u8(&w, 0x00);                 // ServiceDiagnostics (DiagnosticInfo: no fields)
    pc_ua_w_i32(&w, -1);                  // StringTable (null array)
    pc_ua_w_nodeid_numeric(&w, 0, 0);     // AdditionalHeader: null NodeId ...
    pc_ua_w_u8(&w, 0x00);                 // ... + ExtensionObject "no body"

    // OpenSecureChannelResponse body.
    pc_ua_w_u32(&w, 0);                // ServerProtocolVersion
    pc_ua_w_u32(&w, channel_id);       // ChannelSecurityToken.ChannelId
    pc_ua_w_u32(&w, token_id);         // .TokenId
    pc_ua_w_u64(&w, (uint64_t)now_ft); // .CreatedAt
    pc_ua_w_u32(&w, lifetime);         // .RevisedLifetime
    pc_ua_w_string(&w, NULL, -1);      // ServerNonce (null for None)

    if (!w.ok)
    {
        return 0;
    }
    out[4] = (uint8_t)w.n;
    out[5] = (uint8_t)(w.n >> 8);
    out[6] = (uint8_t)(w.n >> 16);
    out[7] = (uint8_t)(w.n >> 24);
    return w.n;
}

// ---------------------------------------------------------------------------
// Session - CreateSession / ActivateSession (MSG service calls)
// ---------------------------------------------------------------------------

// Patch the 4-byte MessageSize field of a UACP message after the body is written.
static size_t patch_size(UaWriter *w)
{
    if (!w->ok)
    {
        return 0;
    }
    w->o[4] = (uint8_t)w->n;
    w->o[5] = (uint8_t)(w->n >> 8);
    w->o[6] = (uint8_t)(w->n >> 16);
    w->o[7] = (uint8_t)(w->n >> 24);
    return w->n;
}

// Write a MSG envelope prefix: UACP header (size placeholder) + SecureChannelId +
// SymmetricSecurityHeader (TokenId) + SequenceHeader (SequenceNumber, RequestId).
static void w_msg_prefix(UaWriter *w, uint32_t channel_id, uint32_t token_id, uint32_t seq, uint32_t request_id)
{
    pc_ua_w_u8(w, 'M');
    pc_ua_w_u8(w, 'S');
    pc_ua_w_u8(w, 'G');
    pc_ua_w_u8(w, 'F');
    pc_ua_w_u32(w, 0);          // size placeholder
    pc_ua_w_u32(w, channel_id); // SecureChannelId
    pc_ua_w_u32(w, token_id);   // SymmetricSecurityHeader.TokenId
    pc_ua_w_u32(w, seq);        // SequenceHeader.SequenceNumber
    pc_ua_w_u32(w, request_id); // SequenceHeader.RequestId
}

// Write a ResponseHeader (the prefix of every service response).
static void w_response_header(UaWriter *w, int64_t now_ft, uint32_t request_handle, uint32_t service_result)
{
    pc_ua_w_u64(w, (uint64_t)now_ft); // Timestamp
    pc_ua_w_u32(w, request_handle);   // RequestHandle echoed
    pc_ua_w_u32(w, service_result);   // ServiceResult
    pc_ua_w_u8(w, 0x00);              // ServiceDiagnostics (DiagnosticInfo: no fields)
    pc_ua_w_i32(w, -1);               // StringTable (null array)
    pc_ua_w_nodeid_numeric(w, 0, 0);  // AdditionalHeader: null NodeId ...
    pc_ua_w_u8(w, 0x00);              // ... + ExtensionObject "no body"
}

proto_bool pc_opcua_parse_msg(const uint8_t *msg, size_t len, OpcUaMsg *out)
{
    UaMsgHeader h;
    if (!pc_opcua_parse_header(msg, len, &h) || memcmp(h.type, "MSG", 3) != 0)
    {
        return PROTO_FALSE;
    }
    if (h.size != len)
    {
        return PROTO_FALSE;
    }

    UaReader r = {msg + 8, len - 8, 0, PROTO_FALSE};
    out->secure_channel_id = pc_ua_r_u32(&r); // SecureChannelId
    out->token_id = pc_ua_r_u32(&r);          // SymmetricSecurityHeader.TokenId
    out->sequence_number = pc_ua_r_u32(&r);   // SequenceHeader.SequenceNumber
    out->request_id = pc_ua_r_u32(&r);        // SequenceHeader.RequestId

    UaNodeId tid;
    if (!pc_ua_r_nodeid(&r, &tid)) // body TypeId
    {
        return PROTO_FALSE;
    }
    out->type_id = tid.numeric ? tid.id : 0;

    return r_request_header(&r, &out->request_handle);
}

// Transport profile URI for UA-TCP / UA-SecureConversation / UA Binary: an OPC UA
// spec identifier string, never dereferenced as a URL.
static const char OPCUA_TRANSPORT_URI[] =
    "http://opcfoundation.org/UA-Profile/Transport/uatcp-uasc-uabinary"; // NOSONAR

// The server-identity defaults (PC_PC_OPCUA_DEFAULT_ENDPOINT / _APP_URI / _APP_NAME) live in
// protocore_config.h under PC_ENABLE_OPCUA so a deployment can override them; used here for both
// the struct default and the builder fallback so the two cannot drift apart.

// All OPC UA agent state, owned by one instance (internal linkage): the advertised server
// identity, the application Read/Write/Browse resolvers, and (ESP32 only) the per-channel
// reassembly / response buffers and the SecureChannel + Session state (single client at a
// time). Grouped so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    OpcUaServerInfo server_info;
    OpcUaReadHandler read_handler;
    OpcUaWriteHandler write_handler;
    OpcUaBrowseHandler browse_handler;
#if PROTOCORE_HOT
    uint8_t msg[PC_OPCUA_BUF]; // single-accessor reassembly buffer
    uint8_t resp[2048];        // single-accessor response buffer (ACK / OPN / MSG response)
    uint32_t channel_id;
    uint32_t token_id;
    uint32_t seq;
    uint32_t session_id;
    uint32_t auth_token;
#endif
} OpcuaCtx;
// server_info carries the only non-zero default: the response builders read it directly, so it is
// set here rather than left to the zero fill. Every other member starts at 0 / NULL.
static OpcuaCtx s_opcua = {
    .server_info = {PC_OPCUA_DEFAULT_ENDPOINT, PC_OPCUA_DEFAULT_APP_URI, PC_OPCUA_DEFAULT_APP_NAME},
};

void pc_opcua_set_endpoint_url(const char *url)
{
    s_opcua.server_info.endpoint_url = url;
}

void pc_ua_w_endpoint_description(UaWriter *w, const OpcUaServerInfo *info)
{
    const char *url = (info && info->endpoint_url) ? info->endpoint_url : PC_OPCUA_DEFAULT_ENDPOINT;
    const char *auri = (info && info->application_uri) ? info->application_uri : PC_OPCUA_DEFAULT_APP_URI;
    const char *aname = (info && info->application_name) ? info->application_name : PC_OPCUA_DEFAULT_APP_NAME;

    pc_ua_w_string(w, url, (int32_t)strnlen(url, w->cap)); // EndpointUrl
    // Server (ApplicationDescription).
    pc_ua_w_string(w, auri, (int32_t)strnlen(auri, w->cap)); // ApplicationUri
    pc_ua_w_string(w, "urn:det:opcua", 13);                  // ProductUri
    pc_ua_w_localizedtext(w, NULL, aname);                   // ApplicationName
    pc_ua_w_u32(w, 0);                                       // ApplicationType = Server
    pc_ua_w_string(w, NULL, -1);                             // GatewayServerUri
    pc_ua_w_string(w, NULL, -1);                             // DiscoveryProfileUri
    pc_ua_w_i32(w, -1);                                      // DiscoveryUrls[] (null)
    pc_ua_w_string(w, NULL, -1);                             // ServerCertificate (ByteString, null)
    pc_ua_w_u32(w, 1);                                       // MessageSecurityMode = None
    pc_ua_w_string(w, OPCUA_POLICY_NONE_URI, (int32_t)(sizeof(OPCUA_POLICY_NONE_URI) - 1)); // SecurityPolicyUri
    // UserIdentityTokens[] - one Anonymous policy.
    pc_ua_w_i32(w, 1);
    pc_ua_w_string(w, "anonymous", 9);                                                  // UserTokenPolicy.PolicyId
    pc_ua_w_u32(w, 0);                                                                  // TokenType = Anonymous
    pc_ua_w_string(w, NULL, -1);                                                        // IssuedTokenType
    pc_ua_w_string(w, NULL, -1);                                                        // IssuerEndpointUrl
    pc_ua_w_string(w, NULL, -1);                                                        // SecurityPolicyUri
    pc_ua_w_string(w, OPCUA_TRANSPORT_URI, (int32_t)(sizeof(OPCUA_TRANSPORT_URI) - 1)); // TransportProfileUri
    pc_ua_w_u8(w, 0);                                                                   // SecurityLevel (Byte)
}

size_t pc_opcua_build_create_session_response(const OpcUaMsg *req, uint32_t session_id, uint32_t auth_token,
                                              double revised_timeout, const OpcUaServerInfo *info, uint32_t seq,
                                              int64_t now_ft, uint8_t *out, size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    w_msg_prefix(&w, req->secure_channel_id, req->token_id, seq, req->request_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_CREATE_SESSION_RESP);
    w_response_header(&w, now_ft, req->request_handle, 0);

    pc_ua_w_nodeid_numeric(&w, 1, session_id); // SessionId (server-assigned)
    pc_ua_w_nodeid_numeric(&w, 1, auth_token); // AuthenticationToken (server-assigned)
    pc_ua_w_f64(&w, revised_timeout);          // RevisedSessionTimeout (ms)
    pc_ua_w_string(&w, NULL, -1);              // ServerNonce (none for SecurityPolicy None)
    pc_ua_w_string(&w, NULL, -1);              // ServerCertificate
    pc_ua_w_i32(&w, 1);                        // ServerEndpoints[] - advertise one None endpoint
    pc_ua_w_endpoint_description(&w, info);
    pc_ua_w_i32(&w, 0);           // ServerSoftwareCertificates[] (empty)
    pc_ua_w_string(&w, NULL, -1); // ServerSignature.Algorithm (null String)
    pc_ua_w_string(&w, NULL, -1); // ServerSignature.Signature (null ByteString)
    pc_ua_w_u32(&w, 0);           // MaxRequestMessageSize (0 = no limit)
    return patch_size(&w);
}

size_t pc_opcua_build_get_endpoints_response(const OpcUaMsg *req, const OpcUaServerInfo *info, uint32_t seq,
                                             int64_t now_ft, uint8_t *out, size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    w_msg_prefix(&w, req->secure_channel_id, req->token_id, seq, req->request_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_GET_ENDPOINTS_RESP);
    w_response_header(&w, now_ft, req->request_handle, 0);
    pc_ua_w_i32(&w, 1); // Endpoints[] - one SecurityPolicy None endpoint
    pc_ua_w_endpoint_description(&w, info);
    return patch_size(&w);
}

size_t pc_opcua_build_service_fault(const OpcUaMsg *req, uint32_t service_result, uint32_t seq, int64_t now_ft,
                                    uint8_t *out, size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    w_msg_prefix(&w, req->secure_channel_id, req->token_id, seq, req->request_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_SERVICE_FAULT);
    w_response_header(&w, now_ft, req->request_handle, service_result); // ServiceFault = ResponseHeader only
    return patch_size(&w);
}

size_t pc_opcua_build_activate_session_response(const OpcUaMsg *req, uint32_t seq, int64_t now_ft, uint8_t *out,
                                                size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    w_msg_prefix(&w, req->secure_channel_id, req->token_id, seq, req->request_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_ACTIVATE_SESSION_RESP);
    w_response_header(&w, now_ft, req->request_handle, 0);

    pc_ua_w_string(&w, NULL, -1); // ServerNonce (none for SecurityPolicy None)
    pc_ua_w_i32(&w, 0);           // Results[] (empty)
    pc_ua_w_i32(&w, 0);           // DiagnosticInfos[] (empty)
    return patch_size(&w);
}

// ---------------------------------------------------------------------------
// Read service - Variant / DataValue encoding + ReadRequest/ReadResponse
// ---------------------------------------------------------------------------
void pc_ua_w_variant(UaWriter *w, const OpcUaVariant *v)
{
    if (!v || v->type == OPCUA_VAR_NULL)
    {
        pc_ua_w_u8(w, (uint8_t)OPCUA_VAR_NULL); // Null Variant (encoding byte 0)
        return;
    }
    pc_ua_w_u8(w, (uint8_t)v->type); // encoding byte = built-in type id (scalar; no array bits)
    switch (v->type)
    {
    case OPCUA_VAR_BOOL:
        pc_ua_w_bool(w, v->b);
        break;
    case OPCUA_VAR_INT32:
        pc_ua_w_i32(w, v->i32);
        break;
    case OPCUA_VAR_UINT32:
        pc_ua_w_u32(w, v->u32);
        break;
    case OPCUA_VAR_INT64:
        pc_ua_w_u64(w, (uint64_t)v->i64); // Int64 is two's-complement little-endian - same 8 bytes as UInt64
        break;
    case OPCUA_VAR_UINT64:
        pc_ua_w_u64(w, v->u64);
        break;
    case OPCUA_VAR_FLOAT:
        pc_ua_w_f32(w, v->f32);
        break;
    case OPCUA_VAR_DOUBLE:
        pc_ua_w_f64(w, v->f64);
        break;
    case OPCUA_VAR_STRING:
        pc_ua_w_string(w, v->str, v->str_len);
        break;
    default:
        w->ok = PROTO_FALSE; // unsupported type id: fail closed
        break;
    }
}

void pc_ua_w_datavalue(UaWriter *w, const OpcUaVariant *v, uint32_t status)
{
    proto_bool has_value = v && v->type != OPCUA_VAR_NULL;
    uint8_t mask = 0;
    if (has_value)
    {
        mask |= 0x01; // Value present
    }
    if (status != OPCUA_STATUS_GOOD)
    {
        mask |= 0x02; // StatusCode present
    }
    pc_ua_w_u8(w, mask);
    if (has_value)
    {
        pc_ua_w_variant(w, v);
    }
    if (status != OPCUA_STATUS_GOOD)
    {
        pc_ua_w_u32(w, status);
    }
}

proto_bool pc_ua_r_variant(UaReader *r, OpcUaVariant *out)
{
    memset(out, 0, sizeof(*out));
    uint8_t enc = pc_ua_r_u8(r);
    if (enc & 0x80) // array bit set: arrays are not supported by this scalar decoder
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    out->type = (OpcUaVariantType)(enc & 0x3F); // built-in type id (mask off array dimension flags)
    switch (out->type)
    {
    case OPCUA_VAR_NULL:
        break;
    case OPCUA_VAR_BOOL:
        out->b = pc_ua_r_bool(r);
        break;
    case OPCUA_VAR_INT32:
        out->i32 = pc_ua_r_i32(r);
        break;
    case OPCUA_VAR_UINT32:
        out->u32 = pc_ua_r_u32(r);
        break;
    case OPCUA_VAR_INT64:
        out->i64 = (int64_t)pc_ua_r_u64(r);
        break;
    case OPCUA_VAR_UINT64:
        out->u64 = pc_ua_r_u64(r);
        break;
    case OPCUA_VAR_FLOAT:
        out->f32 = pc_ua_r_f32(r);
        break;
    case OPCUA_VAR_DOUBLE:
        out->f64 = pc_ua_r_f64(r);
        break;
    case OPCUA_VAR_STRING: {
        int32_t sl = pc_ua_r_i32(r);
        out->str_len = sl;
        if (sl > 0)
        {
            if (r->off + (size_t)sl > r->len)
            {
                r->err = PROTO_TRUE;
                return PROTO_FALSE;
            }
            out->str = (const char *)(r->p + r->off); // points into the source buffer
            r->off += (size_t)sl;
        }
        break;
    }
    default:
        r->err = PROTO_TRUE; // unsupported built-in type
        return PROTO_FALSE;
    }
    return !r->err;
}

proto_bool pc_ua_r_datavalue(UaReader *r, OpcUaVariant *out_value, uint32_t *out_status)
{
    memset(out_value, 0, sizeof(*out_value));
    if (out_status)
    {
        *out_status = OPCUA_STATUS_GOOD;
    }
    uint8_t mask = pc_ua_r_u8(r);
    if (mask & 0x01) // Value (Variant)
    {
        if (!pc_ua_r_variant(r, out_value))
        {
            return PROTO_FALSE;
        }
    }
    if (mask & 0x02) // StatusCode
    {
        uint32_t st = pc_ua_r_u32(r);
        if (out_status)
        {
            *out_status = st;
        }
    }
    if (mask & 0x04) // SourceTimestamp (DateTime)
    {
        (void)pc_ua_r_u64(r);
    }
    if (mask & 0x10) // SourcePicoseconds (UInt16)
    {
        (void)pc_ua_r_u16(r);
    }
    if (mask & 0x08) // ServerTimestamp (DateTime)
    {
        (void)pc_ua_r_u64(r);
    }
    if (mask & 0x20) // ServerPicoseconds (UInt16)
    {
        (void)pc_ua_r_u16(r);
    }
    return !r->err;
}

proto_bool pc_opcua_parse_read(const uint8_t *msg, size_t len, OpcUaReadRequest *out)
{
    UaReader r;
    if (!r_msg_preamble(msg, len, &r, &out->msg))
    {
        return PROTO_FALSE;
    }

    // ReadRequest body.
    (void)pc_ua_r_f64(&r);         // MaxAge
    (void)pc_ua_r_u32(&r);         // TimestampsToReturn (enum)
    int32_t cnt = pc_ua_r_i32(&r); // NodesToRead array length
    out->total = (cnt < 0) ? 0 : (uint32_t)cnt;
    out->count = 0;
    for (int32_t i = 0; i < cnt; i++)
    {
        UaNodeId nid;
        if (!pc_ua_r_nodeid(&r, &nid)) // ReadValueId.NodeId
        {
            return PROTO_FALSE;
        }
        uint32_t attr = pc_ua_r_u32(&r); // AttributeId
        int32_t ir = pc_ua_r_i32(&r);    // IndexRange (String)
        if (ir > 0)
        {
            r_skip(&r, (size_t)ir);
        }
        (void)pc_ua_r_u16(&r);        // DataEncoding (QualifiedName) NamespaceIndex
        int32_t qn = pc_ua_r_i32(&r); // QualifiedName.Name (String)
        if (qn > 0)
        {
            r_skip(&r, (size_t)qn);
        }
        if (out->count < PC_OPCUA_READ_MAX)
        {
            OpcUaReadItem *it = &out->items[out->count++];
            it->ns = nid.ns;
            it->id = nid.id;
            it->numeric = nid.numeric;
            it->attribute = attr;
        }
    }
    return !r.err;
}

size_t pc_opcua_build_read_response(const OpcUaReadRequest *req, const OpcUaVariant *values, const uint32_t *statuses,
                                    uint32_t seq, int64_t now_ft, uint8_t *out, size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    w_msg_prefix(&w, req->msg.secure_channel_id, req->msg.token_id, seq, req->msg.request_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_READ_RESP);
    w_response_header(&w, now_ft, req->msg.request_handle, 0);

    pc_ua_w_i32(&w, (int32_t)req->count); // Results[] (one DataValue per captured node)
    for (uint32_t i = 0; i < req->count; i++)
    {
        pc_ua_w_datavalue(&w, values ? &values[i] : NULL, statuses ? statuses[i] : OPCUA_STATUS_GOOD);
    }
    pc_ua_w_i32(&w, 0); // DiagnosticInfos[] (empty)
    return patch_size(&w);
}

// Application Read resolver (set via pc_opcua_set_read_handler), used by pc_opcua_rx.
void pc_opcua_set_read_handler(OpcUaReadHandler fn)
{
    s_opcua.read_handler = fn;
}

// ---------------------------------------------------------------------------
// Write service - WriteRequest/WriteResponse
// ---------------------------------------------------------------------------
proto_bool pc_opcua_parse_write(const uint8_t *msg, size_t len, OpcUaWriteRequest *out)
{
    UaReader r;
    if (!r_msg_preamble(msg, len, &r, &out->msg))
    {
        return PROTO_FALSE;
    }

    int32_t cnt = pc_ua_r_i32(&r); // NodesToWrite array length
    out->total = (cnt < 0) ? 0 : (uint32_t)cnt;
    out->count = 0;
    for (int32_t i = 0; i < cnt; i++)
    {
        UaNodeId nid;
        if (!pc_ua_r_nodeid(&r, &nid)) // WriteValue.NodeId
        {
            return PROTO_FALSE;
        }
        uint32_t attr = pc_ua_r_u32(&r); // AttributeId
        int32_t ir = pc_ua_r_i32(&r);    // IndexRange (String)
        if (ir > 0)
        {
            r_skip(&r, (size_t)ir);
        }
        OpcUaVariant val;
        if (!pc_ua_r_datavalue(&r, &val, NULL)) // Value (DataValue)
        {
            return PROTO_FALSE;
        }
        if (out->count < PC_OPCUA_WRITE_MAX)
        {
            OpcUaWriteItem *it = &out->items[out->count++];
            it->ns = nid.ns;
            it->id = nid.id;
            it->numeric = nid.numeric;
            it->attribute = attr;
            it->value = val;
        }
    }
    return !r.err;
}

size_t pc_opcua_build_write_response(const OpcUaWriteRequest *req, const uint32_t *results, uint32_t seq,
                                     int64_t now_ft, uint8_t *out, size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    w_msg_prefix(&w, req->msg.secure_channel_id, req->msg.token_id, seq, req->msg.request_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_WRITE_RESP);
    w_response_header(&w, now_ft, req->msg.request_handle, 0);

    pc_ua_w_i32(&w, (int32_t)req->count); // Results[] (one StatusCode per node)
    for (uint32_t i = 0; i < req->count; i++)
    {
        pc_ua_w_u32(&w, results ? results[i] : OPCUA_STATUS_GOOD);
    }
    pc_ua_w_i32(&w, 0); // DiagnosticInfos[] (empty)
    return patch_size(&w);
}

// Application Write resolver (set via pc_opcua_set_write_handler), used by pc_opcua_rx.
void pc_opcua_set_write_handler(OpcUaWriteHandler fn)
{
    s_opcua.write_handler = fn;
}

// ---------------------------------------------------------------------------
// Browse service + CloseSession
// ---------------------------------------------------------------------------
void pc_ua_w_qualifiedname(UaWriter *w, uint16_t ns, const char *name)
{
    pc_ua_w_u16(w, ns);
    pc_ua_w_string(w, name, name ? (int32_t)strnlen(name, w->cap) : -1);
}

void pc_ua_w_localizedtext(UaWriter *w, const char *locale, const char *text)
{
    uint8_t mask = 0;
    if (locale)
    {
        mask |= 0x01; // Locale present
    }
    if (text)
    {
        mask |= 0x02; // Text present
    }
    pc_ua_w_u8(w, mask);
    if (locale)
    {
        pc_ua_w_string(w, locale, (int32_t)strnlen(locale, w->cap));
    }
    if (text)
    {
        pc_ua_w_string(w, text, (int32_t)strnlen(text, w->cap));
    }
}

void pc_ua_w_reference(UaWriter *w, const OpcUaReference *ref)
{
    if (!ref)
    {
        w->ok = PROTO_FALSE; // a lost reference fails the frame closed rather than being serialized
        return;
    }
    pc_ua_w_nodeid_numeric(w, 0, ref->ref_type_id);                  // ReferenceTypeId
    pc_ua_w_bool(w, ref->is_forward);                                // IsForward
    pc_ua_w_nodeid_numeric(w, ref->target_ns, ref->target_id);       // NodeId (ExpandedNodeId, numeric, no flags)
    pc_ua_w_qualifiedname(w, ref->browse_name_ns, ref->browse_name); // BrowseName
    pc_ua_w_localizedtext(w, NULL, ref->display_name);               // DisplayName
    pc_ua_w_u32(w, ref->node_class);                                 // NodeClass
    pc_ua_w_nodeid_numeric(w, 0, ref->type_def_id);                  // TypeDefinition (ExpandedNodeId, numeric)
}

proto_bool pc_opcua_parse_browse(const uint8_t *msg, size_t len, OpcUaBrowseRequest *out)
{
    UaReader r;
    if (!r_msg_preamble(msg, len, &r, &out->msg))
    {
        return PROTO_FALSE;
    }

    // BrowseRequest body: View (ViewDescription) + RequestedMaxReferencesPerNode + NodesToBrowse.
    UaNodeId view;
    pc_ua_r_nodeid(&r, &view); // View.ViewId
    (void)pc_ua_r_u64(&r);     // View.Timestamp
    (void)pc_ua_r_u32(&r);     // View.ViewVersion
    (void)pc_ua_r_u32(&r);     // RequestedMaxReferencesPerNode

    int32_t cnt = pc_ua_r_i32(&r); // NodesToBrowse array length
    out->total = (cnt < 0) ? 0 : (uint32_t)cnt;
    out->count = 0;
    for (int32_t i = 0; i < cnt; i++)
    {
        UaNodeId nid;
        if (!pc_ua_r_nodeid(&r, &nid)) // BrowseDescription.NodeId
        {
            return PROTO_FALSE;
        }
        (void)pc_ua_r_u32(&r); // BrowseDirection
        UaNodeId rt;
        pc_ua_r_nodeid(&r, &rt); // ReferenceTypeId
        (void)pc_ua_r_bool(&r);  // IncludeSubtypes
        (void)pc_ua_r_u32(&r);   // NodeClassMask
        (void)pc_ua_r_u32(&r);   // ResultMask
        if (out->count < PC_OPCUA_BROWSE_MAX)
        {
            OpcUaBrowseItem *it = &out->items[out->count++];
            it->ns = nid.ns;
            it->id = nid.id;
            it->numeric = nid.numeric;
        }
    }
    return !r.err;
}

size_t pc_opcua_build_browse_response(const OpcUaBrowseRequest *req, OpcUaBrowseHandler handler, uint32_t seq,
                                      int64_t now_ft, uint8_t *out, size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    w_msg_prefix(&w, req->msg.secure_channel_id, req->msg.token_id, seq, req->msg.request_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_BROWSE_RESP);
    w_response_header(&w, now_ft, req->msg.request_handle, 0);

    pc_ua_w_i32(&w, (int32_t)req->count); // Results[] (one BrowseResult per browsed node)
    for (uint32_t i = 0; i < req->count; i++)
    {
        OpcUaReference refs[PC_OPCUA_REF_MAX];
        int32_t n = handler ? handler(req->items[i].ns, req->items[i].id, refs, PC_OPCUA_REF_MAX) : -1;
        uint32_t status = (n < 0) ? OPCUA_STATUS_BAD_NODE_ID_UNKNOWN : OPCUA_STATUS_GOOD;
        uint32_t nrefs = (n < 0) ? 0 : (uint32_t)n;

        // BrowseResult.
        pc_ua_w_u32(&w, status);         // StatusCode
        pc_ua_w_string(&w, NULL, -1);    // ContinuationPoint (ByteString, null)
        pc_ua_w_i32(&w, (int32_t)nrefs); // References[]
        for (uint32_t j = 0; j < nrefs; j++)
        {
            pc_ua_w_reference(&w, &refs[j]);
        }
    }
    pc_ua_w_i32(&w, 0); // DiagnosticInfos[] (empty)
    return patch_size(&w);
}

size_t pc_opcua_build_close_session_response(const OpcUaMsg *req, uint32_t seq, int64_t now_ft, uint8_t *out,
                                             size_t cap)
{
    if (!req || !out)
    {
        return 0;
    }
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    w_msg_prefix(&w, req->secure_channel_id, req->token_id, seq, req->request_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_CLOSE_SESSION_RESP);
    w_response_header(&w, now_ft, req->request_handle, 0); // ResponseHeader only
    return patch_size(&w);
}

// Application Browse resolver (set via pc_opcua_set_browse_handler), used by pc_opcua_rx.
void pc_opcua_set_browse_handler(OpcUaBrowseHandler fn)
{
    s_opcua.browse_handler = fn;
}

// ---------------------------------------------------------------------------
// ESP32 TCP server (ConnProto::PROTO_OPCUA)
// ---------------------------------------------------------------------------
#if PROTOCORE_HOT

// Thin adapters over the transport RX read API - the ring is owned by transport;
// this service never indexes rx_buffer or advances rx_tail itself.
static size_t ring_avail(const TcpConn *c)
{
    return pc_conn_available(c->id);
}
static void ring_peek(const TcpConn *c, size_t off, uint8_t *dst, size_t n)
{
    pc_conn_peek(c->id, off, dst, n);
}
static void ring_consume(TcpConn *c, size_t n)
{
    pc_conn_consume(c->id, n);
}
static void raw_send(uint8_t slot, const void *data, size_t n)
{
    if (!pc_conn_active(slot) || n == 0)
    {
        return;
    }
    Tcp.conn->send(slot, data, (proto_u16)n);
    Tcp.conn->flush(slot);
}
static void close_conn(uint8_t slot)
{
    Tcp.conn->close(slot); // transport owns detach + slot reset + close
}

void pc_opcua_rx(uint8_t slot)
{
    if (!pc_conn_active(slot))
    {
        return;
    }
    TcpConn *c = &conn_pool[slot];

    // Drain every complete UACP message currently in the rx ring (a client may
    // pipeline HEL then OPN; each arrives framed by an 8-byte header + MessageSize).
    for (;;)
    {
        if (ring_avail(c) < 8)
        {
            return; // need the UACP header
        }

        uint8_t hdr[8];
        ring_peek(c, 0, hdr, 8);
        UaMsgHeader h;
        if (!pc_opcua_parse_header(hdr, 8, &h) || h.size < 8 || h.size > sizeof(s_opcua.msg))
        {
            close_conn(slot);
            return;
        }
        if (ring_avail(c) < h.size)
        {
            return; // wait for the full message
        }

        ring_peek(c, 0, s_opcua.msg, h.size);
        ring_consume(c, h.size);

        if (memcmp(h.type, "HEL", 3) == 0)
        {
            OpcUaHello hello;
            size_t n;
            if (pc_opcua_parse_hello(s_opcua.msg, h.size, &hello) &&
                (n = pc_opcua_build_ack(&hello, s_opcua.resp, sizeof(s_opcua.resp))) > 0)
            {
                raw_send(slot, s_opcua.resp, n);
            }
            else
            {
                close_conn(slot);
                return;
            }
        }
        else if (memcmp(h.type, "OPN", 3) == 0)
        {
            OpcUaOpenChannel oc;
            if (!pc_opcua_parse_open(s_opcua.msg, h.size, &oc))
            {
                close_conn(slot);
                return;
            }
            if (oc.secure_channel_id == 0) // fresh issue -> assign a channel id
            {
                oc.secure_channel_id = ++s_opcua.channel_id;
            }
            uint32_t token = ++s_opcua.token_id;
            uint32_t seq = ++s_opcua.seq;
            uint32_t lifetime = oc.requested_lifetime ? oc.requested_lifetime : 3600000u;
            int64_t now = pc_opcua_filetime_from_unix((int64_t)time(NULL));
            size_t n = pc_opcua_build_open_response(&oc, oc.secure_channel_id, token, seq, now, lifetime, s_opcua.resp,
                                                    sizeof(s_opcua.resp));
            if (n > 0)
            {
                raw_send(slot, s_opcua.resp, n);
            }
            else
            {
                close_conn(slot);
                return;
            }
        }
        else if (memcmp(h.type, "MSG", 3) == 0)
        {
            OpcUaMsg m;
            if (!pc_opcua_parse_msg(s_opcua.msg, h.size, &m))
            {
                close_conn(slot);
                return;
            }
            int64_t now = pc_opcua_filetime_from_unix((int64_t)time(NULL));
            uint32_t seq = ++s_opcua.seq;
            size_t n = 0;
            if (m.type_id == OPCUA_ID_GET_ENDPOINTS_REQ)
            {
                n = pc_opcua_build_get_endpoints_response(&m, &s_opcua.server_info, seq, now, s_opcua.resp,
                                                          sizeof(s_opcua.resp));
            }
            else if (m.type_id == OPCUA_ID_CREATE_SESSION_REQ)
            {
                n = pc_opcua_build_create_session_response(&m, ++s_opcua.session_id, ++s_opcua.auth_token, 1200000.0,
                                                           &s_opcua.server_info, seq, now, s_opcua.resp,
                                                           sizeof(s_opcua.resp));
            }
            else if (m.type_id == OPCUA_ID_ACTIVATE_SESSION_REQ)
            {
                n = pc_opcua_build_activate_session_response(&m, seq, now, s_opcua.resp, sizeof(s_opcua.resp));
            }
            else if (m.type_id == OPCUA_ID_READ_REQ)
            {
                OpcUaReadRequest rr;
                if (!pc_opcua_parse_read(s_opcua.msg, h.size, &rr))
                {
                    close_conn(slot);
                    return;
                }
                OpcUaVariant vals[PC_OPCUA_READ_MAX];
                uint32_t sts[PC_OPCUA_READ_MAX];
                for (uint32_t i = 0; i < rr.count; i++)
                {
                    memset(&vals[i], 0, sizeof(vals[i]));
                    proto_bool ok = s_opcua.read_handler && s_opcua.read_handler(rr.items[i].ns, rr.items[i].id,
                                                                                 rr.items[i].attribute, &vals[i]);
                    sts[i] = ok ? OPCUA_STATUS_GOOD : OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
                }
                n = pc_opcua_build_read_response(&rr, vals, sts, seq, now, s_opcua.resp, sizeof(s_opcua.resp));
            }
            else if (m.type_id == OPCUA_ID_BROWSE_REQ)
            {
                OpcUaBrowseRequest br;
                if (!pc_opcua_parse_browse(s_opcua.msg, h.size, &br))
                {
                    close_conn(slot);
                    return;
                }
                n = pc_opcua_build_browse_response(&br, s_opcua.browse_handler, seq, now, s_opcua.resp,
                                                   sizeof(s_opcua.resp));
            }
            else if (m.type_id == OPCUA_ID_WRITE_REQ)
            {
                OpcUaWriteRequest wr;
                if (!pc_opcua_parse_write(s_opcua.msg, h.size, &wr))
                {
                    close_conn(slot);
                    return;
                }
                uint32_t res[PC_OPCUA_WRITE_MAX];
                for (uint32_t i = 0; i < wr.count; i++)
                {
                    res[i] = s_opcua.write_handler ? s_opcua.write_handler(wr.items[i].ns, wr.items[i].id,
                                                                           wr.items[i].attribute, &wr.items[i].value)
                                                   : OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
                }
                n = pc_opcua_build_write_response(&wr, res, seq, now, s_opcua.resp, sizeof(s_opcua.resp));
            }
            else if (m.type_id == OPCUA_ID_CLOSE_SESSION_REQ)
            {
                n = pc_opcua_build_close_session_response(&m, seq, now, s_opcua.resp, sizeof(s_opcua.resp));
            }
            else // unknown/unsupported service -> ServiceFault (so the client never hangs)
            {
                n = pc_opcua_build_service_fault(&m, OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED, seq, now, s_opcua.resp,
                                                 sizeof(s_opcua.resp));
            }
            if (n > 0)
            {
                raw_send(slot, s_opcua.resp, n);
            }
        }
        else if (memcmp(h.type, "CLO", 3) == 0)
        {
            close_conn(slot);
            return;
        }
    }
}

// The OPC UA ProtoHandler (Layer 5 dispatch seam) - only a data handler; the handshake reads from
// the rx ring, so there is no per-connection accept/close/poll state. Returned by accessor (no
// session dependency); Session.proto->register_builtins() installs it.
static const ProtoHandler s_opcua_handler = {NULL, pc_opcua_rx, NULL, NULL};
const ProtoHandler *pc_opcua_proto_handler(void)
{
    return &s_opcua_handler;
}

#else // host build: the codec/handshake are tested directly; rx is a no-op stub

void pc_opcua_rx(uint8_t slot)
{
    (void)slot;
}
const ProtoHandler *pc_opcua_proto_handler(void)
{
    return NULL;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_OPCUA
