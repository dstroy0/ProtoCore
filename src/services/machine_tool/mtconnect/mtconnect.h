// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mtconnect.h
 * @brief MTConnect agent response codec (PROTOCORE_ENABLE_MTCONNECT).
 *
 * MTConnect (ANSI/MTC1.4) is the manufacturing-equipment read standard: an HTTP agent answers `probe`,
 * `current`, `sample`, and `asset` requests with XML documents. This builds those response documents
 * into a caller buffer, so the web server is an MTConnect agent over the existing HTTP stack:
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
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MTCONNECT_H
#define PROTOCORE_MTCONNECT_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MTCONNECT

PROTOCORE_BEGIN_DECLS

// PROTOCORE_MTCONNECT_BORROW - the bytes a document runs out of - is stated in protocore_config.h,
// which sums it into the plaintext arena. A caller takes them once and passes the pointer to every
// call. The borrow IS the document, so two documents are two borrows and never collide.

/** @brief The MTConnect DataItem category (which stream element wraps the value). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_MTC_SAMPLE,   ///< a measured value (<Samples>).
    PROTOCORE_MTC_EVENT,    ///< a discrete state (<Events>).
    PROTOCORE_MTC_CONDITION ///< a condition (<Condition>): value is the sub-element name (Normal/Warning/Fault).
} protocore_mtc_category;

/** @brief The buffer a document is built into, and the agent identity its Header carries. */
typedef struct
{
    char *out;            ///< where the document lands
    size_t cap;           ///< how many bytes of it there are
    uint64_t instance_id; ///< the agent's boot id (the Header instanceId)
    const char *sender;   ///< the agent's own name (the Header sender, MTC1.4 HeaderType)
} MtConnectDocArgs;

/** @brief What opening a streams document takes beyond the document itself. */
typedef struct
{
    uint64_t next_seq;        ///< the sequence the next observation will carry
    const char *device_name;  ///< the DeviceStream name
    const char *device_uuid;  ///< its uuid, which DeviceStreamType marks required
    const char *component;    ///< the ComponentStream component name
    const char *component_id; ///< its id, which ComponentStreamType marks required
} MtConnectStreamsArgs;

/** @brief One observation added to the open component. */
typedef struct
{
    protocore_mtc_category cat; ///< which container it belongs in
    const char *type;           ///< the DataItem type element name
    const char *data_id;        ///< its dataItemId
    uint64_t seq;               ///< its sequence number
    const char *timestamp;      ///< its ISO-8601 timestamp
    const char *value;          ///< its value, or the Condition sub-element name
} MtConnectObsArgs;

/** @brief The window a sample response reports, beyond the streams members. */
typedef struct
{
    uint64_t first_seq;   ///< the oldest sequence still retained
    uint64_t last_seq;    ///< the newest one written
    uint32_t buffer_size; ///< how many observations the agent retains
} MtConnectWindowArgs;

/** @brief What opening a probe document takes. */
typedef struct
{
    const char *device_id;   ///< the Device id
    const char *device_name; ///< its name
    const char *uuid;        ///< its uuid
} MtConnectDeviceArgs;

/** @brief One DataItem in the probe document. */
typedef struct
{
    protocore_mtc_category cat; ///< its category attribute
    const char *id;             ///< its id
    const char *type;           ///< its type
    const char *name;           ///< optional name (omitted when null/empty)
    const char *units;          ///< optional units (omitted when null/empty)
} MtConnectItemArgs;

/** @brief What opening an asset document takes. */
typedef struct
{
    uint32_t asset_count;       ///< assets in this response (the Header assetCount)
    uint32_t asset_buffer_size; ///< the agent's asset capacity (the Header assetBufferSize)
} MtConnectAssetsArgs;

/** @brief One CuttingTool and the status its life cycle opens with. */
typedef struct
{
    const char *asset_id;      ///< the CuttingTool assetId
    const char *serial_number; ///< optional serialNumber
    const char *tool_id;       ///< optional toolId
    const char *device_uuid;   ///< optional deviceUuid
    const char *timestamp;     ///< optional ISO-8601 timestamp
    const char *cutter_status; ///< the CutterStatus the life cycle opens with (minOccurs=1)
} MtConnectToolArgs;

