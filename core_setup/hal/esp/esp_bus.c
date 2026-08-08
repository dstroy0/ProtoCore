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
 * this shares the bus with a sketch instead of colliding with it. Every transfer is assembled as a
 * command link rather than through the one-call helpers, because the helpers take a 7-bit address
 * and the link is what can put a 10-bit one on the wire.
 */

#include "protocore_config.h"

#if PC_VENDOR_ESP

#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h" // esp_rom_delay_us: the bit-banged recovery clock

#ifndef PC_SPI_MAX_TXN
#define PC_SPI_MAX_TXN 4096 // bytes per transfer the DMA descriptor is sized for
#endif

/** @brief SPI controllers this seam carries. */
#define PC_SPI_HOSTS 2

/** @brief I2C controllers this seam carries. */
#define PC_I2C_BUSES 2

// Commands one link holds: start, address (two bytes at 10 bits), a write span, a repeated start,
// the second address, a read span, a final NACKed byte, stop. Twelve covers the longest shape.
#define PC_I2C_LINK_CMDS 12

// The SPI bus + device handles, owned by one instance (internal linkage): whether the host is up,
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
    uint8_t bit_order;
    proto_bool half_duplex; ///< dual and quad lane widths need the device in half duplex
} EspSpiHost;

// Whether each port is installed, the pins it came up on, the clock it is running, and the buffer
// its command links are built in. One named owner, internal linkage.
typedef struct
{
    proto_bool up;
    int sda;
    int scl;
    uint32_t hz;
    uint8_t link[I2C_LINK_RECOMMENDED_SIZE(PC_I2C_LINK_CMDS)];
} EspI2cBus;

typedef struct
{
    EspSpiHost spi[PC_SPI_HOSTS];
    EspI2cBus i2c[PC_I2C_BUSES];
} EspBusCtx;
static EspBusCtx s_bus;

// ---------------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------------

// host index -> the IDF controller id. SPI3_HOST is absent on the C-series parts, which have one
// general-purpose host, so index 1 folds onto index 0 there.
static spi_host_device_t spi_host_id(uint8_t host)
{
#ifdef SPI3_HOST
    return host ? SPI3_HOST : SPI2_HOST;
#else
    (void)host;
    return SPI2_HOST;
#endif
}

int pc_platform_spi_begin(uint8_t host, int mosi, int miso, int sclk, int quadwp, int quadhd)
{
    if (host >= PC_SPI_HOSTS)
    {
        return 0;
    }
    EspSpiHost *h = &s_bus.spi[host];
    if (h->up)
    {
        // Already up on different pins: the caller is describing a second bus, which one host does
        // not carry. Fail rather than silently drive the first one.
        return (h->mosi == mosi && h->miso == miso && h->sclk == sclk) ? 1 : 0;
    }
    spi_bus_config_t b = {0};
    b.mosi_io_num = mosi;
    b.miso_io_num = miso;
    b.sclk_io_num = sclk;
    b.quadwp_io_num = quadwp;
    b.quadhd_io_num = quadhd;
    b.max_transfer_sz = PC_SPI_MAX_TXN;
    if (spi_bus_initialize(spi_host_id(host), &b, SPI_DMA_CH_AUTO) != ESP_OK)
    {
        return 0;
    }
    h->up = PROTO_TRUE;
    h->mosi = mosi;
    h->miso = miso;
    h->sclk = sclk;
    return 1;
}

// Attach (or re-attach) the shared device at the requested clock, mode, bit order and duplex. CS is
// driven by the caller through the GPIO seam, so the device is registered with no CS pin of its own.
static proto_bool spi_device_for(EspSpiHost *h, uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode,
                                 proto_bool half_duplex)
{
    if (h->dev != NULL && h->hz == hz && h->mode == mode && h->bit_order == bit_order && h->half_duplex == half_duplex)
    {
        return PROTO_TRUE;
    }
    if (h->dev != NULL)
    {
        spi_bus_remove_device(h->dev);
        h->dev = NULL;
    }
    spi_device_interface_config_t d = {0};
    d.clock_speed_hz = (int)hz;
    d.mode = mode & 0x3u;
    d.spics_io_num = -1;
    d.queue_size = 1;
    d.flags = (bit_order == PC_SPI_LSBFIRST) ? (SPI_DEVICE_BIT_LSBFIRST | SPI_DEVICE_TXBIT_LSBFIRST) : 0;
    if (half_duplex)
    {
        d.flags |= SPI_DEVICE_HALFDUPLEX;
    }
    if (spi_bus_add_device(spi_host_id(host), &d, &h->dev) != ESP_OK)
    {
        h->dev = NULL;
        return PROTO_FALSE;
    }
    h->hz = hz;
    h->mode = mode;
    h->bit_order = bit_order;
    h->half_duplex = half_duplex;
    return PROTO_TRUE;
}

