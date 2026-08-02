// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dnc_stream.c
 * @brief DNC drip-feed engine (see dnc_stream.h). Frames a program with the dnc codec and paces it
 *        against reverse-channel XON/XOFF over a send/recv seam.
 */

#include "dnc_stream.h"

#if PC_ENABLE_DNC

#include <string.h>

// Drain any reverse-channel bytes into the flow state (non-blocking); false on a recv error.
static proto_bool flow_drain(DncFlow *flow, DncRecvFn recv, void *ctx)
{
    uint8_t tmp[16];
    int r = recv(ctx, tmp, sizeof(tmp));
    if (r < 0)
    {
        return PROTO_FALSE;
    }
    for (int i = 0; i < r; i++)
    {
        pc_dnc_flow_feed(flow, tmp[i]);
    }
    return PROTO_TRUE;
}

// Send @p n bytes, first honoring XOFF: update flow, and while paused poll recv for the XON.
static proto_bool emit(DncFlow *flow, DncSendFn send, DncRecvFn recv, void *ctx, const uint8_t *data, size_t n)
{
    if (!flow_drain(flow, recv, ctx))
    {
        return PROTO_FALSE;
    }
    uint32_t polls = 0;
    while (!pc_dnc_flow_can_send(flow))
    {
        if (++polls > PC_DNC_XOFF_MAX_POLLS)
        {
            return PROTO_FALSE; // XOFF never cleared
        }
        if (!flow_drain(flow, recv, ctx))
        {
            return PROTO_FALSE;
        }
    }
    return send(ctx, data, n) == (int)n;
}

// Emit @p count NUL runout (leader/trailer) bytes in chunks, honoring flow control.
static proto_bool emit_runout(DncFlow *flow, DncSendFn send, DncRecvFn recv, void *ctx, uint16_t count)
{
    uint8_t zeros[32];
    memset(zeros, 0, sizeof(zeros));
    while (count)
    {
        uint16_t chunk = count < sizeof(zeros) ? count : (uint16_t)sizeof(zeros);
        if (!emit(flow, send, recv, ctx, zeros, chunk))
        {
            return PROTO_FALSE;
        }
        count -= chunk;
    }
    return PROTO_TRUE;
}

DncStreamResult dnc_stream(const DncCfg *cfg, const char *program, size_t prog_len, DncSendFn send, DncRecvFn recv,
                           void *ctx)
{
    if (!cfg || !send || !recv || (prog_len && !program))
    {
        return DNC_STREAM_ERR_ARG;
    }

    DncFlow flow;
    pc_dnc_flow_init(&flow);
    uint8_t buf[PC_DNC_LINE_MAX + 8];

    // leader runout
    if (cfg->leader_len && !emit_runout(&flow, send, recv, ctx, cfg->leader_len))
    {
        return DNC_STREAM_ERR_IO;
    }

    // program-start marker
    // A marker is at most three bytes ('%' or EOR, plus the CR/LF end-of-block) and buf is
    // PC_DNC_LINE_MAX + 8, so the encode has no failing arm to reach here; the check is what keeps
    // that true if the marker ever grows.
    size_t n = pc_dnc_encode_marker(cfg, buf, sizeof(buf));
    if (n == 0) // GCOVR_EXCL_LINE  a 3-byte marker always fits buf, see above
    {
        return DNC_STREAM_ERR_ENCODE; // GCOVR_EXCL_LINE  unreachable body of the guard above
    }
    if (!emit(&flow, send, recv, ctx, buf, n))
    {
        return DNC_STREAM_ERR_IO;
    }

    // one block per source line
    size_t i = 0;
    while (i < prog_len)
    {
        size_t j = i;
        while (j < prog_len && program[j] != '\n')
        {
            j++;
        }
        size_t line_len = j - i;
        if (line_len && program[i + line_len - 1] == '\r')
        {
            line_len--; // strip a trailing CR (CRLF sources)
        }
        n = pc_dnc_encode_block(cfg, program + i, line_len, buf, sizeof(buf));
        if (n == 0)
        {
            return DNC_STREAM_ERR_ENCODE; // untranslatable char or over-long block - fail closed
        }
        if (!emit(&flow, send, recv, ctx, buf, n))
        {
            return DNC_STREAM_ERR_IO;
        }
        i = j + 1; // skip the LF
    }

    // program-end marker (byte-identical to the start marker)
    n = pc_dnc_encode_marker(cfg, buf, sizeof(buf));
    if (!emit(&flow, send, recv, ctx, buf, n))
    {
        return DNC_STREAM_ERR_IO;
    }

    // trailer runout
    if (cfg->leader_len && !emit_runout(&flow, send, recv, ctx, cfg->leader_len))
    {
        return DNC_STREAM_ERR_IO;
    }

    return DNC_STREAM_OK;
}

#endif // PC_ENABLE_DNC
