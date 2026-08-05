# Radio blob code comparison between the Arduino and IDF installs

Matching symbol names prove only that an API surface is the same shape. This compares what
the functions do, by disassembling every function in both installs and diffing the
instruction streams.

- **identical** - the encoded bytes and relocation targets match, so both builds emitted
  the same machine code.
- **equivalent** - mnemonics, operands and relocation targets match once addresses are
  masked. Code that moved to a different offset lands here.
- **different** - the instruction streams disagree. These do not do the same thing.

Addresses are masked because placement moving is the expected difference. Relocation
targets are not masked: which symbol a call reaches is behavior.

The **equivalent** tier reads zero everywhere, and that is expected rather than a failure:
these are relocatable objects, so each function's section starts at offset zero and nothing
shifts between builds unless the code itself changed. The tier would matter when comparing
linked images. Here the real signal is identical against different.

Read with `objdump -dr`; nothing is decompiled. Regenerate with
`python tools/dev_env/blob_diff.py .`.

| Target    | Library            | Shared | Identical | Equivalent | Different | A only | I only |
| --------- | ------------------ | -----: | --------: | ---------: | --------: | -----: | -----: |
| `esp32`   | `libphy.a`         |    273 |       261 |          0 |        12 |      0 |      3 |
| `esp32`   | `libpp.a`          |    536 |       446 |          0 |        90 |     35 |     84 |
| `esp32`   | `libnet80211.a`    |    832 |       676 |          0 |       156 |     20 |    262 |
| `esp32`   | `libmesh.a`        |    545 |       537 |          0 |         8 |      0 |      0 |
| `esp32`   | `libsmartconfig.a` |     88 |        85 |          0 |         3 |      0 |      0 |
| `esp32c3` | `libphy.a`         |    219 |       219 |          0 |         0 |      2 |      7 |
| `esp32c3` | `libpp.a`          |    570 |       570 |          0 |         0 |     43 |     92 |
| `esp32c3` | `libnet80211.a`    |    950 |       950 |          0 |         0 |     36 |    138 |
| `esp32c3` | `libmesh.a`        |    559 |       559 |          0 |         0 |      0 |      0 |
| `esp32c3` | `libsmartconfig.a` |     90 |        90 |          0 |         0 |      0 |      0 |
| `esp32s2` | `libphy.a`         |    194 |       192 |          0 |         2 |      0 |      5 |
| `esp32s2` | `libpp.a`          |    591 |       493 |          0 |        98 |     45 |    104 |
| `esp32s2` | `libnet80211.a`    |    870 |       721 |          0 |       149 |     24 |    274 |
| `esp32s2` | `libmesh.a`        |    545 |       540 |          0 |         5 |      0 |      0 |
| `esp32s2` | `libsmartconfig.a` |     88 |        85 |          0 |         3 |      0 |      0 |
| `esp32s3` | `libphy.a`         |    255 |       249 |          0 |         6 |      1 |      4 |
| `esp32s3` | `libpp.a`          |    614 |       528 |          0 |        86 |     37 |    101 |
| `esp32s3` | `libnet80211.a`    |    873 |       731 |          0 |       142 |     21 |    116 |
| `esp32s3` | `libmesh.a`        |    545 |       540 |          0 |         5 |      0 |      0 |
| `esp32s3` | `libsmartconfig.a` |     88 |        85 |          0 |         3 |      0 |      0 |

## Totals

9325 functions are in both installs. 8557 are byte-identical, 0 are
equivalent once addresses are masked, and 768 genuinely differ.

## `esp32` / `libphy.a` - 12 functions differ

```
bt_i2c_write_set  (A 165 insn, I 164 insn)
bt_tx_pwctrl_init  (A 43 insn, I 43 insn)
btpwr_tsens_track  (A 10 insn, I 9 insn)
get_rf_freq_init$part$2  (A 20 insn, I 22 insn)
phy_close_pa  (A 23 insn, I 20 insn)
phy_get_bb_freqoffset  (A 16 insn, I 14 insn)
phy_get_most_tpw  (A 5 insn, I 4 insn)
phy_hw_set_freq_enable  (A 33 insn, I 32 insn)
phy_rfcal_data_check  (A 5 insn, I 3 insn)
ram_wait_rfpll_cal_end  (A 9 insn, I 11 insn)
register_chipv7_phy  (A 102 insn, I 117 insn)
tx_cont_en  (A 5 insn, I 4 insn)
```