int pc_platform_spi_txn(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx, uint8_t *rx,
                        uint32_t len)
{
    if (host >= PC_SPI_HOSTS)
    {
        return 0;
    }
    EspSpiHost *h = &s_bus.spi[host];
    if (!h->up || len == 0 || !spi_device_for(h, host, hz, bit_order, mode, PROTO_FALSE))
    {
        return 0;
    }
    spi_transaction_t t = {0};
    t.length = (size_t)len * 8u;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_polling_transmit(h->dev, &t) == ESP_OK ? 1 : 0;
}

int pc_platform_spi_txn_ext(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, uint16_t cmd, uint8_t cmd_bits,
                            uint32_t addr, uint8_t addr_bits, uint8_t dummy_bits, uint8_t lanes, const uint8_t *tx,
                            uint8_t *rx, uint32_t len)
{
    if (host >= PC_SPI_HOSTS)
    {
        return 0;
    }
    EspSpiHost *h = &s_bus.spi[host];
    // Two and four lanes share one set of pins between the directions, so the device runs half
    // duplex. A dummy phase also has no meaning to a full-duplex device, which clocks both ways at
    // once, so it selects half duplex as well.
    proto_bool half = (lanes > PC_SPI_LANES_1 || dummy_bits > 0) ? PROTO_TRUE : PROTO_FALSE;
    if (!h->up || !spi_device_for(h, host, hz, bit_order, mode, half))
    {
        return 0;
    }
    spi_transaction_ext_t t = {0};
    t.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    if (lanes == PC_SPI_LANES_2)
    {
        t.base.flags |= SPI_TRANS_MODE_DIO;
    }
    else if (lanes == PC_SPI_LANES_4)
    {
        t.base.flags |= SPI_TRANS_MODE_QIO;
    }
    t.base.cmd = cmd;
    t.base.addr = addr;
    t.base.tx_buffer = tx;
    t.base.rx_buffer = rx;
    // Half duplex splits the data phase in two: length is what goes out, rxlength what comes back.
    // Full duplex clocks one span both ways, so rxlength stays 0 and follows length.
    if (half)
    {
        t.base.length = (tx != NULL) ? (size_t)len * 8u : 0u;
        t.base.rxlength = (rx != NULL) ? (size_t)len * 8u : 0u;
    }
    else
    {
        t.base.length = (size_t)len * 8u;
    }
    t.command_bits = cmd_bits;
    t.address_bits = addr_bits;
    t.dummy_bits = dummy_bits;
    return spi_device_polling_transmit(h->dev, (spi_transaction_t *)&t) == ESP_OK ? 1 : 0;
}

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------

// bus index -> the IDF port. Port 0 is the one Wire uses, so a sketch and the core share it.
static i2c_port_t i2c_port_id(uint8_t bus)
{
    return bus ? I2C_NUM_1 : I2C_NUM_0;
}

// Put the address on the bus in the write direction: one byte for a 7-bit address, or the 11110xx
// prefix byte holding bits 9:8 followed by the low eight for a 10-bit one.
static esp_err_t i2c_addr_w(i2c_cmd_handle_t cmd, uint16_t addr)
{
    if ((addr & PC_I2C_ADDR_10BIT) != 0)
    {
        uint16_t a = (uint16_t)(addr & PC_I2C_ADDR_MASK);
        esp_err_t e = i2c_master_write_byte(cmd, (uint8_t)(0xF0u | ((a >> 7) & 0x06u)), PROTO_TRUE);
        if (e != ESP_OK)
        {
            return e;
        }
        return i2c_master_write_byte(cmd, (uint8_t)(a & 0xFFu), PROTO_TRUE);
    }
    return i2c_master_write_byte(cmd, (uint8_t)(((addr & 0x7Fu) << 1) | 0u), PROTO_TRUE);
}

// Put the address on the bus in the read direction. A 10-bit read repeats only the prefix byte,
// the low eight having been sent in the write phase that precedes it.
static esp_err_t i2c_addr_r(i2c_cmd_handle_t cmd, uint16_t addr)
{
    if ((addr & PC_I2C_ADDR_10BIT) != 0)
    {
        uint16_t a = (uint16_t)(addr & PC_I2C_ADDR_MASK);
        return i2c_master_write_byte(cmd, (uint8_t)(0xF0u | ((a >> 7) & 0x06u) | 1u), PROTO_TRUE);
    }
    return i2c_master_write_byte(cmd, (uint8_t)(((addr & 0x7Fu) << 1) | 1u), PROTO_TRUE);
}

