// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OPC UA for Robotics model (services/opcua/models/robotics/robotics.h).
//
// Two published sources anchor this file. The OPC UA base NodeIds (OPC 10000-6 Annex A, the
// Opc.Ua.NodeIds.csv the OPC Foundation publishes) fix ObjectsFolder = 85, Organizes = 35,
// HasComponent = 47, BaseObjectType = 58, BaseDataVariableType = 63, NodeClass Object = 1 /
// Variable = 2 and the Value attribute id = 13. The companion spec OPC 40010-1 fixes the
// BrowseNames, published in the OPC Foundation's Opc.Ua.Robotics.NodeIds.csv: MotionDeviceSystemType
// carries MotionDevices, Controllers and SafetyStates; MotionDeviceType carries MotionDeviceCategory,
// ProductCode, SerialNumber, Axes and a ParameterSet with OnPath, InControl and SpeedOverride; an
// axis carries ActualPosition, ActualSpeed, ActualAcceleration and MotionProfile.
//
// test_axis_variables_read_their_own_axis is the load-bearing case. Every other node id in this
// model is a constant; the axis ids are computed (AXIS_BASE + k*10 + sub), so an arithmetic slip
// there does not fail loudly - it silently reports axis 2's position as axis 3's, which is a robot
// telling a monitoring client the wrong joint is where it is.
//
// The MotionDeviceCategory / MotionProfile / OperationalMode numeric enumerations of OPC 40010-1
// were NOT obtainable (the document is behind registration and the published NodeIds file names the
// enums without their values), so those are covered as a round-trip property rather than as
// published numbers.

// The .c, so the Read and Browse resolvers are reachable: they answer the OPC UA server's
// handler contract (Part 4 sec 5.9.2 / sec 5.11.2), so they are internal to the module and the
// env does not build this .c itself.
#include "services/opcua/models/robotics/robotics.c"
#include "services/opcua/opcua.h"
#include <string.h>

#include <unity.h>

#define REF_MAX 16

// The index the server gave this model, which is what its nodes are served at.
static uint16_t model_ns(void)
{
    return protocore_opcua_namespace_index(ROBOTICS_NS_URI);
}

static RoboticsMotionDeviceSystem g_mds;
static OpcUaReference g_ref[REF_MAX];

void setUp(void)
{
    memset(&g_mds, 0, sizeof(g_mds));
    memset(g_ref, 0, sizeof(g_ref));
    g_mds.name = "Cell-1";
    g_mds.device.manufacturer = "Acme Robotics";
    g_mds.device.model = "AR-6";
    g_mds.device.product_code = "PC-6";
    g_mds.device.serial_number = "SN-6";
    g_mds.device.category = ROBOTICS_CAT_ARTICULATED_ROBOT;
    g_mds.device.on_path = PROTO_TRUE;
    g_mds.device.in_control = PROTO_TRUE;
    g_mds.device.speed_override = 75.0;
    g_mds.device.axis_count = 3;
    for (uint32_t k = 0; k < 3; k++)
    {
        g_mds.device.axes[k].actual_position = 10.0 + (double)k;
        g_mds.device.axes[k].actual_speed = 100.0 + (double)k;
        g_mds.device.axes[k].actual_acceleration = 1000.0 + (double)k;
        g_mds.device.axes[k].motion_profile = ROBOTICS_PROFILE_ROTARY;
    }
    g_mds.controller.manufacturer = "Acme Control";
    g_mds.controller.model = "CR-1";
    g_mds.controller.product_code = "CPC-1";
    g_mds.controller.serial_number = "CSN-1";
    g_mds.controller.sw_manufacturer = "Acme Software";
    g_mds.controller.sw_model = "RobotOS";
    g_mds.controller.sw_revision = "3.4.5";
    g_mds.safety.operational_mode = ROBOTICS_MODE_AUTOMATIC;
    g_mds.safety.emergency_stop = PROTO_FALSE;
    g_mds.safety.protective_stop = PROTO_TRUE;
    RoboticsV.bind_args.mds = &g_mds;
    Robotics.bind(protocore_robotics_span());
}