## `esp32` / `libpp.a` - 90 functions differ

```
esf_buf_alloc  (A 31 insn, I 28 insn)
esf_buf_recycle  (A 30 insn, I 27 insn)
esf_buf_setup  (A 79 insn, I 62 insn)
hal_crypto_set_key_entry  (A 25 insn, I 44 insn)
hal_mac_set_csi  (A 7 insn, I 7 insn)
hal_mac_tsf_reset  (A 21 insn, I 29 insn)
ic_init  (A 19 insn, I 23 insn)
ic_set_key  (A 8 insn, I 8 insn)
ic_set_vif  (A 66 insn, I 78 insn)
lmacAdjustTimestamp  (A 10 insn, I 9 insn)
lmacDisableTransmit  (A 24 insn, I 19 insn)
lmacDiscardMSDU  (A 21 insn, I 19 insn)
lmacEndFrameExchangeSequence  (A 104 insn, I 111 insn)
lmacMSDUAged  (A 17 insn, I 18 insn)
lmacProcessCollision  (A 36 insn, I 37 insn)
lmacProcessCollisions_task  (A 11 insn, I 10 insn)
lmacProcessLongRetryFail  (A 57 insn, I 60 insn)
lmacProcessShortRetryFail  (A 85 insn, I 83 insn)
lmacProcessTxComplete  (A 53 insn, I 58 insn)
lmacProcessTxRtsError  (A 33 insn, I 32 insn)
lmacProcessTxTimeout  (A 9 insn, I 8 insn)
lmacRetryTxFrame  (A 20 insn, I 21 insn)
lmacSetTxFrame  (A 45 insn, I 46 insn)
pm_beacon_monitor_tbtt_start  (A 10 insn, I 26 insn)
pm_check_state  (A 6 insn, I 12 insn)
pm_coex_schm_process  (A 23 insn, I 43 insn)
pm_coex_slice_timeout_process  (A 35 insn, I 36 insn)
pm_coex_tbtt_process  (A 20 insn, I 36 insn)
pm_connectionless_wake_interval_timeout_process  (A 2 insn, I 10 insn)
pm_connectionless_wake_window_timeout_process  (A 8 insn, I 16 insn)
pm_disconnected_sleep  (A 10 insn, I 22 insn)
pm_dream  (A 23 insn, I 35 insn)
pm_enable_active_timer  (A 18 insn, I 16 insn)
pm_go_to_sleep  (A 18 insn, I 19 insn)
pm_go_to_wake  (A 17 insn, I 42 insn)
pm_keep_alive  (A 14 insn, I 15 insn)
pm_noise_check  (A 16 insn, I 15 insn)
pm_on_channel  (A 25 insn, I 27 insn)
pm_on_coex_schm_status_config  (A 12 insn, I 12 insn)
pm_on_probe_resp_rx  (A 13 insn, I 14 insn)
pm_parse_beacon  (A 63 insn, I 68 insn)
pm_process_tim  (A 48 insn, I 57 insn)
pm_rx_beacon_process  (A 16 insn, I 17 insn)
pm_send_nullfunc  (A 6 insn, I 7 insn)
pm_set_next_tbtt  (A 36 insn, I 53 insn)
pm_start  (A 30 insn, I 44 insn)
pm_tbtt_process  (A 28 insn, I 59 insn)
pm_tx_data_process  (A 73 insn, I 99 insn)
pm_tx_null_data_done_process  (A 66 insn, I 79 insn)
pm_unregister_connectionless_wake_window  (A 11 insn, I 16 insn)
pm_update_by_connectionless_status  (A 14 insn, I 38 insn)
pm_update_next_tbtt  (A 106 insn, I 101 insn)
pm_update_params  (A 6 insn, I 17 insn)
pm_wake_done  (A 12 insn, I 14 insn)
ppCalFrameTimes  (A 10 insn, I 12 insn)
ppCalTxAMPDULength  (A 57 insn, I 63 insn)
ppFillAMPDUBar  (A 20 insn, I 17 insn)
ppMapTxQueue  (A 32 insn, I 36 insn)
ppMapWaitTxq  (A 14 insn, I 20 insn)
ppProcTxDone  (A 39 insn, I 50 insn)
... 30 more, rerun with --full
```

