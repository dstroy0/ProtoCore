# Bug log

A running record of every bug found in this library: what broke, the root cause,
the fix, and status. Newest first. A bug is logged here the moment it is found
(even before it is fixed) so nothing slips.

Status key: **OPEN** (found, not fixed) - **FIXED** (fixed, validated) - **SHIPPED** (released).

---

## 51 helpers became global symbols when the C conversion deleted their anonymous namespace

- **Status:** FIXED (2026-08-02), pending a target build. Found by diffing `src/` against v0.0.1
  after `check_owned_context` reported four contexts with external linkage.
- **Symptom:** no diagnostic anywhere. Unprefixed names - `resolve`, `service`, `intern`, `peek`,
  `new_node`, `find_port`, `teardown`, `do_dns` - sat at global scope, and two of them collided
  outright: `s_ctx` was defined non-static in both `relay_listener.c` (`RelayListenerCtx`) and
  `iface_bridge_hw.c` (`BridgeGlueCtx`). Two external definitions of one name with different types
  is a duplicate symbol; a C tentative definition can also merge them, giving two unrelated
  subsystems one object sized to the larger and letting each scribble on the other's state.
- **Root cause:** v0.0.1 wrapped these in `namespace { ... }`, which is internal linkage. C has no
  anonymous namespace, so the conversion deleted the wrapper. Where it did not substitute `static`,
  the guarantee left with the construct. 156 files in 0.0.1 had an anonymous namespace; 8 still
  carried a symbol that lost its linkage, 51 symbols in total. `graphql.c`, `relay_listener.c` and
  `iface_bridge_hw.c` lost every symbol theirs held.
- **Why nothing caught it:** `check_owned_context` globbed `*.cpp` only, so after the conversion it
  read 51 of 351 implementation files and never saw any of these. Even once widened it reports the
  three contexts and not the 48 functions, because it checks file-scope mutables rather than
  linkage in general. `check_duplicate_symbols` misses `s_ctx` because it only reports a duplicate
  that some header also declares `extern`, and neither does. Nothing else reads linkage at all.
- **Fix:** `static` on all 51, verified against the diff as exactly 51 lines each unchanged except
  for a prepended `static`. This is the same defect a previous session fixed by hand in
  `crypto/pqc/sntrup761.c` using `nm` on the object file, which is the authoritative check and the
  one to repeat once the tree compiles.
- **Not affected:** the public API. `pc_gql_arg_*`, `pc_espnow_*` and their kind were declared
  outside the namespace in 0.0.1 and are correctly external.
- **Separately:** `check_owned_context` matched only the C++ spelling `thread_local`, so
  `static _Thread_local int t_worker_id` in `mmgr/arena.c` read as a loose global. The C11 spelling
  is now matched too; the variable was always correct.

## The theme-blob generator kept emitting C++, so CI resurrects a dead `binary_asset_blobs.cpp` on every run

- **Status:** OPEN (2026-08-02). Found by widening `check_duplicate_symbols` to `.c`, which it had
  stopped reading.
- **Symptom:** `PC_THEME_BLOBS` and `PC_THEME_BLOB_COUNT` are each defined in two tracked files,
  `binary_asset_blobs.c` and `binary_asset_blobs.cpp`. Any build that compiles the whole tree -
  Arduino, and the ESP-IDF component, whose `CMakeLists.txt` globs `src/*.c` and `src/*.cpp` -
  gets two definitions of both and fails to link. The native envs name their sources explicitly
  and reference neither file, so nothing in the test matrix sees it.
- **Root cause:** `web_assets/wizard/gen_theme_blobs.py` writes `BASENAME + ".cpp"` and renders
  `return nullptr;` with unbraced bodies. The C conversion produced the `.c` by hand-editing that
  generated output (`nullptr` -> `NULL`, braces added) and left the `.cpp` tracked. The generator
  was never converted with the tree, so `feature-tables.yml` re-runs it and `git add`s
  `binary_asset_blobs.cpp` explicitly - deleting the file by hand is undone by the next CI run.
  The `.cpp` is byte-identical to what the generator emits today, which is what identifies it as
  the live output and the `.c` as the hand-edited copy.
- **Why nothing caught it:** `check_duplicate_symbols` globbed `*.cpp` only, so after the
  conversion it compared 51 files out of 351 and could not see a `.c`/`.cpp` pair. It reported OK
  throughout. No native env compiles either file, and no target build runs in CI.
- **Fix:** the generator emits `.c` and emits C (`NULL`, braced bodies); `--check` now reproduces
  the committed `.c` byte for byte. `feature-tables.yml` and `.clang-format-ignore` follow the
  rename. The dead `binary_asset_blobs.cpp` still has to be deleted - pending, because deleting it
  before the generator fix landed would only have brought it back.

## `webdav.c` and `webdav.cpp` are two different modules sharing one name in one directory

- **Status:** OPEN (2026-08-02). Found alongside the `binary_asset_blobs` pair.
- **Symptom:** `src/network_drivers/application/webdav/` holds both `webdav.c` (the
  filesystem-backed half: PROPFIND/PUT/COPY/MOVE over a mounted subtree) and `webdav.cpp` (the
  pure core: method classification, header parsing, the 207 Multi-Status XML builder). They define
  different symbols, so this does not break the link, and both are live - the test matrix builds
  `webdav.c` in five envs and `webdav.cpp` in three, and one env builds both.
- **Root cause:** `bddf3f4a3` ("move each module under the layer that owns it") moved the
  filesystem half into the directory that already held the pure core. `webdav.c`'s own file
  comment still says the pure core "lives in network_drivers/application/webdav/", which is now
  the directory it is sitting in, so the comment reads as self-referential.
- **Why nothing caught it:** nothing checks for a basename collision across extensions, and the
  two files carry no duplicate symbol for `check_duplicate_symbols` to catch. Both compile.
- **Fix:** not yet decided. One of the two needs a name that says which half it is.

## `check_docs` stopped reading `src/*.c`, so 78 flags and 63 functions became invisible to it

- **Status:** FIXED (2026-08-02). Found while clearing the checker's one live finding, the stale
  `PC_DIAG_JSON` citation.
- **Symptom:** none yet. The next doc to cite a symbol that lives only in a `.c` file gets reported
  as a stale citation and fails CI, with the symbol sitting in the tree the whole time.
- **Root cause:** the checker builds its authority set - "does this symbol exist" - from
  `git ls-files src/*.h src/*.cpp test/*.h test/*.cpp penetration_testing/*.h penetration_testing/*.cpp`. That list
  was written when every implementation file was a `.cpp`. The C conversion left 576 `.c` files
  outside it, and with them 78 `PC_*` names and 63 `pc_*` functions that no longer appear anywhere
  the checker looks.
- **Why nothing caught it:** the gap only produces false positives, and only for a doc citing one
  of those 141 symbols in backticks. No current doc does, so the checker kept reporting OK while
  its authority set was a third of what it should be. A checker that is too strict fails visibly
  when it finally fires, but until then it looks identical to a correct one.
- **Fix:** add `*.c` to each of the three globs. The set is meant to answer whether a symbol exists
  in the tree, so it follows the file extensions the tree actually uses.

## Six `network_drivers` enums lost their declared width in the C conversion, growing every struct that holds one

- **Status:** FIXED (2026-08-02), pending a target build. Found by diffing `network_drivers/`
  against v0.0.1.
- **Symptom:** no failure and no diagnostic. `sizeof` grew on `TcpConn`, `TcpEvt` and `pc_ip`, so
  the static pools built from them grew with no source change naming a size.
- **Root cause:** `enum class X : uint8_t` states the width in the declaration. Rewriting it as a
  plain `typedef enum` drops that clause, and a C enum with no attribute is whatever the
  implementation picks, which is `int` on every target here. Six declarations lost it:
  `ConnState`, `EvtType` and `pc_conn_reason` in `transport/tcp.h`, `pc_ip_family` and
  `pc_ip_scope` in `network/ip.h`, and `pc_tcp_op` in `transport/tcp.c`. Three are stored rather
  than only passed, so each one multiplies: `TcpConn` holds `_Atomic ConnState state` and
  `conn_pool` is `TcpConn[CONN_POOL_SLOTS]`; `TcpEvt` holds `EvtType type` and every listener
  carries `_queue_storage[EVT_QUEUE_DEPTH * sizeof(TcpEvt)]`; `pc_ip` holds `pc_ip_family family`
  and is embedded wherever an address is stored. One byte to four, per instance, in the pools whose
  total is meant to be computable before flashing.
- **Why nothing caught it:** the width was never asserted anywhere. It lived only in the
  declaration, so removing the declaration removed the requirement with it, and nothing downstream
  reads a size it could disagree with. Every test still passes because no behavior depends on the
  width, only the footprint does.
- **Fix:** `PROTO_ENUM_PACKED` on all six. The attribute asks for the narrowest type the values fit,
  which is a byte for each of these, and `types.h` already proves the toolchain honors it with a
  `static_assert` on `proto_enum_probe`. `pc_phy_ps` in `physical/physical.h` was the same defect,
  found and fixed separately, which is what prompted pairing every `enum class X : T` in v0.0.1
  against its counterpart here: 34 enums carried a declared width, 6 had lost it.
- **Not affected:** `WsCloseCode` (`uint16_t` -> packed) still occupies two bytes because its values
  run 1000-4999 and packed widens to fit. `DeflateResult` and `InflateResult` (`int32_t` -> packed)
  narrow to a byte, and both are return values rather than stored fields. `CborType` and
  `MsgpackType` were folded into `pc_codec_type`, which is packed.

## A mount root without a trailing slash silently concatenates: `/gcode` + `/part.nc` -> `/gcodepart.nc`

- **Status:** FIXED (2026-07-31, host-validated). Pre-existing; found while moving the path join
  into `server/filesystem`.
- **Symptom:** an SFTP or SCP server started with a root that does not end in `/` writes every file
  to a path with the separator missing. `pc_ssh_sftp_begin(LittleFS, "/gcode")` plus a client
  request for `/part.nc` resolves to `/gcodepart.nc` - a sibling of the intended directory, not a
  file inside it. No error is reported at any layer; the transfer succeeds to the wrong path.
- **Root cause:** the join treats the mount root's trailing `/` as a known property and emits
  `root || sub` with no separator of its own, which is correct and is why there is no runtime test
  for it. Nothing established the property, though: the root arrived as a caller-supplied
  `const char *` stored as-is by `pc_ssh_sftp_begin` / `pc_ssh_scp_begin`, whose only normalization
  was `(root && root[0]) ? root : "/"`. `examples/L5-Session/SSHSftp/README.md:68` documents the
  broken form, so the recommended call was the failing one. The shipped example uses `"/"`, which
  happens to end in a slash, which is why HW validation never caught it.
- **Fix:** the accessor owns the root, so it is the one place that can make the shape true rather
  than assume it. `pc_fs_begin()` stores a normalized copy with the separator appended if absent;
  the join's assumption then holds by construction and still costs no runtime test. A root that
  does not fit is refused outright rather than truncated into a different directory.

## Every ESP32 example failed to link: `web_assets.cpp` was tracked twice under two names

- **Status:** FIXED (2026-07-31). Both changed sketches now build on the real core: Telnet 919,127 B
  (70%), WebTerminal 921,847 B (70%), `esp32:esp32:esp32s3` core 3.3.10.
- **Symptom:** every one of the 152 example sketches failed at link with
  `multiple definition of 'PC_DASHBOARD_PAGE'`, and the same for `PC_PROV_FORM`,
  `PC_PROV_SAVED_HTML`, `PC_TERMINAL_PAGE`, `PC_SERVICE_WORKER`, `PC_STATS_JSON`, `PC_METRICS_PROM`.
- **Root cause:** a now-deleted `web.cpp`, beside `web_assets.cpp` in `src/network_drivers/application/`, was a byte-identical copy of
  `web_assets.cpp`, and `web.h` was `web_assets.h` with only its include guard renamed - its file
  banner still read `@file web_assets.h`. Both were tracked. Arduino compiles every `.cpp` under a
  library's `src/`, so both copies were compiled and both defined the same symbols. They are
  `const`, which is internal linkage in C++ and would have been harmless, but `web_assets.h`
  declares them `extern`, which makes them external and therefore a collision.
- **Why nothing caught it:** the native envs compile an **explicit source list** per env, so a file
  nobody lists is a file nobody builds - 5,780 host tests passed with the duplicate present. Only a
  whole-tree build reaches it. `check_examples.py` reads sketch source for API misuse and never
  links. Worse, the file had already been _touched_ by a green commit: "fix the web.h guard" made
  the duplicate's include guard unique, which is what a linter asks for and the exact opposite of
  what the file needed.
- **Fix:** deleted `web.cpp` and `web.h`. `web_assets.*` is the generated pair
  (`web_assets/wizard/build_assets.py` emits it) and all six consumers already included
  `web_assets.h`; nothing included `web.h`, not even `web.cpp`.
- **Gate:** `ci_tooling/check/check_duplicate_symbols.py`, wired into CI. It reports a file-scope
  variable defined in two `.cpp` files, but only when the definition actually carries external
  linkage - non-`const`, or `const` with an `extern` declaration in a header. That distinction is
  load-bearing: without it the check flags the four codecs that each define their own
  `const DIST_BASE[]` / `LEN_BASE[]`, which never collide because a file-scope `const` is internal
  linkage in C++. Proven by restoring the duplicate and watching all 7 symbols report.

---

## The banned-construct gate was a no-op on Windows: its baseline keys carried the host's path separator

- **Status:** FIXED (2026-07-31). `check_src_banned.py --all` on Windows now reports
  `971 known ratcheted site(s) remain, 16 fixed`, matching Linux.
- **Symptom:** running the gate locally on Windows reported **every** recorded site as a brand-new
  violation - 987 of them - so the output read as "the sweep has regressed catastrophically" rather
  than "the tool cannot find its baseline." CI on Linux passed the whole time.
- **Root cause:** `_key()` built its baseline key from `str(path)`, and `--all` collects through
  `pathlib.Path.rglob`, which yields a `WindowsPath`. So the scanner produced
  `src\services\web\httpcache\httpcache.cpp|19|rev#1` while the committed baseline holds
  `src/services/web/httpcache/httpcache.cpp|19|rev#1`. Nothing matched, so nothing was ever
  recognized as known. The module already had a `_norm()` helper for exactly this, but only
  `collect()` used it.
- **Why nothing caught it:** the baseline is a _committed_ file compared against a _host-generated_
  key, and only one of the two platforms that reads it runs in CI. A ratchet that silently stops
  recognizing its own floor fails in the safe direction (it over-reports), which is why it survived -
  it never let a violation through, it just stopped being usable where the code is written.
- **Fix:** `_key()` normalizes the path before building the key. The gate is the same on both
  platforms now, which is the point of committing the baseline at all.

---

## AES-256-GCM ran 7.6x slow: we replaced the vendor's AEAD, then rebuilt its context per packet

- **Status:** FIXED (2026-07-31). Sealing 1 KiB on an S3: 616,567 -> **81,130** cyc. The raw vendor call
  is 81,104, so this is now at the toolchain ceiling. Native 267/267 across the affected envs; all four
  ESP32 examples build; measured on the COM3/COM4 S3 rig.
- **Symptom:** none. Nothing failed and no test complained - `pc_aesgcm` was simply 7.6x slower than the
  same chip's own AES-GCM, and `docs/FEATURE_PERFORMANCE.md` recorded that as a property of the silicon.
- **Root cause 1 - a path that should not have existed.** `SOC_AES_SUPPORT_GCM` is unset for the S3, so
  the code concluded the die had no GCM and hand-rolled a replacement, driving the block cipher 16 bytes
  at a time with a software GHASH. The capability macro describes one mode of one peripheral; it is not a
  statement that no fast path exists. The vendor's implementation knows what its own silicon can do.
- **Root cause 2 - the api forced a context rebuild per record.** `pc_aesgcm_seal_tag()` took raw key
  bytes, so every packet stood an mbedtls GCM context up and tore it down: **9,221 cycles**, fixed
  regardless of message size. ~10% of a 1 KiB record, ~30% of a 256 B TLS record, and most of a small
  interactive SSH packet.
- **Why nothing caught it:** correctness tests cannot see it - the KATs passed the whole time, because a
  slow AEAD is still a correct AEAD. And the one micro-measurement that looks like it would catch root
  cause 2 actively hides it: `init+setkey+free` timed **alone** is 513 cycles, ~18x under the truth,
  because a loop that never encrypts never takes the AES peripheral. The cost only appears as
  acquire/use/release around real work.
- **Fix:** the AEAD moved to explicitly-selected backends under `board_drivers/hal/` (vendor / portable,
  no weak symbol), and `pc_aesgcm` became keyed - `pc_aesgcm_key_init` once per key, seal/open per
  record, with the raw-key entry points deleted rather than kept as a shim so no caller can pay the
  lifecycle invisibly. SSH holds a context per direction in its keymat and now stores no raw GCM key at
  all; SMB/IKEv2/IPsec-ESP build one per call at the call site, where the cost is at least visible.
- **Ruled out by measurement** (recorded so it is not re-litigated): pointer alignment - context at pool
  align 8 = 81,102, align 32 = 81,085, stack local = 81,077, identical - and the pool itself, 849 cyc for
  borrow + wipe.
- **Found on the way:** the SSH round-trip test hand-populated `km->aes_key_*` instead of building a
  context. A zeroed GHASH table makes the tag a **constant**, so the round trip still passed while forged
  packets were accepted - the tamper assertion is what caught it. Shipping code was never affected (the
  KEX install path always builds the context), but it is a sharp edge worth knowing: an uninitialized
  AEAD context fails open, not closed.
- **Follow-ups CLOSED (same day).** `pc_quic_aes128_gcm` and `pc_dtls_record` carried the identical
  pair of errors and got the identical pair of fixes: **562,292 -> 80,361** (7.0x) and
  **578,730 -> 91,209** (6.3x). `crypto/aead/aes128gcm.cpp` had no vendor AEAD path at all, so it was
  deleted outright and replaced by esp/portable backends; the AES-128 block primitive (`pc_aes128`,
  QUIC header protection) went with it, leaving the core with declarations only. Four raw-key entry
  points collapsed to two keyed ones - attached-tag is just detached with `tag_out = ct_out + len`.
- **Two things this turned up that the first pass did not:**
    1. Removing the combined-length check from the AEAD moved a bounds obligation onto every caller.
       `pc_aes128gcm_open()` used to reject a buffer shorter than a tag; the detached api has no
       combined length to check, and QUIC's `ct_len` comes off the wire, so `ct_len - TAG_LEN` would
       have wrapped to a huge `size_t`. Explicit guards restored in both callers. DTLS was only
       accidentally safe - a wrapped length happened to fail a later `> out_cap` test.
    2. A silent no-op edit wrote a 176-byte key schedule into a 16-byte field. `clang-format` had
       reflowed the struct member, so a literal-string replacement missed it and - unlike the others -
       that one had no assertion. The RFC 9001 A.2 wire vector caught it. Every replacement asserts now.
- **A number I got wrong and corrected:** I first reported the header-protection context at ~8,400
  cycles. That micro-bench measured pool borrow + context build + one AES-128-ECB block. Split: block
  alone 7,842, so the context is **556** - which matches the observed ~560 improvement, where 8,400
  did not. Subtract the required work before crediting the wrapper.
- **Next target, found by that split:** one 16-byte AES-ECB block costs **7,842 cycles** (~490
  cycles/byte, against ~78 for the 1 KiB GCM beside it). A single hardware AES operation pays per-call
  setup a bulk operation amortizes. Header protection runs on every packet, so this is now the
  dominant per-packet cost on small QUIC/DTLS records - and software AES may beat the accelerator for
  one block. OPEN.
- **Lesson:** a benchmark is a correctness test for performance claims, and we had none for this - the
  7.6x sat in a doc as a fact about the chip for as long as nobody re-measured it.

---

## The whole LoRa driver was excluded from every build by a stale feature guard

- **Status:** FIXED (2026-07-30). `native_lora` 19/19, and the file now compiles into the S3 image.
- **Symptom:** an `arduino-cli` build of an unrelated sketch died on
  `services/radio/lora/lora.cpp:14: fatal error: services/lora/lora.h: No such file or directory`.
- **Root cause:** two independent misses in one file. The reorg moved LoRa to
  `services/radio/lora/` without updating that file's own `#include`, and the `dws_` -> `pc_`
  rename skipped the file entirely: 13 stale tokens, including its guard `#if DWS_ENABLE_LORA`.
- **Why nothing caught it:** the stale guard is never true, so the entire body of the file was
  preprocessed away in every configuration. A file that compiles to nothing compiles cleanly. The
  header had been renamed correctly, so `PC_ENABLE_LORA=1` would have compiled every `pc_lora_*`
  declaration and then failed to link a single definition - the feature was shipped-broken, and
  only the default of 0 hid it. The one line outside the guard was the `#include`, which is
  precisely why that is all the compiler ever complained about.
- **Fix:** correct the include path, rename all 13 tokens. Tree-wide `DWS_`/`DET_`/`dws_`/`detws_`
  count is now 0.
- **Lesson:** a feature disabled by default has no compile-time coverage at all, so a rename sweep
  measured by "does it still build" cannot see it. The check that would have caught this is that
  every quoted `#include` under `src/` resolves to a real file - that holds whether or not the
  code around it is enabled.

---

## A header used uint32_t without including stdint.h and only worked by accident

- **Status:** FIXED (2026-07-30). `native_primitives` 21/21 including four new `pc_sb` cases.
- **Symptom:** adding the first direct unit test of `shared_primitives/strbuf.h` failed to compile:
  `'uint32_t' was not declared in this scope`, pointing at a signature that had been there all along.
- **Root cause:** `strbuf.h` declared `pc_sb_u32(pc_sb *, uint32_t)` but included only `<stddef.h>`
  and `<string.h>`. Every file that included it happened to pull in `<stdint.h>` first, so the header
  was never compiled on its own and the missing include never showed.
- **Fix:** include `<stdint.h>` in the header that uses it.
- **Lesson:** "it compiles everywhere it is used" is not the same as "it compiles". A header is only
  self-contained if something includes it first, and nothing did until a test did.

---

## A tree-wide rename of a one-letter constant rewrote character literals, test vectors, and prose

- **Status:** FIXED (2026-07-30), same session. 489 corrupted sites reverted, 0 residue, 310/310 tests
  green on every suite that had been touched.
- **Symptom:** `src/network_drivers/application/smb/ntlmssp.c` stopped compiling with
  `narrowing conversion of 1346592592 from int to uint8_t`. 1346592592 is `'PC_P'` - a
  four-character literal - where the source had written `'P'`.
- **Root cause:** the rule-18 converter prefixes a converted constant and then renames that token
  across the tree. One of the constants was named `P` (an sntrup761 spec parameter). The rename used
  `(?<![\w])P\b`, and **quotes are not word characters**, so it matched inside literals and prose:
    - `{'N','T','L','M','S','S','P',0}` became `'PC_P'` - the NTLMSSP signature
    - NMEA test vectors `",12202.1236,W,1,08,"` became `,PC_W,` - silently changing what they assert
    - comments: `"Q-command codec"` -> `PC_Q-command`, `"single-bit R/W"` -> `R/PC_W`
- **Why the gates missed it:** a mangled character literal violates no naming rule, so check_symbols
  and check_src_banned were both green with `'PC_P'` in the source. **The compiler caught it** - the
  narrowing conversion. Types found what pattern matching could not.
