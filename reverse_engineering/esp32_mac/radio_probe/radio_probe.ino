// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Reproduction firmware for the radio analysis.
//
// The vendor blobs fill their PHY dispatch table at runtime, so no static artifact says which slot
// reaches which routine. This build links against libphy directly, so its ELF carries the address
// of every routine by name. Printing the live table alongside those addresses names each slot by
// comparison rather than by inference.
//
// The radio has to be up before the table holds anything, so WiFi starts first.
//
//   arduino-cli compile -b esp32:esp32:esp32s3 tools/dev_env/radio_probe
//   arduino-cli upload  -b esp32:esp32:esp32s3 -p COM3 tools/dev_env/radio_probe

#include <WiFi.h>

extern "C"
{
    // The table pointer libphy fills during radio bring-up. One object in the archive imports it,
    // another defines it, so it links from here.
    extern void *g_phyFuns;

    // Routines libphy keeps in IRAM on this die. Only their addresses are read, so the declared
    // signature does not have to match the real one.
    void btbb_wifi_bb_cfg2(void);
    void phy_close_rf(void);
    void phy_dig_reg_backup(void);
    void phy_freq_mem_backup(void);
    void phy_freq_module_resetn(void);
    void phy_i2c_bbtop_wakeup(void);
    void phy_wakeup_init(void);
    void phy_wifi_enable_set(void);
    void phy_xpd_tsens(void);
    void ram_bt_set_tx_gain(void);
    void ram_chip_i2c_readReg(void);
    void ram_chip_i2c_writeReg(void);
    void ram_disable_wifi_agc(void);
    void ram_enable_wifi_agc(void);
    void ram_fe_i2c_reg_renew(void);
    void ram_get_i2c_hostid(void);
    void ram_i2c_master_reset(void);
    void ram_phy_close_rf(void);
    void ram_phy_dis_hw_set_freq(void);
    void ram_phy_en_hw_set_freq(void);
    void ram_phy_i2c_init1(void);
    void ram_phy_wakeup_init(void);
    void ram_set_pbus_reg(void);
    void ram_set_txcap_reg(void);
    void ram_wifi_tx_dig_gain(void);
    void ram_wifi_tx_dig_gain_reg(void);
    void rom_bt_filter_reg(void);
    void rom_bt_tx_dig_gain(void);
    void rom_open_i2c_xpd(void);
    void rom_phy_ant_init(void);
    void rom_phy_dig_reg_backup(void);
    void rom_phy_freq_mem_backup(void);
    void rom_phy_reg_init(void);
    void rom_phy_xpd_rf(void);
    void rom_phy_xpd_tsens(void);
    void rom_set_chan_reg(void);
    void rom_set_tx_dig_gain(void);
    void rom_wifi_agc_sat_gain(void);
    void rom_write_txrate_power_offset(void);
    void rx_11b_opt(void);
    void rx_agc_reg_opt(void);
    void wait_freq_set_busy(void);
}

struct Named
{
    const char *name;
    void *addr;
};

#define ENTRY(f) {#f, (void *)&f}

static const Named KNOWN[] = {
    ENTRY(btbb_wifi_bb_cfg2),
    ENTRY(phy_close_rf),
    ENTRY(phy_dig_reg_backup),
    ENTRY(phy_freq_mem_backup),
    ENTRY(phy_freq_module_resetn),
    ENTRY(phy_i2c_bbtop_wakeup),
    ENTRY(phy_wakeup_init),
    ENTRY(phy_wifi_enable_set),
    ENTRY(phy_xpd_tsens),
    ENTRY(ram_bt_set_tx_gain),
    ENTRY(ram_chip_i2c_readReg),
    ENTRY(ram_chip_i2c_writeReg),
    ENTRY(ram_disable_wifi_agc),
    ENTRY(ram_enable_wifi_agc),
    ENTRY(ram_fe_i2c_reg_renew),
    ENTRY(ram_get_i2c_hostid),
    ENTRY(ram_i2c_master_reset),
    ENTRY(ram_phy_close_rf),
    ENTRY(ram_phy_dis_hw_set_freq),
    ENTRY(ram_phy_en_hw_set_freq),
    ENTRY(ram_phy_i2c_init1),
    ENTRY(ram_phy_wakeup_init),
    ENTRY(ram_set_pbus_reg),
    ENTRY(ram_set_txcap_reg),
    ENTRY(ram_wifi_tx_dig_gain),
    ENTRY(ram_wifi_tx_dig_gain_reg),
    ENTRY(rom_bt_filter_reg),
    ENTRY(rom_bt_tx_dig_gain),
    ENTRY(rom_open_i2c_xpd),
    ENTRY(rom_phy_ant_init),
    ENTRY(rom_phy_dig_reg_backup),
    ENTRY(rom_phy_freq_mem_backup),
    ENTRY(rom_phy_reg_init),
    ENTRY(rom_phy_xpd_rf),
    ENTRY(rom_phy_xpd_tsens),
    ENTRY(rom_set_chan_reg),
    ENTRY(rom_set_tx_dig_gain),
    ENTRY(rom_wifi_agc_sat_gain),
    ENTRY(rom_write_txrate_power_offset),
    ENTRY(rx_11b_opt),
    ENTRY(rx_agc_reg_opt),
    ENTRY(wait_freq_set_busy),
};

// Slots read from the table. The esp32 analog capture reaches offset 220, so 128 covers it.
#define SLOTS 128

static const char *name_for(void *p)
{
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++)
    {
        if (KNOWN[i].addr == p)
        {
            return KNOWN[i].name;
        }
    }
    return nullptr;
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    WiFi.mode(WIFI_STA);
    WiFi.begin("q_6", "12345678!");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
    {
        delay(200);
    }

    Serial.println();
    Serial.println("=== RADIO PROBE ===");
    Serial.printf("wifi: %s  rssi %d  channel %d\n",
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected", (int)WiFi.RSSI(),
                  (int)WiFi.channel());

    Serial.println("--- known symbol addresses ---");
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++)
    {
        Serial.printf("SYM 0x%08X %s\n", (unsigned)(uintptr_t)KNOWN[i].addr, KNOWN[i].name);
    }

    Serial.printf("--- g_phyFuns = 0x%08X ---\n", (unsigned)(uintptr_t)g_phyFuns);
    void **tbl = (void **)g_phyFuns;
    if (tbl == nullptr)
    {
        Serial.println("table is null: the radio did not bring it up");
    }
    else
    {
        for (int i = 0; i < SLOTS; i++)
        {
            void *e = tbl[i];
            const char *n = name_for(e);
            Serial.printf("SLOT %4d +%-5d 0x%08X %s\n", i, i * 4, (unsigned)(uintptr_t)e, n ? n : "");
        }
    }
    Serial.println("=== END ===");
}

void loop()
{
    delay(1000);
}
