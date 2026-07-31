# DTLS 1.3 (RFC 9147)

`PC_ENABLE_DTLS`

DTLS is TLS for **datagrams**: it gives UDP the same confidentiality, integrity, and
authentication that TLS gives TCP, without imposing a reliable, ordered byte stream. That
matters for constrained devices, where the natural transport is a single UDP exchange -
**CoAP** sensor reads, telemetry, LwM2M - and a TCP + TLS session would be pure overhead.

DTLS 1.3 (RFC 9147) reuses the TLS 1.3 handshake and key schedule wholesale and changes only
what datagrams require: a compact record header, per-record sequence numbers that survive
reordering and loss, encryption of those sequence numbers, and its own reliability (records can
be dropped, duplicated, or reordered, so the handshake carries acknowledgements and
retransmits). This server builds DTLS 1.3 on the same hand-rolled TLS 1.3 crypto that backs
HTTP/3 - no second TLS stack.

> **Status.** The DTLS 1.3 **server handshake completes and interoperates** with a reference
> implementation: the record layer (RFC 9147 §4), the handshake framing and reliability primitives
> (§5, §7), and the **server-side handshake state machine** (§5-6) are implemented, and the
> **wolfSSL DTLS 1.3 client completes a full handshake and an application-data round trip** against
> it ([`test/servers/dtls_wolfssl`](../test/servers/dtls_wolfssl/README.md)) - both directly and
> through a **HelloRetryRequest** group renegotiation. This is the one-round-trip full handshake
> (`TLS_AES_128_GCM_SHA256` / X25519 / Ed25519, no PSK / 0-RTT / client auth); a client that does not
> offer an X25519 key_share up front is answered with an HRR carrying an address-bound cookie and
> renegotiates the group (§5.1). Lost flights are recovered by a **retransmission timer** with
> exponential backoff (§5.8), and inbound **ACKs** cancel it. The **CoAP-over-DTLS front-end** is
> complete: the bridge (`pc_coaps_process`) drives the handshake and then runs `pc_coap_server_process`
> inside the encrypted records, and `coaps_server` binds a UDP port (5684, `coaps://`) to a per-peer
> connection pool - routing each datagram to its connection by peer address, driving every handshake
> and the retransmission timer from one `pc_coaps_server_poll()`, and reaping idle connections
> (example [CoapSecure](../examples/L7-Application/CoapSecure/README.md)). Each layer is a complete,
> independently tested unit.

## The record layer

A DTLS 1.3 record comes in two shapes (RFC 9147 §4):

- **DTLSPlaintext** - the classic 13-byte header (`type · legacy_version · epoch ·
48-bit sequence_number · length · fragment`), used unencrypted for the very first handshake
  flight and for alerts in epoch 0.
- **DTLSCiphertext** - the compact **unified header** plus an AEAD-sealed body, used once record
  keys exist. The first byte packs three fixed bits (`001`) and four flags (connection-id
  present, 8- vs 16-bit sequence number, length present, and the low two bits of the epoch), so
  a protected record can be as small as its payload plus a few bytes of header and a 16-byte tag.

Protecting a record:

1. The **inner plaintext** is the payload followed by its true content type (and optional zero
   padding), exactly like TLS 1.3 (RFC 8446 §5.2).
2. It is sealed with **AEAD_AES_128_GCM**. The nonce is the 96-bit write IV XOR the 64-bit
   record sequence number (RFC 9147 §4.2.2 - the epoch is _not_ mixed in), and the associated
   data is the unified header carrying the _plaintext_ sequence number.
3. The **sequence number is then encrypted** (RFC 9147 §4.2.3): a mask is taken from
   `AES-ECB(sn_key, ciphertext[0..15])` and XORed into the on-wire sequence-number bytes. This
   is the same construction as QUIC header protection, so it shares that code. A receiver
   reverses it, then reconstructs the full sequence number as the value nearest the one it
   expects next (RFC 9000 Appendix A.3).

The record keys - the AEAD `key`, the write `iv`, and the sequence-number `sn` key - are each an
`HKDF-Expand-Label` of a TLS 1.3 traffic secret (RFC 8446 §7.3, RFC 9147 §4.2.3).

