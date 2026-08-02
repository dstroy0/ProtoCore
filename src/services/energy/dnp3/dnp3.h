// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dnp3.h
 * @brief DNP3 (IEEE 1815) data-link frame codec (PC_ENABLE_DNP3) - zero-heap builder +
 *        CRC-validating parser for the SCADA / utility outstation link layer.
 *
 * A DNP3 data-link frame:
 * @code
 *   0x05 0x64  LEN  CTRL  DEST(2,LE)  SRC(2,LE)  CRC(2)      // 10-octet header block
 *   [<=16 user-data octets][CRC(2)] ...                      // data blocks, each CRC'd
 * @endcode
 *  - LEN counts CTRL + DEST + SRC + user data (the start word, LEN itself, and the CRCs are
 *    excluded), so LEN = 5 + user_data_len; min 5, max 255 (<= 250 user-data octets).
 *  - Addresses are little-endian (LSB first). User data is carried in blocks of up to 16
 *    octets, each followed by its own CRC; the header is its own CRC'd block.
 *  - CRC is CRC-16/DNP (poly 0x3D65, init 0x0000, reflected in/out, final XOR 0xFFFF),
 *    transmitted low octet first.
 *
 * This is the data-link layer (framing + CRC) plus the transport function (IEEE 1815 §8.2:
 * segment header + fragment reassembly). The application layer (objects / function codes) is layered
 * on the reassembled fragment.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DNP3_H
#define PROTOCORE_DNP3_H

#include "protocore_config.h"

#if PC_ENABLE_DNP3

#define DNP3_START0 0x05
#define DNP3_START1 0x64
#define DNP3_MAX_USER_DATA 250 ///< LEN max 255 minus the 5 header octets it counts

// Frame geometry (octets). The header is one CRC'd block; user data is carried in
// fixed-size CRC'd blocks. These are wire constants from IEEE 1815, not tunables.
#define DNP3_BLOCK_LEN 16        ///< user-data octets per data block (the last may be shorter)
#define DNP3_CRC_LEN 2           ///< CRC-16/DNP appended after each block, low octet first
#define DNP3_HEADER_LEN 8        ///< header octets the header CRC covers: 0x0564 LEN CTRL DEST SRC
#define DNP3_HEADER_BLOCK_LEN 10 ///< whole header block = DNP3_HEADER_LEN + DNP3_CRC_LEN
#define DNP3_LEN_OVERHEAD 5      ///< octets LEN counts beyond user data: CTRL + DEST + SRC

/** @brief CRC-16/DNP (poly 0x3D65, init 0, reflected, final XOR 0xFFFF). */
uint16_t pc_dnp3_crc(const uint8_t *data, size_t len);

/**
 * @brief Build a complete data-link frame (header block + CRC'd data blocks).
 * @param control link-layer control octet (DIR/PRM/FCB/FCV + function code).
 * @param dest    16-bit destination address (written little-endian).
 * @param src     16-bit source address (written little-endian).
 * @return total octets written, or 0 on overflow / user_data_len > DNP3_MAX_USER_DATA.
 */
size_t pc_dnp3_build_frame(uint8_t *buf, size_t cap, uint8_t control, uint16_t dest, uint16_t src,
                           const uint8_t *user_data, size_t user_data_len);

/** @brief A parsed data-link frame header (the user data is de-blocked separately). */
typedef struct
{
    uint8_t length;  ///< the LEN field value
    uint8_t control; ///< link-layer control octet
    uint16_t dest;
    uint16_t src;
} Dnp3Frame;

/**
 * @brief Parse + CRC-validate a frame, de-blocking the user data (per-block CRCs stripped).
 * @param out_user     receives the reassembled user data.
 * @param out_cap      capacity of @p out_user.
 * @param out_user_len receives the user-data length.
 * @return true on a complete, all-CRC-valid frame; false on a bad start word, an invalid
 *         LEN, truncation, a header or block CRC mismatch, or an out_user overflow.
 */
