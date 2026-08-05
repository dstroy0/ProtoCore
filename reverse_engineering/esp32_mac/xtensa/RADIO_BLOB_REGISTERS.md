# Radio blob register map

Every peripheral register the precompiled ESP32 radio libraries touch, grouped by the
function that touches it and listed in the order the accesses happen. Read out of the
instruction stream with `xtensa-esp32-elf-objdump -dr`; nothing here is decompiled.

Xtensa reaches an absolute address by loading a 32-bit literal with `l32r` and then
displacing off it, so a register shows up as `literal + offset`. A function whose base
register is computed rather than loaded is listed with whatever accesses could be
followed, so absence of a register here is not proof the function leaves it alone.

`wr` and `rd` carry the access width in bits.

Regenerate with `python reverse_engineering/esp32_mac/xtensa/blob_registers.py .`.

## `libphy.a` - RF, PLL, baseband and calibration

122 functions touch a peripheral register.

### `ram_disable_agc`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C030
```

### `ram_enable_agc`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C030
```

### `ram_write_gain_mem`  <sub>phy_chip_v7.o</sub>

```
wr32  0x3FF45038
call  esp_dport_access_reg_read
wr32  0x3FF45034
wr32  0x3FF45038
call  esp_dport_access_reg_read
wr32  0x3FF45034
call  esp_dport_access_reg_read
wr32  0x3FF45034
```

### `ram_set_txclk_en`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF460A0
```

### `ram_set_rxclk_en`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF460A0
```

### `disable_wifi_agc`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C01C
call  esp_dport_access_reg_read
wr32  0x3FF5C038
call  esp_dport_access_reg_read
wr32  0x3FF5C030
call  esp_dport_access_reg_read
wr32  0x3FF5C080
```

### `enable_wifi_agc`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C080
call  esp_dport_access_reg_read
wr32  0x3FF5C030
call  esp_dport_access_reg_read
wr32  0x3FF5C01C
call  esp_dport_access_reg_read
wr32  0x3FF5C038
```

### `wr_bt_tx_atten`  <sub>phy_chip_v7.o</sub>

```
wr32  0x3FF4600C
wr32  0x3FF46010
```

### `set_tx_gain_table`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF46000
call  (indirect)
call  (indirect)
```

### `set_xpd_sar`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4880C
```

### `bb_wdt_rst_enable`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5D040
```

### `bb_wdt_int_enable`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5D040
```

### `bb_wdt_timeout_clear`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5D040
```

### `phy_wifi_enable_set`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C080
call  esp_dport_access_reg_read
wr32  0x3FF5C080
```

### `ram_set_noise_floor`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  set_rx_sense
```

### `phy_close_rf`  <sub>phy_chip_v7.o</sub>

```
call  phy_enter_critical
call  noise_check_loop
call  disable_wifi_agc
call  esp_dport_access_reg_read
wr32  0x3FF00024
rd32  0x3FF000E4
call  (indirect)
rd32  0x3FF000DC
call  (indirect)
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF48030
call  set_xpd_sar
call  phy_exit_critical
```

### `ram_bb_bss_cbw40_dig`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF00024
```

### `ram_set_pbus_mem`  <sub>phy_chip_v7.o</sub>

```
call  memcpy
call  memcpy
call  esp_dport_access_reg_read
wr32  0x3FF46030
wr32  0x3FF45038
call  esp_dport_access_reg_read
wr32  0x3FF45034
call  esp_dport_access_reg_read
wr32  0x3FF45034
```

### `ram_bb_tx_ht20_cen`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x60033C6C
```

### `ram_phy_get_noisefloor`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF5D050
```

### `ram_check_noise_floor`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C018
rd32  0x60033C00
rd32  0x60033C00
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
```

### `ram_cbw2040_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x60033C6C
call  esp_dport_access_reg_read
wr32  0x3FF5D000
call  esp_dport_access_reg_read
wr32  0x60033C6C
call  esp_dport_access_reg_read
wr32  0x3FF5D000
```

### `ram_bb_bss_bw_40_en`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF00024
```

### `ram_spur_coef_cfg`  <sub>phy_chip_v7.o</sub>

```
call  rtc_get_xtal
call  esp_dport_access_reg_read
wr32  0x3FF5D014
call  .text.spur_cal$part$8
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF5D018
call  .text.spur_cal$part$8
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF5D01C
call  .text.spur_cal$part$8
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF5D020
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF5CC48
```

### `set_chan_rxcomp`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C0A0
call  esp_dport_access_reg_read
wr32  0x3FF5C0D0
```

### `phy_ant_init`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C030
wr32  0x3FF5C11C
wr32  0x3FF5C120
```

### `tx_delay_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5D030
call  esp_dport_access_reg_read
wr32  0x3FF450F0
call  esp_dport_access_reg_read
wr32  0x3FF5D000
call  esp_dport_access_reg_read
wr32  0x3FF5D030
call  esp_dport_access_reg_read
wr32  0x3FF450F0
call  esp_dport_access_reg_read
wr32  0x3FF5D000
call  phy_bt_ifs_set
```

### `bb_bss_cbw40`  <sub>phy_chip_v7.o</sub>

```
call  g_phyFuns
call  bb_bss_cbw40_ana
call  tx_delay_cfg
call  esp_dport_access_reg_read
wr32  0x3FF5C450
call  esp_dport_access_reg_read
call  (indirect)
call  bb_bss_cbw40_ana
call  g_phyFuns
call  bb_bss_cbw40_ana
call  esp_dport_access_reg_read
wr32  0x3FF5CC0C
call  esp_dport_access_reg_read
wr32  0x3FF5CC0C
call  tx_delay_cfg
call  esp_dport_access_reg_read
wr32  0x3FF5C450
```

### `tx_paon_set`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5D000
call  esp_dport_access_reg_read
wr32  0x3FF46048
call  esp_dport_access_reg_read
wr32  0x3FF5C400
call  esp_dport_access_reg_read
wr32  0x3FF450D0
wr32  0x3FF450C4
wr32  0x3FF450C8
call  esp_dport_access_reg_read
wr32  0x3FF4E054
call  tx_delay_cfg
```

### `agc_reg_init`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C004
call  esp_dport_access_reg_read
wr32  0x3FF5C0A4
call  esp_dport_access_reg_read
wr32  0x3FF5C0A4
wr32  0x3FF5C0C4
wr32  0x3FF5C030
wr32  0x3FF5C094
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C01C
call  esp_dport_access_reg_read
wr32  0x3FF5C028
call  esp_dport_access_reg_read
wr32  0x3FF5C0F8
call  esp_dport_access_reg_read
wr32  0x3FF5C038
call  esp_dport_access_reg_read
wr32  0x3FF5C088
call  esp_dport_access_reg_read
wr32  0x3FF5C104
call  esp_dport_access_reg_read
wr32  0x3FF5C07C
```

### `bb_reg_init`  <sub>phy_chip_v7.o</sub>

```
wr32  0x3FF5CC48
call  esp_dport_access_reg_read
wr32  0x3FF5CCE4
call  esp_dport_access_reg_read
wr32  0x3FF5C400
wr32  0x3FF5CD04
wr32  0x3FF5CD08
wr32  0x3FF5CC0C
wr32  0x3FF5CC08
wr32  0x3FF5CCDC
call  esp_dport_access_reg_read
wr32  0x3FF5C044
call  esp_dport_access_reg_read
wr32  0x3FF5C024
wr32  0x3FF5C094
call  esp_dport_access_reg_read
wr32  0x3FF5CC04
wr32  0x3FF5CCD8
```

### `mac_enable_bb`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF00024
call  esp_dport_access_reg_read
wr32  0x3FF00024
call  esp_dport_access_reg_read
wr32  0x3FF00024
call  esp_dport_access_reg_read
wr32  0x3FF00024
call  esp_dport_access_reg_read
wr32  0x3FF00024
call  esp_dport_access_reg_read
wr32  0x3FF00024
call  esp_dport_access_reg_read
wr32  0x3FF00024
```

### `bb_wdg_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5D040
wr32  0x3FF5D03C
```

### `rx_11b_opt`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C044
call  esp_dport_access_reg_read
wr32  0x3FF5C044
call  esp_dport_access_reg_read
wr32  0x3FF5C124
call  esp_dport_access_reg_read
wr32  0x3FF5C124
call  esp_dport_access_reg_read
wr32  0x3FF5C804
call  esp_dport_access_reg_read
wr32  0x3FF5C104
call  esp_dport_access_reg_read
wr32  0x3FF5C044
call  esp_dport_access_reg_read
wr32  0x3FF5C044
call  esp_dport_access_reg_read
wr32  0x3FF5C124
call  esp_dport_access_reg_read
wr32  0x3FF5C124
call  esp_dport_access_reg_read
wr32  0x3FF5C804
call  esp_dport_access_reg_read
wr32  0x3FF5C104
```

### `opt_11b_resart`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C0A0
call  esp_dport_access_reg_read
wr32  0x3FF5C01C
call  esp_dport_access_reg_read
wr32  0x3FF5C094
call  esp_dport_access_reg_read
wr32  0x3FF5C0F0
call  esp_dport_access_reg_read
wr32  0x3FF5C0B8
```

### `phy_reg_init`  <sub>phy_chip_v7.o</sub>

```
call  tx_paon_set
call  bb_reg_init
call  agc_reg_init
call  esp_dport_access_reg_read
wr32  0x3FF5D040
wr32  0x3FF5D03C
call  mac_enable_bb
call  phy_set_bbfreq_init
call  phy_set_bbfreq_init
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF5C030
wr32  0x3FF5C11C
wr32  0x3FF5C120
call  opt_11b_resart
call  rx_11b_opt
```

### `set_chan_reg`  <sub>phy_chip_v7.o</sub>

```
call  chan_to_freq
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  chan_to_freq
call  (indirect)
call  chan_to_freq
call  set_chan_rxcomp
call  g_phyFuns
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  bb_bss_cbw40
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  phy_freq_correct_opt
```

### `i2c_master_reset`  <sub>phy_chip_v7.o</sub>

```
rd32  0x60033C00
rd32  0x60033C00
call  phy_printf
wr32  0x3FF4E000
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
```

### `set_rx_gain_cal_iq`  <sub>phy_chip_v7.o</sub>

```
call  memcpy
call  memcpy
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  phy_printf
call  phy_printf
call  phy_printf
call  (indirect)
call  phy_printf
call  (indirect)
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  (indirect)
```

### `set_rx_gain_testchip_70`  <sub>phy_chip_v7.o</sub>

```
call  set_rx_gain_cal_iq
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF450DC
rd32  0x3FF451AC
call  (indirect)
rd32  0x3FF45198
rd32  0x3FF45194
call  (indirect)
call  (indirect)
call  set_rx_gain_cal_dc
call  esp_dport_access_reg_read
wr32  0x3FF460A0
rd32  0x3FF46170
call  (indirect)
rd32  0x3FF46164
call  (indirect)
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  esp_dport_access_reg_read
rd32  0x3FF4519C
wr32  0x3FF460A0
call  (indirect)
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  (indirect)
call  set_rx_gain_cal_dc
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  (indirect)
call  (indirect)
```

### `phy_wifitx_low_power`  <sub>phy_chip_v7.o</sub>

```
call  set_tx_gain_table
wr32  0x3FF46004
wr32  0x3FF46008
call  tx_gain_table_set
```

### `set_tx_dig_gain`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF46000
call  esp_dport_access_reg_read
wr32  0x3FF46000
```

### `chip_sleep_prot_en`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x60033CB8
call  ets_delay_us
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x60033CB8
call  ets_delay_us
call  esp_dport_access_reg_read
call  g_phyFuns
```

### `chip_sleep_prot_dis`  <sub>phy_chip_v7.o</sub>

```
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x60033CB8
```

### `set_cca`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C01C
call  esp_dport_access_reg_read
wr32  0x3FF5C01C
```

### `set_rx_sense`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF5C01C
call  esp_dport_access_reg_read
wr32  0x3FF5C01C
call  esp_dport_access_reg_read
wr32  0x3FF5C010
call  esp_dport_access_reg_read
wr32  0x3FF5C010
call  esp_dport_access_reg_read
wr32  0x3FF5C014
call  esp_dport_access_reg_read
wr32  0x3FF5C014
call  esp_dport_access_reg_read
wr32  0x3FF5C0CC
call  esp_dport_access_reg_read
wr32  0x3FF5C0CC
call  esp_dport_access_reg_read
wr32  0x3FF5C0CC
call  esp_dport_access_reg_read
wr32  0x3FF5C0CC
call  esp_dport_access_reg_read
wr32  0x3FF5C0CC
call  esp_dport_access_reg_read
wr32  0x3FF5C0CC
call  esp_dport_access_reg_read
wr32  0x3FF5C044
call  esp_dport_access_reg_read
wr32  0x3FF5C044
call  esp_dport_access_reg_read
wr32  0x3FF5C124
call  esp_dport_access_reg_read
wr32  0x3FF5C124
```

### `noise_check_loop`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5D044
call  esp_dport_access_reg_read
wr32  0x3FF5D050
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF5D044
```

