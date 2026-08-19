// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file euromap77.c
 * @brief EUROMAP 77 (OPC 40077) IMM_MES_Interface model - resolver implementation.
 *
 * A fixed node table (no heap): each container node (IMM_MES_Interface, MachineInformation,
 * MachineStatus, Jobs, ActiveJob, ActiveJobValues) answers a Browse with its child ReferenceDescriptions,
 * and each leaf Variable answers a Read out of the bound EmImm struct. The Objects folder (ns0 i=85)
 * organizes the IMM_MES_Interface so a client discovers it from the root. Production counters are served
 * as faithful UInt64 (EUROMAP 77 defines them 64-bit).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_EUROMAP77

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protostr/protostr.h"
#include "services/opcua/opcua.h"
#include "services/opcua/models/euromap77/euromap77.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Node identifiers (namespace PROTOCORE_EM77_NS). Objects end in 0; their variables count up from it.
// ---------------------------------------------------------------------------
enum : uint32_t // NOSONAR(cpp:S3642): anonymous table of OPC-UA node ids used as bare uint32_t (arithmetic +
                // wire compares); enum class would force a cast at every use
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

// The two ObjectTypes the GeneralTypes NodeSet publishes for the job objects, at their ids in that
// model's namespace (Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.xml).
enum : uint32_t // NOSONAR(cpp:S3642): as above, node ids used as bare uint32_t
{
    GT_CYCLIC_JOB_INFORMATION_TYPE = 1024,
    GT_ACTIVE_CYCLIC_JOB_VALUES_TYPE = 1031,
};

// All EUROMAP 77 model state, owned by one instance (internal linkage): the bound IMM pointer the
// resolvers read from. Null until protocore_em77_bind(); a Read/Browse before binding is a clean miss
// (BadNodeIdUnknown), so the server never dereferences a null model.
typedef struct
{
    const EmImm *imm;
    uint16_t ns;    // the index the server gave EUROMAP77_NS_URI: where this model's nodes live
    uint16_t ns_gt; // and the one it gave EUROMAP83_NS_URI, which owns the two job types
} EuroMap77Ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define EUROMAP77_OFF_CTX 0u
static_assert(EUROMAP77_OFF_CTX + sizeof(EuroMap77Ctx) <= PROTOCORE_EUROMAP77_BORROW,
              "PROTOCORE_EUROMAP77_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define EUROMAP77_CTX(w) ((EuroMap77Ctx *)(void *)((w) + EUROMAP77_OFF_CTX))

// --- Variant fillers (leaf values) -----------------------------------------
static void set_str(OpcUaVariant *o, const char *s)
{
    o->type = OPCUA_VAR_STRING;
    o->str = s ? s : "";
    o->str_len = (int32_t)str.len(o->str, 0xFFFF); // bound the scan: a model string is a caller-owned C string
}
static void set_u32(OpcUaVariant *o, uint32_t v)
{
    o->type = OPCUA_VAR_UINT32;
    o->u32 = v;
}
static void set_u64(OpcUaVariant *o, uint64_t v)
{
    o->type = OPCUA_VAR_UINT64;
    o->u64 = v;
}
static void set_i32(OpcUaVariant *o, int32_t v)
{
    o->type = OPCUA_VAR_INT32;
    o->i32 = v;
}
static void set_f64(OpcUaVariant *o, double v)
{
    o->type = OPCUA_VAR_DOUBLE;
    o->f64 = v;
}
static void set_bool(OpcUaVariant *o, proto_bool v)
{
    o->type = OPCUA_VAR_BOOL;
    o->b = v;
}

// --- Browse helpers --------------------------------------------------------
// Append one ReferenceDescription (bounded by @p max). BrowseName + DisplayName share @p name; @p ref_type
// and @p type_def are the pair the NodeSet publishes for the link, which is one of three: Organizes to
// BaseObjectType for the Objects->IMM link, HasComponent to BaseObjectType or BaseDataVariableType for a
// containment, HasProperty to PropertyType for a property.
static int32_t add_ref(const EuroMap77Ctx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t target_id,
                       const char *name, uint32_t node_class, uint32_t ref_type, uint16_t type_ns, uint32_t type_def)
{
    if ((uint32_t)n >= max)
    {
        return n;
    }
    OpcUaReference *r = &out[n];
    r->ref_type_id = ref_type;
    r->is_forward = PROTO_TRUE;
    r->target_ns = c->ns;
    r->target_id = target_id;
    r->browse_name_ns = c->ns;
    r->browse_name = name;
    r->display_name = name;
    r->node_class = node_class;
    r->type_def_ns = type_ns;
    r->type_def_id = type_def;
    return n + 1;
}
// A container the NodeSet gives no type of its own: BaseObjectType in the core namespace.
static int32_t add_obj(const EuroMap77Ctx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id,
                       const char *name)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_OBJECT, OPCUA_REFTYPE_HAS_COMPONENT, 0,
                   OPCUA_TYPEDEF_BASE_OBJECT);
}
// A container the GeneralTypes NodeSet types: the two job objects, whose types live in that model's
// namespace rather than the core one.
static int32_t add_gt_obj(const EuroMap77Ctx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id,
                          const char *name, uint32_t type_id)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_OBJECT, OPCUA_REFTYPE_HAS_COMPONENT, c->ns_gt, type_id);
}
// A leaf the NodeSet reaches by HasComponent: the seven ActiveJobValues counters and cycle times.
static int32_t add_var(const EuroMap77Ctx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id,
                       const char *name)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_VARIABLE, OPCUA_REFTYPE_HAS_COMPONENT, 0,
                   OPCUA_TYPEDEF_BASE_DATA_VARIABLE);
}
// A leaf the NodeSet reaches by HasProperty: the other nineteen.
static int32_t add_prop(const EuroMap77Ctx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id,
                        const char *name)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_VARIABLE, OPCUA_REFTYPE_HAS_PROPERTY, 0,
                   OPCUA_TYPEDEF_PROPERTY);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_EUROMAP77_BORROW persistent bytes, or null while the pool was short
} Euromap77OwnCtx;
static Euromap77OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_euromap77_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_EUROMAP77_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void euromap77_bind(uint8_t *restrict work);

