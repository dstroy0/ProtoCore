// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file euromap77.cpp
 * @brief EUROMAP 77 (OPC 40077) IMM_MES_Interface model - resolver implementation.
 *
 * A fixed node table (no heap): each container node (IMM_MES_Interface, MachineInformation,
 * MachineStatus, Jobs, ActiveJob, ActiveJobValues) answers a Browse with its child ReferenceDescriptions,
 * and each leaf Variable answers a Read out of the bound EmImm struct. The Objects folder (ns0 i=85)
 * organizes the IMM_MES_Interface so a client discovers it from the root. Production counters are served
 * as faithful UInt64 (EUROMAP 77 defines them 64-bit).
 */

#include "services/machine_tool/euromap77/euromap77.h"

#if PC_ENABLE_EUROMAP77

#include <string.h> // strnlen (string.h is allowed; the no-stdlib rule is about stdlib.h/malloc)

// ---------------------------------------------------------------------------
// Node identifiers (namespace PC_EM77_NS). Objects end in 0; their variables count up from it.
// ---------------------------------------------------------------------------
namespace
{
enum : uint32_t // NOSONAR(cpp:S3642): anonymous table of OPC-UA node ids used as bare uint32_t (arithmetic + wire
                // compares); enum class would force a cast at every use
{
    IMM_MES_INTERFACE = 7000,

    MACHINEINFORMATION = 7100,
    MI_MANUFACTURER = 7101,
    MI_MODEL = 7102,
    MI_SERIAL = 7103,
    MI_PRODUCTCODE = 7104,
    MI_HWREV = 7105,
    MI_SWREV = 7106,
    MI_DEVREV = 7107,
    MI_MANUFACTURERURI = 7108,

    MACHINESTATUS = 7200,
    MS_ISPRESENT = 7201,
    MS_MACHINEMODE = 7202,

    JOBS = 7300,
    ACTIVEJOB = 7310,
    AJ_JOBNAME = 7311,
    AJ_JOBDESC = 7312,
    AJ_MATERIAL = 7313,
    AJ_PRODUCTNAME = 7314,
    AJ_MOULDID = 7315,
    AJ_EXPECTEDCYCLE = 7316,
    AJ_NUMCAVITIES = 7317,
    AJ_NOMINALPARTS = 7318,
    ACTIVEJOBVALUES = 7320,
    AJV_JOBCYCLECOUNTER = 7321,
    AJV_MACHINECYCLECOUNTER = 7322,
    AJV_LASTCYCLETIME = 7323,
    AJV_AVERAGECYCLETIME = 7324,
    AJV_JOBPARTSCOUNTER = 7325,
    AJV_JOBGOODPARTSCOUNTER = 7326,
    AJV_JOBBADPARTSCOUNTER = 7327,
    AJV_JOBSTATUS = 7328,
};

// All EUROMAP 77 model state, owned by one instance (internal linkage): the bound IMM pointer the
// resolvers read from. Null until pc_em77_bind(); a Read/Browse before binding is a clean miss
// (BadNodeIdUnknown), so the server never dereferences a null model.
struct EuroMap77Ctx
{
    const EmImm *imm;
};
EuroMap77Ctx s_em77 = {nullptr};

// --- Variant fillers (leaf values) -----------------------------------------
void set_str(OpcUaVariant *o, const char *s)
{
    o->type = OpcUaVariantType::OPCUA_VAR_STRING;
    o->str = s ? s : "";
    o->str_len = (int32_t)strnlen(o->str, 0xFFFF); // bound the scan: a model string is a caller-owned C string
}
void set_u32(OpcUaVariant *o, uint32_t v)
{
    o->type = OpcUaVariantType::OPCUA_VAR_UINT32;
    o->u32 = v;
}
void set_u64(OpcUaVariant *o, uint64_t v)
{
    o->type = OpcUaVariantType::OPCUA_VAR_UINT64;
    o->u64 = v;
}
void set_i32(OpcUaVariant *o, int32_t v)
{
    o->type = OpcUaVariantType::OPCUA_VAR_INT32;
    o->i32 = v;
}
void set_f64(OpcUaVariant *o, double v)
{
    o->type = OpcUaVariantType::OPCUA_VAR_DOUBLE;
    o->f64 = v;
}
void set_bool(OpcUaVariant *o, bool v)
{
    o->type = OpcUaVariantType::OPCUA_VAR_BOOL;
    o->b = v;
}

// --- Browse helpers --------------------------------------------------------
// Append one ReferenceDescription (bounded by @p max). BrowseName + DisplayName share @p name; every
// reference is a forward HasComponent (containment) unless @p organizes (the Objects->IMM link).
int32_t add_ref(OpcUaReference *out, int32_t n, uint32_t max, uint32_t target_id, const char *name, uint32_t node_class,
                bool organizes = false)
{
    if ((uint32_t)n >= max)
    {
        return n;
    }
    OpcUaReference *r = &out[n];
    r->ref_type_id = organizes ? OPCUA_REFTYPE_ORGANIZES : OPCUA_REFTYPE_HAS_COMPONENT;
    r->is_forward = true;
    r->target_ns = PC_EM77_NS;
    r->target_id = target_id;
    r->browse_name_ns = PC_EM77_NS;
    r->browse_name = name;
    r->display_name = name;
    r->node_class = node_class;
    r->type_def_id =
        (node_class == OPCUA_NODECLASS_VARIABLE) ? OPCUA_TYPEDEF_BASE_DATA_VARIABLE : OPCUA_TYPEDEF_BASE_OBJECT;
    return n + 1;
}
int32_t add_obj(OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name)
{
    return add_ref(out, n, max, id, name, OPCUA_NODECLASS_OBJECT);
}
int32_t add_var(OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name)
{
    return add_ref(out, n, max, id, name, OPCUA_NODECLASS_VARIABLE);
}
} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void pc_em77_bind(const EmImm *imm)
{
    s_em77.imm = imm;
}

