================================================================================
ESPRESSIF ESP32-P4: COMPLETE CRYPTOGRAPHIC REGISTRAR ARCHITECTURE SPECIFICATION
================================================================================

---

1. COMP_AES REGISTERS (Unified Base: 0x60010000)

---

| Offset | Register Name         | R/W | Bitfield Layout / Functional Spec                        |
| ------ | --------------------- | --- | -------------------------------------------------------- |
| 0x000  | AES_KEY_0..7_REG      | W   | [31:0] 256-bit Key Space (8 sequential 32-bit registers) |
| 0x020  | AES_TEXT_IN_0..3_REG  | W   | [31:0] Plaintext Input (4 sequential 32-bit registers)   |
| 0x030  | AES_TEXT_OUT_0..3_REG | R   | [31:0] Ciphertext Output (4 sequential 32-bit registers) |
| 0x040  | AES_MODE_REG          | R/W | Bit [0]: 0=Encrypt, 1=Decrypt                            |

       |                         |     | Bits [2:1]: Key Size (00=128, 01=192, 10=256 bits)
       |                         |     | Bits [5:3]: Block Mode (000=ECB, 001=CBC, 010=OFB, 011=CTR, 100=GCM)

0x044 | AES_ENDIAN_REG | R/W | Bits [5:0]: Hardware endian swap configuration for data blocks
0x048 | AES_TRIGGER_REG | W | Bit [0]: Strobe high (1) to start manual calculation
0x04C | AES_STATE_REG | R | Bits [1:0]: State Machine Status (0=IDLE, 1=LOAD, 2=CALC, 3=DONE)
0x050 | AES_IV_0..3_REG | W | [31:0] Initialization Vector space (4 sequential registers)
0x060 | AES_H_0..3_REG | W | [31:0] GCM Hash Key (H) Component (4 sequential registers)
0x070 | AES_J_0..3_REG | W | [31:0] GCM J0 Counter Array (4 sequential registers)
0x080 | AES_DPA_MISC_REG | R/W | Bits [31:0]: Differential Power Analysis signal clock mask
0x084 | AES_DMA_IN_CTRL_REG | R/W | Bit [0]: Enable 2D-DMA Input interface link
0x088 | AES_DMA_OUT_CTRL_REG | R/W | Bit [0]: Enable 2D-DMA Output interface link

---

2. COMP_SHA REGISTERS (Unified Base: 0x60012000)

---

| Offset | Register Name    | R/W | Bitfield Layout / Functional Spec                                      |
| ------ | ---------------- | --- | ---------------------------------------------------------------------- |
| 0x000  | SHA_MODE_REG     | R/W | Bits [2:0]: Hash Mode (0=SHA1, 1=SHA224, 2=SHA256, 3=SHA384, 4=SHA512) |
| 0x004  | SHA_START_REG    | W   | Bit [0]: Pulse 1 to run initial block / first message sequence         |
| 0x008  | SHA_CONTINUE_REG | W   | Bit [0]: Pulse 1 to process subsequent buffered message sequences      |
| 0x00C  | SHA_BUSY_REG     | R   | Bit [0]: 1=Engine actively hashing, 0=Saturated/Ready                  |
| 0x010  | SHA_H_0..15_REG  | R   | [31:0] Internal/Final Digest State (16 sequential registers)           |
| 0x050  | SHA_M_0..15_REG  | W   | [31:0] Raw Message Block Input (16 sequential registers)               |

---

3. COMP_RSA REGISTERS (Unified Base: 0x60014000)

---

| Offset | Register Name           | R/W | Field/Bit Configuration [Details]                                    |
| ------ | ----------------------- | --- | -------------------------------------------------------------------- |
| 0x000  | RSA_MEM_M_BLOCK_BASE    | R/W | Modulus M (512 bytes / 4096-bit limit)                               |
| 0x200  | RSA_MEM_Z_BLOCK_BASE    | R/W | Result Z, also the r' input block (512 bytes)                        |
| 0x400  | RSA_MEM_Y_BLOCK_BASE    | R/W | Operand Y: exponent for MODEXP, multiplier for MODMULT (512 bytes)   |
| 0x600  | RSA_MEM_X_BLOCK_BASE    | R/W | Operand X: base / multiplicand (512 bytes)                           |
| 0x800  | RSA_M_DASH_REG          | R/W | Montgomery m' = -M^-1 mod 2^32                                       |
| 0x804  | RSA_LENGTH_REG          | R/W | [5:0]: operand length in words, minus 1                              |
| 0x808  | RSA_QUERY_CLEAN_REG     | R   | [0]: 0 = memory init NOT complete, 1 = complete. Spin while it is 0. |
| 0x80C  | RSA_MODEXP_START_REG    | W   | Write 1: start Montgomery modular exponentiation                     |
| 0x810  | RSA_MOD_MULT_START_REG  | W   | Write 1: start Montgomery modular multiplication                     |
| 0x814  | RSA_MULT_START_REG      | W   | Write 1: start plain large-integer multiplication                    |
| 0x818  | RSA_QUERY_INTERRUPT_REG | R   | [0]: 0 = Busy, 1 = Idle. Reset value is 0.                           |
| 0x81C  | RSA_CLEAR_INTERRUPT_REG | W   | Write 1: clear the completion flag                                   |
| 0x820  | RSA_CONSTANT_TIME_REG   | R/W | [0]: 0 = constant-time modexp (slower, no timing leak)               |
| 0x824  | RSA_SEARCH_OPEN_REG     | R/W | [0]: enable the search-position acceleration                         |
| 0x828  | RSA_SEARCH_POS_REG      | R/W | Search position for the accelerated modexp                           |
| 0x82C  | RSA_INTERRUPT_REG       | R/W | [0]: completion-interrupt enable. ProtoCore polls, so this stays 0.  |

