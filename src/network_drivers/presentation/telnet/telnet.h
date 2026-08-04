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

// ---- Application API ------------------------------------------------------

/** @brief Register the per-line command handler. */
void pc_telnet_on_command(TelnetCommandCb cb);

/** @brief Send text to every connected Telnet client (no trailing newline added). */
void pc_telnet_print(const char *s);

/** @brief Send text + CRLF to every connected Telnet client. */
void pc_telnet_println(const char *s);

/**
 * @brief Build @p spec and broadcast it to every connected Telnet client.
 *
 * The message shape is a `static const pc_field[]` the caller declares, so a console line costs a
 * table walk rather than a format-string parse, and a line longer than TELNET_BUF_SIZE is dropped
 * rather than clipped mid-word.
 *
 * @code
 *   static const pc_field HEAP[] = {{PC_FK_LIT, 0, 11, "free heap: "}, PC_U32,
 *                                   {PC_FK_LIT, 0, 8, " bytes\r\n"}, PC_END};
 *   pc_telnet_frame(HEAP, ESP.getFreeHeap());
 * @endcode
 */
void pc_telnet_frame(const pc_field *spec, ...);

/** @brief Number of connected Telnet clients. */
uint8_t pc_telnet_client_count();

// ---- Connection layer (called by the session layer for ConnProto::PROTO_TELNET slots) -

/** @brief A Telnet connection was accepted on TCP slot @p slot. */
void pc_telnet_accept(uint8_t slot);

/** @brief Drain and process received bytes for the Telnet connection on @p slot. */
void pc_telnet_rx(uint8_t slot);

/** @brief The Telnet connection on @p slot closed; release its state. */
void pc_telnet_close(uint8_t slot);

/** @brief The Telnet ProtoHandler (accessor; installed by the builtins list, no session dep). */
struct ProtoHandler;
const struct ProtoHandler *pc_telnet_proto_handler(void);

#endif // PC_ENABLE_TELNET

PROTO_END_DECLS

#endif // PROTOCORE_TELNET_H
