// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Sen0192.ino
 * @brief Detect motion with a DFRobot SEN0192 microwave Doppler sensor (PC_ENABLE_SEN0192).
 *
 * The SEN0192 is a 10.525 GHz microwave motion sensor: a 3-pin part (V / G / digital OUT) whose OUT line
 * asserts while it senses movement (via Doppler shift) within its adjustable range. Unlike a PIR it works
 * through thin non-metal enclosures and is unaffected by ambient light or temperature. It carries no
 * protocol - just a single digital line - so this driver tracks that line as a debounced presence signal:
 * presence asserts on motion and is held for PC_SEN0192_HOLD_MS after the last motion, so brief gaps
 * between Doppler returns don't make it flap.
 *
 * This sketch lights the onboard LED while motion is present and prints each detection over Serial.
 *
 * Wiring: module OUT -> ESP32 GPIO (PC_SEN0192_PIN, default 4), VCC -> 5V, GND -> GND. The pin,
 * hold time, and OUT polarity are ServerConfig knobs (PC_SEN0192_PIN / _HOLD_MS / _ACTIVE_HIGH) so a
 * driver's pin assignment lives in one place - override them with build flags, no code change.
 *
 * Build flag (PlatformIO): `-DPC_ENABLE_SEN0192=1`
 */

#define PC_ENABLE_SEN0192 1

#include "protocore.h" // declares the library dependency (Arduino build)
#include "services/peripherals/sen0192/sen0192.h"

static const int LED_PIN = 2; // onboard LED on most ESP32 dev boards

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    pc_sen0192_begin(); // input pin + polarity + hold all come from ServerConfig
    Serial.printf("SEN0192 microwave motion ready on GPIO%d - walk in front of it\n", PC_SEN0192_PIN);
}

void loop()
{
    pc_sen0192_poll(); // sample the OUT line (updates the debounced presence)
    bool present = pc_sen0192_present();
    digitalWrite(LED_PIN, present ? HIGH : LOW);

    // Print only when presence changes, so the Serial Monitor stays readable.
    static bool last = false;
    if (present != last)
    {
        last = present;
        if (present)
        {
            Serial.printf("[motion] DETECTED  (event #%u)\n", pc_sen0192_motion_count());
        }
        else
        {
            Serial.println("[motion] clear");
        }
    }
}
