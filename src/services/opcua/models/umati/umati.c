// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file umati.c
 * @brief umati (OPC UA for Machine Tools, OPC 40501-1) MachineTool model - resolver implementation.
 *
 * A fixed node table (no heap): each container node (MachineTool, Identification, Monitoring, its
 * sub-objects, Production, Notification) answers a Browse with its child ReferenceDescriptions, and
 * each leaf Variable answers a Read out of the bound UmatiMachineTool struct. The Objects folder
 * (ns0 i=85) organizes the MachineTool so a client discovers it from the root.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_UMATI

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protostr/protostr.h"
#include "services/opcua/models/umati/umati.h"
#include "services/opcua/opcua.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Node identifiers (namespace PROTOCORE_UMATI_NS). Objects end in 0; their variables count up from it.
// ---------------------------------------------------------------------------
static enum : uint32_t // NOSONAR(cpp:S3642): anonymous table of OPC-UA node ids used as bare uint32_t (arithmetic +
                       // wire compares); enum class would force a cast at every use
{
    MACHINETOOL = 5000,

    IDENTIFICATION = 5100,
    ID_MANUFACTURER = 5101,
    ID_MODEL = 5102,
    ID_SERIAL = 5103,
    ID_YEAR = 5104,
    ID_SWREV = 5105,
    ID_PRODURI = 5106,

    MONITORING = 5200,
    MON_MACHINE = 5210,
    MON_OPMODE = 5211,
    MON_POWERON = 5212,
    MON_CHANNEL = 5220,
    CH_STATE = 5221,
    CH_FEEDOVR = 5222,
    CH_RAPIDOVR = 5223,
    CH_ACTIVEPROG = 5224,
    MON_SPINDLE = 5230,
    SP_SPEED = 5231,
    SP_OVERRIDE = 5232,
    SP_ROTATING = 5233,
    MON_AXIS_X = 5240,
    AX_X_POS = 5241,
    MON_AXIS_Y = 5250,
    AX_Y_POS = 5251,
    MON_AXIS_Z = 5260,
    AX_Z_POS = 5261,

    PRODUCTION = 5300,
    PROD_ACTIVEPROG = 5301,      // an Object (ProductionActiveProgramType), not a value
    PROD_ACTIVEPROG_NAME = 5303, // its Name property, which is where the program string lives
    PROD_STATISTICS = 5310,      // ProductionStatisticsType, which owns the part counter
    PROD_PARTCOUNT = 5311,       // published as PartsProducedInLifetime

    NOTIFICATION = 5400,
    NOTIF_MESSAGE = 5401,
    NOTIF_SEVERITY = 5402,
};

// The ObjectTypes Opc.Ua.MachineTool.NodeSet2.xml publishes for the containers above, at their ids
// in the MachineTool namespace. ElementMonitoringType is what MonitoringType's <MonitoredElement>
// placeholder is typed, which is the nearest published base for an axis.
static enum : uint32_t // NOSONAR(cpp:S3642): as above, node ids used as bare uint32_t
{
    MT_NOTIFICATION_TYPE = 7,
    MT_IDENTIFICATION_TYPE = 11,
    MT_MONITORING_TYPE = 14,
    MT_CHANNEL_MONITORING_TYPE = 16,
    MT_PRODUCTION_STATISTICS_TYPE = 20,
    MT_PRODUCTION_TYPE = 21,
    MT_SPINDLE_MONITORING_TYPE = 22,
    MT_ELEMENT_MONITORING_TYPE = 23,
    MT_MACHINE_OPERATION_MONITORING_TYPE = 26,
    MT_PRODUCTION_ACTIVE_PROGRAM_TYPE = 32,
};

