// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file graphql.cpp
 * @brief GraphQL query subset - parser + executor (implementation).
 *
 * Recursive-descent parse into fixed node/arg pools, then a recursive emit that
 * mirrors the selection set into a JSON `data` object, calling the resolver for
 * leaf fields with the arguments collected along the path. No heap; all state is
 * file-static (single-accessor, like the other services).
 */

#include "services/iot/graphql/graphql.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_GRAPHQL

#include <stdio.h>
#include <string.h>

struct pc_gql_args
{
    const int *idx; // indices into s_gql.args that are in scope
    int count;
};

namespace
{
struct Node
{
    char name[PC_GQL_NAME_MAX];
    int first_arg;
    int n_args;
    int first_child; // -1 if leaf
    int next_sib;    // -1 if last
};
struct Arg
{
    char name[PC_GQL_NAME_MAX];
    pc_gql_value val;
};

// All GraphQL parser + executor state, owned by one instance (internal linkage): the node /
// arg / string pools and their cursors, the parse root + error, and the executor's arg-scope
// stack, resolver, and dotted path. Grouped so it is one named owner, unreachable cross-TU;
// single-accessor (never reentrant). The recursive parser/executor is a single-owner state
// machine, so its helpers reach this owner directly.
struct GqlCtx
{
    Node nodes[PC_GQL_MAX_NODES];
    Arg args[PC_GQL_MAX_ARGS];
    char strbuf[PC_GQL_STRBUF];
    int nnodes;
    int nargs;
    int str_len;
    int root;
    pc_gql_result err;
    // executor: scope stack of in-scope arg indices, resolver, and dotted path
    int scope[PC_GQL_MAX_ARGS];
    int scope_n;
    pc_gql_resolver_fn resolver;
    char path[PC_GQL_PATH_MAX];
};
GqlCtx s_gql;

int new_node()
{
    if (s_gql.nnodes >= PC_GQL_MAX_NODES)
    {
        s_gql.err = pc_gql_result::PC_GQL_ERR_LIMIT;
        return -1;
    }
    Node *n = &s_gql.nodes[s_gql.nnodes];
    n->name[0] = '\0';
    n->first_arg = -1;
    n->n_args = 0;
    n->first_child = -1;
    n->next_sib = -1;
    return s_gql.nnodes++;
}

// ---- lexer helpers --------------------------------------------------------
struct Lex
{
    const char *p;
    const char *e;
};

void skipws(Lex &L)
{
    while (L.p < L.e)
    {
        char c = *L.p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',')
        {
            L.p++;
        }
        else if (c == '#')
        {
            while (L.p < L.e && *L.p != '\n')
            {
                L.p++;
            }
        }
        else
        {
            break;
        }
    }
}

char peek(Lex &L)
{
    skipws(L);
    return L.p < L.e ? *L.p : '\0';
}

// Record a generic parse error, preserving any more specific error already set.
void gql_flag_parse_err()
{
    if (s_gql.err == pc_gql_result::PC_GQL_OK)
    {
        s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE;
    }
}

