# RFC Compliance

This document details the standards conformance of ProtoCore's
HTTP/1.1 (request parsing, response generation, keep-alive, Range), authentication
(Basic / Digest / JWT), WebSocket (server and client), TLS / mTLS, CoAP, SNMP, Modbus
TCP, OPC UA, syslog, the outbound HTTP and MQTT clients, and error-handling behavior.
(SSH conformance lives in [SSH.md](SSH.md); the TLS posture is in [SECURITY.md](SECURITY.md).)
HTTP/2 (RFC 9113 / HPACK) and the HTTP/3 stack in progress (QUIC RFC 9000, QUIC-TLS
RFC 9001, HTTP/3 RFC 9114, QPACK RFC 9204) are tracked in [STANDARDS.md](STANDARDS.md).

## HTTP/1.1 request parsing (RFC 7230)

The parser enforces these rules byte-by-byte during parsing:

<details>
<summary><b>HTTP/1.1 Parsing Conformance Table</b></summary>

| Field              | Allowed characters                                                   | RFC reference | Violation response                  |
| ------------------ | -------------------------------------------------------------------- | ------------- | ----------------------------------- |
| Method             | `tchar` (`ALPHA DIGIT ! # $ % & ' * + - . ^ _ \` \| ~`)              | §3.1.1        | 400                                 |
| Path / Query       | `VCHAR` (%x21–7E)                                                    | RFC 3986 §3.3 | 400                                 |
| Header field-name  | `tchar`                                                              | §3.2          | 400                                 |
| Header field-value | `VCHAR`, SP, HTAB, obs-text (%x80–FF)                                | §3.2          | 400                                 |
| Path length        | ≤ `MAX_PATH_LEN − 1` bytes                                           | §3.1.1        | 414                                 |
| Body size          | ≤ [`BODY_BUF_SIZE`](@ref BODY_BUF_SIZE) bytes (via `Content-Length`) | §3.3.2        | 413                                 |
| Content-Length     | Must be `1*DIGIT`; conflicting duplicates rejected                   | §3.3.2        | 400                                 |
| Host header        | Required for HTTP/1.1; more than one always rejected                 | §5.4          | 400                                 |
| Transfer-Encoding  | Not supported - rejected at dispatch                                 | §3.3.1        | 501                                 |
| HTTP version       | FNV-1a hash match; sets [`HttpReq::version`](@ref HttpReq::version)  | §2.6          | [`HTTP_UNKNOWN`](@ref HTTP_UNKNOWN) |

</details>

Additional behaviors:

- CR mid header field-name → 400
- Leading SP/HTAB in header values stripped per OWS rules (§3.2.3)
- Excess headers beyond [`MAX_HEADERS`](@ref MAX_HEADERS) are consumed and discarded, not rejected
- Query string overflow silently truncates (capacity limit, not a protocol error)
- Host enforcement is governed by [`PC_ENFORCE_HOST_HEADER`](@ref PC_ENFORCE_HOST_HEADER) (default `1`); set to
  `0` to accept HTTP/1.1 requests without a Host header. The "more than one Host"
  and Content-Length rules are always active. `Host` detection is independent of
  the `MAX_HEADERS` storage cap.

## HTTP/1.1 response generation (RFC 7230 §3.3, §4.1)

**HTTP keep-alive** ([`PC_ENABLE_KEEPALIVE`](@ref PC_ENABLE_KEEPALIVE), default
on) answers a cleanly-parsed request with `Connection: keep-alive` and reuses the
connection for the next request (persistent connections, RFC 7230
§6.3): HTTP/1.1 persists unless the client sends `Connection: close`, HTTP/1.0
closes unless it sends `Connection: keep-alive`, and any error or non-complete
parse (400/413/414) always closes since the next request boundary is unknown.
Each connection serves at most [`PC_KEEPALIVE_MAX_REQUESTS`](@ref PC_KEEPALIVE_MAX_REQUESTS)
requests (a fairness bound) and idle ones are still reclaimed by the timeout
sweep; pipelined requests already in the buffer are drained in order.

<details>
<summary><b>Response Conformance Table</b></summary>

