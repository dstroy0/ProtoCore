// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file umati.cpp
 * @brief umati (OPC UA for Machine Tools, OPC 40501-1) MachineTool model - resolver implementation.
 *
 * A fixed node table (no heap): each container node (MachineTool, Identification, Monitoring, its
 * sub-objects, Production, Notification) answers a Browse with its child ReferenceDescriptions, and
 * each leaf Variable answers a Read out of the bound UmatiMachineTool struct. The Objects folder
 * (ns0 i=85) organizes the MachineTool so a client discovers it from the root.
 */

#include "services/machine_tool/umati/umati.h"

#if PC_ENABLE_UMATI

#include <string.h> // strnlen (string.h is allowed; the no-stdlib rule is about stdlib.h/malloc)

// ---------------------------------------------------------------------------
// Node identifiers (namespace PC_UMATI_NS). Objects end in 0; their variables count up from it.
// ---------------------------------------------------------------------------
namespace
{
enum : uint32_t // NOSONAR(cpp:S3642): anonymous table of OPC-UA node ids used as bare uint32_t (arithmetic + wire
                // compares); enum class would force a cast at every use
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
    PROD_ACTIVEPROG = 5301,
    PROD_PARTCOUNT = 5302,

    NOTIFICATION = 5400,
    NOTIF_MESSAGE = 5401,
    NOTIF_SEVERITY = 5402,
};

// All MachineTool model state, owned by one instance (internal linkage): the bound machine-data
// pointer the resolvers read from. Null until pc_umati_bind(); a Read/Browse before binding is a clean
// miss (BadNodeIdUnknown), so the server never dereferences a null model.
struct UmatiCtx
{
    const UmatiMachineTool *mt;
};
UmatiCtx s_umati = {nullptr};

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
// reference is a forward HasComponent (containment) unless @p organizes (the Objects->MachineTool link).
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
    r->target_ns = PC_UMATI_NS;
    r->target_id = target_id;
    r->browse_name_ns = PC_UMATI_NS;
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
void pc_umati_bind(const UmatiMachineTool *mt)
{
    s_umati.mt = mt;
}

bool pc_umati_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out)
{
    const UmatiMachineTool *mt = s_umati.mt;
    if (!mt || ns != PC_UMATI_NS || attribute != OPCUA_ATTR_VALUE)
    {
        return false;
    }
    switch (id)
    {
    // Identification
    case ID_MANUFACTURER:
        set_str(out, mt->ident.manufacturer);
        return true;
    case ID_MODEL:
        set_str(out, mt->ident.model);
        return true;
    case ID_SERIAL:
        set_str(out, mt->ident.serial_number);
        return true;
    case ID_YEAR:
        set_u32(out, mt->ident.year_of_construction);
        return true;
    case ID_SWREV:
        set_str(out, mt->ident.software_revision);
        return true;
    case ID_PRODURI:
        set_str(out, mt->ident.product_instance_uri);
        return true;
    // Monitoring / MachineTool
    case MON_OPMODE:
        set_i32(out, (int32_t)mt->operation_mode);
        return true;
    case MON_POWERON:
        set_f64(out, mt->power_on_duration_s);
        return true;
    // Monitoring / Channel
    case CH_STATE:
        set_i32(out, (int32_t)mt->channel.state);
        return true;
    case CH_FEEDOVR:
        set_f64(out, mt->channel.feed_override);
        return true;
    case CH_RAPIDOVR:
        set_f64(out, mt->channel.rapid_override);
        return true;
    case CH_ACTIVEPROG:
        set_str(out, mt->channel.active_program);
        return true;
    // Monitoring / Spindle
    case SP_SPEED:
        set_f64(out, mt->spindle.rotation_speed);
        return true;
    case SP_OVERRIDE:
        set_f64(out, mt->spindle.override_value);
        return true;
    case SP_ROTATING:
        set_bool(out, mt->spindle.is_rotating);
        return true;
    // Monitoring / Axes
    case AX_X_POS:
        set_f64(out, mt->axis_x.actual_position);
        return true;
    case AX_Y_POS:
        set_f64(out, mt->axis_y.actual_position);
        return true;
    case AX_Z_POS:
        set_f64(out, mt->axis_z.actual_position);
        return true;
    // Production
    case PROD_ACTIVEPROG:
        set_str(out, mt->active_program);
        return true;
    case PROD_PARTCOUNT:
        set_u32(out, mt->produced_part_count);
        return true;
    // Notification
    case NOTIF_MESSAGE:
        set_str(out, mt->message_text);
        return true;
    case NOTIF_SEVERITY:
        set_u32(out, mt->message_severity);
        return true;
    default:
        return false;
    }
}