proto_bool pc_dnp3_parse_frame(const uint8_t *buf, size_t len, Dnp3Frame *out, uint8_t *out_user, size_t out_cap,
                               size_t *out_user_len);

// --- transport function (IEEE 1815 §8.2): reassemble application fragments from link user data ---
//
// Each data-link frame's user data begins with a 1-octet transport header - FIN (bit 7), FIR (bit 6), and
// a 6-bit SEQUENCE - followed by up to 249 octets of the application fragment. A fragment starts with a
// FIR segment, continues with sequence-incrementing segments, and ends with a FIN segment (a single-frame
// fragment sets both). This layers on the de-blocked user data from pc_dnp3_parse_frame.

#define DNP3_TR_FIN 0x80u      ///< transport header: final segment of the fragment
#define DNP3_TR_FIR 0x40u      ///< transport header: first segment of the fragment
#define DNP3_TR_SEQ_MASK 0x3Fu ///< transport header: 6-bit sequence number
#define DNP3_TR_MAX_APP 249    ///< application octets per segment (250 user - 1 transport header)

/** @brief Compose a transport header octet from the FIR / FIN flags and a 6-bit sequence. */
uint8_t pc_dnp3_transport_header(proto_bool fir, proto_bool fin, uint8_t seq);

/**
 * @brief Build one transport segment (transport header + @p app_len application octets) into @p out.
 * @return the segment length (1 + @p app_len), or 0 on a bad argument / overflow / @p app_len > 249.
 */
size_t pc_dnp3_build_transport_segment(uint8_t *out, size_t cap, proto_bool fir, proto_bool fin, uint8_t seq,
                                       const uint8_t *app_data, size_t app_len);

/** @brief Result of feeding a link frame's user data to the transport reassembler. */
enum Dnp3TransportResult
{
    DNP3_TR_IGNORED = 0, ///< out-of-sequence / a continuation with no active fragment: discarded
    DNP3_TR_PROGRESS,    ///< a segment was accepted, more expected
    DNP3_TR_COMPLETE,    ///< the fragment is fully reassembled (see buf / len)
    DNP3_TR_ERROR,       ///< a buffer overflow (the fragment is abandoned)
};

/** @brief Transport-function reassembly state (one in-flight application fragment). */
typedef struct
{
    uint8_t *buf;       ///< caller-provided accumulation buffer
    size_t cap;         ///< its capacity
    size_t len;         ///< application octets accumulated so far
    uint8_t expect_seq; ///< the sequence the next segment must carry
    proto_bool active;  ///< a fragment is in progress (a FIR was seen)
    proto_bool done;    ///< the last segment (FIN) was accepted
} Dnp3TransportRx;

/** @brief Begin transport reassembly with a caller-owned buffer. */
void pc_dnp3_transport_rx_init(Dnp3TransportRx *r, uint8_t *buf, size_t cap);

/**
 * @brief Feed one link frame's de-blocked user data (transport header + application octets).
 * @return a @ref Dnp3TransportResult. On DNP3_TR_COMPLETE the reassembled fragment is @c buf[0..len).
 */
int pc_dnp3_transport_feed(Dnp3TransportRx *r, const uint8_t *user, size_t user_len);

// --- application layer (IEEE 1815 §4.2.2): the reassembled fragment's header ---
//
// An application fragment begins with a 1-octet Application Control (AC) and a 1-octet Function Code (FC).
// A response (FC RESPONSE / UNSOLICITED_RESPONSE) inserts two Internal Indication (IIN) octets before the
// object data; a request carries object headers immediately after the FC. This layers on the fragment that
// pc_dnp3_transport_feed reassembles.