void tearDown(void)
{
    RoboticsV.bind_args.mds = NULL;
    Robotics.bind(protocore_robotics_span());
}

static int32_t browse(uint32_t id, int32_t want)
{
    int32_t n = robotics_browse(model_ns(), id, g_ref, REF_MAX);
    TEST_ASSERT_EQUAL_INT32(want, n);
    return n;
}

// Unity's double assertions are off in this build; every value here is exactly representable in
// float, so the comparison is exact.
static void assert_f64(double want, double got)
{
    TEST_ASSERT_EQUAL_FLOAT((float)want, (float)got);
}

static OpcUaVariant read_ok(uint32_t id)
{
    OpcUaVariant v;
    memset(&v, 0, sizeof(v));
    TEST_ASSERT_TRUE(robotics_read(model_ns(), id, OPCUA_ATTR_VALUE, &v));
    return v;
}

// The MotionDeviceSystem node id, found the way a client finds it.
static uint32_t root(void)
{
    TEST_ASSERT_EQUAL_INT32(1, robotics_browse(0, 85, g_ref, REF_MAX));
    return g_ref[0].target_id;
}

// ns0 i=85 is the Objects folder; it Organizes (35) the MotionDeviceSystem.
void test_objects_folder_organizes_the_motion_device_system(void)
{
    TEST_ASSERT_EQUAL_INT32(1, robotics_browse(0, 85, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, g_ref[0].ref_type_id);
    TEST_ASSERT_TRUE(g_ref[0].is_forward);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_ref[0].node_class);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_TYPEDEF_BASE_OBJECT, g_ref[0].type_def_id);
    TEST_ASSERT_EQUAL_UINT16(model_ns(), g_ref[0].target_ns);
    TEST_ASSERT_EQUAL_STRING("Cell-1", g_ref[0].browse_name);

    g_mds.name = NULL;
    TEST_ASSERT_EQUAL_INT32(1, robotics_browse(0, 85, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_STRING("MotionDeviceSystem", g_ref[0].browse_name);
}

// MotionDeviceSystemType's three folders, OPC 40010-1.
void test_motion_device_system_components(void)
{
    browse(root(), 3);
    TEST_ASSERT_EQUAL_STRING("MotionDevices", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Controllers", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("SafetyStates", g_ref[2].browse_name);
    // Opc.Ua.Robotics.NodeSet2.xml types all three FolderType (61), not BaseObjectType (58):
    // MotionDeviceSystemType reaches each by HasComponent to a FolderType node.
    for (int32_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_ref[i].node_class);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_TYPEDEF_FOLDER, g_ref[i].type_def_id);
    }
}

// A folder points at its members with Organizes (35), not HasComponent (47).
void test_folders_organize_their_members(void)
{
    browse(root(), 3);
    uint32_t devices = g_ref[0].target_id;
    uint32_t controllers = g_ref[1].target_id;
    uint32_t safety_states = g_ref[2].target_id;

    browse(devices, 1);
    TEST_ASSERT_EQUAL_STRING("MotionDevice", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, g_ref[0].ref_type_id);

    browse(controllers, 1);
    TEST_ASSERT_EQUAL_STRING("Controller", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, g_ref[0].ref_type_id);

    browse(safety_states, 1);
    TEST_ASSERT_EQUAL_STRING("SafetyState", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, g_ref[0].ref_type_id);
}

