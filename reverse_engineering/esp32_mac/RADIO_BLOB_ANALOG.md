# Radio blob analog RF sequences

The ESP32's RF synthesizer, PLL, crystal and bias blocks are not memory mapped. They sit
behind an internal serial bus reached through two ROM routines, which `libphy` calls
indirectly through the `g_phyFuns` function table. Every argument at those call sites is an
immediate, so the full programming sequence is recoverable from the instruction stream.

Read out with `xtensa-esp32-elf-objdump -dr`; nothing here is decompiled. Regenerate with
`python tools/dev_env/blob_analog.py .`.

A slot is given by its byte offset into the table. The table is filled at runtime from ROM
addresses, so no header names the slots: the offset and the argument count identify one.
The four-argument shape matches the ROM's `(block, host_id, reg_add, data)`.

Arguments are shown through the last one the call site sets. Slot 160 sets four,
matching `(block, host_id, reg_add, data)`; slot 168 sets six, matching the same with
`(msb, lsb)` ahead of the data, which is a read-modify-write of a bit field.

`?` is an argument this could not follow to a constant, which means it is computed.

## The primitive

`ram_chip_i2c_writeReg` and `ram_chip_i2c_readReg` are not in ROM: `libphy.a` defines them
in its own `.iram1`, so the hardware sequence is readable. They appear as undefined only
because sibling objects in the archive reference them.

Reading `ram_chip_i2c_writeReg(block, host_id, reg_add, data)` out of the disassembly:

- each of the four arguments is masked to 8 bits (`extui aN, aN, 0, 8`),
- the body runs inside `phy_enter_critical` / `phy_exit_critical`,
- a `host_id` below 2 takes a path that calls `phy_dis_hw_set_freq` first, then clears
  bit 8 of `0x3FF4E0C4` through `esp_dport_access_reg_read` and writes it back,
- the per-transfer register address is `(0x0FFD3800 + host_id) << 2`, which is
  `0x3FF4E000 + host_id * 4`. The base is pre-divided by four so the shift lands it.

So the analog bus controller is at `0x3FF4E000` indexed by `host_id * 4`, with a control
register at `+0xC4`. That base is not one of the documented ESP32 peripherals. `memw`
separates the read from the write-back, and the DPORT read path is mandatory there.

The `g_phyFuns` slots below are still numbered rather than named: `esp32.rom.ld` places the
table at `g_phyFuns_instance = 0x3ffae0c4` in DRAM and it is filled at runtime by
`phy_get_romfuncs` (`0x40004100`), so no static artifact carries the mapping. Reading it
out of a live coredump and matching each pointer against the 1 601 `PROVIDE` addresses in
`esp32.rom.ld` is what names them.

## `libphy.a`

95 functions call through a table.

### `wr_bt_tx_gain_mem` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+52   (0x01)
```

### `set_tx_gain_table` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+0    ()
g_phyFuns+52   (0x04 ? 0x6E)
```

### `phy_close_rf` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+192  ()
g_phyFuns+184  (0x01 0x01 0x00)
g_phyFuns+196  ()
```

### `ram_start_tx_tone` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+68   ()
```

### `bt_txdc_cal$part$5` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+192  ()
g_phyFuns+216  ()
g_phyFuns+188  (? 0x01)
g_phyFuns+184  (? 0x01 0x02)
g_phyFuns+184  (0x01 0x02 0x00)
g_phyFuns+0    ()
g_phyFuns+208  (0x00)
g_phyFuns+196  ()
```

### `ram_spur_coef_cfg` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+96   (? ? ? 0x00)
g_phyFuns+96   (0x02 0x51EB851F ? 0x01)
g_phyFuns+96   (0x03 0x51EB851F)
```

### `bb_bss_cbw40` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+112  ()
g_phyFuns+112  (0x01 0x01)
g_phyFuns+116  (0xFFFFFFF3)
g_phyFuns+116  (0x00)
```

### `phy_reg_init` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+0    ()
g_phyFuns+120  ()
```

