# Test Report

**Generated:** 2026-08-04 01:51:24
**Command:** `pio test` over 304 auto-discovered native envs (excludes native_pentest, native_codeql)
**Result:** ✅ 13 passed - 249s

---

## Summary

| Suite                    | Environment             | Tests | Status |     Duration |
| :----------------------- | :---------------------- | ----: | :----: | -----------: |
| `test_control`           | `native_control`        |     0 |   ✅   | 00:00:09.919 |
| `test_ftp`               | `native_ftp`            |     0 |   ✅   | 00:00:00.847 |
| `test_httpcache`         | `native_httpcache`      |     0 |   ✅   | 00:00:00.844 |
| `test_primitives`        | `native_primitives`     |     0 |   ✅   | 00:00:00.728 |
| `test_ip`                | `native_ip`             |     0 |   ✅   | 00:00:00.836 |
| `test_arena`             | `native_arena`          |     0 |   ✅   | 00:00:00.837 |
| `test_ssh_ed25519`       | `native_ssh_ed25519`    |     0 |   ✅   | 00:00:02.103 |
| `test_ssh_inflate`       | `native_ssh_inflate`    |     0 |   ✅   | 00:00:00.853 |
| `test_promisc`           | `native_promisc`        |     0 |   ✅   | 00:00:00.823 |
| `test_bus_capture`       | `native_bus_capture`    |     0 |   ✅   | 00:00:00.827 |
| `test_j1939`             | `native_j1939`          |     0 |   ✅   | 00:00:00.801 |
| `test_devicenet`         | `native_devicenet`      |     0 |   ✅   | 00:00:00.788 |
| `test_mbus`              | `native_mbus`           |     0 |   ✅   | 00:00:00.783 |
| `test_iec60870`          | `native_iec60870`       |     0 |   ✅   | 00:00:00.839 |
| `test_sdi12`             | `native_sdi12`          |     0 |   ✅   | 00:00:00.797 |
| `test_dmx`               | `native_dmx`            |     0 |   ✅   | 00:00:00.787 |
| `test_nmea0183`          | `native_nmea0183`       |     0 |   ✅   | 00:00:00.807 |
| `test_ptp`               | `native_ptp`            |     0 |   ✅   | 00:00:00.800 |
| `test_iolink`            | `native_iolink`         |     0 |   ✅   | 00:00:00.785 |
| `test_base64`            | `native_base64_scalar`  |     0 |   ✅   | 00:00:00.786 |
| `test_ssh_sftp`          | `native_ssh_sftp`       |     0 |   ✅   | 00:00:00.945 |
| `test_stomp`             | `native_stomp`          |     0 |   ✅   | 00:00:00.772 |
| `test_mqtt_sn`           | `native_mqtt_sn`        |     0 |   ✅   | 00:00:00.796 |
| `test_flow_export`       | `native_flow_export`    |     0 |   ✅   | 00:00:00.774 |
| `test_protobuf`          | `native_protobuf`       |     0 |   ✅   | 00:00:00.793 |
| `test_ad9238`            | `native_ad9238`         |     0 |   ✅   | 00:00:00.775 |
| `test_enocean`           | `native_enocean`        |     0 |   ✅   | 00:00:00.783 |
| `test_pn532`             | `native_pn532`          |     0 |   ✅   | 00:00:00.777 |
| `test_sigfox`            | `native_sigfox`         |     0 |   ✅   | 00:00:00.783 |
| `test_zwave`             | `native_zwave`          |     0 |   ✅   | 00:00:00.787 |
| `test_zigbee`            | `native_zigbee`         |     0 |   ✅   | 00:00:00.794 |
| `test_udp_transport`     | `native_udp_transport`  |     0 |   ✅   | 00:00:00.797 |
| `test_sunspec`           | `native_sunspec`        |     0 |   ✅   | 00:00:00.787 |
| `test_c37118`            | `native_c37118`         |     0 |   ✅   | 00:00:00.772 |
| `test_dnp3`              | `native_dnp3`           |     0 |   ✅   | 00:00:00.794 |
| `test_grpcweb`           | `native_grpcweb`        |     0 |   ✅   | 00:00:00.789 |
| `test_lwm2m_tlv`         | `native_lwm2m_tlv`      |     0 |   ✅   | 00:00:00.781 |
| `test_hostlink`          | `native_hostlink`       |     0 |   ✅   | 00:00:00.799 |
| `test_vxi11`             | `native_vxi11`          |     0 |   ✅   | 00:00:00.809 |
| `test_haas_mdc`          | `native_haas_mdc`       |     0 |   ✅   | 00:00:00.783 |
| `test_lsv2`              | `native_lsv2`           |     0 |   ✅   | 00:00:00.775 |
| `test_df1`               | `native_df1`            |     0 |   ✅   | 00:00:00.780 |
| `test_cotp`              | `native_cotp`           |     0 |   ✅   | 00:00:00.794 |
| `test_melsec`            | `native_melsec`         |     0 |   ✅   | 00:00:00.779 |
| `test_fanuc_j519`        | `native_fanuc_j519`     |     0 |   ✅   | 00:00:00.798 |
| `test_pqc_mlkem`         | `native_pqc`            |     0 |   ✅   | 00:00:00.900 |
| `test_pqc_sntrup761`     | `native_pqc`            |     0 |   ✅   | 00:00:01.724 |
| `test_rtcm3`             | `native_rtcm3`          |     0 |   ✅   | 00:00:00.786 |
| `test_ntrip_caster`      | `native_ntrip_caster`   |     0 |   ✅   | 00:00:00.799 |
| `test_bacnet`            | `native_bacnet`         |     0 |   ✅   | 00:00:00.780 |
| `test_enip`              | `native_enip`           |     0 |   ✅   | 00:00:00.788 |
| `test_amqp`              | `native_amqp`           |     0 |   ✅   | 00:00:00.788 |
| `test_cip`               | `native_cip`            |     0 |   ✅   | 00:00:00.790 |
| `test_nats`              | `native_nats`           |     0 |   ✅   | 00:00:00.776 |
| `test_proxy_protocol`    | `native_proxy_protocol` |     0 |   ✅   | 00:00:00.793 |
| `test_sparkplug`         | `native_sparkplug`      |     0 |   ✅   | 00:00:00.818 |
| `test_totp`              | `native_totp`           |     0 |   ✅   | 00:00:00.811 |
| `test_webhook`           | `native_webhook`        |     0 |   ✅   | 00:00:00.798 |
| `test_dns_resolver`      | `native_dns_resolver`   |     0 |   ✅   | 00:00:00.779 |
| `test_robotics`          | `native_robotics`       |     0 |   ✅   | 00:00:00.830 |
| `test_ntp_server`        | `native_ntp_server`     |     0 |   ✅   | 00:00:00.767 |
| `test_dns_server`        | `native_dns_server`     |    13 |   ✅   | 00:00:00.793 |
| `test_hmmd`              | `native_hmmd`           |     0 |   ✅   | 00:00:00.788 |
| `test_rcwl0516`          | `native_rcwl0516`       |     0 |   ✅   | 00:00:00.783 |
| `test_sen0192`           | `native_sen0192`        |     0 |   ✅   | 00:00:00.768 |
| `test_h2_frame`          | `native_h2frame`        |     0 |   ✅   | 00:00:00.793 |
| `test_quic_varint`       | `native_quic_varint`    |     0 |   ✅   | 00:00:00.782 |
| `test_h3_frame`          | `native_h3frame`        |     0 |   ✅   | 00:00:00.797 |
| `test_jwt`               | `native_jwt`            |     0 |   ✅   | 00:00:01.030 |
| `test_device_id`         | `native_device_id`      |     0 |   ✅   | 00:00:00.820 |
| `test_net_egress`        | `native_net_egress`     |     0 |   ✅   | 00:00:00.782 |
| `test_client`            | `native_client`         |     0 |   ✅   | 00:00:00.783 |
| `test_partition_monitor` | `native_partition`      |     0 |   ✅   | 00:00:00.816 |
| `test_udp_telemetry`     | `native_udp_telemetry`  |     0 |   ✅   | 00:00:00.775 |
| `test_sleep_sched`       | `native_sleep_sched`    |     0 |   ✅   | 00:00:00.791 |
| `test_signaling`         | `native_signaling`      |     0 |   ✅   | 00:00:00.797 |
| `test_wearlevel`         | `native_wearlevel`      |     0 |   ✅   | 00:00:00.773 |
| `test_netadapt`          | `native_netadapt`       |     0 |   ✅   | 00:00:00.768 |
| `test_dshot`             | `native_dshot`          |     0 |   ✅   | 00:00:00.784 |
| `test_hart`              | `native_hart`           |     0 |   ✅   | 00:00:00.791 |
| `test_xmpp`              | `native_xmpp`           |     0 |   ✅   | 00:00:00.794 |
| `test_rawl2`             | `native_rawl2`          |     0 |   ✅   | 00:00:00.767 |
| `test_goose`             | `native_goose`          |     0 |   ✅   | 00:00:00.785 |
| `test_mtconnect`         | `native_mtconnect`      |     0 |   ✅   | 00:00:00.770 |
| `test_nema_ts2`          | `native_nema_ts2`       |     0 |   ✅   | 00:00:00.806 |
| `test_snp`               | `native_snp`            |     0 |   ✅   | 00:00:00.799 |
| `test_directnet`         | `native_directnet`      |     0 |   ✅   | 00:00:00.834 |
| `test_sep2`              | `native_sep2`           |     0 |   ✅   | 00:00:00.825 |
| `test_ntcip`             | `native_ntcip`          |     0 |   ✅   | 00:00:00.845 |
| `test_mms`               | `native_mms`            |     0 |   ✅   | 00:00:00.779 |
| `test_cclink`            | `native_cclink`         |     0 |   ✅   | 00:00:00.806 |
| `test_powerlink`         | `native_powerlink`      |     0 |   ✅   | 00:00:00.783 |
| `test_sercos`            | `native_sercos`         |     0 |   ✅   | 00:00:00.801 |
| `test_profibus`          | `native_profibus`       |     0 |   ✅   | 00:00:00.821 |
| `test_lonworks`          | `native_lonworks`       |     0 |   ✅   | 00:00:00.790 |
| `test_mbplus`            | `native_mbplus`         |     0 |   ✅   | 00:00:00.837 |
| `test_interbus`          | `native_interbus`       |     0 |   ✅   | 00:00:00.825 |
| `test_iccp`              | `native_iccp`           |     0 |   ✅   | 00:00:00.838 |
| `test_wave`              | `native_wave`           |     0 |   ✅   | 00:00:00.792 |
| `test_utmc`              | `native_utmc`           |     0 |   ✅   | 00:00:00.786 |
| `test_ocit`              | `native_ocit`           |     0 |   ✅   | 00:00:00.784 |
| `test_atc`               | `native_atc`            |     0 |   ✅   | 00:00:00.779 |
| `test_exc_decoder`       | `native_exc_decoder`    |     0 |   ✅   | 00:00:00.797 |
| `test_http_delivery`     | `native_http_delivery`  |     0 |   ✅   | 00:00:00.772 |
| `test_hw_health`         | `native_hw_health`      |     0 |   ✅   | 00:00:00.781 |
| `test_mdns_adaptive`     | `native_mdns_adaptive`  |     0 |   ✅   | 00:00:00.791 |
| `test_sockpool`          | `native_sockpool`       |     0 |   ✅   | 00:00:00.790 |
| `test_psram_pool`        | `native_psram_pool`     |     0 |   ✅   | 00:00:00.787 |
| `test_wifi_sniffer`      | `native_wifi_sniffer`   |     0 |   ✅   | 00:00:00.791 |
| `test_radio_sniff`       | `native_radio_sniff`    |     0 |   ✅   | 00:00:00.772 |
| `test_ble_gatt`          | `native_ble_gatt`       |     0 |   ✅   | 00:00:00.792 |
| `test_tls_policy`        | `native_tls_policy`     |     0 |   ✅   | 00:00:00.766 |
| `test_wisun`             | `native_wisun`          |     0 |   ✅   | 00:00:00.820 |
| `test_power_mgmt`        | `native_power_mgmt`     |     0 |   ✅   | 00:00:00.798 |
| `test_quic_packet`       | `native_quic_packet`    |     0 |   ✅   | 00:00:00.794 |
| `test_quic_frame`        | `native_quic_frame`     |     0 |   ✅   | 00:00:00.802 |
| `test_dtls_handshake`    | `native_dtls_hs`        |     0 |   ✅   | 00:00:00.998 |
| `test_dtls_tls13`        | `native_dtls_tls13`     |     0 |   ✅   | 00:00:00.971 |
| `test_quic_tp`           | `native_quic_tp`        |     0 |   ✅   | 00:00:00.798 |
| `test_tls13_msg`         | `native_tls13_msg`      |     0 |   ✅   | 00:00:00.969 |
| `test_quic_tls`          | `native_quic_tls`       |     0 |   ✅   | 00:00:01.479 |
| `test_quic_conn`         | `native_quic_conn`      |     0 |   ✅   | 00:00:01.935 |
| `test_h3_conn`           | `native_h3_conn`        |     0 |   ✅   | 00:00:01.577 |
| `test_quic_server`       | `native_quic_server`    |     0 |   ✅   | 00:00:01.633 |
| `test_ssh_chachapoly`    | `native_ssh_chachapoly` |     0 |   ✅   | 00:00:00.983 |
| `test_frame`             | `native_frame`          |     0 |   ✅   | 00:00:00.786 |
| `test_span`              | `native_span`           |     0 |   ✅   | 00:00:00.791 |
| `test_mnt`               | `native_mnt`            |     0 |   ✅   | 00:00:00.919 |
| `test_logbuf`            | `native_logbuf`         |     0 |   ✅   | 00:00:01.385 |
| `test_syslog`            | `native_syslog`         |     0 |   ✅   | 00:00:00.850 |
| `test_clock`             | `native_clock`          |     0 |   ✅   | 00:00:03.860 |
| `test_forward`           | `native_forward`        |     0 |   ✅   | 00:00:10.158 |
| `test_json`              | `native_json`           |     0 |   ✅   | 00:00:02.529 |
| `test_edge_cache`        | `native_edge_cache`     |     0 |   ✅   | 00:00:03.641 |
| `test_edge_fetch`        | `native_edge_cache`     |     0 |   ✅   | 00:00:00.592 |
| `test_http_client`       | `native_http_client`    |     0 |   ✅   | 00:00:00.835 |
| `test_sse`               | `native_sse`            |     0 |   ✅   | 00:00:03.778 |
| `test_telnet`            | `native_telnet`         |     0 |   ✅   | 00:00:01.115 |
| `test_hpack`             | `native_hpack`          |     0 |   ✅   | 00:00:10.587 |
| `test_transport`         | `native_transport`      |     0 |   ✅   | 00:00:09.465 |
| `test_provisioning`      | `native_prov`           |     0 |   ✅   | 00:00:01.420 |
| `test_trace_capture`     | `native_trace_capture`  |     0 |   ✅   | 00:00:10.255 |
| `test_ads`               | `native_ads`            |     0 |   ✅   | 00:00:10.300 |
| `test_canopen`           | `native_canopen`        |     0 |   ✅   | 00:00:01.085 |
| `test_cia402`            | `native_cia402`         |     0 |   ✅   | 00:00:01.086 |
| `test_cloudevents`       | `native_cloudevents`    |     0 |   ✅   | 00:00:01.182 |
| `test_docstore`          | `native_docstore`       |     0 |   ✅   | 00:00:01.191 |
| `test_edge_mesh`         | `native_edge_mesh`      |     0 |   ✅   | 00:00:01.294 |
| `test_euromap77`         | `native_euromap77`      |     0 |   ✅   | 00:00:01.109 |
| `test_failsafe`          | `native_failsafe`       |     0 |   ✅   | 00:00:01.085 |
| `test_focas`             | `native_focas`          |     0 |   ✅   | 00:00:01.050 |
| `test_gpio_map`          | `native_gpio_map`       |     0 |   ✅   | 00:00:01.135 |
| `test_upload`            | `native_upload`         |     0 |   ✅   | 00:00:02.404 |
| `test_gpib`              | `native_gpib`           |     0 |   ✅   | 00:00:03.772 |
| `test_hislip`            | `native_hislip`         |     0 |   ✅   | 00:00:01.049 |
| `test_iface_bridge`      | `native_iface_bridge`   |     0 |   ✅   | 00:00:01.064 |
| `test_ipsec_db`          | `native_ipsec_db`       |     0 |   ✅   | 00:00:01.315 |
| `test_log`               | `native_log`            |     0 |   ✅   | 00:00:01.141 |
| `test_plaintext`         | `native_plaintext`      |     0 |   ✅   | 00:00:01.134 |
| `test_plaintext`         | `native_pool_workers`   |     0 |   ✅   | 00:00:00.615 |
| `test_safety_scl`        | `native_safety_scl`     |     0 |   ✅   | 00:00:01.066 |
| `test_h3_e2e`            | `native_h3_e2e`         |     0 |   ✅   | 00:00:05.049 |
| `test_h3_server`         | `native_h3_server`      |     0 |   ✅   | 00:00:02.824 |
| `test_j2735`             | `native_j2735`          |     0 |   ✅   | 00:00:00.898 |
| `test_link_manager`      | `native_link_manager`   |     0 |   ✅   | 00:00:01.039 |
| `test_openadr`           | `native_openadr`        |     0 |   ✅   | 00:00:00.913 |
| `test_quic_crypto`       | `native_quic_crypto`    |     0 |   ✅   | 00:00:01.327 |
| `test_southbound`        | `native_southbound`     |     0 |   ✅   | 00:00:01.039 |
| `test_sqlite`            | `native_sqlite`         |     0 |   ✅   | 00:00:01.027 |
| `test_ssh_server`        | `native_ssh`            |     0 |   ✅   | 00:00:02.356 |
| `test_ssh_transport`     | `native_ssh`            |     0 |   ✅   | 00:00:01.463 |
| `test_ssh_auth`          | `native_ssh`            |     0 |   ✅   | 00:00:01.637 |
| `test_ssh_channel`       | `native_ssh`            |     0 |   ✅   | 00:00:00.713 |
| `test_ssh_crypto`        | `native_ssh`            |     0 |   ✅   | 00:00:02.366 |
| `test_tls13_kdf`         | `native_tls13_kdf`      |     0 |   ✅   | 00:00:01.210 |
| `test_web_terminal`      | `native_web_terminal`   |     0 |   ✅   | 00:00:02.274 |
| `test_coap`              | `native_coap`           |     0 |   ✅   | 00:00:01.704 |
| `test_coap`              | `native_coap_observe`   |     0 |   ✅   | 00:00:01.215 |
| `test_csrf`              | `native_csrf`           |     0 |   ✅   | 00:00:01.367 |
| `test_fins`              | `native_fins`           |     0 |   ✅   | 00:00:01.101 |
| `test_keepalive`         | `native_keepalive`      |     0 |   ✅   | 00:00:02.401 |
| `test_crc`               | `native_primitives`     |     0 |   ✅   | 00:00:01.155 |
| `test_redis_resp`        | `native_redis`          |     0 |   ✅   | 00:00:01.115 |

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
