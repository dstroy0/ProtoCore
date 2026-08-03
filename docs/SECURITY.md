# Security Documentation

This document covers the security posture of the entire library. Each section
names the specific files that implement or enforce the property described. Where
a security decision appears in source-code documentation, this document links to
it and explains the broader context.

---

## 0. Security Posture at a Glance {#security-posture}

A candid assessment of where the library stands today. ✅ = solid, ⚠️ = acceptable
with caveats, ❌ = a real weakness to be aware of.

### Strong (✅) {#strong-areas}

<details>
<summary><b>Show Strong Security Areas Table</b></summary>

| Area                   | Why it's solid                                                                                                                                                                                                                                                 |
| ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Deterministic memory   | Zero heap after [`begin()`](@ref PC::begin); every buffer is fixed-size and bounds-checked. No use-after-free, no fragmentation, no allocation failure paths.                                                                                        |
| HTTP input validation  | RFC 7230 parser validates every byte; rejects malformed method/path/headers, enforces Host and Content-Length, refuses Transfer-Encoding (no request smuggling surface).                                                                                       |
| WebSocket framing      | Enforces client masking, reserved-opcode/RSV checks, control-frame size and fragmentation rules.                                                                                                                                                               |
| SSH crypto correctness | SHA-256/HMAC/AES-CTR/DH validated against NIST/RFC vectors; RSA verification validated against an openssl KAT and native RSA signing against a sign→verify round-trip with a real 2048-bit private exponent. MAC-verify-before-use; constant-time MAC compare. |
| Secret hygiene         | RSA private key never in static memory (NVS→stack→wipe); session keys, DH secrets, and scratch buffers volatile-wiped after use; key pools are separate linker symbols.                                                                                        |
| SSH hardening option   | `PC_SSH_ALLOW_PASSWORD=0` compiles password auth out entirely for publickey-only deployments.                                                                                                                                                               |

</details>

### Acceptable, with caveats (⚠️) {#acceptable-areas}

<details>
<summary><b>Show Acceptable Areas Table</b></summary>

| Area                        | Caveat                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Transport encryption (HTTP) | Opt-in **HTTPS** ([`PC_ENABLE_TLS`](@ref PC_ENABLE_TLS)) via mbedTLS on a static memory pool - TLS 1.2+/`ECDHE-ECDSA-AES256-GCM-SHA384`, zero-heap (§6). Default off (plain HTTP). Optional **mutual TLS** ([`PC_ENABLE_MTLS`](@ref PC_ENABLE_MTLS)) adds client-certificate authentication (handshake requires a cert chaining to a configured CA), and `wss://` + TLS-SSE run encrypted over the same TLS record layer. Optional **session resumption** ([`PC_ENABLE_TLS_RESUMPTION`](@ref PC_ENABLE_TLS_RESUMPTION)) via RFC 5077 tickets gives returning clients an abbreviated handshake. Caveat: one connection at a time (`MAX_TLS_CONNS`=1). On a trusted LAN plain HTTP is fine; the SSH layer is also an encrypted channel. |
| SSH timing side-channels    | The native software bignum/AES/RSA paths are **not constant-time**, but they are **compile-excluded from firmware**: the software Montgomery cluster is under `#ifndef ARDUINO` (`crypto/bignum.cpp`) and the software AES / native RSA modexp live in the `#else` of an `#ifdef ARDUINO` (`crypto/aes256ctr.cpp`, `crypto/rsa.cpp`). On ESP32 only the hardware/mbedTLS paths are compiled and run; the software paths exist solely for host testing. |
| `Date` response header      | Not emitted (the device usually has no wall clock). RFC 7231 §7.1.1.2 permits this for clock-less servers.                                                                                                                                                                                                                                                                                                                                    |
| Single SSH channel          | One `session` channel per connection; no port-forwarding/X11. Smaller attack surface, but a functional limit.                                                                                                                                                                                                                                                                                                                                 |
| Diagnostic endpoint         | [`PC_ENABLE_DIAG`](@ref PC_ENABLE_DIAG) leaks build configuration; default-off and must stay off in production.                                                                                                                                                                                                                                                                                                                         |
| SNMP agent (v1/v2c)         | Opt-in ([`PC_ENABLE_SNMP`](@ref PC_ENABLE_SNMP), default off). Community-string access only - the community is sent **in cleartext** and is not real authentication (§10). Read-only by default; `Set` is refused unless a separate read-write community is configured. Run only on a trusted/management network and rename the default `public`/`private` communities. For authenticated + encrypted access, enable **SNMPv3 / USM** ([`PC_ENABLE_SNMP_V3`](@ref PC_ENABLE_SNMP_V3): HMAC-SHA-256 auth + AES-128 privacy). |
| JWT bearer auth             | Opt-in ([`PC_ENABLE_JWT`](@ref PC_ENABLE_JWT), default off). HS256 only - a single shared secret both signs and verifies, so any holder of the secret can mint tokens; keep it server-side. Signature compare is constant-time. The verifier checks the signature and can read integer claims (e.g. `exp`), but does not itself enforce expiry - gate on the claim in your handler. Send tokens only over HTTPS (a bearer token is a password). |
| Outbound HTTPS client       | Opt-in ([`PC_ENABLE_HTTP_CLIENT_TLS`](@ref PC_ENABLE_HTTP_CLIENT_TLS), default off). **Encrypt-only by default** (no trust store): resists passive eavesdropping but not an active MITM. Authenticate the peer by installing a CA trust anchor (`http_client_set_ca()`) - verifies the chain + hostname - and/or a SHA-256 certificate pin (`http_client_set_pin()`); a failure aborts the connection. Without one, treat secrets you send as MITM-exposed. |
| MQTT client                 | Opt-in ([`PC_ENABLE_MQTT`](@ref PC_ENABLE_MQTT), default off). Plain MQTT sends the broker username/password and all payloads **in cleartext** - use it only on a trusted segment. For an untrusted network use `mqtts://` ([`PC_ENABLE_MQTT_TLS`](@ref PC_ENABLE_MQTT_TLS)), which shares the client-TLS trust model (encrypt-only by default; install a CA / cert pin via `pc_tls_client_set_ca` / `pc_tls_client_set_pin` to authenticate the broker). |
| WebDAV share                | Opt-in ([`PC_ENABLE_WEBDAV`](@ref PC_ENABLE_WEBDAV), default off). A `dav()` mount exposes a **read/write** view of the filesystem (PUT/DELETE/MKCOL/COPY/MOVE) with **no authentication of its own** - an unprotected mount lets anyone on the network read, overwrite, and delete files. Gate the mount with per-route auth and serve it over HTTPS, and front it with the accept throttles. Locks are **advisory** (a token is issued but not enforced), so they do not prevent concurrent writers. The `..` traversal guard keeps requests inside the mount root. |
| Modbus TCP slave            | Opt-in ([`PC_ENABLE_MODBUS`](@ref PC_ENABLE_MODBUS), default off). Modbus has **no authentication or encryption** - any client that reaches port 502 can read and write the entire data model (coils and holding registers). This is inherent to the protocol. Run it only on an isolated/trusted control network (or behind a VPN), front it with the per-IP accept throttle, and keep safety-critical logic from depending solely on client-supplied register values. Reads/writes are bounds-checked against the configured table sizes (out-of-range -> exception 0x02). |
| OPC UA Binary server        | Opt-in ([`PC_ENABLE_OPCUA`](@ref PC_ENABLE_OPCUA), default off). Runs **SecurityPolicy None** with an Anonymous user identity - messages on TCP/4840 are neither signed nor encrypted, so any client that reaches the port can read/write/browse whatever the registered resolvers expose (Sign / SignAndEncrypt are not implemented). Run it only on an isolated/trusted control network (or behind a VPN), front it with the per-IP accept throttle, and gate writes in the write resolver (return a Bad StatusCode to refuse). |

