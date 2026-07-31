// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file opcua.h
 * @brief OPC UA Binary server: handshake + SecureChannel + Session + Read/Write + Browse (PC_ENABLE_OPCUA).
 *
 * OPC UA (IEC 62541) is large; this is built in increments. **Increment 1** is the
 * foundation every OPC UA server needs:
 *   - the OPC UA Binary built-in-type codec (little-endian writer/reader for
 *     Boolean, integers, Float/Double, String, ByteString - bounds-checked, the
 *     basis for every later structure/service),
 *   - the UA-TCP connection protocol (UACP) message framing
 *     (`MessageType` + chunk byte + `MessageSize`), and
 *   - the **Hello / Acknowledge** handshake (OPC UA Part 6 §7.1.2): parse a
 *     client `HEL`, negotiate buffer sizes, emit an `ACK`.
 *
 * The codec + framing + handshake are pure and host-tested; pc_opcua_rx() is the
 * ConnProto::PROTO_OPCUA TCP data handler (ESP32) - `listen(4840, ConnProto::PROTO_OPCUA)` and a client
 * gets through the handshake. Session and the Read service are later increments.
 * SecurityPolicy is None (no encryption) for now.
 *
 * **Increment 2** adds the SecureChannel: NodeId / ExtensionObject / DateTime
 * encoding on top of the increment-1 codec, then parsing an `OpenSecureChannel`
 * (OPN) request and answering with an `OpenSecureChannelResponse` - the server
 * assigns a SecureChannelId + security token (SecurityPolicy None, OPC UA Part 6
 * §7.1.3 / Part 4 §5.5.2).
 *
 * **Increment 3** adds the Session: `MSG` (secure conversation) service calls
 * dispatched by their body TypeId - `CreateSession` (the server assigns a SessionId
 * and AuthenticationToken) and `ActivateSession` (OPC UA Part 4 §5.6).
 *
 * **Increment 4** adds the `Read` service (OPC UA Part 4 §5.10): scalar Variant +
 * DataValue encoding and a registered resolver that maps a NodeId/attribute to a
 * value - the first service that returns live data.
 *
 * **Increment 5** adds the `Browse` service (OPC UA Part 4 §5.8.2) via a registered
 * resolver (ReferenceDescription / QualifiedName / LocalizedText encoding) and
 * `CloseSession`; CloseSecureChannel arrives as a `CLO` message and closes the slot.
 *
 * Later increments add `GetEndpoints` + a `ServiceFault` fallback (so standard
 * clients interoperate) and the `Write` service (DataValue/Variant decode + a
 * registered write resolver). Verified end to end with python asyncua.
 *
 * No heap, no stdlib.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OPCUA_H
#define PROTOCORE_OPCUA_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_OPCUA

// ---------------------------------------------------------------------------
// OPC UA Binary built-in type codec (little-endian; OPC UA Part 6 §5.2)
// ---------------------------------------------------------------------------

/** @brief Bounds-checked little-endian writer. */
struct UaWriter
{
    uint8_t *o;
    size_t cap;
    size_t n;
    bool ok;
};
void pc_ua_w_u8(UaWriter *w, uint8_t v);
void pc_ua_w_u16(UaWriter *w, uint16_t v);
void pc_ua_w_u32(UaWriter *w, uint32_t v);
void pc_ua_w_u64(UaWriter *w, uint64_t v);
void pc_ua_w_i32(UaWriter *w, int32_t v);
void pc_ua_w_f32(UaWriter *w, float v);
void pc_ua_w_f64(UaWriter *w, double v);
void pc_ua_w_bool(UaWriter *w, bool v);
/** @brief Encode a String/ByteString: int32 length (-1 = null) then the bytes. */
void pc_ua_w_string(UaWriter *w, const char *s, int32_t len);

/** @brief Bounds-checked little-endian reader; @c err latches on underrun. */
struct UaReader
{
    const uint8_t *p;
    size_t len;
    size_t off;
    bool err;
};
uint8_t pc_ua_r_u8(UaReader *r);
uint16_t pc_ua_r_u16(UaReader *r);
uint32_t pc_ua_r_u32(UaReader *r);
uint64_t pc_ua_r_u64(UaReader *r);
int32_t pc_ua_r_i32(UaReader *r);
float pc_ua_r_f32(UaReader *r);
double pc_ua_r_f64(UaReader *r);
bool pc_ua_r_bool(UaReader *r);
/**
 * @brief Decode a String/ByteString into @p out (NUL-terminated, bounded).
 * @param out_len  set to the decoded length (or -1 for a null string).
 * @return false on underrun or if the value does not fit @p cap.
 */
