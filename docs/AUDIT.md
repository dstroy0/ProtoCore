# Standards conformance audit

A per-standard conformance review of every entry in [STANDARDS.md](STANDARDS.md): the key MUST / SHOULD
requirements checked against the implementation, and the evidence backing the verdict. It is the tracking
artifact the roadmap's "audit the library against its standards" item asks for.

**Methodology.** For each standard: (1) read the authoritative text's conformance requirements, (2) check
the implementation against them, (3) prefer a **real-peer conformance check** where one exists - the
interop harness ([`test/servers/interop.py`](../test/servers/interop.py)) drives the device against the
_reference_ implementation, not just our own round-trip, which is the strongest evidence a wire format is
right. Where a real peer is not wired, the evidence is a host test with pinned spec vectors. Any genuine
gap (breaks a compliant peer or violates a MUST) becomes a bug in [BUGS.md](BUGS.md) + a regression test;
this pass found none open beyond the scope boundaries already tracked in [ROADMAP.md](ROADMAP.md).

**Verdict legend.** ✅ conformant (MUSTs met, evidence cited) - 🔷 conformant within a documented scope
boundary (implemented part meets its MUSTs; the unimplemented part is roadmap, not a violation) -
🔗 delegated to mbedTLS (the platform crypto/TLS stack owns conformance) - 📎 reference-only (notation /
obsoleted, nothing to conform to).

**Evidence sources.** `native_*` = a host Unity test env - `interop:<peer>` = a real reference peer in the
harness - `vec:<spec>` = pinned spec test vectors - `HW` = verified on ESP32 hardware.

---

## HTTP core

| Standard           | Verdict | Key MUSTs checked                                                                                              | Evidence                                            |
| ------------------ | ------- | -------------------------------------------------------------------------------------------------------------- | --------------------------------------------------- |
| RFC 9110 Semantics | ✅      | Method/status registry, conditional requests (`If-*`), `Range`/`206`/`416`, header parsing                     | `native_http_*`, `interop:http` (stdlib client), HW |
| RFC 9112 Messaging | ✅      | Request framing; **reject** `Content-Length`+`Transfer-Encoding` conflict and duplicate/invalid CL (smuggling) | `native` (smuggling cases), HW                      |
| RFC 9111 Caching   | ✅      | `ETag`/`Last-Modified`, `304` on conditional GET, `Cache-Control`                                              | `native_http_delivery`, `native_app`                |
| RFC 3986 URI       | ✅      | Path/query split, percent-decoding, dot-segment handling                                                       | `native`                                            |
| RFC 1123 HTTP-date | ✅      | IMF-fixdate format for `Last-Modified` / `If-Modified-Since` (gmtime_r, no locale)                             | `native_app`                                        |
| RFC 6265 Cookies   | ✅      | Inbound cookie parsing (`http_get_cookie`), outbound `set_cookie` emission                                     | `native`, `native_app`, HW                          |
| RFC 7239 Forwarded | ✅      | Parsing client IP / HTTPS scheme from `Forwarded` / `X-Forwarded-*` headers                                    | `native`                                            |
| RFC 5234 ABNF      | 📎      | Grammar notation only                                                                                          | -                                                   |
| RFC 7230/7231/7233 | 📎      | Obsoleted by 9110/9112; cited where code predates the renumbering                                              | -                                                   |

## HTTP/2 & HTTP/3