bool is_name_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool is_name(char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

// @p cap is the size of @p out. It is bounded by the CALLER's buffer, not only by
// PC_GQL_NAME_MAX: parse_value() passes a small scratch array that only ever has to hold
// "true"/"false"/"null", and bounding solely by the global maximum let a longer bareword in an
// argument position - e.g. `{ f(a: LONGENUMVALUE) }`, straight from untrusted query text - write
// past the end of it. Names still cannot exceed PC_GQL_NAME_MAX; whichever limit is tighter wins.
bool parse_name(Lex &L, char *out, size_t cap)
{
    skipws(L);
    if (L.p >= L.e || !is_name_start(*L.p))
    {
        return false;
    }
    const size_t limit = cap < (size_t)PC_GQL_NAME_MAX ? cap : (size_t)PC_GQL_NAME_MAX;
    size_t i = 0;
    while (L.p < L.e && is_name(*L.p))
    {
        // `i + 1 >= limit`, not `i >= limit - 1`: equivalent for every limit >= 1, but it also
        // stays correct if a caller ever passes cap == 0, where `limit - 1` would wrap to SIZE_MAX
        // and defeat the bound entirely. No separate cap==0 guard needed (and none to leave dead).
        if (i + 1 >= limit)
        {
            s_gql.err = pc_gql_result::PC_GQL_ERR_LIMIT;
            return false;
        }
        out[i++] = *L.p++;
    }
    out[i] = '\0';
    return true;
}

// Copy a decoded string into the strbuf pool; returns pointer or nullptr.
const char *intern(const char *s, int len)
{
    if (s_gql.str_len + len + 1 > PC_GQL_STRBUF)
    {
        s_gql.err = pc_gql_result::PC_GQL_ERR_LIMIT;
        return nullptr;
    }
    char *dst = s_gql.strbuf + s_gql.str_len;
    memcpy(dst, s, len);
    dst[len] = '\0';
    s_gql.str_len += len + 1;
    return dst;
}

bool parse_value(Lex &L, pc_gql_value *v)
{
    char c = peek(L);
    if (c == '"')
    {
        L.p++; // opening quote
        char tmp[PC_GQL_STRBUF];
        int n = 0;
        while (L.p < L.e && *L.p != '"')
        {
            char ch = *L.p++;
            if (ch == '\\' && L.p < L.e)
            {
                char esc = *L.p++;
                switch (esc)
                {
                case 'n':
                    ch = '\n';
                    break;
                case 't':
                    ch = '\t';
                    break;
                case 'r':
                    ch = '\r';
                    break;
                case '"':
                    ch = '"';
                    break;
                case '\\':
                    ch = '\\';
                    break;
                case '/':
                    ch = '/';
                    break;
                default:
                    ch = esc;
                    break;
                }
            }
            if (n >= (int)sizeof(tmp) - 1)
            {
                s_gql.err = pc_gql_result::PC_GQL_ERR_LIMIT;
                return false;
            }
            tmp[n++] = ch;
        }
        if (L.p >= L.e)
        {
            s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE;
            return false;
        }
        L.p++; // closing quote
        const char *s = intern(tmp, n);
        if (!s)
        {
            return false;
        }
        v->type = pc_gql_type::PC_GQL_STR;
        v->s = s;
        return true;
    }
    if (c == '-' || (c >= '0' && c <= '9'))
    {
        // Manual number parse (no stdlib): integer, optional fraction, optional
        // exponent. Builds an int64 for plain integers and a double otherwise.
        bool neg = false;
        if (*L.p == '-')
        {
            neg = true;
            L.p++;
        }
        bool any = false;
        bool is_float = false;
        unsigned long long ipart = 0; // accumulate unsigned: signed overflow on a huge literal is UB
        double fval = 0.0;
        while (L.p < L.e && *L.p >= '0' && *L.p <= '9')
        {
            ipart = ipart * 10ULL + (unsigned)(*L.p - '0');
            L.p++;
            any = true;
        }
        fval = (double)ipart;
        if (L.p < L.e && *L.p == '.')
        {
            is_float = true;
            L.p++;
            double scale = 1.0;
            while (L.p < L.e && *L.p >= '0' && *L.p <= '9')
            {
                scale *= 10.0;
                fval += (double)(*L.p - '0') / scale;
                L.p++;
                any = true;
            }
        }
        if (L.p < L.e && (*L.p == 'e' || *L.p == 'E'))
        {
            is_float = true;
            L.p++;
            bool eneg = false;
            if (L.p < L.e && (*L.p == '+' || *L.p == '-'))
            {
                eneg = (*L.p++ == '-');
            }
            int ex = 0;
            while (L.p < L.e && *L.p >= '0' && *L.p <= '9')
            {
                // clamp: 10^400 overflows the double to inf, and bounds the exponent below
                ex = (ex < 400) ? ex * 10 + (*L.p - '0') : ex;
                L.p++;
            }
            double m = 1.0;
            for (int k = 0; k < ex; k++)
            {
                m *= 10.0;
            }
            fval = eneg ? fval / m : fval * m;
        }
        if (!any)
        {
            s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE;
            return false;
        }
        if (is_float)
        {
            v->type = pc_gql_type::PC_GQL_FLOAT;
            v->f = neg ? -fval : fval;
        }
        else
        {
            v->type = pc_gql_type::PC_GQL_INT;
            // Negate in signed space: ipart is unsigned (to dodge signed-overflow UB while
            // accumulating), so -ipart would be a modular unsigned negation, not arithmetic negation.
            v->i = neg ? -(long long)ipart : (long long)ipart;
        }
        return true;
    }
    // keyword: true / false / null
    char kw[8];
    if (parse_name(L, kw, sizeof(kw)))
    {
        if (strcmp(kw, "true") == 0)
        {
            v->type = pc_gql_type::PC_GQL_BOOL;
            v->b = true;
            return true;
        }
        if (strcmp(kw, "false") == 0)
        {
            v->type = pc_gql_type::PC_GQL_BOOL;
            v->b = false;
            return true;
        }
        if (strcmp(kw, "null") == 0)
        {
            v->type = pc_gql_type::PC_GQL_NULL;
            return true;
        }
    }
    s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE;
    return false;
}

int parse_selection(Lex &L, int depth);

int parse_field(Lex &L, int depth)
{
    int idx = new_node();
    if (idx < 0)
    {
        return -1;
    }
    if (!parse_name(L, s_gql.nodes[idx].name, sizeof(s_gql.nodes[idx].name)))
    {
        gql_flag_parse_err();
        return -1;
    }
    // arguments
    if (peek(L) == '(')
    {
        L.p++; // '('
        int first = -1;
        int count = 0;
        while (peek(L) != ')')
        {
            if (s_gql.nargs >= PC_GQL_MAX_ARGS)
            {
                s_gql.err = pc_gql_result::PC_GQL_ERR_LIMIT;
                return -1;
            }
            Arg *a = &s_gql.args[s_gql.nargs];
            if (!parse_name(L, a->name, sizeof(a->name)))
            {
                gql_flag_parse_err();
                return -1;
            }
            if (peek(L) != ':')
            {
                s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE;
                return -1;
            }
            L.p++; // ':'
            if (!parse_value(L, &a->val))
            {
                return -1;
            }
            if (first < 0)
            {
                first = s_gql.nargs;
            }
            count++;
            s_gql.nargs++;
        }
        L.p++; // ')'
        s_gql.nodes[idx].first_arg = first;
        s_gql.nodes[idx].n_args = count;
    }
    // sub-selection
    if (peek(L) == '{')
    {
        s_gql.nodes[idx].first_child = parse_selection(L, depth + 1);
    }
    return s_gql.err != pc_gql_result::PC_GQL_OK ? -1 : idx;
}

int parse_selection(Lex &L, int depth)
{
    if (depth > PC_GQL_MAX_DEPTH)
    {
        s_gql.err = pc_gql_result::PC_GQL_ERR_LIMIT;
        return -1;
    }
    if (peek(L) != '{')
    {
        s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE;
        return -1;
    }
    L.p++; // '{'
    int first = -1;
    int prev = -1;
    while (peek(L) != '}')
    {
        if (L.p >= L.e)
        {
            s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE;
            return -1;
        }
        int f = parse_field(L, depth);
        if (f < 0)
        {
            return -1;
        }
        if (first < 0)
        {
            first = f;
        }
        else
        {
            s_gql.nodes[prev].next_sib = f;
        }
        prev = f;
    }
    L.p++; // '}'
    return first;
}

bool parse_document(Lex &L)
{
    char c = peek(L);
    if (c != '{')
    {
        char kw[PC_GQL_NAME_MAX];
        if (!parse_name(L, kw, sizeof(kw)) || strcmp(kw, "query") != 0)
        {
            s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE; // only anonymous or `query` operations
            return false;
        }
        if (peek(L) != '{') // optional operation name
        {
            char opname[PC_GQL_NAME_MAX];
            if (!parse_name(L, opname, sizeof(opname)))
            {
                gql_flag_parse_err();
                return false;
            }
        }
    }
    s_gql.root = parse_selection(L, 1);
    if (s_gql.err != pc_gql_result::PC_GQL_OK)
    {
        return false;
    }
    if (peek(L) != '\0') // trailing junk after the operation
    {
        s_gql.err = pc_gql_result::PC_GQL_ERR_PARSE;
        return false;
    }
    return true;
}

// ---- writer + executor ----------------------------------------------------
struct Writer
{
    char *o;
    size_t cap;
    size_t n;
    bool ovf;
};
void w_raw(Writer &w, const char *s, size_t len)
{
    if (w.ovf)
    {
        return;
    }
    if (w.n + len > w.cap)
    {
        w.ovf = true;
        return;
    }
    memcpy(w.o + w.n, s, len);
    w.n += len;
}
void w_str(Writer &w, const char *s)
{
    w_raw(w, s, strnlen(s, w.cap + 1));
}
void w_json_str(Writer &w, const char *s)
{
    w_raw(w, "\"", 1);
    for (const char *p = s; *p; p++)
    {
        unsigned char ch = (unsigned char)*p;
        if (ch == '"')
        {
            w_raw(w, "\\\"", 2);
        }
        else if (ch == '\\')
        {
            w_raw(w, "\\\\", 2);
        }
        else if (ch == '\n')
        {
            w_raw(w, "\\n", 2);
        }
        else if (ch == '\r')
        {
            w_raw(w, "\\r", 2);
        }
        else if (ch == '\t')
        {
            w_raw(w, "\\t", 2);
        }
        else if (ch < 0x20)
        {
            char u[7];
            pc_sb sb_u = {u, sizeof(u), 0, true};
            pc_sb_put(&sb_u, "\\u");
            pc_sb_hex(&sb_u, (uint64_t)(ch), 4);
            if (pc_sb_finish(&sb_u) == 0)
            {
                u[0] = '\0';
            }
            w_raw(w, u, 6);
        }
        else
        {
            w_raw(w, (const char *)&ch, 1);
        }
    }
    w_raw(w, "\"", 1);
}
void w_scalar(Writer &w, const pc_gql_value *v)
{
    char b[40];
    switch (v->type)
    {
    case pc_gql_type::PC_GQL_INT: {
        pc_sb sb_b = {b, sizeof(b), 0, true};
        pc_sb_i64(&sb_b, (int64_t)(v->i));
        if (pc_sb_finish(&sb_b) == 0)
        {
            b[0] = '\0';
        }
    }
        w_str(w, b);
        break;
    case pc_gql_type::PC_GQL_FLOAT: {
        pc_sb sb_b2 = {b, sizeof(b), 0, true};
        pc_sb_g(&sb_b2, (double)(v->f), 6);
        if (pc_sb_finish(&sb_b2) == 0)
        {
            b[0] = '\0';
        }
    }
        w_str(w, b);
        break;
    case pc_gql_type::PC_GQL_BOOL:
        w_str(w, v->b ? "true" : "false");
        break;
    case pc_gql_type::PC_GQL_STR:
        w_json_str(w, v->s ? v->s : "");
        break;
    default:
        w_str(w, "null");
        break;
    }
}

void emit_field(Writer &w, int idx, int path_len)
{
    Node *node = &s_gql.nodes[idx];

    // extend the dotted path: [parent].name
    int plen = path_len;
    if (plen > 0)
    {
        if (plen + 1 >= PC_GQL_PATH_MAX)
        {
            w.ovf = true;
            return;
        }
        s_gql.path[plen++] = '.';
    }
    int nl = (int)strnlen(node->name, PC_GQL_PATH_MAX);
    if (plen + nl >= PC_GQL_PATH_MAX)
    {
        w.ovf = true;
        return;
    }
    memcpy(s_gql.path + plen, node->name, nl);
    plen += nl;
    s_gql.path[plen] = '\0';

    // push this field's args into scope
    int pushed = 0;
    for (int a = 0; a < node->n_args; a++)
    {
        // scope_n cannot reach the cap: scope[] and args[] are both PC_GQL_MAX_ARGS long,
        // parse_field refuses to record arg number PC_GQL_MAX_ARGS, and the nodes on one
        // root-to-leaf path own disjoint slices of that pool - so the guard never bites.
        if (s_gql.scope_n < PC_GQL_MAX_ARGS) // GCOVR_EXCL_LINE
        {
            s_gql.scope[s_gql.scope_n++] = node->first_arg + a;
            pushed++;
        }
    }

    w_json_str(w, node->name);
    w_raw(w, ":", 1);

    if (node->first_child >= 0)
    {
        w_raw(w, "{", 1);
        bool first = true;
        for (int c = node->first_child; c >= 0; c = s_gql.nodes[c].next_sib)
        {
            if (!first)
            {
                w_raw(w, ",", 1);
            }
            first = false;
            emit_field(w, c, plen);
        }
        w_raw(w, "}", 1);
    }
    else
    {
        pc_gql_value v;
        v.type = pc_gql_type::PC_GQL_NULL;
        pc_gql_args view = {s_gql.scope, s_gql.scope_n};
        if (s_gql.resolver && s_gql.resolver(s_gql.path, &view, &v))
        {
            w_scalar(w, &v);
        }
        else
        {
            w_str(w, "null");
        }
    }

    s_gql.scope_n -= pushed; // pop
    s_gql.path[path_len] = '\0';
}
} // namespace

