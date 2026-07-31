// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Mpr121.ino
 * @brief Read touch buttons with an MPR121 capacitive-touch chip (PC_ENABLE_MPR121).
 *
 * The MPR121 turns twelve wires (or copper pads, or pieces of foil / fruit) into touch buttons.
 * This sketch brings it up over I2C and prints which electrode you touch or release, lighting
 * the onboard LED while any pad is held. A great first soldering / wiring project - and the
 * decode + bring-up sequence are unit-tested on a PC, so only the I2C wiring is yours to get
 * right.
 *
 * Wiring (I2C): module SDA -> GPIO 21, SCL -> GPIO 22, VCC -> 3V3, GND -> GND, ADDR -> GND
 * (address 0x5A). Connect a wire from any ELE0..ELE11 pad to something touchable.
 *
 * Build flag (PlatformIO): `-DPC_ENABLE_MPR121=1`
 */

#define PC_ENABLE_MPR121 1

#include "protocore.h" // declares the library dependency (Arduino build)
#include "services/peripherals/mpr121/mpr121.h"

static const int LED_PIN = 2; // onboard LED on most ESP32 dev boards

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    if (pc_mpr121_begin(0x5A))
    {
        Serial.println("MPR121 ready - touch a pad (ELE0..ELE11)");
    }
    else
    {
        Serial.println("MPR121 not found - check wiring and the ADDR pin (address 0x5A)");
    }
}

void loop()
{
    static uint16_t last = 0;
    uint16_t now = pc_mpr121_read_touched();

    if (now != last)
    {
        // Report each electrode that just changed (press or release).
        for (uint8_t e = 0; e < MPR121_ELECTRODES; e++)
        {
            bool was = pc_mpr121_is_touched(last, e);
            bool is = pc_mpr121_is_touched(now, e);
            if (is && !was)
            {
                Serial.printf("electrode %u touched\n", e);
            }
            else if (was && !is)
            {
                Serial.printf("electrode %u released\n", e);
            }
        }
        digitalWrite(LED_PIN, now ? HIGH : LOW);
        last = now;
    }
    delay(20); // ~50 Hz poll is plenty for touch
}
