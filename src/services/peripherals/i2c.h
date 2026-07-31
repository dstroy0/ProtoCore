// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file i2c.h
 * @brief The one owner of the shared I2C bus bring-up for the peripheral drivers.
 *
 * The sensor / peripheral drivers (RTC, SHT3x, MPR121, ADS1115, INA219, PCA9685) all share a
 * single I2C bus, so they all initialize it through pc_i2c_begin() rather than calling
 * Wire.begin() themselves. That gives one place to choose the pins: PC_I2C_SDA_PIN /
 * PC_I2C_SCL_PIN (default -1 = the platform default GPIO 21 / 22). Move them off 21/22 when
 * those pins are needed elsewhere - most importantly a wired-Ethernet PHY: the LAN8720 RMII on
 * the classic ESP32 (WROOM/WROVER) and the ESP32-P4 uses GPIO 21 and 22 (the S3/C3 have no RMII
 * EMAC and use an SPI Ethernet such as the W5500 instead). Re-begin is idempotent, so per-driver
 * calls are harmless.
 *
 * Host builds compile this to nothing (there is no Wire); the I2C transfer is ESP32-only.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_I2C_H
#define PROTOCORE_I2C_H

#include "protocore_config.h"

#if defined(ARDUINO)

#include <Wire.h>

/** @brief Bring up the shared I2C bus on PC_I2C_SDA_PIN / PC_I2C_SCL_PIN (-1 = default). */
inline void pc_i2c_begin()
{
    Wire.begin((int)PC_I2C_SDA_PIN, (int)PC_I2C_SCL_PIN);
}

#endif // ARDUINO

#endif // PROTOCORE_I2C_H
