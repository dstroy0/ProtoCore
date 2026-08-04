# Test Suite

Welcome to the testing documentation for `ProtoCore`. This repository is designed to be extremely robust, employing **100% hardware-free, deterministic testing**.

Whether you are a beginner looking to understand how C++ testing works or an expert systems engineer designing secure, high-concurrency embedded protocols, this guide explains the architectures, methodologies, and concepts behind our test suite.

---

## 1. Introduction & Core Philosophy

### Why Native Testing?

Traditionally, testing code written for microcontroller frameworks like ESP-IDF or Arduino requires uploading binaries to physical ESP32 chips. This hardware-in-the-loop (HIL) testing has several drawbacks:

- **Slow feedback cycles**: Compiling, flashing, and rebooting microcontrollers takes minutes.
- **Flakiness**: Wireless connections fail, hardware pins float, and components experience wear.
- **Hard-to-reproduce bugs**: Multi-threaded concurrency bugs or network timing jitter cannot be reliably reproduced on physical chips.

`ProtoCore` solves this by executing all test suites **natively** on your development machine (x86/x64 host).

### The Deterministic Asynchronous Model

This server is built on cooperative multitasking. Instead of physical threads, it uses a single-threaded event-driven event loop. Because of this, we can make tests **100% deterministic** through **Time-Travel Mocking**.

Instead of waiting for real-world seconds to elapse to test a connection timeout, the test suite manually increments a virtual clock (`millis()`) and drives the state machine forward manually. This means:

- A 5-second connection timeout can be tested in **less than a millisecond**.
- Execution order is guaranteed to be identical on every single run, eliminating race-condition flakiness.

```mermaid
graph TD
    A[Real Time] -->|Cannot control| B[Physical Hardware]
    C[Virtual Time Mocks] -->|Deterministic Control| D[Native C++ Event Loop]
    D -->|Simulate Events| E[Test Cases]
    E -->|Assert State| F[Pass/Fail]
```

---

## 2. Test Architecture & Mocking Strategies

To isolate our application code from physical hardware and the operating system's IP stack, we use a custom mocking layer.

### Mocks, Stubs, and Spies

- **Stubs**: Provide canned answers to calls made during the test. For example, our **Filesystem Stub** simulates an SPIFFS/LittleFS system by feeding static file contents from memory arrays instead of reading from a physical hard drive.
- **Mocks**: Objects pre-programmed with expectations that form a specification of the calls they are expected to receive.
- **Virtual Network Taps**: We mock the network stack completely. Instead of binding to real network sockets, we hook the server directly into virtual byte-pumps (ring buffers) that simulate incoming TCP packets.

```
       +---------------------------------------------+
       |                  TEST SUITE                 |
       +----------------------++---------------------+
                              || Simulates network packets
                              \/
       +---------------------------------------------+
       |             VIRTUAL TRANSPORT               |
       |  (mocks sockets, ring buffers, timeouts)    |
       +----------------------++---------------------+
                              || Drives HTTP/SSH bytes
                              \/
       +---------------------------------------------+
       |            CORE WEB SERVER ENGINE           |
       |     (HTTP parser, WebSockets, SSH)          |
       +---------------------------------------------+
```

---

## 3. PlatformIO Test Environments

<!-- BEGIN GENERATED test-environments (edit test/test_matrix.json, run test/gen_test_readme.py) -->

The native test matrix has **307 environments**, one per feature, generated from [test_matrix.json](test_matrix.json) into [platformio.ini](../platformio.ini) by [gen_test_envs.py](gen_test_envs.py). Each compiles a strict per-feature slice of `src/` with its own flags and runs that feature's suite in isolation, so "this feature builds and tests on its own" stays guaranteed.