### `set_chan_reg` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+132  ()
g_phyFuns+0    ()
```

### `set_rx_gain_cal_iq` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+40   (0x01)
g_phyFuns+168  (0x64 0x00 0x04 0x07 0x07 0x01)
g_phyFuns+0    (? 0x01)
g_phyFuns+0    (0x0F 0x00)
g_phyFuns+32   (? 0x104 0x100)
g_phyFuns+188  (0x01 0x01)
g_phyFuns+184  (0x01 0x01)
g_phyFuns+0    (0xFA0 ? 0x0A 0x00 0x00)
g_phyFuns+184  (0x01 0x01 0x1F1)
g_phyFuns+184  (0x01 0x01 0x1F9)
g_phyFuns+68   (0x01 ? ? ? 0x00 0x00)
g_phyFuns+0    (0x01 0x3FF)
g_phyFuns+0    ()
g_phyFuns+76   (0x01)
g_phyFuns+184  (0x05 0x01)
g_phyFuns+0    (? 0x78)
g_phyFuns+40   (0x00)
g_phyFuns+168  (0x64 0x00 ? 0x07 0x07 0x00)
g_phyFuns+184  (0x05 0x01)
```

### `rx_chan_dc_sort` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+0    (? 0x00)
g_phyFuns+0    ()
```

### `set_rx_gain_cal_dc` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+64   (0x01)
g_phyFuns+60   (0x01)
g_phyFuns+184  (0x02)
g_phyFuns+184  (0x03)
g_phyFuns+160  (0x64 0x00 0x04)
g_phyFuns+160  (0x64 0x00 0x07)
g_phyFuns+184  (0x00 0x01 0x184)
g_phyFuns+200  ()
g_phyFuns+184  (0x00 0x01 0x184)
g_phyFuns+200  ()
g_phyFuns+184  ()
g_phyFuns+184  (0x03)
g_phyFuns+184  ()
g_phyFuns+200  ()
g_phyFuns+64   (0x00)
g_phyFuns+60   (0x00)
g_phyFuns+184  (0x02 0x02 0x100)
g_phyFuns+184  (0x03 0x02 0x100)
g_phyFuns+184  (0x02 0x02)
g_phyFuns+184  (0x03 0x02)
g_phyFuns+184  ()
g_phyFuns+184  (0x03)
```

### `wr_rx_gain_mem` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+52   (? ? ? ? 0x00 0x80)
```

### `set_rx_gain_testchip_70` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+164  (0x67 0x01 0x0F 0x02 0x00)
g_phyFuns+168  (0x67 0x01 0x0F 0x02 0x00 0x00)
g_phyFuns+192  ()
g_phyFuns+208  (0x00)
g_phyFuns+188  (0x01 0x01)
g_phyFuns+184  (0x01 0x01)
g_phyFuns+208  (0x00)
g_phyFuns+188  (0x01 0x01)
g_phyFuns+184  (0x01 0x01)
g_phyFuns+208  (0x00)
g_phyFuns+196  ()
g_phyFuns+168  (0x67 0x01 0x0F 0x02 0x00)
g_phyFuns+192  ()
g_phyFuns+208  ()
g_phyFuns+208  (0x00)
g_phyFuns+208  (0x00)
g_phyFuns+196  ()
```

### `phy_bttx_low_power` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+160  (0x6B 0x02 0x01 0x60)
g_phyFuns+160  (0x6B 0x02 0x02 0x00)
g_phyFuns+0    (? 0x00)
g_phyFuns+0    ()
```

### `set_chanfreq` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+108  (? 0x80)
```

### `set_chanfreq_nomac` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+108  (? 0x80)
```

### `chip_sleep_prot_en` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+8    ()
```

### `chip_sleep_prot_dis` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+12   ()
```

### `noise_check_loop` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+132  (0xFFFFFEA0)
```

### `noise_init` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+128  ()
g_phyFuns+132  ()
```

### `phy_set_rfrx_dcap` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+164  (0x64 0x00 0x07 0x03 0x00)
g_phyFuns+164  (0x64 0x00 0x04 0x03 0x00)
g_phyFuns+168  (0x64 0x00 0x07 0x03 0x00 0x00)
g_phyFuns+168  (0x64 0x00 0x04 0x03 0x00 0x00)
g_phyFuns+168  (0x64 0x01 0x07 0x03 0x01)
g_phyFuns+168  (0x64 0x01 0x04 0x03 0x01)
```