// Application Control octet (IEEE 1815 §4.2.2.1).
#define DNP3_AC_FIR 0x80u      ///< first fragment of a multi-fragment response
#define DNP3_AC_FIN 0x40u      ///< final fragment
#define DNP3_AC_CON 0x20u      ///< confirmation of this fragment is requested
#define DNP3_AC_UNS 0x10u      ///< unsolicited response
#define DNP3_AC_SEQ_MASK 0x0Fu ///< 4-bit application sequence number

// Application function codes (IEEE 1815 Table 4-1, common subset).
#define DNP3_FC_CONFIRM 0x00u
#define DNP3_FC_READ 0x01u
#define DNP3_FC_WRITE 0x02u
#define DNP3_FC_SELECT 0x03u
#define DNP3_FC_OPERATE 0x04u
#define DNP3_FC_DIRECT_OPERATE 0x05u
#define DNP3_FC_DIRECT_OPERATE_NR 0x06u ///< direct operate, no response
#define DNP3_FC_COLD_RESTART 0x0Du
#define DNP3_FC_WARM_RESTART 0x0Eu
#define DNP3_FC_RESPONSE 0x81u
#define DNP3_FC_UNSOLICITED_RESPONSE 0x82u

// Internal Indication bits (IEEE 1815 §4.2.2.5), packed IIN1 in the low octet, IIN2 in the high octet.
#define DNP3_IIN_BROADCAST 0x0001u          ///< IIN1.0 request was broadcast
#define DNP3_IIN_CLASS1_EVENTS 0x0002u      ///< IIN1.1 class 1 events available
#define DNP3_IIN_CLASS2_EVENTS 0x0004u      ///< IIN1.2 class 2 events available
#define DNP3_IIN_CLASS3_EVENTS 0x0008u      ///< IIN1.3 class 3 events available
#define DNP3_IIN_NEED_TIME 0x0010u          ///< IIN1.4 time synchronization required
#define DNP3_IIN_LOCAL_CONTROL 0x0020u      ///< IIN1.5 some output point is in local mode
#define DNP3_IIN_DEVICE_TROUBLE 0x0040u     ///< IIN1.6 device trouble
#define DNP3_IIN_DEVICE_RESTART 0x0080u     ///< IIN1.7 device restarted
#define DNP3_IIN_FUNC_NOT_SUPPORTED 0x0100u ///< IIN2.0 function code not supported
#define DNP3_IIN_OBJECT_UNKNOWN 0x0200u     ///< IIN2.1 requested object(s) unknown
#define DNP3_IIN_PARAM_ERROR 0x0400u        ///< IIN2.2 parameter error in the request
#define DNP3_IIN_EVENT_OVERFLOW 0x0800u     ///< IIN2.3 event buffer overflowed
#define DNP3_IIN_ALREADY_EXECUTING 0x1000u  ///< IIN2.4 operation already executing
#define DNP3_IIN_CONFIG_CORRUPT 0x2000u     ///< IIN2.5 configuration corrupt

/** @brief A decoded application-fragment header (from pc_dnp3_parse_app_header). */
typedef struct
{
    uint8_t app_control; ///< raw Application Control octet
    proto_bool fir;
    proto_bool fin;
    proto_bool con;
    proto_bool uns;
    uint8_t seq;            ///< 4-bit application sequence
    uint8_t fc;             ///< function code
    proto_bool is_response; ///< true for RESPONSE / UNSOLICITED_RESPONSE (the two IIN octets are present)
    uint16_t iin;           ///< internal indications (IIN1 low octet, IIN2 high octet); 0 for a request
    const uint8_t *objects; ///< pointer into the fragment at the first object header (or nullptr if none)
    size_t obj_len;         ///< object octets remaining after the header
} Dnp3AppHeader;

/** @brief Compose an Application Control octet from the FIR/FIN/CON/UNS flags and a 4-bit sequence. */
uint8_t pc_dnp3_app_control(proto_bool fir, proto_bool fin, proto_bool con, proto_bool uns, uint8_t seq);