### `noise_init`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  esp_dport_access_reg_read
wr32  0x3FF5C018
call  chip_v7_set_chan_nomac
call  (indirect)
call  g_phyFuns
```

### `set_rx_gain_table`  <sub>phy_chip_v7.o</sub>

```
call  memcpy
call  memcpy
call  g_phyFuns
call  ram_gen_rx_gain_table
call  ram_gen_rx_gain_table
call  set_rx_gain_testchip_70
call  set_rx_gain_testchip_70
call  wr_rx_gain_mem
call  wr_rx_gain_mem
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C0A4
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  esp_dport_access_reg_read
wr32  0x3FF450DC
```

### `phy_rx11blr_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C860
call  esp_dport_access_reg_read
wr32  0x3FF5C860
call  esp_dport_access_reg_read
wr32  0x3FF5C87C
```

### `wifi_rifs_mode_en`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C0F4
```

### `phy_chan_filt_set`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5CC0C
call  esp_dport_access_reg_read
wr32  0x3FF5CC0C
call  esp_dport_access_reg_read
wr32  0x3FF5CC0C
call  esp_dport_access_reg_read
wr32  0x3FF5CC0C
call  esp_dport_access_reg_read
wr32  0x3FF5CD08
call  esp_dport_access_reg_read
wr32  0x3FF5CD04
call  esp_dport_access_reg_read
wr32  0x3FF5C074
call  esp_dport_access_reg_read
wr32  0x3FF5C074
```

### `rf_cal_data_backup`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
```

### `i2cmst_reg_init`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
```

### `fe_reg_init`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  esp_dport_access_reg_read
wr32  0x3FF45114
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  esp_dport_access_reg_read
wr32  0x3FF450D8
call  esp_dport_access_reg_read
wr32  0x3FF460B8
call  esp_dport_access_reg_read
wr32  0x3FF4609C
```

### `reg_init_begin`  <sub>phy_chip_v7.o</sub>

```
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF48030
call  esp_dport_access_reg_read
wr32  0x3FF66000
call  fe_reg_init
call  i2cmst_reg_init
```

### `phy_wakeup_init`  <sub>phy_chip_v7.o</sub>

```
call  phy_enter_critical
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  phy_dis_hw_set_freq
call  i2c_master_reset
call  reg_init_begin
call  phy_i2c_init
call  phy_reg_init
call  set_chan_reg
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  g_phyFuns
call  enable_wifi_agc
call  phy_en_hw_set_freq
call  esp_dport_access_reg_read
wr32  0x3FF5C080
call  phy_exit_critical
```

### `reset_rf_dig`  <sub>phy_chip_v7.o</sub>

```
call  disable_wifi_agc
call  esp_dport_access_reg_read
wr32  0x3FF00024
call  esp_dport_access_reg_read
wr32  0x3FF000D0
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF000D0
call  esp_dport_access_reg_read
wr32  0x3FF48030
call  ets_delay_us
call  force_txrxoff
```

### `register_chipv7_phy`  <sub>phy_chip_v7.o</sub>

```
rd32  0x60033C00
call  phy_wakeup_init
call  phy_get_romfunc_addr
call  reset_rf_dig
call  phy_dis_hw_set_freq
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  i2c_master_reset
call  phy_rfcal_data_check
call  register_chipv7_phy_init_param
call  rf_cal_data_recovery
call  write_freq_mem_all
call  rf_init
call  bb_init
call  rf_cal_data_backup
call  phy_rfcal_data_check
call  (indirect)
call  chip_v7_set_chan_nomac
call  force_txrxoff
call  chip_v7_set_chan_nomac
call  chip_v7_set_chan_offset
call  g_phyFuns
call  tsens_code_read
call  phy_en_hw_set_freq
call  enable_wifi_agc
call  esp_dport_access_reg_read
wr32  0x3FF5C080
```

### `phy_rx_sense_set`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C010
call  esp_dport_access_reg_read
wr32  0x3FF5C014
call  esp_dport_access_reg_read
wr32  0x3FF5C044
call  esp_dport_access_reg_read
wr32  0x3FF5C108
call  esp_dport_access_reg_read
wr32  0x3FF5C108
```

### `ant_dft_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C11C
```

### `ant_wifitx_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF45104
call  esp_dport_access_reg_read
wr32  0x3FF45104
```

### `ant_wifirx_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C11C
call  esp_dport_access_reg_read
wr32  0x3FF45104
call  esp_dport_access_reg_read
wr32  0x3FF45108
call  esp_dport_access_reg_read
wr32  0x3FF45108
call  esp_dport_access_reg_read
wr32  0x3FF45108
```

### `ant_bttx_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF45108
call  esp_dport_access_reg_read
wr32  0x3FF4510C
```

### `ant_btrx_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C11C
call  esp_dport_access_reg_read
wr32  0x3FF4510C
call  esp_dport_access_reg_read
wr32  0x3FF4510C
call  esp_dport_access_reg_read
wr32  0x3FF4510C
call  esp_dport_access_reg_read
wr32  0x3FF45110
```

### `esp_tx_state_out`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF45104
call  esp_dport_access_reg_read
wr32  0x3FF45108
call  esp_dport_access_reg_read
wr32  0x3FF4510C
call  esp_dport_access_reg_read
wr32  0x3FF45110
```

### `phy_chan_dump_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5CD0C
call  esp_dport_access_reg_read
wr32  0x3FF5CD0C
call  esp_dport_access_reg_read
wr32  0x3FF5CD0C
call  esp_dport_access_reg_read
wr32  0x3FF5CD0C
call  esp_dport_access_reg_read
wr32  0x3FF5CD0C
```

### `chan14_mic_cfg`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C400
call  (indirect)
call  (indirect)
call  phy_set_most_tpw
call  esp_dport_access_reg_read
wr32  0x3FF5C400
rd32  0x3FF5C4A8
call  (indirect)
call  (indirect)
call  phy_set_most_tpw
```

### `phy_get_adc_rand`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF48030
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  esp_dport_access_reg_read
wr32  0x3FF66000
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
wr32  0x3FF66000
call  (indirect)
```

### `phy_enable_low_rate`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C860
call  esp_dport_access_reg_read
wr32  0x3FF5C860
call  esp_dport_access_reg_read
wr32  0x3FF5C87C
call  write_txrate_power_offset
```

### `phy_disable_low_rate`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C860
call  esp_dport_access_reg_read
wr32  0x3FF5C860
call  esp_dport_access_reg_read
wr32  0x3FF5C87C
call  write_txrate_power_offset
```

### `phy_close_pa`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  esp_dport_access_reg_read
wr32  0x3FF4609C
call  esp_dport_access_reg_read
wr32  0x3FF4609C
call  esp_dport_access_reg_read
wr32  0x3FF4609C
call  esp_dport_access_reg_read
wr32  0x3FF4609C
call  esp_dport_access_reg_read
wr32  0x3FF460A0
```

### `phy_dig_reg_backup`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF5CD0C
wr32  0x3FF5CC0C
wr32  0x3FF5CD08
wr32  0x3FF5CD04
wr32  0x3FF5C074
wr32  0x3FF5C11C
wr32  0x3FF45104
wr32  0x3FF45108
wr32  0x3FF4510C
wr32  0x3FF45110
wr32  0x3FF5C860
wr32  0x3FF5C87C
wr32  0x3FF5C02C
wr32  0x3FF5C018
wr32  0x3FF5C01C
wr32  0x3FF5C010
wr32  0x3FF5C014
wr32  0x3FF5C044
wr32  0x3FF5C108
wr32  0x3FF5C0CC
wr32  0x3FF5C124
```

### `freq_offset_get_pwr_1`  <sub>phy_chip_v7.o</sub>

```
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF450A8
call  esp_dport_access_reg_read
wr32  0x3FF460B8
rd32  0x3FF460B8
call  (indirect)
call  get_iq_est_snr_1
call  (indirect)
call  phy_printf
call  esp_dport_access_reg_read
wr32  0x3FF460B8
call  (indirect)
```

### `get_spur4m_pwr`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  freq_offset_get_pwr_1
call  freq_offset_get_pwr_1
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
```

### `bt_opt_write_mem`  <sub>phy_chip_v7.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E148
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
```

### `bt_rx_spur_opt`  <sub>phy_chip_v7.o</sub>

```
call  (indirect)
call  (indirect)
call  set_chan_freq_sw_start
call  rx_spur_cal
call  rx_spur_cal
call  bt_opt_write_mem
call  bt_opt_write_mem
call  set_chan_freq_sw_start
call  rx_spur_cal
call  chip7_phy_init_ctrl
call  rx_spur_cal
call  rx_spur_cal
call  bt_opt_write_mem
call  (indirect)
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
rd32  0x3FF4E164
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
```

### `pll_correct_dcap`  <sub>phy_chip_v7_ana.o</sub>

```
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF4E130
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E148
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
```

### `phy_dis_hw_set_freq`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  ets_delay_us
call  esp_dport_access_reg_read
```

### `phy_force_wifi_chan`  <sub>phy_chip_v7_ana.o</sub>

```
call  phy_enter_critical
call  esp_dport_access_reg_read
wr32  0x3FF51040
rd32  0x60033C00
rd32  0x60033C00
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  phy_exit_critical
call  phy_dis_hw_set_freq
call  phy_dis_hw_set_freq
```

### `phy_en_hw_set_freq`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF5C07C
```

### `ram_chip_i2c_readReg`  <sub>phy_chip_v7_ana.o</sub>

```
call  phy_enter_critical
call  phy_dis_hw_set_freq
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E044
call  esp_dport_access_reg_read
wr32  0x3FF4E044
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  phy_en_hw_set_freq
call  phy_exit_critical
```

### `ram_chip_i2c_writeReg`  <sub>phy_chip_v7_ana.o</sub>

```
call  phy_enter_critical
call  phy_dis_hw_set_freq
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  phy_en_hw_set_freq
call  phy_exit_critical
```

### `phy_unforce_wifi_chan`  <sub>phy_chip_v7_ana.o</sub>

```
call  phy_enter_critical
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  phy_en_hw_set_freq
call  esp_dport_access_reg_read
wr32  0x3FF51040
call  phy_exit_critical
```

### `btpwr_pll_track`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
```

### `phy_bt_ifs_set`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF5103C
```

### `tsens_code_read`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4884C
call  ets_delay_us
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4884C
call  esp_dport_access_reg_read
wr32  0x3FF4884C
call  esp_dport_access_reg_read
wr32  0x3FF4884C
```

### `ram_pbus_force_test`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF46094
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF46094
```

### `force_txrxoff`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  esp_dport_access_reg_read
wr32  0x3FF460A0
```

### `ram_pbus_force_mode`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4609C
call  esp_dport_access_reg_read
wr32  0x3FF46094
call  esp_dport_access_reg_read
wr32  0x3FF46094
call  esp_dport_access_reg_read
wr32  0x3FF4609C
call  esp_dport_access_reg_read
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF5C02C
```

### `phy_freq_correct_opt`  <sub>phy_chip_v7_ana.o</sub>

```
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF5C400
call  esp_dport_access_reg_read
wr32  0x3FF4E054
```

### `correct_rfpll_offset`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E148
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
```

### `wr_rf_freq_mem`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
wr32  0x3FF4E148
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
```

### `bt_i2c_read_set`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E14C
call  esp_dport_access_reg_read
wr32  0x3FF4E14C
call  esp_dport_access_reg_read
wr32  0x3FF4E14C
call  esp_dport_access_reg_read
wr32  0x3FF4E14C
call  esp_dport_access_reg_read
wr32  0x3FF4E150
call  esp_dport_access_reg_read
wr32  0x3FF4E150
```

### `bt_i2c_read_mem`  <sub>phy_chip_v7_ana.o</sub>

```
call  phy_printf
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  phy_printf
call  phy_printf
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  phy_printf
```

### `bt_i2c_write_set`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E100
call  esp_dport_access_reg_read
wr32  0x3FF4E104
call  esp_dport_access_reg_read
wr32  0x3FF4E108
call  esp_dport_access_reg_read
wr32  0x3FF4E0D8
call  esp_dport_access_reg_read
wr32  0x3FF4E0E0
call  esp_dport_access_reg_read
wr32  0x3FF4E110
call  esp_dport_access_reg_read
wr32  0x3FF4E128
call  esp_dport_access_reg_read
wr32  0x3FF4E12C
call  esp_dport_access_reg_read
wr32  0x3FF4E0D0
call  esp_dport_access_reg_read
wr32  0x3FF4E11C
call  esp_dport_access_reg_read
wr32  0x3FF4E0D4
call  esp_dport_access_reg_read
wr32  0x3FF4E120
call  esp_dport_access_reg_read
wr32  0x3FF4E124
call  esp_dport_access_reg_read
wr32  0x3FF4E124
call  esp_dport_access_reg_read
wr32  0x3FF4E0C8
call  esp_dport_access_reg_read
wr32  0x3FF4E0CC
call  esp_dport_access_reg_read
wr32  0x3FF4E118
call  esp_dport_access_reg_read
rd8   0x3FF4E124
wr32  0x3FF4E118
wr32  0x3FF4E164
wr32  0x3FF4E164
```

### `bt_i2c_set_wifi_data`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E130
call  esp_dport_access_reg_read
wr32  0x3FF4E134
call  esp_dport_access_reg_read
wr32  0x3FF4E140
```

### `tsens_read_init`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4884C
call  esp_dport_access_reg_read
wr32  0x3FF4884C
call  esp_dport_access_reg_read
wr32  0x3FF4884C
call  esp_dport_access_reg_read
wr32  0x3FF4884C
```

### `write_wifi_chan_data`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
call  get_lna_vga_dcap_val
call  bt_i2c_set_wifi_data
```

### `set_chan_freq_hw_init`  <sub>phy_chip_v7_ana.o</sub>

