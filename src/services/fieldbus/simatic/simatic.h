// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file simatic.h
 * @brief Siemens SIMATIC serial point-to-point link (PROTOCORE_ENABLE_SIMATIC) - the 3964R link protocol +
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SIMATIC

PROTOCORE_BEGIN_DECLS

// PROTOCORE_SIMATIC_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

// 3964R control characters (wire bytes).
#define SIMATIC_STX 0x02
#define SIMATIC_DLE 0x10
#define SIMATIC_ETX 0x03
#define SIMATIC_NAK 0x15

// 3964R retry limits: Siemens RepetitionAttempts and BuildupAttempts, both default 6.
#define SIMATIC_MAX_BLOCK_RETRY 6
#define SIMATIC_MAX_CONN_RETRY 6

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

    uint8_t txbuf[PROTOCORE_SIMATIC_BLOCK_MAX]; ///< the block body being sent (built once, re-sent on retry)
    size_t txlen;
    uint8_t rxbuf[PROTOCORE_SIMATIC_BLOCK_MAX]; ///< raw inbound block body (pre un-stuffing)
    size_t rxpos;

    uint8_t block_retries; ///< block resends this connection (max 6)
    uint8_t conn_retries;  ///< connection reattempts (max 6)
    uint32_t deadline_ms;  ///< QVZ (handshake) / ZVZ (inter-char) expiry
    proto_bool prev_dle;   ///< rx terminator scan: previous rx byte was an un-paired DLE
    proto_bool await_bcc;  ///< rx: DLE ETX seen, the next byte is the BCC (R variant)
} Simatic3964Ctx;

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

/** @brief What bcc_3964r takes: data, len. */
typedef struct
{
    const uint8_t *data;
    size_t len;
} SimaticBcc3964rArgs;

/** @brief What build_block_3964r takes: buf, cap, data, len, with_bcc. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *data;
    size_t len;
    proto_bool with_bcc;
} SimaticBuildBlock3964rArgs;

/** @brief What parse_block_3964r takes: buf, len, with_bcc, out, ... */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    proto_bool with_bcc;
    uint8_t *out;    ///< receives the de-stuffed payload
    size_t out_cap;  ///< capacity of out
    size_t *out_len; ///< receives the payload length
} SimaticParseBlock3964rArgs;

/** @brief What init_3964r takes: ctx, high_priority, with_bcc, tx, ... */
typedef struct
{
    Simatic3964Ctx *ctx;
    proto_bool high_priority;
    proto_bool with_bcc;
    Simatic3964TxFn tx;
    Simatic3964RxFn rx;
    void *user;
} SimaticInit3964rArgs;

/** @brief What send_3964r takes: ctx, data, len, now_ms. */
typedef struct
{
    Simatic3964Ctx *ctx;
    const uint8_t *data;
    size_t len;
    uint32_t now_ms;
} SimaticSend3964rArgs;

/** @brief What rx_byte_3964r takes: ctx, b, now_ms. */
typedef struct
{
    Simatic3964Ctx *ctx;
    uint8_t b;
    uint32_t now_ms;
} SimaticRxByte3964rArgs;

/** @brief What tick_3964r takes: ctx, now_ms. */
typedef struct
{
    Simatic3964Ctx *ctx;
    uint32_t now_ms;
} SimaticTick3964rArgs;

/** @brief What idle_3964r takes: ctx. */
typedef struct
{
    const Simatic3964Ctx *ctx;
} SimaticIdle3964rArgs;

/** @brief What build_send_rk512 takes: buf, cap, area, dbnr, addr, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    Rk512Area area;
    uint8_t dbnr;
    uint16_t addr;
    const uint16_t *words;
    uint16_t wcount;
} SimaticBuildSendRk512Args;

/** @brief What build_fetch_rk512 takes: buf, cap, area, dbnr, addr, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    Rk512Area area;
    uint8_t dbnr;
    uint16_t addr;
    uint16_t wcount;
} SimaticBuildFetchRk512Args;

/** @brief What build_reaction_rk512 takes: buf, cap, status. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint16_t status;
} SimaticBuildReactionRk512Args;

/** @brief What parse_header_rk512 takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    Rk512Header *out;
} SimaticParseHeaderRk512Args;

/** @brief What parse_reaction_rk512 takes: buf, len, status, data, ... */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint16_t *status;
    const uint8_t **data;
    size_t *dlen;
} SimaticParseReactionRk512Args;

