// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dashboard.c
 * @brief Dashboard widget table + JSON serializers (PROTOCORE_ENABLE_DASHBOARD).
 *
 * It owns the widget table and value array and turns them into the layout / values JSON the page
 * consumes, and registers the routes that serve them. The serializers are pure and unit-tested
 * directly; the route callbacks begin() installs are in dashboard_routes.c, which holds no state.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DASHBOARD

#include "server/core/worker/worker.h" // the cellblock this module's state is taken from
#include "server/web/dashboard/dashboard.h"

#include "cellularum_laboro/cellularum_laboro.h"
#include "numeros_scribo/numeros_scribo.h"
#include "protocore.h"                 // on_http / on_sse / on_ws: the tables the begin entry installs on
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

PROTOCORE_BEGIN_DECLS

// A message key as it appears in the JSON: quoted, so it cannot match a widget key containing it.
static const mmgr_field QUOTED_KEY[] = {{MMGR_FK_LIT, 0, 1, "\""}, MMGR_STR, {MMGR_FK_LIT, 0, 1, "\""}, MMGR_END};

// All dashboard state, owned by one instance (internal linkage): the widget table, the
// per-widget value array, and the inbound-control callback, grouped so it is one named owner,
// unreachable from any other translation unit.
typedef struct
{
    const protocore_widget *widgets;
    uint8_t count;
    float values[PROTOCORE_DASHBOARD_MAX_WIDGETS];
    protocore_control_cb control_cb;
    // Whether begin() has run. The route handlers cannot run before it - they exist only because it
    // registered them - but publish() is called by the application on its own schedule, so it can
    // arrive first.
    proto_bool started;
    char stream_path[MAX_PATH_LEN];
#if PROTOCORE_ENABLE_WEBSOCKET
    char ws_path[MAX_PATH_LEN];
#endif
} DashboardCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define DASHBOARD_OFF_CTX 0u
static_assert(DASHBOARD_OFF_CTX + sizeof(DashboardCtx) <= PROTOCORE_DASHBOARD_BORROW,
              "PROTOCORE_DASHBOARD_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: a cellblock hands out cells on
// MMGR_CARCER_ALIGN boundaries, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    DASHBOARD_OFF_CTX % _Alignof(DashboardCtx) == 0,
    "DASHBOARD_OFF_CTX is not a multiple of alignof(DashboardCtx) - DASHBOARD_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define DASHBOARD_CTX(w) ((DashboardCtx *)(void *)((w) + DASHBOARD_OFF_CTX))

static const char *widget_type_name(protocore_widget_type t)
{
    switch (t)
    {
    case PROTOCORE_WIDGET_GAUGE:
        return "gauge";
    case PROTOCORE_WIDGET_BAR:
        return "bar";
    case PROTOCORE_WIDGET_SPARKLINE:
        return "sparkline";
    case PROTOCORE_WIDGET_CHART:
        return "chart";
    case PROTOCORE_WIDGET_BUTTON:
        return "button";
    case PROTOCORE_WIDGET_TOGGLE:
        return "toggle";
    case PROTOCORE_WIDGET_SLIDER:
        return "slider";
    default:
        return "value";
    }
}

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_DASHBOARD_BORROW persistent bytes
} DashboardOwnCtx;
static DashboardOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_dashboard_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = (uint8_t *)protocore_secure_persist(PROTOCORE_DASHBOARD_BORROW);
    }
    return s_own.span;
}

void protocore_dashboard_configure(uint8_t *work);
void protocore_dashboard_parse_control(uint8_t *work);
void protocore_dashboard_values_json(uint8_t *work);

void protocore_dashboard_configure(uint8_t *work)
{
    const protocore_widget *widgets = DashboardV.configure_args.widgets;
    uint8_t count = DashboardV.configure_args.count;

    DASHBOARD_CTX(work)->widgets = widgets;
    DASHBOARD_CTX(work)->count = count > PROTOCORE_DASHBOARD_MAX_WIDGETS ? PROTOCORE_DASHBOARD_MAX_WIDGETS : count;
    for (uint8_t i = 0; i < PROTOCORE_DASHBOARD_MAX_WIDGETS; i++)
    {
        DASHBOARD_CTX(work)->values[i] = 0.0f;
    }
}

void protocore_dashboard_set(uint8_t *work)
{
    const char *key = DashboardV.set_args.key;
    float value = DashboardV.set_args.value;

    if (!key || !DASHBOARD_CTX(work)->widgets)
    {
        DashboardV.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < DASHBOARD_CTX(work)->count; i++)
    {
        if (DASHBOARD_CTX(work)->widgets[i].key &&
            EMBED_CALL(cellul.eq, CatenaFinitaCfg, .src = DASHBOARD_CTX(work)->widgets[i].key, .other = key,
                       .cap = MAX_KEY_LEN, .ci = PROTO_FALSE))
        {
            DASHBOARD_CTX(work)->values[i] = value;
            DashboardV.ok = PROTO_TRUE;
            return;
        }
    }
    DashboardV.ok = PROTO_FALSE;
}

