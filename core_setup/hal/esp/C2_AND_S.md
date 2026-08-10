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

| Offset                                                                           | Register Name  | R/W | Field/Bit Configuration [Details]                              |
| -------------------------------------------------------------------------------- | -------------- | --- | -------------------------------------------------------------- |
| 0x000                                                                            | RSA_M_K_REG    | R/W | [31:0] Montgomery Constant M' (M' = -1/M mod 2^32)             |
| 0x004                                                                            | RSA_LENGTH_REG | R/W | [5:0]: Array size bound register: ((Bit_Width / 32) - 1)       |
| 0x008                                                                            | RSA_V_CMD_REG  | R/W | [1:0]: 0x0=ModExp, 0x1=ModMult, 0x2=Large-Int Mult             |
| 0x00C                                                                            | RSA_READY_REG  | R   | Bit 0: 1=Pipeline ready / Engine idle, 0=Computing             |
| 0x100                                                                            | RSA_MEM_X_BASE | R/W | Multiplicand Block Memory Array X (512 bytes / 4096-bit limit) |
| 0x300                                                                            | RSA_MEM_Y_BASE | R/W | Exponent / Multiplier Block Memory Array Y (512 bytes)         |
| 0xC00                                                                            | RSA_MEM_M_BASE | R/W | Modulus Block Memory Array M (512 bytes)                       |
| 0xE00                                                                            | RSA_MEM_Z_BASE | R/W | Result / Intermediate Output Block Memory Array Z (512 bytes)  |
| ================================================================================ |
