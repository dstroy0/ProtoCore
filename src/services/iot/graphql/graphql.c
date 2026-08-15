// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file graphql.c
 * @brief The GraphQL executor: the sec 2 parse, the sec 6 execution, the sec 7 response.
 *
 * Sections cite the GraphQL specification, October 2021 release, published by the GraphQL
 * Foundation at spec.graphql.org. It is not an IETF standard and has no RFC number.
 *
 * A recursive-descent parse over the sec 2 grammar fills the fixed field and argument pools, then
 * ExecuteSelectionSet (sec 6.3) and ExecuteField (sec 6.4) walk the parsed selection sets and write
 * the sec 7.1 response map in the sec 7.2.1 JSON form, calling the resolver (sec 6.4.2) for each
 * scalar leaf with the argument values collected along the path (sec 6.4.1). No heap; every pool is
 * BSS owned by one storage instance.
 */

#include "services/iot/graphql/graphql.h"

#if PROTOCORE_ENABLE_GRAPHQL

#include "mmgr/membuild.h" // Sb: the Int, Float and \uXXXX renderings
#include "mmgr/protomem.h" // mem.cpy: the spans a name and a decoded String move with
#include "mmgr/protostr.h" // str.eq / str.len: the bounded compares and measures

// The argument values in scope at a field (spec sec 6.4.1): indices into the document's argument
// pool, in the order the path collected them.
typedef struct protocore_gql_args
{
    const int *idx; ///< the argument-pool slots that are in scope
    int count;      ///< how many of them
} protocore_gql_args;

// One Field of a selection set (spec sec 2.5): its Name, the slice of the argument pool its
// Arguments occupy, its sub-SelectionSet, and the next Selection beside it.
typedef struct
{
    char name[PROTOCORE_GQL_NAME_MAX];
    int first_arg;
    int n_args;
    int first_child; // -1 when the field is a scalar leaf
    int next_sib;    // -1 when the field is last in its selection set
} GqlField;

// One Argument (spec sec 2.6): `Name : Value`.
typedef struct
{
    char name[PROTOCORE_GQL_NAME_MAX];
    protocore_gql_value val;
} GqlArgument;

// The cursor over the document source text (spec sec 2.1).
typedef struct
{
    const char *p; // the next octet to read
    const char *e; // one past the last
} GqlLexer;

// The response serializer (spec sec 7.2.1): a bump cursor over the caller's buffer that latches
// @c ovf the first time an append would pass the end, and writes nothing after that.
typedef struct
{
    char *o;
    size_t cap;
    size_t n;
    proto_bool ovf;
} GqlWriter;

// The parsed ExecutableDocument (spec sec 2.2): the field and argument pools its selection sets are
// built from, the decoded String octets those arguments point into, the root selection set, and the
// first request error raised (spec sec 7.1.2).
typedef struct
{
    GqlField fields[PROTOCORE_GQL_MAX_NODES];
    GqlArgument args[PROTOCORE_GQL_MAX_ARGS];
    char strings[PROTOCORE_GQL_STRBUF];
    int n_fields;
    int n_args;
    int str_len;
    int root;
    protocore_gql_result err;
} GqlDocument;

// The executing operation's state (spec sec 6.2.1): the argument-pool slots in scope along the path
// from the root, the resolver a leaf calls, and the dotted path it is called with.
typedef struct
{
    int scope[PROTOCORE_GQL_MAX_ARGS];
    int scope_n;
    protocore_gql_resolver_fn resolver;
    char path[PROTOCORE_GQL_PATH_MAX];
} GqlExecution;

/**
 * @brief The executor's compile-time storage: the parse pools and the executing operation.
 *
 * All of it BSS, so a query costs no heap.
 */
struct GraphQLStorage
{
    GqlDocument doc;   ///< the parsed document (spec sec 2.2)
    GqlExecution exec; ///< the operation being executed (spec sec 6.2.1)
};

/**
 * @brief The executor's state and the calls that reach it - what GraphQLNs points at.
 *
 * @var GraphQLInternal::store  the parse pools and the executing operation's state
 * @var GraphQLInternal::ns     the handle a caller sets a call's members on
 */
struct GraphQLInternal
{
    struct GraphQLStorage *store;
    GraphQLNs *ns;
};