| Standard               | Verdict | Key MUSTs checked                                                                                                                                                                                                                                                                                                                                                                                      | Evidence                                                                                                |
| ---------------------- | ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------- |
| RFC 9113 HTTP/2        | ✅      | Frame layout, stream states, flow control, `h2` ALPN; connection preface                                                                                                                                                                                                                                                                                                                               | `native_h2_*` (PSRAM-gated), HW                                                                         |
| RFC 7541 HPACK         | ✅      | Static+dynamic table, integer + canonical Huffman string coding, eviction                                                                                                                                                                                                                                                                                                                              | `native_hpack` (RFC 7541 vectors)                                                                       |
| RFC 9204 QPACK         | ✅      | Static-table field-section encode/decode, prefix                                                                                                                                                                                                                                                                                                                                                       | `native_qpack`                                                                                          |
| RFC 9000 QUIC          | 🔷      | varint/packet/frame codecs, transport params (§18), stateful v1 engine (AEAD per level, CRYPTO/STREAM reassembly, ACK, coalescing). Loss recovery + CC are roadmap                                                                                                                                                                                                                                     | `native_quic_*`, HW byte-exact                                                                          |
| RFC 9001 QUIC-TLS      | ✅      | Initial/Handshake/1-RTT protection, header protection, Retry integrity tag                                                                                                                                                                                                                                                                                                                             | `native_quic_crypto` (vs App A), HW                                                                     |
| RFC 8446 TLS1.3 (QUIC) | 🔷      | From-scratch handshake for HTTP/3 (AES-128-GCM + X25519 + Ed25519, §7.1 schedule, §4 msgs) pinned to RFC 8448                                                                                                                                                                                                                                                                                          | `native_quic_tls` (`vec:RFC 8448`)                                                                      |
| RFC 8448 traces        | ✅      | Key schedule + ServerHello/Certificate/Finished bytes pinned                                                                                                                                                                                                                                                                                                                                           | `vec:RFC 8448`                                                                                          |
| RFC 9114 HTTP/3        | ✅      | Control + QPACK streams, SETTINGS, request→response mapping, UDP/server wiring, and hardware-interop                                                                                                                                                                                                                                                                                                   | `native_h3_*` end-to-end, HW                                                                            |
| RFC 9147 DTLS 1.3      | 🔷      | Record layer (unified header, AEAD-128-GCM, §4.2.3 seq-num encryption + §4.2.2 reconstruction, §4.5.1 anti-replay) + handshake framing (§5.2 header, §5.4 reassembly, §7 ACK, §5.1 cookie) + §5-6 server handshake (full 1-RTT, epoch 0→2→3, §5.9 `dtls13` labels) + §5.1 HRR group renegotiation (address-bound cookie) + §5.8 PTO retransmission (backoff, ACK cancellation). CoAP-over-DTLS roadmap | `native_dtls*`, `native_dtls_conn`, **`interop:wolfSSL` DTLS 1.3 (handshake + app data, direct + HRR)** |

## HTTP authentication & authorization

| Standard        | Verdict | Key MUSTs checked                                      | Evidence                            |
| --------------- | ------- | ------------------------------------------------------ | ----------------------------------- |
| RFC 7617 Basic  | ✅      | `Basic` challenge/credential, base64 userid:password   | `native_app`                        |
| RFC 7616 Digest | ✅      | SHA-256, `qop=auth`, nonce/cnonce/nc, response hash    | `native_app` (RFC vectors)          |
| RFC 7519 JWT    | ✅      | Header/claims decode, `exp`/`nbf`/`iat`, HS256 verify  | `native_jwt`, `native_oidc`         |
| RFC 7515 JWS    | ✅      | Signing-input construction, signature verify structure | `native_jwt`                        |
| RFC 7518 JWA    | ✅      | HS256 / RS256 identifiers + verification               | `native_jwt`, `native_oidc`         |
| RFC 6749 OAuth2 | ✅      | Token-endpoint request/response (client)               | `native_oauth2`                     |
| RFC 7636 PKCE   | ✅      | `code_verifier`/`code_challenge` S256                  | `native_oauth2`                     |
| OIDC Core 1.0   | ✅      | ID-token RS256 verification, claim checks              | `native_oidc` (regenerated vectors) |

## Content types & serialization

| Standard              | Verdict | Key MUSTs checked                                      | Evidence                            |
| --------------------- | ------- | ------------------------------------------------------ | ----------------------------------- |
| RFC 4648 Base16/32/64 | ✅      | Padding, alphabet, base64url; reject non-alphabet      | `native`, `vec:RFC 4648`            |
| RFC 7578 multipart    | ✅      | Boundary handling, part headers, `Content-Disposition` | `native_app`                        |
| RFC 2046 MIME         | 📎      | Multipart boundary semantics (referenced)              | -                                   |
| RFC 1951 DEFLATE      | ✅      | inflate/deflate; used by WS permessage-deflate         | `native_deflate`, `native_ssh_zlib` |
| RFC 8949 CBOR         | ✅      | Major types, canonical lengths, encode+decode          | `native_cbor` (vectors)             |
| MessagePack           | ✅      | Type tags, fixext, encode+decode                       | `native_msgpack`                    |
| GraphQL               | 🔷      | Bounded query subset (documented scope)                | `native_graphql`                    |
| WHATWG SSE            | ✅      | `data:`/`event:`/`id:`/`retry:` field format, framing  | `native`, HW                        |

## WebSocket

| Standard           | Verdict | Key MUSTs checked                                                            | Evidence                                              |
| ------------------ | ------- | ---------------------------------------------------------------------------- | ----------------------------------------------------- |
| RFC 6455           | ✅      | Handshake (SHA-1 accept), frame opcodes, masking, close codes, fragmentation | `native`, `interop:websockets` (`websockets` lib), HW |
| RFC 7692 pmdeflate | ✅      | `permessage-deflate` negotiation, context takeover, per-message flush        | `native`                                              |

