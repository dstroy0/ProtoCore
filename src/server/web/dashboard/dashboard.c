// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dashboard.c
 * @brief Dashboard widget table + JSON serializers (PROTOCORE_ENABLE_DASHBOARD).
 *
 * The host-testable core: it owns the widget table and value array and turns them
 * into the layout / values JSON the page consumes. No server or web
 * dependency lives here, so it compiles and unit-tests standalone; the route /
 * SSE wiring is in dashboard_routes.cpp.
 */

#include "server/web/dashboard/dashboard.h"

#if PROTOCORE_ENABLE_DASHBOARD

#include "mmgr/protoframe.h"
#include "mmgr/protostr.h"

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
} DashboardCtx;
static DashboardCtx s_dash;

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

void protocore_dashboard_configure(const protocore_widget *widgets, uint8_t count)
{
    s_dash.widgets = widgets;
    s_dash.count = count > PROTOCORE_DASHBOARD_MAX_WIDGETS ? PROTOCORE_DASHBOARD_MAX_WIDGETS : count;
    for (uint8_t i = 0; i < PROTOCORE_DASHBOARD_MAX_WIDGETS; i++)
    {
        s_dash.values[i] = 0.0f;
    }
}

proto_bool protocore_dashboard_set(const char *key, float value)
{
    if (!key || !s_dash.widgets)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < s_dash.count; i++)
    {
        if (s_dash.widgets[i].key && strcmp(s_dash.widgets[i].key, key) == 0)
        {
            s_dash.values[i] = value;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
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

int32_t protocore_dashboard_layout_json(char *out, uint32_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    if (!s_dash.widgets)
    {
        return 0;
    }
    // Each arm empties the buffer before reporting 0: a frame that did not fit leaves the document
    // open, and a caller measuring the buffer instead of reading the count would ship the fragment.
    if (frame.append(out, cap, DASH_ARRAY_OPEN, NULL, 0) == 0)
    {
        out[0] = '\0';
        return 0;
    }
    for (uint8_t i = 0; i < s_dash.count; i++)
    {
        const protocore_widget *w = &s_dash.widgets[i];
        if (frame.append(out, cap, DASH_WIDGET,
                         (const protocore_fval[]){PROTOCORE_VSTR(PROTOCORE_JSON_SEP[!!i]),
                                                  PROTOCORE_VJSON(widget_type_name(w->type)), PROTOCORE_VJSON(w->label),
                                                  PROTOCORE_VJSON(w->key), PROTOCORE_VG((double)w->min),
                                                  PROTOCORE_VG((double)w->max), PROTOCORE_VJSON(w->unit)},
                         7) == 0)
        {
            out[0] = '\0';
            return 0;
        }
    }
    size_t n = frame.append(out, cap, DASH_ARRAY_CLOSE, NULL, 0);
    if (n == 0)
    {
        out[0] = '\0';
    }
    return (int32_t)n;
}

int32_t protocore_dashboard_values_json(char *out, uint32_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    if (!s_dash.widgets)
    {
        return 0;
    }
    if (frame.append(out, cap, DASH_OBJECT_OPEN, NULL, 0) == 0)
    {
        out[0] = '\0';
        return 0;
    }
    for (uint8_t i = 0; i < s_dash.count; i++)
    {
        if (frame.append(out, cap, DASH_VALUE,
                         (const protocore_fval[]){PROTOCORE_VSTR(PROTOCORE_JSON_SEP[!!i]),
                                                  PROTOCORE_VJSON(s_dash.widgets[i].key),
                                                  PROTOCORE_VG((double)s_dash.values[i])},
                         3) == 0)
        {
            out[0] = '\0';
            return 0;
        }
    }
    size_t n = frame.append(out, cap, DASH_OBJECT_CLOSE, NULL, 0);
    if (n == 0)
    {
        out[0] = '\0';
    }
    return (int32_t)n;
}

// ---------------------------------------------------------------------------
// Controls (inbound WebSocket messages)
// ---------------------------------------------------------------------------

void protocore_dashboard_on_control(protocore_control_cb cb)
{
    s_dash.control_cb = cb;
}

// Locate the value of "key" in a {"k":...,"v":...} object: a pointer just past
// the ':' (whitespace skipped), or nullptr. The quoted pattern ("k" / "v") only
// matches the message's own keys, not a widget key that happens to contain k/v.
static const char *control_value_ptr(const char *s, const char *key)
{
    char pat[8];
    // A key too long for the buffer leaves pat empty, and strstr then finds nothing - fail closed.
    frame.build(pat, sizeof(pat), QUOTED_KEY, (const protocore_fval[]){PROTOCORE_VSTR(key)}, 1);
    const char *p = strstr(s, pat);
    if (!p)
    {
        return NULL;
    }
    p += strnlen(pat, sizeof(pat));
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

proto_bool protocore_dashboard_parse_control(const char *msg, char *key_out, size_t key_cap, float *value_out)
{
    if (!msg || !key_out || key_cap == 0 || !value_out)
    {
        return PROTO_FALSE;
    }
    key_out[0] = '\0';
    const char *kp = control_value_ptr(msg, "k");
    const char *vp = control_value_ptr(msg, "v");
    if (!kp || !vp || *kp != '"')
    {
        return PROTO_FALSE;
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
        return PROTO_FALSE; // unterminated or key too long
    }
    key_out[i] = '\0';
    const char *end = NULL;
    float v = str.to_float(vp, &end);
    if (end == vp)
    {
        return PROTO_FALSE; // no numeric value
    }
    *value_out = v;
    return PROTO_TRUE;
}

proto_bool protocore_dashboard_dispatch_control(const char *msg)
{
    char key[32];
    float value;
    if (!protocore_dashboard_parse_control(msg, key, sizeof(key), &value))
    {
        return PROTO_FALSE;
    }
    if (s_dash.control_cb)
    {
        s_dash.control_cb(key, value);
    }
    return s_dash.control_cb != NULL;
}

#endif // PROTOCORE_ENABLE_DASHBOARD
