// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file umati.h
 * @brief umati - OPC UA for Machine Tools (OPC 40501-1) information model (PROTOCORE_ENABLE_UMATI).
 *
 * umati ("universal machine technology interface") is the OPC UA companion specification for machine
 * tools (VDW / OPC Foundation, OPC 40501-1, namespace `http://opcfoundation.org/UA/MachineTool/`). It
 * standardizes how a machine tool exposes its identity and live state so any umati / OPC UA client (the
 * umati dashboard, UaExpert, python `asyncua`, ...) reads the same structure across vendors.
 *
 * This module builds the MachineTool address space on top of the OPC UA Binary server
 * (`services/opcua`, PROTOCORE_ENABLE_OPCUA): it registers a Browse + Read resolver that answers for the
 * MachineTool node hierarchy and serves live values out of a caller-owned @ref UmatiMachineTool struct
 * you refresh in your loop. No heap, no stdlib - the model is a fixed node table, the values are
 * pointers/scalars in your struct.
 *
 * Model exposed (BrowseNames per OPC 40501-1), under the Objects folder:
 *
 *   MachineTool
 *     Identification   Manufacturer, Model, SerialNumber, YearOfConstruction, SoftwareRevision,
 *                      ProductInstanceUri
 *     Monitoring
 *       MachineTool    OperationMode, PowerOnDuration
 *       Channel        ChannelState, FeedOverride, RapidOverride, ActiveProgram
 *       Spindle        RotationSpeed, OverrideValue, IsRotating
 *       Axis_X/Y/Z     ActualPosition
 *     Production       ActiveProgram, ProducedPartCount
 *     Notification     ActiveMessage, Severity
 *
 * The model is read-only (a monitoring model - the machine reports, the client observes). Scope note:
 * a single Channel/Spindle and three linear axes are exposed (the common embedded machine); the values
 * carry faithful BrowseNames, but the companion-spec TypeDefinitions and the NamespaceArray entry for
 * the MachineTool URI (which needs array-Variant support in the base server) are a documented follow-on
 * - a generic OPC UA client still browses the structure and reads every value by BrowseName today.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date     2026
 */

#ifndef PROTOCORE_UMATI_H
#define PROTOCORE_UMATI_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_UMATI

PROTOCORE_BEGIN_DECLS

// PROTOCORE_UMATI_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief The OPC UA for Machine Tools companion-spec namespace URI (OPC 40501-1). */
#define UMATI_NS_URI "http://opcfoundation.org/UA/MachineTool/"

/**
 * @brief MachineTool OperationMode (OPC 40501-1 MachineOperationMode). Exposed as Int32; the numeric
 *        values follow the companion-spec enumeration. The machine reports its current mode.
 */
typedef enum PROTO_ENUM_PACKED
{
    UMATI_OP_OTHER = 0,     ///< a mode outside the ones below.
    UMATI_OP_MANUAL = 1,    ///< hand / jog operation.
    UMATI_OP_MDA = 2,       ///< manual data automatic (single-block MDI).
    UMATI_OP_AUTOMATIC = 3, ///< running a stored program automatically.
    UMATI_OP_SETUP = 4,     ///< set-up / preparation.
} UmatiOperationMode;

/**
 * @brief Channel state (OPC 40501-1 ChannelState). Exposed as Int32; the numeric values follow the
 *        companion-spec enumeration.
 */
typedef enum PROTO_ENUM_PACKED
{
    UMATI_CH_INTERRUPTED = 0, ///< program interrupted / feed hold.
    UMATI_CH_RESET = 1,       ///< channel reset (no program active).
    UMATI_CH_RUNNING = 2,     ///< a program is executing.
    UMATI_CH_WAITING = 3,     ///< waiting (e.g. for a synchronization / dwell).
} UmatiChannelState;

/** @brief MachineTool identification (OPC 40501-1 Identification, subtype of Machinery MachineIdentification). */
typedef struct
{
    const char *manufacturer;         ///< Manufacturer (LocalizedText -> String).
    const char *model;                ///< Model (LocalizedText -> String).
    const char *serial_number;        ///< SerialNumber (String).
    const char *software_revision;    ///< SoftwareRevision (String).
    const char *product_instance_uri; ///< ProductInstanceUri (String) - the unique instance URI.
    uint16_t year_of_construction;    ///< YearOfConstruction (UInt16, exposed as UInt32).
} UmatiIdentification;

