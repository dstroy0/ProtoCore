// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file scp.c
 * @brief SCP (RCP) protocol wire codec - implementation. See scp.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSH_SCP

#include "mmgr/membuild.h" // protocore_sb frame builder
#include "mmgr/protomem.h"
#include "network_drivers/session/scp/scp.h"

PROTOCORE_BEGIN_DECLS

// Apply one scp flag token (e.g. "-t", "-rf"): -t selects the sink role, -f the source; other letters
// (-v/-r/-p/-d and combinations) are accepted and ignored.
static void apply_scp_flags(const char *tok, size_t tlen, ScpMode *mode)
{
    for (size_t k = 1; k < tlen; k++)
    {
        if (tok[k] == 't')
        {
            *mode = SCP_MODE_SINK;
        }
        else if (tok[k] == 'f')
        {
            *mode = SCP_MODE_SOURCE;
        }
    }
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void scp_parse_cmd(uint8_t *restrict work)
{
    (void)work;
    const char *cmd = Scp.parse_cmd_args.cmd;
    size_t cmd_len = Scp.parse_cmd_args.cmd_len;
    char *path_out = Scp.parse_cmd_args.path_out;
    size_t path_cap = Scp.parse_cmd_args.path_cap;

    if (!cmd || !path_out || path_cap == 0)
    {
        Scp.value = SCP_MODE_INVALID;
        return;
    }
    ScpMode mode = SCP_MODE_INVALID;
    const char *last_tok = NULL; // the last non-flag token is the target path
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
    if (mode == SCP_MODE_INVALID || !last_tok || last_len == 0 || last_len >= path_cap)
    {
        Scp.value = SCP_MODE_INVALID;
        return;
    }
    mem.cpy(path_out, last_tok, last_len);
    path_out[last_len] = '\0';
    Scp.value = mode;
}

static void scp_parse_cline(uint8_t *restrict work)
{
    (void)work;
    const char *line = Scp.parse_cline_args.line;
    size_t len = Scp.parse_cline_args.len;
    uint32_t *mode_out = Scp.parse_cline_args.mode_out;
    uint64_t *size_out = Scp.parse_cline_args.size_out;
    char *name_out = Scp.parse_cline_args.name_out;
    size_t name_cap = Scp.parse_cline_args.name_cap;

    if (!line || len < 1 || line[0] != 'C') // only plain file records (not D/E directory records)
    {
        Scp.ok = PROTO_FALSE;
        return;
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
        Scp.ok = PROTO_FALSE;
        return;
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
        Scp.ok = PROTO_FALSE;
        return;
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
        Scp.ok = PROTO_FALSE;
        return;
    }
    mem.cpy(name_out, line + ns, nlen);
    name_out[nlen] = '\0';

    if (mode_out)
    {
        *mode_out = mode;
    }
    if (size_out)
    {
        *size_out = size;
    }
    Scp.ok = PROTO_TRUE;
}

static void scp_build_cline(uint8_t *restrict work)
{
    (void)work;
    uint32_t mode = Scp.build_cline_args.mode;
    uint64_t size = Scp.build_cline_args.size;
    const char *name = Scp.build_cline_args.name;
    char *out = Scp.build_cline_args.out;
    size_t cap = Scp.build_cline_args.cap;

    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, "C");
    Sb.uint(&sb_out, (uint64_t)((unsigned)(mode & 07777)), 8, 4);
    Sb.put(&sb_out, " ");
    Sb.u64(&sb_out, (uint64_t)((unsigned long long)size));
    Sb.put(&sb_out, " ");
    Sb.put(&sb_out, name);
    Sb.put(&sb_out, "\n");
    int n = (int)Sb.finish(&sb_out);
    // The n <= 0 arm can never be true, so this line is branch-excluded: snprintf returns the length
    // it WOULD have written (never negative here - no encoding can fail on this format), and that
    // format always emits at least "C0000 0 \n". Only the truncation arm is reachable, and it is
    // covered. The guard stays as defense against a non-conforming libc.
    if (n <= 0 || (size_t)n >= cap)
    {
        Scp.n = 0;
        return;
    }
    Scp.n = (size_t)n;
}

ScpNs Scp = {
    .parse_cmd = scp_parse_cmd,
    .parse_cline = scp_parse_cline,
    .build_cline = scp_build_cline,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_SCP