## `esp32` / `libnet80211.a` - 156 functions differ

```
_do_wifi_start  (A 15 insn, I 33 insn)
_do_wifi_stop  (A 8 insn, I 30 insn)
ampdu_alloc_rx_ba_index  (A 14 insn, I 19 insn)
ampdu_dispatch_as_many_as_possible  (A 16 insn, I 17 insn)
check_bss_queue  (A 7 insn, I 8 insn)
chm_init  (A 18 insn, I 17 insn)
chm_set_current_channel  (A 38 insn, I 35 insn)
cnx_add_to_blacklist  (A 22 insn, I 19 insn)
cnx_bss_alloc  (A 100 insn, I 49 insn)
cnx_bss_init  (A 20 insn, I 18 insn)
cnx_connect_timeout_process  (A 21 insn, I 21 insn)
cnx_handshake_timeout_process  (A 26 insn, I 17 insn)
cnx_node_join  (A 78 insn, I 83 insn)
cnx_node_leave  (A 45 insn, I 50 insn)
cnx_node_remove  (A 16 insn, I 17 insn)
cnx_node_search  (A 21 insn, I 17 insn)
cnx_rc_update_state_metric  (A 10 insn, I 11 insn)
cnx_remove_rc  (A 14 insn, I 19 insn)
cnx_sta_connect_cmd  (A 98 insn, I 101 insn)
cnx_sta_leave  (A 65 insn, I 68 insn)
cnx_sta_scan_cmd  (A 142 insn, I 131 insn)
cnx_start_obss_scan  (A 17 insn, I 19 insn)
cnx_update_bss_more  (A 108 insn, I 107 insn)
esp_wifi_ap_get_sta_aid_local  (A 15 insn, I 16 insn)
esp_wifi_deinit_internal  (A 49 insn, I 54 insn)
esp_wifi_get_beacon_interval  (A 12 insn, I 15 insn)
esp_wifi_get_config_channel_local  (A 11 insn, I 9 insn)
esp_wifi_get_negotiated_bw_local  (A 39 insn, I 33 insn)
esp_wifi_get_negotiated_channel_local  (A 25 insn, I 21 insn)
esp_wifi_init_internal  (A 58 insn, I 65 insn)
esp_wifi_internal_set_spp_amsdu  (A 11 insn, I 13 insn)
esp_wifi_mesh_tx  (A 32 insn, I 31 insn)
esp_wifi_scan_sort_ap_records  (A 47 insn, I 46 insn)
esp_wifi_scan_sort_get_cur_ap_info  (A 38 insn, I 42 insn)
esp_wifi_set_ap_key_internal  (A 47 insn, I 52 insn)
esp_wifi_set_igtk_internal  (A 10 insn, I 24 insn)
esp_wifi_set_inactive_time_local  (A 12 insn, I 14 insn)
esp_wifi_set_promiscuous  (A 18 insn, I 19 insn)
esp_wifi_stop  (A 55 insn, I 59 insn)
get_total_scan_time  (A 28 insn, I 28 insn)
hostap_auth_open  (A 53 insn, I 48 insn)
hostap_handle_timer_process  (A 56 insn, I 57 insn)
hostap_input  (A 225 insn, I 236 insn)
hostap_recv_ctl  (A 52 insn, I 51 insn)
hostap_recv_mgmt  (A 346 insn, I 542 insn)
ieee80211_add_extcap  (A 11 insn, I 20 insn)
ieee80211_add_htinfo_body  (A 35 insn, I 31 insn)
ieee80211_ampdu_reorder  (A 71 insn, I 80 insn)
ieee80211_ampdu_request  (A 60 insn, I 22 insn)
ieee80211_amsdu_adjust_head  (A 13 insn, I 12 insn)
ieee80211_assoc_resp_construct  (A 80 insn, I 91 insn)
ieee80211_auth_construct  (A 70 insn, I 77 insn)
ieee80211_beacon_alloc  (A 59 insn, I 60 insn)
ieee80211_cal_tx_pps  (A 16 insn, I 6 insn)
ieee80211_ccmp_decrypt  (A 40 insn, I 39 insn)
ieee80211_classify  (A 31 insn, I 49 insn)
ieee80211_decrypt_espnow_pkt  (A 10 insn, I 8 insn)
ieee80211_disassoc_construct  (A 13 insn, I 8 insn)
ieee80211_encap_amsdu  (A 52 insn, I 56 insn)
ieee80211_encap_esfbuf  (A 130 insn, I 137 insn)
... 96 more, rerun with --full
```