bool pc_em77_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out)
{
    const EmImm *imm = s_em77.imm;
    if (!imm || ns != PC_EM77_NS || attribute != OPCUA_ATTR_VALUE)
    {
        return false;
    }
    switch (id)
    {
    // MachineInformation
    case MI_MANUFACTURER:
        set_str(out, imm->info.manufacturer);
        return true;
    case MI_MODEL:
        set_str(out, imm->info.model);
        return true;
    case MI_SERIAL:
        set_str(out, imm->info.serial_number);
        return true;
    case MI_PRODUCTCODE:
        set_str(out, imm->info.product_code);
        return true;
    case MI_HWREV:
        set_str(out, imm->info.hardware_revision);
        return true;
    case MI_SWREV:
        set_str(out, imm->info.software_revision);
        return true;
    case MI_DEVREV:
        set_str(out, imm->info.device_revision);
        return true;
    case MI_MANUFACTURERURI:
        set_str(out, imm->info.manufacturer_uri);
        return true;
    // MachineStatus
    case MS_ISPRESENT:
        set_bool(out, imm->status.is_present);
        return true;
    case MS_MACHINEMODE:
        set_i32(out, (int32_t)imm->status.machine_mode);
        return true;
    // Jobs / ActiveJob
    case AJ_JOBNAME:
        set_str(out, imm->active_job.job_name);
        return true;
    case AJ_JOBDESC:
        set_str(out, imm->active_job.job_description);
        return true;
    case AJ_MATERIAL:
        set_str(out, imm->active_job.material);
        return true;
    case AJ_PRODUCTNAME:
        set_str(out, imm->active_job.product_name);
        return true;
    case AJ_MOULDID:
        set_str(out, imm->active_job.mould_id);
        return true;
    case AJ_EXPECTEDCYCLE:
        set_f64(out, imm->active_job.expected_cycle_time);
        return true;
    case AJ_NUMCAVITIES:
        set_u32(out, imm->active_job.num_cavities);
        return true;
    case AJ_NOMINALPARTS:
        set_u64(out, imm->active_job.nominal_parts);
        return true;
    // Jobs / ActiveJobValues
    case AJV_JOBCYCLECOUNTER:
        set_u64(out, imm->active_job_values.job_cycle_counter);
        return true;
    case AJV_MACHINECYCLECOUNTER:
        set_u64(out, imm->active_job_values.machine_cycle_counter);
        return true;
    case AJV_LASTCYCLETIME:
        set_f64(out, imm->active_job_values.last_cycle_time);
        return true;
    case AJV_AVERAGECYCLETIME:
        set_f64(out, imm->active_job_values.average_cycle_time);
        return true;
    case AJV_JOBPARTSCOUNTER:
        set_u64(out, imm->active_job_values.job_parts_counter);
        return true;
    case AJV_JOBGOODPARTSCOUNTER:
        set_u64(out, imm->active_job_values.job_good_parts_counter);
        return true;
    case AJV_JOBBADPARTSCOUNTER:
        set_u64(out, imm->active_job_values.job_bad_parts_counter);
        return true;
    case AJV_JOBSTATUS:
        set_i32(out, (int32_t)imm->active_job_values.job_status);
        return true;
    default:
        return false;
    }
}

