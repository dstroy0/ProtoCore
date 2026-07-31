// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// FreeRTOS queue mock for native host builds

#pragma once
#include "FreeRTOS.h"
#include <stdlib.h>
#include <string.h>

inline QueueHandle_t xQueueCreate(uint32_t, size_t)
{
    return (void *)1;
}

// Test hook: force the next xQueueCreateStatic() call to report failure. Models the real
// FreeRTOS case a caller (listener_add/listener_add_dynamic) guards against, even though a
// statically-allocated queue cannot actually fail to construct. Auto-clears after one use.
inline bool &mock_queue_create_fail_once()
{
    static bool v = false;
    return v;
}
inline QueueHandle_t xQueueCreateStatic(uint32_t, size_t, uint8_t *, StaticQueue_t *)
{
    if (mock_queue_create_fail_once())
    {
        mock_queue_create_fail_once() = false;
        return nullptr;
    }
    return (void *)1;
}
// Test hook: force the next xQueueSend() call to report a full queue. Models the real
// FreeRTOS "queue full" case (the application isn't draining server_tick() fast enough)
// so a caller's fallback path (defer-drop, etc.) is host-testable. Auto-clears after one use.
inline bool &mock_queue_send_fail_once()
{
    static bool v = false;
    return v;
}
inline int xQueueSend(QueueHandle_t, const void *, uint32_t)
{
    if (mock_queue_send_fail_once())
    {
        mock_queue_send_fail_once() = false;
        return pdFALSE;
    }
    return pdTRUE;
}

// ---------------------------------------------------------------------------
// Programmable staged-event buffer - for testing the event-dispatch path.
//
// Call queue_stage_raw(item, size) before server_tick().  xQueueReceive
// drains staged items FIFO, then returns pdFALSE when empty, matching
// real FreeRTOS behavior.
//
// Uses the inline function-local-static trick (C++11 §3.2) so all
// translation units in one test binary share a single buffer instance.
// Call queue_stage_reset() in setUp() to guarantee a clean state.
// ---------------------------------------------------------------------------

struct _QueueStageBuf
{
    uint8_t items[16][32]; // up to 16 events, each ≤ 32 bytes
    int item_sz[16];
    int count;
    int idx;
};

inline _QueueStageBuf &_queue_stage_buf()
{
    static _QueueStageBuf b; // zero-initialized (static storage duration)
    return b;
}

inline void queue_stage_raw(const void *item, int sz)
{
    _QueueStageBuf &b = _queue_stage_buf();
    if (sz > 0 && sz <= 32 && b.count < 16)
    {
        memcpy(b.items[b.count], item, (size_t)sz);
        b.item_sz[b.count] = sz;
        b.count++;
    }
}

inline void queue_stage_reset()
{
    _QueueStageBuf &b = _queue_stage_buf();
    b.count = 0;
    b.idx = 0;
}

inline int xQueueReceive(QueueHandle_t, void *buf, uint32_t)
{
    _QueueStageBuf &b = _queue_stage_buf();
    if (b.idx < b.count)
    {
        memcpy(buf, b.items[b.idx], (size_t)b.item_sz[b.idx]);
        b.idx++;
        return pdTRUE;
    }
    return pdFALSE;
}

inline void vQueueDelete(QueueHandle_t)
{
}