```
call  .text.get_rf_freq_init$part$2
call  bt_get_i2c_data
call  bt_i2c_write_set
call  write_wifi_chan_data
call  esp_dport_access_reg_read
wr32  0x60033D38
call  i2cmst_reg_init
```

### `phy_hw_set_freq_enable`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  ets_delay_us
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF5C07C
```

### `set_chan_freq_sw_start`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  phy_dis_hw_set_freq
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  correct_rfpll_offset
call  write_wifi_chan_data
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  ets_delay_us
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  ets_delay_us
call  pll_correct_dcap
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  phy_en_hw_set_freq
```

### `set_channel_rfpll_freq`  <sub>phy_chip_v7_ana.o</sub>

```
call  g_phyFuns
call  phy_printf
call  esp_dport_access_reg_read
call  set_chan_freq_sw_start
call  esp_dport_access_reg_read
call  write_wifi_chan_data
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
```

### `chip_v7_set_chan_nomac`  <sub>phy_chip_v7_ana.o</sub>

```
rd32  0x60033C00
call  disable_wifi_agc
call  phy_enter_critical
call  set_channel_rfpll_freq
call  bb_bss_cbw40
call  bb_bss_cbw40
call  g_phyFuns
call  chip_v7_set_chan_misc
call  phy_exit_critical
call  chan14_mic_cfg
call  chan14_mic_cfg
call  enable_wifi_agc
```

### `freq_write_wifi_chan`  <sub>phy_chip_v7_ana.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  write_wifi_chan_data
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  ets_delay_us
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
wr32  0x3FF4E0C4
call  esp_dport_access_reg_read
```

### `write_txrate_power_offset`  <sub>phy_chip_v7_cal.o</sub>

```
wr32  0x3FF4506C
wr32  0x3FF45070
wr32  0x3FF45074
wr32  0x3FF45078
wr32  0x3FF4503C
wr32  0x3FF45040
wr32  0x3FF45044
wr32  0x3FF45048
call  get_target_power_offset
call  esp_dport_access_reg_read
wr32  0x3FF4504C
call  esp_dport_access_reg_read
wr32  0x3FF45050
wr32  0x3FF4504C
```

### `force_txrx_off`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF460A0
call  ets_delay_us
```

### `phy_pwdet_onetime_en`  <sub>phy_chip_v7_cal.o</sub>

```
call  pwdet_sar2_init
call  esp_dport_access_reg_read
wr32  0x3FF4E060
call  esp_dport_access_reg_read
wr32  0x3FF4E060
```

### `write_wifi_dig_gain`  <sub>phy_chip_v7_cal.o</sub>

```
wr32  0x3FF46004
wr32  0x3FF46008
```

### `phy_set_bbfreq_init`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF5CCB8
call  esp_dport_access_reg_read
wr32  0x3FF5C85C
```

### `ram_tx_pwctrl_bg_init`  <sub>phy_chip_v7_cal.o</sub>

```
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF4E060
```

### `pwdet_sar2_init`  <sub>phy_chip_v7_cal.o</sub>

```
call  set_xpd_sar
call  esp_dport_access_reg_read
wr32  0x3FF48890
call  esp_dport_access_reg_read
wr32  0x3FF66010
call  esp_dport_access_reg_read
wr32  0x3FF48890
call  esp_dport_access_reg_read
wr32  0x3FF48894
call  esp_dport_access_reg_read
wr32  0x3FF48894
call  esp_dport_access_reg_read
wr32  0x3FF4882C
call  esp_dport_access_reg_read
wr32  0x3FF48838
call  esp_dport_access_reg_read
wr32  0x3FF48838
call  esp_dport_access_reg_read
wr32  0x3FF4882C
```

### `ram_en_pwdet`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E05C
call  esp_dport_access_reg_read
wr32  0x3FF4E05C
call  esp_dport_access_reg_read
wr32  0x3FF4E050
call  esp_dport_access_reg_read
wr32  0x3FF460C0
call  esp_dport_access_reg_read
wr32  0x3FF4E050
call  pwdet_sar2_init
```

### `ram_txdc_cal_v70`  <sub>phy_chip_v7_cal.o</sub>

```
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF4E04C
call  esp_dport_access_reg_read
wr32  0x3FF4E04C
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF4E04C
```

### `ram_get_fm_sar_dout`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E050
call  esp_dport_access_reg_read
wr32  0x3FF4E050
call  ets_delay_us
call  esp_dport_access_reg_read
call  g_phyFuns
```

### `ram_txiq_get_mis_pwr`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF460B8
call  esp_dport_access_reg_read
wr32  0x3FF450A8
call  ets_delay_us
rd32  0x3FF450A8
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF460B8
call  ets_delay_us
call  (indirect)
```

### `rfcal_txiq`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  esp_dport_access_reg_read
wr32  0x3FF450DC
call  txcal_debuge_mode
call  txcal_debuge_mode
call  txcal_debuge_mode
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
call  ets_delay_us
call  ets_delay_us
call  ram_txiq_cover
call  (indirect)
wr32  0x3FF460B8
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF450DC
```

### `ram_iq_est_enable`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF46060
call  esp_dport_access_reg_read
wr32  0x3FF4607C
call  esp_dport_access_reg_read
wr32  0x3FF4607C
call  esp_dport_access_reg_read
wr32  0x3FF4607C
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF4607C
call  esp_dport_access_reg_read
```

### `ram_iq_est_disable`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4607C
call  ets_delay_us
call  esp_dport_access_reg_read
wr32  0x3FF4607C
```

### `tx_pwctrl_init_cal`  <sub>phy_chip_v7_cal.o</sub>

```
call  set_channel_rfpll_freq
call  chip7_phy_init_ctrl
call  cal_rf_ana_gain
call  ram_tx_pwr_backoff
call  ram_tx_pwr_backoff
call  g_phyFuns
call  esp_dport_access_reg_read
wr32  0x3FF460F8
call  ram_rfcal_pwrctrl
call  (indirect)
```

### `ram_phy_get_vdd33`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4882C
call  esp_dport_access_reg_read
wr32  0x3FF460C0
call  set_xpd_sar
call  esp_dport_access_reg_read
wr32  0x3FF48890
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF4882C
call  esp_dport_access_reg_read
wr32  0x3FF48838
call  esp_dport_access_reg_read
wr32  0x3FF4E05C
call  esp_dport_access_reg_read
wr32  0x3FF4E05C
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E050
call  esp_dport_access_reg_read
wr32  0x3FF4E050
call  ets_delay_us
call  esp_dport_access_reg_read
rd32  0x3FF4E050
call  (indirect)
call  (indirect)
call  esp_dport_access_reg_read
wr32  0x3FF4882C
call  esp_dport_access_reg_read
wr32  0x3FF4E05C
```

### `phy_pwdet_always_en`  <sub>phy_chip_v7_cal.o</sub>

```
call  esp_dport_access_reg_read
wr32  0x3FF4E050
call  ets_delay_us
call  esp_dport_access_reg_read
call  esp_dport_access_reg_read
wr32  0x3FF4E050
```

## `librtc.a` - RTC / low-power domain and clock

60 functions touch a peripheral register.

### `BT_tx_8m_enable`  <sub>bt_bb.o</sub>

```
rd32  0x3FF51000
wr32  0x3FF51000
wr32  0x3FF51014
rd32  0x3FF51044
wr32  0x3FF51044
rd32  0x3FF51040
wr32  0x3FF51040
rd32  0x3FF51040
wr32  0x3FF51040
```

### `BT_tx_if_init`  <sub>bt_bb.o</sub>

```
rd32  0x3FF5103C
wr32  0x3FF5103C
call  phy_get_fetx_delay
rd32  0x3FF5103C
wr32  0x3FF5103C
rd32  0x3FF5103C
wr32  0x3FF5103C
rd32  0x3FF5103C
wr32  0x3FF5103C
rd32  0x3FF5103C
wr32  0x3FF5103C
```

### `BT_init_rx_filters`  <sub>bt_bb.o</sub>

```
wr32  0x3FF51028
rd32  0x3FF5105C
wr32  0x3FF5105C
rd32  0x3FF5105C
wr32  0x3FF5105C
rd32  0x3FF51058
wr32  0x3FF51058
rd32  0x3FF51020
wr32  0x3FF51020
rd32  0x3FF5C07C
wr32  0x3FF5C07C
rd32  0x3FF5C090
wr32  0x3FF5C090
rd32  0x3FF5C090
wr32  0x3FF5C090
rd32  0x3FF5C080
wr32  0x3FF5C080
rd32  0x3FF5C0D0
wr32  0x3FF5C0D0
rd32  0x3FF5C080
wr32  0x3FF5C080
rd32  0x3FF5104C
wr32  0x3FF5104C
rd32  0x3FF5C07C
wr32  0x3FF5C07C
rd32  0x3FF5C080
wr32  0x3FF5C080
rd32  0x3FF5C07C
wr32  0x3FF5C07C
rd32  0x3FF5C080
wr32  0x3FF5C080
rd32  0x3FF5C084
wr32  0x3FF5C084
rd32  0x3FF5C084
wr32  0x3FF5C084
rd32  0x3FF5C084
wr32  0x3FF5C084
rd32  0x3FF5C084
wr32  0x3FF5C084
rd32  0x3FF5106C
wr32  0x3FF5106C
rd32  0x60032124
wr32  0x60032124
rd32  0x3FF51084
wr32  0x3FF51084
```

### `bt_dgmixer_fstep_250k`  <sub>bt_bb.o</sub>

```
rd32  0x3FF51040
wr32  0x3FF51040
```

### `bt_rfoffset_en`  <sub>bt_bb.o</sub>

```
rd32  0x3FF51020
wr32  0x3FF51020
rd32  0x3FF51040
wr32  0x3FF51040
rd32  0x3FF51020
wr32  0x3FF51020
```

### `bt_bb_init_cmplx`  <sub>bt_bb.o</sub>

```
call  phy_dis_hw_set_freq
call  ets_delay_us
rd32  0x3FF51000
wr32  0x3FF51000
wr32  0x3FF51014
rd32  0x3FF51044
wr32  0x3FF51044
rd32  0x3FF51040
wr32  0x3FF51040
rd32  0x3FF51040
wr32  0x3FF51040
call  BT_tx_if_init
call  BT_init_rx_filters
rd32  0x3FF51040
wr32  0x3FF51040
rd32  0x60032130
wr32  0x60032130
rd32  0x60032130
wr32  0x60032130
rd32  0x600320F4
wr32  0x600320F4
rd32  0x600320F4
wr32  0x600320F4
rd32  0x600320F0
wr32  0x600320F0
rd32  0x600320F0
wr32  0x600320F0
rd32  0x600320DC
wr32  0x600320DC
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  g_phyFuns
rd32  0x6000604C
wr32  0x6000604C
call  phy_en_hw_set_freq
```

### `bt_bb_init_cmplx_reg`  <sub>bt_bb.o</sub>

```
rd32  0x3FF51000
wr32  0x3FF51000
wr32  0x3FF51014
rd32  0x3FF51044
wr32  0x3FF51044
rd32  0x3FF51040
wr32  0x3FF51040
rd32  0x3FF51040
wr32  0x3FF51040
call  BT_tx_if_init
call  BT_init_rx_filters
rd32  0x3FF51040
wr32  0x3FF51040
```

### `rw_coex_on`  <sub>bt_bb.o</sub>

```
rd32  0x600310D0
wr32  0x600310D0
rd32  0x60031300
wr32  0x60031300
rd32  0x60033D38
wr32  0x60033D38
rd32  0x60033D30
wr32  0x60033D30
rd32  0x60033D30
wr32  0x60033D30
```

### `force_bt_mode`  <sub>bt_bb.o</sub>

```
rd32  0x600310D0
wr32  0x600310D0
rd32  0x60031300
wr32  0x60031300
rd32  0x3FF5C080
wr32  0x3FF5C080
rd32  0x60033D30
wr32  0x60033D30
rd32  0x60033D30
wr32  0x60033D30
rd32  0x60033D38
wr32  0x60033D38
rd32  0x60033D40
wr32  0x60033D40
rd32  0x600041C4
wr32  0x600041C4
rd32  0x3FF5D040
wr32  0x3FF5D040
```

### `force_wifi_mode`  <sub>bt_bb.o</sub>

```
rd32  0x3FF51098
wr32  0x3FF51098
rd32  0x600310D0
wr32  0x600310D0
rd32  0x60031300
wr32  0x60031300
rd32  0x3FF5C080
wr32  0x3FF5C080
rd32  0x600041C4
wr32  0x600041C4
call  phy_force_wifi_chan
rd32  0x3FF5D040
wr32  0x3FF5D040
```

### `unforce_wifi_mode`  <sub>bt_bb.o</sub>

```
rd32  0x3FF5110C
rd32  0x3FF5110C
call  phy_unforce_wifi_chan
rd32  0x3FF5C080
wr32  0x3FF5C080
rd32  0x600041C4
wr32  0x600041C4
rd32  0x3FF5D040
wr32  0x3FF5D040
rd32  0x3FF51098
wr32  0x3FF51098
```

### `coex_bt_high_prio`  <sub>bt_bb.o</sub>

