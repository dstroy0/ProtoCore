// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OPC UA for Robotics (OPC 40010-1) MotionDeviceSystem model:
// the Browse hierarchy shape + the Read resolver over a bound RoboticsMotionDeviceSystem, including the
// parametric Axes (one Axis_k object per bound axis, four variables each).

#include "services/machine_tool/robotics/robotics.h"
#include <string.h>
#include <unity.h>

// Node ids (must track the internal enum in robotics.cpp).
enum
{
    N_MOTIONDEVICESYSTEM = 6000,
    N_MOTIONDEVICES = 6100,
    N_MOTIONDEVICE = 6200,
    N_MD_MANUFACTURER = 6201,
    N_MD_MODEL = 6202,
    N_MD_PRODUCTCODE = 6203,
    N_MD_SERIAL = 6204,
    N_MD_CATEGORY = 6205,
    N_MDP_INCONTROL = 6212,
    N_MD_PARAMSET = 6210,
    N_MDP_ONPATH = 6211,
    N_MDP_SPEEDOVERRIDE = 6213,
    N_MD_AXES = 6300,
    N_AXIS_BASE = 6400,
    N_AXIS1 = 6410,
    N_AXIS1_POSITION = 6411,
    N_AXIS1_SPEED = 6412,
    N_AXIS1_ACCEL = 6413,
    N_AXIS1_PROFILE = 6414,
    N_AXIS2 = 6420,
    N_AXIS2_SPEED = 6422,
    N_CONTROLLERS = 6600,
    N_CONTROLLER = 6610,
    N_CT_MANUFACTURER = 6611,
    N_CT_MODEL = 6612,
    N_CT_PRODUCTCODE = 6613,
    N_CT_SERIAL = 6614,
    N_CT_SOFTWARE = 6620,
    N_SW_MANUFACTURER = 6621,
    N_SW_MODEL = 6622,
    N_SW_REVISION = 6623,
    N_SAFETYSTATES = 6700,
    N_SAFETYSTATE = 6710,
    N_SS_PARAMSET = 6720,
    N_SSP_OPMODE = 6721,
    N_SSP_ESTOP = 6722,
    N_SSP_PSTOP = 6723,
};

static RoboticsMotionDeviceSystem g_mds;

void setUp(void)
{
    memset(&g_mds, 0, sizeof(g_mds));
    g_mds.name = "Robot-1";
    g_mds.device.manufacturer = "Acme Robotics";
    g_mds.device.model = "AR-6";
    g_mds.device.product_code = "AR6-STD";
    g_mds.device.serial_number = "SN-R-0007";
    g_mds.device.category = RoboticsMotionDeviceCategory::ROBOTICS_CAT_ARTICULATED_ROBOT;
    g_mds.device.on_path = true;
    g_mds.device.in_control = true;
    g_mds.device.speed_override = 75.0;
    g_mds.device.axis_count = 3;
    // distinct per-axis values so a Read must pick the right axis
    g_mds.device.axes[0].actual_position = 10.5;
    g_mds.device.axes[0].actual_speed = 1.0;
    g_mds.device.axes[0].actual_acceleration = 0.1;
    g_mds.device.axes[0].motion_profile = RoboticsMotionProfile::ROBOTICS_PROFILE_ROTARY;
    g_mds.device.axes[1].actual_position = -20.25;
    g_mds.device.axes[1].actual_speed = 2.0;
    g_mds.device.axes[1].actual_acceleration = 0.2;
    g_mds.device.axes[1].motion_profile = RoboticsMotionProfile::ROBOTICS_PROFILE_LINEAR;
    g_mds.device.axes[2].actual_position = 33.0;
    g_mds.device.axes[2].actual_speed = 3.0;
    g_mds.device.axes[2].actual_acceleration = 0.3;
    g_mds.device.axes[2].motion_profile = RoboticsMotionProfile::ROBOTICS_PROFILE_ROTARY_ENDLESS;
    g_mds.controller.manufacturer = "Acme Controls";
    g_mds.controller.model = "CTRL-9";
    g_mds.controller.product_code = "C9-STD";
    g_mds.controller.serial_number = "SN-C-0009";
    g_mds.controller.sw_manufacturer = "Acme Software";
    g_mds.controller.sw_model = "RobOS";
    g_mds.controller.sw_revision = "4.2.0";
    g_mds.safety.operational_mode = RoboticsOperationalMode::ROBOTICS_MODE_AUTOMATIC;
    g_mds.safety.emergency_stop = false;
    g_mds.safety.protective_stop = true;
    pc_robotics_bind(&g_mds);
}
void tearDown(void)
{
}