An **anti-replay sliding window** (RFC 9147 §4.5.1) tracks the highest sequence number accepted
in an epoch and a 64-record bitmap behind it, so a replayed or too-old record is rejected before
it reaches the application.

## The handshake framing

TLS 1.3 assumes a reliable, ordered byte stream; DTLS carries the same handshake messages over
datagrams that can be lost, reordered, or duplicated. The framing layer bridges that gap so the
reused TLS 1.3 message builders never have to know they are on UDP.

- **The 12-byte handshake header** (RFC 9147 §5.2) wraps each message body with a `message_seq`
  and a `fragment_offset` / `fragment_length`, so one message can be split across records and
  each fragment placed independently of the datagram that carried it.
- **Reassembly** collects a message's fragments into a contiguous body. Fragments may arrive out
  of order, duplicated, or **overlapping** (§5.4 requires handling overlap - a sender that
  lowers its estimate of the path MTU may retransmit with smaller, differently-cut fragments);
  received byte ranges are merged into a bounded interval list, and the message is complete once
  a single interval covers it. The bound caps the work a maximally-fragmented flight can force.
- **The ACK message** (RFC 9147 §7, content type 26) is a list of `(epoch, sequence_number)`
  record numbers. DTLS acknowledges records rather than relying on TCP, so a receiver can tell
  the sender which fragments of a flight arrived and the sender can drop them from its next
  retransmission.
- **The HelloRetryRequest cookie** (RFC 9147 §5.1) is the return-routability / anti-amplification
  defense. Before committing handshake state, the server can answer the first ClientHello with a
  cookie the client must echo. The cookie is **stateless**: it carries an
  `HMAC-SHA256(server_secret, version · timestamp · client_address · payload)`, so the client's
  address is authenticated (a cookie minted for one peer is worthless to another) without the
  server storing anything, and a timestamp bounds its lifetime. The opaque payload is where the
  state machine parks what it needs to resume the handshake after the retry.

## The handshake

The server state machine ties the layers together and reuses the TLS 1.3 message builders and key
schedule that already back HTTP/3 - it is a DTLS driver over the same crypto, not a second TLS
stack. A full handshake is one round trip:

1. The client's **ClientHello** arrives in an epoch-0 `DTLSPlaintext` record. The server checks the
   offer (TLS 1.3, X25519, Ed25519), computes the X25519 shared secret, and starts the transcript.
2. The server replies with a **ServerHello** (still epoch 0, plaintext), then derives the
   handshake-traffic secrets from `Transcript-Hash(ClientHello..ServerHello)` and installs the
   **epoch-2** record keys.
3. Under those keys it sends **EncryptedExtensions, Certificate, CertificateVerify, and Finished**
   as epoch-2 `DTLSCiphertext` records. CertificateVerify is an Ed25519 signature over the
   transcript; Finished is the HMAC the client checks to authenticate the whole exchange.
4. From `Transcript-Hash(ClientHello..Finished)` the server derives the **application-traffic**
   secrets and installs the **epoch-3** keys.
5. The client's **Finished** arrives (epoch 2); the server verifies its MAC and the handshake is
   complete. Application data (CoAP) then flows under the epoch-3 keys.

Each TLS handshake message is wrapped in the 12-byte DTLS handshake header and carried in its own
record; the transcript is computed over the reassembled TLS messages (the DTLS framing fields
removed), exactly as RFC 9147 §5.2 requires. Epochs advance 0 → 2 → 3 as the keys are installed
(RFC 9147 §6.1).

When the first ClientHello offers X25519 in `supported_groups` but carries no X25519 key_share, the
server does not begin the expensive handshake. It answers with a **HelloRetryRequest** (RFC 9147 §5.1)
selecting X25519 and a stateless cookie bound to the client's address, and restarts the transcript as
the synthetic `message_hash(ClientHello1)` that RFC 8446 §4.4.1 requires. Only when the second
ClientHello echoes a valid, fresh cookie - proving the client can receive at its claimed address - does
the server spend the X25519 shared secret and its signature. The transcript both sides then
authenticate is `message_hash || HelloRetryRequest || ClientHello2 || ServerHello || ...`, and the
server's outbound `message_seq` counter shifts every later message up by one to account for the HRR.

