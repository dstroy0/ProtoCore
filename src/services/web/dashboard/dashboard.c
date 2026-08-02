// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dashboard.c
 * @brief Dashboard widget table + JSON serializers (PC_ENABLE_DASHBOARD).
 *
 * The host-testable core: it owns the widget table and value array and turns them
 * into the layout / values JSON the page consumes. No server or web
 * dependency lives here, so it compiles and unit-tests standalone; the route /
 * SSE wiring is in dashboard_routes.cpp.
 */

#include "services/web/dashboard/dashboard.h"

#if PC_ENABLE_DASHBOARD

#include "mmgr/frame.h"
#include "shared_primitives/numparse.h"
#include <string.h>

// A message key as it appears in the JSON: quoted, so it cannot match a widget key containing it.
static const pc_field QUOTED_KEY[] = {{PC_FK_LIT, 0, 1, "\""}, PC_STR, {PC_FK_LIT, 0, 1, "\""}, PC_END};

// All dashboard state, owned by one instance (internal linkage): the widget table, the
// per-widget value array, and the inbound-control callback, grouped so it is one named owner,
// unreachable from any other translation unit.
typedef struct
{
    const pc_widget *widgets = NULL;
    uint8_t count = 0;
    float values[PC_DASHBOARD_MAX_WIDGETS] = {};
    pc_control_cb control_cb = NULL;
} DashboardCtx;
static DashboardCtx s_dash;

static const char *widget_type_name(pc_widget_type t)
{
    switch (t)
    {
    case PC_WIDGET_GAUGE:
        return "gauge";
    case PC_WIDGET_BAR:
        return "bar";
    case PC_WIDGET_SPARKLINE:
        return "sparkline";
    case PC_WIDGET_CHART:
        return "chart";
    case PC_WIDGET_BUTTON:
        return "button";
    case PC_WIDGET_TOGGLE:
        return "toggle";
    case PC_WIDGET_SLIDER:
        return "slider";
    default:
        return "value";
    }
}

void pc_dashboard_configure(const pc_widget *widgets, uint8_t count)
{
    s_dash.widgets = widgets;
    s_dash.count = count > PC_DASHBOARD_MAX_WIDGETS ? PC_DASHBOARD_MAX_WIDGETS : count;
    for (uint8_t i = 0; i < PC_DASHBOARD_MAX_WIDGETS; i++)
    {
        s_dash.values[i] = 0.0f;
    }
}

proto_bool pc_dashboard_set(const char *key, float value)
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
static const pc_field DASH_ARRAY_OPEN[] = {{PC_FK_LIT, 0, 1, "["}, PC_END};
static const pc_field DASH_ARRAY_CLOSE[] = {{PC_FK_LIT, 0, 1, "]"}, PC_END};
static const pc_field DASH_WIDGET[] = {
    PC_STR,                           // "," from the second widget on
    {PC_FK_LIT, 0, 8, "{\"type\":"},  //
    PC_JSON,                          // type name
    {PC_FK_LIT, 0, 9, ",\"label\":"}, //
    PC_JSON,                          //
    {PC_FK_LIT, 0, 7, ",\"key\":"},   //
    PC_JSON,                          //
    {PC_FK_LIT, 0, 7, ",\"min\":"},   //
    {PC_FK_G, 0, 0, NULL},            // width 0 == 6 significant digits, the %g default
    {PC_FK_LIT, 0, 7, ",\"max\":"},   //
    {PC_FK_G, 0, 0, NULL},            //
    {PC_FK_LIT, 0, 8, ",\"unit\":"},  //
    PC_JSON,                          //
    {PC_FK_LIT, 0, 1, "}"},           //
    PC_END,
};

static const pc_field DASH_OBJECT_OPEN[] = {{PC_FK_LIT, 0, 1, "{"}, PC_END};
static const pc_field DASH_OBJECT_CLOSE[] = {{PC_FK_LIT, 0, 1, "}"}, PC_END};
static const pc_field DASH_VALUE[] = {
    PC_STR,                 // "," from the second pair on
    PC_JSON,                // key
    {PC_FK_LIT, 0, 1, ":"}, //
    {PC_FK_G, 0, 0, NULL},  // the reading
    PC_END,
};

int32_t pc_dashboard_layout_json(char *out, uint32_t cap)
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
    if (pc_frame_append(out, cap, DASH_ARRAY_OPEN) == 0)
    {
        return 0;
    }
    for (uint8_t i = 0; i < s_dash.count; i++)
    {
        const pc_widget *w = &s_dash.widgets[i];
        if (pc_frame_append(out, cap, DASH_WIDGET, i ? "," : "", widget_type_name(w->type), w->label ? w->label : "",
                            w->key ? w->key : "", (double)w->min, (double)w->max, w->unit ? w->unit : "") == 0)
        {
            return 0;
        }
    }
    return (int32_t)pc_frame_append(out, cap, DASH_ARRAY_CLOSE);
}

int32_t pc_dashboard_values_json(char *out, uint32_t cap)
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
    if (pc_frame_append(out, cap, DASH_OBJECT_OPEN) == 0)
    {
        return 0;
    }
    for (uint8_t i = 0; i < s_dash.count; i++)
    {
        if (pc_frame_append(out, cap, DASH_VALUE, i ? "," : "", s_dash.widgets[i].key ? s_dash.widgets[i].key : "",
                            (double)s_dash.values[i]) == 0)
        {
            return 0;
        }
    }
    return (int32_t)pc_frame_append(out, cap, DASH_OBJECT_CLOSE);
}

// ---------------------------------------------------------------------------
// Controls (inbound WebSocket messages)
// ---------------------------------------------------------------------------

void pc_dashboard_on_control(pc_control_cb cb)
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
    pc_frame_build(pat, sizeof(pat), QUOTED_KEY, key);
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

proto_bool pc_dashboard_parse_control(const char *msg, char *key_out, size_t key_cap, float *value_out)
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
    float v = pc_strtof(vp, &end);
    if (end == vp)
    {
        return PROTO_FALSE; // no numeric value
    }
    *value_out = v;
    return PROTO_TRUE;
}

proto_bool pc_dashboard_dispatch_control(const char *msg)
{
    char key[32];
    float value;
    if (!pc_dashboard_parse_control(msg, key, sizeof(key), &value))
    {
        return PROTO_FALSE;
    }
    if (s_dash.control_cb)
    {
        s_dash.control_cb(key, value);
    }
    return s_dash.control_cb != NULL;
}

#endif // PC_ENABLE_DASHBOARD
