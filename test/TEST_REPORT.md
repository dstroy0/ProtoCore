# Test Report

**Generated:** 2026-09-30 21:18:53
**Command:** `harness.py run` over 411 native envs
**Result:** ✅ 500 passed, 0 failed - 229s

---

## Summary

| Suite             | Environment          | Tests | Status | Duration |
| :---------------- | :------------------- | ----: | :----: | -------: |
| test_ad9238       | native_ad9238        |    21 |   ✅   |        - |
| test_cc1101       | native_cc1101        |    18 |   ✅   |        - |
| test_clock        | native_clock         |     7 |   ✅   |        - |
| test_ct_eq        | native_ct_eq         |     8 |   ✅   |        - |
| test_ct_eq        | native_ct_eq_unit    |     8 |   ✅   |        - |
| test_df1          | native_df1           |    12 |   ✅   |        - |
| test_dnc          | native_dnc           |    23 |   ✅   |        - |
| test_dshot        | native_dshot         |    11 |   ✅   |        - |
| test_boot         | native_boot          |    10 |   ✅   |        - |
| test_hex          | native_hex           |     9 |   ✅   |        - |
| test_interbus     | native_interbus      |     9 |   ✅   |        - |
| test_iolink       | native_iolink        |     9 |   ✅   |        - |
| test_j2735        | native_j2735_uper    |    18 |   ✅   |        - |
| test_lfs_mock     | native_lfs_mock      |    15 |   ✅   |        - |
| test_link_manager | native_link_manager  |     8 |   ✅   |        - |
| test_lora         | native_lora          |    19 |   ✅   |        - |
| test_netadapt     | native_netadapt      |     9 |   ✅   |        - |
| test_nrf24        | native_nrf24         |    17 |   ✅   |        - |
| test_ntcip        | native_ntcip_oid     |     8 |   ✅   |        - |
| test_ota_rollback | native_ota_rollback  |    10 |   ✅   |        - |
| test_packml       | native_packml        |    17 |   ✅   |        - |
| test_pn532        | native_pn532         |    11 |   ✅   |        - |
| test_pqc_sha3     | native_pqc           |    10 |   ✅   |        - |
| test_pqc_sha3     | native_sha3_kat      |    10 |   ✅   |        - |
| test_crc          | native_primitives    |    12 |   ✅   |        - |
| test_crc          | native_crc           |    12 |   ✅   |        - |
| test_quic_varint  | native_quic_varint   |     8 |   ✅   |        - |
| test_safety_scl   | native_safety_scl    |    14 |   ✅   |        - |
| test_sigfox       | native_sigfox_at     |     6 |   ✅   |        - |
| test_sleep_sched  | native_sleep_sched   |    11 |   ✅   |        - |
| test_snmp_ber     | native_snmp_ber_x690 |    19 |   ✅   |        - |
| test_sockpool     | native_sockpool      |    12 |   ✅   |        - |
| test_telemetry    | native_telemetry     |    20 |   ✅   |        - |
| test_thread       | native_radio_thread  |    19 |   ✅   |        - |
| test_time_compat  | native_time_compat   |     7 |   ✅   |        - |
| test_time_source  | native_time_fallback |     8 |   ✅   |        - |
| test_tls_policy   | native_tls_policy    |    14 |   ✅   |        - |
| test_utf8         | native_utf8          |     9 |   ✅   |        - |
| test_wearlevel    | native_wearlevel     |     9 |   ✅   |        - |
| test_zigbee       | native_radio_zigbee  |    12 |   ✅   |        - |
| test_zwave        | native_radio_zwave   |    11 |   ✅   |        - |

---

## test_ad9238 - native_ad9238 - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_an877_instruction_phase_bit_field`               |   ✅   | An877 instruction phase bit field               |
|   2 | `test_an877_word_length_is_w1w0_plus_one`              |   ✅   | An877 word length is w1w0 plus one              |
|   3 | `test_an877_address_field_is_thirteen_bits`            |   ✅   | An877 address field is thirteen bits            |
|   4 | `test_an877_read_write_bit_is_the_only_difference`     |   ✅   | An877 read write bit is the only difference     |
|   5 | `test_an877_transfer_register_write`                   |   ✅   | An877 transfer register write                   |
|   6 | `test_an877_configuration_chip_id_and_grade_addresses` |   ✅   | An877 configuration chip id and grade addresses |
|   7 | `test_an877_chip_id_read_transaction`                  |   ✅   | An877 chip id read transaction                  |
|   8 | `test_an877_output_mode_write_transaction`             |   ✅   | An877 output mode write transaction             |
|   9 | `test_an877_output_data_format_codes`                  |   ✅   | An877 output data format codes                  |
|  10 | `test_an877_output_test_mode_codes`                    |   ✅   | An877 output test mode codes                    |
|  11 | `test_byte_counts_outside_one_to_four_are_refused`     |   ✅   | Byte counts outside one to four are refused     |
|  12 | `test_null_and_undersized_buffers_fail_closed`         |   ✅   | Null and undersized buffers fail closed         |
|  13 | `test_named_register_addresses_are_distinct`           |   ✅   | Named register addresses are distinct           |
|  14 | `test_an877_modes_register_is_0x008`                   |   ✅   | An877 modes register is 0x008                   |
|  15 | `test_an877_output_test_modes_register_is_0x00d`       |   ✅   | An877 output test modes register is 0x00d       |
|  16 | `test_an877_analog_input_register_is_0x00f`            |   ✅   | An877 analog input register is 0x00f            |
|  17 | `test_an877_offset_adjust_register_is_0x010`           |   ✅   | An877 offset adjust register is 0x010           |
|  18 | `test_an877_output_mode_register_is_0x014`             |   ✅   | An877 output mode register is 0x014             |
|  19 | `test_an877_output_delay_adjust_register_is_0x017`     |   ✅   | An877 output delay adjust register is 0x017     |
|  20 | `test_an877_reference_adjust_register_is_0x018`        |   ✅   | An877 reference adjust register is 0x018        |
|  21 | `test_an877_device_index_register_is_0x004_or_0x005`   |   ✅   | An877 device index register is 0x004 or 0x005   |

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

## test_clock - native_clock - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_ct_eq - native_ct_eq - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_identical_buffers_are_equal`                      |   ✅   | Identical buffers are equal                      |
|   2 | `test_a_difference_at_every_position_is_caught`         |   ✅   | A difference at every position is caught         |
|   3 | `test_difference_in_the_last_byte`                      |   ✅   | Difference in the last byte                      |
|   4 | `test_embedded_zero_bytes_do_not_terminate_the_compare` |   ✅   | Embedded zero bytes do not terminate the compare |
|   5 | `test_zero_length_is_equal`                             |   ✅   | Zero length is equal                             |
|   6 | `test_same_pointer_is_equal`                            |   ✅   | Same pointer is equal                            |
|   7 | `test_null_operands_are_refused`                        |   ✅   | Null operands are refused                        |
|   8 | `test_inline_and_namespace_agree`                       |   ✅   | Inline and namespace agree                       |

