// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file canopen.h
 * @brief CANopen (CiA 301) application-layer message codec (PROTOCORE_ENABLE_CANOPEN).
 *
 * A pure, zero-heap builder + parser for the CANopen messaging set carried over classic
 * CAN frames (see shared/can/can.h): NMT node control, SYNC, TIME, the
 * heartbeat / boot-up (NMT error control), EMCY, PDO (process data), and expedited SDO
 * (service data object) read / write / abort. The 11-bit CAN identifier is a 4-bit
 * function code plus a 7-bit node id; each builder computes the right COB-ID and each
 * parser classifies a received frame back to its function + node.
 *
 * Scope: the CANopen object dictionary itself is the application's; this is the wire codec.
 * SDO transfers cover expedited (<= 4 octets) and segmented (CiA 301 §7.2.4.3, larger objects via a
 * toggling run of 7-octet segments); block SDO is not covered.
 *
 * Bridging: pair with the ESP32's TWAI peripheral (or an MCP2515 over SPI) to bridge a
 * CANopen field bus onto Wi-Fi - expose node state / PDOs over HTTP, MQTT, or a WebSocket.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CANOPEN_H
#define PROTOCORE_CANOPEN_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CANOPEN

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// Function-code COB-ID bases. The 11-bit id is (function-code | node-id); the node id is
// 1..127 (0 = broadcast for NMT / SYNC / TIME). EMCY shares 0x080 with SYNC: SYNC is the
// node-id == 0 case, EMCY is 0x081..0x0FF.
#define CANOPEN_COB_NMT 0x000u       ///< NMT node control (broadcast), 2 data octets
#define CANOPEN_COB_SYNC 0x080u      ///< SYNC (broadcast), 0 data octets
#define CANOPEN_COB_EMCY 0x080u      ///< EMCY base (+ node id)
#define CANOPEN_COB_TIME 0x100u      ///< TIME stamp (broadcast)
#define CANOPEN_COB_TPDO1 0x180u     ///< transmit PDO 1 base (+ node id)
#define CANOPEN_COB_RPDO1 0x200u     ///< receive PDO 1 base (+ node id)
#define CANOPEN_COB_TPDO2 0x280u     ///< transmit PDO 2 base
#define CANOPEN_COB_RPDO2 0x300u     ///< receive PDO 2 base
#define CANOPEN_COB_TPDO3 0x380u     ///< transmit PDO 3 base
#define CANOPEN_COB_RPDO3 0x400u     ///< receive PDO 3 base
#define CANOPEN_COB_TPDO4 0x480u     ///< transmit PDO 4 base
#define CANOPEN_COB_RPDO4 0x500u     ///< receive PDO 4 base
#define CANOPEN_COB_SDO_TX 0x580u    ///< SDO server -> client (response), + node id
#define CANOPEN_COB_SDO_RX 0x600u    ///< SDO client -> server (request), + node id
#define CANOPEN_COB_HEARTBEAT 0x700u ///< NMT error control (heartbeat / boot-up), + node id
#define CANOPEN_FUNC_MASK 0x780u     ///< top 4 bits select the function code
#define CANOPEN_NODE_MASK 0x07Fu     ///< low 7 bits select the node id

// NMT node-control commands (CANOPEN_COB_NMT data[0]).
#define CANOPEN_NMT_START 0x01u      ///< enter Operational
#define CANOPEN_NMT_STOP 0x02u       ///< enter Stopped
#define CANOPEN_NMT_PRE_OP 0x80u     ///< enter Pre-operational
#define CANOPEN_NMT_RESET_NODE 0x81u ///< reset application
#define CANOPEN_NMT_RESET_COMM 0x82u ///< reset communication

// NMT states reported in a heartbeat (data[0], low 7 bits).
#define CANOPEN_STATE_BOOTUP 0x00u
#define CANOPEN_STATE_STOPPED 0x04u
#define CANOPEN_STATE_OPERATIONAL 0x05u
#define CANOPEN_STATE_PRE_OP 0x7Fu

