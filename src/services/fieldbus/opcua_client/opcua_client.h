// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_opcua_client.h
 * @brief OPC UA Binary client - request builders + response parsers (PC_ENABLE_OPCUA_CLIENT).
 *
 * The client side of the OPC UA Binary protocol, the mirror of services/opcua: it
 * builds the request messages a client sends (Hello, OpenSecureChannel,
 * CreateSession, ActivateSession, Read, Browse, CloseSession, CloseSecureChannel)
 * and parses the server's responses, reusing the same little-endian codec
 * (UaWriter / UaReader / NodeId / Variant / DataValue / QualifiedName /
 * LocalizedText) declared in opcua.h.
 *
 * It is **transport-agnostic**: every function works on caller-provided byte
 * buffers, so the application owns the outbound socket (for example an Arduino
 * WiFiClient) and just pumps these bytes. An OpcUaClient holds the small amount of
 * per-connection state (SecureChannelId, security TokenId, the session's
 * AuthenticationToken, and the monotonically increasing sequence / request ids).
 *
 * SecurityPolicy is None (no signing/encryption), matching the server. The response
 * parsers target that encoding (empty diagnostics, null string tables). No heap,
 * no stdlib.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OPCUA_CLIENT_H
#define PROTOCORE_OPCUA_CLIENT_H

#include "protocore_config.h"
#include "services/fieldbus/opcua/opcua.h"

#if PC_ENABLE_OPCUA_CLIENT

// Dependency (OPCUA_CLIENT requires OPCUA) is enforced centrally in protocore_config.h.

/** @brief Per-connection OPC UA client state (SecureChannel + Session + counters). */
typedef struct
{
    uint32_t channel_id;             ///< SecureChannelId (from the OPN response).
    uint32_t token_id;               ///< security TokenId (from the OPN response; sent in every MSG).
    uint16_t session_auth_ns;        ///< session AuthenticationToken NodeId namespace (from CreateSession).
    uint32_t session_auth_id;        ///< session AuthenticationToken NodeId identifier.
    proto_bool session_auth_numeric; ///< whether the AuthenticationToken is numeric.
    uint32_t seq;                    ///< client SequenceNumber (incremented per message).
    uint32_t request_id;             ///< client RequestId (incremented per message).
    uint32_t request_handle;         ///< client RequestHandle (incremented per request).
} OpcUaClient;

/** @brief Zero a client's state before the first connection. */
void pc_opcua_client_init(OpcUaClient *c);

// ---------------------------------------------------------------------------
// Request builders (return bytes written to @p out, or 0 if it does not fit)
// ---------------------------------------------------------------------------

/** @brief Build a `HEL` Hello, advertising PC_OPCUA_BUF buffer sizes. */
size_t pc_opcua_client_hello(const char *endpoint_url, uint8_t *out, size_t cap);

/** @brief Build an `OPN` OpenSecureChannelRequest (Issue, SecurityPolicy None). */
size_t pc_opcua_client_open(OpcUaClient *c, uint8_t *out, size_t cap);

/** @brief Build a `MSG` GetEndpointsRequest (no session needed). */
size_t pc_opcua_client_get_endpoints(OpcUaClient *c, const char *endpoint_url, uint8_t *out, size_t cap);

/** @brief Build a `MSG` CreateSessionRequest. */
size_t pc_opcua_client_create_session(OpcUaClient *c, const char *session_name, const char *endpoint_url, uint8_t *out,
                                      size_t cap);

/** @brief Build a `MSG` ActivateSessionRequest (anonymous user identity token). */
size_t pc_opcua_client_activate_session(OpcUaClient *c, uint8_t *out, size_t cap);

/** @brief Build a `MSG` ReadRequest for @p n nodes (each a NodeId + AttributeId). */
size_t pc_opcua_client_read(OpcUaClient *c, const OpcUaReadItem *items, uint32_t n, uint8_t *out, size_t cap);

/** @brief Build a `MSG` BrowseRequest for one node (forward references). */
size_t pc_opcua_client_browse(OpcUaClient *c, uint16_t ns, uint32_t id, uint8_t *out, size_t cap);

/** @brief Build a `MSG` WriteRequest writing @p n values (each item carries its Variant). */
size_t pc_opcua_client_write(OpcUaClient *c, const OpcUaWriteItem *items, uint32_t n, uint8_t *out, size_t cap);

/** @brief Build a `MSG` CloseSessionRequest. */
size_t pc_opcua_client_close_session(OpcUaClient *c, uint8_t *out, size_t cap);

/** @brief Build a `CLO` CloseSecureChannel message (the server closes the socket). */
size_t pc_opcua_client_close_channel(uint8_t *out, size_t cap);

// ---------------------------------------------------------------------------
// Response parsers (consume the server reply; update client state)
// ---------------------------------------------------------------------------

/** @brief Negotiated buffer sizes from an `ACK`. */
typedef struct
{
    uint32_t recv_buf_size;
    uint32_t send_buf_size;
    uint32_t max_msg_size;
    uint32_t max_chunk_count;
} OpcUaAckInfo;

/** @brief Parse an `ACK`. @return true if valid. */
proto_bool pc_opcua_client_on_ack(const uint8_t *msg, size_t len, OpcUaAckInfo *out);

/** @brief Parse an `OPN` OpenSecureChannelResponse; sets channel_id + token_id. @return true if Good. */
proto_bool pc_opcua_client_on_open(OpcUaClient *c, const uint8_t *msg, size_t len);

/** @brief Parse a GetEndpointsResponse. @return the advertised endpoint count, or -1 on error. */
int32_t pc_opcua_client_on_get_endpoints(const uint8_t *msg, size_t len);

/** @brief Parse a CreateSessionResponse; stores the session AuthenticationToken. @return true if Good. */
proto_bool pc_opcua_client_on_create_session(OpcUaClient *c, const uint8_t *msg, size_t len);

/** @brief Parse an ActivateSessionResponse. @return true if ServiceResult is Good. */
proto_bool pc_opcua_client_on_activate_session(const uint8_t *msg, size_t len);

/**
 * @brief Parse a ReadResponse into @p vals / @p statuses (one per result, capped at @p max).
 * @note A returned OPCUA_VAR_STRING value points into @p msg (keep it alive while used).
 * @return number of results, or -1 on a malformed/non-Good response.
 */
int32_t pc_opcua_client_on_read(const uint8_t *msg, size_t len, OpcUaVariant *vals, uint32_t *statuses, uint32_t max);

/** @brief One reference parsed from a BrowseResponse (self-contained; name is copied). */
typedef struct
{
    uint32_t ref_type_id;
    proto_bool is_forward;
    uint16_t target_ns;
    uint32_t target_id;
    uint32_t node_class;
    char browse_name[32];
} OpcUaClientRef;

/**
 * @brief Parse a BrowseResponse, flattening all results' references into @p refs (capped at @p max).
 * @return number of references, or -1 on a malformed/non-Good response.
 */
int32_t pc_opcua_client_on_browse(const uint8_t *msg, size_t len, OpcUaClientRef *refs, uint32_t max);

/**
 * @brief Parse a WriteResponse into @p results (one StatusCode per written node, capped at @p max).
 * @return number of results, or -1 on a malformed/non-Good response.
 */
int32_t pc_opcua_client_on_write(const uint8_t *msg, size_t len, uint32_t *results, uint32_t max);

#endif // PC_ENABLE_OPCUA_CLIENT
#endif // PROTOCORE_OPCUA_CLIENT_H
