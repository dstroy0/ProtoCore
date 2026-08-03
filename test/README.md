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
| `native_application` | default | `test_application` | test_application against the native_stack_http stack. |
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
| `native_codeql` | `PC_ENABLE_CSRF=1`, `PC_ENABLE_AUTH_LOCKOUT=1`, `PC_ENABLE_IP_ALLOWLIST=1`, `PC_ENABLE_WS_DEFLATE=1`, `PC_ENABLE_TIME_SOURCE=1`, `PC_ENABLE_CONFIG_STORE=1`, `PC_ENABLE_DEVICE_ID=1`, `PC_ENABLE_TELEMETRY=1`, `PC_ENABLE_DASHBOARD=1`, `PC_ENABLE_PARTITION_MONITOR=1`, `PC_ENABLE_CBOR=1`, `PC_ENABLE_MSGPACK=1`, `PC_ENABLE_GPIO_MAP=1`, `PC_ENABLE_UDP_TELEMETRY=1`, `PC_ENABLE_GUARDRAILS=1`, `PC_ENABLE_FAILSAFE=1`, `PC_ENABLE_SLEEP_SCHED=1`, `PC_ENABLE_WEARLEVEL=1`, `PC_ENABLE_NETADAPT=1`, `PC_ENABLE_DSHOT=1`, `PC_ENABLE_HART=1`, `PC_ENABLE_NTS=1`, `PC_ENABLE_DDS=1`, `PC_ENABLE_XMPP=1`, `PC_ENABLE_RAWL2=1`, `PC_ENABLE_SPA_ROUTER=1`, `PC_ENABLE_GOOSE=1`, `PC_ENABLE_MTCONNECT=1`, `PC_ENABLE_J2735=1`, `PC_ENABLE_NEMA_TS2=1`, `PC_ENABLE_SNP=1`, `PC_ENABLE_DIRECTNET=1`, `PC_ENABLE_SEP2=1`, `PC_ENABLE_PROFINET=1`, `PC_ENABLE_NTCIP=1`, `PC_ENABLE_OPENADR=1`, `PC_ENABLE_MMS=1`, `PC_ENABLE_CCLINK=1`, `PC_ENABLE_POWERLINK=1`, `PC_ENABLE_SERCOS=1`, `PC_ENABLE_PROFIBUS=1`, `PC_ENABLE_LONWORKS=1`, `PC_ENABLE_MBPLUS=1`, `PC_ENABLE_INTERBUS=1`, `PC_ENABLE_ICCP=1`, `PC_ENABLE_WAVE=1`, `PC_ENABLE_UTMC=1`, `PC_ENABLE_OCIT=1`, `PC_ENABLE_ATC=1`, `PC_ENABLE_SOUTHBOUND=1`, `PC_ENABLE_EXC_DECODER=1`, `PC_ENABLE_HTTP_DELIVERY=1`, `PC_ENABLE_HW_HEALTH=1`, `PC_ENABLE_MDNS_ADAPTIVE=1`, `PC_ENABLE_SOCKPOOL=1`, `PC_ENABLE_PSRAM_POOL=1`, `PC_ENABLE_HAPPY_EYEBALLS=1`, `PC_ENABLE_WIFI_SNIFFER=1`, `PC_ENABLE_LINK_MANAGER=1`, `PC_ENABLE_CC1101=1`, `PC_ENABLE_FDC2214=1`, `PC_ENABLE_LDC1614=1`, `PC_ENABLE_VL53L0X=1`, `PC_ENABLE_RADIO_SNIFF=1`, `PC_ENABLE_BLE_GATT=1`, `PC_ENABLE_TLS_POLICY=1`, `PC_ENABLE_WISUN=1`, `PC_ENABLE_LOGBUF=1`, `PC_ENABLE_OTA_ROLLBACK=1`, `PC_ENABLE_TOTP=1`, `PC_ENABLE_WEBHOOK=1`, `PC_ENABLE_RADIO_POWER=1`, `PC_ENABLE_AUDIT_LOG=1`, `PC_ENABLE_OIDC=1`, `PC_ENABLE_MNT=1`, `PC_ENABLE_GRAPHQL=1`, `PC_ENABLE_ESPNOW=1`, `PC_ENABLE_OAUTH2=1`, `PC_ENABLE_OPCUA=1`, `PC_ENABLE_OPCUA_CLIENT=1`, `PC_ENABLE_WEBSOCKET=1`, `PC_ENABLE_SSE=1` | `test_dispatch` | CodeQL coverage env: the full app compiled with every new feature flag ON so CodeQL traces the integration paths (CSRF / lockout / allowlist gates, permessage-deflate) AND the new service modules, whi... |
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

A thorough directory of all **283 test cases** across **8 suites**. Expand a suite to see its test cases, and a test case to see its objective and assertions.

