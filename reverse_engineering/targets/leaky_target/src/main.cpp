// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file main.cpp
 * @brief A real, physically-probeable target: the same leaky operation
 *        hardening_demo.py's "Story 2" models synthetically, running on actual silicon so a
 *        real probe (NF/EM probe -> amp -> AD9226/AD9238/scope, or the internal-audio front
 *        end listening for a relay/buzzer tied to TRIGGER_PIN) sees a real signal, not a
 *        simulation of one.
 *
 * The leaky operation is deliberately the smallest possible one: a single AES SubBytes step,
 * `PC_AES_SBOX[plaintext ^ KEY_BYTE] ^ plaintext` - the exact Hamming-distance intermediate
 * this whole reverse_engineering suite's CPA/TVLA/SPA code already targets. TRIGGER_PIN goes
 * high right before it runs and low right after, so an external capture front end has a clean
 * edge to arm on.
 *
 * Two build environments, one source file:
 *
 *   vulnerable - the op runs at a FIXED time after the trigger edge, every iteration. A CPA
 *                at that fixed sample offset recovers KEY_BYTE cleanly (see the pipeline's
 *                own dpa_cpa_engine.py / template_attack_engine.py / spa_engine.py).
 *   hardened   - the SAME op, but preceded by a random delay (TARGET_HARDENED's desync
 *                countermeasure) drawn fresh every iteration. The op's timing relative to the
 *                trigger edge now varies trace to trace - the exact "hiding" countermeasure
 *                hardening_demo.py's Story 2 demonstrates defeating a naive fixed-position
 *                CPA, and which a realignment pass (dpa_cpa_engine.align_trace_jitter) can
 *                undo if the desync window is not deep enough.
 *
 * This firmware does not talk to the analysis pipeline itself - it is the physical artifact
 * you point a REAL probe/scope/DAQ node at. hardening_demo.py's synthetic version is what is
 * automatically, numerically verified (`python hardening_demo.py`); this is what makes the
 * same story tangible on a bench with real hardware.
 *
 *     pio run -e vulnerable -t upload --upload-port COM7
 *     pio run -e hardened   -t upload --upload-port COM7
 */

#include "crypto/cipher/aes_sbox.h"
#include <Arduino.h>
#include <esp_random.h>

#ifndef TARGET_HARDENED
#define TARGET_HARDENED 0
#endif
#ifndef TRIGGER_PIN
#define TRIGGER_PIN 4 // wire this to the capture front end's external trigger input
#endif
#ifndef KEY_BYTE
#define KEY_BYTE 0xD3 // the "secret" this target teaches recovering - change it, it still works
#endif
#ifndef DESYNC_WINDOW_US
#define DESYNC_WINDOW_US 200 // hardened only: the random pre-op delay's upper bound
#endif
#ifndef LOOP_PERIOD_MS
#define LOOP_PERIOD_MS 2 // paces iterations so trigger edges stay individually resolvable
#endif

static uint32_t g_iteration = 0;

void setup()
{
    Serial.begin(115200);
    delay(300);
    pinMode(TRIGGER_PIN, OUTPUT);
    digitalWrite(TRIGGER_PIN, LOW);
#if TARGET_HARDENED
    Serial.println("[*] hardened target: leaky op at a RANDOMIZED time after the trigger edge");
    Serial.printf("    desync window: 0-%d us\n", DESYNC_WINDOW_US);
#else
    Serial.println("[*] vulnerable target: leaky op at a FIXED time after the trigger edge");
#endif
    Serial.printf("    KEY_BYTE = 0x%02X, TRIGGER_PIN = %d\n", KEY_BYTE, TRIGGER_PIN);
}

void loop()
{
    uint8_t plaintext = (uint8_t)(g_iteration & 0xFF);

    digitalWrite(TRIGGER_PIN, HIGH); // the fixed reference edge an external capture arms on
#if TARGET_HARDENED
    uint32_t desync_us = esp_random() % DESYNC_WINDOW_US;
    if (desync_us)
    {
        delayMicroseconds(desync_us); // the hiding countermeasure: move the leak, not remove it
    }
#endif
    // The leaky operation itself: a single SubBytes Hamming-distance step. `volatile` and the
    // (void) below only stop the compiler from optimizing the "dead" result away - on real
    // silicon this line's own CMOS switching current is the actual side channel, physics the
    // firmware cannot fake or suppress by itself (that is what a real probe is for).
    volatile uint8_t intermediate = PC_AES_SBOX[plaintext ^ KEY_BYTE] ^ plaintext;
    digitalWrite(TRIGGER_PIN, LOW);
    (void)intermediate;

    Serial.printf("PT %lu %02X\n", (unsigned long)g_iteration, plaintext); // correlate a capture's
    // trace_id with this iteration's plaintext by tailing this serial port alongside the DAQ
    // node's own log - both count monotonically from the same trigger source.
    g_iteration++;

    delay(LOOP_PERIOD_MS);
}
