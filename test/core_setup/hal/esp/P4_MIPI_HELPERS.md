================================================================================
ESPRESSIF ESP32-P4: SYSTEM ENGINE, CO-DATA PRE-PROCESSOR, & ISOLATION REGISTERS
================================================================================

The following registers compile the on-the-fly Bit Scrambler processing engine,
the Debug Assistant boundary validation suite, and the Core Interrupt Matrix.
Together, they establish a secure, uncontested, and hardware-accelerated
execution lane for high-performance I/O processing tasks.

---

1. COMP_BS: BIT SCRAMBLER REGISTERS (Unified Base Address: 0x60034000)

---

Positions directly inside the GDMA channels to execute on-the-fly byte-swapping,
bit-reversal, and lookup-table transformations completely in-silicon.

| Offset | Register Name        | R/W | Field / Bit-Level Hardware Specification                    |
| ------ | -------------------- | --- | ----------------------------------------------------------- |
| 0x000  | BS_TX_CTRL_REG       | R/W | Bit 0: Transmit scrambler core enable. Bit 1: Soft reset    |
| 0x004  | BS_TX_MODE_REG       | R/W | Bits [2:0]: Mode (0=Bypass, 1=LUT, 2=Bit-swap, 3=Reverse)   |
| 0x00C  | BS_TX_LUT_ADDR_REG   | R/W | [31:0]: Base address of hardware Lookup Table in SRAM       |
| 0x018  | BS_RX_CTRL_REG       | R/W | Bit 0: Receive scrambler core enable. Bit 1: Soft reset     |
| 0x01C  | BS_RX_MODE_REG       | R/W | Bits [2:0]: Inbound transform mode (Bypass, LUT, Swap, Rev) |
| 0x028  | BS_RX_BITS_COUNT_REG | R/W | Bits [15:0]: Word boundary tracking for continuous streams  |
| 0x034  | BS_INT_ST_REG        | R   | Latches lookup collisions or boundary alignment slips       |
| 0x038  | BS_INT_CLR_REG       | W   | Strobe high (1) to clear active Bit Scrambler interrupts    |

---

2. COMP_DA: DEBUG ASSISTANT REGISTERS (Unified Base Address: 0x60024000)

---

Acts as a zero-overhead, hardware-level watchpoint engine monitoring ring buffer
pointers and execution spaces for precise fault-capturing without JTAG stalls.

| Offset | Register Name       | R/W | Field / Bit-Level Hardware Specification                       |
| ------ | ------------------- | --- | -------------------------------------------------------------- |
| 0x000  | DA_ASS_CTRL_REG     | R/W | Bit 0: Watchpoint channel 0 enable. Bit 1: Channel 1 enable    |
| 0x004  | DA_ASS_ADDR_MIN_REG | R/W | [31:0]: Lower address boundary of targeted memory region       |
| 0x008  | DA_ASS_ADDR_MAX_REG | R/W | [31:0]: Upper address boundary of targeted memory region       |
| 0x00C  | DA_ASS_MODE_REG     | R/W | Bits [1:0]: Trigger condition (0=Read, 1=Write, 2=R/W, 3=Exec) |
| 0x010  | DA_ASS_COUNTER_REG  | R/W | Bits [31:0]: Triggers a hardware trace signal after N matches  |
| 0x014  | DA_ASS_STATUS_REG   | R   | Latches exact violation address and active core master ID      |
| 0x018  | DA_ASS_INT_CLR_REG  | W   | Strobe high (1) to clear active debug assistant status flags   |

---

3. COMP_INT: DYNAMIC INTERRUPT MATRIX CONFIGURATION (Unified Base Address: 0x60018000)

---

Isolates systemic peripheral noise and background scheduler interrupts away from
the worker core, eliminating context-switching and register-saving cycle taxes.

| Offset                                                                           | Register Name        | R/W | Field / Bit-Level Hardware Specification                         |
| -------------------------------------------------------------------------------- | -------------------- | --- | ---------------------------------------------------------------- |
| 0x000                                                                            | INT_MAP_CORE0_REGn   | R/W | [7:0]: Assign Peripheral ID 'n' (0-127) to Core 0 (PRO CPU)      |
| 0x200                                                                            | INT_MAP_CORE1_REGn   | R/W | [7:0]: Assign Peripheral ID 'n' (0-127) to Core 1 (APP CPU)      |
| 0x400                                                                            | INT_STATUS_CORE0_REG | R   | [31:0]: Active hardware peripheral interrupt mask hitting Core 0 |
| 0x404                                                                            | INT_STATUS_CORE1_REG | R   | [31:0]: Active hardware peripheral interrupt mask hitting Core 1 |
| 0x408                                                                            | INT_GLOBAL_EN_CORE0  | R/W | Bit 0: Master global interrupt switch for Core 0 pipeline        |
| 0x40C                                                                            | INT_GLOBAL_EN_CORE1  | R/W | Bit 0: Master global interrupt switch for Core 1 pipeline        |
| ================================================================================ |
