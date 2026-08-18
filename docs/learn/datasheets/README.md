# Cached part datasheets

Register maps, field widths and conversion formulas the peripheral suites assert against.
Fetched from the vendor; the .txt beside each .pdf is the greppable form, made with
`pdftotext -layout`. A table whose columns the extraction scrambles is read from the .pdf instead.

- **ad9238** (cached) - ADI AD9238 dual ADC: output coding, data format. The register map is in
  AN-877, cached beside it as `an877.pdf`
  <https://www.analog.com/media/en/technical-documentation/data-sheets/AD9238.pdf>
- **ads1115** (cached) - TI ADS1115 16-bit ADC: config register bits, PGA FSR, data rate
  <https://www.ti.com/lit/ds/symlink/ads1115.pdf>
- **fdc2214** (cached) - TI FDC2214 capacitance-to-digital: register map, DATA/CLOCK_DIVIDERS
  <https://www.ti.com/lit/ds/symlink/fdc2214.pdf>
- **ina219** (cached) - TI INA219: bus/shunt voltage register layout, LSB weights, CNVR/OVF flags
  <https://www.ti.com/lit/ds/symlink/ina219.pdf>
- **ldc1614** (cached) - TI LDC1614 inductance-to-digital: register map, conversion result format
  <https://www.ti.com/lit/ds/symlink/ldc1614.pdf>
- **mpr121** (cached) - NXP MPR121 touch: electrode filtering, touch/release thresholds, register map
  <https://www.nxp.com/docs/en/data-sheet/MPR121.pdf>
- **pca9685** (cached) - NXP PCA9685 PWM: MODE1/MODE2, PRE_SCALE, LEDn ON/OFF registers
  <https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf>
- **pmbus** (NOT CACHED) - PMBus part II: command codes, LINEAR11/LINEAR16 data formats
  <https://pmbus.org/Assets/PDFS/Public/PMBus_Specification_Part_II_Rev_1-1_20070205.pdf>
- **pn532** (cached) - NXP PN532 NFC: frame format, command set, checksum rules
  <https://www.nxp.com/docs/en/user-guide/141520.pdf>
- **sht3x** (cached) - Sensirion SHT3x: measurement commands, CRC-8 polynomial, RH/T conversion formulas
  <https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf>
- **vl53l0x** (cached) - ST VL53L0X ToF, DS11555: I2C device address 0x52 (8-bit, so 0x29 on a
  7-bit API), Table 5 reference registers, multibyte order
  <https://www.st.com/resource/en/datasheet/vl53l0x.pdf>
  The ranging registers are NOT in the datasheet. Two further sources carry them:
    - `vl53l0x_um2039.pdf` (cached) - the API user manual: Table 1 is the API-level RangeStatus,
      where 0 is Range Valid. This is a different scale from the raw DeviceRangeStatus in the
      register, and confusing the two inverts the validity test
      <https://www.st.com/resource/en/user_manual/um2039-...-stmicroelectronics.pdf>
    - ST's API header `vl53l0x_device.h` (not cached, it is source not a document) - the register
      addresses and `VL53L0X_DEVICEERROR_RANGECOMPLETE = 11`, which is the raw DeviceRangeStatus
      value that means a completed measurement
      <https://github.com/stm32duino/VL53L0X/blob/main/src/vl53l0x_device.h>