| Environment | Feature flag(s) | Test suite(s) | Purpose |
| :--- | :--- | :--- | :--- |
| `native_accept_gate` | `PC_ENFORCE_HOST_HEADER=0`, `PC_ENABLE_ACCEPT_THROTTLE=1`, `PC_ENABLE_PER_IP_THROTTLE=1`, `PC_ENABLE_IP_ALLOWLIST=1`, `PC_ACCEPT_THROTTLE_MAX=3`, `PC_ACCEPT_THROTTLE_WINDOW_MS=1000`, `PC_PER_IP_THROTTLE_MAX=2`, `PC_PER_IP_THROTTLE_WINDOW_MS=1000`, `PC_PER_IP_THROTTLE_SLOTS=4`, `PC_IP_ALLOWLIST_SLOTS=4` | `test_accept_gate` | Accept-time connection gates with their flags ON (PC_ENABLE_ACCEPT_THROTTLE / PER_IP_THROTTLE / IP_ALLOWLIST): the global fixed-window throttle, the per-source-IP bucket table (independent budgets, wi... |
| `native_ad9238` | `PC_ENABLE_AD9238=1` | `test_ad9238` | AD9238 SPI configuration-port codec (services/peripherals/ad9238): the 16-bit instruction word (R/W + byte-count + 13-bit address) for single-byte register writes/reads, the device-update transfer tra... |
| `native_ads` | `PC_ENABLE_ADS=1` | `test_ads` | Beckhoff ADS / AMS codec (services/fieldbus/ads): the AMS/TCP + AMS-header request builders (little-endian, target-before-source addressing, cmd id + state flags + cbData + invoke id) for Read/Write/R... |
| `native_ads1115` | `PC_ENABLE_ADS1115=1` | `test_ads1115` | ADS1115 16-bit ADC codec (services/peripherals/ads1115): building the 16-bit config word for a single-shot single-ended reading (channel MUX, gain, data rate, start/mode/comparator bits, with out-of-r... |
| `native_amqp` | `PC_ENABLE_AMQP=1` | `test_amqp` | AMQP 0-9-1 frame codec (services/iot/amqp): the protocol header, the frame + method builders, the heartbeat, and the frame/method parsers (type/channel/size/payload/0xCE). |
| `native_application` | `PC_ENABLE_FILE_SERVING=1` | `test_application` | test_application against the native_stack_http stack. |
| `native_arena` | default | `test_arena` | Unified double-ended server arena (network_drivers/session/pc_arena): first-fit persistent end (bottom, individual free + coalesce + boundary shrink) + bump scratch end (top, mark/release/reset) shari... |
| `native_atc` | `PC_ENABLE_ATC=1` | `test_atc` | ATC field-I/O interop snapshot (services/machine_tool/atc): serialize this device's field-I/O map as {"inputs":[...],"outputs":[...]} JSON for an ATC engine over HTTP, plus the output setter and value... |
| `native_audit_log` | `PC_ENABLE_AUDIT_LOG=1` | `test_audit_log` | Tamper-evident hash-chained audit log (services/security/audit_log). |
| `native_auth` | `PC_ENABLE_AUTH=1` | `test_auth` | test_auth against the native_stack_http stack. |
| `native_auth_lockout` | `PC_ENABLE_AUTH=1`, `PC_ENABLE_AUTH_LOCKOUT=1` | `test_auth_lockout` | Per-IP brute-force auth lockout (services/security/auth_lockout): exponential-backoff lockout state machine. |
| `native_bacnet` | `PC_ENABLE_BACNET=1` | `test_bacnet` | BACnet/IP BVLC + NPDU codec (services/fieldbus/bacnet): the BVLC envelope (type 0x81, function, length) + the NPDU header (version + NPCI control + optional DNET/DADR + hop count) builders and parsers... |
| `native_base64` | default | `test_base64` | test_base64 against the native_stack_l46 stack. |
| `native_base64_scalar` | `PC_BASE64_SWAR=0` | `test_base64` | base64 scalar constant-time decode fallback (PC_BASE64_SWAR=0): classify one character at a time instead of the default SWAR four-per-word path. |
| `native_ble_gatt` | `PC_ENABLE_BLE_GATT=1` | `test_ble_gatt` | Bluetooth ATT codec + GATT bridge (services/radio/ble_gatt): build/parse the common ATT PDUs (read/write/notify/error, LE handles) and serialize a GATT characteristic table as JSON for the web stack. |
| `native_bus_capture` | `PC_ENABLE_BUS_CAPTURE=1` | `test_bus_capture` | CAN listen-only capture framing (server/signaling/bus_capture): can_to_socketcan() building the 16-byte Linux SocketCAN frame (big-endian can_id, EFF/RTR flags, length, data) and the DLT_CAN_SOCKETCAN... |
| `native_c37118` | `PC_ENABLE_C37118=1` | `test_c37118` | IEEE C37.118.2 synchrophasor frame codec (services/energy/c37118): CRC-CCITT, the frame builder + Command frame, and the CRC-validating parser (type / ids / timestamp / payload). |
| `native_canopen` | `PC_ENABLE_CANOPEN=1` | `test_canopen` | CANopen (CiA 301) message codec (services/fieldbus/canopen): NMT, SYNC, heartbeat, EMCY, PDO, and expedited SDO read/write/abort + the COB-ID classifier, over the shared CAN frame (shared_primitives/c... |
| `native_cbor` | `PC_ENABLE_CBOR=1` | `test_cbor` | CBOR (RFC 8949) encoder (network_drivers/presentation/codec/cbor): a pure byte-output codec, host-tested against the RFC 8949 Appendix A vectors. |
| `native_cc1101` | `PC_ENABLE_CC1101=1` | `test_cc1101` | CC1101 sub-GHz radio driver (services/radio/cc1101): the TI SPI header protocol (config registers, command strobes, status registers, TX/RX FIFO) - init/detect, variable-length send, TX-done, set-rx, ... |
| `native_cclink` | `PC_ENABLE_CCLINK=1` | `test_cclink` | CC-Link cyclic fieldbus frame codec (services/fieldbus/cclink): the frame ([station][command][bit data][word data][sum]) build + parse and the bit/word process-image accessors. |
| `native_chunked` | default | `test_chunked` | test_chunked against the native_stack_http stack. |
| `native_cia402` | `PC_ENABLE_CIA402=1`, `PC_ENABLE_CANOPEN=1` | `test_cia402` | CiA 402 / IEC 61800-7-201 drive profile (services/fieldbus/cia402): the Statusword power-state decode (mask/value table), the Controlword commands + enable sequence, Statusword flags, the CANopen SDO ... |
| `native_cip` | `PC_ENABLE_CIP=1` | `test_cip` | CIP message codec (services/fieldbus/cip): the EPATH logical-segment builder, the request builders (Get_Attribute_Single), and the response parser (service / status / data). |
| `native_client` | default | `test_client` | Outbound TCP client transport (network_drivers/transport/client.cpp), the pooled layer-4 peer of tcp.cpp used by http_client / mqtt / ws_client / relay / smtp / ssh port-forward (PC_NEED_DET_CLIENT). |
| `native_clock` | default | `test_clock` | Pluggable monotonic clock (server/clock): default millis(), custom clock divided down to the internal 1000 Hz, plus the microsecond base and latency budgeting. |
| `native_cloudevents` | `PC_ENABLE_CLOUDEVENTS=1` | `test_cloudevents` | CloudEvents v1.0 envelope (services/iot/cloudevents): the structured-JSON builder (over the JSON writer) + the binary-mode ce-* header reader. |
| `native_coap` | `PC_ENABLE_COAP=1`, `PC_ENABLE_COAP_BLOCK=1`, `PC_COAP_BLOCK_SZX_MAX=2`, `PC_COAP_BLOCK1_MAX=128` | `test_coap` | CoAP server (RFC 7252) message codec + resource dispatch. |
| `native_coap_observe` | `PC_ENABLE_COAP=1`, `PC_ENABLE_COAP_BLOCK=1`, `PC_COAP_BLOCK_SZX_MAX=2`, `PC_COAP_BLOCK1_MAX=128`, `PC_ENABLE_COAP_OBSERVE=1` | `test_coap` | CoAP with resource observation (RFC 7641) enabled. |
| `native_coaps` | `PC_ENABLE_DTLS=1`, `PC_ENABLE_COAP=1` | `test_coaps` | CoAP over DTLS (services/iot/coap/coaps, RFC 7252 sec 9): the bridge that drives a DtlsConn handshake and, once established, unwraps each epoch-3 application record, answers it with coap_server_proces... |
| `native_coaps_server` | `PC_ENABLE_DTLS=1`, `PC_ENABLE_COAP=1` | `test_coaps_server` | CoAP-over-DTLS server front-end (services/iot/coap/coaps_server): the per-peer DtlsConn pool + ingest/poll seam on top of pc_coaps_process(). |
| `native_codeql` | `PC_ENABLE_CSRF=1`, `PC_ENABLE_AUTH=1`, `PC_ENABLE_AUTH_LOCKOUT=1`, `PC_ENABLE_IP_ALLOWLIST=1`, `PC_ENABLE_WS_DEFLATE=1`, `PC_ENABLE_TIME_SOURCE=1`, `PC_ENABLE_CONFIG_STORE=1`, `PC_ENABLE_DEVICE_ID=1`, `PC_ENABLE_TELEMETRY=1`, `PC_ENABLE_DASHBOARD=1`, `PC_ENABLE_PARTITION_MONITOR=1`, `PC_ENABLE_CBOR=1`, `PC_ENABLE_MSGPACK=1`, `PC_ENABLE_GPIO_MAP=1`, `PC_ENABLE_UDP_TELEMETRY=1`, `PC_ENABLE_GUARDRAILS=1`, `PC_ENABLE_FAILSAFE=1`, `PC_ENABLE_SLEEP_SCHED=1`, `PC_ENABLE_WEARLEVEL=1`, `PC_ENABLE_NETADAPT=1`, `PC_ENABLE_DSHOT=1`, `PC_ENABLE_HART=1`, `PC_ENABLE_NTS=1`, `PC_ENABLE_DDS=1`, `PC_ENABLE_XMPP=1`, `PC_ENABLE_RAWL2=1`, `PC_ENABLE_SPA_ROUTER=1`, `PC_ENABLE_GOOSE=1`, `PC_ENABLE_MTCONNECT=1`, `PC_ENABLE_J2735=1`, `PC_ENABLE_NEMA_TS2=1`, `PC_ENABLE_SNP=1`, `PC_ENABLE_DIRECTNET=1`, `PC_ENABLE_SEP2=1`, `PC_ENABLE_PROFINET=1`, `PC_ENABLE_NTCIP=1`, `PC_ENABLE_OPENADR=1`, `PC_ENABLE_MMS=1`, `PC_ENABLE_CCLINK=1`, `PC_ENABLE_POWERLINK=1`, `PC_ENABLE_SERCOS=1`, `PC_ENABLE_PROFIBUS=1`, `PC_ENABLE_LONWORKS=1`, `PC_ENABLE_MBPLUS=1`, `PC_ENABLE_INTERBUS=1`, `PC_ENABLE_ICCP=1`, `PC_ENABLE_WAVE=1`, `PC_ENABLE_UTMC=1`, `PC_ENABLE_OCIT=1`, `PC_ENABLE_ATC=1`, `PC_ENABLE_SOUTHBOUND=1`, `PC_ENABLE_EXC_DECODER=1`, `PC_ENABLE_HTTP_DELIVERY=1`, `PC_ENABLE_HW_HEALTH=1`, `PC_ENABLE_MDNS_ADAPTIVE=1`, `PC_ENABLE_SOCKPOOL=1`, `PC_ENABLE_PSRAM_POOL=1`, `PC_ENABLE_HAPPY_EYEBALLS=1`, `PC_ENABLE_WIFI_SNIFFER=1`, `PC_ENABLE_LINK_MANAGER=1`, `PC_ENABLE_CC1101=1`, `PC_ENABLE_FDC2214=1`, `PC_ENABLE_LDC1614=1`, `PC_ENABLE_VL53L0X=1`, `PC_ENABLE_RADIO_SNIFF=1`, `PC_ENABLE_BLE_GATT=1`, `PC_ENABLE_TLS_POLICY=1`, `PC_ENABLE_WISUN=1`, `PC_ENABLE_LOGBUF=1`, `PC_ENABLE_OTA_ROLLBACK=1`, `PC_ENABLE_TOTP=1`, `PC_ENABLE_WEBHOOK=1`, `PC_ENABLE_RADIO_POWER=1`, `PC_ENABLE_AUDIT_LOG=1`, `PC_ENABLE_OIDC=1`, `PC_ENABLE_MNT=1`, `PC_ENABLE_GRAPHQL=1`, `PC_ENABLE_ESPNOW=1`, `PC_ENABLE_OAUTH2=1`, `PC_ENABLE_OPCUA=1`, `PC_ENABLE_OPCUA_CLIENT=1`, `PC_ENABLE_WEBSOCKET=1`, `PC_ENABLE_SSE=1` | `test_dispatch` | CodeQL coverage env: the full app compiled with every new feature flag ON so CodeQL traces the integration paths (CSRF / lockout / allowlist gates, permessage-deflate) AND the new service modules, whi... |
| `native_compliance` | default | `test_compliance` | RFC-compliance suite: builds with all enforcement at production defaults (PC_ENFORCE_HOST_HEADER=1) and exercises the strict behaviors. |
| `native_concurrency` | `O1`, `pthread` | `test_concurrency` | Concurrency proof for the cross-thread slot fields (pc_atomic state / rx_head / rx_tail). |
| `native_config_io` | `PC_ENABLE_CONFIG_STORE=1`, `PC_ENABLE_CONFIG_IO=1` | `test_config_io` | Schema-driven config export/restore (services/storage/config_io) over the config store; round-trip host-tested against the in-memory backend. |
| `native_config_store` | `PC_ENABLE_CONFIG_STORE=1` | `test_config_store` | Typed NVS config store (services/storage/config_store): string/u32/blob round-trips, defaults, capacity, erase/clear - run against the host in-memory backend (the ESP32 Preferences/NVS backend is comp... |
| `native_control` | `PC_ENABLE_CONTROL=1` | `test_control` | PID control law (services/system/control): the P/I/D terms, output clamping, anti-windup by conditional integration, derivative-on-measurement (no setpoint kick) + optional low-pass, feed-forward, the... |
| `native_cotp` | `PC_ENABLE_COTP=1` | `test_cotp` | TPKT (RFC 1006) + COTP X.224 class-0 frame codec (services/fieldbus/cotp): the TPKT envelope, the COTP Data TPDU + Connection Request builders, and the COTP parser. |
| `native_crypto_kat` | `PC_ENABLE_HTTP3=1` | `test_crypto_kat` | Data-driven external crypto known-answer tests: HMAC-SHA256/512, AEAD_AES_128_GCM, X25519, and Ed25519 verify from Project Wycheproof (including its adversarial edge cases), plus HKDF-SHA256 Extract (... |
| `native_csrf` | `PC_ENABLE_CSRF=1` | `test_csrf` | Stateless HMAC-signed CSRF token (services/security/csrf): issue/verify with a fixed secret unit-tests on the host (PC_ENABLE_CSRF set). |
| `native_dashboard` | `PC_ENABLE_DASHBOARD=1`, `PC_ENABLE_SSE=1` | `test_dashboard` | Dashboard widget-table JSON serializers (services/web/dashboard core). |
| `native_dbm` | `PC_ENABLE_WAL=1`, `PC_ENABLE_DBM=1` | `test_dbm` | Log-structured hash key-value store on the WAL (services/storage/dbm): put/get/delete with an in-RAM open-addressed index and value data appended to the write-ahead log, plus index rebuild by replayin... |
| `native_dds` | `PC_ENABLE_DDS=1` | `test_dds` | DDS / RTPS framing codec (services/iot/dds): the 20-octet RTPS header (magic/version/vendor/ guidPrefix) and the submessage TLV (id/flags/octetsToNextHeader, endianness flag), build + parse. |
| `native_defer` | default | `test_defer` | test_defer against the native_stack_http stack. |
| `native_deflate` | `PC_ENABLE_WS_DEFLATE=1`, `PC_ENABLE_WEBSOCKET=1` | `test_deflate` | RFC 1951 DEFLATE core (the WebSocket permessage-deflate compressor). |
| `native_device_id` | `PC_ENABLE_DEVICE_ID=1` | `test_device_id` | MAC-derived device UUID (server/signaling/device_id): RFC 4122 v5 from a MAC via SHA-1. |
| `native_devicenet` | `PC_ENABLE_DEVICENET=1` | `test_devicenet` | DeviceNet link-adaptation codec (services/fieldbus/devicenet): the 4-group 11-bit CAN id, explicit-message header octet, single-frame explicit messages, and the fragmentation reassembler (CIP over CAN... |
| `native_df1` | `PC_ENABLE_DF1=1` | `test_df1` | Allen-Bradley DF1 full-duplex frame codec (services/fieldbus/df1): BCC + CRC-16/ARC, the frame builder with DLE byte-stuffing, and the validating, un-stuffing parser. |
| `native_diag` | `PC_ENABLE_DIAG=1` | `test_diag` | Build-flag reporter (diag() / PC_ENABLE_DIAG). |
| `native_diffserv` | `PC_ENABLE_DIFFSERV=1` | `test_diffserv` | DiffServ QoS marking (PC_ENABLE_DIFFSERV): the DSCP->TOS encode (DSCP << 2, ECN 0), the server-wide and UDP DSCP defaults (set/get, 6-bit mask), the per-connection setter (pc_conn_set_dscp writes pcb-... |
| `native_digest_auth` | `PC_ENABLE_AUTH=1` | `test_digest_auth` | test_digest_auth against the native_stack_http stack. |
| `native_digest_vectors` | `PC_ENABLE_AUTH=1` | `test_digest_vectors` | test_digest_vectors against the native_stack_http stack. |
| `native_directnet` | `PC_ENABLE_DIRECTNET=1` | `test_directnet` | AutomationDirect DirectNET serial frame codec (services/fieldbus/directnet): the header (SOH + ASCII-hex slave/type/addr/blocks + ETB + LRC) and data (STX + data + ETX + LRC) frames build/parse. |
| `native_dispatch` | default | `test_dispatch` | test_dispatch against the native_stack_http stack. |
| `native_dma` | `PC_ENABLE_DMA=1`, `PC_DMA_BUF_SIZE=8`, `PC_DMA_CHANNELS=2` | `test_dma` | DMA peripheral ingest / egress simulator (mmgr/dma), v5 hardware ingest: an ingress feed surfaces as RX completion events, a full buffer ping-pongs and re-arms, egress DMA is captured, TX is one-in-fl... |
| `native_dmx` | `PC_ENABLE_DMX=1` | `test_dmx` | DMX512 + RDM lighting codec (services/peripherals/dmx): the DMX512 slot packet (build/get) and the RDM (ANSI E1.20) packet build/parse with 48-bit UIDs and the 16-bit additive checksum. |
| `native_dnc` | `PC_ENABLE_DNC=1` | `test_dnc`, `test_dnc_stream` | CNC DNC drip-feed (services/machine_tool/dnc): the EIA RS-244 <-> ISO/ASCII tape-code translation (odd-parity EIA table), ISO even parity, G-code block framing with '%' rewind-stop and leader runout, ... |
| `native_dnp3` | `PC_ENABLE_DNP3=1` | `test_dnp3` | DNP3 (IEEE 1815) data-link frame codec (services/energy/dnp3): CRC-16/DNP, the frame builder (0x0564 header + CRC'd 16-octet data blocks) and the CRC-validating, de-blocking parser. |
| `native_dns_resolver` | `PC_ENABLE_DNS_RESOLVER=1` | `test_dns_resolver` | DNS resolver answer classifier/verifier (network_drivers/network/dns_resolver): host-tested; the lwIP resolve is ESP32-only. |
| `native_dns_server` | `PC_ENABLE_DNS_SERVER=1` | `test_dns_server` | Authoritative DNS server (services/net/dns_server): the pure A-record response builder (QNAME parse, compressed A answer, NXDOMAIN, non-A query, header flags, malformed guards) and the built-in name->... |
| `native_docstore` | `PC_ENABLE_WAL=1`, `PC_ENABLE_DBM=1`, `PC_ENABLE_DOCSTORE=1` | `test_docstore` | Local JSON document store on the WAL (services/storage/docstore): JSON documents addressed by id, stored via dbm on the write-ahead log, plus top-level field queries (find documents whose JSON field e... |
| `native_dshot` | `PC_ENABLE_DSHOT=1` | `test_dshot` | DShot ESC throttle codec (services/peripherals/dshot): the 16-bit frame (11-bit value + telemetry + 4-bit nibble-xor CRC), the bidirectional inverted-CRC variant, decode/validate, and per-rate bit tim... |
| `native_dtls` | `PC_ENABLE_DTLS=1` | `test_dtls_record` | DTLS 1.3 record layer (network_drivers/presentation/security/dtls/dtls_record, RFC 9147 sec 4): DTLSPlaintext + DTLSCiphertext protect/unprotect, the unified header, sequence-number encryption (sec 4.... |
| `native_dtls_conn` | `PC_ENABLE_DTLS=1`, `PC_ENABLE_TLS_RPK=1` | `test_dtls_conn` | DTLS 1.3 server handshake state machine (network_drivers/presentation/security/dtls/dtls_conn, RFC 9147 sec 5-6): the one-round-trip full handshake (TLS_AES_128_GCM_SHA256 / X25519 / Ed25519) over the... |
| `native_dtls_hs` | `PC_ENABLE_DTLS=1` | `test_dtls_handshake` | DTLS 1.3 handshake framing + reliability (network_drivers/presentation/security/dtls/dtls_handshake, RFC 9147 sec 5 + 7): the 12-byte DTLS handshake header, overlap-tolerant message reassembly, the AC... |
| `native_dtls_tls13` | `PC_ENABLE_DTLS=1`, `PC_ENABLE_TLS_RPK=1` | `test_dtls_tls13` | TLS 1.3 messages the DTLS 1.3 handshake adds to tls13_msg (RFC 8446 sec 4.1.4 / 4.4.1), compiled for the DTLS path (PC_ENABLE_DTLS, not HTTP/3): the HelloRetryRequest builder, the cookie extension par... |
| `native_edge_cache` | `PC_ENABLE_HTTP_CACHE=1`, `PC_ENABLE_HTTP_CLIENT=1`, `PC_ENABLE_EDGE_CACHE=1`, `PC_ENABLE_RANGE=1` | `test_edge_cache`, `test_edge_fetch` | CDN edge-cache engine (services/web/edge_cache): the pure freshness/validator core (response header-field access, HTTP-date parsing over IMF-fixdate / RFC 850 / asctime, RFC 9111 lifetime + Expires-Da... |
| `native_edge_cache_sd` | `PC_ENABLE_WAL=1`, `PC_ENABLE_DBM=1`, `PC_DBM_VAL_MAX=1024`, `PC_ENABLE_HTTP_CACHE=1`, `PC_ENABLE_HTTP_CLIENT=1`, `PC_ENABLE_EDGE_CACHE=1` | `test_edge_cache_sd` | CDN edge-cache L2 SD-persistence tier (services/web/edge_cache/edge_cache_sd): the entry <-> dbm-value serialization roundtrip (all response metadata, Vary variants, binary and max-size bodies), the s... |
| `native_edge_mesh` | `PC_ENABLE_HTTP_CACHE=1`, `PC_ENABLE_HTTP_CLIENT=1`, `PC_ENABLE_EDGE_CACHE=1`, `PC_ENABLE_EDGE_MESH=1` | `test_edge_mesh` | CDN edge-cache mesh sibling-cache codec (services/web/edge_cache/edge_mesh): the request/response wire frames (build + tri-state accumulating parse over partial reads, magic/version/opcode validation)... |
| `native_enip` | `PC_ENABLE_ENIP=1` | `test_enip` | EtherNet/IP encapsulation codec (services/fieldbus/enip): the 24-octet header, RegisterSession + SendRRData builders (Common Packet Format), and the SendRRData reply extractor. |
| `native_enocean` | `PC_ENABLE_ENOCEAN=1`, `PC_ENOCEAN_MAX_DATA=16` | `test_enocean` | EnOcean ESP3 serial codec (services/radio/enocean), v5 radio plugin: the CRC-8 (poly 0x07) against known answers, a build -> parse round trip, malformed framing (bad sync / header CRC / data CRC), inc... |
| `native_esp` | `PC_ENABLE_IKEV2=1` | `test_esp` | ESP (RFC 4303) packet transform with AES-256-GCM (RFC 4106) - the IPsec datapath crypto core (services/system/esp): encapsulate a payload into SPI\|Seq\|IV\|AES-GCM(payload\|pad\|padlen\|nexthdr)\|ICV... |
| `native_espnow` | `PC_ENABLE_ESPNOW=1` | `test_espnow` | ESP-NOW peer messaging (services/radio/espnow) - the envelope codec + peer registry are host-tested here; the esp_now radio binding is ESP32-only. |
| `native_euromap77` | `PC_ENABLE_OPCUA=1`, `PC_ENABLE_EUROMAP77=1` | `test_euromap77` | EUROMAP 77 (OPC 40077) IMM_MES_Interface model (services/machine_tool/euromap77) - OPC UA for injection molding machines. |
| `native_exc_decoder` | `PC_ENABLE_EXC_DECODER=1` | `test_exc_decoder` | ESP32 panic / exception decoder (server/exc_decoder): parse a captured Guru Meditation dump (cause, register PC + EXCVADDR, backtrace PC:SP frames) into a structured ExcInfo and serialize it as JSON f... |
| `native_failsafe` | `PC_ENABLE_FAILSAFE=1` | `test_failsafe` | Software watchdog / deadlock detection + safe-state (server/failsafe): the wrap-safe overdue predicate, the lifeline registry, fire-once-per-episode breach callback, and JSON. |
| `native_fanuc_j519` | `PC_ENABLE_FANUC_J519=1` | `test_fanuc_j519` | FANUC Stream Motion / option J519 UDP codec (services/machine_tool/fanuc_j519): the robot counterpart to FOCAS. |
| `native_fdc2214` | `PC_ENABLE_FDC2214=1` | `test_fdc2214` | FDC2114/2214 capacitance-to-digital field sensor (services/peripherals/fdc2214): the 28-bit data combine + error flags, the frequency scale (data/2^28 * fref), and the single-channel config-sequence b... |
| `native_file_serving` | `PC_ENABLE_FILE_SERVING=1` | `test_file_serving` | test_file_serving against the native_stack_http stack. |
| `native_fins` | `PC_ENABLE_FINS=1` | `test_fins` | Omron FINS frame codec (services/fieldbus/fins): the command builder + Memory Area Read convenience + the command / response parsers (10-octet header, MRC/SRC, MRES/SRES end code). |
| `native_flow_export` | `PC_ENABLE_FLOW_EXPORT=1` | `test_flow_export` | Flow-record export codec (services/net/flow_export): NetFlow v5 fixed header/record builders + the NetFlow v9 / IPFIX template-then-data cursor (length/count patching, v9 4-octet padding). |
| `native_focas` | `PC_ENABLE_FOCAS=1` | `test_focas` | FANUC FOCAS Ethernet codec (services/machine_tool/focas): the big-endian frame envelope (magic/version/type/length) + open/close handshake, the generic command request (6-octet function selector + fiv... |
| `native_form_params` | default | `test_form_params` | test_form_params against the native_stack_http stack. |
| `native_forward` | `PC_ENABLE_FORWARD=1`, `PC_FWD_MAX_IFACES=4`, `PC_FWD_MAX_RULES=4`, `PC_FWD_MAX_ACL=4`, `PC_FWD_MAX_ROUTES=4`, `PC_FWD_INSPECT=1` | `test_forward` | Interface forwarding plane (services/net/forward), v5 bridge / router: default-deny, an ALLOW rule forwards, a DENY wins, multi-destination fan-out, no reflection to the source, the per-rule rate cap ... |
| `native_forwarded_trust` | `PC_ENABLE_AUTH=1`, `PC_ENABLE_AUTH_LOCKOUT=1`, `PC_ENABLE_FORWARDED_TRUST=1` | `test_forwarded_trust` | Trusted-reverse-proxy forwarded-client resolver (services/security/forwarded_trust): a Forwarded / X-Forwarded-For client address is honored only when the real TCP peer is a configured trusted-upstrea... |
| `native_frame` | default | `test_frame` | The declarative frame builder (shared_primitives/frame.h + frame.cpp): the single engine that turns a static pc_field spec into wire bytes, so the ~160 formatting sites in this library carry a table r... |
| `native_ftp` | `PC_ENABLE_FTP=1` | `test_ftp` | FTP client wire codec (services/file_transfer/ftp, RFC 959 + RFC 2428): the control-command builders (generic verb + PORT + EPRT), the single/multi-line 3-digit reply parser, and the PASV / EPSV data-... |
| `native_gateway` | `PC_ENABLE_GATEWAY=1`, `PC_GW_MAX_PORTS=4` | `test_gateway` | Radio / wireless gateway bridge (services/net/gateway), v5 southbound-to-northbound: an uplink envelopes a received frame (src address / port / rssi / seq) and publishes it, fail-closed on no sink / u... |
| `native_gnss_survey` | `PC_ENABLE_NTRIP_CASTER=1`, `PC_ENABLE_NMEA0183=1`, `UNITY_INCLUDE_DOUBLE` | `test_gnss_survey` | GNSS survey-in core (services/timing_position/gnss/gnss_survey): the exact WGS84 geodetic<->ECEF transform (matched against pyproj EPSG:4979->EPSG:4978), the shifted-origin position averager with a 3-... |
| `native_goose` | `PC_ENABLE_GOOSE=1` | `test_goose` | IEC 61850 GOOSE publisher codec (services/energy/goose): the BER IECGoosePdu (gocbRef..allData, minimal-length INTEGERs with the positive leading-zero rule) + the GOOSE header + Ethernet frame (ethert... |
| `native_gpib` | `PC_ENABLE_GPIB=1` | `test_gpib` | GPIB-over-LAN (Prologix-style) controller command codec (services/instrumentation/gpib): the ++ command builders (addr / mode / read / eoi / eos / spoll / clr / trg / ver), the data-line escaping (lea... |
| `native_gpio_map` | `PC_ENABLE_GPIO_MAP=1` | `test_gpio_map` | GPIO pin-mapper / browser diag core (server/signaling/gpio_map): direction names, JSON serializer, control-POST parser, output guard - all pure and host-tested. |
| `native_graphql` | `PC_ENABLE_GRAPHQL=1` | `test_graphql` | GraphQL query subset (services/iot/graphql) - pure parser + executor, host-tested with a demo resolver. |
| `native_grpcweb` | `PC_ENABLE_GRPC_WEB=1` | `test_grpcweb` | gRPC-Web message framing codec (services/iot/grpcweb): the 5-octet length-prefixed message frame builder + the 0x80 trailers frame (grpc-status / grpc-message) + the frame parser. |
| `native_guardrails` | `PC_ENABLE_GUARDRAILS=1` | `test_guardrails` | Heap/stack guardrails (services/security/guardrails): threshold evaluator + JSON, host-tested. |
| `native_h2conn` | `PC_ENABLE_HTTP2=1` | `test_h2_conn` | HTTP/2 connection engine (network_drivers/presentation/http/http2/h2_conn, RFC 9113): initial SETTINGS on init, preface + client SETTINGS -> SETTINGS ACK, decoding a real HPACK-encoded request into th... |
| `native_h2frame` | `PC_ENABLE_HTTP2=1` | `test_h2_frame` | HTTP/2 binary framing (network_drivers/presentation/http/http2/h2_frame, RFC 9113): the 9-byte frame header parse/write (24-bit length, reserved-bit masking), SETTINGS build + parse with validation, a... |
| `native_h3_conn` | `PC_ENABLE_HTTP3=1` | `test_h3_conn` | HTTP/3 application engine (network_drivers/presentation/http/http3/h3_conn, RFC 9114): drives h3_conn through the quic_conn callback seam - a QPACK-encoded request on a request stream dispatches the r... |
| `native_h3_e2e` | `PC_ENABLE_HTTP3=1` | `test_h3_e2e` | End-to-end HTTP/3 capstone (network_drivers/presentation/http/http3): a QUIC client in the test completes the TLS 1.3 handshake against a quic_conn + h3_conn server, sends a real HTTP/3 GET (QPACK HEA... |
| `native_h3_server` | `PC_ENABLE_HTTP3=1` | `test_h3_server` | HTTP/3 dispatch bridge end-to-end through PC (the full Layer-7 app built with PC_ENABLE_HTTP3=1): a QUIC client completes the handshake and sends an HTTP/3 GET, quic_server routes it to the reserved d... |
| `native_h3frame` | `PC_ENABLE_HTTP3=1` | `test_h3_frame` | HTTP/3 framing (network_drivers/presentation/http/http3/h3_frame, RFC 9114 sec 7): the type+length varint header parse/write (incl. |
| `native_haas_mdc` | `PC_ENABLE_HAAS_MDC=1` | `test_haas_mdc` | Haas Machine Data Collection (MDC) Q-command codec (services/machine_tool/haas_mdc): the ?Q query builders (Q100 serial, Q101 software version, Q102 model, Q104 mode, Q300 power-on time, Q500 program ... |
| `native_happy_eyeballs` | `PC_ENABLE_HAPPY_EYEBALLS=1` | `test_happy_eyeballs` | Dual-stack Happy Eyeballs selection (services/net/happy_eyeballs): RFC 6724 destination preference scoring, the candidate-list sort + RFC 8305 address-family interleave, and the Connection Attempt Del... |
| `native_hart` | `PC_ENABLE_HART=1` | `test_hart` | HART / HART-IP codec (services/fieldbus/hart): the HART command frame (longitudinal XOR checksum, short + long addressing) build/parse and the 8-octet HART-IP message header. |
| `native_hislip` | `PC_ENABLE_HISLIP=1` | `test_hislip` | HiSLIP (High-Speed LAN Instrument Protocol, IVI-6.1) message codec (services/instrumentation/hislip): the fixed 16-byte header build/parse (HS prologue + type + control + 32-bit param + 64-bit payload... |
| `native_hmmd` | `PC_ENABLE_HMMD=1` | `test_hmmd` | Waveshare HMMD 24GHz mmWave micro-motion radar codec (services/peripherals/hmmd): the LD2410-family little-endian framing, the report parse (detection flag, distance, all 16 gate energies), rejecting ... |
| `native_hostlink` | `PC_ENABLE_HOSTLINK=1` | `test_hostlink` | Omron Host Link (C-mode) frame codec (services/fieldbus/hostlink): the FCS (XOR), the ASCII command builder (@UU + header + text + FCS + *CR), and the FCS-validating parser + end-code reader. |
| `native_hotswap` | `PC_ENABLE_HOTSWAP=1` | `test_hotswap` | Removable-storage hot-swap safeties (services/storage/hotswap): the ABSENT/READY/FAULTED state machine - a run of consecutive I/O errors faults a volume while a single one does not, any success resets... |
| `native_hpack` | `PC_ENABLE_HTTP2=1` | `test_hpack` | HPACK header compression for HTTP/2 (RFC 7541): prefix-integer coding (App C.1), the Huffman string code (App B / C.4.1), the first-request decode with dynamic-table insertion (C.3.1), dynamic-table i... |
| `native_http_client` | `PC_ENABLE_HTTP_CLIENT=1` | `test_http_client` | Outbound HTTP client: URL parser + request builder + response parser. |
| `native_http_delivery` | `PC_ENABLE_HTTP_DELIVERY=1` | `test_http_delivery` | HTTP delivery optimizations (services/file_transfer/http_delivery): the RFC 5861 stale-while-revalidate freshness decision + its Cache-Control builder, and the versioned service-worker precache manife... |
| `native_http_parser` | default | `test_http_parser` | test_http_parser against the native_stack_l46 stack. |
| `native_httpcache` | `PC_ENABLE_HTTP_CACHE=1` | `test_httpcache` | HTTP Cache-Control helpers (services/web/httpcache, RFC 9111 + 8246 + 5861): the structured directive builder + first-class origin presets (immutable asset / shared / no-store / revalidatable), the to... |
| `native_hw_health` | `PC_ENABLE_HW_HEALTH=1` | `test_hw_health` | Hardware-health diagnostics (server/signaling/hw_health): power-rail voltage-drop logger (worst droop + sag/brownout counts), SPI-bus CRC audit with hysteretic clock backoff, GPIO short-circuit test (... |
| `native_iccp` | `PC_ENABLE_ICCP=1` | `test_iccp` | ICCP / TASE.2 (IEC 60870-6) Data_Value codec (services/energy/iccp): the StateQ (state + quality) and RealQ (scaled INTEGER + quality) indication-point BER structures with optional timestamp. |
| `native_iec60870` | `PC_ENABLE_IEC60870=1` | `test_iec60870` | IEC 60870-5-101/-104 codec (services/energy/iec60870): the -104 APCI (I/S/U), the ASDU header + 3-octet IOA, and the -101 FT1.2 fixed/variable link frames (sum checksum). |
| `native_iface` | default | `test_iface` | test_iface against the native_stack_http stack. |
| `native_iface_bridge` | `PC_ENABLE_IFACE_BRIDGE=1` | `test_iface_bridge` | Interface bridge pure core (services/net/iface_bridge): the user-defined address:port -> bus rule table (register / find / dedup / capacity, keyed by port+proto with the full pc_ip bind address preser... |
| `native_ikev2` | `PC_ENABLE_IKEV2=1` | `test_ikev2`, `test_ikev2_natt` | IKEv2 (RFC 7296) message + payload codec (services/security/ikev2): the 28-octet IKE header, the generic payload chain walker, the SA -> proposal -> transform tree (incl. |
| `native_ina219` | `PC_ENABLE_INA219=1` | `test_ina219` | INA219 current/power codec (services/peripherals/ina219): decoding the bus-voltage register (bits [15:3], LSB 4 mV, status bits ignored) and the shunt-voltage register (signed, LSB 10 uV), computing t... |
| `native_inflate` | `PC_ENABLE_WS_DEFLATE=1`, `PC_ENABLE_WEBSOCKET=1` | `test_inflate` | RFC 1951 INFLATE core (the WebSocket permessage-deflate decompressor). |
| `native_interbus` | `PC_ENABLE_INTERBUS=1` | `test_interbus` | INTERBUS summation-frame codec (services/fieldbus/interbus): the summation frame (loopback + per-device 16-bit slices + CRC-16/CCITT FCS) assemble + disassemble. |
| `native_iolink` | `PC_ENABLE_IOLINK=1` | `test_iolink` | IO-Link (SDCI) data-link message codec (services/fieldbus/iolink): the MC / CKT / CKS control octets and the SDCI checksum (seed 0x52 + the 8->6 compression of IO-Link spec A.1.6), with a hand-compute... |
| `native_ip` | default | `test_ip` | IP address core (network_drivers/network/pc_ip): RFC 4291 IPv4/IPv6 text parsing, RFC 5952 canonical formatting (:: zero-compression, v4-mapped), and scope classification (loopback / link-local / priv... |
| `native_ipsec_db` | `PC_ENABLE_IKEV2=1` | `test_ipsec_db` | IPsec Security Policy Database + Security Association Database (RFC 4301, services/system/esp/ipsec_db): ordered first-match-wins SPD policy lookup over source/destination/protocol/port selector range... |
| `native_j1939` | `PC_ENABLE_J1939=1` | `test_j1939` | SAE J1939 codec (services/fieldbus/j1939): 29-bit id encode/decode (PDU1 + PDU2), single-frame messages, Request PGN, Address Claimed + NAME, and the Transport Protocol (BAM + TP.DT) reassembler, over... |
| `native_j2735` | `PC_ENABLE_J2735=1` | `test_j2735` | SAE J2735 V2X codec (services/transportation/j2735): the ASN.1 UPER bit primitive layer (constrained INTEGER / BOOLEAN / bit fields) and the BSMcore block encode/decode. |
| `native_json` | default | `test_json` | test_json against the native_stack_http stack. |
| `native_jwt` | `PC_ENABLE_JWT=1` | `test_jwt` | JWT (HS256) bearer-auth verification. |
| `native_keepalive` | `PC_ENFORCE_HOST_HEADER=0`, `PC_ENABLE_KEEPALIVE=1`, `PC_KEEPALIVE_MAX_REQUESTS=3` | `test_keepalive` | HTTP/1.1 keep-alive (persistent connections): full server built with PC_ENABLE_KEEPALIVE=1; a small per-connection request cap makes the fairness-bound test fast. |
| `native_ld2410` | `PC_ENABLE_LD2410=1` | `test_ld2410` | LD2410 mmWave radar codec (services/peripherals/ld2410): decoding a basic and an engineering-mode report frame, rejecting malformed frames, the byte-by-byte stream reassembler (resync past noise, spli... |
| `native_ldc1614` | `PC_ENABLE_LDC1614=1` | `test_ldc1614` | LDC1614 inductance-to-digital field sensor (services/peripherals/ldc1614): the 28-bit data combine + error flags, the frequency scale (data/2^28 * fref), and the single-channel config-sequence builder. |
| `native_lfs_mock` | `PC_ENABLE_MNT=1` | `test_lfs_mock` | The littlefs-backed pc_mnt_backend used by the host tests that need a real tree (test/mocks/lfs_mock.h): round-trip, seek, directory listing, stat, rename/remove, append, and a full volume refusing ra... |
| `native_link_manager` | `PC_ENABLE_LINK_MANAGER=1` | `test_link_manager` | Multi-interface egress selection (server/signaling/link_manager): a table of interfaces (kind + priority + up/down) with deterministic best-link-up selection, graceful escalation to a higher-priority ... |
| `native_log` | `PC_ENABLE_LOGBUF=1`, `PC_LOG_LEVEL=PC_LOG_LEVEL_INFO` | `test_log` | Abstract logging macros (shared_primitives/log.h) whose disabled levels are discarded by the preprocessor: built at PC_LOG_LEVEL_INFO so DEBUG is below the floor. |
| `native_logbuf` | `PC_ENABLE_LOGBUF=1` | `test_logbuf` | Rotating log ring + severity trap (server/logbuf): pure, fully host-tested. |
| `native_lonworks` | `PC_ENABLE_LONWORKS=1` | `test_lonworks` | LonWorks / LON-IP network-variable codec (services/fieldbus/lonworks): the LonTalk NV PDU ([msg-code][14-bit selector][value]) build + parse and the SNVT_temp / SNVT_switch scalar encodings. |
| `native_lora` | `PC_ENABLE_LORA=1` | `test_lora` | LoRa codec + SX127x driver (services/radio/lora), v5 radio plugin: the RadioHead 4-byte header parse/build, and the SX127x register protocol (init / send / tx-done / set-rx / recv) exercised against a... |
| `native_lsv2` | `PC_ENABLE_LSV2=1` | `test_lsv2` | Heidenhain LSV/2 telegram codec (services/machine_tool/lsv2): the framer (4-byte big-endian payload-length prefix + 4-char mnemonic + payload), the typed request builders (login A_LG / logout A_LO, nu... |
| `native_lwm2m_tlv` | `PC_ENABLE_LWM2M=1` | `test_lwm2m_tlv` | OMA LwM2M TLV codec (services/iot/lwm2m): the writer (raw + int / bool / string / float value helpers, 8-/16-bit ids, inline / 8-/16-/24-bit lengths) + the cursor reader + integer value decoding. |
| `native_mbplus` | `PC_ENABLE_MBPLUS=1` | `test_mbplus` | Modbus Plus HDLC token-bus codec (services/fieldbus/mbplus): the HDLC frame (7E addr ctrl payload CRC-16/X-25 7E) build + validate and the token-rotation ring helper. |
| `native_mbus` | `PC_ENABLE_MBUS=1` | `test_mbus` | Wired M-Bus codec (services/fieldbus/mbus): the ACK / short / long frame builders + parser (start/stop, doubled length, 8-bit sum checksum) and the EN 13757-3 variable-data record walker (DIF/VIF, DIF... |
| `native_mdns_adaptive` | `PC_ENABLE_MDNS_ADAPTIVE=1` | `test_mdns_adaptive` | Adaptive mDNS beacon scheduling (network_drivers/application/mdns_adaptive): RF-contention backoff/recovery of the announce interval, the TTL/2 continuous-refresher cadence, the announce-due check, an... |
| `native_melsec` | `PC_ENABLE_MELSEC=1` | `test_melsec` | Mitsubishi MELSEC MC binary 3E codec (services/fieldbus/melsec): the batch-read request builder (little-endian, subheader 0x5000, command 0x0401, device code + 24-bit head device) + the 0xD000 respons... |
| `native_middleware` | default | `test_middleware` | test_middleware against the native_stack_http stack. |
| `native_mms` | `PC_ENABLE_MMS=1` | `test_mms` | IEC 61850 MMS PDU codec (services/energy/mms): the BER confirmed-request/response Read PDUs (invokeID + read service + named ObjectName), build + parse. |
| `native_mnt` | `PC_ENABLE_MNT=1` | `test_mnt` | Mounted storage (server/filesystem/mnt) - the backend vtable and its built-in RAM backend, host-tested through that backend (the Arduino FS backend is board-layer and HW-verified). |
| `native_modbus` | `PC_ENABLE_MODBUS=1`, `PC_ENABLE_MODBUS_RTU=1` | `test_modbus` | Modbus TCP slave core + RTU framing (Modbus Application Protocol): the data model + MBAP/PDU codec + the RTU ADU codec (CRC16 + [addr][PDU][CRC]). |
| `native_modbus_master` | `PC_ENABLE_MODBUS=1`, `PC_ENABLE_MODBUS_MASTER=1` | `test_modbus_master` | Modbus master codec + scanner (services/fieldbus/modbus/modbus_master): build read requests, parse responses; host-tested as a round-trip against the slave codec. |
| `native_mpr121` | `PC_ENABLE_MPR121=1` | `test_mpr121` | MPR121 capacitive-touch codec (services/peripherals/mpr121): decoding the touch-status word into an electrode bitmask (masking proximity / over-current), the per-electrode touched test, the proximity ... |
| `native_mqtt` | `PC_ENABLE_MQTT=1` | `test_mqtt` |  |
| `native_mqtt_sn` | `PC_ENABLE_MQTT_SN=1` | `test_mqtt_sn` | MQTT-SN v1.2 wire codec (services/iot/mqtt/mqtt_sn): the zero-heap message builders (CONNECT/REGISTER/PUBLISH/SUBSCRIBE/PINGREQ/DISCONNECT/SEARCHGW) + the Length+MsgType header parser (1- and 3-octet ... |
| `native_msgpack` | `PC_ENABLE_MSGPACK=1` | `test_msgpack` | MessagePack encoder (network_drivers/presentation/codec/msgpack): a pure byte-output codec, host-tested against the spec encodings. |
| `native_mtconnect` | `PC_ENABLE_MTCONNECT=1` | `test_mtconnect` | MTConnect agent response codec (services/machine_tool/mtconnect, ANSI/MTC1.4): the incremental MTConnectStreams builder (header + Samples/Events/Condition), the MTConnectDevices probe (device model), ... |
| `native_multipart` | default | `test_multipart` | test_multipart against the native_stack_http stack. |
| `native_nats` | `PC_ENABLE_NATS=1` | `test_nats` | NATS client protocol codec (services/iot/nats): the CONNECT / PUB / SUB / UNSUB / PING / PONG builders + the inbound MSG / INFO / PING / +OK / -ERR parser (subject/sid/reply/payload). |
| `native_nema_ts2` | `PC_ENABLE_NEMA_TS2=1` | `test_nema_ts2` | NEMA TS 2 SDLC frame codec (services/transportation/nema_ts2): the traffic-cabinet bus frame ([address][control][frame-type][data][CRC-16/X-25]) build + validate. |
| `native_net_egress` | default | `test_net_egress` | Egress-interface reporting (network_drivers/physical). |
| `native_netadapt` | `PC_ENABLE_NETADAPT=1` | `test_netadapt` | Network adaptation decisions (services/net/netadapt): TCP receive-window sizing from the free heap (reserve + quarter-of-spare, clamped) and the DHCP->static-IP fallback trigger. |
| `native_nmea0183` | `PC_ENABLE_NMEA0183=1` | `test_nmea0183` | NMEA 0183 sentence codec (services/timing_position/nmea0183): the XOR checksum, sentence build, parse (field splitting, talker/type, checksum validation) against the canonical GGA vector, and the fiel... |
| `native_nmea2000` | `PC_ENABLE_NMEA2000=1` | `test_nmea2000` | NMEA 2000 codec (services/timing_position/nmea2000): single-frame messages plus the Fast Packet transport (frame count, build, reassembly), built on the J1939 id codec (implied). |
| `native_nrf24` | `PC_ENABLE_NRF24=1`, `PC_NRF24_PAYLOAD=8` | `test_nrf24` | nRF24L01+ driver (services/radio/nrf24), v5 radio plugin: the Nordic SPI command protocol (STATUS shifted out first, W/R_REGISTER, W_TX/R_RX_PAYLOAD, write-1-to-clear) exercised against a mock chip - ... |
| `native_ntcip` | `PC_ENABLE_NTCIP=1` | `test_ntcip` | NTCIP transportation object OIDs (services/transportation/ntcip): the NTCIP 1202 signal-controller + 1203 DMS object roots under 1.3.6.1.4.1.1206.4.2 and the OID builder (root + instance index), for t... |
| `native_ntp_server` | `PC_ENABLE_NTP_SERVER=1` | `test_ntp_server` | NTP/SNTP server (RFC 5905 server mode) response codec (ntp_server_build_response): version echo, mode/LI/stratum, origin-timestamp copy, reference/receive/transmit stamps, big-endian encoding, and the... |
| `native_ntrip_caster` | `PC_ENABLE_NTRIP_CASTER=1` | `test_ntrip_caster` | NTRIP caster protocol codec (services/timing_position/gnss/ntrip_caster): rover request parsing (mountpoint, NTRIP 1.0/2.0 version, HTTP Basic auth), the stream-accept / error responses, and the RTCM ... |
| `native_nts` | `PC_ENABLE_NTS=1` | `test_nts` | Network Time Security codec (network_drivers/application/nts, RFC 8915): the NTS-KE TLV records (build the standard request, parse a response) and the NTS NTP extension-field framing (unique id / cook... |
| `native_oauth2` | `PC_ENABLE_OAUTH2=1` | `test_oauth2` | OAuth2 token-endpoint client (services/security/oauth2) - the form-body builder + JSON token-response parser are host-tested (the parser reuses the JSON reader); the HTTP exchange is ESP32-only. |
| `native_observability` | `PC_ENABLE_OBSERVABILITY=1` | `test_observability` | Transport observability (PC_ENABLE_OBSERVABILITY): the pc_conn_on_event hook, by-reason counters, the live CONN_CLOSING gauge, and that the real lwIP callbacks (recv FIN / error / timeout / local clos... |
| `native_ocit` | `PC_ENABLE_OCIT=1` | `test_ocit` | OCIT-Outstations message codec (services/transportation/ocit): the object message ([msg-type][object-type][instance][data-type][value]) build + parse and the typed-value accessors. |
| `native_oidc` | `PC_ENABLE_OIDC=1` | `test_oidc` | OIDC RS256 ID-token verifier (services/security/oidc). |
| `native_opcua` | `PC_ENABLE_OPCUA=1` | `test_opcua` | OPC UA Binary increment 1 (services/fieldbus/opcua) - the type codec, UACP framing, and Hello/Acknowledge handshake are host-tested here; the TCP server (opcua_rx) is ESP32-only. |
| `native_opcua_client` | `PC_ENABLE_OPCUA=1`, `PC_ENABLE_OPCUA_CLIENT=1` | `test_opcua_client` |  |
| `native_openadr` | `PC_ENABLE_OPENADR=1` | `test_openadr` | OpenADR 3.0 JSON codec (services/energy/openadr): the event (programID + eventName + interval payloads) and report (VEN reading) JSON documents build, with escaping + a no-stdlib 3-decimal formatter. |
| `native_ota` | `PC_ENFORCE_HOST_HEADER=0`, `PC_ENABLE_OTA=1` | `test_http_ota` | Parser streaming-body hook (OTA) - exercises http_parser with PC_ENABLE_OTA=1 using a mock sink (no ESP32 Update dependency). |
| `native_ota_rollback` | `PC_ENABLE_OTA_ROLLBACK=1` | `test_ota_rollback` | OTA rollback decision (server/update/ota_rollback): pure decision matrix host-tested; the esp_ota commit/rollback are ESP32-only. |
| `native_packml` | `PC_ENABLE_PACKML=1` | `test_packml` | PackML / OMAC packaging-machine state model (services/machine_tool/packml), ISA-TR88.00.02: the pure 17-state transition engine (command / state-complete / execute-complete + command validity) and the... |
| `native_partition` | `PC_ENABLE_PARTITION_MONITOR=1` | `test_partition_monitor` | Flash partition-map monitor (services/storage/partition_monitor core): the kind classifier + JSON serializer host-test here; the esp_partition walk is ESP32-only. |
| `native_path_params` | default | `test_path_params` | test_path_params against the native_stack_http stack. |
| `native_pca9685` | `PC_ENABLE_PCA9685=1` | `test_pca9685` | PCA9685 PWM/servo codec (services/peripherals/pca9685): the PRESCALE computation from a PWM frequency (with clamping), the per-channel register address, the servo pulse-width -> 12-bit count conversio... |
| `native_pentest` | `PC_ENABLE_MODBUS=1`, `PC_ENABLE_MODBUS_MASTER=1`, `PC_ENABLE_TOTP=1`, `PC_ENABLE_MULTIPART=1`, `PC_ENABLE_CBOR=1`, `PC_ENABLE_MSGPACK=1`, `PC_ENABLE_COAP=1`, `PC_ENABLE_COAP_BLOCK=1`, `PC_COAP_BLOCK_SZX_MAX=2`, `PC_COAP_BLOCK1_MAX=128`, `PC_ENABLE_SNMP=1`, `PC_ENABLE_SQLITE=1`, `PC_ENABLE_REDIS=1`, `PC_ENABLE_OPCUA=1`, `PC_ENABLE_GRAPHQL=1`, `PC_ENABLE_DNS_SERVER=1`, `PC_ENABLE_DNP3=1`, `PC_ENABLE_STOMP=1`, `PC_ENABLE_SMB=1`, `PC_ENABLE_DNC=1`, `PC_ENABLE_FTP=1`, `PC_ENABLE_FINS=1`, `PC_ENABLE_MELSEC=1`, `PC_ENABLE_CIP=1`, `PC_ENABLE_ENIP=1`, `PC_ENABLE_DF1=1`, `PC_ENABLE_BACNET=1`, `PC_ENABLE_COTP=1`, `PC_ENABLE_C37118=1`, `PC_ENABLE_JWT=1`, `PC_ENABLE_DIRECTNET=1`, `PC_ENABLE_CCLINK=1`, `PC_ENABLE_AMQP=1`, `PC_ENABLE_MMS=1`, `PC_ENABLE_DDS=1`, `PC_ENABLE_WEBDAV=1`, `PC_ENABLE_HTTP2=1`, `PC_ENABLE_HTTP3=1`, `PC_ENABLE_FILE_SERVING=1` | `test_pentest` | Adversarial / pentest harness - run SEPARATELY (`pio test -e native_pentest`), NOT part of run_tests.sh. |
| `native_plaintext` | default | `test_plaintext` | The plaintext pool accessor (server/mmgr/plaintext): bump-allocate + reset semantics, alignment, and fail-closed exhaustion. |
| `native_pn532` | `PC_ENABLE_PN532=1`, `PC_PN532_MAX_DATA=8` | `test_pn532` | PN532 NFC frame codec (services/peripherals/pn532), v5 radio plugin: the normal-information-frame build/parse against the documented GetFirmwareVersion command + response frames (LEN/LCS + DCS checksu... |
| `native_pool_workers` | `PC_WORKER_COUNT=2` | `test_plaintext`, `test_secure_pool` | Both pool accessors at PC_WORKER_COUNT=2. |
| `native_power_mgmt` | `PC_ENABLE_POWER_MGMT=1` | `test_power_mgmt` | SoC power governor (server/power_mgmt): the pure clock decision from load, die temperature and reset reason - load-based scaling, the thermal hysteresis that stops a part parked at the limit from osci... |
| `native_powerlink` | `PC_ENABLE_POWERLINK=1` | `test_powerlink` | Ethernet POWERLINK basic frame codec (services/fieldbus/powerlink): the EPL cyclic frames ([messageType][dest][source][payload]) - SoC/PReq/PRes/SoA - build + parse, over raw L2 (0x88AB). |
| `native_pqc` | `PC_ENABLE_PQC_KEX=1` | `test_pqc_sha3`, `test_pqc_mlkem`, `test_pqc_sntrup761` | Post-quantum hybrid KEX primitives (network_drivers/presentation/pqc): the Keccak/SHA-3/SHAKE sponge (FIPS 202) and ML-KEM-768 Encaps (FIPS 203) - the responder half of the mlkem768x25519-sha256 (SSH)... |
| `native_preempt_queue` | `PC_ENABLE_PREEMPT_QUEUE=1`, `PC_PQ_DEPTH=4`, `PC_PQ_ITEM_SIZE=4` | `test_preempt_queue` | Preempting work queue (services/system/preempt_queue), v5 real-time ingest: the host fixed-ring core - FIFO order, urgent-to-front, fail-closed when full, high-water, and drain/handler dispatch. |
| `native_presentation` | default | `test_presentation` | test_presentation against the native_stack_l46 stack. |
| `native_primitives` | default | `test_primitives`, `test_crc` | Shared no-stdlib primitives (shared_primitives): the base-10 pc_strtol/strtoul/strtof number parsers (numparse.h), the strict RFC 3629 UTF-8 validator (utf8.h), and the parameterized Rocksoft/Williams... |
| `native_profibus` | `PC_ENABLE_PROFIBUS=1` | `test_profibus` | PROFIBUS-DP FDL telegram codec (services/fieldbus/profibus): the SD1 (no-data) + SD2 (variable data, LE/LEr + arithmetic-sum FCS) telegrams build + validate. |
| `native_profinet` | `PC_ENABLE_PROFINET=1` | `test_profinet` | PROFINET DCP frame codec (services/fieldbus/profinet): the 10-octet DCP header + option/suboption blocks (even-padding) build + parse/walk, for Identify/Set over raw L2 (ethertype 0x8892). |
| `native_promisc` | `PC_ENABLE_PROMISC=1` | `test_promisc` | Wi-Fi promiscuous capture helpers (services/radio/promisc): the pure 802.11 MAC header parser (to/from-DS src/dst/bssid resolution, QoS, WDS 4-address, control frames, malformed rejection) and libpcap... |
| `native_protobuf` | `PC_ENABLE_PROTOBUF=1` | `test_protobuf` | Protocol Buffers wire codec (services/iot/protobuf): the zero-heap streaming writer (varint / ZigZag / fixed32 / fixed64 / length-delimited) + the cursor reader, host-tested against the spec vectors. |
| `native_prov` | default | `test_provisioning` | Provisioning form-field parser - the only host-testable part of the captive portal (softAP / lwIP UDP / NVS are ESP32-only and compiled out here). |
| `native_proxy_protocol` | `PC_ENABLE_PROXY_PROTOCOL=1` | `test_proxy_protocol` | HAProxy PROXY protocol codec (services/net/proxy_protocol): the v1 (text) + v2 (binary) TCP/IPv4 header builders and the unified parser (recover the real client IP behind a load balancer). |
| `native_psram_pool` | `PC_ENABLE_PSRAM_POOL=1` | `test_psram_pool` | Buffer placement policy + DMA ping-pong (services/storage/psram_pool): pc_psram_place picks DRAM vs PSRAM by size / DMA requirement / free-heap headroom (large-cold to PSRAM, small-hot + DMA to DRAM, ... |
| `native_ptp` | `PC_ENABLE_PTP=1` | `test_ptp` | PTP / IEEE 1588-2008 (PTPv2) message codec + slave clock math (network_drivers/application/ptp): the 34-octet common header, 10-octet timestamp, Sync/Delay_Req/Follow_Up/Delay_Resp/Announce build+pars... |
| `native_qpack` | `PC_ENABLE_HTTP3=1` | `test_qpack` | QPACK field-section compression for HTTP/3 (network_drivers/presentation/http/http3/qpack, RFC 9204): the Appendix B.1 worked example (literal field line with a static name reference), the encoder's e... |
| `native_quic_conn` | `PC_ENABLE_HTTP3=1` | `test_quic_conn` | QUIC v1 server connection engine (network_drivers/presentation/http/http3/quic_conn, RFC 9000 / RFC 9001): the test acts as a QUIC client - builds real Initial / Handshake / 1-RTT packets and drives a... |
| `native_quic_crypto` | `PC_ENABLE_HTTP3=1` | `test_quic_crypto` | QUIC Initial packet crypto (crypto/hkdf + quic_aead + quic_crypto, RFC 9001): HKDF-Expand-Label key derivation, AEAD_AES_128_GCM (software AES-128 + GHASH) and header protection. |
| `native_quic_frame` | `PC_ENABLE_HTTP3=1` | `test_quic_frame` | QUIC frame codec (network_drivers/presentation/http/http3/quic_frame, RFC 9000 sec 19): builder/parser round-trips for PADDING/PING/HANDSHAKE_DONE, ACK (single-range + a hand-built multi-range-with-EC... |
| `native_quic_packet` | `PC_ENABLE_HTTP3=1` | `test_quic_packet` | QUIC packet header + packet-number codec (network_drivers/presentation/http/http3/quic_packet, RFC 9000 sec 17): the long-header build/parse round-trip, a Version Negotiation packet (Version 0 + suppo... |
| `native_quic_server` | `PC_ENABLE_HTTP3=1` | `test_quic_server` | HTTP/3 server glue (network_drivers/presentation/http/http3/quic_server): the UDP-facing pool that routes datagrams by Destination Connection ID to a pool of QuicConn + H3Conn engines. |
| `native_quic_tls` | `PC_ENABLE_HTTP3=1` | `test_quic_tls` | TLS 1.3 server handshake state machine for QUIC (network_drivers/presentation/http/http3/ quic_tls, RFC 9001 / RFC 8446): a full interop round-trip - drive the server with a hand-built ClientHello, ru... |
| `native_quic_tls_pqc` | `PC_ENABLE_HTTP3=1`, `PC_ENABLE_PQC_KEX=1`, `PC_WORKER_TASK_STACK=16384` | `test_quic_tls` | TLS 1.3 QUIC handshake with the X25519MLKEM768 post-quantum hybrid group (IANA 0x11ec, PC_ENABLE_PQC_KEX=1): drives the server with a hybrid ClientHello, then verifies it as a conforming client - ML-K... |
| `native_quic_tp` | `PC_ENABLE_HTTP3=1` | `test_quic_tp` | QUIC transport parameters codec (network_drivers/presentation/http/http3/quic_tp, RFC 9000 sec 18): the sec 18.2 defaults, an encode/parse round-trip over the connection IDs + every varint parameter +... |
| `native_quic_varint` | `PC_ENABLE_HTTP3=1` | `test_quic_varint` | QUIC variable-length integer codec (network_drivers/presentation/http/http3/quic_varint, RFC 9000 sec 16) - the foundational HTTP/3 primitive: the Appendix A.1 worked examples (1/2/4/8 byte encodings)... |
| `native_radio_power` | `PC_ENABLE_RADIO_POWER=1` | `test_radio_power` | WiFi radio power controls (network_drivers/physical/radio_power): modem-sleep mode names host-tested; the apply/readback are ESP32-only (esp_wifi). |
| `native_radio_sniff` | `PC_ENABLE_RADIO_SNIFF=1` | `test_radio_sniff` | Receive-only radio channel sniffer -> pcap (services/radio/radio_sniff): the int->float32 RSSI encode, the pcap global header (DLT 802.15.4 TAP), and the per-frame TAP record (RSSI + channel TLVs + MA... |
| `native_range` | `PC_ENFORCE_HOST_HEADER=0`, `PC_ENABLE_RANGE=1`, `PC_ENABLE_FILE_SERVING=1`, `PC_ENABLE_KEEPALIVE=1` | `test_range` | HTTP Range requests / 206 Partial Content (RFC 7233): full server built with PC_ENABLE_RANGE=1, serving a real littlefs volume through the mount seam and reading the responses back off the tcp_write c... |
| `native_rawl2` | `PC_ENABLE_RAWL2=1` | `test_rawl2` | Raw L2 Ethernet frame codec (services/fieldbus/rawl2): Ethernet II + 802.1Q VLAN build/parse and the 802.3 FCS (CRC-32). |
| `native_rcwl0516` | `PC_ENABLE_RCWL0516=1` | `test_rcwl0516` | RCWL-0516 Doppler presence sensor + the shared one-GPIO presence facade (services/peripherals/rcwl0516): the debounce that swallows comparator chatter, the hold that bridges the module's ~2s retrigger... |
| `native_redis` | `PC_ENABLE_REDIS=1` | `test_redis_resp` | Redis RESP2/RESP3 codec (services/iot/redis_resp): the zero-heap command encoder + the cursor reply parser (RESP2 simple/error/integer/bulk/array/nil plus RESP3 null/boolean/double/big number/bulk err... |
| `native_regex` | default | `test_regex` | test_regex against the native_stack_http stack. |
| `native_relay` | `PC_ENABLE_RELAY=1` | `test_relay` | TCP relay / DNAT byte pump (services/net/relay): the bidirectional relay engine that publishes an internal host:port through the server. |
| `native_response_headers` | default | `test_response_headers` | test_response_headers against the native_stack_http stack. |
| `native_roaming` | `PC_ENABLE_ROAMING=1` | `test_roaming` | Wi-Fi roaming decision layer (network_drivers/network/roaming): the pure policy that fuses the current RSSI, a candidate neighbour list, and an optional 802.11v BTM hint into a roam/stay decision (tar... |
| `native_robotics` | `PC_ENABLE_OPCUA=1`, `PC_ENABLE_ROBOTICS=1` | `test_robotics` | OPC UA for Robotics (OPC 40010-1) MotionDeviceSystem model (services/machine_tool/robotics) - the Browse hierarchy + the Read resolver over a bound RoboticsMotionDeviceSystem, including the parametric... |
| `native_rtc` | `PC_ENABLE_RTC=1` | `test_rtc` | DS1307/DS3231 RTC conversions (services/peripherals/rtc): BCD time registers <-> Unix epoch in 24- and 12-hour encodings, leap years, clock-halt/century bit masks, range validation, and a round-trip o... |
| `native_rtcm3` | `PC_ENABLE_NTRIP_CASTER=1` | `test_rtcm3` | RTCM 3.x framing + station-reference codec (services/timing_position/gnss/rtcm3), the pure core of the GNSS RTK base / NTRIP caster: the transport frame (0xD3 preamble, 10-bit length, CRC-24Q), MSB-fi... |
| `native_s7comm` | `PC_ENABLE_S7COMM=1` | `test_s7comm` | Siemens S7comm PDU codec (services/fieldbus/s7comm): the Setup Communication + Read Var request builders, the header parser, and the response data-item reader (length-in-bits + even padding). |
| `native_safety_scl` | `PC_ENABLE_SAFETY_SCL=1` | `test_safety_scl` | IEC 61784-3 black-channel Safety Communication Layer primitives (services/machine_tool/safety_scl): the monitoring-counter state machine, the receive watchdog, and the fail-safe latch the four safety ... |
| `native_sb_modbus` | `PC_ENABLE_SOUTHBOUND=1`, `PC_ENABLE_MODBUS=1`, `PC_ENABLE_MODBUS_MASTER=1` | `test_sb_modbus` | Modbus-master southbound driver adapter (services/net/southbound/sb_modbus): binds the transport-agnostic Modbus TCP master codec into the southbound driver framework, so an app reads/writes register ... |
| `native_scp` | `PC_ENABLE_SSH=1`, `PC_ENABLE_SSH_SCP=1`, `PC_ENABLE_MNT=1` | `test_scp` | SCP (RCP) protocol wire codec (network_drivers/application/scp): parse an `scp -t/-f <path>` exec command into its sink/source role + target, parse + build the `C<mode> <size> <name>` control line (oc... |
| `native_scpi` | `PC_ENABLE_SCPI=1`, `UNITY_INCLUDE_DOUBLE` | `test_scpi` | SCPI / IEEE 488.2 instrument-control codec (services/instrumentation/scpi): the command builder (:-hierarchy header + params + terminator), the response parsers (numeric NR1/NR2/NR3, boolean, quoted s... |
| `native_sdi12` | `PC_ENABLE_SDI12=1` | `test_sdi12` | SDI-12 sensor-bus codec (services/peripherals/sdi12): the command builders, the measurement response parser (atttn), the data-value splitter, and the SDI-12 CRC (compute/encode/verify). |
| `native_secure_pool` | default | `test_secure_pool` | The secure pool accessor (server/mmgr/secure): the SAME pool mechanism as the plaintext side (server/mmgr/arena) instantiated a second time at a disjoint address, so only what differs is covered here ... |
| `native_sen0192` | `PC_ENABLE_SEN0192=1` | `test_sen0192` | SEN0192 microwave motion sensor presence state machine (services/peripherals/sen0192): presence asserts on an active sample and holds for the configured window after the last active sample, clears aft... |
| `native_senml` | `PC_ENABLE_SENML=1` | `test_senml` | SenML (RFC 8428) pack builder (services/iot/senml): the SenML-JSON encoder (over the JSON writer) + the SenML-CBOR encoder (over the CBOR writer, integer labels), integral numbers emitted as integers. |
| `native_sep2` | `PC_ENABLE_SEP2=1` | `test_sep2` | IEEE 2030.5 (SEP 2.0) resource codec (services/energy/sep2): the DeviceCapability, EndDevice, and DERControl XML documents (urn:ieee:std:2030.5:ns), XML-escaped. |
| `native_sercos` | `PC_ENABLE_SERCOS=1` | `test_sercos` | SERCOS III motion-bus codec (services/fieldbus/sercos): the MDT/AT telegram (type + phase + cycle + data) build + parse and the 16-bit IDN encode/decode (S/P + set + block). |
| `native_session` | default | `test_session` | test_session against the native_stack_l46 stack. |
| `native_sht3x` | `PC_ENABLE_SHT3X=1` | `test_sht3x` | Sensirion SHT3x temperature/humidity codec (services/peripherals/sht3x): the CRC-8 against the datasheet check value (0xBEEF -> 0x92), the raw-tick -> milli-unit temperature/humidity conversions at th... |
| `native_sigfox` | `PC_ENABLE_SIGFOX=1` | `test_sigfox` | Sigfox modem AT-command codec (services/radio/sigfox), v5 radio plugin: the AT$SF uplink command (uppercase hex encoding of the payload), its bounds (12-byte cap, output cap), and the OK / ERROR / PEN... |
| `native_signaling` | default | `test_signaling` | Application-layer signaling (server/signaling): the state bucket. |
| `native_simatic` | `PC_ENABLE_SIMATIC=1` | `test_simatic` | Siemens SIMATIC serial (services/fieldbus/simatic): 3964R block framing (DLE-double + XOR BCC) + the 3964R link state machine (STX/DLE handshake, NAK/QVZ retry, ZVZ timeout, priority arbitration) + RK... |
| `native_sleep_sched` | `PC_ENABLE_SLEEP_SCHED=1` | `test_sleep_sched` | Dynamic sleep-cycle scheduler (server/sleep_sched): the wrap-safe idle->sleep-window decision core with a doubling ramp clamped to a ceiling. |
| `native_smb` | `PC_ENABLE_SMB=1` | `test_smb2`, `test_smb_crypto`, `test_ntlm`, `test_ntlmssp`, `test_spnego`, `test_smb_client` | SMB2 client (network_drivers/application/smb, MS-SMB2 / MS-NLMP): the SMB2 wire codec (transport frame, sync header, NEGOTIATE, SESSION_SETUP, TREE_CONNECT/CREATE/CLOSE/READ/WRITE); the NTLM digests M... |
| `native_smtp` | `PC_ENABLE_SMTP=1` | `test_smtp` | SMTP client (RFC 5321) dialogue engine (services/net/smtp/smtp_run): greeting/EHLO/AUTH LOGIN/MAIL/RCPT/DATA over a send/recv seam, with dot-stuffing + multi-line reply parsing. |
| `native_snmp` | `PC_ENABLE_SNMP=1` | `test_snmp_ber`, `test_snmp_agent` | SNMP ASN.1 BER codec (the version-agnostic base for the SNMP agent). |
| `native_snmp_trap` | `PC_ENABLE_SNMP=1`, `PC_ENABLE_SNMP_TRAP=1` | `test_snmp_trap` |  |
| `native_snmp_v3` | `PC_ENABLE_SNMP=1`, `PC_ENABLE_SNMP_V3=1`, `PC_ENABLE_SNMP_TRAP=1` | `test_snmp_v3` | SNMPv3 USM layer: auth (HMAC-SHA-256), privacy (AES-128-CFB), engine discovery, timeliness. |
| `native_snp` | `PC_ENABLE_SNP=1` | `test_snp` | GE Fanuc SNP serial frame codec (services/fieldbus/snp): the Series Ninety Protocol frame ([control][length][data][arithmetic-sum BCC]) build + validate. |
| `native_sockpool` | `PC_ENABLE_SOCKPOOL=1` | `test_sockpool` | Dynamic socket recycling (services/net/sockpool): a fixed LRU connection-slot pool - acquire (free slot, else recycle the least-recently-used and report the evicted id), touch, release, find, and in-u... |
| `native_southbound` | `PC_ENABLE_SOUTHBOUND=1` | `test_southbound` | Southbound protocol-driver framework (services/net/southbound): the bounded driver registry (register / find / clear / count) and the name-dispatched read/write/read_block/write_block facade, includin... |
| `native_spa_router` | `PC_ENABLE_SPA_ROUTER=1` | `test_spa_router` | Single-page-app micro-routing (services/web/spa_router): the serve-file / serve-shell / passthrough decision from a request path (extension test + API prefix). |
| `native_span` | default | `test_span` | The bounded byte region (server/mmgr/span.h): a pointer and the capacity that belongs to it, bound together. |
| `native_sparkplug` | `PC_ENABLE_SPARKPLUG=1` | `test_sparkplug` | Sparkplug B codec (services/iot/sparkplug): the topic builder + the Metric / Payload protobuf serializers (over the protobuf codec). |
| `native_sqlite` | `PC_ENABLE_SQLITE=1` | `test_sqlite` | SQLite3 on-disk file-format reader (services/storage/sqlite): the 100-byte database header, the b-tree page header, the record varint, and record serial types, parsed by hand. |
| `native_sse` | `PC_ENABLE_SSE=1` | `test_sse` | test_sse against the native_stack_l46 stack. |
| `native_ssh` | `PC_SSH_MAX_CHANNELS=3`, `PC_ENABLE_SSH=1`, `PC_ENABLE_MNT=1`, `PC_ENABLE_SSH_SFTP=1`, `PC_ENABLE_SSH_SCP=1` | `test_ssh_crypto`, `test_ssh_transport`, `test_ssh_auth`, `test_ssh_channel`, `test_ssh_server` | SSH crypto layer (native software paths only, no mbedtls dependency); channels multiplexed (PC_SSH_MAX_CHANNELS=3) to exercise routing; SFTP/SCP subsystem routing on (MNT satisfies the guard - the fil... |
| `native_ssh_aesgcm` | default | `test_ssh_aesgcm` | AES-256-GCM AEAD for aes256-gcm@openssh.com (RFC 5647) host-tested here: seal/open vs the NIST/McGrew AES-256-GCM Test Case 16 vector, tamper rejection, and the invocation-counter advance. |
| `native_ssh_chachapoly` | default | `test_ssh_chachapoly` | chacha20-poly1305@openssh.com AEAD (network_drivers/presentation/ssh): ChaCha20 vs RFC 8439 sec 2.3.2 block vector, Poly1305 vs RFC 8439 sec 2.5.2, and the OpenSSH construction (length decode, encrypt... |
| `native_ssh_comp` | `PC_ENABLE_SSH=1`, `PC_ENABLE_SSH_ZLIB=1`, `PC_ENABLE_WS_DEFLATE=1`, `PC_ENABLE_WEBSOCKET=1` | `test_ssh_comp` | SSH s2c compression WIRING with the full SSH stack built with PC_ENABLE_SSH_ZLIB=1: the compression owner (ssh_comp) + its NEWKEYS / USERAUTH_SUCCESS activation + the packet-layer compress path in ssh... |
| `native_ssh_conn` | `PC_ENABLE_SSH=1` | `test_ssh_conn` | SSH wired through the real transport/session layers (PROTO_SSH byte-pump) |
| `native_ssh_ecdsa` | default | `test_ssh_ecdsa` | ECDSA P-256 for ecdsa-sha2-nistp256 (RFC 5656) host-tested on the software path: pubkey, deterministic sign, and verify pinned byte-exact to the RFC 6979 A.2.5 (P-256/SHA-256) vectors, plus tamper rej... |
| `native_ssh_ed25519` | default | `test_ssh_ed25519` | Modern SSH crypto KATs (curve25519-sha256 KEX + ssh-ed25519 host key / client auth): SHA-512 (FIPS 180-4), X25519 (RFC 7748), Ed25519 (RFC 8032). |
| `native_ssh_hardened` | `PC_SSH_ALLOW_PASSWORD=0` | `test_ssh_hardening` | SSH built with password auth disabled (publickey-only hardening) |
| `native_ssh_inflate` | `PC_ENABLE_SSH=1`, `PC_ENABLE_SSH_ZLIB=1` | `test_ssh_inflate` | SSH client-to-server resumable INFLATE (ssh_inflate): decompresses OpenSSH's per-packet Z_PARTIAL_FLUSH zlib stream across packets with a 32 KB context-takeover window. |
| `native_ssh_kbdint` | `PC_ENABLE_SSH=1`, `PC_ENABLE_SSH_KEYBOARD_INTERACTIVE=1` | `test_ssh_kbdint`, `test_ssh_auth` | SSH keyboard-interactive auth (RFC 4256) built with PC_ENABLE_SSH_KEYBOARD_INTERACTIVE=1: the server sends one non-echoed Password prompt (INFO_REQUEST) and verifies the INFO_RESPONSE via the password... |
| `native_ssh_pqc` | `PC_SSH_MAX_CHANNELS=3`, `PC_ENABLE_PQC_KEX=1` | `test_ssh_pqc` | mlkem768x25519-sha256 SSH hybrid KEX (draft-ietf-sshm-mlkem-hybrid-kex) end to end: the full SSH transport built with PC_ENABLE_PQC_KEX=1 plus the ML-KEM-768 / SHA-3 core. |
| `native_ssh_sftp` | `PC_ENABLE_SSH=1`, `PC_ENABLE_SSH_SFTP=1`, `PC_ENABLE_MNT=1` | `test_ssh_sftp` | SFTP protocol v3 wire codec (network_drivers/application/sftp): the SSH_FXP_* request reader + response builders (VERSION / STATUS / HANDLE / DATA / ATTRS / NAME), the ATTRS blob encode/decode round-t... |
| `native_ssh_zlib` | `PC_ENABLE_SSH=1`, `PC_ENABLE_SSH_ZLIB=1`, `PC_ENABLE_WS_DEFLATE=1`, `PC_ENABLE_WEBSOCKET=1` | `test_ssh_zlib` | SSH server-to-client streaming compressor (zlib@openssh.com / zlib): a context-takeover DEFLATE stream (persistent sliding window across packets, sync-flush per packet, zlib wrapper). |
| `native_stack_http` | `BODY_BUF_SIZE=512`, `PC_ENFORCE_HOST_HEADER=0`, `PC_ENABLE_STATS=1`, `PC_ENABLE_METRICS=1`, `PC_ENABLE_ETAG=1`, `PC_ENABLE_WEB_TERMINAL=1`, `PC_HTTP_EMIT_DATE=1`, `PC_ENABLE_WEBSOCKET=1` | - | Full HTTP/1.1 server stack through Layer 7. |
| `native_stack_l46` | `PC_ENFORCE_HOST_HEADER=0` | - | Layers 4-6 stack: transport + session + presentation + the standalone parser, no app layer. |
| `native_statsd` | `PC_ENABLE_STATSD=1` | `test_statsd` | StatsD metrics client (services/iot/statsd): the pure line formatter (name:value\|type, sample rate, DogStatsD tags) plus the count/gauge/timing/set emit helpers, whose sent bytes are captured through... |
| `native_stomp` | `PC_ENABLE_STOMP=1` | `test_stomp` | STOMP 1.2 frame codec (services/iot/stomp): the zero-heap frame builder (command + escaped headers + NUL body) + the non-mutating parser (command/header slices/body, honoring content-length) + escape/... |
| `native_sunspec` | `PC_ENABLE_SUNSPEC=1` | `test_sunspec` | SunSpec Modbus model codec (services/energy/sunspec): the map writer (marker / model headers / points / end model) + the model-chain walker + typed point readers (u16 / i16 / u32 / i32 / string). |
| `native_swar` | default | `test_swar` | Lane math (shared_primitives/swar.h): one 32-bit word as four byte lanes. |
| `native_syslog` | `PC_ENABLE_SYSLOG=1` | `test_syslog` | Syslog client (RFC 5424) line formatter. |
| `native_telemetry` | `PC_ENABLE_TELEMETRY=1` | `test_telemetry` | Telemetry math (services/iot/telemetry): moving-window stats, rate-of-change, and totalizer. |
| `native_telnet` | `PC_ENABLE_TELNET=1` | `test_telnet` | Telnet server (RFC 854 IAC negotiation + line editing) wired through the real transport ring buffer; output checked via the tcp_write capture mock. |
| `native_template` | default | `test_template` | test_template against the native_stack_http stack. |
| `native_thread` | `PC_ENABLE_THREAD=1`, `PC_THREAD_MAX_DATA=64` | `test_thread` | Thread spinel / HDLC-lite codec (services/radio/thread), v5 radio plugin: the FCS (CRC-16/X-25) against its catalog check value (0x906E), an encode -> decode round trip, the byte-stuffing of reserved ... |
| `native_time_source` | `PC_ENABLE_TIME_SOURCE=1` | `test_time_source` | Multi-source time fallback matrix (services/timing_position/time_source): priority-ordered query of user time sources with first-valid-wins fallback. |
| `native_tls13_kdf` | `PC_ENABLE_HTTP3=1` | `test_tls13_kdf` | TLS 1.3 key schedule for the QUIC handshake (network_drivers/tls/tls13_kdf, RFC 8446 sec 7.1 / 4.4.4): Early/Handshake/Master secret Extract chain, client/server handshake + application traffic secret... |
| `native_tls13_msg` | `PC_ENABLE_HTTP3=1` | `test_tls13_msg` | TLS 1.3 handshake messages for the QUIC handshake (network_drivers/presentation/http/http3/ tls13_msg, RFC 8446 sec 4): ClientHello parse (X25519 key_share + capability flags), and the server flight. |
| `native_tls_policy` | `PC_ENABLE_TLS_POLICY=1` | `test_tls_policy` | TLS version negotiation + pinned cipher policy (services/security/tls_policy): the server-style version pick (highest supported not above the client's), the version name, cipher selection by server pr... |
| `native_totp` | `PC_ENABLE_TOTP=1` | `test_totp` | TOTP two-factor (services/security/totp): HMAC-SHA1 HOTP/TOTP + base32, host-tested against the RFC 6238 vectors (builds on the software SHA-1). |
| `native_trace_capture` | `PC_ENABLE_TRACE_CAPTURE=1`, `PC_TC_MAX_WINDOW_SAMPLES=32` | `test_trace_capture` | Pre/post-trigger sample-window assembler (server/signaling/trace_capture), v5 high-rate acquisition: a continuously-running pre-trigger ring, trigger() freezing it as the window's pre-trigger half, fe... |
| `native_transport` | default | `test_transport` | test_transport against the native_stack_l46 stack. |
| `native_tsan` | `g`, `O1`, `fsanitize=thread`, `pthread` | `test_concurrency` | Same harness under ThreadSanitizer: proves ZERO data races on the slot fields (the pc_atomic acquire/release happens-before lets the plain rx_buffer[] writes be read on the other core safely). |
| `native_ubx` | `PC_ENABLE_UBX=1` | `test_ubx` | UBX (u-blox binary GNSS protocol) codec (services/timing_position/ubx): B5 62 framing, 8-bit Fletcher checksum, build/poll/parse, and the streaming NMEA+UBX demultiplexer. |
| `native_udp_telemetry` | `PC_ENABLE_UDP_TELEMETRY=1` | `test_udp_telemetry` | UDP telemetry line builder (services/iot/udp_telemetry): InfluxDB line-protocol formatting, host-tested. |
| `native_udp_transport` | default | `test_udp_transport` | UDP transport multicast receive (network_drivers/transport/udp.cpp): joining an IPv4 multicast group by dotted-quad, rejecting a non-multicast or malformed group, delivering a group datagram to the re... |
| `native_umati` | `PC_ENABLE_OPCUA=1`, `PC_ENABLE_UMATI=1` | `test_umati` | umati / OPC UA for Machine Tools (OPC 40501-1) MachineTool model (services/machine_tool/umati) - the Browse hierarchy + the Read resolver over a bound UmatiMachineTool are host-tested here. |
| `native_upload` | `PC_ENFORCE_HOST_HEADER=0`, `PC_ENABLE_UPLOAD=1`, `BODY_BUF_SIZE=64` | `test_upload` | Streaming file upload: POST body -> FS file via the parser streaming hook. |
| `native_utmc` | `PC_ENABLE_UTMC=1` | `test_utmc` | UTMC common-database codec (services/transportation/utmc): the UTMCRequest (object id) and UTMCResponse (value + quality + timestamp) HTTP/XML documents build + the request-id parse, escaped. |
| `native_vl53l0x` | `PC_ENABLE_VL53L0X=1` | `test_vl53l0x` | VL53L0X time-of-flight ranging codec (services/peripherals/vl53l0x): the range byte-pair combine to millimeters, the interrupt-status data-ready decode, and the device range-status validity check. |
| `native_vxi11` | `PC_ENABLE_VXI11=1` | `test_vxi11` | VXI-11 (TCP/IP Instrument Protocol) codec over ONC RPC / XDR (services/instrumentation/vxi11): the XDR write/read helpers (4-byte-aligned, big-endian, length-prefixed opaque/string), the ONC-RPC recor... |
| `native_wal` | `PC_ENABLE_WAL=1` | `test_wal`, `test_wal_store` | Write-ahead store for atomic buffer-to-flash storage (services/storage/wal): CRC32 record framing + crash-recovery replay (the atomicity core), plus the A/B superblock + checkpoint + mount layer over ... |
| `native_wamp` | `PC_ENABLE_WAMP=1` | `test_wamp` | WAMP messaging codec (services/iot/wamp): the JSON-array message builders (HELLO / SUBSCRIBE / PUBLISH / CALL / REGISTER / YIELD / GOODBYE over JsonWriter) + the positional array parser (type / ids / ... |
| `native_wave` | `PC_ENABLE_WAVE=1` | `test_wave` | IEEE 1609 WAVE codec (services/transportation/wave): the 1609.3 WSMP header (version + P-encoded PSID + length) build + parse, the PSID p-encoding, and the 1609.2 secured-message envelope header. |
| `native_wearlevel` | `PC_ENABLE_WEARLEVEL=1` | `test_wearlevel` | Flash wear-leveling slot selector (server/filesystem/wearlevel): least-worn pick (ties -> lowest index), saturating mark, and the wear-imbalance spread metric. |
| `native_web_terminal` | default | `test_web_terminal` | test_web_terminal against the native_stack_http stack. |
| `native_webdav` | `PC_ENABLE_WEBDAV=1`, `PC_ENABLE_FILE_SERVING=1` | `test_webdav` | WebDAV server core (RFC 4918): method classification, header parsing, XML escaping, and the 207 Multi-Status builder. |
| `native_webdav_handler` | `BODY_BUF_SIZE=512`, `PC_ENFORCE_HOST_HEADER=0`, `PC_ENABLE_WEBDAV=1`, `PC_ENABLE_FILE_SERVING=1`, `PC_ENABLE_WEB_TERMINAL=1`, `PC_ENABLE_WEBSOCKET=1` | `test_webdav_handler` | WebDAV request handler over a directory-capable FS mock (recursive COPY/MOVE/DELETE) |
| `native_webhook` | `PC_ENABLE_WEBHOOK=1` | `test_webhook` | Webhook / IFTTT builders (services/net/webhook): IFTTT URL + value1/2/3 JSON payload, host-tested. |
| `native_websocket` | default | `test_websocket` | test_websocket against the native_stack_l46 stack. |
| `native_wifi_sniffer` | `PC_ENABLE_WIFI_SNIFFER=1` | `test_wifi_sniffer` | 802.11 sniffer / traffic analyzer (services/radio/wifi_sniffer): decode an 802.11 MAC header (frame-control type/subtype + flags, ToDS/FromDS-dependent addresses), tally frames by type, the RSSI-hyste... |
| `native_wisun` | `PC_ENABLE_WISUN=1` | `test_wisun` | Wi-SUN FAN border-router connector (services/radio/wisun): the CoAP client request builder (RFC 7252 header + Uri-Path options with extended-length + payload) and the FAN node registry (register / fin... |
| `native_workers` | `PC_WORKER_COUNT=2` | `test_workers` | Core-partitioning invariant at N=2 (PC_WORKER_COUNT=2): each worker reaps only its owned slots (check_timeouts ownership). |
| `native_ws_client` | `PC_ENABLE_WS_CLIENT=1` | `test_ws_client` |  |
| `native_ws_deflate` | `PC_ENFORCE_HOST_HEADER=0`, `PC_ENABLE_WS_DEFLATE=1`, `PC_ENABLE_WEBSOCKET=1` | `test_websocket` | WebSocket permessage-deflate (RFC 7692) inbound path wired through the real WS stack: handshake negotiation, the RSV1 frame path, and INFLATE delivery (with the table scratch borrowed from the shared ... |
| `native_xmpp` | `PC_ENABLE_XMPP=1` | `test_xmpp` | XMPP stanza codec (services/iot/xmpp, RFC 6120): XML-escaped stream/message/presence/iq builders and the stanza-name + attribute readers. |
| `native_zigbee` | `PC_ENABLE_ZIGBEE=1`, `PC_ZIGBEE_MAX_DATA=32` | `test_zigbee` | Zigbee EZSP / ASH framing codec (services/radio/zigbee), v5 radio plugin: the CRC-16/CCITT and the encoded RST frame against their documented values (C0 38 BC 7E), an encode -> decode round trip, the ... |
| `native_zwave` | `PC_ENABLE_ZWAVE=1`, `PC_ZWAVE_MAX_DATA=16` | `test_zwave` | Z-Wave Serial API frame codec (services/radio/zwave), v5 radio plugin: the data-frame build/parse against the documented GetVersion request (01 03 00 15 E9), the XOR checksum, a round trip, malformed ... |

<!-- END GENERATED test-environments -->

> [!NOTE]
> The `native_stack_l46` and `native_stack_http` environments build with `PC_ENFORCE_HOST_HEADER=0` because their legacy test suites focus strictly on lower-level parser mechanics. The stricter RFC 7230 §5.4 host header validation is tested independently in `native_compliance`.

> [!IMPORTANT]
> **Compilation Isolation & Feature Flags**:
> Under PlatformIO (and standard Arduino/C++ build systems), library source files (in `src/`) are compiled independently of the main application (the sketch's `.ino` file) as separate translation units.
>
> Consequently, `#define` macros specified inside `.ino` sketch files (e.g., `#define PC_ENABLE_PROVISIONING 1`) **do not propagate** to the library's compiled source code. If you define a configuration macro or feature flag in your sketch rather than in the build configuration, the library's `.cpp` files will compile with their default configuration, resulting in linker errors (e.g., undefined symbols) or severe runtime/memory layout mismatches.
>
> To configure the library correctly, all override configuration constants and feature flags (such as [`PC_ENABLE_PROVISIONING`](@ref PC_ENABLE_PROVISIONING), [`PC_ENABLE_SSH`](@ref PC_ENABLE_SSH), [`MAX_CONNS`](@ref MAX_CONNS), etc.) **must** be set as compiler build flags in your environment (e.g., `build_flags = -DPC_ENABLE_PROVISIONING=1` in `platformio.ini`).

---

## 4. Deep Dive: Key Concepts Tested

### 1. HTTP/1.1 Parser & RFC Compliance

HTTP parsing is notoriously difficult to write safely. A single parsing slip can lead to security vulnerabilities like **HTTP Request Smuggling**. Our parser is tested against:

- **RFC 7230 & 7231**: Ensuring correct interpretation of URI paths, query parameters, header keys, and body limits.
- **Buffer Overflows (413 & 414)**: We verify that when client requests send URIs larger than `URI_BUF_SIZE` (414 URI Too Long) or bodies exceeding [`BODY_BUF_SIZE`](@ref BODY_BUF_SIZE) (413 Payload Too Large), the server safely terminates the connection without corrupting memory.
- **Host Header Enforcement**: In compliance builds, the server rejects any HTTP/1.1 request lacking a `Host` header, or containing duplicate `Host` headers.

### 2. WebSocket Protocols

WebSocket communication begins as an HTTP request and upgrades to a binary frame protocol. The suites test:

- **Sec-WebSocket-Accept**: Verifying the server takes the client's key, appends the RFC 6455 GUID (`258EAFA5-E914-47DA-95CA-C5AB0DC85B11`), hashes it using SHA-1, and Base64-encodes it to complete the handshake.
- **Masking Key Validation**: The protocol requires all client-to-server frames to be masked (XOR-encrypted). The tests send both masked and unmasked frames to ensure the server decodes them properly and rejects illegal unmasked frames.
- **Fragmentation**: Large payloads can be split across multiple frames. We simulate fragmented packets to ensure the server correctly buffers and reconstructs them.

### 3. Cryptography & Known-Answer Tests (KAT)

The native SSH server implementation includes an entire cryptography stack. Cryptography code should never be verified with random data. We use **Known-Answer Test Vectors** directly from NIST and RFC specifications:

- **SHA-256 / HMAC-SHA2-256**: Tested against NIST FIPS 180-4 vectors to guarantee message authentication code integrity.
- **AES-256-CTR**: Block cipher decryption/encryption verified against NIST SP 800-38A standard streams.
- **RSA Signature Verification**: Verified using real-world public-private key signatures generated via external `openssl` binaries.

---

## 5. How to Write and Run Tests

All tests are written using the **Unity** testing framework.

### Running Tests Locally

To run all test suites across all environments:

```bash
pio test -e native_stack_l46 -e native_stack_http -e native_ssh -e native_ssh_hardened -e native_ssh_conn -e native_compliance
```

To run a single specific environment (which is much faster):

```bash
pio test -e native
```

To regenerate the formatted Markdown test report locally:

```bash
bash test/run_tests.sh
```

---

### Running on Windows (PowerShell) and Linux (WSL)

The native suite is host-only, so on Windows it runs directly for almost every
environment. A few tests use POSIX-only seams (`gmtime_r`, ThreadSanitizer, the
`snmpget` interop) that the Windows MinGW toolchain does not provide, so those
build only on Linux. Continuous integration runs on Linux, so a green run under
**WSL (Ubuntu)** is the one that matches CI.

**On Windows (PowerShell) - the everyday path:**

```powershell
# one environment (fast)
pio test -e native_hostlink

# the formatting / lint gates, identical to CI:
clang-format -i src\services\hostlink\hostlink.cpp          # format C/C++ in place
clang-format --dry-run --Werror (git diff --name-only)     # check only (CI gate)
npx prettier@3.9.1 --write --end-of-line auto docs\*.md     # Markdown; --end-of-line auto avoids CRLF false flags
npx cspell --no-progress docs\ROADMAP.md                    # spellcheck (CI gate)
```

> A `git diff`-based `clang-format` check only sees **tracked** files: a brand
> new file is invisible until you `git add` it, so always run `clang-format` on
> any new file explicitly. (This is exactly what let an unformatted new header
> slip past a local check and fail the Code Formatting job in CI.)

**On Linux (WSL Ubuntu) - the CI-parity path:** PlatformIO lives in a venv at
`~/.pio-venv`, and the repo is visible under `/mnt/c/...`, so no copy is needed.

```bash
cd /mnt/c/Users/<you>/.../ProtoCore
export PATH="$HOME/.pio-venv/bin:$PATH"

pio test -e native_tsan        # a Linux-only environment (ThreadSanitizer)
bash test/run_tests.sh         # full suite + regenerates docs/TEST_REPORT.md
```

**Driving WSL from a Windows shell (Git Bash):** calling `wsl.exe` from Git Bash
mangles arguments in two ways worth knowing:

- Git Bash maps `/tmp` to the Windows temp folder and rewrites POSIX paths on the
  command line. Prefix the call with `MSYS_NO_PATHCONV=1` to stop the rewrite.
- Inline scripts with embedded quotes get re-quoted passing through `wsl.exe` and
  can lose variable assignments. The reliable pattern is to pipe the script in on
  **stdin** (stripping carriage returns first) so no fragile quoting survives:

```bash
# run a script file on WSL, robustly, from Git Bash:
tr -d '\r' < scripts/run_native.sh | MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash -l
```

To run the whole native suite in **parallel** on WSL (much faster than one serial
`pio test` invocation that builds every environment back to back):

```bash
envs=$(grep -oE '^\[env:native[A-Za-z0-9_]*\]' platformio.ini \
        | sed -E 's/\[env:(.*)\]/\1/' | grep -vE 'codeql')
printf '%s\n' $envs | xargs -P 6 -I{} pio test -e {}
```

---

### Step-by-Step: Writing a New Test Case

Let's walk through creating a test case to verify that the HTTP parser correctly parses a basic `GET` request.

#### Step 1: Open the Test Suite File

If you are testing parser mechanics, open `test/test_http_parser/test_http_parser.cpp`.

#### Step 2: Write the Test Function

Add a test function. Keep it self-contained and descriptive:

```cpp
void test_http_parser_simple_get_request() {
    // 1. Arrange: Initialize your parser state and sample request bytes
    http_parser_t parser;
    http_parser_init(&parser, 0); // Slot ID 0

    const char* request_bytes = "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";

    // 2. Act: Feed bytes incrementally to simulate packet arrivals
    size_t bytes_fed = http_parser_feed(&parser, request_bytes, strlen(request_bytes));

    // 3. Assert: Verify the state is correct
    TEST_ASSERT_EQUAL(strlen(request_bytes), bytes_fed);
    TEST_ASSERT_EQUAL(PARSE_STATE_COMPLETE, parser.state);
    TEST_ASSERT_EQUAL_STRING("/index.html", parser.path);
    TEST_ASSERT_EQUAL_STRING("GET", parser.method);
}
```

> [!TIP]
> Keep your descriptions inside the function body as a single line comment starting with `//`. The reporting scripts automatically parse these comments to generate documentation strings in the final report!

#### Step 3: Register the Test in `main()`

Scroll to the bottom of the test file where `main()` resides, and register your function using `RUN_TEST`:

```cpp
int main() {
    UNITY_BEGIN();

    // ... other registered tests ...
    RUN_TEST(test_http_parser_simple_get_request);

    return UNITY_END();
}
```

---

## 6. Expert-Level Debugging: Memory Safety & Sanitizers

When developing C++ code natively, we can compile our suites with compilers like `gcc` or `clang` and attach advanced debugging sanitizers that would be impossible to run on an actual ESP32 chip.

### AddressSanitizer (ASan)

If you run into segmentation faults or want to ensure your code has no memory leaks, you can enable AddressSanitizer. In your `platformio.ini` file, add:

```ini
[env:native]
platform = native
build_flags =
    -fsanitize=address,undefined
    -g
```

When you execute `pio test`, your host compiler compiles instrumentation checks around every pointer access. If a buffer overflow or use-after-free occurs, the test runner immediately stops and prints a stack trace pointing directly to the offending line of code.

### Simulating Race Conditions

We test session and socket race conditions by interleaved function calling:

1. Initialize the socket buffer.
2. Feed partial packets.
3. Call an intermediate tick handler (simulating thread preemption).
4. Assert that the buffer holds its state and has not entered an invalid transition.
   This is fully reproducible because there are no actual operating system threads involved.

## 7. Comprehensive Test Directory

<!-- BEGIN GENERATED test-directory (run test/gen_test_readme.py) -->

A thorough directory of all **0 test cases** across **0 suites**. Expand a suite to see its test cases, and a test case to see its objective and assertions.

<!-- END GENERATED test-directory -->