- **Fix:** revert every `PC_<single char>` globally (no library macro is one character, so all of them
  were damage), and make the converter refuse to rename any name of 2 characters or fewer, printing
  why. The three sntrup761 parameters were then named by hand as `PC_SNTRUP_P/Q/W`, keeping the spec
  mapping in a comment - which also removes the `#define P 761` hazard that rewrites the token `P` in
  every header that translation unit includes (SYMBOLS.md s2's `OUTPUT` problem, exactly).
- **Lesson:** a tree-wide rename's safety is a property of the NAME, not of the tool. `EDGE_MESH_RESP_MAX`
  can be swept blind; `P` cannot, and no amount of care in the sweep fixes that - only refusing the
  input does.

---

## 130 of 152 example sketches could not be compiled: include order defeated Arduino's library discovery

- **Status:** FIXED (2026-07-30). HW-verified: `examples/Foundation/Basic` builds (908 KB flash / 41% RAM),
  flashes to an ESP32-S3, joins WiFi, and serves `GET /` with keep-alive plus the framework's automatic 404.
- **Symptom:** every affected sketch failed at the first line of the build, against a clean Arduino install:
  `fatal error: network_drivers/physical/physical.h: No such file or directory`.
- **Root cause:** Arduino resolves libraries by walking a sketch's `#include` directives and matching each
  against an installed library. `network_drivers/physical/physical.h` matches nothing until ProtoCore's
  `src/` is already on the include path, and the only include that puts it there is `protocore.h`. With
  the physical include first, the compile dies before the library is ever discovered. Alphabetical include
  order is what produced it (`n` sorts before `p`), so the house style caused the bug: this is the one
  place a sketch must not sort, because the header that identifies the library has to lead.
- **Why nothing caught it:** the native test suite compiles `src/` directly with explicit include paths, so
  it never exercises Arduino's library resolution. 5756 host tests passed against sketches that could not
  build. Only a real `arduino-cli compile` reaches this code path - which is exactly why the examples
  compile gate exists as its own task.
- **Fix:** `protocore.h` first in all 132 sketches (120 plain, 12 whose include carried a trailing comment).
  Twelve of them already carried the comment "library entry header (also sets the src/ include root)" - the
  reason was known and written down, and the order was still wrong. Documenting a constraint does not
  enforce it.

---

## 86 example READMEs teach the banned `WiFi.*` API that rule 6 forbids, and no guardrail scans them

- **Status:** FIXED (2026-07-30). 446 vendor calls and 144 bare enum members corrected across 83 READMEs
  plus the two esp-idf examples; guarded by `ci_tooling/check/check_examples.py`, which reads sketches AND
  README fenced code. The guard was verified by injecting a regression and confirming a non-zero exit.
- **Second defect, found while writing the guard:** the enum-member harvester used a lazy `.*?` body match,
  so it mis-attributed members between enums and missed most of them entirely - it saw 376 members where
  the tree has 894. The first scoping pass therefore fixed 68 and silently left 76. Bounding the body with
  `[^{}]*` and stripping `#if` lines from it (enum bodies are conditionally compiled, so the flag names were
  being harvested as members) fixed both. The harvester now lives in `ci_tooling/lib/src_symbols.py` so the
  sweep and the checker cannot disagree.

- **Symptom:** 86 of 154 `examples/**/README.md` teach `#include <WiFi.h>`, `WiFi.localIP()`, `WiFiClient`, or
  `WiFiUDP` in their annotated-source blocks, while **0 of the `.ino` sketches** use any of them. A beginner who
  follows a README instead of reading the sketch next to it writes exactly the code the library bans, and it
  will not link against the transport API the sketch actually uses.
- **Root cause:** two independent gaps that only bite together. (1) The sketches were migrated to the library
  transport (`pc_client_*` / `pc_udp_*` / `pc_net_egress_ip()`), but the annotated-source blocks are
  hand-rolled on purpose - the heavy annotation is the teaching content and cannot be generated - so the
  migration updated the code and left 86 hand-written copies behind. (2) `docs/SRCBANNED.md` rule 6 explicitly
  states **"Applies to `examples/` too"** and even documents the check
  (`rg -n 'WiFiClient|WiFiUDP|AsyncUDP' src/ examples/`), but `ci_tooling/check/check_src_banned.py` scans only `src/`,
  explicitly exempts `examples/`, and never scans markdown at all. The rule that would have caught this was
  written down and never enforced where it claimed to apply.
- **Second class, same cause:** 62 READMEs also use **unscoped enum members** - `server.on("/", HTTP_GET, h)`
  and `case HTTP_11:` - but `HttpMethod` is an `enum class` (src/protocore.h:76), so that code does not
  compile at all. Every sketch uses the scoped `HttpMethod::HTTP_GET` form (84 files); zero use the bare
  form. Same hand-rolled blocks, same migration, same drift.
- **Fix (planned):** correct both classes in place, preserving the surrounding annotation, and extend the
  guardrail to enforce rule 6 over `examples/` for both `.ino` and README fenced code, so the documented scope
  and the enforced scope finally match. The remaining bans stay `src/`-only per SRCBANNED.
- **Lesson:** a ban that names its own scope in prose is not enforced until a checker reads that scope. The
  hand-rolled annotation is worth keeping; what was missing is a check that lets it stay hand-rolled safely.

---

## SMB 3.x client couldn't reach a `smb encrypt = required` share; then AES-CCM in-place decrypt clobbered its own AAD/tag

- **Status:** FIXED (2026-07-27). HW-verified against a real Samba (RPi, `smb encrypt = required` share) with all
  four SMB 3.1.1 ciphers (AES-128/256-GCM, AES-128/256-CCM), each reading the share file byte-exact.
- **Symptom (1):** the encrypted-share probe failed at TREE_CONNECT with ACCESS_DENIED (0xc0000022). **Symptom
  (2), found while adding the other ciphers:** AES-CCM round-tripped in isolation (KAT + separate-buffer codec
  test) but failed end to end in the client (`smb_open` -> SMB_ERR_PROTOCOL), while GCM worked.
- **Root cause (1):** the NEGOTIATE request left the Capabilities field zero, so it never advertised
  `SMB2_GLOBAL_CAP_ENCRYPTION` (MS-SMB2 §3.2.4.2.2.2). Samba then negotiated no cipher and rejected the
  unencrypted session. Additionally, a share-level (not global) `smb encrypt = required` does **not** set the
  session `SMB2_SESSION_FLAG_ENCRYPT_DATA`, so the client - which only encrypted when the server set that flag -
  never turned encryption on; it needs client-forced encryption (like smbclient `-e`, MS-SMB2 §3.2.4.1.5).
- **Root cause (2):** CCM is decrypt-then-MAC (unlike GCM, which verifies before producing plaintext), so
  `pc_aesccm_open_tag` reads the AAD and received tag **after** writing the recovered plaintext. The SMB codec
  decrypts **in place** (`rx -> rx`, a no-heap design GCM tolerated by read-before-write), so the plaintext
  overwrote the TRANSFORM_HEADER's AAD (`rx[20..51]`) and tag (`rx[4..19]`) before the MAC consumed them - it
  authenticated garbage and failed closed. Separate-buffer tests couldn't see it (no aliasing).
- **Fix:** (1) advertise `SMB2_GLOBAL_CAP_ENCRYPTION` in NEGOTIATE and add `SmbConfig.encrypt` (client-forced
  encryption: activate once a cipher is negotiated, regardless of the server session flag). (2) `pc_smb2_decrypt`
  snapshots the 48 header bytes (AAD + tag) into locals before decrypting, making in-place decrypt correct for
  every cipher - the crypto primitive's contract is "out may alias the ciphertext", never the AAD/tag, so the
  codec that aliased the whole buffer is what must preserve them. Read the authoritative MS-SMB2 spec + the
  smbclient reference trace to get the negotiation right.
- **Also delivered:** AES-CCM (SP800-38C, 128/256) as a new `crypto/aesccm`, detached-tag AES-256-GCM in
  `crypto/aesgcm`, all four ciphers wired into the codec + negotiate + key derivation. KAT'd vs pyca/cryptography.

---

## pc_net_mac read all-zeros on an Ethernet-only device (no egress-interface MAC accessor)

- **Status:** FIXED (2026-07-26). Surfaced + validated on the ESP32-P4 (Ethernet, 192.168.1.153) and S3 (WiFi).
- **Symptom:** the physical (L1) link test printed `MAC=00:00:00:00:00:00` on the P4 - an Ethernet-only board
  whose PHY plainly has a MAC (`e8:f6:0a:e0:a7:8d`). The library exposed no way to read the active interface's
  hardware address.
- **Root cause:** `pc_net_mac()` returns the WiFi _station_ MAC via `WiFi.macAddress()` (what ESP-NOW / WiFi
  diagnostics need), which reads back zeros when the WiFi driver was never started, as on the wired P4. It does
  exactly what it is documented to do; the real gap was the absence of an interface-neutral "MAC on the wire"
  accessor for the egress link. Not a regression - the physical-layer vendor-partition HW test just exposed it.
- **Fix:** added `pc_net_egress_mac()` - reads the live default-route netif's `hwaddr` (lwIP), so it returns
  the Ethernet PHY's MAC on a wired link and the WiFi STA MAC on a wireless one, independent of which driver
  started; fallback stub on host / no-backend builds. Clarified `pc_net_mac()`'s doc as WiFi-STA-specific and
  cross-referenced the new accessor. HW-verified: P4 `egress_mac=e8:f6:0a:e0:a7:8d` (with `wifi_sta_mac` zeros);
  S3 `egress_mac=94:a9:90:d1:7a:b8` == its `wifi_sta_mac` (WiFi is the egress there).

---

## Slow-loris held the connection pool indefinitely (slot-exhaustion DoS)

- **Status:** FIXED (2026-07-21). Reproduced and fixed on an ESP32-P4 (Ethernet, 192.168.1.153).
- **Symptom:** a slow-loris - open N connections, send `GET / HTTP/1.1\r\n`, then trickle one header line
  (`X-a: b\r\n`) every ~3 s and never terminate the headers - filled the entire fixed connection pool
  (`MAX_CONNS`) and denied it to legitimate clients **forever**. A `scratchpad/slowloris2.py` with 12 holders
  saw every legitimate probe DENIED (`ConnectionResetError`) with no recovery.
- **Root cause:** the only per-connection timeout was the idle sweep (`CONN_TIMEOUT_MS`, 5 s), and
  `last_activity_ms` is refreshed on **every accepted RX byte** (tcp.cpp, the recv handler). A trickle that
  drips a byte just under the idle window therefore keeps the idle timer alive indefinitely while never
  completing a request - so a partial request could hold a slot for as long as the attacker kept dripping.
  There was no deadline a trickle could not reset.
- **Fix:** a request-**header** read deadline - the nginx `client_header_timeout` semantic. A new per-slot
  `req_start_ms` is armed once, on the first RX byte of a request (tcp.cpp), and cannot be reset by later
  trickle bytes. The per-tick session poll (`http_poll_slot`) reaps any slot still in the **header** phase
  (`parse_state < PARSE_BODY`) past `PC_REQUEST_TIMEOUT_MS` (default 10 s) with a `408 Request Timeout` +
  `Connection: close`, freeing the slot. It is scoped to the header phase, so a legitimate slow **body** upload
  (which sits in `PARSE_BODY` for its whole duration, governed by the streaming handler + idle timer) is never
  reaped; WebSocket / SSE slots are skipped by the poll before the check. `req_start_ms` is disarmed on request
  completion so a kept-alive connection re-arms per request.
- **Verified:** host - four reap tests in `test_dispatch` (reaped past deadline, survives before it, a
  completed slow request is not reaped, a `PARSE_BODY` upload is not reaped) plus the full transport/app/
  accept-gate/keepalive suites. HW (ESP32-P4) - the pool now **recovers**: probes DENIED at t=2/5/8 s (pool
  full), SERVED at t=11/14/17 s once the 10 s deadline reaps the holders; a threaded-trickle client (idle timer
  kept fresh) receives a real `HTTP/1.1 408 Request Timeout` at t=10.1 s, confirming the request deadline
  (not the 5 s idle sweep) is what fires; heap stable, no panic across the attack cycles.

## SSH algorithm negotiation used server preference, not client preference (RFC 4253 §7.1) - KEX reset vs CycloneSSH

- **Status:** FIXED (2026-07-20). Found by **real-peer interop**: the Oryx **CycloneSSH** client (a
  from-scratch second SSH stack) could not complete a handshake against our SSH server on an ESP32-P4 (COM9) -
  the server reset the TCP connection right after the client's `SSH_MSG_KEX_ECDH_INIT`. OpenSSH (the 16/16
  algorithm matrix) never hit it because its modern defaults happen to match the order we advertise.
- **Symptom:** CycloneSSH negotiated curve25519-sha256 + ssh-ed25519 + chacha20-poly1305 (its own picks), sent
  a 32-byte curve25519 `KEX_ECDH_INIT`, and got a TCP RST with no `KEX_ECDH_REPLY`. Captured on the wire and
  reproduced deterministically by feeding the exact KEXINIT + init bytes through `ssh_kexinit_parse` →
  `ssh_kex_generate` → `ssh_kexdh_handle` in a host test (`ssh_kexdh_handle` returned -1).
- **Root cause:** `negotiate_alg` (ssh_transport.cpp) iterated the SERVER's candidate list and picked the first
  name the client also offered - **server preference**. RFC 4253 §7.1 mandates **client preference**: "iterate
  over the client's algorithms ... choose the first the server also supports." When the server holds an RSA
  host key, its `prefer_rsa` ordering ranks ecdh-sha2-nistp256 + rsa-sha2-512 above curve25519 + ssh-ed25519,
  so a client that lists curve25519/ed25519 first made the two sides **negotiate different algorithms**: the
  server chose nistp256 and expected a 65-byte init, the client sent a 32-byte curve25519 init,
  `parse_ecdh_init_p256` rejected it (`n != 65`), and `ssh_kexdh_handle` returned -1 → RST. OpenSSH advertises
  its algorithms in the same PQC/curve-first order we do, so it always guessed our top pick and never diverged;
  a differently-ordered implementation was required to expose it. The same server-preference applied to
  cipher/MAC/compression (they happened to agree only because the top choices matched).
- **Fix:** `negotiate_alg` now iterates the **client's** name-list in order and takes the first name any
  available server candidate matches (RFC 4253 §7.1 client preference) - for KEX, host key, cipher, MAC, and
  compression. Host-tested (`native_ssh` 196/196, incl. a new client-preference cipher test and a regression
  that drives the exact captured CycloneSSH bytes) and **HW-verified on the ESP32-P4**: CycloneSSH now
  completes the full KEX + auth + a byte-exact encrypted-channel echo, and OpenSSH is unaffected.

---

## `tcp_recved` on a freed pcb - remotely-triggerable reboot under SSH connection churn (DoS)

- **Status:** FIXED (2026-07-20). Found by **stress-testing the live SSH server on an ESP32-P4** (COM9) with
  the pentest tool's new SSH DoS attacks (`ssh_conn_saturation` / `ssh_slowloris` / `ssh_handshake_flood`)
  plus concurrent sntrup761 handshakes killed mid-flight and rapid connect/RST churn.
- **Symptom:** under connection churn the device rebooted (`rst:0xc (SW_CPU_RESET)`) with any of three
  panics - `assert failed: tcp_output ... (tcp_output: invalid pcb)`, `assert failed: tcp_update_rcv_ann_wnd
... (new_rcv_ann_wnd <= 0xffff)`, or `Guru Meditation Error: Load access fault`. A remote peer could reboot
  the server at will by opening SSH connections and resetting them mid-handshake - a denial of service.
- **Root cause:** every socket op the worker marshals to `tcpip_thread` re-checks that the slot still owns
  the captured pcb before touching lwIP (`k->pcb == conn_pool[slot].pcb`, since a remote RST frees the pcb via
  the error callback between capture and execution) - **except `PC_OP_RECVED`**, which called
  `tcp_recved(k->pcb, k->len)` unguarded (`tcp.cpp`). The RX-ack path (`pc_conn_ack_consumed`) checks the pcb
  worker-side, but the marshaled `tcp_recved` runs later; if the connection was torn down in between,
  `tcp_recved` walks a freed pcb - `tcp_update_rcv_ann_wnd` (window assert) and, when reopening the window,
  a window-update `tcp_output` (`invalid pcb`) - which is why one hole produced all three signatures. A serial
  `esp_rom_printf` trace confirmed every _guarded_ `tcp_output` ran on an ESTABLISHED pcb (`state==4`), so the
  crash was not at any guarded send site - it was the unguarded receive-ack.
- **Fix:** guard `PC_OP_RECVED` with the same O(1) liveness check as `PC_OP_SEND`/`PC_OP_OUTPUT` - skip
  `tcp_recved` (return `ERR_CLSD`) when `k->pcb != conn_pool[slot].pcb`. Verified on HW: this **eliminates the
  `tcp_update_rcv_ann_wnd` assert** and the reboot on the targeted mid-flight-kill + connect/RST churn repro
  (0 reboots, heap flat ~282 KB). A **second, distinct** `tcp_output: invalid pcb` reboot remained under the
  broader DoS suite (`ssh_slowloris` / `ssh_handshake_flood`) - a **NULL-pcb** hole in the same guards, now
  also fixed; see the entry below.

---

## `tcp_output(NULL)` - a NULL pcb passes the liveness guard (SSH slow-loris / churn DoS)

- **Status:** FIXED (2026-07-20). A second, distinct remotely-triggerable reboot from the RECVED bug above;
  `assert failed: tcp_output ... invalid pcb` -> `rst:0xc (SW_CPU_RESET)`, ~26-36 reboots per high-intensity
  DoS run on an ESP32-P4.
- **Reliable repro (also the regression test):** `pc_pentest.py --only ssh_slowloris` - 8 half-open
  partial-banner connections (`"SSH-2.0-sl"` + a dribble of `.` bytes), refreshed ~19 s; also
  `ssh_handshake_flood`. Heap stays flat, so a use-after-free, not exhaustion.
- **Root cause (pinned by the flash coredump, NOT by guessing):** every worker-marshalled socket op guards
  `k->pcb == conn_pool[slot].pcb` (a stale pcb after a torn-down connection won't match the slot's live pcb).
  But the guard compared **pointers without a NULL test**. `pc_conn_flush()` marshals `PC_OP_OUTPUT` with
  `conn_pool[slot].pcb`, which is **NULL** for a slot that was torn down between the caller and the op - so a
  captured-NULL vs a live-NULL (`NULL == NULL`) **passed** the guard and called `tcp_output(NULL)` -> lwIP's
  "invalid pcb" panic. The `esp_coredump` backtrace showed exactly this: frame `pc_tcp_do` `tcp.cpp:297`
  (`PC_OP_OUTPUT`) with `k->op=PC_OP_OUTPUT, k->slot=0, k->pcb=0x0, conn_pool[0].pcb=0x0, state=CONN_FREE`.
  An earlier `esp_rom_printf` trace had wrongly ruled this path out (it only ever printed the _non-NULL_
  survivors), which is why the coredump was decisive.
- **Fix:** add the null test to the `PC_OP_SEND` / `PC_OP_OUTPUT` / `PC_OP_RECVED` guards -
  `k->pcb && k->pcb == conn_pool[slot].pcb` (SEND: `!k->pcb || k->pcb != ...`). A captured-NULL now skips
  (`ERR_CLSD`) instead of calling `tcp_output`/`tcp_write`/`tcp_recved(NULL)`. **HW-verified on the P4:** the
  full DoS suite that rebooted it 26-36x now runs **3 PASS / 0 findings, 0 reboots, 0 asserts, heap flat**;
  `ssh_handshake_flood` also dropped from ~237 s (crash-stalled) to ~19 s. The pentest reboot detector was
  fixed in the same pass (an ESP32 reboot times out rather than `ECONNREFUSED`), so a crash-and-recover is no
  longer mis-read as "alive".
- **Note:** a speculative `PC_OP_CLOSE` teardown-reorder tried earlier changed the reboot count by nothing
  and was reverted rather than shipped - the real bug was the missing NULL test, not the CLOSE path.

---

## SSH server host-key name-list truncated - `rsa-sha2-256` dropped when all three host keys are loaded

- **Status:** FIXED (2026-07-20). Found by a **real-OpenSSH interop matrix on an ESP32-P4** (the shipped
  SSH server, all four host-key algorithms forced one at a time via `ssh -o HostKeyAlgorithms=`).
- **Symptom:** with ed25519 **and** ecdsa-p256 **and** RSA host keys all provisioned, a client that forced
  `rsa-sha2-256` got `Unable to negotiate ... no matching host key type`. The server's advertised
  `server_host_key_algorithms` list came across truncated: `ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rs`
    - the last entry `rsa-sha2-256` cut to `rs`. `rsa-sha2-512` (one earlier) still worked, which is what made
      it look key-specific rather than a buffer overrun.
- **Root cause:** `ssh_kexinit_build()` assembled the host-key name-list into a **48-byte** stack buffer
  (`ssh_transport.cpp` `char hklist[48]`). The full four-algorithm list
  `ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256` is **57 chars + NUL = 58 bytes**, so the
  bounded `snprintf` in `build_hostkey_list()` stopped after `...rsa-sha2-512,rs`. It only bites when all
  three key types are held at once - the SSH example loads just RSA and the pentest rig just ed25519, so the
  list was always short enough before; a device provisioned with all three (a realistic deployment) hit it.
- **Fix:** size the buffer to hold the full list - `hklist[64]`. Regression test
  `test_kexinit_hostkey_list_carries_all_four_when_all_keys_loaded` (native_ssh_transport) provisions all
  three host keys and asserts every algorithm, incl. `rsa-sha2-256`, survives in the built KEXINIT; it fails
  on the old 48-byte buffer and passes on 64. Confirmed on HW: the P4 interop matrix went 15/16 -> **16/16**
  after the fix (every KEX incl. the mlkem768x25519 hybrid, every cipher/MAC, all four host-key algorithms).

---

## A board profile's hardcoded `RX_BUF_SIZE` silently defeated the streaming ring floor (large uploads reset)

- **Status:** FIXED (2026-07-20). Found while auditing the per-variant board-profile defaults; the failure
  mode and the fix were both confirmed on an ESP32-S3 rig (COM7), which corrected an initial mis-diagnosis.
- **Symptom:** on any chip whose profile pins `RX_BUF_SIZE` below the streaming floor (S3/P4/S31 = 2048,
  C3/C5/C6/H4 = 1536, the rest = 1024), enabling `PC_ENABLE_UPLOAD`/`OTA`/`WEBDAV` (streaming) left that small
  ring in place instead of raising it to 8192. **HW-measured on an S3 pinned at 2048:** a 4 KB upload succeeds
  byte-exact, but a 64 KB streamed upload is **reset ~5.6 s in** (`curl` exit 56, 0 bytes stored). With the ring
  at 8192, the same 64 KB (0.7 s) and a 256 KB (1.6 s, ~160 KB/s) upload round-trip **byte-exact**.
- **Root cause (two parts):** (1) the feature-driven ring upsize lived inline in `protocore_config.h` gated on
  `defined(PC_RX_BUF_SIZE_DEFAULTED)` - a marker set **only** in the base `#ifndef RX_BUF_SIZE` branch. A board
  profile is included first and sets `RX_BUF_SIZE` itself, so that branch (and the marker) is skipped and the
  upsize is a silent no-op for every profile that pins the ring. (2) The failure is _not_ a deadlock (the initial
  guess): with ack-on-consume the peer's advertised window tracks ring free space, so a sub-window ring forces a
  sustained upload to dribble a ring-full at a time and spend long spells in backpressure. The idle timer is
  refreshed only when a segment is **accepted**, never during backpressure (deliberate - a truly stuck connection
  must still be reaped, [[tcp.cpp]] `:850`), so a prolonged sub-window backpressure spell trips the 5 s idle
  timeout and the connection is reset mid-upload.
- **Fix:** move the resolution out of `protocore_config.h` into `board_drivers/board_profiles/derived_sizing.h` (the sizing layer's
  job), included last once every feature flag is known, and drop the `DEFAULTED` gate so the floor is enforced
  against whatever set the value - profile, `-D`, or base default - as a monotone raise (below floor -> lift;
  at/above -> untouched, so a deliberately roomy ring is preserved). Because the streaming floor is a full TCP
  window (8192), it is real DRAM per connection: the classic-ESP32 streaming examples (FileUpload / OTA /
  OtaRollback / WebDav) dial `MAX_CONNS` down so `8192 * MAX_CONNS` fits the ~122 KB `dram0_0_seg`.

---

## Reverse-SSH tunnel: scratch foreign-task crash + channel-slot starvation