bool pc_ua_r_string(UaReader *r, char *out, size_t cap, int32_t *out_len);

// ---------------------------------------------------------------------------
// UA-TCP (UACP) message framing
// ---------------------------------------------------------------------------

/** @brief Parsed UACP message header (8 bytes). */
struct UaMsgHeader
{
    char type[3];  ///< "HEL" / "ACK" / "ERR" / "OPN" / "MSG" / "CLO".
    char chunk;    ///< 'F' final, 'C' intermediate, 'A' abort.
    uint32_t size; ///< total message size including this 8-byte header.
};

/** @brief Parse the 8-byte UACP header from @p buf (need >= 8 bytes). */
bool pc_opcua_parse_header(const uint8_t *buf, size_t len, UaMsgHeader *h);

// ---------------------------------------------------------------------------
// Hello / Acknowledge handshake (OPC UA Part 6 §7.1.2)
// ---------------------------------------------------------------------------

/** @brief Decoded Hello message body. */
struct OpcUaHello
{
    uint32_t protocol_version;
    uint32_t recv_buf_size;
    uint32_t send_buf_size;
    uint32_t max_msg_size;
    uint32_t max_chunk_count;
};

/** @brief Parse a complete `HEL` message (header + body). @return true if valid. */
bool pc_opcua_parse_hello(const uint8_t *msg, size_t len, OpcUaHello *out);

/**
 * @brief Build the `ACK` reply to a parsed Hello, negotiating buffer sizes down
 *        to the server's PC_OPCUA_BUF limit.
 * @return total ACK message bytes written to @p out, or 0 if it does not fit.
 */
size_t pc_opcua_build_ack(const OpcUaHello *hello, uint8_t *out, size_t cap);

// Common transport-level status codes for a UACP ERR message (OPC UA Part 6 §7.1.2.4).
#define PC_OPCUA_BAD_TCP_MESSAGE_TYPE_INVALID 0x807E0000u
#define PC_OPCUA_BAD_TCP_MESSAGE_TOO_LARGE 0x80800000u
#define PC_OPCUA_BAD_TCP_NOT_ENOUGH_RESOURCES 0x80810000u
#define PC_OPCUA_BAD_TCP_INTERNAL_ERROR 0x80820000u

/**
 * @brief Build a transport-level `ERR` message (the reply a server sends before closing when the handshake
 *        fails): `ERR F <size> <error:UInt32> <reason:String>`. @p error_code is an OPC UA StatusCode
 *        (PC_OPCUA_BAD_TCP_*); @p reason is a short UTF-8 diagnostic (may be null for a null String).
 * @return total ERR message bytes written to @p out, or 0 on a null buffer / overflow.
 */
size_t pc_opcua_build_error(uint32_t error_code, const char *reason, uint8_t *out, size_t cap);

// ---------------------------------------------------------------------------
// NodeId / ExtensionObject / DateTime (OPC UA Part 6 §5.2.2)
// ---------------------------------------------------------------------------

/** @brief Numeric NodeId (the only kind the SecureChannel service needs). */
struct UaNodeId
{
    uint16_t ns;  ///< NamespaceIndex.
    uint32_t id;  ///< Numeric identifier.
    bool numeric; ///< false for a String/Guid/ByteString id (value skipped on read).
};

/** @brief Encode a numeric NodeId, picking the smallest of the TwoByte/FourByte/Numeric forms. */
void pc_ua_w_nodeid_numeric(UaWriter *w, uint16_t ns, uint32_t id);

/**
 * @brief Decode a NodeId. Numeric forms fill @p out; String/Guid/ByteString ids
 *        are skipped (out->numeric=false). Latches @c err on an unknown form.
 */
bool pc_ua_r_nodeid(UaReader *r, UaNodeId *out);

/** @brief Convert a Unix epoch (seconds) to an OPC UA DateTime (100 ns ticks since 1601), 0 for <= 0. */
int64_t pc_opcua_filetime_from_unix(int64_t unix_seconds);