| Behavior                                  | RFC reference | Notes                                                                                                                                                                                       |
| ----------------------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Content-Length` on fixed-size responses  | 7230 §3.3.2   | [`send()`](@ref PC::send) / [`send_empty()`](@ref PC::send_empty) / [`redirect()`](@ref PC::redirect) / [`send_template()`](@ref PC::send_template) |
| Chunked transfer-encoding (streamed body) | 7230 §4.1     | [`send_chunked()`](@ref PC::send_chunked): `Transfer-Encoding: chunked`, `<hexlen>\r\n<data>\r\n` chunks, `0\r\n\r\n` terminator                                                  |
| `HEAD` suppresses body, keeps headers     | 7231 §4.3.2   | applies to chunked too (the `Transfer-Encoding` header is sent, but no chunks)                                                                                                              |

</details>

Note the deliberate asymmetry: an inbound **request** carrying `Transfer-Encoding`
is **rejected** (501, §3.3.1 - the server does not de-chunk request bodies),
whereas an outbound **response** may use chunked transfer via
[`send_chunked()`](@ref PC::send_chunked).

## HTTP authentication (RFC 7235)

A route registered with credentials challenges unauthenticated requests with
`401 Unauthorized` + `WWW-Authenticate` and `Connection: close` (so a client gets
exactly one guess per TCP connection - a built-in brute-force bound).

<details>
<summary><b>Authentication Conformance Table</b></summary>

| Scheme | RFC  | Challenge / verification                                                                                                                                                                                                                 |
| ------ | ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Basic  | 7617 | `WWW-Authenticate: Basic realm="…"`; the base64 `user:pass` is decoded with an output-capacity guard before comparison.                                                                                                                  |
| Digest | 7616 | `Digest` with `algorithm=SHA-256`, `qop="auth"`. `HA1=SHA256(user:realm:pass)`, `HA2=SHA256(method:uri)`, `response=SHA256(HA1:nonce:nc:cnonce:qop:HA2)`. The server nonce is regenerated each `begin()` from the ESP32 hardware CSPRNG. |

</details>

Digest nonce-count (`nc`) replay tracking is not implemented (it needs per-client
state that conflicts with the single shared server nonce on a 1-2 client device);
the per-`begin()` nonce rotation bounds the replay window.

## WebSocket framing (RFC 6455)

<details>
<summary><b>WebSocket Framing Conformance Table</b></summary>

| Rule                                            | Section | Behavior                                    |
| ----------------------------------------------- | ------- | ------------------------------------------- |
| Client→server frames must be masked             | §5.1    | Unmasked frame → Close 1002, fail           |
| Reserved opcodes rejected                       | §5.2    | Opcode ∉ {0,1,2,8,9,A} → Close 1002         |
| RSV2/RSV3 must be zero                          | §5.2    | RSV2/RSV3 set → Close 1002                  |
| RSV1 reserved unless deflate negotiated         | §5.2    | RSV1 set without permessage-deflate → 1002  |
| Control frames ≤ 125 bytes                      | §5.5    | Oversized control frame → Close 1002        |
| Control frames must not be fragmented           | §5.5    | Control frame with FIN=0 → Close 1002       |
| Payload ≤ [`WS_FRAME_SIZE`](@ref WS_FRAME_SIZE) | §5.2    | Oversized / 64-bit length → Close 1009      |
| Handshake version negotiation                   | §4.2.1  | Missing/≠ `13` → 426 with supported version |

</details>

Fragmented data messages (continuation frames, §5.4) are reassembled into the
per-connection buffer and delivered once the FIN frame arrives; control frames
may be interleaved between fragments. The reassembled message must fit in
`WS_FRAME_SIZE` (else Close 1009).

### permessage-deflate (RFC 7692)

Optional inbound message compression ([`PC_ENABLE_WS_DEFLATE`](@ref PC_ENABLE_WS_DEFLATE),
default off). When the client offers `permessage-deflate`, the server accepts it
with `client_no_context_takeover; server_no_context_takeover`, so every message
decompresses independently. A compressed message (RSV1 set on its first frame) is
INFLATEd before delivery via a bounded RFC 1951 decompressor whose tables come
from the shared per-dispatch scratch arena; the 0x00 0x00 0xff 0xff marker
(§7.2.2) is appended internally. Both the compressed input and the decompressed
output must fit `WS_FRAME_SIZE` (else Close 1009); a malformed stream closes 1002.
Outbound frames are sent uncompressed (§6 permits this).

## Transport security - TLS (RFC 5246 / 8446)

Optional HTTPS ([`PC_ENABLE_TLS`](@ref PC_ENABLE_TLS), default off) via
mbedTLS on a static memory pool (no heap). TLS 1.2 (RFC 5246) is the negotiated
minimum; TLS 1.3 (RFC 8446) is used when the client offers it. The verified
cipher suite is `ECDHE-ECDSA-AES256-GCM-SHA384` (forward-secret ECDHE, ECDSA
authentication, AEAD AES-256-GCM). Server certificate/key are loaded via
[`begin_tls()`](@ref PC::begin_tls) / [`tls_cert()`](@ref PC::tls_cert).
`wss://` and TLS Server-Sent Events run over the same TLS record layer when TLS is
enabled: the WebSocket upgrade and every subsequent frame/event are encrypted,
transparent to handler code. Optional **mutual TLS**
([`PC_ENABLE_MTLS`](@ref PC_ENABLE_MTLS)) requires and verifies a client
certificate chaining to a configured CA (RFC 5246 §7.4.6 / RFC 8446 §4.4.2) and
exposes the verified peer subject DN to handlers.