<details>
<summary><b>test_application (100 tests)</b></summary>

  <details style="margin-left: 20px;">
    <summary><b>test_response_headers_that_do_not_fit_are_refused</b> &mdash; <i>(a) The status line alone overflows the header buffer.</i></summary>

    * **Objective**: (a) The status line alone overflows the header buffer.
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "HTTP/1.1 500"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "Connection: close"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "\\r\\n\\r\\n"))</code>
      * <code>Assert null (strstr(tcp_captured(), "aaaa"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "HTTP/1.1 500"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "\\r\\n\\r\\n"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_restart_and_stop</b> &mdash; <i>Before any listener, restart() forwards the no-listeners error (no stop()/begin()).</i></summary>

    * **Objective**: Before any listener, restart() forwards the no-listeners error (no stop()/begin()).
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_INT32(PC_ERR_NO_LISTENERS, restart());</code>
      * <code>TEST_ASSERT_EQUAL_INT32(0, listen((uint16_t)9500));</code>
      * <code>TEST_ASSERT_EQUAL_INT32(PC_OK, begin());</code>
      * <code>TEST_ASSERT_EQUAL_INT32(PC_OK, restart());</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_route_registration_variants_table_full</b> &mdash; <i>The dropped iface route does not dispatch: a request to it falls through (handler untouched).</i></summary>

    * **Objective**: The dropped iface route does not dispatch: a request to it falls through (handler untouched).
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_send_family_slot_and_conn_gone_guards</b> &mdash; <i>Send family slot and conn gone guards</i></summary>

    * **Objective**: Send family slot and conn gone guards
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_send_binary_body_with_nul</b> &mdash; <i>Send binary body with nul</i></summary>

    * **Objective**: Send binary body with nul
    * **Assertions**:
      * <code>Assert not null (strstr(out, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(out, "Content-Type: application/grpc-web+proto\\r\\n"))</code>
      * <code>Assert not null (strstr(out, "Content-Length: 10\\r\\n")); // counts the NUL bytes, not strlen (=0)</code>
      * <code>Assert true (hdr_end &gt; 0)</code>
      * <code>Assert equal uint (sizeof(body), (unsigned)(out_len - hdr_end))</code>
      * <code>Assert equal int (0, memcmp(out + hdr_end, body, sizeof(body)))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_redirect_response_and_code_normalization</b> &mdash; <i>An out-of-range redirect code normalizes to 302.</i></summary>

    * **Objective**: An out-of-range redirect code normalizes to 302.
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "307 Temporary Redirect"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "Location: /new"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "302 Found"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_request_error_paths_te_method_ws</b> &mdash; <i>Wrong method to a GET-only route -> 405 with an Allow header.</i></summary>

    * **Objective**: Wrong method to a GET-only route -> 405 with an Allow header.
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "405"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "Allow:"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "400"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "426"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ws_sse_upgrade_failure_paths</b> &mdash; <i>(a) A Sec-WebSocket-Key that does not base64-decode to 16 bytes -> ws_accept_key rejects -> 400.</i></summary>

    * **Objective**: (a) A Sec-WebSocket-Key that does not base64-decode to 16 bytes -> ws_accept_key rejects -> 400.
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "400"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "400"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "101"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sse_upgrade_pool_exhausted</b> &mdash; <i>Sse upgrade pool exhausted</i></summary>

    * **Objective**: Sse upgrade pool exhausted
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "text/event-stream"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_handler_reads_body</b> &mdash; <i>Handler reads body</i></summary>

    * **Objective**: Handler reads body
    * **Assertions**:
      * <code>Assert equal string ("hello", body_seen)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_handler_reads_query_param</b> &mdash; <i>Handler reads query param</i></summary>

    * **Objective**: Handler reads query param
    * **Assertions**:
      * <code>Assert equal string ("42", q_seen)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_handler_reads_header</b> &mdash; <i>Handler reads header</i></summary>

    * **Objective**: Handler reads header
    * **Assertions**:
      * <code>Assert equal string ("secret", h_seen)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_wildcard_before_exact_wildcard_wins</b> &mdash; <i>Wildcard before exact wildcard wins</i></summary>

    * **Objective**: Wildcard before exact wildcard wins
    * **Assertions**:
      * <code>Assert true (wildcard_called)</code>
      * <code>Assert false (exact_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fn_on_registers_and_dispatches</b> &mdash; <i>Fn on registers and dispatches</i></summary>

    * **Objective**: Fn on registers and dispatches
    * **Assertions**:
      * <code>Assert true (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fn_on_path_copied_null_terminated</b> &mdash; <i>A path of exactly MAX_PATH_LEN-1 chars must not overflow the route buffer.</i></summary>

    * **Objective**: A path of exactly MAX_PATH_LEN-1 chars must not overflow the route buffer.
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fn_on_table_full_extra_routes_dropped</b> &mdash; <i>Fill the table; on() beyond MAX_ROUTES must silently drop</i></summary>

    * **Objective**: Fill the table; on() beyond MAX_ROUTES must silently drop
    * **Assertions**:
      * <code>Assert true (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fn_on_same_path_different_methods_are_distinct</b> &mdash; <i>Fn on same path different methods are distinct</i></summary>

    * **Objective**: Fn on same path different methods are distinct
    * **Assertions**:
      * <code>Assert true (get_called)</code>
      * <code>Assert false (post_called)</code>
      * <code>Assert true (post_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fn_on_not_found_called_when_no_match</b> &mdash; <i>Fn on not found called when no match</i></summary>

    * **Objective**: Fn on not found called when no match
    * **Assertions**:
      * <code>Assert true (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fn_on_not_found_not_called_when_match_exists</b> &mdash; <i>Fn on not found not called when match exists</i></summary>

    * **Objective**: Fn on not found not called when match exists
    * **Assertions**:
      * <code>Assert true (handler_called)</code>
      * <code>Assert false (nf)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fn_set_cors_options_preflight_clears_slot</b> &mdash; <i>Fn set cors options preflight clears slot</i></summary>

    * **Objective**: Fn set cors options preflight clears slot
    * **Assertions**:
      * <code>Assert not equal (PARSE_COMPLETE, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fn_set_cors_empty_string_disables</b> &mdash; <i>Fn set cors empty string disables</i></summary>

    * **Objective**: Fn set cors empty string disables
    * **Assertions**:
      * <code>Assert true (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_wrong_method_does_not_match</b> &mdash; <i>Wrong method does not match</i></summary>

    * **Objective**: Wrong method does not match
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_wrong_path_does_not_match</b> &mdash; <i>Wrong path does not match</i></summary>

    * **Objective**: Wrong path does not match
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_all_http_methods_dispatched</b> &mdash; <i>All http methods dispatched</i></summary>

    * **Objective**: All http methods dispatched
    * **Assertions**:
      * <code>Assert equal message (1, counts[i], "method not dispatched")</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_root_path_matches_exactly</b> &mdash; <i>Root path matches exactly</i></summary>

    * **Objective**: Root path matches exactly
    * **Assertions**:
      * <code>Assert true (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_root_path_does_not_match_subpath</b> &mdash; <i>Root path does not match subpath</i></summary>

    * **Objective**: Root path does not match subpath
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_wildcard_matches_any_suffix</b> &mdash; <i>Wildcard matches any suffix</i></summary>

    * **Objective**: Wildcard matches any suffix
    * **Assertions**:
      * <code>Assert true (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_wildcard_does_not_match_unrelated_prefix</b> &mdash; <i>Wildcard does not match unrelated prefix</i></summary>

    * **Objective**: Wildcard does not match unrelated prefix
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_exact_route_wins_when_registered_first</b> &mdash; <i>Exact route wins when registered first</i></summary>

    * **Objective**: Exact route wins when registered first
    * **Assertions**:
      * <code>Assert true (exact_called)</code>
      * <code>Assert false (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_slot_not_stuck_in_complete_after_handle</b> &mdash; <i>Slot not stuck in complete after handle</i></summary>

    * **Objective**: Slot not stuck in complete after handle
    * **Assertions**:
      * <code>Assert not equal (PARSE_COMPLETE, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_parse_error_slot_auto_reset</b> &mdash; <i>Parse error slot auto reset</i></summary>

    * **Objective**: Parse error slot auto reset
    * **Assertions**:
      * <code>Assert equal (PARSE_ERROR, http_pool[0].parse_state)</code>
      * <code>Assert not equal (PARSE_ERROR, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>stress_last_route_dispatched_in_full_table</b> &mdash; <i>Stress - Last route dispatched in full table</i></summary>

    * **Objective**: Stress - Last route dispatched in full table
    * **Assertions**:
      * <code>Assert equal (1, last_count)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>stress_sequential_requests_no_state_leak</b> &mdash; <i>Stress - Sequential requests no state leak</i></summary>

    * **Objective**: Stress - Sequential requests no state leak
    * **Assertions**:
      * <code>Assert equal (50, seq_count)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>stress_all_slots_dispatched_simultaneously</b> &mdash; <i>Stress - All slots dispatched simultaneously</i></summary>

    * **Objective**: Stress - All slots dispatched simultaneously
    * **Assertions**:
      * <code>Assert equal message (1, counts[i], "slot not dispatched")</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>stress_wildcard_matches_many_paths</b> &mdash; <i>Stress - Wildcard matches many paths</i></summary>

    * **Objective**: Stress - Wildcard matches many paths
    * **Assertions**:
      * <code>Assert equal (10, wc_count)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>stress_handle_with_no_complete_slots_is_nop</b> &mdash; <i>All slots in PARSE_METHOD (setUp resets them) - nothing to dispatch</i></summary>

    * **Objective**: All slots in PARSE_METHOD (setUp resets them) - nothing to dispatch
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>race_slot_complete_between_handle_calls</b> &mdash; <i>Race - Slot complete between handle calls</i></summary>

    * **Objective**: Race - Slot complete between handle calls
    * **Assertions**:
      * <code>Assert false (dispatched)</code>
      * <code>Assert true (dispatched)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>race_conn_freed_after_parse_complete</b> &mdash; <i>Simulate connection drop between parse and dispatch</i></summary>

    * **Objective**: Simulate connection drop between parse and dispatch
    * **Assertions**:
      * <code>Assert equal (PARSE_COMPLETE, http_pool[0].parse_state)</code>
      * <code>Assert not equal (PARSE_COMPLETE, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>race_double_handle_no_double_dispatch</b> &mdash; <i>Race - Double handle no double dispatch</i></summary>

    * **Objective**: Race - Double handle no double dispatch
    * **Assertions**:
      * <code>Assert equal (1, dispatch_count)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>race_error_and_valid_slot_in_same_handle</b> &mdash; <i>Slot 0: inject a parse error</i></summary>

    * **Objective**: Slot 0: inject a parse error
    * **Assertions**:
      * <code>Assert equal (PARSE_ERROR, http_pool[0].parse_state)</code>
      * <code>Assert not equal (PARSE_ERROR, http_pool[0].parse_state)</code>
      * <code>Assert true (valid_dispatched)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>race_callback_manually_resets_slot</b> &mdash; <i>Race - Callback manually resets slot</i></summary>

    * **Objective**: Race - Callback manually resets slot
    * **Assertions**:
      * <code>Assert true (manual_reset_called)</code>
      * <code>Assert equal (PARSE_METHOD, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_uri_too_long_auto_resets_slot</b> &mdash; <i>Overflow the path buffer - handle() should send 414 and free the slot</i></summary>

    * **Objective**: Overflow the path buffer - handle() should send 414 and free the slot
    * **Assertions**:
      * <code>Assert equal (PARSE_URI_TOO_LONG, http_pool[0].parse_state)</code>
      * <code>Assert not equal (PARSE_URI_TOO_LONG, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_transfer_encoding_chunked_is_501</b> &mdash; <i>A request advertising Transfer-Encoding must be rejected with 501</i></summary>

    * **Objective**: A request advertising Transfer-Encoding must be rejected with 501
    * **Assertions**:
      * <code>Assert not equal (PARSE_COMPLETE, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_transfer_encoding_identity_is_501</b> &mdash; <i>Even "identity" is rejected - we advertise no TE support at all</i></summary>

    * **Objective**: Even "identity" is rejected - we advertise no TE support at all
    * **Assertions**:
      * <code>Assert not equal (PARSE_COMPLETE, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_redirect_emits_location_and_status</b> &mdash; <i>Redirect emits location and status</i></summary>

    * **Objective**: Redirect emits location and status
    * **Assertions**:
      * <code>Assert not null (strstr(out, "HTTP/1.1 301 Moved Permanently"))</code>
      * <code>Assert not null (strstr(out, "Location: /index.html\\r\\n"))</code>
      * <code>Assert not null (strstr(out, "Content-Length: 0\\r\\n"))</code>
      * <code>Assert equal (CONN_FREE, (ConnState)conn_pool[0].state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_redirect_invalid_code_defaults_to_302</b> &mdash; <i>Redirect invalid code defaults to 302</i></summary>

    * **Objective**: Redirect invalid code defaults to 302
    * **Assertions**:
      * <code>Assert not null (strstr(out, "HTTP/1.1 302 Found"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_mime_type_detection</b> &mdash; <i>Unknown / missing extension and dotfiles fall back.</i></summary>

    * **Objective**: Unknown / missing extension and dotfiles fall back.
    * **Assertions**:
      * <code>Assert equal string ("text/html", mime_type("/index.html"))</code>
      * <code>Assert equal string ("text/css", mime_type("/css/site.css"))</code>
      * <code>Assert equal string ("application/javascript", mime_type("/app.JS"))</code>
      * <code>Assert equal string ("application/json", mime_type("/api/data.json"))</code>
      * <code>Assert equal string ("image/svg+xml", mime_type("logo.svg"))</code>
      * <code>Assert equal string ("image/png", mime_type("a.b.c.png"))</code>
      * <code>Assert equal string ("application/octet-stream", mime_type("/file.unknownext"))</code>
      * <code>Assert equal string ("application/octet-stream", mime_type("/noext"))</code>
      * <code>Assert equal string ("application/octet-stream", mime_type("/dir.with.dot/file"))</code>
      * <code>Assert equal string ("application/octet-stream", mime_type(NULL))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_file_and_mime</b> &mdash; <i>Serve static file and mime</i></summary>

    * **Objective**: Serve static file and mime
    * **Assertions**:
      * <code>Assert not null (strstr(out, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(out, "Content-Type: text/css"))</code>
      * <code>Assert not null (strstr(out, "body{color:red}"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_wildcard_and_route_full</b> &mdash; <i>Serve static wildcard and route full</i></summary>

    * **Objective**: Serve static wildcard and route full
    * **Assertions**:
      * <code>Assert not null (strstr(out, "HTTP/1.1 200 OK"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_response_header_cookie_guards</b> &mdash; <i>Response header cookie guards</i></summary>

    * **Objective**: Response header cookie guards
    * **Assertions**:
      * <code>Assert not null (strstr(out, "X-Ok: 1"))</code>
      * <code>Assert null (strstr(out, "toobig"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_index_fallback</b> &mdash; <i>Serve static index fallback</i></summary>

    * **Objective**: Serve static index fallback
    * **Assertions**:
      * <code>Assert not null (strstr(out, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(out, "Content-Type: text/html"))</code>
      * <code>Assert not null (strstr(out, "&lt;h1&gt;home&lt;/h1&gt;"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_gzip_when_accepted</b> &mdash; <i>Serve static gzip when accepted</i></summary>

    * **Objective**: Serve static gzip when accepted
    * **Assertions**:
      * <code>Assert not null (strstr(out, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(out, "Content-Type: application/javascript"))</code>
      * <code>Assert not null (strstr(out, "Content-Encoding: gzip"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_no_gzip_when_not_accepted</b> &mdash; <i>Serve static no gzip when not accepted</i></summary>

    * **Objective**: Serve static no gzip when not accepted
    * **Assertions**:
      * <code>Assert null (strstr(out, "Content-Encoding: gzip"))</code>
      * <code>Assert not null (strstr(out, "console.log(1)"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_traversal_not_leaked</b> &mdash; <i>Serve static traversal not leaked</i></summary>

    * **Objective**: Serve static traversal not leaked
    * **Assertions**:
      * <code>Assert null (strstr(out, "topsecret"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_missing_is_404</b> &mdash; <i>Serve static missing is 404</i></summary>

    * **Objective**: Serve static missing is 404
    * **Assertions**:
      * <code>Assert not null (strstr(out, "404"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_etag_conditional_get</b> &mdash; <i>First GET: 200 with an ETag header.</i></summary>

    * **Objective**: First GET: 200 with an ETag header.
    * **Assertions**:
      * <code>Assert not null (strstr(out1, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (etp)</code>
      * <code>Assert not null (strstr(out2, "304 Not Modified"))</code>
      * <code>Assert not null (strstr(out2, etag))</code>
      * <code>Assert null (strstr(out2, "&lt;html&gt;hi&lt;/html&gt;"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_inm_star_list_weak</b> &mdash; <i>First GET to capture the strong ETag (with quotes).</i></summary>

    * **Objective**: First GET to capture the strong ETag (with quotes).
    * **Assertions**:
      * <code>Assert not null (etp)</code>
      * <code>Assert not null (strstr(tcp_captured(), "304 Not Modified"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "304 Not Modified"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "304 Not Modified"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "HTTP/1.1 200 OK"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_last_modified_conditional_get</b> &mdash; <i>(1) plain GET: 200 carries the Last-Modified header.</i></summary>

    * **Objective**: (1) plain GET: 200 carries the Last-Modified header.
    * **Assertions**:
      * <code>Assert not null (strstr(o, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(o, "Last-Modified: Thu, 01 Jan 1970 00:16:40 GMT\\r\\n"))</code>
      * <code>Assert not null (strstr(o, "304 Not Modified"))</code>
      * <code>Assert null (strstr(o, "&lt;html&gt;hi&lt;/html&gt;"))</code>
      * <code>Assert not null (strstr(o, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(o, "&lt;html&gt;hi&lt;/html&gt;"))</code>
      * <code>Assert not null (strstr(o, "304 Not Modified"))</code>
      * <code>Assert not null (strstr(o, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(o, "&lt;html&gt;hi&lt;/html&gt;"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_ims_field_comparisons</b> &mdash; <i>The year-younger direction takes the other branch of the same compare -> 200.</i></summary>

    * **Objective**: The year-younger direction takes the other branch of the same compare -> 200.
    * **Assertions**:
      * <code>Assert not null (strstr(o, "304 Not Modified"))</code>
      * <code>Assert null (strstr(o, "&lt;html&gt;hi&lt;/html&gt;"))</code>
      * <code>Assert not null (strstr(o, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(o, "&lt;html&gt;hi&lt;/html&gt;"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_unrepresentable_mtime</b> &mdash; <i>(a) plain GET: 200 with no Last-Modified line (http_rfc1123 bailed).</i></summary>

    * **Objective**: (a) plain GET: 200 with no Last-Modified line (http_rfc1123 bailed).
    * **Assertions**:
      * <code>Assert not null (strstr(o, "HTTP/1.1 200 OK"))</code>
      * <code>Assert null (strstr(o, "Last-Modified:"))</code>
      * <code>Assert not null (strstr(o, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(o, "&lt;html&gt;hi&lt;/html&gt;"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_if_modified_since_malformed</b> &mdash; <i>Serve static if modified since malformed</i></summary>

    * **Objective**: Serve static if modified since malformed
    * **Assertions**:
      * <code>Assert not null (strstr(o, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(o, "&lt;html&gt;hi&lt;/html&gt;"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_serve_static_cache_control</b> &mdash; <i>Clearing it removes the header (and restores the default for later tests).</i></summary>

    * **Objective**: Clearing it removes the header (and restores the default for later tests).
    * **Assertions**:
      * <code>Assert not null (strstr(out, "HTTP/1.1 200 OK"))</code>
      * <code>Assert not null (strstr(out, "Cache-Control: max-age=3600"))</code>
      * <code>Assert null (strstr(out, "Cache-Control:"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_request_log_hook_fires</b> &mdash; <i>Request log hook fires</i></summary>

    * **Objective**: Request log hook fires
    * **Assertions**:
      * <code>Assert equal int (1, g_log_calls)</code>
      * <code>Assert equal string ("GET", g_log_method)</code>
      * <code>Assert equal string ("/hi", g_log_path)</code>
      * <code>Assert equal int (200, g_log_status)</code>
      * <code>Assert equal int (5, g_log_bytes)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_stats_endpoint_emits_json</b> &mdash; <i>Stats endpoint emits json</i></summary>

    * **Objective**: Stats endpoint emits json
    * **Assertions**:
      * <code>Assert not null (strstr(out, "application/json"))</code>
      * <code>Assert not null (strstr(out, "\\"uptime_ms\\""))</code>
      * <code>Assert not null (strstr(out, "\\"requests\\""))</code>
      * <code>Assert not null (strstr(out, "\\"http_2xx\\""))</code>
      * <code>Assert not null (strstr(out, "\\"http_4xx\\""))</code>
      * <code>Assert not null (strstr(out, "\\"active_conns\\""))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_status_text_reason_phrases</b> &mdash; <i>Status text reason phrases</i></summary>

    * **Objective**: Status text reason phrases
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), want))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_allow_header_lists_methods</b> &mdash; <i>Allow header lists methods</i></summary>

    * **Objective**: Allow header lists methods
    * **Assertions**:
      * <code>Assert not null (strstr(out, "405"))</code>
      * <code>Assert not null (strstr(out, "PATCH"))</code>
      * <code>Assert not null (strstr(out, "OPTIONS"))</code>
      * <code>Assert not null (strstr(out, "HEAD"))</code>
      * <code>Assert not null (strstr(out, "PUT"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_listen_and_begin</b> &mdash; <i>begin() before any listen() -> no-listeners error, no side effects.</i></summary>

    * **Objective**: begin() before any listen() -> no-listeners error, no side effects.
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_INT32(PC_ERR_NO_LISTENERS, begin());</code>
      * <code>TEST_ASSERT_EQUAL_INT32(i, listen((uint16_t)(9100 + i)));</code>
      * <code>TEST_ASSERT_EQUAL_INT32(PC_ERR_LISTENER_FULL, listen(9999));</code>
      * <code>TEST_ASSERT_EQUAL_INT32(PC_OK, begin());</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_begin_port_convenience</b> &mdash; <i>Begin port convenience</i></summary>

    * **Objective**: Begin port convenience
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_INT32(PC_OK, begin_http((uint16_t)8080));</code>
      * <code>TEST_ASSERT_EQUAL_INT32(PC_ERR_LISTENER_FULL, begin_http((uint16_t)9999));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ws_send_api</b> &mdash; <i>Guards: out-of-range id and an id that is in range but inactive.</i></summary>

    * **Objective**: Guards: out-of-range id and an id that is in range but inactive.
    * **Assertions**:
      * <code>Assert not null (ws)</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>Assert true (tcp_captured_len() &gt;= 2)</code>
      * <code>TEST_ASSERT_EQUAL_HEX8(0x81, (uint8_t)tcp_captured()[0]);</code>
      * <code>Assert true (tcp_captured_len() &gt;= 2)</code>
      * <code>TEST_ASSERT_EQUAL_HEX8(0x82, (uint8_t)tcp_captured()[0]);</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>Assert true (tcp_captured_len() &gt;= 2)</code>
      * <code>TEST_ASSERT_EQUAL_HEX8(0x88, (uint8_t)tcp_captured()[0]);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sse_broadcast_after_upgrade_matches_path</b> &mdash; <i>Sse broadcast after upgrade matches path</i></summary>

    * **Objective**: Sse broadcast after upgrade matches path
    * **Assertions**:
      * <code>Assert not null (strstr(out, "text/event-stream"))</code>
      * <code>Assert not null (strstr(out, "data: hello"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sse_send_api</b> &mdash; <i>Guards send nothing.</i></summary>

    * **Objective**: Guards send nothing.
    * **Assertions**:
      * <code>Assert not null (sse)</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>Assert not null (strstr(out, "event: msg"))</code>
      * <code>Assert not null (strstr(out, "id: 42"))</code>
      * <code>Assert not null (strstr(out, "data: hi"))</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_metrics_emits_prometheus</b> &mdash; <i>Every sample line must actually carry a VALUE. Asserting only that the metric NAME appears</i></summary>

    * **Objective**: Every sample line must actually carry a VALUE. Asserting only that the metric NAME appears
    * **Assertions**:
      * <code>Assert not null (strstr(out, "text/plain; version=0.0.4"))</code>
      * <code>Assert not null (strstr(out, "# TYPE pc_http_requests_total counter"))</code>
      * <code>Assert not null (strstr(out, "pc_http_responses_total{class=\\"2xx\\"}"))</code>
      * <code>Assert not null (strstr(out, "pc_free_heap_bytes"))</code>
      * <code>Assert not null (strstr(out, "pc_uptime_seconds"))</code>
      * <code>Assert not null (body)</code>
      * <code>Assert not null message (sp, "metric sample line has no value separator")</code>
      * <code>Assert true message ((size_t)(sp - ln) + 1 &lt; len, "metric sample line has an empty value")</code>
      * <code>Assert true message (samples &gt;= 11, "expected every metric placeholder to emit a sample")</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_stats_counters_ignore_sub_200_status</b> &mdash; <i>Stats counters ignore sub 200 status</i></summary>

    * **Objective**: Stats counters ignore sub 200 status
    * **Assertions**:
      * <code>Assert not null (strstr(out, "\\"requests\\":1"))</code>
      * <code>Assert not null (strstr(out, "\\"http_2xx\\":0"))</code>
      * <code>Assert not null (strstr(out, "\\"http_4xx\\":0"))</code>
      * <code>Assert not null (strstr(out, "\\"http_5xx\\":0"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_response_trailer_cors_block_and_null_disable</b> &mdash; <i>Response trailer cors block and null disable</i></summary>

    * **Objective**: Response trailer cors block and null disable
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "Access-Control-Allow-Origin: https://a.example\\r\\n"))</code>
      * <code>Assert null (strstr(tcp_captured(), "Access-Control-Allow-Origin"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_cache_control_null_clears_header</b> &mdash; <i>Cache control null clears header</i></summary>

    * **Objective**: Cache control null clears header
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "200 OK"))</code>
      * <code>Assert null (strstr(tcp_captured(), "Cache-Control"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_empty_route_pattern_matches_nothing</b> &mdash; <i>Empty route pattern matches nothing</i></summary>

    * **Objective**: Empty route pattern matches nothing
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
      * <code>Assert not null (strstr(tcp_captured(), "404"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_path_param_capture_limits</b> &mdash; <i>An over-long :name and an over-long value are both truncated, not overflowed.</i></summary>

    * **Objective**: An over-long :name and an over-long value are both truncated, not overflowed.
    * **Assertions**:
      * <code>Assert true (handler_called)</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(MAX_PATH_PARAMS, g_seen_param_count); // 5th capture dropped</code>
      * <code>Assert equal string ("4", g_seen_params[3].val)</code>
      * <code>Assert true (handler_called)</code>
      * <code>Assert equal uint (QUERY_KEY_LEN - 1, (unsigned)strlen(g_seen_params[0].key))</code>
      * <code>Assert equal uint (QUERY_VAL_LEN - 1, (unsigned)strlen(g_seen_params[0].val))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_path_param_segment_mismatches</b> &mdash; <i>"//v": both route and path carry an empty segment, then ":a" captures "v".</i></summary>

    * **Objective**: "//v": both route and path carry an empty segment, then ":a" captures "v".
    * **Assertions**:
      * <code>Assert false message (handler_called, misses[i])</code>
      * <code>Assert not null message (strstr(tcp_captured(), "404"), misses[i])</code>
      * <code>Assert true (handler_called)</code>
      * <code>Assert equal string ("v", g_seen_params[0].val)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_worker_owner_filter_skips_foreign_slot</b> &mdash; <i>Worker owner filter skips foreign slot</i></summary>

    * **Objective**: Worker owner filter skips foreign slot
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
      * <code>Assert equal (PARSE_COMPLETE, http_pool[1].parse_state)</code>
      * <code>Assert true (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_slot_poll_requires_registered_handler_with_poll</b> &mdash; <i>Slot poll requires registered handler with poll</i></summary>

    * **Objective**: Slot poll requires registered handler with poll
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
      * <code>Assert false (handler_called)</code>
      * <code>Assert true (handler_called)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_entity_too_large_auto_413</b> &mdash; <i>Entity too large auto 413</i></summary>

    * **Objective**: Entity too large auto 413
    * **Assertions**:
      * <code>Assert equal (PARSE_ENTITY_TOO_LARGE, http_pool[0].parse_state)</code>
      * <code>Assert false (handler_called)</code>
      * <code>Assert not null (strstr(tcp_captured(), "413 Payload Too Large"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_allow_header_dedupes_repeated_method</b> &mdash; <i>Allow header dedupes repeated method</i></summary>

    * **Objective**: Allow header dedupes repeated method
    * **Assertions**:
      * <code>Assert not null (strstr(out, "Allow: POST\\r\\n"))</code>
      * <code>Assert null (strstr(out, "POST, POST"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_error_close_head_and_dead_connection</b> &mdash; <i>HEAD on a POST-only route -> 405 headers, no body.</i></summary>

    * **Objective**: HEAD on a POST-only route -> 405 headers, no body.
    * **Assertions**:
      * <code>Assert not null (strstr(out, "405 Method Not Allowed"))</code>
      * <code>Assert null (strstr(out, "\\r\\n\\r\\nMethod Not Allowed"))</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_transfer_encoding_on_semantic_ingress_is_501</b> &mdash; <i>Transfer encoding on semantic ingress is 501</i></summary>

    * **Objective**: Transfer encoding on semantic ingress is 501
    * **Assertions**:
      * <code>Assert false (handler_called)</code>
      * <code>Assert not null (strstr(tcp_captured(), "501 Not Implemented"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_static_mount_rejects_non_get_methods</b> &mdash; <i>Static mount rejects non get methods</i></summary>

    * **Objective**: Static mount rejects non get methods
    * **Assertions**:
      * <code>Assert not null (strstr(out, "405"))</code>
      * <code>Assert not null (strstr(out, "Allow: GET, HEAD\\r\\n"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_send_null_payload_and_slot_bounds</b> &mdash; <i>Send null payload and slot bounds</i></summary>

    * **Objective**: Send null payload and slot bounds
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>Assert not null (strstr(tcp_captured(), "Content-Length: 0\\r\\n"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_send_body_framing_paths</b> &mdash; <i>HEAD: headers only, but Content-Length still describes the would-be body.</i></summary>

    * **Objective**: HEAD: headers only, but Content-Length still describes the would-be body.
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "Content-Length: 6\\r\\n"))</code>
      * <code>Assert null (strstr(tcp_captured(), "abcdef"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "Content-Length: 0\\r\\n"))</code>
      * <code>Assert not null (strstr(out, want))</code>
      * <code>Assert equal uint (sizeof(big) - 1, (unsigned)strlen(strstr(out, "\\r\\n\\r\\n") + 4))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_send_empty_and_redirect_dead_connection_guards</b> &mdash; <i>Send empty and redirect dead connection guards</i></summary>

    * **Objective**: Send empty and redirect dead connection guards
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>Assert equal (PARSE_METHOD, http_pool[0].parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_send_template_placeholder_edges</b> &mdash; <i>Send template placeholder edges</i></summary>

    * **Objective**: Send template placeholder edges
    * **Assertions**:
      * <code>Assert not null (strstr(out, "a{{0123456789012345678901234567890123}}b"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "Content-Length: 0\\r\\n"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_send_chunked_without_source</b> &mdash; <i>Send chunked without source</i></summary>

    * **Objective**: Send chunked without source
    * **Assertions**:
      * <code>Assert not null (strstr(out, "Transfer-Encoding: chunked\\r\\n"))</code>
      * <code>Assert null (strstr(out, "0\\r\\n\\r\\n"))</code>
      * <code>Assert false (pc_resp_holds_slot(0))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_chunked_pump_small_window_and_connection_lost</b> &mdash; <i>No window at all: the body parks in the pump, still active.</i></summary>

    * **Objective**: No window at all: the body parks in the pump, still active.
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "Transfer-Encoding: chunked\\r\\n"))</code>
      * <code>Assert not null (strstr(tcp_captured(), "28\\r\\nqqqq"))</code>
      * <code>Assert false (pc_resp_holds_slot(0))</code>
      * <code>Assert true (pc_resp_holds_slot(0))</code>
      * <code>Assert false (pc_resp_holds_slot(0))</code>
      * <code>Assert null (strstr(tcp_captured(), "qqqq"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_response_header_null_value_empty_attrs_and_overflow</b> &mdash; <i>Response header null value empty attrs and overflow</i></summary>

    * **Objective**: Response header null value empty attrs and overflow
    * **Assertions**:
      * <code>Assert not null (strstr(out, "X-Keep: 1\\r\\n"))</code>
      * <code>Assert not null (strstr(out, "Set-Cookie: sid=abc\\r\\n"))</code>
      * <code>Assert null (strstr(out, "X-Null"))</code>
      * <code>Assert null (strstr(out, "c-null"))</code>
      * <code>Assert null (strstr(out, "X-Too-Big"))</code>
      * <code>Assert null (strstr(out, "ffff"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_mime_type_extension_edges</b> &mdash; <i>Mime type extension edges</i></summary>

    * **Objective**: Mime type extension edges
    * **Assertions**:
      * <code>Assert equal string ("application/octet-stream", mime_type("/file."))</code>
      * <code>Assert equal string ("application/octet-stream", mime_type("/a.7z"))</code>
      * <code>Assert equal string ("application/octet-stream", mime_type("/a.jsx"))</code>
      * <code>Assert equal string ("application/octet-stream", mime_type("/a.h"))</code>
      * <code>Assert equal string ("font/woff2", mime_type("/a.WOFF2"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ws_upgrade_without_connect_handler</b> &mdash; <i>Ws upgrade without connect handler</i></summary>

    * **Objective**: Ws upgrade without connect handler
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "101 Switching Protocols"))</code>
      * <code>Assert not null (ws_find(0))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ws_dispatch_without_message_or_close_handler</b> &mdash; <i>Ws dispatch without message or close handler</i></summary>

    * **Objective**: Ws dispatch without message or close handler
    * **Assertions**:
      * <code>Assert not null (ws)</code>
      * <code>Assert not null (ws_find(0))</code>
      * <code>Assert not equal (WS_FRAME_READY, ws-&gt;parse_state)</code>
      * <code>Assert null (ws_find(0))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ws_upgrade_handshake_gate</b> &mdash; <i>A real handshake with no Sec-WebSocket-Version at all -> 426, same as a wrong one.</i></summary>

    * **Objective**: A real handshake with no Sec-WebSocket-Version at all -> 426, same as a wrong one.
    * **Assertions**:
      * <code>Assert not null message (strstr(tcp_captured(), "400"), bad[i])</code>
      * <code>Assert not null (strstr(tcp_captured(), "426 Upgrade Required"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ws_send_api_inactive_error_state_and_dead_slot</b> &mdash; <i>In range but not allocated.</i></summary>

    * **Objective**: In range but not allocated.
    * **Assertions**:
      * <code>Assert not null (ws)</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_upgrade_entry_points_on_dead_slot</b> &mdash; <i>Upgrade entry points on dead slot</i></summary>

    * **Objective**: Upgrade entry points on dead slot
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>Assert equal (PARSE_METHOD, http_pool[0].parse_state)</code>
      * <code>Assert false (ws_do_upgrade(0, &http_pool[0], NULL))</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
      * <code>Assert false (pc_sse_do_upgrade(0, &http_pool[0], NULL))</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sse_upgrade_fires_connect_handler</b> &mdash; <i>Sse upgrade fires connect handler</i></summary>

    * **Objective**: Sse upgrade fires connect handler
    * **Assertions**:
      * <code>Assert not null (strstr(tcp_captured(), "text/event-stream"))</code>
      * <code>Assert equal int (1, g_sse_connect_calls)</code>
      * <code>Assert not null (pc_sse_find(0))</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(pc_sse_find(0)-&gt;pc_sse_id, g_sse_connected_id);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sse_send_on_dead_slot_writes_nothing</b> &mdash; <i>Sse send on dead slot writes nothing</i></summary>

    * **Objective**: Sse send on dead slot writes nothing
    * **Assertions**:
      * <code>Assert not null (sse)</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());</code>
  </details>

</details>

<details>
<summary><b>test_concurrency (2 tests)</b></summary>

  <details style="margin-left: 20px;">
    <summary><b>test_spsc_ring_no_race</b> &mdash; <i>Spsc ring no race</i></summary>

    * **Objective**: Spsc ring no race
    * **Assertions**:
      * <code>Assert true message (ok, "SPSC ring delivered corrupted/out-of-order bytes")</code>
      * <code>Assert equal int (kCount, recv)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_state_handoff_no_race</b> &mdash; <i>The slot ends FREE; the observer must not have crashed/torn (TSan checks the</i></summary>

    * **Objective**: The slot ends FREE; the observer must not have crashed/torn (TSan checks the
    * **Assertions**:
      * <code>Assert equal int (CONN_FREE, (ConnState)g_slot.state)</code>
  </details>

</details>

<details>
<summary><b>test_gateway (13 tests)</b></summary>

  <details style="margin-left: 20px;">
    <summary><b>test_uplink_envelopes_and_publishes</b> &mdash; <i>Uplink envelopes and publishes</i></summary>

    * **Objective**: Uplink envelopes and publishes
    * **Assertions**:
      * <code>Assert true (add_port(0, PC_GW_LORA, 0, PROTO_FALSE))</code>
      * <code>Assert true (pc_gateway_uplink(0, 0x42, hi, 2, -50))</code>
      * <code>TEST_ASSERT_EQUAL_size_t(1, g_up.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0x42, g_up[0].src_addr);</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(0, g_up[0].port_id);</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(PC_GW_LORA, g_up[0].kind);</code>
      * <code>TEST_ASSERT_EQUAL_INT16(-50, g_up[0].rssi);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, g_up[0].seq);</code>
      * <code>Assert equal memory (hi, g_up[0].payload.data(), 2)</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, stats().up_published);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_uplink_no_sink_drops</b> &mdash; <i>Uplink no sink drops</i></summary>

    * **Objective**: Uplink no sink drops
    * **Assertions**:
      * <code>Assert false (pc_gateway_uplink(0, 1, x, 1, 0))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, stats().up_published);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_uplink_unknown_port_drops</b> &mdash; <i>Uplink unknown port drops</i></summary>

    * **Objective**: Uplink unknown port drops
    * **Assertions**:
      * <code>Assert false (pc_gateway_uplink(9, 1, x, 1, 0))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, g_up.size());</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_uplink_rate_cap</b> &mdash; <i>Uplink rate cap</i></summary>

    * **Objective**: Uplink rate cap
    * **Assertions**:
      * <code>Assert true (pc_gateway_uplink(0, 1, x, 1, 0))</code>
      * <code>Assert true (pc_gateway_uplink(0, 1, x, 1, 0))</code>
      * <code>Assert false (pc_gateway_uplink(0, 1, x, 1, 0))</code>
      * <code>TEST_ASSERT_EQUAL_size_t(2, g_up.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);</code>
      * <code>Assert true (pc_gateway_uplink(0, 1, x, 1, 0))</code>
      * <code>TEST_ASSERT_EQUAL_size_t(3, g_up.size());</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_uplink_sink_refusal_counted</b> &mdash; <i>Uplink sink refusal counted</i></summary>

    * **Objective**: Uplink sink refusal counted
    * **Assertions**:
      * <code>Assert false (pc_gateway_uplink(0, 1, x, 1, 0))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, stats().up_published);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_downlink_transmits</b> &mdash; <i>Downlink transmits</i></summary>

    * **Objective**: Downlink transmits
    * **Assertions**:
      * <code>Assert true (pc_gateway_downlink(0, 0x10, cmd, 3))</code>
      * <code>TEST_ASSERT_EQUAL_size_t(1, g_down.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(0, g_down[0].port_id);</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0x10, g_down[0].dst);</code>
      * <code>Assert equal memory (cmd, g_down[0].payload.data(), 3)</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, stats().down_sent);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_downlink_no_tx_or_unknown_port_drops</b> &mdash; <i>Downlink no tx or unknown port drops</i></summary>

    * **Objective**: Downlink no tx or unknown port drops
    * **Assertions**:
      * <code>Assert false (pc_gateway_downlink(0, 1, x, 1))</code>
      * <code>Assert false (pc_gateway_downlink(9, 1, x, 1))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(2, stats().down_dropped);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_downlink_tx_refusal_counted</b> &mdash; <i>Downlink tx refusal counted</i></summary>

    * **Objective**: Downlink tx refusal counted
    * **Assertions**:
      * <code>Assert false (pc_gateway_downlink(0, 1, x, 1))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, stats().down_dropped);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, stats().down_sent);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_topic_format</b> &mdash; <i>Topic format</i></summary>

    * **Objective**: Topic format
    * **Assertions**:
      * <code>Assert equal string ("gw/2/66", buf)</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(7, n);</code>
      * <code>Assert equal string ("lora/2/66", buf)</code>
      * <code>Assert equal string ("gw/2/66", buf)</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(&m, tiny, sizeof(tiny))); // too small -&gt; 0</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(&m, NULL, sizeof(buf)));  // null buf -&gt; 0</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_add_port_validation_and_table_full</b> &mdash; <i>Add port validation and table full</i></summary>

    * **Objective**: Add port validation and table full
    * **Assertions**:
      * <code>Assert false (pc_gateway_add_port(NULL))</code>
      * <code>Assert true (add_port(0, PC_GW_LORA, 0, PROTO_FALSE))</code>
      * <code>Assert false (add_port(0, PC_GW_LORA, 0, PROTO_FALSE))</code>
      * <code>Assert true (add_port(1, PC_GW_NRF24, 0, PROTO_FALSE))</code>
      * <code>Assert true (add_port(2, PC_GW_ZIGBEE, 0, PROTO_FALSE))</code>
      * <code>Assert true (add_port(3, PC_GW_BLE, 0, PROTO_FALSE))</code>
      * <code>Assert false (add_port(4, PC_GW_LORA, 0, PROTO_FALSE)); // table full (PC_GW_MAX_PORTS = 4)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_seq_increments_per_uplink</b> &mdash; <i>Seq increments per uplink</i></summary>

    * **Objective**: Seq increments per uplink
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_size_t(2, g_up.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, g_up[0].seq);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, g_up[1].seq);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_topic_zero_and_overflow_steps</b> &mdash; <i>Topic zero and overflow steps</i></summary>

    * **Objective**: Topic zero and overflow steps
    * **Assertions**:
      * <code>Assert true (pc_gateway_topic(&m, buf, sizeof(buf)) &gt; 0)</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(NULL, buf, sizeof(buf))); // null msg</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(&m, buf, 0));             // zero buflen</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(&m, buf, cap)); // prefix, both '/'s, both digits</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_get_stats_null_out_is_noop</b> &mdash; <i>Get stats null out is noop</i></summary>

    * **Objective**: Get stats null out is noop
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT32(1, stats().up_published); // real stats unaffected</code>
  </details>

</details>

<details>
<summary><b>test_h2_conn (30 tests)</b></summary>

  <details style="margin-left: 20px;">
    <summary><b>test_init_and_request</b> &mdash; <i>Assemble: preface + empty client SETTINGS + HEADERS(stream 1, END_HEADERS\|END_STREAM).</i></summary>

    * **Objective**: Assemble: preface + empty client SETTINGS + HEADERS(stream 1, END_HEADERS\|END_STREAM).
    * **Assertions**:
      * <code>Assert equal int (1, count_frames(cap.out, H2_SETTINGS, &acks))</code>
      * <code>Assert equal int (0, acks)</code>
      * <code>Assert true (pc_h2_conn_recv(&c, in.data(), in.size()))</code>
      * <code>Assert equal int (4, (int)cap.req_headers.size())</code>
      * <code>Assert equal string (":method", cap.req_headers[0].first.c_str())</code>
      * <code>Assert equal string ("GET", cap.req_headers[0].second.c_str())</code>
      * <code>Assert equal string (":path", cap.req_headers[2].first.c_str())</code>
      * <code>Assert equal string ("/", cap.req_headers[2].second.c_str())</code>
      * <code>Assert equal string (":authority", cap.req_headers[3].first.c_str())</code>
      * <code>Assert equal string ("example.com", cap.req_headers[3].second.c_str())</code>
      * <code>Assert equal int (1, (int)cap.headers_end.size())</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, cap.headers_end[0]);</code>
      * <code>Assert true (cap.last_end_stream)</code>
      * <code>Assert equal int (1, acks2)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_respond_roundtrip</b> &mdash; <i>Output holds a HEADERS frame + a DATA frame on stream 1.</i></summary>

    * **Objective**: Output holds a HEADERS frame + a DATA frame on stream 1.
    * **Assertions**:
      * <code>Assert true (pc_h2_conn_recv(&c, in.data(), in.size()))</code>
      * <code>Assert true (pc_h2_conn_respond(&c, 1, 200, "text/plain", "hi", 2))</code>
      * <code>Assert equal int (1, count_frames(cap.out, H2_HEADERS))</code>
      * <code>Assert equal int (1, count_frames(cap.out, H2_DATA))</code>
      * <code>Assert equal int (3, (int)rh.size())</code>
      * <code>Assert equal string (":status", rh[0].first.c_str())</code>
      * <code>Assert equal string ("200", rh[0].second.c_str())</code>
      * <code>Assert equal string ("content-type", rh[1].first.c_str())</code>
      * <code>Assert equal string ("text/plain", rh[1].second.c_str())</code>
      * <code>Assert equal string ("content-length", rh[2].first.c_str())</code>
      * <code>Assert equal string ("2", rh[2].second.c_str())</code>
      * <code>Assert equal string ("hi", data.c_str())</code>
      * <code>Assert true (data_end)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ping_and_split_recv</b> &mdash; <i>Preface, then a PING frame, fed one byte at a time (exercises reassembly).</i></summary>

    * **Objective**: Preface, then a PING frame, fed one byte at a time (exercises reassembly).
    * **Assertions**:
      * <code>Assert true (pc_h2_conn_recv(&c, &in[k], 1))</code>
      * <code>Assert equal int (1, count_frames(cap.out, H2_PING, &acks))</code>
      * <code>Assert equal int (1, acks)</code>
      * <code>Assert equal memory (op, &cap.out[i + 9], 8)</code>
      * <code>Assert true (found)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_bad_preface</b> &mdash; <i>Bad preface</i></summary>

    * **Objective**: Bad preface
    * **Assertions**:
      * <code>Assert false (pc_h2_conn_recv(&c, junk, sizeof junk))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_headers_padded_priority</b> &mdash; <i>H2 headers padded priority</i></summary>

    * **Objective**: H2 headers padded priority
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_HEADERS, flags, 1, pl.data(), pl.size()))</code>
      * <code>Assert equal int (4, (int)cap.req_headers.size())</code>
      * <code>Assert true (cap.last_end_stream)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_headers_pad_overflow</b> &mdash; <i>H2 headers pad overflow</i></summary>

    * **Objective**: H2 headers pad overflow
    * **Assertions**:
      * <code>Assert false (feed_frame(c, H2_HEADERS, H2_FLAG_PADDED | H2_FLAG_END_HEADERS, 1, pl, sizeof pl))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_stream_id_must_increase</b> &mdash; <i>H2 stream id must increase</i></summary>

    * **Objective**: H2 stream id must increase
    * **Assertions**:
      * <code>Assert true (pc_h2_conn_recv(&c, hf, pc_h2_build_headers(hf, sizeof hf, 3, block, blen, PROTO_TRUE)))</code>
      * <code>Assert false (pc_h2_conn_recv(&c, hf, pc_h2_build_headers(hf, sizeof hf, 1, block, blen, PROTO_TRUE)))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_headers_bad_stream_id</b> &mdash; <i>H2 headers bad stream id</i></summary>

    * **Objective**: H2 headers bad stream id
    * **Assertions**:
      * <code>Assert false (pc_h2_conn_recv(&c, hf, pc_h2_build_headers(hf, sizeof hf, 2, block, blen, PROTO_TRUE)))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_stream_table_full_rst</b> &mdash; <i>H2 stream table full rst</i></summary>

    * **Objective**: H2 stream table full rst
    * **Assertions**:
      * <code>Assert true (pc_h2_conn_recv(&c, hf, hn))</code>
      * <code>Assert true (pc_h2_conn_recv(&c, hf, hn))</code>
      * <code>Assert true (count_frames(cap.out, H2_RST_STREAM) &gt;= 1)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_continuation</b> &mdash; <i>H2 continuation</i></summary>

    * **Objective**: H2 continuation
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_HEADERS, 0, 1, block, half))</code>
      * <code>Assert true (feed_frame(c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + half, blen - half))</code>
      * <code>Assert equal int (4, (int)cap.req_headers.size())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_continuation_guards</b> &mdash; <i>H2 continuation guards</i></summary>

    * **Objective**: H2 continuation guards
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_HEADERS, 0, 1, block, blen / 2))</code>
      * <code>Assert false (feed_frame(c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 3, x, 4))</code>
      * <code>Assert true (feed_frame(c, H2_HEADERS, 0, 1, block, blen / 2))</code>
      * <code>Assert false (feed_frame(c, H2_DATA, 0, 1, d, 1))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_data</b> &mdash; <i>Padded DATA: [pad=2][body][2 pad].</i></summary>

    * **Objective**: Padded DATA: [pad=2][body][2 pad].
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_DATA, H2_FLAG_END_STREAM, 1, body, 5))</code>
      * <code>Assert equal string ("hello", cap.body.c_str())</code>
      * <code>Assert true (cap.data_end)</code>
      * <code>Assert equal int (2, count_frames(cap.out, H2_WINDOW_UPDATE))</code>
      * <code>Assert true (feed_frame(c, H2_DATA, H2_FLAG_PADDED, 3, pl.data(), pl.size()))</code>
      * <code>Assert equal string ("xy", cap.body.c_str())</code>
      * <code>Assert false (feed_frame(c2, H2_DATA, 0, 0, d, 1))</code>
      * <code>Assert false (feed_frame(c2, H2_DATA, H2_FLAG_PADDED, 1, bad, 2))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_window_update</b> &mdash; <i>H2 window update</i></summary>

    * **Objective**: H2 window update
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_WINDOW_UPDATE, 0, 0, inc, 4))</code>
      * <code>Assert true (feed_frame(c, H2_WINDOW_UPDATE, 0, 1, inc, 4))</code>
      * <code>Assert false (feed_frame(c, H2_WINDOW_UPDATE, 0, 0, bad, 3))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_rst_priority_push</b> &mdash; <i>H2 rst priority push</i></summary>

    * **Objective**: H2 rst priority push
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_RST_STREAM, 0, 1, err, 4))</code>
      * <code>Assert true (feed_frame(c, H2_PRIORITY, 0, 3, prio, 5))</code>
      * <code>Assert false (feed_frame(c, H2_PUSH_PROMISE, H2_FLAG_END_HEADERS, 1, pp, 4))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_goaway_then_ignore</b> &mdash; <i>H2 goaway then ignore</i></summary>

    * **Objective**: H2 goaway then ignore
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_GOAWAY, 0, 0, ga, 8))</code>
      * <code>Assert true (pc_h2_conn_recv(&c, junk, sizeof junk))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_settings_ack_and_bad</b> &mdash; <i>H2 settings ack and bad</i></summary>

    * **Objective**: H2 settings ack and bad
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_SETTINGS, H2_FLAG_ACK, 0, NULL, 0))</code>
      * <code>Assert false (feed_frame(c, H2_SETTINGS, 0, 0, bad, 3))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_ping_bad</b> &mdash; <i>H2 ping bad</i></summary>

    * **Objective**: H2 ping bad
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_PING, H2_FLAG_ACK, 0, p8, 8))</code>
      * <code>Assert false (feed_frame(c, H2_PING, 0, 0, p4, 4))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_frame_too_big</b> &mdash; <i>H2 frame too big</i></summary>

    * **Objective**: H2 frame too big
    * **Assertions**:
      * <code>Assert false (pc_h2_conn_recv(&c, hh, 9))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_respond_paths_and_goaway</b> &mdash; <i>H2 respond paths and goaway</i></summary>

    * **Objective**: H2 respond paths and goaway
    * **Assertions**:
      * <code>Assert false (pc_h2_conn_respond(&c, 99, 200, "text/plain", "x", 1))</code>
      * <code>Assert true (pc_h2_conn_respond(&c, 1, 200, NULL, "0123456789", 10))</code>
      * <code>Assert true (count_frames(cap.out, H2_DATA) &gt;= 3)</code>
      * <code>Assert equal int (1, count_frames(cap.out, H2_GOAWAY))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_more_guards</b> &mdash; <i>H2 more guards</i></summary>

    * **Objective**: H2 more guards
    * **Assertions**:
      * <code>Assert false (fresh_feed(H2_HEADERS, H2_FLAG_PADDED | H2_FLAG_END_HEADERS, 1, NULL, 0))</code>
      * <code>Assert false (fresh_feed(H2_HEADERS, H2_FLAG_PRIORITY | H2_FLAG_END_HEADERS, 1, p3, 3))</code>
      * <code>Assert false (fresh_feed(H2_HEADERS, H2_FLAG_END_HEADERS, 1, bad_hpack, 4))</code>
      * <code>Assert false (fresh_feed(H2_HEADERS, 0, 1, huge.data(), huge.size()))</code>
      * <code>Assert false (fresh_feed(H2_DATA, H2_FLAG_PADDED, 1, NULL, 0))</code>
      * <code>Assert false (fresh_feed(H2_DATA, H2_FLAG_PADDED, 1, dpad, 2))</code>
      * <code>Assert true (fresh_feed(0x2A, 0, 1, x, 1))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_continuation_more</b> &mdash; <i>H2 continuation more</i></summary>

    * **Objective**: H2 continuation more
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_HEADERS, 0, 1, block, t))</code>
      * <code>Assert true (feed_frame(c, H2_CONTINUATION, 0, 1, block + t, t))</code>
      * <code>Assert true (feed_frame(c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + 2 * t, blen - 2 * t))</code>
      * <code>Assert equal int (4, (int)cap.req_headers.size())</code>
      * <code>Assert true (feed_frame(c, H2_HEADERS, 0, 1, frag.data(), frag.size())); // buffered (&lt; hblock)</code>
      * <code>Assert false (feed_frame(c, H2_CONTINUATION, 0, 1, more.data(), more.size()))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_respond_content_type_too_big</b> &mdash; <i>H2 respond content type too big</i></summary>

    * **Objective**: H2 respond content type too big
    * **Assertions**:
      * <code>Assert false (pc_h2_conn_respond(&c, 1, 200, big_ct.c_str(), "x", 1))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_null_callbacks</b> &mdash; <i>The stream was still opened even though no header callback observed it.</i></summary>

    * **Objective**: The stream was still opened even though no header callback observed it.
    * **Assertions**:
      * <code>Assert true (pc_h2_conn_recv(&c, in.data(), in.size()))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, c.last_peer_stream);</code>
      * <code>Assert true (feed_frame(c, H2_DATA, H2_FLAG_END_STREAM, 1, body, 3))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_headers_stream_zero</b> &mdash; <i>H2 headers stream zero</i></summary>

    * **Objective**: H2 headers stream zero
    * **Assertions**:
      * <code>Assert false (pc_h2_conn_recv(&c, hf, pc_h2_build_headers(hf, sizeof hf, 0, block, blen, PROTO_TRUE)))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_continuation_without_headers</b> &mdash; <i>H2 continuation without headers</i></summary>

    * **Objective**: H2 continuation without headers
    * **Assertions**:
      * <code>Assert false (feed_frame(c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, x, 4))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_unknown_stream_frames</b> &mdash; <i>RST_STREAM on stream 0: find_stream must not match an empty (id == 0) slot.</i></summary>

    * **Objective**: RST_STREAM on stream 0: find_stream must not match an empty (id == 0) slot.
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_RST_STREAM, 0, 0, err, 4))</code>
      * <code>Assert true (feed_frame(c, H2_RST_STREAM, 0, 7, err, 4))</code>
      * <code>Assert true (feed_frame(c, H2_WINDOW_UPDATE, 0, 9, inc, 4))</code>
      * <code>TEST_ASSERT_EQUAL_INT32(before, c.conn_send_window);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_data_empty_and_unknown_stream</b> &mdash; <i>DATA with END_STREAM on an id with no stream slot: delivered, no state to update.</i></summary>

    * **Objective**: DATA with END_STREAM on an id with no stream slot: delivered, no state to update.
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_DATA, 0, 1, NULL, 0))</code>
      * <code>Assert equal int (0, count_frames(cap.out, H2_WINDOW_UPDATE))</code>
      * <code>Assert equal string ("", cap.body.c_str())</code>
      * <code>Assert true (feed_frame(c, H2_DATA, H2_FLAG_END_STREAM, 5, d, 2))</code>
      * <code>Assert equal string ("ok", cap.body.c_str())</code>
      * <code>Assert true (cap.data_end)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_continuation_after_stream_freed</b> &mdash; <i>H2 continuation after stream freed</i></summary>

    * **Objective**: H2 continuation after stream freed
    * **Assertions**:
      * <code>Assert true (feed_frame(c, H2_HEADERS, 0, 1, block, half))</code>
      * <code>Assert true (pc_h2_conn_respond(&c, 1, 200, NULL, "x", 1))</code>
      * <code>Assert true (feed_frame(c, H2_CONTINUATION, H2_FLAG_END_HEADERS, 1, block + half, blen - half))</code>
      * <code>Assert equal int (4, (int)cap.req_headers.size())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_respond_default_chunk_size</b> &mdash; <i>H2 respond default chunk size</i></summary>

    * **Objective**: H2 respond default chunk size
    * **Assertions**:
      * <code>Assert true (pc_h2_conn_respond(&c, 1, 200, NULL, body.data(), body.size()))</code>
      * <code>Assert equal int (1, count_frames(cap.out, H2_DATA))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_h2_respond_content_length_no_room</b> &mdash; <i>'&' has an 8-bit Huffman code, so the value is stored literally and its encoded size is</i></summary>

    * **Objective**: '&' has an 8-bit Huffman code, so the value is stored literally and its encoded size is
    * **Assertions**:
      * <code>Assert false (pc_h2_conn_respond(&c, 1, 200, ct.c_str(), "hi", 2))</code>
      * <code>Assert equal int (0, count_frames(cap.out, H2_HEADERS))</code>
      * <code>Assert true (pc_h2_conn_respond(&c, 1, 200, "text/plain", "hi", 2))</code>
      * <code>Assert equal int (1, count_frames(cap.out, H2_HEADERS))</code>
  </details>

</details>

<details>
<summary><b>test_pentest (78 tests)</b></summary>

  <details style="margin-left: 20px;">
    <summary><b>test_http_oversized_path</b> &mdash; <i>Http oversized path</i></summary>

    * **Objective**: Http oversized path
    * **Assertions**:
      * <code>Assert equal int (PARSE_URI_TOO_LONG, g_req.parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_content_length_too_large</b> &mdash; <i>Http content length too large</i></summary>

    * **Objective**: Http content length too large
    * **Assertions**:
      * <code>Assert equal int (PARSE_ENTITY_TOO_LARGE, g_req.parse_state)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_header_flood</b> &mdash; <i>Http header flood</i></summary>

    * **Objective**: Http header flood
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_malformed_lines</b> &mdash; <i>Http malformed lines</i></summary>

    * **Objective**: Http malformed lines
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_smuggling_vectors</b> &mdash; <i>Duplicate Content-Length.</i></summary>

    * **Objective**: Duplicate Content-Length.
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_header_injection</b> &mdash; <i>Http header injection</i></summary>

    * **Objective**: Http header injection
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_binary_and_tls</b> &mdash; <i>A TLS ClientHello record header fed to the cleartext HTTP parser.</i></summary>

    * **Objective**: A TLS ClientHello record header fed to the cleartext HTTP parser.
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_partial_slowloris</b> &mdash; <i>A long request fed nothing-but-headers, never terminated: must stay bounded</i></summary>

    * **Objective**: A long request fed nothing-but-headers, never terminated: must stay bounded
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_random_fuzz</b> &mdash; <i>Many deterministically-random streams of random length: after each, the</i></summary>

    * **Objective**: Many deterministically-random streams of random length: after each, the
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_http_structured_fuzz</b> &mdash; <i>Http structured fuzz</i></summary>

    * **Objective**: Http structured fuzz
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_multipart_random</b> &mdash; <i>Random body, occasionally seeded with real delimiters so the parser</i></summary>

    * **Objective**: Random body, occasionally seeded with real delimiters so the parser
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_coap_random_datagram</b> &mdash; <i>Coap random datagram</i></summary>

    * **Objective**: Coap random datagram
    * **Assertions**:
      * <code>Assert true (rn &lt;= sizeof(resp))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_coap_structured_fuzz</b> &mdash; <i>Coap structured fuzz</i></summary>

    * **Objective**: Coap structured fuzz
    * **Assertions**:
      * <code>Assert true (rn &lt;= sizeof(resp))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_snmp_random_packet</b> &mdash; <i>Snmp random packet</i></summary>

    * **Objective**: Snmp random packet
    * **Assertions**:
      * <code>Assert true (rn &lt;= sizeof(resp))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_snmp_structured_fuzz</b> &mdash; <i>Snmp structured fuzz</i></summary>

    * **Objective**: Snmp structured fuzz
    * **Assertions**:
      * <code>Assert true (rn &lt;= sizeof(resp))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ber_decoder_random</b> &mdash; <i>Walk the buffer with a mix of readers; the cursor must never pass len.</i></summary>

    * **Objective**: Walk the buffer with a mix of readers; the cursor must never pass len.
    * **Assertions**:
      * <code>Assert true (d.pos &lt;= d.len)</code>
      * <code>Assert true (d.pos &lt;= d.len)</code>
      * <code>Assert true (d.pos &lt;= d.len)</code>
      * <code>Assert true (cnt &lt;= 32)</code>
      * <code>Assert true (d.pos &lt;= d.len)</code>
      * <code>Assert true (d.pos &lt;= d.len)</code>
      * <code>Assert true (d.pos &lt;= d.len)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_modbus_random_adu</b> &mdash; <i>Modbus random adu</i></summary>

    * **Objective**: Modbus random adu
    * **Assertions**:
      * <code>Assert true (rn &lt;= sizeof(resp))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_modbus_master_response_random</b> &mdash; <i>Modbus master response random</i></summary>

    * **Objective**: Modbus master response random
    * **Assertions**:
      * <code>Assert true (got &lt;= 64)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_cbor_reader_random</b> &mdash; <i>Try each typed reader against the head item; whatever happens, the</i></summary>

    * **Objective**: Try each typed reader against the head item; whatever happens, the
    * **Assertions**:
      * <code>Assert true (r.pos &lt;= r.len)</code>
      * <code>Assert true (l &lt;= r.len)</code>
      * <code>Assert true (l &lt;= r.len)</code>
      * <code>Assert true (r.pos &lt;= r.len)</code>
      * <code>Assert true (r.pos &lt;= r.len)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_msgpack_reader_random</b> &mdash; <i>Try each typed reader against the head object; whatever happens, the</i></summary>

    * **Objective**: Try each typed reader against the head object; whatever happens, the
    * **Assertions**:
      * <code>Assert true (r.pos &lt;= r.len)</code>
      * <code>Assert true (l &lt;= r.len)</code>
      * <code>Assert true (l &lt;= r.len)</code>
      * <code>Assert true (r.pos &lt;= r.len)</code>
      * <code>Assert true (r.pos &lt;= r.len)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_json_reader_random</b> &mdash; <i>No assert beyond "does not crash / over-read"; the readers return bool</i></summary>

    * **Objective**: No assert beyond "does not crash / over-read"; the readers return bool
    * **Assertions**:
      * <code>Assert true (sval[sizeof(sval) - 1] == sval[sizeof(sval) - 1]); // touch (sanitizer guard)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_json_reader_malformed</b> &mdash; <i>---------------------------------------------------------------------------</i></summary>

    * **Objective**: ---------------------------------------------------------------------------
    * **Assertions**:
      * <code>Assert true (PROTO_TRUE)</code>
      * <code>Assert true (r &lt;= sizeof(out))</code>
      * <code>Assert true (r &lt;= (int)sizeof(out))</code>
      * <code>Assert true (bh.header_size == 8 || bh.header_size == 12)</code>
      * <code>Assert true ((uint64_t)cell.local_off + cell.local_len &lt;= 512)</code>
      * <code>Assert true (v &gt;= page && v + vl &lt;= page + 64)</code>
      * <code>Assert true (v + vl &lt;= leaf + 512)</code>
      * <code>Assert true (cell.payload_len &lt;= sizeof(out))</code>
      * <code>Assert true (v + vl &lt;= leaf + 512)</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(8, NULL, 0));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(1, pc_sqlite_column_int(9, NULL, 0));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(0, any, 8));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(10, any, 8));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(11, any, 8));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(2, any, 1)); // type 2 needs 2 bytes, only 1 given</code>
      * <code>TEST_ASSERT_EQUAL_INT64(127, pc_sqlite_column_int(1, b1p, 1));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-128, pc_sqlite_column_int(1, b1n, 1));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-32768, pc_sqlite_column_int(2, b2n, 2));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-8388608, pc_sqlite_column_int(3, b3n, 3));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-2147483648LL, pc_sqlite_column_int(4, b4n, 4));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-140737488355328LL, pc_sqlite_column_int(5, b6n, 6));</code>
      * <code>TEST_ASSERT_EQUAL_INT64((int64_t)0x8000000000000000ULL, pc_sqlite_column_int(6, b8n, 8));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(42, pc_sqlite_column_int(6, b8p, 8));</code>
      * <code>Assert true (pc_sqlite_column_float(v, 7) == 0.0)</code>
      * <code>Assert true (rl &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc, rec, rl))</code>
      * <code>Assert true (pc_sqlite_record_next(&rc, &st, &val, &vl))</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(7, st);</code>
      * <code>Assert true (pc_sqlite_column_float(val, vl) == 3.5)</code>
      * <code>TEST_ASSERT_EQUAL_UINT32((uint32_t)expect_len[i], (uint32_t)n);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)dn);</code>
      * <code>Assert true (back == vals[i])</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_sqlite_varint_encode(vals[i], buf, n - 1));</code>
      * <code>Assert true (rl &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc, rec, rl))</code>
      * <code>Assert true (pc_sqlite_record_next(&rc, &st, &val, &vl))</code>
      * <code>TEST_ASSERT_EQUAL_INT64(ints[i], pc_sqlite_column_int(st, val, vl));</code>
      * <code>Assert true (rl2 &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc2, rec2, rl2))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, vl2);</code>
      * <code>Assert equal int (3, cols_seen)</code>
      * <code>Assert true (rl3 &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc3, rec3, rl3))</code>
      * <code>Assert true (pc_sqlite_record_next(&rc3, &st3, &val3, &vl3))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(4, vl3);</code>
      * <code>TEST_ASSERT_EQUAL_UINT8_ARRAY(blobdata, val3, 4);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_encode_record(bcol, 1, tiny, sizeof(tiny)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_encode_record(col, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(0, "t", "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(511, "t", "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(100000, "t", "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(600, "t", "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, NULL, "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", NULL, &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", huge_sql, &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", &brow, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", many, 200, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", &wrow, 1, out, sizeof(out)));</code>
      * <code>Assert false (pc_sqlite_parse_db_header(buf, 99, &dh))</code>
      * <code>Assert false (pc_sqlite_parse_db_header(bad, 100, &dh))</code>
      * <code>Assert true (pc_sqlite_parse_db_header(big, 100, &dh))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(65536u, dh.page_size);</code>
      * <code>Assert false (pc_sqlite_parse_db_header(small, 100, &dh))</code>
      * <code>Assert false (pc_sqlite_parse_db_header(notpow2, 100, &dh))</code>
      * <code>Assert true (pc_sqlite_parse_db_header(ok, 100, &dh))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(512u, dh.page_size);</code>
      * <code>Assert false (pc_sqlite_parse_btree_header(page, 5, 0, &bh))</code>
      * <code>Assert false (pc_sqlite_parse_btree_header(page, sizeof(page), 0, &bh))</code>
      * <code>Assert false (pc_sqlite_parse_btree_header(page, 10, 0, &bh))</code>
      * <code>Assert true (pc_sqlite_parse_btree_header(page, sizeof(page), 0, &bh))</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(12, bh.header_size);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_cell_pointer(page, sizeof(page), &bh, 0, 2));</code>
      * <code>Assert false (pc_sqlite_parse_table_leaf_cell(page, 32, 512, 0, 31, &cell))</code>
      * <code>Assert false (pc_sqlite_parse_table_leaf_cell(page, 32, 512, 0, 31, &cell))</code>
      * <code>Assert false (pc_sqlite_read_payload(garbage_page, NULL, 512, 0, leaf, &cell, out, sizeof(out), work))</code>
      * <code>Assert false (pc_sqlite_read_payload(garbage_page, NULL, 512, 0, leaf, &cell, out, sizeof(out), work))</code>
      * <code>Assert false (pc_sqlite_read_payload(garbage_page, NULL, 10, 6, leaf, &cell, out, sizeof(out), work))</code>
      * <code>TEST_ASSERT_FALSE(</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 2))</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[4], 512, leaf2_rows, 2))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, multilevel_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert equal int (4, n)</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(1, seen[0]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(2, seen[1]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(3, seen[2]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(4, seen[3]);</code>
      * <code>Assert false (pc_sqlite_table_cursor_begin(&c, chain_interior_page, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 1))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, reread_fail_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, ovf_reader, NULL, 512, 0, 5, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(1, rid);</code>
      * <code>Assert true (v &gt;= ovf_buf && v + vl &lt;= ovf_buf + sizeof(ovf_buf))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, ovf_reader, NULL, 512, 0, 5, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(1, rid);</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, ovf_reader, NULL, 512, 0, 5, leaf, work))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_begin(&c, always_fail_reader, NULL, 512, 0, 1, leaf, work))</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 1))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, multilevel_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 1))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, multilevel_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc)); // cursor_descend(4)</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 1))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, reread_garbage_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert true (rl &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc, out, rl))</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(9, st);</code>
      * <code>Assert equal int (150, seen)</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(131072u, n);</code>
      * <code>Assert true (pc_sqlite_parse_db_header(out, 100, &dh))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(65536u, dh.page_size);</code>
      * <code>Assert true (r-&gt;str &gt;= (const char *)buf)</code>
      * <code>Assert true (r-&gt;str + r-&gt;str_len &lt;= (const char *)buf + len)</code>
      * <code>Assert true (consumed &lt;= len)</code>
      * <code>Assert true (consumed &lt;= len)</code>
      * <code>Assert true (c2 &lt;= cut)</code>
      * <code>Assert true (rd.count &lt;= PC_OPCUA_READ_MAX)</code>
      * <code>Assert true (wq.count &lt;= PC_OPCUA_WRITE_MAX)</code>
      * <code>Assert true (br.count &lt;= PC_OPCUA_BROWSE_MAX)</code>
      * <code>Assert true (end &gt;= vec[k])</code>
      * <code>Assert true (n &lt;= ocap)</code>
      * <code>Assert true (ulen &lt;= sizeof(user))</code>
      * <code>Assert true (consumed &lt;= len)</code>
      * <code>Assert true (f.header_count &lt;= (size_t)PC_STOMP_MAX_HEADERS)</code>
      * <code>Assert true (slice_in(f.command, f.command_len, buf, len))</code>
      * <code>Assert true (slice_in(f.body, f.body_len, buf, len))</code>
      * <code>Assert true (slice_in(f.headers[h].key, f.headers[h].key_len, buf, len))</code>
      * <code>Assert true (slice_in(f.headers[h].val, f.headers[h].val_len, buf, len))</code>
      * <code>Assert true (slice_in((const char *)neg.sec_buf, neg.sec_buf_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)ss.sec_buf, ss.sec_buf_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)rd.data, rd.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)tok, tl, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)ch.target_info, ch.target_info_len, (const char *)buf, n))</code>
      * <code>Assert true (d.len &lt;= PC_DNC_LINE_MAX)</code>
      * <code>Assert true (consumed &lt;= n)</code>
      * <code>Assert true (slice_in((const char *)fr.data, fr.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)mr.data, mr.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)cr.data, cr.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)cip, pc_cip_len, (const char *)buf, n))</code>
      * <code>Assert true (out_len &lt;= sizeof(out))</code>
      * <code>Assert true (slice_in((const char *)npdu, pc_npdu_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)ni.apdu, ni.apdu_len, (const char *)buf, n))</code>
      * <code>Assert true (consumed &lt;= n)</code>
      * <code>Assert true (slice_in((const char *)payload, payload_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)co.data, co.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)cf.data, cf.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)fc.params, fc.params_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)dn, dn_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)cc.payload, cc.payload_len, (const char *)buf, n))</code>
      * <code>Assert true (consumed &lt;= n)</code>
      * <code>Assert true (slice_in((const char *)af.payload, af.payload_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)mp.service_body, mp.service_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)body, body_len, (const char *)c-&gt;buf, c-&gt;n))</code>
      * <code>Assert true (name_len + value_len &lt;= sizeof(g_hpack_scratch))</code>
      * <code>Assert true (t-&gt;used &lt;= t-&gt;max_size)</code>
      * <code>Assert true (t-&gt;ecount &lt;= PC_HPACK_MAX_ENTRIES)</code>
      * <code>Assert true (t-&gt;rused &lt;= PC_HPACK_TABLE_BYTES)</code>
      * <code>Assert true (name_len + value_len &lt;= sizeof(g_qpack_scratch))</code>
      * <code>Assert true (ret &lt;= cap)</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(0xAA, buf[i]); // never wrote past the cap</code>
      * <code>Assert true (terminated)</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)out[i]); // never wrote past the cap (pass or fail)</code>
      * <code>Assert true (term)</code>
      * <code>Assert true (o &lt; ecap)</code>
      * <code>TEST_ASSERT_EQUAL_UINT8('\\0', esc[o]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)esc[i]);</code>
      * <code>Assert true (len &lt;= dcap)</code>
      * <code>Assert true (len &gt;= prev)</code>
      * <code>Assert true (len &lt;= dcap)</code>
      * <code>Assert true (len &lt;= dcap)</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)doc[i]);</code>
      * <code>Assert true (m &lt;= DAV_M_UNSUPPORTED)</code>
      * <code>Assert true (d == 0 || d == 1 || d == PC_DAV_DEPTH_INFINITY || d == -7)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_base64_random</b> &mdash; <i>Base64 random</i></summary>

    * **Objective**: Base64 random
    * **Assertions**:
      * <code>Assert true (r &lt;= sizeof(out))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_base32_random</b> &mdash; <i>Base32 random</i></summary>

    * **Objective**: Base32 random
    * **Assertions**:
      * <code>Assert true (r &lt;= (int)sizeof(out))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_random_page</b> &mdash; <i>Record cursor over random bytes: every column value stays inside the record.</i></summary>

    * **Objective**: Record cursor over random bytes: every column value stays inside the record.
    * **Assertions**:
      * <code>Assert true (bh.header_size == 8 || bh.header_size == 12)</code>
      * <code>Assert true ((uint64_t)cell.local_off + cell.local_len &lt;= 512)</code>
      * <code>Assert true (v &gt;= page && v + vl &lt;= page + 64)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_hostile_table_cursor</b> &mdash; <i>Walk a table b-tree of pure garbage from many random rootpages. The bounded descent stack + per-page</i></summary>

    * **Objective**: Walk a table b-tree of pure garbage from many random rootpages. The bounded descent stack + per-page
    * **Assertions**:
      * <code>Assert true (v + vl &lt;= leaf + 512)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_hostile_overflow</b> &mdash; <i>pc_sqlite_read_payload following a garbage overflow chain (cyclic / dangling next-pointers) must bound</i></summary>

    * **Objective**: pc_sqlite_read_payload following a garbage overflow chain (cyclic / dangling next-pointers) must bound
    * **Assertions**:
      * <code>Assert true (cell.payload_len &lt;= sizeof(out))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_structured_mutation</b> &mdash; <i>Build a VALID two-page database, corrupt random bytes, then re-read the whole thing. A single-bit or</i></summary>

    * **Objective**: Build a VALID two-page database, corrupt random bytes, then re-read the whole thing. A single-bit or
    * **Assertions**:
      * <code>Assert true (v + vl &lt;= leaf + 512)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_column_int_edges</b> &mdash; <i>The two 0-byte constants never touch `val`.</i></summary>

    * **Objective**: The two 0-byte constants never touch `val`.
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(8, NULL, 0));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(1, pc_sqlite_column_int(9, NULL, 0));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(0, any, 8));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(10, any, 8));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(11, any, 8));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(0, pc_sqlite_column_int(2, any, 1)); // type 2 needs 2 bytes, only 1 given</code>
      * <code>TEST_ASSERT_EQUAL_INT64(127, pc_sqlite_column_int(1, b1p, 1));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-128, pc_sqlite_column_int(1, b1n, 1));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-32768, pc_sqlite_column_int(2, b2n, 2));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-8388608, pc_sqlite_column_int(3, b3n, 3));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-2147483648LL, pc_sqlite_column_int(4, b4n, 4));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(-140737488355328LL, pc_sqlite_column_int(5, b6n, 6));</code>
      * <code>TEST_ASSERT_EQUAL_INT64((int64_t)0x8000000000000000ULL, pc_sqlite_column_int(6, b8n, 8));</code>
      * <code>TEST_ASSERT_EQUAL_INT64(42, pc_sqlite_column_int(6, b8p, 8));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_column_float_edges</b> &mdash; <i>Round-trip through the record writer/reader so the exact big-endian bit layout is exercised.</i></summary>

    * **Objective**: Round-trip through the record writer/reader so the exact big-endian bit layout is exercised.
    * **Assertions**:
      * <code>Assert true (pc_sqlite_column_float(v, 7) == 0.0)</code>
      * <code>Assert true (rl &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc, rec, rl))</code>
      * <code>Assert true (pc_sqlite_record_next(&rc, &st, &val, &vl))</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(7, st);</code>
      * <code>Assert true (pc_sqlite_column_float(val, vl) == 3.5)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_varint_encode_boundaries</b> &mdash; <i>One value at each of the encoder's 9 length thresholds (the largest that still fits N bytes),</i></summary>

    * **Objective**: One value at each of the encoder's 9 length thresholds (the largest that still fits N bytes),
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT32((uint32_t)expect_len[i], (uint32_t)n);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32((uint32_t)n, (uint32_t)dn);</code>
      * <code>Assert true (back == vals[i])</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_sqlite_varint_encode(vals[i], buf, n - 1));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_encode_record_value_types</b> &mdash; <i>The full integer-width ladder, including the values that straddle each range boundary (so both</i></summary>

    * **Objective**: The full integer-width ladder, including the values that straddle each range boundary (so both
    * **Assertions**:
      * <code>Assert true (rl &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc, rec, rl))</code>
      * <code>Assert true (pc_sqlite_record_next(&rc, &st, &val, &vl))</code>
      * <code>TEST_ASSERT_EQUAL_INT64(ints[i], pc_sqlite_column_int(st, val, vl));</code>
      * <code>Assert true (rl2 &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc2, rec2, rl2))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, vl2);</code>
      * <code>Assert equal int (3, cols_seen)</code>
      * <code>Assert true (rl3 &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc3, rec3, rl3))</code>
      * <code>Assert true (pc_sqlite_record_next(&rc3, &st3, &val3, &vl3))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(4, vl3);</code>
      * <code>TEST_ASSERT_EQUAL_UINT8_ARRAY(blobdata, val3, 4);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_encode_record(bcol, 1, tiny, sizeof(tiny)));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_encode_record_overflow_guard</b> &mdash; <i>A single BLOB whose declared length is engineered so record_len's internal uint32_t sum</i></summary>

    * **Objective**: A single BLOB whose declared length is engineered so record_len's internal uint32_t sum
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_encode_record(col, 1, out, sizeof(out)));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_build_table_db_invalid_params</b> &mdash; <i>page_size out of range / not a power of two.</i></summary>

    * **Objective**: page_size out of range / not a power of two.
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(0, "t", "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(511, "t", "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(100000, "t", "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(600, "t", "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, NULL, "CREATE TABLE t(a)", &row, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", NULL, &row, 1, out, sizeof(out)));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_build_table_db_page1_overflow</b> &mdash; <i>The pc_sqlite_schema row (type/name/tbl_name/rootpage + this huge sql text) no longer fits page</i></summary>

    * **Objective**: The pc_sqlite_schema row (type/name/tbl_name/rootpage + this huge sql text) no longer fits page
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", huge_sql, &row, 1, out, sizeof(out)));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_build_table_db_page2_failures</b> &mdash; <i>(a) A single row whose record is real (not a length-overflow artifact) but bigger than one leaf</i></summary>

    * **Objective**: (a) A single row whose record is real (not a length-overflow artifact) but bigger than one leaf
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", &brow, 1, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", many, 200, out, sizeof(out)));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_build_table_db(512, "t", "CREATE TABLE t(a)", &wrow, 1, out, sizeof(out)));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_parse_db_header_edges</b> &mdash; <i>Too short for the fixed 100-byte header.</i></summary>

    * **Objective**: Too short for the fixed 100-byte header.
    * **Assertions**:
      * <code>Assert false (pc_sqlite_parse_db_header(buf, 99, &dh))</code>
      * <code>Assert false (pc_sqlite_parse_db_header(bad, 100, &dh))</code>
      * <code>Assert true (pc_sqlite_parse_db_header(big, 100, &dh))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(65536u, dh.page_size);</code>
      * <code>Assert false (pc_sqlite_parse_db_header(small, 100, &dh))</code>
      * <code>Assert false (pc_sqlite_parse_db_header(notpow2, 100, &dh))</code>
      * <code>Assert true (pc_sqlite_parse_db_header(ok, 100, &dh))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(512u, dh.page_size);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_btree_header_edges</b> &mdash; <i>offset + 8 > page_len (a page_len too small even for a leaf header).</i></summary>

    * **Objective**: offset + 8 > page_len (a page_len too small even for a leaf header).
    * **Assertions**:
      * <code>Assert false (pc_sqlite_parse_btree_header(page, 5, 0, &bh))</code>
      * <code>Assert false (pc_sqlite_parse_btree_header(page, sizeof(page), 0, &bh))</code>
      * <code>Assert false (pc_sqlite_parse_btree_header(page, 10, 0, &bh))</code>
      * <code>Assert true (pc_sqlite_parse_btree_header(page, sizeof(page), 0, &bh))</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(12, bh.header_size);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cell_pointer_edges</b> &mdash; <i>i >= cell_count -> 0.</i></summary>

    * **Objective**: i >= cell_count -> 0.
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT32(0, pc_sqlite_cell_pointer(page, sizeof(page), &bh, 0, 2));</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_leaf_cell_truncated_varints</b> &mdash; <i>First varint (payload_len) has its continuation bit set with no byte left to terminate it.</i></summary>

    * **Objective**: First varint (payload_len) has its continuation bit set with no byte left to terminate it.
    * **Assertions**:
      * <code>Assert false (pc_sqlite_parse_table_leaf_cell(page, 32, 512, 0, 31, &cell))</code>
      * <code>Assert false (pc_sqlite_parse_table_leaf_cell(page, 32, 512, 0, 31, &cell))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_read_payload_direct_edges</b> &mdash; <i>(a) The claimed local extent runs past the page.</i></summary>

    * **Objective**: (a) The claimed local extent runs past the page.
    * **Assertions**:
      * <code>Assert false (pc_sqlite_read_payload(garbage_page, NULL, 512, 0, leaf, &cell, out, sizeof(out), work))</code>
      * <code>Assert false (pc_sqlite_read_payload(garbage_page, NULL, 512, 0, leaf, &cell, out, sizeof(out), work))</code>
      * <code>Assert false (pc_sqlite_read_payload(garbage_page, NULL, 10, 6, leaf, &cell, out, sizeof(out), work))</code>
      * <code>TEST_ASSERT_FALSE(</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cursor_multilevel_tree</b> &mdash; <i>Sqlite cursor multilevel tree</i></summary>

    * **Objective**: Sqlite cursor multilevel tree
    * **Assertions**:
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 2))</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[4], 512, leaf2_rows, 2))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, multilevel_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert equal int (4, n)</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(1, seen[0]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(2, seen[1]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(3, seen[2]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(4, seen[3]);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cursor_max_depth</b> &mdash; <i>An interior chain that never bottoms out in a leaf must fail closed at the bounded descent</i></summary>

    * **Objective**: An interior chain that never bottoms out in a leaf must fail closed at the bounded descent
    * **Assertions**:
      * <code>Assert false (pc_sqlite_table_cursor_begin(&c, chain_interior_page, NULL, 512, 0, 2, leaf, work))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cursor_reread_failure</b> &mdash; <i>Leaf 3 is now exhausted; advancing must re-read the interior root (page 2) to reach its</i></summary>

    * **Objective**: Leaf 3 is now exhausted; advancing must re-read the interior root (page 2) to reach its
    * **Assertions**:
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 1))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, reread_fail_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cursor_overflow_reassembly</b> &mdash; <i>A 600-byte record payload (a valid 1-byte record header declaring zero columns, then filler),</i></summary>

    * **Objective**: A 600-byte record payload (a valid 1-byte record header declaring zero columns, then filler),
    * **Assertions**:
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, ovf_reader, NULL, 512, 0, 5, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(1, rid);</code>
      * <code>Assert true (v &gt;= ovf_buf && v + vl &lt;= ovf_buf + sizeof(ovf_buf))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cursor_overflow_without_buf</b> &mdash; <i>Same overflowing-cell layout as test_sqlite_cursor_overflow_reassembly, but this time no</i></summary>

    * **Objective**: Same overflowing-cell layout as test_sqlite_cursor_overflow_reassembly, but this time no
    * **Assertions**:
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, ovf_reader, NULL, 512, 0, 5, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(1, rid);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cursor_overflow_reassembly_failure</b> &mdash; <i>Same layout as test_sqlite_cursor_overflow_reassembly (a 600-byte payload, 92 bytes local + a</i></summary>

    * **Objective**: Same layout as test_sqlite_cursor_overflow_reassembly (a 600-byte payload, 92 bytes local + a
    * **Assertions**:
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, ovf_reader, NULL, 512, 0, 5, leaf, work))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cursor_begin_read_failure</b> &mdash; <i>The very first page read (the root, during descent) fails - table_cursor_begin must fail closed.</i></summary>

    * **Objective**: The very first page read (the root, during descent) fails - table_cursor_begin must fail closed.
    * **Assertions**:
      * <code>Assert false (pc_sqlite_table_cursor_begin(&c, always_fail_reader, NULL, 512, 0, 1, leaf, work))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_cursor_stack_pop_edges</b> &mdash; <i>(1) The interior root's right-most pointer is 0 ("no child") - the stack-pop advance must fail</i></summary>

    * **Objective**: (1) The interior root's right-most pointer is 0 ("no child") - the stack-pop advance must fail
    * **Assertions**:
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 1))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, multilevel_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 1))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, multilevel_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc)); // cursor_descend(4)</code>
      * <code>Assert true (make_leaf_page(g_ml_pages[3], 512, leaf1_rows, 1))</code>
      * <code>Assert true (pc_sqlite_table_cursor_begin(&c, reread_garbage_reader, NULL, 512, 0, 2, leaf, work))</code>
      * <code>Assert true (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
      * <code>Assert false (pc_sqlite_table_cursor_next(&c, &rid, &rc))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_record_header_size_two_byte_fixed_point</b> &mdash; <i>Enough columns that the record header's own length varint needs 2 bytes, forcing record_len's</i></summary>

    * **Objective**: Enough columns that the record header's own length varint needs 2 bytes, forcing record_len's
    * **Assertions**:
      * <code>Assert true (rl &gt; 0)</code>
      * <code>Assert true (pc_sqlite_record_begin(&rc, out, rl))</code>
      * <code>TEST_ASSERT_EQUAL_UINT64(9, st);</code>
      * <code>Assert equal int (150, seen)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_sqlite_build_table_db_max_pagesize_empty</b> &mdash; <i>page_size == 65536 (the on-disk special-case raw value 1) with zero rows: exercises that</i></summary>

    * **Objective**: page_size == 65536 (the on-disk special-case raw value 1) with zero rows: exercises that
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT32(131072u, n);</code>
      * <code>Assert true (pc_sqlite_parse_db_header(out, 100, &dh))</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(65536u, dh.page_size);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_resp_random</b> &mdash; <i>Resp random</i></summary>

    * **Objective**: Resp random
    * **Assertions**:
      * <code>Assert true (consumed &lt;= len)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_resp_hostile_lengths</b> &mdash; <i>Crafted replies whose length prefixes lie: the decoder must fail (need-more-data) or bound the</i></summary>

    * **Objective**: Crafted replies whose length prefixes lie: the decoder must fail (need-more-data) or bound the
    * **Assertions**:
      * <code>Assert true (consumed &lt;= len)</code>
      * <code>Assert true (c2 &lt;= cut)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_opcua_random</b> &mdash; <i>Half the time, stamp a well-formed UACP header (valid type + size == len) so the body</i></summary>

    * **Objective**: Half the time, stamp a well-formed UACP header (valid type + size == len) so the body
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_opcua_hostile_open</b> &mdash; <i>An OPN whose SecurityPolicyUri length lies: the reader must fail-closed, never r_skip past</i></summary>

    * **Objective**: An OPN whose SecurityPolicyUri length lies: the reader must fail-closed, never r_skip past
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_opcua_hostile_header</b> &mdash; <i>Opcua hostile header</i></summary>

    * **Objective**: Opcua hostile header
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_numparse_hostile</b> &mdash; <i>Numparse hostile</i></summary>

    * **Objective**: Numparse hostile
    * **Assertions**:
      * <code>Assert true (end &gt;= vec[k])</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_graphql_hostile</b> &mdash; <i>Graphql hostile</i></summary>

    * **Objective**: Graphql hostile
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_dns_query_random</b> &mdash; <i>Dns query random</i></summary>

    * **Objective**: Dns query random
    * **Assertions**:
      * <code>Assert true (n &lt;= ocap)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_dnp3_frame_random</b> &mdash; <i>Dnp3 frame random</i></summary>

    * **Objective**: Dnp3 frame random
    * **Assertions**:
      * <code>Assert true (ulen &lt;= sizeof(user))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_stomp_frame_random</b> &mdash; <i>Stomp frame random</i></summary>

    * **Objective**: Stomp frame random
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_smb2_response_fuzz</b> &mdash; <i>half the time seed a valid SMB2 sync header so the per-command body parsers are reached</i></summary>

    * **Objective**: half the time seed a valid SMB2 sync header so the per-command body parsers are reached
    * **Assertions**:
      * <code>Assert true (slice_in((const char *)neg.sec_buf, neg.sec_buf_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)ss.sec_buf, ss.sec_buf_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)rd.data, rd.data_len, (const char *)buf, n))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_spnego_ntlmssp_fuzz</b> &mdash; <i>Spnego ntlmssp fuzz</i></summary>

    * **Objective**: Spnego ntlmssp fuzz
    * **Assertions**:
      * <code>Assert true (slice_in((const char *)tok, tl, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)ch.target_info, ch.target_info_len, (const char *)buf, n))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_dnc_decoder_fuzz</b> &mdash; <i>Dnc decoder fuzz</i></summary>

    * **Objective**: Dnc decoder fuzz
    * **Assertions**:
      * <code>Assert true (d.len &lt;= PC_DNC_LINE_MAX)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ftp_reply_fuzz</b> &mdash; <i>Ftp reply fuzz</i></summary>

    * **Objective**: Ftp reply fuzz
    * **Assertions**:
      * <code>Assert true (consumed &lt;= n)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_industrial_response_fuzz</b> &mdash; <i>response parsers that return a payload slice: it must point inside the frame</i></summary>

    * **Objective**: response parsers that return a payload slice: it must point inside the frame
    * **Assertions**:
      * <code>Assert true (slice_in((const char *)fr.data, fr.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)mr.data, mr.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)cr.data, cr.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)cip, pc_cip_len, (const char *)buf, n))</code>
      * <code>Assert true (out_len &lt;= sizeof(out))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_bacnet_cotp_fuzz</b> &mdash; <i>Bacnet cotp fuzz</i></summary>

    * **Objective**: Bacnet cotp fuzz
    * **Assertions**:
      * <code>Assert true (slice_in((const char *)npdu, pc_npdu_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)ni.apdu, ni.apdu_len, (const char *)buf, n))</code>
      * <code>Assert true (consumed &lt;= n)</code>
      * <code>Assert true (slice_in((const char *)payload, payload_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)co.data, co.data_len, (const char *)buf, n))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_c37118_fins_command_fuzz</b> &mdash; <i>C37118 fins command fuzz</i></summary>

    * **Objective**: C37118 fins command fuzz
    * **Assertions**:
      * <code>Assert true (slice_in((const char *)cf.data, cf.data_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)fc.params, fc.params_len, (const char *)buf, n))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_jwt_fuzz</b> &mdash; <i>Jwt fuzz</i></summary>

    * **Objective**: Jwt fuzz
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_fieldbus_amqp_mms_fuzz</b> &mdash; <i>Fieldbus amqp mms fuzz</i></summary>

    * **Objective**: Fieldbus amqp mms fuzz
    * **Assertions**:
      * <code>Assert true (slice_in((const char *)dn, dn_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)cc.payload, cc.payload_len, (const char *)buf, n))</code>
      * <code>Assert true (consumed &lt;= n)</code>
      * <code>Assert true (slice_in((const char *)af.payload, af.payload_len, (const char *)buf, n))</code>
      * <code>Assert true (slice_in((const char *)mp.service_body, mp.service_len, (const char *)buf, n))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_dds_rtps_fuzz</b> &mdash; <i>Dds rtps fuzz</i></summary>

    * **Objective**: Dds rtps fuzz
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_webdav_proppatch_fuzz</b> &mdash; <i>Vary the cap across the fail-closed (tiny) and comfortable ranges.</i></summary>

    * **Objective**: Vary the cap across the fail-closed (tiny) and comfortable ranges.
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_webdav_dest_path_fuzz</b> &mdash; <i>Webdav dest path fuzz</i></summary>

    * **Objective**: Webdav dest path fuzz
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)out[i]); // never wrote past the cap (pass or fail)</code>
      * <code>Assert true (term)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_webdav_builder_fuzz</b> &mdash; <i>xml-escape: a random source into a capped destination is always NUL-terminated</i></summary>

    * **Objective**: xml-escape: a random source into a capped destination is always NUL-terminated
    * **Assertions**:
      * <code>Assert true (o &lt; ecap)</code>
      * <code>TEST_ASSERT_EQUAL_UINT8('\\0', esc[o]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)esc[i]);</code>
      * <code>Assert true (len &lt;= dcap)</code>
      * <code>Assert true (len &gt;= prev)</code>
      * <code>Assert true (len &lt;= dcap)</code>
      * <code>Assert true (len &lt;= dcap)</code>
      * <code>TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)doc[i]);</code>
      * <code>Assert true (m &lt;= DAV_M_UNSUPPORTED)</code>
      * <code>Assert true (d == 0 || d == 1 || d == PC_DAV_DEPTH_INFINITY || d == -7)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_hpack_random_block</b> &mdash; <i>A random block must decode (or fail) without crashing / over-reading; the table stays bounded.</i></summary>

    * **Objective**: A random block must decode (or fail) without crashing / over-reading; the table stays bounded.
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_hpack_structured_fuzz</b> &mdash; <i>Hpack structured fuzz</i></summary>

    * **Objective**: Hpack structured fuzz
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_qpack_random_block</b> &mdash; <i>A random block must decode (or reject) without crashing / over-reading past len.</i></summary>

    * **Objective**: A random block must decode (or reject) without crashing / over-reading past len.
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_qpack_structured_fuzz</b> &mdash; <i>Qpack structured fuzz</i></summary>

    * **Objective**: Qpack structured fuzz
  </details>

