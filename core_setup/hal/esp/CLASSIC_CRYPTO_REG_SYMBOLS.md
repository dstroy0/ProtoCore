================================================================================
ESPRESSIF CLASSIC ESP32: CRYPTOGRAPHIC ACCELERATOR REGISTRAR SPECIFICATION
================================================================================

---

1. AES CO-PROCESSOR REGISTERS (Base Address: 0x3FF3B000)

---

The classic ESP32 hardware AES engine operates on a state machine block
with static text/key register windows.

| Offset | Register Name      | R/W | Field/Bit Configuration [Details]                                |
| ------ | ------------------ | --- | ---------------------------------------------------------------- |
| 0x000  | AES_KEY_0_REG      | W   | [31:0] Key Word 0 (Total 8 sequential 32-bit registers to 0x01C) |
| 0x020  | AES_TEXT_IN_0_REG  | W   | [31:0] Plaintext Input Word 0 (Total 4 registers to 0x02C)       |
| 0x030  | AES_TEXT_OUT_0_REG | R   | [31:0] Ciphertext Output Word 0 (Total 4 registers to 0x03C)     |
| 0x040  | AES_MODE_REG       | R/W | [0]: 0=Encrypt, 1=Decrypt                                        |

       |                       |     | [2:1]: Key Length Select (00=128-bit, 01=192-bit, 10=256-bit)

0x044 | AES_TRIGGER_REG | W | [0]: Strobe high (1) to start internal execution cycle
0x048 | AES_STATE_REG | R | [0]: 1=Engine actively processing, 0=Idle Complete

---

2. SHA SECURE HASH CO-PROCESSOR (Base Address: 0x3FF3E000)

---

Utilizes a manual, iterative FIFO-style block structure where chunks are
loaded sequentially by the CPU core.

| Offset | Register Name         | R/W | Field/Bit Configuration [Details]                                |
| ------ | --------------------- | --- | ---------------------------------------------------------------- |
| 0x000  | SHA_TEXT_IN_0..15_REG | W   | [31:0] Message block memory input window (16 registers to 0x03C) |
| 0x040  | SHA_MODE_REG          | R/W | [2:0]: 0=SHA-1, 1=SHA-256, 2=SHA-384, 3=SHA-512                  |
| 0x044  | SHA_START_REG         | W   | [0]: Pulse 1 to begin calculation on initial message block       |
| 0x048  | SHA_CONTI_REG         | W   | [0]: Pulse 1 to compute successive blocks in the sequence        |
| 0x04C  | SHA_BUSY_REG          | R   | [0]: 1=Hash engine active, 0=Ready/Idle Saturated                |
| 0x000  | SHA_TEXT_OUT_0_REG    | R   | Alias read path for finished digest state (After complete)       |

---

3. RSA / MULTI-PRECISION INTEGER (MPI) ACCELERATOR (Base Address: 0x3FF3C000)

---

This memory-mapped block holds 512-byte wide-integer computation spaces
(X, Y, M, Z buffers) alongside Montgomery control structures.

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

---

4. SECURE FLASH / EXTERNAL MEMORY CRYPTION REGISTERS (Base Address: 0x3FF5A000)

---

Handles standard on-the-fly encryption/decryption specifically for external NOR flash.

| Offset                                                                           | Register Name         | R/W | Field/Bit Configuration [Details]                             |
| -------------------------------------------------------------------------------- | --------------------- | --- | ------------------------------------------------------------- |
| 0x000                                                                            | FLASH_ENC_MODE_REG    | R/W | [1:0]: 0=Encrypted pass, 1=Bypass, 2=Debug access mode        |
| 0x004                                                                            | FLASH_ENC_TRIGGER_REG | W   | [0]: Fire manual system update cycle for page encryption keys |
| 0x008                                                                            | FLASH_ENC_STATUS_REG  | R   | [0]: 1=Flash hardware line decryption pipeline saturated      |
| ================================================================================ |