### `chip_v7_set_chan_misc` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+132  ()
g_phyFuns+0    ()
```

### `set_rx_gain_table` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+252  ()
```

### `analog_gain_init` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+192  ()
g_phyFuns+184  (0x04 0x01 0x00)
g_phyFuns+184  (0x05 0x01 0x00)
g_phyFuns+184  (0x00 0x01 0x184)
g_phyFuns+184  (0x01 0x01 0x18B)
g_phyFuns+184  (0x01 0x02 0x00)
g_phyFuns+220  ()
g_phyFuns+196  ()
g_phyFuns+220  (0x01 0x01 0x189)
g_phyFuns+184  (0x01 0x02 0x00)
g_phyFuns+220  ()
```

### `bb_init` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+48   ()
g_phyFuns+0    ()
```

### `reg_init_begin` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+160  (0x66 0x04 0x0C 0x00)
```

### `phy_wakeup_init` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+152  (0x63 0x00 0x00)
```

### `tx_cont_en` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+168  (0x67 0x01 0x03 0x06)
g_phyFuns+168  (0x67 0x01 0x04 0x06)
g_phyFuns+168  (0x67 0x01 0x03 0x06 0x00)
g_phyFuns+168  (0x67 0x01 0x04 0x06 0x00)
```

### `tx_cont_dis` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+168  (0x67 0x01 0x03 0x06 0x00)
g_phyFuns+168  (0x67 0x01 0x04 0x06 0x00)
```

### `phy_get_tx_pwr` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+0    ()
```

### `register_chipv7_phy` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+160  (0x65 0x04 0x00 0x63)
g_phyFuns+152  (0x63 0x00 0x00)
```

### `chan14_mic_cfg` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+168  (0x67 ? 0x03 0x06 0x00 0x4C)
g_phyFuns+168  (0x67 ? 0x04 0x06 0x00 0x4E)
g_phyFuns+168  (0x67 0x01 0x03 0x06 0x00 0x02)
g_phyFuns+168  (0x67 0x01 0x04 0x06 0x00)
```

### `phy_get_adc_rand` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+64   (0x01)
g_phyFuns+168  (0x67 0x01 0x01 0x07 0x07 0x00)
g_phyFuns+168  (0x67 0x01 0x02 0x07 0x07 0x00)
g_phyFuns+192  ()
g_phyFuns+220  ()
g_phyFuns+208  (0x00)
g_phyFuns+184  (0x00 0x01 0x00)
g_phyFuns+0    (0xFA0 ? 0x0A 0x00 0x00)
g_phyFuns+204  ()
g_phyFuns+196  ()
g_phyFuns+168  (0x67 0x01 0x01 0x07 0x07 0x01)
g_phyFuns+168  (0x67 0x01 0x02 0x07 0x07 0x01)
g_phyFuns+64   ()
```

### `freq_offset_get_pwr_1` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+60   (0x01)
g_phyFuns+0    (0x01)
g_phyFuns+0    (0x00 ? ? 0x00)
g_phyFuns+60   (0x00)
```

### `rx_spur_cal` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+168  (0x66 0x04 0x05 0x02 0x00 0x05)
g_phyFuns+168  (0x68)
g_phyFuns+168  (0x66 0x04 0x05 0x02 0x00)
g_phyFuns+168  (0x68 0x03 ? 0x05 0x02)
```

