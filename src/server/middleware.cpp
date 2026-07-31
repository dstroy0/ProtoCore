// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file middleware.cpp
 * @brief Middleware chain + built-in fixed-window rate limiter for PC.
 *
 * Split out of protocore.cpp (single-purpose server files). use() registers a middleware;
 * run_middleware() runs the chain in order (first MW_HALT stops dispatch); enable_rate_limit()
 * / rate_limit_check() implement a rollover-safe fixed-window counter that answers 429 +
 * Retry-After past the budget. All four are PC methods over member state - a pure move.
 */

#include "protocore.h"
#include "shared_primitives/frame.h" // the one frame engine
#include "shared_primitives/mime.h"  // PC_MIME_TEXT_PLAIN
#include <stdio.h>

// Retry-After carries one number and nothing else.
static const pc_field RETRY_AFTER[] = {PC_U32, PC_END};

// ---------------------------------------------------------------------------
// Middleware chain + built-in rate limiter
// ---------------------------------------------------------------------------

void PC::use(Middleware mw)
{
    if (mw == nullptr || _middleware_count >= MAX_MIDDLEWARE)
    {
        return;
    }
    _middleware[_middleware_count++] = mw;
}

// Run the chain in registration order. The first middleware to return MwResult::MW_HALT
// stops dispatch; it is responsible for having sent a response.
bool PC::run_middleware(uint8_t slot_id, HttpReq *req)
{
    for (uint8_t i = 0; i < _middleware_count; i++)
    {
        // _middleware[i] is never null here: use() is the only writer, and it always stores a
        // non-null entry together with the count increment that admits index i - a slot below
        // _middleware_count can never regress to null.
        if (_middleware[i] && _middleware[i](slot_id, req) == MwResult::MW_HALT) // GCOVR_EXCL_BR_LINE
        {
            return true;
        }
    }
    return false;
}

void PC::enable_rate_limit(uint16_t max_requests, uint32_t window_ms)
{
    _rl_max = max_requests;
    _rl_window_ms = window_ms;
    _rl_window_start = millis();
    _rl_count = 0;
}

// Fixed-window counter. Unsigned subtraction is rollover-safe across the millis()
// wrap. On the request that tips past _rl_max, reply 429 + Retry-After and stop.
bool PC::rate_limit_check(uint8_t slot_id)
{
    if (_rl_max == 0 || _rl_window_ms == 0)
    {
        return false; // disabled
    }

    uint32_t now = millis();
    if ((uint32_t)(now - _rl_window_start) >= _rl_window_ms)
    {
        _rl_window_start = now; // new window
        _rl_count = 0;
    }

    _rl_count++;
    if (_rl_count <= _rl_max)
    {
        return false; // within budget
    }

    // Over budget: advertise how long until the window resets, then 429.
    uint32_t elapsed = (uint32_t)(now - _rl_window_start);
    // The ":0" arm is unreachable: the check above either just reset _rl_window_start to `now`
    // (elapsed == 0) or left it in place because elapsed was already < _rl_window_ms - either way
    // elapsed < _rl_window_ms always holds here, so the ">" arm always taken.
    uint32_t remain_ms = (_rl_window_ms > elapsed) ? (_rl_window_ms - elapsed) : 0; // GCOVR_EXCL_BR_LINE
    char secs[12];
    // Fails closed to an empty string on its own, so there is no failure arm to write here.
    pc_frame_build(secs, sizeof(secs), RETRY_AFTER, (uint32_t)((remain_ms + 999) / 1000));
    add_response_header(slot_id, "Retry-After", secs);
    send(slot_id, 429, PC_MIME_TEXT_PLAIN, "Too Many Requests");
    return true;
}