```
rd32  0x3FF5C080
wr32  0x3FF5C080
rd32  0x600310D0
wr32  0x600310D0
rd32  0x60031300
wr32  0x60031300
rd32  0x60033D30
wr32  0x60033D30
rd32  0x60033D30
wr32  0x60033D30
rd32  0x60033D30
wr32  0x60033D30
rd32  0x60033D38
wr32  0x60033D38
rd32  0x60033D38
wr32  0x60033D38
rd32  0x60033D40
wr32  0x60033D40
rd32  0x600041C4
wr32  0x600041C4
rd32  0x600310D0
wr32  0x600310D0
rd32  0x60031300
wr32  0x60031300
rd32  0x3FF5D040
wr32  0x3FF5D040
```

### `pm_wakeup_opt`  <sub>pm.o</sub>

```
rd32  0x3FF48038
wr32  0x3FF48038
wr32  0x3FF48064
```

### `pm_goto_sleep`  <sub>pm.o</sub>

```
rd32  0x3FF48048
wr32  0x3FF48048
rd32  0x3FF48018
wr32  0x3FF48018
rd32  0x3FF48040
```

### `pm_sleep_set_mac`  <sub>pm.o</sub>

```
rd32  0x60033CB8
wr32  0x60033CB8
rd32  0x60033CB8
rd32  0x60033CB8
wr32  0x60033CB8
rd32  0x60033CB8
call  ets_delay_us
rd32  0x60033CB8
call  ets_delay_us
rd32  0x60033CB8
```

### `pm_set_wakeup_mac`  <sub>pm.o</sub>

```
rd32  0x60033CB8
wr32  0x60033CB8
```

### `pm_mac_init`  <sub>pm.o</sub>

```
rd32  0x60033CB8
wr32  0x60033CB8
```

### `pm_set_sleep_mode_full`  <sub>pm.o</sub>

```
call  phy_close_rf
call  pm_sleep_set_mac
rd32  0x3FF48088
wr32  0x3FF48088
call  ets_delay_us
rd32  0x3FF48084
wr32  0x3FF48084
rd32  0x3FF48078
wr32  0x3FF48078
call  rtc_slp_prep
rd32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF480B4
call  uart_div_modify
rd32  0x3FF48084
rd32  0x3FF48084
rd32  0x3FF48080
rd32  0x3FF48080
rd32  0x3FF48078
wr32  0x3FF48078
call  rtc_slp_prep
rd32  0x3FF480B4
call  uart_div_modify
rd32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48084
rd32  0x3FF48084
rd32  0x3FF48080
rd32  0x3FF48080
call  rtc_printf
call  ets_delay_us
rd32  0x3FF48078
wr32  0x3FF48078
rd32  0x3FF48078
wr32  0x3FF48078
call  rtc_slp_prep
call  rtc_slp_prep
call  rtc_slp_prep
```

### `rtc_cmd_wakeup_conf`  <sub>rtc.o</sub>

```
rd32  0x3FF48830
wr32  0x3FF48830
rd32  0x3FF48830
wr32  0x3FF48830
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF4882C
wr32  0x3FF4882C
```

### `rtc_pads_muxsel`  <sub>rtc.o</sub>

```
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_pads_funsel`  <sub>rtc.o</sub>

```
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_pads_slpsel`  <sub>rtc.o</sub>

```
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_pads_slpoe`  <sub>rtc.o</sub>

```
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_pads_slpie`  <sub>rtc.o</sub>

```
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_pads_funie`  <sub>rtc.o</sub>

```
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_pads_pu`  <sub>rtc.o</sub>

```
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_pads_pd`  <sub>rtc.o</sub>

```
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_pads_hold`  <sub>rtc.o</sub>

```
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48494
wr32  0x3FF48494
rd32  0x3FF48498
wr32  0x3FF48498
rd32  0x3FF4849C
wr32  0x3FF4849C
rd32  0x3FF484A0
wr32  0x3FF484A0
rd32  0x3FF484A4
wr32  0x3FF484A4
rd32  0x3FF484A8
wr32  0x3FF484A8
rd32  0x3FF484AC
wr32  0x3FF484AC
rd32  0x3FF484B0
wr32  0x3FF484B0
```

### `rtc_apbbridge_sel`  <sub>rtc.o</sub>

```
rd32  0x3FF48018
wr32  0x3FF48018
```

### `rtc_powerup_rf`  <sub>rtc.o</sub>

```
rd32  0x3FF48030
wr32  0x3FF48030
```

### `rtc_powerdown_rf`  <sub>rtc.o</sub>

```
rd32  0x3FF48030
wr32  0x3FF48030
```

### `rtc_get_st`  <sub>rtc.o</sub>

```
rd32  0x3FF480C0
```

### `rtc_is_st_idle`  <sub>rtc.o</sub>

```
rd32  0x3FF480C0
```

### `rtc_soc_clk_ck12m`  <sub>rtc.o</sub>

```
rd32  0x3FF48070
wr32  0x3FF48070
rd32  0x3FF48000
wr32  0x3FF48000
```

### `rtc_init_full`  <sub>rtc.o</sub>

```
rd32  0x3FF4808C
wr32  0x3FF4808C
rd32  0x3FF4801C
wr32  0x3FF4801C
rd32  0x3FF4801C
wr32  0x3FF4801C
rd32  0x3FF4801C
wr32  0x3FF4801C
rd32  0x3FF48070
wr32  0x3FF48070
wr32  0x3FF48070
rd32  0x3FF48000
wr32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48084
wr32  0x3FF48084
rd32  0x3FF48084
wr32  0x3FF48084
rd32  0x3FF48084
wr32  0x3FF48084
rd32  0x3FF48084
wr32  0x3FF48084
rd32  0x3FF48080
wr32  0x3FF48080
rd32  0x3FF48088
wr32  0x3FF48088
rd32  0x3FF48088
wr32  0x3FF48088
rd32  0x3FF48088
wr32  0x3FF48088
rd32  0x3FF48080
wr32  0x3FF48080
rd32  0x3FF48088
wr32  0x3FF48088
rd32  0x3FF48088
wr32  0x3FF48088
rd32  0x3FF48078
wr32  0x3FF48078
rd32  0x3FF48078
wr32  0x3FF48078
rd32  0x3FF48078
wr32  0x3FF48078
rd32  0x3FF48078
wr32  0x3FF48078
rd32  0x3FF4807C
wr32  0x3FF4807C
rd32  0x3FF4807C
wr32  0x3FF4807C
rd32  0x3FF4807C
wr32  0x3FF4807C
rd32  0x3FF4807C
wr32  0x3FF4807C
call  rtc_init_clk
```

### `rtc_pad_gpio_wakeup`  <sub>rtc.o</sub>

```
call  rtc_pads_muxsel
call  rtc_pads_funsel
rd32  0x3FF4840C
wr32  0x3FF4840C
call  rtc_pads_slpsel
call  rtc_pads_slpoe
call  rtc_pads_slpie
call  rtc_pads_funie
call  rtc_pads_pu
call  rtc_pads_pd
rd32  0x3FF48428
wr32  0x3FF48428
rd32  0x3FF48428
wr32  0x3FF48428
```

### `rtc_pad_ext_wakeup`  <sub>rtc.o</sub>

```
call  rtc_pads_muxsel
call  rtc_pads_funsel
rd32  0x3FF4840C
wr32  0x3FF4840C
call  rtc_pads_slpsel
call  rtc_pads_slpoe
call  rtc_pads_slpie
call  rtc_pads_funie
call  rtc_pads_pu
call  rtc_pads_pd
rd32  0x3FF484BC
wr32  0x3FF484BC
rd32  0x3FF48060
wr32  0x3FF48060
```

### `rtc_cmd_ext_wakeup`  <sub>rtc.o</sub>

```
call  rtc_pads_muxsel
call  rtc_pads_funsel
rd32  0x3FF4840C
wr32  0x3FF4840C
call  rtc_pads_slpsel
call  rtc_pads_slpoe
call  rtc_pads_slpie
call  rtc_pads_funie
call  rtc_pads_pu
call  rtc_pads_pd
rd32  0x3FF484BC
wr32  0x3FF484BC
rd32  0x3FF48060
wr32  0x3FF48060
```

### `rtc_wifi_force_pd`  <sub>rtc.o</sub>

```
rd32  0x3FF000CC
wr32  0x3FF000CC
rd32  0x3FF000D0
wr32  0x3FF000D0
rd32  0x3FF48088
wr32  0x3FF48088
rd32  0x3FF48084
wr32  0x3FF48084
```

### `rtc_sdreg_off`  <sub>rtc.o</sub>

```
rd32  0x3FF48074
wr32  0x3FF48074
rd32  0x3FF48074
wr32  0x3FF48074
rd32  0x3FF48074
wr32  0x3FF48074
```

### `cfg_sdio_volt`  <sub>rtc.o</sub>

```
rd32  0x3FF48074
wr32  0x3FF48074
rd32  0x3FF48074
wr32  0x3FF48074
rd32  0x3FF48074
wr32  0x3FF48074
rd32  0x3FF48074
wr32  0x3FF48074
rd32  0x3FF48074
wr32  0x3FF48074
rd32  0x3FF48074
wr32  0x3FF48074
rd32  0x3FF48074
wr32  0x3FF48074
```

### `temprature_sens_read`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF4884C
wr32  0x3FF4884C
rd32  0x3FF4884C
wr32  0x3FF4884C
rd32  0x3FF4884C
wr32  0x3FF4884C
rd32  0x3FF4884C
wr32  0x3FF4884C
call  ets_delay_us
rd32  0x3FF4884C
wr32  0x3FF4884C
call  ets_delay_us
rd32  0x3FF48844
rd32  0x3FF4884C
wr32  0x3FF4884C
rd32  0x3FF4884C
wr32  0x3FF4884C
rd32  0x3FF4884C
wr32  0x3FF4884C
```

### `dac_out`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF48898
wr32  0x3FF48898
rd32  0x3FF48898
wr32  0x3FF48898
rd32  0x3FF48898
wr32  0x3FF48898
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48484
wr32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF48488
wr32  0x3FF48488
wr32  0x3FF48488
```

### `touch_init`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF48490
wr32  0x3FF48490
rd32  0x3FF48490
wr32  0x3FF48490
rd32  0x3FF48490
wr32  0x3FF48490
rd32  0x3FF48490
wr32  0x3FF48490
rd32  0x3FF48884
wr32  0x3FF48884
rd32  0x3FF4888C
wr32  0x3FF4888C
rd32  0x3FF48494
wr32  0x3FF48494
```

### `touch_read`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF48858
wr32  0x3FF48858
rd32  0x3FF48858
wr32  0x3FF48858
rd32  0x3FF48884
wr32  0x3FF48884
rd32  0x3FF48884
rd32  0x3FF48884
wr32  0x3FF48884
```

### `vdd33_init`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF460C0
wr32  0x3FF460C0
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF4E050
wr32  0x3FF4E050
call  g_phyFuns
rd32  0x3FF48890
wr32  0x3FF48890
rd32  0x3FF48030
wr32  0x3FF48030
rd32  0x3FF4880C
wr32  0x3FF4880C
rd32  0x3FF4E05C
wr32  0x3FF4E05C
rd32  0x3FF4E05C
wr32  0x3FF4E05C
rd32  0x3FF48894
wr32  0x3FF48894
rd32  0x3FF48838
wr32  0x3FF48838
rd32  0x3FF66010
wr32  0x3FF66010
```

### `get_vdd33`  <sub>rtc_analog.o</sub>

```
call  (indirect)
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF4E05C
wr32  0x3FF4E05C
rd32  0x3FF4E050
rd32  0x3FF4E050
wr32  0x3FF4E050
rd32  0x3FF4E050
wr32  0x3FF4E050
call  ets_delay_us
rd32  0x3FF4E050
rd32  0x3FF4E080
call  g_phyFuns
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF4E05C
wr32  0x3FF4E05C
```

### `adc1_read_test`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF48800
wr32  0x3FF48800
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF48800
wr32  0x3FF48800
rd32  0x3FF4880C
wr32  0x3FF4880C
rd32  0x3FF48834
wr32  0x3FF48834
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF488A0
wr32  0x3FF488A0
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48808
wr32  0x3FF48808
rd32  0x3FF48808
wr32  0x3FF48808
rd32  0x3FF4880C
wr32  0x3FF4880C
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF4883C
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF48854
rd32  0x3FF48854
rd32  0x3FF48854
wr32  0x3FF48854
```

### `adc1_amp_read_full`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF48800
wr32  0x3FF48800
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF48800
wr32  0x3FF48800
rd32  0x3FF48834
wr32  0x3FF48834
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48810
wr32  0x3FF48810
rd32  0x3FF48808
wr32  0x3FF48808
rd32  0x3FF48808
wr32  0x3FF48808
rd32  0x3FF4880C
wr32  0x3FF4880C
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF48854
wr32  0x3FF48854
rd32  0x3FF48854
rd32  0x3FF48854
rd32  0x3FF48854
wr32  0x3FF48854
call  ets_delay_us
rd32  0x3FF48854
```

### `hall_sens_read_full`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF48858
wr32  0x3FF48858
rd32  0x3FF48478
wr32  0x3FF48478
rd32  0x3FF48858
wr32  0x3FF48858
rd32  0x3FF48478
wr32  0x3FF48478
call  ets_delay_us
call  adc1_read_test
call  adc1_read_test
rd32  0x3FF48478
wr32  0x3FF48478
call  adc1_read_test
call  adc1_read_test
rd32  0x3FF48858
wr32  0x3FF48858
rd32  0x3FF48858
wr32  0x3FF48858
```

