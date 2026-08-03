# Roadmap

Work that is **not finished**. Organized by status rather than by theme, so what is actually in
flight is visible without reading the whole file.

| bucket               | meaning                                                           |
| -------------------- | ----------------------------------------------------------------- |
| **Now**              | in flight, or blocking something that is                          |
| **Next**             | committed and scoped, not started                                 |
| **Later**            | wanted, not yet scoped                                            |
| **Partial**          | a working piece is in the tree, with an explicit _Remaining_ note |
| **Decision records** | settled questions, kept so they are not re-litigated              |

**Item states**

- `- [ ]` not started.
- `- [~]` **partial** - shipped in part; the item states what exists and what remains. These are
  the easiest to lose track of, because from outside they look finished.
- `- [x]` does not appear in this file. Completed work lives in [DELIVERED.md](DELIVERED.md).

**Sizing** is `S` / `M` / `L` / `XL`, carried in the item's tag alongside the standard or vendor
it implements, e.g. `(L, Siemens S7-1200/1500)`.

**Where things live.** This file is the plan. [DELIVERED.md](DELIVERED.md) is what was built;
[FEATURES.md](FEATURES.md) is what the library does today; [CHANGELOG.md](CHANGELOG.md) is when
each change landed; [ARCHITECTURE.md](ARCHITECTURE.md) holds design, including the multi-vendor
and architecture-agnostic work this roadmap schedules. Bugs and known limitations are in
[TODO.md](TODO.md) and [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md).

**Every item must keep the core guarantees**: no heap after `begin()`, fixed-size buffers, a
host-testable core where possible, and a `PC_ENABLE_*` flag (default off) so it costs nothing
when unused.

## Now

In flight. Each of these is either being worked or blocks something that is.

- [ ] **Symbol prefix sweep** (L, house naming law) - ~540 header types, 1112 unprefixed macros, ~383 header
      functions still sit unprefixed in one global C namespace, where generic names like
      `CanFrame` and `BerEnc` can collide with user code. Validate candidates against the
      pre-rename tree so foreign names (`PCB`, `PCA9685`, the `PCR_*` silicon registers) are
      never touched.