</details>

### Weak / not implemented (❌) {#weak-areas}

<details>
<summary><b>Show Weak / Not Implemented Areas Table</b></summary>

| Area                          | Status                                                                                                                                                                                                                                                                                                                                                                       |
| ----------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Connection-rate throttling    | SSH bounds failed auth attempts per connection ([`SSH_MAX_AUTH_ATTEMPTS`](@ref SSH_MAX_AUTH_ATTEMPTS), then [`SSH_MSG_DISCONNECT`](@ref SSH_MSG_DISCONNECT)); HTTP closes the socket on every 401 (one guess per connection). Across connections, two opt-in accept-path throttles bound reconnect/brute-force churn: a global fixed-window limit ([`PC_ENABLE_ACCEPT_THROTTLE`](@ref PC_ENABLE_ACCEPT_THROTTLE)) and a per-source-IPv4 fixed-window limit ([`PC_ENABLE_PER_IP_THROTTLE`](@ref PC_ENABLE_PER_IP_THROTTLE), a bounded BSS bucket table - so one noisy client is throttled without affecting others). Both are best-effort and IPv4-keyed; an attacker spreading across many source addresses can still churn the bounded pool, so also filter at the network layer. |
| Replay/DoS on the accept path | A flood of connections exhausts the fixed pool (by design - no heap), returning 503; the accept-path throttles above reduce churn but there is no allow-list or SYN-cookie equivalent.                                                                                                                                                                                                                                        |
| Formal audit                  | The crypto and protocol code is vector-tested and reviewed, but has **not** had an independent security audit. Treat accordingly for high-value deployments.                                                                                                                                                                                                                 |

</details>

The remaining sections document each property in depth.

---

## Table of Contents