### `hall_sens_amp_read_full`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF48858
wr32  0x3FF48858
rd32  0x3FF48478
wr32  0x3FF48478
rd32  0x3FF48858
wr32  0x3FF48858
rd32  0x3FF48478
wr32  0x3FF48478
call  ets_delay_us
call  adc1_amp_read_full
rd32  0x3FF48478
wr32  0x3FF48478
call  adc1_amp_read_full
rd32  0x3FF48858
wr32  0x3FF48858
rd32  0x3FF48858
wr32  0x3FF48858
```

### `adc2_read_test`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF48890
wr32  0x3FF48890
rd32  0x3FF48890
wr32  0x3FF48890
rd32  0x3FF48894
wr32  0x3FF48894
rd32  0x3FF48838
wr32  0x3FF48838
rd32  0x3FF48894
wr32  0x3FF48894
rd32  0x3FF4E050
rd32  0x3FF4E050
wr32  0x3FF4E050
rd32  0x3FF48890
wr32  0x3FF48890
rd32  0x3FF48894
wr32  0x3FF48894
rd32  0x3FF4883C
rd32  0x3FF48894
wr32  0x3FF48894
rd32  0x3FF48894
wr32  0x3FF48894
rd32  0x3FF48894
rd32  0x3FF48894
rd32  0x3FF48894
wr32  0x3FF48894
rd32  0x3FF48894
wr32  0x3FF48894
rd32  0x3FF4E050
wr32  0x3FF4E050
rd32  0x3FF48890
wr32  0x3FF48890
```

### `adc_pad_init`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4847C
wr32  0x3FF4847C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF4848C
wr32  0x3FF4848C
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
rd32  0x3FF48480
wr32  0x3FF48480
```

### `dac_pad_init`  <sub>rtc_analog.o</sub>

```
rd32  0x3FF4889C
wr32  0x3FF4889C
rd32  0x3FF48898
wr32  0x3FF48898
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48484
wr32  0x3FF48484
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF48488
wr32  0x3FF48488
rd32  0x3FF48488
wr32  0x3FF48488
```

### `rtc_wifi_force_pd_off`  <sub>rtc_cntl.o</sub>

```
rd32  0x3FF48084
wr32  0x3FF48084
rd32  0x3FF48088
wr32  0x3FF48088
rd32  0x3FF000D0
wr32  0x3FF000D0
rd32  0x3FF000CC
wr32  0x3FF000CC
```

### `rtc_digital_lp_mode_off_stg1`  <sub>rtc_cntl.o</sub>

```
rd32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48000
wr32  0x3FF48000
rd32  0x3FF48084
wr32  0x3FF48084
rd32  0x3FF48088
wr32  0x3FF48088
wr32  0x3FF000C0
wr32  0x3FF000CC
rd32  0x3FF48084
wr32  0x3FF48084
rd32  0x3FF48088
wr32  0x3FF48088
rd32  0x3FF000D0
wr32  0x3FF000D0
rd32  0x3FF000CC
wr32  0x3FF000CC
call  rtc_dbias_cfg
```

### `rtc_digital_lp_mode_off_stg2`  <sub>rtc_cntl.o</sub>

```
rd32  0x3FF48000
wr32  0x3FF48000
```

### `rtc_sar_sleep_timer_start`  <sub>rtc_cntl.o</sub>

```
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF4882C
wr32  0x3FF4882C
rd32  0x3FF4882C
wr32  0x3FF4882C
wr32  0x3FF48818
rd32  0x3FF4882C
wr32  0x3FF4882C
```

### `rtc_mac_tx_init`  <sub>rtc_mac.o</sub>

```
rd32  0x60033D24
wr32  0x60033D24
rd32  0x60033D24
rd32  0x60033C6C
wr32  0x60033C6C
rd32  0x60033C6C
wr32  0x60033C6C
rd32  0x60033C78
wr32  0x60033C78
rd32  0x600330D8
wr32  0x600330D8
rd32  0x600330DC
wr32  0x600330DC
rd32  0x600330E0
wr32  0x600330E0
rd32  0x600330E4
wr32  0x600330E4
rd32  0x60033C6C
wr32  0x60033C6C
wr32  0x60033040
wr32  0x60033044
rd32  0x60033C40
wr32  0x60033C40
rd32  0x60033C4C
wr32  0x60033C4C
rd32  0x3FF5C860
wr32  0x3FF5C860
rd32  0x3FF5C87C
wr32  0x3FF5C87C
wr32  0x3FF5C860
rd32  0x3FF5C87C
wr32  0x3FF5C87C
rd32  0x600340F0
wr32  0x600340F0
rd32  0x600340F0
wr32  0x600340F0
rd32  0x600340F0
wr32  0x600340F0
rd32  0x600340F0
wr32  0x600340F0
wr32  0x600340F8
rd32  0x600340FC
wr32  0x600340FC
rd32  0x60033CEC
wr32  0x60033CEC
rd32  0x60033CEC
wr32  0x60033CEC
rd32  0x60033CF0
wr32  0x60033CF0
rd32  0x60033CF0
wr32  0x60033CF0
rd32  0x60033CF0
wr32  0x60033CF0
rd32  0x60033CF0
wr32  0x60033CF0
```

### `rtc_mac_tx`  <sub>rtc_mac.o</sub>

```
rd32  0x60033C4C
wr32  0x60033C4C
rd32  0x60033CF0
wr32  0x60033CF0
rd32  0x60033C48
rd32  0x60033CC8
rd32  0x60033CC4
wr32  0x60033CC4
rd32  0x60034108
```

## `libpp.a` - WiFi MAC, the 802.11 packet processor

83 functions touch a peripheral register.

### `hal_ba_session_store`  <sub>hal_ampdu.o</sub>

```
rd32  0x3FF73250
rd32  0x3FF7325C
rd32  0x3FF73268
rd32  0x3FF7326C
```

### `hal_ba_session_restore`  <sub>hal_ampdu.o</sub>

```
rd32  0x3FF73250
rd32  0x3FF73250
wr32  0x3FF73250
rd32  0x3FF7325C
wr32  0x3FF7325C
wr32  0x3FF73260
wr32  0x3FF73264
rd32  0x3FF73250
wr32  0x3FF73250
```

### `hal_crypto_clr_key_entry`  <sub>hal_crypto.o</sub>

```
rd32  0x3FF73814
wr32  0x3FF73814
```

### `hal_crypto_get_using_key_idx`  <sub>hal_crypto.o</sub>

```
rd32  0x3FF7442C
```

### `hal_crypto_set_key_entry`  <sub>hal_crypto.o</sub>

```
call  wifi_log
rd32  0x3FF73814
wr32  0x3FF73814
call  memcpy
```

### `hal_crypto_is_key_valid`  <sub>hal_crypto.o</sub>

```
rd32  0x3FF73814
```

### `hal_crypto_init`  <sub>hal_crypto.o</sub>

```
wr32  0x3FF73800
wr32  0x3FF73804
wr32  0x3FF73808
wr32  0x3FF7380C
wr32  0x3FF73810
```

### `hal_crypto_enable`  <sub>hal_crypto.o</sub>

```
rd32  0x3FF73810
wr32  0x3FF73810
```

### `hal_crypto_disable`  <sub>hal_crypto.o</sub>

```
wr32  0x3FF73804
rd32  0x3FF73814
wr32  0x3FF73814
```

### `hal_random`  <sub>hal_mac.o</sub>

```
rd32  0x3FF75144
```

### `hal_mac_tx_set_cca`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73C58
wr32  0x3FF73C58
```

### `hal_mac_disable_low_rate`  <sub>hal_mac.o</sub>

```
call  phy_disable_low_rate
wr32  0x3FF7341C
wr32  0x3FF73418
rd32  0x6001C860
wr32  0x6001C860
rd32  0x6001C860
wr32  0x6001C860
rd32  0x6001C87C
wr32  0x6001C87C
```

### `hal_mac_enable_low_rate`  <sub>hal_mac.o</sub>

```
call  phy_enable_low_rate
wr32  0x3FF7341C
wr32  0x3FF73418
rd32  0x6001C860
wr32  0x6001C860
rd32  0x6001C860
wr32  0x6001C860
rd32  0x6001C87C
wr32  0x6001C87C
```

### `hal_mac_is_low_rate_enabled`  <sub>hal_mac.o</sub>

```
rd32  0x6001C860
rd32  0x6001C87C
```

### `mac_rxbuf_init`  <sub>hal_mac.o</sub>

```
wr32  0x3FF7311C
wr32  0x3FF73124
wr32  0x3FF73118
wr32  0x3FF73120
rd32  0x3FF73080
wr32  0x3FF73080
wr32  0x3FF73088
rd32  0x3FF73080
wr32  0x3FF73080
```

### `hal_disable_mac`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73C04
wr32  0x3FF73C04
rd32  0x3FF73C40
wr32  0x3FF73C40
wr32  0x3FF73C44
wr32  0x3FF73C30
```

### `hal_enable_mac`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73C04
wr32  0x3FF73C04
wr32  0x3FF73C40
wr32  0x3FF73088
```

### `hal_mac_rx_read_rxdscrlast`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73090
```

### `hal_mac_rx_read_rxdscrnext`  <sub>hal_mac.o</sub>

```
rd32  0x3FF7308C
```

### `hal_mac_rx_set_base`  <sub>hal_mac.o</sub>

```
wr32  0x3FF73088
```

### `mac_txrx_init`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73C6C
wr32  0x3FF73C6C
rd32  0x3FF73C6C
wr32  0x3FF73C6C
rd32  0x3FF730D8
wr32  0x3FF730D8
rd32  0x3FF730D8
wr32  0x3FF730D8
rd32  0x3FF730F8
wr32  0x3FF730F8
rd32  0x3FF730FC
wr32  0x3FF730FC
rd32  0x3FF730F8
wr32  0x3FF730F8
rd32  0x3FF730FC
wr32  0x3FF730FC
rd32  0x3FF73C74
wr32  0x3FF73C74
rd32  0x3FF7310C
wr32  0x3FF7310C
rd32  0x3FF73114
wr32  0x3FF73114
rd32  0x3FF73114
wr32  0x3FF73114
rd32  0x3FF73C78
wr32  0x3FF73C78
rd32  0x3FF73D78
wr32  0x3FF73D78
rd32  0x3FF73C54
wr32  0x3FF73C54
rd32  0x3FF73C54
wr32  0x3FF73C54
rd32  0x3FF73C1C
wr32  0x3FF73C1C
rd32  0x3FF73C1C
wr32  0x3FF73C1C
rd32  0x3FF73C1C
wr32  0x3FF73C1C
rd32  0x3FF73C20
wr32  0x3FF73C20
rd32  0x3FF73C24
wr32  0x3FF73C24
rd32  0x3FF73CAC
wr32  0x3FF73CAC
rd32  0x3FF73C5C
wr32  0x3FF73C5C
rd32  0x3FF73C5C
wr32  0x3FF73C5C
rd32  0x3FF73C88
wr32  0x3FF73C88
rd32  0x3FF73288
wr32  0x3FF73288
rd32  0x3FF73084
wr32  0x3FF73084
```

### `mac_last_rxbuf_init`  <sub>hal_mac.o</sub>

```
wr32  0x3FF7314C
wr32  0x3FF73158
wr32  0x3FF73164
rd32  0x3FF73148
wr32  0x3FF73148
rd32  0x3FF73148
wr32  0x3FF73148
rd32  0x3FF7309C
wr32  0x3FF7309C
```

### `mac_last_rxbuf_deinit`  <sub>hal_mac.o</sub>

```
wr32  0x3FF7314C
wr32  0x3FF73158
wr32  0x3FF73164
rd32  0x3FF73148
wr32  0x3FF73148
rd32  0x3FF73148
wr32  0x3FF73148
rd32  0x3FF7309C
wr32  0x3FF7309C
```

### `hal_deinit`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73288
wr32  0x3FF73288
rd32  0x3FF73C40
wr32  0x3FF73C40
wr32  0x3FF73C40
wr32  0x3FF73C4C
rd32  0x3FF73D24
wr32  0x3FF73D24
rd32  0x3FF73D24
```

### `hal_mac_interrupt_get_event`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73C48
```

### `hal_mac_interrupt_clr_event`  <sub>hal_mac.o</sub>

```
wr32  0x3FF73C4C
```

### `hal_mac_interrupt_clr_watchdog`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73C4C
wr32  0x3FF73C4C
```

### `hal_init`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73D24
wr32  0x3FF73D24
rd32  0x3FF73D24
wr32  0x3FF73C40
wr32  0x3FF73C4C
call  mac_txrx_init
call  hal_mac_rx_set_policy
call  mac_rxbuf_init
call  mac_last_rxbuf_init
call  hal_mac_rate_autoack_init
call  hal_mac_disable_low_rate
call  hal_crypto_init
call  hal_attenna_init
wr32  0x3FF73C40
call  bb_wdt_int_enable
rd32  0x3FF7309C
wr32  0x3FF7309C
```

### `hal_mac_tsf_get_time`  <sub>hal_mac.o</sub>

```
rd32  0x3FF75010
wr32  0x3FF75010
rd32  0x3FF75054
rd32  0x3FF75010
rd32  0x3FF75010
wr32  0x3FF75010
rd32  0x3FF75094
rd32  0x3FF75090
rd32  0x3FF75010
wr32  0x3FF75010
```

### `wDev_Mesh_Disable_Tsf`  <sub>hal_mac.o</sub>

```
rd32  0x3FF75020
wr32  0x3FF75020
```

### `hal_get_tsf_time`  <sub>hal_mac.o</sub>