/**
 * @brief Siemens SIMATIC serial point-to-point link (PROTOCORE_ENABLE_SIMATIC) - the 3964R link protocol + the RK512
 * computer-link telegrams, zero-heap.
 *
 * A caller sets the members a call takes, invokes it through ::Simatic with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Simatic.bcc_3964r_args.data = ...;
 *   Simatic.bcc_3964r_args.len = ...;
 *   Simatic.bcc_3964r(work);
 *   // Simatic.value is what the call reports
 *
 * @var SimaticNs::bcc_3964r_args  what bcc_3964r takes: data, len
 * @var SimaticNs::build_block_3964r_args  what build_block_3964r takes: buf, cap, data, len, with_bcc
 * @var SimaticNs::parse_block_3964r_args  what parse_block_3964r takes: buf, len, with_bcc, out,
 * @var SimaticNs::init_3964r_args  what init_3964r takes: ctx, high_priority, with_bcc, tx,
 * @var SimaticNs::send_3964r_args  what send_3964r takes: ctx, data, len, now_ms
 * @var SimaticNs::rx_byte_3964r_args  what rx_byte_3964r takes: ctx, b, now_ms
 * @var SimaticNs::tick_3964r_args  what tick_3964r takes: ctx, now_ms
 * @var SimaticNs::idle_3964r_args  what idle_3964r takes: ctx
 * @var SimaticNs::build_send_rk512_args  what build_send_rk512 takes: buf, cap, area, dbnr, addr,
 * @var SimaticNs::build_fetch_rk512_args  what build_fetch_rk512 takes: buf, cap, area, dbnr, addr,
 * @var SimaticNs::build_reaction_rk512_args  what build_reaction_rk512 takes: buf, cap, status
 * @var SimaticNs::parse_header_rk512_args  what parse_header_rk512 takes: buf, len, out
 * @var SimaticNs::parse_reaction_rk512_args  what parse_reaction_rk512 takes: buf, len, status, data,
 * @var SimaticNs::ok  true on a complete, check-valid block; false on bad framing, a lone ...
 * @var SimaticNs::value  the value a call reports
 * @var SimaticNs::n  octets written, or 0 on overflow / bad input
 * @var SimaticNs::bcc_3964r  3964R BCC: the longitudinal XOR (even parity) over len bytes at ...
 * @var SimaticNs::build_block_3964r  build the 3964R block body: DLE-stuffed data, then DLE ETX, then ...
 * @var SimaticNs::parse_block_3964r  parse + validate a 3964R block body (the bytes after STX): un-stuff ...
 * @var SimaticNs::init_3964r  initialize the link. high_priority: one end true, the other false ...
 * @var SimaticNs::send_3964r  start sending data (one job in flight). Emits STX and arms the ...
 * @var SimaticNs::rx_byte_3964r  feed one inbound byte at now_ms; drives the handshake / block ...
 * @var SimaticNs::tick_3964r  drive timeouts (QVZ/ZVZ) + retries; call periodically with the ...
 * @var SimaticNs::idle_3964r  true when no job is in flight and no block is being received
 * @var SimaticNs::build_send_rk512  build a SEND telegram header + the wcount big-endian data words at ...
 * @var SimaticNs::build_fetch_rk512  build a FETCH telegram header (no data words - the partner returns ...
 * @var SimaticNs::build_reaction_rk512  build a reaction (acknowledge) telegram carrying status (0 = ok)
 * @var SimaticNs::parse_header_rk512  parse an RK512 header off a telegram. true on a complete, valid ...
 * @var SimaticNs::parse_reaction_rk512  parse a reaction telegram: the status word, and (for a FETCH ...
 *
 * @c work is PROTOCORE_SIMATIC_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    SimaticBcc3964rArgs bcc_3964r_args;
    SimaticBuildBlock3964rArgs build_block_3964r_args;
    SimaticParseBlock3964rArgs parse_block_3964r_args;
    SimaticInit3964rArgs init_3964r_args;
    SimaticSend3964rArgs send_3964r_args;
    SimaticRxByte3964rArgs rx_byte_3964r_args;
    SimaticTick3964rArgs tick_3964r_args;
    SimaticIdle3964rArgs idle_3964r_args;
    SimaticBuildSendRk512Args build_send_rk512_args;
    SimaticBuildFetchRk512Args build_fetch_rk512_args;
    SimaticBuildReactionRk512Args build_reaction_rk512_args;
    SimaticParseHeaderRk512Args parse_header_rk512_args;
    SimaticParseReactionRk512Args parse_reaction_rk512_args;
    proto_bool ok;
    uint8_t value;
    size_t n;
} SimaticVars;

/** @brief The operands and the outcome. */
extern SimaticVars SimaticV;