// MotionDeviceType: four identity Strings, the category, then ParameterSet and Axes.
void test_motion_device_identity(void)
{
    browse(root(), 3);
    browse(g_ref[0].target_id, 1);
    uint32_t device = g_ref[0].target_id;

    browse(device, 7);
    static const char *const NAMES[7] = {"Manufacturer",         "Model",        "ProductCode", "SerialNumber",
                                         "MotionDeviceCategory", "ParameterSet", "Axes"};
    for (int32_t i = 0; i < 7; i++)
    {
        TEST_ASSERT_EQUAL_STRING(NAMES[i], g_ref[i].browse_name);
    }
    // MotionDeviceType reaches every nameplate leaf by HasProperty (46) to a PropertyType (68) node,
    // per Opc.Ua.Robotics.NodeSet2.xml. A client browsing for a property finds nothing otherwise.
    for (int32_t i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_VARIABLE, g_ref[i].node_class);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_HAS_PROPERTY, g_ref[i].ref_type_id);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_TYPEDEF_PROPERTY, g_ref[i].type_def_id);
    }
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_ref[5].node_class);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_ref[6].node_class);

    TEST_ASSERT_EQUAL_STRING("Acme Robotics", read_ok(g_ref[0].target_id).str);
    TEST_ASSERT_EQUAL_STRING("AR-6", read_ok(g_ref[1].target_id).str);
    TEST_ASSERT_EQUAL_STRING("PC-6", read_ok(g_ref[2].target_id).str);
    TEST_ASSERT_EQUAL_STRING("SN-6", read_ok(g_ref[3].target_id).str);
    OpcUaVariant cat = read_ok(g_ref[4].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_INT32, cat.type);
    TEST_ASSERT_EQUAL_INT32((int32_t)ROBOTICS_CAT_ARTICULATED_ROBOT, cat.i32);
}