/** @brief Numeric NodeIds (namespace 0) the SecureChannel service uses (binary encoding ids). */
#define OPCUA_ID_OPEN_REQ 446  ///< OpenSecureChannelRequest_Encoding_DefaultBinary.
#define OPCUA_ID_OPEN_RESP 449 ///< OpenSecureChannelResponse_Encoding_DefaultBinary.

/** @brief SecurityPolicy "None" URI (no signing/encryption). */
#define OPCUA_POLICY_NONE_URI "http://opcfoundation.org/UA/SecurityPolicy#None"

// ---------------------------------------------------------------------------
// SecureChannel - OpenSecureChannel (OPN), SecurityPolicy None
// ---------------------------------------------------------------------------

/** @brief Fields extracted from an OpenSecureChannelRequest we need to reply. */
struct OpcUaOpenChannel
{
    uint32_t secure_channel_id;           ///< 0 on a fresh issue; non-zero on renew.
    uint32_t sequence_number;             ///< Client SequenceHeader SequenceNumber.
    uint32_t request_id;                  ///< Client SequenceHeader RequestId (echoed).
    uint32_t request_handle;              ///< RequestHeader RequestHandle (echoed).
    uint32_t client_protocol_version;     ///< ClientProtocolVersion.
    uint32_t security_token_request_type; ///< 0 = Issue, 1 = Renew.
    uint32_t message_security_mode;       ///< 1 = None, 2 = Sign, 3 = SignAndEncrypt.
    uint32_t requested_lifetime;          ///< RequestedLifetime (ms).
};

/** @brief Parse a complete `OPN` message (SecurityPolicy None). @return true if valid. */
bool pc_opcua_parse_open(const uint8_t *msg, size_t len, OpcUaOpenChannel *out);

/**
 * @brief Build the `OPN` OpenSecureChannelResponse to a parsed request.
 * @param req        the parsed request (RequestId/RequestHandle are echoed).
 * @param channel_id SecureChannelId the server assigns/keeps.
 * @param token_id   security TokenId the server issues.
 * @param seq_number server SequenceNumber for this message.
 * @param now_ft     OPC UA DateTime for the response/token timestamps (0 = unset).
 * @param lifetime   RevisedLifetime (ms) granted to the token.
 * @return total OPN message bytes written, or 0 if it does not fit @p cap.
 */
size_t pc_opcua_build_open_response(const OpcUaOpenChannel *req, uint32_t channel_id, uint32_t token_id,
                                    uint32_t seq_number, int64_t now_ft, uint32_t lifetime, uint8_t *out, size_t cap);

// ---------------------------------------------------------------------------
// Session - CreateSession / ActivateSession (MSG service calls, SecurityPolicy None)
// ---------------------------------------------------------------------------

/** @brief Numeric NodeIds (namespace 0) for the Session services (binary encoding ids). */
#define OPCUA_ID_CREATE_SESSION_REQ 461    ///< CreateSessionRequest_Encoding_DefaultBinary.
#define OPCUA_ID_CREATE_SESSION_RESP 464   ///< CreateSessionResponse_Encoding_DefaultBinary.
#define OPCUA_ID_ACTIVATE_SESSION_REQ 467  ///< ActivateSessionRequest_Encoding_DefaultBinary.
#define OPCUA_ID_ACTIVATE_SESSION_RESP 470 ///< ActivateSessionResponse_Encoding_DefaultBinary.

/** @brief Common fields of a `MSG` (secure conversation) service request. */
struct OpcUaMsg
{
    uint32_t secure_channel_id; ///< SecureChannelId (echoed in the response).
    uint32_t token_id;          ///< SymmetricSecurityHeader TokenId.
    uint32_t sequence_number;   ///< SequenceHeader SequenceNumber.
    uint32_t request_id;        ///< SequenceHeader RequestId (echoed in the response).
    uint32_t type_id;           ///< Body TypeId NodeId (numeric id; 0 if non-numeric).
    uint32_t request_handle;    ///< RequestHeader RequestHandle (echoed in the response).
};

/**
 * @brief Parse a `MSG` envelope: security + sequence headers, body TypeId, and the
 *        leading RequestHeader (every service request starts with one).
 * @return true if valid. @p out->type_id selects the service to dispatch.
 */
bool pc_opcua_parse_msg(const uint8_t *msg, size_t len, OpcUaMsg *out);