## IoT / industrial messaging

| Standard              | Verdict | Key MUSTs checked                                                          | Evidence                                            |
| --------------------- | ------- | -------------------------------------------------------------------------- | --------------------------------------------------- |
| RFC 7252 CoAP         | ✅      | Message format, type/token, option delta encoding, response codes          | `native_coap`, `interop:aiocoap`                    |
| RFC 7959 Block-Wise   | ✅      | Block1/Block2 option, NUM/M/SZX, `2.31 Continue`                           | `native_coap`                                       |
| RFC 7641 CoAP Observe | ✅      | Observe option, monotonic sequence number (the clock bug is fixed)         | `native_coap_observe`                               |
| MQTT 3.1.1 / 5.0      | ✅      | CONNECT/PUBLISH/SUBSCRIBE packets, QoS, v5 properties (client)             | `native_mqtt`, `interop:mosquitto`+`paho`, HW       |
| Modbus v1.1b3 + TCP   | ✅      | MBAP header, FC dispatch, exception codes, register model                  | `native_modbus`, `interop:pymodbus` (client+server) |
| OPC UA (IEC 62541)    | ✅      | Binary built-in types, UACP framing, Hello/Ack, session, Read/Write/Browse | `native_opcua*`, `interop:asyncua`, HW              |

## Network management (SNMP)

| Standard                      | Verdict | Key MUSTs checked                                               | Evidence                                   |
| ----------------------------- | ------- | --------------------------------------------------------------- | ------------------------------------------ |
| RFC 1157 SNMPv1               | ✅      | Message + PDU BER, community, GET/GETNEXT/SET                   | `native_snmp`, `interop:net-snmp`          |
| RFC 3411-3416                 | ✅      | v3 architecture, message processing, USM, v2c/v3 PDU ops        | `native_snmp`, `interop:net-snmp`+`pysnmp` |
| RFC 3414 USM                  | ✅      | authPriv, engine discovery, time window, msgAuthoritativeEngine | `native_snmp`                              |
| RFC 3826 AES / 7860 HMAC-SHA2 | ✅      | CFB-128 priv, HMAC-SHA-256 auth, localized keys                 | `native_snmp` (vectors)                    |
| RFC 2578 SMIv2                | 📎      | MIB/OID structure (referenced)                                  | -                                          |
| ITU-T X.690 BER/DER           | ✅      | TLV, length forms, INTEGER/OID/SEQUENCE encoding                | `native_snmp`, `native_oidc`               |

## SSH & Telnet

| Standard            | Verdict | Key MUSTs checked                                                                           | Evidence                                |
| ------------------- | ------- | ------------------------------------------------------------------------------------------- | --------------------------------------- |
| RFC 4251-4254       | ✅      | Architecture, banner/KEXINIT, binary packet, userauth, channels, global requests            | `native_ssh*`, `interop:OpenSSH`, HW    |
| RFC 4253 transport  | ✅      | Binary packet + MAC, KEX, **per-direction NEWKEYS** (§7.3), **rekeying** (§9, this session) | `native_ssh`, `native_ssh_conn`         |
| RFC 4344 CTR        | ✅      | aes256-ctr counter mode                                                                     | `native_ssh`                            |
| RFC 6668 HMAC-SHA2  | ✅      | hmac-sha2-256/512 (+ ETM variants)                                                          | `native_ssh_hardened`                   |
| RFC 8268 / 3526 DH  | ✅      | group14-sha256 MODP KEX                                                                     | `native_ssh`, `interop:OpenSSH`         |
| RFC 8332 RSA-SHA2   | ✅      | rsa-sha2-256/512 host key signatures                                                        | `native_ssh`, HW                        |
| RFC 8731 curve25519 | ✅      | curve25519-sha256 KEX                                                                       | `native_ssh_ed25519`, `interop:OpenSSH` |
| RFC 8709 Ed25519    | ✅      | ssh-ed25519 host key + auth                                                                 | `native_ssh_ed25519`                    |
| RFC 5656 ECDSA P256 | ✅      | ecdsa-sha2-nistp256 host key + auth (RFC 6979 sign KAT)                                     | `native_ssh_ecdsa`, `native_ssh`        |
| RFC 5656 ECDH P256  | ✅      | ecdh-sha2-nistp256 key exchange, on-curve peer check (RFC 5903 §8.1 shared-secret KAT)      | `native_ssh_ecdsa`, `native_ssh`        |
| RFC 8308 ext-info   | ✅      | ext-info-c, server-sig-algs                                                                 | `native_ssh`                            |
| RFC 854 Telnet      | ✅      | IAC command/option negotiation                                                              | `native_telnet`                         |

