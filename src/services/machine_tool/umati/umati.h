// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file umati.h
 * @brief umati - OPC UA for Machine Tools (OPC 40501-1) information model (PC_ENABLE_UMATI).
 *
 * umati ("universal machine technology interface") is the OPC UA companion specification for machine
 * tools (VDW / OPC Foundation, OPC 40501-1, namespace `http://opcfoundation.org/UA/MachineTool/`). It
 * standardizes how a machine tool exposes its identity and live state so any umati / OPC UA client (the
 * umati dashboard, UaExpert, python `asyncua`, ...) reads the same structure across vendors.
 *
 * This module builds the MachineTool address space on top of the OPC UA Binary server
 * (`services/opcua`, PC_ENABLE_OPCUA): it registers a Browse + Read resolver that answers for the
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

#include "protocore_config.h"

#if PC_ENABLE_UMATI

#include "services/fieldbus/opcua/opcua.h" // OpcUaVariant / OpcUaReference / handler typedefs (shares the OPC UA codec)
#include <stdint.h>

/** @brief The OPC UA for Machine Tools companion-spec namespace URI (OPC 40501-1). */
#define UMATI_NS_URI "http://opcfoundation.org/UA/MachineTool/"

/**
 * @brief MachineTool OperationMode (OPC 40501-1 MachineOperationMode). Exposed as Int32; the numeric
 *        values follow the companion-spec enumeration. The machine reports its current mode.
 */
enum class UmatiOperationMode : int32_t
{
    UMATI_OP_OTHER = 0,     ///< a mode outside the ones below.
    UMATI_OP_MANUAL = 1,    ///< hand / jog operation.
    UMATI_OP_MDA = 2,       ///< manual data automatic (single-block MDI).
    UMATI_OP_AUTOMATIC = 3, ///< running a stored program automatically.
    UMATI_OP_SETUP = 4,     ///< set-up / preparation.
};

/**
 * @brief Channel state (OPC 40501-1 ChannelState). Exposed as Int32; the numeric values follow the
 *        companion-spec enumeration.
 */
enum class UmatiChannelState : int32_t
{
    UMATI_CH_INTERRUPTED = 0, ///< program interrupted / feed hold.
    UMATI_CH_RESET = 1,       ///< channel reset (no program active).
    UMATI_CH_RUNNING = 2,     ///< a program is executing.
    UMATI_CH_WAITING = 3,     ///< waiting (e.g. for a synchronization / dwell).
};

/** @brief MachineTool identification (OPC 40501-1 Identification, subtype of Machinery MachineIdentification). */
struct UmatiIdentification
{
    const char *manufacturer;         ///< Manufacturer (LocalizedText -> String).
    const char *model;                ///< Model (LocalizedText -> String).
    const char *serial_number;        ///< SerialNumber (String).
    const char *software_revision;    ///< SoftwareRevision (String).
    const char *product_instance_uri; ///< ProductInstanceUri (String) - the unique instance URI.
    uint16_t year_of_construction;    ///< YearOfConstruction (UInt16, exposed as UInt32).
};

/** @brief One Channel's live monitoring values (OPC 40501-1 ChannelType, common subset). */
struct UmatiChannel
{
    UmatiChannelState state;    ///< ChannelState.
    double feed_override;       ///< FeedOverride (%).
    double rapid_override;      ///< RapidOverride (%).
    const char *active_program; ///< ActiveProgram.Name (the running NC program).
};

/** @brief One Spindle's live monitoring values (OPC 40501-1 SpindleType, common subset). */
struct UmatiSpindle
{
    double rotation_speed; ///< RotationSpeed (rpm).
    double override_value; ///< OverrideValue (%).
    bool is_rotating;      ///< IsRotating.
};

/** @brief One linear Axis' live monitoring value (OPC 40501-1 LinearAxisType, common subset). */
struct UmatiAxis
{
    double actual_position; ///< ActualPosition (mm).
};

/**
 * @brief The whole MachineTool the server exposes. Own it in your sketch and refresh its fields each
 *        loop from your machine I/O; the umati resolvers read straight out of it (no copy). String
 *        fields may be null (served as an empty String).
 */
struct UmatiMachineTool
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
};

/**
 * @brief Bind the MachineTool the resolvers serve. @p mt must outlive the server (own it statically).
 *        Refresh its fields any time; each Read returns the current values.
 */
void pc_umati_bind(const UmatiMachineTool *mt);

/**
 * @brief Read resolver for the MachineTool model (an @ref OpcUaReadHandler): fills @p out for a umati
 *        node's Value attribute. Returns false for a node outside the model (the server answers
 *        BadNodeIdUnknown). Install with `pc_opcua_set_read_handler(pc_umati_read)`.
 */
bool pc_umati_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out);

/**
 * @brief Browse resolver for the MachineTool model (an @ref OpcUaBrowseHandler): writes the child
 *        references of a umati node (and of the Objects folder, which organizes the MachineTool) into
 *        @p out. Returns the count, or -1 for a node outside the model. Install with
 *        `pc_opcua_set_browse_handler(pc_umati_browse)`.
 */
int32_t pc_umati_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max);

/**
 * @brief Convenience: bind @p mt and register both resolvers on the OPC UA server in one call
 *        (`pc_opcua_set_read_handler` + `pc_opcua_set_browse_handler`). Call before `server.begin()`.
 */
void pc_umati_install(const UmatiMachineTool *mt);

#endif // PC_ENABLE_UMATI
#endif // PROTOCORE_UMATI_H