// The layout is an array of widget objects; the values document is one flat object of key/number
// pairs. Both open, repeat one frame per widget, and close, with the separating comma carried as
// the repeated frame's first field.
// The item index selects it; !!i is 0 or 1, so the separator is a load rather than a branch.
static const char *const PROTOCORE_JSON_SEP[2] = {"", ","};

static const mmgr_field DASH_ARRAY_OPEN[] = {{MMGR_FK_LIT, 0, 1, "["}, MMGR_END};
static const mmgr_field DASH_ARRAY_CLOSE[] = {{MMGR_FK_LIT, 0, 1, "]"}, MMGR_END};
static const mmgr_field DASH_WIDGET[] = {
    MMGR_STR,                           // "," from the second widget on
    {MMGR_FK_LIT, 0, 8, "{\"type\":"},  //
    MMGR_JSON,                          // type name
    {MMGR_FK_LIT, 0, 9, ",\"label\":"}, //
    MMGR_JSON,                          //
    {MMGR_FK_LIT, 0, 7, ",\"key\":"},   //
    MMGR_JSON,                          //
    {MMGR_FK_LIT, 0, 7, ",\"min\":"},   //
    {MMGR_FK_G, 0, 0, NULL},            // width 0 == 6 significant digits, the %g default
    {MMGR_FK_LIT, 0, 7, ",\"max\":"},   //
    {MMGR_FK_G, 0, 0, NULL},            //
    {MMGR_FK_LIT, 0, 8, ",\"unit\":"},  //
    MMGR_JSON,                          //
    {MMGR_FK_LIT, 0, 1, "}"},           //
    MMGR_END,
};

static const mmgr_field DASH_OBJECT_OPEN[] = {{MMGR_FK_LIT, 0, 1, "{"}, MMGR_END};
static const mmgr_field DASH_OBJECT_CLOSE[] = {{MMGR_FK_LIT, 0, 1, "}"}, MMGR_END};
static const mmgr_field DASH_VALUE[] = {
    MMGR_STR,                 // "," from the second pair on
    MMGR_JSON,                // key
    {MMGR_FK_LIT, 0, 1, ":"}, //
    {MMGR_FK_G, 0, 0, NULL},  // the reading
    MMGR_END,
};

void protocore_dashboard_layout_json(uint8_t *work)
{
    char *out = DashboardV.layout_json_args.out;
    uint32_t cap = DashboardV.layout_json_args.cap;

    if (!out || cap == 0)
    {
        DashboardV.value = 0;
        return;
    }
    out[0] = '\0';
    if (!DASHBOARD_CTX(work)->widgets)
    {
        DashboardV.value = 0;
        return;
    }
    // Each arm empties the buffer before reporting 0: a frame that did not fit leaves the document
    // open, and a caller measuring the buffer instead of reading the count would ship the fragment.
    if (EMBED_CALL(numer.append, NumerosCfg, .out = out, .cap = cap, .spec = DASH_ARRAY_OPEN, .vals = NULL,
                   .nvals = 0) == 0)
    {
        out[0] = '\0';
        DashboardV.value = 0;
        return;
    }
    for (uint8_t i = 0; i < DASHBOARD_CTX(work)->count; i++)
    {
        const protocore_widget *w = &DASHBOARD_CTX(work)->widgets[i];
        if (EMBED_CALL(numer.append, NumerosCfg, .out = out, .cap = cap, .spec = DASH_WIDGET,
                       .vals = (const mmgr_fval[]){MMGR_VSTR(PROTOCORE_JSON_SEP[!!i]),
                                                   MMGR_VJSON(widget_type_name(w->type)), MMGR_VJSON(w->label),
                                                   MMGR_VJSON(w->key), MMGR_VG((double)w->min), MMGR_VG((double)w->max),
                                                   MMGR_VJSON(w->unit)},
                       .nvals = 7) == 0)
        {
            out[0] = '\0';
            DashboardV.value = 0;
            return;
        }
    }
    size_t n = EMBED_CALL(numer.append, NumerosCfg, .out = out, .cap = cap, .spec = DASH_ARRAY_CLOSE, .vals = NULL,
                          .nvals = 0);
    if (n == 0)
    {
        out[0] = '\0';
    }
    DashboardV.value = (int32_t)n;
}