/** @brief Identity the server advertises in GetEndpoints / CreateSession endpoint descriptions. */
struct OpcUaServerInfo
{
    const char *endpoint_url;     ///< e.g. "opc.tcp://192.168.1.85:4840".
    const char *application_uri;  ///< server ApplicationUri.
    const char *application_name; ///< server ApplicationName (display text).
};

/**
 * @brief Build a `MSG` CreateSessionResponse (SecurityPolicy None): assign a SessionId
 *        and AuthenticationToken and advertise one None endpoint in ServerEndpoints so
 *        a client's endpoint validation matches GetEndpoints.
 * @param req             the parsed request (TokenId/RequestId/RequestHandle echoed).
 * @param session_id      numeric SessionId identifier the server assigns (namespace 1).
 * @param auth_token      numeric AuthenticationToken the server assigns (namespace 1).
 * @param revised_timeout RevisedSessionTimeout (ms).
 * @param info            server identity for the advertised endpoint (may be null for defaults).
 * @param seq             server SequenceNumber for this message.
 * @param now_ft          OPC UA DateTime for the response timestamp (0 = unset).
 * @return total MSG bytes written, or 0 if it does not fit @p cap.
 */
size_t pc_opcua_build_create_session_response(const OpcUaMsg *req, uint32_t session_id, uint32_t auth_token,
                                              double revised_timeout, const OpcUaServerInfo *info, uint32_t seq,
                                              int64_t now_ft, uint8_t *out, size_t cap);

/**
 * @brief Build a `MSG` ActivateSessionResponse (SecurityPolicy None): ServiceResult Good,
 *        empty ServerNonce / Results / DiagnosticInfos.
 * @return total MSG bytes written, or 0 if it does not fit @p cap.
 */
size_t pc_opcua_build_activate_session_response(const OpcUaMsg *req, uint32_t seq, int64_t now_ft, uint8_t *out,
                                                size_t cap);

// ---------------------------------------------------------------------------
// Read service (MSG, SecurityPolicy None) + Variant / DataValue encoding
// ---------------------------------------------------------------------------

/** @brief Numeric NodeIds (namespace 0) for the Read service (binary encoding ids). */
#define OPCUA_ID_READ_REQ 631  ///< ReadRequest_Encoding_DefaultBinary.
#define OPCUA_ID_READ_RESP 634 ///< ReadResponse_Encoding_DefaultBinary.

/** @brief The OPC UA Attribute id a Read usually wants (the node's Value). */
#define OPCUA_ATTR_VALUE 13

/** @brief StatusCodes the Read service returns. */
#define OPCUA_STATUS_GOOD 0x00000000u
#define OPCUA_STATUS_BAD_NODE_ID_UNKNOWN 0x80340000u

/** @brief OPC UA built-in type ids for the scalar Variants the Read service encodes. */
enum class OpcUaVariantType : uint8_t
{
    OPCUA_VAR_NULL = 0,
    OPCUA_VAR_BOOL = 1,
    OPCUA_VAR_INT32 = 6,
    OPCUA_VAR_UINT32 = 7,
    OPCUA_VAR_INT64 = 8,
    OPCUA_VAR_UINT64 = 9,
    OPCUA_VAR_FLOAT = 10,
    OPCUA_VAR_DOUBLE = 11,
    OPCUA_VAR_STRING = 12,
};

/** @brief A scalar OPC UA Variant value (the supported built-in types). */
struct OpcUaVariant
{
    OpcUaVariantType type; ///< 0 = null Variant.
    bool b;                ///< OpcUaVariantType::OPCUA_VAR_BOOL.
    int32_t i32;           ///< OpcUaVariantType::OPCUA_VAR_INT32.
    uint32_t u32;          ///< OpcUaVariantType::OPCUA_VAR_UINT32.
    int64_t i64;           ///< OpcUaVariantType::OPCUA_VAR_INT64.
    uint64_t u64;          ///< OpcUaVariantType::OPCUA_VAR_UINT64.
    float f32;             ///< OpcUaVariantType::OPCUA_VAR_FLOAT.
    double f64;            ///< OpcUaVariantType::OPCUA_VAR_DOUBLE.
    const char *str;       ///< OpcUaVariantType::OPCUA_VAR_STRING (referenced, not copied).
    int32_t str_len;       ///< string length (-1 = null string).
};

/** @brief Encode a scalar Variant: encoding byte (built-in type id) then the value. */
void pc_ua_w_variant(UaWriter *w, const OpcUaVariant *v);