// SDO command specifier (high 3 bits of data[0]).
#define CANOPEN_SDO_CCS_DOWNLOAD 1u ///< client download initiate (write)
#define CANOPEN_SDO_CCS_UPLOAD 2u   ///< client upload initiate (read)
#define CANOPEN_SDO_SCS_UPLOAD 2u   ///< server upload initiate response
#define CANOPEN_SDO_SCS_DOWNLOAD 3u ///< server download initiate response (ack)
#define CANOPEN_SDO_ABORT 4u        ///< abort transfer (either direction)

// A few common SDO abort codes (the field is any 32-bit value).
#define CANOPEN_ABORT_TOGGLE 0x05030000u      ///< toggle bit not alternated
#define CANOPEN_ABORT_TIMEOUT 0x05040000u     ///< SDO protocol timed out
#define CANOPEN_ABORT_NO_OBJECT 0x06020000u   ///< object does not exist
#define CANOPEN_ABORT_NO_SUBINDEX 0x06090011u ///< sub-index does not exist
#define CANOPEN_ABORT_GENERAL 0x08000000u     ///< general error

/** @brief Octets in a TIME message (CiA 301 TIME_OF_DAY: 4-octet ms + 2-octet days). */
#define CANOPEN_TIME_LEN 6

/** @brief The ms-after-midnight field is 28 bits; the top 4 bits of the U32 are reserved. */
#define CANOPEN_TIME_MS_MASK 0x0FFFFFFFu

/** @brief Octets of object data a single SDO segment carries. */
#define CANOPEN_SDO_SEG_DATA 7

/** @brief CANopen message classes (the function decoded from the COB-ID). */
typedef enum PROTO_ENUM_PACKED
{
    CANOPEN_T_UNKNOWN = 0,
    CANOPEN_T_NMT,
    CANOPEN_T_SYNC,
    CANOPEN_T_EMCY,
    CANOPEN_T_TIME,
    CANOPEN_T_TPDO,
    CANOPEN_T_RPDO,
    CANOPEN_T_SDO_TX,
    CANOPEN_T_SDO_RX,
    CANOPEN_T_HEARTBEAT,
} CanopenType;

/** @brief A classified CANopen frame (the function code + node, from protocore_canopen_parse). */
typedef struct
{
    CanopenType type;
    uint8_t node_id; ///< 1..127, or 0 for a broadcast (NMT / SYNC / TIME)
    uint8_t pdo_num; ///< 1..4 for TPDO / RPDO, else 0
} CanopenMsg;

/** @brief A decoded SDO initiate response (from protocore_canopen_parse_sdo_response). */
typedef struct
{
    uint16_t index;       ///< object index echoed by the server
    uint8_t sub;          ///< sub-index echoed by the server
    proto_bool is_abort;  ///< true => the server aborted the transfer
    uint32_t abort_code;  ///< valid when is_abort
    proto_bool is_upload; ///< true => upload (read) response; false => download (write) ack
    proto_bool expedited; ///< true => the payload is inline in data[0..len-1]
    uint8_t data[4];      ///< expedited upload payload
    uint8_t len;          ///< expedited payload length 0..4
} CanopenSdoResponse;

/** @brief Decoded CANopen TIME_OF_DAY (the TIME message payload, CiA 301 §7.2.6). The CANopen epoch is
 *  January 1, 1984, so @c days_since_1984 plus @c ms_since_midnight locate an absolute instant. */
typedef struct
{
    uint32_t ms_since_midnight; ///< milliseconds after midnight (28-bit; 0..86'399'999)
    uint16_t days_since_1984;   ///< days since January 1, 1984
} CanopenTime;

/** @brief Segmented-upload reassembly state (accumulates segment data into a caller buffer). */
typedef struct
{
    uint8_t *buf;             ///< caller-provided accumulation buffer
    size_t cap;               ///< its capacity
    size_t len;               ///< octets accumulated so far
    proto_bool expect_toggle; ///< the toggle the next segment must carry (starts false)
    proto_bool done;          ///< set once the last segment is accepted
} CanopenSdoReasm;

#include "shared/can/can.h" // CanFrame: the type a parameter points at