## Raw Public Keys (RFC 7250)

With `PC_ENABLE_TLS_RPK`, the server can present a **bare public key** instead of an X.509
certificate - the RFC 7252 §9 CoAP-over-DTLS RawPublicKey profile, a natural fit for provisioned,
key-pinned fleets. When the ClientHello carries the `server_certificate_type` extension (RFC 7250,
IANA 20) offering `RawPublicKey`(2), the server selects it, echoes `server_certificate_type =
RawPublicKey` in **EncryptedExtensions**, and its **Certificate** message carries a 44-byte DER
`SubjectPublicKeyInfo` (the Ed25519 key, RFC 8410) in place of the X.509 chain. Everything else is
unchanged: the same Ed25519 key signs **CertificateVerify**, so the client authenticates the exact
key it was handed - no security downgrade, just a smaller handshake with nothing to parse. It is
purely additive and server-side: a client that does not offer the extension still receives the X.509
certificate, and the server never requests a client certificate.

## What is verified

The whole record - header layout, AEAD nonce, associated-data selection, and sequence-number
encryption - is pinned **byte-for-byte** to an _independent_ reconstruction: HKDF-Expand-Label
from stdlib HMAC-SHA256, and AES-128-GCM + AES-ECB from a separate crypto library. A byte-exact
match proves every field is assembled the way the RFC specifies. Round-trips across payload
sizes and content types, sequence-number reconstruction across a 16-bit rollover, tampered-tag
and wrong-epoch rejection, and the replay window are all host-tested (`native_dtls`).

The framing layer is host-tested separately (`native_dtls_hs`): the handshake header round-trips
and rejects malformed fragments; reassembly is exercised in order, out of order, and with
overlapping and duplicate fragments, and its bounds are checked; the ACK message round-trips and
rejects malformed input. The cookie's wire format is pinned **byte-for-byte** to an independent
Python-stdlib HMAC-SHA256, and verification is shown to reject a wrong client address, a tampered
byte, a truncated cookie, and a stale or future-dated timestamp. The TLS 1.3 messages the DTLS
handshake adds - HelloRetryRequest, the cookie extension, and the `message_hash` transcript
wrapper - are host-tested too (`native_dtls_tls13`), with the HelloRetryRequest pinned byte-exact.

The **handshake itself** is proven end-to-end (`native_dtls_conn`): a from-scratch peer, built from
the same primitives but driven independently, completes a full handshake against the server. It
deprotects the server's epoch-2 flight, **verifies the CertificateVerify Ed25519 signature and the
server Finished MAC over the real transcript**, sends its own Finished, and confirms that both
sides install **identical application-traffic keys**. Any wrong transcript byte, epoch, nonce, or
derived secret would fail the AEAD open, the signature check, or the key comparison. The same test
drives the **HelloRetryRequest** path: a ClientHello with no X25519 key_share draws an HRR, and the
retry that echoes the cookie completes the handshake over the `message_hash || HRR || ClientHello2`
transcript; a retry that omits the cookie is rejected. A ClientHello that does not offer TLS 1.3 is
rejected with the right alert. The **retransmission timer** (§5.8) is driven off a controllable clock:
the flight is re-sent with fresh record sequence numbers once the PTO elapses and the client completes
the handshake from that retransmission, the timeout doubles up to the cap and abandons the handshake at
the retransmit ceiling, an ACK covering the flight stops the timer, and a retransmitted client Finished
is re-acknowledged.

