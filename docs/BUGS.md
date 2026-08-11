# Bug log

A running record of every bug found in this library: what broke, the root cause,
the fix, and status. Newest first. A bug is logged here the moment it is found
(even before it is fixed) so nothing slips.

Status key: **OPEN** (found, not fixed) - **FIXED** (fixed, validated) - **SHIPPED** (released).

---

## RFC 4254 sec 6.10: an exec'd command's exit status is never returned

- **Status:** OPEN. Found by labelling `test_ssh_channel.c` by RFC section: sec 6.10 had no test,
  and the reason is that nothing implements it.
- **Sec 6.10:** "When the command running at the other end terminates, the following message can be
  sent to return the exit status of the command. Returning the status is RECOMMENDED." The message
  is a `CHANNEL_REQUEST` naming `"exit-status"` with `want_reply` FALSE and a `uint32 exit_status`,
  and "The channel needs to be closed with SSH_MSG_CHANNEL_CLOSE after this message."
- **What is missing:** `exit-status` and `exit-signal` appear nowhere in `src/`. A channel that ran
  an `exec` is closed with EOF + CLOSE and no status, so a client sees a command that produced
  output and then ended with no exit code. `ssh host cmd` reports success regardless of what the
  command did, which matters most for the SCP path, where a failed transfer is indistinguishable
  from a successful one at the exit code.
- **Not fixed:** emitting it needs a way for the application to report the code when its exec
  finishes, which is a new seam and the owner's call.

## RFC 4254 sec 5.1 refusal codes: direct-tcpip reports the wrong one, and one is unreachable

- **Status:** OPEN. Found writing the per-file suite for `ssh_forward.c`; both are pinned by
  `test_s7_2_a_denied_target_is_not_dialed` and
  `test_s7_2_a_refused_connect_is_reported_after_the_confirmation`, which assert what the code does
  today, not what the RFC names.
- **Every owner refusal reports code 2.** `SshForwardOpenCb` returns `int`, and
  `ssh_channel.c:436` maps any `< 0` to `build_open_failure(..., 2u)`. So an administratively
  denied target, a host longer than `PC_SSH_FWD_HOST_MAX`, and a full `PC_SSH_FWD_MAX` table all
  report SSH_OPEN_CONNECT_FAILED. Sec 5.1 names 1 (ADMINISTRATIVELY_PROHIBITED) for the policy
  denial and 4 (RESOURCE_SHORTAGE) for the full table. The callback cannot express which, so
  distinguishing them is a contract change, not a local fix.
- **Code 2 is unreachable for the case it names.** `on_forward_open` (`ssh_forward.c:238`) treats
  `Tcp.client->open() < 0` as the connect failure, but `pc_client_open` (`tcp_client.c:308`) calls
  `cc_pump` and then returns the slot id without consulting `c->closed` - the transport is
  non-blocking and the connect has not settled yet. A refused target therefore yields
  CHANNEL_OPEN_CONFIRMATION followed by EOF + CLOSE on the next `pc_ssh_forward_pump`, never
  SSH_OPEN_CONNECT_FAILED. Not a protocol violation, but the client is told a channel opened to a
  host nothing ever connected to.
- **Stale comment:** `ssh_forward.c:238` says the open "blocks on DNS + connect". It does not.

## RFC 4254 sec 7.1: no privileged-port check on a remote forward

- **Status:** OPEN. Sec 7.1: "Implementations should only allow forwarding privileged ports if the
  user has been authenticated as a privileged user."
- **What is missing:** `on_rforward_open` (`ssh_forward.c:274`) checks port 0, a duplicate binding,
  `PC_SSH_RFWD_MAX` capacity and `MAX_LISTENERS` capacity. There is no port-range check anywhere in
  the file, so any authenticated user can bind 22, 80 or 443 on the device with `ssh -R`.
- **Reach:** `PC_SSH_PORT_FORWARD` defaults to 0 and remote forwarding is inert until the
  application calls `pc_ssh_forward_begin()`, so no default configuration is exposed. A deployment
  that turns forwarding on gets the whole port range.
- **Not fixed:** a privileged-port gate is a new policy surface and the owner's call.

## ssh_forward.c does not compile: `Session` undeclared

- **Status:** FIXED 2026-08-10, validated by `native_ssh_forward` building and running 14/14.
- **What broke:** `pc_ssh_forward_begin` calls `Session.proto->add(PROTO_SSH_RFWD, &s_rfwd_handler)`
  at `ssh_forward.c:466`. `Session` is declared in `network_drivers/session/session.h:46`, which the
  file never included - it includes `session/proto_handler.h`, which declares `ProtoRegistryNs` but
  not the `Session` instance. Any build with `PC_SSH_PORT_FORWARD=1` fails with
  `error: 'Session' undeclared (first use in this function); did you mean 'SshSession'?`.
- **Why it was never caught:** no environment in `test_matrix.json` compiled the file. The whole of
  `ssh -L` and `ssh -R` was unbuilt, so the error sat in the tree rather than failing CI. Found by
  adding `native_ssh_forward`, the first env that builds it.
- **Fix:** the missing `#include "network_drivers/session/session.h"`. The direction is acyclic -
  `session.h` reaches only into `session/` and `transport/`, never back into `presentation/`.
- **Reach:** compile-time only, and only when the feature is on. `PC_SSH_PORT_FORWARD` defaults to 0
  (`protocore_config.h:6365`), so no shipped default configuration was affected.

## Audit F3 "a framing failure permanently wedges the channel" - OVERSTATED, the idle sweep reclaims it

- **Status:** CLOSED 2026-08-09 as not the defect it was filed as. The leak is real; the consequence
  is not. Filed so the severity is not acted on twice.
- **The leak, confirmed:** `pc_ssh_conn_open_forwarded` (`ssh_conn.c:271`) releases the secure mark
  and returns -1 when `ssh_pkt_send` fails, while the channel `pc_ssh_channel_open_forwarded`
  allocated at `:263` stays `pending`. `chan_alloc` skips any slot with `open || pending`, so with
  the default `PC_SSH_MAX_CHANNELS == 1` no further channel opens on that connection.
- **The claim that does not hold:** F3 calls it "unrecoverable short of a reconnect", which reads as
  a permanent wedge. The reconnect is automatic and bounded. `tcp_conn.c:443` arms the connection
  pool's idle sweep at `CONN_TIMEOUT_MS` (5000 ms, `protocore_config.h:220`); `ssh_conn.c:133` binds
  `.on_close = pc_ssh_conn_close`, so the sweep frees the SSH slot; and `pc_ssh_conn_accept` calls
  `pc_ssh_channel_init` (`:357`), which clears the slot's channels for the next tenant. A wedged
  channel therefore costs at most one idle timeout.
- **What it actually is:** up to 5 s during which an `ssh -R` accept on that connection returns -1
  and `rfwd_on_accept` closes the inbound socket. Worth tidying, not worth an API.
- **Not fixed, deliberately:** releasing the slot on that path needs a counterpart to `chan_alloc`
  that does not exist - the two handlers that clear `pending` both parse a peer message, and here
  the CHANNEL_OPEN never reached the wire. Adding a public release for a 5 s self-healing window is
  not worth the surface.

## A refused SSH send leaves the sequence number and the cipher ahead of the peer, and the session dies

- **Status:** OPEN for the desync. The reporting half is FIXED in `cd435b1b6`; this entry is the part
  that fix does not reach. Found in the `network_drivers/presentation` resource audit (F2).
- **What is fixed:** `pc_ssh_conn_send` discarded the `Tcp.conn->send` return and returned `len`
  regardless, so a short send window read to the caller as a complete write. It now returns -1.
- **What is not:** by the time that send runs, `ssh_pkt_send` has already done `s->seq_no_send++`
  (`ssh_packet.c:372`) and advanced the AEAD invocation counter (`:352`). A refused queue therefore
  leaves this end's send side one packet ahead of the peer's receive side, and **every subsequent
  packet fails the peer's MAC**. Returning -1 tells the caller the write failed; it does not undo
  the cipher step, and nothing retries the framed bytes. The session is unrecoverable from that
  point whether or not the caller notices.
- **Reach:** `pc_ssh_conn_send` is the port-forward data path (`ssh_forward.c:211`, `:521`), which is
  exactly where sustained load fills a send buffer. `ssh_scp.c:88,93` and `ssh_sftp.c:170` discard
  the return entirely, so those two do not even learn.
- **Resolved shape (maintainer, 2026-08-09):** retrying is the wrong answer. A refused queue means
  the peer is gone, and a peer that is gone sends nothing further, so there is no session left to
  keep in step with - the desync is only a problem for a session that continues. The connection is
  torn down instead of preserving cipher state for a peer that will never read it. That leaves the
  fix as propagating the failure to a teardown, not as a retry queue, and unties this finding from
  F1's borrow-placement question.
- **Still to write:** the teardown itself. `pc_ssh_conn_send` now returns -1; `ssh_scp.c:88,93` and
  `ssh_sftp.c:170` still discard that, so they neither learn nor close.

## The server's SSH_MSG_NEWKEYS is framed into an occupied slot and dropped, and the test cannot see it

- **Status:** FIXED 2026-08-09 (`ssh_packet.c` `ssh_pkt_emit`). Confirmed by reading the whole path,
  found in the `network_drivers/presentation` resource audit (F1). **Not reproduced on hardware yet** - that is the
  next step, and the audit says the same.
- **Symptom (by construction, not observed):** the server answers `SSH_MSG_KEXDH_INIT` with two
  packets and only the first reaches the wire.
- **The path, every hop read:**
    1. `ssh_server.c:138` `emit(i, reply.buf, n)` - KEXDH_REPLY.
    2. `ssh_server.c:141` `emit(i, &newkeys, 1)` - NEWKEYS, immediately after, nothing between.
    3. `emit()` (`ssh_server.c:34`) is `static inline void` and calls `s_srv.emit_cb`, discarding
       any result.
    4. `emit_cb` is bound to `ssh_emit` at `ssh_conn.c:124`.
    5. `ssh_emit` (`ssh_conn.c:78`) calls `ssh_pkt_emit`, and on 0 calls `Workers.wake`.
    6. `ssh_pkt_emit` (`ssh_packet.c:157-160`) opens with
       `if (s->tx_ready) { return -1; } // one packet in flight per slot`.
       The first emit sets `tx_ready` (`:176`). The second therefore returns -1 and the payload is never
       framed. `emit()` returns `void`, so nothing upstream learns of it, and `ssh_newkeys_sent(i)` on the
       very next line turns `enc_out` on regardless.
- **Nothing drains in between.** `Workers.wake` -> `pc_worker_wake` (`worker.c`) is
  `pc_platform_task_notify` - it signals a task, it does not run one. The only drain is
  `ssh_tx_drain`, called at `ssh_conn.c:419`, _after_ `ssh_pkt_recv` at `:414` has returned, and at
  `:316` on a later poll.
- **Consequence:** the peer never sees NEWKEYS, so it never switches its inbound cipher, while the
  server encrypts from the next packet on. Every subsequent server packet arrives encrypted into a
  plaintext parser. The handshake stalls and the client is dropped on the idle timeout. The same
  double-emit shape sends `CHANNEL_EOF` + `CHANNEL_CLOSE` at `ssh_server.c:356-360`, where the second
  is likewise dropped and the peer keeps a half-open channel.
- **Why the suite is green:** `test_ssh_server.c:186-190` asserts exactly this - `emt_n == 2`,
  `emt_type[0] == KEXDH_REPLY`, `emt_type[1] == NEWKEYS` - and passes. It installs its own callback
  through `pc_ssh_server_set_emit_cb`, so it proves `pc_ssh_server_dispatch` _calls_ emit twice and
  never exercises `ssh_emit` or `ssh_pkt_emit`. The one-packet-per-slot rule is on the other side of
  the seam the test replaces. No native env covers the real emit path.
- **Fix:** the wire holds two packets (`SSH_WIRE_CAP = 2 * SSH_PKT_WIRE_MAX`), and `ssh_pkt_emit`
  appends the second at `tx_len` instead of returning -1 when `tx_ready`. SSH streams binary packets
  back-to-back (RFC 4253 sec 6), so KEXDH_REPLY + NEWKEYS and CHANNEL_EOF + CHANNEL_CLOSE frame into
  `tx_wire` in sequence and leave on one drain; `ssh_pkt_send` bumps `seq_no_send` per call so the pair
  carries consecutive sequence numbers, and the `enc_out` flip after NEWKEYS keeps both under one state.
  The `ssh_conn.c` static_assert proves the doubled wire fits (needs 10644, budget 12992).
  Verified: `native_ssh` + `native_ssh_conn`, 268/268.

## native_coaps_server: eight CID-routing tests fail, and they failed before the crypto cascade

- **Status:** OPEN, found 2026-08-09 while verifying the crypto-ownership cascade. **Pre-existing** -
  reproduced identically at `0c8fa67db`, the commit before the cascade, in a clean worktree with
  only the `mmgr` link deps added. Same eight tests, same counts. Nothing in the cascade touches it.
- **Symptom:** 8 of 20 fail, every one on connection-ID routing or address migration:
    - `:458` `test_server_single_peer` - `pc_coaps_server_ingest` refuses the first application
      record, though the handshake completed and `pc_coaps_server_active_conns()` is 1.
    - `:478` `test_two_peers_routing`, `:551` `test_cid_address_migration`,
      `:766` `test_unknown_cid_dropped`, `:800` `test_slot_lookup_same_port_different_ip`,
      `:832` `test_slot_by_cid_skips_and_bounds`, `:862` `test_cid_no_migration_when_address_unchanged`,
      `:883` `test_cid_migration_same_port_different_ip`.
      The slot-lifecycle tests around them all pass (`test_ingest_ring_full`, `test_pool_full_rejects_new_peer`,
      `test_fatal_handshake_frees_slot`, `test_pto_ceiling_frees_slot`), so the pool works and the
      lookup path is what does not.
- **Not a crash.** pio reports `SIGFPE`, which is Unity's exit code - it exits with the failure count,
  and 8 renders as signal 8. Under gdb the program runs to completion with no fault and no stack.
  The same artifact renders 5 failures as `SIGTRAP` and 1 as `SIGHUP` elsewhere in the suite.
- **Root cause:** not investigated. `PC_COAPS_MAX_CONNS` (2) and `PC_COAPS_INGEST_RING` (6) are sane
  in `coaps_server.h:42,45`, so the ring arithmetic at `coaps_server.c:176,198` is not at fault.
- **Fix:** not written.

## Audit F1/F2 "the PQC stack overflows an ESP32 worker by 3-5x" - NOT A BUG, the floors are enforced

- **Status:** CLOSED 2026-08-09 as not-a-bug, on the severity claim. Filed so the claim is not acted
  on twice.
- **The claim:** `crypto.md` F1 and F2 measure sntrup761 decapsulation at ~23 KB of frame and
  ML-KEM-768 decapsulation at ~9 KB, then rate both category C on the grounds that "a FreeRTOS worker
  task on an ESP32 is typically stacked at 4-8 KB, so the SSH KEX path overflows by 3-5x".
- **Why it does not hold:** the 4-8 KB is a generic FreeRTOS default, not this library's. The worker
  stack is sized per feature combination in `protocore_config.h:306-332`, and the numbers there are
  the same ones the audit measured:
    - sntrup761 + reverse-SSH client: 40960
    - sntrup761, server only: 32768
    - ML-KEM only (sntrup761 explicitly off): 16384
    - SSH without PQC: 12288; no SSH: 8192
      The block's own comment states "the reverse-SSH CLIENT runs KeyGen+Decaps whose FO re-encrypt
      peaks ~32 KB, the SERVER runs Encaps only (~22 KB)".
- **And it is enforced, not merely defaulted:** four `#error` guards fail the build when the stack is
  set by hand below the floor - `:7053` (RSA/OIDC), `:7062` (curve25519/ed25519), `:7074` (ML-KEM),
  `:7093` (sntrup761, naming 32768 server / 40960 client). A build that starves the KEX does not
  link, it stops at the preprocessor with the number it needs.
- **What survives from F1/F2:** two smaller points, neither category C. The scratch is
  function-scope, so it rides the SRCBANNED #19 ratchet like the other 984 sites. And the frames are
  abandoned unwiped - `f`/`ginv` (the sntrup761 private key), `r`, and ML-KEM's `shat`/`mprime` -
  which is the same exposure as F7 and belongs with it, not with a stack-overflow finding.
- **Lesson for the next audit read:** the figures were right and the consequence was wrong. The
  audit did not read `PC_WORKER_TASK_STACK`.

## The hash context became a view, so every copy of one silently aliased its source

- **Status:** FIXED 2026-08-09 across `2f6875793`, `66c2a024a` and the dtls_conn helper commit.
  Found by the native suite; the shape was proved with a worktree at the pre-cascade commit.
- **Symptom:** the DTLS and QUIC suites disagreed with the server on the Finished MAC
  (`test_dtls_conn.c:441`, five tests), `native_coaps_server` took a SIGBUS, and the h3 / quic
  servers failed to complete a handshake.
- **Root cause:** the ownership cascade turned `pc_sha256_ctx` / `pc_sha512_ctx` into views - `rx`,
  `tx` and `fs` point into the caller's working bytes. Anything that duplicated or shared those
  bytes therefore aliased a live hash instead of snapshotting it. It surfaced three ways, and the
  first two look nothing alike:
    1. **Struct assignment.** `pc_sha256_ctx b = a;` was a snapshot when the context owned its
       storage. 19 sites finalized the copy and destroyed the running transcript.
    2. **By-value parameter.** `complete_handshake_from_flight(DtlsConn *, pc_sha256_ctx tr, ...)`
       is the same copy in argument form, which a search for `ctx X = Y;` does not find.
    3. **One shared work buffer.** The test conversion gave each TU a single `tw[4096]` and passed
       it to every entry point. A context keeps working out of the bytes it was initialized with,
       so a one-shot taking the same buffer overwrote it mid-life: the `pc_ed25519_verify` between
       CertificateVerify and Finished clobbered the transcript.
