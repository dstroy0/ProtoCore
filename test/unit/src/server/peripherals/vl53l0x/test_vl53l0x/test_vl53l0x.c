// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the VL53L0X time-of-flight ranging codec (server/peripherals/vl53l0x/vl53l0x.h).
//
// The register semantics are ST's, not this library's. ST's VL53L0X API extracts the device range
// status as `DeviceRangeStatusInternal = ((DeviceRangeStatus & 0x78) >> 3)` and treats the single
// value 11 as a good ranging - every other code is a hardware, phase, signal or min-range failure.
// test_range_status_is_bits_6_to_3 is therefore the load-bearing case: it walks all sixteen codes
// of that field and asserts 11 and only 11 reads as valid, so a mask or shift that is off by one
// bit cannot pass. The 16-bit distance at RESULT_RANGE_STATUS + 10 is read high octet first, which
// ST's own driver spells `readReg16Bit(RESULT_RANGE_STATUS + 10)`.

// The bus cases at the bottom drive a datasheet model of the part (test/core_setup/hal/host/devices/
// vl53l0x_device.h): a suite puts a distance in front of the sensor, the driver polls data-ready,
// reads the result block and clears the interrupt, and the millimetres come back through its own
// decode. A driver that never clears the interrupt reads a plausible distance forever, which only
// a model that latches one measurement per interrupt catches.

#include "server/peripherals/vl53l0x/vl53l0x.h"

#include "devices/vl53l0x_device.h"

#include <unity.h>

static protocore_vl53l0x_dev s_part;

void setUp(void)
{
    protocore_bus_host_reset();
    protocore_bus_host_detach_all();
    protocore_vl53l0x_dev_place(&s_part, (uint16_t)PROTOCORE_VL53L0X_I2C_ADDR);
}
void tearDown(void)
{
    protocore_bus_host_detach_all();
}