void protocore_dashboard_values_json(uint8_t *work)
{
    char *out = DashboardV.values_json_args.out;
    uint32_t cap = DashboardV.values_json_args.cap;

    if (!out || cap == 0)
    {
        DashboardV.value = 0;
        return;
    }
    out[0] = '\0';
    if (!DASHBOARD_CTX(work)->widgets)
    {
        DashboardV.value = 0;
        return;
    }
    if (EMBED_CALL(numer.append, NumerosCfg, .out = out, .cap = cap, .spec = DASH_OBJECT_OPEN, .vals = NULL,
                   .nvals = 0) == 0)
    {
        out[0] = '\0';
        DashboardV.value = 0;
        return;
    }
    for (uint8_t i = 0; i < DASHBOARD_CTX(work)->count; i++)
    {
        if (EMBED_CALL(numer.append, NumerosCfg, .out = out, .cap = cap, .spec = DASH_VALUE,
                       .vals = (const mmgr_fval[]){MMGR_VSTR(PROTOCORE_JSON_SEP[!!i]),
                                                   MMGR_VJSON(DASHBOARD_CTX(work)->widgets[i].key),
                                                   MMGR_VG((double)DASHBOARD_CTX(work)->values[i])},
                       .nvals = 3) == 0)
        {
            out[0] = '\0';
            DashboardV.value = 0;
            return;
        }
    }
    size_t n = EMBED_CALL(numer.append, NumerosCfg, .out = out, .cap = cap, .spec = DASH_OBJECT_CLOSE, .vals = NULL,
                          .nvals = 0);
    if (n == 0)
    {
        out[0] = '\0';
    }
    DashboardV.value = (int32_t)n;
}

// ---------------------------------------------------------------------------
// Controls (inbound WebSocket messages)
// ---------------------------------------------------------------------------

void protocore_dashboard_on_control(uint8_t *work)
{
    protocore_control_cb cb = DashboardV.on_control_args.cb;

    DASHBOARD_CTX(work)->control_cb = cb;
}

// Locate the value of "key" in a {"k":...,"v":...} object: a pointer just past
// the ':' (whitespace skipped), or nullptr. The quoted pattern ("k" / "v") only
// matches the message's own keys, not a widget key that happens to contain k/v.
static const char *control_value_ptr(const char *s, const char *key)
{
    char pat[8];
    // A key too long for the buffer leaves pat empty, and strstr then finds nothing - fail closed.
    EMBED_CALL(numer.build, NumerosCfg, .out = pat, .cap = sizeof(pat), .spec = QUOTED_KEY,
               .vals = (const mmgr_fval[]){MMGR_VSTR(key)}, .nvals = 1);
    const size_t s_len = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = s, .cap = 0xFFFF);
    const size_t pat_len = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = pat, .cap = sizeof(pat));
    const char *p = EMBED_CALL(cellul.find, CatenaFinitaCfg, .src = s, .cap = s_len + 1u, .other = pat,
                               .other_cap = pat_len + 1u, .ci = PROTO_FALSE);
    if (!p)
    {
        return NULL;
    }
    p += pat_len;
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    if (*p != ':')
    {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    return p;
}

void protocore_dashboard_parse_control(uint8_t *work)
{
    (void)work;
    const char *msg = DashboardV.parse_control_args.msg;
    char *key_out = DashboardV.parse_control_args.key_out;
    size_t key_cap = DashboardV.parse_control_args.key_cap;
    float *value_out = DashboardV.parse_control_args.value_out;

    if (!msg || !key_out || key_cap == 0 || !value_out)
    {
        DashboardV.ok = PROTO_FALSE;
        return;
    }
    key_out[0] = '\0';
    const char *kp = control_value_ptr(msg, "k");
    const char *vp = control_value_ptr(msg, "v");
    if (!kp || !vp || *kp != '"')
    {
        DashboardV.ok = PROTO_FALSE;
        return;
    }
    kp++;
    size_t i = 0;
    while (*kp && *kp != '"' && i + 1 < key_cap)
    {
        key_out[i++] = *kp++;
    }
    if (*kp != '"')
    {
        key_out[0] = '\0';
        DashboardV.ok = PROTO_FALSE;
        return; // unterminated or key too long
    }
    key_out[i] = '\0';
    const char *end = NULL;
    float v = EMBED_CALL(cellul.to_float, TransfiguroCfg, .src = vp, .end = &end);
    if (end == vp)
    {
        DashboardV.ok = PROTO_FALSE;
        return; // no numeric value
    }
    *value_out = v;
    DashboardV.ok = PROTO_TRUE;
}

void protocore_dashboard_dispatch_control(uint8_t *work)
{
    const char *msg = DashboardV.dispatch_control_args.msg;

    char key[32];
    float value;
    DashboardV.parse_control_args.msg = msg;
    DashboardV.parse_control_args.key_out = key;
    DashboardV.parse_control_args.key_cap = sizeof(key);
    DashboardV.parse_control_args.value_out = &value;
    protocore_dashboard_parse_control(work);
    if (!DashboardV.ok)
    {
        DashboardV.ok = PROTO_FALSE;
        return;
    }
    if (DASHBOARD_CTX(work)->control_cb)
    {
        DASHBOARD_CTX(work)->control_cb(key, value);
    }
    DashboardV.ok = DASHBOARD_CTX(work)->control_cb != NULL;
}

