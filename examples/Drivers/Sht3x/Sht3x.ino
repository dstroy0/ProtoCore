// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Sht3x.ino
 * @brief Measure temperature and humidity with a Sensirion SHT3x (PC_ENABLE_SHT3X).
 *
 * The SHT3x (SHT30/31/35) is a small, accurate temperature + humidity sensor on the I2C bus.
 * This reads it once a second and prints the values. Every reading is protected by a CRC-8 the
 * chip sends with its data, so a bad wire shows up as a "checksum failed" rather than a wrong
 * number. A great first soldering / wiring project, and the sensor most weather / room-monitor
 * projects start from.
 *
 * Wiring (I2C): module SDA -> GPIO 21, SCL -> GPIO 22, VCC -> 3V3, GND -> GND, ADDR -> GND
 * (address 0x44).
 *
 * Build flag (PlatformIO): `-DPC_ENABLE_SHT3X=1`
 */

#define PC_ENABLE_SHT3X 1

#include "protocore.h" // declares the library dependency (Arduino build)
#include "services/peripherals/sht3x/sht3x.h"

void setup()
{
    Serial.begin(115200);
    if (pc_sht3x_begin(0x44))
    {
        Serial.println("SHT3x ready");
    }
    else
    {
        Serial.println("SHT3x not found - check wiring and the ADDR pin (address 0x44)");
    }
}

void loop()
{
    int32_t temp_mc = 0, rh_mpct = 0; // milli-degrees C, milli-percent RH
    if (pc_sht3x_read(&temp_mc, &rh_mpct))
    {
        // Print the milli-units as X.XXX by hand (no float formatting needed).
        long t_whole = temp_mc / 1000;
        long t_frac = temp_mc % 1000;
        if (t_frac < 0)
        {
            t_frac = -t_frac;
        }
        const char *sign = (temp_mc < 0 && t_whole == 0) ? "-" : "";
        Serial.printf("temp=%s%ld.%03ld C   humidity=%ld.%03ld %%\n", sign, t_whole, t_frac, (long)(rh_mpct / 1000),
                      (long)(rh_mpct % 1000));
    }
    else
    {
        Serial.println("read failed (checksum or bus error)");
    }
    delay(1000);
}