</details>

---

## test_ct_eq - native_ct_eq_unit - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                      |
| --: | :------------------------------------------------------ | :----: | :----------------------------------------------- |
|   1 | `test_identical_buffers_are_equal`                      |   ✅   | Identical buffers are equal                      |
|   2 | `test_a_difference_at_every_position_is_caught`         |   ✅   | A difference at every position is caught         |
|   3 | `test_difference_in_the_last_byte`                      |   ✅   | Difference in the last byte                      |
|   4 | `test_embedded_zero_bytes_do_not_terminate_the_compare` |   ✅   | Embedded zero bytes do not terminate the compare |
|   5 | `test_zero_length_is_equal`                             |   ✅   | Zero length is equal                             |
|   6 | `test_same_pointer_is_equal`                            |   ✅   | Same pointer is equal                            |
|   7 | `test_null_operands_are_refused`                        |   ✅   | Null operands are refused                        |
|   8 | `test_inline_and_namespace_agree`                       |   ✅   | Inline and namespace agree                       |

</details>

---

## test_df1 - native_df1 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                         | Status | Description                                                                                   |
| --: | :------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_control_characters`                    |   ✅   | Control characters                                                                            |
|   2 | `test_crc_matches_the_published_check_value` |   ✅   | The initial value is zero and there is no final XOR, so an empty message CRCs to zero.        |
|   3 | `test_bcc_and_data_sum_to_zero`              |   ✅   | A sum of zero yields a check of zero, not 0x100.                                              |
|   4 | `test_bcc_frame_layout`                      |   ✅   | Bcc frame layout                                                                              |
|   5 | `test_crc_frame_covers_the_data_and_the_etx` |   ✅   | The CRC of the data alone is a different value, so a codec that omitted the ETX would differ. |
|   6 | `test_dle_bytes_are_doubled_on_the_wire`     |   ✅   | sum = 0x10 + 0x41 + 0x10 = 0x61, so BCC = 0x100 - 0x61 = 0x9F                                 |
|   7 | `test_round_trip_over_every_octet_value`     |   ✅   | Round trip over every octet value                                                             |
|   8 | `test_empty_message`                         |   ✅   | Empty message                                                                                 |
|   9 | `test_a_corrupted_frame_fails_its_check`     |   ✅   | The same for the CRC form.                                                                    |
|  10 | `test_framing_refusals`                      |   ✅   | A DLE followed by something that is neither DLE nor ETX is an unexpected control symbol.      |
|  11 | `test_capacity_refusals`                     |   ✅   | A doubled DLE takes one octet of the destination, not two.                                    |
|  12 | `test_out_len_is_optional`                   |   ✅   | Out len is optional                                                                           |

</details>

---

## test_dnc - native_dnc - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                           |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_eia_codes_carry_odd_parity`                |   ✅   | Eia codes carry odd parity                                                            |
|   2 | `test_eia_letters_and_digits_are_derived`        |   ✅   | Eia letters and digits are derived                                                    |
|   3 | `test_eia_translation_is_a_bijection`            |   ✅   | Eia translation is a bijection                                                        |
|   4 | `test_eia_refuses_characters_it_has_no_code_for` |   ✅   | Eia refuses characters it has no code for                                             |
|   5 | `test_eia_special_codes`                         |   ✅   | Eia special codes                                                                     |
|   6 | `test_iso_parity_is_even`                        |   ✅   | Bit 7 of the input is ignored, so adding parity twice is the same as adding it once.  |
|   7 | `test_xon_xoff_flow_state`                       |   ✅   | Repeats are idempotent: two XOFFs still take one XON.                                 |
|   8 | `test_iso_block_framing`                         |   ✅   | Iso block framing                                                                     |
|   9 | `test_iso_block_carries_parity`                  |   ✅   | CR is 0x0D, three bits, so its parity bit is set; LF is 0x0A, two bits, so it is not. |
|  10 | `test_eia_block_framing`                         |   ✅   | Eia block framing                                                                     |
|  11 | `test_eia_block_fails_closed`                    |   ✅   | Eia block fails closed                                                                |
|  12 | `test_block_refuses_a_short_buffer`              |   ✅   | Block refuses a short buffer                                                          |
|  13 | `test_program_marker`                            |   ✅   | Program marker                                                                        |
|  14 | `test_leader_runout`                             |   ✅   | Leader runout                                                                         |
|  15 | `test_decoder_reports_markers_and_lines`         |   ✅   | Decoder reports markers and lines                                                     |
|  16 | `test_decoder_skips_runout`                      |   ✅   | Decoder skips runout                                                                  |
|  17 | `test_decoder_ignores_a_blank_block`             |   ✅   | Decoder ignores a blank block                                                         |
|  18 | `test_decoder_drops_an_overlong_block`           |   ✅   | Decoder drops an overlong block                                                       |
|  19 | `test_decoder_accepts_a_full_length_block`       |   ✅   | Decoder accepts a full length block                                                   |
|  20 | `test_decoder_strips_iso_parity`                 |   ✅   | Decoder strips iso parity                                                             |
|  21 | `test_program_round_trip`                        |   ✅   | Program round trip                                                                    |
|  22 | `test_marker_discards_a_partial_block`           |   ✅   | Marker discards a partial block                                                       |
|  23 | `test_eia_decoder_does_not_filter_flow_bytes`    |   ✅   | Eia decoder does not filter flow bytes                                                |

</details>

---