## `esp32` / `libmesh.a` - 8 functions differ

```
esp_mesh_delete_group_addr  (A 61 insn, I 62 insn)
esp_mesh_parent_select  (A 250 insn, I 248 insn)
esp_mesh_set_config  (A 170 insn, I 172 insn)
esp_mesh_tx_task_init  (A 67 insn, I 67 insn)
mesh_nwk_task_main  (A 1696 insn, I 1690 insn)
mesh_parent_insert_candidate  (A 56 insn, I 58 insn)
mesh_parent_select_done  (A 246 insn, I 256 insn)
mesh_wifi_event_cb  (A 126 insn, I 125 insn)
```

## `esp32` / `libsmartconfig.a` - 3 functions differ

```
sc_get_encode_len  (A 27 insn, I 24 insn)
sc_get_ssid_passwd  (A 374 insn, I 450 insn)
sc_recv_completed  (A 260 insn, I 271 insn)
```

## `esp32s2` / `libphy.a` - 2 functions differ

```
chan14_mic_enable  (A 9 insn, I 7 insn)
register_chipv7_phy  (A 75 insn, I 74 insn)
```

## `esp32s2` / `libpp.a` - 98 functions differ

```
esf_buf_recycle  (A 31 insn, I 26 insn)
esf_buf_setup  (A 80 insn, I 62 insn)
hal_crypto_set_key_entry  (A 25 insn, I 43 insn)
hal_mac_ftm_get_t3  (A 32 insn, I 29 insn)
hal_mac_set_csi  (A 11 insn, I 8 insn)
hal_mac_tsf_get_time  (A 13 insn, I 17 insn)
hal_mac_tsf_reset  (A 23 insn, I 30 insn)
hal_mac_tsf_set_time  (A 4 insn, I 11 insn)
hal_sniffer_set_promis_misc_pkt  (A 7 insn, I 11 insn)
ic_csi_set_config  (A 4 insn, I 12 insn)
ic_init  (A 19 insn, I 23 insn)
lmacAdjustTimestamp  (A 8 insn, I 10 insn)
lmacEndFrameExchangeSequence  (A 104 insn, I 115 insn)
lmacMSDUAged  (A 7 insn, I 8 insn)
lmacProcessAckTimeout  (A 36 insn, I 34 insn)
lmacProcessCollisions_task  (A 11 insn, I 10 insn)
lmacProcessLongRetryFail  (A 58 insn, I 57 insn)
lmacProcessShortRetryFail  (A 80 insn, I 81 insn)
lmacProcessTxComplete  (A 51 insn, I 51 insn)
lmacProcessTxError  (A 34 insn, I 30 insn)
lmacProcessTxTimeout  (A 9 insn, I 8 insn)
lmacRetryTxFrame  (A 22 insn, I 20 insn)
lmacSetTxFrame  (A 30 insn, I 29 insn)
lmacTxDone  (A 27 insn, I 28 insn)
mac_tx_set_plcp1  (A 11 insn, I 10 insn)
pm_beacon_monitor_tbtt_start  (A 11 insn, I 23 insn)
pm_coex_slice_timeout_process  (A 35 insn, I 35 insn)
pm_coex_tbtt_process  (A 19 insn, I 35 insn)
pm_connectionless_wake_window_timeout_process  (A 8 insn, I 16 insn)
pm_disconnected_sleep  (A 10 insn, I 21 insn)
pm_disconnected_sleep_delay_timeout_process  (A 15 insn, I 4 insn)
pm_dream  (A 23 insn, I 34 insn)
pm_enable_active_timer  (A 17 insn, I 16 insn)
pm_enable_beacon_monitor_timer  (A 6 insn, I 36 insn)
pm_go_to_wake  (A 19 insn, I 44 insn)
pm_keep_alive  (A 14 insn, I 15 insn)
pm_noise_check  (A 16 insn, I 15 insn)
pm_on_channel  (A 27 insn, I 29 insn)
pm_on_coex_schm_status_config  (A 11 insn, I 13 insn)
pm_on_probe_resp_rx  (A 11 insn, I 14 insn)
pm_parse_beacon  (A 64 insn, I 74 insn)
pm_register_connectionless_wake_window  (A 11 insn, I 18 insn)
pm_set_sleep_type  (A 35 insn, I 33 insn)
pm_sleep  (A 54 insn, I 57 insn)
pm_start  (A 33 insn, I 43 insn)
pm_tx_data_done_process  (A 29 insn, I 32 insn)
pm_tx_data_process  (A 70 insn, I 101 insn)
pm_tx_null_data_done_process  (A 70 insn, I 83 insn)
pm_unregister_connectionless_wake_window  (A 10 insn, I 20 insn)
pm_update_by_connectionless_status  (A 14 insn, I 40 insn)
pm_update_next_tbtt  (A 29 insn, I 48 insn)
pm_update_params  (A 21 insn, I 35 insn)
pm_wake_done  (A 11 insn, I 13 insn)
ppCheckTxAMPDUlength  (A 9 insn, I 11 insn)
ppMapTxQueue  (A 32 insn, I 35 insn)
ppMapWaitTxq  (A 14 insn, I 20 insn)
ppProcTxDone  (A 39 insn, I 51 insn)
ppProcTxSecFrame  (A 28 insn, I 28 insn)
ppProcessTxQ  (A 37 insn, I 52 insn)
ppRegressAmpdu  (A 13 insn, I 11 insn)
... 38 more, rerun with --full
```

