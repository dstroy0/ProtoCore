// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telnet.h
 * @brief Layer 6/7 - minimal RFC 854 Telnet server (PC_ENABLE_TELNET).
 *
 * A zero-heap line-oriented Telnet console dispatched from the session layer's
 * ConnProto::PROTO_TELNET arms (the same way SSH is dispatched to ssh_conn). On connect it
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
 *   server.listen(23, ConnProto::PROTO_TELNET);     // open the Telnet port
 *   pc_telnet_on_command(my_cmd_handler);   // void(const char *line, uint8_t id)
 * @endcode
 */

#ifndef PROTOCORE_TELNET_H
#define PROTOCORE_TELNET_H

#include "mmgr/frame.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_TELNET

/** @brief Called with each completed input line (NUL-terminated, no CR/LF) and its client id. */
typedef void (*TelnetCommandCb)(const char *line, uint8_t conn_id);

struct ProtoHandler;

/**
 * @brief The console an application drives, and the three arms the session layer turns.
 *
 * The first five are the application's; the three after them are called for a
 * ConnProto::PROTO_TELNET slot and are not an application's business.
 *
 * @var TelnetNs::on_command     register the per-line command handler
 * @var TelnetNs::print          text to every connected client, no trailing newline added
 * @var TelnetNs::println        text + CRLF to every connected client
 * @var TelnetNs::frame          build @p spec and broadcast it. The shape is a `static const
 *                               pc_field[]` the caller declares, so a console line costs a table
 *                               walk rather than a format-string parse, and one longer than
 *                               TELNET_BUF_SIZE is dropped rather than clipped mid-word
 * @var TelnetNs::client_count   connected clients
 * @var TelnetNs::accept         a connection was accepted on TCP slot @p slot
 * @var TelnetNs::rx             drain and process received bytes for the connection on @p slot
 * @var TelnetNs::close          the connection on @p slot closed; release its state
 * @var TelnetNs::proto_handler  the ProtoHandler the builtins list installs, which is what keeps
 *                               this module free of a dependency on the session layer
 *
 * @code
 *   static const pc_field HEAP[] = {{PC_FK_LIT, 0, 11, "free heap: "}, PC_U32,
 *                                   {PC_FK_LIT, 0, 8, " bytes\r\n"}, PC_END};
 *   Telnet.frame(HEAP, ESP.getFreeHeap());
 * @endcode
 */
typedef struct
{
    void (*on_command)(TelnetCommandCb cb);
    void (*print)(const char *s);
    void (*println)(const char *s);
    void (*frame)(const pc_field *spec, ...);
    uint8_t (*client_count)(void);

    void (*accept)(uint8_t slot);
    void (*rx)(uint8_t slot);
    void (*close)(uint8_t slot);
    const struct ProtoHandler *(*proto_handler)(void);
} TelnetNs;

/** @brief The one symbol this module exports. */
extern const TelnetNs Telnet;

#endif // PC_ENABLE_TELNET

PROTO_END_DECLS

#endif // PROTOCORE_TELNET_H
