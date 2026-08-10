// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file euromap77.h
 * @brief EUROMAP 77 / OPC 40077 - OPC UA for injection moulding machines (IMM <-> MES), the
 *        IMM_MES_Interface information model (PC_ENABLE_EUROMAP77).
 *
 * EUROMAP 77 (published as OPC 40077, namespace `http://www.euromap.org/euromap77/`) standardizes how an
 * injection molding machine (IMM) reports its identity, status, and the active job's live production
 * counters to a MES, so any OPC UA client reads the same structure across machine vendors. It builds on
 * EUROMAP 83 (OPC 40083, `http://www.euromap.org/euromap83/`), the shared plastics/rubber type + enum
 * library (MachineModeEnumeration, JobStatusEnumeration, ...).
 *
 * This module builds the IMM_MES_Interface address space on top of the OPC UA Binary server
 * (`services/opcua`, PC_ENABLE_OPCUA): it registers a Browse + Read resolver that answers for the
 * IMM_MES_Interface node hierarchy and serves live values out of a caller-owned @ref EmImm struct you
 * refresh in your loop. Same pattern as `services/umati` / `services/robotics`; no heap, no stdlib - the
 * model is a fixed node table, the values are pointers/scalars in your struct.
 *
 * Model exposed (BrowseNames per the EUROMAP 77 NodeSet), under the Objects folder:
 *
 *   IMM_MES_Interface
 *     MachineInformation  Manufacturer, Model, SerialNumber, ProductCode, HardwareRevision,
 *                         SoftwareRevision, DeviceRevision, ManufacturerUri
 *     MachineStatus       IsPresent, MachineMode
 *     Jobs
 *       ActiveJob         JobName, JobDescription, Material, ProductName, MouldId, ExpectedCycleTime,
 *                         NumCavities, NominalParts
 *       ActiveJobValues   JobCycleCounter, MachineCycleCounter, LastCycleTime, AverageCycleTime,
 *                         JobPartsCounter, JobGoodPartsCounter, JobBadPartsCounter, JobStatus
 *
 * The production counters are faithful UInt64 (EUROMAP 77 defines them 64-bit), served through the OPC UA
 * Variant's UInt64 encoding. The model is read-only (a monitoring model - the machine reports, the MES
 * observes). Scope note: one IMM with its active job is exposed (the common single-machine MES feed); the
 * companion-spec TypeDefinitions, methods, file/dataset transfer, and the multi-cardinality arrays
 * (InjectionUnits, Moulds, MachineConfiguration detail) are a documented follow-on - a generic OPC UA
 * client still browses the structure and reads every value by BrowseName today.
 *
 * Like umati / robotics, this installs the single OPC UA read + browse handler, so one companion model is
 * active per server build (EUROMAP 77, umati, and robotics are mutually exclusive per OPC UA endpoint).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date     2026
 */

#ifndef PROTOCORE_EUROMAP77_H
#define PROTOCORE_EUROMAP77_H

#include "protocore_config.h"

#if PC_ENABLE_EUROMAP77

#include "services/fieldbus/opcua/opcua.h" // OpcUaVariant / OpcUaReference / handler typedefs (shares the OPC UA codec)

/** @brief The EUROMAP 77 companion-spec namespace URI (OPC 40077). */
#define EUROMAP77_NS_URI "http://www.euromap.org/euromap77/"
/** @brief The EUROMAP 83 shared type/enum library namespace URI (OPC 40083). */
#define EUROMAP83_NS_URI "http://www.euromap.org/euromap83/"

/**
 * @brief Machine mode (EUROMAP 83 MachineModeEnumeration, em83 i=3011). Exposed as Int32; the numeric
 *        values are the companion-spec enumeration.
 */
typedef enum PROTO_ENUM_PACKED
{
    EM_MODE_OTHER = 0,          ///< a mode outside the ones below.
    EM_MODE_AUTOMATIC = 1,      ///< automatic production.
    EM_MODE_SEMI_AUTOMATIC = 2, ///< semi-automatic (operator-triggered cycles).
    EM_MODE_MANUAL = 3,         ///< manual / hand operation.
    EM_MODE_SETUP = 4,          ///< set-up / preparation.
    EM_MODE_SLEEP = 5,          ///< energy-saving sleep.
} EmMachineMode;

/**
 * @brief Active-job status (EUROMAP 83 JobStatusEnumeration, em83 i=3017). Exposed as Int32; the numeric
 *        values are the companion-spec enumeration.
 */
