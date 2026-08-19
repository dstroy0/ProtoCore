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

#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/web/dashboard/dashboard.h"

#include "mmgr/membuild/membuild.h" // Sb: the route paths begin() composes
#include "mmgr/protoframe/protoframe.h"
#include "mmgr/protostr/protostr.h"
#include "protocore.h" // on_http / on_sse / on_ws: the tables the begin entry installs on

PROTOCORE_BEGIN_DECLS

// A message key as it appears in the JSON: quoted, so it cannot match a widget key containing it.
static const protocore_field QUOTED_KEY[] = {
    {PROTOCORE_FK_LIT, 0, 1, "\""}, PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 1, "\""}, PROTOCORE_END};

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
    uint8_t *span; ///< PROTOCORE_DASHBOARD_BORROW persistent bytes, or null while the pool was short
} DashboardOwnCtx;
static DashboardOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_dashboard_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_DASHBOARD_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void dashboard_configure(uint8_t *restrict work);
static void dashboard_parse_control(uint8_t *restrict work);
static void dashboard_values_json(uint8_t *restrict work);

static void dashboard_configure(uint8_t *restrict work)
{
    const protocore_widget *widgets = Dashboard.configure_args.widgets;
    uint8_t count = Dashboard.configure_args.count;

    DASHBOARD_CTX(work)->widgets = widgets;
    DASHBOARD_CTX(work)->count = count > PROTOCORE_DASHBOARD_MAX_WIDGETS ? PROTOCORE_DASHBOARD_MAX_WIDGETS : count;
    for (uint8_t i = 0; i < PROTOCORE_DASHBOARD_MAX_WIDGETS; i++)
    {
        DASHBOARD_CTX(work)->values[i] = 0.0f;
    }
}

static void dashboard_set(uint8_t *restrict work)
{
    const char *key = Dashboard.set_args.key;
    float value = Dashboard.set_args.value;

    if (!key || !DASHBOARD_CTX(work)->widgets)
    {
        Dashboard.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < DASHBOARD_CTX(work)->count; i++)
    {
        if (DASHBOARD_CTX(work)->widgets[i].key &&
            str.eq(DASHBOARD_CTX(work)->widgets[i].key, key, MAX_KEY_LEN, PROTO_FALSE))
        {
            DASHBOARD_CTX(work)->values[i] = value;
            Dashboard.ok = PROTO_TRUE;
            return;
        }
    }
    Dashboard.ok = PROTO_FALSE;
}

// The layout is an array of widget objects; the values document is one flat object of key/number
// pairs. Both open, repeat one frame per widget, and close, with the separating comma carried as
// the repeated frame's first field.
// The item index selects it; !!i is 0 or 1, so the separator is a load rather than a branch.
static const char *const PROTOCORE_JSON_SEP[2] = {"", ","};

static const protocore_field DASH_ARRAY_OPEN[] = {{PROTOCORE_FK_LIT, 0, 1, "["}, PROTOCORE_END};
static const protocore_field DASH_ARRAY_CLOSE[] = {{PROTOCORE_FK_LIT, 0, 1, "]"}, PROTOCORE_END};
static const protocore_field DASH_WIDGET[] = {
    PROTOCORE_STR,                           // "," from the second widget on
    {PROTOCORE_FK_LIT, 0, 8, "{\"type\":"},  //
    PROTOCORE_JSON,                          // type name
    {PROTOCORE_FK_LIT, 0, 9, ",\"label\":"}, //
    PROTOCORE_JSON,                          //
    {PROTOCORE_FK_LIT, 0, 7, ",\"key\":"},   //
    PROTOCORE_JSON,                          //
    {PROTOCORE_FK_LIT, 0, 7, ",\"min\":"},   //
    {PROTOCORE_FK_G, 0, 0, NULL},            // width 0 == 6 significant digits, the %g default
    {PROTOCORE_FK_LIT, 0, 7, ",\"max\":"},   //
    {PROTOCORE_FK_G, 0, 0, NULL},            //
    {PROTOCORE_FK_LIT, 0, 8, ",\"unit\":"},  //
    PROTOCORE_JSON,                          //
    {PROTOCORE_FK_LIT, 0, 1, "}"},           //
    PROTOCORE_END,
};

static const protocore_field DASH_OBJECT_OPEN[] = {{PROTOCORE_FK_LIT, 0, 1, "{"}, PROTOCORE_END};
static const protocore_field DASH_OBJECT_CLOSE[] = {{PROTOCORE_FK_LIT, 0, 1, "}"}, PROTOCORE_END};
static const protocore_field DASH_VALUE[] = {
    PROTOCORE_STR,                 // "," from the second pair on
    PROTOCORE_JSON,                // key
    {PROTOCORE_FK_LIT, 0, 1, ":"}, //
    {PROTOCORE_FK_G, 0, 0, NULL},  // the reading
    PROTOCORE_END,
};