## `esp32s2` / `libnet80211.a` - 149 functions differ

```
_do_wifi_start  (A 15 insn, I 33 insn)
_do_wifi_stop  (A 8 insn, I 30 insn)
ampdu_alloc_rx_ba_index  (A 16 insn, I 17 insn)
check_bss_queue  (A 7 insn, I 8 insn)
chm_init  (A 18 insn, I 17 insn)
chm_set_current_channel  (A 38 insn, I 35 insn)
cnx_bss_alloc  (A 77 insn, I 45 insn)
cnx_node_alloc  (A 13 insn, I 19 insn)
cnx_node_join  (A 78 insn, I 88 insn)
cnx_node_leave  (A 46 insn, I 48 insn)
cnx_node_remove  (A 16 insn, I 18 insn)
cnx_node_search  (A 21 insn, I 17 insn)
cnx_remove_all_rc  (A 8 insn, I 12 insn)
cnx_remove_rc  (A 14 insn, I 17 insn)
cnx_sta_leave  (A 64 insn, I 70 insn)
cnx_sta_scan_cmd  (A 145 insn, I 130 insn)
esp_wifi_80211_tx  (A 26 insn, I 24 insn)
esp_wifi_ap_get_sta_aid_local  (A 16 insn, I 17 insn)
esp_wifi_config_11b_rate  (A 36 insn, I 35 insn)
esp_wifi_deinit_internal  (A 50 insn, I 55 insn)
esp_wifi_eb_tx_status_success_internal  (A 6 insn, I 5 insn)
esp_wifi_get_config  (A 14 insn, I 13 insn)
esp_wifi_get_config_channel_local  (A 12 insn, I 12 insn)
esp_wifi_get_inactive_time_local  (A 16 insn, I 16 insn)
esp_wifi_get_negotiated_bw_local  (A 36 insn, I 35 insn)
esp_wifi_get_negotiated_channel_local  (A 21 insn, I 21 insn)
esp_wifi_init_internal  (A 56 insn, I 64 insn)
esp_wifi_ipc_internal  (A 43 insn, I 36 insn)
esp_wifi_scan_get_cur_ap_record  (A 56 insn, I 54 insn)
esp_wifi_scan_sort_ap_records  (A 45 insn, I 46 insn)
esp_wifi_set_igtk_internal  (A 10 insn, I 24 insn)
esp_wifi_set_inactive_time_local  (A 14 insn, I 13 insn)
esp_wifi_set_promiscuous  (A 16 insn, I 17 insn)
esp_wifi_sta_get_negotiated_phymode_local  (A 16 insn, I 18 insn)
esp_wifi_stop  (A 55 insn, I 60 insn)
esp_wifi_vnd_lora_enable  (A 12 insn, I 11 insn)
ftm_add_resp_session  (A 10 insn, I 20 insn)
ftm_create_responder_session  (A 26 insn, I 20 insn)
ftm_free_resp_session  (A 9 insn, I 7 insn)
ftm_initiator_end_session_local  (A 7 insn, I 10 insn)
ftm_offchan_end  (A 17 insn, I 16 insn)
ftm_resp_allocate_para  (A 40 insn, I 49 insn)
ftm_start_initiator_local  (A 91 insn, I 126 insn)
hostap_auth_open  (A 44 insn, I 47 insn)
hostap_handle_timer_process  (A 49 insn, I 59 insn)
hostap_input  (A 218 insn, I 231 insn)
hostap_recv_ctl  (A 42 insn, I 52 insn)
hostap_recv_mgmt  (A 344 insn, I 546 insn)
ieee80211_alloc_proberesp  (A 91 insn, I 87 insn)
ieee80211_ampdu_reorder  (A 66 insn, I 74 insn)
ieee80211_ampdu_request  (A 53 insn, I 22 insn)
ieee80211_assoc_req_construct  (A 122 insn, I 153 insn)
ieee80211_assoc_resp_construct  (A 79 insn, I 92 insn)
ieee80211_auth_construct  (A 68 insn, I 79 insn)
ieee80211_beacon_construct  (A 66 insn, I 63 insn)
ieee80211_cal_tx_pps  (A 6 insn, I 6 insn)
ieee80211_classify  (A 31 insn, I 49 insn)
ieee80211_deauth_construct  (A 12 insn, I 10 insn)
ieee80211_decrypt_espnow_pkt  (A 10 insn, I 8 insn)
ieee80211_disassoc_construct  (A 12 insn, I 11 insn)
... 89 more, rerun with --full
```

