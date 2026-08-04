// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file robotics.c
 * @brief OPC UA for Robotics (OPC 40010-1) MotionDeviceSystem model - resolver implementation.
 *
 * A fixed node table (no heap): each container node (MotionDeviceSystem, the MotionDevices / Controllers
 * / SafetyStates folders, MotionDevice, ParameterSet, Axes, each Axis_k, Controller, Software,
 * SafetyState) answers a Browse with its child ReferenceDescriptions, and each leaf Variable answers a
 * Read out of the bound RoboticsMotionDeviceSystem struct. The Objects folder (ns0 i=85) organizes the
 * MotionDeviceSystem so a client discovers it from the root. Axes are parametric: Axis_k lives at
 * AXIS_BASE + k*10 (k = 1..axis_count) and its four variables at +1..+4.
 */

#include "services/machine_tool/robotics/robotics.h"

#if PC_ENABLE_ROBOTICS

#include <string.h> // strnlen (string.h is allowed; the no-stdlib rule is about stdlib.h/malloc)

// ---------------------------------------------------------------------------
// Node identifiers (namespace PC_ROBOTICS_NS). Objects end in 0; their variables count up from it.
// ---------------------------------------------------------------------------
static enum : uint32_t // NOSONAR(cpp:S3642): anonymous table of OPC-UA node ids used as bare uint32_t (arithmetic +
                       // wire compares); enum class would force a cast at every use
{
    MOTIONDEVICESYSTEM = 6000,

    MOTIONDEVICES = 6100, // Folder

    MOTIONDEVICE = 6200,
    MD_MANUFACTURER = 6201,
    MD_MODEL = 6202,
    MD_PRODUCTCODE = 6203,
    MD_SERIAL = 6204,
    MD_CATEGORY = 6205,
    MD_PARAMSET = 6210,
    MDP_ONPATH = 6211,
    MDP_INCONTROL = 6212,
    MDP_SPEEDOVERRIDE = 6213,
    MD_AXES = 6300, // Folder

    // Axes: Axis_k object = AXIS_BASE + k*10 (k = 1..axis_count); its vars at +1..+4.
    AXIS_BASE = 6400,

    CONTROLLERS = 6600, // Folder
    CONTROLLER = 6610,
    CT_MANUFACTURER = 6611,
    CT_MODEL = 6612,
    CT_PRODUCTCODE = 6613,
    CT_SERIAL = 6614,
    CT_SOFTWARE = 6620,
    SW_MANUFACTURER = 6621,
    SW_MODEL = 6622,
    SW_REVISION = 6623,

    SAFETYSTATES = 6700, // Folder
    SAFETYSTATE = 6710,
    SS_PARAMSET = 6720,
    SSP_OPMODE = 6721,
    SSP_ESTOP = 6722,
    SSP_PSTOP = 6723,
};

// Axis variable sub-ids (offset from the Axis_k object id).
static enum : uint32_t // NOSONAR(cpp:S3642): anonymous table of OPC-UA sub-ids used as bare uint32_t offsets; enum
                       // class would force a cast at every use
{
    AXVAR_POSITION = 1,
    AXVAR_SPEED = 2,
    AXVAR_ACCEL = 3,
    AXVAR_PROFILE = 4,
};

// All robotics model state, owned by one instance (internal linkage): the bound system pointer the
// resolvers read from, plus the per-axis BrowseName strings (filled at bind, referenced by Browse).
// Null until pc_robotics_bind(); a Read/Browse before binding is a clean miss (BadNodeIdUnknown), so
// the server never dereferences a null model.
typedef struct
{
    const RoboticsMotionDeviceSystem *mds;
    char axis_name[PC_ROBOTICS_AXES][12]; // "Axis_" + up to 2 digits + null
} RoboticsCtx;
static RoboticsCtx s_robotics = {NULL, {{0}}};

