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

| Offset | Register Name         | R/W | Bitfield Layout / Functional Spec                            |
| ------ | --------------------- | --- | ------------------------------------------------------------ |
| 0x000  | RSA_SET_START_MODEXP  | W   | Bit [0]: Launch Montgomery Modular Exponentiation            |
| 0x004  | RSA_SET_START_MODMULT | W   | Bit [0]: Launch Montgomery Modular Multiplication            |
| 0x008  | RSA_SET_START_MULT    | W   | Bit [0]: Launch Large-Integer Normal Multiplication          |
| 0x00C  | RSA_QUERY_BUSY_REG    | R   | Bit [0]: 1=RSA engine active, 0=Idle/Complete                |
| 0x010  | RSA_LENGTH_REG        | R/W | Bits [6:0]: Matrix size boundary register: ((WordCount) - 1) |
| 0x014  | RSA_COMP_MODE_REG     | R/W | Bit [0]: 0=Standard core execution, 1=Accelerated mode       |
| 0x800  | RSA_MEM_X_BASE        | R/W | Multiplicand Memory Space X (512 bytes / 4096-bit limit)     |
| 0xA00  | RSA_MEM_Y_BASE        | R/W | Exponent/Multiplier Memory Space Y (512 bytes)               |
| 0xC00  | RSA_MEM_M_BASE        | R/W | Modulus Memory Space M (512 bytes)                           |
| 0xE00  | RSA_MEM_Z_BASE        | R/W | Output Matrix Memory Space Z (512 bytes)                     |

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
