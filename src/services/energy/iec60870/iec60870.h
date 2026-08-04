// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iec60870.h
 * @brief IEC 60870-5-101 / -104 telecontrol (SCADA) codec (PC_ENABLE_IEC60870).
 *
 * The IEC 60870-5 utility-SCADA protocol in its two common transports:
 *  - **-104** over TCP: the APCI (`68 LEN` + 4 control octets) in its I / S / U formats,
 *    carrying an ASDU. I-format transfers numbered information (send/receive sequence
 *    numbers); S-format acknowledges; U-format does STARTDT / STOPDT / TESTFR.
 *  - **-101** over serial: the FT1.2 link frames - fixed length (`10 C A CS 16`) and variable
 *    length (`68 L L 68 C A <ASDU> CS 16`, 8-bit sum checksum), with a 1-octet link address.
 *  - The shared **ASDU** header (type id, variable structure qualifier, cause of transmission,
 *    common address) and the 3-octet Information Object Address used by both.
 *
 * Typed information objects for the common type ids are provided: single-point (M_SP_NA_1), double-point
 * (M_DP_NA_1), short-float measured value (M_ME_NC_1), single command (C_SC_NA_1), and double command
 * (C_DC_NA_1), each an IOA + value + quality / qualifier descriptor.
 * The remaining per-type-id elements are the application's; this is the framing + ASDU layer with named
 * type-id / COT constants for the common ones. Pure and host-tested. Bridge an RTU / outstation onto Wi-Fi:
 * run -104 over the shipped TCP stack, or -101 over a UART through an RS-232/485 transceiver.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IEC60870_H
#define PROTOCORE_IEC60870_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_IEC60870

#define IEC_START_104 0x68u   ///< -104 APCI start octet (also the -101 variable-frame start)
#define IEC_START_FIXED 0x10u ///< -101 fixed-length frame start octet
#define IEC_STOP 0x16u        ///< -101 frame stop octet
#define IEC104_APCI_LEN 6u    ///< -104 control field: start + length + 4 control octets

// -104 U-format control commands (octet 1).
#define IEC104_STARTDT_ACT 0x07u
#define IEC104_STARTDT_CON 0x0Bu
#define IEC104_STOPDT_ACT 0x13u
#define IEC104_STOPDT_CON 0x23u
#define IEC104_TESTFR_ACT 0x43u
#define IEC104_TESTFR_CON 0x83u

// Common ASDU type identifications (IEC 60870-5-101 ch. 7.2.1).
#define IEC_TYPE_M_SP_NA_1 1   ///< single-point information
#define IEC_TYPE_M_DP_NA_1 3   ///< double-point information
#define IEC_TYPE_M_ME_NA_1 9   ///< measured value, normalized
#define IEC_TYPE_M_ME_NB_1 11  ///< measured value, scaled
#define IEC_TYPE_M_ME_NC_1 13  ///< measured value, short float
#define IEC_TYPE_M_IT_NA_1 15  ///< integrated totals (counter)
#define IEC_TYPE_C_SC_NA_1 45  ///< single command
#define IEC_TYPE_C_DC_NA_1 46  ///< double command
#define IEC_TYPE_M_EI_NA_1 70  ///< end of initialization
#define IEC_TYPE_C_IC_NA_1 100 ///< interrogation command
#define IEC_TYPE_C_CS_NA_1 103 ///< clock synchronization command

// Common causes of transmission (COT, low 6 bits).
#define IEC_COT_PERIODIC 1
#define IEC_COT_SPONTANEOUS 3
#define IEC_COT_REQUEST 5
#define IEC_COT_ACTIVATION 6
#define IEC_COT_ACT_CON 7
#define IEC_COT_DEACTIVATION 8
#define IEC_COT_ACT_TERM 10
#define IEC_COT_INTERROGATED 20

// -101 link-layer control-field function codes (low nibble; direction/FCB/FCV in the high bits).
#define IEC_FC_RESET_LINK 0x00        ///< reset of remote link (SEND/CONFIRM)
#define IEC_FC_TEST_LINK 0x02         ///< test function for link
#define IEC_FC_USER_DATA_CONFIRM 0x03 ///< user data, confirmed
#define IEC_FC_USER_DATA_NOREPLY 0x04 ///< user data, no reply
#define IEC_FC_REQUEST_CLASS1 0x0A    ///< request class 1 data
#define IEC_FC_REQUEST_CLASS2 0x0B    ///< request class 2 data