/** @brief Encode a DataValue: a value/status mask byte then the Variant and/or StatusCode. */
void pc_ua_w_datavalue(UaWriter *w, const OpcUaVariant *v, uint32_t status);

/**
 * @brief Decode a scalar Variant. A decoded OpcUaVariantType::OPCUA_VAR_STRING points into the source
 *        buffer (keep it alive). Non-scalar/array Variants latch @c err.
 */
bool pc_ua_r_variant(UaReader *r, OpcUaVariant *out);

/**
 * @brief Decode a DataValue: the mask byte, then (if present) the Variant value and
 *        StatusCode; SourceTimestamp/ServerTimestamp (and picoseconds) are skipped.
 * @param out_value filled with the value (type 0 if no value field present).
 * @param out_status set to the StatusCode (0 if not present).
 */
bool pc_ua_r_datavalue(UaReader *r, OpcUaVariant *out_value, uint32_t *out_status);

/** @brief One NodeId + attribute the client wants to read (a ReadValueId). */
struct OpcUaReadItem
{
    uint16_t ns;
    uint32_t id;
    bool numeric;
    uint32_t attribute;
};

/** @brief Parsed ReadRequest: the MSG envelope plus the (bounded) NodesToRead list. */
struct OpcUaReadRequest
{
    OpcUaMsg msg;   ///< envelope + RequestHeader (type_id = ReadRequest).
    uint32_t total; ///< nodes requested (may exceed the captured count).
    uint32_t count; ///< nodes captured (clamped to PC_OPCUA_READ_MAX).
    OpcUaReadItem items[PC_OPCUA_READ_MAX];
};

/** @brief Parse a `MSG` ReadRequest. @return true if valid. */
bool pc_opcua_parse_read(const uint8_t *msg, size_t len, OpcUaReadRequest *out);

/**
 * @brief Build a `MSG` ReadResponse: one DataValue per captured NodesToRead entry.
 * @param values   per-node values (values[i] paired with req->items[i]); null type = no value.
 * @param statuses per-node StatusCodes (0 = Good).
 * @return total MSG bytes written, or 0 if it does not fit @p cap.
 */
size_t pc_opcua_build_read_response(const OpcUaReadRequest *req, const OpcUaVariant *values, const uint32_t *statuses,
                                    uint32_t seq, int64_t now_ft, uint8_t *out, size_t cap);

/**
 * @brief Application Read resolver: fill @p out for (ns, id, attribute). Return false
 *        for an unknown node/attribute (the server answers BadNodeIdUnknown).
 */
typedef bool (*OpcUaReadHandler)(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out);

/** @brief Register the Read resolver the ConnProto::PROTO_OPCUA server calls for each ReadRequest node. */
void pc_opcua_set_read_handler(OpcUaReadHandler fn);

// ---------------------------------------------------------------------------
// Browse service + Close (MSG, SecurityPolicy None)
// ---------------------------------------------------------------------------

/** @brief Numeric NodeIds (namespace 0) for Browse / Close services (binary encoding ids). */
#define OPCUA_ID_BROWSE_REQ 527         ///< BrowseRequest_Encoding_DefaultBinary.
#define OPCUA_ID_BROWSE_RESP 530        ///< BrowseResponse_Encoding_DefaultBinary.
#define OPCUA_ID_CLOSE_SESSION_REQ 473  ///< CloseSessionRequest_Encoding_DefaultBinary.
#define OPCUA_ID_CLOSE_SESSION_RESP 476 ///< CloseSessionResponse_Encoding_DefaultBinary.

/** @brief Common NodeClass / ReferenceType / TypeDefinition ids (namespace 0) for Browse results. */
#define OPCUA_NODECLASS_OBJECT 1
#define OPCUA_NODECLASS_VARIABLE 2
#define OPCUA_REFTYPE_ORGANIZES 35
#define OPCUA_REFTYPE_HAS_COMPONENT 47
#define OPCUA_TYPEDEF_BASE_OBJECT 58
#define OPCUA_TYPEDEF_BASE_DATA_VARIABLE 63

/** @brief Encode a QualifiedName: NamespaceIndex (UInt16) + Name (String). */
void pc_ua_w_qualifiedname(UaWriter *w, uint16_t ns, const char *name);