- [ ] **Include guards to `PROTOCORE_<FILE>_H`** (M, naming law) - 85 of 373 headers still carry another
      form. `PROTOCORE_` rather than `PC_` because a guard has to be unique in someone else's build,
      where `PC_*` is a plausible name for their own. Two headers exceed the 31-character limit and take
      the documented word-elision exception in [SYMBOLS.md](SYMBOLS.md#4-include-guards-files-and-test-targets).
      Also fix the 18 macros over the same limit. Measure with `ci_tooling/check/check_symbols.py`, then
      re-run `--baseline` so the gate ratchets down.
- [ ] **`ci_tooling/check/check_symbols.py`** (M, CI guard) - enforce the naming law mechanically (prefix and casing per
      symbol kind, macro scope and length, guard form, absence of `namespace`). Must also check
      TYPE-vs-FUNCTION collisions: normalizing types to snake_case collapsed `PCConnCounters`
      onto `pc_conn_counters()` and only a link error caught it.
- [ ] **PIMPL the 34 state types** (L, encapsulation) with opaque fixed storage plus `static_assert`, never heap
      (banned after `begin()`). Secrets then sit low in the stack, behind frames an attacker
      must smash first - the same reasoning as the crypto scratch region.
- [ ] **`pc_octet_ptr` and its accessors** (L, TI C2000 enabler) - pin lane-0 (invariant across targets, NOT word
      endianness), the native-unwrap boundary for DMA/MAC handoff, and the rule that `sizeof`
      is no longer a wire length.
- [ ] **Hot-path framing and masking** (M, throughput) - the remaining cheap wins in the per-byte
      path: the HTTP/2 frame writer and the WebSocket mask/unmask loop still work a byte at a time
      where a word-at-a-time loop applies. This moves steady-state record throughput, not handshake
      latency, so it is measured per feature in docs/FEATURE_PERFORMANCE.md rather than asserted -
      benchmark before and after, and drop the change if the win does not show up on the rig.

## Next

Committed and scoped, not started.

- [ ] **Bulk octet ops in asm** (M, TI C2000 throughput) - move whole words when lanes agree, fall back per-octet only
      when misaligned. This is where the throughput is, not in single-octet access.
- [ ] **Remove the `arduino-esp32` dependency from the core** (L, vendor decoupling) so the ESP-IDF component stops
      requiring it. This is the vendor leak that blocks a platform-neutral build.
- [ ] **CMake rework** (L, multi-platform build) - platform-neutral core target plus per-platform shims (ESP-IDF, Zephyr,
      Pico SDK, host). Prefer an explicit source list over a glob so the build cannot drift.
- [ ] **`examples/` per platform** (L, 8 platforms) - Arduino carries the full L1-L7 catalog as the teaching
      surface; every other platform gets a curated 4-6. Do NOT replicate the catalog: 8
      platforms x 35 examples is an unmaintainable CI matrix.
- [ ] **Zephyr as a platform target** (L, vendor-neutral RTOS) - highest leverage of the set, since one vendor-neutral
      example covers STM/Nordic/NXP/TI across Arm, RISC-V and Xtensa at once.
- [ ] **asm-level guarantee docs** (M, verification) - claim, then disassembly, then why the disassembly
      establishes it. Constant-time crypto, no-heap-after-`begin()`, bounded ISR paths, and the
      octet abstraction compiling away. Measured, not asserted.
- [ ] **`posix/` examples as a CI gate** (S, hardware-free regression) - the only platform that runs without hardware, so the
      natural regression net for the other seven.
- [ ] **Migrate the remaining 6 generators onto `doc_region`** (S, dedupe) - `feature_budget`,
      `gen_api_flow`, `gen_examples`, `gen_feature_tables`, `gen_flag_deps`, and
      `gen_readme_sections` still hand-roll the marked-region read/replace/`--check` logic that
      [`ci_tooling/lib/doc_region.py`](../ci_tooling/lib/doc_region.py) now owns. They use the
      correct `prettier-ignore` mechanism, so they are duplicated rather than wrong; the gate is
      that every `--check` stays green and prettier stays clean.

## Later

Wanted, not yet scoped.

- [ ] **RP2350 Arm-vs-RISC-V comparison** (M, dual-ISA die) - it ships Cortex-M33 and Hazard3 RISC-V cores on one
      die, so an identical build can be A/B'd across architectures without changing boards.

### Industrial & standards protocols

Grouped by the area each belongs to.

#### Industrial protocol additions

- [ ] **S7comm+** (L, Siemens S7-1200/1500) - the modern S7CommPlus protocol, the successor to the
      classic S7comm (`services/s7comm`, S7-300/400) already shipped, over the same COTP/TPKT transport
      (`services/cotp`): the S7CommPlus PDU framing + the integrity/session handshake (protocol
      versioning + the anti-replay integrity id) and read/write of tag values by symbolic access. Harder
      than classic S7comm (session keying); zero-heap codec first, then the session state machine.
- [ ] **PROFIdrive** (L, Siemens drive profile) - the Siemens PROFIBUS/PROFINET drive profile (parameter
      access via PNU, the standard STW1/ZSW1 control/status telegrams + setpoint/actual) over the PROFINET stack.
- [ ] **FANUC robot interop** (M, R-30iB / R-30iA - **next machine-tool step**) - the robot counterpart
      to the shipped FANUC **FOCAS** CNC codec. FANUC robot controllers expose several Ethernet
      interfaces; the tractable, documented ones first: **PROFINET / EtherNet-IP** I/O (already partly
      covered by the shipped `services/enip` + the PROFINET roadmap item - map the robot's assembly
      objects), the **FANUC Robot Ethernet / Stream Motion** UDP position stream, and the **KAREL / R-30iB
      web + socket messaging** (SNPX / the RPC the robot's PC interface uses). Start with a zero-heap codec
      byte-checked against a public reference, then HW-verify against a real controller. Note: "Run MyRobot"
      (above) is the Siemens SINUMERIK route to running a robot from a CNC - a distinct path we also want.
      PARTIAL - the **Stream Motion** leg has shipped: `services/fanuc_j519` (`PC_ENABLE_FANUC_J519`), a
      zero-heap symmetric codec for the J519 UDP protocol (all six packets, the 9-axis binary32 pose /
      joint / motor-current blocks, the 20-entry threshold tables), byte-checked at every field offset
      against the public Wireshark dissector `fanuc-stream-motion/packet-fanuc-stream-motion-j519` and
      host-tested (`native_fanuc_j519`, 13 cases). Still open on this entry: the **PROFINET /
      EtherNet-IP** assembly-object mapping over the shipped `services/enip`, the **KAREL / SNPX**
      socket messaging, and **HW verification of the J519 codec against a real controller** (the codec
      has never been run against physical hardware - only against the documented wire format).
- [ ] **Secure machine agent: G-code deployment over a single secure port** (L) - the device as a
      secure local agent that deploys CNC part programs (G-code) to a machine tool over ONE authenticated,
      encrypted port: NC-program transfer/staging multiplexed on the existing TLS endpoint (or a secured
      OPC UA channel / an OPC UA for Machine Tools program-transfer method), so a shop pushes and stages
      programs to controllers through a single connection instead of per-machine FTP/USB. Northbound
      monitoring = **umati** / **MTConnect** / **FOCAS**; southbound program push builds on TLS + file
      upload (`services/upload_service`) and the existing **DNC** drip-feed (`services/dnc`) for controllers
      with no network stack. The unifying goal behind the machine-tool protocol set.

#### Bare-bones default build + per-service flash cost

- [ ] **Slim the default config to a single-purpose HTTP server** (M) - flip the `src/protocore_config.h`
      `PC_ENABLE_*` defaults so only the core request/response path is on out of the box; every other
      service (WebSocket, SSE, auth, file serving, WebDAV, the L6/L7 protocols, the industrial/IoT buses,
      crypto, storage, ...) defaults **off** and is turned on per build via its own flag. Bare-bones by
      default shrinks the baseline flash/RAM and makes each feature a conscious opt-in. Update the examples
      that rely on a default-on feature to set its flag explicitly, and re-check the configurator + FEATURES
      grid.
- [ ] **Per-base-service flash-cost table** (M) - measure and document the incremental flash (and static
      RAM) each base service adds on top of the bare-bones baseline, so a user can budget a build. Extend the
      footprint tooling (`ci_tooling/generate/example_footprints.py`) to emit a "cost of enabling service X alone" delta
      against the minimal build and land it as a table in `docs/FOOTPRINTS.md`.

#### Split protocore.cpp into single-purpose files

- [ ] **Break protocore.cpp into a `src/server/` of single-purpose files** (L) - carve the monolith into
      cohesive translation units (e.g. `server/lifecycle` for listen/begin/restart, `server/routing` for
      route matching + dispatch, `server/response` for the send/send_template/send_chunked builders,
      `network_drivers/application/file_serving/file_serving`, `network_drivers/application/webdav/webdav`, `server/auth`, and the `server/http2` / `server/http3` seams),
      each with a clear internal header. Pure move-and-split - no behavior change - so the native + dual-core
      builds and the full test suite stay green throughout; keep the owned-context / no-stdlib / single-owner
      rules. Makes the core easier to debug, extend, and maintain.

#### Functional-safety & industrial-standards profiles (IEC 61784-3, ODVA)

- [ ] **IEC 61784-3 black-channel Safety Communication Layer (SCL)** (M) - the shared mechanism the four
      profiles below all specialize: the residual-error model (CRC signature over safety data + a virtual /
      actual consecutive number + a receive watchdog), sized so the probability of an undetected corrupt
      frame meets SIL 3. Land the common primitives once (`services/safety_scl`): a CRC-signature helper, a
      monitoring-counter state machine, and a watchdog/timeout guard, then have each profile's codec compose
      them. Pure + host-tested against per-profile vectors.
      PARTIAL - `services/safety_scl` (`PC_ENABLE_SAFETY_SCL`) lands two of the three primitives: the
      monitoring-counter state machine and the receive watchdog, plus the fail-safe state machine that
      combines them with a caller-supplied signature verdict. Lost / duplicated / inserted / reordered
      frames all reduce to one comparison against the expected counter, and each has its own test case.
      Fail-safe **latches** until an explicit `pc_scl_reset`: a safety layer that silently reheals lets
      an intermittent fault present as a working link, so a subsequent good frame must not revive it
      (asserted). Pure, explicit `now`, wrap-safe watchdog. Host-tested (`native_safety_scl`, 16 cases).
      Still open: the **CRC-signature helper**. The engine half of it now exists -
      `shared_primitives/crc.h` (see **Maintenance**) is the parameterized width / poly / init /
      reflect / xorout engine this entry asked for, so "no engine to configure" is no longer the
      blocker. What remains is genuinely profile-specific: each profile defines not just its
      polynomial, width and seed but the **input ordering** - which fields, in which order, feed the
      register - and those constants live in paid standards. A generic `pc_scl_signature()` would
      still be guessing that ordering, and a wrong safety CRC is worse than none because it looks
      authoritative while failing to detect the corruption it exists to catch. So the caller still
      passes its profile's verdict in as `signature_ok`, and the helper lands **per profile**
      (PROFIsafe CRC2, CIP Safety CRC-S1/S3/S5, FSoE per-slot CRC) as each profile's codec is written
      against its spec - each one configuring the shared engine rather than hand-rolling a loop.
- [ ] **PROFIsafe (IEC 61784-3-3, PI)** (M) - the SIL3 safety layer over the shipped PROFINET / PROFIBUS
      codecs. `PC_ENABLE_PROFISAFE` / `services/profisafe`: the Safety-PDU (F-I/O data + status/control
      byte + consecutive number + CRC2 signature), the F-Parameters (F_Dest_Add / F_WD_Time ...) with their
      CRC1, and the host/device watchdog timing. Pure codec; host-tested and interop-checked against a
      PROFIsafe reference (e.g. a Siemens F-host / codesys-safety).
- [ ] **CIP Safety (IEC 61784-3-2, ODVA)** (M) - the SIL3 safety extension of CIP over the shipped
      EtherNet/IP + CIP codecs. `PC_ENABLE_CIP_SAFETY` / `services/pc_cip_safety`: the safety I/O message
      (Base + Extended format: Mode Byte, Actual + Complemented data, Time Stamp, CRC-S1/S3/S5), the Time
      Coordination exchange, and the safety connection (`SafetyOpen` / `SafetyClose`) producer/consumer time
      expectation. Pure codec; interop vs an ODVA CIP Safety originator.
- [ ] **FSoE / Fail Safe over EtherCAT (IEC 61784-3-12, ETG)** (L) - the SIL3 safety layer over EtherCAT.
      `PC_ENABLE_FSOE` / `services/fsoe`: the FSoE frame (Command + SafeData + a CRC per data slot +
      Connection-ID / sequence), the FSoE connection state machine (Reset -> Session -> Connection ->
      Parameter -> Data), and the watchdog. **Gated on an EtherCAT (mailbox) base transport first** - EtherCAT
      is noted but not yet shipped (see the field-device roadmap above); FSoE itself is black-channel so it
      host-tests standalone against ETG.5100 vectors.
- [ ] **IO-Link Safety (IO-Link Community)** (M) - functional safety over the shipped IO-Link (SDCI,
      IEC 61131-9) codec. `PC_ENABLE_IOLINK_SAFETY` / `services/iolink_safety`: the OSSDe signal handling
      plus the safety-PDU over IO-Link process data / ISDU (CRC + counter + watchdog, FS-Master / FS-Device
      roles). Pure codec; host-tested against the IO-Link Safety test spec vectors.
- [ ] **ODVA CIP Security (CIP Networks Library Vol 8)** (L) - the ODVA _security_ (not safety) profile for
      EtherNet/IP: device authentication + integrity + confidentiality via **TLS (EtherNet/IP TCP) and DTLS
      (UDP I/O)** with X.509 identities, plus the CIP Security object model (the EtherNet/IP Security,
      Certificate Management, and QoS objects). Reuses the crypto already in this library - the mbedTLS TLS
      transport and the hand-rolled **DTLS 1.3** stack ([[dtls13-initiative]]) + Ed25519 / X.509 - so this is
      mostly the CIP object plumbing binding those to `services/enip`. `PC_ENABLE_CIP_SECURITY` /
      `services/pc_cip_security`; interop vs an ODVA CIP Security stack.
- [ ] **PI (PROFIBUS & PROFINET International) - PROFINET Security** (L) - PI is the steward of the shipped
      PROFINET / PROFIBUS codecs and the PROFIsafe profile above; its security counterpart to ODVA's CIP
      Security is **PROFINET Security** (the Security Class 1/2/3 model: integrity + authenticity via a
      per-frame SecurityControl header + MAC, then confidentiality, with certificate / 802.1X device
      identity). `PC_ENABLE_PROFINET_SECURITY` / extend `services/profinet`: the SecurityControl +
      integrity-MAC frame wrapping and the certificate-based identity, reusing the library's crypto
      (HMAC / AES-GCM, X.509). Interop vs a PROFINET Security controller.

#### Industrial wireless & QoS (WirelessHART, DiffServ, IEC 62657-2)

- [ ] **WirelessHART (IEC 62591)** (L) - the 2.4 GHz IEEE 802.15.4 TDMA channel-hopping mesh that carries the
      HART application layer, on top of the shipped `services/hart` command codec (FieldComm Group). Scope the
      realistic slice: the WirelessHART command set (network-manager / device commands) + the network
      management data (superframe / link / route reports) as a pure codec over HART; the full TSMP-style TDMA
      MAC + channel-hopping schedule is radio-firmware territory (like the other 802.15.4 radios) and is
      called out as HW-gated. `PC_ENABLE_WIRELESSHART` / `services/wirelesshart`.
- [ ] **Wireless coexistence management (IEC 62657-2)** (M) - adhere to IEC 62657-2 so the device behaves in
      a crowded industrial 2.4 / 5 GHz band shared by WLAN, WirelessHART, ISA100, Bluetooth, and Zigbee:
      coexistence-aware channel selection plus a spectrum-use / interference report a central coexistence
      manager can consume, and a backoff / channel-blacklist policy. `PC_ENABLE_COEXISTENCE` /
      `services/coexistence`: the reporting data model + the channel-plan policy engine (pure, host-tested);
      the actual RF scan feeds it from the radio driver.

#### Seamless Wi-Fi roaming (IEEE 802.11r / k / v)

- [ ] **802.11r Fast BSS Transition (FT)** (M) - pre-authenticate / pre-derive the PTK with the target AP
      before roaming so reassociation completes in **< 50 ms** (no full 4-way handshake on the new AP). On
      ESP32 the FT key hierarchy (PMK-R0 / PMK-R1) + FT handshake live in the Wi-Fi supplicant, so this is
      enable + configure (`PC_ENABLE_FAST_ROAM`; PSK or WPA2/3-Enterprise FT), expose the handover timing,
      and keep the safety flows pinned across the transition. Measure the handover on the S3 rig against the
      50 ms budget.
- [ ] **802.11k Radio Resource Measurement (neighbor reports)** (S) - request the AP's neighbor report (the
      candidate BSSIDs + channels) so the device targets a roam without a full off-channel scan, and surface
      that list to the decision layer below. `PC_ENABLE_RRM`.
- [ ] **802.11v BSS Transition Management (BTM)** (S) - accept and act on network-suggested roams (BTM
      Request -> Response) so the infrastructure can steer the device to a better AP before its signal drops,
      feeding the hint into the decision layer. `PC_ENABLE_BTM`.
- [ ] **Predictive roaming decision layer** (M) - the policy that fuses 802.11k neighbor reports + 802.11v
      BTM hints + the RSSI trend to pick and execute an 802.11r fast transition _before_ the current link
      degrades, not after it drops - the piece that turns the three primitives into seamless roaming. Pure,
      host-testable logic (feed it synthetic RSSI / neighbor / BTM inputs, assert the roam trigger + target);
      the actual association is the supplicant's. `services/roaming`.

#### Per-variant default sizing (don't kneecap larger boards)

- [ ] **Prune the per-example `build_opt.h` shrink hacks** (S) - once the tiered defaults land, revisit the
      examples that manually dial tunables down (MeshCache and any others carrying a "sized down to fit
      classic-ESP32 DRAM" note) and drop the override where the `CLASSIC`-tier default already fits, so the
      example shows the real intended configuration on each board instead of the lowest-common-denominator one.
- [ ] **CI: assert both ends of the range** (S) - keep the Arduino Build classic-ESP32 `dram0_0_seg` link
      check (the guard that caught MeshCache) and add an S3/PSRAM build of the same heavy examples asserting
      the larger-tier defaults actually took effect (link succeeds and uses the bigger sizes), so a future
      change can't silently re-flatten the profiles or regress the classic-board ceiling.

## Partial - shipped in part, remaining work noted

Each item has a working piece in the tree and an explicit _Remaining_ note.

### Industrial protocol additions

- [~] **MTConnect** (L, ANSI/MTC1.4-2018) _(streams/error response codec shipped)_ -
  `PC_ENABLE_MTCONNECT` (`services/mtconnect`): the MTConnect agent response documents over the
  existing HTTP stack - an incremental **MTConnectStreams** builder (the `current` / `sample`
  response: header with instanceId + nextSequence, then per-DataItem `<Samples>` / `<Events>` /
  `<Condition>` observations) and the **MTConnectError** document, XML-escaped into a caller buffer;
  host-tested (`native_mtconnect`). _Remaining:_ the `probe` **MTConnectDevices** doc, `MTConnectAssets`,
  the fixed-capacity Devices->Components->DataItems model + the circular sample buffer keyed by
  instanceId + sequence (the `from`/`count`/`interval` long-poll semantics). Pairs with gpio_map /
  dashboard as DataItems.
- [~] **Industrial Ethernet** (XL, EtherNet/IP, PROFINET, EtherCAT) - _EtherNet/IP + CIP
  messaging shipped._ `PC_ENABLE_ENIP` (`services/enip`): the encapsulation layer (TCP/UDP 44818) - `pc_eip_build` / `pc_eip_parse` (the 24-octet header), `pc_eip_build_register_session`, and
  `pc_eip_build_send_rr_data` / `pc_eip_parse_send_rr_data` (wrap/unwrap a CIP message via the Common
  Packet Format). `PC_ENABLE_CIP` (`services/cip`): the CIP message inside it -
  `pc_cip_build_epath` (class/instance/attribute logical segments), `pc_cip_build_get_attr_single` /
  `pc_cip_build_request`, and `pc_cip_parse_response`. Together they form a working CIP read path
  (wrap the CIP request with `pc_eip_build_send_rr_data`); both verified against the Wireshark
  ENIP/CIP dissectors, host-tested. Remaining: the broader CIP object dictionary +
  implicit/IO messaging. **PROFINET** (RT, raw L2
  frames) and **EtherCAT** rely on raw L2, which the ESP32 _does_ provide (see
  Low-level networking above - `esp_eth_transmit` / `esp_eth_update_input_path`), and
  the IRT/isochronous cyclic deadline is met by the high-priority preempting-ISR/task
  model in a single-feature build (this protocol + base web server only). So the full
  cyclic RT/IRT stack is on the table, not just a discovery subset; PROFINET `DCP` is
  the easy first milestone, EtherCAT/IRT the hard one. Each: fixed BSS
  object/process-image model, no heap.
- [~] **Fieldbuses** (L, classic serial/CAN buses) - the pre-Ethernet field protocols
  built on the existing zero-heap codec pattern _(the named buses are shipped)_: **CANopen** (CiA 301:
  object dictionary, PDO/SDO, NMT, heartbeat) over the ESP32 TWAI/CAN controller is
  `PC_ENABLE_CANOPEN` (`services/canopen`); **PROFIBUS-DP** is `PC_ENABLE_PROFIBUS`
  (`services/profibus`); **DeviceNet** (CIP-over-CAN) is `PC_ENABLE_DEVICENET`; Modbus TCP + RTU ship
  as [modbus](../src/services/fieldbus/modbus/) (`PC_ENABLE_MODBUS` / `_MODBUS_RTU`). All host-tested behind
  their own build flags. _Remaining:_ the per-bus serial/CAN PHY drivers + HW-over-RS-485/CAN verify
  (tracked per protocol above).
- [~] **Modbus RTU** (M, serial / RS-485) - _codec shipped._ `PC_ENABLE_MODBUS_RTU`
  adds `pc_modbus_rtu_process_adu()`: the `[unit-id][PDU][CRC-16/Modbus]` serial frame
  around the existing data model + function-code dispatch - a CRC mismatch or a
  non-matching unit address is dropped silently, a broadcast (address 0) executes
  with no reply. Host-tested (`test_modbus`, 5 RTU cases incl. the 0xCDC5 CRC vector + read round-trip + bad-CRC/wrong-addr/broadcast). The pure codec is fed a complete
  frame; a UART/RS-485 driver (3.5-char inter-frame idle) + HW-over-RS-485 verify are
  the remaining transport step. (Modbus ASCII is a lower-priority follow-on.)
- [~] **PROFINET / PROFIBUS** (XL, Siemens automation) _(PROFINET DCP + PROFIBUS-DP FDL codecs shipped)_ -
  **PROFINET DCP**: `PC_ENABLE_PROFINET` (`services/profinet`) - the DCP frame codec (10-octet header +
  option/suboption blocks with even-padding, built by `pc_pn_dcp_header` / `_block`, walked by
  `pc_pn_dcp_walk`) for Identify + Set (assign NameOfStation / IP) over raw L2 (ethertype 0x8892);
  host-tested (`native_profinet`). **PROFIBUS-DP**: `PC_ENABLE_PROFIBUS` (`services/profibus`) - the
  FDL telegram codec - SD1 (no-data) and SD2 (variable-data: `SD2 LE LEr SD2 DA SA FC data FCS ED`,
  arithmetic-sum FCS) that a DP master exchanges with slaves over RS-485; host-tested (`native_profibus`).
  _Remaining:_ the PROFINET IO cyclic data exchange + GSDML, and the DP-V0 master state machine + GSD
  slave model. Fixed BSS process image, no heap.
- [~] **SIMATIC serial family** (M, Siemens legacy point-to-point) - the pre-Ethernet SIMATIC link +
  messaging protocols as zero-heap codecs, pairing with the existing S7comm/COTP stack for the full
  Siemens set: **3964(R)** (the byte-oriented point-to-point link protocol - STX/DLE/ETX framing with
  priority arbitration and a BCC block-check byte on the "R" variant), **RK512** (the message /
  interpreter layer carried over 3964R - FETCH/SEND telegrams addressing DB/data blocks, words and
  bits), plus the **MPI** (Multipoint Interface bus) and **PtP IF** (point-to-point CP serial
  interface) link layers. Serial-first (RS-232/RS-485 transport step tracked with the other serial buses).
  SHIPPED (3964R + RK512): services/fieldbus/simatic (PC_ENABLE_SIMATIC) - the 3964R block framing (DLE-double +
  XOR BCC) + the interactive link state machine (STX/DLE handshake, QVZ/ZVZ timeouts, NAK/retry,
  priority arbitration) + the RK512 SEND/FETCH/reaction telegrams (big-endian words). Pure codec +
  one owned link context, host-tested (native_simatic, 14 cases) + byte-cross-checked against an
  independent python 3964R+RK512 reference peer (simatic_peer.py), compiles on-target. REMAINING: MPI
  (token-bus network layer) + PtP IF (generic CP serial), and the live RS-232/485 UART interop against
  a real Siemens PtP CP (tracked with the other serial-bus PHY drivers).
- [~] **DNP3** (L, IEEE 1815) - _data-link frame codec shipped._ `PC_ENABLE_DNP3`
  (`services\dnp3`): a zero-heap builder + CRC-validating parser for the data-link layer - `pc_dnp3_build_frame` emits the `0x0564 LEN CTRL DEST SRC CRC` header block plus the
  CRC'd 16-octet user-data blocks, and `pc_dnp3_parse_frame` validates the header and every
  block CRC (CRC-16/DNP, verified against the canonical 0xEA82 check value) and de-blocks
  the user data. The remaining layers (transport-function segmentation, the application
  objects with groups/variations, Class 0/1/2/3 polling, unsolicited responses,
  time-sync) layer on the de-blocked user data; fixed BSS point database, no heap.
  Optional Secure Authentication (IEEE 1815 SAv5) later.
- [~] **HART** (M, FieldComm) _(frame + HART-IP codec shipped)_ - `PC_ENABLE_HART` (`services/hart`):
  the HART command-frame codec - `pc_hart_build` / `_parse` with the longitudinal XOR checksum and
  short (polling) + long (unique-ID) addressing - and the 8-octet **HART-IP** message header
  (version / type / id / status / seq / length) for the front-end-free UDP/TCP 5094 path; host-tested
  against hand-verified vectors (`native_hart`, command-0 checksum 0x82). _Remaining:_ the command set
  (universal + common-practice) + device-variable/status model + burst mode, and the FSK modem
  (over UART) / 4-20 mA-off-the-ADC transports. Fixed BSS, no heap.
- [~] **CC-Link / CC-Link IE** (L, CLPA) _(cyclic frame codec shipped)_ - `PC_ENABLE_CCLINK`
  (`services/cclink`): the CC-Link cyclic frame - `pc_cclink_build` / `_parse` for
  `[station][command][RX/RY bit data][RWr/RWw word data][sum checksum]` that a Mitsubishi CC-Link
  master exchanges with remote stations over RS-485, plus the bit/word process-image accessors
  (`get_bit` / `set_bit` / `get_word`); host-tested (`native_cclink`). _Remaining:_ the master
  poll/refresh state machine + the RS-485 timing, and **CC-Link IE Field** (Gbit PHY-gated). Fixed
  BSS station model, no heap.
- [~] **PROFIBUS PA** (M, process automation) _(rides the shipped DP FDL codec)_ - PROFIBUS PA runs the
  same DP-V0/V1 application (the shipped `services/profibus` FDL telegrams) over the MBP (Manchester
  Bus Powered, IEC 61158-2) physical layer for hazardous areas. The FDL/DP telegram layer is done;
  _remaining:_ the PA device profiles (transmitters/valves) + the MBP physical layer (hardware-gated -
  couples to a DP segment via a segment coupler in practice).
- [~] **CANopen** (M, CiA 301) _(message codec shipped)_ - `PC_ENABLE_CANOPEN`
  (`services/canopen`): a zero-heap CiA 301 message codec over the shared CAN frame
  (`shared_primitives/can.h`) - NMT, SYNC, heartbeat, EMCY, PDO, and expedited SDO
  read/write/abort plus the COB-ID classifier; host-tested (`native_canopen`, 17 cases).
  _Remaining:_ a first-class object dictionary + node-guarding, the DS401 generic-I/O device
  profile, and the ESP32 TWAI/CAN transport binding. Fixed BSS, no heap.
- [~] **IO-Link** (M, IEC 61131-9) _(data-link codec shipped)_ - `PC_ENABLE_IOLINK`
  (`services/iolink`): the SDCI data-link message codec - the MC / CKT / CKS control octets and
  the SDCI checksum (seed 0x52 + the 8->6 compression of IO-Link spec A.1.6), against a
  hand-computed known-answer vector; host-tested (`native_iolink`). _Remaining:_ the on-request
  ISDU parameter service, the IODD-described device model + state machine, and the 3-wire physical
  layer (hardware-gated). Fixed BSS, no heap.
- [~] **POWERLINK** (XL, EPSG) _(basic frame codec shipped)_ - `PC_ENABLE_POWERLINK`
  (`services/powerlink`): the EPL basic frames - `pc_epl_build` / `_parse` (and the SoC / PReq /
  PRes convenience builders) for `[messageType][dest][source][payload]`, the four cyclic message
  types (SoC start-of-cycle, PReq poll-request, PRes poll-response with process data, SoA
  start-of-async) that make the isochronous managed-node cycle, over raw L2 (ethertype 0x88AB, on the
  shipped services/fieldbus/rawl2); host-tested (`native_powerlink`). _Remaining:_ the MN slot-schedule state
  machine + async SDO + object dictionary, and the isochronous timing (the preempting-task model).
  Fixed BSS, no heap.
- [~] **SERCOS III** (XL, motion bus) _(telegram + IDN codec shipped)_ - `PC_ENABLE_SERCOS`
  (`services/sercos`): the cyclic **MDT/AT** telegram codec - `pc_sercos_build` / `_parse` for
  `[type][phase][cycle][cyclic data]` (the master's setpoint telegram + the drive's actual-value
  telegram) over raw L2 (ethertype 0x88CD, on the shipped services/fieldbus/rawl2) - and the 16-bit **IDN**
  encode/decode (S/P bit + parameter-set + data-block, the "S-0-0100" drive-parameter addressing);
  host-tested (`native_sercos`). _Remaining:_ the hard-real-time slot schedule + the IDN
  service-channel transfer state machine, and the isochronous timing (preempting-task model). Fixed
  BSS, no heap.
- [~] **DeviceNet** (L, CIP over CAN) _(link-adaptation codec shipped)_ - `PC_ENABLE_DEVICENET`
  (`services/devicenet`): the CAN link adaptation for CIP-over-CAN - the 4-group 11-bit CAN id, the
  explicit-message header octet, single-frame explicit messages, and the fragmentation reassembler
  (the CIP body itself is built with the shared `cip` codec); host-tested (`native_devicenet`).
  _Remaining:_ the predefined master/slave connection set, I/O (implicit) messaging, and the ESP32
  TWAI/CAN transport. Fixed BSS, no heap; pairs with the EtherNet/IP CIP work.
- [~] **LonWorks / LON** (L, ISO/IEC 14908) _(network-variable codec shipped)_ - `PC_ENABLE_LONWORKS`
  (`services/lonworks`): the LonTalk network-variable PDU - `pc_lon_build_nv` / `_parse_nv` for
  `[msg-code][14-bit selector][value]` (an NV update / poll), over **LON/IP** (IEC 14908-4, over UDP,
  so no Neuron/transceiver is needed - the host-reachable path) - plus the common SNVT scalar
  encodings (`SNVT_temp` 0.01 K fixed-point, `SNVT_switch` level + state); host-tested
  (`native_lonworks`). _Remaining:_ the fuller SNVT type table + the NV binding/address table. Fixed
  BSS NV table, no heap.
- [~] **Modbus Plus** (L, HDLC token bus) _(frame + token MAC shipped)_ - `PC_ENABLE_MBPLUS`
  (`services/mbplus`): the Modbus Plus HDLC frame codec - `pc_mbplus_build` / `_parse` for
  `[7E][addr][ctrl][payload][CRC-16/X-25][7E]` and `pc_mbplus_next_token` (the next station in the
  logical ring) - reusing the shipped Modbus PDU model for the payload; host-tested (`native_mbplus`,
  CRC check value 0x906E). _Remaining:_ the full path/routing model + the token-rotation timing, and
  the custom 1 Mbit/s bus (hardware-gated).
- [~] **INTERBUS** (L, Phoenix Contact) _(summation-frame codec shipped)_ - `PC_ENABLE_INTERBUS`
  (`services/interbus`): the summation frame - `pc_interbus_build` / `_parse` assemble the single
  rotating frame (loopback word + each device's 16-bit process-image slice + CRC-16/CCITT FCS) from a
  list of per-device word slices and disassemble a received frame back into them; host-tested
  (`native_interbus`, FCS check value 0x29B1). _Remaining:_ the PCP parameter channel + the physical
  ring shift-register clocking (hardware-gated). Fixed BSS, no heap.
- [~] **AMQP** (L, OASIS AMQP 1.0 / 0-9-1) - _the 0-9-1 frame codec is shipped._
  `PC_ENABLE_AMQP` (`services\amqp`): `pc_amqp_protocol_header` (the `"AMQP" 0 0 9 1`
  preamble), `pc_amqp_build_frame` / `pc_amqp_parse_frame` (type + channel + size + payload +
  the 0xCE frame-end), `pc_amqp_build_method` / `pc_amqp_parse_method` (a METHOD frame's
  class-id / method-id / arguments), and `pc_amqp_build_heartbeat`; host-tested. Remaining:
  the 0-9-1 method-argument field encoding (the connection/channel/exchange/queue/basic
  classes) for RabbitMQ interop, and the **AMQP 1.0** type system + framing (open / begin
  / attach / transfer / disposition) for broker links. Zero-heap: fixed link/session
  state, the payload streamed through the client transport, one build flag. Pairs with the
  MQTT / webhook outbound integrations.
- [~] **DF1 / DH+** (M-L, Allen-Bradley / Rockwell) - _DF1 full-duplex frame codec
  shipped._ `PC_ENABLE_DF1` (`services\df1`): a zero-heap framing + DLE byte-stuffing +
  BCC/CRC codec for the serial link layer (pub. 1770-6.5.16) - `pc_df1_build_frame` wraps
  application data in `DLE STX ... DLE ETX` (doubled-DLE escape) with a BCC (2's
  complement of the data sum) or CRC-16/ARC (over the data + ETX, low byte first), and
  `pc_df1_parse_frame` validates and un-stuffs; vectors verified against the manual (BCC
  `0x20`->`0xE0`, CRC `0xBB3D`). Remaining: the **PCCC** command set (PLC-5 / SLC-500
  data-table read/write) inside the application data, the half-duplex master/slave
  framing, and **DH+** (Data Highway Plus token LAN, physical-layer-gated). Fixed BSS,
  no heap.
- [~] **S7comm / S7comm-Plus** (L, Siemens S7) - _ISO-on-TCP framing + the S7comm read
  path shipped._ The TPKT + COTP transport (RFC 1006 / X.224, port 102) is a reusable codec
- [~] **MELSECNET** (L, Mitsubishi) - _MC protocol binary 3E batch-read shipped._
  `PC_ENABLE_MELSEC` (`services\melsec`): `pc_melsec_build_read` emits the binary 3E
  batch-read (word) frame (little-endian fields, subheader 0x5000, command 0x0401, the
  device code + 24-bit head device + point count) and `pc_melsec_parse_response` validates
  the 0xD000 response and reports the end code + data; layout + device codes verified
  against a third-party MC impl, host-tested. Completes the major-vendor PLC read set
  (Omron FINS / Host Link, AB DF1, Siemens S7comm). Remaining: MC batch write + the
  1E/4E + ASCII frame variants, and **MELSECNET/H** / **/10** (the cyclic control
  network, PHY/timing-gated). Fixed BSS device model, no heap.
- [~] **FINS** (M-L, Omron) - _FINS/UDP frame codec shipped._ `PC_ENABLE_FINS`
  (`services\fins`): a zero-heap command/response builder + parser - `pc_fins_build_command`
  / `pc_fins_build_memory_area_read` emit the 10-octet routing header + MRC/SRC command code + parameters, and `pc_fins_parse_command` / `pc_fins_parse_response` read them back (the
  response MRES/SRES end code included), over the shipped UDP transport. Header layout
  verified against the FINS spec; pure, host-tested. Remaining: the full command set
  (memory-area write / run-stop / clock) and the FINS/TCP framing + the
  FINS/Hostlink-gateway addressing model. Fixed BSS device model, no heap.
- [~] **Host Link** (M, Omron) - _C-mode frame codec shipped._ `PC_ENABLE_HOSTLINK`
  (`services\hostlink`): a zero-heap ASCII frame builder + FCS-validating parser for
  Omron's serial C-mode protocol - `pc_hostlink_build` emits `@UU` + header code + text +
  FCS + `*`CR (FCS = the 8-bit XOR from `@` through the text, verified against the
  `@00RD00000010` -> `57` vector), and `pc_hostlink_parse` / `pc_hostlink_end_code` validate and
  split a frame. Pairs with the FINS work (Host Link is the serial sibling); pure,
  host-tested. Remaining: the per-command text encoders (RD/WD/... of the DM/CIO areas)
  and the UART transport. Fixed BSS, no heap.
- [~] **SNP** (M, GE Fanuc Series Ninety Protocol) _(frame codec shipped)_ - `PC_ENABLE_SNP`
  (`services/snp`): the SNP master-slave serial frame - `pc_snp_build` / `_parse` for
  `[control][length][data][arithmetic-sum BCC]` with the control-byte constants, for register
  read/write on a GE Fanuc Series 90 (90-30/90-70) PLC over RS-485; host-tested (`native_snp`).
  _Remaining:_ the SNP-X session setup + the per-command register-access encoders, and the UART
  transport. Fixed BSS, no heap.
- [~] **DirectNET** (M, AutomationDirect) _(frame codec shipped)_ - `PC_ENABLE_DIRECTNET`
  (`services/directnet`): the DirectNET master-slave serial frames - `pc_dnet_header` (SOH +
  ASCII-hex slave/type/address/blocks + ETB + LRC) and `pc_dnet_data` / `_data_parse` (STX + data + ETX + LRC) - for V-memory read/write on an AutomationDirect DirectLOGIC PLC, with the
  longitudinal-XOR LRC and control-byte constants; host-tested (`native_directnet`). _Remaining:_ the
  ACK/NAK handshake sequencing + the V-memory address map, and the UART transport. Fixed BSS, no heap.
- [~] **IEC 60870-5-101 / -104** (L, power-grid SCADA) _(codec shipped)_ - `PC_ENABLE_IEC60870`
  (`services/iec60870`): the tele-control codec - the **-104** APCI (I/S/U frames), the ASDU header + 3-octet IOA, and the **-101** FT1.2 fixed/variable link frames (sum checksum); host-tested
  (`native_iec60870`). _Remaining:_ the fuller ASDU information-object type set (double points,
  measured values, commands) + the k/w sequence flow-control state machine over the shipped TCP
  transport. Fixed BSS point database, no heap.
- [~] **IEC 61850** (XL, substation automation) _(GOOSE publisher + MMS Read shipped)_ - **GOOSE**:
  `PC_ENABLE_GOOSE` (`services/goose`) - the raw-L2 multicast event publish for fast trips (the
  BER-encoded IECGoosePdu wrapped in the 8-octet GOOSE header + Ethernet frame, ethertype 0x88B8, on the
  shipped raw-L2 codec; host-tested `native_goose`). **MMS**: `PC_ENABLE_MMS` (`services/mms`) - the
  ACSI client/server core, the BER confirmed-request/response **Read** PDUs (invokeID + read service + a
  named Data-Object ObjectName) build + parse, over ISO-on-TCP (TPKT + COTP via the shipped
  `services/cotp`) on port 102; host-tested (`native_mms`). _Remaining:_ GOOSE subscribe/decode + the
  fast-retransmit timer, the fuller MMS service set (Write, GetNameList, reports) + the SCL-driven object
  model. Fixed BSS, no heap.
- [~] **IEEE C37.118** (M-L, synchrophasors) - _frame codec shipped._
  `PC_ENABLE_C37118` (`services\c37118`): a zero-heap builder + CRC-validating parser
  for the PMU synchrophasor wire frame (`SYNC FRAMESIZE IDCODE SOC FRACSEC DATA CHK`,
  CHK = CRC-CCITT) - `pc_c37118_build_frame` frames any payload, `pc_c37118_build_command`
  handles the fixed Command frame, and `pc_c37118_parse_frame` validates the CRC and reports
  the frame type / id / timestamp / payload, with `pc_c37118_parse_command`. CRC verified
  against the canonical CRC-CCITT-FALSE check value; pure, host-tested. The fixed phasor
  configuration / data model (encoding the CFG-2 channel layout and the matching data
  frame, over TCP/UDP 4712/4713, streamed via the chunked / UDP cast path) remains to be
  layered on top. Pairs with the telemetry-math service for on-device PMU analytics.
- [~] **IEEE 2030.5 (SEP 2.0)** (M-L, DER / smart energy) _(resource codec shipped)_ -
  `PC_ENABLE_SEP2` (`services/sep2`): the core 2030.5 XML resource documents in the
  `urn:ieee:std:2030.5:ns` namespace - **DeviceCapability** (the root function-set links),
  **EndDevice** (sFDI/lFDI registration), and **DERControl** (a DER dispatch/curtailment event:
  interval + opModFixedW setpoint) - built into a caller buffer over the existing HTTP + TLS stack;
  host-tested (`native_sep2`). _Remaining:_ the fuller function-set resource schema (metering, pricing,
  demand response) + the resource discovery walk + optional EXI, on a fixed BSS model.
- [~] **OpenADR** (L, demand response) _(3.0 event/report codec shipped)_ - `PC_ENABLE_OPENADR`
  (`services/openadr`): the OpenADR 3.0 REST/JSON objects - `pc_openadr_event` (a demand-response
  signal: programID + eventName + interval payload points with start / duration / type / value) and
  `pc_openadr_report` (a VEN reading back to the VTN) - built into a caller buffer over the
  existing HTTP client/server + OAuth2, with JSON escaping and a no-stdlib 3-decimal value formatter;
  host-tested (`native_openadr`). _Remaining:_ the full VEN/VTN role state machine + subscriptions,
  and the OpenADR 2.0 XML/EXI profile. Fixed BSS, no heap.
- [~] **ICCP / TASE.2** (XL, IEC 60870-6) _(Data_Value codec shipped)_ - `PC_ENABLE_ICCP`
  (`services/iccp`): the TASE.2 indication-point Data_Value BER structures - `pc_iccp_state_q`
  (StateQ: a discrete state + quality flags) and `pc_iccp_real_q` (RealQ: a scaled INTEGER value +
  quality), each with an optional 4-octet TimeStamp - the telemetry a control center transfers as MMS
  Reads (on the shipped `services/mms` + `services/cotp`, ISO-on-TCP 102); host-tested (`native_iccp`).
  _Remaining:_ the data-set / transfer-set / bilateral-table object model on top of the MMS ACSI core.
  Fixed BSS object model, no heap.

### Per-variant default sizing (don't kneecap larger boards)

- [~] **Per-variant default profiles** (M) - **framework shipped** (`src/board_drivers/board_profiles/`): `board_profile.h`
  selects, along three independent axes, per-variant default files, each guarded by `#ifndef` so a `-D` /
  `build_opt.h` override always wins. Axes: **chip** (`classic_defaults.h` / `s3_defaults.h` /
  `c6_defaults.h` / `p4_defaults.h`, auto-selected from `CONFIG_IDF_TARGET_*`, holding HW-specific switches +
  chip-appropriate defaults), **PSRAM size** (`8/16/32mbpsram.h`), and **flash size** (`8/16/32mbflash.h`) -
  PSRAM/flash are their own axes because a chip ships in several configs (set `-DPC_PSRAM_MB` / `-DPC_FLASH_MB`,
  auto-filled from `CONFIG_SPIRAM_SIZE` / `CONFIG_ESPTOOLPY_FLASHSIZE_*` on ESP-IDF). Precedence: user -D >
  PSRAM > flash > chip > classic floor. Classic ESP32 keeps its exact historical numbers (no regression);
  larger variants auto-scale. Piloted on the edge-cache + mesh pools; **remaining:** migrate more sizing knobs
  (TLS arena / MAX_TLS_CONNS / MFL, packet + handle buffers, connection tables) into the profiles, wire the
  HW-specific switches (e.g. `PC_HAS_CRYPTO_HWACCEL`) into the crypto paths that today check `CONFIG_IDF_TARGET_*`
  directly, and populate the flash-size axis once a flash-backed default exists.

Each item below has a working piece in the tree and an explicit _Remaining_ note.
They are grouped by the area they came from.

### Concurrency / performance

- [~] \*LoRa / LoRaWAN gateway (L) - Semtech SX127x / SX126x, RFM95/96 over SPI.
  **Codec + SX127x driver shipped** (`PC_ENABLE_LORA`, `services/lora`): the
  RadioHead 4-byte header codec + the SX1276 register protocol (init / send / recv /
  RSSI) over a portable register-access bus, host-tested against a mock chip; example
  LoRaGateway drives a real RFM95 and bridges frames northbound via the gateway.
  Remaining: verify the RF link on the module, an SX126x variant, and a bounded
  LoRaWAN Class A uplink/downlink codec.
- [~] \*nRF24L01+ gateway (M) - Nordic 2.4 GHz over SPI; cheap point-to-multipoint
  sensor links bridged to the web stack. **Driver shipped** (`PC_ENABLE_NRF24`,
  `services/nrf24`): the SPI command protocol (init / send / recv, pipe-addressed,
  static payload) over an SPI + CE bus, host-tested against a mock chip; example
  Nrf24Gateway bridges frames northbound via the gateway. Remaining: verify the
  RF link on the module (and optional dynamic-payload / auto-ack).
- [~] \*CC1101 sub-GHz gateway (M) - TI 300-928 MHz OOK / 2-FSK over SPI; generic
  ISM-band remotes and sensors. **Driver shipped** (`PC_ENABLE_CC1101`, `services/cc1101`): the CC1101
  SPI header protocol (config registers, the 13 command strobes, status registers, TX/RX FIFO) - reset +
  apply a caller-supplied SmartRF register table + set channel + verify VERSION (`pc_cc1101_init`),
  variable-length `pc_cc1101_send`, TX-done poll, `pc_cc1101_set_rx`, and `pc_cc1101_recv` with the appended
  RSSI/LQI status + `pc_cc1101_rssi_dbm` decode, over a portable SPI bus; host-tested against a mock chip
  (`native_cc1101`). Bridges northbound via the gateway framework. Remaining: verify the RF link on the
  module (and an OOK/ASK preset variant).
- [~] \*Zigbee NCP gateway (L) - Silicon Labs EZSP (EFR32) / Digi XBee / TI ZNP over
  UART; join as coordinator and bridge Zigbee devices to MQTT. **ASH framing codec
  shipped** (`PC_ENABLE_ZIGBEE`, `services/zigbee`): the byte-stuffed, CRC-16/CCITT
  ASH data-link (encode/decode) that carries EZSP, host-tested against the documented
  RST frame; example ZigbeeGateway bridges a real NCP. Remaining: the EZSP command
  layer (version negotiation, incomingMessageHandler) + verify against an NCP.
  **Blocked on hardware:** this is a bridge to an _external_ EZSP/ZNP co-processor, so closing it
  needs a Silicon Labs EmberZNet NCP (EFR32), a Digi XBee, or a TI ZNP module - none on hand, and
  unlike Thread's spinel there is no open reference NCP to interop against in software (EmberZNet
  and Z-Stack are proprietary; zigpy/bellows are _hosts_, not NCPs). Note an ESP32-C6/H2 does
  **not** close this: its native 802.15.4 radio speaks Espressif's own Zigbee stack, not EZSP over
  a UART, so it is a different architecture rather than a drop-in NCP.
- [~] \*Z-Wave Serial API gateway (M) - Silicon Labs 500 / 700-series over UART.
  **Serial API frame codec shipped** (`PC_ENABLE_ZWAVE`, `services/zwave`):
  SOF/LEN/Type/Cmd/Data frames + the XOR checksum + ACK/NAK/CAN, host-tested against
  the documented GetVersion frame; example ZWaveGateway bridges a real controller.
  Remaining: verify against a controller + inclusion / SendData sequences.
- [~] \*EnOcean gateway (M) - energy-harvesting 868 MHz ESP3 protocol over UART.
  **ESP3 codec shipped** (`PC_ENABLE_ENOCEAN`, `services/enocean`): telegram
  parse/build + the CRC-8 (poly 0x07), host-tested with known-answer CRCs and
  malformed/resync framing; example EnOceanGateway bridges a real TCM 310 over
  UART. Remaining: verify against a module + EEP profile decoding.
- [~] \*Sigfox uplink (S) - Wisol / Murata module over UART; tiny low-power uplinks.
  **AT-command codec shipped** (`PC_ENABLE_SIGFOX`, `services/sigfox`): the
  `AT$SF=<hex>` uplink formatter + OK/ERROR/pending response classifier, host-tested;
  example SigfoxUplink sends a reading from a real modem. Remaining: verify against
  a module + subscription.
- [~] \*Wi-SUN FAN **connector** (L) - NOT a radio-module driver: the direct FAN
  UART modules (Rohm BP35A1 class) are obsoleted, so Wi-SUN is reached as a
  **connector to a border router / devboard** that already terminates the FAN mesh.
  Wi-SUN FAN is an IPv6 / UDP / CoAP network, so this rides the existing IP stack
  (CoAP / UDP client to the border router) rather than a byte-level radio codec.
  **Connector shipped** (`PC_ENABLE_WISUN`, `services/wisun`): since the CoAP service ships only a
  _server_, the connector adds the CoAP **client** request builder (`pc_wisun_build_coap`: RFC 7252 header +
  delta-encoded Uri-Path options with extended-length + payload) plus the FAN node registry (register /
  find / joined-count) keyed on `pc_ip`, and `pc_wisun_nodes_json` for the web. The app sends the built PDU
  to a node's IPv6 address over `pc_udp`; pure + host-tested (`native_wisun`). _Remaining:_ point it at a
  chosen border-router devboard and verify the mesh end to end (the devboard choice is the only external
  dependency; the connector code is board-agnostic).

I2C / SPI / UART:

- [~] \*NFC / RFID gateway (M) - PN532 (I2C / SPI / UART) or MFRC522 (SPI); tag
  read / write bridged to an HTTP / MQTT event. **PN532 frame codec shipped**
  (`PC_ENABLE_PN532`, `services/pn532`): normal-information-frame build/parse +
  ACK with the LCS / DCS checksums, host-tested against the documented
  GetFirmwareVersion frames; example NfcGateway reads a real PN532 over I2C.
  Remaining: verify against a reader + the MFRC522 variant.

Built-in radio:

- [~] \*BLE GATT bridge (M) - on-chip ESP32 BLE (and external HCI-UART modules):
  scan / expose GATT characteristics and bridge them to the web stack. **ATT codec + bridge shipped**
  (`PC_ENABLE_BLE_GATT`, `services/ble_gatt`): the Attribute Protocol PDUs under GATT (Bluetooth Core
  Vol 3 Part F) - build/parse read / write / notify / error with little-endian handles - plus a GATT
  characteristic-table JSON serializer for the web, pure + host-tested (`native_ble_gatt`). Remaining:
  wire it to the NimBLE / Bluedroid radio (scan + a GATT server) and an HCI-UART transport variant.
- [~] \*Radio channel sniff (L) - the RF gateways above in receive-only mode (sniff a
  LoRa / sub-GHz / 802.15.4 channel without joining) feeding the capture pipeline. **Capture framing
  shipped** (`PC_ENABLE_RADIO_SNIFF`, `services/radio_sniff`): wraps each received 802.15.4 MAC frame
  in the Wireshark IEEE 802.15.4 TAP pseudo-header (per-frame RSSI + channel TLVs, an exact int->float32
  RSSI encode) and a pcap record (new `PC_DLT_IEEE802_15_4_TAP` / `_NOFCS` link types in
  `shared_primitives/pcap.h`), so a sniffed channel opens directly in Wireshark; pure + host-tested
  (`native_radio_sniff`). Remaining: put the CC1101 / LoRa / Thread drivers into receive-only and route
  their frames through this into the existing forwarding sink on hardware.
- [~] \*EM / radar presence + motion (M) - mmWave radar (24 / 60 GHz: LD2410 / MR60BHA
  over UART, Infineon BGT60 over SPI) and Doppler motion (HB100 / RCWL-0516) for
  presence, motion, distance, and vital-sign (breathing / heart-rate) sensing. **LD2410 shipped**
  (`PC_ENABLE_LD2410`, `services/ld2410`): the framed 256000-baud report codec (`pc_ld2410_parse_report`
  basic + engineering mode, a byte-by-byte `Ld2410Stream` reassembler that resyncs on noise, presence /
  distance helpers) and the `FD FC FB FA` config-command encoders, pure + host-tested (`native_ld2410`),
  with an ESP32 UART binding (example wired). Remaining: an MR60BHA 60 GHz vital-sign variant, the
  Infineon BGT60 SPI radar, and the analog Doppler (HB100 / RCWL-0516) presence path.
- [~] \*Capacitive / proximity field sensing (S) - FDC2x14 / MPR121 (I2C): touch,
  proximity, liquid level, and material sensing from capacitance shifts. **Both drivers shipped**:
  MPR121 12-channel touch/proximity (`PC_ENABLE_MPR121`, `services/mpr121`) and the FDC2114/2214
  capacitance-to-digital field sensor (`PC_ENABLE_FDC2214`, `services/fdc2214`): the 28-bit data
  combine + error flags + frequency scale + single-channel config-sequence builder, pure codec
  host-tested (`native_fdc2214`), with an ESP32 I2C binding that verifies the device id, applies the
  config, and reads the channel. Remaining: verify capacitance shift on a real electrode / bench.
- [~] \*Inductive / EM field sensing (S) - LDC1614 (I2C) inductance-to-digital for
  metal / displacement, plus magnetometer-based EM-field perturbation detection. **Driver shipped**
  (`PC_ENABLE_LDC1614`, `services/ldc1614`): the LDC1614 28-bit data combine + error flags + frequency
  scale (`data/2^28 * fref`) + single-channel config-sequence builder, pure codec host-tested
  (`native_ldc1614`), with an ESP32 I2C binding that verifies the device id, applies the config, and reads
  the channel. Remaining: verify eddy-current shift on a real coil / bench, and the magnetometer EM path.
- [~] \*Time-of-flight ranging (S) - VL53L0X / VL53L1X (I2C) optical ToF distance and
  gesture, bridged to the same sink. **Driver shipped** (`PC_ENABLE_VL53L0X`, `services/vl53l0x`): the
  documented ranging registers - `pc_vl53l0x_range_mm` combines the range byte pair, `pc_vl53l0x_data_ready`
  decodes the interrupt-status byte, `pc_vl53l0x_range_valid` checks the device range-status field - pure
  codec host-tested (`native_vl53l0x`), with an ESP32 I2C binding that verifies the model id, starts
  continuous ranging, and reads the distance. Remaining: apply ST's tuning blob for best accuracy, a
  VL53L1X variant, and a bench range check.

### Protocols & integrations

- [~] Southbound protocol-driver framework _(framework shipped)_ - `PC_ENABLE_SOUTHBOUND`
  (`services/southbound`): the uniform seam every field-device driver plugs into so the app polls/drives
  any southbound device (Modbus slave, BACnet controller, raw SPI/I2C/UART sensor) through one facade -
  a bounded driver registry (`pc_southbound_register` / `_find` / `_count` / `_clear`) plus name-dispatched
  `pc_southbound_read` / `_write` / `_read_block` / `_write_block`, where the block calls are the atomic
  multi-point (register-matrix) path. Each driver owns its transport (a read/write vtable + ctx). Pure
  registry + dispatch, host-tested (`native_southbound`). **The Modbus adapter is shipped**
  (`services/net/southbound/sb_modbus`, `PC_ENABLE_SOUTHBOUND && PC_ENABLE_MODBUS_MASTER`): it binds the
  transport-agnostic Modbus TCP master codec into a `SouthboundDriver`, so an app reads register points
  by driver name through the one facade - `pc_sb_modbus_init` (holding FC 0x03 / input FC 0x04, unit,
  a rolling txid) + `pc_sb_modbus_driver` install the full vtable over an app-supplied request/response
  transaction seam (`pc_sb_modbus_txn`, bound to pc_client for Modbus TCP or a serial gateway): `read`
  (one register) and `read_block` (the atomic register matrix, a contiguous span up to 125 registers in
  one request), plus - for a holding-register driver - `write` (Write Single, FC 0x06) and `write_block`
  (Write Multiple, FC 0x10, up to 123 registers). An input-register driver is read-only (write /
  write_block unbound -> Sb::SB_ERR_UNSUPPORTED). A Modbus exception reply surfaces as
  `PC_SB_MODBUS_EXCEPTION` with the raw code captured, distinct from a propagated transport error. The
  matching **master write codec is shipped too** (`pc_modbus_build_write_single` / `_build_write_multiple`
  / `_parse_write_response`). Host-tested end to end against the real slave codec (`pc_modbus_process_adu`)
  over a loopback seam - `native_sb_modbus` (12 cases: single/block read + write round trips, input-register
  read-only, exception vs transport error, bounds, txid roll) and `native_modbus_master` (+6 write cases).
  **The bit-access function codes are shipped too**, completing the master's standard set: read coils
  (FC 0x01) and discrete inputs (FC 0x02) via `pc_modbus_build_read_bits` / `_parse_read_bits_response`
  (LSB-first bit unpacking, the requested count validated against the returned byte count), write single
  coil (FC 0x05, the 0xFF00/0x0000 value encoding) and write multiple coils (FC 0x0F, LSB-first bit
  packing), with `_parse_write_response` extended to the coil replies (which share the register-reply wire
  format). Host-tested as full master->slave round trips with read-back confirmation and a byte-exact
  packed-pattern check (`native_modbus_master` +5, now 24 cases). **The atomic register operations are
  shipped too** - Mask Write Register (FC 0x16, `pc_modbus_build_mask_write` + `_parse_mask_write_response`:
  set/clear individual bits via `(reg AND And_Mask) OR (Or_Mask AND NOT And_Mask)` without a
  read-modify-write race) and Read/Write Multiple Registers (FC 0x17,
  `pc_modbus_build_read_write_multiple`: a write span applied before a read span in one transaction) - both
  implemented on **both** the master and the slave codec (`pc_modbus_process_adu`) and host-tested end to
  end (`native_modbus_master` now 27; the round trip flagged a real off-by-one in the mask-write buffer
  guard, fixed). The master now speaks every standard Modbus function code (0x01-0x06, 0x0F, 0x10, 0x16,
  0x17). _Remaining:_ an example driving a real Modbus TCP server over `pc_client` (HW-verify).

### Networking / connectivity

- [~] Ethernet PHY bring-up (L, greenlit) - a wired-PHY init alongside WiFi; failover + the egress report
  above already work once both links exist. Multi-interface bridge / graceful escalation (L)
  _(escalation policy shipped)_ - `PC_ENABLE_LINK_MANAGER` (`services/link_manager`): a table of
  interfaces (kind + priority + up/down) with deterministic best-link-up selection, graceful escalation to
  a higher-priority interface when it comes up, failover to the next best when it drops, and change
  detection so the app reconfigures the netif only on a real transition. Pure, host-tested
  (`native_link_manager`). The PHY init is **HW-verified (2026-07-19)** on a Waveshare ESP32-P4-POE-ETH
  (onboard IP101 RMII PHY, arduino-esp32 3.x): the shipped `init_eth_physical()` brought the link up (100M
  full-duplex + DHCP) and the PC server answered real HTTP over pure wired Ethernet. _Remaining:_ the
  multi-interface bridge / packet forwarding (the v5 interface-forwarding milestone).
- [~] IPv6 dual-stack + fallback (L); VPN tunneling + reverse-SSH tunnel to a relay (L) _(dual-stack +
  fallback shipped + HW-verified 2026-07-19)_ - the address plumbing is `pc_ip` (RFC 4291 parse / RFC 5952
  format / scope classify / CIDR) and the netif bring-up is `PC_ENABLE_IPV6` (physical layer; listeners bind
  `IPADDR_TYPE_ANY`). HW-verified on an ESP32-S3: SLAAC formed link-local + ULA + a router-advertised global,
  and the dual-stack `:80` listener served real HTTP `GET`s over IPv6 (curled on-link over both the global and
  the ULA). The client-side fallback is `PC_ENABLE_HAPPY_EYEBALLS` (`services/happy_eyeballs`):
  `pc_he_pref` scores a destination (RFC 6724 scope + family), `pc_he_order` sorts a candidate list
  and interleaves the address families (RFC 8305) so successive attempts alternate v6/v4, and
  `pc_he_attempt_due` gates the next attempt by the Connection Attempt Delay - fast IPv6, quick IPv4
  fallback. Pure, host-tested (`native_happy_eyeballs`). The reverse-SSH tunnel to a relay SHIPPED
  (`PC_ENABLE_SSH_CLIENT`, `network_drivers/presentation/ssh/connection/ssh_client`): a full outbound
  SSH client - negotiates the whole modern suite (curve25519 / ecdh-p256 / dh-group14 / mlkem768x25519
  hybrid, ed25519 / ECDSA / RSA host keys pinned by SHA-256, chacha20-poly1305 / aes256-gcm / aes256-ctr)
  through a role-aware packet layer, authenticates by ed25519 publickey, holds a `tcpip-forward`, and
  bridges forwarded-tcpip channels (a pool, `PC_SSH_CLIENT_MAX_CHANNELS`) back to a local service.
  HW-verified against OpenSSH 10.0 on an ESP32-S3 (handshake + auth + forward + the `:80` bridge,
  concurrent and rapid-sequential, byte-exact responses). _Remaining:_ VPN tunneling (the IKEv2 item below).
- [~] **IPsec / IKEv2** (XL, the concrete VPN for the item above - **secure machine bridge over untrusted
  networks**) _(tier 1 codec shipped)_ - IKEv2 (RFC 7296) over UDP 500 / 4500 (NAT-T) plus the ESP datapath
  (RFC 4303). Splits into three tractability tiers, built in order:
    1. **IKEv2 message + payload codec** (M) - SHIPPED (`services/ikev2`, `PC_ENABLE_IKEV2`, host-tested
       `native_ikev2`; byte-exact vs scapy). The 28-octet
       header (initiator/responder SPIs, next-payload, MjVer/MnVer, exchange type, flags, message id, length)
       and the generic payload chain (next-payload + critical + length) for the payloads: **SA** (proposals ->
       transforms: ENCR / PRF / INTEG / D-H / ESN, with the key-length attribute), **KE**, **Ni/Nr** nonce,
       **AUTH**, **IDi/IDr**, **CERT** / **CERTREQ**, **TSi/TSr** traffic selectors, **N** notify, **D** delete,
       and **SK** encrypted-payload framing. Pure
       build/parse into caller buffers, host-tested with RFC vectors. **Reuses crypto we already ship**:
       `ssh_curve25519` (D-H group 31 = X25519, and MODP groups via `ssh_bignum`), `ssh_chachapoly`
       (ChaCha20-Poly1305 per RFC 7634), `pc_sha256/512` (PRF/INTEG HMAC), AES-GCM (the `pc_quic_aead` core) -
       so the primitive surface is largely done; this is mostly framing + the key-derivation (SKEYSEED /
       the SK_* chain). **The key derivation is now shipped** (RFC 7296 §2.13-2.14): `pc_ike_prf_plus`
       (prf+ over HMAC-SHA2-256, capped at 255 blocks) and `pc_ike_derive_keys` (SKEYSEED = prf(Ni|Nr,
       g^ir), then the SK_d/ai/ar/ei/er/pi/pr split by caller-supplied per-algorithm lengths), zero-heap,
       cross-checked against an independent Python HMAC reference incl. the >64-byte-key RFC 2104
       pre-hash and an AES-GCM (key+salt) split length (`test_ikev2`, +5 KAT/guard cases). **The
       SK-payload AEAD is shipped too** (RFC 5282 AES-256-GCM-16): `pc_ike_sk_aead_seal` /
       `pc_ike_sk_aead_open` wrap the library's AES-256-GCM under the RFC 5282 nonce (4-byte salt from
       SK_ei/er || the 8-byte explicit IV), authenticating the header-through-SK-generic-header AAD and
       producing/consuming the `IV | ciphertext | ICV` body, cross-checked against Python cryptography's
       AES-256-GCM incl. tamper (tag + AAD) rejection and in-place operation (+3 cases). **The
       Diffie-Hellman shared-secret step is shipped too** (RFC 7296 §2.7): `pc_ike_dh_public`
       (our KE value) and `pc_ike_dh_compute` (g^ir, which feeds SKEYSEED), group 31 = curve25519
       (RFC 7748 X25519) over the library's `ssh_x25519`, conformance-tested against RFC 7748's own
       §5.2 raw-X25519 and §6.1 Alice/Bob ECDH-agreement vectors (+3 cases; groups 19 P-256 / 14
       MODP-2048 are a later increment). **Tier-2 crypto is now complete** (key schedule + SK AEAD +
       D-H); the remaining tier-2 work is the IKE_SA_INIT -> IKE_AUTH state machine (item 2).
       `test_ikev2` is now 50 cases.
    2. **IKE SA handshake state machine** (L) - IKE_SA_INIT -> IKE_AUTH (PSK and/or certificate auth) ->
       CREATE_CHILD_SA / INFORMATIONAL, rekeying, DPD (dead-peer detection), and the fixed-BSS SA/SPI tables.
       Still host-testable against a reference peer (strongSwan / libreswan) with no datapath. **The
       PSK authentication computation is shipped** (RFC 7296 §2.15): `pc_ike_auth_psk` computes
       AUTH = prf(prf(PSK, "Key Pad for IKEv2"), RealMessage | Nonce | prf(SK_p, RestOfIDPayload)) over
       the streamed HMAC-SHA2-256 (RealMessage never re-buffered), cross-checked against an independent
       Python HMAC reference incl. wrong-key divergence. **The IKE_SA_INIT message assembly is shipped
       too** (RFC 7296 §1.2): `pc_ike_sa_init_build` composes the tier-1 codec into a whole
       HDR | SA | KE | Nonce message with the Next-Payload chain + header Length set correctly, and
       `pc_ike_sa_init_parse` reads it back into an `IkeSaInitMsg` (SPIs, first proposal, KE group +
       data, nonce) - the raw chain bytes are asserted against the RFC values and every field round-trips.
       **The IKE_AUTH encrypted-message assembly is shipped too** (RFC 7296 §3.14 / RFC 5282):
       `pc_ike_auth_msg_build` wraps a caller-built inner chain (IDi | AUTH | SAi2 | TSi | TSr) in the SK
       envelope - HDR | SK{ IV | AES-256-GCM(inner | Pad Length) | ICV } with the RFC 5282 salt||IV nonce
       and the AAD = header-through-SK-generic-header - and `pc_ike_auth_msg_open` verifies the ICV in
       constant time, decrypts in place, and strips the pad trailer, round-tripping an IDi|AUTH chain
       byte-exact with ciphertext + AAD tamper both rejected. **The handshake's message layer is
       complete.** **ECDSA-P256 certificate auth is shipped too** (RFC 7296 §2.15 / RFC 7427):
       `pc_ike_signed_octets` assembles RealMessage | Nonce | prf(SK_p, id) into caller scratch, and
       `pc_ike_auth_sign_ecdsa_p256` / `_verify_ecdsa_p256` sign / verify those octets with the library's
       ECDSA-P256-SHA256 - the octet assembly is cross-checked against a Python HMAC reference and the
       sign->verify round-trip rejects a tampered nonce and a wrong key. **The IKE SA context + key
       derivation from a completed exchange is shipped** (RFC 7296 §2.14/§2.17): an `IkeSa` holds the
       SPIs, role, negotiated `IkeSuite`, and `IkeKeyMaterial`; `pc_ike_suite_keylengths` maps the
       negotiated transforms to the SK_* lengths (AEAD -> sk_a = 0 + a 4-byte GCM salt in sk_e) and
       `pc_ike_sa_keys_from_init` computes g^ir and runs the key schedule - verified as a mutual
       agreement (initiator + responder derive identical SK_* from the RFC 7748 Alice/Bob key pair) and
       cross-checked against a Python reference. **The initiator IKE_SA_INIT handshake driver is shipped
       too** (RFC 7296 §1.2): an `IkeHandshake` state machine (`IkeState`) where `pc_ike_initiator_start`
       emits the IKE_SA_INIT request from caller-supplied ephemeral material (SPI, X25519 key pair, nonce

### Storage & config

- [~] PSRAM web buffers / zero-copy net buffers (`heap_caps_calloc(MALLOC_CAP_SPIRAM)` at begin) + asset
  offloading + COMPONENT_EMBED_TXTFILES (M); SPI DMA ping-pong buffers (M) _(placement policy shipped)_ -
  `PC_ENABLE_PSRAM_POOL` (`services/psram_pool`): `pc_psram_place` picks DRAM vs PSRAM for a buffer
  by size, DMA requirement, and free-heap headroom (large/cold to PSRAM, small/hot + DMA to DRAM, always
  leaving an internal-DRAM reserve), and `pc_pingpong_*` keeps the SPI DMA double-buffer bookkeeping
  (CPU fills one buffer while DMA drains the other; swap flips their roles). Pure, host-tested
  (`native_psram_pool`). _Remaining:_ the `heap_caps_calloc` allocation glue at begin + the
  COMPONENT_EMBED_TXTFILES asset-offload build wiring (M).

### Observability, diagnostics & reliability

- [~] Hardware health (M): power-rail voltage-drop logger, SPI-bus CRC audit + clock backoff, GPIO
  short-circuit test, capacitor-leakage diag _(decision cores shipped)_ - `PC_ENABLE_HW_HEALTH`
  (`services/hw_health`): `pc_hwhealth_rail_sample` tracks a rail's worst droop + sag/brownout counts
  (with a `/health` JSON serializer), `pc_hwhealth_spi_result` is a hysteretic clock-backoff state
  machine (halve on a CRC-fail streak, step up on an ok streak, clamped to a floor/ceiling),
  `pc_hwhealth_gpio_short` classifies a driven-vs-readback mismatch as a short to ground / Vcc, and
  `pc_hwhealth_cap_leak` compares a measured RC decay to expected (too fast = leak, too slow = high
  ESR). Pure, host-tested (`native_hw_health`). _Remaining:_ the ADC / SPI / GPIO sampling glue that
  feeds them on real hardware (M).

### Build / tooling

- [~] Hierarchical build-flag tree (M); virtual protocol-mocking toggles (M) _(tree shipped)_ - the
  `ci_tooling/generate/gen_configurator.py` generator parses `src/protocore_config.h` and builds the flags
  as a hierarchy grouped under the file's section titles, with the hard dependency edges (`#if child &&
!parent` guards) encoded - the build-flag tree, kept from drifting by the `check` CI gate. _Remaining:_
  the virtual protocol-mocking toggles (swap a real driver for a mock transport at build time).
- [~] **Real-protocol interop test harness** (M, per protocol) - _harness shipped_ in
  [test/servers/](../test/servers/): one CLI,
  `python test/servers/interop.py <protocol>`, drives the device against the _real_
  reference implementation, not just our own round-trip. Peers so far: HTTP (stdlib
  client), WebSocket (`websockets`), SNMP (`net-snmp`, `pysnmp` fallback), Modbus
  client + server (`pymodbus`), CoAP (`aiocoap`), MQTT broker (`mosquitto` + `paho`),
  OPC UA client + server (`asyncua`). Each peer says whether the device is the server
  (the harness probes it, `--host ...`) or the client (the harness serves a reference
  peer the device connects to), reports uniform PASS/FAIL, and exits 0/1/2.
  HW-verified against the board on real third-party stacks: HTTP 4/4, WebSocket 3/3,
  CoAP 2/2, Modbus 6/6, SNMP 3/3, MQTT (device client -> real `mosquitto`), and OPC UA
  (`asyncua`) 3/3 - all seven protocol families. Adding a protocol is one module in
  `peers/` (documented in its README). Remaining: wiring it into CI containers, and a
  peer per new protocol as it lands.
- [~] **Server build configurator (CLI + GUI in `configurator/`)** (L) _(GUI + source-of-truth shipped)_ - a guided front end for the ~200 `PC_ENABLE_*` and sizing flags so a user assembles a firmware build without hand-editing `build_flags`. Shipped as `ci_tooling/generate/gen_configurator.py` -> `docs/configurator.html`: it parses [protocore_config.h](../src/protocore_config.h) (the single source of truth, so it never drifts - a `check` CI gate fails on staleness) for every feature flag + tuning knob + section group + the hard `#if child && !parent` dependencies, and emits one self-contained page that ticks features, tunes knobs, resolves dependencies (mutual-exclusion), and copies out a `platformio.ini` `build_flags` block or a `#define` set (only the values that differ from the defaults). Beginner-friendly, ships to Pages. _Remaining:_ a live per-option build-footprint estimate (flash + RAM from the FEATURES tables), advisory "this is unwise, but here you go" guardrails, and a standalone CLI backend (the emission is client-side today).

### Protocol & transport versions

- [~] **TLS 1.2** (L, RFC 5246) - explicit TLS 1.2 record/handshake support with a
  pinned, audited cipher suite set, session resumption, and the static-pool
  mbedTLS integration; make the negotiated version observable and configurable.
  The record + handshake run in `network_drivers/tls` (mbedTLS, floored at TLS 1.2). The version +
  cipher **policy** shipped as `PC_ENABLE_TLS_POLICY` (`services/tls_policy`):
  `pc_tls_negotiate_version` picks the version server-style, `pc_tls_version_name` makes it
  observable, `pc_tls_select_cipher` pins the suite allowlist by server preference, and
  `pc_tls_is_aead` classifies suites - pure + host-tested (`native_tls_policy`). _Remaining:_ wire the
  policy into the mbedTLS conf (cert/cipher list) + session-resumption tickets.
- [~] **TLS 1.3** (L, RFC 8446) - TLS 1.3 handshake (1-RTT, optional 0-RTT early
  data with replay safeguards), modern AEAD-only suites, after TLS 1.2 lands. TLS 1.3 is already
  negotiated by the mbedTLS transport (no max-version cap), and the AEAD-only cipher policy for the
  hardened profile is the shared `services/tls_policy` (`pc_tls_is_aead` + the 1.3 suites 0x1301-0x1303
  in the allowlist). _Remaining:_ optional 0-RTT early data with replay safeguards, and exposing a
  1.3-only profile toggle.

### Low-level networking (raw Layer 2)

- [~] **Raw L2 frame TX/RX** (M, platform enabler) _(Ethernet frame codec shipped)_ -
  `PC_ENABLE_RAWL2` (`services/rawl2`): the host-testable core - build/parse Ethernet II + 802.1Q
  VLAN frames and the 802.3 FCS (CRC-32, check value 0xCBF43926); host-tested (`native_rawl2`). The
  raw-frame API the raw-L2 protocols build on. _Remaining (device transport):_

### Industrial / standards protocols - delivered

- [~] **NTCIP** (L, US ITS) _(object OIDs shipped)_ - `PC_ENABLE_NTCIP` (`services/ntcip`): the NTCIP
  object definitions on top of the shipped SNMP agent ([snmp](../src/services/net/snmp/)) - the **1202**
  (Actuated Signal Controller: maxPhases, phaseMinimumGreen, live phaseStatusGroupGreens) and **1203**
  (Dynamic Message Sign: dmsMaxMultiStringLength, dmsMessageMultiString) object roots under
  `1.3.6.1.4.1.1206.4.2`, plus `pc_ntcip_oid()` to build a full object OID (root + instance index)
  to register with `pc_snmp_agent_add_*`; host-tested (`native_ntcip`). _Remaining:_ the fuller 1202/1203
  object set + **1211** (Signal Control and Prioritization). No heap.
- [~] **UTMC** (L, UK/EU ITS) _(common-database codec shipped)_ - `PC_ENABLE_UTMC` (`services/utmc`):
  the UTMC common-database HTTP+XML message set - `pc_utmc_request` (a UTMCRequest for an object
  id), `pc_utmc_response` (a UTMCResponse carrying the object value + a data-quality flag +
  timestamp), and `pc_utmc_parse_request` (extract the requested id) - over the existing HTTP
  server, XML-escaped; host-tested (`native_utmc`). _Remaining:_ the fuller object model + the
  DATEX-II profile. Fixed BSS, no heap.
- [~] **OCIT** (L, DE/AT/CH ITS) _(message codec shipped)_ - `PC_ENABLE_OCIT` (`services/ocit`): the
  OCIT-Outstations object message - `pc_ocit_build` / `_parse` for
  `[msg-type][object-type][instance][data-type][value]` (get / set / report of a field object) with
  the typed values (bool / byte / u16 / u32 / octets) and typed-value accessors - between central
  traffic computers and field controllers / detectors; host-tested (`native_ocit`). _Remaining:_ the
  fuller object dictionary + the OCIT-O BTPPL transport profile. Fixed BSS device/detector model, no
  heap.
- [~] **V2X / SAE J2735** (XL, connected vehicle) _(UPER codec + BSMcore + SPaT shipped)_ -
  `PC_ENABLE_J2735` (`services/j2735`): the ASN.1 **UPER** (Unaligned Packed Encoding Rules) bit-level
  primitive codec - constrained INTEGER (offset in `ceil(log2(range))` bits), BOOLEAN, raw bit fields, a
  bit writer/reader - and the three core V2X messages encode + decode on top: **BSMcore** (the safety
  kernel: msgCnt / id / secMark / lat / long / elev / speed / heading), **SPaT** (the MovementState list:
  per-signal-group phase + min/max end-time countdown), and **MAP** (the intersection geometry: id +
  reference point + per-lane id / ingress flag / node XY offsets). Host-tested against hand-computed bit
  patterns (`native_j2735`; the 162-bit BSMcore packs to 21 octets). _Remaining:_ the full BSM part II
  optionals + the 1609.2 security envelope. The DSRC / C-V2X radio is an external module; the message
  layer is the deliverable.
- [~] **IEEE 1609 (WAVE)** (XL, vehicular radio stack) _(WSMP + 1609.2 envelope codec shipped)_ -
  `PC_ENABLE_WAVE` (`services/wave`): the 1609.3 **WSMP** (WAVE Short Message Protocol) header -
  `pc_wsmp_build` / `_parse` (version + a P-encoded **PSID** + length + payload) with the PSID
  variable-length p-encoding (`pc_wave_encode_psid` / `_decode_psid`) - and the **1609.2**
  secured-message envelope header (`pc_wave_1609dot2_wrap`: protocolVersion + contentType); these
  carry the shipped J2735 messages. Host-tested (`native_wave`). _Remaining:_ the full 1609.2
  signature / certificate machinery (the crypto layer) + the WSA service advertisement. The DSRC /
  C-V2X radio is an external module; no heap.
- [~] **NEMA TS 2** (M, traffic cabinet bus) _(SDLC frame codec shipped)_ - `PC_ENABLE_NEMA_TS2`
  (`services/nema_ts2`): the traffic-cabinet SDLC bus frame - `pc_nema_ts2_build` / `_parse` for
  `[address][control][frame-type][data][CRC-16/X-25]` linking the controller to the MMU / BIUs /
  detector racks, with the common frame-type constants; host-tested (`native_nema_ts2`, CRC check
  value 0x906E). _Remaining:_ the per-frame-type cabinet object model + the synchronous serial PHY
  / BIU timing (hardware-gated).
- [~] **ATC** (S, platform note) _(interop shipped)_ - the Advanced Traffic Controller
  spec moves cabinets from closed microcontrollers to a standard Linux engine API (the
  ITS Cabinet / ATC API for field-device I/O) for local video analytics + sensor fusion.
  This is a host-platform specification more than a wire protocol; the relevant slice for
  this library is interop - exposing NTCIP / NEMA-TS2 data to an ATC engine over the
  existing HTTP/SNMP surface rather than implementing the Linux ATC stack itself.
  `PC_ENABLE_ATC` (`services/atc`): the field-I/O map serialized as
  `{"inputs":[...],"outputs":[...]}` JSON for an ATC engine over HTTP, plus the output
  setter/getter. _Remaining:_ the Linux ATC engine + video analytics stack (out of scope).
- [~] **LwM2M** (L, OMA LwM2M) - _TLV content format shipped._ `PC_ENABLE_LWM2M`
  (`services\lwm2m`): a zero-heap writer + cursor reader for the OMA TLV
  (`application/vnd.oma.lwm2m+tlv`) resource encoding - `pc_lwm2m_tlv_write` (+ typed
  `_write_int` shortest-form / `_write_bool` / `_write_string` / `_write_float` helpers),
  `pc_lwm2m_tlv_read`, and `pc_lwm2m_tlv_value_int`, handling 8-/16-bit ids and inline / 8- /
  16- / 24-bit lengths and the Object-Instance / Resource / Multiple-Resource /
  Resource-Instance kinds; type-byte layout verified against the spec, host-tested. Built
  on the shipped CoAP service ([coap](../src/services/iot/coap/)). Remaining: the client
  interfaces (Bootstrap, Registration, Device Management, Information Reporting / Observe)
  and the standard object model (Security/0, Server/1, Device/3, Firmware Update/5, ...)
  on a fixed BSS model. The **SenML-CBOR / SenML-JSON** content formats are now shipped
  separately - `PC_ENABLE_SENML` (`services\senml`): `pc_senml_json_build` /
  `pc_senml_cbor_build` emit a SenML (RFC 8428) pack (base name/time, name, unit, one value,
  time per record) over the JSON / CBOR writers, integral numbers kept as integers; verified
  against the RFC example, host-tested. DTLS is gated on the TLS work; scope the NoSec +
  registration/observe core first. No heap, one flag.
- [~] **gRPC / Protocol Buffers** (L) - _Protobuf wire codec shipped._
  `PC_ENABLE_PROTOBUF` (`services\protobuf`): a zero-heap streaming writer
  (`pc_pb_uint64` / `pc_pb_sint64` / `pc_pb_fixed32` / `pc_pb_fixed64` / `pc_pb_float` / `pc_pb_double` /
  `pc_pb_bytes` / `pc_pb_string`, embedded messages via a sub-buffer + `pc_pb_bytes`) and a
  cursor reader (`pc_pb_read_field` over varint / ZigZag / I32 / I64 / length-delimited),
  host-tested against the spec vectors (`08 96 01`, the `"testing"` string, ZigZag
  mapping). **gRPC-Web framing also shipped** - `PC_ENABLE_GRPC_WEB`
  (`services\grpcweb`): the 5-octet `[flags][len BE32]` message frame
  (`pc_grpcweb_frame_message`), the 0x80 trailers frame (`pc_grpcweb_frame_trailer`,
  `grpc-status` / `grpc-message`), and `pc_grpcweb_parse`, wrapping the Protobuf codec over the
  shipped HTTP/1.1 server/client (host-tested). Full **gRPC** (the same framing but over
  **HTTP/2** with `application/grpc`) remains gated on the HTTP/2 roadmap item above. Fixed
  BSS, no heap.
- [~] **DDS** (XL, OMG DDS) _(RTPS framing shipped)_ - `PC_ENABLE_DDS` (`services/dds`): the RTPS
  (DDSI-RTPS) message + submessage framing codec - the 20-octet header (magic / version / vendor /
  guidPrefix) and the typed submessages (INFO_TS, DATA, HEARTBEAT, ACKNACK, ...) with the endianness
  flag, built by `pc_rtps_header` / `_submessage` and walked by `pc_rtps_parse`; host-tested
  (`native_dds`). _Remaining:_ the CDR serialized-payload encoding, SPDP/SEDP discovery, and the
  reliability/heartbeat reader-writer protocol + QoS subset (all zero-heap BSS); **DDS-XRCE** (the
  resource-constrained agent/client profile) is the more MCU-appropriate full target.
- [~] **BACnet/IP & BACnet/SC** (L, ASHRAE 135) - _the BVLC + NPDU framing is shipped._
  `PC_ENABLE_BACNET` (`services\bacnet`): `pc_bvlc_build` / `pc_bvlc_parse` (the BACnet/IP
  virtual-link envelope - type 0x81, function, length) and `pc_npdu_build` / `pc_npdu_parse`
  (the network layer - version + NPCI control + optional DNET/DADR destination addressing + hop count, slicing the APDU); layout per ASHRAE 135 Annex J / Clause 6, host-tested.
  Remaining: the **APDU** application layer (the object model - Device / Analog-Input /
  Binary-Output / ... objects, properties, ReadProperty / WriteProperty / COV) and the
  BBMD foreign-device registration; then **BACnet/SC** (Secure Connect) reuses the shipped
  WebSocket + static-pool TLS for its BVLC-SC framing + the same APDU/object model. Fixed
  BSS object database, no heap.
- [~] **XMPP (IoT profile)** (L, XSF) _(stanza codec shipped)_ - `PC_ENABLE_XMPP` (`services/xmpp`):
  the RFC 6120 stanza codec - correctly XML-escaped `<stream:stream>` / `<message>` / `<presence>` /
  `<iq>` builders and the stanza-name + attribute readers; host-tested against exact-output vectors
  (`native_xmpp`). _Remaining:_ the streaming XML parser + the SASL/TLS handshake (TLS reuses the
  shipped client TLS) and the IoT XEPs (0030 disco, 0060 pub/sub, 0323 sensor data, 0325 control) on
  a fixed BSS roster/node model.
- [~] **NoSQL / database clients** (M-L, candidate) - _Redis RESP codec shipped._
  `PC_ENABLE_REDIS` (`services/redis_resp`): a zero-heap `pc_resp_encode_command()`
  (array of bulk strings, binary-safe via explicit arg lengths - drives any command
  incl. SET/GET/HSET/XADD) + a cursor `pc_resp_parse()` reply decoder (simple / error /
  integer / bulk / array / nil; incomplete + malformed fail closed). Host-tested
  (`test_redis_resp`, 8 cases). Drive it over the shipped outbound client transport.
  Heavier candidates (MongoDB wire protocol, Postgres frontend/backend protocol) are
  larger and lower-priority. Fixed BSS, no heap, one flag per backend.
- [~] **DShot** (M, RMT) _(codec shipped)_ - `PC_ENABLE_DSHOT` (`services/dshot`): the DShot frame
  codec - `pc_dshot_encode` / `_decode` build + validate the 16-bit packet (11-bit throttle /
  command + telemetry-request + 4-bit nibble-xor CRC), with the special-command constants and the
  per-rate (150/300/600/1200) bit high-times for an RMT driver. Host-tested against hand-computed
  vectors (`native_dshot`). _Remaining:_ the RMT pulse-train transport (the HW timing backend) and
  the HTTP/WS/dashboard throttle+arming control surface.
- [~] **Bidirectional / Extended DShot (EDShot)** (M, RMT) _(outbound frame shipped)_ - the
  bidirectional inverted-CRC frame is built by `pc_dshot_encode(..., bidirectional=true)` (and
  validated by `_decode`). _Remaining:_ decoding the ESC's GCR-encoded return frame (eRPM /
  temperature / voltage / current) off the same wire, so the server can stream live ESC telemetry
  (pairs with the telemetry-math + dashboard services).
- [~] **ProShot** (S, RMT) _(shares the DShot packet)_ - ProShot carries the same 16-bit DShot value
  (built by `pc_dshot_encode`, shipped) as four pulse-position symbols. _Remaining:_ the
  nibble->pulse-position encoding + the RMT emission (the packet + CRC are done).
- [~] **OneShot / Multishot** (S, MCPWM) _(pulse-width mapping shipped)_ - `pc_esc_pwm_ns()`
  (`services/dshot`) maps a 0..1000 throttle to the pulse high-time for standard PWM (1-2 ms),
  **OneShot125** (125-250 us), **OneShot42** (42-84 us), and **Multishot** (5-25 us); host-tested
  (`native_dshot`). _Remaining:_ driving it from the MCPWM peripheral synced to the control loop.
- [~] **NTS-KE (Key Establishment)** (L) _(record codec shipped)_ - `PC_ENABLE_NTS`
  (`services/nts`): the NTS-KE TLV record codec - `pc_nts_ke_request` builds the standard client
  request (Next Protocol NTPv4 + AEAD AES-SIV-CMAC-256 + End-of-Message, all critical) and
  `pc_nts_ke_parse` walks a response, surfacing each record (cookies, negotiated AEAD, server,
  port, error) via a callback; host-tested (`native_nts`). _Remaining:_ running it over the static-pool
  mbedTLS client (ALPN `ntske/1`) + the RFC 5705 exporter key derivation (the label is exposed).
- [~] **NTS-protected NTP** (L) _(extension-field framing shipped)_ - `services/nts` also builds the NTS
  NTP extension fields (Unique Identifier, NTS Cookie, and the RFC 7822 4-byte-padded framing);
  host-tested. _Remaining:_ the `AEAD_AES_SIV_CMAC_256` (RFC 5297) authenticator protect/verify + the
  anti-replay unique-id echo + cookie rotation, then feed the validated offset to `pc_clock`.

## Last - do these after everything above

- [ ] **Pass the reference down instead of re-deriving it** (M) - `protocore` already holds the object
      a call is about, so it passes that reference down through its own functions rather than having
      each one look the object up again. Where the library only aliases a vendor call, the alias stays
      a pass-through: nothing is copied, validated or wrapped on the way past. The reference is only
      spent when the object reaches logic that actually reads it, so a path that just hands bytes to
      the vendor costs the same as calling the vendor directly. Do this after the C11 conversion is
      finished, because it touches call signatures across every layer and a half-converted tree would
      make it two passes.

## Decision records

Settled questions, kept so they are not re-litigated.

**Decided 2026-07-29. The name is `ProtoCore` and the rename is done.** The library was previously
`DeterministicESPAsyncWebServer` - literal, but a mouthful, and its "ESP" stopped being true the moment the
multi-vendor work started. `ProtoCore` is accurate rather than merely shorter: what sits in the middle is
protocol logic and nothing else, with silicon pushed out to `board_drivers/` and platform shells out to
`examples/`.

Shipped alongside it: the house prefix is `pc_` / `PC_` / `PROTOCORE`; the namespace question was settled in
favor of one flat naming law ([SYMBOLS.md](SYMBOLS.md)); and every pre-ProtoCore token is gone from the tree.

The remainder of this section is kept as the **decision record** - the criteria applied and the candidates
rejected. It is history, not an open action.

**Candidates considered, not chosen:**

- **Keystone** - the wedge stone that locks an arch; the one piece everything else bears on. Evokes structural
  reliability and "the dependable core of the system". Strong, memorable, embedded-agnostic.
- **Coherence** - consistency / everything staying in lockstep; doubles as a signals/physics term (phase
  coherence) that lands well for an embedded + networking library. Reads as "predictable, in-phase, no drift".

**Further candidates (short, on-theme - determinism / timing / solidity / structure):**

- **Cadence** - deterministic rhythm; nods to the pluggable monotonic clock and bounded, in-time behavior.
- **Datum** - a fixed reference point (surveying/engineering) _and_ "data" - a deterministic baseline you build on.
- **Quartz** - the crystal that keeps deterministic time; precise, embedded, unglamorous-in-a-good-way.
- **Latch** - digital-logic latch: holds a defined state, deterministic, tiny. Very embedded.
- **Invariant** - a property that always holds (CS/formal-methods term for exactly the guarantee we make).
- **Bedrock** / **Cornerstone** - the dependable foundation (same idea as Keystone, if that one is taken).
- **Fulcrum** - the fixed point that gives leverage; short, structural, memorable.
- **Rivet** - fixed, embedded, solid; joins things permanently.

Selection criteria when picking: short + easy to say/type; not colliding with a well-known language/framework
or an active PlatformIO/Arduino library; a clean namespace/prefix; and a spare `.io`/`.dev` domain + a free
GitHub org/repo. Decide alongside the STM32 backend landing so the name changes exactly when "ESP" stops being
literally true, not before.
