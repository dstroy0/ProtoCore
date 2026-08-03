// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_bus.c
 * @brief SPI and I2C backends for the platform bus seam (pc_platform.h), on the IDF drivers.
 *
 * UART is a set of direct aliases in the header because its IDF calls take no handle. SPI and I2C
 * do: a bus is installed once and transfers run against it, so the state lives here rather than in
 * a header every translation unit would get its own copy of.
 *
 * The pins arrive from the caller. Which pins a bus runs on is a board fact, so the library does
 * not carry a default for it - a bridge target names its own, and a wrong guess here would be a
 * silent short rather than a compile error.
 *
 * I2C runs on the legacy driver/i2c.h master API rather than driver/i2c_master.h. That is the one
 * arduino-esp32's Wire is built over, and IDF refuses a build that installs both on one port, so
 * this shares the bus with a sketch instead of colliding with it.
 */

#include "protocore_config.h"

#if PC_VENDOR_ESP

#include "driver/i2c.h"
#include "driver/spi_master.h"

#ifndef PC_SPI_HOST_ID
#define PC_SPI_HOST_ID SPI2_HOST // the general-purpose host on every part in the list
#endif
#ifndef PC_SPI_MAX_TXN
#define PC_SPI_MAX_TXN 4096 // bytes per transfer the DMA descriptor is sized for
#endif

// The SPI bus + device handles, owned by one instance (internal linkage): whether the bus is up,
// the pins it was brought up on, and the device the transactions run against. One named owner.
typedef struct
{
    proto_bool up;
    int mosi;
    int miso;
    int sclk;
    spi_device_handle_t dev;
    uint32_t hz; ///< clock the attached device was configured at; re-attach when it changes
    uint8_t mode;
} EspSpiCtx;
static EspSpiCtx s_spi;

int pc_platform_spi_begin(int mosi, int miso, int sclk)
{
    if (s_spi.up)
    {
        // Already up on different pins: the caller is describing a second bus, which this seam
        // does not carry. Fail rather than silently drive the first one.
        return (s_spi.mosi == mosi && s_spi.miso == miso && s_spi.sclk == sclk) ? 1 : 0;
    }
    spi_bus_config_t b = {0};
    b.mosi_io_num = mosi;
    b.miso_io_num = miso;
    b.sclk_io_num = sclk;
    b.quadwp_io_num = -1;
    b.quadhd_io_num = -1;
    b.max_transfer_sz = PC_SPI_MAX_TXN;
    if (spi_bus_initialize(PC_SPI_HOST_ID, &b, SPI_DMA_CH_AUTO) != ESP_OK)
    {
        return 0;
    }
    s_spi.up = PROTO_TRUE;
    s_spi.mosi = mosi;
    s_spi.miso = miso;
    s_spi.sclk = sclk;
    return 1;
}

// Attach (or re-attach) the shared device at the requested clock and mode. CS is driven by the
// caller through the GPIO seam, so the device is registered with no CS pin of its own.
static proto_bool spi_device_for(uint32_t hz, uint8_t bit_order, uint8_t mode)
{
    if (s_spi.dev && s_spi.hz == hz && s_spi.mode == mode)
    {
        return PROTO_TRUE;
    }
    if (s_spi.dev)
    {
        spi_bus_remove_device(s_spi.dev);
        s_spi.dev = NULL;
    }
    spi_device_interface_config_t d = {0};
    d.clock_speed_hz = (int)hz;
    d.mode = mode & 0x3u;
    d.spics_io_num = -1;
    d.queue_size = 1;
    d.flags = (bit_order == PC_SPI_LSBFIRST) ? (SPI_DEVICE_BIT_LSBFIRST | SPI_DEVICE_TXBIT_LSBFIRST) : 0;
    if (spi_bus_add_device(PC_SPI_HOST_ID, &d, &s_spi.dev) != ESP_OK)
    {
        s_spi.dev = NULL;
        return PROTO_FALSE;
    }
    s_spi.hz = hz;
    s_spi.mode = mode;
    return PROTO_TRUE;
}

int pc_platform_spi_txn(uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if (!s_spi.up || len == 0 || !spi_device_for(hz, bit_order, mode))
    {
        return 0;
    }
    spi_transaction_t t = {0};
    t.length = (size_t)len * 8u;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_polling_transmit(s_spi.dev, &t) == ESP_OK ? 1 : 0;
}

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------

#ifndef PC_I2C_PORT
#define PC_I2C_PORT I2C_NUM_0 // the port Wire uses, so a sketch and the core share one bus
#endif

// Whether the port is installed, and the pins it came up on. One named owner, internal linkage.
typedef struct
{
    proto_bool up;
    int sda;
    int scl;
} EspI2cCtx;
static EspI2cCtx s_i2c = {PROTO_FALSE, -1, -1};

int pc_platform_i2c_begin(int sda, int scl, uint32_t hz)
{
    if (s_i2c.up)
    {
        // Already up on different pins: a second bus, which this seam does not carry. Fail rather
        // than silently drive the first one.
        return (s_i2c.sda == sda && s_i2c.scl == scl) ? 1 : 0;
    }
    i2c_config_t c = {0};
    c.mode = I2C_MODE_MASTER;
    c.sda_io_num = sda;
    c.scl_io_num = scl;
    c.sda_pullup_en = GPIO_PULLUP_ENABLE;
    c.scl_pullup_en = GPIO_PULLUP_ENABLE;
    c.master.clk_speed = hz;
    if (i2c_param_config(PC_I2C_PORT, &c) != ESP_OK)
    {
        return 0;
    }
    // A sketch that already called Wire.begin() installed this port; treat that as ours rather
    // than failing, since both drive the same controller through the same driver.
    esp_err_t e = i2c_driver_install(PC_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
    {
        return 0;
    }
    s_i2c.up = PROTO_TRUE;
    s_i2c.sda = sda;
    s_i2c.scl = scl;
    return 1;
}

int pc_platform_i2c_write(uint8_t addr, const uint8_t *buf, uint32_t len, uint32_t ms)
{
    if (!s_i2c.up || buf == NULL || len == 0)
    {
        return 0;
    }
    return i2c_master_write_to_device(PC_I2C_PORT, addr, buf, len, pdMS_TO_TICKS(ms)) == ESP_OK ? 1 : 0;
}

int pc_platform_i2c_read(uint8_t addr, uint8_t *buf, uint32_t len, uint32_t ms)
{
    if (!s_i2c.up || buf == NULL || len == 0)
    {
        return 0;
    }
    return i2c_master_read_from_device(PC_I2C_PORT, addr, buf, len, pdMS_TO_TICKS(ms)) == ESP_OK ? 1 : 0;
}

int pc_platform_i2c_write_read(uint8_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen, uint32_t ms)
{
    if (!s_i2c.up || w == NULL || wlen == 0 || r == NULL || rlen == 0)
    {
        return 0;
    }
    // One transaction with a repeated start between the write and the read, which is what a
    // register read is: address the register, then turn the bus around without releasing it.
    return i2c_master_write_read_device(PC_I2C_PORT, addr, w, wlen, r, rlen, pdMS_TO_TICKS(ms)) == ESP_OK ? 1 : 0;
}

#endif // PC_VENDOR_ESP