### `bt_rx_spur_opt` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+152  (0x68 0x03 0x00)
g_phyFuns+152  (0x66 0x04 0x05)
g_phyFuns+152  (0x68 0x03 0x00)
g_phyFuns+168  (0x66 0x04 0x05 0x07 0x06 0x00)
g_phyFuns+152  (0x68 0x03 0x00)
g_phyFuns+152  (0x66 0x04 0x05)
g_phyFuns+160  (0x68 0x03 0x00)
g_phyFuns+160  (0x66 0x04 0x05)
```

### `pbus_print` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+188  (0x00 0x01)
g_phyFuns+188  (0x04 0x01)
g_phyFuns+188  (0x05 0x01)
g_phyFuns+188  (0x01 0x01)
g_phyFuns+188  (0x01 0x02)
g_phyFuns+188  (0x02 0x01)
g_phyFuns+188  (0x03 0x01)
g_phyFuns+188  (0x02 0x02)
g_phyFuns+188  (0x03 0x02)
```

### `phy_i2c_check` <sub>phy_chip_v7.o</sub>

```
g_phyFuns+152  (0x6A 0x02 0x00)
g_phyFuns+152  (0x67 0x01)
g_phyFuns+152  (0x66 0x04 0x00)
g_phyFuns+152  (0x6B 0x02 0x01)
g_phyFuns+152  (0x64 0x00 0x00)
g_phyFuns+152  (0x62 0x01 0x00)
g_phyFuns+152  (0x63 0x00 0x00)
g_phyFuns+152  (0x65 0x04 0x00)
g_phyFuns+152  (0x68 0x03 0x00)
g_phyFuns+152  (0x6D 0x03 0x00)
```

### `i2c_bbpll_init$part$0` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+168  (0x66 0x04 0x02 0x07 0x07 0x01)
g_phyFuns+168  (0x66 0x04 0x05 0x02 0x00 0x00)
g_phyFuns+168  (0x66 0x04 0x05 0x07 0x06 0x01)
```

### `pll_correct_dcap` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+164  (0x62 0x01 0x00 0x07 0x07)
g_phyFuns+164  (0x62 0x01 0x05 0x07 0x00)
```

### `wifi_track_pll_cap` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x62 0x01 0x01)
```

### `ram_pbus_debugmode` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+172  (0x01)
```

### `bb_bss_cbw40_ana` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x67 0x01 0x03 0x02 0x02 0x0A)
g_phyFuns+160  (0x67 0x01 0x04)
g_phyFuns+160  (0x67 0x01 ? 0x10)
g_phyFuns+160  (0x67 0x01 0x01)
g_phyFuns+160  (0x67 0x01 0x01)
g_phyFuns+160  (0x67 0x01)
g_phyFuns+160  (0x67 0x01 0x09 0x71)
```

### `i2c_bt_filter_set` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x67 0x01 0x0B 0x61)
g_phyFuns+160  (0x67 0x01 0x0C 0x10)
g_phyFuns+160  (0x67 0x01 0x0F 0xEA)
g_phyFuns+160  (0x67 0x01 0x07)
g_phyFuns+160  (0x67 0x01 0x08)
g_phyFuns+160  (0x67 0x01 0x05)
g_phyFuns+160  (0x67 0x01 0x06)
```

### `phy_i2c_init` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+152  (0x66 0x04 0x0A)
g_phyFuns+152  (0x66 0x04 0x0B)
g_phyFuns+152  (0x66 0x04 0x09)
```

### `ram_pbus_xpd_tx_on` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+184  (0x00 0x01 0x01)
g_phyFuns+184  (0x01 0x01 0x7C)
g_phyFuns+220  ()
g_phyFuns+184  (0x01 0x02)
g_phyFuns+188  (0x01 0x01)
g_phyFuns+184  (0x01 0x01 0x02)
g_phyFuns+184  (0x04 0x01 0x7F)
g_phyFuns+184  (0x05 0x01)
```

### `i2c_xtal_init` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x68 0x03 0x00 0x63)
g_phyFuns+160  (0x68 0x03 0x01 0x80)
```

### `i2c_rfpll_init` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x63 0x00 0x01 0xF3)
g_phyFuns+168  (0x62 0x01 0x03 0x02 0x01 0x03)
g_phyFuns+160  (0x62 0x01 0x08 0x00)
g_phyFuns+160  (0x62 0x01 0x0A 0xB0)
g_phyFuns+160  (0x62 0x01 0x09 0x07)
g_phyFuns+160  (0x62 0x01 0x00 0x3F)
g_phyFuns+160  (0x62 0x01 0x04 0xBF)
```

