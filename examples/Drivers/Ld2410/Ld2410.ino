// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Ld2410.ino
 * @brief Detect people with an HLK-LD2410 mmWave radar (PC_ENABLE_LD2410).
 *
 * The LD2410 is a cheap 24 GHz radar that sees a person even when they are perfectly still
 * (it senses breathing / micro-motion), through thin walls, and in the dark - no camera. It
 * streams a framed report over a UART; this sketch decodes it and lights the onboard LED
 * whenever someone is present, printing the distance and signal energy when presence changes.
 *
 * A great first soldering / bench-test project: wire four pins, wave your hand, watch the LED.
 *
 * Wiring (UART): module OUT/TX -> ESP32 GPIO 16 (RX), module RX -> ESP32 GPIO 17 (TX),
 * VCC -> 5V (the module has its own 3.3V regulator), GND -> GND. Change the pins below if your
 * board's UART2 is elsewhere.
 *
 * Build flag (PlatformIO): `-DPC_ENABLE_LD2410=1`
 */

#define PC_ENABLE_LD2410 1

#include "protocore.h" // declares the library dependency (Arduino build)
#include "services/peripherals/ld2410/ld2410.h"

static const int RADAR_RX = 16; // ESP32 RX  <- module TX/OUT
static const int RADAR_TX = 17; // ESP32 TX  -> module RX
static const int LED_PIN = 2;   // onboard LED on most ESP32 dev boards

static const char *state_name(uint8_t s)
{
    switch (s)
    {
    case LD2410_STATE_MOVING:
        return "moving";
    case LD2410_STATE_STATIC:
        return "stationary";
    case LD2410_STATE_BOTH:
        return "moving+stationary";
    default:
        return "clear";
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    pc_ld2410_begin(RADAR_RX, RADAR_TX);
    pc_ld2410_set_engineering(true); // also report the per-gate energies (nice for tuning)
    Serial.println("LD2410 radar ready - wave a hand in front of it");
}

void loop()
{
    // Pump the UART; pc_ld2410_poll() returns true only when a fresh report has been decoded.
    if (!pc_ld2410_poll())
    {
        return;
    }

    const Ld2410Report *r = pc_ld2410_last();
    digitalWrite(LED_PIN, pc_ld2410_present(r) ? HIGH : LOW);

    // Print only when the presence state changes, so the Serial Monitor stays readable.
    static uint8_t last_state = 0xFF;
    if (r->state != last_state)
    {
        last_state = r->state;
        Serial.printf("[radar] %-17s distance=%3ucm  moving=%3ucm/%-3u static=%3ucm/%-3u\n", state_name(r->state),
                      pc_ld2410_distance_cm(r), r->moving_cm, r->moving_energy, r->static_cm, r->static_energy);
    }
}