static int32_t browse(uint16_t ns, uint32_t id, OpcUaReference *refs, uint32_t cap)
{
    return pc_robotics_browse(ns, id, refs, cap);
}
static const OpcUaReference *find_ref(const OpcUaReference *refs, int32_t n, const char *name)
{
    for (int32_t i = 0; i < n; i++)
    {
        if (refs[i].browse_name && strcmp(refs[i].browse_name, name) == 0)
        {
            return &refs[i];
        }
    }
    return nullptr;
}

// --- Browse hierarchy -------------------------------------------------------
static void test_browse_objects_folder_has_system(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(0, 85, refs, 8); // Objects folder
    TEST_ASSERT_EQUAL_INT32(1, n);
    TEST_ASSERT_EQUAL_UINT32(N_MOTIONDEVICESYSTEM, refs[0].target_id);
    TEST_ASSERT_EQUAL_UINT32(PC_ROBOTICS_NS, refs[0].target_ns);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, refs[0].node_class);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, refs[0].ref_type_id);
    TEST_ASSERT_EQUAL_STRING("Robot-1", refs[0].browse_name); // uses mds->name
}

static void test_browse_system_folders(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_ROBOTICS_NS, N_MOTIONDEVICESYSTEM, refs, 8);
    TEST_ASSERT_EQUAL_INT32(3, n); // MotionDevices, Controllers, SafetyStates
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "MotionDevices"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "Controllers"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "SafetyStates"));
}

static void test_browse_motiondevice_components(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_ROBOTICS_NS, N_MOTIONDEVICES, refs, 8);
    TEST_ASSERT_EQUAL_INT32(1, n);                                          // MotionDevice
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, refs[0].ref_type_id); // folder -> member
    TEST_ASSERT_EQUAL_UINT32(N_MOTIONDEVICE, refs[0].target_id);

    n = browse(PC_ROBOTICS_NS, N_MOTIONDEVICE, refs, 8);
    TEST_ASSERT_EQUAL_INT32(7, n); // Manufacturer, Model, ProductCode, SerialNumber, Category, ParameterSet, Axes
    const OpcUaReference *man = find_ref(refs, n, "Manufacturer");
    TEST_ASSERT_NOT_NULL(man);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_VARIABLE, man->node_class);
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "MotionDeviceCategory"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ParameterSet"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "Axes"));
}

static void test_browse_parameterset(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_ROBOTICS_NS, N_MD_PARAMSET, refs, 8);
    TEST_ASSERT_EQUAL_INT32(3, n); // OnPath, InControl, SpeedOverride
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "OnPath"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "InControl"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "SpeedOverride"));
}

static void test_browse_axes_parametric(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_ROBOTICS_NS, N_MD_AXES, refs, 8);
    TEST_ASSERT_EQUAL_INT32(3, n); // axis_count = 3
    const OpcUaReference *a1 = find_ref(refs, n, "Axis_1");
    TEST_ASSERT_NOT_NULL(a1);
    TEST_ASSERT_EQUAL_UINT32(N_AXIS1, a1->target_id);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, a1->ref_type_id); // folder -> member
    const OpcUaReference *a2 = find_ref(refs, n, "Axis_2");
    TEST_ASSERT_NOT_NULL(a2);
    TEST_ASSERT_EQUAL_UINT32(N_AXIS2, a2->target_id);
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "Axis_3"));
    TEST_ASSERT_NULL(find_ref(refs, n, "Axis_4")); // only 3 bound

    // Browse an Axis_k object -> its four variables.
    n = browse(PC_ROBOTICS_NS, N_AXIS1, refs, 8);
    TEST_ASSERT_EQUAL_INT32(4, n);
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ActualPosition"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ActualSpeed"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ActualAcceleration"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "MotionProfile"));
    const OpcUaReference *pos = find_ref(refs, n, "ActualPosition");
    TEST_ASSERT_EQUAL_UINT32(N_AXIS1_POSITION, pos->target_id);
}

