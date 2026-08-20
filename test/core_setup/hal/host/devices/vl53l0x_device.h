// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host device model: ST VL53L0X optical time-of-flight ranging sensor.
//
// This part takes three sources, because no one of them carries the whole interface:
//
//   DS11555 Rev 6, cached at docs/learn/datasheets/vl53l0x.pdf
//     4.1     device address 0x52 - the 8-bit form with the R/W bit in it, so 0x29 on a 7-bit API
//     4.2     Table 5 reference registers, after a fresh reset and without the API loaded:
//             0xC0 = 0xEE, 0xC1 = 0xAA, 0xC2 = 0x10, 0x51 = 0x0099, 0x61 = 0x0000
//     4.2     Table 6: a multibyte read is ascending addresses, MSB first
//
//   UM2039, cached at docs/learn/datasheets/vl53l0x_um2039.pdf
//     Table 1 is the API-level RangeStatus, where 0 is Range Valid. That is a DIFFERENT scale from
//     the raw DeviceRangeStatus in the register, and reading one for the other inverts the test.
//
//   ST's API header vl53l0x_device.h, which is where the ranging registers are defined at all:
//     VL53L0X_REG_SYSRANGE_START 0x000, with MODE_START_STOP 0x01 and MODE_BACKTOBACK 0x02
//     VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR 0x00B
//     VL53L0X_REG_RESULT_INTERRUPT_STATUS 0x013
//     VL53L0X_REG_RESULT_RANGE_STATUS 0x014
//     VL53L0X_REG_IDENTIFICATION_MODEL_ID 0x0C0
//     VL53L0X_DEVICEERROR_RANGECOMPLETE 11 - the raw DeviceRangeStatus that means a completed
//     measurement, which is what the register's bits 6:3 hold
//   The 0x07 interrupt mask and the range at RESULT_RANGE_STATUS + 10/11 come from the same API's
//   VL53L0X_GetInterruptMaskStatus and VL53L0X_GetRangingMeasurementData.
//
// The interrupt handshake is what this model is for. The part raises a data-ready interrupt, the
// host reads the result block, and the host must then clear the interrupt or the next reading is
// the same one. A driver that never clears still reads a plausible distance forever, and only a
// model that latches one measurement per interrupt shows it.
//
// test/ style: plain host C, like the rest of the host HAL.

#ifndef PROTOCORE_HOST_DEVICE_VL53L0X_H
#define PROTOCORE_HOST_DEVICE_VL53L0X_H

#include "protocore_net_host.h" // protocore_bus_host_attach

#include <stdint.h>

// ST's API header: the addresses this model acts on.
#define PROTOCORE_VL53L0X_DEV_SYSRANGE_START 0x00u
#define PROTOCORE_VL53L0X_DEV_SYSTEM_INTERRUPT_CLEAR 0x0Bu
#define PROTOCORE_VL53L0X_DEV_RESULT_INTERRUPT_STATUS 0x13u
#define PROTOCORE_VL53L0X_DEV_RESULT_RANGE_STATUS 0x14u
#define PROTOCORE_VL53L0X_DEV_IDENTIFICATION_MODEL_ID 0xC0u

// DS11555 Table 5: the reference registers, after a fresh reset.
#define PROTOCORE_VL53L0X_DEV_MODEL_ID 0xEEu
#define PROTOCORE_VL53L0X_DEV_C1 0xAAu
#define PROTOCORE_VL53L0X_DEV_C2 0x10u

// ST's API header: SYSRANGE_START bit 1 selects back-to-back, which is what starts continuous
// ranging; VL53L0X_DEVICEERROR_RANGECOMPLETE is the DeviceRangeStatus for a completed measurement.
#define PROTOCORE_VL53L0X_DEV_BACKTOBACK 0x02u
#define PROTOCORE_VL53L0X_DEV_RANGECOMPLETE 11u

typedef struct
{
    uint16_t range_mm;    /**< the distance the part is measuring, in millimetres */
    uint8_t range_status; /**< the raw DeviceRangeStatus, 11 for a completed measurement */
    uint8_t reg[256];     /**< the register file, addressed as the part addresses it */
    uint8_t ptr;          /**< the register the last transfer named */
    uint8_t ranging;      /**< SYSRANGE_START put it in continuous back-to-back mode */
} protocore_vl53l0x_dev;