// Fill "Axis_k" (k = 1..PC_ROBOTICS_AXES) into the owned name table (no snprintf: a tiny fixed render).
static void build_axis_names(RoboticsCtx *c)
{
    for (uint32_t k = 0; k < PC_ROBOTICS_AXES; k++)
    {
        char *b = c->axis_name[k];
        uint32_t idx = k + 1; // 1-based BrowseName index
        b[0] = 'A';
        b[1] = 'x';
        b[2] = 'i';
        b[3] = 's';
        b[4] = '_';
        size_t o = 5;
        if (idx >= 10)
        {
            b[o++] = (char)('0' + (idx / 10));
        }
        b[o++] = (char)('0' + (idx % 10));
        b[o] = '\0';
    }
}

// Decode an axis node id: ids AXIS_BASE + k*10 + sub, k = 1.., sub = 0 (the Axis object) / 1..4 (a var).
// Returns true (with k 1-based + sub) only when k is within the bound axis_count; sub is the caller's to
// range-check (0 for a Browse of the Axis object, 1..4 for a Read of an axis variable).
static proto_bool decode_axis(uint32_t id, uint32_t axis_count, uint32_t *k_out, uint32_t *sub_out)
{
    if (id <= AXIS_BASE)
    {
        return PROTO_FALSE;
    }
    uint32_t rel = id - AXIS_BASE;
    uint32_t k = rel / 10;
    uint32_t sub = rel % 10;
    if (k < 1 || k > axis_count)
    {
        return PROTO_FALSE;
    }
    *k_out = k;
    *sub_out = sub;
    return PROTO_TRUE;
}