Optional **session resumption** ([`PC_ENABLE_TLS_RESUMPTION`](@ref PC_ENABLE_TLS_RESUMPTION))
via session tickets (RFC 5077): the TLS 1.2 server issues an encrypted ticket and
accepts it on reconnect, so a returning client completes an abbreviated handshake
(no certificate or ECDHE key exchange) - much less CPU and latency on a constrained
device. It is stateless: the session is sealed into the client's ticket with a
server-held AES-256-GCM key that rotates on the
[`PC_TLS_TICKET_LIFETIME_S`](@ref PC_TLS_TICKET_LIFETIME_S) schedule, so no
per-session cache grows in the arena. Full properties and caveats:
[SECURITY.md](SECURITY.md) §6.

## SNMP agent (RFC 1157 / 1901 / 3416 / 2578 / 1213)

Optional SNMP agent ([`PC_ENABLE_SNMP`](@ref PC_ENABLE_SNMP), default off)
on UDP port 161 (via the transport-layer UDP service), with a zero-heap ASN.1 BER codec and a
fixed MIB table. Conformance:

- **Message format (RFC 1157 §3 / RFC 1901):** `SNMPv1` (version 0) and
  `SNMPv2c` (version 1) community-based messages: `SEQUENCE { version, community,
  PDU }`. A request whose community matches neither the read-only nor the
  read-write community is silently discarded.
- **SNMPv3 / USM (RFC 3412 / 3414, optional `PC_ENABLE_SNMP_V3`):** the v3
  message format (msgGlobalData + msgSecurityParameters + scopedPDU), engine
  discovery (Report `usmStatsUnknownEngineIDs`), the timeliness window
  (engineBoots / engineTime), authentication `usmHMAC192SHA256` (HMAC-SHA-256,
  RFC 7860) and privacy `usmAesCfb128` (AES-128-CFB, RFC 3826), with key
  localization per RFC 3414 §2.6. The authenticated, decrypted inner PDU is
  dispatched through the same MIB core as v1/v2c.
- **ASN.1 BER (X.690 / RFC 2578 types):** definite-length TLV encoding of
  `INTEGER`, `OCTET STRING`, `NULL`, `OBJECT IDENTIFIER` (first two arcs packed as
  `40·a+b`, sub-identifiers in base-128), `SEQUENCE`, and the SMI application types
  `Counter32` / `Gauge32` / `TimeTicks` / `IpAddress` / `Opaque`. Integers use the
  minimal two's-complement form; unsigned application types add a leading `0x00`
  when the high bit is set.
- **PDU operations (RFC 1157 §4 / RFC 3416 §4):** `GetRequest`, `GetNextRequest`,
  `GetBulkRequest` (v2c), and `SetRequest`, each answered with a `GetResponse`
  echoing the request id. v2c retrieval reports per-varbind exceptions
  (`noSuchObject` / `endOfMibView`); v1 reports `error-status` / `error-index`
  (`noSuchName`). `Set` is authorized by the read-write community and uses
  `error-status` (`noAccess` / `notWritable` / `wrongType`) in both versions.
  `GetBulk` honors `non-repeaters` / `max-repetitions`, clamped so the response
  never exceeds `SNMP_MAX_VARBINDS`; an over-large response degrades to `tooBig`.