static void dashboard_layout_json(uint8_t *restrict work)
{
    char *out = Dashboard.layout_json_args.out;
    uint32_t cap = Dashboard.layout_json_args.cap;

    if (!out || cap == 0)
    {
        Dashboard.value = 0;
        return;
    }
    out[0] = '\0';
    if (!DASHBOARD_CTX(work)->widgets)
    {
        Dashboard.value = 0;
        return;
    }
    // Each arm empties the buffer before reporting 0: a frame that did not fit leaves the document
    // open, and a caller measuring the buffer instead of reading the count would ship the fragment.
    if (frame.append(out, cap, DASH_ARRAY_OPEN, NULL, 0) == 0)
    {
        out[0] = '\0';
        Dashboard.value = 0;
        return;
    }
    for (uint8_t i = 0; i < DASHBOARD_CTX(work)->count; i++)
    {
        const protocore_widget *w = &DASHBOARD_CTX(work)->widgets[i];
        if (frame.append(out, cap, DASH_WIDGET,
                         (const protocore_fval[]){PROTOCORE_VSTR(PROTOCORE_JSON_SEP[!!i]),
                                                  PROTOCORE_VJSON(widget_type_name(w->type)), PROTOCORE_VJSON(w->label),
                                                  PROTOCORE_VJSON(w->key), PROTOCORE_VG((double)w->min),
                                                  PROTOCORE_VG((double)w->max), PROTOCORE_VJSON(w->unit)},
                         7) == 0)
        {
            out[0] = '\0';
            Dashboard.value = 0;
            return;
        }
    }
    size_t n = frame.append(out, cap, DASH_ARRAY_CLOSE, NULL, 0);
    if (n == 0)
    {
        out[0] = '\0';
    }
    Dashboard.value = (int32_t)n;
}

static void dashboard_values_json(uint8_t *restrict work)
{
    char *out = Dashboard.values_json_args.out;
    uint32_t cap = Dashboard.values_json_args.cap;

    if (!out || cap == 0)
    {
        Dashboard.value = 0;
        return;
    }
    out[0] = '\0';
    if (!DASHBOARD_CTX(work)->widgets)
    {
        Dashboard.value = 0;
        return;
    }
    if (frame.append(out, cap, DASH_OBJECT_OPEN, NULL, 0) == 0)
    {
        out[0] = '\0';
        Dashboard.value = 0;
        return;
    }
    for (uint8_t i = 0; i < DASHBOARD_CTX(work)->count; i++)
    {
        if (frame.append(out, cap, DASH_VALUE,
                         (const protocore_fval[]){PROTOCORE_VSTR(PROTOCORE_JSON_SEP[!!i]),
                                                  PROTOCORE_VJSON(DASHBOARD_CTX(work)->widgets[i].key),
                                                  PROTOCORE_VG((double)DASHBOARD_CTX(work)->values[i])},
                         3) == 0)
        {
            out[0] = '\0';
            Dashboard.value = 0;
            return;
        }
    }
    size_t n = frame.append(out, cap, DASH_OBJECT_CLOSE, NULL, 0);
    if (n == 0)
    {
        out[0] = '\0';
    }
    Dashboard.value = (int32_t)n;
}

// ---------------------------------------------------------------------------
// Controls (inbound WebSocket messages)
// ---------------------------------------------------------------------------

static void dashboard_on_control(uint8_t *restrict work)
{
    protocore_control_cb cb = Dashboard.on_control_args.cb;

    DASHBOARD_CTX(work)->control_cb = cb;
}

