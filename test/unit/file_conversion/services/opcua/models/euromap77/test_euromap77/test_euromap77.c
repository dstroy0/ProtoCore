// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the EUROMAP 77 / OPC 40077 IMM_MES_Interface model
// (services/opcua/models/euromap77/euromap77.h).
//
// Every model expectation here is read out of the companion specification's own NodeSet, downloaded
// in this session from OPCFoundation/UA-Nodeset:
//   PlasticsRubber/IMM2MES/1.01/Opc.Ua.PlasticsRubber.IMM2MES.NodeSet2.xml
//     (Model ModelUri="http://opcfoundation.org/UA/PlasticsRubber/IMM2MES/" Version="1.01"
//      PublicationDate="2020-06-01", line 38; IMM_MES_InterfaceType ns=1;i=1007, line 861)
//   PlasticsRubber/GeneralTypes/1.02/Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.xml
//     (Model ModelUri="http://opcfoundation.org/UA/PlasticsRubber/GeneralTypes/" Version="1.02", line 37)
//   Schema/NodeIds.csv for the ns0 registry values used below (ObjectsFolder 85, Organizes 35,
//     HasProperty 46, HasComponent 47, BaseObjectType 58, BaseDataVariableType 63, PropertyType 68,
//     and the DataType ids Boolean 1, Int32 6, UInt32 7, UInt64 9, Double 11, String 12,
//     LocalizedText 21, Enumeration 29, Duration 290)
// plus OPC UA Part 3 sec 8.13 ("This Simple DataType is a Double that defines an interval of time in
// milliseconds") and Part 6 sec 5.2.4 ("Enumerations are encoded as Int32 values"), both quoted from
// reference.opcfoundation.org in this session.
//
// Two of these cases were written red and named a defect each; both are fixed, and the cases now
// hold the source to the NodeSet:
//
//   test_the_namespace_uris_are_the_published_model_uris - the two URIs were
//   http://www.euromap.org/euromap77/ and .../euromap83/. The namespaces moved to the OPC Foundation
//   when the recommendation was published as OPC 40077 / OPC 40083; the NodeSet ModelUri values
//   above are the ones a client resolves, and a grep for "euromap" over both NodeSets returns 0
//   hits. A server advertising the old URI lands in a different namespace index, so a conforming
//   client never matches the model.
//
//   test_each_leaf_carries_the_reference_type_the_nodeset_publishes - the NodeSet models the
//   MachineInformation, MachineStatus and ActiveJob leaves, and ActiveJobValues.JobStatus, as
//   HasProperty (46) references to PropertyType (68) nodes; only the other seven ActiveJobValues
//   leaves are HasComponent (47) / BaseDataVariableType (63). Every leaf used to be emitted as
//   HasComponent / BaseDataVariableType, so a client browsing HasProperty found nothing.
//
// Three published facts are NOT asserted, because the shared Variant cannot carry them:
// MachineInformation.Manufacturer (ns=1;i=6004) and .Model (ns=1;i=6005) are LocalizedText and
// ActiveJob.Material (i=6039, ValueRank 1) and .ProductName (i=6088, ValueRank 1) are String arrays,
// while opcua.h:303-311 defines scalar built-ins only, with no LocalizedText and no array form.
// euromap77.h:95 discloses the first ("all String"). Separately, euromap77.h:123 describes
// ExpectedCycleTime as "Duration, seconds -> Double", where Part 3 sec 8.13 makes a Duration
// milliseconds; the module only carries the caller's double, so no case can see the unit.
//
// The remaining cases are properties of the resolver - the value a Read returns is the one bound,
// the full published width survives, a node outside the model is refused, a Browse stops at the
// caller's bound, and an unbound model serves nothing.

// The .c, so the Read and Browse resolvers are reachable: they answer the OPC UA server's handler
// contract (Part 4 sec 5.9.2 / sec 5.11.2), so they are internal to the module and the env does not
// build this .c itself.
#include "services/opcua/opcua.h"
#include "services/opcua/models/euromap77/euromap77.c"

#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// OPC UA Schema/NodeIds.csv.
#define UA_OBJECTS_FOLDER 85u
#define UA_ORGANIZES 35u
#define UA_HAS_PROPERTY 46u
#define UA_HAS_COMPONENT 47u
#define UA_BASE_OBJECT_TYPE 58u
#define UA_BASE_DATA_VARIABLE_TYPE 63u
#define UA_PROPERTY_TYPE 68u

