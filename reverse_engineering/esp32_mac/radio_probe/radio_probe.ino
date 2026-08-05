// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Reproduction firmware for the radio analysis.
//
// The vendor blobs fill their PHY dispatch table at runtime, so no static artifact says which slot
// reaches which routine. This dumps the live table; the names come from the ELF this build already
// produces, matched against it afterwards by parse_probe.py.
//
// It references exactly one blob symbol, the table pointer. An earlier version took the address of
// forty libphy routines so it could name them on-device, which forced those objects to link ahead
// of the order WiFi bring-up expects and panicked before setup() ran. Resolving names offline
// leaves the link untouched, and the ELF carries every address either way.
//
//   arduino-cli compile -b esp32:esp32:esp32s3 reverse_engineering/esp32_mac/radio_probe
//   arduino-cli upload  -b esp32:esp32:esp32s3 -p COM3 reverse_engineering/esp32_mac/radio_probe

#include <WiFi.h>

extern "C"
{
    // Filled during radio bring-up. One object in libphy imports it, another defines it.
    extern void *g_phyFuns;
}

// Slots read. The esp32 capture reaches offset 220, so 128 covers it with room to spare.
#define SLOTS 128

// Internal RAM on this die. The table is refused unless it points here, so a stale or null
// pointer is reported rather than followed into a fault.
#define DRAM_LO 0x3FC80000u
#define DRAM_HI 0x3FD00000u

void setup()
{
    Serial.begin(115200);
    delay(3000);

    Serial.println();
    Serial.println("=== RADIO PROBE ===");
    Serial.printf("BEFORE g_phyFuns = 0x%08X\n", (unsigned)(uintptr_t)g_phyFuns);

    // The table holds nothing until the radio has been brought up.
    WiFi.mode(WIFI_STA);
    WiFi.begin("q_6", "12345678!");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
    {
        delay(200);
    }
    Serial.printf("wifi: %s  rssi %d  channel %d\n",
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected", (int)WiFi.RSSI(),
                  (int)WiFi.channel());

    uintptr_t base = (uintptr_t)g_phyFuns;
    Serial.printf("AFTER  g_phyFuns = 0x%08X\n", (unsigned)base);

    if (base < DRAM_LO || base >= DRAM_HI)
    {
        Serial.println("table pointer is not in DRAM: nothing to read");
    }
    else
    {
        void **tbl = (void **)base;
        for (int i = 0; i < SLOTS; i++)
        {
            Serial.printf("SLOT %4d +%-5d 0x%08X\n", i, i * 4, (unsigned)(uintptr_t)tbl[i]);
            if ((i & 0x0F) == 0x0F)
            {
                Serial.flush();
            }
        }
    }
    Serial.println("=== END ===");
    Serial.flush();
}

void loop()
{
    delay(1000);
}