// ---------------------------------------------------------------------------
// Server wiring
// ---------------------------------------------------------------------------
//
// The entries live here with the rest of the namespace, so the whole surface is one initializer.
// dashboard_routes.c holds only the fixed-signature route callbacks they register, declared here
// rather than in the header: their signatures are the dispatcher's, not this module's surface.

void dash_page_handler(uint8_t slot_id, HttpReq *req);
void dash_layout_handler(uint8_t slot_id, HttpReq *req);
void dash_sse_connect(uint8_t protocore_sse_id);
#if PROTOCORE_ENABLE_WEBSOCKET
void dash_ws_connect(uint8_t ws_id);
void dash_ws_message(uint8_t ws_id);
void dash_ws_close(uint8_t ws_id);
#endif

void protocore_dashboard_begin(uint8_t *work)
{
    const char *path = DashboardV.begin_args.path;
    const protocore_widget *widgets = DashboardV.begin_args.widgets;
    uint8_t count = DashboardV.begin_args.count;

    DashboardV.configure_args.widgets = widgets;
    DashboardV.configure_args.count = count;
    protocore_dashboard_configure(work);

    if (!path || !path[0])
    {
        path = "/dashboard";
    }

    char layout_path[MAX_PATH_LEN];
    size_t sb_layout_path = 0;
    sb_layout_path = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = layout_path, .cap = sizeof(layout_path),
                                .at = sb_layout_path, .text = path);
    sb_layout_path = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = layout_path, .cap = sizeof(layout_path),
                                .at = sb_layout_path, .text = "/layout");
    if (EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = layout_path, .cap = sizeof(layout_path),
                   .at = sb_layout_path) == 0)
    {
        layout_path[0] = '\0';
    }
    char *const sb_stream_path_buf = DASHBOARD_CTX(work)->stream_path;
    const size_t sb_stream_path_cap = sizeof(DASHBOARD_CTX(work)->stream_path);
    size_t sb_stream_path = 0;
    sb_stream_path = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = sb_stream_path_buf, .cap = sb_stream_path_cap,
                                .at = sb_stream_path, .text = path);
    sb_stream_path = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = sb_stream_path_buf, .cap = sb_stream_path_cap,
                                .at = sb_stream_path, .text = "/stream");
    if (EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = sb_stream_path_buf, .cap = sb_stream_path_cap,
                   .at = sb_stream_path) == 0)
    {
        DASHBOARD_CTX(work)->stream_path[0] = '\0';
    }

    on_http(path, HTTP_GET, dash_page_handler);
    on_http(layout_path, HTTP_GET, dash_layout_handler);
    on_sse(DASHBOARD_CTX(work)->stream_path, dash_sse_connect);
#if PROTOCORE_ENABLE_WEBSOCKET
    char *const sb_ws_path_buf = DASHBOARD_CTX(work)->ws_path;
    const size_t sb_ws_path_cap = sizeof(DASHBOARD_CTX(work)->ws_path);
    size_t sb_ws_path = 0;
    sb_ws_path = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = sb_ws_path_buf, .cap = sb_ws_path_cap,
                            .at = sb_ws_path, .text = path);
    sb_ws_path = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = sb_ws_path_buf, .cap = sb_ws_path_cap,
                            .at = sb_ws_path, .text = "/ws");
    if (EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = sb_ws_path_buf, .cap = sb_ws_path_cap, .at = sb_ws_path) ==
        0)
    {
        DASHBOARD_CTX(work)->ws_path[0] = '\0';
    }
    on_ws(DASHBOARD_CTX(work)->ws_path, dash_ws_connect, dash_ws_message, dash_ws_close);
#endif
    DASHBOARD_CTX(work)->started = PROTO_TRUE; // last: publish() is only meaningful once the stream route exists
}

void protocore_dashboard_publish(uint8_t *work)
{

    if (!DASHBOARD_CTX(work)->started)
    {
        return; // nothing is subscribed until begin() has registered the stream routes
    }
    char buf[PROTOCORE_DASHBOARD_JSON_BUF];
    DashboardV.values_json_args.out = buf;
    DashboardV.values_json_args.cap = sizeof(buf);
    protocore_dashboard_values_json(work);
    if (DashboardV.value > 0)
    {
        protocore_sse_broadcast(DASHBOARD_CTX(work)->stream_path, buf, NULL, NULL);
    }
}

/** @brief The operands and the outcome. */
DashboardVars DashboardV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DASHBOARD