- [0. Security Posture at a Glance](#security-posture)
  - [Strong (✅)](#strong-areas)
  - [Acceptable, with caveats (⚠️)](#acceptable-areas)
  - [Weak / not implemented (❌)](#weak-areas)
- [1. Threat Model](#threat-model)
- [2. Memory Safety - HTTP / Core Stack](#memory-safety)
  - [Static Allocation](#core-static)
  - [Buffer Overflow Containment](#core-overflow)
  - [Compile-Time Safety Checks](#core-checks)
- [3. Input Validation - RFC 7230 Parser](#input-validation)
  - [Character-Class Validation](#parser-validation)
  - [Length Limits](#parser-limits)
  - [CR-Injection Defense](#parser-cr)
  - [Transfer-Encoding Rejection](#parser-transfer)
- [4. WebSocket Security](#websocket-security)
  - [Handshake Verification](#ws-handshake)
  - [Frame Validation](#ws-frame)
  - [No Origin Validation by Default](#ws-origin)
- [5. Authentication (HTTP Basic Auth)](#auth-security)
  - [Implementation](#auth-implementation)
  - [Credentials in Flash](#auth-flash)
- [6. TLS / Transport Encryption](#tls-security)
- [7. SSH Cryptographic Layer](#ssh-security)
  - [7.1 Key Exchange - DH-group14-SHA256](#ssh-kex)
  - [7.2 Symmetric Encryption - AES-256-CTR](#ssh-aes)
  - [7.3 Integrity - HMAC-SHA2-256](#ssh-hmac)
  - [7.4 Host Authentication - RSA-SHA2-256](#ssh-host-auth)
  - [7.5 Key Derivation - RFC 4253 §7.2](#ssh-kdf)
  - [7.6 Private Key Lifetime](#ssh-pkey-lifetime)
  - [7.7 Memory Layout Defenses](#ssh-memory-defenses)
  - [7.8 Sequence Number Overflow Guard](#ssh-seq-overflow)
  - [7.9 Secure Wipe](#ssh-wipe)
  - [7.10 Random Number Generation](#ssh-rng)
- [8. Diagnostic Endpoint](#diagnostic-endpoint)
- [9. Known Limitations and Non-Goals](#known-limitations)
- [10. SNMP Agent Security](#snmp-security)
- [11. Hardening Checklist](#hardening-checklist)
  - [General](#general-hardening)
  - [Authentication](#auth-hardening)
  - [SSH](#ssh-hardening)
  - [SNMP](#snmp-hardening)
  - [Build Hardening](#build-hardening)

---

## 1. Threat Model {#threat-model}

**In scope (attacks this library defends against):**

- **Buffer overflow → key/state exfiltration.** An attacker who can trigger an
  out-of-bounds read or write in any packet receive buffer should not be able to
  reach cryptographic key material in a single stride.
- **Heap spray / heap grooming.** All library-managed state is statically
  allocated in BSS; there is no heap to spray or groom.
- **Malformed HTTP request injection.** Every byte of method, path, header
  field-name, and header field-value is validated against RFC 7230 character
  classes before being stored.
- **Padding oracle attacks (SSH).** HMAC is verified before any plaintext byte
  is acted upon; the connection is closed on MAC failure without echo.
- **Small-subgroup DH attacks.** The received DH public value `e` is validated
  (`1 < e < p-1`) before any computation.
- **Private key persistence in RAM.** The RSA host private key is loaded from
  NVS to the stack, used, then volatile-wiped. It never touches static or heap
  memory.
- **CTR keystream reuse at 32-bit sequence wrap.** Connections are closed when
  the sequence number approaches 2^32.

**Out of scope (not defended here):**

- Full constant-time implementations of the software crypto paths. The native
  (non-Arduino) paths exist for testing only and are not constant-time.
- Physical attacks (fault injection, power analysis, electromagnetic side
  channels).
- Vulnerabilities in the underlying ESP-IDF / mbedTLS / lwIP / FreeRTOS
  libraries. This library relies on their correctness.
- Client-side certificate verification (SSH host key verification is the
  client's responsibility; this library presents its host key and signs the KEX
  hash).
- Rekeying (SSH sequence number overflow closes the connection instead of
  rekeying; rekeying is not implemented).

---

## 2. Memory Safety - HTTP / Core Stack {#memory-safety}

**Files:** [src/network_drivers/transport/tcp.cpp](@ref tcp.cpp),
[src/network_drivers/presentation/presentation.cpp](@ref presentation.cpp),
[src/protocore_config.h](@ref protocore_config.h)

### Static Allocation {#core-static}

Every buffer that can hold user-supplied data is a fixed-size array in BSS,
sized at compile time:

| Buffer                         | Symbol                   | Size                                        |
| ------------------------------ | ------------------------ | ------------------------------------------- |
| TCP ring buffer per connection | `conn_pool[i].rx_buffer` | [`RX_BUF_SIZE`](@ref RX_BUF_SIZE) bytes     |
| HTTP request body              | `http_pool[i].body`      | `BODY_BUF_SIZE + 1` bytes                   |
| WebSocket frame payload        | `ws_pool[i].buf`         | [`WS_FRAME_SIZE`](@ref WS_FRAME_SIZE) bytes |
| SSH packet receive             | `ssh_pkt[i].rx_buf`      | `SSH_RX_BUF_SIZE` bytes                     |

No `malloc`, `new`, or `pvPortMalloc` is called after `begin()` for any of
these paths. The total footprint is a compile-time constant; there is no
fragmentation, no use-after-free, and no heap spray surface.

### Buffer Overflow Containment {#core-overflow}

Each buffer is separately named in BSS. An overflow inside one connection's
ring buffer cannot reach another connection's data without crossing the entire
`conn_pool[]` symbol and then whatever the linker places next - it cannot
reach the SSH key store in a single linear stride (see §7.7).

### Compile-Time Safety Checks {#core-checks}

`protocore_config.h` contains `#error` guards that catch impossible
combinations at build time:

```cpp
#if MAX_WS_CONNS + MAX_SSE_CONNS > MAX_CONNS
#  error "MAX_WS_CONNS + MAX_SSE_CONNS must not exceed MAX_CONNS"
#endif
#if BODY_BUF_SIZE > RX_BUF_SIZE
#  error "BODY_BUF_SIZE must not exceed RX_BUF_SIZE"
#endif
```

This prevents the common embedded mistake of configuring a body buffer larger
than the transport ring buffer, which would cause the body parser to believe it
has buffered data that was never written.

---

## 3. Input Validation - RFC 7230 Parser {#input-validation}

**Files:** [src/network_drivers/presentation/http/http_parser/http_parser.cpp](@ref http_parser.cpp),
[src/network_drivers/presentation/http/http_parser/http_parser.h](@ref http_parser.h)

### Character-Class Validation {#parser-validation}

The parser validates every byte of every user-controlled field before it is
stored or acted upon. This is a defense-in-depth measure: even if a buffer
overflow were possible, the parser rejects payloads that contain non-printing
or control characters before they enter any buffer.

| Field              | Allowed characters                                      | RFC reference   | Violation response |
| ------------------ | ------------------------------------------------------- | --------------- | ------------------ |
| Method             | `tchar` (`ALPHA DIGIT ! # $ % & ' * + - . ^ _ \` \| ~`) | RFC 7230 §3.1.1 | 400                |
| Path               | `VCHAR` (%x21–7E)                                       | RFC 3986 §3.3   | 400                |
| Query              | `VCHAR` (%x21–7E)                                       | RFC 3986 §3.4   | 400                |
| Header field-name  | `tchar`                                                 | RFC 7230 §3.2   | 400                |
| Header field-value | `VCHAR`, SP, HTAB, obs-text (%x80–FF)                   | RFC 7230 §3.2   | 400                |

### Length Limits {#parser-limits}

| Field              | Limit                                       | Violation response    |
| ------------------ | ------------------------------------------- | --------------------- |
| Path               | `MAX_PATH_LEN - 1` bytes                    | 414 URI Too Long      |
| Body               | [`BODY_BUF_SIZE`](@ref BODY_BUF_SIZE) bytes | 413 Payload Too Large |
| Header field-name  | [`MAX_KEY_LEN`](@ref MAX_KEY_LEN) bytes     | silently truncated    |
| Header field-value | [`MAX_VAL_LEN`](@ref MAX_VAL_LEN) bytes     | silently truncated    |

Path and body oversize conditions are detected before any byte is written to
the destination buffer and result in an error response sent immediately.

### CR-Injection Defense {#parser-cr}

A bare CR (`\r`) inside a header field-name transitions the parser to an error
state (400). This prevents HTTP response splitting and CRLF injection via
carefully crafted header values that contain `\r\n`.

### Transfer-Encoding Rejection {#parser-transfer}

The library does not support chunked transfer encoding. Any request containing
a `Transfer-Encoding` header is rejected with 501 Not Implemented. This closes
the "HTTP request smuggling via chunked+content-length desync" attack class for
the server side.

---

## 4. WebSocket Security {#websocket-security}

**Files:** [src/network_drivers/presentation/http/websocket/websocket.cpp](@ref websocket.cpp),
[src/network_drivers/presentation/http/websocket/websocket.h](@ref websocket.h)

### Handshake Verification {#ws-handshake}

The WebSocket upgrade handshake requires:

1. An HTTP `GET` request (any other method is rejected with 400).
2. A `Connection: Upgrade` header (case-insensitive match).
3. An `Upgrade: websocket` header (case-insensitive match).
4. A `Sec-WebSocket-Key` header containing a 24-character base64 value
   (16 decoded bytes).

The accept key is computed as SHA-1(`client_key + WS_GUID`) using the mbedTLS
hardware accelerator on ESP32. The GUID (`258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
is defined in RFC 6455 §1.3 and prevents cross-protocol attacks where an HTTP
server is tricked into treating a non-WebSocket request as one.

### Frame Validation {#ws-frame}

- Client frames must be masked (RFC 6455 §5.1 requires client-to-server masking).
  Unmasked frames are rejected and the connection is closed.
- Fragment reassembly is not supported. Multi-frame messages must fit in a
  single `WS_FRAME_SIZE`-byte frame. Oversized frames are rejected.
- Control frames (Close, Ping, Pong) are handled automatically. Ping is answered
  with Pong without forwarding to the application handler.

### No Origin Validation by Default {#ws-origin}

The library does not validate the `Origin` header by default. For internet-
facing deployments, the application should check `http_get_header(req, "Origin")`
in the WebSocket route handler and reject origins that are not in an allowlist.

---

## 5. Authentication (HTTP Basic Auth) {#auth-security}

**Files:** [src/network_drivers/presentation/presentation.cpp](@ref presentation.cpp),
[src/protocore_config.h](@ref protocore_config.h)

### Implementation {#auth-implementation}

HTTP Basic Auth credentials are compared after base64-decoding the
`Authorization` header value. The comparison uses the standard library
`strncmp`. **This comparison is NOT constant-time** and is therefore
susceptible to timing side channels if an attacker can measure the response
time precisely.

For embedded device use this is generally acceptable because:

- The comparison time difference (a few nanoseconds) is dominated by network
  jitter on WiFi.
- The credential is transmitted in base64 (effectively plaintext) over HTTP; if
  the deployment requires true authentication security, TLS (or SSH) must be
  used at the transport layer.

### Credentials in Flash {#auth-flash}

Credentials passed to `server.on(path, method, handler, realm, user, pass)` are
stored in the application's `.rodata` or `.text` section. They are never copied
to heap or writable RAM by the library. Flash memory is read-only; an attacker
who has read access to writable RAM cannot extract the credential via a memory
read exploit.

---

## 6. TLS / Transport Encryption (HTTPS) {#tls-security}

HTTPS is available as an opt-in layer (`PC_ENABLE_TLS`, default off) built on
mbedTLS, designed so it does **not** break the library's zero-heap / determinism
guarantee. With it disabled, no TLS code is compiled and the build is unchanged.

### Static memory - no heap {#tls-static}

mbedTLS normally heap-allocates per connection (TLS context, ~16 KB IN + 16 KB
OUT record buffers, handshake temporaries). Here every mbedTLS allocation is
redirected to a **fixed BSS arena** (`PC_TLS_ARENA_SIZE`) via a custom
first-fit allocator installed with `mbedtls_platform_set_calloc_free()` - so the
system heap is never touched after `begin()`. The arena is sized for the
worst-case handshake; if it is ever too small the handshake **fails cleanly**
(allocation returns NULL → connection dropped), never corrupting memory. The
live high-water mark is queryable via [`pc_tls_arena_peak()`](@ref pc_tls_arena_peak).
Measured peak for one ECDSA P-256 connection on Arduino-esp32 (16 KB IN/OUT
records) is ~41.5 KB; the default arena is 48 KB.

- ESP32/Arduino only - mbedTLS is not part of the native test build, so TLS is
  verified on hardware (`openssl s_client` + browser), not in the native suite.
  The arena allocator's accounting is the unit-testable part.

### Cryptography {#tls-crypto}

- **Protocol:** TLS 1.2 minimum (`MBEDTLS_SSL_VERSION_TLS1_2`); TLS 1.3 is
  negotiated when the client offers it.
- **Cipher (verified on device):** `ECDHE-ECDSA-AES256-GCM-SHA384` - forward
  secrecy via ephemeral ECDHE, ECDSA server authentication, AEAD AES-256-GCM
  (hardware-accelerated on the ESP32 AES/SHA engines).
- **RNG:** the ESP32 hardware CSPRNG (`esp_fill_random`) feeds mbedTLS directly
  (`mbedtls_ssl_conf_rng`); no software DRBG state to seed or persist.
- **Server key/cert:** loaded once at `begin_tls()` / `tls_cert()` into the
  static pool. An ECDSA P-256 cert is recommended (faster handshake, smaller
  than RSA). The private key lives in the static arena for the device's lifetime
  (it is not wiped per-connection like the SSH host key).

### Limitations / caveats (⚠️) {#tls-caveats}

- **One TLS connection at a time** by default (`MAX_TLS_CONNS` = 1). A second
  concurrent TLS connection roughly doubles the ~32 KB record-buffer cost, which
  overflows the static DRAM budget on a stock Arduino build; raising it requires
  shrinking the IDF record sizes (`CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN`, an
  ESP-IDF-framework build).
- **Client-certificate auth is optional, off by default** - enable mutual TLS
  with [`PC_ENABLE_MTLS`](@ref PC_ENABLE_MTLS) and
  [`tls_require_client_cert()`](@ref PC::tls_require_client_cert): the
  handshake then requires a client cert chaining to the configured CA and exposes
  the verified peer subject DN to handlers. Without it the server does not
  authenticate clients via TLS (use HTTP Basic/Digest/JWT auth for client
  identity instead).
- **Session resumption is optional** ([`PC_ENABLE_TLS_RESUMPTION`](@ref PC_ENABLE_TLS_RESUMPTION), default off, TLS 1.2 / RFC 5077 tickets). When on, a returning client resumes with an abbreviated handshake; the session is sealed into the client's ticket (a stateless server), so no per-session cache grows. Security trade-off: the ticket-sealing key (AES-256-GCM, in the static arena) protects every outstanding ticket, so its compromise would expose those resumed sessions - it rotates automatically on the [`PC_TLS_TICKET_LIFETIME_S`](@ref PC_TLS_TICKET_LIFETIME_S) schedule (default 1 day) to bound that window. Full handshakes still get per-connection ECDHE forward secrecy. Leave it off if ticket-key compromise is in your threat model.
- **`wss://` and TLS Server-Sent Events run over the record layer** when TLS is
  enabled: a WebSocket/SSE upgrade on a TLS connection is encrypted frame-by-frame
  (transparent to handler code), not rejected. Plain `ws://`/SSE on a non-TLS
  listener is unaffected.
- The example certificate in `examples/L4-Transport/HTTPS` is a **public throwaway** -
  generate your own key/cert and keep the key secret.

### When to use what {#tls-guidance}

- **Trusted LAN:** plain HTTP is often fine (WPA2/WPA3 protects the segment);
  it is also the fastest path.
- **Internet-facing:** enable on-device HTTPS as above, or place a
  TLS-terminating reverse proxy (nginx, HAProxy) in front, or use the SSH
  transport (§7). On-device HTTPS keeps the device self-contained at the cost of
  ~48 KB RAM and one connection at a time.

---

## 7. SSH Cryptographic Layer {#ssh-security}

**Files:**

- [src/network_drivers/presentation/ssh/transport/ssh_keymat.h](@ref ssh_keymat.h) - security model, types, wipe helpers
- [src/crypto/asymmetric/bignum.h](@ref bignum.h) / [.cpp](@ref bignum.cpp) - 2048-bit Montgomery arithmetic (shared library primitive)
- [src/crypto/hash/sha256.h](@ref sha256.h) / [.cpp](@ref sha256.cpp) - SHA-256 (shared library primitive)
- [src/crypto/mac/hmac_sha256.h](@ref hmac_sha256.h) / [.cpp](@ref hmac_sha256.cpp) - HMAC-SHA2-256 (shared library primitive)
- [src/crypto/cipher/aes256ctr.h](@ref aes256ctr.h) / [.cpp](@ref aes256ctr.cpp) - AES-256-CTR (shared library primitive)
- [src/network_drivers/presentation/ssh/transport/ssh_dh.h](@ref ssh_dh.h) / [.cpp](@ref ssh_dh.cpp) - DH-group14-SHA256 KEX
- [src/crypto/asymmetric/rsa.h](@ref rsa.h) / [.cpp](@ref rsa.cpp) - RSA-2048 PKCS#1 v1.5 verify + software sign (shared primitive)
- [src/network_drivers/tls/ssh_rsa.h](@ref ssh_rsa.h) / [.cpp](@ref ssh_rsa.cpp) - SSH RSA host-key layer (NVS key, signing, "ssh-rsa" blob)
- [src/network_drivers/presentation/ssh/transport/ssh_packet.h](@ref ssh_packet.h) / [.cpp](@ref ssh_packet.cpp) - binary packet protocol

---

### 7.1 Key Exchange - DH-group14-SHA256 {#ssh-kex}

<details>
<summary><b>Expand 7.1 Key Exchange - DH-group14-SHA256 Details</b></summary>

**RFCs:** RFC 3526 §3 (MODP group 14), RFC 8268 §3 (SHA-256 with group14)

The SSH key exchange uses Diffie-Hellman with the 2048-bit MODP group 14
prime (p) and generator g=2. The exchange hash uses SHA-256.

**Why group14?**

Group14 (2048-bit) provides approximately 112 bits of security (NIST SP 800-131A
equivalent), which is the minimum recommended for new deployments as of 2024.
It is widely supported by existing SSH clients. Group16 (4096-bit) provides
~140 bits but is 4× slower on ESP32; the hardware mbedTLS path makes group14
fast enough.

**Protocol flow**

```
Client                              Server
──────                              ──────
SSH_MSG_KEXDH_INIT ──── e ──────►  (1) y = esp_fill_random(2048 bits)
                                    (2) f = 2^y mod p         [ssh_dh_generate()]
                                    (3) validate e: 1 < e < p-1
                                    (4) K = e^y mod p          [ssh_dh_finish()]
                                    (5) H = SHA256(V_C||V_S||I_C||I_S||K_S||e||f||K)
                                    (6) sig = RSA-SHA2-256(host_key, H)
                    ◄── K_S,f,sig ─ SSH_MSG_KEXDH_REPLY
                                    (7) y wiped; K → key derivation; K wiped
```

**Client value validation**

The received client public value `e` is checked against 1 and p-1 by
[`bn_dh_validate()`](@ref bn_dh_validate) before any computation. A value of 1 leaks the server
scalar y directly (K = 1^y = 1, fixed). A value of p-1 causes K to be either
1 or p-1 (depending on y parity), which is also a fixed value. Both are
well-known small-subgroup attacks specified in RFC 4253 §8.

**Private scalar generation**

The server's private scalar `y` is generated by `esp_fill_random()` (hardware
RNG on ESP32; see §7.10). The top two bits are masked to ensure y < p; the
least significant bit 1 is set to ensure y ≠ 0 and y ≠ 1. A fresh `y` is
generated for every connection.

**y is never reused across connections.** Reuse of y with two different client
values e₁, e₂ allows recovery of y via:

    log_g(K₁) / log_g(K₂) = y  (both sides known → y can be derived)

[`ssh_dh_generate()`](@ref ssh_dh_generate) always generates fresh randomness; there is no caching.

</details>

---

### 7.2 Symmetric Encryption - AES-256-CTR {#ssh-aes}

<details>
<summary><b>Expand 7.2 Symmetric Encryption - AES-256-CTR Details</b></summary>

**RFC:** RFC 4344 §4

AES-256-CTR is a stream cipher mode. The keystream is produced by encrypting a
128-bit counter with AES-256 and XOR-ing it with the plaintext.

**Why CTR mode?**

- No padding required (stream cipher).
- Encryption and decryption are identical operations.
- Parallelisable (hardware accelerator on ESP32).
- No "padding oracle" vulnerability class (unlike CBC).

**Counter management**

The counter is initialized to IV_c2s or IV_s2c (derived from the KEX; see §7.5)
and incremented as a big-endian 128-bit integer after each 16-byte block. The
counter never repeats within a connection because:

- The IV is unique per connection (derived from a unique K and H).
- The sequence number overflow guard (§7.8) closes the connection before enough
  packets could be sent to cause a counter repetition within the 2^128 counter
  space (which is practically unreachable in any case).

**Platform-specific implementations**

- **Arduino (ESP32):** `mbedtls_aes_crypt_ecb()` with the hardware AES accelerator
  inside the ESP32 crypto engine. The key schedule is managed by mbedtls.
- **Native (test host):** Software AES-256 using the standard S-box and
  MixColumns polynomial (FIPS 197). NOT constant-time; test-only.

The platform is selected by `#ifdef ARDUINO` in
[aes256ctr.cpp](@ref aes256ctr.cpp).
No guessed buffer sizes are used: the Arduino path embeds `mbedtls_aes_context`
directly (by `#include <mbedtls/aes.h>`), so the compiler enforces the exact
size. The "opaque buffer of guessed size" anti-pattern is explicitly rejected.

</details>

---

### 7.3 Integrity - HMAC-SHA2-256 {#ssh-hmac}

<details>
<summary><b>Expand 7.3 Integrity - HMAC-SHA2-256 Details</b></summary>

**RFC:** RFC 2104 (HMAC), RFC 6668 §2 (hmac-sha2-256 for SSH)

Every SSH binary packet carries a 32-byte HMAC-SHA2-256 MAC computed over:

    HMAC-SHA256(mac_key, seq_no_be32 || packet_length || padding_length || payload || padding)

where `seq_no_be32` is the 32-bit packet sequence number in big-endian encoding.

**MAC key size**

The mac keys (mac_key_c2s, mac_key_s2c) are 32 bytes each, derived from the
key exchange (§7.5). HMAC-SHA256 block length is 64 bytes; a 32-byte key is
shorter than the block length and is padded with zeros to 64 bytes internally.
This is correct per RFC 2104 §2: keys shorter than B (block length) are
right-padded with zeros.

**Verify-before-use**

The MAC is verified **before** the payload bytes are forwarded to any protocol
handler. The verification uses a constant-time 32-byte comparison (`ct_memcmp`
in [ssh_packet.cpp](@ref ssh_packet.cpp))
that accumulates XOR differences without early-exit branching. This prevents
timing-oracle attacks where an attacker measures how many bytes of the MAC
matched before a short-circuit return.

If MAC verification fails:

1. The decrypted payload buffer is wiped.
2. The receive buffer is wiped.
3. [`ssh_pkt_recv()`](@ref ssh_pkt_recv) returns -1.
4. The caller (SSH session layer) closes the TCP connection immediately.
5. No byte of the payload is acted upon.

</details>

---

### 7.4 Host Authentication - RSA-SHA2-256 {#ssh-host-auth}

<details>
<summary><b>Expand 7.4 Host Authentication - RSA-SHA2-256 Details</b></summary>

**RFC:** RFC 8332 §3 (rsa-sha2-256), RFC 8017 §8.2 (PKCS#1 v1.5 signature)

The server authenticates itself to the client by signing the exchange hash H
with its RSA-2048 private key using PKCS#1 v1.5 and SHA-256.

**Why PKCS#1 v1.5 and not RSA-PSS?**

The SSH protocol specifies `rsa-sha2-256` (RFC 8332), which is defined as
PKCS#1 v1.5 with SHA-256 as per RFC 8017 §8.2. RSA-PSS is a different scheme
(`rsa-sha2-256` with PSS would require a separate algorithm identifier). All
major SSH clients (OpenSSH ≥ 7.2) support `rsa-sha2-256`. This library
implements the algorithm the RFC requires.

**Signature construction**

1. Compute `digest = SHA-256(H)`.
2. Build PKCS#1 v1.5 DigestInfo:
   ```
   0x30 0x31                   SEQUENCE (49 bytes)
   0x30 0x0d                   SEQUENCE (AlgorithmIdentifier, 13 bytes)
   0x06 0x09 60 86 48 01 65 03 04 02 01  OID 2.16.840.1.101.3.4.2.1 (SHA-256)
   0x05 0x00                   NULL (parameters)
   0x04 0x20                   OCTET STRING (32 bytes, digest follows)
   <digest[32]>
   ```
3. Pad to 256 bytes (RSA-2048):
   ```
   0x00 0x01 [202 × 0xFF] 0x00 <DigestInfo[51]>
   ```
4. Compute `sig = pad^d mod n` (RSA private-key operation).

The 256-byte signature is transmitted in the SSH_MSG_KEXDH_REPLY.

**Key storage and loading**

- **NVS namespace:** `"ssh_host_key"`, key `"priv_der"`.
- **Format:** DER-encoded PKCS#1 RSAPrivateKey (RFC 8017 Appendix C).
- The host key must be provisioned using `nvs_set_blob` before the first
  connection is accepted. The library does not generate a key.
- The NVS partition should be encrypted (enable NVS encryption in the ESP-IDF
  menuconfig) to protect the key at rest.

For the private key lifetime policy, see §7.6.

</details>

---

### 7.5 Key Derivation - RFC 4253 §7.2 {#ssh-kdf}

<details>
<summary><b>Expand 7.5 Key Derivation - RFC 4253 §7.2 Details</b></summary>

Six values are derived from the shared secret K and exchange hash H:

| Label | Use                                        | Size           |
| ----- | ------------------------------------------ | -------------- |
| `'A'` | IV_c2s (AES-CTR IV, client→server)         | first 16 bytes |
| `'B'` | IV_s2c (AES-CTR IV, server→client)         | first 16 bytes |
| `'C'` | key_c2s (AES-256 key, client→server)       | 32 bytes       |
| `'D'` | key_s2c (AES-256 key, server→client)       | 32 bytes       |
| `'E'` | mac_c2s (HMAC-SHA2-256 key, client→server) | 32 bytes       |
| `'F'` | mac_s2c (HMAC-SHA2-256 key, server→client) | 32 bytes       |

Each value is derived as:

    SHA-256(mpint(K) || H || label || session_id)

where for the first (and only, in this implementation) KEX, `session_id = H`.

`K` is encoded as an SSH `mpint`: a 4-byte big-endian length followed by an
optional `0x00` prefix byte (required if the MSB of K is set, to indicate a
positive integer), followed by the 256-byte big-endian value of K.

After derivation, all stack temporaries (`key_c2s`, `key_s2c`, `iv_c2s`,
`iv_s2c`, the SHA-256 context) are zeroed via `ssh_wipe()` before
[`ssh_dh_derive_keys()`](@ref ssh_dh_derive_keys) returns.

</details>

---

### 7.6 Private Key Lifetime {#ssh-pkey-lifetime}

<details>
<summary><b>Expand 7.6 Private Key Lifetime Details</b></summary>

This is the most security-critical design decision in the SSH implementation.
The same principle is documented in the source at the top of
[ssh_rsa.h](@ref ssh_rsa.h).

**Why the private key must never be in static memory**

If the RSA-2048 private key (`d`, `p`, `q`, `dp`, `dq`, `qinv`) were stored
in a global or static array:

1. **Linear overflow attack:** Any single-direction buffer overflow in any
   receive path, anywhere in the process, could potentially reach and exfiltrate
   the private key. Static variables are all in BSS/data, which is a flat
   contiguous region. There is no hardware barrier between `conn_pool[i].rx_buffer`
   and a hypothetical `rsa_private_key[256]` stored in BSS.

2. **Cold-boot persistence:** SRAM on ESP32 retains data for a brief period
   after power loss. A key in static memory would be recoverable from a cold-
   boot dump of SRAM for up to several hundred milliseconds after power-off.

3. **Use-after-free / dangling read:** A bug that reads a stale pointer into
   BSS could silently return key bytes without the application knowing.

**The mandated lifetime**

```
NVS (encrypted flash)
    │
    ▼  ssh_rsa_sign() begins
local SshRsaPrivKey on the stack   ← only location the private key exists in RAM
    │
    ▼  mbedtls_pk_parse_key() (Arduino) or direct byte copy (native test)
private key loaded into local struct
    │
    ▼  sign operation completes
ssh_wipe(&priv, sizeof(priv))       ← volatile loop, not elided by compiler
    │
    ▼  ssh_rsa_sign() returns
stack frame deallocated - no key bytes remain anywhere in RAM
```

The exposure window is the duration of a single RSA-2048 sign operation,
approximately 0.5–2 ms on ESP32. During this window, the key is on the stack,
which is in SRAM. It is not accessible from a single linear overflow of any
BSS buffer because the stack is a separate region with an opposite growth
direction.

**NVS encryption recommendation**

The NVS partition should be encrypted using the ESP-IDF NVS encryption feature
(enable `CONFIG_NVS_ENCRYPTION` in menuconfig, provision the NVS encryption
key via `nvs_flash_generate_keys()`). Without NVS encryption, the private key
is stored in plaintext flash and can be read by anyone with physical access to
the device.

</details>

---

### 7.7 Memory Layout Defenses {#ssh-memory-defenses}

<details>
<summary><b>Expand 7.7 Memory Layout Defenses Details</b></summary>

**Source:** [ssh_keymat.h](@ref ssh_keymat.h)
(Defense 1 comment block)

Three separate BSS symbols hold SSH state:

```
ssh_pkt[MAX_SSH_CONNS]     ← packet state + receive buffers
ssh_keys[MAX_SSH_CONNS]    ← AES keys + IV/counters + MAC keys  (separate symbol)
ssh_dh[MAX_SSH_CONNS]      ← DH scalars y, f, K        (separate symbol)
```

The linker assigns each symbol its own address. An overflow that starts inside
`ssh_pkt[i].rx_buf` and continues linearly would first overwrite other
[`SshPacketState`](@ref SshPacketState) fields, then overflow past the end of `ssh_pkt[]`, then
traverse whatever the linker placed between `ssh_pkt` and `ssh_keys` (which
could be hundreds or thousands of bytes of other data), before reaching any
byte of `ssh_keys`.

This is not obscurity - the principle is the same as used by hardware security
modules that physically separate key memory from bus-accessible memory. The
mechanism here is linker symbol isolation rather than hardware isolation, which
raises the attack bar significantly without requiring hardware security features.

**Contrast with the alternative:** If all SSH state were in a single
`SshConn` struct with fields in the order `rx_buf, aes_key, pc_hmac_key`, then a
one-byte overflow past `rx_buf` immediately reaches `aes_key`. The separate-
symbol layout eliminates that risk.

</details>

---

### 7.8 Sequence Number Overflow Guard {#ssh-seq-overflow}

<details>
<summary><b>Expand 7.8 Sequence Number Overflow Guard Details</b></summary>

**Source:** [ssh_packet.h](@ref ssh_packet.h),
[ssh_packet.cpp](@ref ssh_packet.cpp)

SSH sequence numbers are 32-bit unsigned integers (RFC 4253 §6.4). They wrap
at 2^32. Two problems arise at wrap:

1. **AES-CTR counter reuse.** The AES-CTR keystream is indexed by the counter
   (IV) value and does not reset at sequence number wrap. However, if the
   session persists long enough for the _sequence number_ to wrap, the
   _sequence-number field_ in the MAC input wraps too. Whether this directly
   causes keystream reuse depends on implementation details, but RFC 4253 §9.3.4
   recommends rekeying before the sequence number wraps.

2. **MAC oracle.** If the sequence number wraps and the MAC key is unchanged,
   an attacker who observed packet N can predict that packet N + 2^32 will have
   the same sequence number in the MAC input, potentially allowing chosen-
   plaintext/ciphertext attacks.

**Policy:** When `seq_no_send` or `seq_no_recv` reaches [`SSH_SEQ_CLOSE_THRESHOLD`](@ref SSH_SEQ_CLOSE_THRESHOLD)
(0xFFFFFFF0 - 16 below the 32-bit maximum), the library closes the connection.
[`ssh_pkt_send()`](@ref ssh_pkt_send) returns -1 at this point; the caller is responsible for
closing the TCP connection and notifying the client.

A rekeying implementation (SSH_MSG_KEXINIT exchange to install new keys and
reset sequence numbers) would allow long-lived sessions. This is a known
limitation and is noted in [docs/CHANGELOG.md](CHANGELOG.md).

</details>

---

### 7.9 Secure Wipe {#ssh-wipe}

<details>
<summary><b>Expand 7.9 Secure Wipe Details</b></summary>

**Source:** [ssh_keymat.h](@ref ssh_keymat.h)

```cpp
static inline void ssh_wipe(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0; i < len; i++) p[i] = 0;
}
```

**Why not `memset`?**

C and C++ compilers are explicitly permitted by the standard to elide a `memset`
call whose result is "not observed" before the memory goes out of scope or is
overwritten. This optimization is commonly performed by:

- GCC with `-O2` or higher (`-fdce` / dead code elimination).
- Clang with `-O1` or higher (same analysis).
- The ESP-IDF toolchain (Xtensa GCC) with the default `-Os` optimization level.

A `memset` that the compiler removes silently leaves key bytes in memory.
The `volatile` pointer cast forces every store to actually reach SRAM because
`volatile` reads/writes are not subject to the "as-if rule" and cannot be
removed.

**Where ssh_wipe is used**

| What is wiped                                 | When                                                             | File             |
| --------------------------------------------- | ---------------------------------------------------------------- | ---------------- |
| `crypto_work[1536]`                           | After every [`bn_expmod_group14()`](@ref bn_expmod_group14) call | `crypto/bignum.cpp` |
| `SshDhState.y`, `.K`                          | After key derivation in [`ssh_dh_finish()`](@ref ssh_dh_finish)  | `ssh_dh.cpp`     |
| RSA sign bignum temporaries (n/d/m/s)         | Before [`pc_rsa_sign_sw()`](@ref pc_rsa_sign_sw) returns       | `crypto/rsa.cpp` |
| [`SshKeyMat`](@ref SshKeyMat)                 | On connection close / error                                      | `ssh_keymat.h`   |
| [`SshDhState`](@ref SshDhState) (full struct) | On connection close / error                                      | `ssh_keymat.h`   |
| DER private key stack copy                    | After `mbedtls_pk_parse_key()` in `pc_ssh_rsa_load_pubkey()`    | `ssh_rsa.cpp`    |
| Key derivation stack temporaries              | After `ssh_dh_derive_keys()`                                     | `ssh_dh.cpp`     |

</details>

---

### 7.10 Random Number Generation {#ssh-rng}

<details>
<summary><b>Expand 7.10 Random Number Generation Details</b></summary>

**Source:** `test/mocks/Arduino.h` (native mock),
ESP-IDF `esp_random.h` (Arduino production)

**Production (Arduino / ESP32)**

The server DH private scalar `y` and all random padding bytes are generated
using `esp_fill_random()`, which wraps `esp_random()`. On ESP32:

- `esp_random()` reads from the hardware RNG peripheral, which is seeded by
  thermal noise from the analog front-end and feeds into a 32-bit LFSR with
  hardware whitening.
- The ESP-IDF documentation (v5.x) classifies this as a true random number
  generator suitable for cryptographic use when the RF subsystem (WiFi or
  Bluetooth) is active, which is always the case when the web server is running.
- Entropy rate is approximately 2 Mbit/s; `esp_fill_random(256 bytes)` is
  effectively instantaneous.

**Native test environment**

The native test mock in `test/mocks/Arduino.h` provides a time-seeded PRNG:

```cpp
inline uint32_t esp_random() {
    static bool seeded = false;
    static uint32_t ctr = 0;
    if (!seeded) { srand((unsigned)time(nullptr) ^ 0xDEADBEEFu); seeded = true; }
    return (uint32_t)rand() ^ (++ctr * 0x9e3779b9u);
}
```

**This is NOT cryptographically secure.** It is used only for unit testing
the structural correctness of the DH, RSA, and packet code. The tests that
exercise these paths use known-value inputs where possible (small exponents,
fixed test keys) and do not rely on the RNG producing cryptographically
unpredictable output.

The native mock is clearly marked and will not compile on Arduino targets
because `test/mocks/Arduino.h` is only included by the `native_ssh` PlatformIO
environment.

</details>

---

## 8. Diagnostic Endpoint {#diagnostic-endpoint}

**File:** [protocore.cpp](@ref protocore.cpp)

The optional `PC_ENABLE_DIAG` build flag enables a JSON endpoint at `/diag`
that returns all active feature flags and configuration constants.

**This endpoint exposes build configuration details that could assist an
attacker in fingerprinting the firmware and calculating buffer sizes.** It is
disabled by default (`PC_ENABLE_DIAG = 0`). Do not enable it in production.

If enabled during development, protect it with HTTP Basic Auth:

```cpp
server.on("/diag", HTTP_GET, [](uint8_t slot, HttpReq *req) {
    server.diag(slot);
}, "Admin", "admin", "secretpassword");
```

---

## 9. Known Limitations and Non-Goals {#known-limitations}

| Limitation                                      | Impact                                                | Workaround                                                              |
| ----------------------------------------------- | ----------------------------------------------------- | ----------------------------------------------------------------------- |
| No SSH rekeying                                 | Connection closed at seq ≈ 2^32 (≈ 4 billion packets) | Reconnect                                                               |
| No SSH user authentication                      | Any client that completes KEX is accepted             | Add [`SSH_MSG_USERAUTH_REQUEST`](@ref SSH_MSG_USERAUTH_REQUEST) handler |
| HTTP Basic Auth is not constant-time            | Timing oracle risk if measurable from network         | Use TLS or SSH tunnel                                                   |
| Software AES/SHA paths are not constant-time    | Test-only paths; not relevant in production           | Production uses hardware AES (mbedTLS)                                  |
| Outbound HTTPS client is encrypt-only by default | Without a CA/pin the client does not authenticate the peer (active MITM exposed) | Install a CA (`http_client_set_ca`) and/or SHA-256 cert pin (`http_client_set_pin`); treat secrets as MITM-exposed otherwise |
| No HSTS, CSP, or other HTTP security headers    | Application must add headers manually                 | Call [`send()`](@ref PC::send) with appropriate headers       |
| WebSocket Origin not validated                  | Cross-origin WebSocket requests accepted              | Check Origin in ws_connect handler                                      |
| NVS not encrypted by default                    | RSA private key readable from flash                   | Enable `CONFIG_NVS_ENCRYPTION`                                          |
| SNMP v1/v2c community is cleartext              | Anyone who can sniff UDP/161 learns the community      | Trusted/management VLAN; rename communities; enable SNMPv3 USM authPriv (`PC_ENABLE_SNMP_V3`), or tunnel |

---

## 10. SNMP Agent Security {#snmp-security}

The optional SNMP agent ([`PC_ENABLE_SNMP`](@ref PC_ENABLE_SNMP), default
off) implements **SNMP v1 and v2c**, whose security model is the community
string - effectively a shared password sent **in the clear** in every datagram.
It is convenient for monitoring on a trusted network but is **not** an
authentication or confidentiality mechanism.

For authenticated and encrypted access, enable **SNMPv3 / USM**
([`PC_ENABLE_SNMP_V3`](@ref PC_ENABLE_SNMP_V3), default off): a single
authPriv user with `usmHMAC192SHA256` authentication (HMAC-SHA-256) and
`usmAesCfb128` privacy (AES-128-CFB), engine discovery, and the RFC 3414
timeliness window against replay. The agent rejects unauthenticated non-discovery
requests, verifies the HMAC before acting on any byte, and answers unknown
users / wrong digests / out-of-window times with the standard USM Report PDUs.
Keys are derived once from the passwords (RFC 3414 §2.6 localization), so no
password material is recomputed per request. Operational notes: passwords must be
>= 8 characters; persist and increment `engineBoots` in NVS
([`pc_snmp_v3_set_boots()`](@ref pc_snmp_v3_set_boots)) so the timeliness window
survives reboots; give each device a unique engine ID (derive from the MAC).

**What the agent does enforce**

- **Two communities, least-privilege by default.** The read-only community
  (default `public`) authorizes only `Get` / `GetNext` / `GetBulk`. `Set` is
  refused (`noAccess`) unless a distinct read-write community is configured via
  [`pc_snmp_agent_set_rw_community()`](@ref pc_snmp_agent_set_rw_community); if none is
  set, the agent is effectively read-only. Writable objects must additionally
  opt in with a setter callback (others answer `notWritable`).
- **Unknown community → silent drop.** A datagram whose community matches neither
  configured value produces no response (no oracle, no amplification).
- **Zero-heap, bounded work.** Request and response share two fixed BSS buffers
  ([`SNMP_MSG_BUF_SIZE`](@ref SNMP_MSG_BUF_SIZE)); the MIB is a fixed table; the
  BER decoder is fully bounds-checked and a malformed/truncated message is
  dropped. `GetBulk` expansion is clamped to
  [`SNMP_MAX_VARBINDS`](@ref SNMP_MAX_VARBINDS), and an over-large response
  degrades to `tooBig` - so one request yields at most one bounded datagram (no
  unbounded amplification).
- **No reflection vector beyond 1:1.** The agent only ever replies to the source
  of a valid-community request with a single response datagram.

**What it does not provide**

- **No encryption or message authentication (v1/v2c).** The community string and
  all varbind data are visible to anyone who can observe UDP/161. Treat the
  community as a low-value access token, not a secret.
- **No per-source rate limiting.** Mitigate query floods at the network layer
  (firewall the port to your management host/VLAN).

See [RFC.md](RFC.md) for the protocol-conformance details.

---

## 11. Hardening Checklist {#hardening-checklist}

Use this checklist before deploying to a production environment.

### General {#general-hardening}

- [ ] Disable `PC_ENABLE_DIAG` (default is off; confirm `#define PC_ENABLE_DIAG 0`)
- [ ] Set [`CONN_TIMEOUT_MS`](@ref CONN_TIMEOUT_MS) appropriately (default 5000 ms) to limit slow-loris attacks
- [ ] Configure [`MAX_CONNS`](@ref MAX_CONNS) no higher than necessary to limit resource exhaustion
- [ ] Review all route handlers for unchecked user input (query params, body content, header values)
- [ ] Validate the `Origin` header in WebSocket upgrade handlers if the device is accessible from untrusted networks

### Authentication {#auth-hardening}

- [ ] All sensitive routes are protected with Basic Auth at minimum
- [ ] Credentials are in flash (`.rodata`), not computed from user input
- [ ] If using HTTP (not SSH), accept that Basic Auth credentials are transmitted in cleartext (base64 is not encryption)

### SSH {#ssh-hardening}

- [ ] RSA host key is provisioned in NVS (`"ssh_host_key"` / `"priv_der"`)
- [ ] NVS encryption is enabled in ESP-IDF menuconfig (`CONFIG_NVS_ENCRYPTION`)
- [ ] NVS encryption key (`nvs_keys` partition) is stored in eFuse or secure flash
- [ ] SSH listener port is firewalled / restricted to known clients if possible
- [ ] Client host-key verification is enforced by the connecting SSH client (openssh `known_hosts`)
- [ ] [`MAX_SSH_CONNS`](@ref MAX_SSH_CONNS) is set to the minimum required concurrent sessions

### SNMP {#snmp-hardening}

- [ ] SNMP is left disabled unless needed ([`PC_ENABLE_SNMP`](@ref PC_ENABLE_SNMP) default off)
- [ ] Default communities `public` / `private` are renamed to non-guessable values
- [ ] No read-write community is configured unless `Set` is actually required (read-only otherwise)
- [ ] UDP/161 is firewalled to the management host / VLAN (v1/v2c communities are cleartext)
- [ ] For authenticated/encrypted access use SNMPv3 authPriv ([`PC_ENABLE_SNMP_V3`](@ref PC_ENABLE_SNMP_V3)); v1/v2c offer no encryption or real authentication
- [ ] SNMPv3: each device has a unique engine ID and persists/increments `engineBoots` in NVS; auth/priv passwords are >= 8 chars and not the defaults

### Build Hardening {#build-hardening}

- [ ] Build with `-Os` (default in ESP-IDF) - do not disable optimization, as it is not needed for security and `ssh_wipe` is already volatile-correct
- [ ] Confirm the final binary does not include the native test PRNG mock (it is excluded by the `#ifdef ARDUINO` guard in production builds)
- [ ] Run `pio test -e native_ssh` to verify all SSH crypto test vectors pass before deployment
