// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file simatic.h
 * @brief Siemens SIMATIC serial point-to-point link (PC_ENABLE_SIMATIC) - the 3964R link protocol +
 *        the RK512 computer-link telegrams, zero-heap.
 *
 * The pre-Ethernet Siemens point-to-point link, two layers:
 *
 *  - **3964R** - a byte-oriented, half-duplex link protocol (S5/S7 PtP CP modules CP 341 / CP 441 /
 *    CP 524 / CP 525). A block is framed @code STX <data, DLE bytes doubled> DLE ETX [BCC] @endcode
 *    with an interactive per-block handshake: the sender emits STX and waits for the receiver's DLE
 *    (ready) before sending the block, then waits for a final DLE (ok) or NAK (retry). On a simultaneous
 *    STX collision the low-priority station yields. The "R" variant appends a BCC - the longitudinal XOR
 *    (even parity) of every character of the block after STX, i.e. the stuffed data and the terminating
 *    DLE ETX. A payload byte equal to DLE (0x10) is doubled (transparency); a doubled DLE contributes
 *    0x10 ^ 0x10 = 0 to the XOR, so it does not change the BCC.
 *
 *  - **RK512** - fixed-header request/reaction telegrams carried as the 3964R block payload: SEND (write
 *    words to the partner) and FETCH (read words from the partner), addressing a data block / flag / I-O
 *    area by number + word offset + count. Siemens words are big-endian.
 *
 * Pure, host-tested (native_simatic) against an independent python 3964R+RK512 reference peer; the
 * RS-232 / RS-485 UART is the application's (like the other serial-bus codecs). Control-char handshake,
 * QVZ/ZVZ timeouts, priority arbitration, BCC, and the RK512 header layout follow the Siemens "3964(R)
 * transmission protocol" and "RK 512 computer link" CP-module manuals.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SIMATIC_H
#define PROTOCORE_SIMATIC_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SIMATIC

// 3964R control characters (wire bytes).
#define SIMATIC_STX 0x02
#define SIMATIC_DLE 0x10
#define SIMATIC_ETX 0x03
#define SIMATIC_NAK 0x15

// ---------------------------------------------------------------------------
// 3964R block framing (pure builders/parsers - the state machine owns STX + the handshake)
// ---------------------------------------------------------------------------

/**
 * @brief 3964R BCC: the longitudinal XOR (even parity) over @p len bytes at @p data.
 *
 * The block-check is taken over every transmitted character after STX - the DLE-stuffed payload plus the
 * terminating DLE ETX - so callers pass exactly those bytes. XOR (not DF1's 2's-complement sum): a doubled
 * DLE pair cancels (0x10 ^ 0x10 = 0).
 */
uint8_t pc_3964r_bcc(const uint8_t *data, size_t len);

/**
 * @brief Build the 3964R block body: DLE-stuffed @p data, then DLE ETX, then (if @p with_bcc) the BCC.
 *
 * The STX and the connect handshake are emitted by the link state machine; this builds only the block body
 * a caller hands the state machine (or a test drives directly).
 * @return octets written, or 0 on overflow / bad input.
 */
size_t pc_3964r_build_block(uint8_t *buf, size_t cap, const uint8_t *data, size_t len, proto_bool with_bcc);

/**
 * @brief Parse + validate a 3964R block body (the bytes after STX): un-stuff the payload, check DLE ETX
 *        and the BCC (when @p with_bcc).
 * @param out     receives the de-stuffed payload.
 * @param out_cap capacity of @p out.
 * @param out_len receives the payload length.
 * @return true on a complete, check-valid block; false on bad framing, a lone control byte, truncation,
 *         an out overflow, or a BCC mismatch (fail-closed).
 */
proto_bool pc_3964r_parse_block(const uint8_t *buf, size_t len, proto_bool with_bcc, uint8_t *out, size_t out_cap,
                                size_t *out_len);

// ---------------------------------------------------------------------------
// 3964R link state machine (interactive half-duplex: STX/DLE handshake, retries, priority arbitration)
// ---------------------------------------------------------------------------

/** @brief Link state (one job in flight; half-duplex). */
typedef enum PROTO_ENUM_PACKED
{
    SIMATIC3964_STATE_IDLE,          ///< nothing in flight
    SIMATIC3964_STATE_TX_AWAIT_CONN, ///< sent STX, awaiting the partner's connect DLE (QVZ)
    SIMATIC3964_STATE_TX_AWAIT_END,  ///< sent the block, awaiting the partner's end DLE / NAK (QVZ)
    SIMATIC3964_STATE_RX_COLLECT     ///< replied DLE to a partner STX, collecting the block (ZVZ per-char)
} Simatic3964State;

/** @brief Sink for one outbound byte (the state machine writes to the UART through this). */
typedef void (*Simatic3964TxFn)(void *user, uint8_t byte);
/** @brief Delivery of a fully received, check-valid block payload. */
typedef void (*Simatic3964RxFn)(void *user, const uint8_t *data, size_t len);

/**
 * @brief 3964R link owner - all link state in one named context (no file-scope mutable). The tx/rx buffers
 *        are fixed BSS; @p user is threaded to the callbacks.
 */