bool pc_gql_arg_int(const pc_gql_args *args, const char *name, long long *out)
{
    if (!args)
    {
        return false;
    }
    for (int k = 0; k < args->count; k++)
    {
        Arg *a = &s_gql.args[args->idx[k]];
        if (strcmp(a->name, name) == 0 && a->val.type == pc_gql_type::PC_GQL_INT)
        {
            *out = a->val.i;
            return true;
        }
    }
    return false;
}
bool pc_gql_arg_str(const pc_gql_args *args, const char *name, const char **out)
{
    if (!args)
    {
        return false;
    }
    for (int k = 0; k < args->count; k++)
    {
        Arg *a = &s_gql.args[args->idx[k]];
        if (strcmp(a->name, name) == 0 && a->val.type == pc_gql_type::PC_GQL_STR)
        {
            *out = a->val.s;
            return true;
        }
    }
    return false;
}
bool pc_gql_arg_bool(const pc_gql_args *args, const char *name, bool *out)
{
    if (!args)
    {
        return false;
    }
    for (int k = 0; k < args->count; k++)
    {
        Arg *a = &s_gql.args[args->idx[k]];
        if (strcmp(a->name, name) == 0 && a->val.type == pc_gql_type::PC_GQL_BOOL)
        {
            *out = a->val.b;
            return true;
        }
    }
    return false;
}