```
rd32  0x3FF75010
wr32  0x3FF75010
rd32  0x3FF75018
rd32  0x3FF75014
rd32  0x3FF75010
rd32  0x3FF75010
wr32  0x3FF75010
rd32  0x3FF75058
rd32  0x3FF75054
rd32  0x3FF75010
wr32  0x3FF75010
```

### `hal_mac_tsf_set_time`  <sub>hal_mac.o</sub>

```
wr32  0x3FF75060
wr32  0x3FF75064
rd32  0x3FF75010
wr32  0x3FF75010
```

### `hal_mac_tsf_reset`  <sub>hal_mac.o</sub>

```
rd32  0x3FF7505C
wr32  0x3FF7505C
call  wDev_reset_bcnSendTick
wr32  0x3FF75060
wr32  0x3FF75064
rd32  0x3FF75010
wr32  0x3FF75010
rd32  0x3FF7505C
wr32  0x3FF7505C
rd32  0x3FF75020
wr32  0x3FF75020
wr32  0x3FF75024
wr32  0x3FF75028
rd32  0x3FF75010
wr32  0x3FF75010
rd32  0x3FF75020
wr32  0x3FF75020
rd32  0x3FF7505C
wr32  0x3FF7505C
wr32  0x3FF75060
wr32  0x3FF75064
rd32  0x3FF75010
wr32  0x3FF75010
rd32  0x3FF7505C
wr32  0x3FF7505C
```

### `hal_mac_set_csi`  <sub>hal_mac.o</sub>

```
call  config_get_wifi_csi_enable
rd32  0x3FF7309C
wr32  0x3FF7309C
rd32  0x3FF7309C
wr32  0x3FF7309C
```

### `hal_mac_init`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73CB8
wr32  0x3FF73CB8
```

### `hal_mac_deinit`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73CB8
wr32  0x3FF73CB8
call  ets_delay_us
```

### `hal_mac_rx_enable`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73084
wr32  0x3FF73084
```

### `hal_mac_rx_disable`  <sub>hal_mac.o</sub>

```
rd32  0x3FF73084
wr32  0x3FF73084
```

### `hal_mac_rx_get_end_state`  <sub>hal_mac_rx.o</sub>

```
rd32  0x3FF730A4
```

### `hal_mac_rx_get_last_dscr`  <sub>hal_mac_rx.o</sub>

```
rd32  0x3FF73090
```

### `hal_mac_rx_is_dscr_reload`  <sub>hal_mac_rx.o</sub>

```
rd32  0x3FF73084
```

### `hal_mac_rx_set_dscr_reload`  <sub>hal_mac_rx.o</sub>

```
rd32  0x3FF73084
wr32  0x3FF73084
```

### `hal_mac_rx_get_end_info`  <sub>hal_mac_rx.o</sub>

```
rd32  0x3FF73D84
rd32  0x3FF73D88
rd32  0x3FF730A4
rd32  0x3FF732F8
rd32  0x3FF732F4
rd32  0x3FF732F0
```

### `hal_mac_get_txq_state`  <sub>hal_mac_tx.o</sub>

```
rd32  0x3FF73CC0
rd32  0x3FF73CC0
rd32  0x3FF73CC8
```

### `hal_mac_clr_txq_state`  <sub>hal_mac_tx.o</sub>

```
rd32  0x3FF73CBC
rd32  0x3FF73CBC
rd32  0x3FF73CC4
wr32  0x3FF73CC4
```

### `hal_attenna_init`  <sub>hal_mac_tx.o</sub>

```
rd32  0x3FF73284
wr32  0x3FF73284
rd32  0x3FF73284
wr32  0x3FF73284
```

### `hal_mac_rate_autoack_init`  <sub>hal_mac_tx.o</sub>

```
wr32  0x3FF73404
wr32  0x3FF73400
wr32  0x3FF7340C
wr32  0x3FF73408
wr32  0x3FF73414
wr32  0x3FF73410
wr32  0x3FF7341C
wr32  0x3FF73418
```

### `hal_sniffer_enable`  <sub>hal_sniffer.o</sub>

```
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
```

### `hal_sniffer_disable`  <sub>hal_sniffer.o</sub>

```
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
rd32  0x3FF730E4
wr32  0x3FF730E4
```

### `hal_sniffer_rx_set_promis`  <sub>hal_sniffer.o</sub>

```
rd32  0x3FF73C40
rd32  0x3FF73104
wr32  0x3FF73104
rd32  0x3FF73104
wr32  0x3FF73104
rd32  0x3FF730F8
wr32  0x3FF730F8
rd32  0x3FF730FC
wr32  0x3FF730FC
rd32  0x3FF730F8
wr32  0x3FF730F8
rd32  0x3FF730FC
wr32  0x3FF730FC
rd32  0x3FF730F8
wr32  0x3FF730F8
rd32  0x3FF730FC
wr32  0x3FF730FC
```

### `hal_sniffer_rx_clr_statistics`  <sub>hal_sniffer.o</sub>

```
rd32  0x3FF73D7C
wr32  0x3FF73D7C
rd32  0x3FF73D7C
wr32  0x3FF73D7C
```

### `hal_enable_sta_tsf`  <sub>hal_tsf.o</sub>

```
rd32  0x3FF75020
wr32  0x3FF75020
```

### `hal_disable_sta_tsf`  <sub>hal_tsf.o</sub>

```
rd32  0x3FF75020
wr32  0x3FF75020
```

### `bb_intr_handl`  <sub>if_hwctrl.o</sub>

```
rd32  0x3FF5D04C
```

### `ic_set_vif`  <sub>if_hwctrl.o</sub>

```
call  wifi_log
call  wifi_log
call  wifi_set_rx_policy
call  ic_enable_sniffer
call  memcpy
call  __popcountsi2
rd32  0x3FF73084
wr32  0x3FF73084
call  ic_disable_crypto
call  rc_disable_trc_by_interface
call  __popcountsi2
rd32  0x3FF73084
wr32  0x3FF73084
call  pm_force_scan_unlock
call  wifi_log
call  wifi_log
call  wifi_set_rx_policy
call  ic_disable_sniffer
call  wifi_log
```

### `ic_enable_rx`  <sub>if_hwctrl.o</sub>

```
rd32  0x3FF73084
wr32  0x3FF73084
```

### `ic_disable_rx`  <sub>if_hwctrl.o</sub>

```
rd32  0x3FF73084
wr32  0x3FF73084
```

### `lmacSetTxFrame`  <sub>lmac.o</sub>

```
call  wDev_is_low_rate_enable
call  (indirect)
rd32  0x3FF73C00
call  hal_mac_tx_config_timeout
call  hal_mac_tx_set_ppdu
```

### `lmacProcessRxSucData`  <sub>lmac.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  pp_post
```

### `lmacMSDUAged`  <sub>lmac.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
```

### `lmacAdjustTimestamp`  <sub>lmac.o</sub>

```
rd32  0x3FF73C00
```

### `pm_update_next_tbtt`  <sub>pm.o</sub>

```
call  (indirect)
call  __umoddi3
call  (indirect)
call  (indirect)
call  __udivdi3
call  (indirect)
call  __umoddi3
call  __udivdi3
call  __umoddi3
call  (indirect)
rd32  0x3FF73C00
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
call  (indirect)
rd32  0x3FF73C00
call  (indirect)
call  (indirect)
call  (indirect)
rd32  0x3FF73C00
call  esp_mesh_quick_funcs
call  esp_mesh_quick_funcs
call  (indirect)
rd32  0x3FF73C00
call  g_pm
```

### `pm_coex_tbtt_process`  <sub>pm.o</sub>

```
call  (indirect)
call  g_pm+0xf4
call  g_pm+0xf4
call  (indirect)
call  pm_coex_schm_process_restart
call  (indirect)
rd32  0x3FF73CB8
wr32  0x3FF73CB8
call  pm_go_to_wake
```

### `pm_coex_slice_timeout_process`  <sub>pm.o</sub>

```
call  (indirect)
call  (indirect)
call  wifi_gpio_debug
call  pm_enable_disconnected_sleep_delay_timer
rd32  0x3FF73CB8
wr32  0x3FF73CB8
call  ppCheckTxIdle
call  pm_go_to_sleep
call  pm_enable_sleep_delay_timer
call  wifi_gpio_debug
```

### `pm_tbtt_process`  <sub>pm.o</sub>

```
call  (indirect)
call  pm_check_state
call  (indirect)
call  (indirect)
rd32  0x3FF73CB8
wr32  0x3FF73CB8
call  pm_enable_beacon_monitor_timer
call  pm_dream
call  pm_coex_tbtt_process
call  pm_noise_check
call  pm_mesh_set_next_tbtt
call  pm_set_next_tbtt
call  pm_update_params
```

### `pm_stop`  <sub>pm.o</sub>

```
rd32  0x3FF73CB8
wr32  0x3FF73CB8
call  pm_disable_sleep_delay_timer
call  .text.pm_disable_active_timer
rd32  0x3FF73DA4
call  (indirect)
rd32  0x3FF73DA4
call  (indirect)
call  pm_dream
call  ppProcessWaitingQueue
call  g_pm+0xb8
call  wifi_log
call  pm_on_coex_schm_status_config
call  (indirect)
call  pm_coex_schm_process_restart
call  pm_update_by_connectionless_status
call  pm_enable_disconnected_sleep_delay_timer
call  (indirect)
call  esp_mesh_get_running_active_duty_cycle
call  __floatundisf
call  __floatundisf
call  __divsf3
call  __extendsfdf2
call  wifi_log
call  ieee80211_hostapd_beacon_txcb
```

### `ppRxFragmentProc`  <sub>pp.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  ppRecycleRxPkt
call  pp_hdrsize
call  ic_get_ptk_alg
call  ic_get_ptk_alg
call  wifi_log
call  ppRecycleRxPkt
call  pp_gettid
call  pp_gettid
call  memcmp
call  memcpy
call  ppRecycleRxPkt
call  ic_get_ptk_alg
call  memcpy
call  ic_obtain_key
call  ppCalTkipMic
call  memcmp
call  .bss.s_michael_mic_failure_cb
call  ppRecycleRxPkt
call  ppRecycleRxPkt
call  ppRecycleRxPkt
```

### `ppReSendBar`  <sub>pp.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  rcGetRate
call  lmacTxFrame
```

### `ppTxPkt`  <sub>pp.o</sub>

```
call  ic_interface_enabled
call  wifi_log
call  wifi_log
call  ppTxProtoProc
call  ppProcTxSecFrame
call  rcGetSched
call  ppCalFrameTimes
call  ppMapTxQueue
call  (indirect)
rd32  0x3FF73C00
call  lmacIsIdle
call  pp_post
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
call  esf_buf_recycle
call  pp_process_hmac_waiting_txq
```

### `dbg_lmac_rxtx_statis_dump`  <sub>pp_debug.o</sub>

```
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
rd32  0x3FF732CC
rd32  0x3FF732D0
rd32  0x3FF732AC
rd32  0x3FF732DC
call  wifi_log
```

### `dbg_lmac_hw_statis_dump`  <sub>pp_debug.o</sub>

```
call  wifi_log
call  wifi_log
call  wifi_log
rd32  0x3FF73094
rd32  0x3FF7328C
rd32  0x3FF73290
rd32  0x3FF73294
rd32  0x3FF73298
rd32  0x3FF7329C
rd32  0x3FF732A0
rd32  0x3FF732A4
call  wifi_log
call  wifi_log
rd32  0x3FF732A8
rd32  0x3FF732B0
rd32  0x3FF732B4
rd32  0x3FF732B8
rd32  0x3FF732BC
rd32  0x3FF732C0
rd32  0x3FF732C4
rd32  0x3FF732C8
call  wifi_log
call  wifi_log
rd32  0x3FF732E0
rd32  0x3FF732D8
rd32  0x3FF73D58
rd32  0x3FF73D5C
rd32  0x3FF73D60
rd32  0x3FF73D64
rd32  0x3FF73D68
rd32  0x3FF73D6C
rd32  0x3FF73D70
call  wifi_log
```

### `dbg_lmac_diag_statis_dump`  <sub>pp_debug.o</sub>

```
call  wifi_log
call  wifi_log
call  wifi_log
rd32  0x3FF73DAC
rd32  0x3FF73DB0
rd32  0x3FF73DB4
rd32  0x3FF73DB8
rd32  0x3FF732F0
rd32  0x3FF732F4
rd32  0x3FF732F8
call  wifi_log
call  wifi_log
rd32  0x3FF732FC
rd32  0x3FF73424
rd32  0x3FF73428
rd32  0x3FF7342C
rd32  0x3FF73430
rd32  0x3FF73DBC
call  wifi_log
```

### `dbg_perf_throughput_cal`  <sub>pp_debug.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
```

### `rcLowerSched`  <sub>trc.o</sub>

```
call  rcClearCurSched
call  (indirect)
rd32  0x3FF73C00
```

### `rcUpSched`  <sub>trc.o</sub>

```
call  rcClearCurSched
call  (indirect)
rd32  0x3FF73C00
```

### `rcAmpduLowerRate`  <sub>trc.o</sub>

```
call  TRC_AMPDU_PER_DOWN_THRESHOLD
call  trcAmpduSetState
call  rcClearCurSched
call  trc_onAmpduOp
call  (indirect)
rd32  0x3FF73C00
call  rx11NRate2AMPDULimit
call  rcSetTxAmpduLimit
call  rcClearCurAMPDUSched
```

### `rcUpdateTxDoneAmpdu2`  <sub>trc.o</sub>

