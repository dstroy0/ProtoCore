================================================================================
ESPRESSIF ESP32-C6: CRYPTOGRAPHIC ACCELERATOR REGISTRAR SPECIFICATION
================================================================================

---

1. SHA / AES CO-PROCESSOR REGISTERS (Unified Base Address: 0x6003A000)

---

The ESP32-C6 shares memory lanes and status structures between its AES and SHA
hardware pipelines, using an interlock matrix to route the data paths.

| Offset | Register Name      | R/W | Field/Bit Configuration [Details]                                    |
| ------ | ------------------ | --- | -------------------------------------------------------------------- |
| 0x000  | AES_KEY_0_REG      | W   | [31:0] Key Word 0 (Total 8 sequential registers up to 0x01C)         |
| 0x020  | AES_TEXT_IN_0_REG  | W   | [31:0] Data Input Word 0 (Total 4 sequential registers up to 0x02C)  |
| 0x030  | AES_TEXT_OUT_0_REG | R   | [31:0] Data Output Word 0 (Total 4 sequential registers up to 0x03C) |
| 0x040  | AES_MODE_REG       | R/W | : 0=Encrypt, 1=Decrypt                                               |

       |                       |     | [2:1]: Key Size Selector (00=128-bit, 01=192-bit, 10=256-bit)

0x044 | AES_ENDIAN_REG | R/W | [5:0]: In-silicon byte/word swapping for Key, In, and Out
0x048 | AES_TRIGGER_REG | W |: Set bit high (1) to strobe manual computation loop
0x04C | AES_STATE_REG | R | [1:0]: State tracker: 0=IDLE, 1=LOAD, 2=CALC, 3=DONE
0x054 | AES_DMA_IN_CTRL_REG | R/W |: 1=Route GDMA master streams straight to AES input FIFO
0x058 | AES_DMA_OUT_CTRL_REG | R/W |: 1=Route finished AES output FIFO to GDMA stream lanes
0x05C | SHA_MODE_REG | R/W | [2:0]: Hash type (1=SHA-1, 2=SHA-256) -> *Note: C6 lacks 384/512
0x060 | SHA_START_REG | W |: Strobe high (1) to execute initial message block
0x064 | SHA_CONTINUE_REG | W |: Strobe high (1) to hash consecutive sequence blocks
0x068 | SHA_BUSY_REG | R |: 1=SHA state machine active, 0=Ready/Idle
0x06C | SHA_DMA_IN_CTRL_REG | R/W |: 1=Route GDMA channel directly to SHA block input registers

---

2. RSA / MULTI-PRECISION INTEGER (MPI) CO-PROCESSOR (Base Address: 0x6003C000)

---

Manages 512-byte hardware matrices with Montgomery multiplication logic.

| Offset | Register Name         | R/W | Field/Bit Configuration [Details]                                |
| ------ | --------------------- | --- | ---------------------------------------------------------------- |
| 0x000  | RSA_SET_START_MODEXP  | W   | : Launch Modular Exponentiation                                  |
| 0x004  | RSA_SET_START_MODMULT | W   | : Launch Modular Multiplication Sequence                         |
| 0x008  | RSA_SET_START_MULT    | W   | : Launch Normal Large-Integer Matrix Multiplication              |
| 0x00C  | RSA_QUERY_BUSY_REG    | R   | : 1=Montgomery Engine executing equations, 0=Idle Complete       |
| 0x010  | RSA_LENGTH_REG        | R/W | [5:0]: Array size bound register: ((WordCount) - 1)              |
| 0x014  | RSA_COMP_MODE_REG     | R/W | : 0=Standard core, 1=Accelerated core                            |
| 0x800  | RSA_MEM_X_BASE        | R/W | Array access to Multiplicand Buffer X (512 bytes / 4096-bit max) |
| 0xA00  | RSA_MEM_Y_BASE        | R/W | Array access to Exponent/Multiplier Buffer Y (512 bytes)         |
| 0xC00  | RSA_MEM_M_BASE        | R/W | Array access to Modulus Buffer M (512 bytes)                     |
| 0xE00  | RSA_MEM_Z_BASE        | R/W | Array access to calculated output results array Z (512 bytes)    |

---

3. DIGITAL SIGNATURE (DS) HARDWARE ENGINE (Base Address: 0x6003E000)

---

Provides secure RSA signing by pulling key encryption parameters directly
from internal HMAC/eFuse lines.

| Offset                                                                           | Register Name | R/W | Field/Bit Configuration [Details]                               |
| -------------------------------------------------------------------------------- | ------------- | --- | --------------------------------------------------------------- |
| 0x000                                                                            | DS_START_REG  | W   | : Pulse 1 to begin hardware signature generation                |
| 0x004                                                                            | DS_BUSY_REG   | R   | : 1=DS peripheral internal operations active, 0=Ready           |
| 0x008                                                                            | DS_IV_0_REG   | W   | [31:0] Initialization Vector Word 0 (Total 4 words up to 0x014) |
| 0x100                                                                            | DS_M_MEM_BASE | W   | Base input array for message blocks to be signed (512 bytes)    |
| 0x200                                                                            | DS_Y_MEM_BASE | R/W | Internal encrypted signature parameter block base (512 bytes)   |
| 0x300                                                                            | DS_X_MEM_BASE | R   | Output signature computation result base (512 bytes)            |
| ================================================================================ |