// All MachineTool model state, owned by one instance (internal linkage): the bound machine-data
// pointer the resolvers read from. Null until protocore_umati_bind(); a Read/Browse before binding is a clean
// miss (BadNodeIdUnknown), so the server never dereferences a null model.
typedef struct
{
    const UmatiMachineTool *mt;
    uint16_t ns; // the index the server gave UMATI_NS_URI
} UmatiCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define UMATI_OFF_CTX 0u
static_assert(UMATI_OFF_CTX + sizeof(UmatiCtx) <= PROTOCORE_UMATI_BORROW,
              "PROTOCORE_UMATI_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define UMATI_CTX(w) ((UmatiCtx *)(void *)((w) + UMATI_OFF_CTX))

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
// Append one ReferenceDescription (bounded by @p max). BrowseName + DisplayName share @p name; every
// reference is a forward HasComponent (containment) unless @p organizes (the Objects->MachineTool link).
static int32_t add_ref(const UmatiCtx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t target_id,
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
// A container MachineTool gives a type of its own, at that type's id in this model's namespace.
static int32_t add_typed_obj(const UmatiCtx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id,
                             const char *name, uint32_t type_id)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_OBJECT, OPCUA_REFTYPE_HAS_COMPONENT, c->ns, type_id);
}
// Identification is an add-in, not a component: Machinery and MachineTool both hang it off the
// machine by HasAddIn (Part 3), and a client walking HasComponent never reaches it.
static int32_t add_addin(const UmatiCtx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name,
                         uint32_t type_id)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_OBJECT, OPCUA_REFTYPE_HAS_ADDIN, c->ns, type_id);
}
// A nameplate leaf: MachineryItemIdentificationType reaches every one of its by HasProperty.
static int32_t add_prop(const UmatiCtx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_VARIABLE, OPCUA_REFTYPE_HAS_PROPERTY, 0,
                   OPCUA_TYPEDEF_PROPERTY);
}
static int32_t add_obj(const UmatiCtx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_OBJECT, OPCUA_REFTYPE_HAS_COMPONENT, 0,
                   OPCUA_TYPEDEF_BASE_OBJECT);
}
static int32_t add_var(const UmatiCtx *c, OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name)
{
    return add_ref(c, out, n, max, id, name, OPCUA_NODECLASS_VARIABLE, OPCUA_REFTYPE_HAS_COMPONENT, 0,
                   OPCUA_TYPEDEF_BASE_DATA_VARIABLE);
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
    uint8_t *span; ///< PROTOCORE_UMATI_BORROW persistent bytes
} UmatiOwnCtx;
static UmatiOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_umati_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_UMATI_BORROW).buf;
    }
    return s_own.span;
}

static void umati_bind(uint8_t *restrict work);

static void umati_bind(uint8_t *restrict work)
{
    const UmatiMachineTool *mt = Umati.bind_args.mt;

    UMATI_CTX(work)->mt = mt;
    // The NamespaceArray is the server's (Part 3 sec 8.2.2), so these nodes are served at whatever
    // index it gave this model's URI rather than at a number the model picked.
    UMATI_CTX(work)->ns = protocore_opcua_namespace_index(UMATI_NS_URI);
}

