// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file scp.cpp
 * @brief SCP (RCP) protocol wire codec - implementation. See scp.h.
 */

#include "services/file_transfer/scp/scp.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_SSH_SCP

#include <stdio.h>
#include <string.h>

namespace
{
// Apply one scp flag token (e.g. "-t", "-rf"): -t selects the sink role, -f the source; other letters
// (-v/-r/-p/-d and combinations) are accepted and ignored.
void apply_scp_flags(const char *tok, size_t tlen, ScpMode *mode)
{
    for (size_t k = 1; k < tlen; k++)
    {
        if (tok[k] == 't')
        {
            *mode = ScpMode::SINK;
        }
        else if (tok[k] == 'f')
        {
            *mode = ScpMode::SOURCE;
        }
    }
}
} // namespace

ScpMode pc_scp_parse_cmd(const char *cmd, size_t cmd_len, char *path_out, size_t path_cap)
{
    if (!cmd || !path_out || path_cap == 0)
    {
        return ScpMode::INVALID;
    }
    ScpMode mode = ScpMode::INVALID;
    const char *last_tok = nullptr; // the last non-flag token is the target path
    size_t last_len = 0;
    size_t i = 0;
    while (i < cmd_len)
    {
        while (i < cmd_len && cmd[i] == ' ')
        {
            i++;
        }
        if (i >= cmd_len)
        {
            break;
        }
        size_t start = i;
        while (i < cmd_len && cmd[i] != ' ')
        {
            i++;
        }
        size_t tlen = i - start;
        if (tlen >= 2 && cmd[start] == '-')
        {
            apply_scp_flags(cmd + start, tlen, &mode);
        }
        else
        {
            last_tok = cmd + start; // "scp" then the path; the last one wins
            last_len = tlen;
        }
    }
    // The last_len == 0 arm below can never be true, so this line is branch-excluded. The scan above
    // only assigns last_tok/last_len after skipping spaces and confirming i < cmd_len, then advances
    // at least one byte before the token ends - so a recorded token is always >= 1 byte long. The
    // other three conditions here are covered.
    if (mode == ScpMode::INVALID || !last_tok || last_len == 0 || last_len >= path_cap) // GCOVR_EXCL_LINE
    {
        return ScpMode::INVALID;
    }
    memcpy(path_out, last_tok, last_len);
    path_out[last_len] = '\0';
    return mode;
}

bool pc_scp_parse_cline(const char *line, size_t len, uint32_t *mode_out, uint64_t *size_out, char *name_out,
                        size_t name_cap)
{
    if (!line || len < 1 || line[0] != 'C') // only plain file records (not D/E directory records)
    {
        return false;
    }
    size_t i = 1;

    uint32_t mode = 0;
    size_t ms = i;
    while (i < len && line[i] >= '0' && line[i] <= '7')
    {
        mode = mode * 8 + (uint32_t)(line[i] - '0');
        i++;
    }
    if (i == ms || i >= len || line[i] != ' ')
    {
        return false;
    }
    i++;

    uint64_t size = 0;
    size_t ss = i;
    while (i < len && line[i] >= '0' && line[i] <= '9')
    {
        size = size * 10 + (uint64_t)(line[i] - '0');
        i++;
    }
    if (i == ss || i >= len || line[i] != ' ')
    {
        return false;
    }
    i++;

    size_t ns = i;
    while (i < len && line[i] != '\n' && line[i] != '\0')
    {
        i++;
    }
    size_t nlen = i - ns;
    if (nlen == 0 || nlen >= name_cap)
    {
        return false;
    }
    memcpy(name_out, line + ns, nlen);
    name_out[nlen] = '\0';

    if (mode_out)
    {
        *mode_out = mode;
    }
    if (size_out)
    {
        *size_out = size;
    }
    return true;
}

size_t pc_scp_build_cline(uint32_t mode, uint64_t size, const char *name, char *out, size_t cap)
{
    pc_sb sb_out = {out, cap, 0, true};
    pc_sb_put(&sb_out, "C");
    pc_sb_uint(&sb_out, (uint64_t)((unsigned)(mode & 07777)), 8, 4);
    pc_sb_put(&sb_out, " ");
    pc_sb_u64(&sb_out, (uint64_t)((unsigned long long)size));
    pc_sb_put(&sb_out, " ");
    pc_sb_put(&sb_out, name);
    pc_sb_put(&sb_out, "\n");
    int n = (int)pc_sb_finish(&sb_out);
    // The n <= 0 arm can never be true, so this line is branch-excluded: snprintf returns the length
    // it WOULD have written (never negative here - no encoding can fail on this format), and that
    // format always emits at least "C0000 0 \n". Only the truncation arm is reachable, and it is
    // covered. The guard stays as defense against a non-conforming libc.
    if (n <= 0 || (size_t)n >= cap) // GCOVR_EXCL_LINE
    {
        return 0;
    }
    return (size_t)n;
}

#endif // PC_ENABLE_SSH_SCP
