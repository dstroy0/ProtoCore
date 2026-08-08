# Known limitations

Deliberate constraints and caveats of ProtoCore. Most stem
from the zero-heap / fixed-buffer / single-loop design and are intentional; where
a limit is configurable, the relevant `*_SIZE` / `*_MAX` macro is named. Work to
lift some of these is tracked in [ROADMAP.md](ROADMAP.md).

## Platform

- **ESP32 only.** No ESP8266 / RP2040 / RP2350 port - the HW crypto, NVS, and
  lwIP raw-API integration are ESP32-specific. Host builds exist only for tests.
- **Interfaces.** Wi-Fi STA/AP and, with `PC_ENABLE_ETHERNET`, a wired Ethernet PHY
  (`Physical.eth->init()`; the PHY bring-up needs the hardware) - either an RMII part (LAN8720,
  needs a built-in MAC) or a W5500 over SPI (`PC_ETH_W5500`, HW-verified on the S3, which has
  no RMII MAC). W5500 throughput is SPI-bound (~7-8 Mbit/s, not the 100 Mbit PHY line rate; see
  FEATURE_PERFORMANCE). Dual-stack IPv6 is opt-in via `PC_ENABLE_IPV6` (SLAAC; the listeners
  bind `IPADDR_TYPE_ANY`), off by default.

## HTTP core

- **Fixed request limits:** `MAX_HEADERS` headers; `MAX_KEY_LEN` / `MAX_VAL_LEN`
  per header (over-long keys/values are truncated, not rejected - except the
  `Authorization` value, which has its own larger buffer); `MAX_PATH_LEN` path;
  `BODY_BUF_SIZE` body (larger gets 413 unless a streaming sink is installed).
- **No `Date` response header by default** (a clock-less device; RFC 7231
  §7.1.1.2). With `PC_ENABLE_NTP` an app can add it from a handler.

## WebSocket

- A reassembled message must fit `WS_FRAME_SIZE` (else Close 1009).
- **permessage-deflate is bidirectional** (RFC 7692, `PC_ENABLE_WS_DEFLATE`): the
  server decompresses inbound compressed messages and compresses outbound data frames
  with bounded fixed-Huffman DEFLATE (RSV1), falling back to uncompressed when the result
  would not shrink. No other extensions; RSV2/RSV3 are always rejected.

## JSON

- The reader decodes `\uXXXX` escapes to UTF-8 (1-4 bytes), joining UTF-16
  surrogate pairs into astral code points; an unpaired surrogate becomes U+FFFD
  and malformed/short hex becomes `?`. A code point whose UTF-8 sequence would not
  fit the caller buffer truncates the string before it rather than writing a
  partial character.
- `JsonWriter` is bounded by the caller buffer + `JSON_MAX_DEPTH`; overflow flips
  `ok()` and truncates rather than growing.

## Authentication

- **HTTP Digest:** no `nc` (nonce-count) replay tracking - a single shared server
  nonce suits this 1-2 client device class; the per-`begin()` nonce rotation
  bounds the replay window.

## TLS