int32_t pc_em77_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    const EmImm *imm = s_em77.imm;
    if (!imm)
    {
        return -1;
    }

    // The Objects folder (ns0 i=85) organizes the IMM_MES_Interface so a client finds it from the root.
    if (ns == 0 && id == 85)
    {
        return add_ref(out, 0, max, IMM_MES_INTERFACE, imm->name ? imm->name : "IMM_MES_Interface",
                       OPCUA_NODECLASS_OBJECT, /*organizes=*/true);
    }

    if (ns != PC_EM77_NS)
    {
        return -1;
    }

    int32_t n = 0;
    switch (id)
    {
    case IMM_MES_INTERFACE:
        n = add_obj(out, n, max, MACHINEINFORMATION, "MachineInformation");
        n = add_obj(out, n, max, MACHINESTATUS, "MachineStatus");
        n = add_obj(out, n, max, JOBS, "Jobs");
        return n;
    case MACHINEINFORMATION:
        n = add_var(out, n, max, MI_MANUFACTURER, "Manufacturer");
        n = add_var(out, n, max, MI_MODEL, "Model");
        n = add_var(out, n, max, MI_SERIAL, "SerialNumber");
        n = add_var(out, n, max, MI_PRODUCTCODE, "ProductCode");
        n = add_var(out, n, max, MI_HWREV, "HardwareRevision");
        n = add_var(out, n, max, MI_SWREV, "SoftwareRevision");
        n = add_var(out, n, max, MI_DEVREV, "DeviceRevision");
        n = add_var(out, n, max, MI_MANUFACTURERURI, "ManufacturerUri");
        return n;
    case MACHINESTATUS:
        n = add_var(out, n, max, MS_ISPRESENT, "IsPresent");
        n = add_var(out, n, max, MS_MACHINEMODE, "MachineMode");
        return n;
    case JOBS:
        n = add_obj(out, n, max, ACTIVEJOB, "ActiveJob");
        n = add_obj(out, n, max, ACTIVEJOBVALUES, "ActiveJobValues");
        return n;
    case ACTIVEJOB:
        n = add_var(out, n, max, AJ_JOBNAME, "JobName");
        n = add_var(out, n, max, AJ_JOBDESC, "JobDescription");
        n = add_var(out, n, max, AJ_MATERIAL, "Material");
        n = add_var(out, n, max, AJ_PRODUCTNAME, "ProductName");
        n = add_var(out, n, max, AJ_MOULDID, "MouldId");
        n = add_var(out, n, max, AJ_EXPECTEDCYCLE, "ExpectedCycleTime");
        n = add_var(out, n, max, AJ_NUMCAVITIES, "NumCavities");
        n = add_var(out, n, max, AJ_NOMINALPARTS, "NominalParts");
        return n;
    case ACTIVEJOBVALUES:
        n = add_var(out, n, max, AJV_JOBCYCLECOUNTER, "JobCycleCounter");
        n = add_var(out, n, max, AJV_MACHINECYCLECOUNTER, "MachineCycleCounter");
        n = add_var(out, n, max, AJV_LASTCYCLETIME, "LastCycleTime");
        n = add_var(out, n, max, AJV_AVERAGECYCLETIME, "AverageCycleTime");
        n = add_var(out, n, max, AJV_JOBPARTSCOUNTER, "JobPartsCounter");
        n = add_var(out, n, max, AJV_JOBGOODPARTSCOUNTER, "JobGoodPartsCounter");
        n = add_var(out, n, max, AJV_JOBBADPARTSCOUNTER, "JobBadPartsCounter");
        n = add_var(out, n, max, AJV_JOBSTATUS, "JobStatus");
        return n;
    default:
        return -1; // a leaf Variable (no children) or an unknown node
    }
}

void pc_em77_install(const EmImm *imm)
{
    pc_em77_bind(imm);
    pc_opcua_set_read_handler(pc_em77_read);
    pc_opcua_set_browse_handler(pc_em77_browse);
}

#endif // PC_ENABLE_EUROMAP77