/** @brief One ToolLife element. LifeType marks all four attributes required. */
typedef struct
{
    const char *type;            ///< "MINUTES", "PART_COUNT" or "WEAR"
    const char *count_direction; ///< "UP" or "DOWN"
    const char *initial;         ///< the life the tool started with
    const char *limit;           ///< the threshold the count runs to
    const char *value;           ///< the current life
} MtConnectLifeArgs;

/** @brief The error an MTConnectError document reports. */
typedef struct
{
    const char *error_code; ///< the errorCode attribute
    const char *message;    ///< the element text
} MtConnectErrorArgs;

/** @brief The sub-window a sample replay asks the ring for. */
typedef struct
{
    uint64_t from;  ///< the first sequence wanted
    uint32_t count; ///< how many at most
} MtConnectQueryArgs;

/**
 * @brief MTConnect (ANSI/MTC1.4) agent responses.
 *
 * A caller sets the members a call takes, invokes it through ::MtConnect with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 *   MtConnect.doc.out = buf;
 *   MtConnect.doc.cap = sizeof(buf);
 *   MtConnect.doc.instance_id = 7;
 *   MtConnect.doc.sender = "agent-1";
 *   MtConnect.streams.next_seq = 100;
 *   MtConnect.streams.device_name = "VF2";
 *   MtConnect.streams.device_uuid = "uuid-1";
 *   MtConnect.streams_begin(work);
 *   MtConnect.obs.cat = PROTOCORE_MTC_SAMPLE;
 *   ...
 *   MtConnect.streams_add(work);
 *   MtConnect.streams_end(work);
 *   // MtConnect.n is the document length
 *
 * @var MtConnectNs::doc       the buffer a document is built into, and the agent identity
 * @var MtConnectNs::streams   what opening a streams document takes
 * @var MtConnectNs::obs       one observation added to the open component
 * @var MtConnectNs::window    the window a sample response reports
 * @var MtConnectNs::device    what opening a probe document takes
 * @var MtConnectNs::item      one DataItem in the probe document
 * @var MtConnectNs::assets    what opening an asset document takes
 * @var MtConnectNs::tool      one CuttingTool and its opening status
 * @var MtConnectNs::life      one ToolLife element
 * @var MtConnectNs::err       the error an MTConnectError document reports
 * @var MtConnectNs::query     the sub-window a sample replay asks the ring for
 * @var MtConnectNs::ok        a call's true/false outcome
 * @var MtConnectNs::n         a finished document's length, or 0 when it did not fit
 * @var MtConnectNs::seq       the sequence number the last ring add assigned
 * @var MtConnectNs::streams_begin  open a streams document and its device + component
 * @var MtConnectNs::streams_add    add one observation to the open component
 * @var MtConnectNs::streams_end    close the component, the device and the document
 * @var MtConnectNs::error          build a whole MTConnectError document
 * @var MtConnectNs::devices_begin  open a probe document and its device
 * @var MtConnectNs::devices_add    add one DataItem to it
 * @var MtConnectNs::devices_end    close the probe document
 * @var MtConnectNs::assets_begin   open an asset document
 * @var MtConnectNs::tool_begin     open one CuttingTool and its life cycle
 * @var MtConnectNs::tool_life      add one ToolLife to the open life cycle
 * @var MtConnectNs::tool_end       close the life cycle and the tool
 * @var MtConnectNs::assets_end     close the asset document
 * @var MtConnectNs::ring_init      empty the observation ring and seat its first sequence
 * @var MtConnectNs::ring_add       record one observation, assigning it the next sequence
 * @var MtConnectNs::ring_query     replay a window of the ring as a streams document
 *
 * A ComponentStream is an xs:sequence of Samples, Events and Condition, each maxOccurs="1", so an
 * observation is accumulated in the region its category owns and the three are written out in that
 * order when the component closes. The containers are therefore never opened in arrival order, and a
 * caller adds observations in whatever order it has them.
 *
 * @c work is PROTOCORE_MTCONNECT_BORROW plaintext bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call. The borrow IS the document and the ring, so two
 * agents are two borrows and never collide.
 */