```
call  rcUpdateAckSnr
call  (indirect)
call  (indirect)
call  TRC_AMPDU_PER_DOWN_THRESHOLD
call  (indirect)
rd32  0x3FF73C00
call  trc_calc_duration
call  trc_calc_duration
call  rcAmpduLowerRate
call  TRC_AMPDU_PER_UP_THRESHOLD
call  trc_calc_duration
call  (indirect)
rd32  0x3FF73C00
call  rx11NRate2AMPDULimit
call  rcSetTxAmpduLimit
call  rcClearCurAMPDUSched
```

### `rcUpdateRate`  <sub>trc.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  trc_calc_duration
call  trc_calc_duration
call  rcClearCurSched
call  (indirect)
call  trc_calc_duration
call  TRC_PER_IS_GOOD
call  rssi_margin
call  trc_calc_duration
call  rcUpSched
call  rcLowerSched
```

### `wdev_mac_wakeup`  <sub>wdev.o</sub>

```
call  g_osi_funcs_p
rd32  0x3FF73C04
wr32  0x3FF73C04
rd32  0x3FF73084
wr32  0x3FF73084
call  wdev_mac_reg_load
call  hal_enable_mac
call  wdev_mac_special_reg_load
rd32  0x3FF73084
wr32  0x3FF73084
```

### `wdev_mac_sleep`  <sub>wdev.o</sub>

```
rd32  0x3FF73084
wr32  0x3FF73084
call  wdev_is_data_in_rxlist
rd32  0x3FF73084
wr32  0x3FF73084
call  wdev_mac_special_reg_store
call  hal_disable_mac
call  wdev_mac_reg_store
call  g_osi_funcs_p
```

### `wdev_set_promis_misc_pkt`  <sub>wdev.o</sub>

```
call  hal_sniffer_rx_set_promis
rd32  0x3FF730F4
wr32  0x3FF730F4
call  hal_sniffer_rx_set_promis
rd32  0x3FF730F4
wr32  0x3FF730F4
```

### `wdev_process_panic_watchdog`  <sub>wdev.o</sub>

```
call  hal_mac_rx_get_end_state
call  bb_wdt_get_status
rd32  0x3FF40078
wr32  0x3FF00024
rd32  0x3FF40078
wr32  0x3FF00024
call  bb_wdt_timeout_clear
call  hal_mac_interrupt_clr_watchdog
```

### `wDev_ProcessFiq`  <sub>wdev.o</sub>

```
call  (indirect)
call  hal_mac_interrupt_get_event
call  hal_mac_interrupt_clr_event
call  wdev_process_panic_watchdog
call  pp_post
call  hal_mac_rx_get_end_info
rd32  0x6001C06C
call  hal_sniffer_rx_clr_statistics
call  wdev_push_promis_misc_buf
call  pp_post
call  lmacProcessRxSucData
call  lmacPostTxComplete
call  lmacProcessAllTxTimeout
call  lmacProcessCollisions
call  (indirect)
```

## `libnet80211.a` - 802.11 MLME: scan, auth, assoc

25 functions touch a peripheral register.

### `ieee80211_send_action_vendor_spec`  <sub>ieee80211_action_vendor.o</sub>

```
call  wifi_get_macaddr
call  get_iav_key
call  (indirect)
call  memcmp
call  cnx_node_search
call  (indirect)
call  ieee80211_alloc_action_vendor_spec
call  (indirect)
call  memcpy
call  memcpy
call  memcpy
call  (indirect)
rd32  0x3FF73C00
call  ic_get_default_sched
call  ic_get_espnow_rate
call  ieee80211_post_hmac_tx
call  .data.s_global_vendor_seq$10155
```

### `esp_wifi_stop`  <sub>ieee80211_api.o</sub>

```
call  wifi_api_lock
call  (indirect)
call  wifi_api_unlock
call  wifi_log
call  (indirect)
call  wifi_api_unlock
call  wifi_api_unlock
call  ieee80211_ioctl
call  (indirect)
call  ieee80211_ioctl
call  (indirect)
call  ieee80211_ioctl
rd32  0x3FF73CB8
call  wifi_log
call  wifi_log
call  (indirect)
call  ieee80211_ioctl
call  wifi_log
call  wifi_api_lock
call  wifi_api_unlock
```

### `ieee80211_hostap_send_beacon_process`  <sub>ieee80211_hostap.o</sub>

```
call  ic_interface_enabled
call  wifi_log
call  (indirect)
call  ieee80211_sta_is_connected
call  esp_mesh_get_running_active_duty_cycle
call  wifi_mesh_event_post
call  ieee80211_beacon_alloc
call  wifi_log
call  chm_get_home_channel
call  hal_get_tsf_time
call  __divdi3
call  (indirect)
rd32  0x3FF73C00
call  chm_is_at_home_channel
call  ic_tx_pkt
call  g_beacon_idx
call  ic_get_next_tbtt
call  g_osi_funcs_p
```

### `hostap_handle_timer_process`  <sub>ieee80211_hostap.o</sub>

```
call  wifi_log
call  cnx_node_is_existing
call  wifi_log
call  (indirect)
rd32  0x3FF73C00
call  (indirect)
call  ieee80211_send_mgmt
call  ieee80211_send_mgmt
call  wifi_log
call  ieee80211_send_mgmt
call  cnx_node_leave
call  pwrsave_flushq
```

### `ieee80211_hostapd_data_txcb`  <sub>ieee80211_hostap.o</sub>

```
call  cnx_node_search
call  (indirect)
rd32  0x3FF73C00
```

### `hostap_auth_open`  <sub>ieee80211_hostap.o</sub>

```
call  wifi_log
call  g_osi_funcs_p
call  cnx_node_search
call  cnx_node_alloc
call  wifi_log
call  ieee80211_send_mgmt
call  memcpy
call  wifi_event_post
call  ieee80211_send_mgmt
call  (indirect)
rd32  0x3FF73C00
call  memcpy
call  pwrsave_flushq
call  ieee80211_psq_init
call  g_wifi_mac_time_delta
call  g_wifi_mac_time_delta
call  g_wifi_mac_time_delta
call  g_osi_funcs_p
```

### `hostap_recv_ctl`  <sub>ieee80211_hostap.o</sub>

```
call  wifi_log
call  ieee80211_send_mgmt
call  (indirect)
rd32  0x3FF73C00
call  ieee80211_send_nulldata
call  ieee80211_set_tim
call  chm_is_at_home_channel
call  ic_tx_pkt
call  ieee80211_recv_bar
```

### `hostap_input`  <sub>ieee80211_hostap.o</sub>

```
call  wifi_get_macaddr
call  .text.ieee80211_hdrsize
call  ieee80211_rfid_locp_recv
call  cnx_node_search
call  cnx_node_search
call  wifi_log
call  ieee80211_send_deauth
call  memcmp
call  memcmp
call  cnx_node_search
call  ieee80211_ethbroadcast
rd32  0x3FF73C00
call  cnx_rc_update_rssi
call  (indirect)
rd32  0x3FF73C00
call  memcmp
call  memcmp
call  ieee80211_gettid
call  memcmp
call  memcpy
call  .text.ieee80211_hdrsize
call  ieee80211_send_mgmt
call  ieee80211_node_pwrsave
call  ieee80211_ampdu_reorder
call  wifi_log
call  ieee80211_crypto_decap
call  ieee80211_decap
call  (indirect)
call  ieee80211_decap_amsdu
call  hostap_deliver_data
call  .text.ieee80211_hdrsize
call  ieee80211_crypto_decap
call  hostap_recv_mgmt
call  hostap_recv_ctl
call  ic_ebuf_recycle_rx
call  memcpy
```

### `ieee80211_cal_tx_pps`  <sub>ieee80211_ht.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
```

### `ieee80211_ampdu_enable`  <sub>ieee80211_ht.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
```

### `ieee80211_ampdu_request`  <sub>ieee80211_ht.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
call  g_osi_funcs_p
call  wifi_log
call  wifi_log
call  (indirect)
call  ieee80211_send_action
call  wifi_log
call  (indirect)
call  (indirect)
call  (indirect)
```

### `.text.ieee80211_ampdu_age_bss`  <sub>ieee80211_ht.o</sub>

```
call  wifi_log
call  (indirect)
rd32  0x3FF73C00
call  ampdu_dispatch_upto
```

### `wifi_reset_mac`  <sub>ieee80211_ioctl.o</sub>

```
call  (indirect)
call  (indirect)
call  coex_bt_high_prio
call  g_osi_funcs_p
rd32  0x3FF73084
wr32  0x3FF73084
```

### `ieee80211_output_raw_process`  <sub>ieee80211_output.o</sub>

```
call  ic_ebuf_recycle_tx
call  wifi_log
call  (indirect)
rd32  0x3FF73C00
call  wifi_get_macaddr
call  memcmp
call  memcmp
call  cnx_node_search
call  ppTxPktForceWaked
call  wifi_log
```

### `ieee80211_set_tx_desc`  <sub>ieee80211_output.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  ic_get_trc
```

### `pwrsave_flushq`  <sub>ieee80211_power.o</sub>

```
call  chm_is_at_home_channel
call  ic_tx_pkt
call  (indirect)
rd32  0x3FF73C00
call  ic_ebuf_recycle_tx
```

### `ieee80211_pwrsave_txcb`  <sub>ieee80211_power.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  ieee80211_set_tim
```

### `.text.scan_done`  <sub>ieee80211_scan.o</sub>

```
call  wifi_log
call  chm_return_home_channel
call  scan_flush_all_tx_buf
call  chm_release_lock
call  (indirect)
call  (indirect)
call  wifi_log
call  wifi_set_rx_policy
call  scan_build_chan_list
call  (indirect)
call  (indirect)
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
call  wifi_log
call  wifi_log
call  (indirect)
call  clear_bss_queue
call  wifi_station_get_reconnect_policy
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  wifi_log
call  wifi_event_post
call  chm_get_current_channel
call  net80211_funcs
call  pm_on_channel
```

### `scan_enter_oper_channel_process`  <sub>ieee80211_scan.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
call  .text.scan_next_channel
```

### `scan_start`  <sub>ieee80211_scan.o</sub>

```
call  connect_scan_flag
call  connect_scan_flag
call  connect_scan_flag
call  scan_cancel
call  chm_acquire_lock
call  wifi_log
call  fpm_allow_tx
call  fpm_do_wakeup
call  pm_off_channel
call  wifi_log
call  wifi_set_rx_policy
call  clear_bss_queue
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
call  scan_inter_channel_timeout_process
call  wifi_log
```

### `sta_recv_assoc`  <sub>ieee80211_sta.o</sub>

```
call  wifi_log
call  ieee80211_find_ie
call  wifi_log
call  wifi_log
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  wifi_log
call  ieee80211_sta_new_state
call  wifi_station_get_reconnect_policy
call  (indirect)
call  (indirect)
call  (indirect)
call  g_osi_funcs_p
call  ieee80211_setup_phy_mode
call  (indirect)
call  (indirect)
call  (indirect)
call  esp_mesh_quick_funcs
call  (indirect)
call  (indirect)
call  ieee80211_setup_rates
call  (indirect)
rd32  0x3FF73C00
call  ieee80211_parse_wmeparams
call  ieee80211_wme_updateparams
call  ieee80211_ht_node_init
call  ieee80211_ht_updateparams
call  ieee80211_setup_htrates
call  ieee80211_setup_basic_htrates
call  (indirect)
call  (indirect)
call  esp_mesh_quick_funcs
call  wifi_log
call  ieee80211_parse_obss_scan_param
call  ieee80211_set_shortslottime
call  wifi_log
call  wifi_set_rx_policy
call  ieee80211_sta_new_state
call  ieee80211_setup_lr_rates
call  ieee80211_is_lr_only
```

### `.text.cnx_connect_to_bss`  <sub>wl_cnx.o</sub>

```
call  chm_get_current_channel
call  wifi_log
call  wifi_log
call  ic_set_bssid
call  wifi_log
call  wifi_set_rx_policy
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_mesh_event_post
call  (indirect)
call  (indirect)
call  (indirect)
call  (indirect)
call  g_ic
call  wifi_log
call  wifi_log
call  wifi_log
call  wifi_log
call  ieee80211_update_channel
call  (indirect)
call  (indirect)
rd32  0x3FF73C00
call  chm_acquire_lock
call  wifi_log
call  chm_start_op
call  .text.cnx_connect_op
```

### `cnx_handshake_timeout_process`  <sub>wl_cnx.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
call  (indirect)
call  ieee80211_sta_new_state
call  wifi_station_get_reconnect_policy
call  cnx_connect_timeout_process
```

### `cnx_bss_alloc`  <sub>wl_cnx.o</sub>

```
call  (indirect)
rd32  0x3FF73C00
call  wifi_log
call  cnx_check_bssid_in_blacklist
call  wifi_log
call  wifi_sta_get_prof_password
call  wifi_log
call  wifi_log
call  (indirect)
rd32  0x3FF73C00
call  .text.cnx_cal_rc_util
call  memcmp
call  cnx_remove_rc
```

### `cnx_update_bss_more`  <sub>wl_cnx.o</sub>

