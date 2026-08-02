# Test Report

**Generated:** 2026-08-02 21:19:52
**Command:** `pio test` over 304 auto-discovered native envs (excludes native_pentest, native_codeql)
**Result:** ❌ 189 passed, 933 failed - 1729s

---

## Summary

| Suite              | Environment          | Tests | Status |     Duration |
| :----------------- | :------------------- | ----: | :----: | -----------: |
| `test_crc`         | `native_primitives`  |    11 |   ✅   | 00:00:00.863 |
| `test_ntp_server`  | `native_ntp_server`  |     9 |   ✅   | 00:00:00.852 |
| `test_dns_server`  | `native_dns_server`  |    13 |   ✅   | 00:00:00.813 |
| `test_rtc`         | `native_rtc`         |    13 |   ✅   | 00:00:00.826 |
| `test_mpr121`      | `native_mpr121`      |     6 |   ✅   | 00:00:00.831 |
| `test_sht3x`       | `native_sht3x`       |     7 |   ✅   | 00:00:00.845 |
| `test_pca9685`     | `native_pca9685`     |     5 |   ✅   | 00:00:00.841 |
| `test_ina219`      | `native_ina219`      |     5 |   ✅   | 00:00:00.829 |
| `test_mqtt`        | `native_mqtt`        |    24 |   ✅   | 00:00:00.858 |
| `test_time_source` | `native_time_source` |    11 |   ✅   | 00:00:00.837 |
| `test_cbor`        | `native_cbor`        |    25 |   ✅   | 00:00:00.879 |
| `test_msgpack`     | `native_msgpack`     |    29 |   ✅   | 00:00:00.875 |
| `test_fdc2214`     | `native_fdc2214`     |     5 |   ✅   | 00:00:00.877 |
| `test_ldc1614`     | `native_ldc1614`     |     5 |   ✅   | 00:00:00.856 |
| `test_vl53l0x`     | `native_vl53l0x`     |     3 |   ✅   | 00:00:00.828 |
| `test_span`        | `native_span`        |    18 |   ✅   | 00:00:00.829 |

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

## test_ntp_server - native_ntp_server - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the NTP server response codec (services/pc_ntp_server_build_response): a pure_

|   # | Test                              | Status | Description                                                                             |
| --: | :-------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_happy_path_fields`          |   ✅   | Happy path fields                                                                       |
|   2 | `test_origin_is_client_transmit`  |   ✅   | Origin is client transmit                                                               |
|   3 | `test_version_echo`               |   ✅   | Version echo                                                                            |
|   4 | `test_poll_echo_and_default`      |   ✅   | Poll echo and default                                                                   |
|   5 | `test_stratum_passthrough`        |   ✅   | Stratum passthrough                                                                     |
|   6 | `test_big_endian_encoding`        |   ✅   | Big endian encoding                                                                     |
|   7 | `test_length_guards`              |   ✅   | Length guards                                                                           |
|   8 | `test_root_dispersion_advertised` |   ✅   | Root dispersion advertised                                                              |
|   9 | `test_begin_is_host_stub`         |   ✅   | On a host build (no ARDUINO/lwIP) pc_ntp_server_begin() cannot bind UDP/123, so it must |

</details>

---

## test_dns_server - native_dns_server - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the authoritative DNS server (services/net/dns_server): the pure response_

|   # | Test                                     | Status | Description                       |
| --: | :--------------------------------------- | :----: | :-------------------------------- |
|   1 | `test_a_record_answer`                   |   ✅   | A record answer                   |
|   2 | `test_nxdomain`                          |   ✅   | Nxdomain                          |
|   3 | `test_non_a_query_no_error`              |   ✅   | Non a query no error              |
|   4 | `test_multilabel_name_reaches_resolver`  |   ✅   | Multilabel name reaches resolver  |
|   5 | `test_malformed_guards`                  |   ✅   | Malformed guards                  |
|   6 | `test_table_add_lookup_case_insensitive` |   ✅   | Table add lookup case insensitive |
|   7 | `test_end_to_end_with_table`             |   ✅   | End to end with table             |
|   8 | `test_dns_opcode_notimp`                 |   ✅   | Dns opcode notimp                 |
|   9 | `test_dns_truncated_questions`           |   ✅   | Dns truncated questions           |
|  10 | `test_dns_oversized_name`                |   ✅   | Dns oversized name                |
|  11 | `test_dns_question_exceeds_out_cap`      |   ✅   | Dns question exceeds out cap      |
|  12 | `test_dns_add_and_lookup_guards`         |   ✅   | Dns add and lookup guards         |
|  13 | `test_dns_begin_host_stub`               |   ✅   | Dns begin host stub               |

</details>

---

## test_rtc - native_rtc - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DS1307/DS3231 RTC conversions (services/peripherals/rtc): the BCD time registers_

|   # | Test                               | Status | Description                                                                                |
| --: | :--------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_known_epoch_2000`            |   ✅   | Known epoch 2000                                                                           |
|   2 | `test_decode_datetime`             |   ✅   | Decode datetime                                                                            |
|   3 | `test_12hour_mode_equivalence`     |   ✅   | 14:00 as 24-hour (0x14) and as 12-hour PM 2 (0x40                                          | 0x20 | 0x02) must be the same time. |
|   4 | `test_12hour_midnight_and_noon`    |   ✅   | 12hour midnight and noon                                                                   |
|   5 | `test_roundtrip_over_range`        |   ✅   | Roundtrip over range                                                                       |
|   6 | `test_leap_day`                    |   ✅   | Leap day                                                                                   |
|   7 | `test_masks_ch_and_century`        |   ✅   | The DS1307 clock-halt bit (sec bit7) and the DS3231 century bit (month bit7) must be       |
|   8 | `test_invalid_guards`              |   ✅   | Invalid guards                                                                             |
|   9 | `test_null_regs_pointer`           |   ✅   | Null regs pointer                                                                          |
|  10 | `test_invalid_guards_upper_bounds` |   ✅   | Invalid guards upper bounds                                                                |
|  11 | `test_12hour_invalid_h12`          |   ✅   | 12hour invalid h12                                                                         |
|  12 | `test_epoch_overflow_rejected`     |   ✅   | Epoch overflow rejected                                                                    |
|  13 | `test_host_i2c_stubs`              |   ✅   | Host build: no I2C bus. begin() reports ready, reads yield 0, set fails, time source is 0. |