// A DataType the scalar Variant cannot carry (LocalizedText, or a String array).
#define NOT_A_SCALAR (-1)

static EmImm g_imm;
static OpcUaReference g_refs[16];

static void bind_machine(void)
{
    memset(&g_imm, 0, sizeof(g_imm));
    g_imm.name = "IMM_MES_Interface";
    g_imm.info.manufacturer = "ACME Machines";
    g_imm.info.model = "IMM-250";
    g_imm.info.serial_number = "SN-0001";
    g_imm.info.product_code = "PC-42";
    g_imm.info.hardware_revision = "HW1";
    g_imm.info.software_revision = "SW2";
    g_imm.info.device_revision = "DR3";
    g_imm.info.manufacturer_uri = "http://acme.example/";
    g_imm.status.is_present = PROTO_TRUE;
    g_imm.status.machine_mode = EM_MODE_AUTOMATIC;
    g_imm.active_job.job_name = "JOB-7";
    g_imm.active_job.job_description = "front bezel";
    g_imm.active_job.material = "PA66-GF30";
    g_imm.active_job.product_name = "bezel";
    g_imm.active_job.mould_id = "M-19";
    g_imm.active_job.expected_cycle_time = 12500.0;
    g_imm.active_job.num_cavities = 4;
    g_imm.active_job.nominal_parts = 100000;
    g_imm.active_job_values.job_cycle_counter = 1234;
    g_imm.active_job_values.machine_cycle_counter = 9876543210ull;
    g_imm.active_job_values.last_cycle_time = 12250.0;
    g_imm.active_job_values.average_cycle_time = 12750.0;
    g_imm.active_job_values.job_parts_counter = 4936;
    g_imm.active_job_values.job_good_parts_counter = 4900;
    g_imm.active_job_values.job_bad_parts_counter = 36;
    g_imm.active_job_values.job_status = EM_JOB_IN_PRODUCTION;
    Euromap77.bind_args.imm = &g_imm;
    Euromap77.bind(protocore_euromap77_span());
}

static const OpcUaReference *child(const OpcUaReference *refs, int32_t n, const char *name)
{
    for (int32_t i = 0; i < n; i++)
    {
        if (refs[i].browse_name && strcmp(refs[i].browse_name, name) == 0)
        {
            return &refs[i];
        }
    }
    return NULL;
}

static int32_t browse(uint16_t ns, uint32_t id)
{
    return em77_browse(ns, id, g_refs, (uint32_t)(sizeof(g_refs) / sizeof(g_refs[0])));
}

static uint32_t interface_id(void)
{
    const int32_t n = browse(0, UA_OBJECTS_FOLDER);
    TEST_ASSERT_EQUAL_INT32(1, n);
    return g_refs[0].target_id;
}

