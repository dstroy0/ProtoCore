================================================================================
ESPRESSIF ESP32-C2/ESP8684 & ESP32-S: CRYPTOGRAPHIC REGISTER SPECIFICATION
================================================================================

---

1. ESP32-C2 / ESP8684 EXCLUSIVE: FLASH ENCRYPTION ACCELERATOR (Base: 0x6001C000)

---

The C2 lacks standard user-accessible AES, SHA, or RSA co-processors on the
peripheral bus. It uses a single hardware pipeline hooked strictly between the
external SPI flash and the internal instruction/data caches.

| Offset | Register Name    | R/W | Field/Bit Configuration [Hardware Details] |
| ------ | ---------------- | --- | ------------------------------------------ |
| 0x000  | XTS_AES_MODE_REG | R/W | Bit 0: 0=XTS-AES-128, 1=Bypass             |

       |                         |     | Bit 1: Key source (0=Internal eFuse, 1=Registers)

0x004 | XTS_AES_TRIGGER_REG | W | Bit 0: Pulse 1 to run manual key-refresh update cycle
0x008 | XTS_AES_STATUS_REG | R | Bit 0: 1=Memory lines encryption engine active, 0=Ready
0x00C | XTS_AES_ERR_ADDR_REG | R | [31:0] Captures absolute physical address of access violations

---

2. ESP32-S CORE ACCELERATOR: AES MODULE (Base Address: 0x3FF3B000)

---

The ESP32-S tracks the original Xtensa LX6 architecture exactly, forcing direct
CPU copy loops for all block transactions.

| Offset | Register Name      | R/W | Field/Bit Configuration [Details]                          |
| ------ | ------------------ | --- | ---------------------------------------------------------- |
| 0x000  | AES_KEY_0_REG      | W   | [31:0] Key Word 0 (8 sequential 32-bit registers to 0x01C) |
| 0x020  | AES_TEXT_IN_0_REG  | W   | [31:0] Plaintext Input Word 0 (4 registers up to 0x02C)    |
| 0x030  | AES_TEXT_OUT_0_REG | R   | [31:0] Ciphertext Output Word 0 (4 registers up to 0x03C)  |
| 0x040  | AES_MODE_REG       | R/W | Bit 0: 0=Encrypt, 1=Decrypt                                |

       |                         |     | Bits [2:1]: Key Size Selector (00=128, 01=192, 10=256-bit)

0x044 | AES_TRIGGER_REG | W | Bit 0: Strobe high (1) to execute block cipher transformation
0x048 | AES_STATE_REG | R | Bit 0: 1=Core actively processing, 0=Idle Complete

---

3. ESP32-S CORE ACCELERATOR: SHA CO-PROCESSOR (Base Address: 0x3FF3E000)

---

| Offset | Register Name         | R/W | Field/Bit Configuration [Details]                              |
| ------ | --------------------- | --- | -------------------------------------------------------------- |
| 0x000  | SHA_TEXT_IN_0..15_REG | W   | [31:0] Message input window (16 sequential registers to 0x03C) |
| 0x040  | SHA_MODE_REG          | R/W | [2:0]: 0=SHA-1, 1=SHA-256, 2=SHA-384, 3=SHA-512                |
| 0x044  | SHA_START_REG         | W   | Bit 0: Pulse 1 to initialize and calculate the first block     |
| 0x048  | SHA_CONTI_REG         | W   | Bit 0: Pulse 1 to run subsequent sequential data blocks        |
| 0x04C  | SHA_BUSY_REG          | R   | Bit 0: 1=Hash pipeline running, 0=Block calculation complete   |

---

4. ESP32-S CORE ACCELERATOR: RSA / MPI ACCELERATOR (Base Address: 0x3FF3C000)

---

| Offset | Register Name           | R/W | Field/Bit Configuration [Details]                                    |
| ------ | ----------------------- | --- | -------------------------------------------------------------------- |
| 0x000  | RSA_MEM_M_BLOCK_BASE    | R/W | Modulus M (512 bytes / 4096-bit limit)                               |
| 0x200  | RSA_MEM_Z_BLOCK_BASE    | R/W | Result Z, also RSA_MEM_RB_BLOCK_BASE (512 bytes)                     |
| 0x400  | RSA_MEM_Y_BLOCK_BASE    | R/W | Operand Y: exponent for MODEXP, multiplier for MULT (512 bytes)      |
| 0x600  | RSA_MEM_X_BLOCK_BASE    | R/W | Operand X: base / multiplicand (512 bytes)                           |
| 0x800  | RSA_M_DASH_REG          | R/W | Montgomery m' = -M^-1 mod 2^32                                       |
| 0x804  | RSA_MODEXP_MODE_REG     | R/W | Operand length in words, minus 1, for MODEXP                         |
| 0x808  | RSA_MODEXP_START_REG    | W   | Write 1: start Montgomery modular exponentiation                     |
| 0x80C  | RSA_MULT_MODE_REG       | R/W | Operand length selector for MULT                                     |
| 0x810  | RSA_MULT_START_REG      | W   | Write 1: start plain large-integer multiplication                    |
| 0x814  | RSA_CLEAR_INTERRUPT_REG | R/W | Write 1 clears the completion flag; reads it as RSA_QUERY_INTERRUPT  |
| 0x818  | RSA_QUERY_CLEAN_REG     | R   | [0]: 0 = memory init NOT complete, 1 = complete. Spin while it is 0. |

> Verified against the vendor header `soc/esp32/include/soc/hwcrypto_reg.h`. This die's control
> registers are NOT at the same offsets as the S3/C6 generation, which is why
> `core_setup/hal/esp/esp_crypto_hal.h` refuses it with an `#error` rather than reusing its map.