static struct GraphQLStorage s_store;

static struct GraphQLInternal s_graphql = {.store = &s_store, .ns = &GraphQL};

// ---- lexer ----------------------------------------------------------------

// Step over Ignored tokens (spec sec 2.1.7): white space (sec 2.1.2), line terminators (sec 2.1.3),
// commas (sec 2.1.5), and a comment through to its line terminator (sec 2.1.4).
static void skip_ignored(GqlLexer *lx)
{
    while (lx->p < lx->e)
    {
        char c = *lx->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',')
        {
            lx->p++;
        }
        else if (c == '#')
        {
            while (lx->p < lx->e && *lx->p != '\n')
            {
                lx->p++;
            }
        }
        else
        {
            break;
        }
    }
}

// The next lexical token's first octet, or '\0' at the end of the document.
static char peek(GqlLexer *lx)
{
    skip_ignored(lx);
    return lx->p < lx->e ? *lx->p : '\0';
}

// NameStart (spec sec 2.1.9): a Latin ASCII letter or '_'.
static proto_bool is_name_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

// NameContinue (spec sec 2.1.9): a NameStart or an ASCII Digit.
static proto_bool is_name_continue(char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

// Raise a request error (spec sec 7.1.2), keeping any more specific error already raised.
static void flag_request_error(struct GraphQLInternal *restrict ctx)
{
    if (ctx->store->doc.err == PROTOCORE_GQL_OK)
    {
        ctx->store->doc.err = PROTOCORE_GQL_ERR_PARSE;
    }
}

// Claim the next field slot, cleared; -1 when the pool is full.
static int new_field(struct GraphQLInternal *restrict ctx)
{
    GqlDocument *doc = &ctx->store->doc;
    if (doc->n_fields >= PROTOCORE_GQL_MAX_NODES)
    {
        doc->err = PROTOCORE_GQL_ERR_LIMIT;
        return -1;
    }
    GqlField *f = &doc->fields[doc->n_fields];
    f->name[0] = '\0';
    f->first_arg = -1;
    f->n_args = 0;
    f->first_child = -1;
    f->next_sib = -1;
    return doc->n_fields++;
}

// Read one Name (spec sec 2.1.9) into @p out, which holds @p cap octets.
//
// @p cap is the size of @p out. It is bounded by the CALLER's buffer, not only by
// PROTOCORE_GQL_NAME_MAX: parse_value() passes a small scratch array that only ever has to hold
// "true"/"false"/"null", and bounding solely by the global maximum let a longer bareword in an
// argument position - an EnumValue (sec 2.9.6) such as `{ f(a: LONGENUMVALUE) }`, straight from
// untrusted document text - write past the end of it. Names still cannot exceed
// PROTOCORE_GQL_NAME_MAX; whichever limit is tighter wins.
static proto_bool parse_name(struct GraphQLInternal *restrict ctx, GqlLexer *lx, char *out, size_t cap)
{
    skip_ignored(lx);
    if (lx->p >= lx->e || !is_name_start(*lx->p))
    {
        return PROTO_FALSE;
    }
    const size_t limit = cap < (size_t)PROTOCORE_GQL_NAME_MAX ? cap : (size_t)PROTOCORE_GQL_NAME_MAX;
    size_t i = 0;
    while (lx->p < lx->e && is_name_continue(*lx->p))
    {
        // `i + 1 >= limit`, not `i >= limit - 1`: equivalent for every limit >= 1, but it also
        // stays correct if a caller ever passes cap == 0, where `limit - 1` would wrap to SIZE_MAX
        // and defeat the bound entirely. No separate cap==0 guard needed (and none to leave dead).
        if (i + 1 >= limit)
        {
            ctx->store->doc.err = PROTOCORE_GQL_ERR_LIMIT;
            return PROTO_FALSE;
        }
        out[i++] = *lx->p++;
    }
    out[i] = '\0';
    return PROTO_TRUE;
}

// Copy a decoded String (spec sec 2.9.4) into the document's string pool; NULL when it is full.
static const char *intern(struct GraphQLInternal *restrict ctx, const char *s, int len)
{
    GqlDocument *doc = &ctx->store->doc;
    if (doc->str_len + len + 1 > PROTOCORE_GQL_STRBUF)
    {
        doc->err = PROTOCORE_GQL_ERR_LIMIT;
        return NULL;
    }
    char *dst = doc->strings + doc->str_len;
    mem.cpy(dst, s, (size_t)len);
    dst[len] = '\0';
    doc->str_len += len + 1;
    return dst;
}

// Read one Value (spec sec 2.9): StringValue, IntValue, FloatValue, BooleanValue or NullValue.
static proto_bool parse_value(struct GraphQLInternal *restrict ctx, GqlLexer *lx, protocore_gql_value *v)
{
    char c = peek(lx);
    if (c == '"')
    {
        // StringValue (sec 2.9.4): StringCharacter list between quotation marks, with the
        // EscapedCharacter set `" \ / b f n r t`. Block strings and \u EscapedUnicode are out of
        // scope, so `\u` and any other escape decode to the octet after the backslash.
        lx->p++; // opening quote
        char tmp[PROTOCORE_GQL_STRBUF];
        int n = 0;
        while (lx->p < lx->e && *lx->p != '"')
        {
            char ch = *lx->p++;
            if (ch == '\\' && lx->p < lx->e)
            {
                char esc = *lx->p++;
                switch (esc)
                {
                case 'b':
                    ch = '\b';
                    break;
                case 'f':
                    ch = '\f';
                    break;
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
                ctx->store->doc.err = PROTOCORE_GQL_ERR_LIMIT;
                return PROTO_FALSE;
            }
            tmp[n++] = ch;
        }
        if (lx->p >= lx->e)
        {
            ctx->store->doc.err = PROTOCORE_GQL_ERR_PARSE;
            return PROTO_FALSE;
        }
        lx->p++; // closing quote
        const char *s = intern(ctx, tmp, n);
        if (!s)
        {
            return PROTO_FALSE;
        }
        v->type = PROTOCORE_GQL_STR;
        v->s = s;
        return PROTO_TRUE;
    }
    if (c == '-' || (c >= '0' && c <= '9'))
    {
        // IntValue (sec 2.9.1) and FloatValue (sec 2.9.2), parsed by hand: IntegerPart, optional
        // FractionalPart, optional ExponentPart. An IntegerPart alone builds an int64; either of the
        // other two parts makes it a Float.
        proto_bool neg = PROTO_FALSE;
        if (*lx->p == '-')
        {
            neg = PROTO_TRUE;
            lx->p++;
        }
        proto_bool any = PROTO_FALSE;
        proto_bool is_float = PROTO_FALSE;
        unsigned long long ipart = 0; // accumulate unsigned: signed overflow on a huge literal is UB
        double fval = 0.0;
        while (lx->p < lx->e && *lx->p >= '0' && *lx->p <= '9')
        {
            ipart = ipart * 10ULL + (unsigned)(*lx->p - '0');
            lx->p++;
            any = PROTO_TRUE;
        }
        fval = (double)ipart;
        if (lx->p < lx->e && *lx->p == '.')
        {
            is_float = PROTO_TRUE;
            lx->p++;
            double scale = 1.0;
            while (lx->p < lx->e && *lx->p >= '0' && *lx->p <= '9')
            {
                scale *= 10.0;
                fval += (double)(*lx->p - '0') / scale;
                lx->p++;
                any = PROTO_TRUE;
            }
        }
        if (lx->p < lx->e && (*lx->p == 'e' || *lx->p == 'E'))
        {
            is_float = PROTO_TRUE;
            lx->p++;
            proto_bool eneg = PROTO_FALSE;
            if (lx->p < lx->e && (*lx->p == '+' || *lx->p == '-'))
            {
                eneg = (*lx->p++ == '-');
            }
            int ex = 0;
            while (lx->p < lx->e && *lx->p >= '0' && *lx->p <= '9')
            {
                // clamp: 10^400 overflows the double to inf, and bounds the exponent below
                ex = (ex < 400) ? ex * 10 + (*lx->p - '0') : ex;
                lx->p++;
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
            ctx->store->doc.err = PROTOCORE_GQL_ERR_PARSE;
            return PROTO_FALSE;
        }
        if (is_float)
        {
            v->type = PROTOCORE_GQL_FLOAT;
            v->f = neg ? -fval : fval;
        }
        else
        {
            v->type = PROTOCORE_GQL_INT;
            // Negate in signed space: ipart is unsigned (to dodge signed-overflow UB while
            // accumulating), so -ipart would be a modular unsigned negation, not arithmetic negation.
            v->i = neg ? -(long long)ipart : (long long)ipart;
        }
        return PROTO_TRUE;
    }
    // BooleanValue (sec 2.9.3) and NullValue (sec 2.9.5): the keywords true, false and null.
    char kw[8];
    if (parse_name(ctx, lx, kw, sizeof(kw)))
    {
        if (str.eq(kw, "true", sizeof("true"), PROTO_FALSE))
        {
            v->type = PROTOCORE_GQL_BOOL;
            v->b = PROTO_TRUE;
            return PROTO_TRUE;
        }
        if (str.eq(kw, "false", sizeof("false"), PROTO_FALSE))
        {
            v->type = PROTOCORE_GQL_BOOL;
            v->b = PROTO_FALSE;
            return PROTO_TRUE;
        }
        if (str.eq(kw, "null", sizeof("null"), PROTO_FALSE))
        {
            v->type = PROTOCORE_GQL_NULL;
            return PROTO_TRUE;
        }
    }
    ctx->store->doc.err = PROTOCORE_GQL_ERR_PARSE;
    return PROTO_FALSE;
}

static int parse_selection_set(struct GraphQLInternal *ctx, GqlLexer *lx, int depth);

// Read one Field (spec sec 2.5): `Name Arguments? SelectionSet?`.
static int parse_field(struct GraphQLInternal *restrict ctx, GqlLexer *lx, int depth)
{
    GqlDocument *doc = &ctx->store->doc;
    int idx = new_field(ctx);
    if (idx < 0)
    {
        return -1;
    }
    if (!parse_name(ctx, lx, doc->fields[idx].name, sizeof(doc->fields[idx].name)))
    {
        flag_request_error(ctx);
        return -1;
    }
    // Arguments (sec 2.6): `( Name : Value list )`.
    if (peek(lx) == '(')
    {
        lx->p++; // '('
        int first = -1;
        int count = 0;
        while (peek(lx) != ')')
        {
            if (doc->n_args >= PROTOCORE_GQL_MAX_ARGS)
            {
                doc->err = PROTOCORE_GQL_ERR_LIMIT;
                return -1;
            }
            GqlArgument *a = &doc->args[doc->n_args];
            if (!parse_name(ctx, lx, a->name, sizeof(a->name)))
            {
                flag_request_error(ctx);
                return -1;
            }
            if (peek(lx) != ':')
            {
                doc->err = PROTOCORE_GQL_ERR_PARSE;
                return -1;
            }
            lx->p++; // ':'
            if (!parse_value(ctx, lx, &a->val))
            {
                return -1;
            }
            if (first < 0)
            {
                first = doc->n_args;
            }
            count++;
            doc->n_args++;
        }
        lx->p++; // ')'
        doc->fields[idx].first_arg = first;
        doc->fields[idx].n_args = count;
    }
    // A sub-SelectionSet makes the field composite; without one it is a scalar leaf.
    if (peek(lx) == '{')
    {
        doc->fields[idx].first_child = parse_selection_set(ctx, lx, depth + 1);
    }
    return doc->err != PROTOCORE_GQL_OK ? -1 : idx;
}

// Read one SelectionSet (spec sec 2.4): `{ Selection list }`. Returns the first field, the rest
// chained through next_sib in document order.
static int parse_selection_set(struct GraphQLInternal *restrict ctx, GqlLexer *lx, int depth)
{
    GqlDocument *doc = &ctx->store->doc;
    if (depth > PROTOCORE_GQL_MAX_DEPTH)
    {
        doc->err = PROTOCORE_GQL_ERR_LIMIT;
        return -1;
    }
    if (peek(lx) != '{')
    {
        doc->err = PROTOCORE_GQL_ERR_PARSE;
        return -1;
    }
    lx->p++; // '{'
    int first = -1;
    int prev = -1;
    while (peek(lx) != '}')
    {
        if (lx->p >= lx->e)
        {
            doc->err = PROTOCORE_GQL_ERR_PARSE;
            return -1;
        }
        int f = parse_field(ctx, lx, depth);
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
            doc->fields[prev].next_sib = f;
        }
        prev = f;
    }
    lx->p++; // '}'
    return first;
}

// Read the Document (spec sec 2.2): one OperationDefinition, either the sec 2.3 query shorthand
// `{...}` or `query Name? {...}`. A mutation or subscription keyword raises a request error.
static proto_bool parse_document(struct GraphQLInternal *restrict ctx, GqlLexer *lx)
{
    GqlDocument *doc = &ctx->store->doc;
    char c = peek(lx);
    if (c != '{')
    {
        char kw[PROTOCORE_GQL_NAME_MAX];
        if (!parse_name(ctx, lx, kw, sizeof(kw)) || !str.eq(kw, "query", sizeof("query"), PROTO_FALSE))
        {
            doc->err = PROTOCORE_GQL_ERR_PARSE; // only the query OperationType
            return PROTO_FALSE;
        }
        if (peek(lx) != '{') // the operation Name is optional (sec 2.3)
        {
            char opname[PROTOCORE_GQL_NAME_MAX];
            if (!parse_name(ctx, lx, opname, sizeof(opname)))
            {
                flag_request_error(ctx);
                return PROTO_FALSE;
            }
        }
    }
    doc->root = parse_selection_set(ctx, lx, 1);
    if (doc->err != PROTOCORE_GQL_OK)
    {
        return PROTO_FALSE;
    }
    if (peek(lx) != '\0') // a second definition, or trailing junk
    {
        doc->err = PROTOCORE_GQL_ERR_PARSE;
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// ---- response serializer (spec sec 7.2.1) ---------------------------------

static void w_raw(GqlWriter *w, const char *s, size_t len)
{
    if (w->ovf)
    {
        return;
    }
    if (w->n + len > w->cap)
    {
        w->ovf = PROTO_TRUE;
        return;
    }
    mem.cpy(w->o + w->n, s, len);
    w->n += len;
}

static void w_str(GqlWriter *w, const char *s)
{
    w_raw(w, s, str.len(s, w->cap + 1));
}

// Append @p s as a JSON string: quoted, with `"` and `\` escaped, the named escapes for the three
// line and tab controls, and \uXXXX for every other C0 control.
static void w_json_str(GqlWriter *w, const char *s)
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
            protocore_sb sb_u = {u, sizeof(u), 0, PROTO_TRUE};
            Sb.put(&sb_u, "\\u");
            Sb.hex(&sb_u, (uint64_t)(ch), 4);
            if (Sb.finish(&sb_u) == 0)
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

// CoerceResult (spec sec 6.4.3) for the scalars this module carries, in the sec 7.2.1 JSON forms:
// Int and Float as numbers, Boolean as true/false, String quoted, Null as null.
static void w_scalar(GqlWriter *w, const protocore_gql_value *v)
{
    char b[40];
    switch (v->type)
    {
    case PROTOCORE_GQL_INT: {
        protocore_sb sb_b = {b, sizeof(b), 0, PROTO_TRUE};
        Sb.i64(&sb_b, (int64_t)(v->i));
        if (Sb.finish(&sb_b) == 0)
        {
            b[0] = '\0';
        }
    }
        w_str(w, b);
        break;
    case PROTOCORE_GQL_FLOAT: {
        protocore_sb sb_b2 = {b, sizeof(b), 0, PROTO_TRUE};
        Sb.g(&sb_b2, (double)(v->f), 6);
        if (Sb.finish(&sb_b2) == 0)
        {
            b[0] = '\0';
        }
    }
        w_str(w, b);
        break;
    case PROTOCORE_GQL_BOOL:
        w_str(w, v->b ? "true" : "false");
        break;
    case PROTOCORE_GQL_STR:
        w_json_str(w, v->s ? v->s : "");
        break;
    default:
        w_str(w, "null");
        break;
    }
}

// ---- execution (spec sec 6) -----------------------------------------------

static void execute_selection_set(struct GraphQLInternal *ctx, GqlWriter *w, int first, int path_len);

// ExecuteField (spec sec 6.4): write the field's response key, then complete its value (sec 6.4.3)
// by executing its sub-selection set or by resolving the leaf (sec 6.4.2). The dotted path and the
// arguments in scope are extended for the duration of the field and unwound on the way out.
static void execute_field(struct GraphQLInternal *restrict ctx, GqlWriter *w, int idx, int path_len)
{
    GqlExecution *ex = &ctx->store->exec;
    GqlField *field = &ctx->store->doc.fields[idx];

    // extend the dotted path: [parent].name
    int plen = path_len;
    if (plen > 0)
    {
        if (plen + 1 >= PROTOCORE_GQL_PATH_MAX)
        {
            w->ovf = PROTO_TRUE;
            return;
        }
        ex->path[plen++] = '.';
    }
    int nl = (int)str.len(field->name, sizeof(field->name));
    if (plen + nl >= PROTOCORE_GQL_PATH_MAX)
    {
        w->ovf = PROTO_TRUE;
        return;
    }
    mem.cpy(ex->path + plen, field->name, (size_t)nl);
    plen += nl;
    ex->path[plen] = '\0';

    // CoerceArgumentValues (spec sec 6.4.1): this field's arguments join the values in scope.
    int pushed = 0;
    for (int a = 0; a < field->n_args; a++)
    {
        // scope_n cannot reach the cap: scope[] and args[] are both PROTOCORE_GQL_MAX_ARGS long,
        // parse_field refuses to record argument number PROTOCORE_GQL_MAX_ARGS, and the fields on
        // one root-to-leaf path own disjoint slices of that pool - so the guard never bites.
        if (ex->scope_n < PROTOCORE_GQL_MAX_ARGS)
        {
            ex->scope[ex->scope_n++] = field->first_arg + a;
            pushed++;
        }
    }

    // The response key is the field's Name: aliases (sec 2.7) are out of scope.
    w_json_str(w, field->name);
    w_raw(w, ":", 1);

    if (field->first_child >= 0)
    {
        execute_selection_set(ctx, w, field->first_child, plen);
    }
    else
    {
        protocore_gql_value v;
        v.type = PROTOCORE_GQL_NULL;
        protocore_gql_args view = {ex->scope, ex->scope_n};
        if (ex->resolver && ex->resolver(ex->path, &view, &v))
        {
            w_scalar(w, &v);
        }
        else
        {
            w_str(w, "null");
        }
    }

    ex->scope_n -= pushed;
    ex->path[path_len] = '\0';
}

// ExecuteSelectionSet (spec sec 6.3): every field of the set becomes one entry of a response map,
// in the order the document lists them.
static void execute_selection_set(struct GraphQLInternal *restrict ctx, GqlWriter *w, int first, int path_len)
{
    w_raw(w, "{", 1);
    proto_bool leading = PROTO_TRUE;
    for (int c = first; c >= 0; c = ctx->store->doc.fields[c].next_sib)
    {
        if (!leading)
        {
            w_raw(w, ",", 1);
        }
        leading = PROTO_FALSE;
        execute_field(ctx, w, c, path_len);
    }
    w_raw(w, "}", 1);
}

// ---- calls ----------------------------------------------------------------

// The argument named ns->argument.name among the values in scope, or NULL. Names are compared
// case-sensitively (spec sec 2.1.9).
static const GqlArgument *arg_lookup(struct GraphQLInternal *restrict ctx)
{
    const protocore_gql_args *view = ctx->ns->argument.values;
    if (!view || !ctx->ns->argument.name)
    {
        return NULL;
    }
    for (int k = 0; k < view->count; k++)
    {
        const GqlArgument *a = &ctx->store->doc.args[view->idx[k]];
        if (str.eq(a->name, ctx->ns->argument.name, sizeof(a->name), PROTO_FALSE))
        {
            return a;
        }
    }
    return NULL;
}

// Read the named argument as an Int (spec sec 3.5.1).
static void graphql_arg_int(struct GraphQLInternal *restrict ctx)
{
    const GqlArgument *a = arg_lookup(ctx);
    ctx->ns->i64 = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (a && a->val.type == PROTOCORE_GQL_INT)
    {
        ctx->ns->i64 = a->val.i;
        ctx->ns->ok = PROTO_TRUE;
    }
}

// Read the named argument as a String (spec sec 3.5.3).
static void graphql_arg_str(struct GraphQLInternal *restrict ctx)
{
    const GqlArgument *a = arg_lookup(ctx);
    ctx->ns->text = NULL;
    ctx->ns->ok = PROTO_FALSE;
    if (a && a->val.type == PROTOCORE_GQL_STR)
    {
        ctx->ns->text = a->val.s;
        ctx->ns->ok = PROTO_TRUE;
    }
}

// Read the named argument as a Boolean (spec sec 3.5.4).
static void graphql_arg_bool(struct GraphQLInternal *restrict ctx)
{
    const GqlArgument *a = arg_lookup(ctx);
    ctx->ns->b = PROTO_FALSE;
    ctx->ns->ok = PROTO_FALSE;
    if (a && a->val.type == PROTOCORE_GQL_BOOL)
    {
        ctx->ns->b = a->val.b;
        ctx->ns->ok = PROTO_TRUE;
    }
}

// ExecuteRequest (spec sec 6.1): parse the document, execute its query operation (sec 6.2.1), and
// serialize the response map (sec 7.1) into ns->response.
static void graphql_execute(struct GraphQLInternal *restrict ctx)
{
    GqlDocument *doc = &ctx->store->doc;
    GqlExecution *ex = &ctx->store->exec;

    doc->n_fields = 0;
    doc->n_args = 0;
    doc->str_len = 0;
    doc->root = -1;
    doc->err = PROTOCORE_GQL_OK;
    ex->scope_n = 0;
    // Latched here, so a resolver may set the argument members mid-walk without disturbing the walk.
    ex->resolver = ctx->ns->request.resolver;
    ex->path[0] = '\0';

    const char *query = ctx->ns->request.document;
    char *out = ctx->ns->response.out;
    const size_t cap = ctx->ns->response.cap;
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;

    if (!query || !out || cap == 0)
    {
        ctx->ns->result = PROTOCORE_GQL_ERR_PARSE;
        return;
    }

    GqlLexer lx = {query, query + ctx->ns->request.len};

    if (!parse_document(ctx, &lx))
    {
        // A request error is raised before execution begins, so the response map carries a non-empty
        // errors list and no data entry (spec sec 7.1, sec 7.1.2). Every error is a map with a
        // message entry (sec 7.1.2 "Error result format").
        const char *msg = (doc->err == PROTOCORE_GQL_ERR_LIMIT) ? "query exceeds a configured limit" : "syntax error";
        GqlWriter w = {out, cap, 0, PROTO_FALSE};
        w_str(&w, "{\"errors\":[{\"message\":");
        w_json_str(&w, msg);
        w_str(&w, "}]}");
        if (!w.ovf && w.n < cap)
        {
            out[w.n] = '\0';
            ctx->ns->n = w.n;
        }
        // every path that makes parse_document() return false has already set doc->err, so the
        // PROTOCORE_GQL_OK side of this test is unreachable
        ctx->ns->result = doc->err != PROTOCORE_GQL_OK ? doc->err : PROTOCORE_GQL_ERR_PARSE;
        return;
    }

    GqlWriter w = {out, cap, 0, PROTO_FALSE};
    w_str(&w, "{\"data\":");
    execute_selection_set(ctx, &w, doc->root, 0);
    w_str(&w, "}");
    if (w.ovf || w.n >= cap)
    {
        // A resolver reads its arguments through this same handle, so ok carries the last accessor's
        // verdict by the time the walk ends. The execute states its own here.
        ctx->ns->ok = PROTO_FALSE;
        ctx->ns->result = PROTOCORE_GQL_ERR_OVERFLOW;
        return;
    }
    out[w.n] = '\0';
    ctx->ns->n = w.n;
    ctx->ns->result = PROTOCORE_GQL_OK;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
GraphQLNs GraphQL = {.execute = graphql_execute,
                     .arg_int = graphql_arg_int,
                     .arg_str = graphql_arg_str,
                     .arg_bool = graphql_arg_bool,
                     .internal = &s_graphql};

#endif // PROTOCORE_ENABLE_GRAPHQL