- **Notifications (RFC 3416 §4.2.6, optional `PC_ENABLE_SNMP_TRAP`):** outbound
  `SNMPv2-Trap` and `InformRequest` PDUs, each opening with `sysUpTime.0`
  (TimeTicks) and `snmpTrapOID.0` (the trap OID) followed by caller varbinds.
  SNMPv2c uses the community envelope; SNMPv3 traps are USM authPriv, built from
  the agent's engine ID and localized keys (the same message construction as v3
  responses).
- **MIB (RFC 1213):** the standard `system` group (`1.3.6.1.2.1.1`):
  `sysDescr`, `sysObjectID`, `sysUpTime` (`TimeTicks`), `sysContact`, `sysName`,
  `sysLocation`, `sysServices`: plus any application objects registered under a
  private enterprise subtree. `GetNext` / `GetBulk` walk objects in lexicographic
  OID order.

The decode/dispatch/encode core ([`pc_snmp_agent_process()`](@ref pc_snmp_agent_process))
is transport-independent and host-tested; only the UDP socket is ESP32-specific.
SNMP security properties (cleartext community strings, no v1/v2c authentication):
[SECURITY.md](SECURITY.md).

## HTTP Range requests (RFC 7233)

Optional ([`PC_ENABLE_RANGE`](@ref PC_ENABLE_RANGE), requires file serving,
default off). Served files advertise `Accept-Ranges: bytes`. A single-range
`Range: bytes=A-B`, `bytes=A-`, or `bytes=-N` request is answered `206 Partial
Content` with `Content-Range: bytes A-B/size`, seeking the file and streaming only
the requested bytes (resumable downloads, media seeking). An unsatisfiable range
yields `416 Range Not Satisfiable` with `Content-Range: bytes */size`. A
multi-range (comma-separated) request degrades to a full `200` response, which
RFC 7233 §3.1 explicitly permits, as does a malformed or absent `Range`.

## WebDAV (RFC 4918)

Optional ([`PC_ENABLE_WEBDAV`](@ref PC_ENABLE_WEBDAV), requires file serving,
default off). [`dav()`](@ref PC::dav) mounts a filesystem subtree that
answers the WebDAV methods, extending HTTP so a client can manage files:

- **OPTIONS** advertises `DAV: 1, 2` and the supported `Allow` set.
- **PROPFIND** (Depth `0` or `1`) returns `207 Multi-Status` with a `<D:response>`
  per resource carrying `resourcetype` (collection or empty), `getcontentlength`,
  `getcontenttype`, and an RFC 1123 `getlastmodified`. A Depth-1 listing is bounded
  by `PC_WEBDAV_MAX_ENTRIES`; the document is built into a `PC_WEBDAV_BUF_SIZE`
  buffer. The builder + XML escaping are host-tested.
- **GET / HEAD** stream a file (the file-serving path); **PUT** writes the request
  body (`201 Created` / `204 No Content`); **DELETE** removes a file or recursively
  a collection (`204`); **MKCOL** creates a collection; **COPY** (files and
  collections, recursive, honoring `Depth: 0`/`infinity`) and **MOVE** honor
  `Destination` and `Overwrite` (`F` -> `412 Precondition Failed`).
- **LOCK / UNLOCK** are advisory: a synthetic exclusive-write `opaquelocktoken`
  is issued (so clients that require a lock can write) but not enforced.

Out of scope: PROPPATCH (properties are read-only -> `405`), shared/real locking,
and `Depth: infinity` (treated as `1`). PUT buffers the body (bounded by
`BODY_BUF_SIZE`); large uploads need the streaming-body sink. A destination
outside the mount yields `502 Bad Gateway`. Pair a writable share with per-route
auth, HTTPS, and the accept throttles.

## Modbus TCP slave (Modbus Application Protocol v1.1b3)

Optional ([`PC_ENABLE_MODBUS`](@ref PC_ENABLE_MODBUS), default off) Modbus
TCP server on port 502. The MBAP header (Transaction Id, Protocol Id = 0, Length,
Unit Id) is validated and echoed; the PDU is dispatched against a fixed BSS data
model of coils, discrete inputs, holding registers, and input registers. Supported
function codes: 0x01 Read Coils, 0x02 Read Discrete Inputs, 0x03 Read Holding
Registers, 0x04 Read Input Registers, 0x05 Write Single Coil, 0x06 Write Single
Register, 0x0F Write Multiple Coils, 0x10 Write Multiple Registers. An unsupported
function returns exception 0x01 (Illegal Function), an out-of-range address 0x02
(Illegal Data Address), and a bad quantity/value 0x03 (Illegal Data Value). The
codec ([`pc_modbus_process_adu()`](@ref pc_modbus_process_adu)) is transport-independent
and host-tested; the TCP framing (one ADU per MBAP length, pipelined out of the rx
ring) is dispatched through the PROTO_MODBUS connection handler. The application
reads and writes the model with the accessor functions and is notified of client
writes via [`pc_modbus_on_write()`](@ref pc_modbus_on_write). Modbus has no
authentication or encryption - run it only on a trusted control network.

