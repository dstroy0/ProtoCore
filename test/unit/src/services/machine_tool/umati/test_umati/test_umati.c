// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the umati / OPC UA for Machine Tools model (services/machine_tool/umati/umati.h).
//
// Two published sources anchor this file. The OPC UA base NodeIds (OPC 10000-6 Annex A, the
// Opc.Ua.NodeIds.csv the OPC Foundation publishes) fix ObjectsFolder = 85, Organizes = 35,
// HasComponent = 47, BaseObjectType = 58, BaseDataVariableType = 63, NodeClass Object = 1 /
// Variable = 2, and the Value attribute id = 13. The companion spec OPC 40501-1 fixes the
// BrowseNames, published in the OPC Foundation's Opc.Ua.MachineTool.NodeIds.csv: MachineToolType
// carries Identification, Monitoring, Production and Notification; Monitoring carries MachineTool
// with OperationMode and PowerOnDuration; the channel and spindle monitoring types carry
// FeedOverride, ChannelState and IsRotating.
//
// test_objects_folder_organizes_the_machine_tool is the load-bearing case: a client reaches this
// whole model by browsing ns0 i=85, so if that one reference carries the wrong ReferenceType or the
// wrong NodeClass the machine is invisible no matter how correct the rest of the tree is.
//
// The OperationMode / ChannelState numeric enumerations of OPC 40501-1 were NOT obtainable (the
// document is behind registration and the published NodeIds file names the enum without its
// values), so those are covered as a round-trip property - the bound value is what a Read returns -
// rather than as published numbers.

#include "services/machine_tool/umati/umati.h"
#include <string.h>

#include <unity.h>

#define REF_MAX 16

static UmatiMachineTool g_mt;
static OpcUaReference g_ref[REF_MAX];

void setUp(void)
{
    memset(&g_mt, 0, sizeof(g_mt));
    memset(g_ref, 0, sizeof(g_ref));
    g_mt.name = "VMC-1";
    g_mt.ident.manufacturer = "Acme";
    g_mt.ident.model = "VMC-500";
    g_mt.ident.serial_number = "SN-42";
    g_mt.ident.software_revision = "2.1.0";
    g_mt.ident.product_instance_uri = "urn:acme:vmc:42";
    g_mt.ident.year_of_construction = 2024;
    g_mt.operation_mode = UMATI_OP_AUTOMATIC;
    g_mt.power_on_duration_s = 1234.5;
    g_mt.channel.state = UMATI_CH_RUNNING;
    g_mt.channel.feed_override = 87.5;
    g_mt.channel.rapid_override = 50.0;
    g_mt.channel.active_program = "O1234.NC";
    g_mt.spindle.rotation_speed = 8000.0;
    g_mt.spindle.override_value = 100.0;
    g_mt.spindle.is_rotating = PROTO_TRUE;
    g_mt.axis_x.actual_position = 10.25;
    g_mt.axis_y.actual_position = -3.5;
    g_mt.axis_z.actual_position = 0.125;
    g_mt.active_program = "O1234.NC";
    g_mt.produced_part_count = 77;
    g_mt.message_text = "Tool life low";
    g_mt.message_severity = 500;
    protocore_umati_bind(&g_mt);
}

void tearDown(void)
{
    protocore_umati_bind(NULL);
}

// Browse a node and require exactly @p want references back.
static int32_t browse(uint32_t id, int32_t want)
{
    int32_t n = protocore_umati_browse(PROTOCORE_UMATI_NS, id, g_ref, REF_MAX);
    TEST_ASSERT_EQUAL_INT32(want, n);
    return n;
}

