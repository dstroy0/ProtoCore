================================================================================
ESPRESSIF ESP32-C3: CRYPTOGRAPHIC ACCELERATOR REGISTER SPECIFICATION
================================================================================

---

1. SHA / AES INTERLOCKED CORE ACCELERATOR (Unified Base Address: 0x6003A000)

---

The C3 unifies AES and SHA into contiguous memory windows. They share an internal
data bus interlock, preventing simultaneous CPU execution but offering shared GDMA blocks.

| Offset | Register Name      | R/W | Field/Bit Configuration [Hardware Details]                              |
| ------ | ------------------ | --- | ----------------------------------------------------------------------- |
| 0x000  | AES_KEY_0_REG      | W   | [31:0] Key Word 0 (Total 8 sequential registers up to 0x01C)            |
| 0x020  | AES_TEXT_IN_0_REG  | W   | [31:0] Plaintext Input Word 0 (Total 4 sequential registers to 0x02C)   |
| 0x030  | AES_TEXT_OUT_0_REG | R   | [31:0] Ciphertext Output Word 0 (Total 4 sequential registers to 0x03C) |
| 0x040  | AES_MODE_REG       | R/W | : 0=Encrypt, 1=Decrypt                                                  |

       |                       |     | [2:1]: Key Size Selector (00=128-bit, 01=192-bit, 10=256-bit)

0x044 | AES_ENDIAN_REG | R/W | [5:0]: In-silicon byte/word swapping configuration
0x048 | AES_TRIGGER_REG | W |: Strobe high (1) to execute manual single block cipher burst
0x04C | AES_STATE_REG | R | [1:0]: State Matrix: 0=IDLE, 1=LOAD, 2=CALC, 3=DONE
0x054 | AES_DMA_IN_CTRL_REG | R/W |: Bit 0: 1=Link active GDMA input channel straight to AES FIFO
0x058 | AES_DMA_OUT_CTRL_REG | R/W |: Bit 0: 1=Link active GDMA output channel straight to AES FIFO
0x05C | SHA_MODE_REG | R/W | [2:0]: Hash Type (1=SHA-1, 2=SHA-256) -> *C3 lacks 384/512 extensions
0x060 | SHA_START_REG | W |: Strobe high (1) to process the initial message block
0x064 | SHA_CONTINUE_REG | W |: Strobe high (1) to process consecutive sequence blocks
0x068 | SHA_BUSY_REG | R |: 1=SHA core active compiling data, 0=Ready/Idle
0x06C | SHA_DMA_IN_CTRL_REG | R/W |: Bit 0: 1=Link active GDMA input channel straight to SHA Block Buffer

---

2. RSA / MULTI-PRECISION INTEGER (MPI) CO-PROCESSOR (Base Address: 0x6003C000)

---

| Offset | Register Name         | R/W | Field/Bit Configuration [Hardware Details]                      |
| ------ | --------------------- | --- | --------------------------------------------------------------- |
| 0x000  | RSA_SET_START_MODEXP  | W   | : Launch Modular Exponentiation Sequence                        |
| 0x004  | RSA_SET_START_MODMULT | W   | : Launch Modular Multiplication Sequence                        |
| 0x008  | RSA_SET_START_MULT    | W   | : Launch Large-Integer Normal Matrix Multiplication             |
| 0x00C  | RSA_QUERY_BUSY_REG    | R   | : 1=Montgomery Core running computations, 0=Idle Complete       |
| 0x010  | RSA_LENGTH_REG        | R/W | [5:0]: Array size bound parameter: ((WordCount) - 1)            |
| 0x014  | RSA_COMP_MODE_REG     | R/W | : 0=Standard core execution, 1=Accelerated Montgomery Core      |
| 0x800  | RSA_MEM_X_BASE        | R/W | Multiplicand Block Buffer X array space (512 bytes / 4096 bits) |
| 0xA00  | RSA_MEM_Y_BASE        | R/W | Exponent / Multiplier Block Buffer Y array space (512 bytes)    |
| 0xC00  | RSA_MEM_M_BASE        | R/W | Modulus Block Buffer M array space (512 bytes)                  |
| 0xE00  | RSA_MEM_Z_BASE        | R/W | Calculated output result/intermediate Z array space (512 bytes) |

---

3. DIGITAL SIGNATURE (DS) & HMAC HARDWARE CORE (Base Address: 0x6003E000)

---

| Offset                                                                           | Register Name      | R/W | Field/Bit Configuration [Hardware Details]                           |
| -------------------------------------------------------------------------------- | ------------------ | --- | -------------------------------------------------------------------- |
| 0x000                                                                            | DS_START_REG       | W   | : Strobe 1 to begin hardware signature generation                    |
| 0x004                                                                            | DS_BUSY_REG        | R   | : 1=Digital Signature peripheral busy internally, 0=Ready            |
| 0x008                                                                            | DS_IV_0_REG        | W   | [31:0] Initialization Vector Word 0 (Total 4 words up to 0x014)      |
| 0x100                                                                            | DS_M_MEM_BASE      | W   | Base input message buffer array for RSA signatures (512 bytes)       |
| 0x200                                                                            | DS_Y_MEM_BASE      | R/W | Base internal encrypted signature parameter block (512 bytes)        |
| 0x300                                                                            | DS_X_MEM_BASE      | R   | Base output calculated signature result array (512 bytes)            |
| 0x400                                                                            | HMAC_SET_START_REG | W   | : Pulse 1 to initialize HMAC key-derivation states                   |
| 0x408                                                                            | HMAC_KEY_SEL_REG   | W   | [2:0]: Force-route unexposed eFuse key blocks (0-5) into calculation |
| 0x410                                                                            | HMAC_TEXT_IN_REG   | W   | [31:0] Feed message blocks directly into hash state machine          |
| ================================================================================ |