## CoAP server (RFC 7252)

Optional ([`PC_ENABLE_COAP`](@ref PC_ENABLE_COAP), default off) zero-heap
Constrained Application Protocol endpoint on UDP/5683. Message layer: a
Confirmable (CON) request is answered with a piggybacked ACK, a Non-confirmable
(NON) request with a NON response, and a malformed or empty CON with a Reset
(RST). The parser handles the 4-byte header + token (≤ 8 bytes), delta-encoded
options (Uri-Path, Uri-Query, Content-Format), and the `0xFF` payload marker, then
dispatches GET/POST/PUT/DELETE against a fixed resource table by reconstructed
Uri-Path. The codec ([`pc_coap_server_process()`](@ref pc_coap_server_process)) is
transport-independent and host-tested; only the UDP socket is ESP32-specific.

**Resource discovery (RFC 6690):** a GET to `/.well-known/core` returns the
registered resources in CoRE Link Format (`application/link-format`, Content-Format
40), e.g. `</hello>,</time>` - paged with Block2 when large; a non-GET to it is
`4.05 Method Not Allowed`.

**Resource observation (RFC 7641, optional [`PC_ENABLE_COAP_OBSERVE`](@ref PC_ENABLE_COAP_OBSERVE)):**
a GET carrying the Observe option (value 0) registers the client as an observer;
the registration response and every subsequent notification carry the Observe
option with a monotonically increasing sequence. [`pc_coap_notify()`](@ref pc_coap_notify)
re-renders the resource and pushes a NON notification to each observer from the
server's port (so the client matches it by token + endpoint). An observer is
removed by a GET with Observe (1), a Reset, or a failed send.

**Block-wise transfer (RFC 7959, optional [`PC_ENABLE_COAP_BLOCK`](@ref PC_ENABLE_COAP_BLOCK)):**
the Block2 (descriptive) and Block1 (control) options carry transfers larger than
one datagram. For a response, a representation bigger than the server's maximum
block size, or any request bearing a Block2 option, is served one block at a time:
the server re-renders the (idempotent) resource and slices out the requested block
number, clamping an over-large client block size to its own maximum and setting the
More bit until the last block. For a request, a chunked POST/PUT payload is
reassembled into a single buffer - each non-final block is acknowledged `2.31
Continue`, and the final block dispatches the handler with the whole payload. One
transfer is reassembled at a time; an out-of-order block yields `4.08 Request Entity
Incomplete` and an oversized one `4.13 Request Entity Too Large`. Size1/Size2 are
not emitted.

## OPC UA Binary server (IEC 62541 / OPC UA Part 4 + Part 6)

Optional ([`PC_ENABLE_OPCUA`](@ref PC_ENABLE_OPCUA), default off) zero-heap OPC
UA Binary server on TCP/4840 (`listen(4840, PROTO_OPCUA)`). Transport is UA-TCP /
UA-SecureConversation (Part 6): every message is `MessageType + chunk + MessageSize`,
and a secure message carries `SecureChannelId + SymmetricSecurityHeader(TokenId) +
SequenceHeader(SequenceNumber, RequestId)` before the body (the SecureChannelId field
is mandatory - omitting it is rejected by compliant clients). Supported flow:

- **Discovery / transport:** `HEL`/`ACK` handshake (buffer-size negotiation), `OPN`
  OpenSecureChannel (the server assigns a SecureChannelId + security token), and
  `GetEndpoints` (advertises one endpoint).
- **Session (Part 4 §5.6):** `CreateSession` (assigns a SessionId + AuthenticationToken,
  returns ServerEndpoints) and `ActivateSession`.
- **Attribute / view services:** `Read` (Part 4 §5.10) and `Write` decode/encode scalar
  `Variant`/`DataValue` (Boolean, Int32, UInt32, Float, Double, String); `Browse` (Part 4
  §5.8.2) returns `ReferenceDescription`s. The application supplies the address space via
  registered resolvers ([`pc_opcua_set_read_handler`](@ref pc_opcua_set_read_handler) /
  [`pc_opcua_set_write_handler`](@ref pc_opcua_set_write_handler) /
  [`pc_opcua_set_browse_handler`](@ref pc_opcua_set_browse_handler)); an unknown NodeId yields
  `BadNodeIdUnknown`.