// Locate the value of "key" in a {"k":...,"v":...} object: a pointer just past
// the ':' (whitespace skipped), or nullptr. The quoted pattern ("k" / "v") only
// matches the message's own keys, not a widget key that happens to contain k/v.
static const char *control_value_ptr(const char *s, const char *key)
{
    char pat[8];
    // A key too long for the buffer leaves pat empty, and strstr then finds nothing - fail closed.
    frame.build(pat, sizeof(pat), QUOTED_KEY, (const protocore_fval[]){PROTOCORE_VSTR(key)}, 1);
    const char *p = str.find(s, str.len(s, 0xFFFF) + 1u, pat, str.len(pat, sizeof(pat)) + 1u, PROTO_FALSE);
    if (!p)
    {
        return NULL;
    }
    p += str.len(pat, sizeof(pat));
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

static void dashboard_parse_control(uint8_t *restrict work)
{
    (void)work;
    const char *msg = Dashboard.parse_control_args.msg;
    char *key_out = Dashboard.parse_control_args.key_out;
    size_t key_cap = Dashboard.parse_control_args.key_cap;
    float *value_out = Dashboard.parse_control_args.value_out;

    if (!msg || !key_out || key_cap == 0 || !value_out)
    {
        Dashboard.ok = PROTO_FALSE;
        return;
    }
    key_out[0] = '\0';
    const char *kp = control_value_ptr(msg, "k");
    const char *vp = control_value_ptr(msg, "v");
    if (!kp || !vp || *kp != '"')
    {
        Dashboard.ok = PROTO_FALSE;
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
        Dashboard.ok = PROTO_FALSE;
        return; // unterminated or key too long
    }
    key_out[i] = '\0';
    const char *end = NULL;
    float v = str.to_float(vp, &end);
    if (end == vp)
    {
        Dashboard.ok = PROTO_FALSE;
        return; // no numeric value
    }
    *value_out = v;
    Dashboard.ok = PROTO_TRUE;
}

static void dashboard_dispatch_control(uint8_t *restrict work)
{
    const char *msg = Dashboard.dispatch_control_args.msg;

    char key[32];
    float value;
    Dashboard.parse_control_args.msg = msg;
    Dashboard.parse_control_args.key_out = key;
    Dashboard.parse_control_args.key_cap = sizeof(key);
    Dashboard.parse_control_args.value_out = &value;
    dashboard_parse_control(work);
    if (!Dashboard.ok)
    {
        Dashboard.ok = PROTO_FALSE;
        return;
    }
    if (DASHBOARD_CTX(work)->control_cb)
    {
        DASHBOARD_CTX(work)->control_cb(key, value);
    }
    Dashboard.ok = DASHBOARD_CTX(work)->control_cb != NULL;
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

static void dashboard_begin(uint8_t *restrict work)
{
    const char *path = Dashboard.begin_args.path;
    const protocore_widget *widgets = Dashboard.begin_args.widgets;
    uint8_t count = Dashboard.begin_args.count;

    Dashboard.configure_args.widgets = widgets;
    Dashboard.configure_args.count = count;
    dashboard_configure(work);

    if (!path || !path[0])
    {
        path = "/dashboard";
    }

    char layout_path[MAX_PATH_LEN];
    protocore_sb sb_layout_path = {layout_path, sizeof(layout_path), 0, PROTO_TRUE};
    Sb.put(&sb_layout_path, path);
    Sb.put(&sb_layout_path, "/layout");
    if (Sb.finish(&sb_layout_path) == 0)
    {
        layout_path[0] = '\0';
    }
    protocore_sb sb_stream_path = {DASHBOARD_CTX(work)->stream_path, sizeof(DASHBOARD_CTX(work)->stream_path), 0,
                                   PROTO_TRUE};
    Sb.put(&sb_stream_path, path);
    Sb.put(&sb_stream_path, "/stream");
    if (Sb.finish(&sb_stream_path) == 0)
    {
        DASHBOARD_CTX(work)->stream_path[0] = '\0';
    }

    on_http(path, HTTP_GET, dash_page_handler);
    on_http(layout_path, HTTP_GET, dash_layout_handler);
    on_sse(DASHBOARD_CTX(work)->stream_path, dash_sse_connect);
#if PROTOCORE_ENABLE_WEBSOCKET
    protocore_sb sb_ws_path = {DASHBOARD_CTX(work)->ws_path, sizeof(DASHBOARD_CTX(work)->ws_path), 0, PROTO_TRUE};
    Sb.put(&sb_ws_path, path);
    Sb.put(&sb_ws_path, "/ws");
    if (Sb.finish(&sb_ws_path) == 0)
    {
        DASHBOARD_CTX(work)->ws_path[0] = '\0';
    }
    on_ws(DASHBOARD_CTX(work)->ws_path, dash_ws_connect, dash_ws_message, dash_ws_close);
#endif
    DASHBOARD_CTX(work)->started = PROTO_TRUE; // last: publish() is only meaningful once the stream route exists
}

static void dashboard_publish(uint8_t *restrict work)
{

    if (!DASHBOARD_CTX(work)->started)
    {
        return; // nothing is subscribed until begin() has registered the stream routes
    }
    char buf[PROTOCORE_DASHBOARD_JSON_BUF];
    Dashboard.values_json_args.out = buf;
    Dashboard.values_json_args.cap = sizeof(buf);
    dashboard_values_json(work);
    if (Dashboard.value > 0)
    {
        protocore_sse_broadcast(DASHBOARD_CTX(work)->stream_path, buf, NULL, NULL);
    }
}

DashboardNs Dashboard = {.configure = dashboard_configure,
                         .set = dashboard_set,
                         .layout_json = dashboard_layout_json,
                         .values_json = dashboard_values_json,
                         .on_control = dashboard_on_control,
                         .parse_control = dashboard_parse_control,
                         .dispatch_control = dashboard_dispatch_control,
                         .begin = dashboard_begin,
                         .publish = dashboard_publish};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DASHBOARD