/** @brief What build_nmt takes: out, command, node_id. */
typedef struct
{
    CanFrame *out;
    uint8_t command;
    uint8_t node_id;
} CanopenBuildNmtArgs;

/** @brief What build_sync takes: out. */
typedef struct
{
    CanFrame *out;
} CanopenBuildSyncArgs;

/** @brief What build_time takes: out, ms_since_midnight, ... */
typedef struct
{
    CanFrame *out;
    uint32_t ms_since_midnight;
    uint16_t days_since_1984;
} CanopenBuildTimeArgs;

/** @brief What build_heartbeat takes: out, node_id, state. */
typedef struct
{
    CanFrame *out;
    uint8_t node_id;
    uint8_t state;
} CanopenBuildHeartbeatArgs;

/** @brief What build_emcy takes: out, node_id, error_code, error_reg, ... */
typedef struct
{
    CanFrame *out;
    uint8_t node_id;
    uint16_t error_code;
    uint8_t error_reg;
    const uint8_t *msef; ///< 5 bytes.
} CanopenBuildEmcyArgs;

/** @brief What build_tpdo takes: out, pdo_num, node_id, data, len. */
typedef struct
{
    CanFrame *out;
    uint8_t pdo_num;
    uint8_t node_id;
    const uint8_t *data;
    uint8_t len;
} CanopenBuildTpdoArgs;

/** @brief What build_rpdo takes: out, pdo_num, node_id, data, len. */
typedef struct
{
    CanFrame *out;
    uint8_t pdo_num;
    uint8_t node_id;
    const uint8_t *data;
    uint8_t len;
} CanopenBuildRpdoArgs;

/** @brief What build_sdo_read takes: out, node_id, index, sub. */
typedef struct
{
    CanFrame *out;
    uint8_t node_id;
    uint16_t index;
    uint8_t sub;
} CanopenBuildSdoReadArgs;

/** @brief What build_sdo_write takes: out, node_id, index, sub, data, ... */
typedef struct
{
    CanFrame *out;
    uint8_t node_id;
    uint16_t index;
    uint8_t sub;
    const uint8_t *data;
    uint8_t len;
} CanopenBuildSdoWriteArgs;

/** @brief What build_sdo_abort takes: out, node_id, index, sub, ... */
typedef struct
{
    CanFrame *out;
    uint8_t node_id;
    uint16_t index;
    uint8_t sub;
    uint32_t abort_code;
    proto_bool to_server;
} CanopenBuildSdoAbortArgs;

/** @brief What parse takes: f, out. */
typedef struct
{
    const CanFrame *f;
    CanopenMsg *out;
} CanopenParseArgs;

/** @brief What parse_emcy takes: f, node_id, error_code, error_reg, ... */
typedef struct
{
    const CanFrame *f;
    uint8_t *node_id;
    uint16_t *error_code;
    uint8_t *error_reg;
    uint8_t *msef; ///< 5 bytes.
} CanopenParseEmcyArgs;

/** @brief What parse_heartbeat takes: f, node_id, state. */
typedef struct
{
    const CanFrame *f;
    uint8_t *node_id;
    uint8_t *state;
} CanopenParseHeartbeatArgs;

/** @brief What parse_time takes: f, out. */
typedef struct
{
    const CanFrame *f;
    CanopenTime *out;
} CanopenParseTimeArgs;

/** @brief What parse_sdo_response takes: f, out. */
typedef struct
{
    const CanFrame *f;
    CanopenSdoResponse *out;
} CanopenParseSdoResponseArgs;

/** @brief What build_sdo_download_init takes: out, node_id, index, ... */
typedef struct
{
    CanFrame *out;
    uint8_t node_id;
    uint16_t index;
    uint8_t sub;
    uint32_t total_size;
} CanopenBuildSdoDownloadInitArgs;

/** @brief What build_sdo_download_segment takes: out, node_id, ... */
typedef struct
{
    CanFrame *out;
    uint8_t node_id;
    proto_bool toggle; ///< the toggle bit for this segment (0 for the first, then alternating)
    const uint8_t *data;
    uint8_t len;
    proto_bool last; ///< true on the final segment
} CanopenBuildSdoDownloadSegmentArgs;

