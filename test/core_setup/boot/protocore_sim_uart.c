// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_sim_uart.c
 * @brief The one output an image booted under QEMU has: bytes into UART0's transmit FIFO.
 *
 * An image loaded with -kernel runs before any driver exists, so the only way it can say anything
 * is the register the ROM already left usable. Writing a byte to the FIFO register queues it for
 * transmission, and QEMU forwards it to whatever -serial names.
 *
 * The address is the same on both parts this builds for: DR_REG_UART_BASE is 0x60000000 on the
 * ESP32-C3 and on the ESP32-S3 (soc/reg_base.h in each), and UART_FIFO_REG(0) is that base plus 0
 * (soc/uart_reg.h). No baud rate is programmed: the ROM configures UART0 before it hands over, and
 * QEMU's model accepts a write whatever the divisor says.
 */

#include <stdint.h>

#define PROTOCORE_SIM_UART0_FIFO 0x60000000u

/** @brief Queue one byte for transmission on UART0. */
void protocore_sim_putc(char c)
{
    *(volatile uint32_t *)PROTOCORE_SIM_UART0_FIFO = (uint32_t)(unsigned char)c;
}

/** @brief Queue @p s up to its NUL, each newline preceded by a carriage return. */
void protocore_sim_puts(const char *s)
{
    if (!s)
    {
        return;
    }
    for (uint32_t i = 0; s[i] != '\0'; ++i)
    {
        if (s[i] == '\n')
        {
            protocore_sim_putc('\r');
        }
        protocore_sim_putc(s[i]);
    }
}

/** @brief Queue @p v as eight hex digits, most significant first. */
void protocore_sim_puthex(uint32_t v)
{
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        const uint32_t nib = (v >> shift) & 0xFu;
        protocore_sim_putc((char)(nib < 10 ? '0' + nib : 'a' + (nib - 10)));
    }
}
