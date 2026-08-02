# Test Report

**Generated:** 2026-08-02 08:14:55
**Command:** `pio test` over 304 auto-discovered native envs (excludes native_pentest, native_codeql)
**Result:** ❌ 2704 passed, 768 failed - 1823s

---

## Summary

| Suite                  | Environment              | Tests | Status |     Duration |
| :--------------------- | :----------------------- | ----: | :----: | -----------: |
| `test_canopen`         | `native_canopen`         |    27 |   ✅   | 00:00:10.351 |
| `test_cia402`          | `native_cia402`          |    15 |   ✅   | 00:00:00.814 |
| `test_control`         | `native_control`         |    19 |   ✅   | 00:00:00.803 |
| `test_dbm`             | `native_dbm`             |    23 |   ✅   | 00:00:00.855 |
| `test_dnc_stream`      | `native_dnc`             |    14 |   ✅   | 00:00:00.806 |
| `test_dnc`             | `native_dnc`             |    14 |   ✅   | 00:00:00.604 |
| `test_ftp`             | `native_ftp`             |    22 |   ✅   | 00:00:00.796 |
| `test_httpcache`       | `native_httpcache`       |    15 |   ✅   | 00:00:00.781 |
| `test_crc`             | `native_primitives`      |    11 |   ✅   | 00:00:00.780 |
| `test_ip`              | `native_ip`              |    11 |   ✅   | 00:00:00.763 |
| `test_ssh_ed25519`     | `native_ssh_ed25519`     |    19 |   ✅   | 00:00:02.064 |
| `test_ssh_inflate`     | `native_ssh_inflate`     |     6 |   ✅   | 00:00:00.810 |
| `test_promisc`         | `native_promisc`         |    12 |   ✅   | 00:00:00.770 |
| `test_bus_capture`     | `native_bus_capture`     |     9 |   ✅   | 00:00:00.769 |
| `test_j1939`           | `native_j1939`           |    29 |   ✅   | 00:00:00.798 |
| `test_devicenet`       | `native_devicenet`       |    17 |   ✅   | 00:00:00.776 |
| `test_nmea2000`        | `native_nmea2000`        |    28 |   ✅   | 00:00:00.811 |
| `test_mbus`            | `native_mbus`            |    19 |   ✅   | 00:00:00.780 |
| `test_iec60870`        | `native_iec60870`        |    29 |   ✅   | 00:00:00.802 |
| `test_sdi12`           | `native_sdi12`           |    16 |   ✅   | 00:00:00.791 |
| `test_dmx`             | `native_dmx`             |    11 |   ✅   | 00:00:00.773 |
| `test_nmea0183`        | `native_nmea0183`        |    27 |   ✅   | 00:00:00.797 |
| `test_ubx`             | `native_ubx`             |    21 |   ✅   | 00:00:00.785 |
| `test_ptp`             | `native_ptp`             |    12 |   ✅   | 00:00:00.797 |
| `test_roaming`         | `native_roaming`         |    10 |   ✅   | 00:00:00.771 |
| `test_iolink`          | `native_iolink`          |     6 |   ✅   | 00:00:00.785 |
| `test_snmp_ber`        | `native_snmp`            |    27 |   ✅   | 00:00:00.862 |
| `test_snmp_agent`      | `native_snmp`            |    41 |   ✅   | 00:00:00.668 |
| `test_coap`            | `native_coap`            |    58 |   ✅   | 00:00:00.993 |
| `test_coap`            | `native_coap_observe`    |    66 |   ✅   | 00:00:01.043 |
| `test_modbus`          | `native_modbus`          |    30 |   ✅   | 00:00:00.815 |
| `test_redis_resp`      | `native_redis`           |    21 |   ✅   | 00:00:00.792 |
| `test_sqlite`          | `native_sqlite`          |    43 |   ✅   | 00:00:00.858 |
| `test_stomp`           | `native_stomp`           |    17 |   ✅   | 00:00:00.787 |
| `test_mqtt_sn`         | `native_mqtt_sn`         |    17 |   ✅   | 00:00:00.814 |
| `test_flow_export`     | `native_flow_export`     |    10 |   ✅   | 00:00:00.789 |
| `test_protobuf`        | `native_protobuf`        |    19 |   ✅   | 00:00:00.783 |
| `test_preempt_queue`   | `native_preempt_queue`   |    15 |   ✅   | 00:00:00.847 |
| `test_dma`             | `native_dma`             |    12 |   ✅   | 00:00:00.956 |
| `test_trace_capture`   | `native_trace_capture`   |     9 |   ✅   | 00:00:00.856 |
| `test_ad9238`          | `native_ad9238`          |     7 |   ✅   | 00:00:00.780 |
| `test_forward`         | `native_forward`         |    33 |   ✅   | 00:00:01.045 |
| `test_gateway`         | `native_gateway`         |    13 |   ✅   | 00:00:00.944 |
| `test_lora`            | `native_lora`            |    19 |   ✅   | 00:00:00.793 |
| `test_nrf24`           | `native_nrf24`           |    17 |   ✅   | 00:00:00.783 |
| `test_enocean`         | `native_enocean`         |    14 |   ✅   | 00:00:00.785 |
| `test_pn532`           | `native_pn532`           |    14 |   ✅   | 00:00:00.785 |
| `test_sigfox`          | `native_sigfox`          |     9 |   ✅   | 00:00:00.782 |
| `test_zwave`           | `native_zwave`           |    15 |   ✅   | 00:00:00.789 |
| `test_zigbee`          | `native_zigbee`          |    16 |   ✅   | 00:00:00.781 |
| `test_thread`          | `native_thread`          |    38 |   ✅   | 00:00:00.816 |
| `test_udp_transport`   | `native_udp_transport`   |    21 |   ✅   | 00:00:00.781 |
| `test_sunspec`         | `native_sunspec`         |    10 |   ✅   | 00:00:00.809 |
| `test_c37118`          | `native_c37118`          |    12 |   ✅   | 00:00:00.786 |
| `test_dnp3`            | `native_dnp3`            |    20 |   ✅   | 00:00:00.862 |
| `test_grpcweb`         | `native_grpcweb`         |    20 |   ✅   | 00:00:00.799 |
| `test_lwm2m_tlv`       | `native_lwm2m_tlv`       |    18 |   ✅   | 00:00:00.808 |
| `test_fins`            | `native_fins`            |     9 |   ✅   | 00:00:00.799 |
| `test_hostlink`        | `native_hostlink`        |    21 |   ✅   | 00:00:00.774 |
| `test_hislip`          | `native_hislip`          |    15 |   ✅   | 00:00:00.793 |
| `test_vxi11`           | `native_vxi11`           |    24 |   ✅   | 00:00:00.777 |
| `test_packml`          | `native_packml`          |    28 |   ✅   | 00:00:00.795 |
| `test_lsv2`            | `native_lsv2`            |    17 |   ✅   | 00:00:00.809 |
| `test_df1`             | `native_df1`             |    11 |   ✅   | 00:00:00.790 |
| `test_simatic`         | `native_simatic`         |    36 |   ✅   | 00:00:00.916 |
| `test_cotp`            | `native_cotp`            |    14 |   ✅   | 00:00:00.782 |
| `test_s7comm`          | `native_s7comm`          |    14 |   ✅   | 00:00:00.784 |
| `test_melsec`          | `native_melsec`          |    10 |   ✅   | 00:00:00.782 |
| `test_ads`             | `native_ads`             |    17 |   ✅   | 00:00:00.795 |
| `test_focas`           | `native_focas`           |    16 |   ✅   | 00:00:00.809 |
| `test_fanuc_j519`      | `native_fanuc_j519`      |    14 |   ✅   | 00:00:00.830 |
| `test_pqc_mlkem`       | `native_pqc`             |    10 |   ✅   | 00:00:00.988 |
| `test_pqc_sha3`        | `native_pqc`             |     4 |   ✅   | 00:00:00.631 |
| `test_pqc_sntrup761`   | `native_pqc`             |     4 |   ✅   | 00:00:01.761 |
| `test_iface_bridge`    | `native_iface_bridge`    |    11 |   ✅   | 00:00:00.849 |
| `test_rtcm3`           | `native_rtcm3`           |    16 |   ✅   | 00:00:00.823 |
| `test_gnss_survey`     | `native_gnss_survey`     |    25 |   ✅   | 00:00:00.910 |
| `test_bacnet`          | `native_bacnet`          |    17 |   ✅   | 00:00:00.832 |
| `test_enip`            | `native_enip`            |    10 |   ✅   | 00:00:00.814 |
| `test_amqp`            | `native_amqp`            |    15 |   ✅   | 00:00:00.813 |
| `test_cip`             | `native_cip`             |    12 |   ✅   | 00:00:00.817 |
| `test_nats`            | `native_nats`            |    16 |   ✅   | 00:00:00.807 |
| `test_sparkplug`       | `native_sparkplug`       |     9 |   ✅   | 00:00:00.846 |
| `test_modbus_master`   | `native_modbus_master`   |    27 |   ✅   | 00:00:00.855 |
| `test_ota_rollback`    | `native_ota_rollback`    |     6 |   ✅   | 00:00:00.842 |
| `test_totp`            | `native_totp`            |     9 |   ✅   | 00:00:00.844 |
| `test_radio_power`     | `native_radio_power`     |     3 |   ✅   | 00:00:00.814 |
| `test_dns_resolver`    | `native_dns_resolver`    |     6 |   ✅   | 00:00:00.809 |
| `test_espnow`          | `native_espnow`          |    11 |   ✅   | 00:00:00.775 |
| `test_opcua`           | `native_opcua`           |    71 |   ✅   | 00:00:00.999 |
| `test_opcua_client`    | `native_opcua_client`    |    31 |   ✅   | 00:00:00.865 |
| `test_umati`           | `native_umati`           |    17 |   ✅   | 00:00:00.847 |
| `test_robotics`        | `native_robotics`        |    22 |   ✅   | 00:00:00.881 |
| `test_euromap77`       | `native_euromap77`       |    18 |   ✅   | 00:00:00.833 |
| `test_syslog`          | `native_syslog`          |    14 |   ✅   | 00:00:00.821 |
| `test_ntp_server`      | `native_ntp_server`      |     9 |   ✅   | 00:00:00.788 |
| `test_dns_server`      | `native_dns_server`      |    13 |   ✅   | 00:00:00.791 |
| `test_rtc`             | `native_rtc`             |    13 |   ✅   | 00:00:00.788 |
| `test_relay`           | `native_relay`           |    12 |   ✅   | 00:00:00.786 |
| `test_ld2410`          | `native_ld2410`          |    14 |   ✅   | 00:00:00.795 |
| `test_safety_scl`      | `native_safety_scl`      |    16 |   ✅   | 00:00:00.807 |
| `test_hmmd`            | `native_hmmd`            |    13 |   ✅   | 00:00:00.794 |
| `test_rcwl0516`        | `native_rcwl0516`        |    10 |   ✅   | 00:00:00.824 |
| `test_sen0192`         | `native_sen0192`         |     7 |   ✅   | 00:00:00.791 |
| `test_mpr121`          | `native_mpr121`          |     6 |   ✅   | 00:00:00.848 |
| `test_sht3x`           | `native_sht3x`           |     7 |   ✅   | 00:00:00.842 |
| `test_pca9685`         | `native_pca9685`         |     5 |   ✅   | 00:00:00.823 |
| `test_ads1115`         | `native_ads1115`         |     5 |   ✅   | 00:00:00.820 |
| `test_ina219`          | `native_ina219`          |     5 |   ✅   | 00:00:00.821 |
| `test_hpack`           | `native_hpack`           |    19 |   ✅   | 00:00:01.022 |
| `test_h2_frame`        | `native_h2frame`         |     7 |   ✅   | 00:00:00.829 |
| `test_quic_varint`     | `native_quic_varint`     |     3 |   ✅   | 00:00:00.821 |
| `test_h3_frame`        | `native_h3frame`         |    12 |   ✅   | 00:00:00.850 |
| `test_mqtt`            | `native_mqtt`            |    24 |   ✅   | 00:00:00.846 |
| `test_snmp_trap`       | `native_snmp_trap`       |     7 |   ✅   | 00:00:00.827 |
| `test_time_source`     | `native_time_source`     |    11 |   ✅   | 00:00:00.800 |
| `test_config_store`    | `native_config_store`    |    24 |   ✅   | 00:00:00.779 |
| `test_device_id`       | `native_device_id`       |     4 |   ✅   | 00:00:00.812 |
| `test_auth_lockout`    | `native_auth_lockout`    |    14 |   ✅   | 00:00:00.823 |
| `test_forwarded_trust` | `native_forwarded_trust` |    15 |   ✅   | 00:00:00.824 |
| `test_telemetry`       | `native_telemetry`       |    10 |   ✅   | 00:00:00.770 |
| `test_net_egress`      | `native_net_egress`      |     9 |   ✅   | 00:00:00.778 |
| `test_client`          | `native_client`          |     7 |   ✅   | 00:00:00.778 |
| `test_cbor`            | `native_cbor`            |    25 |   ✅   | 00:00:00.799 |
| `test_msgpack`         | `native_msgpack`         |    29 |   ✅   | 00:00:00.850 |
| `test_statsd`          | `native_statsd`          |    15 |   ✅   | 00:00:00.875 |
| `test_failsafe`        | `native_failsafe`        |    11 |   ✅   | 00:00:00.828 |
| `test_sleep_sched`     | `native_sleep_sched`     |    10 |   ✅   | 00:00:00.802 |
| `test_wearlevel`       | `native_wearlevel`       |     5 |   ✅   | 00:00:00.832 |
| `test_netadapt`        | `native_netadapt`        |     6 |   ✅   | 00:00:00.806 |
| `test_dshot`           | `native_dshot`           |     9 |   ✅   | 00:00:00.802 |
| `test_hart`            | `native_hart`            |     8 |   ✅   | 00:00:00.795 |
| `test_nts`             | `native_nts`             |    10 |   ✅   | 00:00:00.813 |
| `test_dds`             | `native_dds`             |    14 |   ✅   | 00:00:00.815 |
| `test_xmpp`            | `native_xmpp`            |    18 |   ✅   | 00:00:00.839 |
| `test_rawl2`           | `native_rawl2`           |     7 |   ✅   | 00:00:00.819 |
| `test_spa_router`      | `native_spa_router`      |    17 |   ✅   | 00:00:00.852 |
| `test_goose`           | `native_goose`           |     9 |   ✅   | 00:00:00.827 |
| `test_mtconnect`       | `native_mtconnect`       |    19 |   ✅   | 00:00:00.817 |
| `test_wal`             | `native_wal`             |     8 |   ✅   | 00:00:00.859 |
| `test_wal_store`       | `native_wal`             |    35 |   ✅   | 00:00:00.670 |
| `test_j2735`           | `native_j2735`           |    12 |   ✅   | 00:00:00.821 |
| `test_nema_ts2`        | `native_nema_ts2`        |     7 |   ✅   | 00:00:00.794 |
| `test_snp`             | `native_snp`             |     6 |   ✅   | 00:00:00.797 |
| `test_directnet`       | `native_directnet`       |     8 |   ✅   | 00:00:00.812 |
| `test_profinet`        | `native_profinet`        |     9 |   ✅   | 00:00:00.832 |
| `test_ntcip`           | `native_ntcip`           |     4 |   ✅   | 00:00:00.815 |
| `test_mms`             | `native_mms`             |    17 |   ✅   | 00:00:00.829 |
| `test_cclink`          | `native_cclink`          |    10 |   ✅   | 00:00:00.781 |
| `test_powerlink`       | `native_powerlink`       |     8 |   ✅   | 00:00:00.796 |
| `test_sercos`          | `native_sercos`          |     6 |   ✅   | 00:00:00.777 |
| `test_profibus`        | `native_profibus`        |    11 |   ✅   | 00:00:00.790 |
| `test_lonworks`        | `native_lonworks`        |     9 |   ✅   | 00:00:00.773 |
| `test_mbplus`          | `native_mbplus`          |     7 |   ✅   | 00:00:00.787 |
| `test_interbus`        | `native_interbus`        |     6 |   ✅   | 00:00:00.803 |
| `test_iccp`            | `native_iccp`            |     6 |   ✅   | 00:00:00.775 |
| `test_wave`            | `native_wave`            |    12 |   ✅   | 00:00:00.782 |
| `test_ocit`            | `native_ocit`            |    12 |   ✅   | 00:00:00.779 |
| `test_southbound`      | `native_southbound`      |    10 |   ✅   | 00:00:00.779 |
| `test_sb_modbus`       | `native_sb_modbus`       |    12 |   ✅   | 00:00:00.919 |
| `test_mdns_adaptive`   | `native_mdns_adaptive`   |    18 |   ✅   | 00:00:00.794 |
| `test_sockpool`        | `native_sockpool`        |    11 |   ✅   | 00:00:00.774 |
| `test_psram_pool`      | `native_psram_pool`      |     7 |   ✅   | 00:00:00.773 |
| `test_happy_eyeballs`  | `native_happy_eyeballs`  |    10 |   ✅   | 00:00:00.808 |
| `test_wifi_sniffer`    | `native_wifi_sniffer`    |    17 |   ✅   | 00:00:00.774 |
| `test_link_manager`    | `native_link_manager`    |     8 |   ✅   | 00:00:00.773 |
| `test_cc1101`          | `native_cc1101`          |    18 |   ✅   | 00:00:00.779 |
| `test_fdc2214`         | `native_fdc2214`         |     5 |   ✅   | 00:00:00.769 |
| `test_ldc1614`         | `native_ldc1614`         |     5 |   ✅   | 00:00:00.774 |
| `test_vl53l0x`         | `native_vl53l0x`         |     3 |   ✅   | 00:00:00.775 |
| `test_radio_sniff`     | `native_radio_sniff`     |     6 |   ✅   | 00:00:00.777 |
| `test_tls_policy`      | `native_tls_policy`      |     5 |   ✅   | 00:00:00.786 |
| `test_clock`           | `native_clock`           |     7 |   ✅   | 00:00:00.783 |
| `test_qpack`           | `native_qpack`           |    12 |   ✅   | 00:00:00.944 |
| `test_quic_packet`     | `native_quic_packet`     |     9 |   ✅   | 00:00:00.777 |
| `test_quic_frame`      | `native_quic_frame`      |    14 |   ✅   | 00:00:00.796 |
| `test_dtls_tls13`      | `native_dtls_tls13`      |    14 |   ✅   | 00:00:00.976 |
| `test_quic_tp`         | `native_quic_tp`         |    13 |   ✅   | 00:00:00.810 |
| `test_tls13_msg`       | `native_tls13_msg`       |    18 |   ✅   | 00:00:01.010 |
| `test_ssh_aesgcm`      | `native_ssh_aesgcm`      |     5 |   ✅   | 00:00:00.957 |
| `test_span`            | `native_span`            |    18 |   ✅   | 00:00:00.768 |

---

## test_canopen - native_canopen - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CANopen (CiA 301) message codec (services/fieldbus/canopen): NMT, SYNC,_

|   # | Test                                               | Status | Description                                                             |
| --: | :------------------------------------------------- | :----: | :---------------------------------------------------------------------- |
|   1 | `test_nmt_start_node`                              |   ✅   | Nmt start node                                                          |
|   2 | `test_sync`                                        |   ✅   | Sync                                                                    |
|   3 | `test_time_roundtrip`                              |   ✅   | 12:34:56.789 -> 45296789 ms after midnight; day 15545 since 1984-01-01. |
|   4 | `test_heartbeat_roundtrip`                         |   ✅   | Heartbeat roundtrip                                                     |
|   5 | `test_emcy_roundtrip`                              |   ✅   | Emcy roundtrip                                                          |
|   6 | `test_pdo_roundtrip`                               |   ✅   | Pdo roundtrip                                                           |
|   7 | `test_sdo_read_request`                            |   ✅   | Sdo read request                                                        |
|   8 | `test_sdo_write_expedited`                         |   ✅   | Sdo write expedited                                                     |
|   9 | `test_sdo_upload_response_expedited`               |   ✅   | Sdo upload response expedited                                           |
|  10 | `test_sdo_abort_roundtrip`                         |   ✅   | Sdo abort roundtrip                                                     |
|  11 | `test_sdo_download_ack`                            |   ✅   | Sdo download ack                                                        |
|  12 | `test_parse_classifies`                            |   ✅   | Parse classifies                                                        |
|  13 | `test_build_arg_validation`                        |   ✅   | Build arg validation                                                    |
|  14 | `test_emcy_build_null_msef`                        |   ✅   | Emcy build null msef                                                    |
|  15 | `test_parse_all_function_codes`                    |   ✅   | Parse all function codes                                                |
|  16 | `test_parse_emcy_rejections`                       |   ✅   | Parse emcy rejections                                                   |
|  17 | `test_parse_heartbeat_rejections`                  |   ✅   | Parse heartbeat rejections                                              |
|  18 | `test_parse_sdo_response_variants`                 |   ✅   | Parse sdo response variants                                             |
|  19 | `test_pdo_zero_length`                             |   ✅   | Pdo zero length                                                         |
|  20 | `test_sdo_write_arg_validation`                    |   ✅   | Sdo write arg validation                                                |
|  21 | `test_emcy_and_sdo_abort_null_out_and_to_server`   |   ✅   | Emcy and sdo abort null out and to server                               |
|  22 | `test_parse_emcy_extended_and_null_outputs`        |   ✅   | Parse emcy extended and null outputs                                    |
|  23 | `test_parse_heartbeat_extended_null_and_node_zero` |   ✅   | Parse heartbeat extended null and node zero                             |
|  24 | `test_parse_sdo_response_extended`                 |   ✅   | Parse sdo response extended                                             |
|  25 | `test_sdo_segmented_download_build`                |   ✅   | Sdo segmented download build                                            |
|  26 | `test_sdo_segmented_upload_roundtrip`              |   ✅   | Sdo segmented upload roundtrip                                          |
|  27 | `test_sdo_segmented_guards`                        |   ✅   | Sdo segmented guards                                                    |

</details>

---

## test_cia402 - native_cia402 - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CiA 402 drive profile (services/fieldbus/cia402): the Statusword state decode, the_

|   # | Test                                  | Status | Description                                                                               |
| --: | :------------------------------------ | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_state_decode`                   |   ✅   | State decode                                                                              |
|   2 | `test_state_decode_ignores_high_bits` |   ✅   | The upper Statusword bits (voltage, remote, target reached, warning, ...) must not change |
|   3 | `test_controlword_commands`           |   ✅   | Controlword commands                                                                      |
|   4 | `test_enable_sequence`                |   ✅   | Enable sequence                                                                           |
|   5 | `test_statusword_flags`               |   ✅   | Statusword flags                                                                          |
|   6 | `test_sdo_set_controlword`            |   ✅   | Sdo set controlword                                                                       |
|   7 | `test_sdo_set_targets`                |   ✅   | Sdo set targets                                                                           |
|   8 | `test_sdo_get_roundtrip`              |   ✅   | Build a read request, then decode a crafted SDO upload response for the Statusword.       |
|   9 | `test_pdo_pack_unpack`                |   ✅   | Pdo pack unpack                                                                           |
|  10 | `test_state_decode_unknown`           |   ✅   | State decode unknown                                                                      |
|  11 | `test_controlword_invalid_command`    |   ✅   | Controlword invalid command                                                               |
|  12 | `test_sdo_set_velocity_torque`        |   ✅   | Sdo set velocity torque                                                                   |
|  13 | `test_sdo_get_i32_roundtrip`          |   ✅   | Sdo get i32 roundtrip                                                                     |
|  14 | `test_sdo_upload_reject_paths`        |   ✅   | (a) parse failure: dlc < 8 makes pc_canopen_parse_sdo_response fail.                      |
|  15 | `test_pdo_null_guards`                |   ✅   | Pdo null guards                                                                           |

</details>

---

## test_control - native_control - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the PID control law (services/system/control): P / I / D terms, output clamping,_

|   # | Test                                                   | Status | Description                                                                                 |
| --: | :----------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_proportional_only`                               |   ✅   | Proportional only                                                                           |
|   2 | `test_integral_accumulates`                            |   ✅   | Integral accumulates                                                                        |
|   3 | `test_feedforward`                                     |   ✅   | Feedforward                                                                                 |
|   4 | `test_output_clamp_and_antiwindup`                     |   ✅   | Output clamp and antiwindup                                                                 |
|   5 | `test_antiwindup_recovers`                             |   ✅   | Once the error reverses, the (un-wound) integrator resumes normally.                        |
|   6 | `test_derivative_on_measurement`                       |   ✅   | Derivative on measurement                                                                   |
|   7 | `test_setpoint_step_no_kick`                           |   ✅   | A setpoint step must NOT produce a derivative kick (D acts on measurement only).            |
|   8 | `test_derivative_filter`                               |   ✅   | Derivative filter                                                                           |
|   9 | `test_reset_and_guards`                                |   ✅   | Reset and guards                                                                            |
|  10 | `test_batched_update`                                  |   ✅   | Batched update                                                                              |
|  11 | `test_fixed_rate_matches`                              |   ✅   | pid_update_fixed(sp, meas) must equal pid_update(sp, meas, dt) once pid_set_rate caches dt. |
|  12 | `test_control_primitives`                              |   ✅   | Control primitives                                                                          |
|  13 | `test_setter_null_guards`                              |   ✅   | Setter null guards                                                                          |
|  14 | `test_integral_limits_take_effect`                     |   ✅   | Integral limits take effect                                                                 |
|  15 | `test_pid_update_n_null_guards`                        |   ✅   | Pid update n null guards                                                                    |
|  16 | `test_pid_log_header_bytes`                            |   ✅   | Pid log header bytes                                                                        |
|  17 | `test_pid_log_record_bytes`                            |   ✅   | Pid log record bytes                                                                        |
|  18 | `test_slew_down_and_fixed_null`                        |   ✅   | Slew down and fixed null                                                                    |
|  19 | `test_antiwindup_integrates_while_saturated_reversing` |   ✅   | Anti-windup itself stops the integral growing once the output rails, so build the integral  |

</details>

---

## test_dbm - native_dbm - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/dbm: a log-structured hash KV over the WAL, exercised on a RAM-backed WalDev._

|   # | Test                                                      | Status | Description                                                                                             |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------------------ |
|   1 | `test_put_get_overwrite`                                  |   ✅   | Put get overwrite                                                                                       |
|   2 | `test_delete_and_contains`                                |   ✅   | Delete and contains                                                                                     |
|   3 | `test_persist_across_reboot_with_checkpoint`              |   ✅   | Persist across reboot with checkpoint                                                                   |
|   4 | `test_persist_across_reboot_without_checkpoint`           |   ✅   | Persist across reboot without checkpoint                                                                |
|   5 | `test_delete_persists_across_reboot`                      |   ✅   | Delete persists across reboot                                                                           |
|   6 | `test_many_keys_and_collisions`                           |   ✅   | Many keys and collisions                                                                                |
|   7 | `test_index_full_fails_closed`                            |   ✅   | Index full fails closed                                                                                 |
|   8 | `test_bounds_and_empty_value`                             |   ✅   | Bounds and empty value                                                                                  |
|   9 | `test_max_value_roundtrip`                                |   ✅   | Max value roundtrip                                                                                     |
|  10 | `test_compact_reclaims_space`                             |   ✅   | Compact reclaims space                                                                                  |
|  11 | `test_compact_dest_too_small_fails_closed`                |   ✅   | Compact dest too small fails closed                                                                     |
|  12 | `test_compact_source_read_failure`                        |   ✅   | If reading a value back from the source log fails mid-compaction, compact must fail closed BEFORE       |
|  13 | `test_compact_checkpoint_failure`                         |   ✅   | If the destination checkpoint (sync) fails after the live keys are copied, compact must fail closed and |
|  14 | `test_replay_skips_malformed_records`                     |   ✅   | Replay must step over anything it cannot trust and keep rebuilding the index from the rest, so one      |
|  15 | `test_reopen_rejects_a_log_with_more_keys_than_slots`     |   ✅   | The index is a fixed array: a log carrying more distinct live keys than it has slots cannot be          |
|  16 | `test_probe_walks_a_saturated_table_for_an_absent_key`    |   ✅   | With no empty slot left to end the probe chain on, a lookup has to walk the whole table and             |
|  17 | `test_insert_reuses_a_tombstone_in_a_saturated_table`     |   ✅   | Once every slot has been used and freed, a new key must land in the earliest reusable tombstone         |
|  18 | `test_hash_collision_slots_are_walked_past`               |   ✅   | The stored 64-bit hash is only a prefilter; key_len + the key bytes are what actually identify a        |
|  19 | `test_put_rejects_an_empty_key`                           |   ✅   | A zero-length key has no identity in the log format (key_len 0 is how a corrupt record reads).          |
|  20 | `test_put_fails_closed_when_the_log_is_full`              |   ✅   | The record is appended before the index is touched, so a full log leaves the index exactly as it        |
|  21 | `test_get_fails_when_the_value_cannot_be_read_back`       |   ✅   | The index says where the value is, but the read still has to succeed; a device error is reported        |
|  22 | `test_iterate_visits_live_keys_and_honours_an_early_stop` |   ✅   | iterate walks only live keys (tombstones are skipped), a null callback just counts them, and a          |
|  23 | `test_compact_carries_empty_values`                       |   ✅   | A key stored with a zero-length value has nothing to read back from the old log; compaction must        |

</details>

---

## test_dnc_stream - native_dnc - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DNC drip-feed engine (services/machine_tool/dnc/dnc_stream): stream a G-code program over a_