- **Concurrent TLS (`MAX_TLS_CONNS` > 1) needs the arena to fit ~122 KB of static
  DRAM, not the 320 KB PlatformIO prints.** The whole mbedTLS working set (the
  16 KB-in + 16 KB-out record buffers dominate, ~41.5 KB/connection) is served from a
  single static `.bss` arena (`PC_TLS_ARENA_SIZE`, 48 KB default). The real ceiling
  is the ESP32 `dram0_0_seg` linker region - only **~122 KB** (`0x2c200 − 0xdb5c`),
  ROM-reserved at both ends; the `RAM: … from 327680 bytes` PlatformIO reports is a
  misleading aggregate that counts heap-only DRAM. So a 48 KB arena already uses most
  of the region and a second connection overflows the link (`region 'dram0_0_seg'
overflowed by 34048 bytes`). A build guard now turns that cryptic linker error into a
  clear message; pick one of three paths and set `PC_TLS_ACK_MULTI_CONN_DRAM=1` (the
  PSRAM path sets the guard on its own):
    1. **PSRAM board (recommended).** Set `PC_TLS_ARENA_IN_PSRAM=1` to place the arena
       in external RAM (`EXT_RAM_BSS_ATTR`, zero heap), freeing the whole arena back off
       internal DRAM so many connections fit. This needs a framework built with
       `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`, which the **stock** precompiled
       arduino-esp32 (verified 2.0.x and 3.3.x, PlatformIO and arduino-cli) ships **OFF** - so
       `EXT_RAM_BSS_ATTR` there silently no-ops and the array stays in internal RAM. The library
       now **fails the compile** in that case rather than losing the offload silently; you must
       **rebuild the core** with the flag on. Full hand-held recipe (PlatformIO pioarduino
       `custom_sdkconfig`, or the `esp32-arduino-lib-builder` script for Arduino IDE / arduino-cli,
       plus the octal-vs-quad PSRAM-mode gotcha and how to verify): **`tools/psram/README.md`**.
       Verified end to end on an ESP32-S3 N16R8: with a rebuilt (flag-enabled) core, an
       `EXT_RAM_BSS_ATTR` array lands at `0x3C0xxxxx` (PSRAM, `esp_ptr_external_ram()=1`), the
       board boots octal with no watchdog loop, and internal DRAM use drops sharply - zero heap.
       A full `MAX_TLS_CONNS=2` build (128 KB arena) then links using **56 KB** internal DRAM
       with the arena in PSRAM versus **187 KB** with it in DRAM, boots, and `begin_tls()` runs
       with a flat heap. Note the ~122 KB `dram0_0_seg` ceiling above is the **classic ESP32**;
       the S3's data segment is much larger, so on the S3 two connections fit in DRAM as well and
       PSRAM buys headroom rather than being strictly required - it is required only where the
       segment is tight (the classic ESP32).
        - **Flash-cache / OTA caveat (know this before choosing PSRAM).** PSRAM is on the flash
          cache bus, so while flash is being written (an NVS commit, an OTA update) PSRAM is
          momentarily unreadable, and TLS code that touches the arena during that window faults
          with an illegal-cache-access. The arena is a single pool, so this is a whole-build
          choice: put the arena in PSRAM (`PC_TLS_ARENA_IN_PSRAM=1`) for a TLS workload that
          does **not** write flash while serving (pure proxy/forwarding, or a device that only
          persists config at boot / while idle), and keep it in internal DRAM (the default, with
          `PC_TLS_ACK_MULTI_CONN_DRAM=1` if you accept the ~1-2 connection ceiling) for a
          workload that does OTA / NVS / file-serving **concurrently** with live TLS. In
          practice TLS request traffic and flash writes rarely overlap, but if yours does, keep
          the arena in DRAM. (Per-slot arena placement was considered and deliberately not built:
          it would route each connection's allocations through mbedTLS's context-free global
          allocator, a lot of moving parts for a narrow case - the whole-build choice above is
          the intended design.)
    2. **Shrink the records (custom ESP-IDF build).** Lower
       `CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN` so each connection's arena cost drops, and
       set `PC_TLS_MAX_FRAG_LEN` (512/1024/2048/4096) to negotiate the smaller record on
       the wire (RFC 6066). The precompiled Arduino mbedTLS fixes the record size, so the
       memory win needs an ESP-IDF / pioarduino build; MFL still caps on-wire records on any
       build.
    3. **Reclaim internal DRAM (advanced).** Growing `dram0_0_seg` past ~122 KB via a
       `memory.ld` override is possible but risky: on the stock arduino-esp32 the base
       `0xdb5c` and the top cap are **ROM-reserved** (this is already the "BT not built"
       memory.ld variant), so a naive reclaim can corrupt boot. Prefer paths 1-2.
- **Session resumption** (RFC 5077 tickets): both the server and the outbound client
  resume - the client accepts the server-issued ticket and presents it on the next
  `begin()` for an abbreviated handshake.

## SSH

- **Single `session` channel** - no X11 or channel multiplexing (a `tcpip-forward`
  global-request seam exists for `ssh -R`, but full port-forwarding is not wired).
- Per-direction NEWKEYS is a single flag (correct for the strict
  send-then-receive ordering), and the KDF produces a single <= 32-byte block
  (enough for the negotiated AES-256 / HMAC-SHA-256). Time- and packet-count-based
  in-session rekeying is supported (`SSH_REKEY_TIME_MS`, server-initiated).

## Protocol services

- **CoAP:** piggybacked responses only - no separate (deferred) responses, no CON
  retransmission / de-duplication. (`/.well-known/core` resource discovery, RFC 6690,
  is supported.)
- **WebDAV:** `PROPPATCH` returns a 207 with every property refused (403). `LOCK` /
  `UNLOCK` are enforced by a fixed lock table (`PC_DAV_LOCK_MAX` concurrent locks): a
  write to a locked resource without the matching token in its `If` header is
  refused 423. Each lock has a timeout (Second-3600) and is swept once it expires;
  a LOCK with the token in its `If` header refreshes it. Locks are not persisted
  across a reboot. `PUT` streams to the file as the body arrives, and `COPY`/`MOVE`
  handle both files and collections (recursive).