static void test_browse_controller_and_software(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_ROBOTICS_NS, N_CONTROLLERS, refs, 8);
    TEST_ASSERT_EQUAL_INT32(1, n); // Controller
    TEST_ASSERT_EQUAL_UINT32(N_CONTROLLER, refs[0].target_id);

    n = browse(PC_ROBOTICS_NS, N_CONTROLLER, refs, 8);
    TEST_ASSERT_EQUAL_INT32(5, n); // Manufacturer, Model, ProductCode, SerialNumber, Software
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "Software"));

    n = browse(PC_ROBOTICS_NS, N_CT_SOFTWARE, refs, 8);
    TEST_ASSERT_EQUAL_INT32(3, n); // Manufacturer, Model, SoftwareRevision
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "SoftwareRevision"));
}

static void test_browse_safetystate(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_ROBOTICS_NS, N_SAFETYSTATES, refs, 8);
    TEST_ASSERT_EQUAL_INT32(1, n); // SafetyState
    n = browse(PC_ROBOTICS_NS, N_SAFETYSTATE, refs, 8);
    TEST_ASSERT_EQUAL_INT32(1, n); // ParameterSet
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ParameterSet"));
    n = browse(PC_ROBOTICS_NS, N_SS_PARAMSET, refs, 8);
    TEST_ASSERT_EQUAL_INT32(3, n); // OperationalMode, EmergencyStop, ProtectiveStop
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "OperationalMode"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "EmergencyStop"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ProtectiveStop"));
}

static void test_browse_leaf_and_unknown_return_negative(void)
{
    OpcUaReference refs[4];
    TEST_ASSERT_EQUAL_INT32(-1, browse(PC_ROBOTICS_NS, N_MD_MANUFACTURER, refs, 4)); // a leaf Variable
    TEST_ASSERT_EQUAL_INT32(-1, browse(PC_ROBOTICS_NS, N_AXIS1_POSITION, refs, 4));  // an axis-variable leaf
    TEST_ASSERT_EQUAL_INT32(-1, browse(PC_ROBOTICS_NS, 999999, refs, 4));            // unknown node
    TEST_ASSERT_EQUAL_INT32(-1, browse(7, N_MOTIONDEVICESYSTEM, refs, 4));           // wrong namespace
    TEST_ASSERT_EQUAL_INT32(-1, browse(PC_ROBOTICS_NS, N_AXIS_BASE + 40, refs, 4));  // Axis_4 (past axis_count)
}

// --- Read resolver ----------------------------------------------------------
static void test_read_motiondevice_identity(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_MD_MANUFACTURER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_STRING("Acme Robotics", v.str);

    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_MD_CATEGORY, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_INT32, v.type);
    TEST_ASSERT_EQUAL_INT32((int32_t)RoboticsMotionDeviceCategory::ROBOTICS_CAT_ARTICULATED_ROBOT, v.i32);

    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_MDP_ONPATH, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_BOOL, v.type);
    TEST_ASSERT_TRUE(v.b);

    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_MDP_SPEEDOVERRIDE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_DOUBLE, v.type);
    TEST_ASSERT_TRUE(v.f64 == 75.0);
}

static void test_read_axes_pick_the_right_axis(void)
{
    OpcUaVariant v;
    // Axis_1 ActualPosition = 10.5
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1_POSITION, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_DOUBLE, v.type);
    TEST_ASSERT_TRUE(v.f64 == 10.5);
    // Axis_1 MotionProfile enum
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1_PROFILE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_INT32, v.type);
    TEST_ASSERT_EQUAL_INT32((int32_t)RoboticsMotionProfile::ROBOTICS_PROFILE_ROTARY, v.i32);
    // Axis_2 ActualSpeed = 2.0 (distinct axis)
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS2_SPEED, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.f64 == 2.0);
    // Axis_3 ActualPosition = 33.0
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, 6430 + 1, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.f64 == 33.0);
}

static void test_read_controller_and_safety(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_CT_MANUFACTURER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("Acme Controls", v.str);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_SW_REVISION, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("4.2.0", v.str);

    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_SSP_OPMODE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_INT32((int32_t)RoboticsOperationalMode::ROBOTICS_MODE_AUTOMATIC, v.i32);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_SSP_ESTOP, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(v.b);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_SSP_PSTOP, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.b);
}

static void test_read_null_string_served_as_empty(void)
{
    g_mds.device.model = nullptr; // a null field must not crash - served as ""
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, 6202 /*Model*/, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_STRING("", v.str);
    TEST_ASSERT_EQUAL_INT32(0, v.str_len);
}