static void euromap77_bind(uint8_t *restrict work)
{
    const EmImm *imm = Euromap77.bind_args.imm;

    Euromap77.ok = work != NULL;
    if (!Euromap77.ok)
    {
        return;
    }
    EuroMap77Ctx *c = EUROMAP77_CTX(work);
    c->imm = imm;
    // The NamespaceArray is the server's (Part 3 sec 8.2.2), so the indices these nodes are served
    // at are whatever it gave these two URIs rather than a number this model picked.
    c->ns = protocore_opcua_namespace_index(EUROMAP77_NS_URI);
    c->ns_gt = protocore_opcua_namespace_index(EUROMAP83_NS_URI);
    Euromap77.ns = c->ns;
}

// OPC UA Part 4 sec 5.11.2: Read is a Server service over the Server's AddressSpace, so this is the
// server's OpcUaReadHandler contract rather than an entry of this module's own.
static proto_bool em77_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_euromap77_span();

    const EuroMap77Ctx *c = EUROMAP77_CTX(work);
    const EmImm *imm = c->imm;
    if (!imm || ns != c->ns || attribute != OPCUA_ATTR_VALUE)
    {
        return PROTO_FALSE;
    }
    switch (id)
    {
    // MachineInformation
    case MI_MANUFACTURER:
        set_str(out, imm->info.manufacturer);
        return PROTO_TRUE;
    case MI_MODEL:
        set_str(out, imm->info.model);
        return PROTO_TRUE;
    case MI_SERIAL:
        set_str(out, imm->info.serial_number);
        return PROTO_TRUE;
    case MI_PRODUCTCODE:
        set_str(out, imm->info.product_code);
        return PROTO_TRUE;
    case MI_HWREV:
        set_str(out, imm->info.hardware_revision);
        return PROTO_TRUE;
    case MI_SWREV:
        set_str(out, imm->info.software_revision);
        return PROTO_TRUE;
    case MI_DEVREV:
        set_str(out, imm->info.device_revision);
        return PROTO_TRUE;
    case MI_MANUFACTURERURI:
        set_str(out, imm->info.manufacturer_uri);
        return PROTO_TRUE;
    // MachineStatus
    case MS_ISPRESENT:
        set_bool(out, imm->status.is_present);
        return PROTO_TRUE;
    case MS_MACHINEMODE:
        set_i32(out, (int32_t)imm->status.machine_mode);
        return PROTO_TRUE;
    // Jobs / ActiveJob
    case AJ_JOBNAME:
        set_str(out, imm->active_job.job_name);
        return PROTO_TRUE;
    case AJ_JOBDESC:
        set_str(out, imm->active_job.job_description);
        return PROTO_TRUE;
    case AJ_MATERIAL:
        set_str(out, imm->active_job.material);
        return PROTO_TRUE;
    case AJ_PRODUCTNAME:
        set_str(out, imm->active_job.product_name);
        return PROTO_TRUE;
    case AJ_MOULDID:
        set_str(out, imm->active_job.mould_id);
        return PROTO_TRUE;
    case AJ_EXPECTEDCYCLE:
        set_f64(out, imm->active_job.expected_cycle_time);
        return PROTO_TRUE;
    case AJ_NUMCAVITIES:
        set_u32(out, imm->active_job.num_cavities);
        return PROTO_TRUE;
    case AJ_NOMINALPARTS:
        set_u64(out, imm->active_job.nominal_parts);
        return PROTO_TRUE;
    // Jobs / ActiveJobValues
    case AJV_JOBCYCLECOUNTER:
        set_u64(out, imm->active_job_values.job_cycle_counter);
        return PROTO_TRUE;
    case AJV_MACHINECYCLECOUNTER:
        set_u64(out, imm->active_job_values.machine_cycle_counter);
        return PROTO_TRUE;
    case AJV_LASTCYCLETIME:
        set_f64(out, imm->active_job_values.last_cycle_time);
        return PROTO_TRUE;
    case AJV_AVERAGECYCLETIME:
        set_f64(out, imm->active_job_values.average_cycle_time);
        return PROTO_TRUE;
    case AJV_JOBPARTSCOUNTER:
        set_u64(out, imm->active_job_values.job_parts_counter);
        return PROTO_TRUE;
    case AJV_JOBGOODPARTSCOUNTER:
        set_u64(out, imm->active_job_values.job_good_parts_counter);
        return PROTO_TRUE;
    case AJV_JOBBADPARTSCOUNTER:
        set_u64(out, imm->active_job_values.job_bad_parts_counter);
        return PROTO_TRUE;
    case AJV_JOBSTATUS:
        set_i32(out, (int32_t)imm->active_job_values.job_status);
        return PROTO_TRUE;
    default:
        return PROTO_FALSE;
    }
}

