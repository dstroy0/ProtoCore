// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file euromap77.h
 * @brief EUROMAP 77 / OPC 40077 - OPC UA for injection moulding machines (IMM <-> MES), the
 *        IMM_MES_Interface information model (PROTOCORE_ENABLE_EUROMAP77).
 *
 * EUROMAP 77 (published as OPC 40077, ModelUri `http://opcfoundation.org/UA/PlasticsRubber/IMM2MES/`)
 * standardizes how an injection molding machine (IMM) reports its identity, status, and the active job's
 * live production counters to a MES, so any OPC UA client reads the same structure across machine vendors.
 * It builds on EUROMAP 83 (OPC 40083, ModelUri
 * `http://opcfoundation.org/UA/PlasticsRubber/GeneralTypes/`), the shared plastics/rubber type + enum
 * library (MachineModeEnumeration, JobStatusEnumeration, ...).
 *
 * This module builds the IMM_MES_Interface address space on top of the OPC UA Binary server
 * (`services/opcua`, PROTOCORE_ENABLE_OPCUA): it registers a Browse + Read resolver that answers for the
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_EUROMAP77

PROTOCORE_BEGIN_DECLS

// PROTOCORE_EUROMAP77_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief The IMM2MES namespace URI: the ModelUri Opc.Ua.PlasticsRubber.IMM2MES.NodeSet2.xml publishes. */
#define EUROMAP77_NS_URI "http://opcfoundation.org/UA/PlasticsRubber/IMM2MES/"

/** @brief The GeneralTypes namespace URI: the ModelUri Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.xml publishes. */
#define EUROMAP83_NS_URI "http://opcfoundation.org/UA/PlasticsRubber/GeneralTypes/"

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
/** @brief What bind takes: imm. */
typedef struct
{
    const EmImm *imm;
} Euromap77BindArgs;
/** @brief What install takes: imm. */
typedef struct
{
    const EmImm *imm;
} Euromap77InstallArgs;
/**
 * @brief EUROMAP 77 / OPC 40077 - OPC UA for injection moulding machines (IMM <-> MES), the IMM_MES_Interface
 * information model (PROTOCORE_ENABLE_EUROMAP77).
 *
 * A caller sets the members a call takes, invokes it through ::Euromap77 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Euromap77.bind_args.imm = ...;
 *   Euromap77.bind(work);
 *
 * @var Euromap77Ns::bind_args  what bind takes: imm
 * @var Euromap77Ns::install_args  what install takes: imm
 * @var Euromap77Ns::ok  a call's true/false outcome
 * @var Euromap77Ns::ns  the NamespaceIndex the server gave EUROMAP77_NS_URI, once a bind has run
 * @var Euromap77Ns::bind  bind the IMM the resolvers serve. imm must outlive the server (own ...
 * @var Euromap77Ns::install  bind imm and hand the model's Read and Browse resolvers to the OPC ...
 *
 * @c work is PROTOCORE_EUROMAP77_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    Euromap77BindArgs bind_args;
    Euromap77InstallArgs install_args;
    proto_bool ok;
    uint16_t ns;
} Euromap77Vars;

/** @brief The operands and the outcome. */
extern Euromap77Vars Euromap77V;

/** @brief The entries. */
typedef struct
{
    void (*const bind)(uint8_t *restrict work);
    void (*const install)(uint8_t *restrict work);
} Euromap77Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Euromap77V or a region of the borrow at a fixed offset.
void protocore_euromap77_bind(uint8_t *restrict work);
void protocore_euromap77_install(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Euromap77.bind(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Euromap77Ns Euromap77 __attribute__((unused)) = {
    .bind = protocore_euromap77_bind,
    .install = protocore_euromap77_install,
};

/**
 * @brief The PROTOCORE_EUROMAP77_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_euromap77_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EUROMAP77

#endif // PROTOCORE_EUROMAP77_H