int pc_platform_i2c_begin(uint8_t bus, int sda, int scl, uint32_t hz)
{
    if (bus >= PC_I2C_BUSES)
    {
        return 0;
    }
    EspI2cBus *b = &s_bus.i2c[bus];
    if (b->up)
    {
        // Already up on different pins: a second bus, which one port does not carry. Fail rather
        // than silently drive the first one.
        return (b->sda == sda && b->scl == scl) ? 1 : 0;
    }
    i2c_config_t c = {0};
    c.mode = I2C_MODE_MASTER;
    c.sda_io_num = sda;
    c.scl_io_num = scl;
    c.sda_pullup_en = GPIO_PULLUP_ENABLE;
    c.scl_pullup_en = GPIO_PULLUP_ENABLE;
    c.master.clk_speed = hz;
    if (i2c_param_config(i2c_port_id(bus), &c) != ESP_OK)
    {
        return 0;
    }
    // A sketch that already called Wire.begin() installed this port; treat that as ours rather
    // than failing, since both drive the same controller through the same driver.
    esp_err_t e = i2c_driver_install(i2c_port_id(bus), I2C_MODE_MASTER, 0, 0, 0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
    {
        return 0;
    }
    b->up = PROTO_TRUE;
    b->sda = sda;
    b->scl = scl;
    b->hz = hz;
    return 1;
}

int pc_platform_i2c_set_clock(uint8_t bus, uint32_t hz)
{
    if (bus >= PC_I2C_BUSES || hz == 0)
    {
        return 0;
    }
    EspI2cBus *b = &s_bus.i2c[bus];
    if (!b->up)
    {
        return 0;
    }
    if (b->hz == hz)
    {
        return 1;
    }
    i2c_config_t c = {0};
    c.mode = I2C_MODE_MASTER;
    c.sda_io_num = b->sda;
    c.scl_io_num = b->scl;
    c.sda_pullup_en = GPIO_PULLUP_ENABLE;
    c.scl_pullup_en = GPIO_PULLUP_ENABLE;
    c.master.clk_speed = hz;
    if (i2c_param_config(i2c_port_id(bus), &c) != ESP_OK)
    {
        return 0;
    }
    b->hz = hz;
    return 1;
}

int pc_platform_i2c_write(uint8_t bus, uint16_t addr, const uint8_t *buf, uint32_t len, uint32_t ms)
{
    if (bus >= PC_I2C_BUSES || buf == NULL || len == 0)
    {
        return 0;
    }
    EspI2cBus *b = &s_bus.i2c[bus];
    if (!b->up)
    {
        return 0;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create_static(b->link, sizeof(b->link));
    if (cmd == NULL)
    {
        return 0;
    }
    esp_err_t e = i2c_master_start(cmd);
    if (e == ESP_OK)
    {
        e = i2c_addr_w(cmd, addr);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_write(cmd, buf, len, PROTO_TRUE);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_stop(cmd);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_cmd_begin(i2c_port_id(bus), cmd, pdMS_TO_TICKS(ms));
    }
    i2c_cmd_link_delete_static(cmd);
    return e == ESP_OK ? 1 : 0;
}

int pc_platform_i2c_read(uint8_t bus, uint16_t addr, uint8_t *buf, uint32_t len, uint32_t ms)
{
    if (bus >= PC_I2C_BUSES || buf == NULL || len == 0)
    {
        return 0;
    }
    EspI2cBus *b = &s_bus.i2c[bus];
    if (!b->up)
    {
        return 0;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create_static(b->link, sizeof(b->link));
    if (cmd == NULL)
    {
        return 0;
    }
    esp_err_t e = i2c_master_start(cmd);
    // A 10-bit read names the full address in the write direction first, then turns the bus around
    // and repeats the prefix byte with the read bit set.
    if (e == ESP_OK && (addr & PC_I2C_ADDR_10BIT) != 0)
    {
        e = i2c_addr_w(cmd, addr);
        if (e == ESP_OK)
        {
            e = i2c_master_start(cmd);
        }
    }
    if (e == ESP_OK)
    {
        e = i2c_addr_r(cmd, addr);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_stop(cmd);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_cmd_begin(i2c_port_id(bus), cmd, pdMS_TO_TICKS(ms));
    }
    i2c_cmd_link_delete_static(cmd);
    return e == ESP_OK ? 1 : 0;
}

int pc_platform_i2c_write_read(uint8_t bus, uint16_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen,
                               uint32_t ms)
{
    if (bus >= PC_I2C_BUSES || w == NULL || wlen == 0 || r == NULL || rlen == 0)
    {
        return 0;
    }
    EspI2cBus *b = &s_bus.i2c[bus];
    if (!b->up)
    {
        return 0;
    }
    // One transaction with a repeated start between the write and the read, which is what a
    // register read is: address the register, then turn the bus around without releasing it.
    i2c_cmd_handle_t cmd = i2c_cmd_link_create_static(b->link, sizeof(b->link));
    if (cmd == NULL)
    {
        return 0;
    }
    esp_err_t e = i2c_master_start(cmd);
    if (e == ESP_OK)
    {
        e = i2c_addr_w(cmd, addr);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_write(cmd, w, wlen, PROTO_TRUE);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_start(cmd);
    }
    if (e == ESP_OK)
    {
        e = i2c_addr_r(cmd, addr);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_read(cmd, r, rlen, I2C_MASTER_LAST_NACK);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_stop(cmd);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_cmd_begin(i2c_port_id(bus), cmd, pdMS_TO_TICKS(ms));
    }
    i2c_cmd_link_delete_static(cmd);
    return e == ESP_OK ? 1 : 0;
}

int pc_platform_i2c_probe(uint8_t bus, uint16_t addr, uint32_t ms)
{
    if (bus >= PC_I2C_BUSES)
    {
        return 0;
    }
    EspI2cBus *b = &s_bus.i2c[bus];
    if (!b->up)
    {
        return 0;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create_static(b->link, sizeof(b->link));
    if (cmd == NULL)
    {
        return 0;
    }
    esp_err_t e = i2c_master_start(cmd);
    if (e == ESP_OK)
    {
        e = i2c_addr_w(cmd, addr);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_stop(cmd);
    }
    if (e == ESP_OK)
    {
        e = i2c_master_cmd_begin(i2c_port_id(bus), cmd, pdMS_TO_TICKS(ms));
    }
    i2c_cmd_link_delete_static(cmd);
    return e == ESP_OK ? 1 : 0;
}

// Half a bit period at 100 kHz, the rate the recovery sequence is clocked at.
#define PC_I2C_RECOVER_HALF_US 5u

// Clocks the bus can be pulsed before the device is declared stuck: a device mid-byte releases
// SDA within one byte plus its ACK.
#define PC_I2C_RECOVER_CLOCKS 9u

int pc_platform_i2c_recover(uint8_t bus, int sda, int scl)
{
    if (bus >= PC_I2C_BUSES || sda < 0 || scl < 0)
    {
        return 0;
    }
    EspI2cBus *b = &s_bus.i2c[bus];
    uint32_t hz = b->up ? b->hz : 100000u;
    if (b->up)
    {
        i2c_driver_delete(i2c_port_id(bus));
        b->up = PROTO_FALSE;
    }
    // Drive SCL and let SDA float: a device holding the line low is the only thing pulling it down,
    // so it reads high the moment that device lets go.
    pc_platform_gpio_mode((uint8_t)scl, PC_GPIO_OUT);
    pc_platform_gpio_mode((uint8_t)sda, PC_GPIO_IN_PULLUP);
    pc_platform_gpio_write((uint8_t)scl, PC_GPIO_HIGH);
    esp_rom_delay_us(PC_I2C_RECOVER_HALF_US);
    for (uint32_t i = 0; i < PC_I2C_RECOVER_CLOCKS && pc_platform_gpio_read((uint8_t)sda) == 0; i++)
    {
        pc_platform_gpio_write((uint8_t)scl, PC_GPIO_LOW);
        esp_rom_delay_us(PC_I2C_RECOVER_HALF_US);
        pc_platform_gpio_write((uint8_t)scl, PC_GPIO_HIGH);
        esp_rom_delay_us(PC_I2C_RECOVER_HALF_US);
    }
    // A stop is SDA rising while SCL is high, which returns every device to its idle state.
    pc_platform_gpio_mode((uint8_t)sda, PC_GPIO_OUT);
    pc_platform_gpio_write((uint8_t)sda, PC_GPIO_LOW);
    esp_rom_delay_us(PC_I2C_RECOVER_HALF_US);
    pc_platform_gpio_write((uint8_t)scl, PC_GPIO_HIGH);
    esp_rom_delay_us(PC_I2C_RECOVER_HALF_US);
    pc_platform_gpio_mode((uint8_t)sda, PC_GPIO_IN_PULLUP);
    esp_rom_delay_us(PC_I2C_RECOVER_HALF_US);
    proto_bool freed = pc_platform_gpio_read((uint8_t)sda) != 0;
    return (pc_platform_i2c_begin(bus, sda, scl, hz) != 0 && freed) ? 1 : 0;
}

#endif // PC_VENDOR_ESP