// One reference: its BrowseName, NodeClass and the TypeDefinition that follows from it.
static void assert_ref(int32_t i, const char *name, uint32_t node_class)
{
    TEST_ASSERT_EQUAL_STRING_MESSAGE(name, g_ref[i].browse_name, name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(name, g_ref[i].display_name, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(node_class, g_ref[i].node_class, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(OPCUA_REFTYPE_HAS_COMPONENT, g_ref[i].ref_type_id, name);
    TEST_ASSERT_TRUE_MESSAGE(g_ref[i].is_forward, name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(PROTOCORE_UMATI_NS, g_ref[i].target_ns, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(node_class == OPCUA_NODECLASS_VARIABLE ? OPCUA_TYPEDEF_BASE_DATA_VARIABLE
                                                                            : OPCUA_TYPEDEF_BASE_OBJECT,
                                     g_ref[i].type_def_id, name);
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
    TEST_ASSERT_TRUE(protocore_umati_read(PROTOCORE_UMATI_NS, id, OPCUA_ATTR_VALUE, &v));
    return v;
}

// ns0 i=85 is the Objects folder; it Organizes (35) the MachineTool, which is how a client finds it.
void test_objects_folder_organizes_the_machine_tool(void)
{
    TEST_ASSERT_EQUAL_INT32(1, protocore_umati_browse(0, 85, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, g_ref[0].ref_type_id);
    TEST_ASSERT_TRUE(g_ref[0].is_forward);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_ref[0].node_class);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_TYPEDEF_BASE_OBJECT, g_ref[0].type_def_id);
    TEST_ASSERT_EQUAL_STRING("VMC-1", g_ref[0].browse_name); // the bound machine's own name
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_UMATI_NS, g_ref[0].target_ns);

    // an unnamed machine still browses, under the type's own name
    g_mt.name = NULL;
    TEST_ASSERT_EQUAL_INT32(1, protocore_umati_browse(0, 85, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_STRING("MachineTool", g_ref[0].browse_name);
}

// MachineToolType's four components, OPC 40501-1.
void test_machine_tool_components(void)
{
    TEST_ASSERT_EQUAL_INT32(1, protocore_umati_browse(0, 85, g_ref, REF_MAX));
    uint32_t root = g_ref[0].target_id; // whatever id the Objects folder pointed at

    browse(root, 4);
    TEST_ASSERT_EQUAL_STRING("Identification", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Monitoring", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("Production", g_ref[2].browse_name);
    TEST_ASSERT_EQUAL_STRING("Notification", g_ref[3].browse_name);
    for (int32_t i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_ref[i].node_class);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_HAS_COMPONENT, g_ref[i].ref_type_id);
    }
}

// Identification's six variables carry the Machinery / MachineTool BrowseNames.
void test_identification_variables(void)
{
    protocore_umati_browse(0, 85, g_ref, REF_MAX);
    uint32_t root = g_ref[0].target_id;
    browse(root, 4);
    uint32_t ident = g_ref[0].target_id;

    browse(ident, 6);
    static const char *const NAMES[6] = {
        "Manufacturer", "Model", "SerialNumber", "YearOfConstruction", "SoftwareRevision", "ProductInstanceUri"};
    for (int32_t i = 0; i < 6; i++)
    {
        assert_ref(i, NAMES[i], OPCUA_NODECLASS_VARIABLE);
    }

    // and each one reads back the bound value, in the type OPC UA carries it as
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_STRING, read_ok(g_ref[0].target_id).type);
    TEST_ASSERT_EQUAL_STRING("Acme", read_ok(g_ref[0].target_id).str);
    TEST_ASSERT_EQUAL_STRING("VMC-500", read_ok(g_ref[1].target_id).str);
    TEST_ASSERT_EQUAL_STRING("SN-42", read_ok(g_ref[2].target_id).str);
    OpcUaVariant year = read_ok(g_ref[3].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_UINT32, year.type);
    TEST_ASSERT_EQUAL_UINT32(2024u, year.u32);
    TEST_ASSERT_EQUAL_STRING("2.1.0", read_ok(g_ref[4].target_id).str);
    TEST_ASSERT_EQUAL_STRING("urn:acme:vmc:42", read_ok(g_ref[5].target_id).str);
}

// Monitoring's sub-objects: the machine itself, one channel, one spindle, three linear axes.
void test_monitoring_sub_objects(void)
{
    protocore_umati_browse(0, 85, g_ref, REF_MAX);
    uint32_t root = g_ref[0].target_id;
    browse(root, 4);
    uint32_t mon = g_ref[1].target_id;

    browse(mon, 6);
    static const char *const NAMES[6] = {"MachineTool", "Channel", "Spindle", "Axis_X", "Axis_Y", "Axis_Z"};
    uint32_t ids[6];
    for (int32_t i = 0; i < 6; i++)
    {
        TEST_ASSERT_EQUAL_STRING(NAMES[i], g_ref[i].browse_name);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_ref[i].node_class);
        ids[i] = g_ref[i].target_id;
    }

    // Monitoring/MachineTool: OperationMode (Int32) and PowerOnDuration (Double)
    browse(ids[0], 2);
    TEST_ASSERT_EQUAL_STRING("OperationMode", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("PowerOnDuration", g_ref[1].browse_name);
    OpcUaVariant mode = read_ok(g_ref[0].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_INT32, mode.type);
    TEST_ASSERT_EQUAL_INT32((int32_t)UMATI_OP_AUTOMATIC, mode.i32);
    OpcUaVariant pod = read_ok(g_ref[1].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_DOUBLE, pod.type);
    assert_f64(1234.5, pod.f64);

    // Monitoring/Channel
    browse(ids[1], 4);
    TEST_ASSERT_EQUAL_STRING("ChannelState", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("FeedOverride", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("RapidOverride", g_ref[2].browse_name);
    TEST_ASSERT_EQUAL_STRING("ActiveProgram", g_ref[3].browse_name);
    TEST_ASSERT_EQUAL_INT32((int32_t)UMATI_CH_RUNNING, read_ok(g_ref[0].target_id).i32);
    assert_f64(87.5, read_ok(g_ref[1].target_id).f64);
    assert_f64(50.0, read_ok(g_ref[2].target_id).f64);
    TEST_ASSERT_EQUAL_STRING("O1234.NC", read_ok(g_ref[3].target_id).str);

    // Monitoring/Spindle
    browse(ids[2], 3);
    TEST_ASSERT_EQUAL_STRING("RotationSpeed", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("OverrideValue", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("IsRotating", g_ref[2].browse_name);
    assert_f64(8000.0, read_ok(g_ref[0].target_id).f64);
    OpcUaVariant rot = read_ok(g_ref[2].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_BOOL, rot.type);
    TEST_ASSERT_TRUE(rot.b);
}

// Each axis object exposes exactly one ActualPosition, and each reads its own value.
void test_axes_expose_one_position_each(void)
{
    protocore_umati_browse(0, 85, g_ref, REF_MAX);
    uint32_t root = g_ref[0].target_id;
    browse(root, 4);
    uint32_t mon = g_ref[1].target_id;
    browse(mon, 6);
    uint32_t ax[3] = {g_ref[3].target_id, g_ref[4].target_id, g_ref[5].target_id};

    static const double WANT[3] = {10.25, -3.5, 0.125};
    for (int i = 0; i < 3; i++)
    {
        browse(ax[i], 1);
        TEST_ASSERT_EQUAL_STRING("ActualPosition", g_ref[0].browse_name);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_VARIABLE, g_ref[0].node_class);
        OpcUaVariant p = read_ok(g_ref[0].target_id);
        TEST_ASSERT_EQUAL_INT(OPCUA_VAR_DOUBLE, p.type);
        assert_f64(WANT[i], p.f64);
    }
}

// Production and Notification.
void test_production_and_notification(void)
{
    protocore_umati_browse(0, 85, g_ref, REF_MAX);
    uint32_t root = g_ref[0].target_id;
    browse(root, 4);
    uint32_t prod = g_ref[2].target_id;
    uint32_t notif = g_ref[3].target_id;

    browse(prod, 2);
    TEST_ASSERT_EQUAL_STRING("ActiveProgram", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("ProducedPartCount", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("O1234.NC", read_ok(g_ref[0].target_id).str);
    OpcUaVariant parts = read_ok(g_ref[1].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_UINT32, parts.type);
    TEST_ASSERT_EQUAL_UINT32(77u, parts.u32);

    browse(notif, 2);
    TEST_ASSERT_EQUAL_STRING("ActiveMessage", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Severity", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("Tool life low", read_ok(g_ref[0].target_id).str);
    TEST_ASSERT_EQUAL_UINT32(500u, read_ok(g_ref[1].target_id).u32);
}

// A null model string is served as an empty String, never as a null pointer a client would deref.
void test_null_strings_read_as_empty(void)
{
    g_mt.ident.manufacturer = NULL;
    protocore_umati_browse(0, 85, g_ref, REF_MAX);
    uint32_t root = g_ref[0].target_id;
    browse(root, 4);
    uint32_t ident = g_ref[0].target_id;
    browse(ident, 6);

    OpcUaVariant v = read_ok(g_ref[0].target_id);
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_NOT_NULL(v.str);
    TEST_ASSERT_EQUAL_STRING("", v.str);
    TEST_ASSERT_EQUAL_INT32(0, v.str_len);
}

// A Read outside the model, in another namespace, or of another attribute is a clean miss: the
// server answers BadNodeIdUnknown rather than inventing a value.
void test_reads_outside_the_model_are_refused(void)
{
    OpcUaVariant v;
    protocore_umati_browse(0, 85, g_ref, REF_MAX);
    uint32_t root = g_ref[0].target_id;

    TEST_ASSERT_FALSE(protocore_umati_read(PROTOCORE_UMATI_NS, 1u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(protocore_umati_read(PROTOCORE_UMATI_NS, root, OPCUA_ATTR_VALUE, &v)); // an Object, not a value
    TEST_ASSERT_FALSE(protocore_umati_read((uint16_t)(PROTOCORE_UMATI_NS + 1), root + 101u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(protocore_umati_read(PROTOCORE_UMATI_NS, root + 101u, OPCUA_ATTR_VALUE + 1u, &v));

    // a leaf Variable has no children, and an unknown node is not in the model
    TEST_ASSERT_EQUAL_INT32(-1, protocore_umati_browse(PROTOCORE_UMATI_NS, root + 101u, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_umati_browse(PROTOCORE_UMATI_NS, 1u, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_umati_browse((uint16_t)(PROTOCORE_UMATI_NS + 1), root, g_ref, REF_MAX));
}

// Before a model is bound there is nothing to serve, including from the Objects folder.
void test_nothing_is_served_before_bind(void)
{
    OpcUaVariant v;
    protocore_umati_browse(0, 85, g_ref, REF_MAX);
    uint32_t root = g_ref[0].target_id;

    protocore_umati_bind(NULL);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_umati_browse(0, 85, g_ref, REF_MAX));
    TEST_ASSERT_EQUAL_INT32(-1, protocore_umati_browse(PROTOCORE_UMATI_NS, root, g_ref, REF_MAX));
    TEST_ASSERT_FALSE(protocore_umati_read(PROTOCORE_UMATI_NS, root + 101u, OPCUA_ATTR_VALUE, &v));
}

// A Browse never writes past the caller's array: a max below the child count truncates.
void test_browse_respects_the_reference_cap(void)
{
    protocore_umati_browse(0, 85, g_ref, REF_MAX);
    uint32_t root = g_ref[0].target_id;

    TEST_ASSERT_EQUAL_INT32(2, protocore_umati_browse(PROTOCORE_UMATI_NS, root, g_ref, 2));
    TEST_ASSERT_EQUAL_STRING("Identification", g_ref[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Monitoring", g_ref[1].browse_name);
    TEST_ASSERT_EQUAL_INT32(0, protocore_umati_browse(PROTOCORE_UMATI_NS, root, g_ref, 0));
}

// The whole model is reachable from ns0 i=85 with no dead reference: every Object browsed here
// yields children, and every Variable reads.
void test_every_reference_resolves(void)
{
    uint32_t queue[64];
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t variables = 0;

    TEST_ASSERT_EQUAL_INT32(1, protocore_umati_browse(0, 85, g_ref, REF_MAX));
    queue[tail++] = g_ref[0].target_id;

    while (head < tail)
    {
        uint32_t id = queue[head++];
        int32_t n = protocore_umati_browse(PROTOCORE_UMATI_NS, id, g_ref, REF_MAX);
        TEST_ASSERT_TRUE(n > 0);
        OpcUaReference kids[REF_MAX];
        memcpy(kids, g_ref, sizeof(kids));
        for (int32_t i = 0; i < n; i++)
        {
            if (kids[i].node_class == OPCUA_NODECLASS_VARIABLE)
            {
                OpcUaVariant v;
                TEST_ASSERT_TRUE_MESSAGE(
                    protocore_umati_read(PROTOCORE_UMATI_NS, kids[i].target_id, OPCUA_ATTR_VALUE, &v),
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
    // 6 Identification + 2 Monitoring/MachineTool + 4 Channel + 3 Spindle + 3 axes + 2 Production
    // + 2 Notification = 22
    TEST_ASSERT_EQUAL_UINT32(22u, variables);
}