- **SNMP:** the v3 engine ID defaults to a placeholder enterprise OID; pass your own
  to `pc_snmp_v3_init()`. (Trap and the confirmed _inform_ are implemented for both v2c
  and v3; the caller drives inform retransmission until the receiver's Response arrives.)
- **Telnet** is plaintext - no auth or encryption; use it only on a trusted LAN
  (prefer SSH or the WebSocket terminal otherwise).
- **Multipart:** at most `MAX_MULTIPART_PARTS` parts; only `name` / `filename` are
  extracted. Parsing is length-bounded over `body_len` and matches the full
  `CRLF--boundary` delimiter, so a **binary** part (embedded NUL bytes, or the raw
  boundary string inside the payload) is preserved intact - read it via
  `part->data` + `part->data_len`.

## DMA ingest

- **The shipped backend is the ingress/egress simulator, not a silicon DMA driver.**
  `services/dma` (`PC_ENABLE_DMA`) is the full deterministic ingest pipeline -
  channels, ping-pong RX, TX egress, completion events, fail-closed - and with
  `PC_DMA_SIMULATE=1` (the default) it runs end to end through an in-memory model of
  the peripheral (feed bytes in, capture bytes out, optional TX->RX loopback), on the host
  bench and on-device. A real UART UHCI / `spi_master` DMA driver is **not implemented
  yet**; it plugs into the `pc_dma_hw_*` hooks when `PC_DMA_SIMULATE=0` (the default
  hooks fail closed - `pc_dma_open()` returns false - until a driver overrides them). The
  silicon backend is deferred because it needs peripheral hardware (and a real loopback) to
  verify, and shipping an unverified driver is against the project's test-on-hardware rule.
- **RX buffers are 2-deep ping-pong.** The event's `data` pointer is valid only until the
  buffer is reused a transfer or two later. A consumer that drains the completion in another
  task (e.g. off the preempting queue) must **copy the bytes** into the posted item, not
  keep the pointer - see the note in `dma.h` and example `DmaIngest`.

## Interface forwarding

- **The forwarding plane is transport-agnostic.** `services/forward` (`PC_ENABLE_FORWARD`)
  owns the rules, rate caps, default-deny, and dispatch, but it moves bytes only through the
  send callbacks **you** register for each interface and the `pc_forward_ingress()` calls
  **you** drive from each interface's RX. The library does not itself bind those to Wi-Fi /
  Ethernet / a bus / a radio - that wiring (and the "true zero-copy DMA descriptor reuse" the
  roadmap describes) is the integration's, and depends on the real silicon DMA backend
  (above). It forwards whole frames handed to it; it does not itself parse protocol headers.
- **The ingress ACL and policy routes are stateless byte-pattern matchers.** Both match raw
  bytes at a fixed offset under a mask (up to `PC_FWD_ACL_PATLEN` bytes), first-match-wins.
  The ACL allows/denies at ingress; a **policy route** (`pc_forward_route_add()`) binds a match
  to a chosen egress interface (path selection by a header field at a known offset - EtherType,
  IP protocol, a port, an address prefix), taking precedence over the src->dst rules. Neither
  tracks flows or does longest-prefix / CIDR matching; they are fast fixed-offset gates, not a
  stateful firewall or a full routing table.

## Radio gateway

- **The gateway is the generic framework, not a radio driver.** `services/gateway`
  (`PC_ENABLE_GATEWAY`) owns ports, the northbound envelope + topic, the up/down-link
  routing, the rate cap, and stats, but the radio's frame format (its codec) and its wire
  access - the SPI / I2C / UART register reads that produce a frame and the transmit that
  sends one - are **your** callbacks, as is the northbound publish (wire it to the MQTT /
  HTTP / WebSocket client). The specific radios (LoRa, Zigbee, nRF24, ...) are per-module
  codecs + drivers on the roadmap, deferred until their hardware is on hand to verify
  against. The gateway carries whole frames with a 16-bit node address; it does not itself
  do mesh routing, retransmission, or LoRaWAN / Zigbee session state.
- **A shipped radio driver's register protocol is host-verified; its RF link is not.** The
  LoRa SX127x driver (`services/lora`, `PC_ENABLE_LORA`) is tested against a mock chip
  (register file + FIFO), which proves the init / send / receive register sequence matches
  the datasheet, and it compiles for the ESP32 - but whether it actually keys the radio and
  demodulates a frame over the air is only confirmable with the module. Treat a radio driver
  as "protocol-correct, RF-unverified" until it has been run against real hardware. It also
  implements a single modem config (explicit header, CRC on) and the base modulation, not
  the chip's every feature (FHSS, CAD, the SX126x command interface).

## Streaming sinks

- **OTA and streaming upload share the single parser streaming hook** - enable at
  most one of `PC_ENABLE_OTA` / `PC_ENABLE_UPLOAD` per build.
