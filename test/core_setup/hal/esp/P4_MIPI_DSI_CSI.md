================================================================================
ESPRESSIF ESP32-P4: EXHAUSTIVE MIPI DSI & CSI-2 INTERFACE REGISTER MAP
================================================================================

---

1. MIPI DSI HOST & BRIDGE REGISTERS (Outbound Bus Matrix - Base: 0x600A0000)

---

Controls packet packaging, lane configuration, and raw high-speed bitstream output.

| Offset | Register Name        | R/W | Field / Bit-Level Hardware Specification               |
| ------ | -------------------- | --- | ------------------------------------------------------ |
| 0x000  | DSI_BGD_CTRL_REG     | R/W | Bit 0: Bridge enable. Bit 1: Software reset            |
| 0x004  | DSI_LANE_CTRL_REG    | R/W | Bits [1:0]: Active lane count (00=1 lane, 01=2 lanes)  |
| 0x008  | DSI_PWR_UP_REG       | R/W | Bit 0: Core power up (0=Shutdown, 1=Core active)       |
| 0x00C  | DSI_VID_MODE_CFG_REG | R/W | Bits [1:0]: 0=Non-burst, 1=Burst mode (Max throughput) |

       |                         |     | Bits [5:4]: Frame transmission Data Type (DT) overrides

0x018 | DSI_PCK_H_LEN_REG | R/W | Bits [15:0]: Word Count (WC) per "line" packet burst
0x01C | DSI_PCK_V_LEN_REG | R/W | Bits [15:0]: Total line counts per virtual "frame" envelope
0x028 | DSI_H_BLANKING_REG | R/W | Bits [11:0]: Horizontal blanking interval (Set to 0/min)
0x02C | DSI_V_BLANKING_REG | R/W | Bits [11:0]: Vertical blanking interval (Set to 0/min)
0x038 | DSI_CMD_MODE_CFG_REG | R/W | Configuration for out-of-band control packet insertion
0x048 | DSI_INT_STATUS_REG | R | Latches FIFO overflows, link errors, and completion flags
0x04C | DSI_INT_CLR_REG | W | Clear active DSI interrupt status flags
0x054 | DSI_RAW_DATA_TYPE_REG | R/W | Bits [5:0]: Explicit user-defined Data Type (e.g., 0x30-0x3F)

---

2. MIPI CSI-2 CAMERA CONTROLLER REGISTERS (Inbound Bus Matrix - Base: 0x600C0000)

---

Handles high-speed input streams, matching custom data types, and routing directly to DMA.

| Offset | Register Name         | R/W | Field / Bit-Level Hardware Specification               |
| ------ | --------------------- | --- | ------------------------------------------------------ |
| 0x000  | CSI_CTRL_REG          | R/W | Bit 0: CSI core enable. Bit 4: Software loop reset     |
| 0x004  | CSI_LANE_CFG_REG      | R/W | Bits [1:0]: Lane configuration selection (Single/Dual) |
| 0x008  | CSI_DATA_TYPE_CFG_REG | R/W | Bits [5:0]: Expected input Data Type matching code     |
| 0x00C  | CSI_DMA_IN_MODE_REG   | R/W | Bit 0: Bypass internal Image Signal Processor (ISP)    |

       |                         |     | Bit 4: Unformatted raw byte-stream capture mode enable

0x010 | CSI_FRAME_TIMEOUT_REG | R/W | [31:0] Maximum clock cycles to wait before forcing a drop
0x018 | CSI_BUFFER_STATUS_REG | R | Bits [2:0]: Input FIFO state (0=Ready, 1=Full, 2=Over)
0x020 | CSI_INT_ST_REG | R | Latches Line Start (LS), Line End (LE), and CRC anomalies
0x024 | CSI_INT_CLR_REG | W | Clear active CSI input interface interrupts

---

3. MIPI DPHY REGISTERS (Physical Signaling Layer - Base: 0x600A4000)

---

Governs analog line drivers, clock lanes, and High-Speed (HS) to Low-Power (LP) states.

| Offset                                                                           | Register Name      | R/W | Field / Bit-Level Hardware Specification                     |
| -------------------------------------------------------------------------------- | ------------------ | --- | ------------------------------------------------------------ |
| 0x000                                                                            | DPHY_CLK_LANE_REG  | R/W | Bit 0: Force clock lane into continuous High-Speed mode      |
| 0x004                                                                            | DPHY_DATA_LANE_REG | R/W | Bit 0: Force data lanes to stay in active HS state           |
| 0x00C                                                                            | DPHY_TIME_HSS_REG  | R/W | Bits [7:0]: Time duration for LP to HS transition (T_LPX)    |
| 0x010                                                                            | DPHY_TIME_HSE_REG  | R/W | Bits [7:0]: Time duration for HS to LP teardown sequence     |
| 0x020                                                                            | DPHY_STATUS_REG    | R   | Bits [3:0]: Physical pin status states (LP-00, LP-01, LP-11) |
| ================================================================================ |
