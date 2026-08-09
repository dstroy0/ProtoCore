# Test Report

**Generated:** 2026-08-09 01:09:58
**Command:** `pio test` over 324 auto-discovered native envs (excludes native_pentest, native_codeql)
**Result:** ❌ 1921 passed, 220 failed - 1723s

---

## Summary

| Suite | Environment | Tests | Status | Duration |
| :---- | :---------- | ----: | :----: | -------: |

---

## test_crc - native_primitives - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the shared parameterized CRC engine (shared_primitives/crc.h)._

|   # | Test                                                  | Status | Description                                                                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_cataloge_check_values`                          |   ✅   | Cataloge check values                                                                          |
|   2 | `test_reflection_flags_actually_apply`                |   ✅   | Reflection flags actually apply                                                                |
|   3 | `test_streaming_matches_one_shot`                     |   ✅   | Streaming matches one shot                                                                     |
|   4 | `test_single_bit_flip_changes_the_crc`                |   ✅   | Single bit flip changes the crc                                                                |
|   5 | `test_order_sensitivity`                              |   ✅   | Order sensitivity                                                                              |
|   6 | `test_leading_zeros_are_significant`                  |   ✅   | Leading zeros are significant                                                                  |
|   7 | `test_empty_input_is_the_bare_init`                   |   ✅   | With no octets folded in, the result is init through the output stage - not an error.          |
|   8 | `test_width_is_respected`                             |   ✅   | Every result must fit its declared width - a leaked high bit would corrupt a packed frame.     |
|   9 | `test_out_of_range_width_is_clamped`                  |   ✅   | Out of range width is clamped                                                                  |
|  10 | `test_engine_matches_the_hand_rolled_implementations` |   ✅   | A spread of lengths, including the empty and single-octet degenerate cases, over a buffer with |
|  11 | `test_null_guards`                                    |   ✅   | Null guards                                                                                    |

</details>

---

## test_primitives - native_primitives - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the shared no-stdlib primitives: the base-10 number parsers_

|   # | Test                       | Status | Description                                                                              |
| --: | :------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_sb_u32`              |   ✅   | Sb u32                                                                                   |
|   2 | `test_sb_u32_boundaries`   |   ✅   | For each digit count, a buffer holding exactly the digits + NUL must succeed, and one    |
|   3 | `test_sb_overflow_latches` |   ✅   | Once ok latches false every later append is a no-op, so callers test one flag at the end |
|   4 | `test_sb_widths_and_bases` |   ✅   | Sb widths and bases                                                                      |
|   5 | `test_sb_64bit`            |   ✅   | Sb 64bit                                                                                 |
|   6 | `test_sb_g_matches_libc`   |   ✅   | Sb g matches libc                                                                        |
|   7 | `test_sb_json_escapes`     |   ✅   | Sb json escapes                                                                          |
|   8 | `test_strtol`              |   ✅   | Strtol                                                                                   |
|   9 | `test_strtoul`             |   ✅   | Strtoul                                                                                  |
|  10 | `test_strtof`              |   ✅   | Strtof                                                                                   |
|  11 | `test_numparse_branches`   |   ✅   | pc_np_ws: exercise every whitespace operand (line 24) - a run of each                    |
|  12 | `test_utf8_valid`          |   ✅   | Utf8 valid                                                                               |
|  13 | `test_utf8_invalid`        |   ✅   | Utf8 invalid                                                                             |

</details>

---

## test_bus_capture - native_bus_capture - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CAN listen-only capture framing (server/signaling/bus_capture): can_to_socketcan()_

|   # | Test                               | Status | Description                                                                           |
| --: | :--------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_standard_data_frame`         |   ✅   | Standard data frame                                                                   |
|   2 | `test_extended_id_sets_eff`        |   ✅   | Extended id sets eff                                                                  |
|   3 | `test_rtr_flag_and_no_data`        |   ✅   | Rtr flag and no data                                                                  |
|   4 | `test_masks_and_bounds`            |   ✅   | Masks and bounds                                                                      |
|   5 | `test_pcap_can_linktype`           |   ✅   | Pcap can linktype                                                                     |
|   6 | `test_pcap_global_header_bounds`   |   ✅   | Pcap global header bounds                                                             |
|   7 | `test_pcap_record_header_bounds`   |   ✅   | Pcap record header bounds                                                             |
|   8 | `test_host_twai_stubs_fail_closed` |   ✅   | On host there is no TWAI controller: begin fails closed and poll/end are safe no-ops. |
|   9 | `test_host_can_stubs`              |   ✅   | Host build: no TWAI/CAN peripheral. begin() fails; poll/end are no-ops.               |

</details>

---

## test_roaming - native_roaming - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Wi-Fi roaming decision layer (network_drivers/network/roaming): the pure policy that fuses the_

|   # | Test                                                | Status | Description                                                                                       |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------------ |
|   1 | `test_stay_when_link_strong`                        |   ✅   | Strong current link (-50); even a stronger candidate does not trigger a roam below the threshold. |
|   2 | `test_roam_on_low_rssi_to_strongest`                |   ✅   | Weak current link (-78) and AP_A is clearly stronger (-55): roam to AP_A.                         |
|   3 | `test_hysteresis_blocks_marginal_roam`              |   ✅   | Weak link (-78) but the best candidate is only 4 dB better (< 8 dB hysteresis): stay.             |
|   4 | `test_btm_imminent_forces_roam`                     |   ✅   | Btm imminent forces roam                                                                          |
|   5 | `test_btm_suggested_honoured_only_if_not_weaker`    |   ✅   | Btm suggested honoured only if not weaker                                                         |
|   6 | `test_never_targets_current_and_guards`             |   ✅   | The neighbour list contains only the current BSSID -> nothing to roam to even on a weak link.     |
|   7 | `test_parse_neighbor_report`                        |   ✅   | A non-neighbor element (id 7, len 3) between the two must be skipped.                             |
|   8 | `test_parse_neighbor_report_edges`                  |   ✅   | A neighbor element shorter than the 13-octet body is skipped (not decoded).                       |
|   9 | `test_parse_btm_request`                            |   ✅   | BTM Request: preferred-list (bit 0) + disassoc-imminent (bit 2) = 0x05, one candidate (AP_A).     |
|  10 | `test_parse_btm_request_optional_fields_and_guards` |   ✅   | Request mode with BSS-Termination-Included (bit 3) + preferred list (bit 0) = 0x09: the candidate |

</details>

---