> Verified against the vendor headers: `soc/esp32s3/include/soc/hwcrypto_reg.h` (S3/S2/classic
> naming) and `soc/<die>/register/soc/rsa_reg.h` (C3/C5/C6/H2/P4 naming). The layout is the same on
> every die in the list; only the symbol names differ between the two header generations.
> `core_setup/hal/esp/esp_crypto_hal.h` encodes exactly this map.

---

4. COMP_ECC REGISTERS (Unified Base: 0x60016000)

---

| Offset | Register Name | R/W | Bitfield Layout / Functional Spec                       |
| ------ | ------------- | --- | ------------------------------------------------------- |
| 0x000  | ECC_START_REG | W   | Bit [0]: Write 1 to launch calculation sequence         |
| 0x004  | ECC_BUSY_REG  | R   | Bit [0]: 1=Elliptic Curve engine active, 0=Idle         |
| 0x008  | ECC_MODE_REG  | R/W | Bits [2:0]: Curve type (0=P256, 1=P192, 2=P384, 3=P521) |

       |                         |     | Bits [5:3]: Macro operation command code

0x00C | ECC_INT_EN_REG | R/W | Bit [0]: Assert global hardware interrupt upon completion
0x010 | ECC_INT_CLR_REG | W | Bit [0]: Clear completion status latch
0x100 | ECC_POINT_X_BASE | R/W | Prime parameter coordinate X block buffer (128 words)
0x180 | ECC_POINT_Y_BASE | R/W | Prime parameter coordinate Y block buffer (128 words)

---

5. ESP32-P4 EXCLUSIVE: RSA_DS (Digital Signature Base: 0x6001A000)

---

Expsds specialized keys derived from eFuse/HMAC inputs seamlessly via hardware.

| Offset | Register Name   | R/W | Bitfield Layout / Functional Spec                             |
| ------ | --------------- | --- | ------------------------------------------------------------- |
| 0x000  | DS_START_REG    | W   | Bit [0]: Strobe 1 to begin hardware signature generation      |
| 0x004  | DS_BUSY_REG     | R   | Bit [0]: 1=DS peripheral internal operations active           |
| 0x008  | DS_KEY_SIGN_REG | W   | Bits [31:0]: Key verification configuration inputs            |
| 0x00C  | DS_IV_REG       | W   | [31:0] Initialization Vector for key parameters               |
| 0x100  | DS_M_MEM_BASE   | W   | Input message memory array base (Assembled for RSA signature) |
| 0x200  | DS_Y_MEM_BASE   | R/W | Internal encrypted signature parameter block base             |
| 0x300  | DS_X_MEM_BASE   | R   | Output signature computation result base                      |

---

6. ESP32-P4 EXCLUSIVE: XTS_AES FLASH PORTS (Base: 0x6001C000)

---

Performs real-time memory encryption bypassing standard CPU caches entirely.

| Offset | Register Name    | R/W | Bitfield Layout / Functional Spec        |
| ------ | ---------------- | --- | ---------------------------------------- |
| 0x000  | XTS_AES_MODE_REG | R/W | Bits [1:0]: 0=XTS-AES-128, 1=XTS-AES-256 |

       |                         |     | Bit [2]: Key source selector (0=Registers, 1=eFuse/Key Manager)

0x004 | XTS_AES_TRIGGER_REG | W | Bit [0]: Manual key-refresh update cycle execution
0x008 | XTS_AES_STATUS_REG | R | Bit [0]: 1=Memory lines encryption active, 0=Ready
0x00C | XTS_AES_ERR_ADDR_REG | R | [31:0] Captures physical address of unauthorized access attempts
================================================================================