## test_dshot - native_dshot - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                | Status | Description                                                                                |
| --: | :------------------------------------------------------------------ | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_published_crc_over_worked_frames`                             |   ✅   | value 0, tlm 0: v12 = 0x000, nibbles 0^0^0 = 0 -> frame 0x0000                             |
|   2 | `test_bidirectional_inverts_only_the_checksum`                      |   ✅   | value 1000: normal crc A -> inverted 5, so 0x7D0A becomes 0x7D05                           |
|   3 | `test_encode_decode_round_trip_over_the_whole_value_domain`         |   ✅   | Encode decode round trip over the whole value domain                                       |
|   4 | `test_every_single_bit_error_is_rejected`                           |   ✅   | Every single bit error is rejected                                                         |
|   5 | `test_the_two_crc_conventions_do_not_accept_each_other`             |   ✅   | The two crc conventions do not accept each other                                           |
|   6 | `test_values_wider_than_eleven_bits_are_masked`                     |   ✅   | Each pair is captured a frame at a time: both encodes report through the one namespace, so |
|   7 | `test_published_command_and_throttle_domains`                       |   ✅   | The encoded frame is captured into a local before the decode: both calls report through    |
|   8 | `test_bit_timing_is_three_quarters_and_three_eighths_of_the_period` |   ✅   | T1H is twice T0H to within the integer truncation of a single nanosecond                   |
|   9 | `test_unknown_bit_rates_return_zero`                                |   ✅   | Unknown bit rates return zero                                                              |
|  10 | `test_analog_pulse_width_endpoints_and_midpoint`                    |   ✅   | above the domain the throttle clamps rather than running past the maximum pulse            |
|  11 | `test_analog_pulse_width_is_monotone_and_bounded`                   |   ✅   | Analog pulse width is monotone and bounded                                                 |

</details>

---

## test_boot - native_boot - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_data_is_copied_word_for_word`             |   ✅   | Data is copied word for word             |
|   2 | `test_bss_is_zeroed_and_nothing_else`           |   ✅   | Bss is zeroed and nothing else           |
|   3 | `test_a_reset_does_both`                        |   ✅   | A reset does both                        |
|   4 | `test_empty_regions_write_nothing`              |   ✅   | Empty regions write nothing              |
|   5 | `test_null_regions_are_skipped`                 |   ✅   | Null regions are skipped                 |
|   6 | `test_paint_covers_the_region_below_the_frame`  |   ✅   | Paint covers the region below the frame  |
|   7 | `test_unused_counts_the_paint_from_the_low_end` |   ✅   | Unused counts the paint from the low end |
|   8 | `test_an_unpainted_stack_reports_nothing_free`  |   ✅   | An unpainted stack reports nothing free  |
|   9 | `test_paint_and_measure_agree`                  |   ✅   | Paint and measure agree                  |
|  10 | `test_a_null_stack_is_not_a_fault`              |   ✅   | A null stack is not a fault              |

</details>

---

## test_hex - native_hex - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_interbus - native_interbus - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                    | Status | Description                                                             |
| --: | :-------------------------------------- | :----: | :---------------------------------------------------------------------- |
|   1 | `test_published_check_value`            |   ✅   | init=0xffff, xorout=0x0000: an empty message leaves the seed untouched. |
|   2 | `test_loopback_word`                    |   ✅   | Loopback word                                                           |
|   3 | `test_frame_layout`                     |   ✅   | The trailing FCS is the CRC of everything before it, big-endian.        |
|   4 | `test_zero_word_frame`                  |   ✅   | Zero word frame                                                         |
|   5 | `test_round_trip`                       |   ✅   | Round trip                                                              |
|   6 | `test_single_bit_corruption_is_refused` |   ✅   | Single bit corruption is refused                                        |
|   7 | `test_open_ring_is_refused`             |   ✅   | Open ring is refused                                                    |
|   8 | `test_parse_refuses_malformed_lengths`  |   ✅   | Parse refuses malformed lengths                                         |
|   9 | `test_build_refuses_a_short_buffer`     |   ✅   | Build refuses a short buffer                                            |

</details>

---

## test_iolink - native_iolink - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                    | Status | Description                                                                  |
| --: | :-------------------------------------- | :----: | :--------------------------------------------------------------------------- |
|   1 | `test_mc_octet_fields`                  |   ✅   | read, Page channel, address 0: 1 000 0 0000 -> 0x80 \| (1 << 5) = 0xA0       |
|   2 | `test_ckt_octet_fields`                 |   ✅   | The checksum argument is six bits: a wider one never overwrites the type.    |
|   3 | `test_cks_octet_fields`                 |   ✅   | Cks octet fields                                                             |
|   4 | `test_type0_read_checksum`              |   ✅   | Type0 read checksum                                                          |
|   5 | `test_type0_write_checksum`             |   ✅   | Type0 write checksum                                                         |
|   6 | `test_device_reply_checksum`            |   ✅   | Finalizing preserved the Event flag rather than overwriting the whole octet. |
|   7 | `test_finalize_then_verify`             |   ✅   | Finalize then verify                                                         |
|   8 | `test_single_bit_corruption_is_refused` |   ✅   | Single bit corruption is refused                                             |
|   9 | `test_bounds_are_refused`               |   ✅   | The message was not touched by the refused calls.                            |

</details>

---

## test_j2735 - native_j2735_uper - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                              | Status | Description                                                                |
| --: | :-------------------------------------------------------------------------------- | :----: | :------------------------------------------------------------------------- |
|   1 | `test_x691_published_bit_field_size_table`                                        |   ✅   | X691 published bit field size table                                        |
|   2 | `test_x691_a_range_of_one_occupies_no_bits`                                       |   ✅   | X691 a range of one occupies no bits                                       |
|   3 | `test_x691_widths_of_the_published_element_ranges`                                |   ✅   | X691 widths of the published element ranges                                |
|   4 | `test_the_modules_narrowed_bounds_keep_the_published_widths`                      |   ✅   | The modules narrowed bounds keep the published widths                      |
|   5 | `test_bits_are_packed_msb_first`                                                  |   ✅   | Bits are packed msb first                                                  |
|   6 | `test_cint_is_the_offset_from_the_lower_bound`                                    |   ✅   | Cint is the offset from the lower bound                                    |
|   7 | `test_bsm_core_bit_layout`                                                        |   ✅   | Bsm core bit layout                                                        |
|   8 | `test_bsm_core_round_trip_at_the_range_bounds`                                    |   ✅   | Bsm core round trip at the range bounds                                    |
|   9 | `test_bsm_core_bounds`                                                            |   ✅   | Bsm core bounds                                                            |
|  10 | `test_writer_overflow_latches`                                                    |   ✅   | Writer overflow latches                                                    |
|  11 | `test_reader_refuses_a_read_past_the_end`                                         |   ✅   | Reader refuses a read past the end                                         |
|  12 | `test_spat_round_trip`                                                            |   ✅   | Spat round trip                                                            |
|  13 | `test_spat_count_bounds`                                                          |   ✅   | Spat count bounds                                                          |
|  14 | `test_map_round_trip`                                                             |   ✅   | Map round trip                                                             |
|  15 | `test_map_bounds`                                                                 |   ✅   | Map bounds                                                                 |
|  16 | `test_movement_phase_state_reds_match_the_published_enumeration`                  |   ✅   | Movement phase state reds match the published enumeration                  |
|  17 | `test_movement_phase_state_greens_and_clearances_match_the_published_enumeration` |   ✅   | Movement phase state greens and clearances match the published enumeration |
|  18 | `test_every_movement_phase_index_survives_the_round_trip`                         |   ✅   | Every movement phase index survives the round trip                         |

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