## Cryptographic primitives

| Standard                   | Verdict | Key MUSTs checked                                       | Evidence                            |
| -------------------------- | ------- | ------------------------------------------------------- | ----------------------------------- |
| FIPS 180-4 SHA-2           | ✅      | SHA-256/512 digests                                     | `native_ssh` (NIST vectors)         |
| RFC 3174 SHA-1             | ✅      | WebSocket accept-key only                               | `native`                            |
| FIPS 197 AES               | ✅ / 🔗 | AES-128 block + QUIC header protection; TLS via mbedTLS | `native_quic_crypto`, `native_snmp` |
| SP 800-38D GCM             | ✅      | AEAD_AES_128_GCM, software GHASH                        | `native_quic_crypto`                |
| FIPS 198-1 / RFC 2104 HMAC | ✅      | HMAC construction                                       | `native_ssh`                        |
| RFC 5869 HKDF              | ✅      | Extract/Expand, HKDF-Expand-Label                       | `native_quic_tls`, `vec:RFC 8448`   |
| RFC 8017 PKCS#1            | ✅ / 🔗 | RS256 verify (EMSA-PKCS1-v1_5); mbedTLS for TLS         | `native_oidc`, `native_ssh`         |
| RFC 7748 X25519            | ✅      | Curve25519 scalar mult, MPI-accel inversion on ESP32    | `native_ssh_ed25519`, HW            |
| RFC 8032 Ed25519           | ✅      | EdDSA sign/verify                                       | `native_ssh_ed25519`                |
| RFC 5754                   | 📎      | RS256 DigestInfo identifiers (referenced)               | -                                   |
| RFC 6238 TOTP / 4226 HOTP  | ✅      | HMAC-based OTP, time-step                               | `native_totp`                       |
| RFC 4122 UUID              | ✅      | v4 layout, variant/version bits                         | `native_device_id`                  |

## Files, logging, naming, transport

| Standard        | Verdict | Key MUSTs checked                                                                | Evidence                  |
| --------------- | ------- | -------------------------------------------------------------------------------- | ------------------------- |
| RFC 4918 WebDAV | ✅      | PROPFIND/PROPPATCH/MKCOL/COPY/MOVE, `Depth`, multistatus                         | `native_webdav`           |
| RFC 5424 Syslog | ✅      | PRI/version/timestamp/structured-data framing                                    | `native_syslog`           |
| RFC 1035 DNS    | ✅      | Question/answer encoding, name compression, resolver + captive responder         | `native_dns_resolver`, HW |
| IEEE 802.11     | 🔷      | Wi-Fi link (SDK-owned); raw-L2 TX + the 802.11 MAC-header decode for the sniffer | `native_wifi_sniffer`, HW |

## TLS (over TCP)

| Standard         | Verdict | Key MUSTs checked                                                             | Evidence                     |
| ---------------- | ------- | ----------------------------------------------------------------------------- | ---------------------------- |
| RFC 8446 TLS 1.3 | 🔗      | Record/handshake owned by mbedTLS; version + cipher **policy** layered on top | `native_tls_policy`, mbedTLS |
| RFC 5246 TLS 1.2 | 🔗      | mbedTLS; floored at 1.2, negotiated version observable                        | `native_tls_policy`, mbedTLS |
| RFC 5077 tickets | ✅      | Session resumption via mbedTLS with explicit server ticket key rotation       | `native_tls_policy`, HW      |

---

## Findings

No open conformance defects were found in this pass. The library's wire formats for the major protocols
(HTTP, WebSocket, CoAP, MQTT, Modbus, SNMP, OPC UA, SSH) are each verified against a **real reference
implementation** in the interop harness - the definitive conformance check - and the cryptographic
primitives are pinned to their FIPS/RFC test vectors. The two most recent findings that came out of this
kind of review (HTTP request-smuggling framing, and the CoAP-Observe monotonic-clock bug) are fixed and
regression-tested.

Items marked 🔷 are documented **scope boundaries**, not violations: the implemented subset meets its
MUSTs, and the unimplemented remainder (QUIC loss recovery / congestion control, the HTTP/3 UDP server
wiring, the bounded GraphQL subset) is tracked in [ROADMAP.md](ROADMAP.md). Items marked 🔗 delegate
conformance to mbedTLS, the audited platform TLS/crypto stack.

_This audit is re-run when a subsystem changes; keep the verdict + evidence column current as protocols
are added or a spec is revised._