</details>

---

## test_mpr121 - native_mpr121 - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the MPR121 capacitive-touch codec (services/peripherals/mpr121): decoding the touch-status_

|   # | Test                               | Status | Description                                                                              |
| --: | :--------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_touched_decode`              |   ✅   | low byte -> electrodes 0..7; here electrodes 0 and 2.                                    |
|   2 | `test_prox_and_overcurrent_masked` |   ✅   | Proximity (status bit 12 = high-byte bit 4) and OVCF (bit 15 = high-byte bit 7) must not |
|   3 | `test_word10`                      |   ✅   | Word10                                                                                   |
|   4 | `test_build_init_bytes`            |   ✅   | Build init bytes                                                                         |
|   5 | `test_build_init_guards`           |   ✅   | one electrode: 26 fixed + 4 threshold + 8 tail = 38 bytes; ECR enables 1 electrode.      |
|   6 | `test_host_i2c_stubs`              |   ✅   | Host build: no I2C bus. begin() fails, register reads return 0.                          |

</details>

---

## test_sht3x - native_sht3x - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Sensirion SHT3x codec (services/peripherals/sht3x): the CRC-8 against the datasheet_

|   # | Test                         | Status | Description                                                   |
| --: | :--------------------------- | :----: | :------------------------------------------------------------ |
|   1 | `test_crc8_datasheet_vector` |   ✅   | Crc8 datasheet vector                                         |
|   2 | `test_conversion`            |   ✅   | Endpoints of the linear map are exact.                        |
|   3 | `test_parse_valid`           |   ✅   | Parse valid                                                   |
|   4 | `test_parse_bad_crc`         |   ✅   | Parse bad crc                                                 |
|   5 | `test_parse_null_out`        |   ✅   | Parse null out                                                |
|   6 | `test_parse_null_resp`       |   ✅   | Parse null resp                                               |
|   7 | `test_host_i2c_stubs`        |   ✅   | Host build: no I2C. begin() fails and read() reports failure. |

</details>

---

## test_pca9685 - native_pca9685 - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the PCA9685 PWM/servo codec (services/peripherals/pca9685): the PRESCALE computation from a_

|   # | Test                                | Status | Description                                                           |
| --: | :---------------------------------- | :----: | :-------------------------------------------------------------------- |
|   1 | `test_prescale`                     |   ✅   | Prescale                                                              |
|   2 | `test_channel_reg`                  |   ✅   | Channel reg                                                           |
|   3 | `test_us_to_count`                  |   ✅   | Us to count                                                           |
|   4 | `test_set_pwm_bytes`                |   ✅   | channel 0, on=0, off=307 (0x133) -> reg 0x06, off_l 0x33, off_h 0x01. |
|   5 | `test_prescale_zero_and_host_stubs` |   ✅   | Zero frequency takes the max-prescale early return.                   |

</details>

---

## test_ina219 - native_ina219 - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the INA219 current/power codec (services/peripherals/ina219): decoding the bus-voltage_

|   # | Test                              | Status | Description                                                                                   |
| --: | :-------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_bus_mv`                     |   ✅   | 3300 mV -> value 825 (0x339) in bits [15:3] -> register 825<<3 = 0x19C8.                      |
|   2 | `test_shunt_uv`                   |   ✅   | Shunt uv                                                                                      |
|   3 | `test_calibration`                |   ✅   | Calibration                                                                                   |
|   4 | `test_current_and_power`          |   ✅   | current = raw * current_LSB (uA); power = raw * 20 * current_LSB (uW).                        |
|   5 | `test_host_i2c_stubs_fail_closed` |   ✅   | On a host build there is no I2C: begin and every read fail closed (return false), so a caller |

