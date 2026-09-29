# Test Report

**Generated:** 2026-09-29 20:19:22
**Command:** `harness.py run` over 411 native envs
**Result:** ✅ 4784 passed - 0s

---

## Summary

| Suite                   | Environment                 | Tests | Status | Duration |
| :---------------------- | :-------------------------- | ----: | :----: | -------: |
| test_ad9238             | native_ad9238               |    21 |   ✅   |        - |
| test_ads                | native_ads                  |    15 |   ✅   |        - |
| test_ads1115            | native_ads1115              |    27 |   ✅   |        - |
| test_amqp               | native_amqp                 |    13 |   ✅   |        - |
| test_arena              | native_arena                |    27 |   ✅   |        - |
| test_atc                | native_atc                  |    13 |   ✅   |        - |
| test_audit_log          | native_audit_log            |    19 |   ✅   |        - |
| test_auth_lockout       | native_auth_lockout         |    15 |   ✅   |        - |
| test_bacnet             | native_bacnet               |    16 |   ✅   |        - |
| test_base64             | native_codec_base64         |    10 |   ✅   |        - |
| test_base64             | native_codec_base64_scalar  |    10 |   ✅   |        - |
| test_bitio              | native_bitio                |    12 |   ✅   |        - |
| test_bitio              | native_mmgr_bitio           |    12 |   ✅   |        - |
| test_ble_gatt           | native_ble_gatt_att         |    10 |   ✅   |        - |
| test_bus_capture        | native_bus_capture          |    13 |   ✅   |        - |
| test_bus_wire           | native_bus_wire             |    17 |   ✅   |        - |
| test_bytes              | native_bytes                |    17 |   ✅   |        - |
| test_bytes              | native_mmgr_bytes           |    17 |   ✅   |        - |
| test_c37118             | native_c37118               |    12 |   ✅   |        - |
| test_canopen            | native_canopen              |    14 |   ✅   |        - |
| test_cbor               | native_codec_cbor           |    17 |   ✅   |        - |
| test_cc1101             | native_cc1101               |    18 |   ✅   |        - |
| test_cclink             | native_cclink               |    13 |   ✅   |        - |
| test_cia402             | native_cia402               |    11 |   ✅   |        - |
| test_cip                | native_cip                  |    12 |   ✅   |        - |
| test_client             | native_client               |     8 |   ✅   |        - |
| test_clock              | native_clock                |     7 |   ✅   |        - |
| test_cloudevents        | native_cloudevents          |    13 |   ✅   |        - |
| test_coap               | native_coap                 |    58 |   ✅   |        - |
| test_coap               | native_coap_observe         |    66 |   ✅   |        - |
| test_compliance         | native_compliance           |    15 |   ✅   |        - |
| test_config_io          | native_config_io            |    12 |   ✅   |        - |
| test_config_store       | native_config_store         |    11 |   ✅   |        - |
| test_control            | native_system_control       |    20 |   ✅   |        - |
| test_cotp               | native_cotp                 |    12 |   ✅   |        - |
| test_x509               | native_x509                 |    16 |   ✅   |        - |
| test_ct_eq              | native_ct_eq                |     8 |   ✅   |        - |
| test_ct_eq              | native_ct_eq_unit           |     8 |   ✅   |        - |
| test_dbm                | native_dbm                  |    23 |   ✅   |        - |
| test_dds                | native_dds_rtps             |    16 |   ✅   |        - |
| test_der                | native_der                  |    32 |   ✅   |        - |
| test_deflate            | native_codec_deflate        |    16 |   ✅   |        - |
| test_device_id          | native_device_id            |     7 |   ✅   |        - |
| test_devicenet          | native_devicenet            |    15 |   ✅   |        - |
| test_df1                | native_df1                  |    12 |   ✅   |        - |
| test_diffserv           | native_diffserv             |    10 |   ✅   |        - |
| test_digest_vectors     | native_sha256_kat           |     9 |   ✅   |        - |
| test_aes_block          | native_aes_block            |     7 |   ✅   |        - |
| test_digest_vectors     | native_sha256_kat_hw        |     9 |   ✅   |        - |
| test_sha384             | native_sha384_kat           |     9 |   ✅   |        - |
| test_sha384             | native_sha384_kat_hw        |     9 |   ✅   |        - |
| test_hmac_sha384        | native_hmac_sha384_kat      |     5 |   ✅   |        - |
| test_hmac_sha384        | native_hmac_sha384_kat_hw   |     5 |   ✅   |        - |
| test_hkdf_sha384        | native_hkdf_sha384_kat      |     7 |   ✅   |        - |
| test_directnet          | native_directnet            |    10 |   ✅   |        - |
| test_dma                | native_dma                  |    12 |   ✅   |        - |
| test_dmx                | native_dmx                  |    15 |   ✅   |        - |
| test_dnc                | native_dnc                  |    23 |   ✅   |        - |
| test_dnc_stream         | native_dnc                  |    14 |   ✅   |        - |
| test_dnp3               | native_dnp3                 |    22 |   ✅   |        - |
| test_dns_resolver       | native_dns_resolver         |    18 |   ✅   |        - |
| test_dns_server         | native_dns_server           |    13 |   ✅   |        - |
| test_dns_wire           | native_dns_wire             |    14 |   ✅   |        - |
| test_dns_wire           | native_dns_wire_codec       |    14 |   ✅   |        - |
| test_docstore           | native_docstore             |     8 |   ✅   |        - |
| test_dshot              | native_dshot                |    11 |   ✅   |        - |
| test_edge_fetch         | native_edge_cache           |    17 |   ✅   |        - |
| test_edge_cache         | native_edge_cache_core      |    30 |   ✅   |        - |
| test_edge_cache_sd      | native_edge_cache_sd        |    23 |   ✅   |        - |
| test_endian             | native_endian               |     9 |   ✅   |        - |
| test_endian             | native_mmgr_endian          |     9 |   ✅   |        - |
| test_enip               | native_enip                 |     9 |   ✅   |        - |
| test_enocean            | native_enocean              |    11 |   ✅   |        - |
| test_enocean            | native_enocean_esp3         |    11 |   ✅   |        - |
| test_espnow             | native_espnow_envelope      |    11 |   ✅   |        - |
| test_euromap77          | native_euromap77            |    19 |   ✅   |        - |
| test_failsafe           | native_failsafe             |    13 |   ✅   |        - |
| test_fanuc_j519         | native_fanuc_j519           |    22 |   ✅   |        - |
| test_fdc2214            | native_fdc2214              |    19 |   ✅   |        - |
| test_fins               | native_fins                 |     8 |   ✅   |        - |
| test_float_bits         | native_float_bits           |    11 |   ✅   |        - |
| test_float_bits         | native_mmgr_float_bits      |    11 |   ✅   |        - |
| test_flow_export        | native_flow_export          |    13 |   ✅   |        - |
| test_focas              | native_focas                |    15 |   ✅   |        - |
| test_forward            | native_forward              |    33 |   ✅   |        - |
| test_forwarded_trust    | native_forwarded_trust      |    15 |   ✅   |        - |
| test_frame              | native_frame                |    18 |   ✅   |        - |
| test_frame              | native_mmgr_frame           |    18 |   ✅   |        - |
| test_ftp                | native_ftp                  |    17 |   ✅   |        - |
| test_binary_asset_blobs | native_binary_asset_blobs   |     6 |   ✅   |        - |
| test_ftp_session        | native_ftp_session          |     8 |   ✅   |        - |
| test_gateway            | native_gateway              |    13 |   ✅   |        - |
| test_gnss_survey        | native_gnss_survey          |    12 |   ✅   |        - |
| test_gnss_survey        | native_gnss_survey_in       |    12 |   ✅   |        - |
| test_goose              | native_goose                |    10 |   ✅   |        - |
| test_gpib               | native_gpib                 |    15 |   ✅   |        - |
| test_gpio_map           | native_gpio_map             |    22 |   ✅   |        - |
| test_graphql            | native_graphql_exec         |    18 |   ✅   |        - |
| test_grpcweb            | native_grpcweb_frame        |    13 |   ✅   |        - |
| test_guardrails         | native_guardrails           |    13 |   ✅   |        - |
| test_boot               | native_boot                 |    10 |   ✅   |        - |
| test_h2_conn            | native_h2conn               |    41 |   ✅   |        - |
| test_h2_frame           | native_h2_frame_rfc         |    16 |   ✅   |        - |
| test_h3_frame           | native_h3_frame_rfc         |    12 |   ✅   |        - |
| test_haas_mdc           | native_haas_mdc             |    21 |   ✅   |        - |
| test_happy_eyeballs     | native_happy_eyeballs       |    11 |   ✅   |        - |
| test_hart               | native_hart                 |    10 |   ✅   |        - |
| test_hex                | native_hex                  |     9 |   ✅   |        - |
| test_hislip             | native_hislip               |    15 |   ✅   |        - |
| test_hmmd               | native_hmmd                 |    15 |   ✅   |        - |
| test_hmmd               | native_hmmd_nobus           |    15 |   ✅   |        - |
| test_hostlink           | native_hostlink             |    10 |   ✅   |        - |
| test_hotswap            | native_hotswap              |    31 |   ✅   |        - |
| test_hpack              | native_hpack                |    13 |   ✅   |        - |
| test_hpack              | native_codec_hpack_prim     |    13 |   ✅   |        - |
| test_http_client        | native_http_client          |    14 |   ✅   |        - |
| test_http_date          | native_http_date            |    10 |   ✅   |        - |
| test_http_delivery      | native_http_delivery        |    16 |   ✅   |        - |
| test_httpcache          | native_httpcache            |    10 |   ✅   |        - |
| test_hw_health          | native_hw_health            |    14 |   ✅   |        - |
| test_iccp               | native_iccp                 |     7 |   ✅   |        - |
| test_iec60870           | native_iec60870             |    20 |   ✅   |        - |
| test_iface_bridge       | native_iface_bridge         |    11 |   ✅   |        - |
| test_ina219             | native_ina219               |    25 |   ✅   |        - |
| test_inflate            | native_codec_inflate        |    11 |   ✅   |        - |
| test_interbus           | native_interbus             |     9 |   ✅   |        - |
| test_iolink             | native_iolink               |     9 |   ✅   |        - |
| test_ip                 | native_ip                   |    12 |   ✅   |        - |
| test_j1939              | native_j1939                |    14 |   ✅   |        - |
| test_j2735              | native_j2735_uper           |    18 |   ✅   |        - |
| test_json               | native_json_codec           |    19 |   ✅   |        - |
| test_ld2410             | native_ld2410               |    14 |   ✅   |        - |
| test_ld2410             | native_ld2410_nobus         |    14 |   ✅   |        - |
| test_ldc1614            | native_ldc1614              |     8 |   ✅   |        - |
| test_lfs_mock           | native_lfs_mock             |    15 |   ✅   |        - |
| test_link_manager       | native_link_manager         |     8 |   ✅   |        - |
| test_log                | native_log_frames           |    11 |   ✅   |        - |
| test_logbuf             | native_logbuf               |    15 |   ✅   |        - |
| test_lonworks           | native_lonworks             |    11 |   ✅   |        - |
| test_lora               | native_lora                 |    19 |   ✅   |        - |
| test_lsv2               | native_lsv2                 |    12 |   ✅   |        - |
| test_lwm2m_tlv          | native_lwm2m_tlv_codec      |    11 |   ✅   |        - |
| test_mbplus             | native_mbplus               |     9 |   ✅   |        - |
| test_mbus               | native_mbus                 |    18 |   ✅   |        - |
| test_mdns_adaptive      | native_mdns_adaptive        |    10 |   ✅   |        - |
| test_mdns_service       | native_mdns_service         |    15 |   ✅   |        - |
| test_melsec             | native_melsec               |    11 |   ✅   |        - |
| test_membuild           | native_membuild             |    18 |   ✅   |        - |
| test_membuild           | native_mmgr_membuild        |    18 |   ✅   |        - |
| test_mms                | native_mms                  |     8 |   ✅   |        - |
| test_mnt                | native_mnt                  |    31 |   ✅   |        - |
| test_modbus             | native_modbus               |    14 |   ✅   |        - |
| test_modbus_master      | native_modbus_master        |    27 |   ✅   |        - |
| test_mpr121             | native_mpr121               |    21 |   ✅   |        - |
| test_mqtt               | native_mqtt_codec           |    19 |   ✅   |        - |
| test_mqtt_sn            | native_mqtt_sn_codec        |    11 |   ✅   |        - |
| test_msgpack            | native_msgpack_wire         |    15 |   ✅   |        - |
| test_mtconnect          | native_mtconnect            |    22 |   ✅   |        - |
| test_net_addr           | native_net_addr             |    16 |   ✅   |        - |
| test_nats               | native_nats_proto           |    13 |   ✅   |        - |
| test_nema_ts2           | native_nema_ts2_sdlc        |     9 |   ✅   |        - |
| test_net_egress         | native_net_egress           |    11 |   ✅   |        - |
| test_net_egress         | native_l1_egress            |    11 |   ✅   |        - |
| test_netadapt           | native_netadapt             |     9 |   ✅   |        - |
| test_nmea0183           | native_gnss_nmea0183        |    17 |   ✅   |        - |
| test_nmea2000           | native_marine_nmea2000      |    16 |   ✅   |        - |
| test_nrf24              | native_nrf24                |    17 |   ✅   |        - |
| test_ntcip              | native_ntcip_oid            |     8 |   ✅   |        - |
| test_ntp_server         | native_ntp_server           |    16 |   ✅   |        - |
| test_ntp_service        | native_ntp_service          |     9 |   ✅   |        - |
| test_ntrip_caster       | native_gnss_ntrip_caster    |    15 |   ✅   |        - |
| test_nts                | native_nts_ke               |    17 |   ✅   |        - |
| test_oauth2_exchange    | native_oauth2_exchange      |     7 |   ✅   |        - |
| test_oauth2_transport   | native_oauth2_transport     |     7 |   ✅   |        - |
| test_observability      | native_observability        |    23 |   ✅   |        - |
| test_ocit               | native_ocit_msg             |     9 |   ✅   |        - |
| test_opcua              | native_opcua                |    25 |   ✅   |        - |
| test_opcua_client       | native_opcua_client         |    31 |   ✅   |        - |
| test_openadr            | native_openadr              |     7 |   ✅   |        - |
| test_http_ota           | native_ota                  |     6 |   ✅   |        - |
| test_ota_rollback       | native_ota_rollback         |    10 |   ✅   |        - |
| test_packml             | native_packml               |    17 |   ✅   |        - |
| test_pca9685            | native_pca9685              |    20 |   ✅   |        - |
| test_pcap               | native_pcap                 |     7 |   ✅   |        - |
| test_phy                | native_phy                  |    15 |   ✅   |        - |
| test_phy                | native_l1_link              |    15 |   ✅   |        - |
| test_iface              | native_phy_iface            |    12 |   ✅   |        - |
| test_plaintext          | native_plaintext            |    19 |   ✅   |        - |
| test_plaintext          | native_mmgr_plaintext       |    19 |   ✅   |        - |
| test_pmbus              | native_pmbus                |    11 |   ✅   |        - |
| test_pn532              | native_pn532                |    11 |   ✅   |        - |
| test_plaintext          | native_pool_workers         |    19 |   ✅   |        - |
| test_secure_pool        | native_pool_workers         |    12 |   ✅   |        - |
| test_power_mgmt         | native_power_mgmt           |    22 |   ✅   |        - |
| test_powerlink          | native_powerlink            |    11 |   ✅   |        - |
| test_pqc_sha3           | native_pqc                  |    10 |   ✅   |        - |
| test_pqc_mlkem          | native_pqc                  |     9 |   ✅   |        - |
| test_pqc_sha3           | native_sha3_kat             |    10 |   ✅   |        - |
| test_pqc_mlkem          | native_mlkem_kat            |     9 |   ✅   |        - |
| test_pqc_sntrup761      | native_sntrup761_kat        |     7 |   ✅   |        - |
| test_preempt_queue      | native_preempt_queue        |    16 |   ✅   |        - |
| test_primitives         | native_primitives           |    14 |   ✅   |        - |
| test_crc                | native_primitives           |    12 |   ✅   |        - |
| test_crc                | native_crc                  |    12 |   ✅   |        - |
| test_primitives         | native_mmgr_primitives      |    14 |   ✅   |        - |
| test_profibus           | native_profibus             |    13 |   ✅   |        - |
| test_profinet           | native_profinet             |     8 |   ✅   |        - |
| test_promisc            | native_promisc_dot11        |    12 |   ✅   |        - |
| test_protobuf           | native_protobuf_wire        |    12 |   ✅   |        - |
| test_protomem           | native_protomem             |    15 |   ✅   |        - |
| test_protomem           | native_mmgr_protomem        |    15 |   ✅   |        - |
| test_protostr           | native_protostr             |    22 |   ✅   |        - |
| test_protostr           | native_mmgr_protostr        |    22 |   ✅   |        - |
| test_proxy_protocol     | native_proxy_protocol       |    22 |   ✅   |        - |
| test_psram_pool         | native_psram_pool           |     7 |   ✅   |        - |
| test_ptp                | native_ptp_wire             |    22 |   ✅   |        - |
| test_qpack              | native_qpack_rfc            |    13 |   ✅   |        - |
| test_quic_frame         | native_quic_frame_rfc       |    13 |   ✅   |        - |
| test_quic_packet        | native_quic_packet_rfc      |    10 |   ✅   |        - |
| test_quic_varint        | native_quic_varint          |     8 |   ✅   |        - |
| test_radio_power        | native_radio_power          |     6 |   ✅   |        - |
| test_radio_sniff        | native_radio_sniff_tap      |     7 |   ✅   |        - |
| test_rawl2              | native_rawl2                |     9 |   ✅   |        - |
| test_rawmemcpy          | native_rawmemcpy            |     8 |   ✅   |        - |
| test_rcwl0516           | native_rcwl0516             |    12 |   ✅   |        - |
| test_redis_resp         | native_redis_resp           |    14 |   ✅   |        - |
| test_relay              | native_relay                |    12 |   ✅   |        - |
| test_rfc1951            | native_rfc1951              |    13 |   ✅   |        - |
| test_rfc1951            | native_codec_rfc1951        |    13 |   ✅   |        - |
| test_ring               | native_ring                 |    13 |   ✅   |        - |
| test_roaming            | native_roaming              |    13 |   ✅   |        - |
| test_robotics           | native_robotics             |    15 |   ✅   |        - |
| test_rtc                | native_rtc                  |     8 |   ✅   |        - |
| test_rtcm3              | native_gnss_rtcm3           |    14 |   ✅   |        - |
| test_s7comm             | native_s7comm               |    11 |   ✅   |        - |
| test_safety_scl         | native_safety_scl           |    14 |   ✅   |        - |
| test_sb_modbus          | native_sb_modbus            |    12 |   ✅   |        - |
| test_scp                | native_scp                  |    16 |   ✅   |        - |
| test_scp                | native_scp_wire             |    16 |   ✅   |        - |
| test_scpi               | native_scpi                 |    24 |   ✅   |        - |
| test_sdi12              | native_sdi12                |    14 |   ✅   |        - |
| test_secure_pool        | native_secure_pool          |    12 |   ✅   |        - |
| test_sen0192            | native_sen0192              |    10 |   ✅   |        - |
| test_senml              | native_senml_pack           |    11 |   ✅   |        - |
| test_sep2               | native_sep2                 |     6 |   ✅   |        - |
| test_sercos             | native_sercos               |    13 |   ✅   |        - |
| test_sht3x              | native_sht3x                |     7 |   ✅   |        - |
| test_sigfox             | native_sigfox_at            |     6 |   ✅   |        - |
| test_simatic            | native_simatic              |    24 |   ✅   |        - |
| test_sleep_sched        | native_sleep_sched          |    11 |   ✅   |        - |
| test_smb_crypto         | native_md_kat               |     7 |   ✅   |        - |
| test_ntlm               | native_ntlm_v2              |    14 |   ✅   |        - |
| test_ntlmssp            | native_ntlmssp              |     9 |   ✅   |        - |
| test_spnego             | native_spnego               |     8 |   ✅   |        - |
| test_smbus              | native_smbus                |    30 |   ✅   |        - |
| test_smtp               | native_smtp                 |    39 |   ✅   |        - |
| test_snmp_ber           | native_snmp                 |    19 |   ✅   |        - |
| test_snmp_agent         | native_snmp                 |    41 |   ✅   |        - |
| test_snmp_ber           | native_snmp_ber_x690        |    19 |   ✅   |        - |
| test_snmp_trap          | native_snmp_trap            |    12 |   ✅   |        - |
| test_snmp_trap          | native_snmp_notify          |    12 |   ✅   |        - |
| test_snmp_v3            | native_snmp_v3              |    32 |   ✅   |        - |
| test_snp                | native_snp                  |    12 |   ✅   |        - |
| test_sockpool           | native_sockpool             |    12 |   ✅   |        - |
| test_southbound         | native_southbound           |    10 |   ✅   |        - |
| test_spa_router         | native_spa_router           |    11 |   ✅   |        - |
| test_span               | native_span                 |    10 |   ✅   |        - |
| test_sparkplug          | native_sparkplug            |    18 |   ✅   |        - |
| test_sqlite             | native_storage_sqlite       |    24 |   ✅   |        - |
| test_ssh_ecdsa          | native_ssh_ecdsa            |    14 |   ✅   |        - |
| test_ssh_ecdsa          | native_ssh_ecdsa_hw         |    14 |   ✅   |        - |
| test_rsa_kat            | native_rsa_kat              |    12 |   ✅   |        - |
| test_rsa_kat            | native_rsa_kat_hw           |    12 |   ✅   |        - |
| test_bignum_group14     | native_bignum_group14       |    10 |   ✅   |        - |
| test_bignum_group14     | native_bignum_group14_hw    |    10 |   ✅   |        - |
| test_ssh_sftp           | native_ssh_sftp             |    17 |   ✅   |        - |
| test_ssh_sftp           | native_sftp_wire            |    17 |   ✅   |        - |
| test_statsd             | native_statsd               |    15 |   ✅   |        - |
| test_stomp              | native_stomp                |    21 |   ✅   |        - |
| test_sunspec            | native_sunspec              |    12 |   ✅   |        - |
| test_swar               | native_swar                 |    11 |   ✅   |        - |
| test_syslog             | native_syslog               |    14 |   ✅   |        - |
| test_tcp_callbacks      | native_tcp                  |     5 |   ✅   |        - |
| test_tcp_client         | native_tcp_client           |    25 |   ✅   |        - |
| test_tcp_conn           | native_tcp_conn             |    59 |   ✅   |        - |
| test_tcp_evt            | native_tcp_evt              |    13 |   ✅   |        - |
| test_tcp_listener       | native_tcp_listener         |    52 |   ✅   |        - |
| test_tcp                | native_tcp_ns               |    24 |   ✅   |        - |
| test_telemetry          | native_telemetry            |    20 |   ✅   |        - |
| test_telnet             | native_telnet               |    24 |   ✅   |        - |
| test_thread             | native_radio_thread         |    19 |   ✅   |        - |
| test_time_compat        | native_time_compat          |     7 |   ✅   |        - |
| test_time_source        | native_time_fallback        |     8 |   ✅   |        - |
| test_http_clock         | native_http_clock           |     4 |   ✅   |        - |
| test_tls13_kdf          | native_tls13_kdf            |    13 |   ✅   |        - |
| test_tls_policy         | native_tls_policy           |    14 |   ✅   |        - |
| test_totp               | native_security_totp        |    17 |   ✅   |        - |
| test_trace_capture      | native_trace_capture        |    11 |   ✅   |        - |
| test_ubx                | native_ubx_codec            |    18 |   ✅   |        - |
| test_udp                | native_udp                  |    10 |   ✅   |        - |
| test_udp_telemetry      | native_udp_telemetry        |    17 |   ✅   |        - |
| test_udp_transport      | native_udp_transport        |    22 |   ✅   |        - |
| test_umati              | native_umati                |    11 |   ✅   |        - |
| test_utf8               | native_utf8                 |     9 |   ✅   |        - |
| test_utmc               | native_utmc_xml             |    11 |   ✅   |        - |
| test_vl53l0x            | native_vl53l0x              |    22 |   ✅   |        - |
| test_vxi11              | native_vxi11                |    17 |   ✅   |        - |
| test_wal                | native_wal                  |     8 |   ✅   |        - |
| test_wal_store          | native_wal                  |    35 |   ✅   |        - |
| test_wamp               | native_wamp                 |    23 |   ✅   |        - |
| test_wave               | native_wave_wsmp            |    13 |   ✅   |        - |
| test_wearlevel          | native_wearlevel            |     9 |   ✅   |        - |
| test_webdav             | native_webdav_wire          |    16 |   ✅   |        - |
| test_webhook            | native_webhook_json         |    11 |   ✅   |        - |
| test_wifi_sniffer       | native_radio_wifi_sniffer   |    14 |   ✅   |        - |
| test_wifi_sniffer       | native_wifi_sniffer_promisc |    14 |   ✅   |        - |
| test_wisun              | native_radio_wisun          |    11 |   ✅   |        - |
| test_workers            | native_workers              |    10 |   ✅   |        - |
| test_workers            | native_workers_stack        |    10 |   ✅   |        - |
| test_ws_client          | native_ws_client_rfc6455    |    19 |   ✅   |        - |
| test_xmpp               | native_xmpp                 |    18 |   ✅   |        - |
| test_zigbee             | native_radio_zigbee         |    12 |   ✅   |        - |
| test_zwave              | native_radio_zwave          |    11 |   ✅   |        - |

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

## test_ads - native_ads - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                             |
| --: | :---------------------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_published_constants`                      |   ✅   | Command ids are the AMS numbering, 1..9 with no gaps.                                   |
|   2 | `test_ams_header_octet_layout`                  |   ✅   | Ams header octet layout                                                                 |
|   3 | `test_read_request_length_and_payload`          |   ✅   | payload: IndexGroup 0xF005, IndexOffset 0x00010001, Length 4, each 32-bit little-endian |
|   4 | `test_header_round_trip`                        |   ✅   | Header round trip                                                                       |
|   5 | `test_write_frames_the_data_after_the_length`   |   ✅   | index group 0x4020 (%M flag memory), offset 0x20, length 3, then the data               |
|   6 | `test_read_write_symbol_by_name`                |   ✅   | Read write symbol by name                                                               |
|   7 | `test_write_control_carries_the_state_pair`     |   ✅   | AdsState 5 = Run, DeviceState 0, Length 0, all little-endian                            |
|   8 | `test_add_notification_payload_is_forty_octets` |   ✅   | 0xF005, offset 0x11, length 2, mode 4 (server on change), max delay 0,                  |
|   9 | `test_parse_read_response`                      |   ✅   | An error result: ADS error 0x00000706 (invalid index group / symbol not found).         |
|  10 | `test_parse_read_state_response`                |   ✅   | Parse read state response                                                               |
|  11 | `test_parse_device_info_terminates_a_full_name` |   ✅   | Parse device info terminates a full name                                                |
|  12 | `test_parse_add_notification_response`          |   ✅   | Parse add notification response                                                         |
|  13 | `test_walks_a_notification_stamp`               |   ✅   | A sample size that runs off the end aborts the walk instead of reading past the buffer. |
|  14 | `test_malformed_framing_is_refused`             |   ✅   | Malformed framing is refused                                                            |
|  15 | `test_builders_refuse_a_short_buffer`           |   ✅   | Builders refuse a short buffer                                                          |

</details>

---

## test_ads1115 - native_ads1115 - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                 | Status | Description                                                                               |
| --: | :------------------------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_sbas444_reset_value_with_the_mux_moved_to_ain0_gnd`            |   ✅   | Sbas444 reset value with the mux moved to ain0 gnd                                        |
|   2 | `test_sbas444_mux_encoding_for_each_single_ended_channel`            |   ✅   | Sbas444 mux encoding for each single ended channel                                        |
|   3 | `test_sbas444_pga_encoding`                                          |   ✅   | Sbas444 pga encoding                                                                      |
|   4 | `test_sbas444_data_rate_encoding`                                    |   ✅   | Sbas444 data rate encoding                                                                |
|   5 | `test_sbas444_start_single_shot_and_comparator_disabled`             |   ✅   | Sbas444 start single shot and comparator disabled                                         |
|   6 | `test_out_of_range_fields_fall_back_to_the_defaults`                 |   ✅   | Out of range fields fall back to the defaults                                             |
|   7 | `test_sbas444_lsb_is_full_scale_over_32768`                          |   ✅   | Sbas444 lsb is full scale over 32768                                                      |
|   8 | `test_sbas444_table4_endpoints`                                      |   ✅   | 8000h = -32768: -FS exactly, since -32768 * FS / 32768 = -FS                              |
|   9 | `test_conversion_is_odd_about_zero`                                  |   ✅   | Conversion is odd about zero                                                              |
|  10 | `test_lower_gain_reads_a_larger_voltage_for_the_same_code`           |   ✅   | The wider range is captured before the narrower one runs: both report through the one     |
|  11 | `test_raw_to_uv_out_of_range_gain_falls_back`                        |   ✅   | Raw to uv out of range gain falls back                                                    |
|  12 | `test_sbas444_register_addresses`                                    |   ✅   | Sbas444 register addresses                                                                |
|  13 | `test_sbas444e_model_reset_values_and_a_persistent_address_pointer`  |   ✅   | No pointer this time: 8.1.1 says the part still answers from the register it was left on. |
|  14 | `test_sbas444e_one_reading_is_a_config_write_then_a_conversion_read` |   ✅   | the config write: pointer 01h, then the word                                              |
|  15 | `test_sbas444e_a_reading_is_the_input_divided_by_the_lsb`            |   ✅   | Sbas444e a reading is the input divided by the lsb                                        |
|  16 | `test_sbas444e_read_uv_returns_the_applied_voltage`                  |   ✅   | Sbas444e read uv returns the applied voltage                                              |
|  17 | `test_sbas444e_a_negative_input_reads_a_twos_complement_code`        |   ✅   | Sbas444e a negative input reads a twos complement code                                    |
|  18 | `test_sbas444e_an_input_past_full_scale_clips`                       |   ✅   | Sbas444e an input past full scale clips                                                   |
|  19 | `test_sbas444e_each_channel_reads_its_own_pin`                       |   ✅   | Sbas444e each channel reads its own pin                                                   |
|  20 | `test_sbas444e_the_gain_selects_the_range_the_part_converts_against` |   ✅   | Sbas444e the gain selects the range the part converts against                             |
|  21 | `test_a_second_reading_sees_the_new_input`                           |   ✅   | A second reading sees the new input                                                       |
|  22 | `test_a_part_at_another_address_is_not_read`                         |   ✅   | A part at another address is not read                                                     |
|  23 | `test_begin_sends_later_transfers_to_the_address_it_was_given`       |   ✅   | and back to the strapped default, so the address is state and not a constant              |
|  24 | `test_a_refused_config_write_fails_the_reading`                      |   ✅   | A refused config write fails the reading                                                  |
|  25 | `test_a_refused_conversion_read_fails_the_reading`                   |   ✅   | A refused conversion read fails the reading                                               |
|  26 | `test_read_uv_reports_a_failed_transfer`                             |   ✅   | Read uv reports a failed transfer                                                         |
|  27 | `test_read_raw_refuses_a_null_destination`                           |   ✅   | Read raw refuses a null destination                                                       |

</details>

---

## test_amqp - native_amqp - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                                           |
| --: | :--------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_amqp091_protocol_header`                 |   ✅   | eight octets exactly; seven is not a protocol header                                  |
|   2 | `test_amqp091_frame_constants`                 |   ✅   | Amqp091 frame constants                                                               |
|   3 | `test_amqp091_frame_layout`                    |   ✅   | an empty payload frames to the eight octets of overhead alone                         |
|   4 | `test_frame_end_is_checked_before_decoding`    |   ✅   | Frame end is checked before decoding                                                  |
|   5 | `test_frame_round_trip_and_consumed`           |   ✅   | a second frame right behind it, so the consumed count has something to be wrong about |
|   6 | `test_partial_frame_is_not_parsed`             |   ✅   | Partial frame is not parsed                                                           |
|   7 | `test_amqp091_method_frame_layout`             |   ✅   | Amqp091 method frame layout                                                           |
|   8 | `test_method_round_trip_over_the_class_table`  |   ✅   | scrub the handle so a value that survives is one the parse actually wrote             |
|   9 | `test_method_payload_shorter_than_its_indices` |   ✅   | exactly four octets is a method with no arguments                                     |
|  10 | `test_amqp091_content_header_layout`           |   ✅   | and the whole thing parses back as one frame                                          |
|  11 | `test_amqp091_heartbeat`                       |   ✅   | Amqp091 heartbeat                                                                     |
|  12 | `test_builds_refuse_a_short_buffer`            |   ✅   | Builds refuse a short buffer                                                          |
|  13 | `test_an_oversized_size_field_is_refused`      |   ✅   | one octet more than is present is refused just the same                               |

</details>

---

## test_arena - native_arena - ✅ 27 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

|   # | Test                                      | Status | Description                                               |
| --: | :---------------------------------------- | :----: | :-------------------------------------------------------- |
|   1 | `test_snapshot_partitions_the_map`        |   ✅   | Snapshot partitions the map                               |
|   2 | `test_snapshot_of_an_empty_map`           |   ✅   | Snapshot of an empty map                                  |
|   3 | `test_snapshot_of_one_direction`          |   ✅   | Snapshot of one direction                                 |
|   4 | `test_point_names_are_json_escaped`       |   ✅   | Point names are json escaped                              |
|   5 | `test_value_range`                        |   ✅   | Value range                                               |
|   6 | `test_snapshot_refuses_a_short_buffer`    |   ✅   | Snapshot refuses a short buffer                           |
|   7 | `test_snapshot_buffer_boundary`           |   ✅   | Snapshot buffer boundary                                  |
|   8 | `test_set_output_then_get`                |   ✅   | The set is visible in the snapshot the engine reads back. |
|   9 | `test_set_output_refuses_an_input`        |   ✅   | Set output refuses an input                               |
|  10 | `test_unknown_point`                      |   ✅   | A point whose value really is zero is found.              |
|  11 | `test_names_match_whole`                  |   ✅   | Names match whole                                         |
|  12 | `test_get_without_the_found_flag`         |   ✅   | Get without the found flag                                |
|  13 | `test_accessors_refuse_missing_arguments` |   ✅   | Accessors refuse missing arguments                        |

</details>

---

## test_audit_log - native_audit_log - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                                |
| --: | :------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_chain_hash_is_sha256_over_the_documented_fields`   |   ✅   | The second record chains the first's hash, not the genesis anchor.                         |
|   2 | `test_seq_is_monotonic_from_one`                         |   ✅   | Seq is monotonic from one                                                                  |
|   3 | `test_untouched_chain_verifies`                          |   ✅   | Untouched chain verifies                                                                   |
|   4 | `test_empty_log_verifies`                                |   ✅   | Empty log verifies                                                                         |
|   5 | `test_every_covered_field_is_tamper_evident`             |   ✅   | the message                                                                                |
|   6 | `test_reordering_breaks_the_chain`                       |   ✅   | Reordering breaks the chain                                                                |
|   7 | `test_identical_messages_hash_differently`               |   ✅   | One result member, so the first record is copied out before the second call overwrites it. |
|   8 | `test_the_retained_window_verifies_after_the_ring_wraps` |   ✅   | The retained window verifies after the ring wraps                                          |
|   9 | `test_the_oldest_retained_record_is_still_anchored`      |   ✅   | The oldest retained record is still anchored                                               |
|  10 | `test_a_long_message_is_truncated`                       |   ✅   | a null message is an empty one, not a dereference                                          |
|  11 | `test_reset_returns_the_chain_to_genesis`                |   ✅   | Reset returns the chain to genesis                                                         |
|  12 | `test_the_sink_receives_the_complete_record`             |   ✅   | The sink receives the complete record                                                      |
|  13 | `test_category_names`                                    |   ✅   | Category names                                                                             |
|  14 | `test_format_renders_one_record`                         |   ✅   | the hash is 64 hex characters, lowercase                                                   |
|  15 | `test_format_escapes_the_message`                        |   ✅   | exactly two unescaped quotes surround the message, so the string is still one string       |
|  16 | `test_format_fails_closed_at_every_short_capacity`       |   ✅   | one octet more than the text is exactly enough, for the NUL                                |
|  17 | `test_dump_reports_integrity`                            |   ✅   | Dump reports integrity                                                                     |
|  18 | `test_dump_fails_closed_at_every_short_capacity`         |   ✅   | Dump fails closed at every short capacity                                                  |
|  19 | `test_dump_of_an_empty_log`                              |   ✅   | Dump of an empty log                                                                       |

</details>

---

## test_auth_lockout - native_auth_lockout - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                                                   |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_an_unseen_address_is_not_locked`                     |   ✅   | An unseen address is not locked                                                               |
|   2 | `test_below_the_threshold_nothing_locks`                   |   ✅   | the threshold'th failure is the one that locks                                                |
|   3 | `test_backoff_doubles_then_caps`                           |   ✅   | Backoff doubles then caps                                                                     |
|   4 | `test_the_window_counts_down_and_expires`                  |   ✅   | The window counts down and expires                                                            |
|   5 | `test_a_later_failure_restarts_the_window`                 |   ✅   | A later failure restarts the window                                                           |
|   6 | `test_the_window_survives_the_millisecond_rollover`        |   ✅   | start + base has wrapped past zero, so an implementation that compared the two instants       |
|   7 | `test_success_clears_the_address`                          |   ✅   | Success clears the address                                                                    |
|   8 | `test_success_from_an_unseen_address_touches_nothing`      |   ✅   | Success from an unseen address touches nothing                                                |
|   9 | `test_addresses_do_not_share_state`                        |   ✅   | ... and a success from one does not release the other                                         |
|  10 | `test_v4_and_v6_are_different_peers`                       |   ✅   | a v6 address differing in its last octet is a different peer                                  |
|  11 | `test_an_unspecified_address_is_never_locked`              |   ✅   | and the table is still empty, so a real peer still locks normally                             |
|  12 | `test_every_slot_holds_its_own_lockout`                    |   ✅   | Every slot holds its own lockout                                                              |
|  13 | `test_a_flood_of_new_addresses_does_not_release_a_lockout` |   ✅   | one millisecond into the window, so the remaining time is the base wait less that millisecond |
|  14 | `test_reset_releases_every_address`                        |   ✅   | Reset releases every address                                                                  |
|  15 | `test_the_configured_bounds`                               |   ✅   | The configured bounds                                                                         |

</details>

---

## test_bacnet - native_bacnet - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                                   |
| --: | :---------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_published_constants`                            |   ✅   | The device / object instance is a 22-bit field, so 4194303 is the largest legal one.          |
|   2 | `test_global_broadcast_who_is_datagram`               |   ✅   | Global broadcast who is datagram                                                              |
|   3 | `test_bvlc_length_covers_the_whole_bvll`              |   ✅   | A datagram longer than the declared BVLL is trimmed to it, not carried whole.                 |
|   4 | `test_bvlc_refusals`                                  |   ✅   | Bvlc refusals                                                                                 |
|   5 | `test_npci_control_octet_is_assembled_from_the_bits`  |   ✅   | Only the low two bits of the priority argument reach the octet.                               |
|   6 | `test_npdu_with_a_destination_address`                |   ✅   | version + control + DNET(2) + DLEN(1) + DADR(1) + hop(1) + APDU(2)                            |
|   7 | `test_hop_count_follows_the_source_fields`            |   ✅   | Hop count follows the source fields                                                           |
|   8 | `test_npdu_refusals`                                  |   ✅   | Npdu refusals                                                                                 |
|   9 | `test_network_layer_message_is_flagged`               |   ✅   | Network layer message is flagged                                                              |
|  10 | `test_who_is_with_limits_uses_context_tags`           |   ✅   | The length field is the octet count, so 4194303 = 0x3FFFFF takes three and the tag reads      |
|  11 | `test_i_am_object_identifier_packs_type_and_instance` |   ✅   | A vendor id below 256 needs one value octet, so the tag octet drops to 0x21.                  |
|  12 | `test_read_property_request`                          |   ✅   | Device object (type 8) instance 4194303, the top of the 22-bit field: 0x02000000 \| 0x3FFFFF. |
|  13 | `test_apdu_header_parse_per_pdu_type`                 |   ✅   | Apdu header parse per pdu type                                                                |
|  14 | `test_segmented_pdu_skips_the_sequence_and_window`    |   ✅   | confirmed request, SEG \| MOR set: flags, max-resp, invoke, sequence, window, choice, data    |
|  15 | `test_unsupported_pdu_types_and_short_buffers`        |   ✅   | Unsupported pdu types and short buffers                                                       |
|  16 | `test_datagram_round_trip`                            |   ✅   | Datagram round trip                                                                           |

</details>

---

## test_base64 - native_codec_base64 - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                   |
| --: | :------------------------------------------------- | :----: | :---------------------------------------------------------------------------- |
|   1 | `test_rfc4648_section_10_vectors`                  |   ✅   | Rfc4648 section 10 vectors                                                    |
|   2 | `test_rfc4648_alphabets_are_the_two_tables`        |   ✅   | Rfc4648 alphabets are the two tables                                          |
|   3 | `test_each_alphabet_rejects_the_others_characters` |   ✅   | Each alphabet rejects the others characters                                   |
|   4 | `test_decode_rejects_malformed`                    |   ✅   | Decode rejects malformed                                                      |
|   5 | `test_decode_refuses_a_short_destination`          |   ✅   | Decode refuses a short destination                                            |
|   6 | `test_decode_guards_every_octet_of_a_quad`         |   ✅   | Decode guards every octet of a quad                                           |
|   7 | `test_url_decode_stops_at_padding`                 |   ✅   | The unpadded tails sec 3.2 allows: 2 characters carry one octet, 3 carry two. |
|   8 | `test_url_encode_carries_no_padding`               |   ✅   | Url encode carries no padding                                                 |
|   9 | `test_url_decode_refuses_a_short_destination`      |   ✅   | Url decode refuses a short destination                                        |
|  10 | `test_round_trip_is_the_identity`                  |   ✅   | Round trip is the identity                                                    |

</details>

---

## test_base64 - native_codec_base64_scalar - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                   |
| --: | :------------------------------------------------- | :----: | :---------------------------------------------------------------------------- |
|   1 | `test_rfc4648_section_10_vectors`                  |   ✅   | Rfc4648 section 10 vectors                                                    |
|   2 | `test_rfc4648_alphabets_are_the_two_tables`        |   ✅   | Rfc4648 alphabets are the two tables                                          |
|   3 | `test_each_alphabet_rejects_the_others_characters` |   ✅   | Each alphabet rejects the others characters                                   |
|   4 | `test_decode_rejects_malformed`                    |   ✅   | Decode rejects malformed                                                      |
|   5 | `test_decode_refuses_a_short_destination`          |   ✅   | Decode refuses a short destination                                            |
|   6 | `test_decode_guards_every_octet_of_a_quad`         |   ✅   | Decode guards every octet of a quad                                           |
|   7 | `test_url_decode_stops_at_padding`                 |   ✅   | The unpadded tails sec 3.2 allows: 2 characters carry one octet, 3 carry two. |
|   8 | `test_url_encode_carries_no_padding`               |   ✅   | Url encode carries no padding                                                 |
|   9 | `test_url_decode_refuses_a_short_destination`      |   ✅   | Url decode refuses a short destination                                        |
|  10 | `test_round_trip_is_the_identity`                  |   ✅   | Round trip is the identity                                                    |

</details>

---

## test_bitio - native_bitio - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_ble_gatt - native_ble_gatt_att - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                 |
| --: | :-------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_core_spec_att_pdu_layout`               |   ✅   | Read Request: [0x0A][handle:2]                                                              |
|   2 | `test_core_spec_opcode_values`                |   ✅   | Vol 3 Part F sec 3.3.1 splits the opcode into a 6-bit Method with the Command Flag at bit 6 |
|   3 | `test_core_spec_characteristic_property_bits` |   ✅   | Each names one distinct bit: no two overlap.                                                |
|   4 | `test_build_parse_round_trip`                 |   ✅   | Build parse round trip                                                                      |
|   5 | `test_parse_refuses_a_truncated_pdu`          |   ✅   | Parse refuses a truncated pdu                                                               |
|   6 | `test_parse_value_absent_and_unknown_opcode`  |   ✅   | Parse value absent and unknown opcode                                                       |
|   7 | `test_parsed_value_points_into_the_input`     |   ✅   | Parsed value points into the input                                                          |
|   8 | `test_builders_fail_closed`                   |   ✅   | A zero-length Attribute Value is legal: Vol 3 Part F sec 3.2.9 allows an attribute value of |
|   9 | `test_characteristic_table_json`              |   ✅   | An empty table is the empty array, not an empty string.                                     |
|  10 | `test_characteristic_table_json_fails_closed` |   ✅   | Characteristic table json fails closed                                                      |

</details>

---

## test_bus_capture - native_bus_capture - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                  |
| --: | :------------------------------------------------------ | :----: | :--------------------------------------------------------------------------- |
|   1 | `test_the_socketcan_layout_is_the_published_one`        |   ✅   | The socketcan layout is the published one                                    |
|   2 | `test_the_record_is_sixteen_octets`                     |   ✅   | The record is sixteen octets                                                 |
|   3 | `test_the_published_flag_bits`                          |   ✅   | The published flag bits                                                      |
|   4 | `test_the_identifier_width_and_the_extended_flag`       |   ✅   | an 11-bit frame carrying a wider id keeps only the 11 bits, and sets no flag |
|   5 | `test_a_remote_frame_sets_its_flag_and_carries_no_data` |   ✅   | both flags together, on an extended remote frame                             |
|   6 | `test_the_length_is_clamped_to_eight`                   |   ✅   | exactly want octets of payload, the rest zero                                |
|   7 | `test_the_reserved_octets_are_zeroed`                   |   ✅   | The reserved octets are zeroed                                               |
|   8 | `test_the_identifier_is_big_endian`                     |   ✅   | 0x0A0B0C0D & 0x1FFFFFFF = 0x0A0B0C0D, with the EFF flag on top               |
|   9 | `test_a_short_buffer_writes_nothing`                    |   ✅   | A short buffer writes nothing                                                |
|  10 | `test_null_arguments_are_refused`                       |   ✅   | Null arguments are refused                                                   |
|  11 | `test_the_framer_holds_nothing`                         |   ✅   | The framer holds nothing                                                     |
|  12 | `test_every_frame_field_reaches_the_record`             |   ✅   | Every frame field reaches the record                                         |
|  13 | `test_a_capture_with_no_sink_is_refused`                |   ✅   | A capture with no sink is refused                                            |

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

## test_bytes - native_bytes - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                                   |
| --: | :---------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_rfc4251_uint32_encoding`                        |   ✅   | The same octets read back, through the cursor form and the offset form.                       |
|   2 | `test_rfc4251_string_encoding`                        |   ✅   | Rfc4251 string encoding                                                                       |
|   3 | `test_rfc4251_mpint_examples`                         |   ✅   | 0: no data at all, so every lane of the destination is zero.                                  |
|   4 | `test_mpint_wider_than_the_destination_is_refused`    |   ✅   | Only the leading zeros count toward the width: the same bytes behind five zero pad bytes fit. |
|   5 | `test_put_writes_and_counts`                          |   ✅   | Put writes and counts                                                                         |
|   6 | `test_put_past_cap_reports_the_capacity_needed`       |   ✅   | Put past cap reports the capacity needed                                                      |
|   7 | `test_put_be_writes_most_significant_byte_first`      |   ✅   | The low n bytes of the value, in decreasing significance: the tail of the full string.        |
|   8 | `test_put_be_past_cap_counts_its_whole_width`         |   ✅   | Put be past cap counts its whole width                                                        |
|   9 | `test_raw_stores_whole_or_not_at_all`                 |   ✅   | Raw stores whole or not at all                                                                |
|  10 | `test_take_be_advances_by_the_width`                  |   ✅   | Take be advances by the width                                                                 |
|  11 | `test_take_be_at_the_end_and_past_it`                 |   ✅   | Take be at the end and past it                                                                |
|  12 | `test_take_be_refusal_is_sticky`                      |   ✅   | Take be refusal is sticky                                                                     |
|  13 | `test_take_be_zero_width`                             |   ✅   | Take be zero width                                                                            |
|  14 | `test_rd_u32_short_read_is_refused`                   |   ✅   | Rd u32 short read is refused                                                                  |
|  15 | `test_rd_str_overlong_length_rewinds`                 |   ✅   | Rd str overlong length rewinds                                                                |
|  16 | `test_rd_str_full_range_length_cannot_wrap_the_bound` |   ✅   | Rd str full range length cannot wrap the bound                                                |
|  17 | `test_rd_str_exact_fit_and_empty_string`              |   ✅   | Rd str exact fit and empty string                                                             |

</details>

---

## test_bytes - native_mmgr_bytes - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                                   |
| --: | :---------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_rfc4251_uint32_encoding`                        |   ✅   | The same octets read back, through the cursor form and the offset form.                       |
|   2 | `test_rfc4251_string_encoding`                        |   ✅   | Rfc4251 string encoding                                                                       |
|   3 | `test_rfc4251_mpint_examples`                         |   ✅   | 0: no data at all, so every lane of the destination is zero.                                  |
|   4 | `test_mpint_wider_than_the_destination_is_refused`    |   ✅   | Only the leading zeros count toward the width: the same bytes behind five zero pad bytes fit. |
|   5 | `test_put_writes_and_counts`                          |   ✅   | Put writes and counts                                                                         |
|   6 | `test_put_past_cap_reports_the_capacity_needed`       |   ✅   | Put past cap reports the capacity needed                                                      |
|   7 | `test_put_be_writes_most_significant_byte_first`      |   ✅   | The low n bytes of the value, in decreasing significance: the tail of the full string.        |
|   8 | `test_put_be_past_cap_counts_its_whole_width`         |   ✅   | Put be past cap counts its whole width                                                        |
|   9 | `test_raw_stores_whole_or_not_at_all`                 |   ✅   | Raw stores whole or not at all                                                                |
|  10 | `test_take_be_advances_by_the_width`                  |   ✅   | Take be advances by the width                                                                 |
|  11 | `test_take_be_at_the_end_and_past_it`                 |   ✅   | Take be at the end and past it                                                                |
|  12 | `test_take_be_refusal_is_sticky`                      |   ✅   | Take be refusal is sticky                                                                     |
|  13 | `test_take_be_zero_width`                             |   ✅   | Take be zero width                                                                            |
|  14 | `test_rd_u32_short_read_is_refused`                   |   ✅   | Rd u32 short read is refused                                                                  |
|  15 | `test_rd_str_overlong_length_rewinds`                 |   ✅   | Rd str overlong length rewinds                                                                |
|  16 | `test_rd_str_full_range_length_cannot_wrap_the_bound` |   ✅   | Rd str full range length cannot wrap the bound                                                |
|  17 | `test_rd_str_exact_fit_and_empty_string`              |   ✅   | Rd str exact fit and empty string                                                             |

</details>

---

## test_c37118 - native_c37118 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                        | Status | Description                                                                            |
| --: | :------------------------------------------ | :----: | :------------------------------------------------------------------------------------- |
|   1 | `test_crc_ccitt_published_check_value`      |   ✅   | init 0xFFFF, so the empty message is the initial register, not 0.                      |
|   2 | `test_command_frame_field_layout`           |   ✅   | CHK covers every octet up to but excluding itself, and is written big-endian.          |
|   3 | `test_frame_round_trip`                     |   ✅   | The version nibble is carried independently of the type field.                         |
|   4 | `test_command_word_round_trip`              |   ✅   | Command word round trip                                                                |
|   5 | `test_parse_rejects_a_corrupted_frame`      |   ✅   | Parse rejects a corrupted frame                                                        |
|   6 | `test_parse_rejects_malformed_framing`      |   ✅   | Parse rejects malformed framing                                                        |
|   7 | `test_build_refuses_an_undersized_buffer`   |   ✅   | A payload pointer is required whenever a payload length is given.                      |
|   8 | `test_stat_all_zero_is_a_healthy_pmu`       |   ✅   | Stat all zero is a healthy pmu                                                         |
|   9 | `test_stat_all_ones`                        |   ✅   | Stat all ones                                                                          |
|  10 | `test_stat_flags_are_independent`           |   ✅   | Stat flags are independent                                                             |
|  11 | `test_stat_multi_bit_fields`                |   ✅   | All three at once, still separate: quality 3, unlocked over 1000 s, frequency trigger. |
|  12 | `test_stat_is_refused_outside_a_data_frame` |   ✅   | Stat is refused outside a data frame                                                   |

</details>

---

## test_canopen - native_canopen - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                         | Status | Description                                                                     |
| --: | :----------------------------------------------------------- | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_predefined_connection_set`                             |   ✅   | The two masks partition the 11-bit identifier with nothing left over.           |
|   2 | `test_expedited_sdo_upload_of_the_identity_vendor_id`        |   ✅   | Expedited sdo upload of the identity vendor id                                  |
|   3 | `test_sdo_expedited_download_encodes_the_unused_octet_count` |   ✅   | The upload response side reads the same n back out.                             |
|   4 | `test_sdo_abort_carries_the_code_little_endian`              |   ✅   | to_server picks the client -> server COB-ID instead.                            |
|   5 | `test_sdo_download_acknowledgement`                          |   ✅   | A command specifier the server never sends (1 = client download) is refused.    |
|   6 | `test_nmt_node_control`                                      |   ✅   | Nmt node control                                                                |
|   7 | `test_sync_and_emcy_share_a_function_code`                   |   ✅   | 0x8130 = the "communication error / bus off" emergency error code               |
|   8 | `test_heartbeat_state_and_toggle_bit`                        |   ✅   | Boot-up is the state-0 heartbeat a node emits on entering Pre-operational.      |
|   9 | `test_time_of_day`                                           |   ✅   | The four reserved bits never reach the wire, and never come back off it.        |
|  10 | `test_pdo_bases_and_classification`                          |   ✅   | A zero-length PDO is legal.                                                     |
|  11 | `test_classifier_rejects_extended_and_unknown`               |   ✅   | Classifier rejects extended and unknown                                         |
|  12 | `test_segmented_download_initiate`                           |   ✅   | Segmented download initiate                                                     |
|  13 | `test_segment_command_octet_layout`                          |   ✅   | An upload segment request is command specifier 3, so it is not a segment frame. |
|  14 | `test_segmented_upload_reassembly`                           |   ✅   | Nothing is accepted after the last segment.                                     |

</details>

---

## test_cbor - native_codec_cbor - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                                   |
| --: | :-------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_rfc8949_appendix_a_vectors`                   |   ✅   | 0 \| 0x00                                                                                     |
|   2 | `test_str_n_takes_its_length_from_the_caller`       |   ✅   | Str n takes its length from the caller                                                        |
|   3 | `test_null_string_is_the_empty_text_item`           |   ✅   | Null string is the empty text item                                                            |
|   4 | `test_put_label_writes_the_cbor_label_number`       |   ✅   | Put label writes the cbor label number                                                        |
|   5 | `test_head_forms_round_trip`                        |   ✅   | Head forms round trip                                                                         |
|   6 | `test_negative_integers_round_trip`                 |   ✅   | Negative integers round trip                                                                  |
|   7 | `test_peek_names_every_item`                        |   ✅   | 0xc0 is tag 0 (sec 3.4), 0xe0 is an unassigned simple value: neither is an item this carries. |
|   8 | `test_map_round_trips_item_by_item`                 |   ✅   | Map round trips item by item                                                                  |
|   9 | `test_byte_string_round_trips`                      |   ✅   | Byte string round trips                                                                       |
|  10 | `test_float_forms_read_back`                        |   ✅   | 2.5 as an IEEE 754 double: sign 0, exponent 0x400, mantissa 0x4000000000000.                  |
|  11 | `test_overflow_reports_the_size_it_needed`          |   ✅   | Overflow reports the size it needed                                                           |
|  12 | `test_reserved_and_indefinite_heads_are_refused`    |   ✅   | 0x9f is the indefinite-length array head of Table 6's "[_ ]" (0x9fff).                        |
|  13 | `test_type_mismatch_fails_and_marks_the_reader`     |   ✅   | Type mismatch fails and marks the reader                                                      |
|  14 | `test_declared_length_past_the_end_is_refused`      |   ✅   | Declared length past the end is refused                                                       |
|  15 | `test_the_read_error_is_sticky`                     |   ✅   | The read error is sticky                                                                      |
|  16 | `test_truncated_item_is_refused`                    |   ✅   | Truncated item is refused                                                                     |
|  17 | `test_every_reader_fails_closed_on_an_empty_region` |   ✅   | Every reader fails closed on an empty region                                                  |

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

## test_cclink - native_cclink - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_checksum_is_the_low_byte_of_the_sum`                    |   ✅   | Checksum is the low byte of the sum                    |
|   2 | `test_frame_is_the_arguments_in_order_plus_the_checksum`      |   ✅   | Frame is the arguments in order plus the checksum      |
|   3 | `test_build_parse_round_trip`                                 |   ✅   | Build parse round trip                                 |
|   4 | `test_every_command_octet_survives_the_round_trip`            |   ✅   | Every command octet survives the round trip            |
|   5 | `test_command_macros_are_mutually_distinct`                   |   ✅   | Command macros are mutually distinct                   |
|   6 | `test_any_single_octet_change_fails_verification`             |   ✅   | Any single octet change fails verification             |
|   7 | `test_bit_addressing_matches_the_documented_index_arithmetic` |   ✅   | Bit addressing matches the documented index arithmetic |
|   8 | `test_bit_accessors_round_trip_over_a_block`                  |   ✅   | Bit accessors round trip over a block                  |
|   9 | `test_word_accessor_is_little_endian`                         |   ✅   | Word accessor is little endian                         |
|  10 | `test_accessors_refuse_out_of_range`                          |   ✅   | Accessors refuse out of range                          |
|  11 | `test_build_refusals`                                         |   ✅   | Build refusals                                         |
|  12 | `test_parse_refusals_and_the_empty_payload`                   |   ✅   | Parse refusals and the empty payload                   |
|  13 | `test_bit_only_and_word_only_exchanges`                       |   ✅   | Bit only and word only exchanges                       |

</details>

---

## test_cia402 - native_cia402 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                            |
| --: | :---------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------- |
|   1 | `test_object_dictionary_indices`                            |   ✅   | Modes of Operation values; 5 is not assigned, so the run is not contiguous.            |
|   2 | `test_statusword_mask_value_table`                          |   ✅   | Statusword mask value table                                                            |
|   3 | `test_bit_five_separates_quick_stop_from_operation_enabled` |   ✅   | The same pair with a realistic upper byte: voltage enabled, remote, target reached.    |
|   4 | `test_unmatched_statusword_is_unknown`                      |   ✅   | Unmatched statusword is unknown                                                        |
|   5 | `test_statusword_flag_accessors`                            |   ✅   | Statusword flag accessors                                                              |
|   6 | `test_controlword_command_table`                            |   ✅   | Disable Voltage clears the enable-voltage bit; Quick Stop keeps voltage but clears the |
|   7 | `test_enable_sequence_walks_the_state_machine`              |   ✅   | A fault must be reset before the sequence can restart.                                 |
|   8 | `test_sdo_setters_target_the_right_objects`                 |   ✅   | -1000 as a 32-bit two's complement is 0x100000000 - 1000 = 0xFFFFFC18, little-endian.  |
|   9 | `test_sdo_get_u16_checks_the_index`                         |   ✅   | A zero want_index accepts whatever object the reply names.                             |
|  10 | `test_sdo_get_i32_is_signed`                                |   ✅   | A two-octet reply cannot carry a 32-bit object.                                        |
|  11 | `test_pdo_pack_and_unpack_round_trip`                       |   ✅   | A real cyclic exchange: Statusword 0x0637 with a position of 2147483647.               |

</details>

---

## test_cip - native_cip - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                                                      |
| --: | :------------------------------------------------------------ | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_logical_segment_constants`                              |   ✅   | Service codes, and the reply bit that turns a service into its response.         |
|   2 | `test_identity_vendor_id_request`                             |   ✅   | Identity vendor id request                                                       |
|   3 | `test_get_attributes_all_has_no_attribute_segment`            |   ✅   | Get attributes all has no attribute segment                                      |
|   4 | `test_wide_ids_use_the_sixteen_bit_segment_form`              |   ✅   | All three wide: 4 octets each.                                                   |
|   5 | `test_epath_is_always_word_aligned`                           |   ✅   | Epath is always word aligned                                                     |
|   6 | `test_set_attribute_single_appends_the_value`                 |   ✅   | A zero-length value is legal and yields the bare request.                        |
|   7 | `test_parse_successful_response`                              |   ✅   | Vendor ID 0x004D returned as a UINT, little-endian.                              |
|   8 | `test_additional_status_is_counted_in_words`                  |   ✅   | General status 0x1F (vendor specific error) with two words of additional status. |
|   9 | `test_response_refusals`                                      |   ✅   | Response refusals                                                                |
|  10 | `test_request_refuses_a_misaligned_or_oversized_path`         |   ✅   | 7 octets of capacity cannot hold the 8-octet request.                            |
|  11 | `test_epath_refuses_a_short_buffer`                           |   ✅   | Epath refuses a short buffer                                                     |
|  12 | `test_service_and_reply_service_differ_only_by_the_reply_bit` |   ✅   | Service and reply service differ only by the reply bit                           |

</details>

---

## test_client - native_client - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_the_dial_resolves_a_literal`                   |   ✅   | The wiring is checked by what the stored resolver DOES, not by its address. A namespace table |
|   2 | `test_open_connects_and_reports_the_slot`            |   ✅   | Open connects and reports the slot                                                            |
|   3 | `test_open_refuses_a_bad_host_and_a_refused_connect` |   ✅   | Open refuses a bad host and a refused connect                                                 |
|   4 | `test_send_reaches_the_wire`                         |   ✅   | Send reaches the wire                                                                         |
|   5 | `test_received_bytes_buffer_and_drain`               |   ✅   | Received bytes buffer and drain                                                               |
|   6 | `test_a_peer_fin_closes_the_slot`                    |   ✅   | A peer fin closes the slot                                                                    |
|   7 | `test_guards_reject_ids_outside_the_pool`            |   ✅   | Guards reject ids outside the pool                                                            |
|   8 | `test_pool_exhaustion_refuses_a_further_open`        |   ✅   | Pool exhaustion refuses a further open                                                        |

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

## test_cloudevents - native_cloudevents - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                                |
| --: | :---------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_binary_mode_published_request`            |   ✅   | sec 3.1.1: datacontenttype rides in Content-Type, never in a ce- header                    |
|   2 | `test_binary_mode_optional_subject`             |   ✅   | Binary mode optional subject                                                               |
|   3 | `test_binary_mode_requires_id_source_and_type`  |   ✅   | no message at all clears every attribute rather than leaving the last read's values behind |
|   4 | `test_structured_mode_required_attributes`      |   ✅   | Structured mode required attributes                                                        |
|   5 | `test_structured_mode_media_type`               |   ✅   | Structured mode media type                                                                 |
|   6 | `test_structured_mode_json_data`                |   ✅   | Structured mode json data                                                                  |
|   7 | `test_structured_mode_string_data`              |   ✅   | Structured mode string data                                                                |
|   8 | `test_structured_mode_stated_datacontenttype`   |   ✅   | Structured mode stated datacontenttype                                                     |
|   9 | `test_structured_mode_optional_attributes`      |   ✅   | a datacontenttype with no data at all still describes the (absent) payload                 |
|  10 | `test_structured_mode_refuses_missing_required` |   ✅   | Structured mode refuses missing required                                                   |
|  11 | `test_structured_mode_refuses_a_short_buffer`   |   ✅   | Structured mode refuses a short buffer                                                     |
|  12 | `test_attribute_values_are_json_escaped`        |   ✅   | Attribute values are json escaped                                                          |
|  13 | `test_binary_read_feeds_a_structured_build`     |   ✅   | Binary read feeds a structured build                                                       |

</details>

---

## test_coap - native_coap - ✅ 58 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                               |
| --: | :---------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_response_option_capacity_stop`                  |   ✅   | Response option capacity stop                                                             |
|   2 | `test_coap_udp_handler_basic`                         |   ✅   | Coap udp handler basic                                                                    |
|   3 | `test_non_confirmable_malformed_is_silent`            |   ✅   | Non confirmable malformed is silent                                                       |
|   4 | `test_response_code_as_request_is_method_not_allowed` |   ✅   | Response code as request is method not allowed                                            |
|   5 | `test_block1_ignored_on_get`                          |   ✅   | Block1 ignored on get                                                                     |
|   6 | `test_block1_block_size_change_is_incomplete`         |   ✅   | Block1 block size change is incomplete                                                    |
|   7 | `test_block1_empty_intermediate_block`                |   ✅   | Block1 empty intermediate block                                                           |
|   8 | `test_error_response_carries_no_observe_or_block2`    |   ✅   | Error response carries no observe or block2                                               |
|   9 | `test_block2_offset_at_end_of_representation`         |   ✅   | Block2 offset at end of representation                                                    |
|  10 | `test_block2_on_empty_success_body`                   |   ✅   | Block2 on empty success body                                                              |
|  11 | `test_add_resource_limits`                            |   ✅   | Add resource limits                                                                       |
|  12 | `test_short_and_truncated_token`                      |   ✅   | Short and truncated token                                                                 |
|  13 | `test_malformed_options_bad_request`                  |   ✅   | Malformed options bad request                                                             |
|  14 | `test_extended_delta_and_length_ignored`              |   ✅   | Extended delta and length ignored                                                         |
|  15 | `test_oversized_path_and_query`                       |   ✅   | Oversized path and query                                                                  |
|  16 | `test_block_option_too_wide`                          |   ✅   | Block option too wide                                                                     |
|  17 | `test_block1_reserved_szx`                            |   ✅   | RFC 7959 sec 2.2: a reserved SZX of 7 "MUST lead to a 4.00 Bad Request response code upon |
|  18 | `test_block1_continue_no_space`                       |   ✅   | Block1 continue no space                                                                  |
|  19 | `test_response_payload_clamped`                       |   ✅   | Response payload clamped                                                                  |
|  20 | `test_response_buffer_too_small`                      |   ✅   | Response buffer too small                                                                 |
|  21 | `test_well_known_core_truncates`                      |   ✅   | Well known core truncates                                                                 |
|  22 | `test_observe_large_seq_encoding`                     |   ✅   | Observe large seq encoding                                                                |
|  23 | `test_block2_explicit_paging`                         |   ✅   | Block2 explicit paging                                                                    |
|  24 | `test_block2_auto_when_large`                         |   ✅   | Block2 auto when large                                                                    |
|  25 | `test_block2_szx_clamped`                             |   ✅   | Block2 szx clamped                                                                        |
|  26 | `test_block2_absent_for_small`                        |   ✅   | Block2 absent for small                                                                   |
|  27 | `test_block2_out_of_range`                            |   ✅   | Block2 out of range                                                                       |
|  28 | `test_block2_reserved_szx`                            |   ✅   | RFC 7959 sec 2.2: "The value 7 for SZX (which would indicate a block size of 2048) is     |
|  29 | `test_block1_upload_two_blocks`                       |   ✅   | Block1 upload two blocks                                                                  |
|  30 | `test_block1_out_of_order`                            |   ✅   | Block1 out of order                                                                       |
|  31 | `test_block1_too_large`                               |   ✅   | Block1 too large                                                                          |
|  32 | `test_observe_option_in_response`                     |   ✅   | Observe option in response                                                                |
|  33 | `test_response_option_overflows_buffer`               |   ✅   | Response option overflows buffer                                                          |
|  34 | `test_no_observe_option_when_seq_negative`            |   ✅   | No observe option when seq negative                                                       |
|  35 | `test_get_content`                                    |   ✅   | Get content                                                                               |
|  36 | `test_not_found`                                      |   ✅   | Not found                                                                                 |
|  37 | `test_method_not_allowed`                             |   ✅   | Method not allowed                                                                        |
|  38 | `test_non_request_type`                               |   ✅   | Non request type                                                                          |
|  39 | `test_put_with_payload`                               |   ✅   | Put with payload                                                                          |
|  40 | `test_multi_segment_path`                             |   ✅   | Multi segment path                                                                        |
|  41 | `test_uri_query`                                      |   ✅   | Uri query                                                                                 |
|  42 | `test_empty_con_ping_rst`                             |   ✅   | Empty con ping rst                                                                        |
|  43 | `test_bad_version_rst`                                |   ✅   | Bad version rst                                                                           |
|  44 | `test_delete`                                         |   ✅   | Delete                                                                                    |
|  45 | `test_token_8_bytes`                                  |   ✅   | Token 8 bytes                                                                             |
|  46 | `test_extended_option_length`                         |   ✅   | Extended option length                                                                    |
|  47 | `test_ack_ignored`                                    |   ✅   | Ack ignored                                                                               |
|  48 | `test_root_path`                                      |   ✅   | Root path                                                                                 |
|  49 | `test_unknown_method_not_allowed`                     |   ✅   | Unknown method not allowed                                                                |
|  50 | `test_unknown_critical_option_bad_option`             |   ✅   | Unknown critical option bad option                                                        |
|  51 | `test_well_known_core_discovery`                      |   ✅   | Well known core discovery                                                                 |
|  52 | `test_well_known_core_rejects_post`                   |   ✅   | Well known core rejects post                                                              |
|  53 | `test_dedup_store_lookup_roundtrip`                   |   ✅   | Dedup store lookup roundtrip                                                              |
|  54 | `test_dedup_full_address_keying`                      |   ✅   | Dedup full address keying                                                                 |
|  55 | `test_dedup_expiry`                                   |   ✅   | Dedup expiry                                                                              |
|  56 | `test_dedup_too_large_not_cached`                     |   ✅   | Dedup too large not cached                                                                |
|  57 | `test_dedup_eviction_and_update`                      |   ✅   | Dedup eviction and update                                                                 |
|  58 | `test_dedup_handler_replays_without_rerunning`        |   ✅   | Dedup handler replays without rerunning                                                   |

</details>

---

## test_coap - native_coap_observe - ✅ 66 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                               |
| --: | :---------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_response_option_capacity_stop`                  |   ✅   | Response option capacity stop                                                             |
|   2 | `test_coap_udp_handler_basic`                         |   ✅   | Coap udp handler basic                                                                    |
|   3 | `test_coap_observe_over_udp`                          |   ✅   | Coap observe over udp                                                                     |
|   4 | `test_coap_observe_registry_full`                     |   ✅   | Coap observe registry full                                                                |
|   5 | `test_coap_observe_registry_key_fields`               |   ✅   | Coap observe registry key fields                                                          |
|   6 | `test_coap_observe_zero_length_token`                 |   ✅   | Coap observe zero length token                                                            |
|   7 | `test_coap_observe_targeted_removal`                  |   ✅   | Coap observe targeted removal                                                             |
|   8 | `test_coap_notify_clamps_oversized_body`              |   ✅   | Coap notify clamps oversized body                                                         |
|   9 | `test_coap_observe_on_discovery_is_not_registered`    |   ✅   | Coap observe on discovery is not registered                                               |
|  10 | `test_coap_udp_edge_datagrams`                        |   ✅   | Coap udp edge datagrams                                                                   |
|  11 | `test_non_confirmable_malformed_is_silent`            |   ✅   | Non confirmable malformed is silent                                                       |
|  12 | `test_response_code_as_request_is_method_not_allowed` |   ✅   | Response code as request is method not allowed                                            |
|  13 | `test_block1_ignored_on_get`                          |   ✅   | Block1 ignored on get                                                                     |
|  14 | `test_block1_block_size_change_is_incomplete`         |   ✅   | Block1 block size change is incomplete                                                    |
|  15 | `test_block1_empty_intermediate_block`                |   ✅   | Block1 empty intermediate block                                                           |
|  16 | `test_error_response_carries_no_observe_or_block2`    |   ✅   | Error response carries no observe or block2                                               |
|  17 | `test_block2_offset_at_end_of_representation`         |   ✅   | Block2 offset at end of representation                                                    |
|  18 | `test_block2_on_empty_success_body`                   |   ✅   | Block2 on empty success body                                                              |
|  19 | `test_add_resource_limits`                            |   ✅   | Add resource limits                                                                       |
|  20 | `test_short_and_truncated_token`                      |   ✅   | Short and truncated token                                                                 |
|  21 | `test_malformed_options_bad_request`                  |   ✅   | Malformed options bad request                                                             |
|  22 | `test_extended_delta_and_length_ignored`              |   ✅   | Extended delta and length ignored                                                         |
|  23 | `test_oversized_path_and_query`                       |   ✅   | Oversized path and query                                                                  |
|  24 | `test_block_option_too_wide`                          |   ✅   | Block option too wide                                                                     |
|  25 | `test_block1_reserved_szx`                            |   ✅   | RFC 7959 sec 2.2: a reserved SZX of 7 "MUST lead to a 4.00 Bad Request response code upon |
|  26 | `test_block1_continue_no_space`                       |   ✅   | Block1 continue no space                                                                  |
|  27 | `test_response_payload_clamped`                       |   ✅   | Response payload clamped                                                                  |
|  28 | `test_response_buffer_too_small`                      |   ✅   | Response buffer too small                                                                 |
|  29 | `test_well_known_core_truncates`                      |   ✅   | Well known core truncates                                                                 |
|  30 | `test_observe_large_seq_encoding`                     |   ✅   | Observe large seq encoding                                                                |
|  31 | `test_block2_explicit_paging`                         |   ✅   | Block2 explicit paging                                                                    |
|  32 | `test_block2_auto_when_large`                         |   ✅   | Block2 auto when large                                                                    |
|  33 | `test_block2_szx_clamped`                             |   ✅   | Block2 szx clamped                                                                        |
|  34 | `test_block2_absent_for_small`                        |   ✅   | Block2 absent for small                                                                   |
|  35 | `test_block2_out_of_range`                            |   ✅   | Block2 out of range                                                                       |
|  36 | `test_block2_reserved_szx`                            |   ✅   | RFC 7959 sec 2.2: "The value 7 for SZX (which would indicate a block size of 2048) is     |
|  37 | `test_block1_upload_two_blocks`                       |   ✅   | Block1 upload two blocks                                                                  |
|  38 | `test_block1_out_of_order`                            |   ✅   | Block1 out of order                                                                       |
|  39 | `test_block1_too_large`                               |   ✅   | Block1 too large                                                                          |
|  40 | `test_observe_option_in_response`                     |   ✅   | Observe option in response                                                                |
|  41 | `test_response_option_overflows_buffer`               |   ✅   | Response option overflows buffer                                                          |
|  42 | `test_no_observe_option_when_seq_negative`            |   ✅   | No observe option when seq negative                                                       |
|  43 | `test_get_content`                                    |   ✅   | Get content                                                                               |
|  44 | `test_not_found`                                      |   ✅   | Not found                                                                                 |
|  45 | `test_method_not_allowed`                             |   ✅   | Method not allowed                                                                        |
|  46 | `test_non_request_type`                               |   ✅   | Non request type                                                                          |
|  47 | `test_put_with_payload`                               |   ✅   | Put with payload                                                                          |
|  48 | `test_multi_segment_path`                             |   ✅   | Multi segment path                                                                        |
|  49 | `test_uri_query`                                      |   ✅   | Uri query                                                                                 |
|  50 | `test_empty_con_ping_rst`                             |   ✅   | Empty con ping rst                                                                        |
|  51 | `test_bad_version_rst`                                |   ✅   | Bad version rst                                                                           |
|  52 | `test_delete`                                         |   ✅   | Delete                                                                                    |
|  53 | `test_token_8_bytes`                                  |   ✅   | Token 8 bytes                                                                             |
|  54 | `test_extended_option_length`                         |   ✅   | Extended option length                                                                    |
|  55 | `test_ack_ignored`                                    |   ✅   | Ack ignored                                                                               |
|  56 | `test_root_path`                                      |   ✅   | Root path                                                                                 |
|  57 | `test_unknown_method_not_allowed`                     |   ✅   | Unknown method not allowed                                                                |
|  58 | `test_unknown_critical_option_bad_option`             |   ✅   | Unknown critical option bad option                                                        |
|  59 | `test_well_known_core_discovery`                      |   ✅   | Well known core discovery                                                                 |
|  60 | `test_well_known_core_rejects_post`                   |   ✅   | Well known core rejects post                                                              |
|  61 | `test_dedup_store_lookup_roundtrip`                   |   ✅   | Dedup store lookup roundtrip                                                              |
|  62 | `test_dedup_full_address_keying`                      |   ✅   | Dedup full address keying                                                                 |
|  63 | `test_dedup_expiry`                                   |   ✅   | Dedup expiry                                                                              |
|  64 | `test_dedup_too_large_not_cached`                     |   ✅   | Dedup too large not cached                                                                |
|  65 | `test_dedup_eviction_and_update`                      |   ✅   | Dedup eviction and update                                                                 |
|  66 | `test_dedup_handler_replays_without_rerunning`        |   ✅   | Dedup handler replays without rerunning                                                   |

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

## test_config_io - native_config_io - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                            |
| --: | :------------------------------------------------- | :----: | :--------------------------------------------------------------------- |
|   1 | `test_export_writes_one_key_value_line_per_field`  |   ✅   | Export writes one key value line per field                             |
|   2 | `test_export_carries_every_field_even_when_unset`  |   ✅   | Export carries every field even when unset                             |
|   3 | `test_export_import_round_trip`                    |   ✅   | Export import round trip                                               |
|   4 | `test_import_is_idempotent`                        |   ✅   | Import is idempotent                                                   |
|   5 | `test_import_writes_only_keys_the_schema_declares` |   ✅   | Import writes only keys the schema declares                            |
|   6 | `test_import_steps_over_a_keyless_schema_entry`    |   ✅   | Import steps over a keyless schema entry                               |
|   7 | `test_import_rejects_a_field_of_an_unknown_type`   |   ✅   | Import rejects a field of an unknown type                              |
|   8 | `test_import_skips_a_line_with_no_separator`       |   ✅   | Import skips a line with no separator                                  |
|   9 | `test_import_splits_on_the_first_separator`        |   ✅   | Import splits on the first separator                                   |
|  10 | `test_import_drops_a_line_past_the_store_limits`   |   ✅   | Import drops a line past the store limits                              |
|  11 | `test_export_fails_closed_on_a_short_buffer`       |   ✅   | One byte short of the whole blob is still a refusal, not a truncation. |
|  12 | `test_missing_arguments_are_refused`               |   ✅   | Missing arguments are refused                                          |

</details>

---

## test_config_store - native_config_store - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                                |
| --: | :--------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_string_round_trip`                             |   ✅   | The returned count is the characters written, so it agrees with the string's own length.   |
|   2 | `test_u32_round_trip`                                |   ✅   | U32 round trip                                                                             |
|   3 | `test_blob_round_trip`                               |   ✅   | Blob round trip                                                                            |
|   4 | `test_an_absent_key_reports_the_default`             |   ✅   | A null default is the empty string, not a dereference.                                     |
|   5 | `test_an_overlong_key_is_refused_not_truncated`      |   ✅   | Neither over-long key aliased onto the 15-character one, which still holds its own value.  |
|   6 | `test_namespaces_hold_separate_values_for_one_key`   |   ✅   | Clearing one namespace leaves the other untouched.                                         |
|   7 | `test_erase_drops_one_key_and_clear_drops_them_all`  |   ✅   | Erase drops one key and clear drops them all                                               |
|   8 | `test_an_unusable_namespace_is_refused`              |   ✅   | The refused open did not leave "t" addressable, so the value is not reachable by accident. |
|   9 | `test_a_short_destination_is_bounded_and_terminated` |   ✅   | The default is bounded the same way when the key is absent.                                |
|  10 | `test_a_read_with_no_room_is_refused`                |   ✅   | A read with no room is refused                                                             |
|  11 | `test_a_write_with_no_value_is_refused`              |   ✅   | A write with no value is refused                                                           |

</details>

---

## test_control - native_system_control - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                  |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_proportional_term`                         |   ✅   | The output tracks the error only: halving it halves the command.                             |
|   2 | `test_integral_term_accumulates`                 |   ✅   | An error of the opposite sign unwinds it by the same rule.                                   |
|   3 | `test_derivative_acts_on_the_measurement`        |   ✅   | A held measurement has no derivative.                                                        |
|   4 | `test_setpoint_step_produces_no_derivative_kick` |   ✅   | Setpoint step produces no derivative kick                                                    |
|   5 | `test_derivative_low_pass`                       |   ✅   | Derivative low pass                                                                          |
|   6 | `test_feedforward_term`                          |   ✅   | Feedforward term                                                                             |
|   7 | `test_output_clamping`                           |   ✅   | Output clamping                                                                              |
|   8 | `test_anti_windup_freezes_and_releases`          |   ✅   | The same freeze applies at the lower rail with a negative error.                             |
|   9 | `test_integral_hard_clamp`                       |   ✅   | Integral hard clamp                                                                          |
|  10 | `test_all_four_terms_together`                   |   ✅   | All four terms together                                                                      |
|  11 | `test_non_positive_dt_is_not_a_step`             |   ✅   | Non positive dt is not a step                                                                |
|  12 | `test_fixed_rate_matches_the_variable_rate_law`  |   ✅   | Fixed rate matches the variable rate law                                                     |
|  13 | `test_reset_clears_only_the_runtime_state`       |   ✅   | Reset clears only the runtime state                                                          |
|  14 | `test_init_defaults`                             |   ✅   | Init defaults                                                                                |
|  15 | `test_batched_update_matches_the_single_loop`    |   ✅   | A null array is a no-op rather than a write through it.                                      |
|  16 | `test_control_primitives`                        |   ✅   | Deadband is continuous at the band edge: it returns 0 inside and v shifted toward 0 outside. |
|  17 | `test_slew_reaches_the_target_without_overshoot` |   ✅   | Slew reaches the target without overshoot                                                    |
|  18 | `test_log_header_layout`                         |   ✅   | 0.5f is IEEE-754 0x3F000000, so a little-endian float ends with 0x3F.                        |
|  19 | `test_log_record_layout`                         |   ✅   | Log record layout                                                                            |
|  20 | `test_closed_loop_settles_on_the_setpoint`       |   ✅   | A pure proportional loop leaves the offset the I term removes.                               |

</details>

---

## test_cotp - native_cotp - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                    |
| --: | :--------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_published_constants`                           |   ✅   | Published constants                                                            |
|   2 | `test_smallest_tpkt_is_seven_octets`                 |   ✅   | Smallest tpkt is seven octets                                                  |
|   3 | `test_tpkt_length_includes_the_header`               |   ✅   | Tpkt length includes the header                                                |
|   4 | `test_consumed_advances_past_one_packet_in_a_stream` |   ✅   | Consumed advances past one packet in a stream                                  |
|   5 | `test_tpkt_refusals`                                 |   ✅   | Tpkt refusals                                                                  |
|   6 | `test_data_tpdu_layout`                              |   ✅   | Without EOT the third octet is 0: this TPDU is not the end of the TSDU.        |
|   7 | `test_connection_request_layout`                     |   ✅   | The TPDU-size parameter's value is an exponent: 0x0A means 2^10 = 1024 octets. |
|   8 | `test_connection_request_with_tsap_parameters`       |   ✅   | Parsed back out of the stream.                                                 |
|   9 | `test_connection_confirm_echoes_the_peer_reference`  |   ✅   | A CR's src-ref becomes the CC's dst-ref: the two halves of one connection.     |
|  10 | `test_type_is_the_high_nibble`                       |   ✅   | A credit in the low nibble does not change the type.                           |
|  11 | `test_cotp_refusals`                                 |   ✅   | Cotp refusals                                                                  |
|  12 | `test_stack_round_trip`                              |   ✅   | Stack round trip                                                               |

</details>

---

## test_x509 - native_x509 - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                           | Status | Description                                                                                |
| --: | :------------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_an_ed25519_leaf_reads_as_openssl_wrote_it`               |   ✅   | -set_serial 4919 == 0x1337, and sec 4.1.2.2 keeps it as it was encoded.                    |
|   2 | `test_a_p256_leaf_reports_its_curve`                           |   ✅   | RFC 5480 sec 2.2: an uncompressed P-256 point is 0x04 then two 32-octet coordinates.       |
|   3 | `test_an_rsa_leaf_reads_its_key`                               |   ✅   | A 2048-bit RSAPublicKey SEQUENCE is a little over 256 octets once the modulus and exponent |
|   4 | `test_a_ca_reports_its_constraints_and_usage`                  |   ✅   | It was not given digitalSignature, so it must not report one.                              |
|   5 | `test_a_leaf_is_not_a_ca`                                      |   ✅   | A leaf is not a ca                                                                         |
|   6 | `test_the_leaf_issuer_is_the_ca_subject_byte_for_byte`         |   ✅   | The leaf issuer is the ca subject byte for byte                                            |
|   7 | `test_nothing_and_rubbish_are_refused`                         |   ✅   | Nothing and rubbish are refused                                                            |
|   8 | `test_a_truncated_certificate_is_refused`                      |   ✅   | A truncated certificate is refused                                                         |
|   9 | `test_a_flipped_length_octet_does_not_run_off_the_buffer`      |   ✅   | No assertion on the verdict: a parse may legitimately still succeed. What must hold is     |
|  10 | `test_an_exact_dns_name_matches`                               |   ✅   | An exact dns name matches                                                                  |
|  11 | `test_a_name_matches_regardless_of_case_or_a_trailing_dot`     |   ✅   | A name matches regardless of case or a trailing dot                                        |
|  12 | `test_a_name_that_is_not_in_the_list_does_not_match`           |   ✅   | A name that is not in the list does not match                                              |
|  13 | `test_a_wildcard_matches_one_label`                            |   ✅   | A wildcard matches one label                                                               |
|  14 | `test_a_wildcard_does_not_span_a_dot_or_match_the_bare_domain` |   ✅   | A wildcard does not span a dot or match the bare domain                                    |
|  15 | `test_a_certificate_without_a_san_matches_nothing`             |   ✅   | A certificate without a san matches nothing                                                |
|  16 | `test_a_match_needs_both_a_certificate_and_a_name`             |   ✅   | A match needs both a certificate and a name                                                |

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

## test_dds - native_dds_rtps - ✅ 16 passed

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
|   9 | `test_parse_refuses_a_message_short_of_the_header`       |   ✅   | Parse refuses a message short of the header       |
|  10 | `test_parse_refuses_a_larger_major_version`              |   ✅   | Parse refuses a larger major version              |
|  11 | `test_any_minor_protocol_version_is_valid`               |   ✅   | Any minor protocol version is valid               |
|  12 | `test_contents_past_the_end_are_refused`                 |   ✅   | Contents past the end are refused                 |
|  13 | `test_a_partial_submessage_header_invalidates_the_rest`  |   ✅   | A partial submessage header invalidates the rest  |
|  14 | `test_submessage_kinds_match_the_published_enum`         |   ✅   | Submessage kinds match the published enum         |
|  15 | `test_submessage_refuses_a_short_buffer`                 |   ✅   | Submessage refuses a short buffer                 |
|  16 | `test_parse_without_a_sink_still_validates`              |   ✅   | Parse without a sink still validates              |

</details>

---

## test_der - native_der - ✅ 32 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                             | Status | Description                                                                                 |
| --: | :--------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_a_short_form_value_reports_its_content_and_successor`      |   ✅   | A short form value reports its content and successor                                        |
|   2 | `test_a_long_form_length_is_read`                                |   ✅   | A long form length is read                                                                  |
|   3 | `test_a_long_form_that_fits_the_short_one_is_refused`            |   ✅   | A long form that fits the short one is refused                                              |
|   4 | `test_a_long_form_with_a_leading_zero_is_refused`                |   ✅   | A long form with a leading zero is refused                                                  |
|   5 | `test_an_indefinite_length_is_refused`                           |   ✅   | An indefinite length is refused                                                             |
|   6 | `test_a_length_past_the_buffer_is_refused`                       |   ✅   | A length past the buffer is refused                                                         |
|   7 | `test_a_length_that_would_wrap_is_refused`                       |   ✅   | A length that would wrap is refused                                                         |
|   8 | `test_a_multi_octet_tag_is_refused`                              |   ✅   | A multi octet tag is refused                                                                |
|   9 | `test_an_empty_or_truncated_buffer_is_refused`                   |   ✅   | An empty or truncated buffer is refused                                                     |
|  10 | `test_entering_a_sequence_lands_on_its_first_field`              |   ✅   | The field after it.                                                                         |
|  11 | `test_entering_a_primitive_is_refused`                           |   ✅   | Entering a primitive is refused                                                             |
|  12 | `test_entering_an_empty_sequence_is_refused`                     |   ✅   | Entering an empty sequence is refused                                                       |
|  13 | `test_a_context_tag_is_entered_like_any_constructed_value`       |   ✅   | A context tag is entered like any constructed value                                         |
|  14 | `test_an_integer_reports_its_value`                              |   ✅   | An integer reports its value                                                                |
|  15 | `test_a_leading_zero_is_read_when_the_value_needs_it`            |   ✅   | A leading zero is read when the value needs it                                              |
|  16 | `test_a_redundant_leading_zero_is_refused`                       |   ✅   | A redundant leading zero is refused                                                         |
|  17 | `test_a_negative_integer_is_refused`                             |   ✅   | A negative integer is refused                                                               |
|  18 | `test_an_integer_wider_than_the_value_is_refused`                |   ✅   | An integer wider than the value is refused                                                  |
|  19 | `test_an_empty_integer_is_refused`                               |   ✅   | An empty integer is refused                                                                 |
|  20 | `test_a_bit_string_yields_its_octets_past_the_unused_count`      |   ✅   | A bit string yields its octets past the unused count                                        |
|  21 | `test_a_bit_string_with_unused_bits_is_refused`                  |   ✅   | A bit string with unused bits is refused                                                    |
|  22 | `test_an_empty_bit_string_is_refused`                            |   ✅   | An empty bit string is refused                                                              |
|  23 | `test_an_oid_matches_only_itself`                                |   ✅   | An oid matches only itself                                                                  |
|  24 | `test_an_oid_prefix_is_not_a_match`                              |   ✅   | An oid prefix is not a match                                                                |
|  25 | `test_a_value_that_is_not_an_oid_does_not_match_one`             |   ✅   | A value that is not an oid does not match one                                               |
|  26 | `test_a_utc_time_below_the_pivot_is_this_century`                |   ✅   | A utc time below the pivot is this century                                                  |
|  27 | `test_a_utc_time_at_or_above_the_pivot_is_last_century`          |   ✅   | At the pivot itself: 50 is 1950, which is before the epoch this reports seconds from, so it |
|  28 | `test_a_generalized_time_carries_its_whole_year`                 |   ✅   | A generalized time carries its whole year                                                   |
|  29 | `test_a_leap_day_is_counted`                                     |   ✅   | A leap day is counted                                                                       |
|  30 | `test_a_time_missing_its_seconds_or_zone_is_refused`             |   ✅   | A time missing its seconds or zone is refused                                               |
|  31 | `test_a_time_with_a_non_digit_or_an_impossible_field_is_refused` |   ✅   | A time with a non digit or an impossible field is refused                                   |
|  32 | `test_a_value_that_is_not_a_time_is_refused`                     |   ✅   | A value that is not a time is refused                                                       |

</details>

---

## test_deflate - native_codec_deflate - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                              |
| --: | :------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_rfc1951_fixed_block_bytes`                   |   ✅   | "Hi": 'H' is 72 -> 00110000 + 72 = 01111000, 'i' is 105 -> 00110000 + 105 = 10011001.    |
|   2 | `test_payload_is_a_non_final_fixed_block`          |   ✅   | Payload is a non final fixed block                                                       |
|   3 | `test_marker_is_stripped_from_the_reported_length` |   ✅   | Deflate wrote clen + 4 octets and reported clen; the four past it are the marker itself. |
|   4 | `test_round_trip_text`                             |   ✅   | Round trip text                                                                          |
|   5 | `test_round_trip_empty`                            |   ✅   | Round trip empty                                                                         |
|   6 | `test_round_trip_single_byte`                      |   ✅   | Round trip single byte                                                                   |
|   7 | `test_round_trip_every_octet_value`                |   ✅   | Round trip every octet value                                                             |
|   8 | `test_repetitive_input_shrinks`                    |   ✅   | Repetitive input shrinks                                                                 |
|   9 | `test_json_frame_shrinks`                          |   ✅   | Json frame shrinks                                                                       |
|  10 | `test_hash_chain_exhaustion_round_trips`           |   ✅   | Hash chain exhaustion round trips                                                        |
|  11 | `test_match_past_the_window_is_not_used`           |   ✅   | Match past the window is not used                                                        |
|  12 | `test_longest_match_round_trips`                   |   ✅   | Longest match round trips                                                                |
|  13 | `test_random_input_round_trips`                    |   ✅   | Random input round trips                                                                 |
|  14 | `test_low_entropy_input_round_trips`               |   ✅   | Low entropy input round trips                                                            |
|  15 | `test_output_overflow_fails_closed`                |   ✅   | Output overflow fails closed                                                             |
|  16 | `test_scratch_too_small_fails_closed`              |   ✅   | Scratch too small fails closed                                                           |

</details>

---

## test_device_id - native_device_id - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                     | Status | Description                                                                |
| --: | :--------------------------------------- | :----: | :------------------------------------------------------------------------- |
|   1 | `test_rfc9562_published_uuidv5_vector`   |   ✅   | the published SHA-1, before the version and variant octets are overwritten |
|   2 | `test_the_uuid_is_uuidv5_of_the_mac_hex` |   ✅   | The uuid is uuidv5 of the mac hex                                          |
|   3 | `test_the_version_and_variant_nibbles`   |   ✅   | The version and variant nibbles                                            |
|   4 | `test_the_text_form`                     |   ✅   | The text form                                                              |
|   5 | `test_the_uuid_is_stable_for_a_mac`      |   ✅   | The uuid is stable for a mac                                               |
|   6 | `test_every_mac_octet_changes_the_uuid`  |   ✅   | and a single nibble is enough, so the hex name is not being truncated      |
|   7 | `test_the_name_is_lowercase_hex`         |   ✅   | The name is lowercase hex                                                  |

</details>

---

## test_devicenet - native_devicenet - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                 |
| --: | :---------------------------------------------------- | :----: | :-------------------------------------------------------------------------- |
|   1 | `test_published_constants`                            |   ✅   | The type and count fields partition the octet.                              |
|   2 | `test_group_ranges_match_the_identifier_allocation`   |   ✅   | Group ranges match the identifier allocation                                |
|   3 | `test_group_three_message_id_seven_has_no_identifier` |   ✅   | Group three message id seven has no identifier                              |
|   4 | `test_identifier_round_trip`                          |   ✅   | Identifier round trip                                                       |
|   5 | `test_duplicate_mac_id_check_identifier`              |   ✅   | A master's explicit request to slave MAC 5: 0x400 \| (5 << 3) \| 4 = 0x42C. |
|   6 | `test_invalid_identifiers_and_fields`                 |   ✅   | Invalid identifiers and fields                                              |
|   7 | `test_message_header_octet`                           |   ✅   | A MAC id wider than six bits cannot reach the flag bits.                    |
|   8 | `test_fragmentation_octet`                            |   ✅   | Neither field can overflow into the other.                                  |
|   9 | `test_single_frame_explicit_message`                  |   ✅   | The header octet plus the body must fit the 8-octet CAN payload.            |
|  10 | `test_fragment_frame_layout`                          |   ✅   | Fragment frame layout                                                       |
|  11 | `test_unfragmented_message_completes_immediately`     |   ✅   | A header octet alone is a complete, empty message.                          |
|  12 | `test_fragmented_message_reassembly`                  |   ✅   | Fragmented message reassembly                                               |
|  13 | `test_reassembly_refusals`                            |   ✅   | A middle fragment with no first one is an error.                            |
|  14 | `test_a_new_first_fragment_restarts_the_message`      |   ✅   | A new first fragment restarts the message                                   |
|  15 | `test_fragment_count_wraps_at_sixty_four`             |   ✅   | Fragment count wraps at sixty four                                          |

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

## test_diffserv - native_diffserv - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                              |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_dscp_to_tos_encode`                             |   ✅   | Dscp to tos encode                                                                       |
|   2 | `test_default_dscp_roundtrip`                         |   ✅   | Masked to six bits on write, so a caller cannot spill into the two ECN bits.             |
|   3 | `test_udp_dscp_roundtrip`                             |   ✅   | Udp dscp roundtrip                                                                       |
|   4 | `test_the_two_defaults_are_separate_marks`            |   ✅   | The two defaults are separate marks                                                      |
|   5 | `test_the_flat_readers_report_what_the_entries_wrote` |   ✅   | The flat readers report what the entries wrote                                           |
|   6 | `test_listen_set_dscp_override_and_sentinel`          |   ✅   | set_dscp names the port, not the row: it walks the pool for the active listener bound to |
|   7 | `test_accept_cb_applies_per_listener_dscp_override`   |   ✅   | Accept cb applies per listener dscp override                                             |
|   8 | `test_accept_cb_falls_back_to_server_default_dscp`    |   ✅   | Accept cb falls back to server default dscp                                              |
|   9 | `test_accept_cb_skips_tos_write_at_best_effort`       |   ✅   | Accept cb skips tos write at best effort                                                 |
|  10 | `test_dynamic_listener_inherits_default_dscp`         |   ✅   | Dynamic listener inherits default dscp                                                   |

</details>

---

## test_digest_vectors - native_sha256_kat - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                 |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_rfc6234_published_vectors`                 |   ✅   | TEST4 is 64 octets and is fed 10 times: 640 octets, an exact multiple of the 512-bit block. |
|   2 | `test_rfc6234_one_million_a`                     |   ✅   | Rfc6234 one million a                                                                       |
|   3 | `test_empty_message`                             |   ✅   | Empty message                                                                               |
|   4 | `test_chunk_boundaries_do_not_change_the_digest` |   ✅   | Chunk boundaries do not change the digest                                                   |
|   5 | `test_empty_update_is_a_no_op`                   |   ✅   | Empty update is a no op                                                                     |
|   6 | `test_final_leaves_the_context_running`          |   ✅   | Reading it twice in a row must give the same answer.                                        |
|   7 | `test_one_shot_matches_streaming`                |   ✅   | One shot matches streaming                                                                  |
|   8 | `test_distinct_messages_hash_differently`        |   ✅   | Distinct messages hash differently                                                          |
|   9 | `test_block_length_constants`                    |   ✅   | 64 octets of TEST4 is one whole block; its digest must differ from the 63-octet prefix.     |

</details>

---

## test_aes_block - native_aes_block - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                     | Status | Description                                     |
| --: | :--------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_fips197_c1_aes128`                 |   ✅   | Fips197 c1 aes128                               |
|   2 | `test_fips197_c3_aes256`                 |   ✅   | Fips197 c3 aes256                               |
|   3 | `test_fips197_a1_key_schedule`           |   ✅   | w0..w3 are the key itself, big-endian per word. |
|   4 | `test_in_and_out_may_alias`              |   ✅   | In and out may alias                            |
|   5 | `test_schedule_is_a_function_of_the_key` |   ✅   | Schedule is a function of the key               |
|   6 | `test_inline_and_namespace_agree`        |   ✅   | Inline and namespace agree                      |
|   7 | `test_null_operands_are_refused`         |   ✅   | Null operands are refused                       |

</details>

---

## test_digest_vectors - native_sha256_kat_hw - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                 |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_rfc6234_published_vectors`                 |   ✅   | TEST4 is 64 octets and is fed 10 times: 640 octets, an exact multiple of the 512-bit block. |
|   2 | `test_rfc6234_one_million_a`                     |   ✅   | Rfc6234 one million a                                                                       |
|   3 | `test_empty_message`                             |   ✅   | Empty message                                                                               |
|   4 | `test_chunk_boundaries_do_not_change_the_digest` |   ✅   | Chunk boundaries do not change the digest                                                   |
|   5 | `test_empty_update_is_a_no_op`                   |   ✅   | Empty update is a no op                                                                     |
|   6 | `test_final_leaves_the_context_running`          |   ✅   | Reading it twice in a row must give the same answer.                                        |
|   7 | `test_one_shot_matches_streaming`                |   ✅   | One shot matches streaming                                                                  |
|   8 | `test_distinct_messages_hash_differently`        |   ✅   | Distinct messages hash differently                                                          |
|   9 | `test_block_length_constants`                    |   ✅   | 64 octets of TEST4 is one whole block; its digest must differ from the 63-octet prefix.     |

</details>

---

## test_sha384 - native_sha384_kat - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                                             |
| --: | :---------------------------------------- | :----: | :------------------------------------------------------ |
|   1 | `test_rfc6234_published_vectors`          |   ✅   | Row 1: one block with room for the mark and the length. |
|   2 | `test_rfc6234_exact_block_multiple`       |   ✅   | Rfc6234 exact block multiple                            |
|   3 | `test_rfc6234_one_million_a`              |   ✅   | Rfc6234 one million a                                   |
|   4 | `test_not_a_truncated_sha512`             |   ✅   | Not a truncated sha512                                  |
|   5 | `test_chunk_split_invariance`             |   ✅   | Chunk split invariance                                  |
|   6 | `test_final_leaves_the_context_running`   |   ✅   | Final leaves the context running                        |
|   7 | `test_one_shot_matches_streaming`         |   ✅   | One shot matches streaming                              |
|   8 | `test_distinct_messages_distinct_digests` |   ✅   | Distinct messages distinct digests                      |
|   9 | `test_block_length_constants`             |   ✅   | Block length constants                                  |

</details>

---

## test_sha384 - native_sha384_kat_hw - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                                             |
| --: | :---------------------------------------- | :----: | :------------------------------------------------------ |
|   1 | `test_rfc6234_published_vectors`          |   ✅   | Row 1: one block with room for the mark and the length. |
|   2 | `test_rfc6234_exact_block_multiple`       |   ✅   | Rfc6234 exact block multiple                            |
|   3 | `test_rfc6234_one_million_a`              |   ✅   | Rfc6234 one million a                                   |
|   4 | `test_not_a_truncated_sha512`             |   ✅   | Not a truncated sha512                                  |
|   5 | `test_chunk_split_invariance`             |   ✅   | Chunk split invariance                                  |
|   6 | `test_final_leaves_the_context_running`   |   ✅   | Final leaves the context running                        |
|   7 | `test_one_shot_matches_streaming`         |   ✅   | One shot matches streaming                              |
|   8 | `test_distinct_messages_distinct_digests` |   ✅   | Distinct messages distinct digests                      |
|   9 | `test_block_length_constants`             |   ✅   | Block length constants                                  |

</details>

---

## test_hmac_sha384 - native_hmac_sha384_kat - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                                 |
| --: | :---------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_rfc4231_published_vectors`                |   ✅   | Case 1: a 20-octet key, an 8-octet message.                                                 |
|   2 | `test_the_block_is_the_sha512_block`            |   ✅   | A 128-octet key is padded; a 129-octet one is pre-hashed. Two different keys, two different |
|   3 | `test_not_a_truncated_hmac_sha512`              |   ✅   | Not a truncated hmac sha512                                                                 |
|   4 | `test_streaming_matches_one_shot`               |   ✅   | Streaming matches one shot                                                                  |
|   5 | `test_a_changed_key_or_message_changes_the_mac` |   ✅   | A changed key or message changes the mac                                                    |

</details>

---

## test_hmac_sha384 - native_hmac_sha384_kat_hw - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                                 |
| --: | :---------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_rfc4231_published_vectors`                |   ✅   | Case 1: a 20-octet key, an 8-octet message.                                                 |
|   2 | `test_the_block_is_the_sha512_block`            |   ✅   | A 128-octet key is padded; a 129-octet one is pre-hashed. Two different keys, two different |
|   3 | `test_not_a_truncated_hmac_sha512`              |   ✅   | Not a truncated hmac sha512                                                                 |
|   4 | `test_streaming_matches_one_shot`               |   ✅   | Streaming matches one shot                                                                  |
|   5 | `test_a_changed_key_or_message_changes_the_mac` |   ✅   | A changed key or message changes the mac                                                    |

</details>

---

## test_hkdf_sha384 - native_hkdf_sha384_kat - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                             |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_extract`                                       |   ✅   | Extract                                                                                 |
|   2 | `test_expand`                                        |   ✅   | Expand                                                                                  |
|   3 | `test_expand_label`                                  |   ✅   | Expand label                                                                            |
|   4 | `test_the_two_label_forms_agree_on_an_empty_context` |   ✅   | The two label forms agree on an empty context                                           |
|   5 | `test_the_label_and_prefix_are_bound_in`             |   ✅   | The label and prefix are bound in                                                       |
|   6 | `test_the_expand_cap_is_at_the_sha384_block`         |   ✅   | One octet past the cap: refused, and the buffer is left zeroed rather than half-filled. |
|   7 | `test_null_operands_are_refused`                     |   ✅   | Null operands are refused                                                               |

</details>

---

## test_directnet - native_directnet - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                                                              |
| --: | :---------------------------------------- | :----: | :----------------------------------------------------------------------- |
|   1 | `test_ascii_control_codes`                |   ✅   | Ascii control codes                                                      |
|   2 | `test_lrc_block_xors_to_zero`             |   ✅   | An empty span has no octets to fold, so its LRC is the identity element. |
|   3 | `test_header_frame`                       |   ✅   | Header frame                                                             |
|   4 | `test_header_hex_digits_are_uppercase`    |   ✅   | The whole framed block, LRC included, folds to zero.                     |
|   5 | `test_data_frame`                         |   ✅   | Data frame                                                               |
|   6 | `test_data_frame_round_trip`              |   ✅   | Data frame round trip                                                    |
|   7 | `test_data_parse_optional_outputs`        |   ✅   | Data parse optional outputs                                              |
|   8 | `test_single_octet_corruption_is_refused` |   ✅   | Single octet corruption is refused                                       |
|   9 | `test_data_parse_rejects_bad_framing`     |   ✅   | Data parse rejects bad framing                                           |
|  10 | `test_builders_refuse_a_short_buffer`     |   ✅   | Builders refuse a short buffer                                           |

</details>

---

## test_dma - native_dma - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_dmx - native_dmx - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                          |
| --: | :---------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_e120_table_6_6_checksum_example`                      |   ✅   | 6.2.11: the checksum is the additive sum of slots 0 through 24.                      |
|   2 | `test_e120_appendix_a_constants`                            |   ✅   | E120 appendix a constants                                                            |
|   3 | `test_e120_message_length_points_at_the_checksum_high_slot` |   ✅   | E120 message length points at the checksum high slot                                 |
|   4 | `test_e120_uid_is_manufacturer_above_device`                |   ✅   | E120 uid is manufacturer above device                                                |
|   5 | `test_e120_table_7_1_discovery_response_encoding`           |   ✅   | Table 7-1 shows the full seven preamble slots; 7.5 allows 0 to 7.                    |
|   6 | `test_e120_table_7_2_discovery_response_decoding`           |   ✅   | The Table 7-1 response for 0x123456789ABC, written out rather than rebuilt.          |
|   7 | `test_discovery_response_round_trips`                       |   ✅   | Discovery response round trips                                                       |
|   8 | `test_discovery_response_builder_guards`                    |   ✅   | Discovery response builder guards                                                    |
|   9 | `test_e120_device_info_block`                               |   ✅   | 10.6.3: 0xFFFF is the start address of a device that uses no DMX512 slots.           |
|  10 | `test_device_info_rides_a_get_response_packet`              |   ✅   | Device info rides a get response packet                                              |
|  11 | `test_e120_parse_discards_malformed_packets`                |   ✅   | E120 parse discards malformed packets                                                |
|  12 | `test_rdm_build_guards`                                     |   ✅   | Rdm build guards                                                                     |
|  13 | `test_e111_slot_array`                                      |   ✅   | A start code other than 0x00 is carried through: RDM rides the same wire as SC 0xCC. |
|  14 | `test_e111_universe_is_512_slots`                           |   ✅   | E111 universe is 512 slots                                                           |
|  15 | `test_dmx_build_guards`                                     |   ✅   | No slots at all is a legal packet: the start code alone.                             |

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

|   # | Test                                                     | Status | Description                                                                                   |
| --: | :------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_crc16_dnp_published_check_value`                   |   ✅   | init 0x0000 with a final XOR of 0xFFFF, so the empty message is 0xFFFF.                       |
|   2 | `test_header_block_field_layout`                         |   ✅   | Header block field layout                                                                     |
|   3 | `test_user_data_is_carried_in_crc_protected_blocks`      |   ✅   | User data is carried in crc protected blocks                                                  |
|   4 | `test_frame_round_trip_across_block_boundaries`          |   ✅   | Frame round trip across block boundaries                                                      |
|   5 | `test_parse_rejects_a_corrupted_block`                   |   ✅   | Parse rejects a corrupted block                                                               |
|   6 | `test_parse_rejects_malformed_framing`                   |   ✅   | Parse rejects malformed framing                                                               |
|   7 | `test_build_refuses_oversized_or_unbuffered_frames`      |   ✅   | Build refuses oversized or unbuffered frames                                                  |
|   8 | `test_transport_header_bit_layout`                       |   ✅   | The sequence is 6 bits wide, so a wider value cannot reach the FIR / FIN bits.                |
|   9 | `test_transport_segment_build`                           |   ✅   | Transport segment build                                                                       |
|  10 | `test_transport_reassembles_a_multi_segment_fragment`    |   ✅   | A single-frame fragment sets both FIR and FIN and completes on its own.                       |
|  11 | `test_transport_sequence_wraps_at_sixty_four`            |   ✅   | Transport sequence wraps at sixty four                                                        |
|  12 | `test_transport_discards_out_of_sequence_segments`       |   ✅   | A fresh FIR restarts the fragment from zero rather than appending to the abandoned one.       |
|  13 | `test_transport_overflow_abandons_the_fragment`          |   ✅   | Transport overflow abandons the fragment                                                      |
|  14 | `test_application_control_bit_layout`                    |   ✅   | The sequence is 4 bits, so it cannot bleed into UNS.                                          |
|  15 | `test_application_request_round_trip`                    |   ✅   | A bare 2-octet fragment parses with no object data at all.                                    |
|  16 | `test_application_response_carries_internal_indications` |   ✅   | An unsolicited response is the other form that carries IIN.                                   |
|  17 | `test_object_header_range_picks_the_narrowest_form`      |   ✅   | A stop before the start names no objects, and a buffer too small writes nothing.              |
|  18 | `test_object_header_all_objects`                         |   ✅   | Object header all objects                                                                     |
|  19 | `test_object_header_count_forms_and_prefix_code`         |   ✅   | A truncated range field, and a qualifier form this decoder does not accept, are both refused. |
|  20 | `test_crob_field_layout`                                 |   ✅   | The clear bit is bit 5, and the trip code is 2 in bits 6-7.                                   |
|  21 | `test_analog_output_block_int32`                         |   ✅   | Analog output block int32                                                                     |
|  22 | `test_analog_output_block_float`                         |   ✅   | Analog output block float                                                                     |

</details>

---

## test_dns_resolver - native_dns_resolver - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                             |
| --: | :------------------------------------------------------ | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_rfc1035_published_qname`                          |   ✅   | Rfc1035 published qname                                                                 |
|   2 | `test_query_build_refuses_what_does_not_fit`            |   ✅   | Query build refuses what does not fit                                                   |
|   3 | `test_answer_parse_reads_the_a_record`                  |   ✅   | Answer parse reads the a record                                                         |
|   4 | `test_answer_parse_walks_past_other_types`              |   ✅   | Answer parse walks past other types                                                     |
|   5 | `test_answer_parse_requires_the_id_to_match`            |   ✅   | Answer parse requires the id to match                                                   |
|   6 | `test_answer_parse_requires_a_response_with_rcode_zero` |   ✅   | Answer parse requires a response with rcode zero                                        |
|   7 | `test_answer_parse_requires_an_a_record_in_class_in`    |   ✅   | Answer parse requires an a record in class in                                           |
|   8 | `test_answer_parse_refuses_a_truncated_message`         |   ✅   | Answer parse refuses a truncated message                                                |
|   9 | `test_classify_matches_the_registry`                    |   ✅   | Classify matches the registry                                                           |
|  10 | `test_verify_refuses_what_cannot_be_a_remote_host`      |   ✅   | Verify refuses what cannot be a remote host                                             |
|  11 | `test_a_literal_answers_itself`                         |   ✅   | A literal answers itself                                                                |
|  12 | `test_resolve_refuses_a_null_host`                      |   ✅   | Resolve refuses a null host                                                             |
|  13 | `test_a_name_puts_one_question_on_the_wire`             |   ✅   | A second name asked while that query is out does not put a second question on the wire. |
|  14 | `test_the_answer_completes_the_resolve`                 |   ✅   | The answer completes the resolve                                                        |
|  15 | `test_a_foreign_response_does_not_end_the_query`        |   ✅   | A foreign response does not end the query                                               |
|  16 | `test_the_query_ends_at_its_deadline`                   |   ✅   | The query ends at its deadline                                                          |
|  17 | `test_set_server_takes_only_an_address`                 |   ✅   | Set server takes only an address                                                        |
|  18 | `test_resolve_verified_refuses_an_implausible_answer`   |   ✅   | Resolve verified refuses an implausible answer                                          |

</details>

---

## test_dns_server - native_dns_server - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_a_record_answer`                         |   ✅   | A record answer                         |
|   2 | `test_nxdomain`                                |   ✅   | Nxdomain                                |
|   3 | `test_non_a_query_no_error`                    |   ✅   | Non a query no error                    |
|   4 | `test_multilabel_name_reaches_resolver`        |   ✅   | Multilabel name reaches resolver        |
|   5 | `test_malformed_guards`                        |   ✅   | Malformed guards                        |
|   6 | `test_table_add_lookup_case_insensitive`       |   ✅   | Table add lookup case insensitive       |
|   7 | `test_end_to_end_with_table`                   |   ✅   | End to end with table                   |
|   8 | `test_dns_opcode_notimp`                       |   ✅   | Dns opcode notimp                       |
|   9 | `test_dns_truncated_questions`                 |   ✅   | Dns truncated questions                 |
|  10 | `test_dns_oversized_name`                      |   ✅   | Dns oversized name                      |
|  11 | `test_dns_question_exceeds_out_cap`            |   ✅   | Dns question exceeds out cap            |
|  12 | `test_dns_add_and_lookup_guards`               |   ✅   | Dns add and lookup guards               |
|  13 | `test_dns_begin_answers_a_query_over_the_wire` |   ✅   | Dns begin answers a query over the wire |

</details>

---

## test_dns_wire - native_dns_wire - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                                 |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_rfc1035_worked_message`                            |   ✅   | Rfc1035 worked message                                                                      |
|   2 | `test_encode_is_length_prefixed_labels_and_a_root_octet` |   ✅   | sec 3.1's own note: the trailing dot writes the same octets, because the root octet is      |
|   3 | `test_encode_decode_round_trip`                          |   ✅   | Encode decode round trip                                                                    |
|   4 | `test_label_length_limit`                                |   ✅   | Label length limit                                                                          |
|   5 | `test_reserved_label_types_are_refused`                  |   ✅   | Reserved label types are refused                                                            |
|   6 | `test_pointer_loops_terminate`                           |   ✅   | A pointer at 0 aimed at 0.                                                                  |
|   7 | `test_pointers_are_refused_when_not_allowed`             |   ✅   | Offset 20 is pure labels, so it decodes either way.                                         |
|   8 | `test_truncated_names_are_refused`                       |   ✅   | Starting past the end of the message.                                                       |
|   9 | `test_output_buffer_bounds`                              |   ✅   | "abc.de" encodes as 3 a b c 2 d e 0 = 8 octets, and reads back as 6 text octets plus a NUL. |
|  10 | `test_encode_buffer_bounds`                              |   ✅   | "a.b" encodes as 1 a 1 b 0 = 5 octets, so four octets of room is a refusal.                 |
|  11 | `test_empty_labels_inside_a_name_are_refused`            |   ✅   | Empty labels inside a name are refused                                                      |
|  12 | `test_case_insensitive_comparison`                       |   ✅   | Only A-Z folds: the octets either side of the alphabetic ranges keep their identity, so     |
|  13 | `test_pointer_hop_cap`                                   |   ✅   | Nine pointers, each aimed two octets further on, with a real label after the last.          |
|  14 | `test_failure_reports_zero_progress`                     |   ✅   | Failure reports zero progress                                                               |

</details>

---

## test_dns_wire - native_dns_wire_codec - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                                 |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_rfc1035_worked_message`                            |   ✅   | Rfc1035 worked message                                                                      |
|   2 | `test_encode_is_length_prefixed_labels_and_a_root_octet` |   ✅   | sec 3.1's own note: the trailing dot writes the same octets, because the root octet is      |
|   3 | `test_encode_decode_round_trip`                          |   ✅   | Encode decode round trip                                                                    |
|   4 | `test_label_length_limit`                                |   ✅   | Label length limit                                                                          |
|   5 | `test_reserved_label_types_are_refused`                  |   ✅   | Reserved label types are refused                                                            |
|   6 | `test_pointer_loops_terminate`                           |   ✅   | A pointer at 0 aimed at 0.                                                                  |
|   7 | `test_pointers_are_refused_when_not_allowed`             |   ✅   | Offset 20 is pure labels, so it decodes either way.                                         |
|   8 | `test_truncated_names_are_refused`                       |   ✅   | Starting past the end of the message.                                                       |
|   9 | `test_output_buffer_bounds`                              |   ✅   | "abc.de" encodes as 3 a b c 2 d e 0 = 8 octets, and reads back as 6 text octets plus a NUL. |
|  10 | `test_encode_buffer_bounds`                              |   ✅   | "a.b" encodes as 1 a 1 b 0 = 5 octets, so four octets of room is a refusal.                 |
|  11 | `test_empty_labels_inside_a_name_are_refused`            |   ✅   | Empty labels inside a name are refused                                                      |
|  12 | `test_case_insensitive_comparison`                       |   ✅   | Only A-Z folds: the octets either side of the alphabetic ranges keep their identity, so     |
|  13 | `test_pointer_hop_cap`                                   |   ✅   | Nine pointers, each aimed two octets further on, with a real label after the last.          |
|  14 | `test_failure_reports_zero_progress`                     |   ✅   | Failure reports zero progress                                                               |

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

## test_edge_cache - native_edge_cache_core - ✅ 30 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                               | Status | Description                                                                                  |
| --: | :----------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_rfc9110_three_spellings_of_one_instant`                      |   ✅   | Rfc9110 three spellings of one instant                                                       |
|   2 | `test_http_date_anchor_instants`                                   |   ✅   | Http date anchor instants                                                                    |
|   3 | `test_http_date_refuses_text_that_names_no_instant`                |   ✅   | Http date refuses text that names no instant                                                 |
|   4 | `test_rfc9110_published_range_examples`                            |   ✅   | Rfc9110 published range examples                                                             |
|   5 | `test_rfc9110_last_pos_and_suffix_clamping`                        |   ✅   | The one-byte ends of both forms: first byte, last byte.                                      |
|   6 | `test_rfc9110_unsatisfiable_ranges`                                |   ✅   | Rfc9110 unsatisfiable ranges                                                                 |
|   7 | `test_rfc9110_an_invalid_int_range_is_never_served`                |   ✅   | Rfc9110 an invalid int range is never served                                                 |
|   8 | `test_rfc9110_zero_length_representation`                          |   ✅   | Rfc9110 zero length representation                                                           |
|   9 | `test_rfc9110_unusable_range_headers_fall_back_to_a_full_response` |   ✅   | Rfc9110 unusable range headers fall back to a full response                                  |
|  10 | `test_rfc9110_large_decimal_numerals_do_not_wrap`                  |   ✅   | Rfc9110 large decimal numerals do not wrap                                                   |
|  11 | `test_rfc9112_field_lookup_is_case_insensitive_and_ows_trimmed`    |   ✅   | Rfc9112 field lookup is case insensitive and ows trimmed                                     |
|  12 | `test_field_lookup_refuses_rather_than_truncates`                  |   ✅   | Field lookup refuses rather than truncates                                                   |
|  13 | `test_rfc9111_freshness_lifetime_precedence`                       |   ✅   | Rfc9111 freshness lifetime precedence                                                        |
|  14 | `test_rfc9111_heuristic_freshness_is_a_tenth_of_the_interval`      |   ✅   | Rfc9111 heuristic freshness is a tenth of the interval                                       |
|  15 | `test_rfc9111_corrected_initial_age`                               |   ✅   | Rfc9111 corrected initial age                                                                |
|  16 | `test_rfc9111_current_age_over_a_wrapping_millisecond_clock`       |   ✅   | Rfc9111 current age over a wrapping millisecond clock                                        |
|  17 | `test_rfc9111_fresh_predicate_is_strictly_greater`                 |   ✅   | Rfc9111 fresh predicate is strictly greater                                                  |
|  18 | `test_cache_key_is_canonical`                                      |   ✅   | Excluding the query collapses the two queries onto one key, and including it separates them. |
|  19 | `test_key_digest_matches_the_published_sha256_vector`              |   ✅   | Key digest matches the published sha256 vector                                               |
|  20 | `test_rfc9111_vary_secondary_key`                                  |   ✅   | No Vary nominates no field, so every request matches: one key, and it is empty.              |
|  21 | `test_store_alloc_and_lookup`                                      |   ✅   | Store alloc and lookup                                                                       |
|  22 | `test_store_evicts_the_least_recently_used_slot`                   |   ✅   | Store evicts the least recently used slot                                                    |
|  23 | `test_rfc9111_store_find_resolves_the_vary_variant`                |   ✅   | Rfc9111 store find resolves the vary variant                                                 |
|  24 | `test_store_purge_by_key_and_by_path_prefix`                       |   ✅   | Store purge by key and by path prefix                                                        |
|  25 | `test_sweep_drops_only_unrevalidatable_stale_entries`              |   ✅   | Sweep drops only unrevalidatable stale entries                                               |
|  26 | `test_rfc9111_storeability`                                        |   ✅   | Rfc9111 storeability                                                                         |
|  27 | `test_rfc9111_conditional_request_carries_the_stored_validators`   |   ✅   | A precondition cut in half asks a different question, so a buffer too small emits nothing.   |
|  28 | `test_rfc9111_a_304_freshens_and_keeps_the_stored_content`         |   ✅   | Rfc9111 a 304 freshens and keeps the stored content                                          |
|  29 | `test_freshness_falls_back_to_the_default_ttl`                     |   ✅   | Freshness falls back to the default ttl                                                      |
|  30 | `test_an_expires_in_the_past_stores_as_stale`                      |   ✅   | An expires in the past stores as stale                                                       |

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

## test_endian - native_endian - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                   | Status | Description                                                                    |
| --: | :----------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_rfc1071_normal_order_is_the_big_endian_read`     |   ✅   | The whole string as one 64-bit field: the two 32-bit halves in the same order. |
|   2 | `test_rfc1071_swapped_order_is_the_little_endian_read` |   ✅   | Rfc1071 swapped order is the little endian read                                |
|   3 | `test_rfc4251_uint32_octets`                           |   ✅   | Rfc4251 uint32 octets                                                          |
|   4 | `test_uint64_octets_in_decreasing_significance`        |   ✅   | Uint64 octets in decreasing significance                                       |
|   5 | `test_writers_return_their_width`                      |   ✅   | Writers return their width                                                     |
|   6 | `test_adjacent_fields_do_not_overlap`                  |   ✅   | Adjacent fields do not overlap                                                 |
|   7 | `test_round_trip_at_every_offset`                      |   ✅   | Round trip at every offset                                                     |
|   8 | `test_big_endian_is_the_byte_reverse_of_little_endian` |   ✅   | Big endian is the byte reverse of little endian                                |
|   9 | `test_a_narrow_write_drops_the_bits_above_its_width`   |   ✅   | A narrow write drops the bits above its width                                  |

</details>

---

## test_endian - native_mmgr_endian - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                   | Status | Description                                                                    |
| --: | :----------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_rfc1071_normal_order_is_the_big_endian_read`     |   ✅   | The whole string as one 64-bit field: the two 32-bit halves in the same order. |
|   2 | `test_rfc1071_swapped_order_is_the_little_endian_read` |   ✅   | Rfc1071 swapped order is the little endian read                                |
|   3 | `test_rfc4251_uint32_octets`                           |   ✅   | Rfc4251 uint32 octets                                                          |
|   4 | `test_uint64_octets_in_decreasing_significance`        |   ✅   | Uint64 octets in decreasing significance                                       |
|   5 | `test_writers_return_their_width`                      |   ✅   | Writers return their width                                                     |
|   6 | `test_adjacent_fields_do_not_overlap`                  |   ✅   | Adjacent fields do not overlap                                                 |
|   7 | `test_round_trip_at_every_offset`                      |   ✅   | Round trip at every offset                                                     |
|   8 | `test_big_endian_is_the_byte_reverse_of_little_endian` |   ✅   | Big endian is the byte reverse of little endian                                |
|   9 | `test_a_narrow_write_drops_the_bits_above_its_width`   |   ✅   | A narrow write drops the bits above its width                                  |

</details>

---

## test_enip - native_enip - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                     | Status | Description                                                                                |
| --: | :--------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_published_registry_values`         |   ✅   | Published registry values                                                                  |
|   2 | `test_register_session_octets`           |   ✅   | A null context is the same request with eight zero octets.                                 |
|   3 | `test_headers_without_command_data`      |   ✅   | Headers without command data                                                               |
|   4 | `test_send_rr_data_common_packet_format` |   ✅   | The reply extractor walks the same CPF and hands back exactly the CIP octets.              |
|   5 | `test_header_round_trip`                 |   ✅   | Header round trip                                                                          |
|   6 | `test_parse_refuses_a_truncated_message` |   ✅   | Parse refuses a truncated message                                                          |
|   7 | `test_list_identity_item`                |   ✅   | item[2..17] is the 16-octet socket address; left zero, the codec does not reinterpret it.  |
|   8 | `test_cpf_walk_refuses_a_missing_item`   |   ✅   | interface handle(4) + timeout(2) + count(2)=1 + a Connected Data item carrying two octets. |
|   9 | `test_builders_refuse_a_short_buffer`    |   ✅   | Builders refuse a short buffer                                                             |

</details>

---

## test_enocean - native_enocean - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                                          |
| --: | :--------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_esp3_crc8_published_table`               |   ✅   | The CRC catalogue's published check value for CRC-8/SMBUS: the CRC of the nine ASCII |
|   2 | `test_esp3_published_crc_octets`               |   ✅   | Esp3 published crc octets                                                            |
|   3 | `test_esp3_telegram_field_offsets`             |   ✅   | Esp3 telegram field offsets                                                          |
|   4 | `test_esp3_build_parse_round_trip`             |   ✅   | Every ESP3 packet type frames the same way; only the type octet differs.             |
|   5 | `test_esp3_parse_waits_for_the_whole_telegram` |   ✅   | Trailing octets past the telegram are left for the next frame call.                  |
|   6 | `test_esp3_resynchronizes_on_a_bad_frame`      |   ✅   | Not a sync octet at all.                                                             |
|   7 | `test_esp3_build_fails_closed`                 |   ✅   | Esp3 build fails closed                                                              |
|   8 | `test_erp1_field_layout`                       |   ✅   | RPS (0xF6, rocker switches) carries one payload octet.                               |
|   9 | `test_erp1_round_trip`                         |   ✅   | Erp1 round trip                                                                      |
|  10 | `test_erp1_fails_closed`                       |   ✅   | Erp1 fails closed                                                                    |
|  11 | `test_erp1_inside_an_esp3_packet`              |   ✅   | Erp1 inside an esp3 packet                                                           |

</details>

---

## test_enocean - native_enocean_esp3 - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                                          |
| --: | :--------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_esp3_crc8_published_table`               |   ✅   | The CRC catalogue's published check value for CRC-8/SMBUS: the CRC of the nine ASCII |
|   2 | `test_esp3_published_crc_octets`               |   ✅   | Esp3 published crc octets                                                            |
|   3 | `test_esp3_telegram_field_offsets`             |   ✅   | Esp3 telegram field offsets                                                          |
|   4 | `test_esp3_build_parse_round_trip`             |   ✅   | Every ESP3 packet type frames the same way; only the type octet differs.             |
|   5 | `test_esp3_parse_waits_for_the_whole_telegram` |   ✅   | Trailing octets past the telegram are left for the next frame call.                  |
|   6 | `test_esp3_resynchronizes_on_a_bad_frame`      |   ✅   | Not a sync octet at all.                                                             |
|   7 | `test_esp3_build_fails_closed`                 |   ✅   | Esp3 build fails closed                                                              |
|   8 | `test_erp1_field_layout`                       |   ✅   | RPS (0xF6, rocker switches) carries one payload octet.                               |
|   9 | `test_erp1_round_trip`                         |   ✅   | Erp1 round trip                                                                      |
|  10 | `test_erp1_fails_closed`                       |   ✅   | Erp1 fails closed                                                                    |
|  11 | `test_erp1_inside_an_esp3_packet`              |   ✅   | Erp1 inside an esp3 packet                                                           |

</details>

---

## test_espnow - native_espnow_envelope - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                         |
| --: | :---------------------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_envelope_field_layout`                          |   ✅   | A message with no payload is the header alone.                                      |
|   2 | `test_encode_decode_round_trip`                       |   ✅   | Encode decode round trip                                                            |
|   3 | `test_decode_requires_the_declared_length_exactly`    |   ✅   | The declared length itself is what is checked, not the octet count alone.           |
|   4 | `test_decode_requires_the_magic_octet`                |   ✅   | Decode requires the magic octet                                                     |
|   5 | `test_decode_fails_closed`                            |   ✅   | Every output is optional: a caller that wants only the verdict passes none of them. |
|   6 | `test_payload_cap_is_the_radio_limit_less_the_header` |   ✅   | The declared length still fits the one octet that carries it.                       |
|   7 | `test_encode_fails_closed`                            |   ✅   | Encode fails closed                                                                 |
|   8 | `test_peer_registry_membership`                       |   ✅   | Peer registry membership                                                            |
|   9 | `test_peer_registry_is_bounded`                       |   ✅   | Peer registry is bounded                                                            |
|  10 | `test_broadcast_address_is_all_ones`                  |   ✅   | Broadcast address is all ones                                                       |
|  11 | `test_radio_binding_reports_no_radio`                 |   ✅   | Radio binding reports no radio                                                      |

</details>

---

## test_euromap77 - native_euromap77 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                | Status | Description                                                  |
| --: | :------------------------------------------------------------------ | :----: | :----------------------------------------------------------- |
|   1 | `test_the_namespace_uris_are_the_published_model_uris`              |   ✅   | The namespace uris are the published model uris              |
|   2 | `test_the_two_namespace_uris_differ`                                |   ✅   | The two namespace uris differ                                |
|   3 | `test_the_machine_mode_enumeration_is_the_published_one`            |   ✅   | The machine mode enumeration is the published one            |
|   4 | `test_the_job_status_enumeration_is_the_published_one`              |   ✅   | The job status enumeration is the published one              |
|   5 | `test_the_browse_hierarchy_carries_the_published_browsenames`       |   ✅   | The browse hierarchy carries the published browsenames       |
|   6 | `test_the_containers_are_objects_and_the_leaves_are_variables`      |   ✅   | The containers are objects and the leaves are variables      |
|   7 | `test_each_leaf_carries_the_reference_type_the_nodeset_publishes`   |   ✅   | Each leaf carries the reference type the nodeset publishes   |
|   8 | `test_each_leaf_serves_the_variant_its_published_datatype_requires` |   ✅   | Each leaf serves the variant its published datatype requires |
|   9 | `test_every_value_comes_from_the_bound_model`                       |   ✅   | Every value comes from the bound model                       |
|  10 | `test_the_counters_keep_the_full_published_uint64_width`            |   ✅   | The counters keep the full published uint64 width            |
|  11 | `test_a_read_follows_the_model_without_a_rebind`                    |   ✅   | A read follows the model without a rebind                    |
|  12 | `test_absent_strings_read_as_empty`                                 |   ✅   | Absent strings read as empty                                 |
|  13 | `test_the_interface_name_comes_from_the_bound_model`                |   ✅   | The interface name comes from the bound model                |
|  14 | `test_the_objects_folder_organizes_the_interface`                   |   ✅   | The objects folder organizes the interface                   |
|  15 | `test_every_reference_is_forward_and_in_the_models_namespace`       |   ✅   | Every reference is forward and in the models namespace       |
|  16 | `test_reads_outside_the_model_are_refused`                          |   ✅   | Reads outside the model are refused                          |
|  17 | `test_browse_outside_the_model_is_refused`                          |   ✅   | Browse outside the model is refused                          |
|  18 | `test_an_unbound_model_serves_nothing`                              |   ✅   | An unbound model serves nothing                              |
|  19 | `test_browse_respects_the_caller_bound`                             |   ✅   | Browse respects the caller bound                             |

</details>

---

## test_failsafe - native_failsafe - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                    |
| --: | :------------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_overdue_is_a_wrap_safe_unsigned_delta`             |   ✅   | and away from the wrap, the same rule: equal to the deadline is still fed      |
|   2 | `test_a_lifeline_starts_fed`                             |   ✅   | A lifeline starts fed                                                          |
|   3 | `test_a_feed_moves_the_deadline`                         |   ✅   | A feed moves the deadline                                                      |
|   4 | `test_the_registry_is_bounded`                           |   ✅   | The registry is bounded                                                        |
|   5 | `test_check_reports_one_bit_per_lifeline`                |   ✅   | Check reports one bit per lifeline                                             |
|   6 | `test_a_breach_fires_once_per_episode`                   |   ✅   | A breach fires once per episode                                                |
|   7 | `test_a_feed_rearms_the_callback`                        |   ✅   | A feed rearms the callback                                                     |
|   8 | `test_a_feed_names_an_armed_lifeline`                    |   ✅   | A feed names an armed lifeline                                                 |
|   9 | `test_the_report_is_an_rfc8259_object`                   |   ✅   | motor: 1260 - 1200 = 60, inside its 100. loop: 1260 - 1000 = 260, past its 50. |
|  10 | `test_an_empty_registry_still_reports_an_object`         |   ✅   | An empty registry still reports an object                                      |
|  11 | `test_the_report_stays_inside_its_buffer`                |   ✅   | The report stays inside its buffer                                             |
|  12 | `test_the_report_refuses_null_and_zero_capacity`         |   ✅   | The report refuses null and zero capacity                                      |
|  13 | `test_reset_empties_the_registry_and_drops_the_callback` |   ✅   | Reset empties the registry and drops the callback                              |

</details>

---

## test_fanuc_j519 - native_fanuc_j519 - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_packet_lengths`                           |   ✅   | Packet lengths                           |
|   2 | `test_udp_port_and_block_counts`                |   ✅   | Udp port and block counts                |
|   3 | `test_packet_type_codes`                        |   ✅   | Packet type codes                        |
|   4 | `test_data_style_codes`                         |   ✅   | Data style codes                         |
|   5 | `test_io_type_codes`                            |   ✅   | Io type codes                            |
|   6 | `test_threshold_type_codes`                     |   ✅   | Threshold type codes                     |
|   7 | `test_status_bit_masks`                         |   ✅   | Status bit masks                         |
|   8 | `test_motion_octet_field_offsets`               |   ✅   | Motion octet field offsets               |
|   9 | `test_status_octet_field_offsets`               |   ✅   | Status octet field offsets               |
|  10 | `test_header_words_are_big_endian`              |   ✅   | Header words are big endian              |
|  11 | `test_body_integers_are_big_endian`             |   ✅   | Body integers are big endian             |
|  12 | `test_axis_values_are_big_endian_binary32`      |   ✅   | Axis values are big endian binary32      |
|  13 | `test_builders_leave_no_residue`                |   ✅   | Builders leave no residue                |
|  14 | `test_motion_round_trip`                        |   ✅   | Motion round trip                        |
|  15 | `test_status_round_trip`                        |   ✅   | Status round trip                        |
|  16 | `test_request_and_ack_round_trip`               |   ✅   | Request and ack round trip               |
|  17 | `test_axis_values_survive_the_binary32_packing` |   ✅   | Axis values survive the binary32 packing |
|  18 | `test_length_separates_the_shared_type_codes`   |   ✅   | Length separates the shared type codes   |
|  19 | `test_parsers_check_the_type_word`              |   ✅   | Parsers check the type word              |
|  20 | `test_peek_needs_a_whole_header`                |   ✅   | Peek needs a whole header                |
|  21 | `test_builders_refuse_a_short_buffer`           |   ✅   | Builders refuse a short buffer           |
|  22 | `test_parsers_refuse_missing_arguments`         |   ✅   | Parsers refuse missing arguments         |

</details>

---

## test_fdc2214 - native_fdc2214 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                                                                  |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_snoscz5_data_register_pair_is_a_28_bit_result`            |   ✅   | msb bits 11:0 = 0x123, lsb = 0x4567 -> 0x1234567                                             |
|   2 | `test_data_never_exceeds_twenty_eight_bits`                     |   ✅   | Data never exceeds twenty eight bits                                                         |
|   3 | `test_snoscz5_status_flags_come_from_the_top_nibble`            |   ✅   | The flag field and the data field partition the MSB register: neither ever reads the other.  |
|   4 | `test_snoscz5_sensor_frequency_scales_data_over_two_to_the_28`  |   ✅   | the product is formed in 64 bits: full-scale data against a 40 MHz reference does not wrap   |
|   5 | `test_sensor_frequency_is_monotone_in_the_data`                 |   ✅   | Sensor frequency is monotone in the data                                                     |
|   6 | `test_config_sequence_register_order_and_addresses`             |   ✅   | CONFIG is the last write of the sequence.                                                    |
|   7 | `test_snoscz5_register_addresses`                               |   ✅   | Snoscz5 register addresses                                                                   |
|   8 | `test_snoscz5_identity_registers`                               |   ✅   | Snoscz5 identity registers                                                                   |
|   9 | `test_config_builder_fails_closed`                              |   ✅   | Config builder fails closed                                                                  |
|  10 | `test_snoscz5_model_reset_values`                               |   ✅   | 7.4.1: it powers up in Sleep Mode, waiting to be configured                                  |
|  11 | `test_snoscz5_begin_refuses_a_part_that_is_not_an_fdc`          |   ✅   | the 12-bit sibling is accepted                                                               |
|  12 | `test_snoscz5_begin_leaves_the_part_configured_and_converting`  |   ✅   | 7.6.28 Table 7-38 requires bits 12 and 10 set and bit 8 clear; the written word obeys it     |
|  13 | `test_snoscz5_a_conversion_reads_back_whole`                    |   ✅   | Snoscz5 a conversion reads back whole                                                        |
|  14 | `test_snoscz5_the_error_flags_do_not_leak_into_the_result`      |   ✅   | and the flags are readable where the datasheet puts them                                     |
|  15 | `test_snoscz5_the_low_half_is_latched_by_reading_the_high_half` |   ✅   | read the low half on its own: it holds whatever the last high read latched, which is nothing |
|  16 | `test_snoscz5_nothing_converts_while_the_part_is_asleep`        |   ✅   | Snoscz5 nothing converts while the part is asleep                                            |
|  17 | `test_begin_sends_later_transfers_to_the_address_it_was_given`  |   ✅   | Begin sends later transfers to the address it was given                                      |
|  18 | `test_a_refused_transfer_fails_begin`                           |   ✅   | A refused transfer fails begin                                                               |
|  19 | `test_read_ch0_refuses_a_null_destination`                      |   ✅   | Read ch0 refuses a null destination                                                          |

</details>

---

## test_fins - native_fins - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                  | Status | Description                                                                           |
| --: | :------------------------------------ | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_published_header_constants`     |   ✅   | ICF bit 6 selects command(0) / response(1); bit 7 requests gateway use.               |
|   2 | `test_memory_area_read_octets`        |   ✅   | Memory area read octets                                                               |
|   3 | `test_memory_area_write_octets`       |   ✅   | Memory area write octets                                                              |
|   4 | `test_operating_mode_commands`        |   ✅   | Operating mode commands                                                               |
|   5 | `test_header_round_trip`              |   ✅   | Header round trip                                                                     |
|   6 | `test_response_end_code`              |   ✅   | Response end code                                                                     |
|   7 | `test_parsers_refuse_short_frames`    |   ✅   | Parsers refuse short frames                                                           |
|   8 | `test_builders_refuse_a_short_buffer` |   ✅   | The prefix fits but the write data does not: still 0, not a headerless partial write. |

</details>

---

## test_float_bits - native_float_bits - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------------- |
|   1 | `test_ieee754_binary64_field_layout`                |   ✅   | Ieee754 binary64 field layout                                              |
|   2 | `test_ieee754_published_encodings`                  |   ✅   | The same words, decoded: from_bits is the inverse of the field reads.      |
|   3 | `test_the_two_zeros_differ_only_in_the_sign`        |   ✅   | The two zeros differ only in the sign                                      |
|   4 | `test_infinity_and_nan_share_the_all_ones_exponent` |   ✅   | A NaN payload survives the split even though no comparison could check it. |
|   5 | `test_the_subnormal_boundary`                       |   ✅   | The subnormal boundary                                                     |
|   6 | `test_the_largest_finite_value`                     |   ✅   | The largest finite value                                                   |
|   7 | `test_merge_masks_each_field`                       |   ✅   | Merge masks each field                                                     |
|   8 | `test_every_bit_position_survives_the_split`        |   ✅   | Every bit position survives the split                                      |
|   9 | `test_the_exponent_field_walks_its_whole_range`     |   ✅   | The exponent field walks its whole range                                   |
|  10 | `test_a_walking_significand_bit_survives`           |   ✅   | A walking significand bit survives                                         |
|  11 | `test_repeating_significand_patterns_survive`       |   ✅   | Repeating significand patterns survive                                     |

</details>

---

## test_float_bits - native_mmgr_float_bits - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------------- |
|   1 | `test_ieee754_binary64_field_layout`                |   ✅   | Ieee754 binary64 field layout                                              |
|   2 | `test_ieee754_published_encodings`                  |   ✅   | The same words, decoded: from_bits is the inverse of the field reads.      |
|   3 | `test_the_two_zeros_differ_only_in_the_sign`        |   ✅   | The two zeros differ only in the sign                                      |
|   4 | `test_infinity_and_nan_share_the_all_ones_exponent` |   ✅   | A NaN payload survives the split even though no comparison could check it. |
|   5 | `test_the_subnormal_boundary`                       |   ✅   | The subnormal boundary                                                     |
|   6 | `test_the_largest_finite_value`                     |   ✅   | The largest finite value                                                   |
|   7 | `test_merge_masks_each_field`                       |   ✅   | Merge masks each field                                                     |
|   8 | `test_every_bit_position_survives_the_split`        |   ✅   | Every bit position survives the split                                      |
|   9 | `test_the_exponent_field_walks_its_whole_range`     |   ✅   | The exponent field walks its whole range                                   |
|  10 | `test_a_walking_significand_bit_survives`           |   ✅   | A walking significand bit survives                                         |
|  11 | `test_repeating_significand_patterns_survive`       |   ✅   | Repeating significand patterns survive                                     |

</details>

---

## test_flow_export - native_flow_export - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                              |
| --: | :---------------------------------------------------- | :----: | :----------------------------------------------------------------------- |
|   1 | `test_v9_packet_header`                               |   ✅   | V9 packet header                                                         |
|   2 | `test_rfc3954_template_flowset_example`               |   ✅   | Rfc3954 template flowset example                                         |
|   3 | `test_rfc3954_data_flowset_example`                   |   ✅   | RFC 3954 sec 5.1 Count = the Template Record plus the three Data Records |
|   4 | `test_ipfix_message_header_and_template_set_id`       |   ✅   | 16-octet Message Header, then the 28-octet Template Set                  |
|   5 | `test_data_set_id_must_be_256_or_above`               |   ✅   | Data set id must be 256 or above                                         |
|   6 | `test_v9_data_set_is_padded_to_a_four_octet_boundary` |   ✅   | an already-aligned Set gets no padding                                   |
|   7 | `test_ipfix_data_set_is_not_padded`                   |   ✅   | Ipfix data set is not padded                                             |
|   8 | `test_an_open_set_is_closed_by_what_follows`          |   ✅   | no data_set_end: the template that follows must close it                 |
|   9 | `test_calls_out_of_order_are_refused`                 |   ✅   | closing a Set that is not open is refused too                            |
|  10 | `test_overflow_fails_closed`                          |   ✅   | a header that does not fit either                                        |
|  11 | `test_v5_header_is_twenty_four_octets`                |   ✅   | V5 header is twenty four octets                                          |
|  12 | `test_v5_record_is_forty_eight_octets`                |   ✅   | V5 record is forty eight octets                                          |
|  13 | `test_v5_refuses_a_short_span`                        |   ✅   | V5 refuses a short span                                                  |

</details>

---

## test_focas - native_focas - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                                                                 |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_open_request_is_byte_exact`                         |   ✅   | Open request is byte exact                                                                  |
|   2 | `test_close_request_is_byte_exact`                        |   ✅   | Close request is byte exact                                                                 |
|   3 | `test_sysinfo_request_is_byte_exact`                      |   ✅   | Sysinfo request is byte exact                                                               |
|   4 | `test_arguments_are_big_endian_i32`                       |   ✅   | A negative argument is the two's-complement pattern, not a truncation or an absolute value. |
|   5 | `test_position_kind_and_axis_are_the_first_two_arguments` |   ✅   | Position kind and axis are the first two arguments                                          |
|   6 | `test_extra_data_extends_the_declared_length`             |   ✅   | length field = 26 + 3 = 29 = 0x001D                                                         |
|   7 | `test_builders_refuse_a_short_buffer`                     |   ✅   | Builders refuse a short buffer                                                              |
|   8 | `test_built_frames_parse_back`                            |   ✅   | Built frames parse back                                                                     |
|   9 | `test_malformed_frames_are_refused`                       |   ✅   | declare a 3-octet payload but hand over only the 12 octets already written                  |
|  10 | `test_command_response_decodes_selector_status_and_data`  |   ✅   | Command response decodes selector status and data                                           |
|  11 | `test_status_is_a_signed_return_code`                     |   ✅   | Status is a signed return code                                                              |
|  12 | `test_response_truncation_and_wrong_type_are_refused`     |   ✅   | an open response (0x0102) is a valid frame but not a command response                       |
|  13 | `test_sysinfo_splits_the_fixed_width_ascii_fields`        |   ✅   | Sysinfo splits the fixed width ascii fields                                                 |
|  14 | `test_value8_scales_by_base_and_exponent`                 |   ✅   | data is signed: 0xFFFFFFFF is -1, so -1 / 10^1 = -0.1                                       |
|  15 | `test_value8_sentinel_and_unknown_base_are_invalid`       |   ✅   | fewer than eight octets is not a value at all                                               |

</details>

---

## test_forward - native_forward - ✅ 33 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                          |
| --: | :-------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_default_deny`                           |   ✅   | Default deny                                                                         |
|   2 | `test_allow_forwards`                         |   ✅   | Allow forwards                                                                       |
|   3 | `test_no_self_forward`                        |   ✅   | No self forward                                                                      |
|   4 | `test_deny_wins_over_allow`                   |   ✅   | Deny wins over allow                                                                 |
|   5 | `test_multi_destination_fanout`               |   ✅   | Multi destination fanout                                                             |
|   6 | `test_rate_cap_drops_then_reopens`            |   ✅   | Rate cap drops then reopens                                                          |
|   7 | `test_send_failure_counted`                   |   ✅   | Send failure counted                                                                 |
|   8 | `test_add_if_validation_and_table_full`       |   ✅   | Add if validation and table full                                                     |
|   9 | `test_add_rule_table_full`                    |   ✅   | Add rule table full                                                                  |
|  10 | `test_unregistered_destination_is_inert`      |   ✅   | Unregistered destination is inert                                                    |
|  11 | `test_rule_with_mismatched_src_is_ignored`    |   ✅   | Rule with mismatched src is ignored                                                  |
|  12 | `test_duplicate_allow_rule_first_one_governs` |   ✅   | Duplicate allow rule first one governs                                               |
|  13 | `test_get_stats_null_pointer_is_noop`         |   ✅   | Reading the counters reports them and does not disturb them: the second read agrees. |
|  14 | `test_acl_deny_by_byte_pattern`               |   ✅   | Acl deny by byte pattern                                                             |
|  15 | `test_acl_allowlist_default_deny`             |   ✅   | Acl allowlist default deny                                                           |
|  16 | `test_acl_first_match_wins`                   |   ✅   | Acl first match wins                                                                 |
|  17 | `test_acl_src_any_content_wildcard`           |   ✅   | Acl src any content wildcard                                                         |
|  18 | `test_acl_entry_src_mismatch_falls_through`   |   ✅   | Acl entry src mismatch falls through                                                 |
|  19 | `test_acl_short_frame_skips_entry`            |   ✅   | Acl short frame skips entry                                                          |
|  20 | `test_acl_add_validation_and_table_full`      |   ✅   | Acl add validation and table full                                                    |
|  21 | `test_acl_add_null_pointer_validation`        |   ✅   | Acl add null pointer validation                                                      |
|  22 | `test_route_selects_egress_and_falls_through` |   ✅   | Route selects egress and falls through                                               |
|  23 | `test_route_never_reflects_to_source`         |   ✅   | Route never reflects to source                                                       |
|  24 | `test_route_unregistered_egress_fail_closed`  |   ✅   | Route unregistered egress fail closed                                                |
|  25 | `test_route_src_specific_filters_by_source`   |   ✅   | Route src specific filters by source                                                 |
|  26 | `test_route_send_failure_counted`             |   ✅   | Route send failure counted                                                           |
|  27 | `test_route_rate_cap`                         |   ✅   | Route rate cap                                                                       |
|  28 | `test_route_default_any_content`              |   ✅   | Route default any content                                                            |
|  29 | `test_route_first_match_wins`                 |   ✅   | Route first match wins                                                               |
|  30 | `test_route_add_validation_and_table_full`    |   ✅   | Route add validation and table full                                                  |
|  31 | `test_inspect_pass_and_drop`                  |   ✅   | Inspect pass and drop                                                                |
|  32 | `test_inspect_runs_after_acl`                 |   ✅   | Inspect runs after acl                                                               |
|  33 | `test_inspect_cleared_by_null`                |   ✅   | Inspect cleared by null                                                              |

</details>

---

## test_forwarded_trust - native_forwarded_trust - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                          |
| --: | :--------------------------------------------------------- | :----: | :------------------------------------------------------------------- |
|   1 | `test_an_empty_table_trusts_nothing`                       |   ✅   | An empty table trusts nothing                                        |
|   2 | `test_a_cidr_covers_its_block_and_nothing_else`            |   ✅   | A cidr covers its block and nothing else                             |
|   3 | `test_a_bare_address_is_a_host_route`                      |   ✅   | A bare address is a host route                                       |
|   4 | `test_a_v6_cidr_covers_only_v6`                            |   ✅   | A v6 cidr covers only v6                                             |
|   5 | `test_a_zero_prefix_covers_the_family`                     |   ✅   | A zero prefix covers the family                                      |
|   6 | `test_malformed_cidr_text_is_refused`                      |   ✅   | Malformed cidr text is refused                                       |
|   7 | `test_the_prefix_width_bound_per_family`                   |   ✅   | an address with no family is not a network                           |
|   8 | `test_the_table_is_bounded`                                |   ✅   | the rules already installed are untouched                            |
|   9 | `test_reset_empties_the_table`                             |   ✅   | Reset empties the table                                              |
|  10 | `test_an_untrusted_peer_can_never_forge_a_client_address`  |   ✅   | An untrusted peer can never forge a client address                   |
|  11 | `test_a_trusted_upstream_is_believed`                      |   ✅   | A trusted upstream is believed                                       |
|  12 | `test_a_trusted_upstream_with_no_usable_client_falls_back` |   ✅   | A trusted upstream with no usable client falls back                  |
|  13 | `test_the_destination_is_always_written`                   |   ✅   | no peer at all leaves an address that names nothing, not a stale one |
|  14 | `test_several_upstreams_are_all_trusted`                   |   ✅   | Several upstreams are all trusted                                    |
|  15 | `test_a_null_peer_is_not_trusted`                          |   ✅   | A null peer is not trusted                                           |

</details>

---

## test_frame - native_frame - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

|   # | Test                                                  | Status | Description                                                                      |
| --: | :---------------------------------------------------- | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_rfc959_multiline_reply_example`                 |   ✅   | Rfc959 multiline reply example                                                   |
|   2 | `test_partial_multiline_reply_needs_more`             |   ✅   | Partial multiline reply needs more                                               |
|   3 | `test_single_line_reply_and_pipelining`               |   ✅   | Single line reply and pipelining                                                 |
|   4 | `test_malformed_reply_heads_are_refused`              |   ✅   | Malformed reply heads are refused                                                |
|   5 | `test_a_different_code_does_not_terminate`            |   ✅   | A different code does not terminate                                              |
|   6 | `test_rfc2428_published_eprt_examples`                |   ✅   | Rfc2428 published eprt examples                                                  |
|   7 | `test_port_command_splits_into_eight_bit_fields`      |   ✅   | the extremes of both fields, from the same definition                            |
|   8 | `test_pasv_tuple_decodes_to_the_same_pair`            |   ✅   | Pasv tuple decodes to the same pair                                              |
|   9 | `test_pasv_refuses_out_of_range_and_malformed_tuples` |   ✅   | Pasv refuses out of range and malformed tuples                                   |
|  10 | `test_rfc2428_published_epsv_example`                 |   ✅   | Rfc2428 published epsv example                                                   |
|  11 | `test_epsv_accepts_any_legal_delimiter`               |   ✅   | Epsv accepts any legal delimiter                                                 |
|  12 | `test_epsv_refuses_malformed_replies`                 |   ✅   | Epsv refuses malformed replies                                                   |
|  13 | `test_command_line_form`                              |   ✅   | an argument may itself contain spaces; sec 5.3 makes the whole tail the argument |
|  14 | `test_reply_class_follows_the_first_digit`            |   ✅   | a code outside the three-digit range has no class                                |
|  15 | `test_builders_refuse_a_short_buffer`                 |   ✅   | Builders refuse a short buffer                                                   |
|  16 | `test_builders_refuse_bad_arguments`                  |   ✅   | Builders refuse bad arguments                                                    |
|  17 | `test_parsers_refuse_null_arguments`                  |   ✅   | Parsers refuse null arguments                                                    |

</details>

---

## test_binary_asset_blobs - native_binary_asset_blobs - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                            |
| --: | :------------------------------------------------- | :----: | :------------------------------------------------------------------------------------- |
|   1 | `test_every_embedded_name_finds_its_own_css`       |   ✅   | Every embedded name finds its own css                                                  |
|   2 | `test_registry_is_sorted_by_name`                  |   ✅   | Registry is sorted by name                                                             |
|   3 | `test_known_theme_is_minified_css`                 |   ✅   | Longer than the longest embedded name, so the length scan stops at its cap and nothing |
|   4 | `test_near_misses_do_not_match`                    |   ✅   | Near misses do not match                                                               |
|   5 | `test_name_longer_than_every_theme_does_not_match` |   ✅   | Longer than the longest embedded name, so the length scan stops at its cap and nothing |
|   6 | `test_trademarked_theme_follows_its_gate`          |   ✅   | Trademarked theme follows its gate                                                     |

</details>

---

## test_ftp_session - native_ftp_session - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_the_span_carves_the_session`                       |   ✅   | The span carves the session                       |
|   2 | `test_both_handles_are_seated_closed`                    |   ✅   | Both handles are seated closed                    |
|   3 | `test_an_incomplete_request_opens_nothing`               |   ✅   | An incomplete request opens nothing               |
|   4 | `test_the_session_waits_for_the_greeting_before_sending` |   ✅   | The session waits for the greeting before sending |
|   5 | `test_the_login_sequence_is_user_pass_type_epsv`         |   ✅   | The login sequence is user pass type epsv         |
|   6 | `test_a_refused_login_ends_the_transfer`                 |   ✅   | A refused login ends the transfer                 |
|   7 | `test_epsv_opens_the_data_connection_and_sends_stor`     |   ✅   | Epsv opens the data connection and sends stor     |
|   8 | `test_a_refused_epsv_falls_back_to_pasv`                 |   ✅   | A refused epsv falls back to pasv                 |

</details>

---

## test_gateway - native_gateway - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_gnss_survey - native_gnss_survey - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                                 |
| --: | :---------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_wgs84_published_axes`                     |   ✅   | Wgs84 published axes                                                                        |
|   2 | `test_height_adds_along_the_normal_at_the_axes` |   ✅   | Height adds along the normal at the axes                                                    |
|   3 | `test_longitude_only_rotates_about_the_axis`    |   ✅   | The northern and southern hemispheres are mirror images across the equator.                 |
|   4 | `test_geodetic_ecef_round_trip`                 |   ✅   | And the other way: ECEF -> geodetic -> ECEF returns the same point.                         |
|   5 | `test_metres_to_tenth_millimetres`              |   ✅   | Half rounds away from zero in both directions.                                              |
|   6 | `test_survey_starts_empty`                      |   ✅   | A single fix has no spread to report.                                                       |
|   7 | `test_survey_mean_and_spread`                   |   ✅   | Repeating the identical fix leaves the mean where it is and drives the spread to zero.      |
|   8 | `test_survey_averages_a_symmetric_scatter`      |   ✅   | Per axis the population variance of -50..50 is sum(k^2)/101 = 85850/101 = 850, and two axes |
|   9 | `test_survey_completion_gate`                   |   ✅   | One wild fix widens the spread past a tight limit without changing the count requirement.   |
|  10 | `test_survey_accepts_geodetic_fixes`            |   ✅   | Survey accepts geodetic fixes                                                               |
|  11 | `test_gga_folds_into_a_geodetic_fix`            |   ✅   | Gga folds into a geodetic fix                                                               |
|  12 | `test_gga_without_a_fix_is_refused`             |   ✅   | Gga without a fix is refused                                                                |

</details>

---

## test_gnss_survey - native_gnss_survey_in - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                                 |
| --: | :---------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_wgs84_published_axes`                     |   ✅   | Wgs84 published axes                                                                        |
|   2 | `test_height_adds_along_the_normal_at_the_axes` |   ✅   | Height adds along the normal at the axes                                                    |
|   3 | `test_longitude_only_rotates_about_the_axis`    |   ✅   | The northern and southern hemispheres are mirror images across the equator.                 |
|   4 | `test_geodetic_ecef_round_trip`                 |   ✅   | And the other way: ECEF -> geodetic -> ECEF returns the same point.                         |
|   5 | `test_metres_to_tenth_millimetres`              |   ✅   | Half rounds away from zero in both directions.                                              |
|   6 | `test_survey_starts_empty`                      |   ✅   | A single fix has no spread to report.                                                       |
|   7 | `test_survey_mean_and_spread`                   |   ✅   | Repeating the identical fix leaves the mean where it is and drives the spread to zero.      |
|   8 | `test_survey_averages_a_symmetric_scatter`      |   ✅   | Per axis the population variance of -50..50 is sum(k^2)/101 = 85850/101 = 850, and two axes |
|   9 | `test_survey_completion_gate`                   |   ✅   | One wild fix widens the spread past a tight limit without changing the count requirement.   |
|  10 | `test_survey_accepts_geodetic_fixes`            |   ✅   | Survey accepts geodetic fixes                                                               |
|  11 | `test_gga_folds_into_a_geodetic_fix`            |   ✅   | Gga folds into a geodetic fix                                                               |
|  12 | `test_gga_without_a_fix_is_refused`             |   ✅   | Gga without a fix is refused                                                                |

</details>

---

## test_goose - native_goose - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                                    |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_pdu_is_ber_encoded_in_tag_order`                      |   ✅   | Pdu is ber encoded in tag order                                                                |
|   2 | `test_boolean_true_is_all_ones`                             |   ✅   | Boolean true is all ones                                                                       |
|   3 | `test_integer_contents_are_minimal_and_positive`            |   ✅   | stNum 0 is one 0x00 octet, not an empty one.                                                   |
|   4 | `test_long_form_length_boundary`                            |   ✅   | The allData TLV is 1 tag + 2 length octets + 200 = 203; the other eleven fields are 41 octets, |
|   5 | `test_ethernet_and_goose_header_layout`                     |   ✅   | Ethernet and goose header layout                                                               |
|   6 | `test_publish_subscribe_round_trip`                         |   ✅   | The decoded strings and blobs point into the caller's frame, not into a copy.                  |
|   7 | `test_unknown_pdu_tags_are_skipped`                         |   ✅   | 22 octets of Ethernet + GOOSE header, then 61 0A { 80 01 41, 8F 02 AA BB, 85 01 07 }.          |
|   8 | `test_parse_rejects_a_frame_that_is_not_a_valid_goose_apdu` |   ✅   | Parse rejects a frame that is not a valid goose apdu                                           |
|   9 | `test_build_refuses_an_undersized_buffer`                   |   ✅   | Build refuses an undersized buffer                                                             |
|  10 | `test_absent_optional_fields_encode_as_empty`               |   ✅   | Content: 2+2+2 (three empty strings) + 4 (ttl 1000) + 10 (t) + 3+3+3+3 (stNum, sqNum, confRev, |

</details>

---

## test_gpib - native_gpib - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                        |
| --: | :-------------------------------------------- | :----: | :--------------------------------------------------------------------------------- |
|   1 | `test_prologix_published_escape_example`      |   ✅   | Prologix published escape example                                                  |
|   2 | `test_only_the_four_named_octets_are_escaped` |   ✅   | Only the four named octets are escaped                                             |
|   3 | `test_empty_payload_is_a_bare_terminator`     |   ✅   | Empty payload is a bare terminator                                                 |
|   4 | `test_addr_command_matches_the_manual`        |   ✅   | the ends of the primary range are addressable, one past it is not                  |
|   5 | `test_read_command_matches_the_manual`        |   ✅   | the <char> field is decimal, so 255 renders as three digits and not as an octet    |
|   6 | `test_spoll_command_matches_the_manual`       |   ✅   | a secondary without a primary is not a form the syntax allows, so it is dropped    |
|   7 | `test_eos_command_matches_the_manual`         |   ✅   | Eos command matches the manual                                                     |
|   8 | `test_generic_command_form`                   |   ✅   | Generic command form                                                               |
|   9 | `test_command_versus_data_classification`     |   ✅   | a '+' the codec escaped is preceded by ESC, so the line no longer starts with "++" |
|  10 | `test_decimal_response_parsing`               |   ✅   | Decimal response parsing                                                           |
|  11 | `test_addr_response_parsing`                  |   ✅   | Addr response parsing                                                              |
|  12 | `test_version_response_parsing`               |   ✅   | Version response parsing                                                           |
|  13 | `test_published_ports`                        |   ✅   | Published ports                                                                    |
|  14 | `test_builders_refuse_a_short_buffer`         |   ✅   | Builders refuse a short buffer                                                     |
|  15 | `test_data_builder_reserves_the_terminator`   |   ✅   | an escaped octet needs two, so the same payload length needs more room             |

</details>

---

## test_gpio_map - native_gpio_map - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                               | Status | Description                                                 |
| --: | :----------------------------------------------------------------- | :----: | :---------------------------------------------------------- |
|   1 | `test_the_grammar_checker_rejects_malformed_texts`                 |   ✅   | The grammar checker rejects malformed texts                 |
|   2 | `test_the_report_is_a_json_text`                                   |   ✅   | The report is a json text                                   |
|   3 | `test_every_pin_is_one_element_in_table_order`                     |   ✅   | Every pin is one element in table order                     |
|   4 | `test_an_empty_table_is_an_empty_array`                            |   ✅   | An empty table is an empty array                            |
|   5 | `test_a_shorter_table_is_the_same_document_with_fewer_elements`    |   ✅   | A shorter table is the same document with fewer elements    |
|   6 | `test_a_label_cannot_escape_its_string`                            |   ✅   | A label cannot escape its string                            |
|   7 | `test_the_reported_level_is_a_flag`                                |   ✅   | The reported level is a flag                                |
|   8 | `test_the_four_directions_are_told_apart`                          |   ✅   | The four directions are told apart                          |
|   9 | `test_an_undeclared_direction_is_not_named_as_an_output`           |   ✅   | An undeclared direction is not named as an output           |
|  10 | `test_the_smallest_buffer_that_holds_the_report`                   |   ✅   | The smallest buffer that holds the report                   |
|  11 | `test_a_report_that_does_not_fit_is_reported_as_such`              |   ✅   | A report that does not fit is reported as such              |
|  12 | `test_the_serializer_refuses_missing_arguments`                    |   ✅   | The serializer refuses missing arguments                    |
|  13 | `test_only_a_declared_output_may_be_driven`                        |   ✅   | Only a declared output may be driven                        |
|  14 | `test_a_set_request_parses_both_fields`                            |   ✅   | A set request parses both fields                            |
|  15 | `test_a_field_name_must_start_at_a_pair_boundary`                  |   ✅   | A field name must start at a pair boundary                  |
|  16 | `test_a_pin_the_field_cannot_hold_is_not_delivered_as_another_pin` |   ✅   | A pin the field cannot hold is not delivered as another pin |
|  17 | `test_the_parsed_level_is_a_flag`                                  |   ✅   | The parsed level is a flag                                  |
|  18 | `test_an_incomplete_set_request_is_refused`                        |   ✅   | An incomplete set request is refused                        |
|  19 | `test_the_body_length_bounds_the_parse`                            |   ✅   | The body length bounds the parse                            |
|  20 | `test_each_pin_is_armed_in_its_declared_direction`                 |   ✅   | Each pin is armed in its declared direction                 |
|  21 | `test_a_sample_reads_the_seam_back_into_the_table`                 |   ✅   | A sample reads the seam back into the table                 |
|  22 | `test_a_missing_table_is_walked_zero_times`                        |   ✅   | A missing table is walked zero times                        |

</details>

---

## test_graphql - native_graphql_exec - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                                    |
| --: | :------------------------------------------------------ | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_spec_example_3_produces_example_4`                |   ✅   | sec 6.4.2 hands the resolver the field, reached here as the dotted path from the root.         |
|   2 | `test_query_shorthand_matches_the_long_form`            |   ✅   | Query shorthand matches the long form                                                          |
|   3 | `test_selection_set_keeps_document_order`               |   ✅   | The same three fields written in another order come back in that other order.                  |
|   4 | `test_nested_selection_sets_shape_the_response`         |   ✅   | Nested selection sets shape the response                                                       |
|   5 | `test_arguments_reach_the_leaf_that_needs_them`         |   ✅   | Arguments reach the leaf that needs them                                                       |
|   6 | `test_arguments_are_unordered`                          |   ✅   | Arguments are unordered                                                                        |
|   7 | `test_argument_accessors_are_named_and_typed`           |   ✅   | Argument accessors are named and typed                                                         |
|   8 | `test_no_resolver_completes_every_leaf_as_null`         |   ✅   | No resolver completes every leaf as null                                                       |
|   9 | `test_scalar_serialization_forms`                       |   ✅   | -9007199254740993 is 2^53 + 1 negated: it needs the full 64-bit Int this module carries, and   |
|  10 | `test_string_values_are_json_escaped`                   |   ✅   | String values are json escaped                                                                 |
|  11 | `test_request_error_carries_errors_and_no_data`         |   ✅   | The grammar this module declares out of scope: each construct is legal GraphQL but parses as a |
|  12 | `test_out_of_scope_grammar_is_a_request_error`          |   ✅   | Out of scope grammar is a request error                                                        |
|  13 | `test_comments_and_commas_are_ignored`                  |   ✅   | Comments and commas are ignored                                                                |
|  14 | `test_unresolved_leaf_completes_as_null`                |   ✅   | Unresolved leaf completes as null                                                              |
|  15 | `test_bounds_are_request_errors`                        |   ✅   | PROTOCORE_GQL_MAX_DEPTH nesting levels parse; one more does not. The root set is level 1.      |
|  16 | `test_short_buffer_reports_overflow`                    |   ✅   | Short buffer reports overflow                                                                  |
|  17 | `test_null_inputs_are_refused`                          |   ✅   | Null inputs are refused                                                                        |
|  18 | `test_argument_accessor_without_values_reports_absence` |   ✅   | Argument accessor without values reports absence                                               |

</details>

---

## test_grpcweb - native_grpcweb_frame - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                             |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_message_length_is_four_octets_big_endian`      |   ✅   | The same octets read back give the length again.                                        |
|   2 | `test_compressed_flag_is_bit_zero_of_the_frame_byte` |   ✅   | Compressed flag is bit zero of the frame byte                                           |
|   3 | `test_msb_of_the_frame_byte_is_the_trailers_bit`     |   ✅   | A build stamps the same MSB.                                                            |
|   4 | `test_trailer_section_is_lower_case_field_lines`     |   ✅   | A null or empty Status-Message omits the whole grpc-message line.                       |
|   5 | `test_status_round_trips_the_published_code_values`  |   ✅   | The gRPC status codes, by name and number, as the project publishes them.               |
|   6 | `test_message_slice_stays_percent_encoded`           |   ✅   | The slice points into the caller's octets rather than a copy: "grpc-status:2\r\n" is 15 |
|   7 | `test_a_key_inside_a_value_is_not_a_field_name`      |   ✅   | A section with no Status at all, and one whose value is not 1*DIGIT.                    |
|   8 | `test_empty_message_is_a_bare_prefix`                |   ✅   | Empty message is a bare prefix                                                          |
|   9 | `test_parse_waits_for_the_whole_message`             |   ✅   | Parse waits for the whole message                                                       |
|  10 | `test_a_stream_walks_frame_by_frame`                 |   ✅   | A stream walks frame by frame                                                           |
|  11 | `test_frame_writes_the_given_frame_byte`             |   ✅   | Frame writes the given frame byte                                                       |
|  12 | `test_builders_refuse_a_short_buffer`                |   ✅   | A trailers frame that cannot even hold its prefix.                                      |
|  13 | `test_a_negative_status_is_refused`                  |   ✅   | A negative status is refused                                                            |

</details>

---

## test_guardrails - native_guardrails - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                   | Status | Description                                          |
| --: | :----------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_the_floor_is_strictly_below`                     |   ✅   | The floor is strictly below                          |
|   2 | `test_each_floor_reads_one_field`                      |   ✅   | Each floor reads one field                           |
|   3 | `test_the_low_water_mark_is_not_a_guardrail`           |   ✅   | The low water mark is not a guardrail                |
|   4 | `test_the_breach_bits_are_disjoint_powers_of_two`      |   ✅   | The breach bits are disjoint powers of two           |
|   5 | `test_a_null_snapshot_reports_no_breach`               |   ✅   | A null snapshot reports no breach                    |
|   6 | `test_json_is_an_rfc8259_object`                       |   ✅   | Json is an rfc8259 object                            |
|   7 | `test_json_writes_zero_as_one_digit`                   |   ✅   | Json writes zero as one digit                        |
|   8 | `test_json_writes_the_full_32_bit_range`               |   ✅   | Json writes the full 32 bit range                    |
|   9 | `test_json_boundary_is_the_object_plus_its_terminator` |   ✅   | Json boundary is the object plus its terminator      |
|  10 | `test_json_refuses_null_and_zero_capacity`             |   ✅   | Json refuses null and zero capacity                  |
|  11 | `test_the_sampler_reports_the_stated_counters`         |   ✅   | A snapshot there is nowhere to write is not a crash. |
|  12 | `test_check_judges_what_the_sampler_read`              |   ✅   | Check judges what the sampler read                   |
|  13 | `test_begin_installs_and_replaces_the_callback`        |   ✅   | Begin installs and replaces the callback             |

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

## test_h2_conn - native_h2conn - ✅ 41 passed

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
|  17 | `test_h2_data`                                          |   ✅   | H2 data                                          |
|  18 | `test_h2_window_update`                                 |   ✅   | H2 window update                                 |
|  19 | `test_h2_rst_priority_push`                             |   ✅   | H2 rst priority push                             |
|  20 | `test_h2_goaway_then_ignore`                            |   ✅   | H2 goaway then ignore                            |
|  21 | `test_h2_settings_ack_and_bad`                          |   ✅   | H2 settings ack and bad                          |
|  22 | `test_h2_ping_bad`                                      |   ✅   | H2 ping bad                                      |
|  23 | `test_h2_frame_too_big`                                 |   ✅   | H2 frame too big                                 |
|  24 | `test_h2_respond_paths_and_goaway`                      |   ✅   | H2 respond paths and goaway                      |
|  25 | `test_h2_more_guards`                                   |   ✅   | H2 more guards                                   |
|  26 | `test_h2_continuation_more`                             |   ✅   | H2 continuation more                             |
|  27 | `test_h2_respond_content_type_too_big`                  |   ✅   | H2 respond content type too big                  |
|  28 | `test_h2_null_callbacks`                                |   ✅   | H2 null callbacks                                |
|  29 | `test_h2_headers_stream_zero`                           |   ✅   | H2 headers stream zero                           |
|  30 | `test_h2_continuation_without_headers`                  |   ✅   | H2 continuation without headers                  |
|  31 | `test_h2_idle_stream_frames_are_connection_errors`      |   ✅   | H2 idle stream frames are connection errors      |
|  32 | `test_h2_window_update_on_a_closed_stream_is_ignored`   |   ✅   | H2 window update on a closed stream is ignored   |
|  33 | `test_h2_frame_size_and_stream_id_rules`                |   ✅   | H2 frame size and stream id rules                |
|  34 | `test_h2_content_length_must_match_the_data`            |   ✅   | H2 content length must match the data            |
|  35 | `test_h2_continuation_flood_is_bounded`                 |   ✅   | H2 continuation flood is bounded                 |
|  36 | `test_h2_data_empty_and_unknown_stream`                 |   ✅   | H2 data empty and unknown stream                 |
|  37 | `test_h2_window_update_zero_and_overflow`               |   ✅   | H2 window update zero and overflow               |
|  38 | `test_h2_data_after_end_stream_resets_the_stream`       |   ✅   | H2 data after end stream resets the stream       |
|  39 | `test_h2_continuation_after_stream_freed`               |   ✅   | H2 continuation after stream freed               |
|  40 | `test_h2_respond_default_chunk_size`                    |   ✅   | H2 respond default chunk size                    |
|  41 | `test_h2_respond_content_length_no_room`                |   ✅   | H2 respond content length no room                |

</details>

---

## test_h2_frame - native_h2_frame_rfc - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                       | Status | Description                                                          |
| --: | :----------------------------------------- | :----: | :------------------------------------------------------------------- |
|   1 | `test_rfc9113_preface_octets`              |   ✅   | Rfc9113 preface octets                                               |
|   2 | `test_rfc9113_frame_header_layout`         |   ✅   | Rfc9113 frame header layout                                          |
|   3 | `test_rfc9113_reserved_bit`                |   ✅   | Rfc9113 reserved bit                                                 |
|   4 | `test_rfc9113_length_is_24_bits`           |   ✅   | Rfc9113 length is 24 bits                                            |
|   5 | `test_rfc9113_settings_initial_values`     |   ✅   | Rfc9113 settings initial values                                      |
|   6 | `test_rfc9113_settings_payload_shape`      |   ✅   | id 0x1 = 8192, then an unknown id 0xabcd                             |
|   7 | `test_rfc9113_settings_bounds`             |   ✅   | Rfc9113 settings bounds                                              |
|   8 | `test_rfc9113_settings_round_trip`         |   ✅   | Rfc9113 settings round trip                                          |
|   9 | `test_rfc9113_settings_ack_bytes`          |   ✅   | Rfc9113 settings ack bytes                                           |
|  10 | `test_rfc9113_window_update_bytes`         |   ✅   | Rfc9113 window update bytes                                          |
|  11 | `test_rfc9113_rst_stream_bytes`            |   ✅   | Rfc9113 rst stream bytes                                             |
|  12 | `test_rfc9113_goaway_bytes`                |   ✅   | Rfc9113 goaway bytes                                                 |
|  13 | `test_rfc9113_ping_ack_echoes_the_payload` |   ✅   | Rfc9113 ping ack echoes the payload                                  |
|  14 | `test_rfc9113_headers_and_data`            |   ✅   | an empty DATA frame is a header and nothing else                     |
|  15 | `test_builders_refuse_a_short_destination` |   ✅   | Builders refuse a short destination                                  |
|  16 | `test_rfc9113_registry_values`             |   ✅   | sec 6.5.2 / sec 6.5: the SETTINGS identifiers are 16-bit wire values |

</details>

---

## test_h3_frame - native_h3_frame_rfc - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                       |
| --: | :------------------------------------------------------ | :----: | :-------------------------------------------------------------------------------- |
|   1 | `test_rfc9000_sample_varints_as_frame_lengths`          |   ✅   | Rfc9000 sample varints as frame lengths                                           |
|   2 | `test_rfc9000_long_spelling_decodes_but_is_not_emitted` |   ✅   | Rfc9000 long spelling decodes but is not emitted                                  |
|   3 | `test_rfc9114_frame_type_registry`                      |   ✅   | Rfc9114 frame type registry                                                       |
|   4 | `test_rfc9114_reserved_http2_frame_types`               |   ✅   | sec 7.2.8 grease: 0x1f * N + 0x21, here N = 0 and N = 1                           |
|   5 | `test_rfc9114_settings_defaults`                        |   ✅   | Rfc9114 settings defaults                                                         |
|   6 | `test_rfc9114_settings_round_trip`                      |   ✅   | varint widths: 1+2 (id 0x01, 4096) + 1+4 (id 0x06, 16384 exceeds the 14-bit form) |
|   7 | `test_rfc9114_reserved_settings_identifiers`            |   ✅   | an empty SETTINGS payload is legal and changes nothing                            |
|   8 | `test_rfc9114_settings_truncated_pair`                  |   ✅   | Rfc9114 settings truncated pair                                                   |
|   9 | `test_rfc9114_data_and_headers_builders`                |   ✅   | a zero-length DATA frame is the two header varints and nothing else               |
|  10 | `test_rfc9114_goaway_builder`                           |   ✅   | Rfc9114 goaway builder                                                            |
|  11 | `test_truncated_header_is_refused`                      |   ✅   | Truncated header is refused                                                       |
|  12 | `test_builders_refuse_a_short_destination`              |   ✅   | Builders refuse a short destination                                               |

</details>

---

## test_haas_mdc - native_haas_mdc - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_query_numbers_match_the_published_table`         |   ✅   | Query numbers match the published table         |
|   2 | `test_query_line_spells_the_published_command`         |   ✅   | Query line spells the published command         |
|   3 | `test_macro_read_line_is_q600_space_variable`          |   ✅   | Macro read line is q600 space variable          |
|   4 | `test_query_letters_are_uppercase`                     |   ✅   | Query letters are uppercase                     |
|   5 | `test_numbered_query_is_terminated_with_a_new_line`    |   ✅   | Numbered query is terminated with a new line    |
|   6 | `test_macro_query_is_terminated_with_a_new_line`       |   ✅   | Macro query is terminated with a new line       |
|   7 | `test_setting143_frame_bytes`                          |   ✅   | Setting143 frame bytes                          |
|   8 | `test_published_example_responses_split_on_the_commas` |   ✅   | Published example responses split on the commas |
|   9 | `test_q500_published_example`                          |   ✅   | Q500 published example                          |
|  10 | `test_busy_is_status_busy`                             |   ✅   | Busy is status busy                             |
|  11 | `test_unknown_is_the_unrecognized_request_reply`       |   ✅   | Unknown is the unrecognized request reply       |
|  12 | `test_ngc_ethernet_response_is_parsed`                 |   ✅   | Ngc ethernet response is parsed                 |
|  13 | `test_bytes_outside_the_frame_are_ignored`             |   ✅   | Bytes outside the frame are ignored             |
|  14 | `test_fields_are_trimmed_of_surrounding_spaces`        |   ✅   | Fields are trimmed of surrounding spaces        |
|  15 | `test_incomplete_frame_is_refused`                     |   ✅   | Incomplete frame is refused                     |
|  16 | `test_q500_refuses_a_non_numeric_parts_field`          |   ✅   | Q500 refuses a non numeric parts field          |
|  17 | `test_macro_row_with_a_variable_number_decodes`        |   ✅   | Macro row with a variable number decodes        |
|  18 | `test_dprnt_line_is_unframed_text`                     |   ✅   | Dprnt line is unframed text                     |
|  19 | `test_dprnt_keeps_interior_spaces`                     |   ✅   | Dprnt keeps interior spaces                     |
|  20 | `test_field_table_is_bounded`                          |   ✅   | Field table is bounded                          |
|  21 | `test_builders_refuse_a_short_buffer`                  |   ✅   | Builders refuse a short buffer                  |

</details>

---

## test_happy_eyeballs - native_happy_eyeballs - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                                                                |
| --: | :-------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_rfc8305_4_interleaves_families`                     |   ✅   | Rfc8305 4 interleaves families                                                             |
|   2 | `test_rfc8305_4_leading_family_follows_the_first_address` |   ✅   | Only IPv4 candidates: nothing to interleave with, order preserved.                         |
|   3 | `test_interleave_drains_the_shorter_family`               |   ✅   | Interleave drains the shorter family                                                       |
|   4 | `test_preference_follows_the_scope_ladder`                |   ✅   | The same ladder over IPv4 (RFC 1918 private, RFC 3927 link-local, RFC 1122 loopback).      |
|   5 | `test_v4_mapped_counts_as_ipv4`                           |   ✅   | A list of one native v6 and two mapped v4s alternates as if the mapped ones were plain v4. |
|   6 | `test_equal_preference_keeps_input_order`                 |   ✅   | Equal preference keeps input order                                                         |
|   7 | `test_scope_beats_family`                                 |   ✅   | Scope beats family                                                                         |
|   8 | `test_rfc8305_5_attempt_delay_gate`                       |   ✅   | sec 8: "Connection Attempt Delay ... Recommended to be 250 milliseconds."                  |
|   9 | `test_attempt_gate_survives_the_millis_wrap`              |   ✅   | before + 300 wraps past zero to 0x2c; the difference is still 300.                         |
|  10 | `test_short_and_null_lists_are_left_alone`                |   ✅   | Short and null lists are left alone                                                        |
|  11 | `test_oversized_lists_are_sorted_without_interleaving`    |   ✅   | Alternate a link-local v6 and a global v4 so the sort has something to do.                 |

</details>

---

## test_hart - native_hart - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                                                 |
| --: | :--------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_published_delimiter_bits`                |   ✅   | The frame type occupies the low three bits, so the long-address bit never collides with it. |
|   2 | `test_command_zero_frame`                      |   ✅   | Command zero frame                                                                          |
|   3 | `test_checksum_folds_the_frame_to_zero`        |   ✅   | delimiter + 5 address + command + byte count + 6 data + check = 15.                         |
|   4 | `test_long_address_is_driven_by_the_delimiter` |   ✅   | Only one and five are legal address widths.                                                 |
|   5 | `test_frame_round_trip`                        |   ✅   | Frame round trip                                                                            |
|   6 | `test_single_bit_corruption_is_refused`        |   ✅   | The byte-count octet re-lengths the frame rather than breaking parity, so it is covered     |
|   7 | `test_parse_refuses_a_truncated_frame`         |   ✅   | Parse refuses a truncated frame                                                             |
|   8 | `test_hartip_header_octets`                    |   ✅   | Hartip header octets                                                                        |
|   9 | `test_hartip_payload_slice`                    |   ✅   | The sliced payload is itself a valid HART frame.                                            |
|  10 | `test_hartip_refuses_impossible_byte_counts`   |   ✅   | Hartip refuses impossible byte counts                                                       |

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

## test_hislip - native_hislip - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                             |
| --: | :-------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_ivi61_table2_header_layout`                   |   ✅   | Ivi61 table2 header layout                                                              |
|   2 | `test_header_round_trip`                            |   ✅   | Header round trip                                                                       |
|   3 | `test_prologue_is_checked`                          |   ✅   | Prologue is checked                                                                     |
|   4 | `test_ivi61_table4_message_type_values`             |   ✅   | Ivi61 table4 message type values                                                        |
|   5 | `test_ivi61_port_assignment`                        |   ✅   | Ivi61 port assignment                                                                   |
|   6 | `test_initialize_packs_version_and_vendor`          |   ✅   | "may be of zero length": no payload at all is a legal Initialize                        |
|   7 | `test_version_words`                                |   ✅   | Version words                                                                           |
|   8 | `test_initialize_response_control_bits_and_session` |   ✅   | bit 0 clear is "Prefer Synchronized"; bit 1 set is "encryption mandatory"               |
|   9 | `test_typed_parsers_reject_the_wrong_type`          |   ✅   | Typed parsers reject the wrong type                                                     |
|  10 | `test_initialize_refuses_a_short_payload`           |   ✅   | Initialize refuses a short payload                                                      |
|  11 | `test_async_initialize_pair`                        |   ✅   | Async initialize pair                                                                   |
|  12 | `test_data_and_data_end`                            |   ✅   | an empty payload is legal and the length field says so                                  |
|  13 | `test_message_id_increments_by_two_and_wraps`       |   ✅   | sec 6.2 also gives the value used after initialization and device clear: 0xffffff00 - 2 |
|  14 | `test_builders_refuse_a_short_buffer`               |   ✅   | Builders refuse a short buffer                                                          |
|  15 | `test_unknown_message_type_is_carried_through`      |   ✅   | Unknown message type is carried through                                                 |

</details>

---

## test_hmmd - native_hmmd - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                                               |
| --: | :--------------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_declared_frame_geometry`                             |   ✅   | Declared frame geometry                                                                   |
|   2 | `test_report_fields_round_trip`                            |   ✅   | The distance is little-endian: swapping its two octets reads a different distance.        |
|   3 | `test_detection_flag_is_exactly_one`                       |   ✅   | no target means no distance, whatever the payload carried                                 |
|   4 | `test_malformed_report_frames_are_refused`                 |   ✅   | this module emits exactly one report length, so a shorter or longer buffer is not one     |
|   5 | `test_stream_resyncs_past_noise_and_reports_once`          |   ✅   | Stream resyncs past noise and reports once                                                |
|   6 | `test_stream_handles_a_partial_header_before_the_real_one` |   ✅   | Stream handles a partial header before the real one                                       |
|   7 | `test_stream_drops_an_absurd_length_and_recovers`          |   ✅   | Stream drops an absurd length and recovers                                                |
|   8 | `test_stream_drops_a_bad_frame_and_keeps_going`            |   ✅   | Stream drops a bad frame and keeps going                                                  |
|   9 | `test_stream_null_arguments_are_refused`                   |   ✅   | Stream null arguments are refused                                                         |
|  10 | `test_ld2410_v102_published_command_envelope`              |   ✅   | 2.2.1: word 0x00FF, value 0x0001, so the frame data length is 4                           |
|  11 | `test_named_command_words`                                 |   ✅   | The register selector is passed through verbatim and counts toward the frame data length. |
|  12 | `test_command_length_field_tracks_the_value`               |   ✅   | Command length field tracks the value                                                     |
|  13 | `test_command_builder_fails_closed`                        |   ✅   | Command builder fails closed                                                              |
|  14 | `test_ack_decodes_the_command_word_and_payload`            |   ✅   | The open command as its own echo: word 0x00FF, two payload octets.                        |
|  15 | `test_malformed_ack_frames_are_refused`                    |   ✅   | The report and command envelopes never accept each other's frames.                        |

</details>

---

## test_hmmd - native_hmmd_nobus - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                                               |
| --: | :--------------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_declared_frame_geometry`                             |   ✅   | Declared frame geometry                                                                   |
|   2 | `test_report_fields_round_trip`                            |   ✅   | The distance is little-endian: swapping its two octets reads a different distance.        |
|   3 | `test_detection_flag_is_exactly_one`                       |   ✅   | no target means no distance, whatever the payload carried                                 |
|   4 | `test_malformed_report_frames_are_refused`                 |   ✅   | this module emits exactly one report length, so a shorter or longer buffer is not one     |
|   5 | `test_stream_resyncs_past_noise_and_reports_once`          |   ✅   | Stream resyncs past noise and reports once                                                |
|   6 | `test_stream_handles_a_partial_header_before_the_real_one` |   ✅   | Stream handles a partial header before the real one                                       |
|   7 | `test_stream_drops_an_absurd_length_and_recovers`          |   ✅   | Stream drops an absurd length and recovers                                                |
|   8 | `test_stream_drops_a_bad_frame_and_keeps_going`            |   ✅   | Stream drops a bad frame and keeps going                                                  |
|   9 | `test_stream_null_arguments_are_refused`                   |   ✅   | Stream null arguments are refused                                                         |
|  10 | `test_ld2410_v102_published_command_envelope`              |   ✅   | 2.2.1: word 0x00FF, value 0x0001, so the frame data length is 4                           |
|  11 | `test_named_command_words`                                 |   ✅   | The register selector is passed through verbatim and counts toward the frame data length. |
|  12 | `test_command_length_field_tracks_the_value`               |   ✅   | Command length field tracks the value                                                     |
|  13 | `test_command_builder_fails_closed`                        |   ✅   | Command builder fails closed                                                              |
|  14 | `test_ack_decodes_the_command_word_and_payload`            |   ✅   | The open command as its own echo: word 0x00FF, two payload octets.                        |
|  15 | `test_malformed_ack_frames_are_refused`                    |   ✅   | The report and command envelopes never accept each other's frames.                        |

</details>

---

## test_hostlink - native_hostlink - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                         |
| --: | :-------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_fcs_is_a_running_xor`                   |   ✅   | '@' ^ '0' ^ '0' = 0x40 ^ 0x30 ^ 0x30 = 0x40         |
|   2 | `test_read_command_frame`                     |   ✅   | Read command frame                                  |
|   3 | `test_write_command_frame`                    |   ✅   | Write command frame                                 |
|   4 | `test_read_response_words`                    |   ✅   | Read response words                                 |
|   5 | `test_build_parse_round_trip`                 |   ✅   | A node above 99 has no two-digit spelling.          |
|   6 | `test_fcs_rendering_and_acceptance`           |   ✅   | Fcs rendering and acceptance                        |
|   7 | `test_single_character_corruption_is_refused` |   ✅   | Single character corruption is refused              |
|   8 | `test_parse_rejects_bad_framing`              |   ✅   | Parse rejects bad framing                           |
|   9 | `test_end_code_guards`                        |   ✅   | "@00RD" alone: the response has no end code at all. |
|  10 | `test_builders_refuse_a_short_buffer`         |   ✅   | Builders refuse a short buffer                      |

</details>

---

## test_hotswap - native_hotswap - ✅ 31 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_starts_absent_not_ready`                                |   ✅   | Starts absent not ready                                |
|   2 | `test_first_probe_is_due_immediately`                         |   ✅   | First probe is due immediately                         |
|   3 | `test_first_probe_is_due_when_init_time_is_near_zero`         |   ✅   | First probe is due when init time is near zero         |
|   4 | `test_zero_threshold_is_clamped_to_one`                       |   ✅   | Zero threshold is clamped to one                       |
|   5 | `test_one_failure_does_not_fault_a_healthy_volume`            |   ✅   | One failure does not fault a healthy volume            |
|   6 | `test_threshold_run_faults_and_counts`                        |   ✅   | Threshold run faults and counts                        |
|   7 | `test_a_success_resets_the_failure_run`                       |   ✅   | A success resets the failure run                       |
|   8 | `test_further_failures_while_faulted_are_ignored`             |   ✅   | Further failures while faulted are ignored             |
|   9 | `test_io_while_absent_is_ignored`                             |   ✅   | Io while absent is ignored                             |
|  10 | `test_fail_run_saturates_instead_of_wrapping`                 |   ✅   | Fail run saturates instead of wrapping                 |
|  11 | `test_fail_run_at_the_uint8_ceiling_does_not_wrap`            |   ✅   | Fail run at the uint8 ceiling does not wrap            |
|  12 | `test_no_probe_while_ready`                                   |   ✅   | No probe while ready                                   |
|  13 | `test_probe_is_rate_limited_while_absent`                     |   ✅   | Probe is rate limited while absent                     |
|  14 | `test_probe_pacing_is_wrapsafe_across_rollover`               |   ✅   | Probe pacing is wrapsafe across rollover               |
|  15 | `test_present_but_unmountable_stays_absent`                   |   ✅   | Present but unmountable stays absent                   |
|  16 | `test_mount_counts_only_on_transition`                        |   ✅   | Mount counts only on transition                        |
|  17 | `test_full_removal_and_reinsertion_cycle`                     |   ✅   | Full removal and reinsertion cycle                     |
|  18 | `test_faulted_volume_can_go_straight_back_to_ready`           |   ✅   | Faulted volume can go straight back to ready           |
|  19 | `test_null_core_is_not_a_crash`                               |   ✅   | Null core is not a crash                               |
|  20 | `test_state_names`                                            |   ✅   | State names                                            |
|  21 | `test_json_and_overflow_is_fail_closed`                       |   ✅   | Json and overflow is fail closed                       |
|  22 | `test_binding_poll_before_begin_does_nothing`                 |   ✅   | Binding poll before begin does nothing                 |
|  23 | `test_binding_mounts_on_the_first_poll_and_notifies`          |   ✅   | Binding mounts on the first poll and notifies          |
|  24 | `test_binding_ready_volume_is_never_reprobed`                 |   ✅   | Binding ready volume is never reprobed                 |
|  25 | `test_binding_io_fault_unmounts_immediately_and_notifies`     |   ✅   | Binding io fault unmounts immediately and notifies     |
|  26 | `test_binding_drops_a_faulted_mount_before_retrying`          |   ✅   | Binding drops a faulted mount before retrying          |
|  27 | `test_binding_faults_and_retries_without_an_unmount_callback` |   ✅   | Binding faults and retries without an unmount callback |
|  28 | `test_binding_without_card_detect_lets_the_mount_decide`      |   ✅   | Binding without card detect lets the mount decide      |
|  29 | `test_binding_without_a_mount_callback_never_becomes_ready`   |   ✅   | Binding without a mount callback never becomes ready   |
|  30 | `test_binding_event_callback_is_optional`                     |   ✅   | Binding event callback is optional                     |
|  31 | `test_binding_poll_reads_the_library_clock`                   |   ✅   | Binding poll reads the library clock                   |

</details>

---

## test_hpack - native_hpack - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                              | Status | Description                                                                                   |
| --: | :------------------------------------------------ | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_rfc7541_c1_integer_examples`                |   ✅   | Rfc7541 c1 integer examples                                                                   |
|   2 | `test_prefix_flags_are_left_alone`                |   ✅   | A dynamic table size update is 001 then a 5-bit prefix integer (sec 6.3).                     |
|   3 | `test_prefix_int_round_trips_at_every_width`      |   ✅   | Prefix int round trips at every width                                                         |
|   4 | `test_prefix_int_rejects_an_overflowing_encoding` |   ✅   | Four continuations put the fifth octet's payload at bit 28, so its value may not exceed 0x0f. |
|   5 | `test_encode_int_refuses_a_short_buffer`          |   ✅   | Encode int refuses a short buffer                                                             |
|   6 | `test_appendix_c_huffman_strings`                 |   ✅   | C.4.1: :authority www.example.com, prefix 0x8c then twelve octets.                            |
|   7 | `test_appendix_b_huffman_table`                   |   ✅   | Appendix b huffman table                                                                      |
|   8 | `test_huffman_decode_rejects_bad_padding_and_eos` |   ✅   | EOS is the 30-bit all-ones code, so four octets of 0xff resolve to it.                        |
|   9 | `test_huff_encode_refuses_a_short_buffer`         |   ✅   | Huff encode refuses a short buffer                                                            |
|  10 | `test_decode_str_reads_both_forms`                |   ✅   | C.3.1's raw form: 0x0f then "www.example.com".                                                |
|  11 | `test_decode_str_fails_closed`                    |   ✅   | A length prefix whose continuation never terminates.                                          |
|  12 | `test_encode_str_picks_the_shorter_form`          |   ✅   | "www.example.com" is fifteen octets raw and twelve Huffman-coded, so the H bit is set and the |
|  13 | `test_encode_str_round_trips_every_octet`         |   ✅   | Encode str round trips every octet                                                            |

</details>

---

## test_hpack - native_codec_hpack_prim - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                              | Status | Description                                                                                   |
| --: | :------------------------------------------------ | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_rfc7541_c1_integer_examples`                |   ✅   | Rfc7541 c1 integer examples                                                                   |
|   2 | `test_prefix_flags_are_left_alone`                |   ✅   | A dynamic table size update is 001 then a 5-bit prefix integer (sec 6.3).                     |
|   3 | `test_prefix_int_round_trips_at_every_width`      |   ✅   | Prefix int round trips at every width                                                         |
|   4 | `test_prefix_int_rejects_an_overflowing_encoding` |   ✅   | Four continuations put the fifth octet's payload at bit 28, so its value may not exceed 0x0f. |
|   5 | `test_encode_int_refuses_a_short_buffer`          |   ✅   | Encode int refuses a short buffer                                                             |
|   6 | `test_appendix_c_huffman_strings`                 |   ✅   | C.4.1: :authority www.example.com, prefix 0x8c then twelve octets.                            |
|   7 | `test_appendix_b_huffman_table`                   |   ✅   | Appendix b huffman table                                                                      |
|   8 | `test_huffman_decode_rejects_bad_padding_and_eos` |   ✅   | EOS is the 30-bit all-ones code, so four octets of 0xff resolve to it.                        |
|   9 | `test_huff_encode_refuses_a_short_buffer`         |   ✅   | Huff encode refuses a short buffer                                                            |
|  10 | `test_decode_str_reads_both_forms`                |   ✅   | C.3.1's raw form: 0x0f then "www.example.com".                                                |
|  11 | `test_decode_str_fails_closed`                    |   ✅   | A length prefix whose continuation never terminates.                                          |
|  12 | `test_encode_str_picks_the_shorter_form`          |   ✅   | "www.example.com" is fifteen octets raw and twelve Huffman-coded, so the H bit is set and the |
|  13 | `test_encode_str_round_trips_every_octet`         |   ✅   | Encode str round trips every octet                                                            |

</details>

---

## test_http_client - native_http_client - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                          |
| --: | :------------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_target_uri_split`                            |   ✅   | an explicit port overrides the scheme default                                        |
|   2 | `test_target_uri_refusals`                         |   ✅   | a host or path longer than the caller's buffer is refused, not truncated             |
|   3 | `test_get_request_message`                         |   ✅   | a request with no content declares no Content-Length                                 |
|   4 | `test_host_field_carries_only_a_non_default_port`  |   ✅   | 443 is not the default for http, so it is carried                                    |
|   5 | `test_post_request_message`                        |   ✅   | Post request message                                                                 |
|   6 | `test_post_defaults_the_content_type`              |   ✅   | Post defaults the content type                                                       |
|   7 | `test_build_refuses_a_short_buffer`                |   ✅   | the field section fits but the content does not: one octet short of header plus body |
|   8 | `test_status_line`                                 |   ✅   | RFC 9110 sec 15: the range is 100..599, at both ends                                 |
|   9 | `test_malformed_responses_are_refused`             |   ✅   | Malformed responses are refused                                                      |
|  10 | `test_body_framing_follows_the_rfc9112_precedence` |   ✅   | item 6: a valid Content-Length without Transfer-Encoding                             |
|  11 | `test_chunked_must_be_the_final_coding`            |   ✅   | and a coding list that ends in chunked is the final coding                           |
|  12 | `test_a_short_body_is_clamped_to_what_arrived`     |   ✅   | a Content-Length of zero frames an empty body even with octets in the buffer         |
|  13 | `test_field_names_are_case_insensitive`            |   ✅   | Field names are case insensitive                                                     |
|  14 | `test_body_offset_is_past_the_field_section`       |   ✅   | Body offset is past the field section                                                |

</details>

---

## test_http_date - native_http_date - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_http_delivery - native_http_delivery - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                   | Status | Description                                     |
| --: | :----------------------------------------------------- | :----: | :---------------------------------------------- |
|   1 | `test_rfc5861_worked_example`                          |   ✅   | Rfc5861 worked example                          |
|   2 | `test_rfc5861_published_totals`                        |   ✅   | Rfc5861 published totals                        |
|   3 | `test_the_second_past_max_age_is_never_fresh`          |   ✅   | The second past max age is never fresh          |
|   4 | `test_a_zero_stale_window_has_no_stale_band`           |   ✅   | A zero stale window has no stale band           |
|   5 | `test_verdict_is_monotonic_in_age`                     |   ✅   | Verdict is monotonic in age                     |
|   6 | `test_the_window_sum_does_not_wrap`                    |   ✅   | The window sum does not wrap                    |
|   7 | `test_rfc5861_example_cache_control_value`             |   ✅   | Rfc5861 example cache control value             |
|   8 | `test_cache_control_directive_forms`                   |   ✅   | Cache control directive forms                   |
|   9 | `test_cache_control_needs_room_for_the_terminator`     |   ✅   | Cache control needs room for the terminator     |
|  10 | `test_manifest_documented_shape`                       |   ✅   | Manifest documented shape                       |
|  11 | `test_manifest_leaves_no_raw_control_character`        |   ✅   | Manifest leaves no raw control character        |
|  12 | `test_manifest_escapes_a_character_with_no_short_form` |   ✅   | Manifest escapes a character with no short form |
|  13 | `test_manifest_escapes_quote_solidus_and_control`      |   ✅   | Manifest escapes quote solidus and control      |
|  14 | `test_full_precache_list_fits_the_configured_buffer`   |   ✅   | Full precache list fits the configured buffer   |
|  15 | `test_manifest_refuses_rather_than_truncating`         |   ✅   | Manifest refuses rather than truncating         |
|  16 | `test_manifest_refuses_bad_arguments`                  |   ✅   | Manifest refuses bad arguments                  |

</details>

---

## test_httpcache - native_httpcache - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                  |
| --: | :-------------------------------------------------- | :----: | :--------------------------------------------------------------------------- |
|   1 | `test_rfc9111_4_2_1_first_match`                    |   ✅   | Bullet 1 beats bullet 2, but only when the cache is shared.                  |
|   2 | `test_init_is_an_empty_directive_set`               |   ✅   | Nothing set means nothing to send: 0 rather than an empty header value.      |
|   3 | `test_build_emits_the_grammar`                      |   ✅   | The RFC 5861 extensions.                                                     |
|   4 | `test_build_reports_its_own_length`                 |   ✅   | Build reports its own length                                                 |
|   5 | `test_parse_is_tolerant_as_sec_5_2_requires`        |   ✅   | sec 5.2.3: a directive a cache does not understand is ignored, not an error. |
|   6 | `test_parse_separates_bare_max_stale_from_valued`   |   ✅   | Parse separates bare max stale from valued                                   |
|   7 | `test_delta_seconds_saturates_rather_than_wrapping` |   ✅   | 2^31-1 itself survives unchanged, so the clamp is not eating a legal value.  |
|   8 | `test_build_parse_round_trip`                       |   ✅   | Build parse round trip                                                       |
|   9 | `test_presets_match_their_documented_directives`    |   ✅   | RFC 8246: "immutable" tells a cache not to revalidate while fresh.           |
|  10 | `test_build_refuses_a_short_buffer`                 |   ✅   | One octet short of value + NUL is still a refusal; exactly enough is not.    |

</details>

---

## test_hw_health - native_hw_health - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                                                        |
| --: | :-------------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------- |
|   1 | `test_rail_thresholds_are_strictly_below`                       |   ✅   | Rail thresholds are strictly below                                                 |
|   2 | `test_rail_min_is_the_worst_droop_and_events_tally`             |   ✅   | A reading above nominal never raises the worst droop.                              |
|   3 | `test_rail_json_is_an_rfc8259_object`                           |   ✅   | A monitor that has seen nothing reports zero as "0", not "" and not "00".          |
|   4 | `test_rail_json_fails_closed_on_a_short_buffer`                 |   ✅   | Rail json fails closed on a short buffer                                           |
|   5 | `test_spi_halves_only_on_a_full_fail_streak`                    |   ✅   | Two failures then a success: the run is broken, so a third failure does not halve. |
|   6 | `test_spi_doubles_only_on_a_full_ok_streak`                     |   ✅   | Spi doubles only on a full ok streak                                               |
|   7 | `test_spi_clock_stays_between_floor_and_ceiling`                |   ✅   | Spi clock stays between floor and ceiling                                          |
|   8 | `test_spi_init_clamps_the_start_clock_and_defaults_a_zero_trip` |   ✅   | fail_trip 2, so one failure reports the clock without changing it.                 |
|   9 | `test_spi_doubling_wrap_clamps_to_the_ceiling`                  |   ✅   | Spi doubling wrap clamps to the ceiling                                            |
|  10 | `test_gpio_readback_mismatch_names_the_rail`                    |   ✅   | Gpio readback mismatch names the rail                                              |
|  11 | `test_cap_tolerance_window_is_inclusive`                        |   ✅   | Cap tolerance window is inclusive                                                  |
|  12 | `test_cap_band_truncates_and_a_zero_expectation_never_judges`   |   ✅   | Cap band truncates and a zero expectation never judges                             |
|  13 | `test_cap_band_wider_than_expected_clamps_the_low_edge`         |   ✅   | Cap band wider than expected clamps the low edge                                   |
|  14 | `test_a_missing_monitor_is_refused`                             |   ✅   | A missing monitor is refused                                                       |

</details>

---

## test_iccp - native_iccp - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                    |
| --: | :----------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_state_q_ber_layout`                        |   ✅   | State q ber layout                                                                             |
|   2 | `test_state_q_carries_an_optional_timestamp`     |   ✅   | State q carries an optional timestamp                                                          |
|   3 | `test_state_and_quality_occupy_separate_fields`  |   ✅   | Values wider than the fields are masked, never allowed to spill into the neighbouring bits.    |
|   4 | `test_real_q_ber_layout`                         |   ✅   | With a timestamp the 17 TLV follows the quality, inside the same A3.                           |
|   5 | `test_real_q_integer_is_minimal_twos_complement` |   ✅   | A3 + len + (02 + len + content) + (85 01 00)                                                   |
|   6 | `test_real_q_quality_is_masked`                  |   ✅   | milli 1 is one content octet, so the value is 02 01 01 + 85 01 <q> and the quality lands last. |
|   7 | `test_build_refuses_an_undersized_buffer`        |   ✅   | Build refuses an undersized buffer                                                             |

</details>

---

## test_iec60870 - native_iec60870 - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                                                                    |
| --: | :-------------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_iec104_i_format_field_layout`                             |   ✅   | Iec104 i format field layout                                                                   |
|   2 | `test_iec104_sequence_numbers_span_fifteen_bits`                |   ✅   | The specific octet pair spelled out above.                                                     |
|   3 | `test_iec104_s_format`                                          |   ✅   | Iec104 s format                                                                                |
|   4 | `test_iec104_u_format_commands`                                 |   ✅   | Iec104 u format commands                                                                       |
|   5 | `test_iec104_parse_rejects_malformed_apdus`                     |   ✅   | Iec104 parse rejects malformed apdus                                                           |
|   6 | `test_iec104_build_refuses_oversized_or_unbuffered_apdus`       |   ✅   | Iec104 build refuses oversized or unbuffered apdus                                             |
|   7 | `test_asdu_header_field_layout`                                 |   ✅   | The test bit is the other flag sharing that octet, and the count field is 7 bits wide.         |
|   8 | `test_information_object_address_is_three_octets_little_endian` |   ✅   | The field is three octets wide, so anything above it is dropped rather than overrunning.       |
|   9 | `test_single_point_object`                                      |   ✅   | Single point object                                                                            |
|  10 | `test_double_point_object`                                      |   ✅   | Double point object                                                                            |
|  11 | `test_short_float_measured_value`                               |   ✅   | -2.0 is sign 1, biased exponent 128 (0x80), zero significand: 1 10000000 0000... = 0xC0000000. |
|  12 | `test_scaled_measured_value`                                    |   ✅   | Scaled measured value                                                                          |
|  13 | `test_normalized_measured_value`                                |   ✅   | Values beyond the interval clamp to the field rather than wrapping through it.                 |
|  14 | `test_integrated_totals_counter`                                |   ✅   | Integrated totals counter                                                                      |
|  15 | `test_single_command_object`                                    |   ✅   | Single command object                                                                          |
|  16 | `test_double_command_object`                                    |   ✅   | The qualifier is five bits, so its widest value fills bits 3-7 without reaching S/E.           |
|  17 | `test_ft12_fixed_length_frame`                                  |   ✅   | The sum wraps at 256, which is the only way the check octet can be reached from large fields.  |
|  18 | `test_ft12_variable_length_frame`                               |   ✅   | An empty ASDU is still a legal variable frame: L is 2 and no ASDU slice is reported.           |
|  19 | `test_ft12_parse_rejects_corrupted_frames`                      |   ✅   | A fixed-length frame with a wrong check octet or stop octet is refused the same way.           |
|  20 | `test_ft12_build_refuses_oversized_or_unbuffered_frames`        |   ✅   | Ft12 build refuses oversized or unbuffered frames                                              |

</details>

---

## test_iface_bridge - native_iface_bridge - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                 | Status | Description                   |
| --: | :----------------------------------- | :----: | :---------------------------- |
|   1 | `test_map_and_find`                  |   ✅   | Map and find                  |
|   2 | `test_any_interface_and_dedup`       |   ✅   | Any interface and dedup       |
|   3 | `test_bad_address_rejected`          |   ✅   | Bad address rejected          |
|   4 | `test_table_full`                    |   ✅   | Table full                    |
|   5 | `test_txn_roundtrip`                 |   ✅   | Txn roundtrip                 |
|   6 | `test_txn_partial_and_readonly`      |   ✅   | Txn partial and readonly      |
|   7 | `test_build_overflow_fails_closed`   |   ✅   | Build overflow fails closed   |
|   8 | `test_null_arg_guards`               |   ✅   | Null arg guards               |
|   9 | `test_map_empty_ip_is_any_interface` |   ✅   | Map empty ip is any interface |
|  10 | `test_txn_parse_null_outputs`        |   ✅   | Txn parse null outputs        |
|  11 | `test_txn_build_edge_cases`          |   ✅   | Txn build edge cases          |

</details>

---

## test_ina219 - native_ina219 - ✅ 25 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                   | Status | Description                                                                             |
| --: | :--------------------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_sbos448_bus_voltage_register`                                    |   ✅   | one LSB: bit 3 set -> 4 mV                                                              |
|   2 | `test_sbos448_bus_status_bits_do_not_reach_the_voltage`                |   ✅   | Sbos448 bus status bits do not reach the voltage                                        |
|   3 | `test_sbos448_shunt_voltage_register`                                  |   ✅   | 320 mV, the full-scale of the /8 PGA range: 320000 uV / 10 = 32000 counts               |
|   4 | `test_sbos448_calibration_equation`                                    |   ✅   | Sbos448 calibration equation                                                            |
|   5 | `test_calibration_truncates_and_clamps_to_sixteen_bits`                |   ✅   | anything at or beyond 65535 saturates rather than wrapping into a small calibration     |
|   6 | `test_calibration_zero_denominator`                                    |   ✅   | Calibration zero denominator                                                            |
|   7 | `test_calibration_falls_as_the_denominator_grows`                      |   ✅   | The smaller shunt is captured before the larger one runs: both report through the one   |
|   8 | `test_sbos448_current_scaling`                                         |   ✅   | 1 A at 100 uA/bit is 10000 counts                                                       |
|   9 | `test_sbos448_power_lsb_is_twenty_times_the_current_lsb`               |   ✅   | 1 W at a 100 uA current LSB (2 mW power LSB) is 500 counts                              |
|  10 | `test_current_and_power_are_odd_about_zero`                            |   ✅   | Each positive reading is captured before its negative runs: both report through the one |
|  11 | `test_sbos448_register_addresses`                                      |   ✅   | Sbos448 register addresses                                                              |
|  12 | `test_bus_voltage_is_monotone`                                         |   ✅   | Bus voltage is monotone                                                                 |
|  13 | `test_sbos448g_model_reset_values_and_a_persistent_address_pointer`    |   ✅   | no pointer this time: the part still answers from the register it was left on           |
|  14 | `test_sbos448g_begin_programs_the_calibration_then_the_config`         |   ✅   | the calibration write: pointer 05h, then 4096 big-endian                                |
|  15 | `test_sbos448g_the_bus_reading_is_the_applied_voltage`                 |   ✅   | Sbos448g the bus reading is the applied voltage                                         |
|  16 | `test_sbos448g_the_shunt_reading_is_the_applied_drop`                  |   ✅   | Sbos448g the shunt reading is the applied drop                                          |
|  17 | `test_sbos448g_a_drop_past_the_pga_range_clips`                        |   ✅   | Sbos448g a drop past the pga range clips                                                |
|  18 | `test_sbos448g_one_amp_through_the_shunt_reads_one_amp`                |   ✅   | Sbos448g one amp through the shunt reads one amp                                        |
|  19 | `test_sbos448g_power_is_the_bus_voltage_times_the_current`             |   ✅   | Sbos448g power is the bus voltage times the current                                     |
|  20 | `test_sbos448g_current_reads_zero_until_the_calibration_is_programmed` |   ✅   | Sbos448g current reads zero until the calibration is programmed                         |
|  21 | `test_the_reading_is_independent_of_the_programmed_current_lsb`        |   ✅   | The reading is independent of the programmed current lsb                                |
|  22 | `test_begin_sends_later_transfers_to_the_address_it_was_given`         |   ✅   | Begin sends later transfers to the address it was given                                 |
|  23 | `test_a_part_at_another_address_is_not_read`                           |   ✅   | A part at another address is not read                                                   |
|  24 | `test_a_refused_transfer_fails_the_reading`                            |   ✅   | A refused transfer fails the reading                                                    |
|  25 | `test_begin_reports_a_refused_write`                                   |   ✅   | Begin reports a refused write                                                           |

</details>

---

## test_inflate - native_codec_inflate - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                            |
| --: | :---------------------------------------------------- | :----: | :------------------------------------------------------------------------------------- |
|   1 | `test_rfc1951_hand_built_blocks`                      |   ✅   | "AB": 'B' is 66, so its code is 00110000 + 66 = 01110010 in bits 11..18.               |
|   2 | `test_permessage_deflate_payload_with_the_marker`     |   ✅   | Permessage deflate payload with the marker                                             |
|   3 | `test_dynamic_huffman_block`                          |   ✅   | sec 3.2.3: BTYPE = 10 is the dynamic form.                                             |
|   4 | `test_reserved_block_type_is_refused`                 |   ✅   | Reserved block type is refused                                                         |
|   5 | `test_stored_block_nlen_must_be_the_complement`       |   ✅   | A stored block whose LEN claims more octets than the input holds.                      |
|   6 | `test_symbols_that_never_occur_are_refused`           |   ✅   | Symbol 286 is 280 + 6, so its 8-bit code is 11000000 + 6 = 11000110 in bits 3..10.     |
|   7 | `test_distance_before_the_start_of_output_is_refused` |   ✅   | Length code 257 then distance code 1 (distance 2), with nothing produced yet.          |
|   8 | `test_truncated_stream_is_refused`                    |   ✅   | A dynamic block header cut off inside its HLIT/HDIST/HCLEN counts.                     |
|   9 | `test_output_overflow_fails_closed`                   |   ✅   | A stored block whose octets do not fit either.                                         |
|  10 | `test_scratch_too_small_fails_closed`                 |   ✅   | Scratch too small fails closed                                                         |
|  11 | `test_two_blocks_concatenate`                         |   ✅   | Non-final stored "Hi" (BFINAL=0 so the first octet is 0x00), then the final fixed "A". |

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

## test_ip - native_ip - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                 | Status | Description                                                               |
| --: | :----------------------------------- | :----: | :------------------------------------------------------------------------ |
|   1 | `test_v4_round_trip`                 |   ✅   | V4 round trip                                                             |
|   2 | `test_rfc5952_canonical_output`      |   ✅   | sec 4.1: leading zeros in a field are suppressed                          |
|   3 | `test_v4_mapped`                     |   ✅   | a plain v4 address is not a mapped one                                    |
|   4 | `test_malformed_text_is_refused`     |   ✅   | Malformed text is refused                                                 |
|   5 | `test_constructors_match_the_parser` |   ✅   | Constructors match the parser                                             |
|   6 | `test_to_v4_be`                      |   ✅   | a v6 address that is not v4-mapped has no v4 form                         |
|   7 | `test_equal_separates_families`      |   ✅   | Equal separates families                                                  |
|   8 | `test_is_unspecified`                |   ✅   | Is unspecified                                                            |
|   9 | `test_classify_v4`                   |   ✅   | 172.15 and 172.32 sit either side of the RFC 1918 /12, so they are global |
|  10 | `test_classify_v6`                   |   ✅   | Classify v6                                                               |
|  11 | `test_prefix_match`                  |   ✅   | /0 contains everything                                                    |
|  12 | `test_format_refuses_a_short_buffer` |   ✅   | Format refuses a short buffer                                             |

</details>

---

## test_j1939 - native_j1939 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                                 |
| --: | :---------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_published_pgn_registry`                   |   ✅   | TP.CM control octets (J1939-21 section 5.10).                                               |
|   2 | `test_published_identifiers`                    |   ✅   | The requested PGN is three octets, little-endian: 0x00F004 -> 04 F0 00.                     |
|   3 | `test_pdu1_and_pdu2_boundary`                   |   ✅   | PF 0xEF: the last PDU1 format. The PGN's low octet is dropped and the destination takes it. |
|   4 | `test_name_bit_layout`                          |   ✅   | Bit 48 is reserved and no field reaches it.                                                 |
|   5 | `test_single_frame_padding`                     |   ✅   | Single frame padding                                                                        |
|   6 | `test_bam_announce`                             |   ✅   | BAM is for messages the single frame cannot carry: 9 octets up to the reassembly limit.     |
|   7 | `test_transport_protocol_reassembly`            |   ✅   | Transport protocol reassembly                                                               |
|   8 | `test_transport_protocol_rejects_bad_sequences` |   ✅   | A packet from a different source is ignored, not merged.                                    |
|   9 | `test_decode_eec1`                              |   ✅   | A 1-octet SPN is valid to 0xFA and a 2-octet one to 0xFAFF; above that is not available.    |
|  10 | `test_decode_et1`                               |   ✅   | Decode et1                                                                                  |
|  11 | `test_decode_ccvs`                              |   ✅   | Decode ccvs                                                                                 |
|  12 | `test_decode_vd`                                |   ✅   | 0xFAFFFFFF is still a reading: 4211081215 * 0.125 = 526385151.875 km.                       |
|  13 | `test_decode_lfe_amb_ic1`                       |   ✅   | Decode lfe amb ic1                                                                          |
|  14 | `test_decode_dm1`                               |   ✅   | 0x54 = 01 01 01 00: MIL 1, red stop 1, amber 1, protect 0.                                  |

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

## test_json - native_json_codec - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                   |
| --: | :-------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_rfc8259_section_13_example_document`    |   ✅   | Rfc8259 section 13 example document                                                           |
|   2 | `test_rfc8259_mandatory_escapes`              |   ✅   | Rfc8259 mandatory escapes                                                                     |
|   3 | `test_member_name_is_escaped`                 |   ✅   | Member name is escaped                                                                        |
|   4 | `test_rfc8259_literal_names`                  |   ✅   | Rfc8259 literal names                                                                         |
|   5 | `test_rfc8259_g_clef_surrogate`               |   ✅   | Rfc8259 g clef surrogate                                                                      |
|   6 | `test_rfc3629_escape_widths`                  |   ✅   | Rfc3629 escape widths                                                                         |
|   7 | `test_rfc8259_unpaired_surrogate`             |   ✅   | a high surrogate whose partner is not a low surrogate is unpaired too                         |
|   8 | `test_rfc8259_two_character_escapes`          |   ✅   | Rfc8259 two character escapes                                                                 |
|   9 | `test_reader_matches_only_top_level_members`  |   ✅   | Reader matches only top level members                                                         |
|  10 | `test_reader_skips_insignificant_whitespace`  |   ✅   | Reader skips insignificant whitespace                                                         |
|  11 | `test_write_read_round_trip`                  |   ✅   | Write read round trip                                                                         |
|  12 | `test_reader_refuses_a_mismatched_type`       |   ✅   | Reader refuses a mismatched type                                                              |
|  13 | `test_reader_guards`                          |   ✅   | Reader guards                                                                                 |
|  14 | `test_reader_truncates_to_capacity`           |   ✅   | a multi-byte escape is emitted whole or not at all: two bytes do not fit in the one left here |
|  15 | `test_writer_overflow_latches_and_terminates` |   ✅   | Writer overflow latches and terminates                                                        |
|  16 | `test_writer_depth_limit`                     |   ✅   | Writer depth limit                                                                            |
|  17 | `test_writer_unbalanced_close`                |   ✅   | Writer unbalanced close                                                                       |
|  18 | `test_writer_without_storage`                 |   ✅   | Writer without storage                                                                        |
|  19 | `test_writer_raw_literal`                     |   ✅   | Writer raw literal                                                                            |

</details>

---

## test_ld2410 - native_ld2410 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                                           |
| --: | :--------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_v102_published_report_frames`                        |   ✅   | no engineering block in a 0x02 frame                                                  |
|   2 | `test_v102_frame_lengths`                                  |   ✅   | V102 frame lengths                                                                    |
|   3 | `test_v102_target_state_drives_presence_and_distance`      |   ✅   | Both targets present: the moving one is the one being tracked.                        |
|   4 | `test_malformed_report_frames_are_refused`                 |   ✅   | Malformed report frames are refused                                                   |
|   5 | `test_stream_resyncs_past_noise_and_reports_once`          |   ✅   | The stream is back in sync for the next frame, including a different frame kind.      |
|   6 | `test_stream_handles_a_partial_header_before_the_real_one` |   ✅   | Stream handles a partial header before the real one                                   |
|   7 | `test_stream_drops_an_absurd_length_and_recovers`          |   ✅   | Stream drops an absurd length and recovers                                            |
|   8 | `test_stream_drops_a_bad_frame_and_keeps_going`            |   ✅   | Stream drops a bad frame and keeps going                                              |
|   9 | `test_v102_published_command_frames`                       |   ✅   | 2.2.1 enable configuration: word 0x00FF, value 0x0001                                 |
|  10 | `test_ld2410b_command_frames_follow_the_same_envelope`     |   ✅   | Bluetooth on / off: word 0x00A4, value 0x0001 / 0x0000                                |
|  11 | `test_command_encoders_fail_closed`                        |   ✅   | Command encoders fail closed                                                          |
|  12 | `test_v102_published_ack_frames`                           |   ✅   | 2.2.1 enable-configuration ACK: status 0, protocol version 0x0001, buffer size 0x0040 |
|  13 | `test_get_mac_ack_yields_the_address`                      |   ✅   | A failed get-MAC, a different command word, and a short payload all yield nothing.    |
|  14 | `test_malformed_ack_frames_are_refused`                    |   ✅   | A report frame is not an ACK: the two envelopes never accept each other's frames.     |

</details>

---

## test_ld2410 - native_ld2410_nobus - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                                           |
| --: | :--------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_v102_published_report_frames`                        |   ✅   | no engineering block in a 0x02 frame                                                  |
|   2 | `test_v102_frame_lengths`                                  |   ✅   | V102 frame lengths                                                                    |
|   3 | `test_v102_target_state_drives_presence_and_distance`      |   ✅   | Both targets present: the moving one is the one being tracked.                        |
|   4 | `test_malformed_report_frames_are_refused`                 |   ✅   | Malformed report frames are refused                                                   |
|   5 | `test_stream_resyncs_past_noise_and_reports_once`          |   ✅   | The stream is back in sync for the next frame, including a different frame kind.      |
|   6 | `test_stream_handles_a_partial_header_before_the_real_one` |   ✅   | Stream handles a partial header before the real one                                   |
|   7 | `test_stream_drops_an_absurd_length_and_recovers`          |   ✅   | Stream drops an absurd length and recovers                                            |
|   8 | `test_stream_drops_a_bad_frame_and_keeps_going`            |   ✅   | Stream drops a bad frame and keeps going                                              |
|   9 | `test_v102_published_command_frames`                       |   ✅   | 2.2.1 enable configuration: word 0x00FF, value 0x0001                                 |
|  10 | `test_ld2410b_command_frames_follow_the_same_envelope`     |   ✅   | Bluetooth on / off: word 0x00A4, value 0x0001 / 0x0000                                |
|  11 | `test_command_encoders_fail_closed`                        |   ✅   | Command encoders fail closed                                                          |
|  12 | `test_v102_published_ack_frames`                           |   ✅   | 2.2.1 enable-configuration ACK: status 0, protocol version 0x0001, buffer size 0x0040 |
|  13 | `test_get_mac_ack_yields_the_address`                      |   ✅   | A failed get-MAC, a different command word, and a short payload all yield nothing.    |
|  14 | `test_malformed_ack_frames_are_refused`                    |   ✅   | A report frame is not an ACK: the two envelopes never accept each other's frames.     |

</details>

---

## test_ldc1614 - native_ldc1614 - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                                   |
| --: | :---------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_datasheet_register_map`                               |   ✅   | Datasheet register map                                                                        |
|   2 | `test_datasheet_register_ids_and_data_layout`               |   ✅   | every flag set over a 12-bit field of 0x123: the data is the field alone.                     |
|   3 | `test_range_sentinels_round_trip`                           |   ✅   | the flags above a full-scale field do not push the result past 28 bits.                       |
|   4 | `test_equation4_sensor_frequency`                           |   ✅   | under-range data is zero frequency, whatever the reference.                                   |
|   5 | `test_build_config_writes_the_datasheet_registers_in_order` |   ✅   | the two caller-supplied counts land big-endian, the way a 16-bit register write is framed.    |
|   6 | `test_build_config_honors_the_reserved_fields`              |   ✅   | equation 4 only holds at divider 1, so both dividers are 1: FIN_DIVIDER0 [15:12], FREF [9:0]. |
|   7 | `test_build_config_refuses_a_short_buffer`                  |   ✅   | Build config refuses a short buffer                                                           |
|   8 | `test_build_config_refuses_a_null_buffer`                   |   ✅   | Build config refuses a null buffer                                                            |

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

## test_log - native_log_frames - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                      |
| --: | :------------------------------------------------------ | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_a_discarded_call_emits_nothing`                   |   ✅   | A discarded call emits nothing                                                   |
|   2 | `test_a_discarded_call_does_not_evaluate_its_arguments` |   ✅   | the same probes on an emitted level ARE evaluated, so the probes themselves work |
|   3 | `test_each_level_emits_with_its_own_severity`           |   ✅   | Each level emits with its own severity                                           |
|   4 | `test_the_emitted_line_is_the_built_frame`              |   ✅   | The emitted line is the built frame                                              |
|   5 | `test_a_null_string_field_renders_empty`                |   ✅   | A null string field renders empty                                                |
|   6 | `test_an_emitted_line_reaches_the_ring`                 |   ✅   | An emitted line reaches the ring                                                 |
|   7 | `test_the_log_and_ring_severity_scales_agree`           |   ✅   | and the scale is ordered low to high, with NONE above every emitting level       |
|   8 | `test_clearing_the_sink`                                |   ✅   | Clearing the sink                                                                |
|   9 | `test_a_null_spec_emits_nothing`                        |   ✅   | A null spec emits nothing                                                        |
|  10 | `test_mismatched_values_yield_an_empty_line`            |   ✅   | and the wrong arity is refused the same way                                      |
|  11 | `test_a_line_that_does_not_fit_is_emitted_empty`        |   ✅   | A line that does not fit is emitted empty                                        |

</details>

---

## test_logbuf - native_logbuf - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                  |
| --: | :------------------------------------------------- | :----: | :----------------------------------------------------------- |
|   1 | `test_the_severity_letter_leads_the_line`          |   ✅   | The severity letter leads the line                           |
|   2 | `test_the_levels_are_ordered`                      |   ✅   | The levels are ordered                                       |
|   3 | `test_lines_come_back_oldest_first`                |   ✅   | Lines come back oldest first                                 |
|   4 | `test_the_oldest_is_pruned_on_overflow`            |   ✅   | one letter per line, so each is "I <c>" and its own identity |
|   5 | `test_a_lookup_past_the_end_reports_nothing`       |   ✅   | A lookup past the end reports nothing                        |
|   6 | `test_a_null_message_renders_the_letter_alone`     |   ✅   | A null message renders the letter alone                      |
|   7 | `test_a_line_that_does_not_fit_is_empty`           |   ✅   | A line that does not fit is empty                            |
|   8 | `test_dump_joins_the_held_lines_with_a_newline`    |   ✅   | Dump joins the held lines with a newline                     |
|   9 | `test_dump_fails_closed_when_a_line_would_not_fit` |   ✅   | Dump fails closed when a line would not fit                  |
|  10 | `test_dump_of_an_empty_ring_is_an_empty_string`    |   ✅   | Dump of an empty ring is an empty string                     |
|  11 | `test_dump_refuses_null_and_zero_capacity`         |   ✅   | Dump refuses null and zero capacity                          |
|  12 | `test_the_trap_fires_at_or_above_its_threshold`    |   ✅   | The trap fires at or above its threshold                     |
|  13 | `test_the_trap_sees_the_stored_line`               |   ✅   | The trap sees the stored line                                |
|  14 | `test_the_trap_can_be_turned_off`                  |   ✅   | The trap can be turned off                                   |
|  15 | `test_reset_empties_the_ring`                      |   ✅   | Reset empties the ring                                       |

</details>

---

## test_lonworks - native_lonworks - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                   |
| --: | :------------------------------------------------- | :----: | :------------------------------------------------------------ |
|   1 | `test_lontalk_nv_message_bit_and_selector_width`   |   ✅   | Lontalk nv message bit and selector width                     |
|   2 | `test_lontalk_nv_header_is_two_octets`             |   ✅   | Lontalk nv header is two octets                               |
|   3 | `test_lontalk_parse_reads_the_two_octet_header`    |   ✅   | Lontalk parse reads the two octet header                      |
|   4 | `test_lontalk_a_poll_response_is_header_only`      |   ✅   | Lontalk a poll response is header only                        |
|   5 | `test_nv_selector_bounds_and_round_trip`           |   ✅   | Nv selector bounds and round trip                             |
|   6 | `test_nv_value_round_trip`                         |   ✅   | Nv value round trip                                           |
|   7 | `test_snvt_temp_published_scaling`                 |   ✅   | Snvt temp published scaling                                   |
|   8 | `test_snvt_temp_saturates_at_the_published_bounds` |   ✅   | Snvt temp saturates at the published bounds                   |
|   9 | `test_snvt_switch_published_states`                |   ✅   | -1 in a signed 8-bit field is 0xFF, the published NULL state. |
|  10 | `test_snvt_switch_clamps_to_the_published_range`   |   ✅   | Snvt switch clamps to the published range                     |
|  11 | `test_guards`                                      |   ✅   | Guards                                                        |

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

## test_lsv2 - native_lsv2 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                     |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------------------------- |
|   1 | `test_bare_t_ok_is_eight_octets`                     |   ✅   | Bare t ok is eight octets                                       |
|   2 | `test_length_prefix_counts_only_the_payload`         |   ✅   | Length prefix counts only the payload                           |
|   3 | `test_login_payload_is_a_nul_terminated_group`       |   ✅   | Login payload is a nul terminated group                         |
|   4 | `test_login_appends_the_password_as_a_second_string` |   ✅   | payload = 9 ("PLCDEBUG" + NUL) + 7 ("807667" + NUL) = 16 = 0x10 |
|   5 | `test_logout_with_and_without_a_group`               |   ✅   | an empty string is the same as none                             |
|   6 | `test_filename_command_frames_the_name`              |   ✅   | Filename command frames the name                                |
|   7 | `test_run_info_selector_is_big_endian`               |   ✅   | a selector above 255 puts its high octet first                  |
|   8 | `test_stream_reframes_on_the_consumed_count`         |   ✅   | Stream reframes on the consumed count                           |
|   9 | `test_incomplete_telegram_is_refused`                |   ✅   | Incomplete telegram is refused                                  |
|  10 | `test_error_payload_is_class_then_code`              |   ✅   | the transfer error is the same shape                            |
|  11 | `test_response_mnemonics_are_discriminated`          |   ✅   | Response mnemonics are discriminated                            |
|  12 | `test_builders_refuse_a_short_buffer`                |   ✅   | "INSPECT" needs 8 payload octets; 7 available past the header   |

</details>

---

## test_lwm2m_tlv - native_lwm2m_tlv_codec - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                                                    |
| --: | :--------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_published_device_object_entries`         |   ✅   | Manufacturer: 0b11 0 01 000 = 0xC8, ID 0x00, Length 0x14, "Open Mobile Alliance".              |
|   2 | `test_published_access_control_payload`        |   ✅   | Access Control Object Instance 0: 0b00 0 01 000, ID 0x00, Length 0x0E.                         |
|   3 | `test_sixteen_bit_identifier`                  |   ✅   | 255 still rides in the 8-bit field; 256 is the first that does not.                            |
|   4 | `test_length_field_widths`                     |   ✅   | The Length field is an unsigned integer in network byte order: 256 is 0x01 0x00 over two       |
|   5 | `test_integer_takes_the_shortest_signed_width` |   ✅   | A Value of 1, 2 or 4 octets rides in the inline Length (bits 2-0); 8 passes the inline maximum |
|   6 | `test_boolean_is_one_octet`                    |   ✅   | Boolean is one octet                                                                           |
|   7 | `test_float_is_binary64_in_network_byte_order` |   ✅   | Float is binary64 in network byte order                                                        |
|   8 | `test_writer_fails_closed`                     |   ✅   | A later write that would have fit is refused too: the cursor stays poisoned.                   |
|   9 | `test_reader_refuses_a_truncated_entry`        |   ✅   | A 16-bit Identifier with only one octet behind it.                                             |
|  10 | `test_value_integer_refuses_other_widths`      |   ✅   | Value integer refuses other widths                                                             |
|  11 | `test_write_then_read_round_trip`              |   ✅   | Write then read round trip                                                                     |

</details>

---

## test_mbplus - native_mbplus - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                    | Status | Description                                                   |
| --: | :-------------------------------------- | :----: | :------------------------------------------------------------ |
|   1 | `test_published_check_value`            |   ✅   | init=0xffff and xorout=0xffff cancel over an empty message.   |
|   2 | `test_published_constants`              |   ✅   | Published constants                                           |
|   3 | `test_frame_layout`                     |   ✅   | Frame layout                                                  |
|   4 | `test_token_frame_has_no_payload`       |   ✅   | Token frame has no payload                                    |
|   5 | `test_round_trip`                       |   ✅   | Round trip                                                    |
|   6 | `test_single_bit_corruption_is_refused` |   ✅   | Single bit corruption is refused                              |
|   7 | `test_parse_rejects_bad_framing`        |   ✅   | Parse rejects bad framing                                     |
|   8 | `test_token_ring_rotation`              |   ✅   | The wrap itself, and the fail-safe when no station is active. |
|   9 | `test_build_refuses_bad_arguments`      |   ✅   | Build refuses bad arguments                                   |

</details>

---

## test_mbus - native_mbus - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                                 |
| --: | :-------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_mbdoc_rsp_ud_example`                         |   ✅   | Record 1: DIF 03 = 24-bit integer, VIF 13 = volume 10^(3-6) m3, i.e. litres.                |
|   2 | `test_single_character_ack`                         |   ✅   | Single character ack                                                                        |
|   3 | `test_snd_nke_short_frame`                          |   ✅   | Snd nke short frame                                                                         |
|   4 | `test_req_ud_fcb_toggles_bit5`                      |   ✅   | Req ud fcb toggles bit5                                                                     |
|   5 | `test_control_frame_is_a_long_frame_with_l_three`   |   ✅   | Control frame is a long frame with l three                                                  |
|   6 | `test_build_long_reproduces_the_published_telegram` |   ✅   | Build long reproduces the published telegram                                                |
|   7 | `test_parse_refuses_a_damaged_frame`                |   ✅   | Parse refuses a damaged frame                                                               |
|   8 | `test_parse_refuses_a_truncated_frame`              |   ✅   | Parse refuses a truncated frame                                                             |
|   9 | `test_dif_data_field_lengths`                       |   ✅   | only the low nibble selects the coding; the function / storage bits above it do not         |
|  10 | `test_vif_unit_table`                               |   ✅   | the extension bit is bit 7 and carries no unit information, so setting it changes nothing   |
|  11 | `test_bcd_decoding_and_sign`                        |   ✅   | Bcd decoding and sign                                                                       |
|  12 | `test_integer_decoding_is_little_endian_and_signed` |   ✅   | Integer decoding is little endian and signed                                                |
|  13 | `test_real32_record`                                |   ✅   | Real32 record                                                                               |
|  14 | `test_variable_length_record`                       |   ✅   | Variable length record                                                                      |
|  15 | `test_record_walk_refuses_an_overrun`               |   ✅   | Record walk refuses an overrun                                                              |
|  16 | `test_manufacturer_code_packing`                    |   ✅   | Manufacturer code packing                                                                   |
|  17 | `test_var_header_bounds_and_bcd_validation`         |   ✅   | Var header bounds and bcd validation                                                        |
|  18 | `test_builders_refuse_a_short_buffer`               |   ✅   | MBDOC48 5.2: user data is 0..252 octets, since L is one octet and counts C + A + CI as well |

</details>

---

## test_mdns_adaptive - native_mdns_adaptive - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                                                  |
| --: | :--------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_the_refresher_is_half_the_rfc6762_record_lifetime`   |   ✅   | The refresher is half the rfc6762 record lifetime                                            |
|   2 | `test_init_cannot_produce_a_ceiling_below_the_floor`       |   ✅   | Init cannot produce a ceiling below the floor                                                |
|   3 | `test_contention_backs_the_interval_off_to_the_ceiling`    |   ✅   | A ceiling that is not twice the floor is still exact: the double is clamped, not rounded up. |
|   4 | `test_quiet_air_recovers_the_interval_to_the_floor`        |   ✅   | A halving that would land below the floor is clamped to it, not rounded down.                |
|   5 | `test_moderate_contention_holds_the_interval`              |   ✅   | Moderate contention holds the interval                                                       |
|   6 | `test_due_is_wrap_safe_across_the_millis_rollover`         |   ✅   | The last announce sits 256 ms before the wrap, so the 1000 ms interval expires 744 ms after  |
|   7 | `test_a_sleep_that_would_lapse_the_record_announces_first` |   ✅   | 200 ms since the last announce: a 799 ms sleep lands short of the interval, an 800 ms sleep  |
|   8 | `test_the_sampling_window_reports_the_frames_it_counted`   |   ✅   | 700 - 500 = 200 frames in the window that just closed.                                       |
|   9 | `test_the_sampling_window_is_wrap_safe`                    |   ✅   | The frame counter wraps inside the window: 0x20 - 0xFFFFFFF0 = 0x30 frames.                  |
|  10 | `test_null_handles_are_refused`                            |   ✅   | Null handles are refused                                                                     |

</details>

---

## test_mdns_service - native_mdns_service - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                 | Status | Description                                                                                  |
| --: | :------------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_begin_joins_the_rfc6762_group`                                 |   ✅   | Begin joins the rfc6762 group                                                                |
|   2 | `test_a_response_carries_the_rfc6762_header_bits`                    |   ✅   | A response carries the rfc6762 header bits                                                   |
|   3 | `test_the_cache_flush_bit_separates_unique_records_from_shared_ones` |   ✅   | The cache flush bit separates unique records from shared ones                                |
|   4 | `test_service_enumeration_lists_the_registered_type`                 |   ✅   | Service enumeration lists the registered type                                                |
|   5 | `test_the_service_type_points_at_the_instance`                       |   ✅   | The service type points at the instance                                                      |
|   6 | `test_the_instance_srv_carries_the_port_and_the_target`              |   ✅   | The instance srv carries the port and the target                                             |
|   7 | `test_a_txt_record_is_never_zero_length`                             |   ✅   | A txt record is never zero length                                                            |
|   8 | `test_txt_pairs_are_length_prefixed_key_equals_value`                |   ✅   | Txt pairs are length prefixed key equals value                                               |
|   9 | `test_qtype_any_on_an_instance_answers_srv_and_txt`                  |   ✅   | Qtype any on an instance answers srv and txt                                                 |
|  10 | `test_an_added_service_is_advertised_beside_the_first`               |   ✅   | The first type still answers.                                                                |
|  11 | `test_a_name_this_host_does_not_own_draws_silence`                   |   ✅   | The A record is the egress address, and there is none on a host with no link up, so the name |
|  12 | `test_a_response_on_the_group_is_not_answered`                       |   ✅   | A response on the group is not answered                                                      |
|  13 | `test_a_malformed_query_is_dropped`                                  |   ✅   | QDCOUNT 1, then a label claiming five octets with three present.                             |
|  14 | `test_the_service_table_fills_and_then_refuses`                      |   ✅   | The first service still answers, so the refusal did not disturb the table.                   |
|  15 | `test_a_missing_name_is_refused`                                     |   ✅   | A missing name is refused                                                                    |

</details>

---

## test_melsec - native_melsec - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                                    |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_mitsubishi_batch_read_example`                        |   ✅   | Mitsubishi batch read example                                                                  |
|   2 | `test_head_device_and_device_code_layout`                   |   ✅   | the head device number field is 24 bits wide, so its top octet carries bits 16..23             |
|   3 | `test_device_code_list`                                     |   ✅   | the device code is one octet in the frame, written whatever it is                              |
|   4 | `test_request_data_length_counts_from_the_monitoring_timer` |   ✅   | Request data length counts from the monitoring timer                                           |
|   5 | `test_batch_write_command_and_length`                       |   ✅   | the routing prefix and device tail are the same fields the read writes                         |
|   6 | `test_write_with_no_data`                                   |   ✅   | Write with no data                                                                             |
|   7 | `test_monitoring_timer_is_little_endian`                    |   ✅   | Monitoring timer is little endian                                                              |
|   8 | `test_response_subheader_is_checked`                        |   ✅   | Response subheader is checked                                                                  |
|   9 | `test_error_end_code_response`                              |   ✅   | Error end code response                                                                        |
|  10 | `test_response_length_field_is_validated`                   |   ✅   | a frame shorter than subheader..end code cannot be parsed at all                               |
|  11 | `test_builders_refuse_bad_arguments`                        |   ✅   | the request-length field is 16 bits, so the write data cannot exceed 0xFFFF minus the fixed 12 |

</details>

---

## test_membuild - native_membuild - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                                                                 |
| --: | :------------------------------------------------------------ | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_put_n_takes_the_length_and_not_a_terminator`            |   ✅   | Put n takes the length and not a terminator                                                 |
|   2 | `test_lit_takes_the_array_extent`                             |   ✅   | Lit takes the array extent                                                                  |
|   3 | `test_put_appends_a_runtime_string`                           |   ✅   | Put appends a runtime string                                                                |
|   4 | `test_ch_appends_one_character`                               |   ✅   | Ch appends one character                                                                    |
|   5 | `test_an_append_is_all_or_nothing_at_the_exact_boundary`      |   ✅   | An append is all or nothing at the exact boundary                                           |
|   6 | `test_ok_latches_and_every_later_append_is_a_noop`            |   ✅   | Ok latches and every later append is a noop                                                 |
|   7 | `test_zero_capacity_writes_nothing`                           |   ✅   | Zero capacity writes nothing                                                                |
|   8 | `test_put_clip_fills_what_fits_without_latching`              |   ✅   | A NULL source appends nothing and still does not latch.                                     |
|   9 | `test_u64_clip_right_aligns_or_appends_nothing`               |   ✅   | A field that does not fit appends nothing at all, and does not latch.                       |
|  10 | `test_zero_padded_widths_follow_the_printf_conversions`       |   ✅   | Zero padded widths follow the printf conversions                                            |
|  11 | `test_uint_renders_base_8_10_and_16`                          |   ✅   | Uint renders base 8 10 and 16                                                               |
|  12 | `test_the_full_integer_ranges`                                |   ✅   | INT64_MIN: taking the magnitude by negating the signed value would overflow, so this is the |
|  13 | `test_each_digit_count_at_its_exact_fit`                      |   ✅   | Each digit count at its exact fit                                                           |
|  14 | `test_xml_writes_the_predefined_entities`                     |   ✅   | Xml writes the predefined entities                                                          |
|  15 | `test_json_quotes_and_escapes_the_two_required_characters`    |   ✅   | A NULL source is the empty JSON string, not a crash and not "null".                         |
|  16 | `test_a_json_escape_that_would_straddle_the_end_fails_closed` |   ✅   | A json escape that would straddle the end fails closed                                      |
|  17 | `test_sign_bit_reads_the_encoding_not_the_value`              |   ✅   | Sign bit reads the encoding not the value                                                   |
|  18 | `test_is_inf_and_is_nan_split_the_all_ones_exponent`          |   ✅   | 0x7FF0000000000000 is +inf and 0x7FF0000000000001 a NaN, built from the field layout.       |

</details>

---

## test_membuild - native_mmgr_membuild - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                                                                 |
| --: | :------------------------------------------------------------ | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_put_n_takes_the_length_and_not_a_terminator`            |   ✅   | Put n takes the length and not a terminator                                                 |
|   2 | `test_lit_takes_the_array_extent`                             |   ✅   | Lit takes the array extent                                                                  |
|   3 | `test_put_appends_a_runtime_string`                           |   ✅   | Put appends a runtime string                                                                |
|   4 | `test_ch_appends_one_character`                               |   ✅   | Ch appends one character                                                                    |
|   5 | `test_an_append_is_all_or_nothing_at_the_exact_boundary`      |   ✅   | An append is all or nothing at the exact boundary                                           |
|   6 | `test_ok_latches_and_every_later_append_is_a_noop`            |   ✅   | Ok latches and every later append is a noop                                                 |
|   7 | `test_zero_capacity_writes_nothing`                           |   ✅   | Zero capacity writes nothing                                                                |
|   8 | `test_put_clip_fills_what_fits_without_latching`              |   ✅   | A NULL source appends nothing and still does not latch.                                     |
|   9 | `test_u64_clip_right_aligns_or_appends_nothing`               |   ✅   | A field that does not fit appends nothing at all, and does not latch.                       |
|  10 | `test_zero_padded_widths_follow_the_printf_conversions`       |   ✅   | Zero padded widths follow the printf conversions                                            |
|  11 | `test_uint_renders_base_8_10_and_16`                          |   ✅   | Uint renders base 8 10 and 16                                                               |
|  12 | `test_the_full_integer_ranges`                                |   ✅   | INT64_MIN: taking the magnitude by negating the signed value would overflow, so this is the |
|  13 | `test_each_digit_count_at_its_exact_fit`                      |   ✅   | Each digit count at its exact fit                                                           |
|  14 | `test_xml_writes_the_predefined_entities`                     |   ✅   | Xml writes the predefined entities                                                          |
|  15 | `test_json_quotes_and_escapes_the_two_required_characters`    |   ✅   | A NULL source is the empty JSON string, not a crash and not "null".                         |
|  16 | `test_a_json_escape_that_would_straddle_the_end_fails_closed` |   ✅   | A json escape that would straddle the end fails closed                                      |
|  17 | `test_sign_bit_reads_the_encoding_not_the_value`              |   ✅   | Sign bit reads the encoding not the value                                                   |
|  18 | `test_is_inf_and_is_nan_split_the_all_ones_exponent`          |   ✅   | 0x7FF0000000000000 is +inf and 0x7FF0000000000001 a NaN, built from the field layout.       |

</details>

---

## test_mms - native_mms - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                             | Status | Description                                                                              |
| --: | :--------------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_read_request_ber_nesting`                                  |   ✅   | Read request ber nesting                                                                 |
|   2 | `test_read_response_ber_nesting`                                 |   ✅   | An empty AccessResult still produces a well-formed response, two octets shorter.         |
|   3 | `test_invoke_id_integer_is_minimal_and_positive`                 |   ✅   | The parse reads the same number back out of the wire form.                               |
|   4 | `test_long_form_length_boundary`                                 |   ✅   | One character more than the module's item-name ceiling is refused rather than truncated. |
|   5 | `test_parse_reports_the_pdu_header_and_borrows_the_service_body` |   ✅   | Parse reports the pdu header and borrows the service body                                |
|   6 | `test_parse_of_a_pdu_with_no_service_element`                    |   ✅   | The confirmed-error PDU is the third top-level tag the parse accepts.                    |
|   7 | `test_parse_rejects_malformed_pdus`                              |   ✅   | Parse rejects malformed pdus                                                             |
|   8 | `test_build_refuses_bad_arguments_and_undersized_buffers`        |   ✅   | Build refuses bad arguments and undersized buffers                                       |

</details>

---

## test_mnt - native_mnt - ✅ 31 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_write_then_read_file`                  |   ✅   | Write then read file                  |
|   2 | `test_streamed_write_and_read`               |   ✅   | Streamed write and read               |
|   3 | `test_write_mode_truncates`                  |   ✅   | Write mode truncates                  |
|   4 | `test_append_extends`                        |   ✅   | Append extends                        |
|   5 | `test_remove_and_rename`                     |   ✅   | Remove and rename                     |
|   6 | `test_missing_file_fails_closed`             |   ✅   | Missing file fails closed             |
|   7 | `test_remove_refuses_the_root_itself`        |   ✅   | Remove refuses the root itself        |
|   8 | `test_read_buffer_too_small_fails_closed`    |   ✅   | Read buffer too small fails closed    |
|   9 | `test_file_full_is_bounded`                  |   ✅   | File full is bounded                  |
|  10 | `test_file_pool_exhaustion`                  |   ✅   | File pool exhaustion                  |
|  11 | `test_handle_pool_exhaustion`                |   ✅   | Handle pool exhaustion                |
|  12 | `test_unmounted_fails_closed`                |   ✅   | Unmounted fails closed                |
|  13 | `test_ram_guard_subconditions`               |   ✅   | Ram guard subconditions               |
|  14 | `test_unmounted_all_entry_points`            |   ✅   | Unmounted all entry points            |
|  15 | `test_handle_validity_edges`                 |   ✅   | Handle validity edges                 |
|  16 | `test_write_to_read_handle_rejected`         |   ✅   | Write to read handle rejected         |
|  17 | `test_rename_argument_guards`                |   ✅   | Rename argument guards                |
|  18 | `test_rename_overwrites_destination`         |   ✅   | Rename overwrites destination         |
|  19 | `test_read_file_handle_exhaustion`           |   ✅   | Read file handle exhaustion           |
|  20 | `test_write_file_larger_than_capacity`       |   ✅   | Write file larger than capacity       |
|  21 | `test_zero_progress_backend_terminates`      |   ✅   | Zero progress backend terminates      |
|  22 | `test_root_without_trailing_slash`           |   ✅   | Root without trailing slash           |
|  23 | `test_leaf_joins_onto_a_directory`           |   ✅   | Leaf joins onto a directory           |
|  24 | `test_remove_takes_the_whole_subtree`        |   ✅   | Remove takes the whole subtree        |
|  25 | `test_remove_file_and_missing_unchanged`     |   ✅   | Remove file and missing unchanged     |
|  26 | `test_copy_file`                             |   ✅   | Copy file                             |
|  27 | `test_copy_takes_the_whole_subtree`          |   ✅   | Copy takes the whole subtree          |
|  28 | `test_tree_ops_refuse_traversal`             |   ✅   | Tree ops refuse traversal             |
|  29 | `test_unbound_root_fails_closed`             |   ✅   | Unbound root fails closed             |
|  30 | `test_null_store_is_intentional_and_says_so` |   ✅   | Null store is intentional and says so |
|  31 | `test_status_separates_the_reasons`          |   ✅   | Status separates the reasons          |

</details>

---

## test_modbus - native_modbus - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_map_published_pdu_examples`                    |   ✅   | section 6.1, "read discrete outputs 20-38": start 0013h, quantity 0013h. The published        |
|   2 | `test_unsupported_function_returns_illegal_function` |   ✅   | Unsupported function returns illegal function                                                 |
|   3 | `test_quantity_bounds`                               |   ✅   | the top of the spec's range is never a data-value problem: it either reads, or it runs        |
|   4 | `test_address_plus_quantity_boundary`                |   ✅   | Address plus quantity boundary                                                                |
|   5 | `test_write_single_coil_value_is_ff00_or_0000`       |   ✅   | Write single coil value is ff00 or 0000                                                       |
|   6 | `test_write_multiple_byte_count_must_match_quantity` |   ✅   | and the declared byte count must actually be present in the PDU                               |
|   7 | `test_write_multiple_quantity_limits`                |   ✅   | Write multiple quantity limits                                                                |
|   8 | `test_mask_write_register`                           |   ✅   | Mask write register                                                                           |
|   9 | `test_read_write_multiple_registers`                 |   ✅   | an overlapping transaction reads what the write in the same PDU just placed there             |
|  10 | `test_mbap_header_validation`                        |   ✅   | Read Holding Registers, address 0, one register: MBAP Length 0006h = unit id + a 5-octet PDU. |
|  11 | `test_write_callback_reports_each_write`             |   ✅   | a read fires nothing, and a rejected write fires nothing                                      |
|  12 | `test_data_model_bounds_and_reset`                   |   ✅   | Data model bounds and reset                                                                   |
|  13 | `test_rtu_frame_round_trip`                          |   ✅   | Rtu frame round trip                                                                          |
|  14 | `test_rtu_crc_address_and_broadcast`                 |   ✅   | Rtu crc address and broadcast                                                                 |

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

## test_mpr121 - native_mpr121 - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                   | Status | Description                                                                                    |
| --: | :--------------------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_datasheet_status_register_bit_positions`                         |   ✅   | every electrode, and nothing else in the two registers.                                        |
|   2 | `test_proximity_and_overcurrent_flags`                                 |   ✅   | section 5.2: an over-current also clears the electrode bits, so both can be read from one byte |
|   3 | `test_is_touched_is_bounded_to_twelve_electrodes`                      |   ✅   | 12 and 15 are the proximity and over-current bit positions of the raw status word: not         |
|   4 | `test_filtered_data_is_ten_bits`                                       |   ✅   | anything above bit 9 belongs to no field and is dropped.                                       |
|   5 | `test_build_init_writes_the_datasheet_registers`                       |   ✅   | section 5.13 soft reset, then the ECR written to zero to guarantee Stop Mode.                  |
|   6 | `test_ecr_encodes_the_datasheet_fields`                                |   ✅   | Ecr encodes the datasheet fields                                                               |
|   7 | `test_build_init_length_tracks_the_electrode_count`                    |   ✅   | Build init length tracks the electrode count                                                   |
|   8 | `test_build_init_refuses_bad_arguments`                                |   ✅   | Build init refuses bad arguments                                                               |
|   9 | `test_datasheet_model_starts_stopped_and_unconfigured`                 |   ✅   | Datasheet model starts stopped and unconfigured                                                |
|  10 | `test_datasheet_a_config_write_in_run_mode_is_discarded`               |   ✅   | Datasheet a config write in run mode is discarded                                              |
|  11 | `test_datasheet_begin_leaves_every_configuration_register_written`     |   ✅   | the rising / falling / touched baseline filter defaults (AN3944)                               |
|  12 | `test_datasheet_a_touch_on_the_pads_reads_back`                        |   ✅   | one in each status byte at once, so a decoder that drops the high byte fails here              |
|  13 | `test_datasheet_proximity_and_overcurrent_are_not_electrodes`          |   ✅   | and it is visible where the datasheet puts it                                                  |
|  14 | `test_datasheet_nothing_is_measured_in_stop_mode`                      |   ✅   | Datasheet nothing is measured in stop mode                                                     |
|  15 | `test_datasheet_filtered_data_is_ten_bits_across_two_registers`        |   ✅   | Datasheet filtered data is ten bits across two registers                                       |
|  16 | `test_datasheet_each_electrode_reads_its_own_filtered_data`            |   ✅   | Datasheet each electrode reads its own filtered data                                           |
|  17 | `test_datasheet_the_soft_reset_returns_the_part_to_its_power_up_state` |   ✅   | and begin() brings it all back                                                                 |
|  18 | `test_read_filtered_refuses_an_out_of_range_electrode`                 |   ✅   | Read filtered refuses an out of range electrode                                                |
|  19 | `test_begin_sends_later_transfers_to_the_address_it_was_given`         |   ✅   | Begin sends later transfers to the address it was given                                        |
|  20 | `test_a_refused_transfer_fails_begin`                                  |   ✅   | A refused transfer fails begin                                                                 |
|  21 | `test_a_refused_read_reports_nothing_touched`                          |   ✅   | A refused read reports nothing touched                                                         |

</details>

---

## test_mqtt - native_mqtt_codec - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                                                    |
| --: | :--------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_table_2_4_remaining_length_boundaries`   |   ✅   | Table 2 4 remaining length boundaries                                                          |
|   2 | `test_remaining_length_worked_examples`        |   ✅   | Remaining length worked examples                                                               |
|   3 | `test_remaining_length_bounds`                 |   ✅   | Four continuation octets: the field would run to a fifth.                                      |
|   4 | `test_connect_matches_figure_3_6`              |   ✅   | Connect matches figure 3 6                                                                     |
|   5 | `test_connect_flags_follow_the_fields_present` |   ✅   | Will QoS 2 and Will Retain set, with a Will Topic present this time: bits 4-3 are 10 and bit 5 |
|   6 | `test_publish_matches_figure_3_11`             |   ✅   | Publish matches figure 3 11                                                                    |
|   7 | `test_publish_fixed_header_flags`              |   ✅   | A QoS above 2 has no encoding.                                                                 |
|   8 | `test_publish_refuses_wildcards`               |   ✅   | The same filters are accepted by a SUBSCRIBE.                                                  |
|   9 | `test_subscribe_matches_figure_3_23`           |   ✅   | MQTT-3.8.3-4: the Requested QoS must be 0, 1 or 2.                                             |
|  10 | `test_unsubscribe_reserved_flags`              |   ✅   | Unsubscribe reserved flags                                                                     |
|  11 | `test_ack_packets_are_four_octets`             |   ✅   | Table 2.1: the type values are what the nibble carries.                                        |
|  12 | `test_pingreq_and_disconnect_are_two_octets`   |   ✅   | Pingreq and disconnect are two octets                                                          |
|  13 | `test_parse_fixed_header`                      |   ✅   | One octet is never a whole fixed header.                                                       |
|  14 | `test_parse_publish_round_trip`                |   ✅   | sec 3.3.2.2: at QoS 0 there is no Packet Identifier, so the whole body past the Topic Name is  |
|  15 | `test_parse_publish_refuses_a_malformed_body`  |   ✅   | Both QoS bits set.                                                                             |
|  16 | `test_parse_connack_table_3_1`                 |   ✅   | A body shorter than the two octets sec 3.2.2 defines reports -1 rather than a code.            |
|  17 | `test_parse_suback_return_codes`               |   ✅   | A SUBACK with no return code at all reports Failure rather than a subscription that took.      |
|  18 | `test_parse_ack_packet_identifier`             |   ✅   | Parse ack packet identifier                                                                    |
|  19 | `test_builds_refuse_short_buffers`             |   ✅   | The body scratch is checked before a single octet of it is used.                               |

</details>

---

## test_mqtt_sn - native_mqtt_sn_codec - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                                    |
| --: | :------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_length_field_switches_at_255`                |   ✅   | A PUBLISH is Length + MsgType + Flags + TopicId 2 + MsgId 2 + Data, so the 1-octet form covers |
|   2 | `test_flags_octet_bit_positions`                   |   ✅   | TopicIdType: 0b00 normal, 0b01 pre-defined, 0b10 short topic name (sec 5.3.4).                 |
|   3 | `test_connect_variable_part`                       |   ✅   | 1 Length + 1 MsgType + 1 Flags + 1 ProtocolId + 2 Duration + 6 ClientId = 12.                  |
|   4 | `test_register_and_regack`                         |   ✅   | 1 + 1 + 2 + 2 + 14 = 20.                                                                       |
|   5 | `test_publish_and_puback`                          |   ✅   | 1 + 1 + 1 + 2 + 2 + 4 = 11.                                                                    |
|   6 | `test_subscribe_by_name_and_by_id`                 |   ✅   | 1 + 1 + 1 + 2 + 3 = 8.                                                                         |
|   7 | `test_pingreq_and_disconnect_optional_fields`      |   ✅   | sec 5.4.2: SEARCHGW is Length, MsgType, Radius, and Radius 0x00 broadcasts to all nodes.       |
|   8 | `test_table_3_msgtype_values`                      |   ✅   | sec 5.3.10 Table 5: the ReturnCode values.                                                     |
|   9 | `test_header_parse_refuses_an_inconsistent_length` |   ✅   | The 3-octet form declaring a total of 2 cannot cover its own three octets plus a MsgType.      |
|  10 | `test_typed_parsers_refuse_a_short_variable_part`  |   ✅   | Typed parsers refuse a short variable part                                                     |
|  11 | `test_builders_fail_closed`                        |   ✅   | Builders fail closed                                                                           |

</details>

---

## test_msgpack - native_msgpack_wire - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                                                                      |
| --: | :---------------------------------------- | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_spec_first_byte_table`              |   ✅   | positive fixint stores a 7-bit unsigned integer, so 0x7f is its last value       |
|   2 | `test_spec_float32`                       |   ✅   | Spec float32                                                                     |
|   3 | `test_label_is_the_numeric_spelling`      |   ✅   | Label is the numeric spelling                                                    |
|   4 | `test_peek_maps_first_byte_to_type`       |   ✅   | Peek maps first byte to type                                                     |
|   5 | `test_never_used_and_ext_are_invalid`     |   ✅   | an empty region has nothing to name either                                       |
|   6 | `test_decode_int_widths`                  |   ✅   | the int reader also accepts the uint family, since those values are integers too |
|   7 | `test_decode_str_and_bin_alias_the_input` |   ✅   | Decode str and bin alias the input                                               |
|   8 | `test_str_and_bin_families_do_not_cross`  |   ✅   | Str and bin families do not cross                                                |
|   9 | `test_decode_nil_bool_float`              |   ✅   | float 64: 1.5 is sign 0, biased exponent 1023 = 0x3ff, mantissa 0x8000000000000  |
|  10 | `test_map_round_trip`                     |   ✅   | Map round trip                                                                   |
|  11 | `test_array_16_round_trip`                |   ✅   | Array 16 round trip                                                              |
|  12 | `test_truncated_input_fails_closed`       |   ✅   | Truncated input fails closed                                                     |
|  13 | `test_error_is_sticky`                    |   ✅   | Error is sticky                                                                  |
|  14 | `test_overflow_reports_the_size_needed`   |   ✅   | produced() yields nothing once the region overflowed                             |
|  15 | `test_null_string_is_empty`               |   ✅   | Null string is empty                                                             |

</details>

---

## test_mtconnect - native_mtconnect - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                        | Status | Description                                                                       |
| --: | :-------------------------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------- |
|   1 | `test_the_xml_checker_rejects_malformed_documents`                          |   ✅   | The xml checker rejects malformed documents                                       |
|   2 | `test_every_document_is_well_formed_xml`                                    |   ✅   | Every document is well formed xml                                                 |
|   3 | `test_root_namespaces_are_the_published_target_namespaces`                  |   ✅   | Root namespaces are the published target namespaces                               |
|   4 | `test_each_root_is_a_header_followed_by_its_payload`                        |   ✅   | Each root is a header followed by its payload                                     |
|   5 | `test_every_header_carries_the_attributes_its_schema_marks_required`        |   ✅   | Every header carries the attributes its schema marks required                     |
|   6 | `test_the_device_stream_carries_name_and_uuid`                              |   ✅   | The device stream carries name and uuid                                           |
|   7 | `test_the_component_stream_carries_component_and_component_id`              |   ✅   | The component stream carries component and component id                           |
|   8 | `test_the_component_stream_groups_each_category_once_and_in_order`          |   ✅   | The component stream groups each category once and in order                       |
|   9 | `test_a_sample_and_an_event_carry_the_required_result_attributes`           |   ✅   | A sample and an event carry the required result attributes                        |
|  10 | `test_a_condition_is_one_of_the_four_published_states`                      |   ✅   | No state named: the element still has to be one of the four the schema publishes. |
|  11 | `test_a_data_item_carries_its_required_attributes_and_a_published_category` |   ✅   | A data item carries its required attributes and a published category              |
|  12 | `test_the_probed_device_carries_id_name_and_uuid`                           |   ✅   | The probed device carries id name and uuid                                        |
|  13 | `test_markup_characters_in_values_are_escaped`                              |   ✅   | Markup characters in values are escaped                                           |
|  14 | `test_the_cutting_tool_life_cycle_carries_a_cutter_status`                  |   ✅   | The cutting tool life cycle carries a cutter status                               |
|  15 | `test_tool_life_carries_the_attributes_the_schema_marks_required`           |   ✅   | Tool life carries the attributes the schema marks required                        |
|  16 | `test_the_cutting_tool_reports_its_asset_id_and_life_values`                |   ✅   | The cutting tool reports its asset id and life values                             |
|  17 | `test_overflow_reports_zero_length`                                         |   ✅   | Overflow reports zero length                                                      |
|  18 | `test_sample_buffer_assigns_monotonic_sequences`                            |   ✅   | Sample buffer assigns monotonic sequences                                         |
|  19 | `test_sample_buffer_eviction_advances_the_window`                           |   ✅   | Sample buffer eviction advances the window                                        |
|  20 | `test_sample_query_replays_the_requested_window`                            |   ✅   | Sample query replays the requested window                                         |
|  21 | `test_sample_query_clamps_a_stale_from`                                     |   ✅   | Sample query clamps a stale from                                                  |
|  22 | `test_sample_query_past_the_newest_returns_no_observations`                 |   ✅   | Sample query past the newest returns no observations                              |

</details>

---

## test_net_addr - native_net_addr - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                                           |
| --: | :-------------------------------------------------------- | :----: | :-------------------------------------------------------------------- |
|   1 | `test_v4_reads_into_the_library_address_in_network_order` |   ✅   | V4 reads into the library address in network order                    |
|   2 | `test_v6_reads_into_the_library_address_unchanged`        |   ✅   | V6 reads into the library address unchanged                           |
|   3 | `test_an_untagged_family_reads_as_none`                   |   ✅   | An untagged family reads as none                                      |
|   4 | `test_a_null_source_empties_the_destination`              |   ✅   | A null source empties the destination                                 |
|   5 | `test_a_null_destination_reads_nothing`                   |   ✅   | A null destination reads nothing                                      |
|   6 | `test_v4_writes_into_the_stack_address`                   |   ✅   | V4 writes into the stack address                                      |
|   7 | `test_v6_writes_into_the_stack_address`                   |   ✅   | A stack built without v6 has nowhere to put the address, and says so. |
|   8 | `test_an_empty_address_cannot_be_written`                 |   ✅   | An empty address cannot be written                                    |
|   9 | `test_a_null_source_cannot_be_written`                    |   ✅   | A null source cannot be written                                       |
|  10 | `test_a_null_destination_cannot_be_written`               |   ✅   | A null destination cannot be written                                  |
|  11 | `test_the_entry_reads_the_operands_off_the_handle`        |   ✅   | The entry reads the operands off the handle                           |
|  12 | `test_the_entry_reports_the_outcome_on_the_handle`        |   ✅   | The entry reports the outcome on the handle                           |
|  13 | `test_the_entry_reports_a_refusal_on_the_handle`          |   ✅   | The entry reports a refusal on the handle                             |
|  14 | `test_each_entry_reaches_the_call_its_name_promises`      |   ✅   | Each entry reaches the call its name promises                         |
|  15 | `test_a_v4_round_trip_is_the_address_it_started_as`       |   ✅   | A v4 round trip is the address it started as                          |
|  16 | `test_a_v6_round_trip_is_the_address_it_started_as`       |   ✅   | A v6 round trip is the address it started as                          |

</details>

---

## test_nats - native_nats_proto - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                  |
| --: | :-------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_published_pub_examples`                 |   ✅   | Published pub examples                                                                       |
|   2 | `test_published_hpub_examples`                |   ✅   | "NATS/1.0" 8 + CRLF 2 + "Bar: Baz" 8 + CRLF 2 + CRLF 2 = 22, the count the example prints.   |
|   3 | `test_published_sub_examples`                 |   ✅   | Published sub examples                                                                       |
|   4 | `test_published_unsub_examples`               |   ✅   | Published unsub examples                                                                     |
|   5 | `test_ping_pong_and_connect`                  |   ✅   | Ping pong and connect                                                                        |
|   6 | `test_published_msg_examples`                 |   ✅   | Published msg examples                                                                       |
|   7 | `test_published_hmsg_example`                 |   ✅   | The same message with the optional reply-to left off.                                        |
|   8 | `test_control_line_only_operations`           |   ✅   | -ERR <error message>: the reference's own answer to an operation the server does not know.   |
|   9 | `test_repeated_whitespace_is_one_delimiter`   |   ✅   | Repeated whitespace is one delimiter                                                         |
|  10 | `test_parse_waits_for_the_whole_operation`    |   ✅   | A stream of two operations walks one at a time by the octets each reports.                   |
|  11 | `test_malformed_control_lines_are_refused`    |   ✅   | Malformed control lines are refused                                                          |
|  12 | `test_builders_fail_closed`                   |   ✅   | An HPUB with no header section is not an HPUB.                                               |
|  13 | `test_byte_counts_render_and_read_as_decimal` |   ✅   | "PUB FOO 1234\r\n" is 14 octets ahead of the payload, and the trailing CR LF is 2 behind it. |

</details>

---

## test_nema_ts2 - native_nema_ts2_sdlc - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                                                                                  |
| --: | :---------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_x25_check_value_frames_the_fcs`     |   ✅   | and the helper on its own reports the same published check value                             |
|   2 | `test_hdlc_good_fcs_residue`              |   ✅   | swapping the two FCS octets destroys the residue, which is what makes the order load bearing |
|   3 | `test_build_parse_round_trip`             |   ✅   | Build parse round trip                                                                       |
|   4 | `test_parse_refuses_any_single_bit_error` |   ✅   | Parse refuses any single bit error                                                           |
|   5 | `test_parse_refuses_short_frames`         |   ✅   | Parse refuses short frames                                                                   |
|   6 | `test_zero_length_data_frame`             |   ✅   | Zero length data frame                                                                       |
|   7 | `test_build_bounds`                       |   ✅   | Build bounds                                                                                 |
|   8 | `test_crc_of_an_empty_span`               |   ✅   | Crc of an empty span                                                                         |
|   9 | `test_frame_type_response_offset`         |   ✅   | Frame type response offset                                                                   |

</details>

---

## test_net_egress - native_net_egress - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                     |
| --: | :--------------------------------------------- | :----: | :-------------------------------------------------------------- |
|   1 | `test_classify_maps_the_live_route`            |   ✅   | The route's address is the station's -> the station carries it. |
|   2 | `test_a_down_interface_never_claims_the_route` |   ✅   | softAP up, station up, route matching neither -> still wired.   |
|   3 | `test_station_is_compared_before_the_softap`   |   ✅   | Station is compared before the softap                           |
|   4 | `test_egress_follows_the_live_default_route`   |   ✅   | Egress follows the live default route                           |
|   5 | `test_a_wired_link_takes_the_route`            |   ✅   | A wired link takes the route                                    |
|   6 | `test_wifi_bring_up_associates_the_station`    |   ✅   | Wifi bring up associates the station                            |
|   7 | `test_ipv6_autoconfigures_a_global_address`    |   ✅   | Ipv6 autoconfigures a global address                            |
|   8 | `test_radio_readouts_answer_from_the_link`     |   ✅   | Radio readouts answer from the link                             |
|   9 | `test_mac_readouts_fill_six_octets`            |   ✅   | Mac readouts fill six octets                                    |
|  10 | `test_radio_control_applies_and_reads_back`    |   ✅   | Capture with nowhere to deliver is a caller bug, not a mode.    |
|  11 | `test_layer_handle_is_bound`                   |   ✅   | Layer handle is bound                                           |

</details>

---

## test_net_egress - native_l1_egress - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                     |
| --: | :--------------------------------------------- | :----: | :-------------------------------------------------------------- |
|   1 | `test_classify_maps_the_live_route`            |   ✅   | The route's address is the station's -> the station carries it. |
|   2 | `test_a_down_interface_never_claims_the_route` |   ✅   | softAP up, station up, route matching neither -> still wired.   |
|   3 | `test_station_is_compared_before_the_softap`   |   ✅   | Station is compared before the softap                           |
|   4 | `test_egress_follows_the_live_default_route`   |   ✅   | Egress follows the live default route                           |
|   5 | `test_a_wired_link_takes_the_route`            |   ✅   | A wired link takes the route                                    |
|   6 | `test_wifi_bring_up_associates_the_station`    |   ✅   | Wifi bring up associates the station                            |
|   7 | `test_ipv6_autoconfigures_a_global_address`    |   ✅   | Ipv6 autoconfigures a global address                            |
|   8 | `test_radio_readouts_answer_from_the_link`     |   ✅   | Radio readouts answer from the link                             |
|   9 | `test_mac_readouts_fill_six_octets`            |   ✅   | Mac readouts fill six octets                                    |
|  10 | `test_radio_control_applies_and_reads_back`    |   ✅   | Capture with nowhere to deliver is a caller bug, not a mode.    |
|  11 | `test_layer_handle_is_bound`                   |   ✅   | Layer handle is bound                                           |

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

## test_nmea0183 - native_gnss_nmea0183 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                             |
| --: | :-------------------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_published_sentence_checksums`           |   ✅   | Published sentence checksums                                                            |
|   2 | `test_checksum_is_the_xor_of_the_body`        |   ✅   | $GPZDA,160012.71,11,03,2004,-1,00*7D, folded the same way, lands on 7D.                 |
|   3 | `test_build_frames_the_body`                  |   ✅   | The build of every published sentence's body reproduces that sentence.                  |
|   4 | `test_address_splits_into_talker_and_type`    |   ✅   | Address splits into talker and type                                                     |
|   5 | `test_empty_fields_are_counted`               |   ✅   | Empty fields are counted                                                                |
|   6 | `test_framing_is_enforced`                    |   ✅   | '!' is the AIS-encapsulation delimiter and is accepted; the checksum rule is unchanged. |
|   7 | `test_gga_decodes_the_published_fix`          |   ✅   | A GGA decoder refuses a sentence that is not a GGA.                                     |
|   8 | `test_rmc_decodes_the_published_fix`          |   ✅   | Rmc decodes the published fix                                                           |
|   9 | `test_gsv_decodes_the_published_sky_view`     |   ✅   | Gsv decodes the published sky view                                                      |
|  10 | `test_zda_decodes_the_published_time`         |   ✅   | Zda decodes the published time                                                          |
|  11 | `test_vtg_decodes_the_published_course`       |   ✅   | Vtg decodes the published course                                                        |
|  12 | `test_gsa_decodes_the_published_fix_set`      |   ✅   | Gsa decodes the published fix set                                                       |
|  13 | `test_gll_decodes_the_published_position`     |   ✅   | Gll decodes the published position                                                      |
|  14 | `test_dpt_decodes_the_published_depth`        |   ✅   | Dpt decodes the published depth                                                         |
|  15 | `test_instrument_sentences_round_trip`        |   ✅   | MWV: angle, reference, speed, units, status.                                            |
|  16 | `test_typed_decoders_check_the_sentence_type` |   ✅   | A GGA truncated before its altitude no longer has the fields the decoder needs.         |
|  17 | `test_hemisphere_letters_set_the_sign`        |   ✅   | Hemisphere letters set the sign                                                         |

</details>

---

## test_nmea2000 - native_marine_nmea2000 - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                                                   |
| --: | :-------------------------------------------------------- | :----: | :---------------------------------------------------------------------------- |
|   1 | `test_fastpacket_frame_count`                             |   ✅   | The transport tops out at 223 octets: 6 + 31 * 7 = 223, so exactly 32 frames. |
|   2 | `test_fastpacket_split_and_reassemble`                    |   ✅   | Fastpacket split and reassemble                                               |
|   3 | `test_fastpacket_rejects_out_of_order_and_foreign_frames` |   ✅   | Frame 2 before frame 1 is out of order.                                       |
|   4 | `test_fastpacket_bounds`                                  |   ✅   | Fastpacket bounds                                                             |
|   5 | `test_single_frame_message`                               |   ✅   | More than eight octets is not a single frame.                                 |
|   6 | `test_position_rapid_update`                              |   ✅   | The all-ones signed raw is the not-available marker.                          |
|   7 | `test_cog_sog_rapid_update`                               |   ✅   | Cog sog rapid update                                                          |
|   8 | `test_engine_rapid_update`                                |   ✅   | Engine rapid update                                                           |
|   9 | `test_wind_data`                                          |   ✅   | Wind data                                                                     |
|  10 | `test_water_depth`                                        |   ✅   | Water depth                                                                   |
|  11 | `test_vessel_heading`                                     |   ✅   | Vessel heading                                                                |
|  12 | `test_temperature_converts_kelvin_to_celsius`             |   ✅   | Temperature converts kelvin to celsius                                        |
|  13 | `test_attitude_angles_are_signed`                         |   ✅   | Attitude angles are signed                                                    |
|  14 | `test_battery_status`                                     |   ✅   | Battery status                                                                |
|  15 | `test_engine_dynamic_rides_the_fast_packet`               |   ✅   | 26 octets is 6 + 7 + 7 + 6, so four frames.                                   |
|  16 | `test_short_payloads_are_refused`                         |   ✅   | At their documented minimum lengths they all succeed.                         |

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

## test_ntp_server - native_ntp_server - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                                        |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------------------- |
|   1 | `test_rfc5905_header_layout`                                  |   ✅   | Rfc5905 header layout                                              |
|   2 | `test_rfc5905_first_octet_packing`                            |   ✅   | Rfc5905 first octet packing                                        |
|   3 | `test_rfc4330_version_and_poll_are_copied_intact`             |   ✅   | LI 0, VN vn, Mode 3: 0 * 64 + vn * 8 + 3.                          |
|   4 | `test_rfc4330_a_zero_poll_is_copied_intact_too`               |   ✅   | Rfc4330 a zero poll is copied intact too                           |
|   5 | `test_rfc4330_precision_lies_in_the_published_range`          |   ✅   | Rfc4330 precision lies in the published range                      |
|   6 | `test_rfc4330_a_primary_server_zeroes_delay_and_dispersion`   |   ✅   | Rfc4330 a primary server zeroes delay and dispersion               |
|   7 | `test_rfc4330_origin_is_the_request_transmit_stamp`           |   ✅   | Rfc4330 origin is the request transmit stamp                       |
|   8 | `test_rfc5905_stratum_and_reference_id_are_written_verbatim`  |   ✅   | Rfc5905 stratum and reference id are written verbatim              |
|   9 | `test_rfc5905_a_synchronized_reply_carries_no_zero_timestamp` |   ✅   | Figure 31: x.xmt <- clock, the field a client reads the time from. |
|  10 | `test_a_packet_short_of_the_48_octet_header_is_refused`       |   ✅   | A packet short of the 48 octet header is refused                   |
|  11 | `test_rfc5905_begin_binds_the_published_port`                 |   ✅   | Rfc5905 begin binds the published port                             |
|  12 | `test_a_client_request_is_answered_on_the_wire`               |   ✅   | A client request is answered on the wire                           |
|  13 | `test_rfc4330_a_non_client_mode_request_is_discarded`         |   ✅   | LI 0, VN 4, Mode MODE[i]: 0 * 64 + 4 * 8 + MODE[i] = 32 + MODE[i]. |
|  14 | `test_rfc4330_symmetric_active_is_answered_symmetric_passive` |   ✅   | Rfc4330 symmetric active is answered symmetric passive             |
|  15 | `test_a_server_with_no_clock_puts_no_time_on_the_wire`        |   ✅   | A server with no clock puts no time on the wire                    |
|  16 | `test_a_runt_datagram_is_dropped`                             |   ✅   | A runt datagram is dropped                                         |

</details>

---

## test_ntp_service - native_ntp_service - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                             | Status | Description                                                                           |
| --: | :--------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_the_request_is_an_rfc4330_client_packet`                   |   ✅   | Every octet between the first and the transmit timestamp is zero.                     |
|   2 | `test_a_matching_reply_sets_the_clock`                           |   ✅   | A matching reply sets the clock                                                       |
|   3 | `test_a_reply_whose_origin_does_not_echo_the_request_is_ignored` |   ✅   | Off by one in the cookie is still not an answer.                                      |
|   4 | `test_a_packet_that_is_not_a_server_reply_is_ignored`            |   ✅   | A packet that is not a server reply is ignored                                        |
|   5 | `test_a_kiss_o_death_or_unsynchronized_stratum_is_ignored`       |   ✅   | Stratum 1 (a primary reference) and 15 (the last secondary) are both taken.           |
|   6 | `test_an_implausible_clock_is_ignored`                           |   ✅   | All-zero timestamps: RFC 4330 sec 5 says an unsynchronized server sends exactly this. |
|   7 | `test_a_reply_short_of_48_octets_is_ignored`                     |   ✅   | A reply short of 48 octets is ignored                                                 |
|   8 | `test_the_epoch_advances_off_the_monotonic_clock`                |   ✅   | Sub-second ticks do not move the reported second.                                     |
|   9 | `test_a_server_name_is_refused`                                  |   ✅   | A server name is refused                                                              |

</details>

---

## test_ntrip_caster - native_gnss_ntrip_caster - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                  | Status | Description                                                                                  |
| --: | :-------------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_rfc9112_header_block_terminates_the_request`                    |   ✅   | Rfc9112 header block terminates the request                                                  |
|   2 | `test_rfc7617_basic_credentials`                                      |   ✅   | No Authorization line is no credentials, not empty ones: an empty span would authenticate as |
|   3 | `test_rfc9110_header_names_are_case_insensitive`                      |   ✅   | Rfc9110 header names are case insensitive                                                    |
|   4 | `test_the_version_header_discriminates_the_two_revisions`             |   ✅   | The version header discriminates the two revisions                                           |
|   5 | `test_ntrip_a_request_without_a_mountpoint_asks_for_the_source_table` |   ✅   | Ntrip a request without a mountpoint asks for the source table                               |
|   6 | `test_non_get_requests_and_the_mountpoint_bound`                      |   ✅   | Non get requests and the mountpoint bound                                                    |
|   7 | `test_stream_response_is_one_complete_message_per_revision`           |   ✅   | Stream response is one complete message per revision                                         |
|   8 | `test_a_stream_response_is_not_a_sourcetable_response`                |   ✅   | A stream response is not a sourcetable response                                              |
|   9 | `test_rfc9110_error_and_unauthorized_responses`                       |   ✅   | Rfc9110 error and unauthorized responses                                                     |
|  10 | `test_str_record_field_positions`                                     |   ✅   | field 12 nmea: "Caster requires NMEA input (1) or not (0)", so the flag is the digit.        |
|  11 | `test_str_record_latitude_and_longitude_carry_two_decimals`           |   ✅   | Str record latitude and longitude carry two decimals                                         |
|  12 | `test_str_record_unset_fields_keep_the_layout`                        |   ✅   | Str record unset fields keep the layout                                                      |
|  13 | `test_ntrip_sourcetable_frames_its_body`                              |   ✅   | Ntrip sourcetable frames its body                                                            |
|  14 | `test_ntrip_v1_sourcetable_server_field`                              |   ✅   | Ntrip v1 sourcetable server field                                                            |
|  15 | `test_ntrip_sourcetable_empty_and_overflow`                           |   ✅   | Ntrip sourcetable empty and overflow                                                         |

</details>

---

## test_nts - native_nts_ke - ✅ 17 passed

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
|   8 | `test_ke_parse_refuses_a_record_after_end_of_message`    |   ✅   | Ke parse refuses a record after end of message    |
|   9 | `test_rfc7822_length_includes_header_and_padding`        |   ✅   | Rfc7822 length includes header and padding        |
|  10 | `test_every_extension_field_is_word_aligned`             |   ✅   | Every extension field is word aligned             |
|  11 | `test_extension_field_types_match_the_registry`          |   ✅   | Extension field types match the registry          |
|  12 | `test_exporter_label_is_the_registered_string`           |   ✅   | Exporter label is the registered string           |
|  13 | `test_record_type_numbers_match_the_registry`            |   ✅   | Record type numbers match the registry            |
|  14 | `test_ke_record_fails_closed`                            |   ✅   | Ke record fails closed                            |
|  15 | `test_ke_request_needs_all_sixteen_octets`               |   ✅   | Ke request needs all sixteen octets               |
|  16 | `test_extension_field_length_bound`                      |   ✅   | Extension field length bound                      |
|  17 | `test_extension_field_fails_closed`                      |   ✅   | Extension field fails closed                      |

</details>

---

## test_oauth2_exchange - native_oauth2_exchange - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                              | Status | Description                                                |
| --: | :---------------------------------------------------------------- | :----: | :--------------------------------------------------------- |
|   1 | `test_the_span_carves_the_two_exchange_buffers`                   |   ✅   | The span carves the two exchange buffers                   |
|   2 | `test_the_span_is_taken_once`                                     |   ✅   | The span is taken once                                     |
|   3 | `test_exchange_code_builds_the_sec413_body_into_its_own_buffer`   |   ✅   | Exchange code builds the sec413 body into its own buffer   |
|   4 | `test_refresh_builds_the_sec6_body_into_its_own_buffer`           |   ✅   | Refresh builds the sec6 body into its own buffer           |
|   5 | `test_an_unbuildable_grant_reports_err_build`                     |   ✅   | An unbuildable grant reports err build                     |
|   6 | `test_an_exchange_that_reaches_no_endpoint_reports_err_transport` |   ✅   | An exchange that reaches no endpoint reports err transport |
|   7 | `test_the_exchange_posts_the_body_it_built`                       |   ✅   | The exchange posts the body it built                       |

</details>

---

## test_oauth2_transport - native_oauth2_transport - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                          |
| --: | :---------------------------------------------------------- | :----: | :--------------------------------------------------- |
|   1 | `test_the_exchange_posts_the_sec413_body_to_the_endpoint`   |   ✅   | The exchange posts the sec413 body to the endpoint   |
|   2 | `test_the_refresh_posts_the_sec6_body`                      |   ✅   | The refresh posts the sec6 body                      |
|   3 | `test_a_sec51_response_fills_the_tokens`                    |   ✅   | A sec51 response fills the tokens                    |
|   4 | `test_a_sec52_error_object_keeps_its_status`                |   ✅   | A sec52 error object keeps its status                |
|   5 | `test_a_2xx_without_an_access_token_reports_err_response`   |   ✅   | A 2xx without an access token reports err response   |
|   6 | `test_an_endpoint_that_never_answers_reports_err_transport` |   ✅   | An endpoint that never answers reports err transport |
|   7 | `test_an_unbuildable_grant_sends_nothing`                   |   ✅   | An unbuildable grant sends nothing                   |

</details>

---

## test_observability - native_observability - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                            |
| --: | :------------------------------------------------------------ | :----: | :----------------------------------------------------- |
|   1 | `test_transition_fires_hook_with_args`                        |   ✅   | Transition fires hook with args                        |
|   2 | `test_each_reason_bumps_its_counter`                          |   ✅   | Each reason bumps its counter                          |
|   3 | `test_closing_gauge_is_derived_from_pool`                     |   ✅   | Closing gauge is derived from pool                     |
|   4 | `test_reset_clears_cumulative_not_derived_gauge`              |   ✅   | Reset clears cumulative not derived gauge              |
|   5 | `test_no_hook_after_unregister`                               |   ✅   | No hook after unregister                               |
|   6 | `test_notice_without_hook_still_counts`                       |   ✅   | Notice without hook still counts                       |
|   7 | `test_recv_fin_counts_remote_close`                           |   ✅   | Recv fin counts remote close                           |
|   8 | `test_err_cb_counts_error_close`                              |   ✅   | Err cb counts error close                              |
|   9 | `test_timeout_sweep_counts_timeout`                           |   ✅   | Timeout sweep counts timeout                           |
|  10 | `test_local_close_counts_local`                               |   ✅   | Local close counts local                               |
|  11 | `test_abort_slot_counts_abort_and_frees`                      |   ✅   | Abort slot counts abort and frees                      |
|  12 | `test_abort_slot_noop_on_free_slot`                           |   ✅   | Abort slot noop on free slot                           |
|  13 | `test_backpressure_counts_when_ring_full`                     |   ✅   | Backpressure counts when ring full                     |
|  14 | `test_begin_close_dwells_then_drains_on_ack`                  |   ✅   | Begin close dwells then drains on ack                  |
|  15 | `test_begin_close_finalizes_immediately_when_already_drained` |   ✅   | Begin close finalizes immediately when already drained |
|  16 | `test_begin_close_noop_if_not_active`                         |   ✅   | Begin close noop if not active                         |
|  17 | `test_closing_timeout_reaps_stuck_slot`                       |   ✅   | Closing timeout reaps stuck slot                       |
|  18 | `test_stop_posts_abort_transition_for_each_live_slot`         |   ✅   | Stop posts abort transition for each live slot         |
|  19 | `test_err_cb_during_closing_counts_drained_not_error`         |   ✅   | Err cb during closing counts drained not error         |
|  20 | `test_enqueue_failure_from_recv_cb_counts_defer_drop`         |   ✅   | Enqueue failure from recv cb counts defer drop         |
|  21 | `test_accept_cb_posts_accept_transition`                      |   ✅   | Accept cb posts accept transition                      |
|  22 | `test_accept_cb_enqueue_failure_posts_defer_drop`             |   ✅   | Accept cb enqueue failure posts defer drop             |
|  23 | `test_recv_during_closing_is_reset_not_processed`             |   ✅   | Recv during closing is reset not processed             |

</details>

---

## test_ocit - native_ocit_msg - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_message_layout_is_big_endian`           |   ✅   | Message layout is big endian           |
|   2 | `test_set_u16_builds_a_set_message`           |   ✅   | Set u16 builds a set message           |
|   3 | `test_build_parse_round_trip`                 |   ✅   | Build parse round trip                 |
|   4 | `test_get_carries_no_value`                   |   ✅   | Get carries no value                   |
|   5 | `test_parse_refuses_a_short_message`          |   ✅   | Parse refuses a short message          |
|   6 | `test_value_u16_is_gated_on_the_data_type`    |   ✅   | right length, wrong tag                |
|   7 | `test_octet_string_value_takes_the_remainder` |   ✅   | Octet string value takes the remainder |
|   8 | `test_build_bounds`                           |   ✅   | Build bounds                           |
|   9 | `test_codes_are_distinct`                     |   ✅   | Codes are distinct                     |

</details>

---

## test_opcua - native_opcua - ✅ 25 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_part6_builtin_type_encodings`             |   ✅   | sec 5.2.2.2: "All integer types shall be encoded as little-endian values where the least |
|   2 | `test_reader_inverts_the_writer`                |   ✅   | Reader inverts the writer                                                                |
|   3 | `test_bounds_latch`                             |   ✅   | Bounds latch                                                                             |
|   4 | `test_string_decoding`                          |   ✅   | String decoding                                                                          |
|   5 | `test_nodeid_encoding_forms`                    |   ✅   | Nodeid encoding forms                                                                    |
|   6 | `test_nodeid_round_trip`                        |   ✅   | Nodeid round trip                                                                        |
|   7 | `test_nodeid_non_numeric_forms_are_skipped`     |   ✅   | String form: 03, ns, Int32 length, bytes                                                 |
|   8 | `test_datetime_epoch`                           |   ✅   | one second later is exactly 10^7 ticks later                                             |
|   9 | `test_uacp_header`                              |   ✅   | the size field is little-endian like every other integer                                 |
|  10 | `test_hello_parse`                              |   ✅   | a MessageSize that disagrees with the octets delivered is not a complete message         |
|  11 | `test_ack_negotiation`                          |   ✅   | a client asking for less than the server can hold gets what it asked for                 |
|  12 | `test_error_message`                            |   ✅   | a null reason is the null String form, so the message is the header plus two UInt32      |
|  13 | `test_service_nodeids_match_the_registry`       |   ✅   | and the StatusCodes the services return                                                  |
|  14 | `test_open_secure_channel`                      |   ✅   | a buffer that cannot hold the whole response writes nothing                              |
|  15 | `test_open_secure_channel_rejects_wrong_frames` |   ✅   | rebuild with a CloseSecureChannel type id instead                                        |
|  16 | `test_msg_envelope`                             |   ✅   | Msg envelope                                                                             |
|  17 | `test_session_responses`                        |   ✅   | an unknown service draws a ServiceFault carrying BadServiceUnsupported                   |
|  18 | `test_variant_round_trip`                       |   ✅   | a null Variant is the encoding byte 0 alone                                              |
|  19 | `test_datavalue_mask`                           |   ✅   | a Bad status sets bit 1 as well                                                          |
|  20 | `test_read_request_and_response`                |   ✅   | the response carries one DataValue per captured node, in request order                   |
|  21 | `test_read_request_is_clamped`                  |   ✅   | Read request is clamped                                                                  |
|  22 | `test_browse_request_and_response`              |   ✅   | Part 3 sec 8.30 NodeClass and Part 5 the standard ReferenceType / VariableType NodeIds   |
|  23 | `test_write_request_and_response`               |   ✅   | the two StatusCodes are the last eight octets before the empty DiagnosticInfos array     |
|  24 | `test_resolver_registration`                    |   ✅   | the endpoint url the server advertises reaches the EndpointDescription encoder           |
|  25 | `test_qualifiedname_and_localizedtext`          |   ✅   | Qualifiedname and localizedtext                                                          |

</details>

---

## test_opcua_client - native_opcua_client - ✅ 31 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                            |
| --: | :-------------------------------------------- | :----: | :------------------------------------- |
|   1 | `test_hello_ack_roundtrip`                    |   ✅   | Hello ack roundtrip                    |
|   2 | `test_open_roundtrip`                         |   ✅   | Open roundtrip                         |
|   3 | `test_session_roundtrip`                      |   ✅   | Session roundtrip                      |
|   4 | `test_read_roundtrip`                         |   ✅   | Read roundtrip                         |
|   5 | `test_browse_roundtrip`                       |   ✅   | Browse roundtrip                       |
|   6 | `test_get_endpoints_roundtrip`                |   ✅   | Get endpoints roundtrip                |
|   7 | `test_service_fault_rejected_by_parsers`      |   ✅   | Service fault rejected by parsers      |
|   8 | `test_write_roundtrip`                        |   ✅   | Write roundtrip                        |
|   9 | `test_close_session_roundtrip`                |   ✅   | Close session roundtrip                |
|  10 | `test_close_channel_is_clo`                   |   ✅   | Close channel is clo                   |
|  11 | `test_seq_and_request_id_increment`           |   ✅   | Seq and request id increment           |
|  12 | `test_on_read_all_variant_types`              |   ✅   | On read all variant types              |
|  13 | `test_client_parsers_reject_fault`            |   ✅   | Client parsers reject fault            |
|  14 | `test_client_parsers_reject_malformed`        |   ✅   | Client parsers reject malformed        |
|  15 | `test_builder_overflow_guard`                 |   ✅   | Builder overflow guard                 |
|  16 | `test_on_read_unknown_variant_rejected`       |   ✅   | On read unknown variant rejected       |
|  17 | `test_response_parsers_reject_negative_count` |   ✅   | Response parsers reject negative count |
|  18 | `test_on_open_guards`                         |   ✅   | On open guards                         |
|  19 | `test_response_header_string_table_skip`      |   ✅   | Response header string table skip      |
|  20 | `test_browse_display_name_locale`             |   ✅   | Browse display name locale             |
|  21 | `test_builders_encode_null_strings`           |   ✅   | Builders encode null strings           |
|  22 | `test_on_ack_header_guards`                   |   ✅   | On ack header guards                   |
|  23 | `test_msg_envelope_guards`                    |   ✅   | Msg envelope guards                    |
|  24 | `test_on_open_envelope_and_result_guards`     |   ✅   | On open envelope and result guards     |
|  25 | `test_on_open_rejects_message_size_mismatch`  |   ✅   | On open rejects message size mismatch  |
|  26 | `test_parsers_reject_bad_service_result`      |   ✅   | Parsers reject bad service result      |
|  27 | `test_parsers_reject_truncated_body`          |   ✅   | Parsers reject truncated body          |
|  28 | `test_on_read_optional_fields_and_limits`     |   ✅   | On read optional fields and limits     |
|  29 | `test_on_write_limits_and_null_sink`          |   ✅   | On write limits and null sink          |
|  30 | `test_on_browse_limits_and_null_sink`         |   ✅   | On browse limits and null sink         |
|  31 | `test_on_browse_display_name_empty_mask`      |   ✅   | On browse display name empty mask      |

</details>

---

## test_openadr - native_openadr - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                         | Status | Description                                             |
| --: | :------------------------------------------- | :----: | :------------------------------------------------------ |
|   1 | `test_event_document_shape`                  |   ✅   | Event document shape                                    |
|   2 | `test_event_carries_every_interval_in_order` |   ✅   | Two interval objects are separated, never concatenated. |
|   3 | `test_report_document_shape`                 |   ✅   | Report document shape                                   |
|   4 | `test_rfc8259_string_escaping`               |   ✅   | No raw control octet survives into the document.        |
|   5 | `test_payload_value_formatting`              |   ✅   | Payload value formatting                                |
|   6 | `test_timestamps_are_plain_decimal_integers` |   ✅   | Timestamps are plain decimal integers                   |
|   7 | `test_overflow_reports_zero`                 |   ✅   | Overflow reports zero                                   |

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

## test_pca9685 - native_pca9685 - ✅ 20 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                                                                  |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_datasheet_prescale_example`                               |   ✅   | Datasheet prescale example                                                                   |
|   2 | `test_equation1_at_other_rates`                                 |   ✅   | Equation1 at other rates                                                                     |
|   3 | `test_prescale_is_clamped_to_the_register_range`                |   ✅   | a zero rate has no prescale, so it takes the slowest the register can name.                  |
|   4 | `test_datasheet_channel_register_addresses`                     |   ✅   | MODE1 / MODE2 / PRE_SCALE sit where Table 5 and Table 7 put them.                            |
|   5 | `test_pulse_width_to_count`                                     |   ✅   | Pulse width to count                                                                         |
|   6 | `test_pulse_width_saturates_at_the_twelve_bit_maximum`          |   ✅   | Pulse width saturates at the twelve bit maximum                                              |
|   7 | `test_set_pwm_bytes_layout`                                     |   ✅   | ON = 0, OFF = 0x0ABC: the 12-bit count splits into 0xBC low and 0x0A high.                   |
|   8 | `test_full_on_and_full_off_flags`                               |   ✅   | anything a caller sets above bit 12 belongs to no field and never reaches the reserved bits. |
|   9 | `test_set_pwm_bytes_refuses_bad_arguments`                      |   ✅   | Set pwm bytes refuses bad arguments                                                          |
|  10 | `test_rev4_model_reset_values`                                  |   ✅   | Rev4 model reset values                                                                      |
|  11 | `test_rev4_a_prescale_write_while_awake_is_dropped`             |   ✅   | asleep, the same write takes                                                                 |
|  12 | `test_rev4_begin_leaves_the_part_at_the_frequency_it_was_given` |   ✅   | Eq 2's own worked form at 50 Hz: round(25e6 / (4096 * 50)) - 1 = 121 = 0x79                  |
|  13 | `test_rev4_the_200hz_example_programs_the_reset_prescale`       |   ✅   | Rev4 the 200hz example programs the reset prescale                                           |
|  14 | `test_rev4_set_pwm_lands_in_the_channels_own_registers`         |   ✅   | and nothing else moved                                                                       |
|  15 | `test_rev4_each_channel_has_its_own_registers`                  |   ✅   | Rev4 each channel has its own registers                                                      |
|  16 | `test_rev4_a_servo_pulse_lands_as_its_fraction_of_the_period`   |   ✅   | the ends of a common servo travel, at the same frequency                                     |
|  17 | `test_a_servo_pulse_follows_the_frequency_begin_programmed`     |   ✅   | A servo pulse follows the frequency begin programmed                                         |
|  18 | `test_begin_sends_later_transfers_to_the_address_it_was_given`  |   ✅   | Begin sends later transfers to the address it was given                                      |
|  19 | `test_set_pwm_refuses_an_out_of_range_channel`                  |   ✅   | Set pwm refuses an out of range channel                                                      |
|  20 | `test_a_refused_transfer_fails_begin`                           |   ✅   | A refused transfer fails begin                                                               |

</details>

---

## test_pcap - native_pcap - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

|   # | Test                                                 | Status | Description                                                                                |
| --: | :--------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_a_no_link_reports_no_route`                    |   ✅   | No route means no egress address, even before any interface has one of its own.            |
|   2 | `test_b_station_bring_up_is_live`                    |   ✅   | RSSI is a signed dBm reading, negative for any real association.                           |
|   3 | `test_c_ssid_reads_back`                             |   ✅   | C ssid reads back                                                                          |
|   4 | `test_d_ssid_truncates_to_the_callers_cap`           |   ✅   | D ssid truncates to the callers cap                                                        |
|   5 | `test_e_ssid_is_capped_at_the_802_11_limit`          |   ✅   | E ssid is capped at the 802 11 limit                                                       |
|   6 | `test_f_station_mac_is_locally_administered_unicast` |   ✅   | F station mac is locally administered unicast                                              |
|   7 | `test_g_softap_has_its_own_address`                  |   ✅   | G softap has its own address                                                               |
|   8 | `test_h_wired_wins_the_route`                        |   ✅   | H wired wins the route                                                                     |
|   9 | `test_i_egress_mac_tracks_the_route`                 |   ✅   | I egress mac tracks the route                                                              |
|  10 | `test_j_ipv6_global_address`                         |   ✅   | RFC 3849 reserves 2001:db8::/32 for documentation, which is what the backend answers with. |
|  11 | `test_k_power_save_mode_round_trips`                 |   ✅   | 802.11-2020 11.7.6 selects a transmit power in dBm; a cap may be negative.                 |
|  12 | `test_l_busy_hold_refcount_gates_doze`               |   ✅   | An unbalanced release cannot drive the count negative or change the mode again.            |
|  13 | `test_m_power_applies_the_configured_mode`           |   ✅   | M power applies the configured mode                                                        |
|  14 | `test_n_monitor_mode_tunes_and_refuses_a_null_sink`  |   ✅   | N monitor mode tunes and refuses a null sink                                               |
|  15 | `test_o_power_save_names`                            |   ✅   | O power save names                                                                         |

</details>

---

## test_phy - native_l1_link - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                                |
| --: | :--------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_a_no_link_reports_no_route`                    |   ✅   | No route means no egress address, even before any interface has one of its own.            |
|   2 | `test_b_station_bring_up_is_live`                    |   ✅   | RSSI is a signed dBm reading, negative for any real association.                           |
|   3 | `test_c_ssid_reads_back`                             |   ✅   | C ssid reads back                                                                          |
|   4 | `test_d_ssid_truncates_to_the_callers_cap`           |   ✅   | D ssid truncates to the callers cap                                                        |
|   5 | `test_e_ssid_is_capped_at_the_802_11_limit`          |   ✅   | E ssid is capped at the 802 11 limit                                                       |
|   6 | `test_f_station_mac_is_locally_administered_unicast` |   ✅   | F station mac is locally administered unicast                                              |
|   7 | `test_g_softap_has_its_own_address`                  |   ✅   | G softap has its own address                                                               |
|   8 | `test_h_wired_wins_the_route`                        |   ✅   | H wired wins the route                                                                     |
|   9 | `test_i_egress_mac_tracks_the_route`                 |   ✅   | I egress mac tracks the route                                                              |
|  10 | `test_j_ipv6_global_address`                         |   ✅   | RFC 3849 reserves 2001:db8::/32 for documentation, which is what the backend answers with. |
|  11 | `test_k_power_save_mode_round_trips`                 |   ✅   | 802.11-2020 11.7.6 selects a transmit power in dBm; a cap may be negative.                 |
|  12 | `test_l_busy_hold_refcount_gates_doze`               |   ✅   | An unbalanced release cannot drive the count negative or change the mode again.            |
|  13 | `test_m_power_applies_the_configured_mode`           |   ✅   | M power applies the configured mode                                                        |
|  14 | `test_n_monitor_mode_tunes_and_refuses_a_null_sink`  |   ✅   | N monitor mode tunes and refuses a null sink                                               |
|  15 | `test_o_power_save_names`                            |   ✅   | O power save names                                                                         |

</details>

---

## test_iface - native_phy_iface - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

|   # | Test                                                       | Status | Description                                                             |
| --: | :--------------------------------------------------------- | :----: | :---------------------------------------------------------------------- |
|   1 | `test_the_high_water_mark_starts_at_zero`                  |   ✅   | The high water mark starts at zero                                      |
|   2 | `test_a_borrow_advances_the_usage_report`                  |   ✅   | A borrow advances the usage report                                      |
|   3 | `test_two_borrows_never_overlap`                           |   ✅   | Two borrows never overlap                                               |
|   4 | `test_the_requested_alignment_is_honored`                  |   ✅   | The requested alignment is honored                                      |
|   5 | `test_a_zero_size_borrow_is_not_a_failure`                 |   ✅   | A zero size borrow is not a failure                                     |
|   6 | `test_the_reset_empties_the_arena_and_reuses_the_base`     |   ✅   | The reset empties the arena and reuses the base                         |
|   7 | `test_exhaustion_fails_closed_without_moving_the_cursor`   |   ✅   | Exhaustion fails closed without moving the cursor                       |
|   8 | `test_a_request_wider_than_the_arena_is_refused`           |   ✅   | A request wider than the arena is refused                               |
|   9 | `test_alignment_padding_cannot_run_past_the_end`           |   ✅   | Alignment padding cannot run past the end                               |
|  10 | `test_the_high_water_mark_is_bounded_by_the_arena`         |   ✅   | The high water mark is bounded by the arena                             |
|  11 | `test_a_release_restores_the_usage_at_the_mark`            |   ✅   | A release restores the usage at the mark                                |
|  12 | `test_nested_marks_unwind_innermost_first`                 |   ✅   | Nested marks unwind innermost first                                     |
|  13 | `test_repeated_scopes_do_not_accumulate`                   |   ✅   | Repeated scopes do not accumulate                                       |
|  14 | `test_the_two_pools_are_disjoint_regions`                  |   ✅   | The two pools are disjoint regions                                      |
|  15 | `test_a_borrow_comes_from_the_callers_slot`                |   ✅   | A borrow comes from the callers slot                                    |
|  16 | `test_the_span_form_binds_the_length_to_the_borrow`        |   ✅   | The span form binds the length to the borrow                            |
|  17 | `test_an_over_budget_span_is_empty_not_null_with_capacity` |   ✅   | An over budget span is empty not null with capacity                     |
|  18 | `test_a_persistent_borrow_survives_the_reset`              |   ✅   | A transient borrow after the reset does not land on the persistent one. |
|  19 | `test_the_table_names_the_functions_it_claims_to`          |   ✅   | The table names the functions it claims to                              |

</details>

---

## test_plaintext - native_mmgr_plaintext - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                             |
| --: | :--------------------------------------------------------- | :----: | :---------------------------------------------------------------------- |
|   1 | `test_the_high_water_mark_starts_at_zero`                  |   ✅   | The high water mark starts at zero                                      |
|   2 | `test_a_borrow_advances_the_usage_report`                  |   ✅   | A borrow advances the usage report                                      |
|   3 | `test_two_borrows_never_overlap`                           |   ✅   | Two borrows never overlap                                               |
|   4 | `test_the_requested_alignment_is_honored`                  |   ✅   | The requested alignment is honored                                      |
|   5 | `test_a_zero_size_borrow_is_not_a_failure`                 |   ✅   | A zero size borrow is not a failure                                     |
|   6 | `test_the_reset_empties_the_arena_and_reuses_the_base`     |   ✅   | The reset empties the arena and reuses the base                         |
|   7 | `test_exhaustion_fails_closed_without_moving_the_cursor`   |   ✅   | Exhaustion fails closed without moving the cursor                       |
|   8 | `test_a_request_wider_than_the_arena_is_refused`           |   ✅   | A request wider than the arena is refused                               |
|   9 | `test_alignment_padding_cannot_run_past_the_end`           |   ✅   | Alignment padding cannot run past the end                               |
|  10 | `test_the_high_water_mark_is_bounded_by_the_arena`         |   ✅   | The high water mark is bounded by the arena                             |
|  11 | `test_a_release_restores_the_usage_at_the_mark`            |   ✅   | A release restores the usage at the mark                                |
|  12 | `test_nested_marks_unwind_innermost_first`                 |   ✅   | Nested marks unwind innermost first                                     |
|  13 | `test_repeated_scopes_do_not_accumulate`                   |   ✅   | Repeated scopes do not accumulate                                       |
|  14 | `test_the_two_pools_are_disjoint_regions`                  |   ✅   | The two pools are disjoint regions                                      |
|  15 | `test_a_borrow_comes_from_the_callers_slot`                |   ✅   | A borrow comes from the callers slot                                    |
|  16 | `test_the_span_form_binds_the_length_to_the_borrow`        |   ✅   | The span form binds the length to the borrow                            |
|  17 | `test_an_over_budget_span_is_empty_not_null_with_capacity` |   ✅   | An over budget span is empty not null with capacity                     |
|  18 | `test_a_persistent_borrow_survives_the_reset`              |   ✅   | A transient borrow after the reset does not land on the persistent one. |
|  19 | `test_the_table_names_the_functions_it_claims_to`          |   ✅   | The table names the functions it claims to                              |

</details>

---

## test_pmbus - native_pmbus - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                                   |
| --: | :------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_linear11_field_layout`                             |   ✅   | the exponent's own two's complement range, at both ends and either side of the sign bit       |
|   2 | `test_linear11_decode`                                   |   ✅   | Linear11 decode                                                                               |
|   3 | `test_linear11_out_of_range_is_refused`                  |   ✅   | Y = 1023 at N = 15 is 1023 * 32768 = 33 521 664, which past the micro scaling is ~3.35e13.    |
|   4 | `test_linear11_round_trip_keeps_the_mantissa_resolution` |   ✅   | exactly representable values come back exactly: 12 = 768 * 2^-6 is one of them. The encoded   |
|   5 | `test_vout_mode_selector`                                |   ✅   | Vout mode selector                                                                            |
|   6 | `test_vout_mode_exponent`                                |   ✅   | the Mode bits above it are not part of the exponent                                           |
|   7 | `test_ulinear16_decode`                                  |   ✅   | section 8.1.1 restricts ULINEAR16 to positive values, so the mantissa is unsigned throughout. |
|   8 | `test_ulinear16_round_trip`                              |   ✅   | 1.0 V at 2^-9 is exactly mantissa 512                                                         |
|   9 | `test_direct_format`                                     |   ✅   | Y is two's complement, so a word above 0x7FFF is a negative reading                           |
|  10 | `test_status_byte_bits`                                  |   ✅   | the eight are distinct single bits covering the byte                                          |
|  11 | `test_command_codes`                                     |   ✅   | Command codes                                                                                 |

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

## test_plaintext - native_pool_workers - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                                             |
| --: | :--------------------------------------------------------- | :----: | :---------------------------------------------------------------------- |
|   1 | `test_the_high_water_mark_starts_at_zero`                  |   ✅   | The high water mark starts at zero                                      |
|   2 | `test_a_borrow_advances_the_usage_report`                  |   ✅   | A borrow advances the usage report                                      |
|   3 | `test_two_borrows_never_overlap`                           |   ✅   | Two borrows never overlap                                               |
|   4 | `test_the_requested_alignment_is_honored`                  |   ✅   | The requested alignment is honored                                      |
|   5 | `test_a_zero_size_borrow_is_not_a_failure`                 |   ✅   | A zero size borrow is not a failure                                     |
|   6 | `test_the_reset_empties_the_arena_and_reuses_the_base`     |   ✅   | The reset empties the arena and reuses the base                         |
|   7 | `test_exhaustion_fails_closed_without_moving_the_cursor`   |   ✅   | Exhaustion fails closed without moving the cursor                       |
|   8 | `test_a_request_wider_than_the_arena_is_refused`           |   ✅   | A request wider than the arena is refused                               |
|   9 | `test_alignment_padding_cannot_run_past_the_end`           |   ✅   | Alignment padding cannot run past the end                               |
|  10 | `test_the_high_water_mark_is_bounded_by_the_arena`         |   ✅   | The high water mark is bounded by the arena                             |
|  11 | `test_a_release_restores_the_usage_at_the_mark`            |   ✅   | A release restores the usage at the mark                                |
|  12 | `test_nested_marks_unwind_innermost_first`                 |   ✅   | Nested marks unwind innermost first                                     |
|  13 | `test_repeated_scopes_do_not_accumulate`                   |   ✅   | Repeated scopes do not accumulate                                       |
|  14 | `test_the_two_pools_are_disjoint_regions`                  |   ✅   | The two pools are disjoint regions                                      |
|  15 | `test_a_borrow_comes_from_the_callers_slot`                |   ✅   | A borrow comes from the callers slot                                    |
|  16 | `test_the_span_form_binds_the_length_to_the_borrow`        |   ✅   | The span form binds the length to the borrow                            |
|  17 | `test_an_over_budget_span_is_empty_not_null_with_capacity` |   ✅   | An over budget span is empty not null with capacity                     |
|  18 | `test_a_persistent_borrow_survives_the_reset`              |   ✅   | A transient borrow after the reset does not land on the persistent one. |
|  19 | `test_the_table_names_the_functions_it_claims_to`          |   ✅   | The table names the functions it claims to                              |

</details>

---

## test_secure_pool - native_pool_workers - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_release_wipes_before_the_bytes_are_available_again` |   ✅   | Release wipes before the bytes are available again |
|   2 | `test_reset_wipes_every_live_borrow`                      |   ✅   | Reset wipes every live borrow                      |
|   3 | `test_a_scope_guard_wipes_on_every_exit_path`             |   ✅   | A scope guard wipes on every exit path             |
|   4 | `test_nested_scopes_reclaim_lifo`                         |   ✅   | Nested scopes reclaim lifo                         |
|   5 | `test_the_two_pools_are_disjoint_regions`                 |   ✅   | The borrowing slot is the calling worker's own.    |
|   6 | `test_a_pointer_from_neither_pool_belongs_to_neither`     |   ✅   | A pointer from neither pool belongs to neither     |
|   7 | `test_one_past_the_pool_is_not_owned`                     |   ✅   | One past the pool is not owned                     |
|   8 | `test_a_persistent_borrow_outlives_every_release`         |   ✅   | A persistent borrow outlives every release         |
|   9 | `test_high_water_records_peak_demand`                     |   ✅   | High water records peak demand                     |
|  10 | `test_an_over_budget_borrow_fails_closed`                 |   ✅   | An over budget borrow fails closed                 |
|  11 | `test_the_table_is_wired_to_the_named_functions`          |   ✅   | The table is wired to the named functions          |
|  12 | `test_the_pool_works_through_the_table`                   |   ✅   | The pool works through the table                   |

</details>

---

## test_power_mgmt - native_power_mgmt - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                       |
| --: | :------------------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_the_throttle_engages_at_the_hot_threshold`         |   ✅   | The throttle engages at the hot threshold         |
|   2 | `test_the_throttle_releases_at_the_cool_threshold`       |   ✅   | The throttle releases at the cool threshold       |
|   3 | `test_the_band_between_the_thresholds_retains_state`     |   ✅   | The band between the thresholds retains state     |
|   4 | `test_a_feedback_ramp_crosses_once_per_direction`        |   ✅   | A feedback ramp crosses once per direction        |
|   5 | `test_no_sensor_holds_a_throttle_already_held`           |   ✅   | No sensor holds a throttle already held           |
|   6 | `test_no_sensor_never_engages_a_throttle`                |   ✅   | No sensor never engages a throttle                |
|   7 | `test_the_load_picks_the_rail`                           |   ✅   | The load picks the rail                           |
|   8 | `test_a_load_over_a_hundred_decides_as_a_hundred`        |   ✅   | A load over a hundred decides as a hundred        |
|   9 | `test_a_brownout_boot_holds_the_floor_for_its_window`    |   ✅   | A brownout boot holds the floor for its window    |
|  10 | `test_a_clean_boot_never_recovers`                       |   ✅   | A clean boot never recovers                       |
|  11 | `test_either_hold_forces_the_floor`                      |   ✅   | Either hold forces the floor                      |
|  12 | `test_the_decision_is_a_pure_function_of_its_arguments`  |   ✅   | The decision is a pure function of its arguments  |
|  13 | `test_a_null_config_decides_nothing`                     |   ✅   | A null config decides nothing                     |
|  14 | `test_the_defaults_carry_each_build_flag`                |   ✅   | The defaults carry each build flag                |
|  15 | `test_defaults_refuse_a_null_destination`                |   ✅   | Defaults refuse a null destination                |
|  16 | `test_every_report_is_a_json_text`                       |   ✅   | Every report is a json text                       |
|  17 | `test_the_flags_use_the_literal_names_the_rfc_publishes` |   ✅   | The flags use the literal names the rfc publishes |
|  18 | `test_a_reading_survives_the_report`                     |   ✅   | A reading survives the report                     |
|  19 | `test_the_clock_survives_the_report`                     |   ✅   | The clock survives the report                     |
|  20 | `test_the_sentinel_is_never_reported_as_a_reading`       |   ✅   | The sentinel is never reported as a reading       |
|  21 | `test_a_short_buffer_yields_nothing_not_a_prefix`        |   ✅   | A short buffer yields nothing not a prefix        |
|  22 | `test_the_report_refuses_what_it_cannot_write`           |   ✅   | The report refuses what it cannot write           |

</details>

---

## test_powerlink - native_powerlink - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                       |
| --: | :--------------------------------------------- | :----: | :------------------------------------------------ |
|   1 | `test_epsg_message_type_ids`                   |   ✅   | Epsg message type ids                             |
|   2 | `test_soc_frame`                               |   ✅   | Soc frame                                         |
|   3 | `test_preq_frame`                              |   ✅   | Preq frame                                        |
|   4 | `test_pres_frame`                              |   ✅   | Pres frame                                        |
|   5 | `test_soa_frame`                               |   ✅   | Soa frame                                         |
|   6 | `test_asnd_frame`                              |   ✅   | Asnd frame                                        |
|   7 | `test_build_parse_round_trip`                  |   ✅   | Build parse round trip                            |
|   8 | `test_parse_refuses_an_undefined_message_type` |   ✅   | Parse refuses an undefined message type           |
|   9 | `test_parse_refuses_a_short_frame`             |   ✅   | Parse refuses a short frame                       |
|  10 | `test_build_refuses_bad_arguments`             |   ✅   | the convenience builders inherit the same refusal |
|  11 | `test_isochronous_cycle_sequence`              |   ✅   | Isochronous cycle sequence                        |

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

## test_pqc_mlkem - native_pqc - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                          |
| --: | :---------------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_fips203_encoding_sizes`                         |   ✅   | Fips203 encoding sizes                                                               |
|   2 | `test_acvp_keygen`                                    |   ✅   | Acvp keygen                                                                          |
|   3 | `test_decapsulation_key_embeds_the_encapsulation_key` |   ✅   | dk = dk_PKE (384k = 1152) \|\| ek (1184) \|\| H(ek) (32) \|\| z (32)                 |
|   4 | `test_acvp_encaps`                                    |   ✅   | Acvp encaps                                                                          |
|   5 | `test_acvp_decaps`                                    |   ✅   | Acvp decaps                                                                          |
|   6 | `test_encaps_decaps_agree`                            |   ✅   | Encaps decaps agree                                                                  |
|   7 | `test_tampered_ciphertext_implicitly_rejects`         |   ✅   | Tampered ciphertext implicitly rejects                                               |
|   8 | `test_encaps_refuses_a_malformed_encapsulation_key`   |   ✅   | Encaps refuses a malformed encapsulation key                                         |
|   9 | `test_seeds_determine_the_key_pair`                   |   ✅   | z only feeds the implicit-reject value, so ek is unchanged and only dk's tail moves. |

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

## test_pqc_mlkem - native_mlkem_kat - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                          |
| --: | :---------------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_fips203_encoding_sizes`                         |   ✅   | Fips203 encoding sizes                                                               |
|   2 | `test_acvp_keygen`                                    |   ✅   | Acvp keygen                                                                          |
|   3 | `test_decapsulation_key_embeds_the_encapsulation_key` |   ✅   | dk = dk_PKE (384k = 1152) \|\| ek (1184) \|\| H(ek) (32) \|\| z (32)                 |
|   4 | `test_acvp_encaps`                                    |   ✅   | Acvp encaps                                                                          |
|   5 | `test_acvp_decaps`                                    |   ✅   | Acvp decaps                                                                          |
|   6 | `test_encaps_decaps_agree`                            |   ✅   | Encaps decaps agree                                                                  |
|   7 | `test_tampered_ciphertext_implicitly_rejects`         |   ✅   | Tampered ciphertext implicitly rejects                                               |
|   8 | `test_encaps_refuses_a_malformed_encapsulation_key`   |   ✅   | Encaps refuses a malformed encapsulation key                                         |
|   9 | `test_seeds_determine_the_key_pair`                   |   ✅   | z only feeds the implicit-reject value, so ek is unchanged and only dk's tail moves. |

</details>

---

## test_pqc_sntrup761 - native_sntrup761_kat - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                |
| --: | :-------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_openssh_interop_decaps_vector`          |   ✅   | Openssh interop decaps vector                                                              |
|   2 | `test_encoding_sizes_match_the_vector`        |   ✅   | Encoding sizes match the vector                                                            |
|   3 | `test_round_trip_agrees_over_many_keypairs`   |   ✅   | Round trip agrees over many keypairs                                                       |
|   4 | `test_encaps_is_randomized`                   |   ✅   | Encaps is randomized                                                                       |
|   5 | `test_secret_key_embeds_the_public_key`       |   ✅   | ...and encapsulating against the embedded copy is the same operation as against pk itself. |
|   6 | `test_tampered_ciphertext_implicitly_rejects` |   ✅   | Tampered ciphertext implicitly rejects                                                     |
|   7 | `test_keygen_retries_a_noninvertible_g`       |   ✅   | Keygen retries a noninvertible g                                                           |

</details>

---

## test_preempt_queue - native_preempt_queue - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_primitives - native_primitives - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                          |
| --: | :------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_c11_g_selects_the_style_by_the_exponent`           |   ✅   | C11 g selects the style by the exponent                                              |
|   2 | `test_g_threshold_moves_with_the_precision`              |   ✅   | A precision of zero is taken as 1, so it renders the same as P = 1.                  |
|   3 | `test_g_rounds_to_the_significant_digits`                |   ✅   | G rounds to the significant digits                                                   |
|   4 | `test_g_strips_trailing_zeros_and_a_bare_point`          |   ✅   | G strips trailing zeros and a bare point                                             |
|   5 | `test_g_exponent_carries_a_sign_and_two_digits`          |   ✅   | G exponent carries a sign and two digits                                             |
|   6 | `test_g_renders_the_sign_from_the_encoding`              |   ✅   | G renders the sign from the encoding                                                 |
|   7 | `test_c11_f_rounds_the_stored_binary_value`              |   ✅   | C11 f rounds the stored binary value                                                 |
|   8 | `test_an_exact_midpoint_rounds_to_the_even_digit`        |   ✅   | An exact midpoint rounds to the even digit                                           |
|   9 | `test_f_emits_exactly_the_requested_decimals`            |   ✅   | F emits exactly the requested decimals                                               |
|  10 | `test_f_always_leads_with_a_digit`                       |   ✅   | F always leads with a digit                                                          |
|  11 | `test_f_above_the_64_bit_range_falls_back_to_the_g_form` |   ✅   | Just below the boundary it is still an exact expansion: 2^63 is 9223372036854775808. |
|  12 | `test_non_finite_values_are_named`                       |   ✅   | Non finite values are named                                                          |
|  13 | `test_a_number_that_does_not_fit_latches`                |   ✅   | A number that does not fit latches                                                   |
|  14 | `test_the_decimal_count_is_clamped`                      |   ✅   | 0.5 is exact, so every decimal past the first is a zero however many are asked for.  |

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

## test_primitives - native_mmgr_primitives - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                          |
| --: | :------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_c11_g_selects_the_style_by_the_exponent`           |   ✅   | C11 g selects the style by the exponent                                              |
|   2 | `test_g_threshold_moves_with_the_precision`              |   ✅   | A precision of zero is taken as 1, so it renders the same as P = 1.                  |
|   3 | `test_g_rounds_to_the_significant_digits`                |   ✅   | G rounds to the significant digits                                                   |
|   4 | `test_g_strips_trailing_zeros_and_a_bare_point`          |   ✅   | G strips trailing zeros and a bare point                                             |
|   5 | `test_g_exponent_carries_a_sign_and_two_digits`          |   ✅   | G exponent carries a sign and two digits                                             |
|   6 | `test_g_renders_the_sign_from_the_encoding`              |   ✅   | G renders the sign from the encoding                                                 |
|   7 | `test_c11_f_rounds_the_stored_binary_value`              |   ✅   | C11 f rounds the stored binary value                                                 |
|   8 | `test_an_exact_midpoint_rounds_to_the_even_digit`        |   ✅   | An exact midpoint rounds to the even digit                                           |
|   9 | `test_f_emits_exactly_the_requested_decimals`            |   ✅   | F emits exactly the requested decimals                                               |
|  10 | `test_f_always_leads_with_a_digit`                       |   ✅   | F always leads with a digit                                                          |
|  11 | `test_f_above_the_64_bit_range_falls_back_to_the_g_form` |   ✅   | Just below the boundary it is still an exact expansion: 2^63 is 9223372036854775808. |
|  12 | `test_non_finite_values_are_named`                       |   ✅   | Non finite values are named                                                          |
|  13 | `test_a_number_that_does_not_fit_latches`                |   ✅   | A number that does not fit latches                                                   |
|  14 | `test_the_decimal_count_is_clamped`                      |   ✅   | 0.5 is exact, so every decimal past the first is a zero however many are asked for.  |

</details>

---

## test_profibus - native_profibus - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                      |
| --: | :------------------------------------------------------ | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_delimiters_and_their_hamming_distance`            |   ✅   | Delimiters and their hamming distance                                            |
|   2 | `test_frame_control_function_codes`                     |   ✅   | Frame control function codes                                                     |
|   3 | `test_fcs_is_the_arithmetic_sum_with_carries_discarded` |   ✅   | Fcs is the arithmetic sum with carries discarded                                 |
|   4 | `test_sd1_telegram`                                     |   ✅   | Sd1 telegram                                                                     |
|   5 | `test_sd2_telegram`                                     |   ✅   | Sd2 telegram                                                                     |
|   6 | `test_sd3_telegram`                                     |   ✅   | Sd3 telegram                                                                     |
|   7 | `test_frame_control_macros_build_the_derived_telegrams` |   ✅   | Frame control macros build the derived telegrams                                 |
|   8 | `test_sd2_length_field_across_the_range`                |   ✅   | Sd2 length field across the range                                                |
|   9 | `test_sd2_length_field_minimum_is_four`                 |   ✅   | SD2 LE=3 LEr=3 SD2 DA=0x7F SA=0x02 FC=0x49, FCS = 127 + 2 + 73 = 202 = 0xCA, ED. |
|  10 | `test_parse_refuses_a_malformed_frame`                  |   ✅   | Parse refuses a malformed frame                                                  |
|  11 | `test_parse_refuses_a_truncated_telegram`               |   ✅   | Parse refuses a truncated telegram                                               |
|  12 | `test_builders_refuse_a_short_buffer`                   |   ✅   | Builders refuse a short buffer                                                   |
|  13 | `test_address_octets_round_trip`                        |   ✅   | Address octets round trip                                                        |

</details>

---

## test_profinet - native_profinet - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                | Status | Description                                                                     |
| --: | :---------------------------------- | :----: | :------------------------------------------------------------------------------ |
|   1 | `test_dcp_constants`                |   ✅   | Dcp constants                                                                   |
|   2 | `test_dcp_header_layout`            |   ✅   | Dcp header layout                                                               |
|   3 | `test_dcp_header_field_widths`      |   ✅   | Dcp header field widths                                                         |
|   4 | `test_dcp_block_layout_and_padding` |   ✅   | "et200sp" is seven octets, so the block declares 7 and occupies 4 + 7 + 1 = 12  |
|   5 | `test_dcp_walk_steps_over_the_pad`  |   ✅   | Dcp walk steps over the pad                                                     |
|   6 | `test_identify_response_frame`      |   ✅   | Identify response frame                                                         |
|   7 | `test_dcp_walk_refuses_an_overrun`  |   ✅   | a trailing fragment shorter than a block header is simply the end of the blocks |
|   8 | `test_bounds_refusals`              |   ✅   | Bounds refusals                                                                 |

</details>

---

## test_promisc - native_promisc_dot11 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                           |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_ieee80211_address_fields_by_ds_bits`       |   ✅   | To DS 0, From DS 0 (IBSS or management): A1 = DA, A2 = SA, A3 = BSSID.                |
|   2 | `test_ieee80211_frame_control_type_and_subtype`  |   ✅   | The Protocol Version bits are octet 0 bits 0-1 and belong to neither field.           |
|   3 | `test_ieee80211_sequence_number`                 |   ✅   | Field value 0x1237: fragment 7, sequence number 0x123.                                |
|   4 | `test_ieee80211_header_length`                   |   ✅   | Management frames carry no QoS Control even at a Subtype with bit 3 set.              |
|   5 | `test_ieee80211_control_frame`                   |   ✅   | Ieee80211 control frame                                                               |
|   6 | `test_ieee80211_protected_frame_bit`             |   ✅   | Ieee80211 protected frame bit                                                         |
|   7 | `test_parse_refuses_a_short_frame`               |   ✅   | Parse refuses a short frame                                                           |
|   8 | `test_pcap_global_header_declares_ieee80211`     |   ✅   | Pcap global header declares ieee80211                                                 |
|   9 | `test_pcap_record_header`                        |   ✅   | Pcap record header                                                                    |
|  10 | `test_pcap_headers_fail_closed`                  |   ✅   | Pcap headers fail closed                                                              |
|  11 | `test_capture_refuses_a_null_sink`               |   ✅   | Capture refuses a null sink                                                           |
|  12 | `test_capture_delivers_frames_through_the_radio` |   ✅   | A retune is what the next frame arrives on; 0 means "whatever the radio is tuned to". |

</details>

---

## test_protobuf - native_protobuf_wire - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                                    |
| --: | :---------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_encoding_document_worked_examples`              |   ✅   | "Message Structure": `message Test1 { optional int32 a = 1; }` with a = 150 is `08 96 01`.     |
|   2 | `test_base_128_varint`                                |   ✅   | Base 128 varint                                                                                |
|   3 | `test_tag_formula_and_wire_type_ids`                  |   ✅   | Tag formula and wire type ids                                                                  |
|   4 | `test_zigzag_table`                                   |   ✅   | The encoder writes the ZigZag varint behind a tag; the bare varint is what the table names.    |
|   5 | `test_int64_is_two_s_complement_and_sint64_is_zigzag` |   ✅   | -1 as a uint64 is 0xFFFFFFFFFFFFFFFF, ten varint octets behind a one-octet tag.                |
|   6 | `test_fixed_width_and_bool_payloads`                  |   ✅   | fixed32 of 0x01020304 is `0d 04 03 02 01`: tag (1<<3)\|5, then four little-endian octets.      |
|   7 | `test_float_and_double_bit_patterns`                  |   ✅   | The two bit readers name the same values back, compared as bit patterns so the check is exact. |
|   8 | `test_packed_repeated_field`                          |   ✅   | Each element's varint, from the Base 128 definition:                                           |
|   9 | `test_group_and_unassigned_wire_types_are_refused`    |   ✅   | Group and unassigned wire types are refused                                                    |
|  10 | `test_truncated_records_are_refused`                  |   ✅   | A LEN record claiming five octets with three behind it.                                        |
|  11 | `test_writer_fails_closed`                            |   ✅   | A row with no buffer starts poisoned.                                                          |
|  12 | `test_a_message_walks_record_by_record`               |   ✅   | An open seated past the start begins at that offset, clamped to the length.                    |

</details>

---

## test_protomem - native_protomem - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                      |
| --: | :------------------------------------------------------ | :----: | :------------------------------- |
|   1 | `test_c11_cmp_orders_bytes_as_unsigned`                 |   ✅   | C11 cmp orders bytes as unsigned |
|   2 | `test_cmp_of_equal_and_of_zero_length`                  |   ✅   | Cmp of equal and of zero length  |
|   3 | `test_cmp_does_not_stop_at_a_nul`                       |   ✅   | Cmp does not stop at a nul       |
|   4 | `test_cpy_moves_exactly_n_bytes`                        |   ✅   | Cpy moves exactly n bytes        |
|   5 | `test_cpy_at_every_offset_pair`                         |   ✅   | Cpy at every offset pair         |
|   6 | `test_cpy_of_zero_bytes_writes_nothing`                 |   ✅   | Cpy of zero bytes writes nothing |
|   7 | `test_move_is_correct_under_overlap_in_both_directions` |   ✅   | destination above the source     |
|   8 | `test_move_onto_itself_changes_nothing`                 |   ✅   | Move onto itself changes nothing |
|   9 | `test_move_without_overlap`                             |   ✅   | Move without overlap             |
|  10 | `test_chr_finds_the_first_occurrence`                   |   ✅   | Chr finds the first occurrence   |
|  11 | `test_chr_does_not_stop_at_a_nul`                       |   ✅   | Chr does not stop at a nul       |
|  12 | `test_chr_finds_a_high_byte`                            |   ✅   | Chr finds a high byte            |
|  13 | `test_set_fills_exactly_n_bytes`                        |   ✅   | Set fills exactly n bytes        |
|  14 | `test_set_writes_a_byte_not_a_word`                     |   ✅   | Set writes a byte not a word     |
|  15 | `test_zero_clears_exactly_n_bytes`                      |   ✅   | Zero clears exactly n bytes      |

</details>

---

## test_protomem - native_mmgr_protomem - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                      |
| --: | :------------------------------------------------------ | :----: | :------------------------------- |
|   1 | `test_c11_cmp_orders_bytes_as_unsigned`                 |   ✅   | C11 cmp orders bytes as unsigned |
|   2 | `test_cmp_of_equal_and_of_zero_length`                  |   ✅   | Cmp of equal and of zero length  |
|   3 | `test_cmp_does_not_stop_at_a_nul`                       |   ✅   | Cmp does not stop at a nul       |
|   4 | `test_cpy_moves_exactly_n_bytes`                        |   ✅   | Cpy moves exactly n bytes        |
|   5 | `test_cpy_at_every_offset_pair`                         |   ✅   | Cpy at every offset pair         |
|   6 | `test_cpy_of_zero_bytes_writes_nothing`                 |   ✅   | Cpy of zero bytes writes nothing |
|   7 | `test_move_is_correct_under_overlap_in_both_directions` |   ✅   | destination above the source     |
|   8 | `test_move_onto_itself_changes_nothing`                 |   ✅   | Move onto itself changes nothing |
|   9 | `test_move_without_overlap`                             |   ✅   | Move without overlap             |
|  10 | `test_chr_finds_the_first_occurrence`                   |   ✅   | Chr finds the first occurrence   |
|  11 | `test_chr_does_not_stop_at_a_nul`                       |   ✅   | Chr does not stop at a nul       |
|  12 | `test_chr_finds_a_high_byte`                            |   ✅   | Chr finds a high byte            |
|  13 | `test_set_fills_exactly_n_bytes`                        |   ✅   | Set fills exactly n bytes        |
|  14 | `test_set_writes_a_byte_not_a_word`                     |   ✅   | Set writes a byte not a word     |
|  15 | `test_zero_clears_exactly_n_bytes`                      |   ✅   | Zero clears exactly n bytes      |

</details>

---

## test_protostr - native_protostr - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                           |
| --: | :------------------------------------------------------ | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_len_is_the_bounded_strnlen`                       |   ✅   | Len is the bounded strnlen                                                            |
|   2 | `test_len_stops_at_the_terminator_whatever_the_cap`     |   ✅   | Len stops at the terminator whatever the cap                                          |
|   3 | `test_len_at_every_length`                              |   ✅   | Len at every length                                                                   |
|   4 | `test_diff_names_the_first_differing_index`             |   ✅   | Diff names the first differing index                                                  |
|   5 | `test_diff_at_every_position`                           |   ✅   | Diff at every position                                                                |
|   6 | `test_eq_requires_the_terminator_before_the_difference` |   ✅   | Eq requires the terminator before the difference                                      |
|   7 | `test_starts_reads_the_tie_as_a_match`                  |   ✅   | Starts reads the tie as a match                                                       |
|   8 | `test_ci_folds_only_ascii_letters`                      |   ✅   | Every letter, both cases, does fold.                                                  |
|   9 | `test_ci_over_a_run_longer_than_a_word`                 |   ✅   | Ci over a run longer than a word                                                      |
|  10 | `test_find_returns_the_first_occurrence`                |   ✅   | Repeated anchor bytes: the first full match wins, not the first anchor.               |
|  11 | `test_find_does_not_read_past_the_terminator`           |   ✅   | Find does not read past the terminator                                                |
|  12 | `test_find_is_bounded_by_read_cap`                      |   ✅   | Find is bounded by read cap                                                           |
|  13 | `test_has_agrees_with_find`                             |   ✅   | Has agrees with find                                                                  |
|  14 | `test_copy_always_terminates_within_the_destination`    |   ✅   | An exact fit: seven bytes plus the NUL.                                               |
|  15 | `test_copy_with_zero_capacity_writes_nothing`           |   ✅   | A capacity of one holds the terminator and nothing else.                              |
|  16 | `test_ws_is_the_c11_white_space_set`                    |   ✅   | Ws is the c11 white space set                                                         |
|  17 | `test_digit_is_the_ten_decimal_digits`                  |   ✅   | Digit is the ten decimal digits                                                       |
|  18 | `test_to_long_follows_the_strtol_endptr_contract`       |   ✅   | A sign with no digits after it is also an empty subject sequence.                     |
|  19 | `test_to_ulong_takes_plus_and_not_minus`                |   ✅   | To ulong takes plus and not minus                                                     |
|  20 | `test_to_double_parses_the_strtod_subject_sequence`     |   ✅   | Values whose decimal digits are each a dyadic fraction, so the parse is exact and the |
|  21 | `test_to_double_clamps_a_runaway_exponent`              |   ✅   | To double clamps a runaway exponent                                                   |
|  22 | `test_to_float_narrows_a_double_parse`                  |   ✅   | To float narrows a double parse                                                       |

</details>

---

## test_protostr - native_mmgr_protostr - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                           |
| --: | :------------------------------------------------------ | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_len_is_the_bounded_strnlen`                       |   ✅   | Len is the bounded strnlen                                                            |
|   2 | `test_len_stops_at_the_terminator_whatever_the_cap`     |   ✅   | Len stops at the terminator whatever the cap                                          |
|   3 | `test_len_at_every_length`                              |   ✅   | Len at every length                                                                   |
|   4 | `test_diff_names_the_first_differing_index`             |   ✅   | Diff names the first differing index                                                  |
|   5 | `test_diff_at_every_position`                           |   ✅   | Diff at every position                                                                |
|   6 | `test_eq_requires_the_terminator_before_the_difference` |   ✅   | Eq requires the terminator before the difference                                      |
|   7 | `test_starts_reads_the_tie_as_a_match`                  |   ✅   | Starts reads the tie as a match                                                       |
|   8 | `test_ci_folds_only_ascii_letters`                      |   ✅   | Every letter, both cases, does fold.                                                  |
|   9 | `test_ci_over_a_run_longer_than_a_word`                 |   ✅   | Ci over a run longer than a word                                                      |
|  10 | `test_find_returns_the_first_occurrence`                |   ✅   | Repeated anchor bytes: the first full match wins, not the first anchor.               |
|  11 | `test_find_does_not_read_past_the_terminator`           |   ✅   | Find does not read past the terminator                                                |
|  12 | `test_find_is_bounded_by_read_cap`                      |   ✅   | Find is bounded by read cap                                                           |
|  13 | `test_has_agrees_with_find`                             |   ✅   | Has agrees with find                                                                  |
|  14 | `test_copy_always_terminates_within_the_destination`    |   ✅   | An exact fit: seven bytes plus the NUL.                                               |
|  15 | `test_copy_with_zero_capacity_writes_nothing`           |   ✅   | A capacity of one holds the terminator and nothing else.                              |
|  16 | `test_ws_is_the_c11_white_space_set`                    |   ✅   | Ws is the c11 white space set                                                         |
|  17 | `test_digit_is_the_ten_decimal_digits`                  |   ✅   | Digit is the ten decimal digits                                                       |
|  18 | `test_to_long_follows_the_strtol_endptr_contract`       |   ✅   | A sign with no digits after it is also an empty subject sequence.                     |
|  19 | `test_to_ulong_takes_plus_and_not_minus`                |   ✅   | To ulong takes plus and not minus                                                     |
|  20 | `test_to_double_parses_the_strtod_subject_sequence`     |   ✅   | Values whose decimal digits are each a dyadic fraction, so the parse is exact and the |
|  21 | `test_to_double_clamps_a_runaway_exponent`              |   ✅   | To double clamps a runaway exponent                                                   |
|  22 | `test_to_float_narrows_a_double_parse`                  |   ✅   | To float narrows a double parse                                                       |

</details>

---

## test_proxy_protocol - native_proxy_protocol - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                           | Status | Description                                             |
| --: | :------------------------------------------------------------- | :----: | :------------------------------------------------------ |
|   1 | `test_v1_published_example_line`                               |   ✅   | V1 published example line                               |
|   2 | `test_v1_widest_tcp4_line_is_56_octets`                        |   ✅   | V1 widest tcp4 line is 56 octets                        |
|   3 | `test_v1_unknown_short_form_is_15_octets`                      |   ✅   | V1 unknown short form is 15 octets                      |
|   4 | `test_v1_unknown_ignores_everything_before_the_crlf`           |   ✅   | V1 unknown ignores everything before the crlf           |
|   5 | `test_v1_requires_a_complete_crlf`                             |   ✅   | V1 requires a complete crlf                             |
|   6 | `test_v1_line_is_bounded_at_107_octets`                        |   ✅   | V1 line is bounded at 107 octets                        |
|   7 | `test_v1_fields_outside_the_published_ranges_are_discarded`    |   ✅   | V1 fields outside the published ranges are discarded    |
|   8 | `test_v1_heading_zeroes_are_discarded`                         |   ✅   | V1 heading zeroes are discarded                         |
|   9 | `test_v1_an_unlisted_family_token_is_discarded`                |   ✅   | V1 an unlisted family token is discarded                |
|  10 | `test_v1_tcp6_is_allowed_but_carries_no_ipv4`                  |   ✅   | V1 tcp6 is allowed but carries no ipv4                  |
|  11 | `test_v1_the_published_range_endpoints_decode`                 |   ✅   | V1 the published range endpoints decode                 |
|  12 | `test_v2_published_layout`                                     |   ✅   | V2 published layout                                     |
|  13 | `test_v2_round_trip`                                           |   ✅   | V2 round trip                                           |
|  14 | `test_v2_local_command_yields_no_address`                      |   ✅   | V2 local command yields no address                      |
|  15 | `test_v2_a_valid_combination_it_does_not_implement_is_skipped` |   ✅   | V2 a valid combination it does not implement is skipped |
|  16 | `test_v2_only_version_2_is_accepted`                           |   ✅   | V2 only version 2 is accepted                           |
|  17 | `test_v2_an_unassigned_command_is_dropped`                     |   ✅   | V2 an unassigned command is dropped                     |
|  18 | `test_v2_an_unassigned_address_family_is_rejected`             |   ✅   | V2 an unassigned address family is rejected             |
|  19 | `test_partial_headers_are_refused`                             |   ✅   | Partial headers are refused                             |
|  20 | `test_a_stream_without_a_proxy_header_is_refused`              |   ✅   | A stream without a proxy header is refused              |
|  21 | `test_builders_fail_closed_on_a_short_buffer`                  |   ✅   | Builders fail closed on a short buffer                  |
|  22 | `test_null_arguments_are_refused`                              |   ✅   | Null arguments are refused                              |

</details>

---

## test_psram_pool - native_psram_pool - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                                                                           |
| --: | :--------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_dram_reserve_is_never_spent`             |   ✅   | A reserve of 0 places the whole free heap and nothing past it.                        |
|   2 | `test_a_zero_size_request_is_refused`          |   ✅   | A zero size request is refused                                                        |
|   3 | `test_dma_required_never_leaves_dram`          |   ✅   | 8192 is at/above the threshold, which without the DMA requirement would prefer PSRAM. |
|   4 | `test_at_or_above_the_threshold_prefers_psram` |   ✅   | No PSRAM: the large buffer falls back to DRAM while the reserve still fits.           |
|   5 | `test_below_the_threshold_prefers_dram`        |   ✅   | 512 + 32768 > 33000, so the reserve rules DRAM out and PSRAM takes it.                |
|   6 | `test_pingpong_roles_are_always_opposite`      |   ✅   | The swap reports the new fill index, and it is the buffer DMA was draining.           |
|   7 | `test_pingpong_accessors_refuse_a_null_handle` |   ✅   | Pingpong accessors refuse a null handle                                               |

</details>

---

## test_ptp - native_ptp_wire - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                                                                    |
| --: | :-------------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_timestamp_published_example`                              |   ✅   | 6.4.3.4 struct Timestamp {UInteger48 seconds; UInteger32 nanoseconds;}, written per 6.4.4.5    |
|   2 | `test_timestamp_octet_layout`                                   |   ✅   | The widest value each member holds: 2^48-1 seconds and 10^9-1 nanoseconds.                     |
|   3 | `test_nanosecond_conversion_is_exact_and_carries_at_one_second` |   ✅   | 1500000000 ns = 1 * 10^9 + 500000000.                                                          |
|   4 | `test_a_negative_instant_still_yields_a_well_formed_timestamp`  |   ✅   | A negative instant still yields a well formed timestamp                                        |
|   5 | `test_correction_field_published_example`                       |   ✅   | Table 10-7 puts correctionField at offset 8, 8 octets, most significant octet first (6.4.4.4). |
|   6 | `test_common_header_field_offsets`                              |   ✅   | 6.4.4.4: "one octet contains multiple fields ... the bit positions within the octet ... shall  |
|   7 | `test_header_round_trip`                                        |   ✅   | Header round trip                                                                              |
|   8 | `test_message_type_values`                                      |   ✅   | Message type values                                                                            |
|   9 | `test_event_and_general_message_classes`                        |   ✅   | No two enumerators share a number, so a parsed low nibble names one message.                   |
|  10 | `test_message_lengths`                                          |   ✅   | Message lengths                                                                                |
|  11 | `test_timestamp_message_build_and_parse`                        |   ✅   | An entry takes the borrow and reports on the namespace, so the three builders cannot sit in    |
|  12 | `test_delay_resp_body`                                          |   ✅   | Delay resp body                                                                                |
|  13 | `test_peer_delay_messages`                                      |   ✅   | 0x3 and 0xA are different messages, so neither parse accepts the other's frame.                |
|  14 | `test_pdelay_req_frame_is_fifty_four_octets`                    |   ✅   | Pdelay req frame is fifty four octets                                                          |
|  15 | `test_announce_body_offsets`                                    |   ✅   | Announce body offsets                                                                          |
|  16 | `test_announce_utc_offset_is_signed`                            |   ✅   | Announce utc offset is signed                                                                  |
|  17 | `test_builders_stamp_version_two`                               |   ✅   | Builders stamp version two                                                                     |
|  18 | `test_offset_and_delay_from_the_four_timestamps`                |   ✅   | Offset and delay from the four timestamps                                                      |
|  19 | `test_offset_and_delay_worked_example`                          |   ✅   | Offset and delay worked example                                                                |
|  20 | `test_peer_link_delay_is_independent_of_the_peer_offset`        |   ✅   | Peer link delay is independent of the peer offset                                              |
|  21 | `test_short_buffers_are_refused`                                |   ✅   | Short buffers are refused                                                                      |
|  22 | `test_transport_ports`                                          |   ✅   | Transport ports                                                                                |

</details>

---

## test_qpack - native_qpack_rfc - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                        | Status | Description                                                                                 |
| --: | :------------------------------------------ | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_rfc9204_b1_worked_example`            |   ✅   | Rfc9204 b1 worked example                                                                   |
|   2 | `test_rfc9204_field_section_prefix`         |   ✅   | Rfc9204 field section prefix                                                                |
|   3 | `test_rfc9204_indexed_field_line`           |   ✅   | Appendix A index 63 is :status 100, and 63 is the largest value a 6-bit prefix can hold, so |
|   4 | `test_rfc9204_appendix_a_static_table`      |   ✅   | Rfc9204 appendix a static table                                                             |
|   5 | `test_rfc9204_literal_with_name_reference`  |   ✅   | Appendix A index 12 is location, so 0x50 \| 12 = 0x5C                                       |
|   6 | `test_rfc9204_literal_with_literal_name`    |   ✅   | an empty value is a zero-length string literal, not an absent field                         |
|   7 | `test_field_section_round_trip`             |   ✅   | Field section round trip                                                                    |
|   8 | `test_dynamic_table_references_are_refused` |   ✅   | Dynamic table references are refused                                                        |
|   9 | `test_static_index_out_of_range_is_refused` |   ✅   | 6-bit prefix: 63 in the prefix plus 36 in the continuation octet                            |
|  10 | `test_truncated_block_is_refused`           |   ✅   | Truncated block is refused                                                                  |
|  11 | `test_scratch_bound_is_respected`           |   ✅   | Scratch bound is respected                                                                  |
|  12 | `test_emit_refusal_aborts_the_decode`       |   ✅   | Emit refusal aborts the decode                                                              |
|  13 | `test_encoder_refuses_a_short_destination`  |   ✅   | Encoder refuses a short destination                                                         |

</details>

---

## test_quic_frame - native_quic_frame_rfc - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                                   |
| --: | :-------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_rfc9000_frame_type_table`                     |   ✅   | sec 19.8: the STREAM type bits                                                                |
|   2 | `test_rfc9000_single_octet_frames`                  |   ✅   | sec 19.1: PADDING has no semantic value; n of them are n zero octets, each parsed on its own  |
|   3 | `test_rfc9000_ack_frame_fields`                     |   ✅   | Rfc9000 ack frame fields                                                                      |
|   4 | `test_rfc9000_ack_ranges_and_ecn_are_consumed`      |   ✅   | largest 10, delay 0, range count 2, first range 1, then (gap, len) twice                      |
|   5 | `test_rfc9000_crypto_frame`                         |   ✅   | Rfc9000 crypto frame                                                                          |
|   6 | `test_rfc9000_stream_frame_type_bits`               |   ✅   | LEN only: 0x08 \| 0x02 = 0x0a, then id, length, data                                          |
|   7 | `test_rfc9000_max_data`                             |   ✅   | Rfc9000 max data                                                                              |
|   8 | `test_rfc9000_connection_close_variants`            |   ✅   | transport variant: 0x1c, error PROTOCOL_VIOLATION (0x0a), triggering frame type CRYPTO (0x06) |
|   9 | `test_rfc9000_transport_error_codes`                |   ✅   | RFC 9001 sec 4.8: a TLS alert travels as 0x0100 + the alert description                       |
|  10 | `test_rfc9000_unhandled_frames_consume_their_shape` |   ✅   | 19.4 RESET_STREAM: Stream ID, Application Error Code, Final Size                              |
|  11 | `test_rfc9000_fixed_width_frames`                   |   ✅   | Rfc9000 fixed width frames                                                                    |
|  12 | `test_truncated_frames_are_refused`                 |   ✅   | Truncated frames are refused                                                                  |
|  13 | `test_builders_refuse_a_short_destination`          |   ✅   | Builders refuse a short destination                                                           |

</details>

---

## test_quic_packet - native_quic_packet_rfc - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                    |
| --: | :-------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_rfc9001_published_headers`              |   ✅   | A.2 client Initial: 0xc3 = long form, Fixed Bit, type 0x00, and 0b11 in the packet-number      |
|   2 | `test_build_reproduces_the_published_headers` |   ✅   | Build reproduces the published headers                                                         |
|   3 | `test_rfc9000_long_packet_types`              |   ✅   | Rfc9000 long packet types                                                                      |
|   4 | `test_rfc9000_fixed_bit_is_required`          |   ✅   | Rfc9000 fixed bit is required                                                                  |
|   5 | `test_rfc9001_short_header`                   |   ✅   | the spin and key-phase bits, each read off its own mask (0x20 and 0x04)                        |
|   6 | `test_rfc9000_version_negotiation`            |   ✅   | Version 0 is what marks it, and it is the one long header exempt from the Fixed Bit rule       |
|   7 | `test_rfc9000_a2_packet_number_length`        |   ✅   | A.2's encode step is "truncate to the num_bytes least significant bytes", big-endian           |
|   8 | `test_rfc9000_a3_packet_number_decode`        |   ✅   | RFC 9001 A.5 states a packet number of 654360564 (0x2700bff4) encoded on 3 octets as 0x00bff4, |
|   9 | `test_connection_id_bounds`                   |   ✅   | pn_len is 1..4 (sec 17.2: the field holds length - 1 in two bits)                              |
|  10 | `test_truncated_headers_are_refused`          |   ✅   | Truncated headers are refused                                                                  |

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

## test_radio_power - native_radio_power - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                | Status | Description                                                                       |
| --: | :-------------------------------------------------- | :----: | :-------------------------------------------------------------------------------- |
|   1 | `test_ps_names_are_the_layers_own`                  |   ✅   | Ps names are the layers own                                                       |
|   2 | `test_ps_name_does_not_apply_the_mode`              |   ✅   | Ps name does not apply the mode                                                   |
|   3 | `test_apply_sets_the_mode_and_reads_it_back`        |   ✅   | 802.11-2020 11.7.6 selects a transmit power in dBm; the backend takes both signs. |
|   4 | `test_power_applies_the_configured_mode`            |   ✅   | Power applies the configured mode                                                 |
|   5 | `test_busy_hold_forces_active_and_release_restores` |   ✅   | Nested: the inner release is not the last one, so the mode stays active.          |
|   6 | `test_the_borrow_is_carved`                         |   ✅   | The borrow is carved                                                              |

</details>

---

## test_radio_sniff - native_radio_sniff_tap - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                              |
| --: | :--------------------------------------------------- | :----: | :----------------------------------------------------------------------- |
|   1 | `test_ieee754_binary32_rss_encoding`                 |   ✅   | 1 = 1.0 * 2^0 -> exponent field 127 = 0x7F, significand 0                |
|   2 | `test_ieee754_binary32_wide_magnitude`               |   ✅   | 2^24 = 1.0 * 2^24 -> exponent field 151 = 0x97, significand 0            |
|   3 | `test_pcap_global_header_declares_the_tap_link_type` |   ✅   | Pcap global header declares the tap link type                            |
|   4 | `test_tap_record_layout`                             |   ✅   | pcap record header: seconds, microseconds, captured length, wire length  |
|   5 | `test_tap_record_lengths_track_the_frame`            |   ✅   | caplen is a little-endian 32-bit field at offset 8 of the record header. |
|   6 | `test_tap_channel_assignment_is_sixteen_bits`        |   ✅   | Channel 26, the top of the 2.4 GHz O-QPSK page 0 range.                  |
|   7 | `test_tap_record_fails_closed`                       |   ✅   | Tap record fails closed                                                  |

</details>

---

## test_rawl2 - native_rawl2 - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                     | Status | Description                                                                               |
| --: | :--------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_ethertype_registry`                |   ✅   | Ethertype registry                                                                        |
|   2 | `test_ethernet_ii_header_layout`         |   ✅   | Ethernet ii header layout                                                                 |
|   3 | `test_8021q_tag_layout`                  |   ✅   | PCP 6 = 110b, DEI 1, VID ABCh -> 1101 1010 1011 1100 = DABC                               |
|   4 | `test_vlan_tci_field_widths`             |   ✅   | the DEI is bit 12 of the TCI and belongs to neither the PCP nor the VID                   |
|   5 | `test_tagged_and_untagged_stay_distinct` |   ✅   | Tagged and untagged stay distinct                                                         |
|   6 | `test_fcs_published_check_value`         |   ✅   | the empty message: init FFFFFFFF with no octets folded in, then the final XOR of FFFFFFFF |
|   7 | `test_parse_refuses_a_short_frame`       |   ✅   | Parse refuses a short frame                                                               |
|   8 | `test_build_refuses_bad_arguments`       |   ✅   | Build refuses bad arguments                                                               |
|   9 | `test_payload_round_trip`                |   ✅   | Payload round trip                                                                        |

</details>

---

## test_rawmemcpy - native_rawmemcpy - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                                |
| --: | :------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_word_rung_follows_the_declared_register_width`     |   ✅   | One of the three rungs the file is built from, and never wider than the declared register. |
|   2 | `test_scalar_rungs_match_a_byte_loop`                    |   ✅   | Scalar rungs match a byte loop                                                             |
|   3 | `test_load_selects_a_rung_and_refuses_every_other_width` |   ✅   | Load selects a rung and refuses every other width                                          |
|   4 | `test_put_round_trips_and_stays_inside_its_width`        |   ✅   | Nothing below the store either.                                                            |
|   5 | `test_aligned_rungs_agree_with_the_unaligned_ones`       |   ✅   | The mover's own rung is the aligned load and store at PROTO_RAW_WORD.                      |
|   6 | `test_read_moves_every_offset_pair_and_length`           |   ✅   | Read moves every offset pair and length                                                    |
|   7 | `test_read_carries_an_overlaid_header_struct`            |   ✅   | Read carries an overlaid header struct                                                     |
|   8 | `test_the_table_is_wired_to_the_named_rungs`             |   ✅   | The table is wired to the named rungs                                                      |

</details>

---

## test_rcwl0516 - native_rcwl0516 - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                                 |
| --: | :---------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_fresh_core_is_absent`                                 |   ✅   | Fresh core is absent                                                                        |
|   2 | `test_a_level_is_believed_only_after_the_debounce`          |   ✅   | A level is believed only after the debounce                                                 |
|   3 | `test_chatter_below_the_debounce_is_swallowed`              |   ✅   | Chatter below the debounce is swallowed                                                     |
|   4 | `test_hold_bridges_the_retrigger_gap`                       |   ✅   | OUT drops, and is sampled LOW across a gap well inside the hold                             |
|   5 | `test_presence_clears_exactly_one_hold_after_the_last_high` |   ✅   | Presence clears exactly one hold after the last high                                        |
|   6 | `test_event_is_taken_once_per_edge`                         |   ✅   | polling on while present raises no new event                                                |
|   7 | `test_zero_debounce_and_zero_hold_follow_the_level`         |   ✅   | Zero debounce and zero hold follow the level                                                |
|   8 | `test_zero_hold_still_debounces`                            |   ✅   | the LOW is not believed until it too has outlasted the debounce; then presence goes at once |
|   9 | `test_timing_survives_the_millis_rollover`                  |   ✅   | Timing survives the millis rollover                                                         |
|  10 | `test_rcwl0516_defaults`                                    |   ✅   | Rcwl0516 defaults                                                                           |
|  11 | `test_repeated_timestamps_are_harmless`                     |   ✅   | Repeated timestamps are harmless                                                            |
|  12 | `test_null_core_is_refused`                                 |   ✅   | Null core is refused                                                                        |

</details>

---

## test_redis_resp - native_redis_resp - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                                                                                    |
| --: | :---------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_sending_commands_to_a_redis_server` |   ✅   | The server's reply to it, the Integers example `:48293\r\n`.                                   |
|   2 | `test_binary_safe_arguments`              |   ✅   | A zero-length argument is `$0\r\n\r\n`, the empty bulk string of the specification.            |
|   3 | `test_simple_strings_and_errors`          |   ✅   | The message is the line less the leading '-' and the trailing CRLF, so three of the octets the |
|   4 | `test_integers`                           |   ✅   | A value that is not a base-10 run is refused rather than read as zero.                         |
|   5 | `test_bulk_strings`                       |   ✅   | Bulk strings are binary safe: a CRLF inside the body is data, and the length is what ends it.  |
|   6 | `test_arrays_walk_element_by_element`     |   ✅   | The Null array is the length -1 exactly; no other negative count has an encoding.              |
|   7 | `test_resp3_simple_types`                 |   ✅   | Only 't' and 'f' are Booleans.                                                                 |
|   8 | `test_bulk_errors_and_verbatim_strings`   |   ✅   | "Exactly three (3) bytes represent the data's encoding", then the colon separator.             |
|   9 | `test_maps_report_two_children_per_entry` |   ✅   | Maps report two children per entry                                                             |
|  10 | `test_sets_and_pushes`                    |   ✅   | Sets and pushes                                                                                |
|  11 | `test_unknown_first_bytes_are_refused`    |   ✅   | Unknown first bytes are refused                                                                |
|  12 | `test_parse_waits_for_the_whole_value`    |   ✅   | A header line with no CRLF yet is not a value either.                                          |
|  13 | `test_encode_fails_closed`                |   ✅   | An argument array with a hole in it is refused.                                                |
|  14 | `test_multi_argument_command`             |   ✅   | The encoded command reads back as an Array header of five bulk strings.                        |

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

|   # | Test                                                  | Status | Description                                                                                  |
| --: | :---------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_length_table_matches_rfc`                       |   ✅   | Length table matches rfc                                                                     |
|   2 | `test_distance_table_matches_rfc`                     |   ✅   | Distance table matches rfc                                                                   |
|   3 | `test_length_spans_are_contiguous`                    |   ✅   | Length spans are contiguous                                                                  |
|   4 | `test_distance_spans_are_contiguous`                  |   ✅   | Code 29's printed range is 24577-32768, so its last value is base + 2^extra - 1.             |
|   5 | `test_namespace_holds_all_four_tables`                |   ✅   | Namespace holds all four tables                                                              |
|   6 | `test_build_fixed_lengths_match_rfc`                  |   ✅   | Build fixed lengths match rfc                                                                |
|   7 | `test_build_fixed_codes_are_the_rfc_codes_reversed`   |   ✅   | The whole 0-143 run is consecutive from 00110000, which is what "canonical" means here.      |
|   8 | `test_reverse_bits_is_its_own_inverse`                |   ✅   | Reverse bits is its own inverse                                                              |
|   9 | `test_emit_literal_puts_the_code_on_the_wire`         |   ✅   | Emit literal puts the code on the wire                                                       |
|  10 | `test_emit_match_selects_the_code_for_the_span`       |   ✅   | Emit match selects the code for the span                                                     |
|  11 | `test_emit_match_uses_the_single_length_code_for_258` |   ✅   | Symbol 285 is 280 + 5, so its 8-bit code is 11000000 + 5 = 11000101; then the 5-bit distance |
|  12 | `test_emit_match_writes_the_offset_in_the_extra_bits` |   ✅   | The table itself says which code a span belongs to, which is what the emitter walks.         |
|  13 | `test_emit_past_the_buffer_latches_overflow`          |   ✅   | Emit past the buffer latches overflow                                                        |

</details>

---

## test_rfc1951 - native_codec_rfc1951 - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                                  |
| --: | :---------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_length_table_matches_rfc`                       |   ✅   | Length table matches rfc                                                                     |
|   2 | `test_distance_table_matches_rfc`                     |   ✅   | Distance table matches rfc                                                                   |
|   3 | `test_length_spans_are_contiguous`                    |   ✅   | Length spans are contiguous                                                                  |
|   4 | `test_distance_spans_are_contiguous`                  |   ✅   | Code 29's printed range is 24577-32768, so its last value is base + 2^extra - 1.             |
|   5 | `test_namespace_holds_all_four_tables`                |   ✅   | Namespace holds all four tables                                                              |
|   6 | `test_build_fixed_lengths_match_rfc`                  |   ✅   | Build fixed lengths match rfc                                                                |
|   7 | `test_build_fixed_codes_are_the_rfc_codes_reversed`   |   ✅   | The whole 0-143 run is consecutive from 00110000, which is what "canonical" means here.      |
|   8 | `test_reverse_bits_is_its_own_inverse`                |   ✅   | Reverse bits is its own inverse                                                              |
|   9 | `test_emit_literal_puts_the_code_on_the_wire`         |   ✅   | Emit literal puts the code on the wire                                                       |
|  10 | `test_emit_match_selects_the_code_for_the_span`       |   ✅   | Emit match selects the code for the span                                                     |
|  11 | `test_emit_match_uses_the_single_length_code_for_258` |   ✅   | Symbol 285 is 280 + 5, so its 8-bit code is 11000000 + 5 = 11000101; then the 5-bit distance |
|  12 | `test_emit_match_writes_the_offset_in_the_extra_bits` |   ✅   | The table itself says which code a span belongs to, which is what the emitter walks.         |
|  13 | `test_emit_past_the_buffer_latches_overflow`          |   ✅   | Emit past the buffer latches overflow                                                        |

</details>

---

## test_ring - native_ring - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                                |
| --: | :---------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_wrap_is_the_modulo_a_power_of_two_capacity_defines`   |   ✅   | Wrap is the modulo a power of two capacity defines                                         |
|   2 | `test_available_and_free_partition_the_ring`                |   ✅   | Never more than cap-1 bytes can be written, so a producer cannot make head meet tail.      |
|   3 | `test_read_byte_pops_in_fifo_order_and_reports_empty`       |   ✅   | Read byte pops in fifo order and reports empty                                             |
|   4 | `test_read_takes_what_is_asked_for_and_advances_the_tail`   |   ✅   | Asking for more than is there yields what is there, and no more.                           |
|   5 | `test_peek_does_not_consume_and_consume_advances`           |   ✅   | The same peek again, and one from an offset: a header read then the body behind it.        |
|   6 | `test_a_span_across_the_wrap_reads_back_whole`              |   ✅   | Start the fill 12 bytes in, so 4 bytes land before the end and 6 after the wrap.           |
|   7 | `test_a_segment_is_invisible_until_it_is_published`         |   ✅   | A segment is invisible until it is published                                               |
|   8 | `test_segments_release_in_order_and_a_full_ring_refuses`    |   ✅   | The indices keep counting; the segment they name is the count masked by nsegs, so the ring |
|   9 | `test_slot_take_is_won_by_exactly_one_caller`               |   ✅   | Slot take is won by exactly one caller                                                     |
|  10 | `test_a_losing_hold_cannot_redirect_the_keepout`            |   ✅   | A losing hold cannot redirect the keepout                                                  |
|  11 | `test_ready_is_marked_minus_held_within_the_count`          |   ✅   | With the count raised to reach it, slot 5 is ready too.                                    |
|  12 | `test_slot_next_is_the_lowest_set_and_minus_one_when_empty` |   ✅   | With every higher bit set as well, the answer is still the lowest.                         |
|  13 | `test_an_out_of_range_slot_names_nothing`                   |   ✅   | The count-to-mask side of the same bound: a count at or past the width is every slot.      |

</details>

---

## test_roaming - native_roaming - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                          | Status | Description                                                                                   |
| --: | :---------------------------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_a_neighbor_report_yields_bssid_and_channel`                             |   ✅   | A neighbor report yields bssid and channel                                                    |
|   2 | `test_other_element_ids_are_stepped_over`                                     |   ✅   | Other element ids are stepped over                                                            |
|   3 | `test_a_truncated_or_short_element_yields_no_candidate`                       |   ✅   | Element 52 with a 12-octet body: one short of BSSID + BSSID Information + Operating Class +   |
|   4 | `test_the_decode_stops_at_the_output_bound`                                   |   ✅   | The decode stops at the output bound                                                          |
|   5 | `test_a_btm_request_decodes_its_request_mode`                                 |   ✅   | Bit 1 (Abridged) is not bit 2, so it does not read as an imminent disassociation.             |
|   6 | `test_a_frame_that_is_not_a_btm_request_is_refused`                           |   ✅   | A frame that is not a btm request is refused                                                  |
|   7 | `test_a_btm_request_carries_its_preferred_candidate_past_the_optional_fields` |   ✅   | BSS Termination Duration subelement: Subelement ID 4 \| Length 10 \| TSF(8) \| Duration(2).   |
|   8 | `test_disassociation_imminent_overrides_the_signal`                           |   ✅   | The link is strong (-40) and the preferred candidate is the weaker of the two: it still wins. |
|   9 | `test_a_suggested_candidate_is_taken_only_when_it_is_no_weaker`               |   ✅   | A preferred BSSID that is not in the candidate list names nothing to move to.                 |
|  10 | `test_the_signal_transition_needs_the_threshold_and_the_margin`               |   ✅   | Serving one dB above the threshold: no transition however strong the candidate.               |
|  11 | `test_a_null_policy_takes_the_built_in_thresholds`                            |   ✅   | A null policy takes the built in thresholds                                                   |
|  12 | `test_the_serving_bss_is_never_the_target`                                    |   ✅   | With only ourselves in the list there is nothing to move to.                                  |
|  13 | `test_no_serving_bssid_stays_and_clears_the_verdict`                          |   ✅   | A candidate count with no list behind it is the same refusal.                                 |

</details>

---

## test_robotics - native_robotics - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                     | Status | Description                                                                                    |
| --: | :------------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_objects_folder_organizes_the_motion_device_system` |   ✅   | Objects folder organizes the motion device system                                              |
|   2 | `test_motion_device_system_components`                   |   ✅   | Opc.Ua.Robotics.NodeSet2.xml types all three FolderType (61), not BaseObjectType (58):         |
|   3 | `test_folders_organize_their_members`                    |   ✅   | Folders organize their members                                                                 |
|   4 | `test_motion_device_identity`                            |   ✅   | MotionDeviceType reaches every nameplate leaf by HasProperty (46) to a PropertyType (68) node, |
|   5 | `test_parameter_set_values`                              |   ✅   | Parameter set values                                                                           |
|   6 | `test_axes_folder_follows_the_bound_axis_count`          |   ✅   | widen the machine and the folder widens with it, up to the build's axis cap                    |
|   7 | `test_axis_variables_read_their_own_axis`                |   ✅   | Axis variables read their own axis                                                             |
|   8 | `test_axis_beyond_the_bound_count_is_absent`             |   ✅   | and a sub-id past the four axis variables is not one either                                    |
|   9 | `test_controller_and_software`                           |   ✅   | Controller and software                                                                        |
|  10 | `test_safety_state`                                      |   ✅   | Safety state                                                                                   |
|  11 | `test_null_strings_read_as_empty`                        |   ✅   | Null strings read as empty                                                                     |
|  12 | `test_reads_outside_the_model_are_refused`               |   ✅   | Reads outside the model are refused                                                            |
|  13 | `test_nothing_is_served_before_bind`                     |   ✅   | Nothing is served before bind                                                                  |
|  14 | `test_browse_respects_the_reference_cap`                 |   ✅   | Browse respects the reference cap                                                              |
|  15 | `test_every_reference_resolves`                          |   ✅   | Every reference resolves                                                                       |

</details>

---

## test_rtc - native_rtc - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                  |
| --: | :-------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_y2k_and_leap_days`                      |   ✅   | 2016 is a plain divisible-by-four leap year.                                                 |
|   2 | `test_time_of_day_adds_to_the_date`           |   ✅   | Time of day adds to the date                                                                 |
|   3 | `test_twelve_hour_encoding`                   |   ✅   | Twelve hour encoding                                                                         |
|   4 | `test_clock_halt_and_century_bits_are_masked` |   ✅   | Clock halt and century bits are masked                                                       |
|   5 | `test_out_of_range_fields_are_refused`        |   ✅   | an all-zero read, which is what an absent or never-set part looks like                       |
|   6 | `test_epoch_to_regs_is_bcd_and_24_hour`       |   ✅   | 946684800 + 45296 is 2000-01-01 12:34:56 (see test_time_of_day_adds_to_the_date)             |
|   7 | `test_day_of_week_from_the_epoch`             |   ✅   | 2000-01-01 was a Saturday: 10957 days after a Thursday, 10957 mod 7 = 2, Thu + 2 = Sat = 6.  |
|   8 | `test_round_trip_over_the_register_range`     |   ✅   | 100 years of seconds stepped by a value that is coprime with a day, an hour and a minute, so |

</details>

---

## test_rtcm3 - native_gnss_rtcm3 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                   |
| --: | :-------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_crc24q_residue_is_zero`                 |   ✅   | Crc24q residue is zero                                                                        |
|   2 | `test_crc24q_is_linear_with_a_zero_seed`      |   ✅   | The result never spills past 24 bits.                                                         |
|   3 | `test_frame_header_layout`                    |   ✅   | 1023 is the widest a 10-bit length can name; one more does not fit the field.                 |
|   4 | `test_frame_parse_round_trip`                 |   ✅   | Extra bytes after the frame belong to the next one and are not consumed.                      |
|   5 | `test_frame_parse_waits_for_the_whole_frame`  |   ✅   | Frame parse waits for the whole frame                                                         |
|   6 | `test_frame_parse_reports_a_bad_crc`          |   ✅   | Frame parse reports a bad crc                                                                 |
|   7 | `test_sync_finds_the_next_preamble`           |   ✅   | Sync finds the next preamble                                                                  |
|   8 | `test_bit_writer_is_msb_first`                |   ✅   | The next field continues in the same octet: four more bits of 0b1010 fill the low nibble.     |
|   9 | `test_bit_cursor_signed_fields`               |   ✅   | -1 in a 38-bit field is 38 set bits: the first four octets are 0xFF and the next six bits are |
|  10 | `test_message_1005_field_offsets`             |   ✅   | Message 1005 field offsets                                                                    |
|  11 | `test_message_1006_adds_the_antenna_height`   |   ✅   | Message 1006 adds the antenna height                                                          |
|  12 | `test_ecef_coordinates_span_the_38_bit_range` |   ✅   | Ecef coordinates span the 38 bit range                                                        |
|  13 | `test_parse_1005_rejects_what_it_is_not`      |   ✅   | Rewrite DF002 to 1004 and it is no longer a 1005: 1004 = 0x3EC.                               |
|  14 | `test_build_capacity_is_respected`            |   ✅   | Build capacity is respected                                                                   |

</details>

---

## test_s7comm - native_s7comm - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                      |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_dissector_constants`                       |   ✅   | Dissector constants                                                              |
|   2 | `test_setup_communication_job`                   |   ✅   | Setup communication job                                                          |
|   3 | `test_read_request_s7any_item`                   |   ✅   | Read request s7any item                                                          |
|   4 | `test_read_request_multiple_items`               |   ✅   | Read request multiple items                                                      |
|   5 | `test_read_response_item_length_rule`            |   ✅   | one BYTE item of four octets: length 4 * 8 = 32 bits                             |
|   6 | `test_read_response_even_padding`                |   ✅   | an odd-length LAST item carries no pad, so the section ends exactly on its value |
|   7 | `test_write_request_round_trips_the_length_rule` |   ✅   | parameter 2 + 24, data (4 + 1 + 1 pad) + (4 + 4) = 14, header 10                 |
|   8 | `test_response_header_carries_the_error_code`    |   ✅   | a plain Ack response is the other 12-octet ROSCTR                                |
|   9 | `test_header_validation`                         |   ✅   | an Ack_Data truncated inside its own error code                                  |
|  10 | `test_read_item_refuses_an_overrun`              |   ✅   | Read item refuses an overrun                                                     |
|  11 | `test_builders_refuse_bad_arguments`             |   ✅   | Builders refuse bad arguments                                                    |

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

|   # | Test                                              | Status | Description                                                                                  |
| --: | :------------------------------------------------ | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_mode_is_the_posix_permission_word_in_octal` |   ✅   | Mode is the posix permission word in octal                                                   |
|   2 | `test_mode_rejects_non_octal_digits`              |   ✅   | Mode rejects non octal digits                                                                |
|   3 | `test_control_line_round_trip`                    |   ✅   | Control line round trip                                                                      |
|   4 | `test_build_masks_the_file_type_bits`             |   ✅   | The setuid bit is inside the twelve, so it survives as a fifth-column-free 4-digit mode.     |
|   5 | `test_only_c_records_are_file_records`            |   ✅   | Only c records are file records                                                              |
|   6 | `test_truncated_records_are_refused`              |   ✅   | Truncated records are refused                                                                |
|   7 | `test_name_ends_at_the_newline_or_the_length`     |   ✅   | An embedded NUL terminates the name the same way a newline does.                             |
|   8 | `test_name_too_long_is_refused_not_truncated`     |   ✅   | Exactly one short of the buffer fits; the buffer's worth does not (the NUL needs a byte).    |
|   9 | `test_mode_and_size_outputs_are_optional`         |   ✅   | Mode and size outputs are optional                                                           |
|  10 | `test_build_refuses_a_short_buffer`               |   ✅   | "C0644 1 f\n" is 10 octets, so 10 leaves no room for the NUL and 11 does.                    |
|  11 | `test_role_flag_selects_sink_or_source`           |   ✅   | The other flags scp sends (-v verbose, -r recursive, -p preserve, -d directory) are accepted |
|  12 | `test_command_without_a_role_is_invalid`          |   ✅   | Command without a role is invalid                                                            |
|  13 | `test_command_tokenizing`                         |   ✅   | Command tokenizing                                                                           |
|  14 | `test_path_too_long_is_refused`                   |   ✅   | Path too long is refused                                                                     |
|  15 | `test_command_null_arguments_are_refused`         |   ✅   | Command null arguments are refused                                                           |
|  16 | `test_ack_octets`                                 |   ✅   | Ack octets                                                                                   |

</details>

---

## test_scp - native_scp_wire - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                              | Status | Description                                                                                  |
| --: | :------------------------------------------------ | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_mode_is_the_posix_permission_word_in_octal` |   ✅   | Mode is the posix permission word in octal                                                   |
|   2 | `test_mode_rejects_non_octal_digits`              |   ✅   | Mode rejects non octal digits                                                                |
|   3 | `test_control_line_round_trip`                    |   ✅   | Control line round trip                                                                      |
|   4 | `test_build_masks_the_file_type_bits`             |   ✅   | The setuid bit is inside the twelve, so it survives as a fifth-column-free 4-digit mode.     |
|   5 | `test_only_c_records_are_file_records`            |   ✅   | Only c records are file records                                                              |
|   6 | `test_truncated_records_are_refused`              |   ✅   | Truncated records are refused                                                                |
|   7 | `test_name_ends_at_the_newline_or_the_length`     |   ✅   | An embedded NUL terminates the name the same way a newline does.                             |
|   8 | `test_name_too_long_is_refused_not_truncated`     |   ✅   | Exactly one short of the buffer fits; the buffer's worth does not (the NUL needs a byte).    |
|   9 | `test_mode_and_size_outputs_are_optional`         |   ✅   | Mode and size outputs are optional                                                           |
|  10 | `test_build_refuses_a_short_buffer`               |   ✅   | "C0644 1 f\n" is 10 octets, so 10 leaves no room for the NUL and 11 does.                    |
|  11 | `test_role_flag_selects_sink_or_source`           |   ✅   | The other flags scp sends (-v verbose, -r recursive, -p preserve, -d directory) are accepted |
|  12 | `test_command_without_a_role_is_invalid`          |   ✅   | Command without a role is invalid                                                            |
|  13 | `test_command_tokenizing`                         |   ✅   | Command tokenizing                                                                           |
|  14 | `test_path_too_long_is_refused`                   |   ✅   | Path too long is refused                                                                     |
|  15 | `test_command_null_arguments_are_refused`         |   ✅   | Command null arguments are refused                                                           |
|  16 | `test_ack_octets`                                 |   ✅   | Ack octets                                                                                   |

</details>

---

## test_scpi - native_scpi - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                                                                   |
| --: | :-------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_scpi99_exact_short_or_long_form_only`               |   ✅   | Scpi99 exact short or long form only                                                          |
|   2 | `test_query_marker_and_depth_must_agree`                  |   ✅   | Query marker and depth must agree                                                             |
|   3 | `test_numeric_suffix_defaults_to_one`                     |   ✅   | Numeric suffix defaults to one                                                                |
|   4 | `test_leading_colon_and_parameters_are_ignored`           |   ✅   | Leading colon and parameters are ignored                                                      |
|   5 | `test_common_commands`                                    |   ✅   | Common commands                                                                               |
|   6 | `test_command_line_form`                                  |   ✅   | Command line form                                                                             |
|   7 | `test_build_refuses_bad_arguments`                        |   ✅   | Build refuses bad arguments                                                                   |
|   8 | `test_numeric_response_forms`                             |   ✅   | Numeric response forms                                                                        |
|   9 | `test_scpi99_special_numeric_values`                      |   ✅   | Scpi99 special numeric values                                                                 |
|  10 | `test_malformed_numbers_are_refused`                      |   ✅   | Malformed numbers are refused                                                                 |
|  11 | `test_real_format_round_trips`                            |   ✅   | Real format round trips                                                                       |
|  12 | `test_boolean_responses`                                  |   ✅   | Boolean responses                                                                             |
|  13 | `test_string_responses`                                   |   ✅   | a doubled quote inside collapses to one                                                       |
|  14 | `test_ieee4882_definite_length_block`                     |   ✅   | a block carrying binary octets, which is the whole point of the form                          |
|  15 | `test_ieee4882_indefinite_length_block`                   |   ✅   | without the terminator the block is not yet complete                                          |
|  16 | `test_block_refuses_malformed_and_truncated`              |   ✅   | Block refuses malformed and truncated                                                         |
|  17 | `test_ieee4882_register_bit_positions`                    |   ✅   | Ieee4882 register bit positions                                                               |
|  18 | `test_scpi99_error_class_sets_its_esr_bit`                |   ✅   | sec 21.8: "The value, zero, is also reserved to indicate that no error or event has occurred" |
|  19 | `test_scpi99_error_queue_is_fifo_and_empties_to_no_error` |   ✅   | Scpi99 error queue is fifo and empties to no error                                            |
|  20 | `test_scpi99_queue_overflow_rule`                         |   ✅   | the least recent entries survive in order                                                     |
|  21 | `test_scpi99_standard_error_messages`                     |   ✅   | a device-specific (positive) number has no standard text                                      |
|  22 | `test_status_byte_summary_bits`                           |   ✅   | a queued error raises EAV, and popping it lowers it again                                     |
|  23 | `test_cls_clears_events_not_enables`                      |   ✅   | status_init is the power-on state: everything clear                                           |
|  24 | `test_status_calls_tolerate_null`                         |   ✅   | Status calls tolerate null                                                                    |

</details>

---

## test_sdi12 - native_sdi12 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                          |
| --: | :--------------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_spec_crc_vectors`                              |   ✅   | and the encoder reproduces those same three octets from the data before them         |
|   2 | `test_crc16_arc_check_value`                         |   ✅   | Crc16 arc check value                                                                |
|   3 | `test_crc_encoding_is_always_printable`              |   ✅   | and the three fields are the CRC's own bits, per the published algorithm             |
|   4 | `test_corrupt_data_fails_the_crc`                    |   ✅   | section 4.4.12.2 puts the CRC before the <CR><LF>, which the check trims             |
|   5 | `test_spec_command_set`                              |   ✅   | section 4.4.3: '?' is the wild card address used with the acknowledge active command |
|   6 | `test_spec_indexed_commands`                         |   ✅   | section 4.4.8: the send data commands stop at D9                                     |
|   7 | `test_spec_measurement_responses`                    |   ✅   | Spec measurement responses                                                           |
|   8 | `test_spec_concurrent_response_has_two_count_digits` |   ✅   | and the ttt field keeps its full three-digit range                                   |
|   9 | `test_measurement_response_edges`                    |   ✅   | Measurement response edges                                                           |
|  10 | `test_spec_data_responses`                           |   ✅   | 4.4.8.2 / 4.4.8.4 a: one value                                                       |
|  11 | `test_values_ignore_the_appended_crc`                |   ✅   | Values ignore the appended crc                                                       |
|  12 | `test_values_are_bounded`                            |   ✅   | a sign with no digits after it is not a value                                        |
|  13 | `test_spec_identify_field_widths`                    |   ✅   | vendor and model space-padded to their widths, the way the spec pads short values    |
|  14 | `test_build_refuses_a_short_buffer`                  |   ✅   | Build refuses a short buffer                                                         |

</details>

---

## test_secure_pool - native_secure_pool - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_release_wipes_before_the_bytes_are_available_again` |   ✅   | Release wipes before the bytes are available again |
|   2 | `test_reset_wipes_every_live_borrow`                      |   ✅   | Reset wipes every live borrow                      |
|   3 | `test_a_scope_guard_wipes_on_every_exit_path`             |   ✅   | A scope guard wipes on every exit path             |
|   4 | `test_nested_scopes_reclaim_lifo`                         |   ✅   | Nested scopes reclaim lifo                         |
|   5 | `test_the_two_pools_are_disjoint_regions`                 |   ✅   | The borrowing slot is the calling worker's own.    |
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

|   # | Test                                              | Status | Description                                                        |
| --: | :------------------------------------------------ | :----: | :----------------------------------------------------------------- |
|   1 | `test_fresh_tracker_is_absent`                    |   ✅   | and a tick before any sample cannot assert or clear anything       |
|   2 | `test_first_active_sample_is_the_only_edge`       |   ✅   | First active sample is the only edge                               |
|   3 | `test_presence_is_held_then_clears_at_the_window` |   ✅   | an inactive sample ages presence out the same way a bare tick does |
|   4 | `test_active_samples_extend_one_span`             |   ✅   | and the window still runs from the last of them                    |
|   5 | `test_events_count_arrivals`                      |   ✅   | Events count arrivals                                              |
|   6 | `test_polarity_selects_the_active_level`          |   ✅   | Polarity selects the active level                                  |
|   7 | `test_active_age`                                 |   ✅   | an inactive sample does not reset it                               |
|   8 | `test_timing_survives_the_millis_rollover`        |   ✅   | Timing survives the millis rollover                                |
|   9 | `test_zero_hold_clears_on_the_next_tick`          |   ✅   | Zero hold clears on the next tick                                  |
|  10 | `test_repeated_timestamps_are_harmless`           |   ✅   | Repeated timestamps are harmless                                   |

</details>

---

## test_senml - native_senml_pack - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_rfc8428_section_5_1_1_example`            |   ✅   | Rfc8428 section 5 1 1 example                                                            |
|   2 | `test_rfc8428_section_5_1_2_example`            |   ✅   | Rfc8428 section 5 1 2 example                                                            |
|   3 | `test_integral_numbers_are_written_as_integers` |   ✅   | Integral numbers are written as integers                                                 |
|   4 | `test_the_three_value_fields`                   |   ✅   | An empty Pack is the empty array, which is still a well-formed sec 3 Pack.               |
|   5 | `test_cbor_table_4_integer_map_keys`            |   ✅   | array(1), map(6), then the six label / value pairs in the module's field order.          |
|   6 | `test_cbor_non_integral_number_is_a_float`      |   ✅   | Cbor non integral number is a float                                                      |
|   7 | `test_resolve_folds_base_name_and_base_time`    |   ✅   | The Base Name and Base Time carry forward to the Records after the one that stated them. |
|   8 | `test_resolve_overrides_and_absent_time`        |   ✅   | A resolve stops at the room the caller lent, and reports how many Records it filled.     |
|   9 | `test_resolve_refuses_to_truncate_a_name`       |   ✅   | Resolve refuses to truncate a name                                                       |
|  10 | `test_builders_report_zero_on_a_short_buffer`   |   ✅   | Builders report zero on a short buffer                                                   |
|  11 | `test_missing_arguments_are_refused`            |   ✅   | Missing arguments are refused                                                            |

</details>

---

## test_sep2 - native_sep2 - ✅ 6 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                                                                   |
| --: | :---------------------------------------- | :----: | :---------------------------------------------------------------------------- |
|   1 | `test_device_capability_document`         |   ✅   | Device capability document                                                    |
|   2 | `test_end_device_document`                |   ✅   | 2^36 - 1 above and 0 here bracket the range the identifier is defined over.   |
|   3 | `test_der_control_document`               |   ✅   | The interval spans the full 32-bit epoch range the fields are defined over.   |
|   4 | `test_xml_special_characters_are_escaped` |   ✅   | No literal '&' or '<' survives from the caller's text into the document body. |
|   5 | `test_null_strings_render_as_empty`       |   ✅   | Null strings render as empty                                                  |
|   6 | `test_overflow_reports_zero`              |   ✅   | Overflow reports zero                                                         |

</details>

---

## test_sercos - native_sercos - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                   |
| --: | :--------------------------------------------------- | :----: | :-------------------------------------------- |
|   1 | `test_idn_symbolic_names`                            |   ✅   | Idn symbolic names                            |
|   2 | `test_idn_fields_are_disjoint_and_tile_the_word`     |   ✅   | Idn fields are disjoint and tile the word     |
|   3 | `test_an_over_wide_argument_stays_in_its_own_field`  |   ✅   | An over wide argument stays in its own field  |
|   4 | `test_idn_round_trip_over_every_word`                |   ✅   | Idn round trip over every word                |
|   5 | `test_idn_parse_accepts_null_outputs`                |   ✅   | Idn parse accepts null outputs                |
|   6 | `test_a_telegram_is_a_fixed_header_plus_its_payload` |   ✅   | A telegram is a fixed header plus its payload |
|   7 | `test_telegram_round_trip`                           |   ✅   | Telegram round trip                           |
|   8 | `test_the_two_telegram_types_are_distinct`           |   ✅   | The two telegram types are distinct           |
|   9 | `test_cycle_count_carries_every_sixteen_bit_value`   |   ✅   | Cycle count carries every sixteen bit value   |
|  10 | `test_phase_octet_carries_every_value`               |   ✅   | Phase octet carries every value               |
|  11 | `test_only_the_two_defined_types_are_accepted`       |   ✅   | Only the two defined types are accepted       |
|  12 | `test_bounds_refusals`                               |   ✅   | Bounds refusals                               |
|  13 | `test_mdt_at_exchange`                               |   ✅   | Mdt at exchange                               |

</details>

---

## test_sht3x - native_sht3x - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                                 |
| --: | :---------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_datasheet_crc_check_value`                |   ✅   | the CRC covers exactly the two data bytes, so a different pair gives a different check byte |
|   2 | `test_datasheet_temperature_formula`            |   ✅   | Datasheet temperature formula                                                               |
|   3 | `test_datasheet_humidity_formula`               |   ✅   | Datasheet humidity formula                                                                  |
|   4 | `test_conversions_stay_inside_the_sensor_range` |   ✅   | Conversions stay inside the sensor range                                                    |
|   5 | `test_six_byte_response`                        |   ✅   | either output is optional; the CRCs are still checked                                       |
|   6 | `test_corrupt_response_is_refused`              |   ✅   | nothing was written through on the refusal                                                  |
|   7 | `test_datasheet_command_codes`                  |   ✅   | Table 9, single shot with clock stretching disabled: high 0x2400, medium 0x240B, low 0x2416 |

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

## test_simatic - native_simatic - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                 |
| --: | :---------------------------------------------- | :----: | :-------------------------------------------------------------------------- |
|   1 | `test_bcc_covers_stuffed_data_and_terminator`   |   ✅   | the same value reached through the exported checksum over the checked range |
|   2 | `test_doubled_dle_is_checksum_neutral`          |   ✅   | Doubled dle is checksum neutral                                             |
|   3 | `test_block_without_bcc_ends_at_dle_etx`        |   ✅   | Block without bcc ends at dle etx                                           |
|   4 | `test_block_round_trip_is_transparent`          |   ✅   | Block round trip is transparent                                             |
|   5 | `test_single_bit_flip_changes_the_bcc`          |   ✅   | Single bit flip changes the bcc                                             |
|   6 | `test_parse_refuses_a_wrong_bcc`                |   ✅   | Parse refuses a wrong bcc                                                   |
|   7 | `test_parse_refuses_bad_framing`                |   ✅   | Parse refuses bad framing                                                   |
|   8 | `test_bounds_are_refusals_not_truncations`      |   ✅   | exactly the room the block needs, then one byte short of it                 |
|   9 | `test_send_handshake_order`                     |   ✅   | Send handshake order                                                        |
|  10 | `test_receive_acks_and_delivers`                |   ✅   | Receive acks and delivers                                                   |
|  11 | `test_receive_naks_a_bad_bcc`                   |   ✅   | Receive naks a bad bcc                                                      |
|  12 | `test_priority_arbitration_on_an_stx_collision` |   ✅   | Priority arbitration on an stx collision                                    |
|  13 | `test_block_nak_retries_are_bounded`            |   ✅   | Block nak retries are bounded                                               |
|  14 | `test_qvz_timeout_retries_then_gives_up`        |   ✅   | Qvz timeout retries then gives up                                           |
|  15 | `test_zvz_inter_character_timeout_naks`         |   ✅   | Zvz inter character timeout naks                                            |
|  16 | `test_send_refuses_when_busy_or_unframeable`    |   ✅   | Send refuses when busy or unframeable                                       |
|  17 | `test_init_is_idle_and_null_callbacks_are_safe` |   ✅   | Init is idle and null callbacks are safe                                    |
|  18 | `test_tick_while_idle_does_nothing`             |   ✅   | Tick while idle does nothing                                                |
|  19 | `test_rk512_words_are_big_endian`               |   ✅   | Rk512 words are big endian                                                  |
|  20 | `test_rk512_header_round_trip`                  |   ✅   | Rk512 header round trip                                                     |
|  21 | `test_rk512_reaction_carries_status_and_data`   |   ✅   | a non-zero status is a big-endian word like every other                     |
|  22 | `test_rk512_parsers_fail_closed`                |   ✅   | Rk512 parsers fail closed                                                   |
|  23 | `test_rk512_builders_refuse_a_short_buffer`     |   ✅   | Rk512 builders refuse a short buffer                                        |
|  24 | `test_rk512_telegram_survives_3964r_framing`    |   ✅   | Rk512 telegram survives 3964r framing                                       |

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

## test_smb_crypto - native_md_kat - ✅ 7 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                              | Status | Description                           |
| --: | :-------------------------------- | :----: | :------------------------------------ |
|   1 | `test_rfc1321_md5_suite`          |   ✅   | Rfc1321 md5 suite                     |
|   2 | `test_rfc1320_md4_suite`          |   ✅   | Rfc1320 md4 suite                     |
|   3 | `test_rfc2202_hmac_md5_cases`     |   ✅   | case 1: key 0x0b x16, data "Hi There" |
|   4 | `test_long_key_is_its_own_digest` |   ✅   | Long key is its own digest            |
|   5 | `test_streaming_matches_one_shot` |   ✅   | Streaming matches one shot            |
|   6 | `test_md4_and_md5_are_distinct`   |   ✅   | Md4 and md5 are distinct              |
|   7 | `test_block_boundary_lengths`     |   ✅   | Block boundary lengths                |

</details>

---

## test_ntlm - native_ntlm_v2 - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                   | Status | Description                                                                                |
| --: | :----------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_msnlmp_ntowfv2_worked_example`                   |   ✅   | Msnlmp ntowfv2 worked example                                                              |
|   2 | `test_nt_hash_is_the_published_ntowfv1`                |   ✅   | Nt hash is the published ntowfv1                                                           |
|   3 | `test_msnlmp_ntlmv2_response_and_session_base_key`     |   ✅   | NTProofStr, MS-NLMP 4.2.4.2.2                                                              |
|   4 | `test_only_the_user_is_uppercased`                     |   ✅   | The domain is taken as given, so a different spelling is a different key.                  |
|   5 | `test_nt_hash_is_case_sensitive`                       |   ✅   | MD4 of the empty string is a fixed value, so an empty password still yields a defined hash |
|   6 | `test_response_length_is_forty_eight_plus_target_info` |   ✅   | One octet short of the needed room writes nothing.                                         |
|   7 | `test_timestamp_is_carried_and_bound_in`               |   ✅   | Timestamp is carried and bound in                                                          |
|   8 | `test_server_challenge_is_bound_into_the_proof`        |   ✅   | Server challenge is bound into the proof                                                   |
|   9 | `test_mic_flag_is_inserted_before_the_eol`             |   ✅   | The two original pairs are untouched.                                                      |
|  10 | `test_mic_flag_is_ored_into_an_existing_pair`          |   ✅   | Setting it twice is the same list: OR-ing a bit already present changes nothing.           |
|  11 | `test_mic_flag_changes_the_response_and_fails_closed`  |   ✅   | Mic flag changes the response and fails closed                                             |
|  12 | `test_mic_matches_the_rfc2202_hmac_md5_vectors`        |   ✅   | The same message split differently must give the same digest: the split is not part of it. |
|  13 | `test_mic_binds_the_key_and_every_message`             |   ✅   | Mic binds the key and every message                                                        |
|  14 | `test_ntowfv2_refuses_an_oversized_name_pair`          |   ✅   | Ntowfv2 refuses an oversized name pair                                                     |

</details>

---

## test_ntlmssp - native_ntlmssp - ✅ 9 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                  | Status | Description                                                                               |
| --: | :---------------------------------------------------- | :----: | :---------------------------------------------------------------------------------------- |
|   1 | `test_msnlmp_challenge_message`                       |   ✅   | The blob at offset 68 is the AV_PAIR list of 4.2.4.3: MsvAvNbDomainName "Domain",         |
|   2 | `test_negotiate_message_layout`                       |   ✅   | Negotiate message layout                                                                  |
|   3 | `test_negotiate_flag_bits`                            |   ✅   | The example server's flags (0xe28a8233) carry Unicode, NTLM, ExtendedSessionSecurity and  |
|   4 | `test_msnlmp_authenticate_message`                    |   ✅   | MS-NLMP 4.2.1 Common Values, UTF-16LE: "Domain", "User" and the workstation "COMPUTER".   |
|   5 | `test_authenticate_with_mic_reserves_version_and_mic` |   ✅   | 88 fixed + 0 LM + 8 NT + 2 "D" + 2 "U" + 0 workstation = 100                              |
|   6 | `test_challenge_parse_fails_closed`                   |   ✅   | Shorter than the fixed fields through TargetInfoFields (48 octets), and short of what the |
|   7 | `test_challenge_without_target_info`                  |   ✅   | Challenge without target info                                                             |
|   8 | `test_authenticate_fails_closed`                      |   ✅   | 64 + 8 + 2 + 2 = 76 with no MIC.                                                          |
|   9 | `test_absent_identity_fields`                         |   ✅   | Absent identity fields                                                                    |

</details>

---

## test_spnego - native_spnego - ✅ 8 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                                                                 |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_first_token_is_an_rfc2743_initial_context_token`          |   ✅   | First token is an rfc2743 initial context token                                             |
|   2 | `test_second_token_is_a_bare_neg_token_resp`                    |   ✅   | No InitialContextToken framing: the first octet is the [1] tag, not [APPLICATION 0].        |
|   3 | `test_response_token_is_found_after_negstate_and_supportedmech` |   ✅   | SEQUENCE content: [0] negState 5 + [1] supportedMech 14 + [2] responseToken 10 = 29 octets, |
|   4 | `test_wrap_then_parse_round_trip`                               |   ✅   | Wrap then parse round trip                                                                  |
|   5 | `test_der_length_forms`                                         |   ✅   | 127-octet token: OCTET STRING 04 7f <127> = 129; [2] a2 81 81 <129> = 132;                  |
|   6 | `test_parse_response_fails_closed`                              |   ✅   | Wrong outer tag: an InitialContextToken where a NegTokenResp belongs.                       |
|   7 | `test_wrappers_fail_closed`                                     |   ✅   | Wrappers fail closed                                                                        |
|   8 | `test_both_oids_appear_in_the_first_token`                      |   ✅   | Both oids appear in the first token                                                         |

</details>

---

## test_smbus - native_smbus - ✅ 30 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                         | Status | Description                                                                                  |
| --: | :----------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_addr_octet_carries_the_direction_bit`                  |   ✅   | The address is 7 bits, so bit 7 of the argument is dropped rather than shifted into bit 8.   |
|   2 | `test_pec_is_crc8_of_the_address_octet`                      |   ✅   | Pec is crc8 of the address octet                                                             |
|   3 | `test_pec_write_covers_address_then_payload`                 |   ✅   | Pec write covers address then payload                                                        |
|   4 | `test_pec_read_spans_both_halves_and_the_repeated_start`     |   ✅   | Pec read spans both halves and the repeated start                                            |
|   5 | `test_pec_read_without_a_command`                            |   ✅   | Pec read without a command                                                                   |
|   6 | `test_pec_binds_to_the_address`                              |   ✅   | The first address is captured before the second runs: both report through the one namespace, |
|   7 | `test_pec_binds_to_the_direction`                            |   ✅   | The write direction is captured before the read runs: both report through the one namespace. |
|   8 | `test_pec_empty_payload_still_covers_the_address`            |   ✅   | Pec empty payload still covers the address                                                   |
|   9 | `test_pec_holds_nothing_between_transactions`                |   ✅   | Pec holds nothing between transactions                                                       |
|  10 | `test_pec_flag_round_trips`                                  |   ✅   | Pec flag round trips                                                                         |
|  11 | `test_write_shapes_put_their_own_octets_on_the_wire`         |   ✅   | Write shapes put their own octets on the wire                                                |
|  12 | `test_pec_octet_is_appended_to_a_write`                      |   ✅   | Pec octet is appended to a write                                                             |
|  13 | `test_block_write_counts_the_payload`                        |   ✅   | Block write counts the payload                                                               |
|  14 | `test_block_write_refuses_over_the_protocol_cap`             |   ✅   | A zero-length block and a null payload are refused the same way.                             |
|  15 | `test_read_shapes_take_their_octets_back`                    |   ✅   | Read shapes take their octets back                                                           |
|  16 | `test_reads_refuse_a_null_destination`                       |   ✅   | Reads refuse a null destination                                                              |
|  17 | `test_block_read_refuses_a_count_over_the_capacity`          |   ✅   | Block read refuses a count over the capacity                                                 |
|  18 | `test_block_read_refuses_a_zero_count`                       |   ✅   | Block read refuses a zero count                                                              |
|  19 | `test_process_call_exchanges_a_word`                         |   ✅   | 6.5.6: the slave answers with a word it computed, not the one it was sent.                   |
|  20 | `test_a_slave_that_does_not_acknowledge_fails_the_shape`     |   ✅   | A slave that does not acknowledge fails the shape                                            |
|  21 | `test_a_wrong_pec_on_a_read_is_rejected`                     |   ✅   | The slave supplies a corrupted checksum, which is the line noise 6.4 exists to catch.        |
|  22 | `test_a_byte_round_trips_through_a_command_code`             |   ✅   | A byte round trips through a command code                                                    |
|  23 | `test_a_word_round_trips_low_octet_first`                    |   ✅   | A word round trips low octet first                                                           |
|  24 | `test_a_block_round_trips_with_its_count`                    |   ✅   | A block round trips with its count                                                           |
|  25 | `test_smbus31_the_pec_spans_the_address_octets`              |   ✅   | Smbus31 the pec spans the address octets                                                     |
|  26 | `test_smbus31_a_read_verifies_the_pec_the_slave_supplied`    |   ✅   | Smbus31 a read verifies the pec the slave supplied                                           |
|  27 | `test_smbus31_a_wrong_pec_is_not_processed`                  |   ✅   | Smbus31 a wrong pec is not processed                                                         |
|  28 | `test_smbus31_a_word_and_a_block_round_trip_with_the_pec_on` |   ✅   | Smbus31 a word and a block round trip with the pec on                                        |
|  29 | `test_smbus31_two_slaves_keep_their_own_command_codes`       |   ✅   | Smbus31 two slaves keep their own command codes                                              |
|  30 | `test_a_refused_transfer_fails_the_write`                    |   ✅   | A refused transfer fails the write                                                           |

</details>

---

## test_smtp - native_smtp - ✅ 39 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                         |
| --: | :---------------------------------------------------------- | :----: | :---------------------------------------------------------------------------------- |
|   1 | `test_happy_path_no_auth`                                   |   ✅   | Happy path no auth                                                                  |
|   2 | `test_auth_login`                                           |   ✅   | Auth login                                                                          |
|   3 | `test_auth_rejected`                                        |   ✅   | Auth rejected                                                                       |
|   4 | `test_greeting_not_ready`                                   |   ✅   | Greeting not ready                                                                  |
|   5 | `test_rcpt_rejected`                                        |   ✅   | Rcpt rejected                                                                       |
|   6 | `test_data_refused`                                         |   ✅   | Data refused                                                                        |
|   7 | `test_dot_stuffing`                                         |   ✅   | Dot stuffing                                                                        |
|   8 | `test_multiline_reply_and_lf_body`                          |   ✅   | Multiline reply and lf body                                                         |
|   9 | `test_partial_reads_dribble`                                |   ✅   | Partial reads dribble                                                               |
|  10 | `test_missing_required_arg`                                 |   ✅   | Missing required arg                                                                |
|  11 | `test_io_error_when_server_hangs`                           |   ✅   | Io error when server hangs                                                          |
|  12 | `test_reply_buffer_overflow`                                |   ✅   | Reply buffer overflow                                                               |
|  13 | `test_command_send_fails`                                   |   ✅   | Command send fails                                                                  |
|  14 | `test_body_send_fails`                                      |   ✅   | Body send fails                                                                     |
|  15 | `test_auth_secret_too_long`                                 |   ✅   | Auth secret too long                                                                |
|  16 | `test_io_error_at_each_step`                                |   ✅   | Io error at each step                                                               |
|  17 | `test_protocol_error_at_each_step`                          |   ✅   | Protocol error at each step                                                         |
|  18 | `test_command_line_overflows`                               |   ✅   | Command line overflows                                                              |
|  19 | `test_message_header_overflow`                              |   ✅   | Message header overflow                                                             |
|  20 | `test_cr_in_body_dropped`                                   |   ✅   | Cr in body dropped                                                                  |
|  21 | `test_build_message_boundary_overflows`                     |   ✅   | Build message boundary overflows                                                    |
|  22 | `test_host_smtp_send_stub`                                  |   ✅   | Host smtp send stub                                                                 |
|  23 | `test_starttls_upgrades_and_reissues_ehlo`                  |   ✅   | Starttls upgrades and reissues ehlo                                                 |
|  24 | `test_starttls_not_advertised_fails_before_auth`            |   ✅   | Starttls not advertised fails before auth                                           |
|  25 | `test_starttls_partial_keyword_is_not_a_match`              |   ✅   | Starttls partial keyword is not a match                                             |
|  26 | `test_starttls_capability_match_is_case_insensitive`        |   ✅   | Starttls capability match is case insensitive                                       |
|  27 | `test_starttls_server_refuses_the_upgrade`                  |   ✅   | Starttls server refuses the upgrade                                                 |
|  28 | `test_starttls_handshake_failure_aborts`                    |   ✅   | Starttls handshake failure aborts                                                   |
|  29 | `test_starttls_without_an_upgrade_callback_is_an_arg_error` |   ✅   | Starttls without an upgrade callback is an arg error                                |
|  30 | `test_plain_ignores_an_advertised_starttls`                 |   ✅   | Plain ignores an advertised starttls                                                |
|  31 | `test_reply_parser_skips_malformed_lines`                   |   ✅   | Reply parser skips malformed lines                                                  |
|  32 | `test_reply_bare_three_digit_line_is_final`                 |   ✅   | Reply bare three digit line is final                                                |
|  33 | `test_ehlo_capability_scan_edges`                           |   ✅   | Ehlo capability scan edges                                                          |
|  34 | `test_null_optional_fields`                                 |   ✅   | A caller that names no Domain gets SMTP_DEFAULT_CLIENT_NAME (RFC 5321 sec 4.1.1.1). |
|  35 | `test_null_password_sends_empty_secret`                     |   ✅   | Null password sends empty secret                                                    |
|  36 | `test_empty_user_skips_auth`                                |   ✅   | Empty user skips auth                                                               |
|  37 | `test_arg_validation_rejects_each_missing_field`            |   ✅   | Arg validation rejects each missing field                                           |
|  38 | `test_rcpt_251_is_accepted`                                 |   ✅   | Rcpt 251 is accepted                                                                |
|  39 | `test_command_helper_send_failure`                          |   ✅   | Command helper send failure                                                         |

</details>

---

## test_snmp_ber - native_snmp - ✅ 19 passed

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

## test_snmp_agent - native_snmp - ✅ 41 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                           | Status | Description                             |
| --: | :--------------------------------------------- | :----: | :-------------------------------------- |
|   1 | `test_get_string_v2c`                          |   ✅   | Get string v2c                          |
|   2 | `test_get_unknown_v2c_exception`               |   ✅   | Get unknown v2c exception               |
|   3 | `test_get_bad_instance_v2c_nosuchinstance`     |   ✅   | Get bad instance v2c nosuchinstance     |
|   4 | `test_get_unknown_v1_error`                    |   ✅   | Get unknown v1 error                    |
|   5 | `test_getnext_walks_to_first`                  |   ✅   | Getnext walks to first                  |
|   6 | `test_getnext_past_end_endofmibview`           |   ✅   | Getnext past end endofmibview           |
|   7 | `test_set_without_rw_community_denied`         |   ✅   | Set without rw community denied         |
|   8 | `test_set_with_rw_community_invokes_setter`    |   ✅   | Set with rw community invokes setter    |
|   9 | `test_set_readonly_not_writable`               |   ✅   | Set readonly not writable               |
|  10 | `test_getbulk_returns_multiple`                |   ✅   | Getbulk returns multiple                |
|  11 | `test_dynamic_counter_value`                   |   ✅   | Dynamic counter value                   |
|  12 | `test_uptime_is_timeticks`                     |   ✅   | Uptime is timeticks                     |
|  13 | `test_unknown_community_no_response`           |   ✅   | Unknown community no response           |
|  14 | `test_v3_message_dropped`                      |   ✅   | V3 message dropped                      |
|  15 | `test_registration_and_rw_edges`               |   ✅   | Registration and rw edges               |
|  16 | `test_ipaddress_value_encodes`                 |   ✅   | Ipaddress value encodes                 |
|  17 | `test_set_wrong_type_and_unknown`              |   ✅   | Set wrong type and unknown              |
|  18 | `test_getbulk_variants`                        |   ✅   | Getbulk variants                        |
|  19 | `test_dispatch_value_types_and_malformed`      |   ✅   | Dispatch value types and malformed      |
|  20 | `test_getbulk_repeaters_and_end`               |   ✅   | Getbulk repeaters and end               |
|  21 | `test_getbulk_nonrep_clamp_and_v1_reject`      |   ✅   | Getbulk nonrep clamp and v1 reject      |
|  22 | `test_response_too_big_reencodes`              |   ✅   | Response too big reencodes              |
|  23 | `test_version_and_community_guards`            |   ✅   | Version and community guards            |
|  24 | `test_dispatch_malformed_pdu`                  |   ✅   | Dispatch malformed pdu                  |
|  25 | `test_udp_handler_via_inject`                  |   ✅   | Udp handler via inject                  |
|  26 | `test_malformed_message_guards`                |   ✅   | Malformed message guards                |
|  27 | `test_snmp_dispatch_varbind_guards`            |   ✅   | Snmp dispatch varbind guards            |
|  28 | `test_snmp_oid_cmp_request_longer`             |   ✅   | Snmp oid cmp request longer             |
|  29 | `test_init_community_defaults`                 |   ✅   | Init community defaults                 |
|  30 | `test_empty_rw_community_clears_write`         |   ✅   | Empty rw community clears write         |
|  31 | `test_add_string_null_value`                   |   ✅   | Add string null value                   |
|  32 | `test_registration_table_limits`               |   ✅   | Registration table limits               |
|  33 | `test_getnext_picks_smallest_out_of_order`     |   ✅   | Getnext picks smallest out of order     |
|  34 | `test_set_v1_error_variants`                   |   ✅   | Set v1 error variants                   |
|  35 | `test_get_failing_getter_is_nosuchinstance`    |   ✅   | Get failing getter is nosuchinstance    |
|  36 | `test_get_short_oid_is_nosuchobject`           |   ✅   | Get short oid is nosuchobject           |
|  37 | `test_getbulk_saturates_varbind_table`         |   ✅   | Getbulk saturates varbind table         |
|  38 | `test_dispatch_truncated_pdu_fields`           |   ✅   | Dispatch truncated pdu fields           |
|  39 | `test_dispatch_empty_varbind_list_tiny_buffer` |   ✅   | Dispatch empty varbind list tiny buffer |
|  40 | `test_message_truncated_before_community`      |   ✅   | Message truncated before community      |
|  41 | `test_udp_handler_drops_unanswerable`          |   ✅   | Udp handler drops unanswerable          |

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

## test_snmp_trap - native_snmp_trap - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                        | Status | Description                                         |
| --: | :------------------------------------------ | :----: | :-------------------------------------------------- |
|   1 | `test_rfc3416_trap_variable_bindings`       |   ✅   | binding 1: sysUpTime.0 with a TimeTicks value       |
|   2 | `test_sysuptime_value_is_the_caller_uptime` |   ✅   | Sysuptime value is the caller uptime                |
|   3 | `test_rfc3416_pdu_tags`                     |   ✅   | Rfc3416 pdu tags                                    |
|   4 | `test_request_id_is_the_callers`            |   ✅   | Request id is the callers                           |
|   5 | `test_rfc1901_message_wrapper`              |   ✅   | Rfc1901 message wrapper                             |
|   6 | `test_rfc2578_varbind_value_tags`           |   ✅   | Rfc2578 varbind value tags                          |
|   7 | `test_rfc2578_ipaddress_is_four_octets`     |   ✅   | Rfc2578 ipaddress is four octets                    |
|   8 | `test_build_pdu_appends_to_an_open_encoder` |   ✅   | Build pdu appends to an open encoder                |
|   9 | `test_short_buffer_writes_nothing`          |   ✅   | Short buffer writes nothing                         |
|  10 | `test_unknown_varbind_type_fails_closed`    |   ✅   | Unknown varbind type fails closed                   |
|  11 | `test_missing_arguments_are_refused`        |   ✅   | build_pdu with no open encoder is the same refusal. |
|  12 | `test_sends_report_no_transport`            |   ✅   | Sends report no transport                           |

</details>

---

## test_snmp_trap - native_snmp_notify - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                        | Status | Description                                         |
| --: | :------------------------------------------ | :----: | :-------------------------------------------------- |
|   1 | `test_rfc3416_trap_variable_bindings`       |   ✅   | binding 1: sysUpTime.0 with a TimeTicks value       |
|   2 | `test_sysuptime_value_is_the_caller_uptime` |   ✅   | Sysuptime value is the caller uptime                |
|   3 | `test_rfc3416_pdu_tags`                     |   ✅   | Rfc3416 pdu tags                                    |
|   4 | `test_request_id_is_the_callers`            |   ✅   | Request id is the callers                           |
|   5 | `test_rfc1901_message_wrapper`              |   ✅   | Rfc1901 message wrapper                             |
|   6 | `test_rfc2578_varbind_value_tags`           |   ✅   | Rfc2578 varbind value tags                          |
|   7 | `test_rfc2578_ipaddress_is_four_octets`     |   ✅   | Rfc2578 ipaddress is four octets                    |
|   8 | `test_build_pdu_appends_to_an_open_encoder` |   ✅   | Build pdu appends to an open encoder                |
|   9 | `test_short_buffer_writes_nothing`          |   ✅   | Short buffer writes nothing                         |
|  10 | `test_unknown_varbind_type_fails_closed`    |   ✅   | Unknown varbind type fails closed                   |
|  11 | `test_missing_arguments_are_refused`        |   ✅   | build_pdu with no open encoder is the same refusal. |
|  12 | `test_sends_report_no_transport`            |   ✅   | Sends report no transport                           |

</details>

---

## test_snmp_v3 - native_snmp_v3 - ✅ 32 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------- |
|   1 | `test_localize_key_sha256_vector`               |   ✅   | Localize key sha256 vector               |
|   2 | `test_localize_key_empty_password`              |   ✅   | Localize key empty password              |
|   3 | `test_aes128_fips197_vector`                    |   ✅   | Aes128 fips197 vector                    |
|   4 | `test_aes_cfb_roundtrip_partial_block`          |   ✅   | Aes cfb roundtrip partial block          |
|   5 | `test_discovery_reports_engine_id`              |   ✅   | Discovery reports engine id              |
|   6 | `test_authnopriv_get`                           |   ✅   | Authnopriv get                           |
|   7 | `test_authpriv_get`                             |   ✅   | Authpriv get                             |
|   8 | `test_wrong_auth_password_reports_wrong_digest` |   ✅   | Wrong auth password reports wrong digest |
|   9 | `test_unknown_user_reports`                     |   ✅   | Unknown user reports                     |
|  10 | `test_not_in_time_window_reports`               |   ✅   | Not in time window reports               |
|  11 | `test_inform_v3_builds_informrequest`           |   ✅   | Inform v3 builds informrequest           |
|  12 | `test_v3_message_structure_rejections`          |   ✅   | V3 message structure rejections          |
|  13 | `test_v3_init_and_boots_accessors`              |   ✅   | V3 init and boots accessors              |
|  14 | `test_v3_discovery_variants`                    |   ✅   | V3 discovery variants                    |
|  15 | `test_v3_priv_not_configured`                   |   ✅   | V3 priv not configured                   |
|  16 | `test_v3_notify_paths`                          |   ✅   | V3 notify paths                          |
|  17 | `test_v3_field_tag_corruption`                  |   ✅   | V3 field tag corruption                  |
|  18 | `test_v3_scoped_parse_rejections`               |   ✅   | V3 scoped parse rejections               |
|  19 | `test_v3_discovery_malformed_scoped`            |   ✅   | V3 discovery malformed scoped            |
|  20 | `test_v3_auth_edge_rejections`                  |   ✅   | V3 auth edge rejections                  |
|  21 | `test_v3_notify_overflow_guards`                |   ✅   | V3 notify overflow guards                |
|  22 | `test_v3_response_scopedpdu_overflow`           |   ✅   | V3 response scopedpdu overflow           |
|  23 | `test_v3_truncated_fields_fail_closed`          |   ✅   | V3 truncated fields fail closed          |
|  24 | `test_v3_outer_tag_and_empty_flags`             |   ✅   | V3 outer tag and empty flags             |
|  25 | `test_v3_scoped_truncated_headers`              |   ✅   | V3 scoped truncated headers              |
|  26 | `test_v3_same_length_wrong_engine_id`           |   ✅   | V3 same length wrong engine id           |
|  27 | `test_v3_unknown_user_variants`                 |   ✅   | V3 unknown user variants                 |
|  28 | `test_v3_oversized_message_is_wrong_digest`     |   ✅   | V3 oversized message is wrong digest     |
|  29 | `test_v3_boots_mismatch_not_in_time`            |   ✅   | V3 boots mismatch not in time            |
|  30 | `test_v3_privacy_parameter_edges`               |   ✅   | V3 privacy parameter edges               |
|  31 | `test_v3_init_length_guards_and_null_user`      |   ✅   | V3 init length guards and null user      |
|  32 | `test_v3_trap_reports_transport_failure`        |   ✅   | V3 trap reports transport failure        |

</details>

---

## test_snp - native_snp - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                            | Status | Description                                                                              |
| --: | :---------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_published_x_attach_block_check_codes`     |   ✅   | Published x attach block check codes                                                     |
|   2 | `test_rotate_wraps_the_top_bit_into_the_bottom` |   ✅   | 'A' 'B' 'C', stepped through the same algorithm:                                         |
|   3 | `test_empty_range_is_the_seed`                  |   ✅   | Empty range is the seed                                                                  |
|   4 | `test_byte_order_changes_the_code`              |   ✅   | One result member, so the first code is taken into a local before the second call lands. |
|   5 | `test_control_characters_match_table_7_1`       |   ✅   | Control characters match table 7 1                                                       |
|   6 | `test_frame_layout`                             |   ✅   | Frame layout                                                                             |
|   7 | `test_round_trip`                               |   ✅   | Round trip                                                                               |
|   8 | `test_any_single_bit_flip_is_refused`           |   ✅   | Any single bit flip is refused                                                           |
|   9 | `test_truncation_is_refused`                    |   ✅   | Truncation is refused                                                                    |
|  10 | `test_trailing_bytes_are_ignored`               |   ✅   | Trailing bytes are ignored                                                               |
|  11 | `test_builder_refuses_bad_arguments`            |   ✅   | Builder refuses bad arguments                                                            |
|  12 | `test_parser_refuses_bad_arguments`             |   ✅   | Parser refuses bad arguments                                                             |

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

|   # | Test                                                       | Status | Description                                                                                 |
| --: | :--------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_extension_lives_in_the_last_segment`                 |   ✅   | Extension lives in the last segment                                                         |
|   2 | `test_root_serves_the_shell`                               |   ✅   | Root serves the shell                                                                       |
|   3 | `test_api_prefix_passes_through`                           |   ✅   | A path that only shares a leading substring with the prefix is not under it.                |
|   4 | `test_client_routes_get_the_shell_and_assets_get_the_file` |   ✅   | Client routes get the shell and assets get the file                                         |
|   5 | `test_route_ex_matches_plain_route_when_healthy`           |   ✅   | The plain decision is captured before the extended one runs: both report through            |
|   6 | `test_only_the_shell_decision_degrades`                    |   ✅   | Only the shell decision degrades                                                            |
|   7 | `test_stream_output_is_chunk_size_independent`             |   ✅   | Stream output is chunk size independent                                                     |
|   8 | `test_predicates_run_as_the_stream_reaches_them`           |   ✅   | Emit the first fragment, flip the flag, then continue: the second fragment is now included. |
|   9 | `test_empty_and_all_skipped_streams_finish_immediately`    |   ✅   | Empty and all skipped streams finish immediately                                            |
|  10 | `test_null_fragment_body_is_skipped`                       |   ✅   | Null fragment body is skipped                                                               |
|  11 | `test_stream_refuses_bad_arguments`                        |   ✅   | Stream refuses bad arguments                                                                |

</details>

---

## test_span - native_span - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                         | Status | Description                                                                                  |
| --: | :----------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_the_capacity_is_the_constant_it_was_bound_with`        |   ✅   | The capacity is the constant it was bound with                                               |
|   2 | `test_an_empty_region_cannot_carry_a_live_capacity`          |   ✅   | A write into the empty region stores nothing and dereferences nothing.                       |
|   3 | `test_pos_reports_the_capacity_the_payload_needed`           |   ✅   | Five more than it holds: nothing is stored, the flag latches, and pos names the size wanted. |
|   4 | `test_reset_rewinds_and_clears_the_overflow`                 |   ✅   | Reset rewinds and clears the overflow                                                        |
|   5 | `test_after_clamps_rather_than_pointing_past_the_allocation` |   ✅   | After clamps rather than pointing past the allocation                                        |
|   6 | `test_first_clamps_to_what_the_parent_holds`                 |   ✅   | First clamps to what the parent holds                                                        |
|   7 | `test_produced_is_the_cursor_and_is_empty_after_an_overflow` |   ✅   | Nothing produced yet is an empty view, not a view of the whole capacity.                     |
|   8 | `test_read_clamps_to_the_capacity`                           |   ✅   | Read clamps to the capacity                                                                  |
|   9 | `test_the_region_round_trips_through_the_paired_verbs`       |   ✅   | Past the end sets the sticky err and leaves the cursor where it was.                         |
|  10 | `test_the_table_is_wired_to_the_named_accessors`             |   ✅   | The table is wired to the named accessors                                                    |

</details>

---

## test_sparkplug - native_sparkplug - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                 |
| --: | :-------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_topic_namespace`                        |   ✅   | sec 4.1.5: the optional device_id element is the fifth level, so a Device topic carries it. |
|   2 | `test_topic_every_message_type`               |   ✅   | "spBv1.0/G/" is 10 octets, then the type, then "/N".                                        |
|   3 | `test_topic_refuses_a_missing_element`        |   ✅   | Topic refuses a missing element                                                             |
|   4 | `test_topic_refuses_a_short_buffer`           |   ✅   | Topic refuses a short buffer                                                                |
|   5 | `test_metric_wire_octets`                     |   ✅   | Metric wire octets                                                                          |
|   6 | `test_metric_alias_varint`                    |   ✅   | Metric alias varint                                                                         |
|   7 | `test_metric_string_value`                    |   ✅   | Metric string value                                                                         |
|   8 | `test_metric_float_and_double_payloads`       |   ✅   | Metric float and double payloads                                                            |
|   9 | `test_metric_boolean_payload`                 |   ✅   | Metric boolean payload                                                                      |
|  10 | `test_payload_wire_octets`                    |   ✅   | Payload wire octets                                                                         |
|  11 | `test_payload_round_trip`                     |   ✅   | metric 0                                                                                    |
|  12 | `test_decoded_strings_point_into_the_source`  |   ✅   | Decoded strings point into the source                                                       |
|  13 | `test_parse_reports_absent_header_fields`     |   ✅   | Parse reports absent header fields                                                          |
|  14 | `test_parse_rejects_a_truncated_varint`       |   ✅   | Parse rejects a truncated varint                                                            |
|  15 | `test_next_metric_rejects_an_overlong_length` |   ✅   | Next metric rejects an overlong length                                                      |
|  16 | `test_build_refuses_a_short_buffer`           |   ✅   | Build refuses a short buffer                                                                |
|  17 | `test_build_refuses_a_null_buffer`            |   ✅   | Build refuses a null buffer                                                                 |
|  18 | `test_payload_with_no_metrics`                |   ✅   | Payload with no metrics                                                                     |

</details>

---

## test_sqlite - native_storage_sqlite - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                                   |
| --: | :------------------------------------------------------ | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_fileformat_magic_header_string`                   |   ✅   | A buffer shorter than the 100-byte header cannot carry one.                                   |
|   2 | `test_fileformat_database_header_offsets`               |   ✅   | sec 1.3 also fixes what several of those values may be: the page size is what the fixture was |
|   3 | `test_fileformat_page_size_encoding`                    |   ✅   | Fileformat page size encoding                                                                 |
|   4 | `test_fileformat_varint_decoding`                       |   ✅   | The ninth byte contributes all eight of its bits, so eight 0x7f septets plus 0xff is 2^64-1.  |
|   5 | `test_fileformat_varint_length_boundaries`              |   ✅   | A buffer one byte short of the encoding is refused rather than written into.                  |
|   6 | `test_fileformat_serial_type_content_sizes`             |   ✅   | N >= 12 and even: a BLOB of (N-12)/2 bytes. N >= 13 and odd: TEXT of (N-13)/2.                |
|   7 | `test_fileformat_btree_page_header_offsets`             |   ✅   | The overflow fixture's table root is an interior table page: 12-byte header with the          |
|   8 | `test_fileformat_btree_page_type_domain`                |   ✅   | An interior page needs 12 bytes of header; 11 is short.                                       |
|   9 | `test_fileformat_cell_content_start_zero_means_65536`   |   ✅   | Fileformat cell content start zero means 65536                                                |
|  10 | `test_fileformat_cell_pointer_array`                    |   ✅   | An index at or past the cell count names no cell.                                             |
|  11 | `test_fileformat_schema_row`                            |   ✅   | Fileformat schema row                                                                         |
|  12 | `test_fileformat_column_int_is_signextended_big_endian` |   ✅   | The most negative 8-bit value, and the sign bit at each width boundary.                       |
|  13 | `test_fileformat_column_float_is_big_endian_ieee754`    |   ✅   | A value that is not eight bytes is not a serial-type-7 value.                                 |
|  14 | `test_table_cursor_walks_every_row_in_rowid_order`      |   ✅   | Table cursor walks every row in rowid order                                                   |
|  15 | `test_overflow_chain_reassembly`                        |   ✅   | Overflow chain reassembly                                                                     |
|  16 | `test_overflow_without_a_buffer_yields_the_prefix`      |   ✅   | Overflow without a buffer yields the prefix                                                   |
|  17 | `test_table_cursor_refuses_an_unreadable_root`          |   ✅   | Table cursor refuses an unreadable root                                                       |
|  18 | `test_table_cursor_walks_the_schema_table_on_page_one`  |   ✅   | Table cursor walks the schema table on page one                                               |
|  19 | `test_encode_record_round_trip`                         |   ✅   | -2.0 is sign 1, exponent 1024, zero mantissa: 0xC000000000000000, stored big-endian.          |
|  20 | `test_encode_record_picks_the_narrowest_integer_type`   |   ✅   | Encode record picks the narrowest integer type                                                |
|  21 | `test_build_table_db_writes_a_readable_file`            |   ✅   | Build table db writes a readable file                                                         |
|  22 | `test_build_table_db_fails_closed`                      |   ✅   | One 512-byte leaf page cannot hold a row far larger than itself.                              |
|  23 | `test_record_cursor_refuses_a_malformed_record`         |   ✅   | Header size 2 (itself plus one serial type), a TEXT column of 5 bytes, but no value bytes.    |
|  24 | `test_leaf_cell_refuses_a_truncated_cell`               |   ✅   | A payload length far larger than the page, at the very end of it.                             |

</details>

---

## test_ssh_ecdsa - native_ssh_ecdsa - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_ssh_ecdsa - native_ssh_ecdsa_hw - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_rsa_kat - native_rsa_kat - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                                                              |
| --: | :------------------------------------------------------------ | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_rsa_verify_sha256_wycheproof`                           |   ✅   | Rsa verify sha256 wycheproof                                                             |
|   2 | `test_rsa_verify_sha512_wycheproof`                           |   ✅   | Rsa verify sha512 wycheproof                                                             |
|   3 | `test_rsa_verify_pss_sha256_openssl`                          |   ✅   | Rsa verify pss sha256 openssl                                                            |
|   4 | `test_the_two_padding_modes_do_not_accept_each_other`         |   ✅   | The two padding modes do not accept each other                                           |
|   5 | `test_rsa_verify_rejects_the_wrong_digest`                    |   ✅   | Rsa verify rejects the wrong digest                                                      |
|   6 | `test_rsa_verify_rejects_a_tampered_message`                  |   ✅   | Rsa verify rejects a tampered message                                                    |
|   7 | `test_rsa_verify_rejects_a_signature_at_or_above_the_modulus` |   ✅   | sig += n, big-endian. A sum that carries out of the buffer is no longer congruent to sig |
|   8 | `test_rsa_verify_refuses_a_short_signature_and_null_operands` |   ✅   | Rsa verify refuses a short signature and null operands                                   |
|   9 | `test_rsa_sign_matches_openssl`                               |   ✅   | Rsa sign matches openssl                                                                 |
|  10 | `test_rsa_sign_then_verify_round_trips`                       |   ✅   | and one flipped signature octet must break it                                            |
|  11 | `test_rsa_sign_refuses_null_operands`                         |   ✅   | Rsa sign refuses null operands                                                           |
|  12 | `test_vector_tables_are_populated`                            |   ✅   | and both outcomes are represented, so neither branch of run_verify is dead               |

</details>

---

## test_rsa_kat - native_rsa_kat_hw - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                                                              |
| --: | :------------------------------------------------------------ | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_rsa_verify_sha256_wycheproof`                           |   ✅   | Rsa verify sha256 wycheproof                                                             |
|   2 | `test_rsa_verify_sha512_wycheproof`                           |   ✅   | Rsa verify sha512 wycheproof                                                             |
|   3 | `test_rsa_verify_pss_sha256_openssl`                          |   ✅   | Rsa verify pss sha256 openssl                                                            |
|   4 | `test_the_two_padding_modes_do_not_accept_each_other`         |   ✅   | The two padding modes do not accept each other                                           |
|   5 | `test_rsa_verify_rejects_the_wrong_digest`                    |   ✅   | Rsa verify rejects the wrong digest                                                      |
|   6 | `test_rsa_verify_rejects_a_tampered_message`                  |   ✅   | Rsa verify rejects a tampered message                                                    |
|   7 | `test_rsa_verify_rejects_a_signature_at_or_above_the_modulus` |   ✅   | sig += n, big-endian. A sum that carries out of the buffer is no longer congruent to sig |
|   8 | `test_rsa_verify_refuses_a_short_signature_and_null_operands` |   ✅   | Rsa verify refuses a short signature and null operands                                   |
|   9 | `test_rsa_sign_matches_openssl`                               |   ✅   | Rsa sign matches openssl                                                                 |
|  10 | `test_rsa_sign_then_verify_round_trips`                       |   ✅   | and one flipped signature octet must break it                                            |
|  11 | `test_rsa_sign_refuses_null_operands`                         |   ✅   | Rsa sign refuses null operands                                                           |
|  12 | `test_vector_tables_are_populated`                            |   ✅   | and both outcomes are represented, so neither branch of run_verify is dead               |

</details>

---

## test_bignum_group14 - native_bignum_group14 - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                        | Status | Description                                                          |
| --: | :-------------------------------------------------------------------------- | :----: | :------------------------------------------------------------------- |
|   1 | `test_group14_prime_and_generator_match_rfc3526`                            |   ✅   | Group14 prime and generator match rfc3526                            |
|   2 | `test_from_bytes_and_to_bytes_are_inverses`                                 |   ✅   | From bytes and to bytes are inverses                                 |
|   3 | `test_from_bytes_handles_a_short_and_a_long_source`                         |   ✅   | From bytes handles a short and a long source                         |
|   4 | `test_cmp_orders_two_values_and_cmp_raw_spans_the_stated_limbs`             |   ✅   | over one limb the two agree, because they differ only above it       |
|   5 | `test_is_zero_finds_every_limb_zero`                                        |   ✅   | one bit anywhere in the width is enough to make it non-zero          |
|   6 | `test_dh_validate_accepts_only_values_strictly_between_one_and_p_minus_one` |   ✅   | Dh validate accepts only values strictly between one and p minus one |
|   7 | `test_expmod_group14_matches_the_vectors`                                   |   ✅   | Expmod group14 matches the vectors                                   |
|   8 | `test_the_two_diffie_hellman_halves_agree`                                  |   ✅   | The two diffie hellman halves agree                                  |
|   9 | `test_every_entry_refuses_a_null_operand`                                   |   ✅   | Every entry refuses a null operand                                   |
|  10 | `test_vector_table_is_populated`                                            |   ✅   | Vector table is populated                                            |

</details>

---

## test_bignum_group14 - native_bignum_group14_hw - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                        | Status | Description                                                          |
| --: | :-------------------------------------------------------------------------- | :----: | :------------------------------------------------------------------- |
|   1 | `test_group14_prime_and_generator_match_rfc3526`                            |   ✅   | Group14 prime and generator match rfc3526                            |
|   2 | `test_from_bytes_and_to_bytes_are_inverses`                                 |   ✅   | From bytes and to bytes are inverses                                 |
|   3 | `test_from_bytes_handles_a_short_and_a_long_source`                         |   ✅   | From bytes handles a short and a long source                         |
|   4 | `test_cmp_orders_two_values_and_cmp_raw_spans_the_stated_limbs`             |   ✅   | over one limb the two agree, because they differ only above it       |
|   5 | `test_is_zero_finds_every_limb_zero`                                        |   ✅   | one bit anywhere in the width is enough to make it non-zero          |
|   6 | `test_dh_validate_accepts_only_values_strictly_between_one_and_p_minus_one` |   ✅   | Dh validate accepts only values strictly between one and p minus one |
|   7 | `test_expmod_group14_matches_the_vectors`                                   |   ✅   | Expmod group14 matches the vectors                                   |
|   8 | `test_the_two_diffie_hellman_halves_agree`                                  |   ✅   | The two diffie hellman halves agree                                  |
|   9 | `test_every_entry_refuses_a_null_operand`                                   |   ✅   | Every entry refuses a null operand                                   |
|  10 | `test_vector_table_is_populated`                                            |   ✅   | Vector table is populated                                            |

</details>

---

## test_ssh_sftp - native_ssh_sftp - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                             |
| --: | :------------------------------------------------------ | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_packet_length_excludes_the_length_field`          |   ✅   | Stated the other way: the length field is always the total minus the four it sits in.   |
|   2 | `test_protocol_constants`                               |   ✅   | Protocol constants                                                                      |
|   3 | `test_status_response_layout`                           |   ✅   | A null message is still a present, zero-length string: the language tag must follow it. |
|   4 | `test_handle_and_data_responses`                        |   ✅   | A zero-length DATA string is the legal way to say "nothing read", not an omitted field. |
|   5 | `test_attrs_field_order_and_presence`                   |   ✅   | With no flags set the blob is the flag word alone.                                      |
|   6 | `test_attrs_round_trip`                                 |   ✅   | The whole blob was consumed: no field was skipped and none was read twice.              |
|   7 | `test_attrs_skips_extended_fields`                      |   ✅   | Attrs skips extended fields                                                             |
|   8 | `test_name_response_layout`                             |   ✅   | Name response layout                                                                    |
|   9 | `test_frame_length`                                     |   ✅   | Frame length                                                                            |
|  10 | `test_reader_stays_failed_after_a_short_read`           |   ✅   | A u64 that needs eight octets from a seven-octet buffer fails without consuming any.    |
|  11 | `test_reader_refuses_a_string_longer_than_the_payload`  |   ✅   | A zero-length string is legal and consumes only its count.                              |
|  12 | `test_writer_overflow_is_final`                         |   ✅   | A buffer too small even for the length prefix fails at init.                            |
|  13 | `test_patch_u32_backfills_a_reserved_count`             |   ✅   | A patch aimed past the buffer writes nothing.                                           |
|  14 | `test_longname_permission_column`                       |   ✅   | Longname permission column                                                              |
|  15 | `test_longname_ignores_the_file_type_bits`              |   ✅   | Longname ignores the file type bits                                                     |
|  16 | `test_longname_carries_the_size_and_ends_with_the_name` |   ✅   | Longname carries the size and ends with the name                                        |
|  17 | `test_longname_clips_to_the_buffer`                     |   ✅   | Longname clips to the buffer                                                            |

</details>

---

## test_ssh_sftp - native_sftp_wire - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                                             |
| --: | :------------------------------------------------------ | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_packet_length_excludes_the_length_field`          |   ✅   | Stated the other way: the length field is always the total minus the four it sits in.   |
|   2 | `test_protocol_constants`                               |   ✅   | Protocol constants                                                                      |
|   3 | `test_status_response_layout`                           |   ✅   | A null message is still a present, zero-length string: the language tag must follow it. |
|   4 | `test_handle_and_data_responses`                        |   ✅   | A zero-length DATA string is the legal way to say "nothing read", not an omitted field. |
|   5 | `test_attrs_field_order_and_presence`                   |   ✅   | With no flags set the blob is the flag word alone.                                      |
|   6 | `test_attrs_round_trip`                                 |   ✅   | The whole blob was consumed: no field was skipped and none was read twice.              |
|   7 | `test_attrs_skips_extended_fields`                      |   ✅   | Attrs skips extended fields                                                             |
|   8 | `test_name_response_layout`                             |   ✅   | Name response layout                                                                    |
|   9 | `test_frame_length`                                     |   ✅   | Frame length                                                                            |
|  10 | `test_reader_stays_failed_after_a_short_read`           |   ✅   | A u64 that needs eight octets from a seven-octet buffer fails without consuming any.    |
|  11 | `test_reader_refuses_a_string_longer_than_the_payload`  |   ✅   | A zero-length string is legal and consumes only its count.                              |
|  12 | `test_writer_overflow_is_final`                         |   ✅   | A buffer too small even for the length prefix fails at init.                            |
|  13 | `test_patch_u32_backfills_a_reserved_count`             |   ✅   | A patch aimed past the buffer writes nothing.                                           |
|  14 | `test_longname_permission_column`                       |   ✅   | Longname permission column                                                              |
|  15 | `test_longname_ignores_the_file_type_bits`              |   ✅   | Longname ignores the file type bits                                                     |
|  16 | `test_longname_carries_the_size_and_ends_with_the_name` |   ✅   | Longname carries the size and ends with the name                                        |
|  17 | `test_longname_clips_to_the_buffer`                     |   ✅   | Longname clips to the buffer                                                            |

</details>

---

## test_statsd - native_statsd - ✅ 15 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                       | Status | Description                         |
| --: | :----------------------------------------- | :----: | :---------------------------------- |
|   1 | `test_format_types`                        |   ✅   | Format types                        |
|   2 | `test_format_sample_rate`                  |   ✅   | Format sample rate                  |
|   3 | `test_format_tags_and_both`                |   ✅   | Format tags and both                |
|   4 | `test_format_guards`                       |   ✅   | Format guards                       |
|   5 | `test_emit_counter_and_negative`           |   ✅   | Emit counter and negative           |
|   6 | `test_emit_gauge_and_delta`                |   ✅   | Emit gauge and delta                |
|   7 | `test_emit_timing_set_sampled`             |   ✅   | Emit timing set sampled             |
|   8 | `test_emit_global_tags`                    |   ✅   | Emit global tags                    |
|   9 | `test_emit_noop_until_begin`               |   ✅   | Emit noop until begin               |
|  10 | `test_rate_clamp_and_stage_overflow`       |   ✅   | Rate clamp and stage overflow       |
|  11 | `test_format_guard_null_out_and_zero_cap`  |   ✅   | Format guard null out and zero cap  |
|  12 | `test_format_append_chain_overflow_points` |   ✅   | Format append chain overflow points |
|  13 | `test_format_rate_zero_and_empty_tags`     |   ✅   | Format rate zero and empty tags     |
|  14 | `test_emit_zero_value_and_set_null_member` |   ✅   | Emit zero value and set null member |
|  15 | `test_emit_overlong_name_is_noop`          |   ✅   | Emit overlong name is noop          |

</details>

---

## test_stomp - native_stomp - ✅ 21 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                        | Status | Description                                                                      |
| --: | :------------------------------------------ | :----: | :------------------------------------------------------------------------------- |
|   1 | `test_published_error_frame`                |   ✅   | sizeof carries the terminating NUL, which is the frame's own NULL octet (sec 9). |
|   2 | `test_published_send_frame`                 |   ✅   | Published send frame                                                             |
|   3 | `test_repeated_header_first_entry_wins`     |   ✅   | Repeated header first entry wins                                                 |
|   4 | `test_published_connect_frame`              |   ✅   | Published connect frame                                                          |
|   5 | `test_build_emits_the_published_send_frame` |   ✅   | Build emits the published send frame                                             |
|   6 | `test_build_minimal_frame`                  |   ✅   | Build minimal frame                                                              |
|   7 | `test_build_escapes_a_header`               |   ✅   | Build escapes a header                                                           |
|   8 | `test_unescape_the_four_transformations`    |   ✅   | Unescape the four transformations                                                |
|   9 | `test_unescape_rejects_an_undefined_escape` |   ✅   | Unescape rejects an undefined escape                                             |
|  10 | `test_eol_accepts_an_optional_cr`           |   ✅   | Eol accepts an optional cr                                                       |
|  11 | `test_leading_eols_are_consumed`            |   ✅   | Leading eols are consumed                                                        |
|  12 | `test_content_length_reads_null_octets`     |   ✅   | Content length reads null octets                                                 |
|  13 | `test_content_length_must_land_on_the_null` |   ✅   | Content length must land on the null                                             |
|  14 | `test_incomplete_frame_is_refused`          |   ✅   | Incomplete frame is refused                                                      |
|  15 | `test_header_without_a_colon_is_refused`    |   ✅   | Header without a colon is refused                                                |
|  16 | `test_only_eols_is_not_a_frame`             |   ✅   | Only eols is not a frame                                                         |
|  17 | `test_header_lookup_misses`                 |   ✅   | Header lookup misses                                                             |
|  18 | `test_build_refuses_a_short_buffer`         |   ✅   | Build refuses a short buffer                                                     |
|  19 | `test_build_refuses_missing_arguments`      |   ✅   | Build refuses missing arguments                                                  |
|  20 | `test_parse_slices_the_source`              |   ✅   | Parse slices the source                                                          |
|  21 | `test_build_parse_round_trip`               |   ✅   | Build parse round trip                                                           |

</details>

---

## test_sunspec - native_sunspec - ✅ 12 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                                    |
| --: | :--------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_sunspec_identifier_is_the_ascii_marker`        |   ✅   | "Suns" differs from "SunS" in one bit of one octet and is not the identifier.                  |
|   2 | `test_begin_positions_past_the_two_marker_registers` |   ✅   | Begin positions past the two marker registers                                                  |
|   3 | `test_walks_the_model_chain_to_the_end_model`        |   ✅   | 0xFFFF terminates the chain; the cursor is left on the end model rather than advanced past it. |
|   4 | `test_truncated_body_is_refused`                     |   ✅   | A header cut in half is refused too.                                                           |
|   5 | `test_typed_point_readers_are_big_endian`            |   ✅   | registers 2..3 = 0x000186A0 = 100000                                                           |
|   6 | `test_string_point_stops_at_the_nul_padding`         |   ✅   | A field with no padding: all 4 octets are content.                                             |
|   7 | `test_writer_and_walker_round_trip`                  |   ✅   | The identifier the writer laid down is the ASCII one.                                          |
|   8 | `test_end_model_is_two_registers`                    |   ✅   | End model is two registers                                                                     |
|   9 | `test_write_string_is_clamped_to_the_field`          |   ✅   | Write string is clamped to the field                                                           |
|  10 | `test_overflow_latches_and_finish_reports_zero`      |   ✅   | A string that does not fit latches the same way.                                               |
|  11 | `test_signed_points_round_trip_at_the_extremes`      |   ✅   | -32768 is 0x8000 big-endian: the sign octet leads.                                             |
|  12 | `test_next_model_guards`                             |   ✅   | A model may legally declare length 0: header only, empty body, cursor advances by 4.           |

</details>

---

## test_swar - native_swar - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                          | Status | Description                                                                                    |
| --: | :------------------------------------------------------------ | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_the_lane_constants_derive_from_the_carrier_width`       |   ✅   | The lane constants derive from the carrier width                                               |
|   2 | `test_has_zero_is_exact_in_every_lane`                        |   ✅   | The regression the exact form exists for: a zero lane next to a lane holding 0x01. A borrow    |
|   3 | `test_zero_lane_is_the_first_in_address_order`                |   ✅   | With two zero lanes the answer is the earlier address, not the other one.                      |
|   4 | `test_eq_matches_equality_on_every_byte`                      |   ✅   | Eq matches equality on every byte                                                              |
|   5 | `test_eq_ci_matches_the_scalar_rule_on_every_byte`            |   ✅   | The named pairs, stated rather than left implicit in the sweep above.                          |
|   6 | `test_the_lane_tests_are_independent_across_lanes`            |   ✅   | The syndrome is zero in exactly the lanes that match, so ORing two exact masks answers for two |
|   7 | `test_ge_and_le_match_the_scalar_compares_on_seven_bit_lanes` |   ✅   | The window the base64 decoder classifies with: A-Z selected and nothing else, per lane.        |
|   8 | `test_spread_widens_a_guard_mask_without_carrying`            |   ✅   | Spread widens a guard mask without carrying                                                    |
|   9 | `test_sub7_is_the_lane_local_subtraction`                     |   ✅   | (0x80 \| a) - lo, keeping the low seven bits: the guard bit absorbs the borrow.                |
|  10 | `test_the_two_loads_agree_where_both_are_legal`               |   ✅   | Whatever the alignment, the unaligned load reads the bytes that are there.                     |
|  11 | `test_the_table_is_wired_to_the_named_lane_tests`             |   ✅   | The table is wired to the named lane tests                                                     |

</details>

---

## test_syslog - native_syslog - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                      | Status | Description                        |
| --: | :---------------------------------------- | :----: | :--------------------------------- |
|   1 | `test_pri_local0_info`                    |   ✅   | Pri local0 info                    |
|   2 | `test_pri_computation_varies`             |   ✅   | Pri computation varies             |
|   3 | `test_nilvalue_for_empty_fields`          |   ✅   | Nilvalue for empty fields          |
|   4 | `test_empty_message_ok`                   |   ✅   | Empty message ok                   |
|   5 | `test_overflow_returns_zero`              |   ✅   | Overflow returns zero              |
|   6 | `test_length_matches_strlen`              |   ✅   | Length matches strlen              |
|   7 | `test_init_and_log_captured`              |   ✅   | Init and log captured              |
|   8 | `test_log_not_ready_when_no_server`       |   ✅   | Log not ready when no server       |
|   9 | `test_format_null_and_pri_clamp`          |   ✅   | Format null and pri clamp          |
|  10 | `test_init_truncates_long_fields`         |   ✅   | Init truncates long fields         |
|  11 | `test_init_empty_server_ip_not_ready`     |   ✅   | Init empty server ip not ready     |
|  12 | `test_format_hostname_empty_appname_null` |   ✅   | Format hostname empty appname null |
|  13 | `test_format_append_boundaries`           |   ✅   | Format append boundaries           |
|  14 | `test_log_overflow_when_ready`            |   ✅   | Log overflow when ready            |

</details>

---

## test_tcp_callbacks - native_tcp - ✅ 5 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_accept_wires_every_callback_on_the_pcb`              |   ✅   | Accept wires every callback on the pcb              |
|   2 | `test_delivery_through_the_wired_callback_fills_the_ring`  |   ✅   | Delivery through the wired callback fills the ring  |
|   3 | `test_peer_fin_through_the_wired_callback_closes_the_slot` |   ✅   | Peer fin through the wired callback closes the slot |
|   4 | `test_marshaled_send_reaches_the_capture`                  |   ✅   | Marshaled send reaches the capture                  |
|   5 | `test_marshaled_close_releases_the_slot`                   |   ✅   | Marshaled close releases the slot                   |

</details>

---

## test_tcp_client - native_tcp_client - ✅ 25 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                 | Status | Description                                                           |
| --: | :------------------------------------------------------------------- | :----: | :-------------------------------------------------------------------- |
|   1 | `test_open_to_a_literal_address_connects_without_a_query`            |   ✅   | Open to a literal address connects without a query                    |
|   2 | `test_open_rejects_a_null_host`                                      |   ✅   | Open rejects a null host                                              |
|   3 | `test_open_reports_a_full_pool`                                      |   ✅   | Open reports a full pool                                              |
|   4 | `test_slots_are_distinct_and_returned_on_close`                      |   ✅   | Slots are distinct and returned on close                              |
|   5 | `test_open_closes_the_slot_when_no_control_block_is_available`       |   ✅   | Open closes the slot when no control block is available               |
|   6 | `test_a_refused_connect_closes_the_slot`                             |   ✅   | A refused connect closes the slot                                     |
|   7 | `test_the_open_timeout_bounds_the_name_lookup`                       |   ✅   | The open timeout bounds the name lookup                               |
|   8 | `test_an_unknown_connection_id_is_inert`                             |   ✅   | An unknown connection id is inert                                     |
|   9 | `test_an_unopened_slot_reports_no_connection`                        |   ✅   | An unopened slot reports no connection                                |
|  10 | `test_send_reaches_the_wire`                                         |   ✅   | Send reaches the wire                                                 |
|  11 | `test_send_is_refused_once_the_connection_is_gone`                   |   ✅   | Send is refused once the connection is gone                           |
|  12 | `test_send_reports_a_full_send_buffer`                               |   ✅   | Send reports a full send buffer                                       |
|  13 | `test_delivered_bytes_are_readable_in_order`                         |   ✅   | Delivered bytes are readable in order                                 |
|  14 | `test_arrival_alone_reopens_no_window`                               |   ✅   | Arrival alone reopens no window                                       |
|  15 | `test_the_window_reopens_by_exactly_what_was_read`                   |   ✅   | The window reopens by exactly what was read                           |
|  16 | `test_an_empty_read_reopens_no_window`                               |   ✅   | An empty read reopens no window                                       |
|  17 | `test_a_segment_that_will_not_fit_is_refused_whole`                  |   ✅   | Fill the ring to one byte short of its usable capacity.               |
|  18 | `test_a_multi_buffer_segment_is_reassembled_in_order`                |   ✅   | A multi buffer segment is reassembled in order                        |
|  19 | `test_a_short_read_leaves_the_remainder_queued`                      |   ✅   | A short read leaves the remainder queued                              |
|  20 | `test_a_peer_fin_closes_the_slot`                                    |   ✅   | A peer fin closes the slot                                            |
|  21 | `test_the_error_callback_drops_the_control_block_without_reusing_it` |   ✅   | Closing afterwards is safe: there is no control block left to act on. |
|  22 | `test_close_unwires_the_stack_callbacks`                             |   ✅   | Close unwires the stack callbacks                                     |
|  23 | `test_close_falls_back_to_a_reset`                                   |   ✅   | Close falls back to a reset                                           |
|  24 | `test_close_is_idempotent`                                           |   ✅   | Close is idempotent                                                   |
|  25 | `test_a_reopened_slot_starts_clean`                                  |   ✅   | A reopened slot starts clean                                          |

</details>

---

## test_tcp_conn - native_tcp_conn - ✅ 59 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                | Status | Description                                                                           |
| --: | :------------------------------------------------------------------ | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_recv_does_not_reopen_the_window_on_copy`                      |   ✅   | Recv does not reopen the window on copy                                               |
|   2 | `test_window_reopens_by_exactly_the_bytes_consumed`                 |   ✅   | Bytes still unread stay charged against the window.                                   |
|   3 | `test_ack_with_nothing_consumed_issues_no_window_update`            |   ✅   | Ack with nothing consumed issues no window update                                     |
|   4 | `test_reopened_window_never_exceeds_bytes_consumed`                 |   ✅   | Unread bytes plus the space handed back never exceeds the buffer.                     |
|   5 | `test_ack_at_maximum_occupancy_reports_the_whole_ring`              |   ✅   | Ack at maximum occupancy reports the whole ring                                       |
|   6 | `test_ack_is_correct_across_the_ring_wrap`                          |   ✅   | Push the cursors close to the wrap point and drain, so the next segment straddles it. |
|   7 | `test_oversized_segment_is_refused_whole_and_opens_no_window`       |   ✅   | Oversized segment is refused whole and opens no window                                |
|   8 | `test_refused_segment_does_not_refresh_the_idle_timer`              |   ✅   | Refused segment does not refresh the idle timer                                       |
|   9 | `test_accepted_segment_refreshes_the_idle_timer`                    |   ✅   | Accepted segment refreshes the idle timer                                             |
|  10 | `test_ack_consumed_is_inert_off_the_active_state`                   |   ✅   | Ack consumed is inert off the active state                                            |
|  11 | `test_ack_consumed_rejects_an_out_of_range_slot`                    |   ✅   | Ack consumed rejects an out of range slot                                             |
|  12 | `test_close_dwells_while_the_peer_still_owes_an_ack`                |   ✅   | Peer acks the last of it; the sent callback finalizes.                                |
|  13 | `test_close_with_a_drained_queue_releases_immediately`              |   ✅   | Close with a drained queue releases immediately                                       |
|  14 | `test_sent_callback_does_not_release_a_slot_that_is_still_draining` |   ✅   | Sent callback does not release a slot that is still draining                          |
|  15 | `test_dwell_expiry_aborts_the_connection`                           |   ✅   | Dwell expiry aborts the connection                                                    |
|  16 | `test_dwell_is_not_reaped_before_its_deadline`                      |   ✅   | Dwell is not reaped before its deadline                                               |
|  17 | `test_begin_close_is_a_no_op_off_the_active_state`                  |   ✅   | Begin close is a no op off the active state                                           |
|  18 | `test_data_after_close_resets_the_connection`                       |   ✅   | Data after close resets the connection                                                |
|  19 | `test_a_zero_length_segment_during_the_dwell_does_not_reset`        |   ✅   | A zero length segment during the dwell does not reset                                 |
|  20 | `test_peer_fin_during_the_dwell_leaves_the_slot_closing`            |   ✅   | Peer fin during the dwell leaves the slot closing                                     |
|  21 | `test_remote_fin_reports_a_normal_close`                            |   ✅   | Remote fin reports a normal close                                                     |
|  22 | `test_stack_error_reports_an_abort`                                 |   ✅   | Stack error reports an abort                                                          |
|  23 | `test_normal_close_and_abort_are_distinct_signals`                  |   ✅   | Normal close and abort are distinct signals                                           |
|  24 | `test_error_callback_does_not_touch_the_freed_control_block`        |   ✅   | Error callback does not touch the freed control block                                 |
|  25 | `test_error_during_the_dwell_posts_no_further_event`                |   ✅   | Error during the dwell posts no further event                                         |
|  26 | `test_callbacks_tolerate_a_null_slot_argument`                      |   ✅   | Callbacks tolerate a null slot argument                                               |
|  27 | `test_recv_on_a_free_slot_is_refused`                               |   ✅   | Recv on a free slot is refused                                                        |
|  28 | `test_idle_timeout_aborts_the_connection_and_signals_an_error`      |   ✅   | Idle timeout aborts the connection and signals an error                               |
|  29 | `test_idle_timeout_does_not_fire_before_its_deadline`               |   ✅   | Idle timeout does not fire before its deadline                                        |
|  30 | `test_idle_timeout_survives_the_millisecond_counter_wrap`           |   ✅   | Idle timeout survives the millisecond counter wrap                                    |
|  31 | `test_sweep_skips_slots_owned_by_another_worker`                    |   ✅   | Sweep skips slots owned by another worker                                             |
|  32 | `test_touch_active_defers_the_sweep_for_a_streaming_response`       |   ✅   | Touch active defers the sweep for a streaming response                                |
|  33 | `test_touch_active_is_bounded_and_state_guarded`                    |   ✅   | Touch active is bounded and state guarded                                             |
|  34 | `test_allocator_takes_the_lowest_free_slot`                         |   ✅   | Allocator takes the lowest free slot                                                  |
|  35 | `test_a_held_slot_is_not_allocatable_though_it_reads_free`          |   ✅   | A held slot is not allocatable though it reads free                                   |
|  36 | `test_allocator_reports_exhaustion_when_every_slot_is_taken`        |   ✅   | Allocator reports exhaustion when every slot is taken                                 |
|  37 | `test_free_bitmap_tracks_every_state_write`                         |   ✅   | Free bitmap tracks every state write                                                  |
|  38 | `test_set_state_ignores_a_slot_past_the_pool`                       |   ✅   | Set state ignores a slot past the pool                                                |
|  39 | `test_active_count_counts_only_live_slots`                          |   ✅   | Active count counts only live slots                                                   |
|  40 | `test_active_predicate_requires_both_the_state_and_a_control_block` |   ✅   | Active predicate requires both the state and a control block                          |
|  41 | `test_init_leaves_every_slot_free_and_indexed`                      |   ✅   | Init leaves every slot free and indexed                                               |
|  42 | `test_init_takes_the_idle_bound_it_is_given`                        |   ✅   | Init takes the idle bound it is given                                                 |
|  43 | `test_stop_resets_live_slots_and_leaves_the_pool_free`              |   ✅   | Stop resets live slots and leaves the pool free                                       |
|  44 | `test_send_on_a_torn_down_slot_is_refused`                          |   ✅   | Send on a torn down slot is refused                                                   |
|  45 | `test_send_writes_through_to_the_wire`                              |   ✅   | Send writes through to the wire                                                       |
|  46 | `test_raw_send_rejects_a_null_control_block`                        |   ✅   | Raw send rejects a null control block                                                 |
|  47 | `test_sndbuf_reports_zero_without_a_control_block`                  |   ✅   | Sndbuf reports zero without a control block                                           |
|  48 | `test_close_frees_the_slot_before_handing_the_pcb_to_the_stack`     |   ✅   | Close frees the slot before handing the pcb to the stack                              |
|  49 | `test_close_falls_back_to_a_reset_when_the_fin_cannot_be_queued`    |   ✅   | Close falls back to a reset when the fin cannot be queued                             |
|  50 | `test_close_and_abort_slot_ignore_out_of_range_and_empty_slots`     |   ✅   | Close and abort slot ignore out of range and empty slots                              |
|  51 | `test_abort_slot_resets_the_connection_and_frees_the_slot`          |   ✅   | Abort slot resets the connection and frees the slot                                   |
|  52 | `test_remote_address_is_reported_only_for_a_live_connection`        |   ✅   | Remote address is reported only for a live connection                                 |
|  53 | `test_remote_address_rejects_a_null_output_and_a_bad_slot`          |   ✅   | Remote address rejects a null output and a bad slot                                   |
|  54 | `test_peek_reads_without_consuming`                                 |   ✅   | Peek reads without consuming                                                          |
|  55 | `test_read_byte_reports_an_empty_ring`                              |   ✅   | Read byte reports an empty ring                                                       |
|  56 | `test_a_multi_buffer_segment_is_reassembled_in_order`               |   ✅   | A multi buffer segment is reassembled in order                                        |
|  57 | `test_zero_length_segment_posts_no_event`                           |   ✅   | Zero length segment posts no event                                                    |
|  58 | `test_accepted_data_posts_its_length`                               |   ✅   | Accepted data posts its length                                                        |
|  59 | `test_a_refused_segment_still_wakes_the_reader`                     |   ✅   | A refused segment still wakes the reader                                              |

</details>

---

## test_tcp_evt - native_tcp_evt - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                       | Status | Description                                         |
| --: | :--------------------------------------------------------- | :----: | :-------------------------------------------------- |
|   1 | `test_the_event_type_is_one_byte`                          |   ✅   | The event type is one byte                          |
|   2 | `test_the_four_event_kinds_are_distinct`                   |   ✅   | The four event kinds are distinct                   |
|   3 | `test_every_event_kind_fits_one_byte`                      |   ✅   | Every event kind fits one byte                      |
|   4 | `test_a_normal_close_and_an_abort_are_different_kinds`     |   ✅   | A normal close and an abort are different kinds     |
|   5 | `test_the_slot_field_can_name_every_pool_slot`             |   ✅   | The slot field can name every pool slot             |
|   6 | `test_the_length_field_holds_a_full_ring`                  |   ✅   | The length field holds a full ring                  |
|   7 | `test_the_fields_are_independent`                          |   ✅   | The fields are independent                          |
|   8 | `test_a_listener_reserves_storage_for_the_depth_it_claims` |   ✅   | A listener reserves storage for the depth it claims |
|   9 | `test_the_queue_depth_covers_a_burst_from_every_slot`      |   ✅   | The queue depth covers a burst from every slot      |
|  10 | `test_a_record_survives_a_round_trip_through_a_queue`      |   ✅   | A record survives a round trip through a queue      |
|  11 | `test_records_are_drained_in_the_order_they_were_posted`   |   ✅   | Records are drained in the order they were posted   |
|  12 | `test_every_event_kind_survives_the_queue`                 |   ✅   | Every event kind survives the queue                 |
|  13 | `test_an_empty_queue_yields_nothing`                       |   ✅   | An empty queue yields nothing                       |

</details>

---

## test_tcp_listener - native_tcp_listener - ✅ 52 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                 | Status | Description                                                                    |
| --: | :------------------------------------------------------------------- | :----: | :----------------------------------------------------------------------------- |
|   1 | `test_accept_claims_a_slot_and_wires_the_connection`                 |   ✅   | The request deadline is not armed here and is not the transport's: it lives in |
|   2 | `test_accept_resets_the_ring_of_a_reused_slot`                       |   ✅   | Accept resets the ring of a reused slot                                        |
|   3 | `test_accept_posts_a_connect_event`                                  |   ✅   | Accept posts a connect event                                                   |
|   4 | `test_successive_accepts_take_distinct_slots`                        |   ✅   | Successive accepts take distinct slots                                         |
|   5 | `test_accept_resets_the_connection_when_the_pool_is_full`            |   ✅   | Accept resets the connection when the pool is full                             |
|   6 | `test_accept_rejects_a_failed_handshake_or_a_null_control_block`     |   ✅   | Accept rejects a failed handshake or a null control block                      |
|   7 | `test_accept_rejects_a_listener_index_past_the_pool`                 |   ✅   | Accept rejects a listener index past the pool                                  |
|   8 | `test_accept_tags_the_ingress_interface`                             |   ✅   | Accept tags the ingress interface                                              |
|   9 | `test_accept_tags_a_station_connection_when_no_access_point_matches` |   ✅   | Accept tags a station connection when no access point matches                  |
|  10 | `test_each_listener_stamps_its_own_protocol`                         |   ✅   | Each listener stamps its own protocol                                          |
|  11 | `test_accept_survives_a_full_event_queue`                            |   ✅   | Accept survives a full event queue                                             |
|  12 | `test_a_listener_dscp_marks_the_connections_it_accepts`              |   ✅   | A listener dscp marks the connections it accepts                               |
|  13 | `test_a_listener_dscp_is_masked_to_six_bits`                         |   ✅   | A listener dscp is masked to six bits                                          |
|  14 | `test_setting_a_dscp_on_an_unbound_port_reports_failure`             |   ✅   | Setting a dscp on an unbound port reports failure                              |
|  15 | `test_an_unmarked_listener_leaves_the_ds_field_clear`                |   ✅   | An unmarked listener leaves the ds field clear                                 |
|  16 | `test_the_global_throttle_spends_and_refills_its_window`             |   ✅   | The global throttle spends and refills its window                              |
|  17 | `test_the_global_throttle_survives_the_counter_wrap`                 |   ✅   | The global throttle survives the counter wrap                                  |
|  18 | `test_an_accept_over_the_global_budget_is_reset`                     |   ✅   | An accept over the global budget is reset                                      |
|  19 | `test_each_address_has_its_own_budget`                               |   ✅   | Each address has its own budget                                                |
|  20 | `test_an_address_family_does_not_share_a_bucket`                     |   ✅   | An address family does not share a bucket                                      |
|  21 | `test_an_unspecified_address_defers_to_the_global_throttle`          |   ✅   | An unspecified address defers to the global throttle                           |
|  22 | `test_the_bucket_table_is_bounded_and_evicts`                        |   ✅   | The bucket table is bounded and evicts                                         |
|  23 | `test_a_per_address_window_refills`                                  |   ✅   | A per address window refills                                                   |
|  24 | `test_an_empty_allowlist_admits_everything`                          |   ✅   | An empty allowlist admits everything                                           |
|  25 | `test_a_prefix_matches_on_its_network_bits_alone`                    |   ✅   | A prefix matches on its network bits alone                                     |
|  26 | `test_host_bits_in_a_rule_are_ignored`                               |   ✅   | Host bits in a rule are ignored                                                |
|  27 | `test_a_zero_length_prefix_matches_every_address`                    |   ✅   | A zero length prefix matches every address                                     |
|  28 | `test_a_full_length_prefix_matches_one_host`                         |   ✅   | A full length prefix matches one host                                          |
|  29 | `test_a_rule_never_matches_across_families`                          |   ✅   | A rule never matches across families                                           |
|  30 | `test_any_matching_rule_admits_the_address`                          |   ✅   | Any matching rule admits the address                                           |
|  31 | `test_a_cidr_string_parses_its_address_and_prefix`                   |   ✅   | A cidr string parses its address and prefix                                    |
|  32 | `test_a_bare_address_is_a_host_route`                                |   ✅   | A bare address is a host route                                                 |
|  33 | `test_a_prefix_wider_than_the_family_is_rejected`                    |   ✅   | A prefix wider than the family is rejected                                     |
|  34 | `test_malformed_rules_are_rejected`                                  |   ✅   | Malformed rules are rejected                                                   |
|  35 | `test_the_rule_table_is_bounded`                                     |   ✅   | The rule table is bounded                                                      |
|  36 | `test_an_address_outside_the_allowlist_is_reset_at_accept`           |   ✅   | An address outside the allowlist is reset at accept                            |
|  37 | `test_add_binds_a_port_and_creates_its_queue`                        |   ✅   | Add binds a port and creates its queue                                         |
|  38 | `test_add_rejects_an_index_past_the_pool`                            |   ✅   | Add rejects an index past the pool                                             |
|  39 | `test_stop_deactivates_the_listener_and_releases_its_queue`          |   ✅   | Stop deactivates the listener and releases its queue                           |
|  40 | `test_stop_tolerates_an_index_past_the_pool_and_an_inactive_row`     |   ✅   | Stop tolerates an index past the pool and an inactive row                      |
|  41 | `test_stop_all_clears_every_listener`                                |   ✅   | Stop all clears every listener                                                 |
|  42 | `test_add_replaces_an_active_listener`                               |   ✅   | Add replaces an active listener                                                |
|  43 | `test_add_unwinds_when_the_queue_cannot_be_created`                  |   ✅   | Add unwinds when the queue cannot be created                                   |
|  44 | `test_add_unwinds_and_releases_its_queue_when_the_bind_fails`        |   ✅   | Add unwinds and releases its queue when the bind fails                         |
|  45 | `test_add_unwinds_when_the_control_block_pool_is_spent`              |   ✅   | Add unwinds when the control block pool is spent                               |
|  46 | `test_add_unwinds_when_the_listen_call_fails`                        |   ✅   | Add unwinds when the listen call fails                                         |
|  47 | `test_a_dynamic_listener_binds_and_stops`                            |   ✅   | A dynamic listener binds and stops                                             |
|  48 | `test_enqueue_delivers_an_event_to_the_listener_queue`               |   ✅   | Enqueue delivers an event to the listener queue                                |
|  49 | `test_enqueue_rejects_a_null_event_and_a_slot_past_the_pool`         |   ✅   | Enqueue rejects a null event and a slot past the pool                          |
|  50 | `test_enqueue_rejects_an_unknown_or_inactive_listener`               |   ✅   | Enqueue rejects an unknown or inactive listener                                |
|  51 | `test_enqueue_reports_a_full_queue`                                  |   ✅   | Enqueue reports a full queue                                                   |
|  52 | `test_each_listener_owns_its_own_queue`                              |   ✅   | Each listener owns its own queue                                               |

</details>

---

## test_tcp - native_tcp_ns - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                            | Status | Description                                              |
| --: | :-------------------------------------------------------------- | :----: | :------------------------------------------------------- |
|   1 | `test_each_half_points_at_the_module_that_owns_it`              |   ✅   | Each half points at the module that owns it              |
|   2 | `test_no_member_of_the_pool_half_is_unbound`                    |   ✅   | No member of the pool half is unbound                    |
|   3 | `test_no_member_of_the_listener_half_is_unbound`                |   ✅   | No member of the listener half is unbound                |
|   4 | `test_the_seam_below_the_pool_owns_the_raw_control_block_calls` |   ✅   | The seam below the pool owns the raw control block calls |
|   5 | `test_alloc_free_reports_a_slot_index`                          |   ✅   | Alloc free reports a slot index                          |
|   6 | `test_set_state_writes_the_state_it_is_given`                   |   ✅   | Set state writes the state it is given                   |
|   7 | `test_timeout_ms_reports_what_init_was_configured_with`         |   ✅   | Timeout ms reports what init was configured with         |
|   8 | `test_active_count_counts_and_stop_clears`                      |   ✅   | Active count counts and stop clears                      |
|   9 | `test_sndbuf_reports_the_room_the_stack_offers`                 |   ✅   | Sndbuf reports the room the stack offers                 |
|  10 | `test_send_and_send_flush_both_reach_the_wire`                  |   ✅   | Send and send flush both reach the wire                  |
|  11 | `test_raw_send_writes_to_a_control_block_with_no_slot`          |   ✅   | Raw send writes to a control block with no slot          |
|  12 | `test_flush_is_accepted_on_a_live_slot`                         |   ✅   | Flush is accepted on a live slot                         |
|  13 | `test_touch_active_moves_the_idle_stamp`                        |   ✅   | Touch active moves the idle stamp                        |
|  14 | `test_close_frees_the_slot_and_abort_slot_resets_it`            |   ✅   | Close frees the slot and abort slot resets it            |
|  15 | `test_begin_close_takes_the_slot_into_the_drain_dwell`          |   ✅   | Begin close takes the slot into the drain dwell          |
|  16 | `test_check_timeouts_reaps_a_stale_slot`                        |   ✅   | Check timeouts reaps a stale slot                        |
|  17 | `test_ack_consumed_reopens_the_window`                          |   ✅   | Ack consumed reopens the window                          |
|  18 | `test_detach_and_abort_act_on_a_bare_control_block`             |   ✅   | Detach and abort act on a bare control block             |
|  19 | `test_set_ttl_takes_a_code_point_and_refuses_zero`              |   ✅   | Set ttl takes a code point and refuses zero              |
|  20 | `test_remote_ip_and_remote_addr_report_the_peer`                |   ✅   | Remote ip and remote addr report the peer                |
|  21 | `test_add_binds_a_port_and_stop_takes_it_down`                  |   ✅   | Add binds a port and stop takes it down                  |
|  22 | `test_add_dynamic_and_stop_dynamic_are_their_own_pair`          |   ✅   | Add dynamic and stop dynamic are their own pair          |
|  23 | `test_set_dscp_installs_a_code_point_on_the_port_it_names`      |   ✅   | Set dscp installs a code point on the port it names      |
|  24 | `test_stop_all_takes_every_listener_down`                       |   ✅   | Stop all takes every listener down                       |

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

## test_telnet - native_telnet - ✅ 24 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                                                   |
| --: | :-------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_accept_negotiates_echo_and_sga`         |   ✅   | Accept negotiates echo and sga                                                                |
|   2 | `test_line_echoed_and_dispatched`             |   ✅   | Line echoed and dispatched                                                                    |
|   3 | `test_backspace_first_line`                   |   ✅   | Backspace first line                                                                          |
|   4 | `test_iac_will_gets_dont`                     |   ✅   | Iac will gets dont                                                                            |
|   5 | `test_iac_do_unsupported_gets_wont`           |   ✅   | Iac do unsupported gets wont                                                                  |
|   6 | `test_iac_do_echo_is_silent`                  |   ✅   | Iac do echo is silent                                                                         |
|   7 | `test_iac_stripped_from_data`                 |   ✅   | Iac stripped from data                                                                        |
|   8 | `test_print_broadcast`                        |   ✅   | Print broadcast                                                                               |
|   9 | `test_unknown_slot_is_noop`                   |   ✅   | Unknown slot is noop                                                                          |
|  10 | `test_cr_and_control_ignored`                 |   ✅   | Cr and control ignored                                                                        |
|  11 | `test_cr_nul_dispatches_line`                 |   ✅   | Cr nul dispatches line                                                                        |
|  12 | `test_iac_escaped_literal`                    |   ✅   | Iac escaped literal                                                                           |
|  13 | `test_subnegotiation_consumed`                |   ✅   | Subnegotiation consumed                                                                       |
|  14 | `test_subnegotiation_bare_se_does_not_inject` |   ✅   | Subnegotiation bare se does not inject                                                        |
|  15 | `test_accept_no_capacity`                     |   ✅   | Accept no capacity                                                                            |
|  16 | `test_output_escaping_and_printf`             |   ✅   | Output escaping and printf                                                                    |
|  17 | `test_inactive_conn_sends_nothing`            |   ✅   | Inactive conn sends nothing                                                                   |
|  18 | `test_iac_wont_and_dont_are_silent`           |   ✅   | Iac wont and dont are silent                                                                  |
|  19 | `test_iac_do_sga_is_silent`                   |   ✅   | Iac do sga is silent                                                                          |
|  20 | `test_line_no_cmd_cb_is_noop`                 |   ✅   | Line no cmd cb is noop                                                                        |
|  21 | `test_backspace_del_and_empty_noop`           |   ✅   | Backspace del and empty noop                                                                  |
|  22 | `test_line_buffer_overflow_truncates`         |   ✅   | Line buffer overflow truncates                                                                |
|  23 | `test_print_println_null_and_printf_empty`    |   ✅   | Print println null and printf empty                                                           |
|  24 | `test_protocore_handler_accessor`             |   ✅   | The three arms are trampolines: the dispatcher hands them a slot, and they turn that into the |

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

## test_http_clock - native_http_clock - ✅ 4 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                         | Status | Description                           |
| --: | :------------------------------------------- | :----: | :------------------------------------ |
|   1 | `test_rfc9110_date_from_the_active_source`   |   ✅   | Rfc9110 date from the active source   |
|   2 | `test_no_date_until_a_source_has_valid_time` |   ✅   | No date until a source has valid time |
|   3 | `test_no_date_with_no_source_registered`     |   ✅   | No date with no source registered     |
|   4 | `test_the_span_is_stable_across_calls`       |   ✅   | The span is stable across calls       |

</details>

---

## test_tls13_kdf - native_tls13_kdf - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                                   |
| --: | :---------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_transcript_hashes_match_the_trace`                    |   ✅   | Hashing only the ClientHello is a different transcript, so the anchors above are not vacuous. |
|   2 | `test_rfc8448_secret_chain`                                 |   ✅   | The empty-transcript hash the "derived" steps take is SHA-256("").                            |
|   3 | `test_rfc8446_4_4_4_finished_mac`                           |   ✅   | The trace's own Finished message carries exactly that verify_data behind its 4-byte header.   |
|   4 | `test_rfc8446_7_3_traffic_key_expansion`                    |   ✅   | Rfc8446 7 3 traffic key expansion                                                             |
|   5 | `test_derive_secret_matches_the_trace`                      |   ✅   | A label differing in one octet gives an unrelated secret: that is what the label is for.      |
|   6 | `test_dtls_prefix_separates_the_schedules`                  |   ✅   | The early secret is an Extract with no label at all, so the two variants share it.            |
|   7 | `test_a_different_ecdhe_gives_a_different_handshake_secret` |   ✅   | The early secret is upstream of the (EC)DHE, so it is unchanged.                              |
|   8 | `test_client_and_server_secrets_differ_at_every_level`      |   ✅   | Client and server secrets differ at every level                                               |
|   9 | `test_a_null_borrow_is_refused`                             |   ✅   | A null borrow is refused                                                                      |
|  10 | `test_sha384_secret_chain`                                  |   ✅   | Sha384 secret chain                                                                           |
|  11 | `test_sha384_finished_mac`                                  |   ✅   | Sha384 finished mac                                                                           |
|  12 | `test_the_bound_hash_sets_the_secret_length`                |   ✅   | The layout does not move with the hash: a term is one TLS13_SECRET_MAX slot either way.       |
|  13 | `test_the_two_hashes_give_different_schedules`              |   ✅   | The two hashes give different schedules                                                       |

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

## test_totp - native_security_totp - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                    |
| --: | :----------------------------------------------- | :----: | :--------------------------------------------------------------------------------------------- |
|   1 | `test_rfc4226_hotp_test_values`                  |   ✅   | Rfc4226 hotp test values                                                                       |
|   2 | `test_rfc4226_truncated_values_at_eight_digits`  |   ✅   | Rfc4226 truncated values at eight digits                                                       |
|   3 | `test_digit_reduction_is_one_truncation`         |   ✅   | Digit reduction is one truncation                                                              |
|   4 | `test_digit_zero_takes_the_minimum`              |   ✅   | Digit zero takes the minimum                                                                   |
|   5 | `test_rfc6238_totp_test_vectors`                 |   ✅   | Rfc6238 totp test vectors                                                                      |
|   6 | `test_rfc6238_time_step_matches_the_published_t` |   ✅   | Rfc6238 time step matches the published t                                                      |
|   7 | `test_time_step_default_x`                       |   ✅   | Time step default x                                                                            |
|   8 | `test_time_step_floors_within_a_step`            |   ✅   | T0 shifts the origin: (t - T0) / X, so t = T0 + 59 is step 1 exactly as t = 59 is with T0 = 0. |
|   9 | `test_time_step_honors_x`                        |   ✅   | At X = 60 the step is floor(t / 60), which HOTP at that counter must reproduce.                |
|  10 | `test_rfc6238_drift_window`                      |   ✅   | 1111111140 is T = 0x23523EE, two steps past 0x23523EC, so it needs a window of two.            |
|  11 | `test_verify_refuses_what_it_should`             |   ✅   | Verify refuses what it should                                                                  |
|  12 | `test_verify_window_clamps_at_the_epoch`         |   ✅   | Verify window clamps at the epoch                                                              |
|  13 | `test_rfc4648_base32_test_vectors`               |   ✅   | Rfc4648 base32 test vectors                                                                    |
|  14 | `test_base32_accepts_the_provisioning_spellings` |   ✅   | Base32 accepts the provisioning spellings                                                      |
|  15 | `test_base32_refuses_bad_input`                  |   ✅   | Base32 refuses bad input                                                                       |
|  16 | `test_long_key_is_hashed_to_the_block`           |   ✅   | 65 bytes takes the hashed path, 64 the padded one, so the two OTPs must differ.                |
|  17 | `test_counter_is_a_full_64_bit_moving_factor`    |   ✅   | 20000000000 seconds at X = 30 is step 0x27BC86AA, past 2^31 in seconds but not in steps.       |

</details>

---

## test_trace_capture - native_trace_capture - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                                   |
| --: | :---------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------------- |
|   1 | `test_window_is_the_pre_roll_then_the_post_trigger_samples` |   ✅   | Window is the pre roll then the post trigger samples                                          |
|   2 | `test_a_partly_filled_pre_roll_still_reads_oldest_first`    |   ✅   | A partly filled pre roll still reads oldest first                                             |
|   3 | `test_a_second_trigger_is_refused_and_counted`              |   ✅   | A second trigger is refused and counted                                                       |
|   4 | `test_trace_id_counts_completed_windows`                    |   ✅   | Trace id counts completed windows                                                             |
|   5 | `test_arming_is_refused_when_it_cannot_be_honored`          |   ✅   | The split's sum is what has to fit, so a legal half plus an illegal one is still refused.     |
|   6 | `test_samples_with_nothing_armed_are_counted_dropped`       |   ✅   | Samples with nothing armed are counted dropped                                                |
|   7 | `test_arming_clears_the_previous_capture`                   |   ✅   | The pre-roll starts empty, so a trigger straight after arming reads zeros, not stale samples. |
|   8 | `test_a_capture_with_no_pre_roll_is_all_post_trigger`       |   ✅   | A capture with no pre roll is all post trigger                                                |
|   9 | `test_a_capture_with_no_post_trigger_never_completes`       |   ✅   | A capture with no post trigger never completes                                                |
|  10 | `test_the_sink_gets_the_context_it_was_armed_with`          |   ✅   | The sink gets the context it was armed with                                                   |
|  11 | `test_a_stats_read_with_no_destination_is_refused`          |   ✅   | A stats read with no destination is refused                                                   |

</details>

---

## test_ubx - native_ubx_codec - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                  |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------------- |
|   1 | `test_published_poll_frames`                     |   ✅   | Published poll frames                                                                        |
|   2 | `test_fletcher_checksum_over_the_published_span` |   ✅   | An empty span leaves both accumulators at their zero start.                                  |
|   3 | `test_published_cfg_frames`                      |   ✅   | Published cfg frames                                                                         |
|   4 | `test_length_field_is_little_endian`             |   ✅   | Length field is little endian                                                                |
|   5 | `test_build_parse_round_trip`                    |   ✅   | Build parse round trip                                                                       |
|   6 | `test_parse_refuses_a_corrupted_frame`           |   ✅   | Parse refuses a corrupted frame                                                              |
|   7 | `test_parse_refuses_malformed_input`             |   ✅   | Parse refuses malformed input                                                                |
|   8 | `test_build_bounds`                              |   ✅   | Build bounds                                                                                 |
|   9 | `test_ack_helper`                                |   ✅   | Ack helper                                                                                   |
|  10 | `test_little_endian_readers`                     |   ✅   | Little endian readers                                                                        |
|  11 | `test_nav_pvt_published_field_offsets`           |   ✅   | The wrong class, the wrong id, or a payload shorter than the published length is refused.    |
|  12 | `test_nav_timeutc_published_field_offsets`       |   ✅   | Without the UTC-valid bit the leap seconds are unresolved, so the convenience flag is false. |
|  13 | `test_nav_sat_header_and_blocks`                 |   ✅   | Past the declared block count, and a declared length that cannot hold numSvs blocks.         |
|  14 | `test_stream_separates_nmea_from_ubx`            |   ✅   | and the demux is back to hunting, so the next stray octet passes through                     |
|  15 | `test_stream_doubled_sync1_still_opens_a_frame`  |   ✅   | Stream doubled sync1 still opens a frame                                                     |
|  16 | `test_stream_discards_a_bad_checksum`            |   ✅   | Stream discards a bad checksum                                                               |
|  17 | `test_stream_skips_an_over_long_frame`           |   ✅   | payload + the two checksum octets are discarded; only the last one reports the overflow      |
|  18 | `test_stream_accepts_a_zero_length_frame`        |   ✅   | Stream accepts a zero length frame                                                           |

</details>

---

## test_udp - native_udp - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_listener_delivers_in_order_with_boundaries`         |   ✅   | Listener delivers in order with boundaries         |
|   2 | `test_listener_peer_carries_v6`                           |   ✅   | Listener peer carries v6                           |
|   3 | `test_listener_sends_from_a_bound_port`                   |   ✅   | Listener sends from a bound port                   |
|   4 | `test_client_refuses_a_malformed_address_without_sending` |   ✅   | Client refuses a malformed address without sending |
|   5 | `test_client_sends_both_families`                         |   ✅   | Client sends both families                         |
|   6 | `test_a_spent_pbuf_pool_drops_the_datagram`               |   ✅   | A spent pbuf pool drops the datagram               |
|   7 | `test_every_send_returns_its_pbuf`                        |   ✅   | Every send returns its pbuf                        |
|   8 | `test_capture_renders_a_v4_datagram`                      |   ✅   | Capture renders a v4 datagram                      |
|   9 | `test_capture_renders_a_v6_datagram`                      |   ✅   | Capture renders a v6 datagram                      |
|  10 | `test_capture_refuses_a_buffer_that_cannot_hold_it`       |   ✅   | Capture refuses a buffer that cannot hold it       |

</details>

---

## test_udp_telemetry - native_udp_telemetry - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                         | Status | Description                                        |
| --: | :------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_published_point`                       |   ✅   | Published point                                    |
|   2 | `test_published_tag_escaping`                |   ✅   | Published tag escaping                             |
|   3 | `test_tag_escapes_comma_and_equals`          |   ✅   | Tag escapes comma and equals                       |
|   4 | `test_published_integer_fields`              |   ✅   | Published integer fields                           |
|   5 | `test_published_unsigned_fields`             |   ✅   | Published unsigned fields                          |
|   6 | `test_published_float_field`                 |   ✅   | Published float field                              |
|   7 | `test_field_set_separators`                  |   ✅   | Exactly two unescaped spaces in the finished line. |
|   8 | `test_a_point_needs_a_field`                 |   ✅   | A point needs a field                              |
|   9 | `test_tag_after_a_field_is_refused`          |   ✅   | Tag after a field is refused                       |
|  10 | `test_timestamp_before_any_field_is_refused` |   ✅   | Timestamp before any field is refused              |
|  11 | `test_overflow_latches`                      |   ✅   | Overflow latches                                   |
|  12 | `test_measurement_reopens_the_line`          |   ✅   | Measurement reopens the line                       |
|  13 | `test_length_excludes_the_terminator`        |   ✅   | Length excludes the terminator                     |
|  14 | `test_null_buffer_is_refused`                |   ✅   | Null buffer is refused                             |
|  15 | `test_null_measurement_opens_an_empty_line`  |   ✅   | Null measurement opens an empty line               |
|  16 | `test_send_refuses_without_a_network_stack`  |   ✅   | Send refuses without a network stack               |
|  17 | `test_write_refuses_an_incomplete_line`      |   ✅   | Write refuses an incomplete line                   |

</details>

---

## test_udp_transport - native_udp_transport - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                                        | Status | Description                                                          |
| --: | :-------------------------------------------------------------------------- | :----: | :------------------------------------------------------------------- |
|   1 | `test_join_records_the_group`                                               |   ✅   | Join records the group                                               |
|   2 | `test_group_datagram_reaches_the_handler`                                   |   ✅   | Group datagram reaches the handler                                   |
|   3 | `test_counts_repeated_announcements`                                        |   ✅   | Counts repeated announcements                                        |
|   4 | `test_rejects_non_multicast_group`                                          |   ✅   | Rejects non multicast group                                          |
|   5 | `test_accepts_group_range_edges`                                            |   ✅   | Accepts group range edges                                            |
|   6 | `test_rejects_malformed_group`                                              |   ✅   | Rejects malformed group                                              |
|   7 | `test_leave_releases_the_slot`                                              |   ✅   | Leave releases the slot                                              |
|   8 | `test_leave_ignores_a_plain_listener`                                       |   ✅   | Leave ignores a plain listener                                       |
|   9 | `test_listen_rebinds_existing_port`                                         |   ✅   | Listen rebinds existing port                                         |
|  10 | `test_listen_refuses_a_third_port_when_the_pool_is_full`                    |   ✅   | Listen refuses a third port when the pool is full                    |
|  11 | `test_multicast_group_too_long_for_buffer_rejected`                         |   ✅   | Multicast group too long for buffer rejected                         |
|  12 | `test_multicast_join_finds_slot_past_an_unrelated_listener`                 |   ✅   | Multicast join finds slot past an unrelated listener                 |
|  13 | `test_multicast_rejoin_scans_past_a_freed_lower_slot`                       |   ✅   | Multicast rejoin scans past a freed lower slot                       |
|  14 | `test_peer_addr_rejects_null_peer`                                          |   ✅   | Peer addr rejects null peer                                          |
|  15 | `test_peer_addr_copies_and_tolerates_null_outparams`                        |   ✅   | Peer addr copies and tolerates null outparams                        |
|  16 | `test_send_paths_are_captured`                                              |   ✅   | Send paths are captured                                              |
|  17 | `test_a_refused_send_reports_the_refusal`                                   |   ✅   | A refused send reports the refusal                                   |
|  18 | `test_send_rejects_null_zero_and_oversized_payload`                         |   ✅   | Send rejects null zero and oversized payload                         |
|  19 | `test_inject_skips_a_listener_with_no_handler`                              |   ✅   | Inject skips a listener with no handler                              |
|  20 | `test_an_untagged_source_address_carries_no_address`                        |   ✅   | An untagged source address carries no address                        |
|  21 | `test_multicast_lookup_skips_a_different_multicast_group`                   |   ✅   | Multicast lookup skips a different multicast group                   |
|  22 | `test_peer_addr_refuses_a_buffer_it_cannot_fill_and_allows_a_null_port_out` |   ✅   | Peer addr refuses a buffer it cannot fill and allows a null port out |

</details>

---

## test_umati - native_umati - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                 |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------------ |
|   1 | `test_objects_folder_organizes_the_machine_tool` |   ✅   | an unnamed machine still browses, under the type's own name                                 |
|   2 | `test_machine_tool_components`                   |   ✅   | Identification is an add-in (Part 3), which is how Machinery and MachineTool both attach    |
|   3 | `test_identification_variables`                  |   ✅   | and each one reads back the bound value, in the type OPC UA carries it as                   |
|   4 | `test_monitoring_sub_objects`                    |   ✅   | Monitoring/MachineTool: OperationMode (Int32) and PowerOnDuration (Double)                  |
|   5 | `test_axes_expose_one_position_each`             |   ✅   | Axes expose one position each                                                               |
|   6 | `test_production_and_notification`               |   ✅   | ProductionType holds two Objects, not two values: ActiveProgram is a                        |
|   7 | `test_null_strings_read_as_empty`                |   ✅   | Null strings read as empty                                                                  |
|   8 | `test_reads_outside_the_model_are_refused`       |   ✅   | a leaf Variable has no children, and an unknown node is not in the model                    |
|   9 | `test_nothing_is_served_before_bind`             |   ✅   | Nothing is served before bind                                                               |
|  10 | `test_browse_respects_the_reference_cap`         |   ✅   | Browse respects the reference cap                                                           |
|  11 | `test_every_reference_resolves`                  |   ✅   | 6 Identification + 2 Monitoring/MachineTool + 4 Channel + 3 Spindle + 3 axes + 2 Production |

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

## test_utmc - native_utmc_xml - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                    | Status | Description                                                    |
| --: | :------------------------------------------------------ | :----: | :------------------------------------------------------------- |
|   1 | `test_request_document`                                 |   ✅   | Request document                                               |
|   2 | `test_response_document`                                |   ✅   | Response document                                              |
|   3 | `test_attribute_values_use_the_xml_predefined_entities` |   ✅   | the value and timestamp attributes go through the same escaper |
|   4 | `test_quality_flag_renders_as_a_decimal`                |   ✅   | Quality flag renders as a decimal                              |
|   5 | `test_request_round_trip`                               |   ✅   | Request round trip                                             |
|   6 | `test_parse_returns_the_raw_attribute_text`             |   ✅   | Parse returns the raw attribute text                           |
|   7 | `test_parse_accepts_an_empty_id`                        |   ✅   | Parse accepts an empty id                                      |
|   8 | `test_parse_refuses_malformed_documents`                |   ✅   | Parse refuses malformed documents                              |
|   9 | `test_parse_refuses_an_oversized_id`                    |   ✅   | Parse refuses an oversized id                                  |
|  10 | `test_build_overflow_is_refused_whole`                  |   ✅   | Build overflow is refused whole                                |
|  11 | `test_null_text_renders_empty`                          |   ✅   | Null text renders empty                                        |

</details>

---

## test_vl53l0x - native_vl53l0x - ✅ 22 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                             | Status | Description                                                                                |
| --: | :--------------------------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_range_is_the_big_endian_register_pair`                     |   ✅   | 0x04D2 = 4*256 + 13*16 + 2 = 1024 + 208 + 2 = 1234 mm                                      |
|   2 | `test_range_octets_never_overlap`                                |   ✅   | Range octets never overlap                                                                 |
|   3 | `test_data_ready_is_the_low_three_interrupt_bits`                |   ✅   | bits 3 and up on their own are not a data-ready report                                     |
|   4 | `test_range_status_is_bits_6_to_3`                               |   ✅   | the field sits in bits 6:3; bits 7, 2, 1 and 0 belong to other fields and must not leak in |
|   5 | `test_named_status_codes`                                        |   ✅   | a zero register is not a valid measurement, which is the fail-closed case that matters: a  |
|   6 | `test_register_map`                                              |   ✅   | Register map                                                                               |
|   7 | `test_begin_refuses_a_wrong_model_id`                            |   ✅   | the only octet that went out is the model-id register address, no SYSRANGE_START behind it |
|   8 | `test_begin_arms_continuous_ranging`                             |   ✅   | Begin arms continuous ranging                                                              |
|   9 | `test_read_refuses_when_no_measurement_is_ready`                 |   ✅   | Read refuses when no measurement is ready                                                  |
|  10 | `test_read_takes_the_distance_from_offset_ten`                   |   ✅   | Read takes the distance from offset ten                                                    |
|  11 | `test_read_refuses_an_invalid_status`                            |   ✅   | Read refuses an invalid status                                                             |
|  12 | `test_read_refuses_a_null_destination`                           |   ✅   | Read refuses a null destination                                                            |
|  13 | `test_ds11555_model_reference_registers`                         |   ✅   | Ds11555 model reference registers                                                          |
|  14 | `test_begin_refuses_a_part_that_is_not_a_vl53l0x`                |   ✅   | Begin refuses a part that is not a vl53l0x                                                 |
|  15 | `test_begin_starts_continuous_back_to_back_ranging`              |   ✅   | Begin starts continuous back to back ranging                                               |
|  16 | `test_a_distance_in_front_of_the_sensor_reads_back`              |   ✅   | A distance in front of the sensor reads back                                               |
|  17 | `test_only_rangecomplete_reports_a_valid_reading`                |   ✅   | Only rangecomplete reports a valid reading                                                 |
|  18 | `test_the_interrupt_is_cleared_so_a_reading_is_not_served_twice` |   ✅   | nothing new has been measured, so the next read finds nothing ready                        |
|  19 | `test_nothing_is_measured_before_ranging_starts`                 |   ✅   | Nothing is measured before ranging starts                                                  |
|  20 | `test_begin_sends_later_transfers_to_the_address_it_was_given`   |   ✅   | Begin sends later transfers to the address it was given                                    |
|  21 | `test_a_refused_transfer_fails_begin`                            |   ✅   | A refused transfer fails begin                                                             |
|  22 | `test_a_refused_result_read_fails_the_reading`                   |   ✅   | A refused result read fails the reading                                                    |

</details>

---

## test_vxi11 - native_vxi11 - ✅ 17 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                               | Status | Description                                                                  |
| --: | :------------------------------------------------- | :----: | :--------------------------------------------------------------------------- |
|   1 | `test_destroy_link_call_is_byte_exact`             |   ✅   | Destroy link call is byte exact                                              |
|   2 | `test_rfc1833_getport_call_is_byte_exact`          |   ✅   | Rfc1833 getport call is byte exact                                           |
|   3 | `test_rfc5531_record_marking`                      |   ✅   | the largest length the 31-bit field can name                                 |
|   4 | `test_create_link_call_pads_the_device_string`     |   ✅   | Create link call pads the device string                                      |
|   5 | `test_device_write_parameter_order`                |   ✅   | the proc word says device_write = 11                                         |
|   6 | `test_device_read_parameter_order`                 |   ✅   | Device read parameter order                                                  |
|   7 | `test_generic_parms_calls_share_a_layout`          |   ✅   | Generic parms calls share a layout                                           |
|   8 | `test_rfc5531_accepted_reply_header`               |   ✅   | MSG_DENIED (reply_stat 1) is not an accepted reply                           |
|   9 | `test_a_non_success_accept_stat_yields_no_results` |   ✅   | A non success accept stat yields no results                                  |
|  10 | `test_getport_reply`                               |   ✅   | Getport reply                                                                |
|  11 | `test_create_link_reply`                           |   ✅   | Create link reply                                                            |
|  12 | `test_device_read_reply`                           |   ✅   | an opaque whose count runs past the buffer is refused rather than pointed at |
|  13 | `test_write_and_readstb_replies`                   |   ✅   | Write and readstb replies                                                    |
|  14 | `test_bare_device_error_reply`                     |   ✅   | Bare device error reply                                                      |
|  15 | `test_error_codes_have_distinct_descriptions`      |   ✅   | a code with no entry still returns a usable string, never null               |
|  16 | `test_device_flags_bits`                           |   ✅   | Device flags bits                                                            |
|  17 | `test_builders_refuse_a_short_buffer`              |   ✅   | 44 header + 16 parameter words + (4 count + 3 data + 1 XDR pad) = 68         |

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
|  29 | `test_mount_picks_newer_generation_a`              |   ✅   | Mount picks newer generation a              |
|  30 | `test_replay_tail_seq_not_bumped_when_not_newer`   |   ✅   | Replay tail seq not bumped when not newer   |
|  31 | `test_format_sync_fails`                           |   ✅   | Format sync fails                           |
|  32 | `test_checkpoint_first_sync_fails`                 |   ✅   | Checkpoint first sync fails                 |
|  33 | `test_scan_stops_on_length_overrun`                |   ✅   | Scan stops on length overrun                |
|  34 | `test_scan_stops_when_record_exceeds_scratch`      |   ✅   | Scan stops when record exceeds scratch      |
|  35 | `test_pread_in_and_out_of_range`                   |   ✅   | Pread in and out of range                   |

</details>

---

## test_wamp - native_wamp - ✅ 23 passed

<details>
<summary><b>Expand Suite Details</b></summary>

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

## test_wave - native_wave_wsmp - ✅ 13 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                                |
| --: | :----------------------------------------------- | :----: | :----------------------------------------------------------------------------------------- |
|   1 | `test_psid_p_encoding_boundaries`                |   ✅   | Psid p encoding boundaries                                                                 |
|   2 | `test_psid_round_trip_over_every_accepted_value` |   ✅   | Psid round trip over every accepted value                                                  |
|   3 | `test_psid_decode_refuses_malformed_input`       |   ✅   | Psid decode refuses malformed input                                                        |
|   4 | `test_psid_encode_bounds`                        |   ✅   | Psid encode bounds                                                                         |
|   5 | `test_wsmp_frame_layout`                         |   ✅   | a three-octet PSID pushes the length octet out by two, and nothing else moves              |
|   6 | `test_wsmp_round_trip`                           |   ✅   | Wsmp round trip                                                                            |
|   7 | `test_wsmp_parse_checks_the_version`             |   ✅   | the high nibble is a subtype and is not checked, so only the low nibble may fail it        |
|   8 | `test_wsmp_parse_refuses_truncation`             |   ✅   | a declared length longer than what is present                                              |
|   9 | `test_wsmp_payload_length_is_one_octet`          |   ✅   | Wsmp payload length is one octet                                                           |
|  10 | `test_wsmp_build_bounds`                         |   ✅   | Wsmp build bounds                                                                          |
|  11 | `test_1609dot2_envelope`                         |   ✅   | the unsecured content type differs, so a peer can tell a signed frame from an unsigned one |
|  12 | `test_1609dot2_bounds`                           |   ✅   | 1609dot2 bounds                                                                            |
|  13 | `test_wsmp_carries_a_1609dot2_envelope`          |   ✅   | Wsmp carries a 1609dot2 envelope                                                           |

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

## test_webdav - native_webdav_wire - ✅ 16 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                         | Status | Description                                                                           |
| --: | :----------------------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_rfc4918_lock_compatibility_table`                      |   ✅   | Row "None".                                                                           |
|   2 | `test_lock_scope_follows_depth_and_segment_boundaries`       |   ✅   | A Depth-0 lock reaches no further than itself.                                        |
|   3 | `test_lock_paths_normalize_the_trailing_slash`               |   ✅   | The root keeps its single slash rather than normalizing to nothing.                   |
|   4 | `test_write_needs_the_covering_lock_token`                   |   ✅   | A Depth-infinity lock gates its whole subtree the same way.                           |
|   5 | `test_lock_timeout_and_refresh`                              |   ✅   | A refresh moves the expiry, so the second the lock would have died passes harmlessly. |
|   6 | `test_lock_table_is_bounded`                                 |   ✅   | Freeing one slot makes room again.                                                    |
|   7 | `test_lock_oversized_path_and_token_are_refused`             |   ✅   | Lock oversized path and token are refused                                             |
|   8 | `test_if_header_state_token`                                 |   ✅   | "Not" prefixes the condition; the first Coded-URL is still the token the list names.  |
|   9 | `test_depth_header`                                          |   ✅   | Depth header                                                                          |
|  10 | `test_method_classification`                                 |   ✅   | RFC 9110 sec 9.1 makes the method token case-sensitive, so a lowercase spelling is a  |
|  11 | `test_xml_escape`                                            |   ✅   | A closing tag smuggled into an href cannot survive the escape.                        |
|  12 | `test_destination_header_path`                               |   ✅   | RFC 3986 sec 2.1: %20 is a space, and the hex digits are case-insensitive.            |
|  13 | `test_multistatus_document_shape`                            |   ✅   | A file entry has no collection marker; a collection has no content length.            |
|  14 | `test_multistatus_entry_is_atomic`                           |   ✅   | The same is true of begin and end against a buffer that cannot hold them.             |
|  15 | `test_proppatch_multistatus_echoes_the_requested_properties` |   ✅   | The wrappers are not properties and must not be echoed as ones.                       |
|  16 | `test_proppatch_does_not_echo_injected_markup`               |   ✅   | An empty body still produces a well-formed document with no properties in it.         |

</details>

---

## test_webhook - native_webhook_json - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                 | Status | Description                                                                              |
| --: | :--------------------------------------------------- | :----: | :--------------------------------------------------------------------------------------- |
|   1 | `test_target_uri_carries_event_then_key_as_segments` |   ✅   | Swapping the two values swaps the two segments and nothing else: neither is baked in.    |
|   2 | `test_rfc8259_object_grammar`                        |   ✅   | Rfc8259 object grammar                                                                   |
|   3 | `test_absent_values_omit_their_member`               |   ✅   | Absent values omit their member                                                          |
|   4 | `test_rfc8259_escapes_quote_and_reverse_solidus`     |   ✅   | A lone reverse solidus at the end of a value still doubles: the escape is per octet.     |
|   5 | `test_overflow_writes_nothing`                       |   ✅   | An escape that would land one octet past the end fails the same way, mid-value.          |
|   6 | `test_exact_capacity_boundary`                       |   ✅   | Exact capacity boundary                                                                  |
|   7 | `test_every_field_fails_closed`                      |   ✅   | "{\"value1\":\"a\"" is 13 octets, so at cap 14 the value-separator before value2 is what |
|   8 | `test_builder_argument_guards`                       |   ✅   | Builder argument guards                                                                  |
|   9 | `test_post_reports_no_transport`                     |   ✅   | Post reports no transport                                                                |
|  10 | `test_trigger_builds_then_posts`                     |   ✅   | Trigger builds then posts                                                                |
|  11 | `test_post_argument_guards`                          |   ✅   | Post argument guards                                                                     |

</details>

---

## test_wifi_sniffer - native_radio_wifi_sniffer - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                             |
| --: | :---------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_beacon_mac_header`                                    |   ✅   | Beacon mac header                                                                       |
|   2 | `test_frame_control_type_field`                             |   ✅   | Frame control type field                                                                |
|   3 | `test_frame_control_version_and_subtype`                    |   ✅   | Frame control version and subtype                                                       |
|   4 | `test_frame_control_flag_bit_positions`                     |   ✅   | Frame control flag bit positions                                                        |
|   5 | `test_truncated_capture_reports_how_many_addresses_it_held` |   ✅   | Truncated capture reports how many addresses it held                                    |
|   6 | `test_parse_null_arguments`                                 |   ✅   | Parse null arguments                                                                    |
|   7 | `test_stats_tally`                                          |   ✅   | Stats tally                                                                             |
|   8 | `test_roam_needs_to_clear_the_hysteresis`                   |   ✅   | Zero hysteresis roams on any improvement at all.                                        |
|   9 | `test_scan_walks_the_range_and_wraps`                       |   ✅   | A single-channel sweep re-selects the same channel and counts a sweep every hop.        |
|  10 | `test_scan_init_clamps_the_range`                           |   ✅   | Scan init clamps the range                                                              |
|  11 | `test_scan_due_is_rollover_safe`                            |   ✅   | The dwell starts 100 ms before the counter wraps and ends 20 ms after it.               |
|  12 | `test_survey_keeps_the_strongest_per_channel`               |   ✅   | Channels outside [first, first + count) are dropped, not folded into a neighbor.        |
|  13 | `test_survey_best_excludes_the_current_channel`             |   ✅   | The candidate feeds the roam decision: 6 clears 3 by 10 dB, so a 5 dB hysteresis roams. |
|  14 | `test_null_state_is_refused`                                |   ✅   | Null state is refused                                                                   |

</details>

---

## test_wifi_sniffer - native_wifi_sniffer_promisc - ✅ 14 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                        | Status | Description                                                                             |
| --: | :---------------------------------------------------------- | :----: | :-------------------------------------------------------------------------------------- |
|   1 | `test_beacon_mac_header`                                    |   ✅   | Beacon mac header                                                                       |
|   2 | `test_frame_control_type_field`                             |   ✅   | Frame control type field                                                                |
|   3 | `test_frame_control_version_and_subtype`                    |   ✅   | Frame control version and subtype                                                       |
|   4 | `test_frame_control_flag_bit_positions`                     |   ✅   | Frame control flag bit positions                                                        |
|   5 | `test_truncated_capture_reports_how_many_addresses_it_held` |   ✅   | Truncated capture reports how many addresses it held                                    |
|   6 | `test_parse_null_arguments`                                 |   ✅   | Parse null arguments                                                                    |
|   7 | `test_stats_tally`                                          |   ✅   | Stats tally                                                                             |
|   8 | `test_roam_needs_to_clear_the_hysteresis`                   |   ✅   | Zero hysteresis roams on any improvement at all.                                        |
|   9 | `test_scan_walks_the_range_and_wraps`                       |   ✅   | A single-channel sweep re-selects the same channel and counts a sweep every hop.        |
|  10 | `test_scan_init_clamps_the_range`                           |   ✅   | Scan init clamps the range                                                              |
|  11 | `test_scan_due_is_rollover_safe`                            |   ✅   | The dwell starts 100 ms before the counter wraps and ends 20 ms after it.               |
|  12 | `test_survey_keeps_the_strongest_per_channel`               |   ✅   | Channels outside [first, first + count) are dropped, not folded into a neighbor.        |
|  13 | `test_survey_best_excludes_the_current_channel`             |   ✅   | The candidate feeds the roam decision: 6 clears 3 by 10 dB, so a 5 dB hysteresis roams. |
|  14 | `test_null_state_is_refused`                                |   ✅   | Null state is refused                                                                   |

</details>

---

## test_wisun - native_radio_wisun - ✅ 11 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                         | Status | Description                                                                          |
| --: | :------------------------------------------- | :----: | :----------------------------------------------------------------------------------- |
|   1 | `test_rfc7252_figure_16_request`             |   ✅   | Rfc7252 figure 16 request                                                            |
|   2 | `test_leading_slash_is_not_a_segment`        |   ✅   | Leading slash is not a segment                                                       |
|   3 | `test_type_field_selects_confirmable_or_not` |   ✅   | The method code is the second octet: 0.01 GET is 1, 0.03 PUT is 3.                   |
|   4 | `test_token_length_and_placement`            |   ✅   | TKL 9-15 are reserved and MUST NOT be sent.                                          |
|   5 | `test_each_path_segment_is_its_own_option`   |   ✅   | Three segments: one option each, the last two at delta 0.                            |
|   6 | `test_option_length_extension`               |   ✅   | A 12-octet segment is the widest that still fits the nibble.                         |
|   7 | `test_payload_marker`                        |   ✅   | Payload marker                                                                       |
|   8 | `test_build_refuses_a_short_buffer`          |   ✅   | Build refuses a short buffer                                                         |
|   9 | `test_node_registry`                         |   ✅   | The same address again is the same entry, with a fresh last_seen.                    |
|  10 | `test_registry_without_storage`              |   ✅   | A null border router zeroes the field rather than leaving whatever was on the stack. |
|  11 | `test_nodes_json`                            |   ✅   | A buffer that cannot hold the whole document reports 0 rather than truncated JSON.   |

</details>

---

## test_workers - native_workers - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_worker_count_is_two`                                |   ✅   | Worker count is two                                |
|   2 | `test_check_timeouts_reaps_only_owned_slots`              |   ✅   | Check timeouts reaps only owned slots              |
|   3 | `test_pool_init_defaults_owner_zero`                      |   ✅   | Pool init defaults owner zero                      |
|   4 | `test_worker_self_id_roundtrip`                           |   ✅   | Worker self id roundtrip                           |
|   5 | `test_worker_lifecycle_raises_and_lowers_the_run_flag`    |   ✅   | Worker lifecycle raises and lowers the run flag    |
|   6 | `test_defer_queues_and_run_deferred_runs_it`              |   ✅   | Defer queues and run deferred runs it              |
|   7 | `test_listener_worker_queues_init_and_lookup`             |   ✅   | Listener worker queues init and lookup             |
|   8 | `test_enqueue_routes_by_slot_owner_and_rejects_bad_owner` |   ✅   | Enqueue routes by slot owner and rejects bad owner |
|   9 | `test_accept_cb_round_robins_slot_owner`                  |   ✅   | Accept cb round robins slot owner                  |
|  10 | `test_dynamic_listener_creates_worker_queues`             |   ✅   | Dynamic listener creates worker queues             |

</details>

---

## test_workers - native_workers_stack - ✅ 10 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                                      | Status | Description                                        |
| --: | :-------------------------------------------------------- | :----: | :------------------------------------------------- |
|   1 | `test_worker_count_is_two`                                |   ✅   | Worker count is two                                |
|   2 | `test_check_timeouts_reaps_only_owned_slots`              |   ✅   | Check timeouts reaps only owned slots              |
|   3 | `test_pool_init_defaults_owner_zero`                      |   ✅   | Pool init defaults owner zero                      |
|   4 | `test_worker_self_id_roundtrip`                           |   ✅   | Worker self id roundtrip                           |
|   5 | `test_worker_lifecycle_raises_and_lowers_the_run_flag`    |   ✅   | Worker lifecycle raises and lowers the run flag    |
|   6 | `test_defer_queues_and_run_deferred_runs_it`              |   ✅   | Defer queues and run deferred runs it              |
|   7 | `test_listener_worker_queues_init_and_lookup`             |   ✅   | Listener worker queues init and lookup             |
|   8 | `test_enqueue_routes_by_slot_owner_and_rejects_bad_owner` |   ✅   | Enqueue routes by slot owner and rejects bad owner |
|   9 | `test_accept_cb_round_robins_slot_owner`                  |   ✅   | Accept cb round robins slot owner                  |
|  10 | `test_dynamic_listener_creates_worker_queues`             |   ✅   | Dynamic listener creates worker queues             |

</details>

---

## test_ws_client - native_ws_client_rfc6455 - ✅ 19 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                             | Status | Description                                                                           |
| --: | :----------------------------------------------- | :----: | :------------------------------------------------------------------------------------ |
|   1 | `test_rfc6455_accept_for_the_published_key`      |   ✅   | 28 base64 characters: SHA-1 is 20 octets and RFC 4648 sec 4 encodes 20 octets as 28,  |
|   2 | `test_rfc6455_opening_handshake_fields`          |   ✅   | The empty line is the last thing in the request.                                      |
|   3 | `test_subprotocol_is_offered_only_when_named`    |   ✅   | Subprotocol is offered only when named                                                |
|   4 | `test_rfc6455_server_handshake_is_verified`      |   ✅   | One character different: same length, so only the comparison itself separates them.   |
|   5 | `test_rfc6455_masked_text_frame`                 |   ✅   | Rfc6455 masked text frame                                                             |
|   6 | `test_rfc6455_parse_unmasked_text_frame`         |   ✅   | Rfc6455 parse unmasked text frame                                                     |
|   7 | `test_rfc6455_fragmented_message`                |   ✅   | Rfc6455 fragmented message                                                            |
|   8 | `test_rfc6455_control_frames`                    |   ✅   | The masked Pong the RFC pairs with it is exactly what this end builds for opcode 0xA. |
|   9 | `test_rfc6455_payload_length_forms`              |   ✅   | 125 octets: the last length that fits the 7-bit field.                                |
|  10 | `test_rfc6455_256_octet_frame`                   |   ✅   | The RFC's own unmasked form parses to the same 256 octets, four into the frame.       |
|  11 | `test_rfc6455_64kib_frame`                       |   ✅   | Read back the server's unmasked form of the same message.                             |
|  12 | `test_parse_refuses_an_incomplete_frame`         |   ✅   | Parse refuses an incomplete frame                                                     |
|  13 | `test_parse_refuses_an_oversized_payload_length` |   ✅   | Parse refuses an oversized payload length                                             |
|  14 | `test_parse_stays_aligned_past_a_masking_key`    |   ✅   | Parse stays aligned past a masking key                                                |
|  15 | `test_build_frame_fails_closed`                  |   ✅   | Exactly enough room is enough: 2 header octets, the key, and the data.                |
|  16 | `test_accept_for_key_fails_closed`               |   ✅   | Accept for key fails closed                                                           |
|  17 | `test_build_handshake_fails_closed`              |   ✅   | Build handshake fails closed                                                          |
|  18 | `test_check_server_handshake_fails_closed`       |   ✅   | Check server handshake fails closed                                                   |
|  19 | `test_transport_reports_no_connection`           |   ✅   | Transport reports no connection                                                       |

</details>

---

## test_xmpp - native_xmpp - ✅ 18 passed

<details>
<summary><b>Expand Suite Details</b></summary>

|   # | Test                                          | Status | Description                                                          |
| --: | :-------------------------------------------- | :----: | :------------------------------------------------------------------- |
|   1 | `test_predefined_entities`                    |   ✅   | Predefined entities                                                  |
|   2 | `test_escape_leaves_ordinary_text_alone`      |   ✅   | An empty run is a valid escape of nothing.                           |
|   3 | `test_stream_header`                          |   ✅   | An address the caller left unset leaves its attribute out entirely.  |
|   4 | `test_message_stanza`                         |   ✅   | No body: the element still closes, with nothing between the tags.    |
|   5 | `test_message_body_is_escaped`                |   ✅   | Message body is escaped                                              |
|   6 | `test_attribute_values_are_escaped`           |   ✅   | Attribute values are escaped                                         |
|   7 | `test_presence_stanza`                        |   ✅   | Presence stanza                                                      |
|   8 | `test_iq_stanza`                              |   ✅   | A result with no payload is the same element with nothing inside it. |
|   9 | `test_stanza_name`                            |   ✅   | Stanza name                                                          |
|  10 | `test_stanza_name_skips_non_start_tags`       |   ✅   | Nothing but an end-tag: there is no start-tag to name.               |
|  11 | `test_attribute_read`                         |   ✅   | An empty value is a value.                                           |
|  12 | `test_attribute_name_must_start_an_attribute` |   ✅   | Attribute name must start an attribute                               |
|  13 | `test_attribute_value_is_raw`                 |   ✅   | Attribute value is raw                                               |
|  14 | `test_attribute_read_stops_at_the_start_tag`  |   ✅   | Attribute read stops at the start tag                                |
|  15 | `test_attribute_read_refusals`                |   ✅   | Attribute read refusals                                              |
|  16 | `test_build_refuses_a_short_buffer`           |   ✅   | Build refuses a short buffer                                         |
|  17 | `test_escape_refuses_a_null_source`           |   ✅   | Escape refuses a null source                                         |
|  18 | `test_build_then_read_round_trip`             |   ✅   | Build then read round trip                                           |

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