/** @brief Encode a LocalizedText: a present-fields mask then the optional Locale / Text Strings. */
void pc_ua_w_localizedtext(UaWriter *w, const char *locale, const char *text);

/** @brief One reference (ReferenceDescription) returned by a Browse. Strings are referenced, not copied. */
struct OpcUaReference
{
    uint32_t ref_type_id;     ///< ReferenceType NodeId numeric id (e.g. OPCUA_REFTYPE_ORGANIZES).
    bool is_forward;          ///< IsForward.
    uint16_t target_ns;       ///< target NodeId namespace.
    uint32_t target_id;       ///< target NodeId numeric id.
    uint16_t browse_name_ns;  ///< BrowseName namespace.
    const char *browse_name;  ///< BrowseName.Name.
    const char *display_name; ///< DisplayName text.
    uint32_t node_class;      ///< NodeClass (e.g. OPCUA_NODECLASS_VARIABLE).
    uint32_t type_def_id;     ///< TypeDefinition NodeId numeric id (e.g. OPCUA_TYPEDEF_BASE_DATA_VARIABLE).
};

/** @brief Encode a ReferenceDescription. */
void pc_ua_w_reference(UaWriter *w, const OpcUaReference *ref);

/** @brief One NodeId the client wants to browse (a BrowseDescription). */
struct OpcUaBrowseItem
{
    uint16_t ns;
    uint32_t id;
    bool numeric;
};

/** @brief Parsed BrowseRequest: the MSG envelope plus the (bounded) NodesToBrowse list. */
struct OpcUaBrowseRequest
{
    OpcUaMsg msg;
    uint32_t total;
    uint32_t count;
    OpcUaBrowseItem items[PC_OPCUA_BROWSE_MAX];
};

/** @brief Parse a `MSG` BrowseRequest. @return true if valid. */
bool pc_opcua_parse_browse(const uint8_t *msg, size_t len, OpcUaBrowseRequest *out);

/**
 * @brief Application Browse resolver: write up to @p max references for (ns, id) into
 *        @p out. @return the count written, or -1 for an unknown node (the server
 *        answers BadNodeIdUnknown for that BrowseResult).
 */
typedef int32_t (*OpcUaBrowseHandler)(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max);

/** @brief Register the Browse resolver the ConnProto::PROTO_OPCUA server calls for each browsed node. */
void pc_opcua_set_browse_handler(OpcUaBrowseHandler fn);

/**
 * @brief Build a `MSG` BrowseResponse: one BrowseResult per browsed node, each with the
 *        references the @p handler returns (up to PC_OPCUA_REF_MAX).
 * @return total MSG bytes written, or 0 if it does not fit @p cap.
 */
size_t pc_opcua_build_browse_response(const OpcUaBrowseRequest *req, OpcUaBrowseHandler handler, uint32_t seq,
                                      int64_t now_ft, uint8_t *out, size_t cap);

/** @brief Build a `MSG` CloseSessionResponse (ResponseHeader only, ServiceResult Good). */
size_t pc_opcua_build_close_session_response(const OpcUaMsg *req, uint32_t seq, int64_t now_ft, uint8_t *out,
                                             size_t cap);

// ---------------------------------------------------------------------------
// GetEndpoints + ServiceFault (third-party client interop)
// ---------------------------------------------------------------------------

/** @brief Numeric NodeIds (namespace 0) for GetEndpoints / ServiceFault (binary encoding ids). */
#define OPCUA_ID_GET_ENDPOINTS_REQ 428  ///< GetEndpointsRequest_Encoding_DefaultBinary.
#define OPCUA_ID_GET_ENDPOINTS_RESP 431 ///< GetEndpointsResponse_Encoding_DefaultBinary.
#define OPCUA_ID_SERVICE_FAULT 397      ///< ServiceFault_Encoding_DefaultBinary.

/** @brief StatusCode for an unsupported service (returned in a ServiceFault). */
#define OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED 0x800B0000u

/** @brief Encode one EndpointDescription (SecurityMode/Policy None, one Anonymous user-token policy). */
void pc_ua_w_endpoint_description(UaWriter *w, const OpcUaServerInfo *info);

/** @brief Build a `MSG` GetEndpointsResponse advertising a single SecurityPolicy None endpoint. */
size_t pc_opcua_build_get_endpoints_response(const OpcUaMsg *req, const OpcUaServerInfo *info, uint32_t seq,
                                             int64_t now_ft, uint8_t *out, size_t cap);