</details>

<details>
<summary><b>test_qpack (12 tests)</b></summary>

  <details style="margin-left: 20px;">
    <summary><b>test_qpack_field_int_truncation</b> &mdash; <i>Indexed Field Line (T=1 static), prefix-6 integer 63 (all-ones) with no continuation byte:</i></summary>

    * **Objective**: Indexed Field Line (T=1 static), prefix-6 integer 63 (all-ones) with no continuation byte:
    * **Assertions**:
      * <code>Assert false (decode_all(idx_trunc, 3, &s))</code>
      * <code>Assert false (decode_all(nameref_trunc, 3, &s))</code>
      * <code>Assert false (decode_all(nameref_badidx, 4, &s))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_appendix_b1_decode</b> &mdash; <i>Appendix b1 decode</i></summary>

    * **Objective**: Appendix b1 decode
    * **Assertions**:
      * <code>Assert true (decode_all(block, sizeof block, &s))</code>
      * <code>Assert equal uint (1, (unsigned)s.hdrs.size())</code>
      * <code>Assert equal string (":path", s.hdrs[0].first.c_str())</code>
      * <code>Assert equal string ("/index.html", s.hdrs[0].second.c_str())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_encode_indexed</b> &mdash; <i>Encode indexed</i></summary>

    * **Objective**: Encode indexed
    * **Assertions**:
      * <code>Assert equal int (1, (int)n)</code>
      * <code>TEST_ASSERT_EQUAL_HEX8(0xD9, out[0]);</code>
      * <code>Assert true (decode_all(block, 3, &s))</code>
      * <code>Assert equal string (":status", s.hdrs[0].first.c_str())</code>
      * <code>Assert equal string ("200", s.hdrs[0].second.c_str())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_encode_nameref_roundtrip</b> &mdash; <i>Encode nameref roundtrip</i></summary>

    * **Objective**: Encode nameref roundtrip
    * **Assertions**:
      * <code>Assert true (n &gt; 1)</code>
      * <code>TEST_ASSERT_EQUAL_HEX8(0x51, out[0]); // 01 N=0 T=1 index=1</code>
      * <code>Assert true (decode_all(block, n + 2, &s))</code>
      * <code>Assert equal string (":path", s.hdrs[0].first.c_str())</code>
      * <code>Assert equal string ("/index.html", s.hdrs[0].second.c_str())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_literal_name</b> &mdash; <i>Hand-built raw literal name (H=0, name len 6) + raw value "hi" (H=0, len 2).</i></summary>

    * **Objective**: Hand-built raw literal name (H=0, name len 6) + raw value "hi" (H=0, len 2).
    * **Assertions**:
      * <code>Assert true (n &gt; 0)</code>
      * <code>TEST_ASSERT_EQUAL_HEX8(0x20, out[0] & 0xE0); // 001 pattern</code>
      * <code>Assert true (decode_all(block, n + 2, &s))</code>
      * <code>Assert equal string ("x-test", s.hdrs[0].first.c_str())</code>
      * <code>Assert equal string ("hi", s.hdrs[0].second.c_str())</code>
      * <code>Assert true (decode_all(raw, sizeof raw, &s2))</code>
      * <code>Assert equal string ("x-test", s2.hdrs[0].first.c_str())</code>
      * <code>Assert equal string ("hi", s2.hdrs[0].second.c_str())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_full_section</b> &mdash; <i>Full section</i></summary>

    * **Objective**: Full section
    * **Assertions**:
      * <code>Assert equal int (2, (int)o)</code>
      * <code>TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);</code>
      * <code>TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);</code>
      * <code>Assert true (decode_all(out, o, &s))</code>
      * <code>Assert equal uint (3, (unsigned)s.hdrs.size())</code>
      * <code>Assert equal string (":status", s.hdrs[0].first.c_str())</code>
      * <code>Assert equal string ("200", s.hdrs[0].second.c_str())</code>
      * <code>Assert equal string ("content-type", s.hdrs[1].first.c_str())</code>
      * <code>Assert equal string ("x/y", s.hdrs[1].second.c_str())</code>
      * <code>Assert equal string ("x-test", s.hdrs[2].first.c_str())</code>
      * <code>Assert equal string ("hello", s.hdrs[2].second.c_str())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_reject_dynamic</b> &mdash; <i>Reject dynamic</i></summary>

    * **Objective**: Reject dynamic
    * **Assertions**:
      * <code>Assert false (decode_all(ric_nonzero, 2, &s))</code>
      * <code>Assert false (decode_all(indexed_dyn, 3, &s))</code>
      * <code>Assert false (decode_all(nameref_dyn, 4, &s))</code>
      * <code>Assert false (decode_all(postbase, 3, &s))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_encode_edges</b> &mdash; <i>A name that Huffman-compresses (8x 'a' = 40 bits = 5 bytes < 8) takes the H-bit path.</i></summary>

    * **Objective**: A name that Huffman-compresses (8x 'a' = 40 bits = 5 bytes < 8) takes the H-bit path.
    * **Assertions**:
      * <code>Assert equal int (0, (int)pc_qpack_encode_prefix(out, 1))</code>
      * <code>Assert equal int (0, (int)pc_qpack_encode_header(out, 0, ":status", 7, "200", 3))</code>
      * <code>Assert equal int (0, (int)pc_qpack_encode_header(out, 1, ":path", 5, "/x", 2))</code>
      * <code>Assert equal int (0, (int)pc_qpack_encode_header(out, 1, "zzzz", 4, "v", 1))</code>
      * <code>Assert true (n &gt; 0)</code>
      * <code>Assert true ((out[0] & 0xE0) == 0x20 && (out[0] & 0x08))</code>
      * <code>Assert true (decode_all(blk, n + 2, &s))</code>
      * <code>Assert equal string ("aaaaaaaa", s.hdrs[0].first.c_str())</code>
      * <code>Assert equal string ("v", s.hdrs[0].second.c_str())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_decode_errors</b> &mdash; <i>A decoded name that does not fit the caller's scratch is rejected.</i></summary>

    * **Objective**: A decoded name that does not fit the caller's scratch is rejected.
    * **Assertions**:
      * <code>Assert false (decode_all(novalue, 3, &s))</code>
      * <code>Assert false (decode_all(badidx, 4, &s))</code>
      * <code>Assert false (pc_qpack_decode(nameref, 5, tiny, sizeof tiny, sink_emit, &s))</code>
      * <code>Assert false (pc_qpack_decode(litname, 8, tiny2, sizeof tiny2, sink_emit, &s))</code>
      * <code>Assert false (decode_all(prefix_only, 1, &s))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_value_string_paths</b> &mdash; <i>Value marked Huffman (0x81 = H, len 1) but 0xFF is not a valid single-byte code.</i></summary>

    * **Objective**: Value marked Huffman (0x81 = H, len 1) but 0xFF is not a valid single-byte code.
    * **Assertions**:
      * <code>Assert false (decode_all(bad_huff, 5, &s))</code>
      * <code>Assert false (decode_all(val_trunc, 5, &s))</code>
      * <code>Assert false (pc_qpack_decode(val_over, 9, scratch, sizeof scratch, sink_emit, &s))</code>
      * <code>Assert true (decode_all(blk, n + 2, &s))</code>
      * <code>Assert equal string ("aaaaaaaa", s.hdrs[s.hdrs.size() - 1].second.c_str())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_qpack_more_encode_decode_paths</b> &mdash; <i>A short literal name that does not Huffman-compress takes the raw memcpy path.</i></summary>

    * **Objective**: A short literal name that does not Huffman-compress takes the raw memcpy path.
    * **Assertions**:
      * <code>Assert true (n &gt; 0)</code>
      * <code>Assert true ((out[0] & 0xE0) == 0x20 && !(out[0] & 0x08))</code>
      * <code>Assert true (pc_qpack_encode_header(out, sizeof out, ":path", 5, "aaaaaaaa", 8) &gt; 0)</code>
      * <code>Assert equal int (0, (int)pc_qpack_encode_header(out, 0, ":path", 5, "/x", 2))</code>
      * <code>Assert equal int (0, (int)pc_qpack_encode_header(out, 0, "zzzz", 4, "v", 1))</code>
      * <code>Assert false (decode_all(bad_ric, 1, &s))</code>
      * <code>Assert false (decode_all(idx_dyn, 3, &s))</code>
      * <code>Assert false (decode_all(nameref_dyn, 3, &s))</code>
      * <code>Assert false (decode_all(litname_trunc, 3, &s))</code>
      * <code>Assert false (decode_all(litname_badhuff, 5, &s))</code>
      * <code>Assert false (decode_all(litname_novalue, 4, &s))</code>
      * <code>Assert false (decode_all(nameref_badvlen, 4, &s))</code>
      * <code>Assert false (pc_qpack_decode(indexed, 3, sc, sizeof sc, fail_emit, NULL))</code>
      * <code>Assert false (pc_qpack_decode(litname, 6, sc, sizeof sc, fail_emit, NULL))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_qpack_emit_fail_and_namelen_past</b> &mdash; <i>Literal Field Line with Name Reference + a valid value, but the emit callback rejects it.</i></summary>

    * **Objective**: Literal Field Line with Name Reference + a valid value, but the emit callback rejects it.
    * **Assertions**:
      * <code>Assert false (pc_qpack_decode(nameref, 5, sc, sizeof sc, fail_emit, NULL))</code>
      * <code>Assert false (decode_all(namelen_past, 3, &s))</code>
  </details>