- **Status:** FIXED (2026-07-20). Found by the **HW bring-up of the reverse-SSH client** on an ESP32-S3 tunnelling
  to a real OpenSSH 10.0 relay and serving the device's own `:80` back through it - the whole-firmware,
  two-task path host tests cannot reach.
- **Symptom (1):** the SSH handshake, ed25519 auth and `tcpip-forward` all succeeded ("tunnel up"), but the first
  `curl` through the forwarded port panicked the device with `assert_single_owner scratch.cpp:78 "scratch arena
borrowed from a foreign task"`. **Symptom (2):** after that was fixed, the first request returned HTTP 200 but
  every subsequent one hung / returned 000 until the channel eventually freed.
- **Root cause (1):** SSH packet decrypt borrows the shared per-worker scratch arena, which is single-accessor-
  per-task. The PC server's worker owns slot 0; the tunnel `poll()` runs in a different task, so opening the
  forwarded-tcpip channel decrypted a packet from a foreign task and tripped the tripwire. **Root cause (2):** the
  client held a single channel slot and only freed it on the relay's `CHANNEL_CLOSE`, which OpenSSH sends late -
  it waits for the device's EOF, which never came because the bridged keep-alive `:80` connection never closes.
  So the slot stayed busy and later `forwarded-tcpip` opens were refused ("administratively prohibited").
- **Fix:** (1) give the reverse-SSH client its own scratch slot - `PC_SCRATCH_SLOTS = PC_WORKER_COUNT + 1` when
  `PC_ENABLE_SSH_CLIENT`, claimed in `begin()` via `pc_worker_set_self()` so every later decrypt in that task
  uses the client's own arena. (2) replace the single channel with a pool (`PC_SSH_CLIENT_MAX_CHANNELS`, with
  `PC_CLIENT_CONNS` auto-provisioned to `1 + N`), and tear a channel down promptly on the relay's **EOF** (the
  forwarded peer is done sending; for a request/response bridge the reply is already delivered) instead of waiting
  for its CLOSE. Re-flashed: single, 6 rapid-sequential and 4 concurrent requests all return HTTP 200 with the
  device's body byte-for-byte, sustained across repeated bursts; `native_ssh` + `native_ssh_conn` + `native_pqc`
  (220 cases) stay green.

## SSH transport dropped a TCP read that carried several pipelined packets (SFTP write)

- **Status:** FIXED (2026-07-17). Found by the **HW bring-up of the SFTP server** on an ESP32-S3 + SD card,
  driven by the real OpenSSH `sftp` client - the exact whole-firmware path host tests cannot reach.
- **Symptom:** a small (< 2 KB) `put` round-tripped byte-exact, but any larger `put` reset the connection during
  the first `SSH2_FXP_WRITE` (client saw "broken pipe"); the device did not crash (heap stable, no panic).