### `ram_restart_cal` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+168  (0x62 0x01 0x00 0x06 0x06 0x01)
g_phyFuns+168  (0x62 0x01 0x00 0x05 0x05 0x00)
g_phyFuns+168  (0x62 0x01 0x00 0x05 0x05 0x01)
g_phyFuns+168  (0x62 0x01 0x00 0x06 0x06 0x00)
```

### `ram_wait_rfpll_cal_end` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+164  (0x62 0x01 0x07 0x07 0x07)
```

### `get_lna_vga_dcap_val` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+44   (? 0x0F 0x00)
g_phyFuns+44   (? 0x0F 0x00)
```

### `chip_v7_rxmax_ext_ana` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+164  (0x62 0x01 0x05 0x07 0x00)
g_phyFuns+160  (0x64 0x00 0x04 0x40)
g_phyFuns+160  (0x64 0x00 0x04)
g_phyFuns+160  (0x64 0x00 0x07 0x40)
```

### `phy_freq_correct_opt` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+248  (0x01)
```

### `chip_v7_adc_wr_dly` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x66 0x04 0x08)
```

### `i2c_bbtop_init` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x66 0x04 0x08 0x21)
g_phyFuns+160  (0x66 0x04 0x09 0x84)
g_phyFuns+160  (0x67 0x01 0x09 0x71)
g_phyFuns+160  (0x67 0x01 0x0B 0x61)
```

### `i2c_rftx_init` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x6B 0x02 0x03 0x88)
g_phyFuns+160  (0x6B 0x02 0x04 0x06)
g_phyFuns+160  (0x6B 0x02 0x05 0x08)
g_phyFuns+160  (0x6B 0x02 0x06 0xF8)
g_phyFuns+160  (0x6B 0x02 0x07 0x5D)
g_phyFuns+160  (0x6B 0x02 0x0A 0x74)
```

### `i2c_bias_init` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+160  (0x6A 0x02 0x02 0x68)
g_phyFuns+160  (0x6A 0x02 0x00)
```

### `rfpll_1p2_opt` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+152  (0x62 0x01 0x00)
g_phyFuns+160  (0x6A 0x02 0x00 0x2A)
```

### `get_rf_freq_cap` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+224  ()
g_phyFuns+240  ()
g_phyFuns+232  ()
g_phyFuns+228  ()
g_phyFuns+236  ()
g_phyFuns+164  (0x62 0x01 0x05 0x07 0x00)
g_phyFuns+152  (0x62 0x01 0x02)
g_phyFuns+164  (0x62 0x01 0x06 0x03 0x00)
```

### `get_rfrx_dcap_bt` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+152  (0x64 0x00)
g_phyFuns+44   (? 0x0F 0x01)
```

### `get_rf_freq_init$part$2` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+168  (0x62 0x01 0x00 0x07 0x07 0x00)
g_phyFuns+168  (0x62 0x01 0x02 0x07 0x07 0x00)
g_phyFuns+152  (0x68 0x03 0x00)
g_phyFuns+152  (0x66 0x04 0x05)
g_phyFuns+160  (0x62 0x01 0x01 0x6E 0x00)
```

### `bt_get_i2c_data` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+168  (0x62 0x01 0x00 0x07 0x07 0x01)
```

### `rf_init` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+192  ()
g_phyFuns+220  ()
g_phyFuns+212  ()
g_phyFuns+204  ()
g_phyFuns+196  ()
```

### `check_rfpll_write_i2c` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+152  ()
```

### `set_channel_rfpll_freq` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+244  ()
```

### `chip_v7_set_chan_nomac` <sub>phy_chip_v7_ana.o</sub>

```
g_phyFuns+100  ()
```

### `get_phy_target_power` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
g_phyFuns+0    ()
```

### `tx_gain_table_set` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+44   (? 0x1E 0xFFFFFF81)
```

### `set_chan_dig_gain` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
```