// Walk BrowseNames from the interface down and return the node the path names.
static uint32_t node_at(const char *a, const char *b)
{
    uint32_t id = interface_id();
    const char *const path[2] = {a, b};
    for (size_t i = 0; i < 2 && path[i]; i++)
    {
        const int32_t n = browse(Euromap77.ns, id);
        const OpcUaReference *r = child(g_refs, n, path[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, path[i]);
        id = r->target_id;
    }
    return id;
}

static proto_bool read_value(uint32_t id, OpcUaVariant *out)
{
    memset(out, 0, sizeof(*out));
    return em77_read(Euromap77.ns, id, OPCUA_ATTR_VALUE, out);
}

// One Variable of the IMM_MES_Interface as OPC 40077 publishes it: where it hangs, its BrowseName,
// the Variant its published DataType requires, and the ReferenceType + TypeDefinition the NodeSet
// gives the reference that reaches it.
struct PublishedLeaf
{
    const char *container;
    const char *sub;
    const char *name;
    int var_type;
    uint32_t ref_type;
    uint32_t type_def;
};

// MachineInformation (ns=1;i=5001, imm.xml:2418) reaches all eight by HasProperty; Manufacturer
// i=6004 and Model i=6005 are LocalizedText, the other six String.
// MachineStatus (i=5006, :2979) reaches IsPresent i=6007 (Boolean) and MachineMode i=6008
// (MachineModeEnumeration) by HasProperty.
// ActiveJob (i=5002, :917) reaches all eight by HasProperty: ExpectedCycleTime i=6175 Duration,
// NominalParts i=6003 UInt64, NumCavities i=6189 UInt32, MouldId/JobName/JobDescription String,
// Material i=6039 and ProductName i=6088 String arrays.
// ActiveJobValues (i=5013, :1371) reaches seven by HasComponent - the five UInt64 counters and the
// two Duration times - and JobStatus i=6134 by HasProperty.
static const struct PublishedLeaf LEAF[] = {
    {"MachineInformation", NULL, "Manufacturer", NOT_A_SCALAR, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"MachineInformation", NULL, "Model", NOT_A_SCALAR, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"MachineInformation", NULL, "SerialNumber", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"MachineInformation", NULL, "ProductCode", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"MachineInformation", NULL, "HardwareRevision", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"MachineInformation", NULL, "SoftwareRevision", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"MachineInformation", NULL, "DeviceRevision", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"MachineInformation", NULL, "ManufacturerUri", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},

    {"MachineStatus", NULL, "IsPresent", OPCUA_VAR_BOOL, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"MachineStatus", NULL, "MachineMode", OPCUA_VAR_INT32, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},

    {"Jobs", "ActiveJob", "JobName", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"Jobs", "ActiveJob", "JobDescription", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"Jobs", "ActiveJob", "Material", NOT_A_SCALAR, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"Jobs", "ActiveJob", "ProductName", NOT_A_SCALAR, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"Jobs", "ActiveJob", "MouldId", OPCUA_VAR_STRING, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"Jobs", "ActiveJob", "ExpectedCycleTime", OPCUA_VAR_DOUBLE, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"Jobs", "ActiveJob", "NumCavities", OPCUA_VAR_UINT32, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
    {"Jobs", "ActiveJob", "NominalParts", OPCUA_VAR_UINT64, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},

    {"Jobs", "ActiveJobValues", "JobCycleCounter", OPCUA_VAR_UINT64, UA_HAS_COMPONENT, UA_BASE_DATA_VARIABLE_TYPE},
    {"Jobs", "ActiveJobValues", "MachineCycleCounter", OPCUA_VAR_UINT64, UA_HAS_COMPONENT, UA_BASE_DATA_VARIABLE_TYPE},
    {"Jobs", "ActiveJobValues", "LastCycleTime", OPCUA_VAR_DOUBLE, UA_HAS_COMPONENT, UA_BASE_DATA_VARIABLE_TYPE},
    {"Jobs", "ActiveJobValues", "AverageCycleTime", OPCUA_VAR_DOUBLE, UA_HAS_COMPONENT, UA_BASE_DATA_VARIABLE_TYPE},
    {"Jobs", "ActiveJobValues", "JobPartsCounter", OPCUA_VAR_UINT64, UA_HAS_COMPONENT, UA_BASE_DATA_VARIABLE_TYPE},
    {"Jobs", "ActiveJobValues", "JobGoodPartsCounter", OPCUA_VAR_UINT64, UA_HAS_COMPONENT, UA_BASE_DATA_VARIABLE_TYPE},
    {"Jobs", "ActiveJobValues", "JobBadPartsCounter", OPCUA_VAR_UINT64, UA_HAS_COMPONENT, UA_BASE_DATA_VARIABLE_TYPE},
    {"Jobs", "ActiveJobValues", "JobStatus", OPCUA_VAR_INT32, UA_HAS_PROPERTY, UA_PROPERTY_TYPE},
};
#define LEAF_N (sizeof(LEAF) / sizeof(LEAF[0]))

static const OpcUaReference *leaf_ref(const struct PublishedLeaf *l)
{
    const int32_t n = browse(Euromap77.ns, node_at(l->container, l->sub));
    const OpcUaReference *r = child(g_refs, n, l->name);
    TEST_ASSERT_NOT_NULL_MESSAGE(r, l->name);
    return r;
}

// The namespace URI is what a client resolves the model by, and the NodeSets publish these two
// ModelUri values; neither NodeSet mentions euromap.org at all.
void test_the_namespace_uris_are_the_published_model_uris(void)
{
    TEST_ASSERT_EQUAL_STRING("http://opcfoundation.org/UA/PlasticsRubber/IMM2MES/", EUROMAP77_NS_URI);
    TEST_ASSERT_EQUAL_STRING("http://opcfoundation.org/UA/PlasticsRubber/GeneralTypes/", EUROMAP83_NS_URI);
}

// The two URIs name different models, so nothing may collapse them into one.
void test_the_two_namespace_uris_differ(void)
{
    TEST_ASSERT_TRUE(strcmp(EUROMAP77_NS_URI, EUROMAP83_NS_URI) != 0);
}

// GeneralTypes MachineModeEnumeration (ns=1;i=3011, gt.xml:1162): the Definition publishes six
// Fields - OTHER 0, AUTOMATIC 1, SEMI_AUTOMATIC 2, MANUAL 3, SETUP 4, SLEEP 5 - and its EnumValues
// Property (i=6181) carries ArrayDimensions="6", so five is the largest value the type defines.
void test_the_machine_mode_enumeration_is_the_published_one(void)
{
    TEST_ASSERT_EQUAL_INT(0, EM_MODE_OTHER);
    TEST_ASSERT_EQUAL_INT(1, EM_MODE_AUTOMATIC);
    TEST_ASSERT_EQUAL_INT(2, EM_MODE_SEMI_AUTOMATIC);
    TEST_ASSERT_EQUAL_INT(3, EM_MODE_MANUAL);
    TEST_ASSERT_EQUAL_INT(4, EM_MODE_SETUP);
    TEST_ASSERT_EQUAL_INT(5, EM_MODE_SLEEP);
}

// GeneralTypes JobStatusEnumeration (ns=1;i=3017, gt.xml:681): twelve Fields - OTHER 0,
// TRANSFERRED_ASSIGNED 1, SET_UP_ACTIVE 2, SET_UP_INTERRUPTED 3, SET_UP_FINISHED 4,
// START_UP_ACTIVE 5, JOB_IN_PRODUCTION 6, JOB_INTERRUPTED 7, JOB_FINISHED 8, TEAR_DOWN_ACTIVE 9,
// TEAR_DOWN_INTERRUPTED 10, TEAR_DOWN_FINISHED 11 - and EnumValues (i=6264) ArrayDimensions="12".
void test_the_job_status_enumeration_is_the_published_one(void)
{
    TEST_ASSERT_EQUAL_INT(0, EM_JOB_OTHER);
    TEST_ASSERT_EQUAL_INT(1, EM_JOB_TRANSFERRED_ASSIGNED);
    TEST_ASSERT_EQUAL_INT(2, EM_JOB_SET_UP_ACTIVE);
    TEST_ASSERT_EQUAL_INT(3, EM_JOB_SET_UP_INTERRUPTED);
    TEST_ASSERT_EQUAL_INT(4, EM_JOB_SET_UP_FINISHED);
    TEST_ASSERT_EQUAL_INT(5, EM_JOB_START_UP_ACTIVE);
    TEST_ASSERT_EQUAL_INT(6, EM_JOB_IN_PRODUCTION);
    TEST_ASSERT_EQUAL_INT(7, EM_JOB_INTERRUPTED);
    TEST_ASSERT_EQUAL_INT(8, EM_JOB_FINISHED);
    TEST_ASSERT_EQUAL_INT(9, EM_JOB_TEAR_DOWN_ACTIVE);
    TEST_ASSERT_EQUAL_INT(10, EM_JOB_TEAR_DOWN_INTERRUPTED);
    TEST_ASSERT_EQUAL_INT(11, EM_JOB_TEAR_DOWN_FINISHED);
}

// IMM_MES_InterfaceType (i=1007) reaches MachineInformation, MachineStatus and Jobs by HasComponent,
// and Jobs reaches ActiveJob (i=5002) and ActiveJobValues (i=5013) the same way. Every BrowseName
// below is the one the NodeSet spells; the counts are the subset euromap77.h:21-31 documents.
void test_the_browse_hierarchy_carries_the_published_browsenames(void)
{
    bind_machine();

    int32_t n = browse(Euromap77.ns, interface_id());
    TEST_ASSERT_EQUAL_INT32(3, n);
    TEST_ASSERT_NOT_NULL(child(g_refs, n, "MachineInformation"));
    TEST_ASSERT_NOT_NULL(child(g_refs, n, "MachineStatus"));
    TEST_ASSERT_NOT_NULL(child(g_refs, n, "Jobs"));

    n = browse(Euromap77.ns, node_at("Jobs", NULL));
    TEST_ASSERT_EQUAL_INT32(2, n);
    TEST_ASSERT_NOT_NULL(child(g_refs, n, "ActiveJob"));
    TEST_ASSERT_NOT_NULL(child(g_refs, n, "ActiveJobValues"));

    TEST_ASSERT_EQUAL_INT32(8, browse(Euromap77.ns, node_at("MachineInformation", NULL)));
    TEST_ASSERT_EQUAL_INT32(2, browse(Euromap77.ns, node_at("MachineStatus", NULL)));
    TEST_ASSERT_EQUAL_INT32(8, browse(Euromap77.ns, node_at("Jobs", "ActiveJob")));
    TEST_ASSERT_EQUAL_INT32(8, browse(Euromap77.ns, node_at("Jobs", "ActiveJobValues")));

    for (size_t i = 0; i < LEAF_N; i++)
    {
        (void)leaf_ref(&LEAF[i]);
    }
}

// The NodeSet declares the six containers UAObject and all 26 leaves UAVariable.
void test_the_containers_are_objects_and_the_leaves_are_variables(void)
{
    bind_machine();

    static const char *const CONTAINER[3] = {"MachineInformation", "MachineStatus", "Jobs"};
    const int32_t n = browse(Euromap77.ns, interface_id());
    for (size_t i = 0; i < 3; i++)
    {
        const OpcUaReference *r = child(g_refs, n, CONTAINER[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, CONTAINER[i]);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, r->node_class);
        TEST_ASSERT_EQUAL_UINT32(UA_HAS_COMPONENT, r->ref_type_id);
        TEST_ASSERT_EQUAL_UINT32(UA_BASE_OBJECT_TYPE, r->type_def_id);
    }

    const int32_t jn = browse(Euromap77.ns, node_at("Jobs", NULL));
    static const char *const JOB_CONTAINER[2] = {"ActiveJob", "ActiveJobValues"};
    for (size_t i = 0; i < 2; i++)
    {
        const OpcUaReference *r = child(g_refs, jn, JOB_CONTAINER[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, JOB_CONTAINER[i]);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, r->node_class);
        TEST_ASSERT_EQUAL_UINT32(UA_HAS_COMPONENT, r->ref_type_id);
    }

    for (size_t i = 0; i < LEAF_N; i++)
    {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(OPCUA_NODECLASS_VARIABLE, leaf_ref(&LEAF[i])->node_class, LEAF[i].name);
    }
}

// Per the NodeSet, 19 of the 26 leaves are HasProperty (46) references to PropertyType (68) nodes
// and only the seven HasComponent ones under ActiveJobValues are BaseDataVariableType (63).
void test_each_leaf_carries_the_reference_type_the_nodeset_publishes(void)
{
    bind_machine();
    for (size_t i = 0; i < LEAF_N; i++)
    {
        const OpcUaReference *r = leaf_ref(&LEAF[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(LEAF[i].ref_type, r->ref_type_id, LEAF[i].name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(LEAF[i].type_def, r->type_def_id, LEAF[i].name);
    }
}

// Each leaf's published DataType fixes the Variant it must carry: UInt64 for the five job counters
// and NominalParts, UInt32 for NumCavities, Double for the three Durations (Part 3 sec 8.13), Int32
// for the two enumerations (Part 6 sec 5.2.4), Boolean for IsPresent, String for the rest.
void test_each_leaf_serves_the_variant_its_published_datatype_requires(void)
{
    bind_machine();
    for (size_t i = 0; i < LEAF_N; i++)
    {
        if (LEAF[i].var_type == NOT_A_SCALAR)
        {
            continue;
        }
        OpcUaVariant v;
        TEST_ASSERT_TRUE_MESSAGE(read_value(leaf_ref(&LEAF[i])->target_id, &v), LEAF[i].name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(LEAF[i].var_type, v.type, LEAF[i].name);
    }
}

// Every Read returns what the bound model holds, for each leaf the model can carry.
void test_every_value_comes_from_the_bound_model(void)
{
    bind_machine();

    struct
    {
        const char *container;
        const char *sub;
        const char *name;
        const char *want;
    } static const STR[] = {
        {"MachineInformation", NULL, "Manufacturer", "ACME Machines"},
        {"MachineInformation", NULL, "Model", "IMM-250"},
        {"MachineInformation", NULL, "SerialNumber", "SN-0001"},
        {"MachineInformation", NULL, "ProductCode", "PC-42"},
        {"MachineInformation", NULL, "HardwareRevision", "HW1"},
        {"MachineInformation", NULL, "SoftwareRevision", "SW2"},
        {"MachineInformation", NULL, "DeviceRevision", "DR3"},
        {"MachineInformation", NULL, "ManufacturerUri", "http://acme.example/"},
        {"Jobs", "ActiveJob", "JobName", "JOB-7"},
        {"Jobs", "ActiveJob", "JobDescription", "front bezel"},
        {"Jobs", "ActiveJob", "Material", "PA66-GF30"},
        {"Jobs", "ActiveJob", "ProductName", "bezel"},
        {"Jobs", "ActiveJob", "MouldId", "M-19"},
    };
    for (size_t i = 0; i < sizeof(STR) / sizeof(STR[0]); i++)
    {
        const int32_t n = browse(Euromap77.ns, node_at(STR[i].container, STR[i].sub));
        const OpcUaReference *r = child(g_refs, n, STR[i].name);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, STR[i].name);
        OpcUaVariant v;
        TEST_ASSERT_TRUE_MESSAGE(read_value(r->target_id, &v), STR[i].name);
        TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)strlen(STR[i].want), v.str_len, STR[i].name);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(STR[i].want, v.str, (size_t)v.str_len, STR[i].name);
    }

    struct
    {
        const char *sub;
        const char *name;
        uint64_t want;
    } static const U64[] = {
        {"ActiveJob", "NominalParts", 100000ull},
        {"ActiveJobValues", "JobCycleCounter", 1234ull},
        {"ActiveJobValues", "MachineCycleCounter", 9876543210ull},
        {"ActiveJobValues", "JobPartsCounter", 4936ull},
        {"ActiveJobValues", "JobGoodPartsCounter", 4900ull},
        {"ActiveJobValues", "JobBadPartsCounter", 36ull},
    };
    for (size_t i = 0; i < sizeof(U64) / sizeof(U64[0]); i++)
    {
        const int32_t n = browse(Euromap77.ns, node_at("Jobs", U64[i].sub));
        const OpcUaReference *r = child(g_refs, n, U64[i].name);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, U64[i].name);
        OpcUaVariant v;
        TEST_ASSERT_TRUE_MESSAGE(read_value(r->target_id, &v), U64[i].name);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(U64[i].want, v.u64, U64[i].name);
    }

    struct
    {
        const char *sub;
        const char *name;
        double want;
    } static const F64[] = {
        {"ActiveJob", "ExpectedCycleTime", 12500.0},
        {"ActiveJobValues", "LastCycleTime", 12250.0},
        {"ActiveJobValues", "AverageCycleTime", 12750.0},
    };
    for (size_t i = 0; i < sizeof(F64) / sizeof(F64[0]); i++)
    {
        const int32_t n = browse(Euromap77.ns, node_at("Jobs", F64[i].sub));
        const OpcUaReference *r = child(g_refs, n, F64[i].name);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, F64[i].name);
        OpcUaVariant v;
        TEST_ASSERT_TRUE_MESSAGE(read_value(r->target_id, &v), F64[i].name);
        TEST_ASSERT_TRUE_MESSAGE(v.f64 == F64[i].want, F64[i].name); // carried, not converted
    }

    OpcUaVariant v;
    const int32_t sn = browse(Euromap77.ns, node_at("MachineStatus", NULL));
    TEST_ASSERT_TRUE(read_value(child(g_refs, sn, "MachineMode")->target_id, &v));
    TEST_ASSERT_EQUAL_INT32((int32_t)EM_MODE_AUTOMATIC, v.i32);
    TEST_ASSERT_TRUE(read_value(child(g_refs, sn, "IsPresent")->target_id, &v));
    TEST_ASSERT_TRUE(v.b);

    const int32_t an = browse(Euromap77.ns, node_at("Jobs", "ActiveJob"));
    TEST_ASSERT_TRUE(read_value(child(g_refs, an, "NumCavities")->target_id, &v));
    TEST_ASSERT_EQUAL_UINT32(4u, v.u32);

    const int32_t vn = browse(Euromap77.ns, node_at("Jobs", "ActiveJobValues"));
    TEST_ASSERT_TRUE(read_value(child(g_refs, vn, "JobStatus")->target_id, &v));
    TEST_ASSERT_EQUAL_INT32((int32_t)EM_JOB_IN_PRODUCTION, v.i32);
}

// The counters are UInt64, so every one of the 64 bits has to survive the read: 2^64-1 truncated to
// 32 bits reads 4294967295, and 2^32 reads 0.
void test_the_counters_keep_the_full_published_uint64_width(void)
{
    static const uint64_t WIDE[] = {0ull, 4294967295ull, 4294967296ull, 18446744073709551615ull};
    for (size_t w = 0; w < sizeof(WIDE) / sizeof(WIDE[0]); w++)
    {
        bind_machine();
        g_imm.active_job_values.machine_cycle_counter = WIDE[w];
        g_imm.active_job.nominal_parts = WIDE[w];

        OpcUaVariant v;
        const int32_t vn = browse(Euromap77.ns, node_at("Jobs", "ActiveJobValues"));
        TEST_ASSERT_TRUE(read_value(child(g_refs, vn, "MachineCycleCounter")->target_id, &v));
        TEST_ASSERT_EQUAL_UINT64(WIDE[w], v.u64);

        const int32_t an = browse(Euromap77.ns, node_at("Jobs", "ActiveJob"));
        TEST_ASSERT_TRUE(read_value(child(g_refs, an, "NominalParts")->target_id, &v));
        TEST_ASSERT_EQUAL_UINT64(WIDE[w], v.u64);
    }
}

// euromap77.h:142-145: the resolvers read straight out of the caller's struct, so a value changed
// after the bind is the value the next Read returns, with no rebind.
void test_a_read_follows_the_model_without_a_rebind(void)
{
    bind_machine();
    const int32_t vn = browse(Euromap77.ns, node_at("Jobs", "ActiveJobValues"));
    const uint32_t cycles = child(g_refs, vn, "JobCycleCounter")->target_id;

    OpcUaVariant v;
    TEST_ASSERT_TRUE(read_value(cycles, &v));
    TEST_ASSERT_EQUAL_UINT64(1234ull, v.u64);

    g_imm.active_job_values.job_cycle_counter = 1235ull;
    TEST_ASSERT_TRUE(read_value(cycles, &v));
    TEST_ASSERT_EQUAL_UINT64(1235ull, v.u64);
}

// euromap77.h:145: "String fields may be null (served as an empty String)" - an empty String, not a
// null pointer a caller would dereference.
void test_absent_strings_read_as_empty(void)
{
    bind_machine();
    g_imm.info.model = NULL;
    g_imm.active_job.job_name = NULL;

    const int32_t n = browse(Euromap77.ns, node_at("MachineInformation", NULL));
    OpcUaVariant v;
    TEST_ASSERT_TRUE(read_value(child(g_refs, n, "Model")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_INT32(0, v.str_len);
    TEST_ASSERT_NOT_NULL(v.str);

    const int32_t an = browse(Euromap77.ns, node_at("Jobs", "ActiveJob"));
    TEST_ASSERT_TRUE(read_value(child(g_refs, an, "JobName")->target_id, &v));
    TEST_ASSERT_EQUAL_INT32(0, v.str_len);
    TEST_ASSERT_NOT_NULL(v.str);
}

// euromap77.h:148: name is the "IMM_MES_Interface BrowseName / DisplayName", so the bound name is
// what a Browse of the Objects folder reports, and a null one falls back to the type's own name.
void test_the_interface_name_comes_from_the_bound_model(void)
{
    bind_machine();
    g_imm.name = "IMM-250 line 3";
    TEST_ASSERT_EQUAL_INT32(1, browse(0, UA_OBJECTS_FOLDER));
    TEST_ASSERT_EQUAL_STRING("IMM-250 line 3", g_refs[0].browse_name);
    TEST_ASSERT_EQUAL_STRING(g_refs[0].browse_name, g_refs[0].display_name);

    g_imm.name = NULL;
    TEST_ASSERT_EQUAL_INT32(1, browse(0, UA_OBJECTS_FOLDER));
    TEST_ASSERT_EQUAL_STRING("IMM_MES_Interface", g_refs[0].browse_name);
}

// The Objects folder is ns0 i=85 and Organizes is i=35: that one reference is how a client walking
// from the root reaches the interface at all.
void test_the_objects_folder_organizes_the_interface(void)
{
    bind_machine();
    TEST_ASSERT_EQUAL_INT32(1, browse(0, UA_OBJECTS_FOLDER));
    TEST_ASSERT_EQUAL_UINT32(UA_ORGANIZES, g_refs[0].ref_type_id);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_refs[0].node_class);
    TEST_ASSERT_TRUE(g_refs[0].is_forward);
    TEST_ASSERT_EQUAL_UINT(Euromap77.ns, g_refs[0].target_ns);
}

// Every reference the model hands out points forward into the model's own namespace, and names its
// target the same way twice.
void test_every_reference_is_forward_and_in_the_models_namespace(void)
{
    bind_machine();
    const uint32_t container[] = {
        interface_id(),        node_at("MachineInformation", NULL), node_at("MachineStatus", NULL),
        node_at("Jobs", NULL), node_at("Jobs", "ActiveJob"),        node_at("Jobs", "ActiveJobValues"),
    };
    for (size_t c = 0; c < sizeof(container) / sizeof(container[0]); c++)
    {
        const int32_t n = browse(Euromap77.ns, container[c]);
        TEST_ASSERT_TRUE(n > 0);
        for (int32_t i = 0; i < n; i++)
        {
            TEST_ASSERT_TRUE(g_refs[i].is_forward);
            TEST_ASSERT_EQUAL_UINT(Euromap77.ns, g_refs[i].target_ns);
            TEST_ASSERT_EQUAL_UINT(Euromap77.ns, g_refs[i].browse_name_ns);
            TEST_ASSERT_EQUAL_STRING(g_refs[i].browse_name, g_refs[i].display_name);
        }
    }
}

// euromap77.h:161-166: a node outside the model returns false, so the server answers
// BadNodeIdUnknown instead of a value that belongs to something else.
void test_reads_outside_the_model_are_refused(void)
{
    bind_machine();
    const uint32_t root = interface_id();
    const int32_t n = browse(Euromap77.ns, node_at("MachineInformation", NULL));
    const uint32_t manufacturer = child(g_refs, n, "Manufacturer")->target_id;
    OpcUaVariant v;

    TEST_ASSERT_FALSE(em77_read(Euromap77.ns, 1u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(em77_read(Euromap77.ns, root, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(em77_read((uint16_t)(Euromap77.ns + 1u), manufacturer, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(em77_read(Euromap77.ns, manufacturer, OPCUA_ATTR_VALUE + 1u, &v));
}

// euromap77.h:168-174: a node outside the model browses to -1, which is not the 0 a childless node
// in the model would report.
void test_browse_outside_the_model_is_refused(void)
{
    bind_machine();
    const uint32_t root = interface_id();
    const int32_t n = browse(Euromap77.ns, node_at("MachineInformation", NULL));
    const uint32_t manufacturer = child(g_refs, n, "Manufacturer")->target_id;

    TEST_ASSERT_EQUAL_INT32(-1, browse(Euromap77.ns, manufacturer));
    TEST_ASSERT_EQUAL_INT32(-1, browse(Euromap77.ns, 1u));
    TEST_ASSERT_EQUAL_INT32(-1, browse((uint16_t)(Euromap77.ns + 1u), root));
}

// euromap77.c:64-66: a Read or Browse before the bind is a clean miss, so the server never
// dereferences a model that is not there.
void test_an_unbound_model_serves_nothing(void)
{
    Euromap77.bind_args.imm = NULL;
    Euromap77.bind(protocore_euromap77_span());
    OpcUaVariant v;
    TEST_ASSERT_FALSE(em77_read(Euromap77.ns, 7101u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_INT32(-1, browse(0, UA_OBJECTS_FOLDER));
    TEST_ASSERT_EQUAL_INT32(-1, browse(Euromap77.ns, 7000u));
    bind_machine();
}

// The caller's array bound is the only thing that decides how much is written: a short one truncates
// to that many references and a zero one writes none.
void test_browse_respects_the_caller_bound(void)
{
    bind_machine();
    const uint32_t info = node_at("MachineInformation", NULL);

    OpcUaReference few[3];
    memset(few, 0, sizeof(few));
    const int32_t n = em77_browse(Euromap77.ns, info, few, 3u);
    TEST_ASSERT_EQUAL_INT32(3, n);
    for (int32_t i = 0; i < n; i++)
    {
        TEST_ASSERT_NOT_NULL(few[i].browse_name);
    }

    OpcUaReference one[1];
    memset(one, 0, sizeof(one));
    TEST_ASSERT_EQUAL_INT32(0, em77_browse(Euromap77.ns, info, one, 0u));
    TEST_ASSERT_NULL(one[0].browse_name);
}
