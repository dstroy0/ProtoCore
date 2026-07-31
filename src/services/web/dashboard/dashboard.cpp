// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dashboard.cpp
 * @brief Dashboard widget table + JSON serializers (PC_ENABLE_DASHBOARD).
 *
 * The host-testable core: it owns the widget table and value array and turns them
 * into the layout / values JSON the page consumes. No server or web
 * dependency lives here, so it compiles and unit-tests standalone; the route /
 * SSE wiring is in dashboard_routes.cpp.
 */

#include "services/web/dashboard/dashboard.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_DASHBOARD

#include "shared_primitives/fmtbuf.h"
#include "shared_primitives/numparse.h"
#include <stdio.h>
#include <string.h>

// All dashboard state, owned by one instance (internal linkage): the widget table, the
// per-widget value array, and the inbound-control callback, grouped so it is one named owner,
// unreachable from any other translation unit.
struct DashboardCtx
{
    const pc_widget *widgets = nullptr;
    uint8_t count = 0;
    float values[PC_DASHBOARD_MAX_WIDGETS] = {};
    pc_control_cb control_cb = nullptr;
};
static DashboardCtx s_dash;

static const char *widget_type_name(pc_widget_type t)
{
    switch (t)
    {
    case pc_widget_type::PC_WIDGET_GAUGE:
        return "gauge";
    case pc_widget_type::PC_WIDGET_BAR:
        return "bar";
    case pc_widget_type::PC_WIDGET_SPARKLINE:
        return "sparkline";
    case pc_widget_type::PC_WIDGET_CHART:
        return "chart";
    case pc_widget_type::PC_WIDGET_BUTTON:
        return "button";
    case pc_widget_type::PC_WIDGET_TOGGLE:
        return "toggle";
    case pc_widget_type::PC_WIDGET_SLIDER:
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

bool pc_dashboard_set(const char *key, float value)
{
    if (!key || !s_dash.widgets)
    {
        return false;
    }
    for (uint8_t i = 0; i < s_dash.count; i++)
    {
        if (s_dash.widgets[i].key && strcmp(s_dash.widgets[i].key, key) == 0)
        {
            s_dash.values[i] = value;
            return true;
        }
    }
    return false;
}

int pc_dashboard_layout_json(char *out, size_t cap)
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
    size_t pos = 0;
    if (pc_fmt_append(out, cap, &pos, "[") != 0)
    {
        return 0;
    }
    for (uint8_t i = 0; i < s_dash.count; i++)
    {
        const pc_widget *w = &s_dash.widgets[i];
        if (pc_fmt_append(out, cap, &pos,
                          "%s{\"type\":\"%s\",\"label\":\"%s\",\"key\":\"%s\",\"min\":%g,\"max\":%g,\"unit\":\"%s\"}",
                          i ? "," : "", widget_type_name(w->type), w->label ? w->label : "", w->key ? w->key : "",
                          (double)w->min, (double)w->max, w->unit ? w->unit : "") != 0)
        {
            return 0;
        }
    }
    if (pc_fmt_append(out, cap, &pos, "]") != 0)
    {
        return 0;
    }
    return (int)pos;
}

int pc_dashboard_values_json(char *out, size_t cap)
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
    size_t pos = 0;
    if (pc_fmt_append(out, cap, &pos, "{") != 0)
    {
        return 0;
    }
    for (uint8_t i = 0; i < s_dash.count; i++)
    {
        if (pc_fmt_append(out, cap, &pos, "%s\"%s\":%g", i ? "," : "",
                          s_dash.widgets[i].key ? s_dash.widgets[i].key : "", (double)s_dash.values[i]) != 0)
        {
            return 0;
        }
    }
    if (pc_fmt_append(out, cap, &pos, "}") != 0)
    {
        return 0;
    }
    return (int)pos;
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
    pc_sb sb_pat = {pat, sizeof(pat), 0, true};
    pc_sb_put(&sb_pat, "\"");
    pc_sb_put(&sb_pat, key);
    pc_sb_put(&sb_pat, "\"");
    if (pc_sb_finish(&sb_pat) == 0)
    {
        pat[0] = '\0';
    }
    const char *p = strstr(s, pat);
    if (!p)
    {
        return nullptr;
    }
    p += strnlen(pat, sizeof(pat));
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    if (*p != ':')
    {
        return nullptr;
    }
    p++;
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    return p;
}

bool pc_dashboard_parse_control(const char *msg, char *key_out, size_t key_cap, float *value_out)
{
    if (!msg || !key_out || key_cap == 0 || !value_out)
    {
        return false;
    }
    key_out[0] = '\0';
    const char *kp = control_value_ptr(msg, "k");
    const char *vp = control_value_ptr(msg, "v");
    if (!kp || !vp || *kp != '"')
    {
        return false;
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
        return false; // unterminated or key too long
    }
    key_out[i] = '\0';
    const char *end = nullptr;
    float v = pc_strtof(vp, &end);
    if (end == vp)
    {
        return false; // no numeric value
    }
    *value_out = v;
    return true;
}

bool pc_dashboard_dispatch_control(const char *msg)
{
    char key[32];
    float value;
    if (!pc_dashboard_parse_control(msg, key, sizeof(key), &value))
    {
        return false;
    }
    if (s_dash.control_cb)
    {
        s_dash.control_cb(key, value);
    }
    return s_dash.control_cb != nullptr;
}

#endif // PC_ENABLE_DASHBOARD