// The distance is the register pair at RESULT_RANGE_STATUS + 10 / + 11, high octet first.
void test_range_is_the_big_endian_register_pair(void)
{
    Vl53l0x.range_mm_args.hi = 0x00;
    Vl53l0x.range_mm_args.lo = 0x00;
    Vl53l0x.range_mm(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT16(0u, Vl53l0x.mm);
    // 0x04D2 = 4*256 + 13*16 + 2 = 1024 + 208 + 2 = 1234 mm
    Vl53l0x.range_mm_args.hi = 0x04;
    Vl53l0x.range_mm_args.lo = 0xD2;
    Vl53l0x.range_mm(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT16(1234u, Vl53l0x.mm);
    // the two octets are not interchangeable: swapping them names a different distance
    Vl53l0x.range_mm_args.hi = 0xD2;
    Vl53l0x.range_mm_args.lo = 0x04;
    Vl53l0x.range_mm(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT16(0xD204u, Vl53l0x.mm);
    // 8190 mm is the out-of-range value the part reports when nothing is in front of it
    Vl53l0x.range_mm_args.hi = 0x1F;
    Vl53l0x.range_mm_args.lo = 0xFE;
    Vl53l0x.range_mm(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT16(8190u, Vl53l0x.mm);
    Vl53l0x.range_mm_args.hi = 0xFF;
    Vl53l0x.range_mm_args.lo = 0xFF;
    Vl53l0x.range_mm(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT16(65535u, Vl53l0x.mm);
}

// Every high octet lands in bits 15:8 and every low octet in bits 7:0, over the whole range.
void test_range_octets_never_overlap(void)
{
    for (unsigned hi = 0; hi < 256u; hi += 17u)
    {
        for (unsigned lo = 0; lo < 256u; lo += 19u)
        {
            Vl53l0x.range_mm_args.hi = (uint8_t)hi;
            Vl53l0x.range_mm_args.lo = (uint8_t)lo;
            Vl53l0x.range_mm(protocore_vl53l0x_span());
            uint16_t mm = Vl53l0x.mm;
            TEST_ASSERT_EQUAL_UINT8((uint8_t)hi, (uint8_t)(mm >> 8));
            TEST_ASSERT_EQUAL_UINT8((uint8_t)lo, (uint8_t)(mm & 0xFFu));
        }
    }
}

// ST's driver polls RESULT_INTERRUPT_STATUS until any of its low three bits is set; the bits above
// them are the interrupt configuration, not a data-ready report.
void test_data_ready_is_the_low_three_interrupt_bits(void)
{
    Vl53l0x.data_ready_args.interrupt_status = 0x00;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    Vl53l0x.data_ready_args.interrupt_status = 0x01;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    Vl53l0x.data_ready_args.interrupt_status = 0x02;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    Vl53l0x.data_ready_args.interrupt_status = 0x04;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    Vl53l0x.data_ready_args.interrupt_status = 0x07;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    // bits 3 and up on their own are not a data-ready report
    Vl53l0x.data_ready_args.interrupt_status = 0x08;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    Vl53l0x.data_ready_args.interrupt_status = 0x10;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    Vl53l0x.data_ready_args.interrupt_status = 0xF8;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    // a set low bit is still a report when the bits above it are set too
    Vl53l0x.data_ready_args.interrupt_status = 0xFF;
    Vl53l0x.data_ready(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
}

// ST: DeviceRangeStatusInternal = (RESULT_RANGE_STATUS & 0x78) >> 3, and 11 is the good ranging.
void test_range_status_is_bits_6_to_3(void)
{
    TEST_ASSERT_EQUAL_UINT8(11u, (uint8_t)VL53L0X_RANGE_VALID);

    for (unsigned code = 0; code < 16u; code++)
    {
        // the field sits in bits 6:3; bits 7, 2, 1 and 0 belong to other fields and must not leak in
        uint8_t reg = (uint8_t)((code << 3) | 0x87u);
        Vl53l0x.range_status_args.range_status_reg = reg;
        Vl53l0x.range_status(protocore_vl53l0x_span());
        TEST_ASSERT_EQUAL_UINT8((uint8_t)code, Vl53l0x.status);
        if (code == VL53L0X_RANGE_VALID)
        {
            Vl53l0x.range_valid_args.range_status_reg = reg;
            Vl53l0x.range_valid(protocore_vl53l0x_span());
            TEST_ASSERT_TRUE(Vl53l0x.ok);
        }
        else
        {
            Vl53l0x.range_valid_args.range_status_reg = reg;
            Vl53l0x.range_valid(protocore_vl53l0x_span());
            TEST_ASSERT_FALSE(Vl53l0x.ok);
        }
    }
}

// The named ST failure codes, spelled as the register octet a part would report.
void test_named_status_codes(void)
{
    Vl53l0x.range_status_args.range_status_reg = 11u << 3;
    Vl53l0x.range_status(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT8(11u, Vl53l0x.status); // good ranging
    Vl53l0x.range_valid_args.range_status_reg = 11u << 3;
    Vl53l0x.range_valid(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    Vl53l0x.range_status_args.range_status_reg = 1u << 3;
    Vl53l0x.range_status(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT8(1u, Vl53l0x.status); // VCSEL continuity fail
    Vl53l0x.range_valid_args.range_status_reg = 1u << 3;
    Vl53l0x.range_valid(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    Vl53l0x.range_status_args.range_status_reg = 4u << 3;
    Vl53l0x.range_status(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT8(4u, Vl53l0x.status); // signal fail
    Vl53l0x.range_valid_args.range_status_reg = 4u << 3;
    Vl53l0x.range_valid(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    Vl53l0x.range_status_args.range_status_reg = 6u << 3;
    Vl53l0x.range_status(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT8(6u, Vl53l0x.status); // phase fail
    Vl53l0x.range_valid_args.range_status_reg = 6u << 3;
    Vl53l0x.range_valid(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    Vl53l0x.range_status_args.range_status_reg = 8u << 3;
    Vl53l0x.range_status(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_UINT8(8u, Vl53l0x.status); // min range fail
    Vl53l0x.range_valid_args.range_status_reg = 8u << 3;
    Vl53l0x.range_valid(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    // a zero register is not a valid measurement, which is the fail-closed case that matters: a
    // part that never answered leaves the buffer at zero
    Vl53l0x.range_valid_args.range_status_reg = 0x00;
    Vl53l0x.range_valid(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
}

// The documented register addresses and the identification value, from the part's register map.
void test_register_map(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, VL53L0X_REG_SYSRANGE_START);
    TEST_ASSERT_EQUAL_HEX8(0x0B, VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR);
    TEST_ASSERT_EQUAL_HEX8(0x13, VL53L0X_REG_RESULT_INTERRUPT_STATUS);
    TEST_ASSERT_EQUAL_HEX8(0x14, VL53L0X_REG_RESULT_RANGE_STATUS);
    TEST_ASSERT_EQUAL_HEX8(0xC0, VL53L0X_REG_IDENTIFICATION_MODEL_ID);
    TEST_ASSERT_EQUAL_HEX8(0xEE, VL53L0X_MODEL_ID);
}

// begin() reads IDENTIFICATION_MODEL_ID first and refuses anything that is not 0xEE, so a wrong
// part on the bus does not get ranging commands written into it.
void test_begin_refuses_a_wrong_model_id(void)
{
    s_part.reg[VL53L0X_REG_IDENTIFICATION_MODEL_ID] = 0xAA;
    Vl53l0x.begin_args.addr = 0x29;
    Vl53l0x.begin(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);

    // the only octet that went out is the model-id register address, no SYSRANGE_START behind it
    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(1u, n);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_IDENTIFICATION_MODEL_ID, tx[0]);
}

// With the right model id, begin arms continuous back-to-back ranging: SYSRANGE_START = 0x02.
void test_begin_arms_continuous_ranging(void)
{
    Vl53l0x.begin_args.addr = 0x29;
    Vl53l0x.begin(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(3u, n);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_IDENTIFICATION_MODEL_ID, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_SYSRANGE_START, tx[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, tx[2]);
}

// A read with no measurement pending answers false and reads no result registers.
void test_read_refuses_when_no_measurement_is_ready(void)
{
    Vl53l0x.begin_args.addr = 0x29;
    Vl53l0x.begin(protocore_vl53l0x_span());
    protocore_bus_host_reset(); // the capture below is about the read, not the bring-up
    uint16_t mm = 0xFFFF;
    Vl53l0x.read_mm_args.mm = &mm;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, mm); // untouched

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(1u, n);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_RESULT_INTERRUPT_STATUS, tx[0]);
}

// A ready, valid measurement: the twelve result registers from RESULT_RANGE_STATUS, status 11 in
// bits 6:3 of the first, distance in the eleventh and twelfth. The interrupt is cleared after.
void test_read_takes_the_distance_from_offset_ten(void)
{
    Vl53l0x.begin_args.addr = 0x29;
    Vl53l0x.begin(protocore_vl53l0x_span());
    s_part.range_status = 11u; // DeviceRangeStatus 11: the measurement completed
    protocore_vl53l0x_dev_measure(&s_part, 1234u);
    protocore_bus_host_reset(); // the capture below is about the read, not the bring-up

    uint16_t mm = 0;
    Vl53l0x.read_mm_args.mm = &mm;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT16(1234u, mm);

    uint32_t n = 0;
    const uint8_t *tx = protocore_bus_host_written(&n);
    TEST_ASSERT_EQUAL_UINT32(4u, n);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_RESULT_INTERRUPT_STATUS, tx[0]);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_RESULT_RANGE_STATUS, tx[1]);
    TEST_ASSERT_EQUAL_HEX8(VL53L0X_REG_SYSTEM_INTERRUPT_CLEAR, tx[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx[3]);
}

// A ready measurement whose status is not 11 answers false: the reading exists but the part says it
// is not trustworthy, and a caller that ignores that publishes a wrong distance.
void test_read_refuses_an_invalid_status(void)
{
    Vl53l0x.begin_args.addr = 0x29;
    Vl53l0x.begin(protocore_vl53l0x_span());
    s_part.range_status = 4u; // DeviceRangeStatus 4: signal fail
    protocore_vl53l0x_dev_measure(&s_part, 1234u);

    uint16_t mm = 0;
    Vl53l0x.read_mm_args.mm = &mm;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
}

// A null destination is refused rather than written through.
void test_read_refuses_a_null_destination(void)
{
    Vl53l0x.read_mm_args.mm = NULL;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
}

// ---------------------------------------------------------------------------
// Over the bus, against the device model
// ---------------------------------------------------------------------------

// DS11555 Table 5 publishes the reference registers after a fresh reset: 0xC0 = 0xEE, 0xC1 = 0xAA,
// 0xC2 = 0x10. Asserted through the platform seam, because every case below reads registers back
// and a model that started elsewhere would agree with a wrong driver.
void test_ds11555_model_reference_registers(void)
{
    uint8_t reg = 0xC0u;
    uint8_t r[3] = {0u, 0u, 0u};
    TEST_ASSERT_TRUE(protocore_platform_i2c_write_read(0u, (uint16_t)PROTOCORE_VL53L0X_I2C_ADDR, &reg, 1u, r, 3u, 10u));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, r[0]); // IDENTIFICATION_MODEL_ID
    TEST_ASSERT_EQUAL_HEX8(0xAAu, r[1]); // and the two beside it, read in ascending order
    TEST_ASSERT_EQUAL_HEX8(0x10u, r[2]);
}

// begin() reads the model ID first and refuses anything that is not a VL53L0X, so a part that
// answers with something else is never put into ranging.
void test_begin_refuses_a_part_that_is_not_a_vl53l0x(void)
{
    s_part.reg[0xC0u] = 0x12u;
    Vl53l0x.begin_args.addr = (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
    Vl53l0x.begin(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT8(0u, s_part.ranging);
}

// ST's API defines SYSRANGE_START bit 1 as back-to-back mode, which is what starts continuous
// ranging. begin() writes it, so the part is left converting.
void test_begin_starts_continuous_back_to_back_ranging(void)
{
    Vl53l0x.begin_args.addr = (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
    Vl53l0x.begin(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_HEX8(0x02u, s_part.reg[0x00u]);
    TEST_ASSERT_EQUAL_UINT8(1u, s_part.ranging);
}

// A distance in front of the sensor comes back as millimetres: the driver polls the interrupt,
// reads the twelve result registers, and takes the pair at +10 / +11 high octet first.
void test_a_distance_in_front_of_the_sensor_reads_back(void)
{
    Vl53l0x.begin_args.addr = (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
    Vl53l0x.begin(protocore_vl53l0x_span());

    static const uint16_t APPLIED[5] = {0u, 30u, 255u, 1000u, 2000u};
    for (uint32_t i = 0; i < 5u; i++)
    {
        protocore_vl53l0x_dev_measure(&s_part, APPLIED[i]);
        uint16_t mm = 0xFFFFu;
        Vl53l0x.read_mm_args.mm = &mm;
        Vl53l0x.read_mm(protocore_vl53l0x_span());
        TEST_ASSERT_TRUE(Vl53l0x.ok);
        TEST_ASSERT_EQUAL_UINT16(APPLIED[i], mm);
    }
}

// ST's API treats DeviceRangeStatus 11 (VL53L0X_DEVICEERROR_RANGECOMPLETE) as the completed
// measurement and every other code as a failure. The distance still comes back - the driver reports
// it either way - but the reading says whether to believe it.
void test_only_rangecomplete_reports_a_valid_reading(void)
{
    Vl53l0x.begin_args.addr = (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
    Vl53l0x.begin(protocore_vl53l0x_span());

    for (uint8_t status = 0u; status < 16u; status++)
    {
        s_part.range_status = status;
        protocore_vl53l0x_dev_measure(&s_part, 1234u);
        uint16_t mm = 0u;
        Vl53l0x.read_mm_args.mm = &mm;
        Vl53l0x.read_mm(protocore_vl53l0x_span());
        if (status == 11u)
        {
            TEST_ASSERT_TRUE(Vl53l0x.ok);
        }
        else
        {
            TEST_ASSERT_FALSE(Vl53l0x.ok);
        }
        TEST_ASSERT_EQUAL_UINT16(1234u, mm); // the distance is reported either way
    }
}

// The driver clears the interrupt after reading, so the next poll finds no data ready rather than
// handing back the same measurement again. A driver that skipped the clear would read forever.
void test_the_interrupt_is_cleared_so_a_reading_is_not_served_twice(void)
{
    Vl53l0x.begin_args.addr = (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
    Vl53l0x.begin(protocore_vl53l0x_span());

    protocore_vl53l0x_dev_measure(&s_part, 500u);
    uint16_t mm = 0u;
    Vl53l0x.read_mm_args.mm = &mm;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT16(500u, mm);
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_part.reg[0x13u]); // the interrupt was acknowledged

    // nothing new has been measured, so the next read finds nothing ready
    mm = 0xFFFFu;
    Vl53l0x.read_mm_args.mm = &mm;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, mm); // and it left the caller's millimetres alone

    // a fresh measurement raises it again
    protocore_vl53l0x_dev_measure(&s_part, 600u);
    Vl53l0x.read_mm_args.mm = &mm;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT16(600u, mm);
}

// Nothing converts until SYSRANGE_START says so, so a distance present before begin() reports no
// data ready rather than a reading.
void test_nothing_is_measured_before_ranging_starts(void)
{
    protocore_vl53l0x_dev_measure(&s_part, 500u);
    uint16_t mm = 0xFFFFu;
    Vl53l0x.read_mm_args.mm = &mm;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, mm);
}

// begin() sends later transfers to the address it was given, and back to the DS11555 default when
// handed zero - so the address is state and not a constant.
void test_begin_sends_later_transfers_to_the_address_it_was_given(void)
{
    protocore_bus_host_detach_all();
    protocore_vl53l0x_dev_place(&s_part, 0x30u);
    Vl53l0x.begin_args.addr = 0x30u;
    Vl53l0x.begin(protocore_vl53l0x_span());
    TEST_ASSERT_TRUE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_HEX16(0x30u, protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
    Vl53l0x.begin_args.addr = 0u;
    Vl53l0x.begin(protocore_vl53l0x_span());
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_VL53L0X_I2C_ADDR,
                            protocore_bus_host_log[protocore_bus_host_log_len - 1u].target);
}

// A refused transfer is reported rather than passed off as a started sensor.
void test_a_refused_transfer_fails_begin(void)
{
    protocore_bus_host_fail = 1u;
    Vl53l0x.begin_args.addr = (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
    Vl53l0x.begin(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT8(0u, s_part.ranging);
}

// A refused result-block read is reported, and leaves the caller's millimetres alone.
void test_a_refused_result_read_fails_the_reading(void)
{
    Vl53l0x.begin_args.addr = (uint8_t)PROTOCORE_VL53L0X_I2C_ADDR;
    Vl53l0x.begin(protocore_vl53l0x_span());
    protocore_vl53l0x_dev_measure(&s_part, 500u);
    protocore_bus_host_fail = 2u; // let the interrupt poll through, refuse the block read after it
    uint16_t mm = 4321u;
    Vl53l0x.read_mm_args.mm = &mm;
    Vl53l0x.read_mm(protocore_vl53l0x_span());
    TEST_ASSERT_FALSE(Vl53l0x.ok);
    TEST_ASSERT_EQUAL_UINT16(4321u, mm);
}