</details>

<details>
<summary><b>test_smtp (39 tests)</b></summary>

  <details style="margin-left: 20px;">
    <summary><b>test_reply_parser_skips_malformed_lines</b> &mdash; <i>Reply parser skips malformed lines</i></summary>

    * **Objective**: Reply parser skips malformed lines
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_reply_bare_three_digit_line_is_final</b> &mdash; <i>Reply bare three digit line is final</i></summary>

    * **Objective**: Reply bare three digit line is final
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_ehlo_capability_scan_edges</b> &mdash; <i>Ehlo capability scan edges</i></summary>

    * **Objective**: Ehlo capability scan edges
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m))</code>
      * <code>Assert equal int (1, m.upgrades)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_null_optional_fields</b> &mdash; <i>Null optional fields</i></summary>

    * **Objective**: Null optional fields
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.find("EHLO esp32\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("Subject: \\r\\n") != std::string::npos); // empty, not "(null)</code>
      * <code>Assert true (m.sent.find("\\r\\n\\r\\n.\\r\\n") != std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_null_password_sends_empty_secret</b> &mdash; <i>Null password sends empty secret</i></summary>

    * **Objective**: Null password sends empty secret
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.find("dXNlcg==\\r\\n") != std::string::npos); // base64("user")</code>
      * <code>Assert true (m.sent.find("AUTH LOGIN\\r\\n\\r\\n") == std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_empty_user_skips_auth</b> &mdash; <i>Empty user skips auth</i></summary>

    * **Objective**: Empty user skips auth
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.find("AUTH") == std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_arg_validation_rejects_each_missing_field</b> &mdash; <i>Arg validation rejects each missing field</i></summary>

    * **Objective**: Arg validation rejects each missing field
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(NULL, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&c, NULL, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&c, &msg, NULL, mock_recv, NULL, &m))</code>
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&c, &msg, mock_send, NULL, NULL, &m))</code>
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&nohost, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&nofrom, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&c, &noto, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&c, &emptyto, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.empty())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_rcpt_251_is_accepted</b> &mdash; <i>Rcpt 251 is accepted</i></summary>

    * **Objective**: Rcpt 251 is accepted
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_command_helper_send_failure</b> &mdash; <i>Command helper send failure</i></summary>

    * **Objective**: Command helper send failure
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_happy_path_no_auth</b> &mdash; <i>Commands, in order.</i></summary>

    * **Objective**: Commands, in order.
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.find("EHLO esp32\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("MAIL FROM:&lt;device@example.net&gt;\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("RCPT TO:&lt;ops@example.net&gt;\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("DATA\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("QUIT\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("Subject: Alert\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("To: &lt;ops@example.net&gt;\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("sensor tripped\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("\\r\\n.\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("AUTH") == std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_auth_login</b> &mdash; <i>Auth login</i></summary>

    * **Objective**: Auth login
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.find("AUTH LOGIN\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("dXNlcg==\\r\\n") != std::string::npos); // base64("user")</code>
      * <code>Assert true (m.sent.find("cGFzcw==\\r\\n") != std::string::npos); // base64("pass")</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_auth_rejected</b> &mdash; <i>Auth rejected</i></summary>

    * **Objective**: Auth rejected
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_AUTH, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_greeting_not_ready</b> &mdash; <i>Greeting not ready</i></summary>

    * **Objective**: Greeting not ready
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_PROTOCOL, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_rcpt_rejected</b> &mdash; <i>Rcpt rejected</i></summary>

    * **Objective**: Rcpt rejected
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_PROTOCOL, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_data_refused</b> &mdash; <i>Data refused</i></summary>

    * **Objective**: Data refused
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_PROTOCOL, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_dot_stuffing</b> &mdash; <i>Dot stuffing</i></summary>

    * **Objective**: Dot stuffing
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.find("..hidden\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("...two dots\\r\\n") != std::string::npos)</code>
      * <code>Assert true (m.sent.find("last\\r\\n.\\r\\n") != std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_multiline_reply_and_lf_body</b> &mdash; <i>Multiline reply and lf body</i></summary>

    * **Objective**: Multiline reply and lf body
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.find("a\\r\\nb\\r\\n") != std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_partial_reads_dribble</b> &mdash; <i>Partial reads dribble</i></summary>

    * **Objective**: Partial reads dribble
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_missing_required_arg</b> &mdash; <i>Missing required arg</i></summary>

    * **Objective**: Missing required arg
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_io_error_when_server_hangs</b> &mdash; <i>Io error when server hangs</i></summary>

    * **Objective**: Io error when server hangs
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_reply_buffer_overflow</b> &mdash; <i>Reply buffer overflow</i></summary>

    * **Objective**: Reply buffer overflow
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_IO, dialogue({huge}, base_cfg(), base_msg()))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_command_send_fails</b> &mdash; <i>Command send fails</i></summary>

    * **Objective**: Command send fails
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_body_send_fails</b> &mdash; <i>Body send fails</i></summary>

    * **Objective**: Body send fails
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_auth_secret_too_long</b> &mdash; <i>Auth secret too long</i></summary>

    * **Objective**: Auth secret too long
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_OVERFLOW, dialogue({"220 ESMTP\\r\\n", "250 OK\\r\\n", "334 x\\r\\n"}, c, base_msg()))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_io_error_at_each_step</b> &mdash; <i>greeting ok, then hang before: EHLO / MAIL(no auth) / AUTH(user) / pass-leg / RCPT / DATA / final.</i></summary>

    * **Objective**: greeting ok, then hang before: EHLO / MAIL(no auth) / AUTH(user) / pass-leg / RCPT / DATA / final.
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_IO, dialogue({"220 x\\r\\n"}, c, msg))</code>
      * <code>Assert equal int (SMTP_ERR_IO, dialogue({"220 x\\r\\n", "250 OK\\r\\n"}, c, msg))</code>
      * <code>Assert equal int (SMTP_ERR_IO, dialogue({"220 x\\r\\n", "250 OK\\r\\n"}, cu, msg))</code>
      * <code>TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO,</code>
      * <code>Assert equal int (SMTP_ERR_IO, dialogue({"220 x\\r\\n", "250 OK\\r\\n", "250 Ok\\r\\n"}, c, msg))</code>
      * <code>TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO,</code>
      * <code>TEST_ASSERT_EQUAL_INT( // final acceptance read</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_protocol_error_at_each_step</b> &mdash; <i>Protocol error at each step</i></summary>

    * **Objective**: Protocol error at each step
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_PROTOCOL, dialogue({"220 x\\r\\n", "500 no ehlo\\r\\n"}, c, msg))</code>
      * <code>TEST_ASSERT_EQUAL_INT(SMTP_ERR_AUTH,</code>
      * <code>TEST_ASSERT_EQUAL_INT(</code>
      * <code>TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL,</code>
      * <code>TEST_ASSERT_EQUAL_INT(                                                                  // final acceptance != 250</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_command_line_overflows</b> &mdash; <i>Command line overflows</i></summary>

    * **Objective**: Command line overflows
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_OVERFLOW, dialogue({"220 x\\r\\n"}, ch, base_msg()))</code>
      * <code>Assert equal int (SMTP_ERR_OVERFLOW, dialogue({"220 x\\r\\n", "250 OK\\r\\n"}, cf, base_msg()))</code>
      * <code>TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW,</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_message_header_overflow</b> &mdash; <i>Message header overflow</i></summary>

    * **Objective**: Message header overflow
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_INT(</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_cr_in_body_dropped</b> &mdash; <i>Cr in body dropped</i></summary>

    * **Objective**: Cr in body dropped
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
      * <code>Assert true (m.sent.find("x\\r\\ny\\r\\n") != std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_build_message_boundary_overflows</b> &mdash; <i>Build message boundary overflows</i></summary>

    * **Objective**: Build message boundary overflows
    * **Assertions**:
      * <code>Assert not equal (SMTP_OK, r)</code>
      * <code>Assert true (saw_overflow)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_host_smtp_send_stub</b> &mdash; <i>Host smtp send stub</i></summary>

    * **Objective**: Host smtp send stub
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_CONNECT, smtp_send(&c, &msg))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_starttls_upgrades_and_reissues_ehlo</b> &mdash; <i>RFC 3207 sec 4.2: EHLO must be reissued after the upgrade, so it appears twice.</i></summary>

    * **Objective**: RFC 3207 sec 4.2: EHLO must be reissued after the upgrade, so it appears twice.
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m))</code>
      * <code>Assert equal int (1, m.upgrades)</code>
      * <code>Assert true (m.sent.find("STARTTLS\\r\\n") != std::string::npos)</code>
      * <code>Assert true (first != std::string::npos)</code>
      * <code>Assert true (m.sent.find("EHLO", first + 1) != std::string::npos)</code>
      * <code>Assert true (m.sent.find("STARTTLS\\r\\n") &lt; m.sent.find("MAIL FROM"))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_starttls_not_advertised_fails_before_auth</b> &mdash; <i>The security property: a server (or an attacker stripping the capability) that does not offer</i></summary>

    * **Objective**: The security property: a server (or an attacker stripping the capability) that does not offer
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_NO_STARTTLS, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m))</code>
      * <code>Assert equal int (0, m.upgrades)</code>
      * <code>Assert true (m.sent.find("AUTH") == std::string::npos)</code>
      * <code>Assert true (m.sent.find("aHVudGVyMg==") == std::string::npos); // base64("hunter2")</code>
      * <code>Assert true (m.sent.find("MAIL FROM") == std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_starttls_partial_keyword_is_not_a_match</b> &mdash; <i>"STARTTLSX" is a different keyword; treating it as STARTTLS would be a silent downgrade.</i></summary>

    * **Objective**: "STARTTLSX" is a different keyword; treating it as STARTTLS would be a silent downgrade.
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_NO_STARTTLS, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_starttls_capability_match_is_case_insensitive</b> &mdash; <i>Starttls capability match is case insensitive</i></summary>

    * **Objective**: Starttls capability match is case insensitive
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m))</code>
      * <code>Assert equal int (1, m.upgrades)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_starttls_server_refuses_the_upgrade</b> &mdash; <i>Starttls server refuses the upgrade</i></summary>

    * **Objective**: Starttls server refuses the upgrade
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_TLS, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m))</code>
      * <code>Assert equal int (0, m.upgrades)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_starttls_handshake_failure_aborts</b> &mdash; <i>Starttls handshake failure aborts</i></summary>

    * **Objective**: Starttls handshake failure aborts
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_TLS, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m))</code>
      * <code>Assert equal int (1, m.upgrades)</code>
      * <code>Assert true (m.sent.find("MAIL FROM") == std::string::npos)</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_starttls_without_an_upgrade_callback_is_an_arg_error</b> &mdash; <i>Starttls without an upgrade callback is an arg error</i></summary>

    * **Objective**: Starttls without an upgrade callback is an arg error
    * **Assertions**:
      * <code>Assert equal int (SMTP_ERR_ARG, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_plain_ignores_an_advertised_starttls</b> &mdash; <i>Configured plaintext: the advertisement is informational, the engine must not upgrade.</i></summary>

    * **Objective**: Configured plaintext: the advertisement is informational, the engine must not upgrade.
    * **Assertions**:
      * <code>Assert equal int (SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m))</code>
      * <code>Assert equal int (0, m.upgrades)</code>
      * <code>Assert true (m.sent.find("STARTTLS\\r\\n") == std::string::npos)</code>
  </details>