/** @brief What build_sdo_upload_segment_req takes: out, node_id, ... */
typedef struct
{
    CanFrame *out;
    uint8_t node_id;
    proto_bool toggle;
} CanopenBuildSdoUploadSegmentReqArgs;

/** @brief What parse_sdo_segment takes: f, toggle, data, len, last. */
typedef struct
{
    const CanFrame *f;
    proto_bool *toggle;
    uint8_t *data;
    uint8_t *len;
    proto_bool *last;
} CanopenParseSdoSegmentArgs;

/** @brief What sdo_reasm_init takes: r, buf, cap. */
typedef struct
{
    CanopenSdoReasm *r;
    uint8_t *buf;
    size_t cap;
} CanopenSdoReasmInitArgs;

/** @brief What sdo_reasm_feed takes: r, data, len, toggle, last. */
typedef struct
{
    CanopenSdoReasm *r;
    const uint8_t *data;
    uint8_t len;
    proto_bool toggle;
    proto_bool last;
} CanopenSdoReasmFeedArgs;

/**
 * @brief CANopen (CiA 301) application-layer message codec (PROTOCORE_ENABLE_CANOPEN).
 *
 * A caller sets the members a call takes, invokes it through ::Canopen with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Canopen.build_nmt_args.out = ...;
 *   Canopen.build_nmt_args.command = ...;
 *   Canopen.build_nmt_args.node_id = ...;
 *   Canopen.build_nmt(work);
 *   // Canopen.ok is what the call reports
 *
 * @var CanopenNs::build_nmt_args  what build_nmt takes: out, command, node_id
 * @var CanopenNs::build_sync_args  what build_sync takes: out
 * @var CanopenNs::build_time_args  what build_time takes: out, ms_since_midnight,
 * @var CanopenNs::build_heartbeat_args  what build_heartbeat takes: out, node_id, state
 * @var CanopenNs::build_emcy_args  what build_emcy takes: out, node_id, error_code, error_reg,
 * @var CanopenNs::build_tpdo_args  what build_tpdo takes: out, pdo_num, node_id, data, len
 * @var CanopenNs::build_rpdo_args  what build_rpdo takes: out, pdo_num, node_id, data, len
 * @var CanopenNs::build_sdo_read_args  what build_sdo_read takes: out, node_id, index, sub
 * @var CanopenNs::build_sdo_write_args  what build_sdo_write takes: out, node_id, index, sub, data,
 * @var CanopenNs::build_sdo_abort_args  what build_sdo_abort takes: out, node_id, index, sub,
 * @var CanopenNs::parse_args  what parse takes: f, out
 * @var CanopenNs::parse_emcy_args  what parse_emcy takes: f, node_id, error_code, error_reg,
 * @var CanopenNs::parse_heartbeat_args  what parse_heartbeat takes: f, node_id, state
 * @var CanopenNs::parse_time_args  what parse_time takes: f, out
 * @var CanopenNs::parse_sdo_response_args  what parse_sdo_response takes: f, out
 * @var CanopenNs::build_sdo_download_init_args  what build_sdo_download_init takes: out, node_id, index,
 * @var CanopenNs::build_sdo_download_segment_args  what build_sdo_download_segment takes: out, node_id,
 * @var CanopenNs::build_sdo_upload_segment_req_args  what build_sdo_upload_segment_req takes: out, node_id,
 * @var CanopenNs::parse_sdo_segment_args  what parse_sdo_segment takes: f, toggle, data, len, last
 * @var CanopenNs::sdo_reasm_init_args  what sdo_reasm_init takes: r, buf, cap
 * @var CanopenNs::sdo_reasm_feed_args  what sdo_reasm_feed takes: r, data, len, toggle, last
 * @var CanopenNs::ok  true iff f is an 8-octet frame whose command specifier is the ...
 * @var CanopenNs::build_nmt  NMT node-control frame. node_id 0 addresses all nodes
 * @var CanopenNs::build_sync  SYNC frame (zero-length, broadcast)
 * @var CanopenNs::build_time  TIME frame (broadcast): the TIME_OF_DAY (ms_since_midnight masked ...
 * @var CanopenNs::build_heartbeat  heartbeat / boot-up frame for node_id reporting state
 * @var CanopenNs::build_emcy  emergency (EMCY) frame: 16-bit error code (LE), error register, 5 ...
 * @var CanopenNs::build_tpdo  Transmit-PDO frame (pdo_num 1..4): up to 8 raw mapped octets
 * @var CanopenNs::build_rpdo  Receive-PDO frame (pdo_num 1..4): up to 8 raw mapped octets
 * @var CanopenNs::build_sdo_read  SDO expedited upload (read) request for object index / sub on ...
 * @var CanopenNs::build_sdo_write  SDO expedited download (write) of len (1..4) octets to index / sub
 * @var CanopenNs::build_sdo_abort  SDO abort frame. to_server true => client->server (0x600), false => ...
 * @var CanopenNs::parse  classify any frame by COB-ID into its CANopen function + node
 * @var CanopenNs::parse_emcy  decode an EMCY frame (must be a 0x080+node, 8-octet frame)
 * @var CanopenNs::parse_heartbeat  decode a heartbeat frame (0x700+node, 1 octet)
 * @var CanopenNs::parse_time  decode a TIME frame (0x100, 6 octets) into out. true iff f is the ...
 * @var CanopenNs::parse_sdo_response  decode an SDO server response (0x580+node): upload data, download ...
 * @var CanopenNs::build_sdo_download_init  build a segmented SDO download (write) initiate: names the object ...
 * @var CanopenNs::build_sdo_download_segment  build a segmented SDO download segment carrying len (1..7) octets ...
 * @var CanopenNs::build_sdo_upload_segment_req  build a segmented SDO upload segment request (client asks the ...
 * @var CanopenNs::parse_sdo_segment  decode an SDO segment frame (either direction) into its toggle, ...
 * @var CanopenNs::sdo_reasm_init  begin a segmented-upload reassembly with a caller-owned buffer
 * @var CanopenNs::sdo_reasm_feed  feed one decoded upload segment into the reassembler (append len ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    CanopenBuildNmtArgs build_nmt_args;
    CanopenBuildSyncArgs build_sync_args;
    CanopenBuildTimeArgs build_time_args;
    CanopenBuildHeartbeatArgs build_heartbeat_args;
    CanopenBuildEmcyArgs build_emcy_args;
    CanopenBuildTpdoArgs build_tpdo_args;
    CanopenBuildRpdoArgs build_rpdo_args;
    CanopenBuildSdoReadArgs build_sdo_read_args;
    CanopenBuildSdoWriteArgs build_sdo_write_args;
    CanopenBuildSdoAbortArgs build_sdo_abort_args;
    CanopenParseArgs parse_args;
    CanopenParseEmcyArgs parse_emcy_args;
    CanopenParseHeartbeatArgs parse_heartbeat_args;
    CanopenParseTimeArgs parse_time_args;
    CanopenParseSdoResponseArgs parse_sdo_response_args;
    CanopenBuildSdoDownloadInitArgs build_sdo_download_init_args;
    CanopenBuildSdoDownloadSegmentArgs build_sdo_download_segment_args;
    CanopenBuildSdoUploadSegmentReqArgs build_sdo_upload_segment_req_args;
    CanopenParseSdoSegmentArgs parse_sdo_segment_args;
    CanopenSdoReasmInitArgs sdo_reasm_init_args;
    CanopenSdoReasmFeedArgs sdo_reasm_feed_args;
    proto_bool ok;
} CanopenVars;

/** @brief The operands and the outcome. */
extern CanopenVars CanopenV;