</details>

---

## test_mqtt - native_mqtt - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host unit tests for the MQTT 3.1.1 packet codec (env:native_mqtt)._

|   # | Test                                            | Status | Description                                                                 |
| --: | :---------------------------------------------- | :----: | :-------------------------------------------------------------------------- |
|   1 | `test_build_guards_and_overflow`                |   ✅   | Build guards and overflow                                                   |
|   2 | `test_parse_guards`                             |   ✅   | Parse guards                                                                |
|   3 | `test_build_null_topic_guards_and_empty_field`  |   ✅   | Build null topic guards and empty field                                     |
|   4 | `test_parse_short_len_and_null_outparam_guards` |   ✅   | Parse short len and null outparam guards                                    |
|   5 | `test_host_transport_stubs`                     |   ✅   | Host transport stubs                                                        |
|   6 | `test_remlen_boundaries`                        |   ✅   | Remlen boundaries                                                           |
|   7 | `test_remlen_too_big`                           |   ✅   | Remlen too big                                                              |
|   8 | `test_remlen_decode_incomplete`                 |   ✅   | Remlen decode incomplete                                                    |
|   9 | `test_remlen_decode_malformed`                  |   ✅   | Remlen decode malformed                                                     |
|  10 | `test_connect_minimal`                          |   ✅   | Connect minimal                                                             |
|  11 | `test_connect_full`                             |   ✅   | Connect full                                                                |
|  12 | `test_publish_qos0_roundtrip`                   |   ✅   | Publish qos0 roundtrip                                                      |
|  13 | `test_publish_qos1_flags_and_id`                |   ✅   | Publish qos1 flags and id                                                   |
|  14 | `test_publish_topic_overflow_rejected`          |   ✅   | Publish topic overflow rejected                                             |
|  15 | `test_publish_qos3_rejected`                    |   ✅   | Publish qos3 rejected                                                       |
|  16 | `test_publish_wildcard_topic_rejected`          |   ✅   | Publish wildcard topic rejected                                             |
|  17 | `test_publish_topic_nul_or_bad_utf8_rejected`   |   ✅   | topic length 2, bytes {0xC3,0x28} = invalid UTF-8 sequence, qos0 (flags 0). |
|  18 | `test_subscribe`                                |   ✅   | Subscribe                                                                   |
|  19 | `test_unsubscribe`                              |   ✅   | Unsubscribe                                                                 |
|  20 | `test_ack_packets`                              |   ✅   | Ack packets                                                                 |
|  21 | `test_connack`                                  |   ✅   | Connack                                                                     |
|  22 | `test_suback`                                   |   ✅   | Suback                                                                      |
|  23 | `test_ping_disconnect`                          |   ✅   | Ping disconnect                                                             |
|  24 | `test_fixed_header_multibyte_remlen`            |   ✅   | Remaining length 300 -> 2-byte field {0xAC, 0x02}.                          |

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

## test_cbor - native_cbor - ✅ 25 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CBOR encoder (network_drivers/presentation/codec/cbor). Expected_

