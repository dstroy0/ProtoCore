// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fins.h
 * @brief Omron FINS frame codec (PROTOCORE_ENABLE_FINS) - zero-heap command/response builder +
 *        parser for the Factory Interface Network Service (FINS/UDP), so a device can talk
 *        to an Omron PLC over the shipped UDP transport.
 *
 * A FINS message is a 10-octet header then the command code and data:
 * @code
 *   ICF RSV GCT  DNA DA1 DA2  SNA SA1 SA2  SID   MRC SRC  [params / data...]
 * @endcode
 *  - ICF: bit 6 = command(0)/response(1), bit 0 = response required(0)/not(1), bit 7 = use
 *    gateway. RSV = 0, GCT = 0x02. DNA/DA1/DA2 = destination net/node/unit; SNA/SA1/SA2 =
 *    source; SID = service id (echoed in the response).
 *  - MRC/SRC are the main/sub command code. A response inserts a 2-octet end code
 *    (MRES/SRES) before its data; MRES = SRES = 0 means normal completion.
 *  - Multi-octet command parameters (addresses, counts) are big-endian.
 *
 * FINS/UDP carries this frame directly (UDP provides integrity, so there is no checksum);
 * FINS/TCP would prepend its own header. This is the message codec; the send is the app's.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FINS_H
#define PROTOCORE_FINS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_FINS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define FINS_HEADER_SIZE 10

#define FINS_ICF_COMMAND 0x80     ///< command, response required, gateway
#define FINS_ICF_RESPONSE 0xC0    ///< response
#define FINS_ICF_NO_RESPONSE 0x01 ///< OR into ICF: response not required

// Common command codes (MRC, SRC).
#define FINS_MRC_MEMORY_AREA 0x01
#define FINS_SRC_MEMORY_AREA_READ 0x01
#define FINS_SRC_MEMORY_AREA_WRITE 0x02
#define FINS_MRC_OPERATING_MODE 0x04
#define FINS_SRC_RUN 0x01
#define FINS_SRC_STOP 0x02

/** @brief The operating mode requested by a RUN (0401) command. */
typedef enum PROTO_ENUM_PACKED
{
    FINS_RUN_MODE_MONITOR = 0x02, ///< MONITOR mode (program runs, online edits allowed)
    FINS_RUN_MODE_RUN = 0x04,     ///< RUN mode (program runs, no online edits)
} FinsRunMode;

/** @brief The 10-octet FINS routing header. */
typedef struct
{
    uint8_t icf;
    uint8_t rsv;
    uint8_t gct;
    uint8_t dna;
    uint8_t da1;
    uint8_t da2; ///< destination network / node / unit
    uint8_t sna;
    uint8_t sa1;
    uint8_t sa2; ///< source network / node / unit
    uint8_t sid; ///< service id
} FinsHeader;

/** @brief A parsed command (request side). @ref params points INTO the source buffer. */
typedef struct
{
    FinsHeader header;
    uint8_t mrc;
    uint8_t src;
    const uint8_t *params;
    size_t params_len;
} FinsCommand;

/** @brief A parsed response. @ref data points INTO the source buffer. */
typedef struct
{
    FinsHeader header;
    uint8_t mrc;
    uint8_t src; ///< echoed command code
    uint8_t mres;
    uint8_t sres; ///< end code (0/0 = normal completion)
    const uint8_t *data;
    size_t data_len;
} FinsResponse;