// ParameterSet: OnPath and InControl as Booleans, SpeedOverride as a Double.
void test_parameter_set_values(void)
{
    browse(root(), 3);
    browse(g_ref[0].target_id, 1);
    browse(g_ref[0].target_id, 7);
    uint32_t paramset = g_ref[5].target_id;

    browse(paramset, 3);
    TEST_ASSERT_EQUAL_STRING("OnPath", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("InControl", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("SpeedOverride", g_ref[2].browse_name);

    OpcUaVariant on_path = read_ok(g_ref[0].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_BOOL, on_path.type);
    TEST_ASSERT_TRUE(on_path.b);
    TEST_ASSERT_TRUE(read_ok(g_ref[1].target_id).b);
    OpcUaVariant so = read_ok(g_ref[2].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_DOUBLE, so.type);
    assert_f64(75.0, so.f64);
}

// The Axes folder is parametric: exactly axis_count members, named Axis_1 upward.
void test_axes_folder_follows_the_bound_axis_count(void)
{
    browse(root(), 3);
    browse(g_ref[0].target_id, 1);
    browse(g_ref[0].target_id, 7);
    uint32_t axes = g_ref[6].target_id;

    browse(axes, 3);
    TEST_ASSERT_EQUAL_STRING("Axis_1", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Axis_2", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("Axis_3", g_ref[2].browse_name);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, g_ref[0].ref_type_id);

    // widen the machine and the folder widens with it, up to the build's axis cap
    g_mds.device.axis_count = PROTOCORE_ROBOTICS_AXES;
    browse(axes, (int32_t)PROTOCORE_ROBOTICS_AXES);
    TEST_ASSERT_EQUAL_STRING("Axis_1", g_ref[0].browse_name);

    // and a machine with no axes lists none
    g_mds.device.axis_count = 0;
    browse(axes, 0);
}

// The four axis variables, each read from the axis they belong to. Distinct values per axis are
// what makes an id-arithmetic slip visible.
void test_axis_variables_read_their_own_axis(void)
{
    browse(root(), 3);
    browse(g_ref[0].target_id, 1);
    browse(g_ref[0].target_id, 7);
    uint32_t axes = g_ref[6].target_id;
    browse(axes, 3);
    uint32_t axis_id[3] = {g_ref[0].target_id, g_ref[1].target_id, g_ref[2].target_id};

    for (uint32_t k = 0; k < 3; k++)
    {
        browse(axis_id[k], 4);
        TEST_ASSERT_EQUAL_STRING("ActualPosition", g_ref[0].browse_name);
        TEST_ASSERT_EQUAL_STRING("ActualSpeed", g_ref[1].browse_name);
        TEST_ASSERT_EQUAL_STRING("ActualAcceleration", g_ref[2].browse_name);
        TEST_ASSERT_EQUAL_STRING("MotionProfile", g_ref[3].browse_name);

        OpcUaVariant pos = read_ok(g_ref[0].target_id);
        TEST_ASSERT_EQUAL_INT(OPCUA_VAR_DOUBLE, pos.type);
        assert_f64(10.0 + (double)k, pos.f64);
        assert_f64(100.0 + (double)k, read_ok(g_ref[1].target_id).f64);
        assert_f64(1000.0 + (double)k, read_ok(g_ref[2].target_id).f64);
        OpcUaVariant profile = read_ok(g_ref[3].target_id);
        TEST_ASSERT_EQUAL_INT(OPCUA_VAR_INT32, profile.type);
        TEST_ASSERT_EQUAL_INT32((int32_t)ROBOTICS_PROFILE_ROTARY, profile.i32);
    }
}

// An axis past the bound count is not in the model, even though its id is arithmetically valid.
void test_axis_beyond_the_bound_count_is_absent(void)
{
    browse(root(), 3);
    browse(g_ref[0].target_id, 1);
    browse(g_ref[0].target_id, 7);
    uint32_t axes = g_ref[6].target_id;
    browse(axes, 3);
    uint32_t axis3 = g_ref[2].target_id;
    uint32_t axis4 = axis3 + 10u; // the id axis 4 would have

    OpcUaVariant v;
    TEST_ASSERT_TRUE(robotics_read(model_ns(), axis3 + 1u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(robotics_read(model_ns(), axis4 + 1u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_INT32(-1, robotics_browse(model_ns(), axis4, g_ref, REF_MAX));

    // and a sub-id past the four axis variables is not one either
    TEST_ASSERT_FALSE(robotics_read(model_ns(), axis3 + 5u, OPCUA_ATTR_VALUE, &v));
}

// Controller identity plus its nested Software object.
void test_controller_and_software(void)
{
    browse(root(), 3);
    uint32_t controllers = g_ref[1].target_id;
    browse(controllers, 1);
    uint32_t controller = g_ref[0].target_id;

    browse(controller, 5);
    TEST_ASSERT_EQUAL_STRING("Manufacturer", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Model", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("ProductCode", g_ref[2].browse_name);
    TEST_ASSERT_EQUAL_STRING("SerialNumber", g_ref[3].browse_name);
    TEST_ASSERT_EQUAL_STRING("Software", g_ref[4].browse_name);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_ref[4].node_class);
    TEST_ASSERT_EQUAL_STRING("Acme Control", read_ok(g_ref[0].target_id).str);
    TEST_ASSERT_EQUAL_STRING("CSN-1", read_ok(g_ref[3].target_id).str);
    uint32_t software = g_ref[4].target_id;

    browse(software, 3);
    TEST_ASSERT_EQUAL_STRING("Manufacturer", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Model", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("SoftwareRevision", g_ref[2].browse_name);
    TEST_ASSERT_EQUAL_STRING("Acme Software", read_ok(g_ref[0].target_id).str);
    TEST_ASSERT_EQUAL_STRING("RobotOS", read_ok(g_ref[1].target_id).str);
    TEST_ASSERT_EQUAL_STRING("3.4.5", read_ok(g_ref[2].target_id).str);
}

// SafetyState reaches its values through its own ParameterSet.
void test_safety_state(void)
{
    browse(root(), 3);
    browse(g_ref[2].target_id, 1);
    uint32_t safety = g_ref[0].target_id;

    browse(safety, 1);
    TEST_ASSERT_EQUAL_STRING("ParameterSet", g_ref[0].browse_name);
    uint32_t paramset = g_ref[0].target_id;

    browse(paramset, 3);
    TEST_ASSERT_EQUAL_STRING("OperationalMode", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("EmergencyStop", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("ProtectiveStop", g_ref[2].browse_name);

    OpcUaVariant mode = read_ok(g_ref[0].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_INT32, mode.type);
    TEST_ASSERT_EQUAL_INT32((int32_t)ROBOTICS_MODE_AUTOMATIC, mode.i32);
    OpcUaVariant estop = read_ok(g_ref[1].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_BOOL, estop.type);
    TEST_ASSERT_FALSE(estop.b);
    TEST_ASSERT_TRUE(read_ok(g_ref[2].target_id).b);
}

// A null model string is served as an empty String, never as a null a client would dereference.
void test_null_strings_read_as_empty(void)
{
    g_mds.controller.sw_revision = NULL;
    browse(root(), 3);
    browse(g_ref[1].target_id, 1);
    browse(g_ref[0].target_id, 5);
    browse(g_ref[4].target_id, 3);

    OpcUaVariant v = read_ok(g_ref[2].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_NOT_NULL(v.str);
    TEST_ASSERT_EQUAL_STRING("", v.str);
    TEST_ASSERT_EQUAL_INT32(0, v.str_len);
}

// Outside the model, another namespace, or another attribute: a clean miss.
void test_reads_outside_the_model_are_refused(void)
{
    OpcUaVariant v;
    uint32_t r = root();
    browse(r, 3);
    browse(g_ref[0].target_id, 1);
    uint32_t device = g_ref[0].target_id;
    browse(device, 7);
    uint32_t manufacturer = g_ref[0].target_id;

    TEST_ASSERT_FALSE(robotics_read(model_ns(), 1u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(robotics_read(model_ns(), r, OPCUA_ATTR_VALUE, &v)); // an Object
    TEST_ASSERT_FALSE(robotics_read((uint16_t)(model_ns() + 1), manufacturer, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(robotics_read(model_ns(), manufacturer, OPCUA_ATTR_VALUE + 1u, &v));

    TEST_ASSERT_EQUAL_INT32(-1, robotics_browse(model_ns(), manufacturer, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_INT32(-1, robotics_browse(model_ns(), 1u, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_INT32(-1, robotics_browse((uint16_t)(model_ns() + 1), r, g_ref, REF_MAX));
}

// Before a model is bound there is nothing to serve, including from the Objects folder.
void test_nothing_is_served_before_bind(void)
{
    OpcUaVariant v;
    uint32_t r = root();
    browse(r, 3);
    browse(g_ref[0].target_id, 1);
    browse(g_ref[0].target_id, 7);
    uint32_t manufacturer = g_ref[0].target_id;

    RoboticsV.bind_args.mds = NULL;
    Robotics.bind(protocore_robotics_span());
    TEST_ASSERT_EQUAL_INT32(-1, robotics_browse(0, 85, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_INT32(-1, robotics_browse(model_ns(), r, g_ref, REF_MAX));
    TEST_ASSERT_FALSE(robotics_read(model_ns(), manufacturer, OPCUA_ATTR_VALUE, &v));
}

// A Browse never writes past the caller's array.
void test_browse_respects_the_reference_cap(void)
{
    uint32_t r = root();
    TEST_ASSERT_EQUAL_INT32(2, robotics_browse(model_ns(), r, g_ref, 2));
    TEST_ASSERT_EQUAL_STRING("MotionDevices", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Controllers", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_INT32(0, robotics_browse(model_ns(), r, g_ref, 0));
}

// The whole model is reachable from ns0 i=85 with no dead reference: every Object yields children
// and every Variable reads. With 3 axes: 5 device + 3 ParameterSet + 3*4 axis + 4 controller
// + 3 software + 3 safety = 30 variables.
void test_every_reference_resolves(void)
{
    uint32_t queue[64];
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t variables = 0;

    queue[tail++] = root();
    while (head < tail)
    {
        uint32_t id = queue[head++];
        int32_t n = robotics_browse(model_ns(), id, g_ref, REF_MAX);
        TEST_ASSERT_TRUE(n > 0);
        OpcUaReference kids[REF_MAX];
        memcpy(kids, g_ref, sizeof(kids));
        for (int32_t i = 0; i < n; i++)
        {
            if (kids[i].node_class == OPCUA_NODECLASS_VARIABLE)
            {
                OpcUaVariant v;
                TEST_ASSERT_TRUE_MESSAGE(robotics_read(model_ns(), kids[i].target_id, OPCUA_ATTR_VALUE, &v),
                                         kids[i].browse_name);
                variables++;
            }
            else
            {
                TEST_ASSERT_TRUE(tail < 64);
                queue[tail++] = kids[i].target_id;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(30u, variables);
}