### `tx_pwctrl_cal` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
```

### `ram_set_txcap_reg` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+160  (0x6B 0x02 0x01)
g_phyFuns+160  (0x6B 0x02 0x02)
```

### `ram_tx_pwctrl_bg_init` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
```

### `ram_txdc_cal_v70` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+184  (0x02 0x02 0x100)
g_phyFuns+184  (0x03 0x02 0x100)
g_phyFuns+184  (0x03)
g_phyFuns+184  (0x02)
g_phyFuns+184  (0x03)
g_phyFuns+184  (0x02)
```

### `txcal_debuge_mode` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+192  ()
g_phyFuns+216  ()
g_phyFuns+0    ()
g_phyFuns+220  ()
g_phyFuns+60   (0x01)
g_phyFuns+0    ()
```

### `ram_txcal_work_mode` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+76   (0x01)
g_phyFuns+60   (0x00)
g_phyFuns+208  (0x00)
g_phyFuns+196  ()
```

### `ram_get_fm_sar_dout` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
```

### `ram_txiq_get_mis_pwr` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
g_phyFuns+0    ()
```

### `ram_txiq_cover` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    (? 0x01)
g_phyFuns+0    (? 0x00)
g_phyFuns+0    (0x01)
g_phyFuns+0    (0x00)
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    (? 0x01)
g_phyFuns+0    (? 0x00)
```

### `rfcal_txiq` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+184  (? 0x02)
g_phyFuns+188  ()
g_phyFuns+184  (? ? 0x02)
g_phyFuns+40   ()
g_phyFuns+0    ()
g_phyFuns+220  ()
g_phyFuns+68   (0x01 ? ? 0x00 0x00 0x00)
g_phyFuns+0    (0x00)
g_phyFuns+0    ()
g_phyFuns+40   (0x00)
```

### `ram_dc_iq_est` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
g_phyFuns+0    (0x00 0x00)
g_phyFuns+0    ()
```

### `ram_pbus_rx_dco_cal` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+188  (0x01 0x02)
g_phyFuns+184  (0x02 0x02 0x100)
g_phyFuns+184  (0x03 0x02 0x100)
g_phyFuns+184  (0x02)
g_phyFuns+184  (0x03)
g_phyFuns+0    (0x01)
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+184  (0x02 0x02 0x100)
g_phyFuns+184  (0x03 0x02 0x100)
```

### `rxdc_est_min` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    (0x01)
```

### `pbus_rx_dco_cal_1step` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+188  (0x01 0x02)
g_phyFuns+188  (0x00 0x01)
g_phyFuns+184  (0x02)
g_phyFuns+184  (0x03)
g_phyFuns+184  (0x01 0x02 0x00)
g_phyFuns+184  (0x01 0x02 0x20)
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+44   (? 0x05 0xFFFFFFFB)
g_phyFuns+44   (? 0x05 0xFFFFFFFB)
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+184  (0x02 ? 0x1FF)
g_phyFuns+184  (0x03)
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+0    ()
```

### `rc_cal` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+168  (0x6A 0x02 0x06 0x04 ? 0x02)
g_phyFuns+168  (0x6A 0x02 ? 0x05 0x04 0x02)
g_phyFuns+168  (0x6A 0x02 0x04 0x07 0x04 0x0B)
g_phyFuns+168  (0x68 0x03 0x01 0x05 0x05 0x01)
g_phyFuns+168  (0x6A 0x02 0x04 0x00 0x00 0x01)
g_phyFuns+168  (0x6A 0x02 0x04 0x03 0x03 0x00)
g_phyFuns+168  (0x6A 0x02 0x04 0x03 0x03 0x01)
g_phyFuns+164  (0x6A 0x02 0x05 0x05 0x00)
g_phyFuns+168  (0x68 0x03 0x01 0x05 0x05 0x00)
g_phyFuns+168  (0x6A 0x02 0x04 0x00 0x00 0x00)
g_phyFuns+160  (0x67 0x01 0x07)
g_phyFuns+160  (0x67 0x01 0x08)
g_phyFuns+160  (0x67 0x01 0x05)
g_phyFuns+160  (0x67 0x01 0x06)
```

### `ram_rfcal_txcap` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+68   (0x01 ? ? 0x00 0x00 0x00)
g_phyFuns+164  (0x6B 0x02 0x01 0x03 0x00)
g_phyFuns+164  (0x6B ? 0x02 0x03 0x00)
g_phyFuns+164  (0x6B ? ? 0x06 0x04)
g_phyFuns+168  (0x6B 0x02 0x01 0x03 0x00)
g_phyFuns+168  (0x6B 0x02 0x02 0x03 0x00)
g_phyFuns+168  (0x6B ? ? 0x06 0x04)
g_phyFuns+0    (0x00)
g_phyFuns+168  (0x6B 0x02 0x01 0x03 0x00)
g_phyFuns+76   (0x01)
```