// OPC UA Part 4 sec 5.11.2: Read is a Server service over the Server's AddressSpace, so this is the
// server's OpcUaReadHandler contract rather than an entry of this module's own.
static proto_bool umati_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the accessor
    // rather than a parameter.
    uint8_t *restrict work = protocore_umati_span();
    const UmatiMachineTool *mt = UMATI_CTX(work)->mt;
    if (!mt || ns != UMATI_CTX(work)->ns || attribute != OPCUA_ATTR_VALUE)
    {
        return PROTO_FALSE;
    }
    switch (id)
    {
    // Identification
    case ID_MANUFACTURER:
        set_str(out, mt->ident.manufacturer);
        return PROTO_TRUE;
    case ID_MODEL:
        set_str(out, mt->ident.model);
        return PROTO_TRUE;
    case ID_SERIAL:
        set_str(out, mt->ident.serial_number);
        return PROTO_TRUE;
    case ID_YEAR:
        set_u32(out, mt->ident.year_of_construction);
        return PROTO_TRUE;
    case ID_SWREV:
        set_str(out, mt->ident.software_revision);
        return PROTO_TRUE;
    case ID_PRODURI:
        set_str(out, mt->ident.product_instance_uri);
        return PROTO_TRUE;
    // Monitoring / MachineTool
    case MON_OPMODE:
        set_i32(out, (int32_t)mt->operation_mode);
        return PROTO_TRUE;
    case MON_POWERON:
        set_f64(out, mt->power_on_duration_s);
        return PROTO_TRUE;
    // Monitoring / Channel
    case CH_STATE:
        set_i32(out, (int32_t)mt->channel.state);
        return PROTO_TRUE;
    case CH_FEEDOVR:
        set_f64(out, mt->channel.feed_override);
        return PROTO_TRUE;
    case CH_RAPIDOVR:
        set_f64(out, mt->channel.rapid_override);
        return PROTO_TRUE;
    case CH_ACTIVEPROG:
        set_str(out, mt->channel.active_program);
        return PROTO_TRUE;
    // Monitoring / Spindle
    case SP_SPEED:
        set_f64(out, mt->spindle.rotation_speed);
        return PROTO_TRUE;
    case SP_OVERRIDE:
        set_f64(out, mt->spindle.override_value);
        return PROTO_TRUE;
    case SP_ROTATING:
        set_bool(out, mt->spindle.is_rotating);
        return PROTO_TRUE;
    // Monitoring / Axes
    case AX_X_POS:
        set_f64(out, mt->axis_x.actual_position);
        return PROTO_TRUE;
    case AX_Y_POS:
        set_f64(out, mt->axis_y.actual_position);
        return PROTO_TRUE;
    case AX_Z_POS:
        set_f64(out, mt->axis_z.actual_position);
        return PROTO_TRUE;
    // Production. ActiveProgram is an Object, so it has no value of its own: the program string is
    // its Name property, which is where ProductionActiveProgramType puts it.
    case PROD_ACTIVEPROG_NAME:
        set_str(out, mt->active_program);
        return PROTO_TRUE;
    case PROD_PARTCOUNT:
        set_u32(out, mt->produced_part_count);
        return PROTO_TRUE;
    // Notification
    case NOTIF_MESSAGE:
        set_str(out, mt->message_text);
        return PROTO_TRUE;
    case NOTIF_SEVERITY:
        set_u32(out, mt->message_severity);
        return PROTO_TRUE;
    default:
        return PROTO_FALSE;
    }
}