- **Teardown / errors:** `CloseSession`, `CLO` CloseSecureChannel (closes the socket),
  and a `ServiceFault` (`BadServiceUnsupported`) for any unsupported service so a client
  never stalls.

The built-in-type codec, framing, and all service request/response builders are
transport-independent and host-tested; only [`pc_opcua_rx()`](@ref pc_opcua_rx) (the
PROTO_OPCUA data handler) is ESP32-specific. Interoperability is verified against a
third-party client (Python `asyncua`): connect (handshake → secure channel → session →
activate), browse the Objects folder, and read/write live values. An optional matching
**client** ([`PC_ENABLE_OPCUA_CLIENT`](@ref PC_ENABLE_OPCUA_CLIENT),
`services/fieldbus/opcua_client`) builds the requests and parses the responses, transport-agnostic.

**Security properties:** SecurityPolicy is `None` - messages are neither signed nor
encrypted and the user identity is Anonymous, so (like cleartext SNMP v1/v2c) the OPC UA
endpoint must run only on a trusted network or behind a separate transport-security
layer. Sign / SignAndEncrypt policies are not implemented.

## Remote logging - syslog (RFC 5424)

Optional ([`PC_ENABLE_SYSLOG`](@ref PC_ENABLE_SYSLOG), default off) syslog
client over UDP. Each line is `<PRI>1 TIMESTAMP HOSTNAME APP-NAME PROCID MSGID
STRUCTURED-DATA MSG`, where `PRI = facility*8 + severity` and TIMESTAMP / PROCID /
MSGID / STRUCTURED-DATA are the NILVALUE `-` (the device has no wall clock or PID).
Fire-and-forget; the formatter ([`pc_syslog_format()`](@ref pc_syslog_format)) is
host-tested and the datagram is sent via the transport-layer UDP service.

## JWT bearer tokens (RFC 7519 / 7515 / 7518)

Optional ([`PC_ENABLE_JWT`](@ref PC_ENABLE_JWT), default off). Verifies a
compact-serialization JWS (RFC 7515) JSON Web Token (RFC 7519):
`base64url(header).base64url(payload).base64url(signature)` with `alg=HS256`
(HMAC-SHA-256, RFC 7518). The signature is recomputed over `header.payload` and
compared in constant time ([`pc_jwt_bearer_valid()`](@ref pc_jwt_bearer_valid)); integer
claims such as `exp` are readable via [`pc_jwt_claim_int()`](@ref pc_jwt_claim_int) for
the handler to enforce. The full `Authorization` header is captured (a bearer
token exceeds the normal header-value cap). Shared-secret caveat:
[SECURITY.md](SECURITY.md).

## MQTT 3.1.1 client (OASIS)

Optional ([`PC_ENABLE_MQTT`](@ref PC_ENABLE_MQTT), default off) persistent
publish/subscribe client. Conformance to the OASIS MQTT 3.1.1 specification:

- **Packet framing (§2):** the 2-bit-flags + Remaining-Length variable-length
  integer header, length-prefixed UTF-8 strings, and all control packets:
  CONNECT/CONNACK, PUBLISH, PUBACK/PUBREC/PUBREL/PUBCOMP, SUBSCRIBE/SUBACK,
  UNSUBSCRIBE/UNSUBACK, PINGREQ/PINGRESP, DISCONNECT.
- **CONNECT (§3.1):** protocol level 4, Clean Session, keep-alive, optional
  username/password, and an optional Last-Will (topic / message / QoS / retain).
- **Quality of Service (§4.3):** QoS 0 (at most once), QoS 1 (PUBLISH/PUBACK,
  outbound DUP retransmit until acknowledged), and QoS 2 (the four-packet
  PUBLISH/PUBREC/PUBREL/PUBCOMP exchange both directions, with inbound de-dup by
  packet id). Outbound QoS 1/2 messages are held in a bounded in-flight pool.
- **Keep-alive (§3.1.2.10):** a PINGREQ is sent when the link is idle; the
  connection is dropped if no PINGRESP returns within the keep-alive window.