/**
 * @brief Build an application request fragment: AC + FC + @p obj_len object octets.
 * @return the fragment length (2 + @p obj_len), or 0 on a bad argument / overflow.
 */
size_t pc_dnp3_build_app_request(uint8_t *out, size_t cap, uint8_t app_control, uint8_t fc, const uint8_t *objects,
                                 size_t obj_len);

/**
 * @brief Build an application response fragment: AC + FC + IIN (2 octets, little-endian) + object octets.
 * @return the fragment length (4 + @p obj_len), or 0 on a bad argument / overflow.
 */
size_t pc_dnp3_build_app_response(uint8_t *out, size_t cap, uint8_t app_control, uint8_t fc, uint16_t iin,
                                  const uint8_t *objects, size_t obj_len);

/**
 * @brief Decode an application fragment's header (AC + FC, plus IIN for a response) into @p out.
 * @return true iff @p len covers the header (2 octets, or 4 for a response); false otherwise.
 */
proto_bool pc_dnp3_parse_app_header(const uint8_t *frag, size_t len, Dnp3AppHeader *out);

// --- object header (IEEE 1815 §4.3): group + variation + qualifier + range, after the function code ---

// Qualifier octet: the index-size / prefix code (bits 6-4) selects any per-object index prefix; the range
// specifier code (bits 3-0) selects the range field form.
#define DNP3_QUAL_PREFIX_MASK 0x70u
#define DNP3_QUAL_PREFIX_SHIFT 4u
#define DNP3_QUAL_RANGE_MASK 0x0Fu

// Range specifier codes (qualifier low nibble), common forms.
#define DNP3_RANGE_START_STOP_1 0x00u ///< 1-octet start + stop indexes
#define DNP3_RANGE_START_STOP_2 0x01u ///< 2-octet start + stop (little-endian)
#define DNP3_RANGE_START_STOP_4 0x02u ///< 4-octet start + stop (little-endian)
#define DNP3_RANGE_NO_RANGE 0x06u     ///< all objects (no range field)
#define DNP3_RANGE_COUNT_1 0x07u      ///< 1-octet object count
#define DNP3_RANGE_COUNT_2 0x08u      ///< 2-octet object count (little-endian)
#define DNP3_RANGE_COUNT_4 0x09u      ///< 4-octet object count (little-endian)

/** @brief A decoded object header (from pc_dnp3_parse_object_header). */
typedef struct
{
    uint8_t group;          ///< object group
    uint8_t variation;      ///< object variation
    uint8_t qualifier;      ///< the raw qualifier octet
    uint8_t prefix_code;    ///< index-size / prefix code (qualifier bits 6-4)
    uint8_t range_code;     ///< range specifier code (qualifier bits 3-0)
    proto_bool is_count;    ///< true when the range field is an object count; false for a start/stop range
    uint32_t start;         ///< range start index (start/stop forms; 0 otherwise)
    uint32_t stop;          ///< range stop index (start/stop forms; 0 otherwise)
    uint32_t count;         ///< object count (count forms, or stop-start+1 for a start/stop range)
    const uint8_t *objects; ///< the object data following the header, or nullptr if none
    size_t objects_len;     ///< octets remaining after the header
} Dnp3ObjectHeader;

/**
 * @brief Decode an object header (group + variation + qualifier + range) at the start of @p buf.
 * @return true iff the header and its range field fit in @p len, and the qualifier is a supported form
 *         (start-stop 0x00/0x01/0x02, no-range 0x06, or count 0x07/0x08/0x09); false otherwise.
 */
proto_bool pc_dnp3_parse_object_header(const uint8_t *buf, size_t len, Dnp3ObjectHeader *out);

/**
 * @brief Build an object header addressing a contiguous index range [@p start, @p stop]: group + variation +
 *        a start-stop qualifier, picking the smallest of the 1 / 2 / 4-octet range forms (0x00 / 0x01 / 0x02)
 *        that holds @p stop. The per-object index-prefix code is 0 (no prefix). @return octets written (5, 7,
 *        or 11), or 0 on @p stop < @p start, a null buffer, or overflow.
 */