- **Root cause (two layers):** (1) the `CHANNEL_OPEN_CONFIRMATION` advertised `SSH_CHAN_MAX_PACKET` = 32768 as
  the max packet we can receive, but the transport rejects any inbound packet larger than `SSH_PKT_BUF_SIZE`
  (2048) - so a peer that believed the advertisement (an SFTP write) sent a packet the transport threw away.
  (2) more fundamentally, `ssh_pkt_recv()` appended the whole incoming read to its `SSH_PKT_BUF_SIZE` buffer
  _before_ extracting packets and disconnected if the read exceeded the remaining space - so a single TCP read
  carrying several back-to-back CHANNEL_DATA messages (which is exactly how a client pipelines a large SFTP
  write's fragments) overflowed the buffer even though every individual packet fit. Interactive shells never
  send enough at once to hit either.
- **Fix:** (1) derive `SSH_CHAN_MAX_PACKET` from `SSH_PKT_BUF_SIZE` (`- 64`) so we never advertise more than we
  can receive, and it scales when the buffer is raised for throughput. (2) `ssh_pkt_recv()` now consumes its
  input **incrementally** - append as much as fits, extract every complete packet to drain the buffer, then
  append more - so a multi-packet read is processed instead of rejected. Re-flashed: a 60 KB `put`/`get`
  round-trips byte-exact over the SD card, and `native_ssh` + `native_ssh_conn` (209 cases) stay green.

## SFTP READDIR NAME response carried a garbage 4-byte prefix per entry

- **Status:** FIXED (2026-07-17). Found by the same SFTP HW bring-up: `put`/`get` worked byte-exact but any
  `ls` on a non-empty directory reset the connection.
- **Root cause:** `build_entry()` serialized a directory entry with an `SftpWriter`, which reserves a 4-byte
  packet-length prefix, and returned the entry length - but the entry _data_ started at `ent + 4`, while
  `do_readdir()` copied from `ent[0]`. So every NAME entry was prefixed with 4 bytes of the discarded length
  field, malforming the response; the client rejected it and disconnected.
- **Fix:** `build_entry()` drops the reserved prefix (`memmove(ent, ent + 4, len)`) so the entry bytes start at
  `ent[0]`. Re-flashed: `ls -l` lists files with correct sizes / longnames, and rename / rm take effect.

---

## Edge-cache mesh serving node RST'd the response before it drained (immediate close)

- **Status:** FIXED (2026-07-17). Found by the **two-rig HW bring-up** of the new mesh sibling cache
  (`PC_ENABLE_EDGE_MESH`) on two ESP32-S3s - the exact whole-firmware path host mock-seam tests cannot
  reach (the mock transport does not model TCP flush/close).
- **Symptom:** node B's cold miss for an object node A had cached always fell through to the origin
  (`X-Cache: MISS`, `mesh_misses` incremented, the origin logged a hit from B) instead of pulling from A
  (`X-Cache: MESH`). A hand-crafted valid mesh request to A over `:7645` got the connection **reset with 0
  bytes** (`WinError 10054`) - so A built a response but it never reached the peer.
- **Root cause:** the `PROTO_MESH` serve pump queued the whole response with `pc_conn_send` (which
  `tcp_write`s with `TCP_WRITE_FLAG_COPY`) and then immediately called `pc_conn_close` - the **immediate**
  teardown, which `tcp_close`s and, if the FIN cannot queue, `tcp_abort`s (RST), discarding the just-queued
  response the peer had not read yet. The requester then saw a closed connection with no complete frame →
  `MESH_FAIL` → treated as a miss → origin fallthrough.
- **Fix:** after queuing the full response, `pc_conn_flush(slot)` (tcp_output) then `pc_conn_begin_close(slot)`
    - the graceful dwell that holds the slot in `CONN_CLOSING` until the peer ACKs, finalizing from the sent
      callback once the TX drains (the same sequence `websocket_sse` uses for its response-then-close). The
      `closing_finalize` path does not dispatch the proto `on_close`, and `pc_conn_send` already COPY'd the
      bytes, so the `MeshConn` is freed proactively right after `begin_close`. Re-flashed both rigs: B's cold miss
      now serves `X-Cache: MESH` byte-exact from A with `Age` propagated, the origin is fetched exactly once (by
      A), and fall-through / symmetry / loop-free all pass. Classic HW-only integration bug (green host tests, a
      real RST on the wire).

---

## Edge-cache Range on a MISS lost the client's Range header (stale http_pool[slot])

- **Status:** FIXED (2026-07-17). Found by the **HW MISS+Range test** of the new Range/206-from-cache
  feature on an ESP32-S3 - the exact integration path host tests cannot reach.
- **Symptom:** a `Range: bytes=15-35` request against a **cold** cache returned the **full 200** body
  (83 bytes, `HTTP/1.0`, `X-Cache: MISS`) instead of a `206` window. A fresh-**HIT** Range request worked
  (byte-exact `206`), so range parsing/serving was correct - only the miss path lost the range.
- **Root cause:** `serve_hit` read the `Range` header from `http_pool[slot]`. On a fresh hit that runs
  synchronously in dispatch while `http_pool[slot]` is still the client request. But a miss/stale entry is
  served from the **poll loop after the async origin fetch**, by which point `http_pool[slot]` has been
  reset/reused (the tell: the miss response was `HTTP/1.0` - `send_chunked`'s version fallback fires because
  `http_pool[slot].version` is no longer `HTTP_11`), so `Range` (and the version) were gone.
- **Fix:** capture the client `Range` header at **middleware time** (when `http_pool[slot]` is valid) into a
  per-slot owned buffer `EdgeCacheProxyCtx::range_hdr[MAX_CONNS][48]`, and resolve the window against that in
  `serve_hit`. Re-flashed: a cold-MISS `Range: bytes=15-35` now returns `206` + `Content-Range: bytes
15-35/83` + the byte-exact 21-byte window, and HIT/416/HEAD/`Accept-Ranges` all stay byte-exact.
- **Related (pre-existing, not fixed here):** the same `http_pool[slot]`-goes-stale-after-the-async-fetch
  root cause means a miss response is emitted as `HTTP/1.0` (`Connection: close` instead of keep-alive), and
  `store_response`'s `Vary`-value capture reads the stale request - so a `Vary` response cached on a miss
  stores an empty/garbage secondary key (it degrades to always-miss-and-refetch for that variant; never
  serves wrong content). Both are latent efficiency issues present since the RAM tier and want a holistic
  fix (preserve or snapshot the client request across the suspend); tracked in TODO.md.

---

## EdgeCache example opened no HTTP listener - server.begin() with no port

- **Status:** FIXED (2026-07-17). Found by the **first HW bring-up** of the edge cache on an ESP32-S3: the
  board joined WiFi and its TCP stack was alive (it RST'd port 80), but nothing served - `:80` was refused.
- **Symptom:** `GET /cdn/<path>` got a connection refusal; the board pinged fine and actively refused `:80`.
  A serial DIAG confirmed the edge cache itself was configured (`map=1`, `begin=1` listeners) but no HTTP
  port was open.
- **Root cause:** the example called `server.begin()` with no port and no prior `server.listen()`. `begin()`
  with no argument requires a registered listener (else it returns `PC_ERR_NO_LISTENERS` and starts
  nothing); the single-HTTP-port form is `server.begin(80)`. A library-contract slip in the example, not a
  library defect.
- **Fix:** `server.begin(80)` in `examples/L7-Application/EdgeCache`. Re-flashed; the full
  MISS -> HIT -> REVALIDATED(304) -> purge path then passed byte-exact against a real origin. Exactly the
  class of whole-firmware integration bug that host mock-seam tests miss and a real HW run catches.

---

## native_codeql (all-flags) test_dispatch 405/Allow tests fail - CSRF gate, hidden by CI

- **Status:** FIXED (2026-07-17). Found 2026-07-16 by the WSL native env actually _running_ the
  `native_codeql` suite. **CI never caught it** because the CodeQL workflow builds `native_codeql` with
  `pio test --without-testing` (compile-only, to trace the build) - its assertions have never executed in CI.
- **Symptom:** under the all-feature-flags `native_codeql` config, `test_get_route_advertises_head_in_allow`
  (POST to a GET-only route → expect `405` + `GET`/`HEAD` in Allow) and `test_405_includes_allow_header`
  (DELETE to a POST-only route → expect `Allow: POST`) both failed (`Expected Non-NULL`), while the same tests
  driven with **GET** (`test_method_mismatch_returns_405`, `test_405_allow_lists_all_methods_for_path`) and
  `HEAD` passed.
- **Root cause:** a **feature-flag interaction, not a dispatch bug** - the all-flags build enables
  `PC_ENABLE_CSRF`, and `csrf_gate` (protocore.cpp) runs _before_ the route loop and rejects any un-tokened
  state-changing method (`POST`/`PUT`/`PATCH`/`DELETE`) with `403`, so those requests never reach the
  §6.5.5 405/Allow dispatch. `GET`/`HEAD`/`OPTIONS` are CSRF-exempt, so they still 405. This is the intended,
  fail-closed security behavior (don't leak a path's allowed methods to an un-tokened request); the **test**
  simply didn't account for CSRF being on.
- **Fix:** the two unsafe-method tests now attach a valid `X-CSRF-Token` (via `feed_unsafe` + a suite-level
  `csrf_set_secret` in `setUp`, both `#if PC_ENABLE_CSRF`), so a legitimate token-bearing request reaches
  the 405/Allow dispatch; with CSRF off the request line is plain. Verified: `native_codeql:test_dispatch`
  11/11 (was 2 failed) and `native_app:test_dispatch` (CSRF off) still green.

---

## DTLS 1.3 HelloRetryRequest carried the TLS version codepoints (0x0303 / 0x0304) instead of DTLS (0xFEFD / 0xFEFC)

- **Status:** FIXED (2026-07-15). Found by **real-peer interop** the moment the HelloRetryRequest path was first
  driven end-to-end (wolfSSL DTLS 1.3 client leading with a non-X25519 group; `test/servers/dtls_wolfssl`). The
  HRR builder shipped in v6.17.0 but was never wired into the state machine until the HRR group renegotiation
  landed, so no released version ever sent an HRR - the bug was latent in unexercised code.
- **Symptom:** with the client offering only a non-X25519 key_share, the server correctly answered with a
  HelloRetryRequest, but wolfSSL rejected it with a protocol_version alert (`-326`, record layer version error).
  The direct one-round-trip path (client offers X25519 up front) worked, because its first server message is a
  ServerHello - which _did_ use the DTLS codepoints - while the HRR did not.
- **Root cause:** `tls13_build_server_hello` already took a `dtls` flag to emit `legacy_version` `0xFEFD` and
  `supported_versions` `0xFEFC` (RFC 9147 §5.3), but `tls13_build_hello_retry_request` - a separate builder for
  the same ServerHello structure - hard-coded `0x0303` / `0x0304`. The byte-exact HRR KAT (`native_dtls_tls13`)
  had pinned those TLS codepoints, so it stayed green: another self-referential KAT that shared the mistake.
- **Fix:** `tls13_build_hello_retry_request` gained the same `dtls` flag (`0xFEFD` / `0xFEFC` when set); the
  DTLS state machine passes `dtls=true`, and the HRR KAT was recomputed with the DTLS codepoints. Verified
  end-to-end: wolfSSL now completes the full handshake **through a HelloRetryRequest** and an application-data
  round trip (`HANDSHAKE OK (via HelloRetryRequest)` ... `INTEROP OK`).
- **Lesson:** exactly [the same lesson as the entry below](#dtls-13-used-the-tls-13-tls13--hkdf-label-prefix-instead-of-dtls13-plus-two-dtls-clienthelloversion-bugs) -
  a self-KAT proves self-consistency, not conformance. The moment a new code path (the HRR) was exercised
  against a real peer, it surfaced a deviation the green KAT had frozen in. Wire up real-peer interop for
  _every_ path, not just the happy one.

---

## DTLS 1.3 used the TLS 1.3 "tls13 " HKDF label prefix instead of "dtls13" (plus two DTLS ClientHello/version bugs)

- **Status:** FIXED (2026-07-15). Three DTLS-vs-TLS conformance bugs found by the first **real-peer interop**
  (wolfSSL DTLS 1.3 client vs. `dtls_conn`; `test/servers/dtls_wolfssl`). Shipped in v6.15.0 (record layer)
  and v6.18.0 (handshake).
- **Symptom:** the hand-rolled DTLS 1.3 server completed a self-consistent handshake against its own test
  client but could not interoperate with wolfSSL at all - the ClientHello failed to parse, then (after that
  was fixed) the handshake was rejected on version, then (after that) the AEAD open of the peer's first
  encrypted record failed because the derived keys diverged.
- **Root causes (all DTLS-specific deviations from TLS 1.3 that the self-referential host KATs could not
  catch, since the KATs shared the implementation's assumptions):**
    1. **`legacy_cookie`** (RFC 9147 §5.3): the DTLS ClientHello carries an extra `legacy_cookie` field between
       `legacy_session_id` and `cipher_suites`; the shared `tls13_parse_client_hello` (written for QUIC) skipped
       it, so it read `cipher_suites` at the wrong offset and the parse failed.
    2. **Version codepoints** (RFC 9147 §5.3): DTLS 1.3 advertises `0xFEFC` in `supported_versions` (not the TLS
       `0x0304`) and puts `legacy_version` `0xFEFD` in the ServerHello. The server checked for `0x0304` and sent
       a TLS ServerHello, so wolfSSL rejected it with a protocol_version alert.
    3. **HKDF-Expand-Label prefix** (RFC 9147 §5.9): DTLS 1.3 replaces the `"tls13 "` label prefix with
       `"dtls13"` in **every** Expand-Label - both the key schedule (traffic secrets, Finished) and the record
       `key`/`iv`/`sn`. The DTLS code reused `quic_hkdf_expand_label`, which hard-coded `"tls13 "`, so every
       DTLS secret was wrong. Diagnosed by dumping our handshake-traffic secret and comparing to wolfSSL's
       `SSLKEYLOGFILE`, then bisecting transcript vs. key-schedule in an independent Python reconstruction.
- **Fix:** (1) `tls13_parse_client_hello` takes a `dtls` flag that skips `legacy_cookie`; (2)
  `tls13_build_server_hello` + `supported_versions` parse use the DTLS codepoints under the same flag; (3) the
  label prefix became a first-class KDF variant - `Tls13Kdf` (`TLS13_KDF` / `DTLS13_KDF`), bound once into the
  `Tls13KeySchedule` and passed to the record-key derivation, replacing the hard-coded prefix (no `bool dtls`
  threaded through the schedule). The record KAT was recomputed with `"dtls13"`. Verified end-to-end: wolfSSL
  now completes the handshake **and** an application-data round trip (`INTEROP OK`).
- **Lesson:** a byte-exact KAT pinned to your _own_ independent reimplementation proves self-consistency, not
  conformance - it shares your misreadings of the spec. A real reference peer (or vectors from one) is the
  only thing that catches a wrong-but-consistent assumption. Every new wire protocol needs a real-peer interop
  check, not just a self-KAT.

---

## SSH server KEXINIT (I_S) overflowed its 512-byte store once ecdh-sha2-nistp256 was advertised

- **Status:** FIXED (2026-07-15). Found while adding the `ecdh-sha2-nistp256` KEX (v6.14.0).
- **Symptom:** advertising one more KEX algorithm made `ssh_kexinit_build()` return `-1` in some builds; two
  native tests (`test_begin_rekey_preserves_session_and_auth`, `test_ssh_transport_more_guards`) failed with
  a stack-smash abort (SIGQUIT) once the payload crossed the buffer edge.
- **Root cause:** `SSH_KEXINIT_S_MAX` was `512`, exactly enough for the _previous_ advertised suite. Adding
  `ecdh-sha2-nistp256,` (19 bytes) to `kex_algorithms` pushed the worst-case server KEXINIT (PQC hybrid + zlib
  s2c + all three host-key types + the full cipher/MAC lists ~= 580 bytes) past 512, tripping the `w.len >
SSH_KEXINIT_S_MAX` guard (which had been marked "never exceeds"). The production packet buffer
  (`SSH_PKT_BUF_SIZE = 2048`) was fine; the fixed `i_s[]` store and one test's local 512-byte buffer were not.
- **Fix:** raised `SSH_KEXINIT_S_MAX` to `704` (headroom over the ~580 worst case) and grew the test's local
  `kbuf` to 1024. Also corrected `test_kexinit_parse_rejects_missing_kex`, which had used `ecdh-sha2-nistp256`
  as its example of an _unsupported_ KEX - now `ecdh-sha2-nistp521`.
- **Lesson:** a buffer sized to _exactly_ fit today's advertised algorithm lists breaks the next time a list
  grows. Size protocol-list buffers to the theoretical worst case with headroom, and never mark a length guard
  "unreachable" - it is one algorithm away from firing.

---

## InterfaceBridge: ESP32 Build linked without its feature flag - pc_bridge_publish undefined

- **Status:** FIXED (2026-07-15, commit 90e5a972).
- **Symptom:** the **ESP32 Build** CI job for `InterfaceBridge` failed at link with an undefined reference to
  `pc_bridge_publish()` - chronically red since the example shipped (v6.8.0).
- **Root cause:** the ESP32 Build discovers each example's `build_flags` by scraping the first documented
  `pio ci` command from its `README.md` (`ci_tooling/generate/example_footprints.py`). InterfaceBridge's README had no
  such command, so CI built it with _empty_ flags: the library's `iface_bridge_hw.cpp` guards its body under
  `#if PC_ENABLE_IFACE_BRIDGE`, so with the flag absent `pc_bridge_publish()` compiled to nothing while the
  sketch (which sets the flag only in its own translation unit) still referenced it. An in-sketch `#define`
  never reaches the separately compiled library.
- **Fix:** added the standard `## Build` section with `-DPC_ENABLE_IFACE_BRIDGE=1` to the README, matching
  every other feature-gated example. Verified `pio ci` links on a real ESP32 (esp32dev, 59.5% flash) and the
  ESP32 Build CI turned green.
- **Lesson:** when the build system derives config from docs, a missing doc line is a silent build break. Every
  feature-gated example needs its `pio ci` build-flag command in the README, not just an in-sketch `#define`.

---

## Protocol dispatch: PROTO_BRIDGE (and any ConnProto id >= 8) silently never registered

- **Status:** FIXED (2026-07-14). Found while adding `PROTO_NTRIP_CASTER = 9`.
- **Symptom:** the interface-bridge listener (`PROTO_BRIDGE = 8`, shipped v6.8.0) would accept connections but
  its handler was never invoked - the dispatch table returned no handler for the slot - so a bridged port did
  nothing. Latent because the v6.8.0 verification was a host codec test + a compile, not a live PROTO_BRIDGE
  connection.
- **Root cause:** `PC_PROTO_MAX` (the dispatch-table size) was `8`, and both `proto_register()` and
  `proto_get()` bound-check with `(unsigned)proto < PC_PROTO_MAX`. `PROTO_BRIDGE = 8` fails `8 < 8`, so the
  handler was neither stored nor fetched. Adding a ConnProto id at/above the table size silently disabled it.
- **Fix:** raised `PC_PROTO_MAX` to `10` and added a `static_assert((unsigned)ConnProto::PROTO_NTRIP_CASTER
< PC_PROTO_MAX, ...)` next to the enum's config so any future proto that outgrows the table is a compile
  error, not a silent no-op.
- **Lesson:** a fixed-size table indexed by an enum needs a static_assert pinning the size to the enum's max; an
  off-by-the-newest-value ceiling is invisible without one, and "compiles + host test passes" does not exercise
  runtime registration. Verify a new listener with a live connection, not only a compile.

---

## W5500: large transfers crash / truncate - marginal SPI signal integrity, NOT a library defect

- **Status:** NOT A LIBRARY BUG (hardware; 2026-07-13). Root-caused by JTAG + an SPI-clock sweep, resolved by
  clean wiring + a conservative clock. Logged so it is not re-chased as a software bug.
- **Symptom:** on an ESP32-S3 + W5500 (breadboard jumpers), a large streamed download (`send_chunked`) crashed
  the board mid-transfer; curl saw a connection reset (`rc=56`). Small requests succeeded.
- **Investigation:** a JTAG break-in under load caught a **TLSF heap-corruption panic**
  (`assert block_is_free ... "block must be free"`) inside `emac_w5500_task -> esp_pbuf_allocate -> mem_malloc`
    - the W5500 driver's RX path, entirely upstream, with **zero library frames**. The corrupt tlsf was in PSRAM.
      Initial hypotheses (our send path flooding lwIP; pbufs spilling to a PSRAM heap) were **disproved** by
      instrumenting the live heap: across clean 50 MB and 200 MB transfers the internal heap never drained, **PSRAM
      was never allocated from** (`psram_free` flat), and `heap_caps_check_integrity_all` stayed OK. Our
      `chunk_send_pump` already honors `tcp_sndbuf` backpressure and is zero-heap.
- **Root cause:** marginal **SPI signal integrity**. After the W5500 was rewired in isolation (off shared
  breadboard rails / USB3 noise), the same firmware streamed **200 MB byte-exact** with a flat heap. An SPI-clock
  sweep then reproduced the failure **on demand**: clean to ~24 MHz sustained, truncation at 40-60 MHz, and at
  80 MHz the W5500 chip-ID read back `0x82` instead of `0x04` - a corrupted SPI read. A corrupted frame length
  from a bad SPI read makes the driver over-copy into a pbuf, which is what corrupts the heap. So the "crash" was
  a downstream symptom of bad wiring, not a code defect.
- **Fix / mitigation:** added `PC_ETH_W5500_SPI_MHZ` (default 20, the safe/upstream value proven at 200 MB) so
  the clock can be tuned to the wiring; documented the SPI-bound throughput curve and the signal-integrity ceiling
  in FEATURE_PERFORMANCE.md / HARDWARE_HOOKUP.md. No library code was at fault.
- **Lesson:** an upstream-only backtrace plus a **flat heap under load** points at hardware, not the library; the
  cheapest confirmation was an SPI-clock sweep that turned an intermittent crash into a monotonic, explainable
  signal-integrity curve. Overclocking a bus to force the failure beats guessing at a software cause.

---

## Transport: a large streamed response (chunked / file) truncates mid-transfer - the idle sweep reaps an actively-sending connection

- **Status:** FIXED (library, 2026-07-13; found by a 1 GB download benchmark against ESP32Async/ESPAsyncWebServer
  on an ESP32-S3, then reproduced deterministically).
- **Symptom:** a response whose body is much larger than one TCP window (`send_chunked` or `serve_file`) drops
  mid-stream at a **non-deterministic** point - observed once at 233 MB with a connection reset (curl `rc=56`) and
  once at 86 MB with a short clean close - while small requests to the same server keep succeeding. The same 1 GB
  download served by ESPAsyncWebServer completes fine.
- **Root cause:** the `CONN_TIMEOUT_MS` (5 s) idle sweep in `check_timeouts()` reaps any `CONN_ACTIVE` slot whose
  `last_activity_ms` is older than 5 s. That timestamp is refreshed on RX (recv callback) and on TX **ACK** (sent
  callback), so a healthy stream stays fresh - until a **transient send stall** (a Wi-Fi hiccup, or a brief full
  window with no ACKs) exceeds 5 s, at which point the sweep reaps a connection that is **actively mid-transfer**,
  truncating the body. (Same 5 s mechanism as the SSH "drops every framed packet after the banner" bug below, a
  different trigger.)
- **Fix:** a slot still paging out a body is active, not idle. The file/chunk send pumps now call
  `pc_conn_touch_active(slot)` each poll they run (they run every `handle()` loop through the `on_poll` seam),
  refreshing the idle timer so the sweep cannot reap an in-flight transfer. Dead-peer teardown for such a slot is
  delegated to lwIP's own retransmission timers, which abort a black-holed pcb through the err callback. The
  timestamp read that a size-based check would need lives on `tcpip_thread`, so refreshing from the worker-side
  pump (writing our own `last_activity_ms`) is the layer-clean signal.
- **Validation:** native `native` (334, incl. `test_transport`) + `native_keepalive` (11) + `native_range` (20)
  all pass. HW (ESP32-S3): a deterministic reproduction - pause the client 9 s (> 5 s) mid-stream with `SIGSTOP`
  so ACKs starve - truncates on the pre-fix build and **survives on the fixed build** (the transfer resumes on
  `SIGCONT` and keeps streaming). Regression test `test_active_send_not_reaped`.
- **Lesson:** an idle-timeout sweep must exclude a connection with an in-flight response - "no activity for N
  seconds" only means "idle" when nothing is being sent. A large-transfer **stress** test (not a happy-path smoke)
  is what exposes it; a small-response test never streams long enough to hit a stall.

---

## SSH: SERVICE_REQUEST accepted before key exchange completes - pre-encryption userauth bypass

- **Status:** FIXED (library, 2026-07-11; found by the pentest tool's `ssh_msgtype_abuse` against the S3 rig).
- **Symptom:** a client that sends its KEXINIT and then - instead of completing the key exchange - sends
  `SSH_MSG_SERVICE_REQUEST("ssh-userauth")` gets `SSH_MSG_SERVICE_ACCEPT` back and the server advances to the
  userauth phase, all in **cleartext** (no NEWKEYS, no session keys derived, no host-key verification).
- **Root cause:** the `SSH_MSG_SERVICE_REQUEST` case in `ssh_server_dispatch()` had **no phase guard** (unlike
  `KEXDH_INIT` and `USERAUTH_REQUEST`, which check their phase). It processed a service request in any phase,
  so a client could jump `SSH_PHASE_DH_INIT` -> `SSH_PHASE_AUTH`, skipping the entire key exchange. RFC 4253
  §10 requires the service request only after the key exchange (`ssh_newkeys_complete()` advances a fresh
  connection to `SSH_PHASE_SERVICE` and turns on encryption).
- **Fix:** guard the case with `if (s->phase != SSH_PHASE_SERVICE) return -1;`, so a premature service request
  is rejected and the connection closed. Regression test `test_service_request_before_newkeys_rejected`.
  HW-verified: the pentest `ssh_msgtype_abuse` attack goes from 1 finding to 0, and a real OpenSSH login
  (which sends SERVICE_REQUEST in the correct phase, after NEWKEYS) still completes 6/6.
- **Lesson:** every state-machine transition that grants a capability needs an explicit phase guard; the
  raw-socket adversarial client that ignores the normal message order is the thing that finds the gap - a
  well-behaved client (or a host test that drives the happy-path sequence) never would.

---

## SSH: server drops every framed packet after the banner - the dispatcher's emit callback is never wired

- **Status:** FIXED (library, 2026-07-11; root-caused + fixed with a real OpenSSH 10.0 client, tcpdump, and
  on-device counters over JTAG on an ESP32-S3).
- **Symptom:** a real SSH client connects, both sides exchange identification banners, the client sends its
  KEXINIT - then the connection is `reset by peer` ~5 s later and the server's KEXINIT never arrives (`ssh -v`).
  tcpdump: the device **ACKs** the 672-byte client KEXINIT (lwIP received it), then sends nothing for exactly
  5 s (`CONN_TIMEOUT_MS`) before the RST. No panic, no reboot - the device is healthy, it just never replies.
- **Root cause:** `ssh_conn_setup()` - which installs the SSH dispatcher's binary-packet emit callback via
  `ssh_server_set_emit_cb(ssh_emit)` - had **no production caller**. It was declared, defined, and documented
  ("call from begin()"), but nothing ever called it, so `s_srv.emit_cb` stayed null. The server-identification
  banner is written directly by `ssh_conn_accept()` (not through the callback), so it still went out; but every
  _framed_ SSH packet - KEXINIT, KEXDH_REPLY, NEWKEYS, channel data - is emitted through the null callback and
  silently dropped, so the handshake stalls forever and the client is reset on the idle timeout. On-device
  counters confirmed the receive path was perfect (`rx_enter=2 bytes=713 disp=1 msg=20`, KEXINIT parsed + reply
  built `n=422`) while `SSHEMIT enter=0`: `ssh_emit` was never reached because the callback was null.
- **Why host tests missed it:** every SSH test wires the emit callback itself (`ssh_server_set_emit_cb(rec_emit)`
  in test_ssh_server; `ssh_conn_setup()` in test_ssh_conn's `setUp`) and then drives `ssh_server_dispatch` /
  `ssh_conn_rx` directly. The mechanism was covered; the **production wiring** was not - a mock-seam blind spot.
- **Fix:** `ssh_proto_handler()` (the one accessor every consumer goes through to install SSH) now calls
  `ssh_conn_setup()` before returning, so registering the handler always wires the emit callback - it can never
  be forgotten again. Regression guard `test_proto_handler_wires_emit` clears the callback, calls
  `ssh_proto_handler()`, drives a banner+KEXINIT, and asserts the server's reply reaches the socket (fails
  without the fix; verified). HW-verified end to end on an ESP32-S3 vs OpenSSH 10.0: curve25519-sha256 KEX,
  ssh-ed25519 host key, NEWKEYS, `Authenticated ... using "password"`, and a byte-exact channel echo over
  chacha20-poly1305.
- **Lesson:** a unit test that installs the production callback itself proves the mechanism but not that the
  mechanism is wired in production. Wire such one-time hookups at the single install seam (the handler
  accessor), not as a separate step a caller must remember.

---

## Native build: transport unit envs fail to compile - freertos/task.h has no host mock

- **Status:** FIXED (test infra, 2026-07-11).
- **Symptom:** `pio test -e native_ssh_conn` (and every native env that compiles `tcp.cpp`) fails at the build
  stage: `src/network_drivers/transport/tcp.c:31:10: fatal error: freertos/task.h: No such file or directory`.
- **Root cause:** the TLS tcpip-thread self-detection fix (babf01f4) added `#include "freertos/task.h"` (for
  `xTaskGetCurrentTaskHandle()`) to `tcp.cpp` unconditionally, but the host build resolves `freertos/*` through
  `test/mocks/freertos/` and only `FreeRTOS.h` + `queue.h` were mocked - `task.h` was missing. Its symbols are
  used only inside `#if defined(ARDUINO)` code, so on the host the include just needs to resolve.
- **Fix:** added `test/mocks/freertos/task.h` (typedef `TaskHandle_t` + an `xTaskGetCurrentTaskHandle()` stub),
  matching the existing host-mock pattern. All native transport/session/SSH envs build again.

---

## HTTP/3: QUIC frame parser rejects standard post-handshake frames - real clients get FRAME_ENCODING_ERROR

- **Status:** FIXED (library, 2026-07-11; found + fixed same day with an aioquic client on the PSRAM board).
- **Symptom:** `QUIC handshake: CONNECTED`, then the device sends CONNECTION_CLOSE `error_code=0x07`
  (FRAME_ENCODING_ERROR) and the h3 `GET /` times out. aioquic's event log:
  `ConnectionTerminated(error_code=7, frame_type=0)`.
- **Root cause:** `quic_frame_parse()` (`quic_frame.cpp`) only decodes PADDING, PING, HANDSHAKE_DONE, ACK,
  ACK_ECN, CRYPTO, STREAM (0x08-0x0f), MAX_DATA, and CONNECTION_CLOSE; for **every other frame type it
  returns 0** (line ~122, "a frame type this minimal server does not handle"), and `process_frames()`
  turns a 0 into FRAME_ENCODING_ERROR + closes the connection (quic_conn.cpp:193). But a real QUIC client
  sends connection-management + flow-control frames right after the handshake - **MAX_STREAMS (0x12/0x13),
  MAX_STREAM_DATA (0x11), NEW_CONNECTION_ID (0x18), NEW_TOKEN (0x07), DATA_BLOCKED (0x14),
  STREAM_DATA_BLOCKED (0x15), STREAMS_BLOCKED (0x16/0x17), RESET_STREAM (0x04), STOP_SENDING (0x05),
  RETIRE_CONNECTION_ID (0x19), PATH_CHALLENGE/PATH_RESPONSE (0x1a/0x1b)** - so the very first 1-RTT packet
  carrying the h3 request also carries one of these and the whole packet is rejected. RFC 9000 requires an
  endpoint to be able to parse every defined frame type (it may ignore the ones it does not act on);
  returning FRAME_ENCODING_ERROR for a well-formed known frame is both a spec violation and an interop
  break with any real client.
- **Fix:** `quic_frame_parse()` (+ named constants in `quic_frame.h`) now **consumes** (skips) all the
  standard frame types above with their correct varint/byte layout, so they parse successfully and the
  dispatcher ignores the ones with no server-side action (like it already does for MAX_DATA/PING). Grouped
  by wire shape (1/2/3 varints; length-prefixed NEW_TOKEN/NEW_CONNECTION_ID; fixed-width
  PATH_CHALLENGE/RESPONSE). HW-verified with aioquic: `h3 GET / -> :status=200` (HeadersReceived +
  DataReceived, body served over QUIC). HTTP/3 device-as-server now works end to end.
- **Lesson:** "a minimal server only parses what it acts on" is wrong for QUIC - the transport must parse
  the whole frame grammar even to ignore it, or the first real-client packet closes the connection.

---

## HTTP/3: QUIC handshake crashes the device - worker-task stack too small for Ed25519 signing

- **Status:** FIXED (library, found on hardware 2026-07-11 bringing up the HTTP/3/QUIC server on the PSRAM
  board; the first QUIC handshake panicked the board).
- **Symptom:** `h3_cert()` + `begin()` come up (`h3_cert=1`, `BEGIN=1`) and the QUIC listener binds UDP/443,
  but the first client handshake reboots the board: `Guru Meditation ... Core 1 panic'ed (Unhandled debug
exception)` - a task **stack canary** trip, no clean assert message.
- **Root cause (JTAG addr2line of the backtrace):** the crash is deep in Ed25519 signing during the QUIC
  TLS-1.3 CertificateVerify: `quic_server_poll -> quic_conn_recv -> quic_tls_recv_crypto ->
process_client_hello -> tls13_build_cert_verify -> ssh_ed25519_sign -> ed_scalarbase/scalarmult/add ->
ssh_gf_mul`. The QUIC TLS-1.3 handshake **reuses the SSH ed25519 signer**, whose software field
  arithmetic peaks at ~10.5 KB of stack. `quic_server_poll` runs on the **worker task**, whose default
  stack is only 8 KB unless SSH is enabled - and there was a compile guard forcing >= 12 KB
  (`PC_WORKER_STACK_CURVE_MIN`) for `PC_ENABLE_SSH` but **not for `PC_ENABLE_HTTP3`**, even though
  HTTP/3 exercises the same signer. So an HTTP/3-without-SSH build got the 8 KB default and overflowed.
- **Fix:** `protocore_config.h` - `PC_ENABLE_HTTP3` now bumps the default `PC_WORKER_TASK_STACK` to 12 KB
  (same as SSH) and is included in the `PC_WORKER_STACK_CURVE_MIN` build guard. After the fix the QUIC
  handshake completes (`QUIC handshake: CONNECTED` from an aioquic client) with no crash.
- **Lesson:** a shared crypto primitive imposes its stack floor on **every** feature that reaches it -
  when a new caller (HTTP/3) reuses SSH's ed25519, it must inherit SSH's stack guard, not just the code.
  [[hw-testing-finds-integration-bugs]]

---

## TLS: handshake crashes on a core-locking lwIP core (arduino 3.x / PSRAM) - `tcp_write` called without the core lock

- **Status:** FIXED (library, found on hardware 2026-07-11 running HTTP/2-over-TLS on the rebuilt PSRAM
  core, IDF 5.5; the TLS handshake rebooted the board on every connection).
- **Symptom:** the TLS server starts, but the first record flush during the handshake panics/reboots:
  `assert failed: tcp_write ... "Required to lock TCPIP core functionality!"`. On the stock PlatformIO
  arduino-2.x core the same firmware handshakes fine.
- **Root cause:** lwIP has two threading models and the framework picks one. **Mailbox** (arduino 2.x /
  IDF 4.x, `CONFIG_LWIP_TCPIP_CORE_LOCKING` off): `tcpip_api_call` marshals the op to one dedicated
  `tcpip` thread. **Core-locking** (arduino 3.x / IDF 5.x, the PSRAM core, flag on): `tcpip_api_call`
  instead takes the core lock and runs the op **inline on the calling task**. `pc_tcp_marshal`'s
  "am I already in a safe context, so run inline instead of marshaling" test was `on_tcpip_thread()` =
  a **task-handle compare** (captured on the first `pc_tcp_do`). That is correct for the mailbox model,
  but under core-locking the captured "tcpip task" is just whichever task ran the first op, so the test
  false-positives for a normal caller (the handshake pump) and runs `pc_tcp_do` -> `tcp_write` **without
  holding the core lock** -> the assert. (This path never worked on core-locking cores; it is why CI only
  _compiles_ arduino 3.x. It surfaced now because the PSRAM core is the first core-locking build actually
  HW-run with TLS.)
- **Fix:** `tcp.cpp` `on_tcpip_thread()` now branches on `LWIP_TCPIP_CORE_LOCKING`. Core-locking: use
  lwIP's own holder query `sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER)` (the exact predicate
  `LWIP_ASSERT_CORE_LOCKED` uses) - a direct lwIP call is safe iff we hold the lock. Mailbox: the
  original task-handle compare, **byte-identical**, so the shipped 2.x path cannot regress.
- **Verified:** on the PSRAM/IDF-5.5 core the TLS handshake now completes and **HTTP/2 (ALPN `h2`) is
  served**: `curl -k` -> `HTTP 200 ver=2`, `openssl -alpn h2` -> `ALPN protocol: h2` (TLS 1.2),
  `curl --http2` -> `200`. Both static pools live in PSRAM (`s_h2`@0x3c0e0000, `s_pool`@0x3c0fcf30),
  internal DRAM 18%. This also fixes TLS on the stock arduino-esp32 3.x core (same core-locking model).
- **Lesson:** "am I in a context where a direct lwIP call is safe" is a **per-threading-model** question -
  detect the model, don't assume a dedicated tcpip thread exists. [[no-spaghetti-piping]]
  [[hw-testing-finds-integration-bugs]]

---

## TLS: device HARD-HANGS on a TLS 1.3-leading ClientHello (tcpip_thread self-deadlock in the close path)

- **Status:** FIXED (library, found on hardware 2026-07-11 bringing up the TLS device-as-server interop peer
  against an ESP32-S3; a real 1.3-leading client from `curl`/OpenSSL/Python wedged the whole board).
- **Symptom:** a forced-TLS-1.2 client handshakes and serves `200 OK` fine, but a default client (which leads
  with TLS 1.3) leaves the device **completely dead** - no ping, all ports closed, **no panic and no watchdog
  reboot** (a silent hard hang, not a crash). A single attempt could wedge it.
- **Root cause (caught live over JTAG/GDB):** the lwIP `tcpip_thread` self-deadlocks. A raw lwIP `sent`
  callback (`lowlevel_sent_cb`, which runs **in** `tcpip_thread`) finalizes a closing slot ->
  `pc_tls_conn_end()` -> `mbedtls_ssl_close_notify()` -> `server_bio_send()` -> `pc_conn_raw_send()` ->
  `pc_tcp_marshal(RAWSEND)` -> `tcpip_api_call()` -> `sys_arch_sem_wait()` **forever**: it marshals a raw
  write onto `tcpip_thread` and blocks on the mailbox semaphore that only `tcpip_thread` can post - but the
  caller _is_ `tcpip_thread`. The reentrancy guard `TransportCtx::in_tcpip_thread` was set only inside
  `pc_tcp_do()`, so raw lwIP callbacks (which never enter through `pc_tcp_do`) read it as `false` and
  wrongly re-marshal. (UDP got this right - its flag is set in the recv trampoline; TCP missed the raw
  callbacks.) Every task that then touches lwIP (the worker's `pc_conn_detach`) also blocks on the dead
  `tcpip_thread`, so the whole stack dies.
- **Fix:** `tcp.cpp` - replace the "inside `pc_tcp_do`" boolean with the **actual `tcpip_thread` task
  handle** (captured the first time `pc_tcp_do` runs) and compare `xTaskGetCurrentTaskHandle()` against it in
  `on_tcpip_thread()`. `pc_tcp_marshal()` now owns the context decision for **every** op: run `pc_tcp_do`
  inline when already in `tcpip_thread` (any raw callback), else `tcpip_api_call`. This is correct for raw
  callbacks that `in_tcpip_thread` never covered and kills the whole deadlock class, not just close_notify.
- **Lesson:** a reentrancy flag that only some entry points set is a latent self-deadlock; detect the thread
  by identity (task handle), not by "did I come through my own dispatcher." Mock-seam host tests can't see
  this - it only appears with a real TLS client on hardware. [[no-spaghetti-piping]] [[hw-testing-finds-integration-bugs]]

---

## TLS: a modern (TLS 1.3) ClientHello is refused because it exceeds the 1024 B RX ring

- **Status:** FIXED (library, found on hardware 2026-07-11, same bring-up; distinct from the deadlock above).
- **Symptom (after the deadlock fix):** the board no longer hangs but still **RSTs** any 1.3-leading
  ClientHello (`curl` -> `HTTP 000`, OpenSSL default -> handshake fails, read 0 bytes) while a `-no_tls1_3` /
  max-1.2 client succeeds. OpenSSL reported writing a **1533-byte** ClientHello.
- **Root cause:** a modern TLS 1.3 ClientHello (key shares + cipher/sig-alg lists + RFC 7685 padding) is
  ~1.5 KB and arrives in one TCP segment **larger than the whole `RX_BUF_SIZE` (1024 B) ring**. The recv
  callback refuses a segment that will not fit the ring (`ERR_MEM`, lossless backpressure), so a ClientHello
  bigger than the ring is refused **forever** and the handshake stalls until the idle-timeout reaper RSTs it.
  A 1.2-only ClientHello is small enough to fit, which is why it squeaked through. The ring was smaller than
  a single MSS segment - a latent limit a big handshake exposes (SSH's ~1.5 KB KEXINIT already had the same
  auto-upsize).
- **Fix:** `protocore_config.h` - when `PC_ENABLE_TLS` and `RX_BUF_SIZE` was left at its default, upsize it to
  **2048** (mirrors the existing SSH KEXINIT upsize). An explicit `RX_BUF_SIZE` build flag is honored. After
  both fixes: `curl` -> `200`, interop peer 7/7 (TLS 1.2 ECDHE-ECDSA-AES256-GCM-SHA384), 20/20 default
  handshakes negotiate 1.2 with the board staying alive, 10/10 rapid curls `200`.
- **Lesson:** the RX ring must hold at least one full MSS segment; a handshake whose first flight is one big
  segment (TLS ClientHello, SSH KEXINIT) will otherwise be refused-forever by all-or-nothing segment
  backpressure. [[stress-test-before-ship]]

---

## Rig route table overflowed MAX_ROUTES (16) - silently dropped /ws, /events, /secure, /syslog/probe

- **Status:** FIXED (rig-firmware config, found on hardware 2026-07-11 while adding the syslog `/syslog/probe`
  route - it 404'd even though the new firmware was live, since its `/bench` field was present).
- **Root cause:** `PC` has a fixed flat route table of `MAX_ROUTES` (default **16**); `server.on()`
  / `on_ws()` / `on_sse()` / `dav()` each consume one slot and **return false (silently) when the table is
  full** - the app does not check the return. The rig had grown to **20 registrations** (dav + 17 handlers +
  ws + sse), so the four registered after slot 16 (`/syslog/probe`, `/secure`, `/ws`, `/events`) never took
  effect. The overflow first occurred at the FTP tick (when the count crossed 16), silently disabling the
  rig's WebSocket + SSE + auth surface from then on (those dims were covered in earlier ticks, so no false
  coverage was claimed, but the rig had regressed).
- **Fix:** rig `platformio.ini` `-DMAX_ROUTES=28` (test-firmware config; not a library change). Verified on
  the rig: `/syslog/probe` now registers (7/7 interop), and `/secure` -> 401 + `/events` -> 200 are back.
- **Lesson:** this is not a library defect (the fixed table + false-on-full is the documented O(MAX_ROUTES)
  design), but `server.on()`'s return value is worth checking in apps that register many routes. Whenever a
  new rig route 404s while a fresh `/bench` field proves the new firmware is live, suspect the route table is
  full before anything else. Watch the count as more device-as-client probes accumulate.
- **Sequel (2026-07-11, NTP tick):** the **same class** bit the UDP side - `PC_MAX_UDP_LISTENERS` defaults
  to **2** (CoAP 5683 + SNMP 161 filled it), so `ntp_server_begin()`'s `pc_udp_listen(123)` returned false
  and the rig serial printed `NTP=bind-failed`. Fixed with `-DPC_MAX_UDP_LISTENERS=4`. Same rule: fixed
  pools that fail closed need their cap raised as the rig accretes protocols - check UDP listeners too, not
  just the HTTP route table.

---

## syslog MSG CR/LF passthrough (log-forging) - AUDITED, INFO-level (caller's responsibility, spec-permitted)

- **Status:** NOT A LIBRARY BUG (audited 2026-07-11 via the new `syslog_injection` attack; recorded INFO).
- **Observed:** `syslog_format()` copies the caller's MSG verbatim into the RFC 5424 line (`... - - - %s`),
  so a message containing CR/LF/control bytes is emitted as-is. At a collector that splits a stream/file on
  newlines this enables log forging (CWE-117): the attack sent `legit\r\n<34>1 - evil ... FORGED-RECORD` and
  the collector received it verbatim in one datagram.
- **Why it is INFO, not a finding:** over UDP (RFC 5426) **one datagram = one syslog message**, and RFC 5424
  §6.4 permits any characters in MSG, so a _compliant_ receiver treats the whole datagram as one MSG and does
  not split on `\n` - the passthrough does not break a compliant peer or violate the spec. CWE-117 output
  neutralization is the responsibility of the code that logs untrusted data (the caller), which is why the
  attack records it as INFO. The device stayed up and the fixed `PC_SYSLOG_MSG_MAX` (256 B) bound held
  (a 2 KB message was refused: `syslog_format` -> 0, no datagram, largest observed datagram 80 B).
- **Optional hardening (deferred, not done):** `syslog_format` _could_ replace control bytes in MSG with a
  safe char for defense-in-depth (many production syslog clients do). Left as caller responsibility per the
  library's "the app owns the log content" model; revisit if a user wants built-in sanitization.
- **Sequel (2026-07-11, statsd tick):** the **same class** applies to the StatsD client - `statsd_format`
  copies the metric name verbatim, and StatsD packs multiple `\n`-separated metrics per UDP packet, so a
  newline in the name forges extra metrics at a _compliant_ collector (and a `:` / `|` corrupts the
  `name:value|type` split). Recorded INFO by the `statsd_injection` attack; same verdict - the caller sanitizes
  the metric name. The fixed `PC_STATSD_LINE_MAX` (256 B) bound holds (a 2 KB name is dropped, largest
  observed datagram 51 B), so there is no over-read - only the app-level forging concern.

---

## Outbound-client-path first-touch heap drop (FTP + SMTP) - INVESTIGATED, NOT A BUG (one-time warmup)

- **Status:** NOT A BUG (investigated on hardware 2026-07-11 while adding the `ftp_malicious_server` and
  `smtp_malicious_server` attacks - both showed the identical signature).
- **Found:** the first-ever run of each device-as-client attack against a freshly-booted S3 rig flagged the
  heap-drift oracle: free heap fell ~1344-2016 B (e.g. FTP 131836 -> 130044; SMTP 131356 -> 130012) and
  **stayed down** past the 6 s settle, which the oracle reports as a determinism-promise violation ("no heap
  allocation after begin()").
- **Why it is not a leak:** the drop is a **one-time lazy warmup** of the outbound-client TCP path
  (`pc_client_*` -> lwIP), which `begin()` never exercises - the server listener path is warm at boot, but the
  first _outbound_ connect+send (and, for SMTP, the first rapid connect+error+close churn) grows lwIP's TX pbuf
  / working-set backing once. Proof it is bounded, not monotonic: (1) for FTP, 8 more failed-open `/ftp/probe`
    - 8 more `/redis/probe` connections dropped **nothing** further (plateaued at 130044); (2) the **second and
      third identical attack runs were clean - 0 findings**, heap steady within a ~4 B jitter (FTP 130044 ->
      130044; SMTP 130012 -> 130008 -> 130012). A real per-connection leak would fall by another ~1.5 KB each run.
      The library's own code allocates nothing after `begin()`; the one-time growth is inside esp-idf/lwIP pools
      and is capped.
- **Lesson:** the heap-drift oracle's first-touch false-positive fires the first time _any_ never-before-used
  code path runs after begin() (here the outbound-client path). Distinguish warmup from a leak by re-running:
  warmup plateaus immediately and the second run is clean; a leak is monotonic. The MQTT/Redis device-as-client
  probes were judged clean earlier only because their path was already warm by the time the oracle sampled.
  (SMTP's warmup fired even after the interop happy-path had run, because the attack's error/close churn hits a
  slightly different lwIP allocation - so re-run per _attack_, not just per code path.)

---

## SSE teardown slot leak wedges the whole HTTP server (sse_free never called)

- **Status:** FIXED (found on hardware 2026-07-11 by the pentest rig's new `sse_exhaustion` attack).
- **Found:** `penetration_testing/pc_pentest.py --only sse_exhaustion` against the S3 rig (`penetration_testing/rig_firmware`,
  pinned `espressif32@6.13.0`, MAX_CONNS=4, MAX_SSE_CONNS=2). A **single clean run** on a freshly-booted,
  healthy board (heap 252636) drove the server permanently unresponsive: `/health` -> `56` then `28`
  (timeout), no recovery past `CONN_TIMEOUT_MS` (5 s), and **no crash/reboot** (serial clean) - a hard wedge.
- **Symptom:** one burst of `GET /events` connections beyond `MAX_SSE_CONNS` and the device stops answering
  every endpoint, forever, without rebooting (so no watchdog catches it). A remotely-triggerable permanent DoS.
- **Root cause (JTAG-confirmed):** `sse_free()` had **zero callers** - dead code. WebSocket teardown is wired
  (`ws_free()` in `protocore.cpp` handle loop), but SSE had no equivalent, so a closed / idle-reaped / aborted
  SSE stream never released its `sse_pool` entry. An SSE upgrade also leaves the slot as `ConnProto::PROTO_HTTP`
  (SSE is a long-lived HTTP response, not a protocol switch), so the leaked binding persists on the HTTP slot.
  The kill step is in `http_poll_slot()`: `if (sse_find(i)) return;` skips HTTP dispatch for a slot it believes
  is a live SSE stream. Once `sse_pool` is full of leaked entries (slot_id 0,1), any **new** HTTP connection
  reusing conn slot 0 or 1 matches the stale `sse_find()` and is silently never dispatched -> the client hangs.
  Live JTAG on a wedged board: `sse_pool[0]`+`sse_pool[1]` both `active` (paths `/events`), `conn_pool[1]`
  already `CONN_FREE` (stale binding to a freed slot), a `GET /health` curl sat unparsed in a slot's rx ring,
  and `conn_pool[0].last_activity_ms` climbed 163048 -> 423695 across two halts (each hung-then-retried
  connection refreshed it), so the idle sweep never reaped it. `loopTask`/`worker`/`tcpip_thread` all alive.
- **Fix:** `src/network_drivers/presentation/presentation.c` - new `http_release_upgrade_bindings(slot)` calls
  `ws_free(slot)` + `sse_free(slot)` (both no-ops when unbound). Invoked from `http_evt_close()` (FIN/RST/error
  on an SSE or WS slot frees its binding) **and** `http_conn_open()` (a reused slot must not inherit a stale
  binding - covers the idle-sweep / abort free paths that never fire a close event). Verified on the rig: the
  exact `sse_exhaustion` burst that permanently wedged the board now recovers (`/health` -> 200 within ~5 s);
  native `test_sse` (+2 regression tests) and `test_presentation` green.
- **Lesson:** every presentation-layer binding (WS/SSE) needs a teardown wired to **every** transport free path,
  not just the graceful protocol-close path; clean up stale bindings at slot **reuse** to cover the direct-free
  paths (idle sweep, abort) that never emit a close event. Also: the liveness oracle must settle-recheck so a
  permanent wedge (stays down) is distinguished from a connection-holding attack's transient pool saturation
  (recovers) - the same discriminator that proved the fix.

---

## Basic-auth password compared with strcmp: NUL-truncating + not constant-time

- **Status:** FIXED (found on hardware 2026-07-11 by the pentest rig's `auth_bypass` attack).
- **Found:** `penetration_testing/pc_pentest.py --only auth_bypass` against the S3 rig. Sending
  `Authorization: Basic base64("admin:admin\x00GARBAGE")` returned **200** on the protected route.
- **Symptom:** a submitted password of the form `correct-password` + `\0` + junk authenticated, because
  `check_basic_auth` compared the password with `strcmp(pass, r->auth_pass)` and `strcmp` stops at the
  first embedded NUL - so `"admin\0GARBAGE"` compared equal to `"admin"`.
- **Severity:** LOW as a direct exploit (the attacker still has to submit the correct password prefix, so
  it grants no access they could not already get), but a real robustness + **timing-side-channel** weakness:
  `strcmp`/`memcmp` early-out, leaking via response timing how many leading credential bytes matched, and the
  NUL truncation means the decoded credential length was not validated.
- **Root cause:** `src/network_drivers/application/auth/auth.c check_basic_auth` used `memcmp` for the username (length-checked, ok)
  but `strcmp` for the password, and never bounded the password to its decoded byte length.
- **Fix:** compute the real password byte length (`plen = n - (pass - decoded)`) and compare BOTH fields with
  a new constant-time, length-bounded `ct_equal()` - so an embedded NUL cannot truncate the compare and the
  byte loop always runs to completion (no timing early-out). Verified: native test_auth/test_digest_auth green
  (261), and on the rig the null-truncation vector now returns 401.
- **Lesson:** never compare a secret with `strcmp`/`memcmp` - use a length-bounded constant-time equality.
  A password check must be validated against its actual byte length, not a C-string terminator.

---

## Connection pool wedges (no recovery) under a saturation + RST-race flood - was masked by the crash

- **Status:** FIXED (library) - the remediation is implemented at
  `src/network_drivers/transport/tcp.c` `lowlevel_recv_cb`: the idle-timer refresh
  (`slot->last_activity_ms = pc_millis()`) now sits **after** the backpressure check, and the
  refused-segment branch explicitly does not refresh (it returns `ERR_MEM` without touching the
  timer), so a no-progress connection idle-times-out and is reaped. HW re-attack with the pentest
  rig (`http_conn_saturation` + oversized lines, then confirm the pool recovers past CONN_TIMEOUT_MS)
  is the remaining validation.
- **Found:** re-attacking the S3 rig after the stale-pcb crash fix below. `http_conn_saturation` + a burst of
  oversized requests closed with SO_LINGER=0 (RST) leaves the device **pingable but not serving HTTP**
  (curl -> `000`), and it does **not** recover on its own past `CONN_TIMEOUT_MS` (5 s) - only a reset brings
  the server back. A fresh boot serves normally (`/health` -> 200), so the fix does not break normal HTTP.
- **Why it surfaced now:** the stale-pcb crash used to **reboot and self-heal** this exact wedge; fixing the
  crash removed that accidental recovery, exposing that the 4-slot pool's slots do not free under abnormal
  (RST-race / half-open saturation) teardown. Fixing one bug uncovered the one it was hiding.
- **Root cause (JTAG-confirmed):** the idle-timeout timestamp is refreshed on _raw recv activity_ instead of
  _accepted-data progress_. `lowlevel_recv_cb` set `slot->last_activity_ms = pc_millis()` **before** the
  backpressure check. An oversized request line (or any body larger than `RX_BUF_SIZE`) fills the RX ring; the
  segment is refused (`ERR_MEM`, kept as lwIP `refused_data`) and **redelivered every retransmit**. Each
  redelivery refreshed `last_activity_ms`, so `check_timeouts` always saw the slot as recently active and never
  reaped it. Live JTAG breakpoint at the reap check on a wedged slot: `now=810066`, `last_act=806956`,
  `diff=3110 < timeout=5000` (and `last_act` climbing 358768 -> 806956 across samples = being refreshed). Four
  such slots leak and permanently wedge the 4-slot pool. A Slowloris-class DoS.
- **Fix:** move the `last_activity_ms` refresh in `lowlevel_recv_cb` to **after** the backpressure check, so a
  refused/redelivered segment does not refresh the idle timer - only data actually accepted into the ring (real
  progress) does. A no-progress connection now idle-times-out and is reaped; a legitimately backpressured
  connection the worker is draining still refreshes on each accepted segment.
- **Lesson:** (1) a crash that reboots can _mask_ a resource-leak DoS - fix crashes, then re-attack to find what
  the reboot was hiding. (2) an idle/liveness timer must be driven by _progress_, not raw I/O events: refreshing
  on a refused/retransmitted segment lets a stalled peer hold a slot forever.

---

## Oversized request line / connection saturation reboots the device (tcp_output on a stale pcb)

- **Status:** FIXED (library) - the remediation is implemented in
  `src/network_drivers/transport/tcp.c` `pc_tcp_do`: `pcb_still_bound()` (and the O(1)
  `k->pcb == conn_pool[k->slot].pcb` check for the slot-carrying SEND/OUTPUT ops) re-validates the
  captured pcb at execution time on `tcpip_thread`; a stale pcb skips with `ERR_CLSD` instead of
  calling `tcp_write`/`tcp_output` on freed memory. HW re-attack with the pentest rig
  (`http_oversized_request_line` + `http_conn_saturation`, confirm no panic + no heap drift) is the
  remaining validation.
- **Found:** `penetration_testing/pc_pentest.py --host <rig> --diag` (attacks `http_oversized_request_line` +
  `http_conn_saturation`) against the ESP32-S3 rig firmware (`penetration_testing/rig_firmware`, pinned
  `espressif32@6.13.0`, MAX_CONNS=4). Reproduced standalone with ~15 oversized-request-line connections.
- **Symptom:** the device **panics and reboots**. Serial:
  `assert failed: tcp_output .../lwip/src/core/tcp_out.c:1249 (tcp_output: invalid pcb)` + backtrace +
  `rst:0xc (RTC_SW_CPU_RST)`. The determinism oracle also saw free heap drift down (251904 -> 245136)
  before the crash, and legitimate requests hang while the pool is saturated.
- **Root cause:** `src/network_drivers/transport/tcp.c:232`, `pc_tcp_do()` (the `tcpip_api_call`
  marshalled raw-lwIP op) calls `tcp_output(k->pcb)` - and `tcp_write(k->pcb, ...)` for SEND/RAWSEND -
  **without re-validating that `k->pcb` is still live**. The worker captures the pcb, then marshals the
  op to `tcpip_thread`; in between, the connection can be torn down (RST / lwIP error callback / close
  nulls `conn_pool[slot].pcb`), leaving `k->pcb` stale. `tcp_output` on the freed/closed pcb trips lwIP's
  assertion and panics. Symbolized via `addr2line` on the `-g` rig `firmware.elf`:
  `0x420074c6 = pc_tcp_do (tcp.cpp:232) -> tcp_output (tcp_out.c:1251) -> __assert_func`.
- **Fix:** (implemented) re-validate the pcb at execution time inside `pc_tcp_do` for the SEND /
  RAWSEND / OUTPUT ops - skip with `ERR_CLSD` when the captured pcb is no longer the slot's live pcb
  (`pcb_still_bound()` scans the pool for RAWSEND, whose slot is 0; SEND/OUTPUT do the O(1)
  `k->pcb == conn_pool[k->slot].pcb` compare). Both reads are on `tcpip_thread`, where teardown also
  runs, so the compare is race-free. HW re-attack over JTAG to confirm the crash is gone is pending.
- **Lesson:** a marshalled/deferred raw-lwIP op MUST re-validate its pcb at execution time - the pcb it
  captured on another thread may have been freed by teardown before the op runs. Capturing a raw pointer
  across the worker -> tcpip_thread hop without a liveness re-check is a use-after-free waiting to happen.

---

## FTP command emitters used additive length checks that could wrap (buffer-overflow risk)

- **Status:** FIXED (native_ftp: 16 cases pass, byte-identical output; the change is behavior-preserving
  under the codec's own invariant and only hardens the helper against a hostile length).
- **Found:** 2026-07-10, SonarCloud flagged the additive bound in `ftp_emit`.
- **Symptom:** none in practice - every in-tree caller passes a `strnlen(_, cap)`-bounded length, so the
  sum never wrapped. The risk is latent: `ftp_emit`/`ftp_emit_uint`/`ftp_finish` are general helpers that
  take a raw `size_t` length, and a future caller passing a near-`SIZE_MAX` length would wrap the check.
- **Root cause:** the room check was written as `n + slen > cap`. With `n` and `slen` both `size_t`, a
  huge `slen` makes `n + slen` overflow to a small value that passes the check, after which
  `memcpy(buf + n, s, slen)` writes past `buf`.
- **Fix:** rewrote all three bounds as overflow-safe subtraction (`slen > cap - n`, `ri > cap - n`,
  `n >= cap`). The codec keeps the invariant `n <= cap` on every non-sentinel return, so `cap - n` can
  never underflow; the checks are now provably safe for any length.
- **Lesson:** never bound a write with `offset + len > cap` when `len` is (or could become) untrusted -
  use the subtraction form `len > cap - offset` and keep the `offset <= cap` invariant that makes it safe.

---

## server.listen() returned PC_OK instead of the listener id (port-forward never matched)

- **Status:** FIXED (HW: the relay/DNAT example now forwards a file byte-exact through the ESP32;
  native_app + native_relay pass with the corrected return).
- **Found:** 2026-07-10, on hardware, testing PortForward against a real HTTP origin.
- **Symptom:** a connection to the published front port was accepted then RST with nothing relayed;
  `relay_on_accept`'s bind lookup found no bind, so it closed the connection. The origin never saw the
  request. No handler output at all, which made it look like the event was dropped.
- **Root cause:** `PC::listen()` returned `PC_OK` - which is **1**, not 0 - but the relay
  example (and `relay_listener.h`'s own docs) treat the return as the listener id passed to
  `pc_relay_publish()`. `begin()` assigns the actual listener index (0 for the only listener), so the
  bind was stored under id 1 while the accepted slot carried id 0; `bind_by_listener()` missed.
- **Fix:** `listen()` now returns the listener id (its index, `_listener_count - 1`) on success; errors
  stay negative. Updated the two tests that asserted `== PC_OK` and the header doc.
- **Lesson:** an API whose documented use is "pass the return to publish()" must return that id, not a
  generic success sentinel - and `PC_OK == 1` turned it into an off-by-one that only bit when the
  listener was not index 1.

## SMB / DNC / relay / SMTP / SSH-forward could never connect (client transport stubbed out)

- **Status:** FIXED (HW: an ESP32-S3 SMB client now connects to a real Samba server - `pc_client_open`
  returns a valid slot instead of -1). Found by hardware testing; the host tests cannot catch it because
  they drive the engines through a mock send/recv seam, not the real `pc_client` transport.
- **Found:** 2026-07-10, first on-hardware run of the SmbFileClient example.
- **Symptom:** every outbound connection from an SMB (also DNC, relay/DNAT, SMTP, or SSH port-forward)
  build failed - `pc_client_open()` returned -1 on the very first call, so the feature silently never
  talked to its peer on device.
- **Root cause:** `client.cpp` compiles the real transport only under `#if defined(ARDUINO) &&
PC_NEED_DET_CLIENT`, else it falls through to a host stub whose `pc_client_open` returns -1.
  `PC_NEED_DET_CLIENT` was derived from only `HTTP_CLIENT || MQTT || WS_CLIENT`, omitting every newer
  feature that drives pc_client: the direct callers (relay, smtp, ssh port-forward) and the seam-based
  engines whose shipped example binds the seam to pc_client (smb, dnc). An SMB-only firmware got the stub.
- **Fix:** added `PC_ENABLE_RELAY || PC_ENABLE_SMTP || PC_SSH_PORT_FORWARD || PC_ENABLE_SMB ||
PC_ENABLE_DNC` to the `PC_NEED_DET_CLIENT` derivation (which also force-enables the shared DNS
  resolver), so any feature that needs the outbound transport pulls it in.
- **Lesson:** a "needs X" derived flag must list EVERY consumer, including features that reach the transport
  only through an example's seam binding; and a stub that returns an error instead of failing to link hides
  the omission until a board is on the bench.

## SMB client crashed the ESP32 during NTLMv2 auth (smb_open stack overflow)

- **Status:** FIXED (HW: the S3 read a file byte-exact - the fnv1a matched the server - after the buffers
  moved off the stack; native_smb host tests still pass).
- **Found:** 2026-07-10, on hardware, right after the pc_client fix let `smb_open` reach the real Samba
  NTLMv2 exchange.
- **Symptom:** "Guru Meditation Error ... Stack canary watchpoint triggered (loopTask)" inside
  `hmac_md5` <- `ntlm_ntowfv2` <- `smb_open`, in a boot loop. Host tests never saw it (the native stack is
  large and unguarded).
- **Root cause:** `smb_open` declared ~4 KB of working buffers on the stack (`tx` + `rx` = 2*PC_SMB_BUF,
  plus `nt_resp` + `ntauth` + `sp2` + `utf16` = 4*(PC_SMB_BUF/2)); `smb_read`/`smb_write` each add
  2*PC_SMB_BUF. With the caller's frame this overran the default 8 KB Arduino loopTask stack, tripping
  the canary during the deep NTLMv2 call chain.
- **Fix:** moved the large working buffers into one owned, feature-gated `SmbClientCtx` static (matching the
  library's owner-context pattern), leaving only small locals on the stack. The SMB dialogue is sequential
  (open -> read/write -> close) so a single shared working set is correct; documented as not reentrant
  across two concurrent SMB connections.
- **Lesson:** protocol clients with multi-KB working buffers must own them statically, not stack-allocate
  them - default embedded task stacks are ~8 KB and host tests will not reveal the overflow.

## Multipart parser truncated binary parts (strstr on a length-tracked binary body)

- **Status:** FIXED (native_app `test_binary_part_not_truncated` + all multipart cases pass; native_pentest
  38/38 clean under ASan + UBSan with the rewritten scan). Previously documented as a "known limitation";
  it is really a data-integrity bug and is now fixed.
- **Found:** 2026-07-10, reviewing the KNOWN_LIMITATIONS entry "a binary part containing the boundary bytes
  is truncated".
- **Symptom:** a multipart/form-data **file upload** whose body contains a NUL byte, or the raw boundary
  token (e.g. `--BND`) inside the payload, was truncated - the part's `data_len` stopped at the first NUL
  or the first boundary-looking bytes, corrupting binary uploads (images, firmware, ...).
- **Root cause:** `multipart_parse` scanned the body with `strstr` (`strstr(body, delim)` /
  `strstr(pos, "\r\n")`), which (1) stops at the first NUL even though `HttpReq::body` is a byte buffer with
  an explicit `body_len`, and (2) matched the bare `--boundary` bytes anywhere, so a payload that merely
  _contained_ those bytes (without the framing `CRLF`) was treated as a delimiter.
- **Fix:** rewrote the scan to be length-bounded over `body_len` with a binary-safe `mem_find` (memcmp, no
  NUL stop) and to match the full RFC 2046 `\r\n--boundary` delimiter for the data sections, so only a true
  `CRLF--boundary` ends a part. The in-place NUL terminator is kept as a convenience for text parts; binary
  parts are read via `part->data` + `part->data_len`.
- **Lesson:** never `strstr`/`strlen`-scan a buffer that is length-tracked and may hold binary - use a
  length-bounded `memcmp` search; and match the _full_ framed delimiter (`CRLF--boundary`), not the bare
  token, so payload bytes can never masquerade as a boundary.

---

## Signed-overflow UB + `10^exponent` DoS in the remaining hand-rolled number parsers

- **Status:** FIXED (native_pentest 35/35 clean under ASan + UBSan `-fno-sanitize-recover=all`,
  including new `pc_strtof` + GraphQL fuzz targets that feed huge exponents / integer literals;
  native_jwt 22/22 and native_exc_decoder 7/7 clean under UBSan). Completes the sweep started by the
  previous entry.
- **Found:** 2026-07-10, continuing the signed-`v*10` audit. Five more sites; two carried a second,
  worse bug: an exponent parsed into an `int` then applied as `for (k = 0; k < ex; k++) m *= 10.0` -
  a huge exponent (e.g. GraphQL `1e999999999`) is both signed-overflow UB **and** a denial-of-service
  (billions of iterations hang the device).
- **Sites + fix:**
    - `shared_primitives/numparse.h` `pc_strtof` and `services/iot/graphql/graphql.cpp` (query number
      literal): the exponent overflowed and its `10^ex` loop was unbounded - **clamped** the exponent
      (`if (ex < 400)` - 10^400 saturates the double to inf anyway), fixing UB + DoS. GraphQL's integer
      literal (`long long ipart`) also overflowed - now unsigned-accumulate.
    - `services/security/jwt/jwt.cpp` `jwt_claim_int` (untrusted numeric claim) and
      `server/exc_decoder.c` (crash-dump core id): the same signed `v*10` - fixed by
      unsigned-accumulate + reinterpret (jwt) / clamp (exc_decoder).
- **Not a bug:** `network_drivers/network/ip.cpp:54` matched the grep but is bounded - the
  `if (digits >= 3) return false` guard caps the octet at 3 digits (<= 999), so `val*10` never
  overflows. Left as-is.
- **Lesson:** the same as the previous entry, plus: an exponent applied by a `10^ex` **loop** is a DoS
  vector independent of the overflow - always clamp a parsed exponent to the type's real range.

---

## Signed-integer-overflow UB in three untrusted-input number parsers

- **Status:** FIXED (native_pentest now passes clean under ASan + UBSan with
  `-fno-sanitize-recover=all`: 33/33; the directly-affected suites - native_primitives,
  native_redis, native_snmp, native_http_client - all still green).
- **Found:** 2026-07-09, by the extended pentest fuzzer. Adding OPC UA Binary fuzz targets and
  running the built binary directly under `-fno-sanitize-recover=all` (so UBSan aborts instead of
  just printing) surfaced three latent undefined-behavior sites that earlier runs had been printing
  and ignoring. All three are hit by feeding a parser a very long / hostile digit string.
- **Sites + root cause:**
    - `services/net/snmp/snmp_ber.cpp` `ber_read_integer`: sign-extended the BER INTEGER by seeding a
      signed `long v = -1` and then `v = (v << 8) | byte` - **left-shifting a negative signed value
      is UB** (C++ < 20).
    - `shared_primitives/numparse.h` `pc_strtol`: `v = v * 10 + digit` on a signed `long` - **signed
      overflow is UB** once the digits exceed `LONG_MAX` (the unsigned `pc_strtoul` was already safe).
    - `services/redis_resp.cpp` RESP integer + double parsers: the same `v = v * 10 + digit` on a
      signed `int64_t` (the integer) and an unbounded `int exp` accumulator (the exponent).
- **Fix:** accumulate in unsigned and reinterpret with sign (BER integer, `pc_strtol`, RESP
  integer - `neg ? (T)(0 - uv) : (T)uv`, which also avoids the negate-`MIN` UB), and clamp the RESP
  double exponent (`if (exp < 1000000)` - a larger exponent saturates the `double` to inf/0 anyway).
  All fixes are value-identical for in-range inputs, so no test changed.
- **Lesson:** a hand-rolled `v = v*10 + digit` (or a `neg_seed << 8`) on a **signed** accumulator is
  UB the moment an attacker supplies enough digits; parse untrusted numbers into an unsigned type and
  reinterpret, or clamp. The fuzzer only caught these once it ran the sanitized binary with
  `-fno-sanitize-recover=all` - printing-but-not-failing UBSan output had hidden them.

---

## DNC decoder ate the EIA digit `3` (0x13) as an XOFF flow-control byte

- **Status:** FIXED (caught pre-ship by the codec's own encode -> decode round-trip test;
  never released). `native_dnc` 13/13; the program "M30" now round-trips through the EIA
  code intact.
- **Found:** 2026-07-09, on the first run of `test_roundtrip_program` for the new CNC DNC
  codec (services/machine_tool/dnc): the EIA round-trip of `M30` came back as `M0` - the `3` vanished.
- **Symptom:** decoding an EIA-coded program silently dropped every digit `3`; any block
  containing a `3` was corrupted.
- **Root cause:** the block decoder (`dnc_decode_feed`) filtered XON/XOFF (DC1 0x11 / DC3
  0x13) out of the byte stream as flow control - but in the EIA RS-244 tape code the data
  character `3` **is** 0x13. Filtering it from the forward program stream deleted the digit.
  The design conflated two different channels: the forward program data (sender -> controller,
  what the decoder reassembles) and the reverse flow-control channel (controller -> sender,
  XON/XOFF). They are opposite directions of a full-duplex link and must not share a filter.
- **Fix:** removed the XON/XOFF filtering from `dnc_decode_feed` entirely; the decoder now
  decodes the forward stream faithfully (0x13 = the data byte `3`). Flow control lives only in
  `dnc_flow_feed`, which the caller drives from the **reverse** channel's bytes. Added
  `test_decode_eia_three_is_not_xoff` as a regression guard.
- **Lesson:** an in-band control code (ASCII DC3) can collide with a data code in a different
  character set (EIA `3`); never filter control bytes out of a data stream that may legitimately
  contain that byte value. Keep flow control on its own channel.

---

## Owner-context grouping anchored the server TLS config + worker queue store (Arduino 3.x DRAM)

- **Status:** FIXED (WebSocketClient builds on the arduino-esp32 3.x core - 37% DRAM, was 408 bytes
  over; HTTPS/mTLS/TlsResumption/SecureWebSocket + SSH still build; native_ssh /
  native_crypto_kat 154/154).
- **Found:** 2026-07-07, after the scratch-arena fix cleared the ESP32 (PIO 2.x) build and 4 of the 5
  Arduino (3.x) failures. The last, `WebSocketClient` (a TLS WebSocket _client_), still overflowed
  `dram0_0_seg` by 408 bytes - but only on the arduino-esp32 3.x core, whose larger core footprint
  leaves less headroom than the 2.x core the PlatformIO job uses.
- **Symptom:** an outbound-only WebSocket client linked ~900 bytes of server-only state it never uses:
  the linked-map/cref diff (base vs HEAD) showed `tls.cpp` +600, `worker.cpp` +160, `listener.cpp`
  +145. Two more instances of the same gc-liveness trap as the scratch arena.
- **Root cause:** small always-referenced fields grouped into large owned `Ctx` symbols, so
  `--gc-sections` (per-symbol) could not drop the big cold parts:
    - `TlsServerCtx` bundled a 1-byte `ready` flag with the ~600-byte mbedTLS server config/cert/key/
      ticket. `pc_tls_ready()` (called on the client path) reads `ready`, anchoring the whole server
      config into a client that never runs `pc_tls_configure()`.
    - `DeferCtx` bundled the per-worker FreeRTOS queue **handles** (hot; `pc_defer()` pushes to them)
      with the multi-hundred-byte static queue **storage** (only `pc_workers_start()` touches it), so
      the storage stayed linked in a build that never starts workers.
- **Fix:** split each into a hot symbol + a cold symbol - `TlsServerReadyCtx s_srv_ready` apart from
  `TlsServerCtx s_srv`; `DeferStorageCtx s_defer_store` apart from `DeferCtx s_defer`. The cold halves
  are now referenced only by server-setup / worker-start code and garbage-collect out of client-only
  firmwares. Server examples that do use them are unchanged (HTTPS DRAM identical). Both new types
  end in `Ctx`, so the owner-context guard still passes. (`listener.cpp` +145 was left: the two splits
  already reclaimed far more than the 408-byte deficit.)
- **Lesson (reinforced):** verify on the tightest target - the arduino-esp32 3.x core overflows where
  the 2.x core has room. Keep large conditionally-used buffers in their own owned symbol, separate
  from small always-referenced fields. See the scratch-arena entry below for the first instance.

---

## Owner-context grouping anchored the 8 KB scratch arena, overflowing TLS-example DRAM

- **Status:** FIXED (all four TLS examples - HTTPS, mTLS, TlsResumption, SecureWebSocket -
  build for esp32dev again; native_ssh 146/146 and the SSH scratch-consumer build still pass).
- **Found:** 2026-07-06, RCA'ing why ESP32 Build / Arduino Build were red. Last green at `f7767ba6`,
  first red at `23e0797b` - both owner-context-refactor commits.
- **Symptom:** the four largest TLS examples failed to link: `.dram0.bss will not fit in region
dram0_0_seg`, overflowed by 944-1264 bytes. A per-object `.o` size diff showed only +62 bytes of
  DRAM growth across the whole tree, which could not explain a ~1 KB overflow.
- **Root cause:** a `--gc-sections` **liveness** change, invisible to a compiled-size diff (the object
  is compiled in both trees; only its _linkage_ differs). The scratch owner-sweep merged three separate
  symbols - the 8 KB per-worker bump `arena`, the `off[]` offsets, and `high_water[]` - into one
  `struct ScratchCtx s_scratch`. `session.cpp` calls `scratch_reset()` **every dispatch**, touching only
  `off[]`; but `off[]` and the arena were now one symbol/section, and `--gc-sections` is per-section, so
  that always-live reference anchored the whole 8 KB. Previously the arena was its own symbol, referenced
  only by `scratch_alloc()`, which is dead code in a plain TLS/HTTP build (its callers are SSH / WebSocket
  / OIDC) - so the linker dropped it. The linked-map diff was unambiguous: `scratch.cpp` contributed
  **8 bytes** of DRAM at `f7767ba6` and **8228 bytes** at HEAD. That +8 KB tipped the already
  DRAM-marginal TLS examples (~122 KB `dram0_0_seg` ceiling) over.
- **Fix:** split the arena back into its own owned instance (`struct ScratchArenaCtx s_scratch_arena`),
  keeping only the small `off[]`/`high_water[]` metadata in `ScratchCtx`. `scratch_alloc()` is the sole
  referrer of the arena, so a firmware that never allocates scratch garbage-collects both again; the
  always-live `scratch_reset()` anchors only the tiny metadata. Both are `*Ctx` types, so the
  owner-context guard still passes. Semantics are byte-for-byte unchanged.
- **Lesson:** when consolidating globals into one owned `Ctx`, keep a large _conditionally-used_ buffer
  in a **separate** owned symbol from small _always-referenced_ fields, or `--gc-sections` can no longer
  drop the buffer from builds that never use it. Measure regressions with the **linked** map, not a
  compiled `.o` size sum.

---

## Ed25519 verify accepted non-canonical S (signature malleability)

- **Status:** FIXED (native_crypto_kat green: the Wycheproof SignatureMalleability vectors now reject;
  native_ssh_ed25519 RFC 8032 7.1 regression still passes).
- **Found:** 2026-07-06, standing up the data-driven external crypto KAT env (native_crypto_kat) that
  runs Project Wycheproof vectors through the primitives. Wycheproof ed25519 tcId 63-70
  (`SignatureMalleability`) verified when they must be rejected.
- **Symptom:** `ssh_ed25519_verify` accepted a signature whose scalar S had been replaced by S + L
  (L = the group order). Because L\*B is the identity, `S*B - h*A` recomputes the same R, so the
  recompute-and-compare-R verification passed for both S and S + L - i.e. a third party could maul a
  valid signature into a different byte string that still verifies.
- **Root cause:** verification checked the group equation but never range-checked S. RFC 8032 5.1.7
  requires the verifier to reject a signature whose S is not in `[0, L)`; that check was missing.
- **Fix:** added `ed_scalar_canonical()` (compares the little-endian S against the group order `ED_L`
  from the top byte down) and reject up front in `ssh_ed25519_verify` when S >= L. Verification is
  public-data only, so a plain compare is fine. Legitimate signatures always have S < L (signing
  reduces mod L), so no valid vector or existing test changes.

---

## 54 shipped features were missing from the feature grid (FEATURES.md drift)

- **Status:** FIXED (gen_feature_tables.py coverage guard green; all PC_ENABLE\_\* flags now documented).
- **Found:** 2026-07-06, answering "did all the features make it into docs/FEATURES.md?".
- **Symptom:** 54 opt-in features had a `PC_ENABLE_*` flag, a `src/services/*` implementation, and
  tests, but no `##` heading in docs/FEATURES.md - so they were absent from the README/docs feature
  tables too (those tables are generated _from_ FEATURES.md). The whole industrial-protocol wave
  (HART, GOOSE, MMS, PROFINET, PROFIBUS, J2735, NTCIP, OpenADR, ...), HTTP/3, and several infra
  features (Failsafe, Sleep Scheduler, Wear Leveling, Network Adaptation, PSRAM Pool, Themes, ...)
  had shipped without ever being listed.
- **Root cause:** FEATURES.md is hand-maintained and nothing enforced that every feature flag has an
  entry. gen_feature_tables.py guarded the README tables against drift from FEATURES.md, but not
  FEATURES.md against drift from the config header, so shipped features silently never reached the grid
  (the same failure mode as the "silently lost 28 features" the generator docstring already warned of).
- **Fix:** backfilled all 54 entries (descriptions extracted verbatim from each flag's config-header
  doc comment) into FEATURES.md, mapped them to their OSI layer, regenerated the tables, and added a
  coverage guard to gen_feature_tables.py: it fails (in the Feature Tables CI job) if any
  `PC_ENABLE_*` flag lacks a FEATURES.md entry, excluding a small allowlist of internal derived
  flags (STREAM_BODY, CLIENT_TLS). Docs-only; no library code changed.

---

## HTTP/3 TLS flight append ignored its buffer cap (potential `flight_hs` overflow)

- **Status:** FIXED (native_quic_tls / native_tls13_msg / native_h3_e2e green; flagged by SonarCloud
  cpp:S3519 as two BLOCKER "memory copy overflows the destination buffer" findings).
- **Found:** 2026-07-06, reviewing the SonarCloud quality gate on the HTTP/3 handshake code.
- **Symptom:** the TLS 1.3 server flight is built message-by-message into a fixed `qt->flight_hs`
  buffer, each builder called with `sizeof(flight_hs) - flight_hs_len` as its capacity. `emit()` was
  handed that same capacity but ignored it (`(void)cap;`) and did `*plen += written` unconditionally.
- **Root cause:** nothing bounded `flight_hs_len` against `sizeof(flight_hs)`. In the correct flow each
  builder returns `<= cap`, so the sum stayed in bounds, but the invariant was never enforced: if a
  builder ever returned more than the remaining room, a later builder's `sizeof(flight_hs) -
flight_hs_len` would underflow to a huge `cap` and `w_bytes()` would `memcpy` past the fixed array.
- **Fix:** `emit()` now honors the `cap` contract it was given - it refuses an append that would run
  past the buffer (`written > cap - *plen`), maintaining the `*plen <= cap` invariant so the next
  builder's capacity subtraction can never underflow. `w_bytes()` also now checks `w->pos > w->cap`
  explicitly before the `w->cap - w->pos` bound so that subtraction provably cannot underflow even for
  a malformed `Writer`. Separately, the HTTP/3 DATA-frame coalescing in `dispatch_request()` now clamps
  with a room subtraction (`sizeof(body) - body_len`) instead of a `body_len + take` sum that could
  wrap. All three are the codebase's own overflow-safe idiom.

---

## QUIC anti-amplification checked after building, desyncing the flight under loss

- **Status:** FIXED (RPi netem loss interop + native_quic_conn; found while adding PTO loss recovery).
- **Found:** 2026-07-06, driving the HTTP/3 interop harness under 10-20% netem packet loss - a
  timer-polled server got _worse_ with loss, not better, and connections stalled.
- **Symptom:** under loss, `quic_conn_send()` advanced packet-number / CRYPTO-offset / stream-send
  state for a datagram it then discarded, so the retransmitted flight no longer matched what the peer
  had (or had not) received. Loss recovery made the stall worse instead of curing it.
- **Root cause:** the 3x anti-amplification check (RFC 9000 sec 8.1) ran _after_ `build_packet()` had
  already bumped `next_pn` / `crypto_tx_off` / `tx_sent` / `last_ae_pn`. When the send was then
  amplification-blocked and dropped, that state stayed advanced - a build-then-discard desync.
- **Fix:** move the amplification check to the top of `quic_conn_send()`, before any packet is built,
  so a blocked send advances no packet state. Also reset the PTO backoff on acknowledged progress
  (RFC 9002 sec 6.2) so a recovering connection does not keep doubling its probe interval.

---

## SSH outbound wire buffer under-sized for a near-max payload + long MAC

- **Status:** FIXED (native SSH suites green; found while adding s2c compression, which made the
  overflow reachable in the general case).
- **Found:** 2026-07-05, sizing the wire buffer for compression - the existing sizing did not cover
  the worst case even without compression.
- **Root cause:** `ssh_conn.cpp` sized every outbound wire buffer as `SSH_PKT_BUF_SIZE +
SSH_HMAC_SHA256_LEN` (= 2080). A payload approaching `SSH_PKT_BUF_SIZE` with the hmac-sha2-512 MAC
  (64-byte tag) needs `4 + (1 + payload + pad) + 64` ≈ 2128 bytes, so `ssh_pkt_send()` would hit its
  `wire_len > out_cap` guard and return -1, dropping the packet. Latent because real payloads
  (channel-data chunks bounded by the peer window) never approached the max. With s2c compression the
  effective payload can also expand slightly (fixed-Huffman on incompressible data), and a dropped
  packet mid-stream desyncs the stateful cipher / compression stream (session corruption), so the
  under-size had to be fixed properly rather than relied upon to never trigger.
- **Fix:** a single `SSH_WIRE_CAP` in `ssh_packet.h` sized for the true worst case - `4 + 1 +
SSH_MAX_EFFECTIVE_PAYLOAD + SSH_MAX_PAD(32) + SSH_MAX_MAC(64)`, where the effective payload grows to
  `ssh_deflate_bound(SSH_PKT_BUF_SIZE)` when `PC_ENABLE_SSH_ZLIB` is set. All four wire buffers in
  `ssh_conn.cpp` use it. Correct for every cipher/MAC mode, compressed or not.

---

## UDP transport called raw lwIP off tcpip_thread (sibling of the listener bug)

- **Status:** FIXED (HW-validated on an ESP32-S3: the SNMP agent binds UDP/161 and runs;
  before, the same `udp_bind` from the app task would have asserted like the listener did).
- **Found:** 2026-07-04, immediately after the listener fix below - auditing for the same
  root cause in the UDP path, since UDP services bind at `begin()` from the app task too.
- **Root cause:** `udp.cpp` (`pc_udp_*`, the single place lwIP UDP is touched)
  called raw `udp_new` / `udp_bind` / `udp_recv` / `udp_remove` / `udp_sendto` **directly from
  the app task**, never marshaled. On arduino-esp32 3.x (lwIP core-locking) that trips
  `LWIP_ASSERT_CORE_LOCKED`, so every UDP service - SNMP agent (`:161`), CoAP, the captive-portal
  DNS responder (`:53`), syslog, UDP telemetry, flow-export - would crash at `begin()` / on send.
  Same latency story as the listener: harmless on the IDF-4.x board all runtime HW used, and CI
  only compiles the 3.x core. (The TCP transport `pc_tcp_do` and the outbound client `cc_do_*`
  were already marshaled; UDP was the remaining raw path.)
- **Fix:** marshal the UDP ops onto tcpip_thread via `tcpip_api_call()` (a `udp_do` dispatcher for
  listen / send / send-out), mirroring the TCP transport - including the `s_in_tcpip_thread` guard
  so a handler replying from the `udp_recv` trampoline (already in-thread) sends directly instead of
  re-marshaling (which would deadlock). All `pc_udp_*` callers are unchanged; the native host stubs
  are untouched. Native UDP-service suites green (`native_coap` / `native_snmp` / `native_dns_resolver`
  / `native_syslog`); HW: SNMP agent binds `:161` and the device runs on the modern core.
- **Prevention:** every raw lwIP `tcp_*` / `udp_*` from outside a lwIP callback now goes through
  `tcpip_api_call()`; the transport layer is the one owner of that rule (listener, TCP conn, UDP,
  client all marshal). See the listener entry below for the runtime-test-the-3.x-core lesson.

---

## Listener bring-up called raw lwIP off tcpip_thread, asserting on arduino-esp32 3.x

- **Status:** FIXED (HW-validated on an ESP32-S3: `begin_tls()` now starts the HTTPS
  listener and the server runs, where before it crash-looped at boot).
- **Found:** 2026-07-04, first on-hardware run of any listener on the arduino-esp32 **3.x**
  core (IDF 5.x) - surfaced while HW-proving the PSRAM TLS arena. The board booted, joined
  WiFi, then panicked in `begin_tls()`:
  `assert failed: tcp_alloc ...lwip/src/core/tcp.c:1854 (Required to lock TCPIP core functionality!)`,
  rebooting in a loop.
- **Root cause:** `listener_add()` / `listener_stop()` (the path `begin()` and `begin_tls()`
  use) called raw lwIP `tcp_new_ip_type` / `tcp_bind` / `tcp_listen_with_backlog` / `tcp_close`
  **directly from the app (setup/loop) task**, which is not `tcpip_thread` and does not hold the
  TCPIP core lock. arduino-esp32 3.x ships `CONFIG_LWIP_TCPIP_CORE_LOCKING=1` +
  `CONFIG_LWIP_CHECK_THREAD_SAFETY=1`, so `LWIP_ASSERT_CORE_LOCKED` fires. It was latent because
  (1) all prior runtime HW testing used the classic ESP32 via PlatformIO (arduino 2.x / IDF 4.x),
  where the assert is compiled out and the unlocked call merely raced, and (2) the "Arduino Build"
  CI only **compiles** the 3.x core, never runs it. The _dynamic_ listener (SSH `ssh -R`) already
  marshaled correctly via `tcpip_api_call()`; the primary listener was the one unmarshaled path,
  written under a stale "this build has core-locking off" assumption.
- **Fix:** `listener_add()` / `listener_stop()` now route their create/close through the same
  `listener_lwip_marshal()` (`tcpip_api_call`) the dynamic listener uses, on ARDUINO. The raw path
  stays only for the native host build (no lwIP thread). This is correct on **both** cores: with
  core-locking it satisfies the lock; without it, it still removes the off-thread race. The stale
  comment was corrected.
- **Prevention:** HW test the modern (arduino-esp32 3.x) core at runtime, not only compile it -
  the core-locking assert only fires when the code actually runs. Any new raw lwIP `tcp_*` from a
  non-callback context must go through `tcpip_api_call()` (transport `pc_tcp_do` and the client
  `cc_do_*` were already correct; the listener now matches).

---

## SSH curve25519/ed25519 handshake overflowed the 8 KB worker task stack

- **Status:** FIXED (HW-validated on an ESP32-S3; the modern-crypto handshake completes with
  the raised default and ~1.8 KB of stack margin).
- **Found:** 2026-07-03, first on-hardware connect to the new curve25519-sha256 +
  ssh-ed25519 suite: the client reached `expecting SSH2_MSG_KEX_ECDH_REPLY` then
  `Connection reset by peer`, and the serial log showed
  `Guru Meditation Error: ... Stack canary watchpoint triggered (pc_worker)`.
- **Root cause:** the software field arithmetic for curve25519 (`ssh_x25519`) and ed25519
  (`ssh_ed25519_sign`), in radix-2^16 with many `ssh_gf` temporaries per frame, nests deeper
  than the finite-field DH/RSA path - plus the field inversion calls the mbedTLS bignum
  modexp at the bottom of that chain. Measured peak worker-task stack use is ~10.5 KB
  (`uxTaskGetStackHighWaterMark`: a 16 KB stack left 5928 B free at the deepest point), which
  overflows the historical 8 KB worker default sized for the ~7 KB RSA path.
- **Fix:** `PC_WORKER_TASK_STACK` now defaults to 12288 when `PC_ENABLE_SSH` is set
  (8192 otherwise), a new `PC_WORKER_STACK_CURVE_MIN` (12288) documents the floor, and the
  compile-time guard enforces it for SSH builds (the RSA-only floor still applies to
  OIDC-only builds). Re-flashed at the shipped 12288 default: the curve25519 + ssh-ed25519 +
  ed25519-client-auth session runs to a data round-trip with 1832 B of stack free at peak, no
  canary trip; the DH-group14 + rsa-sha2-256 path is unchanged (both echo on hardware).
- **Prevention:** the worker stack is now sized from a measured high-water mark, not a guess,
  and a build-time `#error` catches any future lowering below the curve floor before it can
  reach the canary at runtime.

---

## SSH answered SSH_MSG_GLOBAL_REQUEST with UNIMPLEMENTED instead of REQUEST_FAILURE

- **Status:** FIXED (`native_ssh` 127/127; the dispatcher now routes GLOBAL_REQUEST to a
  dedicated handler).
- **Found:** 2026-07-03, implementing `ssh -R` remote forwarding (which arrives as a
  `tcpip-forward` global request).
- **Root cause:** the SSH message dispatcher had no case for `SSH_MSG_GLOBAL_REQUEST` (80),
  so every connection-wide request fell through to the default arm and got
  `SSH_MSG_UNIMPLEMENTED` (RFC 4253 §11.4). That is wrong: GLOBAL_REQUEST is a **known**
  message type - only the request _name_ may be unknown - and RFC 4254 §4 says an
  unrecognized request is answered with `SSH_MSG_REQUEST_FAILURE` when `want_reply` is set,
  or ignored otherwise. In practice this broke OpenSSH client keepalives
  (`keepalive@openssh.com`, `want_reply=true`): the client saw an UNIMPLEMENTED rather than a
  SUCCESS/FAILURE reply and could treat the keepalive as unanswered, and benign
  `want_reply=false` requests (`hostkeys-00@openssh.com`, `no-more-sessions@openssh.com`) drew
  a spurious UNIMPLEMENTED.
- **Fix:** `ssh_global_request_handle()` (ssh_channel) now parses the request name +
  `want_reply`, replies `REQUEST_FAILURE` (or stays silent) per §4, and routes
  `tcpip-forward` / `cancel-tcpip-forward` to an opt-in remote-forward seam (accepted only
  when an owner is installed; a port-0 bind echoes its allocated port per §7.1). Host-tested
  (`test_ssh_channel`: accept / refuse / port-0 echo / no-reply / cancel / unknown-request /
  malformed).
- **Prevention:** the seam is the plug-in point for the `ssh -R` listener + byte-bridge owner
  (the next phase); until it is installed the server correctly declines forwards rather than
  making a promise it cannot keep.

---

## Abuse-prevention state keyed on a 32-bit hash of the IPv6 source address (security)

- **Status:** FIXED (transport now carries the full family-tagged address; all IP-keyed
  features match the whole IPv4/IPv6 address; `native_ip` / `native_accept_gate` /
  `native_auth_lockout` / `native` green with added v6 coverage).
- **Found:** 2026-07-03, reviewing the IPv6 phase-2 work. Shipped in v4.87.0 (auth lockout)
  and v4.88.0 (per-IP throttle) - both superseded by this fix.
- **Root cause:** when IPv6 arrived, the per-IP throttle, the auth-lockout table, and the
  accept-time source key all reduced the peer address to a 32-bit value: a raw v4 word, and
  for v6 a **32-bit FNV-1a hash** (`pc_ip_key` / `pc_conn_remote_key` / `pc_lwip_ip_key`).
  In a security-forward library that is a real vulnerability, not a shortcut:
    - **Targeted lockout / throttle poisoning.** A 32-bit hash is trivially collidable, so an
      attacker can pick a v6 source that hashes to the same bucket as a victim and drive the
      victim into lockout or throttle denial (a remote DoS of a specific peer).
    - **Per-IP cap evasion.** A v6 attacker owns a whole `/64` (2^64 addresses); collisions let
      many real addresses share one bucket (reset/share another's budget) or dodge their own cap.
    - **Cross-family confusion.** Flattening a v6 peer toward a v4-shaped key risks it colliding
      with a real v4 host's bucket.
- **Fix:** the transport pipe was made protocol-agnostic. `pc_ip` (a family tag + 16 address
  bytes, the library's `sockaddr_storage`/lwIP `ip_addr_t` equivalent) is now carried end to
  end: `pc_conn_remote_addr()` / `pc_lwip_to_ip()` produce it, and **every** IP-keyed
  feature stores and matches the **full** address - bucket lookups by `pc_ip_equal` (throttle,
  lockout), allowlist by `pc_ip_prefix_match` (real v4 `/0-32` and v6 `/0-128` CIDR
  containment). The hash/word keys (`pc_ip_key`, `pc_conn_remote_key`, `pc_lwip_ip_key`) and
  the host-order `PC_IPV4` allowlist macro are deleted; the public allowlist entry point is
  now CIDR text (`listener_ip_allow_add_cidr("2001:db8::/32")`). No hashing, no flattening.
- **Prevention:** the native suites now assert the security property directly - distinct v6
  peers get distinct throttle/lockout buckets, a v6 peer never shares a v4 peer's state, and a
  v4 allowlist rule never admits a v6 peer (and vice versa). One owner (the transport) resolves
  the peer address; features consume `pc_ip` and never re-key it.

---

## PreemptQueue example used the pre-3.0 Arduino-ESP32 timer API

- **Status:** FIXED (compiles on esp32:esp32 3.3.10; still builds on the 2.x core the
  PlatformIO CI pins).
- **Found:** 2026-07-02, the all-example compile sweep against the 3.x core (the second
  break it caught, after ESP-NOW).
- **Root cause:** Arduino-ESP32 **3.0** reworked the timer API - `timerBegin(freq)` is now
  frequency-based (was `timerBegin(num, divider, countUp)`), `timerAttachInterrupt()` dropped
  its edge argument, and `timerAlarm()` replaced `timerAlarmWrite()` + `timerAlarmEnable()`.
  The example's ISR setup used the 2.x forms, so it failed to compile under the core an
  Arduino IDE user installs.
- **Fix:** the timer block in `PreemptQueue.ino` is guarded with
  `#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)` and uses the 3.x calls on the
  new core, the 2.x calls otherwise. It is the only example using that API (grep-verified).
- **Prevention:** a new **Arduino Build** CI (`.github/workflows/arduino-build.yml`) now
  compiles every example against the Arduino-ESP32 3.x core with arduino-cli - using each
  example's shipped `build_opt.h`, exactly as the IDE builds it - so 3.x / IDF-5 API drift is
  caught on every push, not just in a manual local sweep.

---

## ESP-NOW would not compile on the Arduino-ESP32 3.x core (ESP-IDF 5 recv-callback ABI)

- **Status:** FIXED (compiles clean on esp32:esp32 3.3.10 via arduino-cli; `native_espnow`
  host suite still 7/7).
- **Found:** 2026-07-02, first compile of `EspNow` in the Arduino IDE toolchain
  (esp32 core 3.x). The PlatformIO CI pins `espressif32 @ ^6.0.0` (Arduino-ESP32 **2.x**,
  ESP-IDF 4.4), so it never exercised the 3.x API and reported green - the break only
  showed up under the core an Arduino IDE user actually installs.
- **Root cause:** ESP-IDF 5.0 changed the `esp_now_recv_cb_t` signature. The receive
  callback used the 4.x shape `(const uint8_t *mac, const uint8_t *data, int len)`; under
  IDF 5 the source MAC moved into a struct and the type is now
  `(const esp_now_recv_info_t *info, const uint8_t *data, int len)`, so registering the old
  callback was an invalid conversion (`-fpermissive` error).
- **Fix:** `services/radio/espnow/espnow.cpp` selects the callback signature with an
  `#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)` guard (mirrors how `ssh_rsa` handles
  mbedTLS v2/v3) and reads the MAC from `info->src_addr` on 5.x, the `mac` argument on 4.x.
  Both cores now build. The whole binding is under `#ifdef ARDUINO`, so host tests are
  unaffected.
- **Prevention:** all examples now build unmodified in the Arduino IDE (each ships a
  `build_opt.h`) and were compile-swept against the 3.x core; a broad multi-feature 3.x
  compile found no other IDF-5 API breaks.

---

## DMA frames corrupted (~8%) when the completion was posted to the work queue as a pointer

- **Status:** FIXED (HW-verified on an ESP32 DevKitV1: 2.2M+ frames ingested with zero
  integrity errors under concurrent HTTP stress, no heap growth).
- **Found:** 2026-07-02, first HW stress of the new DMA ingest path (`services/dma`). A
  combined webserver + continuous-DMA rig reported `dma_errors: 8824` out of ~95946 frames
  (~8%) via an HTTP counter - while all 11 host tests passed. Textbook "a small happy-path
  smoke hides the bug; stress on hardware surfaces it."
- **Root cause:** the DMA-complete callback posted the `pc_dma_event` whose `data` is a
  **pointer into the 2-deep ping-pong RX buffer** into the 16-deep preempting work queue.
  `pc_pq_post_from_isr()` calls `portYIELD_FROM_ISR()`, but the simulator drives the
  callback from a **task** context (not a real ISR), where the yield is not an immediate
  context switch - so under load `loop()` ran several more completions before the
  high-priority task drained the queue, the two ping-pong buffers wrapped, and the older
  queued pointers read half-overwritten data. A descriptor consumed by another task must
  own its bytes, not point into a buffer that is reused a transfer or two later.
- **Fix:** the callback now **copies the frame bytes into the queue item** (a self-contained
  message) instead of posting the pointer. Eliminates the dangling reference entirely
  (error rate 8% -> 0%). The example `DmaIngest` uses the same copy pattern, and
  `dma.h` now documents that a deferred (queued) consumer must copy the `len` bytes
  rather than keep the RX pointer. The pointer API itself is unchanged and correct for an
  in-callback consumer (the standard DMA-HAL shape).
- **Tests:** the host suite (`native_dma`) covers the ping-pong flip, byte-exact loopback
  round trip, and fail-closed paths; the deferred-consumer lifetime is a HW-load property,
  now covered by the on-device stress rig.

---

## SSH server could not interoperate with a stock OpenSSH client

- **Status:** FIXED (host-tested + HW-verified on an ESP32 against OpenSSH 9.5: `ssh`, pubkey
  auth, and `ssh -L` port forwarding all work with no client-side algorithm overrides).
- **Found:** 2026-06-30, first live `ssh` against the server once a host key was provisioned.
  A stock `ssh user@board` reset during key exchange; even forcing the KEX through, RSA pubkey
  auth was refused. The earlier "handshake HW-verified" had only ever used a hand-built
  minimal client (the native test) and a tiny forced-algorithm client.
- **Two independent root causes, both fixed:**
    - **The RX ring and the KEXINIT store were smaller than a real client's first flight.** A
      modern OpenSSH KEXINIT (post-quantum + curve KEX names, cert host-key algs, EtM MACs,
      `ext-info-c`) is ~1.5 KB. `SSH_KEXINIT_MAX` was 512, so `ssh_kexinit_parse()` rejected
      the payload outright (`len > SSH_KEXINIT_MAX` -> dispatch returns -1 -> RST), and the
      default 1024-byte `RX_BUF_SIZE` ring could not hold the banner + KEXINIT, resetting the
      handshake at key exchange. Fixed by raising `SSH_KEXINIT_MAX` to 2048 (client I_C; the
      server I_S stays small at `SSH_KEXINIT_S_MAX`) and auto-upsizing `RX_BUF_SIZE` to >= 2048
      when SSH is enabled and the ring was left at its default (same idiom as the streaming-body
      upsize; an explicit `RX_BUF_SIZE` is honored).
    - **No RFC 8308 `ext-info` / `server-sig-algs`.** Without it a modern OpenSSH client will
      not sign an RSA key (`send_pubkey_test: no mutual signature algorithm`) and falls back to
      password. Fixed by advertising `ext-info-s` in the server KEXINIT, detecting the client's
      `ext-info-c`, and sending `SSH_MSG_EXT_INFO` with `server-sig-algs = rsa-sha2-256` as the
      first message after NEWKEYS. An inbound `EXT_INFO` is now accepted (ignored) rather than
      answered with UNIMPLEMENTED.
- **Tests:** `test_ssh_server` gains `test_extinfo_build_advertises_server_sig_algs` and
  `test_large_client_kexinit_accepted`; the full-handshake test now sends `ext-info-c` and
  asserts the server replies with EXT_INFO. (Curve25519 KEX + Ed25519 keys, which would let the
  client use its _preferred_ algorithms instead of falling back to group14/rsa, are tracked
  separately on the roadmap.)

---

## SSH channel close packed CHANNEL_EOF + CHANNEL_CLOSE into one binary packet

- **Status:** FIXED (host-tested; `test_ssh_server`).
- **Found:** 2026-06-30, while building the SSH port-forwarding (`direct-tcpip`) close path on top
  of the channel layer.
- **Root cause:** `build_close_chan()` (`ssh_channel.cpp`) frames the channel-close sequence as
  ten bytes - `CHANNEL_EOF` (type + recipient, bytes 0..4) immediately followed by
  `CHANNEL_CLOSE` (type + recipient, bytes 5..9) - in one output buffer, and the only emit site
  (the `SSH_MSG_CHANNEL_CLOSE` case in `ssh_server.cpp`) sent the whole buffer through a single
  `ssh_pkt_send()`, i.e. **two SSH messages in one binary packet**. RFC 4253 6 says a binary
  packet carries exactly one message; a strict client (openssh runs `packet_check_eom()` after
  each message handler) sees five trailing bytes after `CHANNEL_EOF` and disconnects with a
  packet-integrity error, so a channel could not be closed cleanly against openssh. It slipped
  earlier HW checks because those exercised the handshake + channel data, not a strict close.
- **Fix:** emit the two halves as two packets - `emit(buf, 5)` then `emit(buf + 5, 5)` - so each
  is framed, encrypted, and sequence-numbered on its own. The builder is unchanged (its 10-byte
  layout is still the contract); the fix is at the emit boundary, with a comment so it is not
  re-bundled. Regression test `test_inbound_close_emits_eof_then_close_separately` asserts the
  dispatcher emits two packets (EOF then CLOSE).

---

## SonarCloud static-analysis sweep: response-header over-read, crypto zeroization, and friends

- **Status:** FIXED (first SonarCloud C/C++ scan; host-tested).
- **Found:** 2026-06-30, the initial SonarCloud analysis (8 BLOCKER, 7 bug, 7 vulnerability
  findings; the ~2169 "code smell" results are mostly modern-C++ style rules that clash with
  this deliberately terse C++11 / zero-heap embedded style and are not defects).
- **Real issues fixed:**
    - **Dynamic-response header over-read on truncation (`cpp:S3519`,
      `protocore.cpp`).** `append_resp_trailer()` returned `snprintf`'s
      _would-be_ length, which can exceed the header buffer when the trailer (Date + CORS +
      user `_extra_hdr` cookies/headers + Connection) does not fit. The caller then sent
      `(u16_t)hlen` bytes from a fixed `header[RESP_HDR_BUF_SIZE]` - reading past the stack
      buffer. Fixed by clamping `append_resp_trailer()`'s return into `[0, cap-1]` so no send
      path (send / send_empty / redirect / send_template / send_chunked) can ever read past
      the buffer; an over-long header is now sent truncated-but-in-bounds.
    - **Crypto key material not securely zeroed (`cpp:S5798` x5, `services/net/snmp/snmp_crypto.cpp`).**
      The `memset()`s that wipe the localized key, AES round keys, key stream, and CFB feedback
      at function exit can be elided by the optimizer (dead store on a buffer that is never read
      again), leaving secrets on the stack. Replaced with `snmp_wipe()`, a volatile-loop clear
      the compiler cannot remove (same idiom as `ssh_wipe`).
    - **Uninitialized byte fed to the parser if the ring drains mid-read (`cpp:S836` x2,
      `presentation.cpp`, `websocket.cpp`).** The drain loops ignored `pc_conn_read_byte()`'s
      return and fed `byte` to the parser even though a `false` return means nothing was read.
      Now they `break` when the read fails.
    - **`conn_pool[slot_id]` indexed without a bound (`cpp:S3519`).** The public response
      emitters dereferenced `conn_pool[slot_id]` on a caller-supplied id; added a
      `slot_id >= MAX_CONNS` guard at each entry (send / send_empty / redirect / send_template /
      send_chunked).
    - **Misleading constructs:** a `(cond) ? 4 : 4` ternary (`cpp:S3923`, `nats.cpp`) reduced to
      `4`; a `while` that always broke on the first iteration (`cpp:S1751`,
      `http_parser.cpp` Forwarded `proto=`) rewritten as the `if` it actually is; and a
      redundant `if (!app(...)) return len; return len;` (`cpp:S3923`, `webdav.cpp`) simplified
      to `app(...); return len;` (the helper is atomic - it leaves `len` unchanged on no-room).
- **Assessed and intentionally kept (false positives, marked `// NOSONAR` with a reason):**
  `cpp:S5332` "using HTTP is insecure" on `http_client.cpp` (a URL parser in an HTTP **client**
  must accept `http://`; TLS is the caller's choice) and `opcua.cpp` (the OPC UA spec
  transport-profile URI `http://opcfoundation.org/...`, an identifier string that is never
  dereferenced as a URL). The 3 remaining `cpp:S134`/long-switch style notes are correct
  protocol-dispatch switches, left as-is.

---

## WebSocket / SSE upgrade-alloc-fail detached the pcb but never closed it (leak)

- **Status:** FIXED (during the L7 teardown-ownership refactor; host-tested + HW-soaked).
- **Found:** 2026-06-30, auditing the hand-rolled connection teardown sites while routing
  them through one transport-owned API.
- **When a WebSocket or SSE upgrade had already been promoted but the per-protocol slot
  pool was full (`ws_alloc` / `sse_alloc` returned null), the cleanup path called
  `pc_conn_detach(pcb)` and reset the slot but never closed or aborted the pcb.** The
  `struct tcp_pcb` was left open with no lwIP callbacks attached: a leaked pcb that lingered
  until lwIP's own timeout, with the application slot already freed. Under upgrade churn
  against an exhausted pool this slowly bled pcbs.
- **Fix:** the teardown is now owned by the transport. `pc_conn_abort_slot(slot)` frees the
  TLS context (abrupt), detaches the pcb, resets the slot, and sends a RST; every drop path
  (WS/SSE close + upgrade-fail, `session.cpp` `tls_abort`, SSH/telnet/modbus/opcua) routes
  through it (or `pc_conn_close(slot)` for graceful), so a pcb can no longer be detached
  without also being closed. Verified on hardware (COM3): a WS-pool-exhaustion churn drove
  12 `pc_conn_abort_slot` RSTs with every slot reclaimed and no pcb/slot leak (the device
  kept accepting throughout). Tracked-debt item closed in [TODO.md](TODO.md).

---

## Test-report generator spewed "tail: write error: Broken pipe" into the CI log

- **Status:** FIXED (host-verified against all 1906 test functions; behavior identical).
- **Found:** 2026-06-30, reading the CI Test Report run log: the report was written fine but
  the job output was flooded with dozens of `tail: write error: Broken pipe` lines.
- **`get_test_comment` in `test/run_tests.sh` extracted a test's doc comment with**
  `tail -n "+${fn_line}" "$abs_file" | head -20 | awk '...'`**.** The `awk` exits at the first
  match (`print; exit`) and `head -20` closes its input after 20 lines, so once the reader is
  gone GNU `tail` gets `EPIPE` on its next write and prints `tail: write error: Broken pipe` to
  stderr. The helper runs once per test function (~1900 calls in a full run), so the CI log
  filled with the message. Not a correctness bug (the report still generated), but noise that
  hides real warnings.
- **Fix:** fold the slice + scan into a single `awk -v start="$fn_line"` pass that reads the
  file directly (`NR < start` skips, `NR >= start + 20` exits), so the early `exit` no longer
  closes a pipe and nothing writes into a dead reader. Verified behavior-identical to the old
  pipeline across every `void test_*()` in `test/test_*/` (1906 functions, 0 diffs, 0 stderr).

---

## Host Link frame builder did not NUL-terminate, so callers read uninitialized memory

- **Status:** FIXED (found by the header-shim migration verification; host-tested).
- **Found:** 2026-06-29, running the full native suite on WSL after migrating the source tree
  to the new `shared_primitives/shim.h` include. `native_hostlink` was the lone failure out of
  103 envs.
- **`hostlink_build` (`services/fieldbus/hostlink/hostlink.cpp`) wrote exactly the wire frame
  (`@UU XX <text> FF * CR`) and returned its length, but never wrote a NUL terminator, while the
  sibling ASCII builder `sdi12_build` does (it reserves `n + 1` and writes `buf[n] = '\0'`).**
  NUL-terminating the ASCII frame is the house convention, so a caller (and the test) that treats
  the returned buffer as a C-string reads off the end into uninitialized stack. It stayed latent
  because the stack byte after the frame happened to be zero; the shim migration shifted the
  binary layout and the byte was now garbage, so `TEST_ASSERT_EQUAL_STRING` saw
  `@00RD0000001057*\r` followed by junk and the test crashed (SIGHUP/ERRORED).
- **Fix:** make `hostlink_build` consistent with `sdi12_build` - reserve room for the terminator
  (`if (total >= cap) return 0`, wrap-safe: no addition) and write `buf[p] = '\0'` after the
  frame. The return value is still the frame length (not counting the NUL), so callers may treat
  `buf` as either a sized buffer or a C-string. The existing `native_hostlink` tests now pass
  7/7; the overflow test (`char small[8]`) still fails closed.

---

## Two more 32-bit length-wrap bounds bypasses: SNMP BER decoder + HTTP chunked decode

- **Status:** FIXED (follow-up bug-hunt; host-tested, incl. a regression that crashes the
  pre-fix code even on a 64-bit host).
- **Found:** 2026-06-29, a second bug-hunt sweeping the wrap class across the parsers the first
  pass did not cover.
- **The same 32-bit `size_t` wrap as the codec entry below, in two network-facing parsers the
  first pass missed:**
    - **SNMP BER decoder (`ber_read_header`, `services/net/snmp/snmp_ber.cpp`).** A long-form ASN.1
      length is read as a full 4-octet value (up to `0xFFFFFFFF`), then checked with
      `d->pos + length_val > d->len`. On the 32-bit ESP32 an attacker-supplied length of
      `0xFFFFFFFF` makes `d->pos + length_val` wrap to `d->pos - 1` (`< d->len`), so the bound
      is bypassed and a bogus huge length is returned; downstream `ber_read_oid`'s `i < len`
      loop then reads out of bounds. SNMP is an attacker-reachable UDP agent. `ber_skip` had the
      same pattern. Fix: compare against the remaining capacity (`length_val > d->len - d->pos`,
      valid because `d->pos <= d->len` holds after the count-byte check).
    - **HTTP client chunked decode (`http_client_parse_response`, `services/net/http_client/http_client.cpp`).**
      The hex chunk size `csz` is accumulated unbounded from the response, then clamped with
      `if (in + csz > len)`. A malicious / MITM'd server sending an oversized chunk size makes
      `in + csz` wrap below `len`, skipping the clamp and leaving a multi-gigabyte `memmove`. Fix:
      `if (csz > len - in)` (wrap-safe; `in <= len` here).
- The 64-bit host never wrapped on the 4-octet BER case, so it stayed latent; the HTTP
  regression uses a 16-hex-digit size (`0xFFFFFFFFFFFFFFFF`) that wraps a 64-bit `size_t` too,
  so it crashes the old code on the host and proves the fix. Tests added to test_snmp_ber and
  test_http_client.
- **Related hardening (same sweep):** audited the rest of the wrap class clean - `opcua`
  (`r_skip`/`ua_r_string` lengths are `int32`, capped at `0x7FFFFFFF`, and all callers guard
  `> 0`), `protobuf` (`PB_WT_LEN` compares in `uint64_t`; varints reject `> 10` bytes), `lwm2m`
  (24-bit length bounded), and `wamp_get_uri` (`body + 1 > out_cap`) are all correctly bounded.

---

## Codec length fields could wrap the bounds check on a 32-bit target

- **Status:** FIXED (multi-agent codebase audit; host-tested).
- **Found:** 2026-06-29, the codec bug-hunt audit of the v4.x protocol codecs.
- **Three text/binary parsers computed `overhead + declared_length` before comparing to the
  buffer length, which wraps on a 32-bit `size_t` (the ESP32 target) for an attacker-controlled
  length field, so the `> len` guard could falsely pass and hand the caller an out-of-bounds
  slice.** Affected: `amqp_parse_frame` (AMQP 0-9-1 32-bit size, `services/iot/amqp/amqp.cpp`),
  `nats_parse` MSG byte count (`services/iot/nats/nats.cpp`), and `resp_parse` `$` bulk length
  (`services/redis_resp.cpp`). The 64-bit host tests never exercised the wrap, so it was latent.
  Fix: in each, compare the declared length against the _remaining capacity_ (`len - overhead`)
  without adding, so no addition can wrap. Added oversized-length rejection tests to
  test_amqp / test_nats / test_redis_resp.
- **Related hardening in the same pass:** `stomp` `content-length` parse now rejects on overflow
  AND a present-but-invalid `content-length` is a malformed frame (previously it silently
  fell back to NUL-delimited body parsing - a request-smuggling-style differential); `parse_len`
  caps at `SIZE_MAX`. `flow_export` IPFIX `finish` now fails closed when the message exceeds the
  16-bit length field instead of truncating it. Tests added/updated accordingly.

---

## client.cpp failed to compile on a server-only Arduino build

- **Status:** FIXED (found while building the interop rig; HW build verified on COM3).
- **Found:** 2026-06-29, bringing up the real-protocol interop harness (test/servers).
- **A server-only Arduino build (no HTTP client / MQTT / WS client) did not compile
  (build break).** `client.cpp` guarded its body with only `#if defined(ARDUINO)` yet
  calls `pc_dns_resolve()`, whose declaration lives behind `PC_ENABLE_DNS_RESOLVER`.
  That flag is force-enabled only by a client transport (`HTTP_CLIENT || MQTT || WS_CLIENT`,
  protocore_config.h), so a build that enabled, e.g., WebSocket + SNMP + CoAP + Modbus but
  no client left the symbol undeclared: `error: 'pc_dns_resolve' was not declared in this
scope`. Host builds were unaffected (the body is already `#if defined(ARDUINO)`), so the
  native suites never caught it. Fix: add `PC_NEED_DET_CLIENT` (set alongside the
  DNS-resolver force-enable) and gate the translation unit on
  `#if defined(ARDUINO) && PC_NEED_DET_CLIENT`, so pc_client compiles exactly when a
  client transport needs it. Verified: a WebSocket+SNMP+CoAP+Modbus rig now builds and
  flashes, and the interop harness drives all four against real peers.

## Standards-conformance audit, batch 2e (LOW/SHOULD closeout)

- **Status:** FIXED (standards-conformance audit; host + HW-verified on COM3)
- **Found:** 2026-06-29, multi-agent conformance audit (the batch-2b "Still OPEN" list).
- **Closes the remaining LOW/SHOULD items so the audit is 100% addressed.**
- **WebSocket handshake accepted a malformed upgrade (LOW, RFC 6455 4.2.1).** The upgrade
  test did not require an `upgrade` token in the `Connection` header, nor that
  `Sec-WebSocket-Key` base64-decode to 16 bytes. Both are now required; a bad handshake
  gets `400` and no upgrade. Tests: `test_web_terminal`
  `test_ws_upgrade_requires_connection_token`, `test_ws_upgrade_rejects_bad_key_length`.
- **MQTT topic validation (LOW, MQTT-3.3.2-2 / 1.5.3).** `mqtt_build_publish` now rejects a
  Topic Name containing wildcards (`+`/`#`); `mqtt_parse_publish` rejects a Topic Name that
  is not well-formed UTF-8 or contains U+0000. The UTF-8 validator was extracted to a shared
  primitive (`shared_primitives/utf8.h`) and reused by WebSocket (no duplicate copy).
  Tests: `test_mqtt` `test_publish_wildcard_topic_rejected`,
  `test_publish_topic_nul_or_bad_utf8_rejected`.
- **base64url decoder was lenient (LOW, RFC 4648 5 / RFC 7515).** `base64url_decode` also
  accepted the standard `+`/`/` alphabet, contradicting its name and the JWS base64url
  contract. Now strict (URL alphabet only); no caller fed it standard base64 (OIDC `n`/`e`
  are Base64urlUInt, not `x5c`). Not a signature-bypass vector (JWS signs the literal
  transmitted ASCII), hence LOW. Test: `test_jwt` `test_base64url_strict_alphabet`.
- **Digest nonce never rotated; no replay window bound (SHOULD, RFC 7616 3.3).** The server
  used a single fixed nonce regenerated only at `begin()`, so a captured Digest response
  could be replayed indefinitely. Replaced with a stateless, keyed, timestamped nonce
  (`<issue_ms_hex>.<SHA-256(secret||issue) truncated>`): no per-nonce table (compatible with
  the shared-nothing worker model), bounded lifetime (`PC_DIGEST_NONCE_LIFETIME_MS`,
  default 5 min), and an expired-but-valid response is answered `401 stale=true` for a
  transparent retry (not counted against the lockout). Full per-nonce `nc` replay tracking
  remains intentionally out of scope: it requires shared mutable per-nonce state the
  deterministic worker model cannot hold safely; the bounded-lifetime nonce is the standard
  stateless mitigation. Tests: `test_digest_auth` `test_nonce_is_stateless_timestamped`,
  `test_stale_nonce_triggers_transparent_retry`.
- **SNMP v2c GET returned noSuchObject for a missing instance (LOW, RFC 3416 4.2.1).** A
  Get for an unbound name always reported `noSuchObject`. Now distinguishes
  `noSuchInstance` (the name's object-type prefix matches a registered object, only the
  instance is absent) from `noSuchObject` (no such object at all). Test: `test_snmp_agent`
  `test_get_bad_instance_v2c_nosuchinstance`.
- **HTTP chunked was sent to HTTP/1.0 clients (MED-niche, RFC 7230 3.3.1).** `send_chunked`
  always emitted `Transfer-Encoding: chunked`, which is invalid for a 1.0 client. It now
  falls back to a close-delimited body (no Transfer-Encoding, `Connection: close`, raw bytes
  paged across loops, end signalled by the connection close) when the request is not
  HTTP/1.1. Tests: `test_chunked` `test_http10_falls_back_to_close_delimited`,
  `test_http10_large_body_not_truncated`. HW: `--http1.0 /stream` -> `HTTP/1.0 200` +
  `Connection: close`, body intact; `--http1.1` -> chunked.

## Standards-conformance audit, batch 2d (WebDAV PROPFIND Depth: infinity)

- **Status:** FIXED (standards-conformance audit; HW-verified)
- **Found:** 2026-06-29, multi-agent conformance audit (WebDAV).
- **PROPFIND `Depth: infinity` was silently truncated to one level (MED, RFC 4918
  9.1.1).** A collection PROPFIND with `Depth: infinity` returned a one-level 207 the
  client would read as a complete tree. The server lists at most one level, so it now
  rejects infinity with `403` + the `<D:propfind-finite-depth/>` precondition body;
  `Depth: 0`/`1` are unchanged. HW: `Depth: infinity` -> 403 (precondition body present),
  `Depth: 1` -> 207.

## Standards-conformance audit, batch 2c (HTTP If-None-Match comparison)

- **Status:** FIXED (standards-conformance audit)
- **Found:** 2026-06-29, multi-agent conformance audit (HTTP semantics).
- **If-None-Match used exact strong `strcmp` (MED, RFC 9110 13.1.2).** Conditional GET
  only matched a single, byte-identical strong tag, so it ignored `*`, a comma-separated
  tag list, and the mandated weak comparison (an inbound `W/"x"` for our strong `"x"`).
  A standards-compliant cache revalidating with `W/` tags, a list, or `*` got a full 200
  instead of 304. Fix: `inm_matches()` handles `*` (matches the current representation),
  splits a list, and weak-compares (ignores a `W/` prefix). Test: `test_application`
  `test_serve_static_inm_star_list_weak` (`*` / `W/"x"` / list-with-tag -> 304;
  list-without-tag -> 200).

## Standards-conformance audit, batch 2b (WS UTF-8, SSH padding, syslog PRI, BER OID)

- **Status:** FIXED (standards-conformance audit)
- **Found:** 2026-06-29, multi-agent conformance audit.
- **WebSocket: TEXT messages were not UTF-8-validated (MED, RFC 6455 8.1).** An invalid-
  UTF-8 text message was delivered to the app instead of failing the connection. Fix:
  validate the fully reassembled + decompressed TEXT message and close 1007
  (WS_CLOSE_INVALID_PAYLOAD) on invalid UTF-8 (strict: rejects overlong / surrogate /
  out-of-range / truncated). BINARY frames are not validated. Tests: test_websocket.
- **SSH: padding_length < 4 was not rejected (LOW, RFC 4253 6).** The receive path only
  checked padding < packet length. Now also rejects padding < 4 (gated behind MAC
  verification, so a robustness gap, not a vulnerability).
- **syslog: PRI not range-bounded (LOW, RFC 5424 6.2.1).** An out-of-range caller
  facility/severity could emit a malformed PRI. Now clamped to 0..191.
- **SNMP/BER: OID decoder mishandled a first subidentifier >= 128 (LOW, X.690 8.19.4).**
  The first subidentifier (40*arc0 + arc1) is base-128 and may span octets; the decoder
  read only one octet (encoder was already correct). Fixed; common OIDs (1.3.6.1...,
  first subid 43) decode identically. Test: test_oid_large_first_subidentifier_roundtrip.
- **These LOW/SHOULD items are now CLOSED in batch 2e (above):** HTTP chunked to non-1.1
  clients, SNMP v2c noSuchInstance vs noSuchObject, Digest nonce rotation (stateless
  timestamped nonce; full nc-replay tracking intentionally out of scope), stricter
  base64url, WS handshake Connection:Upgrade + key-length check, MQTT topic UTF-8/wildcard.

## Standards-conformance audit, batch 2a (auth: JWT alg, Digest uri)

- **Status:** FIXED (found by the auth/crypto conformance audit)
- **Found:** 2026-06-29, multi-agent conformance audit (auth module).
- **JWT: the `alg` header was never validated (RFC 7515 5.2, MED).** `jwt_verify_hs256`
  computed HMAC-SHA256 and compared without checking the token's declared algorithm, so
  a token whose header said `none` / `RS256` / `HS384` was accepted as long as its
  signature equaled base64url(HMAC-SHA256(secret, signing_input)) - an algorithm-
  substitution hazard (not directly exploitable since the HMAC is still enforced and
  empty-sig `none` fails the length gate, hence MED). Fix: decode the header and require
  `alg":"HS256"` before verifying. Test: `test_jwt` `test_alg_not_hs256_rejected` (a
  valid-HMAC token with alg `none` is rejected).
- **Digest: the `uri` parameter was not matched to the request target (RFC 7616 3.4,
  MED).** `check_digest_auth` folded the client-supplied `uri` into HA2 but never
  compared it to the actual request target, so a Digest response captured for route A
  was structurally valid against any route under the same realm/nonce. Fix: reconstruct
  the request target (`path[?query]`) and require it equals `uri`. Test:
  `test_digest_auth` `test_uri_mismatch_rejected`.
- Audited clean: Basic auth (first-colon split), OIDC RS256 (strict alg/exp/nbf/iss/aud
    - constant-time RSA block compare), TOTP/HOTP (RFC 4226 truncation), OAuth2 client,
      constant-time HMAC compare. Noted for later (SHOULD/LOW): Digest nonce rotation + `nc`
      replay tracking; stricter base64url; constant-time Digest response compare.

## Standards-conformance audit, batch 1 (WS / MQTT / CoAP / Telnet)

- **Status:** FIXED (found by the parallel standards-conformance audit; specs read from
  the downloaded RFC texts, mapped in docs/STANDARDS.md)
- **Found:** 2026-06-29, multi-agent conformance audit against the live specs.
- Five real conformance gaps, fixed with tests:
    - **WebSocket close left the TCP socket open (HIGH, RFC 6455 5.5.1).** The plaintext
      WS close/error path (protocore.cpp) only freed the WS slot and
      `http_reset`'d it - it never closed the TCP connection (the TLS path did). The slot
      stayed CONN_ACTIVE and re-armed as an HTTP parser, so bytes after the Close frame
      were re-interpreted as a new HTTP request (state confusion). Fix: `pc_conn_begin_close`
      on the slot so it leaves CONN_ACTIVE (the queued Close frame still flushes).
    - **MQTT PUBLISH QoS=3 accepted (HIGH, MQTT-3.3.1-4 / 4.8.0-1).** A PUBLISH with both
      QoS bits set was treated as QoS 2; the spec says it is malformed and the receiver
      MUST close the connection. Fix: `mqtt_parse_publish` rejects qos==3; the handler
      `mq_close()`s on a malformed PUBLISH.
    - **CoAP unsupported method returned 5.01 (MED, RFC 7252 5.8).** Must be 4.05 Method
      Not Allowed. Fixed.
    - **CoAP unrecognized critical option silently ignored (MED, RFC 7252 5.4.1).** An
      unknown odd-numbered (critical) option must yield 4.02 Bad Option (so e.g. Accept,
      or Block when COAP_BLOCK is off, is rejected, not ignored). Fixed.
    - **Telnet literal IAC (0xFF) in output not doubled (MED, RFC 854).** Echoed/printed
      0xFF bytes were sent un-doubled, desyncing the client's command stream. Fix: a
      `send_escaped` data path doubles IAC for echo + app output (protocol commands still
      use the raw path).
    - SNMP/BER, SSH, WebDAV, syslog, OIDC/TOTP/Basic-auth/OAuth2 audited clean.

## CoAP Observe used millis() (would not build on host + pluggable-clock violation)

- **Status:** FIXED (found by the test-gap hardening pass)
- **Found:** 2026-06-29, adding a CI env that compiles the Observe-gated code.
- **Symptom:** `coap_notify()` (under `PC_ENABLE_COAP_OBSERVE`) built the notification
  message-id with `millis()` and pulled `<Arduino.h>` for it. The flag was enabled by no
  test env, so the code had never been compiled on host - it failed to build there
  (`'millis' was not declared`), and even on ESP32 it violated the pluggable-clock rule
  that `pc_millis()` is the single monotonic source (same class as the dns_resolver
  bug above). Latent because the whole Observe path was never compiled in CI.
- **Fix:** use `pc_millis()` (include `services/clock.h`), drop the Arduino.h
  include; added a `native_coap_observe` env so the Observe-gated code is compiled + the
  CoAP suite runs under the flag in CI (no longer bit-rots).

## HTTP request smuggling: Transfer-Encoding ignored on inbound requests

- **Status:** FIXED (found by the test-gap hardening pass)
- **Found:** 2026-06-29, request-parser test-gap review.
- **Symptom:** the HTTP parser only framed request bodies by `Content-Length` and
  ignored `Transfer-Encoding` entirely. A `Transfer-Encoding: chunked` request with no
  `Content-Length` was treated as body-length-0, so the chunk octets
  (`5\r\nhello\r\n0\r\n\r\n`) were left in the RX buffer and re-parsed as the next request
    - a classic TE request-smuggling / desync vector on a keep-alive connection (and the
      CL+TE combination is the canonical CL.TE desync).
- **Fix:** the server does not decode chunked request bodies, so reject any request
  bearing `Transfer-Encoding` -> `PARSE_ERROR` (400), fail-closed, matching the existing
  conflicting-`Content-Length` rejection (RFC 9112 §6.1/§6.3). Tests:
  `test_compliance` `test_transfer_encoding_chunked_rejected` /
  `_with_content_length_rejected` / `_case_insensitive_rejected`.

## Byte-range parser integer overflow on a huge Range value

- **Status:** FIXED (v4.9.1; found by the edge-case audit)
- **Found:** 2026-06-28, agent edge-case test-gap audit.
- **Symptom (latent):** `parse_byte_range` accumulated `start/end = *10 + digit` with no
  overflow guard, so a `Range: bytes=99999999999999999999999-` wraps `size_t` to a small
  value that can pass the `start >= size` check and yield a wrong `206` window.
- **Fix:** saturate the accumulator at `SIZE_MAX` on overflow, so a huge start is treated
  as past-EOF (416) and a huge end clamps to the last byte - never a corrupt window.

## If-Modified-Since month token could mis-parse (off-by-alignment)

- **Status:** FIXED (v4.9.1; in v4.9.0's just-shipped conditional-GET code)
- **Found:** 2026-06-28, agent edge-case test-gap audit.
- **Symptom (latent):** `http_not_modified_since` matched the month via
  `strstr(MONTHS, mon)` without checking the match offset is a multiple of 3, so a
  malformed token like `ebM` (which appears inside "FebMar") parsed as a valid month ->
  a wrong 304/200 cache decision on malformed input. No memory-safety impact.
- **Fix:** reject the match unless `(mp - MONTHS) % 3 == 0`.

## DNS resolver ignored the pluggable clock

- **Status:** FIXED (v4.9.1; found by the duplicate-code audit)
- **Found:** 2026-06-28, agent services duplicate-code audit.
- **Symptom:** `services/dns_resolver` polled its resolve deadline with `millis()` instead
  of `pc_millis()`, so it ignored a custom clock (`pc_set_clock`) - violating the
  pluggable-clock rule that `pc_millis()` is the single monotonic source.
- **Fix:** use `pc_millis()`. (The bigger dedup, pc_client reusing this one resolver,
  shipped later as the shared-primitive DNS-owner change.)

## Client ring used `volatile` indices (weak cross-core ordering)

- **Status:** FIXED (v4.8.1; found by analysis while unifying the ring primitive)
- **Found:** 2026-06-28, merging the server/client ring implementations.
- **Symptom (latent):** the outbound client ring (`pc_client`) used `volatile size_t`
  head/tail. `volatile` blocks compiler reordering but provides no cross-core
  acquire/release; on the dual-core ESP32 (producer = tcpip_thread, consumer = caller
  loop, often different cores) the consumer could observe an advanced `head` before
  the buffer bytes it published were visible -> a rare stale read. The server ring
  already used `pc_atomic` (correct); the client was inconsistent.
- **Fix:** both transports now share `ring.h` (the `pc_atomic` SPSC index +
  the drain math); the client ring's indices are `pc_atomic`, matching the server's
  acquire/release ordering. One ring primitive, no hand-rolled wrap/ordering.

## Client transport could deadlock on a large inbound transfer

- **Status:** FIXED (v4.8.0; same fix as the server, found by analysis during the
  unified-client-transport pass)
- **Found:** 2026-06-28, reviewing `pc_client` against the server's ack-on-consume fix.
- **Symptom (latent):** an outbound client (http_client / mqtt / ws_client) reading a
  response larger than the client ring could stall - the same class as the server
  deadlock below, in the other direction.
- **Root cause:** `pc_client`'s recv callback ACKed on copy (`tcp_recved` in
  `cc_recv`) with a 4 KB ring (< TCP_WND), so a sustained inbound transfer could fill
  the ring, get refused (lwIP `refused_data`), and race.
- **Fix:** ack-on-consume on the client transport too - `cc_recv` no longer ACKs;
  `pc_client_read()` marshals `tcp_recved()` for the bytes it drained. Default
  `PC_CLIENT_RX_BUF` raised 4096 -> 8192 (>= TCP_WND). Client and server transports
  now share one flow-control model.
- **HW proof:** device `http_get()` of a 12 KB body (> the 8 KB client ring, so it
  wraps the ring and exercises the ack-on-consume read path) returned the full
  `len:12000`, 5/5 - no truncation or deadlock.

## RX flow-control deadlock on streamed uploads (WebDAV PUT)

- **Status:** FIXED (v4.6.0; host + HW validated)
- **Found:** 2026-06-28, stress-testing WebDAV streaming PUT on hardware.
- **Symptom:** large PUTs intermittently hung ~20 s (curl timeout), stored 0 bytes,
  and repeated hangs eventually wedged every slot (device pings but HTTP is dead).
- **Root cause:** `recv_cb` ACKed received data on **copy** (`tcp_recved` at copy
  time), decoupling the advertised TCP window from how full the ring actually was.
  A slow consumer (flash writes) let the ring fill to `RX_BUF_SIZE`; the next
  segment was refused (`ERR_MEM` -> lwIP `refused_data`). When `RX_BUF_SIZE <
TCP_WND` the ring can never hold a full receive window, so refusals were constant
  and lwIP's refused-data redelivery raced fatally. Serial trace nailed it: failing
  PUTs stalled at exactly `bytes_written == RX_BUF_SIZE`. This was an **interlayer**
  bug: the receive-window invariant was smeared across transport (ACK on copy),
  presentation (drains the ring) and session (worker loop), with no single owner.
- **Fix:** ack-on-consume, owned entirely by transport. `recv_cb` no longer ACKs;
  the worker calls `pc_conn_ack_consumed(slot)` once per loop and transport
  reopens the window by exactly the bytes drained since the last ACK
  (`tcp_recved` marshaled to tcpip_thread). The window now tracks ring occupancy,
  so a slow sink cannot overflow the ring. **TCP-level requirement:** the ring must
  hold at least one receive window (`RX_BUF_SIZE >= TCP_WND`); a smaller ring is a
  configuration error (you cannot advertise a window larger than your buffer).
- **HW proof:** RX=8192 (>= TCP_WND) + ack-on-consume -> 10/10 50 KB byte-exact,
  `backpressure=0`. Pre-fix: RX=2048 -> ~40% hang + permanent wedge.

## Boot stack-overflow when RX_BUF_SIZE is large (pool_init)

- **Status:** FIXED (v4.6.0; host validated)
- **Found:** 2026-06-28, while testing large rings for the deadlock above.
- **Symptom:** "Stack canary watchpoint triggered (loopTask)" at boot
  (`begin()` -> `pool_init` -> `memset`) once `RX_BUF_SIZE` was set large (e.g. 8192).
- **Root cause:** `conn_pool[i] = {}` materializes a full `sizeof(TcpConn)`
  temporary - the entire `rx_buffer[RX_BUF_SIZE]` - on the loopTask stack.
- **Fix:** reset from a single `static const TcpConn blank = {}` in BSS via
  copy-assign (uses `pc_atomic::operator=`, no atomic-memset UB); no large stack
  temporary.

## WebDAV streamed PUT leaks the file handle on abort

- **Status:** FIXED (v4.6.0)
- **Found:** 2026-06-28, investigating the deadlock cascade to a permanent wedge.
- **Symptom:** a PUT torn down before completion (peer reset / timeout / the
  deadlock above) never closed `g_dav_put_file`; after a few, LittleFS ran out of
  open-file slots and `open()` failed ("no permits for creation").
- **Root cause:** the file was closed only on the PARSE_COMPLETE handler path; no
  cleanup hook for an aborted stream.
- **Fix:** added `HttpStreamAbortCb` to the parser stream hooks, fired from
  `http_parser_reset` when `body_streaming && parse_state != PARSE_COMPLETE`.
  WebDAV registers it and closes the half-written file. The completion path now
  also clears `g_dav_put_active` so the abort hook cannot double-close.

## WebDAV concurrent streamed PUTs clobber each other

- **Status:** FIXED (v4.7.0; host + HW validated)
- **Found:** 2026-06-28, concurrent (4x) 50 KB PUT stress test.
- **Symptom:** 4 simultaneous PUTs -> one 201, the rest 409/timeout (the file was
  not actually written for the losers).
- **Root cause:** the WebDAV streaming-PUT state (`g_dav_put_file/active/error/...`)
  was a single global, and `HttpStreamDataCb` carried no slot/connection, so the
  data hook could not route bytes to per-connection state. Overlapping PUTs shared
  and clobbered the one global transfer.
- **Fix:** made the streaming-body `data` hook slot-aware (`HttpStreamDataCb(HttpReq*,
...)`; `begin`/`abort` already had it) and replaced the global state with per-slot
  `g_dav_put[MAX_CONNS]`, so each connection streams to its own file. Aligns with the
  no-spaghetti directive (the data hook lacking connection context was the interlayer
  gap). HW: 4 concurrent PUTs with distinct payloads, all 4 byte-exact (was 0/4).

## TX truncation on large responses (serve_file, send_chunked)

- **Status:** SHIPPED (serve_file v4.4.1; send_chunked v4.5.0)
- **Found:** 2026-06-28, stress-testing file/chunked GET on hardware.
- **Symptom:** any file/WebDAV GET or chunked/SSE body larger than ~`TCP_SND_BUF`
  (~5.7 KB) was truncated.
- **Root cause:** the senders called `pc_conn_send()` and ignored the return; once
  the TCP send buffer filled, the remainder was silently dropped. Hidden on host
  because the mock `tcp_sndbuf` is constant and `tcp_write` never returns `ERR_MEM`.
- **Fix:** per-slot send continuations (`file_send_pump` / `chunk_send_pump`) that
  page out one send-window per worker loop and resume on the sent callback; the
  chunked API became a pull generator (`ChunkSource`) so it can resume across loops.