|   # | Test                                          | Status | Description                                                                                      |
| --: | :-------------------------------------------- | :----: | :----------------------------------------------------------------------------------------------- |
|   1 | `test_iso_roundtrip`                          |   ✅   | Iso roundtrip                                                                                    |
|   2 | `test_eia_roundtrip`                          |   ✅   | Eia roundtrip                                                                                    |
|   3 | `test_crlf_and_parity`                        |   ✅   | Crlf and parity                                                                                  |
|   4 | `test_xoff_pacing`                            |   ✅   | Xoff pacing                                                                                      |
|   5 | `test_leader_trailer`                         |   ✅   | Leader trailer                                                                                   |
|   6 | `test_empty_program`                          |   ✅   | Empty program                                                                                    |
|   7 | `test_encode_error`                           |   ✅   | Encode error                                                                                     |
|   8 | `test_io_error_and_args`                      |   ✅   | Io error and args                                                                                |
|   9 | `test_null_send_or_recv_rejected`             |   ✅   | Both halves of the seam are required: there is no "send-only" drip feed (the engine must be able |
|  10 | `test_reverse_channel_error_fails_the_stream` |   ✅   | A recv error is not "no bytes available": the engine cannot know the controller's flow state any |
|  11 | `test_xoff_never_released_gives_up`           |   ✅   | A controller that asserts XOFF and never releases it must not hang the feed forever.             |
|  12 | `test_reverse_channel_error_while_paused`     |   ✅   | The reverse channel breaking mid-pause is an error, not an implicit XON.                         |
|  13 | `test_send_failure_at_each_stage`             |   ✅   | Stage 1 of a leadered stream is the leader runout itself.                                        |
|  14 | `test_blank_lines_and_crlf_source`            |   ✅   | A CRLF source has its CR stripped, and a blank line produces an empty block (no decoded line).   |

</details>

---

## test_dnc - native_dnc - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CNC RS-232 DNC drip-feed codec (services/machine_tool/dnc): the EIA RS-244_

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_eia_table_odd_parity_and_inverse` |   ✅   | Eia table odd parity and inverse |
|   2 | `test_eia_known_vectors`                |   ✅   | Eia known vectors                |
|   3 | `test_iso_even_parity`                  |   ✅   | Iso even parity                  |
|   4 | `test_encode_block_iso`                 |   ✅   | Encode block iso                 |
|   5 | `test_encode_block_eia`                 |   ✅   | Encode block eia                 |
|   6 | `test_encode_block_fail_closed`         |   ✅   | Encode block fail closed         |
|   7 | `test_encode_marker`                    |   ✅   | Encode marker                    |
|   8 | `test_encode_leader`                    |   ✅   | Encode leader                    |
|   9 | `test_flow_control`                     |   ✅   | Flow control                     |
|  10 | `test_roundtrip_program`                |   ✅   | Roundtrip program                |
|  11 | `test_decode_overflow_and_recovery`     |   ✅   | Decode overflow and recovery     |
|  12 | `test_decode_ignores_runout`            |   ✅   | Decode ignores runout            |
|  13 | `test_decode_eia_three_is_not_xoff`     |   ✅   | Decode eia three is not xoff     |
|  14 | `test_encode_overflow_paths`            |   ✅   | Encode overflow paths            |

</details>

---

## test_ftp - native_ftp - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the FTP client wire codec (services/file_transfer/ftp): command builders, the_

|   # | Test                                                | Status | Description                                                                     |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_build_command`                                |   ✅   | Build command                                                                   |
|   2 | `test_build_command_fail_closed`                    |   ✅   | Build command fail closed                                                       |
|   3 | `test_build_port_and_eprt`                          |   ✅   | Build port and eprt                                                             |
|   4 | `test_reply_single_line`                            |   ✅   | Reply single line                                                               |
|   5 | `test_reply_multiline_greeting`                     |   ✅   | real test.rebex.net greeting: continuation lines do NOT repeat the code         |
|   6 | `test_reply_multiline_feat`                         |   ✅   | real FEAT reply: many indented continuation lines, terminated by "211 End."     |
|   7 | `test_reply_incomplete_and_malformed`               |   ✅   | single line without its CRLF yet -> incomplete                                  |
|   8 | `test_reply_pipelined_advance`                      |   ✅   | two replies back-to-back; parse the first, advance by `used`, parse the second. |
|   9 | `test_reply_multiline_not_terminated_by_other_code` |   ✅   | Reply multiline not terminated by other code                                    |
|  10 | `test_parse_pasv`                                   |   ✅   | Parse pasv                                                                      |
|  11 | `test_parse_pasv_malformed`                         |   ✅   | Parse pasv malformed                                                            |
|  12 | `test_parse_epsv`                                   |   ✅   | Parse epsv                                                                      |
|  13 | `test_parse_epsv_malformed`                         |   ✅   | Parse epsv malformed                                                            |
|  14 | `test_reply_null_and_partial_multiline`             |   ✅   | Reply null and partial multiline                                                |
|  15 | `test_build_overflow_and_null`                      |   ✅   | Build overflow and null                                                         |
|  16 | `test_pasv_epsv_null_and_edges`                     |   ✅   | Pasv epsv null and edges                                                        |
|  17 | `test_build_null_args`                              |   ✅   | Build null args                                                                 |
|  18 | `test_reply_head_nondigit_edges`                    |   ✅   | Reply head nondigit edges                                                       |
|  19 | `test_reply_multiline_samecode_dash`                |   ✅   | Reply multiline samecode dash                                                   |
|  20 | `test_parse_pasv_edges`                             |   ✅   | Parse pasv edges                                                                |
|  21 | `test_parse_epsv_edges`                             |   ✅   | Parse epsv edges                                                                |
|  22 | `test_reply_class_out_of_range`                     |   ✅   | Reply class out of range                                                        |

</details>

---

## test_httpcache - native_httpcache - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the HTTP Cache-Control helpers (services/web/httpcache): the directive_

|   # | Test                                                 | Status | Description                                                                           |
| --: | :--------------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_preset_immutable`                              |   ✅   | Preset immutable                                                                      |
|   2 | `test_preset_no_store_and_shared_and_revalidatable`  |   ✅   | Preset no store and shared and revalidatable                                          |
|   3 | `test_build_manual_and_edges`                        |   ✅   | Build manual and edges                                                                |
|   4 | `test_parse_response_directives`                     |   ✅   | Parse response directives                                                             |
|   5 | `test_parse_case_insensitive_and_quoted_and_unknown` |   ✅   | case-insensitive names, a quoted delta, extra OWS, and an unknown directive to ignore |
|   6 | `test_parse_request_directives`                      |   ✅   | Parse request directives                                                              |
|   7 | `test_build_parse_roundtrip`                         |   ✅   | Build parse roundtrip                                                                 |
|   8 | `test_freshness_precedence`                          |   ✅   | Freshness precedence                                                                  |
|   9 | `test_build_all_directives`                          |   ✅   | Build all directives                                                                  |
|  10 | `test_parse_all_directives`                          |   ✅   | Parse all directives                                                                  |
|  11 | `test_parse_and_build_guards`                        |   ✅   | Parse and build guards                                                                |
|  12 | `test_preset_clamps`                                 |   ✅   | Preset clamps                                                                         |
|  13 | `test_build_boundaries`                              |   ✅   | Build boundaries                                                                      |
|  14 | `test_parse_ci_length_edges`                         |   ✅   | Parse ci length edges                                                                 |
|  15 | `test_parse_ows_and_empty`                           |   ✅   | Parse ows and empty                                                                   |

</details>

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

## test_ip - native_ip - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the pc_ip address core (network_drivers/network/pc_ip): RFC 4291 text_

|   # | Test                                          | Status | Description                                                                 |
| --: | :-------------------------------------------- | :----: | :-------------------------------------------------------------------------- |
|   1 | `test_v4_round_trip`                          |   ✅   | V4 round trip                                                               |
|   2 | `test_from_v6_bytes`                          |   ✅   | 2001:db8::1 as raw network-order bytes -> pc_ip -> canonical text.          |
|   3 | `test_is_unspecified`                         |   ✅   | Is unspecified                                                              |
|   4 | `test_prefix_match`                           |   ✅   | IPv4 CIDR containment (the allowlist primitive - full address, no hashing). |
|   5 | `test_v6_canonical_5952`                      |   ✅   | RFC 5952: lower-case, no leading zeros, longest zero run -> "::".           |
|   6 | `test_v4_mapped`                              |   ✅   | V4 mapped                                                                   |
|   7 | `test_classify_v4`                            |   ✅   | Classify v4                                                                 |
|   8 | `test_classify_v6`                            |   ✅   | Classify v6                                                                 |
|   9 | `test_reject_malformed`                       |   ✅   | Reject malformed                                                            |
|  10 | `test_equal_and_from_v4`                      |   ✅   | Equal and from v4                                                           |
|  11 | `test_ip_classify_equal_cidr_and_parse_edges` |   ✅   | classify: null and a pc_ip_family::PC_IP_NONE address are UNSPECIFIED.      |

</details>

---

## test_ssh_ed25519 - native_ssh_ed25519 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Known-answer tests for the modern SSH crypto suite (curve25519-sha256 KEX +_

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_sha512_empty`                                |   ✅   | Sha512 empty                                |
|   2 | `test_sha512_abc`                                  |   ✅   | Sha512 abc                                  |
|   3 | `test_sha512_one_block_boundary`                   |   ✅   | Sha512 one block boundary                   |
|   4 | `test_sha512_two_block_boundary`                   |   ✅   | Sha512 two block boundary                   |
|   5 | `test_sha512_million_a_streaming`                  |   ✅   | Sha512 million a streaming                  |
|   6 | `test_sha512_streaming_matches_oneshot`            |   ✅   | Sha512 streaming matches oneshot            |
|   7 | `test_x25519_rfc7748_vector1`                      |   ✅   | X25519 rfc7748 vector1                      |
|   8 | `test_x25519_rfc7748_vector2`                      |   ✅   | X25519 rfc7748 vector2                      |
|   9 | `test_x25519_iterated_1`                           |   ✅   | X25519 iterated 1                           |
|  10 | `test_x25519_iterated_1000`                        |   ✅   | X25519 iterated 1000                        |
|  11 | `test_x25519_dh_agreement`                         |   ✅   | X25519 dh agreement                         |
|  12 | `test_ed25519_vector_empty_msg`                    |   ✅   | Ed25519 vector empty msg                    |
|  13 | `test_ed25519_vector_rfc8032_test2`                |   ✅   | Ed25519 vector rfc8032 test2                |
|  14 | `test_ed25519_vector_zero_seed`                    |   ✅   | Ed25519 vector zero seed                    |
|  15 | `test_ed25519_verify_rejects_tampering`            |   ✅   | Ed25519 verify rejects tampering            |
|  16 | `test_ed25519_verify_rejects_noncanonical_s`       |   ✅   | Ed25519 verify rejects noncanonical s       |
|  17 | `test_ed25519_verify_rejects_invalid_pubkey_point` |   ✅   | Ed25519 verify rejects invalid pubkey point |
|  18 | `test_ed25519_roundtrip_long`                      |   ✅   | Ed25519 roundtrip long                      |
|  19 | `test_gf_mul_s16_model_matches_scalar`             |   ✅   | Gf mul s16 model matches scalar             |

</details>

---

## test_ssh_inflate - native_ssh_inflate - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Tests the SSH client-to-server resumable INFLATE (ssh_inflate) against golden vectors produced by_

|   # | Test                                | Status | Description                  |
| --: | :---------------------------------- | :----: | :--------------------------- |
|   1 | `test_decode_partial_flush_stream`  |   ✅   | Decode partial flush stream  |
|   2 | `test_reinit_resets_stream`         |   ✅   | Reinit resets stream         |
|   3 | `test_rejects_bad_header`           |   ✅   | Rejects bad header           |
|   4 | `test_rejects_bad_block_type`       |   ✅   | Rejects bad block type       |
|   5 | `test_output_overflow_fails_closed` |   ✅   | Output overflow fails closed |
|   6 | `test_header_split_across_calls`    |   ✅   | Header split across calls    |

</details>

---

## test_promisc - native_promisc - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Wi-Fi promiscuous capture helpers (services/radio/promisc): the pure 802.11 MAC_

|   # | Test                              | Status | Description                                                                                |
| --: | :-------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_beacon_mgmt`                |   ✅   | Mgmt (type 0), Beacon (subtype 8): fc0 = (8<<4)                                            | (0<<2) = 0x80; no DS bits.                      |
|   2 | `test_data_from_ds`               |   ✅   | Data (type 2), from the AP: fc0 = (0<<4)                                                   | (2<<2) = 0x08; from_ds = 0x02.                  |
|   3 | `test_data_to_ds`                 |   ✅   | Data to the AP: to_ds = 0x01. a1 = BSSID, a2 = SRC, a3 = DST.                              |
|   4 | `test_qos_data_header_len`        |   ✅   | QoS Data subtype 8: fc0 = (8<<4)                                                           | (2<<2) = 0x88. Adds a 2-byte QoS Control field. |
|   5 | `test_wds_four_address`           |   ✅   | WDS: to_ds & from_ds set (fc1 = 0x03). Addr4 at offset 24; DST = a3, SRC = a4.             |
|   6 | `test_control_frame`              |   ✅   | ACK (type 1, subtype 13): fc0 = (13<<4)                                                    | (1<<2) = 0xD4. Only Addr1 (RA), 10-byte header. |
|   7 | `test_reject_short`               |   ✅   | Reject short                                                                               |
|   8 | `test_null_out_pointer`           |   ✅   | Null out pointer                                                                           |
|   9 | `test_qos_order_bit_ht_control`   |   ✅   | QoS Data subtype 8 with the Order bit set: fc0 = 0x88, fc1 = 0x80. hlen = 24 + 2 (QoS) + 4 |
|  10 | `test_qos_len_less_than_hlen`     |   ✅   | QoS Data subtype 8, no DS/order bits: fc0 = 0x88, fc1 = 0x00. Computed hlen = 24 + 2 = 26, |
|  11 | `test_pcap_headers`               |   ✅   | Pcap headers                                                                               |
|  12 | `test_host_stubs_and_short_frame` |   ✅   | Host stubs and short frame                                                                 |

</details>

---

## test_bus_capture - native_bus_capture - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CAN listen-only capture framing (services/system/bus_capture): can_to_socketcan()_

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

## test_j1939 - native_j1939 - ✅ 29 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the SAE J1939 codec (services/fieldbus/j1939): 29-bit id encode/decode (PDU1 + PDU2),_

|   # | Test                                      | Status | Description                                                                                                  |
| --: | :---------------------------------------- | :----: | :----------------------------------------------------------------------------------------------------------- |
|   1 | `test_id_pdu2_roundtrip`                  |   ✅   | Id pdu2 roundtrip                                                                                            |
|   2 | `test_id_pdu1_roundtrip`                  |   ✅   | Id pdu1 roundtrip                                                                                            |
|   3 | `test_encode_rejects_bad_args`            |   ✅   | Encode rejects bad args                                                                                      |
|   4 | `test_build_single_frame`                 |   ✅   | Build single frame                                                                                           |
|   5 | `test_request_pgn`                        |   ✅   | Request pgn                                                                                                  |
|   6 | `test_address_claim_name`                 |   ✅   | Address claim name                                                                                           |
|   7 | `test_tp_num_packets`                     |   ✅   | Tp num packets                                                                                               |
|   8 | `test_tp_bam_roundtrip`                   |   ✅   | Tp bam roundtrip                                                                                             |
|   9 | `test_tp_out_of_sequence_errors`          |   ✅   | Tp out of sequence errors                                                                                    |
|  10 | `test_build_error_paths`                  |   ✅   | Build error paths                                                                                            |
|  11 | `test_tp_feed_error_paths`                |   ✅   | Tp feed error paths                                                                                          |
|  12 | `test_null_guard_paths`                   |   ✅   | Null guard paths                                                                                             |
|  13 | `test_build_message_length_edges`         |   ✅   | Build message length edges                                                                                   |
|  14 | `test_build_name_not_arbitrary_capable`   |   ✅   | Build name not arbitrary capable                                                                             |
|  15 | `test_build_bam_cm_too_large`             |   ✅   | Build bam cm too large                                                                                       |
|  16 | `test_tp_feed_short_cm_frame_ignored`     |   ✅   | Tp feed short cm frame ignored                                                                               |
|  17 | `test_tp_feed_rts_starts_session`         |   ✅   | Tp feed rts starts session                                                                                   |
|  18 | `test_tp_feed_cm_total_size_out_of_range` |   ✅   | Tp feed cm total size out of range                                                                           |
|  19 | `test_tp_feed_dt_short_frame_ignored`     |   ✅   | Tp feed dt short frame ignored                                                                               |
|  20 | `test_tp_feed_dt_wrong_source_ignored`    |   ✅   | Tp feed dt wrong source ignored                                                                              |
|  21 | `test_decode_eec1`                        |   ✅   | 1500 rpm (raw 12000 = 0x2EE0), driver's demand +100 % (raw 225), actual +80 % (raw 205), mode 3.             |
|  22 | `test_decode_et1`                         |   ✅   | coolant 90 C (raw 130), fuel 60 C (raw 100), oil 100 C (raw (100+273)/0.03125 = 11936 = 0x2EA0).             |
|  23 | `test_decode_lfe`                         |   ✅   | fuel rate 20.0 L/h (raw 400), instant econ 5.0 km/L (raw 2560), avg 4.5 (raw 2304), throttle 40 % (raw 100). |
|  24 | `test_decode_amb`                         |   ✅   | baro 101.5 kPa (raw 203), cab 21.5 C (raw 9424), ambient 15.0 C (raw 9216),                                  |
|  25 | `test_decode_ic1`                         |   ✅   | trap inlet 25 kPa (raw 50), boost 200 kPa (raw 100), intake 60 C (raw 100), air inlet 100 kPa (raw 50),      |
|  26 | `test_decode_vd`                          |   ✅   | trip 1000.000 km (raw 8000 = 0x1F40), total 250000.000 km (raw 2,000,000 = 0x1E8480).                        |
|  27 | `test_decode_ccvs`                        |   ✅   | Wheel-based vehicle speed 65.5 km/h (raw 65.5*256 = 16768 = 0x4180, LE bytes 80 41); cruise-active = 1       |
|  28 | `test_decode_dm1`                         |   ✅   | Single-frame DM1: amber warning on, one DTC (SPN 100 oil pressure, FMI 1, OC 5), 0xFF padding.               |
|  29 | `test_decode_pgn_mismatch_and_guards`     |   ✅   | Decode pgn mismatch and guards                                                                               |

</details>

---

## test_devicenet - native_devicenet - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DeviceNet link-adaptation codec (services/fieldbus/devicenet): the 4-group 11-bit_

|   # | Test                                   | Status | Description                                                          |
| --: | :------------------------------------- | :----: | :------------------------------------------------------------------- |
|   1 | `test_id_group1`                       |   ✅   | Id group1                                                            |
|   2 | `test_id_group2`                       |   ✅   | Group 2: 10 MAC(6) MsgID(3). mac 0x21, unconnected explicit request. |
|   3 | `test_id_group3_and_4`                 |   ✅   | Id group3 and 4                                                      |
|   4 | `test_header_and_frag_octets`          |   ✅   | Header and frag octets                                               |
|   5 | `test_build_explicit_single_frame`     |   ✅   | Build explicit single frame                                          |
|   6 | `test_frag_non_fragmented`             |   ✅   | header octet with FRAG clear -> the body is complete in one frame.   |
|   7 | `test_frag_reassembly_roundtrip`       |   ✅   | Frag reassembly roundtrip                                            |
|   8 | `test_build_fragment_roundtrip`        |   ✅   | Build fragment roundtrip                                             |
|   9 | `test_frag_out_of_order_errors`        |   ✅   | Frag out of order errors                                             |
|  10 | `test_id_error_paths`                  |   ✅   | Id error paths                                                       |
|  11 | `test_frag_reject_paths`               |   ✅   | Frag reject paths                                                    |
|  12 | `test_frag_overflow`                   |   ✅   | Frag overflow                                                        |
|  13 | `test_null_arguments`                  |   ✅   | encode_id with a null destination fails closed and writes nothing.   |
|  14 | `test_build_explicit_body_arguments`   |   ✅   | body_len 0 with a null body: valid, just the header octet.           |
|  15 | `test_frag_non_fragmented_header_only` |   ✅   | Frag non fragmented header only                                      |
|  16 | `test_frag_empty_data_fragments`       |   ✅   | Frag empty data fragments                                            |
|  17 | `test_frag_sequence_rejects`           |   ✅   | Frag sequence rejects                                                |

</details>

---

## test_nmea2000 - native_nmea2000 - ✅ 28 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the NMEA 2000 codec (services/timing_position/nmea2000): single-frame messages (J1939-based)_

|   # | Test                                                           | Status | Description                                                                                                       |
| --: | :------------------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------------------------------- |
|   1 | `test_num_frames`                                              |   ✅   | Num frames                                                                                                        |
|   2 | `test_single_frame`                                            |   ✅   | Single frame                                                                                                      |
|   3 | `test_fastpacket_roundtrip`                                    |   ✅   | Fastpacket roundtrip                                                                                              |
|   4 | `test_fastpacket_single_frame_completes`                       |   ✅   | Fastpacket single frame completes                                                                                 |
|   5 | `test_fastpacket_interleaved_sequence_ignored`                 |   ✅   | Fastpacket interleaved sequence ignored                                                                           |
|   6 | `test_fastpacket_out_of_order_errors`                          |   ✅   | Fastpacket out of order errors                                                                                    |
|   7 | `test_nmea2000_error_paths`                                    |   ✅   | Nmea2000 error paths                                                                                              |
|   8 | `test_fastpacket_build_frame_total_too_large`                  |   ✅   | Fastpacket build frame total too large                                                                            |
|   9 | `test_fastpacket_reset_null_is_safe`                           |   ✅   | Fastpacket reset null is safe                                                                                     |
|  10 | `test_fastpacket_feed_total_too_large_errors`                  |   ✅   | Fastpacket feed total too large errors                                                                            |
|  11 | `test_fastpacket_continuation_without_active_sequence_ignored` |   ✅   | Fastpacket continuation without active sequence ignored                                                           |
|  12 | `test_fastpacket_continuation_wrong_source_ignored`            |   ✅   | Fastpacket continuation wrong source ignored                                                                      |
|  13 | `test_fastpacket_continuation_wrong_pgn_ignored`               |   ✅   | Fastpacket continuation wrong pgn ignored                                                                         |
|  14 | `test_fastpacket_roundtrip_short_last_frame`                   |   ✅   | Fastpacket roundtrip short last frame                                                                             |
|  15 | `test_decode_position_rapid`                                   |   ✅   | lat 37.3749, lon -122.0841 (1e-7 deg/bit), little-endian.                                                         |
|  16 | `test_decode_cog_sog_rapid`                                    |   ✅   | sid 0x11, ref magnetic, COG 1.5708 rad (raw 15708), SOG 6.17 m/s (raw 617).                                       |
|  17 | `test_decode_engine_rapid`                                     |   ✅   | instance 0, speed 2400 rpm (raw 9600), boost 150000 Pa (raw 1500), tilt +15 %.                                    |
|  18 | `test_decode_engine_dynamic`                                   |   ✅   | A reassembled 26-octet engine record: oil 3.0 bar (raw 3000), oil 90.05 C (raw 3632), coolant 90.0 C              |
|  19 | `test_decode_temperature`                                      |   ✅   | sid 5, instance 0, source inside, actual 25.0 C (raw 29815 = 298.15 K), set 20.0 C (raw 29315).                   |
|  20 | `test_decode_battery_status`                                   |   ✅   | instance 1, voltage 12.6 V (raw 1260 = 0x04EC), current 5.5 A (raw 55), temp 25.0 C (raw 29815 = 0x7477), sid 10. |
|  21 | `test_decode_fluid_level`                                      |   ✅   | instance 2, fuel (type 0) -> byte0 0x02; level 75% (raw 18750 = 0x493E); capacity 200 L (raw 2000 = 0x07D0).      |
|  22 | `test_decode_actual_pressure`                                  |   ✅   | sid 7, instance 0, atmospheric source, 101325 Pa (raw 1013250 = 0x000F7602 at 0.1 Pa/bit).                        |
|  23 | `test_decode_attitude`                                         |   ✅   | sid 5, yaw 0.5236 rad (raw 5236), pitch 0.1 rad (raw 1000), roll -0.2 rad (raw -2000).                            |
|  24 | `test_decode_rudder`                                           |   ✅   | instance 0, move-to-starboard order, angle order 0.1745 rad (raw 1745), position 0.1571 rad (raw 1571).           |
|  25 | `test_decode_wind_data`                                        |   ✅   | sid 0x2A, speed 5.00 m/s (raw 500), angle 1.5708 rad (raw 15708), reference apparent.                             |
|  26 | `test_decode_speed`                                            |   ✅   | sid 5, water speed 5.14 m/s (raw 514), ground speed 5.50 m/s (raw 550), paddle-wheel sensor.                      |
|  27 | `test_decode_water_depth`                                      |   ✅   | SID 1, depth 12.34 m (raw 1234), transducer offset 0.5 m (raw 500).                                               |
|  28 | `test_decode_vessel_heading`                                   |   ✅   | SID 2, heading 1.5708 rad (90 deg, raw 15708), deviation 0, variation -0.1 rad, reference magnetic.               |

</details>

---

## test_mbus - native_mbus - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the wired M-Bus codec (services/fieldbus/mbus): the ACK / short / long frame builders_

|   # | Test                            | Status | Description                                                                     |
| --: | :------------------------------ | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_ack`                      |   ✅   | Ack                                                                             |
|   2 | `test_short_frame_roundtrip`    |   ✅   | Short frame roundtrip                                                           |
|   3 | `test_req_ud2_fcb`              |   ✅   | Req ud2 fcb                                                                     |
|   4 | `test_req_ud1_fcb`              |   ✅   | REQ_UD1 (class-1 / alarm data): C = 0x5A, or 0x7A with the FCB bit set.         |
|   5 | `test_long_frame_roundtrip`     |   ✅   | Long frame roundtrip                                                            |
|   6 | `test_parse_rejects_corruption` |   ✅   | Parse rejects corruption                                                        |
|   7 | `test_dif_data_len`             |   ✅   | Dif data len                                                                    |
|   8 | `test_record_walk`              |   ✅   | Record walk                                                                     |
|   9 | `test_record_truncated_fails`   |   ✅   | Record truncated fails                                                          |
|  10 | `test_build_and_parse_guards`   |   ✅   | Builder guards.                                                                 |
|  11 | `test_long_frame_control`       |   ✅   | data_len == 0 builds a control frame: a long frame carrying no user data.       |
|  12 | `test_parse_null_consumed`      |   ✅   | consumed may be nullptr on all three successful-parse paths.                    |
|  13 | `test_dif_data_len_remaining`   |   ✅   | Dif data len remaining                                                          |
|  14 | `test_record_edges`             |   ✅   | Record edges                                                                    |
|  15 | `test_record_vife_chain`        |   ✅   | Record vife chain                                                               |
|  16 | `test_value_int`                |   ✅   | Value int                                                                       |
|  17 | `test_value_real`               |   ✅   | Value real                                                                      |
|  18 | `test_vif_decode`               |   ✅   | Vif decode                                                                      |
|  19 | `test_var_header`               |   ✅   | A CI=0x72 fixed header: serial 12345678, manufacturer LUG (Landis+Gyr, 0x32A7), |

</details>

---

## test_iec60870 - native_iec60870 - ✅ 29 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the IEC 60870-5-101/-104 codec (services/energy/iec60870): the -104 APCI (I/S/U_

|   # | Test                                                                  | Status | Description                                                                                            |
| --: | :-------------------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------------------- |
|   1 | `test_104_i_format_roundtrip`                                         |   ✅   | 104 i format roundtrip                                                                                 |
|   2 | `test_104_s_format`                                                   |   ✅   | 104 s format                                                                                           |
|   3 | `test_104_u_format`                                                   |   ✅   | 104 u format                                                                                           |
|   4 | `test_104_sequence_numbers_15bit`                                     |   ✅   | 104 sequence numbers 15bit                                                                             |
|   5 | `test_asdu_header_roundtrip`                                          |   ✅   | Asdu header roundtrip                                                                                  |
|   6 | `test_ioa_roundtrip`                                                  |   ✅   | Ioa roundtrip                                                                                          |
|   7 | `test_101_fixed_frame`                                                |   ✅   | 101 fixed frame                                                                                        |
|   8 | `test_101_variable_frame_roundtrip`                                   |   ✅   | 101 variable frame roundtrip                                                                           |
|   9 | `test_104_build_guards`                                               |   ✅   | 104 build guards                                                                                       |
|  10 | `test_104_parse_rejects`                                              |   ✅   | 104 parse rejects                                                                                      |
|  11 | `test_asdu_ioa_guards`                                                |   ✅   | Asdu ioa guards                                                                                        |
|  12 | `test_101_build_guards`                                               |   ✅   | 101 build guards                                                                                       |
|  13 | `test_101_parse_rejects`                                              |   ✅   | 101 parse rejects                                                                                      |
|  14 | `test_104_parse_null_out_and_too_short`                               |   ✅   | 104 parse null out and too short                                                                       |
|  15 | `test_104_parse_consumed_null`                                        |   ✅   | 104 parse consumed null                                                                                |
|  16 | `test_asdu_header_build_null_h_and_flag_branches`                     |   ✅   | Asdu header build null h and flag branches                                                             |
|  17 | `test_asdu_header_parse_null_args_and_consumed_null`                  |   ✅   | Asdu header parse null args and consumed null                                                          |
|  18 | `test_101_build_variable_null_buf_and_zero_len_roundtrip`             |   ✅   | 101 build variable null buf and zero len roundtrip                                                     |
|  19 | `test_101_parse_null_out`                                             |   ✅   | 101 parse null out                                                                                     |
|  20 | `test_101_parse_fixed_too_short_and_consumed_null`                    |   ✅   | 101 parse fixed too short and consumed null                                                            |
|  21 | `test_101_parse_variable_bad_second_start_and_truncated_and_bad_stop` |   ✅   | 101 parse variable bad second start and truncated and bad stop                                         |
|  22 | `test_io_single_point`                                                |   ✅   | Io single point                                                                                        |
|  23 | `test_io_measured_float`                                              |   ✅   | Io measured float                                                                                      |
|  24 | `test_io_measured_scaled`                                             |   ✅   | M_ME_NB_1: IOA(3) + signed 16-bit SVA (LE) + QDS(1); 12345 = 0x3039 -> bytes 39 30.                    |
|  25 | `test_io_measured_normalized`                                         |   ✅   | M_ME_NA_1: IOA(3) + signed 16-bit NVA (LE) + QDS(1); 0.5 -> 0.5*32768 = 16384 = 0x4000 -> bytes 00 40. |
|  26 | `test_io_integrated_totals`                                           |   ✅   | M_IT_NA_1: IOA(3) + BCR = signed 32-bit counter (LE) + sequence-notation octet.                        |
|  27 | `test_io_single_command_in_asdu`                                      |   ✅   | Assemble a C_SC_NA_1 ASDU: the 6-octet header + one single-command object (select, ON).                |
|  28 | `test_io_double_point`                                                |   ✅   | DPI = ON (2), quality = not-topical.                                                                   |
|  29 | `test_io_double_command_in_asdu`                                      |   ✅   | Assemble a C_DC_NA_1 ASDU: the 6-octet header + one double-command object (select, ON, QU 3).          |

</details>

---

## test_sdi12 - native_sdi12 - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the SDI-12 codec (services/peripherals/sdi12): the command builders, the measurement_

|   # | Test                                            | Status | Description                                                                                              |
| --: | :---------------------------------------------- | :----: | :------------------------------------------------------------------------------------------------------- |
|   1 | `test_command_builders`                         |   ✅   | Command builders                                                                                         |
|   2 | `test_build_continuous_and_verify`              |   ✅   | Build continuous and verify                                                                              |
|   3 | `test_build_additional_measurements`            |   ✅   | Build additional measurements                                                                            |
|   4 | `test_parse_measure_m`                          |   ✅   | aM! response "0" + "012" (12 s) + "2" (2 values).                                                        |
|   5 | `test_parse_identify`                           |   ✅   | aI! response: addr 0, SDI-12 v1.4, vendor "ACMEINC " (space-padded to 8), model "SNS100", version "1.0". |
|   6 | `test_parse_measure_concurrent_two_digit_count` |   ✅   | aC! response "0" + "013" (13 s) + "10" (10 values).                                                      |
|   7 | `test_parse_values`                             |   ✅   | Parse values                                                                                             |
|   8 | `test_crc_roundtrip`                            |   ✅   | Build a response, append the SDI-12 CRC, then verify it (and that corruption fails).                     |
|   9 | `test_crc_encode_printable`                     |   ✅   | Crc encode printable                                                                                     |
|  10 | `test_sdi12_error_paths`                        |   ✅   | Sdi12 error paths                                                                                        |
|  11 | `test_build_concurrent_crc`                     |   ✅   | Build concurrent crc                                                                                     |
|  12 | `test_parse_measure_null_outputs`               |   ✅   | Parse measure null outputs                                                                               |
|  13 | `test_parse_measure_count_runs_to_buffer_end`   |   ✅   | Parse measure count runs to buffer end                                                                   |
|  14 | `test_parse_values_stops_at_max`                |   ✅   | 3 values present but max is 2: the loop must exit via cnt<max turning false.                             |
|  15 | `test_parse_values_bare_lf_and_minus_no_digits` |   ✅   | A lone '\n' terminator (no preceding '\r') exercises the c=='\n' branch directly                         |
|  16 | `test_check_crc_trims_to_nothing`               |   ✅   | Check crc trims to nothing                                                                               |

</details>

---

## test_dmx - native_dmx - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DMX512 + RDM codec (services/peripherals/dmx): the DMX512 slot packet, and the RDM_

|   # | Test                                   | Status | Description                                                                                              |
| --: | :------------------------------------- | :----: | :------------------------------------------------------------------------------------------------------- |
|   1 | `test_dmx_build_and_get`               |   ✅   | Dmx build and get                                                                                        |
|   2 | `test_rdm_uid`                         |   ✅   | Rdm uid                                                                                                  |
|   3 | `test_rdm_decode_disc_response`        |   ✅   | Rdm decode disc response                                                                                 |
|   4 | `test_rdm_build_disc_response`         |   ✅   | The builder must match the test's independent reference encoder byte-for-byte, at every preamble length. |
|   5 | `test_rdm_get_roundtrip`               |   ✅   | Rdm get roundtrip                                                                                        |
|   6 | `test_rdm_set_with_data`               |   ✅   | Rdm set with data                                                                                        |
|   7 | `test_rdm_device_info`                 |   ✅   | Packs the 19-octet big-endian DEVICE_INFO block byte-exact (E1.20 Table A-15 field order).               |
|   8 | `test_rdm_parse_rejects_bad`           |   ✅   | Rdm parse rejects bad                                                                                    |
|   9 | `test_dmx_rdm_error_paths`             |   ✅   | Dmx rdm error paths                                                                                      |
|  10 | `test_dmx_build_get_channel_branches`  |   ✅   | Dmx build get channel branches                                                                           |
|  11 | `test_rdm_parse_null_out_and_consumed` |   ✅   | Rdm parse null out and consumed                                                                          |

</details>

---

## test_nmea0183 - native_nmea0183 - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the NMEA 0183 codec (services/timing_position/nmea0183): the XOR checksum, sentence build,_

|   # | Test                                             | Status | Description                                                                                       |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------------------ |
|   1 | `test_checksum_known_vector`                     |   ✅   | Checksum known vector                                                                             |
|   2 | `test_build`                                     |   ✅   | Build                                                                                             |
|   3 | `test_parse_gga`                                 |   ✅   | Parse gga                                                                                         |
|   4 | `test_field_helpers`                             |   ✅   | Field helpers                                                                                     |
|   5 | `test_parse_rejects_bad_checksum`                |   ✅   | Flip the checksum digits.                                                                         |
|   6 | `test_parse_rejects_no_dollar`                   |   ✅   | Parse rejects no dollar                                                                           |
|   7 | `test_build_then_parse`                          |   ✅   | Build then parse                                                                                  |
|   8 | `test_nmea0183_error_paths`                      |   ✅   | Nmea0183 error paths                                                                              |
|   9 | `test_nmea0183_hex_val_edges`                    |   ✅   | Nmea0183 hex val edges                                                                            |
|  10 | `test_nmea0183_parse_guards`                     |   ✅   | Nmea0183 parse guards                                                                             |
|  11 | `test_nmea0183_parse_scan_edges`                 |   ✅   | Nmea0183 parse scan edges                                                                         |
|  12 | `test_nmea0183_field_overflow_and_short_address` |   ✅   | Nmea0183 field overflow and short address                                                         |
|  13 | `test_nmea0183_field_helpers_more_guards`        |   ✅   | Nmea0183 field helpers more guards                                                                |
|  14 | `test_decode_gga`                                |   ✅   | Decode gga                                                                                        |
|  15 | `test_decode_rmc`                                |   ✅   | Decode rmc                                                                                        |
|  16 | `test_decode_gsv`                                |   ✅   | Classic GSV: 3 sentences, this is #1, 11 satellites in view, 4 satellite records.                 |
|  17 | `test_decode_gsv_blank_snr_and_partial`          |   ✅   | A single-satellite GSV whose SNR field is blank (in view, not tracked).                           |
|  18 | `test_decode_zda`                                |   ✅   | Full ZDA: 20:15:30.50 UTC on 2026-07-04, local zone -05:30.                                       |
|  19 | `test_decode_vtg`                                |   ✅   | Course 54.7 T / 34.4 M, speed 5.5 kn / 10.2 km/h, autonomous mode.                                |
|  20 | `test_decode_gsa`                                |   ✅   | 3D auto fix on 5 satellites (blank PRN slots between them), PDOP 2.5 / HDOP 1.3 / VDOP 2.1.       |
|  21 | `test_decode_mwv`                                |   ✅   | Apparent wind at 214.8 deg relative, 10.5 knots, valid.                                           |
|  22 | `test_decode_dpt`                                |   ✅   | Depth 2.4 m relative to the transducer, offset +0.5 m (to waterline), range scale 200 m.          |
|  23 | `test_decode_hdg`                                |   ✅   | Magnetic heading 98.3 deg, deviation 0.0 E, variation 12.6 W (-> -12.6 signed).                   |
|  24 | `test_decode_gll`                                |   ✅   | Classic GLL (pre-NMEA-2.3, no mode field): position 4916.45N / 12311.12W, 22:54:44 UTC, valid.    |
|  25 | `test_decode_vhw`                                |   ✅   | Water speed + heading: heading 10.0 true / 8.5 magnetic, 6.5 knots / 12.0 km/h through the water. |
|  26 | `test_decode_vlw`                                |   ✅   | Distance through water: 1234.5 nm total, 12.3 nm since reset.                                     |
|  27 | `test_decode_type_mismatch`                      |   ✅   | Decode type mismatch                                                                              |

</details>

---

## test_ubx - native_ubx - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the UBX codec (services/timing_position/ubx): the 8-bit Fletcher checksum, frame build / poll,_

|   # | Test                                | Status | Description                                                             |
| --: | :---------------------------------- | :----: | :---------------------------------------------------------------------- |
|   1 | `test_checksum_known_vector`        |   ✅   | Checksum known vector                                                   |
|   2 | `test_build_poll_mon_ver`           |   ✅   | Build poll mon ver                                                      |
|   3 | `test_build_poll_cfg_prt`           |   ✅   | Build poll cfg prt                                                      |
|   4 | `test_build_with_payload`           |   ✅   | UBX-CFG-MSG set-rate: enable UBX-NAV-PVT (01 07) at rate 1.             |
|   5 | `test_build_rejects_bad_args`       |   ✅   | Build rejects bad args                                                  |
|   6 | `test_parse_roundtrip`              |   ✅   | Parse roundtrip                                                         |
|   7 | `test_parse_rejects`                |   ✅   | Parse rejects                                                           |
|   8 | `test_ack`                          |   ✅   | ACK-ACK (05 01)                                                         |
|   9 | `test_le_readers`                   |   ✅   | Le readers                                                              |
|  10 | `test_stream_demux_mixed`           |   ✅   | ASCII NMEA, then a UBX frame, then more ASCII.                          |
|  11 | `test_stream_bad_checksum_resyncs`  |   ✅   | Stream bad checksum resyncs                                             |
|  12 | `test_stream_overflow_skips`        |   ✅   | Hand-craft a frame declaring a payload length above PC_UBX_MAX_PAYLOAD. |
|  13 | `test_stream_false_and_double_sync` |   ✅   | Stream false and double sync                                            |
|  14 | `test_stream_null_safe`             |   ✅   | Stream null safe                                                        |
|  15 | `test_nav_pvt_decode`               |   ✅   | Nav pvt decode                                                          |
|  16 | `test_nav_pvt_rejects`              |   ✅   | Wrong class/id: an ACK-ACK frame is not a NAV-PVT.                      |
|  17 | `test_nav_sat_decode`               |   ✅   | Nav sat decode                                                          |
|  18 | `test_nav_sat_rejects`              |   ✅   | Wrong class/id (a NAV-PVT frame is not NAV-SAT).                        |
|  19 | `test_build_cfg_msg`                |   ✅   | Build cfg msg                                                           |
|  20 | `test_build_cfg_rate`               |   ✅   | Build cfg rate                                                          |
|  21 | `test_nav_timeutc_decode`           |   ✅   | Nav timeutc decode                                                      |

</details>

---

## test_ptp - native_ptp - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the PTP / IEEE 1588-2008 codec (services/timing_position/ptp): the 10-octet timestamp, the 34-octet_

|   # | Test                            | Status | Description                                                                     |
| --: | :------------------------------ | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_timestamp_roundtrip`      |   ✅   | Timestamp roundtrip                                                             |
|   2 | `test_timestamp_ns_conversion`  |   ✅   | Timestamp ns conversion                                                         |
|   3 | `test_header_roundtrip`         |   ✅   | Header roundtrip                                                                |
|   4 | `test_header_rejects`           |   ✅   | Header rejects                                                                  |
|   5 | `test_sync_delay_req_follow_up` |   ✅   | Sync delay req follow up                                                        |
|   6 | `test_timestamp_msg_rejects`    |   ✅   | Timestamp msg rejects                                                           |
|   7 | `test_delay_resp`               |   ✅   | Delay resp                                                                      |
|   8 | `test_announce`                 |   ✅   | Announce                                                                        |
|   9 | `test_build_announce`           |   ✅   | Build announce                                                                  |
|  10 | `test_compute_symmetric`        |   ✅   | Symmetric path delay d = 50 ns, true offset o = 25 ns:                          |
|  11 | `test_pdelay_mechanism`         |   ✅   | Pdelay mechanism                                                                |
|  12 | `test_pdelay_link_delay`        |   ✅   | t1 = req egress, t2 = req ingress at peer, t3 = resp egress, t4 = resp ingress. |

</details>

---

## test_roaming - native_roaming - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Wi-Fi roaming decision layer (services/system/roaming): the pure policy that fuses the_

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

## test_snmp_ber - native_snmp - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the SNMP ASN.1 BER codec. Encodings are checked against_

|   # | Test                                                     | Status | Description                                                                     |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_enc_init_rejects_unusable_buffer`                  |   ✅   | Enc init rejects unusable buffer                                                |
|   2 | `test_read_header_on_failed_decoder`                     |   ✅   | Read header on failed decoder                                                   |
|   3 | `test_read_integer_rejects_bad_tlv`                      |   ✅   | Read integer rejects bad tlv                                                    |
|   4 | `test_read_integer_sign_extends_negative`                |   ✅   | Read integer sign extends negative                                              |
|   5 | `test_read_oid_truncated_header_and_tiny_max`            |   ✅   | Read oid truncated header and tiny max                                          |
|   6 | `test_ber_skip_cursor_past_end`                          |   ✅   | Ber skip cursor past end                                                        |
|   7 | `test_integer_vectors`                                   |   ✅   | Integer vectors                                                                 |
|   8 | `test_oid_vector`                                        |   ✅   | 1.3.6.1 -> 06 03 2B 06 01                                                       |
|   9 | `test_octet_string_and_null`                             |   ✅   | Octet string and null                                                           |
|  10 | `test_counter32_keeps_unsigned`                          |   ✅   | 0x80000000 has the top bit set -> a leading 0x00 must be added.                 |
|  11 | `test_sequence_roundtrip`                                |   ✅   | Sequence roundtrip                                                              |
|  12 | `test_oid_roundtrip`                                     |   ✅   | Oid roundtrip                                                                   |
|  13 | `test_large_arc_roundtrip`                               |   ✅   | An arc > 127 exercises multi-byte base-128 encoding (e.g. enterprise 8072).     |
|  14 | `test_oid_large_first_subidentifier_roundtrip`           |   ✅   | Oid large first subidentifier roundtrip                                         |
|  15 | `test_encoder_overflow_sets_not_ok`                      |   ✅   | Encoder overflow sets not ok                                                    |
|  16 | `test_decoder_truncated_length_fails`                    |   ✅   | Claims 10 bytes of content but only 2 are present.                              |
|  17 | `test_decoder_longform_length_count_past_buffer_fails`   |   ✅   | Decoder longform length count past buffer fails                                 |
|  18 | `test_decoder_longform_length_too_wide_fails`            |   ✅   | Decoder longform length too wide fails                                          |
|  19 | `test_decoder_longform_length_content_past_buffer_fails` |   ✅   | 0x82 0x01 0x00 = long form, length 256; only a few content bytes follow.        |
|  20 | `test_decoder_longform_length_max_uint32_fails`          |   ✅   | Decoder longform length max uint32 fails                                        |
|  21 | `test_decoder_indefinite_length_fails`                   |   ✅   | Decoder indefinite length fails                                                 |
|  22 | `test_decoder_oversized_integer_fails`                   |   ✅   | Decoder oversized integer fails                                                 |
|  23 | `test_enc_len_long_form`                                 |   ✅   | A value >= 128 octets forces the long-form definite length (0x81 <len>).        |
|  24 | `test_put_oid_guards`                                    |   ✅   | Put oid guards                                                                  |
|  25 | `test_seq_end_overflow`                                  |   ✅   | A content region larger than the 16-bit back-patched length field fails closed. |
|  26 | `test_read_oid_rejects`                                  |   ✅   | pc_ber_read_oid on a non-OID TLV.                                               |
|  27 | `test_ber_skip`                                          |   ✅   | Ber skip                                                                        |

</details>

---

## test_snmp_agent - native_snmp - ✅ 41 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the SNMP v1/v2c agent core (pc_snmp_agent_process). Each test_

|   # | Test                                           | Status | Description                                                                              |
| --: | :--------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_init_community_defaults`                 |   ✅   | Init community defaults                                                                  |
|   2 | `test_empty_rw_community_clears_write`         |   ✅   | Empty rw community clears write                                                          |
|   3 | `test_add_string_null_value`                   |   ✅   | Add string null value                                                                    |
|   4 | `test_registration_table_limits`               |   ✅   | Registration table limits                                                                |
|   5 | `test_getnext_picks_smallest_out_of_order`     |   ✅   | Getnext picks smallest out of order                                                      |
|   6 | `test_set_v1_error_variants`                   |   ✅   | Set v1 error variants                                                                    |
|   7 | `test_get_failing_getter_is_nosuchinstance`    |   ✅   | Get failing getter is nosuchinstance                                                     |
|   8 | `test_get_short_oid_is_nosuchobject`           |   ✅   | Get short oid is nosuchobject                                                            |
|   9 | `test_getbulk_saturates_varbind_table`         |   ✅   | Getbulk saturates varbind table                                                          |
|  10 | `test_dispatch_truncated_pdu_fields`           |   ✅   | varbind list declares one byte of content; that byte is a bare tag with no length octet. |
|  11 | `test_dispatch_empty_varbind_list_tiny_buffer` |   ✅   | Dispatch empty varbind list tiny buffer                                                  |
|  12 | `test_message_truncated_before_community`      |   ✅   | Message truncated before community                                                       |
|  13 | `test_udp_handler_drops_unanswerable`          |   ✅   | Udp handler drops unanswerable                                                           |
|  14 | `test_registration_and_rw_edges`               |   ✅   | Registration and rw edges                                                                |
|  15 | `test_ipaddress_value_encodes`                 |   ✅   | Ipaddress value encodes                                                                  |
|  16 | `test_set_wrong_type_and_unknown`              |   ✅   | Set wrong type and unknown                                                               |
|  17 | `test_getbulk_variants`                        |   ✅   | non-repeaters = 1, max-repetitions = 2, one varbind at the system prefix.                |
|  18 | `test_dispatch_value_types_and_malformed`      |   ✅   | uint-typed and OID-typed varbind values decode without error.                            |
|  19 | `test_get_string_v2c`                          |   ✅   | Get string v2c                                                                           |
|  20 | `test_get_unknown_v2c_exception`               |   ✅   | Get unknown v2c exception                                                                |
|  21 | `test_get_bad_instance_v2c_nosuchinstance`     |   ✅   | Get bad instance v2c nosuchinstance                                                      |
|  22 | `test_get_unknown_v1_error`                    |   ✅   | Get unknown v1 error                                                                     |
|  23 | `test_getnext_walks_to_first`                  |   ✅   | Getnext walks to first                                                                   |
|  24 | `test_getnext_past_end_endofmibview`           |   ✅   | Getnext past end endofmibview                                                            |
|  25 | `test_set_without_rw_community_denied`         |   ✅   | Set without rw community denied                                                          |
|  26 | `test_set_with_rw_community_invokes_setter`    |   ✅   | Set with rw community invokes setter                                                     |
|  27 | `test_set_readonly_not_writable`               |   ✅   | Set readonly not writable                                                                |
|  28 | `test_getbulk_returns_multiple`                |   ✅   | non-repeaters=0, max-repetitions=3, one repeater starting at the system prefix.          |
|  29 | `test_dynamic_counter_value`                   |   ✅   | Dynamic counter value                                                                    |
|  30 | `test_uptime_is_timeticks`                     |   ✅   | Uptime is timeticks                                                                      |
|  31 | `test_unknown_community_no_response`           |   ✅   | Unknown community no response                                                            |
|  32 | `test_v3_message_dropped`                      |   ✅   | V3 message dropped                                                                       |
|  33 | `test_getbulk_repeaters_and_end`               |   ✅   | Pure repeaters (non_rep=0, max_rep=3) walk successive OIDs from the sys prefix.          |
|  34 | `test_getbulk_nonrep_clamp_and_v1_reject`      |   ✅   | non_rep (5) exceeds the single varbind -> clamped to the varbind count.                  |
|  35 | `test_response_too_big_reencodes`              |   ✅   | Response too big reencodes                                                               |
|  36 | `test_version_and_community_guards`            |   ✅   | v3 with the USM layer not built here -> 0.                                               |
|  37 | `test_dispatch_malformed_pdu`                  |   ✅   | A PDU whose header parses but whose request-id integer is truncated fails closed.        |
|  38 | `test_udp_handler_via_inject`                  |   ✅   | Udp handler via inject                                                                   |
|  39 | `test_malformed_message_guards`                |   ✅   | Malformed message guards                                                                 |
|  40 | `test_snmp_dispatch_varbind_guards`            |   ✅   | Snmp dispatch varbind guards                                                             |
|  41 | `test_snmp_oid_cmp_request_longer`             |   ✅   | Snmp oid cmp request longer                                                              |

</details>

---

## test_coap - native_coap - ✅ 58 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CoAP server core (pc_coap_server_process). Each test encodes a_

|   # | Test                                                  | Status | Description                                                                         |
| --: | :---------------------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_response_option_capacity_stop`                  |   ✅   | Response option capacity stop                                                       |
|   2 | `test_coap_udp_handler_basic`                         |   ✅   | Coap udp handler basic                                                              |
|   3 | `test_non_confirmable_malformed_is_silent`            |   ✅   | A reserved token length (9..15) in a CON is malformed: Reset, with an empty token.  |
|   4 | `test_response_code_as_request_is_method_not_allowed` |   ✅   | Response code as request is method not allowed                                      |
|   5 | `test_block1_ignored_on_get`                          |   ✅   | Block1 ignored on get                                                               |
|   6 | `test_block1_block_size_change_is_incomplete`         |   ✅   | Block1 block size change is incomplete                                              |
|   7 | `test_block1_empty_intermediate_block`                |   ✅   | Block1 empty intermediate block                                                     |
|   8 | `test_error_response_carries_no_observe_or_block2`    |   ✅   | Error response carries no observe or block2                                         |
|   9 | `test_block2_offset_at_end_of_representation`         |   ✅   | Block2 offset at end of representation                                              |
|  10 | `test_block2_on_empty_success_body`                   |   ✅   | Block2 on empty success body                                                        |
|  11 | `test_add_resource_limits`                            |   ✅   | Add resource limits                                                                 |
|  12 | `test_short_and_truncated_token`                      |   ✅   | Short and truncated token                                                           |
|  13 | `test_malformed_options_bad_request`                  |   ✅   | Malformed options bad request                                                       |
|  14 | `test_extended_delta_and_length_ignored`              |   ✅   | Extended delta and length ignored                                                   |
|  15 | `test_oversized_path_and_query`                       |   ✅   | Oversized path and query                                                            |
|  16 | `test_block_option_too_wide`                          |   ✅   | Block option too wide                                                               |
|  17 | `test_block1_reserved_szx`                            |   ✅   | Block1 reserved szx                                                                 |
|  18 | `test_block1_continue_no_space`                       |   ✅   | Block1 continue no space                                                            |
|  19 | `test_response_payload_clamped`                       |   ✅   | Response payload clamped                                                            |
|  20 | `test_response_buffer_too_small`                      |   ✅   | Response buffer too small                                                           |
|  21 | `test_well_known_core_truncates`                      |   ✅   | Well known core truncates                                                           |
|  22 | `test_observe_large_seq_encoding`                     |   ✅   | Observe large seq encoding                                                          |
|  23 | `test_block2_explicit_paging`                         |   ✅   | Block2 explicit paging                                                              |
|  24 | `test_block2_auto_when_large`                         |   ✅   | Block2 auto when large                                                              |
|  25 | `test_block2_szx_clamped`                             |   ✅   | Block2 szx clamped                                                                  |
|  26 | `test_block2_absent_for_small`                        |   ✅   | Block2 absent for small                                                             |
|  27 | `test_block2_out_of_range`                            |   ✅   | Block2 out of range                                                                 |
|  28 | `test_block2_reserved_szx`                            |   ✅   | Block2 reserved szx                                                                 |
|  29 | `test_block1_upload_two_blocks`                       |   ✅   | Block1 upload two blocks                                                            |
|  30 | `test_block1_out_of_order`                            |   ✅   | Block1 out of order                                                                 |
|  31 | `test_block1_too_large`                               |   ✅   | Block1 too large                                                                    |
|  32 | `test_observe_option_in_response`                     |   ✅   | Observe option in response                                                          |
|  33 | `test_response_option_overflows_buffer`               |   ✅   | resp holds the 4-byte header + 2-byte token (=6) but not the Content-Format option. |
|  34 | `test_no_observe_option_when_seq_negative`            |   ✅   | No observe option when seq negative                                                 |
|  35 | `test_get_content`                                    |   ✅   | Get content                                                                         |
|  36 | `test_not_found`                                      |   ✅   | Not found                                                                           |
|  37 | `test_method_not_allowed`                             |   ✅   | Method not allowed                                                                  |
|  38 | `test_non_request_type`                               |   ✅   | Non request type                                                                    |
|  39 | `test_put_with_payload`                               |   ✅   | Put with payload                                                                    |
|  40 | `test_multi_segment_path`                             |   ✅   | Multi segment path                                                                  |
|  41 | `test_uri_query`                                      |   ✅   | Uri query                                                                           |
|  42 | `test_empty_con_ping_rst`                             |   ✅   | Empty con ping rst                                                                  |
|  43 | `test_bad_version_rst`                                |   ✅   | Bad version rst                                                                     |
|  44 | `test_delete`                                         |   ✅   | Delete                                                                              |
|  45 | `test_token_8_bytes`                                  |   ✅   | Token 8 bytes                                                                       |
|  46 | `test_extended_option_length`                         |   ✅   | Extended option length                                                              |
|  47 | `test_ack_ignored`                                    |   ✅   | Ack ignored                                                                         |
|  48 | `test_root_path`                                      |   ✅   | Root path                                                                           |
|  49 | `test_unknown_method_not_allowed`                     |   ✅   | Code 0.05 (FETCH) is a valid class-0 code we don't implement. RFC 7252 5.8:         |
|  50 | `test_unknown_critical_option_bad_option`             |   ✅   | Hand-build: ver1/CON/TKL0, GET, MID, Uri-Path "temp", then Accept(17) - a           |
|  51 | `test_well_known_core_discovery`                      |   ✅   | Well known core discovery                                                           |
|  52 | `test_well_known_core_rejects_post`                   |   ✅   | Well known core rejects post                                                        |
|  53 | `test_dedup_store_lookup_roundtrip`                   |   ✅   | Dedup store lookup roundtrip                                                        |
|  54 | `test_dedup_full_address_keying`                      |   ✅   | Dedup full address keying                                                           |
|  55 | `test_dedup_expiry`                                   |   ✅   | Dedup expiry                                                                        |
|  56 | `test_dedup_too_large_not_cached`                     |   ✅   | Dedup too large not cached                                                          |
|  57 | `test_dedup_eviction_and_update`                      |   ✅   | Dedup eviction and update                                                           |
|  58 | `test_dedup_handler_replays_without_rerunning`        |   ✅   | Dedup handler replays without rerunning                                             |

</details>

---

## test_coap - native_coap_observe - ✅ 66 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CoAP server core (pc_coap_server_process). Each test encodes a_

|   # | Test                                                  | Status | Description                                                                         |
| --: | :---------------------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_response_option_capacity_stop`                  |   ✅   | Response option capacity stop                                                       |
|   2 | `test_coap_udp_handler_basic`                         |   ✅   | Coap udp handler basic                                                              |
|   3 | `test_coap_observe_over_udp`                          |   ✅   | Coap observe over udp                                                               |
|   4 | `test_coap_observe_registry_full`                     |   ✅   | Coap observe registry full                                                          |
|   5 | `test_coap_observe_registry_key_fields`               |   ✅   | Coap observe registry key fields                                                    |
|   6 | `test_coap_observe_zero_length_token`                 |   ✅   | Coap observe zero length token                                                      |
|   7 | `test_coap_observe_targeted_removal`                  |   ✅   | Coap observe targeted removal                                                       |
|   8 | `test_coap_notify_clamps_oversized_body`              |   ✅   | Coap notify clamps oversized body                                                   |
|   9 | `test_coap_observe_on_discovery_is_not_registered`    |   ✅   | Coap observe on discovery is not registered                                         |
|  10 | `test_coap_udp_edge_datagrams`                        |   ✅   | Coap udp edge datagrams                                                             |
|  11 | `test_non_confirmable_malformed_is_silent`            |   ✅   | A reserved token length (9..15) in a CON is malformed: Reset, with an empty token.  |
|  12 | `test_response_code_as_request_is_method_not_allowed` |   ✅   | Response code as request is method not allowed                                      |
|  13 | `test_block1_ignored_on_get`                          |   ✅   | Block1 ignored on get                                                               |
|  14 | `test_block1_block_size_change_is_incomplete`         |   ✅   | Block1 block size change is incomplete                                              |
|  15 | `test_block1_empty_intermediate_block`                |   ✅   | Block1 empty intermediate block                                                     |
|  16 | `test_error_response_carries_no_observe_or_block2`    |   ✅   | Error response carries no observe or block2                                         |
|  17 | `test_block2_offset_at_end_of_representation`         |   ✅   | Block2 offset at end of representation                                              |
|  18 | `test_block2_on_empty_success_body`                   |   ✅   | Block2 on empty success body                                                        |
|  19 | `test_add_resource_limits`                            |   ✅   | Add resource limits                                                                 |
|  20 | `test_short_and_truncated_token`                      |   ✅   | Short and truncated token                                                           |
|  21 | `test_malformed_options_bad_request`                  |   ✅   | Malformed options bad request                                                       |
|  22 | `test_extended_delta_and_length_ignored`              |   ✅   | Extended delta and length ignored                                                   |
|  23 | `test_oversized_path_and_query`                       |   ✅   | Oversized path and query                                                            |
|  24 | `test_block_option_too_wide`                          |   ✅   | Block option too wide                                                               |
|  25 | `test_block1_reserved_szx`                            |   ✅   | Block1 reserved szx                                                                 |
|  26 | `test_block1_continue_no_space`                       |   ✅   | Block1 continue no space                                                            |
|  27 | `test_response_payload_clamped`                       |   ✅   | Response payload clamped                                                            |
|  28 | `test_response_buffer_too_small`                      |   ✅   | Response buffer too small                                                           |
|  29 | `test_well_known_core_truncates`                      |   ✅   | Well known core truncates                                                           |
|  30 | `test_observe_large_seq_encoding`                     |   ✅   | Observe large seq encoding                                                          |
|  31 | `test_block2_explicit_paging`                         |   ✅   | Block2 explicit paging                                                              |
|  32 | `test_block2_auto_when_large`                         |   ✅   | Block2 auto when large                                                              |
|  33 | `test_block2_szx_clamped`                             |   ✅   | Block2 szx clamped                                                                  |
|  34 | `test_block2_absent_for_small`                        |   ✅   | Block2 absent for small                                                             |
|  35 | `test_block2_out_of_range`                            |   ✅   | Block2 out of range                                                                 |
|  36 | `test_block2_reserved_szx`                            |   ✅   | Block2 reserved szx                                                                 |
|  37 | `test_block1_upload_two_blocks`                       |   ✅   | Block1 upload two blocks                                                            |
|  38 | `test_block1_out_of_order`                            |   ✅   | Block1 out of order                                                                 |
|  39 | `test_block1_too_large`                               |   ✅   | Block1 too large                                                                    |
|  40 | `test_observe_option_in_response`                     |   ✅   | Observe option in response                                                          |
|  41 | `test_response_option_overflows_buffer`               |   ✅   | resp holds the 4-byte header + 2-byte token (=6) but not the Content-Format option. |
|  42 | `test_no_observe_option_when_seq_negative`            |   ✅   | No observe option when seq negative                                                 |
|  43 | `test_get_content`                                    |   ✅   | Get content                                                                         |
|  44 | `test_not_found`                                      |   ✅   | Not found                                                                           |
|  45 | `test_method_not_allowed`                             |   ✅   | Method not allowed                                                                  |
|  46 | `test_non_request_type`                               |   ✅   | Non request type                                                                    |
|  47 | `test_put_with_payload`                               |   ✅   | Put with payload                                                                    |
|  48 | `test_multi_segment_path`                             |   ✅   | Multi segment path                                                                  |
|  49 | `test_uri_query`                                      |   ✅   | Uri query                                                                           |
|  50 | `test_empty_con_ping_rst`                             |   ✅   | Empty con ping rst                                                                  |
|  51 | `test_bad_version_rst`                                |   ✅   | Bad version rst                                                                     |
|  52 | `test_delete`                                         |   ✅   | Delete                                                                              |
|  53 | `test_token_8_bytes`                                  |   ✅   | Token 8 bytes                                                                       |
|  54 | `test_extended_option_length`                         |   ✅   | Extended option length                                                              |
|  55 | `test_ack_ignored`                                    |   ✅   | Ack ignored                                                                         |
|  56 | `test_root_path`                                      |   ✅   | Root path                                                                           |
|  57 | `test_unknown_method_not_allowed`                     |   ✅   | Code 0.05 (FETCH) is a valid class-0 code we don't implement. RFC 7252 5.8:         |
|  58 | `test_unknown_critical_option_bad_option`             |   ✅   | Hand-build: ver1/CON/TKL0, GET, MID, Uri-Path "temp", then Accept(17) - a           |
|  59 | `test_well_known_core_discovery`                      |   ✅   | Well known core discovery                                                           |
|  60 | `test_well_known_core_rejects_post`                   |   ✅   | Well known core rejects post                                                        |
|  61 | `test_dedup_store_lookup_roundtrip`                   |   ✅   | Dedup store lookup roundtrip                                                        |
|  62 | `test_dedup_full_address_keying`                      |   ✅   | Dedup full address keying                                                           |
|  63 | `test_dedup_expiry`                                   |   ✅   | Dedup expiry                                                                        |
|  64 | `test_dedup_too_large_not_cached`                     |   ✅   | Dedup too large not cached                                                          |
|  65 | `test_dedup_eviction_and_update`                      |   ✅   | Dedup eviction and update                                                           |
|  66 | `test_dedup_handler_replays_without_rerunning`        |   ✅   | Dedup handler replays without rerunning                                             |

</details>

---

## test_modbus - native_modbus - ✅ 30 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Modbus TCP slave core (services/fieldbus/modbus): the data model and_

|   # | Test                                        | Status | Description                                                                     |
| --: | :------------------------------------------ | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_read_holding_registers`               |   ✅   | Read holding registers                                                          |
|   2 | `test_read_input_registers`                 |   ✅   | Read input registers                                                            |
|   3 | `test_read_coils_packs_bits`                |   ✅   | Read coils packs bits                                                           |
|   4 | `test_write_single_coil`                    |   ✅   | Write single coil                                                               |
|   5 | `test_write_single_register`                |   ✅   | Write single register                                                           |
|   6 | `test_write_multiple_registers`             |   ✅   | Write multiple registers                                                        |
|   7 | `test_write_multiple_coils`                 |   ✅   | qty 5, 1 byte of data: bits 0..4 = 1,0,1,1,0 -> 0x0D                            |
|   8 | `test_exception_illegal_function`           |   ✅   | Exception illegal function                                                      |
|   9 | `test_exception_illegal_address`            |   ✅   | Read holding regs beyond the 64-register table.                                 |
|  10 | `test_exception_illegal_value`              |   ✅   | Exception illegal value                                                         |
|  11 | `test_write_single_coil_bad_value`          |   ✅   | Write single coil bad value                                                     |
|  12 | `test_non_modbus_protocol_id_ignored`       |   ✅   | Non modbus protocol id ignored                                                  |
|  13 | `test_truncated_frame_ignored`              |   ✅   | Truncated frame ignored                                                         |
|  14 | `test_discrete_and_input_accessors`         |   ✅   | Discrete and input accessors                                                    |
|  15 | `test_exceptions_per_function`              |   ✅   | FC1/FC2 read coils/discrete.                                                    |
|  16 | `test_small_response_buffer`                |   ✅   | Small response buffer                                                           |
|  17 | `test_rtu_crc16_known_vector`               |   ✅   | Rtu crc16 known vector                                                          |
|  18 | `test_rtu_read_holding_roundtrip`           |   ✅   | Rtu read holding roundtrip                                                      |
|  19 | `test_rtu_bad_crc_dropped`                  |   ✅   | Rtu bad crc dropped                                                             |
|  20 | `test_rtu_wrong_address_dropped`            |   ✅   | Rtu wrong address dropped                                                       |
|  21 | `test_rtu_broadcast_executes_without_reply` |   ✅   | Rtu broadcast executes without reply                                            |
|  22 | `test_rtu_edge_cases`                       |   ✅   | Rtu edge cases                                                                  |
|  23 | `test_server_init_bounds_and_handler`       |   ✅   | Server init bounds and handler                                                  |
|  24 | `test_input_register_accessor_bounds`       |   ✅   | Input register accessor bounds                                                  |
|  25 | `test_read_quantity_bounds`                 |   ✅   | Read quantity bounds                                                            |
|  26 | `test_write_single_coil_off`                |   ✅   | Write single coil off                                                           |
|  27 | `test_writes_without_callback`              |   ✅   | Writes without callback                                                         |
|  28 | `test_multi_write_field_validation`         |   ✅   | FC15: qty 0, qty above the 1968 limit, and a byte count that runs past the PDU. |
|  29 | `test_adu_framing_guards`                   |   ✅   | Adu framing guards                                                              |
|  30 | `test_rtu_response_buffer_too_small`        |   ✅   | Rtu response buffer too small                                                   |

</details>

---

## test_redis_resp - native_redis - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Redis RESP2 codec (services/iot/redis_resp): the command encoder_

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_encode_command`                        |   ✅   | Encode command                        |
|   2 | `test_encode_binary_safe`                    |   ✅   | Encode binary safe                    |
|   3 | `test_encode_overflow_fails_closed`          |   ✅   | Encode overflow fails closed          |
|   4 | `test_parse_simple_and_error`                |   ✅   | Parse simple and error                |
|   5 | `test_parse_integer`                         |   ✅   | Parse integer                         |
|   6 | `test_parse_bulk_and_nil`                    |   ✅   | Parse bulk and nil                    |
|   7 | `test_parse_array_cursor`                    |   ✅   | Parse array cursor                    |
|   8 | `test_parse_incomplete_and_malformed`        |   ✅   | Parse incomplete and malformed        |
|   9 | `test_encode_guard_subconditions`            |   ✅   | Encode guard subconditions            |
|  10 | `test_parse_guard_subconditions_and_edges`   |   ✅   | Parse guard subconditions and edges   |
|  11 | `test_parse_resp3_null_bool`                 |   ✅   | Parse resp3 null bool                 |
|  12 | `test_parse_resp3_double`                    |   ✅   | Parse resp3 double                    |
|  13 | `test_parse_resp3_bignum_bulkerr_verbatim`   |   ✅   | Parse resp3 bignum bulkerr verbatim   |
|  14 | `test_parse_resp3_map_set_push`              |   ✅   | Parse resp3 map set push              |
|  15 | `test_encode_zero_length_arg`                |   ✅   | Encode zero length arg                |
|  16 | `test_encode_overflow_stages`                |   ✅   | Encode overflow stages                |
|  17 | `test_parse_resp3_double_forms`              |   ✅   | Parse resp3 double forms              |
|  18 | `test_parse_double_special_case_insensitive` |   ✅   | Parse double special case insensitive |
|  19 | `test_parse_bulk_body_rejects`               |   ✅   | Parse bulk body rejects               |
|  20 | `test_parse_aggregate_and_scalar_rejects`    |   ✅   | Parse aggregate and scalar rejects    |
|  21 | `test_parse_line_scan_and_integer_octets`    |   ✅   | Parse line scan and integer octets    |

</details>

---

## test_sqlite - native_sqlite - ✅ 43 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/sqlite: the SQLite3 on-disk file-format parsers. The page-1 vector below is the_

|   # | Test                                             | Status | Description                                                                                           |
| --: | :----------------------------------------------- | :----: | :---------------------------------------------------------------------------------------------------- |
|   1 | `test_db_header_real_file`                       |   ✅   | Db header real file                                                                                   |
|   2 | `test_db_header_rejects_bad_magic`               |   ✅   | Db header rejects bad magic                                                                           |
|   3 | `test_btree_header_real_page1`                   |   ✅   | Page 1's b-tree header follows the 100-byte database header.                                          |
|   4 | `test_btree_header_rejects_bad_type`             |   ✅   | Btree header rejects bad type                                                                         |
|   5 | `test_first_cell_varints`                        |   ✅   | The single cell pointer lives right after the 8-byte leaf header (offset 108), big-endian u16.        |
|   6 | `test_varint_spec_vectors`                       |   ✅   | Varint spec vectors                                                                                   |
|   7 | `test_serial_type_sizes`                         |   ✅   | Serial type sizes                                                                                     |
|   8 | `test_read_schema_row`                           |   ✅   | Read schema row                                                                                       |
|   9 | `test_column_int_signextend`                     |   ✅   | Column int signextend                                                                                 |
|  10 | `test_leaf_cell_overflow_detection`              |   ✅   | Leaf cell overflow detection                                                                          |
|  11 | `test_table_cursor_multipage`                    |   ✅   | The table's root page (page 2) is an interior table page, so this exercises the descent stack.        |
|  12 | `test_overflow_read_payload`                     |   ✅   | Overflow read payload                                                                                 |
|  13 | `test_read_payload_nonoverflow`                  |   ✅   | Read payload nonoverflow                                                                              |
|  14 | `test_read_payload_bad_overflow_pointer`         |   ✅   | Read payload bad overflow pointer                                                                     |
|  15 | `test_overflow_read_payload_bounds`              |   ✅   | Overflow read payload bounds                                                                          |
|  16 | `test_overflow_cursor`                           |   ✅   | Overflow cursor                                                                                       |
|  17 | `test_varint_encode_roundtrip`                   |   ✅   | Varint encode roundtrip                                                                               |
|  18 | `test_encode_record_roundtrip`                   |   ✅   | A row of (INT, TEXT, FLOAT, NULL, INT=0) round-trips through the record reader.                       |
|  19 | `test_build_table_db_roundtrip`                  |   ✅   | Build a real 2-page DB, then read it back with our own reader.                                        |
|  20 | `test_encode_record_int_widths`                  |   ✅   | Every integer serial type: the value round-trips and the encoder picks the minimal type.              |
|  21 | `test_encode_record_blob`                        |   ✅   | A BLOB column (serial type 12 + 2n) round-trips its raw bytes, including embedded NULs.               |
|  22 | `test_build_table_db_page_overflow_fails_closed` |   ✅   | Many rows that each fit but collectively exceed one leaf page must fail closed (distinct from the     |
|  23 | `test_build_table_db_fails_closed`               |   ✅   | A single row larger than one leaf page can hold must fail closed (bounded writer, no overflow pages). |
|  24 | `test_varint_decode_truncated_nine_byte`         |   ✅   | Eight continuation bytes with no ninth byte: the 9-byte form is incomplete.                           |
|  25 | `test_db_header_page_size_rejects`               |   ✅   | Db header page size rejects                                                                           |
|  26 | `test_btree_header_index_pages_and_truncation`   |   ✅   | An interior INDEX page is a valid b-tree page and carries the 12-byte header.                         |
|  27 | `test_cell_pointer_rejects`                      |   ✅   | Cell pointer rejects                                                                                  |
|  28 | `test_leaf_cell_parse_rejects`                   |   ✅   | Leaf cell parse rejects                                                                               |
|  29 | `test_record_begin_rejects`                      |   ✅   | Record begin rejects                                                                                  |
|  30 | `test_record_next_rejects`                       |   ✅   | A truncated serial-type varint inside the record header.                                              |
|  31 | `test_column_decoder_rejects`                    |   ✅   | Column decoder rejects                                                                                |
|  32 | `test_read_payload_chain_edges`                  |   ✅   | Read payload chain edges                                                                              |
|  33 | `test_cursor_descend_rejects`                    |   ✅   | The root page cannot be read at all.                                                                  |
|  34 | `test_cursor_depth_cap`                          |   ✅   | An endless interior chain stops at SQLITE_BTREE_MAX_DEPTH instead of overrunning the stack.           |
|  35 | `test_cursor_next_skips_bad_cells`               |   ✅   | Cursor next skips bad cells                                                                           |
|  36 | `test_cursor_parent_frame_rejects`               |   ✅   | Re-reading the parent interior page fails once the first leaf is exhausted.                           |
|  37 | `test_table_cursor_page1_schema_scan`            |   ✅   | Scanning the schema table roots the cursor at page 1, whose b-tree header sits after the              |
|  38 | `test_overflow_cursor_without_buffer`            |   ✅   | With no overflow buffer the cursor still yields every row, just the in-page prefix of the             |
|  39 | `test_overflow_cursor_short_buffer_skips_row`    |   ✅   | An overflow buffer too small for a row makes the reassembly fail, and that row is skipped             |
|  40 | `test_encode_record_empty_text_and_out_cap`      |   ✅   | Zero-length TEXT and BLOB columns contribute a serial type but no value bytes.                        |
|  41 | `test_encode_record_multibyte_header_size`       |   ✅   | 127 columns push the record header past 127 bytes, so the header-size varint itself grows to          |
|  42 | `test_build_table_db_input_rejects`              |   ✅   | Build table db input rejects                                                                          |
|  43 | `test_build_table_db_64k_empty_table`            |   ✅   | The largest legal page size: the on-disk page-size field stores 1, and an empty page-2 leaf           |

</details>

---

## test_stomp - native_stomp - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the STOMP 1.2 frame codec (services/iot/stomp): the frame builder, the_

|   # | Test                                                       | Status | Description                                                                    |
| --: | :--------------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_build_send`                                          |   ✅   | Build send                                                                     |
|   2 | `test_build_cr_escape_and_guards`                          |   ✅   | Build cr escape and guards                                                     |
|   3 | `test_parse_more_edges`                                    |   ✅   | Parse more edges                                                               |
|   4 | `test_header_and_unescape_null`                            |   ✅   | Header and unescape null                                                       |
|   5 | `test_build_no_headers_no_body`                            |   ✅   | Build no headers no body                                                       |
|   6 | `test_build_escapes_header`                                |   ✅   | Build escapes header                                                           |
|   7 | `test_build_overflow_fails_closed`                         |   ✅   | Build overflow fails closed                                                    |
|   8 | `test_round_trip`                                          |   ✅   | Round trip                                                                     |
|   9 | `test_parse_message_crlf`                                  |   ✅   | Parse message crlf                                                             |
|  10 | `test_parse_content_length_body_with_nul`                  |   ✅   | Parse content length body with nul                                             |
|  11 | `test_parse_skips_leading_heartbeats`                      |   ✅   | Parse skips leading heartbeats                                                 |
|  12 | `test_parse_incomplete_and_malformed`                      |   ✅   | Parse incomplete and malformed                                                 |
|  13 | `test_parse_header_capacity_cap`                           |   ✅   | Parse header capacity cap                                                      |
|  14 | `test_parse_duplicate_content_length_and_lookalike_header` |   ✅   | "contentxlength" is 14 chars, same as "content-length", but does not match it. |
|  15 | `test_header_lookup_edge_branches`                         |   ✅   | Header lookup edge branches                                                    |
|  16 | `test_unescape`                                            |   ✅   | Unescape                                                                       |
|  17 | `test_unescape_rejects_bad`                                |   ✅   | Unescape rejects bad                                                           |

</details>

---

## test_mqtt_sn - native_mqtt_sn - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the MQTT-SN v1.2 wire codec (services/iot/mqtt/mqtt_sn): the message_

|   # | Test                                       | Status | Description                                                                       |
| --: | :----------------------------------------- | :----: | :-------------------------------------------------------------------------------- |
|   1 | `test_parse_typed_null_payload`            |   ✅   | Parse typed null payload                                                          |
|   2 | `test_parse_typed_outputs_are_optional`    |   ✅   | Parse typed outputs are optional                                                  |
|   3 | `test_parse_header_output_guards`          |   ✅   | Parse header output guards                                                        |
|   4 | `test_publish_empty_and_oversized_body`    |   ✅   | Publish empty and oversized body                                                  |
|   5 | `test_make_flags`                          |   ✅   | DUP, QoS 2, retain, will, clean, short topic name.                                |
|   6 | `test_build_connect_bytes`                 |   ✅   | total = 1(len) + 1(type) + 1(flags) + 1(protoid) + 2(duration) + 4(clientid) = 10 |
|   7 | `test_build_publish_bytes`                 |   ✅   | total = 1+1+1(flags)+2(topic)+2(msgid)+2(data) = 9                                |
|   8 | `test_register_round_trip`                 |   ✅   | Register round trip                                                               |
|   9 | `test_parse_connack_regack_suback_publish` |   ✅   | Parse connack regack suback publish                                               |
|  10 | `test_three_octet_length`                  |   ✅   | Three octet length                                                                |
|  11 | `test_optional_fields`                     |   ✅   | PINGREQ with no client id is a 2-byte keep-alive.                                 |
|  12 | `test_overflow_and_malformed`              |   ✅   | Overflow and malformed                                                            |
|  13 | `test_build_regack_puback`                 |   ✅   | Build regack puback                                                               |
|  14 | `test_build_subscribe_variants`            |   ✅   | Build subscribe variants                                                          |
|  15 | `test_pingreq_with_client_id`              |   ✅   | Pingreq with client id                                                            |
|  16 | `test_build_guards`                        |   ✅   | Build guards                                                                      |
|  17 | `test_parse_typed_rejections`              |   ✅   | Parse typed rejections                                                            |

</details>

---

## test_flow_export - native_flow_export - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the flow-export codec (services/net/flow_export): NetFlow v5 fixed records,_

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_v5_header_bytes`                             |   ✅   | V5 header bytes                             |
|   2 | `test_v5_record_bytes`                             |   ✅   | V5 record bytes                             |
|   3 | `test_v5_overflow_fails_closed`                    |   ✅   | V5 overflow fails closed                    |
|   4 | `test_ipfix_message_bytes`                         |   ✅   | Ipfix message bytes                         |
|   5 | `test_v9_count_and_padding`                        |   ✅   | V9 count and padding                        |
|   6 | `test_finish_overflow_fails_closed`                |   ✅   | Finish overflow fails closed                |
|   7 | `test_v5_write_overflow`                           |   ✅   | V5 write overflow                           |
|   8 | `test_flow_guards_and_overflows`                   |   ✅   | begin null-arg guards + finish(null).       |
|   9 | `test_v5_write_null_guards`                        |   ✅   | V5 write null guards                        |
|  10 | `test_data_record_null_and_zero_len_with_set_open` |   ✅   | Data record null and zero len with set open |

</details>

---

## test_protobuf - native_protobuf - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Protocol Buffers wire codec (services/iot/protobuf): the streaming_

|   # | Test                                       | Status | Description                                                            |
| --: | :----------------------------------------- | :----: | :--------------------------------------------------------------------- |
|   1 | `test_writer_error_paths`                  |   ✅   | A 5-byte varint does not fit a 4-byte buffer.                          |
|   2 | `test_reader_error_paths`                  |   ✅   | Reader error paths                                                     |
|   3 | `test_float_bits_helper`                   |   ✅   | Float bits helper                                                      |
|   4 | `test_vector_field1_150`                   |   ✅   | Vector field1 150                                                      |
|   5 | `test_vector_string_testing`               |   ✅   | Vector string testing                                                  |
|   6 | `test_zigzag_mapping`                      |   ✅   | Decode: encoded 1 -> -1, 2 -> 1, 3 -> -2.                              |
|   7 | `test_fixed_and_float_bytes`               |   ✅   | Fixed and float bytes                                                  |
|   8 | `test_round_trip_reader`                   |   ✅   | Round trip reader                                                      |
|   9 | `test_int64_negative`                      |   ✅   | Int64 negative                                                         |
|  10 | `test_varint_and_overflow`                 |   ✅   | A multi-byte varint round-trips.                                       |
|  11 | `test_malformed_reads`                     |   ✅   | Malformed reads                                                        |
|  12 | `test_varint_width_boundary`               |   ✅   | The maximum 64-bit varint: nine 0xFF groups then 0x01 -> all bits set. |
|  13 | `test_empty_length_field`                  |   ✅   | Empty length field                                                     |
|  14 | `test_writer_error_is_sticky`              |   ✅   | Writer error is sticky                                                 |
|  15 | `test_bool_true_and_false`                 |   ✅   | Bool true and false                                                    |
|  16 | `test_uint64_tag_and_value_overflow`       |   ✅   | Uint64 tag and value overflow                                          |
|  17 | `test_fixed32_fixed64_tag_overflow`        |   ✅   | Fixed32 fixed64 tag overflow                                           |
|  18 | `test_bytes_header_overflow_and_null_data` |   ✅   | Bytes header overflow and null data                                    |
|  19 | `test_reader_additional_null_arg_paths`    |   ✅   | Reader additional null arg paths                                       |

</details>

---

## test_preempt_queue - native_preempt_queue - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the preempting work queue (services/system/preempt_queue) host core: the_

|   # | Test                                                | Status | Description                                                                           |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_start_validates_and_runs`                     |   ✅   | Start validates and runs                                                              |
|   2 | `test_fifo_order`                                   |   ✅   | Fifo order                                                                            |
|   3 | `test_urgent_goes_to_front`                         |   ✅   | Urgent goes to front                                                                  |
|   4 | `test_fail_closed_when_full`                        |   ✅   | The test env sizes PC_PQ_DEPTH = 4.                                                   |
|   5 | `test_high_water_tracks_peak`                       |   ✅   | High water tracks peak                                                                |
|   6 | `test_from_isr_enqueues`                            |   ✅   | From isr enqueues                                                                     |
|   7 | `test_drain_empties_and_reuses`                     |   ✅   | Drain empties and reuses                                                              |
|   8 | `test_internal_lanes_outrank_user`                  |   ✅   | DMA highest, then forward, then device, all above the user lane.                      |
|   9 | `test_lanes_are_isolated`                           |   ✅   | The USER lane is already started by setUp; start the internal DMA lane too.           |
|  10 | `test_lane_start_stop_running_independent`          |   ✅   | Lane start stop running independent                                                   |
|  11 | `test_lane_high_water_is_per_lane`                  |   ✅   | Lane high water is per lane                                                           |
|  12 | `test_lane_api_urgent_and_drain`                    |   ✅   | Lane api urgent and drain                                                             |
|  13 | `test_lane_guards_reject_bad_lane_and_null_item`    |   ✅   | A bad lane (>= PC_PQ_LANE_COUNT) must fail closed / return safe defaults on every     |
|  14 | `test_post_lane_urgent_fails_closed_when_full`      |   ✅   | Post lane urgent fails closed when full                                               |
|  15 | `test_drain_lane_without_handler_skips_call_safely` |   ✅   | FORWARD is never started elsewhere in this suite, so its handler stays null. The host |

</details>

---

## test_dma - native_dma - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DMA ingest / egress simulator (services/system/dma) host core: an ingress_

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

## test_trace_capture - native_trace_capture - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the pre/post-trigger sample-window assembler (services/system/trace_capture):_

|   # | Test                                                  | Status | Description                                                              |
| --: | :---------------------------------------------------- | :----: | :----------------------------------------------------------------------- |
|   1 | `test_begin_validates`                                |   ✅   | Begin validates                                                          |
|   2 | `test_pretrigger_ring_wraps_and_freezes_on_trigger`   |   ✅   | Pretrigger ring wraps and freezes on trigger                             |
|   3 | `test_trigger_fail_closed_while_capturing`            |   ✅   | Trigger fail closed while capturing                                      |
|   4 | `test_feed_before_begin_or_after_end_drops`           |   ✅   | Feed before begin or after end drops                                     |
|   5 | `test_zero_pretrigger_edge_case`                      |   ✅   | Zero pretrigger edge case                                                |
|   6 | `test_multiple_sequential_windows_increment_trace_id` |   ✅   | Multiple sequential windows increment trace id                           |
|   7 | `test_feed_null_samples_while_configured_drops`       |   ✅   | line 76: configured is true, so `!s_tc.configured` is false and the OR   |
|   8 | `test_zero_posttrigger_never_completes`               |   ✅   | line 85 second operand false: with posttrigger 0, after trigger the fill |
|   9 | `test_get_stats_null_and_capturing_when_unconfigured` |   ✅   | Get stats null and capturing when unconfigured                           |

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

## test_forward - native_forward - ✅ 33 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the interface forwarding plane (services/net/forward): default-deny, an_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_default_deny`                           |   ✅   | Default deny                           |
|   2 | `test_allow_forwards`                         |   ✅   | Allow forwards                         |
|   3 | `test_no_self_forward`                        |   ✅   | No self forward                        |
|   4 | `test_deny_wins_over_allow`                   |   ✅   | Deny wins over allow                   |
|   5 | `test_multi_destination_fanout`               |   ✅   | Multi destination fanout               |
|   6 | `test_rate_cap_drops_then_reopens`            |   ✅   | Rate cap drops then reopens            |
|   7 | `test_send_failure_counted`                   |   ✅   | Send failure counted                   |
|   8 | `test_add_if_validation_and_table_full`       |   ✅   | Add if validation and table full       |
|   9 | `test_add_rule_table_full`                    |   ✅   | Add rule table full                    |
|  10 | `test_unregistered_destination_is_inert`      |   ✅   | Unregistered destination is inert      |
|  11 | `test_rule_with_mismatched_src_is_ignored`    |   ✅   | Rule with mismatched src is ignored    |
|  12 | `test_duplicate_allow_rule_first_one_governs` |   ✅   | Duplicate allow rule first one governs |
|  13 | `test_get_stats_null_pointer_is_noop`         |   ✅   | Get stats null pointer is noop         |
|  14 | `test_acl_deny_by_byte_pattern`               |   ✅   | Acl deny by byte pattern               |
|  15 | `test_acl_allowlist_default_deny`             |   ✅   | Acl allowlist default deny             |
|  16 | `test_acl_first_match_wins`                   |   ✅   | Acl first match wins                   |
|  17 | `test_acl_src_any_content_wildcard`           |   ✅   | Acl src any content wildcard           |
|  18 | `test_acl_entry_src_mismatch_falls_through`   |   ✅   | Acl entry src mismatch falls through   |
|  19 | `test_acl_short_frame_skips_entry`            |   ✅   | Acl short frame skips entry            |
|  20 | `test_acl_add_validation_and_table_full`      |   ✅   | Acl add validation and table full      |
|  21 | `test_acl_add_null_pointer_validation`        |   ✅   | Acl add null pointer validation        |
|  22 | `test_route_selects_egress_and_falls_through` |   ✅   | Route selects egress and falls through |
|  23 | `test_route_never_reflects_to_source`         |   ✅   | Route never reflects to source         |
|  24 | `test_route_unregistered_egress_fail_closed`  |   ✅   | Route unregistered egress fail closed  |
|  25 | `test_route_src_specific_filters_by_source`   |   ✅   | Route src specific filters by source   |
|  26 | `test_route_send_failure_counted`             |   ✅   | Route send failure counted             |
|  27 | `test_route_rate_cap`                         |   ✅   | Route rate cap                         |
|  28 | `test_route_default_any_content`              |   ✅   | Route default any content              |
|  29 | `test_route_first_match_wins`                 |   ✅   | Route first match wins                 |
|  30 | `test_route_add_validation_and_table_full`    |   ✅   | Route add validation and table full    |
|  31 | `test_inspect_pass_and_drop`                  |   ✅   | Inspect pass and drop                  |
|  32 | `test_inspect_runs_after_acl`                 |   ✅   | Inspect runs after acl                 |
|  33 | `test_inspect_cleared_by_null`                |   ✅   | Inspect cleared by null                |

</details>

---

## test_gateway - native_gateway - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the radio / wireless gateway bridge (services/net/gateway): an uplink_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_uplink_envelopes_and_publishes`       |   ✅   | Uplink envelopes and publishes       |
|   2 | `test_uplink_no_sink_drops`                 |   ✅   | Uplink no sink drops                 |
|   3 | `test_uplink_unknown_port_drops`            |   ✅   | Uplink unknown port drops            |
|   4 | `test_uplink_rate_cap`                      |   ✅   | Uplink rate cap                      |
|   5 | `test_uplink_sink_refusal_counted`          |   ✅   | Uplink sink refusal counted          |
|   6 | `test_downlink_transmits`                   |   ✅   | Downlink transmits                   |
|   7 | `test_downlink_no_tx_or_unknown_port_drops` |   ✅   | Downlink no tx or unknown port drops |
|   8 | `test_downlink_tx_refusal_counted`          |   ✅   | Downlink tx refusal counted          |
|   9 | `test_topic_format`                         |   ✅   | Topic format                         |
|  10 | `test_add_port_validation_and_table_full`   |   ✅   | Add port validation and table full   |
|  11 | `test_seq_increments_per_uplink`            |   ✅   | Seq increments per uplink            |
|  12 | `test_topic_zero_and_overflow_steps`        |   ✅   | Topic zero and overflow steps        |
|  13 | `test_get_stats_null_out_is_noop`           |   ✅   | Get stats null out is noop           |

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

## test_enocean - native_enocean - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the EnOcean ESP3 codec (services/radio/enocean): the CRC-8 (poly 0x07) against_

|   # | Test                                   | Status | Description                                                                                 |
| --: | :------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_crc8_known_answers`              |   ✅   | Crc8 known answers                                                                          |
|   2 | `test_build_then_parse_round_trip`     |   ✅   | Build then parse round trip                                                                 |
|   3 | `test_parse_rejects_bad_sync`          |   ✅   | Parse rejects bad sync                                                                      |
|   4 | `test_parse_rejects_bad_header_crc`    |   ✅   | Parse rejects bad header crc                                                                |
|   5 | `test_parse_rejects_bad_data_crc`      |   ✅   | Parse rejects bad data crc                                                                  |
|   6 | `test_parse_needs_more_bytes`          |   ✅   | Parse needs more bytes                                                                      |
|   7 | `test_parse_rejects_over_length`       |   ✅   | A header claiming data_len 100 (> PC_ENOCEAN_MAX_DATA = 16) is rejected early.              |
|   8 | `test_parse_resynchronises_after_junk` |   ✅   | Parse resynchronises after junk                                                             |
|   9 | `test_build_bounds`                    |   ✅   | Build bounds                                                                                |
|  10 | `test_esp3_parse_null_guard`           |   ✅   | Esp3 parse null guard                                                                       |
|  11 | `test_parse_succeeds_with_null_out`    |   ✅   | A fully valid telegram is still framed correctly when the caller doesn't want the           |
|  12 | `test_build_rejects_null_out`          |   ✅   | Build rejects null out                                                                      |
|  13 | `test_erp1_parse`                      |   ✅   | A RPS (rocker switch) telegram: RORG 0xF6, 1 payload octet, sender 0x008B1234, status 0x30. |
|  14 | `test_erp1_build`                      |   ✅   | Build the RPS telegram from test_erp1_parse and check it byte-for-byte.                     |

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
|  13 | `test_build_frame_null_data_and_out_guards`  |   ✅   | out == nullptr is rejected regardless of other args.                                |
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

## test_udp_transport - native_udp_transport - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the UDP transport's multicast receive path (pc_udp_listen_multicast /_

|   # | Test                                                                  | Status | Description                                                                    |
| --: | :-------------------------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_join_records_the_group`                                         |   ✅   | Join records the group                                                         |
|   2 | `test_group_datagram_reaches_the_handler`                             |   ✅   | Group datagram reaches the handler                                             |
|   3 | `test_counts_repeated_announcements`                                  |   ✅   | The contention-counting use case: many announcements land on one joined group. |
|   4 | `test_rejects_non_multicast_group`                                    |   ✅   | A unicast address would bind but never deliver - fail loudly instead.          |
|   5 | `test_accepts_group_range_edges`                                      |   ✅   | Accepts group range edges                                                      |
|   6 | `test_rejects_malformed_group`                                        |   ✅   | Rejects malformed group                                                        |
|   7 | `test_leave_releases_the_slot`                                        |   ✅   | Leave releases the slot                                                        |
|   8 | `test_leave_ignores_a_plain_listener`                                 |   ✅   | A non-multicast listener on the same port must not be torn down by a leave.    |
|   9 | `test_listen_rebinds_existing_port`                                   |   ✅   | Listen rebinds existing port                                                   |
|  10 | `test_listen_evicts_slot_zero_when_pool_full`                         |   ✅   | Listen evicts slot zero when pool full                                         |
|  11 | `test_multicast_group_too_long_for_buffer_rejected`                   |   ✅   | Multicast group too long for buffer rejected                                   |
|  12 | `test_multicast_join_finds_slot_past_an_unrelated_listener`           |   ✅   | Multicast join finds slot past an unrelated listener                           |
|  13 | `test_multicast_rejoin_scans_past_a_freed_lower_slot`                 |   ✅   | Multicast rejoin scans past a freed lower slot                                 |
|  14 | `test_peer_addr_rejects_null_peer`                                    |   ✅   | Peer addr rejects null peer                                                    |
|  15 | `test_peer_addr_copies_and_tolerates_null_outparams`                  |   ✅   | Peer addr copies and tolerates null outparams                                  |
|  16 | `test_send_paths_are_captured`                                        |   ✅   | Send paths are captured                                                        |
|  17 | `test_capture_rejects_null_zero_and_oversized_payload`                |   ✅   | Capture rejects null zero and oversized payload                                |
|  18 | `test_inject_skips_a_listener_with_no_handler`                        |   ✅   | Inject skips a listener with no handler                                        |
|  19 | `test_inject_null_src_ip_becomes_empty_string`                        |   ✅   | Inject null src ip becomes empty string                                        |
|  20 | `test_multicast_lookup_skips_a_different_multicast_group`             |   ✅   | Multicast lookup skips a different multicast group                             |
|  21 | `test_peer_addr_tolerates_null_ip_out_and_zero_cap_and_null_port_out` |   ✅   | Peer addr tolerates null ip out and zero cap and null port out                 |

</details>

---

## test_sunspec - native_sunspec - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the SunSpec Modbus codec (services/energy/sunspec): the map writer, the_

|   # | Test                                                     | Status | Description                                                                              |
| --: | :------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_build_and_walk`                                    |   ✅   | Build and walk                                                                           |
|   2 | `test_two_models`                                        |   ✅   | Two models                                                                               |
|   3 | `test_string_point`                                      |   ✅   | String point                                                                             |
|   4 | `test_marker_and_truncation`                             |   ✅   | Marker and truncation                                                                    |
|   5 | `test_writer_overflow_fails_closed`                      |   ✅   | Writer overflow fails closed                                                             |
|   6 | `test_reader_guards_and_i32`                             |   ✅   | Reader guards and i32                                                                    |
|   7 | `test_writer_error_and_string_paths`                     |   ✅   | Writer error and string paths                                                            |
|   8 | `test_check_marker_null_and_short_and_begin_null_offset` |   ✅   | Check marker null and short and begin null offset                                        |
|   9 | `test_string_loop_boundary_exits`                        |   ✅   | No NUL anywhere in the field; the loop runs until i == avail (out_cap is not the limit). |
|  10 | `test_writer_two_step_short_circuit_failures`            |   ✅   | Writer two step short circuit failures                                                   |

</details>

---

## test_c37118 - native_c37118 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the IEEE C37.118.2 synchrophasor frame codec (services/energy/c37118): the_

|   # | Test                                     | Status | Description                                                       |
| --: | :--------------------------------------- | :----: | :---------------------------------------------------------------- |
|   1 | `test_crc_check_value`                   |   ✅   | Crc check value                                                   |
|   2 | `test_build_command_bytes`               |   ✅   | Build command bytes                                               |
|   3 | `test_command_round_trip`                |   ✅   | Command round trip                                                |
|   4 | `test_data_frame_payload`                |   ✅   | Data frame payload                                                |
|   5 | `test_decode_stat`                       |   ✅   | A data frame whose STAT word 0xFB63 exercises a mix of flags:     |
|   6 | `test_parse_rejects_bad`                 |   ✅   | A flipped payload bit must fail the CRC check.                    |
|   7 | `test_build_overflow_fails_closed`       |   ✅   | Build overflow fails closed                                       |
|   8 | `test_build_frame_null_and_zero_payload` |   ✅   | Null destination buffer.                                          |
|   9 | `test_build_frame_size_field_overflow`   |   ✅   | Build frame size field overflow                                   |
|  10 | `test_parse_frame_null_args`             |   ✅   | Parse frame null args                                             |
|  11 | `test_parse_frame_framesize_too_small`   |   ✅   | Spoof an under-sized FRAMESIZE field (big-endian, at octets 2-3). |
|  12 | `test_parse_command_edge_cases`          |   ✅   | Parse command edge cases                                          |

</details>

---

## test_dnp3 - native_dnp3 - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DNP3 (IEEE 1815) data-link frame codec (services/energy/dnp3): CRC-16/DNP,_

|   # | Test                               | Status | Description                                                                                         |
| --: | :--------------------------------- | :----: | :-------------------------------------------------------------------------------------------------- |
|   1 | `test_dnp3_parse_guards`           |   ✅   | Dnp3 parse guards                                                                                   |
|   2 | `test_crc_check_value`             |   ✅   | Crc check value                                                                                     |
|   3 | `test_build_header_bytes`          |   ✅   | 10 header + 3 data + 2 block CRC = 15                                                               |
|   4 | `test_round_trip_single_block`     |   ✅   | Round trip single block                                                                             |
|   5 | `test_round_trip_multi_block`      |   ✅   | Round trip multi block                                                                              |
|   6 | `test_header_only_frame`           |   ✅   | Header only frame                                                                                   |
|   7 | `test_parse_rejects_bad`           |   ✅   | A corrupted data octet fails the block CRC.                                                         |
|   8 | `test_build_overflow_fails_closed` |   ✅   | Build overflow fails closed                                                                         |
|   9 | `test_build_frame_null_guards`     |   ✅   | Build frame null guards                                                                             |
|  10 | `test_parse_frame_null_guards`     |   ✅   | Parse frame null guards                                                                             |
|  11 | `test_transport_header_and_build`  |   ✅   | Transport header and build                                                                          |
|  12 | `test_transport_single_and_multi`  |   ✅   | Single-frame fragment (FIR+FIN, seq 7).                                                             |
|  13 | `test_transport_errors`            |   ✅   | Transport errors                                                                                    |
|  14 | `test_app_request_roundtrip`       |   ✅   | A READ request: AC = FIR                                                                            | FIN, seq 3; FC READ; a small object header (group 1, var 0, qualifier 0x06). |
|  15 | `test_app_response_roundtrip`      |   ✅   | App response roundtrip                                                                              |
|  16 | `test_object_header_forms`         |   ✅   | Start-stop, 1-octet indexes: group 1 var 2 (binary inputs), 0..9 -> count 10, then 2 object octets. |
|  17 | `test_object_header_rejects`       |   ✅   | Too short for even the 3-octet header.                                                              |
|  18 | `test_build_object_header`         |   ✅   | 1-octet start-stop: group 1 var 2 (binary inputs), 0..9.                                            |
|  19 | `test_build_crob`                  |   ✅   | LATCH_ON, no trip/close, count 1, on/off time 0: control code = 0x03.                               |
|  20 | `test_build_aob`                   |   ✅   | g41v1: a 32-bit signed setpoint 12345 (0x3039) little-endian + a status octet (0 in a request).     |

</details>

---

## test_grpcweb - native_grpcweb - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the gRPC-Web message framing codec (services/iot/grpcweb): the message and_

|   # | Test                                          | Status | Description                                                                           |
| --: | :-------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_frame_message_bytes`                    |   ✅   | Frame message bytes                                                                   |
|   2 | `test_compressed_flag`                        |   ✅   | Compressed flag                                                                       |
|   3 | `test_trailer_frame`                          |   ✅   | Trailer frame                                                                         |
|   4 | `test_trailer_message`                        |   ✅   | A non-zero status (13 = INTERNAL) carrying a human-readable message.                  |
|   5 | `test_trailer_status_only`                    |   ✅   | Trailer status only                                                                   |
|   6 | `test_parse_stream`                           |   ✅   | frame 1: the message                                                                  |
|   7 | `test_parse_incomplete`                       |   ✅   | Parse incomplete                                                                      |
|   8 | `test_frame_overflow_fails_closed`            |   ✅   | Frame overflow fails closed                                                           |
|   9 | `test_frame_and_trailer_guards`               |   ✅   | Frame and trailer guards                                                              |
|  10 | `test_trailer_status_parse_paths`             |   ✅   | Trailer status parse paths                                                            |
|  11 | `test_frame_zero_length_body`                 |   ✅   | Frame zero length body                                                                |
|  12 | `test_frame_body_len_too_large`               |   ✅   | Frame body len too large                                                              |
|  13 | `test_trailer_frame_more_guards`              |   ✅   | Trailer frame more guards                                                             |
|  14 | `test_trailer_empty_message`                  |   ✅   | Trailer empty message                                                                 |
|  15 | `test_trailer_message_body_and_crlf_overflow` |   ✅   | After "grpc-status:0\r\n" (15) the prefix is at 20; "grpc-message:" (13) fits exactly |
|  16 | `test_parse_null_guards`                      |   ✅   | Parse null guards                                                                     |
|  17 | `test_trailer_status_multiline`               |   ✅   | Trailer status multiline                                                              |
|  18 | `test_trailer_status_digit_bounds`            |   ✅   | Trailer status digit bounds                                                           |
|  19 | `test_trailer_status_digit_loop_bounds`       |   ✅   | Trailer status digit loop bounds                                                      |
|  20 | `test_trailer_status_null_output`             |   ✅   | Trailer status null output                                                            |

</details>

---

## test_lwm2m_tlv - native_lwm2m_tlv - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the OMA LwM2M TLV codec (services/iot/lwm2m): the writer (raw + typed value_

|   # | Test                             | Status | Description                                                                      |
| --: | :------------------------------- | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_write_int_1byte`           |   ✅   | Write int 1byte                                                                  |
|   2 | `test_write_int_2byte`           |   ✅   | Write int 2byte                                                                  |
|   3 | `test_write_string_8bit_length`  |   ✅   | Write string 8bit length                                                         |
|   4 | `test_write_16bit_id`            |   ✅   | Write 16bit id                                                                   |
|   5 | `test_round_trip_and_value_int`  |   ✅   | Round trip and value int                                                         |
|   6 | `test_object_instance_nested`    |   ✅   | Object instance nested                                                           |
|   7 | `test_write_16bit_length`        |   ✅   | Write 16bit length                                                               |
|   8 | `test_read_24bit_length`         |   ✅   | Read 24bit length                                                                |
|   9 | `test_value_int_4_and_8_byte`    |   ✅   | Value int 4 and 8 byte                                                           |
|  10 | `test_zero_length_value`         |   ✅   | Zero length value                                                                |
|  11 | `test_overflow_and_malformed`    |   ✅   | Overflow and malformed                                                           |
|  12 | `test_write_error_paths`         |   ✅   | Write error paths                                                                |
|  13 | `test_write_float_roundtrip`     |   ✅   | Write float roundtrip                                                            |
|  14 | `test_read_id16_and_truncation`  |   ✅   | 16-bit-id resource: type 0xE1 (id16 flag + inline len 1), id 0x0405, value 0x07. |
|  15 | `test_write_bool_false`          |   ✅   | Write bool false                                                                 |
|  16 | `test_write_after_error_latched` |   ✅   | Write after error latched                                                        |
|  17 | `test_read_null_args`            |   ✅   | Read null args                                                                   |
|  18 | `test_value_int_null_args`       |   ✅   | Value int null args                                                              |

</details>

---

## test_fins - native_fins - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Omron FINS frame codec (services/fieldbus/fins): the command builder, the_

|   # | Test                             | Status | Description                                              |
| --: | :------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_build_command_bytes`       |   ✅   | Build command bytes                                      |
|   2 | `test_memory_area_read`          |   ✅   | area 0xB0 (DM), word 100 = 0x0064, bit 0, read 10 words. |
|   3 | `test_memory_area_write`         |   ✅   | area 0xB0 (DM), word 100 = 0x0064, bit 0, write 2 words. |
|   4 | `test_run_and_stop`              |   ✅   | RUN into MONITOR mode.                                   |
|   5 | `test_parse_command`             |   ✅   | Parse command                                            |
|   6 | `test_parse_response_ok`         |   ✅   | Parse response ok                                        |
|   7 | `test_parse_response_error`      |   ✅   | Parse response error                                     |
|   8 | `test_overflow_and_truncation`   |   ✅   | Overflow and truncation                                  |
|   9 | `test_build_command_zero_params` |   ✅   | Build command zero params                                |

</details>

---

## test_hostlink - native_hostlink - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Omron Host Link (C-mode) frame codec (services/fieldbus/hostlink): the FCS,_

|   # | Test                                  | Status | Description                                                                       |
| --: | :------------------------------------ | :----: | :-------------------------------------------------------------------------------- |
|   1 | `test_fcs_vector`                     |   ✅   | Fcs vector                                                                        |
|   2 | `test_build_dm_read`                  |   ✅   | Build dm read                                                                     |
|   3 | `test_build_read_and_extract`         |   ✅   | RD command: node 0, DM word 100 (-> "0100"), read 2 words (-> "0002").            |
|   4 | `test_build_write`                    |   ✅   | Build write                                                                       |
|   5 | `test_build_node_digits`              |   ✅   | Build node digits                                                                 |
|   6 | `test_round_trip`                     |   ✅   | Round trip                                                                        |
|   7 | `test_parse_response_end_code`        |   ✅   | Build a "response-shaped" frame: header RD, text = end code "00" + 4 data digits. |
|   8 | `test_parse_rejects_bad`              |   ✅   | Corrupt a text char -> FCS no longer matches.                                     |
|   9 | `test_build_overflow_fails_closed`    |   ✅   | Build overflow fails closed                                                       |
|  10 | `test_guards_and_hex`                 |   ✅   | build guards                                                                      |
|  11 | `test_build_fcs_hex_letter`           |   ✅   | Build fcs hex letter                                                              |
|  12 | `test_hex_val_lowercase_out_of_range` |   ✅   | Hex val lowercase out of range                                                    |
|  13 | `test_build_zero_length_text`         |   ✅   | Build zero length text                                                            |
|  14 | `test_build_empty_header_code`        |   ✅   | Build empty header code                                                           |
|  15 | `test_parse_null_pointers`            |   ✅   | Parse null pointers                                                               |
|  16 | `test_parse_bad_star_position`        |   ✅   | Parse bad star position                                                           |
|  17 | `test_parse_bad_start_char`           |   ✅   | Parse bad start char                                                              |
|  18 | `test_parse_node_field_bounds`        |   ✅   | Parse node field bounds                                                           |
|  19 | `test_parse_fcs_low_nibble_invalid`   |   ✅   | Parse fcs low nibble invalid                                                      |
|  20 | `test_end_code_low_nibble_invalid`    |   ✅   | End code low nibble invalid                                                       |
|  21 | `test_end_code_null_code_output`      |   ✅   | End code null code output                                                         |

</details>

---

## test_hislip - native_hislip - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the HiSLIP (IVI-6.1) message codec (services/instrumentation/hislip): the fixed 16-byte header_

|   # | Test                                     | Status | Description                                                                   |
| --: | :--------------------------------------- | :----: | :---------------------------------------------------------------------------- |
|   1 | `test_header_roundtrip`                  |   ✅   | Header roundtrip                                                              |
|   2 | `test_header_rejects`                    |   ✅   | Header rejects                                                                |
|   3 | `test_header_null_args`                  |   ✅   | build_header fails closed on a null buffer                                    |
|   4 | `test_message_type_codes`                |   ✅   | Message type codes                                                            |
|   5 | `test_build_initialize_vector`           |   ✅   | Build initialize vector                                                       |
|   6 | `test_parse_initialize`                  |   ✅   | Parse initialize                                                              |
|   7 | `test_parse_initialize_rejects`          |   ✅   | null output pointer                                                           |
|   8 | `test_initialize_response`               |   ✅   | Initialize response                                                           |
|   9 | `test_parse_initialize_response_rejects` |   ✅   | null output pointer                                                           |
|  10 | `test_async_initialize`                  |   ✅   | Async initialize                                                              |
|  11 | `test_build_dataend_vector`              |   ✅   | Build dataend vector                                                          |
|  12 | `test_data_roundtrip`                    |   ✅   | Data roundtrip                                                                |
|  13 | `test_message_id_increment`              |   ✅   | Message id increment                                                          |
|  14 | `test_build_overflow`                    |   ✅   | a 6-byte payload needs 22 bytes; a 20-byte buffer fails closed                |
|  15 | `test_build_with_payload_edge_cases`     |   ✅   | build_with_payload (via build_data) fails closed on a null destination buffer |

</details>

---

## test_vxi11 - native_vxi11 - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the VXI-11 codec over ONC RPC / XDR (services/instrumentation/vxi11): the record-marking header,_

|   # | Test                                            | Status | Description                                                                                     |
| --: | :---------------------------------------------- | :----: | :---------------------------------------------------------------------------------------------- |
|   1 | `test_record_mark`                              |   ✅   | Record mark                                                                                     |
|   2 | `test_create_link_vector`                       |   ✅   | Create link vector                                                                              |
|   3 | `test_create_link_reply`                        |   ✅   | Create link reply                                                                               |
|   4 | `test_getport`                                  |   ✅   | spot-check the call header: prog=portmapper, proc=GETPORT, and the mapping.prog word            |
|   5 | `test_device_write`                             |   ✅   | header(40) + record-mark(4) + lid,io,lock,flags (16) + opaque(len 4 + 6 data + 2 pad = 12) = 72 |
|   6 | `test_device_read`                              |   ✅   | Device read                                                                                     |
|   7 | `test_readstb_and_destroy`                      |   ✅   | Readstb and destroy                                                                             |
|   8 | `test_device_clear_and_trigger`                 |   ✅   | device_clear: proc 15, then Device_GenericParms (lid, flags, lock_timeout, io_timeout).         |
|   9 | `test_reply_rejects`                            |   ✅   | MSG_DENIED (reply_stat = 1)                                                                     |
|  10 | `test_error_str`                                |   ✅   | Error str                                                                                       |
|  11 | `test_build_overflow`                           |   ✅   | Build overflow                                                                                  |
|  12 | `test_record_mark_guards`                       |   ✅   | Record mark guards                                                                              |
|  13 | `test_reply_full_length_rejects`                |   ✅   | a COMPLETE header (so the XDR reader stays healthy) whose message type is CALL, not REPLY       |
|  14 | `test_reply_optional_outputs`                   |   ✅   | Reply optional outputs                                                                          |
|  15 | `test_getport_reject_paths`                     |   ✅   | accepted but the procedure did not run -> the results are not read                              |
|  16 | `test_create_link_lock_and_empty_device`        |   ✅   | lockDevice sits at record-mark(4) + header(40) + clientId(4) = offset 48                        |
|  17 | `test_opaque_overflows_after_a_good_header`     |   ✅   | 60 bytes hold the whole call header + the three fixed words, but not the device opaque          |
|  18 | `test_create_link_resp_reject_paths`            |   ✅   | Create link resp reject paths                                                                   |
|  19 | `test_device_write_empty_payload`               |   ✅   | a zero-length write is legal - the guard only rejects a null pointer WITH a length              |
|  20 | `test_write_resp_reject_paths`                  |   ✅   | Write resp reject paths                                                                         |
|  21 | `test_read_resp_reject_paths`                   |   ✅   | Read resp reject paths                                                                          |
|  22 | `test_readstb_and_error_resp_reject_paths`      |   ✅   | Readstb and error resp reject paths                                                             |
|  23 | `test_resp_parser_rejects_malformed_rpc_header` |   ✅   | Resp parser rejects malformed rpc header                                                        |
|  24 | `test_error_str_full_table`                     |   ✅   | Error str full table                                                                            |

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
|  28 | `test_svc_status_null_out_is_ignored`                      |   ✅   | A null status buffer must be a no-op, not a write through nullptr.                     |

</details>

---

## test_lsv2 - native_lsv2 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Heidenhain LSV/2 telegram codec (services/machine_tool/lsv2): the framer (4-byte big-endian_

|   # | Test                                    | Status | Description                                                                                 |
| --: | :-------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_build_no_payload`                 |   ✅   | R_ST with no payload -> exactly 8 bytes: 00 00 00 00 'R' '_' 'S' 'T'                        |
|   2 | `test_build_with_payload`               |   ✅   | Build with payload                                                                          |
|   3 | `test_build_run_info`                   |   ✅   | Build run info                                                                              |
|   4 | `test_build_login`                      |   ✅   | login "INSPECT", no password -> payload "INSPECT\0" (8 bytes)                               |
|   5 | `test_build_logout`                     |   ✅   | no login -> log out of everything -> empty payload, 8 bytes                                 |
|   6 | `test_build_filename`                   |   ✅   | R_FL "PGM.H" -> payload "PGM.H\0" (6 bytes)                                                 |
|   7 | `test_parse_ok`                         |   ✅   | Parse ok                                                                                    |
|   8 | `test_parse_error`                      |   ✅   | T_ER with a 2-byte error-class + error-code payload                                         |
|   9 | `test_parse_data_reply`                 |   ✅   | S_RI run-info reply carrying 3 payload bytes                                                |
|  10 | `test_parse_incomplete`                 |   ✅   | fewer than 8 header bytes -> false, and out is cleared                                      |
|  11 | `test_parse_stream_multi`               |   ✅   | two telegrams back-to-back: T_OK then S_RI(2 bytes)                                         |
|  12 | `test_roundtrip`                        |   ✅   | build then parse: run-info request survives a frame/parse round trip                        |
|  13 | `test_build_rejects_bad_args`           |   ✅   | Null destination / null mnemonic / a buffer that cannot even hold the header, a declared    |
|  14 | `test_build_login_guards_and_overflow`  |   ✅   | Null buffer / null login / a header-only buffer are refused, and a login (or password) that |
|  15 | `test_build_logout_and_filename_guards` |   ✅   | Build logout and filename guards                                                            |
|  16 | `test_parse_and_is_reject_null_args`    |   ✅   | A null out struct, a null input buffer, and null arguments to the mnemonic comparison are   |
|  17 | `test_error_payload_shape_is_enforced`  |   ✅   | pc_lsv2_error only reports on an error telegram carrying exactly the 2-byte class+code      |

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

## test_simatic - native_simatic - ✅ 36 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Siemens SIMATIC serial codec (services/fieldbus/simatic): 3964R block framing_

|   # | Test                                                    | Status | Description                                                                                   |
| --: | :------------------------------------------------------ | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_bcc_is_xor`                                       |   ✅   | Bcc is xor                                                                                    |
|   2 | `test_build_block_stuffs_dle_and_terminates`            |   ✅   | 0x41, DLE, DLE (doubled), 0x42, DLE, ETX, BCC                                                 |
|   3 | `test_block_round_trip_with_embedded_dle`               |   ✅   | Block round trip with embedded dle                                                            |
|   4 | `test_block_round_trip_no_bcc`                          |   ✅   | Block round trip no bcc                                                                       |
|   5 | `test_parse_rejects_bad`                                |   ✅   | bad BCC                                                                                       |
|   6 | `test_build_block_rejects_bad_args`                     |   ✅   | A null destination, and a null payload pointer with a non-zero length, are refused; a null    |
|   7 | `test_build_block_overflow_at_each_stage`               |   ✅   | Every write stage is capacity-checked independently: payload byte, the doubled DLE, the       |
|   8 | `test_parse_block_rejects_null_args`                    |   ✅   | All three pointers are mandatory; a missing one fails closed rather than writing anywhere.    |
|   9 | `test_parse_block_missing_bcc_and_doubled_dle_overflow` |   ✅   | R variant whose trailing BCC was truncated away: the terminator alone is not enough           |
|  10 | `test_sm_send_happy_path`                               |   ✅   | Sm send happy path                                                                            |
|  11 | `test_sm_receive_path_delivers`                         |   ✅   | Sm receive path delivers                                                                      |
|  12 | `test_sm_block_nak_retries`                             |   ✅   | Sm block nak retries                                                                          |
|  13 | `test_sm_qvz_timeout_then_abort`                        |   ✅   | Sm qvz timeout then abort                                                                     |
|  14 | `test_sm_priority_arbitration`                          |   ✅   | Low-priority station, mid-send, sees a partner STX -> yields to receive.                      |
|  15 | `test_sm_reply_from_rx_callback`                        |   ✅   | Sm reply from rx callback                                                                     |
|  16 | `test_sm_send_rejects_when_busy_or_unframeable`         |   ✅   | One job in flight at a time, and a payload that cannot be framed inside the block buffer is   |
|  17 | `test_sm_null_callbacks_are_safe`                       |   ✅   | tx/rx are optional: the link still runs the handshake and accepts a block, it just has        |
|  18 | `test_sm_receive_bad_bcc_naks`                          |   ✅   | A check-invalid block is NAKed and never delivered.                                           |
|  19 | `test_sm_receive_no_bcc_variant_delivers`               |   ✅   | Plain 3964 (no BCC): DLE ETX finalizes the block immediately, no trailing check byte.         |
|  20 | `test_sm_receive_illegal_control_naks`                  |   ✅   | DLE followed by something that is neither DLE nor ETX is a framing error mid-collect.         |
|  21 | `test_sm_receive_overflow_naks`                         |   ✅   | A partner that never terminates the block fills rxbuf; the next byte is rejected.             |
|  22 | `test_sm_idle_ignores_non_stx`                          |   ✅   | Line noise while idle must not open a receive.                                                |
|  23 | `test_sm_conn_nak_retries_then_gives_up`                |   ✅   | A partner that NAKs the connect gets MAX_CONN_RETRY fresh STXs, then the job is abandoned.    |
|  24 | `test_sm_await_conn_ignores_other_bytes`                |   ✅   | Neither DLE, STX nor NAK: nothing happens, we keep waiting for the connect.                   |
|  25 | `test_sm_await_end_ignores_noise_then_gives_up`         |   ✅   | In TX_AWAIT_END only DLE (done) and NAK (repeat) mean anything; MAX_BLOCK_RETRY rejections    |
|  26 | `test_sm_tick_before_deadline_is_a_noop`                |   ✅   | The QVZ timer must not fire early.                                                            |
|  27 | `test_sm_tick_block_timeout_retries_then_gives_up`      |   ✅   | No end DLE within QVZ repeats the block from STX, up to MAX_BLOCK_RETRY times.                |
|  28 | `test_sm_tick_zvz_aborts_receive`                       |   ✅   | A partner that stops mid-block trips the ZVZ inter-character timeout -> NAK, link freed.      |
|  29 | `test_sm_unknown_state_is_inert`                        |   ✅   | Defensive: a state byte outside the four defined states (a corrupted context) makes both      |
|  30 | `test_rk512_build_send_field_order`                     |   ✅   | Rk512 build send field order                                                                  |
|  31 | `test_rk512_build_fetch_and_parse`                      |   ✅   | Rk512 build fetch and parse                                                                   |
|  32 | `test_rk512_reaction_round_trip`                        |   ✅   | Rk512 reaction round trip                                                                     |
|  33 | `test_rk512_parse_rejects`                              |   ✅   | Rk512 parse rejects                                                                           |
|  34 | `test_rk512_build_guards`                               |   ✅   | Every builder fails closed on a null destination or a destination too small for its telegram. |
|  35 | `test_rk512_parse_header_guards`                        |   ✅   | Null arguments, an area code under the valid range, and a REACTION command byte are all       |
|  36 | `test_rk512_parse_reaction_guards_and_data`             |   ✅   | Null arguments / a short buffer / a non-REACTION command byte are refused; a FETCH response   |

</details>

---

## test_cotp - native_cotp - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the TPKT + COTP (X.224 class 0) frame codec (services/fieldbus/cotp): the TPKT_

|   # | Test                               | Status | Description                                                                                 |
| --: | :--------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_tpkt_bytes`                  |   ✅   | Tpkt bytes                                                                                  |
|   2 | `test_cotp_dt_bytes`               |   ✅   | Cotp dt bytes                                                                               |
|   3 | `test_cotp_cr_bytes`               |   ✅   | Cotp cr bytes                                                                               |
|   4 | `test_cotp_cr_with_tsaps`          |   ✅   | Cotp cr with tsaps                                                                          |
|   5 | `test_cotp_cc_bytes`               |   ✅   | CC echoing a client src-ref 0x0001 as the destination reference, this end's src-ref 0x0042. |
|   6 | `test_full_stack`                  |   ✅   | total = 4 (tpkt) + 3 (cotp dt) + 4 (s7) = 11                                                |
|   7 | `test_parse_rejects_bad`           |   ✅   | Parse rejects bad                                                                           |
|   8 | `test_guards_and_types`            |   ✅   | Guards and types                                                                            |
|   9 | `test_tpkt_build_edge_cases`       |   ✅   | Tpkt build edge cases                                                                       |
|  10 | `test_tpkt_parse_edge_cases`       |   ✅   | Tpkt parse edge cases                                                                       |
|  11 | `test_cotp_dt_edge_cases`          |   ✅   | Cotp dt edge cases                                                                          |
|  12 | `test_cotp_cr_after_li_overflow`   |   ✅   | Cotp cr after li overflow                                                                   |
|  13 | `test_cotp_parse_guard_edge_cases` |   ✅   | Cotp parse guard edge cases                                                                 |
|  14 | `test_cotp_parse_cc`               |   ✅   | Cotp parse cc                                                                               |

</details>

---

## test_s7comm - native_s7comm - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Siemens S7comm PDU codec (services/fieldbus/s7comm): the Setup Communication_

|   # | Test                                        | Status | Description                          |
| --: | :------------------------------------------ | :----: | :----------------------------------- |
|   1 | `test_build_setup`                          |   ✅   | Build setup                          |
|   2 | `test_build_read_request`                   |   ✅   | Build read request                   |
|   3 | `test_read_request_bit_address`             |   ✅   | Read request bit address             |
|   4 | `test_build_write_request`                  |   ✅   | Build write request                  |
|   5 | `test_parse_response_single`                |   ✅   | Parse response single                |
|   6 | `test_parse_response_padding`               |   ✅   | Parse response padding               |
|   7 | `test_parse_octet_and_error`                |   ✅   | Parse octet and error                |
|   8 | `test_parse_rejects_bad`                    |   ✅   | Parse rejects bad                    |
|   9 | `test_build_overflow_fails_closed`          |   ✅   | Build overflow fails closed          |
|  10 | `test_null_and_short_guards`                |   ✅   | Null and short guards                |
|  11 | `test_parse_header_null_out`                |   ✅   | Parse header null out                |
|  12 | `test_read_next_item_null_offset_and_out`   |   ✅   | Read next item null offset and out   |
|  13 | `test_read_next_item_bit_and_int_transport` |   ✅   | Read next item bit and int transport |
|  14 | `test_parse_response_even_length_not_last`  |   ✅   | Parse response even length not last  |

</details>

---

## test_melsec - native_melsec - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Mitsubishi MELSEC MC binary 3E codec (services/fieldbus/melsec): the batch-read_

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_build_read_bytes`                         |   ✅   | Build read bytes                         |
|   2 | `test_build_write_bytes`                        |   ✅   | Build write bytes                        |
|   3 | `test_head_device_24bit`                        |   ✅   | Head device 24bit                        |
|   4 | `test_parse_response_ok`                        |   ✅   | Parse response ok                        |
|   5 | `test_parse_response_error`                     |   ✅   | Parse response error                     |
|   6 | `test_parse_rejects_bad`                        |   ✅   | Parse rejects bad                        |
|   7 | `test_build_overflow_fails_closed`              |   ✅   | Build overflow fails closed              |
|   8 | `test_build_null_buf_fails_closed`              |   ✅   | Build null buf fails closed              |
|   9 | `test_parse_guards`                             |   ✅   | Parse guards                             |
|  10 | `test_parse_rejects_bad_second_subheader_octet` |   ✅   | Parse rejects bad second subheader octet |

</details>

---

## test_ads - native_ads - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Beckhoff ADS / AMS codec (services/fieldbus/ads): the request builders and the_

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_build_read_bytes`                   |   ✅   | Build read bytes                   |
|   2 | `test_parse_read_response`                |   ✅   | Parse read response                |
|   3 | `test_build_write`                        |   ✅   | Build write                        |
|   4 | `test_build_read_write_symbol`            |   ✅   | Build read write symbol            |
|   5 | `test_read_state_roundtrip`               |   ✅   | Read state roundtrip               |
|   6 | `test_parse_device_info`                  |   ✅   | Parse device info                  |
|   7 | `test_write_control_and_result`           |   ✅   | Write control and result           |
|   8 | `test_add_notification`                   |   ✅   | Add notification                   |
|   9 | `test_parse_notification_stream`          |   ✅   | Parse notification stream          |
|  10 | `test_build_overflow_fails_closed`        |   ✅   | Build overflow fails closed        |
|  11 | `test_parse_guards`                       |   ✅   | Parse guards                       |
|  12 | `test_build_read_device_info_and_del`     |   ✅   | Build read device info and del     |
|  13 | `test_build_null_and_small_buffer_guards` |   ✅   | Build null and small buffer guards |
|  14 | `test_build_write_control_variants`       |   ✅   | Build write control variants       |
|  15 | `test_parse_ams_header_more_guards`       |   ✅   | Parse ams header more guards       |
|  16 | `test_parse_payload_guards`               |   ✅   | Parse payload guards               |
|  17 | `test_parse_notification_guards`          |   ✅   | Parse notification guards          |

</details>

---

## test_focas - native_focas - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the FANUC FOCAS Ethernet codec (services/machine_tool/focas): the request builders and the_

|   # | Test                                        | Status | Description                                                                                |
| --: | :------------------------------------------ | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_build_open`                           |   ✅   | Build open                                                                                 |
|   2 | `test_build_close`                          |   ✅   | Build close                                                                                |
|   3 | `test_build_sysinfo`                        |   ✅   | Build sysinfo                                                                              |
|   4 | `test_build_read_position`                  |   ✅   | Build read position                                                                        |
|   5 | `test_build_read_param`                     |   ✅   | Build read param                                                                           |
|   6 | `test_build_request_extra`                  |   ✅   | Build request extra                                                                        |
|   7 | `test_parse_sysinfo_response`               |   ✅   | Parse sysinfo response                                                                     |
|   8 | `test_parse_alarm_and_status`               |   ✅   | Parse alarm and status                                                                     |
|   9 | `test_decode8_value`                        |   ✅   | 123.456 mm = 123456 / 10^3.                                                                |
|  10 | `test_build_overflow_fails_closed`          |   ✅   | Build overflow fails closed                                                                |
|  11 | `test_parse_guards`                         |   ✅   | Parse guards                                                                               |
|  12 | `test_build_remaining_selectors`            |   ✅   | Build remaining selectors                                                                  |
|  13 | `test_build_request_guards`                 |   ✅   | a declared extra length with no extra pointer                                              |
|  14 | `test_parse_frame_rejects_each_magic_octet` |   ✅   | All four magic octets are checked independently.                                           |
|  15 | `test_parser_null_and_short_guards`         |   ✅   | Every parser refuses a null input, a null out, and a buffer shorter than its fixed record. |
|  16 | `test_decode8_base_and_sentinel_edges`      |   ✅   | Only base 2 and base 10 are decimal-scaled, and the "no value" sentinel needs BOTH octets  |

</details>

---

## test_fanuc_j519 - native_fanuc_j519 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the FANUC Stream Motion (J519) UDP codec: byte-exact field placement against the_

|   # | Test                                             | Status | Description                                                     |
| --: | :----------------------------------------------- | :----: | :-------------------------------------------------------------- |
|   1 | `test_build_start_and_stop_exact_bytes`          |   ✅   | Build start and stop exact bytes                                |
|   2 | `test_peek_reads_type_and_version`               |   ✅   | Peek reads type and version                                     |
|   3 | `test_build_motion_exact_field_offsets`          |   ✅   | Build motion exact field offsets                                |
|   4 | `test_motion_roundtrip`                          |   ✅   | Motion roundtrip                                                |
|   5 | `test_build_status_exact_field_offsets`          |   ✅   | Build status exact field offsets                                |
|   6 | `test_status_roundtrip`                          |   ✅   | Status roundtrip                                                |
|   7 | `test_request_roundtrip_and_bytes`               |   ✅   | Request roundtrip and bytes                                     |
|   8 | `test_ack_roundtrip_and_table_offsets`           |   ✅   | Ack roundtrip and table offsets                                 |
|   9 | `test_shared_type_codes_are_separated_by_length` |   ✅   | an 8-octet Start must not parse as a Robot Status (both type 0) |
|  10 | `test_parsers_reject_wrong_type`                 |   ✅   | Parsers reject wrong type                                       |
|  11 | `test_parsers_reject_off_by_one_lengths`         |   ✅   | Parsers reject off by one lengths                               |
|  12 | `test_builders_reject_short_capacity`            |   ✅   | Builders reject short capacity                                  |
|  13 | `test_null_guards`                               |   ✅   | Null guards                                                     |
|  14 | `test_remaining_null_guard_branches`             |   ✅   | Remaining null guard branches                                   |

</details>

---

## test_pqc_mlkem - native_pqc - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Known-answer test for ML-KEM-768 (network_drivers/presentation/pqc/mlkem), the post-quantum half of_

|   # | Test                                                 | Status | Description                                                                               |
| --: | :--------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_mlkem768_encaps_kat`                           |   ✅   | Mlkem768 encaps kat                                                                       |
|   2 | `test_mlkem768_encaps_varies_with_m`                 |   ✅   | Mlkem768 encaps varies with m                                                             |
|   3 | `test_mlkem768_rejects_malformed_ek`                 |   ✅   | Mlkem768 rejects malformed ek                                                             |
|   4 | `test_mlkem768_keygen_kat`                           |   ✅   | Mlkem768 keygen kat                                                                       |
|   5 | `test_mlkem768_decaps_kat`                           |   ✅   | Mlkem768 decaps kat                                                                       |
|   6 | `test_mlkem768_roundtrip`                            |   ✅   | Mlkem768 roundtrip                                                                        |
|   7 | `test_mlkem768_decaps_implicit_reject`               |   ✅   | Mlkem768 decaps implicit reject                                                           |
|   8 | `test_mlkem768_ek_modulus_check_boundary`            |   ✅   | Coefficient 0 of the first polynomial is ByteDecode_12(ek[0], ek[1] low nibble); the high |
|   9 | `test_mlkem768_rejects_ek_last_coefficient`          |   ✅   | Mlkem768 rejects ek last coefficient                                                      |
|  10 | `test_mlkem768_implicit_reject_equals_j_of_z_and_ct` |   ✅   | Mlkem768 implicit reject equals j of z and ct                                             |

</details>

---

## test_pqc_sha3 - native_pqc - ✅ 4 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Known-answer tests for the Keccak/SHA-3/SHAKE primitive (network_drivers/presentation/pqc/sha3),_

|   # | Test                           | Status | Description             |
| --: | :----------------------------- | :----: | :---------------------- |
|   1 | `test_sha3_256`                |   ✅   | Sha3 256                |
|   2 | `test_sha3_512`                |   ✅   | Sha3 512                |
|   3 | `test_shake_empty`             |   ✅   | Shake empty             |
|   4 | `test_shake_stream_continuity` |   ✅   | Shake stream continuity |

</details>

---

## test_pqc_sntrup761 - native_pqc - ✅ 4 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Streamlined NTRU Prime sntrup761 KEM (network_drivers/presentation/pqc/sntrup761): the second PQC_

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_sntrup761_decaps_kat`                        |   ✅   | Sntrup761 decaps kat                        |
|   2 | `test_sntrup761_roundtrip`                         |   ✅   | Sntrup761 roundtrip                         |
|   3 | `test_sntrup761_implicit_reject`                   |   ✅   | Sntrup761 implicit reject                   |
|   4 | `test_sntrup761_keygen_retries_on_noninvertible_g` |   ✅   | Sntrup761 keygen retries on noninvertible g |

</details>

---

## test_iface_bridge - native_iface_bridge - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the interface-bridge pure core (services/net/iface_bridge): the address:port -> bus rule_

|   # | Test                                 | Status | Description                                                                                 |
| --: | :----------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_map_and_find`                  |   ✅   | Map and find                                                                                |
|   2 | `test_any_interface_and_dedup`       |   ✅   | Any interface and dedup                                                                     |
|   3 | `test_bad_address_rejected`          |   ✅   | Bad address rejected                                                                        |
|   4 | `test_table_full`                    |   ✅   | Table full                                                                                  |
|   5 | `test_txn_roundtrip`                 |   ✅   | Txn roundtrip                                                                               |
|   6 | `test_txn_partial_and_readonly`      |   ✅   | Partial header (< 4 bytes) -> need more.                                                    |
|   7 | `test_build_overflow_fails_closed`   |   ✅   | Build overflow fails closed                                                                 |
|   8 | `test_null_arg_guards`               |   ✅   | add() with a NULL rule pointer fails closed and touches nothing.                            |
|   9 | `test_map_empty_ip_is_any_interface` |   ✅   | A non-NULL but empty ip string is treated the same as NULL: "any interface".                |
|  10 | `test_txn_parse_null_outputs`        |   ✅   | A complete frame parsed with every output pointer NULL: the caller can probe "is this frame |
|  11 | `test_txn_build_edge_cases`          |   ✅   | out == NULL fails closed regardless of cap.                                                 |

</details>

---

## test_rtcm3 - native_rtcm3 - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the RTCM 3.x pure codec (services/timing_position/gnss/rtcm3): CRC-24Q, MSB-first bit I/O, the_

|   # | Test                                           | Status | Description                                                                                           |
| --: | :--------------------------------------------- | :----: | :---------------------------------------------------------------------------------------------------- |
|   1 | `test_writer_rejects_bad_widths_and_is_sticky` |   ✅   | Writer rejects bad widths and is sticky                                                               |
|   2 | `test_signed_bit_io_full_width`                |   ✅   | Signed bit io full width                                                                              |
|   3 | `test_frame_parse_edges`                       |   ✅   | Frame parse edges                                                                                     |
|   4 | `test_frame_build_edges`                       |   ✅   | Frame build edges                                                                                     |
|   5 | `test_parse_1005_rejects_bad_input`            |   ✅   | Parse 1005 rejects bad input                                                                          |
|   6 | `test_build_1005_matches_pyrtcm`               |   ✅   | Build 1005 matches pyrtcm                                                                             |
|   7 | `test_build_1006_matches_pyrtcm`               |   ✅   | Build 1006 matches pyrtcm                                                                             |
|   8 | `test_parse_frame_and_1005`                    |   ✅   | Parse frame and 1005                                                                                  |
|   9 | `test_parse_frame_and_1006`                    |   ✅   | Parse frame and 1006                                                                                  |
|  10 | `test_crc24q_matches_frame`                    |   ✅   | The 3 trailing CRC bytes are CRC-24Q over the preamble + header + payload (all but the last 3 bytes). |
|  11 | `test_crc_detects_corruption`                  |   ✅   | Crc detects corruption                                                                                |
|  12 | `test_partial_frame_needs_more`                |   ✅   | Partial frame needs more                                                                              |
|  13 | `test_sync_finds_preamble`                     |   ✅   | Sync finds preamble                                                                                   |
|  14 | `test_bit_io_roundtrip`                        |   ✅   | Bit io roundtrip                                                                                      |
|  15 | `test_writer_overflow_fails_closed`            |   ✅   | Writer overflow fails closed                                                                          |
|  16 | `test_frame_build_roundtrip`                   |   ✅   | Frame build roundtrip                                                                                 |

</details>

---

## test_gnss_survey - native_gnss_survey - ✅ 25 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the GNSS survey-in core (services/timing_position/gnss/pc_gnss_survey): the WGS84 geodetic->ECEF_

|   # | Test                                            | Status | Description                                          |
| --: | :---------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_geodetic_to_ecef_matches_pyproj`          |   ✅   | Geodetic to ecef matches pyproj                      |
|   2 | `test_ecef_to_geodetic_roundtrip`               |   ✅   | Ecef to geodetic roundtrip                           |
|   3 | `test_m_to_01mm_rounds_half_away`               |   ✅   | M to 01mm rounds half away                           |
|   4 | `test_survey_single_fix_matches_reference`      |   ✅   | Survey single fix matches reference                  |
|   5 | `test_survey_averages_out_scatter`              |   ✅   | Survey averages out scatter                          |
|   6 | `test_survey_accuracy_clamps_negative_variance` |   ✅   | Survey accuracy clamps negative variance             |
|   7 | `test_survey_empty_has_no_mean`                 |   ✅   | Survey empty has no mean                             |
|   8 | `test_gga_to_geodetic`                          |   ✅   | Gga to geodetic                                      |
|   9 | `test_gga_no_fix_rejected`                      |   ✅   | Fix quality field (index 6) = 0 -> no fix -> reject. |
|  10 | `test_survey_add_gga_folds_fix`                 |   ✅   | Survey add gga folds fix                             |
|  11 | `test_ecef_to_geodetic_north_pole`              |   ✅   | Ecef to geodetic north pole                          |
|  12 | `test_ecef_to_geodetic_south_pole`              |   ✅   | Ecef to geodetic south pole                          |
|  13 | `test_gga_empty_lat_rejected`                   |   ✅   | Gga empty lat rejected                               |
|  14 | `test_gga_nonnumeric_lat_rejected`              |   ✅   | Gga nonnumeric lat rejected                          |
|  15 | `test_gga_null_lat_field_rejected`              |   ✅   | Gga null lat field rejected                          |
|  16 | `test_gga_empty_lon_rejected`                   |   ✅   | Gga empty lon rejected                               |
|  17 | `test_gga_empty_quality_rejected`               |   ✅   | Gga empty quality rejected                           |
|  18 | `test_gga_empty_altitude_rejected`              |   ✅   | Gga empty altitude rejected                          |
|  19 | `test_gga_too_few_fields_rejected`              |   ✅   | Gga too few fields rejected                          |
|  20 | `test_gga_southern_eastern_hemisphere`          |   ✅   | Gga southern eastern hemisphere                      |
|  21 | `test_gga_lowercase_hemispheres`                |   ✅   | Gga lowercase hemispheres                            |
|  22 | `test_gga_empty_hemisphere_fields_not_negated`  |   ✅   | Gga empty hemisphere fields not negated              |
|  23 | `test_gga_geoid_absent_defaults_zero`           |   ✅   | Gga geoid absent defaults zero                       |
|  24 | `test_gga_bad_args_and_types_rejected`          |   ✅   | Gga bad args and types rejected                      |
|  25 | `test_survey_add_gga_rejects_bad_fix`           |   ✅   | Survey add gga rejects bad fix                       |

</details>

---

## test_bacnet - native_bacnet - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the BACnet/IP BVLC + NPDU codec (services/fieldbus/bacnet): the BVLC envelope and_

|   # | Test                                          | Status | Description                                                                                            |
| --: | :-------------------------------------------- | :----: | :----------------------------------------------------------------------------------------------------- |
|   1 | `test_bacnet_guards_and_truncations`          |   ✅   | Bacnet guards and truncations                                                                          |
|   2 | `test_bvlc_bytes`                             |   ✅   | Bvlc bytes                                                                                             |
|   3 | `test_npdu_local`                             |   ✅   | Npdu local                                                                                             |
|   4 | `test_npdu_dest`                              |   ✅   | Npdu dest                                                                                              |
|   5 | `test_npdu_broadcast`                         |   ✅   | Npdu broadcast                                                                                         |
|   6 | `test_npdu_parse_with_source`                 |   ✅   | Npdu parse with source                                                                                 |
|   7 | `test_full_stack`                             |   ✅   | Full stack                                                                                             |
|   8 | `test_parse_rejects_bad`                      |   ✅   | Parse rejects bad                                                                                      |
|   9 | `test_overflow_fails_closed`                  |   ✅   | Overflow fails closed                                                                                  |
|  10 | `test_bvlc_build_zero_len_and_giant_overflow` |   ✅   | Bvlc build zero len and giant overflow                                                                 |
|  11 | `test_bvlc_parse_edge_branches`               |   ✅   | Bvlc parse edge branches                                                                               |
|  12 | `test_npdu_build_zero_apdu_and_null_dadr`     |   ✅   | Npdu build zero apdu and null dadr                                                                     |
|  13 | `test_npdu_parse_null_buf_out_and_short`      |   ✅   | Npdu parse null buf out and short                                                                      |
|  14 | `test_apdu_parse`                             |   ✅   | Confirmed-Request ReadProperty (service 12): type/flags, max octet, invoke id 1, service choice, data. |
|  15 | `test_apdu_build_who_is`                      |   ✅   | Unbounded Who-Is (no limits): the 2-octet form every device answers.                                   |
|  16 | `test_apdu_build_i_am`                        |   ✅   | I-Am for Device 260, max APDU 1476, no-segmentation (3), vendor 42.                                    |
|  17 | `test_apdu_build_read_property`               |   ✅   | ReadProperty: invoke 1, max-resp 0x05, Analog Input 5, present-value (85).                             |

</details>

---

## test_enip - native_enip - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the EtherNet/IP encapsulation codec (services/fieldbus/enip): the header, the_

|   # | Test                               | Status | Description                                                     |
| --: | :--------------------------------- | :----: | :-------------------------------------------------------------- |
|   1 | `test_header_round_trip`           |   ✅   | Header round trip                                               |
|   2 | `test_register_session`            |   ✅   | Register session                                                |
|   3 | `test_unregister_session`          |   ✅   | Unregister session                                              |
|   4 | `test_send_rr_data_bytes`          |   ✅   | Send rr data bytes                                              |
|   5 | `test_send_rr_data_round_trip`     |   ✅   | Send rr data round trip                                         |
|   6 | `test_list_identity`               |   ✅   | Request: a header-only ListIdentity (command 0x0063, length 0). |
|   7 | `test_parse_rejects_bad`           |   ✅   | Parse rejects bad                                               |
|   8 | `test_build_overflow_fails_closed` |   ✅   | Build overflow fails closed                                     |
|   9 | `test_build_and_parse_guards`      |   ✅   | Build and parse guards                                          |
|  10 | `test_more_branch_coverage`        |   ✅   | More branch coverage                                            |

</details>

---

## test_amqp - native_amqp - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the AMQP 0-9-1 frame codec (services/iot/amqp): the protocol header, the frame_

|   # | Test                                                 | Status | Description                                                                     |
| --: | :--------------------------------------------------- | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_protocol_header`                               |   ✅   | Protocol header                                                                 |
|   2 | `test_build_method_bytes`                            |   ✅   | Build method bytes                                                              |
|   3 | `test_method_round_trip`                             |   ✅   | Method round trip                                                               |
|   4 | `test_heartbeat`                                     |   ✅   | Heartbeat                                                                       |
|   5 | `test_content_header`                                |   ✅   | Content header for a Basic (class 60) publish of a 5-octet body, no properties. |
|   6 | `test_parse_stream`                                  |   ✅   | Parse stream                                                                    |
|   7 | `test_parse_rejects_bad`                             |   ✅   | A frame whose end octet is not 0xCE.                                            |
|   8 | `test_build_overflow_fails_closed`                   |   ✅   | Build overflow fails closed                                                     |
|   9 | `test_build_and_parse_guards`                        |   ✅   | Build and parse guards                                                          |
|  10 | `test_protocol_header_null_buf`                      |   ✅   | Protocol header null buf                                                        |
|  11 | `test_build_frame_with_payload_round_trip`           |   ✅   | Build frame with payload round trip                                             |
|  12 | `test_build_frame_payload_len_overflow_fails_closed` |   ✅   | Build frame payload len overflow fails closed                                   |
|  13 | `test_build_method_guards`                           |   ✅   | Build method guards                                                             |
|  14 | `test_parse_frame_optional_out_params`               |   ✅   | Parse frame optional out params                                                 |
|  15 | `test_parse_method_optional_out_params`              |   ✅   | Parse method optional out params                                                |

</details>

---

## test_cip - native_cip - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the CIP message codec (services/fieldbus/cip): the EPATH builder, the request_

|   # | Test                                    | Status | Description                      |
| --: | :-------------------------------------- | :----: | :------------------------------- |
|   1 | `test_cip_build_guards`                 |   ✅   | Cip build guards                 |
|   2 | `test_epath_8bit`                       |   ✅   | Epath 8bit                       |
|   3 | `test_epath_16bit`                      |   ✅   | Epath 16bit                      |
|   4 | `test_get_attr_single`                  |   ✅   | Get attr single                  |
|   5 | `test_get_attr_all`                     |   ✅   | Get attr all                     |
|   6 | `test_set_attr_single`                  |   ✅   | Set attr single                  |
|   7 | `test_build_request_with_data`          |   ✅   | Build request with data          |
|   8 | `test_parse_response_ok`                |   ✅   | Parse response ok                |
|   9 | `test_parse_response_additional_status` |   ✅   | Parse response additional status |
|  10 | `test_parse_response_error`             |   ✅   | Parse response error             |
|  11 | `test_parse_response_null_guards`       |   ✅   | Parse response null guards       |
|  12 | `test_rejects_bad`                      |   ✅   | Rejects bad                      |

</details>

---

## test_nats - native_nats - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the NATS client protocol codec (services/iot/nats): the CONNECT/PUB/SUB/UNSUB/_

|   # | Test                               | Status | Description                                                                    |
| --: | :--------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_build_connect`               |   ✅   | Build connect                                                                  |
|   2 | `test_build_ping_pong`             |   ✅   | Build ping pong                                                                |
|   3 | `test_build_null_args`             |   ✅   | Build null args                                                                |
|   4 | `test_build_overflow_put_ch`       |   ✅   | cap 6: "PUB " fits, "foo" overflows in put_str -> ok=false, then put_ch bails. |
|   5 | `test_parse_edges`                 |   ✅   | Parse edges                                                                    |
|   6 | `test_build_pub`                   |   ✅   | Build pub                                                                      |
|   7 | `test_build_pub_with_reply`        |   ✅   | Build pub with reply                                                           |
|   8 | `test_build_pub_empty_payload`     |   ✅   | Build pub empty payload                                                        |
|   9 | `test_build_sub_and_unsub`         |   ✅   | Build sub and unsub                                                            |
|  10 | `test_parse_msg`                   |   ✅   | Parse msg                                                                      |
|  11 | `test_parse_msg_with_reply`        |   ✅   | Parse msg with reply                                                           |
|  12 | `test_build_hpub`                  |   ✅   | Build hpub                                                                     |
|  13 | `test_parse_hmsg`                  |   ✅   | Parse hmsg                                                                     |
|  14 | `test_parse_control_lines`         |   ✅   | Parse control lines                                                            |
|  15 | `test_parse_incomplete`            |   ✅   | Parse incomplete                                                               |
|  16 | `test_build_overflow_fails_closed` |   ✅   | Build overflow fails closed                                                    |

</details>

---

## test_sparkplug - native_sparkplug - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Sparkplug B codec (services/iot/sparkplug): the topic builder, the Metric_

|   # | Test                              | Status | Description                                          |
| --: | :-------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_spb_error_and_kind_paths`   |   ✅   | Spb error and kind paths                             |
|   2 | `test_decode_payload_and_metrics` |   ✅   | Decode payload and metrics                           |
|   3 | `test_topic`                      |   ✅   | Topic                                                |
|   4 | `test_metric_bytes`               |   ✅   | Metric bytes                                         |
|   5 | `test_payload_round_trip`         |   ✅   | Payload round trip                                   |
|   6 | `test_metric_int_and_string`      |   ✅   | skip name + datatype, read the int value (field 10). |
|   7 | `test_metric_alias`               |   ✅   | Metric alias                                         |
|   8 | `test_overflow_fails_closed`      |   ✅   | Overflow fails closed                                |
|   9 | `test_spb_more_branch_coverage`   |   ✅   | Spb more branch coverage                             |

</details>

---

## test_modbus_master - native_modbus_master - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Modbus master codec (services/fieldbus/modbus/pc_modbus_master): request_

|   # | Test                                     | Status | Description                                                                     |
| --: | :--------------------------------------- | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_build_read_bytes`                  |   ✅   | Build read bytes                                                                |
|   2 | `test_build_rejects_bad_args`            |   ✅   | Build rejects bad args                                                          |
|   3 | `test_round_trip_holding_regs`           |   ✅   | Round trip holding regs                                                         |
|   4 | `test_round_trip_exception`              |   ✅   | Read a wildly out-of-range address: the slave returns an exception ADU.         |
|   5 | `test_parse_short_frame_fails`           |   ✅   | Parse short frame fails                                                         |
|   6 | `test_build_null_out_and_input_fc`       |   ✅   | Build null out and input fc                                                     |
|   7 | `test_parse_null_adu`                    |   ✅   | Parse null adu                                                                  |
|   8 | `test_parse_bad_protocol_id`             |   ✅   | Parse bad protocol id                                                           |
|   9 | `test_parse_unexpected_function`         |   ✅   | Parse unexpected function                                                       |
|  10 | `test_parse_exception_null_out`          |   ✅   | Parse exception null out                                                        |
|  11 | `test_parse_bad_byte_count`              |   ✅   | Parse bad byte count                                                            |
|  12 | `test_parse_max_regs_and_null_out`       |   ✅   | A 4-register response (byte count 8), len = 9 + 8 = 17.                         |
|  13 | `test_parse_accepts_input_regs_function` |   ✅   | Parse accepts input regs function                                               |
|  14 | `test_build_write_single_bytes`          |   ✅   | Build write single bytes                                                        |
|  15 | `test_round_trip_write_single`           |   ✅   | Round trip write single                                                         |
|  16 | `test_build_write_multiple_bytes`        |   ✅   | Build write multiple bytes                                                      |
|  17 | `test_round_trip_write_multiple`         |   ✅   | Round trip write multiple                                                       |
|  18 | `test_build_write_rejects_bad_args`      |   ✅   | Build write rejects bad args                                                    |
|  19 | `test_parse_write_response_edges`        |   ✅   | Exception reply (FC 0x06                                                        | 0x80, code 2) -> 0 written, exception set. |
|  20 | `test_round_trip_read_coils`             |   ✅   | Round trip read coils                                                           |
|  21 | `test_round_trip_read_discrete_inputs`   |   ✅   | Round trip read discrete inputs                                                 |
|  22 | `test_round_trip_write_single_coil`      |   ✅   | Round trip write single coil                                                    |
|  23 | `test_round_trip_write_multiple_coils`   |   ✅   | Clear then write an alternating pattern across a byte boundary.                 |
|  24 | `test_bit_build_and_parse_guards`        |   ✅   | build_read_bits rejects a non-bit FC, an out-of-range count, and a null buffer. |
|  25 | `test_round_trip_mask_write`             |   ✅   | Round trip mask write                                                           |
|  26 | `test_round_trip_read_write_multiple`    |   ✅   | Round trip read write multiple                                                  |
|  27 | `test_fc16_17_guards`                    |   ✅   | Mask-write build guards.                                                        |

</details>

---

## test_ota_rollback - native_ota_rollback - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the OTA rollback decision (services/system/ota_rollback). The esp_ota_

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

## test_totp - native_totp - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for TOTP (services/security/totp): the RFC 6238 Appendix B test vectors_

|   # | Test                                      | Status | Description                                                                  |
| --: | :---------------------------------------- | :----: | :--------------------------------------------------------------------------- |
|   1 | `test_rfc6238_vectors`                    |   ✅   | RFC 6238 Appendix B (SHA-1, T0=0, step=30, digits=8).                        |
|   2 | `test_verify_window`                      |   ✅   | Verify window                                                                |
|   3 | `test_base32_decode`                      |   ✅   | Base32 decode                                                                |
|   4 | `test_base32_rejects_invalid`             |   ✅   | Base32 rejects invalid                                                       |
|   5 | `test_long_key_default_period_and_base32` |   ✅   | Long key default period and base32                                           |
|   6 | `test_verify_period_zero_default`         |   ✅   | pc_totp_verify's period == 0 branch defaults to 30, same as pc_totp's.       |
|   7 | `test_verify_window_skips_negative_step`  |   ✅   | At unix_time 0 (step 0) with window 1, the w=-1 candidate step is negative   |
|   8 | `test_base32_decode_null_args`            |   ✅   | Base32 decode null args                                                      |
|   9 | `test_base32_decode_rejects_char_above_z` |   ✅   | '~' (0x7E) is >= 'a' but > 'z', exercising the else-if's upper-bound branch. |

</details>

---

## test_radio_power - native_radio_power - ✅ 3 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the radio-power mode names (services/system/radio_power). Applying the_

|   # | Test                                     | Status | Description                                                                    |
| --: | :--------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_ps_names`                          |   ✅   | Ps names                                                                       |
|   2 | `test_apply_is_noop_on_host`             |   ✅   | Apply is noop on host                                                          |
|   3 | `test_busy_hold_release_is_noop_on_host` |   ✅   | Bulk-transfer keep-awake refcount is ESP32-only; on host both calls are no-ops |

</details>

---

## test_dns_resolver - native_dns_resolver - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the DNS answer classifier / verifier (services/net/dns_resolver)._

|   # | Test                               | Status | Description                                                                     |
| --: | :--------------------------------- | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_classify`                    |   ✅   | Classify                                                                        |
|   2 | `test_verify_rejects_suspicious`   |   ✅   | Verify rejects suspicious                                                       |
|   3 | `test_verify_accepts_plausible`    |   ✅   | Verify accepts plausible                                                        |
|   4 | `test_resolve_is_noop_on_host`     |   ✅   | Resolve is noop on host                                                         |
|   5 | `test_resolve_verified_paths`      |   ✅   | resolve fails -> false.                                                         |
|   6 | `test_resolve_host_ok_null_out_ip` |   ✅   | Call pc_dns_resolver_resolve() (the host stub) directly - not via the _verified |

</details>

---

## test_espnow - native_espnow - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the ESP-NOW host-testable core (services/radio/espnow): the typed_

|   # | Test                                                | Status | Description                                                          |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------- |
|   1 | `test_encode_decode_roundtrip`                      |   ✅   | Encode decode roundtrip                                              |
|   2 | `test_encode_zero_length`                           |   ✅   | Encode zero length                                                   |
|   3 | `test_encode_rejects_oversize_and_small_buffer`     |   ✅   | Encode rejects oversize and small buffer                             |
|   4 | `test_decode_rejects_corrupt`                       |   ✅   | bad magic                                                            |
|   5 | `test_encode_null_out_and_null_payload_nonzero_len` |   ✅   | null out buffer is rejected regardless of otherwise-valid arguments. |
|   6 | `test_decode_null_buf_and_null_out_params`          |   ✅   | Decode null buf and null out params                                  |
|   7 | `test_peer_has_and_remove_reject_null_mac`          |   ✅   | Peer has and remove reject null mac                                  |
|   8 | `test_peer_registry`                                |   ✅   | Peer registry                                                        |
|   9 | `test_peer_table_full_fails_closed`                 |   ✅   | Peer table full fails closed                                         |
|  10 | `test_broadcast_address`                            |   ✅   | Broadcast address                                                    |
|  11 | `test_peer_guard_and_host_stubs`                    |   ✅   | Peer guard and host stubs                                            |

</details>

---

## test_opcua - native_opcua - ✅ 71 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for OPC UA (services/fieldbus/opcua): the Binary built-in type codec (incl._

|   # | Test                                                             | Status | Description                                                                                          |
| --: | :--------------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------------- |
|   1 | `test_w_string_positive_len_null_pointer`                        |   ✅   | W string positive len null pointer                                                                   |
|   2 | `test_r_string_optional_len_zero_cap_and_frame_underrun`         |   ✅   | R string optional len zero cap and frame underrun                                                    |
|   3 | `test_w_nodeid_numeric_widens_for_large_identifier`              |   ✅   | W nodeid numeric widens for large identifier                                                         |
|   4 | `test_r_nodeid_guid_truncated_latches_error`                     |   ✅   | R nodeid guid truncated latches error                                                                |
|   5 | `test_r_nodeid_null_namespace_uri_and_server_index_flags`        |   ✅   | R nodeid null namespace uri and server index flags                                                   |
|   6 | `test_parsers_reject_frame_shorter_than_header`                  |   ✅   | Parsers reject frame shorter than header                                                             |
|   7 | `test_parse_hello_rejects_consistent_but_undersized_frame`       |   ✅   | Parse hello rejects consistent but undersized frame                                                  |
|   8 | `test_ack_negotiation_clamps_oversized_client_request`           |   ✅   | Client offers far more than PC_OPCUA_BUF on every axis -> every field clamps to the server's.        |
|   9 | `test_parse_msg_string_typeid_and_empty_extension_body`          |   ✅   | Parse msg string typeid and empty extension body                                                     |
|  10 | `test_parse_open_rejects_non_numeric_and_wrong_namespace_typeid` |   ✅   | Parse open rejects non numeric and wrong namespace typeid                                            |
|  11 | `test_builders_reject_null_output_buffer`                        |   ✅   | Builders reject null output buffer                                                                   |
|  12 | `test_endpoint_description_falls_back_per_field`                 |   ✅   | Endpoint description falls back per field                                                            |
|  13 | `test_read_response_without_values_or_statuses`                  |   ✅   | Read response without values or statuses                                                             |
|  14 | `test_variant_null_string_roundtrip`                             |   ✅   | Variant null string roundtrip                                                                        |
|  15 | `test_datavalue_status_only_with_and_without_status_sink`        |   ✅   | Datavalue status only with and without status sink                                                   |
|  16 | `test_parse_captures_at_most_the_compiled_maximum`               |   ✅   | Parse captures at most the compiled maximum                                                          |
|  17 | `test_parse_browse_and_write_cap_captured_items`                 |   ✅   | Parse browse and write cap captured items                                                            |
|  18 | `test_write_response_without_results_array`                      |   ✅   | Write response without results array                                                                 |
|  19 | `test_browse_response_reference_without_names`                   |   ✅   | Browse response reference without names                                                              |
|  20 | `test_localizedtext_every_field_combination`                     |   ✅   | Both fields present: mask 0x03, Locale then Text.                                                    |
|  21 | `test_w_reference_null_fails_writer_closed`                      |   ✅   | W reference null fails writer closed                                                                 |
|  22 | `test_browse_response_without_a_resolver`                        |   ✅   | Browse response without a resolver                                                                   |
|  23 | `test_parse_read_optional_fields`                                |   ✅   | Parse read optional fields                                                                           |
|  24 | `test_parse_rejections`                                          |   ✅   | Parse rejections                                                                                     |
|  25 | `test_build_guards_and_overflow`                                 |   ✅   | Build guards and overflow                                                                            |
|  26 | `test_setters_and_endpoint_url`                                  |   ✅   | Setters and endpoint url                                                                             |
|  27 | `test_variant_scalar_types`                                      |   ✅   | Variant scalar types                                                                                 |
|  28 | `test_variant_errors`                                            |   ✅   | Variant errors                                                                                       |
|  29 | `test_datavalue_all_masks`                                       |   ✅   | Datavalue all masks                                                                                  |
|  30 | `test_nodeid_encodings`                                          |   ✅   | Nodeid encodings                                                                                     |
|  31 | `test_reader_underruns`                                          |   ✅   | Reader underruns                                                                                     |
|  32 | `test_codec_roundtrip`                                           |   ✅   | Codec roundtrip                                                                                      |
|  33 | `test_string_null_roundtrip`                                     |   ✅   | String null roundtrip                                                                                |
|  34 | `test_reader_underrun_latches`                                   |   ✅   | Reader underrun latches                                                                              |
|  35 | `test_writer_overflow_fails_closed`                              |   ✅   | Writer overflow fails closed                                                                         |
|  36 | `test_parse_header`                                              |   ✅   | Parse header                                                                                         |
|  37 | `test_parse_hello`                                               |   ✅   | Parse hello                                                                                          |
|  38 | `test_parse_hello_rejects_short`                                 |   ✅   | Parse hello rejects short                                                                            |
|  39 | `test_build_ack_negotiates`                                      |   ✅   | Build ack negotiates                                                                                 |
|  40 | `test_build_error`                                               |   ✅   | Build error                                                                                          |
|  41 | `test_nodeid_roundtrip`                                          |   ✅   | Nodeid roundtrip                                                                                     |
|  42 | `test_filetime_from_unix`                                        |   ✅   | Filetime from unix                                                                                   |
|  43 | `test_parse_open`                                                |   ✅   | Parse open                                                                                           |
|  44 | `test_parse_open_rejects_wrong_type`                             |   ✅   | Corrupt the message type so it is no longer "OPN".                                                   |
|  45 | `test_build_open_response`                                       |   ✅   | Build open response                                                                                  |
|  46 | `test_parse_msg`                                                 |   ✅   | Parse msg                                                                                            |
|  47 | `test_parse_msg_rejects_non_msg`                                 |   ✅   | Parse msg rejects non msg                                                                            |
|  48 | `test_build_create_session_response`                             |   ✅   | Build create session response                                                                        |
|  49 | `test_build_activate_session_response`                           |   ✅   | Build activate session response                                                                      |
|  50 | `test_datavalue_good_int32`                                      |   ✅   | Datavalue good int32                                                                                 |
|  51 | `test_datavalue_bad_status`                                      |   ✅   | Datavalue bad status                                                                                 |
|  52 | `test_variant_u64_i64_roundtrip`                                 |   ✅   | UInt64                                                                                               |
|  53 | `test_parse_read`                                                |   ✅   | Parse read                                                                                           |
|  54 | `test_build_read_response`                                       |   ✅   | Build read response                                                                                  |
|  55 | `test_parse_browse`                                              |   ✅   | Parse browse                                                                                         |
|  56 | `test_build_browse_response`                                     |   ✅   | Build browse response                                                                                |
|  57 | `test_build_browse_response_unknown`                             |   ✅   | Build browse response unknown                                                                        |
|  58 | `test_build_close_session_response`                              |   ✅   | Build close session response                                                                         |
|  59 | `test_build_get_endpoints`                                       |   ✅   | Build get endpoints                                                                                  |
|  60 | `test_build_service_fault`                                       |   ✅   | Build service fault                                                                                  |
|  61 | `test_datavalue_roundtrip`                                       |   ✅   | Datavalue roundtrip                                                                                  |
|  62 | `test_parse_and_build_write`                                     |   ✅   | Build a WriteRequest writing one Int32 to ns=1;i=10 (value-only DataValue).                          |
|  63 | `test_rx_and_proto_handler_host_stubs`                           |   ✅   | Rx and proto handler host stubs                                                                      |
|  64 | `test_parse_open_with_cert_and_nonce`                            |   ✅   | An OPEN carrying non-empty SenderCertificate + ReceiverCertificateThumbprint + ClientNonce           |
|  65 | `test_parse_read_truncated_item_rejected`                        |   ✅   | A NodesToRead count larger than the items actually present makes the per-item NodeId read            |
|  66 | `test_parse_browse_truncated_item_rejected`                      |   ✅   | Parse browse truncated item rejected                                                                 |
|  67 | `test_parse_write_truncated_item_and_indexrange`                 |   ✅   | Count claims two items but only one is present -> the second NodeId read underruns -> reject.        |
|  68 | `test_parse_open_wrong_body_typeid`                              |   ✅   | Body TypeId is OPEN_REQ (446 -> FourByte bytes 01 00 BE 01); corrupt the id so it no longer matches. |
|  69 | `test_parse_write_malformed_datavalue_rejected`                  |   ✅   | The item's DataValue is INT32 0x11223344; corrupt its Variant type byte to an unsupported value.     |
|  70 | `test_parse_request_header_truncated_addhdr`                     |   ✅   | Parse request header truncated addhdr                                                                |
|  71 | `test_parse_open_truncated_frames`                               |   ✅   | Parse open truncated frames                                                                          |

</details>

---

## test_opcua_client - native_opcua_client - ✅ 31 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Round-trip tests for the OPC UA client (services/pc_opcua_client): the client builds_

|   # | Test                                          | Status | Description                                                                               |
| --: | :-------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_builders_encode_null_strings`           |   ✅   | Builders encode null strings                                                              |
|   2 | `test_on_ack_header_guards`                   |   ✅   | On ack header guards                                                                      |
|   3 | `test_msg_envelope_guards`                    |   ✅   | Msg envelope guards                                                                       |
|   4 | `test_on_open_envelope_and_result_guards`     |   ✅   | On open envelope and result guards                                                        |
|   5 | `test_on_open_rejects_message_size_mismatch`  |   ✅   | On open rejects message size mismatch                                                     |
|   6 | `test_parsers_reject_bad_service_result`      |   ✅   | Parsers reject bad service result                                                         |
|   7 | `test_parsers_reject_truncated_body`          |   ✅   | Parsers reject truncated body                                                             |
|   8 | `test_on_read_optional_fields_and_limits`     |   ✅   | On read optional fields and limits                                                        |
|   9 | `test_on_write_limits_and_null_sink`          |   ✅   | On write limits and null sink                                                             |
|  10 | `test_on_browse_limits_and_null_sink`         |   ✅   | On browse limits and null sink                                                            |
|  11 | `test_on_browse_display_name_empty_mask`      |   ✅   | On browse display name empty mask                                                         |
|  12 | `test_browse_display_name_locale`             |   ✅   | Browse display name locale                                                                |
|  13 | `test_on_read_all_variant_types`              |   ✅   | On read all variant types                                                                 |
|  14 | `test_client_parsers_reject_fault`            |   ✅   | Client parsers reject fault                                                               |
|  15 | `test_client_parsers_reject_malformed`        |   ✅   | Client parsers reject malformed                                                           |
|  16 | `test_hello_ack_roundtrip`                    |   ✅   | Hello ack roundtrip                                                                       |
|  17 | `test_open_roundtrip`                         |   ✅   | Open roundtrip                                                                            |
|  18 | `test_session_roundtrip`                      |   ✅   | Session roundtrip                                                                         |
|  19 | `test_get_endpoints_roundtrip`                |   ✅   | Get endpoints roundtrip                                                                   |
|  20 | `test_service_fault_rejected_by_parsers`      |   ✅   | An unknown service draws a ServiceFault; a typed parser must reject it (wrong TypeId).    |
|  21 | `test_read_roundtrip`                         |   ✅   | Read roundtrip                                                                            |
|  22 | `test_browse_roundtrip`                       |   ✅   | Browse roundtrip                                                                          |
|  23 | `test_write_roundtrip`                        |   ✅   | Write roundtrip                                                                           |
|  24 | `test_close_session_roundtrip`                |   ✅   | Close session roundtrip                                                                   |
|  25 | `test_close_channel_is_clo`                   |   ✅   | Close channel is clo                                                                      |
|  26 | `test_seq_and_request_id_increment`           |   ✅   | Seq and request id increment                                                              |
|  27 | `test_builder_overflow_guard`                 |   ✅   | A capacity too small for even the frame header overflows the writer; cw_patch returns 0.  |
|  28 | `test_on_read_unknown_variant_rejected`       |   ✅   | A server sending a DataValue whose Variant type byte is unsupported must be rejected, not |
|  29 | `test_response_parsers_reject_negative_count` |   ✅   | Response parsers reject negative count                                                    |
|  30 | `test_on_open_guards`                         |   ✅   | On open guards                                                                            |
|  31 | `test_response_header_string_table_skip`      |   ✅   | A ResponseHeader carrying a non-empty StringTable makes cr_skip_string_array iterate; the |

</details>

---

## test_umati - native_umati - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the umati (OPC UA for Machine Tools, OPC 40501-1) MachineTool model:_

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_install_binds_the_model`                     |   ✅   | Install binds the model                     |
|   2 | `test_read_every_remaining_leaf`                   |   ✅   | Read every remaining leaf                   |
|   3 | `test_browse_every_remaining_container`            |   ✅   | Browse every remaining container            |
|   4 | `test_browse_clamps_to_max`                        |   ✅   | Browse clamps to max                        |
|   5 | `test_browse_objects_folder_without_model_name`    |   ✅   | Browse objects folder without model name    |
|   6 | `test_browse_ns0_other_than_objects_folder_misses` |   ✅   | Browse ns0 other than objects folder misses |
|   7 | `test_browse_objects_folder_has_machinetool`       |   ✅   | Browse objects folder has machinetool       |
|   8 | `test_browse_machinetool_components`               |   ✅   | Browse machinetool components               |
|   9 | `test_browse_identification_variables`             |   ✅   | Browse identification variables             |
|  10 | `test_browse_monitoring_and_children`              |   ✅   | Browse monitoring and children              |
|  11 | `test_browse_leaf_and_unknown_return_negative`     |   ✅   | Browse leaf and unknown return negative     |
|  12 | `test_read_identification`                         |   ✅   | Read identification                         |
|  13 | `test_read_monitoring_values`                      |   ✅   | Read monitoring values                      |
|  14 | `test_read_production_and_notification`            |   ✅   | Read production and notification            |
|  15 | `test_read_null_string_served_as_empty`            |   ✅   | Read null string served as empty            |
|  16 | `test_read_rejects_unknown_ns_attr_and_node`       |   ✅   | Read rejects unknown ns attr and node       |
|  17 | `test_read_before_bind_is_a_clean_miss`            |   ✅   | Read before bind is a clean miss            |

</details>

---

## test_robotics - native_robotics - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the OPC UA for Robotics (OPC 40010-1) MotionDeviceSystem model:_

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_read_every_remaining_leaf`                          |   ✅   | Read every remaining leaf                          |
|   2 | `test_read_axis_all_four_variables`                       |   ✅   | Read axis all four variables                       |
|   3 | `test_read_axis_sub_id_bounds`                            |   ✅   | Read axis sub id bounds                            |
|   4 | `test_browse_clamps_to_max`                               |   ✅   | Browse clamps to max                               |
|   5 | `test_browse_ns0_other_than_objects_folder_misses`        |   ✅   | Browse ns0 other than objects folder misses        |
|   6 | `test_browse_objects_folder_without_model_name`           |   ✅   | Browse objects folder without model name           |
|   7 | `test_browse_axes_clamped_to_compiled_maximum`            |   ✅   | Browse axes clamped to compiled maximum            |
|   8 | `test_install_binds_the_model`                            |   ✅   | Install binds the model                            |
|   9 | `test_browse_objects_folder_has_system`                   |   ✅   | Browse objects folder has system                   |
|  10 | `test_browse_system_folders`                              |   ✅   | Browse system folders                              |
|  11 | `test_browse_motiondevice_components`                     |   ✅   | Browse motiondevice components                     |
|  12 | `test_browse_parameterset`                                |   ✅   | Browse parameterset                                |
|  13 | `test_browse_axes_parametric`                             |   ✅   | Browse axes parametric                             |
|  14 | `test_browse_controller_and_software`                     |   ✅   | Browse controller and software                     |
|  15 | `test_browse_safetystate`                                 |   ✅   | Browse safetystate                                 |
|  16 | `test_browse_leaf_and_unknown_return_negative`            |   ✅   | Browse leaf and unknown return negative            |
|  17 | `test_read_motiondevice_identity`                         |   ✅   | Read motiondevice identity                         |
|  18 | `test_read_axes_pick_the_right_axis`                      |   ✅   | Axis_1 ActualPosition = 10.5                       |
|  19 | `test_read_controller_and_safety`                         |   ✅   | Read controller and safety                         |
|  20 | `test_read_null_string_served_as_empty`                   |   ✅   | Read null string served as empty                   |
|  21 | `test_read_rejects_unknown_ns_attr_and_axis_out_of_range` |   ✅   | Read rejects unknown ns attr and axis out of range |
|  22 | `test_read_before_bind_is_a_clean_miss`                   |   ✅   | Read before bind is a clean miss                   |

</details>

---

## test_euromap77 - native_euromap77 - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the EUROMAP 77 (OPC 40077) IMM_MES_Interface model: the Browse hierarchy shape + the_

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_browse_objects_folder_has_interface`     |   ✅   | Browse objects folder has interface     |
|   2 | `test_browse_interface_components`             |   ✅   | Browse interface components             |
|   3 | `test_browse_machineinformation`               |   ✅   | Browse machineinformation               |
|   4 | `test_browse_status_and_jobs`                  |   ✅   | Browse status and jobs                  |
|   5 | `test_browse_activejob_and_values`             |   ✅   | Browse activejob and values             |
|   6 | `test_browse_leaf_and_unknown_return_negative` |   ✅   | Browse leaf and unknown return negative |
|   7 | `test_read_identity_and_status`                |   ✅   | Read identity and status                |
|   8 | `test_read_job_and_counters`                   |   ✅   | Read job and counters                   |
|   9 | `test_read_null_string_served_as_empty`        |   ✅   | Read null string served as empty        |
|  10 | `test_read_rejects_unknown_ns_attr_and_node`   |   ✅   | Read rejects unknown ns attr and node   |
|  11 | `test_read_before_bind_is_a_clean_miss`        |   ✅   | Read before bind is a clean miss        |
|  12 | `test_read_every_machineinformation_string`    |   ✅   | Read every machineinformation string    |
|  13 | `test_read_every_activejob_string`             |   ✅   | Read every activejob string             |
|  14 | `test_read_remaining_activejobvalues`          |   ✅   | Read remaining activejobvalues          |
|  15 | `test_browse_stops_at_caller_capacity`         |   ✅   | Browse stops at caller capacity         |
|  16 | `test_browse_other_ns0_node_is_a_miss`         |   ✅   | Browse other ns0 node is a miss         |
|  17 | `test_browse_objects_folder_default_name`      |   ✅   | Browse objects folder default name      |
|  18 | `test_install_binds_the_model`                 |   ✅   | Install binds the model                 |

</details>

---

## test_syslog - native_syslog - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the RFC 5424 syslog client (pc_syslog_format formatter + pc_syslog_init /_

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_pri_local0_info`                    |   ✅   | Pri local0 info                    |
|   2 | `test_pri_computation_varies`             |   ✅   | daemon(3)*8 + err(3) = 27          |
|   3 | `test_nilvalue_for_empty_fields`          |   ✅   | Nilvalue for empty fields          |
|   4 | `test_empty_message_ok`                   |   ✅   | Empty message ok                   |
|   5 | `test_overflow_returns_zero`              |   ✅   | Overflow returns zero              |
|   6 | `test_length_matches_strlen`              |   ✅   | Length matches strlen              |
|   7 | `test_init_and_log_captured`              |   ✅   | Init and log captured              |
|   8 | `test_log_not_ready_when_no_server`       |   ✅   | Log not ready when no server       |
|   9 | `test_format_null_and_pri_clamp`          |   ✅   | Guard clauses return 0.            |
|  10 | `test_init_truncates_long_fields`         |   ✅   | Init truncates long fields         |
|  11 | `test_init_empty_server_ip_not_ready`     |   ✅   | Init empty server ip not ready     |
|  12 | `test_format_hostname_empty_appname_null` |   ✅   | Format hostname empty appname null |
|  13 | `test_format_append_boundaries`           |   ✅   | Format append boundaries           |
|  14 | `test_log_overflow_when_ready`            |   ✅   | Log overflow when ready            |

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

## test_relay - native_relay - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the TCP relay / DNAT byte pump (services/net/relay): bidirectional transfer, the_

|   # | Test                                 | Status | Description                                                             |
| --: | :----------------------------------- | :----: | :---------------------------------------------------------------------- |
|   1 | `test_bidirectional`                 |   ✅   | Bidirectional                                                           |
|   2 | `test_backpressure`                  |   ✅   | Backpressure                                                            |
|   3 | `test_half_close_shutdown`           |   ✅   | Half close shutdown                                                     |
|   4 | `test_send_error`                    |   ✅   | Send error                                                              |
|   5 | `test_one_way_idle_then_close`       |   ✅   | origin never sends; client sends then closes -> relay completes cleanly |
|   6 | `test_note_eof_out_of_band`          |   ✅   | Note eof out of band                                                    |
|   7 | `test_zero_length_read_no_progress`  |   ✅   | Zero length read no progress                                            |
|   8 | `test_flush_send_error`              |   ✅   | Flush send error                                                        |
|   9 | `test_send_error_reverse_direction`  |   ✅   | Send error reverse direction                                            |
|  10 | `test_null_argument_guards`          |   ✅   | Null argument guards                                                    |
|  11 | `test_shutdown_null_seam`            |   ✅   | Shutdown null seam                                                      |
|  12 | `test_note_eof_with_backlog_pending` |   ✅   | Note eof with backlog pending                                           |

</details>

---

## test_ld2410 - native_ld2410 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the LD2410 mmWave radar codec (services/peripherals/ld2410): decoding a basic and an_

|   # | Test                                      | Status | Description                                                              |
| --: | :---------------------------------------- | :----: | :----------------------------------------------------------------------- |
|   1 | `test_parse_basic`                        |   ✅   | Parse basic                                                              |
|   2 | `test_parse_engineering`                  |   ✅   | Parse engineering                                                        |
|   3 | `test_reject_malformed`                   |   ✅   | bad header                                                               |
|   4 | `test_stream_resync_and_split`            |   ✅   | Stream resync and split                                                  |
|   5 | `test_stream_absurd_length_drops`         |   ✅   | Stream absurd length drops                                               |
|   6 | `test_helpers`                            |   ✅   | Helpers                                                                  |
|   7 | `test_command_encoders`                   |   ✅   | Command encoders                                                         |
|   8 | `test_host_stubs_and_parse_guards`        |   ✅   | Host build: the UART bind functions fail closed / return null.           |
|   9 | `test_ld2410b_command_encoders`           |   ✅   | "FD FC FB FA                                                             | 04 00 | A4 00 | 01 00 | 04 03 02 01" (Bluetooth on) |
|  10 | `test_ld2410b_ack_decoding`               |   ✅   | get-MAC ACK: "FD FC FB FA                                                | 0A 00 | A5 01 | 00 00 | 8F 27 2E B8 0F 65           | 04 03 02 01" |
|  11 | `test_ld2410b_ack_rejects_malformed`      |   ✅   | Ld2410b ack rejects malformed                                            |
|  12 | `test_parse_report_more_branches`         |   ✅   | out == nullptr on an otherwise well-formed frame must still fail closed. |
|  13 | `test_stream_header_partial_resync`       |   ✅   | Stream header partial resync                                             |
|  14 | `test_distance_cm_and_ack_extra_branches` |   ✅   | Null-report guard, and the state == BOTH arm (moving distance wins).     |

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

## test_hmmd - native_hmmd - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the Waveshare HMMD mmWave radar codec (services/peripherals/hmmd): decoding a report frame_

|   # | Test                                              | Status | Description                                                                                 |
| --: | :------------------------------------------------ | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_frame_geometry_is_self_consistent`          |   ✅   | 4 header + 2 length + 35 payload + 4 footer == 45, the reference library's kMaxFrameLength. |
|   2 | `test_parse_report`                               |   ✅   | Parse report                                                                                |
|   3 | `test_parse_report_not_detected`                  |   ✅   | Parse report not detected                                                                   |
|   4 | `test_reject_malformed_report`                    |   ✅   | Reject malformed report                                                                     |
|   5 | `test_stream_resync_and_split`                    |   ✅   | Stream resync and split                                                                     |
|   6 | `test_stream_absurd_length_drops`                 |   ✅   | Stream absurd length drops                                                                  |
|   7 | `test_stream_push_rejects_null_out`               |   ✅   | Stream push rejects null out                                                                |
|   8 | `test_stream_header_resync_on_repeated_lead_byte` |   ✅   | Stream header resync on repeated lead byte                                                  |
|   9 | `test_command_encoders`                           |   ✅   | open command mode: word 0x00FF, value 0x0001 -> len 4                                       |
|  10 | `test_command_encoder_guards`                     |   ✅   | Command encoder guards                                                                      |
|  11 | `test_ack_decoding`                               |   ✅   | ACK to read-config: word 0x0108 (reply convention), then two data octets                    |
|  12 | `test_ack_rejects_malformed`                      |   ✅   | Ack rejects malformed                                                                       |
|  13 | `test_host_binding_stubs`                         |   ✅   | Host binding stubs                                                                          |

</details>

---

## test_rcwl0516 - native_rcwl0516 - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for the one-GPIO presence facade (services/peripherals/rcwl0516): the debounce that swallows_

|   # | Test                                                | Status | Description                                                         |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------ |
|   1 | `test_starts_absent`                                |   ✅   | Starts absent                                                       |
|   2 | `test_high_asserts_only_after_debounce`             |   ✅   | High asserts only after debounce                                    |
|   3 | `test_chatter_shorter_than_debounce_never_asserts`  |   ✅   | Chatter shorter than debounce never asserts                         |
|   4 | `test_hold_bridges_the_gap_after_pin_drops`         |   ✅   | Hold bridges the gap after pin drops                                |
|   5 | `test_retrigger_gaps_stay_one_continuous_span`      |   ✅   | Retrigger gaps stay one continuous span                             |
|   6 | `test_event_fires_once_per_transition`              |   ✅   | Event fires once per transition                                     |
|   7 | `test_wrap_safe_across_millis_rollover`             |   ✅   | Wrap safe across millis rollover                                    |
|   8 | `test_zero_debounce_and_zero_hold_are_pass_through` |   ✅   | Zero debounce and zero hold are pass through                        |
|   9 | `test_repeated_and_static_now_is_harmless`          |   ✅   | Polling faster than the clock ticks must not stall or double-count. |
|  10 | `test_rcwl_defaults_and_null_guards`                |   ✅   | Rcwl defaults and null guards                                       |

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
|   7 | `test_host_build_gpio_binding_stubs`     |   ✅   | This test binary is a host (non-ARDUINO) build, so the GPIO-binding functions compile to the      |

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

## test_ads1115 - native_ads1115 - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the ADS1115 ADC codec (services/peripherals/ads1115): building the 16-bit config word for a_

|   # | Test                              | Status | Description                                                                                    |
| --: | :-------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_config_word`                |   ✅   | ch0, +/-4.096V, 128 SPS: OS                                                                    | MUX_AIN0 | PGA1 | MODE_SINGLE | DR128 | COMP_DISABLE. |
|   2 | `test_config_fallbacks`           |   ✅   | Out-of-range channel/gain/dr fall back to ch0 / +/-2.048V / 128 SPS = 0xC583.                  |
|   3 | `test_raw_to_uv`                  |   ✅   | gain 1 (+/-4.096 V) -> 125 uV/LSB.                                                             |
|   4 | `test_raw_to_uv_gain_clamp`       |   ✅   | An out-of-range gain code clamps to GAIN_2 (its FSR), so the conversion never indexes past the |
|   5 | `test_host_i2c_stubs_fail_closed` |   ✅   | On a host build there is no I2C: begin and both reads fail closed (false).                     |

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

## test_hpack - native_hpack - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the HPACK codec (network_drivers/presentation/http/http2/hpack) against the RFC 7541_

|   # | Test                                      | Status | Description                                                         |
| --: | :---------------------------------------- | :----: | :------------------------------------------------------------------ |
|   1 | `test_hpack_dyn_init_default_size`        |   ✅   | Hpack dyn init default size                                         |
|   2 | `test_hpack_indexed_field_truncated_int`  |   ✅   | Hpack indexed field truncated int                                   |
|   3 | `test_hpack_encode_repeated_static_name`  |   ✅   | Hpack encode repeated static name                                   |
|   4 | `test_hpack_prim_edge_guards`             |   ✅   | Hpack prim edge guards                                              |
|   5 | `test_hpack_more_errors`                  |   ✅   | Hpack more errors                                                   |
|   6 | `test_dyn_size_update`                    |   ✅   | Dyn size update                                                     |
|   7 | `test_oversize_entry_clears`              |   ✅   | Oversize entry clears                                               |
|   8 | `test_dynamic_name_and_index`             |   ✅   | Dynamic name and index                                              |
|   9 | `test_hpack_decode_errors`                |   ✅   | Hpack decode errors                                                 |
|  10 | `test_hpack_buffer_bounds`                |   ✅   | Hpack buffer bounds                                                 |
|  11 | `test_hpack_resolve_dynamic_name_too_big` |   ✅   | Hpack resolve dynamic name too big                                  |
|  12 | `test_hpack_encode_paths`                 |   ✅   | pc_hpack_dyn_init clamps a too-large max to the table storage.      |
|  13 | `test_int_coding`                         |   ✅   | C.1.1: 10, prefix 5 -> 0x0a                                         |
|  14 | `test_huffman`                            |   ✅   | Huffman                                                             |
|  15 | `test_decode_c31_and_index`               |   ✅   | RFC 7541 C.3.1: GET / with :authority www.example.com (no Huffman). |
|  16 | `test_dynamic_eviction`                   |   ✅   | Dynamic eviction                                                    |
|  17 | `test_encode_static`                      |   ✅   | Encode static                                                       |
|  18 | `test_encode_decode_roundtrip`            |   ✅   | Encode decode roundtrip                                             |
|  19 | `test_reject_malformed`                   |   ✅   | Reject malformed                                                    |

</details>

---

## test_h2_frame - native_h2frame - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the HTTP/2 frame layer (network_drivers/presentation/http/http2/pc_h2_frame, RFC 9113):_

|   # | Test                                     | Status | Description                                                       |
| --: | :--------------------------------------- | :----: | :---------------------------------------------------------------- |
|   1 | `test_header_roundtrip`                  |   ✅   | Header roundtrip                                                  |
|   2 | `test_settings_build_parse`              |   ✅   | Settings build parse                                              |
|   3 | `test_settings_validation`               |   ✅   | Settings validation                                               |
|   4 | `test_control_frames`                    |   ✅   | SETTINGS ACK: length 0, type 4, flags ACK, stream 0               |
|   5 | `test_headers_and_data`                  |   ✅   | HEADERS stream 1, one HPACK byte, end_stream -> flags END_HEADERS | END_STREAM = 0x05 |
|   6 | `test_preface`                           |   ✅   | Preface                                                           |
|   7 | `test_settings_all_ids_and_build_guards` |   ✅   | Settings all ids and build guards                                 |

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

## test_h3_frame - native_h3frame - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the HTTP/3 framing layer (network_drivers/presentation/http/http3/pc_h3_frame, RFC 9114_

|   # | Test                                    | Status | Description                                                                                |
| --: | :-------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_header_roundtrip`                 |   ✅   | SETTINGS(4), length 0 -> two 1-byte varints.                                               |
|   2 | `test_build_data_and_goaway`            |   ✅   | Build data and goaway                                                                      |
|   3 | `test_settings_roundtrip`               |   ✅   | header (type 0x04 + length 0x08) + payload: 01 5000 06 80100000                            |
|   4 | `test_reserved`                         |   ✅   | Reserved                                                                                   |
|   5 | `test_build_headers`                    |   ✅   | Build headers                                                                              |
|   6 | `test_builder_overflow`                 |   ✅   | Builder overflow                                                                           |
|   7 | `test_parse_errors`                     |   ✅   | Parse errors                                                                               |
|   8 | `test_settings_blocked_streams`         |   ✅   | Settings blocked streams                                                                   |
|   9 | `test_parse_settings_id_decode_fails`   |   ✅   | Parse settings id decode fails                                                             |
|  10 | `test_build_data_and_headers_edge_caps` |   ✅   | len == 0: header is written, memcpy is skipped.                                            |
|  11 | `test_build_settings_partial_overflow`  |   ✅   | header (type 0x04 + length 0x02) consumes exactly 2 bytes; no room left for the id varint. |
|  12 | `test_build_goaway_partial_overflow`    |   ✅   | stream id 64 needs a 2-byte varint; header (type 0x07 + length 0x02) consumes exactly 2    |

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

## test_config_store - native_config_store - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the typed NVS config store (services/storage/config_store), exercised_

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_str_round_trip`                         |   ✅   | Str round trip                         |
|   2 | `test_str_default_when_missing`               |   ✅   | Str default when missing               |
|   3 | `test_str_overwrite`                          |   ✅   | Str overwrite                          |
|   4 | `test_str_truncates_to_capacity`              |   ✅   | Str truncates to capacity              |
|   5 | `test_u32_round_trip`                         |   ✅   | U32 round trip                         |
|   6 | `test_u32_default_when_missing`               |   ✅   | U32 default when missing               |
|   7 | `test_blob_round_trip`                        |   ✅   | Blob round trip                        |
|   8 | `test_blob_bounded_by_capacity`               |   ✅   | Blob bounded by capacity               |
|   9 | `test_blob_missing_returns_zero`              |   ✅   | Blob missing returns zero              |
|  10 | `test_erase_removes_key`                      |   ✅   | Erase removes key                      |
|  11 | `test_clear_wipes_namespace`                  |   ✅   | Clear wipes namespace                  |
|  12 | `test_table_full_rejects_new_key`             |   ✅   | Table full rejects new key             |
|  13 | `test_existing_key_overwrites_even_when_full` |   ✅   | Existing key overwrites even when full |
|  14 | `test_key_too_long_rejected`                  |   ✅   | Key too long rejected                  |
|  15 | `test_setter_getter_null_guards`              |   ✅   | Setter getter null guards              |
|  16 | `test_key_ok_rejects_null_and_empty_key`      |   ✅   | Key ok rejects null and empty key      |
|  17 | `test_get_str_zero_capacity_with_nonnull_out` |   ✅   | Get str zero capacity with nonnull out |
|  18 | `test_set_blob_rejects_len_over_capacity`     |   ✅   | Set blob rejects len over capacity     |
|  19 | `test_get_u32_short_entry_returns_default`    |   ✅   | Get u32 short entry returns default    |
|  20 | `test_get_u32_rejects_invalid_key`            |   ✅   | Get u32 rejects invalid key            |
|  21 | `test_get_blob_rejects_invalid_key`           |   ✅   | Get blob rejects invalid key           |
|  22 | `test_get_blob_null_out_with_existing_key`    |   ✅   | Get blob null out with existing key    |
|  23 | `test_get_blob_entry_shorter_than_capacity`   |   ✅   | Get blob entry shorter than capacity   |
|  24 | `test_erase_rejects_invalid_key`              |   ✅   | Erase rejects invalid key              |

</details>

---

## test_device_id - native_device_id - ✅ 4 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the MAC-derived device UUID (services/system/device_id). The expected_

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_uuid_matches_reference_aabbccddeeff` |   ✅   | Uuid matches reference aabbccddeeff |
|   2 | `test_uuid_matches_reference_001122334455` |   ✅   | Uuid matches reference 001122334455 |
|   3 | `test_uuid_is_deterministic`               |   ✅   | Uuid is deterministic               |
|   4 | `test_uuid_version_and_variant_bits`       |   ✅   | Uuid version and variant bits       |

</details>

---

## test_auth_lockout - native_auth_lockout - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the per-peer brute-force auth lockout (services/security/auth_lockout)._

|   # | Test                                               | Status | Description                                 |
| --: | :------------------------------------------------- | :----: | :------------------------------------------ |
|   1 | `test_below_threshold_not_locked`                  |   ✅   | Below threshold not locked                  |
|   2 | `test_locks_at_threshold`                          |   ✅   | Locks at threshold                          |
|   3 | `test_exponential_backoff`                         |   ✅   | Exponential backoff                         |
|   4 | `test_caps_at_max`                                 |   ✅   | Caps at max                                 |
|   5 | `test_expires_after_window`                        |   ✅   | Expires after window                        |
|   6 | `test_success_clears`                              |   ✅   | Success clears                              |
|   7 | `test_isolates_addresses`                          |   ✅   | Isolates addresses                          |
|   8 | `test_v6_distinct_from_v4_and_each_other`          |   ✅   | V6 distinct from v4 and each other          |
|   9 | `test_zero_ip_never_locked`                        |   ✅   | Zero ip never locked                        |
|  10 | `test_table_full_tracks_new_address`               |   ✅   | Table full tracks new address               |
|  11 | `test_active_lockout_survives_eviction`            |   ✅   | Active lockout survives eviction            |
|  12 | `test_succeed_unspecified_and_table_full_eviction` |   ✅   | Succeed unspecified and table full eviction |
|  13 | `test_succeed_unknown_address_is_noop`             |   ✅   | Succeed unknown address is noop             |
|  14 | `test_fail_counter_saturates_at_uint16_max`        |   ✅   | Fail counter saturates at uint16 max        |

</details>

---

## test_forwarded_trust - native_forwarded_trust - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the trusted-reverse-proxy forwarded-client resolver (services/security/forwarded_trust)._

|   # | Test                                               | Status | Description                                                                                  |
| --: | :------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_empty_table_trusts_nothing`                  |   ✅   | Empty table trusts nothing                                                                   |
|   2 | `test_v4_cidr_membership`                          |   ✅   | V4 cidr membership                                                                           |
|   3 | `test_v6_cidr_and_host_route`                      |   ✅   | V6 cidr and host route                                                                       |
|   4 | `test_add_cidr_rejects_malformed`                  |   ✅   | Add cidr rejects malformed                                                                   |
|   5 | `test_table_full`                                  |   ✅   | Table full                                                                                   |
|   6 | `test_trusted_peer_honors_forwarded`               |   ✅   | Trusted peer honors forwarded                                                                |
|   7 | `test_trusted_peer_honors_v6_forwarded`            |   ✅   | Trusted peer honors v6 forwarded                                                             |
|   8 | `test_untrusted_peer_ignores_forwarded`            |   ✅   | Untrusted peer ignores forwarded                                                             |
|   9 | `test_trusted_peer_bad_token_falls_back`           |   ✅   | Trusted peer bad token falls back                                                            |
|  10 | `test_null_guards`                                 |   ✅   | Null guards                                                                                  |
|  11 | `test_add_rejects_null_network`                    |   ✅   | Add rejects null network                                                                     |
|  12 | `test_add_rejects_bad_family_and_over_long_prefix` |   ✅   | Add rejects bad family and over long prefix                                                  |
|  13 | `test_add_cidr_rejects_overlong_address`           |   ✅   | PC_IP_STR_MAX is 46; this address text alone is well past that, with no slash reached first. |
|  14 | `test_add_cidr_rejects_prefix_below_digit_range`   |   ✅   | Add cidr rejects prefix below digit range                                                    |
|  15 | `test_contains_rejects_null_peer`                  |   ✅   | Contains rejects null peer                                                                   |

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

## test_net_egress - native_net_egress - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for egress-interface reporting (network_drivers/physical). The lwIP_

|   # | Test                            | Status | Description              |
| --: | :------------------------------ | :----: | :----------------------- |
|   1 | `test_classify_sta`             |   ✅   | Classify sta             |
|   2 | `test_classify_ap`              |   ✅   | Classify ap              |
|   3 | `test_classify_eth`             |   ✅   | Classify eth             |
|   4 | `test_classify_none`            |   ✅   | Classify none            |
|   5 | `test_egress_host_stub`         |   ✅   | Egress host stub         |
|   6 | `test_eth_host_stub`            |   ✅   | Eth host stub            |
|   7 | `test_wifi_bringup_host_stub`   |   ✅   | Wifi bringup host stub   |
|   8 | `test_ipv6_host_stub`           |   ✅   | Ipv6 host stub           |
|   9 | `test_radio_readouts_host_stub` |   ✅   | Radio readouts host stub |

</details>

---

## test_client - native_client - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the outbound TCP client transport (network_drivers/transport/client.cpp)._

|   # | Test                          | Status | Description                                                                 |
| --: | :---------------------------- | :----: | :-------------------------------------------------------------------------- |
|   1 | `test_open_fails_closed`      |   ✅   | Open fails closed                                                           |
|   2 | `test_connected_always_false` |   ✅   | Connected always false                                                      |
|   3 | `test_is_closed_always_true`  |   ✅   | Is closed always true                                                       |
|   4 | `test_send_always_false`      |   ✅   | Send always false                                                           |
|   5 | `test_available_always_zero`  |   ✅   | Available always zero                                                       |
|   6 | `test_read_always_zero`       |   ✅   | Read always zero                                                            |
|   7 | `test_close_is_a_noop`        |   ✅   | No state to observe (the host build has none); just prove it does not crash |

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

## test_statsd - native_statsd - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the StatsD client (services/iot/statsd): the pure line formatter_

|   # | Test                                       | Status | Description                                                                            |
| --: | :----------------------------------------- | :----: | :------------------------------------------------------------------------------------- |
|   1 | `test_format_types`                        |   ✅   | Format types                                                                           |
|   2 | `test_format_sample_rate`                  |   ✅   | Format sample rate                                                                     |
|   3 | `test_format_tags_and_both`                |   ✅   | Format tags and both                                                                   |
|   4 | `test_format_guards`                       |   ✅   | Format guards                                                                          |
|   5 | `test_emit_counter_and_negative`           |   ✅   | Emit counter and negative                                                              |
|   6 | `test_emit_gauge_and_delta`                |   ✅   | Emit gauge and delta                                                                   |
|   7 | `test_emit_timing_set_sampled`             |   ✅   | Emit timing set sampled                                                                |
|   8 | `test_emit_global_tags`                    |   ✅   | Emit global tags                                                                       |
|   9 | `test_emit_noop_until_begin`               |   ✅   | Emit noop until begin                                                                  |
|  10 | `test_rate_clamp_and_stage_overflow`       |   ✅   | A rate rounding below one thousandth clamps up to 1; a rate near 1 clamps down to 999. |
|  11 | `test_format_guard_null_out_and_zero_cap`  |   ✅   | Format guard null out and zero cap                                                     |
|  12 | `test_format_append_chain_overflow_points` |   ✅   | Format append chain overflow points                                                    |
|  13 | `test_format_rate_zero_and_empty_tags`     |   ✅   | Format rate zero and empty tags                                                        |
|  14 | `test_emit_zero_value_and_set_null_member` |   ✅   | Emit zero value and set null member                                                    |
|  15 | `test_emit_overlong_name_is_noop`          |   ✅   | Emit overlong name is noop                                                             |

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

## test_hart - native_hart - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/hart: the HART command frame + HART-IP header codec._

|   # | Test                                    | Status | Description                                                                         |
| --: | :-------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_checksum`                         |   ✅   | XOR longitudinal parity.                                                            |
|   2 | `test_build_command0_short`             |   ✅   | Command 0 (read unique id), STX, primary-master short address 0, no data.           |
|   3 | `test_build_with_data`                  |   ✅   | [02 80 01 02 AB CD ck], ck = 02^80^01^02^AB^CD = 0xE7.                              |
|   4 | `test_build_long_address`               |   ✅   | Build long address                                                                  |
|   5 | `test_parse_roundtrip_and_bad_checksum` |   ✅   | Parse roundtrip and bad checksum                                                    |
|   6 | `test_hartip_header`                    |   ✅   | Hartip header                                                                       |
|   7 | `test_hartip_parse`                     |   ✅   | A HART-IP response carrying a 5-octet token PDU payload; total length = 8 + 5 = 13. |
|   8 | `test_build_and_parse_guards`           |   ✅   | Build and parse guards                                                              |

</details>

---

## test_nts - native_nts - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/nts: the NTS-KE record + NTS NTP extension-field wire codec (RFC 8915)._

|   # | Test                            | Status | Description                                                                      |
| --: | :------------------------------ | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_ke_record`                |   ✅   | Ke record                                                                        |
|   2 | `test_ke_request`               |   ✅   | Next-Protocol(NTPv4) + AEAD(AES-SIV-CMAC-256=15) + End-of-Message, all critical. |
|   3 | `test_ke_parse`                 |   ✅   | Ke parse                                                                         |
|   4 | `test_extension_field_padding`  |   ✅   | 32-byte unique id: 4 + 32 = 36, already a multiple of 4.                         |
|   5 | `test_ef_wrappers_and_guards`   |   ✅   | Ef wrappers and guards                                                           |
|   6 | `test_ke_record_guards`         |   ✅   | Ke record guards                                                                 |
|   7 | `test_ke_record_non_critical`   |   ✅   | Ke record non critical                                                           |
|   8 | `test_ke_request_short_buffers` |   ✅   | Ke request short buffers                                                         |
|   9 | `test_ef_empty_and_null_value`  |   ✅   | Ef empty and null value                                                          |
|  10 | `test_ef_length_field_overflow` |   ✅   | Ef length field overflow                                                         |

</details>

---

## test_dds - native_dds - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/dds: the RTPS message + submessage framing codec._

|   # | Test                                       | Status | Description                                                       |
| --: | :----------------------------------------- | :----: | :---------------------------------------------------------------- |
|   1 | `test_header`                              |   ✅   | Header                                                            |
|   2 | `test_submessage_endianness`               |   ✅   | Little-endian (E flag set): octetsToNextHeader = 0x0008 -> 08 00. |
|   3 | `test_parse_message`                       |   ✅   | Parse message                                                     |
|   4 | `test_parse_rejects`                       |   ✅   | Parse rejects                                                     |
|   5 | `test_rtps_build_guards`                   |   ✅   | Rtps build guards                                                 |
|   6 | `test_header_null_args`                    |   ✅   | Header null args                                                  |
|   7 | `test_submessage_null_args`                |   ✅   | Submessage null args                                              |
|   8 | `test_parse_null_msg`                      |   ✅   | Parse null msg                                                    |
|   9 | `test_parse_rejects_each_magic_byte`       |   ✅   | Parse rejects each magic byte                                     |
|  10 | `test_parse_version_major_and_older_minor` |   ✅   | Parse version major and older minor                               |
|  11 | `test_parse_big_endian_submessage`         |   ✅   | Parse big endian submessage                                       |
|  12 | `test_parse_zero_length_terminates`        |   ✅   | Parse zero length terminates                                      |
|  13 | `test_parse_rejects_truncated_submessage`  |   ✅   | Parse rejects truncated submessage                                |
|  14 | `test_parse_without_callback`              |   ✅   | Parse without callback                                            |

</details>

---

## test_xmpp - native_xmpp - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/xmpp: the XMPP stanza builder + minimal reader._

|   # | Test                                         | Status | Description                                                                                       |
| --: | :------------------------------------------- | :----: | :------------------------------------------------------------------------------------------------ |
|   1 | `test_put_attr_fails_at_each_step`           |   ✅   | "<presence"(9) then ' '(10) "type"(14) '="'(16) "ab"(18) '"'(19) "/>"(21).                        |
|   2 | `test_message_fails_at_each_step`            |   ✅   | <message(8) to=(16) from=(28) type=(40) >(41) <body>(47) hi(49) </body>(56) </message>(66)        |
|   3 | `test_iq_fails_at_each_step`                 |   ✅   | <iq(3) type=(14) id=(23) >(24) <q/>(28) </iq>(33)                                                 |
|   4 | `test_stream_open_fails_at_each_step`        |   ✅   | Stream open fails at each step                                                                    |
|   5 | `test_readers_reject_null_out_and_zero_cap`  |   ✅   | Readers reject null out and zero cap                                                              |
|   6 | `test_readers_stop_at_end_of_buffer`         |   ✅   | Readers stop at end of buffer                                                                     |
|   7 | `test_attr_name_must_be_followed_by_equals`  |   ✅   | Attr name must be followed by equals                                                              |
|   8 | `test_escape`                                |   ✅   | Escape                                                                                            |
|   9 | `test_message`                               |   ✅   | Message                                                                                           |
|  10 | `test_presence`                              |   ✅   | Presence                                                                                          |
|  11 | `test_iq`                                    |   ✅   | Iq                                                                                                |
|  12 | `test_stanza_name`                           |   ✅   | Stanza name                                                                                       |
|  13 | `test_attr`                                  |   ✅   | Attr                                                                                              |
|  14 | `test_escape_all_entities_and_overflow`      |   ✅   | Every escapable character plus a normal one exercises each switch case in put_escaped.            |
|  15 | `test_builders_overflow_fail_closed`         |   ✅   | Builders overflow fail closed                                                                     |
|  16 | `test_builders_omit_optional_and_null_attrs` |   ✅   | body/child null skip the optional block; null attr values skip put_attr (its `!value` true side). |
|  17 | `test_stanza_name_edges`                     |   ✅   | Each terminator: '>', '/', space, tab, newline.                                                   |
|  18 | `test_attr_edges`                            |   ✅   | Single-quoted value + the leading-space substring guard (must not match 'to' inside 'xto').       |

</details>

---

## test_rawl2 - native_rawl2 - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/rawl2: the Ethernet II / 802.1Q frame codec + the FCS._

|   # | Test                                           | Status | Description                                                                   |
| --: | :--------------------------------------------- | :----: | :---------------------------------------------------------------------------- |
|   1 | `test_build_ethernet_ii`                       |   ✅   | Build ethernet ii                                                             |
|   2 | `test_build_vlan`                              |   ✅   | pcp 3, dei 0, vid 100 -> TCI 0x6064; PROFINET ethertype.                      |
|   3 | `test_parse`                                   |   ✅   | Parse                                                                         |
|   4 | `test_fcs_check_vector`                        |   ✅   | The canonical CRC-32 check value: CRC of "123456789" = 0xCBF43926.            |
|   5 | `test_eth_build_parse_guards`                  |   ✅   | Eth build parse guards                                                        |
|   6 | `test_eth_build_null_src_out_and_zero_payload` |   ✅   | pc_eth_build: null src, null out, zero-length payload (skips the copy), and a |
|   7 | `test_eth_parse_null_guards`                   |   ✅   | Eth parse null guards                                                         |

</details>

---

## test_spa_router - native_spa_router - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/spa_router: the single-page-app routing decision._

|   # | Test                                                  | Status | Description                                                                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_has_extension`                                  |   ✅   | Has extension                                                                                  |
|   2 | `test_route`                                          |   ✅   | Route                                                                                          |
|   3 | `test_route_ex_healthy_matches_the_plain_router`      |   ✅   | Route ex healthy matches the plain router                                                      |
|   4 | `test_missing_shell_falls_back`                       |   ✅   | Missing shell falls back                                                                       |
|   5 | `test_non_scripting_client_falls_back`                |   ✅   | Non scripting client falls back                                                                |
|   6 | `test_degraded_device_falls_back`                     |   ✅   | Degraded device falls back                                                                     |
|   7 | `test_api_still_passes_through_in_fallback`           |   ✅   | The property that makes the fallback worth having: its own controls POST to these endpoints,   |
|   8 | `test_assets_are_unaffected_by_degradation`           |   ✅   | An asset request stays an asset request; a real 404 is the caller's to report. Rewriting it to |
|   9 | `test_route_ex_null_ctx_degrades_to_the_plain_router` |   ✅   | Route ex null ctx degrades to the plain router                                                 |
|  10 | `test_stream_includes_only_passing_fragments`         |   ✅   | Stream includes only passing fragments                                                         |
|  11 | `test_stream_reflects_the_predicate_state`            |   ✅   | Stream reflects the predicate state                                                            |
|  12 | `test_stream_is_chunk_size_independent`               |   ✅   | The point of the cursor: a buffer smaller than a single fragment must still produce the exact  |
|  13 | `test_stream_all_excluded_emits_nothing`              |   ✅   | Stream all excluded emits nothing                                                              |
|  14 | `test_stream_empty_set_is_done_immediately`           |   ✅   | Stream empty set is done immediately                                                           |
|  15 | `test_stream_skips_a_null_body`                       |   ✅   | Stream skips a null body                                                                       |
|  16 | `test_stream_bad_args_do_not_crash`                   |   ✅   | Stream bad args do not crash                                                                   |
|  17 | `test_stream_not_done_mid_stream`                     |   ✅   | A valid, non-null stream that still has fragments left must report not-done - the counterpart  |

</details>

---

## test_goose - native_goose - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/goose: the IEC 61850 GOOSE BER PDU + Ethernet frame codec._

|   # | Test                                        | Status | Description                                                              |
| --: | :------------------------------------------ | :----: | :----------------------------------------------------------------------- |
|   1 | `test_pdu_structure`                        |   ✅   | Content is 42 octets (see goose.cpp field sizes); PDU = 61 2A <42> = 44. |
|   2 | `test_integer_leading_zero`                 |   ✅   | Integer leading zero                                                     |
|   3 | `test_frame`                                |   ✅   | Frame                                                                    |
|   4 | `test_goose_error_and_longform`             |   ✅   | Goose error and longform                                                 |
|   5 | `test_goose_null_string_true_bool_and_time` |   ✅   | Goose null string true bool and time                                     |
|   6 | `test_goose_pdu_field_boundary_failures`    |   ✅   | Goose pdu field boundary failures                                        |
|   7 | `test_goose_frame_null_guards`              |   ✅   | Goose frame null guards                                                  |
|   8 | `test_parse_roundtrip`                      |   ✅   | Parse roundtrip                                                          |
|   9 | `test_parse_rejects`                        |   ✅   | A non-GOOSE ethertype is rejected.                                       |

</details>

---

## test_mtconnect - native_mtconnect - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/mtconnect: the MTConnectStreams + MTConnectError document builders._

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_streams_document`                              |   ✅   | Streams document                              |
|   2 | `test_streams_escapes_value`                         |   ✅   | Streams escapes value                         |
|   3 | `test_error_document`                                |   ✅   | Error document                                |
|   4 | `test_overflow_returns_zero`                         |   ✅   | Overflow returns zero                         |
|   5 | `test_escape_gt_quote_and_overflow`                  |   ✅   | Escape gt quote and overflow                  |
|   6 | `test_devices_probe_document`                        |   ✅   | Devices probe document                        |
|   7 | `test_devices_escape_and_overflow`                   |   ✅   | Devices escape and overflow                   |
|   8 | `test_assets_document`                               |   ✅   | Assets document                               |
|   9 | `test_assets_escape_and_overflow`                    |   ✅   | Assets escape and overflow                    |
|  10 | `test_sample_buffer_and_query`                       |   ✅   | Sample buffer and query                       |
|  11 | `test_sample_buffer_eviction`                        |   ✅   | Sample buffer eviction                        |
|  12 | `test_sample_query_future_and_empty`                 |   ✅   | Sample query future and empty                 |
|  13 | `test_streams_null_strings`                          |   ✅   | Streams null strings                          |
|  14 | `test_builders_reject_null_buffer_and_zero_cap`      |   ✅   | Builders reject null buffer and zero cap      |
|  15 | `test_error_null_strings_and_capacity_sweep`         |   ✅   | Error null strings and capacity sweep         |
|  16 | `test_devices_null_ids_and_empty_optionals`          |   ✅   | Devices null ids and empty optionals          |
|  17 | `test_assets_empty_optionals_and_null_strings`       |   ✅   | Assets empty optionals and null strings       |
|  18 | `test_sample_buffer_null_and_truncated_fields`       |   ✅   | Sample buffer null and truncated fields       |
|  19 | `test_sample_query_rejects_null_buffer_and_zero_cap` |   ✅   | Sample query rejects null buffer and zero cap |

</details>

---

## test_wal - native_wal - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/wal: record framing + CRC32 + crash-recovery replay (the atomicity core)._

|   # | Test                                                | Status | Description                                                                    |
| --: | :-------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_crc32_known_vector`                           |   ✅   | The canonical CRC-32/ISO-HDLC check value for "123456789".                     |
|   2 | `test_encode_replay_roundtrip`                      |   ✅   | Encode replay roundtrip                                                        |
|   3 | `test_replay_recovers_to_last_good_on_corrupt_tail` |   ✅   | Corrupt a payload byte of the third record -> its CRC now fails.               |
|   4 | `test_replay_stops_on_truncated_tail`               |   ✅   | Simulate a power loss mid-write of record 2: only part of it made it to media. |
|   5 | `test_encode_capacity_and_empty_payload`            |   ✅   | Exactly fits a 3-byte payload.                                                 |
|   6 | `test_replay_empty_and_garbage`                     |   ✅   | Replay empty and garbage                                                       |
|   7 | `test_encode_null_out_fails`                        |   ✅   | Encode null out fails                                                          |
|   8 | `test_replay_null_callback`                         |   ✅   | Replay null callback                                                           |

</details>

---

## test_wal_store - native_wal - ✅ 35 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/storage/wal pc_wal_store: A/B superblock + checkpoint + mount/recover over a RAM device._

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

## test_nema_ts2 - native_nema_ts2 - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/nema_ts2: the TS 2 SDLC frame codec + CRC-16/X-25._

|   # | Test                                   | Status | Description                                                         |
| --: | :------------------------------------- | :----: | :------------------------------------------------------------------ |
|   1 | `test_crc_check_vector`                |   ✅   | CRC-16/X-25 canonical check value: CRC of "123456789" = 0x906E.     |
|   2 | `test_build_and_parse`                 |   ✅   | Build and parse                                                     |
|   3 | `test_no_data_frame`                   |   ✅   | No data frame                                                       |
|   4 | `test_parse_rejects_bad_crc_and_short` |   ✅   | Parse rejects bad crc and short                                     |
|   5 | `test_build_rejects_bad_args`          |   ✅   | null output buffer.                                                 |
|   6 | `test_build_rejects_undersized_cap`    |   ✅   | frame would be 3 + 2 + 2 = 7 bytes; cap of 6 is one byte too small. |
|   7 | `test_parse_rejects_null_args`         |   ✅   | Parse rejects null args                                             |

</details>

---

## test_snp - native_snp - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/snp: the GE Fanuc SNP serial frame codec._

|   # | Test                    | Status | Description                                            |
| --: | :---------------------- | :----: | :----------------------------------------------------- |
|   1 | `test_bcc`              |   ✅   | sum = 0x01+0x03+0x10+0x20+0x30 = 0x64.                 |
|   2 | `test_build_and_parse`  |   ✅   | [01][03][10 20 30][BCC] ; BCC = 01+03+10+20+30 = 0x64. |
|   3 | `test_empty_data`       |   ✅   | Empty data                                             |
|   4 | `test_parse_rejects`    |   ✅   | Parse rejects                                          |
|   5 | `test_snp_build_guards` |   ✅   | Snp build guards                                       |
|   6 | `test_snp_parse_guards` |   ✅   | Snp parse guards                                       |

</details>

---

## test_directnet - native_directnet - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/directnet: the DirectNET serial frame codec._

|   # | Test                            | Status | Description                                                               |
| --: | :------------------------------ | :----: | :------------------------------------------------------------------------ |
|   1 | `test_lrc`                      |   ✅   | Lrc                                                                       |
|   2 | `test_header_frame`             |   ✅   | SOH(1) + slave(2) + type(1) + addr(4) + blocks(2) + ETB(1) + LRC(1) = 12. |
|   3 | `test_data_frame_roundtrip`     |   ✅   | STX + ABCD + ETX + LRC = 7.                                               |
|   4 | `test_data_parse_rejects`       |   ✅   | Data parse rejects                                                        |
|   5 | `test_header_hex_letters`       |   ✅   | Header hex letters                                                        |
|   6 | `test_data_frame_empty_payload` |   ✅   | STX + ETX + LRC = 3.                                                      |
|   7 | `test_data_parse_null_outputs`  |   ✅   | Data parse null outputs                                                   |
|   8 | `test_guards`                   |   ✅   | Guards                                                                    |

</details>

---

## test_profinet - native_profinet - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/profinet: the PROFINET DCP frame codec._

|   # | Test                             | Status | Description                                                                                    |
| --: | :------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_header_roundtrip`          |   ✅   | Header roundtrip                                                                               |
|   2 | `test_block_even_padding`        |   ✅   | NameOfStation "plc" is 3 bytes (odd) -> padded to an even total, filler not counted in length. |
|   3 | `test_walk_blocks`               |   ✅   | Walk blocks                                                                                    |
|   4 | `test_walk_rejects_truncated`    |   ✅   | blockLength claims 10 but only 2 value bytes present.                                          |
|   5 | `test_pn_guards`                 |   ✅   | Pn guards                                                                                      |
|   6 | `test_block_zero_length_value`   |   ✅   | value_len == 0 (value may be null) is legal: exercises the "value_len is falsy" path in        |
|   7 | `test_block_value_len_too_large` |   ✅   | value_len > 0xFFFF cannot fit in the 16-bit blockLength field, regardless of cap.              |
|   8 | `test_parse_header_null_out`     |   ✅   | Valid frame/len but a null destination struct.                                                 |
|   9 | `test_walk_null_callback`        |   ✅   | cb == nullptr over a well-formed (non-truncated) block list: the walk still succeeds, it just  |

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

## test_mms - native_mms - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/mms: the IEC 61850 MMS Read PDU codec._

|   # | Test                                           | Status | Description                                                                              |
| --: | :--------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_read_request_structure`                  |   ✅   | Read request structure                                                                   |
|   2 | `test_read_request_parse`                      |   ✅   | Read request parse                                                                       |
|   3 | `test_read_response_roundtrip`                 |   ✅   | A caller-encoded Data value: boolean-ish [3] BOOLEAN true -> 83 01 FF (context Data).    |
|   4 | `test_parse_rejects_bad_tag`                   |   ✅   | Parse rejects bad tag                                                                    |
|   5 | `test_invoke_id_zero_and_msb`                  |   ✅   | id 0 -> int_content emits {0x00}; round-trips back to 0.                                 |
|   6 | `test_read_request_bad_args`                   |   ✅   | Read request bad args                                                                    |
|   7 | `test_read_request_long_name_long_form`        |   ✅   | Read request long name long form                                                         |
|   8 | `test_read_response_bad_args_and_overflow`     |   ✅   | data_len set but data null -> reject.                                                    |
|   9 | `test_parse_null_and_short`                    |   ✅   | Parse null and short                                                                     |
|  10 | `test_parse_malformed`                         |   ✅   | Outer length in long form but the count byte is malformed (nb == 0).                     |
|  11 | `test_parse_no_service`                        |   ✅   | Parse no service                                                                         |
|  12 | `test_read_response_rejects_over_long_payload` |   ✅   | Read response rejects over long payload                                                  |
|  13 | `test_read_response_three_octet_outer_length`  |   ✅   | Read response three octet outer length                                                   |
|  14 | `test_read_response_empty_data`                |   ✅   | Read response empty data                                                                 |
|  15 | `test_parse_confirmed_error_tag`               |   ✅   | Parse confirmed error tag                                                                |
|  16 | `test_parse_length_field_guards`               |   ✅   | Outer length long form with nb == 3: unsupported (only 1- and 2-byte forms are decoded). |
|  17 | `test_parse_invoke_id_truncated`               |   ✅   | Zero-length body: off + 2 is already past the 2-octet PDU.                               |

</details>

---

## test_cclink - native_cclink - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/cclink: the CC-Link cyclic frame codec + process-image accessors._

|   # | Test                             | Status | Description                                                           |
| --: | :------------------------------- | :----: | :-------------------------------------------------------------------- |
|   1 | `test_sum`                       |   ✅   | Sum                                                                   |
|   2 | `test_build_and_parse`           |   ✅   | Build and parse                                                       |
|   3 | `test_bit_accessors`             |   ✅   | Bit accessors                                                         |
|   4 | `test_parse_rejects`             |   ✅   | Parse rejects                                                         |
|   5 | `test_build_and_accessor_guards` |   ✅   | Build and accessor guards                                             |
|   6 | `test_build_null_args`           |   ✅   | out == nullptr -> rejected before any other check.                    |
|   7 | `test_build_zero_bit_len`        |   ✅   | bit_len == 0 (with non-empty word data) on a successful build path.   |
|   8 | `test_parse_null_args`           |   ✅   | Parse null args                                                       |
|   9 | `test_parse_no_payload`          |   ✅   | station + command + checksum only -> body <= 2 -> payload == nullptr. |
|  10 | `test_accessor_null_ptrs`        |   ✅   | Accessor null ptrs                                                    |

</details>

---

## test_powerlink - native_powerlink - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/powerlink: the Ethernet POWERLINK basic frame codec._

|   # | Test                           | Status | Description                                                                                    |
| --: | :----------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_soc`                     |   ✅   | Soc                                                                                            |
|   2 | `test_preq_pres_roundtrip`     |   ✅   | PReq: MN (240) -> CN 5, carrying output PDO.                                                   |
|   3 | `test_soa_asnd`                |   ✅   | SoA: MN -> broadcast, opening the async phase, carrying the SoA field block.                   |
|   4 | `test_parse_rejects`           |   ✅   | Parse rejects                                                                                  |
|   5 | `test_epl_build_guards`        |   ✅   | Epl build guards                                                                               |
|   6 | `test_epl_build_null_out`      |   ✅   | Null output buffer must be rejected on its own (independent of the payload_len/payload check). |
|   7 | `test_parse_null_args`         |   ✅   | Parse null args                                                                                |
|   8 | `test_parse_all_message_types` |   ✅   | Exactly len == 3 (no payload): exercises the len>3 ternary's false arm too.                    |

</details>

---

## test_sercos - native_sercos - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/sercos: the SERCOS III telegram + IDN codec._

|   # | Test                               | Status | Description                                                                     |
| --: | :--------------------------------- | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_idn_roundtrip`               |   ✅   | S-0-0100 (velocity loop): S-parameter, set 0, block 100.                        |
|   2 | `test_telegram_roundtrip`          |   ✅   | Telegram roundtrip                                                              |
|   3 | `test_at_telegram_and_rejects`     |   ✅   | At telegram and rejects                                                         |
|   4 | `test_sercos_build_guards`         |   ✅   | Sercos build guards                                                             |
|   5 | `test_idn_parse_null_out_pointers` |   ✅   | Doc contract: "any out-pointer may be null" - exercise every pointer being null |
|   6 | `test_sercos_parse_null_guards`    |   ✅   | Sercos parse null guards                                                        |

</details>

---

## test_profibus - native_profibus - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/profibus: the PROFIBUS-DP FDL telegram codec._

|   # | Test                                       | Status | Description                                                                                     |
| --: | :----------------------------------------- | :----: | :---------------------------------------------------------------------------------------------- |
|   1 | `test_fcs`                                 |   ✅   | Fcs                                                                                             |
|   2 | `test_sd1`                                 |   ✅   | SD1 DA SA FC FCS ED : 10 03 02 49 4E 16                                                         |
|   3 | `test_sd3_roundtrip`                       |   ✅   | FCS = (0x05 + 0x02 + 0x7C + sum(data)) mod 256 = 0xE7.                                          |
|   4 | `test_sd2_roundtrip`                       |   ✅   | le = 3 + 3 = 6; total = 4 + 6 + 2 = 12.                                                         |
|   5 | `test_parse_rejects`                       |   ✅   | Parse rejects                                                                                   |
|   6 | `test_build_and_parse_guard_subconditions` |   ✅   | Build guards: null out and a capacity below the frame size fail closed.                         |
|   7 | `test_sd2_build_more_guards`               |   ✅   | Null out pointer fails closed before any other subcondition is checked.                         |
|   8 | `test_sd2_zero_length_data`                |   ✅   | data_len == 0 (data may be null): the memcpy is skipped and the parsed data pointer stays null. |
|   9 | `test_sd1_parse_corruption`                |   ✅   | FCS mismatch (ED still correct) fails closed.                                                   |
|  10 | `test_parse_unknown_sd`                    |   ✅   | Neither SD1 nor SD2: falls through both checks and fails closed at the end.                     |
|  11 | `test_sd2_parse_length_guards`             |   ✅   | len >= 6 (passes the top-level guard) but < 9 fails closed before the LE/LEr checks.            |

</details>

---

## test_lonworks - native_lonworks - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/lonworks: the LonTalk NV PDU + SNVT scalar codec._

|   # | Test                                   | Status | Description                                                                    |
| --: | :------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_nv_pdu_roundtrip`                |   ✅   | selector 0x1234 is 14-bit -> stored 0x12 0x34.                                 |
|   2 | `test_nv_selector_masked_to_14_bits`   |   ✅   | The top two bits of the selector byte are not part of the 14-bit value.        |
|   3 | `test_snvt_temp`                       |   ✅   | Snvt temp                                                                      |
|   4 | `test_snvt_switch`                     |   ✅   | Snvt switch                                                                    |
|   5 | `test_snvt_clamps_and_guards`          |   ✅   | Snvt clamps and guards                                                         |
|   6 | `test_nv_build_null_guards`            |   ✅   | out == nullptr guard branch.                                                   |
|   7 | `test_nv_parse_null_guards`            |   ✅   | pdu == nullptr guard branch.                                                   |
|   8 | `test_snvt_temp_clamp_high_in_range`   |   ✅   | (celsius + 273.15) * 100 = 47315, which is inside int32_t range so the cast is |
|   9 | `test_snvt_switch_decode_null_outputs` |   ✅   | percent == nullptr: only state should be written.                              |

</details>

---

## test_mbplus - native_mbplus - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/mbplus: the Modbus Plus HDLC token-bus frame codec._

|   # | Test                              | Status | Description                                           |
| --: | :-------------------------------- | :----: | :---------------------------------------------------- |
|   1 | `test_crc_check_vector`           |   ✅   | CRC-16/X-25 check value: CRC of "123456789" = 0x906E. |
|   2 | `test_build_and_parse`            |   ✅   | 7E 05 00 10 03 00 CRClo CRChi 7E = 9 bytes.           |
|   3 | `test_token_frame_no_payload`     |   ✅   | Token frame no payload                                |
|   4 | `test_next_token_ring`            |   ✅   | Next token ring                                       |
|   5 | `test_parse_rejects`              |   ✅   | Parse rejects                                         |
|   6 | `test_build_parse_and_token_wrap` |   ✅   | Build parse and token wrap                            |
|   7 | `test_mbplus_null_and_flag_edges` |   ✅   | Null output buffer at build.                          |

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

## test_iccp - native_iccp - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/iccp: the ICCP / TASE.2 Data_Value codec._

|   # | Test                                  | Status | Description                                                                          |
| --: | :------------------------------------ | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_state_q_no_time`                |   ✅   | A2 { 85 01 <sq> } ; sq = (ON=2)<<6                                                   | valid(0) = 0x80. -> A2 03 85 01 80 |
|   2 | `test_state_q_with_time`              |   ✅   | State q with time                                                                    |
|   3 | `test_real_q`                         |   ✅   | Real q                                                                               |
|   4 | `test_real_q_negative`                |   ✅   | -1 -> minimal two's complement INTEGER 02 01 FF.                                     |
|   5 | `test_state_and_real_q_guards`        |   ✅   | State and real q guards                                                              |
|   6 | `test_real_q_positive_needs_pad_byte` |   ✅   | 128 = 0x80: its low byte alone has the sign bit set, so the minimal two's-complement |

</details>

---

## test_wave - native_wave - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/wave: the IEEE 1609 WSMP + 1609.2 envelope + PSID codec._

|   # | Test                             | Status | Description               |
| --: | :------------------------------- | :----: | :------------------------ |
|   1 | `test_psid_p_encoding`           |   ✅   | 1-octet: 0x20 -> 20.      |
|   2 | `test_psid_four_octet_and_caps`  |   ✅   | Psid four octet and caps  |
|   3 | `test_psid_decode_guards`        |   ✅   | Psid decode guards        |
|   4 | `test_wsmp_build_guards`         |   ✅   | Wsmp build guards         |
|   5 | `test_wsmp_parse_more_guards`    |   ✅   | Wsmp parse more guards    |
|   6 | `test_1609dot2_wrap_guards`      |   ✅   | 1609dot2 wrap guards      |
|   7 | `test_wsmp_parse_null_out`       |   ✅   | Wsmp parse null out       |
|   8 | `test_wsmp_zero_length_payload`  |   ✅   | Wsmp zero length payload  |
|   9 | `test_1609dot2_wrap_zero_length` |   ✅   | 1609dot2 wrap zero length |
|  10 | `test_wsmp_roundtrip`            |   ✅   | Wsmp roundtrip            |
|  11 | `test_1609dot2_wrap`             |   ✅   | 1609dot2 wrap             |
|  12 | `test_wsmp_parse_rejects`        |   ✅   | Wsmp parse rejects        |

</details>

---

## test_ocit - native_ocit - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/ocit: the OCIT-Outstations message codec._

|   # | Test                                     | Status | Description                                                                         |
| --: | :--------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_build_and_parse`                   |   ✅   | Build and parse                                                                     |
|   2 | `test_set_u16_helper`                    |   ✅   | Set u16 helper                                                                      |
|   3 | `test_get_no_value`                      |   ✅   | Get no value                                                                        |
|   4 | `test_parse_rejects_short`               |   ✅   | Parse rejects short                                                                 |
|   5 | `test_build_rejects_null_out`            |   ✅   | Build rejects null out                                                              |
|   6 | `test_build_rejects_null_value_with_len` |   ✅   | Build rejects null value with len                                                   |
|   7 | `test_build_rejects_overflow`            |   ✅   | Build rejects overflow                                                              |
|   8 | `test_parse_rejects_null_msg`            |   ✅   | Parse rejects null msg                                                              |
|   9 | `test_parse_rejects_null_out`            |   ✅   | Parse rejects null out                                                              |
|  10 | `test_value_u16_rejects_null_msg`        |   ✅   | Value u16 rejects null msg                                                          |
|  11 | `test_value_u16_rejects_wrong_type`      |   ✅   | Value u16 rejects wrong type                                                        |
|  12 | `test_value_u16_rejects_null_value_ptr`  |   ✅   | Hand-built OcitMsg (not reachable via pc_ocit_parse) exercising the !m->value guard |

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

## test_sb_modbus - native_sb_modbus - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the Modbus-master southbound driver adapter_

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
|   8 | `test_acquire_recycle_with_null_evicted_id`       |   ✅   | Fill the pool, then force a recycle while passing evicted_id == nullptr, exercising the     |
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

## test_happy_eyeballs - native_happy_eyeballs - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/happy_eyeballs: RFC 6724 ordering + RFC 8305 family interleave + attempt gate._

|   # | Test                                         | Status | Description                                                                                  |
| --: | :------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_pref_order`                            |   ✅   | Global outranks link-local outranks loopback; within global, native v6 outranks v4.          |
|   2 | `test_order_and_interleave`                  |   ✅   | Two global v6 + one global v4, given v4-first: sort puts v6 ahead, interleave alternates.    |
|   3 | `test_order_single_family`                   |   ✅   | All v4: interleave is a no-op, order stays preference-sorted (global before private).        |
|   4 | `test_attempt_due`                           |   ✅   | Attempt due                                                                                  |
|   5 | `test_pref_scopes_and_order_edges`           |   ✅   | Exercise the multicast + unspecified scope arms of pc_he_pref (values are pc_ip-classified). |
|   6 | `test_pref_null_and_none`                    |   ✅   | Null pointer and an empty (PC_IP_NONE) address both hit the sentinel-return arm.             |
|   7 | `test_order_null_list_is_noop`               |   ✅   | A null list must return immediately without dereferencing it.                                |
|   8 | `test_order_v4_mapped_treated_as_v4`         |   ✅   | ::ffff:a.b.c.d is family V6 but eff_is_v6() must treat it as V4 for interleave purposes.     |
|   9 | `test_order_oversized_list_skips_interleave` |   ✅   | A list longer than PC_HE_MAX (16) is stable-sorted but the interleave step is skipped        |
|  10 | `test_order_family_imbalance_drains_v6`      |   ✅   | 3 global v6 + 1 global v4, v6-first: v4 exhausts after one pick and the "preferred family    |

</details>

---

## test_wifi_sniffer - native_wifi_sniffer - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Host tests for services/wifi_sniffer: 802.11 header decode, traffic tally, roaming decision._

|   # | Test                                            | Status | Description                                                                                   |
| --: | :---------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_parse_data`                               |   ✅   | Parse data                                                                                    |
|   2 | `test_parse_beacon`                             |   ✅   | Parse beacon                                                                                  |
|   3 | `test_parse_ctrl_short`                         |   ✅   | Parse ctrl short                                                                              |
|   4 | `test_stats`                                    |   ✅   | Stats                                                                                         |
|   5 | `test_roam`                                     |   ✅   | Current -80 dBm, candidate -70 dBm, 8 dB hysteresis: 10 > 8 -> roam.                          |
|   6 | `test_stats_add_null_and_default_type`          |   ✅   | Stats add null and default type                                                               |
|   7 | `test_scan_hops_and_wraps`                      |   ✅   | Scan hops and wraps                                                                           |
|   8 | `test_scan_clamps_and_single_channel`           |   ✅   | Scan clamps and single channel                                                                |
|   9 | `test_scan_wrapsafe_across_millis_rollover`     |   ✅   | Scan wrapsafe across millis rollover                                                          |
|  10 | `test_scan_null_guards`                         |   ✅   | Scan null guards                                                                              |
|  11 | `test_survey_tracks_best_rssi_per_channel`      |   ✅   | Survey tracks best rssi per channel                                                           |
|  12 | `test_survey_out_of_range_ignored`              |   ✅   | Survey out of range ignored                                                                   |
|  13 | `test_survey_best_picks_strongest_and_excludes` |   ✅   | Survey best picks strongest and excludes                                                      |
|  14 | `test_survey_feeds_roam_decision`               |   ✅   | The end-to-end decision a channel-agility roam makes: survey -> best candidate -> hysteresis. |
|  15 | `test_survey_add_null_frame_and_short_naddr`    |   ✅   | Survey add null frame and short naddr                                                         |
|  16 | `test_survey_best_null_out_params`              |   ✅   | A caller that only wants the bool (does it need to roam at all?) may pass null outs.          |
|  17 | `test_survey_reset_clamps_count`                |   ✅   | Survey reset clamps count                                                                     |

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

## test_qpack - native_qpack - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the QPACK codec (network_drivers/presentation/http/http3/qpack, RFC 9204): the_

|   # | Test                                    | Status | Description                                                                                |
| --: | :-------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_qpack_field_int_truncation`       |   ✅   | Indexed Field Line (T=1 static), prefix-6 integer 63 (all-ones) with no continuation byte: |
|   2 | `test_appendix_b1_decode`               |   ✅   | Appendix b1 decode                                                                         |
|   3 | `test_encode_indexed`                   |   ✅   | Encode indexed                                                                             |
|   4 | `test_encode_nameref_roundtrip`         |   ✅   | Encode nameref roundtrip                                                                   |
|   5 | `test_literal_name`                     |   ✅   | Literal name                                                                               |
|   6 | `test_full_section`                     |   ✅   | Full section                                                                               |
|   7 | `test_reject_dynamic`                   |   ✅   | Reject dynamic                                                                             |
|   8 | `test_encode_edges`                     |   ✅   | Encode edges                                                                               |
|   9 | `test_decode_errors`                    |   ✅   | Decode errors                                                                              |
|  10 | `test_value_string_paths`               |   ✅   | Value marked Huffman (0x81 = H, len 1) but 0xFF is not a valid single-byte code.           |
|  11 | `test_qpack_more_encode_decode_paths`   |   ✅   | A short literal name that does not Huffman-compress takes the raw memcpy path.             |
|  12 | `test_qpack_emit_fail_and_namelen_past` |   ✅   | Literal Field Line with Name Reference + a valid value, but the emit callback rejects it.  |

</details>

---

## test_quic_packet - native_quic_packet - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the QUIC packet header + packet-number codec (network_drivers/presentation/http/http3/_

|   # | Test                         | Status | Description                                                                               |
| --: | :--------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_long_header_roundtrip` |   ✅   | Long header roundtrip                                                                     |
|   2 | `test_version_negotiation`   |   ✅   | Version negotiation                                                                       |
|   3 | `test_short_header_parse`    |   ✅   | Short header parse                                                                        |
|   4 | `test_pn_encode`             |   ✅   | RFC 9000 A.2: acked 0xabe8b3, sending 0xac5c02 -> 16-bit encoding.                        |
|   5 | `test_pn_decode`             |   ✅   | RFC 9000 A.3: largest 0xa82f30ea, 16-bit truncated 0x9b32 -> 0xa82f9b32.                  |
|   6 | `test_pn_decode_wraparound`  |   ✅   | largest_pn=199 -> expected=200, pn_nbits=8 (window 256, half-window 128). Naive candidate |
|   7 | `test_reject`                |   ✅   | Destination Connection ID length 21 (> 20) must be dropped.                               |
|   8 | `test_build_guards`          |   ✅   | Build guards                                                                              |
|   9 | `test_short_header_guards`   |   ✅   | Short header guards                                                                       |

</details>

---

## test_quic_frame - native_quic_frame - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the QUIC frame codec (network_drivers/presentation/http/http3/pc_quic_frame, RFC 9000_

|   # | Test                              | Status | Description                                                                                          |
| --: | :-------------------------------- | :----: | :--------------------------------------------------------------------------------------------------- |
|   1 | `test_frame_edge_guards`          |   ✅   | STREAM with LEN set but the Length varint is absent -> rejected at the length read.                  |
|   2 | `test_frame_truncation_sweep`     |   ✅   | ACK with ECN: largest 60, delay 5, 2 ranges (gap/len pairs), then the three ECN counts.              |
|   3 | `test_builder_capacity_sweep`     |   ✅   | ACK: type + largest(2 octets) + delay(2) + range count + first range.                                |
|   4 | `test_builders_with_empty_bodies` |   ✅   | CRYPTO carrying no data: header only, and it parses back with length 0.                              |
|   5 | `test_simple_frames`              |   ✅   | Simple frames                                                                                        |
|   6 | `test_ack`                        |   ✅   | Ack                                                                                                  |
|   7 | `test_ack_multi_range`            |   ✅   | type 0x03, largest 60, delay 5, range_count 2, first_range 3, [gap 2,len 4][gap 1,len 1], ECN 1/2/0. |
|   8 | `test_crypto`                     |   ✅   | Crypto                                                                                               |
|   9 | `test_stream`                     |   ✅   | With offset + FIN.                                                                                   |
|  10 | `test_max_data_and_close`         |   ✅   | Max data and close                                                                                   |
|  11 | `test_sequence_and_truncation`    |   ✅   | A packet payload: PADDING, PING, then a CRYPTO frame - parse them in order.                          |
|  12 | `test_builder_overflow`           |   ✅   | Builder overflow                                                                                     |
|  13 | `test_parse_errors`               |   ✅   | Parse errors                                                                                         |
|  14 | `test_skip_and_extra_frames`      |   ✅   | One-varint frames: type followed by a single varint.                                                 |

</details>

---

## test_dtls_tls13 - native_dtls_tls13 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_TLS 1.3 messages the DTLS 1.3 handshake adds to pc_tls13_msg (RFC 8446 §4.1.4 / §4.4.1): the_

|   # | Test                                    | Status | Description                                                                                         |
| --: | :-------------------------------------- | :----: | :-------------------------------------------------------------------------------------------------- |
|   1 | `test_parse_server_cert_type_malformed` |   ✅   | Empty extension body: there is not even a list-length byte.                                         |
|   2 | `test_quic_encrypted_extensions_rpk`    |   ✅   | Quic encrypted extensions rpk                                                                       |
|   3 | `test_parse_every_extension_arm`        |   ✅   | Parse every extension arm                                                                           |
|   4 | `test_hrr_magic_symbol`                 |   ✅   | The builder and the RFC constant agree.                                                             |
|   5 | `test_hrr_build_kat`                    |   ✅   | Hrr build kat                                                                                       |
|   6 | `test_hrr_echoes_session_id`            |   ✅   | Hrr echoes session id                                                                               |
|   7 | `test_message_hash`                     |   ✅   | Message hash                                                                                        |
|   8 | `test_empty_encrypted_extensions`       |   ✅   | Empty encrypted extensions                                                                          |
|   9 | `test_client_hello_cookie_parse`        |   ✅   | Assemble a minimal but well-formed ClientHello with exactly one extension (cookie).                 |
|  10 | `test_ed25519_spki`                     |   ✅   | Ed25519 spki                                                                                        |
|  11 | `test_build_certificate_rpk`            |   ✅   | Derive a real public key from a seed, so the test spans seed -> pubkey -> SPKI -> Certificate.      |
|  12 | `test_ee_rpk_extension`                 |   ✅   | The empty (DTLS-profile) EncryptedExtensions with RPK selected carries server_certificate_type.     |
|  13 | `test_parse_server_cert_type_rpk`       |   ✅   | server_certificate_type list [X509(0), RawPublicKey(2)]: the client accepts a RawPublicKey from us. |
|  14 | `test_parse_server_cert_type_x509_only` |   ✅   | A list with only X509(0): no RPK offer.                                                             |

</details>

---

## test_quic_tp - native_quic_tp - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the QUIC transport-parameters codec (network_drivers/presentation/http/http3/pc_quic_tp;_

|   # | Test                                       | Status | Description                                                                                  |
| --: | :----------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_defaults`                            |   ✅   | Defaults                                                                                     |
|   2 | `test_roundtrip`                           |   ✅   | Roundtrip                                                                                    |
|   3 | `test_parse_bytes`                         |   ✅   | Parse bytes                                                                                  |
|   4 | `test_skip_unknown`                        |   ✅   | id 0x1a (unknown), len 3, value 01 02 03; then 04 01 20 (initial_max_data = 0x20 = 32).      |
|   5 | `test_reject_duplicate`                    |   ✅   | initial_max_data twice.                                                                      |
|   6 | `test_reject_oversized_cid`                |   ✅   | original_destination_connection_id with a 21-byte value (max is 20).                         |
|   7 | `test_reject_bad_values`                   |   ✅   | active_connection_id_limit = 1 (must be >= 2).                                               |
|   8 | `test_quic_tp_more_paths`                  |   ✅   | Encode overflow: a CID param's ID varint, length varint, and value each fail at a tight cap. |
|   9 | `test_encode_cid_ok_chain_gaps`            |   ✅   | All three connection-ID params present; cap = 0 fails original_dcid immediately, so both the |
|  10 | `test_encode_varint_param_overflow_gaps`   |   ✅   | Encode varint param overflow gaps                                                            |
|  11 | `test_encode_disable_migration_gaps`       |   ✅   | Encode disable migration gaps                                                                |
|  12 | `test_parse_id_decode_and_large_id`        |   ✅   | Announces an 8-octet varint (top 2 bits = 11) with zero bytes available.                     |
|  13 | `test_parse_range_check_value_decode_gaps` |   ✅   | Parse range check value decode gaps                                                          |

</details>

---

## test_tls13_msg - native_tls13_msg - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the TLS 1.3 handshake messages (network_drivers/presentation/http/http3/pc_tls13_msg;_

|   # | Test                                           | Status | Description                                                                                     |
| --: | :--------------------------------------------- | :----: | :---------------------------------------------------------------------------------------------- |
|   1 | `test_tls13_extension_and_truncation_coverage` |   ✅   | Body ends right after cipher_suites -> r_u8(compression_methods length) truncates.              |
|   2 | `test_tls13_dtls_client_hello_shape`           |   ✅   | supported_versions offering DTLS 1.3 (0xFEFC).                                                  |
|   3 | `test_tls13_client_hello_field_truncations`    |   ✅   | No bytes at all: even the handshake type cannot be read.                                        |
|   4 | `test_tls13_extension_body_guards`             |   ✅   | supported_groups: declared list length (255) exceeds the extension body.                        |
|   5 | `test_tls13_builders_dtls_codepoints`          |   ✅   | Tls13 builders dtls codepoints                                                                  |
|   6 | `test_tls13_builder_overflow_guards`           |   ✅   | cookie_len + 2 must fit a uint16: refused before anything is written.                           |
|   7 | `test_tls13_cert_verify_client_context`        |   ✅   | Tls13 cert verify client context                                                                |
|   8 | `test_tls13_malformed_extensions`              |   ✅   | Tls13 malformed extensions                                                                      |
|   9 | `test_tls13_parse_guards`                      |   ✅   | Tls13 parse guards                                                                              |
|  10 | `test_tls13_builder_cap_guards`                |   ✅   | Tls13 builder cap guards                                                                        |
|  11 | `test_parse_client_hello`                      |   ✅   | Parse client hello                                                                              |
|  12 | `test_build_server_hello`                      |   ✅   | Build server hello                                                                              |
|  13 | `test_tls13_build_server_hello_conn_id`        |   ✅   | Tls13 build server hello conn id                                                                |
|  14 | `test_build_certificate`                       |   ✅   | Reconstruct the DER cert from the expected message: strip the 11-byte prefix and 2-byte suffix. |
|  15 | `test_build_finished`                          |   ✅   | Build finished                                                                                  |
|  16 | `test_encrypted_extensions`                    |   ✅   | Encrypted extensions                                                                            |
|  17 | `test_cert_verify_content`                     |   ✅   | Cert verify content                                                                             |
|  18 | `test_cert_verify_sign_roundtrip`              |   ✅   | Cert verify sign roundtrip                                                                      |

</details>

---

## test_ssh_aesgcm - native_ssh_aesgcm - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

_Unit tests for the AES-256-GCM AEAD used by aes256-gcm@openssh.com (RFC 5647):_

|   # | Test                                      | Status | Description                                                                                      |
| --: | :---------------------------------------- | :----: | :----------------------------------------------------------------------------------------------- |
|   1 | `test_aesgcm_nist_tc16_seal`              |   ✅   | Aesgcm nist tc16 seal                                                                            |
|   2 | `test_aesgcm_nist_tc16_open`              |   ✅   | Aesgcm nist tc16 open                                                                            |
|   3 | `test_aesgcm_invocation_counter_advances` |   ✅   | Aesgcm invocation counter advances                                                               |
|   4 | `test_aesgcm_iv_counter_carries`          |   ✅   | The invocation-counter advance is now the caller's job (pc_aesgcm_iv_increment); this checks the |
|   5 | `test_aesgcm_gctr_counter_byte_carry`     |   ✅   | Aesgcm gctr counter byte carry                                                                   |

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