|   # | Test                                                     | Status | Description                                                                |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------------------------------- |
|   1 | `test_no_interface_up_selects_nothing`                   |   ✅   | No interface up selects nothing                                            |
|   2 | `test_selection_is_total_order_over_priority_then_index` |   ✅   | The higher priority sits at the later index: position does not decide.     |
|   3 | `test_escalation_and_failover_walk_the_priority_order`   |   ✅   | Escalation and failover walk the priority order                            |
|   4 | `test_changed_reports_a_moved_egress_only`               |   ✅   | A lower-priority link going up and back down never touches the active one. |
|   5 | `test_select_does_not_move_the_active_interface`         |   ✅   | Select does not move the active interface                                  |
|   6 | `test_an_index_past_the_table_changes_nothing`           |   ✅   | An index past the table changes nothing                                    |
|   7 | `test_a_manager_with_no_table_carries_nothing`           |   ✅   | A manager with no table carries nothing                                    |
|   8 | `test_a_missing_manager_is_refused`                      |   ✅   | A missing manager is refused                                               |

</details>

---

## test_lora - native_lora - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_frame_build_then_parse`                      |   ✅   | Frame build then parse                      |
|   2 | `test_frame_parse_rejects_short`                   |   ✅   | Frame parse rejects short                   |
|   3 | `test_frame_build_bounds`                          |   ✅   | Frame build bounds                          |
|   4 | `test_init_verifies_chip_and_lands_in_standby`     |   ✅   | Init verifies chip and lands in standby     |
|   5 | `test_init_fails_on_wrong_version`                 |   ✅   | Init fails on wrong version                 |
|   6 | `test_init_programs_frequency`                     |   ✅   | Init programs frequency                     |
|   7 | `test_send_loads_fifo_and_starts_tx`               |   ✅   | Send loads fifo and starts tx               |
|   8 | `test_tx_done_flag`                                |   ✅   | Tx done flag                                |
|   9 | `test_set_rx_enters_continuous`                    |   ✅   | Set rx enters continuous                    |
|  10 | `test_recv_reads_frame_and_rssi`                   |   ✅   | Recv reads frame and rssi                   |
|  11 | `test_recv_no_packet`                              |   ✅   | Recv no packet                              |
|  12 | `test_recv_crc_error_dropped`                      |   ✅   | Recv crc error dropped                      |
|  13 | `test_recv_truncates_to_cap`                       |   ✅   | Recv truncates to cap                       |
|  14 | `test_frame_parse_build_guards`                    |   ✅   | Frame parse build guards                    |
|  15 | `test_frame_parse_null_guards_and_optional_outs`   |   ✅   | Frame parse null guards and optional outs   |
|  16 | `test_frame_build_null_and_size_guards`            |   ✅   | Frame build null and size guards            |
|  17 | `test_init_rejects_incomplete_bus`                 |   ✅   | Init rejects incomplete bus                 |
|  18 | `test_init_sets_low_data_rate_optimize_at_high_sf` |   ✅   | Init sets low data rate optimize at high sf |
|  19 | `test_driver_entry_points_reject_null_bus`         |   ✅   | Driver entry points reject null bus         |

</details>

---

## test_netadapt - native_netadapt - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                |
| --: | :--------------------------------------------------- | :----: | :------------------------------------------------------------------------- |
|   1 | `test_window_floor_at_and_below_the_reserve`         |   ✅   | one octet of spare: (8001-8000)/4 = 0, below the floor, so still the floor |
|   2 | `test_window_is_a_quarter_of_the_spare_heap`         |   ✅   | (40000 - 8000) / 4 = 32000 / 4 = 8000                                      |
|   3 | `test_window_clamps_to_the_ceiling`                  |   ✅   | (73540 - 8000) / 4 = 16385, one above the ceiling                          |
|   4 | `test_window_inverted_bounds_yield_the_floor`        |   ✅   | Window inverted bounds yield the floor                                     |
|   5 | `test_window_stays_inside_the_stated_bounds`         |   ✅   | Window stays inside the stated bounds                                      |
|   6 | `test_window_never_shrinks_as_the_heap_grows`        |   ✅   | Window never shrinks as the heap grows                                     |
|   7 | `test_dhcp_fallback_timeout_boundary`                |   ✅   | a zero timeout fires immediately                                           |
|   8 | `test_dhcp_fallback_attempt_budget`                  |   ✅   | Dhcp fallback attempt budget                                               |
|   9 | `test_dhcp_fallback_latches_as_the_counters_advance` |   ✅   | Dhcp fallback latches as the counters advance                              |

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

## test_ntcip - native_ntcip_oid - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                    |
| --: | :----------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_roots_sit_under_the_nema_enterprise_arc`   |   ✅   | Roots sit under the nema enterprise arc                                                        |
|   2 | `test_device_class_arc_separates_1202_from_1203` |   ✅   | Device class arc separates 1202 from 1203                                                      |
|   3 | `test_every_root_is_distinct`                    |   ✅   | Every root is distinct                                                                         |
|   4 | `test_oid_builder_appends_the_instance`          |   ✅   | Oid builder appends the instance                                                               |
|   5 | `test_oid_builder_scalar_takes_zero`             |   ✅   | the same root with a different index is a different OID                                        |
|   6 | `test_oid_builder_refuses_a_short_buffer`        |   ✅   | Oid builder refuses a short buffer                                                             |
|   7 | `test_oid_builder_null_guards`                   |   ✅   | Oid builder null guards                                                                        |
|   8 | `test_lengths_are_self_consistent`               |   ✅   | a table column carries entry + column arcs below the table node, so it is longer than a scalar |

</details>

---

## test_ota_rollback - native_ota_rollback - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                           | Status | Description                                                                              |
| --: | :------------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_a_confirmed_image_commits`                               |   ✅   | A confirmed image commits                                                                |
|   2 | `test_the_confirm_window_closes_at_its_own_length`             |   ✅   | A window of zero has already closed at boot, so an image that has to confirm and has not |
|   3 | `test_a_late_confirmation_still_commits`                       |   ✅   | A late confirmation still commits                                                        |
|   4 | `test_only_a_pending_image_is_ever_acted_on`                   |   ✅   | A state outside the defined set is not pending either, so it is left alone as well.      |
|   5 | `test_the_decision_carries_nothing_between_calls`              |   ✅   | The decision carries nothing between calls                                               |
|   6 | `test_the_image_states_are_distinct`                           |   ✅   | The image states are distinct                                                            |
|   7 | `test_the_seam_reports_the_state_the_part_holds`               |   ✅   | The seam reports the state the part holds                                                |
|   8 | `test_a_tick_commits_a_confirmed_image_through_the_seam`       |   ✅   | A tick commits a confirmed image through the seam                                        |
|   9 | `test_a_tick_rolls_back_an_unconfirmed_image_through_the_seam` |   ✅   | A tick rolls back an unconfirmed image through the seam                                  |
|  10 | `test_a_tick_leaves_a_settled_image_alone_at_the_seam`         |   ✅   | A tick leaves a settled image alone at the seam                                          |