- **Why the library was never exposed:** `src/` holds every context in its owner and passes it by
  pointer. A sweep for all three forms across `src/` returns nothing. The hazard was real but only
  the tests tripped it.
- **Fix:** the copies are gone - `final()` compresses into a state copy and leaves the context
  running, so the snapshot they existed for is unnecessary. 23 test contexts take their own storage,
  and the one by-value helper rebinds onto its own span. Both context headers now state that the
  three pointers are the caller's and a struct copy aliases them.
- **Nothing caught it:** the aliasing is invisible to the compiler - the copy is well-formed C - and
  undefined behaviour made it luck-dependent, so `native_quic_tls` and `native_coaps` passed on the
  same bug that made `native_tls13_kdf` derive a wrong master secret.

## The HkdfLabel scratch reserves 514 bytes for a 51-byte worst case, and the clamp is why

- **Status:** OPEN, found 2026-08-08 in the `src/` resource audit (`crypto.md` F11). The ban-19 half
  is closed - the buffer moved out of function scope into the caller's borrow - and this is the
  remainder. Needs a contract decision, not a patch.
- **Symptom:** `crypto/kdf/hkdf.c:21` sets `HKDF_INFO_CAP` to `(2 + 1 + 255 + 1 + 255)` = 514, and
  every consumer's `PC_HKDF_BORROW` carries it. The file's own comment at `:71-73` bounds what this
  tree actually produces: the longest label is `tls13 client in` (15) and the context is a
  Transcript-Hash (<= 32) or empty, so 2 + 1 + 15 + 1 + 32 = **51 bytes** of the 514 are ever used.
- **Why it is not just a smaller number:** the 514 is not arbitrary, it matches the clamp.
  `:78-79` reads `strnlen(label_prefix, 255)` and `strnlen(label, 255 - prefix_len)`, so a caller
  that passed a long label would legitimately fill the buffer. Shrinking the buffer without
  shrinking the clamp turns an over-reservation into an overflow. The two move together or not at
  all.
- **The decision:** `TUNING.md:100` says a pool size is the TU's precomputed worst case, not a
  chosen number - which argues for sizing to this tree's real callers and clamping to match. Against
  that, `pc_hkdf_expand_label_ctx` is written as a protocol-general RFC 8446 sec 7.1 primitive whose
  label is `opaque<7..255>`, and narrowing the clamp narrows what it accepts. Roughly 460 bytes per
  consumer borrow ride on the answer.

## Five crypto TUs carry PC_CRYPTO_HOT against crypto_opt.h's own prohibition, and its own bench numbers

- **Status:** OPEN, found 2026-08-08 in the `src/` resource audit (`crypto.md` F19). Needs a decision,
  not a patch: the two halves of `crypto_opt.h` disagree and only the maintainer can pick.
- **Symptom:** `crypto_opt.h:40-46` caveat 1 says to apply `PC_CRYPTO_HOT` ONLY to code that is
  constant-time by structure, and "Do NOT put it on scalar-multiplication / bignum / point-arithmetic
  code that relies on branchless mask-selects". All five TUs that carry it are those categories:
    - `crypto/asymmetric/bignum.c:32` and `rsa.c:25` - bignum.
    - `crypto/asymmetric/curve25519.c:31` - a Montgomery ladder resting on `pc_gf_cswap:305`.
    - `crypto/asymmetric/ed25519.c:32` - point arithmetic resting on `ed_cswap:457`.
    - `crypto/asymmetric/ecdsa.c:74,76` - point arithmetic resting on `pt_table_select:666`, and it
      takes the stronger `PC_CRYPTO_HOT_PEEL` on the S3.
- **Why this is not a straight removal:** the caveat argues those paths are accelerator-dominated so
  the `-O` level "buys them almost nothing - all risk, no reward". The same header's measured
  per-die defaults (`:69-74`) contradict that for two of them: on the P4, x25519 is 6.8% and ed25519
  4.5% faster at `-O3`, and `:54` records that the S3's Ed25519 sign is 1.2% SLOWER at `-O3`. So
  these TUs were benched, and the pragma on at least curve25519/ed25519 buys a measured win.
- **The exposure, stated honestly:** `#pragma GCC optimize` applies to the whole TU, and the
  documented risk is if-conversion running backwards on a mask-select, turning
  `dst->X[i] |= table[e].X[i] & mask;` into a branch on a secret index. Nobody has disassembled to
  show it happens here. `SRC_LAW.md`'s "Guarantees are proven at the binary" requires exactly that
  chain for a constant-time claim, and the chain does not exist for these five either way.
- **What would settle it:** disassemble the five mask-select sites at the level each TU actually
  builds at and check no branch depends on a secret. That decides it on evidence instead of on which
  half of the header is believed. Until then the choice is the maintainer's: keep the measured speed,
  or drop the pragma on the three point/bignum TUs and take the loss.

## Six SSH sites read a slot's crypto bytes before the slot was bound, so the first host-key set faulted

- **Status:** FIXED 2026-08-09 in `145c3b925`, found by the native suite (`native_ssh_pqc`).
- **Symptom:** `pc_ssh_hostkey_ed25519_set()` segfaulted. gdb put it in `pc_sha512_update` with the
  key pointer at address 0, three frames under `pc_ed25519_pubkey`.
- **Root cause:** the crypto-ownership cascade moved every hash and signature onto caller-supplied
  working bytes, and the SSH sites take theirs from `ssh_pkt[i].crypto_work`. That member is null
  until `ssh_pkt_slot_storage()` binds it, and `cli_crypto_work()` returns null when the pool cannot
  cover the slot. Six sites read it without either:
    - `transport/ssh_transport.c:117` - `pc_ssh_hostkey_ed25519_set`, the one that faulted.
    - `transport/ssh_transport.c:1011` - the Ed25519 arm of `sign_hash`; the ECDSA and RSA arms
      beside it were already guarded.
    - `connection/ssh_client.c` x4 - `cli_crypto_work()` passed straight in as an argument, so the
      null check inside it bought the caller nothing.
- **Worst consequence:** a null write and a null read inside key derivation, on the first host-key
  set of a server that has one configured. Not reachable from the wire; reachable from `begin()`.
- **Fix:** each site binds the slot first or fails closed. `hybrid_sntrup761_x25519` also reached
  back into the slot for its SHA-512 instead of using the `work` it was handed; it uses the
  parameter now.
- **Nothing caught it:** no host env compiled the SSH transport with a bound pool before this run.

## test_coaps asserted a length guard that had moved out of the AEAD, and smashed the stack proving it

- **Status:** FIXED 2026-08-09 in `f7bc5d0dc`, found by the native suite (`native_coaps`).
- **Symptom:** `test_quic_aead_open_rejects_short_ciphertext` segfaulted.
- **Root cause:** the test passed `uint8_t key[16]` where `pc_aes128gcm_open()` takes
  `struct pc_aes128gcm_key *`, and passed a null tag. Its comment claimed neither was dereferenced
  because open() rejects a short ciphertext first. That guard was moved out to the callers -
  `quic_crypto.c:139` records the move - so the portable backend now casts the key and calls
  `set_j0()` on its first line, writing past a 16-byte stack array.
- **Fix:** the test is deleted rather than repaired: with the current contract it cannot be written
  at all, since a null tag faults in `pc_ct_eq` even with a valid key context. The guard is covered
  where it now lives, by `test_gcm_open_rejects_short` in `test_quic_crypto`. Every caller of
  `pc_aes128gcm_open` was checked for the length guard first: `quic_crypto.c`, `dtls_record.c` and
  `tls_record.c` all hold it, and `smb2.c` reads its tag from a separate 16-byte copy.

## The mbedtls-backed asymmetric and CCM paths may call the heap at run time

- **Status:** OPEN, found 2026-08-08 in the `src/` resource audit (`crypto.md` F12). **DEFERRED** by
  the maintainer 2026-08-08: not to be chased until someone confirms it against a linked binary.
- **Symptom:** unconfirmed. On the accelerated builds, one X25519 key exchange, one ECDSA sign, one
  RSA verify and every AES-CCM record may each take and release heap, which would break the
  "no heap after `begin()`" guarantee on the configuration the library actually ships.
    - `crypto/asymmetric/curve25519.c:270-285` - `pc_gf_inv` declares four `mbedtls_mpi` and calls
      `mbedtls_mpi_read_binary` / `mbedtls_mpi_exp_mod` / `mbedtls_mpi_write_binary`.
    - `crypto/asymmetric/ecdsa.c:95-118,128-151,160-183,189-212` - the four mbedtls entry points,
      through `mbedtls_ecp_group_load` and `mbedtls_ecp_mul`.
    - `crypto/asymmetric/rsa.c:63-106`.
    - `crypto/aead/aesccm.c:51-52,81-82` - `mbedtls_ccm_setkey` reaches `mbedtls_cipher_setup`.

    The other three mbedtls uses in that tree (`aes256ctr.c:59`, `aes_cmac.c:35`, and the SHA
    backends) take plain context structs and do not allocate.

- **Root cause:** if it holds, `mbedtls_mpi_grow` under `read_binary` is `mbedtls_calloc`,
  `exp_mod` allocates its own sliding-window table, and `cipher_setup`'s `ctx_alloc_func()` is
  `mbedtls_calloc`. Whether any of that is reachable depends on the platform's
  `MBEDTLS_PLATFORM_MEMORY` / `MBEDTLS_PLATFORM_STD_CALLOC` configuration, which this repo does not
  own.
- **Why it is deferred rather than fixed:** the finding is indirect, inferred through a vendor
  library rather than observed. `SRC_LAW.md` requires a guarantee about emitted code to be proven at
  the binary, and that proof does not exist either way here. Confirming it needs the linked `.text`
  checked for a reachable allocator call on a real ESP32 build; the audit that raised it says the
  same about itself. Acting on an unconfirmed vendor-behavior claim would mean rewriting four
  working crypto paths on a guess.
- **What would settle it:** build for `esp32dev`, disassemble the linked image, and check whether
  `mbedtls_calloc` is reachable from `pc_gf_inv`, the four `ecdsa.c` entry points, `rsa.c`'s verify,
  and `mbedtls_ccm_setkey`. Either it is reachable, and this becomes a real SRC_LAW rule-1 violation
  with a known blast radius, or it is not, and the entry is closed as NOT A BUG.
- **Filed site:** `git_project/audit/resource/crypto.md` F12.

## Six TUs write through an unchecked pool borrow, so exhaustion is a NULL write or a garbage key

- **Status:** OPEN, found 2026-08-08 in the `src/` resource audit. Cross-cutting: every site below is
  filed in its own area report, the class is filed in none.
- **Symptom:** the pools fail closed and these callers fail open.
    - `crypto/mac/hmac_sha256.c:72` returns without setting `ctx->okey`, which is then used.
    - `crypto/rng/rng.c:74` dereferences an unchecked `pc_secure_persist_span()`.
    - `network_drivers/application/smb/smb2.c:917,929,987,998` pass `pc_secure_span(...).buf` with no
      `pc_span_ok()`.
    - `network_drivers/presentation/http/http3/quic_crypto.c:46,50,180` write through `pc_secure_alloc`
      with no NULL test.
    - `services/security/ikev2/ikev2.c:1434,1458` the same, and `sk_message_build:1697` discards the
      seal result on top.
    - `services/system/esp/esp.c:78,113` the same.

    The AEAD backends close the trap: `core_setup/hal/portable/portable_aes128gcm.c:209-213` writes
    through `storage` unconditionally, so a NULL arrives as a store rather than as a check.

- **Root cause:** `mmgr/secure.h:99-105` states the contract - "Returns NULL if the request does not
  fit - callers MUST handle null and fail closed" - and `pc_secure_span()` yields an empty span so an
  omitted `pc_span_ok()` writes nothing rather than dereferencing null. Both are advisory. Nothing in
  the signature forces the check, so six independent TUs skipped it the same way.
- **Worst consequence:** `hmac_sha256.c:72` leaves `ctx->okey` indeterminate and proceeds. Under
  deterministic ECDSA nonce generation that is a nonce-reuse path, and nonce reuse recovers the
  private key. The rest are NULL writes and a reset.
- **Nothing can catch it:** no test drives an arena to exhaustion, so every one of these paths is
  unreachable in the native suite by construction.
- **Fix:** not written. The durable form is to make the check unskippable rather than to add six
  checks - a borrow that cannot be handed to a keyed init without passing `pc_span_ok()` first.
- **Filed sites:** `git_project/audit/resource/crypto.md`,
  `network_drivers_application.md`, `network_drivers_http.md`, `services_security_storage.md`,
  `services_net_system.md`.

## `PC_PLAINTEXT_ARENA_SIZE` is a chosen number, not a sum, so concurrent borrows are never proved

- **Status:** PARTIALLY FIXED 2026-08-09. The arena is now a sum:
  `PC_PLAINTEXT_SCRATCH + PC_PLAINTEXT_WORK_H2CONN + PC_PLAINTEXT_WORK_H3CONN + 256`, and the twelve
  board profiles pin `PC_PLAINTEXT_SCRATCH` instead of the total, so a per-connection term always
  adds on top of whatever a die tuned. What remains open is the transient half: the scratch figure is
  still a per-die guess rather than the max over the declared `PC_PLAINTEXT_WORK_*` transient terms,
  so the 8,760 B compressed-SSH draw against an 8,192 B c2/s2 scratch is still unproved.
- **Status:** OPEN, found 2026-08-08 in the `src/` resource audit. Cross-cutting: the derivation gap
  and the failure it causes are filed in two different area reports and connected in neither.
- **Symptom:** `protocore_config.h:6477` sets `PC_PLAINTEXT_ARENA_SIZE` to 8192. The plaintext work
  terms sum to **15,011 B**. Each module header asserts only that its own term fits the arena alone,
  so no two concurrent borrows are ever proved to coexist. The live consequence: SSH's real nested
  draw is **8,760 B** with `PC_ENABLE_SSH_ZLIB=1`, against the 8,192 B arena, so publickey auth fails
  closed on every compressed session.
- **Root cause:** the secure side derives `PC_SECURE_ARENA_SIZE` from its `PC_WORK_*` terms, so a new
  draw that is not budgeted fails the build. The plaintext side has no equivalent derivation, so a
  new draw that is not budgeted fails at run time instead, on the path that happened to be second.
  `PC_PLAINTEXT_WORK_SSH_TRANSPORT` is defined and referenced nowhere, which is the same gap showing
  from the other direction.
- **Nothing can catch it:** an assert that a term fits alone passes for every term individually at
  any arena size down to the largest single term.
- **Fix:** not written. Derive the plaintext arena from its terms the way the secure one is derived,
  which turns both of the above into build failures.
- **Filed halves:** `git_project/audit/resource/mmgr_shared_primitives.md` (the missing derivation,
  and the 15,011 B sum), `network_drivers_ssh_security_codec.md` (the 8,760 B draw and the orphan).

## The plaintext pool has no PSRAM seam, so the converted HTTP buffers landed in DRAM twice

- **Status:** OPEN, found 2026-08-09 converting `h2_conn` and `h3_conn` onto the connection-owns-the-
  memory law.
- **Symptom:** `h2_conn` and `h3_conn` used to hold their frame, header-block, scratch and per-stream
  buffers inline, inside `s_h2` (`PC_H2_POOL_ATTR`) and `s_qpool` (`PC_QUIC_POOL_ATTR`) - both of
  which a board can move to PSRAM. As pool borrows those bytes now live in
  `PlainPoolStorageCtx.mem`, which has no placement attribute, so on an S3 they are 346,816 B of
  internal DRAM where they were previously PSRAM-movable.
- **Second multiplier:** the storage is `[PC_REG_POOL_SLOTS][PC_PLAINTEXT_ARENA_SIZE]`, and
  `PC_REG_POOL_SLOTS` is `PC_WORKER_COUNT + 1`. A per-connection persistent borrow is therefore
  budgeted once per worker slot plus once for the ghost, which never serves a connection. On the
  default single-worker build that is 2x: 693,632 B on an S3 against ~400 KB of usable DRAM.
- **Not a new class:** `PC_WORK_SSH_CONN` and `PC_WORK_TLS_CONN` are budgeted the same way in
  `s_secure_storage`, which also has no placement attribute. The conversion made the existing
  accounting expensive enough to see, it did not introduce it.
- **Fix:** not written, and it is two decisions, not one. Whether pool storage gets the placement
  attribute the module pools already have, and whether a per-connection term is budgeted per worker
  slot at all when a connection is served by exactly one worker. Both are the owner's call.

## `Content-Length` folds with no overflow guard, so a 33-digit value frames as 0

- **Status:** OPEN, found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/http1-core.md` #1).
- **Symptom:** `http_parser.c:381` is `cl = cl * 10 + (size_t)(*q - '0')` inside the digit loop, with a
  digit-class check and a duplicate-disagreement check around it and no bound on the accumulator.
  `Content-Length: 18446744073709551616` folds to **0** on both 32- and 64-bit `size_t`;
  `Content-Length: 4294967301` folds to **5** on 32-bit, which is the ESP32 target. The parser reaches
  `PARSE_COMPLETE`/`PARSE_BODY` and the residual octets are reparsed as a pipelined request.
- **Root cause:** the comment three lines above cites RFC 7230 §3.3.2 `Content-Length = 1*DIGIT` and the
  loop enforces exactly that - the grammar, not the range. RFC 9110 sec 8.6 states the other half in the
  same paragraph: a recipient "MUST anticipate potentially large decimal numerals and prevent parsing
  errors due to integer conversion overflows". RFC 9112 sec 6.3 item 5 makes an invalid `Content-Length`
  a 400 and a connection close. This is a live request-smuggling vector.
- **Nothing can catch it:** `test_pentest.c:217` feeds the 20-digit value and then asserts only
  `assert_http_bounded()` (`:105-112`: buffer indices in range, `parse_state` a valid enum), which cannot
  distinguish 400 from silent acceptance.
- **Fix:** not written. Refuse when `cl > (SIZE_MAX - 9) / 10` before the multiply and return
  `PARSE_BAD_REQUEST`, and give the test an assertion on the status rather than on boundedness.

## Telnet sends 22 bytes from a 19-character literal on every accept

- **Status:** FIXED 2026-08-09. Found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/ssh-auth-connection-telnet.md` #15).
- **Symptom:** `telnet.c:142` is `raw_send(slot, "PC Telnet ready\r\n> ", 22);`. The literal is 19
  characters, 20 bytes with its NUL. Two bytes past the end of the string constant are read and pushed
  onto the wire on every connection.