// --- Variant fillers (leaf values) -----------------------------------------
static void set_str(OpcUaVariant *o, const char *s)
{
    o->type = OPCUA_VAR_STRING;
    o->str = s ? s : "";
    o->str_len = (int32_t)strnlen(o->str, 0xFFFF); // bound the scan: a model string is a caller-owned C string
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
// Append one ReferenceDescription (bounded by @p max). BrowseName + DisplayName share @p name. A folder
// points at its members with Organizes (35); an object points at its component objects/variables with
// HasComponent (47) - the umati simplification (see robotics.h scope note).
static int32_t add_ref(OpcUaReference *out, int32_t n, uint32_t max, uint32_t target_id, const char *name,
                       uint32_t node_class, proto_bool organizes)
{
    if ((uint32_t)n >= max)
    {
        return n;
    }
    OpcUaReference *r = &out[n];
    r->ref_type_id = organizes ? OPCUA_REFTYPE_ORGANIZES : OPCUA_REFTYPE_HAS_COMPONENT;
    r->is_forward = PROTO_TRUE;
    r->target_ns = PC_ROBOTICS_NS;
    r->target_id = target_id;
    r->browse_name_ns = PC_ROBOTICS_NS;
    r->browse_name = name;
    r->display_name = name;
    r->node_class = node_class;
    r->type_def_id =
        (node_class == OPCUA_NODECLASS_VARIABLE) ? OPCUA_TYPEDEF_BASE_DATA_VARIABLE : OPCUA_TYPEDEF_BASE_OBJECT;
    return n + 1;
}
static int32_t add_obj(OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name)
{
    return add_ref(out, n, max, id, name, OPCUA_NODECLASS_OBJECT, /*organizes=*/PROTO_FALSE);
}
static int32_t add_folder_member(OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name)
{
    return add_ref(out, n, max, id, name, OPCUA_NODECLASS_OBJECT, /*organizes=*/PROTO_TRUE);
}
static int32_t add_var(OpcUaReference *out, int32_t n, uint32_t max, uint32_t id, const char *name)
{
    return add_ref(out, n, max, id, name, OPCUA_NODECLASS_VARIABLE, /*organizes=*/PROTO_FALSE);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void pc_robotics_bind(const RoboticsMotionDeviceSystem *mds)
{
    s_robotics.mds = mds;
    build_axis_names(&s_robotics);
}

proto_bool pc_robotics_read(uint16_t ns, uint32_t id, uint32_t attribute, OpcUaVariant *out)
{
    const RoboticsMotionDeviceSystem *mds = s_robotics.mds;
    if (!mds || ns != PC_ROBOTICS_NS || attribute != OPCUA_ATTR_VALUE)
    {
        return PROTO_FALSE;
    }

    // Axis variables (parametric): AXIS_BASE + k*10 + {1..4}.
    uint32_t k = 0;
    uint32_t sub = 0;
    if (decode_axis(id, mds->device.axis_count, &k, &sub) && sub >= AXVAR_POSITION && sub <= AXVAR_PROFILE)
    {
        const RoboticsAxis *ax = &mds->device.axes[k - 1];
        // The guard above already pins sub to AXVAR_POSITION..AXVAR_PROFILE, so the default edge is
        // unreachable; gcov cannot drop a single switch edge, so the dispatch line is excluded whole.
        switch (sub)
        {
        case AXVAR_POSITION:
            set_f64(out, ax->actual_position);
            return PROTO_TRUE;
        case AXVAR_SPEED:
            set_f64(out, ax->actual_speed);
            return PROTO_TRUE;
        case AXVAR_ACCEL:
            set_f64(out, ax->actual_acceleration);
            return PROTO_TRUE;
        case AXVAR_PROFILE:
            set_i32(out, (int32_t)ax->motion_profile);
            return PROTO_TRUE;
        default:
            return PROTO_FALSE;
        }
    }

    switch (id)
    {
    // MotionDevice identity
    case MD_MANUFACTURER:
        set_str(out, mds->device.manufacturer);
        return PROTO_TRUE;
    case MD_MODEL:
        set_str(out, mds->device.model);
        return PROTO_TRUE;
    case MD_PRODUCTCODE:
        set_str(out, mds->device.product_code);
        return PROTO_TRUE;
    case MD_SERIAL:
        set_str(out, mds->device.serial_number);
        return PROTO_TRUE;
    case MD_CATEGORY:
        set_i32(out, (int32_t)mds->device.category);
        return PROTO_TRUE;
    // MotionDevice ParameterSet
    case MDP_ONPATH:
        set_bool(out, mds->device.on_path);
        return PROTO_TRUE;
    case MDP_INCONTROL:
        set_bool(out, mds->device.in_control);
        return PROTO_TRUE;
    case MDP_SPEEDOVERRIDE:
        set_f64(out, mds->device.speed_override);
        return PROTO_TRUE;
    // Controller identity + Software
    case CT_MANUFACTURER:
        set_str(out, mds->controller.manufacturer);
        return PROTO_TRUE;
    case CT_MODEL:
        set_str(out, mds->controller.model);
        return PROTO_TRUE;
    case CT_PRODUCTCODE:
        set_str(out, mds->controller.product_code);
        return PROTO_TRUE;
    case CT_SERIAL:
        set_str(out, mds->controller.serial_number);
        return PROTO_TRUE;
    case SW_MANUFACTURER:
        set_str(out, mds->controller.sw_manufacturer);
        return PROTO_TRUE;
    case SW_MODEL:
        set_str(out, mds->controller.sw_model);
        return PROTO_TRUE;
    case SW_REVISION:
        set_str(out, mds->controller.sw_revision);
        return PROTO_TRUE;
    // SafetyState ParameterSet
    case SSP_OPMODE:
        set_i32(out, (int32_t)mds->safety.operational_mode);
        return PROTO_TRUE;
    case SSP_ESTOP:
        set_bool(out, mds->safety.emergency_stop);
        return PROTO_TRUE;
    case SSP_PSTOP:
        set_bool(out, mds->safety.protective_stop);
        return PROTO_TRUE;
    default:
        return PROTO_FALSE;
    }
}

int32_t pc_robotics_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    const RoboticsMotionDeviceSystem *mds = s_robotics.mds;
    if (!mds)
    {
        return -1;
    }

    // The Objects folder (ns0 i=85) organizes the MotionDeviceSystem so a client finds it from the root.
    if (ns == 0 && id == 85)
    {
        return add_folder_member(out, 0, max, MOTIONDEVICESYSTEM, mds->name ? mds->name : "MotionDeviceSystem");
    }

    if (ns != PC_ROBOTICS_NS)
    {
        return -1;
    }

    // An Axis_k object: browse its four variables (before the switch, since ids are parametric).
    uint32_t k = 0;
    uint32_t sub = 0;
    if (decode_axis(id, mds->device.axis_count, &k, &sub) && sub == 0)
    {
        uint32_t base = AXIS_BASE + k * 10;
        int32_t n = 0;
        n = add_var(out, n, max, base + AXVAR_POSITION, "ActualPosition");
        n = add_var(out, n, max, base + AXVAR_SPEED, "ActualSpeed");
        n = add_var(out, n, max, base + AXVAR_ACCEL, "ActualAcceleration");
        n = add_var(out, n, max, base + AXVAR_PROFILE, "MotionProfile");
        return n;
    }

    int32_t n = 0;
    switch (id)
    {
    case MOTIONDEVICESYSTEM:
        n = add_obj(out, n, max, MOTIONDEVICES, "MotionDevices");
        n = add_obj(out, n, max, CONTROLLERS, "Controllers");
        n = add_obj(out, n, max, SAFETYSTATES, "SafetyStates");
        return n;
    case MOTIONDEVICES:
        return add_folder_member(out, 0, max, MOTIONDEVICE, "MotionDevice");
    case MOTIONDEVICE:
        n = add_var(out, n, max, MD_MANUFACTURER, "Manufacturer");
        n = add_var(out, n, max, MD_MODEL, "Model");
        n = add_var(out, n, max, MD_PRODUCTCODE, "ProductCode");
        n = add_var(out, n, max, MD_SERIAL, "SerialNumber");
        n = add_var(out, n, max, MD_CATEGORY, "MotionDeviceCategory");
        n = add_obj(out, n, max, MD_PARAMSET, "ParameterSet");
        n = add_obj(out, n, max, MD_AXES, "Axes");
        return n;
    case MD_PARAMSET:
        n = add_var(out, n, max, MDP_ONPATH, "OnPath");
        n = add_var(out, n, max, MDP_INCONTROL, "InControl");
        n = add_var(out, n, max, MDP_SPEEDOVERRIDE, "SpeedOverride");
        return n;
    case MD_AXES:
        for (uint32_t a = 1; a <= mds->device.axis_count && a <= PC_ROBOTICS_AXES; a++)
        {
            n = add_folder_member(out, n, max, AXIS_BASE + a * 10, s_robotics.axis_name[a - 1]);
        }
        return n;
    case CONTROLLERS:
        return add_folder_member(out, 0, max, CONTROLLER, "Controller");
    case CONTROLLER:
        n = add_var(out, n, max, CT_MANUFACTURER, "Manufacturer");
        n = add_var(out, n, max, CT_MODEL, "Model");
        n = add_var(out, n, max, CT_PRODUCTCODE, "ProductCode");
        n = add_var(out, n, max, CT_SERIAL, "SerialNumber");
        n = add_obj(out, n, max, CT_SOFTWARE, "Software");
        return n;
    case CT_SOFTWARE:
        n = add_var(out, n, max, SW_MANUFACTURER, "Manufacturer");
        n = add_var(out, n, max, SW_MODEL, "Model");
        n = add_var(out, n, max, SW_REVISION, "SoftwareRevision");
        return n;
    case SAFETYSTATES:
        return add_folder_member(out, 0, max, SAFETYSTATE, "SafetyState");
    case SAFETYSTATE:
        return add_obj(out, 0, max, SS_PARAMSET, "ParameterSet");
    case SS_PARAMSET:
        n = add_var(out, n, max, SSP_OPMODE, "OperationalMode");
        n = add_var(out, n, max, SSP_ESTOP, "EmergencyStop");
        n = add_var(out, n, max, SSP_PSTOP, "ProtectiveStop");
        return n;
    default:
        return -1; // a leaf Variable (no children) or an unknown node
    }
}

void pc_robotics_install(const RoboticsMotionDeviceSystem *mds)
{
    pc_robotics_bind(mds);
    pc_opcua_set_read_handler(pc_robotics_read);
    pc_opcua_set_browse_handler(pc_robotics_browse);
}

#endif // PC_ENABLE_ROBOTICS