/**
 * @brief Build a `MSG` ServiceFault (TypeId i=397) - a bare ResponseHeader carrying
 *        @p service_result, the reply for an unsupported/unknown service request.
 */
size_t pc_opcua_build_service_fault(const OpcUaMsg *req, uint32_t service_result, uint32_t seq, int64_t now_ft,
                                    uint8_t *out, size_t cap);

/** @brief Set the endpoint URL the ConnProto::PROTO_OPCUA server advertises (GetEndpoints / CreateSession). */
void pc_opcua_set_endpoint_url(const char *url);

// ---------------------------------------------------------------------------
// Write service (MSG, SecurityPolicy None)
// ---------------------------------------------------------------------------

/** @brief Numeric NodeIds (namespace 0) for the Write service (binary encoding ids). */
#define OPCUA_ID_WRITE_REQ 673  ///< WriteRequest_Encoding_DefaultBinary.
#define OPCUA_ID_WRITE_RESP 676 ///< WriteResponse_Encoding_DefaultBinary.

/** @brief StatusCode for a node/attribute that cannot be written. */
#define OPCUA_STATUS_BAD_NOT_WRITABLE 0x803B0000u

/** @brief One value the client wants to write (a WriteValue). */
struct OpcUaWriteItem
{
    uint16_t ns;
    uint32_t id;
    bool numeric;
    uint32_t attribute;
    OpcUaVariant value; ///< the DataValue's Variant (string values point into the source buffer).
};

/** @brief Parsed WriteRequest: the MSG envelope plus the (bounded) NodesToWrite list. */
struct OpcUaWriteRequest
{
    OpcUaMsg msg;
    uint32_t total;
    uint32_t count;
    OpcUaWriteItem items[PC_OPCUA_WRITE_MAX];
};

/** @brief Parse a `MSG` WriteRequest. @return true if valid. */
bool pc_opcua_parse_write(const uint8_t *msg, size_t len, OpcUaWriteRequest *out);

/**
 * @brief Build a `MSG` WriteResponse: one StatusCode per NodesToWrite entry.
 * @param results per-node StatusCodes (0 = Good); may be null (all Good).
 * @return total MSG bytes written, or 0 if it does not fit @p cap.
 */
size_t pc_opcua_build_write_response(const OpcUaWriteRequest *req, const uint32_t *results, uint32_t seq,
                                     int64_t now_ft, uint8_t *out, size_t cap);

/**
 * @brief Application Write resolver: apply @p value to (ns, id, attribute) and return a
 *        StatusCode (0 = Good). Return a Bad code (e.g. OPCUA_STATUS_BAD_NODE_ID_UNKNOWN
 *        or OPCUA_STATUS_BAD_NOT_WRITABLE) to reject the write.
 */
typedef uint32_t (*OpcUaWriteHandler)(uint16_t ns, uint32_t id, uint32_t attribute, const OpcUaVariant *value);

/** @brief Register the Write resolver the ConnProto::PROTO_OPCUA server calls for each WriteRequest node. */
void pc_opcua_set_write_handler(OpcUaWriteHandler fn);

// ---------------------------------------------------------------------------
// ESP32 TCP server (ConnProto::PROTO_OPCUA data handler)
// ---------------------------------------------------------------------------

/**
 * @brief ConnProto::PROTO_OPCUA data handler: handshake, SecureChannel, Session, Read.
 *
 * Dispatched by the session layer for connections accepted on an OPC UA listener
 * (`listen(4840, ConnProto::PROTO_OPCUA)`). Drains framed messages from the slot's rx ring:
 * `HEL` -> negotiated `ACK`, `OPN` -> OpenSecureChannelResponse (SecurityPolicy
 * None), `MSG` GetEndpoints/CreateSession/ActivateSession/Read/Write/Browse/
 * CloseSession -> their responses (Read/Write/Browse call the registered resolvers;
 * an unknown service draws a ServiceFault), `CLO` (CloseSecureChannel) -> close.
 */
void pc_opcua_rx(uint8_t slot);

/** @brief The OPC UA ProtoHandler (accessor; nullptr on host builds; installed by the builtins list). */
struct ProtoHandler;
const struct ProtoHandler *pc_opcua_proto_handler(void);

#endif // PC_ENABLE_OPCUA
#endif // PROTOCORE_OPCUA_H
