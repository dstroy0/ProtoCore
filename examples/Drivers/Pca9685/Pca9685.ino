// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Pca9685.ino
 * @brief Drive servos / LEDs with a PCA9685 16-channel PWM board (PC_ENABLE_PCA9685).
 *
 * The PCA9685 gives the ESP32 sixteen hardware PWM channels over just two I2C wires - enough for
 * a walking robot, a set of dimmable LEDs, or one very smooth servo. This sketch sets the PWM
 * frequency to 50 Hz (what hobby servos expect) and sweeps a servo on channel 0 back and forth.
 * A great first soldering / wiring project.
 *
 * Wiring (I2C + power): board SDA -> GPIO 21, SCL -> GPIO 22, VCC -> 3V3 (chip logic), GND ->
 * GND. Plug the servo onto the channel-0 3-pin header. IMPORTANT: power the servos from the
 * board's V+ screw terminal with a separate 5-6 V supply (a servo can draw far more than the
 * ESP32's 3V3 pin can give) and share GND.
 *
 * Build flag (PlatformIO): `-DPC_ENABLE_PCA9685=1`
 */

#define PC_ENABLE_PCA9685 1

#include "protocore.h" // declares the library dependency (Arduino build)
#include "services/peripherals/pca9685/pca9685.h"

static const uint8_t SERVO_CH = 0;

void setup()
{
    Serial.begin(115200);
    if (pc_pca9685_begin(0x40, 50)) // 50 Hz suits hobby servos
    {
        Serial.println("PCA9685 ready - sweeping the servo on channel 0");
    }
    else
    {
        Serial.println("PCA9685 not found - check wiring and the address (0x40)");
    }
}

void loop()
{
    // Sweep ~500 us..2500 us (roughly 0..180 degrees on a typical servo) and back.
    for (int us = 500; us <= 2500; us += 20)
    {
        pc_pca9685_set_servo_us(SERVO_CH, us);
        delay(15);
    }
    for (int us = 2500; us >= 500; us -= 20)
    {
        pc_pca9685_set_servo_us(SERVO_CH, us);
        delay(15);
    }
}
