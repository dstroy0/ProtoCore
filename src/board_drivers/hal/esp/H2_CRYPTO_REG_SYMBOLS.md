================================================================================
ESPRESSIF ESP32-H2: EXHAUSTIVE CRYPTOGRAPHIC ACCELERATOR REGISTER SPECIFICATION
================================================================================

---

1. SHA / AES UNIFIED ACCELERATOR (Unified Base Address: 0x6003A000)

---

The H2 maps both the AES and SHA engines directly into adjacent register windows,
utilizing identical pipeline locking logic to the C6.

| Offset | Register Name      | R/W | Field/Bit Configuration [Hardware Details]                |
| ------ | ------------------ | --- | --------------------------------------------------------- |
| 0x000  | AES_KEY_0_REG      | W   | [31:0] Key Word 0 (8 sequential registers up to 0x01C)    |
| 0x020  | AES_TEXT_IN_0_REG  | W   | [31:0] Plaintext Input Word 0 (4 registers up to 0x02C)   |
| 0x030  | AES_TEXT_OUT_0_REG | R   | [31:0] Ciphertext Output Word 0 (4 registers up to 0x03C) |
| 0x040  | AES_MODE_REG       | R/W | : 0=Encrypt, 1=Decrypt                                    |

       |                       |     | [2:1]: Key Size (00=128-bit, 01=192-bit, 10=256-bit)

0x044 | AES_ENDIAN_REG | R/W | [5:0]: Hardware endian-swap (Key, In, Out bytes)
0x048 | AES_TRIGGER_REG | W |: Strobe high (1) to execute single cipher burst
0x04C | AES_STATE_REG | R | [1:0]: State: 0=IDLE, 1=LOAD, 2=CALC, 3=DONE
0x054 | AES_DMA_IN_CTRL_REG | R/W |: 1=Link GDMA channel directly to AES Input FIFO
0x058 | AES_DMA_OUT_CTRL_REG | R/W |: 1=Link GDMA channel directly to AES Output FIFO
0x05C | SHA_MODE_REG | R/W | [2:0]: 0=SHA-1, 1=SHA-224, 2=SHA-256
0x060 | SHA_START_REG | W |: Strobe high (1) to process first message block
0x064 | SHA_CONTINUE_REG | W |: Strobe high (1) to process consecutive blocks
0x068 | SHA_BUSY_REG | R |: 1=SHA Engine processing, 0=Ready/Idle Saturated
0x06C | SHA_DMA_IN_CTRL_REG | R/W |: 1=Link GDMA directly to SHA Block Buffer

---

2. RSA / MULTI-PRECISION INTEGER (MPI) CO-PROCESSOR (Base Address: 0x6003C000)

---

| Offset | Register Name         | R/W | Field/Bit Configuration [Hardware Details]            |
| ------ | --------------------- | --- | ----------------------------------------------------- |
| 0x000  | RSA_SET_START_MODEXP  | W   | : Fire Montgomery Modular Exponentiation              |
| 0x004  | RSA_SET_START_MODMULT | W   | : Fire Montgomery Modular Multiplication              |
| 0x008  | RSA_SET_START_MULT    | W   | : Fire Large-Integer Normal Matrix Multiplication     |
| 0x00C  | RSA_QUERY_BUSY_REG    | R   | : 1=RSA execution active, 0=Idle Complete             |
| 0x010  | RSA_LENGTH_REG        | R/W | [5:0]: Word Count Array Bound: ((WordCount) - 1)      |
| 0x014  | RSA_COMP_MODE_REG     | R/W | : 0=Standard mode, 1=Accelerated Montgomery Core      |
| 0x800  | RSA_MEM_X_BASE        | R/W | Multiplicand Block Memory X (512 bytes)               |
| 0xA00  | RSA_MEM_Y_BASE        | R/W | Exponent/Multiplier Block Memory Y (512 bytes)        |
| 0xC00  | RSA_MEM_M_BASE        | R/W | Modulus Block Memory M (512 bytes)                    |
| 0xE00  | RSA_MEM_Z_BASE        | R/W | Result Output/Intermediate Block Memory Z (512 bytes) |

---

3. ECC / ECDSA CORE ACCELERATOR (Base Address: 0x6003E000)

---

| Offset | Register Name | R/W | Field/Bit Configuration [Hardware Details]       |
| ------ | ------------- | --- | ------------------------------------------------ |
| 0x000  | ECC_START_REG | W   | : Strobe 1 to begin hardware elliptic curve calc |
| 0x004  | ECC_BUSY_REG  | R   | : 1=ECC engine active, 0=Ready/Idle              |
| 0x008  | ECC_MODE_REG  | R/W | [2:0]: Curve (0=P256, 1=P192, 2=P384, 3=P521)    |

       |                       |     | [5:3]: Macro operation command code

0x00C | ECC_INT_EN_REG | R/W |: Enable Done interrupt status flag
0x010 | ECC_INT_CLR_REG | W |: Clear completion status latch

---

4. HMAC ENGINE & DIGITAL SIGNATURE CORE (Base Address: 0x60036000)

---

Pulls cryptographic keys directly from hidden eFuse slots down into shadow registers,
shielding secret parameters entirely from the data bus.

| Offset                                                                           | Register Name      | R/W | Field/Bit Configuration [Hardware Details]          |
| -------------------------------------------------------------------------------- | ------------------ | --- | --------------------------------------------------- |
| 0x000                                                                            | HMAC_SET_START_REG | W   | : Pulse 1 to begin HMAC hash key-derivation         |
| 0x004                                                                            | HMAC_INT_ST_REG    | R   | : Interrupt status monitor for the HMAC block       |
| 0x008                                                                            | HMAC_INT_CLR_REG   | W   | : Clear HMAC active status flag                     |
| 0x00C                                                                            | HMAC_KEY_SEL_REG   | W   | [2:0]: Select eFuse key block (0 to 5) for hash     |
| 0x010                                                                            | HMAC_TEXT_IN_REG   | W   | [31:0] Stream data window directly for message hash |
| 0x050                                                                            | DS_START_REG       | W   | : Strobe 1 to launch RSA signature compilation      |
| 0x054                                                                            | DS_BUSY_REG        | R   | : 1=Digital Signature hardware processing, 0=Idle   |
| ================================================================================ |