/** @brief -104 APCI frame formats. */
typedef enum PROTO_ENUM_PACKED
{
    IEC104_I = 0, ///< information transfer (numbered; carries an ASDU)
    IEC104_S,     ///< supervisory (acknowledge only)
    IEC104_U,     ///< unnumbered (STARTDT / STOPDT / TESTFR)
} Iec104Format;

/** @brief A parsed -104 APCI. */
typedef struct
{
    Iec104Format format;
    uint16_t ns;         ///< send sequence number (I-format)
    uint16_t nr;         ///< receive sequence number (I + S formats)
    uint8_t u_cmd;       ///< U-format command octet
    const uint8_t *asdu; ///< ASDU slice (I-format), or nullptr
    size_t asdu_len;
} Iec104Apci;

/** @brief A parsed ASDU header (the 6 octets before the information objects). */
typedef struct
{
    uint8_t type_id;
    proto_bool sq;        ///< structure qualifier: elements share one base IOA
    uint8_t count;        ///< number of information objects / elements (0..127)
    proto_bool test;      ///< COT test bit
    proto_bool negative;  ///< COT positive/negative confirm bit
    uint8_t cot;          ///< cause of transmission (low 6 bits)
    uint8_t orig_addr;    ///< originator address
    uint16_t common_addr; ///< common address of the ASDU
} IecAsduHeader;

// --- IEC 60870-5-104 APCI (over TCP) ---

/** @brief Build an I-format APDU carrying @p asdu (numbered with @p ns / @p nr). */
size_t pc_iec104_build_i(uint8_t *buf, size_t cap, uint16_t ns, uint16_t nr, const uint8_t *asdu, size_t asdu_len);

/** @brief Build an S-format (supervisory) APDU acknowledging up to @p nr. */
size_t pc_iec104_build_s(uint8_t *buf, size_t cap, uint16_t nr);

/** @brief Build a U-format APDU with command @p u_cmd (IEC104_STARTDT_ACT, ...). */
size_t pc_iec104_build_u(uint8_t *buf, size_t cap, uint8_t u_cmd);

/** @brief Parse one -104 APDU. Fills @p out and @p consumed (the whole APDU length). */
proto_bool pc_iec104_parse(const uint8_t *buf, size_t len, Iec104Apci *out, size_t *consumed);

// --- ASDU header + Information Object Address (shared by -101 and -104) ---

/** @brief Build the 6-octet ASDU header (type id, VSQ, COT[2], common address[2]). */
size_t pc_iec_asdu_build_header(uint8_t *buf, size_t cap, const IecAsduHeader *h);

/** @brief Parse the 6-octet ASDU header; @p consumed is 6 on success. */
proto_bool pc_iec_asdu_parse_header(const uint8_t *buf, size_t len, IecAsduHeader *out, size_t *consumed);

/** @brief Write a 3-octet Information Object Address (little-endian). */
size_t pc_iec_put_ioa(uint8_t *buf, size_t cap, uint32_t ioa);

/** @brief Read a 3-octet Information Object Address (little-endian). */
uint32_t pc_iec_get_ioa(const uint8_t *p);

// --- typed information objects (the value(s) inside an ASDU, without a time tag) ---
//
// Each object is an IOA(3) followed by the type-specific value + a quality descriptor. The quality bits
// are shared: the low bit is value-specific (SPI for a single point, OV overflow for a measured value),
// and bits 4..7 are BL (blocked) / SB (substituted) / NT (not topical) / IV (invalid).

#define IEC_QUAL_OV 0x01u ///< measured-value overflow (QDS bit 0)
#define IEC_QUAL_BL 0x10u ///< blocked
#define IEC_QUAL_SB 0x20u ///< substituted
#define IEC_QUAL_NT 0x40u ///< not topical
#define IEC_QUAL_IV 0x80u ///< invalid
#define IEC_SCO_ON 0x01u  ///< single command: command state ON (SCS)
#define IEC_SCO_SE 0x80u  ///< single command: select (1) vs execute (0)