// OPC UA Part 4 sec 5.9.2: Browse is a Server service that discovers a Node's References, so this is
// the server's OpcUaBrowseHandler contract rather than an entry of this module's own.
static int32_t umati_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the accessor
    // rather than a parameter.
    uint8_t *restrict work = protocore_umati_span();
    const UmatiCtx *c = UMATI_CTX(work);
    const UmatiMachineTool *mt = c->mt;
    if (!mt)
    {
        return -1;
    }

    // The Objects folder (ns0 i=85) organizes the MachineTool so a client finds it from the root.
    if (ns == 0 && id == 85)
    {
        return add_ref(c, out, 0, max, MACHINETOOL, mt->name ? mt->name : "MachineTool", OPCUA_NODECLASS_OBJECT,
                       OPCUA_REFTYPE_ORGANIZES, 0, OPCUA_TYPEDEF_BASE_OBJECT);
    }

    if (ns != UMATI_CTX(work)->ns)
    {
        return -1;
    }

    int32_t n = 0;
    switch (id)
    {
    case MACHINETOOL:
        n = add_addin(c, out, n, max, IDENTIFICATION, "Identification", MT_IDENTIFICATION_TYPE);
        n = add_typed_obj(c, out, n, max, MONITORING, "Monitoring", MT_MONITORING_TYPE);
        n = add_typed_obj(c, out, n, max, PRODUCTION, "Production", MT_PRODUCTION_TYPE);
        n = add_typed_obj(c, out, n, max, NOTIFICATION, "Notification", MT_NOTIFICATION_TYPE);
        return n;
    case IDENTIFICATION:
        n = add_prop(c, out, n, max, ID_MANUFACTURER, "Manufacturer");
        n = add_prop(c, out, n, max, ID_MODEL, "Model");
        n = add_prop(c, out, n, max, ID_SERIAL, "SerialNumber");
        n = add_prop(c, out, n, max, ID_YEAR, "YearOfConstruction");
        n = add_prop(c, out, n, max, ID_SWREV, "SoftwareRevision");
        n = add_prop(c, out, n, max, ID_PRODURI, "ProductInstanceUri");
        return n;
    case MONITORING:
        n = add_typed_obj(c, out, n, max, MON_MACHINE, "MachineTool", MT_MACHINE_OPERATION_MONITORING_TYPE);
        n = add_typed_obj(c, out, n, max, MON_CHANNEL, "Channel", MT_CHANNEL_MONITORING_TYPE);
        n = add_typed_obj(c, out, n, max, MON_SPINDLE, "Spindle", MT_SPINDLE_MONITORING_TYPE);
        n = add_typed_obj(c, out, n, max, MON_AXIS_X, "Axis_X", MT_ELEMENT_MONITORING_TYPE);
        n = add_typed_obj(c, out, n, max, MON_AXIS_Y, "Axis_Y", MT_ELEMENT_MONITORING_TYPE);
        n = add_typed_obj(c, out, n, max, MON_AXIS_Z, "Axis_Z", MT_ELEMENT_MONITORING_TYPE);
        return n;
    case MON_MACHINE:
        n = add_var(c, out, n, max, MON_OPMODE, "OperationMode");
        n = add_var(c, out, n, max, MON_POWERON, "PowerOnDuration");
        return n;
    case MON_CHANNEL:
        n = add_var(c, out, n, max, CH_STATE, "ChannelState");
        n = add_var(c, out, n, max, CH_FEEDOVR, "FeedOverride");
        n = add_var(c, out, n, max, CH_RAPIDOVR, "RapidOverride");
        n = add_var(c, out, n, max, CH_ACTIVEPROG, "ActiveProgram");
        return n;
    case MON_SPINDLE:
        n = add_var(c, out, n, max, SP_SPEED, "RotationSpeed");
        n = add_var(c, out, n, max, SP_OVERRIDE, "Override");
        n = add_var(c, out, n, max, SP_ROTATING, "IsRotating");
        return n;
    case MON_AXIS_X:
        return add_var(c, out, 0, max, AX_X_POS, "ActualPosition");
    case MON_AXIS_Y:
        return add_var(c, out, 0, max, AX_Y_POS, "ActualPosition");
    case MON_AXIS_Z:
        return add_var(c, out, 0, max, AX_Z_POS, "ActualPosition");
    case PRODUCTION:
        // ProductionType reaches an ActiveProgram OBJECT and a Statistics object; the program's
        // name and the part counter hang off those, not off Production itself.
        n = add_typed_obj(c, out, n, max, PROD_ACTIVEPROG, "ActiveProgram", MT_PRODUCTION_ACTIVE_PROGRAM_TYPE);
        n = add_typed_obj(c, out, n, max, PROD_STATISTICS, "Statistics", MT_PRODUCTION_STATISTICS_TYPE);
        return n;
    case PROD_ACTIVEPROG:
        return add_prop(c, out, 0, max, PROD_ACTIVEPROG_NAME, "Name");
    case PROD_STATISTICS:
        n = add_var(c, out, n, max, PROD_PARTCOUNT, "PartsProducedInLifetime");
        return n;
    case NOTIFICATION:
        n = add_var(c, out, n, max, NOTIF_MESSAGE, "ActiveMessage");
        n = add_var(c, out, n, max, NOTIF_SEVERITY, "Severity");
        return n;
    default:
        return -1; // a leaf Variable (no children) or an unknown node
    }
}

static void umati_install(uint8_t *restrict work)
{
    const UmatiMachineTool *mt = Umati.install_args.mt;

    Umati.bind_args.mt = mt;
    umati_bind(work);
    protocore_opcua_set_read_handler(umati_read);
    protocore_opcua_set_browse_handler(umati_browse);
}

UmatiNs Umati = {.bind = umati_bind, .install = umati_install};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_UMATI
