// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Ina219.ino
 * @brief Measure current and power with an INA219 monitor (PC_ENABLE_INA219).
 *
 * The INA219 sits in series with a load and reports the voltage, the current flowing through it,
 * and the power it uses - so you can answer "how much power does this actually draw?". This
 * sketch prints all three once a second. A nice first soldering / wiring project and the basis
 * of any battery-life or power-budget experiment.
 *
 * Wiring (I2C + load in series): board VCC -> 3V3, GND -> GND, SDA -> GPIO 21, SCL -> GPIO 22.
 * Put the board in the power path: supply (+) -> Vin+, Vin- -> your load (+), load (-) -> GND.
 * The default assumes the common 0.1 ohm shunt on these breakouts.
 *
 * Build flag (PlatformIO): `-DPC_ENABLE_INA219=1`
 */

#define PC_ENABLE_INA219 1

#include "protocore.h" // declares the library dependency (Arduino build)
#include "services/peripherals/ina219/ina219.h"

void setup()
{
    Serial.begin(115200);
    // current LSB 100 uA/bit, 0.1 ohm (100 mohm) shunt -> up to ~3.2 A full scale.
    if (pc_ina219_begin(0x40, 100, 100))
    {
        Serial.println("INA219 ready");
    }
    else
    {
        Serial.println("INA219 not found - check wiring and the address (0x40)");
    }
}

void loop()
{
    int32_t bus_mv = 0, current_ua = 0, power_uw = 0;
    bool ok = pc_ina219_read_bus_mv(&bus_mv);
    ok &= pc_ina219_read_current_ua(&current_ua);
    ok &= pc_ina219_read_power_uw(&power_uw);

    if (ok)
    {
        // Print integer milli/micro units as fixed-point by hand (no float formatting).
        long ma = current_ua / 1000, ma_f = current_ua % 1000;
        if (ma_f < 0)
        {
            ma_f = -ma_f;
        }
        Serial.printf("bus=%ld.%03ld V  current=%ld.%03ld mA  power=%ld mW\n", bus_mv / 1000, (long)(bus_mv % 1000), ma,
                      ma_f, (long)(power_uw / 1000));
    }
    else
    {
        Serial.println("read failed (bus error)");
    }
    delay(1000);
}