// Double-point value (DPI in a DIQ, DCS in a DCO): the low 2 bits of the octet.
#define IEC_DP_INDETERMINATE 0u   ///< indeterminate or intermediate state
#define IEC_DP_OFF 1u             ///< determined state OFF
#define IEC_DP_ON 2u              ///< determined state ON
#define IEC_DP_INDETERMINATE_3 3u ///< indeterminate state (alternate encoding)
#define IEC_DP_MASK 0x03u         ///< the DPI / DCS field occupies bits 0..1
#define IEC_DCO_SE 0x80u          ///< double command: select (1) vs execute (0)
#define IEC_DCO_QU_SHIFT 2u       ///< double command: qualifier of command (QU) occupies bits 2..6
#define IEC_DCO_QU_MASK 0x1Fu     ///< the 5-bit qualifier value (after shifting down)

// Binary counter reading (BCR) sequence-notation octet (M_IT_NA_1, type 15).
#define IEC_BCR_SQ_MASK 0x1Fu ///< the 5-bit sequence number occupies bits 0..4
#define IEC_BCR_CY 0x20u      ///< counter overflow (carry) occurred since the last reading
#define IEC_BCR_CA 0x40u      ///< counter was adjusted since the last reading
#define IEC_BCR_IV 0x80u      ///< the counter reading is invalid

/**
 * @brief Build a single-point information object (M_SP_NA_1, type 1): IOA(3) + SIQ(1).
 * @param on       the single-point value (SPI bit).
 * @param quality  the quality flags (IEC_QUAL_BL / _SB / _NT / _IV; the low bits are ignored).
 * @return 4 on success, 0 on overflow / a null buffer.
 */
size_t pc_iec_io_build_sp(uint8_t *buf, size_t cap, uint32_t ioa, proto_bool on, uint8_t quality);

/** @brief Parse a single-point information object into its IOA, value, and quality flags. False if < 4 octets. */
proto_bool pc_iec_io_parse_sp(const uint8_t *buf, size_t len, uint32_t *ioa, proto_bool *on, uint8_t *quality);

/**
 * @brief Build a short-float measured-value object (M_ME_NC_1, type 13): IOA(3) + IEEE-754 float(4, LE) + QDS(1).
 * @return 8 on success, 0 on overflow / a null buffer.
 */
size_t pc_iec_io_build_float(uint8_t *buf, size_t cap, uint32_t ioa, float value, uint8_t qds);

/** @brief Parse a short-float measured-value object into its IOA, value, and quality byte. False if < 8 octets. */
proto_bool pc_iec_io_parse_float(const uint8_t *buf, size_t len, uint32_t *ioa, float *value, uint8_t *qds);

/**
 * @brief Build a scaled measured-value object (M_ME_NB_1, type 11): IOA(3) + SVA(2, signed 16-bit LE) + QDS(1).
 *        The scaled value is a raw signed integer the receiver rescales by its engineering range.
 * @return 6 on success, 0 on overflow / a null buffer.
 */
size_t pc_iec_io_build_scaled(uint8_t *buf, size_t cap, uint32_t ioa, int16_t value, uint8_t qds);

/** @brief Parse a scaled measured-value object into its IOA, signed value, and quality byte. False if < 6 octets. */
proto_bool pc_iec_io_parse_scaled(const uint8_t *buf, size_t len, uint32_t *ioa, int16_t *value, uint8_t *qds);

/**
 * @brief Build a normalized measured-value object (M_ME_NA_1, type 9): IOA(3) + NVA(2, signed 16-bit LE) +
 *        QDS(1). @p value is the normalized fraction in [-1, +1), stored as value * 32768 (clamped to the
 *        signed-16-bit range) - the fixed-point companion to the scaled (raw integer) and short-float forms.
 * @return 6 on success, 0 on overflow / a null buffer.
 */
size_t pc_iec_io_build_normalized(uint8_t *buf, size_t cap, uint32_t ioa, float value, uint8_t qds);

/** @brief Parse a normalized measured-value object into its IOA, the fraction value (NVA / 32768), and the
 *  quality byte. False if < 6 octets. */
proto_bool pc_iec_io_parse_normalized(const uint8_t *buf, size_t len, uint32_t *ioa, float *value, uint8_t *qds);

/**
 * @brief Build an integrated-totals object (M_IT_NA_1, type 15): IOA(3) + BCR(5) = a signed 32-bit counter
 *        value (LE, two's complement) followed by a sequence-notation octet (the 5-bit sequence number in
 *        IEC_BCR_SQ_MASK plus the IEC_BCR_CY / _CA / _IV flags). The counter reading is the metering (energy /
 *        pulse total) companion to the measured values.
 * @return 8 on success, 0 on overflow / a null buffer.
 */
