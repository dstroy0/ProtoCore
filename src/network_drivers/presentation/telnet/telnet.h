// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telnet.h
 * @brief Layer 6/7 - minimal RFC 854 Telnet server (PROTOCORE_ENABLE_TELNET).
 *
 * A zero-heap line-oriented Telnet console dispatched from the session layer's
 * ProtoConn::PROTO_TELNET arms (the same way SSH is dispatched to ssh_conn). On connect it
 * negotiates server-side echo + suppress-go-ahead (so the client runs in
 * character mode and the server draws the line), accumulates a line, echoes
 * keystrokes (with backspace handling), and hands each completed line to a
 * command callback. Output can be pushed to all connected clients.
 *
 * Telnet is plaintext - no authentication or encryption. Use it only on a
 * trusted network; prefer SSH or the WebSocket terminal otherwise.
 *
 * Usage:
 * @code
 *   server.listen(23, ProtoConn::PROTO_TELNET);     // open the Telnet port
 *   protocore_telnet_on_command(my_cmd_handler);   // void(const char *line, uint8_t id)
 * @endcode
 */

#ifndef PROTOCORE_TELNET_H
#define PROTOCORE_TELNET_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

// Only ever pointed at from here, so the tags are enough and the engine's header stays out of
// every translation unit that includes this one.
struct protocore_field;
struct protocore_fval;

#if PROTOCORE_ENABLE_TELNET

/** @brief Called with each completed input line (NUL-terminated, no CR/LF) and its client id. */
typedef void (*TelnetCommandCb)(const char *line, uint8_t conn_id);

struct ProtoHandler;

/** @brief RFC 854 NVT data: what a write puts on the terminal, as a line or as a field set. */
typedef struct
{
    const char *text;                   ///< a line of NVT ASCII
    const struct protocore_field *spec; ///< the field layout a frame is built from
    const struct protocore_fval *val;   ///< the values that fill it
    size_t nv;                          ///< how many
} TelnetOutArgs;

/**
 * @brief The console an application drives, and the three arms the session layer turns.
 *
 * The first five are the application's; the three after them are called for a
 * ProtoConn::PROTO_TELNET slot and are not an application's business.
 *
 * A caller sets the members a call takes, invokes it through ::Telnet, and reads the outcome off
 * the same handle.
 *
 * @var TelnetNs::slot           the connection a call acts on
 * @var TelnetNs::cb             the per-line command handler an install registers
 * @var TelnetNs::out            what a write puts on the NVT: a line, or a field set
 * @var TelnetNs::u8             a call's 8-bit outcome
 * @var TelnetNs::handler        the ProtoHandler a lookup reports
 * @var TelnetNs::on_command     register the per-line command handler
 * @var TelnetNs::print          text to every connected client, no trailing newline added
 * @var TelnetNs::println        text + CRLF to every connected client
 * @var TelnetNs::frame          build @c out.spec and broadcast it. The shape is a `static const
 *                               protocore_field[]` the caller declares, so a console line costs a table
 *                               walk rather than a format-string parse, and one longer than
 *                               TELNET_BUF_SIZE is dropped rather than clipped mid-word
 * @var TelnetNs::client_count   connected clients
 * @var TelnetNs::accept         a connection was accepted on TCP slot @c slot
 * @var TelnetNs::rx             process the received bytes for the connection on @c slot
 * @var TelnetNs::close          the connection on @c slot closed; release its state
 * @var TelnetNs::proto_handler  the ProtoHandler the builtins list installs, which is what keeps
 *                               this module free of a dependency on the session layer
 *
 * @code
 *   static const protocore_field HEAP[] = {{PROTOCORE_FK_LIT, 0, 11, "free heap: "}, PROTOCORE_U32,
 *                                   {PROTOCORE_FK_LIT, 0, 8, " bytes\r\n"}, PROTOCORE_END};
 *   Telnet.out.spec = HEAP;
 *   Telnet.out.val  = (const protocore_fval[]){PROTOCORE_VU32(free_heap_bytes())};
 *   Telnet.out.nv   = 1;
 *   Telnet.frame(Telnet.internal);
 * @endcode
 */

typedef struct
{
    uint8_t slot;       ///< the NVT every call names
    TelnetCommandCb cb; ///< what a received command line is dispatched to

    TelnetOutArgs out; ///< what a write puts on the NVT

    uint8_t u8;
    const struct ProtoHandler *handler;

    void (*const on_command)(uint8_t *restrict work);
    void (*const print)(uint8_t *restrict work);
    void (*const println)(uint8_t *restrict work);
    void (*const frame)(uint8_t *restrict work);
    void (*const client_count)(uint8_t *restrict work);

    void (*const accept)(uint8_t *restrict work);
    void (*const rx)(uint8_t *restrict work);
    void (*const close)(uint8_t *restrict work);
    void (*const proto_handler)(uint8_t *restrict work);
} TelnetNs;

/** @brief The one symbol this module exports. */
extern TelnetNs Telnet;

/**
 * @brief The PROTOCORE_TELNET_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_telnet_span(void);

#endif // PROTOCORE_ENABLE_TELNET

PROTOCORE_END_DECLS

#endif // PROTOCORE_TELNET_H