</details>

---

## test_packml - native_packml - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                                                              |
| --: | :-------------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_statecurrent_values_are_the_published_numbers`            |   ✅   | Statecurrent values are the published numbers                                            |
|   2 | `test_control_command_numbers`                                  |   ✅   | Control command numbers                                                                  |
|   3 | `test_production_path`                                          |   ✅   | execute_complete only fires from Execute                                                 |
|   4 | `test_hold_branch_returns_to_execute`                           |   ✅   | Hold branch returns to execute                                                           |
|   5 | `test_suspend_branch_returns_to_execute`                        |   ✅   | Suspend branch returns to execute                                                        |
|   6 | `test_abort_is_legal_everywhere_but_the_abort_branch`           |   ✅   | Aborting -SC-> Aborted, and only Clear leaves it: Clearing -SC-> Stopped                 |
|   7 | `test_stop_is_legal_everywhere_but_the_stop_and_abort_branches` |   ✅   | Stop is legal everywhere but the stop and abort branches                                 |
|   8 | `test_clear_and_reset_are_state_specific`                       |   ✅   | Clear and reset are state specific                                                       |
|   9 | `test_illegal_commands_leave_the_state_unchanged`               |   ✅   | the null command never moves anything                                                    |
|  10 | `test_acting_states_advance_and_wait_states_do_not`             |   ✅   | the seven wait states, named: Stopped, Idle, Suspended, Execute, Aborted, Held, Complete |
|  11 | `test_names`                                                    |   ✅   | Names                                                                                    |
|  12 | `test_service_initializes_stopped`                              |   ✅   | Service initializes stopped                                                              |
|  13 | `test_service_follows_the_engine`                               |   ✅   | State-Complete in a wait state is a no-op                                                |
|  14 | `test_service_counts_only_while_executing`                      |   ✅   | ending the run leaves Execute, so a further unit is not counted                          |
|  15 | `test_service_mode_change_is_restricted`                        |   ✅   | Service mode change is restricted                                                        |
|  16 | `test_service_speed_is_reported_only_while_executing`           |   ✅   | Service speed is reported only while executing                                           |
|  17 | `test_service_timers_measure_from_their_own_marks`              |   ✅   | Service timers measure from their own marks                                              |

</details>

---

## test_pn532 - native_pn532 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                    | Status | Description                                                                               |
| --: | :-------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_um0701_getfirmwareversion_frames` |   ✅   | the command frame, built from its command code alone                                      |
|   2 | `test_frame_identifiers`                |   ✅   | Frame identifiers                                                                         |
|   3 | `test_ack_frame`                        |   ✅   | five bytes cannot yet be told apart from the head of a longer frame                       |
|   4 | `test_nack_is_not_an_ack`               |   ✅   | nor is it an information frame: LEN + LCS is 0xFF, not 0x00.                              |
|   5 | `test_ack_is_not_an_information_frame`  |   ✅   | Ack is not an information frame                                                           |
|   6 | `test_um0701_error_frame`               |   ✅   | Um0701 error frame                                                                        |
|   7 | `test_round_trip`                       |   ✅   | Round trip                                                                                |
|   8 | `test_incomplete_frame_asks_for_more`   |   ✅   | index 0..1 of a partial frame is still a legal preamble, so every prefix is "more needed" |
|   9 | `test_malformed_frames_are_refused`     |   ✅   | LEN 0 has no room for the TFI section 6.2.1.1 requires. LCS keeps the relation.           |
|  10 | `test_over_length_is_refused`           |   ✅   | LEN 20, LCS 0xEC keeps LEN + LCS == 0, so only the length itself is out of range.         |
|  11 | `test_build_refuses_bad_arguments`      |   ✅   | a null payload with zero length is the empty frame, which is legal                        |

</details>

---

## test_pqc_sha3 - native_pqc - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_pqc_sha3 - native_sha3_kat - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_crc - native_primitives - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_catalogue_check_values`                         |   ✅   | Catalogue check values                                                                         |
|   2 | `test_reflection_flags_actually_apply`                |   ✅   | ...and so must differing init values on otherwise identical parameters                         |
|   3 | `test_streaming_matches_the_one_shot`                 |   ✅   | octet-at-a-time is the same thing taken to the limit                                           |
|   4 | `test_the_intermediate_register_is_not_the_crc`       |   ✅   | The intermediate register is not the crc                                                       |
|   5 | `test_single_bit_flip_changes_the_crc`                |   ✅   | Single bit flip changes the crc                                                                |
|   6 | `test_order_sensitivity`                              |   ✅   | Order sensitivity                                                                              |
|   7 | `test_leading_zeros_are_significant`                  |   ✅   | Leading zeros are significant                                                                  |
|   8 | `test_empty_input_is_the_bare_init`                   |   ✅   | With no octets folded in, the result is init through the output stage - not an error.          |
|   9 | `test_width_is_respected`                             |   ✅   | Every result must fit its declared width - a leaked high bit would corrupt a packed frame.     |
|  10 | `test_out_of_range_width_is_clamped`                  |   ✅   | Out of range width is clamped                                                                  |
|  11 | `test_engine_matches_the_hand_rolled_implementations` |   ✅   | A spread of lengths, including the empty and single-octet degenerate cases, over a buffer with |
|  12 | `test_null_guards`                                    |   ✅   | Null guards                                                                                    |

</details>

---

## test_crc - native_crc - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_catalogue_check_values`                         |   ✅   | Catalogue check values                                                                         |
|   2 | `test_reflection_flags_actually_apply`                |   ✅   | ...and so must differing init values on otherwise identical parameters                         |
|   3 | `test_streaming_matches_the_one_shot`                 |   ✅   | octet-at-a-time is the same thing taken to the limit                                           |
|   4 | `test_the_intermediate_register_is_not_the_crc`       |   ✅   | The intermediate register is not the crc                                                       |
|   5 | `test_single_bit_flip_changes_the_crc`                |   ✅   | Single bit flip changes the crc                                                                |
|   6 | `test_order_sensitivity`                              |   ✅   | Order sensitivity                                                                              |
|   7 | `test_leading_zeros_are_significant`                  |   ✅   | Leading zeros are significant                                                                  |
|   8 | `test_empty_input_is_the_bare_init`                   |   ✅   | With no octets folded in, the result is init through the output stage - not an error.          |
|   9 | `test_width_is_respected`                             |   ✅   | Every result must fit its declared width - a leaked high bit would corrupt a packed frame.     |
|  10 | `test_out_of_range_width_is_clamped`                  |   ✅   | Out of range width is clamped                                                                  |
|  11 | `test_engine_matches_the_hand_rolled_implementations` |   ✅   | A spread of lengths, including the empty and single-octet degenerate cases, over a buffer with |
|  12 | `test_null_guards`                                    |   ✅   | Null guards                                                                                    |

