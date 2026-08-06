================================================================================
ESPRESSIF ESP32-S3: CRYPTOGRAPHIC ACCELERATOR REGISTRAR SPECIFICATION
================================================================================

---

1. AES CO-PROCESSOR REGISTERS (Base Address: 0x6003A000)

---

The S3 AES block features full 2D/1D GDMA channel bus access, bypassing CPU copy
overhead completely if enabled via the control registers.

| Offset | Register Name      | R/W | Field/Bit Configuration [Details]                                    |
| ------ | ------------------ | --- | -------------------------------------------------------------------- |
| 0x000  | AES_KEY_0_REG      | W   | [31:0] Key Word 0 (Total 8 sequential registers up to 0x01C)         |
| 0x020  | AES_TEXT_IN_0_REG  | W   | [31:0] Data Input Word 0 (Total 4 sequential registers up to 0x02C)  |
| 0x030  | AES_TEXT_OUT_0_REG | R   | [31:0] Data Output Word 0 (Total 4 sequential registers up to 0x03C) |
| 0x040  | AES_MODE_REG       | R/W | : 0=Encrypt, 1=Decrypt                                               |

       |                       |     | [2:1]: Key Size (00=128-bit, 01=192-bit, 10=256-bit)

0x044 | AES_ENDIAN_REG | R/W | [5:0]: In-silicon byte/word swapping for Key, In, and Out
0x048 | AES_TRIGGER_REG | W |: Strobe high (1) to execute manual calculation sequence
0x04C | AES_STATE_REG | R | [1:0]: State: 0=IDLE, 1=LOAD, 2=CALC, 3=DONE
0x054 | AES_DMA_IN_CTRL_REG | R/W |: Bit 0: 1=Link GDMA directly to input FIFO stream lanes
0x058 | AES_DMA_OUT_CTRL_REG | R/W |: Bit 0: 1=Link GDMA directly to output FIFO stream lanes

---

2. SHA SECURE HASH ENGINE (Base Address: 0x6003B000)

---

| Offset | Register Name       | R/W | Field/Bit Configuration [Details]                                  |
| ------ | ------------------- | --- | ------------------------------------------------------------------ |
| 0x000  | SHA_MODE_REG        | R/W | [2:0]: Hash Mode (1=SHA1, 2=SHA256, 3=SHA384, 4=SHA512)            |
| 0x004  | SHA_START_REG       | W   | : Pulse 1 to run initial block / first message sequence            |
| 0x008  | SHA_CONTINUE_REG    | W   | : Pulse 1 to process subsequent block data sequences               |
| 0x00C  | SHA_BUSY_REG        | R   | : 1=Hash Core active, 0=Ready/Idle Saturated                       |
| 0x010  | SHA_H_0..15_REG     | R   | [31:0] Internal Digest State registers (16 sequential registers)   |
| 0x050  | SHA_M_0..15_REG     | W   | [31:0] Raw Message Block Input registers (16 sequential registers) |
| 0x090  | SHA_DMA_IN_CTRL_REG | R/W | : Bit 0: 1=HttpRoute GDMA controller directly into SHA block array |

---

3. RSA / MULTI-PRECISION INTEGER (MPI) CO-PROCESSOR (Base Address: 0x6003C000)

---

| Offset | Register Name         | R/W | Field/Bit Configuration [Details]                          |
| ------ | --------------------- | --- | ---------------------------------------------------------- |
| 0x000  | RSA_SET_START_MODEXP  | W   | : Execute Montgomery Modular Exponentiation                |
| 0x004  | RSA_SET_START_MODMULT | W   | : Execute Montgomery Modular Multiplication                |
| 0x008  | RSA_SET_START_MULT    | W   | : Execute Normal Large-Integer Matrix Multiplication       |
| 0x00C  | RSA_QUERY_BUSY_REG    | R   | : 1=RSA Engine actively computing, 0=Idle Complete         |
| 0x010  | RSA_LENGTH_REG        | R/W | [5:0]: Array size bound register: ((WordCount) - 1)        |
| 0x014  | RSA_COMP_MODE_REG     | R/W | : 0=Standard core execution, 1=Accelerated Montgomery Core |
| 0x800  | RSA_MEM_X_BASE        | R/W | Multiplicand Block Memory X (512 bytes / 4096-bit limit)   |
| 0xA00  | RSA_MEM_Y_BASE        | R/W | Exponent / Multiplier Block Memory Y (512 bytes)           |
| 0xC00  | RSA_MEM_M_BASE        | R/W | Modulus Block Memory M (512 bytes)                         |
| 0xE00  | RSA_MEM_Z_BASE        | R/W | Result Output / Intermediate Block Memory Z (512 bytes)    |

---

4. DIGITAL SIGNATURE (DS) PERIPHERAL CORE (Base Address: 0x6003D000)

---

Offsets specialized parameter keys straight from hidden HMAC slots to ensure
zero-visibility of private keys.

| Offset                                                                           | Register Name | R/W | Field/Bit Configuration [Details]                               |
| -------------------------------------------------------------------------------- | ------------- | --- | --------------------------------------------------------------- |
| 0x000                                                                            | DS_START_REG  | W   | : Strobe 1 to begin hardware signature generation               |
| 0x004                                                                            | DS_BUSY_REG   | R   | : 1=DS peripheral internal operations active, 0=Ready           |
| 0x008                                                                            | DS_IV_0_REG   | W   | [31:0] Initialization Vector Word 0 (Total 4 words up to 0x014) |
| 0x100                                                                            | DS_M_MEM_BASE | W   | Input message memory array base for RSA signature (512 bytes)   |
| 0x200                                                                            | DS_Y_MEM_BASE | R/W | Internal encrypted signature parameter block base (512 bytes)   |
| 0x300                                                                            | DS_X_MEM_BASE | R   | Output signature computation result base (512 bytes)            |
| ================================================================================ |