size_t pc_dnp3_build_object_header_range(uint8_t *buf, size_t cap, uint8_t group, uint8_t variation, uint32_t start,
                                         uint32_t stop);

/**
 * @brief Build an all-objects object header (qualifier 0x06, no range field): group + variation + 0x06 - the
 *        common "read every object of this group" form (e.g. a Class-data poll of group 60). @return 3, or 0.
 */
size_t pc_dnp3_build_object_header_all(uint8_t *buf, size_t cap, uint8_t group, uint8_t variation);

// --- Control Relay Output Block (group 12 variation 1, IEEE 1815 Table): the 11-octet control object a
// SELECT / OPERATE carries to command a binary output (relay). ---

#define DNP3_CROB_LEN 11 ///< octets in a g12v1 CROB object

// Control-code operation type (the low nibble of the control-code octet).
#define DNP3_CROB_OP_NUL 0x00u       ///< no operation
#define DNP3_CROB_OP_PULSE_ON 0x01u  ///< energize for on-time, then de-energize
#define DNP3_CROB_OP_PULSE_OFF 0x02u ///< de-energize for off-time, then energize
#define DNP3_CROB_OP_LATCH_ON 0x03u  ///< energize and hold
#define DNP3_CROB_OP_LATCH_OFF 0x04u ///< de-energize and hold

// Control-code trip/close code (bits 6-7 of the control-code octet).
#define DNP3_CROB_TCC_NUL 0x00u
#define DNP3_CROB_TCC_CLOSE 0x01u
#define DNP3_CROB_TCC_TRIP 0x02u

/**
 * @brief Build a Control Relay Output Block (g12v1) object: control code (@p op_type in the low nibble, the
 *        @p clear bit, and @p tcc in bits 6-7), @p count, the on / off time in milliseconds (each a 4-octet
 *        little-endian field), and a status octet (0 in a request). This is the control object itself; wrap it
 *        with an object header + index prefix inside a SELECT / OPERATE request.
 * @return DNP3_CROB_LEN (11), or 0 on a null buffer, an @p op_type > 0x0F, a @p tcc > 3, or an overflow.
 */
size_t pc_dnp3_build_crob(uint8_t *buf, size_t cap, uint8_t op_type, uint8_t tcc, proto_bool clear, uint8_t count,
                          uint32_t on_time_ms, uint32_t off_time_ms);

// --- Analog Output Block (group 41): the analog counterpart to the CROB - the setpoint object a SELECT /
// OPERATE carries to command an analog output. Variation 1 is a 32-bit signed integer, variation 3 a 32-bit
// float; each carries the value (little-endian) followed by a 1-octet control status (0 in a request). ---

#define DNP3_AOB_LEN 5 ///< octets in a g41v1 (32-bit int) or g41v3 (32-bit float) Analog Output Block object

/**
 * @brief Build a 32-bit Analog Output Block (g41v1) object: a signed 32-bit setpoint value (little-endian,
 *        two's complement) followed by a status octet (0 in a request). Wrap it with an object header + index
 *        prefix inside a SELECT / OPERATE request; the outstation reports the result status in its response.
 * @return DNP3_AOB_LEN (5), or 0 on a null buffer or an overflow.
 */
size_t pc_dnp3_build_aob32(uint8_t *buf, size_t cap, int32_t value);

/**
 * @brief Build a 32-bit floating-point Analog Output Block (g41v3) object: an IEEE-754 single-precision
 *        setpoint value (little-endian) followed by a status octet (0 in a request).
 * @return DNP3_AOB_LEN (5), or 0 on a null buffer or an overflow.
 */
size_t pc_dnp3_build_aob_float(uint8_t *buf, size_t cap, float value);

#endif // PC_ENABLE_DNP3

#endif // PROTOCORE_DNP3_H