/** @brief Power-up state: the DS11555 Table 5 reference registers, not ranging, nothing measured. */
static inline void protocore_vl53l0x_dev_init(protocore_vl53l0x_dev *d)
{
    d->range_mm = 0;
    d->range_status = PROTOCORE_VL53L0X_DEV_RANGECOMPLETE;
    for (uint32_t i = 0; i < 256u; i++)
    {
        d->reg[i] = 0;
    }
    d->reg[PROTOCORE_VL53L0X_DEV_IDENTIFICATION_MODEL_ID] = PROTOCORE_VL53L0X_DEV_MODEL_ID;
    d->reg[0xC1u] = PROTOCORE_VL53L0X_DEV_C1;
    d->reg[0xC2u] = PROTOCORE_VL53L0X_DEV_C2;
    d->ptr = 0;
    d->ranging = 0;
}

/** @brief Raise a data-ready interrupt with one measurement behind it, as a completed range does. */
static inline void protocore_vl53l0x_dev_measure(protocore_vl53l0x_dev *d, uint16_t mm)
{
    d->range_mm = mm;
    if (!d->ranging)
    {
        return; // nothing converts until SYSRANGE_START says so
    }
    d->reg[PROTOCORE_VL53L0X_DEV_RESULT_INTERRUPT_STATUS] = 0x04u; // the new-measure-ready bit
    // The twelve result registers from RESULT_RANGE_STATUS: the status is bits 6:3 of the first,
    // and the distance is the pair at +10 and +11, MSB first (DS11555 Table 6).
    d->reg[PROTOCORE_VL53L0X_DEV_RESULT_RANGE_STATUS] = (uint8_t)((d->range_status & 0x0Fu) << 3);
    d->reg[PROTOCORE_VL53L0X_DEV_RESULT_RANGE_STATUS + 10u] = (uint8_t)(mm >> 8);
    d->reg[PROTOCORE_VL53L0X_DEV_RESULT_RANGE_STATUS + 11u] = (uint8_t)(mm & 0xFFu);
}

// One byte into the register the pointer names.
static inline void protocore_vl53l0x_dev_store(protocore_vl53l0x_dev *d, uint8_t v)
{
    if (d->ptr == PROTOCORE_VL53L0X_DEV_SYSRANGE_START)
    {
        d->ranging = (v & PROTOCORE_VL53L0X_DEV_BACKTOBACK) ? 1u : 0u;
    }
    if (d->ptr == PROTOCORE_VL53L0X_DEV_SYSTEM_INTERRUPT_CLEAR && (v & 0x01u))
    {
        // The interrupt is acknowledged, so the reading behind it is consumed. Until the next
        // measurement, there is nothing new to report.
        d->reg[PROTOCORE_VL53L0X_DEV_RESULT_INTERRUPT_STATUS] = 0;
        return;
    }
    d->reg[d->ptr] = v;
}

// One transfer. The first byte written is the register index; the bytes after it land there. A read
// returns from that register onward, ascending, which is how the twelve result registers come back
// in one go (DS11555 Table 6).
static inline uint32_t protocore_vl53l0x_dev_txn(void *ctx, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen)
{
    protocore_vl53l0x_dev *d = (protocore_vl53l0x_dev *)ctx;
    if (w && wlen)
    {
        d->ptr = w[0];
        for (uint32_t i = 1; i < wlen; i++)
        {
            protocore_vl53l0x_dev_store(d, w[i]);
            d->ptr++;
        }
    }
    uint32_t n = 0;
    while (n < rlen)
    {
        r[n++] = d->reg[d->ptr++];
    }
    return n;
}

/** @brief Reset the model and put it on the I2C bus at one address. 0 when the table is full. */
static inline int protocore_vl53l0x_dev_place(protocore_vl53l0x_dev *d, uint16_t addr)
{
    protocore_vl53l0x_dev_init(d);
    return protocore_bus_host_attach(PROTOCORE_BUS_HOST_I2C, addr, d, protocore_vl53l0x_dev_txn);
}

#endif // PROTOCORE_HOST_DEVICE_VL53L0X_H
