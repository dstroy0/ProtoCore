// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dashboard.h
 * @brief Real-time SVG telemetry dashboard (PROTOCORE_ENABLE_DASHBOARD).
 *
 * Widgets are declared once in a fixed compile-time protocore_widget table - no heap,
 * fixed at link. protocore_dashboard_begin() serves three things at @p path:
 *   - GET path           the self-contained SVG dashboard page (from web);
 *   - GET path/layout    the widget table serialized as a JSON array;
 *   - SSE path/stream     a live stream of the current values.
 * The page fetches the layout, renders one SVG widget per entry, and updates them
 * from the SSE value stream. The application feeds readings with
 * protocore_dashboard_set(key, value) and pushes them with protocore_dashboard_publish().
 *
 * The widget-table -> JSON serializers (layout + values) are pure and have no
 * server dependency, so they unit-test on the host. Requires PROTOCORE_ENABLE_SSE.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DASHBOARD_H
#define PROTOCORE_DASHBOARD_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_DASHBOARD

PROTOCORE_BEGIN_DECLS

// PROTOCORE_DASHBOARD_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief Widget rendering / interaction style. */
typedef enum PROTO_ENUM_PACKED
{
    // Display widgets - updated from the SSE value stream.
    PROTOCORE_WIDGET_VALUE = 0, ///< plain numeric readout
    PROTOCORE_WIDGET_GAUGE,     ///< radial arc gauge over [min, max]
    PROTOCORE_WIDGET_BAR,       ///< horizontal bar over [min, max]
    PROTOCORE_WIDGET_SPARKLINE, ///< recent-history SVG line over [min, max]
    PROTOCORE_WIDGET_CHART,     ///< dense Canvas line chart over [min, max]
    // Control widgets - send values back to the device over WebSocket.
    PROTOCORE_WIDGET_BUTTON, ///< momentary button -> control value 1
    PROTOCORE_WIDGET_TOGGLE, ///< on/off toggle -> control value 0/1 (reflects SSE state)
    PROTOCORE_WIDGET_SLIDER  ///< range slider over [min, max] -> control value
} protocore_widget_type;

/** @brief Control callback: invoked when a control widget sends a value over WebSocket. */
typedef void (*protocore_control_cb)(const char *key, float value);

/** @brief One dashboard widget, declared in a fixed compile-time table. */
typedef struct
{
    protocore_widget_type type; ///< rendering style.
    const char *label;          ///< display label.
    const char *key;            ///< telemetry source key (matches protocore_dashboard_set()).
    float min;                  ///< scale minimum (gauge / bar / sparkline).
    float max;                  ///< scale maximum.
    const char *unit;           ///< unit suffix shown by the widget (may be "").
} protocore_widget;

/** @brief What configure takes: widgets, count. */
typedef struct
{
    const protocore_widget *widgets;
    uint8_t count;
} DashboardConfigureArgs;

/** @brief What set takes: key, value. */
typedef struct
{
    const char *key;
    float value;
} DashboardSetArgs;

/** @brief What layout_json takes: out, cap. */
typedef struct
{
    char *out;
    uint32_t cap;
} DashboardLayoutJsonArgs;

/** @brief What values_json takes: out, cap. */
typedef struct
{
    char *out;
    uint32_t cap;
} DashboardValuesJsonArgs;

/** @brief What on_control takes: cb. */
typedef struct
{
    protocore_control_cb cb;
} DashboardOnControlArgs;

/** @brief What parse_control takes: msg, key_out, key_cap, value_out. */
typedef struct
{
    const char *msg;
    char *key_out;
    size_t key_cap;
    float *value_out;
} DashboardParseControlArgs;

/** @brief What dispatch_control takes: msg. */
typedef struct
{
    const char *msg;
} DashboardDispatchControlArgs;

/** @brief What begin takes: path, widgets, count. */
typedef struct
{
    const char *path;
    const protocore_widget *widgets;
    uint8_t count;
} DashboardBeginArgs;

/**
 * @brief Real-time SVG telemetry dashboard (PROTOCORE_ENABLE_DASHBOARD).
 *
 * A caller sets the members a call takes, invokes it through ::Dashboard with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Dashboard.configure_args.widgets = ...;
 *   Dashboard.configure_args.count = ...;
 *   Dashboard.configure(work);
 *
 * @var DashboardNs::configure_args  what configure takes: widgets, count
 * @var DashboardNs::set_args  what set takes: key, value
 * @var DashboardNs::layout_json_args  what layout_json takes: out, cap
 * @var DashboardNs::values_json_args  what values_json takes: out, cap
 * @var DashboardNs::on_control_args  what on_control takes: cb
 * @var DashboardNs::parse_control_args  what parse_control takes: msg, key_out, key_cap, value_out
 * @var DashboardNs::dispatch_control_args  what dispatch_control takes: msg
 * @var DashboardNs::begin_args  what begin takes: path, widgets, count
 * @var DashboardNs::ok  true if well-formed; writes the key (bounded by key_cap) and value
 * @var DashboardNs::value  number of characters written, or 0 if cap is too small
 * @var DashboardNs::configure  bind the widget table and reset every value to 0
 * @var DashboardNs::set  set a widget's current value by key. false if the key is unknown
 * @var DashboardNs::layout_json  serialize the widget layout as a JSON array into out
 * @var DashboardNs::values_json  serialize the current values as a JSON object {key:value,...} into ...
 * @var DashboardNs::on_control  register the callback invoked when a control widget sends a value
 * @var DashboardNs::parse_control  parse a control message `{"k":"<key>","v":<number>}` from the page
 * @var DashboardNs::dispatch_control  parse a control message and invoke the registered control callback
 * @var DashboardNs::begin  serve the dashboard at path (page, layout JSON, and SSE value ...
 * @var DashboardNs::publish  broadcast the current values to all SSE subscribers (after ...
 *
 * @c work is PROTOCORE_DASHBOARD_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    DashboardConfigureArgs configure_args;
    DashboardSetArgs set_args;
    DashboardLayoutJsonArgs layout_json_args;
    DashboardValuesJsonArgs values_json_args;
    DashboardOnControlArgs on_control_args;
    DashboardParseControlArgs parse_control_args;
    DashboardDispatchControlArgs dispatch_control_args;
    DashboardBeginArgs begin_args;

    proto_bool ok;
    int32_t value;

    void (*const configure)(uint8_t *restrict work);
    void (*const set)(uint8_t *restrict work);
    void (*const layout_json)(uint8_t *restrict work);
    void (*const values_json)(uint8_t *restrict work);
    void (*const on_control)(uint8_t *restrict work);
    void (*const parse_control)(uint8_t *restrict work);
    void (*const dispatch_control)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const publish)(uint8_t *restrict work);
} DashboardNs;

/** @brief The one symbol this module exports. */
extern DashboardNs Dashboard;

/**
 * @brief The PROTOCORE_DASHBOARD_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_dashboard_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DASHBOARD

#endif // PROTOCORE_DASHBOARD_H