/** @brief What build_command takes: buf, cap, h, mrc, src, params, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const FinsHeader *h;
    uint8_t mrc;
    uint8_t src;
    const uint8_t *params;
    size_t params_len;
} FinsBuildCommandArgs;

/** @brief What build_memory_area_read takes: buf, cap, h, area, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const FinsHeader *h;
    uint8_t area;
    uint16_t address;
    uint8_t bit;
    uint16_t count;
} FinsBuildMemoryAreaReadArgs;

/** @brief What build_memory_area_write takes: buf, cap, h, area, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const FinsHeader *h;
    uint8_t area;
    uint16_t address;
    uint8_t bit;
    uint16_t count;
    const uint8_t *data;
    size_t data_len;
} FinsBuildMemoryAreaWriteArgs;

/** @brief What build_run takes: buf, cap, h, mode. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const FinsHeader *h;
    FinsRunMode mode;
} FinsBuildRunArgs;

/** @brief What build_stop takes: buf, cap, h. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const FinsHeader *h;
} FinsBuildStopArgs;

/** @brief What parse_command takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    FinsCommand *out;
} FinsParseCommandArgs;

/** @brief What parse_response takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    FinsResponse *out;
} FinsParseResponseArgs;

/**
 * @brief Omron FINS frame codec (PROTOCORE_ENABLE_FINS) - zero-heap command/response builder + parser for the Factory
 * Interface Network Service (FINS/UDP), so a device can talk to an Omron PLC over the shipped UDP transport.
 *
 * A caller sets the members a call takes, invokes it through ::Fins with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Fins.build_command_args.buf = ...;
 *   Fins.build_command_args.cap = ...;
 *   Fins.build_command_args.h = ...;
 *   Fins.build_command_args.mrc = ...;
 *   Fins.build_command_args.src = ...;
 *   Fins.build_command_args.params = ...;
 *   Fins.build_command_args.params_len = ...;
 *   Fins.build_command(work);
 *   // Fins.n is what the call reports
 *
 * @var FinsNs::build_command_args  what build_command takes: buf, cap, h, mrc, src, params,
 * @var FinsNs::build_memory_area_read_args  what build_memory_area_read takes: buf, cap, h, area,
 * @var FinsNs::build_memory_area_write_args  what build_memory_area_write takes: buf, cap, h, area,
 * @var FinsNs::build_run_args  what build_run takes: buf, cap, h, mode
 * @var FinsNs::build_stop_args  what build_stop takes: buf, cap, h
 * @var FinsNs::parse_command_args  what parse_command takes: buf, len, out
 * @var FinsNs::parse_response_args  what parse_response takes: buf, len, out
 * @var FinsNs::ok  a call's true/false outcome
 * @var FinsNs::n  total octets written, or 0 on a null data pointer with a nonzero ...
 * @var FinsNs::build_command  build a command frame: header + MRC + SRC + params. Returns total ...
 * @var FinsNs::build_memory_area_read  build a Memory Area Read command (0101): area code, 2-octet word ...
 * @var FinsNs::build_memory_area_write  build a Memory Area Write command (0102): the same area / word ...
 * @var FinsNs::build_run  build a RUN command (0401): switches the PLC to mode. Parameters ...
 * @var FinsNs::build_stop  build a STOP command (0402): switches the PLC to PROGRAM mode ...
 * @var FinsNs::parse_command  parse a command frame (header + MRC + SRC + params)
 * @var FinsNs::parse_response  parse a response frame (header + MRC + SRC + MRES + SRES + data)
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    FinsBuildCommandArgs build_command_args;
    FinsBuildMemoryAreaReadArgs build_memory_area_read_args;
    FinsBuildMemoryAreaWriteArgs build_memory_area_write_args;
    FinsBuildRunArgs build_run_args;
    FinsBuildStopArgs build_stop_args;
    FinsParseCommandArgs parse_command_args;
    FinsParseResponseArgs parse_response_args;

    proto_bool ok;
    size_t n;

    void (*const build_command)(uint8_t *restrict work);
    void (*const build_memory_area_read)(uint8_t *restrict work);
    void (*const build_memory_area_write)(uint8_t *restrict work);
    void (*const build_run)(uint8_t *restrict work);
    void (*const build_stop)(uint8_t *restrict work);
    void (*const parse_command)(uint8_t *restrict work);
    void (*const parse_response)(uint8_t *restrict work);
} FinsNs;

/** @brief The one symbol this module exports. */
extern FinsNs Fins;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FINS

#endif // PROTOCORE_FINS_H