## test_iolink - native_iolink - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the IO-Link (SDCI) data-link message codec (services/fieldbus/iolink): the MC / CKT /_

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_mc_octet`                                       |   ✅   | read, Page channel, address 0x10 -> 0x80       | (1<<5) | 0x10 = 0xB0. |
|   2 | `test_ckt_cks_octets`                                 |   ✅   | Ckt cks octets                                 |
|   3 | `test_checksum_known_vector`                          |   ✅   | Checksum known vector                          |
|   4 | `test_finalize_preserves_type_and_detects_corruption` |   ✅   | Finalize preserves type and detects corruption |
|   5 | `test_device_reply_cks_roundtrip`                     |   ✅   | Device reply cks roundtrip                     |
|   6 | `test_iol_finalize_verify_guards`                     |   ✅   | Iol finalize verify guards                     |

</details>

---

## test_base64 - native_base64_scalar - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_base64 codec tests, anchored on the RFC 4648 sec 10 vectors, both alphabets, and the constant-time_

|   # | Test                                               | Status | Description                                                                 |
| --: | :------------------------------------------------- | :----: | :-------------------------------------------------------------------------- |
|   1 | `test_rfc4648_vectors`                             |   ✅   | Rfc4648 vectors                                                             |
|   2 | `test_alphabets`                                   |   ✅   | Alphabets                                                                   |
|   3 | `test_decode_rejects_malformed`                    |   ✅   | Decode rejects malformed                                                    |
|   4 | `test_decode_capacity_guard`                       |   ✅   | "foobar" decodes to 6 bytes; a 2-byte buffer must fail rather than overrun. |
|   5 | `test_decode_capacity_guard_first_and_second_byte` |   ✅   | Decode capacity guard first and second byte                                 |
|   6 | `test_url_decode_stops_at_padding`                 |   ✅   | Url decode stops at padding                                                 |
|   7 | `test_url_decode_capacity_guard`                   |   ✅   | Url decode capacity guard                                                   |
|   8 | `test_roundtrip_fuzz`                              |   ✅   | Roundtrip fuzz                                                              |

</details>

---

## test_ssh_server - native_ssh - ✅ 39 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_End-to-end SSH server dispatcher test: drives a full handshake_

|   # | Test                                                  | Status | Description                                                               |
| --: | :---------------------------------------------------- | :----: | :------------------------------------------------------------------------ |
|   1 | `test_ssh_dispatch_bad_slot`                          |   ✅   | Ssh dispatch bad slot                                                     |
|   2 | `test_ssh_kexinit_parse_fail`                         |   ✅   | Ssh kexinit parse fail                                                    |
|   3 | `test_ssh_kexdh_guards`                               |   ✅   | Ssh kexdh guards                                                          |
|   4 | `test_ssh_service_request_fail`                       |   ✅   | Ssh service request fail                                                  |
|   5 | `test_ssh_userauth_guards`                            |   ✅   | Ssh userauth guards                                                       |
|   6 | `test_ssh_postauth_authed_guard`                      |   ✅   | Ssh postauth authed guard                                                 |
|   7 | `test_ssh_postauth_handler_fails`                     |   ✅   | Ssh postauth handler fails                                                |
|   8 | `test_ssh_open_confirm_failure_authed`                |   ✅   | Ssh open confirm failure authed                                           |
|   9 | `test_ssh_global_request_reply`                       |   ✅   | Ssh global request reply                                                  |
|  10 | `test_ssh_window_adjust_and_eof`                      |   ✅   | Ssh window adjust and eof                                                 |
|  11 | `test_ssh_pkt_index_and_cap_guards`                   |   ✅   | Ssh pkt index and cap guards                                              |
|  12 | `test_ssh_pkt_recv_unencrypted_errors`                |   ✅   | Ssh pkt recv unencrypted errors                                           |
|  13 | `test_ssh_pkt_seq_overflow_guards`                    |   ✅   | Ssh pkt seq overflow guards                                               |
|  14 | `test_ssh_pkt_encrypted_roundtrip_and_mac_fail`       |   ✅   | Ssh pkt encrypted roundtrip and mac fail                                  |
|  15 | `test_ssh_pkt_client_role_and_zero_remainder_padding` |   ✅   | Ssh pkt client role and zero remainder padding                            |
|  16 | `test_ssh_pkt_client_role_all_cipher_modes`           |   ✅   | Ssh pkt client role all cipher modes                                      |
|  17 | `test_ssh_pkt_aesgcm_minimum_padding`                 |   ✅   | Ssh pkt aesgcm minimum padding                                            |
|  18 | `test_ssh_pkt_chachapoly_frame_errors`                |   ✅   | Ssh pkt chachapoly frame errors                                           |
|  19 | `test_ssh_pkt_aesgcm_frame_errors`                    |   ✅   | Ssh pkt aesgcm frame errors                                               |
|  20 | `test_ssh_pkt_ctr_etm_frame_errors`                   |   ✅   | Ssh pkt ctr etm frame errors                                              |
|  21 | `test_ssh_pkt_ctr_emac_and_plain_frame_errors`        |   ✅   | Ssh pkt ctr emac and plain frame errors                                   |
|  22 | `test_full_handshake_to_channel_data`                 |   ✅   | Banner exchange already done out-of-band; seed V_C and enter KEXINIT.     |
|  23 | `test_extinfo_build_advertises_server_sig_algs`       |   ✅   | Extinfo build advertises server sig algs                                  |
|  24 | `test_extinfo_not_sent_without_ext_info_c`            |   ✅   | Extinfo not sent without ext info c                                       |
|  25 | `test_inbound_ext_info_ignored`                       |   ✅   | Inbound ext info ignored                                                  |
|  26 | `test_large_client_kexinit_accepted`                  |   ✅   | Large client kexinit accepted                                             |
|  27 | `test_channel_open_before_auth_rejected`              |   ✅   | Channel open before auth rejected                                         |
|  28 | `test_service_request_before_newkeys_rejected`        |   ✅   | Service request before newkeys rejected                                   |
|  29 | `test_disconnect_closes`                              |   ✅   | Disconnect closes                                                         |
|  30 | `test_ignore_is_noop`                                 |   ✅   | Ignore is noop                                                            |
|  31 | `test_auth_bruteforce_disconnect`                     |   ✅   | The first SSH_MAX_AUTH_ATTEMPTS-1 failures keep the connection open.      |
|  32 | `test_auth_success_after_failures`                    |   ✅   | Auth success after failures                                               |
|  33 | `test_unimplemented_reply_for_unknown_message`        |   ✅   | Unimplemented reply for unknown message                                   |
|  34 | `test_inbound_close_emits_eof_then_close_separately`  |   ✅   | Open a channel so the close path has something to close (peer id 21).     |
|  35 | `test_ssh_global_request_silent_without_want_reply`   |   ✅   | Ssh global request silent without want reply                              |
|  36 | `test_ssh_channel_request_silent_without_want_reply`  |   ✅   | Ssh channel request silent without want reply                             |
|  37 | `test_ssh_channel_close_unhandled_emits_nothing`      |   ✅   | No channel has been opened in this test, so recipient 0 does not resolve. |
|  38 | `test_ssh_kexinit_midsession_rekey`                   |   ✅   | Ssh kexinit midsession rekey                                              |
|  39 | `test_ssh_dispatch_without_emit_cb`                   |   ✅   | Ssh dispatch without emit cb                                              |

</details>

---

## test_ssh_transport - native_ssh - ✅ 63 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_SSH transport handshake tests (RFC 4253): identification-string exchange and_

|   # | Test                                                              | Status | Description                                                                               |
| --: | :---------------------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_hostkey_ecdsa_set_rejects_invalid_scalar`                   |   ✅   | Hostkey ecdsa set rejects invalid scalar                                                  |
|   2 | `test_kexdh_handle_ecdsa_hostkey_absent_fails`                    |   ✅   | Kexdh handle ecdsa hostkey absent fails                                                   |
|   3 | `test_transport_index_guards`                                     |   ✅   | Transport index guards                                                                    |
|   4 | `test_banner_and_build_caps`                                      |   ✅   | Banner and build caps                                                                     |
|   5 | `test_kexinit_parse_field_and_trunc`                              |   ✅   | Kexinit parse field and trunc                                                             |
|   6 | `test_kexdh_parse_and_handle_errors`                              |   ✅   | Kexdh parse and handle errors                                                             |
|   7 | `test_server_banner_format`                                       |   ✅   | Server banner format                                                                      |
|   8 | `test_recv_banner_complete`                                       |   ✅   | Recv banner complete                                                                      |
|   9 | `test_recv_banner_bare_lf`                                        |   ✅   | Recv banner bare lf                                                                       |
|  10 | `test_recv_banner_split_across_reads`                             |   ✅   | Recv banner split across reads                                                            |
|  11 | `test_recv_banner_skips_preamble_lines`                           |   ✅   | RFC 4253 §4.2 allows lines before the SSH identification string.                          |
|  12 | `test_kexinit_build_starts_with_msg_and_stores_is`                |   ✅   | Kexinit build starts with msg and stores is                                               |
|  13 | `test_kexinit_parse_accepts_supported`                            |   ✅   | Kexinit parse accepts supported                                                           |
|  14 | `test_kexinit_parse_accepts_when_ours_listed_among_others`        |   ✅   | Kexinit parse accepts when ours listed among others                                       |
|  15 | `test_kexinit_parse_rejects_missing_kex`                          |   ✅   | Only a KEX method we do not implement (nistp521) -> no mutual KEX -> reject. (nistp256 IS |
|  16 | `test_kexinit_parse_rejects_hostkey_we_lack`                      |   ✅   | Kexinit parse rejects hostkey we lack                                                     |
|  17 | `test_kexinit_parse_steers_to_curve_ed25519`                      |   ✅   | Kexinit parse steers to curve ed25519                                                     |
|  18 | `test_kexinit_parse_rejects_missing_cipher`                       |   ✅   | Only ciphers we do not implement -> no mutual cipher -> reject.                           |
|  19 | `test_kexinit_parse_selects_chacha20poly1305`                     |   ✅   | Kexinit parse selects chacha20poly1305                                                    |
|  20 | `test_kexinit_parse_selects_aes256gcm`                            |   ✅   | Kexinit parse selects aes256gcm                                                           |
|  21 | `test_kexinit_parse_honors_client_cipher_preference`              |   ✅   | Kexinit parse honors client cipher preference                                             |
|  22 | `test_kexinit_parse_selects_rsa_sha512`                           |   ✅   | Both offered -> rsa-sha2-512 wins (server preference).                                    |
|  23 | `test_kexinit_parse_selects_ecdsa`                                |   ✅   | Kexinit parse selects ecdsa                                                               |
|  24 | `test_kexinit_parse_selects_ecdh_nistp256`                        |   ✅   | Kexinit parse selects ecdh nistp256                                                       |
|  25 | `test_kexinit_parse_selects_etm_mac`                              |   ✅   | Kexinit parse selects etm mac                                                             |
|  26 | `test_kexinit_parse_rejects_truncated`                            |   ✅   | Kexinit parse rejects truncated                                                           |
|  27 | `test_exchange_hash_matches_independent_assembly`                 |   ✅   | Populate the session fields the hash reads.                                               |
|  28 | `test_exchange_hash_changes_with_input`                           |   ✅   | Exchange hash changes with input                                                          |
|  29 | `test_kexdh_parse_init_extracts_e_with_padding`                   |   ✅   | Kexdh parse init extracts e with padding                                                  |
|  30 | `test_kexdh_parse_init_extracts_small_e`                          |   ✅   | Kexdh parse init extracts small e                                                         |
|  31 | `test_kexdh_parse_init_rejects_wrong_type`                        |   ✅   | Kexdh parse init rejects wrong type                                                       |
|  32 | `test_kexdh_parse_init_rejects_oversized_e`                       |   ✅   | mpint with 300 magnitude bytes → exceeds 2048 bits.                                       |
|  33 | `test_kexdh_build_reply_structure`                                |   ✅   | Kexdh build reply structure                                                               |
|  34 | `test_kexdh_handle_produces_reply_and_installs_keys`              |   ✅   | Kexdh handle produces reply and installs keys                                             |
|  35 | `test_kexdh_handle_rejects_invalid_e`                             |   ✅   | Kexdh handle rejects invalid e                                                            |
|  36 | `test_kexdh_handle_curve25519_ed25519_end_to_end`                 |   ✅   | Fixed baseline host keys for deterministic regression, plus one fresh throwaway           |
|  37 | `test_kexdh_handle_curve25519_rejects_low_order`                  |   ✅   | Kexdh handle curve25519 rejects low order                                                 |
|  38 | `test_kexdh_handle_ecdh_nistp256_end_to_end`                      |   ✅   | Kexdh handle ecdh nistp256 end to end                                                     |
|  39 | `test_kexdh_handle_ecdh_nistp256_rejects_bad_point`               |   ✅   | Kexdh handle ecdh nistp256 rejects bad point                                              |
|  40 | `test_kexdh_handle_rsa_sha512_signature`                          |   ✅   | Kexdh handle rsa sha512 signature                                                         |
|  41 | `test_kexdh_handle_ecdsa_end_to_end`                              |   ✅   | Kexdh handle ecdsa end to end                                                             |
|  42 | `test_derive_keys_session_id_affects_output`                      |   ✅   | Derive keys session id affects output                                                     |
|  43 | `test_rekey_needed_threshold`                                     |   ✅   | Rekey needed threshold                                                                    |
|  44 | `test_rekey_due_volume_and_time`                                  |   ✅   | Neither budget spent.                                                                     |
|  45 | `test_begin_rekey_preserves_session_and_auth`                     |   ✅   | Begin rekey preserves session and auth                                                    |
|  46 | `test_kdf_edge_paths_and_slot_guards`                             |   ✅   | Kdf edge paths and slot guards                                                            |
|  47 | `test_kexinit_parse_truncation_points`                            |   ✅   | One cut per name-list read, in field order: kex / host-key / cipher-c2s / cipher-s2c /    |
|  48 | `test_ssh_transport_more_guards`                                  |   ✅   | Ssh transport more guards                                                                 |
|  49 | `test_dh_derive_keys_gcm_installs`                                |   ✅   | Dh derive keys gcm installs                                                               |
|  50 | `test_kdf_string_k_hybrid`                                        |   ✅   | Kdf string k hybrid                                                                       |
|  51 | `test_kexinit_parse_rejects_direction_mismatch`                   |   ✅   | Kexinit parse rejects direction mismatch                                                  |
|  52 | `test_kexinit_parse_aead_ignores_mac_lists`                       |   ✅   | Kexinit parse aead ignores mac lists                                                      |
|  53 | `test_kexinit_parse_same_length_names_do_not_match`               |   ✅   | Kexinit parse same length names do not match                                              |
|  54 | `test_extinfo_build_modern_first_order`                           |   ✅   | Extinfo build modern first order                                                          |
|  55 | `test_kexdh_handle_curve25519_rejects_malformed_init`             |   ✅   | Kexdh handle curve25519 rejects malformed init                                            |
|  56 | `test_kexdh_handle_ecdh_p256_rejects_malformed_init`              |   ✅   | Kexdh handle ecdh p256 rejects malformed init                                             |
|  57 | `test_recv_banner_empty_and_short_preamble_lines`                 |   ✅   | Recv banner empty and short preamble lines                                                |
|  58 | `test_kexinit_parse_rejects_short_and_mistyped`                   |   ✅   | Kexinit parse rejects short and mistyped                                                  |
|  59 | `test_kexdh_parse_init_accepts_all_zero_mpint`                    |   ✅   | Kexdh parse init accepts all zero mpint                                                   |
|  60 | `test_kexdh_handle_ecdh_p256_rejects_bad_ephemeral`               |   ✅   | Kexdh handle ecdh p256 rejects bad ephemeral                                              |
|  61 | `test_rekey_needed_on_receive_sequence_alone`                     |   ✅   | Rekey needed on receive sequence alone                                                    |
|  62 | `test_kexinit_hostkey_list_carries_all_four_when_all_keys_loaded` |   ✅   | Kexinit hostkey list carries all four when all keys loaded                                |
|  63 | `test_cyclonessh_kex_repro`                                       |   ✅   | Cyclonessh kex repro                                                                      |

</details>

---

## test_ssh_auth - native_ssh - ✅ 29 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_SSH user-authentication tests (RFC 4252): service request/accept, request_

|   # | Test                                              | Status | Description                                       |
| --: | :------------------------------------------------ | :----: | :------------------------------------------------ |
|   1 | `test_service_request_errors`                     |   ✅   | Service request errors                            |
|   2 | `test_build_response_guards`                      |   ✅   | Build response guards                             |
|   3 | `test_parse_request_truncations`                  |   ✅   | Parse request truncations                         |
|   4 | `test_pubkey_blob_parse_failures`                 |   ✅   | Pubkey blob parse failures                        |
|   5 | `test_pubkey_oversized_signed_prefix`             |   ✅   | Pubkey oversized signed prefix                    |
|   6 | `test_handle_request_index_and_parse_guards`      |   ✅   | Handle request index and parse guards             |
|   7 | `test_pubkey_without_verifier_fails`              |   ✅   | Pubkey without verifier fails                     |
|   8 | `test_pubkey_rsa_blob_type_length_and_zero_mpint` |   ✅   | Pubkey rsa blob type length and zero mpint        |
|   9 | `test_pubkey_ed25519_blob_and_siglen_rejections`  |   ✅   | Pubkey ed25519 blob and siglen rejections         |
|  10 | `test_pubkey_ecdsa_blob_rejections`               |   ✅   | Pubkey ecdsa blob rejections                      |
|  11 | `test_pubkey_ecdsa_signature_rejections`          |   ✅   | Pubkey ecdsa signature rejections                 |
|  12 | `test_pubkey_verifier_rejects_key`                |   ✅   | Pubkey verifier rejects key                       |
|  13 | `test_build_failure_partial_success_flag`         |   ✅   | Build failure partial success flag                |
|  14 | `test_service_request_accept`                     |   ✅   | Service request accept                            |
|  15 | `test_service_request_rejects_unknown`            |   ✅   | Service request rejects unknown                   |
|  16 | `test_parse_password_request`                     |   ✅   | Parse password request                            |
|  17 | `test_parse_none_request`                         |   ✅   | Parse none request                                |
|  18 | `test_handle_request_success`                     |   ✅   | Handle request success                            |
|  19 | `test_handle_request_wrong_password_fails`        |   ✅   | Handle request wrong password fails               |
|  20 | `test_handle_none_request_fails_without_auth`     |   ✅   | Handle none request fails without auth            |
|  21 | `test_handle_request_no_callback_fails`           |   ✅   | No callback installed → all credentials rejected. |
|  22 | `test_pubkey_probe_returns_pk_ok`                 |   ✅   | Pubkey probe returns pk ok                        |
|  23 | `test_pubkey_valid_signature_succeeds`            |   ✅   | Pubkey valid signature succeeds                   |
|  24 | `test_pubkey_rsa_sha512_signature_succeeds`       |   ✅   | Pubkey rsa sha512 signature succeeds              |
|  25 | `test_pubkey_ecdsa_signature_succeeds`            |   ✅   | Pubkey ecdsa signature succeeds                   |
|  26 | `test_pubkey_ed25519_valid_signature_succeeds`    |   ✅   | Pubkey ed25519 valid signature succeeds           |
|  27 | `test_pubkey_tampered_signature_fails`            |   ✅   | Pubkey tampered signature fails                   |
|  28 | `test_pubkey_unauthorized_key_fails`              |   ✅   | Pubkey unauthorized key fails                     |
|  29 | `test_aesgcm_gctr_counter_byte_carry`             |   ✅   | Aesgcm gctr counter byte carry                    |

</details>

---

## test_ssh_channel - native_ssh - ✅ 50 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_SSH connection-protocol (channel) tests - RFC 4254, including multiplexing_

|   # | Test                                                 | Status | Description                                                                                    |
| --: | :--------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_chan_slot_and_msgtype_guards`                  |   ✅   | Chan slot and msgtype guards                                                                   |
|   2 | `test_chan_malformed_payloads`                       |   ✅   | Chan malformed payloads                                                                        |
|   3 | `test_chan_open_cap_guards`                          |   ✅   | Chan open cap guards                                                                           |
|   4 | `test_chan_forward_and_channel_guards`               |   ✅   | While a slot is free: null address (262) and a too-small buffer (273).                         |
|   5 | `test_chan_global_request_reply_caps`                |   ✅   | Unknown request name, want_reply, no room for the 1-byte reply (246).                          |
|   6 | `test_chan_empty_and_mistyped_payloads`              |   ✅   | Chan empty and mistyped payloads                                                               |
|   7 | `test_chan_same_length_names_do_not_match`           |   ✅   | "tcpip-forwarX" is 13 chars like "tcpip-forward"; "cancel-tcpip-forwarX" is 20 like the cancel |
|   8 | `test_chan_request_accept_set`                       |   ✅   | Chan request accept set                                                                        |
|   9 | `test_chan_missing_trailing_port`                    |   ✅   | Chan missing trailing port                                                                     |
|  10 | `test_chan_rforward_refused_paths`                   |   ✅   | Chan rforward refused paths                                                                    |
|  11 | `test_chan_forwarded_open_guards_and_silent_failure` |   ✅   | Chan forwarded open guards and silent failure                                                  |
|  12 | `test_chan_data_without_sinks_and_empty_payload`     |   ✅   | Session channel with no data callback.                                                         |
|  13 | `test_chan_outbound_limits_and_window_saturation`    |   ✅   | Chan outbound limits and window saturation                                                     |
|  14 | `test_open_session_confirms`                         |   ✅   | Open session confirms                                                                          |
|  15 | `test_open_unknown_type_fails`                       |   ✅   | Open unknown type fails                                                                        |
|  16 | `test_direct_tcpip_no_cb_prohibited`                 |   ✅   | Forwarding is opt-in: with no open callback installed it is refused.                           |
|  17 | `test_direct_tcpip_accept_confirms`                  |   ✅   | Direct tcpip accept confirms                                                                   |
|  18 | `test_direct_tcpip_refused_connect_failed`           |   ✅   | Direct tcpip refused connect failed                                                            |
|  19 | `test_forward_data_routes_to_forward_cb`             |   ✅   | Forward data routes to forward cb                                                              |
|  20 | `test_shell_request_success_with_reply`              |   ✅   | Shell request success with reply                                                               |
|  21 | `test_unknown_request_failure`                       |   ✅   | Unknown request failure                                                                        |
|  22 | `test_request_no_reply_produces_nothing`             |   ✅   | Request no reply produces nothing                                                              |
|  23 | `test_inbound_data_invokes_callback`                 |   ✅   | Inbound data invokes callback                                                                  |
|  24 | `test_inbound_data_window_replenish`                 |   ✅   | Inbound data window replenish                                                                  |
|  25 | `test_inbound_data_exceeding_window_rejected`        |   ✅   | Inbound data exceeding window rejected                                                         |
|  26 | `test_outbound_data_frames_and_decrements_window`    |   ✅   | Outbound data frames and decrements window                                                     |
|  27 | `test_outbound_data_exceeding_peer_window_rejected`  |   ✅   | Outbound data exceeding peer window rejected                                                   |
|  28 | `test_window_adjust_grows_peer_window`               |   ✅   | Window adjust grows peer window                                                                |
|  29 | `test_build_close_emits_eof_and_close`               |   ✅   | Build close emits eof and close                                                                |
|  30 | `test_inbound_close_routes_to_channel`               |   ✅   | Inbound close routes to channel                                                                |
|  31 | `test_multiplex_two_channels_route_independently`    |   ✅   | Multiplex two channels route independently                                                     |
|  32 | `test_pool_full_open_fails`                          |   ✅   | Pool full open fails                                                                           |
|  33 | `test_data_to_unknown_channel_rejected`              |   ✅   | Data to unknown channel rejected                                                               |
|  34 | `test_rforward_no_cb_refused`                        |   ✅   | Rforward no cb refused                                                                         |
|  35 | `test_rforward_accept_specific_port`                 |   ✅   | Rforward accept specific port                                                                  |
|  36 | `test_rforward_port0_echoes_allocated`               |   ✅   | Rforward port0 echoes allocated                                                                |
|  37 | `test_rforward_no_reply_silent`                      |   ✅   | Rforward no reply silent                                                                       |
|  38 | `test_rforward_cancel`                               |   ✅   | Rforward cancel                                                                                |
|  39 | `test_global_unknown_request`                        |   ✅   | Global unknown request                                                                         |
|  40 | `test_global_malformed`                              |   ✅   | Global malformed                                                                               |
|  41 | `test_forwarded_open_builds_channel`                 |   ✅   | Forwarded open builds channel                                                                  |
|  42 | `test_forwarded_confirm_opens_channel`               |   ✅   | Forwarded confirm opens channel                                                                |
|  43 | `test_forwarded_failure_frees_channel`               |   ✅   | Forwarded failure frees channel                                                                |
|  44 | `test_forwarded_confirm_unknown_rejected`            |   ✅   | Forwarded confirm unknown rejected                                                             |
|  45 | `test_forwarded_inbound_data_routes_to_forward_cb`   |   ✅   | Forwarded inbound data routes to forward cb                                                    |
|  46 | `test_sftp_subsystem_routes`                         |   ✅   | Sftp subsystem routes                                                                          |
|  47 | `test_unknown_subsystem_refused`                     |   ✅   | Unknown subsystem refused                                                                      |
|  48 | `test_sftp_subsystem_match_and_missing_cb`           |   ✅   | Sftp subsystem match and missing cb                                                            |
|  49 | `test_scp_exec_routes`                               |   ✅   | Scp exec routes                                                                                |
|  50 | `test_scp_exec_match_and_missing_cb`                 |   ✅   | Scp exec match and missing cb                                                                  |

</details>

---

## test_ssh_crypto - native_ssh - ✅ 61 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_SSH crypto layer test suite._

|   # | Test                                         | Status | Description                                                                                         |
| --: | :------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------------- |
|   1 | `test_ghash_table_matches_bitwise`           |   ✅   | Ghash table matches bitwise                                                                         |
|   2 | `test_sha256_empty`                          |   ✅   | SHA256("") = e3b0c44298fc1c149afb...                                                                |
|   3 | `test_sha256_abc`                            |   ✅   | SHA256("abc") = ba7816bf8f01cfea414140de5dae2ec73b00361bbef0469...                                  |
|   4 | `test_sha256_448bit`                         |   ✅   | SHA256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")                                  |
|   5 | `test_sha256_streaming`                      |   ✅   | Same as test_sha256_abc but using the streaming API.                                                |
|   6 | `test_hmac_sha256_tc1`                       |   ✅   | RFC 4231 Test Case 1                                                                                |
|   7 | `test_hmac_sha256_tc2`                       |   ✅   | RFC 4231 Test Case 2                                                                                |
|   8 | `test_hmac_sha256_tc3`                       |   ✅   | RFC 4231 Test Case 3                                                                                |
|   9 | `test_hmac_sha256_streaming`                 |   ✅   | Same as tc1 but via streaming API.                                                                  |
|  10 | `test_hmac_sha256_tc6_large_key`             |   ✅   | Hmac sha256 tc6 large key                                                                           |
|  11 | `test_hmac_sha512_tc1`                       |   ✅   | RFC 4231 Test Case 1: Key = 0x0b x20, Data = "Hi There".                                            |
|  12 | `test_hmac_sha512_tc2`                       |   ✅   | RFC 4231 Test Case 2: Key = "Jefe", Data = "what do ya want for nothing?".                          |
|  13 | `test_hmac_sha512_streaming`                 |   ✅   | Same as tc1 but via the streaming API (also exercises the 128-byte block boundary).                 |
|  14 | `test_hmac_sha512_tc6_large_key`             |   ✅   | Hmac sha512 tc6 large key                                                                           |
|  15 | `test_aes256ctr_encrypt`                     |   ✅   | NIST SP 800-38A, Section F.5.5                                                                      |
|  16 | `test_aes256ctr_decrypt`                     |   ✅   | AES-256-CTR decrypt is identical to encrypt.                                                        |
|  17 | `test_aes256ctr_multi_block`                 |   ✅   | NIST F.5.5 blocks 1-4 (64 bytes).                                                                   |
|  18 | `test_aes256ctr_scratch_wiped`               |   ✅   | Security model: the ephemeral AES key schedule lives in the shared crypto scratch and MUST be wiped |
|  19 | `test_bn_roundtrip`                          |   ✅   | Round-trip: bytes → pc_bignum → bytes.                                                              |
|  20 | `test_bn_cmp_equal`                          |   ✅   | Bn cmp equal                                                                                        |
|  21 | `test_bn_cmp_less`                           |   ✅   | Bn cmp less                                                                                         |
|  22 | `test_bn_cmp_greater`                        |   ✅   | Bn cmp greater                                                                                      |
|  23 | `test_bn_is_zero`                            |   ✅   | Bn is zero                                                                                          |
|  24 | `test_bn_dh_validate_rejects_zero`           |   ✅   | Bn dh validate rejects zero                                                                         |
|  25 | `test_bn_dh_validate_rejects_one`            |   ✅   | Bn dh validate rejects one                                                                          |
|  26 | `test_bn_dh_validate_accepts_two`            |   ✅   | Bn dh validate accepts two                                                                          |
|  27 | `test_expmod_exp1`                           |   ✅   | Expmod exp1                                                                                         |
|  28 | `test_expmod_exp2`                           |   ✅   | Expmod exp2                                                                                         |
|  29 | `test_expmod_exp3`                           |   ✅   | Expmod exp3                                                                                         |
|  30 | `test_expmod_commutative`                    |   ✅   | Expmod commutative                                                                                  |
|  31 | `test_rsa_pkcs1_pad_structure`               |   ✅   | The padding, not a host key: pc_rsa_sign_sw takes n and d directly, so the synthetic modulus        |
|  32 | `test_rsa_sign_verify_roundtrip`             |   ✅   | Rsa sign verify roundtrip                                                                           |
|  33 | `test_rsa_load_pkcs1_and_pkcs8_agree`        |   ✅   | Rsa load pkcs1 and pkcs8 agree                                                                      |
|  34 | `test_rsa_load_rejects_malformed`            |   ✅   | Rsa load rejects malformed                                                                          |
|  35 | `test_rsa_encode_pubkey`                     |   ✅   | Rsa encode pubkey                                                                                   |
|  36 | `test_rsa_verify_and_encode_guards`          |   ✅   | Rsa verify and encode guards                                                                        |
|  37 | `test_rsa_verify_valid_signature`            |   ✅   | Rsa verify valid signature                                                                          |
|  38 | `test_rsa_verify_rejects_tampered_signature` |   ✅   | Rsa verify rejects tampered signature                                                               |
|  39 | `test_rsa_verify_rejects_wrong_message`      |   ✅   | Rsa verify rejects wrong message                                                                    |
|  40 | `test_rsa_sha512_kat_sign_verify`            |   ✅   | A known-answer vector: the signature has to byte-match a reference produced with this exact         |
|  41 | `test_pkt_send_recv_unencrypted`             |   ✅   | Pkt send recv unencrypted                                                                           |
|  42 | `test_pkt_padding_alignment`                 |   ✅   | Packet length + padding must be multiple of 16.                                                     |
|  43 | `test_pkt_seq_increments`                    |   ✅   | Pkt seq increments                                                                                  |
|  44 | `test_pkt_disconnect_zeroes_state`           |   ✅   | Pkt disconnect zeroes state                                                                         |
|  45 | `test_pkt_encrypted_roundtrip`               |   ✅   | Pkt encrypted roundtrip                                                                             |
|  46 | `test_pkt_chacha20poly1305_roundtrip`        |   ✅   | Install a chacha20-poly1305 session with the same key both directions, so ssh_pkt_send()            |
|  47 | `test_pkt_aes256gcm_roundtrip`               |   ✅   | Install an aes256-gcm@openssh.com session with the same key/IV both directions, so ssh_pkt_send     |
|  48 | `test_pkt_aes_etm_sha256_roundtrip`          |   ✅   | Pkt aes etm sha256 roundtrip                                                                        |
|  49 | `test_pkt_aes_etm_sha512_roundtrip`          |   ✅   | Pkt aes etm sha512 roundtrip                                                                        |
|  50 | `test_pkt_encrypted_fragmented`              |   ✅   | Pkt encrypted fragmented                                                                            |
|  51 | `test_pkt_encrypted_two_packets`             |   ✅   | Pkt encrypted two packets                                                                           |
|  52 | `test_pkt_chacha_padding_and_incomplete`     |   ✅   | Pkt chacha padding and incomplete                                                                   |
|  53 | `test_pkt_etm_padding_and_incomplete`        |   ✅   | Pkt etm padding and incomplete                                                                      |
|  54 | `test_pkt_chacha_forged_rejects`             |   ✅   | Pkt chacha forged rejects                                                                           |
|  55 | `test_pkt_etm_bad_length`                    |   ✅   | Pkt etm bad length                                                                                  |
|  56 | `test_pkt_etm_forged_rejects`                |   ✅   | Pkt etm forged rejects                                                                              |
|  57 | `test_pkt_scratch_exhausted`                 |   ✅   | Pkt scratch exhausted                                                                               |
|  58 | `test_pkt_eam_forged_rejects`                |   ✅   | Pkt eam forged rejects                                                                              |
|  59 | `test_ssh_kdf_canonical_mpint_k`             |   ✅   | Ssh kdf canonical mpint k                                                                           |
|  60 | `test_ssh_kdf_extension_chain`               |   ✅   | Ssh kdf extension chain                                                                             |
|  61 | `test_keymat_wipe_out_of_range_is_noop`      |   ✅   | Keymat wipe out of range is noop                                                                    |

</details>

---

## test_ssh_auth - native_ssh_kbdint - ✅ 29 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_SSH user-authentication tests (RFC 4252): service request/accept, request_

|   # | Test                                              | Status | Description                                       |
| --: | :------------------------------------------------ | :----: | :------------------------------------------------ |
|   1 | `test_service_request_errors`                     |   ✅   | Service request errors                            |
|   2 | `test_build_response_guards`                      |   ✅   | Build response guards                             |
|   3 | `test_parse_request_truncations`                  |   ✅   | Parse request truncations                         |
|   4 | `test_pubkey_blob_parse_failures`                 |   ✅   | Pubkey blob parse failures                        |
|   5 | `test_pubkey_oversized_signed_prefix`             |   ✅   | Pubkey oversized signed prefix                    |
|   6 | `test_handle_request_index_and_parse_guards`      |   ✅   | Handle request index and parse guards             |
|   7 | `test_pubkey_without_verifier_fails`              |   ✅   | Pubkey without verifier fails                     |
|   8 | `test_pubkey_rsa_blob_type_length_and_zero_mpint` |   ✅   | Pubkey rsa blob type length and zero mpint        |
|   9 | `test_pubkey_ed25519_blob_and_siglen_rejections`  |   ✅   | Pubkey ed25519 blob and siglen rejections         |
|  10 | `test_pubkey_ecdsa_blob_rejections`               |   ✅   | Pubkey ecdsa blob rejections                      |
|  11 | `test_pubkey_ecdsa_signature_rejections`          |   ✅   | Pubkey ecdsa signature rejections                 |
|  12 | `test_pubkey_verifier_rejects_key`                |   ✅   | Pubkey verifier rejects key                       |
|  13 | `test_build_failure_partial_success_flag`         |   ✅   | Build failure partial success flag                |
|  14 | `test_service_request_accept`                     |   ✅   | Service request accept                            |
|  15 | `test_service_request_rejects_unknown`            |   ✅   | Service request rejects unknown                   |
|  16 | `test_parse_password_request`                     |   ✅   | Parse password request                            |
|  17 | `test_parse_none_request`                         |   ✅   | Parse none request                                |
|  18 | `test_handle_request_success`                     |   ✅   | Handle request success                            |
|  19 | `test_handle_request_wrong_password_fails`        |   ✅   | Handle request wrong password fails               |
|  20 | `test_handle_none_request_fails_without_auth`     |   ✅   | Handle none request fails without auth            |
|  21 | `test_handle_request_no_callback_fails`           |   ✅   | No callback installed → all credentials rejected. |
|  22 | `test_pubkey_probe_returns_pk_ok`                 |   ✅   | Pubkey probe returns pk ok                        |
|  23 | `test_pubkey_valid_signature_succeeds`            |   ✅   | Pubkey valid signature succeeds                   |
|  24 | `test_pubkey_rsa_sha512_signature_succeeds`       |   ✅   | Pubkey rsa sha512 signature succeeds              |
|  25 | `test_pubkey_ecdsa_signature_succeeds`            |   ✅   | Pubkey ecdsa signature succeeds                   |
|  26 | `test_pubkey_ed25519_valid_signature_succeeds`    |   ✅   | Pubkey ed25519 valid signature succeeds           |
|  27 | `test_pubkey_tampered_signature_fails`            |   ✅   | Pubkey tampered signature fails                   |
|  28 | `test_pubkey_unauthorized_key_fails`              |   ✅   | Pubkey unauthorized key fails                     |
|  29 | `test_aesgcm_gctr_counter_byte_carry`             |   ✅   | Aesgcm gctr counter byte carry                    |

</details>

---

## test_ssh_kbdint - native_ssh_kbdint - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_SSH keyboard-interactive authentication tests (RFC 4256): the server sends one INFO_REQUEST with a_

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_dispatch_all_switch_arms`                |   ✅   | Dispatch all switch arms                |
|   2 | `test_dispatch_guard_and_error_arms`           |   ✅   | Dispatch guard and error arms           |
|   3 | `test_kbdint_request_prompts`                  |   ✅   | Kbdint request prompts                  |
|   4 | `test_kbdint_correct_password_succeeds`        |   ✅   | Kbdint correct password succeeds        |
|   5 | `test_kbdint_wrong_password_fails`             |   ✅   | Kbdint wrong password fails             |
|   6 | `test_kbdint_response_without_request_fails`   |   ✅   | Kbdint response without request fails   |
|   7 | `test_kbdint_zero_responses_fails`             |   ✅   | Kbdint zero responses fails             |
|   8 | `test_kbdint_response_replay_fails`            |   ✅   | Kbdint response replay fails            |
|   9 | `test_methods_list_advertises_kbdint`          |   ✅   | Methods list advertises kbdint          |
|  10 | `test_kbdint_request_without_verifier_or_room` |   ✅   | Kbdint request without verifier or room |
|  11 | `test_kbdint_info_response_wire_guards`        |   ✅   | Kbdint info response wire guards        |
|  12 | `test_kbdint_dispatch_guards_and_success`      |   ✅   | Kbdint dispatch guards and success      |
|  13 | `test_kbdint_dispatch_failures_hit_the_limit`  |   ✅   | Kbdint dispatch failures hit the limit  |

</details>

---

## test_ssh_pqc - native_ssh_pqc - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_End-to-end test of the mlkem768x25519-sha256 SSH hybrid key exchange (draft-ietf-sshm-mlkem-hybrid-_

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_decaps_ref_matches_kat`                    |   ✅   | Decaps ref matches kat                    |
|   2 | `test_hybrid_negotiated`                         |   ✅   | Hybrid negotiated                         |
|   3 | `test_hybrid_absent_falls_back`                  |   ✅   | Hybrid absent falls back                  |
|   4 | `test_hybrid_kex_end_to_end`                     |   ✅   | Hybrid kex end to end                     |
|   5 | `test_kex_generate_per_method`                   |   ✅   | Kex generate per method                   |
|   6 | `test_kexinit_advertises_both_hybrids_first`     |   ✅   | Kexinit advertises both hybrids first     |
|   7 | `test_sntrup761_hybrid_kex_end_to_end`           |   ✅   | Sntrup761 hybrid kex end to end           |
|   8 | `test_classical_dh_kex_in_pqc_build`             |   ✅   | Classical dh kex in pqc build             |
|   9 | `test_hybrid_init_malformed_rejected`            |   ✅   | Hybrid init malformed rejected            |
|  10 | `test_hybrid_rejects_low_order_point_and_bad_ek` |   ✅   | Hybrid rejects low order point and bad ek |

</details>

---

## test_ssh_hardening - native_ssh_hardened - ✅ 4 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Built with PC_SSH_ALLOW_PASSWORD=0: verifies password authentication is_

|   # | Test                                                        | Status | Description                                                            |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------------------------- |
|   1 | `test_password_refused_even_with_correct_callback`          |   ✅   | Even a callback that accepts everything must not authenticate, because |
|   2 | `test_failure_advertises_publickey_only`                    |   ✅   | Failure advertises publickey only                                      |
|   3 | `test_ecdsa_direct_sign_verify_ecdh_roundtrip`              |   ✅   | Ecdsa direct sign verify ecdh roundtrip                                |
|   4 | `test_ecdsa_publickey_auth_succeeds_when_password_disabled` |   ✅   | Ecdsa publickey auth succeeds when password disabled                   |

</details>

---

## test_ssh_conn - native_ssh_conn - ✅ 26 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_SSH transport-glue test: drives a PROTO_SSH connection through the real_

|   # | Test                                                                  | Status | Description                                                    |
| --: | :-------------------------------------------------------------------- | :----: | :------------------------------------------------------------- |
|   1 | `test_conn_entrypoints_reject_unmapped_slot`                          |   ✅   | Conn entrypoints reject unmapped slot                          |
|   2 | `test_conn_outbound_arena_exhausted`                                  |   ✅   | Conn outbound arena exhausted                                  |
|   3 | `test_conn_outbound_arena_fits_payload_not_wire`                      |   ✅   | Conn outbound arena fits payload not wire                      |
|   4 | `test_conn_emit_drops_reply_on_dead_socket`                           |   ✅   | Conn emit drops reply on dead socket                           |
|   5 | `test_conn_poll_rx_foreign_slot_mapping`                              |   ✅   | Conn poll rx foreign slot mapping                              |
|   6 | `test_conn_poll_rekey_preconditions`                                  |   ✅   | Conn poll rekey preconditions                                  |
|   7 | `test_conn_accept_skips_banner_on_dead_socket`                        |   ✅   | Conn accept skips banner on dead socket                        |
|   8 | `test_conn_rx_banner_then_packet_in_separate_reads`                   |   ✅   | Conn rx banner then packet in separate reads                   |
|   9 | `test_conn_outbound_pkt_send_fails`                                   |   ✅   | Conn outbound pkt send fails                                   |
|  10 | `test_poll_rekey_emit_fails`                                          |   ✅   | Poll rekey emit fails                                          |
|  11 | `test_accept_sends_server_banner`                                     |   ✅   | Accept sends server banner                                     |
|  12 | `test_banner_then_kexinit_advances_and_replies`                       |   ✅   | Banner then kexinit advances and replies                       |
|  13 | `test_poll_triggers_server_rekey`                                     |   ✅   | Poll triggers server rekey                                     |
|  14 | `test_proto_handler_accessor`                                         |   ✅   | Proto handler accessor                                         |
|  15 | `test_proto_handler_wires_emit`                                       |   ✅   | Proto handler wires emit                                       |
|  16 | `test_send_entrypoints_reject`                                        |   ✅   | Send entrypoints reject                                        |
|  17 | `test_poll_rx_banner_guards`                                          |   ✅   | Poll rx banner guards                                          |
|  18 | `test_conn_send_close_open_channel`                                   |   ✅   | Conn send close open channel                                   |
|  19 | `test_send_channel_reject_paths`                                      |   ✅   | Send channel reject paths                                      |
|  20 | `test_accept_no_ssh_capacity`                                         |   ✅   | Accept no ssh capacity                                         |
|  21 | `test_poll_ignores_inactive_conn`                                     |   ✅   | Poll ignores inactive conn                                     |
|  22 | `test_rx_disconnect_tears_down`                                       |   ✅   | Rx disconnect tears down                                       |
|  23 | `test_rx_overlong_banner_closes`                                      |   ✅   | Rx overlong banner closes                                      |
|  24 | `test_bn_expmod_group14_hits_correction_sliver_without_overflow_limb` |   ✅   | Bn expmod group14 hits correction sliver without overflow limb |
|  25 | `test_dispatch_all_switch_arms`                                       |   ✅   | Dispatch all switch arms                                       |
|  26 | `test_dispatch_guard_and_error_arms`                                  |   ✅   | Dispatch guard and error arms                                  |

</details>

---

## test_auth - native_auth - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for HTTP Basic Authentication (per-route)._

|   # | Test                                                   | Status | Description                                                               |
| --: | :----------------------------------------------------- | :----: | :------------------------------------------------------------------------ |
|   1 | `test_unprotected_route_fires_handler`                 |   ✅   | Unprotected route fires handler                                           |
|   2 | `test_protected_route_no_header_returns_401`           |   ✅   | Protected route no header returns 401                                     |
|   3 | `test_protected_route_wrong_password_returns_401`      |   ✅   | Protected route wrong password returns 401                                |
|   4 | `test_protected_route_wrong_username_returns_401`      |   ✅   | Protected route wrong username returns 401                                |
|   5 | `test_protected_route_valid_credentials_fires_handler` |   ✅   | Protected route valid credentials fires handler                           |
|   6 | `test_401_includes_www_authenticate_header`            |   ✅   | 401 includes www authenticate header                                      |
|   7 | `test_non_basic_scheme_returns_401`                    |   ✅   | Non basic scheme returns 401                                              |
|   8 | `test_credentials_without_colon_returns_401`           |   ✅   | Credentials without colon returns 401                                     |
|   9 | `test_protected_and_unprotected_routes_coexist`        |   ✅   | Protected and unprotected routes coexist                                  |
|  10 | `test_auth_route_returns_404_for_wrong_path`           |   ✅   | Auth route returns 404 for wrong path                                     |
|  11 | `test_auth_checked_per_method`                         |   ✅   | HttpRoute only handles POST; a GET to that path is 405 Method Not Allowed |
|  12 | `test_basic_auth_same_length_wrong_credentials`        |   ✅   | Basic auth same length wrong credentials                                  |
|  13 | `test_basic_auth_invalid_base64_rejected`              |   ✅   | Basic auth invalid base64 rejected                                        |
|  14 | `test_unauth_challenge_cors_and_head`                  |   ✅   | Unauth challenge cors and head                                            |
|  15 | `test_unauth_challenge_on_dead_connection`             |   ✅   | Unauth challenge on dead connection                                       |
|  16 | `test_digest_field_parser_boundaries`                  |   ✅   | Digest field parser boundaries                                            |
|  17 | `test_digest_token_values_and_truncation`              |   ✅   | Digest token values and truncation                                        |
|  18 | `test_digest_nonce_shape_and_mac`                      |   ✅   | Digest nonce shape and mac                                                |
|  19 | `test_digest_missing_field_rejected`                   |   ✅   | Digest missing field rejected                                             |
|  20 | `test_digest_uri_includes_query_string`                |   ✅   | Digest uri includes query string                                          |
|  21 | `stress_auth_50_valid_requests`                        |   ✅   | Stress - Auth 50 valid requests                                           |
|  22 | `stress_auth_50_invalid_requests`                      |   ✅   | Stress - Auth 50 invalid requests                                         |

</details>

---

## test_file_serving - native_file_serving - ✅ 26 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for serve_file()._

|   # | Test                                                  | Status | Description                                                                  |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------------------------------------- |
|   1 | `test_missing_file_returns_404`                       |   ✅   | Missing file returns 404                                                     |
|   2 | `test_existing_file_returns_200`                      |   ✅   | Existing file returns 200                                                    |
|   3 | `test_response_includes_content_type_html`            |   ✅   | Response includes content type html                                          |
|   4 | `test_response_includes_content_type_js`              |   ✅   | Response includes content type js                                            |
|   5 | `test_content_length_matches_file_size`               |   ✅   | Content length matches file size                                             |
|   6 | `test_file_body_is_sent`                              |   ✅   | File body is sent                                                            |
|   7 | `test_empty_file_returns_200_with_zero_length`        |   ✅   | Empty file returns 200 with zero length                                      |
|   8 | `test_large_file_body_fully_sent`                     |   ✅   | A body far larger than one send-buffer window: the cross-loop file pump must |
|   9 | `test_serve_file_does_not_affect_other_routes`        |   ✅   | Serve file does not affect other routes                                      |
|  10 | `test_multiple_content_types`                         |   ✅   | Multiple content types                                                       |
|  11 | `test_serve_static_root_join_variants`                |   ✅   | Serve static root join variants                                              |
|  12 | `test_serve_static_empty_prefix_mount`                |   ✅   | Serve static empty prefix mount                                              |
|  13 | `test_serve_static_directory_and_overlong_path`       |   ✅   | Serve static directory and overlong path                                     |
|  14 | `test_serve_static_gzip_negotiation_misses`           |   ✅   | Serve static gzip negotiation misses                                         |
|  15 | `test_serve_static_head_and_cors_headers`             |   ✅   | Serve static head and cors headers                                           |
|  16 | `test_serve_static_inm_non_matching_forms`            |   ✅   | Serve static inm non matching forms                                          |
|  17 | `test_file_send_pump_connection_lost_midtransfer`     |   ✅   | File send pump connection lost midtransfer                                   |
|  18 | `test_inm_leading_ows_still_matches`                  |   ✅   | Inm leading ows still matches                                                |
|  19 | `test_inm_list_separators_reach_later_tag`            |   ✅   | Inm list separators reach later tag                                          |
|  20 | `test_conditional_304_carries_cors_block`             |   ✅   | Conditional 304 carries cors block                                           |
|  21 | `test_serve_static_overlong_prefix_registers_nothing` |   ✅   | Serve static overlong prefix registers nothing                               |
|  22 | `test_serve_static_param_mount_shorter_than_pattern`  |   ✅   | Serve static param mount shorter than pattern                                |
|  23 | `test_serve_static_trailing_slash_root_bare_prefix`   |   ✅   | Serve static trailing slash root bare prefix                                 |
|  24 | `test_serve_static_joined_path_overflow_is_404`       |   ✅   | Serve static joined path overflow is 404                                     |
|  25 | `stress_serve_file_50_requests`                       |   ✅   | Stress - Serve file 50 requests                                              |
|  26 | `stress_alternate_missing_and_found`                  |   ✅   | Stress - Alternate missing and found                                         |

</details>

---

## test_multipart - native_multipart - ✅ 33 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for multipart/form-data parser (multipart.cpp)._

|   # | Test                                             | Status | Description                                                           |
| --: | :----------------------------------------------- | :----: | :-------------------------------------------------------------------- |
|   1 | `test_no_content_type_returns_false`             |   ✅   | No content type returns false                                         |
|   2 | `test_no_boundary_in_content_type_returns_false` |   ✅   | No boundary in content type returns false                             |
|   3 | `test_body_missing_delimiter_returns_false`      |   ✅   | Body missing delimiter returns false                                  |
|   4 | `test_single_text_field_parsed`                  |   ✅   | Single text field parsed                                              |
|   5 | `test_two_text_fields_parsed`                    |   ✅   | Two text fields parsed                                                |
|   6 | `test_three_text_fields_parsed`                  |   ✅   | Three text fields parsed                                              |
|   7 | `test_file_upload_part`                          |   ✅   | File upload part                                                      |
|   8 | `test_file_upload_with_text_field`               |   ✅   | File upload with text field                                           |
|   9 | `test_get_field_found`                           |   ✅   | Get field found                                                       |
|  10 | `test_get_field_not_found_returns_null`          |   ✅   | Get field not found returns null                                      |
|  11 | `test_get_field_multiple_fields`                 |   ✅   | Get field multiple fields                                             |
|  12 | `test_data_len_is_correct`                       |   ✅   | Data len is correct                                                   |
|  13 | `test_max_parts_captured`                        |   ✅   | Build exactly MAX_MULTIPART_PARTS + 1 parts; only MAX_MULTIPART_PARTS |
|  14 | `test_empty_field_value`                         |   ✅   | Empty field value                                                     |
|  15 | `test_part_without_filename_has_null_filename`   |   ✅   | Part without filename has null filename                               |
|  16 | `test_part_without_content_type_has_null_type`   |   ✅   | Part without content type has null type                               |
|  17 | `test_long_boundary_string`                      |   ✅   | MAX_VAL_LEN=48 limits the stored Content-Type value.                  |
|  18 | `stress_parse_100_requests`                      |   ✅   | Stress - Parse 100 requests                                           |
|  19 | `stress_get_field_100_lookups`                   |   ✅   | Stress - Get field 100 lookups                                        |
|  20 | `test_binary_part_not_truncated`                 |   ✅   | Binary part not truncated                                             |
|  21 | `test_quoted_boundary`                           |   ✅   | Quoted boundary                                                       |
|  22 | `test_empty_boundary_returns_false`              |   ✅   | Empty boundary returns false                                          |
|  23 | `test_malformed_disposition_values`              |   ✅   | unquoted name= value                                                  |
|  24 | `test_body_shorter_than_delimiter`               |   ✅   | Body shorter than delimiter                                           |
|  25 | `test_truncated_part_fails_closed`               |   ✅   | Truncated part fails closed                                           |
|  26 | `test_boundary_stops_at_semicolon_or_space`      |   ✅   | Boundary stops at semicolon or space                                  |
|  27 | `test_empty_multipart_body_has_no_parts`         |   ✅   | Empty multipart body has no parts                                     |
|  28 | `test_lone_cr_after_delimiter_fails_closed`      |   ✅   | Lone cr after delimiter fails closed                                  |
|  29 | `test_unrecognized_header_line_yields_null_name` |   ✅   | Unrecognized header line yields null name                             |
|  30 | `test_part_data_ends_exactly_at_buffer_end`      |   ✅   | Part data ends exactly at buffer end                                  |
|  31 | `test_content_disposition_no_space_after_colon`  |   ✅   | Content disposition no space after colon                              |
|  32 | `test_delimiter_with_nothing_after_it`           |   ✅   | Delimiter with nothing after it                                       |
|  33 | `test_lone_cr_after_data_delimiter_fails_closed` |   ✅   | Lone cr after data delimiter fails closed                             |

</details>

---

## test_dispatch - native_dispatch - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Dispatch-level RFC 7231 compliance:_

|   # | Test                                                     | Status | Description                                                                                            |
| --: | :------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------------------- |
|   1 | `test_method_mismatch_returns_405`                       |   ✅   | Method mismatch returns 405                                                                            |
|   2 | `test_405_includes_allow_header`                         |   ✅   | 405 includes allow header                                                                              |
|   3 | `test_405_allow_lists_all_methods_for_path`              |   ✅   | 405 allow lists all methods for path                                                                   |
|   4 | `test_unknown_path_still_404_not_405`                    |   ✅   | Unknown path still 404 not 405                                                                         |
|   5 | `test_unknown_method_returns_501`                        |   ✅   | Unknown method returns 501                                                                             |
|   6 | `test_unknown_method_not_treated_as_get`                 |   ✅   | A bogus method must NOT run the GET handler (security: no method spoofing).                            |
|   7 | `test_head_runs_get_handler_without_body`                |   ✅   | Head runs get handler without body                                                                     |
|   8 | `test_get_route_advertises_head_in_allow`                |   ✅   | Get route advertises head in allow                                                                     |
|   9 | `test_head_on_post_only_route_405`                       |   ✅   | Head on post only route 405                                                                            |
|  10 | `test_http_parse_skips_ws_upgraded_slot`                 |   ✅   | Http parse skips ws upgraded slot                                                                      |
|  11 | `test_correct_method_still_dispatches`                   |   ✅   | Correct method still dispatches                                                                        |
|  12 | `test_slowloris_incomplete_request_reaped_past_deadline` |   ✅   | Slowloris incomplete request reaped past deadline                                                      |
|  13 | `test_incomplete_request_survives_before_deadline`       |   ✅   | Incomplete request survives before deadline                                                            |
|  14 | `test_completed_slow_request_not_reaped`                 |   ✅   | A request that arrives slowly but COMPLETES is dispatched normally and never 408'd, even when a later  |
|  15 | `test_streaming_body_upload_not_reaped_past_deadline`    |   ✅   | The deadline is header-scoped (nginx client_header_timeout): a legitimate slow body sits in PARSE_BODY |

</details>

---

## test_application - native_application - ✅ 95 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit, stress, and race-condition tests for Layer 7 (Application)._

|   # | Test                                                       | Status | Description                                                                               |
| --: | :--------------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_response_headers_that_do_not_fit_are_refused`        |   ✅   | (a) The status line alone overflows the header buffer.                                    |
|   2 | `test_restart_and_stop`                                    |   ✅   | Before any listener, restart() forwards the no-listeners error (no stop()/proto_begin()). |
|   3 | `test_route_registration_variants_table_full`              |   ✅   | Route registration variants table full                                                    |
|   4 | `test_send_family_slot_and_conn_gone_guards`               |   ✅   | Send family slot and conn gone guards                                                     |
|   5 | `test_send_binary_body_with_nul`                           |   ✅   | Send binary body with nul                                                                 |
|   6 | `test_redirect_response_and_code_normalization`            |   ✅   | Redirect response and code normalization                                                  |
|   7 | `test_request_error_paths_te_method_ws`                    |   ✅   | Request error paths te method ws                                                          |
|   8 | `test_ws_sse_upgrade_failure_paths`                        |   ✅   | Ws sse upgrade failure paths                                                              |
|   9 | `test_handler_reads_body`                                  |   ✅   | Handler reads body                                                                        |
|  10 | `test_handler_reads_query_param`                           |   ✅   | Handler reads query param                                                                 |
|  11 | `test_handler_reads_header`                                |   ✅   | Handler reads header                                                                      |
|  12 | `test_wildcard_before_exact_wildcard_wins`                 |   ✅   | Wildcard before exact wildcard wins                                                       |
|  13 | `test_fn_on_registers_and_dispatches`                      |   ✅   | Fn on registers and dispatches                                                            |
|  14 | `test_fn_on_path_copied_null_terminated`                   |   ✅   | A path of exactly MAX_PATH_LEN-1 chars must not overflow the route buffer.                |
|  15 | `test_fn_on_table_full_extra_routes_dropped`               |   ✅   | Fill the table; on() beyond MAX_ROUTES must silently drop                                 |
|  16 | `test_fn_on_same_path_different_methods_are_distinct`      |   ✅   | Fn on same path different methods are distinct                                            |
|  17 | `test_fn_on_not_found_called_when_no_match`                |   ✅   | Fn on not found called when no match                                                      |
|  18 | `test_fn_on_not_found_not_called_when_match_exists`        |   ✅   | Fn on not found not called when match exists                                              |
|  19 | `test_fn_set_cors_options_preflight_clears_slot`           |   ✅   | Fn set cors options preflight clears slot                                                 |
|  20 | `test_fn_set_cors_empty_string_disables`                   |   ✅   | Fn set cors empty string disables                                                         |
|  21 | `test_wrong_method_does_not_match`                         |   ✅   | Wrong method does not match                                                               |
|  22 | `test_wrong_path_does_not_match`                           |   ✅   | Wrong path does not match                                                                 |
|  23 | `test_all_http_methods_dispatched`                         |   ✅   | All http methods dispatched                                                               |
|  24 | `test_root_path_matches_exactly`                           |   ✅   | Root path matches exactly                                                                 |
|  25 | `test_root_path_does_not_match_subpath`                    |   ✅   | Root path does not match subpath                                                          |
|  26 | `test_wildcard_matches_any_suffix`                         |   ✅   | Wildcard matches any suffix                                                               |
|  27 | `test_wildcard_does_not_match_unrelated_prefix`            |   ✅   | Wildcard does not match unrelated prefix                                                  |
|  28 | `test_exact_route_wins_when_registered_first`              |   ✅   | Exact route wins when registered first                                                    |
|  29 | `test_slot_not_stuck_in_complete_after_handle`             |   ✅   | Slot not stuck in complete after handle                                                   |
|  30 | `test_parse_error_slot_auto_reset`                         |   ✅   | Parse error slot auto reset                                                               |
|  31 | `stress_last_route_dispatched_in_full_table`               |   ✅   | Stress - Last route dispatched in full table                                              |
|  32 | `stress_sequential_requests_no_state_leak`                 |   ✅   | Stress - Sequential requests no state leak                                                |
|  33 | `stress_all_slots_dispatched_simultaneously`               |   ✅   | Stress - All slots dispatched simultaneously                                              |
|  34 | `stress_wildcard_matches_many_paths`                       |   ✅   | Stress - Wildcard matches many paths                                                      |
|  35 | `stress_handle_with_no_complete_slots_is_nop`              |   ✅   | Stress - Handle with no complete slots is nop                                             |
|  36 | `race_slot_complete_between_handle_calls`                  |   ✅   | Race - Slot complete between handle calls                                                 |
|  37 | `race_conn_freed_after_parse_complete`                     |   ✅   | Race - Conn freed after parse complete                                                    |
|  38 | `race_double_handle_no_double_dispatch`                    |   ✅   | Race - Double handle no double dispatch                                                   |
|  39 | `race_error_and_valid_slot_in_same_handle`                 |   ✅   | Race - Error and valid slot in same handle                                                |
|  40 | `race_callback_manually_resets_slot`                       |   ✅   | Race - Callback manually resets slot                                                      |
|  41 | `test_uri_too_long_auto_resets_slot`                       |   ✅   | Overflow the path buffer - handle() should send 414 and free the slot                     |
|  42 | `test_transfer_encoding_chunked_is_501`                    |   ✅   | A request advertising Transfer-Encoding must be rejected with 501                         |
|  43 | `test_transfer_encoding_identity_is_501`                   |   ✅   | Even "identity" is rejected - we advertise no TE support at all                           |
|  44 | `test_redirect_emits_location_and_status`                  |   ✅   | Redirect emits location and status                                                        |
|  45 | `test_redirect_invalid_code_defaults_to_302`               |   ✅   | Redirect invalid code defaults to 302                                                     |
|  46 | `test_mime_type_detection`                                 |   ✅   | Mime type detection                                                                       |
|  47 | `test_serve_static_file_and_mime`                          |   ✅   | Serve static file and mime                                                                |
|  48 | `test_serve_static_wildcard_and_route_full`                |   ✅   | Serve static wildcard and route full                                                      |
|  49 | `test_response_header_cookie_guards`                       |   ✅   | Response header cookie guards                                                             |
|  50 | `test_serve_static_index_fallback`                         |   ✅   | Serve static index fallback                                                               |
|  51 | `test_serve_static_gzip_when_accepted`                     |   ✅   | Serve static gzip when accepted                                                           |
|  52 | `test_serve_static_no_gzip_when_not_accepted`              |   ✅   | Serve static no gzip when not accepted                                                    |
|  53 | `test_serve_static_traversal_not_leaked`                   |   ✅   | Serve static traversal not leaked                                                         |
|  54 | `test_serve_static_missing_is_404`                         |   ✅   | Serve static missing is 404                                                               |
|  55 | `test_serve_static_etag_conditional_get`                   |   ✅   | Serve static etag conditional get                                                         |
|  56 | `test_serve_static_inm_star_list_weak`                     |   ✅   | Serve static inm star list weak                                                           |
|  57 | `test_serve_static_last_modified_conditional_get`          |   ✅   | Serve static last modified conditional get                                                |
|  58 | `test_serve_static_ims_field_comparisons`                  |   ✅   | Serve static ims field comparisons                                                        |
|  59 | `test_serve_static_no_timestamp`                           |   ✅   | Serve static no timestamp                                                                 |
|  60 | `test_serve_static_if_modified_since_malformed`            |   ✅   | Serve static if modified since malformed                                                  |
|  61 | `test_serve_static_cache_control`                          |   ✅   | Serve static cache control                                                                |
|  62 | `test_request_log_hook_fires`                              |   ✅   | Request log hook fires                                                                    |
|  63 | `test_stats_endpoint_emits_json`                           |   ✅   | Stats endpoint emits json                                                                 |
|  64 | `test_status_text_reason_phrases`                          |   ✅   | Status text reason phrases                                                                |
|  65 | `test_allow_header_lists_methods`                          |   ✅   | Allow header lists methods                                                                |
|  66 | `test_listen_and_begin`                                    |   ✅   | proto_begin() before any listen() -> no-listeners error, no side effects.                 |
|  67 | `test_begin_port_convenience`                              |   ✅   | Begin port convenience                                                                    |
|  68 | `test_ws_send_api`                                         |   ✅   | Ws send api                                                                               |
|  69 | `test_metrics_emits_prometheus`                            |   ✅   | Metrics emits prometheus                                                                  |
|  70 | `test_stats_counters_ignore_sub_200_status`                |   ✅   | Stats counters ignore sub 200 status                                                      |
|  71 | `test_response_trailer_cors_block_and_null_disable`        |   ✅   | Response trailer cors block and null disable                                              |
|  72 | `test_cache_control_null_clears_header`                    |   ✅   | Cache control null clears header                                                          |
|  73 | `test_empty_route_pattern_matches_nothing`                 |   ✅   | Empty route pattern matches nothing                                                       |
|  74 | `test_path_param_capture_limits`                           |   ✅   | Path param capture limits                                                                 |
|  75 | `test_path_param_segment_mismatches`                       |   ✅   | Path param segment mismatches                                                             |
|  76 | `test_worker_owner_filter_skips_foreign_slot`              |   ✅   | Worker owner filter skips foreign slot                                                    |
|  77 | `test_slot_poll_requires_registered_handler_with_poll`     |   ✅   | Slot poll requires registered handler with poll                                           |
|  78 | `test_entity_too_large_auto_413`                           |   ✅   | Entity too large auto 413                                                                 |
|  79 | `test_allow_header_dedupes_repeated_method`                |   ✅   | Allow header dedupes repeated method                                                      |
|  80 | `test_error_close_head_and_dead_connection`                |   ✅   | Error close head and dead connection                                                      |
|  81 | `test_transfer_encoding_on_semantic_ingress_is_501`        |   ✅   | Transfer encoding on semantic ingress is 501                                              |
|  82 | `test_static_mount_rejects_non_get_methods`                |   ✅   | Static mount rejects non get methods                                                      |
|  83 | `test_send_null_payload_and_slot_bounds`                   |   ✅   | Send null payload and slot bounds                                                         |
|  84 | `test_send_body_framing_paths`                             |   ✅   | HEAD: headers only, but Content-Length still describes the would-be body.                 |
|  85 | `test_send_empty_and_redirect_dead_connection_guards`      |   ✅   | Send empty and redirect dead connection guards                                            |
|  86 | `test_send_template_placeholder_edges`                     |   ✅   | Send template placeholder edges                                                           |
|  87 | `test_send_chunked_without_source`                         |   ✅   | Send chunked without source                                                               |
|  88 | `test_chunked_pump_small_window_and_connection_lost`       |   ✅   | Chunked pump small window and connection lost                                             |
|  89 | `test_response_header_null_value_empty_attrs_and_overflow` |   ✅   | Response header null value empty attrs and overflow                                       |
|  90 | `test_mime_type_extension_edges`                           |   ✅   | Mime type extension edges                                                                 |
|  91 | `test_ws_upgrade_without_connect_handler`                  |   ✅   | Ws upgrade without connect handler                                                        |
|  92 | `test_ws_dispatch_without_message_or_close_handler`        |   ✅   | Ws dispatch without message or close handler                                              |
|  93 | `test_ws_upgrade_handshake_gate`                           |   ✅   | Ws upgrade handshake gate                                                                 |
|  94 | `test_ws_send_api_inactive_error_state_and_dead_slot`      |   ✅   | Ws send api inactive error state and dead slot                                            |
|  95 | `test_upgrade_entry_points_on_dead_slot`                   |   ✅   | Upgrade entry points on dead slot                                                         |

</details>

---

## test_response_headers - native_response_headers - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for custom response headers and cookies:_

|   # | Test                                       | Status | Description                                                                                  |
| --: | :----------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_ntp_host_seam_accessors`             |   ✅   | Host build: begin() is a no-op returning false; synced()/epoch() reflect the injected epoch. |
|   2 | `test_date_header_emitted_when_time_set`   |   ✅   | Date header emitted when time set                                                            |
|   3 | `test_date_header_omitted_when_clockless`  |   ✅   | Date header omitted when clockless                                                           |
|   4 | `test_single_custom_header_present`        |   ✅   | Single custom header present                                                                 |
|   5 | `test_multiple_custom_headers_present`     |   ✅   | Multiple custom headers present                                                              |
|   6 | `test_set_cookie_basic`                    |   ✅   | Set cookie basic                                                                             |
|   7 | `test_set_cookie_with_attrs`               |   ✅   | Set cookie with attrs                                                                        |
|   8 | `test_custom_header_on_send_empty`         |   ✅   | Custom header on send empty                                                                  |
|   9 | `test_custom_header_on_redirect`           |   ✅   | Custom header on redirect                                                                    |
|  10 | `test_headers_do_not_leak_across_requests` |   ✅   | Headers do not leak across requests                                                          |
|  11 | `test_clear_response_headers`              |   ✅   | Clear response headers                                                                       |
|  12 | `test_oversized_header_dropped_whole`      |   ✅   | Oversized header dropped whole                                                               |

</details>

---

## test_form_params - native_form_params - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for http_get_form(): application/x-www-form-urlencoded body_

|   # | Test                                   | Status | Description                     |
| --: | :------------------------------------- | :----: | :------------------------------ |
|   1 | `test_form_fields_parsed`              |   ✅   | Form fields parsed              |
|   2 | `test_form_missing_key_returns_false`  |   ✅   | Form missing key returns false  |
|   3 | `test_form_empty_value`                |   ✅   | Form empty value                |
|   4 | `test_form_wrong_content_type_ignored` |   ✅   | Form wrong content type ignored |
|   5 | `test_form_value_truncated_to_buffer`  |   ✅   | Form value truncated to buffer  |

</details>

---

## test_path_params - native_path_params - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for `:name` path parameters and http_get_param()._

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_single_param_captured`            |   ✅   | Single param captured            |
|   2 | `test_multiple_params_captured`         |   ✅   | Multiple params captured         |
|   3 | `test_missing_param_returns_null`       |   ✅   | Missing param returns null       |
|   4 | `test_literal_segment_mismatch_404`     |   ✅   | Literal segment mismatch 404     |
|   5 | `test_extra_segment_does_not_match`     |   ✅   | Extra segment does not match     |
|   6 | `test_empty_param_value_does_not_match` |   ✅   | Empty param value does not match |
|   7 | `test_exact_route_still_matches`        |   ✅   | Exact route still matches        |
|   8 | `test_param_route_wrong_method_405`     |   ✅   | Param route wrong method 405     |

</details>

---

## test_digest_auth - native_digest_auth - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for HTTP Digest authentication (RFC 7616, SHA-256, qop=auth)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_challenge_is_digest_sha256`             |   ✅   | Challenge is digest sha256             |
|   2 | `test_valid_digest_authenticates`             |   ✅   | Valid digest authenticates             |
|   3 | `test_wrong_password_rejected`                |   ✅   | Wrong password rejected                |
|   4 | `test_bad_nonce_rejected`                     |   ✅   | Bad nonce rejected                     |
|   5 | `test_wrong_username_rejected`                |   ✅   | Wrong username rejected                |
|   6 | `test_wrong_qop_rejected`                     |   ✅   | Wrong qop rejected                     |
|   7 | `test_missing_response_field_rejected`        |   ✅   | Missing response field rejected        |
|   8 | `test_basic_scheme_on_digest_route_rejected`  |   ✅   | Basic scheme on digest route rejected  |
|   9 | `test_uri_mismatch_rejected`                  |   ✅   | Uri mismatch rejected                  |
|  10 | `test_nonce_is_stateless_timestamped`         |   ✅   | Nonce is stateless timestamped         |
|  11 | `test_stale_nonce_triggers_transparent_retry` |   ✅   | Stale nonce triggers transparent retry |

</details>

---

## test_digest_vectors - native_digest_vectors - ✅ 4 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Independent-oracle regression test for the Digest-auth math (RFC 7616,_

|   # | Test                            | Status | Description              |
| --: | :------------------------------ | :----: | :----------------------- |
|   1 | `test_sha256_fips_kats`         |   ✅   | Sha256 fips kats         |
|   2 | `test_ha1_matches_openssl`      |   ✅   | Ha1 matches openssl      |
|   3 | `test_ha2_matches_openssl`      |   ✅   | Ha2 matches openssl      |
|   4 | `test_response_matches_openssl` |   ✅   | Response matches openssl |

</details>

---

## test_template - native_template - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for send_template() {{name}} placeholder substitution._

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_basic_substitution`                  |   ✅   | Basic substitution                  |
|   2 | `test_multiple_placeholders`               |   ✅   | Multiple placeholders               |
|   3 | `test_unknown_placeholder_is_empty`        |   ✅   | Unknown placeholder is empty        |
|   4 | `test_unterminated_placeholder_is_literal` |   ✅   | Unterminated placeholder is literal |
|   5 | `test_null_resolver_empties_all`           |   ✅   | Null resolver empties all           |
|   6 | `test_head_suppresses_body_keeps_length`   |   ✅   | Head suppresses body keeps length   |

</details>

---

## test_middleware - native_middleware - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the middleware chain (use()) and the built-in rate limiter_

|   # | Test                                          | Status | Description                                                                |
| --: | :-------------------------------------------- | :----: | :------------------------------------------------------------------------- |
|   1 | `test_middleware_runs_then_handler`           |   ✅   | Middleware runs then handler                                               |
|   2 | `test_middleware_runs_for_unmatched_route`    |   ✅   | No route registered -> 404, but the middleware still observes the request. |
|   3 | `test_middleware_can_inject_response_header`  |   ✅   | Middleware can inject response header                                      |
|   4 | `test_middleware_halt_short_circuits_handler` |   ✅   | Middleware halt short circuits handler                                     |
|   5 | `test_middleware_runs_in_registration_order`  |   ✅   | Middleware runs in registration order                                      |
|   6 | `test_use_respects_capacity_cap`              |   ✅   | Register more than MAX_MIDDLEWARE; extras are dropped, none crash.         |
|   7 | `test_rate_limit_allows_then_rejects`         |   ✅   | Rate limit allows then rejects                                             |
|   8 | `test_rate_limit_window_resets`               |   ✅   | Rate limit window resets                                                   |
|   9 | `test_rate_limit_disabled_by_default`         |   ✅   | Rate limit disabled by default                                             |
|  10 | `test_use_rejects_null_middleware`            |   ✅   | Use rejects null middleware                                                |
|  11 | `test_rate_limit_zero_window_disables`        |   ✅   | Rate limit zero window disables                                            |

</details>

---

## test_chunked - native_chunked - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for send_chunked(, NULL) / ChunkedResponse streaming responses._

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_hex_u32_size_line`                          |   ✅   | Hex u32 size line                          |
|   2 | `test_chunked_source_overreport_clamped`          |   ✅   | Chunked source overreport clamped          |
|   3 | `test_chunked_backpressure_resumes_across_polls`  |   ✅   | Chunked backpressure resumes across polls  |
|   4 | `test_headers_announce_chunked_no_content_length` |   ✅   | Headers announce chunked no content length |
|   5 | `test_single_chunk_framing`                       |   ✅   | Single chunk framing                       |
|   6 | `test_multiple_chunks_in_order`                   |   ✅   | Multiple chunks in order                   |
|   7 | `test_printf_chunk`                               |   ✅   | Printf chunk                               |
|   8 | `test_single_piece_then_terminator`               |   ✅   | Single piece then terminator               |
|   9 | `test_empty_body_is_just_terminator`              |   ✅   | Empty body is just terminator              |
|  10 | `test_large_chunked_body_not_truncated`           |   ✅   | Large chunked body not truncated           |
|  11 | `test_head_sends_headers_only`                    |   ✅   | Head sends headers only                    |
|  12 | `test_custom_header_injected_into_chunked`        |   ✅   | Custom header injected into chunked        |
|  13 | `test_log_hook_reports_total_body_length`         |   ✅   | Log hook reports total body length         |
|  14 | `test_http10_falls_back_to_close_delimited`       |   ✅   | Http10 falls back to close delimited       |
|  15 | `test_http10_large_body_not_truncated`            |   ✅   | Http10 large body not truncated            |

</details>

---

## test_json - native_json - ✅ 49 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the zero-heap JSON helper: pc_json_writer (serialization) and the_

|   # | Test                                                               | Status | Description                                                                    |
| --: | :----------------------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_reader_non_object_and_bad_member`                            |   ✅   | Reader non object and bad member                                               |
|   2 | `test_reader_int_rejects_string_and_nondigits`                     |   ✅   | Reader int rejects string and nondigits                                        |
|   3 | `test_reader_unicode_escape_invalid_and_wide`                      |   ✅   | Reader unicode escape invalid and wide                                         |
|   4 | `test_writer_simple_object`                                        |   ✅   | Writer simple object                                                           |
|   5 | `test_writer_nested_and_array`                                     |   ✅   | Writer nested and array                                                        |
|   6 | `test_writer_value_types`                                          |   ✅   | Writer value types                                                             |
|   7 | `test_writer_escapes_strings`                                      |   ✅   | Writer escapes strings                                                         |
|   8 | `test_writer_control_char_unicode_escape`                          |   ✅   | Writer control char unicode escape                                             |
|   9 | `test_writer_overflow_sets_not_ok_and_stays_terminated`            |   ✅   | Writer overflow sets not ok and stays terminated                               |
|  10 | `test_writer_depth_overflow_sets_not_ok`                           |   ✅   | Writer depth overflow sets not ok                                              |
|  11 | `test_reader_get_string`                                           |   ✅   | Reader get string                                                              |
|  12 | `test_reader_get_int`                                              |   ✅   | Reader get int                                                                 |
|  13 | `test_reader_get_bool`                                             |   ✅   | Reader get bool                                                                |
|  14 | `test_reader_only_matches_top_level_key`                           |   ✅   | "x" exists both nested and at top level; the top-level one must win.           |
|  15 | `test_reader_missing_key`                                          |   ✅   | Reader missing key                                                             |
|  16 | `test_reader_type_mismatch`                                        |   ✅   | "name" is a string, not an int or bool.                                        |
|  17 | `test_reader_unescapes_value`                                      |   ✅   | Reader unescapes value                                                         |
|  18 | `test_reader_unicode_escape_to_byte`                               |   ✅   | Reader unicode escape to byte                                                  |
|  19 | `test_reader_truncates_to_capacity`                                |   ✅   | Reader truncates to capacity                                                   |
|  20 | `test_reader_negative_int`                                         |   ✅   | Reader negative int                                                            |
|  21 | `test_writer_null_and_remaining_escapes`                           |   ✅   | Writer null and remaining escapes                                              |
|  22 | `test_reader_null_guards`                                          |   ✅   | Reader null guards                                                             |
|  23 | `test_reader_all_escapes`                                          |   ✅   | Reader all escapes                                                             |
|  24 | `test_reader_unicode_hex_case`                                     |   ✅   | Reader unicode hex case                                                        |
|  25 | `test_reader_unicode_utf8_multibyte`                               |   ✅   | U+20AC EURO SIGN -> 3-byte UTF-8 E2 82 AC.                                     |
|  26 | `test_reader_unicode_surrogate_edges`                              |   ✅   | Reader unicode surrogate edges                                                 |
|  27 | `test_reader_false_bool`                                           |   ✅   | Reader false bool                                                              |
|  28 | `test_reader_malformed`                                            |   ✅   | Reader malformed                                                               |
|  29 | `test_writer_null_buffer_and_zero_capacity`                        |   ✅   | Writer null buffer and zero capacity                                           |
|  30 | `test_reader_whitespace_between_tokens`                            |   ✅   | Reader whitespace between tokens                                               |
|  31 | `test_reader_get_str_on_non_string_value`                          |   ✅   | Reader get str on non string value                                             |
|  32 | `test_reader_null_key_guard`                                       |   ✅   | Reader null key guard                                                          |
|  33 | `test_reader_skips_unterminated_string_with_trailing_backslash`    |   ✅   | Reader skips unterminated string with trailing backslash                       |
|  34 | `test_reader_get_str_trailing_backslash_no_escape`                 |   ✅   | Reader get str trailing backslash no escape                                    |
|  35 | `test_reader_get_str_unterminated_value`                           |   ✅   | Reader get str unterminated value                                              |
|  36 | `test_reader_skips_array_and_doubly_nested_value`                  |   ✅   | Reader skips array and doubly nested value                                     |
|  37 | `test_reader_malformed_primitive_terminators`                      |   ✅   | Reader malformed primitive terminators                                         |
|  38 | `test_reader_truncated_member_name`                                |   ✅   | Reader truncated member name                                                   |
|  39 | `test_reader_trailing_comma_then_end`                              |   ✅   | Reader trailing comma then end                                                 |
|  40 | `test_reader_unicode_hex_lowercase_out_of_range`                   |   ✅   | Reader unicode hex lowercase out of range                                      |
|  41 | `test_reader_unicode_escape_nothing_after_u`                       |   ✅   | Reader unicode escape nothing after u                                          |
|  42 | `test_reader_unicode_escape_three_digits_then_end`                 |   ✅   | Reader unicode escape three digits then end                                    |
|  43 | `test_reader_unicode_high_surrogate_followed_by_non_u_escape`      |   ✅   | Reader unicode high surrogate followed by non u escape                         |
|  44 | `test_reader_unicode_high_surrogate_followed_by_non_low_surrogate` |   ✅   | Codepoint 0x0041 ('A') is well below the low-surrogate range (0xDC00..0xDFFF). |
|  45 | `test_reader_unicode_above_surrogate_range_standalone`             |   ✅   | Reader unicode above surrogate range standalone                                |
|  46 | `test_reader_bool_terminators`                                     |   ✅   | Reader bool terminators                                                        |
|  47 | `test_reader_unicode_escape_one_and_two_digits_then_end`           |   ✅   | Reader unicode escape one and two digits then end                              |
|  48 | `test_reader_skips_primitive_terminated_by_close_brace`            |   ✅   | Reader skips primitive terminated by close brace                               |
|  49 | `test_reader_false_bool_before_comma`                              |   ✅   | Reader false bool before comma                                                 |

</details>

---

## test_iface - native_iface - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for per-route STA/AP interface filters (PC::on(..., pc_if_kind))._

|   # | Test                                          | Status | Description                                                               |
| --: | :-------------------------------------------- | :----: | :------------------------------------------------------------------------ |
|   1 | `test_ap_only_matches_on_ap`                  |   ✅   | Ap only matches on ap                                                     |
|   2 | `test_ap_only_hidden_on_sta`                  |   ✅   | Ap only hidden on sta                                                     |
|   3 | `test_sta_only_matches_on_sta`                |   ✅   | Sta only matches on sta                                                   |
|   4 | `test_sta_only_hidden_on_ap`                  |   ✅   | Sta only hidden on ap                                                     |
|   5 | `test_unfiltered_route_matches_any_interface` |   ✅   | Unfiltered route matches any interface                                    |
|   6 | `test_same_path_two_interfaces_picks_correct` |   ✅   | Same path bound to different interfaces; the request's interface decides. |
|   7 | `test_set_ap_ip_updates_global`               |   ✅   | Set ap ip updates global                                                  |

</details>

---

## test_regex - native_regex - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for bounded regex routes (PC::on_regex())._

|   # | Test                                            | Status | Description                                                                 |
| --: | :---------------------------------------------- | :----: | :-------------------------------------------------------------------------- |
|   1 | `test_numeric_class_plus`                       |   ✅   | Numeric class plus                                                          |
|   2 | `test_dot_star_matches_rest`                    |   ✅   | Dot star matches rest                                                       |
|   3 | `test_escaped_dot_extension`                    |   ✅   | Escaped dot extension                                                       |
|   4 | `test_optional_quantifier`                      |   ✅   | Optional quantifier                                                         |
|   5 | `test_range_class_only`                         |   ✅   | Range class only                                                            |
|   6 | `test_negated_class`                            |   ✅   | Negated class                                                               |
|   7 | `test_anchored_full_match`                      |   ✅   | Anchored full match                                                         |
|   8 | `test_method_still_enforced`                    |   ✅   | Method still enforced                                                       |
|   9 | `test_pathological_pattern_terminates_no_match` |   ✅   | Catastrophic-looking pattern with no possible match: must return (not hang) |
|  10 | `test_escape_class_digit`                       |   ✅   | Escape class digit                                                          |
|  11 | `test_escape_class_word`                        |   ✅   | Escape class word                                                           |
|  12 | `test_escape_class_space`                       |   ✅   | Escape class space                                                          |
|  13 | `test_class_escaped_members`                    |   ✅   | Class escaped members                                                       |
|  14 | `test_trailing_backslash_atom`                  |   ✅   | Trailing backslash atom                                                     |
|  15 | `test_class_leading_bracket_is_literal`         |   ✅   | Class leading bracket is literal                                            |
|  16 | `test_class_unterminated_fails_closed`          |   ✅   | Class unterminated fails closed                                             |
|  17 | `test_class_trailing_backslash_in_body`         |   ✅   | Class trailing backslash in body                                            |
|  18 | `test_class_escaped_bound_at_end`               |   ✅   | Class escaped bound at end                                                  |
|  19 | `test_empty_class_matches_nothing`              |   ✅   | Empty class matches nothing                                                 |
|  20 | `test_class_trailing_dash_is_literal`           |   ✅   | Class trailing dash is literal                                              |
|  21 | `test_class_two_ranges`                         |   ✅   | Class two ranges                                                            |
|  22 | `test_escape_class_digit_low_edge`              |   ✅   | Escape class digit low edge                                                 |
|  23 | `test_escape_class_word_edges`                  |   ✅   | Escape class word edges                                                     |
|  24 | `test_escape_class_space_direct`                |   ✅   | Escape class space direct                                                   |

</details>

---

## test_web_terminal - native_web_terminal - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the WebSocket web-serial terminal (PC_ENABLE_WEB_TERMINAL):_

|   # | Test                                        | Status | Description                                                  |
| --: | :------------------------------------------ | :----: | :----------------------------------------------------------- |
|   1 | `test_api_inert_before_begin`               |   ✅   | Api inert before begin                                       |
|   2 | `test_serves_terminal_page`                 |   ✅   | Serves terminal page                                         |
|   3 | `test_ws_upgrade_tracks_client`             |   ✅   | Ws upgrade tracks client                                     |
|   4 | `test_ws_upgrade_requires_connection_token` |   ✅   | Ws upgrade requires connection token                         |
|   5 | `test_ws_upgrade_rejects_bad_key_length`    |   ✅   | Ws upgrade rejects bad key length                            |
|   6 | `test_command_delivered_to_callback`        |   ✅   | Command delivered to callback                                |
|   7 | `test_broadcast_reaches_client`             |   ✅   | Broadcast reaches client                                     |
|   8 | `test_printf_broadcast`                     |   ✅   | Printf broadcast                                             |
|   9 | `test_no_broadcast_without_clients`         |   ✅   | No handshake -> no terminal clients -> print writes nothing. |
|  10 | `test_close_clears_client`                  |   ✅   | Close clears client                                          |
|  11 | `test_println_appends_newline`              |   ✅   | Println appends newline                                      |
|  12 | `test_print_null_is_ignored`                |   ✅   | Print null is ignored                                        |
|  13 | `test_begin_defaults_path_when_missing`     |   ✅   | Begin defaults path when missing                             |
|  14 | `test_message_without_callback`             |   ✅   | Message without callback                                     |
|  15 | `test_stale_client_slot_is_skipped`         |   ✅   | Stale client slot is skipped                                 |

</details>

---

## test_defer - native_defer - ✅ 3 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Phase 3a: the thread-safe app->worker deferred-callback path. defer() hands the callback to the_

|   # | Test                                           | Status | Description                                                    |
| --: | :--------------------------------------------- | :----: | :------------------------------------------------------------- |
|   1 | `test_defer_queues_and_the_drain_runs_it_once` |   ✅   | Defer queues and the drain runs it once                        |
|   2 | `test_server_defer_routes_by_owner`            |   ✅   | Server defer routes by owner                                   |
|   3 | `test_defer_null_fn_fails`                     |   ✅   | A null callback fails closed on every build (host and target). |

</details>

---

## test_webdav_handler - native_webdav_handler - ✅ 43 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the WebDAV request handler's recursive filesystem operations_

|   # | Test                                             | Status | Description                                                                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_pc_fs_join_seam`                           |   ✅   | sub starts with '/': the duplicate is consumed, the root's separator is the one kept.     |
|   2 | `test_pc_fs_resolve_traversal_and_root_edge`     |   ✅   | A ".." anywhere in sub is refused before touching pc_fs_join.                             |
|   3 | `test_webdav_status_text_table`                  |   ✅   | Webdav status text table                                                                  |
|   4 | `test_webdav_join_root_slash_with_empty_subpath` |   ✅   | Webdav join root slash with empty subpath                                                 |
|   5 | `test_put_stream_error_latches_for_later_chunks` |   ✅   | The file is created and takes some of the body, then the medium refuses - leaving several |
|   6 | `test_webdav_join_root_variants`                 |   ✅   | (a) root ending in '/': "/tsroot/" + "/f.txt" must not become "/tsroot//f.txt".           |
|   7 | `test_webdav_dav_empty_prefix_mount`             |   ✅   | Webdav dav empty prefix mount                                                             |
|   8 | `test_webdav_method_dispatch_edges`              |   ✅   | Webdav method dispatch edges                                                              |
|   9 | `test_webdav_copy_header_edges`                  |   ✅   | Webdav copy header edges                                                                  |
|  10 | `test_webdav_copy_dest_joins_to_root`            |   ✅   | Webdav copy dest joins to root                                                            |
|  11 | `test_webdav_propfind_file_and_trailing_slash`   |   ✅   | Webdav propfind file and trailing slash                                                   |
|  12 | `test_webdav_route_scan_skips_non_dav_routes`    |   ✅   | Webdav route scan skips non dav routes                                                    |
|  13 | `test_webdav_stream_put_abort_without_open`      |   ✅   | Webdav stream put abort without open                                                      |
|  14 | `test_webdav_status_on_dead_connection`          |   ✅   | Webdav status on dead connection                                                          |
|  15 | `test_webdav_get_put_dest_edges`                 |   ✅   | Webdav get put dest edges                                                                 |
|  16 | `test_webdav_copy_dest_path_too_long_414`        |   ✅   | 240-char fs root: a short source ("/s") still joins under 256, but root + any             |
|  17 | `test_webdav_recursive_open_failure`             |   ✅   | DELETE: the store cannot remove the name -> 403. Unlinking is a metadata write, not an    |
|  18 | `test_webdav_source_path_too_long_414`           |   ✅   | Webdav source path too long 414                                                           |
|  19 | `test_webdav_dav_wildcard_and_route_full`        |   ✅   | (a) A wildcard-terminated prefix is stored as-is; a request under it still routes.        |
|  20 | `test_webdav_error_paths`                        |   ✅   | Webdav error paths                                                                        |
|  21 | `test_webdav_deep_tree_rejected`                 |   ✅   | Webdav deep tree rejected                                                                 |
|  22 | `test_webdav_propfind_limit_and_proppatch`       |   ✅   | Webdav propfind limit and proppatch                                                       |
|  23 | `test_webdav_copy_fs_table_full`                 |   ✅   | Webdav copy fs table full                                                                 |
|  24 | `test_copy_collection_recursive`                 |   ✅   | Copy collection recursive                                                                 |
|  25 | `test_copy_collection_depth0_shallow`            |   ✅   | Copy collection depth0 shallow                                                            |
|  26 | `test_copy_overwrite_semantics`                  |   ✅   | Copy overwrite semantics                                                                  |
|  27 | `test_move_collection_recursive`                 |   ✅   | Move collection recursive                                                                 |
|  28 | `test_delete_collection_recursive`               |   ✅   | Delete collection recursive                                                               |
|  29 | `test_propfind_depth0_collection_only`           |   ✅   | Propfind depth0 collection only                                                           |
|  30 | `test_propfind_depth1_lists_members`             |   ✅   | Propfind depth1 lists members                                                             |
|  31 | `test_mkcol_create_and_conflict`                 |   ✅   | Mkcol create and conflict                                                                 |
|  32 | `test_delete_single_file`                        |   ✅   | Delete single file                                                                        |
|  33 | `test_options_advertises_dav`                    |   ✅   | Options advertises dav                                                                    |
|  34 | `test_get_file_through_mount`                    |   ✅   | Get file through mount                                                                    |
|  35 | `test_put_stream_create`                         |   ✅   | Put stream create                                                                         |
|  36 | `test_put_stream_overwrite`                      |   ✅   | Put stream overwrite                                                                      |
|  37 | `test_put_empty_buffered`                        |   ✅   | Put empty buffered                                                                        |
|  38 | `test_put_stream_write_fails_507`                |   ✅   | Put stream write fails 507                                                                |
|  39 | `test_put_stream_open_fails_409`                 |   ✅   | Put stream open fails 409                                                                 |
|  40 | `test_put_stream_traversal_403`                  |   ✅   | Put stream traversal 403                                                                  |
|  41 | `test_put_stream_begin_declines`                 |   ✅   | Non-PUT with a body: begin sees method != PUT and declines.                               |
|  42 | `test_put_stream_abort`                          |   ✅   | Headers + a partial body: Content-Length promises 10, only 4 arrive.                      |
|  43 | `test_lock_enforcement`                          |   ✅   | Lock enforcement                                                                          |

</details>

---

## test_diag - native_diag - ✅ 2 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Exercises the build-flag reporter (diag() / PC_ENABLE_DIAG): the gated diag()_

|   # | Test                               | Status | Description                 |
| --: | :--------------------------------- | :----: | :-------------------------- |
|   1 | `test_diag_serves_build_info_json` |   ✅   | Diag serves build info json |
|   2 | `test_diag_json_braces_balanced`   |   ✅   | Diag json braces balanced   |

</details>

---

## test_dma - native_dma - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DMA ingest / egress simulator (mmgr/dma) host core: an ingress_

|   # | Test                                   | Status | Description                     |
| --: | :------------------------------------- | :----: | :------------------------------ |
|   1 | `test_open_validates`                  |   ✅   | Open validates                  |
|   2 | `test_ingress_emits_rx_event`          |   ✅   | Ingress emits rx event          |
|   3 | `test_buffer_fills_then_partial_flush` |   ✅   | Buffer fills then partial flush |
|   4 | `test_ping_pong_flips_buffer`          |   ✅   | Ping pong flips buffer          |
|   5 | `test_egress_captures_tx`              |   ✅   | Egress captures tx              |
|   6 | `test_tx_one_in_flight_fail_closed`    |   ✅   | Tx one in flight fail closed    |
|   7 | `test_tx_rejects_bad_len`              |   ✅   | Tx rejects bad len              |
|   8 | `test_loopback_round_trip`             |   ✅   | Loopback round trip             |
|   9 | `test_feed_fail_closed_when_full`      |   ✅   | Feed fail closed when full      |
|  10 | `test_closed_channel_is_inert`         |   ✅   | Closed channel is inert         |
|  11 | `test_two_channels_independent`        |   ✅   | Two channels independent        |
|  12 | `test_channel_guard_subconditions`     |   ✅   | Channel guard subconditions     |

</details>

---

## test_ad9238 - native_ad9238 - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the AD9238 SPI configuration-port codec (services/peripherals/ad9238): the 16-bit_

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_instruction_word_write_single_byte`  |   ✅   | Instruction word write single byte  |
|   2 | `test_instruction_word_read_sets_msb`      |   ✅   | Instruction word read sets msb      |
|   3 | `test_instruction_word_byte_count_field`   |   ✅   | streaming (W1:W0=11): word = R/W(0) | W1:W0(11) << 13 | addr(0x100) = 0x6000 | 0x0100 = 0x6100. |
|   4 | `test_instruction_word_rejects_bad_input`  |   ✅   | Instruction word rejects bad input  |
|   5 | `test_build_write_transaction`             |   ✅   | Build write transaction             |
|   6 | `test_build_read_transaction`              |   ✅   | Build read transaction              |
|   7 | `test_build_transfer_writes_device_update` |   ✅   | Build transfer writes device update |

</details>

---

## test_lora - native_lora - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the LoRa codec + SX127x driver (services/radio/lora). The codec (RadioHead_

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_frame_parse_null_guards_and_optional_outs`   |   ✅   | Frame parse null guards and optional outs   |
|   2 | `test_frame_build_null_and_size_guards`            |   ✅   | Frame build null and size guards            |
|   3 | `test_init_rejects_incomplete_bus`                 |   ✅   | Init rejects incomplete bus                 |
|   4 | `test_init_sets_low_data_rate_optimize_at_high_sf` |   ✅   | Init sets low data rate optimize at high sf |
|   5 | `test_driver_entry_points_reject_null_bus`         |   ✅   | Driver entry points reject null bus         |
|   6 | `test_frame_build_then_parse`                      |   ✅   | Frame build then parse                      |
|   7 | `test_frame_parse_rejects_short`                   |   ✅   | Frame parse rejects short                   |
|   8 | `test_frame_build_bounds`                          |   ✅   | Frame build bounds                          |
|   9 | `test_init_verifies_chip_and_lands_in_standby`     |   ✅   | Init verifies chip and lands in standby     |
|  10 | `test_init_fails_on_wrong_version`                 |   ✅   | Init fails on wrong version                 |
|  11 | `test_init_programs_frequency`                     |   ✅   | Init programs frequency                     |
|  12 | `test_send_loads_fifo_and_starts_tx`               |   ✅   | Send loads fifo and starts tx               |
|  13 | `test_tx_done_flag`                                |   ✅   | Tx done flag                                |
|  14 | `test_set_rx_enters_continuous`                    |   ✅   | Set rx enters continuous                    |
|  15 | `test_recv_reads_frame_and_rssi`                   |   ✅   | Recv reads frame and rssi                   |
|  16 | `test_recv_no_packet`                              |   ✅   | Recv no packet                              |
|  17 | `test_recv_crc_error_dropped`                      |   ✅   | Recv crc error dropped                      |
|  18 | `test_recv_truncates_to_cap`                       |   ✅   | Recv truncates to cap                       |
|  19 | `test_frame_parse_build_guards`                    |   ✅   | Frame parse build guards                    |

</details>

---

## test_nrf24 - native_nrf24 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the nRF24L01+ driver (services/radio/nrf24) against a mock chip that emulates_

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_init_configures_and_powers_up`       |   ✅   | Init configures and powers up       |
|   2 | `test_init_fails_when_absent`              |   ✅   | Init fails when absent              |
|   3 | `test_send_pads_to_width_and_keys_tx`      |   ✅   | Send pads to width and keys tx      |
|   4 | `test_send_rejects_oversize`               |   ✅   | Send rejects oversize               |
|   5 | `test_tx_done_flag`                        |   ✅   | Tx done flag                        |
|   6 | `test_set_rx_enters_prx`                   |   ✅   | Set rx enters prx                   |
|   7 | `test_recv_reads_payload_and_pipe`         |   ✅   | Recv reads payload and pipe         |
|   8 | `test_recv_no_packet`                      |   ✅   | Recv no packet                      |
|   9 | `test_recv_fifo_empty_pipe`                |   ✅   | Recv fifo empty pipe                |
|  10 | `test_recv_truncates_to_cap`               |   ✅   | Recv truncates to cap               |
|  11 | `test_data_rate_variants`                  |   ✅   | Data rate variants                  |
|  12 | `test_init_rejects_null_args`              |   ✅   | Init rejects null args              |
|  13 | `test_send_rejects_null_args_and_zero_len` |   ✅   | Send rejects null args and zero len |
|  14 | `test_tx_done_null_bus`                    |   ✅   | Tx done null bus                    |
|  15 | `test_set_rx_null_bus_is_noop`             |   ✅   | Set rx null bus is noop             |
|  16 | `test_recv_rejects_null_args`              |   ✅   | Recv rejects null args              |
|  17 | `test_recv_with_null_pipe_out_ok`          |   ✅   | Recv with null pipe out ok          |

</details>

---

## test_pn532 - native_pn532 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the PN532 NFC frame codec (services/peripherals/pn532): the normal-information-frame_

|   # | Test                                         | Status | Description                                                                         |
| --: | :------------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_build_getfirmwareversion_kat`          |   ✅   | Host -> PN532 GetFirmwareVersion (command 0x02): the documented frame is            |
|   2 | `test_parse_getfirmwareversion_response_kat` |   ✅   | PN532 -> host response: 00 00 FF 06 FA D5 03 32 01 06 07 E8 00.                     |
|   3 | `test_build_then_parse_round_trip`           |   ✅   | Build then parse round trip                                                         |
|   4 | `test_parse_rejects_bad_preamble_and_start`  |   ✅   | Parse rejects bad preamble and start                                                |
|   5 | `test_parse_rejects_bad_lcs`                 |   ✅   | Parse rejects bad lcs                                                               |
|   6 | `test_parse_rejects_bad_dcs`                 |   ✅   | Parse rejects bad dcs                                                               |
|   7 | `test_parse_needs_more_bytes`                |   ✅   | Parse needs more bytes                                                              |
|   8 | `test_parse_rejects_over_length`             |   ✅   | frame_len 20 (> PC_PN532_MAX_DATA + 1 = 9) is rejected early.                       |
|   9 | `test_parse_rejects_zero_length`             |   ✅   | frame_len == 0 (no TFI at all) with a matching LCS is rejected explicitly, distinct |
|  10 | `test_parse_success_with_null_outputs`       |   ✅   | A fully valid, complete frame with every output pointer null must not dereference   |
|  11 | `test_ack_frame`                             |   ✅   | Ack frame                                                                           |
|  12 | `test_build_bounds`                          |   ✅   | Build bounds                                                                        |
|  13 | `test_build_frame_null_data_and_out_guards`  |   ✅   | out == NULL is rejected regardless of other args.                                   |
|  14 | `test_frame_parse_and_ack_guards`            |   ✅   | Frame parse and ack guards                                                          |

</details>

---

## test_sigfox - native_sigfox - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Sigfox AT-command codec (services/radio/sigfox): the AT$SF uplink command_

|   # | Test                             | Status | Description                                                                          |
| --: | :------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_build_uplink_hex_encode`   |   ✅   | Build uplink hex encode                                                              |
|   2 | `test_build_uplink_single_byte`  |   ✅   | Build uplink single byte                                                             |
|   3 | `test_build_uplink_bounds`       |   ✅   | Build uplink bounds                                                                  |
|   4 | `test_build_uplink_null_args`    |   ✅   | Build uplink null args                                                               |
|   5 | `test_parse_response_ok`         |   ✅   | Parse response ok                                                                    |
|   6 | `test_parse_response_error`      |   ✅   | Parse response error                                                                 |
|   7 | `test_parse_response_pending`    |   ✅   | Parse response pending                                                               |
|   8 | `test_parse_response_null_buf`   |   ✅   | Parse response null buf                                                              |
|   9 | `test_parse_response_error_wins` |   ✅   | If a buffer holds both (e.g. an echoed "OK" token then an ERROR), ERROR is reported. |

</details>

---

## test_zwave - native_zwave - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Z-Wave Serial API frame codec (services/radio/zwave): the data-frame_

|   # | Test                                            | Status | Description                                                                                |
| --: | :---------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_build_getversion_kat`                     |   ✅   | Host -> controller FUNC_ID_ZW_GET_VERSION (0x15), a REQ with no data: the documented       |
|   2 | `test_build_then_parse_round_trip`              |   ✅   | Build then parse round trip                                                                |
|   3 | `test_parse_getversion_kat`                     |   ✅   | Parse getversion kat                                                                       |
|   4 | `test_parse_rejects_bad_sof`                    |   ✅   | Parse rejects bad sof                                                                      |
|   5 | `test_parse_rejects_bad_checksum`               |   ✅   | Parse rejects bad checksum                                                                 |
|   6 | `test_parse_needs_more_bytes`                   |   ✅   | Parse needs more bytes                                                                     |
|   7 | `test_parse_rejects_over_length`                |   ✅   | frame_len 80 (> PC_ZWAVE_MAX_DATA + 3 = 19) is rejected early.                             |
|   8 | `test_control_bytes`                            |   ✅   | Control bytes                                                                              |
|   9 | `test_build_bounds`                             |   ✅   | Build bounds                                                                               |
|  10 | `test_build_rejects_null_out`                   |   ✅   | Build rejects null out                                                                     |
|  11 | `test_build_rejects_null_data_with_nonzero_len` |   ✅   | data_len > 0 but data is null: invalid combination, rejected before any bytes are written. |
|  12 | `test_parse_rejects_null_raw`                   |   ✅   | Parse rejects null raw                                                                     |
|  13 | `test_parse_needs_more_bytes_on_zero_len`       |   ✅   | Parse needs more bytes on zero len                                                         |
|  14 | `test_parse_rejects_frame_len_too_short`        |   ✅   | frame_len (raw[1]) must be at least 3 (Type + Command + Checksum); 2 is too short.         |
|  15 | `test_parse_allows_null_out_params`             |   ✅   | A successful parse must tolerate any subset of the out-params being null.                  |

</details>

---

## test_zigbee - native_zigbee - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Zigbee EZSP / ASH framing codec (services/radio/zigbee): the CRC-16/CCITT_

|   # | Test                                          | Status | Description                                                                          |
| --: | :-------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_crc16_rst_kat`                          |   ✅   | CRC-16/CCITT (poly 0x1021, init 0xFFFF) of {0xC0} is 0x38BC (the ASH RST frame CRC). |
|   2 | `test_encode_rst_frame_kat`                   |   ✅   | The documented ASH RST frame is C0 38 BC 7E (control, CRC hi/lo, flag).              |
|   3 | `test_encode_decode_round_trip`               |   ✅   | Encode decode round trip                                                             |
|   4 | `test_byte_stuffing_round_trip`               |   ✅   | A payload full of reserved bytes must survive: none may appear raw in the body.      |
|   5 | `test_decode_needs_more_without_flag`         |   ✅   | Decode needs more without flag                                                       |
|   6 | `test_decode_rejects_bad_crc`                 |   ✅   | Decode rejects bad crc                                                               |
|   7 | `test_decode_rejects_dangling_escape`         |   ✅   | Decode rejects dangling escape                                                       |
|   8 | `test_decode_rejects_small_payload_buffer`    |   ✅   | Decode rejects small payload buffer                                                  |
|   9 | `test_encode_bounds`                          |   ✅   | Encode bounds                                                                        |
|  10 | `test_encode_decode_guards`                   |   ✅   | Encode decode guards                                                                 |
|  11 | `test_encode_null_args`                       |   ✅   | Encode null args                                                                     |
|  12 | `test_encode_stuffed_control_needs_two_bytes` |   ✅   | Encode stuffed control needs two bytes                                               |
|  13 | `test_encode_capacity_boundaries`             |   ✅   | Encode capacity boundaries                                                           |
|  14 | `test_decode_null_raw`                        |   ✅   | Decode null raw                                                                      |
|  15 | `test_decode_rejects_oversized_frame`         |   ✅   | Decode rejects oversized frame                                                       |
|  16 | `test_decode_optional_outputs`                |   ✅   | Decode optional outputs                                                              |

</details>

---

## test_thread - native_thread - ✅ 38 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Thread spinel / HDLC-lite framing codec (services/radio/thread): the FCS_

|   # | Test                                         | Status | Description                                                                                |
| --: | :------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_fcs_x25_check_value`                   |   ✅   | CRC-16/X-25 (poly 0x8408, init 0xFFFF, reflected, xorout 0xFFFF) of "123456789" = 0x906E.  |
|   2 | `test_encode_decode_round_trip`              |   ✅   | A tiny spinel frame: header (flag                                                          | iid               | tid) + command (PROP_VALUE_GET) + property. |
|   3 | `test_byte_stuffing_round_trip`              |   ✅   | Byte stuffing round trip                                                                   |
|   4 | `test_decode_needs_more_without_flag`        |   ✅   | Decode needs more without flag                                                             |
|   5 | `test_decode_rejects_bad_fcs`                |   ✅   | Decode rejects bad fcs                                                                     |
|   6 | `test_decode_rejects_dangling_escape`        |   ✅   | Decode rejects dangling escape                                                             |
|   7 | `test_decode_rejects_small_payload_buffer`   |   ✅   | Decode rejects small payload buffer                                                        |
|   8 | `test_encode_bounds`                         |   ✅   | Encode bounds                                                                              |
|   9 | `test_spinel_pack_uint_kats`                 |   ✅   | Spinel pack uint kats                                                                      |
|  10 | `test_spinel_pack_unpack_round_trip`         |   ✅   | Spinel pack unpack round trip                                                              |
|  11 | `test_spinel_unpack_needs_more_and_overflow` |   ✅   | Spinel unpack needs more and overflow                                                      |
|  12 | `test_spinel_command_build_parse_round_trip` |   ✅   | header 0x81, CMD_PROP_VALUE_SET, a large property id (multi-byte packed), a value.         |
|  13 | `test_spinel_command_through_hdlc`           |   ✅   | The command payload rides inside an HDLC frame: build the command, frame it, decode        |
|  14 | `test_spinel_guards`                         |   ✅   | Spinel guards                                                                              |
|  15 | `test_thread_more_guards`                    |   ✅   | pack/unpack null-pointer guards.                                                           |
|  16 | `test_spinel_value_round_trip`               |   ✅   | Build a heterogeneous value with the writer, read it back with the reader.                 |
|  17 | `test_spinel_put_bool_false`                 |   ✅   | Every other test only exercises pc_spinel_put_bool(true); cover the v == false arm of      |
|  18 | `test_spinel_le_wire_layout`                 |   ✅   | Confirm the on-wire encoding is little-endian for the fixed-width integers.                |
|  19 | `test_spinel_protocol_version_and_caps`      |   ✅   | PROTOCOL_VERSION is two packed uints; CAPS is a packed-uint array - decode as a real       |
|  20 | `test_spinel_data_wlen_and_utf8`             |   ✅   | STREAM_RAW-style 'd' data (uint16 length prefix), then STREAM_DEBUG-style 'U' text.        |
|  21 | `test_spinel_get_data_rest`                  |   ✅   | Spinel get data rest                                                                       |
|  22 | `test_spinel_reader_bounds_latch`            |   ✅   | A too-short value latches err and every later read fails.                                  |
|  23 | `test_spinel_writer_overflow_latch`          |   ✅   | Spinel writer overflow latch                                                               |
|  24 | `test_spinel_header_helpers`                 |   ✅   | Spinel header helpers                                                                      |
|  25 | `test_spinel_prop_registry`                  |   ✅   | Spinel prop registry                                                                       |
|  26 | `test_spinel_status_names`                   |   ✅   | Spinel status names                                                                        |
|  27 | `test_spinel_last_status_decode`             |   ✅   | A real NCP unsolicited frame: header                                                       | CMD_PROP_VALUE_IS | PROP_LAST_STATUS                            | status(i). |
|  28 | `test_spinel_null_out_params`                |   ✅   | unpack_uint with no value out-parameter still reports the bytes consumed.                  |
|  29 | `test_spinel_reader_init_variants`           |   ✅   | Spinel reader init variants                                                                |
|  30 | `test_spinel_getters_null_reader`            |   ✅   | Spinel getters null reader                                                                 |
|  31 | `test_spinel_getters_short_value`            |   ✅   | An empty value: every typed read runs off the end at its first byte.                       |
|  32 | `test_spinel_get_uint_edges`                 |   ✅   | A packed uint whose continuation bit is set but which has no terminator.                   |
|  33 | `test_spinel_getters_null_out_params`        |   ✅   | Build one value holding every fixed-width field, then read it back discarding each result. |
|  34 | `test_spinel_writer_init_and_null_writer`    |   ✅   | Spinel writer init and null writer                                                         |
|  35 | `test_spinel_put_null_args`                  |   ✅   | A null data pointer with a zero length is a legal empty 'D' field.                         |
|  36 | `test_spinel_put_no_room_each_type`          |   ✅   | A zero-capacity writer: every field type fails at the room reservation.                    |
|  37 | `test_spinel_frame_edges`                    |   ✅   | encode: a null output buffer, and a null payload with a positive length.                   |
|  38 | `test_spinel_status_name_below_reset_range`  |   ✅   | Unregistered codes on either side of the 0x70..0x77 reset-cause window.                    |

</details>

---

## test_wamp - native_wamp - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the WAMP codec (services/iot/wamp): the message builders (JSON arrays over_

|   # | Test                                     | Status | Description                                                         |
| --: | :--------------------------------------- | :----: | :------------------------------------------------------------------ |
|   1 | `test_build_hello`                       |   ✅   | Build hello                                                         |
|   2 | `test_build_subscribe_default_options`   |   ✅   | Build subscribe default options                                     |
|   3 | `test_build_publish_with_args`           |   ✅   | Build publish with args                                             |
|   4 | `test_build_publish_kwargs_only`         |   ✅   | Build publish kwargs only                                           |
|   5 | `test_build_call_and_register_and_yield` |   ✅   | Build call and register and yield                                   |
|   6 | `test_build_unsubscribe_and_goodbye`     |   ✅   | Build unsubscribe and goodbye                                       |
|   7 | `test_build_unregister`                  |   ✅   | The canonical WAMP UNREGISTER example: [66, 788923562, 2103333224]. |
|   8 | `test_build_overflow_fails_closed`       |   ✅   | Build overflow fails closed                                         |
|   9 | `test_parse_type_and_id`                 |   ✅   | Parse type and id                                                   |
|  10 | `test_parse_event_positions`             |   ✅   | Parse event positions                                               |
|  11 | `test_parse_get_uri_and_nesting`         |   ✅   | Parse get uri and nesting                                           |
|  12 | `test_parse_malformed`                   |   ✅   | Parse malformed                                                     |
|  13 | `test_get_uri_dest_bounds`               |   ✅   | Get uri dest bounds                                                 |
|  14 | `test_builder_null_guards`               |   ✅   | Builder null guards                                                 |
|  15 | `test_emit_uint_zero_and_no_args`        |   ✅   | Emit uint zero and no args                                          |
|  16 | `test_parser_error_paths`                |   ✅   | Parser error paths                                                  |
|  17 | `test_builder_explicit_options`          |   ✅   | Builder explicit options                                            |
|  18 | `test_parser_all_whitespace_forms`       |   ✅   | Parser all whitespace forms                                         |
|  19 | `test_parser_nested_containers`          |   ✅   | Parser nested containers                                            |
|  20 | `test_parser_bare_token_terminators`     |   ✅   | Parser bare token terminators                                       |
|  21 | `test_parser_optional_out_params`        |   ✅   | Parser optional out params                                          |
|  22 | `test_get_uint_rejects_non_digits`       |   ✅   | Get uint rejects non digits                                         |
|  23 | `test_get_uri_shape_rejects`             |   ✅   | Get uri shape rejects                                               |

</details>

---

## test_haas_mdc - native_haas_mdc - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Haas Machine Data Collection (MDC) Q-command codec (services/machine_tool/haas_mdc): the ?Q_

|   # | Test                                    | Status | Description                                                                            |
| --: | :-------------------------------------- | :----: | :------------------------------------------------------------------------------------- |
|   1 | `test_build_q`                          |   ✅   | Build q                                                                                |
|   2 | `test_build_var`                        |   ✅   | Build var                                                                              |
|   3 | `test_parse_simple_and_value`           |   ✅   | Q100 -> serial number                                                                  |
|   4 | `test_parse_status_idle`                |   ✅   | Parse status idle                                                                      |
|   5 | `test_parse_status_busy`                |   ✅   | Parse status busy                                                                      |
|   6 | `test_parse_macro`                      |   ✅   | documented 6-decimal form                                                              |
|   7 | `test_error_and_no_frame`               |   ✅   | Error and no frame                                                                     |
|   8 | `test_leading_prompt`                   |   ✅   | previous response's trailing '>' prompt precedes this frame in the stream              |
|   9 | `test_field_access`                     |   ✅   | Field access                                                                           |
|  10 | `test_dprnt`                            |   ✅   | a pushed DPRNT line: raw text + CRLF, no STX/ETB                                       |
|  11 | `test_build_guards`                     |   ✅   | Build guards                                                                           |
|  12 | `test_parse_guards`                     |   ✅   | Parse guards                                                                           |
|  13 | `test_field_trimming_edges`             |   ✅   | trailing spaces before the comma are trimmed off the field                             |
|  14 | `test_max_fields_cap`                   |   ✅   | more comma-separated fields than the struct holds: the extras are dropped, not written |
|  15 | `test_accessor_guards`                  |   ✅   | Accessor guards                                                                        |
|  16 | `test_field_is_prefix_mismatch`         |   ✅   | the field runs past the literal: "STATUSX" is not "STATUS"                             |
|  17 | `test_parse_status_guards_and_branches` |   ✅   | Parse status guards and branches                                                       |
|  18 | `test_parse_macro_guards_and_rejects`   |   ✅   | Parse macro guards and rejects                                                         |
|  19 | `test_dprnt_guards_and_strip_edges`     |   ✅   | Dprnt guards and strip edges                                                           |

</details>

---

## test_packml - native_packml - ✅ 28 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the PackML / OMAC state model (ISA-TR88.00.02): the pure transition engine_

|   # | Test                                                       | Status | Description                                                                            |
| --: | :--------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------- |
|   1 | `test_engine_startup_to_execute`                           |   ✅   | Engine startup to execute                                                              |
|   2 | `test_engine_execute_to_complete_and_back`                 |   ✅   | Engine execute to complete and back                                                    |
|   3 | `test_engine_hold_unhold`                                  |   ✅   | Engine hold unhold                                                                     |
|   4 | `test_engine_suspend_unsuspend`                            |   ✅   | Engine suspend unsuspend                                                               |
|   5 | `test_engine_stop_from_many_states`                        |   ✅   | Engine stop from many states                                                           |
|   6 | `test_engine_abort_and_clear`                              |   ✅   | Abort from any non-abort state -> Aborting -> Aborted.                                 |
|   7 | `test_engine_stop_and_abort_are_noops_inside_a_teardown`   |   ✅   | Stop must not restart a teardown that is already running, and Abort must not           |
|   8 | `test_engine_wait_states_ignore_foreign_commands`          |   ✅   | Each wait state accepts exactly one command; anything else leaves it untouched,        |
|   9 | `test_engine_acting_states_accept_only_stop_and_abort`     |   ✅   | Acting states are transient: nothing but the universal Stop / Abort may interrupt      |
|  10 | `test_engine_execute_complete_only_from_execute`           |   ✅   | "production done" is meaningless anywhere but Execute, so it must not move the state.  |
|  11 | `test_engine_invalid_commands_are_noops`                   |   ✅   | Start only from Idle; Hold only from Execute; Reset only from Stopped/Complete; etc.   |
|  12 | `test_engine_acting_classification`                        |   ✅   | Engine acting classification                                                           |
|  13 | `test_state_wire_numbers`                                  |   ✅   | Status.StateCurrent carries the ISA-TR88.00.02 numbers an HMI expects.                 |
|  14 | `test_every_state_has_its_isa_name`                        |   ✅   | The names go straight onto an HMI / into a log line, so every one of the 17 states     |
|  15 | `test_every_command_has_its_isa_name`                      |   ✅   | Every command has its isa name                                                         |
|  16 | `test_svc_init_is_stopped`                                 |   ✅   | Svc init is stopped                                                                    |
|  17 | `test_svc_full_run_with_counts`                            |   ✅   | Svc full run with counts                                                               |
|  18 | `test_svc_count_only_in_execute`                           |   ✅   | Not executing (Stopped) -> counts are ignored.                                         |
|  19 | `test_svc_rejects_illegal_command`                         |   ✅   | Start is illegal in Stopped; the service reports no change.                            |
|  20 | `test_svc_mode_change_rules`                               |   ✅   | Allowed in Stopped.                                                                    |
|  21 | `test_svc_speed_actual_tracks_execute`                     |   ✅   | Svc speed actual tracks execute                                                        |
|  22 | `test_svc_timers`                                          |   ✅   | Svc timers                                                                             |
|  23 | `test_svc_abort_and_clear_cycle`                           |   ✅   | The fault branch driven through the owned service: Execute -> Aborting -> Aborted,     |
|  24 | `test_svc_stop_from_execute_lands_stopped`                 |   ✅   | The other teardown: Stop is legal mid-production and completes to Stopped, which       |
|  25 | `test_svc_state_complete_in_a_wait_state_does_not_restamp` |   ✅   | Wait states have no State-Complete transition, so the call must be a true no-op -      |
|  26 | `test_svc_complete_run_requires_execute`                   |   ✅   | ExecuteComplete outside Execute is not a state change and must report so.              |
|  27 | `test_svc_mode_change_allowed_in_idle_and_aborted`         |   ✅   | The mode-change rule is "stable and not producing", which is Stopped, Idle or Aborted. |
|  28 | `test_svc_status_null_out_is_ignored`                      |   ✅   | A null status buffer must be a no-op, not a write through NULL.                        |

</details>

---

## test_ikev2 - native_ikev2 - ✅ 80 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the IKEv2 (RFC 7296) message + payload codec (services/security/ikev2): the 28-octet header, the_

|   # | Test                                 | Status | Description                                                                                     |
| --: | :----------------------------------- | :----: | :---------------------------------------------------------------------------------------------- |
|   1 | `test_hdr_build`                     |   ✅   | Hdr build                                                                                       |
|   2 | `test_hdr_parse`                     |   ✅   | Hdr parse                                                                                       |
|   3 | `test_hdr_set_length`                |   ✅   | Hdr set length                                                                                  |
|   4 | `test_ke`                            |   ✅   | Ke                                                                                              |
|   5 | `test_nonce`                         |   ✅   | Nonce                                                                                           |
|   6 | `test_notify`                        |   ✅   | Notify                                                                                          |
|   7 | `test_delete`                        |   ✅   | Delete                                                                                          |
|   8 | `test_sa_build_no_keylen`            |   ✅   | Sa build no keylen                                                                              |
|   9 | `test_sa_build_keylen`               |   ✅   | Sa build keylen                                                                                 |
|  10 | `test_sa_parse`                      |   ✅   | parse the SA body (proposal area, after the 4-byte generic header) from the keylen vector       |
|  11 | `test_id_auth`                       |   ✅   | Id auth                                                                                         |
|  12 | `test_ts`                            |   ✅   | generic(4) + num/res(4) + selector(8 + 2*4) = 24                                                |
|  13 | `test_sk_frame`                      |   ✅   | Sk frame                                                                                        |
|  14 | `test_full_build`                    |   ✅   | Full build                                                                                      |
|  15 | `test_full_chain_walk`               |   ✅   | Full chain walk                                                                                 |
|  16 | `test_parse_malformed`               |   ✅   | a payload claiming length 3 (< 4) is rejected                                                   |
|  17 | `test_hdr_guards`                    |   ✅   | Hdr guards                                                                                      |
|  18 | `test_payload_iter_guards`           |   ✅   | Payload iter guards                                                                             |
|  19 | `test_payload_build_raw`             |   ✅   | Payload build raw                                                                               |
|  20 | `test_oversize_payload_lengths`      |   ✅   | a payload whose total does not fit the 16-bit length field is refused                           |
|  21 | `test_typed_builder_guards`          |   ✅   | null destination                                                                                |
|  22 | `test_builder_empty_bodies`          |   ✅   | every variable-length builder frames an empty body                                              |
|  23 | `test_cert_build`                    |   ✅   | Cert build                                                                                      |
|  24 | `test_notify_build_with_spi`         |   ✅   | Notify build with spi                                                                           |
|  25 | `test_delete_build_with_spis`        |   ✅   | Delete build with spis                                                                          |
|  26 | `test_sk_build_variants`             |   ✅   | every component is optional: an empty envelope is just the generic header                       |
|  27 | `test_sa_build_guards_and_spi`       |   ✅   | Sa build guards and spi                                                                         |
|  28 | `test_ts_build_guards`               |   ✅   | Ts build guards                                                                                 |
|  29 | `test_parse_optional_outparams`      |   ✅   | every out-param is optional, and a short body clears the ones that were supplied                |
|  30 | `test_notify_parse_spi`              |   ✅   | proto ESP, 4-byte SPI, type 16389, 2 bytes of notification data                                 |
|  31 | `test_delete_parse_spis`             |   ✅   | 2 SPIs of 4 bytes                                                                               |
|  32 | `test_sk_parse_variants`             |   ✅   | an implicit-IV / no-ICV cipher leaves the whole body as ciphertext                              |
|  33 | `test_sa_proposal_malformed`         |   ✅   | Sa proposal malformed                                                                           |
|  34 | `test_transform_iter_guards`         |   ✅   | Transform iter guards                                                                           |
|  35 | `test_transform_attributes`          |   ✅   | transform 1 carries a TLV attribute (AF bit clear: a 2-byte length then the value), transform 2 |
|  36 | `test_ts_parse_malformed`            |   ✅   | Ts parse malformed                                                                              |
|  37 | `test_ts_get_second_selector`        |   ✅   | Ts get second selector                                                                          |
|  38 | `test_sa_build_widest_proposal`      |   ✅   | The widest SA this builder can emit - a 255-byte SPI and 255 keyed (12-byte) transforms,        |
|  39 | `test_ts_build_widest_selector_list` |   ✅   | The widest TS payload - 255 IPv6 selectors, the largest selector at 40 bytes - frames to        |
|  40 | `test_prf_plus_kat`                  |   ✅   | Prf plus kat                                                                                    |
|  41 | `test_prf_plus_guards`               |   ✅   | Prf plus guards                                                                                 |
|  42 | `test_derive_keys_kat_16b_nonces`    |   ✅   | Derive keys kat 16b nonces                                                                      |
|  43 | `test_derive_keys_kat_prehash_key`   |   ✅   | Derive keys kat prehash key                                                                     |
|  44 | `test_derive_keys_guards`            |   ✅   | Derive keys guards                                                                              |
|  45 | `test_sk_aead_seal_kat`              |   ✅   | Sk aead seal kat                                                                                |
|  46 | `test_sk_aead_open_roundtrip`        |   ✅   | open the golden ct+tag -> the plaintext.                                                        |
|  47 | `test_sk_aead_inplace_and_guards`    |   ✅   | In-place seal then open (out aliases the plaintext buffer) round-trips.                         |
|  48 | `test_dh_x25519_raw_kat`             |   ✅   | pc_ike_dh_compute is X25519(scalar, u) for group 31 - RFC 7748 §5.2 vector 1.                   |
|  49 | `test_dh_x25519_agreement`           |   ✅   | RFC 7748 §6.1: each side's public = X25519(priv, base), and both shared secrets agree.          |
|  50 | `test_dh_guards`                     |   ✅   | Unsupported group (19 = P-256, not yet implemented) -> 0.                                       |
|  51 | `test_auth_psk_kat`                  |   ✅   | Auth psk kat                                                                                    |
|  52 | `test_auth_psk_guards`               |   ✅   | Auth psk guards                                                                                 |
|  53 | `test_sa_init_build_parse`           |   ✅   | Sa init build parse                                                                             |
|  54 | `test_sa_init_parse_guards`          |   ✅   | Sa init parse guards                                                                            |
|  55 | `test_auth_msg_roundtrip`            |   ✅   | Build the inner chain IDi(next=AUTH)                                                            | AUTH(next=PC_NONE). |
|  56 | `test_auth_msg_tamper_and_guards`    |   ✅   | Auth msg tamper and guards                                                                      |
|  57 | `test_signed_octets_kat`             |   ✅   | Signed octets kat                                                                               |
|  58 | `test_auth_ecdsa_sign_verify`        |   ✅   | Auth ecdsa sign verify                                                                          |
|  59 | `test_suite_keylengths`              |   ✅   | AEAD (AES-GCM-16, 256-bit): sk_a = 0 (no separate integrity), sk_e = 32 key + 4 salt.           |
|  60 | `test_sa_keys_from_init_agreement`   |   ✅   | The initiator holds Alice's D-H private and receives Bob's KE; the responder is the mirror.     |
|  61 | `test_initiator_sa_init_handshake`   |   ✅   | Initiator sa init handshake                                                                     |
|  62 | `test_initiator_handshake_guards`    |   ✅   | A response echoing the WRONG initiator SPI is rejected and lands in FAILED.                     |
|  63 | `test_initiator_ike_auth_send`       |   ✅   | Initiator ike auth send                                                                         |
|  64 | `test_initiator_full_handshake`      |   ✅   | Happy path: a responder signing with the shared PSK is authenticated -> ESTABLISHED.            |
|  65 | `test_responder_sa_init_exchange`    |   ✅   | The initiator (Alice) starts.                                                                   |
|  66 | `test_full_bidirectional_handshake`  |   ✅   | IKE_SA_INIT: initiator -> responder -> initiator.                                               |
|  67 | `test_informational_exchange`        |   ✅   | Informational exchange                                                                          |
|  68 | `test_child_keymat_kat`              |   ✅   | Child keymat kat                                                                                |
|  69 | `test_create_child_sa_msg`           |   ✅   | Create child sa msg                                                                             |
|  70 | `test_auth_verify_rsa`               |   ✅   | Auth verify rsa                                                                                 |
|  71 | `test_rekey_derive_keys`             |   ✅   | Rekey derive keys                                                                               |
|  72 | `test_cp_build_golden`               |   ✅   | Cp build golden                                                                                 |
|  73 | `test_cp_parse_roundtrip`            |   ✅   | Parse the golden body (after the 4-byte generic header) and walk its attributes.                |
|  74 | `test_cp_request_empty_and_guards`   |   ✅   | A CFG_REQUEST asks for an address with an empty (zero-length) attribute.                        |
|  75 | `test_skf_build_parse`               |   ✅   | Fragment 2 of 3: body = 4 gen + 4 frag hdr + 8 iv + 12 ct + 16 icv = 44.                        |
|  76 | `test_frag_reassembly`               |   ✅   | Frag reassembly                                                                                 |
|  77 | `test_frag_guards`                   |   ✅   | Frag guards                                                                                     |
|  78 | `test_cookie_compute_kat`            |   ✅   | Cookie compute kat                                                                              |
|  79 | `test_cookie_verify`                 |   ✅   | The genuine cookie verifies; the version tag is read from the cookie itself.                    |
|  80 | `test_cookie_notify_build`           |   ✅   | The COOKIE notify carries the cookie and parses back with type 16390.                           |

</details>

---

## test_ikev2_natt - native_ikev2 - ✅ 4 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for IKEv2 NAT traversal (services/security/ikev2/ikev2_natt): the RFC 7296 §2.23 NAT-detection hash_

|   # | Test                           | Status | Description                                                                                        |
| --: | :----------------------------- | :----: | :------------------------------------------------------------------------------------------------- |
|   1 | `test_natd_hash_kat`           |   ✅   | Natd hash kat                                                                                      |
|   2 | `test_natd_notify_build_parse` |   ✅   | A NAT_DETECTION_SOURCE_IP notify carries the 20-byte hash and parses back with the right type.     |
|   3 | `test_natd_detection`          |   ✅   | The peer sends NAT_DETECTION_SOURCE_IP over what it believes its own address is (203.0.113.5:500). |
|   4 | `test_natt_udp_demux`          |   ✅   | A NAT-keepalive is exactly one 0xFF octet.                                                         |

</details>

---

## test_df1 - native_df1 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Allen-Bradley DF1 full-duplex frame codec (services/fieldbus/df1): the BCC and_

|   # | Test                                               | Status | Description                                                 |
| --: | :------------------------------------------------- | :----: | :---------------------------------------------------------- |
|   1 | `test_bcc_vector`                                  |   ✅   | Bcc vector                                                  |
|   2 | `test_crc_vector`                                  |   ✅   | Crc vector                                                  |
|   3 | `test_build_bcc_frame`                             |   ✅   | Build bcc frame                                             |
|   4 | `test_build_dle_stuffing`                          |   ✅   | Build dle stuffing                                          |
|   5 | `test_round_trip_bcc`                              |   ✅   | Round trip bcc                                              |
|   6 | `test_round_trip_crc`                              |   ✅   | Round trip crc                                              |
|   7 | `test_empty_data_frame`                            |   ✅   | Empty data frame                                            |
|   8 | `test_parse_rejects_bad`                           |   ✅   | Corrupt a data byte -> BCC mismatch.                        |
|   9 | `test_build_overflow_fails_closed`                 |   ✅   | Build overflow fails closed                                 |
|  10 | `test_parse_edges_and_guards`                      |   ✅   | build guards                                                |
|  11 | `test_parse_bad_leader_first_byte_and_null_outlen` |   ✅   | First octet is not DLE (second octet left untouched/valid). |

</details>

---

## test_ota_rollback - native_ota_rollback - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the OTA rollback decision (server/update/ota_rollback). The esp_ota_

|   # | Test                                      | Status | Description                                                                      |
| --: | :---------------------------------------- | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_not_pending_waits`                  |   ✅   | A normally-booted (valid/undefined) image never rolls back.                      |
|   2 | `test_pending_self_test_ok_commits`       |   ✅   | Pending self test ok commits                                                     |
|   3 | `test_pending_within_window_waits`        |   ✅   | Pending within window waits                                                      |
|   4 | `test_pending_window_elapsed_rolls_back`  |   ✅   | Pending window elapsed rolls back                                                |
|   5 | `test_self_test_ok_beats_window`          |   ✅   | A passing self-test commits even past the window.                                |
|   6 | `test_host_platform_hooks_are_safe_noops` |   ✅   | On a host build there are no OTA partitions: img_state reports UNDEFINED and the |

</details>

---

## test_radio_power - native_radio_power - ✅ 3 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the radio-power mode names (network_drivers/physical/radio_power). Applying the_

|   # | Test                                     | Status | Description                                                                    |
| --: | :--------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_ps_names`                          |   ✅   | Ps names                                                                       |
|   2 | `test_apply_is_noop_on_host`             |   ✅   | Apply is noop on host                                                          |
|   3 | `test_busy_hold_release_is_noop_on_host` |   ✅   | Bulk-transfer keep-awake refcount is ESP32-only; on host both calls are no-ops |

</details>

---

## test_oidc - native_oidc - ✅ 42 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the OIDC RS256 ID-token verifier (services/security/oidc). Vectors are_

|   # | Test                                                   | Status | Description                                                                                |
| --: | :----------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_verify_scratch_partial_exhaustion`               |   ✅   | Verify scratch partial exhaustion                                                          |
|   2 | `test_find_field_separator_forms`                      |   ✅   | space / tab / colon / newline / CR all skipped before the value.                           |
|   3 | `test_find_field_value_runs_to_buffer_end`             |   ✅   | Trailing backslash with no following byte: the escape skip must not step past the end.     |
|   4 | `test_get_int64_negative_and_non_numeric`              |   ✅   | Get int64 negative and non numeric                                                         |
|   5 | `test_aud_same_length_mismatch_and_numeric`            |   ✅   | Aud same length mismatch and numeric                                                       |
|   6 | `test_split3_rejects_empty_segments`                   |   ✅   | Split3 rejects empty segments                                                              |
|   7 | `test_jwk_field_widths`                                |   ✅   | e decodes to 5 bytes whose leading byte is NOT zero -> does not fit the 4-byte field.      |
|   8 | `test_jwks_empty_kid_and_truncated_document`           |   ✅   | Empty kid behaves like "no kid requested": the first usable RSA key is taken.              |
|   9 | `test_verify_with_key_arg_guards`                      |   ✅   | Verify with key arg guards                                                                 |
|  10 | `test_verify_optional_iss_aud_expectations`            |   ✅   | Verify optional iss aud expectations                                                       |
|  11 | `test_verify_exp_required_nbf_past`                    |   ✅   | Verify exp required nbf past                                                               |
|  12 | `test_oidc_parse_edge_guards`                          |   ✅   | Oidc parse edge guards                                                                     |
|  13 | `test_oidc_signed_claim_guards`                        |   ✅   | Oidc signed claim guards                                                                   |
|  14 | `test_jwks_malformed_keys`                             |   ✅   | Jwks malformed keys                                                                        |
|  15 | `test_token_kid_guards`                                |   ✅   | Token kid guards                                                                           |
|  16 | `test_jwks_find_guards`                                |   ✅   | Jwks find guards                                                                           |
|  17 | `test_verify_guards_and_malformed`                     |   ✅   | Verify guards and malformed                                                                |
|  18 | `test_token_kid`                                       |   ✅   | Token kid                                                                                  |
|  19 | `test_jwks_find`                                       |   ✅   | Jwks find                                                                                  |
|  20 | `test_jwks_find_missing_kid_fails`                     |   ✅   | Jwks find missing kid fails                                                                |
|  21 | `test_verify_valid_token_and_claims`                   |   ✅   | Verify valid token and claims                                                              |
|  22 | `test_verify_aud_array`                                |   ✅   | Verify aud array                                                                           |
|  23 | `test_reject_expired`                                  |   ✅   | Reject expired                                                                             |
|  24 | `test_reject_wrong_issuer`                             |   ✅   | Reject wrong issuer                                                                        |
|  25 | `test_reject_wrong_audience`                           |   ✅   | Reject wrong audience                                                                      |
|  26 | `test_reject_non_rs256_header`                         |   ✅   | Reject non rs256 header                                                                    |
|  27 | `test_reject_tampered_payload`                         |   ✅   | Reject tampered payload                                                                    |
|  28 | `test_reject_tampered_signature`                       |   ✅   | Reject tampered signature                                                                  |
|  29 | `test_reject_unknown_key`                              |   ✅   | JWKS whose only key has a different kid than the token's.                                  |
|  30 | `test_reject_malformed`                                |   ✅   | No kid extractable -> the sole JWKS key is selected, then the token shape                  |
|  31 | `test_bn_is_zero`                                      |   ✅   | Bn is zero                                                                                 |
|  32 | `test_bn_dh_validate_range_guards`                     |   ✅   | v == 0: no high limb set, d[0] <= 1 -> reject.                                             |
|  33 | `test_bn_expmod_group14_small_exponent`                |   ✅   | Bn expmod group14 small exponent                                                           |
|  34 | `test_bn_expmod_group14_reinit_short_circuit`          |   ✅   | Bn expmod group14 reinit short circuit                                                     |
|  35 | `test_bn_expmod_group14_large_operand_needs_reduction` |   ✅   | Bn expmod group14 large operand needs reduction                                            |
|  36 | `test_rsa_sign_verify_sha512`                          |   ✅   | Rsa sign verify sha512                                                                     |
|  37 | `test_rsa_sign_zero_exponent`                          |   ✅   | No real key has d == 0, so this goes to the primitive, which takes n and d directly.       |
|  38 | `test_rsa_sign_tiny_modulus_reduction_equal_limbs`     |   ✅   | Rsa sign tiny modulus reduction equal limbs                                                |
|  39 | `test_rsa_verify_length_and_range_guards`              |   ✅   | Rsa verify length and range guards                                                         |
|  40 | `test_rsa_verify_zero_public_exponent`                 |   ✅   | Rsa verify zero public exponent                                                            |
|  41 | `test_rsa_encode_pubkey`                               |   ✅   | Not loaded -> guard (line coverage only; state is fully re-established below).             |
|  42 | `test_rsa_encode_pubkey_zero_exponent`                 |   ✅   | No key carries e == 0, so it is written straight into the public struct the encoder reads. |

</details>

---

## test_keepalive - native_keepalive - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_HTTP/1.1 keep-alive (PC_ENABLE_KEEPALIVE). Each test drives one or more_

|   # | Test                                              | Status | Description                                                                               |
| --: | :------------------------------------------------ | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_conn_token_ws_and_bare_keepalive`           |   ✅   | Conn token ws and bare keepalive                                                          |
|   2 | `test_conn_token_delimiter_runs_and_trailing_ows` |   ✅   | A leading comma, then SP, then HTAB: the whole delimiter run is skipped before the token. |
|   3 | `test_http11_default_keeps_alive`                 |   ✅   | Http11 default keeps alive                                                                |
|   4 | `test_http11_explicit_close`                      |   ✅   | Http11 explicit close                                                                     |
|   5 | `test_http10_default_closes`                      |   ✅   | Http10 default closes                                                                     |
|   6 | `test_http10_explicit_keepalive`                  |   ✅   | Http10 explicit keepalive                                                                 |
|   7 | `test_connection_token_list_close`                |   ✅   | "close" appearing in a token list must still be honored.                                  |
|   8 | `test_two_sequential_requests_same_slot`          |   ✅   | Two sequential requests same slot                                                         |
|   9 | `test_pipelined_requests`                         |   ✅   | Two requests delivered in one shot: the proactive drain in handle() must                  |
|  10 | `test_404_still_keeps_alive`                      |   ✅   | A well-formed request to an unknown path is a normal response, not an                     |
|  11 | `test_max_requests_cap_closes`                    |   ✅   | PC_KEEPALIVE_MAX_REQUESTS=3: the 3rd response closes the connection.                      |
|  12 | `test_fresh_connection_resets_count`              |   ✅   | Run a slot up to the cap, then re-open it (new connection) and confirm the                |

</details>

---

## test_range - native_range - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_HTTP Range requests / 206 Partial Content (PC_ENABLE_RANGE). Each test_

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_unsatisfiable_range_416_carries_cors`        |   ✅   | Unsatisfiable range 416 carries cors        |
|   2 | `test_file_send_backpressure_resumes_across_polls` |   ✅   | File send backpressure resumes across polls |
|   3 | `test_file_send_write_fails_then_retries`          |   ✅   | File send write fails then retries          |
|   4 | `test_file_send_short_read_stops`                  |   ✅   | File send short read stops                  |
|   5 | `test_range_trailing_garbage_ignored`              |   ✅   | Range trailing garbage ignored              |
|   6 | `test_range_start_after_end_unsatisfiable`         |   ✅   | Range start after end unsatisfiable         |
|   7 | `test_range_suffix_on_empty_file`                  |   ✅   | Range suffix on empty file                  |
|   8 | `test_serve_file_connection_gone`                  |   ✅   | Serve file connection gone                  |
|   9 | `test_no_range_full_200`                           |   ✅   | No range full 200                           |
|  10 | `test_range_prefix`                                |   ✅   | Range prefix                                |
|  11 | `test_range_open_ended`                            |   ✅   | Range open ended                            |
|  12 | `test_range_suffix`                                |   ✅   | Range suffix                                |
|  13 | `test_range_single_byte`                           |   ✅   | Range single byte                           |
|  14 | `test_range_clamped_to_eof`                        |   ✅   | Range clamped to eof                        |
|  15 | `test_range_unsatisfiable_416`                     |   ✅   | Range unsatisfiable 416                     |
|  16 | `test_malformed_range_ignored`                     |   ✅   | Malformed range ignored                     |
|  17 | `test_range_overflow_start_unsatisfiable`          |   ✅   | Range overflow start unsatisfiable          |
|  18 | `test_range_overflow_end_clamps`                   |   ✅   | Range overflow end clamps                   |
|  19 | `test_range_suffix_zero_unsatisfiable`             |   ✅   | Range suffix zero unsatisfiable             |
|  20 | `test_multirange_falls_back_to_200`                |   ✅   | Multirange falls back to 200                |
|  21 | `test_head_with_range_no_body`                     |   ✅   | Head with range no body                     |

</details>

---

## test_smtp - native_smtp - ✅ 39 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the SMTP client dialogue engine (services/net/smtp/smtp_run). A scripted_

|   # | Test                                                        | Status | Description                                                                                        |
| --: | :---------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------------- |
|   1 | `test_reply_parser_skips_malformed_lines`                   |   ✅   | Reply parser skips malformed lines                                                                 |
|   2 | `test_reply_bare_three_digit_line_is_final`                 |   ✅   | Reply bare three digit line is final                                                               |
|   3 | `test_ehlo_capability_scan_edges`                           |   ✅   | Ehlo capability scan edges                                                                         |
|   4 | `test_null_optional_fields`                                 |   ✅   | Null optional fields                                                                               |
|   5 | `test_null_password_sends_empty_secret`                     |   ✅   | Null password sends empty secret                                                                   |
|   6 | `test_empty_user_skips_auth`                                |   ✅   | Empty user skips auth                                                                              |
|   7 | `test_arg_validation_rejects_each_missing_field`            |   ✅   | Arg validation rejects each missing field                                                          |
|   8 | `test_rcpt_251_is_accepted`                                 |   ✅   | Rcpt 251 is accepted                                                                               |
|   9 | `test_command_helper_send_failure`                          |   ✅   | Command helper send failure                                                                        |
|  10 | `test_happy_path_no_auth`                                   |   ✅   | Happy path no auth                                                                                 |
|  11 | `test_auth_login`                                           |   ✅   | Auth login                                                                                         |
|  12 | `test_auth_rejected`                                        |   ✅   | Auth rejected                                                                                      |
|  13 | `test_greeting_not_ready`                                   |   ✅   | Greeting not ready                                                                                 |
|  14 | `test_rcpt_rejected`                                        |   ✅   | Rcpt rejected                                                                                      |
|  15 | `test_data_refused`                                         |   ✅   | Data refused                                                                                       |
|  16 | `test_dot_stuffing`                                         |   ✅   | Dot stuffing                                                                                       |
|  17 | `test_multiline_reply_and_lf_body`                          |   ✅   | Multiline reply and lf body                                                                        |
|  18 | `test_partial_reads_dribble`                                |   ✅   | Partial reads dribble                                                                              |
|  19 | `test_missing_required_arg`                                 |   ✅   | Missing required arg                                                                               |
|  20 | `test_io_error_when_server_hangs`                           |   ✅   | Io error when server hangs                                                                         |
|  21 | `test_reply_buffer_overflow`                                |   ✅   | Reply buffer overflow                                                                              |
|  22 | `test_command_send_fails`                                   |   ✅   | Command send fails                                                                                 |
|  23 | `test_body_send_fails`                                      |   ✅   | Body send fails                                                                                    |
|  24 | `test_auth_secret_too_long`                                 |   ✅   | Auth secret too long                                                                               |
|  25 | `test_io_error_at_each_step`                                |   ✅   | greeting ok, then hang before: EHLO / MAIL(no auth) / AUTH(user) / pass-leg / RCPT / DATA / final. |
|  26 | `test_protocol_error_at_each_step`                          |   ✅   | Protocol error at each step                                                                        |
|  27 | `test_command_line_overflows`                               |   ✅   | Command line overflows                                                                             |
|  28 | `test_message_header_overflow`                              |   ✅   | Message header overflow                                                                            |
|  29 | `test_cr_in_body_dropped`                                   |   ✅   | Cr in body dropped                                                                                 |
|  30 | `test_build_message_boundary_overflows`                     |   ✅   | Build message boundary overflows                                                                   |
|  31 | `test_host_smtp_send_stub`                                  |   ✅   | Host smtp send stub                                                                                |
|  32 | `test_starttls_upgrades_and_reissues_ehlo`                  |   ✅   | Starttls upgrades and reissues ehlo                                                                |
|  33 | `test_starttls_not_advertised_fails_before_auth`            |   ✅   | The security property: a server (or an attacker stripping the capability) that does not offer      |
|  34 | `test_starttls_partial_keyword_is_not_a_match`              |   ✅   | "STARTTLSX" is a different keyword; treating it as STARTTLS would be a silent downgrade.           |
|  35 | `test_starttls_capability_match_is_case_insensitive`        |   ✅   | Starttls capability match is case insensitive                                                      |
|  36 | `test_starttls_server_refuses_the_upgrade`                  |   ✅   | Starttls server refuses the upgrade                                                                |
|  37 | `test_starttls_handshake_failure_aborts`                    |   ✅   | Starttls handshake failure aborts                                                                  |
|  38 | `test_starttls_without_an_upgrade_callback_is_an_arg_error` |   ✅   | Starttls without an upgrade callback is an arg error                                               |
|  39 | `test_plain_ignores_an_advertised_starttls`                 |   ✅   | Configured plaintext: the advertisement is informational, the engine must not upgrade.             |

</details>

---

## test_dns_wire - native_dns_wire - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_The DNS name codec (network_drivers/network/dns/dns_wire, RFC 1035 sec 3.1 / 4.1.4): labels to a_

|   # | Test                                       | Status | Description                                                       |
| --: | :----------------------------------------- | :----: | :---------------------------------------------------------------- |
|   1 | `test_decode_single_label`                 |   ✅   | Decode single label                                               |
|   2 | `test_decode_multi_label`                  |   ✅   | Decode multi label                                                |
|   3 | `test_decode_root_is_empty`                |   ✅   | Decode root is empty                                              |
|   4 | `test_decode_truncated`                    |   ✅   | Decode truncated                                                  |
|   5 | `test_decode_undefined_label_type`         |   ✅   | Decode undefined label type                                       |
|   6 | `test_decode_out_cap`                      |   ✅   | Decode out cap                                                    |
|   7 | `test_decode_pointer_refused_and_followed` |   ✅   | offset 0: "local" root. offset 7: "www" then a pointer back to 0. |
|   8 | `test_decode_pointer_loop_terminates`      |   ✅   | Decode pointer loop terminates                                    |
|   9 | `test_decode_null_args`                    |   ✅   | Decode null args                                                  |
|  10 | `test_encode_multi_label`                  |   ✅   | Encode multi label                                                |
|  11 | `test_encode_trailing_dot`                 |   ✅   | Encode trailing dot                                               |
|  12 | `test_encode_empty_is_root`                |   ✅   | Encode empty is root                                              |
|  13 | `test_encode_rejects_bad_labels`           |   ✅   | Encode rejects bad labels                                         |
|  14 | `test_encode_cap`                          |   ✅   | Encode cap                                                        |
|  15 | `test_encode_null_args`                    |   ✅   | Encode null args                                                  |
|  16 | `test_encode_decode_round_trip`            |   ✅   | Encode decode round trip                                          |
|  17 | `test_name_eq_ignores_case`                |   ✅   | Name eq ignores case                                              |

</details>

---

## test_rtc - native_rtc - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DS1307/DS3231 RTC conversions (services/peripherals/rtc): the BCD time registers_

|   # | Test                               | Status | Description                                                                          |
| --: | :--------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_known_epoch_2000`            |   ✅   | Known epoch 2000                                                                     |
|   2 | `test_decode_datetime`             |   ✅   | Decode datetime                                                                      |
|   3 | `test_12hour_mode_equivalence`     |   ✅   | 14:00 as 24-hour (0x14) and as 12-hour PM 2 (0x40                                    | 0x20 | 0x02) must be the same time. |
|   4 | `test_12hour_midnight_and_noon`    |   ✅   | 12hour midnight and noon                                                             |
|   5 | `test_roundtrip_over_range`        |   ✅   | Roundtrip over range                                                                 |
|   6 | `test_leap_day`                    |   ✅   | Leap day                                                                             |
|   7 | `test_masks_ch_and_century`        |   ✅   | The DS1307 clock-halt bit (sec bit7) and the DS3231 century bit (month bit7) must be |
|   8 | `test_invalid_guards`              |   ✅   | Invalid guards                                                                       |
|   9 | `test_null_regs_pointer`           |   ✅   | Null regs pointer                                                                    |
|  10 | `test_invalid_guards_upper_bounds` |   ✅   | Invalid guards upper bounds                                                          |
|  11 | `test_12hour_invalid_h12`          |   ✅   | 12hour invalid h12                                                                   |
|  12 | `test_epoch_overflow_rejected`     |   ✅   | Epoch overflow rejected                                                              |
|  13 | `test_read_and_set_drive_the_bus`  |   ✅   | Read and set drive the bus                                                           |
|  14 | `test_absent_rtc_reports_zero`     |   ✅   | Absent rtc reports zero                                                              |

</details>

---

## test_safety_scl - native_safety_scl - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEC 61784-3 black-channel SCL primitives (services/machine_tool/safety_scl). The four ways_

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_starts_in_init_and_is_usable`                  |   ✅   | Starts in init and is usable                  |
|   2 | `test_good_frames_run`                               |   ✅   | Good frames run                               |
|   3 | `test_bad_signature_trips_signature_fault`           |   ✅   | Bad signature trips signature fault           |
|   4 | `test_lost_frame_trips_counter_fault`                |   ✅   | Lost frame trips counter fault                |
|   5 | `test_duplicate_frame_trips_counter_fault`           |   ✅   | Duplicate frame trips counter fault           |
|   6 | `test_reordered_frame_trips_counter_fault`           |   ✅   | Reordered frame trips counter fault           |
|   7 | `test_inserted_frame_trips_counter_fault`            |   ✅   | Inserted frame trips counter fault            |
|   8 | `test_watchdog_trips_on_a_silent_channel`            |   ✅   | Watchdog trips on a silent channel            |
|   9 | `test_watchdog_does_not_trip_before_the_first_frame` |   ✅   | Watchdog does not trip before the first frame |
|  10 | `test_watchdog_is_wrap_safe`                         |   ✅   | Watchdog is wrap safe                         |
|  11 | `test_zero_watchdog_disables_the_timeout`            |   ✅   | Zero watchdog disables the timeout            |
|  12 | `test_failsafe_latches_and_keeps_the_first_fault`    |   ✅   | Failsafe latches and keeps the first fault    |
|  13 | `test_reset_re_establishes_and_preserves_tallies`    |   ✅   | Reset re establishes and preserves tallies    |
|  14 | `test_counter_wraps_at_the_modulus`                  |   ✅   | Counter wraps at the modulus                  |
|  15 | `test_init_normalises_the_first_counter`             |   ✅   | Init normalises the first counter             |
|  16 | `test_null_guards`                                   |   ✅   | Null guards                                   |

</details>

---

## test_rcwl0516 - native_rcwl0516 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the one-GPIO presence facade (services/peripherals/rcwl0516): the debounce that swallows_

|   # | Test                                                | Status | Description                                                         |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------ |
|   1 | `test_binding_refuses_before_begin`                 |   ✅   | Binding refuses before begin                                        |
|   2 | `test_starts_absent`                                |   ✅   | Starts absent                                                       |
|   3 | `test_high_asserts_only_after_debounce`             |   ✅   | High asserts only after debounce                                    |
|   4 | `test_chatter_shorter_than_debounce_never_asserts`  |   ✅   | Chatter shorter than debounce never asserts                         |
|   5 | `test_hold_bridges_the_gap_after_pin_drops`         |   ✅   | Hold bridges the gap after pin drops                                |
|   6 | `test_retrigger_gaps_stay_one_continuous_span`      |   ✅   | Retrigger gaps stay one continuous span                             |
|   7 | `test_event_fires_once_per_transition`              |   ✅   | Event fires once per transition                                     |
|   8 | `test_wrap_safe_across_millis_rollover`             |   ✅   | Wrap safe across millis rollover                                    |
|   9 | `test_zero_debounce_and_zero_hold_are_pass_through` |   ✅   | Zero debounce and zero hold are pass through                        |
|  10 | `test_repeated_and_static_now_is_harmless`          |   ✅   | Polling faster than the clock ticks must not stall or double-count. |
|  11 | `test_rcwl_defaults_and_null_guards`                |   ✅   | Rcwl defaults and null guards                                       |
|  12 | `test_binding_samples_the_pin`                      |   ✅   | Binding samples the pin                                             |

</details>

---

## test_sen0192 - native_sen0192 - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the SEN0192 microwave motion sensor's pure presence state machine_

|   # | Test                                     | Status | Description                                                                                       |
| --: | :--------------------------------------- | :----: | :------------------------------------------------------------------------------------------------ |
|   1 | `test_asserts_on_active_and_counts_edge` |   ✅   | Asserts on active and counts edge                                                                 |
|   2 | `test_holds_then_clears_after_window`    |   ✅   | Holds then clears after window                                                                    |
|   3 | `test_reasserts_as_new_event`            |   ✅   | Reasserts as new event                                                                            |
|   4 | `test_active_low_polarity`               |   ✅   | Active low polarity                                                                               |
|   5 | `test_active_age`                        |   ✅   | Active age                                                                                        |
|   6 | `test_tick_present_unseeded_holds`       |   ✅   | present && !seeded cannot occur through the public update()/tick() sequence (present is only ever |
|   7 | `test_binding_samples_the_pin`           |   ✅   | Binding samples the pin                                                                           |

</details>

---

## test_sht3x - native_sht3x - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Sensirion SHT3x codec (services/peripherals/sht3x): the CRC-8 against the datasheet_

|   # | Test                          | Status | Description                            |
| --: | :---------------------------- | :----: | :------------------------------------- |
|   1 | `test_crc8_datasheet_vector`  |   ✅   | Crc8 datasheet vector                  |
|   2 | `test_conversion`             |   ✅   | Endpoints of the linear map are exact. |
|   3 | `test_parse_valid`            |   ✅   | Parse valid                            |
|   4 | `test_parse_bad_crc`          |   ✅   | Parse bad crc                          |
|   5 | `test_parse_null_out`         |   ✅   | Parse null out                         |
|   6 | `test_parse_null_resp`        |   ✅   | Parse null resp                        |
|   7 | `test_read_drives_the_bus`    |   ✅   | Read drives the bus                    |
|   8 | `test_read_rejects_a_bad_crc` |   ✅   | Read rejects a bad crc                 |

</details>

---

## test_smbus - native_smbus - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_SMBus 3.1 Packet Error Code. The PEC covers every byte of a transaction including the address_

|   # | Test                                  | Status | Description                    |
| --: | :------------------------------------ | :----: | :----------------------------- |
|   1 | `test_addr_byte`                      |   ✅   | Addr byte                      |
|   2 | `test_pec_write_matches_engine`       |   ✅   | Pec write matches engine       |
|   3 | `test_pec_read_matches_engine`        |   ✅   | Pec read matches engine        |
|   4 | `test_pec_read_no_command`            |   ✅   | Pec read no command            |
|   5 | `test_pec_depends_on_address`         |   ✅   | Pec depends on address         |
|   6 | `test_pec_depends_on_direction`       |   ✅   | Pec depends on direction       |
|   7 | `test_pec_empty_payload`              |   ✅   | Pec empty payload              |
|   8 | `test_pec_flag`                       |   ✅   | Pec flag                       |
|   9 | `test_shapes_on_the_wire`             |   ✅   | Shapes on the wire             |
|  10 | `test_block_write_counts_the_payload` |   ✅   | Block write counts the payload |
|  11 | `test_block_write_refuses_oversize`   |   ✅   | Block write refuses oversize   |

</details>

---

## test_pmbus - native_pmbus - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_PMBus 1.3 numeric encodings. Every value a part reports arrives in one of three formats, and all_

|   # | Test                        | Status | Description                                                                             |
| --: | :-------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_vout_mode`            |   ✅   | Vout mode                                                                               |
|   2 | `test_linear11_fields`      |   ✅   | Linear11 fields                                                                         |
|   3 | `test_linear11_decode`      |   ✅   | Linear11 decode                                                                         |
|   4 | `test_linear11_round_trip`  |   ✅   | Linear11 round trip                                                                     |
|   5 | `test_linear16`             |   ✅   | 614 * 2^-9 is 1.19921875, which truncates to 1199218 micro-units.                       |
|   6 | `test_linear16_round_trip`  |   ✅   | Linear16 round trip                                                                     |
|   7 | `test_out_of_range_refused` |   ✅   | mantissa 1023 at exponent 15 is 33521664, which past the micro scaling leaves an int32. |
|   8 | `test_direct`               |   ✅   | Direct                                                                                  |
|   9 | `test_set_page_wire`        |   ✅   | Set page wire                                                                           |
|  10 | `test_read_linear11_wire`   |   ✅   | Read linear11 wire                                                                      |
|  11 | `test_clear_faults_wire`    |   ✅   | Clear faults wire                                                                       |
|  12 | `test_write_linear16_wire`  |   ✅   | Write linear16 wire                                                                     |

</details>

---

## test_bus_wire - native_bus_wire - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_End to end from the host harness through the library: a real driver is called, it runs through_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_sht3x_read_wire`                      |   ✅   | Sht3x read wire                      |
|   2 | `test_sht3x_bad_crc_rejected`               |   ✅   | Sht3x bad crc rejected               |
|   3 | `test_pca9685_set_pwm_wire`                 |   ✅   | Pca9685 set pwm wire                 |
|   4 | `test_pca9685_servo_wire`                   |   ✅   | Pca9685 servo wire                   |
|   5 | `test_ina219_wire_is_big_endian`            |   ✅   | Ina219 wire is big endian            |
|   6 | `test_rtc_read_wire`                        |   ✅   | Rtc read wire                        |
|   7 | `test_rtc_set_wire`                         |   ✅   | Rtc set wire                         |
|   8 | `test_smbus_pec_on_the_wire`                |   ✅   | Smbus pec on the wire                |
|   9 | `test_smbus_without_pec`                    |   ✅   | Smbus without pec                    |
|  10 | `test_smbus_word_is_little_endian`          |   ✅   | Smbus word is little endian          |
|  11 | `test_smbus_read_word_wire`                 |   ✅   | Smbus read word wire                 |
|  12 | `test_i2c_scan_probes_every_address`        |   ✅   | I2c scan probes every address        |
|  13 | `test_transfers_carry_their_address`        |   ✅   | Transfers carry their address        |
|  14 | `test_rtc_read_is_one_transaction`          |   ✅   | Rtc read is one transaction          |
|  15 | `test_pca9685_begin_settles_the_oscillator` |   ✅   | Pca9685 begin settles the oscillator |
|  16 | `test_failure_propagates`                   |   ✅   | Failure propagates                   |
|  17 | `test_spi_wire`                             |   ✅   | Spi wire                             |

</details>

---

## test_pca9685 - native_pca9685 - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the PCA9685 PWM/servo codec (services/peripherals/pca9685): the PRESCALE computation from a_

|   # | Test                              | Status | Description                                                           |
| --: | :-------------------------------- | :----: | :-------------------------------------------------------------------- |
|   1 | `test_prescale`                   |   ✅   | Prescale                                                              |
|   2 | `test_channel_reg`                |   ✅   | Channel reg                                                           |
|   3 | `test_us_to_count`                |   ✅   | Us to count                                                           |
|   4 | `test_set_pwm_bytes`              |   ✅   | channel 0, on=0, off=307 (0x133) -> reg 0x06, off_l 0x33, off_h 0x01. |
|   5 | `test_prescale_zero`              |   ✅   | Zero frequency takes the max-prescale early return.                   |
|   6 | `test_begin_sequence_on_the_wire` |   ✅   | Begin sequence on the wire                                            |
|   7 | `test_set_pwm_on_the_wire`        |   ✅   | Set pwm on the wire                                                   |

</details>

---

## test_ads1115 - native_ads1115 - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the ADS1115 ADC codec (services/peripherals/ads1115): building the 16-bit config word for a_

|   # | Test                                      | Status | Description                                                                                    |
| --: | :---------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_config_word`                        |   ✅   | ch0, +/-4.096V, 128 SPS: OS                                                                    | MUX_AIN0 | PGA1 | MODE_SINGLE | DR128 | COMP_DISABLE. |
|   2 | `test_config_fallbacks`                   |   ✅   | Out-of-range channel/gain/dr fall back to ch0 / +/-2.048V / 128 SPS = 0xC583.                  |
|   3 | `test_raw_to_uv`                          |   ✅   | gain 1 (+/-4.096 V) -> 125 uV/LSB.                                                             |
|   4 | `test_raw_to_uv_gain_clamp`               |   ✅   | An out-of-range gain code clamps to GAIN_2 (its FSR), so the conversion never indexes past the |
|   5 | `test_read_drives_the_bus`                |   ✅   | Read drives the bus                                                                            |
|   6 | `test_read_fails_when_the_part_is_silent` |   ✅   | Read fails when the part is silent                                                             |

</details>

---

## test_ina219 - native_ina219 - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the INA219 current/power codec (services/peripherals/ina219): decoding the bus-voltage_

|   # | Test                                 | Status | Description                                                              |
| --: | :----------------------------------- | :----: | :----------------------------------------------------------------------- |
|   1 | `test_bus_mv`                        |   ✅   | 3300 mV -> value 825 (0x339) in bits [15:3] -> register 825<<3 = 0x19C8. |
|   2 | `test_shunt_uv`                      |   ✅   | Shunt uv                                                                 |
|   3 | `test_calibration`                   |   ✅   | Calibration                                                              |
|   4 | `test_current_and_power`             |   ✅   | current = raw * current_LSB (uA); power = raw * 20 * current_LSB (uW).   |
|   5 | `test_begin_and_read_drive_the_bus`  |   ✅   | Begin and read drive the bus                                             |
|   6 | `test_reads_fail_closed_when_silent` |   ✅   | Reads fail closed when silent                                            |

</details>

---

## test_quic_varint - native_quic_varint - ✅ 3 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the QUIC variable-length integer codec (network_drivers/presentation/http/http3/_

|   # | Test                         | Status | Description                                                              |
| --: | :--------------------------- | :----: | :----------------------------------------------------------------------- |
|   1 | `test_rfc_examples`          |   ✅   | RFC 9000 Appendix A.1                                                    |
|   2 | `test_non_minimal_decode`    |   ✅   | The RFC's two-byte encoding of 37 must decode to 37 (consuming 2 bytes). |
|   3 | `test_boundaries_and_guards` |   ✅   | Length boundaries.                                                       |

</details>

---

## test_upload - native_upload - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Streaming file upload (PC_ENABLE_UPLOAD): a POST body is streamed straight_

|   # | Test                                   | Status | Description                     |
| --: | :------------------------------------- | :----: | :------------------------------ |
|   1 | `test_upload_streams_body_to_file`     |   ✅   | Upload streams body to file     |
|   2 | `test_small_body_single_chunk`         |   ✅   | Small body single chunk         |
|   3 | `test_empty_body_not_streamed`         |   ✅   | Empty body not streamed         |
|   4 | `test_non_post_body_rejected_by_begin` |   ✅   | Non post body rejected by begin |
|   5 | `test_wrong_path_rejected_by_begin`    |   ✅   | Wrong path rejected by begin    |
|   6 | `test_open_failure_replies_500`        |   ✅   | Open failure replies 500        |
|   7 | `test_null_dest_replies_500`           |   ✅   | Null dest replies 500           |
|   8 | `test_write_failure_replies_500`       |   ✅   | Write failure replies 500       |

</details>

---

## test_snmp_trap - native_snmp_trap - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host unit tests for the outbound SNMP notification builder (env:native_snmp_trap)._

|   # | Test                        | Status | Description          |
| --: | :-------------------------- | :----: | :------------------- |
|   1 | `test_trap_v2c_structure`   |   ✅   | Trap v2c structure   |
|   2 | `test_all_varbind_types`    |   ✅   | All varbind types    |
|   3 | `test_invalid_varbind_type` |   ✅   | Invalid varbind type |
|   4 | `test_build_v2c_null_args`  |   ✅   | Build v2c null args  |
|   5 | `test_host_transport_stubs` |   ✅   | Host transport stubs |
|   6 | `test_inform_tag`           |   ✅   | Inform tag           |
|   7 | `test_buffer_too_small`     |   ✅   | Buffer too small     |

</details>

---

## test_ssh_comp - native_ssh_comp - ✅ 25 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Integration test for SSH server-to-client compression WIRING (network_drivers/presentation/ssh):_

|   # | Test                                                    | Status | Description                                                                                     |
| --: | :------------------------------------------------------ | :----: | :---------------------------------------------------------------------------------------------- |
|   1 | `test_dispatch_all_switch_arms`                         |   ✅   | Dispatch all switch arms                                                                        |
|   2 | `test_dispatch_guard_and_error_arms`                    |   ✅   | Dispatch guard and error arms                                                                   |
|   3 | `test_delayed_activation`                               |   ✅   | Delayed activation                                                                              |
|   4 | `test_immediate_activation`                             |   ✅   | Immediate activation                                                                            |
|   5 | `test_none_never_activates`                             |   ✅   | None never activates                                                                            |
|   6 | `test_c2s_activation_and_decompress`                    |   ✅   | C2s activation and decompress                                                                   |
|   7 | `test_c2s_delayed_activation`                           |   ✅   | C2s delayed activation                                                                          |
|   8 | `test_packet_layer_stream_roundtrip`                    |   ✅   | Packet layer stream roundtrip                                                                   |
|   9 | `test_packet_layer_window_slide`                        |   ✅   | Packet layer window slide                                                                       |
|  10 | `test_packet_compress_scratch_exhausted`                |   ✅   | Packet compress scratch exhausted                                                               |
|  11 | `test_comp_slot_guards`                                 |   ✅   | Comp slot guards                                                                                |
|  12 | `test_comp_activation_idempotent`                       |   ✅   | zlib: NEWKEYS starts it; a second NEWKEYS is a no-op (s2c_active already true), and USERAUTH is |
|  13 | `test_kexinit_negotiates_s2c_compression`               |   ✅   | Kexinit negotiates s2c compression                                                              |
|  14 | `test_packet_send_uncompressed_before_activation`       |   ✅   | Packet send uncompressed before activation                                                      |
|  15 | `test_newkeys_sent_starts_immediate_stream_only`        |   ✅   | Newkeys sent starts immediate stream only                                                       |
|  16 | `test_packet_compress_rejects_oversized_payload`        |   ✅   | Packet compress rejects oversized payload                                                       |
|  17 | `test_dispatch_auth_success_starts_delayed_compression` |   ✅   | Dispatch auth success starts delayed compression                                                |
|  18 | `test_aes256ctr_nist_vector_roundtrip`                  |   ✅   | Aes256ctr nist vector roundtrip                                                                 |
|  19 | `test_aes256ctr_counter_full_wraparound`                |   ✅   | Aes256ctr counter full wraparound                                                               |
|  20 | `test_dh_generate_slot_guard_and_state`                 |   ✅   | Dh generate slot guard and state                                                                |
|  21 | `test_dh_derive_keys_default_wrapper_and_slot_guard`    |   ✅   | Dh derive keys default wrapper and slot guard                                                   |
|  22 | `test_dh_derive_keys_chachapoly_and_gcm_branches`       |   ✅   | Dh derive keys chachapoly and gcm branches                                                      |
|  23 | `test_kdf_mpint_k_edge_encodings`                       |   ✅   | Kdf mpint k edge encodings                                                                      |
|  24 | `test_kdf_string_k_hybrid_branch`                       |   ✅   | Kdf string k hybrid branch                                                                      |
|  25 | `test_kdf_out_len_clamp_matches_exact_max`              |   ✅   | Kdf out len clamp matches exact max                                                             |

</details>

---

## test_time_source - native_time_source - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the multi-source time fallback matrix (services/timing_position/time_source):_

|   # | Test                                            | Status | Description                                                                               |
| --: | :---------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_single_source`                            |   ✅   | Single source                                                                             |
|   2 | `test_priority_order_lowest_value_wins`         |   ✅   | Priority order lowest value wins                                                          |
|   3 | `test_falls_back_when_primary_unavailable`      |   ✅   | Falls back when primary unavailable                                                       |
|   4 | `test_all_unavailable_returns_zero`             |   ✅   | All unavailable returns zero                                                              |
|   5 | `test_first_valid_short_circuits`               |   ✅   | First valid short circuits                                                                |
|   6 | `test_fallback_queries_in_priority_order`       |   ✅   | Fallback queries in priority order                                                        |
|   7 | `test_table_full_rejects`                       |   ✅   | Table full rejects                                                                        |
|   8 | `test_null_fn_rejected`                         |   ✅   | Null fn rejected                                                                          |
|   9 | `test_table_full_all_unavailable_exhausts_scan` |   ✅   | Fill every slot (PC_TIME_SOURCE_MAX) with sources that all report no valid                |
|  10 | `test_reset_clears_sources`                     |   ✅   | Reset clears sources                                                                      |
|  11 | `test_http_date_from_active_source`             |   ✅   | The HTTP Date header draws from the registry: no valid source -> nothing; a source with a |

</details>

---

## test_telemetry - native_telemetry - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the telemetry math helpers (services/iot/telemetry): moving-window_

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_window_classic_stats`                     |   ✅   | Window classic stats                     |
|   2 | `test_window_empty`                             |   ✅   | Window empty                             |
|   3 | `test_window_single_sample`                     |   ✅   | Window single sample                     |
|   4 | `test_window_eviction`                          |   ✅   | Window eviction                          |
|   5 | `test_window_push_guards`                       |   ✅   | cap == 0, buf non-NULL.                  |
|   6 | `test_window_variance_clamps_negative_rounding` |   ✅   | Window variance clamps negative rounding |
|   7 | `test_rate_basic`                               |   ✅   | Rate basic                               |
|   8 | `test_rate_zero_dt`                             |   ✅   | Rate zero dt                             |
|   9 | `test_totalizer_constant_rate`                  |   ✅   | Totalizer constant rate                  |
|  10 | `test_totalizer_trapezoid_and_reset`            |   ✅   | Totalizer trapezoid and reset            |

</details>

---

## test_dashboard - native_dashboard - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the dashboard widget-table JSON serializers (services/web/dashboard_

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_layout_bar_sparkline_types`                         |   ✅   | Layout bar sparkline types                         |
|   2 | `test_null_widget_table_guards`                           |   ✅   | Null widget table guards                           |
|   3 | `test_json_overflow_paths`                                |   ✅   | Json overflow paths                                |
|   4 | `test_parse_control_edges`                                |   ✅   | Parse control edges                                |
|   5 | `test_layout_json`                                        |   ✅   | Layout json                                        |
|   6 | `test_values_json_initial_zero`                           |   ✅   | Values json initial zero                           |
|   7 | `test_set_and_values`                                     |   ✅   | Set and values                                     |
|   8 | `test_set_unknown_key`                                    |   ✅   | Set unknown key                                    |
|   9 | `test_configure_resets_values`                            |   ✅   | Configure resets values                            |
|  10 | `test_small_buffer_fails_closed`                          |   ✅   | Small buffer fails closed                          |
|  11 | `test_parse_control_ok`                                   |   ✅   | Parse control ok                                   |
|  12 | `test_parse_control_float`                                |   ✅   | Parse control float                                |
|  13 | `test_parse_control_rejects_malformed`                    |   ✅   | Parse control rejects malformed                    |
|  14 | `test_dispatch_control_invokes_cb`                        |   ✅   | Dispatch control invokes cb                        |
|  15 | `test_layout_control_types`                               |   ✅   | Layout control types                               |
|  16 | `test_null_widget_fields_are_skipped_and_serialize_empty` |   ✅   | Null widget fields are skipped and serialize empty |
|  17 | `test_serializers_null_out_pointer`                       |   ✅   | Serializers null out pointer                       |
|  18 | `test_parse_control_tab_whitespace`                       |   ✅   | Parse control tab whitespace                       |
|  19 | `test_parse_control_non_string_key_value`                 |   ✅   | Parse control non string key value                 |
|  20 | `test_parse_control_unterminated_key_runs_to_eof`         |   ✅   | Parse control unterminated key runs to eof         |
|  21 | `test_dispatch_control_no_callback_registered`            |   ✅   | Dispatch control no callback registered            |

</details>

---

## test_iface - native_phy_iface - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for per-route STA/AP interface filters (PC::on(..., pc_if_kind))._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_add_registers_and_reports`              |   ✅   | Add registers and reports              |
|   2 | `test_unregistered_id_reads_as_absent`        |   ✅   | Unregistered id reads as absent        |
|   3 | `test_duplicate_id_is_refused`                |   ✅   | Duplicate id is refused                |
|   4 | `test_null_send_is_refused`                   |   ✅   | Null send is refused                   |
|   5 | `test_table_full_is_fail_closed`              |   ✅   | Table full is fail closed              |
|   6 | `test_reset_empties_the_registry`             |   ✅   | Reset empties the registry             |
|   7 | `test_send_carries_the_id_and_the_ctx`        |   ✅   | Send carries the id and the ctx        |
|   8 | `test_send_to_an_unregistered_id_fails`       |   ✅   | Send to an unregistered id fails       |
|   9 | `test_send_reports_a_refusing_interface`      |   ✅   | Send reports a refusing interface      |
|  10 | `test_at_walks_rows_and_marks_the_empty_ones` |   ✅   | At walks rows and marks the empty ones |
|  11 | `test_mixed_kinds_coexist`                    |   ✅   | Mixed kinds coexist                    |

</details>

---

## test_net_egress - native_net_egress - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for egress-interface reporting (network_drivers/physical). The lwIP_

|   # | Test                                           | Status | Description                                                                                |
| --: | :--------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_mac_readouts_leave_the_buffer_untouched` |   ✅   | Mac readouts leave the buffer untouched                                                    |
|   2 | `test_radio_control_host_stub`                 |   ✅   | The L1 entry points themselves, not the Radio table that wraps them: RadioNs is incomplete |
|   3 | `test_layer_handle_carries_every_child`        |   ✅   | Layer handle carries every child                                                           |
|   4 | `test_classify_is_bound_to_the_classifier`     |   ✅   | Each arm returns a different kind, so no other member could stand in for this one.         |
|   5 | `test_classify_sta`                            |   ✅   | Classify sta                                                                               |
|   6 | `test_classify_ap`                             |   ✅   | Classify ap                                                                                |
|   7 | `test_classify_eth`                            |   ✅   | Classify eth                                                                               |
|   8 | `test_classify_none`                           |   ✅   | Classify none                                                                              |
|   9 | `test_egress_host_stub`                        |   ✅   | Egress host stub                                                                           |
|  10 | `test_eth_host_stub`                           |   ✅   | Eth host stub                                                                              |
|  11 | `test_wifi_bringup_host_stub`                  |   ✅   | Wifi bringup host stub                                                                     |
|  12 | `test_ipv6_host_stub`                          |   ✅   | Ipv6 host stub                                                                             |
|  13 | `test_radio_readouts_host_stub`                |   ✅   | Radio readouts host stub                                                                   |

</details>

---

## test_partition_monitor - native_partition - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the partition-map core (services/storage/partition_monitor): the_

|   # | Test                                              | Status | Description                                                              |
| --: | :------------------------------------------------ | :----: | :----------------------------------------------------------------------- |
|   1 | `test_kind_app`                                   |   ✅   | Kind app                                                                 |
|   2 | `test_kind_data`                                  |   ✅   | Kind data                                                                |
|   3 | `test_json`                                       |   ✅   | Json                                                                     |
|   4 | `test_json_small_buffer_fails_closed`             |   ✅   | Json small buffer fails closed                                           |
|   5 | `test_collect_host_stub`                          |   ✅   | Collect host stub                                                        |
|   6 | `test_partition_kind_data_subtypes`               |   ✅   | Partition kind data subtypes                                             |
|   7 | `test_json_null_out_and_zero_cap`                 |   ✅   | out == NULL fails closed before touching the buffer.                     |
|   8 | `test_json_null_parts`                            |   ✅   | Json null parts                                                          |
|   9 | `test_json_entry_overflow_fails_closed`           |   ✅   | 20 bytes fits the opening `{"partitions":[` (15 chars) but not the first |
|  10 | `test_json_closing_bracket_overflow_fails_closed` |   ✅   | 107 bytes fits the opening bracket + the one entry (106 bytes total) but |

</details>

---

## test_guardrails - native_guardrails - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the guardrails core (services/security/guardrails): the threshold_

|   # | Test                                  | Status | Description                                                                                    |
| --: | :------------------------------------ | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_eval_all_clear`                 |   ✅   | Eval all clear                                                                                 |
|   2 | `test_eval_heap_breach`               |   ✅   | Eval heap breach                                                                               |
|   3 | `test_eval_frag_and_stack`            |   ✅   | Eval frag and stack                                                                            |
|   4 | `test_eval_all_breached`              |   ✅   | Eval all breached                                                                              |
|   5 | `test_json`                           |   ✅   | Json                                                                                           |
|   6 | `test_json_small_buffer_fails_closed` |   ✅   | Json small buffer fails closed                                                                 |
|   7 | `test_eval_null_health_is_clear`      |   ✅   | A null health snapshot reports no breach (nothing to evaluate).                                |
|   8 | `test_json_guards_fail_closed`        |   ✅   | Null out or zero cap -> 0 (nothing written).                                                   |
|   9 | `test_host_sampler_stubs`             |   ✅   | On host there are no live counters: sample() zeroes the snapshot (and no-ops on null), begin() |

</details>

---

## test_failsafe - native_failsafe - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/failsafe: the software watchdog / deadlock detector. Uses the explicit_

|   # | Test                                         | Status | Description                                                                                   |
| --: | :------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_overdue_predicate`                     |   ✅   | Overdue predicate                                                                             |
|   2 | `test_register_and_not_overdue_when_fresh`   |   ✅   | Register and not overdue when fresh                                                           |
|   3 | `test_breach_fires_once_then_clears_on_feed` |   ✅   | b has a huge deadline so it never trips during this test - a stays the only overdue lifeline. |
|   4 | `test_registry_full`                         |   ✅   | Registry full                                                                                 |
|   5 | `test_feed_bad_id`                           |   ✅   | Feed bad id                                                                                   |
|   6 | `test_breach_without_callback`               |   ✅   | Breach without callback                                                                       |
|   7 | `test_json`                                  |   ✅   | Json                                                                                          |
|   8 | `test_json_null_out_and_zero_cap`            |   ✅   | Json null out and zero cap                                                                    |
|   9 | `test_json_unnamed_lifeline`                 |   ✅   | Json unnamed lifeline                                                                         |
|  10 | `test_json_truncated_buffer`                 |   ✅   | Json truncated buffer                                                                         |
|  11 | `test_millis_wrappers_and_json`              |   ✅   | Millis wrappers and json                                                                      |

</details>

---

## test_sleep_sched - native_sleep_sched - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/sleep_sched: the dynamic sleep-cycle decision core. Pure, synthetic clock._

|   # | Test                                               | Status | Description                                                                                  |
| --: | :------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_awake_when_recent`                           |   ✅   | idle 999 < 1000 -> stay awake.                                                               |
|   2 | `test_min_window_at_threshold`                     |   ✅   | idle exactly 1000: past threshold, 0 doublings -> the floor.                                 |
|   3 | `test_ramp_doubles`                                |   ✅   | idle 1500: one ramp period (500) past threshold -> 100<<1 = 200.                             |
|   4 | `test_clamps_to_ceiling`                           |   ✅   | idle 10000: many periods, clamped to max_ms = 2000 (not 100<<18).                            |
|   5 | `test_no_ramp_jumps_to_ceiling`                    |   ✅   | No ramp jumps to ceiling                                                                     |
|   6 | `test_degenerate_max_below_min`                    |   ✅   | Degenerate max below min                                                                     |
|   7 | `test_wrap_safe`                                   |   ✅   | last_active just before the millis() rollover, now just after: real idle 1284 >= 1000.       |
|   8 | `test_null_cfg`                                    |   ✅   | Null cfg                                                                                     |
|   9 | `test_zero_min_and_max_clamps_seed_window_down`    |   ✅   | min_ms=0 -> the "or 1" seed kicks in (window starts at 1); max_ms=0 too, so ceil_ms=0.       |
|  10 | `test_window_hits_ceiling_exactly_before_doubling` |   ✅   | min_ms=4, max_ms=8: window doubles 4 -> 8 on the first iteration (4 is not > ceil_ms/2 == 4, |

</details>

---

## test_wearlevel - native_wearlevel - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for server/filesystem/wearlevel: the flash wear-leveling slot selector._

|   # | Test                                     | Status | Description                                                                        |
| --: | :--------------------------------------- | :----: | :--------------------------------------------------------------------------------- |
|   1 | `test_pick_least_worn_ties_lowest_index` |   ✅   | Pick least worn ties lowest index                                                  |
|   2 | `test_pick_edge`                         |   ✅   | Pick edge                                                                          |
|   3 | `test_pick_plus_mark_levels_the_region`  |   ✅   | Repeated pick+mark must keep every slot within 1 of the others (round-robin wear). |
|   4 | `test_mark_saturates_and_bounds`         |   ✅   | Mark saturates and bounds                                                          |
|   5 | `test_spread`                            |   ✅   | Spread                                                                             |

</details>

---

## test_netadapt - native_netadapt - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/netadapt: TCP window sizing by free RAM + DHCP->static fallback._

|   # | Test                                   | Status | Description                                                              |
| --: | :------------------------------------- | :----: | :----------------------------------------------------------------------- |
|   1 | `test_window_floor_when_low_heap`      |   ✅   | heap at or below the reserve -> the floor.                               |
|   2 | `test_window_scales_with_heap`         |   ✅   | (free - reserve)/4, clamped. free=40000, reserve=8000 -> 32000/4 = 8000. |
|   3 | `test_window_clamps_to_ceiling`        |   ✅   | Huge heap -> clamped to max_win.                                         |
|   4 | `test_window_degenerate_max_below_min` |   ✅   | Window degenerate max below min                                          |
|   5 | `test_dhcp_fallback_on_timeout`        |   ✅   | Dhcp fallback on timeout                                                 |
|   6 | `test_dhcp_fallback_on_attempts`       |   ✅   | Dhcp fallback on attempts                                                |

</details>

---

## test_dshot - native_dshot - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/dshot: the DShot ESC throttle frame codec (hand-computed vectors)._

|   # | Test                                    | Status | Description                                                                                     |
| --: | :-------------------------------------- | :----: | :---------------------------------------------------------------------------------------------- |
|   1 | `test_encode_known_vector`              |   ✅   | Encode known vector                                                                             |
|   2 | `test_encode_telemetry_bit`             |   ✅   | value 1046, telemetry set: v12 = 0x82D, nibbles 8^2^D = 7, frame = 0x82D7.                      |
|   3 | `test_encode_bidirectional_inverts_crc` |   ✅   | Same value, bidirectional: crc = ~6 & 0xF = 9, frame = 0x82C9.                                  |
|   4 | `test_value_masked_to_11_bits`          |   ✅   | 0xF000                                                                                          | 1046: the high bits are dropped to the 11-bit field -> same as 1046. |
|   5 | `test_decode_roundtrip_and_crc`         |   ✅   | Decode roundtrip and crc                                                                        |
|   6 | `test_decode_null_out_params`           |   ✅   | A valid frame decodes successfully even when the caller doesn't want the value or telemetry bit |
|   7 | `test_bit_timing`                       |   ✅   | 600 kbit: period 1667 ns; "1" ~3/4, "0" ~3/8.                                                   |
|   8 | `test_esc_pwm_mapping`                  |   ✅   | OneShot125: 125..250 us.                                                                        |
|   9 | `test_bit_ns_all_rates`                 |   ✅   | Each supported line rate maps to a non-zero bit period; an unknown rate is rejected.            |

</details>

---

## test_j2735 - native_j2735 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/j2735: the ASN.1 UPER primitive codec + the BSMcore block._

|   # | Test                               | Status | Description                                                                                    |
| --: | :--------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_cint_bits`                   |   ✅   | Cint bits                                                                                      |
|   2 | `test_bit_writer_pattern`          |   ✅   | Write 0b101 (3 bits) then 0b11 (2 bits): stream 10111 000 -> 0xB8.                             |
|   3 | `test_writer_null_and_zero`        |   ✅   | A null buffer (or zero cap) leaves the writer not-ok and must not dereference it.              |
|   4 | `test_cint_roundtrip`              |   ✅   | Cint roundtrip                                                                                 |
|   5 | `test_bsm_core_roundtrip`          |   ✅   | Bsm core roundtrip                                                                             |
|   6 | `test_bsm_core_bit_length`         |   ✅   | msgCnt 7 + id 32 + secMark 16 + lat 31 + long 32 + elev 16 + speed 13 + heading 15 = 162 bits  |
|   7 | `test_spat_roundtrip`              |   ✅   | Spat roundtrip                                                                                 |
|   8 | `test_spat_decode_too_many`        |   ✅   | Only room for 1 but 2 encoded -> false.                                                        |
|   9 | `test_map_roundtrip`               |   ✅   | Map roundtrip                                                                                  |
|  10 | `test_uper_overflow_and_bsm_guard` |   ✅   | Uper overflow and bsm guard                                                                    |
|  11 | `test_j2735_guards_and_truncation` |   ✅   | pc_uper_put_cint / pc_uper_get_cint with a single-value (zero-bit) range: nothing on the wire. |
|  12 | `test_j2735_extra_branch_coverage` |   ✅   | pc_uper_put_bits: nbits == 0 on an otherwise-ok writer is a no-op (the guard's second operand, |

</details>

---

## test_sep2 - native_sep2 - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/sep2: the IEEE 2030.5 resource document builders._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_device_capability`                       |   ✅   | Device capability                       |
|   2 | `test_end_device`                              |   ✅   | End device                              |
|   3 | `test_der_control_negative_setpoint`           |   ✅   | Der control negative setpoint           |
|   4 | `test_xml_escape_in_href`                      |   ✅   | Xml escape in href                      |
|   5 | `test_overflow`                                |   ✅   | Overflow                                |
|   6 | `test_device_capability_null_out_and_zero_cap` |   ✅   | Device capability null out and zero cap |
|   7 | `test_end_device_null_out_and_zero_cap`        |   ✅   | End device null out and zero cap        |
|   8 | `test_der_control_null_out_and_zero_cap`       |   ✅   | Der control null out and zero cap       |

</details>

---

## test_ntcip - native_ntcip - ✅ 4 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/ntcip: the NTCIP object OID definitions + the OID builder._

|   # | Test                                | Status | Description                                       |
| --: | :---------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_roots_under_nema`             |   ✅   | Every NTCIP object is under 1.3.6.1.4.1.1206.4.2. |
|   2 | `test_oid_builder_scalar_and_index` |   ✅   | A scalar takes .0.                                |
|   3 | `test_oid_builder_overflow`         |   ✅   | Oid builder overflow                              |
|   4 | `test_oid_builder_invalid_args`     |   ✅   | NULL root.                                        |

</details>

---

## test_openadr - native_openadr - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/openadr: the OpenADR 3.0 event / report JSON builders._

|   # | Test                                                    | Status | Description                                                                       |
| --: | :------------------------------------------------------ | :----: | :-------------------------------------------------------------------------------- |
|   1 | `test_event`                                            |   ✅   | Event                                                                             |
|   2 | `test_report_negative_value`                            |   ✅   | Report negative value                                                             |
|   3 | `test_json_escape`                                      |   ✅   | Json escape                                                                       |
|   4 | `test_overflow`                                         |   ✅   | Overflow                                                                          |
|   5 | `test_openadr_escape_and_overflow`                      |   ✅   | Openadr escape and overflow                                                       |
|   6 | `test_openadr_null_program_and_count_without_intervals` |   ✅   | NULL program_id/event_name exercise put_json_str's `s ? s : ""` defensive branch. |

</details>

---

## test_interbus - native_interbus - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/interbus: the summation-frame codec._

|   # | Test                                 | Status | Description                                                            |
| --: | :----------------------------------- | :----: | :--------------------------------------------------------------------- |
|   1 | `test_fcs_check_vector`              |   ✅   | CRC-16/CCITT-FALSE check value: CRC of "123456789" = 0x29B1.           |
|   2 | `test_build_and_parse`               |   ✅   | Three device slices: 0x1111, 0x2222, 0x3333.                           |
|   3 | `test_empty_frame`                   |   ✅   | Empty frame                                                            |
|   4 | `test_parse_rejects`                 |   ✅   | Corrupt FCS.                                                           |
|   5 | `test_build_parse_guards`            |   ✅   | Build parse guards                                                     |
|   6 | `test_parse_rejects_odd_word_region` |   ✅   | Loopback word valid, but the region between loopback and FCS is an odd |

</details>

---

## test_atc - native_atc - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/atc: the ATC field-I/O interop snapshot._

|   # | Test                                            | Status | Description                                                                           |
| --: | :---------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_snapshot_json`                            |   ✅   | Snapshot json                                                                         |
|   2 | `test_set_output`                               |   ✅   | Set an output.                                                                        |
|   3 | `test_get`                                      |   ✅   | Get                                                                                   |
|   4 | `test_empty_and_overflow`                       |   ✅   | Empty and overflow                                                                    |
|   5 | `test_json_escapes_and_overflow`                |   ✅   | Json escapes and overflow                                                             |
|   6 | `test_atc_null_and_missing_args`                |   ✅   | pc_atc_snapshot_json: null io / null out / (count>0 && !points) all fail closed.      |
|   7 | `test_atc_null_name_point_and_multidigit_value` |   ✅   | A point with a null name renders as an empty JSON string and is safely skipped (never |
|   8 | `test_strbuf_xml_and_json_direct`               |   ✅   | pc_sb_xml: all four escapes (&,<,>,") plus literal passthrough chars, in one pass.    |

</details>

---

## test_southbound - native_southbound - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/southbound: the driver registry + name-dispatched read/write facade._

|   # | Test                                       | Status | Description                                                                                  |
| --: | :----------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_register_and_find`                   |   ✅   | Register and find                                                                            |
|   2 | `test_read_write_dispatch`                 |   ✅   | Read write dispatch                                                                          |
|   3 | `test_block_atomic`                        |   ✅   | Block atomic                                                                                 |
|   4 | `test_unsupported_capability`              |   ✅   | A driver that only implements single-point read.                                             |
|   5 | `test_registry_full`                       |   ✅   | Fill the registry with distinct-named drivers, then overflow.                                |
|   6 | `test_dispatch_not_found_guards`           |   ✅   | Dispatch not found guards                                                                    |
|   7 | `test_find_null_name`                      |   ✅   | pc_southbound_find's own null-name guard, independent of any dispatch caller.                |
|   8 | `test_read_missing_capability`             |   ✅   | A driver that implements write but not read, to hit pc_southbound_read's                     |
|   9 | `test_find_skips_driver_mutated_name_null` |   ✅   | pc_southbound_find() stores a _borrowed_ pointer (const SouthboundDriver *), not a copy: the |
|  10 | `test_block_not_found_and_arg_edges`       |   ✅   | Block not found and arg edges                                                                |

</details>

---

## test_exc_decoder - native_exc_decoder - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/exc_decoder: parsing a real ESP32 Guru Meditation panic dump._

|   # | Test                                            | Status | Description                                                                                         |
| --: | :---------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------------- |
|   1 | `test_exc_edge_guards`                          |   ✅   | Exc edge guards                                                                                     |
|   2 | `test_parse_full`                               |   ✅   | Parse full                                                                                          |
|   3 | `test_json`                                     |   ✅   | Json                                                                                                |
|   4 | `test_backtrace_only_and_corrupted`             |   ✅   | No register dump: PC must fall back to the first backtrace frame. Trailing corruption marker.       |
|   5 | `test_garbage_returns_false`                    |   ✅   | Garbage returns false                                                                               |
|   6 | `test_json_omits_core_when_absent_and_overflow` |   ✅   | Json omits core when absent and overflow                                                            |
|   7 | `test_upper_hex_and_json_overflow`              |   ✅   | Uppercase hex addresses exercise the A-F branch of the nibble parser.                               |
|   8 | `test_hex_literal_rejections`                   |   ✅   | parse_hex refuses anything that is not "0x"/"0X" + at least one hex digit, and stops at 8 digits.   |
|   9 | `test_field_without_colon_or_value`             |   ✅   | A recognized field name with no ':' after it, and one whose value will not parse, are both ignored. |
|  10 | `test_core_field_variants`                      |   ✅   | "Core " followed by a non-digit leaves core at -1; a digit run ends at the first non-digit.         |
|  11 | `test_multi_digit_core_in_json`                 |   ✅   | A two-digit core exercises the multi-iteration decimal emitter.                                     |
|  12 | `test_cause_truncation`                         |   ✅   | The cause is bounded by the field width, and an unterminated cause stops at end-of-string.          |
|  13 | `test_backtrace_frame_cap_and_separator`        |   ✅   | The frame list stops at PC_EXC_MAX_FRAMES even when more pairs follow.                              |
|  14 | `test_parse_true_on_zero_pc_frame`              |   ✅   | A single frame whose pc is 0 still counts as a successful parse (frame_count carries it).           |

</details>

---

## test_http_delivery - native_http_delivery - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/http_delivery: RFC 5861 stale-while-revalidate (decision + header) and_

|   # | Test                                   | Status | Description                     |
| --: | :------------------------------------- | :----: | :------------------------------ |
|   1 | `test_builder_edge_guards`             |   ✅   | Builder edge guards             |
|   2 | `test_swr_decision`                    |   ✅   | max-age=60, swr=30.             |
|   3 | `test_cache_control`                   |   ✅   | Cache control                   |
|   4 | `test_sw_manifest`                     |   ✅   | Sw manifest                     |
|   5 | `test_manifest_fits_the_served_buffer` |   ✅   | Manifest fits the served buffer |
|   6 | `test_delivery_guards_and_escape`      |   ✅   | Delivery guards and escape      |

</details>

---

## test_hw_health - native_hw_health - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/hw_health: rail droop, SPI CRC backoff, GPIO short, cap leakage._

|   # | Test                                                  | Status | Description                                                                    |
| --: | :---------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_hwhealth_null_guards_and_init_clamps`           |   ✅   | Hwhealth null guards and init clamps                                           |
|   2 | `test_hwhealth_trip_defaults_overflow_and_band_clamp` |   ✅   | fail_trip=0 / ok_trip=0 default to 1 (ternary false branch): trips on the very |
|   3 | `test_rail_monitor`                                   |   ✅   | Rail monitor                                                                   |
|   4 | `test_spi_backoff`                                    |   ✅   | Spi backoff                                                                    |
|   5 | `test_spi_backoff_clamps`                             |   ✅   | Spi backoff clamps                                                             |
|   6 | `test_gpio_short`                                     |   ✅   | Gpio short                                                                     |
|   7 | `test_cap_leak`                                       |   ✅   | Expected 100ms decay, 10% tolerance -> [90, 110].                              |
|   8 | `test_rail_ok_spi_clamps_probes`                      |   ✅   | Rail ok spi clamps probes                                                      |

</details>

---

## test_mdns_adaptive - native_mdns_adaptive - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/mdns_adaptive: RF-aware backoff, TTL refresher, auto-sleep beacon._

|   # | Test                                                 | Status | Description                                                                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_refresh_interval`                              |   ✅   | Refresh interval                                                                              |
|   2 | `test_backoff_and_recover`                           |   ✅   | Backoff and recover                                                                           |
|   3 | `test_due`                                           |   ✅   | Due                                                                                           |
|   4 | `test_presleep`                                      |   ✅   | Presleep                                                                                      |
|   5 | `test_refresh_interval_overflow`                     |   ✅   | ttl_s large enough that ttl_s * 1000 / 2 overflows a uint32_t -> clamp to UINT32_MAX.         |
|   6 | `test_beacon_init_clamps_and_defaults`               |   ✅   | max_ms below base_ms: the ceiling clamps up to the floor.                                     |
|   7 | `test_beacon_adapt_overflow_clamps_to_ceiling`       |   ✅   | base_ms picked so doubling overflows a uint32_t (the shifted value wraps below cur_ms).       |
|   8 | `test_beacon_null_guards`                            |   ✅   | Beacon null guards                                                                            |
|   9 | `test_refresh_interval_and_beacon`                   |   ✅   | Refresh interval and beacon                                                                   |
|  10 | `test_contention_no_sample_before_the_window`        |   ✅   | Contention no sample before the window                                                        |
|  11 | `test_contention_reports_the_window_delta`           |   ✅   | Contention reports the window delta                                                           |
|  12 | `test_contention_delta_is_per_window_not_cumulative` |   ✅   | Contention delta is per window not cumulative                                                 |
|  13 | `test_contention_saturates_at_uint16`                |   ✅   | Contention saturates at uint16                                                                |
|  14 | `test_contention_frame_counter_wrap`                 |   ✅   | The promiscuous counter is uint32 and will eventually wrap. A window straddling the wrap must |
|  15 | `test_contention_clock_wrap`                         |   ✅   | The millis clock wraps too; the window-elapsed test is modular, so a window straddling the    |
|  16 | `test_contention_zero_window_defaults`               |   ✅   | Contention zero window defaults                                                               |
|  17 | `test_contention_null_is_safe`                       |   ✅   | Contention null is safe                                                                       |
|  18 | `test_contention_drives_the_beacon`                  |   ✅   | Contention drives the beacon                                                                  |

</details>

---

## test_sockpool - native_sockpool - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/sockpool: the LRU connection-slot recycling pool._

|   # | Test                                              | Status | Description                                                                                 |
| --: | :------------------------------------------------ | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_acquire_free`                               |   ✅   | Acquire free                                                                                |
|   2 | `test_lru_recycle`                                |   ✅   | Fill: id 100@t10, 101@t20, 102@t30.                                                         |
|   3 | `test_touch_changes_lru`                          |   ✅   | Touch changes lru                                                                           |
|   4 | `test_release_reopens_free`                       |   ✅   | Release reopens free                                                                        |
|   5 | `test_empty_pool_fails`                           |   ✅   | Empty pool fails                                                                            |
|   6 | `test_null_guard_subconditions`                   |   ✅   | Null guard subconditions                                                                    |
|   7 | `test_acquire_null_pool_and_nonnull_slots_zero_n` |   ✅   | Null pool pointer -> FAIL (the acquire-specific null-pool branch; not exercised elsewhere). |
|   8 | `test_acquire_recycle_with_null_evicted_id`       |   ✅   | Fill the pool, then force a recycle while passing evicted_id == NULL, exercising the        |
|   9 | `test_touch_guard_subconditions`                  |   ✅   | Valid pool pointer but null slots array -> no-op (p->slots branch).                         |
|  10 | `test_release_guard_subconditions`                |   ✅   | Null pool pointer -> false (release-specific null-pool branch; not exercised elsewhere).    |
|  11 | `test_find_and_in_use_with_null_slots`            |   ✅   | Valid pool pointer but null slots array -> exercises the p->slots branch in both            |

</details>

---

## test_psram_pool - native_psram_pool - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/psram_pool: DRAM/PSRAM placement policy + DMA ping-pong bookkeeping._

|   # | Test                             | Status | Description                                                                                |
| --: | :------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_place_large_prefers_psram` |   ✅   | 64KB asset, threshold 4KB, plenty of both heaps, 32KB DRAM reserve.                        |
|   2 | `test_place_small_prefers_dram`  |   ✅   | 512B hot buffer, threshold 4KB -> DRAM.                                                    |
|   3 | `test_place_dma_forces_dram`     |   ✅   | DMA-required buffer must be DRAM even if large.                                            |
|   4 | `test_place_edges`               |   ✅   | Place edges                                                                                |
|   5 | `test_place_small_neither_fits`  |   ✅   | small / hot buffer: DRAM too tight (reserve dominates) AND PSRAM too small -> FAIL.        |
|   6 | `test_pingpong`                  |   ✅   | Pingpong                                                                                   |
|   7 | `test_pingpong_null_safety`      |   ✅   | Every pc_pingpong_* accessor guards against a null PingPong* and returns a fixed fallback. |

</details>

---

## test_link_manager - native_link_manager - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/link_manager: egress selection, graceful escalation, failover._

|   # | Test                                             | Status | Description                                                                              |
| --: | :----------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_init_none_up`                              |   ✅   | Init none up                                                                             |
|   2 | `test_escalation_and_failover`                   |   ✅   | WiFi STA comes up first -> it becomes active.                                            |
|   3 | `test_tie_break_lower_index`                     |   ✅   | Two interfaces at equal priority: the lower index wins.                                  |
|   4 | `test_select_escalates_to_later_higher_priority` |   ✅   | Both up, but the higher priority sits at the _later_ index: the scan must still pick it, |
|   5 | `test_out_of_range_no_change`                    |   ✅   | Out of range no change                                                                   |
|   6 | `test_select_null_guards`                        |   ✅   | Select null guards                                                                       |
|   7 | `test_init_and_active_null`                      |   ✅   | Init and active null                                                                     |
|   8 | `test_set_guard_paths`                           |   ✅   | Null manager: reports -1 for both previous and new active, returns false.                |

</details>

---

## test_cc1101 - native_cc1101 - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CC1101 driver (services/radio/cc1101) against a mock chip emulating the SPI header_

|   # | Test                                   | Status | Description                                             |
| --: | :------------------------------------- | :----: | :------------------------------------------------------ |
|   1 | `test_init_configures_and_detects`     |   ✅   | Init configures and detects                             |
|   2 | `test_init_fails_when_absent`          |   ✅   | Init fails when absent                                  |
|   3 | `test_send_writes_fifo_and_strobes_tx` |   ✅   | Send writes fifo and strobes tx                         |
|   4 | `test_send_rejects_bad_len`            |   ✅   | Send rejects bad len                                    |
|   5 | `test_tx_done`                         |   ✅   | Tx done                                                 |
|   6 | `test_set_rx`                          |   ✅   | Set rx                                                  |
|   7 | `test_recv_reads_packet_and_rssi`      |   ✅   | FIFO: [len=3][A][B][C][rssi_raw][lqi]; RXBYTES = 6.     |
|   8 | `test_recv_empty`                      |   ✅   | Recv empty                                              |
|   9 | `test_recv_truncates`                  |   ✅   | Recv truncates                                          |
|  10 | `test_rssi_decode`                     |   ✅   | TI formula: raw>=128 -> (raw-256)/2-74 ; else raw/2-74. |
|  11 | `test_send_guard_subconditions`        |   ✅   | Send guard subconditions                                |
|  12 | `test_init_null_args`                  |   ✅   | Init null args                                          |
|  13 | `test_init_no_regs`                    |   ✅   | Init no regs                                            |
|  14 | `test_tx_done_null_args`               |   ✅   | Tx done null args                                       |
|  15 | `test_set_rx_null_args`                |   ✅   | Set rx null args                                        |
|  16 | `test_recv_null_args`                  |   ✅   | Recv null args                                          |
|  17 | `test_recv_bad_length`                 |   ✅   | Zero length byte with bytes waiting.                    |
|  18 | `test_send_null_spi`                   |   ✅   | Send null spi                                           |

</details>

---

## test_fdc2214 - native_fdc2214 - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/fdc2214: the capacitance-to-digital codec (data combine, error flags,_

|   # | Test                          | Status | Description                                                                       |
| --: | :---------------------------- | :----: | :-------------------------------------------------------------------------------- |
|   1 | `test_data_combine`           |   ✅   | MSB register: error flags 0x3 in top nibble, data MSB 0xABC; LSB register 0x1234. |
|   2 | `test_freq_scale`             |   ✅   | data = 2^27 (half scale), fref = 40 MHz -> f_sensor = 20 MHz.                     |
|   3 | `test_build_config`           |   ✅   | Build config                                                                      |
|   4 | `test_build_config_too_small` |   ✅   | Build config too small                                                            |
|   5 | `test_build_config_null_buf`  |   ✅   | buf == NULL must be rejected before the capacity check is even reached.           |

</details>

---

## test_ldc1614 - native_ldc1614 - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/ldc1614: the inductance-to-digital codec (data combine, error flags,_

|   # | Test                          | Status | Description            |
| --: | :---------------------------- | :----: | :--------------------- |
|   1 | `test_data_combine`           |   ✅   | Data combine           |
|   2 | `test_freq_scale`             |   ✅   | Freq scale             |
|   3 | `test_build_config`           |   ✅   | Build config           |
|   4 | `test_build_config_too_small` |   ✅   | Build config too small |
|   5 | `test_build_config_null_buf`  |   ✅   | Build config null buf  |

</details>

---

## test_vl53l0x - native_vl53l0x - ✅ 3 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/vl53l0x: the ToF ranging codec (range combine, data-ready, range status)._

|   # | Test                | Status | Description                                                                |
| --: | :------------------ | :----: | :------------------------------------------------------------------------- |
|   1 | `test_range_mm`     |   ✅   | Range mm                                                                   |
|   2 | `test_data_ready`   |   ✅   | Data ready                                                                 |
|   3 | `test_range_status` |   ✅   | DeviceRangeStatus = 11 (valid) in bits 6:3 -> register value 11<<3 = 0x58. |

</details>

---

## test_radio_sniff - native_radio_sniff - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/radio_sniff: the int->float32 RSSI encode and the 802.15.4 TAP pcap record._

|   # | Test                        | Status | Description                           |
| --: | :-------------------------- | :----: | :------------------------------------ |
|   1 | `test_i2f32`                |   ✅   | I2f32                                 |
|   2 | `test_i2f32_wide_magnitude` |   ✅   |                                       | dbm | >= 2^23 takes the "highest bit at/above the mantissa width" leg of the mantissa |
|   3 | `test_global_header`        |   ✅   | Global header                         |
|   4 | `test_tap_record`           |   ✅   | record(16) + tap(20) + frame(5) = 41. |
|   5 | `test_tap_record_overflow`  |   ✅   | Tap record overflow                   |
|   6 | `test_tap_record_bad_args`  |   ✅   | out == NULL.                          |

</details>

---

## test_tls_policy - native_tls_policy - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/tls_policy: version negotiation, cipher selection, AEAD classification._

|   # | Test                           | Status | Description                                                                                  |
| --: | :----------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_negotiate_version`       |   ✅   | Server supports 1.2..1.3.                                                                    |
|   2 | `test_version_name`            |   ✅   | Version name                                                                                 |
|   3 | `test_select_cipher`           |   ✅   | Server prefers ECDHE_RSA_AES_128_GCM then CHACHA20; client offers CHACHA20 + a legacy suite. |
|   4 | `test_select_cipher_null_args` |   ✅   | Null client_offered -> 0, defensive early-out.                                               |
|   5 | `test_is_aead`                 |   ✅   | Is aead                                                                                      |

</details>

---

## test_power_mgmt - native_power_mgmt - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the SoC power governor (server/power_mgmt): load-based scaling, the thermal_

|   # | Test                                                             | Status | Description                                                                                  |
| --: | :--------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_idle_runs_at_the_floor`                                    |   ✅   | Idle runs at the floor                                                                       |
|   2 | `test_busy_runs_at_the_ceiling`                                  |   ✅   | Busy runs at the ceiling                                                                     |
|   3 | `test_busy_threshold_is_inclusive`                               |   ✅   | Busy threshold is inclusive                                                                  |
|   4 | `test_load_above_100_is_clamped_not_wrapped`                     |   ✅   | Load above 100 is clamped not wrapped                                                        |
|   5 | `test_hot_die_throttles_even_when_busy`                          |   ✅   | Hot die throttles even when busy                                                             |
|   6 | `test_throttle_threshold_is_inclusive`                           |   ✅   | Throttle threshold is inclusive                                                              |
|   7 | `test_throttle_holds_between_the_two_thresholds`                 |   ✅   | 75 C is below the throttle point but above the restore point: once throttled it must stay    |
|   8 | `test_throttle_releases_at_the_cool_threshold`                   |   ✅   | Throttle releases at the cool threshold                                                      |
|   9 | `test_no_oscillation_when_parked_at_the_limit`                   |   ✅   | Feed the plan's own output back in, exactly as a caller does, while the die sits at the      |
|  10 | `test_brownout_boot_holds_the_floor_even_when_busy_and_cool`     |   ✅   | Brownout boot holds the floor even when busy and cool                                        |
|  11 | `test_recovery_window_ends`                                      |   ✅   | Recovery window ends                                                                         |
|  12 | `test_normal_boot_never_recovers`                                |   ✅   | Normal boot never recovers                                                                   |
|  13 | `test_brownout_and_hot_both_reported`                            |   ✅   | Precedence puts both at the floor, but the flags must still say why - a caller logging this  |
|  14 | `test_missing_sensor_does_not_read_as_ice_cold`                  |   ✅   | INT16_MIN means "this part has no sensor". Treating it as a temperature would both refuse to |
|  15 | `test_null_cfg_is_not_a_crash`                                   |   ✅   | Null cfg is not a crash                                                                      |
|  16 | `test_null_cfg_defaults_is_not_a_crash`                          |   ✅   | Null cfg defaults is not a crash                                                             |
|  17 | `test_defaults_are_self_consistent`                              |   ✅   | Defaults are self consistent                                                                 |
|  18 | `test_json`                                                      |   ✅   | Json                                                                                         |
|  19 | `test_json_reports_a_missing_sensor_as_null`                     |   ✅   | Json reports a missing sensor as null                                                        |
|  20 | `test_json_missing_sensor_reports_throttled_and_recovering_true` |   ✅   | The no-sensor branch has its own throttled/recovering ternaries; exercise both true arms,    |
|  21 | `test_json_with_a_sensor_reading_reports_recovering_true`        |   ✅   | test_json only ever sees recovering=false; cover the recovering-true arm of the              |
|  22 | `test_json_overflow_is_fail_closed`                              |   ✅   | Json overflow is fail closed                                                                 |
|  23 | `test_json_null_out_is_rejected`                                 |   ✅   | Json null out is rejected                                                                    |
|  24 | `test_json_zero_cap_is_rejected`                                 |   ✅   | Json zero cap is rejected                                                                    |

</details>

---

## test_hotswap - native_hotswap - ✅ 31 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the removable-storage state machine (services/storage/hotswap): the fault threshold and_

|   # | Test                                                          | Status | Description                                                                                          |
| --: | :------------------------------------------------------------ | :----: | :--------------------------------------------------------------------------------------------------- |
|   1 | `test_starts_absent_not_ready`                                |   ✅   | Starting STORAGE_STATE_READY would let a caller write before anything was ever mounted.              |
|   2 | `test_first_probe_is_due_immediately`                         |   ✅   | Back-dated last_probe: a card already present at boot must mount now, not one interval later.        |
|   3 | `test_first_probe_is_due_when_init_time_is_near_zero`         |   ✅   | Real case: begin() runs a few ms after boot, so `now - probe_interval` underflows past zero.         |
|   4 | `test_zero_threshold_is_clamped_to_one`                       |   ✅   | Zero threshold is clamped to one                                                                     |
|   5 | `test_one_failure_does_not_fault_a_healthy_volume`            |   ✅   | One failure does not fault a healthy volume                                                          |
|   6 | `test_threshold_run_faults_and_counts`                        |   ✅   | Threshold run faults and counts                                                                      |
|   7 | `test_a_success_resets_the_failure_run`                       |   ✅   | A success resets the failure run                                                                     |
|   8 | `test_further_failures_while_faulted_are_ignored`             |   ✅   | Further failures while faulted are ignored                                                           |
|   9 | `test_io_while_absent_is_ignored`                             |   ✅   | Io while absent is ignored                                                                           |
|  10 | `test_fail_run_saturates_instead_of_wrapping`                 |   ✅   | Fail run saturates instead of wrapping                                                               |
|  11 | `test_fail_run_at_the_uint8_ceiling_does_not_wrap`            |   ✅   | The saturation guard itself, with the counter already parked at the ceiling: the                     |
|  12 | `test_no_probe_while_ready`                                   |   ✅   | No probe while ready                                                                                 |
|  13 | `test_probe_is_rate_limited_while_absent`                     |   ✅   | Probe is rate limited while absent                                                                   |
|  14 | `test_probe_pacing_is_wrapsafe_across_rollover`               |   ✅   | Last probe just before the 32-bit millis rollover; "now" just after it.                              |
|  15 | `test_present_but_unmountable_stays_absent`                   |   ✅   | A card that will not mount is not storage, however present the detect pin says it is.                |
|  16 | `test_mount_counts_only_on_transition`                        |   ✅   | Mount counts only on transition                                                                      |
|  17 | `test_full_removal_and_reinsertion_cycle`                     |   ✅   | Full removal and reinsertion cycle                                                                   |
|  18 | `test_faulted_volume_can_go_straight_back_to_ready`           |   ✅   | A card reseated quickly enough that the probe finds it mounted without an STORAGE_STATE_ABSENT step. |
|  19 | `test_null_core_is_not_a_crash`                               |   ✅   | Null core is not a crash                                                                             |
|  20 | `test_state_names`                                            |   ✅   | State names                                                                                          |
|  21 | `test_json_and_overflow_is_fail_closed`                       |   ✅   | Json and overflow is fail closed                                                                     |
|  22 | `test_binding_poll_before_begin_does_nothing`                 |   ✅   | No callbacks installed yet: poll must not probe or claim storage. (Must be the                       |
|  23 | `test_binding_mounts_on_the_first_poll_and_notifies`          |   ✅   | begin() back-dates the probe clock, so a card already in the slot mounts on the                      |
|  24 | `test_binding_ready_volume_is_never_reprobed`                 |   ✅   | Nothing to remount while STORAGE_STATE_READY, so the per-loop poll must cost no callbacks at all.    |
|  25 | `test_binding_io_fault_unmounts_immediately_and_notifies`     |   ✅   | The point of the whole owner: on the failure that faults the volume the mount is                     |
|  26 | `test_binding_drops_a_faulted_mount_before_retrying`          |   ✅   | The remount attempt unmounts first, so it starts clean instead of reusing handles                    |
|  27 | `test_binding_faults_and_retries_without_an_unmount_callback` |   ✅   | unmount is optional. Without one the fault must still be recorded and notified,                      |
|  28 | `test_binding_without_card_detect_lets_the_mount_decide`      |   ✅   | A NULL present callback means "assume a card is there"; an unmountable volume                        |
|  29 | `test_binding_without_a_mount_callback_never_becomes_ready`   |   ✅   | No way to mount anything means no storage: it must stay fail-closed rather than                      |
|  30 | `test_binding_event_callback_is_optional`                     |   ✅   | Clearing the event callback must not stop the machine from running.                                  |
|  31 | `test_binding_poll_reads_the_library_clock`                   |   ✅   | poll() is poll_at(pc_millis()), so the same rate limit applies to the loop-driven                    |

</details>

---

## test_clock - native_clock - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the pluggable monotonic clock (services/pc_clock): the platform_

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_default_is_platform_millis`       |   ✅   | Default is platform millis       |
|   2 | `test_custom_clock_divides_to_1000hz`   |   ✅   | Custom clock divides to 1000hz   |
|   3 | `test_sub_khz_source_not_divided`       |   ✅   | Sub khz source not divided       |
|   4 | `test_revert_to_default`                |   ✅   | Revert to default                |
|   5 | `test_micros_custom_divides_to_1mhz`    |   ✅   | Micros custom divides to 1mhz    |
|   6 | `test_latency_stat_records_and_budgets` |   ✅   | Latency stat records and budgets |
|   7 | `test_latency_budget_zero_disables`     |   ✅   | Latency budget zero disables     |

</details>

---

## test_concurrency - native_concurrency - ✅ 2 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Concurrency proof for the cross-thread slot fields (pc_atomic state / rx_head /_

|   # | Test                         | Status | Description           |
| --: | :--------------------------- | :----: | :-------------------- |
|   1 | `test_spsc_ring_no_race`     |   ✅   | Spsc ring no race     |
|   2 | `test_state_handoff_no_race` |   ✅   | State handoff no race |

</details>

---

## test_concurrency - native_tsan - ✅ 2 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Concurrency proof for the cross-thread slot fields (pc_atomic state / rx_head /_

|   # | Test                         | Status | Description           |
| --: | :--------------------------- | :----: | :-------------------- |
|   1 | `test_spsc_ring_no_race`     |   ✅   | Spsc ring no race     |
|   2 | `test_state_handoff_no_race` |   ✅   | State handoff no race |

</details>

---

## test_tls_record - native_tls_record - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_TLS 1.3 stream record layer (RFC 8446 sec 5). Properties and wire structure; the AEAD itself is_

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_keys_derive_kat`                         |   ✅   | Keys derive kat                         |
|   2 | `test_protect_kat`                             |   ✅   | Protect kat                             |
|   3 | `test_unprotect_kat`                           |   ✅   | Unprotect kat                           |
|   4 | `test_plaintext_round_trips`                   |   ✅   | Plaintext round trips                   |
|   5 | `test_plaintext_parse_refuses_truncated`       |   ✅   | Plaintext parse refuses truncated       |
|   6 | `test_plaintext_parse_ignores_legacy_version`  |   ✅   | Plaintext parse ignores legacy version  |
|   7 | `test_plaintext_build_refuses_overflow`        |   ✅   | Plaintext build refuses overflow        |
|   8 | `test_protect_round_trips_and_hides_the_type`  |   ✅   | Protect round trips and hides the type  |
|   9 | `test_sequence_advances_and_records_differ`    |   ✅   | Sequence advances and records differ    |
|  10 | `test_out_of_order_record_fails`               |   ✅   | Out of order record fails               |
|  11 | `test_tampered_record_is_refused`              |   ✅   | Tampered record is refused              |
|  12 | `test_short_and_malformed_records_are_refused` |   ✅   | Short and malformed records are refused |
|  13 | `test_unkeyed_context_fails_closed`            |   ✅   | Unkeyed context fails closed            |
|  14 | `test_protect_refuses_overflow`                |   ✅   | Protect refuses overflow                |
|  15 | `test_empty_plaintext_carries_only_the_type`   |   ✅   | Empty plaintext carries only the type   |
|  16 | `test_content_with_trailing_zeros_round_trips` |   ✅   | Content with trailing zeros round trips |
|  17 | `test_keys_wipe_disables_the_context`          |   ✅   | Keys wipe disables the context          |

</details>

---

## test_h3_server - native_h3_server - ✅ 3 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_HTTP/3 dispatch-bridge test: proves an HTTP/3 request served by a *real PC route*. A_

|   # | Test                              | Status | Description                                                                        |
| --: | :-------------------------------- | :----: | :--------------------------------------------------------------------------------- |
|   1 | `test_h3_begin_edges`             |   ✅   | No listeners, no HTTP/3 -> rejected (listener_count==0 && !_h3_enabled true side). |
|   2 | `test_h3_request_served_by_route` |   ✅   | H3 request served by route                                                         |
|   3 | `test_h3_dispatch_edges`          |   ✅   | H3 dispatch edges                                                                  |

</details>

---

## test_frame - native_frame - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the declarative frame builder (mmgr/frame.h)._

|   # | Test                                    | Status | Description                                                                         |
| --: | :-------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_frame_matches_printf`             |   ✅   | Frame matches printf                                                                |
|   2 | `test_frame_every_kind`                 |   ✅   | Frame every kind                                                                    |
|   3 | `test_frame_widths`                     |   ✅   | Frame widths                                                                        |
|   4 | `test_frame_null_string_is_empty`       |   ✅   | Frame null string is empty                                                          |
|   5 | `test_frame_literal_only`               |   ✅   | Frame literal only                                                                  |
|   6 | `test_frame_empty_spec`                 |   ✅   | an empty frame writes nothing and reports 0, and must still leave a valid C string  |
|   7 | `test_frame_overflow_fails_closed`      |   ✅   | Frame overflow fails closed                                                         |
|   8 | `test_frame_exact_fit_boundary`         |   ✅   | Frame exact fit boundary                                                            |
|   9 | `test_frame_guards`                     |   ✅   | Frame guards                                                                        |
|  10 | `test_frame_zero_cap_writes_nothing`    |   ✅   | Frame zero cap writes nothing                                                       |
|  11 | `test_frame_append_accumulates`         |   ✅   | Frame append accumulates                                                            |
|  12 | `test_frame_append_rewinds_whole_frame` |   ✅   | A frame that does not fit must leave the accumulated buffer exactly as it was - a   |
|  13 | `test_frame_append_to_full_buffer`      |   ✅   | Frame append to full buffer                                                         |
|  14 | `test_frame_float_matches_printf`       |   ✅   | Frame float matches printf                                                          |
|  15 | `test_frame_fixed_huge_falls_back`      |   ✅   | Frame fixed huge falls back                                                         |
|  16 | `test_frame_unknown_opcode_refuses`     |   ✅   | A spec built against a newer engine must not silently emit a frame missing a field. |

</details>

---

## test_protostr - native_protostr - ✅ 32 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_mmgr/protostr.h: the bounded-run walks. The oracle is libc's own string functions._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_len_matches_the_oracle`                           |   ✅   | Len matches the oracle                           |
|   2 | `test_len_matches_strnlen_at_every_cap`                 |   ✅   | Len matches strnlen at every cap                 |
|   3 | `test_len_absent_returns_the_cap`                       |   ✅   | Len absent returns the cap                       |
|   4 | `test_len_ignores_high_bytes`                           |   ✅   | Len ignores high bytes                           |
|   5 | `test_len_unaligned`                                    |   ✅   | Len unaligned                                    |
|   6 | `test_len_stops_at_the_cap`                             |   ✅   | Len stops at the cap                             |
|   7 | `test_diff_matches_the_oracle`                          |   ✅   | Diff matches the oracle                          |
|   8 | `test_diff_reports_the_cap_when_equal`                  |   ✅   | Diff reports the cap when equal                  |
|   9 | `test_diff_case_insensitive`                            |   ✅   | Diff case insensitive                            |
|  10 | `test_eq_requires_the_whole_string`                     |   ✅   | Eq requires the whole string                     |
|  11 | `test_eq_at_every_length`                               |   ✅   | Eq at every length                               |
|  12 | `test_starts_reads_the_tie_as_a_match`                  |   ✅   | Starts reads the tie as a match                  |
|  13 | `test_starts_at_every_prefix_length`                    |   ✅   | Starts at every prefix length                    |
|  14 | `test_find_locates_the_first_occurrence`                |   ✅   | Find locates the first occurrence                |
|  15 | `test_find_absent`                                      |   ✅   | Find absent                                      |
|  16 | `test_find_empty_needle_matches_at_the_start`           |   ✅   | Find empty needle matches at the start           |
|  17 | `test_find_stops_at_the_haystack_nul`                   |   ✅   | Find stops at the haystack nul                   |
|  18 | `test_find_case_insensitive`                            |   ✅   | Find case insensitive                            |
|  19 | `test_has_agrees_with_find`                             |   ✅   | Has agrees with find                             |
|  20 | `test_find_agrees_with_a_naive_search_under_hard_input` |   ✅   | Find agrees with a naive search under hard input |
|  21 | `test_find_locates_every_substring_of_itself`           |   ✅   | Find locates every substring of itself           |
|  22 | `test_find_rejects_every_near_miss`                     |   ✅   | Find rejects every near miss                     |
|  23 | `test_compares_under_hard_input`                        |   ✅   | Compares under hard input                        |
|  24 | `test_starts_and_eq_part_at_the_pattern_end`            |   ✅   | Starts and eq part at the pattern end            |
|  25 | `test_copy_terminates_and_reports`                      |   ✅   | Copy terminates and reports                      |
|  26 | `test_copy_bounds_by_the_destination`                   |   ✅   | Copy bounds by the destination                   |
|  27 | `test_copy_zero_capacity_writes_nothing`                |   ✅   | Copy zero capacity writes nothing                |
|  28 | `test_copy_empty_source`                                |   ✅   | Copy empty source                                |
|  29 | `test_ws_classifies_the_six`                            |   ✅   | Ws classifies the six                            |
|  30 | `test_digit_classifies_the_ten`                         |   ✅   | Digit classifies the ten                         |
|  31 | `test_step_byte_settles_or_continues`                   |   ✅   | Step byte settles or continues                   |
|  32 | `test_each_member_is_the_walk_it_names`                 |   ✅   | Each member is the walk it names                 |

</details>

---

## test_protomem - native_protomem - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_mmgr/protomem.h: the byte-span operations. The oracle is a byte loop written here._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_cpy_every_length_aligned_source`        |   ✅   | Cpy every length aligned source        |
|   2 | `test_cpy_every_source_offset`                |   ✅   | Cpy every source offset                |
|   3 | `test_cpy_long_span`                          |   ✅   | Cpy long span                          |
|   4 | `test_cpy_zero_writes_nothing`                |   ✅   | Cpy zero writes nothing                |
|   5 | `test_move_overlap_down`                      |   ✅   | Move overlap down                      |
|   6 | `test_move_overlap_up`                        |   ✅   | Move overlap up                        |
|   7 | `test_move_disjoint`                          |   ✅   | Move disjoint                          |
|   8 | `test_move_same_and_zero`                     |   ✅   | Move same and zero                     |
|   9 | `test_cmp_equal`                              |   ✅   | Cmp equal                              |
|  10 | `test_cmp_first_difference_at_every_position` |   ✅   | Cmp first difference at every position |
|  11 | `test_cmp_is_unsigned`                        |   ✅   | Cmp is unsigned                        |
|  12 | `test_cmp_does_not_stop_at_a_nul`             |   ✅   | Cmp does not stop at a nul             |
|  13 | `test_chr_finds_the_first_match`              |   ✅   | Chr finds the first match              |
|  14 | `test_chr_absent_and_bounded`                 |   ✅   | Chr absent and bounded                 |
|  15 | `test_chr_finds_a_nul_and_searches_past_one`  |   ✅   | Chr finds a nul and searches past one  |
|  16 | `test_set_fills_the_span`                     |   ✅   | Set fills the span                     |
|  17 | `test_zero_clears_the_span`                   |   ✅   | Zero clears the span                   |
|  18 | `test_set_splats_every_lane`                  |   ✅   | Set splats every lane                  |
|  19 | `test_each_member_is_the_walk_it_names`       |   ✅   | Each member is the walk it names       |

</details>

---

## test_float_bits - native_float_bits - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_mmgr/float_bits.h: a double read as sign, exponent and mantissa, and merged back from them._

|   # | Test                                          | Status | Description                                                |
| --: | :-------------------------------------------- | :----: | :--------------------------------------------------------- |
|   1 | `test_masks_partition_the_word`               |   ✅   | Masks partition the word                                   |
|   2 | `test_double_is_sixty_four_bits`              |   ✅   | Double is sixty four bits                                  |
|   3 | `test_known_encoding`                         |   ✅   | Known encoding                                             |
|   4 | `test_non_finite_encodings`                   |   ✅   | Non finite encodings                                       |
|   5 | `test_subnormal_encoding`                     |   ✅   | Subnormal encoding                                         |
|   6 | `test_split_and_merge_round_trips`            |   ✅   | Split and merge round trips                                |
|   7 | `test_merge_masks_each_field`                 |   ✅   | Merge masks each field                                     |
|   8 | `test_every_bit_position_survives`            |   ✅   | Every bit position survives                                |
|   9 | `test_exponent_knee_in_and_out`               |   ✅   | Exponent knee in and out                                   |
|  10 | `test_repeating_mantissa_patterns_in_and_out` |   ✅   | The exponents either side of every boundary the field has. |
|  11 | `test_walking_mantissa_bit_in_and_out`        |   ✅   | Walking mantissa bit in and out                            |
|  12 | `test_from_bits_is_the_inverse_of_the_read`   |   ✅   | From bits is the inverse of the read                       |

</details>

---

## test_membuild - native_membuild - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_mmgr/membuild.h: the bounded no-heap builder. The float and integer renderings are diffed_

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_put_n_appends`                            |   ✅   | Put n appends                            |
|   2 | `test_lit_takes_the_length_from_the_type`       |   ✅   | Lit takes the length from the type       |
|   3 | `test_capacity_reserves_the_terminator`         |   ✅   | Capacity reserves the terminator         |
|   4 | `test_overflow_latches_and_writes_nothing`      |   ✅   | Overflow latches and writes nothing      |
|   5 | `test_zero_capacity_writes_nothing`             |   ✅   | Zero capacity writes nothing             |
|   6 | `test_ch_appends_and_latches`                   |   ✅   | Ch appends and latches                   |
|   7 | `test_clip_truncates_without_latching`          |   ✅   | Clip truncates without latching          |
|   8 | `test_u64_clip_is_all_or_nothing`               |   ✅   | U64 clip is all or nothing               |
|   9 | `test_u64_clip_right_aligns_in_columns`         |   ✅   | U64 clip right aligns in columns         |
|  10 | `test_u64_matches_printf_decimal`               |   ✅   | U64 matches printf decimal               |
|  11 | `test_hex_matches_printf`                       |   ✅   | Hex matches printf                       |
|  12 | `test_u32w_matches_printf_zero_pad`             |   ✅   | U32w matches printf zero pad             |
|  13 | `test_i64_matches_printf_including_the_minimum` |   ✅   | I64 matches printf including the minimum |
|  14 | `test_g_matches_printf`                         |   ✅   | G matches printf                         |
|  15 | `test_g_renders_negative_zero`                  |   ✅   | G renders negative zero                  |
|  16 | `test_g_renders_infinity_and_nan`               |   ✅   | G renders infinity and nan               |
|  17 | `test_fixed_matches_printf`                     |   ✅   | Fixed matches printf                     |
|  18 | `test_xml_escapes_the_metacharacters`           |   ✅   | Xml escapes the metacharacters           |
|  19 | `test_json_quotes_and_escapes`                  |   ✅   | Json quotes and escapes                  |
|  20 | `test_json_escape_that_does_not_fit_latches`    |   ✅   | Json escape that does not fit latches    |
|  21 | `test_finish_terminates_and_reports`            |   ✅   | Finish terminates and reports            |

</details>

---

## test_endian - native_endian - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_mmgr/endian.h: a fixed width moved between an integer and the bytes at a pointer._

|   # | Test                                  | Status | Description                    |
| --: | :------------------------------------ | :----: | :----------------------------- |
|   1 | `test_be_writers_match_the_oracle`    |   ✅   | Be writers match the oracle    |
|   2 | `test_le_writers_match_the_oracle`    |   ✅   | Le writers match the oracle    |
|   3 | `test_readers_match_the_oracle`       |   ✅   | Readers match the oracle       |
|   4 | `test_be_is_the_reverse_of_le`        |   ✅   | Be is the reverse of le        |
|   5 | `test_round_trip_at_every_offset`     |   ✅   | Round trip at every offset     |
|   6 | `test_adjacent_fields_do_not_overlap` |   ✅   | Adjacent fields do not overlap |

</details>

---

## test_bytes - native_bytes - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_mmgr/bytes.h: append into a pc_span, take out of a pc_cspan, and the offset-passing reads a_

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_put_writes_and_counts`                           |   ✅   | Put writes and counts                           |
|   2 | `test_put_past_cap_counts_the_size_needed`             |   ✅   | Put past cap counts the size needed             |
|   3 | `test_put_be_is_network_order`                         |   ✅   | Put be is network order                         |
|   4 | `test_put_be_past_cap_counts_the_whole_width`          |   ✅   | Put be past cap counts the whole width          |
|   5 | `test_take_be_reads_and_advances`                      |   ✅   | Take be reads and advances                      |
|   6 | `test_take_be_at_and_past_the_end`                     |   ✅   | Take be at and past the end                     |
|   7 | `test_take_be_refusal_is_sticky_and_leaves_the_cursor` |   ✅   | Take be refusal is sticky and leaves the cursor |
|   8 | `test_take_be_zero_width`                              |   ✅   | Take be zero width                              |
|   9 | `test_rd_u32_reads_and_advances`                       |   ✅   | Rd u32 reads and advances                       |
|  10 | `test_rd_u32_short_read_is_refused`                    |   ✅   | Rd u32 short read is refused                    |
|  11 | `test_rd_str_points_into_the_payload`                  |   ✅   | Rd str points into the payload                  |
|  12 | `test_rd_str_overlong_is_refused_and_rewinds`          |   ✅   | Rd str overlong is refused and rewinds          |
|  13 | `test_rd_str_full_range_length_is_refused`             |   ✅   | Rd str full range length is refused             |
|  14 | `test_rd_str_exact_fit_is_accepted`                    |   ✅   | Rd str exact fit is accepted                    |

</details>

---

## test_bitio - native_bitio - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_mmgr/bitio.h: the LSB-first bit writer. The oracle is a bit reader written here._

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_put_round_trips_lsb_first`          |   ✅   | Put round trips lsb first          |
|   2 | `test_eight_bits_spills_one_byte`         |   ✅   | Eight bits spills one byte         |
|   3 | `test_wide_put_spills_every_whole_byte`   |   ✅   | Wide put spills every whole byte   |
|   4 | `test_align_pads_high_bits_with_zero`     |   ✅   | Align pads high bits with zero     |
|   5 | `test_align_on_boundary_emits_nothing`    |   ✅   | Align on boundary emits nothing    |
|   6 | `test_exact_fill_is_not_overflow`         |   ✅   | Exact fill is not overflow         |
|   7 | `test_one_byte_past_cap_latches_overflow` |   ✅   | One byte past cap latches overflow |
|   8 | `test_align_at_cap_latches_overflow`      |   ✅   | Align at cap latches overflow      |
|   9 | `test_overflow_stays_latched`             |   ✅   | Overflow stays latched             |

</details>

---

## test_rawmemcpy - native_rawmemcpy - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_mmgr/rawmemcpy.h: the scalar rungs, the aligned rungs, and the ladder proto_raw_read steps down._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_word_rung_follows_the_declared_width`      |   ✅   | Word rung follows the declared width      |
|   2 | `test_align_declares_alignment`                  |   ✅   | Align declares alignment                  |
|   3 | `test_raw_scalar_loads_match_the_oracle`         |   ✅   | Raw scalar loads match the oracle         |
|   4 | `test_raw_scalar_stores_match_the_oracle`        |   ✅   | Raw scalar stores match the oracle        |
|   5 | `test_raw_load_widths_and_the_default`           |   ✅   | Raw load widths and the default           |
|   6 | `test_aligned_loads_agree_with_raw`              |   ✅   | Aligned loads agree with raw              |
|   7 | `test_aligned_stores_agree_with_raw`             |   ✅   | Aligned stores agree with raw             |
|   8 | `test_mover_rung_round_trips`                    |   ✅   | Mover rung round trips                    |
|   9 | `test_read_cross_product_of_offsets_and_lengths` |   ✅   | Read cross product of offsets and lengths |
|  10 | `test_read_zero_length_writes_nothing`           |   ✅   | Read zero length writes nothing           |
|  11 | `test_read_long_span_is_byte_exact`              |   ✅   | Read long span is byte exact              |

</details>

---

## test_span - native_span - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_pc_span: the run length must be bound in BOTH directions._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_capacity_is_the_constant_it_was_built_from`    |   ✅   | Capacity is the constant it was built from    |
|   2 | `test_span_survives_what_sizeof_loses`               |   ✅   | Span survives what sizeof loses               |
|   3 | `test_a_fresh_span_is_empty_and_ok`                  |   ✅   | A fresh span is empty and ok                  |
|   4 | `test_produced_length_rides_back_with_the_buffer`    |   ✅   | Produced length rides back with the buffer    |
|   5 | `test_overflow_keeps_counting_the_required_size`     |   ✅   | Overflow keeps counting the required size     |
|   6 | `test_reset_rewinds_and_clears_overflow`             |   ✅   | Reset rewinds and clears overflow             |
|   7 | `test_null_pointer_yields_zero_capacity`             |   ✅   | Null pointer yields zero capacity             |
|   8 | `test_zero_capacity_yields_null_pointer`             |   ✅   | Zero capacity yields null pointer             |
|   9 | `test_writing_a_failed_allocation_is_a_noop`         |   ✅   | Writing a failed allocation is a noop         |
|  10 | `test_cspan_null_and_zero_normalize`                 |   ✅   | Cspan null and zero normalize                 |
|  11 | `test_after_advances_and_shrinks`                    |   ✅   | After advances and shrinks                    |
|  12 | `test_after_past_the_end_is_empty_not_out_of_bounds` |   ✅   | After past the end is empty not out of bounds |
|  13 | `test_first_clamps_to_what_exists`                   |   ✅   | First clamps to what exists                   |
|  14 | `test_produced_view_uses_the_spans_own_cursor`       |   ✅   | Produced view uses the spans own cursor       |
|  15 | `test_produced_view_of_an_overflowed_span_is_empty`  |   ✅   | Produced view of an overflowed span is empty  |
|  16 | `test_read_narrows_to_a_given_length`                |   ✅   | Read narrows to a given length                |
|  17 | `test_bytes_read_cursor_drives_a_cspan`              |   ✅   | Bytes read cursor drives a cspan              |
|  18 | `test_a_wire_length_cannot_overflow_the_bound`       |   ✅   | A wire length cannot overflow the bound       |

</details>

---

## test_ring - native_ring - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the shared ring primitive (mmgr/ring.h) and its three views: bytes by head/tail,_

|   # | Test                                                                 | Status | Description                                                   |
| --: | :------------------------------------------------------------------- | :----: | :------------------------------------------------------------ |
|   1 | `test_a_power_of_two_capacity_is_what_makes_the_index_a_mask`        |   ✅   | A power of two capacity is what makes the index a mask        |
|   2 | `test_the_mask_wraps_exactly_where_a_modulo_would`                   |   ✅   | The mask wraps exactly where a modulo would                   |
|   3 | `test_an_empty_ring_reports_nothing_available_and_all_but_one_free`  |   ✅   | An empty ring reports nothing available and all but one free  |
|   4 | `test_one_slot_stays_reserved_so_full_is_distinguishable_from_empty` |   ✅   | One slot stays reserved so full is distinguishable from empty |
|   5 | `test_a_byte_pops_in_order_and_the_ring_empties`                     |   ✅   | A byte pops in order and the ring empties                     |
|   6 | `test_a_read_stops_at_what_is_there_not_at_what_was_asked`           |   ✅   | A read stops at what is there not at what was asked           |

</details>

---

## test_swar - native_swar - ✅ 3 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the lane math (mmgr/swar.h)._

|   # | Test                           | Status | Description             |
| --: | :----------------------------- | :----: | :---------------------- |
|   1 | `test_has_zero_finds_any_lane` |   ✅   | Has zero finds any lane |
|   2 | `test_zero_lane_from_mask`     |   ✅   | Zero lane from mask     |
|   3 | `test_lane_compares`           |   ✅   | Every lane is >= '0'.   |

</details>

---
