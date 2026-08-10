================================================================================
ESPRESSIF ESP32-E (ECO V3): CRYPTOGRAPHIC REGISTRAR ARCHITECTURE SPECIFICATION
================================================================================

---

1. AES ENGINE REGISTERS (Base Address: 0x3FF3B000)

---

The ESP32-E retains the single-block hardware engine. Unlike the P4/C6,
it features zero DMA master capabilities for this block—the CPU must copy all words.

| Offset | Register Name      | R/W | Field/Bit Configuration [Details]                                      |
| ------ | ------------------ | --- | ---------------------------------------------------------------------- |
| 0x000  | AES_KEY_0_REG      | W   | [31:0] Key Word 0 (8 sequential 32-bit registers to 0x01C for 256-bit) |
| 0x020  | AES_TEXT_IN_0_REG  | W   | [31:0] Plaintext Input Word 0 (4 registers up to 0x02C)                |
| 0x030  | AES_TEXT_OUT_0_REG | R   | [31:0] Ciphertext Output Word 0 (4 registers up to 0x03C)              |
| 0x040  | AES_MODE_REG       | R/W | : 0=Encrypt, 1=Decrypt                                                 |

       |                       |     | [2:1]: Key Length Select (00=128-bit, 01=192-bit, 10=256-bit)

0x044 | AES_TRIGGER_REG | W |: Strobe high (1) to execute block cipher transformation
0x048 | AES_STATE_REG | R |: 1=Engine actively processing, 0=Computation Complete/Idle

---

2. SHA SECURE HASH ENGINE (Base Address: 0x3FF3E000)

---

Iterative, hardware-FIFO architecture. Blocks must be streamed manually
word-by-word into the register array by the core.

| Offset | Register Name         | R/W | Field/Bit Configuration [Details]                              |
| ------ | --------------------- | --- | -------------------------------------------------------------- |
| 0x000  | SHA_TEXT_IN_0..15_REG | W   | [31:0] Message input window (16 sequential registers to 0x03C) |
| 0x040  | SHA_MODE_REG          | R/W | [2:0]: 0=SHA-1, 1=SHA-256, 2=SHA-384, 3=SHA-512                |
| 0x044  | SHA_START_REG         | W   | : Pulse 1 to initialize and calculate the first block          |
| 0x048  | SHA_CONTI_REG         | W   | : Pulse 1 to run subsequent sequential data blocks             |
| 0x04C  | SHA_BUSY_REG          | R   | : 1=Hash pipeline running, 0=Block calculation complete        |
| 0x000  | SHA_TEXT_OUT_0_REG    | R   | Alias read path to digest output state (Valid when BUSY == 0)  |

---

3. RSA / MULTI-PRECISION INTEGER (MPI) ACCELERATOR (Base Address: 0x3FF3C000)

---

Maps large 512-byte hardware spaces (Buffers X, Y, M, Z) along with Montgomery
inversions to minimize CPU-bound bignum overhead.

| Offset                                                                           | Register Name      | R/W | Field/Bit Configuration [Details]                              |
| -------------------------------------------------------------------------------- | ------------------ | --- | -------------------------------------------------------------- |
| 0x000                                                                            | RSA_M_K_REG        | R/W | [31:0] Montgomery Constant M' (Calculated: M' = -1/M mod 2^32) |
| 0x004                                                                            | RSA_LENGTH_REG     | R/W | [5:0]: Array size bound register: ((Bit_Width / 32) - 1)       |
| 0x008                                                                            | RSA_V_CMD_REG      | R/W | [1:0]: 0x0=ModExp, 0x1=ModMult, 0x2=Large-Int Mult             |
| 0x00C                                                                            | RSA_READY_REG      | R   | : 1=Pipeline ready / Engine idle, 0=Computing                  |
| 0x010                                                                            | RSA_CLEAN_REG      | W   | : Write 1 to clear calculation internal states                 |
| 0x014                                                                            | RSA_INT_EN_REG     | R/W | : Done interrupt global mask                                   |
| 0x018                                                                            | RSA_INT_STATUS_REG | R   | : Operation complete interrupt active                          |
| 0x020                                                                            | RSA_COMP_MODE_REG  | R/W | : 0=Standard, 1=Accelerated Montgomery Core                    |
| 0x100                                                                            | RSA_MEM_X_BASE     | R/W | Multiplicand Block Memory Array X (512 bytes / 4096-bit limit) |
| 0x300                                                                            | RSA_MEM_Y_BASE     | R/W | Exponent / Multiplier Block Memory Array Y (512 bytes)         |
| 0x500                                                                            | RSA_MEM_M_BASE     | R/W | Modulus Block Memory Array M (512 bytes)                       |
| 0x700                                                                            | RSA_MEM_Z_BASE     | R/W | Result / Intermediate Output Block Memory Array Z (512 bytes)  |
| ================================================================================ |