</details>

---

## test_quic_varint - native_quic_varint - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                         | Status | Description                                                                      |
| --: | :------------------------------------------- | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_rfc9000_appendix_a1_vectors`           |   ✅   | Rfc9000 appendix a1 vectors                                                      |
|   2 | `test_non_minimal_encoding_decodes`          |   ✅   | The encoder still emits the shortest form for the same value.                    |
|   3 | `test_table4_length_boundaries`              |   ✅   | 2^62-1 is the top of the last row, and QUIC_VARINT_MAX must be that same number. |
|   4 | `test_boundary_encodings_carry_their_prefix` |   ✅   | Boundary encodings carry their prefix                                            |
|   5 | `test_above_the_62_bit_range_is_refused`     |   ✅   | Above the 62 bit range is refused                                                |
|   6 | `test_encode_refuses_a_short_buffer`         |   ✅   | cap exactly equal to the encoding length is enough.                              |
|   7 | `test_decode_refuses_a_truncated_input`      |   ✅   | Decode refuses a truncated input                                                 |
|   8 | `test_round_trip_over_every_length_class`    |   ✅   | Round trip over every length class                                               |

</details>

---

## test_safety_scl - native_safety_scl - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                                |
| --: | :-------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_init_starts_in_init_with_no_fault`            |   ✅   | Init starts in init with no fault                                                          |
|   2 | `test_first_valid_frame_runs_the_connection`        |   ✅   | and the run continues, one counter at a time                                               |
|   3 | `test_every_black_channel_failure_is_detected`      |   ✅   | corruption: the profile's CRC rejected the frame                                           |
|   4 | `test_corruption_is_diagnosed_before_the_counter`   |   ✅   | Corruption is diagnosed before the counter                                                 |
|   5 | `test_failsafe_never_self_heals`                    |   ✅   | the frame that should have arrived, arriving correctly, is still refused                   |
|   6 | `test_watchdog_fires_at_the_limit`                  |   ✅   | an accepted frame restarts the interval                                                    |
|   7 | `test_watchdog_does_not_run_before_the_first_frame` |   ✅   | Watchdog does not run before the first frame                                               |
|   8 | `test_zero_watchdog_disables_the_check`             |   ✅   | Zero watchdog disables the check                                                           |
|   9 | `test_watchdog_is_rollover_safe`                    |   ✅   | last frame at 16 ms before the wrap, polled 16 ms after it: 32 ms elapsed, inside a 100 ms |
|  10 | `test_counter_wraps_at_the_modulus`                 |   ✅   | the sender's own sequence over one cycle                                                   |
|  11 | `test_a_narrow_counter_still_catches_a_skip`        |   ✅   | A narrow counter still catches a skip                                                      |
|  12 | `test_reset_re_establishes_and_keeps_the_tallies`   |   ✅   | the re-established connection runs again, and its watchdog measures from the reset         |
|  13 | `test_a_null_connection_is_not_usable`              |   ✅   | A null connection is not usable                                                            |
|  14 | `test_reset_honours_the_counter_modulus`            |   ✅   | Reset honours the counter modulus                                                          |

</details>

---

## test_sigfox - native_sigfox_at - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                         | Status | Description                                                                                   |
| --: | :------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_sigfox_published_uplink_example`       |   ✅   | "AT$SF=" is 6, the hex is 2 per octet, and the command ends CR LF; the NUL is past the count. |
|   2 | `test_hex_is_uppercase_and_msb_nibble_first` |   ✅   | 0x0A and 0xB3 separate the two nibbles and cover both halves of the digit alphabet.           |
|   3 | `test_payload_cap_is_twelve_octets`          |   ✅   | A zero-length uplink is not a message either: AT$SF= carries no payload to send.              |
|   4 | `test_build_fails_closed`                    |   ✅   | Build fails closed                                                                            |
|   5 | `test_response_classification`               |   ✅   | The command echo the modem sends back first is not an answer.                                 |
|   6 | `test_response_respects_the_stated_length`   |   ✅   | Response respects the stated length                                                           |

</details>

---

## test_sleep_sched - native_sleep_sched - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_stays_awake_until_the_idle_threshold`          |   ✅   | Stays awake until the idle threshold          |
|   2 | `test_the_window_doubles_every_ramp`                 |   ✅   | The window doubles every ramp                 |
|   3 | `test_the_ceiling_clamps_off_the_doubling_grid`      |   ✅   | The ceiling clamps off the doubling grid      |
|   4 | `test_the_window_never_leaves_its_bounds`            |   ✅   | The window never leaves its bounds            |
|   5 | `test_the_doubling_clamps_instead_of_overflowing`    |   ✅   | The doubling clamps instead of overflowing    |
|   6 | `test_the_window_is_monotonic_in_the_idle_streak`    |   ✅   | The window is monotonic in the idle streak    |
|   7 | `test_no_ramp_goes_straight_to_the_ceiling`          |   ✅   | No ramp goes straight to the ceiling          |
|   8 | `test_a_ceiling_below_the_floor_clamps_to_the_floor` |   ✅   | A ceiling below the floor clamps to the floor |
|   9 | `test_the_idle_streak_is_wrap_safe`                  |   ✅   | The idle streak is wrap safe                  |
|  10 | `test_a_null_config_stays_awake`                     |   ✅   | A null config stays awake                     |
|  11 | `test_a_zero_floor_stays_inside_its_bounds`          |   ✅   | A zero floor stays inside its bounds          |

</details>

---