## `esp32s2` / `libmesh.a` - 5 functions differ

```
esp_mesh_delete_group_addr  (A 65 insn, I 63 insn)
esp_mesh_set_config  (A 171 insn, I 172 insn)
mesh_nwk_task_main  (A 1706 insn, I 1707 insn)
mesh_parent_insert_candidate  (A 58 insn, I 61 insn)
mesh_parent_select_done  (A 240 insn, I 245 insn)
```

## `esp32s2` / `libsmartconfig.a` - 3 functions differ

```
sc_get_encode_len  (A 28 insn, I 25 insn)
sc_get_ssid_passwd  (A 368 insn, I 457 insn)
sc_recv_completed  (A 266 insn, I 278 insn)
```

## `esp32s3` / `libphy.a` - 6 functions differ

```
get_temp_init  (A 6 insn, I 5 insn)
pbus_rx_dco_cal_1step  (A 106 insn, I 103 insn)
register_chipv7_phy  (A 51 insn, I 54 insn)
rom_noise_check_loop  (A 36 insn, I 35 insn)
set_pbus_mem  (A 47 insn, I 52 insn)
spur_coef_cfg_new  (A 25 insn, I 24 insn)
```

## `esp32s3` / `libpp.a` - 86 functions differ

```
esf_buf_alloc_dynamic  (A 42 insn, I 43 insn)
esf_buf_recycle  (A 31 insn, I 26 insn)
esf_buf_setup  (A 87 insn, I 70 insn)
hal_crypto_set_key_entry  (A 34 insn, I 51 insn)
hal_sniffer_set_promis_misc_pkt  (A 7 insn, I 11 insn)
ic_csi_set_config  (A 4 insn, I 12 insn)
ic_init  (A 19 insn, I 23 insn)
ic_set_vif  (A 66 insn, I 65 insn)
lmacAdjustTimestamp  (A 8 insn, I 10 insn)
lmacDisableTransmit  (A 23 insn, I 19 insn)
lmacEndFrameExchangeSequence  (A 100 insn, I 114 insn)
lmacMSDUAged  (A 7 insn, I 8 insn)
lmacProcessAckTimeout  (A 44 insn, I 42 insn)
lmacProcessShortRetryFail  (A 82 insn, I 85 insn)
lmacProcessTxComplete  (A 51 insn, I 50 insn)
lmacProcessTxError  (A 35 insn, I 32 insn)
lmacProcessTxTimeout  (A 9 insn, I 8 insn)
lmacProcessTxopQComplete  (A 45 insn, I 46 insn)
lmacRetryTxFrame  (A 21 insn, I 20 insn)
lmacSetAcParam  (A 5 insn, I 4 insn)
lmacSetTxFrame  (A 64 insn, I 66 insn)
pm_beacon_monitor_tbtt_start  (A 16 insn, I 31 insn)
pm_check_state  (A 6 insn, I 12 insn)
pm_coex_slice_timeout_process  (A 35 insn, I 35 insn)
pm_connectionless_wake_window_timeout_process  (A 8 insn, I 16 insn)
pm_disconnected_sleep  (A 10 insn, I 23 insn)
pm_disconnected_sleep_delay_timeout_process  (A 15 insn, I 4 insn)
pm_dream  (A 23 insn, I 34 insn)
pm_enable_beacon_monitor_timer  (A 6 insn, I 36 insn)
pm_go_to_sleep  (A 18 insn, I 19 insn)
pm_go_to_wake  (A 19 insn, I 44 insn)
pm_keep_alive  (A 14 insn, I 15 insn)
pm_on_beacon_rx  (A 33 insn, I 46 insn)
pm_on_channel  (A 26 insn, I 31 insn)
pm_on_coex_schm_status_config  (A 11 insn, I 13 insn)
pm_on_probe_resp_rx  (A 11 insn, I 14 insn)
pm_parse_beacon  (A 68 insn, I 78 insn)
pm_register_connectionless_wake_window  (A 11 insn, I 19 insn)
pm_rx_beacon_process  (A 17 insn, I 20 insn)
pm_set_beacon_filter  (A 15 insn, I 13 insn)
pm_tx_data_done_process  (A 29 insn, I 32 insn)
pm_tx_data_process  (A 70 insn, I 100 insn)
pm_tx_null_data_done_process  (A 70 insn, I 83 insn)
pm_unregister_connectionless_wake_window  (A 9 insn, I 20 insn)
pm_update_by_connectionless_status  (A 14 insn, I 40 insn)
pm_update_next_tbtt  (A 35 insn, I 45 insn)
pm_update_params  (A 16 insn, I 35 insn)
pm_wake_done  (A 11 insn, I 13 insn)
ppCalFrameTimes  (A 10 insn, I 12 insn)
ppCalTxAMPDULength  (A 62 insn, I 65 insn)
ppCheckTxAMPDUlength  (A 9 insn, I 11 insn)
ppMapTxQueue  (A 32 insn, I 35 insn)
ppMapWaitTxq  (A 14 insn, I 20 insn)
ppProcTxDone  (A 39 insn, I 49 insn)
ppProcTxSecFrame  (A 27 insn, I 25 insn)
ppProcessTxQ  (A 53 insn, I 55 insn)
ppResortTxAMPDU  (A 106 insn, I 110 insn)
ppRxFragmentProc  (A 108 insn, I 112 insn)
ppRxPkt  (A 50 insn, I 51 insn)
ppRxProtoProc  (A 25 insn, I 29 insn)
... 26 more, rerun with --full
```

