// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file web_terminal.h
 * @brief Browser "web serial" terminal over WebSocket (PROTOCORE_ENABLE_WEB_TERMINAL).
 *
 * A zero-heap equivalent of the WebSerial-style remote serial monitor: it serves
 * a self-contained terminal web page and a WebSocket endpoint on the same path.
 * Device output is broadcast to every connected browser; each line a browser
 * sends is delivered to a command callback. Rides the library's existing
 * WebSocket layer (no extra connection state), so it is TLS-agnostic - the page
 * auto-selects ws:// or wss:// from the page's own scheme.
 *
 * A line is built with the frame engine and handed over as text, so the shape is a
 * `static const protocore_field[]` in rodata rather than a format string parsed per call.
 *
 * @code
 *   static const protocore_field SAID[] = {{PROTOCORE_FK_LIT, 0, 10, "you said: "}, PROTOCORE_STR,
 *                                   {PROTOCORE_FK_LIT, 0, 1, "\n"}, PROTOCORE_END};
 *   void on_cmd(const char *line, uint8_t client) {
 *     char out[64];
 *     frame.build(out, sizeof(out), SAID, (const protocore_fval[]){PROTOCORE_VSTR(line)}, 1);
 *     protocore_web_terminal_print(out);
 *   }
 *   void setup() {
 *     // ... wifi + on_http(...) ...
 *     protocore_web_terminal_begin("/terminal");
 *     protocore_web_terminal_on_command(on_cmd);
 *     begin_http(80);
 *   }
 * @endcode
 *
 * No-op stubs when PROTOCORE_ENABLE_WEB_TERMINAL is 0.
 */

#ifndef PROTOCORE_WEB_TERMINAL_H
#define PROTOCORE_WEB_TERMINAL_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_WEB_TERMINAL

PROTOCORE_BEGIN_DECLS

/**
 * @brief Callback for a line typed in a connected browser terminal.
 * @param line       Null-terminated command text (no trailing newline).
 * @param client_id  WebSocket client index that sent it (ws_pool[] slot).
 */
typedef void (*TermCommandCb)(const char *line, uint8_t client_id);

/**
 * @brief Register the terminal page + WebSocket endpoint.
 *
 * Serves the HTML page at @p path (GET) and accepts the terminal WebSocket at
 * `<path>/ws`. Call before begin_http().
 *
 * @param path URL path for the page.
 */
void protocore_web_terminal_begin(const char *path);

/** @brief Install the command callback (browser -> device). Pass NULL to clear. */
void protocore_web_terminal_on_command(TermCommandCb cb);

/** @brief Broadcast text to every connected terminal browser (device -> browsers). */
void protocore_web_terminal_print(const char *s);

/** @brief Like print() but appends a newline. */
void protocore_web_terminal_println(const char *s);

/** @brief Number of browsers currently connected to the terminal. */
uint8_t protocore_web_terminal_client_count(void);

#else // PROTOCORE_ENABLE_WEB_TERMINAL == 0  -> no-op stubs

typedef void (*TermCommandCb)(const char *line, uint8_t client_id);
static inline void protocore_web_terminal_begin(const char *path)
{
    (void)path;
}
static inline void protocore_web_terminal_on_command(TermCommandCb cb)
{
    (void)cb;
}
static inline void protocore_web_terminal_print(const char *s)
{
    (void)s;
}
static inline void protocore_web_terminal_println(const char *s)
{
    (void)s;
}
static inline uint8_t protocore_web_terminal_client_count(void)
{
    return 0;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEB_TERMINAL

#endif // PROTOCORE_WEB_TERMINAL_H
