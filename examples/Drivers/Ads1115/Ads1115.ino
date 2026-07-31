// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Ads1115.ino
 * @brief Read precise analog voltages with an ADS1115 16-bit ADC (PC_ENABLE_ADS1115).
 *
 * The ESP32's built-in ADC is noisy and only ~12-bit. The ADS1115 is a 16-bit analog-to-digital
 * converter on the I2C bus, with a programmable gain so you can zoom in on small signals. This
 * sketch reads channel AIN0 once a second and prints the voltage. A nice first soldering /
 * wiring project, and the foundation of any "measure a battery / sensor / potentiometer" build.
 *
 * Wiring (I2C): board SDA -> GPIO 21, SCL -> GPIO 22, VDD -> 3V3, GND -> GND, ADDR -> GND
 * (address 0x48). Connect what you want to measure between AIN0 and GND (0..4.096 V for the gain
 * used here; never exceed VDD).
 *
 * Build flag (PlatformIO): `-DPC_ENABLE_ADS1115=1`
 */

#define PC_ENABLE_ADS1115 1

#include "protocore.h" // declares the library dependency (Arduino build)
#include "services/peripherals/ads1115/ads1115.h"

void setup()
{
    Serial.begin(115200);
    pc_ads1115_begin(0x48);
    Serial.println("ADS1115 ready - reading AIN0 at +/-4.096 V full scale");
}

void loop()
{
    int32_t uv = 0; // microvolts
    if (pc_ads1115_read_uv(0, Ads1115Gain::ADS1115_GAIN_1, &uv))
    {
        // Print microvolts as V.mmm (millivolt precision) by hand - no float formatting.
        long v_whole = uv / 1000000;
        long v_frac = (uv % 1000000) / 1000;
        if (v_frac < 0)
        {
            v_frac = -v_frac;
        }
        const char *sign = (uv < 0 && v_whole == 0) ? "-" : "";
        Serial.printf("AIN0 = %s%ld.%03ld V\n", sign, v_whole, v_frac);
    }
    else
    {
        Serial.println("read failed (bus error)");
    }
    delay(1000);
}