/** @brief The entries. */
typedef struct
{
    void (*const build_nmt)(uint8_t *restrict work);
    void (*const build_sync)(uint8_t *restrict work);
    void (*const build_time)(uint8_t *restrict work);
    void (*const build_heartbeat)(uint8_t *restrict work);
    void (*const build_emcy)(uint8_t *restrict work);
    void (*const build_tpdo)(uint8_t *restrict work);
    void (*const build_rpdo)(uint8_t *restrict work);
    void (*const build_sdo_read)(uint8_t *restrict work);
    void (*const build_sdo_write)(uint8_t *restrict work);
    void (*const build_sdo_abort)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const parse_emcy)(uint8_t *restrict work);
    void (*const parse_heartbeat)(uint8_t *restrict work);
    void (*const parse_time)(uint8_t *restrict work);
    void (*const parse_sdo_response)(uint8_t *restrict work);
    void (*const build_sdo_download_init)(uint8_t *restrict work);
    void (*const build_sdo_download_segment)(uint8_t *restrict work);
    void (*const build_sdo_upload_segment_req)(uint8_t *restrict work);
    void (*const parse_sdo_segment)(uint8_t *restrict work);
    void (*const sdo_reasm_init)(uint8_t *restrict work);
    void (*const sdo_reasm_feed)(uint8_t *restrict work);
} CanopenNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in CanopenV or a region of the borrow at a fixed offset.
void protocore_canopen_build_nmt(uint8_t *restrict work);
void protocore_canopen_build_sync(uint8_t *restrict work);
void protocore_canopen_build_time(uint8_t *restrict work);
void protocore_canopen_build_heartbeat(uint8_t *restrict work);
void protocore_canopen_build_emcy(uint8_t *restrict work);
void protocore_canopen_build_tpdo(uint8_t *restrict work);
void protocore_canopen_build_rpdo(uint8_t *restrict work);
void protocore_canopen_build_sdo_read(uint8_t *restrict work);
void protocore_canopen_build_sdo_write(uint8_t *restrict work);
void protocore_canopen_build_sdo_abort(uint8_t *restrict work);
void protocore_canopen_parse(uint8_t *restrict work);
void protocore_canopen_parse_emcy(uint8_t *restrict work);
void protocore_canopen_parse_heartbeat(uint8_t *restrict work);
void protocore_canopen_parse_time(uint8_t *restrict work);
void protocore_canopen_parse_sdo_response(uint8_t *restrict work);
void protocore_canopen_build_sdo_download_init(uint8_t *restrict work);
void protocore_canopen_build_sdo_download_segment(uint8_t *restrict work);
void protocore_canopen_build_sdo_upload_segment_req(uint8_t *restrict work);
void protocore_canopen_parse_sdo_segment(uint8_t *restrict work);
void protocore_canopen_sdo_reasm_init(uint8_t *restrict work);
void protocore_canopen_sdo_reasm_feed(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Canopen.build_nmt(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const CanopenNs Canopen __attribute__((unused)) = {
    .build_nmt = protocore_canopen_build_nmt,
    .build_sync = protocore_canopen_build_sync,
    .build_time = protocore_canopen_build_time,
    .build_heartbeat = protocore_canopen_build_heartbeat,
    .build_emcy = protocore_canopen_build_emcy,
    .build_tpdo = protocore_canopen_build_tpdo,
    .build_rpdo = protocore_canopen_build_rpdo,
    .build_sdo_read = protocore_canopen_build_sdo_read,
    .build_sdo_write = protocore_canopen_build_sdo_write,
    .build_sdo_abort = protocore_canopen_build_sdo_abort,
    .parse = protocore_canopen_parse,
    .parse_emcy = protocore_canopen_parse_emcy,
    .parse_heartbeat = protocore_canopen_parse_heartbeat,
    .parse_time = protocore_canopen_parse_time,
    .parse_sdo_response = protocore_canopen_parse_sdo_response,
    .build_sdo_download_init = protocore_canopen_build_sdo_download_init,
    .build_sdo_download_segment = protocore_canopen_build_sdo_download_segment,
    .build_sdo_upload_segment_req = protocore_canopen_build_sdo_upload_segment_req,
    .parse_sdo_segment = protocore_canopen_parse_sdo_segment,
    .sdo_reasm_init = protocore_canopen_sdo_reasm_init,
    .sdo_reasm_feed = protocore_canopen_sdo_reasm_feed,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CANOPEN

#endif // PROTOCORE_CANOPEN_H