</details>

<details>
<summary><b>test_trace_capture (9 tests)</b></summary>

  <details style="margin-left: 20px;">
    <summary><b>test_begin_validates</b> &mdash; <i>Begin validates</i></summary>

    * **Objective**: Begin validates
    * **Assertions**:
      * <code>Assert false (pc_tc_begin(NULL))</code>
      * <code>Assert false (pc_tc_begin(&cfg))</code>
      * <code>Assert false (pc_tc_begin(&cfg))</code>
      * <code>Assert false (pc_tc_begin(&cfg))</code>
      * <code>Assert true (begin(4, 4))</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_pretrigger_ring_wraps_and_freezes_on_trigger</b> &mdash; <i>Feed 6 samples into a 4-deep pre-trigger ring: only the last 4 (2,3,4,5) survive.</i></summary>

    * **Objective**: Feed 6 samples into a 4-deep pre-trigger ring: only the last 4 (2,3,4,5) survive.
    * **Assertions**:
      * <code>Assert true (begin(4, 4))</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(6, pc_tc_feed(pre, 6));</code>
      * <code>Assert false (pc_tc_capturing())</code>
      * <code>Assert true (pc_tc_trigger())</code>
      * <code>Assert true (pc_tc_capturing())</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(4, pc_tc_feed(post, 4));</code>
      * <code>Assert false (pc_tc_capturing())</code>
      * <code>TEST_ASSERT_EQUAL_size_t(1, g_windows.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(4, w.pretrigger_samples);</code>
      * <code>TEST_ASSERT_EQUAL_size_t(8, w.samples.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(expect[i], w.samples[i]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, w.trace_id);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, st.windows_completed);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(0, st.triggers_dropped);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_trigger_fail_closed_while_capturing</b> &mdash; <i>Trigger fail closed while capturing</i></summary>

    * **Objective**: Trigger fail closed while capturing
    * **Assertions**:
      * <code>Assert true (begin(2, 4))</code>
      * <code>Assert true (pc_tc_trigger())</code>
      * <code>Assert false (pc_tc_trigger())</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(1, st.triggers_dropped);</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, g_windows.size()); // still filling, no sink call yet</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_feed_before_begin_or_after_end_drops</b> &mdash; <i>Feed before begin or after end drops</i></summary>

    * **Objective**: Feed before begin or after end drops
    * **Assertions**:
      * <code>TEST_ASSERT_EQUAL_UINT16(0, pc_tc_feed(s, 3)); // never began</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(3, st.samples_dropped);</code>
      * <code>Assert true (begin(2, 2))</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0, pc_tc_feed(s, 3)); // ended</code>
      * <code>Assert false (pc_tc_trigger())</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_zero_pretrigger_edge_case</b> &mdash; <i>Zero pretrigger edge case</i></summary>

    * **Objective**: Zero pretrigger edge case
    * **Assertions**:
      * <code>Assert true (begin(0, 3))</code>
      * <code>Assert true (pc_tc_trigger())</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(3, pc_tc_feed(post, 3));</code>
      * <code>TEST_ASSERT_EQUAL_size_t(1, g_windows.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0, g_windows[0].pretrigger_samples);</code>
      * <code>TEST_ASSERT_EQUAL_size_t(3, g_windows[0].samples.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(7, g_windows[0].samples[0]);</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(9, g_windows[0].samples[2]);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_multiple_sequential_windows_increment_trace_id</b> &mdash; <i>Multiple sequential windows increment trace id</i></summary>

    * **Objective**: Multiple sequential windows increment trace id
    * **Assertions**:
      * <code>Assert true (begin(1, 1))</code>
      * <code>Assert true (pc_tc_trigger())</code>
      * <code>TEST_ASSERT_EQUAL_size_t(3, g_windows.size());</code>
      * <code>TEST_ASSERT_EQUAL_UINT32((uint32_t)i, g_windows[i].trace_id);</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(3, st.windows_completed);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_feed_null_samples_while_configured_drops</b> &mdash; <i>line 76: configured is true, so `!s_tc.configured` is false and the OR</i></summary>

    * **Objective**: line 76: configured is true, so `!s_tc.configured` is false and the OR
    * **Assertions**:
      * <code>Assert true (begin(2, 2))</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(0, pc_tc_feed(NULL, 5));</code>
      * <code>TEST_ASSERT_EQUAL_UINT32(5, st.samples_dropped);</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_zero_posttrigger_never_completes</b> &mdash; <i>line 85 second operand false: with posttrigger 0, after trigger the fill</i></summary>

    * **Objective**: line 85 second operand false: with posttrigger 0, after trigger the fill
    * **Assertions**:
      * <code>Assert true (begin(3, 0)); // pretrigger-only is accepted (only both-zero is rejected)</code>
      * <code>Assert true (pc_tc_trigger())</code>
      * <code>Assert true (pc_tc_capturing())</code>
      * <code>TEST_ASSERT_EQUAL_UINT16(2, pc_tc_feed(more, 2));</code>
      * <code>Assert true (pc_tc_capturing())</code>
      * <code>TEST_ASSERT_EQUAL_size_t(0, g_windows.size()); // 0 post-samples -&gt; never fires</code>
  </details>

  <details style="margin-left: 20px;">
    <summary><b>test_get_stats_null_and_capturing_when_unconfigured</b> &mdash; <i>Get stats null and capturing when unconfigured</i></summary>

    * **Objective**: Get stats null and capturing when unconfigured
    * **Assertions**:
      * <code>Assert false (pc_tc_capturing())</code>
  </details>

</details>

<!-- END GENERATED test-directory -->
