================================================================================
ESPRESSIF SOC GDMA, 2D-DMA, & CRYPTO-DMA SYSTEM REGISTER SPECIFICATION
================================================================================

---

1. ESP32-P4 EXCLUSIVE: ADVANCED 2D-DMA / GDMA MATRIX (Base: 0x60020000)

---

The P4 features a multi-channel 2D-DMA controller where Channel X is dedicated
to the Crypto Subsystem. It supports row-stride jumping and macro-burst streaming.
Replace 'n' with the mapped Channel Index (typically Ch 0 or Ch 1 for Crypto).

| Offset    | Register Name          | R/W | Field/Bit Configuration [Hardware Details]                |
| --------- | ---------------------- | --- | --------------------------------------------------------- |
| 0x000+40n | DMA_IN_RESTART_CHn_REG | W   | Bit 0: Strobe high (1) to clear FIFO and force-restart RX |
| 0x004+40n | DMA_IN_LINK_CHn_REG    | R/W | [31:0]: Points directly to the first RX DMA Descriptor    |

          |                         |     | Bit 30: Start RX streaming sequence
          |                         |     | Bit 31: Stop RX streaming sequence

0x008+40n | DMA_OUT_RESTART_CHn_REG | W | Bit 0: Strobe high (1) to clear FIFO and force-restart TX
0x00C+40n | DMA_OUT_LINK_CHn_REG | R/W | [31:0]: Points directly to the first TX DMA Descriptor

          |                         |     | Bit 30: Start TX streaming sequence
          |                         |     | Bit 31: Stop TX streaming sequence

0x010+40n | DMA_IN_STATE_CHn_REG | R | [31:0]: Internal state machine status for the input channel
0x014+40n | DMA_OUT_STATE_CHn_REG | R | [31:0]: Internal state machine status for the output channel
0x01C+40n | DMA_IN_PERI_SEL_CHn_REG | R/W | [3:0]: Peripheral ID (Set to Crypto Engine ID)
0x020+40n | DMA_OUT_PERI_SEL_CHn_REG| R/W | [3:0]: Peripheral ID (Set to Crypto Engine ID)
0x028+40n | DMA_IN_2D_S_CHn_REG | R/W | [15:0]: 2D Mode Stride (Number of bytes to skip per row)
0x02C+40n | DMA_IN_2D_W_CHn_REG | R/W | [15:0]: 2D Mode Width (Number of valid bytes per row)
0x030+40n | DMA_OUT_2D_S_CHn_REG | R/W | [15:0]: 2D Mode Stride for TX operations
0x034+40n | DMA_OUT_2D_W_CHn_REG | R/W | [15:0]: 2D Mode Width for TX operations

---

2. MODERN CHIPS: GENERATION 2 GDMA REGISTER MAP (Base: 0x60030000)

---

Applies to: ESP32-S3, ESP32-C6, ESP32-H2, ESP32-C3.
Unified sequential channel structure. Replace 'n' with the active Crypto Channel Index.

| Offset    | Register Name        | R/W | Field/Bit Configuration [Hardware Details]           |
| --------- | -------------------- | --- | ---------------------------------------------------- |
| 0x000+80n | GDMA_IN_LINK_CHn_REG | R/W | [31:0]: Inbound In-Memory Descriptor Address Pointer |

          |                         |     | Bit 28: Strobe high to start RX channel processing

0x004+80n | GDMA_OUT_LINK_CHn_REG | R/W | [31:0]: Outbound In-Memory Descriptor Address Pointer

          |                         |     | Bit 28: Strobe high to start TX channel processing

0x00C+80n | GDMA_IN_PERI_SEL_CHn_REG| R/W | [5:0]: Map channel to peripheral (AES=0, SHA=1, etc.)
0x010+80n | GDMA_OUT_PERI_SEL_CHn_REG| R/W | [5:0]: Map channel to peripheral (AES=0, SHA=1, etc.)
0x01C+80n | GDMA_IN_CONF0_CHn_REG | R/W | Bit 0: Reset channel data counters

          |                         |     | Bit 3: Enable continuous ring-buffer auto-wrap mode

0x020+80n | GDMA_OUT_CONF0_CHn_REG | R/W | Bit 0: Reset channel data counters

          |                         |     | Bit 4: Enable data burst optimization (16-byte chunks)

---

3. LEGACY CHIPS: CRYPTO-DMA REGISTRAR MAP (Base: 0x60038000)

---

Applies to: ESP32-S2 (Classic ESP32 and ESP32-S / E variants lack dedicated Crypto-DMA)

| Offset | Register Name          | R/W | Field/Bit Configuration [Hardware Details]             |
| ------ | ---------------------- | --- | ------------------------------------------------------ |
| 0x000  | AES_DMA_IN_STATUS_REG  | R   | [31:0]: FIFO status for inbound Crypto-DMA channels    |
| 0x004  | AES_DMA_OUT_STATUS_REG | R   | [31:0]: FIFO status for outbound Crypto-DMA channels   |
| 0x008  | SHA_DMA_IN_STATUS_REG  | R   | [31:0]: Inbound block count status for hash engine     |
| 0x00C  | CRYPTO_DMA_IN_LINK_REG | R/W | [31:0]: Base address of raw Crypto-DMA descriptor link |

          |                         |     | Bit 29: Fire stream processing loop

0x010 | CRYPTO_DMA_OUT_LINK_REG | R/W | [31:0]: Base address of raw Crypto-DMA descriptor link

          |                         |     | Bit 29: Fire stream processing loop

================================================================================
