// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mtconnect.h
 * @brief MTConnect agent response codec (PC_ENABLE_MTCONNECT).
 *
 * MTConnect (ANSI/MTC1.4) is the manufacturing-equipment read standard: an HTTP agent answers `probe`,
 * `current`, `sample`, and `asset` requests with XML documents. This builds the two most-used response
 * documents into a caller buffer, so the web server is an MTConnect agent over the existing HTTP stack:
 *
 *  - **MTConnectStreams** (the `current` / `sample` response): a header carrying the agent
 *    instanceId + nextSequence, then per-DataItem `<Samples>/<Events>/<Condition>` values.
 *  - **MTConnectDevices** (the `probe` response): the device model - a `<Device>` with its
 *    `<DataItems>` (each `category`/`id`/`type`, optional `name`/`units`) - that a client
 *    discovers before it streams.
 *  - **MTConnectAssets** (the `asset` response): the tool/fixture inventory - a `<CuttingTool>`
 *    with its `<CuttingToolLifeCycle>` (`<ToolLife>` remaining minutes / part count) - that a
 *    client reads out of band from the observation stream.
 *  - **MTConnectError** (a request error): the header + an `<Errors><Error errorCode=..>` element.
 *
 * A streams document is assembled incrementally: open it, add each observation, close it. The instanceId
 * (an agent-boot id) + a monotonically increasing sequence number give a subscriber the from/count
 * long-poll semantics. Pure text framing, zero heap, no stdlib, host-testable; values are XML-escaped.
 */

#ifndef PROTOCORE_MTCONNECT_H
#define PROTOCORE_MTCONNECT_H

#include "protocore_config.h"

#if PC_ENABLE_MTCONNECT

/** @brief The MTConnect DataItem category (which stream element wraps the value). */
typedef enum PROTO_ENUM_PACKED
{
    PC_MTC_SAMPLE,   ///< a measured value (<Samples>).
    PC_MTC_EVENT,    ///< a discrete state (<Events>).
    PC_MTC_CONDITION ///< a condition (<Condition>): value is the sub-element name (Normal/Warning/Fault).
} pc_mtc_category;

/** @brief Incremental MTConnectStreams builder over a caller buffer. */
typedef struct
{
    char *buf;
    size_t cap;
    size_t len;         ///< bytes written so far (excl NUL).
    proto_bool ok;      ///< cleared on any overflow; the final length is 0 when not ok.
    proto_bool in_comp; ///< a <ComponentStream> is open.
} pc_mtc_streams;

/**
 * @brief Begin an MTConnectStreams document: XML declaration + header + open <Streams>.
 * @param instance_id agent instanceId (boot id).
 * @param next_seq    the nextSequence the agent will assign.
 * @param device_name the single device's name/uuid for the ComponentStream.
 */
void pc_mtc_streams_begin(pc_mtc_streams *s, char *buf, size_t cap, uint64_t instance_id, uint64_t next_seq,
                          const char *device_name);

/**
 * @brief Add one observation.
 * @param cat        the DataItem category.
 * @param type       the DataItem type element name (e.g. "Position", "Execution", "Availability").
 * @param data_id    the DataItem id attribute.
 * @param seq        this observation's sequence number.
 * @param timestamp  ISO-8601 timestamp string.
 * @param value      the value text (XML-escaped); for a CONDITION it is the sub-element (Normal/Fault/...).
 */
void pc_mtc_streams_add(pc_mtc_streams *s, pc_mtc_category cat, const char *type, const char *data_id, uint64_t seq,
                        const char *timestamp, const char *value);

/** @brief Finish the document (close any open component + <Streams> + root). @return length, or 0 on overflow. */
size_t pc_mtc_streams_end(pc_mtc_streams *s);

/**
 * @brief Build a complete MTConnectError document.
 * @return length written, or 0 on overflow.
 */
size_t pc_mtc_error(uint64_t instance_id, const char *error_code, const char *message, char *out, size_t cap);

/**
 * @brief Begin an MTConnectDevices (`probe`) document: XML declaration + header + open one `<Device>`.
 * @param instance_id agent instanceId (boot id).
 * @param device_id   the Device `id` attribute.
 * @param device_name the Device `name` attribute.
 * @param uuid        the Device `uuid` attribute.
 *
 * Reuses ::pc_mtc_streams as the incremental buffer builder (as ::pc_mtc_error does).
 */
void pc_mtc_devices_begin(pc_mtc_streams *s, char *buf, size_t cap, uint64_t instance_id, const char *device_id,
                          const char *device_name, const char *uuid);

/**
 * @brief Add one `<DataItem>` to the device model.
 * @param cat   the DataItem category (SAMPLE / EVENT / CONDITION).
 * @param id    the DataItem `id` attribute.
 * @param type  the DataItem `type` attribute (e.g. "Position", "Execution", "Availability").
 * @param name  optional `name` attribute (omitted when null/empty).
 * @param units optional `units` attribute (omitted when null/empty).
 */
void pc_mtc_devices_add_item(pc_mtc_streams *s, pc_mtc_category cat, const char *id, const char *type, const char *name,
                             const char *units);

/** @brief Finish the probe document (close `<DataItems>` + `<Device>` + root). @return length, or 0 on overflow. */
size_t pc_mtc_devices_end(pc_mtc_streams *s);