static void test_read_rejects_unknown_ns_attr_and_axis_out_of_range(void)
{
    OpcUaVariant v;
    TEST_ASSERT_FALSE(pc_robotics_read(PC_ROBOTICS_NS, 999999, OPCUA_ATTR_VALUE, &v));              // unknown node
    TEST_ASSERT_FALSE(pc_robotics_read(7, N_MD_MANUFACTURER, OPCUA_ATTR_VALUE, &v));                // wrong namespace
    TEST_ASSERT_FALSE(pc_robotics_read(PC_ROBOTICS_NS, N_MD_MANUFACTURER, 12 /*!Value*/, &v));      // not Value attr
    TEST_ASSERT_FALSE(pc_robotics_read(PC_ROBOTICS_NS, 6441 /*Axis_4 pos*/, OPCUA_ATTR_VALUE, &v)); // past axis_count
}

static void test_read_before_bind_is_a_clean_miss(void)
{
    pc_robotics_bind(nullptr); // no model bound
    OpcUaVariant v;
    TEST_ASSERT_FALSE(pc_robotics_read(PC_ROBOTICS_NS, N_MD_MANUFACTURER, OPCUA_ATTR_VALUE, &v));
    OpcUaReference refs[4];
    TEST_ASSERT_EQUAL_INT32(-1, pc_robotics_browse(0, 85, refs, 4));
}

// Every MotionDevice / Controller / Software leaf the focused tests above skip still resolves to its
// bound model field - the arms of the Read switch nothing else visits.
static void test_read_every_remaining_leaf(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_MD_PRODUCTCODE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_STRING("AR6-STD", v.str);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_MD_SERIAL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("SN-R-0007", v.str);

    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_MDP_INCONTROL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_BOOL, v.type);
    TEST_ASSERT_TRUE(v.b);

    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_CT_MODEL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("CTRL-9", v.str);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_CT_PRODUCTCODE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("C9-STD", v.str);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_CT_SERIAL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("SN-C-0009", v.str);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_SW_MANUFACTURER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("Acme Software", v.str);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_SW_MODEL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("RobOS", v.str);
}

// All four axis variables of one axis resolve, each with its own Variant type - in particular
// ActualAcceleration, the arm the focused axis test does not visit.
static void test_read_axis_all_four_variables(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1_POSITION, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.f64 == 10.5);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1_SPEED, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.f64 == 1.0);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1_ACCEL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_DOUBLE, v.type);
    TEST_ASSERT_TRUE(v.f64 == 0.1);
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1_PROFILE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OpcUaVariantType::OPCUA_VAR_INT32, v.type);
}

// Only sub-ids 1..4 under an in-range Axis_k are variables: the Axis object id itself (sub 0) and a
// sub-id past MotionProfile fall through to the plain node switch and miss, and an id inside the
// AXIS_BASE decade (k == 0) is not an axis at all.
static void test_read_axis_sub_id_bounds(void)
{
    OpcUaVariant v;
    TEST_ASSERT_FALSE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1, OPCUA_ATTR_VALUE, &v));     // sub 0: the object
    TEST_ASSERT_FALSE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1 + 5, OPCUA_ATTR_VALUE, &v)); // sub 5: past MotionProfile
    TEST_ASSERT_FALSE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS_BASE + 5, OPCUA_ATTR_VALUE, &v)); // k == 0
}