int32_t pc_umati_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    const UmatiMachineTool *mt = s_umati.mt;
    if (!mt)
    {
        return -1;
    }

    // The Objects folder (ns0 i=85) organizes the MachineTool so a client finds it from the root.
    if (ns == 0 && id == 85)
    {
        return add_ref(out, 0, max, MACHINETOOL, mt->name ? mt->name : "MachineTool", OPCUA_NODECLASS_OBJECT,
                       /*organizes=*/true);
    }

    if (ns != PC_UMATI_NS)
    {
        return -1;
    }

    int32_t n = 0;
    switch (id)
    {
    case MACHINETOOL:
        n = add_obj(out, n, max, IDENTIFICATION, "Identification");
        n = add_obj(out, n, max, MONITORING, "Monitoring");
        n = add_obj(out, n, max, PRODUCTION, "Production");
        n = add_obj(out, n, max, NOTIFICATION, "Notification");
        return n;
    case IDENTIFICATION:
        n = add_var(out, n, max, ID_MANUFACTURER, "Manufacturer");
        n = add_var(out, n, max, ID_MODEL, "Model");
        n = add_var(out, n, max, ID_SERIAL, "SerialNumber");
        n = add_var(out, n, max, ID_YEAR, "YearOfConstruction");
        n = add_var(out, n, max, ID_SWREV, "SoftwareRevision");
        n = add_var(out, n, max, ID_PRODURI, "ProductInstanceUri");
        return n;
    case MONITORING:
        n = add_obj(out, n, max, MON_MACHINE, "MachineTool");
        n = add_obj(out, n, max, MON_CHANNEL, "Channel");
        n = add_obj(out, n, max, MON_SPINDLE, "Spindle");
        n = add_obj(out, n, max, MON_AXIS_X, "Axis_X");
        n = add_obj(out, n, max, MON_AXIS_Y, "Axis_Y");
        n = add_obj(out, n, max, MON_AXIS_Z, "Axis_Z");
        return n;
    case MON_MACHINE:
        n = add_var(out, n, max, MON_OPMODE, "OperationMode");
        n = add_var(out, n, max, MON_POWERON, "PowerOnDuration");
        return n;
    case MON_CHANNEL:
        n = add_var(out, n, max, CH_STATE, "ChannelState");
        n = add_var(out, n, max, CH_FEEDOVR, "FeedOverride");
        n = add_var(out, n, max, CH_RAPIDOVR, "RapidOverride");
        n = add_var(out, n, max, CH_ACTIVEPROG, "ActiveProgram");
        return n;
    case MON_SPINDLE:
        n = add_var(out, n, max, SP_SPEED, "RotationSpeed");
        n = add_var(out, n, max, SP_OVERRIDE, "OverrideValue");
        n = add_var(out, n, max, SP_ROTATING, "IsRotating");
        return n;
    case MON_AXIS_X:
        return add_var(out, 0, max, AX_X_POS, "ActualPosition");
    case MON_AXIS_Y:
        return add_var(out, 0, max, AX_Y_POS, "ActualPosition");
    case MON_AXIS_Z:
        return add_var(out, 0, max, AX_Z_POS, "ActualPosition");
    case PRODUCTION:
        n = add_var(out, n, max, PROD_ACTIVEPROG, "ActiveProgram");
        n = add_var(out, n, max, PROD_PARTCOUNT, "ProducedPartCount");
        return n;
    case NOTIFICATION:
        n = add_var(out, n, max, NOTIF_MESSAGE, "ActiveMessage");
        n = add_var(out, n, max, NOTIF_SEVERITY, "Severity");
        return n;
    default:
        return -1; // a leaf Variable (no children) or an unknown node
    }
}

void pc_umati_install(const UmatiMachineTool *mt)
{
    pc_umati_bind(mt);
    pc_opcua_set_read_handler(pc_umati_read);
    pc_opcua_set_browse_handler(pc_umati_browse);
}

#endif // PC_ENABLE_UMATI
