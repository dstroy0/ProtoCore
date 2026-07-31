# Standards & specifications

Every standard this library implements or relies on, with a link to the
authoritative text. This is the conformance map: when changing a subsystem, read
its standard first (the full spec text is also kept locally while work is in
progress). RFC links go to the RFC Editor; others to the issuing body.

> The per-standard **conformance audit** - the MUST/SHOULD verdict and the evidence backing each entry
> below - lives in **[AUDIT.md](AUDIT.md)**.

Status legend: **impl** = implemented in the library - **via mbedTLS** = provided by
the platform crypto/TLS stack the library binds to - **roadmap** = planned (see
[ROADMAP.md](ROADMAP.md)) - **ref** = referenced for correctness but obsoleted by a
newer entry here.

## HTTP core

- [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110) - HTTP Semantics - **impl** (methods, status, headers, conditional requests, ranges).
- [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112) - HTTP/1.1 Messaging - **impl** (request/response framing, Content-Length, Transfer-Encoding rejection).
- [RFC 9111](https://www.rfc-editor.org/rfc/rfc9111) - HTTP Caching - **impl** (ETag / Last-Modified / conditional GET; the structured `Cache-Control` builder/parser + freshness-lifetime precedence in services/web/httpcache).
- [RFC 8246](https://www.rfc-editor.org/rfc/rfc8246) - HTTP Immutable Responses - **impl** (the `immutable` Cache-Control directive).
- [RFC 5861](https://www.rfc-editor.org/rfc/rfc5861) - HTTP Cache-Control Extensions for Stale Content - **impl** (`stale-while-revalidate` / `stale-if-error`).
- [RFC 3986](https://www.rfc-editor.org/rfc/rfc3986) - URI Generic Syntax - **impl** (path / query parsing, percent-decoding).
- [RFC 5234](https://www.rfc-editor.org/rfc/rfc5234) - ABNF - **ref** (grammar notation used by the HTTP specs).
- [RFC 1123](https://www.rfc-editor.org/rfc/rfc1123) - Host Requirements - **impl** (the IMF-fixdate / HTTP-date format for Last-Modified / If-Modified-Since).
- [RFC 7230](https://www.rfc-editor.org/rfc/rfc7230) / [7231](https://www.rfc-editor.org/rfc/rfc7231) / [7233](https://www.rfc-editor.org/rfc/rfc7233) - HTTP/1.1 (messaging / semantics / range) - **ref** (obsoleted by 9110 / 9112; cited where the code predates the renumbering).
- [RFC 6265](https://www.rfc-editor.org/rfc/rfc6265) - HTTP State Management (Cookies) - **impl** (cookie get/set).
- [RFC 7239](https://www.rfc-editor.org/rfc/rfc7239) - Forwarded HTTP Extension - **impl** (client ip/scheme parsing).

## HTTP/2 & HTTP/3

- [RFC 9113](https://www.rfc-editor.org/rfc/rfc9113) - HTTP/2 - **impl** (framing, stream multiplexing, `h2` ALPN; PSRAM-gated).
- [RFC 7541](https://www.rfc-editor.org/rfc/rfc7541) - HPACK (HTTP/2 header compression) - **impl** (static + dynamic table, canonical Huffman).
- [RFC 9204](https://www.rfc-editor.org/rfc/rfc9204) - QPACK (HTTP/3 header compression) - **impl** (static-table field-section codec).
- [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000) - QUIC transport - **impl** (varint / packet / frame codecs, the transport-parameters codec sec 18, and the stateful v1 server connection engine: per-level AEAD, CRYPTO / STREAM reassembly, ACKs, coalescing; loss recovery + congestion control are **roadmap**).
- [RFC 9001](https://www.rfc-editor.org/rfc/rfc9001) - Using TLS to Secure QUIC - **impl** (Initial / Handshake / 1-RTT packet protection, header protection, Retry integrity tag, and the hand-rolled TLS 1.3 handshake carried in CRYPTO frames).
- [RFC 8446](https://www.rfc-editor.org/rfc/rfc8446) - TLS 1.3 - **impl for QUIC** (a from-scratch server handshake for HTTP/3: TLS_AES_128_GCM_SHA256 + X25519 + Ed25519, the sec 7.1 key schedule + sec 4 messages, pinned to the RFC 8448 traces; mbedTLS has no QUIC-TLS API). TLS over TCP remains **via mbedTLS**.
- [RFC 9147](https://www.rfc-editor.org/rfc/rfc9147) - DTLS 1.3 - **impl (record layer + server handshake)** (`PC_ENABLE_DTLS`: the sec 4 DTLSPlaintext + DTLSCiphertext record layer - unified header, AEAD_AES_128_GCM, sec 4.2.3 sequence-number encryption + sec 4.2.2 reconstruction, sec 4.5.1 anti-replay; the sec 5 + 7 handshake framing / reliability - the 12-byte handshake header, overlap-tolerant reassembly, the ACK message, and the stateless HelloRetryRequest cookie; and the sec 5-6 **server handshake state machine** - the one-round-trip full handshake, TLS_AES_128_GCM_SHA256 / X25519 / Ed25519, epoch 0→2→3 transitions - reusing the TLS 1.3 messages + key schedule, with the sec 5.9 `dtls13` HKDF label prefix. **Interoperates with two independent reference stacks - the wolfSSL and the CycloneSSL DTLS 1.3 clients** - full handshake + application-data round trip, both offering X25519 directly and (wolfSSL) through a sec 5.1 **HelloRetryRequest** group renegotiation with an address-bound cookie (`test/servers/pc_dtls_wolfssl`, `test/servers/cyclone_dtls`); lost flights are recovered by a sec 5.8 retransmission timer with exponential backoff, cancelled by inbound ACKs. A CoAP-over-DTLS front-end is **roadmap**.
- [RFC 7250](https://www.rfc-editor.org/rfc/rfc7250) - Raw Public Keys in TLS/DTLS - **impl (server-side)** (`PC_ENABLE_TLS_RPK`: the DTLS 1.3 server negotiates the `server_certificate_type` extension (IANA 20) and, when the client offers RawPublicKey(2), presents its Ed25519 `SubjectPublicKeyInfo` as the Certificate instead of an X.509 chain - the same key signs CertificateVerify. Additive; the shared TLS 1.3 codec carries it for a future HTTP/3 use. **Interoperates with both the wolfSSL DTLS 1.3 client in `--rpk` (RawPublicKey-only) mode and the CycloneSSL DTLS 1.3 client (RFC 7250 RPK verify callback).**)
- [RFC 8410](https://www.rfc-editor.org/rfc/rfc8410) - Algorithm Identifiers for Ed25519 in X.509/SPKI - **impl** (the 44-byte Ed25519 `SubjectPublicKeyInfo` DER used by the RFC 7250 RawPublicKey path).
- [RFC 8448](https://www.rfc-editor.org/rfc/rfc8448) - Example TLS 1.3 Handshake Traces - **test vectors** (the QUIC TLS 1.3 key schedule + ServerHello / Certificate / Finished bytes are pinned to sec 3).
- [RFC 9114](https://www.rfc-editor.org/rfc/rfc9114) - HTTP/3 - **impl** (application engine over QUIC streams: control + QPACK streams, SETTINGS, request stream -> response; end-to-end host-tested and hardware-validated).

## HTTP authentication & authorization

- [RFC 7617](https://www.rfc-editor.org/rfc/rfc7617) - HTTP Basic auth - **impl**.
- [RFC 7616](https://www.rfc-editor.org/rfc/rfc7616) - HTTP Digest auth (SHA-256, qop=auth) - **impl**.
- [RFC 7519](https://www.rfc-editor.org/rfc/rfc7519) - JSON Web Token (JWT) - **impl** (HS256 verify + claims).
- [RFC 7515](https://www.rfc-editor.org/rfc/rfc7515) - JSON Web Signature (JWS) - **impl** (JWT/OIDC signature structure).
- [RFC 7518](https://www.rfc-editor.org/rfc/rfc7518) - JSON Web Algorithms (JWA) - **impl** (HS256 / RS256).
- [RFC 6749](https://www.rfc-editor.org/rfc/rfc6749) - OAuth 2.0 - **impl** (token-endpoint client).
- [RFC 7636](https://www.rfc-editor.org/rfc/rfc7636) - PKCE - **impl** (OAuth2 code-exchange hardening).
- [OpenID Connect Core 1.0](https://openid.net/specs/openid-connect-core-1_0.html) - **impl** (OIDC ID-token RS256 verification).

## Content types & serialization

- [RFC 4648](https://www.rfc-editor.org/rfc/rfc4648) - Base16 / Base32 / Base64 (and base64url) - **impl**.
- [RFC 7578](https://www.rfc-editor.org/rfc/rfc7578) - multipart/form-data - **impl** (upload parser).
- [RFC 2046](https://www.rfc-editor.org/rfc/rfc2046) - MIME Part 2 (Media Types) - **ref** (multipart boundary semantics).
- [RFC 1951](https://www.rfc-editor.org/rfc/rfc1951) - DEFLATE - **impl** (inflate / deflate codecs; WS permessage-deflate).
- [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949) - CBOR - **impl** (encoder + decoder).
- [MessagePack spec](https://github.com/msgpack/msgpack/blob/master/spec.md) - **impl** (encoder + decoder).
- [GraphQL spec](https://spec.graphql.org/) - **impl** (bounded query subset).
- [WHATWG HTML - Server-Sent Events](https://html.spec.whatwg.org/multipage/server-sent-events.html) - **impl** (SSE / EventSource stream format).

## WebSocket

- [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455) - The WebSocket Protocol - **impl**.
- [RFC 7692](https://www.rfc-editor.org/rfc/rfc7692) - permessage-deflate extension - **impl**.

## IoT / industrial messaging

- [RFC 7252](https://www.rfc-editor.org/rfc/rfc7252) - CoAP - **impl**.
- [RFC 7959](https://www.rfc-editor.org/rfc/rfc7959) - CoAP Block-Wise Transfers - **impl**.
- [RFC 7641](https://www.rfc-editor.org/rfc/rfc7641) - CoAP Observe - **impl** (`PC_ENABLE_COAP_OBSERVE`).
- [OASIS MQTT 3.1.1](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html) / [MQTT 5.0](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html) - **impl** (client).
- [Modbus Application Protocol v1.1b3](https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf) / [Modbus Messaging over TCP/IP](https://www.modbus.org/docs/Modbus_Messaging_Implementation_Guide_V1_0b.pdf) - **impl** (TCP slave + master).
- [OPC UA (IEC 62541) - reference](https://reference.opcfoundation.org/) - **impl** (Binary codec + UACP, increment 1).

## Network management (SNMP)

- [RFC 1157](https://www.rfc-editor.org/rfc/rfc1157) - SNMPv1 - **impl**.
- [RFC 3411](https://www.rfc-editor.org/rfc/rfc3411) - SNMP architecture - **impl** (v3).
- [RFC 3412](https://www.rfc-editor.org/rfc/rfc3412) - SNMP message processing - **impl** (v3).
- [RFC 3414](https://www.rfc-editor.org/rfc/rfc3414) - User-based Security Model (USM) - **impl** (v3 authPriv).
- [RFC 3416](https://www.rfc-editor.org/rfc/rfc3416) - SNMPv2 PDU operations - **impl** (v2c / v3).
- [RFC 3826](https://www.rfc-editor.org/rfc/rfc3826) - AES Cipher in the USM - **impl** (v3 priv).
- [RFC 7860](https://www.rfc-editor.org/rfc/rfc7860) - HMAC-SHA-2 auth in the USM - **impl** (v3 auth).
- [RFC 2578](https://www.rfc-editor.org/rfc/rfc2578) - SMIv2 - **ref** (MIB / OID structure).
- [ITU-T X.690](https://www.itu.int/rec/T-REC-X.690) - ASN.1 BER/DER - **impl** (the SNMP / OIDC BER codec).

## SSH & Telnet

- [RFC 4251](https://www.rfc-editor.org/rfc/rfc4251) - SSH architecture - **impl**.
- [RFC 4252](https://www.rfc-editor.org/rfc/rfc4252) - SSH authentication - **impl**.
- [RFC 4253](https://www.rfc-editor.org/rfc/rfc4253) - SSH transport layer - **impl** (algorithm negotiation follows §7.1 **client preference**; **interoperates with two independent SSH stacks - OpenSSH and Oryx CycloneSSH** - both completing a full KEX + auth + channel exchange with our server, `test/servers/peers/ssh_peer.py` + `test/servers/cyclone_ssh`. The CycloneSSH peer caught a server-preference negotiation bug OpenSSH's matching defaults hid; see [BUGS.md](BUGS.md)).
- [RFC 4254](https://www.rfc-editor.org/rfc/rfc4254) - SSH connection protocol - **impl**.
- [RFC 4250](https://www.rfc-editor.org/rfc/rfc4250) - SSH assigned numbers - **impl**.
- [RFC 4344](https://www.rfc-editor.org/rfc/rfc4344) - SSH transport encryption modes (CTR) - **impl**.
- [RFC 5647](https://www.rfc-editor.org/rfc/rfc5647) - AES-GCM for SSH (aes256-gcm@openssh.com) - **impl**.
- [RFC 6668](https://www.rfc-editor.org/rfc/rfc6668) - SHA-2 data-integrity (HMAC) for SSH - **impl**.
- [RFC 8268](https://www.rfc-editor.org/rfc/rfc8268) - More MODP DH groups for SSH - **impl**.
- [RFC 8332](https://www.rfc-editor.org/rfc/rfc8332) - RSA SHA-2 (rsa-sha2-256/512) for SSH - **impl**.
- [RFC 5656](https://www.rfc-editor.org/rfc/rfc5656) - ECC algorithms for SSH (ecdsa-sha2-nistp256 host key + ecdh-sha2-nistp256 KEX) - **impl**.
- [RFC 6979](https://www.rfc-editor.org/rfc/rfc6979) - Deterministic ECDSA (native ECDSA nonce; KAT source) - **impl**.
- [RFC 5903](https://www.rfc-editor.org/rfc/rfc5903) - ECP groups for IKE/IKEv2 (P-256 ECDH shared-secret KAT source) - **impl**.
- [RFC 3526](https://www.rfc-editor.org/rfc/rfc3526) - MODP Diffie-Hellman groups - **impl** (SSH KEX).
- [RFC 8731](https://www.rfc-editor.org/rfc/rfc8731) - curve25519-sha256 key exchange for SSH - **impl**.
- [RFC 8709](https://www.rfc-editor.org/rfc/rfc8709) - Ed25519/Ed448 public keys for SSH (ssh-ed25519) - **impl**.
- [RFC 8308](https://www.rfc-editor.org/rfc/rfc8308) - SSH extension negotiation (ext-info-c, server-sig-algs) - **impl**.
- [RFC 854](https://www.rfc-editor.org/rfc/rfc854) - Telnet Protocol - **impl**.

## Cryptographic primitives

- [FIPS 180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final) - Secure Hash Standard (SHA-2) - **impl**.
- [RFC 3174](https://www.rfc-editor.org/rfc/rfc3174) - SHA-1 - **impl** (WebSocket handshake only).
- [RFC 1320](https://www.rfc-editor.org/rfc/rfc1320) - MD4 - **impl** (SMB/NTLM only: the NT hash; legacy, KAT-verified).
- [RFC 1321](https://www.rfc-editor.org/rfc/rfc1321) - MD5 - **impl** (SMB/NTLM only: HMAC-MD5 base; legacy, KAT-verified).
- [FIPS 197](https://csrc.nist.gov/pubs/fips/197/final) - AES - **impl** (SNMP priv; QUIC AES-128 block + header protection) / **via mbedTLS** (TLS; QUIC AES block on ESP32).
- [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final) - GCM (Galois/Counter Mode) - **impl** (QUIC packet protection AEAD_AES_128_GCM; software GHASH).
- [FIPS 198-1](https://csrc.nist.gov/pubs/fips/198-1/final) / [RFC 2104](https://www.rfc-editor.org/rfc/rfc2104) - HMAC - **impl**.
- [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869) - HKDF - **impl** (TLS 1.3 HKDF-Expand-Label; QUIC Initial key schedule).
- [RFC 8017](https://www.rfc-editor.org/rfc/rfc8017) - PKCS#1 (RSA) - **impl** (RS256 verify) / **via mbedTLS**.
- [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) - Elliptic curves for security (X25519) - **impl** (SSH KEX; field inversion on the ESP32 MPI accelerator).
- [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032) - EdDSA (Ed25519) - **impl** (SSH host key + client auth).
- [RFC 5754](https://www.rfc-editor.org/rfc/rfc5754) - SHA-2 algorithm identifiers - **ref** (RS256 DigestInfo).
- [RFC 6238](https://www.rfc-editor.org/rfc/rfc6238) - TOTP - **impl**.
- [RFC 4226](https://www.rfc-editor.org/rfc/rfc4226) - HOTP - **impl** (TOTP base).
- [RFC 4122](https://www.rfc-editor.org/rfc/rfc4122) - UUID - **impl** (device-id).

## Files, logging, naming, transport

- [RFC 4918](https://www.rfc-editor.org/rfc/rfc4918) - WebDAV - **impl**.
- [RFC 959](https://www.rfc-editor.org/rfc/rfc959) - File Transfer Protocol (FTP) - **impl** (client codec: command builders, single/multi-line reply parser, PASV address).
- [RFC 2428](https://www.rfc-editor.org/rfc/rfc2428) - FTP Extensions for IPv6 and NATs (EPSV / EPRT) - **impl** (client codec: EPRT builder, EPSV port parser).
- [MS-SMB2](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2) - SMB2 (Server Message Block 2) - **impl** (full read/write-a-file client wire codec: the sync header, NEGOTIATE, SESSION_SETUP with the SPNEGO tokens §2.2.5/§2.2.6, and TREE_CONNECT / CREATE / CLOSE / READ / WRITE §2.2.9-§2.2.22).
- [MS-NLMP](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-nlmp) - NTLM Authentication - **impl** (the NTLMv2 response: NThash / NTOWFv2 / NTProofStr / SessionBaseKey, §3.3.2, verified vs the §4.2 worked example; plus the NTLMSSP message codec, §2.2.1: NEGOTIATE / CHALLENGE / AUTHENTICATE).
- [RFC 4178](https://www.rfc-editor.org/rfc/rfc4178) - SPNEGO (GSS-API negotiation) - **impl** (SMB/NTLM only: the DER InitialContextToken / NegTokenResp wrapping of the NTLMSSP tokens; verified byte-exact + independently vs `openssl asn1parse`).
- [RFC 5424](https://www.rfc-editor.org/rfc/rfc5424) - Syslog Protocol - **impl**.
- [RFC 1035](https://www.rfc-editor.org/rfc/rfc1035) - Domain Names (DNS) - **impl** (resolver + captive-portal responder).
- [IEEE 802.11](https://standards.ieee.org/ieee/802.11/7028/) - Wireless LAN MAC/PHY - **impl** (Wi-Fi link; raw-L2 frame TX path).
- [RFC 2474](https://www.rfc-editor.org/rfc/rfc2474) - Definition of the Differentiated Services Field (DS Field) - **impl** (`PC_ENABLE_DIFFSERV`: the 6-bit DSCP stamped into the outbound IPv4 TOS / IPv6 Traffic-Class byte, per server-default / listener / connection / UDP datagram; HW-verified on-wire on an ESP32-P4).

## TLS

- [RFC 8446](https://www.rfc-editor.org/rfc/rfc8446) - TLS 1.3 - **via mbedTLS** (server + client); explicit version control is **roadmap**.
- [RFC 5246](https://www.rfc-editor.org/rfc/rfc5246) - TLS 1.2 - **via mbedTLS**; explicit support is **roadmap**.
- [RFC 5077](https://www.rfc-editor.org/rfc/rfc5077) - TLS session resumption (tickets) - **impl** (stateless ticket key-rotation and resumption context).