/**
 * @brief Begin an MTConnectAssets (`asset`) document: XML declaration + header + open `<Assets>`.
 * @param instance_id       agent instanceId (boot id).
 * @param asset_count       the number of assets in this response (the Header `assetCount`).
 * @param asset_buffer_size the agent's total asset capacity (the Header `assetBufferSize`).
 *
 * Reuses ::pc_mtc_streams as the incremental buffer builder (as ::pc_mtc_devices_begin does).
 */
void pc_mtc_assets_begin(pc_mtc_streams *s, char *buf, size_t cap, uint64_t instance_id, uint32_t asset_count,
                         uint32_t asset_buffer_size);

/**
 * @brief Open one `<CuttingTool>` asset and its `<CuttingToolLifeCycle>`.
 * @param asset_id      the CuttingTool `assetId` (required).
 * @param serial_number optional `serialNumber` attribute (omitted when null/empty).
 * @param tool_id       optional `toolId` attribute (omitted when null/empty).
 * @param device_uuid   optional `deviceUuid` attribute (omitted when null/empty).
 * @param timestamp     optional ISO-8601 `timestamp` attribute (omitted when null/empty).
 */
void pc_mtc_assets_cutting_tool_begin(pc_mtc_streams *s, const char *asset_id, const char *serial_number,
                                      const char *tool_id, const char *device_uuid, const char *timestamp);

/**
 * @brief Add one `<ToolLife>` element to the open cutting tool's life cycle.
 * @param type            the life kind: "MINUTES", "PART_COUNT", or "WEAR".
 * @param count_direction "UP" (accumulating) or "DOWN" (remaining).
 * @param limit           optional `limit` attribute (the max/threshold; omitted when null/empty).
 * @param value           the current life value text (XML-escaped).
 */
void pc_mtc_assets_tool_life(pc_mtc_streams *s, const char *type, const char *count_direction, const char *limit,
                             const char *value);

/** @brief Close the open `<CuttingToolLifeCycle>` + `<CuttingTool>`. */
void pc_mtc_assets_cutting_tool_end(pc_mtc_streams *s);

/** @brief Finish the asset document (close `<Assets>` + root). @return length, or 0 on overflow. */
size_t pc_mtc_assets_end(pc_mtc_streams *s);

// --- sample sequence cursor: a rolling observation buffer for the `sample` from/count long-poll ---

/** @brief One buffered observation (a value at a sequence number), stored in fixed fields. */
typedef struct
{
    pc_mtc_category cat;
    uint64_t seq;                  ///< the monotonic sequence number assigned when it was recorded.
    char type[PC_MTC_STR_MAX + 1]; ///< DataItem type element name (e.g. "Position").
    char data_id[PC_MTC_STR_MAX + 1];
    char timestamp[PC_MTC_TS_MAX + 1];
    char value[PC_MTC_VAL_MAX + 1];
} pc_mtc_observation;

/**
 * @brief A fixed-size ring of the most recent observations, with the agent's sequence bookkeeping.
 *
 * Holds up to ::PC_MTC_SAMPLE_BUFFER observations. Each ::pc_mtc_sample_buffer_add assigns the
 * next sequence number; when the ring is full the oldest is evicted and `first_seq` advances, so the
 * retained window is always `[first_seq, next_seq)`. ::pc_mtc_sample_query then replays a requested
 * sub-window as an MTConnectStreams document whose header carries firstSequence / lastSequence /
 * nextSequence (MTC1.4 §6.7). Zero heap, single-owner (the caller serializes access).
 */
typedef struct
{
    pc_mtc_observation obs[PC_MTC_SAMPLE_BUFFER];
    uint32_t count;     ///< valid entries (<= PC_MTC_SAMPLE_BUFFER).
    uint32_t head;      ///< ring write index (next slot to fill).
    uint64_t next_seq;  ///< sequence the next add will assign (one past the newest).
    uint64_t first_seq; ///< sequence of the oldest retained observation.
} pc_mtc_sample_buffer;

/**
 * @brief Initialize an empty sample buffer.
 * @param start_seq the first sequence number the agent will assign (0 is treated as 1).
 */
void pc_mtc_sample_buffer_init(pc_mtc_sample_buffer *b, uint64_t start_seq);

/**
 * @brief Record one observation, assigning it the next sequence number (evicting the oldest if full).
 * @return the sequence number assigned to this observation.
 */
uint64_t pc_mtc_sample_buffer_add(pc_mtc_sample_buffer *b, pc_mtc_category cat, const char *type, const char *data_id,
                                  const char *timestamp, const char *value);

/**
 * @brief Build the `sample` MTConnectStreams response for the window starting at @p from.
 *
 * Emits up to @p count observations from sequence @p from onward (a @p from below the retained
 * firstSequence is clamped up to it - a stale subscriber catches up from the oldest kept, and the header
 * firstSequence tells it data was dropped). The header reports firstSequence / lastSequence and a
 * nextSequence the client uses to resume (the sequence past the last one returned, or the buffer's
 * nextSequence when @p from is already at/after the newest). @return document length, or 0 on overflow.
 */
size_t pc_mtc_sample_query(const pc_mtc_sample_buffer *b, char *buf, size_t cap, uint64_t instance_id,
                           const char *device_name, uint64_t from, uint32_t count);

#endif // PC_ENABLE_MTCONNECT
#endif // PROTOCORE_MTCONNECT_H