- **Root cause:** a hand-counted length on a literal. Nothing recomputes it, and `raw_send` takes the
  count as given.
- **Nothing can catch it:** `test_telnet.c:60-69` compares only the first 6 bytes of the greeting.
- **Fix:** the call site was already `raw_send(slot, greet, sizeof(greet) - 1)` (19 bytes); the accept
  test now pins the whole 25-byte output (negotiation + greeting) so a hand-counted over-read cannot
  regress. Verified: `native_telnet`.

## Telnet ends a subnegotiation on a bare 240, injecting the parameter bytes into the command line

- **Status:** FIXED 2026-08-09 (`telnet.c` `TN_SB`). Found 2026-08-08 auditing `test/` for RFC
  conformance (`git_project/audit/ssh-auth-connection-telnet.md` #4).
- **Symptom:** `TN_SB` closed on a bare `T_SE` (240), but RFC 855 terminates a subnegotiation only on
  `IAC SE` (255 240). A data byte 240 inside the parameters ended the subnegotiation early, and the
  bytes after it fell through to the NVT command line - an injection.
- **Nothing can catch it:** the only subneg test fed a bare 240 as the terminator and asserted the
  trailing bytes became the line, enshrining the bug.
- **Fix:** a `TN_SB_IAC` state, so a subnegotiation ends only on `IAC SE` and a bare 240 is data. The
  test now uses `IAC SE`, and a negative test proves a bare 240 mid-parameters does not inject.
  Verified: `native_telnet`, 23/23.

## The SSH userauth service name is parsed and never compared

- **Status:** FIXED 2026-08-09 (`ssh_auth.c` `pc_ssh_auth_parse_request`). Found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/ssh-auth-connection-telnet.md` #3).
- **Symptom:** `ssh_auth.c:279` reads the `service` field of `SSH_MSG_USERAUTH_REQUEST` into
  `req->service`, and that member appears nowhere else in the file. A request naming service `"bogus"`
  authenticates exactly as one naming `"ssh-connection"`.
- **Root cause:** RFC 4252 sec 5 makes the service name part of what is authenticated - "if the service
  does not exist, authentication MUST NOT be accepted" - and it is also inside the signed blob, so the
  signature covers a value the server never checks. Parsing a field into a struct is not validating it.
- **Nothing can catch it:** every test hardcodes `"ssh-connection"`.
- **Fix:** the parser rejects any service but `ssh-connection` (`str.eq`, the bounded compare), so a
  foreign name fails the request (RFC 4252 sec 5). Negative test `test_parse_rejects_foreign_service`.
  Verified: `native_ssh`, 265/265.

## A publickey signature is verified against a session id that no key exchange produced

- **Status:** FIXED 2026-08-09 (`ssh_auth.c` `pc_ssh_auth_handle_pubkey`). Found 2026-08-08 auditing
  `test/` for RFC conformance (`git_project/audit/ssh-auth-connection-telnet.md` #2).
- **Symptom:** the signed blob is `string(session_id) || request` (RFC 4252 sec 7), but the length was
  taken from `ssh_sess[i].session_id_len` without consulting `have_session_id`. Before any KEX both are
  zero, so the blob degenerated to `uint32(0) || prefix`: a signature bound to no session at all, which
  is exactly the binding the field exists to provide.
- **Nothing can catch it:** every publickey test called one `set_session_id_0_to_31()` helper, so no
  test ever drove the handler with no session id bound.
- **Fix:** the signature path refuses when `have_session_id` is false. Negative test
  `test_pubkey_without_session_id_fails`. Reachability was already limited by the phase guard in
  `ssh_server.c`, so this closes the auth layer rather than a live hole.
  Verified: `native_ssh` + 14 more envs, 573/573.

## `pty-req`, `exec` and `env` are accepted on the request name alone

- **Status:** FIXED 2026-08-09 (`ssh_channel.c` `pc_ssh_channel_handle_request`). Found 2026-08-08
  auditing `test/` for RFC conformance (`git_project/audit/ssh-auth-connection-telnet.md` #8, #9).
- **Symptom:** acceptance was one boolean over four `mem.cmp` name matches, so a CHANNEL_REQUEST
  truncated immediately after `want_reply` was answered `CHANNEL_SUCCESS`. A `pty-req` carrying no
  TERM, no dimensions and no mode blob, and an `exec` carrying no command, both succeeded.
- **Nothing can catch it:** the accept-set test sent exactly that truncated form and asserted SUCCESS,
  and the SCP test passed a declared-but-absent command through the same path.
- **Fix:** each type is checked for the fields RFC 4254 sec 6.2 / 6.4 / 6.5 defines for it, read from a
  copy of the offset so the file-transfer classifier still finds the exec argument. `shell` is
  unchanged, carrying no request-specific data. New negative
  `test_chan_request_truncated_fields_refused`; the two tests that pinned the old behaviour now assert
  the RFC's. Verified: `native_ssh` + 14 more envs, 573/573.

## NVT `CR NUL` discards the carriage return and strands the line

- **Status:** FIXED 2026-08-09 (`telnet.c` `handle_data`). Found 2026-08-08 auditing `test/` for RFC
  conformance (`git_project/audit/ssh-auth-connection-telnet.md` #12).
- **Symptom:** RFC 854 gives `CR NUL` as a carriage return alone, and clients send it for Enter. `CR`
  returned early awaiting `LF`, then the `NUL` was swallowed by the `b < 0x20` control-byte arm, so
  neither byte did anything: the line was never dispatched and the user's Enter did nothing.
- **Nothing can catch it:** the only CR test used `{'a','\r',0x01,'b','\n'}`, which never pairs CR with
  NUL.
- **Fix:** a `saw_cr` flag pairs the CR with whichever byte completes it, and `CR LF` and `CR NUL` both
  end the line through one `dispatch_line`. Test `test_cr_nul_dispatches_line`.
  Verified: `native_telnet`, 24/24.

## `CHANNEL_WINDOW_ADJUST` and `CHANNEL_EOF` skip the pre-auth guard the other six arms have

- **Status:** FIXED 2026-08-09 (`ssh_server.c` `pc_ssh_server_dispatch`). Found 2026-08-08 auditing
  `test/` for RFC conformance (`git_project/audit/ssh-auth-connection-telnet.md` #13).
- **Symptom:** six channel arms open with `if (!s->authed) return -1;` and these two did not, so both
  were dispatched on an unauthenticated session. Not exploitable: `chan_by_id` finds no open channel
  before authentication, so both handlers were no-ops. Fixed as an asymmetry, not a live hole.
- **Nothing can catch it:** the `authed_arms[]` list the three suites iterate omitted exactly these two
  message types, so the gap was enshrined rather than caught.
- **Fix:** both arms carry the guard, and the `authed_arms[]` list in all three suites now names all
  eight. Verified: `native_ssh` + 14 more envs, 573/573.

## Two SSH test envs build the SSH stack without defining `PC_ENABLE_SSH`

- **Status:** FIXED 2026-08-09 (`test/test_matrix.json`, `native_ssh_hardened` and `native_ssh_pqc`).
  Found 2026-08-09 while running the suites for the four fixes above.
- **Symptom:** both envs list the SSH sources in `src` but their `flags` never define
  `PC_ENABLE_SSH=1`, so the derived arena terms for SSH were absent and the pool borrows on the
  publickey and KEX paths failed. Every borrow-taking test in them failed closed:
  `test_ecdsa_publickey_auth_succeeds_when_password_disabled` returned USERAUTH_FAILURE, and all five
  `native_ssh_pqc` KEX tests returned -1 out of `ssh_kexinit_parse`.
- **Nothing can catch it:** the tests that do not take a borrow still pass, so each env looked healthy
  as long as nobody added one that does. `native_ssh_pqc` now runs 10 cases where it reported 5.
- **Fix:** both envs declare `PC_ENABLE_SSH=1`, then `platformio.ini` regenerated with
  `test/gen_test_envs.py`. A sweep of the matrix found no third env in this state.
  Verified: `native_ssh_hardened` 4/4, `native_ssh_pqc` 10/10.

## The cipher and MAC were forced equal both ways, so a conforming client could not key

- **Status:** FIXED 2026-08-09 (`ssh_transport.c` `ssh_kexinit_parse`, `ssh_keymat.h`, `ssh_dh.c`,
  `ssh_packet.c`). Found 2026-08-08 auditing `test/` for RFC conformance
  (`git_project/audit/ssh-transport-kex.md` #6).
- **Symptom:** `ssh_transport.c:674` rejected the KEXINIT when `s2c != c2s`, and `:700` when
  `m_s2c != m_c2s`. RFC 4253 sec 7.1 negotiates the two cipher lists and the two MAC lists
  independently, so a client whose directions carried different preferences was refused outright even
  though each list negotiated cleanly on its own.
- **Root cause:** the session and the keymat each held one `cipher_alg` / `mac_alg`, so the two
  directions had nowhere to differ. Everything else was already per-direction: `aes_key_c2s/_s2c`,
  `chacha_key_*`, `gcm_ctx_*`, and both 64-byte `mac_key_*` buffers.
- **Nothing can catch it:** `test_kexinit_parse_rejects_direction_mismatch` asserted the `-1`, pinning
  the behaviour the RFC contradicts.
- **Fix:** the mode is stored per direction (`cipher_mode_c2s/_s2c`, `mac_mode_c2s/_s2c`) and read
  through `km_send_cipher` / `km_recv_cipher` / `km_send_mac_mode` / `km_recv_mac_mode`, mirroring the
  existing `km_send_mac` / `km_recv_mac` key selectors. `ssh_keymat_wipe` releases each GCM context on
  its own mode, so a mixed session cannot leak one. The test now asserts both directions negotiate
  independently. Verified: `native_ssh` + 16 more envs, 594 cases.

## A wrong KEX guess was parsed as the real KEXDH_INIT

- **Status:** FIXED 2026-08-09 (`ssh_transport.c` `ssh_kexinit_parse`, `ssh_server.c` KEXDH_INIT arm).
  Found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/ssh-transport-kex.md` #5).
- **Symptom:** the KEXINIT parser returned after the two compression name-lists and never read the two
  language lists, the `first_kex_packet_follows` boolean, or the reserved uint32. A client that guesses
  sends its KEXDH_INIT ahead of our reply; when the guess lost negotiation that packet carries an
  algorithm nobody agreed on, and RFC 4253 sec 7.1 says it MUST be silently ignored. It was parsed as
  the genuine one instead.
- **Root cause:** the fields after compression were simply never consumed, so the flag that says "a
  speculative packet is coming" was invisible to the server.
- **Nothing can catch it:** every test builder hard-codes the flag to 0, so no test ever sent a guess.
  OpenSSH and CycloneSSH do not guess either, which is why the interop suite never saw it.
- **Fix:** the parser reads the remaining fields and records `drop_guessed_kex_pkt` when the flag is
  set and negotiation did not settle on the client's first-listed kex _and_ host key -
  `negotiate_alg` now reports the winning entry's position, so "the guess held" is position 0 in both.
  The dispatcher consumes one KEXDH_INIT silently when the flag is up. Every `negotiate_*` caller moved
  from `!f(...)` to `f(...) < 0`, since position 0 is a success. Test
  `test_wrong_kex_guess_is_dropped`. Verified: `native_ssh` + 4 more envs, 342 cases.

## EXT_INFO was re-advertised on every re-key

- **Status:** FIXED 2026-08-09 (`ssh_server.c` NEWKEYS arm). Found 2026-08-08 auditing `test/` for RFC
  conformance (`git_project/audit/ssh-transport-kex.md` #12).
- **Symptom:** the NEWKEYS arm emitted EXT_INFO whenever `ext_info_c` was set, and
  `ssh_kexinit_parse` re-reads that flag from every KEXINIT including a re-key's, so the message went
  out again on each one. RFC 8308 sec 2.4 gives a server exactly two places to send it: after its
  first NEWKEYS, or immediately preceding USERAUTH_SUCCESS.
- **Nothing can catch it:** `test_ssh_kexinit_midsession_rekey` drives a full re-key with an
  ext-info-c-bearing KEXINIT and never checked what the second NEWKEYS emitted.
- **Fix:** `ext_info_sent` on the session gates it to the first NEWKEYS. The re-key test now asserts no
  EXT_INFO follows. Verified: `native_ssh`, `ssh_comp`, `ssh_kbdint`, `ssh_pqc`, 337 cases.

## The identification-string bound was off by two against RFC 4253 sec 4.2

- **Status:** FIXED 2026-08-09 (`ssh_transport.c` `ssh_transport_recv_banner`). Found 2026-08-08
  auditing `test/` for RFC conformance (`git_project/audit/ssh-transport-kex.md` #15).
- **Symptom:** the banner was rejected only at `n >= SSH_VERSION_MAX` (256) measured on the content
  with CR stripped, so 255 content bytes plus CR LF - 257 on the wire - was accepted. RFC 4253 sec 4.2
  bounds the string at 255 _including_ the Carriage Return and Line Feed, so the content limit is 253.
- **Nothing can catch it:** `test_banner_and_build_caps` pinned only the 256-byte rejection, leaving
  the RFC's actual bound untested in both directions.
- **Fix:** `SSH_VERSION_CONTENT_MAX` (253) is the bound the SSH- line is checked against.
  `test_recv_banner_rfc_length_bound` pins 253 accepted and 254 refused.
  Verified: `native_ssh` + 16 more envs.

## One forged UDP datagram ended a live DTLS association

- **Status:** FIXED 2026-08-09 (`dtls_conn.c` `process_ciphertext_record`), superseding the OPEN
  entry this replaces. Found 2026-08-08 auditing `test/` for RFC conformance
  (`git_project/audit/dtls13-rpk.md` #3).
- **Symptom:** RFC 9147 sec 4.5.2: "invalid records SHOULD be silently discarded, thus preserving
  the association... Implementations which choose to generate an alert instead MUST generate fatal
  alerts... any implementation which does this will be extremely susceptible to DoS attacks because
  UDP forgery is so easy. Thus, generating fatal alerts is NOT RECOMMENDED." A record failing its
  AEAD called `fail(c, ALERT_DECRYPT_ERROR)` and killed the connection, so anyone able to send a
  packet to the port could end an established session with 48 bytes of noise.
- **Also fixed:** a ciphertext record arriving for an epoch whose keys do not exist yet took the
  same fatal path, which is the same attack against a handshake in progress.
- **Nothing can catch it:** the sibling `open_app` path had a tampered-record test; the connection
  path had none, and two tests asserted the fatal behaviour outright.
- **Fix:** both cases advance past the record and keep walking the datagram, the way the replay
  branch beside them already did. Verified: 230/230 across thirteen DTLS/TLS envs.

## A DTLS receiver rejected a legal legacy_record_version

- **Status:** FIXED 2026-08-09 (`dtls_record.c` `pc_dtls_plaintext_parse`), superseding the OPEN
  entry this replaces. Found 2026-08-08 auditing `test/` for RFC conformance
  (`git_project/audit/dtls13-rpk.md` #4).
- **Symptom:** RFC 9147 sec 4: legacy_record_version "MUST be set to {254, 253} for all records
  other than the initial ClientHello... where it may also be {254, 255} for compatibility purposes.
  It MUST be ignored for all purposes." The parser rejected anything but 0xFEFD, so a client sending
  the compatibility value on its first ClientHello - which the RFC explicitly permits - was turned
  away before the handshake began.
- **Nothing can catch it:** two tests asserted the rejection, one of which existed only to cover the
  second half of the `||` that performed it.
- **Fix:** the field is read past. The test now asserts both {254,255} and an arbitrary value parse;
  the test written to cover the removed branch is gone with it.

## A retransmitted DTLS fragment could rewrite bytes already reassembled

- **Status:** FIXED 2026-08-09 (`dtls_handshake.c` `reasm_add`). Found 2026-08-08 auditing `test/`
  for RFC conformance (`git_project/audit/dtls13-rpk.md` #10).
- **Symptom:** RFC 9147 sec 5.5: "Senders MUST NOT change handshake message bytes upon
  retransmission. Receivers MAY check that retransmitted bytes are identical and SHOULD abort the
  handshake with an illegal_parameter alert if the value of a byte changes." Reassembly copied every
  fragment over whatever was already there, so a peer could deliver one ClientHello, then rewrite
  part of it with a second fragment covering the same range - the transcript hash would cover bytes
  the first flight never contained.
- **Nothing can catch it:** `test_hs_reasm_overlap_and_duplicate` exercises three overlapping
  fragments, all carrying identical bytes, so the silent overwrite looked correct.
- **Fix:** the fragment's overlap with each range already held is compared before the copy; a
  disagreement fails the reassembly. Ranges were already tracked, so the check costs one compare per
  overlapping interval. Verified: `native_dtls_hs` 22/22, 202/202 across eleven DTLS envs.

## An invalid DTLS cookie drew the wrong alert

- **Status:** FIXED 2026-08-09 (`dtls_conn.c`). Found 2026-08-08 auditing `test/` for RFC
  conformance (`git_project/audit/dtls13-rpk.md` #1).
- **Symptom:** RFC 9147 sec 5.1: "If a server receives a ClientHello with an invalid cookie, it MUST
  terminate the handshake with an illegal_parameter alert. This allows the client to restart the
  connection from scratch without a cookie." The server sent `handshake_failure` (40), which tells
  the client the parameters could not be agreed - so a client that would have retried without a
  cookie gives up instead.
- **Nothing can catch it:** both cookie-rejection tests asserted 40.
- **Fix:** `illegal_parameter` (47) on the cookie path only; the paths that genuinely cannot agree
  parameters still send 40. Verified: 214/214 across thirteen DTLS/TLS envs.

## A non-empty DTLS legacy_cookie was read past

- **Status:** FIXED 2026-08-09 (`tls13_msg.c`). Found 2026-08-08 auditing `test/` for RFC
  conformance (`git_project/audit/dtls13-rpk.md` #2).
- **Symptom:** RFC 9147 sec 5.3: "A DTLS 1.3-only client MUST set the legacy_cookie field to zero
  length. If a DTLS 1.3 ClientHello is received with any other value in this field, the server MUST
  abort the handshake with an illegal_parameter alert." The parser read the field and discarded it.
  In DTLS 1.3 the real cookie rides the extension, so a non-empty legacy field is a client that is
  not speaking 1.3.
- **Nothing can catch it:** `test_tls13_dtls_client_hello_shape` asserted the opposite - "a
  non-empty legacy_cookie is skipped just the same" - and every test ClientHello writes 0x00.
- **Fix:** a non-zero length fails the parse, which the caller turns into the alert.

## No common certificate type fell back to X.509 instead of failing

- **Status:** FIXED 2026-08-09 (`tls13_msg.c`, `dtls_conn.c`). Found 2026-08-08 auditing `test/` for
  RFC conformance (`git_project/audit/dtls13-rpk.md` #5).
- **Symptom:** RFC 7250 sec 4.2 outcome 2: a server that supports the extension but has no
  certificate type in common with the client "terminates the session with a fatal alert of type
  unsupported_certificate". The server answered X.509 regardless - including for a RawPublicKey-only
  offer, and always when `PC_ENABLE_TLS_RPK` is 0, because the whole extension case sat inside that
  guard and was not even parsed.
- **Root cause:** the parser recorded only whether RawPublicKey was offered, which cannot express
  "the client named types and none of them is one we can send".
- **Fix:** the extension is parsed whatever the build can answer with - outcome 2 turns on what the
  client named, not on what we support - and the ClientHello now carries `has_server_cert_type` and
  `offers_x509_server_cert` beside the existing RPK flag. A client that sent the extension and named
  neither a type we can produce gets `unsupported_certificate` (43).

## Every TLS connection init drew a fresh persistent borrow it never gave back

- **Status:** FIXED 2026-08-09 (`tls_conn.c` `tls_conn_slot_storage`). Found 2026-08-09 building the
  per-file suite the driver did not have.
- **Symptom:** `conn_init` called `pc_secure_wipe(c, sizeof(*c))` and then `secure.persist_span()`.
  The wipe nulls the very pointers that record whether the connection already holds storage, so the
  guard could never fire: a slot that closed and was re-accepted took another borrow from the
  persistent end, which is never given back. `MAX_TLS_CONNS` inits exhaust the pool permanently.
- **Root cause:** the borrow was treated as a property of the call rather than of the connection.
  `ssh_pkt_slot_storage` had the answer already - a null check on the slot's own first pointer, one
  span, split by named offsets into the slot's fields - and the shape was not copied.
- **Nothing can catch it:** `tls_conn.c` compiled in no test env at all.
- **Fix:** `tls_conn_slot_storage(TlsConn *c)` is `ssh_pkt_slot_storage` against `TlsConn`: returns
  early when `c->tx` is set, otherwise takes the one borrow and splits it across `tx`, `rx`,
  `terms`, `hash_work`, `sign_work`, `ks_work` and `hello`. `ks_work` is new - the key schedule
  reached its region by arithmetic on the borrow base, so it now has a field like the rest. init
  wipes the borrowed bytes and the session state, never the pointers. `native_tls_conn` is the env
  that was missing; the suite holds its connection in static storage, the way a pool slot is, and
  re-initialises the one slot per case, which is the path that was broken.
  Verified: `native_tls_conn` 5/5, 209/209 across thirteen TLS/DTLS/crypto envs.

## The portable TLS arm could not be built at all

- **Status:** FIXED 2026-08-09 (`protocore_config.h`). Found 2026-08-09 standing up `native_tls_conn`.
- **Symptom:** `tls_conn.c:41` asserts `PC_ENABLE_TLS_RPK` because the portable arm authenticates by
  RFC 7250 raw public key, but the config guard accepted that flag only alongside `PC_ENABLE_DTLS`
  or `PC_ENABLE_HTTP3`. The two conditions cannot both hold for a standalone software-TLS build, so
  `PC_TLS_SOFTWARE` was unbuildable on its own - which is why it had no env.
- **Fix:** the guard names the portable arm as the third carrier of the extension.

## A zero-length TLS Handshake or Alert record was both sent and accepted

- **Status:** FIXED 2026-08-09 (`tls_record.c` `pc_tls_record_protect` / `_unprotect`). Found
  2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/tls13.md` #1).
- **Symptom:** RFC 8446 sec 5.4: "Implementations MUST NOT send Handshake and Alert records that
  have a zero-length TLSInnerPlaintext.content; if such a message is received, the receiving
  implementation MUST terminate the connection with an 'unexpected_message' alert." Neither half
  was implemented: `protect` would seal one and `unprotect` would hand it back as a valid record.
- **Nothing can catch it:** `test_empty_plaintext_carries_only_the_type` sealed a zero-length alert
  and asserted it was accepted, pinning both halves of the violation.
- **Fix:** both directions refuse it, for Handshake and Alert only - application_data may be empty,
  which is how a sender pads a stream, and there is a test holding that open. The receive-side test
  builds the offending record through `protect` with a crafted content-type byte, since our own
  `protect` now declines to build it directly. The sec 5.4 all-zero-inner and sec 5.2 record-overflow
  guards were already correct but untested; both now have one. Verified: `native_tls_record` 20/20.

## A TLS 1.3 ClientHello's cipher_suites and compression methods were read past

- **Status:** FIXED 2026-08-09 (`tls13_msg.c`, `quic_tls.c`, `dtls_conn.c`, `tls_conn.c`). Found
  2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/tls13.md` #2, #3).
- **Symptom:** the parser read `cipher_suites` into a local and discarded it, and all three servers
  answered `TLS_AES_128_GCM_SHA256` unconditionally - so a ClientHello that never offered that
  suite got it in the ServerHello, against RFC 8446 sec 4.1.3. `legacy_compression_methods` was
  likewise read and dropped, where sec 4.1.2 says it "MUST contain exactly one byte, set to zero"
  and a TLS 1.3 server aborts on anything else.
- **Fix:** the parser reports the offer as `offers_aes128gcm_sha256`, matching the `offers_tls13`
  shape already in `Tls13ClientHello`, and all three servers refuse a ClientHello without it. The
  compression rule is enforced in the parser, where a malformed field belongs.
- **Note on the memory:** `Tls13ClientHello` lives inside the TLS connection's own borrow
  (`tls_conn.c` `TLS_OFF_HELLO`), and `TLS_OFF_KS` is derived from `sizeof(Tls13ClientHello)`, so
  growing the struct shifts the offsets after it. They follow correctly because they are computed
  rather than written down, and the `PC_TLS_CONN_STATE_CAP` static_assert proves the slot still
  covers them. Verified: 184/184 across all ten TLS/DTLS envs plus `native_crypto_kat`.

## HKDF-Expand had no published-vector coverage at all

- **Status:** FIXED 2026-08-09 (`hkdf.h` `pc_hkdf_expand`, `test_crypto_kat.c`, the RFC 5869 vector
  file and its curator/generator). Found 2026-08-08 auditing `test/` for RFC conformance
  (`git_project/audit/tls13.md` #5, #11).
- **Symptom:** the vector file carried `salt`/`ikm`/`prk` and the suite called only
  `pc_hkdf_extract`. Expand - the half that derives every TLS and QUIC traffic key - was checked
  against nothing published. The only >32-byte check anywhere was `test_quic_crypto.c`'s
  `expand_label_ref`, a line-by-line mirror of the same T(i) loop in the same file, so a shared
  convention error (T(0) as 32 zeros rather than empty) agreed with itself and passed.
- **Root cause of the gap:** only `pc_hkdf_expand_label` was public, and it wraps `info` in the
  RFC 8446 HkdfLabel structure. RFC 5869 Appendix A expands an arbitrary `info`, so its vectors
  could not reach the code through the exported surface at all.
- **Fix:** `pc_hkdf_expand` is exported - the bare sec 2.3 primitive the file already implemented
  internally, which is what the RFC's own vectors address. The vector file, its curator and the
  generator field list now carry `info`, `L` and `OKM` for A.1-A.3, and both halves run. A.2 asks
  for 82 bytes, three SHA-256 blocks, which is the only published multi-block case and the first
  external check the T(i) chain has ever had. The mirror in `test_quic_crypto.c` stays, narrowed to
  what it alone covers - that the HkdfLabel wrapper feeds the chain the right info block - and its
  comment now names the external anchor. Verified: 201/201 across twelve crypto/TLS/DTLS envs.

## HKDF-Expand past 255 blocks silently reused earlier key material

- **Status:** FIXED 2026-08-09 (`hkdf.c` `hkdf_expand`). Found 2026-08-08 auditing `test/` for RFC
  conformance (`git_project/audit/tls13.md` #12).
- **Symptom:** RFC 5869 sec 2.3 bounds L at 255*HashLen because the block counter is one octet. The
  counter was a `uint8_t` incremented without a bound, so a request for more than 8160 bytes wrapped
  it and T(256) repeated T(1) - the output would silently reuse key material already emitted.
- **Root cause:** the loop was written for the QUIC case, where the request never exceeds one hash
  block; the general N-block form was correct but unbounded.
- **Fix:** a request past the RFC's limit has no defined answer, so it produces none - the output is
  zeroed rather than filled with repeating blocks. No in-tree caller comes close to the bound.

## HTTP/3 error codes had no correct frame to travel in

- **Status:** FIXED 2026-08-09 (`quic_frame.c` `pc_quic_build_connection_close`, `quic_conn.c`
  `pc_quic_conn_close_app`). Found 2026-08-09 implementing the HTTP/3 state-machine rules from
  `git_project/audit/quic-http3-qpack.md`.
- **Symptom:** the close path could only build a transport CONNECTION_CLOSE (0x1c). RFC 9114 sec 8
  carries HTTP/3 errors in the application variant (0x1d), whose code comes from the application's
  space. Sent as 0x1c, `H3_FRAME_UNEXPECTED` (0x0105) lands in the transport space of RFC 9000
  sec 20.1, where 0x0100-0x01ff is CRYPTO_ERROR plus a TLS alert - so the peer would read a frame
  sequencing error as "crypto failure, TLS alert 5". The frame parser already accepted both types;
  only the builder and the queue were transport-only.
- **Fix:** the builder takes the variant and omits the Frame Type field for 0x1d, which sec 19.19
  says that variant does not carry. `queue_close` records which was chosen. `pc_quic_conn_close_app`
  is the entry point for an application error; per sec 10.2.3 the 0x1d frame is only legal in
  0-RTT/1-RTT, so before those keys exist it falls back to a transport close with APPLICATION_ERROR,
  and there is a test for that arm. Verified: `native_quic_frame`, `native_quic_conn`,
  `native_h3_conn` and three more, 105/105.

## The HTTP/3 engine enforced none of its stream or frame-sequence rules

- **Status:** FIXED 2026-08-09 (`h3_conn.c`). Found 2026-08-08 auditing `test/` for RFC conformance
  (`git_project/audit/quic-http3-qpack.md` #1, #2, #3, #8, #9).
- **Symptom:** five RFC 9114 MUSTs, none implemented. SETTINGS on a request stream was skipped as
  if it were an unknown frame type (sec 7.2.4). A control stream whose first frame was not SETTINGS
  was consumed and ignored (sec 6.2.1). A second SETTINGS re-defaulted and re-parsed, so a peer
  could reconfigure the connection at any time (sec 7.2.4). Every uni stream typed 0x00 became a
  control stream, with no check that one already existed (sec 6.2.1). A DATA frame arriving before
  any HEADERS was accumulated into the body (sec 4.1).
- **Nothing can catch it:** two of the tests asserted the permissive behaviour outright - one
  called a SETTINGS frame on a request stream "legal on the wire but meaningless here" and asserted
  the request dispatched anyway; the other asserted a first-frame GOAWAY was "consumed and ignored".
- **Fix:** each rule is now a connection error carrying its sec 8.1 code, sent through the 0x1d
  close above. The request-stream check covers the other control-stream-only frames (GOAWAY,
  MAX_PUSH_ID, CANCEL_PUSH) at the same time. Verified: `native_h3_conn` and five more, 105/105.

## QUIC accepted packets with a clear Fixed Bit, non-zero Reserved Bits, or an oversize max_streams

- **Status:** FIXED 2026-08-09 (`quic_packet.c`, `quic_conn.c`, `quic_tp.c`). Found 2026-08-08
  auditing `test/` for RFC conformance (`git_project/audit/quic-http3-qpack.md` #4, #5, #6, #7).
- **Symptom:** `pc_quic_parse_long_header` tested only the header-form bit and
  `pc_quic_parse_short_header` only its absence, so a packet with the Fixed Bit (0x40) clear parsed
  normally where RFC 9000 sec 17.2 / 17.3.1 says it "MUST be discarded". The Reserved Bits were
  never read, though sec 17.2 makes a non-zero value a PROTOCOL_VIOLATION. `initial_max_streams_bidi`
  and `_uni` took any 62-bit varint, where sec 4.6 caps them at 2^60.
- **Fix:** the long-header test exempts version 0, which is the Version Negotiation packet the RFC
  excludes; a short header is never one, so it holds there without exception. The Reserved Bits are
  read in `quic_conn` after the AEAD open rather than inside `pc_quic_packet_unprotect`: the header
  is authenticated by then, so a bit flipped in flight cannot close a connection, and the connection
  is also the only layer that can send the error. Verified: `native_quic_packet`, `native_quic_tp`
  and six more.

## The HTTP/2 engine accepted frames RFC 9113 requires it to reject

- **Status:** FIXED 2026-08-09 (`h2_conn.c` `dispatch_frame`, `handle_continuation`). Found
  2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/http2-hpack.md`, the
  lower-severity remainder) and by an earlier spec audit of `h2_conn.c`.
- **Symptom:** the dispatcher branched on frame type and went straight to the handler. It never
  checked the per-type length or stream id, so all of the following were accepted: RST_STREAM on
  stream 0 and at any length, PRIORITY at any length and on stream 0, GOAWAY shorter than its two
  mandatory fields and on a non-zero stream, SETTINGS and PING on a non-zero stream, and
  RST_STREAM or WINDOW_UPDATE naming a stream no HEADERS had ever opened.
- **Root cause:** `pc_h2_parse_header` validates the framing, and each handler validated what it
  needed to do its own job. Nothing owned the rules that are a property of the frame type itself.
- **Nothing can catch it:** `test_h2_unknown_stream_frames` asserted three of these were tolerated,
  so the suite pinned the violation.
- **Fix:** each case now carries its sec 6.x length and stream-id test. The idle-stream rule
  (sec 5.1) is expressed against `last_peer_stream`, which separates a stream that was never opened
  from one opened and since closed - the latter may still receive a late WINDOW_UPDATE (sec 6.9)
  and ignore it, and there is a test for each. A wrong PRIORITY length is a stream error, so the
  connection survives it; the rest are connection errors.
  Verified: `native_h2conn` 56/56.

## An empty HTTP/2 CONTINUATION frame could be sent without end

- **Status:** FIXED 2026-08-09 (`h2_conn.c` `handle_continuation`, `PC_H2_MAX_CONTINUATION`).
- **Symptom:** a header block spanning CONTINUATION frames was bounded only by `PC_H2_HDR_BLOCK`,
  the bytes it may accumulate. A CONTINUATION carrying no payload adds no bytes, so a peer could
  send them without limit and the block would never have to end, at no memory cost to the attacker.
- **Fix:** `PC_H2_MAX_CONTINUATION` bounds the frame count as well as the byte total. The two
  bounds answer different questions and the byte one could never have answered this.
  Verified: `native_h2conn` 56/56.

## Eight CoAPS server tests were failing on a double-address argument

- **Status:** FIXED 2026-08-09 (`test_coaps_server.c` `client_get_temp`, `assert_coap_205`). Found
  2026-08-09 sweeping the envs that build the shared HTTP sources.
- **Symptom:** `native_coaps_server` ERRORED, 8 of 21 cases red, every one on
  `TEST_ASSERT_TRUE(pc_coaps_server_ingest(...))` returning FALSE.
- **Root cause:** not the ingest. `client_get_temp(DtlsRecordKeys *w, ...)` called
  `DtlsRecord.protect(&w, ...)` - the address of its own pointer parameter, a `DtlsRecordKeys **`,
  where `protect` takes `DtlsRecordKeys *`. The sealer read a stack slot holding a pointer as key
  material, produced nothing, and returned 0. `ring_push` refuses a zero-length datagram, so the
  failure surfaced one call later at the ingest. `assert_coap_205` had the same defect on
  `unprotect`. The two sibling call sites at `:387` and `:410` are correct - they take the address
  of real `DtlsRecordKeys` values, which is what made the wrong pair look idiomatic.
- **Nothing can catch it:** the incompatible pointer type is a warning, not an error, and the suite
  is not built with warnings as errors.
- **Fix:** pass the pointer through. Verified: `native_coaps_server` 21/21, `native_coaps` 6/6.

## The HTTP/2 bridge ran no RFC 9113 sec 8.2 / 8.3 header validation at all

- **Status:** FIXED 2026-08-09 (`h2_server.c` `cb_header` / `cb_headers_end`). Found 2026-08-08
  auditing `test/` for RFC conformance (`git_project/audit/http2-hpack.md` #3, #4, #5).
- **Symptom:** any header block the HPACK decoder could parse was mapped into the slot's `HttpReq`
  and marked `PARSE_COMPLETE`. A request with no `:method`, two `:path` fields, a pseudo-header
  after a regular one, an undefined pseudo-header, an uppercase field name, a colon inside a
  non-pseudo name, a value wrapped in whitespace, or a `Connection` / `Transfer-Encoding` /
  `Upgrade` field all reached the route dispatcher. The audit names this the request-smuggling
  surface: a front end and this server would disagree about where one request ends.
- **Root cause:** the callback was written as a field-extraction chain (`:method` -> `:path` ->
  `:authority` -> other), never as a validator. Nothing in the module held per-block state, so no
  duplicate or ordering rule could have been expressed even if one had been wanted.
- **Nothing can catch it:** no test env compiled `h2_server.c`. The module had zero coverage.
- **Fix:** one bit per pseudo-header in the module's owned context, plus a "regular field seen" bit
  and a "condemned" bit. A duplicate and an out-of-order pseudo-header are one AND against the mask;
  the sec 8.3.1 mandatory set is one XOR at the end of the block. Field-name bytes go through a
  256-bit table indexed with `>> 5` and `& 31`. `on_headers_end` now returns the verdict, and the
  engine - which owns the frame borrow - answers RST_STREAM(PROTOCOL_ERROR) from the dispatcher's
  span. `native_h2server` is the env that was missing; a mutation run with the verdict disabled put
  13 of its 15 cases red, so the suite observes the validation and not the transport.
  Verified: `native_h2server` 15/15, `native_h2conn` 36/36.

## An HTTP/2 content-length was never measured against the body

- **Status:** FIXED 2026-08-09 (`h2_conn.c` `content_length_holds`, `note_content_length`). Found
  2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/http2-hpack.md`, remainder).
- **Symptom:** a request could declare `content-length: 10`, send five octets, and have them
  delivered as a complete body; or declare 2 and send 5; or declare a body and end the stream with
  its headers. RFC 9113 sec 8.1.1 makes each of those malformed. Two disagreeing accounts of one
  body's length is the primitive every request-smuggling attack is built from.
- **Root cause:** `h2_server` parsed content-length into `HttpReq` for the application's benefit
  and nothing compared it to anything. `h2_conn`, which owns both the stream state and the DATA
  accounting, never saw the header as meaning anything.
- **Fix:** the declaration is recorded on its stream as the block decodes, the DATA is counted, and
  the two are settled before any payload reaches the application - the moment the body goes over
  the declared length, or at END_STREAM if it comes up short. A value that is not a plain decimal
  number, or appears twice, is malformed on its own. Each is a stream error, so the connection
  survives. Verified: `native_h2conn` 56/56.

## HTTP/2 trailers were a connection error

- **Status:** FIXED 2026-08-09 (`h2_conn.c` `handle_headers`, `decode_block`). Found 2026-08-08
  auditing `test/` for RFC conformance (`git_project/audit/http2-hpack.md` #14).
- **Symptom:** `handle_headers` rejected every HEADERS whose stream id did not exceed
  `last_peer_stream`, so a trailer section - which RFC 9113 sec 8.1 permits as a second HEADERS on
  an open stream - killed the connection.
- **Root cause:** sec 5.1.1's monotonicity rule governs a _newly established_ stream. The check
  conflated that with any repeat of an id, and `test_h2_stream_id_must_increase` pinned the
  conflation without separating the two cases.
- **Fix:** an id at or below `last_peer_stream` is now looked up. No stream, or one past OPEN, is
  still the sec 5.1.1 connection error; a stream still OPEN makes the block a trailer section. A
  trailer that does not carry END_STREAM, or that contains a pseudo-header (sec 8.1), draws
  RST_STREAM(PROTOCOL_ERROR). Trailers are decoded so the HPACK dynamic table stays in step with
  the peer's encoder, but never delivered: the request they trail has already been dispatched, and
  delivering them would have re-run `cb_headers_end` and dispatched it twice.
  Verified: `native_h2conn` 36/36.

## HTTP/2 DATA on a stream nobody opened was delivered to the application

- **Status:** FIXED 2026-08-09 (`h2_conn.c` `handle_data`). Found 2026-08-08 auditing `test/` for RFC
  conformance (`git_project/audit/http2-hpack.md` #2).
- **Symptom:** `handle_data` called `c->cb.on_data(...)` and only then looked the stream up, and it
  never read `H2Stream.state` at all. A DATA frame naming a stream no HEADERS had ever opened had its
  payload handed to the application as a legitimate request body. RFC 9113 sec 5.1 makes any frame
  other than HEADERS or PRIORITY on an idle stream a connection error of type PROTOCOL_ERROR; sec 6.1
  makes DATA on a stream that is not open a stream error of type STREAM_CLOSED.
- **Root cause:** the lookup existed only to update `state` on END_STREAM, so it was placed after the
  delivery it should have gated. The audit names this the request-smuggling surface.
- **Nothing can catch it:** `test_h2_data_empty_and_unknown_stream` fed DATA on never-opened stream 5
  and asserted both that the call succeeded and that the body reached the app.
- **Fix:** the stream is resolved before the callback. No stream at all is a connection error; a
  stream past OPEN is answered RST_STREAM(STREAM_CLOSED) and the connection survives. The test now
  asserts the payload does not arrive, and `test_h2_data_after_end_stream_resets_the_stream` covers
  the sec 6.1 half. Verified: `native_h2conn`, 32/32.

## An HTTP/2 WINDOW_UPDATE of zero was accepted, and the window add could overflow

- **Status:** FIXED 2026-08-09 (`h2_conn.c` `dispatch_frame`). Found 2026-08-08 auditing `test/` for
  RFC conformance (`git_project/audit/http2-hpack.md` #6, #7).
- **Symptom:** the increment was added with no checks. RFC 9113 sec 6.9 makes a zero increment an
  error, and sec 6.9.1 caps a flow-control window at 2^31-1 with anything beyond it a
  FLOW_CONTROL_ERROR. `c->conn_send_window += (int32_t)inc` with `inc` up to 2^31-1 is signed
  overflow, which is undefined behaviour, not a large window.
- **Nothing can catch it:** the only WINDOW_UPDATE test sent +100 on each of the connection and a
  stream, so neither the zero nor the boundary was ever driven.
- **Fix:** both are rejected, and the RFC's split is honoured - a connection error on the connection
  window, RST_STREAM on a stream's (PROTOCOL_ERROR for zero, FLOW_CONTROL_ERROR for the cap). The cap
  is tested by subtracting from the ceiling rather than adding to the window, so the check itself
  cannot overflow. Test `test_h2_window_update_zero_and_overflow`.
  Verified: `native_h2conn`, 32/32.

## An oversize HPACK dynamic-table size update was clamped instead of refused

- **Status:** FIXED 2026-08-09 (`hpack.c` `pc_hpack_decode`, the 0x20 arm). Found 2026-08-08 auditing
  `test/` for RFC conformance (`git_project/audit/http2-hpack.md` #1).
- **Symptom:** a size update naming any value was accepted; `dyn_set_max` silently lowered it to the
  table's storage. RFC 7541 sec 6.3 says a value above the limit the enclosing protocol determined
  MUST be a decoding error, and RFC 9113 sec 4.3 makes a decoding error in a field block a connection
  error of type COMPRESSION_ERROR. So a peer's illegal update was taken as a legal one.
- **Root cause:** the clamp reads as defensive, and it does keep the table inside its storage, but it
  is answering a different question than the RFC asks. The limit here is 4096: `h2_conn.c` advertises
  no SETTINGS_HEADER_TABLE_SIZE, so the RFC 9113 sec 6.5.2 default applies and it equals
  `PC_HPACK_TABLE_BYTES`.
- **Nothing can catch it:** `test_dyn_size_update` encoded an update to 100000 and asserted the decode
  returned TRUE, pinning the clamp as correct.
- **Fix:** the decode refuses `nm > HPACK_BYTES` before applying it. The test now pins the boundary -
  4096 applied, 4097 and 100000 refused. Verified: `native_hpack` + 6 more, 96/96.

## HPACK prefix-integer decode wraps at `m == 28`

- **Status:** FIXED 2026-08-09 (`hpack_prim.c` `pc_hpack_decode_int`). Found 2026-08-08 auditing
  `test/` for RFC conformance (`git_project/audit/http2-hpack.md` #8).
- **Symptom:** `hpack_prim.c:137` bounds the continuation with `m > 28`, so `m == 28` is admitted and
  `(b & 0x7f) << 28` shifts up to 127 into a `uint32_t`. Confirmed by compiling the function verbatim:
  input `{0x1f,0x80,0x80,0x80,0x80,0x7f}` returns TRUE with `consumed = 6` and `value = 4026531871`
  (`0xf000001f`); the encoded value is 34091302943.
- **Root cause:** the comment reads "bound the continuation to a 32-bit result" and the bound is one
  step too loose - at `m == 28` only the low 4 bits of the septet still fit. RFC 7541 sec 5.1 requires
  that an integer exceeding implementation limits "in value or octet length" be a decoding error.
- **Nothing can catch it:** `test_hpack.c:439-440` pins only the `m > 28` rejection.
- **Fix:** the septet is rejected when `m == 28 && (b & 0x7f) > 0x0f`, and the running sum is checked
  against `0xFFFFFFFF - v` before it is added, so neither the shift nor the add can wrap. Test
  `test_int_decode_rejects_overflowing_prefix_int` pins the audit's own vector plus the boundary
  either side (`0x0f` accepted, `0x10` refused) and the octet-length bound.
  Verified: `native_hpack`, `native_h2conn`, `native_qpack` and four more, 96/96 - QPACK shares this
  primitive, so the fix is on the HTTP/3 path too.

## `pc_base64url_decode` stops at `=` and reports the partial decode as success

- **Status:** OPEN, found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/websocket-and-codecs.md` #2).
- **Symptom:** `base64.c:330-333` breaks out of the loop on `'='` and returns the bytes accumulated so
  far as the decoded length. `pc_base64url_decode("Zm9v=", 5, ...)` returns 3. Everything after the
  first `=` is discarded without a diagnostic, so on the JWT/JWS path `<segment>=<anything>` decodes
  identically to `<segment>`.
- **Root cause:** the branch is commented "optional padding ends the input (public length information)",
  which is the base64 rule, not the base64url one. RFC 4648 sec 3.2/sec 5 and RFC 7515 sec 2 give
  base64url no padding at all, and sec 3.3 requires rejecting any character outside the alphabet. The
  surrounding decoder is carefully branchless for constant time; this one early `break` is the hole in
  it, and it is also the only place the function can return a short count as success.
- **Nothing can catch it:** `test_base64.c:141-147` asserts the 3 and pins the behavior as correct.
- **Fix:** not written. Fold `'='` into the `bad` accumulator like every other non-alphabet byte and
  drop the early break; correct the test to assert rejection.

## SSE field values are copied verbatim, so a newline in application data injects an event

- **Status:** OPEN, found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/websocket-and-codecs.md` #3).
- **Symptom:** `sse.c:110-142` `pc_sse_format` memcpy's `data`, `event` and `id` into the record with no
  escaping. A value containing `\n\n` terminates the record early and the remainder is parsed by the
  client as new fields; a single `\n` silently truncates the field.
- **Root cause:** the WHATWG event-stream format is line-oriented - CR, LF or CRLF separate lines and a
  blank line dispatches the event - so the field separator is in-band and application data has to be
  escaped or refused. The formatter treats the values as opaque bytes.
- **Nothing can catch it:** `test_sse.c:326-357` pins exact bytes only for newline-free values.
- **Fix:** not written. Refuse a value containing CR or LF, or re-emit each embedded newline as a fresh
  `data:` continuation line, which is what the format is for.

## An MQTT redelivery resends whatever the codec framed last

- **Status:** OPEN, found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/coap-mqtt.md` #3).
- **Symptom:** `mqtt.c:1141` sets `s_mqtt.tx[0] |= 0x08` and calls `mq_tx(s_mqtt.inflight[i].len)`. `tx`
  is one shared framing buffer that every later `pc_mqtt_build_ack` / `pingreq` / `subscribe` / second
  `PUBLISH` overwrites (`:776, :790, :814, :822, :1067, :1077, :1116`), so a retransmit sends the
  current contents of that buffer, with bit 3 set, for the _old_ PUBLISH's length. A PINGREQ `0xC0`
  goes out as `0xC8`.
- **Root cause:** the comment at `:1139-1140` states the invariant it relies on - "One packet is in
  flight at a time, so tx is that packet" - and `PC_MQTT_MAX_INFLIGHT` is 4, so the invariant is false.
  The retransmit needs the packet, and what it has is a buffer.
- **Nothing can catch it:** `native_mqtt` is a host build with `PC_HAS_NET_STACK` 0, so the whole
  transport - QoS 1/2 flows, dedup, keepalive, `MqLinkState` - is compiled by no test env.
- **Fix:** not written. The inflight record owns its bytes, or the DUP re-frame rebuilds the PUBLISH
  from the retained topic/payload. Wants a host env that compiles the transport first, or the fix is
  unverifiable in the same way the bug was.

## JWT claim validation fails open when there is no wall clock

- **Status:** OPEN, found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/http-cache-and-auth.md` #1).
- **Symptom:** `jwt.c:146-148` returns `PROTO_TRUE` from `pc_jwt_time_valid` whenever `now_epoch <= 0`,
  so on a device that has not yet synced time every `exp` and `nbf` is accepted. A token that expired in
  1970 validates.
- **Root cause:** the comment reads "no wall clock -> time claims cannot be evaluated (the signature is
  the gate)", which chooses availability over the RFC 7519 sec 4.1.4 MUST NOT. An unevaluatable claim is
  not a satisfied claim.
- **Nothing can catch it:** `test_jwt.c:286-287` asserts the fail-open as correct behavior for both
  `pc_jwt_time_valid` and `pc_jwt_verify_hs256_at`.
- **Fix:** not written. Fail closed when a token carries `exp`/`nbf` and no clock is available, or make
  the caller pass an explicit "time claims not required" flag so the decision is at the call site.

## The Digest response tag is compared with a plain byte compare

- **Status:** OPEN, found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/http-cache-and-auth.md`, non-RFC note).
- **Symptom:** `auth.c:519` is `if (!str.eq(expected, response, sizeof(expected), PROTO_TRUE))` on the
  computed versus client-supplied Digest response - an early-exit byte compare on an authentication tag,
  which is a timing oracle. `check_basic` at `auth.c:396-397` does use `pc_ct_eq`, so the two auth paths
  in one file disagree.
- **Root cause:** `protostr.h:113` states the rule in the library's own words - "A secret comparison uses
  `pc_ct_eq`" - and `str.eq` is the general-purpose comparison that stops at the first difference.
- **Fix:** not written. `pc_ct_eq` over the full 64-hex-character width, matching the Basic path.

## `serve_file_internal()` accepts a backend and never uses it

- **Status:** OPEN, found 2026-08-08 auditing `test/mocks` for harness holes (`git_project/audit/mock-mnt-and-support-fixtures.md` #3).
- **Symptom:** `file_serving.c:227` takes `const pc_mnt_backend *file_sys` and the body never reads it -
  `file_sys` occurs in the file only at `:227` (the parameter), `:525`/`:527` (passed through from
  `serve_file`) and `:530`/`:560` (recorded by `serve_static`). Line 230 goes straight to
  `pc_fs_open(file_root(), ...)`, the globally mounted store. An application following the documented
  `serve_static("/ram/", pc_mnt_ram(), "/")` idiom serves from whatever is mounted, not from the backend
  it named.
- **Root cause:** the multipoint mount table records the per-prefix backend (`mnt.c:47-57`) and
  `file_serving.c:637`/`:642` and `webdav_handler.c:433` read it back and pass it down, so the plumbing
  is complete up to the last function, which drops it. There is not even a `(void)file_sys;`, and the
  build is `-std=gnu11` without `-Wextra`, so the unused parameter is silent.
- **Nothing can catch it:** `test_file_serving.c:24` declares `static const pc_mnt_backend *g_fs;` and
  never assigns it, so all 18 `serve_static()` calls pass NULL. The `setUp` comment at `:69-70` records
  the symptom ("resolves through the accessor, not through the backend handed to `serve_file()`")
  without treating it as a defect.
- **Fix:** not written. Resolve through `file_sys` when it is non-NULL and fall back to the accessor
  otherwise, and give the test a second backend so the two are distinguishable.

## The plaintext SSH receive path accepts sub-minimum padding

- **Status:** FIXED 2026-08-09 (`ssh_packet.c` `ssh_recv_plain`). Found 2026-08-08 auditing `test/` for
  RFC conformance (`git_project/audit/ssh-transport-kex.md` #7).
- **Symptom:** `ssh_packet.c:747` checks only `pad_len_byte >= pkt_len`. All four encrypted receive
  paths check `< 4` (`:438`, `:514`, `:592`, `:700`); the pre-NEWKEYS path does not, so a packet
  declaring 0 to 3 bytes of padding is accepted there.
- **Root cause:** RFC 4253 sec 6 - "There MUST be at least four bytes of padding" - is enforced in the
  four places that were written together and missed in the fifth. `docs/SSH.md:73` claims the rule holds.
- **Nothing can catch it:** `test_ssh_server.c:582` covers only the over-large case (`6 >= 6`).
- **Fix:** the plaintext path carries the same `< 4` arm as the other four.
  Verified: `native_ssh` + 16 more envs.

## The AES-GCM vector corpus ships forty valid vectors and zero negatives

- **Status:** OPEN, found 2026-08-08 auditing `test/vectors` (`git_project/audit/crypto-primitives-and-vectors.md` #1).
- **Symptom:** `test/vectors/wycheproof_aes_128_gcm.json` holds 40 vectors, all `result:"valid"`. The
  runner executes all 40, so nothing is truncated downstream - the corpus itself contains no tampered
  tag, no flipped AAD byte, no truncated ciphertext. An AEAD is tested only on success.
- **Root cause:** `tools/crypto/curate_crypto_vectors.py:63-73` files a test under `flagged` when
  `result=="invalid"` **or** `t.get("flags")` is non-empty, then takes `flagged[:CAP_FLAGGED]` with the
  cap at 40 (`:37`). Every valid AES-GCM vector carries `Pseudorandom`/`Ktv`/`SpecialCase` (35/3/2 = 40
  exactly), so the cap fills with valid-but-flagged entries before the first `invalid` one is reached.
  The HMAC corpora survive the same code only by accident of ordering.
- **Fix:** not written. Select `invalid` first and fill the remainder with flagged-valid, or cap the two
  classes separately. Re-curating changes the committed corpus, so it wants its own commit.

## `pc_ct_eq` has no test of any kind

- **Status:** OPEN, found 2026-08-08 auditing `test/` for crypto coverage (`git_project/audit/crypto-primitives-and-vectors.md` #3).
- **Symptom:** `src/crypto/ct_eq.h:33` is the library's single comparator for, in its own doc comment
  (`:8-10`), "every MAC, tag, digest and signature check". Grep across `test/` returns no reference to
  the symbol - not a functional equality test, let alone a timing one.
- **Root cause:** it is a leaf primitive that every caller exercises indirectly, so no suite claimed it.
  An indirect exercise proves the callers agree with it, not that it is correct.
- **Fix:** not written. A functional suite (equal, differing at first byte, differing at last byte,
  zero length) is cheap; the timing claim wants the off-host CCOUNT harness `test_base64.c:6-7` already
  uses.

## DSCP is written as a whole byte, clobbering the ECN bits

- **Status:** OPEN, found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/fieldbus-and-files-naming.md` #1).
- **Symptom:** every DSCP application writes the entire IPv4 TOS / IPv6 Traffic-Class octet -
  `tcp_conn.c:385`, `tcp_listener.c:506`, `tcp_client.c:175`, `udp/udp_client.c:48`,
  `udp/udp_listener.c:176` - so bits 6-7 are zeroed to Not-ECT on every connection that sets a DSCP.
- **Root cause:** RFC 2474 defines the high 6 bits and RFC 3168 sec 5 gives the low 2 to ECN as a
  separate field with a separate owner. The setter treats the octet as one value.
- **Nothing can catch it:** `test_diffserv.c:57-68` always starts from `pcb.tos = 0`, and `:35` pins
  `pc_dscp_to_tos(63) == 0xFC` with the comment "ECN still 0" - so no test ever presents a pcb already
  carrying ECT(0), ECT(1) or CE.
- **Fix:** not written. Read-modify-write the octet, preserving the low two bits, at all five sites; add
  a test that seeds a non-zero ECN and asserts it survives.

## The DNS name decoder follows forward compression pointers

- **Status:** OPEN, found 2026-08-08 auditing `test/` for RFC conformance (`git_project/audit/fieldbus-and-files-naming.md` #7, #14).
- **Symptom:** `dns_wire.c:53` accepts any pointer offset within the message, including one greater than
  the current position, so forward and mutually-referential chains are followed - bounded only by the
  8-hop `PC_DNS_PTR_HOPS` counter. Separately, `dns_wire.c:82` bounds the assembled name only by the
  caller's `out_cap`, so with a large buffer a chain of 63-octet labels yields a name past the RFC 1035
  sec 2.3.4 limit of 255 octets.
- **Root cause:** RFC 1035 sec 4.1.4 defines a pointer as referring to "a prior occurrence of the same
  name". The hop counter makes the loop terminate, which is the DoS half; the direction rule and the
  total-length rule are the correctness half and neither is enforced.
- **Nothing can catch it:** `test_dns_wire.c:102-107` covers the self-pointer `{0xC0,0x00}` only - not a
  forward pointer, not a pointer past `len`, not an over-long assembled name.
- **Fix:** not written. Refuse an offset `>= cur`, and cap the assembled length at 255 independently of
  `out_cap`.

## The Modbus master never reads the MBAP transaction id or length

- **Status:** OPEN, found 2026-08-08 auditing `test/` for standards conformance (`git_project/audit/fieldbus-and-files-naming.md` #2, #8).
- **Symptom:** none of the four response parsers - `modbus_master.c:46`, `:127`, `:323`, `:443` -
  accepts a transaction id or reads `adu[0..1]`, so a response cannot be matched to its request and a
  stale or injected response is accepted for whatever is outstanding. The same four check only `len < 9`
  and the protocol id, never `adu[4..5]` (Length) against the delivered byte count.
- **Root cause:** the Modbus Messaging Guide V1.0b sec 4.4.1.3 makes both checks mandatory - "If the
  Transaction Identifier doesn't refer to any MODBUS pending transaction, the response must be
  discarded" - and the parser API has no parameter through which the caller could supply the expected
  id. The MUST cannot be satisfied without a signature change.
- **Nothing can catch it:** no test feeds a mismatched transaction id, and the fixtures themselves carry
  inconsistent Length fields (`test_modbus_master.c:136`, `:376`) with no assertion on them.
- **Fix:** not written. Thread the outstanding transaction id into the parsers and discard on mismatch;
  validate Length against the frame. Correct the two fixtures in the same commit.

## The bounded-run walks existed twice, on two owners

- **Status:** FIXED (2026-08-08), found auditing ARCHITECTURE.md against the tree.
- **Symptom:** none at runtime. `shared_primitives/runops.h` and `mmgr/protostr.c` each carried a
  full implementation of the same operations on top of `mmgr/swar.h` - the NUL scan, the first-
  difference index, the agreement walk behind eq/starts, the search behind find/has, and the bounded
  copy. Neither was built on the other. 44 call sites across 10 files took the header, and the rest
  of the tree took `str`.
- **Root cause:** the walks moved into `mmgr/` with the byte layer and the header they came from was
  left in place, so the two drifted independently. SRCBANNED #13 bans exactly this ("an alias, a
  wrapper, a second spelling"), and the copies had already diverged in the small: `runops.h` used
  `?:` (banned by #23, carried in `sweep_baseline.json` as 20 whitelisted ternaries) where
  `protostr.c` spells the same dispatch as `if`/`else`.
- **Fix:** every call site rewritten onto `str` (`proto_scan_nul` -> `str.len`, `proto_diff*` ->
  `str.diff(..., ci)`, `proto_eq_str*` -> `str.eq(..., ci)`, `proto_starts*` -> `str.starts`,
  `proto_find*` / `proto_has*` -> `str.find` / `str.has`, `proto_copy` -> `str.copy`), and
  `runops.h` deleted. `native_dns_resolver`, `native_mdns_service`, `native_client` and
  `native_h3_server` gained `+<mmgr/protostr.c>`: the header was `static inline` and the module is
  not, so those four linked nothing before and needed the TU.
- **Note:** `str` is `extern const StrNs`, not the folding `static const` table `mem` and `swar` use,
  so a call through it is an indirect call the compiler cannot fold. On the hot paths that took the
  header inline - the HTTP method compare and the route match in `http.c` - that is a real change,
  measurable on the S3 bench (`test/penetration_testing/rig_firmware/src/main_swarbench.cpp`).

---

## The SSH client reads the peer's maximum packet size and throws it away

- **Status:** OPEN, found 2026-08-08 reconciling SWEEP_NOTES.md against the tree.
- **Symptom:** `ssh_client.c:1155` is `r_u32(&r); // their max packet` - the CHANNEL_OPEN_CONFIRMATION
  field is parsed and discarded. The send pump then clamps to its own `SSH_CLI_MAXPKT` (16384,
  `:1145`) at `:1312`. Against a peer advertising a smaller maximum we emit an over-long
  CHANNEL_DATA; a strict peer drops the channel.
- **Root cause:** RFC 4254 sec 5.1 makes `maximum packet size` half of the same per-channel state as
  the window, and the client keeps a private copy of that state instead of the one owner:
  `ssh_client.c:302-303` declares `send_win` / `recv_win` and does its own arithmetic at
  `:1273-1334` and `:1477`, duplicating `SshFlow` (`ssh_flow_control.h`), which the server side has
  used since the signaling move. A second implementation of an invariant is a place the invariant
  can be dropped, and this is the field that was dropped.
- **Nothing can catch it:** `ssh_client.c` is compiled by **no** test env - all twelve `native_ssh*`
  entries in `test/test_matrix.json` list other sources, none lists `ssh_client.c`. ~1700 lines of
  client KEX, host-key verification, user auth and channels with zero coverage.
- **Fix:** not written. `CliChannel` folds onto `SshFlow`, which stores `peer_max_pkt` at open and
  gates the send, so the cap is the peer's and the check cannot be forgotten. Wants a
  `native_ssh_client` env first, or the fix is unverifiable in the same way the bug was.

## SSH packet state and key material sit in plain BSS, outside the secure pool

- **Status:** OPEN, found 2026-08-08 reconciling SWEEP_NOTES.md against the tree.
- **Symptom:** `ssh_packet.c:29` `SshPacketState ssh_pkt[MAX_SSH_CONNS];` and
  `ssh_keymat.c` `SshKeyMat ssh_keys[MAX_SSH_CONNS];` are file-scope arrays with external linkage.
  They hold decrypted packet bytes, key schedules and HMAC keys, and they are cleared by nothing on
  release.
- **Root cause:** their separation is incidental linker placement, which `ssh_keymat.h:85` already
  says out loud ("physical separation only raises the bar, not a hard wall"). The secure pool exists
  for exactly this content and wipes on release; as BSS these arrays are outside both it and the
  plaintext arena, so they are covered by neither derived worst case.
- **Fix:** not written. Both borrow from the secure pool with a declared `PC_WORK_SSH_*` term proved
  by a `static_assert` at the borrow site, the shape `crypto/aead/aes128gcm.h` already uses.

## The If-Modified-Since date is parsed with `sscanf`, under a comment that says "no stdlib"

- **Status:** OPEN, found 2026-08-08 auditing stdio use in `src/`.
- **Symptom:** `file_serving.c:132` calls `sscanf(ims, "%*3s, %d %3s %d %d:%d:%d", ...)`. The comment
  four lines above it reads "Parses the date by hand (sscanf, no stdlib)". `sscanf` **is** stdlib -
  the sentence contradicts the line it describes, and a reader auditing for stdlib use believes it.
- **Root cause:** the file also calls `strftime` and `strstr`, so `<stdio.h>` and `<string.h>` are
  both live here. SRCBANNED ban 25 bans both headers in `src/`; the sites are in the ratcheted
  baseline, so the gate stays green and the comment is what a reader actually reads.
- **Cost:** `sscanf` pulls the full scanf format engine into the link, on a path that parses one
  fixed RFC 1123 shape - six integers and a three-character month at known positions.
- **Fix:** not written. Hand-parse the fixed layout with the existing decimal readers, delete both
  includes, and drop the two baseline entries in the same commit.

## Nineteen translation units carry a stale `#include <stdio.h>`

- **Status:** OPEN, found 2026-08-08 auditing stdio use in `src/`.
- **Symptom:** 27 files under `src/` include `<stdio.h>` (SRCBANNED ban 25, 31 ratcheted baseline
  entries whose text reads "nothing in src/ formats"). **19 of them reference no stdio symbol at
  all** - among them `server/middleware.c:17`, `server/websocket_sse.c:25`, `server/logbuf.c`,
  `server/power_mgmt.c`, `codec/json/json.c`, `ssh/transport/ssh_transport.c`. Eight more mention a
  stdio name only inside a comment.
- **Root cause:** the include outlived the `snprintf` calls that were replaced by `pc_sb`; nothing
  removes an include that nothing needs, and the baseline records the site rather than expiring it.
- **Fix:** not written. Delete the 19 dead includes and their baseline entries. The three live users
  are separate: `file_serving.c` (entry above), and the `WSC_DBG` / `CL_DBG` debug `printf` macros in
  `ws_client.c:276` and `http_client.c:354`.

## Forty-six comments in `src/` name a `.cpp` file that no longer exists

- **Status:** OPEN, found 2026-08-08 reconciling SWEEP_NOTES.md against the tree.
- **Symptom:** `src/` is C: 349 `.c`, 378 `.h`, zero `.cpp`. 46 comments still point at the old
  names - `bignum.h:48` "see pc_bignum.cpp", `md.h:27` "md.c", `presentation.h:14`
  "http_parser.h / http_parser.c", `ssh_flow_control.h:13` "ssh_channel.c held the counters",
  `websocket.c:459` "lowlevel_recv_cb() (tcp.c)", `aes256ctr.h:56` "the mode ssh_packet.c uses",
  and 40 more. Every path names a file that cannot be opened.
- **Root cause:** the `.cpp` -> `.c` conversion renamed files and left the prose. `check_comments.py`
  already owns the "a sentence about code that is no longer there" family but matches on phrasing,
  not on whether a path in a comment resolves.
- **Fix:** not written. Rewrite the 46 references, and add a rule to `check_comments.py` that resolves
  any `<name>.<ext>` token in a comment against the tree - mechanical, and it cannot regress after.

## `pc_frame_append` re-scans the whole accumulated document on every call

- **Status:** OPEN, found 2026-08-08 reconciling SWEEP_NOTES.md against the tree.
- **Symptom:** `frame.c:115` is `size_t used = proto_scan_nul(out, cap);` on entry. Appending n
  fragments walks the document n times, so the three emitters that build an array in a loop are
  O(n^2) in document length: `dashboard.c:130-169`, `gpio_map.c:67-80`,
  `partition_monitor.c:99-113`.
- **Root cause:** the append takes only `(out, cap)`, so the length it needs is not in the signature
  and has to be recovered by scanning. `pc_frame_vbuild` returns the length it wrote, so the caller
  already holds the number the scan re-derives.
- **Partial mitigation, not a fix:** `proto_scan_nul` is the SWAR scan and tests four bytes per
  iteration rather than one. That is a constant factor on a quadratic.
- **Fix:** not written. The append takes the current length as an in/out parameter and returns the
  new one; the scan goes. Touches the three emitters and any other `pc_frame_append` caller.

## `PC_PLAINTEXT_ARENA_SIZE` is guessed twelve times, and scaled by the wrong quantity

- **Status:** OPEN, found 2026-08-08 reconciling SWEEP_NOTES.md against the tree.
- **Symptom:** the arena size is a `#ifndef` default in `protocore_config.h:6469` (8192) and again in
  all eleven `core_setup/board_profiles/esp/*_defaults.h`, scaled by die RAM: c2/c61/h2/h21/s2 8192,
  c3/c5/c6/h4 10240, s3/s31 12288, p4 16384.
- **Root cause:** the scaling axis is wrong. The requirement is set by which features are compiled
  in, not by which chip runs them - a C2 running SSH plus websocket borrows exactly what a P4 running
  SSH plus websocket borrows. `core_setup/board_profiles/derived_sizing.h` states that principle in
  its own header for `RX_BUF_SIZE` and the plaintext pool never got it.
- **What is already in place:** the per-TU worst-case terms exist and are proved at their borrow
  sites - `PC_PLAINTEXT_WORK_WS_SEND` / `_WS_RECV` (`websocket.h:76,84`), `_SSH_TRANSPORT`
  (`ssh_packet.h:197`), `_OIDC` (`oidc.h:58`), `_DIAG` (`protocore.c:757`). Each is `static_assert`ed
  against the arena individually. The arena is not derived from them.
- **Consequence today is silent:** a borrow past the guess returns NULL and the call site falls
  through to a degraded path (websocket sends uncompressed), so no peer, log line or test shows it.
- **Fix:** not written. The arena becomes the max over the declared `PC_PLAINTEXT_WORK_*` terms -
  max, not sum, because the arena resets per dispatch - taken in `protocore_config.h`, which is the
  only file that sees them all. The eleven board-profile defaults then delete, and
  `pc_plaintext_alloc`'s NULL return becomes unreachable.

## The portable DNS resolve spins on a blocking loop that pumps a listener it does not own

- **Status:** OPEN, found 2026-08-08 removing the UDP send rings.
- **Symptom:** `dns_resolver.c:432` waits for its answer in
  `while (!s_dr.done && (int32_t)(deadline - pc_millis()) > 0) { Udp.listener->poll(); pcdelay(5); }`.
  A resolve issued from inside a UDP handler never sees its reply and always returns false after the
  full `PC_DNS_TIMEOUT_MS`.
- **Root cause:** two faults in one loop. `poll_all()` sets `s_lst.polling` for its duration and
  returns immediately on a reentrant call, so a resolve called from inside a handler polls nothing
  for every iteration of the wait. And the listener pump belongs to `session.c`, which calls it from
  worker 0; reaching into it here drains every bound port's ring and runs every other service's
  handler from inside a DNS call.
- **Also blocking, same shape:** `dns_resolver.c:303` (vendor arm, `pcdelay` only),
  `tcp_client.c:268` (connect wait), and the `tls.c` handshake waits at `:915`, `:954`, `:977`.
- **Fix:** not written. The shape it takes: every TU that needs a delay owns one module-wide timer
  and never sleeps. A call enters, the functional block sits behind the timer, and
  `if (now - tu_timer >= delay)` clears busy and runs it; otherwise the call returns busy and the
  caller comes back on its own tick. Busy is marked on the TX side only, so RX stays open and the
  reply handler always runs. `resolve()` collapses to one entry point returning READY / BUSY /
  FAILED, which is the shape `ntp_service.h` already states ("Returns immediately; the first sync
  arrives asynchronously") and the shape lwIP's `ERR_INPROGRESS` uses.
- **Blast radius:** one contract change across `dns_resolver.{h,c}` (both arms), `tcp_client.c`
  (`pc_client_open` blocks on DNS then on connect), `edge_cache_proxy.c:163/211/229`, the AdsClient
  example, and `test_dns_resolver` / `test_client`. It converts as one piece: `resolve()` moving
  from `proto_bool` to an enum silently inverts `if (!resolve(...))` at `tcp_client.c:250`.

## Deflate and Inflate bind their namespace instance below the gate that declares its type

- **Status:** FIXED, found 2026-08-08 running the envs behind the `PC_HAS_BUS` batch. Pre-existing
  on `main`: `94c236e9` without the batch gives the identical four errors.
- **Symptom:** `pio test -e native_swar` ERRORS in the compile stage. `deflate.c:297: unknown type
name 'DeflateNs'` and `'deflate_raw' undeclared here (not in a function)`, and the same pair for
  `InflateNs` / `inflate_raw` at `inflate.c:455`.
- **Root cause:** both headers declare the namespace type and its `extern` inside
  `#if PC_ENABLE_WS_DEFLATE` (`deflate.h:67-74`, `inflate.h:35-71`), and both sources close that
  same gate before defining the instance: `deflate.c:295` is `#endif // PC_ENABLE_WS_DEFLATE` and
  the definition sits at `:297`, one line past it. With the feature off the type is not in scope but
  the definition is still compiled, and `native_swar` is an env that builds the codec with
  `PC_ENABLE_WS_DEFLATE` off.
- **Fix:** the definition moves above the `#endif` in both files, so the instance is inside the gate
  that declares what it is. This is the same shape as `radio_power.c`, where six entry points bound
  outside their gate.
- **What it uncovered:** `native_swar` still fails, now at the link rather than the compile, on
  `pc_aesgcm_key_init` / `pc_aesgcm_key_wipe` / `pc_aesgcm_seal` out of `ssh_conn.c`, `ssh_dh.c` and
  `ssh_packet.c`, plus `bn_expmod_group14`. That is the entry below, and it is why the compile error
  had been sitting there: nothing downstream of it had ever run.

## An env with no source list builds the whole of src/ and links none of core_setup/

- **Status:** FIXED for `native_swar`, found 2026-08-08 behind the `Deflate` / `Inflate` gate fix
  above. The general question the last bullet raises is still open.
- **Symptom:** `pio test -e native_swar` fails at the link with undefined references to
  `pc_aesgcm_key_init`, `pc_aesgcm_key_wipe`, `pc_aesgcm_seal` and `bn_expmod_group14`, out of
  translation units the suite has nothing to do with. `test_swar` covers `mmgr/swar.h`, which is
  header-only lane math.
- **Root cause:** `test/test_matrix.json` gives `native_swar` `"src": []` and `"flags": []`, so
  `gen_test_envs.py` emits no `build_src_filter` and PlatformIO falls back to its default of `+<*>`:
  the env compiles every file under `src/`. With no flags, every `PC_ENABLE_*` takes its default, so
  SSH is on and `ssh_conn.c` / `ssh_dh.c` / `ssh_packet.c` compile for real. The AEAD and bignum
  backends they call live under `core_setup/hal/portable`, which sits outside `src/` and so is in no
  env's default filter. Same family as the `native_quic_server` entry below: an env whose source set
  does not close over what its sources call.
- **Scope:** 25 of the 328 envs carry `"src": []` (`native_application`, `native_auth`,
  `native_base64`, `native_swar`, `native_transport` and 20 more). Most pass, because their `flags`
  turn the heavy features off before those files compile to anything. `native_swar` has an empty
  `flags` list as well, which is what makes it the one that links the whole default feature set.
- **Fix:** `native_swar` takes `"src": ["-<*>"]` and `"test_build_src": "no"`, which is what
  `native_concurrency` already uses for the same shape. `test_swar` covers `mmgr/swar.h`, which is
  header-only `static inline`, so the suite needs no library source at all and now builds none.
  (2026-08-08: the bounded scan it also covered moved to `mmgr/protostr.c` with the runops removal
  below, and its assertions went with it, so `native_swar` still links nothing.)
- **Still open:** whether an empty `src` should keep meaning "the whole tree". That default is what
  let this env silently acquire translation units it was never meant to build, and 24 other envs
  carry it. They pass only because their `flags` switch the heavy features off before those files
  compile to anything, which is a coincidence rather than a decision.

## test_ina219 asserts on INA219_REG_BUS_VOLTAGE, a name the library never defined

- **Status:** FIXED, found 2026-08-08 the same way. Pre-existing on `main`.
- **Symptom:** `pio test -e native_ina219` ERRORS in the compile stage:
  `test_ina219.c:72: 'INA219_REG_BUS_VOLTAGE' undeclared (first use in this function); did you mean
'INA219_REG_BUS'?`. The identifier appears nowhere else in the tree.
- **Root cause:** `ina219.h:34-39` names the register pair `INA219_REG_SHUNT` / `INA219_REG_BUS`,
  matching the doc comments ("shunt voltage" / "bus voltage"). `test_begin_and_read_drive_the_bus`
  spells the second one out in full. The reference is under no `#if`, so the env cannot compile at
  all.
- **Fix:** the test takes the name the library publishes. The header's pair is self-consistent and
  is what `ina219.c` reads, so the test was the side that was wrong.

## pc_server_reset() left the not-found handler installed, so no route miss ever answered 404

- **Status:** FIXED, found 2026-08-08 running the envs behind the `PC_HAS_BUS` batch. Pre-existing on
  `main`: `94c236e9` without the batch fails identically, and the other 36 envs in that run pass.
- **Symptom:** `pio test -e native_application` reports 96 cases, 2 failed.
  `test_empty_route_pattern_matches_nothing` (`:1873`) and `test_path_param_segment_mismatches`
  (`:1930`) both fail the same assertion, `strstr(tcp_captured(), "404")` returning NULL. The
  `TEST_ASSERT_FALSE(handler_called)` immediately above each one passes, so the matcher is refusing
  the route correctly and the response is what does not arrive.
- **What is ruled out:** the capture harness works. `:885` runs the identical
  `arm_slot` / `pcb` / `tcp_capture_reset` / `handle` / `strstr(out, "404")` sequence on
  `GET /nope.txt` and passes. Cross-test route pollution is ruled out too: `setUp` calls
  `pc_server_reset()` (`protocore.c:117`), which resets the route table, the mount points, the
  response headers, the middleware and the signal table.
- **Root cause:** `http.c:728` answers a route miss with `s_http.not_found` when one is set and the
  built-in 404 only when it is not. `pc_server_reset()` empties every other owner that holds an app
  registration - `HttpRoutes.reset()`, `Auth.reset()`, `pc_mnt_point_reset()`, `pc_resp_reset()`,
  `pc_middleware_reset()`, `pc_signal_reset()` - but HTTP's own `HttpCtx` was not among them, because
  HTTP was the one owner that never exposed a reset. Two tests near the top of the suite call
  `on_not_found()`, so from that point on every route miss in the binary ran their handler instead.
  The path had nothing to do with it; `GET /nope.txt` passed only because it runs before those two.
- **Fix:** `HttpNs` gains `reset`, which puts `HttpCtx` back to the blank template the same way
  `pc_server_reset()` does for its own `ServerCtx`, and `pc_server_reset()` calls it. That also
  clears the edge-cache poll seam, which had the same escape.

## native_quic_server does not link: it builds the HTTP/3 bridge without the HTTP or TCP owners

- **Status:** FIXED, found 2026-08-07 while yanking the host arms out of the presentation layer.
  Pre-existing and not caused by that sweep: building `ac33d37a0` (its base) gives the identical
  link errors.
- **Symptom:** `pio test -e native_quic_server` ERRORS in the link stage, before any test runs.
  `native_h3_server` builds the same file and passes.
- **Root cause:** the env's `src` list carries `presentation/http/http3/h3_server.c`, whose
  `pc_h3_resp_sink` and `pc_h3_server_request` reference `conn_pool`, `http_pool`, `http_reset`,
  `Tcp`, and `Http`. It carries neither the HTTP owner nor the TCP owner that define them, so
  `ld` reports eight undefined references out of `h3_server.o`.
- **Fix:** the env drops `h3_server.c` and tests `quic_server.c` alone. The dispatch bridge is what
  `native_h3_server` is for: it extends the full-app base, so the route table and the slot pools the
  bridge reaches are already in the image. The env now also carries the UDP listener, the client, the
  address mapping and `ip.c`, which is what `quic_server.c` binds its port through.

## pc_quic_server_stop() left the UDP port bound and its handler armed

- **Status:** FIXED, found 2026-08-07 while putting test_quic_server on the real listener.
- **Symptom:** none on a device, where the server is stopped once at most. A suite that stops and
  restarts saw the second `pc_quic_server_begin()` take `listen_on`'s already-bound path, which
  rebinds the handler without a fresh pcb.
- **Root cause:** `pc_quic_server_stop()` cleared the running flag, the pool and the ring cursors,
  but never called `Udp.listener->close()`, so the bind outlived the server it belonged to and a
  datagram arriving after the stop still filled the ingest ring. `quic_server.h` has documented the
  call as closing the binding since it was written.
- **Fix:** `pc_quic_server_stop()` closes the port first, before releasing the pool, so nothing more
  reaches the ring while it is being torn down.

## Two includes in protocore.c were gated on a flag their callers do not carry

- **Status:** FIXED (2026-08-06), found auditing the include block after the dispatch-chain move.
- **Symptom:** none in any env the matrix builds today, because no env sets `PC_ENABLE_HTTP3` or a
  nonzero `PC_REQUEST_TIMEOUT_MS` or `PC_ENABLE_HTTP_DELIVERY` with `PC_ENABLE_AUTH` off. Any build
  that does fails to compile on an implicit declaration.
- **Root cause:** `server/clock/clock.h` and `http_delivery.h` sat inside `#if PC_ENABLE_AUTH`. Their
  callers are elsewhere: `pc_millis()` runs the QUIC poll under `PC_ENABLE_HTTP3` and the request
  timeout under `PC_REQUEST_TIMEOUT_MS`, and `pc_delivery_cache_control()` runs under
  `PC_ENABLE_HTTP_DELIVERY` alone. An include nested under a flag it does not belong to reads as
  correct as long as one flag implies the other in every configuration anyone has built.
- **Fix:** `clock.h` is unconditional and `http_delivery.h` takes its own `PC_ENABLE_HTTP_DELIVERY`
  guard. `sha1.h`, `base64.h` and `sha256.h` came out with them: nothing in the file names a symbol
  from any of the three.

---

## Moving a function out of a file left its `#if` behind

- **Status:** FIXED (2026-08-06), found on the first build after the dispatch chain moved to
  `presentation/http/http.c`.
- **Symptom:** `native_application` fails to compile with `'Auth' undeclared`, `'HttpRoute' has no
member named 'auth_id'`, and `'CSRF_TOKEN_BUF' undeclared`.
- **Root cause:** the extraction script took each function's body and the comment block above it,
  but not the `#if` on the line before that, and not the `#endif` after. `pc_csrf_gate`,
  `handle_ws_route`, `proto_authorize_request`, `lockout_client_ip` and `send_too_many_requests`
  arrived in the new file unguarded, while `protocore.c` kept four `#if`/`#endif` pairs with nothing
  between them. Every call site was still guarded, so the guards read as intact.
- **Blast radius:** any build with the moved feature off, which is every env except the ones that
  set the matching flag.
- **Fix:** the four guards are restored around the functions in `http.c`, the empty pairs are gone
  from `protocore.c`, and the CSRF, lockout and forwarded-trust includes moved with the code that
  names them.

---

## Every id table a route indexes grew forever, because only the route table had a reset

- **Status:** FIXED (2026-08-06), found with the route-table fix above, which unblocked the suites.
- **Symptom:** `test_auth` passes its early cases and fails its late ones. `test_application` serves
  a static file in one case and 404s the identical setup two cases later. Nothing distinguishes a
  passing case from a failing one except how many ran before it.
- **Root cause:** a route stores an id naming a row in another module's table, and `add()` returns
  the row index. `pc_server_reset()` emptied the route table and left every one of those tables
  full. `Auth` and the mount registry both cap at `MAX_ROUTES`, so once the count reached it
  `Auth.add()` returned `PC_AUTH_NONE` and `pc_mnt_point_add()` returned `PC_MNT_NONE`. A route that
  holds those answers dispatches unguarded and serves nothing, silently, and both are the
  fail-open direction.
- **Blast radius:** any long-lived server that re-registers its routes, and every test suite that
  registers more than `MAX_ROUTES` protected routes or mounts across its cases.
- **Fix:** `Auth` publishes a `reset`, `mnt.c` publishes `pc_mnt_point_reset()`, and
  `pc_server_reset()` calls both beside `network.route->reset()`. An id is a row index, so the
  tables empty with the routes that index them.

---

## Two long-lived tables borrowed from a pool sized only for crypto working sets

- **Status:** FIXED (2026-08-06), found while chasing the `test_auth` failures above.
- **Symptom:** in `native_auth`, `Auth.add()` returns `PC_AUTH_NONE` for the first credential set
  ever registered, so every protected route dispatches unguarded. 17 of 23 cases fail.
- **Root cause:** `PC_SECURE_ARENA_SIZE` is the sum of the `PC_WORK_*` terms a build compiles, and
  `protocore_config.h` states the rule that every borrow declares one, proved by a `static_assert`
  in the owning module. `route.c` and `auth.c` both borrow from the pool and neither declared a
  term. Measured under `native_auth`: the arena is 1664 bytes (bignum 1408 + 256 round-up), the
  route table is 1416 and the credential table 1569. The route table binds first and leaves 248,
  so the credential table never binds.
- **Both were also on the wrong end of the arena.** `pc_secure_span()` bumps down from the scratch
  end, which `mark` walks and `release` reclaims. Both tables are held for the life of the program.
- **Fix:** `secure.h` publishes `pc_secure_persist_span()`, over the arena's persistent end, which
  no mark reaches and which hands back zeroed bytes. Both tables take it, `PC_WORK_ROUTE_TABLE` and
  `PC_WORK_AUTH_TABLE` join `PC_SECURE_ARENA_SIZE`, and each module `static_assert`s its struct
  against its term.

---

## The route table borrowed its storage from an init nothing called, so every route registration failed

- **Status:** FIXED (2026-08-06), found on the first matrix run that could link `native_keepalive`.
- **Symptom:** `on_http("/res", ...)` registers nothing. Every request answers 404 and no handler
  fires, while the keep-alive machinery around it behaves correctly, because a 404 is a normal
  response that keeps the connection alive. Four `test_keepalive` cases fail on the response body and
  the handler tally; the ones that only assert the Connection header pass.
- **Root cause:** moving the route table into the secure pool (3ef5c5a50) replaced its BSS with a
  borrow taken in a new `HttpRouteNs::init`, and no caller anywhere in `src/` calls it. BSS needed no
  init, so nothing existed to update. `s_route` stays NULL, `add()` returns NULL on its first guard,
  and every registration path drops the route it was handed. `pc_server_reset()` calls
  `network.route->reset()`, which is also a no-op on a NULL handle, so the table reads empty and
  consistent at every seam.
- **Blast radius:** every HTTP, WebSocket, SSE, file-serving and WebDAV route in the library. The
  envs that would have caught it were the ones failing to link for unrelated reasons, so it stayed
  invisible from 2026-08-05 until those were fixed.
- **Fix:** the borrow moves into a `bind_route()` taken on first use, the shape `auth.c` already
  uses for the same reason, and `init` leaves `HttpRouteNs`. A registration is the first thing that
  touches the table and every reader runs after one, so there is no moment a caller has to remember.

---

## The tcp_evt.h split cut 24 test suites off from tcp.h

- **Status:** FIXED (2026-08-05), found on a full-matrix run.
- **Symptom:** `ConnState`, `TcpConn`, `conn_pool` and `CONN_*` come out undeclared in a test that
  changed nothing. `native_accept_gate`, `native_presentation` and `native_session` fail to build.
- **Root cause:** `TcpEvt` and `EvtType` moved out of `tcp.h` into their own `tcp_evt.h`, and
  `presentation.h`, `session.h` and `listener.h` each narrowed their include to the new header,
  which is what they need. The slot types stayed in `tcp.h`. 24 white-box suites named the slot
  types while including only a layer header, so they were reaching `tcp.h` transitively and lost it
  the moment the layer stopped needing it.
- **Fix:** each of the 24 suites includes `network_drivers/transport/tcp.h` for the names it uses.
  The layer headers keep the narrow include.
- **Note:** only three of the 24 had been reached when the run stopped, so the count is what the
  symbol sweep found, not what the run reported.

---

## frame.c calls proto_scan_nul without including runops.h

- **Status:** FIXED (2026-08-05), found on the same run.
- **Symptom:** `native_scp` and `native_ssh_sftp` fail at link with `undefined reference to
'proto_scan_nul'`, preceded by an implicit-declaration warning at `src/mmgr/protoframe.c:113`.
- **Root cause:** `pc_frame_append` calls `proto_scan_nul`, which is a `static inline` in
  `shared_primitives/runops.h`, and `protoframe.c` includes only `mmgr/protoframe.h` and
  `shared_primitives/speed_opt.h`. Under C99 rules the implicit declaration makes it an external
  call to a name no TU defines, so it survives compilation and dies at link. Every other env that
  builds `frame.c` also builds a TU that pulls `runops.h` in ahead of it, which is why only the two
  filesystem-application envs showed it.
- **Fix:** `frame.c` includes `shared_primitives/runops.h`. (2026-08-08: that header is gone and the
  include is now `mmgr/protostr.h` - see the entry below.)

---

## The native base flags defeat PROTOCORE_HOT_FORCE, so the target path had no test env

- **Status:** FIXED (2026-08-05), found while adding the first env that builds the target path.
- **Symptom:** a native env that adds `-DPROTOCORE_HOT_FORCE` still compiles the host path.
  `PC_VENDOR_MOCK` comes out 1 and `PROTOCORE_HOT` comes out 0 at the same time, which the
  platform header states is impossible ("exact complements, there is no third"). No diagnostic:
  the env builds and its tests pass, against the wrong arm.
- **Root cause:** two sources of truth for one macro. `pc_platform.h` derives `PROTOCORE_HOST` from
  the vendor axis, defining it only in the else-arm reached when no vendor matched, and
  `PROTOCORE_HOT_FORCE` selects `PC_VENDOR_MOCK` in the arm above it. But `gen_test_envs.py` also
  passed `-DPROTOCORE_HOST=1` in the flags every native env extends. The command-line define wins:
  the guard that would have set it is skipped, `#ifndef PROTOCORE_HOST` then finds it already
  defined, and `#if PROTOCORE_HOST` selects the host path under a mock vendor. The vendor axis
  cannot override a value handed to it from outside, and `#undef` is banned (SRC_LAW rule 11), so
  no arrangement inside the header could have recovered.
- **Blast radius:** every one of the 310 native envs carried the flag, and the whole
  `PROTOCORE_HOT` half of the tree had no test env at all - `core_setup/*/mock/` exists to stand
  in for silicon and nothing was compiling against it. That is how the `pc_lwip_to_ip` bug above
  survived: its arm was unreachable from the suite.
- **Fix:** drop `-DPROTOCORE_HOST=1` from `native_base`. It was redundant - nothing on a native
  build matches a vendor, so the else-arm defines it anyway - and dropping it makes the vendor axis
  the only decider. A new `native_hot_base` extends the base with `-DPROTOCORE_HOT_FORCE`, and an
  env opts in with `"base": "native_hot_base"`.
- **Verified:** only 4 files under `src/` read `PROTOCORE_HOST` and each includes the platform
  chain in its first 40 lines; nothing in `test/mocks` or `test/support` reads it. The first env on
  the new base (`native_udp_hot`) carries `#error` on `!PROTOCORE_HOT`, so the fallback cannot
  return silently.

---

## pc_lwip_to_ip reverses the octets on the mock arm

- **Status:** FIXED (2026-08-05), found while giving UDP a wire layout built on `pc_ip`.
- **Symptom:** `pc_conn_remote_addr()` and the accept-callback address both report `5.0.0.10` where
  the peer is `10.0.0.5`, on any build that selects `PC_VENDOR_MOCK` (`-DPROTOCORE_HOT_FORCE`). No
  host suite catches it because the non-hot arm returns `PC_IP_NONE` and never reaches the mapping.
- **Root cause:** the two backends disagree on what `pc_net_ip4_u32` returns. lwIP's
  `ip4_addr_get_u32` hands back the stored `u32_t`, whose **memory bytes** are the network octets.
  The mock (`test/mocks/pc_net_host.h:730`) instead composes a **numeric** value,
  `bytes[0]<<24 | bytes[1]<<16 | bytes[2]<<8 | bytes[3]`. On a little-endian host those are byte
  reversed. `pc_lwip_to_ip` (`src/network_drivers/transport/tcp.c:915`) peels with
  `(uint8_t)be, (uint8_t)(be >> 8), ...`, which is right for lwIP on a little-endian target and
  backwards for the mock.
- **Second defect in the same three lines:** that peel is itself endianness-dependent. lwIP's value
  is network order in memory, so on a big-endian target the first octet is the high byte and the
  same code reverses there too. The portable read is of the u32's bytes, not of its value.
- **Blast radius:** `pc_net_ip4_u32` has two other callers that compare or return the value
  opaquely (`listener.c:486` interface tagging, `tcp.c:890` `pc_conn_remote_ip`), so changing the
  mock to match lwIP moves those too and needs its own verification pass.
- **Fix:** the mock returns the u32 lwIP does (the four bytes copied, not composed arithmetically),
  and the mapping moved out of `tcp.c` into `network_drivers/transport/net_addr.c` as
  `NetAddr.to_ip()`, where it reads the word's bytes instead of shifting its value and is therefore
  correct on either endianness. TCP needed it on accept and on the per-slot accessor and UDP needs
  it per received datagram, so one owner sits beside both rather than inside either.
- **Verified:** `10.0.0.5`, `192.168.4.1`, `224.0.0.251`, `255.254.253.252` round-trip through
  `NetAddr.to_ip()` and back out of `Ip.format()` unchanged on the mock arm; a null source leaves
  the out-param `PC_IP_NONE` rather than stale. Still to re-run: `native_tcp`, the listener suites,
  and `test_iface` (which asserts on `pc_ap_ip`, the value `listener.c` compares against).

---

## test_coaps segfaults after its last test passes

- **Status:** OPEN (found 2026-08-05, by the first clean run of the whole native matrix).
- **Symptom:** `native_coaps` reports all six tests PASSED and then dies with SIGSEGV, so the env
  is recorded ERRORED and the suite's result is discarded. The crash is after
  `test_coaps_forwards_handshake` returns, not inside any test body.
- **What is known:** the tests themselves pass, so it is a teardown or exit-path fault rather than
  a wrong assertion. `native_coaps_server` separately fails three tests
  (`test_server_single_peer`, `test_two_peers_routing`, `test_cid_address_migration`), which may or
  may not share a cause.
- **Not yet attributed:** it appeared in the first full-matrix run that completed, and earlier runs
  of that matrix were invalid (they defaulted to the `esp32dev` env and errored all 315 suites), so
  there is no clean before-state to compare against. It has NOT been shown to predate the bus-owner
  or toolchain work, and it has not been shown to be caused by it either.
- **Next step:** run `native_coaps` under gdb or valgrind for the faulting frame, then bisect
  against the merge-base if the frame does not name the cause.

---

## The peripheral drivers returned their host stub while the bus owners ran for real

- **Status:** FIXED (2026-08-05). Found by the end-to-end wire suite: every driver call returned
  false on host while the owners themselves worked.
- **Root cause:** the bus owners (`i2c.h`, `spi.h`, `uart.h`) were changed to key their real arm off
  `PC_PLATFORM_HAS_BUS` so a host build with the test seam drives the capture. The thirteen drivers
  that sit on them still split on `#if PROTOCORE_HOT`, so each took its own refusing stub and never
  composed a byte. The owner worked and the driver above it did not.
- **What it hid:** the interesting half of every driver - which bytes it composes, in what order, to
  which address - was unreachable on host, so it was asserted only by reading the code.
- **Fix:** the same `PROTOCORE_HOT || PC_PLATFORM_HAS_BUS` condition on all thirteen drivers.

---

## pcdelay spun forever on a host clock nothing advanced

- **Status:** FIXED (2026-08-05). Found while making the drivers' real body reachable on host.
- **Symptom:** latent until then. `pcdelay`'s host arm spun on `pc_millis()`, and the host virtual
  clock only moves when a test calls `set_millis`, so any wait with a non-zero argument never
  terminated. Nothing had reached it because the host arm of every driver refused before delaying.
- **Root cause:** the host arm was written as a bare spin on the assumption that only device code
  paths call it. Making the drivers real broke that assumption - `pc_pca9685_begin` waits 500 us for
  the oscillator and `pc_sht3x_read` waits 20 ms for a measurement.
- **Fix:** the host arm makes the same one-tick `pc_platform_task_delay(1)` hand-off the RTOS arm
  does, and the host mock advances its virtual clock there. The microsecond source also advances one
  per read, so a sub-millisecond settle terminates and its elapsed time is deterministic.

---

## Every OIDC token verification leaked four scratch borrows, and the arena never came back

- **Status:** FIXED (2026-08-03). Found by running `native_oidc` for the first time: the suite had
  not compiled since the C conversion, so nothing had executed `pc_oidc_verify_with_key` at all.
- **Symptom:** every verify returned `PC_OIDC_ERR_FORMAT` (-1), including a valid, correctly-signed
  token that should have returned 0. Twelve of the suite's tests failed with the same code whatever
  they were actually testing, because the failure happened before any of their input was examined.
- **Root cause:** `pc_oidc_verify_with_key` borrows four buffers from the plaintext arena (header,
  signature, payload, issuer - about 2.6 KB together, hoisted off the worker stack). The C++ version
  scoped them with a `PlaintextScope` whose destructor released on every return path. The conversion
  deleted the guard and did not replace it with a `pc_plaintext_mark()` / `pc_plaintext_release()`
  pair, so **every call leaked all four borrows**. The comment above the allocation still said
  "PlaintextScope reclaims them on every return path", which is how it read as correct.
- **What it does on a device:** the arena is a fixed per-worker pool reset per dispatch, so a single
  verification inside one dispatch still fits. Across the test binary the borrows accumulate until
  the arena cannot satisfy the next request, and from that point `!hdr || !sig || !pl || !iss` is
  true and every token is rejected as malformed - a fail-closed authentication outage that no input
  can clear. The same exhaustion is reachable on a target wherever more than one verify runs before
  the arena is reset.
- **Why nothing caught it:** the suite did not compile, so it never ran. `check_src_banned` does not
  model borrow/release pairing, and the leak is invisible to a reader because the comment describes
  the guard that used to be there.
- **Fix:** `size_t scope = pc_plaintext_mark();` before the first borrow, and
  `pc_plaintext_release(scope);` on each of the eleven returns after it. The two early returns above
  the mark (null token, `split3` failure) borrow nothing and are unchanged. Comment rewritten to
  describe what the code does now.
- **Worth auditing:** this is the same shape as the 17 `PlaintextScope` / `SecureScope` guards
  already converted by hand in `src/`, and the two in `test_plaintext` / `test_secure_pool` that had
  lost their release. Any other site where a deleted guard left a borrow unpaired has the same
  failure mode and the same invisibility.

## The C11 conversion never covered the PROTOCORE_HOT path, and pc_mnt_backend has no sync

- **Status:** OPEN (2026-08-03). Found by grepping `src/` for C++ constructs after the native compile
  sweep came back clean on the files it can reach.
- **Symptom:** `src/` is C11 only on the paths a native env compiles. Everything behind
  `#if PROTOCORE_HOT` is still C++, and no native env compiles it, so 307 envs of compile sweep
  cannot see any of it. Found: `namespace fs { class FS; }` plus `fs::FS &` parameters in
  `server/exc_decoder.h` / `server/exc_coredump.c`, `namespace fs` and `fs::FS *` / `fs::File &` in
  `core_setup/hal/esp/esp_mnt_fs.{h,c}`, and a whole `namespace pc_wal_fs_detail` over `fs::File`
  in `services/storage/wal/wal_fs.h`. A target build of any of them is a hard C error.
- **Root cause:** the conversion was driven by the native suites, which are the only thing that
  compiles during it. `PROTOCORE_HOT` is false on the host, so those regions were never parsed.
- **Fixed here:** `pc_exc_coredump_save` now takes `const pc_mnt_backend *` and goes through the
  vtable, so `server/` names no vendor type; `exc_decoder.h` drops the `namespace fs` forward
  declaration.
- **Still open, and why:** `wal_fs.h` cannot be retargeted onto the seam as it stands. Its durability
  barrier is `File::flush()`, and **`pc_mnt_backend` has no sync/flush entry**. Translating it
  without one would leave `WalDev::sync` returning true having done nothing, which turns a
  power-loss-safe log into one that only looks safe. Adding `sync` to the vtable touches every
  backend (RAM disk, the ESP adapter, the lfs mock), so it is an owner decision rather than a
  mechanical fix.
- **Also open:** `esp_mnt_fs.{h,c}` legitimately names Arduino's `fs::FS` because wrapping it is the
  adapter's whole job. Resolved by compiling `core_setup/hal/esp/` as C++ (owner decision), which
  needs the build rule and a SYMBOLS.md amendment recording the exemption.

## A board profile's PC_GPIO_OUT macro rewrote the pc_gpio_dir enum member of the same name

- **Status:** FIXED (2026-08-03), pending a target build. Found by the full-tree compile sweep: an
  env that pulls in both the host net mock and `gpio_map.h` failed with
  `test/mocks/pc_net_host.h:214:20: error: expected identifier before numeric constant`.
- **Symptom:** two encodings of a pin direction shared four names. `pc_gpio_dir` declared
  `PC_GPIO_IN`, `PC_GPIO_IN_PULLUP`, `PC_GPIO_IN_PULLDOWN`, `PC_GPIO_OUT` as enum members numbered
  0/1/2/3 in that order; `board_profiles/pc_platform.h` (and the host mock beside it) `#define` the
  same four names as the pin-mode argument, numbered IN=0, OUT=1, PULLUP=2, PULLDOWN=3. Where both
  headers reach one translation unit the macro wins, because substitution happens before the
  compiler sees the declaration.
- **What it did on a target:** in `pc_gpio_begin_pins`, `case PC_GPIO_OUT:` became `case 1:`, which
  is the enum's `IN_PULLUP`. A pin mapped as an output has `dir == 3`, matches no case, and falls to
  the default arm, so it was configured as an **input**: a mapped LED or relay never drives. A pin
  mapped `IN_PULLUP` (`dir == 1`) took the OUT arm and was configured as an **output**, so the panel
  drove a pin wired to a button. `pc_gpio_is_output()` compared against the same rewritten constant,
  so the write route agreed with the wrong table.
- **Root cause:** exactly the failure docs/SYMBOLS.md section 2 describes. The enum member carried
  the bare subsystem prefix rather than its own type's, leaving it in the same token space as a macro
  the board layer had every right to define.
- **Why nothing caught it:** the two headers only meet in a host env that mocks the platform, and
  `native_gpio_map` was not one of them, so the enum was never parsed with the macro already
  defined. Where they did not meet, the enum spelled itself and every test passed. On a target the
  numbers silently disagree and nothing is diagnosed, because both sides are valid integers.
- **Fix:** the members take the type's own prefix: `PC_GPIO_DIR_IN`, `PC_GPIO_DIR_IN_PULLUP`,
  `PC_GPIO_DIR_IN_PULLDOWN`, `PC_GPIO_DIR_OUT` (`gpio_map.h`), so the case labels are the enum and
  the arguments to `pc_platform_gpio_mode()` stay the board profile's numbers. Call sites updated in
  `gpio_map.c`, `test_gpio_map.c` and the `GpioMap` example.

## HTTP/2 refuses an over-limit stream with a RST_STREAM naming stream 0

- **Status:** OPEN (2026-08-03). Found while converting `h2_conn`'s last lambda to a named function,
  which put the builder's arguments on their own line.
- **Symptom:** when the stream table is full, `handle_headers()` answers the new HEADERS with
  `pc_h2_build_rst_stream(b, cap, 0, 0)` - stream id 0, error code 0 - and keeps the connection.
  RFC 9113 sec 6.4 says a RST_STREAM with a stream identifier of 0x00 is a connection error of type
  PROTOCOL_ERROR, so a conforming peer must respond with GOAWAY and close. The refusal that was
  meant to shed one stream takes the whole connection down instead, and the error code says
  NO_ERROR rather than REFUSED_STREAM (0x07), which is what tells a client the request was not
  processed and is safe to retry elsewhere.
- **Root cause:** `send_control()` takes a `size_t (*)(uint8_t *, size_t)`, which carries no room
  for the stream id, so the call site passed the two constants the signature allowed.
- **Why nothing caught it:** `test_h2_stream_table_full_rst` asserts only that at least one
  RST_STREAM frame goes out, never which stream it names. No interop test drives the table to its
  limit.
- **Fix:** not yet applied - the refusal needs the stream id, so `send_control`'s builder signature
  has to carry it (or the refusal path builds its frame directly). Deliberately left alone during
  the C conversion, whose contract was to change no behavior.

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

## The theme-blob generator kept emitting C++, so CI resurrects a dead `binary_asset_blobs.c` on every run

- **Status:** OPEN (2026-08-02). Found by widening `check_duplicate_symbols` to `.c`, which it had
  stopped reading.
- **Symptom:** `PC_THEME_BLOBS` and `PC_THEME_BLOB_COUNT` are each defined in two tracked files,
  `binary_asset_blobs.c` and `binary_asset_blobs.c`. Any build that compiles the whole tree -
  Arduino, and the ESP-IDF component, whose `CMakeLists.txt` globs `src/*.c` and `src/*.cpp` -
  gets two definitions of both and fails to link. The native envs name their sources explicitly
  and reference neither file, so nothing in the test matrix sees it.
- **Root cause:** `web_assets/wizard/gen_theme_blobs.py` writes `BASENAME + ".cpp"` and renders
  `return nullptr;` with unbraced bodies. The C conversion produced the `.c` by hand-editing that
  generated output (`nullptr` -> `NULL`, braces added) and left the `.cpp` tracked. The generator
  was never converted with the tree, so `feature-tables.yml` re-runs it and `git add`s
  `binary_asset_blobs.c` explicitly - deleting the file by hand is undone by the next CI run.
  The `.cpp` is byte-identical to what the generator emits today, which is what identifies it as
  the live output and the `.c` as the hand-edited copy.
- **Why nothing caught it:** `check_duplicate_symbols` globbed `*.cpp` only, so after the
  conversion it compared 51 files out of 351 and could not see a `.c`/`.cpp` pair. It reported OK
  throughout. No native env compiles either file, and no target build runs in CI.
- **Fix:** the generator emits `.c` and emits C (`NULL`, braced bodies); `--check` now reproduces
  the committed `.c` byte for byte. `feature-tables.yml` and `.clang-format-ignore` follow the
  rename. The dead `binary_asset_blobs.c` still has to be deleted - pending, because deleting it
  before the generator fix landed would only have brought it back.

## `webdav.c` and `webdav.c` are two different modules sharing one name in one directory

- **Status:** OPEN (2026-08-02). Found alongside the `binary_asset_blobs` pair.
- **Symptom:** `src/network_drivers/application/webdav/` holds both `webdav.c` (the
  filesystem-backed half: PROPFIND/PUT/COPY/MOVE over a mounted subtree) and `webdav.c` (the
  pure core: method classification, header parsing, the 207 Multi-Status XML builder). They define
  different symbols, so this does not break the link, and both are live - the test matrix builds
  `webdav.c` in five envs and `webdav.c` in three, and one env builds both.
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