Most importantly, the handshake is checked against a **real reference implementation**: the wolfSSL
DTLS 1.3 client completes a handshake and an application-data exchange with the server
([`test/servers/dtls_wolfssl`](../test/servers/dtls_wolfssl/README.md)), once leading with its default
group so the server drives a **HelloRetryRequest** to X25519, and once offering X25519 directly. This
is the strongest evidence the wire format is right, and it caught three DTLS-vs-TLS conformance bugs the
self-referential KATs could not - the `legacy_cookie` field, the DTLS version codepoints, and the
`dtls13` HKDF-Expand-Label prefix (RFC 9147 §5.9), all now fixed (see
[BUGS.md](BUGS.md)). With `PC_ENABLE_TLS_RPK`, a fourth run drives the wolfSSL client in
**RawPublicKey-only** mode (`--rpk`): it offers `server_certificate_type = RawPublicKey` and nothing
else, so it rejects an X.509 answer - a completed handshake proves the server's RFC 7250
`SubjectPublicKeyInfo` is wire-conformant. The handshake is also checked against a **second, unrelated
reference stack** - the [Oryx **CycloneSSL**](https://github.com/Oryx-Embedded/CycloneSSL) DTLS 1.3
client ([`test/servers/cyclone_dtls`](../test/servers/cyclone_dtls/README.md)) completes the same
handshake + application-data round trip, both with a plain X.509 certificate and with a RawPublicKey
(its RFC 7250 verify callback fires only when the server actually sends a bare `SubjectPublicKeyInfo`).
Two independent from-scratch implementations - wolfSSL and CycloneSSL - agreeing on the wire is far
stronger than either alone. DTLS 1.3 derives every secret and record key with the `dtls13` prefix rather
than TLS 1.3's `tls13 `; this library models that as a `Tls13Kdf` variant bound once into the key
schedule, so the record layer and the handshake cannot disagree on it.

## Standards

| Area                           | Standard                  | Status                                                                                                                                        |
| ------------------------------ | ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| DTLSPlaintext / DTLSCiphertext | RFC 9147 §4               | Implemented - unified header build/parse, DTLSPlaintext build/parse                                                                           |
| Record AEAD (AEAD_AES_128_GCM) | RFC 9147 §4.2, SP 800-38D | Implemented - TLS 1.3 nonce (§4.2.2), header-as-AAD; reuses `pc_quic_aead`                                                                    |
| Sequence-number encryption     | RFC 9147 §4.2.3           | Implemented - `AES-ECB(sn_key, ct[0..15])` mask; `sn` key via HKDF-Expand-Label                                                               |
| Sequence-number reconstruction | RFC 9147 §4.2.2           | Implemented - closest-to-expected (RFC 9000 App. A.3)                                                                                         |
| Anti-replay window             | RFC 9147 §4.5.1           | Implemented - 64-record sliding window                                                                                                        |
| Record-key derivation          | RFC 8446 §7.3, RFC 5869   | Implemented - `key` / `iv` / `sn` = HKDF-Expand-Label(traffic secret)                                                                         |
| Key-schedule label prefix      | RFC 9147 §5.9             | Implemented - `dtls13` (not `tls13 `), a `Tls13Kdf` variant bound to the schedule                                                             |
| Handshake header + reassembly  | RFC 9147 §5.2, §5.4       | Implemented - 12-byte header build/parse, overlap-tolerant reassembly                                                                         |
| ACK message                    | RFC 9147 §7               | Implemented - content type 26; the client Finished is acknowledged (§5.8.3)                                                                   |
| HelloRetryRequest cookie       | RFC 9147 §5.1             | Implemented - stateless HMAC-SHA256 cookie binding the client address, minted + verified by the state machine                                 |
| Server handshake state machine | RFC 9147 §5-6             | Implemented - full 1-RTT handshake, epoch 0→2→3; **wolfSSL + CycloneSSL DTLS 1.3 interop**                                                    |
| HelloRetryRequest exchange     | RFC 9147 §5.1             | Implemented - group renegotiation to X25519; cookie round-trip + message_hash transcript; **wolfSSL interop**                                 |
| ACK / timeout retransmission   | RFC 9147 §5.8             | Implemented - PTO timer (exponential backoff, capped, retransmit ceiling), ACK cancellation, retransmitted-Finished re-ACK                    |
| Connection ID                  | RFC 9147 §9               | **Roadmap** - negotiated via extension; CID records are rejected for now                                                                      |
| Raw Public Keys                | RFC 7250, RFC 8410        | Implemented (`PC_ENABLE_TLS_RPK`) - `server_certificate_type` negotiation, Ed25519 SPKI Certificate; **wolfSSL + CycloneSSL `--rpk` interop** |

The handshake DTLS carries is the TLS 1.3 already documented for HTTP/3
(`TLS_AES_128_GCM_SHA256` + X25519 + Ed25519); see [SSH.md](SSH.md) for the shared crypto
primitives and [STANDARDS.md](STANDARDS.md) for the full RFC map.
