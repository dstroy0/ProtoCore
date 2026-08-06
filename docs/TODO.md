# TODO / Known Fixes

Outstanding work and known limitations, roughly highest-impact first. Items are
grouped by area; each names the file(s) involved so the fix is easy to locate.

> **Status:** Security/correctness, the ESP32 build blocker, SSH
> `UNIMPLEMENTED`, housekeeping, the DX feature set (`serve_static` + MIME + gzip,
> [`redirect()`](@ref PC::redirect), named [`begin()`](@ref PC::begin) codes), the **HW-crypto performance** items
> (streaming SHA-256 + AES-CTR whole-buffer, **verified on a DevKitV1**), and the
> **optional services** (mDNS, OTA, WiFi provisioning/captive portal, SNTP, ETag,
> runtime stats, access-log hook) are all **done** - host-tested where possible
> and ESP32-firmware-linked. Items marked `[x]` carry a _(done)_ note.
>
> **Since v2.0.0** (all opt-in, default off, host-tested where possible +
> HW-verified): HTTPS/**TLS** server, **mTLS**, and **TLS session resumption**
> (RFC 5077 tickets); outbound **MQTT** (3.1.1) and **WebSocket** clients, each
> also over TLS; **SNMP** traps/informs (v2c + v3); **CoAP** resource Observe
> (RFC 7641) and block-wise transfer (RFC 7959); a **per-IP accept throttle**;
> **WebDAV** (RFC 4918); and a **Modbus TCP** slave. Plus an architecture pass
> (pluggable protocol-handler dispatch, flow-control primitives) and the
> `src/web` asset-generator pipeline.
>
> Optional services use only the base SDK + lwIP + mbedTLS (no add-on Arduino
> libraries): mDNS via the ESP-IDF `mdns` component, the captive-portal DNS via a
> raw lwIP UDP socket. Each is gated by a `PC_ENABLE_*` flag (default off).
>
> **Still deferred (YAGNI / large):** an Ethernet PHY abstraction (an
> architectural track); concurrent TLS connections
> (`MAX_TLS_CONNS` > 1 needs a smaller-record ESP-IDF build); SSH
> multiplexing, per-direction
> NEWKEYS, and the KDF `K1‖K2…` extension (no current use case); moving
> `ssh_pkt_recv`'s ~2 KB scratch off the stack. Full runtime verification of the
> WiFi-dependent services (mDNS resolve, NTP sync, OTA upload, portal join) needs
> WiFi credentials / a phone.

## Status at a glance

A fast index by status so the next actionable item is obvious without scanning the whole file.
Details for each live in the sections below (search the bolded name). Statuses: **OPEN** (actionable
now) - **PARTIAL** (`[~]`, shipped in part, a remainder is noted) - **WON'T / BLOCKED** (deliberate
non-goal or needs hardware / proprietary docs) - **DONE** (`[x]`, the shipped record below).

### OPEN (actionable now)

- **Multi-vendor portability** (architectural track, greenlit) - partition the three silicon-specific layers
  (board profiles, crypto accelerator HAL, physical MAC+PHY) into per-vendor subdirs (`esp/`, `stm/`, `rp/`,
  `ti/`) under common API headers, with a `pc_platform.h` selector so the preprocessor pulls exactly one
  backend per build. First step is a **zero-behavior-change extraction** of today's ESP backends into `esp/`
  plus the selector; then an STM32 backend (PKA/CRYP/HASH crypto + ETH MAC/PHY). Includes the library rename
  (drop "esp" -> `DeterministicAsyncWebServer`; the `pc_`/`PC_` prefix is already vendor-neutral). Full
  design in [ROADMAP.md](ROADMAP.md#multi-vendor-portability-esp--stm--rp--ti-).
- **Cryptobench harness: put the S3 env on the 3.x core** - `penetration_testing/rig_firmware` `rig_s3*` builds with
  `platform = espressif32@6.13.0` (Arduino-ESP32 **2.x**) while the P4/C6 benches use arduino-cli **3.3.10**
  (3.x). Cross-die crypto numbers therefore compare two different mbedtls builds (2.x S3 has no HW ECC/GCM);
  rebuild S3 on the 3.x core (or bench all dies via one toolchain) so the sweep is apples-to-apples. Surfaced
  by the 2026-07-26 rig bench; see the PC polling-mode HW modexp item in ROADMAP.md for the perf follow-up.
- **CDN caching tier** - the RAM-tier reverse-proxy edge cache + cache key / invalidation / purge, the
  **SD (L2) persistence tier** (`PC_ENABLE_DBM`), **Range/`206`-from-cache** (`PC_ENABLE_RANGE`),
  **`https://` origins** (`PC_ENABLE_EDGE_ORIGIN_TLS`), and **cross-device mesh distribution**
  (`PC_ENABLE_EDGE_MESH`: sibling-cache pull over `PROTO_MESH`, RFC 9111 age propagation, one-hop loop-free)
  are all shipped (`PC_ENABLE_EDGE_CACHE`, services/web/edge_cache, examples EdgeCache + MeshCache). The roadmap tier is
  complete; remaining are lower-priority follow-ups: a TLS sibling link + UDP-broadcast peer discovery + push
  replication for the mesh, and (TLS origins) an async handshake so a cold https MISS never blocks the worker +
  a multi-session pool for >1 concurrent TLS fetch.
- **More niche pure codecs** from old manuals (as they are named / requested - not invented).
- **Docs** - chart the docs build + automation; full staleness audit; theming (palette/alignment);
  a teacher switch to disable the sandbox; live theme preview.
- **Ongoing quality** - extend the pentest fuzz corpus; extend test coverage; raise branch coverage
  (ftp + httpcache raised to ~99% branches, v5.100.3; sweep remaining codecs next); remediate any
  Sonar/CodeQL (currently 0 bugs / 0 vulns).
- **Interop harness -> entire feature set** - the HW interop harness currently proves 7 protocol
  families against real third-party peers; expand it to exercise EVERY shipped feature/protocol against
  a real reference implementation (per-feature interop driver + a real peer/broker), so every codec is
  proven on the wire, not just host-mocked. Detail under **Test coverage**.
- **Per-variant crypto HW acceleration (ESP32-P4 / ESP32-C3)** - the crypto fast paths are S3-tuned
  (RSA/MPI MODMULT for curve25519 / Ed25519 / P-256, PIE-vector `ssh_gf` fallback). Extend them to the
  other variants against each chip's TRM (peripherals differ - verify capability flags, do not assume):
  - **ESP32-P4** - has a dedicated **ECC accelerator** (hardware point multiply for NIST P-192 / P-256)
    and an **ECDSA** peripheral: route `ssh_ecdsa` P-256 sign / verify / ECDH through the ECC HW (faster
    than the MODMULT emulation, and moves the constant-time guarantee into silicon). The P4 also has
    **hardware AES-GCM** (`SOC_AES_SUPPORT_GCM`), which would replace the software 4-bit-table GHASH and
    lift the AES-256-GCM record-layer ceiling. curve25519 / Ed25519 stay on the MPI MODMULT (the ECC HW
    is NIST-prime only, not the 2^255-19 field).
  - **ESP32-C3** - RISC-V single core with an RSA/MPI accelerator (to 3072-bit) but **no** ECC block, PIE
    vector, or HW-GCM: gate the S3 MODMULT paths (`PC_FE25519_MPI_HW`, `PC_ECDSA_MPI_HW`) on the C3
    too after confirming its `soc/hwcrypto_reg.h` MODMULT register sequence matches; GHASH stays the 4-bit
    table, the `ssh_gf` SIMD path is unavailable. Consider the DS peripheral for eFuse-backed RSA.
  - Each is HW-gated + KAT'd on the real chip (rig KAT self-check pattern), like the S3 work.

### PARTIAL (`[~]` - shipped, remainder noted)

- **Port forwarding / DNAT** - complete + **HW-verified (2026-07-13)**: the `pc_relay` bidirectional
  TCP byte pump (backpressure + independent half-close, host-tested, `native_relay` 6 cases) **plus the
  `PROTO_RELAY` server-side listener** (`server.listen(port, PROTO_RELAY)` + `pc_relay_publish(...)`
  dials the origin via `pc_client` and pumps the relay from the server poll loop) **plus a runnable
  example** (`PortForward`). On real HW (ESP32-S3 over a W5500 wired link) an inbound->origin round
  trip is **byte-exact** - a 1 MB file pulled through the board's front port matched the origin's SHA256.
  Throughput ~44 KB/s: a single-NIC relay crosses the one W5500 twice per byte and is round-trip-latency
  bound (the design point is exposing a service / small control files, not bulk throughput).
- **Ethernet DNC** - the transport-agnostic `dnc_stream` drip-feed engine + a runnable example
  (`EthernetDnc`) are shipped (leader / `%` markers / blocks over a send/recv seam, XOFF/XON pacing;
  `native_dnc` 22 cases) and **HW-verified (2026-07-13)** over a W5500 link to a TCP sink: the drip is
  **byte-exact** (108-byte framing - 16 NUL leader, `%`CRLF, CR-before-LF blocks, `%`CRLF, 16 NUL
  trailer - matched the computed reference), and the **XON/XOFF pacing** path works (0 bytes sent during
  a 3 s XOFF hold, then the full identical program after XON).
- **SMB/CIFS client** - complete and shipped: all 8 codec increments + the `smb_client` engine
  (`smb_open` / `smb_read` / `smb_write` / `smb_close`, a POSIX-like surface) + a runnable example
  (`SmbFileClient`, ESP32-compile-verified). **SMB 3.1.1 now runs end to end:** the client offers
  2.0.2 .. 3.1.1, chains the preauth-integrity hash across NEGOTIATE + both SESSION_SETUP rounds,
  derives the SP800-108 signing key, and signs the session with AES-128-CMAC (`crypto/aes_cmac`,
  RFC 4493); the KDF label/context assembly + the CMAC are cross-checked byte-for-byte against
  impacket (validated against real Windows). The **NTLMSSP AUTHENTICATE MIC** is sent too
  (`pc_ntlm_mic` = HMAC-MD5 over NEGOTIATE‖CHALLENGE‖AUTHENTICATE, with the MsvAvFlags MIC bit set).
  Host-tested end to end with a scripted mock SMB2 server that is a full 3.1.1 reference peer - the same
  preauth chain + derived key on both sides, a byte-exact signed write/read round trip, and CMAC tamper
  rejection (`native_smb`). **HW-verified (2026-07-25) against a real Samba 4.19** forced to `SMB3_11` +
  `server signing = mandatory`: the `smb_client` engine over a live socket authenticated, negotiated
  `algo=AES-CMAC`, wrote a CMAC-signed file Samba accepted, and read it back byte-exact - and a
  deliberately corrupted MIC made Samba reject the logon (`STATUS_LOGON_FAILURE`), proving the server
  verifies our MIC. **SMB 3.x transport encryption is now shipped too:** all four SMB 3.1.1 ciphers -
  AES-128/256-GCM (`crypto/aes128gcm`, `crypto/aesgcm`) and AES-128/256-CCM (the new `crypto/aesccm`,
  SP800-38C) - wired into the §2.2.41 TRANSFORM_HEADER codec + the §2.2.3.1.2 ENCRYPTION_CAPABILITIES
  negotiate + SP800-108 cipher-key derivation, plus **client-forced encryption** (`SmbConfig.encrypt`) so the
  client can reach a share whose server requires encryption (which denies the unencrypted TREE_CONNECT before
  it can advertise the share flag). KAT'd vs pyca/cryptography and **HW-verified (2026-07-27) against a real
  Samba `smb encrypt = required` share**, each of the four ciphers reading the file byte-exact. **Done.**
- **Concurrent TLS** (`MAX_TLS_CONNS`>1) - library + PSRAM build done; only the live 2-client soak remains
  (the reserved **two-rig HW test**, held per the user's "keep looping, hold the rigs").
- **Ethernet PHY** - RMII bring-up shipped + **HW-verified (2026-07-19)** on a Waveshare ESP32-P4-POE-ETH
  (onboard IP101 PHY): the shipped `init_eth_physical()` serves real HTTP over pure wired Ethernet. See the `[x]` entry below.
- **CoAP scope** - `/.well-known/core` discovery shipped; separate/deferred responses + CON dedup are
  deliberately out of scope.
- **SSH channels** - `direct-tcpip` (`ssh -L`) **and** `forwarded-tcpip` (`ssh -R`) both shipped +
  **HW-verified**. Local forward host-tested (`test_ssh_channel`) + HW-verified; the remote forward is the
  `ssh_forward` owner + `pc_ssh_conn_open_forwarded` glue driven by the `tcpip-forward` global request, and
  the **device-as-server `ssh -R`** is now HW-verified on an ESP32-P4 (2026-07-21): a real OpenSSH
  `ssh -R 8080:localhost:9091` made the device listen on `:8080`, and a connection to it tunneled a
  **byte-exact reverse round trip** back to the client's target. X11 forwarding is a deliberate non-goal
  (no X11 clients run on an embedded server).

### WON'T / BLOCKED (needs hardware, proprietary docs, or a closed decision)

- **Portability beyond ESP32** (ESP8266 / RP2040 / RP2350) - closed won't-do (per the user).
- **Radio-as-a-plugin** - needs bare-metal RF hardware.
- **Fanuc FOCAS** - proprietary wire format; needs manuals + a real controller.

### DONE (recent, this loop)

W5500 SPI Ethernet driver (HW-verified on an ESP32-S3), DNC codec, FTP client, HTTP cache-control
helpers, forwarding policy-routing + inspection hook, Ed25519-sign KAT (RFC 8032), a class of
signed-overflow UB + `10^exp` DoS fixes in the number parsers, multipart binary-safety. Full shipped
record: the `[x]` items and the collapsed sections below.

## From the working thread

Ideas and intentions captured from the [Working Thread discussion](https://github.com/dstroy0/ProtoCore/discussions/15) (the maintainer's thought-stream). Unpolished on purpose; refine as they are picked up.

### Refactor / security hardening

- [x] Owner-refactor endgame _(done)_: remove the remaining globals now that the core is integrated, and switch to known security patterns for owners. The mutable global pointers that supported a speedy implementation are no longer necessary. Every file-scope mutable in `src/` now lives in one feature-gated owned `<Name>Ctx` (least-privilege / object-capability), enforced by `ci_tooling/check/check_owned_context.py` (wired into CI) so any new loose mutable fails the build; the guard passes clean on `main`. The allow-list is the documented `extern` cross-TU substrate (the shared connection pools + SSH session state).
- [ ] Remediate SonarQube code smells and CodeQL gripes. _(SonarCloud: 0 bugs / 0 vulnerabilities project-wide; remaining are style-conflict code smells to disable in the quality profile, not churn.)_

### Test coverage

- [ ] Keep extending test coverage. Provably-dead lines get `GCOVR_EXCL` with rationale inline; be conservative with exclusions (even a pointless test teaches) - guarded dead branches / unreachable guards are the main exclusion targets.
- [ ] Raise condition (branch) coverage - line coverage is good, conditional lags. Test both sides of each condition.
- [ ] **Expand the interop harness to the entire feature set.** The HW interop harness proves 7 protocol families byte-for-byte against real third-party peers (COM3 device + host peer / public broker; see the interop test-setup notes). Extend it so EVERY shipped feature/protocol has an interop driver that round-trips against a real reference implementation (e.g. each MQTT/CoAP/SNMP/Modbus/SMB/SSH/HTTP-client/industrial-bus codec talks to its canonical server/client), not just the host mock-seam tests - host tests miss whole-firmware integration bugs (the SMB/DNC/port-forward HW runs each found real ones). Build out: a per-feature peer (real daemon or a public broker), a harness driver that drives the device against it, and a pass/fail byte-compare; wire into the interop workflow so new features must ship an interop case.

### Codecs / drivers / features

- [ ] Write more niche pure codecs from old manuals - the ones wanted a while ago but never put down on paper.
- [x] W5500 SPI Ethernet driver, to support non-RMII ESP32s like the S3. _(done, HW-verified on an ESP32-S3 + WIZnet W5500 over HSPI: raw VERSIONR=0x04, then DHCP IP + HTTP served over the wired link. Pins CS7/SCK12/MISO13/MOSI11/RST6/INT5; needs arduino-esp32 3.x + PSRAM=opi on an N16R8. **v5.101.0:** streamed 200 MB byte-exact with a flat heap; added `PC_ETH_W5500_SPI_MHZ` (default 20) + a clock-vs-throughput sweep - SPI-bound ~7.2 Mbit/s @20 MHz, ~8.2 @24, plateau ~8.3 @30; reliable-sustained ~24 MHz on breadboard wiring. The earlier "large-transfer crash" was marginal SPI signal integrity, not a library defect - see BUGS.md.)_
- [ ] Radio-as-a-plugin: strip an ESP to bare metal and do (very legal) radio things as a server plugin.
- [x] `Industrial_ESPIDF/`: create the directory and write the CMake for it. _(done: `examples/esp-idf/Industrial_ESPIDF` - an industrial edge gateway (HTTP dashboard + Modbus TCP slave + SNMP agent) built with `idf.py`. The top `CMakeLists.txt` turns the feature flags on for the whole build with `add_compile_definitions(PC_ENABLE_MODBUS=1 PC_ENABLE_SNMP=1)` before `project()` - the ESP-IDF equivalent of build_opt.h. esp32dev compile+link verified (Flash 58%, RAM 29%).)_

### Embedded data stores (SD card)

An on-device data-store stack. The user is attaching an **SD card** and wants an atomic buffer-to-flash
layer built first, then the store codecs on top. Substrate before stores.

- [x] **Atomic buffer-to-flash layer (the substrate).** _(done)_ A power-loss-safe write path over the
      FS / SD: a write-ahead log with a CRC per record and an A/B superblock commit marker, so an
      interrupted write is discarded (never half-applied) on the next mount. Zero-heap, bounded, static
      buffers. `PC_ENABLE_WAL` = the pure record codec (`services/storage/wal/wal.h`) + the durable store
      (`pc_wal_store.h`: A/B superblock + `pc_wal_store_checkpoint` + mount/tail-replay over a `WalDev`
      block-device seam) + the `fs::FS` binding (`pc_wal_fs.h`, preallocated file, random-access over
      `File::seek`/`flush`). Host-tested (13 cases: CRC vector, torn/truncated-tail recovery, checkpoint
      durability, superblock fallback) **and hardware-verified on an SD card over SPI** (checkpoint
      recovery, torn-tail drop, byte-level payload persistence, and survival across a chip reset all
      pass). _Follow-up:_ a ring-wrap (currently a linear log; wrapping needs a per-record epoch to
      disambiguate stale records); a WAL write-path throughput line in Performance.
- [x] **dbm**: a hash key-value store on the flash layer. _(done)_ `PC_ENABLE_DBM` - a Bitcask-style
      log-structured store: put/delete append one WAL record (fast sequential writes), an in-RAM
      open-addressed hash index (fixed BSS, no heap) maps each live key to its value in the log, and
      open rebuilds the index by replaying the log. **Log compaction now reclaims dead space**
      (`pc_dbm_compact` merges only the live keys - latest value each, no tombstones - into a freshly
      formatted destination store, checkpoints it, then rebinds the handle and rebuilds the index; fails
      closed leaving the original log intact on any I/O error so no data is lost; `pc_dbm_live_bytes`
      pairs with `pc_wal_store_used` to measure the dead fraction and decide when to compact). Host-tested over
      a RAM device (11 cases: overwrite, tombstone resurrection, persistence across remount with/without
      checkpoint, collisions, index-full fail-closed, bounds, max-value round-trip, compaction reclaims
      space + preserves the live set, compaction fails closed on a too-small destination) and **HW-verified
      on an ESP32-S3** (a churned 1632-byte log compacted to 69 bytes - exactly the live records - with every
      value intact).
- [x] **sqlite**: SQLite3 **on-disk file-format** access (the documented page / b-tree / record
      encoding) - reader + bounded writer, both done. Not the full SQLite amalgamation (heap + stdio,
      incompatible with the no-stdlib zero-heap model). `PC_ENABLE_SQLITE` - **reading is complete**
      (`services/storage/sqlite/pc_sqlite_format.h`): database header, b-tree page header, cell pointers, the
      leaf-table cell (rowid + payload + overflow detection), a record cursor (header varints -> typed
      column values), int/float decoders, and a **multi-page table cursor** that walks an interior
      b-tree over `rootpage` in rowid order with a bounded descent stack + two page buffers (works over
      any page source via a reader callback: RAM, pc_wal_fs, fs::FS). **Overflow-page chains are now
      followed** (`pc_sqlite_read_payload` reassembles a record across its linked overflow pages - 4-byte
      next-page pointer + content per page - into a caller buffer with a bounded page count and a
      fail-closed capacity guard; the table cursor transparently reassembles an overflowing row when given
      an overflow buffer via `pc_sqlite_table_cursor_set_overflow_buf`, else it yields the in-page prefix as
      before). Host-tested against real sqlite3-CLI files (14 cases): the `pc_sqlite_schema` row
      column-by-column, a full scan of a 40-row 2-level b-tree, and byte-exact reassembly of 1000- and
      3000-byte TEXT columns spanning multi-page overflow chains (+ a short-buffer fail-closed case), and
      **HW-verified on an ESP32-S3** (interior-root descent + multi-page reassembly + bounds, all byte-exact
      on the Xtensa). A **bounded writer** now completes the item: `pc_sqlite_encode_record` (minimal integer
      serial types, TEXT/BLOB/FLOAT/NULL) + `pc_sqlite_varint_encode` + `pc_sqlite_build_table_db`, which emits a
      fresh two-page single-table database (page 1 = header + the `pc_sqlite_schema` row, page 2 = the table's
      leaf b-tree) straight into a caller buffer, zero-heap, failing closed if a row would overflow a page or
      the rows do not fit one leaf (no page splitting / overflow pages / interior maintenance - a multi-page
      writer is the follow-up). Host-tested (18 cases total: varint + record round-trips, a full build read
      back through our own reader, fail-closed bounds) and **cross-checked against the real sqlite3 CLI**
      (`PRAGMA integrity_check` returns `ok` and `SELECT` returns every row) plus **HW-verified byte-exact on
      an ESP32-S3** (build in RAM + read back). **Remaining:** a multi-page writer (page splits) if larger
      datasets are ever needed.
- [x] **nosql (both)**: _(done)_ a NoSQL **wire client** and a **local on-flash store**.
  - [x] **wire client**: a Redis **RESP** codec _(done)_ - `PC_ENABLE_REDIS` (services/iot/redis_resp)
        extended from RESP2 to full RESP2/RESP3 (null / boolean / double / big number / bulk error /
        verbatim / map / set / push); streaming cursor decoder, no heap. Host-tested against Redis spec
        vectors (14 cases) and **verified live against a real redis-server 8.0.2** (SET/GET, nil, DEL,
        RPUSH/LRANGE array, HELLO 3 RESP3 map, INCRBYFLOAT - all pass). MongoDB OP_MSG/BSON is a
        possible later addition.
  - [x] **local store**: a JSON **document store on the WAL** _(done)_ - `PC_ENABLE_DOCSTORE`
        (services/storage/docstore): JSON documents by id via dbm, plus top-level field queries
        (`find_str`/`_int`/`_bool`) over the live docs using the zero-heap JSON reader; distinct from
        dbm's opaque-value KV. Added `pc_dbm_iterate` to power the scan. Host-tested 5 cases (finds,
        persistence + query across a remount, early stop). Zero-heap, bounded.

### Performance

- [x] **Feature performance measurement -> `docs/FEATURE_PERFORMANCE.md`.** _(done)_ Benchmark each feature's hot
      operation(s) to judge real-world viability: a host **ns/op** deterministic baseline plus the
      on-device **ESP32-S3 us/op @ 240 MHz** and throughput (the number that actually matters). Living
      table: feature, operation, host ns/op, ESP32 us/op, notes. **Done so far:** the storage
      characterization (section 1), the base64 / mtconnect codecs (section 2), the **request path** (section
      3: HTTP request parse for GET + POST, JSON encode + decode - `performance_benching/bench_reqpath.cpp` + an on-device
      firmware; finding: the parse -> build-JSON round trip is ~135 us of CPU, far under the network cost, so
      no optimization was warranted), the full **data-store stack** (section 4), and the **chunked
      send-pump framing** (section 3: `performance_benching/server/send_pump`, host + ESP32-S3) - which surfaced a real
      win: `snprintf("%x")` on the per-chunk size line cost ~4.0 us/chunk on-device, replaced by a
      hand-written `pc_hex_u32` (`shared_primitives/hex.h`) for an **~18x** framing speedup (v7.173.0), and
      the **SSH KEX handshake wall-clock** (`docs/FEATURE_PERFORMANCE.md` "SSH KEX handshake wall-clock":
      a guarded `PC_SSH_KEX_BENCH` probe in `ssh_transport.c` + `performance_benching/ssh/ssh_kex_time.py` driving a live
      OpenSSH client, HW-measured on the S3) - **67.9 ms of device compute per `curve25519-sha256` KEX**
      (2 X25519 + one comb ed25519 sign; ~97% crypto, ~2.3 ms machinery), a ~93 ms client-observed floor;
      the measurement also reconciled the section's stale pre-comb "~0.13 s"/85.6 ms-sign figures. The
      **TLS handshake wall-clock** was already covered (§"TLS handshake": ~498 ms full handshake, the curve
      preference 2.05x win, resumption ~54 ms) - independently re-verified from a WSL client (min 489.5 ms)
      and given a committed reproducer (`performance_benching/tls/tls_hs_time.py`). All sub-items are now measured; nothing
      outstanding.
- [x] **base64 was slow on-device (mbedTLS).** _(done - hybrid)_ mbedTLS's base64 is slow because it is
      constant-time (side-channel hardened). Rather than drop that globally, the path now splits by data
      sensitivity: **encode** (only the public WebSocket-accept digest) uses the fast software codec on
      every target (~731 -> ~47 us on the ESP32-S3, **~15x**); **decode** (the secret Basic-auth
      credentials) keeps mbedTLS's constant-time decoder on the ESP32. JWT / OIDC already used the
      software `base64url`. HW-verified on the ESP32-S3: RFC 4648 vectors both directions, a Basic-auth
      round-trip, a 256-byte round-trip, and fail-closed on malformed input all pass; host tests
      (test_websocket, test_auth) still pass.
- [x] **WAL CRC-32 was CRC-bound on-device.** _(done)_ Replaced the table-less bit-by-bit CRC with a
      byte-table CRC-32 (1 KiB rodata). Measured **~3.6x faster** on the ESP32-S3 (231 -> 64 us/KiB,
      ~4.4 -> ~15.9 MB/s), roughly halving `record_encode` / `store_append` / dbm `put`; same 3.6x on the
      host. Byte-identical output (the CRC-32 check-vector `0xCBF43926` and all wal/dbm/docstore tests
      still pass). See FEATURE_PERFORMANCE section 4.
- [x] **`pc_resp_encode_command` was ~20 us on-device** (formatted length prefixes with `snprintf`). _(done)_
      Replaced with a hand-rolled decimal writer: **~6x faster** on the ESP32-S3 (19.9 -> 3.3 us), ~5x on
      the host (329 -> 65 ns), byte-identical output (all 14 native_redis tests pass).

### CNC / machine-tool connectivity

Two link layers reach a CNC controller: legacy **RS-232** (drip-feed / DNC) and modern **Ethernet** (vendor APIs, file transfer, open telemetry). The read/telemetry side already ships as `services/mtconnect` (an ANSI/MTC1.4 agent over the existing HTTP stack); the rest below is open.

- [x] RS-232 DNC codec _(done)_: G-code (RS-274 / ISO 6983) line framing to drip-feed a program to a controller, with software flow control (XON/XOFF = DC1 `0x11` / DC3 `0x13`), `%` program start/end markers, and EIA RS-244 vs ISO 7-bit tape handling. Transport-agnostic so the same framing rides RS-232 or a socket. `PC_ENABLE_DNC` (services/machine_tool/dnc): `pc_dnc_iso_to_eia`/`pc_dnc_eia_to_iso` translate either tape code (EIA RS-244 is a distinct odd-parity 8-track code, parity in channel 5, EOB = 0x80, rewind-stop = EIA End-of-Record 0x0B, uppercase-only; ISO is ASCII with optional even parity); `pc_dnc_encode_block` + `pc_dnc_encode_marker` + `pc_dnc_encode_leader` frame a program; `DncDecoder` reassembles the wire stream back into ASCII G-code lines (fail-closed, drops an over-long block whole); `DncFlow` tracks XON/XOFF on the **reverse** channel (kept out of the forward decode, since EIA `3` is 0x13 = DC3). The EIA table is validated by an odd-parity + exact-inverse host guardrail; full encode -> decode round-trips pass for both codes (`native_dnc`, 13 cases). _Follow-up:_ an example that drip-feeds over `Serial` with live XON/XOFF (needs a UART-wired controller to verify), and the Ethernet DNC item below reuses this framing over a socket.
- [x] Ethernet DNC _(done, HW-verified)_: stream the same G-code framing over a plain TCP socket (network drip-feed) for controllers that expose a raw program port. **Engine shipped** (services/machine_tool/dnc/dnc_stream): `dnc_stream` drip-feeds a whole program - leader / `%` markers / one block per line / trailer - over a send/recv seam, pausing on a reverse-channel XOFF and resuming on XON. Transport-agnostic (the same engine serves Ethernet DNC over a TCP socket or the RS-232 UART follow-up); host-tested end to end with a scripted mock controller that decodes the stream back and exercises the pause-resume path (`native_dnc`, +8 cases = 22). New knob `PC_DNC_XOFF_MAX_POLLS`. **Example shipped** (`examples/L7-Application/EthernetDnc`): drip-feeds a program to a controller's raw TCP program port on a real device (WiFi -> pc_client -> dnc_stream), with the `cl_send`/`cl_recv` seam glue (a non-blocking reverse-channel read for XON/XOFF); a from-scratch README including a machine-less test (capture the stream with an `nc` listener) + a DncStreamResult troubleshooting table; ESP32-compile-verified via `pio ci` (esp32dev, Flash 68.7%). **HW-verified 2026-07-13** on an ESP32-S3 over a W5500 wired link to a TCP capture sink: the drip is **byte-exact** (108-byte framing - 16 NUL leader, `%`CRLF, CR-before-LF blocks, `%`CRLF, 16 NUL trailer - matched an independently-computed reference), and the **XON/XOFF pacing** path works (0 bytes sent during a 3 s reverse-channel XOFF hold, then the full identical program after XON).
- [ ] Fanuc FOCAS: client codec for the Fanuc Open CNC API (FOCAS1/2 over Ethernet) - read position/status/alarms and program up/download. Proprietary wire format; reverse from manuals plus a real controller.
- [x] FTP client _(done)_: many controllers (Fanuc, Haas, Mazak, Heidenhain) expose program storage over FTP - a small RFC 959 client (control + passive data channel) to push/pull `.nc` files. `PC_ENABLE_FTP` (services/file_transfer/ftp): the pure wire codec - `pc_ftp_build_command` / `pc_ftp_build_port` / `pc_ftp_build_eprt` (RFC 2428) build control commands, `pc_ftp_parse_reply` detects a complete single/multi-line 3-digit reply and reports the bytes consumed, and `pc_ftp_parse_pasv` / `pc_ftp_parse_epsv` decode the data-channel address. Reply / PASV / EPSV parsing verified against authentic strings captured from a live FTP server (`native_ftp`, 13 cases); the two sockets are the application's. _Follow-up:_ an example that runs a RETR/STOR over the `pc_client` transport (needs a real FTP server / controller to HW-verify), and the SMB/CIFS item below for Windows-share storage.
- [~] SMB/CIFS client: Windows-share program storage is the other common file path - a minimal SMB2 client (negotiate / session-setup / tree-connect / create / read / write) to read and write programs on a share. **Increment 1 shipped** (`PC_ENABLE_SMB`, network_drivers/application/smb): the pure SMB2 wire codec - the Direct-TCP transport frame, the 64-byte little-endian sync header (`pc_smb2_build_header`/`pc_smb2_parse_header`, ProtocolId + StructureSize validated), the NEGOTIATE request builder (dialects 2.0.2/2.1/3.0/3.0.2 + client GUID), and the NEGOTIATE response parser (chosen dialect, server GUID, max sizes, the SPNEGO/NTLM security token, bounds-checked). Field layout verified vs MS-SMB2 §2.2.1.2/§2.2.3/§2.2.4; `native_smb`, 6 cases. **Increment 2 shipped** (network_drivers/application/smb/smb_md): the NTLM digests the lib lacked - **MD4 (RFC 1320), MD5 (RFC 1321), HMAC-MD5 (RFC 2104)**, streaming + zero-heap, KAT-verified against the RFC vectors + the well-known NT hash of "password" (`test_smb_crypto`, 5 cases). **Increment 3 shipped** (network_drivers/application/smb/ntlm): the NTLMv2 response computation (MS-NLMP §3.3.2) - `pc_ntlm_nt_hash`, `pc_ntlm_ntowfv2`, `pc_ntlm_v2_response` (NTProofStr / NtChallengeResponse / SessionBaseKey), verified byte-for-byte vs the MS-NLMP §4.2 worked example (`test_ntlm`, 3 cases). **Increment 4 shipped** (network_drivers/application/smb/ntlmssp): the NTLMSSP message codec (MS-NLMP §2.2.1) - `pc_ntlmssp_build_negotiate` (type 1), `pc_ntlmssp_parse_challenge` (type 2, extracts server challenge + target info, bounds-checked), `pc_ntlmssp_build_authenticate` (type 3, Len/MaxLen/Offset payload layout); an end-to-end test parses a CHALLENGE, computes the NTLMv2 response, and confirms the AUTHENTICATE carries it (`test_ntlmssp`, 5 cases). **Increment 5 shipped** (network_drivers/application/smb/spnego): the SPNEGO GSS-API DER wrapping (RFC 4178) - `pc_spnego_wrap_negotiate` (the `[APPLICATION 0]` InitialContextToken advertising the NTLM mech OID + the NTLMSSP NEGOTIATE mechToken), `pc_spnego_parse_response` (extracts the CHALLENGE responseToken from the server NegTokenResp, skipping negState/supportedMech), and `pc_spnego_wrap_authenticate` (the reply NegTokenResp); zero-heap definite-length DER, verified byte-exact + round-trip + independently vs `openssl asn1parse` (`test_spnego`, 4 cases). **Increment 6 shipped** (network_drivers/application/smb/smb2): the SMB2 SESSION_SETUP request/response framing (MS-SMB2 §2.2.5/§2.2.6) - `pc_smb2_build_session_setup` (SecurityMode + PreviousSessionId + the SPNEGO security buffer at offset 88, echoing the server SessionId on round 2) and `pc_smb2_parse_session_setup_response` (StructureSize 9, SessionFlags, server security buffer, bounds-checked; the caller reads SessionId + STATUS_MORE_PROCESSING_REQUIRED/SUCCESS from the header); an end-to-end test routes a full auth round through framing -> SPNEGO -> NTLMSSP and recovers the server challenge intact (`test_smb2`, now 10 cases). **Increment 7 shipped** (network_drivers/application/smb/smb2): the file commands TREE_CONNECT / CREATE / CLOSE (MS-SMB2 §2.2.9-§2.2.16) - `pc_smb2_build_tree_connect` + parse (connect `\\server\share`, TreeId from the response header, ShareType disk/pipe/print), `pc_smb2_build_create` + parse (open/create with DesiredAccess/ShareAccess/CreateDisposition/CreateOptions, returns the 16-byte FileId + EndofFile), `pc_smb2_build_close` + parse (`test_smb2`, now 15 cases). **Increment 8 shipped** (network_drivers/application/smb/smb2): READ / WRITE (MS-SMB2 §2.2.19-§2.2.22) - `pc_smb2_build_read` + parse (read a length at a file offset, response data returned bounds-checked as a pointer into the message) and `pc_smb2_build_write` + parse (write a buffer at an offset, response reports the byte count) (`test_smb2`, now 19 cases). **The SMB2 client codec is complete**: NEGOTIATE -> SESSION_SETUP (NTLMv2 over SPNEGO) -> TREE_CONNECT -> CREATE -> READ / WRITE -> CLOSE. **Client engine shipped** (network_drivers/application/smb/smb_client): `smb_open` drives the whole NEGOTIATE -> two-round NTLMv2 SESSION_SETUP -> TREE_CONNECT -> CREATE handshake over a send/recv seam (Direct-TCP framing + the NTLMSSP/SPNEGO token flow + MsvAvTimestamp extraction handled internally) and returns an `SmbHandle`; `smb_read` / `smb_write` loop the READ / WRITE commands in PC_SMB_BUF-sized chunks (read stops at a short read / STATUS_END_OF_FILE, write grows the cached file size); `smb_close` releases the handle - a POSIX-like open/read/write/close surface, host-tested end to end with a scripted mock SMB2 server (`test_smb_client`, 10 cases: the handshake happy path + auth failure / bad share / not found / IO error / arg validation, plus multi-chunk read, read-past-EOF, multi-chunk write, and a byte-exact write-then-read round trip). **Example shipped** (`examples/L7-Application/SmbFileClient`): reads a file off a share on a real device (WiFi -> pc_client:445 -> smb_open/smb_read/smb_close), showing the `cl_send`/`cl_recv` glue that binds the send/recv seam to `pc_client`; a from-scratch README (set up a Samba share, the CHANGE ME fields, an SmbResult troubleshooting table); ESP32-compile-verified via `pio ci` (esp32dev, Flash 69.4%). **Increment 9 shipped** (network_drivers/application/smb): **SMB 2.x message signing** - `pc_sha256` / `pc_hmac_sha256` added to smb_md (FIPS 180-4 + RFC 2104, KAT-verified vs FIPS/RFC 4231 vectors, `test_smb_crypto`), and `pc_smb2_sign` / `pc_smb2_verify` (MS-SMB2 §3.1.4.1/§3.1.5.1) which set SMB2_FLAGS_SIGNED, HMAC-SHA256 the message with the Signature zeroed, and write/constant-time-compare its first 16 octets; verified against a Python-computed reference signature + tamper/wrong-key/short-message rejection (`test_smb2`). **Increment 10 shipped** (network_drivers/application/smb/smb_client): the signer is **wired into the dialogue engine** - `smb_open` reads the server's NEGOTIATE SecurityMode, and when signing is required derives the SMB 2.x session key (the NTLMv2 SessionBaseKey), signs the round-2 SESSION_SETUP, and marks the `SmbHandle` signing-active (unless the session is GUEST/NULL, MS-SMB2 §3.2.5.3.1); every later request (TREE_CONNECT / CREATE / READ / WRITE / CLOSE) is then HMAC-SHA256 signed and every response signature verified, failing closed on a missing or wrong signature. Host-tested end to end with the mock acting as a reference signing peer that re-derives the same key from the client's AUTHENTICATE (`test_smb_client`: a full signed read/write/close round trip with server-side signature checks, a tampered-response rejection, and an unsigned-when-not-required regression). **Increment 11 shipped** (network_drivers/application/smb/smb2): the **SMB 3.1.1 negotiate-context codec** - `pc_smb2_build_negotiate_311` offers the dialect list through 0x0311 with the mandatory PREAUTH_INTEGRITY_CAPABILITIES context (SHA-512 + a client salt) and a SIGNING_CAPABILITIES context advertising HMAC-SHA256, each 8-byte aligned per §2.2.3.1; `pc_smb2_parse_negotiate_contexts` walks the response NegotiateContextList (located by NegotiateContextOffset / NegotiateContextCount) and extracts the negotiated preauth hash + salt, the signing algorithm, and the cipher, every context header + data bounds-checked (`test_smb2`, 3 cases: build layout, a three-context parse, and malformed/truncated/low-offset rejects). **Increment 12 shipped** (network_drivers/application/smb/smb_md): the **SP800-108 counter-mode KDF** `pc_kdf_ctr_hmac_sha256` (HMAC-SHA256 PRF, a 32-bit counter before the fixed input, NIST SP800-108 §5.1) - the primitive SMB 3.x derives its signing / encryption keys with (MS-SMB2 §3.1.4.2); the caller passes the assembled `Label || 0x00 || Context || [L]` fixed input, keeping the primitive independent of the per-key label choices. Verified against the **NIST CAVP KBKDF (KDFCTR) vectors** - a single-block L=128 case and a two-block L=320 case that exercises the counter loop + truncation (`test_smb_crypto`). **Increment 13 shipped** (network_drivers/application/smb/smb_md): **SHA-512** (FIPS 180-4) - streaming `pc_sha512_init` / `_update` / `_final` + one-shot `pc_sha512`, the hash the SMB 3.1.1 preauth-integrity chain runs over the handshake messages; KAT-verified against the FIPS 180-4 vectors including the 112-byte two-block example, plus a streaming-equals-one-shot check (`test_smb_crypto`). _Only remainder:_ assemble these primitives into the 3.1.1 flow - the preauth-integrity hash chain (SHA-512 over the concatenated NEGOTIATE / SESSION_SETUP messages) + the per-key label/context assembly (`SMBSigningKey` etc.) feeding the KDF + AES-CMAC signing (+ the NTLMSSP MIC), then HW-verify against a real Samba / Windows share.
- [x] MTConnect follow-ups _(done)_: the `probe` (device model) document - `pc_mtc_devices_begin/add_item/end` build an MTConnectDevices doc (a `<Device>` with its `<DataItems>`, optional `name`/`units`); the `asset` document - `pc_mtc_assets_begin` + `pc_mtc_assets_cutting_tool_begin`/`_tool_life`/`_cutting_tool_end` + `pc_mtc_assets_end` build an MTConnectAssets doc (a `<CuttingTool>` with its `<CuttingToolLifeCycle>`/`<ToolLife>`, optional `serialNumber`/`toolId`/`deviceUuid`/`timestamp`/`limit`); and the streaming `sample` sequence cursor - `pc_mtc_sample_buffer` (a fixed ring, `pc_mtc_sample_buffer_init`/`_add`) with `pc_mtc_sample_query` replaying a from/count window as an MTConnectStreams document whose header carries firstSequence/lastSequence/nextSequence (MTC1.4 §6.7, oldest evicted + firstSequence advances when full). All tested in `test_mtconnect` (12 cases). The MTConnect agent's read documents (current/sample/probe/asset) are now complete.

### Routing / forwarding / inspection

Building on the existing forwarder (`native_forward` / `native_gateway` / `native_southbound`) toward the v5 "interface forwarding" milestone.

- [x] HttpRoute-by-tag to interface _(done)_: let a rule tag a flow (by source, destination, port, protocol, or a match expression) and bind that tag to an egress interface, so tagged traffic leaves a chosen NIC / radio - policy routing layered on the forwarder. `pc_forward_route_add(src, offset, pattern, mask, patlen, egress_if, rate_cap)` (services/net/forward): a frame matching the byte pattern (the same offset/mask primitive as the ACL, so it keys on EtherType / IP-proto / port / address-prefix - any field at a known offset) is forwarded only to `egress_if`, taking precedence over the src->dst fan-out (first-match-wins), with the same never-reflect / rate-cap / fail-closed guarantees and a `policy_routed` stat. Static table `PC_FWD_MAX_ROUTES`; additive (empty by default = no behavior change). `native_forward` +7 cases (23 total).
- [x] Port forwarding _(done, HW-verified)_: DNAT-style forward of an inbound port to an internal `host:port` (and the return path), so the server can publish a service that lives behind it. **Engine shipped** (`PC_ENABLE_RELAY`, services/net/relay): `pc_relay_step` is a pure, non-blocking bidirectional byte pump over two send/recv seams (inbound connection <-> origin `pc_client`), with backpressure carry and independent half-close (each direction finishes on its source's EOF; the peer's optional `shutdown` seam propagates the FIN). New knob `PC_RELAY_BUF`. Host-tested with two mock sockets (bidirectional transfer, backpressure, half-close + shutdown propagation, a large byte-exact transfer, a seam error, out-of-band EOF; `native_relay`, 6 cases). **Server-side listener shipped** (services/net/relay/relay_listener, `PROTO_RELAY`): `server.listen(port, PROTO_RELAY)` + `pc_relay_publish(listener_id, origin_host, origin_port)` installs a connection handler that dials the origin via `pc_client` on each inbound accept, pumps `pc_relay_step` from the server poll loop, and tears both down on close (fixed static bind/bridge tables; opt-in, compiled out by default). **Example shipped** (`examples/L7-Application/PortForward`, ESP32-link-verified via `pio ci`, Flash 70.6%; README has a machine-less `python -m http.server` + `curl` test). **HW-verified 2026-07-13** on an ESP32-S3 over a W5500 wired link: a 1 MB file pulled through the board's front port (8080 -> origin :8000) matched the origin's SHA256 **byte-exact**. Throughput ~44 KB/s - a single-NIC relay crosses the one W5500 twice per byte and is round-trip-latency bound (the design point is exposing a service / small control files, not bulk throughput).
- [x] Optional packet inspection _(done)_: an opt-in inspection hook on the forwarding path (parse / observe / filter before forward) for logging, metrics, or drop rules. Off by default (cost + privacy); a build-time + runtime toggle. `PC_FWD_INSPECT` (build-time, compiles the hook out entirely when off) + `pc_forward_set_inspector(fn, ctx)` (runtime; null clears): a flexible app callback runs on every ingress frame after the ACL and before routing, returning `PC_FWD_INSPECT_PASS`/`DROP` (a drop is counted as `inspect_dropped`). `native_forward` +3 cases (26 total).

### Content delivery network (CDN) capability

Let the device act as a caching edge / content-distribution node, not just an origin. Builds on what already exists (file serving, ETag, Range/206, the reverse-proxy `Forwarded` recovery, the forwarder, and the new SD data-store stack) toward serving and replicating content near where it is consumed. Unpolished; **scope to refine with the user** (which role(s), how content is keyed/invalidated, single-device vs the two-rig / mesh case).

- [x] Edge cache for upstream content _(done, HW-verified)_: `PC_ENABLE_EDGE_CACHE` (services/web/edge_cache) - a caching reverse-proxy edge. The **pure engine** (edge_cache): RFC 9111 freshness (`Cache-Control` / `Expires` / heuristic / corrected age over the monotonic clock), the response header-field + HTTP-date parsing httpcache lacks (IMF-fixdate / RFC 850 / asctime), the canonical cache key + SHA-256 digest + `Vary` secondary key, the L1 LRU/TTL store + storeability rules, and conditional revalidation (build `If-None-Match`/`If-Modified-Since`, apply 304). The **async origin-fetch engine** (edge_fetch) accumulates the origin response over a pc_client seam (completion by Content-Length / chunked / close) and never stalls the worker. The **glue** (edge_cache_proxy) registers a cache middleware + an async-fetch poll hook (`edge_poll_hook` in `http_poll_slot`): a fresh hit serves from RAM via `send_chunked`, a miss/stale entry suspends the client request and fetches the origin, non-cacheable/error responses use a transient slot, and every failure fails open. `pc_edge_cache_enable(server)` + `pc_edge_cache_map(prefix, origin)`; replays `Content-Encoding`/`ETag`/`Last-Modified`/`Age`. Host-tested (`native_edge_cache`, 30 cases) and **HW-verified on an ESP32-S3** fetching a real origin over WiFi (example EdgeCache): MISS -> HIT -> REVALIDATED(304) -> purge all **byte-exact**, the origin fetched exactly once per miss, a stale entry refreshed with a cheap 304 (no body re-download). The **L2 SD tier** (edge_cache_sd, gated `PC_ENABLE_DBM`) now spills evicted L1 entries to a dbm store on the WAL (SD-backed) and promotes them back on a miss: a compact versioned entry<->dbm-value serialization keyed by the 32-byte cache-key digest, an `on_evict` write-back hook on the L1 store, promote-on-miss forced to revalidate (the monotonic insert time is meaningless across a reboot, so only validator-carrying entries are spilled - a cheap 304 refreshes them), reboot survival via dbm index replay, and L2-aware purge/reset (foreign values in a shared dbm are left untouched). `pc_edge_cache_bind_sd(dbm)`; host-tested over a RAM `WalDev` (`native_edge_cache_sd`, 15 cases) incl. serialize roundtrip, spill/promote, oversize-stays-L1, reboot survival, and prefix purge. **Range/`206`** is served straight from a cached body (gated `PC_ENABLE_RANGE`): a single-range `Range` request yields `206 Partial Content` + `Content-Range` over the existing `send_chunked` cursor (windowed `off..end`), `416` when unsatisfiable, `Accept-Ranges: bytes` on full hits, via the shared `network_drivers/application/http_range.h` parser reused with the file server. The client `Range` is captured at middleware time into a per-slot buffer because `http_pool[slot]` is reset/reused by the time a miss is served from the poll (an HW-caught bug, see BUGS.md). **`https://` origins** are supported (gated `PC_ENABLE_EDGE_ORIGIN_TLS`): a per-route TLS transport layers the shared client-TLS session (`pc_tls_csess`) over `pc_client` (BIO wrappers mirroring the MQTT/WS clients), selected per route via a `bool https` on the route map and threaded through the fetch slot; the handshake blocks briefly in the transport's `open` (like `Tcp.client->open`'s connect and the MQTT/WS clients), and a `pc_tls_client_session_active()` guard fails open rather than tearing down a live shared session (so one TLS origin fetch runs at a time). Verification is off by default (encrypt-only); `pc_edge_cache_set_origin_ca` / `_pin` opt into chain+hostname or pin verification (shared client-TLS trust store). **HW-verified on an ESP32-S3** against a real https origin: encrypt-only fetch byte-exact, a correct CA verifies + a wrong CA is rejected (fail-open), and Range/`206` works over the TLS-fetched body. Note: on mbedtls v2 (espressif32 default) an IP-address origin needs a CN-matching cert (IP-address SANs are not matched; DNS origins work normally). **Cross-device mesh** (sibling cache) is shipped too - see the mesh/edge item below (`PC_ENABLE_EDGE_MESH`, example 80). _Remaining:_ a holistic fix for the `http_pool[slot]`-goes-stale-after-the-async-fetch root cause (a miss response is emitted `HTTP/1.0`/`Connection: close`, and a `Vary` response cached on a miss stores an empty secondary key - both pre-existing efficiency issues, never a wrong-content serve) by snapshotting the client request across the suspend; and two TLS-origin follow-ups (an async handshake so a cold https MISS never blocks the worker, and a multi-instance client-session pool for >1 concurrent TLS fetch).
- [x] Cache key + invalidation _(done)_: the deterministic key (method + host + path, SHA-256 digested - also the L2 dbm key) + `Vary` as a secondary key (each variant re-serialized against the request); the purge API `pc_edge_cache_purge` (single) / `pc_edge_cache_purge_prefix` (prefix/wildcard); TTL expiry + LRU eviction in the bounded store. Part of services/web/edge_cache above.
- [x] Origin-side cache directives _(done)_: first-class helpers to emit correct edge-cacheable responses from app routes (immutable static assets, `stale-while-revalidate`, `s-maxage`), so a device sitting _behind_ a real CDN is cached correctly. `PC_ENABLE_HTTP_CACHE` (services/web/httpcache): `cache_control_build` serializes a `pc_cache_control` struct into the canonical directive string (pass to `set_cache_control()`) with presets `cache_immutable_asset` / `cache_shared` / `cache_revalidatable` / `cache_no_store`; `cache_control_parse` is a tolerant reader; `cache_freshness_lifetime` implements the RFC 9111 4.2.1 precedence. Verified vs RFC 9111 (+ RFC 8246 `immutable`, RFC 5861 stale-*); `native_httpcache`, 8 cases incl. a build->parse round-trip. This is the standards-mechanics layer; the caching **tier** (below) is the remaining architectural piece to scope with the user.
- [x] Content distribution across devices (mesh/edge) _(done, HW-verified)_: a fleet shares one warm cache. `PC_ENABLE_EDGE_MESH` (services/web/edge_cache/edge_mesh) - the design pass resolved as **pull (sibling cache), not push**: on a full local miss a node queries its static sibling peers over a plaintext `ConnProto::PROTO_MESH` TCP link before the origin, and pulls a fresh copy from whichever peer has it, so the origin is fetched once per fleet. **Consistency** is RFC 9111 §4.2.3 age propagation (the transfer carries the object's freshness/age, reusing `edge_current_age()`), so a sibling-fresh object serves for its remaining lifetime with zero origin contact - no invalidation protocol, no consistency window (a stale peer copy self-expires by TTL, the puller re-checks freshness). **Addressing** is a static peer list (`pc_edge_cache_add_peer`). The wire frame reuses the shared `edge_sd` entry serializer + a timing trailer; the puller ships a bounded request-header snapshot so the peer re-runs the exact `edge_store_find` Vary matcher. A serving node (`pc_edge_cache_mesh_serve` after `server.listen(port, PROTO_MESH)`) answers only from its LOCAL cache - one hop, never re-querying its own origin/peers, so the fleet cannot loop. The query is a pre-origin phase of the same async fetch slot (reusing that slot's origin buffer, so no extra per-slot memory) pumped from the poll loop; a peer MISS / exhausted list transitions to the ordinary origin fetch. Pure wire codec + peer-query engine host-tested (`native_edge_mesh`: frame round-trips, age propagation, HIT/MISS/timeout/close), example MeshCache. _Follow-ups:_ a TLS sibling link, UDP-broadcast peer auto-discovery, and push replication with invalidation.
- [x] Range-aware + chunked delivery from the cache _(done)_: a cached object serves a single-range `Range: bytes=...` request as `206 Partial Content` with a `Content-Range` header (or `416` when unsatisfiable), streaming just the requested window through the existing constant-memory `send_chunked` cursor with backpressure; every full hit advertises `Accept-Ranges: bytes`. Gated `PC_ENABLE_RANGE` (now usable by the edge cache without file serving). The single-range parser is promoted to a shared owner `network_drivers/application/http_range.h` (`http_parse_byte_range`) reused by both the static file server and the cache; multi-range falls back to a full 200 (RFC 7233 §3.1), `If-Range` is a documented follow-up. Host-tested (`native_edge_cache` range-math cases; `native_range` regression green after the promotion).

### Pentesting

- [ ] Extend the pentesting suite to cover more cases. Get creative; try to break the server. _Ongoing:_
      the fuzz harness (`native_pentest`) now also hammers the **SQLite on-disk reader** (random pages, a
      garbage b-tree the multi-page cursor must survive without hanging, a hostile overflow chain, and
      structure-aware mutation of a valid image), the **Redis RESP decoder** (random bytes + lying `$`/`*`
      length prefixes that must not become an over-read), and the **OPC UA Binary parsers** (random bodies
      behind a valid UACP header, an `OPN` with a lying `SecurityPolicyUri` length, per-type size mismatches;
      the NodesToRead/Write/Browse counts stay clamped), the **number parsers** (`pc_strtol`/`_strtoul`/
      `_strtof` on huge integer + exponent strings), the **GraphQL query parser** (huge int / exponent
      literals), the **DNS server** query parse (QNAME over-read + response-builder out_cap), the **DNP3**
      data-link frame, the **STOMP** frame parser (slice-bounds), and the **WebDAV** core (RFC 4918) - the
      PROPPATCH request-body XML walker (`pc_webdav_proppatch_ms`, which echoes property tags straight out
      of an attacker-chosen body: 20k iterations of XML-token/random-byte splices), the Destination-header
      percent-decoder (`pc_webdav_dest_path`), and the 207 Multi-Status builder / xml-escape helper, each
      canary-checked so no write ever crosses the caller's cap - **78/78** cases pass plain and clean under
      ASan+UBSan (run the built `program` directly; the
      PIO runner mishandles the sanitizer binary's signals). Running the binary under `-fno-sanitize-recover=all`
      **found and fixed a whole class of signed-overflow UB + `10^exponent` DoS** in the hand-rolled number
      parsers (SNMP BER, `pc_strtol`, RESP, `pc_strtof`, GraphQL, JWT, exc_decoder - see docs/BUGS.md; sweep
      now complete). _Next candidates:_ the WebSocket frame reassembler (`ws_feed_byte` - needs the
      transport/session mocks wired into the env since it dispatches on frame-ready).

### Docs

- [ ] Chart how the docs are scraped and built; keep refining and implementing automation in the gaps to increase maintainability and catch outliers.
- [ ] Full docs audit (not creative grep + diff): close the Grand-Canyon-sized gaps and the stale sections.
- [ ] Docs theming: the color palette is close but a little off, and there are many alignment issues. Refine "squirty's house" (bubbles, rocks, sponge conceptually right) and his behavior.
- [ ] Add a switch to the sandboxed docs so teachers can turn it off completely.
- [ ] Let users preview the (silly) themes on the live docs. Maybe.

## Feature parity with ESPAsyncWebServer (ESP32Async)

Conceptual features [ESP32Async/ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer)
offers that this library does not yet, ordered by value-vs-fit within the
zero-heap / fixed-buffer model. Implement top-down, one at a time, each with
native Unity tests before moving on. Each must keep the "no heap after
`begin()`" guarantee (fixed-size buffers, compile-time caps).

<details>
<summary><b>Expand feature-parity items</b></summary>

- [x] **1. Custom response headers + cookies (high / easy).** _(done)_
      Per-connection `_extra_hdr[MAX_CONNS][EXTRA_HDR_BUF_SIZE]` buffer injected
      into [`send()`](@ref PC::send) /
      [`send_empty()`](@ref PC::send_empty) /
      [`redirect()`](@ref PC::redirect), the same way the CORS block is
      injected. New API: [`add_response_header()`](@ref PC::add_response_header),
      [`set_cookie()`](@ref PC::set_cookie),
      [`clear_response_headers()`](@ref PC::clear_response_headers).
      Oversized headers are dropped whole; the buffer is cleared at the start of
      each dispatch. Tested by `test_response_headers` (9 cases).

- [x] **2. Request-data convenience (high / easy).** _(done)_ By-name request
      header lookup already existed via [`http_get_header()`](@ref http_get_header);
      added [`http_get_form()`](@ref http_get_form) for urlencoded POST body
      fields (parsed on demand into a caller buffer, gated on the
      `application/x-www-form-urlencoded` Content-Type, raw values to match
      `http_get_query()`). Tested by `test_form_params` (5 cases).

- [x] **3. Path parameters in routing (high / medium).** _(done)_ `/users/:id`
      style capture segments stored in a fixed `HttpReq::path_params[MAX_PATH_PARAMS]`
      array, exposed via [`http_get_param()`](@ref http_get_param). Routes are
      flagged `is_param` at registration; `match_path_params()` does a
      segment-by-segment match (literal segments exact, `:name` segments
      captured) alongside the existing exact + trailing-`*` matcher. Tested by
      `test_path_params` (8 cases).

- [x] **4. Digest authentication (medium / medium).** _(done)_ HTTP Digest
      (RFC 7616, SHA-256, `qop=auth`) via the shared `pc_sha256`, selected by
      the new `digest` flag on
      [`on(..., realm, user, pass, digest=true)`](@ref PC::on).
      Server nonce regenerated per `begin()`; challenge emitted by
      `send_unauth()`; verified by `check_digest_auth()`. The parser now
      captures the full `Authorization` value into a dedicated
      `HttpReq::authorization[DIGEST_AUTH_HDR_MAX]` buffer (a Digest header far
      exceeds `MAX_VAL_LEN`). Tested by `test_digest_auth` (5 cases: challenge,
      valid handshake, wrong password, forged nonce, 128-bit hex nonce) and
      independently grounded against `openssl`/FIPS vectors by `test_digest_vectors`
      (4 cases). The nonce is now seeded from the hardware CSPRNG
      (`esp_random()`) folded through SHA-256 with a counter + `millis()`, and
      regenerated per `begin()`.
      _Follow-up:_ `nc` (nonce-count) replay tracking is still not implemented:
      it needs per-client state, which conflicts with the single shared server
      nonce and this device class's 1–2 client model (global `nc` tracking would
      reject legitimate concurrent clients). The per-`begin()` nonce rotation
      bounds the replay window in the meantime.

- [x] **5. Response templating (medium / medium).** _(done)_ `{{name}}`
      substitution via [`send_template()`](@ref PC::send_template) with
      a `TemplateVar` resolver callback. The body is walked twice (size, then
      write) so it is never buffered whole - constant memory regardless of body
      size. Unterminated/over-long placeholders are emitted literally; HEAD
      sends headers only. Tested by `test_template` (6 cases).
      _Follow-up:_ apply the same resolver path to static-file serving.

- [x] **6. Middleware pipeline (high value / large).** _(done)_ Fixed-size,
      composable global middleware chain (cap `MAX_MIDDLEWARE`, default 4) run in
      registration order before route matching via
      [`use()`](@ref PC::use). A [`Middleware`](@ref Middleware) returns
      [`MW_NEXT`](@ref MwResult) to fall through or [`MW_HALT`](@ref MwResult) to
      short-circuit (after sending its own response); middlewares can also inject
      response headers / log every request (incl. unmatched 404s). Added a
      built-in fixed-window rate limiter,
      [`enable_rate_limit(max, window_ms)`](@ref PC::enable_rate_limit),
      that answers over-budget requests with `429` + `Retry-After` before the
      chain (cheapest rejection under flood); rollover-safe, per-server state, no
      per-IP table. Tested by `test_middleware` (9 cases).
      _Design note:_ CORS + Basic/Digest auth were **left as-is** (tested/green)
      rather than re-expressed as middlewares - the chain is additive and
      composes alongside them; "logging middleware" is the existing
      [`on_request_log()`](@ref PC::on_request_log) hook plus any
      user `use()` middleware. _Follow-up:_ per-route middleware attachment (a
      middleware can already gate on `req->path` in user code).

- [x] **7. Chunked / streaming app responses (medium / medium).** _(done; pull
      generator since v4.5.0)_
      [`send_chunked(slot, code, type, source, ctx)`](@ref PC::send_chunked)
      writes the status + headers (`Transfer-Encoding: chunked`, plus CORS /
      queued custom headers), then pulls the body from a
      [`ChunkSource`](@ref ChunkSource) generator one piece at a time, frames each
      as an RFC 7230 §4.1 chunk, and emits the terminating `0\r\n\r\n`. The body is
      never buffered whole AND the send paces with the TCP window, paging across
      worker loops (`chunk_send_pump`, resumed by the sent callback) - so output is
      unbounded in constant memory and a body past the send window is never
      truncated (the old one-shot `ChunkedResponse` writer silently truncated
      there). The source returns 0 to end and tracks its position in `ctx` (which
      must outlive the response). HEAD sends headers only;
      [`on_request_log()`](@ref PC::on_request_log) reports the total body
      length. Tested by `test_chunked` (10 cases, incl. a 16 KB body).
      _Follow-up (done):_ `chunk_send_pump` sizes each chunk to `pc_conn_sndbuf()`
      (reserving the frame overhead), flushes and resumes on the next loop when the
      window is full, and clamps a misbehaving source to the window - the same
      per-loop send-window backpressure `file_send_pump()` uses.

- [x] **8. Stretch / lower priority.** _(resolved: the sub-items are shipped, except multi-MCU
      portability which is a closed won't-do per the user's standing request.)_
  - [x] Regex routes _(done)_: [`on_regex()`](@ref PC::on_regex):
        whole-path match via a bounded, allocation-free backtracker (`.`, `* + ?`,
        `[...]`/`[^...]` ranges, `\d \w \s`, `\` escapes; non-capturing). A
        `RE_MAX_STEPS` budget keeps it deterministic (fails closed). Tested by
        `test_regex` (9 cases); example `RegexRoutes`.
  - [x] Static JSON request/response helper _(done)_: zero-heap `JsonWriter`
        (formats into a caller buffer, auto comma/escape, `JSON_MAX_DEPTH` cap)
        plus `json_get_str()`/`json_get_int()`/`json_get_bool()` top-level object
        readers (`src/network_drivers/presentation/json.*`). ArduinoJson stays optional (it heap-allocates).
        Tested by `test_json` (17 cases); example `Json`.
  - [x] Interface filters _(done)_: per-route STA/AP gate via
        [`on(..., pc_iface)`](@ref PC::on) + [`set_ap_ip()`](@ref PC::set_ap_ip).
        Each connection is tagged `PC_IFACE_STA`/`PC_IFACE_AP` at accept time by
        comparing its local IP to the softAP IP. Tested by `test_iface` (7 cases);
        example `InterfaceFilter`.
  - [x] Portability beyond ESP32 (ESP8266 / RP2040 / RP2350). **Won't do (closed):** deferred at the
        user's explicit request and confirmed not pursued - a deliberate scope decision, not an open gap.
        The library targets the ESP32 family (Arduino core + ESP-IDF); porting the WiFi/BLE radio,
        lwIP/FreeRTOS bindings, and the mbedTLS/MPI hardware-acceleration seams to the ESP8266 (no
        FreeRTOS SMP, far less RAM) or the RP2040/RP2350 (no on-chip WiFi/BLE) is out of scope. Reopen only
        if the ESP32-only assumption ever needs to change.

</details>

## Round 2 - post-v2.0.0 subsystems

<details>
<summary><b>Expand Round 2 items</b></summary>

All opt-in (`PC_ENABLE_*`, default off), host-tested where a pure codec exists
and HW-verified on an ESP32 DevKit. Per-feature footprints are in the README.

- [x] **Architecture pass.** Pluggable per-protocol handler dispatch
      (`network_drivers/session/proto_handler.h` - a `ProtoHandler` table, so a new
      TCP protocol registers a handler instead of editing the dispatchers),
      flow-control primitives ([`Tcp.conn->send`](@ref Tcp.conn->send) returns bool,
      `pc_conn_sndbuf`, context-safe `Tcp.conn->raw_send`), response header+body
      write coalescing, and a TLS-BIO unification that fixed a latent handshake
      cross-thread race.
- [x] **Client TLS hardening** (extends [`PC_ENABLE_HTTP_CLIENT_TLS`](@ref PC_ENABLE_HTTP_CLIENT_TLS)).
      Optional CA-chain + hostname verification and SHA-256 cert pinning for
      outbound TLS; encrypt-only by default. `native_http_client`; HW-verified.
- [x] **MQTT 3.1.1 client** ([`PC_ENABLE_MQTT`](@ref PC_ENABLE_MQTT)) + MQTTS.
      Full QoS 0/1/2 (DUP retransmit + inbound QoS-2 duplicate suppression),
      Last-Will, keepalive.
      Host-tested codec (`native_mqtt`); example `MqttClient`.
- [x] **WebSocket client** ([`PC_ENABLE_WS_CLIENT`](@ref PC_ENABLE_WS_CLIENT))
      + `wss://`. Masked frames, fragment reassembly, ping/pong. Host-tested codec
      (`native_ws_client`); example `WebSocketClient`.
- [x] **SNMP notifications** ([`PC_ENABLE_SNMP_TRAP`](@ref PC_ENABLE_SNMP_TRAP)).
      Outbound Traps + InformRequests (v2c) and SNMPv3 USM authPriv traps. Host
      -tested PDU builder (`native_snmp_trap`); example `SnmpTrap`.
- [x] **CoAP server** ([`PC_ENABLE_COAP`](@ref PC_ENABLE_COAP), RFC 7252) with
      resource **Observe** (RFC 7641, [`PC_ENABLE_COAP_OBSERVE`](@ref PC_ENABLE_COAP_OBSERVE))
      and **block-wise transfer** (RFC 7959, [`PC_ENABLE_COAP_BLOCK`](@ref PC_ENABLE_COAP_BLOCK)).
      Host-tested core (`native_coap`); examples `CoapObserve`, `CoapBlock`.
- [x] **Per-IP accept throttle** ([`PC_ENABLE_PER_IP_THROTTLE`](@ref PC_ENABLE_PER_IP_THROTTLE)).
      Closes the cross-connection flood gap left by the global throttle. Example
      `PerIpThrottle`.
- [x] **WebDAV** ([`PC_ENABLE_WEBDAV`](@ref PC_ENABLE_WEBDAV), RFC 4918 class 1
      + advisory locks): OPTIONS/PROPFIND/GET/HEAD/PUT/DELETE/MKCOL/COPY/MOVE/LOCK/
      UNLOCK over the FS. Host-tested 207 builder (`native_webdav`); example
      `WebDav`.
- [x] **Modbus TCP slave** ([`PC_ENABLE_MODBUS`](@ref PC_ENABLE_MODBUS)). Fixed
      data model + MBAP/PDU codec, FC 1/2/3/4/5/6/15/16, via a `PROTO_MODBUS`
      handler. Host-tested (`native_modbus`); example `ModbusTcp`.
- [x] **TLS session resumption** (see the TLS item) and the **web-asset generator**
      (`web_assets/input` -> `web.{h,cpp}` via `build_assets.py`; `/metrics` and
      `/stats` are editable `{{name}}` templates).

Open follow-ups discovered during the above:

- [x] **WebDAV: collection `COPY`** _(done, HW-verified)_ - recursive collection copy
      (RFC 4918 9.8) via `dav_copy_recursive` (bounded depth 8): honors `Depth: 0`
      (collection only) vs `infinity`/absent (full tree), and `Overwrite` (clears the
      target first, 204 vs 201, `Overwrite: F` -> 412). HW-tested on LittleFS (nested
      subcollection + files copied byte-exact) and host-tested: `test/mocks/FS.h` gained
      an opt-in directory tree (`mock_fs_tree_enable()`), and the new
      `native_webdav_handler` env (`test_webdav_handler`) drives the real handler through
      recursive COPY / MOVE / DELETE against it.
      _(PROPPATCH done: 207 with each property refused 403. Streaming PUT done: the
      body is written to the file as it arrives, no longer bounded by
      [`BODY_BUF_SIZE`](@ref BODY_BUF_SIZE).)_
- [x] **Client-side TLS resumption** _(done, ESP32 compile-verified)_ - the persistent
      client session (`csess`, e.g. MQTTS/WSS) now enables client session tickets
      ([`PC_ENABLE_TLS_RESUMPTION`](@ref PC_ENABLE_TLS_RESUMPTION)), saves the
      established session with `mbedtls_ssl_get_session()` after each successful
      handshake, and presents it with `mbedtls_ssl_set_session()` on the next
      `pc_tls_client_session_begin()` for an abbreviated handshake;
      `pc_tls_client_session_forget_session()` forces a fresh full handshake. Compiles on the
      ESP32 toolchain. _Full abbreviated-handshake HW proof is blocked by the same
      stock-Arduino DRAM limit as concurrent TLS (the ~48 KB `PC_TLS_ARENA_SIZE`
      plus MQTT + transport overflows DRAM; needs a smaller-record ESP-IDF build)._
- [x] **SNMPv3 _inform_** _(done)_ - `pc_snmp_inform_v3()` (symmetric with
      `pc_snmp_inform_v2c()` / `pc_snmp_trap_v3()`) builds + sends an authenticated (authPriv
      when a privacy password is set) USM `InformRequest`; the caller owns the
      `request_id` the receiver echoes in its Response and retransmits for confirmed
      delivery. Host-tested via a new opt-in UDP capture seam (`test_snmp_v3`
      `test_inform_v3_builds_informrequest`: a v3 message carrying the InformRequest PDU
      + request-id).
- [x] **CoAP server scope.** `/.well-known/core` resource discovery (RFC 6690) is
      served: GET returns the registered resources in Link Format
      (`application/link-format`, CF 40), paged with Block2 if large; non-GET -> 4.05.
      Host-tested (`test_coap` `test_well_known_core_discovery` / `_rejects_post`) and
      HW-verified against `aiocoap` (interop `coap` peer). **Message de-duplication
      (RFC 7252 §4.5, the one REQUIRED reliability behavior) is now implemented**: a
      retransmitted CON is re-answered from a small (source endpoint, Message-ID)-keyed
      cache - the FULL address, never a hash - without re-running its handler, so a
      client's retransmission cannot execute a non-idempotent request twice
      (`PC_COAP_DEDUP_*`; `native_coap` covers store/lookup, full-address keying,
      expiry, eviction, and an end-to-end handler replay proving the handler runs once).
      The remaining two are **non-goals by design, justified on merit** (not punts):
      separate (deferred) responses contradict the synchronous in-line server (a request
      is answered before its handler returns), and there is no CON retransmission because
      the server never sends a Confirmable message - notifications go out Non-confirmable.
- [~] **Concurrent TLS** (`MAX_TLS_CONNS` > 1). _(library side landed; a `MAX_TLS_CONNS=2`
      PSRAM build was HW-verified to link, boot, and run on an ESP32-S3 - the only unproven
      piece is a live 2-clients-at-once soak, blocked by the lab network, not the code.)_ The whole mbedTLS
      working set is served from one static
      `.bss` arena, and the real internal ceiling is the ESP32 `dram0_0_seg` region
      (~122 KB, ROM-reserved both ends - NOT the 320 KB PlatformIO prints), so a 2nd
      connection overflows the link (measured: `overflowed by 34048 bytes` at an 88 KB
      arena). Three library-side paths now exist + a build guard that turns the cryptic
      linker error into a clear message ([`PC_TLS_ACK_MULTI_CONN_DRAM`](@ref PC_TLS_ACK_MULTI_CONN_DRAM)):
      (1) [`PC_TLS_ARENA_IN_PSRAM`](@ref PC_TLS_ARENA_IN_PSRAM) places the arena in
      external RAM via `EXT_RAM_BSS_ATTR` (needs `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`;
      the stock precompiled arduino-esp32 2.0.x has it off, so a PSRAM/IDF build is
      required - now HW-verified end to end on an ESP32-S3 N16R8 with a rebuilt flag-enabled
      core: the arena lands at `0x3C0xxxxx` external RAM, the board boots octal with no
      watchdog loop, internal DRAM use drops sharply, and it stays zero-heap; the rebuild
      recipe is `tools/psram/README.md`);
      (2) [`PC_TLS_MAX_FRAG_LEN`](@ref PC_TLS_MAX_FRAG_LEN) (RFC 6066 MFL, applied to
      server + client) caps records, pairing with a custom-IDF `CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN`
      shrink; (3) a `memory.ld` DRAM reclaim (advanced - the `0xdb5c` is ROM-reserved,
      so risky). Full 3-prong decision tree in docs/KNOWN_LIMITATIONS.md. **HW-verified on an
      ESP32-S3 N16R8** (arduino-esp32 3.x, PSRAM arena): `MAX_TLS_CONNS=2` with a 128 KB arena
      links using only **56 KB internal DRAM** (17%; the same build with the arena in DRAM uses
      187 KB), boots octal PSRAM, and `begin_tls()` starts the HTTPS listener with a flat heap and
      8.2 MB PSRAM free - the arena is static `.ext_ram.bss`, zero heap. Note the S3's data segment
      is far larger than the classic ESP32's ~122 KB, so on the S3 both connections fit in DRAM too;
      PSRAM's win there is headroom, and it is _required_ only on the tight classic-ESP32 segment.
      This run also uncovered + fixed a core-locking crash in the listener bring-up on arduino-esp32
      3.x (see docs/BUGS.md). **Remaining:** a live 2-clients-at-once handshake soak; not run here
      because the test host cannot reach the device (AP client isolation on the lab WiFi + no admin
      to add a route - the same topology the interop harness works around with a device-out broker),
      not a code limitation.
- [x] **Ethernet PHY abstraction** - _(bring-up shipped; HW-verified 2026-07-19)_ `PC_ENABLE_ETHERNET`:
      `init_eth_physical()` / `eth_ready()` in `network_drivers/physical` wrap the Arduino
      ETH library for an RMII PHY (LAN8720 / IP101 / ...), configured by the standard `ETH_PHY_*`
      build flags (or a board variant that supplies them). The egress reporting + per-route interface
      classifier already handle a wired route (PC_IFACE_ETH, host-tested), so the server serves over
      Ethernet - or dual-homed with Wi-Fi - once the link has an IP. **HW-verified on a Waveshare
      ESP32-P4-POE-ETH** (onboard IP101 RMII PHY, arduino-esp32 3.x): the shipped `init_eth_physical()`
      brought the PHY up (link 100M full-duplex + DHCP), `pc_net_egress_ip()` reported the wired
      address, and the PC server answered real HTTP `GET`s over pure wired Ethernet (no W5500, no
      Wi-Fi) from an on-LAN host. No library change was needed. Example Ethernet.
- [x] **IPv6 dual-stack** - _phase 1 landed (v4.83.0); phase 2 landed (v4.89.0); HW-verified 2026-07-19._ `PC_ENABLE_IPV6`
      enables IPv6 on the netif (`init_ipv6_physical` / `net_global_ipv6` / `pc_ipv6_ready`); the
      listeners already bind `IPADDR_TYPE_ANY`, so the server accepts v6 once an address is up. The
      `pc_ip` address core (`shared_primitives/ip.h`) parses / formats / classifies both
      families (`native_ip`; RFC 4291 + 5952). Example IPv6; both cores compiled. **Phase 2
      (done):** the transport carries the peer as a protocol-agnostic family-tagged `pc_ip`
      (`Tcp.conn->remote_addr()` / `pc_lwip_to_ip()`), and every IP-keyed abuse-prevention feature
      stores and matches the FULL address - the per-IP throttle + auth lockout by
      [`pc_ip_equal`](@ref pc_ip_equal), the IP allowlist by
      [`pc_ip_prefix_match`](@ref pc_ip_prefix_match) (v4 /0-32 + v6 /0-128 CIDR via
      [`Tcp.listener->ip_allow_add_cidr`](@ref Tcp.listener->ip_allow_add_cidr)). This replaced the interim v6
      32-bit hash key, which was collidable (see docs/BUGS.md); no abuse-prevention state is keyed on
      a hash or a uint32 flattening any more (the audit log has no client-IP field). **HW-verified
      (2026-07-19):** an ESP32-S3 (arduino-esp32 2.x) joined to a dual-stack Wi-Fi network formed a
      full SLAAC address set via the shipped `init_ipv6_physical()` - link-local, a unique-local
      (`fd00::/8`), and a router-advertised global (`2600:.../64`); `net_global_ipv6()` read the
      global correctly, and the dual-stack `IPADDR_TYPE_ANY` `:80` listener answered real HTTP `GET`s
      over both the global and the ULA (curled from an on-link Linux host, byte-exact body). The 3.x
      path is the analogous `WiFi.enableIPv6()` (CI-compiled on the Arduino 3.x core).
- [x] **Shared scratch-buffer pool (decided: build before permessage-deflate).** _(done)_
      Several features carry their own fixed _transient_ scratch (SSH `crypto_work`
      and the ~2 KB `ssh_pkt_recv` stack buffer, header formatting, the upcoming
      deflate window). These are mutually exclusive in time, so one shared arena
      cuts peak DRAM. **Model - region-reset-per-dispatch:** one compile-time-sized
      BSS arena (`PC_PLAINTEXT_ARENA_SIZE`); `pc_plaintext_alloc(n, align)`
      bump-allocates; the arena is reset to empty at the top of every event
      dispatch in `server_tick()`, before the protocol handler runs.
      **Race-safety (verified):** all codec/protocol logic runs only in the single
      loop task (`server_tick` / `handle`); the lwIP callbacks (tcpip_thread, maybe
      a different core) only fill the rx ring + enqueue events and never touch
      scratch - so the arena has exactly one accessor and needs no lock. Add a debug
      owner-task assert (`xTaskGetCurrentTaskHandle`) that fails loud if any foreign
      context ever borrows. **Exhaustion-safety:** borrows live only within one
      dispatch and are auto-reclaimed at the reset, so leaks (creeping exhaustion)
      are impossible; an over-budget `pc_plaintext_alloc` returns nullptr and every
      caller has a defined fail-closed path (WS close 1011, 503, or skip the
      optimization) - never UB, never block. Sizing = worst-case concurrent borrows
      in any single dispatch. This generalizes the existing single-loop-confined
      `crypto_work` pattern (only one SSH KEX runs at a time).
      _Status (done):_ arena core + LIFO mark/release + RAII `PlaintextScope` landed
      (`test_plaintext`; exhaustion + no-accumulate verified); the single-owner debug
      assert (`assert_single_owner` / `xTaskGetCurrentTaskHandle`, per worker) guards
      against a foreign-task borrow; `pc_plaintext_reset()` wired into `server_tick()`.
      Tenants migrated: `ssh_pkt_recv` (its ~2 KB stack buffer removed), `ssh_conn`,
      the OIDC verifier's ~2.6 KB decode buffers, and - the planned final tenant - the
      **permessage-deflate window** (both the outbound `deflate_raw` and inbound
      `inflate_raw` scratch in `websocket.cpp` are `pc_plaintext_alloc`'d, fail-closed on
      exhaustion). Host tests green and esp32dev links.

</details>

## Roadmap & known limitations

Forward-looking feature ideas and the future-work backlog have moved to their own
files so this one stays focused on bugfixes, maintenance, and the record of
shipped work:

- **Future features / backlog:** [ROADMAP.md](ROADMAP.md)
- **Deliberate constraints / caveats:** [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)

## Build / toolchain

<details>
<summary><b>Expand Build / toolchain items</b></summary>

- [x] **`esp32dev` build failed on the official platform (mbedtls v2).** _(done)_
      `ssh_rsa.cpp`'s ARDUINO path now compiles on **both** mbedtls v2 (official
      `espressif32`, Arduino core 2.0.x) and v3 (core 3.x) via
      `MBEDTLS_VERSION_MAJOR` guards around `mbedtls_rsa_init`, `mbedtls_pk_sign`
      (with an `esp_fill_random`-backed `f_rng`), and `mbedtls_rsa_pkcs1_verify`.
      Two further fixes: (1) a missing `intelhex` Python module broke
      `bootloader.bin` (installed into the PlatformIO Python); (2) **latent bug** -
      the ARDUINO [`ssh_rsa_sign()`](@ref ssh_rsa_sign) passed the raw exchange hash `H` to
      `mbedtls_pk_sign()`, which does not re-hash, so it signed `DigestInfo||H`
      instead of `DigestInfo||SHA256(H)` (RFC 8332) and any client would reject
      the host signature; now hashes `H` first to match the native path.
      Verified: `pio run -e esp32dev` compiles all `src/` (incl. SSH) and a full
      firmware links (`pio ci examples/Basic --board esp32dev`: RAM 18.4%,
      Flash 56.3%). `platformio.ini` pins `espressif32 @ ^6.0.0` for
      reproducibility.

</details>

## Security / correctness (high priority)

<details>
<summary><b>Expand Security / correctness (high priority) items</b></summary>

- [x] **Native RSA signing is a `d=1` test stub, not a real signature.** _(done)_
      `ssh_rsa_sign()` native path now performs a full-width `s = em^d mod n`
      via `bn_modexp_full()` (square-and-multiply over every bit of d, reusing
      the correct `bn_mul_full` / `bn_reduce_full` helpers). Validated by
      `test_rsa_sign_verify_roundtrip` with a real 2048-bit private exponent.
      Still software / not constant-time (test-only path; ESP32/mbedTLS is real) - covered by the constant-time item below. CRT was deliberately skipped
      (YAGNI: the native path is test-only, speed is adequate).

- [x] **No authentication attempt limiting (brute-force).** _(done)_
      SSH now bounds failed `USERAUTH_REQUEST`s per connection: the dispatcher
      (`ssh_server.cpp`) counts [`SSH_MSG_USERAUTH_FAILURE`](@ref SSH_MSG_USERAUTH_FAILURE) responses in
      `SshSession.auth_failures` and, after [`SSH_MAX_AUTH_ATTEMPTS`](@ref SSH_MAX_AUTH_ATTEMPTS)
      (`protocore_config.h`, default 6), emits [`SSH_MSG_DISCONNECT`](@ref SSH_MSG_DISCONNECT)
      (reason 14) and closes (RFC 4252 §4). The publickey probe (PK_OK) and a
      SUCCESS do not count. Tested by `test_auth_bruteforce_disconnect` /
      `test_auth_success_after_failures`.
      HTTP Basic needs no separate per-connection counter: `send_unauth()`
      already sends `Connection: close` and tears down the socket on every 401,
      so a client gets exactly one guess per TCP connection. Cross-connection
      (per-IP) throttling is the connection-flood item below.

- [x] **Software crypto paths are not constant-time.** _(done - asserted out of
      firmware)_ The native Montgomery cluster (`ssh_bignum.cpp`: `bn_init`,
      `bn_monpro`, `bn_shl1`, `bn_sub_inplace`, `g14_R1/R2`) is now under
      `#ifndef ARDUINO`, so it is not compiled into firmware at all; the software
      AES (`ssh_aes256ctr.cpp`) and native RSA modexp (`ssh_rsa.cpp`,
      `bn_reduce_full`/`bn_modexp_*`) already live in the `#else` of an
      `#ifdef ARDUINO`. On ESP32 only the HW/mbedTLS paths compile and run.
      Hardening the software paths to constant-time was deliberately skipped
      (YAGNI: they are host-test-only and now provably absent from firmware).
      Documented in `SECURITY.md` (⚠️ timing row).

- [x] **No connection-flood / rate limiting.** _(done - opt-in global throttle)_
      `listener.cpp` now has a fixed-window accept-rate gate
      ([`Tcp.listener->accept_allowed()`](@ref Tcp.listener->accept_allowed)): when [`PC_ENABLE_ACCEPT_THROTTLE`](@ref PC_ENABLE_ACCEPT_THROTTLE) is set,
      the accept callback drops connections beyond
      [`PC_ACCEPT_THROTTLE_MAX`](@ref PC_ACCEPT_THROTTLE_MAX) per [`PC_ACCEPT_THROTTLE_WINDOW_MS`](@ref PC_ACCEPT_THROTTLE_WINDOW_MS)
      (`protocore_config.h`) before claiming a pool slot. Default off (zero
      cost / no behavior change). Two static counters, global across listeners -
      a per-IP table was deliberately not added (YAGNI; the mock PCB carries no
      remote IP and a 1-3 connection device gains little from per-IP state).
      Rollover-safe; tested by `test_accept_throttle*\*`in`test_transport`.
      A per-IP accept throttle was **since added** (round 2,
      [`PC_ENABLE_PER_IP_THROTTLE`](@ref PC_ENABLE_PER_IP_THROTTLE)): a fixed
      BSS bucket table keyed by source IPv4 with a per-address fixed window,
      host-tested in `test_transport`.

- [x] **Slow-loris held the pool indefinitely (slot-exhaustion DoS).** _(done - HW-verified)_
      The accept-rate throttles above cap the connection _rate_, not the _hold time_: a
      slow-loris that dribbles a header byte under the [`CONN_TIMEOUT_MS`](@ref CONN_TIMEOUT_MS)
      idle window kept refreshing `last_activity_ms` and held its slot forever. Added a
      request-**header** read deadline ([`PC_REQUEST_TIMEOUT_MS`](@ref PC_REQUEST_TIMEOUT_MS),
      default 10 s, the nginx `client_header_timeout` semantic): a per-slot `req_start_ms`
      armed on the first RX byte (a trickle cannot reset it); `http_poll_slot` reaps any slot
      still in the header phase (`parse_state < PARSE_BODY`) past the deadline with `408` +
      `Connection: close`. Header-scoped, so a slow body upload (`PARSE_BODY`) is never reaped.
      HW (ESP32-P4): the pool recovers, a real `408` is delivered at ~10 s. See `BUGS.md`.

- [x] **[`pc_base64_decode()`](@ref pc_base64_decode) has no output-capacity guard (Basic-auth ingestion).**
      _(done)_ `pc_base64_decode()` now takes a `dst_cap` parameter
      (`base64.cpp`/`.h`, both platforms) and bounds every write; an over-capacity
      decode returns 0 instead of overrunning. `check_basic_auth()`
      (`protocore.cpp`) passes `sizeof(decoded) - 1`, leaving
      room for the null terminator regardless of how [`MAX_VAL_LEN`](@ref MAX_VAL_LEN)/[`MAX_AUTH_LEN`](@ref MAX_AUTH_LEN)
      are set. Tested by `test_base64_decode_respects_capacity`; all callers
      (WS handshake tests) updated to the new signature.
      Note: this was the only unguarded ingestion path - the HTTP parser
      (indexed bounds + `body[BODY_BUF_SIZE+1]`), multipart (bounded boundary copy
      over a null-terminated body), SSH `read_string()` (capacity-checked), the SSH
      banner ([`SSH_VERSION_MAX`](@ref SSH_VERSION_MAX) + explicit lengths), and the WS handshake
      (`strnlen(client_key, WS_MAX_KEY_LEN+1)`) are all correctly bounded.

- [x] **Connection close/abort is driven from L7 holding the raw `tcp_pcb`.** _(done)_
      The transport now owns the whole teardown: `Tcp.conn->close(slot)` (graceful, was
      `Tcp.conn->close(slot, pcb)`) and the new `Tcp.conn->abort_slot(slot)` (hard RST)
      each detach the pcb, free the per-connection TLS context, reset the slot, and
      then FIN/RST - on a captured pcb pointer, so a late lwIP callback finds a freed
      slot. Every hand-rolled teardown now passes only the slot: the WS/SSE close +
      upgrade-fail sites in `protocore.cpp`, `session.cpp`
      `tls_abort`, and the SSH (x2) / telnet / modbus / opcua drop paths. This also
      fixed a latent pcb leak (the WS/SSE upgrade-alloc-fail paths detached but never
      aborted). Host-tested (`test_observability`: local-close frees the slot,
      abort-slot counts + frees, abort-slot no-ops on a free slot), full native suite
      green, and **HW-soaked on COM3**: HTTP close-path + WS churn (12 `abort_slot`
      RSTs on WS-pool exhaustion) reclaim every slot with no leak and the device keeps
      accepting throughout.

- [x] **`pc_oidc_verify_with_key()` decode buffers moved off the stack.** _(done)_
      The verifier's `hdr[512]` + `sig[PC_OIDC_RSA_BYTES]` + `pl[PC_OIDC_MAX_LEN]`
      + `iss[256]` (~2.6 KB) are now borrowed from the per-dispatch scratch arena under a
      `PlaintextScope` (fail-closed if the arena is exhausted), not stacked. This is
      single-worker-race-safe (the arena has one accessor) where a `static` buffer would
      race concurrent workers. HW-soaked on COM3: a real RS256 verify returns OK with
      `pc_plaintext_high_water == 2624` (exactly the four buffers, now in BSS) and the verify
      compiles + runs under ARDUINO (mbedTLS RSA). `native_oidc` links
      `session/scratch.cpp` + `worker.cpp`; 13/13 OIDC tests still pass.
      _Follow-up:_ the HW soak showed the verify still consumes ~7 KB of **stack** during
      the call - that residual is the **mbedTLS RSA-2048 modexp** itself, not the decode
      buffers. A worker task that runs OIDC verification must be sized for it (or the
      verify marshaled onto a larger-stack task); tracked as the stack-budget item below.

- [x] **RSA-2048 modexp uses ~7 KB of stack (mbedTLS).** _(done - enforced minimum)_
      Measured on COM3: an OIDC RS256 verify (and any `ssh_rsa_verify` path: OIDC, the
      SSH server host key, JWKS) drops the task stack high-water by ~7 KB, dominated by
      the mbedTLS bignum modexp. The "documented minimum worker-stack" option is now
      **enforced at build time**: [`PC_WORKER_STACK_RSA_MIN`](@ref PC_WORKER_STACK_RSA_MIN)
      (default 8192, `protocore_config.h`) is the floor, and a validation `#error`
      fires when `PC_ENABLE_OIDC` or `PC_ENABLE_SSH` is set while
      [`PC_WORKER_TASK_STACK`](@ref PC_WORKER_TASK_STACK) is below it - so a lowered
      worker stack is caught at compile time instead of overflowing on the first verify.
      An advanced build that marshals every RSA verify onto a dedicated larger-stack task
      (the worker itself never runs one) can override `PC_WORKER_STACK_RSA_MIN`. The
      other two options (lower `MBEDTLS_MPI_MAX_SIZE`, or the dedicated task) remain
      available but are not required for the default architecture. Verified: the default
      config and OIDC-on-with-8192 compile; OIDC-on-with-4096 fails with the guard message.

</details>

## SSH protocol completeness (medium)

<details>
<summary><b>Expand SSH protocol completeness (medium) items</b></summary>

- [x] **[`SSH_MSG_UNIMPLEMENTED`](@ref SSH_MSG_UNIMPLEMENTED) not sent for unknown messages.** _(done)_ The
      dispatcher's default case (`ssh_server.cpp`) now emits
      `SSH_MSG_UNIMPLEMENTED` with the rejected packet's sequence number
      (`ssh_pkt[i].seq_no_recv - 1`, since `ssh_pkt_recv` has already advanced the
      counter) per RFC 4253 §11.4 - no handler-signature change needed. Tested by
      `test_unimplemented_reply_for_unknown_message`.

- [x] **SSH channel multiplexing + port-forwarding.** _(channels, direct-tcpip `ssh -L`,
      and forwarded-tcpip `ssh -R` done + HW-verified; X11 a merit-justified non-goal)_
      `ssh_channel.cpp` is a per-connection channel table (`PC_SSH_MAX_CHANNELS`,
      default 1 = the original single channel): up to N concurrent channels per
      connection, each with its own id / window / peer state, every inbound
      `CHANNEL_*` routed to its channel by the recipient id, and the data callback /
      `pc_ssh_conn_send` tagged with the channel id. **direct-tcpip** (`ssh -L`) channels
      now parse + route through a normalized forwarding seam: a channel carries a
      `type` (session / direct-tcpip), `CHANNEL_OPEN` "direct-tcpip" extracts the
      target host:port and consults `pc_ssh_channel_set_forward_open_cb` (opt-in; absent
      = administratively prohibited, refused = connect-failed, accepted = confirmed),
      and forward-channel data routes to `pc_ssh_channel_set_forward_data_cb` instead of
      the session callback. Host-tested (`test_ssh_channel`: independent routing,
      pool-full -> resource shortage, unknown-type, forward open accept/refuse,
      forward-data routing). The **forward owner** (`ssh_forward`, behind
      `PC_SSH_PORT_FORWARD`) does the actual outbound TCP via the `pc_client`
      transport and bridges bytes both ways - no I/O in the codec - with an optional
      target policy, a per-poll target->client pump bounded by the channel window,
      and EOF+CLOSE propagation; `pc_ssh_conn_close_channel` sends a server-initiated
      close as two packets. ESP32 build + link verified.
      **Global requests** (RFC 4254 §4) now have a real handler
      ([`ssh_global_request_handle`](@ref ssh_global_request_handle)): an unrecognized
      request answers `REQUEST_FAILURE` when `want_reply` is set (never `UNIMPLEMENTED` -
      that was a client-keepalive interop bug, see docs/BUGS.md), and **`tcpip-forward` /
      `cancel-tcpip-forward`** (`ssh -R`) route to an opt-in remote-forward seam
      (`pc_ssh_channel_set_rforward_open_cb` / `_cancel_cb`) that replies `REQUEST_SUCCESS`
      (echoing the allocated port for a port-0 bind, §7.1) when an owner accepts. Host-tested
      (`test_ssh_channel`, +7). **`forwarded-tcpip` (`ssh -R`) now fully works**: the owner
      (`ssh_forward`, behind `PC_SSH_PORT_FORWARD`) allocates a real listener via the new
      **tcpip_thread-marshaled** `listener_add_dynamic` / `Tcp.listener->stop_dynamic` (a dynamic
      listener created from the SSH worker task must marshal its raw lwIP `tcp_bind`/`tcp_listen`
      onto tcpip_thread), routes each accepted connection through a `PROTO_SSH_RFWD` handler that
      opens a server-initiated `forwarded-tcpip` channel (`pc_ssh_channel_open_forwarded` +
      CONFIRMATION/FAILURE handling) and bridges bytes both ways with per-poll window-bounded
      pumps; listeners + bridges are torn down on cancel and on SSH disconnect. Host-tested
      (`test_ssh_channel`: open/confirm/failure/inbound-routing) and **HW-verified end-to-end on
      an ESP32-S3** (`ssh -R 8080:localhost:9000` -> a connection to the device's :8080 tunnels
      to the client's :9000 and back, both directions + close propagation).
      _Not pursued (merit-justified non-goals):_ **X11 forwarding** - server-side X11 forwards TCP
      connections that X11-*client* programs running ON the server open to the forwarded display, and
      a headless embedded SSH server runs no such programs, so there is nothing to forward; the
      `x11-req` channel request is conformantly refused (`CHANNEL_FAILURE`, covered by
      `test_unknown_request_failure`). A per-request **bind-port policy** is already expressible through
      `pc_ssh_channel_set_rforward_open_cb` (return the bound port, or < 0 to refuse a requested port).
      If a real X11 use case ever appears, the opt-in-seam pattern used for direct-tcpip / forwarded-tcpip
      is the extension path.

- [x] **Per-direction NEWKEYS.** _(done)_ The single `ssh_pkt[i].encrypted` flag is
      split into `enc_out` (outbound) and `enc_in` (inbound), tracked independently per
      RFC 4253 sec 7.3. `ssh_newkeys_sent()` turns on the outbound cipher/MAC (+ the s2c
      compression stream) the moment the server emits its NEWKEYS; `ssh_newkeys_complete()`
      turns on the inbound cipher/MAC when the peer's NEWKEYS arrives. The send path (pack)
      reads `enc_out`, the receive path (unpack) reads `enc_in`, so a strict peer that
      activates its send direction before we activate ours is handled. Wire-equivalent on
      the happy path; the 145 `native_ssh` cases (incl. the full KEXINIT->NEWKEYS->SERVICE
      handshake with real crypto + rekey) pass, and it compiles for the ESP32 target.

- [x] **Key-derivation extension (RFC 4253 §7.2).** _(done)_ `ssh_kdf_derive()`
      produces any length up to `SSH_KDF_MAX` (4 blocks) via the `K1‖K2‖…` chain
      (Ki+1 = HASH(mpint(K) ‖ H ‖ K1..Ki)); `derive_key()` is now the 32-byte wrapper,
      so the existing KEX is byte-identical (all negotiated algorithms still use one
      block). Host-tested: `test_ssh_crypto` `test_ssh_kdf_extension_chain` verifies K1
      equals the single-block derive and K2 chains correctly.

- [x] **Session rekeying (RFC 4253 §9).** _(done)_ A server-initiated re-key now fires from
      `pc_ssh_conn_poll()` when either the volume budget (`SSH_REKEY_PACKET_THRESHOLD`, a packet-count
      proxy for ~1 GB) or the time budget (`SSH_REKEY_TIME_MS`, default 1 h) since the last KEX is
      spent, on an authenticated channel that is not already re-keying: it emits a fresh KEXINIT via the
      existing `ssh_transport_begin_rekey()`, and the KEXINIT dispatch carries it to completion (session
      + auth preserved). The decision is a pure, host-tested helper `ssh_rekey_due()`
      (`test_rekey_due_volume_and_time`), the timer resets in `ssh_newkeys_complete()` off the pluggable
      clock, and the sequence-number-wrap close remains the last-resort fallback. So a long-lived /
      high-throughput session re-keys in place instead of being dropped.

- [~] **Compression (RFC 4253 §6.2).** _(both directions implemented; c2s HW interop pending)_
      `PC_ENABLE_SSH_ZLIB` adds `zlib@openssh.com` / `zlib` for **both** directions: a
      context-takeover DEFLATE stream (`ssh_zlib`, persistent sliding window + per-packet
      sync-flush + zlib wrapper) for **s2c**, and a resumable, context-takeover INFLATE
      (`ssh_inflate`, 32 KB window) for **c2s** - both owned by `ssh_comp`, negotiated per
      direction, started after USERAUTH_SUCCESS (delayed) or NEWKEYS (plain), and driven by the
      packet layer (compress on send, decompress on receive). The c2s inflate handles OpenSSH's
      `Z_PARTIAL_FLUSH` (blocks end mid-byte, spilling into the next packet) by carrying the bit
      position + window across packets and committing only whole blocks (it retains the flush-block
      tail and re-decodes it next packet - no mid-block state). s2c is HW-verified against OpenSSH
      10.3 (42 KB byte-perfect); c2s is verified byte-exact against golden vectors from **real zlib**
      (windowBits 15, `Z_PARTIAL_FLUSH`; `native_ssh_inflate` + `native_ssh_comp`,
      tools/gen_ssh_inflate_vectors.py). The SSH-example firmware with `PC_ENABLE_SSH_ZLIB=1` +
      `PC_SSH_ZLIB_ACK_DRAM=1` + `MAX_SSH_CONNS=1` **build-verified on ESP32-S3** (fits internal
      DRAM: 948 KB flash / 182 KB globals, 145 KB free; the classic-ESP32 ~122 KB DRAM ceiling
      overflows, so the S3/P4 or the PSRAM-BSS core is required). _Only remainder:_ live interop of
      the c2s path against a real OpenSSH client (`ssh -o Compression=yes` + a c2s upload, echoing
      back to confirm byte-exact decompression) - needs a free S3/P4 rig (RPi rigs currently
      unplugged; COM7/COM9 are shared GNSS / SSH-interop boards).

</details>

## Performance / hardware acceleration (medium)

<details>
<summary><b>Expand Performance / hardware acceleration (medium) items</b></summary>

- [x] **SSH per-packet HMAC ran in software SHA-256 on ESP32.** _(done; on-device
      verification pending a board connection)_ The streaming SHA-256 context is
      now backed by `mbedtls_sha256_context` on Arduino
      (`mbedtls_sha256_starts/update/finish`, v2/v3-guarded), so the HW SHA engine
      accelerates per-packet HMAC **and** KEX hashing. The software FIPS-180-4 path
      is now compiled only on native (`#ifndef ARDUINO`). The `crypto/hmac_sha256.cpp`
      HW-acceleration comment is now accurate. Native software KATs still pass;
      `examples/SSHCryptoSelfTest` validates the HW path on-device.

- [x] **AES-256-CTR re-acquired the HW engine once per 16-byte block.** _(done)_
      The Arduino [`ssh_aes256ctr_crypt()`](@ref ssh_aes256ctr_crypt) now makes a single
      `mbedtls_aes_crypt_ctr()` call for the whole buffer (our `counter` /
      `keystream` / `pos` fields map 1:1 to mbedtls's `nonce_counter` /
      `stream_block` / `nc_off`), replacing the per-block
      `mbedtls_aes_crypt_ecb()` loop. Native software path unchanged. Validated by
      the native AES-CTR KATs and `examples/SSHCryptoSelfTest` on-device.

- [x] **DMA UART / I2C / SPI transfer (v5 milestone).** DONE - `services/dma`
      (`PC_ENABLE_DMA`). Channels move peripheral bytes to a static ping-pong (RX) /
      staging (TX) buffer; a DMA-complete event carries the bytes to a callback that
      posts them into the preempting task queue. Zero heap, fail-closed. The
      ingress/egress **simulator** (`PC_DMA_SIMULATE`, default on) exercises the whole
      pipeline with no physical loopback - on the host bench and on-device; a real silicon
      driver plugs into the `pc_dma_hw_*` hooks. Host-tested (`native_dma`, 11 cases) +
      HW-verified (example `DmaIngest`; and a combined webserver + continuous-DMA rig
      ingested 2.2M+ frames with zero integrity errors under HTTP stress, no heap growth).
      Remaining: the real UHCI-UART / `spi_master`-DMA silicon backend (needs peripheral
      hardware to verify; the seam is in place).

- [x] **User-configurable preempting task queue (v5 milestone).** DONE -
      `services/preempt_queue` (`PC_ENABLE_PREEMPT_QUEUE`). Producers post from a task
      (`xQueueSendToBack` / `-Front`, wait timeout) or an ISR (`xQueueSendFromISR` +
      `portYIELD_FROM_ISR`); the scheduler preempts to the processing task immediately.
      Task priority + core are user-settable at `pc_pq_start[_lane]()`, depth is
      compile-time. **Named lanes**: one USER lane exposed to the app (no-arg `pc_pq_*`)
      plus internal DMA / FORWARD / DEVICE lanes that run above it (DMA highest, below
      tcpip / WiFi), so internal ingest preempts user work. Host-tested
      (`native_preempt_queue`, 11 cases) + HW-verified (DMA + USER lanes ran continuously
      with zero errors under an HTTP flood; examples PreemptQueue + PreemptLanes).

- [x] **Interface forwarding (v5 milestone), DMA-driven.** DONE - `services/forward`
      (`PC_ENABLE_FORWARD`). A forwarding plane: register interfaces (each with an
      egress send callback), add per-pair rules (allow / deny + rate cap); a frame on one
      interface (`pc_forward_ingress()`, wired from a DMA-complete event on the FORWARD
      lane) is forwarded to every allowed destination, so the device bridges / routes
      instead of only terminating traffic. Default-deny, never reflects to the source,
      fail-closed (exceeded cap / refused send drops and is counted), multi-destination
      fan-out. Zero-heap static tables. Host-tested (`native_forward`, 10 cases) +
      HW-verified (600k+ frames ingested over DMA and forwarded to a second interface with
      zero loss / zero integrity errors under an HTTP flood; example InterfaceForward).
      This is the generic data path the post-v5 wireless gateway bridges sit on top of.

- [~] **Post-v5 southbound bridges + sensing (backlog).** The **generic gateway
      framework is DONE** - `services/gateway` (`PC_ENABLE_GATEWAY`): ports,
      address-aware northbound enveloping + topic, bidirectional up/down-link, per-port
      rate cap, stats; fail-closed, zero-heap, HW-verified end to end over DMA + the
      FORWARD lane (example RadioGateway). The per-module **codec + driver** plugins
      on top of it are now nearly all shipped: RF / wireless **gateway bridges** (LoRa,
      nRF24, CC1101, Thread over SPI; Zigbee, Z-Wave, EnOcean, Sigfox over UART; NFC over
      I2C/SPI/UART; BLE ATT/GATT) - all codec-shipped + host-tested; **promiscuous /
      monitor capture** (Wi-Fi raw 802.11, CAN bus listen-only, radio channel sniff to
      802.15.4-TAP pcap) shipped; and **field-perturbation sensing** (LD2410 mmWave radar,
      FDC2214 capacitive, LDC1614 inductive, VL53L0X ToF) shipped. _Remaining:_ Wi-SUN FAN
      (blocked on a devboard choice), analog Doppler + MR60BHA 60 GHz radar variants, and
      the real-module HW verification of each. See
      [ROADMAP.md](ROADMAP.md#post-v5-rf--wireless-gateway-bridges).

</details>

## HTTP / core (medium)

<details>
<summary><b>Expand HTTP / core (medium) items</b></summary>

- [x] **TLS (HTTPS).** _(done)_ Opt-in mbedTLS server on a fixed static arena
      ([`PC_ENABLE_TLS`](@ref PC_ENABLE_TLS)) - see the HTTPS / TLS item under
      Optional services, plus mTLS, `wss://` / TLS-SSE, and RFC 5077 session
      resumption.

- [x] **`Date` response header** _(done, opt-in)_ - [`PC_HTTP_EMIT_DATE`](@ref PC_HTTP_EMIT_DATE)
      (default off, so the hot path is unchanged unless enabled) auto-injects
      `Date: <IMF-fixdate>` into every dynamic response once a wall-clock time exists
      ([`pc_ntp_http_date()`](@ref pc_ntp_http_date) non-empty); a clock-less / pre-sync device omits it
      (RFC 7231 §7.1.1.2). Host-tested via a time-injection seam
      (`test_response_headers`: emitted-when-set / omitted-when-clockless) and HW-verified
      with NTP (`Date: Mon, 29 Jun 2026 ... GMT`). Apps can still add it from a handler.

- [x] **Recv scratch off the stack.** _(done)_ `ssh_pkt_recv`'s per-packet
      plaintext buffer (`SSH_PKT_BUF_SIZE + SSH_HMAC_SHA256_LEN`, ~2 KB) moved from
      the stack into the shared per-dispatch scratch arena
      (`server/mmgr/scratch.*`), borrowed under an RAII `PlaintextScope`
      so it is reclaimed on every exit path and reused (not accumulated) across
      packets in one call. See the shared scratch-pool item under Round 2.

</details>

## Optional services / features (toggleable, default off)

<details>
<summary><b>Expand Optional services / features (toggleable, default off) items</b></summary>

Capabilities a small IoT web server commonly needs but the library does not yet
provide. Each should follow the existing feature-flag convention - a
`PC_ENABLE_*` macro defaulting to 0, gating its own `.cpp`/pool so it costs
no code, RAM, or flash when disabled (`protocore_config.h`). Roughly ordered
by how often a deployed device needs it.

- [x] **mDNS / DNS-SD advertisement ([`PC_ENABLE_MDNS`](@ref PC_ENABLE_MDNS)).** _(done)_
      `pc_mdns_begin(hostname, port)` (`src/services/mdns_service.*`) makes the
      device reachable at `<hostname>.local` and advertises `_http._tcp`. Uses the
      **ESP-IDF `mdns` component directly** (not the `ESPmDNS` add-on) to keep the
      dependency set to base-SDK + mbedTLS. Firmware links (`examples/mDNS`).

- [x] **OTA firmware update ([`PC_ENABLE_OTA`](@ref PC_ENABLE_OTA)).** _(done)_ Authenticated
      streaming `POST /update` into the ESP32 `Update` API
      (`src/services/ota_service.*`). The HTTP parser gained a `#if PC_ENABLE_OTA`
      streaming-body hook (`http_parser_set_stream_hooks`) that feeds the image to
      `Update.write()` in [`BODY_BUF_SIZE`](@ref BODY_BUF_SIZE) chunks instead of buffering it - so the
      `BODY_BUF_SIZE`/413 cap is bypassed and multi-MB images never live in RAM.
      The matching route handler replies + reboots. Parser hook native-tested
      (`test_http_ota`, env `native_ota`) with **no regression** to the 80 parser
      tests (fully gated); `examples/OTA` firmware links.

- [x] **WiFi provisioning / captive portal ([`PC_ENABLE_PROVISIONING`](@ref PC_ENABLE_PROVISIONING)).** _(done)_
      `src/services/provisioning_service.*`: first-boot softAP + a catch-all DNS
      responder + a credentials form, persisting SSID/PSK to NVS
      (`pc_provisioning_load`/`_begin`/`_clear`, `examples/Provisioning`).
      The DNS responder is a **raw lwIP UDP socket** (no `DNSServer` add-on) -
      callback-driven, so no per-loop polling. The form-field/URL-decode parser is
      native-tested (`test_provisioning`, env `native_prov`); firmware links.

- [x] **Pre-compressed static asset serving.** _(done)_ `serve_static()` serves
      `<path>.gz` with `Content-Encoding: gzip` when the client sends
      `Accept-Encoding: gzip` and the `.gz` exists (original Content-Type
      preserved). No separate flag needed - it is zero-cost when no `.gz` is
      present. Tested by `test_serve_static_gzip_when_accepted` /
      `test_serve_static_no_gzip_when_not_accepted`.

- [x] **Conditional GET / ETag ([`PC_ENABLE_ETAG`](@ref PC_ENABLE_ETAG)).** _(done)_ `serve_file()` /
      `serve_static()` emit a strong `ETag` (`"<hexsize>-<hexmtime>"` from
      `f.size()` + `f.getLastWrite()`) and answer a matching `If-None-Match` with
      `304 Not Modified` (no body). The FS test mock gained `getLastWrite()` so it
      is host-tested (`test_serve_static_etag_conditional_get`); ESP32 path
      compile-verified against the real `fs::File`. Now also emits a `Last-Modified`
      date and honors `If-Modified-Since` (per RFC 9110, only when no `If-None-Match`
      is present); with no wall clock the date validator is skipped and the ETag
      validator still works.

- [x] **SNTP time sync (`PC_ENABLE_NTP`).** _(done)_ [`pc_ntp_begin()`](@ref pc_ntp_begin)/
      `_synced()`/`_epoch()`/`_http_date()` (`src/services/pc_ntp_service.*`) wrap
      `configTzTime` (ESP-IDF SNTP) and format an RFC 7231 `Date`. `examples/SNTP`
      exposes `GET /time`; firmware links. (Auto-emitting the `Date` response
      header is left to the app via the helper - kept off the hot path.)

- [x] **Multi-source time fallback ([`PC_ENABLE_TIME_SOURCE`](@ref PC_ENABLE_TIME_SOURCE)).**
      _(done)_ A zero-heap registry of user-defined time sources
      (`src/services/timing_position/time_source/time_source.*`): each source is a callback
      returning Unix epoch seconds (0 = no valid time), registered with a priority.
      `pc_time_now()` queries them in ascending priority and returns the first
      valid result (stopping early so a costly lower-priority read is skipped), so
      the device falls back automatically (e.g. GPS fix lost -> RTC -> NTP);
      `pc_time_source_active()` reports which source answered. Host-tested
      (`native_time_source`, 9 cases) with mock sources; example
      `TimeSourceFallback` (NTP preferred, RTC fallback); esp32dev links.

- [x] **Zero-copy template slicing.** _(addressed by design)_
      [`send_template()`](@ref PC::send_template) never buffers the
      expanded body: it walks the template twice (size, then stream each literal
      run and resolved `{{name}}` value straight to the socket), and placeholder
      names over 32 chars are emitted literally - so there is no fixed expansion
      slot to overflow. _Follow-up:_ apply the same resolver path to static-file
      serving.

- [x] **JSON request/response helper.** _(done)_ Zero-heap `JsonWriter`
      (formats into a caller buffer with automatic comma/escaping and a
      `JSON_MAX_DEPTH` nesting cap; overflow flips `ok()` and truncates safely)
      plus top-level object readers `json_get_str()`/`json_get_int()`/
      `json_get_bool()` (`src/network_drivers/presentation/json.*`). ArduinoJson stays optional (it
      heap-allocates). Tested by `test_json` (28); example `Json`.
      _Follow-up (done):_ string unescaping now decodes `\uXXXX` to UTF-8 (1-4
      bytes) and joins UTF-16 surrogate pairs into astral code points; an unpaired
      surrogate becomes U+FFFD and malformed/short hex becomes `?`, with a clean
      truncation when a code point's UTF-8 sequence would not fit (`json.cpp`).

- [x] **Web "serial" terminal ([`PC_ENABLE_WEB_TERMINAL`](@ref PC_ENABLE_WEB_TERMINAL)).**
      _(done)_ A WebSerial-style browser terminal over the existing WebSocket
      layer (`src/services/web_terminal.*`): serves a self-contained CRT-themed
      page + a WebSocket endpoint, broadcasts device output to all browsers, and
      delivers typed lines to a command callback - all zero-heap. Tested by
      `test_web_terminal` (7); example `WebTerminal`.

- [x] **HTTPS / TLS ([`PC_ENABLE_TLS`](@ref PC_ENABLE_TLS)).** _(done)_
      Opt-in mbedTLS on a static memory pool (`src/network_drivers/tls/pc_tls.*`):
      all mbedTLS allocations come from a fixed BSS arena
      (`PC_TLS_ARENA_SIZE`, default 48 KB) via
      `mbedtls_platform_set_calloc_free()`, so the zero-heap guarantee holds. HW
      CSPRNG RNG; BIO bridged to the raw `tcp_pcb` + rx ring; handshake pumped in
      the session loop. `begin_tls(port, cert, …)` / [`listen_tls()`](@ref PC::listen_tls).
      HW-verified: `ECDHE-ECDSA-AES256-GCM-SHA384`, TLS 1.2+. See SECURITY.md §6.
      Example `HTTPS`. `wss://` + TLS-SSE now run over the same record layer,
      and **session resumption** shipped (RFC 5077 tickets,
      [`PC_ENABLE_TLS_RESUMPTION`](@ref PC_ENABLE_TLS_RESUMPTION), example
      `TlsResumption`). _Still open:_ `MAX_TLS_CONNS` > 1 (needs smaller IDF
      record buffers) and client-side resumption.

- **SNMP agent v1 / v2c / v3.** Zero-heap ASN.1 BER codec + a fixed MIB
      (OID table) over a raw lwIP UDP socket, GET / GETNEXT / GETBULK / SET.
      Shared base (codec + PDU + MIB) is native-testable.
  - [x] BER codec (RFC indefinite-free definite-length TLV): INTEGER, OCTET
        STRING, NULL, OID (base-128), SEQUENCE, and the SNMP application types.
        `src/services/net/snmp/pc_snmp_ber.*`, KAT-tested (`env:native_snmp`).
  - [x] v1/v2c agent (community-string access, RFC 1157 / 3416): GET / GETNEXT /
        GETBULK / SET dispatch over a fixed MIB-II-style table, per-varbind v2c
        exceptions (`noSuchObject`/`endOfMibView`) and v1 error-status/-index,
        SET gated by a separate read-write community. `pc_snmp_agent_process()` is a
        pure, host-testable core (13 tests); the transport-layer UDP service
        (`Udp.listener->listen`) on :161 carries datagrams (the same service the
        provisioning DNS responder uses). `pc_snmp_agent_*` API, example `SNMP`.
        **HW-verified** with a UDP client: `snmpget`/walk of the system group in
        OID order, GetBulk, dynamic Gauge32, SET authorization (RO→noAccess,
        RW→success), v1 `noSuchName`, and unknown-community drop all behave per
        net-snmp.
  - [x] v3 (USM, RFC 3414): gated behind `PC_ENABLE_SNMP_V3` (default off).
        Auth = `usmHMAC192SHA256` (HMAC-SHA-256, 24-byte; RFC 7860, reusing the
        SSH SHA-256/HMAC), privacy = `usmAesCfb128` (AES-128-CFB, RFC 3826 - a
        compact portable AES added in `pc_snmp_crypto`). Implements the v3 message
        framing (msgGlobalData + msgSecurityParameters + scopedPDU), engine
        discovery (Report `usmStatsUnknownEngineIDs`), the timeliness window
        (engineBoots/engineTime; boots persists via `pc_snmp_v3_set_boots()` from
        NVS), USM error Reports (unknownUserNames / wrongDigests /
        notInTimeWindows / decryptionErrors), and key localization (RFC 3414
        §2.6). `pc_snmp_v3_*` API; `pc_snmp_v3_process()` reuses the shared
        [`pc_snmp_dispatch_pdu()`](@ref pc_snmp_dispatch_pdu) MIB core. Native tests
        (`env:native_snmp_v3`): SHA-256 localization KAT (hashlib-grounded),
        AES-128 FIPS-197 KAT, and the full discovery -> authNoPriv -> authPriv
        flow. **HW-verified** against an independent manager (pycryptodome AES +
        Python hashlib/hmac): authNoPriv + authPriv GET/SET and the error Reports
        interoperate byte-for-byte over real UDP. Example `SNMP` (set the
        flag to enable the user). _Follow-up:_ derive the engine ID from the chip
        MAC; persist engineBoots across reboots.

- [x] **Telnet console ([`PC_ENABLE_TELNET`](@ref PC_ENABLE_TELNET)).**
      _(done)_ Minimal RFC 854 line-oriented Telnet server dispatched from the
      session layer's `PROTO_TELNET` arm
      (`src/network_drivers/presentation/telnet.*`): negotiates server echo +
      suppress-go-ahead (character mode), accumulates a line with backspace
      handling, hands each completed line to a command callback, and can push
      output to all connected clients. Plaintext - no auth or encryption, so use
      it only on a trusted LAN (prefer SSH or the WebSocket terminal otherwise).
      Example `Telnet`.

- [x] **JWT bearer auth ([`PC_ENABLE_JWT`](@ref PC_ENABLE_JWT)).** _(done)_
      Stateless `Authorization: Bearer <jwt>` verification, HS256
      (HMAC-SHA-256, reusing the SSH crypto layer), constant-time signature
      compare, all in fixed stack/BSS - no sessions, no heap
      (`src/services/security/jwt/*`). Host-tested (`native_jwt`); example `JWTAuth`.
      **Time claims now enforced (opt-in via the caller's clock):** the `*_at`
      variants ([`pc_jwt_time_valid`](@ref pc_jwt_time_valid),
      [`pc_jwt_verify_hs256_at`](@ref pc_jwt_verify_hs256_at),
      [`pc_jwt_bearer_valid_at`](@ref pc_jwt_bearer_valid_at)) reject on `exp` (RFC 7519
      §4.1.4) and `nbf` (§4.1.5) with a skew leeway, given `now = (long)pc_time_now()`
      (`PC_ENABLE_NTP` / any time source); passing `now = 0` on a clockless device
      skips the time check so the signature still gates. `iat` is informational
      (read via `pc_jwt_claim_int`). The base signature-only functions are unchanged.
      _Out of scope:_ RS256/ES256 (asymmetric, allocation-heavy).

- [x] **Remote syslog ([`PC_ENABLE_SYSLOG`](@ref PC_ENABLE_SYSLOG)).**
      _(done)_ RFC 5424 log lines shipped as UDP datagrams via the transport UDP
      service (`src/services/net/syslog/*`): a pure host-testable `pc_syslog_format()`
      builds one line into a caller buffer, an ESP32-only `pc_syslog_log()` sends it.
      Host-tested (`native_syslog`); example `Syslog`.

- [x] **Streaming file upload ([`PC_ENABLE_UPLOAD`](@ref PC_ENABLE_UPLOAD)).**
      _(done)_ A `POST` route streams its body straight into a file on an Arduino
      FS (LittleFS/SPIFFS/SD) in `FILE_CHUNK_SIZE` pieces - the upload never has to
      fit in RAM (`src/services/upload_service.*`). Reuses the parser's
      streaming-body hook. Example `FileUpload`. _Constraint:_ only one streaming
      sink exists, so `PC_ENABLE_UPLOAD` and [`PC_ENABLE_OTA`](@ref PC_ENABLE_OTA)
      share it - enable at most one per build.

- [x] **WebSocket permessage-deflate - Phase 1 (inbound)**
      (`PC_ENABLE_WS_DEFLATE`, RFC 7692). _(done)_ The handshake negotiates
      `permessage-deflate` with `client_no_context_takeover; server_no_context_takeover`,
      so each message decompresses independently. A compressed message (RSV1 on its
      first frame) is INFLATEd before delivery by a hand-rolled bounded RFC 1951
      decompressor (`network_drivers/presentation/inflate.*`) whose Huffman tables
      are borrowed from the shared per-dispatch scratch arena - no per-connection
      buffer, and the output buffer doubles as the LZ77 window (no separate 32 KB
      window). Both the compressed input and the decompressed output must fit
      `WS_FRAME_SIZE`; a malformed stream closes 1002. Outbound frames stay
      uncompressed (§6 permits). Host-tested: `native_inflate` (12 cases, vectors
      grounded against zlib) + `native_ws_deflate` (handshake / RSV1 / delivery);
      esp32dev links; example `WebSocketCompression`. _HW test pending a board._
  - [x] **Phase 2 - outbound compress _(shipped)_.** A bounded fixed-Huffman DEFLATE
        encoder compresses outbound data frames (RSV1) under `PC_ENABLE_WS_DEFLATE`,
        with an uncompressed fallback when the result would not shrink. permessage-deflate
        is now bidirectional (see ROADMAP "WebSocket permessage-deflate, inbound and
        outbound"); host-tested via `native_deflate` + `native_ws_deflate`.

(Deliberately omitted as not worth the footprint for this class of device: none
currently. WebSocket permessage-deflate - previously omitted - now ships its
inbound half; see above.)

</details>

## Quality-of-life (developer / operator)

<details>
<summary><b>Expand Quality-of-life (developer / operator) items</b></summary>

Convenience that does not add protocol capability but removes friction. Newbie
items lower the floor for first-time users; operator items help whoever runs a
deployed device.

Newbie / developer experience:

- [x] **One-call static directory mount.** _(done)_
      `serve_static(url_prefix, fs, fs_root)` (`protocore.h`)
      mounts a filesystem subtree at a URL prefix via a wildcard `ROUTE_STATIC`:
      `index.html` fallback for `/` or directory requests, MIME auto-detection,
      gzip-static, path-traversal rejection, GET/HEAD only (else 405). Tested by
      the `test_serve_static_*` suite.

- [x] **MIME type auto-detection by extension.** _(done)_ `PC::mime_type(path)` - a static, case-insensitive extension→type table (html/css/js/json/svg/
      png/jpg/gif/ico/webp/wasm/woff2/… → falls back to
      `application/octet-stream`). Used automatically by `serve_static()` and
      callable directly with `serve_file()`. Tested by `test_mime_type_detection`.

- [x] **Named `begin()` failure codes.** _(done)_ `begin()`/[`listen()`](@ref PC::listen)/[`restart()`](@ref PC::restart)
      now return a [`pc_result`](@ref pc_result) enum: [`PC_OK`](@ref PC_OK), [`PC_ERR_NO_LISTENERS`](@ref PC_ERR_NO_LISTENERS),
      [`PC_ERR_LISTENER_FULL`](@ref PC_ERR_LISTENER_FULL), [`PC_ERR_LISTEN_FAILED`](@ref PC_ERR_LISTEN_FAILED)
      (`protocore.h`/`.cpp`). Subsumes the heap-bytes mismatch
      item below (docstring corrected).

- [x] **`redirect()` helper.** _(done)_ `server.redirect(slot_id, code, location)`
      sends a `Location` header + empty body and closes; accepts 301/302/303/307/308
      (any other code → 302). Tested by `test_redirect_*`.

Operator / sysadmin:

- [x] **Runtime stats endpoint ([`PC_ENABLE_STATS`](@ref PC_ENABLE_STATS)).** _(done)_ `server.stats(slot)`
      emits a JSON snapshot - uptime, total requests, 2xx/4xx/5xx counts, active
      connection-pool slots, and free heap. Counters are maintained centrally in
      `note_response()` (the single funnel through which `send`/`send_empty`/
      `redirect`/`serve_file` report each response). Tested by
      `test_stats_endpoint_emits_json`.

- [x] **Per-request log callback hook.** _(done)_ `server.on_request_log(cb)`
      (`RequestLogCb`) fires once per response with method/path/status/body-length,
      via the same `note_response()` funnel. Pure hook - one function pointer, no
      in-library buffering. Always compiled (no flag). Tested by
      `test_request_log_hook_fires`.

</details>

## Examples (low)

<details>
<summary><b>Expand Examples (low) items</b></summary>

- [x] **`begin()` heap-bytes contract mismatch.** _(done)_ The misleading
      "abs(result) == heap bytes needed" docstring/example was corrected; `begin()`
      now returns a `pc_result` code (see the named-failure-codes item
      above). The `heap_needed()`/`heap_available()` no-op shims were removed in
      v4.0.0 (the library makes no heap allocations).

</details>

## Housekeeping (low)

<details>
<summary><b>Expand Housekeeping (low) items</b></summary>

- [x] **Native `pc_base64_decode()` accepts `=` outside the trailing pad.** _(done)_
      `b64_val()` no longer treats `=` as a value; the decoder validates padding
      positionally - full 4-char quads only, `=` permitted only as 1-2 trailing
      chars of the final quad (`base64.cpp`). Misplaced padding and non-multiple-
      of-4 input now return 0. Tested by `test_base64_decode_rejects_misplaced_padding`.

- [x] **`test/test_application/` is orphaned** _(done)_ - wired into the
      `native_application` env's `test_filter` (`platformio.ini`) and de-bit-rotted (it
      called the removed `DeterministicAsyncTCP::init(80)`; now [`pool_init()`](@ref DeterministicAsyncTCP::pool_init)).
      All 35 cases pass.

- [x] **`docs/CHANGELOG.md` upkeep.** _(done - automated)_ Generated and
      committed by the `changelog.yml` workflow (`chore: update CHANGELOG.md
      [skip ci]`), so it tracks each cycle without a manual pass.

- [x] **Add an SSH usage example** _(done)_ - `examples/SSH/SSH.ino`:
      enables SSH, loads the host key from NVS ([`pc_ssh_rsa_load_pubkey()`](@ref pc_ssh_rsa_load_pubkey)), installs
      password + publickey auth callbacks and a channel data callback that echoes
      via the new [`pc_ssh_conn_send()`](@ref pc_ssh_conn_send) helper, listens on [`PROTO_SSH`](@ref PROTO_SSH). Required a
      small public outbound API (`pc_ssh_conn_send()`, `ssh_conn.*`) since the
      dispatcher's emit path was internal-only.

- [x] **Publish RSA host-key provisioning docs** _(done)_ - `docs/SSH.md`
      now has a "Host key provisioning" section: `openssl genrsa` →
      `pkcs8 -topk8 -outform DER`, embed + write to NVS (`ssh_host_key/priv_der`)
      with `Preferences`, and `pc_ssh_rsa_load_pubkey()` at boot.

</details>
