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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_WEB_TERMINAL

PROTOCORE_BEGIN_DECLS

// PROTOCORE_WEB_TERMINAL_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums it
// into its arena. A caller takes them once and passes the pointer to every call. How they are
// carved is this module's and is never named here.

/**
 * @brief Callback for a line typed in a connected browser terminal.
 * @param line       Null-terminated command text (no trailing newline).
 * @param client_id  WebSocket client index that sent it (ws_pool[] slot).
 */
typedef void (*TermCommandCb)(const char *line, uint8_t client_id);

typedef void (*TermCommandCb)(const char *line, uint8_t client_id);

/** @brief What begin takes. */
typedef struct
{
    const char *path;
} WebTerminalBeginArgs;

/** @brief What on_command takes. */
typedef struct
{
    TermCommandCb cb;
} WebTerminalOnCommandArgs;

/** @brief What print takes. */
typedef struct
{
    const char *s;
} WebTerminalPrintArgs;

/** @brief What println takes. */
typedef struct
{
    const char *s;
} WebTerminalPrintlnArgs;
typedef struct
{
    WebTerminalBeginArgs begin_args;
    WebTerminalOnCommandArgs on_command_args;
    WebTerminalPrintArgs print_args;
    WebTerminalPrintlnArgs println_args;

    proto_bool ok;
    uint16_t value;

    void (*const begin)(uint8_t *restrict work);
    void (*const on_command)(uint8_t *restrict work);
    void (*const print)(uint8_t *restrict work);
    void (*const println)(uint8_t *restrict work);
    void (*const client_count)(uint8_t *restrict work);
} WebTerminalNs;

/** @brief The one symbol this module exports. */
extern WebTerminalNs WebTerminal;

/**
 * @brief The PROTOCORE_WEB_TERMINAL_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_web_terminal_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEB_TERMINAL

#endif // PROTOCORE_WEB_TERMINAL_H