// A Browse buffer smaller than the node's child count is filled to the brim and then left alone -
// add_ref must drop the overflow instead of writing past out[max-1].
static void test_browse_clamps_to_max(void)
{
    OpcUaReference refs[8];
    memset(refs, 0, sizeof(refs));
    int32_t n = browse(PC_ROBOTICS_NS, N_MOTIONDEVICE, refs, 2); // MotionDevice has 7 children
    TEST_ASSERT_EQUAL_INT32(2, n);
    TEST_ASSERT_EQUAL_STRING("Manufacturer", refs[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Model", refs[1].browse_name);
    TEST_ASSERT_NULL(refs[2].browse_name); // the third child was dropped, not written
}

// Namespace 0 holds standard nodes; only the Objects folder (i=85) is ours, anything else in ns0 misses.
static void test_browse_ns0_other_than_objects_folder_misses(void)
{
    OpcUaReference refs[4];
    TEST_ASSERT_EQUAL_INT32(-1, browse(0, 84, refs, 4));   // Root folder
    TEST_ASSERT_EQUAL_INT32(-1, browse(0, 86, refs, 4));   // Types folder
    TEST_ASSERT_EQUAL_INT32(-1, browse(0, 6000, refs, 4)); // our MotionDeviceSystem id, but in ns0
}

// An unnamed model still publishes the system under the Objects folder, using the fallback name.
static void test_browse_objects_folder_without_model_name(void)
{
    g_mds.name = nullptr;
    OpcUaReference refs[4];
    int32_t n = browse(0, 85, refs, 4);
    TEST_ASSERT_EQUAL_INT32(1, n);
    TEST_ASSERT_EQUAL_STRING("MotionDeviceSystem", refs[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("MotionDeviceSystem", refs[0].display_name);
    TEST_ASSERT_EQUAL_UINT32(N_MOTIONDEVICESYSTEM, refs[0].target_id);
}

// The Axes folder never publishes more axes than the build compiled room for, even if the bound
// model over-declares axis_count - the loop is clamped by PC_ROBOTICS_AXES as well.
static void test_browse_axes_clamped_to_compiled_maximum(void)
{
    g_mds.device.axis_count = PC_ROBOTICS_AXES + 1; // over-declared by one
    OpcUaReference refs[PC_ROBOTICS_AXES + 2];
    memset(refs, 0, sizeof(refs));
    int32_t n = browse(PC_ROBOTICS_NS, N_MD_AXES, refs, PC_ROBOTICS_AXES + 2);
    TEST_ASSERT_EQUAL_INT32((int32_t)PC_ROBOTICS_AXES, n);
    TEST_ASSERT_EQUAL_STRING("Axis_1", refs[0].browse_name);
    TEST_ASSERT_EQUAL_UINT32(N_AXIS_BASE + PC_ROBOTICS_AXES * 10, refs[PC_ROBOTICS_AXES - 1].target_id);
    TEST_ASSERT_NULL(refs[PC_ROBOTICS_AXES].browse_name); // the over-declared axis was not emitted
}

// install() binds the model as well as registering the resolvers with the OPC UA server, so a Read
// right after install resolves against the newly bound system.
static void test_install_binds_the_model(void)
{
    static RoboticsMotionDeviceSystem other;
    memset(&other, 0, sizeof(other));
    other.name = "Robot-2";
    other.device.manufacturer = "Other Robotics";
    other.device.axis_count = 1;
    other.device.axes[0].actual_position = 99.5;

    pc_robotics_install(&other);

    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_MD_MANUFACTURER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("Other Robotics", v.str); // the installed model, not the setUp one
    TEST_ASSERT_TRUE(pc_robotics_read(PC_ROBOTICS_NS, N_AXIS1_POSITION, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.f64 == 99.5);
    OpcUaReference refs[4];
    int32_t n = pc_robotics_browse(0, 85, refs, 4);
    TEST_ASSERT_EQUAL_INT32(1, n);
    TEST_ASSERT_EQUAL_STRING("Robot-2", refs[0].browse_name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_read_every_remaining_leaf);
    RUN_TEST(test_read_axis_all_four_variables);
    RUN_TEST(test_read_axis_sub_id_bounds);
    RUN_TEST(test_browse_clamps_to_max);
    RUN_TEST(test_browse_ns0_other_than_objects_folder_misses);
    RUN_TEST(test_browse_objects_folder_without_model_name);
    RUN_TEST(test_browse_axes_clamped_to_compiled_maximum);
    RUN_TEST(test_install_binds_the_model);
    RUN_TEST(test_browse_objects_folder_has_system);
    RUN_TEST(test_browse_system_folders);
    RUN_TEST(test_browse_motiondevice_components);
    RUN_TEST(test_browse_parameterset);
    RUN_TEST(test_browse_axes_parametric);
    RUN_TEST(test_browse_controller_and_software);
    RUN_TEST(test_browse_safetystate);
    RUN_TEST(test_browse_leaf_and_unknown_return_negative);
    RUN_TEST(test_read_motiondevice_identity);
    RUN_TEST(test_read_axes_pick_the_right_axis);
    RUN_TEST(test_read_controller_and_safety);
    RUN_TEST(test_read_null_string_served_as_empty);
    RUN_TEST(test_read_rejects_unknown_ns_attr_and_axis_out_of_range);
    RUN_TEST(test_read_before_bind_is_a_clean_miss);
    return UNITY_END();
}