## `esp32s3` / `libnet80211.a` - 142 functions differ

```
_do_wifi_start  (A 15 insn, I 26 insn)
ampdu_dispatch  (A 18 insn, I 14 insn)
ampdu_dispatch_all  (A 17 insn, I 16 insn)
ampdu_free_rx_ba_index  (A 6 insn, I 5 insn)
check_bss_queue  (A 7 insn, I 8 insn)
chm_init  (A 18 insn, I 17 insn)
chm_set_current_channel  (A 38 insn, I 35 insn)
cnx_bss_alloc  (A 77 insn, I 41 insn)
cnx_node_alloc  (A 13 insn, I 19 insn)
cnx_node_join  (A 78 insn, I 88 insn)
cnx_node_leave  (A 46 insn, I 48 insn)
cnx_node_remove  (A 16 insn, I 18 insn)
cnx_node_search  (A 21 insn, I 17 insn)
cnx_remove_all_rc  (A 8 insn, I 12 insn)
cnx_remove_rc  (A 14 insn, I 17 insn)
cnx_sta_leave  (A 64 insn, I 69 insn)
cnx_sta_scan_cmd  (A 145 insn, I 130 insn)
cnx_update_bss_more  (A 98 insn, I 108 insn)
esp_mesh_set_active_duty_cycle  (A 27 insn, I 28 insn)
esp_mesh_set_network_duty_cycle  (A 17 insn, I 15 insn)
esp_wifi_ap_get_sta_aid  (A 6 insn, I 7 insn)
esp_wifi_ap_get_sta_aid_local  (A 16 insn, I 17 insn)
esp_wifi_deinit_internal  (A 55 insn, I 54 insn)
esp_wifi_get_config_channel_local  (A 12 insn, I 12 insn)
esp_wifi_get_inactive_time_local  (A 16 insn, I 16 insn)
esp_wifi_get_negotiated_bw_local  (A 36 insn, I 35 insn)
esp_wifi_get_negotiated_channel_local  (A 21 insn, I 21 insn)
esp_wifi_init_internal  (A 64 insn, I 77 insn)
esp_wifi_internal_set_mac_sleep  (A 9 insn, I 10 insn)
esp_wifi_internal_set_spp_amsdu  (A 10 insn, I 12 insn)
esp_wifi_mesh_tx  (A 32 insn, I 31 insn)
esp_wifi_scan_sort_ap_records  (A 45 insn, I 44 insn)
esp_wifi_scan_sort_get_cur_ap_info  (A 42 insn, I 38 insn)
esp_wifi_scan_sort_get_cur_ap_record  (A 63 insn, I 64 insn)
esp_wifi_set_ap_key_internal  (A 50 insn, I 54 insn)
esp_wifi_set_igtk_internal  (A 10 insn, I 24 insn)
esp_wifi_set_inactive_time_local  (A 14 insn, I 13 insn)
esp_wifi_sta_get_negotiated_phymode_local  (A 16 insn, I 18 insn)
ftm_add_resp_session  (A 10 insn, I 20 insn)
ftm_create_responder_session  (A 26 insn, I 20 insn)
ftm_free_resp_session  (A 9 insn, I 7 insn)
ftm_initiator_end_session_local  (A 7 insn, I 10 insn)
ftm_offchan_end  (A 17 insn, I 16 insn)
ftm_parse_data  (A 90 insn, I 82 insn)
ftm_resp_allocate_para  (A 40 insn, I 49 insn)
ftm_start_initiator_local  (A 91 insn, I 122 insn)
hostap_auth_open  (A 44 insn, I 46 insn)
hostap_handle_timer_process  (A 49 insn, I 55 insn)
hostap_input  (A 218 insn, I 227 insn)
hostap_recv_ctl  (A 42 insn, I 50 insn)
hostap_recv_mgmt  (A 344 insn, I 554 insn)
ieee80211_add_htinfo_body  (A 36 insn, I 34 insn)
ieee80211_alloc_proberesp  (A 91 insn, I 87 insn)
ieee80211_ampdu_reorder  (A 66 insn, I 76 insn)
ieee80211_ampdu_request  (A 53 insn, I 22 insn)
ieee80211_assoc_req_construct  (A 122 insn, I 152 insn)
ieee80211_assoc_resp_construct  (A 79 insn, I 92 insn)
ieee80211_auth_construct  (A 68 insn, I 78 insn)
ieee80211_beacon_alloc  (A 65 insn, I 62 insn)
ieee80211_beacon_construct  (A 66 insn, I 63 insn)
... 82 more, rerun with --full
```

## `esp32s3` / `libmesh.a` - 5 functions differ

```
esp_mesh_delete_group_addr  (A 65 insn, I 63 insn)
esp_mesh_set_config  (A 171 insn, I 172 insn)
mesh_nwk_task_main  (A 1706 insn, I 1707 insn)
mesh_parent_insert_candidate  (A 58 insn, I 61 insn)
mesh_parent_select_done  (A 240 insn, I 245 insn)
```

## `esp32s3` / `libsmartconfig.a` - 3 functions differ

```
sc_get_encode_len  (A 28 insn, I 25 insn)
sc_get_ssid_passwd  (A 367 insn, I 456 insn)
sc_recv_completed  (A 272 insn, I 285 insn)
```