- `mqtts://` runs over a persistent client-side TLS session
  ([`PC_ENABLE_MQTT_TLS`](@ref PC_ENABLE_MQTT_TLS)) with the same optional
  CA / pin verification as the HTTP client. QoS 2 inbound flow uses method A
  (deliver on PUBLISH, de-dup by id until PUBREL). The packet codec is
  transport-independent and host-tested (env:native_mqtt).

## WebSocket client (RFC 6455)

Optional ([`PC_ENABLE_WS_CLIENT`](@ref PC_ENABLE_WS_CLIENT), default off)
outbound client - the device as a WebSocket client to a remote endpoint:

- **Opening handshake (§4.1-4.2):** sends an HTTP/1.1 Upgrade GET with a random
  `Sec-WebSocket-Key` and `Sec-WebSocket-Version: 13`, and verifies the `101`
  response's `Sec-WebSocket-Accept` = base64(SHA-1(key + GUID)).
- **Framing (§5):** client-to-server frames are always masked with a fresh random
  key (§5.3); server frames are read unmasked. Text/binary/ping/pong/close
  opcodes, 7/16/64-bit payload lengths, and continuation-frame reassembly into one
  delivered message. A Ping is answered with a Pong; a Close is echoed.
- `wss://` runs over the shared persistent client TLS session
  ([`PC_ENABLE_WS_CLIENT_TLS`](@ref PC_ENABLE_WS_CLIENT_TLS)) with the same
  CA / pin verification. The handshake/frame codec is host-tested
  (env:native_ws_client), including the RFC 6455 §4.2.2 accept example.

## Outbound HTTP(S) client (RFC 7230)

Optional ([`PC_ENABLE_HTTP_CLIENT`](@ref PC_ENABLE_HTTP_CLIENT), default
off). Builds RFC 7230 request messages ([`http_get()`](@ref http_get) /
[`http_post()`](@ref http_post)) and parses responses framed by `Content-Length`
or `Transfer-Encoding: chunked` (decoded in place) or by connection close.
`https://` runs over client-side mbedTLS
([`PC_ENABLE_HTTP_CLIENT_TLS`](@ref PC_ENABLE_HTTP_CLIENT_TLS)); encrypt-only
by default, with optional server authentication via a CA trust anchor
([`http_client_set_ca()`](@ref http_client_set_ca)) or a SHA-256 certificate pin
([`http_client_set_pin()`](@ref http_client_set_pin)). See [SECURITY.md](SECURITY.md).

## Automatic error responses

[`handle()`](@ref PC::handle) sends these before dispatching to any route handler:

<details>
<summary><b>Parser State Errors Table</b></summary>

| Parser state                                            | Response              | Trigger                                            |
| ------------------------------------------------------- | --------------------- | -------------------------------------------------- |
| [`PARSE_ERROR`](@ref PARSE_ERROR)                       | 400 Bad Request       | Any RFC 7230 character violation or malformed CRLF |
| [`PARSE_ENTITY_TOO_LARGE`](@ref PARSE_ENTITY_TOO_LARGE) | 413 Payload Too Large | `Content-Length` > `BODY_BUF_SIZE`                 |
| [`PARSE_URI_TOO_LONG`](@ref PARSE_URI_TOO_LONG)         | 414 URI Too Long      | Path exceeds `MAX_PATH_LEN − 1` bytes              |

</details>

`handle()` also sends these during dispatch:

<details>
<summary><b>Dispatch Condition Errors Table</b></summary>

| Condition                                    | Response                                     | RFC reference |
| -------------------------------------------- | -------------------------------------------- | ------------- |
| `Transfer-Encoding` header present           | 501 Not Implemented                          | 7230 §3.3.1   |
| Unrecognized request method                  | 501 Not Implemented                          | 7231 §6.5.2   |
| Path matches a route but the method does not | 405 Method Not Allowed (with `Allow` header) | 7231 §6.5.5   |
| No matching route, no `on_not_found` handler | 404 Not Found                                | 7231 §6.5.4   |
| WebSocket upgrade on a non-WS route          | 400 Bad Request                              | 6455 §4.2.1   |
| Unsupported `Sec-WebSocket-Version`          | 426 Upgrade Required                         | 6455 §4.2.1   |
| WebSocket or SSE pool full                   | 503 Service Unavailable                      | -             |

</details>

A `HEAD` request is served by the matching `GET` route with the body suppressed
(RFC 7231 §4.3.2); `GET` routes advertise `HEAD` in the `Allow` header.