|   # | Test                                 | Status | Description                   |
| --: | :----------------------------------- | :----: | :---------------------------- |
|   1 | `test_cbor_decode_more_types`        |   ✅   | Cbor decode more types        |
|   2 | `test_cbor_head_reserved_and_trunc`  |   ✅   | Cbor head reserved and trunc  |
|   3 | `test_cbor_read_empty`               |   ✅   | Cbor read empty               |
|   4 | `test_uint`                          |   ✅   | Uint                          |
|   5 | `test_peek_each_type`                |   ✅   | Peek each type                |
|   6 | `test_uint_8byte`                    |   ✅   | Uint 8byte                    |
|   7 | `test_read_double_encoded_float`     |   ✅   | Read double encoded float     |
|   8 | `test_read_map_type_mismatch`        |   ✅   | Read map type mismatch        |
|   9 | `test_int`                           |   ✅   | Int                           |
|  10 | `test_text`                          |   ✅   | Text                          |
|  11 | `test_bytes`                         |   ✅   | Bytes                         |
|  12 | `test_simple`                        |   ✅   | Simple                        |
|  13 | `test_float`                         |   ✅   | Float                         |
|  14 | `test_array_and_map`                 |   ✅   | Array and map                 |
|  15 | `test_overflow_fails_closed`         |   ✅   | Overflow fails closed         |
|  16 | `test_cbor_text_null_ptr`            |   ✅   | Cbor text null ptr            |
|  17 | `test_cbor_reader_sticky_err_repeat` |   ✅   | Cbor reader sticky err repeat |
|  18 | `test_peek_edge_cases`               |   ✅   | Peek edge cases               |
|  19 | `test_cbor_read_str_length_overrun`  |   ✅   | Cbor read str length overrun  |
|  20 | `test_decode_uint`                   |   ✅   | Decode uint                   |
|  21 | `test_decode_int`                    |   ✅   | Decode int                    |
|  22 | `test_decode_float_roundtrip`        |   ✅   | Decode float roundtrip        |
|  23 | `test_decode_roundtrip_map`          |   ✅   | Decode roundtrip map          |
|  24 | `test_decode_truncated`              |   ✅   | Decode truncated              |
|  25 | `test_decode_type_mismatch`          |   ✅   | Decode type mismatch          |

</details>

---

## test_msgpack - native_msgpack - ✅ 29 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the MessagePack encoder and decoder_

|   # | Test                                   | Status | Description                                                                 |
| --: | :------------------------------------- | :----: | :-------------------------------------------------------------------------- |
|   1 | `test_encode_wide32`                   |   ✅   | Encode wide32                                                               |
|   2 | `test_peek_wide_types`                 |   ✅   | Peek wide types                                                             |
|   3 | `test_read_int_all_widths`             |   ✅   | Read int all widths                                                         |
|   4 | `test_read_on_empty_reader`            |   ✅   | Read on empty reader                                                        |
|   5 | `test_read_wrong_type_byte`            |   ✅   | Read wrong type byte                                                        |
|   6 | `test_read_truncated_widths`           |   ✅   | Read truncated widths                                                       |
|   7 | `test_uint`                            |   ✅   | Uint                                                                        |
|   8 | `test_wide_roundtrip`                  |   ✅   | Wide roundtrip                                                              |
|   9 | `test_decode_wide_fails_closed`        |   ✅   | str16 header claims 300 bytes, body absent                                  |
|  10 | `test_int`                             |   ✅   | Int                                                                         |
|  11 | `test_str`                             |   ✅   | Str                                                                         |
|  12 | `test_str_null_pointer`                |   ✅   | Str null pointer                                                            |
|  13 | `test_bytes`                           |   ✅   | Bytes                                                                       |
|  14 | `test_simple`                          |   ✅   | Simple                                                                      |
|  15 | `test_float`                           |   ✅   | Float                                                                       |
|  16 | `test_array_and_map`                   |   ✅   | Array and map                                                               |
|  17 | `test_overflow_fails_closed`           |   ✅   | Overflow fails closed                                                       |
|  18 | `test_decode_uint`                     |   ✅   | positive fixint, uint8, uint16, uint32, uint64                              |
|  19 | `test_decode_int`                      |   ✅   | negative fixint (-1, -32), int8 (-128), int16 (-32768), int32 (-2147483648) |
|  20 | `test_decode_str_and_bytes`            |   ✅   | Decode str and bytes                                                        |
|  21 | `test_decode_simple_and_float`         |   ✅   | Decode simple and float                                                     |
|  22 | `test_decode_array_and_map`            |   ✅   | Decode array and map                                                        |
|  23 | `test_decode_roundtrip`                |   ✅   | Encode a small document, then decode it back and check each field.          |
|  24 | `test_decode_fails_closed`             |   ✅   | truncated uint16 (header says read 2 more bytes, only 1 present)            |
|  25 | `test_read_nil_wrong_byte`             |   ✅   | Read nil wrong byte                                                         |
|  26 | `test_reads_after_sticky_error`        |   ✅   | Reads after sticky error                                                    |
|  27 | `test_read_str_below_fixstr_range`     |   ✅   | Read str below fixstr range                                                 |
|  28 | `test_read_array_below_fixarray_range` |   ✅   | Read array below fixarray range                                             |
|  29 | `test_read_count_wide32_success`       |   ✅   | Read count wide32 success                                                   |

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
