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

| Offset | Register Name      | R/W | Field/Bit Configuration [Details]                                          |
| ------ | ------------------ | --- | -------------------------------------------------------------------------- |
| 0x000  | RSA_M_K_REG        | R/W | [31:0] Montgomery Primitive M' parameter calculation: (M' = -1/M mod 2^32) |
| 0x004  | RSA_LENGTH_REG     | R/W | [5:0]: Bit-width computation parameter floor: ((Bit_Width / 32) - 1)       |
| 0x008  | RSA_V_CMD_REG      | R/W | [1:0]: 0x0=ModExponentiation, 0x1=ModMultiplication, 0x2=Multiplication    |
| 0x00C  | RSA_READY_REG      | R   | [0]: 1=Operation Complete / Idle Engine, 0=Busy Computing                  |
| 0x010  | RSA_CLEAN_REG      | W   | [0]: Write 1 to clear execution state machine registers                    |
| 0x014  | RSA_INT_EN_REG     | R/W | [0]: Global Done hardware interrupt enable flag                            |
| 0x018  | RSA_INT_STATUS_REG | R   | [0]: Finished operation interrupt status latch                             |
| 0x020  | RSA_COMP_MODE_REG  | R/W | [0]: 0=Standard core engine, 1=Accelerated Montgomery Core                 |
| 0x100  | RSA_MEM_X_BASE     | R/W | Multiplicand block memory array X (512 bytes / 4096 bits)                  |
| 0x300  | RSA_MEM_Y_BASE     | R/W | Exponent/Multiplier block memory array Y (512 bytes)                       |
| 0x500  | RSA_MEM_M_BASE     | R/W | Modulus block memory array M (512 bytes)                                   |
| 0x700  | RSA_MEM_Z_BASE     | R/W | Calculated output/result intermediate array Z (512 bytes)                  |

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