/** @brief The entries. */
typedef struct
{
    void (*const bcc_3964r)(uint8_t *restrict work);
    void (*const build_block_3964r)(uint8_t *restrict work);
    void (*const parse_block_3964r)(uint8_t *restrict work);
    void (*const init_3964r)(uint8_t *restrict work);
    void (*const send_3964r)(uint8_t *restrict work);
    void (*const rx_byte_3964r)(uint8_t *restrict work);
    void (*const tick_3964r)(uint8_t *restrict work);
    void (*const idle_3964r)(uint8_t *restrict work);
    void (*const build_send_rk512)(uint8_t *restrict work);
    void (*const build_fetch_rk512)(uint8_t *restrict work);
    void (*const build_reaction_rk512)(uint8_t *restrict work);
    void (*const parse_header_rk512)(uint8_t *restrict work);
    void (*const parse_reaction_rk512)(uint8_t *restrict work);
} SimaticNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SimaticV or a region of the borrow at a fixed offset.
void protocore_simatic_bcc_3964r(uint8_t *restrict work);
void protocore_simatic_build_block_3964r(uint8_t *restrict work);
void protocore_simatic_parse_block_3964r(uint8_t *restrict work);
void protocore_simatic_init_3964r(uint8_t *restrict work);
void protocore_simatic_send_3964r(uint8_t *restrict work);
void protocore_simatic_rx_byte_3964r(uint8_t *restrict work);
void protocore_simatic_tick_3964r(uint8_t *restrict work);
void protocore_simatic_idle_3964r(uint8_t *restrict work);
void protocore_simatic_build_send_rk512(uint8_t *restrict work);
void protocore_simatic_build_fetch_rk512(uint8_t *restrict work);
void protocore_simatic_build_reaction_rk512(uint8_t *restrict work);
void protocore_simatic_parse_header_rk512(uint8_t *restrict work);
void protocore_simatic_parse_reaction_rk512(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Simatic.bcc_3964r(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SimaticNs Simatic __attribute__((unused)) = {
    .bcc_3964r = protocore_simatic_bcc_3964r,
    .build_block_3964r = protocore_simatic_build_block_3964r,
    .parse_block_3964r = protocore_simatic_parse_block_3964r,
    .init_3964r = protocore_simatic_init_3964r,
    .send_3964r = protocore_simatic_send_3964r,
    .rx_byte_3964r = protocore_simatic_rx_byte_3964r,
    .tick_3964r = protocore_simatic_tick_3964r,
    .idle_3964r = protocore_simatic_idle_3964r,
    .build_send_rk512 = protocore_simatic_build_send_rk512,
    .build_fetch_rk512 = protocore_simatic_build_fetch_rk512,
    .build_reaction_rk512 = protocore_simatic_build_reaction_rk512,
    .parse_header_rk512 = protocore_simatic_parse_header_rk512,
    .parse_reaction_rk512 = protocore_simatic_parse_reaction_rk512,
};

/**
 * @brief The PROTOCORE_SIMATIC_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_simatic_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SIMATIC

#endif // PROTOCORE_SIMATIC_H