typedef struct
{
    Simatic3964State state;
    proto_bool high_priority; ///< the priority bit; on an STX collision the low-priority side yields to receive
    proto_bool with_bcc;      ///< the "R" (BCC) variant
    Simatic3964TxFn tx;       ///< outbound-byte sink
    Simatic3964RxFn rx;       ///< received-block delivery
    void *user;               ///< passed to tx / rx

    uint8_t txbuf[PC_SIMATIC_BLOCK_MAX]; ///< the block body being sent (built once, re-sent on retry)
    size_t txlen;
    uint8_t rxbuf[PC_SIMATIC_BLOCK_MAX]; ///< raw inbound block body (pre un-stuffing)
    size_t rxpos;

    uint8_t block_retries; ///< block resends this connection (max 6)
    uint8_t conn_retries;  ///< connection reattempts (max 6)
    uint32_t deadline_ms;  ///< QVZ (handshake) / ZVZ (inter-char) expiry
    proto_bool prev_dle;   ///< rx terminator scan: previous rx byte was an un-paired DLE
    proto_bool await_bcc;  ///< rx: DLE ETX seen, the next byte is the BCC (R variant)
} Simatic3964Ctx;

/** @brief 3964R retry / timeout limits (Siemens defaults). */
#define SIMATIC_MAX_BLOCK_RETRY 6
#define SIMATIC_MAX_CONN_RETRY 6

/** @brief Initialize the link. @p high_priority: one end true, the other false (collision arbitration). */
void pc_3964r_init(Simatic3964Ctx *ctx, proto_bool high_priority, proto_bool with_bcc, Simatic3964TxFn tx,
                   Simatic3964RxFn rx, void *user);

/**
 * @brief Start sending @p data (one job in flight). Emits STX and arms the connect timeout.
 * @return true if accepted; false if a job is already in flight or @p len exceeds the block buffer.
 */
proto_bool pc_3964r_send(Simatic3964Ctx *ctx, const uint8_t *data, size_t len, uint32_t now_ms);

/** @brief Feed one inbound byte at @p now_ms; drives the handshake / block collection. */
void pc_3964r_rx_byte(Simatic3964Ctx *ctx, uint8_t b, uint32_t now_ms);

/** @brief Drive timeouts (QVZ/ZVZ) + retries; call periodically with the current time. */
void pc_3964r_tick(Simatic3964Ctx *ctx, uint32_t now_ms);

/** @brief True when no job is in flight and no block is being received. */
proto_bool pc_3964r_idle(const Simatic3964Ctx *ctx);

// ---------------------------------------------------------------------------
// RK512 computer-link telegrams (carried as the 3964R block payload; big-endian words)
// ---------------------------------------------------------------------------

/** @brief RK512 job / telegram identifier (the "Kennung" command byte). */
typedef enum PROTO_ENUM_PACKED
{
    RK512_CMD_SEND = 0x00,    ///< write words to the partner
    RK512_CMD_FETCH = 0x01,   ///< read words from the partner
    RK512_CMD_REACTION = 0x02 ///< the partner's reaction (acknowledge) telegram
} Rk512Cmd;

/** @brief RK512 memory area code (the operand area a job addresses). */
typedef enum PROTO_ENUM_PACKED
{
    RK512_AREA_DB = 0x01, ///< data block (DBNR selects which)
    RK512_AREA_DX = 0x02, ///< extended data block
    RK512_AREA_MB = 0x03, ///< flag / marker (M)
    RK512_AREA_EB = 0x04, ///< process input image (E)
    RK512_AREA_AB = 0x05, ///< process output image (A)
    RK512_AREA_PB = 0x06, ///< peripheral / I-O
    RK512_AREA_ZB = 0x07, ///< counter (Z)
    RK512_AREA_TB = 0x08  ///< timer (T)
} Rk512Area;

/** @brief A decoded RK512 header. */
typedef struct
{
    Rk512Cmd cmd;
    Rk512Area area;
    uint8_t dbnr;   ///< data-block number (when area is DB/DX)
    uint16_t addr;  ///< start word offset (DBADR)
    uint16_t count; ///< word count (ANZ)
} Rk512Header;

/**
 * @brief Build a SEND telegram header + the @p wcount big-endian data words at @p words.
 * @return octets written, or 0 on overflow / bad input.
 */
size_t pc_rk512_build_send(uint8_t *buf, size_t cap, Rk512Area area, uint8_t dbnr, uint16_t addr, const uint16_t *words,
                           uint16_t wcount);

/**
 * @brief Build a FETCH telegram header (no data words - the partner returns them).
 * @return octets written, or 0 on overflow / bad input.
 */
size_t pc_rk512_build_fetch(uint8_t *buf, size_t cap, Rk512Area area, uint8_t dbnr, uint16_t addr, uint16_t wcount);

/**
 * @brief Build a reaction (acknowledge) telegram carrying @p status (0 = ok).
 * @return octets written, or 0 on overflow.
 */
size_t pc_rk512_build_reaction(uint8_t *buf, size_t cap, uint16_t status);

/** @brief Parse an RK512 header off a telegram. @return true on a complete, valid header. */
proto_bool pc_rk512_parse_header(const uint8_t *buf, size_t len, Rk512Header *out);

/**
 * @brief Parse a reaction telegram: the status word, and (for a FETCH response) a pointer to the data
 *        words + their byte length inside @p buf.
 * @return true on a valid reaction telegram.
 */
proto_bool pc_rk512_parse_reaction(const uint8_t *buf, size_t len, uint16_t *status, const uint8_t **data,
                                   size_t *dlen);

#endif // PC_ENABLE_SIMATIC

PROTO_END_DECLS

#endif // PROTOCORE_SIMATIC_H