## test_snmp_ber - native_snmp_ber_x690 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                         |
| --: | :------------------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_x690_integer_minimal_octets`                 |   ✅   | 0 needs one content octet: 8.3.1 forbids a zero-length value.                       |
|   2 | `test_x690_object_identifier_first_subidentifier`  |   ✅   | The decoder must split the multi-octet first subidentifier back into arc0 and arc1. |
|   3 | `test_rfc3418_sysname_instance_oid`                |   ✅   | Rfc3418 sysname instance oid                                                        |
|   4 | `test_x690_multi_octet_subidentifier`              |   ✅   | X690 multi octet subidentifier                                                      |
|   5 | `test_rfc2578_application_types_stay_non_negative` |   ✅   | Counter32 0x80000000: top bit set -> 41 05 00 80 00 00 00                           |
|   6 | `test_x690_octet_string_and_null`                  |   ✅   | X690 octet string and null                                                          |
|   7 | `test_x690_long_form_length`                       |   ✅   | X690 long form length                                                               |
|   8 | `test_rfc3417_definite_long_sequence`              |   ✅   | And it reads back as one SEQUENCE holding those two values.                         |
|   9 | `test_put_raw_appends_verbatim`                    |   ✅   | Put raw appends verbatim                                                            |
|  10 | `test_rfc3417_indefinite_length_is_refused`        |   ✅   | Rfc3417 indefinite length is refused                                                |
|  11 | `test_decoder_refuses_a_length_past_the_buffer`    |   ✅   | short form: claims 10 content octets, 2 present                                     |
|  12 | `test_read_integer_refuses_malformed`              |   ✅   | Read integer refuses malformed                                                      |
|  13 | `test_read_integer_sign_extends`                   |   ✅   | 0x00 0x80 is +128, not -128: the sign octet is what separates them.                 |
|  14 | `test_encoder_fails_closed`                        |   ✅   | Encoder fails closed                                                                |
|  15 | `test_put_oid_bounds`                              |   ✅   | Put oid bounds                                                                      |
|  16 | `test_read_oid_bounds`                             |   ✅   | Read oid bounds                                                                     |
|  17 | `test_skip_bounds`                                 |   ✅   | Skip bounds                                                                         |
|  18 | `test_failed_decoder_stays_failed`                 |   ✅   | Failed decoder stays failed                                                         |
|  19 | `test_seq_end_refuses_over_65535_octets`           |   ✅   | Seq end refuses over 65535 octets                                                   |

</details>

---

## test_sockpool - native_sockpool - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                               |
| --: | :------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_init_leaves_every_slot_free`                 |   ✅   | Init leaves every slot free                                                               |
|   2 | `test_acquire_takes_free_slots_first`              |   ✅   | Acquire takes free slots first                                                            |
|   3 | `test_saturated_pool_evicts_in_last_used_order`    |   ✅   | A scripted mix of refreshes and new connections, driving the LRU choice around the table. |
|   4 | `test_touch_refreshes_the_lru_position`            |   ✅   | Touch refreshes the lru position                                                          |
|   5 | `test_touch_of_a_free_slot_is_ignored`             |   ✅   | Touch of a free slot is ignored                                                           |
|   6 | `test_release_returns_a_slot_to_the_free_list`     |   ✅   | Release returns a slot to the free list                                                   |
|   7 | `test_release_refuses_a_free_or_out_of_range_slot` |   ✅   | Release refuses a free or out of range slot                                               |
|   8 | `test_find_tracks_the_live_ids`                    |   ✅   | Find tracks the live ids                                                                  |
|   9 | `test_find_follows_a_recycle`                      |   ✅   | Find follows a recycle                                                                    |
|  10 | `test_out_parameters_are_optional`                 |   ✅   | Out parameters are optional                                                               |
|  11 | `test_a_pool_with_no_slots_fails_closed`           |   ✅   | A pool with no slots fails closed                                                         |
|  12 | `test_in_use_never_exceeds_the_table`              |   ✅   | In use never exceeds the table                                                            |

</details>

---

## test_telemetry - native_telemetry - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                            |
| --: | :--------------------------------------------- | :----: | :--------------------------------------------------------------------- |
|   1 | `test_window_mean_variance_stddev`             |   ✅   | Window mean variance stddev                                            |
|   2 | `test_window_evicts_the_oldest_sample`         |   ✅   | Window evicts the oldest sample                                        |
|   3 | `test_window_count_stops_at_capacity`          |   ✅   | Window count stops at capacity                                         |
|   4 | `test_window_variance_of_a_constant_is_zero`   |   ✅   | Window variance of a constant is zero                                  |
|   5 | `test_window_of_one_sample`                    |   ✅   | Window of one sample                                                   |
|   6 | `test_window_statistics_need_a_sample`         |   ✅   | Window statistics need a sample                                        |
|   7 | `test_window_init_empties_the_window`          |   ✅   | Window init empties the window                                         |
|   8 | `test_window_push_needs_bound_storage`         |   ✅   | Window push needs bound storage                                        |
|   9 | `test_rate_is_the_first_difference_per_second` |   ✅   | Rate is the first difference per second                                |
|  10 | `test_rate_scales_a_sub_second_interval`       |   ✅   | Rate scales a sub second interval                                      |
|  11 | `test_rate_of_a_zero_interval_is_zero`         |   ✅   | Rate of a zero interval is zero                                        |
|  12 | `test_rate_across_a_counter_rollover`          |   ✅   | Rate across a counter rollover                                         |
|  13 | `test_rate_init_drops_the_prior_sample`        |   ✅   | Rate init drops the prior sample                                       |
|  14 | `test_totalizer_is_the_trapezoidal_integral`   |   ✅   | The first sample only seeds an endpoint, so nothing is integrated yet. |
|  15 | `test_totalizer_of_a_constant_rate`            |   ✅   | Totalizer of a constant rate                                           |
|  16 | `test_totalizer_integrates_a_negative_rate`    |   ✅   | Totalizer integrates a negative rate                                   |
|  17 | `test_totalizer_reset`                         |   ✅   | Totalizer reset                                                        |
|  18 | `test_totalizer_across_a_counter_rollover`     |   ✅   | Totalizer across a counter rollover                                    |
|  19 | `test_calls_refuse_a_null_accumulator`         |   ✅   | Calls refuse a null accumulator                                        |
|  20 | `test_two_windows_are_independent`             |   ✅   | Two windows are independent                                            |

</details>

---

