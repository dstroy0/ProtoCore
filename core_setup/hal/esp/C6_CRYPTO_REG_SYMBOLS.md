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

2. RSA / MULTI-PRECISION INTEGER (MPI) CO-PROCESSOR (Base Address: 0x6008A000)

---

Manages 512-byte hardware matrices with Montgomery multiplication logic.

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