typedef struct
{
    MtConnectDocArgs doc;
    MtConnectStreamsArgs streams;
    MtConnectObsArgs obs;
    MtConnectWindowArgs window;
    MtConnectDeviceArgs device;
    MtConnectItemArgs item;
    MtConnectAssetsArgs assets;
    MtConnectToolArgs tool;
    MtConnectLifeArgs life;
    MtConnectErrorArgs err;
    MtConnectQueryArgs query;
    proto_bool ok;
    size_t n;
    uint64_t seq;
} MtConnectVars;

/** @brief The operands and the outcome. */
extern MtConnectVars MtConnectV;

/** @brief The entries. */
typedef struct
{
    void (*const streams_begin)(uint8_t *restrict work);
    void (*const streams_add)(uint8_t *restrict work);
    void (*const streams_end)(uint8_t *restrict work);
    void (*const error)(uint8_t *restrict work);
    void (*const devices_begin)(uint8_t *restrict work);
    void (*const devices_add)(uint8_t *restrict work);
    void (*const devices_end)(uint8_t *restrict work);
    void (*const assets_begin)(uint8_t *restrict work);
    void (*const tool_begin)(uint8_t *restrict work);
    void (*const tool_life)(uint8_t *restrict work);
    void (*const tool_end)(uint8_t *restrict work);
    void (*const assets_end)(uint8_t *restrict work);
    void (*const ring_init)(uint8_t *restrict work);
    void (*const ring_add)(uint8_t *restrict work);
    void (*const ring_query)(uint8_t *restrict work);
} MtConnectNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MtConnectV or a region of the borrow at a fixed offset.
void protocore_mt_connect_streams_begin(uint8_t *restrict work);
void protocore_mt_connect_streams_add(uint8_t *restrict work);
void protocore_mt_connect_streams_end(uint8_t *restrict work);
void protocore_mt_connect_error(uint8_t *restrict work);
void protocore_mt_connect_devices_begin(uint8_t *restrict work);
void protocore_mt_connect_devices_add(uint8_t *restrict work);
void protocore_mt_connect_devices_end(uint8_t *restrict work);
void protocore_mt_connect_assets_begin(uint8_t *restrict work);
void protocore_mt_connect_tool_begin(uint8_t *restrict work);
void protocore_mt_connect_tool_life(uint8_t *restrict work);
void protocore_mt_connect_tool_end(uint8_t *restrict work);
void protocore_mt_connect_assets_end(uint8_t *restrict work);
void protocore_mt_connect_ring_init(uint8_t *restrict work);
void protocore_mt_connect_ring_add(uint8_t *restrict work);
void protocore_mt_connect_ring_query(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `MtConnect.streams_begin(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MtConnectNs MtConnect __attribute__((unused)) = {
    .streams_begin = protocore_mt_connect_streams_begin,
    .streams_add = protocore_mt_connect_streams_add,
    .streams_end = protocore_mt_connect_streams_end,
    .error = protocore_mt_connect_error,
    .devices_begin = protocore_mt_connect_devices_begin,
    .devices_add = protocore_mt_connect_devices_add,
    .devices_end = protocore_mt_connect_devices_end,
    .assets_begin = protocore_mt_connect_assets_begin,
    .tool_begin = protocore_mt_connect_tool_begin,
    .tool_life = protocore_mt_connect_tool_life,
    .tool_end = protocore_mt_connect_tool_end,
    .assets_end = protocore_mt_connect_assets_end,
    .ring_init = protocore_mt_connect_ring_init,
    .ring_add = protocore_mt_connect_ring_add,
    .ring_query = protocore_mt_connect_ring_query,
};

/**
 * @brief The bytes every entry here runs out of: the running document and the observation ring.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where that
 * borrow comes from. Taken once from the end of the plaintext pool, which no mark and no release
 * walks, because the ring outlives the documents replayed out of it.
 *
 * @return the span.
 */
uint8_t *protocore_mtconnect_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MTCONNECT

#endif // PROTOCORE_MTCONNECT_H