```
call  esp_mesh_quick_funcs
call  memcpy
call  memset
call  memcpy
call  memset
call  memcpy
call  memset
call  memcpy
call  ieee80211_sta_is_connected
call  (indirect)
call  (indirect)
call  cnx_sta_pm
call  ieee80211_set_shortslottime
call  ieee80211_parse_wmeparams
call  ieee80211_wme_updateparams
call  memset
call  (indirect)
rd32  0x3FF73C00
call  ieee80211_setup_rates
call  ieee80211_is_ht_cipher
call  ieee80211_setup_phy_mode
call  ieee80211_ht_updateparams
call  ieee80211_update_channel
call  ieee80211_regdomain_update_in_connect
call  ieee80211_set_max_rate
call  ieee80211_parse_obss_scan_param
```

## `libcoexist.a` - WiFi / Bluetooth radio arbitration

4 functions touch a peripheral register.

### `.iram1.0`  <sub>coexist_hw.o</sub>

```
rd32  0x3FF40078
```

### `.iram1.12`  <sub>coexist_hw.o</sub>

```
call  (indirect)
wr32  0x3FF71250
rd32  0x3FF71254
call  .iram1.0
wr32  0x3FF000CC
call  .iram1.0
wr32  0x3FF00094
call  .iram1.0
wr32  0x3FF00094
rd32  0x3FF510A0
wr32  0x3FF510A0
call  .iram1.0
wr32  0x3FF000CC
call  g_coa_funcs_p
```

### `coex_force_wifi_mode`  <sub>coexist_hw.o</sub>

```
call  coex_time_now
call  (indirect)
wr32  0x3FF71250
rd32  0x3FF71254
rd32  0x3FF71264
call  (indirect)
call  (indirect)
wr32  0x3FF71250
rd32  0x3FF71254
rd32  0x3FF71300
wr32  0x3FF71300
rd32  0x3FF73D40
wr32  0x3FF73D40
rd32  0x3FF441C8
wr32  0x3FF441C8
call  (indirect)
call  coex_time_now
rd32  0x3FF51110
call  .iram1.12
call  (indirect)
call  force_wifi_mode
call  (indirect)
call  (indirect)
rd32  0x3FF71300
wr32  0x3FF71300
rd32  0x3FF73D40
wr32  0x3FF73D40
rd32  0x3FF441C8
wr32  0x3FF441C8
call  (indirect)
rd32  0x3FF5110C
call  coex_time_now
call  coex_time_now
rd32  0x3FF51110
call  coex_time_now
call  force_wifi_mode
call  (indirect)
rd32  0x3FF51110
call  (indirect)
rd32  0x3FF71300
wr32  0x3FF71300
rd32  0x3FF73D40
wr32  0x3FF73D40
rd32  0x3FF441C8
wr32  0x3FF441C8
rd32  0x3FF51110
call  (indirect)
call  coex_time_now
call  coex_time_now
rd32  0x3FF51110
rd32  0x3FF5110C
call  coex_time_now
call  coex_time_now
rd32  0x3FF51110
call  .iram1.12
```

### `coex_unforce_wifi_mode`  <sub>coexist_hw.o</sub>

```
call  btdm_rf_bb_reg_init
rd32  0x3FF5110C
rd32  0x3FF51118
call  (indirect)
call  unforce_wifi_mode
call  g_coa_funcs_p
rd32  0x3FF5110C
```

## Registers by how many functions reach them

```
0x3FF4E0C4  66
0x3FF4847C  64
0x3FF73C00  45
0x3FF4848C  44
0x3FF48000  38
0x3FF48484  33
0x3FF48488  33
0x3FF4882C  33
0x3FF48480  32
0x3FF730E4  32
0x3FF48854  29
0x3FF73084  27
0x3FF460A0  24
0x3FF4884C  24
0x3FF48084  22
0x3FF4889C  22
0x3FF4E050  22
0x3FF51040  22
0x3FF5C080  22
0x3FF75010  22
0x3FF48074  20
0x3FF48088  20
0x3FF48494  20
0x3FF48810  20
0x3FF48858  20
0x3FF48894  20
0x3FF48498  18
0x3FF4849C  18
0x3FF484A0  18
0x3FF484A4  18
0x3FF484A8  18
0x3FF484AC  18
0x3FF484B0  18
0x3FF48078  16
0x60033CB8  15
0x3FF450DC  14
0x3FF5C018  14
0x3FF5C02C  14
0x60033D30  14
0x3FF00024  13
0x3FF48890  13
0x3FF4E05C  13
0x3FF5D040  13
0x3FF73CB8  13
0x3FF48478  12
0x3FF5103C  11
0x3FF48030  10
0x3FF5C860  10
0x3FF7309C  10
0x3FF730F8  10
0x3FF730FC  10
0x3FF75020  10
0x600310D0  10
0x60031300  10
0x60033CF0  10
0x3FF000CC  9
0x3FF4880C  9
0x3FF5C01C  9
0x3FF5C044  9
0x3FF5C07C  9
0x3FF73C40  9
0x6001C860  9
0x60033C6C  9
0x60033D38  9
0x3FF000D0  8
0x3FF4807C  8
0x3FF48080  8
0x3FF48490  8
0x3FF48800  8
0x3FF48808  8
0x3FF48898  8
0x3FF5C084  8
0x3FF5C87C  8
0x3FF5CC0C  8
0x3FF73148  8
0x3FF7505C  8
0x600041C4  8
0x60033C00  8
0x600340F0  8
0x3FF4609C  7
0x3FF460B8  7
0x3FF48838  7
0x3FF48884  7
0x3FF5C030  7
0x3FF5C0CC  7
0x3FF5C124  7
0x3FF73814  7
0x3FF441C8  6
0x3FF45108  6
0x3FF4510C  6
0x3FF4607C  6
0x3FF4801C  6
0x3FF4840C  6
0x3FF51000  6
0x3FF51020  6
0x3FF51044  6
0x3FF5110C  6
0x3FF51110  6
0x3FF5C11C  6
0x3FF5CD0C  6
0x3FF71300  6
0x3FF73250  6
0x3FF73C04  6
0x3FF73C1C  6
0x3FF73D24  6
0x3FF73D40  6
0x3FF45034  5
0x3FF45104  5
0x3FF48070  5
0x3FF5C400  5
0x3FF5D000  5
0x3FF73C4C  5
0x6001C87C  5
0x3FF46094  4
0x3FF460C0  4
0x3FF48018  4
0x3FF48060  4
0x3FF48428  4
0x3FF484BC  4
0x3FF48830  4
0x3FF48834  4
0x3FF4E148  4
0x3FF4E14C  4
0x3FF5105C  4
0x3FF51098  4
0x3FF5C010  4
0x3FF5C014  4
0x3FF5C090  4
0x3FF73080  4
0x3FF730D8  4
0x3FF730F4  4
0x3FF73104  4
0x3FF73114  4
0x3FF73284  4
0x3FF73288  4
0x3FF73C54  4
0x3FF73C5C  4
0x3FF73C6C  4
0x3FF73D7C  4
0x600320F0  4
0x600320F4  4
0x60032130  4
0x60033C4C  4
0x60033CEC  4
0x60033D40  4
0x3FF40078  3
0x3FF45038  3
0x3FF450A8  3
0x3FF45110  3
0x3FF46000  3
0x3FF4E04C  3
0x3FF4E060  3
0x3FF4E124  3
0x3FF4E164  3
0x3FF51014  3
0x3FF5C038  3
0x3FF5C074  3
0x3FF5C094  3
0x3FF5C0A4  3
0x3FF5C0D0  3
0x3FF5C104  3
0x3FF5C108  3
0x3FF5CD04  3
0x3FF5CD08  3
0x3FF66000  3
0x3FF66010  3
0x3FF71250  3
0x3FF71254  3
0x3FF73088  3
0x3FF7325C  3
0x3FF73418  3
0x3FF7341C  3
0x3FF73810  3
0x3FF75060  3
0x3FF75064  3
0x60033D24  3
0x3FF00094  2
0x3FF4504C  2
0x3FF450F0  2
0x3FF46004  2
0x3FF46008  2
0x3FF48038  2
0x3FF48048  2
0x3FF4808C  2
0x3FF480B4  2
0x3FF480C0  2
0x3FF4883C  2
0x3FF4888C  2
0x3FF488A0  2
0x3FF4E044  2
0x3FF4E054  2
0x3FF4E118  2
0x3FF4E130  2
0x3FF4E150  2
0x3FF5104C  2
0x3FF51058  2
0x3FF5106C  2
0x3FF51084  2
0x3FF510A0  2
0x3FF5C0A0  2
0x3FF5C120  2
0x3FF5C450  2
0x3FF5C804  2
0x3FF5CC48  2
0x3FF5D030  2
0x3FF5D03C  2
0x3FF5D044  2
0x3FF5D050  2
0x3FF73090  2
0x3FF730A4  2
0x3FF7310C  2
0x3FF7314C  2
0x3FF73158  2
0x3FF73164  2
0x3FF732F0  2
0x3FF732F4  2
0x3FF732F8  2
0x3FF73804  2
0x3FF73C20  2
0x3FF73C24  2
0x3FF73C58  2
0x3FF73C74  2
0x3FF73C78  2
0x3FF73C88  2
0x3FF73CAC  2
0x3FF73CBC  2
0x3FF73CC0  2
0x3FF73CC4  2
0x3FF73D78  2
0x3FF73DA4  2
0x3FF75054  2
0x6000604C  2
0x600320DC  2
0x60032124  2
0x600330D8  2
0x600330DC  2
0x600330E0  2
0x600330E4  2
0x60033C40  2
0x60033C78  2
0x60033CC4  2
0x600340FC  2
0x3FF000C0  1
0x3FF000DC  1
0x3FF000E4  1
0x3FF4503C  1
0x3FF45040  1
0x3FF45044  1
0x3FF45048  1
0x3FF45050  1
0x3FF4506C  1
0x3FF45070  1
0x3FF45074  1
0x3FF45078  1
0x3FF450C4  1
0x3FF450C8  1
0x3FF450D0  1
0x3FF450D8  1
0x3FF45114  1
0x3FF45194  1
0x3FF45198  1
0x3FF4519C  1
0x3FF451AC  1
0x3FF4600C  1
0x3FF46010  1
0x3FF46030  1
0x3FF46048  1
0x3FF46060  1
0x3FF460F8  1
0x3FF46164  1
0x3FF46170  1
0x3FF48040  1
0x3FF48064  1
0x3FF48818  1
0x3FF48844  1
0x3FF4E000  1
0x3FF4E080  1
0x3FF4E0C8  1
0x3FF4E0CC  1
0x3FF4E0D0  1
0x3FF4E0D4  1
0x3FF4E0D8  1
0x3FF4E0E0  1
0x3FF4E100  1
0x3FF4E104  1
0x3FF4E108  1
0x3FF4E110  1
0x3FF4E11C  1
0x3FF4E120  1
0x3FF4E128  1
0x3FF4E12C  1
0x3FF4E134  1
0x3FF4E140  1
0x3FF51028  1
0x3FF51118  1
0x3FF5C004  1
0x3FF5C024  1
0x3FF5C028  1
0x3FF5C088  1
0x3FF5C0B8  1
0x3FF5C0C4  1
0x3FF5C0F0  1
0x3FF5C0F4  1
0x3FF5C0F8  1
0x3FF5C4A8  1
0x3FF5C85C  1
0x3FF5CC04  1
0x3FF5CC08  1
0x3FF5CCB8  1
0x3FF5CCD8  1
0x3FF5CCDC  1
0x3FF5CCE4  1
0x3FF5D014  1
0x3FF5D018  1
0x3FF5D01C  1
0x3FF5D020  1
0x3FF5D04C  1
0x3FF71264  1
0x3FF7308C  1
0x3FF73094  1
0x3FF73118  1
0x3FF7311C  1
0x3FF73120  1
0x3FF73124  1
0x3FF73260  1
0x3FF73264  1
0x3FF73268  1
0x3FF7326C  1
0x3FF7328C  1
0x3FF73290  1
0x3FF73294  1
0x3FF73298  1
0x3FF7329C  1
0x3FF732A0  1
0x3FF732A4  1
0x3FF732A8  1
0x3FF732AC  1
0x3FF732B0  1
0x3FF732B4  1
0x3FF732B8  1
0x3FF732BC  1
0x3FF732C0  1
0x3FF732C4  1
0x3FF732C8  1
0x3FF732CC  1
0x3FF732D0  1
0x3FF732D8  1
0x3FF732DC  1
0x3FF732E0  1
0x3FF732FC  1
0x3FF73400  1
0x3FF73404  1
0x3FF73408  1
0x3FF7340C  1
0x3FF73410  1
0x3FF73414  1
0x3FF73424  1
0x3FF73428  1
0x3FF7342C  1
0x3FF73430  1
0x3FF73800  1
0x3FF73808  1
0x3FF7380C  1
0x3FF73C30  1
0x3FF73C44  1
0x3FF73C48  1
0x3FF73CC8  1
0x3FF73D58  1
0x3FF73D5C  1
0x3FF73D60  1
0x3FF73D64  1
0x3FF73D68  1
0x3FF73D6C  1
0x3FF73D70  1
0x3FF73D84  1
0x3FF73D88  1
0x3FF73DAC  1
0x3FF73DB0  1
0x3FF73DB4  1
0x3FF73DB8  1
0x3FF73DBC  1
0x3FF7442C  1
0x3FF75014  1
0x3FF75018  1
0x3FF75024  1
0x3FF75028  1
0x3FF75058  1
0x3FF75090  1
0x3FF75094  1
0x3FF75144  1
0x6001C06C  1
0x60033040  1
0x60033044  1
0x60033C48  1
0x60033CC8  1
0x600340F8  1
0x60034108  1
```

