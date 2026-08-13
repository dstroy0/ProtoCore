// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_DASHBOARD

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

// ---------------------------------------------------------------------------
// Host-testable core (no server dependency)
// ---------------------------------------------------------------------------

/** @brief Bind the widget table and reset every value to 0. */
void protocore_dashboard_configure(const protocore_widget *widgets, uint8_t count);

/** @brief Set a widget's current value by key. @return false if the key is unknown. */
proto_bool protocore_dashboard_set(const char *key, float value);

/**
 * @brief Serialize the widget layout as a JSON array into @p out.
 * @return number of characters written, or 0 if @p cap is too small.
 */
int32_t protocore_dashboard_layout_json(char *out, uint32_t cap);

/**
 * @brief Serialize the current values as a JSON object {key:value,...} into @p out.
 * @return number of characters written, or 0 if @p cap is too small.
 */
int32_t protocore_dashboard_values_json(char *out, uint32_t cap);

/** @brief Register the callback invoked when a control widget sends a value. */
void protocore_dashboard_on_control(protocore_control_cb cb);

/**
 * @brief Parse a control message `{"k":"<key>","v":<number>}` from the page.
 * @return true if well-formed; writes the key (bounded by @p key_cap) and value.
 */
proto_bool protocore_dashboard_parse_control(const char *msg, char *key_out, size_t key_cap, float *value_out);

/**
 * @brief Parse a control message and invoke the registered control callback.
 * @return true if the message parsed and a callback was set.
 */
proto_bool protocore_dashboard_dispatch_control(const char *msg);

// ---------------------------------------------------------------------------
// Server integration
// ---------------------------------------------------------------------------

/**
 * @brief Serve the dashboard at @p path (page, layout JSON, and SSE value stream).
 *
 * Calls protocore_dashboard_configure(@p widgets, @p count). Default path "/dashboard".
 */
void protocore_dashboard_begin(const char *path, const protocore_widget *widgets, uint8_t count);

/** @brief Broadcast the current values to all SSE subscribers (after protocore_dashboard_set()). */
void protocore_dashboard_publish();

#endif // PROTOCORE_ENABLE_DASHBOARD

PROTOCORE_END_DECLS

#endif // PROTOCORE_DASHBOARD_H