typedef enum PROTO_ENUM_PACKED
{
    EM_JOB_OTHER = 0,                  ///< a status outside the ones below.
    EM_JOB_TRANSFERRED_ASSIGNED = 1,   ///< transferred / assigned to the machine.
    EM_JOB_SET_UP_ACTIVE = 2,          ///< set-up in progress.
    EM_JOB_SET_UP_INTERRUPTED = 3,     ///< set-up interrupted.
    EM_JOB_SET_UP_FINISHED = 4,        ///< set-up finished.
    EM_JOB_START_UP_ACTIVE = 5,        ///< start-up in progress.
    EM_JOB_IN_PRODUCTION = 6,          ///< running production.
    EM_JOB_INTERRUPTED = 7,            ///< production interrupted.
    EM_JOB_FINISHED = 8,               ///< job finished.
    EM_JOB_TEAR_DOWN_ACTIVE = 9,       ///< tear-down in progress.
    EM_JOB_TEAR_DOWN_INTERRUPTED = 10, ///< tear-down interrupted.
    EM_JOB_TEAR_DOWN_FINISHED = 11,    ///< tear-down finished.
} EmJobStatus;

/** @brief IMM identity (EUROMAP 77 MachineInformation, common subset; all String). */
typedef struct
{
    const char *manufacturer;      ///< Manufacturer.
    const char *model;             ///< Model.
    const char *serial_number;     ///< SerialNumber.
    const char *product_code;      ///< ProductCode.
    const char *hardware_revision; ///< HardwareRevision.
    const char *software_revision; ///< SoftwareRevision.
    const char *device_revision;   ///< DeviceRevision.
    const char *manufacturer_uri;  ///< ManufacturerUri.
} EmMachineInformation;

/** @brief IMM live status (EUROMAP 77 MachineStatus, common subset). */
typedef struct
{
    proto_bool is_present;      ///< IsPresent.
    EmMachineMode machine_mode; ///< MachineMode.
} EmMachineStatus;

/** @brief The active job's static parameters (EUROMAP 77 Jobs.ActiveJob, common subset). */
typedef struct
{
    const char *job_name;        ///< JobName.
    const char *job_description; ///< JobDescription.
    const char *material;        ///< Material.
    const char *product_name;    ///< ProductName.
    const char *mould_id;        ///< MouldId.
    double expected_cycle_time;  ///< ExpectedCycleTime (Duration, seconds -> Double).
    uint32_t num_cavities;       ///< NumCavities.
    uint64_t nominal_parts;      ///< NominalParts.
} EmActiveJob;

/** @brief The active job's live production counters (EUROMAP 77 Jobs.ActiveJobValues; counters UInt64). */
typedef struct
{
    uint64_t job_cycle_counter;      ///< JobCycleCounter.
    uint64_t machine_cycle_counter;  ///< MachineCycleCounter.
    double last_cycle_time;          ///< LastCycleTime (Duration -> Double).
    double average_cycle_time;       ///< AverageCycleTime (Duration -> Double).
    uint64_t job_parts_counter;      ///< JobPartsCounter.
    uint64_t job_good_parts_counter; ///< JobGoodPartsCounter.
    uint64_t job_bad_parts_counter;  ///< JobBadPartsCounter.
    EmJobStatus job_status;          ///< JobStatus.
} EmActiveJobValues;

/**
 * @brief The whole IMM_MES_Interface the server exposes. Own it in your sketch and refresh its fields
 *        each loop from your machine I/O; the resolvers read straight out of it (no copy). String fields
 *        may be null (served as an empty String).
 */
typedef struct
{
    const char *name;                    ///< IMM_MES_Interface BrowseName / DisplayName.
    EmMachineInformation info;           ///< MachineInformation.
    EmMachineStatus status;              ///< MachineStatus.
    EmActiveJob active_job;              ///< Jobs.ActiveJob.
    EmActiveJobValues active_job_values; ///< Jobs.ActiveJobValues.
} EmImm;

/**
 * @brief Bind the IMM the resolvers serve. @p imm must outlive the server (own it statically). Refresh
 *        its fields any time; each Read returns the current values.
 */
void pc_em77_bind(const EmImm *imm);

/**
 * @brief Read resolver for the IMM_MES_Interface model (an @ref OpcUaReadHandler): fills @p out for an
 *        EUROMAP 77 node's Value attribute. Returns false for a node outside the model (the server
 *        answers BadNodeIdUnknown). Install with `pc_opcua_set_read_handler(pc_em77_read)`.
 */
proto_bool pc_em77_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out);

/**
 * @brief Browse resolver for the IMM_MES_Interface model (an @ref OpcUaBrowseHandler): writes the child
 *        references of an EUROMAP 77 node (and of the Objects folder, which organizes the
 *        IMM_MES_Interface) into @p out. Returns the count, or -1 for a node outside the model. Install
 *        with `pc_opcua_set_browse_handler(pc_em77_browse)`.
 */
int32_t pc_em77_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max);

/**
 * @brief Convenience: bind @p imm and register both resolvers on the OPC UA server in one call
 *        (`pc_opcua_set_read_handler` + `pc_opcua_set_browse_handler`). Call before `server.begin()`.
 */
void pc_em77_install(const EmImm *imm);

#endif // PC_ENABLE_EUROMAP77
#endif // PROTOCORE_EUROMAP77_H
