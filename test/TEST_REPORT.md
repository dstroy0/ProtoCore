# Test Report

**Generated:** 2026-08-03 06:34:20
**Command:** `pio test` over 304 auto-discovered native envs (excludes native_pentest, native_codeql)
**Result:** ✅ 13 passed - 67s

---

## Summary

| Suite                 | Environment             | Tests | Status |     Duration |
| :-------------------- | :---------------------- | ----: | :----: | -----------: |
| `test_ip`             | `native_ip`             |     0 |   ✅   | 00:00:00.855 |
| `test_ssh_ed25519`    | `native_ssh_ed25519`    |     0 |   ✅   | 00:00:02.491 |
| `test_ssh_inflate`    | `native_ssh_inflate`    |     0 |   ✅   | 00:00:00.840 |
| `test_promisc`        | `native_promisc`        |     0 |   ✅   | 00:00:00.844 |
| `test_dmx`            | `native_dmx`            |     0 |   ✅   | 00:00:00.820 |
| `test_nmea0183`       | `native_nmea0183`       |     0 |   ✅   | 00:00:00.860 |
| `test_ptp`            | `native_ptp`            |     0 |   ✅   | 00:00:00.839 |
| `test_base64`         | `native_base64_scalar`  |     0 |   ✅   | 00:00:00.828 |
| `test_stomp`          | `native_stomp`          |     0 |   ✅   | 00:00:00.832 |
| `test_flow_export`    | `native_flow_export`    |     0 |   ✅   | 00:00:00.843 |
| `test_enocean`        | `native_enocean`        |     0 |   ✅   | 00:00:00.804 |
| `test_pn532`          | `native_pn532`          |     0 |   ✅   | 00:00:00.835 |
| `test_sigfox`         | `native_sigfox`         |     0 |   ✅   | 00:00:00.818 |
| `test_sunspec`        | `native_sunspec`        |     0 |   ✅   | 00:00:00.848 |
| `test_c37118`         | `native_c37118`         |     0 |   ✅   | 00:00:00.821 |
| `test_hostlink`       | `native_hostlink`       |     0 |   ✅   | 00:00:00.840 |
| `test_haas_mdc`       | `native_haas_mdc`       |     0 |   ✅   | 00:00:00.864 |
| `test_lsv2`           | `native_lsv2`           |     0 |   ✅   | 00:00:00.859 |
| `test_df1`            | `native_df1`            |     0 |   ✅   | 00:00:00.841 |
| `test_melsec`         | `native_melsec`         |     0 |   ✅   | 00:00:00.859 |
| `test_fanuc_j519`     | `native_fanuc_j519`     |     0 |   ✅   | 00:00:00.863 |
| `test_pqc_sntrup761`  | `native_pqc`            |     0 |   ✅   | 00:00:01.727 |
| `test_rtcm3`          | `native_rtcm3`          |     0 |   ✅   | 00:00:00.850 |
| `test_enip`           | `native_enip`           |     0 |   ✅   | 00:00:00.841 |
| `test_amqp`           | `native_amqp`           |     0 |   ✅   | 00:00:00.829 |
| `test_totp`           | `native_totp`           |     0 |   ✅   | 00:00:00.860 |
| `test_webhook`        | `native_webhook`        |     0 |   ✅   | 00:00:00.857 |
| `test_ntp_server`     | `native_ntp_server`     |     0 |   ✅   | 00:00:00.818 |
| `test_dns_server`     | `native_dns_server`     |    13 |   ✅   | 00:00:00.848 |
| `test_hmmd`           | `native_hmmd`           |     0 |   ✅   | 00:00:00.844 |
| `test_quic_varint`    | `native_quic_varint`    |     0 |   ✅   | 00:00:00.806 |
| `test_jwt`            | `native_jwt`            |     0 |   ✅   | 00:00:01.029 |
| `test_device_id`      | `native_device_id`      |     0 |   ✅   | 00:00:00.838 |
| `test_net_egress`     | `native_net_egress`     |     0 |   ✅   | 00:00:00.793 |
| `test_udp_telemetry`  | `native_udp_telemetry`  |     0 |   ✅   | 00:00:00.809 |
| `test_sleep_sched`    | `native_sleep_sched`    |     0 |   ✅   | 00:00:00.798 |
| `test_wearlevel`      | `native_wearlevel`      |     0 |   ✅   | 00:00:00.803 |
| `test_netadapt`       | `native_netadapt`       |     0 |   ✅   | 00:00:00.783 |
| `test_xmpp`           | `native_xmpp`           |     0 |   ✅   | 00:00:00.805 |
| `test_nema_ts2`       | `native_nema_ts2`       |     0 |   ✅   | 00:00:00.789 |
| `test_snp`            | `native_snp`            |     0 |   ✅   | 00:00:00.793 |
| `test_ntcip`          | `native_ntcip`          |     0 |   ✅   | 00:00:00.793 |
| `test_profibus`       | `native_profibus`       |     0 |   ✅   | 00:00:00.795 |
| `test_interbus`       | `native_interbus`       |     0 |   ✅   | 00:00:00.810 |
| `test_ocit`           | `native_ocit`           |     0 |   ✅   | 00:00:00.796 |
| `test_exc_decoder`    | `native_exc_decoder`    |     0 |   ✅   | 00:00:00.801 |
| `test_http_delivery`  | `native_http_delivery`  |     0 |   ✅   | 00:00:00.783 |
| `test_mdns_adaptive`  | `native_mdns_adaptive`  |     0 |   ✅   | 00:00:00.791 |
| `test_sockpool`       | `native_sockpool`       |     0 |   ✅   | 00:00:00.809 |
| `test_radio_sniff`    | `native_radio_sniff`    |     0 |   ✅   | 00:00:00.788 |
| `test_tls_policy`     | `native_tls_policy`     |     0 |   ✅   | 00:00:00.791 |
| `test_quic_packet`    | `native_quic_packet`    |     0 |   ✅   | 00:00:00.789 |
| `test_dtls_handshake` | `native_dtls_hs`        |     0 |   ✅   | 00:00:01.013 |
| `test_ssh_chachapoly` | `native_ssh_chachapoly` |     0 |   ✅   | 00:00:00.979 |
| `test_frame`          | `native_frame`          |     0 |   ✅   | 00:00:00.807 |
| `test_span`           | `native_span`           |     0 |   ✅   | 00:00:00.795 |
| `test_dtls_tls13`     | `native_dtls_tls13`     |     0 |   ✅   | 00:00:09.084 |
| `test_quic_server`    | `native_quic_server`    |     0 |   ✅   | 00:00:07.555 |
| `test_robotics`       | `native_robotics`       |     0 |   ✅   | 00:00:01.431 |

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