### `tx_cap_init` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+168  (0x6B 0x02 0x01 0x03 ? 0x0F)
g_phyFuns+168  (0x6B 0x02 0x02 0x03 ? 0x07)
g_phyFuns+168  (0x6B 0x02 0x02 0x06 0x04 0x03)
g_phyFuns+68   (0x01 0x80 0x50 0x00 0x00 0x00)
g_phyFuns+0    ()
g_phyFuns+0    (0x80 0x50 0x00)
g_phyFuns+0    (0x80 0x50 0x00)
g_phyFuns+0    ()
```

### `ram_meas_tone_pwr_db` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+68   (0x01 0x80 ? 0x00 0x00 0x00)
g_phyFuns+0    ()
g_phyFuns+76   (0x01)
```

### `ram_rfcal_pwrctrl` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+68   (0x01 ? ? 0x00 0x00 0x00)
g_phyFuns+0    ()
g_phyFuns+0    ()
g_phyFuns+76   (0x01 ? 0x01 0x00)
```

### `cal_rf_ana_gain` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+184  (0x05 0x01)
g_phyFuns+184  (0x01 0x02)
g_phyFuns+0    ()
g_phyFuns+220  ()
g_phyFuns+184  (0x05 0x01)
g_phyFuns+184  (0x01 0x02)
```

### `tx_pwctrl_init_cal` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
g_phyFuns+0    (? ? 0x00)
```

### `tx_pwctrl_init` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
```

### `bt_tx_pwctrl_init` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    (? 0x07)
g_phyFuns+184  (0x05 0x01)
g_phyFuns+184  (0x01 0x02)
g_phyFuns+188  (0x01 0x01)
g_phyFuns+184  (0x01 0x01)
g_phyFuns+220  ()
g_phyFuns+0    (? ? ? 0x01)
g_phyFuns+184  (0x05 0x01)
g_phyFuns+184  (0x01 0x02)
g_phyFuns+188  (0x01 0x01)
g_phyFuns+184  (0x01 0x01)
g_phyFuns+220  ()
g_phyFuns+0    ()
```

### `ram_phy_get_vdd33` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+168  (0x6B 0x02 0x09 0x07 0x07 0x01)
g_phyFuns+0    ()
g_phyFuns+168  (0x6B 0x02 0x09 0x07 0x07 0x00)
```

### `txpwr_offset` <sub>phy_chip_v7_cal.o</sub>

```
g_phyFuns+0    ()
g_phyFuns+0    (? 0x03)
g_phyFuns+0    (0xD33 0x03)
```

## `librtc.a`

9 functions call through a table.

### `bt_bb_init_cmplx` <sub>bt_bb.o</sub>

```
g_phyFuns+168  (0x67 0x01 0x0F 0x02 0x00 0x01)
g_phyFuns+168  (0x67 0x01 0x0F 0x03 0x03 0x01)
g_phyFuns+168  (0x67 0x01 ? 0x06 0x00 0x6E)
g_phyFuns+168  (0x67 0x01 0x05 0x06 0x00 0x6C)
g_phyFuns+168  (0x67 0x01 0x0F 0x04 0x04 0x00)
g_phyFuns+168  (0x67 0x01 0x0B 0x04 0x03 0x00)
g_phyFuns+168  (0x67 0x01 0x08 0x06 0x00 0x0C)
g_phyFuns+168  (0x67 0x01 0x07 0x06 0x00 0x0A)
```