pc_gql_result pc_graphql_execute(const char *query, size_t len, pc_gql_resolver_fn resolver, char *out, size_t cap)
{
    s_gql.nnodes = 0;
    s_gql.nargs = 0;
    s_gql.str_len = 0;
    s_gql.scope_n = 0;
    s_gql.root = -1;
    s_gql.err = pc_gql_result::PC_GQL_OK;
    s_gql.resolver = resolver;
    s_gql.path[0] = '\0';

    Lex L = {query, query + (query ? len : 0)};
    if (!query || !out || cap == 0)
    {
        return pc_gql_result::PC_GQL_ERR_PARSE;
    }

    if (!parse_document(L))
    {
        const char *msg =
            (s_gql.err == pc_gql_result::PC_GQL_ERR_LIMIT) ? "query exceeds a configured limit" : "syntax error";
        Writer w = {out, cap, 0, false};
        w_str(w, "{\"errors\":[{\"message\":");
        w_json_str(w, msg);
        w_str(w, "}]}");
        if (!w.ovf && w.n < cap)
        {
            out[w.n] = '\0';
        }
        // every path that makes parse_document() return false has already set s_gql.err, so the
        // PC_GQL_OK side of this test is unreachable
        return s_gql.err != pc_gql_result::PC_GQL_OK ? s_gql.err : pc_gql_result::PC_GQL_ERR_PARSE; // GCOVR_EXCL_LINE
    }

    Writer w = {out, cap, 0, false};
    w_str(w, "{\"data\":{");
    bool first = true;
    for (int c = s_gql.root; c >= 0; c = s_gql.nodes[c].next_sib)
    {
        if (!first)
        {
            w_raw(w, ",", 1);
        }
        first = false;
        emit_field(w, c, 0);
    }
    w_str(w, "}}");
    if (w.ovf || w.n >= cap)
    {
        return pc_gql_result::PC_GQL_ERR_OVERFLOW;
    }
    out[w.n] = '\0';
    return pc_gql_result::PC_GQL_OK;
}

#endif // PC_ENABLE_GRAPHQL