size_t pc_iec_io_build_counter(uint8_t *buf, size_t cap, uint32_t ioa, int32_t value, uint8_t seq);

/** @brief Parse an integrated-totals object into its IOA, the signed counter value, and the raw sequence-notation
 *  octet (test it with IEC_BCR_SQ_MASK / _CY / _CA / _IV). False if < 8 octets. */
proto_bool pc_iec_io_parse_counter(const uint8_t *buf, size_t len, uint32_t *ioa, int32_t *value, uint8_t *seq);

/**
 * @brief Build a single command object (C_SC_NA_1, type 45): IOA(3) + SCO(1).
 * @param on      the commanded state (SCS bit).
 * @param select  true for a select, false for an execute (S/E bit).
 * @return 4 on success, 0 on overflow / a null buffer.
 */
size_t pc_iec_io_build_sc(uint8_t *buf, size_t cap, uint32_t ioa, proto_bool on, proto_bool select);

/** @brief Parse a single command object into its IOA, commanded state, and select/execute flag. False if < 4. */
proto_bool pc_iec_io_parse_sc(const uint8_t *buf, size_t len, uint32_t *ioa, proto_bool *on, proto_bool *select);

/**
 * @brief Build a double-point information object (M_DP_NA_1, type 3): IOA(3) + DIQ(1).
 * @param dpi      the 2-bit double-point value (IEC_DP_OFF / _ON / _INDETERMINATE).
 * @param quality  the quality flags (IEC_QUAL_BL / _SB / _NT / _IV; other bits are ignored).
 * @return 4 on success, 0 on overflow / a null buffer.
 */
size_t pc_iec_io_build_dp(uint8_t *buf, size_t cap, uint32_t ioa, uint8_t dpi, uint8_t quality);

/** @brief Parse a double-point information object into its IOA, 2-bit value, and quality flags. False if < 4. */
proto_bool pc_iec_io_parse_dp(const uint8_t *buf, size_t len, uint32_t *ioa, uint8_t *dpi, uint8_t *quality);

/**
 * @brief Build a double command object (C_DC_NA_1, type 46): IOA(3) + DCO(1).
 * @param dcs     the 2-bit double command state (IEC_DP_OFF / _ON).
 * @param qu      the 5-bit qualifier of command (QU).
 * @param select  true for a select, false for an execute (S/E bit).
 * @return 4 on success, 0 on overflow / a null buffer.
 */
size_t pc_iec_io_build_dc(uint8_t *buf, size_t cap, uint32_t ioa, uint8_t dcs, uint8_t qu, proto_bool select);

/** @brief Parse a double command object into its IOA, 2-bit state, qualifier, and select/execute flag. False if < 4. */
proto_bool pc_iec_io_parse_dc(const uint8_t *buf, size_t len, uint32_t *ioa, uint8_t *dcs, uint8_t *qu,
                              proto_bool *select);

// --- IEC 60870-5-101 FT1.2 link frames (over serial) ---

/** @brief Build a fixed-length frame: 10 C A CS 16 (1-octet link address). */
size_t pc_iec101_build_fixed(uint8_t *buf, size_t cap, uint8_t control, uint8_t addr);

/** @brief Build a variable-length frame: 68 L L 68 C A <asdu> CS 16 (1-octet link address). */
size_t pc_iec101_build_variable(uint8_t *buf, size_t cap, uint8_t control, uint8_t addr, const uint8_t *asdu,
                                uint8_t asdu_len);

/** @brief A parsed -101 FT1.2 frame. */
typedef struct
{
    proto_bool fixed;    ///< true => fixed-length frame (no ASDU)
    uint8_t control;     ///< link control field
    uint8_t addr;        ///< link address
    const uint8_t *asdu; ///< ASDU slice (variable frame), or nullptr
    uint8_t asdu_len;
} Iec101Frame;

/** @brief Parse one -101 FT1.2 frame (validates start/stop, doubled length, sum checksum). */
proto_bool pc_iec101_parse(const uint8_t *buf, size_t len, Iec101Frame *out, size_t *consumed);

#endif // PC_ENABLE_IEC60870

PROTO_END_DECLS

#endif // PROTOCORE_IEC60870_H