/** @brief One Channel's live monitoring values (OPC 40501-1 ChannelType, common subset). */
typedef struct
{
    UmatiChannelState state;    ///< ChannelState.
    double feed_override;       ///< FeedOverride (%).
    double rapid_override;      ///< RapidOverride (%).
    const char *active_program; ///< ActiveProgram.Name (the running NC program).
} UmatiChannel;

/** @brief One Spindle's live monitoring values (OPC 40501-1 SpindleType, common subset). */
typedef struct
{
    double rotation_speed;  ///< RotationSpeed (rpm).
    double override_value;  ///< OverrideValue (%).
    proto_bool is_rotating; ///< IsRotating.
} UmatiSpindle;

/** @brief One linear Axis' live monitoring value (OPC 40501-1 LinearAxisType, common subset). */
typedef struct
{
    double actual_position; ///< ActualPosition (mm).
} UmatiAxis;

/**
 * @brief The whole MachineTool the server exposes. Own it in your sketch and refresh its fields each
 *        loop from your machine I/O; the umati resolvers read straight out of it (no copy). String
 *        fields may be null (served as an empty String).
 */
typedef struct
{
    const char *name;                  ///< MachineTool BrowseName / DisplayName (the machine's name).
    UmatiIdentification ident;         ///< Identification.
    UmatiOperationMode operation_mode; ///< Monitoring.MachineTool.OperationMode.
    double power_on_duration_s;        ///< Monitoring.MachineTool.PowerOnDuration (seconds).
    UmatiChannel channel;              ///< Monitoring.Channel.
    UmatiSpindle spindle;              ///< Monitoring.Spindle.
    UmatiAxis axis_x;                  ///< Monitoring.Axis_X.
    UmatiAxis axis_y;                  ///< Monitoring.Axis_Y.
    UmatiAxis axis_z;                  ///< Monitoring.Axis_Z.
    const char *active_program;        ///< Production.ActiveProgram.Name.
    uint32_t produced_part_count;      ///< Production.ProducedPartCount.
    const char *message_text;          ///< Notification.ActiveMessage (most-recent active message text).
    uint32_t message_severity;         ///< Notification.Severity (0..1000, OPC UA event severity scale).
} UmatiMachineTool;

/** @brief What bind takes: mt. */
typedef struct
{
    const UmatiMachineTool *mt;
} UmatiBindArgs;

/** @brief What install takes: mt. */
typedef struct
{
    const UmatiMachineTool *mt;
} UmatiInstallArgs;

/**
 * @brief umati - OPC UA for Machine Tools (OPC 40501-1) information model (PROTOCORE_ENABLE_UMATI). umati ("universal
 * machine technology interface") is the OPC UA companion specification for machine tools (VDW / OPC Foundation, OPC
 * 40501-1, namespace `http://opcfoundation.org/UA/MachineTool/`).
 *
 * A caller sets the members a call takes, invokes it through ::Umati with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Umati.bind_args.mt = ...;
 *   Umati.bind(work);
 *
 * @var UmatiNs::bind_args  what bind takes: mt
 * @var UmatiNs::install_args  what install takes: mt
 * @var UmatiNs::ok  a call's true/false outcome
 * @var UmatiNs::bind  bind the MachineTool the resolvers serve. mt must outlive the ...
 * @var UmatiNs::install  convenience: bind mt and register both resolvers on the OPC UA ...
 *
 * @c work is PROTOCORE_UMATI_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    UmatiBindArgs bind_args;
    UmatiInstallArgs install_args;

    proto_bool ok;

    void (*const bind)(uint8_t *restrict work);
    void (*const install)(uint8_t *restrict work);
} UmatiNs;

/** @brief The one symbol this module exports. */
extern UmatiNs Umati;

/**
 * @brief The PROTOCORE_UMATI_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_umati_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_UMATI

#endif // PROTOCORE_UMATI_H
