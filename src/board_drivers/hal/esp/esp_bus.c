// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_bus.c
 * @brief SPI backend for the platform bus seam (pc_platform.h), on the IDF driver.
 *
 * UART is a set of direct aliases in the header because its IDF calls take no handle. SPI does:
 * a bus is initialized once and a device is attached to it, so the handles live here rather than
 * in a header every translation unit would get its own copy of.
 *
 * The pins arrive from the caller. Which pins a bus runs on is a board fact, so the library does
 * not carry a default for it - a bridge target names its own, and a wrong guess here would be a
 * silent short rather than a compile error.
 */

#include "protocore_config.h"

#if PC_VENDOR_ESP

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

#endif // PC_VENDOR_ESP
