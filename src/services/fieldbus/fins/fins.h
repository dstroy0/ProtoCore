// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fins.h
 * @brief Omron FINS frame codec (PC_ENABLE_FINS) - zero-heap command/response builder +
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

#include "protocore_config.h"

#if PC_ENABLE_FINS

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
    FINS_RUN_MODE_MONITOR = 0x02, ///< FINS_RUN_MODE_MONITOR mode (program runs, online edits allowed)
    FINS_RUN_MODE_RUN = 0x04,     ///< FINS_RUN_MODE_RUN mode (program runs, no online edits)
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

/** @brief Build a command frame: header + MRC + SRC + params. Returns total octets, or 0. */
size_t pc_fins_build_command(uint8_t *buf, size_t cap, const FinsHeader *h, uint8_t mrc, uint8_t src,
                             const uint8_t *params, size_t params_len);

/**
 * @brief Build a Memory Area Read command (0101): area code, 2-octet word address + bit,
 *        2-octet item count. The number of items is big-endian.
 */
size_t pc_fins_build_memory_area_read(uint8_t *buf, size_t cap, const FinsHeader *h, uint8_t area, uint16_t address,
                                      uint8_t bit, uint16_t count);

/**
 * @brief Build a Memory Area Write command (0102): the same area / word address + bit / item-count
 *        parameters as the read, followed by @p data_len octets of write data (word areas carry two octets
 *        per item, big-endian). @p count is the number of items (big-endian).
 * @return total octets written, or 0 on a null data pointer with a nonzero length, or an overflow.
 */
size_t pc_fins_build_memory_area_write(uint8_t *buf, size_t cap, const FinsHeader *h, uint8_t area, uint16_t address,
                                       uint8_t bit, uint16_t count, const uint8_t *data, size_t data_len);

/**
 * @brief Build a RUN command (0401): switches the PLC to @p mode. Parameters are the program number
 *        (0xFFFF, all programs) followed by the 1-octet mode code. @return total octets, or 0 on overflow.
 */
size_t pc_fins_build_run(uint8_t *buf, size_t cap, const FinsHeader *h, FinsRunMode mode);

/**
 * @brief Build a STOP command (0402): switches the PLC to PROGRAM mode (stops execution). The command
 *        carries no parameters. @return total octets, or 0 on overflow.
 */
size_t pc_fins_build_stop(uint8_t *buf, size_t cap, const FinsHeader *h);

/** @brief A parsed command (request side). @ref params points INTO the source buffer. */
typedef struct
{
    FinsHeader header;
    uint8_t mrc;
    uint8_t src;
    const uint8_t *params;
    size_t params_len;
} FinsCommand;

/** @brief Parse a command frame (header + MRC + SRC + params). */
proto_bool pc_fins_parse_command(const uint8_t *buf, size_t len, FinsCommand *out);

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

/** @brief Parse a response frame (header + MRC + SRC + MRES + SRES + data). */
proto_bool pc_fins_parse_response(const uint8_t *buf, size_t len, FinsResponse *out);

#endif // PC_ENABLE_FINS

#endif // PROTOCORE_FINS_H
