// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mime.h
 * @brief Shared HTTP Content-Type ("MIME") string constants (one source of truth).
 *
 * The single home for the media-type vocabulary the core server, the route services and the
 * examples all name: reference the pointer, never re-type the string, so a value cannot silently
 * diverge across call sites.
 *
 * Header-only like hex.h / numparse.h - no .cpp to wire into every test
 * env's src filter. Each is a `const char *const` to a string literal: the literal
 * lives in the linker's mergeable string section, so there is one copy in flash no
 * matter how many translation units reference it, and an unused one costs nothing.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MIME_H
#define PROTOCORE_MIME_H

static const char *const PC_MIME_JSON = "application/json";
static const char *const PC_MIME_TEXT_PLAIN = "text/plain";
static const char *const PC_MIME_TEXT_HTML = "text/html";
static const char *const PC_MIME_OCTET_STREAM = "application/octet-stream";
static const char *const PC_MIME_JAVASCRIPT = "application/javascript";

#endif // PROTOCORE_MIME_H
