# Test Report

**Generated:** 2026-08-14 14:46:49
**Command:** `pio test` over 440 auto-discovered native envs (excludes native_pentest, native_codeql)
**Result:** ❌ 4992 passed, 89 failed - 3086s

---

## Summary

| Suite | Environment | Tests | Status | Duration |
| :---- | :---------- | ----: | :----: | -------: |

---

## test_ad9238 - native_ad9238 - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_an877_instruction_phase_bit_fields`                     |   ✅   | An877 instruction phase bit fields                     |
|   2 | `test_an877_word_length_field_is_nbytes_minus_one`            |   ✅   | An877 word length field is nbytes minus one            |
|   3 | `test_write_is_instruction_plus_one_data_byte`                |   ✅   | Write is instruction plus one data byte                |
|   4 | `test_read_is_the_two_octet_instruction_alone`                |   ✅   | Read is the two octet instruction alone                |
|   5 | `test_device_update_is_a_write_of_bit0_to_0x0ff`              |   ✅   | Device update is a write of bit0 to 0x0ff              |
|   6 | `test_addresses_wider_than_thirteen_bits_are_refused`         |   ✅   | Addresses wider than thirteen bits are refused         |
|   7 | `test_byte_counts_outside_one_to_four_are_refused`            |   ✅   | Byte counts outside one to four are refused            |
|   8 | `test_null_and_undersized_buffers_fail_closed`                |   ✅   | Null and undersized buffers fail closed                |
|   9 | `test_read_and_write_of_a_register_differ_only_in_the_rw_bit` |   ✅   | Read and write of a register differ only in the rw bit |
|  10 | `test_named_registers_all_encode`                             |   ✅   | Named registers all encode                             |

</details>

---

## test_ads - native_ads - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Beckhoff ADS / AMS codec (services/fieldbus/ads/ads.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_published_constants`                      |   ✅   | Published constants                      |
|   2 | `test_ams_header_octet_layout`                  |   ✅   | Ams header octet layout                  |
|   3 | `test_read_request_length_and_payload`          |   ✅   | Read request length and payload          |
|   4 | `test_header_round_trip`                        |   ✅   | Header round trip                        |
|   5 | `test_write_frames_the_data_after_the_length`   |   ✅   | Write frames the data after the length   |
|   6 | `test_read_write_symbol_by_name`                |   ✅   | Read write symbol by name                |
|   7 | `test_write_control_carries_the_state_pair`     |   ✅   | Write control carries the state pair     |
|   8 | `test_add_notification_payload_is_forty_octets` |   ✅   | Add notification payload is forty octets |
|   9 | `test_parse_read_response`                      |   ✅   | Parse read response                      |
|  10 | `test_parse_read_state_response`                |   ✅   | Parse read state response                |
|  11 | `test_parse_device_info_terminates_a_full_name` |   ✅   | Parse device info terminates a full name |
|  12 | `test_parse_add_notification_response`          |   ✅   | Parse add notification response          |
|  13 | `test_walks_a_notification_stamp`               |   ✅   | Walks a notification stamp               |
|  14 | `test_malformed_framing_is_refused`             |   ✅   | Malformed framing is refused             |
|  15 | `test_builders_refuse_a_short_buffer`           |   ✅   | Builders refuse a short buffer           |

</details>

---

## test_ads1115 - native_ads1115 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TI ADS1115 codec (server/peripherals/ads1115/ads1115.h)._

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_sbas444_reset_value_with_the_mux_moved_to_ain0_gnd`  |   ✅   | Sbas444 reset value with the mux moved to ain0 gnd  |
|   2 | `test_sbas444_mux_encoding_for_each_single_ended_channel`  |   ✅   | Sbas444 mux encoding for each single ended channel  |
|   3 | `test_sbas444_pga_encoding`                                |   ✅   | Sbas444 pga encoding                                |
|   4 | `test_sbas444_data_rate_encoding`                          |   ✅   | Sbas444 data rate encoding                          |
|   5 | `test_sbas444_start_single_shot_and_comparator_disabled`   |   ✅   | Sbas444 start single shot and comparator disabled   |
|   6 | `test_out_of_range_fields_fall_back_to_the_defaults`       |   ✅   | Out of range fields fall back to the defaults       |
|   7 | `test_sbas444_lsb_is_full_scale_over_32768`                |   ✅   | Sbas444 lsb is full scale over 32768                |
|   8 | `test_sbas444_table4_endpoints`                            |   ✅   | Sbas444 table4 endpoints                            |
|   9 | `test_conversion_is_odd_about_zero`                        |   ✅   | Conversion is odd about zero                        |
|  10 | `test_lower_gain_reads_a_larger_voltage_for_the_same_code` |   ✅   | Lower gain reads a larger voltage for the same code |
|  11 | `test_raw_to_uv_out_of_range_gain_falls_back`              |   ✅   | Raw to uv out of range gain falls back              |
|  12 | `test_sbas444_register_addresses`                          |   ✅   | Sbas444 register addresses                          |

</details>

---

## test_amqp - native_amqp - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the AMQP 0-9-1 frame codec (services/iot/amqp/amqp.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_amqp091_protocol_header`                 |   ✅   | Amqp091 protocol header                 |
|   2 | `test_amqp091_frame_constants`                 |   ✅   | Amqp091 frame constants                 |
|   3 | `test_amqp091_frame_layout`                    |   ✅   | Amqp091 frame layout                    |
|   4 | `test_frame_end_is_checked_before_decoding`    |   ✅   | Frame end is checked before decoding    |
|   5 | `test_frame_round_trip_and_consumed`           |   ✅   | Frame round trip and consumed           |
|   6 | `test_partial_frame_is_not_parsed`             |   ✅   | Partial frame is not parsed             |
|   7 | `test_amqp091_method_frame_layout`             |   ✅   | Amqp091 method frame layout             |
|   8 | `test_method_round_trip_over_the_class_table`  |   ✅   | Method round trip over the class table  |
|   9 | `test_method_payload_shorter_than_its_indices` |   ✅   | Method payload shorter than its indices |
|  10 | `test_amqp091_content_header_layout`           |   ✅   | Amqp091 content header layout           |
|  11 | `test_amqp091_heartbeat`                       |   ✅   | Amqp091 heartbeat                       |
|  12 | `test_builds_refuse_a_short_buffer`            |   ✅   | Builds refuse a short buffer            |
|  13 | `test_an_oversized_size_field_is_refused`      |   ✅   | An oversized size field is refused      |

</details>

---

## test_arena - native_arena - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the double-ended arena (mmgr/arena.h)._

|   # | Test                                                              | Status | Description                                                |
| --: | :---------------------------------------------------------------- | :----: | :--------------------------------------------------------- |
|   1 | `test_persist_alloc_is_aligned_and_inside_the_region`             |   ✅   | Persist alloc is aligned and inside the region             |
|   2 | `test_persist_alloc_is_zeroed_on_carve_and_on_reuse`              |   ✅   | Persist alloc is zeroed on carve and on reuse              |
|   3 | `test_persist_reuses_a_freed_hole`                                |   ✅   | Persist reuses a freed hole                                |
|   4 | `test_persist_free_coalesces_adjacent_holes`                      |   ✅   | Persist free coalesces adjacent holes                      |
|   5 | `test_persist_skips_a_hole_that_is_too_small`                     |   ✅   | Persist skips a hole that is too small                     |
|   6 | `test_persist_free_of_the_top_block_returns_the_middle`           |   ✅   | Persist free of the top block returns the middle           |
|   7 | `test_persist_size_overflow_fails_closed`                         |   ✅   | Persist size overflow fails closed                         |
|   8 | `test_persist_double_free_and_null_free_are_noops`                |   ✅   | Persist double free and null free are noops                |
|   9 | `test_free_bytes_reaches_zero_without_crossing`                   |   ✅   | Free bytes reaches zero without crossing                   |
|  10 | `test_scratch_bumps_down_and_resets`                              |   ✅   | Scratch bumps down and resets                              |
|  11 | `test_scratch_mark_and_release`                                   |   ✅   | Scratch mark and release                                   |
|  12 | `test_scratch_release_rejects_a_mark_outside_the_region`          |   ✅   | Scratch release rejects a mark outside the region          |
|  13 | `test_scratch_alignment_is_clamped_to_what_the_region_guarantees` |   ✅   | Scratch alignment is clamped to what the region guarantees |
|  14 | `test_the_two_ends_never_overlap`                                 |   ✅   | The two ends never overlap                                 |
|  15 | `test_a_request_that_would_cross_fails_closed`                    |   ✅   | A request that would cross fails closed                    |
|  16 | `test_the_middle_floats_between_the_ends`                         |   ✅   | The middle floats between the ends                         |
|  17 | `test_a_borrow_owns_its_alignment_pad`                            |   ✅   | A borrow owns its alignment pad                            |
|  18 | `test_a_write_over_the_pad_hits_no_neighbour`                     |   ✅   | A write over the pad hits no neighbour                     |
|  19 | `test_owns_is_an_address_range_test`                              |   ✅   | Owns is an address range test                              |
|  20 | `test_a_zero_length_region_refuses_everything`                    |   ✅   | A zero length region refuses everything                    |
|  21 | `test_a_zero_size_request_still_yields_a_pointer`                 |   ✅   | A zero size request still yields a pointer                 |
|  22 | `test_set_add_limits`                                             |   ✅   | Set add limits                                             |
|  23 | `test_set_prefers_the_first_region_and_spills_to_the_second`      |   ✅   | Set prefers the first region and spills to the second      |
|  24 | `test_set_free_routes_by_address`                                 |   ✅   | Set free routes by address                                 |
|  25 | `test_set_mark_release_spans_every_region`                        |   ✅   | Set mark release spans every region                        |
|  26 | `test_set_release_of_a_mark_taken_before_a_region_joined`         |   ✅   | Set release of a mark taken before a region joined         |
|  27 | `test_set_exhaustion_and_free_bytes`                              |   ✅   | Set exhaustion and free bytes                              |

</details>

---

## test_arena - native_mmgr_arena - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the double-ended arena (mmgr/arena.h)._

|   # | Test                                                              | Status | Description                                                |
| --: | :---------------------------------------------------------------- | :----: | :--------------------------------------------------------- |
|   1 | `test_persist_alloc_is_aligned_and_inside_the_region`             |   ✅   | Persist alloc is aligned and inside the region             |
|   2 | `test_persist_alloc_is_zeroed_on_carve_and_on_reuse`              |   ✅   | Persist alloc is zeroed on carve and on reuse              |
|   3 | `test_persist_reuses_a_freed_hole`                                |   ✅   | Persist reuses a freed hole                                |
|   4 | `test_persist_free_coalesces_adjacent_holes`                      |   ✅   | Persist free coalesces adjacent holes                      |
|   5 | `test_persist_skips_a_hole_that_is_too_small`                     |   ✅   | Persist skips a hole that is too small                     |
|   6 | `test_persist_free_of_the_top_block_returns_the_middle`           |   ✅   | Persist free of the top block returns the middle           |
|   7 | `test_persist_size_overflow_fails_closed`                         |   ✅   | Persist size overflow fails closed                         |
|   8 | `test_persist_double_free_and_null_free_are_noops`                |   ✅   | Persist double free and null free are noops                |
|   9 | `test_free_bytes_reaches_zero_without_crossing`                   |   ✅   | Free bytes reaches zero without crossing                   |
|  10 | `test_scratch_bumps_down_and_resets`                              |   ✅   | Scratch bumps down and resets                              |
|  11 | `test_scratch_mark_and_release`                                   |   ✅   | Scratch mark and release                                   |
|  12 | `test_scratch_release_rejects_a_mark_outside_the_region`          |   ✅   | Scratch release rejects a mark outside the region          |
|  13 | `test_scratch_alignment_is_clamped_to_what_the_region_guarantees` |   ✅   | Scratch alignment is clamped to what the region guarantees |
|  14 | `test_the_two_ends_never_overlap`                                 |   ✅   | The two ends never overlap                                 |
|  15 | `test_a_request_that_would_cross_fails_closed`                    |   ✅   | A request that would cross fails closed                    |
|  16 | `test_the_middle_floats_between_the_ends`                         |   ✅   | The middle floats between the ends                         |
|  17 | `test_a_borrow_owns_its_alignment_pad`                            |   ✅   | A borrow owns its alignment pad                            |
|  18 | `test_a_write_over_the_pad_hits_no_neighbour`                     |   ✅   | A write over the pad hits no neighbour                     |
|  19 | `test_owns_is_an_address_range_test`                              |   ✅   | Owns is an address range test                              |
|  20 | `test_a_zero_length_region_refuses_everything`                    |   ✅   | A zero length region refuses everything                    |
|  21 | `test_a_zero_size_request_still_yields_a_pointer`                 |   ✅   | A zero size request still yields a pointer                 |
|  22 | `test_set_add_limits`                                             |   ✅   | Set add limits                                             |
|  23 | `test_set_prefers_the_first_region_and_spills_to_the_second`      |   ✅   | Set prefers the first region and spills to the second      |
|  24 | `test_set_free_routes_by_address`                                 |   ✅   | Set free routes by address                                 |
|  25 | `test_set_mark_release_spans_every_region`                        |   ✅   | Set mark release spans every region                        |
|  26 | `test_set_release_of_a_mark_taken_before_a_region_joined`         |   ✅   | Set release of a mark taken before a region joined         |
|  27 | `test_set_exhaustion_and_free_bytes`                              |   ✅   | Set exhaustion and free bytes                              |

</details>

---

## test_atc - native_atc - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the ATC field-I/O snapshot (services/machine_tool/atc/atc.h)._

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_snapshot_partitions_the_map`        |   ✅   | Snapshot partitions the map        |
|   2 | `test_snapshot_of_an_empty_map`           |   ✅   | Snapshot of an empty map           |
|   3 | `test_snapshot_of_one_direction`          |   ✅   | Snapshot of one direction          |
|   4 | `test_point_names_are_json_escaped`       |   ✅   | Point names are json escaped       |
|   5 | `test_value_range`                        |   ✅   | Value range                        |
|   6 | `test_snapshot_refuses_a_short_buffer`    |   ✅   | Snapshot refuses a short buffer    |
|   7 | `test_snapshot_buffer_boundary`           |   ✅   | Snapshot buffer boundary           |
|   8 | `test_set_output_then_get`                |   ✅   | Set output then get                |
|   9 | `test_set_output_refuses_an_input`        |   ✅   | Set output refuses an input        |
|  10 | `test_unknown_point`                      |   ✅   | Unknown point                      |
|  11 | `test_names_match_whole`                  |   ✅   | Names match whole                  |
|  12 | `test_get_without_the_found_flag`         |   ✅   | Get without the found flag         |
|  13 | `test_accessors_refuse_missing_arguments` |   ✅   | Accessors refuse missing arguments |

</details>

---

## test_audit_log - native_audit_log - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the tamper-evident hash-chained audit log (server/security/audit_log/audit_log.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_chain_hash_is_sha256_over_the_documented_fields`   |   ✅   | Chain hash is sha256 over the documented fields   |
|   2 | `test_seq_is_monotonic_from_one`                         |   ✅   | Seq is monotonic from one                         |
|   3 | `test_untouched_chain_verifies`                          |   ✅   | Untouched chain verifies                          |
|   4 | `test_empty_log_verifies`                                |   ✅   | Empty log verifies                                |
|   5 | `test_every_covered_field_is_tamper_evident`             |   ✅   | Every covered field is tamper evident             |
|   6 | `test_reordering_breaks_the_chain`                       |   ✅   | Reordering breaks the chain                       |
|   7 | `test_identical_messages_hash_differently`               |   ✅   | Identical messages hash differently               |
|   8 | `test_the_retained_window_verifies_after_the_ring_wraps` |   ✅   | The retained window verifies after the ring wraps |
|   9 | `test_the_oldest_retained_record_is_still_anchored`      |   ✅   | The oldest retained record is still anchored      |
|  10 | `test_a_long_message_is_truncated`                       |   ✅   | A long message is truncated                       |
|  11 | `test_reset_returns_the_chain_to_genesis`                |   ✅   | Reset returns the chain to genesis                |
|  12 | `test_the_sink_receives_the_complete_record`             |   ✅   | The sink receives the complete record             |
|  13 | `test_category_names`                                    |   ✅   | Category names                                    |
|  14 | `test_format_renders_one_record`                         |   ✅   | Format renders one record                         |
|  15 | `test_format_escapes_the_message`                        |   ✅   | Format escapes the message                        |
|  16 | `test_format_fails_closed_at_every_short_capacity`       |   ✅   | Format fails closed at every short capacity       |
|  17 | `test_dump_reports_integrity`                            |   ✅   | Dump reports integrity                            |
|  18 | `test_dump_fails_closed_at_every_short_capacity`         |   ✅   | Dump fails closed at every short capacity         |
|  19 | `test_dump_of_an_empty_log`                              |   ✅   | Dump of an empty log                              |

</details>

---

## test_auth_lockout - native_auth_lockout - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the per-peer brute-force auth lockout (server/security/auth_lockout/auth_lockout.h)._

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_an_unseen_address_is_not_locked`                     |   ✅   | An unseen address is not locked                     |
|   2 | `test_below_the_threshold_nothing_locks`                   |   ✅   | Below the threshold nothing locks                   |
|   3 | `test_backoff_doubles_then_caps`                           |   ✅   | Backoff doubles then caps                           |
|   4 | `test_the_window_counts_down_and_expires`                  |   ✅   | The window counts down and expires                  |
|   5 | `test_a_later_failure_restarts_the_window`                 |   ✅   | A later failure restarts the window                 |
|   6 | `test_the_window_survives_the_millisecond_rollover`        |   ✅   | The window survives the millisecond rollover        |
|   7 | `test_success_clears_the_address`                          |   ✅   | Success clears the address                          |
|   8 | `test_success_from_an_unseen_address_touches_nothing`      |   ✅   | Success from an unseen address touches nothing      |
|   9 | `test_addresses_do_not_share_state`                        |   ✅   | Addresses do not share state                        |
|  10 | `test_v4_and_v6_are_different_peers`                       |   ✅   | V4 and v6 are different peers                       |
|  11 | `test_an_unspecified_address_is_never_locked`              |   ✅   | An unspecified address is never locked              |
|  12 | `test_every_slot_holds_its_own_lockout`                    |   ✅   | Every slot holds its own lockout                    |
|  13 | `test_a_flood_of_new_addresses_does_not_release_a_lockout` |   ✅   | A flood of new addresses does not release a lockout |
|  14 | `test_reset_releases_every_address`                        |   ✅   | Reset releases every address                        |
|  15 | `test_the_configured_bounds`                               |   ✅   | The configured bounds                               |

</details>

---

## test_bacnet - native_bacnet - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the BACnet/IP BVLC + NPDU + APDU codec (services/fieldbus/bacnet/bacnet.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_published_constants`                            |   ✅   | Published constants                            |
|   2 | `test_global_broadcast_who_is_datagram`               |   ✅   | Global broadcast who is datagram               |
|   3 | `test_bvlc_length_covers_the_whole_bvll`              |   ✅   | Bvlc length covers the whole bvll              |
|   4 | `test_bvlc_refusals`                                  |   ✅   | Bvlc refusals                                  |
|   5 | `test_npci_control_octet_is_assembled_from_the_bits`  |   ✅   | Npci control octet is assembled from the bits  |
|   6 | `test_npdu_with_a_destination_address`                |   ✅   | Npdu with a destination address                |
|   7 | `test_hop_count_follows_the_source_fields`            |   ✅   | Hop count follows the source fields            |
|   8 | `test_npdu_refusals`                                  |   ✅   | Npdu refusals                                  |
|   9 | `test_network_layer_message_is_flagged`               |   ✅   | Network layer message is flagged               |
|  10 | `test_who_is_with_limits_uses_context_tags`           |   ✅   | Who is with limits uses context tags           |
|  11 | `test_i_am_object_identifier_packs_type_and_instance` |   ✅   | I am object identifier packs type and instance |
|  12 | `test_read_property_request`                          |   ✅   | Read property request                          |
|  13 | `test_apdu_header_parse_per_pdu_type`                 |   ✅   | Apdu header parse per pdu type                 |
|  14 | `test_segmented_pdu_skips_the_sequence_and_window`    |   ✅   | Segmented pdu skips the sequence and window    |
|  15 | `test_unsupported_pdu_types_and_short_buffers`        |   ✅   | Unsupported pdu types and short buffers        |
|  16 | `test_datagram_round_trip`                            |   ✅   | Datagram round trip                            |

</details>

---

## test_base64 - native_base64 - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the base64 codec (network_drivers/presentation/codec/base64/base64.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_rfc4648_section_10_vectors`                  |   ✅   | Rfc4648 section 10 vectors                  |
|   2 | `test_rfc4648_alphabets_are_the_two_tables`        |   ✅   | Rfc4648 alphabets are the two tables        |
|   3 | `test_each_alphabet_rejects_the_others_characters` |   ✅   | Each alphabet rejects the others characters |
|   4 | `test_decode_rejects_malformed`                    |   ✅   | Decode rejects malformed                    |
|   5 | `test_decode_refuses_a_short_destination`          |   ✅   | Decode refuses a short destination          |
|   6 | `test_decode_guards_every_octet_of_a_quad`         |   ✅   | Decode guards every octet of a quad         |
|   7 | `test_url_decode_stops_at_padding`                 |   ✅   | Url decode stops at padding                 |
|   8 | `test_url_encode_carries_no_padding`               |   ✅   | Url encode carries no padding               |
|   9 | `test_url_decode_refuses_a_short_destination`      |   ✅   | Url decode refuses a short destination      |
|  10 | `test_round_trip_is_the_identity`                  |   ✅   | Round trip is the identity                  |

</details>

---

## test_base64 - native_codec_base64 - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the base64 codec (network_drivers/presentation/codec/base64/base64.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_rfc4648_section_10_vectors`                  |   ✅   | Rfc4648 section 10 vectors                  |
|   2 | `test_rfc4648_alphabets_are_the_two_tables`        |   ✅   | Rfc4648 alphabets are the two tables        |
|   3 | `test_each_alphabet_rejects_the_others_characters` |   ✅   | Each alphabet rejects the others characters |
|   4 | `test_decode_rejects_malformed`                    |   ✅   | Decode rejects malformed                    |
|   5 | `test_decode_refuses_a_short_destination`          |   ✅   | Decode refuses a short destination          |
|   6 | `test_decode_guards_every_octet_of_a_quad`         |   ✅   | Decode guards every octet of a quad         |
|   7 | `test_url_decode_stops_at_padding`                 |   ✅   | Url decode stops at padding                 |
|   8 | `test_url_encode_carries_no_padding`               |   ✅   | Url encode carries no padding               |
|   9 | `test_url_decode_refuses_a_short_destination`      |   ✅   | Url decode refuses a short destination      |
|  10 | `test_round_trip_is_the_identity`                  |   ✅   | Round trip is the identity                  |

</details>

---

## test_base64 - native_base64_scalar - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the base64 codec (network_drivers/presentation/codec/base64/base64.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_rfc4648_section_10_vectors`                  |   ✅   | Rfc4648 section 10 vectors                  |
|   2 | `test_rfc4648_alphabets_are_the_two_tables`        |   ✅   | Rfc4648 alphabets are the two tables        |
|   3 | `test_each_alphabet_rejects_the_others_characters` |   ✅   | Each alphabet rejects the others characters |
|   4 | `test_decode_rejects_malformed`                    |   ✅   | Decode rejects malformed                    |
|   5 | `test_decode_refuses_a_short_destination`          |   ✅   | Decode refuses a short destination          |
|   6 | `test_decode_guards_every_octet_of_a_quad`         |   ✅   | Decode guards every octet of a quad         |
|   7 | `test_url_decode_stops_at_padding`                 |   ✅   | Url decode stops at padding                 |
|   8 | `test_url_encode_carries_no_padding`               |   ✅   | Url encode carries no padding               |
|   9 | `test_url_decode_refuses_a_short_destination`      |   ✅   | Url decode refuses a short destination      |
|  10 | `test_round_trip_is_the_identity`                  |   ✅   | Round trip is the identity                  |

</details>

---

## test_base64 - native_codec_base64_scalar - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the base64 codec (network_drivers/presentation/codec/base64/base64.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_rfc4648_section_10_vectors`                  |   ✅   | Rfc4648 section 10 vectors                  |
|   2 | `test_rfc4648_alphabets_are_the_two_tables`        |   ✅   | Rfc4648 alphabets are the two tables        |
|   3 | `test_each_alphabet_rejects_the_others_characters` |   ✅   | Each alphabet rejects the others characters |
|   4 | `test_decode_rejects_malformed`                    |   ✅   | Decode rejects malformed                    |
|   5 | `test_decode_refuses_a_short_destination`          |   ✅   | Decode refuses a short destination          |
|   6 | `test_decode_guards_every_octet_of_a_quad`         |   ✅   | Decode guards every octet of a quad         |
|   7 | `test_url_decode_stops_at_padding`                 |   ✅   | Url decode stops at padding                 |
|   8 | `test_url_encode_carries_no_padding`               |   ✅   | Url encode carries no padding               |
|   9 | `test_url_decode_refuses_a_short_destination`      |   ✅   | Url decode refuses a short destination      |
|  10 | `test_round_trip_is_the_identity`                  |   ✅   | Round trip is the identity                  |

</details>

---

## test_bitio - native_bitio - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the LSB-first bit writer (mmgr/bitio.h)._

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_rfc1951_empty_fixed_block`                  |   ✅   | Rfc1951 empty fixed block                  |
|   2 | `test_rfc1951_stored_block_header`                |   ✅   | Rfc1951 stored block header                |
|   3 | `test_elements_enter_low_bit_first`               |   ✅   | Elements enter low bit first               |
|   4 | `test_put_takes_only_the_low_n_bits`              |   ✅   | Put takes only the low n bits              |
|   5 | `test_eight_bits_is_exactly_one_byte`             |   ✅   | Eight bits is exactly one byte             |
|   6 | `test_a_wide_put_spills_every_completed_byte`     |   ✅   | A wide put spills every completed byte     |
|   7 | `test_align_pads_the_partial_byte_with_zero`      |   ✅   | Align pads the partial byte with zero      |
|   8 | `test_align_on_a_boundary_writes_nothing`         |   ✅   | Align on a boundary writes nothing         |
|   9 | `test_exact_fill_is_not_an_overflow`              |   ✅   | Exact fill is not an overflow              |
|  10 | `test_a_byte_past_cap_latches_and_stores_nothing` |   ✅   | A byte past cap latches and stores nothing |
|  11 | `test_align_with_no_room_latches`                 |   ✅   | Align with no room latches                 |
|  12 | `test_overflow_stays_latched`                     |   ✅   | Overflow stays latched                     |

</details>

---

## test_bitio - native_mmgr_bitio - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the LSB-first bit writer (mmgr/bitio.h)._

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_rfc1951_empty_fixed_block`                  |   ✅   | Rfc1951 empty fixed block                  |
|   2 | `test_rfc1951_stored_block_header`                |   ✅   | Rfc1951 stored block header                |
|   3 | `test_elements_enter_low_bit_first`               |   ✅   | Elements enter low bit first               |
|   4 | `test_put_takes_only_the_low_n_bits`              |   ✅   | Put takes only the low n bits              |
|   5 | `test_eight_bits_is_exactly_one_byte`             |   ✅   | Eight bits is exactly one byte             |
|   6 | `test_a_wide_put_spills_every_completed_byte`     |   ✅   | A wide put spills every completed byte     |
|   7 | `test_align_pads_the_partial_byte_with_zero`      |   ✅   | Align pads the partial byte with zero      |
|   8 | `test_align_on_a_boundary_writes_nothing`         |   ✅   | Align on a boundary writes nothing         |
|   9 | `test_exact_fill_is_not_an_overflow`              |   ✅   | Exact fill is not an overflow              |
|  10 | `test_a_byte_past_cap_latches_and_stores_nothing` |   ✅   | A byte past cap latches and stores nothing |
|  11 | `test_align_with_no_room_latches`                 |   ✅   | Align with no room latches                 |
|  12 | `test_overflow_stays_latched`                     |   ✅   | Overflow stays latched                     |

</details>

---

## test_ble_gatt - native_ble_gatt - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Bluetooth ATT codec and the GATT characteristic bridge_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_core_spec_att_pdu_layout`               |   ✅   | Core spec att pdu layout               |
|   2 | `test_core_spec_opcode_values`                |   ✅   | Core spec opcode values                |
|   3 | `test_core_spec_characteristic_property_bits` |   ✅   | Core spec characteristic property bits |
|   4 | `test_build_parse_round_trip`                 |   ✅   | Build parse round trip                 |
|   5 | `test_parse_refuses_a_truncated_pdu`          |   ✅   | Parse refuses a truncated pdu          |
|   6 | `test_parse_value_absent_and_unknown_opcode`  |   ✅   | Parse value absent and unknown opcode  |
|   7 | `test_parsed_value_points_into_the_input`     |   ✅   | Parsed value points into the input     |
|   8 | `test_builders_fail_closed`                   |   ✅   | Builders fail closed                   |
|   9 | `test_characteristic_table_json`              |   ✅   | Characteristic table json              |
|  10 | `test_characteristic_table_json_fails_closed` |   ✅   | Characteristic table json fails closed |

</details>

---

## test_ble_gatt - native_ble_gatt_att - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Bluetooth ATT codec and the GATT characteristic bridge_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_core_spec_att_pdu_layout`               |   ✅   | Core spec att pdu layout               |
|   2 | `test_core_spec_opcode_values`                |   ✅   | Core spec opcode values                |
|   3 | `test_core_spec_characteristic_property_bits` |   ✅   | Core spec characteristic property bits |
|   4 | `test_build_parse_round_trip`                 |   ✅   | Build parse round trip                 |
|   5 | `test_parse_refuses_a_truncated_pdu`          |   ✅   | Parse refuses a truncated pdu          |
|   6 | `test_parse_value_absent_and_unknown_opcode`  |   ✅   | Parse value absent and unknown opcode  |
|   7 | `test_parsed_value_points_into_the_input`     |   ✅   | Parsed value points into the input     |
|   8 | `test_builders_fail_closed`                   |   ✅   | Builders fail closed                   |
|   9 | `test_characteristic_table_json`              |   ✅   | Characteristic table json              |
|  10 | `test_characteristic_table_json_fails_closed` |   ✅   | Characteristic table json fails closed |

</details>

---

## test_bus_capture - native_bus_capture - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the listen-only CAN capture framer (server/signaling/bus_capture.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_the_socketcan_layout_is_the_published_one`        |   ✅   | The socketcan layout is the published one        |
|   2 | `test_the_record_is_sixteen_octets`                     |   ✅   | The record is sixteen octets                     |
|   3 | `test_the_published_flag_bits`                          |   ✅   | The published flag bits                          |
|   4 | `test_the_identifier_width_and_the_extended_flag`       |   ✅   | The identifier width and the extended flag       |
|   5 | `test_a_remote_frame_sets_its_flag_and_carries_no_data` |   ✅   | A remote frame sets its flag and carries no data |
|   6 | `test_the_length_is_clamped_to_eight`                   |   ✅   | The length is clamped to eight                   |
|   7 | `test_the_reserved_octets_are_zeroed`                   |   ✅   | The reserved octets are zeroed                   |
|   8 | `test_the_identifier_is_big_endian`                     |   ✅   | The identifier is big endian                     |
|   9 | `test_a_short_buffer_writes_nothing`                    |   ✅   | A short buffer writes nothing                    |
|  10 | `test_null_arguments_are_refused`                       |   ✅   | Null arguments are refused                       |
|  11 | `test_the_framer_holds_nothing`                         |   ✅   | The framer holds nothing                         |
|  12 | `test_every_frame_field_reaches_the_record`             |   ✅   | Every frame field reaches the record             |
|  13 | `test_a_capture_with_no_sink_is_refused`                |   ✅   | A capture with no sink is refused                |

</details>

---

## test_bus_wire - native_bus_wire - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_bytes - native_bytes - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the byte verbs (mmgr/bytes.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_rfc4251_uint32_encoding`                        |   ✅   | Rfc4251 uint32 encoding                        |
|   2 | `test_rfc4251_string_encoding`                        |   ✅   | Rfc4251 string encoding                        |
|   3 | `test_rfc4251_mpint_examples`                         |   ✅   | Rfc4251 mpint examples                         |
|   4 | `test_mpint_wider_than_the_destination_is_refused`    |   ✅   | Mpint wider than the destination is refused    |
|   5 | `test_put_writes_and_counts`                          |   ✅   | Put writes and counts                          |
|   6 | `test_put_past_cap_reports_the_capacity_needed`       |   ✅   | Put past cap reports the capacity needed       |
|   7 | `test_put_be_writes_most_significant_byte_first`      |   ✅   | Put be writes most significant byte first      |
|   8 | `test_put_be_past_cap_counts_its_whole_width`         |   ✅   | Put be past cap counts its whole width         |
|   9 | `test_raw_stores_whole_or_not_at_all`                 |   ✅   | Raw stores whole or not at all                 |
|  10 | `test_take_be_advances_by_the_width`                  |   ✅   | Take be advances by the width                  |
|  11 | `test_take_be_at_the_end_and_past_it`                 |   ✅   | Take be at the end and past it                 |
|  12 | `test_take_be_refusal_is_sticky`                      |   ✅   | Take be refusal is sticky                      |
|  13 | `test_take_be_zero_width`                             |   ✅   | Take be zero width                             |
|  14 | `test_rd_u32_short_read_is_refused`                   |   ✅   | Rd u32 short read is refused                   |
|  15 | `test_rd_str_overlong_length_rewinds`                 |   ✅   | Rd str overlong length rewinds                 |
|  16 | `test_rd_str_full_range_length_cannot_wrap_the_bound` |   ✅   | Rd str full range length cannot wrap the bound |
|  17 | `test_rd_str_exact_fit_and_empty_string`              |   ✅   | Rd str exact fit and empty string              |

</details>

---

## test_bytes - native_mmgr_bytes - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the byte verbs (mmgr/bytes.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_rfc4251_uint32_encoding`                        |   ✅   | Rfc4251 uint32 encoding                        |
|   2 | `test_rfc4251_string_encoding`                        |   ✅   | Rfc4251 string encoding                        |
|   3 | `test_rfc4251_mpint_examples`                         |   ✅   | Rfc4251 mpint examples                         |
|   4 | `test_mpint_wider_than_the_destination_is_refused`    |   ✅   | Mpint wider than the destination is refused    |
|   5 | `test_put_writes_and_counts`                          |   ✅   | Put writes and counts                          |
|   6 | `test_put_past_cap_reports_the_capacity_needed`       |   ✅   | Put past cap reports the capacity needed       |
|   7 | `test_put_be_writes_most_significant_byte_first`      |   ✅   | Put be writes most significant byte first      |
|   8 | `test_put_be_past_cap_counts_its_whole_width`         |   ✅   | Put be past cap counts its whole width         |
|   9 | `test_raw_stores_whole_or_not_at_all`                 |   ✅   | Raw stores whole or not at all                 |
|  10 | `test_take_be_advances_by_the_width`                  |   ✅   | Take be advances by the width                  |
|  11 | `test_take_be_at_the_end_and_past_it`                 |   ✅   | Take be at the end and past it                 |
|  12 | `test_take_be_refusal_is_sticky`                      |   ✅   | Take be refusal is sticky                      |
|  13 | `test_take_be_zero_width`                             |   ✅   | Take be zero width                             |
|  14 | `test_rd_u32_short_read_is_refused`                   |   ✅   | Rd u32 short read is refused                   |
|  15 | `test_rd_str_overlong_length_rewinds`                 |   ✅   | Rd str overlong length rewinds                 |
|  16 | `test_rd_str_full_range_length_cannot_wrap_the_bound` |   ✅   | Rd str full range length cannot wrap the bound |
|  17 | `test_rd_str_exact_fit_and_empty_string`              |   ✅   | Rd str exact fit and empty string              |

</details>

---

## test_c37118 - native_c37118 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEEE C37.118.2 synchrophasor frame codec (services/energy/c37118/c37118.h)._

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_crc_ccitt_published_check_value`      |   ✅   | Crc ccitt published check value      |
|   2 | `test_command_frame_field_layout`           |   ✅   | Command frame field layout           |
|   3 | `test_frame_round_trip`                     |   ✅   | Frame round trip                     |
|   4 | `test_command_word_round_trip`              |   ✅   | Command word round trip              |
|   5 | `test_parse_rejects_a_corrupted_frame`      |   ✅   | Parse rejects a corrupted frame      |
|   6 | `test_parse_rejects_malformed_framing`      |   ✅   | Parse rejects malformed framing      |
|   7 | `test_build_refuses_an_undersized_buffer`   |   ✅   | Build refuses an undersized buffer   |
|   8 | `test_stat_all_zero_is_a_healthy_pmu`       |   ✅   | Stat all zero is a healthy pmu       |
|   9 | `test_stat_all_ones`                        |   ✅   | Stat all ones                        |
|  10 | `test_stat_flags_are_independent`           |   ✅   | Stat flags are independent           |
|  11 | `test_stat_multi_bit_fields`                |   ✅   | Stat multi bit fields                |
|  12 | `test_stat_is_refused_outside_a_data_frame` |   ✅   | Stat is refused outside a data frame |

</details>

---

## test_canopen - native_canopen - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the CANopen (CiA 301) message codec (services/fieldbus/canopen/canopen.h)._

|   # | Test                                                         | Status | Description                                           |
| --: | :----------------------------------------------------------- | :----: | :---------------------------------------------------- |
|   1 | `test_predefined_connection_set`                             |   ✅   | Predefined connection set                             |
|   2 | `test_expedited_sdo_upload_of_the_identity_vendor_id`        |   ✅   | Expedited sdo upload of the identity vendor id        |
|   3 | `test_sdo_expedited_download_encodes_the_unused_octet_count` |   ✅   | Sdo expedited download encodes the unused octet count |
|   4 | `test_sdo_abort_carries_the_code_little_endian`              |   ✅   | Sdo abort carries the code little endian              |
|   5 | `test_sdo_download_acknowledgement`                          |   ✅   | Sdo download acknowledgement                          |
|   6 | `test_nmt_node_control`                                      |   ✅   | Nmt node control                                      |
|   7 | `test_sync_and_emcy_share_a_function_code`                   |   ✅   | Sync and emcy share a function code                   |
|   8 | `test_heartbeat_state_and_toggle_bit`                        |   ✅   | Heartbeat state and toggle bit                        |
|   9 | `test_time_of_day`                                           |   ✅   | Time of day                                           |
|  10 | `test_pdo_bases_and_classification`                          |   ✅   | Pdo bases and classification                          |
|  11 | `test_classifier_rejects_extended_and_unknown`               |   ✅   | Classifier rejects extended and unknown               |
|  12 | `test_segmented_download_initiate`                           |   ✅   | Segmented download initiate                           |
|  13 | `test_segment_command_octet_layout`                          |   ✅   | Segment command octet layout                          |
|  14 | `test_segmented_upload_reassembly`                           |   ✅   | Segmented upload reassembly                           |

</details>

---

## test_cbor - native_cbor - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the CBOR codec (network_drivers/presentation/codec/cbor/cbor.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_rfc8949_appendix_a_vectors`                   |   ✅   | Rfc8949 appendix a vectors                   |
|   2 | `test_str_n_takes_its_length_from_the_caller`       |   ✅   | Str n takes its length from the caller       |
|   3 | `test_null_string_is_the_empty_text_item`           |   ✅   | Null string is the empty text item           |
|   4 | `test_put_label_writes_the_cbor_label_number`       |   ✅   | Put label writes the cbor label number       |
|   5 | `test_head_forms_round_trip`                        |   ✅   | Head forms round trip                        |
|   6 | `test_negative_integers_round_trip`                 |   ✅   | Negative integers round trip                 |
|   7 | `test_peek_names_every_item`                        |   ✅   | Peek names every item                        |
|   8 | `test_map_round_trips_item_by_item`                 |   ✅   | Map round trips item by item                 |
|   9 | `test_byte_string_round_trips`                      |   ✅   | Byte string round trips                      |
|  10 | `test_float_forms_read_back`                        |   ✅   | Float forms read back                        |
|  11 | `test_overflow_reports_the_size_it_needed`          |   ✅   | Overflow reports the size it needed          |
|  12 | `test_reserved_and_indefinite_heads_are_refused`    |   ✅   | Reserved and indefinite heads are refused    |
|  13 | `test_type_mismatch_fails_and_marks_the_reader`     |   ✅   | Type mismatch fails and marks the reader     |
|  14 | `test_declared_length_past_the_end_is_refused`      |   ✅   | Declared length past the end is refused      |
|  15 | `test_the_read_error_is_sticky`                     |   ✅   | The read error is sticky                     |
|  16 | `test_truncated_item_is_refused`                    |   ✅   | Truncated item is refused                    |
|  17 | `test_every_reader_fails_closed_on_an_empty_region` |   ✅   | Every reader fails closed on an empty region |

</details>

---

## test_cbor - native_codec_cbor - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the CBOR codec (network_drivers/presentation/codec/cbor/cbor.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_rfc8949_appendix_a_vectors`                   |   ✅   | Rfc8949 appendix a vectors                   |
|   2 | `test_str_n_takes_its_length_from_the_caller`       |   ✅   | Str n takes its length from the caller       |
|   3 | `test_null_string_is_the_empty_text_item`           |   ✅   | Null string is the empty text item           |
|   4 | `test_put_label_writes_the_cbor_label_number`       |   ✅   | Put label writes the cbor label number       |
|   5 | `test_head_forms_round_trip`                        |   ✅   | Head forms round trip                        |
|   6 | `test_negative_integers_round_trip`                 |   ✅   | Negative integers round trip                 |
|   7 | `test_peek_names_every_item`                        |   ✅   | Peek names every item                        |
|   8 | `test_map_round_trips_item_by_item`                 |   ✅   | Map round trips item by item                 |
|   9 | `test_byte_string_round_trips`                      |   ✅   | Byte string round trips                      |
|  10 | `test_float_forms_read_back`                        |   ✅   | Float forms read back                        |
|  11 | `test_overflow_reports_the_size_it_needed`          |   ✅   | Overflow reports the size it needed          |
|  12 | `test_reserved_and_indefinite_heads_are_refused`    |   ✅   | Reserved and indefinite heads are refused    |
|  13 | `test_type_mismatch_fails_and_marks_the_reader`     |   ✅   | Type mismatch fails and marks the reader     |
|  14 | `test_declared_length_past_the_end_is_refused`      |   ✅   | Declared length past the end is refused      |
|  15 | `test_the_read_error_is_sticky`                     |   ✅   | The read error is sticky                     |
|  16 | `test_truncated_item_is_refused`                    |   ✅   | Truncated item is refused                    |
|  17 | `test_every_reader_fails_closed_on_an_empty_region` |   ✅   | Every reader fails closed on an empty region |

</details>

---

## test_cc1101 - native_cc1101 - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                   | Status | Description                     |
| --: | :------------------------------------- | :----: | :------------------------------ |
|   1 | `test_init_configures_and_detects`     |   ✅   | Init configures and detects     |
|   2 | `test_init_fails_when_absent`          |   ✅   | Init fails when absent          |
|   3 | `test_send_writes_fifo_and_strobes_tx` |   ✅   | Send writes fifo and strobes tx |
|   4 | `test_send_rejects_bad_len`            |   ✅   | Send rejects bad len            |
|   5 | `test_tx_done`                         |   ✅   | Tx done                         |
|   6 | `test_set_rx`                          |   ✅   | Set rx                          |
|   7 | `test_recv_reads_packet_and_rssi`      |   ✅   | Recv reads packet and rssi      |
|   8 | `test_recv_empty`                      |   ✅   | Recv empty                      |
|   9 | `test_recv_truncates`                  |   ✅   | Recv truncates                  |
|  10 | `test_rssi_decode`                     |   ✅   | Rssi decode                     |
|  11 | `test_send_guard_subconditions`        |   ✅   | Send guard subconditions        |
|  12 | `test_init_null_args`                  |   ✅   | Init null args                  |
|  13 | `test_init_no_regs`                    |   ✅   | Init no regs                    |
|  14 | `test_tx_done_null_args`               |   ✅   | Tx done null args               |
|  15 | `test_set_rx_null_args`                |   ✅   | Set rx null args                |
|  16 | `test_recv_null_args`                  |   ✅   | Recv null args                  |
|  17 | `test_recv_bad_length`                 |   ✅   | Recv bad length                 |
|  18 | `test_send_null_spi`                   |   ✅   | Send null spi                   |

</details>

---

## test_cclink - native_cclink - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_checksum_is_the_low_byte_of_the_sum`        |   ✅   | Checksum is the low byte of the sum        |
|   2 | `test_frame_layout_and_length`                    |   ✅   | Frame layout and length                    |
|   3 | `test_build_parse_round_trip`                     |   ✅   | Build parse round trip                     |
|   4 | `test_any_single_octet_change_fails_verification` |   ✅   | Any single octet change fails verification |
|   5 | `test_bit_addressing_is_lsb_first`                |   ✅   | Bit addressing is lsb first                |
|   6 | `test_bit_accessors_round_trip_over_a_block`      |   ✅   | Bit accessors round trip over a block      |
|   7 | `test_word_accessor_is_little_endian`             |   ✅   | Word accessor is little endian             |
|   8 | `test_accessors_refuse_out_of_range`              |   ✅   | Accessors refuse out of range              |
|   9 | `test_build_refusals`                             |   ✅   | Build refusals                             |
|  10 | `test_parse_refusals_and_the_empty_payload`       |   ✅   | Parse refusals and the empty payload       |
|  11 | `test_bit_only_and_word_only_exchanges`           |   ✅   | Bit only and word only exchanges           |

</details>

---

## test_cia402 - native_cia402 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the CiA 402 / IEC 61800-7-201 drive profile (services/fieldbus/cia402/cia402.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_object_dictionary_indices`                            |   ✅   | Object dictionary indices                            |
|   2 | `test_statusword_mask_value_table`                          |   ✅   | Statusword mask value table                          |
|   3 | `test_bit_five_separates_quick_stop_from_operation_enabled` |   ✅   | Bit five separates quick stop from operation enabled |
|   4 | `test_unmatched_statusword_is_unknown`                      |   ✅   | Unmatched statusword is unknown                      |
|   5 | `test_statusword_flag_accessors`                            |   ✅   | Statusword flag accessors                            |
|   6 | `test_controlword_command_table`                            |   ✅   | Controlword command table                            |
|   7 | `test_enable_sequence_walks_the_state_machine`              |   ✅   | Enable sequence walks the state machine              |
|   8 | `test_sdo_setters_target_the_right_objects`                 |   ✅   | Sdo setters target the right objects                 |
|   9 | `test_sdo_get_u16_checks_the_index`                         |   ✅   | Sdo get u16 checks the index                         |
|  10 | `test_sdo_get_i32_is_signed`                                |   ✅   | Sdo get i32 is signed                                |
|  11 | `test_pdo_pack_and_unpack_round_trip`                       |   ✅   | Pdo pack and unpack round trip                       |

</details>

---

## test_cip - native_cip - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the CIP message codec (services/fieldbus/cip/cip.h)._

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_logical_segment_constants`                              |   ✅   | Logical segment constants                              |
|   2 | `test_identity_vendor_id_request`                             |   ✅   | Identity vendor id request                             |
|   3 | `test_get_attributes_all_has_no_attribute_segment`            |   ✅   | Get attributes all has no attribute segment            |
|   4 | `test_wide_ids_use_the_sixteen_bit_segment_form`              |   ✅   | Wide ids use the sixteen bit segment form              |
|   5 | `test_epath_is_always_word_aligned`                           |   ✅   | Epath is always word aligned                           |
|   6 | `test_set_attribute_single_appends_the_value`                 |   ✅   | Set attribute single appends the value                 |
|   7 | `test_parse_successful_response`                              |   ✅   | Parse successful response                              |
|   8 | `test_additional_status_is_counted_in_words`                  |   ✅   | Additional status is counted in words                  |
|   9 | `test_response_refusals`                                      |   ✅   | Response refusals                                      |
|  10 | `test_request_refuses_a_misaligned_or_oversized_path`         |   ✅   | Request refuses a misaligned or oversized path         |
|  11 | `test_epath_refuses_a_short_buffer`                           |   ✅   | Epath refuses a short buffer                           |
|  12 | `test_service_and_reply_service_differ_only_by_the_reply_bit` |   ✅   | Service and reply service differ only by the reply bit |

</details>

---

## test_cloudevents - native_cloudevents - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the CloudEvents envelope (services/iot/cloudevents/cloudevents.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_binary_mode_published_request`            |   ✅   | Binary mode published request            |
|   2 | `test_binary_mode_optional_subject`             |   ✅   | Binary mode optional subject             |
|   3 | `test_binary_mode_requires_id_source_and_type`  |   ✅   | Binary mode requires id source and type  |
|   4 | `test_structured_mode_required_attributes`      |   ✅   | Structured mode required attributes      |
|   5 | `test_structured_mode_media_type`               |   ✅   | Structured mode media type               |
|   6 | `test_structured_mode_json_data`                |   ✅   | Structured mode json data                |
|   7 | `test_structured_mode_string_data`              |   ✅   | Structured mode string data              |
|   8 | `test_structured_mode_stated_datacontenttype`   |   ✅   | Structured mode stated datacontenttype   |
|   9 | `test_structured_mode_optional_attributes`      |   ✅   | Structured mode optional attributes      |
|  10 | `test_structured_mode_refuses_missing_required` |   ✅   | Structured mode refuses missing required |
|  11 | `test_structured_mode_refuses_a_short_buffer`   |   ✅   | Structured mode refuses a short buffer   |
|  12 | `test_attribute_values_are_json_escaped`        |   ✅   | Attribute values are json escaped        |
|  13 | `test_binary_read_feeds_a_structured_build`     |   ✅   | Binary read feeds a structured build     |

</details>

---

## test_compliance - native_compliance - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_http11_missing_host_rejected`                   |   ✅   | Http11 missing host rejected                   |
|   2 | `test_http11_with_host_ok`                            |   ✅   | Http11 with host ok                            |
|   3 | `test_http10_missing_host_ok`                         |   ✅   | Http10 missing host ok                         |
|   4 | `test_duplicate_host_rejected`                        |   ✅   | Duplicate host rejected                        |
|   5 | `test_duplicate_host_rejected_http10`                 |   ✅   | Duplicate host rejected http10                 |
|   6 | `test_host_beyond_max_headers_still_counted`          |   ✅   | Host beyond max headers still counted          |
|   7 | `test_duplicate_host_with_one_beyond_cap_rejected`    |   ✅   | Duplicate host with one beyond cap rejected    |
|   8 | `test_content_length_non_digit_rejected`              |   ✅   | Content length non digit rejected              |
|   9 | `test_content_length_empty_rejected`                  |   ✅   | Content length empty rejected                  |
|  10 | `test_content_length_conflicting_duplicate_rejected`  |   ✅   | Content length conflicting duplicate rejected  |
|  11 | `test_content_length_matching_duplicate_ok`           |   ✅   | Content length matching duplicate ok           |
|  12 | `test_content_length_valid_body`                      |   ✅   | Content length valid body                      |
|  13 | `test_transfer_encoding_chunked_rejected`             |   ✅   | Transfer encoding chunked rejected             |
|  14 | `test_transfer_encoding_with_content_length_rejected` |   ✅   | Transfer encoding with content length rejected |
|  15 | `test_transfer_encoding_case_insensitive_rejected`    |   ✅   | Transfer encoding case insensitive rejected    |

</details>

---

## test_concurrency - native_concurrency - ✅ 2 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                         | Status | Description           |
| --: | :--------------------------- | :----: | :-------------------- |
|   1 | `test_spsc_ring_no_race`     |   ✅   | Spsc ring no race     |
|   2 | `test_state_handoff_no_race` |   ✅   | State handoff no race |

</details>

---

## test_config_io - native_config_io - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for schema-driven config export / restore (server/storage/config_io/config_io.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_export_writes_one_key_value_line_per_field`  |   ✅   | Export writes one key value line per field  |
|   2 | `test_export_carries_every_field_even_when_unset`  |   ✅   | Export carries every field even when unset  |
|   3 | `test_export_import_round_trip`                    |   ✅   | Export import round trip                    |
|   4 | `test_import_is_idempotent`                        |   ✅   | Import is idempotent                        |
|   5 | `test_import_writes_only_keys_the_schema_declares` |   ✅   | Import writes only keys the schema declares |
|   6 | `test_import_steps_over_a_keyless_schema_entry`    |   ✅   | Import steps over a keyless schema entry    |
|   7 | `test_import_rejects_a_field_of_an_unknown_type`   |   ✅   | Import rejects a field of an unknown type   |
|   8 | `test_import_skips_a_line_with_no_separator`       |   ✅   | Import skips a line with no separator       |
|   9 | `test_import_splits_on_the_first_separator`        |   ✅   | Import splits on the first separator        |
|  10 | `test_import_drops_a_line_past_the_store_limits`   |   ✅   | Import drops a line past the store limits   |
|  11 | `test_export_fails_closed_on_a_short_buffer`       |   ✅   | Export fails closed on a short buffer       |
|  12 | `test_missing_arguments_are_refused`               |   ✅   | Missing arguments are refused               |

</details>

---

## test_config_store - native_config_store - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the typed NVS configuration store (server/storage/config_store/config_store.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_string_round_trip`                             |   ✅   | String round trip                             |
|   2 | `test_u32_round_trip`                                |   ✅   | U32 round trip                                |
|   3 | `test_blob_round_trip`                               |   ✅   | Blob round trip                               |
|   4 | `test_an_absent_key_reports_the_default`             |   ✅   | An absent key reports the default             |
|   5 | `test_an_overlong_key_is_refused_not_truncated`      |   ✅   | An overlong key is refused not truncated      |
|   6 | `test_namespaces_hold_separate_values_for_one_key`   |   ✅   | Namespaces hold separate values for one key   |
|   7 | `test_erase_drops_one_key_and_clear_drops_them_all`  |   ✅   | Erase drops one key and clear drops them all  |
|   8 | `test_an_unusable_namespace_is_refused`              |   ✅   | An unusable namespace is refused              |
|   9 | `test_a_short_destination_is_bounded_and_terminated` |   ✅   | A short destination is bounded and terminated |
|  10 | `test_a_read_with_no_room_is_refused`                |   ✅   | A read with no room is refused                |
|  11 | `test_a_write_with_no_value_is_refused`              |   ✅   | A write with no value is refused              |

</details>

---

## test_control - native_control - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the PID control law (services/system/control/control.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_proportional_term`                         |   ✅   | Proportional term                         |
|   2 | `test_integral_term_accumulates`                 |   ✅   | Integral term accumulates                 |
|   3 | `test_derivative_acts_on_the_measurement`        |   ✅   | Derivative acts on the measurement        |
|   4 | `test_setpoint_step_produces_no_derivative_kick` |   ✅   | Setpoint step produces no derivative kick |
|   5 | `test_derivative_low_pass`                       |   ✅   | Derivative low pass                       |
|   6 | `test_feedforward_term`                          |   ✅   | Feedforward term                          |
|   7 | `test_output_clamping`                           |   ✅   | Output clamping                           |
|   8 | `test_anti_windup_freezes_and_releases`          |   ✅   | Anti windup freezes and releases          |
|   9 | `test_integral_hard_clamp`                       |   ✅   | Integral hard clamp                       |
|  10 | `test_all_four_terms_together`                   |   ✅   | All four terms together                   |
|  11 | `test_non_positive_dt_is_not_a_step`             |   ✅   | Non positive dt is not a step             |
|  12 | `test_fixed_rate_matches_the_variable_rate_law`  |   ✅   | Fixed rate matches the variable rate law  |
|  13 | `test_reset_clears_only_the_runtime_state`       |   ✅   | Reset clears only the runtime state       |
|  14 | `test_init_defaults`                             |   ✅   | Init defaults                             |
|  15 | `test_batched_update_matches_the_single_loop`    |   ✅   | Batched update matches the single loop    |
|  16 | `test_control_primitives`                        |   ✅   | Control primitives                        |
|  17 | `test_slew_reaches_the_target_without_overshoot` |   ✅   | Slew reaches the target without overshoot |
|  18 | `test_log_header_layout`                         |   ✅   | Log header layout                         |
|  19 | `test_log_record_layout`                         |   ✅   | Log record layout                         |
|  20 | `test_closed_loop_settles_on_the_setpoint`       |   ✅   | Closed loop settles on the setpoint       |

</details>

---

## test_control - native_system_control - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the PID control law (services/system/control/control.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_proportional_term`                         |   ✅   | Proportional term                         |
|   2 | `test_integral_term_accumulates`                 |   ✅   | Integral term accumulates                 |
|   3 | `test_derivative_acts_on_the_measurement`        |   ✅   | Derivative acts on the measurement        |
|   4 | `test_setpoint_step_produces_no_derivative_kick` |   ✅   | Setpoint step produces no derivative kick |
|   5 | `test_derivative_low_pass`                       |   ✅   | Derivative low pass                       |
|   6 | `test_feedforward_term`                          |   ✅   | Feedforward term                          |
|   7 | `test_output_clamping`                           |   ✅   | Output clamping                           |
|   8 | `test_anti_windup_freezes_and_releases`          |   ✅   | Anti windup freezes and releases          |
|   9 | `test_integral_hard_clamp`                       |   ✅   | Integral hard clamp                       |
|  10 | `test_all_four_terms_together`                   |   ✅   | All four terms together                   |
|  11 | `test_non_positive_dt_is_not_a_step`             |   ✅   | Non positive dt is not a step             |
|  12 | `test_fixed_rate_matches_the_variable_rate_law`  |   ✅   | Fixed rate matches the variable rate law  |
|  13 | `test_reset_clears_only_the_runtime_state`       |   ✅   | Reset clears only the runtime state       |
|  14 | `test_init_defaults`                             |   ✅   | Init defaults                             |
|  15 | `test_batched_update_matches_the_single_loop`    |   ✅   | Batched update matches the single loop    |
|  16 | `test_control_primitives`                        |   ✅   | Control primitives                        |
|  17 | `test_slew_reaches_the_target_without_overshoot` |   ✅   | Slew reaches the target without overshoot |
|  18 | `test_log_header_layout`                         |   ✅   | Log header layout                         |
|  19 | `test_log_record_layout`                         |   ✅   | Log record layout                         |
|  20 | `test_closed_loop_settles_on_the_setpoint`       |   ✅   | Closed loop settles on the setpoint       |

</details>

---

## test_cotp - native_cotp - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TPKT + COTP class-0 codec (services/fieldbus/cotp/cotp.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_published_constants`                           |   ✅   | Published constants                           |
|   2 | `test_smallest_tpkt_is_seven_octets`                 |   ✅   | Smallest tpkt is seven octets                 |
|   3 | `test_tpkt_length_includes_the_header`               |   ✅   | Tpkt length includes the header               |
|   4 | `test_consumed_advances_past_one_packet_in_a_stream` |   ✅   | Consumed advances past one packet in a stream |
|   5 | `test_tpkt_refusals`                                 |   ✅   | Tpkt refusals                                 |
|   6 | `test_data_tpdu_layout`                              |   ✅   | Data tpdu layout                              |
|   7 | `test_connection_request_layout`                     |   ✅   | Connection request layout                     |
|   8 | `test_connection_request_with_tsap_parameters`       |   ✅   | Connection request with tsap parameters       |
|   9 | `test_connection_confirm_echoes_the_peer_reference`  |   ✅   | Connection confirm echoes the peer reference  |
|  10 | `test_type_is_the_high_nibble`                       |   ✅   | Type is the high nibble                       |
|  11 | `test_cotp_refusals`                                 |   ✅   | Cotp refusals                                 |
|  12 | `test_stack_round_trip`                              |   ✅   | Stack round trip                              |

</details>

---

## test_crypto_kat - native_crypto_kat - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Data-driven external known-answer tests for the library's shared crypto primitives: HMAC-SHA256,_

|   # | Test                               | Status | Description                 |
| --: | :--------------------------------- | :----: | :-------------------------- |
|   1 | `test_hmac_sha256`                 |   ✅   | Hmac sha256                 |
|   2 | `test_hmac_sha512`                 |   ✅   | Hmac sha512                 |
|   3 | `test_aes128gcm`                   |   ✅   | Aes128gcm                   |
|   4 | `test_aes128gcm_counter_carry`     |   ✅   | Aes128gcm counter carry     |
|   5 | `test_x25519`                      |   ✅   | X25519                      |
|   6 | `test_ed25519_verify`              |   ✅   | Ed25519 verify              |
|   7 | `test_ed25519_sign`                |   ✅   | Ed25519 sign                |
|   8 | `test_hkdf_extract`                |   ✅   | Hkdf extract                |
|   9 | `test_hkdf_expand`                 |   ✅   | Hkdf expand                 |
|  10 | `test_hkdf_expand_length_bound`    |   ✅   | Hkdf expand length bound    |
|  11 | `test_chacha20_block`              |   ✅   | Chacha20 block              |
|  12 | `test_poly1305`                    |   ✅   | Poly1305                    |
|  13 | `test_vector_tables_are_populated` |   ✅   | Vector tables are populated |

</details>

---

## test_crypto_kat - native_wycheproof_kat - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Data-driven external known-answer tests for the library's shared crypto primitives: HMAC-SHA256,_

|   # | Test                               | Status | Description                 |
| --: | :--------------------------------- | :----: | :-------------------------- |
|   1 | `test_hmac_sha256`                 |   ✅   | Hmac sha256                 |
|   2 | `test_hmac_sha512`                 |   ✅   | Hmac sha512                 |
|   3 | `test_aes128gcm`                   |   ✅   | Aes128gcm                   |
|   4 | `test_aes128gcm_counter_carry`     |   ✅   | Aes128gcm counter carry     |
|   5 | `test_x25519`                      |   ✅   | X25519                      |
|   6 | `test_ed25519_verify`              |   ✅   | Ed25519 verify              |
|   7 | `test_ed25519_sign`                |   ✅   | Ed25519 sign                |
|   8 | `test_hkdf_extract`                |   ✅   | Hkdf extract                |
|   9 | `test_hkdf_expand`                 |   ✅   | Hkdf expand                 |
|  10 | `test_hkdf_expand_length_bound`    |   ✅   | Hkdf expand length bound    |
|  11 | `test_chacha20_block`              |   ✅   | Chacha20 block              |
|  12 | `test_poly1305`                    |   ✅   | Poly1305                    |
|  13 | `test_vector_tables_are_populated` |   ✅   | Vector tables are populated |

</details>

---

## test_csrf - native_csrf - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the stateless HMAC-signed CSRF token (server/security/csrf/csrf.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_token_is_the_documented_hmac_over_the_nonce` |   ✅   | Token is the documented hmac over the nonce |
|   2 | `test_the_nonce_counter_advances_by_one`           |   ✅   | The nonce counter advances by one           |
|   3 | `test_the_token_shape_is_fixed`                    |   ✅   | The token shape is fixed                    |
|   4 | `test_issued_tokens_verify`                        |   ✅   | Issued tokens verify                        |
|   5 | `test_successive_tokens_differ`                    |   ✅   | Successive tokens differ                    |
|   6 | `test_every_signature_character_is_checked`        |   ✅   | Every signature character is checked        |
|   7 | `test_every_nonce_character_is_checked`            |   ✅   | Every nonce character is checked            |
|   8 | `test_a_token_is_bound_to_its_secret`              |   ✅   | A token is bound to its secret              |
|   9 | `test_the_secret_is_capped_at_thirty_two_octets`   |   ✅   | The secret is capped at thirty two octets   |
|  10 | `test_no_secret_fails_closed`                      |   ✅   | No secret fails closed                      |
|  11 | `test_reset_restarts_the_counter`                  |   ✅   | Reset restarts the counter                  |
|  12 | `test_malformed_tokens_are_refused`                |   ✅   | Malformed tokens are refused                |
|  13 | `test_an_odd_length_nonce_is_refused`              |   ✅   | An odd length nonce is refused              |
|  14 | `test_issue_refuses_a_short_buffer`                |   ✅   | Issue refuses a short buffer                |
|  15 | `test_verify_holds_no_state`                       |   ✅   | Verify holds no state                       |

</details>

---

## test_ct_eq - native_ct_eq - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the library's one secret-dependent comparator (crypto/ct_eq.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_equal_buffers_match`                      |   ✅   | Equal buffers match                      |
|   2 | `test_zero_length_is_equal`                     |   ✅   | Zero length is equal                     |
|   3 | `test_aliased_pointer_is_equal`                 |   ✅   | Aliased pointer is equal                 |
|   4 | `test_a_difference_at_every_position_is_caught` |   ✅   | A difference at every position is caught |
|   5 | `test_a_single_flipped_bit_is_caught`           |   ✅   | A single flipped bit is caught           |
|   6 | `test_the_walk_stops_at_n`                      |   ✅   | The walk stops at n                      |
|   7 | `test_differences_do_not_cancel`                |   ✅   | Differences do not cancel                |
|   8 | `test_the_comparison_is_symmetric`              |   ✅   | The comparison is symmetric              |
|   9 | `test_the_high_bit_is_not_lost`                 |   ✅   | The high bit is not lost                 |

</details>

---

## test_ct_eq - native_ct_eq_unit - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the library's one secret-dependent comparator (crypto/ct_eq.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_equal_buffers_match`                      |   ✅   | Equal buffers match                      |
|   2 | `test_zero_length_is_equal`                     |   ✅   | Zero length is equal                     |
|   3 | `test_aliased_pointer_is_equal`                 |   ✅   | Aliased pointer is equal                 |
|   4 | `test_a_difference_at_every_position_is_caught` |   ✅   | A difference at every position is caught |
|   5 | `test_a_single_flipped_bit_is_caught`           |   ✅   | A single flipped bit is caught           |
|   6 | `test_the_walk_stops_at_n`                      |   ✅   | The walk stops at n                      |
|   7 | `test_differences_do_not_cancel`                |   ✅   | Differences do not cancel                |
|   8 | `test_the_comparison_is_symmetric`              |   ✅   | The comparison is symmetric              |
|   9 | `test_the_high_bit_is_not_lost`                 |   ✅   | The high bit is not lost                 |

</details>

---

## test_dashboard - native_dashboard - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the dashboard widget table and its JSON serializers (server/web/dashboard/dashboard.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_layout_is_an_rfc8259_array_of_objects`            |   ✅   | Layout is an rfc8259 array of objects            |
|   2 | `test_values_is_an_rfc8259_object_of_every_key`         |   ✅   | Values is an rfc8259 object of every key         |
|   3 | `test_a_reading_for_an_undeclared_key_is_refused`       |   ✅   | A reading for an undeclared key is refused       |
|   4 | `test_rebinding_the_table_clears_the_readings`          |   ✅   | Rebinding the table clears the readings          |
|   5 | `test_every_widget_style_has_a_name`                    |   ✅   | Every widget style has a name                    |
|   6 | `test_a_label_is_escaped_per_rfc8259_section_7`         |   ✅   | A label is escaped per rfc8259 section 7         |
|   7 | `test_a_control_character_in_a_label_is_escaped`        |   ✅   | A control character in a label is escaped        |
|   8 | `test_a_control_message_yields_its_key_and_value`       |   ✅   | A control message yields its key and value       |
|   9 | `test_a_malformed_control_message_is_refused`           |   ✅   | A malformed control message is refused           |
|  10 | `test_dispatch_reaches_the_registered_callback`         |   ✅   | Dispatch reaches the registered callback         |
|  11 | `test_a_table_past_the_widget_limit_is_clamped`         |   ✅   | A table past the widget limit is clamped         |
|  12 | `test_serializing_with_nothing_to_serialize_is_refused` |   ✅   | Serializing with nothing to serialize is refused |
|  13 | `test_a_short_buffer_fails_closed`                      |   ✅   | A short buffer fails closed                      |

</details>

---

## test_dbm - native_dbm - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_put_get_overwrite`                                  |   ✅   | Put get overwrite                                  |
|   2 | `test_delete_and_contains`                                |   ✅   | Delete and contains                                |
|   3 | `test_persist_across_reboot_with_checkpoint`              |   ✅   | Persist across reboot with checkpoint              |
|   4 | `test_persist_across_reboot_without_checkpoint`           |   ✅   | Persist across reboot without checkpoint           |
|   5 | `test_delete_persists_across_reboot`                      |   ✅   | Delete persists across reboot                      |
|   6 | `test_many_keys_and_collisions`                           |   ✅   | Many keys and collisions                           |
|   7 | `test_index_full_fails_closed`                            |   ✅   | Index full fails closed                            |
|   8 | `test_bounds_and_empty_value`                             |   ✅   | Bounds and empty value                             |
|   9 | `test_max_value_roundtrip`                                |   ✅   | Max value roundtrip                                |
|  10 | `test_compact_reclaims_space`                             |   ✅   | Compact reclaims space                             |
|  11 | `test_compact_dest_too_small_fails_closed`                |   ✅   | Compact dest too small fails closed                |
|  12 | `test_compact_source_read_failure`                        |   ✅   | Compact source read failure                        |
|  13 | `test_compact_checkpoint_failure`                         |   ✅   | Compact checkpoint failure                         |
|  14 | `test_replay_skips_malformed_records`                     |   ✅   | Replay skips malformed records                     |
|  15 | `test_reopen_rejects_a_log_with_more_keys_than_slots`     |   ✅   | Reopen rejects a log with more keys than slots     |
|  16 | `test_probe_walks_a_saturated_table_for_an_absent_key`    |   ✅   | Probe walks a saturated table for an absent key    |
|  17 | `test_insert_reuses_a_tombstone_in_a_saturated_table`     |   ✅   | Insert reuses a tombstone in a saturated table     |
|  18 | `test_hash_collision_slots_are_walked_past`               |   ✅   | Hash collision slots are walked past               |
|  19 | `test_put_rejects_an_empty_key`                           |   ✅   | Put rejects an empty key                           |
|  20 | `test_put_fails_closed_when_the_log_is_full`              |   ✅   | Put fails closed when the log is full              |
|  21 | `test_get_fails_when_the_value_cannot_be_read_back`       |   ✅   | Get fails when the value cannot be read back       |
|  22 | `test_iterate_visits_live_keys_and_honours_an_early_stop` |   ✅   | Iterate visits live keys and honours an early stop |
|  23 | `test_compact_carries_empty_values`                       |   ✅   | Compact carries empty values                       |

</details>

---

## test_dds - native_dds - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_header_matches_the_published_layout`               |   ✅   | Header matches the published layout               |
|   2 | `test_header_refuses_a_short_buffer`                     |   ✅   | Header refuses a short buffer                     |
|   3 | `test_submessage_header_is_four_octets_then_contents`    |   ✅   | Submessage header is four octets then contents    |
|   4 | `test_octets_to_next_header_follows_the_endianness_flag` |   ✅   | Octets to next header follows the endianness flag |
|   5 | `test_parse_walks_every_submessage`                      |   ✅   | Parse walks every submessage                      |
|   6 | `test_zero_octets_to_next_header_runs_to_the_end`        |   ✅   | Zero octets to next header runs to the end        |
|   7 | `test_pad_and_info_ts_do_not_swallow_the_rest`           |   ✅   | Pad and info ts do not swallow the rest           |
|   8 | `test_parse_refuses_a_foreign_protocol`                  |   ✅   | Parse refuses a foreign protocol                  |
|   9 | `test_parse_refuses_an_unsupported_version`              |   ✅   | Parse refuses an unsupported version              |
|  10 | `test_parse_refuses_a_message_short_of_the_header`       |   ✅   | Parse refuses a message short of the header       |
|  11 | `test_contents_past_the_end_are_refused`                 |   ✅   | Contents past the end are refused                 |
|  12 | `test_a_trailing_stub_ends_the_walk`                     |   ✅   | A trailing stub ends the walk                     |
|  13 | `test_submessage_kinds_round_trip`                       |   ✅   | Submessage kinds round trip                       |
|  14 | `test_submessage_refuses_a_short_buffer`                 |   ✅   | Submessage refuses a short buffer                 |
|  15 | `test_parse_without_a_sink_still_validates`              |   ✅   | Parse without a sink still validates              |

</details>

---

## test_dds - native_dds_rtps - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_header_matches_the_published_layout`               |   ✅   | Header matches the published layout               |
|   2 | `test_header_refuses_a_short_buffer`                     |   ✅   | Header refuses a short buffer                     |
|   3 | `test_submessage_header_is_four_octets_then_contents`    |   ✅   | Submessage header is four octets then contents    |
|   4 | `test_octets_to_next_header_follows_the_endianness_flag` |   ✅   | Octets to next header follows the endianness flag |
|   5 | `test_parse_walks_every_submessage`                      |   ✅   | Parse walks every submessage                      |
|   6 | `test_zero_octets_to_next_header_runs_to_the_end`        |   ✅   | Zero octets to next header runs to the end        |
|   7 | `test_pad_and_info_ts_do_not_swallow_the_rest`           |   ✅   | Pad and info ts do not swallow the rest           |
|   8 | `test_parse_refuses_a_foreign_protocol`                  |   ✅   | Parse refuses a foreign protocol                  |
|   9 | `test_parse_refuses_an_unsupported_version`              |   ✅   | Parse refuses an unsupported version              |
|  10 | `test_parse_refuses_a_message_short_of_the_header`       |   ✅   | Parse refuses a message short of the header       |
|  11 | `test_contents_past_the_end_are_refused`                 |   ✅   | Contents past the end are refused                 |
|  12 | `test_a_trailing_stub_ends_the_walk`                     |   ✅   | A trailing stub ends the walk                     |
|  13 | `test_submessage_kinds_round_trip`                       |   ✅   | Submessage kinds round trip                       |
|  14 | `test_submessage_refuses_a_short_buffer`                 |   ✅   | Submessage refuses a short buffer                 |
|  15 | `test_parse_without_a_sink_still_validates`              |   ✅   | Parse without a sink still validates              |

</details>

---

## test_deflate - native_deflate - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the RFC 1951 DEFLATE compressor_

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_rfc1951_fixed_block_bytes`                   |   ✅   | Rfc1951 fixed block bytes                   |
|   2 | `test_payload_is_a_non_final_fixed_block`          |   ✅   | Payload is a non final fixed block          |
|   3 | `test_marker_is_stripped_from_the_reported_length` |   ✅   | Marker is stripped from the reported length |
|   4 | `test_round_trip_text`                             |   ✅   | Round trip text                             |
|   5 | `test_round_trip_empty`                            |   ✅   | Round trip empty                            |
|   6 | `test_round_trip_single_byte`                      |   ✅   | Round trip single byte                      |
|   7 | `test_round_trip_every_octet_value`                |   ✅   | Round trip every octet value                |
|   8 | `test_repetitive_input_shrinks`                    |   ✅   | Repetitive input shrinks                    |
|   9 | `test_json_frame_shrinks`                          |   ✅   | Json frame shrinks                          |
|  10 | `test_hash_chain_exhaustion_round_trips`           |   ✅   | Hash chain exhaustion round trips           |
|  11 | `test_match_past_the_window_is_not_used`           |   ✅   | Match past the window is not used           |
|  12 | `test_longest_match_round_trips`                   |   ✅   | Longest match round trips                   |
|  13 | `test_random_input_round_trips`                    |   ✅   | Random input round trips                    |
|  14 | `test_low_entropy_input_round_trips`               |   ✅   | Low entropy input round trips               |
|  15 | `test_output_overflow_fails_closed`                |   ✅   | Output overflow fails closed                |
|  16 | `test_scratch_too_small_fails_closed`              |   ✅   | Scratch too small fails closed              |

</details>

---

## test_deflate - native_codec_deflate - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the RFC 1951 DEFLATE compressor_

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_rfc1951_fixed_block_bytes`                   |   ✅   | Rfc1951 fixed block bytes                   |
|   2 | `test_payload_is_a_non_final_fixed_block`          |   ✅   | Payload is a non final fixed block          |
|   3 | `test_marker_is_stripped_from_the_reported_length` |   ✅   | Marker is stripped from the reported length |
|   4 | `test_round_trip_text`                             |   ✅   | Round trip text                             |
|   5 | `test_round_trip_empty`                            |   ✅   | Round trip empty                            |
|   6 | `test_round_trip_single_byte`                      |   ✅   | Round trip single byte                      |
|   7 | `test_round_trip_every_octet_value`                |   ✅   | Round trip every octet value                |
|   8 | `test_repetitive_input_shrinks`                    |   ✅   | Repetitive input shrinks                    |
|   9 | `test_json_frame_shrinks`                          |   ✅   | Json frame shrinks                          |
|  10 | `test_hash_chain_exhaustion_round_trips`           |   ✅   | Hash chain exhaustion round trips           |
|  11 | `test_match_past_the_window_is_not_used`           |   ✅   | Match past the window is not used           |
|  12 | `test_longest_match_round_trips`                   |   ✅   | Longest match round trips                   |
|  13 | `test_random_input_round_trips`                    |   ✅   | Random input round trips                    |
|  14 | `test_low_entropy_input_round_trips`               |   ✅   | Low entropy input round trips               |
|  15 | `test_output_overflow_fails_closed`                |   ✅   | Output overflow fails closed                |
|  16 | `test_scratch_too_small_fails_closed`              |   ✅   | Scratch too small fails closed              |

</details>

---

## test_device_id - native_device_id - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MAC-derived device UUID (server/signaling/device_id.h)._

|   # | Test                                     | Status | Description                       |
| --: | :--------------------------------------- | :----: | :-------------------------------- |
|   1 | `test_rfc9562_published_uuidv5_vector`   |   ✅   | Rfc9562 published uuidv5 vector   |
|   2 | `test_the_uuid_is_uuidv5_of_the_mac_hex` |   ✅   | The uuid is uuidv5 of the mac hex |
|   3 | `test_the_version_and_variant_nibbles`   |   ✅   | The version and variant nibbles   |
|   4 | `test_the_text_form`                     |   ✅   | The text form                     |
|   5 | `test_the_uuid_is_stable_for_a_mac`      |   ✅   | The uuid is stable for a mac      |
|   6 | `test_every_mac_octet_changes_the_uuid`  |   ✅   | Every mac octet changes the uuid  |
|   7 | `test_the_name_is_lowercase_hex`         |   ✅   | The name is lowercase hex         |

</details>

---

## test_devicenet - native_devicenet - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DeviceNet link adaptation (services/fieldbus/devicenet/devicenet.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_published_constants`                            |   ✅   | Published constants                            |
|   2 | `test_group_ranges_match_the_identifier_allocation`   |   ✅   | Group ranges match the identifier allocation   |
|   3 | `test_group_three_message_id_seven_has_no_identifier` |   ✅   | Group three message id seven has no identifier |
|   4 | `test_identifier_round_trip`                          |   ✅   | Identifier round trip                          |
|   5 | `test_duplicate_mac_id_check_identifier`              |   ✅   | Duplicate mac id check identifier              |
|   6 | `test_invalid_identifiers_and_fields`                 |   ✅   | Invalid identifiers and fields                 |
|   7 | `test_message_header_octet`                           |   ✅   | Message header octet                           |
|   8 | `test_fragmentation_octet`                            |   ✅   | Fragmentation octet                            |
|   9 | `test_single_frame_explicit_message`                  |   ✅   | Single frame explicit message                  |
|  10 | `test_fragment_frame_layout`                          |   ✅   | Fragment frame layout                          |
|  11 | `test_unfragmented_message_completes_immediately`     |   ✅   | Unfragmented message completes immediately     |
|  12 | `test_fragmented_message_reassembly`                  |   ✅   | Fragmented message reassembly                  |
|  13 | `test_reassembly_refusals`                            |   ✅   | Reassembly refusals                            |
|  14 | `test_a_new_first_fragment_restarts_the_message`      |   ✅   | A new first fragment restarts the message      |
|  15 | `test_fragment_count_wraps_at_sixty_four`             |   ✅   | Fragment count wraps at sixty four             |

</details>

---

## test_df1 - native_df1 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Allen-Bradley DF1 full-duplex frame codec (services/fieldbus/df1/df1.h)._

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_control_characters`                    |   ✅   | Control characters                    |
|   2 | `test_crc_matches_the_published_check_value` |   ✅   | Crc matches the published check value |
|   3 | `test_bcc_and_data_sum_to_zero`              |   ✅   | Bcc and data sum to zero              |
|   4 | `test_bcc_frame_layout`                      |   ✅   | Bcc frame layout                      |
|   5 | `test_crc_frame_covers_the_data_and_the_etx` |   ✅   | Crc frame covers the data and the etx |
|   6 | `test_dle_bytes_are_doubled_on_the_wire`     |   ✅   | Dle bytes are doubled on the wire     |
|   7 | `test_round_trip_over_every_octet_value`     |   ✅   | Round trip over every octet value     |
|   8 | `test_empty_message`                         |   ✅   | Empty message                         |
|   9 | `test_a_corrupted_frame_fails_its_check`     |   ✅   | A corrupted frame fails its check     |
|  10 | `test_framing_refusals`                      |   ✅   | Framing refusals                      |
|  11 | `test_capacity_refusals`                     |   ✅   | Capacity refusals                     |
|  12 | `test_out_len_is_optional`                   |   ✅   | Out len is optional                   |

</details>

---

## test_digest_vectors - native_sha256_kat - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for SHA-256 (crypto/hash/sha256.h), the digest under the SSH exchange hash, the TLS_

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc6234_published_vectors`                 |   ✅   | Rfc6234 published vectors                 |
|   2 | `test_rfc6234_one_million_a`                     |   ✅   | Rfc6234 one million a                     |
|   3 | `test_empty_message`                             |   ✅   | Empty message                             |
|   4 | `test_chunk_boundaries_do_not_change_the_digest` |   ✅   | Chunk boundaries do not change the digest |
|   5 | `test_empty_update_is_a_no_op`                   |   ✅   | Empty update is a no op                   |
|   6 | `test_final_leaves_the_context_running`          |   ✅   | Final leaves the context running          |
|   7 | `test_one_shot_matches_streaming`                |   ✅   | One shot matches streaming                |
|   8 | `test_distinct_messages_hash_differently`        |   ✅   | Distinct messages hash differently        |
|   9 | `test_block_length_constants`                    |   ✅   | Block length constants                    |

</details>

---

## test_directnet - native_directnet - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DirectNET serial frame codec (services/fieldbus/directnet/directnet.h)._

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_ascii_control_codes`                |   ✅   | Ascii control codes                |
|   2 | `test_lrc_block_xors_to_zero`             |   ✅   | Lrc block xors to zero             |
|   3 | `test_header_frame`                       |   ✅   | Header frame                       |
|   4 | `test_header_hex_digits_are_uppercase`    |   ✅   | Header hex digits are uppercase    |
|   5 | `test_data_frame`                         |   ✅   | Data frame                         |
|   6 | `test_data_frame_round_trip`              |   ✅   | Data frame round trip              |
|   7 | `test_data_parse_optional_outputs`        |   ✅   | Data parse optional outputs        |
|   8 | `test_single_octet_corruption_is_refused` |   ✅   | Single octet corruption is refused |
|   9 | `test_data_parse_rejects_bad_framing`     |   ✅   | Data parse rejects bad framing     |
|  10 | `test_builders_refuse_a_short_buffer`     |   ✅   | Builders refuse a short buffer     |

</details>

---

## test_dmx - native_dmx - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DMX512 + RDM codec (server/peripherals/dmx/dmx.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_e120_table_6_6_checksum_example`                      |   ✅   | E120 table 6 6 checksum example                      |
|   2 | `test_e120_appendix_a_constants`                            |   ✅   | E120 appendix a constants                            |
|   3 | `test_e120_message_length_points_at_the_checksum_high_slot` |   ✅   | E120 message length points at the checksum high slot |
|   4 | `test_e120_uid_is_manufacturer_above_device`                |   ✅   | E120 uid is manufacturer above device                |
|   5 | `test_e120_table_7_1_discovery_response_encoding`           |   ✅   | E120 table 7 1 discovery response encoding           |
|   6 | `test_e120_table_7_2_discovery_response_decoding`           |   ✅   | E120 table 7 2 discovery response decoding           |
|   7 | `test_discovery_response_round_trips`                       |   ✅   | Discovery response round trips                       |
|   8 | `test_discovery_response_builder_guards`                    |   ✅   | Discovery response builder guards                    |
|   9 | `test_e120_device_info_block`                               |   ✅   | E120 device info block                               |
|  10 | `test_device_info_rides_a_get_response_packet`              |   ✅   | Device info rides a get response packet              |
|  11 | `test_e120_parse_discards_malformed_packets`                |   ✅   | E120 parse discards malformed packets                |
|  12 | `test_rdm_build_guards`                                     |   ✅   | Rdm build guards                                     |
|  13 | `test_e111_slot_array`                                      |   ✅   | E111 slot array                                      |
|  14 | `test_e111_universe_is_512_slots`                           |   ✅   | E111 universe is 512 slots                           |
|  15 | `test_dmx_build_guards`                                     |   ✅   | Dmx build guards                                     |

</details>

---

## test_dnc - native_dnc - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the CNC DNC drip-feed codec (services/machine_tool/dnc/dnc.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_eia_codes_carry_odd_parity`                |   ✅   | Eia codes carry odd parity                |
|   2 | `test_eia_letters_and_digits_are_derived`        |   ✅   | Eia letters and digits are derived        |
|   3 | `test_eia_translation_is_a_bijection`            |   ✅   | Eia translation is a bijection            |
|   4 | `test_eia_refuses_characters_it_has_no_code_for` |   ✅   | Eia refuses characters it has no code for |
|   5 | `test_eia_special_codes`                         |   ✅   | Eia special codes                         |
|   6 | `test_iso_parity_is_even`                        |   ✅   | Iso parity is even                        |
|   7 | `test_xon_xoff_flow_state`                       |   ✅   | Xon xoff flow state                       |
|   8 | `test_iso_block_framing`                         |   ✅   | Iso block framing                         |
|   9 | `test_iso_block_carries_parity`                  |   ✅   | Iso block carries parity                  |
|  10 | `test_eia_block_framing`                         |   ✅   | Eia block framing                         |
|  11 | `test_eia_block_fails_closed`                    |   ✅   | Eia block fails closed                    |
|  12 | `test_block_refuses_a_short_buffer`              |   ✅   | Block refuses a short buffer              |
|  13 | `test_program_marker`                            |   ✅   | Program marker                            |
|  14 | `test_leader_runout`                             |   ✅   | Leader runout                             |
|  15 | `test_decoder_reports_markers_and_lines`         |   ✅   | Decoder reports markers and lines         |
|  16 | `test_decoder_skips_runout`                      |   ✅   | Decoder skips runout                      |
|  17 | `test_decoder_ignores_a_blank_block`             |   ✅   | Decoder ignores a blank block             |
|  18 | `test_decoder_drops_an_overlong_block`           |   ✅   | Decoder drops an overlong block           |
|  19 | `test_decoder_accepts_a_full_length_block`       |   ✅   | Decoder accepts a full length block       |
|  20 | `test_decoder_strips_iso_parity`                 |   ✅   | Decoder strips iso parity                 |
|  21 | `test_program_round_trip`                        |   ✅   | Program round trip                        |
|  22 | `test_marker_discards_a_partial_block`           |   ✅   | Marker discards a partial block           |
|  23 | `test_eia_decoder_does_not_filter_flow_bytes`    |   ✅   | Eia decoder does not filter flow bytes    |

</details>

---

## test_dnc_stream - native_dnc - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_iso_roundtrip`                          |   ✅   | Iso roundtrip                          |
|   2 | `test_eia_roundtrip`                          |   ✅   | Eia roundtrip                          |
|   3 | `test_crlf_and_parity`                        |   ✅   | Crlf and parity                        |
|   4 | `test_xoff_pacing`                            |   ✅   | Xoff pacing                            |
|   5 | `test_leader_trailer`                         |   ✅   | Leader trailer                         |
|   6 | `test_empty_program`                          |   ✅   | Empty program                          |
|   7 | `test_encode_error`                           |   ✅   | Encode error                           |
|   8 | `test_io_error_and_args`                      |   ✅   | Io error and args                      |
|   9 | `test_null_send_or_recv_rejected`             |   ✅   | Null send or recv rejected             |
|  10 | `test_reverse_channel_error_fails_the_stream` |   ✅   | Reverse channel error fails the stream |
|  11 | `test_xoff_never_released_gives_up`           |   ✅   | Xoff never released gives up           |
|  12 | `test_reverse_channel_error_while_paused`     |   ✅   | Reverse channel error while paused     |
|  13 | `test_send_failure_at_each_stage`             |   ✅   | Send failure at each stage             |
|  14 | `test_blank_lines_and_crlf_source`            |   ✅   | Blank lines and crlf source            |

</details>

---

## test_dnp3 - native_dnp3 - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DNP3 (IEEE 1815) link, transport and application codec (services/energy/dnp3/dnp3.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_crc16_dnp_published_check_value`                   |   ✅   | Crc16 dnp published check value                   |
|   2 | `test_header_block_field_layout`                         |   ✅   | Header block field layout                         |
|   3 | `test_user_data_is_carried_in_crc_protected_blocks`      |   ✅   | User data is carried in crc protected blocks      |
|   4 | `test_frame_round_trip_across_block_boundaries`          |   ✅   | Frame round trip across block boundaries          |
|   5 | `test_parse_rejects_a_corrupted_block`                   |   ✅   | Parse rejects a corrupted block                   |
|   6 | `test_parse_rejects_malformed_framing`                   |   ✅   | Parse rejects malformed framing                   |
|   7 | `test_build_refuses_oversized_or_unbuffered_frames`      |   ✅   | Build refuses oversized or unbuffered frames      |
|   8 | `test_transport_header_bit_layout`                       |   ✅   | Transport header bit layout                       |
|   9 | `test_transport_segment_build`                           |   ✅   | Transport segment build                           |
|  10 | `test_transport_reassembles_a_multi_segment_fragment`    |   ✅   | Transport reassembles a multi segment fragment    |
|  11 | `test_transport_sequence_wraps_at_sixty_four`            |   ✅   | Transport sequence wraps at sixty four            |
|  12 | `test_transport_discards_out_of_sequence_segments`       |   ✅   | Transport discards out of sequence segments       |
|  13 | `test_transport_overflow_abandons_the_fragment`          |   ✅   | Transport overflow abandons the fragment          |
|  14 | `test_application_control_bit_layout`                    |   ✅   | Application control bit layout                    |
|  15 | `test_application_request_round_trip`                    |   ✅   | Application request round trip                    |
|  16 | `test_application_response_carries_internal_indications` |   ✅   | Application response carries internal indications |
|  17 | `test_object_header_range_picks_the_narrowest_form`      |   ✅   | Object header range picks the narrowest form      |
|  18 | `test_object_header_all_objects`                         |   ✅   | Object header all objects                         |
|  19 | `test_object_header_count_forms_and_prefix_code`         |   ✅   | Object header count forms and prefix code         |
|  20 | `test_crob_field_layout`                                 |   ✅   | Crob field layout                                 |
|  21 | `test_analog_output_block_int32`                         |   ✅   | Analog output block int32                         |
|  22 | `test_analog_output_block_float`                         |   ✅   | Analog output block float                         |

</details>

---

## test_dns_resolver - native_dns_resolver - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DNS resolver (network_drivers/network/dns/dns_resolver.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_rfc1035_published_qname`                          |   ✅   | Rfc1035 published qname                          |
|   2 | `test_query_build_refuses_what_does_not_fit`            |   ✅   | Query build refuses what does not fit            |
|   3 | `test_answer_parse_reads_the_a_record`                  |   ✅   | Answer parse reads the a record                  |
|   4 | `test_answer_parse_walks_past_other_types`              |   ✅   | Answer parse walks past other types              |
|   5 | `test_answer_parse_requires_the_id_to_match`            |   ✅   | Answer parse requires the id to match            |
|   6 | `test_answer_parse_requires_a_response_with_rcode_zero` |   ✅   | Answer parse requires a response with rcode zero |
|   7 | `test_answer_parse_requires_an_a_record_in_class_in`    |   ✅   | Answer parse requires an a record in class in    |
|   8 | `test_answer_parse_refuses_a_truncated_message`         |   ✅   | Answer parse refuses a truncated message         |
|   9 | `test_classify_matches_the_registry`                    |   ✅   | Classify matches the registry                    |
|  10 | `test_verify_refuses_what_cannot_be_a_remote_host`      |   ✅   | Verify refuses what cannot be a remote host      |
|  11 | `test_a_literal_answers_itself`                         |   ✅   | A literal answers itself                         |
|  12 | `test_resolve_refuses_a_null_host`                      |   ✅   | Resolve refuses a null host                      |
|  13 | `test_a_name_puts_one_question_on_the_wire`             |   ✅   | A name puts one question on the wire             |
|  14 | `test_the_answer_completes_the_resolve`                 |   ✅   | The answer completes the resolve                 |
|  15 | `test_a_foreign_response_does_not_end_the_query`        |   ✅   | A foreign response does not end the query        |
|  16 | `test_the_query_ends_at_its_deadline`                   |   ✅   | The query ends at its deadline                   |
|  17 | `test_set_server_takes_only_an_address`                 |   ✅   | Set server takes only an address                 |
|  18 | `test_resolve_verified_refuses_an_implausible_answer`   |   ✅   | Resolve verified refuses an implausible answer   |

</details>

---

## test_dns_wire - native_dns_wire - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DNS name codec (network_drivers/network/dns/dns_wire.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_rfc1035_worked_message`                            |   ✅   | Rfc1035 worked message                            |
|   2 | `test_encode_is_length_prefixed_labels_and_a_root_octet` |   ✅   | Encode is length prefixed labels and a root octet |
|   3 | `test_encode_decode_round_trip`                          |   ✅   | Encode decode round trip                          |
|   4 | `test_label_length_limit`                                |   ✅   | Label length limit                                |
|   5 | `test_reserved_label_types_are_refused`                  |   ✅   | Reserved label types are refused                  |
|   6 | `test_pointer_loops_terminate`                           |   ✅   | Pointer loops terminate                           |
|   7 | `test_pointers_are_refused_when_not_allowed`             |   ✅   | Pointers are refused when not allowed             |
|   8 | `test_truncated_names_are_refused`                       |   ✅   | Truncated names are refused                       |
|   9 | `test_output_buffer_bounds`                              |   ✅   | Output buffer bounds                              |
|  10 | `test_encode_buffer_bounds`                              |   ✅   | Encode buffer bounds                              |
|  11 | `test_empty_labels_inside_a_name_are_refused`            |   ✅   | Empty labels inside a name are refused            |
|  12 | `test_case_insensitive_comparison`                       |   ✅   | Case insensitive comparison                       |
|  13 | `test_pointer_hop_cap`                                   |   ✅   | Pointer hop cap                                   |
|  14 | `test_failure_reports_zero_progress`                     |   ✅   | Failure reports zero progress                     |

</details>

---

## test_dns_wire - native_dns_wire_codec - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DNS name codec (network_drivers/network/dns/dns_wire.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_rfc1035_worked_message`                            |   ✅   | Rfc1035 worked message                            |
|   2 | `test_encode_is_length_prefixed_labels_and_a_root_octet` |   ✅   | Encode is length prefixed labels and a root octet |
|   3 | `test_encode_decode_round_trip`                          |   ✅   | Encode decode round trip                          |
|   4 | `test_label_length_limit`                                |   ✅   | Label length limit                                |
|   5 | `test_reserved_label_types_are_refused`                  |   ✅   | Reserved label types are refused                  |
|   6 | `test_pointer_loops_terminate`                           |   ✅   | Pointer loops terminate                           |
|   7 | `test_pointers_are_refused_when_not_allowed`             |   ✅   | Pointers are refused when not allowed             |
|   8 | `test_truncated_names_are_refused`                       |   ✅   | Truncated names are refused                       |
|   9 | `test_output_buffer_bounds`                              |   ✅   | Output buffer bounds                              |
|  10 | `test_encode_buffer_bounds`                              |   ✅   | Encode buffer bounds                              |
|  11 | `test_empty_labels_inside_a_name_are_refused`            |   ✅   | Empty labels inside a name are refused            |
|  12 | `test_case_insensitive_comparison`                       |   ✅   | Case insensitive comparison                       |
|  13 | `test_pointer_hop_cap`                                   |   ✅   | Pointer hop cap                                   |
|  14 | `test_failure_reports_zero_progress`                     |   ✅   | Failure reports zero progress                     |

</details>

---

## test_docstore - native_docstore - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                   | Status | Description                     |
| --: | :------------------------------------- | :----: | :------------------------------ |
|   1 | `test_put_get_del`                     |   ✅   | Put get del                     |
|   2 | `test_find_by_field`                   |   ✅   | Find by field                   |
|   3 | `test_find_bool`                       |   ✅   | Find bool                       |
|   4 | `test_persist_and_query_across_reboot` |   ✅   | Persist and query across reboot |
|   5 | `test_find_early_stop`                 |   ✅   | Find early stop                 |
|   6 | `test_find_field_absent`               |   ✅   | Find field absent               |
|   7 | `test_find_count_only_null_cb`         |   ✅   | Find count only null cb         |
|   8 | `test_find_skips_unreadable_document`  |   ✅   | Find skips unreadable document  |

</details>

---

## test_dshot - native_dshot - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DShot ESC throttle codec (server/peripherals/dshot/dshot.h)._

|   # | Test                                                                | Status | Description                                                  |
| --: | :------------------------------------------------------------------ | :----: | :----------------------------------------------------------- |
|   1 | `test_published_crc_over_worked_frames`                             |   ✅   | Published crc over worked frames                             |
|   2 | `test_bidirectional_inverts_only_the_checksum`                      |   ✅   | Bidirectional inverts only the checksum                      |
|   3 | `test_encode_decode_round_trip_over_the_whole_value_domain`         |   ✅   | Encode decode round trip over the whole value domain         |
|   4 | `test_every_single_bit_error_is_rejected`                           |   ✅   | Every single bit error is rejected                           |
|   5 | `test_the_two_crc_conventions_do_not_accept_each_other`             |   ✅   | The two crc conventions do not accept each other             |
|   6 | `test_values_wider_than_eleven_bits_are_masked`                     |   ✅   | Values wider than eleven bits are masked                     |
|   7 | `test_published_command_and_throttle_domains`                       |   ✅   | Published command and throttle domains                       |
|   8 | `test_bit_timing_is_three_quarters_and_three_eighths_of_the_period` |   ✅   | Bit timing is three quarters and three eighths of the period |
|   9 | `test_unknown_bit_rates_return_zero`                                |   ✅   | Unknown bit rates return zero                                |
|  10 | `test_analog_pulse_width_endpoints_and_midpoint`                    |   ✅   | Analog pulse width endpoints and midpoint                    |
|  11 | `test_analog_pulse_width_is_monotone_and_bounded`                   |   ✅   | Analog pulse width is monotone and bounded                   |

</details>

---

## test_dtls_record - native_dtls - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DTLS 1.3 record layer_

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_rfc9147_4_plaintext_record_layout`          |   ✅   | Rfc9147 4 plaintext record layout          |
|   2 | `test_legacy_record_version_is_ignored`           |   ✅   | Legacy record version is ignored           |
|   3 | `test_plaintext_parse_bounds`                     |   ✅   | Plaintext parse bounds                     |
|   4 | `test_rfc9147_4_unified_header`                   |   ✅   | Rfc9147 4 unified header                   |
|   5 | `test_rfc9147_4_2_3_sequence_number_encryption`   |   ✅   | Rfc9147 4 2 3 sequence number encryption   |
|   6 | `test_rfc9147_4_2_2_sequence_reconstruction`      |   ✅   | Rfc9147 4 2 2 sequence reconstruction      |
|   7 | `test_a_wrong_sequence_number_fails_deprotection` |   ✅   | A wrong sequence number fails deprotection |
|   8 | `test_epoch_bits_select_the_keys`                 |   ✅   | Epoch bits select the keys                 |
|   9 | `test_rfc9146_connection_id`                      |   ✅   | Rfc9146 connection id                      |
|  10 | `test_inner_content_type_and_padding`             |   ✅   | Inner content type and padding             |
|  11 | `test_rfc9147_4_5_2_invalid_records`              |   ✅   | Rfc9147 4 5 2 invalid records              |
|  12 | `test_rfc9147_4_5_1_replay_window`                |   ✅   | Rfc9147 4 5 1 replay window                |
|  13 | `test_round_trip_over_lengths`                    |   ✅   | Round trip over lengths                    |

</details>

---

## test_dtls_handshake - native_dtls_hs - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_hs_header_roundtrip`                 |   ✅   | Hs header roundtrip                 |
|   2 | `test_hs_frag_build_rejects`               |   ✅   | Hs frag build rejects               |
|   3 | `test_hs_reasm_header_guards`              |   ✅   | Hs reasm header guards              |
|   4 | `test_ack_build_rejects`                   |   ✅   | Ack build rejects                   |
|   5 | `test_cookie_make_rejects`                 |   ✅   | Cookie make rejects                 |
|   6 | `test_cookie_empty_payload_roundtrip`      |   ✅   | Cookie empty payload roundtrip      |
|   7 | `test_cookie_verify_structural_rejects`    |   ✅   | Cookie verify structural rejects    |
|   8 | `test_hs_header_parse_rejects`             |   ✅   | Hs header parse rejects             |
|   9 | `test_hs_reasm_single_fragment`            |   ✅   | Hs reasm single fragment            |
|  10 | `test_hs_reasm_in_order`                   |   ✅   | Hs reasm in order                   |
|  11 | `test_hs_reasm_out_of_order`               |   ✅   | Hs reasm out of order               |
|  12 | `test_hs_reasm_overlap_and_duplicate`      |   ✅   | Hs reasm overlap and duplicate      |
|  13 | `test_hs_reasm_conflicting_overlap_aborts` |   ✅   | Hs reasm conflicting overlap aborts |
|  14 | `test_hs_reasm_wrong_msg_seq_ignored`      |   ✅   | Hs reasm wrong msg seq ignored      |
|  15 | `test_hs_reasm_empty_body`                 |   ✅   | Hs reasm empty body                 |
|  16 | `test_hs_reasm_rejects`                    |   ✅   | Hs reasm rejects                    |
|  17 | `test_ack_roundtrip`                       |   ✅   | Ack roundtrip                       |
|  18 | `test_ack_parse_rejects`                   |   ✅   | Ack parse rejects                   |
|  19 | `test_cookie_kat`                          |   ✅   | Cookie kat                          |
|  20 | `test_cookie_verify_accept_and_payload`    |   ✅   | Cookie verify accept and payload    |
|  21 | `test_cookie_verify_rejects`               |   ✅   | Cookie verify rejects               |
|  22 | `test_cookie_freshness`                    |   ✅   | Cookie freshness                    |

</details>

---

## test_dtls_tls13 - native_dtls_tls13 - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS 1.3 messages the DTLS 1.3 handshake adds to the shared message codec_

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_rfc8446_hello_retry_request_random`         |   ✅   | Rfc8446 hello retry request random         |
|   2 | `test_dtls_hello_retry_request_bytes`             |   ✅   | Dtls hello retry request bytes             |
|   3 | `test_tls_hello_retry_request_codepoints`         |   ✅   | Tls hello retry request codepoints         |
|   4 | `test_rfc8446_message_hash`                       |   ✅   | Rfc8446 message hash                       |
|   5 | `test_rfc8446_empty_encrypted_extensions`         |   ✅   | Rfc8446 empty encrypted extensions         |
|   6 | `test_rfc7250_negotiated_server_certificate_type` |   ✅   | Rfc7250 negotiated server certificate type |
|   7 | `test_rfc8410_ed25519_spki`                       |   ✅   | Rfc8410 ed25519 spki                       |
|   8 | `test_rfc7250_raw_public_key_certificate`         |   ✅   | Rfc7250 raw public key certificate         |
|   9 | `test_rfc8446_cookie_extension_is_parsed`         |   ✅   | Rfc8446 cookie extension is parsed         |
|  10 | `test_rfc7250_server_certificate_type_is_parsed`  |   ✅   | Rfc7250 server certificate type is parsed  |
|  11 | `test_rfc9146_connection_id_is_parsed`            |   ✅   | Rfc9146 connection id is parsed            |
|  12 | `test_rfc9147_legacy_cookie_must_be_empty`        |   ✅   | Rfc9147 legacy cookie must be empty        |
|  13 | `test_builders_refuse_a_short_destination`        |   ✅   | Builders refuse a short destination        |

</details>

---

## test_dtls_tls13 - native_dtls_tls13_rfc - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS 1.3 messages the DTLS 1.3 handshake adds to the shared message codec_

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_rfc8446_hello_retry_request_random`         |   ✅   | Rfc8446 hello retry request random         |
|   2 | `test_dtls_hello_retry_request_bytes`             |   ✅   | Dtls hello retry request bytes             |
|   3 | `test_tls_hello_retry_request_codepoints`         |   ✅   | Tls hello retry request codepoints         |
|   4 | `test_rfc8446_message_hash`                       |   ✅   | Rfc8446 message hash                       |
|   5 | `test_rfc8446_empty_encrypted_extensions`         |   ✅   | Rfc8446 empty encrypted extensions         |
|   6 | `test_rfc7250_negotiated_server_certificate_type` |   ✅   | Rfc7250 negotiated server certificate type |
|   7 | `test_rfc8410_ed25519_spki`                       |   ✅   | Rfc8410 ed25519 spki                       |
|   8 | `test_rfc7250_raw_public_key_certificate`         |   ✅   | Rfc7250 raw public key certificate         |
|   9 | `test_rfc8446_cookie_extension_is_parsed`         |   ✅   | Rfc8446 cookie extension is parsed         |
|  10 | `test_rfc7250_server_certificate_type_is_parsed`  |   ✅   | Rfc7250 server certificate type is parsed  |
|  11 | `test_rfc9146_connection_id_is_parsed`            |   ✅   | Rfc9146 connection id is parsed            |
|  12 | `test_rfc9147_legacy_cookie_must_be_empty`        |   ✅   | Rfc9147 legacy cookie must be empty        |
|  13 | `test_builders_refuse_a_short_destination`        |   ✅   | Builders refuse a short destination        |

</details>

---

## test_edge_fetch - native_edge_cache - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_fetch_content_length`                     |   ✅   | Fetch content length                     |
|   2 | `test_fetch_chunked`                            |   ✅   | Fetch chunked                            |
|   3 | `test_fetch_close_delimited`                    |   ✅   | Fetch close delimited                    |
|   4 | `test_fetch_oversize`                           |   ✅   | Fetch oversize                           |
|   5 | `test_fetch_timeout`                            |   ✅   | Fetch timeout                            |
|   6 | `test_fetch_open_fail`                          |   ✅   | Fetch open fail                          |
|   7 | `test_resp_complete_unit`                       |   ✅   | Resp complete unit                       |
|   8 | `test_fetch_send_fail`                          |   ✅   | Fetch send fail                          |
|   9 | `test_fetch_end_releases_once`                  |   ✅   | Fetch end releases once                  |
|  10 | `test_fetch_pump_after_terminal_is_inert`       |   ✅   | Fetch pump after terminal is inert       |
|  11 | `test_fetch_malformed_status_line`              |   ✅   | Fetch malformed status line              |
|  12 | `test_fetch_closed_before_complete`             |   ✅   | Fetch closed before complete             |
|  13 | `test_chunked_hex_sizes`                        |   ✅   | Chunked hex sizes                        |
|  14 | `test_chunked_trailers`                         |   ✅   | Chunked trailers                         |
|  15 | `test_head_end_near_miss_separators`            |   ✅   | Head end near miss separators            |
|  16 | `test_unusable_framing_headers_fall_through`    |   ✅   | Unusable framing headers fall through    |
|  17 | `test_transfer_encoding_case_and_length_bounds` |   ✅   | Transfer encoding case and length bounds |

</details>

---

## test_edge_cache - native_edge_cache_core - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                              | Status | Description                                                |
| --: | :---------------------------------------------------------------- | :----: | :--------------------------------------------------------- |
|   1 | `test_rfc9110_three_spellings_of_one_instant`                     |   ✅   | Rfc9110 three spellings of one instant                     |
|   2 | `test_http_date_anchor_instants`                                  |   ✅   | Http date anchor instants                                  |
|   3 | `test_http_date_refuses_malformed_text`                           |   ✅   | Http date refuses malformed text                           |
|   4 | `test_rfc9110_published_range_examples`                           |   ✅   | Rfc9110 published range examples                           |
|   5 | `test_range_last_pos_and_suffix_clamp_to_the_representation`      |   ✅   | Range last pos and suffix clamp to the representation      |
|   6 | `test_rfc9110_unsatisfiable_ranges`                               |   ✅   | Rfc9110 unsatisfiable ranges                               |
|   7 | `test_unusable_range_headers_fall_back_to_a_full_response`        |   ✅   | Unusable range headers fall back to a full response        |
|   8 | `test_range_overflow_saturates_past_eof`                          |   ✅   | Range overflow saturates past eof                          |
|   9 | `test_field_lookup_is_case_insensitive_and_ows_trimmed`           |   ✅   | Field lookup is case insensitive and ows trimmed           |
|  10 | `test_field_lookup_refuses_rather_than_truncates`                 |   ✅   | Field lookup refuses rather than truncates                 |
|  11 | `test_rfc9111_freshness_lifetime_precedence`                      |   ✅   | Rfc9111 freshness lifetime precedence                      |
|  12 | `test_rfc9111_heuristic_is_a_tenth_of_the_last_modified_interval` |   ✅   | Rfc9111 heuristic is a tenth of the last modified interval |
|  13 | `test_rfc9111_corrected_initial_age`                              |   ✅   | Rfc9111 corrected initial age                              |
|  14 | `test_rfc9111_current_age_and_the_fresh_predicate`                |   ✅   | Rfc9111 current age and the fresh predicate                |
|  15 | `test_cache_key_is_canonical`                                     |   ✅   | Cache key is canonical                                     |
|  16 | `test_key_digest_matches_the_fips_180_4_vector`                   |   ✅   | Key digest matches the fips 180 4 vector                   |
|  17 | `test_rfc9111_vary_secondary_key`                                 |   ✅   | Rfc9111 vary secondary key                                 |
|  18 | `test_store_alloc_and_lookup`                                     |   ✅   | Store alloc and lookup                                     |
|  19 | `test_store_evicts_the_least_recently_used_slot`                  |   ✅   | Store evicts the least recently used slot                  |
|  20 | `test_store_find_resolves_the_vary_variant`                       |   ✅   | Store find resolves the vary variant                       |
|  21 | `test_store_purge_by_key_and_by_path_prefix`                      |   ✅   | Store purge by key and by path prefix                      |
|  22 | `test_sweep_drops_only_unrevalidatable_stale_entries`             |   ✅   | Sweep drops only unrevalidatable stale entries             |
|  23 | `test_rfc9111_storeability`                                       |   ✅   | Rfc9111 storeability                                       |
|  24 | `test_conditional_request_carries_the_stored_validators`          |   ✅   | Conditional request carries the stored validators          |
|  25 | `test_apply_304_refreshes_freshness_and_adopts_validators`        |   ✅   | Apply 304 refreshes freshness and adopts validators        |
|  26 | `test_freshness_falls_back_to_the_default_ttl`                    |   ✅   | Freshness falls back to the default ttl                    |
|  27 | `test_an_expires_in_the_past_stores_as_stale`                     |   ✅   | An expires in the past stores as stale                     |

</details>

---

## test_edge_cache_sd - native_edge_cache_sd - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_serialize_roundtrip_all_fields`                 |   ✅   | Serialize roundtrip all fields                 |
|   2 | `test_serialize_max_body`                             |   ✅   | Serialize max body                             |
|   3 | `test_serialize_too_small_scratch_fails`              |   ✅   | Serialize too small scratch fails              |
|   4 | `test_deserialize_corrupt_fails_closed`               |   ✅   | Deserialize corrupt fails closed               |
|   5 | `test_put_get_roundtrip`                              |   ✅   | Put get roundtrip                              |
|   6 | `test_no_validator_not_spilled`                       |   ✅   | No validator not spilled                       |
|   7 | `test_oversize_body_stays_l1_only`                    |   ✅   | Oversize body stays l1 only                    |
|   8 | `test_spill_on_evict_and_promote`                     |   ✅   | Spill on evict and promote                     |
|   9 | `test_transient_entry_not_spilled`                    |   ✅   | Transient entry not spilled                    |
|  10 | `test_survives_reboot`                                |   ✅   | Survives reboot                                |
|  11 | `test_del`                                            |   ✅   | Del                                            |
|  12 | `test_purge_prefix`                                   |   ✅   | Purge prefix                                   |
|  13 | `test_purge_prefix_multipass`                         |   ✅   | Purge prefix multipass                         |
|  14 | `test_purge_all`                                      |   ✅   | Purge all                                      |
|  15 | `test_shared_dbm_foreign_value_untouched`             |   ✅   | Shared dbm foreign value untouched             |
|  16 | `test_serialize_null_guards_and_every_overflow_point` |   ✅   | Serialize null guards and every overflow point |
|  17 | `test_deserialize_null_guards_and_every_truncation`   |   ✅   | Deserialize null guards and every truncation   |
|  18 | `test_deserialize_rejects_field_longer_than_its_slot` |   ✅   | Deserialize rejects field longer than its slot |
|  19 | `test_deserialize_rejects_oversize_body_length`       |   ✅   | Deserialize rejects oversize body length       |
|  20 | `test_dbm_api_null_guards`                            |   ✅   | Dbm api null guards                            |
|  21 | `test_purge_skips_foreign_and_unreadable_records`     |   ✅   | Purge skips foreign and unreadable records     |
|  22 | `test_purge_prefix_skips_key_without_a_path`          |   ✅   | Purge prefix skips key without a path          |
|  23 | `test_purge_counts_only_the_deletes_that_were_logged` |   ✅   | Purge counts only the deletes that were logged |

</details>

---

## test_edge_mesh - native_edge_mesh - ✅ 28 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                 | Status | Description                                                   |
| --: | :------------------------------------------------------------------- | :----: | :------------------------------------------------------------ |
|   1 | `test_request_roundtrip`                                             |   ✅   | Request roundtrip                                             |
|   2 | `test_request_incomplete_then_complete`                              |   ✅   | Request incomplete then complete                              |
|   3 | `test_request_malformed`                                             |   ✅   | Request malformed                                             |
|   4 | `test_entry_frame_roundtrip`                                         |   ✅   | Entry frame roundtrip                                         |
|   5 | `test_age_propagation`                                               |   ✅   | Age propagation                                               |
|   6 | `test_response_roundtrip`                                            |   ✅   | Response roundtrip                                            |
|   7 | `test_response_malformed`                                            |   ✅   | Response malformed                                            |
|   8 | `test_requester_hit`                                                 |   ✅   | Requester hit                                                 |
|   9 | `test_requester_miss`                                                |   ✅   | Requester miss                                                |
|  10 | `test_requester_open_fail`                                           |   ✅   | Requester open fail                                           |
|  11 | `test_requester_send_fail`                                           |   ✅   | Requester send fail                                           |
|  12 | `test_requester_timeout`                                             |   ✅   | Requester timeout                                             |
|  13 | `test_requester_peer_closed_early`                                   |   ✅   | Requester peer closed early                                   |
|  14 | `test_requester_malformed`                                           |   ✅   | Requester malformed                                           |
|  15 | `test_parse_short_and_bad_prefixes`                                  |   ✅   | Parse short and bad prefixes                                  |
|  16 | `test_build_request_guards`                                          |   ✅   | Build request guards                                          |
|  17 | `test_parse_request_incomplete_at_every_field`                       |   ✅   | Parse request incomplete at every field                       |
|  18 | `test_parse_request_hdrs_too_long_for_destination`                   |   ✅   | Parse request hdrs too long for destination                   |
|  19 | `test_parse_request_null_outputs`                                    |   ✅   | Parse request null outputs                                    |
|  20 | `test_serialize_entry_guards_and_clamps`                             |   ✅   | Serialize entry guards and clamps                             |
|  21 | `test_deserialize_entry_guards`                                      |   ✅   | Deserialize entry guards                                      |
|  22 | `test_build_response_guards`                                         |   ✅   | Build response guards                                         |
|  23 | `test_parse_response_null_outputs`                                   |   ✅   | Parse response null outputs                                   |
|  24 | `test_requester_begin_argument_guards`                               |   ✅   | Requester begin argument guards                               |
|  25 | `test_requester_pump_guards`                                         |   ✅   | Requester pump guards                                         |
|  26 | `test_requester_buffer_full_without_a_frame`                         |   ✅   | Requester buffer full without a frame                         |
|  27 | `test_requester_pump_skips_the_read_when_the_buffer_is_already_full` |   ✅   | Requester pump skips the read when the buffer is already full |
|  28 | `test_requester_end_without_a_connection`                            |   ✅   | Requester end without a connection                            |

</details>

---

## test_endian - native_endian - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the fixed-width serializers (mmgr/endian.h)._

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_rfc1071_normal_order_is_the_big_endian_read`     |   ✅   | Rfc1071 normal order is the big endian read     |
|   2 | `test_rfc1071_swapped_order_is_the_little_endian_read` |   ✅   | Rfc1071 swapped order is the little endian read |
|   3 | `test_rfc4251_uint32_octets`                           |   ✅   | Rfc4251 uint32 octets                           |
|   4 | `test_uint64_octets_in_decreasing_significance`        |   ✅   | Uint64 octets in decreasing significance        |
|   5 | `test_writers_return_their_width`                      |   ✅   | Writers return their width                      |
|   6 | `test_adjacent_fields_do_not_overlap`                  |   ✅   | Adjacent fields do not overlap                  |
|   7 | `test_round_trip_at_every_offset`                      |   ✅   | Round trip at every offset                      |
|   8 | `test_big_endian_is_the_byte_reverse_of_little_endian` |   ✅   | Big endian is the byte reverse of little endian |
|   9 | `test_a_narrow_write_drops_the_bits_above_its_width`   |   ✅   | A narrow write drops the bits above its width   |

</details>

---

## test_endian - native_mmgr_endian - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the fixed-width serializers (mmgr/endian.h)._

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_rfc1071_normal_order_is_the_big_endian_read`     |   ✅   | Rfc1071 normal order is the big endian read     |
|   2 | `test_rfc1071_swapped_order_is_the_little_endian_read` |   ✅   | Rfc1071 swapped order is the little endian read |
|   3 | `test_rfc4251_uint32_octets`                           |   ✅   | Rfc4251 uint32 octets                           |
|   4 | `test_uint64_octets_in_decreasing_significance`        |   ✅   | Uint64 octets in decreasing significance        |
|   5 | `test_writers_return_their_width`                      |   ✅   | Writers return their width                      |
|   6 | `test_adjacent_fields_do_not_overlap`                  |   ✅   | Adjacent fields do not overlap                  |
|   7 | `test_round_trip_at_every_offset`                      |   ✅   | Round trip at every offset                      |
|   8 | `test_big_endian_is_the_byte_reverse_of_little_endian` |   ✅   | Big endian is the byte reverse of little endian |
|   9 | `test_a_narrow_write_drops_the_bits_above_its_width`   |   ✅   | A narrow write drops the bits above its width   |

</details>

---

## test_enip - native_enip - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the EtherNet/IP encapsulation codec (services/fieldbus/enip/enip.h)._

|   # | Test                                     | Status | Description                       |
| --: | :--------------------------------------- | :----: | :-------------------------------- |
|   1 | `test_published_registry_values`         |   ✅   | Published registry values         |
|   2 | `test_register_session_octets`           |   ✅   | Register session octets           |
|   3 | `test_headers_without_command_data`      |   ✅   | Headers without command data      |
|   4 | `test_send_rr_data_common_packet_format` |   ✅   | Send rr data common packet format |
|   5 | `test_header_round_trip`                 |   ✅   | Header round trip                 |
|   6 | `test_parse_refuses_a_truncated_message` |   ✅   | Parse refuses a truncated message |
|   7 | `test_list_identity_item`                |   ✅   | List identity item                |
|   8 | `test_cpf_walk_refuses_a_missing_item`   |   ✅   | Cpf walk refuses a missing item   |
|   9 | `test_builders_refuse_a_short_buffer`    |   ✅   | Builders refuse a short buffer    |

</details>

---

## test_enocean - native_enocean - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the EnOcean ESP3 serial codec (services/radio/enocean/enocean.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_esp3_crc8_published_table`               |   ✅   | Esp3 crc8 published table               |
|   2 | `test_esp3_published_crc_octets`               |   ✅   | Esp3 published crc octets               |
|   3 | `test_esp3_telegram_field_offsets`             |   ✅   | Esp3 telegram field offsets             |
|   4 | `test_esp3_build_parse_round_trip`             |   ✅   | Esp3 build parse round trip             |
|   5 | `test_esp3_parse_waits_for_the_whole_telegram` |   ✅   | Esp3 parse waits for the whole telegram |
|   6 | `test_esp3_resynchronizes_on_a_bad_frame`      |   ✅   | Esp3 resynchronizes on a bad frame      |
|   7 | `test_esp3_build_fails_closed`                 |   ✅   | Esp3 build fails closed                 |
|   8 | `test_erp1_field_layout`                       |   ✅   | Erp1 field layout                       |
|   9 | `test_erp1_round_trip`                         |   ✅   | Erp1 round trip                         |
|  10 | `test_erp1_fails_closed`                       |   ✅   | Erp1 fails closed                       |
|  11 | `test_erp1_inside_an_esp3_packet`              |   ✅   | Erp1 inside an esp3 packet              |

</details>

---

## test_enocean - native_enocean_esp3 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the EnOcean ESP3 serial codec (services/radio/enocean/enocean.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_esp3_crc8_published_table`               |   ✅   | Esp3 crc8 published table               |
|   2 | `test_esp3_published_crc_octets`               |   ✅   | Esp3 published crc octets               |
|   3 | `test_esp3_telegram_field_offsets`             |   ✅   | Esp3 telegram field offsets             |
|   4 | `test_esp3_build_parse_round_trip`             |   ✅   | Esp3 build parse round trip             |
|   5 | `test_esp3_parse_waits_for_the_whole_telegram` |   ✅   | Esp3 parse waits for the whole telegram |
|   6 | `test_esp3_resynchronizes_on_a_bad_frame`      |   ✅   | Esp3 resynchronizes on a bad frame      |
|   7 | `test_esp3_build_fails_closed`                 |   ✅   | Esp3 build fails closed                 |
|   8 | `test_erp1_field_layout`                       |   ✅   | Erp1 field layout                       |
|   9 | `test_erp1_round_trip`                         |   ✅   | Erp1 round trip                         |
|  10 | `test_erp1_fails_closed`                       |   ✅   | Erp1 fails closed                       |
|  11 | `test_erp1_inside_an_esp3_packet`              |   ✅   | Erp1 inside an esp3 packet              |

</details>

---

## test_esp - native_esp - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the ESP packet transform (services/system/esp/esp.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_rfc4303_packet_layout`                          |   ✅   | Rfc4303 packet layout                          |
|   2 | `test_header_fields_round_trip`                       |   ✅   | Header fields round trip                       |
|   3 | `test_every_bit_is_authenticated`                     |   ✅   | Every bit is authenticated                     |
|   4 | `test_a_different_key_or_salt_cannot_open_the_packet` |   ✅   | A different key or salt cannot open the packet |
|   5 | `test_the_iv_selects_the_nonce`                       |   ✅   | The iv selects the nonce                       |
|   6 | `test_header_is_additional_authenticated_data`        |   ✅   | Header is additional authenticated data        |
|   7 | `test_bounds_are_refused`                             |   ✅   | Bounds are refused                             |
|   8 | `test_replay_rejects_sequence_zero`                   |   ✅   | Replay rejects sequence zero                   |
|   9 | `test_replay_rejects_a_duplicate`                     |   ✅   | Replay rejects a duplicate                     |
|  10 | `test_replay_accepts_reordering_inside_the_window`    |   ✅   | Replay accepts reordering inside the window    |
|  11 | `test_replay_window_width`                            |   ✅   | Replay window width                            |
|  12 | `test_replay_window_advances_cleanly`                 |   ✅   | Replay window advances cleanly                 |
|  13 | `test_replay_accepts_a_monotone_stream`               |   ✅   | Replay accepts a monotone stream               |
|  14 | `test_replay_first_packet_may_be_any_sequence`        |   ✅   | Replay first packet may be any sequence        |

</details>

---

## test_esp - native_system_esp - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the ESP packet transform (services/system/esp/esp.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_rfc4303_packet_layout`                          |   ✅   | Rfc4303 packet layout                          |
|   2 | `test_header_fields_round_trip`                       |   ✅   | Header fields round trip                       |
|   3 | `test_every_bit_is_authenticated`                     |   ✅   | Every bit is authenticated                     |
|   4 | `test_a_different_key_or_salt_cannot_open_the_packet` |   ✅   | A different key or salt cannot open the packet |
|   5 | `test_the_iv_selects_the_nonce`                       |   ✅   | The iv selects the nonce                       |
|   6 | `test_header_is_additional_authenticated_data`        |   ✅   | Header is additional authenticated data        |
|   7 | `test_bounds_are_refused`                             |   ✅   | Bounds are refused                             |
|   8 | `test_replay_rejects_sequence_zero`                   |   ✅   | Replay rejects sequence zero                   |
|   9 | `test_replay_rejects_a_duplicate`                     |   ✅   | Replay rejects a duplicate                     |
|  10 | `test_replay_accepts_reordering_inside_the_window`    |   ✅   | Replay accepts reordering inside the window    |
|  11 | `test_replay_window_width`                            |   ✅   | Replay window width                            |
|  12 | `test_replay_window_advances_cleanly`                 |   ✅   | Replay window advances cleanly                 |
|  13 | `test_replay_accepts_a_monotone_stream`               |   ✅   | Replay accepts a monotone stream               |
|  14 | `test_replay_first_packet_may_be_any_sequence`        |   ✅   | Replay first packet may be any sequence        |

</details>

---

## test_espnow - native_espnow - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the ESP-NOW envelope codec and peer registry_

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_envelope_field_layout`                          |   ✅   | Envelope field layout                          |
|   2 | `test_encode_decode_round_trip`                       |   ✅   | Encode decode round trip                       |
|   3 | `test_decode_requires_the_declared_length_exactly`    |   ✅   | Decode requires the declared length exactly    |
|   4 | `test_decode_requires_the_magic_octet`                |   ✅   | Decode requires the magic octet                |
|   5 | `test_decode_fails_closed`                            |   ✅   | Decode fails closed                            |
|   6 | `test_payload_cap_is_the_radio_limit_less_the_header` |   ✅   | Payload cap is the radio limit less the header |
|   7 | `test_encode_fails_closed`                            |   ✅   | Encode fails closed                            |
|   8 | `test_peer_registry_membership`                       |   ✅   | Peer registry membership                       |
|   9 | `test_peer_registry_is_bounded`                       |   ✅   | Peer registry is bounded                       |
|  10 | `test_broadcast_address_is_all_ones`                  |   ✅   | Broadcast address is all ones                  |
|  11 | `test_radio_binding_reports_no_radio`                 |   ✅   | Radio binding reports no radio                 |

</details>

---

## test_espnow - native_espnow_envelope - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the ESP-NOW envelope codec and peer registry_

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_envelope_field_layout`                          |   ✅   | Envelope field layout                          |
|   2 | `test_encode_decode_round_trip`                       |   ✅   | Encode decode round trip                       |
|   3 | `test_decode_requires_the_declared_length_exactly`    |   ✅   | Decode requires the declared length exactly    |
|   4 | `test_decode_requires_the_magic_octet`                |   ✅   | Decode requires the magic octet                |
|   5 | `test_decode_fails_closed`                            |   ✅   | Decode fails closed                            |
|   6 | `test_payload_cap_is_the_radio_limit_less_the_header` |   ✅   | Payload cap is the radio limit less the header |
|   7 | `test_encode_fails_closed`                            |   ✅   | Encode fails closed                            |
|   8 | `test_peer_registry_membership`                       |   ✅   | Peer registry membership                       |
|   9 | `test_peer_registry_is_bounded`                       |   ✅   | Peer registry is bounded                       |
|  10 | `test_broadcast_address_is_all_ones`                  |   ✅   | Broadcast address is all ones                  |
|  11 | `test_radio_binding_reports_no_radio`                 |   ✅   | Radio binding reports no radio                 |

</details>

---

## test_euromap77 - native_euromap77 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_browse_hierarchy`                        |   ✅   | Browse hierarchy                        |
|   2 | `test_namespace_uris`                          |   ✅   | Namespace uris                          |
|   3 | `test_interface_name_comes_from_the_model`     |   ✅   | Interface name comes from the model     |
|   4 | `test_machine_information_values`              |   ✅   | Machine information values              |
|   5 | `test_absent_strings_read_as_empty`            |   ✅   | Absent strings read as empty            |
|   6 | `test_counter_variants_are_uint64`             |   ✅   | Counter variants are uint64             |
|   7 | `test_counter_keeps_full_64_bit_range`         |   ✅   | Counter keeps full 64 bit range         |
|   8 | `test_enumeration_values`                      |   ✅   | Enumeration values                      |
|   9 | `test_reads_follow_the_bound_model`            |   ✅   | Reads follow the bound model            |
|  10 | `test_reads_outside_the_model_are_refused`     |   ✅   | Reads outside the model are refused     |
|  11 | `test_browse_outside_the_model`                |   ✅   | Browse outside the model                |
|  12 | `test_unbound_model_serves_nothing`            |   ✅   | Unbound model serves nothing            |
|  13 | `test_browse_respects_the_caller_bound`        |   ✅   | Browse respects the caller bound        |
|  14 | `test_references_are_forward_and_in_namespace` |   ✅   | References are forward and in namespace |

</details>

---

## test_exc_decoder - native_exc_decoder - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the panic decoder (server/core/exc_decoder.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_espressif_published_panic`                     |   ✅   | Espressif published panic                     |
|   2 | `test_json_of_the_published_panic`                   |   ✅   | Json of the published panic                   |
|   3 | `test_pc_falls_back_to_the_first_frame`              |   ✅   | Pc falls back to the first frame              |
|   4 | `test_absent_fields_are_omitted_from_the_report`     |   ✅   | Absent fields are omitted from the report     |
|   5 | `test_core_number_is_decimal`                        |   ✅   | Core number is decimal                        |
|   6 | `test_hex_literals`                                  |   ✅   | Hex literals                                  |
|   7 | `test_a_frame_is_a_pc_colon_sp_pair`                 |   ✅   | A frame is a pc colon sp pair                 |
|   8 | `test_the_frame_list_stops_at_its_capacity`          |   ✅   | The frame list stops at its capacity          |
|   9 | `test_the_cause_is_bounded`                          |   ✅   | The cause is bounded                          |
|  10 | `test_json_escapes_the_two_characters_a_string_must` |   ✅   | Json escapes the two characters a string must |
|  11 | `test_text_that_is_not_a_dump_is_reported_as_such`   |   ✅   | Text that is not a dump is reported as such   |
|  12 | `test_json_fails_closed_on_a_short_buffer`           |   ✅   | Json fails closed on a short buffer           |
|  13 | `test_null_arguments_are_refused`                    |   ✅   | Null arguments are refused                    |

</details>

---

## test_failsafe - native_failsafe - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the lifeline watchdog (server/core/failsafe.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_overdue_is_a_wrap_safe_unsigned_delta`             |   ✅   | Overdue is a wrap safe unsigned delta             |
|   2 | `test_a_lifeline_starts_fed`                             |   ✅   | A lifeline starts fed                             |
|   3 | `test_a_feed_moves_the_deadline`                         |   ✅   | A feed moves the deadline                         |
|   4 | `test_the_registry_is_bounded`                           |   ✅   | The registry is bounded                           |
|   5 | `test_check_reports_one_bit_per_lifeline`                |   ✅   | Check reports one bit per lifeline                |
|   6 | `test_a_breach_fires_once_per_episode`                   |   ✅   | A breach fires once per episode                   |
|   7 | `test_a_feed_rearms_the_callback`                        |   ✅   | A feed rearms the callback                        |
|   8 | `test_a_feed_names_an_armed_lifeline`                    |   ✅   | A feed names an armed lifeline                    |
|   9 | `test_the_report_is_an_rfc8259_object`                   |   ✅   | The report is an rfc8259 object                   |
|  10 | `test_an_empty_registry_still_reports_an_object`         |   ✅   | An empty registry still reports an object         |
|  11 | `test_the_report_stays_inside_its_buffer`                |   ✅   | The report stays inside its buffer                |
|  12 | `test_the_report_refuses_null_and_zero_capacity`         |   ✅   | The report refuses null and zero capacity         |
|  13 | `test_reset_empties_the_registry_and_drops_the_callback` |   ✅   | Reset empties the registry and drops the callback |

</details>

---

## test_fanuc_j519 - native_fanuc_j519 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_packet_lengths`                           |   ✅   | Packet lengths                           |
|   2 | `test_packet_type_codes`                        |   ✅   | Packet type codes                        |
|   3 | `test_start_and_stop_are_header_only`           |   ✅   | Start and stop are header only           |
|   4 | `test_header_byte_order`                        |   ✅   | Header byte order                        |
|   5 | `test_peek_needs_a_whole_header`                |   ✅   | Peek needs a whole header                |
|   6 | `test_motion_field_offsets`                     |   ✅   | Motion field offsets                     |
|   7 | `test_status_field_offsets`                     |   ✅   | Status field offsets                     |
|   8 | `test_status_bits`                              |   ✅   | Status bits                              |
|   9 | `test_request_and_ack_round_trip`               |   ✅   | Request and ack round trip               |
|  10 | `test_threshold_types`                          |   ✅   | Threshold types                          |
|  11 | `test_io_type_codes`                            |   ✅   | Io type codes                            |
|  12 | `test_data_style`                               |   ✅   | Data style                               |
|  13 | `test_length_separates_the_shared_type_codes`   |   ✅   | Length separates the shared type codes   |
|  14 | `test_parsers_check_the_type_word`              |   ✅   | Parsers check the type word              |
|  15 | `test_builders_refuse_a_short_buffer`           |   ✅   | Builders refuse a short buffer           |
|  16 | `test_parsers_refuse_missing_arguments`         |   ✅   | Parsers refuse missing arguments         |
|  17 | `test_axis_values_survive_the_binary32_packing` |   ✅   | Axis values survive the binary32 packing |

</details>

---

## test_fdc2214 - native_fdc2214 - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TI FDC2114/2214 codec (server/peripherals/fdc2214/fdc2214.h)._

|   # | Test                                                           | Status | Description                                             |
| --: | :------------------------------------------------------------- | :----: | :------------------------------------------------------ |
|   1 | `test_snoscz5_data_register_pair_is_a_28_bit_result`           |   ✅   | Snoscz5 data register pair is a 28 bit result           |
|   2 | `test_data_never_exceeds_twenty_eight_bits`                    |   ✅   | Data never exceeds twenty eight bits                    |
|   3 | `test_snoscz5_status_flags_come_from_the_top_nibble`           |   ✅   | Snoscz5 status flags come from the top nibble           |
|   4 | `test_snoscz5_sensor_frequency_scales_data_over_two_to_the_28` |   ✅   | Snoscz5 sensor frequency scales data over two to the 28 |
|   5 | `test_sensor_frequency_is_monotone_in_the_data`                |   ✅   | Sensor frequency is monotone in the data                |
|   6 | `test_config_sequence_register_order_and_addresses`            |   ✅   | Config sequence register order and addresses            |
|   7 | `test_snoscz5_register_addresses`                              |   ✅   | Snoscz5 register addresses                              |
|   8 | `test_snoscz5_identity_registers`                              |   ✅   | Snoscz5 identity registers                              |
|   9 | `test_config_builder_fails_closed`                             |   ✅   | Config builder fails closed                             |

</details>

---

## test_fins - native_fins - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Omron FINS frame codec (services/fieldbus/fins/fins.h)._

|   # | Test                                  | Status | Description                    |
| --: | :------------------------------------ | :----: | :----------------------------- |
|   1 | `test_published_header_constants`     |   ✅   | Published header constants     |
|   2 | `test_memory_area_read_octets`        |   ✅   | Memory area read octets        |
|   3 | `test_memory_area_write_octets`       |   ✅   | Memory area write octets       |
|   4 | `test_operating_mode_commands`        |   ✅   | Operating mode commands        |
|   5 | `test_header_round_trip`              |   ✅   | Header round trip              |
|   6 | `test_response_end_code`              |   ✅   | Response end code              |
|   7 | `test_parsers_refuse_short_frames`    |   ✅   | Parsers refuse short frames    |
|   8 | `test_builders_refuse_a_short_buffer` |   ✅   | Builders refuse a short buffer |

</details>

---

## test_float_bits - native_float_bits - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the binary64 field reads (mmgr/float_bits.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_ieee754_binary64_field_layout`                |   ✅   | Ieee754 binary64 field layout                |
|   2 | `test_ieee754_published_encodings`                  |   ✅   | Ieee754 published encodings                  |
|   3 | `test_the_two_zeros_differ_only_in_the_sign`        |   ✅   | The two zeros differ only in the sign        |
|   4 | `test_infinity_and_nan_share_the_all_ones_exponent` |   ✅   | Infinity and nan share the all ones exponent |
|   5 | `test_the_subnormal_boundary`                       |   ✅   | The subnormal boundary                       |
|   6 | `test_the_largest_finite_value`                     |   ✅   | The largest finite value                     |
|   7 | `test_merge_masks_each_field`                       |   ✅   | Merge masks each field                       |
|   8 | `test_every_bit_position_survives_the_split`        |   ✅   | Every bit position survives the split        |
|   9 | `test_the_exponent_field_walks_its_whole_range`     |   ✅   | The exponent field walks its whole range     |
|  10 | `test_a_walking_significand_bit_survives`           |   ✅   | A walking significand bit survives           |
|  11 | `test_repeating_significand_patterns_survive`       |   ✅   | Repeating significand patterns survive       |

</details>

---

## test_float_bits - native_mmgr_float_bits - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the binary64 field reads (mmgr/float_bits.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_ieee754_binary64_field_layout`                |   ✅   | Ieee754 binary64 field layout                |
|   2 | `test_ieee754_published_encodings`                  |   ✅   | Ieee754 published encodings                  |
|   3 | `test_the_two_zeros_differ_only_in_the_sign`        |   ✅   | The two zeros differ only in the sign        |
|   4 | `test_infinity_and_nan_share_the_all_ones_exponent` |   ✅   | Infinity and nan share the all ones exponent |
|   5 | `test_the_subnormal_boundary`                       |   ✅   | The subnormal boundary                       |
|   6 | `test_the_largest_finite_value`                     |   ✅   | The largest finite value                     |
|   7 | `test_merge_masks_each_field`                       |   ✅   | Merge masks each field                       |
|   8 | `test_every_bit_position_survives_the_split`        |   ✅   | Every bit position survives the split        |
|   9 | `test_the_exponent_field_walks_its_whole_range`     |   ✅   | The exponent field walks its whole range     |
|  10 | `test_a_walking_significand_bit_survives`           |   ✅   | A walking significand bit survives           |
|  11 | `test_repeating_significand_patterns_survive`       |   ✅   | Repeating significand patterns survive       |

</details>

---

## test_flow_export - native_flow_export - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the flow-record Exporting Process (services/net/flow_export/flow_export.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_v9_packet_header`                               |   ✅   | V9 packet header                               |
|   2 | `test_rfc3954_template_flowset_example`               |   ✅   | Rfc3954 template flowset example               |
|   3 | `test_rfc3954_data_flowset_example`                   |   ✅   | Rfc3954 data flowset example                   |
|   4 | `test_ipfix_message_header_and_template_set_id`       |   ✅   | Ipfix message header and template set id       |
|   5 | `test_data_set_id_must_be_256_or_above`               |   ✅   | Data set id must be 256 or above               |
|   6 | `test_v9_data_set_is_padded_to_a_four_octet_boundary` |   ✅   | V9 data set is padded to a four octet boundary |
|   7 | `test_ipfix_data_set_is_not_padded`                   |   ✅   | Ipfix data set is not padded                   |
|   8 | `test_an_open_set_is_closed_by_what_follows`          |   ✅   | An open set is closed by what follows          |
|   9 | `test_calls_out_of_order_are_refused`                 |   ✅   | Calls out of order are refused                 |
|  10 | `test_overflow_fails_closed`                          |   ✅   | Overflow fails closed                          |
|  11 | `test_v5_header_is_twenty_four_octets`                |   ✅   | V5 header is twenty four octets                |
|  12 | `test_v5_record_is_forty_eight_octets`                |   ✅   | V5 record is forty eight octets                |
|  13 | `test_v5_refuses_a_short_span`                        |   ✅   | V5 refuses a short span                        |

</details>

---

## test_focas - native_focas - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the FANUC FOCAS Ethernet codec (services/machine_tool/focas/focas.h)._

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_open_request_is_byte_exact`                         |   ✅   | Open request is byte exact                         |
|   2 | `test_close_request_is_byte_exact`                        |   ✅   | Close request is byte exact                        |
|   3 | `test_sysinfo_request_is_byte_exact`                      |   ✅   | Sysinfo request is byte exact                      |
|   4 | `test_arguments_are_big_endian_i32`                       |   ✅   | Arguments are big endian i32                       |
|   5 | `test_position_kind_and_axis_are_the_first_two_arguments` |   ✅   | Position kind and axis are the first two arguments |
|   6 | `test_extra_data_extends_the_declared_length`             |   ✅   | Extra data extends the declared length             |
|   7 | `test_builders_refuse_a_short_buffer`                     |   ✅   | Builders refuse a short buffer                     |
|   8 | `test_built_frames_parse_back`                            |   ✅   | Built frames parse back                            |
|   9 | `test_malformed_frames_are_refused`                       |   ✅   | Malformed frames are refused                       |
|  10 | `test_command_response_decodes_selector_status_and_data`  |   ✅   | Command response decodes selector status and data  |
|  11 | `test_status_is_a_signed_return_code`                     |   ✅   | Status is a signed return code                     |
|  12 | `test_response_truncation_and_wrong_type_are_refused`     |   ✅   | Response truncation and wrong type are refused     |
|  13 | `test_sysinfo_splits_the_fixed_width_ascii_fields`        |   ✅   | Sysinfo splits the fixed width ascii fields        |
|  14 | `test_value8_scales_by_base_and_exponent`                 |   ✅   | Value8 scales by base and exponent                 |
|  15 | `test_value8_sentinel_and_unknown_base_are_invalid`       |   ✅   | Value8 sentinel and unknown base are invalid       |

</details>

---

## test_forwarded_trust - native_forwarded_trust - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the trusted-reverse-proxy client resolver_

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_an_empty_table_trusts_nothing`                       |   ✅   | An empty table trusts nothing                       |
|   2 | `test_a_cidr_covers_its_block_and_nothing_else`            |   ✅   | A cidr covers its block and nothing else            |
|   3 | `test_a_bare_address_is_a_host_route`                      |   ✅   | A bare address is a host route                      |
|   4 | `test_a_v6_cidr_covers_only_v6`                            |   ✅   | A v6 cidr covers only v6                            |
|   5 | `test_a_zero_prefix_covers_the_family`                     |   ✅   | A zero prefix covers the family                     |
|   6 | `test_malformed_cidr_text_is_refused`                      |   ✅   | Malformed cidr text is refused                      |
|   7 | `test_the_prefix_width_bound_per_family`                   |   ✅   | The prefix width bound per family                   |
|   8 | `test_the_table_is_bounded`                                |   ✅   | The table is bounded                                |
|   9 | `test_reset_empties_the_table`                             |   ✅   | Reset empties the table                             |
|  10 | `test_an_untrusted_peer_can_never_forge_a_client_address`  |   ✅   | An untrusted peer can never forge a client address  |
|  11 | `test_a_trusted_upstream_is_believed`                      |   ✅   | A trusted upstream is believed                      |
|  12 | `test_a_trusted_upstream_with_no_usable_client_falls_back` |   ✅   | A trusted upstream with no usable client falls back |
|  13 | `test_the_destination_is_always_written`                   |   ✅   | The destination is always written                   |
|  14 | `test_several_upstreams_are_all_trusted`                   |   ✅   | Several upstreams are all trusted                   |
|  15 | `test_a_null_peer_is_not_trusted`                          |   ✅   | A null peer is not trusted                          |

</details>

---

## test_frame - native_frame - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the declarative frame builder (mmgr/protoframe.h)._

|   # | Test                                                         | Status | Description                                           |
| --: | :----------------------------------------------------------- | :----: | :---------------------------------------------------- |
|   1 | `test_a_frame_interleaves_its_literals_and_values`           |   ✅   | A frame interleaves its literals and values           |
|   2 | `test_a_literal_only_frame_takes_no_arguments`               |   ✅   | A literal only frame takes no arguments               |
|   3 | `test_an_empty_spec_yields_an_empty_string`                  |   ✅   | An empty spec yields an empty string                  |
|   4 | `test_a_null_string_argument_renders_as_empty`               |   ✅   | A null string argument renders as empty               |
|   5 | `test_every_field_kind_renders_its_conversion`               |   ✅   | Every field kind renders its conversion               |
|   6 | `test_a_width_pads_but_never_truncates`                      |   ✅   | A width pads but never truncates                      |
|   7 | `test_the_float_kinds_follow_the_printf_style_rule`          |   ✅   | The float kinds follow the printf style rule          |
|   8 | `test_a_fixed_field_above_the_64_bit_range_falls_back`       |   ✅   | A fixed field above the 64 bit range falls back       |
|   9 | `test_a_frame_that_does_not_fit_writes_an_empty_string`      |   ✅   | A frame that does not fit writes an empty string      |
|  10 | `test_the_capacity_boundary_is_exact`                        |   ✅   | The capacity boundary is exact                        |
|  11 | `test_null_arguments_are_refused`                            |   ✅   | Null arguments are refused                            |
|  12 | `test_a_zero_capacity_buffer_is_never_written`               |   ✅   | A zero capacity buffer is never written               |
|  13 | `test_a_value_whose_kind_disagrees_with_the_spec_is_refused` |   ✅   | A value whose kind disagrees with the spec is refused |
|  14 | `test_the_argument_count_must_match_the_spec`                |   ✅   | The argument count must match the spec                |
|  15 | `test_an_unknown_opcode_is_refused`                          |   ✅   | An unknown opcode is refused                          |
|  16 | `test_append_accumulates_onto_the_existing_contents`         |   ✅   | Append accumulates onto the existing contents         |
|  17 | `test_append_rewinds_the_whole_frame_on_overflow`            |   ✅   | Append rewinds the whole frame on overflow            |
|  18 | `test_append_to_a_full_buffer_changes_nothing`               |   ✅   | Append to a full buffer changes nothing               |

</details>

---

## test_frame - native_mmgr_frame - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the declarative frame builder (mmgr/protoframe.h)._

|   # | Test                                                         | Status | Description                                           |
| --: | :----------------------------------------------------------- | :----: | :---------------------------------------------------- |
|   1 | `test_a_frame_interleaves_its_literals_and_values`           |   ✅   | A frame interleaves its literals and values           |
|   2 | `test_a_literal_only_frame_takes_no_arguments`               |   ✅   | A literal only frame takes no arguments               |
|   3 | `test_an_empty_spec_yields_an_empty_string`                  |   ✅   | An empty spec yields an empty string                  |
|   4 | `test_a_null_string_argument_renders_as_empty`               |   ✅   | A null string argument renders as empty               |
|   5 | `test_every_field_kind_renders_its_conversion`               |   ✅   | Every field kind renders its conversion               |
|   6 | `test_a_width_pads_but_never_truncates`                      |   ✅   | A width pads but never truncates                      |
|   7 | `test_the_float_kinds_follow_the_printf_style_rule`          |   ✅   | The float kinds follow the printf style rule          |
|   8 | `test_a_fixed_field_above_the_64_bit_range_falls_back`       |   ✅   | A fixed field above the 64 bit range falls back       |
|   9 | `test_a_frame_that_does_not_fit_writes_an_empty_string`      |   ✅   | A frame that does not fit writes an empty string      |
|  10 | `test_the_capacity_boundary_is_exact`                        |   ✅   | The capacity boundary is exact                        |
|  11 | `test_null_arguments_are_refused`                            |   ✅   | Null arguments are refused                            |
|  12 | `test_a_zero_capacity_buffer_is_never_written`               |   ✅   | A zero capacity buffer is never written               |
|  13 | `test_a_value_whose_kind_disagrees_with_the_spec_is_refused` |   ✅   | A value whose kind disagrees with the spec is refused |
|  14 | `test_the_argument_count_must_match_the_spec`                |   ✅   | The argument count must match the spec                |
|  15 | `test_an_unknown_opcode_is_refused`                          |   ✅   | An unknown opcode is refused                          |
|  16 | `test_append_accumulates_onto_the_existing_contents`         |   ✅   | Append accumulates onto the existing contents         |
|  17 | `test_append_rewinds_the_whole_frame_on_overflow`            |   ✅   | Append rewinds the whole frame on overflow            |
|  18 | `test_append_to_a_full_buffer_changes_nothing`               |   ✅   | Append to a full buffer changes nothing               |

</details>

---

## test_ftp - native_ftp - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the FTP client wire codec (services/file_transfer/ftp/ftp.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_rfc959_multiline_reply_example`                 |   ✅   | Rfc959 multiline reply example                 |
|   2 | `test_partial_multiline_reply_needs_more`             |   ✅   | Partial multiline reply needs more             |
|   3 | `test_single_line_reply_and_pipelining`               |   ✅   | Single line reply and pipelining               |
|   4 | `test_malformed_reply_heads_are_refused`              |   ✅   | Malformed reply heads are refused              |
|   5 | `test_a_different_code_does_not_terminate`            |   ✅   | A different code does not terminate            |
|   6 | `test_rfc2428_published_eprt_examples`                |   ✅   | Rfc2428 published eprt examples                |
|   7 | `test_port_command_splits_into_eight_bit_fields`      |   ✅   | Port command splits into eight bit fields      |
|   8 | `test_pasv_tuple_decodes_to_the_same_pair`            |   ✅   | Pasv tuple decodes to the same pair            |
|   9 | `test_pasv_refuses_out_of_range_and_malformed_tuples` |   ✅   | Pasv refuses out of range and malformed tuples |
|  10 | `test_rfc2428_published_epsv_example`                 |   ✅   | Rfc2428 published epsv example                 |
|  11 | `test_epsv_accepts_any_legal_delimiter`               |   ✅   | Epsv accepts any legal delimiter               |
|  12 | `test_epsv_refuses_malformed_replies`                 |   ✅   | Epsv refuses malformed replies                 |
|  13 | `test_command_line_form`                              |   ✅   | Command line form                              |
|  14 | `test_reply_class_follows_the_first_digit`            |   ✅   | Reply class follows the first digit            |
|  15 | `test_builders_refuse_a_short_buffer`                 |   ✅   | Builders refuse a short buffer                 |
|  16 | `test_builders_refuse_bad_arguments`                  |   ✅   | Builders refuse bad arguments                  |
|  17 | `test_parsers_refuse_null_arguments`                  |   ✅   | Parsers refuse null arguments                  |

</details>

---

## test_gnss_survey - native_gnss_survey - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the GNSS survey-in core (services/timing_position/gnss/gnss_survey.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_wgs84_published_axes`                     |   ✅   | Wgs84 published axes                     |
|   2 | `test_height_adds_along_the_normal_at_the_axes` |   ✅   | Height adds along the normal at the axes |
|   3 | `test_longitude_only_rotates_about_the_axis`    |   ✅   | Longitude only rotates about the axis    |
|   4 | `test_geodetic_ecef_round_trip`                 |   ✅   | Geodetic ecef round trip                 |
|   5 | `test_metres_to_tenth_millimetres`              |   ✅   | Metres to tenth millimetres              |
|   6 | `test_survey_starts_empty`                      |   ✅   | Survey starts empty                      |
|   7 | `test_survey_mean_and_spread`                   |   ✅   | Survey mean and spread                   |
|   8 | `test_survey_averages_a_symmetric_scatter`      |   ✅   | Survey averages a symmetric scatter      |
|   9 | `test_survey_completion_gate`                   |   ✅   | Survey completion gate                   |
|  10 | `test_survey_accepts_geodetic_fixes`            |   ✅   | Survey accepts geodetic fixes            |
|  11 | `test_gga_folds_into_a_geodetic_fix`            |   ✅   | Gga folds into a geodetic fix            |
|  12 | `test_gga_without_a_fix_is_refused`             |   ✅   | Gga without a fix is refused             |

</details>

---

## test_gnss_survey - native_gnss_survey_in - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the GNSS survey-in core (services/timing_position/gnss/gnss_survey.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_wgs84_published_axes`                     |   ✅   | Wgs84 published axes                     |
|   2 | `test_height_adds_along_the_normal_at_the_axes` |   ✅   | Height adds along the normal at the axes |
|   3 | `test_longitude_only_rotates_about_the_axis`    |   ✅   | Longitude only rotates about the axis    |
|   4 | `test_geodetic_ecef_round_trip`                 |   ✅   | Geodetic ecef round trip                 |
|   5 | `test_metres_to_tenth_millimetres`              |   ✅   | Metres to tenth millimetres              |
|   6 | `test_survey_starts_empty`                      |   ✅   | Survey starts empty                      |
|   7 | `test_survey_mean_and_spread`                   |   ✅   | Survey mean and spread                   |
|   8 | `test_survey_averages_a_symmetric_scatter`      |   ✅   | Survey averages a symmetric scatter      |
|   9 | `test_survey_completion_gate`                   |   ✅   | Survey completion gate                   |
|  10 | `test_survey_accepts_geodetic_fixes`            |   ✅   | Survey accepts geodetic fixes            |
|  11 | `test_gga_folds_into_a_geodetic_fix`            |   ✅   | Gga folds into a geodetic fix            |
|  12 | `test_gga_without_a_fix_is_refused`             |   ✅   | Gga without a fix is refused             |

</details>

---

## test_goose - native_goose - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEC 61850 GOOSE publisher / subscriber codec (services/energy/goose/goose.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_pdu_is_ber_encoded_in_tag_order`                      |   ✅   | Pdu is ber encoded in tag order                      |
|   2 | `test_boolean_true_is_all_ones`                             |   ✅   | Boolean true is all ones                             |
|   3 | `test_integer_contents_are_minimal_and_positive`            |   ✅   | Integer contents are minimal and positive            |
|   4 | `test_long_form_length_boundary`                            |   ✅   | Long form length boundary                            |
|   5 | `test_ethernet_and_goose_header_layout`                     |   ✅   | Ethernet and goose header layout                     |
|   6 | `test_publish_subscribe_round_trip`                         |   ✅   | Publish subscribe round trip                         |
|   7 | `test_unknown_pdu_tags_are_skipped`                         |   ✅   | Unknown pdu tags are skipped                         |
|   8 | `test_parse_rejects_a_frame_that_is_not_a_valid_goose_apdu` |   ✅   | Parse rejects a frame that is not a valid goose apdu |
|   9 | `test_build_refuses_an_undersized_buffer`                   |   ✅   | Build refuses an undersized buffer                   |
|  10 | `test_absent_optional_fields_encode_as_empty`               |   ✅   | Absent optional fields encode as empty               |

</details>

---

## test_gpib - native_gpib - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the GPIB-over-LAN controller codec (services/instrumentation/gpib/gpib.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_prologix_published_escape_example`      |   ✅   | Prologix published escape example      |
|   2 | `test_only_the_four_named_octets_are_escaped` |   ✅   | Only the four named octets are escaped |
|   3 | `test_empty_payload_is_a_bare_terminator`     |   ✅   | Empty payload is a bare terminator     |
|   4 | `test_addr_command_matches_the_manual`        |   ✅   | Addr command matches the manual        |
|   5 | `test_read_command_matches_the_manual`        |   ✅   | Read command matches the manual        |
|   6 | `test_spoll_command_matches_the_manual`       |   ✅   | Spoll command matches the manual       |
|   7 | `test_eos_command_matches_the_manual`         |   ✅   | Eos command matches the manual         |
|   8 | `test_generic_command_form`                   |   ✅   | Generic command form                   |
|   9 | `test_command_versus_data_classification`     |   ✅   | Command versus data classification     |
|  10 | `test_decimal_response_parsing`               |   ✅   | Decimal response parsing               |
|  11 | `test_addr_response_parsing`                  |   ✅   | Addr response parsing                  |
|  12 | `test_version_response_parsing`               |   ✅   | Version response parsing               |
|  13 | `test_published_ports`                        |   ✅   | Published ports                        |
|  14 | `test_builders_refuse_a_short_buffer`         |   ✅   | Builders refuse a short buffer         |
|  15 | `test_data_builder_reserves_the_terminator`   |   ✅   | Data builder reserves the terminator   |

</details>

---

## test_gpio_map - native_gpio_map - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_the_serialized_document`                  |   ✅   | The serialized document                  |
|   2 | `test_an_empty_table_renders_an_empty_array`    |   ✅   | An empty table renders an empty array    |
|   3 | `test_the_element_separator`                    |   ✅   | The element separator                    |
|   4 | `test_the_level_is_normalized`                  |   ✅   | The level is normalized                  |
|   5 | `test_a_label_cannot_close_its_string`          |   ✅   | A label cannot close its string          |
|   6 | `test_a_short_capacity_yields_no_document`      |   ✅   | A short capacity yields no document      |
|   7 | `test_the_serializer_refuses_missing_arguments` |   ✅   | The serializer refuses missing arguments |
|   8 | `test_direction_names`                          |   ✅   | Direction names                          |
|   9 | `test_only_a_declared_output_may_be_driven`     |   ✅   | Only a declared output may be driven     |
|  10 | `test_a_set_request_parses_both_fields`         |   ✅   | A set request parses both fields         |
|  11 | `test_the_parsed_level_is_a_flag`               |   ✅   | The parsed level is a flag               |
|  12 | `test_an_incomplete_set_request_is_refused`     |   ✅   | An incomplete set request is refused     |
|  13 | `test_the_body_length_bounds_the_parse`         |   ✅   | The body length bounds the parse         |
|  14 | `test_arming_and_sampling_the_table`            |   ✅   | Arming and sampling the table            |
|  15 | `test_a_write_drives_a_flag`                    |   ✅   | A write drives a flag                    |

</details>

---

## test_graphql - native_graphql - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the GraphQL executor (services/iot/graphql/graphql.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_spec_example_3_produces_example_4`                |   ✅   | Spec example 3 produces example 4                |
|   2 | `test_query_shorthand_matches_the_long_form`            |   ✅   | Query shorthand matches the long form            |
|   3 | `test_selection_set_keeps_document_order`               |   ✅   | Selection set keeps document order               |
|   4 | `test_nested_selection_sets_shape_the_response`         |   ✅   | Nested selection sets shape the response         |
|   5 | `test_arguments_reach_the_leaf_that_needs_them`         |   ✅   | Arguments reach the leaf that needs them         |
|   6 | `test_arguments_are_unordered`                          |   ✅   | Arguments are unordered                          |
|   7 | `test_argument_accessors_are_named_and_typed`           |   ✅   | Argument accessors are named and typed           |
|   8 | `test_no_resolver_completes_every_leaf_as_null`         |   ✅   | No resolver completes every leaf as null         |
|   9 | `test_scalar_serialization_forms`                       |   ✅   | Scalar serialization forms                       |
|  10 | `test_string_values_are_json_escaped`                   |   ✅   | String values are json escaped                   |
|  11 | `test_request_error_carries_errors_and_no_data`         |   ✅   | Request error carries errors and no data         |
|  12 | `test_out_of_scope_grammar_is_a_request_error`          |   ✅   | Out of scope grammar is a request error          |
|  13 | `test_comments_and_commas_are_ignored`                  |   ✅   | Comments and commas are ignored                  |
|  14 | `test_unresolved_leaf_completes_as_null`                |   ✅   | Unresolved leaf completes as null                |
|  15 | `test_bounds_are_request_errors`                        |   ✅   | Bounds are request errors                        |
|  16 | `test_short_buffer_reports_overflow`                    |   ✅   | Short buffer reports overflow                    |
|  17 | `test_null_inputs_are_refused`                          |   ✅   | Null inputs are refused                          |
|  18 | `test_argument_accessor_without_values_reports_absence` |   ✅   | Argument accessor without values reports absence |

</details>

---

## test_graphql - native_graphql_exec - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the GraphQL executor (services/iot/graphql/graphql.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_spec_example_3_produces_example_4`                |   ✅   | Spec example 3 produces example 4                |
|   2 | `test_query_shorthand_matches_the_long_form`            |   ✅   | Query shorthand matches the long form            |
|   3 | `test_selection_set_keeps_document_order`               |   ✅   | Selection set keeps document order               |
|   4 | `test_nested_selection_sets_shape_the_response`         |   ✅   | Nested selection sets shape the response         |
|   5 | `test_arguments_reach_the_leaf_that_needs_them`         |   ✅   | Arguments reach the leaf that needs them         |
|   6 | `test_arguments_are_unordered`                          |   ✅   | Arguments are unordered                          |
|   7 | `test_argument_accessors_are_named_and_typed`           |   ✅   | Argument accessors are named and typed           |
|   8 | `test_no_resolver_completes_every_leaf_as_null`         |   ✅   | No resolver completes every leaf as null         |
|   9 | `test_scalar_serialization_forms`                       |   ✅   | Scalar serialization forms                       |
|  10 | `test_string_values_are_json_escaped`                   |   ✅   | String values are json escaped                   |
|  11 | `test_request_error_carries_errors_and_no_data`         |   ✅   | Request error carries errors and no data         |
|  12 | `test_out_of_scope_grammar_is_a_request_error`          |   ✅   | Out of scope grammar is a request error          |
|  13 | `test_comments_and_commas_are_ignored`                  |   ✅   | Comments and commas are ignored                  |
|  14 | `test_unresolved_leaf_completes_as_null`                |   ✅   | Unresolved leaf completes as null                |
|  15 | `test_bounds_are_request_errors`                        |   ✅   | Bounds are request errors                        |
|  16 | `test_short_buffer_reports_overflow`                    |   ✅   | Short buffer reports overflow                    |
|  17 | `test_null_inputs_are_refused`                          |   ✅   | Null inputs are refused                          |
|  18 | `test_argument_accessor_without_values_reports_absence` |   ✅   | Argument accessor without values reports absence |

</details>

---

## test_grpcweb - native_grpcweb - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the gRPC-Web framing codec (services/iot/grpcweb/grpcweb.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_message_length_is_four_octets_big_endian`      |   ✅   | Message length is four octets big endian      |
|   2 | `test_compressed_flag_is_bit_zero_of_the_frame_byte` |   ✅   | Compressed flag is bit zero of the frame byte |
|   3 | `test_msb_of_the_frame_byte_is_the_trailers_bit`     |   ✅   | Msb of the frame byte is the trailers bit     |
|   4 | `test_trailer_section_is_lower_case_field_lines`     |   ✅   | Trailer section is lower case field lines     |
|   5 | `test_status_round_trips_the_published_code_values`  |   ✅   | Status round trips the published code values  |
|   6 | `test_message_slice_stays_percent_encoded`           |   ✅   | Message slice stays percent encoded           |
|   7 | `test_a_key_inside_a_value_is_not_a_field_name`      |   ✅   | A key inside a value is not a field name      |
|   8 | `test_empty_message_is_a_bare_prefix`                |   ✅   | Empty message is a bare prefix                |
|   9 | `test_parse_waits_for_the_whole_message`             |   ✅   | Parse waits for the whole message             |
|  10 | `test_a_stream_walks_frame_by_frame`                 |   ✅   | A stream walks frame by frame                 |
|  11 | `test_frame_writes_the_given_frame_byte`             |   ✅   | Frame writes the given frame byte             |
|  12 | `test_builders_refuse_a_short_buffer`                |   ✅   | Builders refuse a short buffer                |
|  13 | `test_a_negative_status_is_refused`                  |   ✅   | A negative status is refused                  |

</details>

---

## test_grpcweb - native_grpcweb_frame - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the gRPC-Web framing codec (services/iot/grpcweb/grpcweb.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_message_length_is_four_octets_big_endian`      |   ✅   | Message length is four octets big endian      |
|   2 | `test_compressed_flag_is_bit_zero_of_the_frame_byte` |   ✅   | Compressed flag is bit zero of the frame byte |
|   3 | `test_msb_of_the_frame_byte_is_the_trailers_bit`     |   ✅   | Msb of the frame byte is the trailers bit     |
|   4 | `test_trailer_section_is_lower_case_field_lines`     |   ✅   | Trailer section is lower case field lines     |
|   5 | `test_status_round_trips_the_published_code_values`  |   ✅   | Status round trips the published code values  |
|   6 | `test_message_slice_stays_percent_encoded`           |   ✅   | Message slice stays percent encoded           |
|   7 | `test_a_key_inside_a_value_is_not_a_field_name`      |   ✅   | A key inside a value is not a field name      |
|   8 | `test_empty_message_is_a_bare_prefix`                |   ✅   | Empty message is a bare prefix                |
|   9 | `test_parse_waits_for_the_whole_message`             |   ✅   | Parse waits for the whole message             |
|  10 | `test_a_stream_walks_frame_by_frame`                 |   ✅   | A stream walks frame by frame                 |
|  11 | `test_frame_writes_the_given_frame_byte`             |   ✅   | Frame writes the given frame byte             |
|  12 | `test_builders_refuse_a_short_buffer`                |   ✅   | Builders refuse a short buffer                |
|  13 | `test_a_negative_status_is_refused`                  |   ✅   | A negative status is refused                  |

</details>

---

## test_guardrails - native_guardrails - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the heap/stack guardrails (server/core/guardrails/guardrails.h)._

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_the_floor_is_strictly_below`                     |   ✅   | The floor is strictly below                     |
|   2 | `test_each_floor_reads_one_field`                      |   ✅   | Each floor reads one field                      |
|   3 | `test_the_low_water_mark_is_not_a_guardrail`           |   ✅   | The low water mark is not a guardrail           |
|   4 | `test_the_breach_bits_are_disjoint_powers_of_two`      |   ✅   | The breach bits are disjoint powers of two      |
|   5 | `test_a_null_snapshot_reports_no_breach`               |   ✅   | A null snapshot reports no breach               |
|   6 | `test_json_is_an_rfc8259_object`                       |   ✅   | Json is an rfc8259 object                       |
|   7 | `test_json_writes_zero_as_one_digit`                   |   ✅   | Json writes zero as one digit                   |
|   8 | `test_json_writes_the_full_32_bit_range`               |   ✅   | Json writes the full 32 bit range               |
|   9 | `test_json_boundary_is_the_object_plus_its_terminator` |   ✅   | Json boundary is the object plus its terminator |
|  10 | `test_json_refuses_null_and_zero_capacity`             |   ✅   | Json refuses null and zero capacity             |
|  11 | `test_the_host_sampler_reports_no_counters`            |   ✅   | The host sampler reports no counters            |

</details>

---

## test_h2_conn - native_h2conn - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_init_and_request`                                 |   ✅   | Init and request                                 |
|   2 | `test_respond_roundtrip`                                |   ✅   | Respond roundtrip                                |
|   3 | `test_ping_and_split_recv`                              |   ✅   | Ping and split recv                              |
|   4 | `test_bad_preface`                                      |   ✅   | Bad preface                                      |
|   5 | `test_h2_headers_padded_priority`                       |   ✅   | H2 headers padded priority                       |
|   6 | `test_h2_headers_pad_overflow`                          |   ✅   | H2 headers pad overflow                          |
|   7 | `test_h2_stream_id_must_increase`                       |   ✅   | H2 stream id must increase                       |
|   8 | `test_h2_headers_rfc7541_c31_block`                     |   ✅   | H2 headers rfc7541 c31 block                     |
|   9 | `test_h2_trailers_on_open_stream`                       |   ✅   | H2 trailers on open stream                       |
|  10 | `test_h2_trailers_without_end_stream_reset_the_stream`  |   ✅   | H2 trailers without end stream reset the stream  |
|  11 | `test_h2_trailers_reject_pseudo_headers`                |   ✅   | H2 trailers reject pseudo headers                |
|  12 | `test_h2_headers_on_ended_stream_is_a_connection_error` |   ✅   | H2 headers on ended stream is a connection error |
|  13 | `test_h2_headers_bad_stream_id`                         |   ✅   | H2 headers bad stream id                         |
|  14 | `test_h2_stream_table_full_rst`                         |   ✅   | H2 stream table full rst                         |
|  15 | `test_h2_continuation`                                  |   ✅   | H2 continuation                                  |
|  16 | `test_h2_continuation_guards`                           |   ✅   | H2 continuation guards                           |

</details>

---

## test_h2_frame - native_h2frame - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for HTTP/2 binary framing_

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_rfc9113_preface_octets`              |   ✅   | Rfc9113 preface octets              |
|   2 | `test_rfc9113_frame_header_layout`         |   ✅   | Rfc9113 frame header layout         |
|   3 | `test_rfc9113_reserved_bit`                |   ✅   | Rfc9113 reserved bit                |
|   4 | `test_rfc9113_length_is_24_bits`           |   ✅   | Rfc9113 length is 24 bits           |
|   5 | `test_rfc9113_settings_initial_values`     |   ✅   | Rfc9113 settings initial values     |
|   6 | `test_rfc9113_settings_payload_shape`      |   ✅   | Rfc9113 settings payload shape      |
|   7 | `test_rfc9113_settings_bounds`             |   ✅   | Rfc9113 settings bounds             |
|   8 | `test_rfc9113_settings_round_trip`         |   ✅   | Rfc9113 settings round trip         |
|   9 | `test_rfc9113_settings_ack_bytes`          |   ✅   | Rfc9113 settings ack bytes          |
|  10 | `test_rfc9113_window_update_bytes`         |   ✅   | Rfc9113 window update bytes         |
|  11 | `test_rfc9113_rst_stream_bytes`            |   ✅   | Rfc9113 rst stream bytes            |
|  12 | `test_rfc9113_goaway_bytes`                |   ✅   | Rfc9113 goaway bytes                |
|  13 | `test_rfc9113_ping_ack_echoes_the_payload` |   ✅   | Rfc9113 ping ack echoes the payload |
|  14 | `test_rfc9113_headers_and_data`            |   ✅   | Rfc9113 headers and data            |
|  15 | `test_builders_refuse_a_short_destination` |   ✅   | Builders refuse a short destination |
|  16 | `test_rfc9113_registry_values`             |   ✅   | Rfc9113 registry values             |

</details>

---

## test_h2_frame - native_h2_frame_rfc - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for HTTP/2 binary framing_

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_rfc9113_preface_octets`              |   ✅   | Rfc9113 preface octets              |
|   2 | `test_rfc9113_frame_header_layout`         |   ✅   | Rfc9113 frame header layout         |
|   3 | `test_rfc9113_reserved_bit`                |   ✅   | Rfc9113 reserved bit                |
|   4 | `test_rfc9113_length_is_24_bits`           |   ✅   | Rfc9113 length is 24 bits           |
|   5 | `test_rfc9113_settings_initial_values`     |   ✅   | Rfc9113 settings initial values     |
|   6 | `test_rfc9113_settings_payload_shape`      |   ✅   | Rfc9113 settings payload shape      |
|   7 | `test_rfc9113_settings_bounds`             |   ✅   | Rfc9113 settings bounds             |
|   8 | `test_rfc9113_settings_round_trip`         |   ✅   | Rfc9113 settings round trip         |
|   9 | `test_rfc9113_settings_ack_bytes`          |   ✅   | Rfc9113 settings ack bytes          |
|  10 | `test_rfc9113_window_update_bytes`         |   ✅   | Rfc9113 window update bytes         |
|  11 | `test_rfc9113_rst_stream_bytes`            |   ✅   | Rfc9113 rst stream bytes            |
|  12 | `test_rfc9113_goaway_bytes`                |   ✅   | Rfc9113 goaway bytes                |
|  13 | `test_rfc9113_ping_ack_echoes_the_payload` |   ✅   | Rfc9113 ping ack echoes the payload |
|  14 | `test_rfc9113_headers_and_data`            |   ✅   | Rfc9113 headers and data            |
|  15 | `test_builders_refuse_a_short_destination` |   ✅   | Builders refuse a short destination |
|  16 | `test_rfc9113_registry_values`             |   ✅   | Rfc9113 registry values             |

</details>

---

## test_h3_conn - native_h3_conn - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_request_dispatch_and_response`                    |   ✅   | Request dispatch and response                    |
|   2 | `test_h3_pseudo_header_name_variants`                   |   ✅   | H3 pseudo header name variants                   |
|   3 | `test_h3_request_unknown_frame_and_empty_data`          |   ✅   | H3 request unknown frame and empty data          |
|   4 | `test_h3_control_only_frames_on_a_request_stream`       |   ✅   | H3 control only frames on a request stream       |
|   5 | `test_h3_error_before_app_keys_falls_back_to_transport` |   ✅   | H3 error before app keys falls back to transport |
|   6 | `test_h3_data_before_headers`                           |   ✅   | H3 data before headers                           |
|   7 | `test_h3_second_control_stream`                         |   ✅   | H3 second control stream                         |
|   8 | `test_h3_second_settings_frame`                         |   ✅   | H3 second settings frame                         |
|   9 | `test_h3_no_request_callback`                           |   ✅   | H3 no request callback                           |
|  10 | `test_h3_stream_buffer_overflow_clamped`                |   ✅   | H3 stream buffer overflow clamped                |
|  11 | `test_h3_control_stream_frame_guards`                   |   ✅   | H3 control stream frame guards                   |
|  12 | `test_h3_uni_stream_empty_and_repeat_delivery`          |   ✅   | H3 uni stream empty and repeat delivery          |
|  13 | `test_h3_respond_no_content_type_empty_body`            |   ✅   | H3 respond no content type empty body            |
|  14 | `test_post_with_body`                                   |   ✅   | Post with body                                   |
|  15 | `test_control_stream_settings_sent`                     |   ✅   | Control stream settings sent                     |
|  16 | `test_client_control_stream_settings`                   |   ✅   | Client control stream settings                   |
|  17 | `test_client_uni_stream_types`                          |   ✅   | Client uni stream types                          |
|  18 | `test_handshake_done_idempotent`                        |   ✅   | Handshake done idempotent                        |
|  19 | `test_malformed_request_frame`                          |   ✅   | Malformed request frame                          |
|  20 | `test_respond_body_too_large`                           |   ✅   | Respond body too large                           |
|  21 | `test_stream_pool_full`                                 |   ✅   | Stream pool full                                 |
|  22 | `test_uni_stream_partial_type`                          |   ✅   | Uni stream partial type                          |
|  23 | `test_overlong_field_truncated`                         |   ✅   | Overlong field truncated                         |

</details>

---

## test_h3_frame - native_h3frame - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for HTTP/3 framing (network_drivers/presentation/http/http3/h3_frame.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_rfc9000_sample_varints_as_frame_lengths`          |   ✅   | Rfc9000 sample varints as frame lengths          |
|   2 | `test_rfc9000_long_spelling_decodes_but_is_not_emitted` |   ✅   | Rfc9000 long spelling decodes but is not emitted |
|   3 | `test_rfc9114_frame_type_registry`                      |   ✅   | Rfc9114 frame type registry                      |
|   4 | `test_rfc9114_reserved_http2_frame_types`               |   ✅   | Rfc9114 reserved http2 frame types               |
|   5 | `test_rfc9114_settings_defaults`                        |   ✅   | Rfc9114 settings defaults                        |
|   6 | `test_rfc9114_settings_round_trip`                      |   ✅   | Rfc9114 settings round trip                      |
|   7 | `test_rfc9114_reserved_settings_identifiers`            |   ✅   | Rfc9114 reserved settings identifiers            |
|   8 | `test_rfc9114_settings_truncated_pair`                  |   ✅   | Rfc9114 settings truncated pair                  |
|   9 | `test_rfc9114_data_and_headers_builders`                |   ✅   | Rfc9114 data and headers builders                |
|  10 | `test_rfc9114_goaway_builder`                           |   ✅   | Rfc9114 goaway builder                           |
|  11 | `test_truncated_header_is_refused`                      |   ✅   | Truncated header is refused                      |
|  12 | `test_builders_refuse_a_short_destination`              |   ✅   | Builders refuse a short destination              |

</details>

---

## test_h3_frame - native_h3_frame_rfc - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for HTTP/3 framing (network_drivers/presentation/http/http3/h3_frame.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_rfc9000_sample_varints_as_frame_lengths`          |   ✅   | Rfc9000 sample varints as frame lengths          |
|   2 | `test_rfc9000_long_spelling_decodes_but_is_not_emitted` |   ✅   | Rfc9000 long spelling decodes but is not emitted |
|   3 | `test_rfc9114_frame_type_registry`                      |   ✅   | Rfc9114 frame type registry                      |
|   4 | `test_rfc9114_reserved_http2_frame_types`               |   ✅   | Rfc9114 reserved http2 frame types               |
|   5 | `test_rfc9114_settings_defaults`                        |   ✅   | Rfc9114 settings defaults                        |
|   6 | `test_rfc9114_settings_round_trip`                      |   ✅   | Rfc9114 settings round trip                      |
|   7 | `test_rfc9114_reserved_settings_identifiers`            |   ✅   | Rfc9114 reserved settings identifiers            |
|   8 | `test_rfc9114_settings_truncated_pair`                  |   ✅   | Rfc9114 settings truncated pair                  |
|   9 | `test_rfc9114_data_and_headers_builders`                |   ✅   | Rfc9114 data and headers builders                |
|  10 | `test_rfc9114_goaway_builder`                           |   ✅   | Rfc9114 goaway builder                           |
|  11 | `test_truncated_header_is_refused`                      |   ✅   | Truncated header is refused                      |
|  12 | `test_builders_refuse_a_short_destination`              |   ✅   | Builders refuse a short destination              |

</details>

---

## test_haas_mdc - native_haas_mdc - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_numbered_query_is_the_documented_line`       |   ✅   | Numbered query is the documented line       |
|   2 | `test_macro_query_carries_the_variable_number`     |   ✅   | Macro query carries the variable number     |
|   3 | `test_builders_refuse_a_short_buffer`              |   ✅   | Builders refuse a short buffer              |
|   4 | `test_documented_frame_is_split_on_the_delimiters` |   ✅   | Documented frame is split on the delimiters |
|   5 | `test_bytes_outside_the_frame_are_ignored`         |   ✅   | Bytes outside the frame are ignored         |
|   6 | `test_fields_are_trimmed_of_surrounding_spaces`    |   ✅   | Fields are trimmed of surrounding spaces    |
|   7 | `test_incomplete_frame_is_refused`                 |   ✅   | Incomplete frame is refused                 |
|   8 | `test_q500_program_status_and_parts`               |   ✅   | Q500 program status and parts               |
|   9 | `test_q500_busy_branch_reports_no_counts`          |   ✅   | Q500 busy branch reports no counts          |
|  10 | `test_q500_rejects_a_non_numeric_parts_field`      |   ✅   | Q500 rejects a non numeric parts field      |
|  11 | `test_q600_macro_response`                         |   ✅   | Q600 macro response                         |
|  12 | `test_unknown_is_the_error_response`               |   ✅   | Unknown is the error response               |
|  13 | `test_dprnt_line_is_unframed_text`                 |   ✅   | Dprnt line is unframed text                 |
|  14 | `test_dprnt_keeps_interior_spaces`                 |   ✅   | Dprnt keeps interior spaces                 |
|  15 | `test_field_table_is_bounded`                      |   ✅   | Field table is bounded                      |

</details>

---

## test_happy_eyeballs - native_happy_eyeballs - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Happy Eyeballs v2 destination ordering and attempt gate_

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_rfc8305_4_interleaves_families`                     |   ✅   | Rfc8305 4 interleaves families                     |
|   2 | `test_rfc8305_4_leading_family_follows_the_first_address` |   ✅   | Rfc8305 4 leading family follows the first address |
|   3 | `test_interleave_drains_the_shorter_family`               |   ✅   | Interleave drains the shorter family               |
|   4 | `test_preference_follows_the_scope_ladder`                |   ✅   | Preference follows the scope ladder                |
|   5 | `test_v4_mapped_counts_as_ipv4`                           |   ✅   | V4 mapped counts as ipv4                           |
|   6 | `test_equal_preference_keeps_input_order`                 |   ✅   | Equal preference keeps input order                 |
|   7 | `test_scope_beats_family`                                 |   ✅   | Scope beats family                                 |
|   8 | `test_rfc8305_5_attempt_delay_gate`                       |   ✅   | Rfc8305 5 attempt delay gate                       |
|   9 | `test_attempt_gate_survives_the_millis_wrap`              |   ✅   | Attempt gate survives the millis wrap              |
|  10 | `test_short_and_null_lists_are_left_alone`                |   ✅   | Short and null lists are left alone                |
|  11 | `test_oversized_lists_are_sorted_without_interleaving`    |   ✅   | Oversized lists are sorted without interleaving    |

</details>

---

## test_hart - native_hart - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the HART / HART-IP codec (services/fieldbus/hart/hart.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_published_delimiter_bits`                |   ✅   | Published delimiter bits                |
|   2 | `test_command_zero_frame`                      |   ✅   | Command zero frame                      |
|   3 | `test_checksum_folds_the_frame_to_zero`        |   ✅   | Checksum folds the frame to zero        |
|   4 | `test_long_address_is_driven_by_the_delimiter` |   ✅   | Long address is driven by the delimiter |
|   5 | `test_frame_round_trip`                        |   ✅   | Frame round trip                        |
|   6 | `test_single_bit_corruption_is_refused`        |   ✅   | Single bit corruption is refused        |
|   7 | `test_parse_refuses_a_truncated_frame`         |   ✅   | Parse refuses a truncated frame         |
|   8 | `test_hartip_header_octets`                    |   ✅   | Hartip header octets                    |
|   9 | `test_hartip_payload_slice`                    |   ✅   | Hartip payload slice                    |
|  10 | `test_hartip_refuses_impossible_byte_counts`   |   ✅   | Hartip refuses impossible byte counts   |

</details>

---

## test_hex - native_hex - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for base-16 conversion (shared/hex/hex.h)._

|   # | Test                              | Status | Description                |
| --: | :-------------------------------- | :----: | :------------------------- |
|   1 | `test_digit_tables_are_ascii`     |   ✅   | Digit tables are ascii     |
|   2 | `test_digit_of_nibble`            |   ✅   | Digit of nibble            |
|   3 | `test_digit_masks_to_four_bits`   |   ✅   | Digit masks to four bits   |
|   4 | `test_val_of_character`           |   ✅   | Val of character           |
|   5 | `test_val_refuses_non_digits`     |   ✅   | Val refuses non digits     |
|   6 | `test_encode_decode_round_trip`   |   ✅   | Encode decode round trip   |
|   7 | `test_decode_refuses_odd_length`  |   ✅   | Decode refuses odd length  |
|   8 | `test_decode_refuses_overflow`    |   ✅   | Decode refuses overflow    |
|   9 | `test_u32_is_the_chunk_size_form` |   ✅   | U32 is the chunk size form |

</details>

---

## test_hislip - native_hislip - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the HiSLIP message codec (services/instrumentation/hislip/hislip.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_ivi61_table2_header_layout`                   |   ✅   | Ivi61 table2 header layout                   |
|   2 | `test_header_round_trip`                            |   ✅   | Header round trip                            |
|   3 | `test_prologue_is_checked`                          |   ✅   | Prologue is checked                          |
|   4 | `test_ivi61_table4_message_type_values`             |   ✅   | Ivi61 table4 message type values             |
|   5 | `test_ivi61_port_assignment`                        |   ✅   | Ivi61 port assignment                        |
|   6 | `test_initialize_packs_version_and_vendor`          |   ✅   | Initialize packs version and vendor          |
|   7 | `test_version_words`                                |   ✅   | Version words                                |
|   8 | `test_initialize_response_control_bits_and_session` |   ✅   | Initialize response control bits and session |
|   9 | `test_typed_parsers_reject_the_wrong_type`          |   ✅   | Typed parsers reject the wrong type          |
|  10 | `test_initialize_refuses_a_short_payload`           |   ✅   | Initialize refuses a short payload           |
|  11 | `test_async_initialize_pair`                        |   ✅   | Async initialize pair                        |
|  12 | `test_data_and_data_end`                            |   ✅   | Data and data end                            |
|  13 | `test_message_id_increments_by_two_and_wraps`       |   ✅   | Message id increments by two and wraps       |
|  14 | `test_builders_refuse_a_short_buffer`               |   ✅   | Builders refuse a short buffer               |
|  15 | `test_unknown_message_type_is_carried_through`      |   ✅   | Unknown message type is carried through      |

</details>

---

## test_hmmd - native_hmmd - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Waveshare HMMD mmWave radar codec (server/peripherals/hmmd/hmmd.h)._

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_declared_frame_geometry`                             |   ✅   | Declared frame geometry                             |
|   2 | `test_report_fields_round_trip`                            |   ✅   | Report fields round trip                            |
|   3 | `test_detection_flag_is_exactly_one`                       |   ✅   | Detection flag is exactly one                       |
|   4 | `test_malformed_report_frames_are_refused`                 |   ✅   | Malformed report frames are refused                 |
|   5 | `test_stream_resyncs_past_noise_and_reports_once`          |   ✅   | Stream resyncs past noise and reports once          |
|   6 | `test_stream_handles_a_partial_header_before_the_real_one` |   ✅   | Stream handles a partial header before the real one |
|   7 | `test_stream_drops_an_absurd_length_and_recovers`          |   ✅   | Stream drops an absurd length and recovers          |
|   8 | `test_stream_drops_a_bad_frame_and_keeps_going`            |   ✅   | Stream drops a bad frame and keeps going            |
|   9 | `test_stream_null_arguments_are_refused`                   |   ✅   | Stream null arguments are refused                   |
|  10 | `test_ld2410_v102_published_command_envelope`              |   ✅   | Ld2410 v102 published command envelope              |
|  11 | `test_named_command_words`                                 |   ✅   | Named command words                                 |
|  12 | `test_command_length_field_tracks_the_value`               |   ✅   | Command length field tracks the value               |
|  13 | `test_command_builder_fails_closed`                        |   ✅   | Command builder fails closed                        |
|  14 | `test_ack_decodes_the_command_word_and_payload`            |   ✅   | Ack decodes the command word and payload            |
|  15 | `test_malformed_ack_frames_are_refused`                    |   ✅   | Malformed ack frames are refused                    |

</details>

---

## test_hostlink - native_hostlink - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Omron Host Link (C-mode) frame codec (services/fieldbus/hostlink/hostlink.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_fcs_is_a_running_xor`                   |   ✅   | Fcs is a running xor                   |
|   2 | `test_read_command_frame`                     |   ✅   | Read command frame                     |
|   3 | `test_write_command_frame`                    |   ✅   | Write command frame                    |
|   4 | `test_read_response_words`                    |   ✅   | Read response words                    |
|   5 | `test_build_parse_round_trip`                 |   ✅   | Build parse round trip                 |
|   6 | `test_fcs_rendering_and_acceptance`           |   ✅   | Fcs rendering and acceptance           |
|   7 | `test_single_character_corruption_is_refused` |   ✅   | Single character corruption is refused |
|   8 | `test_parse_rejects_bad_framing`              |   ✅   | Parse rejects bad framing              |
|   9 | `test_end_code_guards`                        |   ✅   | End code guards                        |
|  10 | `test_builders_refuse_a_short_buffer`         |   ✅   | Builders refuse a short buffer         |

</details>

---

## test_hpack - native_hpack - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the shared HPACK/QPACK field-coding primitives_

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_rfc7541_c1_integer_examples`                |   ✅   | Rfc7541 c1 integer examples                |
|   2 | `test_prefix_flags_are_left_alone`                |   ✅   | Prefix flags are left alone                |
|   3 | `test_prefix_int_round_trips_at_every_width`      |   ✅   | Prefix int round trips at every width      |
|   4 | `test_prefix_int_rejects_an_overflowing_encoding` |   ✅   | Prefix int rejects an overflowing encoding |
|   5 | `test_encode_int_refuses_a_short_buffer`          |   ✅   | Encode int refuses a short buffer          |
|   6 | `test_appendix_c_huffman_strings`                 |   ✅   | Appendix c huffman strings                 |
|   7 | `test_appendix_b_huffman_table`                   |   ✅   | Appendix b huffman table                   |
|   8 | `test_huffman_decode_rejects_bad_padding_and_eos` |   ✅   | Huffman decode rejects bad padding and eos |
|   9 | `test_huff_encode_refuses_a_short_buffer`         |   ✅   | Huff encode refuses a short buffer         |
|  10 | `test_decode_str_reads_both_forms`                |   ✅   | Decode str reads both forms                |
|  11 | `test_decode_str_fails_closed`                    |   ✅   | Decode str fails closed                    |
|  12 | `test_encode_str_picks_the_shorter_form`          |   ✅   | Encode str picks the shorter form          |
|  13 | `test_encode_str_round_trips_every_octet`         |   ✅   | Encode str round trips every octet         |

</details>

---

## test_hpack - native_codec_hpack_prim - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the shared HPACK/QPACK field-coding primitives_

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_rfc7541_c1_integer_examples`                |   ✅   | Rfc7541 c1 integer examples                |
|   2 | `test_prefix_flags_are_left_alone`                |   ✅   | Prefix flags are left alone                |
|   3 | `test_prefix_int_round_trips_at_every_width`      |   ✅   | Prefix int round trips at every width      |
|   4 | `test_prefix_int_rejects_an_overflowing_encoding` |   ✅   | Prefix int rejects an overflowing encoding |
|   5 | `test_encode_int_refuses_a_short_buffer`          |   ✅   | Encode int refuses a short buffer          |
|   6 | `test_appendix_c_huffman_strings`                 |   ✅   | Appendix c huffman strings                 |
|   7 | `test_appendix_b_huffman_table`                   |   ✅   | Appendix b huffman table                   |
|   8 | `test_huffman_decode_rejects_bad_padding_and_eos` |   ✅   | Huffman decode rejects bad padding and eos |
|   9 | `test_huff_encode_refuses_a_short_buffer`         |   ✅   | Huff encode refuses a short buffer         |
|  10 | `test_decode_str_reads_both_forms`                |   ✅   | Decode str reads both forms                |
|  11 | `test_decode_str_fails_closed`                    |   ✅   | Decode str fails closed                    |
|  12 | `test_encode_str_picks_the_shorter_form`          |   ✅   | Encode str picks the shorter form          |
|  13 | `test_encode_str_round_trips_every_octet`         |   ✅   | Encode str round trips every octet         |

</details>

---

## test_http_client - native_http_client - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the HTTP user agent (services/net/http_client/http_client.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_target_uri_split`                            |   ✅   | Target uri split                            |
|   2 | `test_target_uri_refusals`                         |   ✅   | Target uri refusals                         |
|   3 | `test_get_request_message`                         |   ✅   | Get request message                         |
|   4 | `test_host_field_carries_only_a_non_default_port`  |   ✅   | Host field carries only a non default port  |
|   5 | `test_post_request_message`                        |   ✅   | Post request message                        |
|   6 | `test_post_defaults_the_content_type`              |   ✅   | Post defaults the content type              |
|   7 | `test_build_refuses_a_short_buffer`                |   ✅   | Build refuses a short buffer                |
|   8 | `test_status_line`                                 |   ✅   | Status line                                 |
|   9 | `test_malformed_responses_are_refused`             |   ✅   | Malformed responses are refused             |
|  10 | `test_body_framing_follows_the_rfc9112_precedence` |   ✅   | Body framing follows the rfc9112 precedence |
|  11 | `test_chunked_must_be_the_final_coding`            |   ✅   | Chunked must be the final coding            |
|  12 | `test_a_short_body_is_clamped_to_what_arrived`     |   ✅   | A short body is clamped to what arrived     |
|  13 | `test_field_names_are_case_insensitive`            |   ✅   | Field names are case insensitive            |
|  14 | `test_body_offset_is_past_the_field_section`       |   ✅   | Body offset is past the field section       |

</details>

---

## test_http_date - native_http_date - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IMF-fixdate formatter (shared/http_date/http_date.h)._

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_rfc9110_published_example`             |   ✅   | Rfc9110 published example             |
|   2 | `test_form_is_fixed_width`                   |   ✅   | Form is fixed width                   |
|   3 | `test_one_second_past_the_epoch`             |   ✅   | One second past the epoch             |
|   4 | `test_signed_32_bit_limit`                   |   ✅   | Signed 32 bit limit                   |
|   5 | `test_day_names_cycle_from_the_anchor`       |   ✅   | Day names cycle from the anchor       |
|   6 | `test_leap_day_2000`                         |   ✅   | Leap day 2000                         |
|   7 | `test_epoch_zero_renders_empty`              |   ✅   | Epoch zero renders empty              |
|   8 | `test_short_buffer_yields_empty_not_partial` |   ✅   | Short buffer yields empty not partial |
|   9 | `test_null_destination_is_refused`           |   ✅   | Null destination is refused           |
|  10 | `test_zero_capacity_is_refused`              |   ✅   | Zero capacity is refused              |

</details>

---

## test_http_delivery - native_http_delivery - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_rfc5861_worked_example`                        |   ✅   | Rfc5861 worked example                        |
|   2 | `test_rfc5861_example_header`                        |   ✅   | Rfc5861 example header                        |
|   3 | `test_rfc5861_twenty_minute_total`                   |   ✅   | Rfc5861 twenty minute total                   |
|   4 | `test_no_swr_window_has_no_stale_band`               |   ✅   | No swr window has no stale band               |
|   5 | `test_verdict_is_monotonic_in_age`                   |   ✅   | Verdict is monotonic in age                   |
|   6 | `test_window_sum_does_not_wrap`                      |   ✅   | Window sum does not wrap                      |
|   7 | `test_cache_control_omits_a_zero_swr`                |   ✅   | Cache control omits a zero swr                |
|   8 | `test_cache_control_renders_the_full_range`          |   ✅   | Cache control renders the full range          |
|   9 | `test_cache_control_refuses_a_short_buffer`          |   ✅   | Cache control refuses a short buffer          |
|  10 | `test_manifest_shape`                                |   ✅   | Manifest shape                                |
|  11 | `test_manifest_escapes_per_rfc8259`                  |   ✅   | Manifest escapes per rfc8259                  |
|  12 | `test_full_precache_list_fits_the_configured_buffer` |   ✅   | Full precache list fits the configured buffer |
|  13 | `test_manifest_refuses_rather_than_truncating`       |   ✅   | Manifest refuses rather than truncating       |
|  14 | `test_manifest_refuses_bad_arguments`                |   ✅   | Manifest refuses bad arguments                |

</details>

---

## test_http_parser - native_http_parser - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the standalone HTTP/1.1 request parser_

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_rfc9112_2_1_message_grammar`                   |   ✅   | Rfc9112 2 1 message grammar                   |
|   2 | `test_rfc9112_6_3_body_framing`                      |   ✅   | Rfc9112 6 3 body framing                      |
|   3 | `test_rfc9112_3_request_line`                        |   ✅   | Rfc9112 3 request line                        |
|   4 | `test_rfc9112_2_3_version_is_case_sensitive`         |   ✅   | Rfc9112 2 3 version is case sensitive         |
|   5 | `test_rfc9112_5_field_lines`                         |   ✅   | Rfc9112 5 field lines                         |
|   6 | `test_rfc9112_5_1_space_before_colon_is_rejected`    |   ✅   | Rfc9112 5 1 space before colon is rejected    |
|   7 | `test_rfc9112_3_2_duplicate_host_is_rejected`        |   ✅   | Rfc9112 3 2 duplicate host is rejected        |
|   8 | `test_rfc9112_2_2_requires_crlf`                     |   ✅   | Rfc9112 2 2 requires crlf                     |
|   9 | `test_capacity_limits_get_their_own_terminal_states` |   ✅   | Capacity limits get their own terminal states |
|  10 | `test_terminal_states_ignore_further_octets`         |   ✅   | Terminal states ignore further octets         |
|  11 | `test_reset_clears_everything_but_the_slot`          |   ✅   | Reset clears everything but the slot          |
|  12 | `test_segmentation_does_not_change_the_parse`        |   ✅   | Segmentation does not change the parse        |
|  13 | `test_headers_past_the_cap_still_frame_the_message`  |   ✅   | Headers past the cap still frame the message  |
|  14 | `test_rfc6265_cookie_extraction`                     |   ✅   | Rfc6265 cookie extraction                     |
|  15 | `test_rfc7239_forwarded_client`                      |   ✅   | Rfc7239 forwarded client                      |
|  16 | `test_urlencoded_form_fields`                        |   ✅   | Urlencoded form fields                        |
|  17 | `test_lookup_helpers_refuse_a_null_destination`      |   ✅   | Lookup helpers refuse a null destination      |

</details>

---

## test_httpcache - native_httpcache - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Cache-Control directive builder / parser / freshness helper_

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_rfc9111_4_2_1_first_match`                    |   ✅   | Rfc9111 4 2 1 first match                    |
|   2 | `test_init_is_an_empty_directive_set`               |   ✅   | Init is an empty directive set               |
|   3 | `test_build_emits_the_grammar`                      |   ✅   | Build emits the grammar                      |
|   4 | `test_build_reports_its_own_length`                 |   ✅   | Build reports its own length                 |
|   5 | `test_parse_is_tolerant_as_sec_5_2_requires`        |   ✅   | Parse is tolerant as sec 5 2 requires        |
|   6 | `test_parse_separates_bare_max_stale_from_valued`   |   ✅   | Parse separates bare max stale from valued   |
|   7 | `test_delta_seconds_saturates_rather_than_wrapping` |   ✅   | Delta seconds saturates rather than wrapping |
|   8 | `test_build_parse_round_trip`                       |   ✅   | Build parse round trip                       |
|   9 | `test_presets_match_their_documented_directives`    |   ✅   | Presets match their documented directives    |
|  10 | `test_build_refuses_a_short_buffer`                 |   ✅   | Build refuses a short buffer                 |

</details>

---

## test_hw_health - native_hw_health - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the hardware-health decision cores (server/signaling/hw_health.h)._

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_rail_thresholds_are_strictly_below`                       |   ✅   | Rail thresholds are strictly below                       |
|   2 | `test_rail_min_is_the_worst_droop_and_events_tally`             |   ✅   | Rail min is the worst droop and events tally             |
|   3 | `test_rail_json_is_an_rfc8259_object`                           |   ✅   | Rail json is an rfc8259 object                           |
|   4 | `test_rail_json_fails_closed_on_a_short_buffer`                 |   ✅   | Rail json fails closed on a short buffer                 |
|   5 | `test_spi_halves_only_on_a_full_fail_streak`                    |   ✅   | Spi halves only on a full fail streak                    |
|   6 | `test_spi_doubles_only_on_a_full_ok_streak`                     |   ✅   | Spi doubles only on a full ok streak                     |
|   7 | `test_spi_clock_stays_between_floor_and_ceiling`                |   ✅   | Spi clock stays between floor and ceiling                |
|   8 | `test_spi_init_clamps_the_start_clock_and_defaults_a_zero_trip` |   ✅   | Spi init clamps the start clock and defaults a zero trip |
|   9 | `test_spi_doubling_wrap_clamps_to_the_ceiling`                  |   ✅   | Spi doubling wrap clamps to the ceiling                  |
|  10 | `test_gpio_readback_mismatch_names_the_rail`                    |   ✅   | Gpio readback mismatch names the rail                    |
|  11 | `test_cap_tolerance_window_is_inclusive`                        |   ✅   | Cap tolerance window is inclusive                        |
|  12 | `test_cap_band_truncates_and_a_zero_expectation_never_judges`   |   ✅   | Cap band truncates and a zero expectation never judges   |
|  13 | `test_cap_band_wider_than_expected_clamps_the_low_edge`         |   ✅   | Cap band wider than expected clamps the low edge         |
|  14 | `test_a_missing_monitor_is_refused`                             |   ✅   | A missing monitor is refused                             |

</details>

---

## test_iccp - native_iccp - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the ICCP / TASE.2 (IEC 60870-6) indication-point codec (services/energy/iccp/iccp.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_state_q_ber_layout`                        |   ✅   | State q ber layout                        |
|   2 | `test_state_q_carries_an_optional_timestamp`     |   ✅   | State q carries an optional timestamp     |
|   3 | `test_state_and_quality_occupy_separate_fields`  |   ✅   | State and quality occupy separate fields  |
|   4 | `test_real_q_ber_layout`                         |   ✅   | Real q ber layout                         |
|   5 | `test_real_q_integer_is_minimal_twos_complement` |   ✅   | Real q integer is minimal twos complement |
|   6 | `test_real_q_quality_is_masked`                  |   ✅   | Real q quality is masked                  |
|   7 | `test_build_refuses_an_undersized_buffer`        |   ✅   | Build refuses an undersized buffer        |

</details>

---

## test_iec60870 - native_iec60870 - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEC 60870-5-101 / -104 telecontrol codec (services/energy/iec60870/iec60870.h)._

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_iec104_i_format_field_layout`                             |   ✅   | Iec104 i format field layout                             |
|   2 | `test_iec104_sequence_numbers_span_fifteen_bits`                |   ✅   | Iec104 sequence numbers span fifteen bits                |
|   3 | `test_iec104_s_format`                                          |   ✅   | Iec104 s format                                          |
|   4 | `test_iec104_u_format_commands`                                 |   ✅   | Iec104 u format commands                                 |
|   5 | `test_iec104_parse_rejects_malformed_apdus`                     |   ✅   | Iec104 parse rejects malformed apdus                     |
|   6 | `test_iec104_build_refuses_oversized_or_unbuffered_apdus`       |   ✅   | Iec104 build refuses oversized or unbuffered apdus       |
|   7 | `test_asdu_header_field_layout`                                 |   ✅   | Asdu header field layout                                 |
|   8 | `test_information_object_address_is_three_octets_little_endian` |   ✅   | Information object address is three octets little endian |
|   9 | `test_single_point_object`                                      |   ✅   | Single point object                                      |
|  10 | `test_double_point_object`                                      |   ✅   | Double point object                                      |
|  11 | `test_short_float_measured_value`                               |   ✅   | Short float measured value                               |
|  12 | `test_scaled_measured_value`                                    |   ✅   | Scaled measured value                                    |
|  13 | `test_normalized_measured_value`                                |   ✅   | Normalized measured value                                |
|  14 | `test_integrated_totals_counter`                                |   ✅   | Integrated totals counter                                |
|  15 | `test_single_command_object`                                    |   ✅   | Single command object                                    |
|  16 | `test_double_command_object`                                    |   ✅   | Double command object                                    |
|  17 | `test_ft12_fixed_length_frame`                                  |   ✅   | Ft12 fixed length frame                                  |
|  18 | `test_ft12_variable_length_frame`                               |   ✅   | Ft12 variable length frame                               |
|  19 | `test_ft12_parse_rejects_corrupted_frames`                      |   ✅   | Ft12 parse rejects corrupted frames                      |
|  20 | `test_ft12_build_refuses_oversized_or_unbuffered_frames`        |   ✅   | Ft12 build refuses oversized or unbuffered frames        |

</details>

---

## test_ikev2_natt - native_ikev2 - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for IKEv2 NAT traversal (services/security/ikev2/ikev2_natt.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc7296_detection_notify_layout`           |   ✅   | Rfc7296 detection notify layout           |
|   2 | `test_every_digest_input_matters`                |   ✅   | Every digest input matters                |
|   3 | `test_address_length_is_four_or_sixteen`         |   ✅   | Address length is four or sixteen         |
|   4 | `test_nat_detection_verdicts`                    |   ✅   | Nat detection verdicts                    |
|   5 | `test_built_payload_carries_the_matching_digest` |   ✅   | Built payload carries the matching digest |
|   6 | `test_rfc3948_keepalive`                         |   ✅   | Rfc3948 keepalive                         |
|   7 | `test_rfc3948_non_esp_marker`                    |   ✅   | Rfc3948 non esp marker                    |

</details>

---

## test_ikev2 - native_ikev2 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IKEv2 message and payload codec (services/security/ikev2/ikev2.h)._

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_rfc7296_ike_header_layout`           |   ✅   | Rfc7296 ike header layout           |
|   2 | `test_length_is_patched_in_place`          |   ✅   | Length is patched in place          |
|   3 | `test_rfc7296_generic_payload_header`      |   ✅   | Rfc7296 generic payload header      |
|   4 | `test_payload_chain_is_walked_forward`     |   ✅   | Payload chain is walked forward     |
|   5 | `test_payload_chain_rejects_bad_lengths`   |   ✅   | Payload chain rejects bad lengths   |
|   6 | `test_rfc7296_sa_proposal_transform_tree`  |   ✅   | Rfc7296 sa proposal transform tree  |
|   7 | `test_sa_proposal_with_an_spi`             |   ✅   | Sa proposal with an spi             |
|   8 | `test_rfc7296_ke_payload`                  |   ✅   | Rfc7296 ke payload                  |
|   9 | `test_rfc7296_nonce_id_and_auth_payloads`  |   ✅   | Rfc7296 nonce id and auth payloads  |
|  10 | `test_rfc7296_notify_payload`              |   ✅   | Rfc7296 notify payload              |
|  11 | `test_rfc7296_delete_payload`              |   ✅   | Rfc7296 delete payload              |
|  12 | `test_rfc7296_traffic_selectors`           |   ✅   | Rfc7296 traffic selectors           |
|  13 | `test_rfc7296_configuration_payload`       |   ✅   | Rfc7296 configuration payload       |
|  14 | `test_rfc5282_sk_payload_envelope`         |   ✅   | Rfc5282 sk payload envelope         |
|  15 | `test_rfc7748_curve25519_key_exchange`     |   ✅   | Rfc7748 curve25519 key exchange     |
|  16 | `test_dh_refuses_other_groups_and_lengths` |   ✅   | Dh refuses other groups and lengths |
|  17 | `test_rfc7296_suite_key_lengths`           |   ✅   | Rfc7296 suite key lengths           |
|  18 | `test_rfc7296_prf_plus_is_one_stream`      |   ✅   | Rfc7296 prf plus is one stream      |
|  19 | `test_rfc7296_stateless_cookie`            |   ✅   | Rfc7296 stateless cookie            |

</details>

---

## test_ikev2 - native_ikev2_rfc7296 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IKEv2 message and payload codec (services/security/ikev2/ikev2.h)._

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_rfc7296_ike_header_layout`           |   ✅   | Rfc7296 ike header layout           |
|   2 | `test_length_is_patched_in_place`          |   ✅   | Length is patched in place          |
|   3 | `test_rfc7296_generic_payload_header`      |   ✅   | Rfc7296 generic payload header      |
|   4 | `test_payload_chain_is_walked_forward`     |   ✅   | Payload chain is walked forward     |
|   5 | `test_payload_chain_rejects_bad_lengths`   |   ✅   | Payload chain rejects bad lengths   |
|   6 | `test_rfc7296_sa_proposal_transform_tree`  |   ✅   | Rfc7296 sa proposal transform tree  |
|   7 | `test_sa_proposal_with_an_spi`             |   ✅   | Sa proposal with an spi             |
|   8 | `test_rfc7296_ke_payload`                  |   ✅   | Rfc7296 ke payload                  |
|   9 | `test_rfc7296_nonce_id_and_auth_payloads`  |   ✅   | Rfc7296 nonce id and auth payloads  |
|  10 | `test_rfc7296_notify_payload`              |   ✅   | Rfc7296 notify payload              |
|  11 | `test_rfc7296_delete_payload`              |   ✅   | Rfc7296 delete payload              |
|  12 | `test_rfc7296_traffic_selectors`           |   ✅   | Rfc7296 traffic selectors           |
|  13 | `test_rfc7296_configuration_payload`       |   ✅   | Rfc7296 configuration payload       |
|  14 | `test_rfc5282_sk_payload_envelope`         |   ✅   | Rfc5282 sk payload envelope         |
|  15 | `test_rfc7748_curve25519_key_exchange`     |   ✅   | Rfc7748 curve25519 key exchange     |
|  16 | `test_dh_refuses_other_groups_and_lengths` |   ✅   | Dh refuses other groups and lengths |
|  17 | `test_rfc7296_suite_key_lengths`           |   ✅   | Rfc7296 suite key lengths           |
|  18 | `test_rfc7296_prf_plus_is_one_stream`      |   ✅   | Rfc7296 prf plus is one stream      |
|  19 | `test_rfc7296_stateless_cookie`            |   ✅   | Rfc7296 stateless cookie            |

</details>

---

## test_ikev2_natt - native_ikev2_natt_rfc3948 - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for IKEv2 NAT traversal (services/security/ikev2/ikev2_natt.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc7296_detection_notify_layout`           |   ✅   | Rfc7296 detection notify layout           |
|   2 | `test_every_digest_input_matters`                |   ✅   | Every digest input matters                |
|   3 | `test_address_length_is_four_or_sixteen`         |   ✅   | Address length is four or sixteen         |
|   4 | `test_nat_detection_verdicts`                    |   ✅   | Nat detection verdicts                    |
|   5 | `test_built_payload_carries_the_matching_digest` |   ✅   | Built payload carries the matching digest |
|   6 | `test_rfc3948_keepalive`                         |   ✅   | Rfc3948 keepalive                         |
|   7 | `test_rfc3948_non_esp_marker`                    |   ✅   | Rfc3948 non esp marker                    |

</details>

---

## test_ina219 - native_ina219 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TI INA219 current/power codec (server/peripherals/ina219/ina219.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_sbos448_bus_voltage_register`                      |   ✅   | Sbos448 bus voltage register                      |
|   2 | `test_sbos448_bus_status_bits_do_not_reach_the_voltage`  |   ✅   | Sbos448 bus status bits do not reach the voltage  |
|   3 | `test_sbos448_shunt_voltage_register`                    |   ✅   | Sbos448 shunt voltage register                    |
|   4 | `test_sbos448_calibration_equation`                      |   ✅   | Sbos448 calibration equation                      |
|   5 | `test_calibration_truncates_and_clamps_to_sixteen_bits`  |   ✅   | Calibration truncates and clamps to sixteen bits  |
|   6 | `test_calibration_zero_denominator`                      |   ✅   | Calibration zero denominator                      |
|   7 | `test_calibration_falls_as_the_denominator_grows`        |   ✅   | Calibration falls as the denominator grows        |
|   8 | `test_sbos448_current_scaling`                           |   ✅   | Sbos448 current scaling                           |
|   9 | `test_sbos448_power_lsb_is_twenty_times_the_current_lsb` |   ✅   | Sbos448 power lsb is twenty times the current lsb |
|  10 | `test_current_and_power_are_odd_about_zero`              |   ✅   | Current and power are odd about zero              |
|  11 | `test_sbos448_register_addresses`                        |   ✅   | Sbos448 register addresses                        |
|  12 | `test_bus_voltage_is_monotone`                           |   ✅   | Bus voltage is monotone                           |

</details>

---

## test_inflate - native_inflate - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_transport/inflate.c (RFC 1950, RFC 1951, RFC 4253 sec 6.2): the decompressor half of the_

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_rfc1951_hand_built_blocks`                      |   ✅   | Rfc1951 hand built blocks                      |
|   2 | `test_permessage_deflate_payload_with_the_marker`     |   ✅   | Permessage deflate payload with the marker     |
|   3 | `test_dynamic_huffman_block`                          |   ✅   | Dynamic huffman block                          |
|   4 | `test_reserved_block_type_is_refused`                 |   ✅   | Reserved block type is refused                 |
|   5 | `test_stored_block_nlen_must_be_the_complement`       |   ✅   | Stored block nlen must be the complement       |
|   6 | `test_symbols_that_never_occur_are_refused`           |   ✅   | Symbols that never occur are refused           |
|   7 | `test_distance_before_the_start_of_output_is_refused` |   ✅   | Distance before the start of output is refused |
|   8 | `test_truncated_stream_is_refused`                    |   ✅   | Truncated stream is refused                    |
|   9 | `test_output_overflow_fails_closed`                   |   ✅   | Output overflow fails closed                   |
|  10 | `test_scratch_too_small_fails_closed`                 |   ✅   | Scratch too small fails closed                 |
|  11 | `test_two_blocks_concatenate`                         |   ✅   | Two blocks concatenate                         |

</details>

---

## test_inflate - native_codec_inflate - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_transport/inflate.c (RFC 1950, RFC 1951, RFC 4253 sec 6.2): the decompressor half of the_

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_rfc1951_hand_built_blocks`                      |   ✅   | Rfc1951 hand built blocks                      |
|   2 | `test_permessage_deflate_payload_with_the_marker`     |   ✅   | Permessage deflate payload with the marker     |
|   3 | `test_dynamic_huffman_block`                          |   ✅   | Dynamic huffman block                          |
|   4 | `test_reserved_block_type_is_refused`                 |   ✅   | Reserved block type is refused                 |
|   5 | `test_stored_block_nlen_must_be_the_complement`       |   ✅   | Stored block nlen must be the complement       |
|   6 | `test_symbols_that_never_occur_are_refused`           |   ✅   | Symbols that never occur are refused           |
|   7 | `test_distance_before_the_start_of_output_is_refused` |   ✅   | Distance before the start of output is refused |
|   8 | `test_truncated_stream_is_refused`                    |   ✅   | Truncated stream is refused                    |
|   9 | `test_output_overflow_fails_closed`                   |   ✅   | Output overflow fails closed                   |
|  10 | `test_scratch_too_small_fails_closed`                 |   ✅   | Scratch too small fails closed                 |
|  11 | `test_two_blocks_concatenate`                         |   ✅   | Two blocks concatenate                         |

</details>

---

## test_interbus - native_interbus - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the INTERBUS summation-frame codec (services/fieldbus/interbus/interbus.h)._

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_published_check_value`            |   ✅   | Published check value            |
|   2 | `test_loopback_word`                    |   ✅   | Loopback word                    |
|   3 | `test_frame_layout`                     |   ✅   | Frame layout                     |
|   4 | `test_zero_word_frame`                  |   ✅   | Zero word frame                  |
|   5 | `test_round_trip`                       |   ✅   | Round trip                       |
|   6 | `test_single_bit_corruption_is_refused` |   ✅   | Single bit corruption is refused |
|   7 | `test_open_ring_is_refused`             |   ✅   | Open ring is refused             |
|   8 | `test_parse_refuses_malformed_lengths`  |   ✅   | Parse refuses malformed lengths  |
|   9 | `test_build_refuses_a_short_buffer`     |   ✅   | Build refuses a short buffer     |

</details>

---

## test_iolink - native_iolink - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IO-Link (SDCI) data-link message codec (services/fieldbus/iolink/iolink.h)._

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_mc_octet_fields`                  |   ✅   | Mc octet fields                  |
|   2 | `test_ckt_octet_fields`                 |   ✅   | Ckt octet fields                 |
|   3 | `test_cks_octet_fields`                 |   ✅   | Cks octet fields                 |
|   4 | `test_type0_read_checksum`              |   ✅   | Type0 read checksum              |
|   5 | `test_type0_write_checksum`             |   ✅   | Type0 write checksum             |
|   6 | `test_device_reply_checksum`            |   ✅   | Device reply checksum            |
|   7 | `test_finalize_then_verify`             |   ✅   | Finalize then verify             |
|   8 | `test_single_bit_corruption_is_refused` |   ✅   | Single bit corruption is refused |
|   9 | `test_bounds_are_refused`               |   ✅   | Bounds are refused               |

</details>

---

## test_ip - native_ip - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IP address core (shared/ip/ip.h)._

|   # | Test                                 | Status | Description                   |
| --: | :----------------------------------- | :----: | :---------------------------- |
|   1 | `test_v4_round_trip`                 |   ✅   | V4 round trip                 |
|   2 | `test_rfc5952_canonical_output`      |   ✅   | Rfc5952 canonical output      |
|   3 | `test_v4_mapped`                     |   ✅   | V4 mapped                     |
|   4 | `test_malformed_text_is_refused`     |   ✅   | Malformed text is refused     |
|   5 | `test_constructors_match_the_parser` |   ✅   | Constructors match the parser |
|   6 | `test_to_v4_be`                      |   ✅   | To v4 be                      |
|   7 | `test_equal_separates_families`      |   ✅   | Equal separates families      |
|   8 | `test_is_unspecified`                |   ✅   | Is unspecified                |
|   9 | `test_classify_v4`                   |   ✅   | Classify v4                   |
|  10 | `test_classify_v6`                   |   ✅   | Classify v6                   |
|  11 | `test_prefix_match`                  |   ✅   | Prefix match                  |
|  12 | `test_format_refuses_a_short_buffer` |   ✅   | Format refuses a short buffer |

</details>

---

## test_ipsec_db - native_ipsec_db - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IPsec policy and SA databases (services/system/esp/ipsec_db.h)._

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_rfc4301_spd_is_ordered_first_match`                       |   ✅   | Rfc4301 spd is ordered first match                       |
|   2 | `test_spd_lookup_reports_no_match`                              |   ✅   | Spd lookup reports no match                              |
|   3 | `test_selector_ranges_are_inclusive`                            |   ✅   | Selector ranges are inclusive                            |
|   4 | `test_selector_compares_whole_addresses_and_families`           |   ✅   | Selector compares whole addresses and families           |
|   5 | `test_selector_matches_ipv6_ranges`                             |   ✅   | Selector matches ipv6 ranges                             |
|   6 | `test_spd_is_bounded`                                           |   ✅   | Spd is bounded                                           |
|   7 | `test_selector_from_ikev2_traffic_selectors`                    |   ✅   | Selector from ikev2 traffic selectors                    |
|   8 | `test_rfc4301_sad_is_keyed_by_spi`                              |   ✅   | Rfc4301 sad is keyed by spi                              |
|   9 | `test_sad_remove`                                               |   ✅   | Sad remove                                               |
|  10 | `test_sad_is_bounded`                                           |   ✅   | Sad is bounded                                           |
|  11 | `test_rfc4303_outbound_sequence_starts_at_one_and_never_cycles` |   ✅   | Rfc4303 outbound sequence starts at one and never cycles |
|  12 | `test_sequence_numbers_are_per_sa`                              |   ✅   | Sequence numbers are per sa                              |
|  13 | `test_inbound_sa_carries_its_replay_window`                     |   ✅   | Inbound sa carries its replay window                     |
|  14 | `test_null_arguments_are_refused`                               |   ✅   | Null arguments are refused                               |

</details>

---

## test_ipsec_db - native_system_ipsec_db - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IPsec policy and SA databases (services/system/esp/ipsec_db.h)._

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_rfc4301_spd_is_ordered_first_match`                       |   ✅   | Rfc4301 spd is ordered first match                       |
|   2 | `test_spd_lookup_reports_no_match`                              |   ✅   | Spd lookup reports no match                              |
|   3 | `test_selector_ranges_are_inclusive`                            |   ✅   | Selector ranges are inclusive                            |
|   4 | `test_selector_compares_whole_addresses_and_families`           |   ✅   | Selector compares whole addresses and families           |
|   5 | `test_selector_matches_ipv6_ranges`                             |   ✅   | Selector matches ipv6 ranges                             |
|   6 | `test_spd_is_bounded`                                           |   ✅   | Spd is bounded                                           |
|   7 | `test_selector_from_ikev2_traffic_selectors`                    |   ✅   | Selector from ikev2 traffic selectors                    |
|   8 | `test_rfc4301_sad_is_keyed_by_spi`                              |   ✅   | Rfc4301 sad is keyed by spi                              |
|   9 | `test_sad_remove`                                               |   ✅   | Sad remove                                               |
|  10 | `test_sad_is_bounded`                                           |   ✅   | Sad is bounded                                           |
|  11 | `test_rfc4303_outbound_sequence_starts_at_one_and_never_cycles` |   ✅   | Rfc4303 outbound sequence starts at one and never cycles |
|  12 | `test_sequence_numbers_are_per_sa`                              |   ✅   | Sequence numbers are per sa                              |
|  13 | `test_inbound_sa_carries_its_replay_window`                     |   ✅   | Inbound sa carries its replay window                     |
|  14 | `test_null_arguments_are_refused`                               |   ✅   | Null arguments are refused                               |

</details>

---

## test_j1939 - native_j1939 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SAE J1939 message codec (services/fieldbus/j1939/j1939.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_published_pgn_registry`                   |   ✅   | Published pgn registry                   |
|   2 | `test_published_identifiers`                    |   ✅   | Published identifiers                    |
|   3 | `test_pdu1_and_pdu2_boundary`                   |   ✅   | Pdu1 and pdu2 boundary                   |
|   4 | `test_name_bit_layout`                          |   ✅   | Name bit layout                          |
|   5 | `test_single_frame_padding`                     |   ✅   | Single frame padding                     |
|   6 | `test_bam_announce`                             |   ✅   | Bam announce                             |
|   7 | `test_transport_protocol_reassembly`            |   ✅   | Transport protocol reassembly            |
|   8 | `test_transport_protocol_rejects_bad_sequences` |   ✅   | Transport protocol rejects bad sequences |
|   9 | `test_decode_eec1`                              |   ✅   | Decode eec1                              |
|  10 | `test_decode_et1`                               |   ✅   | Decode et1                               |
|  11 | `test_decode_ccvs`                              |   ✅   | Decode ccvs                              |
|  12 | `test_decode_vd`                                |   ✅   | Decode vd                                |
|  13 | `test_decode_lfe_amb_ic1`                       |   ✅   | Decode lfe amb ic1                       |
|  14 | `test_decode_dm1`                               |   ✅   | Decode dm1                               |

</details>

---

## test_j2735 - native_j2735 - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_x691_constrained_integer_width`          |   ✅   | X691 constrained integer width          |
|   2 | `test_bits_are_packed_msb_first`               |   ✅   | Bits are packed msb first               |
|   3 | `test_cint_is_the_offset_from_the_lower_bound` |   ✅   | Cint is the offset from the lower bound |
|   4 | `test_bsm_core_bit_layout`                     |   ✅   | Bsm core bit layout                     |
|   5 | `test_bsm_core_round_trip_at_the_range_bounds` |   ✅   | Bsm core round trip at the range bounds |
|   6 | `test_bsm_core_bounds`                         |   ✅   | Bsm core bounds                         |
|   7 | `test_writer_overflow_latches`                 |   ✅   | Writer overflow latches                 |
|   8 | `test_reader_refuses_a_read_past_the_end`      |   ✅   | Reader refuses a read past the end      |
|   9 | `test_spat_round_trip`                         |   ✅   | Spat round trip                         |
|  10 | `test_spat_count_bounds`                       |   ✅   | Spat count bounds                       |
|  11 | `test_map_round_trip`                          |   ✅   | Map round trip                          |
|  12 | `test_map_bounds`                              |   ✅   | Map bounds                              |
|  13 | `test_phase_state_values`                      |   ✅   | Phase state values                      |

</details>

---

## test_j2735 - native_j2735_uper - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_x691_constrained_integer_width`          |   ✅   | X691 constrained integer width          |
|   2 | `test_bits_are_packed_msb_first`               |   ✅   | Bits are packed msb first               |
|   3 | `test_cint_is_the_offset_from_the_lower_bound` |   ✅   | Cint is the offset from the lower bound |
|   4 | `test_bsm_core_bit_layout`                     |   ✅   | Bsm core bit layout                     |
|   5 | `test_bsm_core_round_trip_at_the_range_bounds` |   ✅   | Bsm core round trip at the range bounds |
|   6 | `test_bsm_core_bounds`                         |   ✅   | Bsm core bounds                         |
|   7 | `test_writer_overflow_latches`                 |   ✅   | Writer overflow latches                 |
|   8 | `test_reader_refuses_a_read_past_the_end`      |   ✅   | Reader refuses a read past the end      |
|   9 | `test_spat_round_trip`                         |   ✅   | Spat round trip                         |
|  10 | `test_spat_count_bounds`                       |   ✅   | Spat count bounds                       |
|  11 | `test_map_round_trip`                          |   ✅   | Map round trip                          |
|  12 | `test_map_bounds`                              |   ✅   | Map bounds                              |
|  13 | `test_phase_state_values`                      |   ✅   | Phase state values                      |

</details>

---

## test_json - native_json_codec - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the zero-heap JSON writer and top-level reader_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_rfc8259_section_13_example_document`    |   ✅   | Rfc8259 section 13 example document    |
|   2 | `test_rfc8259_mandatory_escapes`              |   ✅   | Rfc8259 mandatory escapes              |
|   3 | `test_member_name_is_escaped`                 |   ✅   | Member name is escaped                 |
|   4 | `test_rfc8259_literal_names`                  |   ✅   | Rfc8259 literal names                  |
|   5 | `test_rfc8259_g_clef_surrogate`               |   ✅   | Rfc8259 g clef surrogate               |
|   6 | `test_rfc3629_escape_widths`                  |   ✅   | Rfc3629 escape widths                  |
|   7 | `test_rfc8259_unpaired_surrogate`             |   ✅   | Rfc8259 unpaired surrogate             |
|   8 | `test_rfc8259_two_character_escapes`          |   ✅   | Rfc8259 two character escapes          |
|   9 | `test_reader_matches_only_top_level_members`  |   ✅   | Reader matches only top level members  |
|  10 | `test_reader_skips_insignificant_whitespace`  |   ✅   | Reader skips insignificant whitespace  |
|  11 | `test_write_read_round_trip`                  |   ✅   | Write read round trip                  |
|  12 | `test_reader_refuses_a_mismatched_type`       |   ✅   | Reader refuses a mismatched type       |
|  13 | `test_reader_guards`                          |   ✅   | Reader guards                          |
|  14 | `test_reader_truncates_to_capacity`           |   ✅   | Reader truncates to capacity           |
|  15 | `test_writer_overflow_latches_and_terminates` |   ✅   | Writer overflow latches and terminates |
|  16 | `test_writer_depth_limit`                     |   ✅   | Writer depth limit                     |
|  17 | `test_writer_unbalanced_close`                |   ✅   | Writer unbalanced close                |
|  18 | `test_writer_without_storage`                 |   ✅   | Writer without storage                 |
|  19 | `test_writer_raw_literal`                     |   ✅   | Writer raw literal                     |

</details>

---

## test_jwt - native_jwt - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the HS256 JWT verifier (services/security/jwt/jwt.h)._

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc7515_a1_example`                   |   ✅   | Rfc7515 a1 example                   |
|   2 | `test_wrong_key_is_refused`                 |   ✅   | Wrong key is refused                 |
|   3 | `test_any_altered_character_is_refused`     |   ✅   | Any altered character is refused     |
|   4 | `test_alg_must_name_hs256`                  |   ✅   | Alg must name hs256                  |
|   5 | `test_malformed_serializations_are_refused` |   ✅   | Malformed serializations are refused |
|   6 | `test_bearer_credentials`                   |   ✅   | Bearer credentials                   |
|   7 | `test_rfc7515_a1_claims`                    |   ✅   | Rfc7515 a1 claims                    |
|   8 | `test_time_claims_window`                   |   ✅   | Time claims window                   |
|   9 | `test_verify_mac_at_needs_both`             |   ✅   | Verify mac at needs both             |
|  10 | `test_claim_str_escapes_and_bounds`         |   ✅   | Claim str escapes and bounds         |
|  11 | `test_scope_matches_whole_tokens`           |   ✅   | Scope matches whole tokens           |
|  12 | `test_scope_claim_then_check`               |   ✅   | Scope claim then check               |

</details>

---

## test_jwt - native_jwt_rfc7515 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the HS256 JWT verifier (services/security/jwt/jwt.h)._

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc7515_a1_example`                   |   ✅   | Rfc7515 a1 example                   |
|   2 | `test_wrong_key_is_refused`                 |   ✅   | Wrong key is refused                 |
|   3 | `test_any_altered_character_is_refused`     |   ✅   | Any altered character is refused     |
|   4 | `test_alg_must_name_hs256`                  |   ✅   | Alg must name hs256                  |
|   5 | `test_malformed_serializations_are_refused` |   ✅   | Malformed serializations are refused |
|   6 | `test_bearer_credentials`                   |   ✅   | Bearer credentials                   |
|   7 | `test_rfc7515_a1_claims`                    |   ✅   | Rfc7515 a1 claims                    |
|   8 | `test_time_claims_window`                   |   ✅   | Time claims window                   |
|   9 | `test_verify_mac_at_needs_both`             |   ✅   | Verify mac at needs both             |
|  10 | `test_claim_str_escapes_and_bounds`         |   ✅   | Claim str escapes and bounds         |
|  11 | `test_scope_matches_whole_tokens`           |   ✅   | Scope matches whole tokens           |
|  12 | `test_scope_claim_then_check`               |   ✅   | Scope claim then check               |

</details>

---

## test_ld2410 - native_ld2410 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the HLK-LD2410 mmWave radar codec (server/peripherals/ld2410/ld2410.h)._

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_v102_published_report_frames`                        |   ✅   | V102 published report frames                        |
|   2 | `test_v102_frame_lengths`                                  |   ✅   | V102 frame lengths                                  |
|   3 | `test_v102_target_state_drives_presence_and_distance`      |   ✅   | V102 target state drives presence and distance      |
|   4 | `test_malformed_report_frames_are_refused`                 |   ✅   | Malformed report frames are refused                 |
|   5 | `test_stream_resyncs_past_noise_and_reports_once`          |   ✅   | Stream resyncs past noise and reports once          |
|   6 | `test_stream_handles_a_partial_header_before_the_real_one` |   ✅   | Stream handles a partial header before the real one |
|   7 | `test_stream_drops_an_absurd_length_and_recovers`          |   ✅   | Stream drops an absurd length and recovers          |
|   8 | `test_stream_drops_a_bad_frame_and_keeps_going`            |   ✅   | Stream drops a bad frame and keeps going            |
|   9 | `test_v102_published_command_frames`                       |   ✅   | V102 published command frames                       |
|  10 | `test_ld2410b_command_frames_follow_the_same_envelope`     |   ✅   | Ld2410b command frames follow the same envelope     |
|  11 | `test_command_encoders_fail_closed`                        |   ✅   | Command encoders fail closed                        |
|  12 | `test_v102_published_ack_frames`                           |   ✅   | V102 published ack frames                           |
|  13 | `test_get_mac_ack_yields_the_address`                      |   ✅   | Get mac ack yields the address                      |
|  14 | `test_malformed_ack_frames_are_refused`                    |   ✅   | Malformed ack frames are refused                    |

</details>

---

## test_ldc1614 - native_ldc1614 - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TI LDC1614 inductance-to-digital codec (server/peripherals/ldc1614/ldc1614.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_datasheet_register_map`                               |   ✅   | Datasheet register map                               |
|   2 | `test_datasheet_register_ids_and_data_layout`               |   ✅   | Datasheet register ids and data layout               |
|   3 | `test_range_sentinels_round_trip`                           |   ✅   | Range sentinels round trip                           |
|   4 | `test_equation4_sensor_frequency`                           |   ✅   | Equation4 sensor frequency                           |
|   5 | `test_build_config_writes_the_datasheet_registers_in_order` |   ✅   | Build config writes the datasheet registers in order |
|   6 | `test_build_config_honors_the_reserved_fields`              |   ✅   | Build config honors the reserved fields              |
|   7 | `test_build_config_refuses_a_short_buffer`                  |   ✅   | Build config refuses a short buffer                  |
|   8 | `test_build_config_refuses_a_null_buffer`                   |   ✅   | Build config refuses a null buffer                   |

</details>

---

## test_lfs_mock - native_lfs_mock - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_format_mounts_an_empty_volume`                            |   ✅   | Format mounts an empty volume                            |
|   2 | `test_write_then_read_round_trips`                              |   ✅   | Write then read round trips                              |
|   3 | `test_seek_reads_from_the_offset`                               |   ✅   | Seek reads from the offset                               |
|   4 | `test_directory_lists_its_children_only`                        |   ✅   | Directory lists its children only                        |
|   5 | `test_stat_tells_a_directory_from_a_file`                       |   ✅   | Stat tells a directory from a file                       |
|   6 | `test_rename_and_remove`                                        |   ✅   | Rename and remove                                        |
|   7 | `test_append_adds_to_the_end`                                   |   ✅   | Append adds to the end                                   |
|   8 | `test_open_missing_for_read_fails`                              |   ✅   | Open missing for read fails                              |
|   9 | `test_a_full_volume_refuses_rather_than_pretending`             |   ✅   | A full volume refuses rather than pretending             |
|  10 | `test_fill_volume_leaves_nothing_creatable`                     |   ✅   | Fill volume leaves nothing creatable                     |
|  11 | `test_fill_leaving_room_still_creates_but_cannot_write`         |   ✅   | Fill leaving room still creates but cannot write         |
|  12 | `test_read_one_file_while_writing_another`                      |   ✅   | Read one file while writing another                      |
|  13 | `test_two_writers_at_once`                                      |   ✅   | Two writers at once                                      |
|  14 | `test_store_still_answers_after_a_full_fill`                    |   ✅   | Store still answers after a full fill                    |
|  15 | `test_medium_error_refuses_a_write_and_leaves_the_store_usable` |   ✅   | Medium error refuses a write and leaves the store usable |

</details>

---

## test_link_manager - native_link_manager - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the multi-interface egress policy (server/signaling/link_manager.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_no_interface_up_selects_nothing`                   |   ✅   | No interface up selects nothing                   |
|   2 | `test_selection_is_total_order_over_priority_then_index` |   ✅   | Selection is total order over priority then index |
|   3 | `test_escalation_and_failover_walk_the_priority_order`   |   ✅   | Escalation and failover walk the priority order   |
|   4 | `test_changed_reports_a_moved_egress_only`               |   ✅   | Changed reports a moved egress only               |
|   5 | `test_select_does_not_move_the_active_interface`         |   ✅   | Select does not move the active interface         |
|   6 | `test_an_index_past_the_table_changes_nothing`           |   ✅   | An index past the table changes nothing           |
|   7 | `test_a_manager_with_no_table_carries_nothing`           |   ✅   | A manager with no table carries nothing           |
|   8 | `test_a_missing_manager_is_refused`                      |   ✅   | A missing manager is refused                      |

</details>

---

## test_log - native_log - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the abstract logging macros (shared/log/log.h), built at PROTOCORE_LOG_LEVEL_INFO_

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_a_discarded_call_emits_nothing`                   |   ✅   | A discarded call emits nothing                   |
|   2 | `test_a_discarded_call_does_not_evaluate_its_arguments` |   ✅   | A discarded call does not evaluate its arguments |
|   3 | `test_each_level_emits_with_its_own_severity`           |   ✅   | Each level emits with its own severity           |
|   4 | `test_the_emitted_line_is_the_built_frame`              |   ✅   | The emitted line is the built frame              |
|   5 | `test_a_null_string_field_renders_empty`                |   ✅   | A null string field renders empty                |
|   6 | `test_an_emitted_line_reaches_the_ring`                 |   ✅   | An emitted line reaches the ring                 |
|   7 | `test_the_log_and_ring_severity_scales_agree`           |   ✅   | The log and ring severity scales agree           |
|   8 | `test_clearing_the_sink`                                |   ✅   | Clearing the sink                                |
|   9 | `test_a_null_spec_emits_nothing`                        |   ✅   | A null spec emits nothing                        |
|  10 | `test_mismatched_values_yield_an_empty_line`            |   ✅   | Mismatched values yield an empty line            |
|  11 | `test_a_line_that_does_not_fit_is_emitted_empty`        |   ✅   | A line that does not fit is emitted empty        |

</details>

---

## test_log - native_log_frames - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the abstract logging macros (shared/log/log.h), built at PROTOCORE_LOG_LEVEL_INFO_

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_a_discarded_call_emits_nothing`                   |   ✅   | A discarded call emits nothing                   |
|   2 | `test_a_discarded_call_does_not_evaluate_its_arguments` |   ✅   | A discarded call does not evaluate its arguments |
|   3 | `test_each_level_emits_with_its_own_severity`           |   ✅   | Each level emits with its own severity           |
|   4 | `test_the_emitted_line_is_the_built_frame`              |   ✅   | The emitted line is the built frame              |
|   5 | `test_a_null_string_field_renders_empty`                |   ✅   | A null string field renders empty                |
|   6 | `test_an_emitted_line_reaches_the_ring`                 |   ✅   | An emitted line reaches the ring                 |
|   7 | `test_the_log_and_ring_severity_scales_agree`           |   ✅   | The log and ring severity scales agree           |
|   8 | `test_clearing_the_sink`                                |   ✅   | Clearing the sink                                |
|   9 | `test_a_null_spec_emits_nothing`                        |   ✅   | A null spec emits nothing                        |
|  10 | `test_mismatched_values_yield_an_empty_line`            |   ✅   | Mismatched values yield an empty line            |
|  11 | `test_a_line_that_does_not_fit_is_emitted_empty`        |   ✅   | A line that does not fit is emitted empty        |

</details>

---

## test_logbuf - native_logbuf - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the rotating log ring (server/core/logbuf.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_the_severity_letter_leads_the_line`          |   ✅   | The severity letter leads the line          |
|   2 | `test_the_levels_are_ordered`                      |   ✅   | The levels are ordered                      |
|   3 | `test_lines_come_back_oldest_first`                |   ✅   | Lines come back oldest first                |
|   4 | `test_the_oldest_is_pruned_on_overflow`            |   ✅   | The oldest is pruned on overflow            |
|   5 | `test_a_lookup_past_the_end_reports_nothing`       |   ✅   | A lookup past the end reports nothing       |
|   6 | `test_a_null_message_renders_the_letter_alone`     |   ✅   | A null message renders the letter alone     |
|   7 | `test_a_line_that_does_not_fit_is_empty`           |   ✅   | A line that does not fit is empty           |
|   8 | `test_dump_joins_the_held_lines_with_a_newline`    |   ✅   | Dump joins the held lines with a newline    |
|   9 | `test_dump_fails_closed_when_a_line_would_not_fit` |   ✅   | Dump fails closed when a line would not fit |
|  10 | `test_dump_of_an_empty_ring_is_an_empty_string`    |   ✅   | Dump of an empty ring is an empty string    |
|  11 | `test_dump_refuses_null_and_zero_capacity`         |   ✅   | Dump refuses null and zero capacity         |
|  12 | `test_the_trap_fires_at_or_above_its_threshold`    |   ✅   | The trap fires at or above its threshold    |
|  13 | `test_the_trap_sees_the_stored_line`               |   ✅   | The trap sees the stored line               |
|  14 | `test_the_trap_can_be_turned_off`                  |   ✅   | The trap can be turned off                  |
|  15 | `test_reset_empties_the_ring`                      |   ✅   | Reset empties the ring                      |

</details>

---

## test_lonworks - native_lonworks - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_nv_message_codes`                            |   ✅   | Nv message codes                            |
|   2 | `test_nv_pdu_layout`                               |   ✅   | Nv pdu layout                               |
|   3 | `test_selector_is_fourteen_bits`                   |   ✅   | Selector is fourteen bits                   |
|   4 | `test_snvt_temp_published_scaling`                 |   ✅   | Snvt temp published scaling                 |
|   5 | `test_snvt_temp_saturates_at_the_published_bounds` |   ✅   | Snvt temp saturates at the published bounds |
|   6 | `test_snvt_switch_published_states`                |   ✅   | Snvt switch published states                |
|   7 | `test_snvt_switch_clamps_to_the_published_range`   |   ✅   | Snvt switch clamps to the published range   |
|   8 | `test_guards`                                      |   ✅   | Guards                                      |

</details>

---

## test_lora - native_lora - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_lsv2 - native_lsv2 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Heidenhain LSV/2 telegram codec (services/machine_tool/lsv2/lsv2.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_bare_t_ok_is_eight_octets`                     |   ✅   | Bare t ok is eight octets                     |
|   2 | `test_length_prefix_counts_only_the_payload`         |   ✅   | Length prefix counts only the payload         |
|   3 | `test_login_payload_is_a_nul_terminated_group`       |   ✅   | Login payload is a nul terminated group       |
|   4 | `test_login_appends_the_password_as_a_second_string` |   ✅   | Login appends the password as a second string |
|   5 | `test_logout_with_and_without_a_group`               |   ✅   | Logout with and without a group               |
|   6 | `test_filename_command_frames_the_name`              |   ✅   | Filename command frames the name              |
|   7 | `test_run_info_selector_is_big_endian`               |   ✅   | Run info selector is big endian               |
|   8 | `test_stream_reframes_on_the_consumed_count`         |   ✅   | Stream reframes on the consumed count         |
|   9 | `test_incomplete_telegram_is_refused`                |   ✅   | Incomplete telegram is refused                |
|  10 | `test_error_payload_is_class_then_code`              |   ✅   | Error payload is class then code              |
|  11 | `test_response_mnemonics_are_discriminated`          |   ✅   | Response mnemonics are discriminated          |
|  12 | `test_builders_refuse_a_short_buffer`                |   ✅   | Builders refuse a short buffer                |

</details>

---

## test_lwm2m_tlv - native_lwm2m_tlv - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OMA LwM2M TLV codec (services/iot/lwm2m/lwm2m_tlv.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_published_device_object_entries`         |   ✅   | Published device object entries         |
|   2 | `test_published_access_control_payload`        |   ✅   | Published access control payload        |
|   3 | `test_sixteen_bit_identifier`                  |   ✅   | Sixteen bit identifier                  |
|   4 | `test_length_field_widths`                     |   ✅   | Length field widths                     |
|   5 | `test_integer_takes_the_shortest_signed_width` |   ✅   | Integer takes the shortest signed width |
|   6 | `test_boolean_is_one_octet`                    |   ✅   | Boolean is one octet                    |
|   7 | `test_float_is_binary64_in_network_byte_order` |   ✅   | Float is binary64 in network byte order |
|   8 | `test_writer_fails_closed`                     |   ✅   | Writer fails closed                     |
|   9 | `test_reader_refuses_a_truncated_entry`        |   ✅   | Reader refuses a truncated entry        |
|  10 | `test_value_integer_refuses_other_widths`      |   ✅   | Value integer refuses other widths      |
|  11 | `test_write_then_read_round_trip`              |   ✅   | Write then read round trip              |

</details>

---

## test_lwm2m_tlv - native_lwm2m_tlv_codec - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OMA LwM2M TLV codec (services/iot/lwm2m/lwm2m_tlv.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_published_device_object_entries`         |   ✅   | Published device object entries         |
|   2 | `test_published_access_control_payload`        |   ✅   | Published access control payload        |
|   3 | `test_sixteen_bit_identifier`                  |   ✅   | Sixteen bit identifier                  |
|   4 | `test_length_field_widths`                     |   ✅   | Length field widths                     |
|   5 | `test_integer_takes_the_shortest_signed_width` |   ✅   | Integer takes the shortest signed width |
|   6 | `test_boolean_is_one_octet`                    |   ✅   | Boolean is one octet                    |
|   7 | `test_float_is_binary64_in_network_byte_order` |   ✅   | Float is binary64 in network byte order |
|   8 | `test_writer_fails_closed`                     |   ✅   | Writer fails closed                     |
|   9 | `test_reader_refuses_a_truncated_entry`        |   ✅   | Reader refuses a truncated entry        |
|  10 | `test_value_integer_refuses_other_widths`      |   ✅   | Value integer refuses other widths      |
|  11 | `test_write_then_read_round_trip`              |   ✅   | Write then read round trip              |

</details>

---

## test_mbplus - native_mbplus - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Modbus Plus HDLC frame codec (services/fieldbus/mbplus/mbplus.h)._

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_published_check_value`            |   ✅   | Published check value            |
|   2 | `test_published_constants`              |   ✅   | Published constants              |
|   3 | `test_frame_layout`                     |   ✅   | Frame layout                     |
|   4 | `test_token_frame_has_no_payload`       |   ✅   | Token frame has no payload       |
|   5 | `test_round_trip`                       |   ✅   | Round trip                       |
|   6 | `test_single_bit_corruption_is_refused` |   ✅   | Single bit corruption is refused |
|   7 | `test_parse_rejects_bad_framing`        |   ✅   | Parse rejects bad framing        |
|   8 | `test_token_ring_rotation`              |   ✅   | Token ring rotation              |
|   9 | `test_build_refuses_bad_arguments`      |   ✅   | Build refuses bad arguments      |

</details>

---

## test_mbus - native_mbus - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the wired M-Bus frame + record codec (services/fieldbus/mbus/mbus.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_mbdoc_rsp_ud_example`                         |   ✅   | Mbdoc rsp ud example                         |
|   2 | `test_single_character_ack`                         |   ✅   | Single character ack                         |
|   3 | `test_snd_nke_short_frame`                          |   ✅   | Snd nke short frame                          |
|   4 | `test_req_ud_fcb_toggles_bit5`                      |   ✅   | Req ud fcb toggles bit5                      |
|   5 | `test_control_frame_is_a_long_frame_with_l_three`   |   ✅   | Control frame is a long frame with l three   |
|   6 | `test_build_long_reproduces_the_published_telegram` |   ✅   | Build long reproduces the published telegram |
|   7 | `test_parse_refuses_a_damaged_frame`                |   ✅   | Parse refuses a damaged frame                |
|   8 | `test_parse_refuses_a_truncated_frame`              |   ✅   | Parse refuses a truncated frame              |
|   9 | `test_dif_data_field_lengths`                       |   ✅   | Dif data field lengths                       |
|  10 | `test_vif_unit_table`                               |   ✅   | Vif unit table                               |
|  11 | `test_bcd_decoding_and_sign`                        |   ✅   | Bcd decoding and sign                        |
|  12 | `test_integer_decoding_is_little_endian_and_signed` |   ✅   | Integer decoding is little endian and signed |
|  13 | `test_real32_record`                                |   ✅   | Real32 record                                |
|  14 | `test_variable_length_record`                       |   ✅   | Variable length record                       |
|  15 | `test_record_walk_refuses_an_overrun`               |   ✅   | Record walk refuses an overrun               |
|  16 | `test_manufacturer_code_packing`                    |   ✅   | Manufacturer code packing                    |
|  17 | `test_var_header_bounds_and_bcd_validation`         |   ✅   | Var header bounds and bcd validation         |
|  18 | `test_builders_refuse_a_short_buffer`               |   ✅   | Builders refuse a short buffer               |

</details>

---

## test_mdns_adaptive - native_mdns_adaptive - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for adaptive mDNS beacon scheduling_

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_the_refresher_is_half_the_rfc6762_record_lifetime`   |   ✅   | The refresher is half the rfc6762 record lifetime   |
|   2 | `test_init_cannot_produce_a_ceiling_below_the_floor`       |   ✅   | Init cannot produce a ceiling below the floor       |
|   3 | `test_contention_backs_the_interval_off_to_the_ceiling`    |   ✅   | Contention backs the interval off to the ceiling    |
|   4 | `test_quiet_air_recovers_the_interval_to_the_floor`        |   ✅   | Quiet air recovers the interval to the floor        |
|   5 | `test_moderate_contention_holds_the_interval`              |   ✅   | Moderate contention holds the interval              |
|   6 | `test_due_is_wrap_safe_across_the_millis_rollover`         |   ✅   | Due is wrap safe across the millis rollover         |
|   7 | `test_a_sleep_that_would_lapse_the_record_announces_first` |   ✅   | A sleep that would lapse the record announces first |
|   8 | `test_the_sampling_window_reports_the_frames_it_counted`   |   ✅   | The sampling window reports the frames it counted   |
|   9 | `test_the_sampling_window_is_wrap_safe`                    |   ✅   | The sampling window is wrap safe                    |
|  10 | `test_null_handles_are_refused`                            |   ✅   | Null handles are refused                            |

</details>

---

## test_mdns_service - native_mdns_service - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the portable mDNS / DNS-SD responder_

|   # | Test                                                                 | Status | Description                                                   |
| --: | :------------------------------------------------------------------- | :----: | :------------------------------------------------------------ |
|   1 | `test_begin_joins_the_rfc6762_group`                                 |   ✅   | Begin joins the rfc6762 group                                 |
|   2 | `test_a_response_carries_the_rfc6762_header_bits`                    |   ✅   | A response carries the rfc6762 header bits                    |
|   3 | `test_the_cache_flush_bit_separates_unique_records_from_shared_ones` |   ✅   | The cache flush bit separates unique records from shared ones |
|   4 | `test_service_enumeration_lists_the_registered_type`                 |   ✅   | Service enumeration lists the registered type                 |
|   5 | `test_the_service_type_points_at_the_instance`                       |   ✅   | The service type points at the instance                       |
|   6 | `test_the_instance_srv_carries_the_port_and_the_target`              |   ✅   | The instance srv carries the port and the target              |
|   7 | `test_a_txt_record_is_never_zero_length`                             |   ✅   | A txt record is never zero length                             |
|   8 | `test_txt_pairs_are_length_prefixed_key_equals_value`                |   ✅   | Txt pairs are length prefixed key equals value                |
|   9 | `test_qtype_any_on_an_instance_answers_srv_and_txt`                  |   ✅   | Qtype any on an instance answers srv and txt                  |
|  10 | `test_an_added_service_is_advertised_beside_the_first`               |   ✅   | An added service is advertised beside the first               |
|  11 | `test_a_name_this_host_does_not_own_draws_silence`                   |   ✅   | A name this host does not own draws silence                   |
|  12 | `test_a_response_on_the_group_is_not_answered`                       |   ✅   | A response on the group is not answered                       |
|  13 | `test_a_malformed_query_is_dropped`                                  |   ✅   | A malformed query is dropped                                  |
|  14 | `test_the_service_table_fills_and_then_refuses`                      |   ✅   | The service table fills and then refuses                      |
|  15 | `test_a_missing_name_is_refused`                                     |   ✅   | A missing name is refused                                     |

</details>

---

## test_melsec - native_melsec - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MELSEC MC binary 3E codec (services/fieldbus/melsec/melsec.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_mitsubishi_batch_read_example`                        |   ✅   | Mitsubishi batch read example                        |
|   2 | `test_head_device_and_device_code_layout`                   |   ✅   | Head device and device code layout                   |
|   3 | `test_device_code_list`                                     |   ✅   | Device code list                                     |
|   4 | `test_request_data_length_counts_from_the_monitoring_timer` |   ✅   | Request data length counts from the monitoring timer |
|   5 | `test_batch_write_command_and_length`                       |   ✅   | Batch write command and length                       |
|   6 | `test_write_with_no_data`                                   |   ✅   | Write with no data                                   |
|   7 | `test_monitoring_timer_is_little_endian`                    |   ✅   | Monitoring timer is little endian                    |
|   8 | `test_response_subheader_is_checked`                        |   ✅   | Response subheader is checked                        |
|   9 | `test_error_end_code_response`                              |   ✅   | Error end code response                              |
|  10 | `test_response_length_field_is_validated`                   |   ✅   | Response length field is validated                   |
|  11 | `test_builders_refuse_bad_arguments`                        |   ✅   | Builders refuse bad arguments                        |

</details>

---

## test_membuild - native_membuild - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the bounded builder (mmgr/membuild.h)._

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_put_n_takes_the_length_and_not_a_terminator`            |   ✅   | Put n takes the length and not a terminator            |
|   2 | `test_lit_takes_the_array_extent`                             |   ✅   | Lit takes the array extent                             |
|   3 | `test_put_appends_a_runtime_string`                           |   ✅   | Put appends a runtime string                           |
|   4 | `test_ch_appends_one_character`                               |   ✅   | Ch appends one character                               |
|   5 | `test_an_append_is_all_or_nothing_at_the_exact_boundary`      |   ✅   | An append is all or nothing at the exact boundary      |
|   6 | `test_ok_latches_and_every_later_append_is_a_noop`            |   ✅   | Ok latches and every later append is a noop            |
|   7 | `test_zero_capacity_writes_nothing`                           |   ✅   | Zero capacity writes nothing                           |
|   8 | `test_put_clip_fills_what_fits_without_latching`              |   ✅   | Put clip fills what fits without latching              |
|   9 | `test_u64_clip_right_aligns_or_appends_nothing`               |   ✅   | U64 clip right aligns or appends nothing               |
|  10 | `test_zero_padded_widths_follow_the_printf_conversions`       |   ✅   | Zero padded widths follow the printf conversions       |
|  11 | `test_uint_renders_base_8_10_and_16`                          |   ✅   | Uint renders base 8 10 and 16                          |
|  12 | `test_the_full_integer_ranges`                                |   ✅   | The full integer ranges                                |
|  13 | `test_each_digit_count_at_its_exact_fit`                      |   ✅   | Each digit count at its exact fit                      |
|  14 | `test_xml_writes_the_predefined_entities`                     |   ✅   | Xml writes the predefined entities                     |
|  15 | `test_json_quotes_and_escapes_the_two_required_characters`    |   ✅   | Json quotes and escapes the two required characters    |
|  16 | `test_a_json_escape_that_would_straddle_the_end_fails_closed` |   ✅   | A json escape that would straddle the end fails closed |
|  17 | `test_sign_bit_reads_the_encoding_not_the_value`              |   ✅   | Sign bit reads the encoding not the value              |
|  18 | `test_is_inf_and_is_nan_split_the_all_ones_exponent`          |   ✅   | Is inf and is nan split the all ones exponent          |

</details>

---

## test_membuild - native_mmgr_membuild - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the bounded builder (mmgr/membuild.h)._

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_put_n_takes_the_length_and_not_a_terminator`            |   ✅   | Put n takes the length and not a terminator            |
|   2 | `test_lit_takes_the_array_extent`                             |   ✅   | Lit takes the array extent                             |
|   3 | `test_put_appends_a_runtime_string`                           |   ✅   | Put appends a runtime string                           |
|   4 | `test_ch_appends_one_character`                               |   ✅   | Ch appends one character                               |
|   5 | `test_an_append_is_all_or_nothing_at_the_exact_boundary`      |   ✅   | An append is all or nothing at the exact boundary      |
|   6 | `test_ok_latches_and_every_later_append_is_a_noop`            |   ✅   | Ok latches and every later append is a noop            |
|   7 | `test_zero_capacity_writes_nothing`                           |   ✅   | Zero capacity writes nothing                           |
|   8 | `test_put_clip_fills_what_fits_without_latching`              |   ✅   | Put clip fills what fits without latching              |
|   9 | `test_u64_clip_right_aligns_or_appends_nothing`               |   ✅   | U64 clip right aligns or appends nothing               |
|  10 | `test_zero_padded_widths_follow_the_printf_conversions`       |   ✅   | Zero padded widths follow the printf conversions       |
|  11 | `test_uint_renders_base_8_10_and_16`                          |   ✅   | Uint renders base 8 10 and 16                          |
|  12 | `test_the_full_integer_ranges`                                |   ✅   | The full integer ranges                                |
|  13 | `test_each_digit_count_at_its_exact_fit`                      |   ✅   | Each digit count at its exact fit                      |
|  14 | `test_xml_writes_the_predefined_entities`                     |   ✅   | Xml writes the predefined entities                     |
|  15 | `test_json_quotes_and_escapes_the_two_required_characters`    |   ✅   | Json quotes and escapes the two required characters    |
|  16 | `test_a_json_escape_that_would_straddle_the_end_fails_closed` |   ✅   | A json escape that would straddle the end fails closed |
|  17 | `test_sign_bit_reads_the_encoding_not_the_value`              |   ✅   | Sign bit reads the encoding not the value              |
|  18 | `test_is_inf_and_is_nan_split_the_all_ones_exponent`          |   ✅   | Is inf and is nan split the all ones exponent          |

</details>

---

## test_mms - native_mms - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEC 61850 MMS PDU codec (services/energy/mms/mms.h)._

|   # | Test                                                             | Status | Description                                               |
| --: | :--------------------------------------------------------------- | :----: | :-------------------------------------------------------- |
|   1 | `test_read_request_ber_nesting`                                  |   ✅   | Read request ber nesting                                  |
|   2 | `test_read_response_ber_nesting`                                 |   ✅   | Read response ber nesting                                 |
|   3 | `test_invoke_id_integer_is_minimal_and_positive`                 |   ✅   | Invoke id integer is minimal and positive                 |
|   4 | `test_long_form_length_boundary`                                 |   ✅   | Long form length boundary                                 |
|   5 | `test_parse_reports_the_pdu_header_and_borrows_the_service_body` |   ✅   | Parse reports the pdu header and borrows the service body |
|   6 | `test_parse_of_a_pdu_with_no_service_element`                    |   ✅   | Parse of a pdu with no service element                    |
|   7 | `test_parse_rejects_malformed_pdus`                              |   ✅   | Parse rejects malformed pdus                              |
|   8 | `test_build_refuses_bad_arguments_and_undersized_buffers`        |   ✅   | Build refuses bad arguments and undersized buffers        |

</details>

---

## test_modbus - native_modbus - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Modbus TCP / RTU slave core (services/fieldbus/modbus/modbus.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_map_published_pdu_examples`                    |   ✅   | Map published pdu examples                    |
|   2 | `test_unsupported_function_returns_illegal_function` |   ✅   | Unsupported function returns illegal function |
|   3 | `test_quantity_bounds`                               |   ✅   | Quantity bounds                               |
|   4 | `test_address_plus_quantity_boundary`                |   ✅   | Address plus quantity boundary                |
|   5 | `test_write_single_coil_value_is_ff00_or_0000`       |   ✅   | Write single coil value is ff00 or 0000       |
|   6 | `test_write_multiple_byte_count_must_match_quantity` |   ✅   | Write multiple byte count must match quantity |
|   7 | `test_write_multiple_quantity_limits`                |   ✅   | Write multiple quantity limits                |
|   8 | `test_mask_write_register`                           |   ✅   | Mask write register                           |
|   9 | `test_read_write_multiple_registers`                 |   ✅   | Read write multiple registers                 |
|  10 | `test_mbap_header_validation`                        |   ✅   | Mbap header validation                        |
|  11 | `test_write_callback_reports_each_write`             |   ✅   | Write callback reports each write             |
|  12 | `test_data_model_bounds_and_reset`                   |   ✅   | Data model bounds and reset                   |
|  13 | `test_rtu_frame_round_trip`                          |   ✅   | Rtu frame round trip                          |
|  14 | `test_rtu_crc_address_and_broadcast`                 |   ✅   | Rtu crc address and broadcast                 |

</details>

---

## test_modbus_master - native_modbus_master - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                     | Status | Description                       |
| --: | :--------------------------------------- | :----: | :-------------------------------- |
|   1 | `test_build_read_bytes`                  |   ✅   | Build read bytes                  |
|   2 | `test_build_rejects_bad_args`            |   ✅   | Build rejects bad args            |
|   3 | `test_round_trip_holding_regs`           |   ✅   | Round trip holding regs           |
|   4 | `test_round_trip_exception`              |   ✅   | Round trip exception              |
|   5 | `test_parse_short_frame_fails`           |   ✅   | Parse short frame fails           |
|   6 | `test_build_null_out_and_input_fc`       |   ✅   | Build null out and input fc       |
|   7 | `test_parse_null_adu`                    |   ✅   | Parse null adu                    |
|   8 | `test_parse_bad_protocol_id`             |   ✅   | Parse bad protocol id             |
|   9 | `test_parse_unexpected_function`         |   ✅   | Parse unexpected function         |
|  10 | `test_parse_exception_null_out`          |   ✅   | Parse exception null out          |
|  11 | `test_parse_bad_byte_count`              |   ✅   | Parse bad byte count              |
|  12 | `test_parse_max_regs_and_null_out`       |   ✅   | Parse max regs and null out       |
|  13 | `test_parse_accepts_input_regs_function` |   ✅   | Parse accepts input regs function |
|  14 | `test_build_write_single_bytes`          |   ✅   | Build write single bytes          |
|  15 | `test_round_trip_write_single`           |   ✅   | Round trip write single           |
|  16 | `test_build_write_multiple_bytes`        |   ✅   | Build write multiple bytes        |
|  17 | `test_round_trip_write_multiple`         |   ✅   | Round trip write multiple         |
|  18 | `test_build_write_rejects_bad_args`      |   ✅   | Build write rejects bad args      |
|  19 | `test_parse_write_response_edges`        |   ✅   | Parse write response edges        |
|  20 | `test_round_trip_read_coils`             |   ✅   | Round trip read coils             |
|  21 | `test_round_trip_read_discrete_inputs`   |   ✅   | Round trip read discrete inputs   |
|  22 | `test_round_trip_write_single_coil`      |   ✅   | Round trip write single coil      |
|  23 | `test_round_trip_write_multiple_coils`   |   ✅   | Round trip write multiple coils   |
|  24 | `test_bit_build_and_parse_guards`        |   ✅   | Bit build and parse guards        |
|  25 | `test_round_trip_mask_write`             |   ✅   | Round trip mask write             |
|  26 | `test_round_trip_read_write_multiple`    |   ✅   | Round trip read write multiple    |
|  27 | `test_fc16_17_guards`                    |   ✅   | Fc16 17 guards                    |

</details>

---

## test_mpr121 - native_mpr121 - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NXP MPR121 capacitive-touch codec (server/peripherals/mpr121/mpr121.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_datasheet_status_register_bit_positions`      |   ✅   | Datasheet status register bit positions      |
|   2 | `test_proximity_and_overcurrent_flags`              |   ✅   | Proximity and overcurrent flags              |
|   3 | `test_is_touched_is_bounded_to_twelve_electrodes`   |   ✅   | Is touched is bounded to twelve electrodes   |
|   4 | `test_filtered_data_is_ten_bits`                    |   ✅   | Filtered data is ten bits                    |
|   5 | `test_build_init_writes_the_datasheet_registers`    |   ✅   | Build init writes the datasheet registers    |
|   6 | `test_ecr_encodes_the_datasheet_fields`             |   ✅   | Ecr encodes the datasheet fields             |
|   7 | `test_build_init_length_tracks_the_electrode_count` |   ✅   | Build init length tracks the electrode count |
|   8 | `test_build_init_refuses_bad_arguments`             |   ✅   | Build init refuses bad arguments             |

</details>

---

## test_mqtt - native_mqtt - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MQTT Control Packet codec (services/iot/mqtt/mqtt.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_table_2_4_remaining_length_boundaries`   |   ✅   | Table 2 4 remaining length boundaries   |
|   2 | `test_remaining_length_worked_examples`        |   ✅   | Remaining length worked examples        |
|   3 | `test_remaining_length_bounds`                 |   ✅   | Remaining length bounds                 |
|   4 | `test_connect_matches_figure_3_6`              |   ✅   | Connect matches figure 3 6              |
|   5 | `test_connect_flags_follow_the_fields_present` |   ✅   | Connect flags follow the fields present |
|   6 | `test_publish_matches_figure_3_11`             |   ✅   | Publish matches figure 3 11             |
|   7 | `test_publish_fixed_header_flags`              |   ✅   | Publish fixed header flags              |
|   8 | `test_publish_refuses_wildcards`               |   ✅   | Publish refuses wildcards               |
|   9 | `test_subscribe_matches_figure_3_23`           |   ✅   | Subscribe matches figure 3 23           |
|  10 | `test_unsubscribe_reserved_flags`              |   ✅   | Unsubscribe reserved flags              |
|  11 | `test_ack_packets_are_four_octets`             |   ✅   | Ack packets are four octets             |
|  12 | `test_pingreq_and_disconnect_are_two_octets`   |   ✅   | Pingreq and disconnect are two octets   |
|  13 | `test_parse_fixed_header`                      |   ✅   | Parse fixed header                      |
|  14 | `test_parse_publish_round_trip`                |   ✅   | Parse publish round trip                |
|  15 | `test_parse_publish_refuses_a_malformed_body`  |   ✅   | Parse publish refuses a malformed body  |
|  16 | `test_parse_connack_table_3_1`                 |   ✅   | Parse connack table 3 1                 |
|  17 | `test_parse_suback_return_codes`               |   ✅   | Parse suback return codes               |
|  18 | `test_parse_ack_packet_identifier`             |   ✅   | Parse ack packet identifier             |
|  19 | `test_builds_refuse_short_buffers`             |   ✅   | Builds refuse short buffers             |

</details>

---

## test_mqtt - native_mqtt_codec - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MQTT Control Packet codec (services/iot/mqtt/mqtt.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_table_2_4_remaining_length_boundaries`   |   ✅   | Table 2 4 remaining length boundaries   |
|   2 | `test_remaining_length_worked_examples`        |   ✅   | Remaining length worked examples        |
|   3 | `test_remaining_length_bounds`                 |   ✅   | Remaining length bounds                 |
|   4 | `test_connect_matches_figure_3_6`              |   ✅   | Connect matches figure 3 6              |
|   5 | `test_connect_flags_follow_the_fields_present` |   ✅   | Connect flags follow the fields present |
|   6 | `test_publish_matches_figure_3_11`             |   ✅   | Publish matches figure 3 11             |
|   7 | `test_publish_fixed_header_flags`              |   ✅   | Publish fixed header flags              |
|   8 | `test_publish_refuses_wildcards`               |   ✅   | Publish refuses wildcards               |
|   9 | `test_subscribe_matches_figure_3_23`           |   ✅   | Subscribe matches figure 3 23           |
|  10 | `test_unsubscribe_reserved_flags`              |   ✅   | Unsubscribe reserved flags              |
|  11 | `test_ack_packets_are_four_octets`             |   ✅   | Ack packets are four octets             |
|  12 | `test_pingreq_and_disconnect_are_two_octets`   |   ✅   | Pingreq and disconnect are two octets   |
|  13 | `test_parse_fixed_header`                      |   ✅   | Parse fixed header                      |
|  14 | `test_parse_publish_round_trip`                |   ✅   | Parse publish round trip                |
|  15 | `test_parse_publish_refuses_a_malformed_body`  |   ✅   | Parse publish refuses a malformed body  |
|  16 | `test_parse_connack_table_3_1`                 |   ✅   | Parse connack table 3 1                 |
|  17 | `test_parse_suback_return_codes`               |   ✅   | Parse suback return codes               |
|  18 | `test_parse_ack_packet_identifier`             |   ✅   | Parse ack packet identifier             |
|  19 | `test_builds_refuse_short_buffers`             |   ✅   | Builds refuse short buffers             |

</details>

---

## test_mqtt_sn - native_mqtt_sn - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MQTT-SN wire codec (services/iot/mqtt/mqtt_sn.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_length_field_switches_at_255`                |   ✅   | Length field switches at 255                |
|   2 | `test_flags_octet_bit_positions`                   |   ✅   | Flags octet bit positions                   |
|   3 | `test_connect_variable_part`                       |   ✅   | Connect variable part                       |
|   4 | `test_register_and_regack`                         |   ✅   | Register and regack                         |
|   5 | `test_publish_and_puback`                          |   ✅   | Publish and puback                          |
|   6 | `test_subscribe_by_name_and_by_id`                 |   ✅   | Subscribe by name and by id                 |
|   7 | `test_pingreq_and_disconnect_optional_fields`      |   ✅   | Pingreq and disconnect optional fields      |
|   8 | `test_table_3_msgtype_values`                      |   ✅   | Table 3 msgtype values                      |
|   9 | `test_header_parse_refuses_an_inconsistent_length` |   ✅   | Header parse refuses an inconsistent length |
|  10 | `test_typed_parsers_refuse_a_short_variable_part`  |   ✅   | Typed parsers refuse a short variable part  |
|  11 | `test_builders_fail_closed`                        |   ✅   | Builders fail closed                        |

</details>

---

## test_mqtt_sn - native_mqtt_sn_codec - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MQTT-SN wire codec (services/iot/mqtt/mqtt_sn.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_length_field_switches_at_255`                |   ✅   | Length field switches at 255                |
|   2 | `test_flags_octet_bit_positions`                   |   ✅   | Flags octet bit positions                   |
|   3 | `test_connect_variable_part`                       |   ✅   | Connect variable part                       |
|   4 | `test_register_and_regack`                         |   ✅   | Register and regack                         |
|   5 | `test_publish_and_puback`                          |   ✅   | Publish and puback                          |
|   6 | `test_subscribe_by_name_and_by_id`                 |   ✅   | Subscribe by name and by id                 |
|   7 | `test_pingreq_and_disconnect_optional_fields`      |   ✅   | Pingreq and disconnect optional fields      |
|   8 | `test_table_3_msgtype_values`                      |   ✅   | Table 3 msgtype values                      |
|   9 | `test_header_parse_refuses_an_inconsistent_length` |   ✅   | Header parse refuses an inconsistent length |
|  10 | `test_typed_parsers_refuse_a_short_variable_part`  |   ✅   | Typed parsers refuse a short variable part  |
|  11 | `test_builders_fail_closed`                        |   ✅   | Builders fail closed                        |

</details>

---

## test_msgpack - native_msgpack - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MessagePack codec (network_drivers/presentation/codec/msgpack/msgpack.h)._

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_spec_first_byte_table`              |   ✅   | Spec first byte table              |
|   2 | `test_spec_float32`                       |   ✅   | Spec float32                       |
|   3 | `test_label_is_the_numeric_spelling`      |   ✅   | Label is the numeric spelling      |
|   4 | `test_peek_maps_first_byte_to_type`       |   ✅   | Peek maps first byte to type       |
|   5 | `test_never_used_and_ext_are_invalid`     |   ✅   | Never used and ext are invalid     |
|   6 | `test_decode_int_widths`                  |   ✅   | Decode int widths                  |
|   7 | `test_decode_str_and_bin_alias_the_input` |   ✅   | Decode str and bin alias the input |
|   8 | `test_str_and_bin_families_do_not_cross`  |   ✅   | Str and bin families do not cross  |
|   9 | `test_decode_nil_bool_float`              |   ✅   | Decode nil bool float              |
|  10 | `test_map_round_trip`                     |   ✅   | Map round trip                     |
|  11 | `test_array_16_round_trip`                |   ✅   | Array 16 round trip                |
|  12 | `test_truncated_input_fails_closed`       |   ✅   | Truncated input fails closed       |
|  13 | `test_error_is_sticky`                    |   ✅   | Error is sticky                    |
|  14 | `test_overflow_reports_the_size_needed`   |   ✅   | Overflow reports the size needed   |
|  15 | `test_null_string_is_empty`               |   ✅   | Null string is empty               |

</details>

---

## test_msgpack - native_msgpack_wire - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MessagePack codec (network_drivers/presentation/codec/msgpack/msgpack.h)._

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_spec_first_byte_table`              |   ✅   | Spec first byte table              |
|   2 | `test_spec_float32`                       |   ✅   | Spec float32                       |
|   3 | `test_label_is_the_numeric_spelling`      |   ✅   | Label is the numeric spelling      |
|   4 | `test_peek_maps_first_byte_to_type`       |   ✅   | Peek maps first byte to type       |
|   5 | `test_never_used_and_ext_are_invalid`     |   ✅   | Never used and ext are invalid     |
|   6 | `test_decode_int_widths`                  |   ✅   | Decode int widths                  |
|   7 | `test_decode_str_and_bin_alias_the_input` |   ✅   | Decode str and bin alias the input |
|   8 | `test_str_and_bin_families_do_not_cross`  |   ✅   | Str and bin families do not cross  |
|   9 | `test_decode_nil_bool_float`              |   ✅   | Decode nil bool float              |
|  10 | `test_map_round_trip`                     |   ✅   | Map round trip                     |
|  11 | `test_array_16_round_trip`                |   ✅   | Array 16 round trip                |
|  12 | `test_truncated_input_fails_closed`       |   ✅   | Truncated input fails closed       |
|  13 | `test_error_is_sticky`                    |   ✅   | Error is sticky                    |
|  14 | `test_overflow_reports_the_size_needed`   |   ✅   | Overflow reports the size needed   |
|  15 | `test_null_string_is_empty`               |   ✅   | Null string is empty               |

</details>

---

## test_mtconnect - native_mtconnect - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_streams_document_skeleton`                      |   ✅   | Streams document skeleton                      |
|   2 | `test_sample_and_event_wrappers`                      |   ✅   | Sample and event wrappers                      |
|   3 | `test_condition_value_becomes_the_sub_element`        |   ✅   | Condition value becomes the sub element        |
|   4 | `test_xml_special_characters_are_escaped`             |   ✅   | Xml special characters are escaped             |
|   5 | `test_error_document`                                 |   ✅   | Error document                                 |
|   6 | `test_devices_probe_document`                         |   ✅   | Devices probe document                         |
|   7 | `test_assets_cutting_tool_document`                   |   ✅   | Assets cutting tool document                   |
|   8 | `test_overflow_reports_zero_length`                   |   ✅   | Overflow reports zero length                   |
|   9 | `test_sample_buffer_assigns_monotonic_sequences`      |   ✅   | Sample buffer assigns monotonic sequences      |
|  10 | `test_sample_buffer_eviction_advances_first_sequence` |   ✅   | Sample buffer eviction advances first sequence |
|  11 | `test_sample_query_window_and_next_sequence`          |   ✅   | Sample query window and next sequence          |
|  12 | `test_sample_query_clamps_a_stale_from`               |   ✅   | Sample query clamps a stale from               |
|  13 | `test_sample_query_past_the_newest_is_empty`          |   ✅   | Sample query past the newest is empty          |

</details>

---

## test_nats - native_nats - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NATS client protocol codec (services/iot/nats/nats.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_published_pub_examples`                 |   ✅   | Published pub examples                 |
|   2 | `test_published_hpub_examples`                |   ✅   | Published hpub examples                |
|   3 | `test_published_sub_examples`                 |   ✅   | Published sub examples                 |
|   4 | `test_published_unsub_examples`               |   ✅   | Published unsub examples               |
|   5 | `test_ping_pong_and_connect`                  |   ✅   | Ping pong and connect                  |
|   6 | `test_published_msg_examples`                 |   ✅   | Published msg examples                 |
|   7 | `test_published_hmsg_example`                 |   ✅   | Published hmsg example                 |
|   8 | `test_control_line_only_operations`           |   ✅   | Control line only operations           |
|   9 | `test_repeated_whitespace_is_one_delimiter`   |   ✅   | Repeated whitespace is one delimiter   |
|  10 | `test_parse_waits_for_the_whole_operation`    |   ✅   | Parse waits for the whole operation    |
|  11 | `test_malformed_control_lines_are_refused`    |   ✅   | Malformed control lines are refused    |
|  12 | `test_builders_fail_closed`                   |   ✅   | Builders fail closed                   |
|  13 | `test_byte_counts_render_and_read_as_decimal` |   ✅   | Byte counts render and read as decimal |

</details>

---

## test_nats - native_nats_proto - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NATS client protocol codec (services/iot/nats/nats.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_published_pub_examples`                 |   ✅   | Published pub examples                 |
|   2 | `test_published_hpub_examples`                |   ✅   | Published hpub examples                |
|   3 | `test_published_sub_examples`                 |   ✅   | Published sub examples                 |
|   4 | `test_published_unsub_examples`               |   ✅   | Published unsub examples               |
|   5 | `test_ping_pong_and_connect`                  |   ✅   | Ping pong and connect                  |
|   6 | `test_published_msg_examples`                 |   ✅   | Published msg examples                 |
|   7 | `test_published_hmsg_example`                 |   ✅   | Published hmsg example                 |
|   8 | `test_control_line_only_operations`           |   ✅   | Control line only operations           |
|   9 | `test_repeated_whitespace_is_one_delimiter`   |   ✅   | Repeated whitespace is one delimiter   |
|  10 | `test_parse_waits_for_the_whole_operation`    |   ✅   | Parse waits for the whole operation    |
|  11 | `test_malformed_control_lines_are_refused`    |   ✅   | Malformed control lines are refused    |
|  12 | `test_builders_fail_closed`                   |   ✅   | Builders fail closed                   |
|  13 | `test_byte_counts_render_and_read_as_decimal` |   ✅   | Byte counts render and read as decimal |

</details>

---

## test_nema_ts2 - native_nema_ts2 - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NEMA TS 2 traffic-cabinet SDLC frame codec_

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_x25_check_value_frames_the_fcs`     |   ✅   | X25 check value frames the fcs     |
|   2 | `test_hdlc_good_fcs_residue`              |   ✅   | Hdlc good fcs residue              |
|   3 | `test_build_parse_round_trip`             |   ✅   | Build parse round trip             |
|   4 | `test_parse_refuses_any_single_bit_error` |   ✅   | Parse refuses any single bit error |
|   5 | `test_parse_refuses_short_frames`         |   ✅   | Parse refuses short frames         |
|   6 | `test_zero_length_data_frame`             |   ✅   | Zero length data frame             |
|   7 | `test_build_bounds`                       |   ✅   | Build bounds                       |
|   8 | `test_crc_of_an_empty_span`               |   ✅   | Crc of an empty span               |
|   9 | `test_frame_type_response_offset`         |   ✅   | Frame type response offset         |

</details>

---

## test_nema_ts2 - native_nema_ts2_sdlc - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NEMA TS 2 traffic-cabinet SDLC frame codec_

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_x25_check_value_frames_the_fcs`     |   ✅   | X25 check value frames the fcs     |
|   2 | `test_hdlc_good_fcs_residue`              |   ✅   | Hdlc good fcs residue              |
|   3 | `test_build_parse_round_trip`             |   ✅   | Build parse round trip             |
|   4 | `test_parse_refuses_any_single_bit_error` |   ✅   | Parse refuses any single bit error |
|   5 | `test_parse_refuses_short_frames`         |   ✅   | Parse refuses short frames         |
|   6 | `test_zero_length_data_frame`             |   ✅   | Zero length data frame             |
|   7 | `test_build_bounds`                       |   ✅   | Build bounds                       |
|   8 | `test_crc_of_an_empty_span`               |   ✅   | Crc of an empty span               |
|   9 | `test_frame_type_response_offset`         |   ✅   | Frame type response offset         |

</details>

---

## test_net_egress - native_net_egress - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for egress-interface reporting with no L1 backend_

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_classify_maps_the_live_route`                       |   ✅   | Classify maps the live route                       |
|   2 | `test_a_down_interface_never_claims_the_route`            |   ✅   | A down interface never claims the route            |
|   3 | `test_station_is_compared_before_the_softap`              |   ✅   | Station is compared before the softap              |
|   4 | `test_no_backend_reports_no_route`                        |   ✅   | No backend reports no route                        |
|   5 | `test_no_backend_has_no_wired_link`                       |   ✅   | No backend has no wired link                       |
|   6 | `test_no_backend_wifi_bring_up_is_a_no_op`                |   ✅   | No backend wifi bring up is a no op                |
|   7 | `test_no_backend_has_no_ipv6`                             |   ✅   | No backend has no ipv6                             |
|   8 | `test_no_backend_readouts_are_empty`                      |   ✅   | No backend readouts are empty                      |
|   9 | `test_no_backend_mac_readouts_leave_the_buffer_untouched` |   ✅   | No backend mac readouts leave the buffer untouched |
|  10 | `test_no_backend_radio_control_refuses`                   |   ✅   | No backend radio control refuses                   |
|  11 | `test_layer_handle_is_bound`                              |   ✅   | Layer handle is bound                              |

</details>

---

## test_net_egress - native_l1_egress - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for egress-interface reporting with no L1 backend_

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_classify_maps_the_live_route`                       |   ✅   | Classify maps the live route                       |
|   2 | `test_a_down_interface_never_claims_the_route`            |   ✅   | A down interface never claims the route            |
|   3 | `test_station_is_compared_before_the_softap`              |   ✅   | Station is compared before the softap              |
|   4 | `test_no_backend_reports_no_route`                        |   ✅   | No backend reports no route                        |
|   5 | `test_no_backend_has_no_wired_link`                       |   ✅   | No backend has no wired link                       |
|   6 | `test_no_backend_wifi_bring_up_is_a_no_op`                |   ✅   | No backend wifi bring up is a no op                |
|   7 | `test_no_backend_has_no_ipv6`                             |   ✅   | No backend has no ipv6                             |
|   8 | `test_no_backend_readouts_are_empty`                      |   ✅   | No backend readouts are empty                      |
|   9 | `test_no_backend_mac_readouts_leave_the_buffer_untouched` |   ✅   | No backend mac readouts leave the buffer untouched |
|  10 | `test_no_backend_radio_control_refuses`                   |   ✅   | No backend radio control refuses                   |
|  11 | `test_layer_handle_is_bound`                              |   ✅   | Layer handle is bound                              |

</details>

---

## test_netadapt - native_netadapt - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the network adaptation decisions (server/net/netadapt/netadapt.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_window_floor_at_and_below_the_reserve`         |   ✅   | Window floor at and below the reserve         |
|   2 | `test_window_is_a_quarter_of_the_spare_heap`         |   ✅   | Window is a quarter of the spare heap         |
|   3 | `test_window_clamps_to_the_ceiling`                  |   ✅   | Window clamps to the ceiling                  |
|   4 | `test_window_inverted_bounds_yield_the_floor`        |   ✅   | Window inverted bounds yield the floor        |
|   5 | `test_window_stays_inside_the_stated_bounds`         |   ✅   | Window stays inside the stated bounds         |
|   6 | `test_window_never_shrinks_as_the_heap_grows`        |   ✅   | Window never shrinks as the heap grows        |
|   7 | `test_dhcp_fallback_timeout_boundary`                |   ✅   | Dhcp fallback timeout boundary                |
|   8 | `test_dhcp_fallback_attempt_budget`                  |   ✅   | Dhcp fallback attempt budget                  |
|   9 | `test_dhcp_fallback_latches_as_the_counters_advance` |   ✅   | Dhcp fallback latches as the counters advance |

</details>

---

## test_nmea0183 - native_nmea0183 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NMEA 0183 sentence codec (services/timing_position/nmea0183/nmea0183.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_published_sentence_checksums`           |   ✅   | Published sentence checksums           |
|   2 | `test_checksum_is_the_xor_of_the_body`        |   ✅   | Checksum is the xor of the body        |
|   3 | `test_build_frames_the_body`                  |   ✅   | Build frames the body                  |
|   4 | `test_address_splits_into_talker_and_type`    |   ✅   | Address splits into talker and type    |
|   5 | `test_empty_fields_are_counted`               |   ✅   | Empty fields are counted               |
|   6 | `test_framing_is_enforced`                    |   ✅   | Framing is enforced                    |
|   7 | `test_gga_decodes_the_published_fix`          |   ✅   | Gga decodes the published fix          |
|   8 | `test_rmc_decodes_the_published_fix`          |   ✅   | Rmc decodes the published fix          |
|   9 | `test_gsv_decodes_the_published_sky_view`     |   ✅   | Gsv decodes the published sky view     |
|  10 | `test_zda_decodes_the_published_time`         |   ✅   | Zda decodes the published time         |
|  11 | `test_vtg_decodes_the_published_course`       |   ✅   | Vtg decodes the published course       |
|  12 | `test_gsa_decodes_the_published_fix_set`      |   ✅   | Gsa decodes the published fix set      |
|  13 | `test_gll_decodes_the_published_position`     |   ✅   | Gll decodes the published position     |
|  14 | `test_dpt_decodes_the_published_depth`        |   ✅   | Dpt decodes the published depth        |
|  15 | `test_instrument_sentences_round_trip`        |   ✅   | Instrument sentences round trip        |
|  16 | `test_typed_decoders_check_the_sentence_type` |   ✅   | Typed decoders check the sentence type |
|  17 | `test_hemisphere_letters_set_the_sign`        |   ✅   | Hemisphere letters set the sign        |

</details>

---

## test_nmea0183 - native_gnss_nmea0183 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NMEA 0183 sentence codec (services/timing_position/nmea0183/nmea0183.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_published_sentence_checksums`           |   ✅   | Published sentence checksums           |
|   2 | `test_checksum_is_the_xor_of_the_body`        |   ✅   | Checksum is the xor of the body        |
|   3 | `test_build_frames_the_body`                  |   ✅   | Build frames the body                  |
|   4 | `test_address_splits_into_talker_and_type`    |   ✅   | Address splits into talker and type    |
|   5 | `test_empty_fields_are_counted`               |   ✅   | Empty fields are counted               |
|   6 | `test_framing_is_enforced`                    |   ✅   | Framing is enforced                    |
|   7 | `test_gga_decodes_the_published_fix`          |   ✅   | Gga decodes the published fix          |
|   8 | `test_rmc_decodes_the_published_fix`          |   ✅   | Rmc decodes the published fix          |
|   9 | `test_gsv_decodes_the_published_sky_view`     |   ✅   | Gsv decodes the published sky view     |
|  10 | `test_zda_decodes_the_published_time`         |   ✅   | Zda decodes the published time         |
|  11 | `test_vtg_decodes_the_published_course`       |   ✅   | Vtg decodes the published course       |
|  12 | `test_gsa_decodes_the_published_fix_set`      |   ✅   | Gsa decodes the published fix set      |
|  13 | `test_gll_decodes_the_published_position`     |   ✅   | Gll decodes the published position     |
|  14 | `test_dpt_decodes_the_published_depth`        |   ✅   | Dpt decodes the published depth        |
|  15 | `test_instrument_sentences_round_trip`        |   ✅   | Instrument sentences round trip        |
|  16 | `test_typed_decoders_check_the_sentence_type` |   ✅   | Typed decoders check the sentence type |
|  17 | `test_hemisphere_letters_set_the_sign`        |   ✅   | Hemisphere letters set the sign        |

</details>

---

## test_nmea2000 - native_nmea2000 - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NMEA 2000 codec (services/timing_position/nmea2000/nmea2000.h)._

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_fastpacket_frame_count`                             |   ✅   | Fastpacket frame count                             |
|   2 | `test_fastpacket_split_and_reassemble`                    |   ✅   | Fastpacket split and reassemble                    |
|   3 | `test_fastpacket_rejects_out_of_order_and_foreign_frames` |   ✅   | Fastpacket rejects out of order and foreign frames |
|   4 | `test_fastpacket_bounds`                                  |   ✅   | Fastpacket bounds                                  |
|   5 | `test_single_frame_message`                               |   ✅   | Single frame message                               |
|   6 | `test_position_rapid_update`                              |   ✅   | Position rapid update                              |
|   7 | `test_cog_sog_rapid_update`                               |   ✅   | Cog sog rapid update                               |
|   8 | `test_engine_rapid_update`                                |   ✅   | Engine rapid update                                |
|   9 | `test_wind_data`                                          |   ✅   | Wind data                                          |
|  10 | `test_water_depth`                                        |   ✅   | Water depth                                        |
|  11 | `test_vessel_heading`                                     |   ✅   | Vessel heading                                     |
|  12 | `test_temperature_converts_kelvin_to_celsius`             |   ✅   | Temperature converts kelvin to celsius             |
|  13 | `test_attitude_angles_are_signed`                         |   ✅   | Attitude angles are signed                         |
|  14 | `test_battery_status`                                     |   ✅   | Battery status                                     |
|  15 | `test_engine_dynamic_rides_the_fast_packet`               |   ✅   | Engine dynamic rides the fast packet               |
|  16 | `test_short_payloads_are_refused`                         |   ✅   | Short payloads are refused                         |

</details>

---

## test_nmea2000 - native_marine_nmea2000 - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NMEA 2000 codec (services/timing_position/nmea2000/nmea2000.h)._

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_fastpacket_frame_count`                             |   ✅   | Fastpacket frame count                             |
|   2 | `test_fastpacket_split_and_reassemble`                    |   ✅   | Fastpacket split and reassemble                    |
|   3 | `test_fastpacket_rejects_out_of_order_and_foreign_frames` |   ✅   | Fastpacket rejects out of order and foreign frames |
|   4 | `test_fastpacket_bounds`                                  |   ✅   | Fastpacket bounds                                  |
|   5 | `test_single_frame_message`                               |   ✅   | Single frame message                               |
|   6 | `test_position_rapid_update`                              |   ✅   | Position rapid update                              |
|   7 | `test_cog_sog_rapid_update`                               |   ✅   | Cog sog rapid update                               |
|   8 | `test_engine_rapid_update`                                |   ✅   | Engine rapid update                                |
|   9 | `test_wind_data`                                          |   ✅   | Wind data                                          |
|  10 | `test_water_depth`                                        |   ✅   | Water depth                                        |
|  11 | `test_vessel_heading`                                     |   ✅   | Vessel heading                                     |
|  12 | `test_temperature_converts_kelvin_to_celsius`             |   ✅   | Temperature converts kelvin to celsius             |
|  13 | `test_attitude_angles_are_signed`                         |   ✅   | Attitude angles are signed                         |
|  14 | `test_battery_status`                                     |   ✅   | Battery status                                     |
|  15 | `test_engine_dynamic_rides_the_fast_packet`               |   ✅   | Engine dynamic rides the fast packet               |
|  16 | `test_short_payloads_are_refused`                         |   ✅   | Short payloads are refused                         |

</details>

---

## test_nrf24 - native_nrf24 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_ntcip - native_ntcip - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NTCIP transportation-device object identifiers_

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_roots_sit_under_the_nema_enterprise_arc`   |   ✅   | Roots sit under the nema enterprise arc   |
|   2 | `test_device_class_arc_separates_1202_from_1203` |   ✅   | Device class arc separates 1202 from 1203 |
|   3 | `test_every_root_is_distinct`                    |   ✅   | Every root is distinct                    |
|   4 | `test_oid_builder_appends_the_instance`          |   ✅   | Oid builder appends the instance          |
|   5 | `test_oid_builder_scalar_takes_zero`             |   ✅   | Oid builder scalar takes zero             |
|   6 | `test_oid_builder_refuses_a_short_buffer`        |   ✅   | Oid builder refuses a short buffer        |
|   7 | `test_oid_builder_null_guards`                   |   ✅   | Oid builder null guards                   |
|   8 | `test_lengths_are_self_consistent`               |   ✅   | Lengths are self consistent               |

</details>

---

## test_ntcip - native_ntcip_oid - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NTCIP transportation-device object identifiers_

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_roots_sit_under_the_nema_enterprise_arc`   |   ✅   | Roots sit under the nema enterprise arc   |
|   2 | `test_device_class_arc_separates_1202_from_1203` |   ✅   | Device class arc separates 1202 from 1203 |
|   3 | `test_every_root_is_distinct`                    |   ✅   | Every root is distinct                    |
|   4 | `test_oid_builder_appends_the_instance`          |   ✅   | Oid builder appends the instance          |
|   5 | `test_oid_builder_scalar_takes_zero`             |   ✅   | Oid builder scalar takes zero             |
|   6 | `test_oid_builder_refuses_a_short_buffer`        |   ✅   | Oid builder refuses a short buffer        |
|   7 | `test_oid_builder_null_guards`                   |   ✅   | Oid builder null guards                   |
|   8 | `test_lengths_are_self_consistent`               |   ✅   | Lengths are self consistent               |

</details>

---

## test_ntp_server - native_ntp_server - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                         | Status | Description                                           |
| --: | :----------------------------------------------------------- | :----: | :---------------------------------------------------- |
|   1 | `test_rfc4330_reply_field_table`                             |   ✅   | Rfc4330 reply field table                             |
|   2 | `test_the_version_is_echoed_and_the_mode_is_always_server`   |   ✅   | The version is echoed and the mode is always server   |
|   3 | `test_a_zero_poll_field_is_answered_with_the_servers_own`    |   ✅   | A zero poll field is answered with the servers own    |
|   4 | `test_the_clock_quality_fields_describe_a_millisecond_clock` |   ✅   | The clock quality fields describe a millisecond clock |
|   5 | `test_a_packet_short_of_48_octets_is_refused`                |   ✅   | A packet short of 48 octets is refused                |
|   6 | `test_begin_binds_udp_123`                                   |   ✅   | Begin binds udp 123                                   |
|   7 | `test_a_request_is_answered_on_the_wire`                     |   ✅   | A request is answered on the wire                     |
|   8 | `test_a_server_with_no_clock_answers_nothing`                |   ✅   | A server with no clock answers nothing                |
|   9 | `test_a_runt_datagram_is_dropped`                            |   ✅   | A runt datagram is dropped                            |

</details>

---

## test_ntp_service - native_ntp_service - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SNTP client (network_drivers/application/ntp_service/ntp_service.h), over the_

|   # | Test                                                             | Status | Description                                               |
| --: | :--------------------------------------------------------------- | :----: | :-------------------------------------------------------- |
|   1 | `test_the_request_is_an_rfc4330_client_packet`                   |   ✅   | The request is an rfc4330 client packet                   |
|   2 | `test_a_matching_reply_sets_the_clock`                           |   ✅   | A matching reply sets the clock                           |
|   3 | `test_a_reply_whose_origin_does_not_echo_the_request_is_ignored` |   ✅   | A reply whose origin does not echo the request is ignored |
|   4 | `test_a_packet_that_is_not_a_server_reply_is_ignored`            |   ✅   | A packet that is not a server reply is ignored            |
|   5 | `test_a_kiss_o_death_or_unsynchronized_stratum_is_ignored`       |   ✅   | A kiss o death or unsynchronized stratum is ignored       |
|   6 | `test_an_implausible_clock_is_ignored`                           |   ✅   | An implausible clock is ignored                           |
|   7 | `test_a_reply_short_of_48_octets_is_ignored`                     |   ✅   | A reply short of 48 octets is ignored                     |
|   8 | `test_the_epoch_advances_off_the_monotonic_clock`                |   ✅   | The epoch advances off the monotonic clock                |
|   9 | `test_a_server_name_is_refused`                                  |   ✅   | A server name is refused                                  |
|  10 | `test_the_http_date_is_empty_until_synced`                       |   ✅   | The http date is empty until synced                       |

</details>

---

## test_ntrip_caster - native_ntrip_caster - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_rfc9112_header_block_terminates_the_request` |   ✅   | Rfc9112 header block terminates the request |
|   2 | `test_rfc7617_basic_credentials`                   |   ✅   | Rfc7617 basic credentials                   |
|   3 | `test_header_names_are_case_insensitive`           |   ✅   | Header names are case insensitive           |
|   4 | `test_version_comes_from_the_ntrip_version_header` |   ✅   | Version comes from the ntrip version header |
|   5 | `test_root_request_asks_for_the_source_table`      |   ✅   | Root request asks for the source table      |
|   6 | `test_non_get_and_oversized_mountpoints`           |   ✅   | Non get and oversized mountpoints           |
|   7 | `test_stream_response_forms`                       |   ✅   | Stream response forms                       |
|   8 | `test_error_and_unauthorized_responses`            |   ✅   | Error and unauthorized responses            |
|   9 | `test_str_record_field_positions`                  |   ✅   | Str record field positions                  |
|  10 | `test_str_record_defaults`                         |   ✅   | Str record defaults                         |
|  11 | `test_sourcetable_body_is_self_consistent`         |   ✅   | Sourcetable body is self consistent         |

</details>

---

## test_ntrip_caster - native_gnss_ntrip_caster - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_rfc9112_header_block_terminates_the_request` |   ✅   | Rfc9112 header block terminates the request |
|   2 | `test_rfc7617_basic_credentials`                   |   ✅   | Rfc7617 basic credentials                   |
|   3 | `test_header_names_are_case_insensitive`           |   ✅   | Header names are case insensitive           |
|   4 | `test_version_comes_from_the_ntrip_version_header` |   ✅   | Version comes from the ntrip version header |
|   5 | `test_root_request_asks_for_the_source_table`      |   ✅   | Root request asks for the source table      |
|   6 | `test_non_get_and_oversized_mountpoints`           |   ✅   | Non get and oversized mountpoints           |
|   7 | `test_stream_response_forms`                       |   ✅   | Stream response forms                       |
|   8 | `test_error_and_unauthorized_responses`            |   ✅   | Error and unauthorized responses            |
|   9 | `test_str_record_field_positions`                  |   ✅   | Str record field positions                  |
|  10 | `test_str_record_defaults`                         |   ✅   | Str record defaults                         |
|  11 | `test_sourcetable_body_is_self_consistent`         |   ✅   | Sourcetable body is self consistent         |

</details>

---

## test_nts - native_nts - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_ke_length_counts_body_only_ef_counts_whole_field`  |   ✅   | Ke length counts body only ef counts whole field  |
|   2 | `test_ke_record_field_layout`                            |   ✅   | Ke record field layout                            |
|   3 | `test_ke_record_critical_bit_is_separable_from_the_type` |   ✅   | Ke record critical bit is separable from the type |
|   4 | `test_ke_record_type_is_fifteen_bits`                    |   ✅   | Ke record type is fifteen bits                    |
|   5 | `test_ke_request_is_the_three_records_the_rfc_requires`  |   ✅   | Ke request is the three records the rfc requires  |
|   6 | `test_ke_parse_recovers_the_request_it_was_built_from`   |   ✅   | Ke parse recovers the request it was built from   |
|   7 | `test_ke_parse_requires_end_of_message`                  |   ✅   | Ke parse requires end of message                  |
|   8 | `test_ke_parse_stops_at_end_of_message`                  |   ✅   | Ke parse stops at end of message                  |
|   9 | `test_rfc7822_length_includes_header_and_padding`        |   ✅   | Rfc7822 length includes header and padding        |
|  10 | `test_extension_field_types_match_the_registry`          |   ✅   | Extension field types match the registry          |
|  11 | `test_exporter_label_is_the_registered_string`           |   ✅   | Exporter label is the registered string           |
|  12 | `test_record_type_numbers_match_the_registry`            |   ✅   | Record type numbers match the registry            |
|  13 | `test_ke_record_fails_closed`                            |   ✅   | Ke record fails closed                            |
|  14 | `test_ke_request_needs_all_sixteen_octets`               |   ✅   | Ke request needs all sixteen octets               |
|  15 | `test_extension_field_length_bound`                      |   ✅   | Extension field length bound                      |
|  16 | `test_extension_field_fails_closed`                      |   ✅   | Extension field fails closed                      |

</details>

---

## test_nts - native_nts_ke - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_ke_length_counts_body_only_ef_counts_whole_field`  |   ✅   | Ke length counts body only ef counts whole field  |
|   2 | `test_ke_record_field_layout`                            |   ✅   | Ke record field layout                            |
|   3 | `test_ke_record_critical_bit_is_separable_from_the_type` |   ✅   | Ke record critical bit is separable from the type |
|   4 | `test_ke_record_type_is_fifteen_bits`                    |   ✅   | Ke record type is fifteen bits                    |
|   5 | `test_ke_request_is_the_three_records_the_rfc_requires`  |   ✅   | Ke request is the three records the rfc requires  |
|   6 | `test_ke_parse_recovers_the_request_it_was_built_from`   |   ✅   | Ke parse recovers the request it was built from   |
|   7 | `test_ke_parse_requires_end_of_message`                  |   ✅   | Ke parse requires end of message                  |
|   8 | `test_ke_parse_stops_at_end_of_message`                  |   ✅   | Ke parse stops at end of message                  |
|   9 | `test_rfc7822_length_includes_header_and_padding`        |   ✅   | Rfc7822 length includes header and padding        |
|  10 | `test_extension_field_types_match_the_registry`          |   ✅   | Extension field types match the registry          |
|  11 | `test_exporter_label_is_the_registered_string`           |   ✅   | Exporter label is the registered string           |
|  12 | `test_record_type_numbers_match_the_registry`            |   ✅   | Record type numbers match the registry            |
|  13 | `test_ke_record_fails_closed`                            |   ✅   | Ke record fails closed                            |
|  14 | `test_ke_request_needs_all_sixteen_octets`               |   ✅   | Ke request needs all sixteen octets               |
|  15 | `test_extension_field_length_bound`                      |   ✅   | Extension field length bound                      |
|  16 | `test_extension_field_fails_closed`                      |   ✅   | Extension field fails closed                      |

</details>

---

## test_oauth2 - native_oauth2 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OAuth 2.0 token-endpoint client (services/security/oauth2/oauth2.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_rfc6749_413_request_body`                 |   ✅   | Rfc6749 413 request body                 |
|   2 | `test_client_secret_is_appended_when_set`       |   ✅   | Client secret is appended when set       |
|   3 | `test_pkce_code_verifier_is_appended_when_set`  |   ✅   | Pkce code verifier is appended when set  |
|   4 | `test_rfc3986_percent_encoding`                 |   ✅   | Rfc3986 percent encoding                 |
|   5 | `test_rfc6749_sec6_refresh_body`                |   ✅   | Rfc6749 sec6 refresh body                |
|   6 | `test_build_refuses_incomplete_requests`        |   ✅   | Build refuses incomplete requests        |
|   7 | `test_build_refuses_a_short_buffer`             |   ✅   | Build refuses a short buffer             |
|   8 | `test_rfc6749_51_token_response`                |   ✅   | Rfc6749 51 token response                |
|   9 | `test_bearer_response_with_id_token`            |   ✅   | Bearer response with id token            |
|  10 | `test_rfc6749_52_error_object_is_not_a_success` |   ✅   | Rfc6749 52 error object is not a success |
|  11 | `test_parse_null_arguments`                     |   ✅   | Parse null arguments                     |
|  12 | `test_code_exchange_then_refresh`               |   ✅   | Code exchange then refresh               |

</details>

---

## test_oauth2 - native_oauth2_rfc6749 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OAuth 2.0 token-endpoint client (services/security/oauth2/oauth2.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_rfc6749_413_request_body`                 |   ✅   | Rfc6749 413 request body                 |
|   2 | `test_client_secret_is_appended_when_set`       |   ✅   | Client secret is appended when set       |
|   3 | `test_pkce_code_verifier_is_appended_when_set`  |   ✅   | Pkce code verifier is appended when set  |
|   4 | `test_rfc3986_percent_encoding`                 |   ✅   | Rfc3986 percent encoding                 |
|   5 | `test_rfc6749_sec6_refresh_body`                |   ✅   | Rfc6749 sec6 refresh body                |
|   6 | `test_build_refuses_incomplete_requests`        |   ✅   | Build refuses incomplete requests        |
|   7 | `test_build_refuses_a_short_buffer`             |   ✅   | Build refuses a short buffer             |
|   8 | `test_rfc6749_51_token_response`                |   ✅   | Rfc6749 51 token response                |
|   9 | `test_bearer_response_with_id_token`            |   ✅   | Bearer response with id token            |
|  10 | `test_rfc6749_52_error_object_is_not_a_success` |   ✅   | Rfc6749 52 error object is not a success |
|  11 | `test_parse_null_arguments`                     |   ✅   | Parse null arguments                     |
|  12 | `test_code_exchange_then_refresh`               |   ✅   | Code exchange then refresh               |

</details>

---

## test_observability - native_observability - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_transition_fires_hook_with_args`           |   ✅   | Transition fires hook with args           |
|   2 | `test_each_reason_bumps_its_counter`             |   ✅   | Each reason bumps its counter             |
|   3 | `test_closing_gauge_is_derived_from_pool`        |   ✅   | Closing gauge is derived from pool        |
|   4 | `test_reset_clears_cumulative_not_derived_gauge` |   ✅   | Reset clears cumulative not derived gauge |
|   5 | `test_no_hook_after_unregister`                  |   ✅   | No hook after unregister                  |
|   6 | `test_notice_without_hook_still_counts`          |   ✅   | Notice without hook still counts          |

</details>

---

## test_ocit - native_ocit - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OCIT-Outstations message codec (services/transportation/ocit/ocit.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_message_layout_is_big_endian`           |   ✅   | Message layout is big endian           |
|   2 | `test_set_u16_builds_a_set_message`           |   ✅   | Set u16 builds a set message           |
|   3 | `test_build_parse_round_trip`                 |   ✅   | Build parse round trip                 |
|   4 | `test_get_carries_no_value`                   |   ✅   | Get carries no value                   |
|   5 | `test_parse_refuses_a_short_message`          |   ✅   | Parse refuses a short message          |
|   6 | `test_value_u16_is_gated_on_the_data_type`    |   ✅   | Value u16 is gated on the data type    |
|   7 | `test_octet_string_value_takes_the_remainder` |   ✅   | Octet string value takes the remainder |
|   8 | `test_build_bounds`                           |   ✅   | Build bounds                           |
|   9 | `test_codes_are_distinct`                     |   ✅   | Codes are distinct                     |

</details>

---

## test_ocit - native_ocit_msg - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OCIT-Outstations message codec (services/transportation/ocit/ocit.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_message_layout_is_big_endian`           |   ✅   | Message layout is big endian           |
|   2 | `test_set_u16_builds_a_set_message`           |   ✅   | Set u16 builds a set message           |
|   3 | `test_build_parse_round_trip`                 |   ✅   | Build parse round trip                 |
|   4 | `test_get_carries_no_value`                   |   ✅   | Get carries no value                   |
|   5 | `test_parse_refuses_a_short_message`          |   ✅   | Parse refuses a short message          |
|   6 | `test_value_u16_is_gated_on_the_data_type`    |   ✅   | Value u16 is gated on the data type    |
|   7 | `test_octet_string_value_takes_the_remainder` |   ✅   | Octet string value takes the remainder |
|   8 | `test_build_bounds`                           |   ✅   | Build bounds                           |
|   9 | `test_codes_are_distinct`                     |   ✅   | Codes are distinct                     |

</details>

---

## test_oidc - native_oidc - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OpenID Connect RS256 ID Token verifier (services/security/oidc/oidc.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc7515_a2_signature`                      |   ✅   | Rfc7515 a2 signature                      |
|   2 | `test_jwks_find_loads_the_rsa_key`               |   ✅   | Jwks find loads the rsa key               |
|   3 | `test_token_kid`                                 |   ✅   | Token kid                                 |
|   4 | `test_verify_resolves_the_key_itself`            |   ✅   | Verify resolves the key itself            |
|   5 | `test_tampered_token_fails_the_signature`        |   ✅   | Tampered token fails the signature        |
|   6 | `test_alg_must_be_rs256`                         |   ✅   | Alg must be rs256                         |
|   7 | `test_malformed_tokens`                          |   ✅   | Malformed tokens                          |
|   8 | `test_issuer_must_match`                         |   ✅   | Issuer must match                         |
|   9 | `test_audience_must_contain_the_client_id`       |   ✅   | Audience must contain the client id       |
|  10 | `test_expiry`                                    |   ✅   | Expiry                                    |
|  11 | `test_claims_are_cleared_before_each_validation` |   ✅   | Claims are cleared before each validation |

</details>

---

## test_oidc - native_oidc_rfc7515 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OpenID Connect RS256 ID Token verifier (services/security/oidc/oidc.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc7515_a2_signature`                      |   ✅   | Rfc7515 a2 signature                      |
|   2 | `test_jwks_find_loads_the_rsa_key`               |   ✅   | Jwks find loads the rsa key               |
|   3 | `test_token_kid`                                 |   ✅   | Token kid                                 |
|   4 | `test_verify_resolves_the_key_itself`            |   ✅   | Verify resolves the key itself            |
|   5 | `test_tampered_token_fails_the_signature`        |   ✅   | Tampered token fails the signature        |
|   6 | `test_alg_must_be_rs256`                         |   ✅   | Alg must be rs256                         |
|   7 | `test_malformed_tokens`                          |   ✅   | Malformed tokens                          |
|   8 | `test_issuer_must_match`                         |   ✅   | Issuer must match                         |
|   9 | `test_audience_must_contain_the_client_id`       |   ✅   | Audience must contain the client id       |
|  10 | `test_expiry`                                    |   ✅   | Expiry                                    |
|  11 | `test_claims_are_cleared_before_each_validation` |   ✅   | Claims are cleared before each validation |

</details>

---

## test_opcua - native_opcua - ✅ 25 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OPC UA Binary server core (services/opcua/opcua.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_part6_builtin_type_encodings`             |   ✅   | Part6 builtin type encodings             |
|   2 | `test_reader_inverts_the_writer`                |   ✅   | Reader inverts the writer                |
|   3 | `test_bounds_latch`                             |   ✅   | Bounds latch                             |
|   4 | `test_string_decoding`                          |   ✅   | String decoding                          |
|   5 | `test_nodeid_encoding_forms`                    |   ✅   | Nodeid encoding forms                    |
|   6 | `test_nodeid_round_trip`                        |   ✅   | Nodeid round trip                        |
|   7 | `test_nodeid_non_numeric_forms_are_skipped`     |   ✅   | Nodeid non numeric forms are skipped     |
|   8 | `test_datetime_epoch`                           |   ✅   | Datetime epoch                           |
|   9 | `test_uacp_header`                              |   ✅   | Uacp header                              |
|  10 | `test_hello_parse`                              |   ✅   | Hello parse                              |
|  11 | `test_ack_negotiation`                          |   ✅   | Ack negotiation                          |
|  12 | `test_error_message`                            |   ✅   | Error message                            |
|  13 | `test_service_nodeids_match_the_registry`       |   ✅   | Service nodeids match the registry       |
|  14 | `test_open_secure_channel`                      |   ✅   | Open secure channel                      |
|  15 | `test_open_secure_channel_rejects_wrong_frames` |   ✅   | Open secure channel rejects wrong frames |
|  16 | `test_msg_envelope`                             |   ✅   | Msg envelope                             |
|  17 | `test_session_responses`                        |   ✅   | Session responses                        |
|  18 | `test_variant_round_trip`                       |   ✅   | Variant round trip                       |
|  19 | `test_datavalue_mask`                           |   ✅   | Datavalue mask                           |
|  20 | `test_read_request_and_response`                |   ✅   | Read request and response                |
|  21 | `test_read_request_is_clamped`                  |   ✅   | Read request is clamped                  |
|  22 | `test_browse_request_and_response`              |   ✅   | Browse request and response              |
|  23 | `test_write_request_and_response`               |   ✅   | Write request and response               |
|  24 | `test_resolver_registration`                    |   ✅   | Resolver registration                    |
|  25 | `test_qualifiedname_and_localizedtext`          |   ✅   | Qualifiedname and localizedtext          |

</details>

---

## test_opcua_client - native_opcua_client - ✅ 31 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_builders_encode_null_strings`           |   ✅   | Builders encode null strings           |
|   2 | `test_on_ack_header_guards`                   |   ✅   | On ack header guards                   |
|   3 | `test_msg_envelope_guards`                    |   ✅   | Msg envelope guards                    |
|   4 | `test_on_open_envelope_and_result_guards`     |   ✅   | On open envelope and result guards     |
|   5 | `test_on_open_rejects_message_size_mismatch`  |   ✅   | On open rejects message size mismatch  |
|   6 | `test_parsers_reject_bad_service_result`      |   ✅   | Parsers reject bad service result      |
|   7 | `test_parsers_reject_truncated_body`          |   ✅   | Parsers reject truncated body          |
|   8 | `test_on_read_optional_fields_and_limits`     |   ✅   | On read optional fields and limits     |
|   9 | `test_on_write_limits_and_null_sink`          |   ✅   | On write limits and null sink          |
|  10 | `test_on_browse_limits_and_null_sink`         |   ✅   | On browse limits and null sink         |
|  11 | `test_on_browse_display_name_empty_mask`      |   ✅   | On browse display name empty mask      |
|  12 | `test_browse_display_name_locale`             |   ✅   | Browse display name locale             |
|  13 | `test_on_read_all_variant_types`              |   ✅   | On read all variant types              |
|  14 | `test_client_parsers_reject_fault`            |   ✅   | Client parsers reject fault            |
|  15 | `test_client_parsers_reject_malformed`        |   ✅   | Client parsers reject malformed        |
|  16 | `test_hello_ack_roundtrip`                    |   ✅   | Hello ack roundtrip                    |
|  17 | `test_open_roundtrip`                         |   ✅   | Open roundtrip                         |
|  18 | `test_session_roundtrip`                      |   ✅   | Session roundtrip                      |
|  19 | `test_get_endpoints_roundtrip`                |   ✅   | Get endpoints roundtrip                |
|  20 | `test_service_fault_rejected_by_parsers`      |   ✅   | Service fault rejected by parsers      |
|  21 | `test_read_roundtrip`                         |   ✅   | Read roundtrip                         |
|  22 | `test_browse_roundtrip`                       |   ✅   | Browse roundtrip                       |
|  23 | `test_write_roundtrip`                        |   ✅   | Write roundtrip                        |
|  24 | `test_close_session_roundtrip`                |   ✅   | Close session roundtrip                |
|  25 | `test_close_channel_is_clo`                   |   ✅   | Close channel is clo                   |
|  26 | `test_seq_and_request_id_increment`           |   ✅   | Seq and request id increment           |
|  27 | `test_builder_overflow_guard`                 |   ✅   | Builder overflow guard                 |
|  28 | `test_on_read_unknown_variant_rejected`       |   ✅   | On read unknown variant rejected       |
|  29 | `test_response_parsers_reject_negative_count` |   ✅   | Response parsers reject negative count |
|  30 | `test_on_open_guards`                         |   ✅   | On open guards                         |
|  31 | `test_response_header_string_table_skip`      |   ✅   | Response header string table skip      |

</details>

---

## test_openadr - native_openadr - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OpenADR 3.0 event / report JSON codec (services/energy/openadr/openadr.h)._

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_event_document_shape`                  |   ✅   | Event document shape                  |
|   2 | `test_event_carries_every_interval_in_order` |   ✅   | Event carries every interval in order |
|   3 | `test_report_document_shape`                 |   ✅   | Report document shape                 |
|   4 | `test_rfc8259_string_escaping`               |   ✅   | Rfc8259 string escaping               |
|   5 | `test_payload_value_formatting`              |   ✅   | Payload value formatting              |
|   6 | `test_timestamps_are_plain_decimal_integers` |   ✅   | Timestamps are plain decimal integers |
|   7 | `test_overflow_reports_zero`                 |   ✅   | Overflow reports zero                 |

</details>

---

## test_http_ota - native_ota - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_large_body_streams_to_completion`              |   ✅   | Large body streams to completion              |
|   2 | `test_partial_tail_chunk_is_flushed`                 |   ✅   | Partial tail chunk is flushed                 |
|   3 | `test_stream_begin_without_data_sink_tolerates_null` |   ✅   | Stream begin without data sink tolerates null |
|   4 | `test_no_hooks_large_body_is_413`                    |   ✅   | No hooks large body is 413                    |
|   5 | `test_nonmatching_path_not_streamed`                 |   ✅   | Nonmatching path not streamed                 |
|   6 | `test_xff_bracketed_ipv6_overflow`                   |   ✅   | Xff bracketed ipv6 overflow                   |

</details>

---

## test_ota_rollback - native_ota_rollback - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OTA confirm-or-roll-back policy (server/update/ota_rollback.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_a_confirmed_image_commits`                     |   ✅   | A confirmed image commits                     |
|   2 | `test_the_confirm_window_closes_at_its_own_length`   |   ✅   | The confirm window closes at its own length   |
|   3 | `test_a_late_confirmation_still_commits`             |   ✅   | A late confirmation still commits             |
|   4 | `test_only_a_pending_image_is_ever_acted_on`         |   ✅   | Only a pending image is ever acted on         |
|   5 | `test_the_decision_carries_nothing_between_calls`    |   ✅   | The decision carries nothing between calls    |
|   6 | `test_the_image_states_are_distinct`                 |   ✅   | The image states are distinct                 |
|   7 | `test_the_platform_seam_reports_no_image_off_target` |   ✅   | The platform seam reports no image off target |

</details>

---

## test_packml - native_packml - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the PackML / OMAC state model (services/machine_tool/packml/packml.h)._

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_statecurrent_values_are_the_published_numbers`            |   ✅   | Statecurrent values are the published numbers            |
|   2 | `test_control_command_numbers`                                  |   ✅   | Control command numbers                                  |
|   3 | `test_production_path`                                          |   ✅   | Production path                                          |
|   4 | `test_hold_branch_returns_to_execute`                           |   ✅   | Hold branch returns to execute                           |
|   5 | `test_suspend_branch_returns_to_execute`                        |   ✅   | Suspend branch returns to execute                        |
|   6 | `test_abort_is_legal_everywhere_but_the_abort_branch`           |   ✅   | Abort is legal everywhere but the abort branch           |
|   7 | `test_stop_is_legal_everywhere_but_the_stop_and_abort_branches` |   ✅   | Stop is legal everywhere but the stop and abort branches |
|   8 | `test_clear_and_reset_are_state_specific`                       |   ✅   | Clear and reset are state specific                       |
|   9 | `test_illegal_commands_leave_the_state_unchanged`               |   ✅   | Illegal commands leave the state unchanged               |
|  10 | `test_acting_states_advance_and_wait_states_do_not`             |   ✅   | Acting states advance and wait states do not             |
|  11 | `test_names`                                                    |   ✅   | Names                                                    |
|  12 | `test_service_initializes_stopped`                              |   ✅   | Service initializes stopped                              |
|  13 | `test_service_follows_the_engine`                               |   ✅   | Service follows the engine                               |
|  14 | `test_service_counts_only_while_executing`                      |   ✅   | Service counts only while executing                      |
|  15 | `test_service_mode_change_is_restricted`                        |   ✅   | Service mode change is restricted                        |
|  16 | `test_service_speed_is_reported_only_while_executing`           |   ✅   | Service speed is reported only while executing           |
|  17 | `test_service_timers_measure_from_their_own_marks`              |   ✅   | Service timers measure from their own marks              |

</details>

---

## test_partition_monitor - native_partition - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the flash partition-map monitor (server/storage/partition_monitor/partition_monitor.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_kind_matches_the_esp_idf_subtype_registry` |   ✅   | Kind matches the esp idf subtype registry |
|   2 | `test_a_non_app_type_is_classified_as_data`      |   ✅   | A non app type is classified as data      |
|   3 | `test_report_is_an_rfc8259_document`             |   ✅   | Report is an rfc8259 document             |
|   4 | `test_an_empty_table_is_still_an_array`          |   ✅   | An empty table is still an array          |
|   5 | `test_numbers_span_the_whole_32_bit_range`       |   ✅   | Numbers span the whole 32 bit range       |
|   6 | `test_a_label_is_escaped_per_rfc8259_section_7`  |   ✅   | A label is escaped per rfc8259 section 7  |
|   7 | `test_a_short_buffer_fails_closed`               |   ✅   | A short buffer fails closed               |
|   8 | `test_missing_arguments_are_refused`             |   ✅   | Missing arguments are refused             |
|   9 | `test_the_flash_walk_reports_nothing_off_target` |   ✅   | The flash walk reports nothing off target |

</details>

---

## test_pca9685 - native_pca9685 - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NXP PCA9685 PWM / servo codec (server/peripherals/pca9685/pca9685.h)._

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_datasheet_prescale_example`                      |   ✅   | Datasheet prescale example                      |
|   2 | `test_equation1_at_other_rates`                        |   ✅   | Equation1 at other rates                        |
|   3 | `test_prescale_is_clamped_to_the_register_range`       |   ✅   | Prescale is clamped to the register range       |
|   4 | `test_datasheet_channel_register_addresses`            |   ✅   | Datasheet channel register addresses            |
|   5 | `test_pulse_width_to_count`                            |   ✅   | Pulse width to count                            |
|   6 | `test_pulse_width_saturates_at_the_twelve_bit_maximum` |   ✅   | Pulse width saturates at the twelve bit maximum |
|   7 | `test_set_pwm_bytes_layout`                            |   ✅   | Set pwm bytes layout                            |
|   8 | `test_full_on_and_full_off_flags`                      |   ✅   | Full on and full off flags                      |
|   9 | `test_set_pwm_bytes_refuses_bad_arguments`             |   ✅   | Set pwm bytes refuses bad arguments             |

</details>

---

## test_pcap - native_pcap - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the libpcap savefile headers (shared/pcap/pcap.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_global_header_is_24_octets`               |   ✅   | Global header is 24 octets               |
|   2 | `test_global_header_fields`                     |   ✅   | Global header fields                     |
|   3 | `test_magic_octet_order_declares_little_endian` |   ✅   | Magic octet order declares little endian |
|   4 | `test_linktype_reaches_the_file`                |   ✅   | Linktype reaches the file                |
|   5 | `test_record_header_is_16_octets`               |   ✅   | Record header is 16 octets               |
|   6 | `test_record_header_fields`                     |   ✅   | Record header fields                     |
|   7 | `test_short_buffers_write_nothing`              |   ✅   | Short buffers write nothing              |

</details>

---

## test_phy - native_phy - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for layer 1 driven through a real backend (network_drivers/physical/physical.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_a_no_link_reports_no_route`                    |   ✅   | A no link reports no route                    |
|   2 | `test_b_station_bring_up_is_live`                    |   ✅   | B station bring up is live                    |
|   3 | `test_c_ssid_reads_back`                             |   ✅   | C ssid reads back                             |
|   4 | `test_d_ssid_truncates_to_the_callers_cap`           |   ✅   | D ssid truncates to the callers cap           |
|   5 | `test_e_ssid_is_capped_at_the_802_11_limit`          |   ✅   | E ssid is capped at the 802 11 limit          |
|   6 | `test_f_station_mac_is_locally_administered_unicast` |   ✅   | F station mac is locally administered unicast |
|   7 | `test_g_softap_has_its_own_address`                  |   ✅   | G softap has its own address                  |
|   8 | `test_h_wired_wins_the_route`                        |   ✅   | H wired wins the route                        |
|   9 | `test_i_egress_mac_tracks_the_route`                 |   ✅   | I egress mac tracks the route                 |
|  10 | `test_j_ipv6_global_address`                         |   ✅   | J ipv6 global address                         |
|  11 | `test_k_power_save_mode_round_trips`                 |   ✅   | K power save mode round trips                 |
|  12 | `test_l_busy_hold_refcount_gates_doze`               |   ✅   | L busy hold refcount gates doze               |
|  13 | `test_m_power_applies_the_configured_mode`           |   ✅   | M power applies the configured mode           |
|  14 | `test_n_monitor_mode_tunes_and_refuses_a_null_sink`  |   ✅   | N monitor mode tunes and refuses a null sink  |
|  15 | `test_o_power_save_names`                            |   ✅   | O power save names                            |

</details>

---

## test_phy - native_l1_link - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for layer 1 driven through a real backend (network_drivers/physical/physical.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_a_no_link_reports_no_route`                    |   ✅   | A no link reports no route                    |
|   2 | `test_b_station_bring_up_is_live`                    |   ✅   | B station bring up is live                    |
|   3 | `test_c_ssid_reads_back`                             |   ✅   | C ssid reads back                             |
|   4 | `test_d_ssid_truncates_to_the_callers_cap`           |   ✅   | D ssid truncates to the callers cap           |
|   5 | `test_e_ssid_is_capped_at_the_802_11_limit`          |   ✅   | E ssid is capped at the 802 11 limit          |
|   6 | `test_f_station_mac_is_locally_administered_unicast` |   ✅   | F station mac is locally administered unicast |
|   7 | `test_g_softap_has_its_own_address`                  |   ✅   | G softap has its own address                  |
|   8 | `test_h_wired_wins_the_route`                        |   ✅   | H wired wins the route                        |
|   9 | `test_i_egress_mac_tracks_the_route`                 |   ✅   | I egress mac tracks the route                 |
|  10 | `test_j_ipv6_global_address`                         |   ✅   | J ipv6 global address                         |
|  11 | `test_k_power_save_mode_round_trips`                 |   ✅   | K power save mode round trips                 |
|  12 | `test_l_busy_hold_refcount_gates_doze`               |   ✅   | L busy hold refcount gates doze               |
|  13 | `test_m_power_applies_the_configured_mode`           |   ✅   | M power applies the configured mode           |
|  14 | `test_n_monitor_mode_tunes_and_refuses_a_null_sink`  |   ✅   | N monitor mode tunes and refuses a null sink  |
|  15 | `test_o_power_save_names`                            |   ✅   | O power save names                            |

</details>

---

## test_iface - native_phy_iface - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the layer 1 interface registry (network_drivers/physical/physical.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_empty_registry_reports_nothing`            |   ✅   | Empty registry reports nothing            |
|   2 | `test_add_then_lookup_is_the_identity`           |   ✅   | Add then lookup is the identity           |
|   3 | `test_unregistered_id_reads_as_absent`           |   ✅   | Unregistered id reads as absent           |
|   4 | `test_duplicate_id_is_refused`                   |   ✅   | Duplicate id is refused                   |
|   5 | `test_null_send_is_refused`                      |   ✅   | Null send is refused                      |
|   6 | `test_table_full_is_fail_closed`                 |   ✅   | Table full is fail closed                 |
|   7 | `test_reset_empties_the_registry`                |   ✅   | Reset empties the registry                |
|   8 | `test_send_reaches_only_the_addressed_interface` |   ✅   | Send reaches only the addressed interface |
|   9 | `test_send_to_an_unregistered_id_fails`          |   ✅   | Send to an unregistered id fails          |
|  10 | `test_send_reports_a_refusing_interface`         |   ✅   | Send reports a refusing interface         |
|  11 | `test_at_walks_rows_and_marks_the_empty_ones`    |   ✅   | At walks rows and marks the empty ones    |
|  12 | `test_mixed_kinds_coexist`                       |   ✅   | Mixed kinds coexist                       |

</details>

---

## test_iface - native_l1_iface - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the layer 1 interface registry (network_drivers/physical/physical.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_empty_registry_reports_nothing`            |   ✅   | Empty registry reports nothing            |
|   2 | `test_add_then_lookup_is_the_identity`           |   ✅   | Add then lookup is the identity           |
|   3 | `test_unregistered_id_reads_as_absent`           |   ✅   | Unregistered id reads as absent           |
|   4 | `test_duplicate_id_is_refused`                   |   ✅   | Duplicate id is refused                   |
|   5 | `test_null_send_is_refused`                      |   ✅   | Null send is refused                      |
|   6 | `test_table_full_is_fail_closed`                 |   ✅   | Table full is fail closed                 |
|   7 | `test_reset_empties_the_registry`                |   ✅   | Reset empties the registry                |
|   8 | `test_send_reaches_only_the_addressed_interface` |   ✅   | Send reaches only the addressed interface |
|   9 | `test_send_to_an_unregistered_id_fails`          |   ✅   | Send to an unregistered id fails          |
|  10 | `test_send_reports_a_refusing_interface`         |   ✅   | Send reports a refusing interface         |
|  11 | `test_at_walks_rows_and_marks_the_empty_ones`    |   ✅   | At walks rows and marks the empty ones    |
|  12 | `test_mixed_kinds_coexist`                       |   ✅   | Mixed kinds coexist                       |

</details>

---

## test_plaintext - native_plaintext - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the plaintext pool accessor (mmgr/plaintext.h)._

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_the_high_water_mark_starts_at_zero`                  |   ✅   | The high water mark starts at zero                  |
|   2 | `test_a_borrow_advances_the_usage_report`                  |   ✅   | A borrow advances the usage report                  |
|   3 | `test_two_borrows_never_overlap`                           |   ✅   | Two borrows never overlap                           |
|   4 | `test_the_requested_alignment_is_honored`                  |   ✅   | The requested alignment is honored                  |
|   5 | `test_a_zero_size_borrow_is_not_a_failure`                 |   ✅   | A zero size borrow is not a failure                 |
|   6 | `test_the_reset_empties_the_arena_and_reuses_the_base`     |   ✅   | The reset empties the arena and reuses the base     |
|   7 | `test_exhaustion_fails_closed_without_moving_the_cursor`   |   ✅   | Exhaustion fails closed without moving the cursor   |
|   8 | `test_a_request_wider_than_the_arena_is_refused`           |   ✅   | A request wider than the arena is refused           |
|   9 | `test_alignment_padding_cannot_run_past_the_end`           |   ✅   | Alignment padding cannot run past the end           |
|  10 | `test_the_high_water_mark_is_bounded_by_the_arena`         |   ✅   | The high water mark is bounded by the arena         |
|  11 | `test_a_release_restores_the_usage_at_the_mark`            |   ✅   | A release restores the usage at the mark            |
|  12 | `test_nested_marks_unwind_innermost_first`                 |   ✅   | Nested marks unwind innermost first                 |
|  13 | `test_repeated_scopes_do_not_accumulate`                   |   ✅   | Repeated scopes do not accumulate                   |
|  14 | `test_the_two_pools_are_disjoint_regions`                  |   ✅   | The two pools are disjoint regions                  |
|  15 | `test_a_borrow_comes_from_the_callers_slot`                |   ✅   | A borrow comes from the callers slot                |
|  16 | `test_the_span_form_binds_the_length_to_the_borrow`        |   ✅   | The span form binds the length to the borrow        |
|  17 | `test_an_over_budget_span_is_empty_not_null_with_capacity` |   ✅   | An over budget span is empty not null with capacity |
|  18 | `test_a_persistent_borrow_survives_the_reset`              |   ✅   | A persistent borrow survives the reset              |
|  19 | `test_the_table_names_the_functions_it_claims_to`          |   ✅   | The table names the functions it claims to          |

</details>

---

## test_plaintext - native_mmgr_plaintext - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the plaintext pool accessor (mmgr/plaintext.h)._

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_the_high_water_mark_starts_at_zero`                  |   ✅   | The high water mark starts at zero                  |
|   2 | `test_a_borrow_advances_the_usage_report`                  |   ✅   | A borrow advances the usage report                  |
|   3 | `test_two_borrows_never_overlap`                           |   ✅   | Two borrows never overlap                           |
|   4 | `test_the_requested_alignment_is_honored`                  |   ✅   | The requested alignment is honored                  |
|   5 | `test_a_zero_size_borrow_is_not_a_failure`                 |   ✅   | A zero size borrow is not a failure                 |
|   6 | `test_the_reset_empties_the_arena_and_reuses_the_base`     |   ✅   | The reset empties the arena and reuses the base     |
|   7 | `test_exhaustion_fails_closed_without_moving_the_cursor`   |   ✅   | Exhaustion fails closed without moving the cursor   |
|   8 | `test_a_request_wider_than_the_arena_is_refused`           |   ✅   | A request wider than the arena is refused           |
|   9 | `test_alignment_padding_cannot_run_past_the_end`           |   ✅   | Alignment padding cannot run past the end           |
|  10 | `test_the_high_water_mark_is_bounded_by_the_arena`         |   ✅   | The high water mark is bounded by the arena         |
|  11 | `test_a_release_restores_the_usage_at_the_mark`            |   ✅   | A release restores the usage at the mark            |
|  12 | `test_nested_marks_unwind_innermost_first`                 |   ✅   | Nested marks unwind innermost first                 |
|  13 | `test_repeated_scopes_do_not_accumulate`                   |   ✅   | Repeated scopes do not accumulate                   |
|  14 | `test_the_two_pools_are_disjoint_regions`                  |   ✅   | The two pools are disjoint regions                  |
|  15 | `test_a_borrow_comes_from_the_callers_slot`                |   ✅   | A borrow comes from the callers slot                |
|  16 | `test_the_span_form_binds_the_length_to_the_borrow`        |   ✅   | The span form binds the length to the borrow        |
|  17 | `test_an_over_budget_span_is_empty_not_null_with_capacity` |   ✅   | An over budget span is empty not null with capacity |
|  18 | `test_a_persistent_borrow_survives_the_reset`              |   ✅   | A persistent borrow survives the reset              |
|  19 | `test_the_table_names_the_functions_it_claims_to`          |   ✅   | The table names the functions it claims to          |

</details>

---

## test_pmbus - native_pmbus - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the PMBus numeric encodings (server/peripherals/pmbus.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_linear11_field_layout`                             |   ✅   | Linear11 field layout                             |
|   2 | `test_linear11_decode`                                   |   ✅   | Linear11 decode                                   |
|   3 | `test_linear11_out_of_range_is_refused`                  |   ✅   | Linear11 out of range is refused                  |
|   4 | `test_linear11_round_trip_keeps_the_mantissa_resolution` |   ✅   | Linear11 round trip keeps the mantissa resolution |
|   5 | `test_vout_mode_selector`                                |   ✅   | Vout mode selector                                |
|   6 | `test_vout_mode_exponent`                                |   ✅   | Vout mode exponent                                |
|   7 | `test_ulinear16_decode`                                  |   ✅   | Ulinear16 decode                                  |
|   8 | `test_ulinear16_round_trip`                              |   ✅   | Ulinear16 round trip                              |
|   9 | `test_direct_format`                                     |   ✅   | Direct format                                     |
|  10 | `test_status_byte_bits`                                  |   ✅   | Status byte bits                                  |
|  11 | `test_command_codes`                                     |   ✅   | Command codes                                     |

</details>

---

## test_pn532 - native_pn532 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NXP PN532 frame codec (server/peripherals/pn532/pn532.h)._

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_um0701_getfirmwareversion_frames` |   ✅   | Um0701 getfirmwareversion frames |
|   2 | `test_frame_identifiers`                |   ✅   | Frame identifiers                |
|   3 | `test_ack_frame`                        |   ✅   | Ack frame                        |
|   4 | `test_nack_is_not_an_ack`               |   ✅   | Nack is not an ack               |
|   5 | `test_ack_is_not_an_information_frame`  |   ✅   | Ack is not an information frame  |
|   6 | `test_um0701_error_frame`               |   ✅   | Um0701 error frame               |
|   7 | `test_round_trip`                       |   ✅   | Round trip                       |
|   8 | `test_incomplete_frame_asks_for_more`   |   ✅   | Incomplete frame asks for more   |
|   9 | `test_malformed_frames_are_refused`     |   ✅   | Malformed frames are refused     |
|  10 | `test_over_length_is_refused`           |   ✅   | Over length is refused           |
|  11 | `test_build_refuses_bad_arguments`      |   ✅   | Build refuses bad arguments      |

</details>

---

## test_plaintext - native_pool_workers - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the plaintext pool accessor (mmgr/plaintext.h)._

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_the_high_water_mark_starts_at_zero`                  |   ✅   | The high water mark starts at zero                  |
|   2 | `test_a_borrow_advances_the_usage_report`                  |   ✅   | A borrow advances the usage report                  |
|   3 | `test_two_borrows_never_overlap`                           |   ✅   | Two borrows never overlap                           |
|   4 | `test_the_requested_alignment_is_honored`                  |   ✅   | The requested alignment is honored                  |
|   5 | `test_a_zero_size_borrow_is_not_a_failure`                 |   ✅   | A zero size borrow is not a failure                 |
|   6 | `test_the_reset_empties_the_arena_and_reuses_the_base`     |   ✅   | The reset empties the arena and reuses the base     |
|   7 | `test_exhaustion_fails_closed_without_moving_the_cursor`   |   ✅   | Exhaustion fails closed without moving the cursor   |
|   8 | `test_a_request_wider_than_the_arena_is_refused`           |   ✅   | A request wider than the arena is refused           |
|   9 | `test_alignment_padding_cannot_run_past_the_end`           |   ✅   | Alignment padding cannot run past the end           |
|  10 | `test_the_high_water_mark_is_bounded_by_the_arena`         |   ✅   | The high water mark is bounded by the arena         |
|  11 | `test_a_release_restores_the_usage_at_the_mark`            |   ✅   | A release restores the usage at the mark            |
|  12 | `test_nested_marks_unwind_innermost_first`                 |   ✅   | Nested marks unwind innermost first                 |
|  13 | `test_repeated_scopes_do_not_accumulate`                   |   ✅   | Repeated scopes do not accumulate                   |
|  14 | `test_the_two_pools_are_disjoint_regions`                  |   ✅   | The two pools are disjoint regions                  |
|  15 | `test_a_borrow_comes_from_the_callers_slot`                |   ✅   | A borrow comes from the callers slot                |
|  16 | `test_the_span_form_binds_the_length_to_the_borrow`        |   ✅   | The span form binds the length to the borrow        |
|  17 | `test_an_over_budget_span_is_empty_not_null_with_capacity` |   ✅   | An over budget span is empty not null with capacity |
|  18 | `test_a_persistent_borrow_survives_the_reset`              |   ✅   | A persistent borrow survives the reset              |
|  19 | `test_the_table_names_the_functions_it_claims_to`          |   ✅   | The table names the functions it claims to          |

</details>

---

## test_secure_pool - native_pool_workers - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the secure pool accessor (mmgr/secure.h)._

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_release_wipes_before_the_bytes_are_available_again` |   ✅   | Release wipes before the bytes are available again |
|   2 | `test_reset_wipes_every_live_borrow`                      |   ✅   | Reset wipes every live borrow                      |
|   3 | `test_a_scope_guard_wipes_on_every_exit_path`             |   ✅   | A scope guard wipes on every exit path             |
|   4 | `test_nested_scopes_reclaim_lifo`                         |   ✅   | Nested scopes reclaim lifo                         |
|   5 | `test_the_two_pools_are_disjoint_regions`                 |   ✅   | The two pools are disjoint regions                 |
|   6 | `test_a_pointer_from_neither_pool_belongs_to_neither`     |   ✅   | A pointer from neither pool belongs to neither     |
|   7 | `test_one_past_the_pool_is_not_owned`                     |   ✅   | One past the pool is not owned                     |
|   8 | `test_a_persistent_borrow_outlives_every_release`         |   ✅   | A persistent borrow outlives every release         |
|   9 | `test_high_water_records_peak_demand`                     |   ✅   | High water records peak demand                     |
|  10 | `test_an_over_budget_borrow_fails_closed`                 |   ✅   | An over budget borrow fails closed                 |
|  11 | `test_the_table_is_wired_to_the_named_functions`          |   ✅   | The table is wired to the named functions          |
|  12 | `test_the_pool_works_through_the_table`                   |   ✅   | The pool works through the table                   |

</details>

---

## test_power_mgmt - native_power_mgmt - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_the_throttle_cannot_flap`                       |   ✅   | The throttle cannot flap                       |
|   2 | `test_the_throttle_engages_at_the_hot_threshold`      |   ✅   | The throttle engages at the hot threshold      |
|   3 | `test_the_throttle_releases_at_the_cool_threshold`    |   ✅   | The throttle releases at the cool threshold    |
|   4 | `test_no_sensor_is_not_a_cold_reading`                |   ✅   | No sensor is not a cold reading                |
|   5 | `test_the_load_picks_the_rail`                        |   ✅   | The load picks the rail                        |
|   6 | `test_a_load_above_a_hundred_is_clamped`              |   ✅   | A load above a hundred is clamped              |
|   7 | `test_a_brownout_boot_holds_the_floor_for_its_window` |   ✅   | A brownout boot holds the floor for its window |
|   8 | `test_either_hold_forces_the_floor`                   |   ✅   | Either hold forces the floor                   |
|   9 | `test_a_null_config_decides_nothing`                  |   ✅   | A null config decides nothing                  |
|  10 | `test_the_defaults_carry_a_hysteresis_gap`            |   ✅   | The defaults carry a hysteresis gap            |
|  11 | `test_the_report_is_an_rfc8259_object`                |   ✅   | The report is an rfc8259 object                |
|  12 | `test_no_sensor_is_reported_as_null`                  |   ✅   | No sensor is reported as null                  |
|  13 | `test_a_below_zero_reading_keeps_its_sign`            |   ✅   | A below zero reading keeps its sign            |
|  14 | `test_the_report_fails_closed`                        |   ✅   | The report fails closed                        |

</details>

---

## test_powerlink - native_powerlink - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Ethernet POWERLINK basic frame codec (services/fieldbus/powerlink/powerlink.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_epsg_message_type_ids`                   |   ✅   | Epsg message type ids                   |
|   2 | `test_soc_frame`                               |   ✅   | Soc frame                               |
|   3 | `test_preq_frame`                              |   ✅   | Preq frame                              |
|   4 | `test_pres_frame`                              |   ✅   | Pres frame                              |
|   5 | `test_soa_frame`                               |   ✅   | Soa frame                               |
|   6 | `test_asnd_frame`                              |   ✅   | Asnd frame                              |
|   7 | `test_build_parse_round_trip`                  |   ✅   | Build parse round trip                  |
|   8 | `test_parse_refuses_an_undefined_message_type` |   ✅   | Parse refuses an undefined message type |
|   9 | `test_parse_refuses_a_short_frame`             |   ✅   | Parse refuses a short frame             |
|  10 | `test_build_refuses_bad_arguments`             |   ✅   | Build refuses bad arguments             |
|  11 | `test_isochronous_cycle_sequence`              |   ✅   | Isochronous cycle sequence              |

</details>

---

## test_pqc_sntrup761 - native_pqc - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for Streamlined NTRU Prime sntrup761 (crypto/pqc/sntrup761.h), the KEM behind_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_openssh_interop_decaps_vector`          |   ✅   | Openssh interop decaps vector          |
|   2 | `test_encoding_sizes_match_the_vector`        |   ✅   | Encoding sizes match the vector        |
|   3 | `test_round_trip_agrees_over_many_keypairs`   |   ✅   | Round trip agrees over many keypairs   |
|   4 | `test_encaps_is_randomized`                   |   ✅   | Encaps is randomized                   |
|   5 | `test_secret_key_embeds_the_public_key`       |   ✅   | Secret key embeds the public key       |
|   6 | `test_tampered_ciphertext_implicitly_rejects` |   ✅   | Tampered ciphertext implicitly rejects |
|   7 | `test_keygen_retries_a_noninvertible_g`       |   ✅   | Keygen retries a noninvertible g       |

</details>

---

## test_pqc_sha3 - native_sha3_kat - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Keccak-f[1600] sponge (crypto/hash/sha3.h): SHA3-256, SHA3-512, SHAKE128 and_

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_fips202_sha3_256`                         |   ✅   | Fips202 sha3 256                         |
|   2 | `test_fips202_sha3_256_three_blocks`            |   ✅   | Fips202 sha3 256 three blocks            |
|   3 | `test_fips202_sha3_512`                         |   ✅   | Fips202 sha3 512                         |
|   4 | `test_fips202_shake128`                         |   ✅   | Fips202 shake128                         |
|   5 | `test_fips202_shake256`                         |   ✅   | Fips202 shake256                         |
|   6 | `test_fips202_rates`                            |   ✅   | Fips202 rates                            |
|   7 | `test_domain_separation_splits_sha3_from_shake` |   ✅   | Domain separation splits sha3 from shake |
|   8 | `test_incremental_squeeze_matches_one_shot`     |   ✅   | Incremental squeeze matches one shot     |
|   9 | `test_shake_output_is_a_prefix_stream`          |   ✅   | Shake output is a prefix stream          |
|  10 | `test_every_message_octet_reaches_the_digest`   |   ✅   | Every message octet reaches the digest   |

</details>

---

## test_pqc_mlkem - native_mlkem_kat - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for ML-KEM-768 (crypto/pqc/mlkem.h), the post-quantum half of the_

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_fips203_encoding_sizes`                         |   ✅   | Fips203 encoding sizes                         |
|   2 | `test_acvp_keygen`                                    |   ✅   | Acvp keygen                                    |
|   3 | `test_decapsulation_key_embeds_the_encapsulation_key` |   ✅   | Decapsulation key embeds the encapsulation key |
|   4 | `test_acvp_encaps`                                    |   ✅   | Acvp encaps                                    |
|   5 | `test_acvp_decaps`                                    |   ✅   | Acvp decaps                                    |
|   6 | `test_encaps_decaps_agree`                            |   ✅   | Encaps decaps agree                            |
|   7 | `test_tampered_ciphertext_implicitly_rejects`         |   ✅   | Tampered ciphertext implicitly rejects         |
|   8 | `test_encaps_refuses_a_malformed_encapsulation_key`   |   ✅   | Encaps refuses a malformed encapsulation key   |
|   9 | `test_seeds_determine_the_key_pair`                   |   ✅   | Seeds determine the key pair                   |

</details>

---

## test_pqc_sntrup761 - native_sntrup761_kat - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for Streamlined NTRU Prime sntrup761 (crypto/pqc/sntrup761.h), the KEM behind_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_openssh_interop_decaps_vector`          |   ✅   | Openssh interop decaps vector          |
|   2 | `test_encoding_sizes_match_the_vector`        |   ✅   | Encoding sizes match the vector        |
|   3 | `test_round_trip_agrees_over_many_keypairs`   |   ✅   | Round trip agrees over many keypairs   |
|   4 | `test_encaps_is_randomized`                   |   ✅   | Encaps is randomized                   |
|   5 | `test_secret_key_embeds_the_public_key`       |   ✅   | Secret key embeds the public key       |
|   6 | `test_tampered_ciphertext_implicitly_rejects` |   ✅   | Tampered ciphertext implicitly rejects |
|   7 | `test_keygen_retries_a_noninvertible_g`       |   ✅   | Keygen retries a noninvertible g       |

</details>

---

## test_preempt_queue - native_preempt_queue - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the preempting work queues (server/core/preempt_queue.h) and the DMA ingest that_

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_a_lane_is_fifo`                                  |   ✅   | A lane is fifo                                  |
|   2 | `test_an_urgent_post_goes_to_the_front`                |   ✅   | An urgent post goes to the front                |
|   3 | `test_a_full_lane_refuses_rather_than_blocks`          |   ✅   | A full lane refuses rather than blocks          |
|   4 | `test_the_high_water_mark_is_the_peak`                 |   ✅   | The high water mark is the peak                 |
|   5 | `test_a_drained_lane_is_reusable`                      |   ✅   | A drained lane is reusable                      |
|   6 | `test_lanes_are_isolated`                              |   ✅   | Lanes are isolated                              |
|   7 | `test_internal_lanes_outrank_the_user_lane`            |   ✅   | Internal lanes outrank the user lane            |
|   8 | `test_start_requires_a_handler_and_is_idempotent`      |   ✅   | Start requires a handler and is idempotent      |
|   9 | `test_stop_is_per_lane`                                |   ✅   | Stop is per lane                                |
|  10 | `test_a_lane_that_never_started_refuses_every_post`    |   ✅   | A lane that never started refuses every post    |
|  11 | `test_a_lane_out_of_range_and_a_null_item_fail_closed` |   ✅   | A lane out of range and a null item fail closed |
|  12 | `test_a_dma_completion_is_processed_off_the_interrupt` |   ✅   | A dma completion is processed off the interrupt |
|  13 | `test_ping_pong_completions_reach_the_lane_in_order`   |   ✅   | Ping pong completions reach the lane in order   |
|  14 | `test_a_tx_completion_carries_no_bytes`                |   ✅   | A tx completion carries no bytes                |
|  15 | `test_loopback_round_trips_through_the_lane`           |   ✅   | Loopback round trips through the lane           |
|  16 | `test_a_full_lane_drops_the_completion_in_the_isr`     |   ✅   | A full lane drops the completion in the isr     |

</details>

---

## test_crc - native_primitives - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the shared parameterized CRC engine (shared/crc/crc.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_catalogue_check_values`                         |   ✅   | Catalogue check values                         |
|   2 | `test_reflection_flags_actually_apply`                |   ✅   | Reflection flags actually apply                |
|   3 | `test_streaming_matches_the_one_shot`                 |   ✅   | Streaming matches the one shot                 |
|   4 | `test_the_intermediate_register_is_not_the_crc`       |   ✅   | The intermediate register is not the crc       |
|   5 | `test_single_bit_flip_changes_the_crc`                |   ✅   | Single bit flip changes the crc                |
|   6 | `test_order_sensitivity`                              |   ✅   | Order sensitivity                              |
|   7 | `test_leading_zeros_are_significant`                  |   ✅   | Leading zeros are significant                  |
|   8 | `test_empty_input_is_the_bare_init`                   |   ✅   | Empty input is the bare init                   |
|   9 | `test_width_is_respected`                             |   ✅   | Width is respected                             |
|  10 | `test_out_of_range_width_is_clamped`                  |   ✅   | Out of range width is clamped                  |
|  11 | `test_engine_matches_the_hand_rolled_implementations` |   ✅   | Engine matches the hand rolled implementations |
|  12 | `test_null_guards`                                    |   ✅   | Null guards                                    |

</details>

---

## test_primitives - native_primitives - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the no-stdlib floating-point renderers (mmgr/membuild.h: Sb.g and Sb.fixed)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_c11_g_selects_the_style_by_the_exponent`           |   ✅   | C11 g selects the style by the exponent           |
|   2 | `test_g_threshold_moves_with_the_precision`              |   ✅   | G threshold moves with the precision              |
|   3 | `test_g_rounds_to_the_significant_digits`                |   ✅   | G rounds to the significant digits                |
|   4 | `test_g_strips_trailing_zeros_and_a_bare_point`          |   ✅   | G strips trailing zeros and a bare point          |
|   5 | `test_g_exponent_carries_a_sign_and_two_digits`          |   ✅   | G exponent carries a sign and two digits          |
|   6 | `test_g_renders_the_sign_from_the_encoding`              |   ✅   | G renders the sign from the encoding              |
|   7 | `test_c11_f_rounds_the_stored_binary_value`              |   ✅   | C11 f rounds the stored binary value              |
|   8 | `test_an_exact_midpoint_rounds_to_the_even_digit`        |   ✅   | An exact midpoint rounds to the even digit        |
|   9 | `test_f_emits_exactly_the_requested_decimals`            |   ✅   | F emits exactly the requested decimals            |
|  10 | `test_f_always_leads_with_a_digit`                       |   ✅   | F always leads with a digit                       |
|  11 | `test_f_above_the_64_bit_range_falls_back_to_the_g_form` |   ✅   | F above the 64 bit range falls back to the g form |
|  12 | `test_non_finite_values_are_named`                       |   ✅   | Non finite values are named                       |
|  13 | `test_a_number_that_does_not_fit_latches`                |   ✅   | A number that does not fit latches                |
|  14 | `test_the_decimal_count_is_clamped`                      |   ✅   | The decimal count is clamped                      |

</details>

---

## test_crc - native_crc - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the shared parameterized CRC engine (shared/crc/crc.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_catalogue_check_values`                         |   ✅   | Catalogue check values                         |
|   2 | `test_reflection_flags_actually_apply`                |   ✅   | Reflection flags actually apply                |
|   3 | `test_streaming_matches_the_one_shot`                 |   ✅   | Streaming matches the one shot                 |
|   4 | `test_the_intermediate_register_is_not_the_crc`       |   ✅   | The intermediate register is not the crc       |
|   5 | `test_single_bit_flip_changes_the_crc`                |   ✅   | Single bit flip changes the crc                |
|   6 | `test_order_sensitivity`                              |   ✅   | Order sensitivity                              |
|   7 | `test_leading_zeros_are_significant`                  |   ✅   | Leading zeros are significant                  |
|   8 | `test_empty_input_is_the_bare_init`                   |   ✅   | Empty input is the bare init                   |
|   9 | `test_width_is_respected`                             |   ✅   | Width is respected                             |
|  10 | `test_out_of_range_width_is_clamped`                  |   ✅   | Out of range width is clamped                  |
|  11 | `test_engine_matches_the_hand_rolled_implementations` |   ✅   | Engine matches the hand rolled implementations |
|  12 | `test_null_guards`                                    |   ✅   | Null guards                                    |

</details>

---

## test_primitives - native_mmgr_primitives - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the no-stdlib floating-point renderers (mmgr/membuild.h: Sb.g and Sb.fixed)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_c11_g_selects_the_style_by_the_exponent`           |   ✅   | C11 g selects the style by the exponent           |
|   2 | `test_g_threshold_moves_with_the_precision`              |   ✅   | G threshold moves with the precision              |
|   3 | `test_g_rounds_to_the_significant_digits`                |   ✅   | G rounds to the significant digits                |
|   4 | `test_g_strips_trailing_zeros_and_a_bare_point`          |   ✅   | G strips trailing zeros and a bare point          |
|   5 | `test_g_exponent_carries_a_sign_and_two_digits`          |   ✅   | G exponent carries a sign and two digits          |
|   6 | `test_g_renders_the_sign_from_the_encoding`              |   ✅   | G renders the sign from the encoding              |
|   7 | `test_c11_f_rounds_the_stored_binary_value`              |   ✅   | C11 f rounds the stored binary value              |
|   8 | `test_an_exact_midpoint_rounds_to_the_even_digit`        |   ✅   | An exact midpoint rounds to the even digit        |
|   9 | `test_f_emits_exactly_the_requested_decimals`            |   ✅   | F emits exactly the requested decimals            |
|  10 | `test_f_always_leads_with_a_digit`                       |   ✅   | F always leads with a digit                       |
|  11 | `test_f_above_the_64_bit_range_falls_back_to_the_g_form` |   ✅   | F above the 64 bit range falls back to the g form |
|  12 | `test_non_finite_values_are_named`                       |   ✅   | Non finite values are named                       |
|  13 | `test_a_number_that_does_not_fit_latches`                |   ✅   | A number that does not fit latches                |
|  14 | `test_the_decimal_count_is_clamped`                      |   ✅   | The decimal count is clamped                      |

</details>

---

## test_profibus - native_profibus - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_start_delimiters_are_four_bits_apart` |   ✅   | Start delimiters are four bits apart |
|   2 | `test_frame_control_request_bits`           |   ✅   | Frame control request bits           |
|   3 | `test_fcs_is_the_low_octet_of_the_sum`      |   ✅   | Fcs is the low octet of the sum      |
|   4 | `test_fdl_telegram_formats`                 |   ✅   | Fdl telegram formats                 |
|   5 | `test_sd2_with_no_data_unit`                |   ✅   | Sd2 with no data unit                |
|   6 | `test_sd2_length_field_across_the_range`    |   ✅   | Sd2 length field across the range    |
|   7 | `test_parse_refuses_a_damaged_telegram`     |   ✅   | Parse refuses a damaged telegram     |
|   8 | `test_parse_refuses_a_truncated_telegram`   |   ✅   | Parse refuses a truncated telegram   |
|   9 | `test_builders_refuse_a_short_buffer`       |   ✅   | Builders refuse a short buffer       |
|  10 | `test_address_octets_round_trip`            |   ✅   | Address octets round trip            |

</details>

---

## test_profinet - native_profinet - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the PROFINET DCP frame codec (services/fieldbus/profinet/profinet.h)._

|   # | Test                                | Status | Description                  |
| --: | :---------------------------------- | :----: | :--------------------------- |
|   1 | `test_dcp_constants`                |   ✅   | Dcp constants                |
|   2 | `test_dcp_header_layout`            |   ✅   | Dcp header layout            |
|   3 | `test_dcp_header_field_widths`      |   ✅   | Dcp header field widths      |
|   4 | `test_dcp_block_layout_and_padding` |   ✅   | Dcp block layout and padding |
|   5 | `test_dcp_walk_steps_over_the_pad`  |   ✅   | Dcp walk steps over the pad  |
|   6 | `test_identify_response_frame`      |   ✅   | Identify response frame      |
|   7 | `test_dcp_walk_refuses_an_overrun`  |   ✅   | Dcp walk refuses an overrun  |
|   8 | `test_bounds_refusals`              |   ✅   | Bounds refusals              |

</details>

---

## test_promisc - native_promisc - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Wi-Fi promiscuous capture helpers (services/radio/promisc/promisc.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_ieee80211_address_fields_by_ds_bits`      |   ✅   | Ieee80211 address fields by ds bits      |
|   2 | `test_ieee80211_frame_control_type_and_subtype` |   ✅   | Ieee80211 frame control type and subtype |
|   3 | `test_ieee80211_sequence_number`                |   ✅   | Ieee80211 sequence number                |
|   4 | `test_ieee80211_header_length`                  |   ✅   | Ieee80211 header length                  |
|   5 | `test_ieee80211_control_frame`                  |   ✅   | Ieee80211 control frame                  |
|   6 | `test_ieee80211_protected_frame_bit`            |   ✅   | Ieee80211 protected frame bit            |
|   7 | `test_parse_refuses_a_short_frame`              |   ✅   | Parse refuses a short frame              |
|   8 | `test_pcap_global_header_declares_ieee80211`    |   ✅   | Pcap global header declares ieee80211    |
|   9 | `test_pcap_record_header`                       |   ✅   | Pcap record header                       |
|  10 | `test_pcap_headers_fail_closed`                 |   ✅   | Pcap headers fail closed                 |
|  11 | `test_capture_reports_no_radio`                 |   ✅   | Capture reports no radio                 |

</details>

---

## test_promisc - native_promisc_dot11 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Wi-Fi promiscuous capture helpers (services/radio/promisc/promisc.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_ieee80211_address_fields_by_ds_bits`      |   ✅   | Ieee80211 address fields by ds bits      |
|   2 | `test_ieee80211_frame_control_type_and_subtype` |   ✅   | Ieee80211 frame control type and subtype |
|   3 | `test_ieee80211_sequence_number`                |   ✅   | Ieee80211 sequence number                |
|   4 | `test_ieee80211_header_length`                  |   ✅   | Ieee80211 header length                  |
|   5 | `test_ieee80211_control_frame`                  |   ✅   | Ieee80211 control frame                  |
|   6 | `test_ieee80211_protected_frame_bit`            |   ✅   | Ieee80211 protected frame bit            |
|   7 | `test_parse_refuses_a_short_frame`              |   ✅   | Parse refuses a short frame              |
|   8 | `test_pcap_global_header_declares_ieee80211`    |   ✅   | Pcap global header declares ieee80211    |
|   9 | `test_pcap_record_header`                       |   ✅   | Pcap record header                       |
|  10 | `test_pcap_headers_fail_closed`                 |   ✅   | Pcap headers fail closed                 |
|  11 | `test_capture_reports_no_radio`                 |   ✅   | Capture reports no radio                 |

</details>

---

## test_protobuf - native_protobuf - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Protocol Buffers wire codec (services/iot/protobuf/protobuf.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_encoding_document_worked_examples`              |   ✅   | Encoding document worked examples              |
|   2 | `test_base_128_varint`                                |   ✅   | Base 128 varint                                |
|   3 | `test_tag_formula_and_wire_type_ids`                  |   ✅   | Tag formula and wire type ids                  |
|   4 | `test_zigzag_table`                                   |   ✅   | Zigzag table                                   |
|   5 | `test_int64_is_two_s_complement_and_sint64_is_zigzag` |   ✅   | Int64 is two s complement and sint64 is zigzag |
|   6 | `test_fixed_width_and_bool_payloads`                  |   ✅   | Fixed width and bool payloads                  |
|   7 | `test_float_and_double_bit_patterns`                  |   ✅   | Float and double bit patterns                  |
|   8 | `test_packed_repeated_field`                          |   ✅   | Packed repeated field                          |
|   9 | `test_group_and_unassigned_wire_types_are_refused`    |   ✅   | Group and unassigned wire types are refused    |
|  10 | `test_truncated_records_are_refused`                  |   ✅   | Truncated records are refused                  |
|  11 | `test_writer_fails_closed`                            |   ✅   | Writer fails closed                            |
|  12 | `test_a_message_walks_record_by_record`               |   ✅   | A message walks record by record               |

</details>

---

## test_protobuf - native_protobuf_wire - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Protocol Buffers wire codec (services/iot/protobuf/protobuf.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_encoding_document_worked_examples`              |   ✅   | Encoding document worked examples              |
|   2 | `test_base_128_varint`                                |   ✅   | Base 128 varint                                |
|   3 | `test_tag_formula_and_wire_type_ids`                  |   ✅   | Tag formula and wire type ids                  |
|   4 | `test_zigzag_table`                                   |   ✅   | Zigzag table                                   |
|   5 | `test_int64_is_two_s_complement_and_sint64_is_zigzag` |   ✅   | Int64 is two s complement and sint64 is zigzag |
|   6 | `test_fixed_width_and_bool_payloads`                  |   ✅   | Fixed width and bool payloads                  |
|   7 | `test_float_and_double_bit_patterns`                  |   ✅   | Float and double bit patterns                  |
|   8 | `test_packed_repeated_field`                          |   ✅   | Packed repeated field                          |
|   9 | `test_group_and_unassigned_wire_types_are_refused`    |   ✅   | Group and unassigned wire types are refused    |
|  10 | `test_truncated_records_are_refused`                  |   ✅   | Truncated records are refused                  |
|  11 | `test_writer_fails_closed`                            |   ✅   | Writer fails closed                            |
|  12 | `test_a_message_walks_record_by_record`               |   ✅   | A message walks record by record               |

</details>

---

## test_protomem - native_protomem - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the byte-span operations (mmgr/protomem.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_c11_cmp_orders_bytes_as_unsigned`                 |   ✅   | C11 cmp orders bytes as unsigned                 |
|   2 | `test_cmp_of_equal_and_of_zero_length`                  |   ✅   | Cmp of equal and of zero length                  |
|   3 | `test_cmp_does_not_stop_at_a_nul`                       |   ✅   | Cmp does not stop at a nul                       |
|   4 | `test_cpy_moves_exactly_n_bytes`                        |   ✅   | Cpy moves exactly n bytes                        |
|   5 | `test_cpy_at_every_offset_pair`                         |   ✅   | Cpy at every offset pair                         |
|   6 | `test_cpy_of_zero_bytes_writes_nothing`                 |   ✅   | Cpy of zero bytes writes nothing                 |
|   7 | `test_move_is_correct_under_overlap_in_both_directions` |   ✅   | Move is correct under overlap in both directions |
|   8 | `test_move_onto_itself_changes_nothing`                 |   ✅   | Move onto itself changes nothing                 |
|   9 | `test_move_without_overlap`                             |   ✅   | Move without overlap                             |
|  10 | `test_chr_finds_the_first_occurrence`                   |   ✅   | Chr finds the first occurrence                   |
|  11 | `test_chr_does_not_stop_at_a_nul`                       |   ✅   | Chr does not stop at a nul                       |
|  12 | `test_chr_finds_a_high_byte`                            |   ✅   | Chr finds a high byte                            |
|  13 | `test_set_fills_exactly_n_bytes`                        |   ✅   | Set fills exactly n bytes                        |
|  14 | `test_set_writes_a_byte_not_a_word`                     |   ✅   | Set writes a byte not a word                     |
|  15 | `test_zero_clears_exactly_n_bytes`                      |   ✅   | Zero clears exactly n bytes                      |

</details>

---

## test_protomem - native_mmgr_protomem - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the byte-span operations (mmgr/protomem.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_c11_cmp_orders_bytes_as_unsigned`                 |   ✅   | C11 cmp orders bytes as unsigned                 |
|   2 | `test_cmp_of_equal_and_of_zero_length`                  |   ✅   | Cmp of equal and of zero length                  |
|   3 | `test_cmp_does_not_stop_at_a_nul`                       |   ✅   | Cmp does not stop at a nul                       |
|   4 | `test_cpy_moves_exactly_n_bytes`                        |   ✅   | Cpy moves exactly n bytes                        |
|   5 | `test_cpy_at_every_offset_pair`                         |   ✅   | Cpy at every offset pair                         |
|   6 | `test_cpy_of_zero_bytes_writes_nothing`                 |   ✅   | Cpy of zero bytes writes nothing                 |
|   7 | `test_move_is_correct_under_overlap_in_both_directions` |   ✅   | Move is correct under overlap in both directions |
|   8 | `test_move_onto_itself_changes_nothing`                 |   ✅   | Move onto itself changes nothing                 |
|   9 | `test_move_without_overlap`                             |   ✅   | Move without overlap                             |
|  10 | `test_chr_finds_the_first_occurrence`                   |   ✅   | Chr finds the first occurrence                   |
|  11 | `test_chr_does_not_stop_at_a_nul`                       |   ✅   | Chr does not stop at a nul                       |
|  12 | `test_chr_finds_a_high_byte`                            |   ✅   | Chr finds a high byte                            |
|  13 | `test_set_fills_exactly_n_bytes`                        |   ✅   | Set fills exactly n bytes                        |
|  14 | `test_set_writes_a_byte_not_a_word`                     |   ✅   | Set writes a byte not a word                     |
|  15 | `test_zero_clears_exactly_n_bytes`                      |   ✅   | Zero clears exactly n bytes                      |

</details>

---

## test_protostr - native_protostr - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the bounded-run operations (mmgr/protostr.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_len_is_the_bounded_strnlen`                       |   ✅   | Len is the bounded strnlen                       |
|   2 | `test_len_stops_at_the_terminator_whatever_the_cap`     |   ✅   | Len stops at the terminator whatever the cap     |
|   3 | `test_len_at_every_length`                              |   ✅   | Len at every length                              |
|   4 | `test_diff_names_the_first_differing_index`             |   ✅   | Diff names the first differing index             |
|   5 | `test_diff_at_every_position`                           |   ✅   | Diff at every position                           |
|   6 | `test_eq_requires_the_terminator_before_the_difference` |   ✅   | Eq requires the terminator before the difference |
|   7 | `test_starts_reads_the_tie_as_a_match`                  |   ✅   | Starts reads the tie as a match                  |
|   8 | `test_ci_folds_only_ascii_letters`                      |   ✅   | Ci folds only ascii letters                      |
|   9 | `test_ci_over_a_run_longer_than_a_word`                 |   ✅   | Ci over a run longer than a word                 |
|  10 | `test_find_returns_the_first_occurrence`                |   ✅   | Find returns the first occurrence                |
|  11 | `test_find_does_not_read_past_the_terminator`           |   ✅   | Find does not read past the terminator           |
|  12 | `test_find_is_bounded_by_read_cap`                      |   ✅   | Find is bounded by read cap                      |
|  13 | `test_has_agrees_with_find`                             |   ✅   | Has agrees with find                             |
|  14 | `test_copy_always_terminates_within_the_destination`    |   ✅   | Copy always terminates within the destination    |
|  15 | `test_copy_with_zero_capacity_writes_nothing`           |   ✅   | Copy with zero capacity writes nothing           |
|  16 | `test_ws_is_the_c11_white_space_set`                    |   ✅   | Ws is the c11 white space set                    |
|  17 | `test_digit_is_the_ten_decimal_digits`                  |   ✅   | Digit is the ten decimal digits                  |
|  18 | `test_to_long_follows_the_strtol_endptr_contract`       |   ✅   | To long follows the strtol endptr contract       |
|  19 | `test_to_ulong_takes_plus_and_not_minus`                |   ✅   | To ulong takes plus and not minus                |
|  20 | `test_to_double_parses_the_strtod_subject_sequence`     |   ✅   | To double parses the strtod subject sequence     |
|  21 | `test_to_double_clamps_a_runaway_exponent`              |   ✅   | To double clamps a runaway exponent              |
|  22 | `test_to_float_narrows_a_double_parse`                  |   ✅   | To float narrows a double parse                  |

</details>

---

## test_protostr - native_mmgr_protostr - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the bounded-run operations (mmgr/protostr.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_len_is_the_bounded_strnlen`                       |   ✅   | Len is the bounded strnlen                       |
|   2 | `test_len_stops_at_the_terminator_whatever_the_cap`     |   ✅   | Len stops at the terminator whatever the cap     |
|   3 | `test_len_at_every_length`                              |   ✅   | Len at every length                              |
|   4 | `test_diff_names_the_first_differing_index`             |   ✅   | Diff names the first differing index             |
|   5 | `test_diff_at_every_position`                           |   ✅   | Diff at every position                           |
|   6 | `test_eq_requires_the_terminator_before_the_difference` |   ✅   | Eq requires the terminator before the difference |
|   7 | `test_starts_reads_the_tie_as_a_match`                  |   ✅   | Starts reads the tie as a match                  |
|   8 | `test_ci_folds_only_ascii_letters`                      |   ✅   | Ci folds only ascii letters                      |
|   9 | `test_ci_over_a_run_longer_than_a_word`                 |   ✅   | Ci over a run longer than a word                 |
|  10 | `test_find_returns_the_first_occurrence`                |   ✅   | Find returns the first occurrence                |
|  11 | `test_find_does_not_read_past_the_terminator`           |   ✅   | Find does not read past the terminator           |
|  12 | `test_find_is_bounded_by_read_cap`                      |   ✅   | Find is bounded by read cap                      |
|  13 | `test_has_agrees_with_find`                             |   ✅   | Has agrees with find                             |
|  14 | `test_copy_always_terminates_within_the_destination`    |   ✅   | Copy always terminates within the destination    |
|  15 | `test_copy_with_zero_capacity_writes_nothing`           |   ✅   | Copy with zero capacity writes nothing           |
|  16 | `test_ws_is_the_c11_white_space_set`                    |   ✅   | Ws is the c11 white space set                    |
|  17 | `test_digit_is_the_ten_decimal_digits`                  |   ✅   | Digit is the ten decimal digits                  |
|  18 | `test_to_long_follows_the_strtol_endptr_contract`       |   ✅   | To long follows the strtol endptr contract       |
|  19 | `test_to_ulong_takes_plus_and_not_minus`                |   ✅   | To ulong takes plus and not minus                |
|  20 | `test_to_double_parses_the_strtod_subject_sequence`     |   ✅   | To double parses the strtod subject sequence     |
|  21 | `test_to_double_clamps_a_runaway_exponent`              |   ✅   | To double clamps a runaway exponent              |
|  22 | `test_to_float_narrows_a_double_parse`                  |   ✅   | To float narrows a double parse                  |

</details>

---

## test_provisioning - native_prov - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the captive-portal form reader (server/core/provisioning_service/provisioning_service.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_the_specs_own_worked_escapes`                   |   ✅   | The specs own worked escapes                   |
|   2 | `test_plus_decodes_to_a_space`                        |   ✅   | Plus decodes to a space                        |
|   3 | `test_a_triplet_is_the_octets_numeric_value`          |   ✅   | A triplet is the octets numeric value          |
|   4 | `test_hex_digit_case_is_equivalent`                   |   ✅   | Hex digit case is equivalent                   |
|   5 | `test_pairs_are_separated_by_ampersand`               |   ✅   | Pairs are separated by ampersand               |
|   6 | `test_an_empty_value_is_still_a_present_field`        |   ✅   | An empty value is still a present field        |
|   7 | `test_a_name_matches_only_a_whole_field`              |   ✅   | A name matches only a whole field              |
|   8 | `test_an_incomplete_triplet_is_not_decoded`           |   ✅   | An incomplete triplet is not decoded           |
|   9 | `test_the_value_is_bounded_and_terminated`            |   ✅   | The value is bounded and terminated            |
|  10 | `test_null_arguments_and_zero_capacity_are_refused`   |   ✅   | Null arguments and zero capacity are refused   |
|  11 | `test_the_host_credential_store_holds_nothing`        |   ✅   | The host credential store holds nothing        |
|  12 | `test_load_writes_only_the_destinations_it_was_given` |   ✅   | Load writes only the destinations it was given |

</details>

---

## test_proxy_protocol - native_proxy_protocol - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_v1_spec_example_line`                             |   ✅   | V1 spec example line                             |
|   2 | `test_v1_widest_tcp4_line_is_56_octets`                 |   ✅   | V1 widest tcp4 line is 56 octets                 |
|   3 | `test_v1_unknown_short_form`                            |   ✅   | V1 unknown short form                            |
|   4 | `test_v1_unknown_long_form_is_ignored_up_to_the_crlf`   |   ✅   | V1 unknown long form is ignored up to the crlf   |
|   5 | `test_v2_header_layout`                                 |   ✅   | V2 header layout                                 |
|   6 | `test_v2_round_trip`                                    |   ✅   | V2 round trip                                    |
|   7 | `test_v2_local_command_yields_no_address`               |   ✅   | V2 local command yields no address               |
|   8 | `test_v2_unimplemented_family_is_skipped_by_its_length` |   ✅   | V2 unimplemented family is skipped by its length |
|   9 | `test_v2_rejects_a_foreign_version`                     |   ✅   | V2 rejects a foreign version                     |
|  10 | `test_partial_headers_are_refused`                      |   ✅   | Partial headers are refused                      |
|  11 | `test_a_lone_cr_or_lf_does_not_terminate_the_line`      |   ✅   | A lone cr or lf does not terminate the line      |
|  12 | `test_v1_field_ranges`                                  |   ✅   | V1 field ranges                                  |
|  13 | `test_v1_leading_zeros_are_refused`                     |   ✅   | V1 leading zeros are refused                     |
|  14 | `test_v1_other_protocol_tokens_yield_no_address`        |   ✅   | V1 other protocol tokens yield no address        |
|  15 | `test_a_stream_without_a_header_is_reported_as_such`    |   ✅   | A stream without a header is reported as such    |
|  16 | `test_builders_fail_closed_on_a_short_buffer`           |   ✅   | Builders fail closed on a short buffer           |
|  17 | `test_null_arguments_are_refused`                       |   ✅   | Null arguments are refused                       |

</details>

---

## test_psram_pool - native_psram_pool - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the buffer placement policy and the SPI DMA ping-pong index (mmgr/psram_pool.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_dram_reserve_is_never_spent`             |   ✅   | Dram reserve is never spent             |
|   2 | `test_a_zero_size_request_is_refused`          |   ✅   | A zero size request is refused          |
|   3 | `test_dma_required_never_leaves_dram`          |   ✅   | Dma required never leaves dram          |
|   4 | `test_at_or_above_the_threshold_prefers_psram` |   ✅   | At or above the threshold prefers psram |
|   5 | `test_below_the_threshold_prefers_dram`        |   ✅   | Below the threshold prefers dram        |
|   6 | `test_pingpong_roles_are_always_opposite`      |   ✅   | Pingpong roles are always opposite      |
|   7 | `test_pingpong_accessors_refuse_a_null_handle` |   ✅   | Pingpong accessors refuse a null handle |

</details>

---

## test_ptp - native_ptp - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_correction_field_is_nanoseconds_scaled_by_2_16`    |   ✅   | Correction field is nanoseconds scaled by 2 16    |
|   2 | `test_header_field_offsets`                              |   ✅   | Header field offsets                              |
|   3 | `test_header_round_trip`                                 |   ✅   | Header round trip                                 |
|   4 | `test_message_type_numbers`                              |   ✅   | Message type numbers                              |
|   5 | `test_timestamp_octet_layout`                            |   ✅   | Timestamp octet layout                            |
|   6 | `test_timestamp_nanosecond_conversion`                   |   ✅   | Timestamp nanosecond conversion                   |
|   7 | `test_message_lengths`                                   |   ✅   | Message lengths                                   |
|   8 | `test_timestamp_message_build_and_parse`                 |   ✅   | Timestamp message build and parse                 |
|   9 | `test_delay_resp_body`                                   |   ✅   | Delay resp body                                   |
|  10 | `test_peer_delay_messages`                               |   ✅   | Peer delay messages                               |
|  11 | `test_announce_body_offsets`                             |   ✅   | Announce body offsets                             |
|  12 | `test_announce_utc_offset_is_signed`                     |   ✅   | Announce utc offset is signed                     |
|  13 | `test_builders_stamp_version_two`                        |   ✅   | Builders stamp version two                        |
|  14 | `test_offset_and_delay_from_the_four_timestamps`         |   ✅   | Offset and delay from the four timestamps         |
|  15 | `test_offset_and_delay_worked_example`                   |   ✅   | Offset and delay worked example                   |
|  16 | `test_peer_link_delay_is_independent_of_the_peer_offset` |   ✅   | Peer link delay is independent of the peer offset |
|  17 | `test_short_buffers_are_refused`                         |   ✅   | Short buffers are refused                         |
|  18 | `test_transport_ports`                                   |   ✅   | Transport ports                                   |

</details>

---

## test_ptp - native_ptp_wire - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_correction_field_is_nanoseconds_scaled_by_2_16`    |   ✅   | Correction field is nanoseconds scaled by 2 16    |
|   2 | `test_header_field_offsets`                              |   ✅   | Header field offsets                              |
|   3 | `test_header_round_trip`                                 |   ✅   | Header round trip                                 |
|   4 | `test_message_type_numbers`                              |   ✅   | Message type numbers                              |
|   5 | `test_timestamp_octet_layout`                            |   ✅   | Timestamp octet layout                            |
|   6 | `test_timestamp_nanosecond_conversion`                   |   ✅   | Timestamp nanosecond conversion                   |
|   7 | `test_message_lengths`                                   |   ✅   | Message lengths                                   |
|   8 | `test_timestamp_message_build_and_parse`                 |   ✅   | Timestamp message build and parse                 |
|   9 | `test_delay_resp_body`                                   |   ✅   | Delay resp body                                   |
|  10 | `test_peer_delay_messages`                               |   ✅   | Peer delay messages                               |
|  11 | `test_announce_body_offsets`                             |   ✅   | Announce body offsets                             |
|  12 | `test_announce_utc_offset_is_signed`                     |   ✅   | Announce utc offset is signed                     |
|  13 | `test_builders_stamp_version_two`                        |   ✅   | Builders stamp version two                        |
|  14 | `test_offset_and_delay_from_the_four_timestamps`         |   ✅   | Offset and delay from the four timestamps         |
|  15 | `test_offset_and_delay_worked_example`                   |   ✅   | Offset and delay worked example                   |
|  16 | `test_peer_link_delay_is_independent_of_the_peer_offset` |   ✅   | Peer link delay is independent of the peer offset |
|  17 | `test_short_buffers_are_refused`                         |   ✅   | Short buffers are refused                         |
|  18 | `test_transport_ports`                                   |   ✅   | Transport ports                                   |

</details>

---

## test_qpack - native_qpack - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the QPACK field-section codec_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc9204_b1_worked_example`            |   ✅   | Rfc9204 b1 worked example            |
|   2 | `test_rfc9204_field_section_prefix`         |   ✅   | Rfc9204 field section prefix         |
|   3 | `test_rfc9204_indexed_field_line`           |   ✅   | Rfc9204 indexed field line           |
|   4 | `test_rfc9204_appendix_a_static_table`      |   ✅   | Rfc9204 appendix a static table      |
|   5 | `test_rfc9204_literal_with_name_reference`  |   ✅   | Rfc9204 literal with name reference  |
|   6 | `test_rfc9204_literal_with_literal_name`    |   ✅   | Rfc9204 literal with literal name    |
|   7 | `test_field_section_round_trip`             |   ✅   | Field section round trip             |
|   8 | `test_dynamic_table_references_are_refused` |   ✅   | Dynamic table references are refused |
|   9 | `test_static_index_out_of_range_is_refused` |   ✅   | Static index out of range is refused |
|  10 | `test_truncated_block_is_refused`           |   ✅   | Truncated block is refused           |
|  11 | `test_scratch_bound_is_respected`           |   ✅   | Scratch bound is respected           |
|  12 | `test_emit_refusal_aborts_the_decode`       |   ✅   | Emit refusal aborts the decode       |
|  13 | `test_encoder_refuses_a_short_destination`  |   ✅   | Encoder refuses a short destination  |

</details>

---

## test_qpack - native_qpack_rfc - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the QPACK field-section codec_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc9204_b1_worked_example`            |   ✅   | Rfc9204 b1 worked example            |
|   2 | `test_rfc9204_field_section_prefix`         |   ✅   | Rfc9204 field section prefix         |
|   3 | `test_rfc9204_indexed_field_line`           |   ✅   | Rfc9204 indexed field line           |
|   4 | `test_rfc9204_appendix_a_static_table`      |   ✅   | Rfc9204 appendix a static table      |
|   5 | `test_rfc9204_literal_with_name_reference`  |   ✅   | Rfc9204 literal with name reference  |
|   6 | `test_rfc9204_literal_with_literal_name`    |   ✅   | Rfc9204 literal with literal name    |
|   7 | `test_field_section_round_trip`             |   ✅   | Field section round trip             |
|   8 | `test_dynamic_table_references_are_refused` |   ✅   | Dynamic table references are refused |
|   9 | `test_static_index_out_of_range_is_refused` |   ✅   | Static index out of range is refused |
|  10 | `test_truncated_block_is_refused`           |   ✅   | Truncated block is refused           |
|  11 | `test_scratch_bound_is_respected`           |   ✅   | Scratch bound is respected           |
|  12 | `test_emit_refusal_aborts_the_decode`       |   ✅   | Emit refusal aborts the decode       |
|  13 | `test_encoder_refuses_a_short_destination`  |   ✅   | Encoder refuses a short destination  |

</details>

---

## test_quic_crypto - native_quic_crypto - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for QUIC packet protection_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_fips197_aes128_block`                 |   ✅   | Fips197 aes128 block                 |
|   2 | `test_gcm_test_case_4`                      |   ✅   | Gcm test case 4                      |
|   3 | `test_rfc9001_a1_initial_secret_chain`      |   ✅   | Rfc9001 a1 initial secret chain      |
|   4 | `test_rfc9001_a1_packet_keys`               |   ✅   | Rfc9001 a1 packet keys               |
|   5 | `test_rfc9001_a3_server_initial`            |   ✅   | Rfc9001 a3 server initial            |
|   6 | `test_rfc9001_a2_client_initial`            |   ✅   | Rfc9001 a2 client initial            |
|   7 | `test_rfc9001_a4_retry_integrity_tag`       |   ✅   | Rfc9001 a4 retry integrity tag       |
|   8 | `test_tampered_packet_fails_authentication` |   ✅   | Tampered packet fails authentication |
|   9 | `test_parameter_bounds`                     |   ✅   | Parameter bounds                     |
|  10 | `test_short_header_round_trip`              |   ✅   | Short header round trip              |

</details>

---

## test_quic_crypto - native_quic_crypto_rfc - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for QUIC packet protection_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_fips197_aes128_block`                 |   ✅   | Fips197 aes128 block                 |
|   2 | `test_gcm_test_case_4`                      |   ✅   | Gcm test case 4                      |
|   3 | `test_rfc9001_a1_initial_secret_chain`      |   ✅   | Rfc9001 a1 initial secret chain      |
|   4 | `test_rfc9001_a1_packet_keys`               |   ✅   | Rfc9001 a1 packet keys               |
|   5 | `test_rfc9001_a3_server_initial`            |   ✅   | Rfc9001 a3 server initial            |
|   6 | `test_rfc9001_a2_client_initial`            |   ✅   | Rfc9001 a2 client initial            |
|   7 | `test_rfc9001_a4_retry_integrity_tag`       |   ✅   | Rfc9001 a4 retry integrity tag       |
|   8 | `test_tampered_packet_fails_authentication` |   ✅   | Tampered packet fails authentication |
|   9 | `test_parameter_bounds`                     |   ✅   | Parameter bounds                     |
|  10 | `test_short_header_round_trip`              |   ✅   | Short header round trip              |

</details>

---

## test_quic_frame - native_quic_frame - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for QUIC frame coding (network_drivers/presentation/http/http3/quic_frame.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_rfc9000_frame_type_table`                     |   ✅   | Rfc9000 frame type table                     |
|   2 | `test_rfc9000_single_octet_frames`                  |   ✅   | Rfc9000 single octet frames                  |
|   3 | `test_rfc9000_ack_frame_fields`                     |   ✅   | Rfc9000 ack frame fields                     |
|   4 | `test_rfc9000_ack_ranges_and_ecn_are_consumed`      |   ✅   | Rfc9000 ack ranges and ecn are consumed      |
|   5 | `test_rfc9000_crypto_frame`                         |   ✅   | Rfc9000 crypto frame                         |
|   6 | `test_rfc9000_stream_frame_type_bits`               |   ✅   | Rfc9000 stream frame type bits               |
|   7 | `test_rfc9000_max_data`                             |   ✅   | Rfc9000 max data                             |
|   8 | `test_rfc9000_connection_close_variants`            |   ✅   | Rfc9000 connection close variants            |
|   9 | `test_rfc9000_transport_error_codes`                |   ✅   | Rfc9000 transport error codes                |
|  10 | `test_rfc9000_unhandled_frames_consume_their_shape` |   ✅   | Rfc9000 unhandled frames consume their shape |
|  11 | `test_rfc9000_fixed_width_frames`                   |   ✅   | Rfc9000 fixed width frames                   |
|  12 | `test_truncated_frames_are_refused`                 |   ✅   | Truncated frames are refused                 |
|  13 | `test_builders_refuse_a_short_destination`          |   ✅   | Builders refuse a short destination          |

</details>

---

## test_quic_frame - native_quic_frame_rfc - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for QUIC frame coding (network_drivers/presentation/http/http3/quic_frame.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_rfc9000_frame_type_table`                     |   ✅   | Rfc9000 frame type table                     |
|   2 | `test_rfc9000_single_octet_frames`                  |   ✅   | Rfc9000 single octet frames                  |
|   3 | `test_rfc9000_ack_frame_fields`                     |   ✅   | Rfc9000 ack frame fields                     |
|   4 | `test_rfc9000_ack_ranges_and_ecn_are_consumed`      |   ✅   | Rfc9000 ack ranges and ecn are consumed      |
|   5 | `test_rfc9000_crypto_frame`                         |   ✅   | Rfc9000 crypto frame                         |
|   6 | `test_rfc9000_stream_frame_type_bits`               |   ✅   | Rfc9000 stream frame type bits               |
|   7 | `test_rfc9000_max_data`                             |   ✅   | Rfc9000 max data                             |
|   8 | `test_rfc9000_connection_close_variants`            |   ✅   | Rfc9000 connection close variants            |
|   9 | `test_rfc9000_transport_error_codes`                |   ✅   | Rfc9000 transport error codes                |
|  10 | `test_rfc9000_unhandled_frames_consume_their_shape` |   ✅   | Rfc9000 unhandled frames consume their shape |
|  11 | `test_rfc9000_fixed_width_frames`                   |   ✅   | Rfc9000 fixed width frames                   |
|  12 | `test_truncated_frames_are_refused`                 |   ✅   | Truncated frames are refused                 |
|  13 | `test_builders_refuse_a_short_destination`          |   ✅   | Builders refuse a short destination          |

</details>

---

## test_quic_packet - native_quic_packet - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for QUIC packet headers and packet-number coding_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_rfc9001_published_headers`              |   ✅   | Rfc9001 published headers              |
|   2 | `test_build_reproduces_the_published_headers` |   ✅   | Build reproduces the published headers |
|   3 | `test_rfc9000_long_packet_types`              |   ✅   | Rfc9000 long packet types              |
|   4 | `test_rfc9000_fixed_bit_is_required`          |   ✅   | Rfc9000 fixed bit is required          |
|   5 | `test_rfc9001_short_header`                   |   ✅   | Rfc9001 short header                   |
|   6 | `test_rfc9000_version_negotiation`            |   ✅   | Rfc9000 version negotiation            |
|   7 | `test_rfc9000_a2_packet_number_length`        |   ✅   | Rfc9000 a2 packet number length        |
|   8 | `test_rfc9000_a3_packet_number_decode`        |   ✅   | Rfc9000 a3 packet number decode        |
|   9 | `test_connection_id_bounds`                   |   ✅   | Connection id bounds                   |
|  10 | `test_truncated_headers_are_refused`          |   ✅   | Truncated headers are refused          |

</details>

---

## test_quic_packet - native_quic_packet_rfc - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for QUIC packet headers and packet-number coding_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_rfc9001_published_headers`              |   ✅   | Rfc9001 published headers              |
|   2 | `test_build_reproduces_the_published_headers` |   ✅   | Build reproduces the published headers |
|   3 | `test_rfc9000_long_packet_types`              |   ✅   | Rfc9000 long packet types              |
|   4 | `test_rfc9000_fixed_bit_is_required`          |   ✅   | Rfc9000 fixed bit is required          |
|   5 | `test_rfc9001_short_header`                   |   ✅   | Rfc9001 short header                   |
|   6 | `test_rfc9000_version_negotiation`            |   ✅   | Rfc9000 version negotiation            |
|   7 | `test_rfc9000_a2_packet_number_length`        |   ✅   | Rfc9000 a2 packet number length        |
|   8 | `test_rfc9000_a3_packet_number_decode`        |   ✅   | Rfc9000 a3 packet number decode        |
|   9 | `test_connection_id_bounds`                   |   ✅   | Connection id bounds                   |
|  10 | `test_truncated_headers_are_refused`          |   ✅   | Truncated headers are refused          |

</details>

---

## test_quic_tls - native_quic_tls - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS 1.3 server handshake state machine QUIC runs_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc8446_server_flight_order`          |   ✅   | Rfc8446 server flight order          |
|   2 | `test_rfc8446_server_hello_fields`          |   ✅   | Rfc8446 server hello fields          |
|   3 | `test_handshake_interop_round_trip`         |   ✅   | Handshake interop round trip         |
|   4 | `test_rfc9001_peer_transport_parameters`    |   ✅   | Rfc9001 peer transport parameters    |
|   5 | `test_negotiation_failures`                 |   ✅   | Negotiation failures                 |
|   6 | `test_rfc9001_missing_transport_parameters` |   ✅   | Rfc9001 missing transport parameters |
|   7 | `test_partial_crypto_is_not_consumed`       |   ✅   | Partial crypto is not consumed       |
|   8 | `test_message_at_the_wrong_level_or_state`  |   ✅   | Message at the wrong level or state  |
|   9 | `test_malformed_client_hello`               |   ✅   | Malformed client hello               |

</details>

---

## test_quic_tls - native_quic_tls_rfc - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS 1.3 server handshake state machine QUIC runs_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc8446_server_flight_order`          |   ✅   | Rfc8446 server flight order          |
|   2 | `test_rfc8446_server_hello_fields`          |   ✅   | Rfc8446 server hello fields          |
|   3 | `test_handshake_interop_round_trip`         |   ✅   | Handshake interop round trip         |
|   4 | `test_rfc9001_peer_transport_parameters`    |   ✅   | Rfc9001 peer transport parameters    |
|   5 | `test_negotiation_failures`                 |   ✅   | Negotiation failures                 |
|   6 | `test_rfc9001_missing_transport_parameters` |   ✅   | Rfc9001 missing transport parameters |
|   7 | `test_partial_crypto_is_not_consumed`       |   ✅   | Partial crypto is not consumed       |
|   8 | `test_message_at_the_wrong_level_or_state`  |   ✅   | Message at the wrong level or state  |
|   9 | `test_malformed_client_hello`               |   ✅   | Malformed client hello               |

</details>

---

## test_quic_tls - native_quic_tls_pqc - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS 1.3 server handshake state machine QUIC runs_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc8446_server_flight_order`          |   ✅   | Rfc8446 server flight order          |
|   2 | `test_rfc8446_server_hello_fields`          |   ✅   | Rfc8446 server hello fields          |
|   3 | `test_handshake_interop_round_trip`         |   ✅   | Handshake interop round trip         |
|   4 | `test_rfc9001_peer_transport_parameters`    |   ✅   | Rfc9001 peer transport parameters    |
|   5 | `test_negotiation_failures`                 |   ✅   | Negotiation failures                 |
|   6 | `test_rfc9001_missing_transport_parameters` |   ✅   | Rfc9001 missing transport parameters |
|   7 | `test_partial_crypto_is_not_consumed`       |   ✅   | Partial crypto is not consumed       |
|   8 | `test_message_at_the_wrong_level_or_state`  |   ✅   | Message at the wrong level or state  |
|   9 | `test_malformed_client_hello`               |   ✅   | Malformed client hello               |

</details>

---

## test_quic_tp - native_quic_tp - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the QUIC transport-parameter codec_

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_rfc9000_18_2_defaults`               |   ✅   | Rfc9000 18 2 defaults               |
|   2 | `test_hand_built_wire_string`              |   ✅   | Hand built wire string              |
|   3 | `test_encode_parse_round_trip`             |   ✅   | Encode parse round trip             |
|   4 | `test_migration_flag_is_absent_when_clear` |   ✅   | Migration flag is absent when clear |
|   5 | `test_reserved_ids_are_ignored`            |   ✅   | Reserved ids are ignored            |
|   6 | `test_out_of_range_values_are_rejected`    |   ✅   | Out of range values are rejected    |
|   7 | `test_oversized_connection_id_is_rejected` |   ✅   | Oversized connection id is rejected |
|   8 | `test_duplicate_parameter_is_rejected`     |   ✅   | Duplicate parameter is rejected     |
|   9 | `test_malformed_encoding_is_rejected`      |   ✅   | Malformed encoding is rejected      |
|  10 | `test_encode_refuses_a_short_buffer`       |   ✅   | Encode refuses a short buffer       |

</details>

---

## test_quic_varint - native_quic_varint - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the QUIC variable-length integer codec_

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_rfc9000_appendix_a1_vectors`           |   ✅   | Rfc9000 appendix a1 vectors           |
|   2 | `test_non_minimal_encoding_decodes`          |   ✅   | Non minimal encoding decodes          |
|   3 | `test_table4_length_boundaries`              |   ✅   | Table4 length boundaries              |
|   4 | `test_boundary_encodings_carry_their_prefix` |   ✅   | Boundary encodings carry their prefix |
|   5 | `test_above_the_62_bit_range_is_refused`     |   ✅   | Above the 62 bit range is refused     |
|   6 | `test_encode_refuses_a_short_buffer`         |   ✅   | Encode refuses a short buffer         |
|   7 | `test_decode_refuses_a_truncated_input`      |   ✅   | Decode refuses a truncated input      |
|   8 | `test_round_trip_over_every_length_class`    |   ✅   | Round trip over every length class    |

</details>

---

## test_radio_power - native_radio_power - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for 802.11 power management on a build with no radio_

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_ps_names_are_the_layers_own`                  |   ✅   | Ps names are the layers own                  |
|   2 | `test_ps_name_does_not_apply_the_mode`              |   ✅   | Ps name does not apply the mode              |
|   3 | `test_apply_refuses_without_a_radio`                |   ✅   | Apply refuses without a radio                |
|   4 | `test_monitor_refuses_without_a_radio`              |   ✅   | Monitor refuses without a radio              |
|   5 | `test_power_is_a_no_op_without_a_radio`             |   ✅   | Power is a no op without a radio             |
|   6 | `test_busy_hold_release_is_a_no_op_without_a_radio` |   ✅   | Busy hold release is a no op without a radio |
|   7 | `test_handle_is_bound`                              |   ✅   | Handle is bound                              |

</details>

---

## test_radio_power - native_l1_radio - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for 802.11 power management on a build with no radio_

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_ps_names_are_the_layers_own`                  |   ✅   | Ps names are the layers own                  |
|   2 | `test_ps_name_does_not_apply_the_mode`              |   ✅   | Ps name does not apply the mode              |
|   3 | `test_apply_refuses_without_a_radio`                |   ✅   | Apply refuses without a radio                |
|   4 | `test_monitor_refuses_without_a_radio`              |   ✅   | Monitor refuses without a radio              |
|   5 | `test_power_is_a_no_op_without_a_radio`             |   ✅   | Power is a no op without a radio             |
|   6 | `test_busy_hold_release_is_a_no_op_without_a_radio` |   ✅   | Busy hold release is a no op without a radio |
|   7 | `test_handle_is_bound`                              |   ✅   | Handle is bound                              |

</details>

---

## test_radio_sniff - native_radio_sniff - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the receive-only radio sniffer's pcap framing_

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_ieee754_binary32_rss_encoding`                 |   ✅   | Ieee754 binary32 rss encoding                 |
|   2 | `test_ieee754_binary32_wide_magnitude`               |   ✅   | Ieee754 binary32 wide magnitude               |
|   3 | `test_pcap_global_header_declares_the_tap_link_type` |   ✅   | Pcap global header declares the tap link type |
|   4 | `test_tap_record_layout`                             |   ✅   | Tap record layout                             |
|   5 | `test_tap_record_lengths_track_the_frame`            |   ✅   | Tap record lengths track the frame            |
|   6 | `test_tap_channel_assignment_is_sixteen_bits`        |   ✅   | Tap channel assignment is sixteen bits        |
|   7 | `test_tap_record_fails_closed`                       |   ✅   | Tap record fails closed                       |

</details>

---

## test_radio_sniff - native_radio_sniff_tap - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the receive-only radio sniffer's pcap framing_

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_ieee754_binary32_rss_encoding`                 |   ✅   | Ieee754 binary32 rss encoding                 |
|   2 | `test_ieee754_binary32_wide_magnitude`               |   ✅   | Ieee754 binary32 wide magnitude               |
|   3 | `test_pcap_global_header_declares_the_tap_link_type` |   ✅   | Pcap global header declares the tap link type |
|   4 | `test_tap_record_layout`                             |   ✅   | Tap record layout                             |
|   5 | `test_tap_record_lengths_track_the_frame`            |   ✅   | Tap record lengths track the frame            |
|   6 | `test_tap_channel_assignment_is_sixteen_bits`        |   ✅   | Tap channel assignment is sixteen bits        |
|   7 | `test_tap_record_fails_closed`                       |   ✅   | Tap record fails closed                       |

</details>

---

## test_rawl2 - native_rawl2 - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the raw Layer-2 Ethernet frame codec (services/fieldbus/rawl2/rawl2.h)._

|   # | Test                                     | Status | Description                       |
| --: | :--------------------------------------- | :----: | :-------------------------------- |
|   1 | `test_ethertype_registry`                |   ✅   | Ethertype registry                |
|   2 | `test_ethernet_ii_header_layout`         |   ✅   | Ethernet ii header layout         |
|   3 | `test_8021q_tag_layout`                  |   ✅   | 8021q tag layout                  |
|   4 | `test_vlan_tci_field_widths`             |   ✅   | Vlan tci field widths             |
|   5 | `test_tagged_and_untagged_stay_distinct` |   ✅   | Tagged and untagged stay distinct |
|   6 | `test_fcs_published_check_value`         |   ✅   | Fcs published check value         |
|   7 | `test_parse_refuses_a_short_frame`       |   ✅   | Parse refuses a short frame       |
|   8 | `test_build_refuses_bad_arguments`       |   ✅   | Build refuses bad arguments       |
|   9 | `test_payload_round_trip`                |   ✅   | Payload round trip                |

</details>

---

## test_rawmemcpy - native_rawmemcpy - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the raw access module (mmgr/rawmemcpy.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_word_rung_follows_the_declared_register_width`     |   ✅   | Word rung follows the declared register width     |
|   2 | `test_scalar_rungs_match_a_byte_loop`                    |   ✅   | Scalar rungs match a byte loop                    |
|   3 | `test_load_selects_a_rung_and_refuses_every_other_width` |   ✅   | Load selects a rung and refuses every other width |
|   4 | `test_put_round_trips_and_stays_inside_its_width`        |   ✅   | Put round trips and stays inside its width        |
|   5 | `test_aligned_rungs_agree_with_the_unaligned_ones`       |   ✅   | Aligned rungs agree with the unaligned ones       |
|   6 | `test_read_moves_every_offset_pair_and_length`           |   ✅   | Read moves every offset pair and length           |
|   7 | `test_read_carries_an_overlaid_header_struct`            |   ✅   | Read carries an overlaid header struct            |
|   8 | `test_the_table_is_wired_to_the_named_rungs`             |   ✅   | The table is wired to the named rungs             |

</details>

---

## test_rcwl0516 - native_rcwl0516 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the one-GPIO presence facade (server/peripherals/rcwl0516/rcwl0516.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_fresh_core_is_absent`                                 |   ✅   | Fresh core is absent                                 |
|   2 | `test_a_level_is_believed_only_after_the_debounce`          |   ✅   | A level is believed only after the debounce          |
|   3 | `test_chatter_below_the_debounce_is_swallowed`              |   ✅   | Chatter below the debounce is swallowed              |
|   4 | `test_hold_bridges_the_retrigger_gap`                       |   ✅   | Hold bridges the retrigger gap                       |
|   5 | `test_presence_clears_exactly_one_hold_after_the_last_high` |   ✅   | Presence clears exactly one hold after the last high |
|   6 | `test_event_is_taken_once_per_edge`                         |   ✅   | Event is taken once per edge                         |
|   7 | `test_zero_debounce_and_zero_hold_follow_the_level`         |   ✅   | Zero debounce and zero hold follow the level         |
|   8 | `test_zero_hold_still_debounces`                            |   ✅   | Zero hold still debounces                            |
|   9 | `test_timing_survives_the_millis_rollover`                  |   ✅   | Timing survives the millis rollover                  |
|  10 | `test_rcwl0516_defaults`                                    |   ✅   | Rcwl0516 defaults                                    |
|  11 | `test_repeated_timestamps_are_harmless`                     |   ✅   | Repeated timestamps are harmless                     |
|  12 | `test_null_core_is_refused`                                 |   ✅   | Null core is refused                                 |

</details>

---

## test_redis_resp - native_redis - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the RESP codec (services/iot/redis_resp/redis_resp.h)._

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_sending_commands_to_a_redis_server` |   ✅   | Sending commands to a redis server |
|   2 | `test_binary_safe_arguments`              |   ✅   | Binary safe arguments              |
|   3 | `test_simple_strings_and_errors`          |   ✅   | Simple strings and errors          |
|   4 | `test_integers`                           |   ✅   | Integers                           |
|   5 | `test_bulk_strings`                       |   ✅   | Bulk strings                       |
|   6 | `test_arrays_walk_element_by_element`     |   ✅   | Arrays walk element by element     |
|   7 | `test_resp3_simple_types`                 |   ✅   | Resp3 simple types                 |
|   8 | `test_bulk_errors_and_verbatim_strings`   |   ✅   | Bulk errors and verbatim strings   |
|   9 | `test_maps_report_two_children_per_entry` |   ✅   | Maps report two children per entry |
|  10 | `test_sets_and_pushes`                    |   ✅   | Sets and pushes                    |
|  11 | `test_unknown_first_bytes_are_refused`    |   ✅   | Unknown first bytes are refused    |
|  12 | `test_parse_waits_for_the_whole_value`    |   ✅   | Parse waits for the whole value    |
|  13 | `test_encode_fails_closed`                |   ✅   | Encode fails closed                |
|  14 | `test_multi_argument_command`             |   ✅   | Multi argument command             |

</details>

---

## test_redis_resp - native_redis_resp - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the RESP codec (services/iot/redis_resp/redis_resp.h)._

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_sending_commands_to_a_redis_server` |   ✅   | Sending commands to a redis server |
|   2 | `test_binary_safe_arguments`              |   ✅   | Binary safe arguments              |
|   3 | `test_simple_strings_and_errors`          |   ✅   | Simple strings and errors          |
|   4 | `test_integers`                           |   ✅   | Integers                           |
|   5 | `test_bulk_strings`                       |   ✅   | Bulk strings                       |
|   6 | `test_arrays_walk_element_by_element`     |   ✅   | Arrays walk element by element     |
|   7 | `test_resp3_simple_types`                 |   ✅   | Resp3 simple types                 |
|   8 | `test_bulk_errors_and_verbatim_strings`   |   ✅   | Bulk errors and verbatim strings   |
|   9 | `test_maps_report_two_children_per_entry` |   ✅   | Maps report two children per entry |
|  10 | `test_sets_and_pushes`                    |   ✅   | Sets and pushes                    |
|  11 | `test_unknown_first_bytes_are_refused`    |   ✅   | Unknown first bytes are refused    |
|  12 | `test_parse_waits_for_the_whole_value`    |   ✅   | Parse waits for the whole value    |
|  13 | `test_encode_fails_closed`                |   ✅   | Encode fails closed                |
|  14 | `test_multi_argument_command`             |   ✅   | Multi argument command             |

</details>

---

## test_relay - native_relay - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                 | Status | Description                   |
| --: | :----------------------------------- | :----: | :---------------------------- |
|   1 | `test_bidirectional`                 |   ✅   | Bidirectional                 |
|   2 | `test_backpressure`                  |   ✅   | Backpressure                  |
|   3 | `test_half_close_shutdown`           |   ✅   | Half close shutdown           |
|   4 | `test_send_error`                    |   ✅   | Send error                    |
|   5 | `test_one_way_idle_then_close`       |   ✅   | One way idle then close       |
|   6 | `test_note_eof_out_of_band`          |   ✅   | Note eof out of band          |
|   7 | `test_zero_length_read_no_progress`  |   ✅   | Zero length read no progress  |
|   8 | `test_flush_send_error`              |   ✅   | Flush send error              |
|   9 | `test_send_error_reverse_direction`  |   ✅   | Send error reverse direction  |
|  10 | `test_null_argument_guards`          |   ✅   | Null argument guards          |
|  11 | `test_shutdown_null_seam`            |   ✅   | Shutdown null seam            |
|  12 | `test_note_eof_with_backlog_pending` |   ✅   | Note eof with backlog pending |

</details>

---

## test_rfc1951 - native_rfc1951 - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the RFC 1951 code tables (network_drivers/presentation/codec/deflate/rfc1951.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_length_table_matches_rfc`                       |   ✅   | Length table matches rfc                       |
|   2 | `test_distance_table_matches_rfc`                     |   ✅   | Distance table matches rfc                     |
|   3 | `test_length_spans_are_contiguous`                    |   ✅   | Length spans are contiguous                    |
|   4 | `test_distance_spans_are_contiguous`                  |   ✅   | Distance spans are contiguous                  |
|   5 | `test_namespace_is_one_instance`                      |   ✅   | Namespace is one instance                      |
|   6 | `test_build_fixed_lengths_match_rfc`                  |   ✅   | Build fixed lengths match rfc                  |
|   7 | `test_build_fixed_codes_are_the_rfc_codes_reversed`   |   ✅   | Build fixed codes are the rfc codes reversed   |
|   8 | `test_reverse_bits_is_its_own_inverse`                |   ✅   | Reverse bits is its own inverse                |
|   9 | `test_emit_literal_puts_the_code_on_the_wire`         |   ✅   | Emit literal puts the code on the wire         |
|  10 | `test_emit_match_selects_the_code_for_the_span`       |   ✅   | Emit match selects the code for the span       |
|  11 | `test_emit_match_uses_the_single_length_code_for_258` |   ✅   | Emit match uses the single length code for 258 |
|  12 | `test_emit_match_writes_the_offset_in_the_extra_bits` |   ✅   | Emit match writes the offset in the extra bits |
|  13 | `test_emit_past_the_buffer_latches_overflow`          |   ✅   | Emit past the buffer latches overflow          |

</details>

---

## test_rfc1951 - native_codec_rfc1951 - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the RFC 1951 code tables (network_drivers/presentation/codec/deflate/rfc1951.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_length_table_matches_rfc`                       |   ✅   | Length table matches rfc                       |
|   2 | `test_distance_table_matches_rfc`                     |   ✅   | Distance table matches rfc                     |
|   3 | `test_length_spans_are_contiguous`                    |   ✅   | Length spans are contiguous                    |
|   4 | `test_distance_spans_are_contiguous`                  |   ✅   | Distance spans are contiguous                  |
|   5 | `test_namespace_is_one_instance`                      |   ✅   | Namespace is one instance                      |
|   6 | `test_build_fixed_lengths_match_rfc`                  |   ✅   | Build fixed lengths match rfc                  |
|   7 | `test_build_fixed_codes_are_the_rfc_codes_reversed`   |   ✅   | Build fixed codes are the rfc codes reversed   |
|   8 | `test_reverse_bits_is_its_own_inverse`                |   ✅   | Reverse bits is its own inverse                |
|   9 | `test_emit_literal_puts_the_code_on_the_wire`         |   ✅   | Emit literal puts the code on the wire         |
|  10 | `test_emit_match_selects_the_code_for_the_span`       |   ✅   | Emit match selects the code for the span       |
|  11 | `test_emit_match_uses_the_single_length_code_for_258` |   ✅   | Emit match uses the single length code for 258 |
|  12 | `test_emit_match_writes_the_offset_in_the_extra_bits` |   ✅   | Emit match writes the offset in the extra bits |
|  13 | `test_emit_past_the_buffer_latches_overflow`          |   ✅   | Emit past the buffer latches overflow          |

</details>

---

## test_ring - native_ring - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the shared SPSC ring primitive and its three views (mmgr/ring.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_wrap_is_the_modulo_a_power_of_two_capacity_defines`   |   ✅   | Wrap is the modulo a power of two capacity defines   |
|   2 | `test_available_and_free_partition_the_ring`                |   ✅   | Available and free partition the ring                |
|   3 | `test_read_byte_pops_in_fifo_order_and_reports_empty`       |   ✅   | Read byte pops in fifo order and reports empty       |
|   4 | `test_read_takes_what_is_asked_for_and_advances_the_tail`   |   ✅   | Read takes what is asked for and advances the tail   |
|   5 | `test_peek_does_not_consume_and_consume_advances`           |   ✅   | Peek does not consume and consume advances           |
|   6 | `test_a_span_across_the_wrap_reads_back_whole`              |   ✅   | A span across the wrap reads back whole              |
|   7 | `test_a_segment_is_invisible_until_it_is_published`         |   ✅   | A segment is invisible until it is published         |
|   8 | `test_segments_release_in_order_and_a_full_ring_refuses`    |   ✅   | Segments release in order and a full ring refuses    |
|   9 | `test_slot_take_is_won_by_exactly_one_caller`               |   ✅   | Slot take is won by exactly one caller               |
|  10 | `test_a_losing_hold_cannot_redirect_the_keepout`            |   ✅   | A losing hold cannot redirect the keepout            |
|  11 | `test_ready_is_marked_minus_held_within_the_count`          |   ✅   | Ready is marked minus held within the count          |
|  12 | `test_slot_next_is_the_lowest_set_and_minus_one_when_empty` |   ✅   | Slot next is the lowest set and minus one when empty |
|  13 | `test_an_out_of_range_slot_names_nothing`                   |   ✅   | An out of range slot names nothing                   |

</details>

---

## test_roaming - native_roaming - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Layer 2 roam decision (network_drivers/datalink/roaming.h)._

|   # | Test                                                                          | Status | Description                                                            |
| --: | :---------------------------------------------------------------------------- | :----: | :--------------------------------------------------------------------- |
|   1 | `test_a_neighbor_report_yields_bssid_and_channel`                             |   ✅   | A neighbor report yields bssid and channel                             |
|   2 | `test_other_element_ids_are_stepped_over`                                     |   ✅   | Other element ids are stepped over                                     |
|   3 | `test_a_truncated_or_short_element_yields_no_candidate`                       |   ✅   | A truncated or short element yields no candidate                       |
|   4 | `test_the_decode_stops_at_the_output_bound`                                   |   ✅   | The decode stops at the output bound                                   |
|   5 | `test_a_btm_request_decodes_its_request_mode`                                 |   ✅   | A btm request decodes its request mode                                 |
|   6 | `test_a_frame_that_is_not_a_btm_request_is_refused`                           |   ✅   | A frame that is not a btm request is refused                           |
|   7 | `test_a_btm_request_carries_its_preferred_candidate_past_the_optional_fields` |   ✅   | A btm request carries its preferred candidate past the optional fields |
|   8 | `test_disassociation_imminent_overrides_the_signal`                           |   ✅   | Disassociation imminent overrides the signal                           |
|   9 | `test_a_suggested_candidate_is_taken_only_when_it_is_no_weaker`               |   ✅   | A suggested candidate is taken only when it is no weaker               |
|  10 | `test_the_signal_transition_needs_the_threshold_and_the_margin`               |   ✅   | The signal transition needs the threshold and the margin               |
|  11 | `test_a_null_policy_takes_the_built_in_thresholds`                            |   ✅   | A null policy takes the built in thresholds                            |
|  12 | `test_the_serving_bss_is_never_the_target`                                    |   ✅   | The serving bss is never the target                                    |
|  13 | `test_no_serving_bssid_stays_and_clears_the_verdict`                          |   ✅   | No serving bssid stays and clears the verdict                          |

</details>

---

## test_robotics - native_robotics - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OPC UA for Robotics model (services/opcua/models/robotics/robotics.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_objects_folder_organizes_the_motion_device_system` |   ✅   | Objects folder organizes the motion device system |
|   2 | `test_motion_device_system_components`                   |   ✅   | Motion device system components                   |
|   3 | `test_folders_organize_their_members`                    |   ✅   | Folders organize their members                    |
|   4 | `test_motion_device_identity`                            |   ✅   | Motion device identity                            |
|   5 | `test_parameter_set_values`                              |   ✅   | Parameter set values                              |
|   6 | `test_axes_folder_follows_the_bound_axis_count`          |   ✅   | Axes folder follows the bound axis count          |
|   7 | `test_axis_variables_read_their_own_axis`                |   ✅   | Axis variables read their own axis                |
|   8 | `test_axis_beyond_the_bound_count_is_absent`             |   ✅   | Axis beyond the bound count is absent             |
|   9 | `test_controller_and_software`                           |   ✅   | Controller and software                           |
|  10 | `test_safety_state`                                      |   ✅   | Safety state                                      |
|  11 | `test_null_strings_read_as_empty`                        |   ✅   | Null strings read as empty                        |
|  12 | `test_reads_outside_the_model_are_refused`               |   ✅   | Reads outside the model are refused               |
|  13 | `test_nothing_is_served_before_bind`                     |   ✅   | Nothing is served before bind                     |
|  14 | `test_browse_respects_the_reference_cap`                 |   ✅   | Browse respects the reference cap                 |
|  15 | `test_every_reference_resolves`                          |   ✅   | Every reference resolves                          |

</details>

---

## test_rtc - native_rtc - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the DS1307 / DS3231 time-register conversion (server/peripherals/rtc/rtc.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_y2k_and_leap_days`                      |   ✅   | Y2k and leap days                      |
|   2 | `test_time_of_day_adds_to_the_date`           |   ✅   | Time of day adds to the date           |
|   3 | `test_twelve_hour_encoding`                   |   ✅   | Twelve hour encoding                   |
|   4 | `test_clock_halt_and_century_bits_are_masked` |   ✅   | Clock halt and century bits are masked |
|   5 | `test_out_of_range_fields_are_refused`        |   ✅   | Out of range fields are refused        |
|   6 | `test_epoch_to_regs_is_bcd_and_24_hour`       |   ✅   | Epoch to regs is bcd and 24 hour       |
|   7 | `test_day_of_week_from_the_epoch`             |   ✅   | Day of week from the epoch             |
|   8 | `test_round_trip_over_the_register_range`     |   ✅   | Round trip over the register range     |

</details>

---

## test_rtcm3 - native_rtcm3 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the RTCM 3 framing and station-reference codec_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_crc24q_residue_is_zero`                 |   ✅   | Crc24q residue is zero                 |
|   2 | `test_crc24q_is_linear_with_a_zero_seed`      |   ✅   | Crc24q is linear with a zero seed      |
|   3 | `test_frame_header_layout`                    |   ✅   | Frame header layout                    |
|   4 | `test_frame_parse_round_trip`                 |   ✅   | Frame parse round trip                 |
|   5 | `test_frame_parse_waits_for_the_whole_frame`  |   ✅   | Frame parse waits for the whole frame  |
|   6 | `test_frame_parse_reports_a_bad_crc`          |   ✅   | Frame parse reports a bad crc          |
|   7 | `test_sync_finds_the_next_preamble`           |   ✅   | Sync finds the next preamble           |
|   8 | `test_bit_writer_is_msb_first`                |   ✅   | Bit writer is msb first                |
|   9 | `test_bit_cursor_signed_fields`               |   ✅   | Bit cursor signed fields               |
|  10 | `test_message_1005_field_offsets`             |   ✅   | Message 1005 field offsets             |
|  11 | `test_message_1006_adds_the_antenna_height`   |   ✅   | Message 1006 adds the antenna height   |
|  12 | `test_ecef_coordinates_span_the_38_bit_range` |   ✅   | Ecef coordinates span the 38 bit range |
|  13 | `test_parse_1005_rejects_what_it_is_not`      |   ✅   | Parse 1005 rejects what it is not      |
|  14 | `test_build_capacity_is_respected`            |   ✅   | Build capacity is respected            |

</details>

---

## test_rtcm3 - native_gnss_rtcm3 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the RTCM 3 framing and station-reference codec_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_crc24q_residue_is_zero`                 |   ✅   | Crc24q residue is zero                 |
|   2 | `test_crc24q_is_linear_with_a_zero_seed`      |   ✅   | Crc24q is linear with a zero seed      |
|   3 | `test_frame_header_layout`                    |   ✅   | Frame header layout                    |
|   4 | `test_frame_parse_round_trip`                 |   ✅   | Frame parse round trip                 |
|   5 | `test_frame_parse_waits_for_the_whole_frame`  |   ✅   | Frame parse waits for the whole frame  |
|   6 | `test_frame_parse_reports_a_bad_crc`          |   ✅   | Frame parse reports a bad crc          |
|   7 | `test_sync_finds_the_next_preamble`           |   ✅   | Sync finds the next preamble           |
|   8 | `test_bit_writer_is_msb_first`                |   ✅   | Bit writer is msb first                |
|   9 | `test_bit_cursor_signed_fields`               |   ✅   | Bit cursor signed fields               |
|  10 | `test_message_1005_field_offsets`             |   ✅   | Message 1005 field offsets             |
|  11 | `test_message_1006_adds_the_antenna_height`   |   ✅   | Message 1006 adds the antenna height   |
|  12 | `test_ecef_coordinates_span_the_38_bit_range` |   ✅   | Ecef coordinates span the 38 bit range |
|  13 | `test_parse_1005_rejects_what_it_is_not`      |   ✅   | Parse 1005 rejects what it is not      |
|  14 | `test_build_capacity_is_respected`            |   ✅   | Build capacity is respected            |

</details>

---

## test_s7comm - native_s7comm - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Siemens S7comm PDU codec (services/fieldbus/s7comm/s7comm.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_dissector_constants`                       |   ✅   | Dissector constants                       |
|   2 | `test_setup_communication_job`                   |   ✅   | Setup communication job                   |
|   3 | `test_read_request_s7any_item`                   |   ✅   | Read request s7any item                   |
|   4 | `test_read_request_multiple_items`               |   ✅   | Read request multiple items               |
|   5 | `test_read_response_item_length_rule`            |   ✅   | Read response item length rule            |
|   6 | `test_read_response_even_padding`                |   ✅   | Read response even padding                |
|   7 | `test_write_request_round_trips_the_length_rule` |   ✅   | Write request round trips the length rule |
|   8 | `test_response_header_carries_the_error_code`    |   ✅   | Response header carries the error code    |
|   9 | `test_header_validation`                         |   ✅   | Header validation                         |
|  10 | `test_read_item_refuses_an_overrun`              |   ✅   | Read item refuses an overrun              |
|  11 | `test_builders_refuse_bad_arguments`             |   ✅   | Builders refuse bad arguments             |

</details>

---

## test_safety_scl - native_safety_scl - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEC 61784-3 black-channel Safety Communication Layer primitives_

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_init_starts_in_init_with_no_fault`            |   ✅   | Init starts in init with no fault            |
|   2 | `test_first_valid_frame_runs_the_connection`        |   ✅   | First valid frame runs the connection        |
|   3 | `test_every_black_channel_failure_is_detected`      |   ✅   | Every black channel failure is detected      |
|   4 | `test_corruption_is_diagnosed_before_the_counter`   |   ✅   | Corruption is diagnosed before the counter   |
|   5 | `test_failsafe_never_self_heals`                    |   ✅   | Failsafe never self heals                    |
|   6 | `test_watchdog_fires_at_the_limit`                  |   ✅   | Watchdog fires at the limit                  |
|   7 | `test_watchdog_does_not_run_before_the_first_frame` |   ✅   | Watchdog does not run before the first frame |
|   8 | `test_zero_watchdog_disables_the_check`             |   ✅   | Zero watchdog disables the check             |
|   9 | `test_watchdog_is_rollover_safe`                    |   ✅   | Watchdog is rollover safe                    |
|  10 | `test_counter_wraps_at_the_modulus`                 |   ✅   | Counter wraps at the modulus                 |
|  11 | `test_a_narrow_counter_still_catches_a_skip`        |   ✅   | A narrow counter still catches a skip        |
|  12 | `test_reset_re_establishes_and_keeps_the_tallies`   |   ✅   | Reset re establishes and keeps the tallies   |
|  13 | `test_a_null_connection_is_not_usable`              |   ✅   | A null connection is not usable              |
|  14 | `test_reset_honours_the_counter_modulus`            |   ✅   | Reset honours the counter modulus            |

</details>

---

## test_sb_modbus - native_sb_modbus - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                              | Status | Description                |
| --: | :-------------------------------- | :----: | :------------------------- |
|   1 | `test_read_single_holding`        |   ✅   | Read single holding        |
|   2 | `test_read_block_matrix`          |   ✅   | Read block matrix          |
|   3 | `test_read_input_registers`       |   ✅   | Read input registers       |
|   4 | `test_modbus_exception_surfaces`  |   ✅   | Modbus exception surfaces  |
|   5 | `test_transport_error_propagates` |   ✅   | Transport error propagates |
|   6 | `test_write_single_round_trip`    |   ✅   | Write single round trip    |
|   7 | `test_write_block_round_trip`     |   ✅   | Write block round trip     |
|   8 | `test_input_registers_read_only`  |   ✅   | Input registers read only  |
|   9 | `test_write_bounds`               |   ✅   | Write bounds               |
|  10 | `test_init_rejects_bad_args`      |   ✅   | Init rejects bad args      |
|  11 | `test_read_bounds`                |   ✅   | Read bounds                |
|  12 | `test_txid_increments`            |   ✅   | Txid increments            |

</details>

---

## test_scp - native_scp - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SCP (rcp) wire codec (network_drivers/application/scp/scp.h)._

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_mode_is_the_posix_permission_word_in_octal` |   ✅   | Mode is the posix permission word in octal |
|   2 | `test_mode_rejects_non_octal_digits`              |   ✅   | Mode rejects non octal digits              |
|   3 | `test_control_line_round_trip`                    |   ✅   | Control line round trip                    |
|   4 | `test_build_masks_the_file_type_bits`             |   ✅   | Build masks the file type bits             |
|   5 | `test_only_c_records_are_file_records`            |   ✅   | Only c records are file records            |
|   6 | `test_truncated_records_are_refused`              |   ✅   | Truncated records are refused              |
|   7 | `test_name_ends_at_the_newline_or_the_length`     |   ✅   | Name ends at the newline or the length     |
|   8 | `test_name_too_long_is_refused_not_truncated`     |   ✅   | Name too long is refused not truncated     |
|   9 | `test_mode_and_size_outputs_are_optional`         |   ✅   | Mode and size outputs are optional         |
|  10 | `test_build_refuses_a_short_buffer`               |   ✅   | Build refuses a short buffer               |
|  11 | `test_role_flag_selects_sink_or_source`           |   ✅   | Role flag selects sink or source           |
|  12 | `test_command_without_a_role_is_invalid`          |   ✅   | Command without a role is invalid          |
|  13 | `test_command_tokenizing`                         |   ✅   | Command tokenizing                         |
|  14 | `test_path_too_long_is_refused`                   |   ✅   | Path too long is refused                   |
|  15 | `test_command_null_arguments_are_refused`         |   ✅   | Command null arguments are refused         |
|  16 | `test_ack_octets`                                 |   ✅   | Ack octets                                 |

</details>

---

## test_scp - native_scp_wire - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SCP (rcp) wire codec (network_drivers/application/scp/scp.h)._

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_mode_is_the_posix_permission_word_in_octal` |   ✅   | Mode is the posix permission word in octal |
|   2 | `test_mode_rejects_non_octal_digits`              |   ✅   | Mode rejects non octal digits              |
|   3 | `test_control_line_round_trip`                    |   ✅   | Control line round trip                    |
|   4 | `test_build_masks_the_file_type_bits`             |   ✅   | Build masks the file type bits             |
|   5 | `test_only_c_records_are_file_records`            |   ✅   | Only c records are file records            |
|   6 | `test_truncated_records_are_refused`              |   ✅   | Truncated records are refused              |
|   7 | `test_name_ends_at_the_newline_or_the_length`     |   ✅   | Name ends at the newline or the length     |
|   8 | `test_name_too_long_is_refused_not_truncated`     |   ✅   | Name too long is refused not truncated     |
|   9 | `test_mode_and_size_outputs_are_optional`         |   ✅   | Mode and size outputs are optional         |
|  10 | `test_build_refuses_a_short_buffer`               |   ✅   | Build refuses a short buffer               |
|  11 | `test_role_flag_selects_sink_or_source`           |   ✅   | Role flag selects sink or source           |
|  12 | `test_command_without_a_role_is_invalid`          |   ✅   | Command without a role is invalid          |
|  13 | `test_command_tokenizing`                         |   ✅   | Command tokenizing                         |
|  14 | `test_path_too_long_is_refused`                   |   ✅   | Path too long is refused                   |
|  15 | `test_command_null_arguments_are_refused`         |   ✅   | Command null arguments are refused         |
|  16 | `test_ack_octets`                                 |   ✅   | Ack octets                                 |

</details>

---

## test_scpi - native_scpi - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SCPI / IEEE 488.2 codec (services/instrumentation/scpi/scpi.h)._

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_scpi99_exact_short_or_long_form_only`               |   ✅   | Scpi99 exact short or long form only               |
|   2 | `test_query_marker_and_depth_must_agree`                  |   ✅   | Query marker and depth must agree                  |
|   3 | `test_numeric_suffix_defaults_to_one`                     |   ✅   | Numeric suffix defaults to one                     |
|   4 | `test_leading_colon_and_parameters_are_ignored`           |   ✅   | Leading colon and parameters are ignored           |
|   5 | `test_common_commands`                                    |   ✅   | Common commands                                    |
|   6 | `test_command_line_form`                                  |   ✅   | Command line form                                  |
|   7 | `test_build_refuses_bad_arguments`                        |   ✅   | Build refuses bad arguments                        |
|   8 | `test_numeric_response_forms`                             |   ✅   | Numeric response forms                             |
|   9 | `test_scpi99_special_numeric_values`                      |   ✅   | Scpi99 special numeric values                      |
|  10 | `test_malformed_numbers_are_refused`                      |   ✅   | Malformed numbers are refused                      |
|  11 | `test_real_format_round_trips`                            |   ✅   | Real format round trips                            |
|  12 | `test_boolean_responses`                                  |   ✅   | Boolean responses                                  |
|  13 | `test_string_responses`                                   |   ✅   | String responses                                   |
|  14 | `test_ieee4882_definite_length_block`                     |   ✅   | Ieee4882 definite length block                     |
|  15 | `test_ieee4882_indefinite_length_block`                   |   ✅   | Ieee4882 indefinite length block                   |
|  16 | `test_block_refuses_malformed_and_truncated`              |   ✅   | Block refuses malformed and truncated              |
|  17 | `test_ieee4882_register_bit_positions`                    |   ✅   | Ieee4882 register bit positions                    |
|  18 | `test_scpi99_error_class_sets_its_esr_bit`                |   ✅   | Scpi99 error class sets its esr bit                |
|  19 | `test_scpi99_error_queue_is_fifo_and_empties_to_no_error` |   ✅   | Scpi99 error queue is fifo and empties to no error |
|  20 | `test_scpi99_queue_overflow_rule`                         |   ✅   | Scpi99 queue overflow rule                         |
|  21 | `test_scpi99_standard_error_messages`                     |   ✅   | Scpi99 standard error messages                     |
|  22 | `test_status_byte_summary_bits`                           |   ✅   | Status byte summary bits                           |
|  23 | `test_cls_clears_events_not_enables`                      |   ✅   | Cls clears events not enables                      |
|  24 | `test_status_calls_tolerate_null`                         |   ✅   | Status calls tolerate null                         |

</details>

---

## test_sdi12 - native_sdi12 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SDI-12 sensor-bus codec (server/peripherals/sdi12/sdi12.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_spec_crc_vectors`                              |   ✅   | Spec crc vectors                              |
|   2 | `test_crc16_arc_check_value`                         |   ✅   | Crc16 arc check value                         |
|   3 | `test_crc_encoding_is_always_printable`              |   ✅   | Crc encoding is always printable              |
|   4 | `test_corrupt_data_fails_the_crc`                    |   ✅   | Corrupt data fails the crc                    |
|   5 | `test_spec_command_set`                              |   ✅   | Spec command set                              |
|   6 | `test_spec_indexed_commands`                         |   ✅   | Spec indexed commands                         |
|   7 | `test_spec_measurement_responses`                    |   ✅   | Spec measurement responses                    |
|   8 | `test_spec_concurrent_response_has_two_count_digits` |   ✅   | Spec concurrent response has two count digits |
|   9 | `test_measurement_response_edges`                    |   ✅   | Measurement response edges                    |
|  10 | `test_spec_data_responses`                           |   ✅   | Spec data responses                           |
|  11 | `test_values_ignore_the_appended_crc`                |   ✅   | Values ignore the appended crc                |
|  12 | `test_values_are_bounded`                            |   ✅   | Values are bounded                            |
|  13 | `test_spec_identify_field_widths`                    |   ✅   | Spec identify field widths                    |
|  14 | `test_build_refuses_a_short_buffer`                  |   ✅   | Build refuses a short buffer                  |

</details>

---

## test_secure_pool - native_secure_pool - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the secure pool accessor (mmgr/secure.h)._

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_release_wipes_before_the_bytes_are_available_again` |   ✅   | Release wipes before the bytes are available again |
|   2 | `test_reset_wipes_every_live_borrow`                      |   ✅   | Reset wipes every live borrow                      |
|   3 | `test_a_scope_guard_wipes_on_every_exit_path`             |   ✅   | A scope guard wipes on every exit path             |
|   4 | `test_nested_scopes_reclaim_lifo`                         |   ✅   | Nested scopes reclaim lifo                         |
|   5 | `test_the_two_pools_are_disjoint_regions`                 |   ✅   | The two pools are disjoint regions                 |
|   6 | `test_a_pointer_from_neither_pool_belongs_to_neither`     |   ✅   | A pointer from neither pool belongs to neither     |
|   7 | `test_one_past_the_pool_is_not_owned`                     |   ✅   | One past the pool is not owned                     |
|   8 | `test_a_persistent_borrow_outlives_every_release`         |   ✅   | A persistent borrow outlives every release         |
|   9 | `test_high_water_records_peak_demand`                     |   ✅   | High water records peak demand                     |
|  10 | `test_an_over_budget_borrow_fails_closed`                 |   ✅   | An over budget borrow fails closed                 |
|  11 | `test_the_table_is_wired_to_the_named_functions`          |   ✅   | The table is wired to the named functions          |
|  12 | `test_the_pool_works_through_the_table`                   |   ✅   | The pool works through the table                   |

</details>

---

## test_sen0192 - native_sen0192 - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SEN0192 motion-presence tracker (server/peripherals/sen0192/sen0192.h)._

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_fresh_tracker_is_absent`                    |   ✅   | Fresh tracker is absent                    |
|   2 | `test_first_active_sample_is_the_only_edge`       |   ✅   | First active sample is the only edge       |
|   3 | `test_presence_is_held_then_clears_at_the_window` |   ✅   | Presence is held then clears at the window |
|   4 | `test_active_samples_extend_one_span`             |   ✅   | Active samples extend one span             |
|   5 | `test_events_count_arrivals`                      |   ✅   | Events count arrivals                      |
|   6 | `test_polarity_selects_the_active_level`          |   ✅   | Polarity selects the active level          |
|   7 | `test_active_age`                                 |   ✅   | Active age                                 |
|   8 | `test_timing_survives_the_millis_rollover`        |   ✅   | Timing survives the millis rollover        |
|   9 | `test_zero_hold_clears_on_the_next_tick`          |   ✅   | Zero hold clears on the next tick          |
|  10 | `test_repeated_timestamps_are_harmless`           |   ✅   | Repeated timestamps are harmless           |

</details>

---

## test_senml - native_senml - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SenML Pack builders and the Record resolver (services/iot/senml/senml.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_rfc8428_section_5_1_1_example`            |   ✅   | Rfc8428 section 5 1 1 example            |
|   2 | `test_rfc8428_section_5_1_2_example`            |   ✅   | Rfc8428 section 5 1 2 example            |
|   3 | `test_integral_numbers_are_written_as_integers` |   ✅   | Integral numbers are written as integers |
|   4 | `test_the_three_value_fields`                   |   ✅   | The three value fields                   |
|   5 | `test_cbor_table_4_integer_map_keys`            |   ✅   | Cbor table 4 integer map keys            |
|   6 | `test_cbor_non_integral_number_is_a_float`      |   ✅   | Cbor non integral number is a float      |
|   7 | `test_resolve_folds_base_name_and_base_time`    |   ✅   | Resolve folds base name and base time    |
|   8 | `test_resolve_overrides_and_absent_time`        |   ✅   | Resolve overrides and absent time        |
|   9 | `test_resolve_refuses_to_truncate_a_name`       |   ✅   | Resolve refuses to truncate a name       |
|  10 | `test_builders_report_zero_on_a_short_buffer`   |   ✅   | Builders report zero on a short buffer   |
|  11 | `test_missing_arguments_are_refused`            |   ✅   | Missing arguments are refused            |

</details>

---

## test_senml - native_senml_pack - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SenML Pack builders and the Record resolver (services/iot/senml/senml.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_rfc8428_section_5_1_1_example`            |   ✅   | Rfc8428 section 5 1 1 example            |
|   2 | `test_rfc8428_section_5_1_2_example`            |   ✅   | Rfc8428 section 5 1 2 example            |
|   3 | `test_integral_numbers_are_written_as_integers` |   ✅   | Integral numbers are written as integers |
|   4 | `test_the_three_value_fields`                   |   ✅   | The three value fields                   |
|   5 | `test_cbor_table_4_integer_map_keys`            |   ✅   | Cbor table 4 integer map keys            |
|   6 | `test_cbor_non_integral_number_is_a_float`      |   ✅   | Cbor non integral number is a float      |
|   7 | `test_resolve_folds_base_name_and_base_time`    |   ✅   | Resolve folds base name and base time    |
|   8 | `test_resolve_overrides_and_absent_time`        |   ✅   | Resolve overrides and absent time        |
|   9 | `test_resolve_refuses_to_truncate_a_name`       |   ✅   | Resolve refuses to truncate a name       |
|  10 | `test_builders_report_zero_on_a_short_buffer`   |   ✅   | Builders report zero on a short buffer   |
|  11 | `test_missing_arguments_are_refused`            |   ✅   | Missing arguments are refused            |

</details>

---

## test_sep2 - native_sep2 - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEEE 2030.5 (Smart Energy Profile 2.0) resource codec (services/energy/sep2/sep2.h)._

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_device_capability_document`         |   ✅   | Device capability document         |
|   2 | `test_end_device_document`                |   ✅   | End device document                |
|   3 | `test_der_control_document`               |   ✅   | Der control document               |
|   4 | `test_xml_special_characters_are_escaped` |   ✅   | Xml special characters are escaped |
|   5 | `test_null_strings_render_as_empty`       |   ✅   | Null strings render as empty       |
|   6 | `test_overflow_reports_zero`              |   ✅   | Overflow reports zero              |

</details>

---

## test_sercos - native_sercos - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_idn_bit_structure`                  |   ✅   | Idn bit structure                  |
|   2 | `test_idn_fields_do_not_overlap`          |   ✅   | Idn fields do not overlap          |
|   3 | `test_idn_round_trip_over_every_word`     |   ✅   | Idn round trip over every word     |
|   4 | `test_idn_parse_accepts_null_outputs`     |   ✅   | Idn parse accepts null outputs     |
|   5 | `test_telegram_round_trip`                |   ✅   | Telegram round trip                |
|   6 | `test_cycle_count_is_a_full_16_bit_field` |   ✅   | Cycle count is a full 16 bit field |
|   7 | `test_phase_octet_is_carried_whole`       |   ✅   | Phase octet is carried whole       |
|   8 | `test_only_mdt_and_at_are_accepted`       |   ✅   | Only mdt and at are accepted       |
|   9 | `test_bounds_refusals`                    |   ✅   | Bounds refusals                    |
|  10 | `test_mdt_at_exchange`                    |   ✅   | Mdt at exchange                    |

</details>

---

## test_sht3x - native_sht3x - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Sensirion SHT3x codec (server/peripherals/sht3x/sht3x.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_datasheet_crc_check_value`                |   ✅   | Datasheet crc check value                |
|   2 | `test_datasheet_temperature_formula`            |   ✅   | Datasheet temperature formula            |
|   3 | `test_datasheet_humidity_formula`               |   ✅   | Datasheet humidity formula               |
|   4 | `test_conversions_stay_inside_the_sensor_range` |   ✅   | Conversions stay inside the sensor range |
|   5 | `test_six_byte_response`                        |   ✅   | Six byte response                        |
|   6 | `test_corrupt_response_is_refused`              |   ✅   | Corrupt response is refused              |
|   7 | `test_datasheet_command_codes`                  |   ✅   | Datasheet command codes                  |

</details>

---

## test_sigfox - native_sigfox - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Sigfox modem AT-command codec (services/radio/sigfox/sigfox.h)._

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_sigfox_published_uplink_example`       |   ✅   | Sigfox published uplink example       |
|   2 | `test_hex_is_uppercase_and_msb_nibble_first` |   ✅   | Hex is uppercase and msb nibble first |
|   3 | `test_payload_cap_is_twelve_octets`          |   ✅   | Payload cap is twelve octets          |
|   4 | `test_build_fails_closed`                    |   ✅   | Build fails closed                    |
|   5 | `test_response_classification`               |   ✅   | Response classification               |
|   6 | `test_response_respects_the_stated_length`   |   ✅   | Response respects the stated length   |

</details>

---

## test_sigfox - native_sigfox_at - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Sigfox modem AT-command codec (services/radio/sigfox/sigfox.h)._

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_sigfox_published_uplink_example`       |   ✅   | Sigfox published uplink example       |
|   2 | `test_hex_is_uppercase_and_msb_nibble_first` |   ✅   | Hex is uppercase and msb nibble first |
|   3 | `test_payload_cap_is_twelve_octets`          |   ✅   | Payload cap is twelve octets          |
|   4 | `test_build_fails_closed`                    |   ✅   | Build fails closed                    |
|   5 | `test_response_classification`               |   ✅   | Response classification               |
|   6 | `test_response_respects_the_stated_length`   |   ✅   | Response respects the stated length   |

</details>

---

## test_signaling - native_signaling - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the server's signalling bucket (server/signaling/signaling.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc9110_first_digit_selects_the_class`     |   ✅   | Rfc9110 first digit selects the class     |
|   2 | `test_class_ranges_are_a_hundred_wide`           |   ✅   | Class ranges are a hundred wide           |
|   3 | `test_put_tick_replaces_rather_than_accumulates` |   ✅   | Put tick replaces rather than accumulates |
|   4 | `test_masks_carry_identity_as_well_as_count`     |   ✅   | Masks carry identity as well as count     |
|   5 | `test_know_hands_back_a_copy_not_a_window`       |   ✅   | Know hands back a copy not a window       |
|   6 | `test_reset_empties_every_field`                 |   ✅   | Reset empties every field                 |
|   7 | `test_a_read_with_no_destination_is_refused`     |   ✅   | A read with no destination is refused     |
|   8 | `test_kill_forwards_the_slot_unfiltered`         |   ✅   | Kill forwards the slot unfiltered         |
|   9 | `test_kill_deposits_nothing`                     |   ✅   | Kill deposits nothing                     |

</details>

---

## test_simatic - native_simatic - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Siemens SIMATIC serial link (services/fieldbus/simatic/simatic.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_bcc_covers_stuffed_data_and_terminator`   |   ✅   | Bcc covers stuffed data and terminator   |
|   2 | `test_doubled_dle_is_checksum_neutral`          |   ✅   | Doubled dle is checksum neutral          |
|   3 | `test_block_without_bcc_ends_at_dle_etx`        |   ✅   | Block without bcc ends at dle etx        |
|   4 | `test_block_round_trip_is_transparent`          |   ✅   | Block round trip is transparent          |
|   5 | `test_single_bit_flip_changes_the_bcc`          |   ✅   | Single bit flip changes the bcc          |
|   6 | `test_parse_refuses_a_wrong_bcc`                |   ✅   | Parse refuses a wrong bcc                |
|   7 | `test_parse_refuses_bad_framing`                |   ✅   | Parse refuses bad framing                |
|   8 | `test_bounds_are_refusals_not_truncations`      |   ✅   | Bounds are refusals not truncations      |
|   9 | `test_send_handshake_order`                     |   ✅   | Send handshake order                     |
|  10 | `test_receive_acks_and_delivers`                |   ✅   | Receive acks and delivers                |
|  11 | `test_receive_naks_a_bad_bcc`                   |   ✅   | Receive naks a bad bcc                   |
|  12 | `test_priority_arbitration_on_an_stx_collision` |   ✅   | Priority arbitration on an stx collision |
|  13 | `test_block_nak_retries_are_bounded`            |   ✅   | Block nak retries are bounded            |
|  14 | `test_qvz_timeout_retries_then_gives_up`        |   ✅   | Qvz timeout retries then gives up        |
|  15 | `test_zvz_inter_character_timeout_naks`         |   ✅   | Zvz inter character timeout naks         |
|  16 | `test_send_refuses_when_busy_or_unframeable`    |   ✅   | Send refuses when busy or unframeable    |
|  17 | `test_init_is_idle_and_null_callbacks_are_safe` |   ✅   | Init is idle and null callbacks are safe |
|  18 | `test_tick_while_idle_does_nothing`             |   ✅   | Tick while idle does nothing             |
|  19 | `test_rk512_words_are_big_endian`               |   ✅   | Rk512 words are big endian               |
|  20 | `test_rk512_header_round_trip`                  |   ✅   | Rk512 header round trip                  |
|  21 | `test_rk512_reaction_carries_status_and_data`   |   ✅   | Rk512 reaction carries status and data   |
|  22 | `test_rk512_parsers_fail_closed`                |   ✅   | Rk512 parsers fail closed                |
|  23 | `test_rk512_builders_refuse_a_short_buffer`     |   ✅   | Rk512 builders refuse a short buffer     |
|  24 | `test_rk512_telegram_survives_3964r_framing`    |   ✅   | Rk512 telegram survives 3964r framing    |

</details>

---

## test_sleep_sched - native_sleep_sched - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_awake_until_the_idle_threshold`                |   ✅   | Awake until the idle threshold                |
|   2 | `test_the_window_doubles_every_ramp`                 |   ✅   | The window doubles every ramp                 |
|   3 | `test_the_window_never_leaves_its_bounds`            |   ✅   | The window never leaves its bounds            |
|   4 | `test_the_window_is_monotonic_in_the_idle_streak`    |   ✅   | The window is monotonic in the idle streak    |
|   5 | `test_no_ramp_goes_straight_to_the_ceiling`          |   ✅   | No ramp goes straight to the ceiling          |
|   6 | `test_a_ceiling_below_the_floor_clamps_to_the_floor` |   ✅   | A ceiling below the floor clamps to the floor |
|   7 | `test_the_idle_streak_is_wrap_safe`                  |   ✅   | The idle streak is wrap safe                  |
|   8 | `test_a_null_config_stays_awake`                     |   ✅   | A null config stays awake                     |
|   9 | `test_a_zero_floor_still_yields_a_window`            |   ✅   | A zero floor still yields a window            |

</details>

---

## test_ntlmssp - native_smb - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NTLMSSP message codec (network_drivers/application/smb/ntlmssp.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_msnlmp_challenge_message`                       |   ✅   | Msnlmp challenge message                       |
|   2 | `test_negotiate_message_layout`                       |   ✅   | Negotiate message layout                       |
|   3 | `test_negotiate_flag_bits`                            |   ✅   | Negotiate flag bits                            |
|   4 | `test_msnlmp_authenticate_message`                    |   ✅   | Msnlmp authenticate message                    |
|   5 | `test_authenticate_with_mic_reserves_version_and_mic` |   ✅   | Authenticate with mic reserves version and mic |
|   6 | `test_challenge_parse_fails_closed`                   |   ✅   | Challenge parse fails closed                   |
|   7 | `test_challenge_without_target_info`                  |   ✅   | Challenge without target info                  |
|   8 | `test_authenticate_fails_closed`                      |   ✅   | Authenticate fails closed                      |
|   9 | `test_absent_identity_fields`                         |   ✅   | Absent identity fields                         |

</details>

---

## test_smb2 - native_smb - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SMB2 client wire codec (network_drivers/application/smb/smb2.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_msnlmp_smb2_header_layout`               |   ✅   | Msnlmp smb2 header layout               |
|   2 | `test_header_parse_fails_closed`               |   ✅   | Header parse fails closed               |
|   3 | `test_protocol_constants`                      |   ✅   | Protocol constants                      |
|   4 | `test_direct_tcp_transport_framing`            |   ✅   | Direct tcp transport framing            |
|   5 | `test_negotiate_request_body`                  |   ✅   | Negotiate request body                  |
|   6 | `test_negotiate_response_parse`                |   ✅   | Negotiate response parse                |
|   7 | `test_signing_round_trip_and_tamper_detection` |   ✅   | Signing round trip and tamper detection |
|   8 | `test_cmac_signing_is_a_distinct_algorithm`    |   ✅   | Cmac signing is a distinct algorithm    |
|   9 | `test_transform_header_constants`              |   ✅   | Transform header constants              |
|  10 | `test_transform_round_trip_for_every_cipher`   |   ✅   | Transform round trip for every cipher   |
|  11 | `test_transform_fails_closed`                  |   ✅   | Transform fails closed                  |
|  12 | `test_key_derivation_separates_its_outputs`    |   ✅   | Key derivation separates its outputs    |

</details>

---

## test_ntlm - native_smb - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NTLMv2 response computation (network_drivers/application/smb/ntlm.h)._

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_msnlmp_ntowfv2_worked_example`                   |   ✅   | Msnlmp ntowfv2 worked example                   |
|   2 | `test_nt_hash_is_the_published_ntowfv1`                |   ✅   | Nt hash is the published ntowfv1                |
|   3 | `test_msnlmp_ntlmv2_response_and_session_base_key`     |   ✅   | Msnlmp ntlmv2 response and session base key     |
|   4 | `test_only_the_user_is_uppercased`                     |   ✅   | Only the user is uppercased                     |
|   5 | `test_nt_hash_is_case_sensitive`                       |   ✅   | Nt hash is case sensitive                       |
|   6 | `test_response_length_is_forty_eight_plus_target_info` |   ✅   | Response length is forty eight plus target info |
|   7 | `test_timestamp_is_carried_and_bound_in`               |   ✅   | Timestamp is carried and bound in               |
|   8 | `test_server_challenge_is_bound_into_the_proof`        |   ✅   | Server challenge is bound into the proof        |
|   9 | `test_mic_flag_is_inserted_before_the_eol`             |   ✅   | Mic flag is inserted before the eol             |
|  10 | `test_mic_flag_is_ored_into_an_existing_pair`          |   ✅   | Mic flag is ored into an existing pair          |
|  11 | `test_mic_flag_changes_the_response_and_fails_closed`  |   ✅   | Mic flag changes the response and fails closed  |
|  12 | `test_mic_matches_the_rfc2202_hmac_md5_vectors`        |   ✅   | Mic matches the rfc2202 hmac md5 vectors        |
|  13 | `test_mic_binds_the_key_and_every_message`             |   ✅   | Mic binds the key and every message             |
|  14 | `test_ntowfv2_refuses_an_oversized_name_pair`          |   ✅   | Ntowfv2 refuses an oversized name pair          |

</details>

---

## test_spnego - native_smb - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SPNEGO DER wrapping of the NTLMSSP tokens_

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_first_token_is_an_rfc2743_initial_context_token`          |   ✅   | First token is an rfc2743 initial context token          |
|   2 | `test_second_token_is_a_bare_neg_token_resp`                    |   ✅   | Second token is a bare neg token resp                    |
|   3 | `test_response_token_is_found_after_negstate_and_supportedmech` |   ✅   | Response token is found after negstate and supportedmech |
|   4 | `test_wrap_then_parse_round_trip`                               |   ✅   | Wrap then parse round trip                               |
|   5 | `test_der_length_forms`                                         |   ✅   | Der length forms                                         |
|   6 | `test_parse_response_fails_closed`                              |   ✅   | Parse response fails closed                              |
|   7 | `test_wrappers_fail_closed`                                     |   ✅   | Wrappers fail closed                                     |
|   8 | `test_both_oids_appear_in_the_first_token`                      |   ✅   | Both oids appear in the first token                      |

</details>

---

## test_smb_crypto - native_smb - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MD-family digests NTLM needs (crypto/hash/md.h): MD4, MD5 and HMAC-MD5._

|   # | Test                              | Status | Description                |
| --: | :-------------------------------- | :----: | :------------------------- |
|   1 | `test_rfc1321_md5_suite`          |   ✅   | Rfc1321 md5 suite          |
|   2 | `test_rfc1320_md4_suite`          |   ✅   | Rfc1320 md4 suite          |
|   3 | `test_rfc2202_hmac_md5_cases`     |   ✅   | Rfc2202 hmac md5 cases     |
|   4 | `test_long_key_is_its_own_digest` |   ✅   | Long key is its own digest |
|   5 | `test_streaming_matches_one_shot` |   ✅   | Streaming matches one shot |
|   6 | `test_md4_and_md5_are_distinct`   |   ✅   | Md4 and md5 are distinct   |
|   7 | `test_block_boundary_lengths`     |   ✅   | Block boundary lengths     |

</details>

---

## test_smb_crypto - native_md_kat - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the MD-family digests NTLM needs (crypto/hash/md.h): MD4, MD5 and HMAC-MD5._

|   # | Test                              | Status | Description                |
| --: | :-------------------------------- | :----: | :------------------------- |
|   1 | `test_rfc1321_md5_suite`          |   ✅   | Rfc1321 md5 suite          |
|   2 | `test_rfc1320_md4_suite`          |   ✅   | Rfc1320 md4 suite          |
|   3 | `test_rfc2202_hmac_md5_cases`     |   ✅   | Rfc2202 hmac md5 cases     |
|   4 | `test_long_key_is_its_own_digest` |   ✅   | Long key is its own digest |
|   5 | `test_streaming_matches_one_shot` |   ✅   | Streaming matches one shot |
|   6 | `test_md4_and_md5_are_distinct`   |   ✅   | Md4 and md5 are distinct   |
|   7 | `test_block_boundary_lengths`     |   ✅   | Block boundary lengths     |

</details>

---

## test_ntlm - native_ntlm_v2 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NTLMv2 response computation (network_drivers/application/smb/ntlm.h)._

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_msnlmp_ntowfv2_worked_example`                   |   ✅   | Msnlmp ntowfv2 worked example                   |
|   2 | `test_nt_hash_is_the_published_ntowfv1`                |   ✅   | Nt hash is the published ntowfv1                |
|   3 | `test_msnlmp_ntlmv2_response_and_session_base_key`     |   ✅   | Msnlmp ntlmv2 response and session base key     |
|   4 | `test_only_the_user_is_uppercased`                     |   ✅   | Only the user is uppercased                     |
|   5 | `test_nt_hash_is_case_sensitive`                       |   ✅   | Nt hash is case sensitive                       |
|   6 | `test_response_length_is_forty_eight_plus_target_info` |   ✅   | Response length is forty eight plus target info |
|   7 | `test_timestamp_is_carried_and_bound_in`               |   ✅   | Timestamp is carried and bound in               |
|   8 | `test_server_challenge_is_bound_into_the_proof`        |   ✅   | Server challenge is bound into the proof        |
|   9 | `test_mic_flag_is_inserted_before_the_eol`             |   ✅   | Mic flag is inserted before the eol             |
|  10 | `test_mic_flag_is_ored_into_an_existing_pair`          |   ✅   | Mic flag is ored into an existing pair          |
|  11 | `test_mic_flag_changes_the_response_and_fails_closed`  |   ✅   | Mic flag changes the response and fails closed  |
|  12 | `test_mic_matches_the_rfc2202_hmac_md5_vectors`        |   ✅   | Mic matches the rfc2202 hmac md5 vectors        |
|  13 | `test_mic_binds_the_key_and_every_message`             |   ✅   | Mic binds the key and every message             |
|  14 | `test_ntowfv2_refuses_an_oversized_name_pair`          |   ✅   | Ntowfv2 refuses an oversized name pair          |

</details>

---

## test_ntlmssp - native_ntlmssp - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NTLMSSP message codec (network_drivers/application/smb/ntlmssp.h)._

|   # | Test                                                  | Status | Description                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------- |
|   1 | `test_msnlmp_challenge_message`                       |   ✅   | Msnlmp challenge message                       |
|   2 | `test_negotiate_message_layout`                       |   ✅   | Negotiate message layout                       |
|   3 | `test_negotiate_flag_bits`                            |   ✅   | Negotiate flag bits                            |
|   4 | `test_msnlmp_authenticate_message`                    |   ✅   | Msnlmp authenticate message                    |
|   5 | `test_authenticate_with_mic_reserves_version_and_mic` |   ✅   | Authenticate with mic reserves version and mic |
|   6 | `test_challenge_parse_fails_closed`                   |   ✅   | Challenge parse fails closed                   |
|   7 | `test_challenge_without_target_info`                  |   ✅   | Challenge without target info                  |
|   8 | `test_authenticate_fails_closed`                      |   ✅   | Authenticate fails closed                      |
|   9 | `test_absent_identity_fields`                         |   ✅   | Absent identity fields                         |

</details>

---

## test_spnego - native_spnego - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SPNEGO DER wrapping of the NTLMSSP tokens_

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_first_token_is_an_rfc2743_initial_context_token`          |   ✅   | First token is an rfc2743 initial context token          |
|   2 | `test_second_token_is_a_bare_neg_token_resp`                    |   ✅   | Second token is a bare neg token resp                    |
|   3 | `test_response_token_is_found_after_negstate_and_supportedmech` |   ✅   | Response token is found after negstate and supportedmech |
|   4 | `test_wrap_then_parse_round_trip`                               |   ✅   | Wrap then parse round trip                               |
|   5 | `test_der_length_forms`                                         |   ✅   | Der length forms                                         |
|   6 | `test_parse_response_fails_closed`                              |   ✅   | Parse response fails closed                              |
|   7 | `test_wrappers_fail_closed`                                     |   ✅   | Wrappers fail closed                                     |
|   8 | `test_both_oids_appear_in_the_first_token`                      |   ✅   | Both oids appear in the first token                      |

</details>

---

## test_smb2 - native_smb2_wire - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SMB2 client wire codec (network_drivers/application/smb/smb2.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_msnlmp_smb2_header_layout`               |   ✅   | Msnlmp smb2 header layout               |
|   2 | `test_header_parse_fails_closed`               |   ✅   | Header parse fails closed               |
|   3 | `test_protocol_constants`                      |   ✅   | Protocol constants                      |
|   4 | `test_direct_tcp_transport_framing`            |   ✅   | Direct tcp transport framing            |
|   5 | `test_negotiate_request_body`                  |   ✅   | Negotiate request body                  |
|   6 | `test_negotiate_response_parse`                |   ✅   | Negotiate response parse                |
|   7 | `test_signing_round_trip_and_tamper_detection` |   ✅   | Signing round trip and tamper detection |
|   8 | `test_cmac_signing_is_a_distinct_algorithm`    |   ✅   | Cmac signing is a distinct algorithm    |
|   9 | `test_transform_header_constants`              |   ✅   | Transform header constants              |
|  10 | `test_transform_round_trip_for_every_cipher`   |   ✅   | Transform round trip for every cipher   |
|  11 | `test_transform_fails_closed`                  |   ✅   | Transform fails closed                  |
|  12 | `test_key_derivation_separates_its_outputs`    |   ✅   | Key derivation separates its outputs    |

</details>

---

## test_pentest - native_smb_pentest - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Adversarial host tests for the SMB-family parsers that consume bytes off the wire_

|   # | Test                                                             | Status | Description                                               |
| --: | :--------------------------------------------------------------- | :----: | :-------------------------------------------------------- |
|   1 | `test_length_fields_never_reach_past_the_message`                |   ✅   | Length fields never reach past the message                |
|   2 | `test_random_bytes_terminate_and_stay_in_bounds`                 |   ✅   | Random bytes terminate and stay in bounds                 |
|   3 | `test_single_octet_mutations_of_a_valid_challenge`               |   ✅   | Single octet mutations of a valid challenge               |
|   4 | `test_truncation_at_every_length`                                |   ✅   | Truncation at every length                                |
|   5 | `test_hostile_der_terminates`                                    |   ✅   | Hostile der terminates                                    |
|   6 | `test_builders_never_write_past_the_capacity`                    |   ✅   | Builders never write past the capacity                    |
|   7 | `test_bounded_string_core_respects_its_caps`                     |   ✅   | Bounded string core respects its caps                     |
|   8 | `test_numeric_parsers_report_where_they_stopped`                 |   ✅   | Numeric parsers report where they stopped                 |
|   9 | `test_signature_verification_rejects_every_mutation`             |   ✅   | Signature verification rejects every mutation             |
|  10 | `test_encrypted_blobs_are_rejected_before_any_plaintext_appears` |   ✅   | Encrypted blobs are rejected before any plaintext appears |

</details>

---

## test_smbus - native_smbus - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SMBus 3.1 Packet Error Code (server/peripherals/smbus.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_addr_octet_carries_the_direction_bit`              |   ✅   | Addr octet carries the direction bit              |
|   2 | `test_pec_is_crc8_of_the_address_octet`                  |   ✅   | Pec is crc8 of the address octet                  |
|   3 | `test_pec_write_covers_address_then_payload`             |   ✅   | Pec write covers address then payload             |
|   4 | `test_pec_read_spans_both_halves_and_the_repeated_start` |   ✅   | Pec read spans both halves and the repeated start |
|   5 | `test_pec_read_without_a_command`                        |   ✅   | Pec read without a command                        |
|   6 | `test_pec_binds_to_the_address`                          |   ✅   | Pec binds to the address                          |
|   7 | `test_pec_binds_to_the_direction`                        |   ✅   | Pec binds to the direction                        |
|   8 | `test_pec_empty_payload_still_covers_the_address`        |   ✅   | Pec empty payload still covers the address        |
|   9 | `test_pec_holds_nothing_between_transactions`            |   ✅   | Pec holds nothing between transactions            |
|  10 | `test_pec_flag_round_trips`                              |   ✅   | Pec flag round trips                              |
|  11 | `test_write_shapes_put_their_own_octets_on_the_wire`     |   ✅   | Write shapes put their own octets on the wire     |
|  12 | `test_pec_octet_is_appended_to_a_write`                  |   ✅   | Pec octet is appended to a write                  |
|  13 | `test_block_write_counts_the_payload`                    |   ✅   | Block write counts the payload                    |
|  14 | `test_block_write_refuses_over_the_protocol_cap`         |   ✅   | Block write refuses over the protocol cap         |
|  15 | `test_read_shapes_take_their_octets_back`                |   ✅   | Read shapes take their octets back                |
|  16 | `test_reads_refuse_a_null_destination`                   |   ✅   | Reads refuse a null destination                   |
|  17 | `test_block_read_refuses_a_count_over_the_capacity`      |   ✅   | Block read refuses a count over the capacity      |
|  18 | `test_block_read_refuses_a_zero_count`                   |   ✅   | Block read refuses a zero count                   |
|  19 | `test_process_call_exchanges_a_word`                     |   ✅   | Process call exchanges a word                     |
|  20 | `test_a_slave_that_does_not_acknowledge_fails_the_shape` |   ✅   | A slave that does not acknowledge fails the shape |
|  21 | `test_a_wrong_pec_on_a_read_is_rejected`                 |   ✅   | A wrong pec on a read is rejected                 |

</details>

---

## test_snmp_ber - native_snmp - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SNMP ASN.1 BER codec (services/net/snmp/snmp_ber.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_x690_integer_minimal_octets`                 |   ✅   | X690 integer minimal octets                 |
|   2 | `test_x690_object_identifier_first_subidentifier`  |   ✅   | X690 object identifier first subidentifier  |
|   3 | `test_rfc3418_sysname_instance_oid`                |   ✅   | Rfc3418 sysname instance oid                |
|   4 | `test_x690_multi_octet_subidentifier`              |   ✅   | X690 multi octet subidentifier              |
|   5 | `test_rfc2578_application_types_stay_non_negative` |   ✅   | Rfc2578 application types stay non negative |
|   6 | `test_x690_octet_string_and_null`                  |   ✅   | X690 octet string and null                  |
|   7 | `test_x690_long_form_length`                       |   ✅   | X690 long form length                       |
|   8 | `test_rfc3417_definite_long_sequence`              |   ✅   | Rfc3417 definite long sequence              |
|   9 | `test_put_raw_appends_verbatim`                    |   ✅   | Put raw appends verbatim                    |
|  10 | `test_rfc3417_indefinite_length_is_refused`        |   ✅   | Rfc3417 indefinite length is refused        |
|  11 | `test_decoder_refuses_a_length_past_the_buffer`    |   ✅   | Decoder refuses a length past the buffer    |
|  12 | `test_read_integer_refuses_malformed`              |   ✅   | Read integer refuses malformed              |
|  13 | `test_read_integer_sign_extends`                   |   ✅   | Read integer sign extends                   |
|  14 | `test_encoder_fails_closed`                        |   ✅   | Encoder fails closed                        |
|  15 | `test_put_oid_bounds`                              |   ✅   | Put oid bounds                              |
|  16 | `test_read_oid_bounds`                             |   ✅   | Read oid bounds                             |
|  17 | `test_skip_bounds`                                 |   ✅   | Skip bounds                                 |
|  18 | `test_failed_decoder_stays_failed`                 |   ✅   | Failed decoder stays failed                 |
|  19 | `test_seq_end_refuses_over_65535_octets`           |   ✅   | Seq end refuses over 65535 octets           |

</details>

---

## test_snmp_ber - native_snmp_ber_x690 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SNMP ASN.1 BER codec (services/net/snmp/snmp_ber.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_x690_integer_minimal_octets`                 |   ✅   | X690 integer minimal octets                 |
|   2 | `test_x690_object_identifier_first_subidentifier`  |   ✅   | X690 object identifier first subidentifier  |
|   3 | `test_rfc3418_sysname_instance_oid`                |   ✅   | Rfc3418 sysname instance oid                |
|   4 | `test_x690_multi_octet_subidentifier`              |   ✅   | X690 multi octet subidentifier              |
|   5 | `test_rfc2578_application_types_stay_non_negative` |   ✅   | Rfc2578 application types stay non negative |
|   6 | `test_x690_octet_string_and_null`                  |   ✅   | X690 octet string and null                  |
|   7 | `test_x690_long_form_length`                       |   ✅   | X690 long form length                       |
|   8 | `test_rfc3417_definite_long_sequence`              |   ✅   | Rfc3417 definite long sequence              |
|   9 | `test_put_raw_appends_verbatim`                    |   ✅   | Put raw appends verbatim                    |
|  10 | `test_rfc3417_indefinite_length_is_refused`        |   ✅   | Rfc3417 indefinite length is refused        |
|  11 | `test_decoder_refuses_a_length_past_the_buffer`    |   ✅   | Decoder refuses a length past the buffer    |
|  12 | `test_read_integer_refuses_malformed`              |   ✅   | Read integer refuses malformed              |
|  13 | `test_read_integer_sign_extends`                   |   ✅   | Read integer sign extends                   |
|  14 | `test_encoder_fails_closed`                        |   ✅   | Encoder fails closed                        |
|  15 | `test_put_oid_bounds`                              |   ✅   | Put oid bounds                              |
|  16 | `test_read_oid_bounds`                             |   ✅   | Read oid bounds                             |
|  17 | `test_skip_bounds`                                 |   ✅   | Skip bounds                                 |
|  18 | `test_failed_decoder_stays_failed`                 |   ✅   | Failed decoder stays failed                 |
|  19 | `test_seq_end_refuses_over_65535_octets`           |   ✅   | Seq end refuses over 65535 octets           |

</details>

---

## test_snmp_trap - native_snmp_trap - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SNMP notification originator (services/net/snmp/snmp_notify.h)._

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc3416_trap_variable_bindings`       |   ✅   | Rfc3416 trap variable bindings       |
|   2 | `test_sysuptime_value_is_the_caller_uptime` |   ✅   | Sysuptime value is the caller uptime |
|   3 | `test_rfc3416_pdu_tags`                     |   ✅   | Rfc3416 pdu tags                     |
|   4 | `test_request_id_is_the_callers`            |   ✅   | Request id is the callers            |
|   5 | `test_rfc1901_message_wrapper`              |   ✅   | Rfc1901 message wrapper              |
|   6 | `test_rfc2578_varbind_value_tags`           |   ✅   | Rfc2578 varbind value tags           |
|   7 | `test_rfc2578_ipaddress_is_four_octets`     |   ✅   | Rfc2578 ipaddress is four octets     |
|   8 | `test_build_pdu_appends_to_an_open_encoder` |   ✅   | Build pdu appends to an open encoder |
|   9 | `test_short_buffer_writes_nothing`          |   ✅   | Short buffer writes nothing          |
|  10 | `test_unknown_varbind_type_fails_closed`    |   ✅   | Unknown varbind type fails closed    |
|  11 | `test_missing_arguments_are_refused`        |   ✅   | Missing arguments are refused        |
|  12 | `test_sends_report_no_transport`            |   ✅   | Sends report no transport            |

</details>

---

## test_snmp_trap - native_snmp_notify - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SNMP notification originator (services/net/snmp/snmp_notify.h)._

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_rfc3416_trap_variable_bindings`       |   ✅   | Rfc3416 trap variable bindings       |
|   2 | `test_sysuptime_value_is_the_caller_uptime` |   ✅   | Sysuptime value is the caller uptime |
|   3 | `test_rfc3416_pdu_tags`                     |   ✅   | Rfc3416 pdu tags                     |
|   4 | `test_request_id_is_the_callers`            |   ✅   | Request id is the callers            |
|   5 | `test_rfc1901_message_wrapper`              |   ✅   | Rfc1901 message wrapper              |
|   6 | `test_rfc2578_varbind_value_tags`           |   ✅   | Rfc2578 varbind value tags           |
|   7 | `test_rfc2578_ipaddress_is_four_octets`     |   ✅   | Rfc2578 ipaddress is four octets     |
|   8 | `test_build_pdu_appends_to_an_open_encoder` |   ✅   | Build pdu appends to an open encoder |
|   9 | `test_short_buffer_writes_nothing`          |   ✅   | Short buffer writes nothing          |
|  10 | `test_unknown_varbind_type_fails_closed`    |   ✅   | Unknown varbind type fails closed    |
|  11 | `test_missing_arguments_are_refused`        |   ✅   | Missing arguments are refused        |
|  12 | `test_sends_report_no_transport`            |   ✅   | Sends report no transport            |

</details>

---

## test_snp - native_snp - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the GE Fanuc SNP frame codec (services/fieldbus/snp/snp.h)._

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_published_x_attach_block_check_codes`     |   ✅   | Published x attach block check codes     |
|   2 | `test_rotate_wraps_the_top_bit_into_the_bottom` |   ✅   | Rotate wraps the top bit into the bottom |
|   3 | `test_empty_range_is_the_seed`                  |   ✅   | Empty range is the seed                  |
|   4 | `test_byte_order_changes_the_code`              |   ✅   | Byte order changes the code              |
|   5 | `test_control_characters_match_table_7_1`       |   ✅   | Control characters match table 7 1       |
|   6 | `test_frame_layout`                             |   ✅   | Frame layout                             |
|   7 | `test_round_trip`                               |   ✅   | Round trip                               |
|   8 | `test_any_single_bit_flip_is_refused`           |   ✅   | Any single bit flip is refused           |
|   9 | `test_truncation_is_refused`                    |   ✅   | Truncation is refused                    |
|  10 | `test_trailing_bytes_are_ignored`               |   ✅   | Trailing bytes are ignored               |
|  11 | `test_builder_refuses_bad_arguments`            |   ✅   | Builder refuses bad arguments            |
|  12 | `test_parser_refuses_bad_arguments`             |   ✅   | Parser refuses bad arguments             |

</details>

---

## test_sockpool - native_sockpool - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the LRU connection-slot pool (server/net/sockpool/sockpool.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_init_leaves_every_slot_free`                 |   ✅   | Init leaves every slot free                 |
|   2 | `test_acquire_takes_free_slots_first`              |   ✅   | Acquire takes free slots first              |
|   3 | `test_saturated_pool_evicts_in_last_used_order`    |   ✅   | Saturated pool evicts in last used order    |
|   4 | `test_touch_refreshes_the_lru_position`            |   ✅   | Touch refreshes the lru position            |
|   5 | `test_touch_of_a_free_slot_is_ignored`             |   ✅   | Touch of a free slot is ignored             |
|   6 | `test_release_returns_a_slot_to_the_free_list`     |   ✅   | Release returns a slot to the free list     |
|   7 | `test_release_refuses_a_free_or_out_of_range_slot` |   ✅   | Release refuses a free or out of range slot |
|   8 | `test_find_tracks_the_live_ids`                    |   ✅   | Find tracks the live ids                    |
|   9 | `test_find_follows_a_recycle`                      |   ✅   | Find follows a recycle                      |
|  10 | `test_out_parameters_are_optional`                 |   ✅   | Out parameters are optional                 |
|  11 | `test_a_pool_with_no_slots_fails_closed`           |   ✅   | A pool with no slots fails closed           |
|  12 | `test_in_use_never_exceeds_the_table`              |   ✅   | In use never exceeds the table              |

</details>

---

## test_southbound - native_southbound - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_register_and_find`                   |   ✅   | Register and find                   |
|   2 | `test_read_write_dispatch`                 |   ✅   | Read write dispatch                 |
|   3 | `test_block_atomic`                        |   ✅   | Block atomic                        |
|   4 | `test_unsupported_capability`              |   ✅   | Unsupported capability              |
|   5 | `test_registry_full`                       |   ✅   | Registry full                       |
|   6 | `test_dispatch_not_found_guards`           |   ✅   | Dispatch not found guards           |
|   7 | `test_find_null_name`                      |   ✅   | Find null name                      |
|   8 | `test_read_missing_capability`             |   ✅   | Read missing capability             |
|   9 | `test_find_skips_driver_mutated_name_null` |   ✅   | Find skips driver mutated name null |
|  10 | `test_block_not_found_and_arg_edges`       |   ✅   | Block not found and arg edges       |

</details>

---

## test_spa_router - native_spa_router - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for single-page-app routing and conditional UI streaming (server/web/spa_router/spa_router.h)._

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_extension_lives_in_the_last_segment`                 |   ✅   | Extension lives in the last segment                 |
|   2 | `test_root_serves_the_shell`                               |   ✅   | Root serves the shell                               |
|   3 | `test_api_prefix_passes_through`                           |   ✅   | Api prefix passes through                           |
|   4 | `test_client_routes_get_the_shell_and_assets_get_the_file` |   ✅   | Client routes get the shell and assets get the file |
|   5 | `test_route_ex_matches_plain_route_when_healthy`           |   ✅   | Route ex matches plain route when healthy           |
|   6 | `test_only_the_shell_decision_degrades`                    |   ✅   | Only the shell decision degrades                    |
|   7 | `test_stream_output_is_chunk_size_independent`             |   ✅   | Stream output is chunk size independent             |
|   8 | `test_predicates_run_as_the_stream_reaches_them`           |   ✅   | Predicates run as the stream reaches them           |
|   9 | `test_empty_and_all_skipped_streams_finish_immediately`    |   ✅   | Empty and all skipped streams finish immediately    |
|  10 | `test_null_fragment_body_is_skipped`                       |   ✅   | Null fragment body is skipped                       |
|  11 | `test_stream_refuses_bad_arguments`                        |   ✅   | Stream refuses bad arguments                        |

</details>

---

## test_span - native_span - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the bounded byte region (mmgr/span.h), driven by the byte verbs it was written for_

|   # | Test                                                         | Status | Description                                           |
| --: | :----------------------------------------------------------- | :----: | :---------------------------------------------------- |
|   1 | `test_the_capacity_is_the_constant_it_was_bound_with`        |   ✅   | The capacity is the constant it was bound with        |
|   2 | `test_an_empty_region_cannot_carry_a_live_capacity`          |   ✅   | An empty region cannot carry a live capacity          |
|   3 | `test_pos_reports_the_capacity_the_payload_needed`           |   ✅   | Pos reports the capacity the payload needed           |
|   4 | `test_reset_rewinds_and_clears_the_overflow`                 |   ✅   | Reset rewinds and clears the overflow                 |
|   5 | `test_after_clamps_rather_than_pointing_past_the_allocation` |   ✅   | After clamps rather than pointing past the allocation |
|   6 | `test_first_clamps_to_what_the_parent_holds`                 |   ✅   | First clamps to what the parent holds                 |
|   7 | `test_produced_is_the_cursor_and_is_empty_after_an_overflow` |   ✅   | Produced is the cursor and is empty after an overflow |
|   8 | `test_read_clamps_to_the_capacity`                           |   ✅   | Read clamps to the capacity                           |
|   9 | `test_the_region_round_trips_through_the_paired_verbs`       |   ✅   | The region round trips through the paired verbs       |
|  10 | `test_the_table_is_wired_to_the_named_accessors`             |   ✅   | The table is wired to the named accessors             |

</details>

---

## test_sparkplug - native_sparkplug - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Sparkplug B codec (services/iot/sparkplug/sparkplug.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_topic_namespace`                        |   ✅   | Topic namespace                        |
|   2 | `test_topic_every_message_type`               |   ✅   | Topic every message type               |
|   3 | `test_topic_refuses_a_missing_element`        |   ✅   | Topic refuses a missing element        |
|   4 | `test_topic_refuses_a_short_buffer`           |   ✅   | Topic refuses a short buffer           |
|   5 | `test_metric_wire_octets`                     |   ✅   | Metric wire octets                     |
|   6 | `test_metric_alias_varint`                    |   ✅   | Metric alias varint                    |
|   7 | `test_metric_string_value`                    |   ✅   | Metric string value                    |
|   8 | `test_metric_float_and_double_payloads`       |   ✅   | Metric float and double payloads       |
|   9 | `test_metric_boolean_payload`                 |   ✅   | Metric boolean payload                 |
|  10 | `test_payload_wire_octets`                    |   ✅   | Payload wire octets                    |
|  11 | `test_payload_round_trip`                     |   ✅   | Payload round trip                     |
|  12 | `test_decoded_strings_point_into_the_source`  |   ✅   | Decoded strings point into the source  |
|  13 | `test_parse_reports_absent_header_fields`     |   ✅   | Parse reports absent header fields     |
|  14 | `test_parse_rejects_a_truncated_varint`       |   ✅   | Parse rejects a truncated varint       |
|  15 | `test_next_metric_rejects_an_overlong_length` |   ✅   | Next metric rejects an overlong length |
|  16 | `test_build_refuses_a_short_buffer`           |   ✅   | Build refuses a short buffer           |
|  17 | `test_build_refuses_a_null_buffer`            |   ✅   | Build refuses a null buffer            |
|  18 | `test_payload_with_no_metrics`                |   ✅   | Payload with no metrics                |

</details>

---

## test_sqlite - native_sqlite - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SQLite3 on-disk file-format reader (services/storage/sqlite/sqlite_format.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_fileformat_magic_header_string`                   |   ✅   | Fileformat magic header string                   |
|   2 | `test_fileformat_database_header_offsets`               |   ✅   | Fileformat database header offsets               |
|   3 | `test_fileformat_page_size_encoding`                    |   ✅   | Fileformat page size encoding                    |
|   4 | `test_fileformat_varint_decoding`                       |   ✅   | Fileformat varint decoding                       |
|   5 | `test_fileformat_varint_length_boundaries`              |   ✅   | Fileformat varint length boundaries              |
|   6 | `test_fileformat_serial_type_content_sizes`             |   ✅   | Fileformat serial type content sizes             |
|   7 | `test_fileformat_btree_page_header_offsets`             |   ✅   | Fileformat btree page header offsets             |
|   8 | `test_fileformat_btree_page_type_domain`                |   ✅   | Fileformat btree page type domain                |
|   9 | `test_fileformat_cell_content_start_zero_means_65536`   |   ✅   | Fileformat cell content start zero means 65536   |
|  10 | `test_fileformat_cell_pointer_array`                    |   ✅   | Fileformat cell pointer array                    |
|  11 | `test_fileformat_schema_row`                            |   ✅   | Fileformat schema row                            |
|  12 | `test_fileformat_column_int_is_signextended_big_endian` |   ✅   | Fileformat column int is signextended big endian |
|  13 | `test_fileformat_column_float_is_big_endian_ieee754`    |   ✅   | Fileformat column float is big endian ieee754    |
|  14 | `test_table_cursor_walks_every_row_in_rowid_order`      |   ✅   | Table cursor walks every row in rowid order      |
|  15 | `test_overflow_chain_reassembly`                        |   ✅   | Overflow chain reassembly                        |
|  16 | `test_overflow_without_a_buffer_yields_the_prefix`      |   ✅   | Overflow without a buffer yields the prefix      |
|  17 | `test_table_cursor_refuses_an_unreadable_root`          |   ✅   | Table cursor refuses an unreadable root          |
|  18 | `test_table_cursor_walks_the_schema_table_on_page_one`  |   ✅   | Table cursor walks the schema table on page one  |
|  19 | `test_encode_record_round_trip`                         |   ✅   | Encode record round trip                         |
|  20 | `test_encode_record_picks_the_narrowest_integer_type`   |   ✅   | Encode record picks the narrowest integer type   |
|  21 | `test_build_table_db_writes_a_readable_file`            |   ✅   | Build table db writes a readable file            |
|  22 | `test_build_table_db_fails_closed`                      |   ✅   | Build table db fails closed                      |
|  23 | `test_record_cursor_refuses_a_malformed_record`         |   ✅   | Record cursor refuses a malformed record         |
|  24 | `test_leaf_cell_refuses_a_truncated_cell`               |   ✅   | Leaf cell refuses a truncated cell               |

</details>

---

## test_sqlite - native_storage_sqlite - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SQLite3 on-disk file-format reader (services/storage/sqlite/sqlite_format.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_fileformat_magic_header_string`                   |   ✅   | Fileformat magic header string                   |
|   2 | `test_fileformat_database_header_offsets`               |   ✅   | Fileformat database header offsets               |
|   3 | `test_fileformat_page_size_encoding`                    |   ✅   | Fileformat page size encoding                    |
|   4 | `test_fileformat_varint_decoding`                       |   ✅   | Fileformat varint decoding                       |
|   5 | `test_fileformat_varint_length_boundaries`              |   ✅   | Fileformat varint length boundaries              |
|   6 | `test_fileformat_serial_type_content_sizes`             |   ✅   | Fileformat serial type content sizes             |
|   7 | `test_fileformat_btree_page_header_offsets`             |   ✅   | Fileformat btree page header offsets             |
|   8 | `test_fileformat_btree_page_type_domain`                |   ✅   | Fileformat btree page type domain                |
|   9 | `test_fileformat_cell_content_start_zero_means_65536`   |   ✅   | Fileformat cell content start zero means 65536   |
|  10 | `test_fileformat_cell_pointer_array`                    |   ✅   | Fileformat cell pointer array                    |
|  11 | `test_fileformat_schema_row`                            |   ✅   | Fileformat schema row                            |
|  12 | `test_fileformat_column_int_is_signextended_big_endian` |   ✅   | Fileformat column int is signextended big endian |
|  13 | `test_fileformat_column_float_is_big_endian_ieee754`    |   ✅   | Fileformat column float is big endian ieee754    |
|  14 | `test_table_cursor_walks_every_row_in_rowid_order`      |   ✅   | Table cursor walks every row in rowid order      |
|  15 | `test_overflow_chain_reassembly`                        |   ✅   | Overflow chain reassembly                        |
|  16 | `test_overflow_without_a_buffer_yields_the_prefix`      |   ✅   | Overflow without a buffer yields the prefix      |
|  17 | `test_table_cursor_refuses_an_unreadable_root`          |   ✅   | Table cursor refuses an unreadable root          |
|  18 | `test_table_cursor_walks_the_schema_table_on_page_one`  |   ✅   | Table cursor walks the schema table on page one  |
|  19 | `test_encode_record_round_trip`                         |   ✅   | Encode record round trip                         |
|  20 | `test_encode_record_picks_the_narrowest_integer_type`   |   ✅   | Encode record picks the narrowest integer type   |
|  21 | `test_build_table_db_writes_a_readable_file`            |   ✅   | Build table db writes a readable file            |
|  22 | `test_build_table_db_fails_closed`                      |   ✅   | Build table db fails closed                      |
|  23 | `test_record_cursor_refuses_a_malformed_record`         |   ✅   | Record cursor refuses a malformed record         |
|  24 | `test_leaf_cell_refuses_a_truncated_cell`               |   ✅   | Leaf cell refuses a truncated cell               |

</details>

---

## test_ssh_aesgcm - native_ssh_aesgcm - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for AES-256-GCM (crypto/aead/aesgcm.h), the AEAD behind aes256-gcm@openssh.com._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_cavp_empty_plaintext_and_aad`              |   ✅   | Cavp empty plaintext and aad              |
|   2 | `test_cavp_aad_only_gmac`                        |   ✅   | Cavp aad only gmac                        |
|   3 | `test_cavp_one_block`                            |   ✅   | Cavp one block                            |
|   4 | `test_cavp_partial_final_block`                  |   ✅   | Cavp partial final block                  |
|   5 | `test_open_refuses_every_tampered_input`         |   ✅   | Open refuses every tampered input         |
|   6 | `test_aad_and_plaintext_are_not_interchangeable` |   ✅   | Aad and plaintext are not interchangeable |
|   7 | `test_rfc5647_invocation_counter_carries`        |   ✅   | Rfc5647 invocation counter carries        |
|   8 | `test_rfc5647_invocation_counter_steps`          |   ✅   | Rfc5647 invocation counter steps          |
|   9 | `test_stepped_nonce_changes_the_record`          |   ✅   | Stepped nonce changes the record          |
|  10 | `test_gctr_counter_byte_carry`                   |   ✅   | Gctr counter byte carry                   |
|  11 | `test_seal_in_place`                             |   ✅   | Seal in place                             |

</details>

---

## test_ssh_aesgcm - native_aesgcm_kat - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for AES-256-GCM (crypto/aead/aesgcm.h), the AEAD behind aes256-gcm@openssh.com._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_cavp_empty_plaintext_and_aad`              |   ✅   | Cavp empty plaintext and aad              |
|   2 | `test_cavp_aad_only_gmac`                        |   ✅   | Cavp aad only gmac                        |
|   3 | `test_cavp_one_block`                            |   ✅   | Cavp one block                            |
|   4 | `test_cavp_partial_final_block`                  |   ✅   | Cavp partial final block                  |
|   5 | `test_open_refuses_every_tampered_input`         |   ✅   | Open refuses every tampered input         |
|   6 | `test_aad_and_plaintext_are_not_interchangeable` |   ✅   | Aad and plaintext are not interchangeable |
|   7 | `test_rfc5647_invocation_counter_carries`        |   ✅   | Rfc5647 invocation counter carries        |
|   8 | `test_rfc5647_invocation_counter_steps`          |   ✅   | Rfc5647 invocation counter steps          |
|   9 | `test_stepped_nonce_changes_the_record`          |   ✅   | Stepped nonce changes the record          |
|  10 | `test_gctr_counter_byte_carry`                   |   ✅   | Gctr counter byte carry                   |
|  11 | `test_seal_in_place`                             |   ✅   | Seal in place                             |

</details>

---

## test_ssh_chachapoly - native_ssh_chachapoly - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for chacha20-poly1305@openssh.com (crypto/aead/chachapoly.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_rfc8439_keystream_is_applied_as_openssh_splits_it` |   ✅   | Rfc8439 keystream is applied as openssh splits it |
|   2 | `test_the_two_keys_are_not_interchangeable`              |   ✅   | The two keys are not interchangeable              |
|   3 | `test_sequence_number_is_the_nonce`                      |   ✅   | Sequence number is the nonce                      |
|   4 | `test_multi_block_payload_round_trip`                    |   ✅   | Multi block payload round trip                    |
|   5 | `test_decrypt_refuses_every_tampered_field`              |   ✅   | Decrypt refuses every tampered field              |
|   6 | `test_empty_payload`                                     |   ✅   | Empty payload                                     |
|   7 | `test_in_place_encrypt_and_decrypt`                      |   ✅   | In place encrypt and decrypt                      |
|   8 | `test_length_is_readable_before_the_body`                |   ✅   | Length is readable before the body                |

</details>

---

## test_ssh_chachapoly - native_chachapoly_kat - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for chacha20-poly1305@openssh.com (crypto/aead/chachapoly.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_rfc8439_keystream_is_applied_as_openssh_splits_it` |   ✅   | Rfc8439 keystream is applied as openssh splits it |
|   2 | `test_the_two_keys_are_not_interchangeable`              |   ✅   | The two keys are not interchangeable              |
|   3 | `test_sequence_number_is_the_nonce`                      |   ✅   | Sequence number is the nonce                      |
|   4 | `test_multi_block_payload_round_trip`                    |   ✅   | Multi block payload round trip                    |
|   5 | `test_decrypt_refuses_every_tampered_field`              |   ✅   | Decrypt refuses every tampered field              |
|   6 | `test_empty_payload`                                     |   ✅   | Empty payload                                     |
|   7 | `test_in_place_encrypt_and_decrypt`                      |   ✅   | In place encrypt and decrypt                      |
|   8 | `test_length_is_readable_before_the_body`                |   ✅   | Length is readable before the body                |

</details>

---

## test_ssh_ecdsa - native_ssh_ecdsa - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NIST P-256 primitives (crypto/asymmetric/ecdsa.h) behind_

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_rfc6979_public_point`                             |   ✅   | Rfc6979 public point                             |
|   2 | `test_rfc6979_deterministic_signatures`                 |   ✅   | Rfc6979 deterministic signatures                 |
|   3 | `test_rfc6979_signatures_verify`                        |   ✅   | Rfc6979 signatures verify                        |
|   4 | `test_verify_binds_signature_to_message`                |   ✅   | Verify binds signature to message                |
|   5 | `test_verify_refuses_tampering`                         |   ✅   | Verify refuses tampering                         |
|   6 | `test_verify_refuses_a_non_uncompressed_point`          |   ✅   | Verify refuses a non uncompressed point          |
|   7 | `test_verify_refuses_an_out_of_field_coordinate`        |   ✅   | Verify refuses an out of field coordinate        |
|   8 | `test_verify_refuses_r_or_s_outside_one_to_n_minus_one` |   ✅   | Verify refuses r or s outside one to n minus one |
|   9 | `test_private_scalar_bounds_are_enforced`               |   ✅   | Private scalar bounds are enforced               |
|  10 | `test_rfc5903_public_points`                            |   ✅   | Rfc5903 public points                            |
|  11 | `test_rfc5903_shared_secret`                            |   ✅   | Rfc5903 shared secret                            |
|  12 | `test_ecdh_refuses_an_invalid_peer_point`               |   ✅   | Ecdh refuses an invalid peer point               |
|  13 | `test_sign_verify_round_trip_over_many_keys`            |   ✅   | Sign verify round trip over many keys            |
|  14 | `test_ecdh_agrees_for_derived_keys`                     |   ✅   | Ecdh agrees for derived keys                     |

</details>

---

## test_ssh_ecdsa - native_p256_kat - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the NIST P-256 primitives (crypto/asymmetric/ecdsa.h) behind_

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_rfc6979_public_point`                             |   ✅   | Rfc6979 public point                             |
|   2 | `test_rfc6979_deterministic_signatures`                 |   ✅   | Rfc6979 deterministic signatures                 |
|   3 | `test_rfc6979_signatures_verify`                        |   ✅   | Rfc6979 signatures verify                        |
|   4 | `test_verify_binds_signature_to_message`                |   ✅   | Verify binds signature to message                |
|   5 | `test_verify_refuses_tampering`                         |   ✅   | Verify refuses tampering                         |
|   6 | `test_verify_refuses_a_non_uncompressed_point`          |   ✅   | Verify refuses a non uncompressed point          |
|   7 | `test_verify_refuses_an_out_of_field_coordinate`        |   ✅   | Verify refuses an out of field coordinate        |
|   8 | `test_verify_refuses_r_or_s_outside_one_to_n_minus_one` |   ✅   | Verify refuses r or s outside one to n minus one |
|   9 | `test_private_scalar_bounds_are_enforced`               |   ✅   | Private scalar bounds are enforced               |
|  10 | `test_rfc5903_public_points`                            |   ✅   | Rfc5903 public points                            |
|  11 | `test_rfc5903_shared_secret`                            |   ✅   | Rfc5903 shared secret                            |
|  12 | `test_ecdh_refuses_an_invalid_peer_point`               |   ✅   | Ecdh refuses an invalid peer point               |
|  13 | `test_sign_verify_round_trip_over_many_keys`            |   ✅   | Sign verify round trip over many keys            |
|  14 | `test_ecdh_agrees_for_derived_keys`                     |   ✅   | Ecdh agrees for derived keys                     |

</details>

---

## test_ssh_ed25519 - native_ssh_ed25519 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for Curve25519 (crypto/asymmetric/curve25519.h) and the Ed25519 signatures built on_

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_rfc7748_scalar_multiplication`            |   ✅   | Rfc7748 scalar multiplication            |
|   2 | `test_rfc7748_diffie_hellman_vector`            |   ✅   | Rfc7748 diffie hellman vector            |
|   3 | `test_rfc7748_high_bit_of_u_is_masked`          |   ✅   | Rfc7748 high bit of u is masked          |
|   4 | `test_small_order_points_yield_zero`            |   ✅   | Small order points yield zero            |
|   5 | `test_rfc8032_signature_vectors`                |   ✅   | Rfc8032 signature vectors                |
|   6 | `test_verify_refuses_tampering`                 |   ✅   | Verify refuses tampering                 |
|   7 | `test_verify_refuses_non_canonical_s`           |   ✅   | Verify refuses non canonical s           |
|   8 | `test_verify_refuses_an_undecodable_public_key` |   ✅   | Verify refuses an undecodable public key |
|   9 | `test_signing_is_deterministic_and_round_trips` |   ✅   | Signing is deterministic and round trips |
|  10 | `test_gf_pack_unpack_round_trip`                |   ✅   | Gf pack unpack round trip                |
|  11 | `test_gf_multiplication_identities`             |   ✅   | Gf multiplication identities             |
|  12 | `test_gf_add_sub_and_inverse`                   |   ✅   | Gf add sub and inverse                   |
|  13 | `test_gf_cswap`                                 |   ✅   | Gf cswap                                 |
|  14 | `test_gf_pack_reduces_to_canonical`             |   ✅   | Gf pack reduces to canonical             |

</details>

---

## test_ssh_ed25519 - native_curve25519_kat - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for Curve25519 (crypto/asymmetric/curve25519.h) and the Ed25519 signatures built on_

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_rfc7748_scalar_multiplication`            |   ✅   | Rfc7748 scalar multiplication            |
|   2 | `test_rfc7748_diffie_hellman_vector`            |   ✅   | Rfc7748 diffie hellman vector            |
|   3 | `test_rfc7748_high_bit_of_u_is_masked`          |   ✅   | Rfc7748 high bit of u is masked          |
|   4 | `test_small_order_points_yield_zero`            |   ✅   | Small order points yield zero            |
|   5 | `test_rfc8032_signature_vectors`                |   ✅   | Rfc8032 signature vectors                |
|   6 | `test_verify_refuses_tampering`                 |   ✅   | Verify refuses tampering                 |
|   7 | `test_verify_refuses_non_canonical_s`           |   ✅   | Verify refuses non canonical s           |
|   8 | `test_verify_refuses_an_undecodable_public_key` |   ✅   | Verify refuses an undecodable public key |
|   9 | `test_signing_is_deterministic_and_round_trips` |   ✅   | Signing is deterministic and round trips |
|  10 | `test_gf_pack_unpack_round_trip`                |   ✅   | Gf pack unpack round trip                |
|  11 | `test_gf_multiplication_identities`             |   ✅   | Gf multiplication identities             |
|  12 | `test_gf_add_sub_and_inverse`                   |   ✅   | Gf add sub and inverse                   |
|  13 | `test_gf_cswap`                                 |   ✅   | Gf cswap                                 |
|  14 | `test_gf_pack_reduces_to_canonical`             |   ✅   | Gf pack reduces to canonical             |

</details>

---

## test_ssh_sftp - native_ssh_sftp - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SFTP version 3 wire codec (network_drivers/application/sftp/sftp.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_packet_length_excludes_the_length_field`          |   ✅   | Packet length excludes the length field          |
|   2 | `test_protocol_constants`                               |   ✅   | Protocol constants                               |
|   3 | `test_status_response_layout`                           |   ✅   | Status response layout                           |
|   4 | `test_handle_and_data_responses`                        |   ✅   | Handle and data responses                        |
|   5 | `test_attrs_field_order_and_presence`                   |   ✅   | Attrs field order and presence                   |
|   6 | `test_attrs_round_trip`                                 |   ✅   | Attrs round trip                                 |
|   7 | `test_attrs_skips_extended_fields`                      |   ✅   | Attrs skips extended fields                      |
|   8 | `test_name_response_layout`                             |   ✅   | Name response layout                             |
|   9 | `test_frame_length`                                     |   ✅   | Frame length                                     |
|  10 | `test_reader_stays_failed_after_a_short_read`           |   ✅   | Reader stays failed after a short read           |
|  11 | `test_reader_refuses_a_string_longer_than_the_payload`  |   ✅   | Reader refuses a string longer than the payload  |
|  12 | `test_writer_overflow_is_final`                         |   ✅   | Writer overflow is final                         |
|  13 | `test_patch_u32_backfills_a_reserved_count`             |   ✅   | Patch u32 backfills a reserved count             |
|  14 | `test_longname_permission_column`                       |   ✅   | Longname permission column                       |
|  15 | `test_longname_ignores_the_file_type_bits`              |   ✅   | Longname ignores the file type bits              |
|  16 | `test_longname_carries_the_size_and_ends_with_the_name` |   ✅   | Longname carries the size and ends with the name |
|  17 | `test_longname_clips_to_the_buffer`                     |   ✅   | Longname clips to the buffer                     |

</details>

---

## test_ssh_sftp - native_sftp_wire - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SFTP version 3 wire codec (network_drivers/application/sftp/sftp.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_packet_length_excludes_the_length_field`          |   ✅   | Packet length excludes the length field          |
|   2 | `test_protocol_constants`                               |   ✅   | Protocol constants                               |
|   3 | `test_status_response_layout`                           |   ✅   | Status response layout                           |
|   4 | `test_handle_and_data_responses`                        |   ✅   | Handle and data responses                        |
|   5 | `test_attrs_field_order_and_presence`                   |   ✅   | Attrs field order and presence                   |
|   6 | `test_attrs_round_trip`                                 |   ✅   | Attrs round trip                                 |
|   7 | `test_attrs_skips_extended_fields`                      |   ✅   | Attrs skips extended fields                      |
|   8 | `test_name_response_layout`                             |   ✅   | Name response layout                             |
|   9 | `test_frame_length`                                     |   ✅   | Frame length                                     |
|  10 | `test_reader_stays_failed_after_a_short_read`           |   ✅   | Reader stays failed after a short read           |
|  11 | `test_reader_refuses_a_string_longer_than_the_payload`  |   ✅   | Reader refuses a string longer than the payload  |
|  12 | `test_writer_overflow_is_final`                         |   ✅   | Writer overflow is final                         |
|  13 | `test_patch_u32_backfills_a_reserved_count`             |   ✅   | Patch u32 backfills a reserved count             |
|  14 | `test_longname_permission_column`                       |   ✅   | Longname permission column                       |
|  15 | `test_longname_ignores_the_file_type_bits`              |   ✅   | Longname ignores the file type bits              |
|  16 | `test_longname_carries_the_size_and_ends_with_the_name` |   ✅   | Longname carries the size and ends with the name |
|  17 | `test_longname_clips_to_the_buffer`                     |   ✅   | Longname clips to the buffer                     |

</details>

---

## test_stomp - native_stomp - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the STOMP 1.2 frame codec (services/iot/stomp/stomp.h)._

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_published_error_frame`                |   ✅   | Published error frame                |
|   2 | `test_published_send_frame`                 |   ✅   | Published send frame                 |
|   3 | `test_repeated_header_first_entry_wins`     |   ✅   | Repeated header first entry wins     |
|   4 | `test_published_connect_frame`              |   ✅   | Published connect frame              |
|   5 | `test_build_emits_the_published_send_frame` |   ✅   | Build emits the published send frame |
|   6 | `test_build_minimal_frame`                  |   ✅   | Build minimal frame                  |
|   7 | `test_build_escapes_a_header`               |   ✅   | Build escapes a header               |
|   8 | `test_unescape_the_four_transformations`    |   ✅   | Unescape the four transformations    |
|   9 | `test_unescape_rejects_an_undefined_escape` |   ✅   | Unescape rejects an undefined escape |
|  10 | `test_eol_accepts_an_optional_cr`           |   ✅   | Eol accepts an optional cr           |
|  11 | `test_leading_eols_are_consumed`            |   ✅   | Leading eols are consumed            |
|  12 | `test_content_length_reads_null_octets`     |   ✅   | Content length reads null octets     |
|  13 | `test_content_length_must_land_on_the_null` |   ✅   | Content length must land on the null |
|  14 | `test_incomplete_frame_is_refused`          |   ✅   | Incomplete frame is refused          |
|  15 | `test_header_without_a_colon_is_refused`    |   ✅   | Header without a colon is refused    |
|  16 | `test_only_eols_is_not_a_frame`             |   ✅   | Only eols is not a frame             |
|  17 | `test_header_lookup_misses`                 |   ✅   | Header lookup misses                 |
|  18 | `test_build_refuses_a_short_buffer`         |   ✅   | Build refuses a short buffer         |
|  19 | `test_build_refuses_missing_arguments`      |   ✅   | Build refuses missing arguments      |
|  20 | `test_parse_slices_the_source`              |   ✅   | Parse slices the source              |
|  21 | `test_build_parse_round_trip`               |   ✅   | Build parse round trip               |

</details>

---

## test_sunspec - native_sunspec - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the SunSpec Modbus device-information-model codec (services/energy/sunspec/sunspec.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_sunspec_identifier_is_the_ascii_marker`        |   ✅   | Sunspec identifier is the ascii marker        |
|   2 | `test_begin_positions_past_the_two_marker_registers` |   ✅   | Begin positions past the two marker registers |
|   3 | `test_walks_the_model_chain_to_the_end_model`        |   ✅   | Walks the model chain to the end model        |
|   4 | `test_truncated_body_is_refused`                     |   ✅   | Truncated body is refused                     |
|   5 | `test_typed_point_readers_are_big_endian`            |   ✅   | Typed point readers are big endian            |
|   6 | `test_string_point_stops_at_the_nul_padding`         |   ✅   | String point stops at the nul padding         |
|   7 | `test_writer_and_walker_round_trip`                  |   ✅   | Writer and walker round trip                  |
|   8 | `test_end_model_is_two_registers`                    |   ✅   | End model is two registers                    |
|   9 | `test_write_string_is_clamped_to_the_field`          |   ✅   | Write string is clamped to the field          |
|  10 | `test_overflow_latches_and_finish_reports_zero`      |   ✅   | Overflow latches and finish reports zero      |
|  11 | `test_signed_points_round_trip_at_the_extremes`      |   ✅   | Signed points round trip at the extremes      |
|  12 | `test_next_model_guards`                             |   ✅   | Next model guards                             |

</details>

---

## test_swar - native_swar - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the lane math (mmgr/swar.h)._

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_the_lane_constants_derive_from_the_carrier_width`       |   ✅   | The lane constants derive from the carrier width       |
|   2 | `test_has_zero_is_exact_in_every_lane`                        |   ✅   | Has zero is exact in every lane                        |
|   3 | `test_zero_lane_is_the_first_in_address_order`                |   ✅   | Zero lane is the first in address order                |
|   4 | `test_eq_matches_equality_on_every_byte`                      |   ✅   | Eq matches equality on every byte                      |
|   5 | `test_eq_ci_matches_the_scalar_rule_on_every_byte`            |   ✅   | Eq ci matches the scalar rule on every byte            |
|   6 | `test_the_lane_tests_are_independent_across_lanes`            |   ✅   | The lane tests are independent across lanes            |
|   7 | `test_ge_and_le_match_the_scalar_compares_on_seven_bit_lanes` |   ✅   | Ge and le match the scalar compares on seven bit lanes |
|   8 | `test_spread_widens_a_guard_mask_without_carrying`            |   ✅   | Spread widens a guard mask without carrying            |
|   9 | `test_sub7_is_the_lane_local_subtraction`                     |   ✅   | Sub7 is the lane local subtraction                     |
|  10 | `test_the_two_loads_agree_where_both_are_legal`               |   ✅   | The two loads agree where both are legal               |
|  11 | `test_the_table_is_wired_to_the_named_lane_tests`             |   ✅   | The table is wired to the named lane tests             |

</details>

---

## test_telemetry - native_telemetry - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the sample aggregators (services/iot/telemetry/telemetry.h)._

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_window_mean_variance_stddev`             |   ✅   | Window mean variance stddev             |
|   2 | `test_window_evicts_the_oldest_sample`         |   ✅   | Window evicts the oldest sample         |
|   3 | `test_window_count_stops_at_capacity`          |   ✅   | Window count stops at capacity          |
|   4 | `test_window_variance_of_a_constant_is_zero`   |   ✅   | Window variance of a constant is zero   |
|   5 | `test_window_of_one_sample`                    |   ✅   | Window of one sample                    |
|   6 | `test_window_statistics_need_a_sample`         |   ✅   | Window statistics need a sample         |
|   7 | `test_window_init_empties_the_window`          |   ✅   | Window init empties the window          |
|   8 | `test_window_push_needs_bound_storage`         |   ✅   | Window push needs bound storage         |
|   9 | `test_rate_is_the_first_difference_per_second` |   ✅   | Rate is the first difference per second |
|  10 | `test_rate_scales_a_sub_second_interval`       |   ✅   | Rate scales a sub second interval       |
|  11 | `test_rate_of_a_zero_interval_is_zero`         |   ✅   | Rate of a zero interval is zero         |
|  12 | `test_rate_across_a_counter_rollover`          |   ✅   | Rate across a counter rollover          |
|  13 | `test_rate_init_drops_the_prior_sample`        |   ✅   | Rate init drops the prior sample        |
|  14 | `test_totalizer_is_the_trapezoidal_integral`   |   ✅   | Totalizer is the trapezoidal integral   |
|  15 | `test_totalizer_of_a_constant_rate`            |   ✅   | Totalizer of a constant rate            |
|  16 | `test_totalizer_integrates_a_negative_rate`    |   ✅   | Totalizer integrates a negative rate    |
|  17 | `test_totalizer_reset`                         |   ✅   | Totalizer reset                         |
|  18 | `test_totalizer_across_a_counter_rollover`     |   ✅   | Totalizer across a counter rollover     |
|  19 | `test_calls_refuse_a_null_accumulator`         |   ✅   | Calls refuse a null accumulator         |
|  20 | `test_two_windows_are_independent`             |   ✅   | Two windows are independent             |

</details>

---

## test_thread - native_thread - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Thread spinel / HDLC-lite framing codec (services/radio/thread/thread.h)._

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_x25_catalog_check_value`          |   ✅   | X25 catalog check value          |
|   2 | `test_rfc1662_good_fcs_residue`         |   ✅   | Rfc1662 good fcs residue         |
|   3 | `test_rfc1662_escape_table`             |   ✅   | Rfc1662 escape table             |
|   4 | `test_frame_round_trip`                 |   ✅   | Frame round trip                 |
|   5 | `test_decode_rejects_a_corrupted_frame` |   ✅   | Decode rejects a corrupted frame |
|   6 | `test_decode_framing_faults`            |   ✅   | Decode framing faults            |
|   7 | `test_encode_bounds`                    |   ✅   | Encode bounds                    |
|   8 | `test_spinel_packed_uint_vectors`       |   ✅   | Spinel packed uint vectors       |
|   9 | `test_spinel_packed_uint_faults`        |   ✅   | Spinel packed uint faults        |
|  10 | `test_spinel_header_bit_layout`         |   ✅   | Spinel header bit layout         |
|  11 | `test_spinel_command_build_and_parse`   |   ✅   | Spinel command build and parse   |
|  12 | `test_spinel_command_through_hdlc`      |   ✅   | Spinel command through hdlc      |
|  13 | `test_spinel_value_wire_layout`         |   ✅   | Spinel value wire layout         |
|  14 | `test_spinel_value_round_trip`          |   ✅   | Spinel value round trip          |
|  15 | `test_spinel_cursor_bounds_latch`       |   ✅   | Spinel cursor bounds latch       |
|  16 | `test_spinel_property_registry`         |   ✅   | Spinel property registry         |
|  17 | `test_spinel_status_names`              |   ✅   | Spinel status names              |
|  18 | `test_spinel_last_status_decode`        |   ✅   | Spinel last status decode        |
|  19 | `test_null_arguments_are_refused`       |   ✅   | Null arguments are refused       |

</details>

---

## test_thread - native_radio_thread - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Thread spinel / HDLC-lite framing codec (services/radio/thread/thread.h)._

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_x25_catalog_check_value`          |   ✅   | X25 catalog check value          |
|   2 | `test_rfc1662_good_fcs_residue`         |   ✅   | Rfc1662 good fcs residue         |
|   3 | `test_rfc1662_escape_table`             |   ✅   | Rfc1662 escape table             |
|   4 | `test_frame_round_trip`                 |   ✅   | Frame round trip                 |
|   5 | `test_decode_rejects_a_corrupted_frame` |   ✅   | Decode rejects a corrupted frame |
|   6 | `test_decode_framing_faults`            |   ✅   | Decode framing faults            |
|   7 | `test_encode_bounds`                    |   ✅   | Encode bounds                    |
|   8 | `test_spinel_packed_uint_vectors`       |   ✅   | Spinel packed uint vectors       |
|   9 | `test_spinel_packed_uint_faults`        |   ✅   | Spinel packed uint faults        |
|  10 | `test_spinel_header_bit_layout`         |   ✅   | Spinel header bit layout         |
|  11 | `test_spinel_command_build_and_parse`   |   ✅   | Spinel command build and parse   |
|  12 | `test_spinel_command_through_hdlc`      |   ✅   | Spinel command through hdlc      |
|  13 | `test_spinel_value_wire_layout`         |   ✅   | Spinel value wire layout         |
|  14 | `test_spinel_value_round_trip`          |   ✅   | Spinel value round trip          |
|  15 | `test_spinel_cursor_bounds_latch`       |   ✅   | Spinel cursor bounds latch       |
|  16 | `test_spinel_property_registry`         |   ✅   | Spinel property registry         |
|  17 | `test_spinel_status_names`              |   ✅   | Spinel status names              |
|  18 | `test_spinel_last_status_decode`        |   ✅   | Spinel last status decode        |
|  19 | `test_null_arguments_are_refused`       |   ✅   | Null arguments are refused       |

</details>

---

## test_time_compat - native_time_compat - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for reentrant broken-down UTC (shared/time_compat/time_compat.h)._

|   # | Test                                | Status | Description                  |
| --: | :---------------------------------- | :----: | :--------------------------- |
|   1 | `test_epoch_zero_is_the_definition` |   ✅   | Epoch zero is the definition |
|   2 | `test_fills_caller_storage`         |   ✅   | Fills caller storage         |
|   3 | `test_one_day_advances_one_date`    |   ✅   | One day advances one date    |
|   4 | `test_end_of_first_day`             |   ✅   | End of first day             |
|   5 | `test_signed_32_bit_limit`          |   ✅   | Signed 32 bit limit          |
|   6 | `test_leap_day_2000`                |   ✅   | Leap day 2000                |
|   7 | `test_null_destination_is_refused`  |   ✅   | Null destination is refused  |

</details>

---

## test_time_source - native_time_source - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the multi-source time fallback matrix (services/timing_position/time_source/time_source.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_rfc9110_date_from_the_active_source`          |   ✅   | Rfc9110 date from the active source          |
|   2 | `test_lowest_priority_value_is_queried_first`       |   ✅   | Lowest priority value is queried first       |
|   3 | `test_a_source_with_no_time_falls_through`          |   ✅   | A source with no time falls through          |
|   4 | `test_an_answer_stops_the_scan`                     |   ✅   | An answer stops the scan                     |
|   5 | `test_active_names_the_source_that_answered`        |   ✅   | Active names the source that answered        |
|   6 | `test_no_valid_time_reports_zero_and_clears_active` |   ✅   | No valid time reports zero and clears active |
|   7 | `test_empty_registry_and_null_callback`             |   ✅   | Empty registry and null callback             |
|   8 | `test_registry_is_bounded`                          |   ✅   | Registry is bounded                          |
|   9 | `test_reset_clears_the_registry`                    |   ✅   | Reset clears the registry                    |
|  10 | `test_http_date_is_empty_with_no_valid_time`        |   ✅   | Http date is empty with no valid time        |
|  11 | `test_http_date_refuses_a_short_buffer`             |   ✅   | Http date refuses a short buffer             |

</details>

---

## test_time_source - native_time_fallback - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the multi-source time fallback matrix (services/timing_position/time_source/time_source.h)._

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_rfc9110_date_from_the_active_source`          |   ✅   | Rfc9110 date from the active source          |
|   2 | `test_lowest_priority_value_is_queried_first`       |   ✅   | Lowest priority value is queried first       |
|   3 | `test_a_source_with_no_time_falls_through`          |   ✅   | A source with no time falls through          |
|   4 | `test_an_answer_stops_the_scan`                     |   ✅   | An answer stops the scan                     |
|   5 | `test_active_names_the_source_that_answered`        |   ✅   | Active names the source that answered        |
|   6 | `test_no_valid_time_reports_zero_and_clears_active` |   ✅   | No valid time reports zero and clears active |
|   7 | `test_empty_registry_and_null_callback`             |   ✅   | Empty registry and null callback             |
|   8 | `test_registry_is_bounded`                          |   ✅   | Registry is bounded                          |
|   9 | `test_reset_clears_the_registry`                    |   ✅   | Reset clears the registry                    |
|  10 | `test_http_date_is_empty_with_no_valid_time`        |   ✅   | Http date is empty with no valid time        |
|  11 | `test_http_date_refuses_a_short_buffer`             |   ✅   | Http date refuses a short buffer             |

</details>

---

## test_tls13_kdf - native_tls13_kdf - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS 1.3 key schedule (network_drivers/tls/key_schedule/key_schedule.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_transcript_hashes_match_the_trace`                    |   ✅   | Transcript hashes match the trace                    |
|   2 | `test_rfc8448_secret_chain`                                 |   ✅   | Rfc8448 secret chain                                 |
|   3 | `test_rfc8446_4_4_4_finished_mac`                           |   ✅   | Rfc8446 4 4 4 finished mac                           |
|   4 | `test_rfc8446_7_3_traffic_key_expansion`                    |   ✅   | Rfc8446 7 3 traffic key expansion                    |
|   5 | `test_derive_secret_matches_the_trace`                      |   ✅   | Derive secret matches the trace                      |
|   6 | `test_dtls_prefix_separates_the_schedules`                  |   ✅   | Dtls prefix separates the schedules                  |
|   7 | `test_a_different_ecdhe_gives_a_different_handshake_secret` |   ✅   | A different ecdhe gives a different handshake secret |
|   8 | `test_client_and_server_secrets_differ_at_every_level`      |   ✅   | Client and server secrets differ at every level      |
|   9 | `test_a_null_borrow_is_refused`                             |   ✅   | A null borrow is refused                             |

</details>

---

## test_tls13_msg - native_tls13_msg - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS 1.3 handshake message codec_

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_rfc8448_server_hello_bytes`                             |   ✅   | Rfc8448 server hello bytes                             |
|   2 | `test_server_hello_echoes_the_session_id`                     |   ✅   | Server hello echoes the session id                     |
|   3 | `test_rfc8448_client_hello_parse`                             |   ✅   | Rfc8448 client hello parse                             |
|   4 | `test_malformed_client_hello_is_refused`                      |   ✅   | Malformed client hello is refused                      |
|   5 | `test_rfc8446_4_4_3_signed_content`                           |   ✅   | Rfc8446 4 4 3 signed content                           |
|   6 | `test_cert_verify_signature_round_trip`                       |   ✅   | Cert verify signature round trip                       |
|   7 | `test_rfc8446_4_4_2_certificate_layout`                       |   ✅   | Rfc8446 4 4 2 certificate layout                       |
|   8 | `test_rfc8446_4_4_4_finished`                                 |   ✅   | Rfc8446 4 4 4 finished                                 |
|   9 | `test_rfc8446_4_4_1_message_hash`                             |   ✅   | Rfc8446 4 4 1 message hash                             |
|  10 | `test_rfc8446_4_1_3_hello_retry_request`                      |   ✅   | Rfc8446 4 1 3 hello retry request                      |
|  11 | `test_encrypted_extensions_carries_alpn_and_transport_params` |   ✅   | Encrypted extensions carries alpn and transport params |
|  12 | `test_quic_client_hello_extensions`                           |   ✅   | Quic client hello extensions                           |
|  13 | `test_builders_refuse_a_short_buffer`                         |   ✅   | Builders refuse a short buffer                         |

</details>

---

## test_tls_conn - native_tls_conn - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_full_handshake`                            |   ✅   | Full handshake                            |
|   2 | `test_a_client_outside_the_profile_is_refused`   |   ✅   | A client outside the profile is refused   |
|   3 | `test_malformed_and_out_of_order_messages_alert` |   ✅   | Malformed and out of order messages alert |
|   4 | `test_a_wrong_client_finished_is_decrypt_error`  |   ✅   | A wrong client finished is decrypt error  |
|   5 | `test_application_data_needs_the_handshake`      |   ✅   | Application data needs the handshake      |
|   6 | `test_the_client_role_refuses`                   |   ✅   | The client role refuses                   |

</details>

---

## test_tls_policy - native_tls_policy - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS version + cipher-suite policy (server/security/tls_policy/tls_policy.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_the_published_version_words`                          |   ✅   | The published version words                          |
|   2 | `test_negotiation_picks_the_highest_common_version`         |   ✅   | Negotiation picks the highest common version         |
|   3 | `test_a_future_client_gets_the_server_ceiling`              |   ✅   | A future client gets the server ceiling              |
|   4 | `test_a_client_below_the_floor_is_refused`                  |   ✅   | A client below the floor is refused                  |
|   5 | `test_an_inverted_server_range_negotiates_nothing`          |   ✅   | An inverted server range negotiates nothing          |
|   6 | `test_the_negotiated_version_is_always_inside_both_ranges`  |   ✅   | The negotiated version is always inside both ranges  |
|   7 | `test_version_names`                                        |   ✅   | Version names                                        |
|   8 | `test_selection_follows_server_preference_not_client_order` |   ✅   | Selection follows server preference not client order |
|   9 | `test_no_overlap_selects_nothing`                           |   ✅   | No overlap selects nothing                           |
|  10 | `test_selection_refuses_a_null_list`                        |   ✅   | Selection refuses a null list                        |
|  11 | `test_a_selected_suite_was_always_both_pinned_and_offered`  |   ✅   | A selected suite was always both pinned and offered  |
|  12 | `test_the_aead_suites`                                      |   ✅   | The aead suites                                      |
|  13 | `test_the_non_aead_suites`                                  |   ✅   | The non aead suites                                  |
|  14 | `test_an_aead_only_pin_can_only_select_aead`                |   ✅   | An aead only pin can only select aead                |

</details>

---

## test_tls_record - native_tls_record - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the TLS 1.3 record layer (network_drivers/tls/record/record.h)._

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_rfc8448_protected_records`                         |   ✅   | Rfc8448 protected records                         |
|   2 | `test_rfc8448_records_open_again`                        |   ✅   | Rfc8448 records open again                        |
|   3 | `test_rfc8446_5_1_plaintext_record`                      |   ✅   | Rfc8446 5 1 plaintext record                      |
|   4 | `test_plaintext_parse_waits_for_the_whole_record`        |   ✅   | Plaintext parse waits for the whole record        |
|   5 | `test_plaintext_build_bounds`                            |   ✅   | Plaintext build bounds                            |
|   6 | `test_rfc8446_5_2_outer_type_is_always_application_data` |   ✅   | Rfc8446 5 2 outer type is always application data |
|   7 | `test_rfc8446_5_2_padding_is_stripped`                   |   ✅   | Rfc8446 5 2 padding is stripped                   |
|   8 | `test_rfc8446_5_4_zero_length_inner_content`             |   ✅   | Rfc8446 5 4 zero length inner content             |
|   9 | `test_a_tampered_record_is_refused`                      |   ✅   | A tampered record is refused                      |
|  10 | `test_malformed_ciphertext_is_refused`                   |   ✅   | Malformed ciphertext is refused                   |
|  11 | `test_unkeyed_direction_fails_closed`                    |   ✅   | Unkeyed direction fails closed                    |
|  12 | `test_rederiving_restarts_the_sequence`                  |   ✅   | Rederiving restarts the sequence                  |

</details>

---

## test_totp - native_totp - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for one-time passwords (services/security/totp/totp.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc4226_hotp_test_values`                  |   ✅   | Rfc4226 hotp test values                  |
|   2 | `test_rfc4226_truncated_values_at_eight_digits`  |   ✅   | Rfc4226 truncated values at eight digits  |
|   3 | `test_digit_reduction_is_one_truncation`         |   ✅   | Digit reduction is one truncation         |
|   4 | `test_digit_zero_takes_the_minimum`              |   ✅   | Digit zero takes the minimum              |
|   5 | `test_rfc6238_totp_test_vectors`                 |   ✅   | Rfc6238 totp test vectors                 |
|   6 | `test_rfc6238_time_step_matches_the_published_t` |   ✅   | Rfc6238 time step matches the published t |
|   7 | `test_time_step_default_x`                       |   ✅   | Time step default x                       |
|   8 | `test_time_step_floors_within_a_step`            |   ✅   | Time step floors within a step            |
|   9 | `test_time_step_honors_x`                        |   ✅   | Time step honors x                        |
|  10 | `test_rfc6238_drift_window`                      |   ✅   | Rfc6238 drift window                      |
|  11 | `test_verify_refuses_what_it_should`             |   ✅   | Verify refuses what it should             |
|  12 | `test_verify_window_clamps_at_the_epoch`         |   ✅   | Verify window clamps at the epoch         |
|  13 | `test_rfc4648_base32_test_vectors`               |   ✅   | Rfc4648 base32 test vectors               |
|  14 | `test_base32_accepts_the_provisioning_spellings` |   ✅   | Base32 accepts the provisioning spellings |
|  15 | `test_base32_refuses_bad_input`                  |   ✅   | Base32 refuses bad input                  |
|  16 | `test_long_key_is_hashed_to_the_block`           |   ✅   | Long key is hashed to the block           |
|  17 | `test_counter_is_a_full_64_bit_moving_factor`    |   ✅   | Counter is a full 64 bit moving factor    |

</details>

---

## test_totp - native_security_totp - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for one-time passwords (services/security/totp/totp.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc4226_hotp_test_values`                  |   ✅   | Rfc4226 hotp test values                  |
|   2 | `test_rfc4226_truncated_values_at_eight_digits`  |   ✅   | Rfc4226 truncated values at eight digits  |
|   3 | `test_digit_reduction_is_one_truncation`         |   ✅   | Digit reduction is one truncation         |
|   4 | `test_digit_zero_takes_the_minimum`              |   ✅   | Digit zero takes the minimum              |
|   5 | `test_rfc6238_totp_test_vectors`                 |   ✅   | Rfc6238 totp test vectors                 |
|   6 | `test_rfc6238_time_step_matches_the_published_t` |   ✅   | Rfc6238 time step matches the published t |
|   7 | `test_time_step_default_x`                       |   ✅   | Time step default x                       |
|   8 | `test_time_step_floors_within_a_step`            |   ✅   | Time step floors within a step            |
|   9 | `test_time_step_honors_x`                        |   ✅   | Time step honors x                        |
|  10 | `test_rfc6238_drift_window`                      |   ✅   | Rfc6238 drift window                      |
|  11 | `test_verify_refuses_what_it_should`             |   ✅   | Verify refuses what it should             |
|  12 | `test_verify_window_clamps_at_the_epoch`         |   ✅   | Verify window clamps at the epoch         |
|  13 | `test_rfc4648_base32_test_vectors`               |   ✅   | Rfc4648 base32 test vectors               |
|  14 | `test_base32_accepts_the_provisioning_spellings` |   ✅   | Base32 accepts the provisioning spellings |
|  15 | `test_base32_refuses_bad_input`                  |   ✅   | Base32 refuses bad input                  |
|  16 | `test_long_key_is_hashed_to_the_block`           |   ✅   | Long key is hashed to the block           |
|  17 | `test_counter_is_a_full_64_bit_moving_factor`    |   ✅   | Counter is a full 64 bit moving factor    |

</details>

---

## test_trace_capture - native_trace_capture - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the pre/post-trigger window assembler (server/signaling/trace_capture.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_window_is_the_pre_roll_then_the_post_trigger_samples` |   ✅   | Window is the pre roll then the post trigger samples |
|   2 | `test_a_partly_filled_pre_roll_still_reads_oldest_first`    |   ✅   | A partly filled pre roll still reads oldest first    |
|   3 | `test_a_second_trigger_is_refused_and_counted`              |   ✅   | A second trigger is refused and counted              |
|   4 | `test_trace_id_counts_completed_windows`                    |   ✅   | Trace id counts completed windows                    |
|   5 | `test_arming_is_refused_when_it_cannot_be_honored`          |   ✅   | Arming is refused when it cannot be honored          |
|   6 | `test_samples_with_nothing_armed_are_counted_dropped`       |   ✅   | Samples with nothing armed are counted dropped       |
|   7 | `test_arming_clears_the_previous_capture`                   |   ✅   | Arming clears the previous capture                   |
|   8 | `test_a_capture_with_no_pre_roll_is_all_post_trigger`       |   ✅   | A capture with no pre roll is all post trigger       |
|   9 | `test_a_capture_with_no_post_trigger_never_completes`       |   ✅   | A capture with no post trigger never completes       |
|  10 | `test_the_sink_gets_the_context_it_was_armed_with`          |   ✅   | The sink gets the context it was armed with          |
|  11 | `test_a_stats_read_with_no_destination_is_refused`          |   ✅   | A stats read with no destination is refused          |

</details>

---

## test_concurrency - native_tsan - ✅ 2 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                         | Status | Description           |
| --: | :--------------------------- | :----: | :-------------------- |
|   1 | `test_spsc_ring_no_race`     |   ✅   | Spsc ring no race     |
|   2 | `test_state_handoff_no_race` |   ✅   | State handoff no race |

</details>

---

## test_ubx - native_ubx - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the u-blox UBX binary protocol codec (services/timing_position/ubx/ubx.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_published_poll_frames`                     |   ✅   | Published poll frames                     |
|   2 | `test_fletcher_checksum_over_the_published_span` |   ✅   | Fletcher checksum over the published span |
|   3 | `test_published_cfg_frames`                      |   ✅   | Published cfg frames                      |
|   4 | `test_length_field_is_little_endian`             |   ✅   | Length field is little endian             |
|   5 | `test_build_parse_round_trip`                    |   ✅   | Build parse round trip                    |
|   6 | `test_parse_refuses_a_corrupted_frame`           |   ✅   | Parse refuses a corrupted frame           |
|   7 | `test_parse_refuses_malformed_input`             |   ✅   | Parse refuses malformed input             |
|   8 | `test_build_bounds`                              |   ✅   | Build bounds                              |
|   9 | `test_ack_helper`                                |   ✅   | Ack helper                                |
|  10 | `test_little_endian_readers`                     |   ✅   | Little endian readers                     |
|  11 | `test_nav_pvt_published_field_offsets`           |   ✅   | Nav pvt published field offsets           |
|  12 | `test_nav_timeutc_published_field_offsets`       |   ✅   | Nav timeutc published field offsets       |
|  13 | `test_nav_sat_header_and_blocks`                 |   ✅   | Nav sat header and blocks                 |
|  14 | `test_stream_separates_nmea_from_ubx`            |   ✅   | Stream separates nmea from ubx            |
|  15 | `test_stream_doubled_sync1_still_opens_a_frame`  |   ✅   | Stream doubled sync1 still opens a frame  |
|  16 | `test_stream_discards_a_bad_checksum`            |   ✅   | Stream discards a bad checksum            |
|  17 | `test_stream_skips_an_over_long_frame`           |   ✅   | Stream skips an over long frame           |
|  18 | `test_stream_accepts_a_zero_length_frame`        |   ✅   | Stream accepts a zero length frame        |

</details>

---

## test_ubx - native_ubx_codec - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the u-blox UBX binary protocol codec (services/timing_position/ubx/ubx.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_published_poll_frames`                     |   ✅   | Published poll frames                     |
|   2 | `test_fletcher_checksum_over_the_published_span` |   ✅   | Fletcher checksum over the published span |
|   3 | `test_published_cfg_frames`                      |   ✅   | Published cfg frames                      |
|   4 | `test_length_field_is_little_endian`             |   ✅   | Length field is little endian             |
|   5 | `test_build_parse_round_trip`                    |   ✅   | Build parse round trip                    |
|   6 | `test_parse_refuses_a_corrupted_frame`           |   ✅   | Parse refuses a corrupted frame           |
|   7 | `test_parse_refuses_malformed_input`             |   ✅   | Parse refuses malformed input             |
|   8 | `test_build_bounds`                              |   ✅   | Build bounds                              |
|   9 | `test_ack_helper`                                |   ✅   | Ack helper                                |
|  10 | `test_little_endian_readers`                     |   ✅   | Little endian readers                     |
|  11 | `test_nav_pvt_published_field_offsets`           |   ✅   | Nav pvt published field offsets           |
|  12 | `test_nav_timeutc_published_field_offsets`       |   ✅   | Nav timeutc published field offsets       |
|  13 | `test_nav_sat_header_and_blocks`                 |   ✅   | Nav sat header and blocks                 |
|  14 | `test_stream_separates_nmea_from_ubx`            |   ✅   | Stream separates nmea from ubx            |
|  15 | `test_stream_doubled_sync1_still_opens_a_frame`  |   ✅   | Stream doubled sync1 still opens a frame  |
|  16 | `test_stream_discards_a_bad_checksum`            |   ✅   | Stream discards a bad checksum            |
|  17 | `test_stream_skips_an_over_long_frame`           |   ✅   | Stream skips an over long frame           |
|  18 | `test_stream_accepts_a_zero_length_frame`        |   ✅   | Stream accepts a zero length frame        |

</details>

---

## test_udp_telemetry - native_udp_telemetry - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the line protocol caster (services/iot/udp_telemetry/udp_telemetry.h)._

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_published_point`                       |   ✅   | Published point                       |
|   2 | `test_published_tag_escaping`                |   ✅   | Published tag escaping                |
|   3 | `test_tag_escapes_comma_and_equals`          |   ✅   | Tag escapes comma and equals          |
|   4 | `test_published_integer_fields`              |   ✅   | Published integer fields              |
|   5 | `test_published_unsigned_fields`             |   ✅   | Published unsigned fields             |
|   6 | `test_published_float_field`                 |   ✅   | Published float field                 |
|   7 | `test_field_set_separators`                  |   ✅   | Field set separators                  |
|   8 | `test_a_point_needs_a_field`                 |   ✅   | A point needs a field                 |
|   9 | `test_tag_after_a_field_is_refused`          |   ✅   | Tag after a field is refused          |
|  10 | `test_timestamp_before_any_field_is_refused` |   ✅   | Timestamp before any field is refused |
|  11 | `test_overflow_latches`                      |   ✅   | Overflow latches                      |
|  12 | `test_measurement_reopens_the_line`          |   ✅   | Measurement reopens the line          |
|  13 | `test_length_excludes_the_terminator`        |   ✅   | Length excludes the terminator        |
|  14 | `test_null_buffer_is_refused`                |   ✅   | Null buffer is refused                |
|  15 | `test_null_measurement_opens_an_empty_line`  |   ✅   | Null measurement opens an empty line  |
|  16 | `test_send_refuses_without_a_network_stack`  |   ✅   | Send refuses without a network stack  |
|  17 | `test_write_refuses_an_incomplete_line`      |   ✅   | Write refuses an incomplete line      |

</details>

---

## test_umati - native_umati - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the umati / OPC UA for Machine Tools model (services/opcua/models/umati/umati.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_objects_folder_organizes_the_machine_tool` |   ✅   | Objects folder organizes the machine tool |
|   2 | `test_machine_tool_components`                   |   ✅   | Machine tool components                   |
|   3 | `test_identification_variables`                  |   ✅   | Identification variables                  |
|   4 | `test_monitoring_sub_objects`                    |   ✅   | Monitoring sub objects                    |
|   5 | `test_axes_expose_one_position_each`             |   ✅   | Axes expose one position each             |
|   6 | `test_production_and_notification`               |   ✅   | Production and notification               |
|   7 | `test_null_strings_read_as_empty`                |   ✅   | Null strings read as empty                |
|   8 | `test_reads_outside_the_model_are_refused`       |   ✅   | Reads outside the model are refused       |
|   9 | `test_nothing_is_served_before_bind`             |   ✅   | Nothing is served before bind             |
|  10 | `test_browse_respects_the_reference_cap`         |   ✅   | Browse respects the reference cap         |
|  11 | `test_every_reference_resolves`                  |   ✅   | Every reference resolves                  |

</details>

---

## test_utf8 - native_utf8 - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for UTF-8 well-formedness (shared/utf8/utf8.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_shortest_form_boundaries_are_accepted`     |   ✅   | Shortest form boundaries are accepted     |
|   2 | `test_upper_boundaries_are_accepted`             |   ✅   | Upper boundaries are accepted             |
|   3 | `test_overlong_forms_are_refused`                |   ✅   | Overlong forms are refused                |
|   4 | `test_surrogates_are_refused`                    |   ✅   | Surrogates are refused                    |
|   5 | `test_above_max_code_point_is_refused`           |   ✅   | Above max code point is refused           |
|   6 | `test_stray_and_truncated_sequences_are_refused` |   ✅   | Stray and truncated sequences are refused |
|   7 | `test_mixed_text_is_accepted`                    |   ✅   | Mixed text is accepted                    |
|   8 | `test_length_bounds_the_walk`                    |   ✅   | Length bounds the walk                    |
|   9 | `test_empty_run_is_valid`                        |   ✅   | Empty run is valid                        |

</details>

---

## test_utmc - native_utmc - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the UTMC common-database codec (services/transportation/utmc/utmc.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_request_document`                                 |   ✅   | Request document                                 |
|   2 | `test_response_document`                                |   ✅   | Response document                                |
|   3 | `test_attribute_values_use_the_xml_predefined_entities` |   ✅   | Attribute values use the xml predefined entities |
|   4 | `test_quality_flag_renders_as_a_decimal`                |   ✅   | Quality flag renders as a decimal                |
|   5 | `test_request_round_trip`                               |   ✅   | Request round trip                               |
|   6 | `test_parse_returns_the_raw_attribute_text`             |   ✅   | Parse returns the raw attribute text             |
|   7 | `test_parse_accepts_an_empty_id`                        |   ✅   | Parse accepts an empty id                        |
|   8 | `test_parse_refuses_malformed_documents`                |   ✅   | Parse refuses malformed documents                |
|   9 | `test_parse_refuses_an_oversized_id`                    |   ✅   | Parse refuses an oversized id                    |
|  10 | `test_build_overflow_is_refused_whole`                  |   ✅   | Build overflow is refused whole                  |
|  11 | `test_null_text_renders_empty`                          |   ✅   | Null text renders empty                          |

</details>

---

## test_utmc - native_utmc_xml - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the UTMC common-database codec (services/transportation/utmc/utmc.h)._

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_request_document`                                 |   ✅   | Request document                                 |
|   2 | `test_response_document`                                |   ✅   | Response document                                |
|   3 | `test_attribute_values_use_the_xml_predefined_entities` |   ✅   | Attribute values use the xml predefined entities |
|   4 | `test_quality_flag_renders_as_a_decimal`                |   ✅   | Quality flag renders as a decimal                |
|   5 | `test_request_round_trip`                               |   ✅   | Request round trip                               |
|   6 | `test_parse_returns_the_raw_attribute_text`             |   ✅   | Parse returns the raw attribute text             |
|   7 | `test_parse_accepts_an_empty_id`                        |   ✅   | Parse accepts an empty id                        |
|   8 | `test_parse_refuses_malformed_documents`                |   ✅   | Parse refuses malformed documents                |
|   9 | `test_parse_refuses_an_oversized_id`                    |   ✅   | Parse refuses an oversized id                    |
|  10 | `test_build_overflow_is_refused_whole`                  |   ✅   | Build overflow is refused whole                  |
|  11 | `test_null_text_renders_empty`                          |   ✅   | Null text renders empty                          |

</details>

---

## test_vl53l0x - native_vl53l0x - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the VL53L0X time-of-flight ranging codec (server/peripherals/vl53l0x/vl53l0x.h)._

|   # | Test                                              | Status | Description                                |
| --: | :------------------------------------------------ | :----: | :----------------------------------------- |
|   1 | `test_range_is_the_big_endian_register_pair`      |   ✅   | Range is the big endian register pair      |
|   2 | `test_range_octets_never_overlap`                 |   ✅   | Range octets never overlap                 |
|   3 | `test_data_ready_is_the_low_three_interrupt_bits` |   ✅   | Data ready is the low three interrupt bits |
|   4 | `test_range_status_is_bits_6_to_3`                |   ✅   | Range status is bits 6 to 3                |
|   5 | `test_named_status_codes`                         |   ✅   | Named status codes                         |
|   6 | `test_register_map`                               |   ✅   | Register map                               |
|   7 | `test_begin_refuses_a_wrong_model_id`             |   ✅   | Begin refuses a wrong model id             |
|   8 | `test_begin_arms_continuous_ranging`              |   ✅   | Begin arms continuous ranging              |
|   9 | `test_read_refuses_when_no_measurement_is_ready`  |   ✅   | Read refuses when no measurement is ready  |
|  10 | `test_read_takes_the_distance_from_offset_ten`    |   ✅   | Read takes the distance from offset ten    |
|  11 | `test_read_refuses_an_invalid_status`             |   ✅   | Read refuses an invalid status             |
|  12 | `test_read_refuses_a_null_destination`            |   ✅   | Read refuses a null destination            |

</details>

---

## test_vxi11 - native_vxi11 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the VXI-11 codec over ONC RPC / XDR (services/instrumentation/vxi11/vxi11.h)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_destroy_link_call_is_byte_exact`             |   ✅   | Destroy link call is byte exact             |
|   2 | `test_rfc1833_getport_call_is_byte_exact`          |   ✅   | Rfc1833 getport call is byte exact          |
|   3 | `test_rfc5531_record_marking`                      |   ✅   | Rfc5531 record marking                      |
|   4 | `test_create_link_call_pads_the_device_string`     |   ✅   | Create link call pads the device string     |
|   5 | `test_device_write_parameter_order`                |   ✅   | Device write parameter order                |
|   6 | `test_device_read_parameter_order`                 |   ✅   | Device read parameter order                 |
|   7 | `test_generic_parms_calls_share_a_layout`          |   ✅   | Generic parms calls share a layout          |
|   8 | `test_rfc5531_accepted_reply_header`               |   ✅   | Rfc5531 accepted reply header               |
|   9 | `test_a_non_success_accept_stat_yields_no_results` |   ✅   | A non success accept stat yields no results |
|  10 | `test_getport_reply`                               |   ✅   | Getport reply                               |
|  11 | `test_create_link_reply`                           |   ✅   | Create link reply                           |
|  12 | `test_device_read_reply`                           |   ✅   | Device read reply                           |
|  13 | `test_write_and_readstb_replies`                   |   ✅   | Write and readstb replies                   |
|  14 | `test_bare_device_error_reply`                     |   ✅   | Bare device error reply                     |
|  15 | `test_error_codes_have_distinct_descriptions`      |   ✅   | Error codes have distinct descriptions      |
|  16 | `test_device_flags_bits`                           |   ✅   | Device flags bits                           |
|  17 | `test_builders_refuse_a_short_buffer`              |   ✅   | Builders refuse a short buffer              |

</details>

---

## test_wal_store - native_wal - ✅ 35 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_format_then_mount_empty`                     |   ✅   | Format then mount empty                     |
|   2 | `test_mount_unformatted_fails`                     |   ✅   | Mount unformatted fails                     |
|   3 | `test_append_without_checkpoint_recovers_via_tail` |   ✅   | Append without checkpoint recovers via tail |
|   4 | `test_checkpoint_commits_then_tail`                |   ✅   | Checkpoint commits then tail                |
|   5 | `test_torn_tail_recovers_to_last_good`             |   ✅   | Torn tail recovers to last good             |
|   6 | `test_ab_superblock_fallback`                      |   ✅   | Ab superblock fallback                      |
|   7 | `test_append_full_fails_closed`                    |   ✅   | Append full fails closed                    |
|   8 | `test_format_and_mount_too_small`                  |   ✅   | Format and mount too small                  |
|   9 | `test_format_write_b_unwired_fails`                |   ✅   | Format write b unwired fails                |
|  10 | `test_format_write_super_a_fails`                  |   ✅   | Format write super a fails                  |
|  11 | `test_null_sync_still_commits`                     |   ✅   | Null sync still commits                     |
|  12 | `test_mount_read_unwired_fails`                    |   ✅   | Mount read unwired fails                    |
|  13 | `test_mount_super_crc_mismatch`                    |   ✅   | Mount super crc mismatch                    |
|  14 | `test_mount_head_past_capacity_rejected`           |   ✅   | Mount head past capacity rejected           |
|  15 | `test_replay_truncated_len_stops`                  |   ✅   | Replay truncated len stops                  |
|  16 | `test_replay_header_read_fails`                    |   ✅   | Replay header read fails                    |
|  17 | `test_replay_payload_read_fails`                   |   ✅   | Replay payload read fails                   |
|  18 | `test_append_header_write_fails`                   |   ✅   | Append header write fails                   |
|  19 | `test_append_payload_write_fails`                  |   ✅   | Append payload write fails                  |
|  20 | `test_checkpoint_super_write_fails`                |   ✅   | Checkpoint super write fails                |
|  21 | `test_checkpoint_second_sync_fails`                |   ✅   | Checkpoint second sync fails                |
|  22 | `test_scan_reads_records`                          |   ✅   | Scan reads records                          |
|  23 | `test_scan_null_callback_counts`                   |   ✅   | Scan null callback counts                   |
|  24 | `test_scan_scratch_too_small`                      |   ✅   | Scan scratch too small                      |
|  25 | `test_scan_header_read_fails`                      |   ✅   | Scan header read fails                      |
|  26 | `test_scan_full_read_fails`                        |   ✅   | Scan full read fails                        |
|  27 | `test_scan_bad_magic_stops`                        |   ✅   | Scan bad magic stops                        |
|  28 | `test_scan_crc_mismatch_stops`                     |   ✅   | Scan crc mismatch stops                     |
|  29 | `test_pread_in_and_out_of_range`                   |   ✅   | Pread in and out of range                   |
|  30 | `test_mount_picks_newer_generation_a`              |   ✅   | Mount picks newer generation a              |
|  31 | `test_replay_tail_seq_not_bumped_when_not_newer`   |   ✅   | Replay tail seq not bumped when not newer   |
|  32 | `test_format_sync_fails`                           |   ✅   | Format sync fails                           |
|  33 | `test_checkpoint_first_sync_fails`                 |   ✅   | Checkpoint first sync fails                 |
|  34 | `test_scan_stops_on_length_overrun`                |   ✅   | Scan stops on length overrun                |
|  35 | `test_scan_stops_when_record_exceeds_scratch`      |   ✅   | Scan stops when record exceeds scratch      |

</details>

---

## test_wal - native_wal - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                  |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------- |
|   1 | `test_crc32_known_vector`                           |   ✅   | Crc32 known vector                           |
|   2 | `test_encode_replay_roundtrip`                      |   ✅   | Encode replay roundtrip                      |
|   3 | `test_replay_recovers_to_last_good_on_corrupt_tail` |   ✅   | Replay recovers to last good on corrupt tail |
|   4 | `test_replay_stops_on_truncated_tail`               |   ✅   | Replay stops on truncated tail               |
|   5 | `test_encode_capacity_and_empty_payload`            |   ✅   | Encode capacity and empty payload            |
|   6 | `test_replay_empty_and_garbage`                     |   ✅   | Replay empty and garbage                     |
|   7 | `test_encode_null_out_fails`                        |   ✅   | Encode null out fails                        |
|   8 | `test_replay_null_callback`                         |   ✅   | Replay null callback                         |

</details>

---

## test_wamp - native_wamp - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the WAMP message codec (services/iot/wamp/wamp.h)._

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_published_subscribe`                 |   ✅   | Published subscribe                 |
|   2 | `test_published_hello`                     |   ✅   | Published hello                     |
|   3 | `test_published_goodbye`                   |   ✅   | Published goodbye                   |
|   4 | `test_published_unsubscribe`               |   ✅   | Published unsubscribe               |
|   5 | `test_published_publish`                   |   ✅   | Published publish                   |
|   6 | `test_published_call`                      |   ✅   | Published call                      |
|   7 | `test_published_register_and_unregister`   |   ✅   | Published register and unregister   |
|   8 | `test_published_yield`                     |   ✅   | Published yield                     |
|   9 | `test_options_dict_is_carried`             |   ✅   | Options dict is carried             |
|  10 | `test_id_range`                            |   ✅   | Id range                            |
|  11 | `test_uri_is_written_as_a_json_string`     |   ✅   | Uri is written as a json string     |
|  12 | `test_build_refuses_a_missing_uri`         |   ✅   | Build refuses a missing uri         |
|  13 | `test_build_refuses_a_short_buffer`        |   ✅   | Build refuses a short buffer        |
|  14 | `test_read_message_type`                   |   ✅   | Read message type                   |
|  15 | `test_read_ids_by_position`                |   ✅   | Read ids by position                |
|  16 | `test_read_across_nested_elements`         |   ✅   | Read across nested elements         |
|  17 | `test_read_uri`                            |   ✅   | Read uri                            |
|  18 | `test_element_slices_the_message`          |   ✅   | Element slices the message          |
|  19 | `test_read_past_the_end`                   |   ✅   | Read past the end                   |
|  20 | `test_reads_refuse_the_wrong_element_kind` |   ✅   | Reads refuse the wrong element kind |
|  21 | `test_get_uri_refuses_a_short_destination` |   ✅   | Get uri refuses a short destination |
|  22 | `test_read_refuses_a_non_list`             |   ✅   | Read refuses a non list             |
|  23 | `test_build_then_read_round_trip`          |   ✅   | Build then read round trip          |

</details>

---

## test_wave - native_wave - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEEE 1609 WAVE codec (services/transportation/wave/wave.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_psid_p_encoding_boundaries`                |   ✅   | Psid p encoding boundaries                |
|   2 | `test_psid_round_trip_over_every_accepted_value` |   ✅   | Psid round trip over every accepted value |
|   3 | `test_psid_decode_refuses_malformed_input`       |   ✅   | Psid decode refuses malformed input       |
|   4 | `test_psid_encode_bounds`                        |   ✅   | Psid encode bounds                        |
|   5 | `test_wsmp_frame_layout`                         |   ✅   | Wsmp frame layout                         |
|   6 | `test_wsmp_round_trip`                           |   ✅   | Wsmp round trip                           |
|   7 | `test_wsmp_parse_checks_the_version`             |   ✅   | Wsmp parse checks the version             |
|   8 | `test_wsmp_parse_refuses_truncation`             |   ✅   | Wsmp parse refuses truncation             |
|   9 | `test_wsmp_payload_length_is_one_octet`          |   ✅   | Wsmp payload length is one octet          |
|  10 | `test_wsmp_build_bounds`                         |   ✅   | Wsmp build bounds                         |
|  11 | `test_1609dot2_envelope`                         |   ✅   | 1609dot2 envelope                         |
|  12 | `test_1609dot2_bounds`                           |   ✅   | 1609dot2 bounds                           |
|  13 | `test_wsmp_carries_a_1609dot2_envelope`          |   ✅   | Wsmp carries a 1609dot2 envelope          |

</details>

---

## test_wave - native_wave_wsmp - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the IEEE 1609 WAVE codec (services/transportation/wave/wave.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_psid_p_encoding_boundaries`                |   ✅   | Psid p encoding boundaries                |
|   2 | `test_psid_round_trip_over_every_accepted_value` |   ✅   | Psid round trip over every accepted value |
|   3 | `test_psid_decode_refuses_malformed_input`       |   ✅   | Psid decode refuses malformed input       |
|   4 | `test_psid_encode_bounds`                        |   ✅   | Psid encode bounds                        |
|   5 | `test_wsmp_frame_layout`                         |   ✅   | Wsmp frame layout                         |
|   6 | `test_wsmp_round_trip`                           |   ✅   | Wsmp round trip                           |
|   7 | `test_wsmp_parse_checks_the_version`             |   ✅   | Wsmp parse checks the version             |
|   8 | `test_wsmp_parse_refuses_truncation`             |   ✅   | Wsmp parse refuses truncation             |
|   9 | `test_wsmp_payload_length_is_one_octet`          |   ✅   | Wsmp payload length is one octet          |
|  10 | `test_wsmp_build_bounds`                         |   ✅   | Wsmp build bounds                         |
|  11 | `test_1609dot2_envelope`                         |   ✅   | 1609dot2 envelope                         |
|  12 | `test_1609dot2_bounds`                           |   ✅   | 1609dot2 bounds                           |
|  13 | `test_wsmp_carries_a_1609dot2_envelope`          |   ✅   | Wsmp carries a 1609dot2 envelope          |

</details>

---

## test_wearlevel - native_wearlevel - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the flash wear-levelling slot selector (server/storage/wearlevel.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_pick_then_mark_levels_the_region_exactly`      |   ✅   | Pick then mark levels the region exactly      |
|   2 | `test_an_uneven_region_converges_to_level`           |   ✅   | An uneven region converges to level           |
|   3 | `test_pick_is_the_least_worn_slot_and_ties_go_low`   |   ✅   | Pick is the least worn slot and ties go low   |
|   4 | `test_pick_does_not_change_the_table`                |   ✅   | Pick does not change the table                |
|   5 | `test_mark_bumps_exactly_one_slot`                   |   ✅   | Mark bumps exactly one slot                   |
|   6 | `test_a_saturated_count_never_wraps`                 |   ✅   | A saturated count never wraps                 |
|   7 | `test_a_mark_past_the_table_bumps_nothing`           |   ✅   | A mark past the table bumps nothing           |
|   8 | `test_imbalance_is_the_high_water_mark_less_the_low` |   ✅   | Imbalance is the high water mark less the low |
|   9 | `test_an_absent_table_is_refused`                    |   ✅   | An absent table is refused                    |

</details>

---

## test_webdav - native_webdav - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the WebDAV wire core (network_drivers/application/webdav/webdav.h)._

|   # | Test                                                         | Status | Description                                           |
| --: | :----------------------------------------------------------- | :----: | :---------------------------------------------------- |
|   1 | `test_rfc4918_lock_compatibility_table`                      |   ✅   | Rfc4918 lock compatibility table                      |
|   2 | `test_lock_scope_follows_depth_and_segment_boundaries`       |   ✅   | Lock scope follows depth and segment boundaries       |
|   3 | `test_lock_paths_normalize_the_trailing_slash`               |   ✅   | Lock paths normalize the trailing slash               |
|   4 | `test_write_needs_the_covering_lock_token`                   |   ✅   | Write needs the covering lock token                   |
|   5 | `test_lock_timeout_and_refresh`                              |   ✅   | Lock timeout and refresh                              |
|   6 | `test_lock_table_is_bounded`                                 |   ✅   | Lock table is bounded                                 |
|   7 | `test_lock_oversized_path_and_token_are_refused`             |   ✅   | Lock oversized path and token are refused             |
|   8 | `test_if_header_state_token`                                 |   ✅   | If header state token                                 |
|   9 | `test_depth_header`                                          |   ✅   | Depth header                                          |
|  10 | `test_method_classification`                                 |   ✅   | Method classification                                 |
|  11 | `test_xml_escape`                                            |   ✅   | Xml escape                                            |
|  12 | `test_destination_header_path`                               |   ✅   | Destination header path                               |
|  13 | `test_multistatus_document_shape`                            |   ✅   | Multistatus document shape                            |
|  14 | `test_multistatus_entry_is_atomic`                           |   ✅   | Multistatus entry is atomic                           |
|  15 | `test_proppatch_multistatus_echoes_the_requested_properties` |   ✅   | Proppatch multistatus echoes the requested properties |
|  16 | `test_proppatch_does_not_echo_injected_markup`               |   ✅   | Proppatch does not echo injected markup               |

</details>

---

## test_webdav - native_webdav_wire - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the WebDAV wire core (network_drivers/application/webdav/webdav.h)._

|   # | Test                                                         | Status | Description                                           |
| --: | :----------------------------------------------------------- | :----: | :---------------------------------------------------- |
|   1 | `test_rfc4918_lock_compatibility_table`                      |   ✅   | Rfc4918 lock compatibility table                      |
|   2 | `test_lock_scope_follows_depth_and_segment_boundaries`       |   ✅   | Lock scope follows depth and segment boundaries       |
|   3 | `test_lock_paths_normalize_the_trailing_slash`               |   ✅   | Lock paths normalize the trailing slash               |
|   4 | `test_write_needs_the_covering_lock_token`                   |   ✅   | Write needs the covering lock token                   |
|   5 | `test_lock_timeout_and_refresh`                              |   ✅   | Lock timeout and refresh                              |
|   6 | `test_lock_table_is_bounded`                                 |   ✅   | Lock table is bounded                                 |
|   7 | `test_lock_oversized_path_and_token_are_refused`             |   ✅   | Lock oversized path and token are refused             |
|   8 | `test_if_header_state_token`                                 |   ✅   | If header state token                                 |
|   9 | `test_depth_header`                                          |   ✅   | Depth header                                          |
|  10 | `test_method_classification`                                 |   ✅   | Method classification                                 |
|  11 | `test_xml_escape`                                            |   ✅   | Xml escape                                            |
|  12 | `test_destination_header_path`                               |   ✅   | Destination header path                               |
|  13 | `test_multistatus_document_shape`                            |   ✅   | Multistatus document shape                            |
|  14 | `test_multistatus_entry_is_atomic`                           |   ✅   | Multistatus entry is atomic                           |
|  15 | `test_proppatch_multistatus_echoes_the_requested_properties` |   ✅   | Proppatch multistatus echoes the requested properties |
|  16 | `test_proppatch_does_not_echo_injected_markup`               |   ✅   | Proppatch does not echo injected markup               |

</details>

---

## test_webhook - native_webhook - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the outbound webhook builders (services/net/webhook/webhook.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_target_uri_carries_event_then_key_as_segments` |   ✅   | Target uri carries event then key as segments |
|   2 | `test_rfc8259_object_grammar`                        |   ✅   | Rfc8259 object grammar                        |
|   3 | `test_absent_values_omit_their_member`               |   ✅   | Absent values omit their member               |
|   4 | `test_rfc8259_escapes_quote_and_reverse_solidus`     |   ✅   | Rfc8259 escapes quote and reverse solidus     |
|   5 | `test_overflow_writes_nothing`                       |   ✅   | Overflow writes nothing                       |
|   6 | `test_exact_capacity_boundary`                       |   ✅   | Exact capacity boundary                       |
|   7 | `test_every_field_fails_closed`                      |   ✅   | Every field fails closed                      |
|   8 | `test_builder_argument_guards`                       |   ✅   | Builder argument guards                       |
|   9 | `test_post_reports_no_transport`                     |   ✅   | Post reports no transport                     |
|  10 | `test_trigger_builds_then_posts`                     |   ✅   | Trigger builds then posts                     |
|  11 | `test_post_argument_guards`                          |   ✅   | Post argument guards                          |

</details>

---

## test_webhook - native_webhook_json - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the outbound webhook builders (services/net/webhook/webhook.h)._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_target_uri_carries_event_then_key_as_segments` |   ✅   | Target uri carries event then key as segments |
|   2 | `test_rfc8259_object_grammar`                        |   ✅   | Rfc8259 object grammar                        |
|   3 | `test_absent_values_omit_their_member`               |   ✅   | Absent values omit their member               |
|   4 | `test_rfc8259_escapes_quote_and_reverse_solidus`     |   ✅   | Rfc8259 escapes quote and reverse solidus     |
|   5 | `test_overflow_writes_nothing`                       |   ✅   | Overflow writes nothing                       |
|   6 | `test_exact_capacity_boundary`                       |   ✅   | Exact capacity boundary                       |
|   7 | `test_every_field_fails_closed`                      |   ✅   | Every field fails closed                      |
|   8 | `test_builder_argument_guards`                       |   ✅   | Builder argument guards                       |
|   9 | `test_post_reports_no_transport`                     |   ✅   | Post reports no transport                     |
|  10 | `test_trigger_builds_then_posts`                     |   ✅   | Trigger builds then posts                     |
|  11 | `test_post_argument_guards`                          |   ✅   | Post argument guards                          |

</details>

---

## test_wifi_sniffer - native_wifi_sniffer - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the 802.11 sniffer core (services/radio/wifi_sniffer/wifi_sniffer.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_beacon_mac_header`                                    |   ✅   | Beacon mac header                                    |
|   2 | `test_frame_control_type_field`                             |   ✅   | Frame control type field                             |
|   3 | `test_frame_control_version_and_subtype`                    |   ✅   | Frame control version and subtype                    |
|   4 | `test_frame_control_flag_bit_positions`                     |   ✅   | Frame control flag bit positions                     |
|   5 | `test_truncated_capture_reports_how_many_addresses_it_held` |   ✅   | Truncated capture reports how many addresses it held |
|   6 | `test_parse_null_arguments`                                 |   ✅   | Parse null arguments                                 |
|   7 | `test_stats_tally`                                          |   ✅   | Stats tally                                          |
|   8 | `test_roam_needs_to_clear_the_hysteresis`                   |   ✅   | Roam needs to clear the hysteresis                   |
|   9 | `test_scan_walks_the_range_and_wraps`                       |   ✅   | Scan walks the range and wraps                       |
|  10 | `test_scan_init_clamps_the_range`                           |   ✅   | Scan init clamps the range                           |
|  11 | `test_scan_due_is_rollover_safe`                            |   ✅   | Scan due is rollover safe                            |
|  12 | `test_survey_keeps_the_strongest_per_channel`               |   ✅   | Survey keeps the strongest per channel               |
|  13 | `test_survey_best_excludes_the_current_channel`             |   ✅   | Survey best excludes the current channel             |
|  14 | `test_null_state_is_refused`                                |   ✅   | Null state is refused                                |

</details>

---

## test_wifi_sniffer - native_radio_wifi_sniffer - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the 802.11 sniffer core (services/radio/wifi_sniffer/wifi_sniffer.h)._

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_beacon_mac_header`                                    |   ✅   | Beacon mac header                                    |
|   2 | `test_frame_control_type_field`                             |   ✅   | Frame control type field                             |
|   3 | `test_frame_control_version_and_subtype`                    |   ✅   | Frame control version and subtype                    |
|   4 | `test_frame_control_flag_bit_positions`                     |   ✅   | Frame control flag bit positions                     |
|   5 | `test_truncated_capture_reports_how_many_addresses_it_held` |   ✅   | Truncated capture reports how many addresses it held |
|   6 | `test_parse_null_arguments`                                 |   ✅   | Parse null arguments                                 |
|   7 | `test_stats_tally`                                          |   ✅   | Stats tally                                          |
|   8 | `test_roam_needs_to_clear_the_hysteresis`                   |   ✅   | Roam needs to clear the hysteresis                   |
|   9 | `test_scan_walks_the_range_and_wraps`                       |   ✅   | Scan walks the range and wraps                       |
|  10 | `test_scan_init_clamps_the_range`                           |   ✅   | Scan init clamps the range                           |
|  11 | `test_scan_due_is_rollover_safe`                            |   ✅   | Scan due is rollover safe                            |
|  12 | `test_survey_keeps_the_strongest_per_channel`               |   ✅   | Survey keeps the strongest per channel               |
|  13 | `test_survey_best_excludes_the_current_channel`             |   ✅   | Survey best excludes the current channel             |
|  14 | `test_null_state_is_refused`                                |   ✅   | Null state is refused                                |

</details>

---

## test_wisun - native_wisun - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Wi-SUN FAN border-router connector (services/radio/wisun/wisun.h)._

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_rfc7252_figure_16_request`             |   ✅   | Rfc7252 figure 16 request             |
|   2 | `test_leading_slash_is_not_a_segment`        |   ✅   | Leading slash is not a segment        |
|   3 | `test_type_field_selects_confirmable_or_not` |   ✅   | Type field selects confirmable or not |
|   4 | `test_token_length_and_placement`            |   ✅   | Token length and placement            |
|   5 | `test_each_path_segment_is_its_own_option`   |   ✅   | Each path segment is its own option   |
|   6 | `test_option_length_extension`               |   ✅   | Option length extension               |
|   7 | `test_payload_marker`                        |   ✅   | Payload marker                        |
|   8 | `test_build_refuses_a_short_buffer`          |   ✅   | Build refuses a short buffer          |
|   9 | `test_node_registry`                         |   ✅   | Node registry                         |
|  10 | `test_registry_without_storage`              |   ✅   | Registry without storage              |
|  11 | `test_nodes_json`                            |   ✅   | Nodes json                            |

</details>

---

## test_wisun - native_radio_wisun - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Wi-SUN FAN border-router connector (services/radio/wisun/wisun.h)._

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_rfc7252_figure_16_request`             |   ✅   | Rfc7252 figure 16 request             |
|   2 | `test_leading_slash_is_not_a_segment`        |   ✅   | Leading slash is not a segment        |
|   3 | `test_type_field_selects_confirmable_or_not` |   ✅   | Type field selects confirmable or not |
|   4 | `test_token_length_and_placement`            |   ✅   | Token length and placement            |
|   5 | `test_each_path_segment_is_its_own_option`   |   ✅   | Each path segment is its own option   |
|   6 | `test_option_length_extension`               |   ✅   | Option length extension               |
|   7 | `test_payload_marker`                        |   ✅   | Payload marker                        |
|   8 | `test_build_refuses_a_short_buffer`          |   ✅   | Build refuses a short buffer          |
|   9 | `test_node_registry`                         |   ✅   | Node registry                         |
|  10 | `test_registry_without_storage`              |   ✅   | Registry without storage              |
|  11 | `test_nodes_json`                            |   ✅   | Nodes json                            |

</details>

---

## test_ws_client - native_ws_client - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the WebSocket client codec (services/net/ws_client/ws_client.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc6455_accept_for_the_published_key`      |   ✅   | Rfc6455 accept for the published key      |
|   2 | `test_rfc6455_opening_handshake_fields`          |   ✅   | Rfc6455 opening handshake fields          |
|   3 | `test_subprotocol_is_offered_only_when_named`    |   ✅   | Subprotocol is offered only when named    |
|   4 | `test_rfc6455_server_handshake_is_verified`      |   ✅   | Rfc6455 server handshake is verified      |
|   5 | `test_rfc6455_masked_text_frame`                 |   ✅   | Rfc6455 masked text frame                 |
|   6 | `test_rfc6455_parse_unmasked_text_frame`         |   ✅   | Rfc6455 parse unmasked text frame         |
|   7 | `test_rfc6455_fragmented_message`                |   ✅   | Rfc6455 fragmented message                |
|   8 | `test_rfc6455_control_frames`                    |   ✅   | Rfc6455 control frames                    |
|   9 | `test_rfc6455_payload_length_forms`              |   ✅   | Rfc6455 payload length forms              |
|  10 | `test_rfc6455_256_octet_frame`                   |   ✅   | Rfc6455 256 octet frame                   |
|  11 | `test_rfc6455_64kib_frame`                       |   ✅   | Rfc6455 64kib frame                       |
|  12 | `test_parse_refuses_an_incomplete_frame`         |   ✅   | Parse refuses an incomplete frame         |
|  13 | `test_parse_refuses_an_oversized_payload_length` |   ✅   | Parse refuses an oversized payload length |
|  14 | `test_parse_stays_aligned_past_a_masking_key`    |   ✅   | Parse stays aligned past a masking key    |
|  15 | `test_build_frame_fails_closed`                  |   ✅   | Build frame fails closed                  |
|  16 | `test_accept_for_key_fails_closed`               |   ✅   | Accept for key fails closed               |
|  17 | `test_build_handshake_fails_closed`              |   ✅   | Build handshake fails closed              |
|  18 | `test_check_server_handshake_fails_closed`       |   ✅   | Check server handshake fails closed       |
|  19 | `test_transport_reports_no_connection`           |   ✅   | Transport reports no connection           |

</details>

---

## test_ws_client - native_ws_client_rfc6455 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the WebSocket client codec (services/net/ws_client/ws_client.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_rfc6455_accept_for_the_published_key`      |   ✅   | Rfc6455 accept for the published key      |
|   2 | `test_rfc6455_opening_handshake_fields`          |   ✅   | Rfc6455 opening handshake fields          |
|   3 | `test_subprotocol_is_offered_only_when_named`    |   ✅   | Subprotocol is offered only when named    |
|   4 | `test_rfc6455_server_handshake_is_verified`      |   ✅   | Rfc6455 server handshake is verified      |
|   5 | `test_rfc6455_masked_text_frame`                 |   ✅   | Rfc6455 masked text frame                 |
|   6 | `test_rfc6455_parse_unmasked_text_frame`         |   ✅   | Rfc6455 parse unmasked text frame         |
|   7 | `test_rfc6455_fragmented_message`                |   ✅   | Rfc6455 fragmented message                |
|   8 | `test_rfc6455_control_frames`                    |   ✅   | Rfc6455 control frames                    |
|   9 | `test_rfc6455_payload_length_forms`              |   ✅   | Rfc6455 payload length forms              |
|  10 | `test_rfc6455_256_octet_frame`                   |   ✅   | Rfc6455 256 octet frame                   |
|  11 | `test_rfc6455_64kib_frame`                       |   ✅   | Rfc6455 64kib frame                       |
|  12 | `test_parse_refuses_an_incomplete_frame`         |   ✅   | Parse refuses an incomplete frame         |
|  13 | `test_parse_refuses_an_oversized_payload_length` |   ✅   | Parse refuses an oversized payload length |
|  14 | `test_parse_stays_aligned_past_a_masking_key`    |   ✅   | Parse stays aligned past a masking key    |
|  15 | `test_build_frame_fails_closed`                  |   ✅   | Build frame fails closed                  |
|  16 | `test_accept_for_key_fails_closed`               |   ✅   | Accept for key fails closed               |
|  17 | `test_build_handshake_fails_closed`              |   ✅   | Build handshake fails closed              |
|  18 | `test_check_server_handshake_fails_closed`       |   ✅   | Check server handshake fails closed       |
|  19 | `test_transport_reports_no_connection`           |   ✅   | Transport reports no connection           |

</details>

---

## test_xmpp - native_xmpp - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the XMPP stanza codec (services/iot/xmpp/xmpp.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_predefined_entities`                    |   ✅   | Predefined entities                    |
|   2 | `test_escape_leaves_ordinary_text_alone`      |   ✅   | Escape leaves ordinary text alone      |
|   3 | `test_stream_header`                          |   ✅   | Stream header                          |
|   4 | `test_message_stanza`                         |   ✅   | Message stanza                         |
|   5 | `test_message_body_is_escaped`                |   ✅   | Message body is escaped                |
|   6 | `test_attribute_values_are_escaped`           |   ✅   | Attribute values are escaped           |
|   7 | `test_presence_stanza`                        |   ✅   | Presence stanza                        |
|   8 | `test_iq_stanza`                              |   ✅   | Iq stanza                              |
|   9 | `test_stanza_name`                            |   ✅   | Stanza name                            |
|  10 | `test_stanza_name_skips_non_start_tags`       |   ✅   | Stanza name skips non start tags       |
|  11 | `test_attribute_read`                         |   ✅   | Attribute read                         |
|  12 | `test_attribute_name_must_start_an_attribute` |   ✅   | Attribute name must start an attribute |
|  13 | `test_attribute_value_is_raw`                 |   ✅   | Attribute value is raw                 |
|  14 | `test_attribute_read_stops_at_the_start_tag`  |   ✅   | Attribute read stops at the start tag  |
|  15 | `test_attribute_read_refusals`                |   ✅   | Attribute read refusals                |
|  16 | `test_build_refuses_a_short_buffer`           |   ✅   | Build refuses a short buffer           |
|  17 | `test_escape_refuses_a_null_source`           |   ✅   | Escape refuses a null source           |
|  18 | `test_build_then_read_round_trip`             |   ✅   | Build then read round trip             |

</details>

---

## test_zigbee - native_zigbee - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Zigbee EZSP / ASH framing codec (services/radio/zigbee/zigbee.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_crc16_catalog_check_value`                 |   ✅   | Crc16 catalog check value                 |
|   2 | `test_ug101_rst_frame`                           |   ✅   | Ug101 rst frame                           |
|   3 | `test_crc_covers_control_then_payload_msb_first` |   ✅   | Crc covers control then payload msb first |
|   4 | `test_reserved_octets_are_escaped`               |   ✅   | Reserved octets are escaped               |
|   5 | `test_frame_round_trip`                          |   ✅   | Frame round trip                          |
|   6 | `test_empty_payload_round_trip`                  |   ✅   | Empty payload round trip                  |
|   7 | `test_decode_rejects_a_corrupted_frame`          |   ✅   | Decode rejects a corrupted frame          |
|   8 | `test_decode_framing_faults`                     |   ✅   | Decode framing faults                     |
|   9 | `test_decode_refuses_a_short_payload_buffer`     |   ✅   | Decode refuses a short payload buffer     |
|  10 | `test_decode_consumes_one_frame_from_a_stream`   |   ✅   | Decode consumes one frame from a stream   |
|  11 | `test_encode_bounds`                             |   ✅   | Encode bounds                             |
|  12 | `test_decode_null_input`                         |   ✅   | Decode null input                         |

</details>

---

## test_zigbee - native_radio_zigbee - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Zigbee EZSP / ASH framing codec (services/radio/zigbee/zigbee.h)._

|   # | Test                                             | Status | Description                               |
| --: | :----------------------------------------------- | :----: | :---------------------------------------- |
|   1 | `test_crc16_catalog_check_value`                 |   ✅   | Crc16 catalog check value                 |
|   2 | `test_ug101_rst_frame`                           |   ✅   | Ug101 rst frame                           |
|   3 | `test_crc_covers_control_then_payload_msb_first` |   ✅   | Crc covers control then payload msb first |
|   4 | `test_reserved_octets_are_escaped`               |   ✅   | Reserved octets are escaped               |
|   5 | `test_frame_round_trip`                          |   ✅   | Frame round trip                          |
|   6 | `test_empty_payload_round_trip`                  |   ✅   | Empty payload round trip                  |
|   7 | `test_decode_rejects_a_corrupted_frame`          |   ✅   | Decode rejects a corrupted frame          |
|   8 | `test_decode_framing_faults`                     |   ✅   | Decode framing faults                     |
|   9 | `test_decode_refuses_a_short_payload_buffer`     |   ✅   | Decode refuses a short payload buffer     |
|  10 | `test_decode_consumes_one_frame_from_a_stream`   |   ✅   | Decode consumes one frame from a stream   |
|  11 | `test_encode_bounds`                             |   ✅   | Encode bounds                             |
|  12 | `test_decode_null_input`                         |   ✅   | Decode null input                         |

</details>

---

## test_zwave - native_zwave - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Z-Wave Serial API frame codec (services/radio/zwave/zwave.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_ins12350_getversion_frame`              |   ✅   | Ins12350 getversion frame              |
|   2 | `test_len_field_counts_type_through_checksum` |   ✅   | Len field counts type through checksum |
|   3 | `test_checksum_span_and_position`             |   ✅   | Checksum span and position             |
|   4 | `test_build_then_parse_round_trip`            |   ✅   | Build then parse round trip            |
|   5 | `test_parse_rejects_a_corrupted_frame`        |   ✅   | Parse rejects a corrupted frame        |
|   6 | `test_parse_rejects_a_non_sof_start`          |   ✅   | Parse rejects a non sof start          |
|   7 | `test_parse_waits_for_the_rest`               |   ✅   | Parse waits for the rest               |
|   8 | `test_parse_rejects_an_out_of_range_len`      |   ✅   | Parse rejects an out of range len      |
|   9 | `test_control_octets`                         |   ✅   | Control octets                         |
|  10 | `test_build_bounds`                           |   ✅   | Build bounds                           |
|  11 | `test_parse_accepts_null_out_parameters`      |   ✅   | Parse accepts null out parameters      |

</details>

---

## test_zwave - native_radio_zwave - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Z-Wave Serial API frame codec (services/radio/zwave/zwave.h)._

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_ins12350_getversion_frame`              |   ✅   | Ins12350 getversion frame              |
|   2 | `test_len_field_counts_type_through_checksum` |   ✅   | Len field counts type through checksum |
|   3 | `test_checksum_span_and_position`             |   ✅   | Checksum span and position             |
|   4 | `test_build_then_parse_round_trip`            |   ✅   | Build then parse round trip            |
|   5 | `test_parse_rejects_a_corrupted_frame`        |   ✅   | Parse rejects a corrupted frame        |
|   6 | `test_parse_rejects_a_non_sof_start`          |   ✅   | Parse rejects a non sof start          |
|   7 | `test_parse_waits_for_the_rest`               |   ✅   | Parse waits for the rest               |
|   8 | `test_parse_rejects_an_out_of_range_len`      |   ✅   | Parse rejects an out of range len      |
|   9 | `test_control_octets`                         |   ✅   | Control octets                         |
|  10 | `test_build_bounds`                           |   ✅   | Build bounds                           |
|  11 | `test_parse_accepts_null_out_parameters`      |   ✅   | Parse accepts null out parameters      |

</details>

---