## test_thread - native_radio_thread - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                    | Status | Description                                                                               |
| --: | :-------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_x25_catalog_check_value`          |   ✅   | X25 catalog check value                                                                   |
|   2 | `test_rfc1662_good_fcs_residue`         |   ✅   | Rfc1662 good fcs residue                                                                  |
|   3 | `test_rfc1662_escape_table`             |   ✅   | Rfc1662 escape table                                                                      |
|   4 | `test_frame_round_trip`                 |   ✅   | Frame round trip                                                                          |
|   5 | `test_decode_rejects_a_corrupted_frame` |   ✅   | Decode rejects a corrupted frame                                                          |
|   6 | `test_decode_framing_faults`            |   ✅   | A payload that does not fit the caller's buffer is refused rather than truncated into it. |
|   7 | `test_encode_bounds`                    |   ✅   | The frame is all-or-nothing: one octet short of the exact length writes nothing.          |
|   8 | `test_spinel_packed_uint_vectors`       |   ✅   | Spinel packed uint vectors                                                                |
|   9 | `test_spinel_packed_uint_faults`        |   ✅   | A four-byte buffer cannot hold the five-byte encoding of a 32-bit value.                  |
|  10 | `test_spinel_header_bit_layout`         |   ✅   | Interface 0, transaction 1: 0b10 000 1 -> 0x81.                                           |
|  11 | `test_spinel_command_build_and_parse`   |   ✅   | Setting the PAN id: the property is 0x36, still one packed octet, and the value is the    |
|  12 | `test_spinel_command_through_hdlc`      |   ✅   | Spinel command through hdlc                                                               |
|  13 | `test_spinel_value_wire_layout`         |   ✅   | Spinel value wire layout                                                                  |
|  14 | `test_spinel_value_round_trip`          |   ✅   | Spinel value round trip                                                                   |
|  15 | `test_spinel_cursor_bounds_latch`       |   ✅   | A value with no NUL is not a UTF8 field.                                                  |
|  16 | `test_spinel_property_registry`         |   ✅   | Spinel property registry                                                                  |
|  17 | `test_spinel_status_names`              |   ✅   | Spinel status names                                                                       |
|  18 | `test_spinel_last_status_decode`        |   ✅   | Spinel last status decode                                                                 |
|  19 | `test_null_arguments_are_refused`       |   ✅   | Null arguments are refused                                                                |

</details>

---

## test_time_compat - native_time_compat - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_time_source - native_time_fallback - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                       |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_lowest_priority_value_is_queried_first`       |   ✅   | Lowest priority value is queried first            |
|   2 | `test_a_source_with_no_time_falls_through`          |   ✅   | A source with no time falls through               |
|   3 | `test_an_answer_stops_the_scan`                     |   ✅   | An answer stops the scan                          |
|   4 | `test_active_names_the_source_that_answered`        |   ✅   | the GNSS regains its fix and takes the query back |
|   5 | `test_no_valid_time_reports_zero_and_clears_active` |   ✅   | No valid time reports zero and clears active      |
|   6 | `test_empty_registry_and_null_callback`             |   ✅   | Empty registry and null callback                  |
|   7 | `test_registry_is_bounded`                          |   ✅   | Registry is bounded                               |
|   8 | `test_reset_clears_the_registry`                    |   ✅   | and the freed slot takes a registration again     |

</details>

---

## test_tls_policy - native_tls_policy - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                        |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------- |
|   1 | `test_the_published_version_words`                          |   ✅   | The published version words                                                        |
|   2 | `test_negotiation_picks_the_highest_common_version`         |   ✅   | both peers can do 1.3                                                              |
|   3 | `test_a_future_client_gets_the_server_ceiling`              |   ✅   | A future client gets the server ceiling                                            |
|   4 | `test_a_client_below_the_floor_is_refused`                  |   ✅   | a 1.2 client against a 1.3-only server has no overlap either                       |
|   5 | `test_an_inverted_server_range_negotiates_nothing`          |   ✅   | An inverted server range negotiates nothing                                        |
|   6 | `test_the_negotiated_version_is_always_inside_both_ranges`  |   ✅   | The negotiated version is always inside both ranges                                |
|   7 | `test_version_names`                                        |   ✅   | Version names                                                                      |
|   8 | `test_selection_follows_server_preference_not_client_order` |   ✅   | the client's own order is ignored: the server's first pinned suite wins both times |
|   9 | `test_no_overlap_selects_nothing`                           |   ✅   | an empty list on either side is no overlap                                         |
|  10 | `test_selection_refuses_a_null_list`                        |   ✅   | Selection refuses a null list                                                      |
|  11 | `test_a_selected_suite_was_always_both_pinned_and_offered`  |   ✅   | A selected suite was always both pinned and offered                                |
|  12 | `test_the_aead_suites`                                      |   ✅   | The aead suites                                                                    |
|  13 | `test_the_non_aead_suites`                                  |   ✅   | a code point next to a pinned one is not pinned by proximity                       |
|  14 | `test_an_aead_only_pin_can_only_select_aead`                |   ✅   | An aead only pin can only select aead                                              |

</details>

---

## test_utf8 - native_utf8 - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_wearlevel - native_wearlevel - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                    |
| --: | :--------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_pick_then_mark_levels_the_region_exactly`      |   ✅   | Pick then mark levels the region exactly                                       |
|   2 | `test_an_uneven_region_converges_to_level`           |   ✅   | An uneven region converges to level                                            |
|   3 | `test_pick_is_the_least_worn_slot_and_ties_go_low`   |   ✅   | Pick is the least worn slot and ties go low                                    |
|   4 | `test_pick_does_not_change_the_table`                |   ✅   | Pick does not change the table                                                 |
|   5 | `test_mark_bumps_exactly_one_slot`                   |   ✅   | Mark bumps exactly one slot                                                    |
|   6 | `test_a_saturated_count_never_wraps`                 |   ✅   | The saturated slot is still the most worn, so the pick stays on the other one. |
|   7 | `test_a_mark_past_the_table_bumps_nothing`           |   ✅   | A mark past the table bumps nothing                                            |
|   8 | `test_imbalance_is_the_high_water_mark_less_the_low` |   ✅   | A single slot has nothing to be out of balance with.                           |
|   9 | `test_an_absent_table_is_refused`                    |   ✅   | An absent table is refused                                                     |

</details>

---

## test_zigbee - native_radio_zigbee - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                         |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------ |
|   1 | `test_crc16_catalog_check_value`                 |   ✅   | Crc16 catalog check value                                           |
|   2 | `test_ug101_rst_frame`                           |   ✅   | Ug101 rst frame                                                     |
|   3 | `test_crc_covers_control_then_payload_msb_first` |   ✅   | And the encoder lays out the same octets the hand-built frame does. |
|   4 | `test_reserved_octets_are_escaped`               |   ✅   | Reserved octets are escaped                                         |
|   5 | `test_frame_round_trip`                          |   ✅   | Frame round trip                                                    |
|   6 | `test_empty_payload_round_trip`                  |   ✅   | Empty payload round trip                                            |
|   7 | `test_decode_rejects_a_corrupted_frame`          |   ✅   | Decode rejects a corrupted frame                                    |
|   8 | `test_decode_framing_faults`                     |   ✅   | Two octets cannot hold a control byte plus a two-octet CRC.         |
|   9 | `test_decode_refuses_a_short_payload_buffer`     |   ✅   | Decode refuses a short payload buffer                               |
|  10 | `test_decode_consumes_one_frame_from_a_stream`   |   ✅   | Decode consumes one frame from a stream                             |
|  11 | `test_encode_bounds`                             |   ✅   | Encode bounds                                                       |
|  12 | `test_decode_null_input`                         |   ✅   | Decode null input                                                   |

</details>

---

## test_zwave - native_radio_zwave - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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
