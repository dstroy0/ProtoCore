// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file web_terminal.h
 * @brief Browser "web serial" terminal over WebSocket (PC_ENABLE_WEB_TERMINAL).
 *
 * A zero-heap equivalent of the WebSerial-style remote serial monitor: it serves
 * a self-contained terminal web page and a WebSocket endpoint on the same path.
 * Device output is broadcast to every connected browser; each line a browser
 * sends is delivered to a command callback. Rides the library's existing
 * WebSocket layer (no extra connection state), so it is TLS-agnostic - the page
 * auto-selects ws:// or wss:// from the page's own scheme.
 *
 * @code
 *   static const pc_field SAID[] = {{PC_FK_LIT, 0, 10, "you said: "}, PC_STR,
 *                                   {PC_FK_LIT, 0, 1, "\n"}, PC_END};
 *   static const pc_field UPTIME[] = {{PC_FK_LIT, 0, 7, "uptime "}, PC_U32,
 *                                     {PC_FK_LIT, 0, 1, "\n"}, PC_END};
 *   void on_cmd(const char *line, uint8_t client) {
 *     pc_web_terminal_frame(SAID, line);
 *   }
 *   void setup() {
 *     // ... wifi + server.on(...) ...
 *     pc_web_terminal_begin(server, "/terminal");
 *     pc_web_terminal_on_command(on_cmd);
 *     server.begin(80);
 *   }
 *   void loop() {
 *     server.handle();
 *     pc_web_terminal_frame(UPTIME, (uint32_t)millis()); // device -> browsers
 *   }
 * @endcode
 *
 * No-op stubs when PC_ENABLE_WEB_TERMINAL is 0.
 */

#ifndef PROTOCORE_WEB_TERMINAL_H
#define PROTOCORE_WEB_TERMINAL_H

#include "protocore.h"
#include "shared_primitives/frame.h"

#if PC_ENABLE_WEB_TERMINAL

/**
 * @brief Callback for a line typed in a connected browser terminal.
 * @param line       Null-terminated command text (no trailing newline).
 * @param client_id  WebSocket client index that sent it (ws_pool[] slot).
 */
typedef void (*TermCommandCb)(const char *line, uint8_t client_id);

/**
 * @brief Register the terminal page + WebSocket endpoint on @p server.
 *
 * Serves the HTML page at @p path (GET) and accepts the terminal WebSocket at
 * `<path>/ws`. Call before server.begin().
 *
 * @param server The web server to attach to (must outlive the terminal).
 * @param path   URL path for the page (default "/terminal").
 */
void pc_web_terminal_begin(PC &server, const char *path = "/terminal");

/** @brief Install the command callback (browser -> device). Pass nullptr to clear. */
void pc_web_terminal_on_command(TermCommandCb cb);

/** @brief Broadcast text to every connected terminal browser (device -> browsers). */
void pc_web_terminal_print(const char *s);

/** @brief Like print() but appends a newline. */
void pc_web_terminal_println(const char *s);

/**
 * @brief Build @p spec and broadcast it to every connected browser (capped at TERM_TX_BUF_SIZE).
 *
 * The message shape is a `static const pc_field[]` the caller declares, so a terminal line costs a
 * table walk rather than a format-string parse.
 */
void pc_web_terminal_frame(const pc_field *spec, ...);

/** @brief Number of browsers currently connected to the terminal. */
uint8_t pc_web_terminal_client_count();

#else // PC_ENABLE_WEB_TERMINAL == 0  -> no-op stubs

typedef void (*TermCommandCb)(const char *line, uint8_t client_id);
static inline void pc_web_terminal_begin(PC &, const char * = "/terminal")
{
}
static inline void pc_web_terminal_on_command(TermCommandCb)
{
}
static inline void pc_web_terminal_print(const char *)
{
}
static inline void pc_web_terminal_println(const char *)
{
}
static inline void pc_web_terminal_frame(const pc_field *, ...)
{
}
static inline uint8_t pc_web_terminal_client_count()
{
    return 0;
}

#endif // PC_ENABLE_WEB_TERMINAL

#endif // PROTOCORE_WEB_TERMINAL_H