### `bt_rxfilt` <sub>bt_bb.o</sub>

```
g_phyFuns+168  (0x67 0x01 0x0F 0x02 0x00 0x04)
```

### `bt_txfilt` <sub>bt_bb.o</sub>

```
g_phyFuns+168  (0x67 0x01 0x0F 0x02 0x00 0x00)
```

### `bt_cmplx_hq_wr` <sub>bt_bb.o</sub>

```
g_phyFuns+168  (0x67 0x01 0x06 0x06 0x00)
```

### `bt_cmplx_lq_wr` <sub>bt_bb.o</sub>

```
g_phyFuns+168  (0x67 0x01 0x05 0x06 0x00)
```

### `bt_cmplx_hq_re` <sub>bt_bb.o</sub>

```
g_phyFuns+164  (0x67 0x01 0x06 0x06 0x00)
```

### `bt_cmplx_lq_re` <sub>bt_bb.o</sub>

```
g_phyFuns+164  (0x67 0x01 0x05 0x06 0x00)
```

### `vdd33_init` <sub>rtc_analog.o</sub>

```
g_phyFuns+168  (0x6B 0x02 0x09 0x02 0x00 0x00)
```

### `get_vdd33` <sub>rtc_analog.o</sub>

```
g_phyFuns+168  (0x6B 0x02 0x09 0x07 0x07 0x01)
g_phyFuns+168  (0x6B 0x02 0x09 0x07 0x07 0x00)
```

## Table slots by call count

| Slot            | Calls |
| --------------- | ----: |
| `g_phyFuns+0`   |    90 |
| `g_phyFuns+168` |    71 |
| `g_phyFuns+184` |    66 |
| `g_phyFuns+160` |    55 |
| `g_phyFuns+152` |    26 |
| `g_phyFuns+188` |    20 |
| `g_phyFuns+164` |    15 |
| `g_phyFuns+220` |    11 |
| `g_phyFuns+208` |     9 |
| `g_phyFuns+192` |     8 |
| `g_phyFuns+196` |     8 |
| `g_phyFuns+68`  |     7 |
| `g_phyFuns+60`  |     6 |
| `g_phyFuns+44`  |     6 |
| `g_phyFuns+76`  |     5 |
| `g_phyFuns+132` |     4 |
| `g_phyFuns+40`  |     4 |
| `g_phyFuns+64`  |     4 |
| `g_phyFuns+52`  |     3 |
| `g_phyFuns+96`  |     3 |
| `g_phyFuns+200` |     3 |
| `g_phyFuns+216` |     2 |
| `g_phyFuns+112` |     2 |
| `g_phyFuns+116` |     2 |
| `g_phyFuns+108` |     2 |
| `g_phyFuns+204` |     2 |
| `g_phyFuns+120` |     1 |
| `g_phyFuns+32`  |     1 |
| `g_phyFuns+8`   |     1 |
| `g_phyFuns+12`  |     1 |
| `g_phyFuns+128` |     1 |
| `g_phyFuns+252` |     1 |
| `g_phyFuns+48`  |     1 |
| `g_phyFuns+172` |     1 |
| `g_phyFuns+248` |     1 |
| `g_phyFuns+224` |     1 |
| `g_phyFuns+240` |     1 |
| `g_phyFuns+232` |     1 |
| `g_phyFuns+228` |     1 |
| `g_phyFuns+236` |     1 |
| `g_phyFuns+212` |     1 |
| `g_phyFuns+244` |     1 |
| `g_phyFuns+100` |     1 |

## Analog blocks addressed

Argument 0 of the two serial-bus slots (160 and 168) only.

| Block  | Accesses |
| ------ | -------: |
| `0x67` |       48 |
| `0x6B` |       22 |
| `0x62` |       15 |
| `0x64` |       11 |
| `0x66` |       11 |
| `0x6A` |       10 |
| `0x68` |        7 |
| `0x65` |        1 |
| `0x63` |        1 |