// OPC UA Part 4 sec 5.9.2: Browse is a Server service that discovers a Node's References, so this is
// the server's OpcUaBrowseHandler contract rather than an entry of this module's own.
static int32_t em77_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_euromap77_span();

    const EuroMap77Ctx *c = EUROMAP77_CTX(work);
    const EmImm *imm = c->imm;
    if (!imm)
    {
        return -1;
    }

    // The Objects folder (ns0 i=85) organizes the IMM_MES_Interface so a client finds it from the root.
    if (ns == 0 && id == 85)
    {
        return add_ref(c, out, 0, max, IMM_MES_INTERFACE, imm->name ? imm->name : "IMM_MES_Interface",
                       OPCUA_NODECLASS_OBJECT, OPCUA_REFTYPE_ORGANIZES, 0, OPCUA_TYPEDEF_BASE_OBJECT);
    }

    if (ns != c->ns)
    {
        return -1;
    }

    int32_t n = 0;
    switch (id)
    {
    case IMM_MES_INTERFACE:
        n = add_obj(c, out, n, max, MACHINEINFORMATION, "MachineInformation");
        n = add_obj(c, out, n, max, MACHINESTATUS, "MachineStatus");
        n = add_obj(c, out, n, max, JOBS, "Jobs");
        return n;
    case MACHINEINFORMATION:
        n = add_prop(c, out, n, max, MI_MANUFACTURER, "Manufacturer");
        n = add_prop(c, out, n, max, MI_MODEL, "Model");
        n = add_prop(c, out, n, max, MI_SERIAL, "SerialNumber");
        n = add_prop(c, out, n, max, MI_PRODUCTCODE, "ProductCode");
        n = add_prop(c, out, n, max, MI_HWREV, "HardwareRevision");
        n = add_prop(c, out, n, max, MI_SWREV, "SoftwareRevision");
        n = add_prop(c, out, n, max, MI_DEVREV, "DeviceRevision");
        n = add_prop(c, out, n, max, MI_MANUFACTURERURI, "ManufacturerUri");
        return n;
    case MACHINESTATUS:
        n = add_prop(c, out, n, max, MS_ISPRESENT, "IsPresent");
        n = add_prop(c, out, n, max, MS_MACHINEMODE, "MachineMode");
        return n;
    case JOBS:
        n = add_gt_obj(c, out, n, max, ACTIVEJOB, "ActiveJob", GT_CYCLIC_JOB_INFORMATION_TYPE);
        n = add_gt_obj(c, out, n, max, ACTIVEJOBVALUES, "ActiveJobValues", GT_ACTIVE_CYCLIC_JOB_VALUES_TYPE);
        return n;
    case ACTIVEJOB:
        n = add_prop(c, out, n, max, AJ_JOBNAME, "JobName");
        n = add_prop(c, out, n, max, AJ_JOBDESC, "JobDescription");
        n = add_prop(c, out, n, max, AJ_MATERIAL, "Material");
        n = add_prop(c, out, n, max, AJ_PRODUCTNAME, "ProductName");
        n = add_prop(c, out, n, max, AJ_MOULDID, "MouldId");
        n = add_prop(c, out, n, max, AJ_EXPECTEDCYCLE, "ExpectedCycleTime");
        n = add_prop(c, out, n, max, AJ_NUMCAVITIES, "NumCavities");
        n = add_prop(c, out, n, max, AJ_NOMINALPARTS, "NominalParts");
        return n;
    case ACTIVEJOBVALUES:
        n = add_var(c, out, n, max, AJV_JOBCYCLECOUNTER, "JobCycleCounter");
        n = add_var(c, out, n, max, AJV_MACHINECYCLECOUNTER, "MachineCycleCounter");
        n = add_var(c, out, n, max, AJV_LASTCYCLETIME, "LastCycleTime");
        n = add_var(c, out, n, max, AJV_AVERAGECYCLETIME, "AverageCycleTime");
        n = add_var(c, out, n, max, AJV_JOBPARTSCOUNTER, "JobPartsCounter");
        n = add_var(c, out, n, max, AJV_JOBGOODPARTSCOUNTER, "JobGoodPartsCounter");
        n = add_var(c, out, n, max, AJV_JOBBADPARTSCOUNTER, "JobBadPartsCounter");
        n = add_prop(c, out, n, max, AJV_JOBSTATUS, "JobStatus");
        return n;
    default:
        return -1; // a leaf Variable (no children) or an unknown node
    }
}

static void euromap77_install(uint8_t *restrict work)
{
    const EmImm *imm = Euromap77.install_args.imm;

    Euromap77.bind_args.imm = imm;
    euromap77_bind(work);
    if (!Euromap77.ok)
    {
        return;
    }
    protocore_opcua_set_read_handler(em77_read);
    protocore_opcua_set_browse_handler(em77_browse);
}

Euromap77Ns Euromap77 = {.bind = euromap77_bind, .install = euromap77_install};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EUROMAP77
