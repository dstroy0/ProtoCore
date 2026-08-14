// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/machine_tool/euromap77/euromap77.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define OBJECTS_FOLDER_ID 85

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
    g_imm.active_job.expected_cycle_time = 12.5;
    g_imm.active_job.num_cavities = 4;
    g_imm.active_job.nominal_parts = 100000;
    g_imm.active_job_values.job_cycle_counter = 1234;
    g_imm.active_job_values.machine_cycle_counter = 9876543210ull;
    g_imm.active_job_values.last_cycle_time = 12.25;
    g_imm.active_job_values.average_cycle_time = 12.75;
    g_imm.active_job_values.job_parts_counter = 4936;
    g_imm.active_job_values.job_good_parts_counter = 4900;
    g_imm.active_job_values.job_bad_parts_counter = 36;
    g_imm.active_job_values.job_status = EM_JOB_IN_PRODUCTION;
    protocore_em77_bind(&g_imm);
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
    return protocore_em77_browse(ns, id, g_refs, (uint32_t)(sizeof(g_refs) / sizeof(g_refs[0])));
}

static uint32_t node_at(const char *a, const char *b, const char *c)
{
    int32_t n = browse(0, OBJECTS_FOLDER_ID);
    TEST_ASSERT_EQUAL_INT32(1, n);
    uint32_t id = g_refs[0].target_id;
    const char *const path[3] = {a, b, c};
    for (size_t i = 0; i < 3 && path[i]; i++)
    {
        n = browse(PROTOCORE_EM77_NS, id);
        const OpcUaReference *r = child(g_refs, n, path[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, path[i]);
        id = r->target_id;
    }
    return id;
}

static proto_bool read_value(uint32_t id, OpcUaVariant *out)
{
    memset(out, 0, sizeof(*out));
    return protocore_em77_read(PROTOCORE_EM77_NS, id, OPCUA_ATTR_VALUE, out);
}

void test_browse_hierarchy(void)
{
    bind_machine();

    int32_t n = browse(0, OBJECTS_FOLDER_ID);
    TEST_ASSERT_EQUAL_INT32(1, n);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, g_refs[0].ref_type_id);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, g_refs[0].node_class);
    TEST_ASSERT_EQUAL_STRING("IMM_MES_Interface", g_refs[0].browse_name);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_EM77_NS, g_refs[0].target_ns);
    const uint32_t interface_id = g_refs[0].target_id;

    n = browse(PROTOCORE_EM77_NS, interface_id);
    TEST_ASSERT_EQUAL_INT32(3, n);
    static const char *const TOP[] = {"MachineInformation", "MachineStatus", "Jobs"};
    uint32_t top_id[3];
    for (size_t i = 0; i < 3; i++)
    {
        const OpcUaReference *r = child(g_refs, n, TOP[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, TOP[i]);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, r->node_class);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_HAS_COMPONENT, r->ref_type_id);
        top_id[i] = r->target_id;
    }

    static const char *const INFO[] = {"Manufacturer",     "Model",            "SerialNumber", "ProductCode",
                                       "HardwareRevision", "SoftwareRevision", "DeviceRevision", "ManufacturerUri"};
    n = browse(PROTOCORE_EM77_NS, top_id[0]);
    TEST_ASSERT_EQUAL_INT32(8, n);
    for (size_t i = 0; i < 8; i++)
    {
        const OpcUaReference *r = child(g_refs, n, INFO[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, INFO[i]);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_VARIABLE, r->node_class);
        TEST_ASSERT_EQUAL_UINT32(OPCUA_TYPEDEF_BASE_DATA_VARIABLE, r->type_def_id);
    }

    n = browse(PROTOCORE_EM77_NS, top_id[1]);
    TEST_ASSERT_EQUAL_INT32(2, n);
    TEST_ASSERT_NOT_NULL(child(g_refs, n, "IsPresent"));
    TEST_ASSERT_NOT_NULL(child(g_refs, n, "MachineMode"));

    n = browse(PROTOCORE_EM77_NS, top_id[2]);
    TEST_ASSERT_EQUAL_INT32(2, n);
    const OpcUaReference *aj = child(g_refs, n, "ActiveJob");
    const OpcUaReference *ajv = child(g_refs, n, "ActiveJobValues");
    TEST_ASSERT_NOT_NULL(aj);
    TEST_ASSERT_NOT_NULL(ajv);
    const uint32_t active_job = aj->target_id;
    const uint32_t active_job_values = ajv->target_id;

    static const char *const JOB[] = {"JobName",  "JobDescription",    "Material",    "ProductName",
                                      "MouldId",  "ExpectedCycleTime", "NumCavities", "NominalParts"};
    n = browse(PROTOCORE_EM77_NS, active_job);
    TEST_ASSERT_EQUAL_INT32(8, n);
    for (size_t i = 0; i < 8; i++)
    {
        TEST_ASSERT_NOT_NULL_MESSAGE(child(g_refs, n, JOB[i]), JOB[i]);
    }

    static const char *const VALUES[] = {"JobCycleCounter",     "MachineCycleCounter", "LastCycleTime",
                                         "AverageCycleTime",    "JobPartsCounter",     "JobGoodPartsCounter",
                                         "JobBadPartsCounter",  "JobStatus"};
    n = browse(PROTOCORE_EM77_NS, active_job_values);
    TEST_ASSERT_EQUAL_INT32(8, n);
    for (size_t i = 0; i < 8; i++)
    {
        TEST_ASSERT_NOT_NULL_MESSAGE(child(g_refs, n, VALUES[i]), VALUES[i]);
    }
}

void test_namespace_uris(void)
{
    TEST_ASSERT_EQUAL_STRING("http://www.euromap.org/euromap77/", EUROMAP77_NS_URI);
    TEST_ASSERT_EQUAL_STRING("http://www.euromap.org/euromap83/", EUROMAP83_NS_URI);
}

void test_interface_name_comes_from_the_model(void)
{
    bind_machine();
    g_imm.name = "IMM-250 line 3";
    int32_t n = browse(0, OBJECTS_FOLDER_ID);
    TEST_ASSERT_EQUAL_INT32(1, n);
    TEST_ASSERT_EQUAL_STRING("IMM-250 line 3", g_refs[0].browse_name);
    TEST_ASSERT_EQUAL_STRING(g_refs[0].browse_name, g_refs[0].display_name);

    g_imm.name = NULL;
    n = browse(0, OBJECTS_FOLDER_ID);
    TEST_ASSERT_EQUAL_STRING("IMM_MES_Interface", g_refs[0].browse_name);
}

void test_machine_information_values(void)
{
    bind_machine();
    const int32_t n = browse(PROTOCORE_EM77_NS, node_at("MachineInformation", NULL, NULL));
    TEST_ASSERT_EQUAL_INT32(8, n);
    static const char *const NAME[] = {"Manufacturer",     "Model",            "SerialNumber",   "ProductCode",
                                       "HardwareRevision", "SoftwareRevision", "DeviceRevision", "ManufacturerUri"};
    static const char *const WANT[] = {"ACME Machines", "IMM-250", "SN-0001", "PC-42",
                                       "HW1",           "SW2",     "DR3",     "http://acme.example/"};
    for (size_t i = 0; i < 8; i++)
    {
        const OpcUaReference *r = child(g_refs, n, NAME[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, NAME[i]);
        OpcUaVariant v;
        TEST_ASSERT_TRUE_MESSAGE(read_value(r->target_id, &v), NAME[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(OPCUA_VAR_STRING, v.type, NAME[i]);
        TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)strlen(WANT[i]), v.str_len, NAME[i]);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(WANT[i], v.str, (size_t)v.str_len, NAME[i]);
    }
}

void test_absent_strings_read_as_empty(void)
{
    bind_machine();
    g_imm.info.model = NULL;
    const int32_t n = browse(PROTOCORE_EM77_NS, node_at("MachineInformation", NULL, NULL));

    OpcUaVariant v;
    TEST_ASSERT_TRUE(read_value(child(g_refs, n, "Model")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_INT32(0, v.str_len);
    TEST_ASSERT_NOT_NULL(v.str);
}

void test_counter_variants_are_uint64(void)
{
    bind_machine();
    const uint32_t active_job = node_at("Jobs", "ActiveJob", NULL);
    const uint32_t values = node_at("Jobs", "ActiveJobValues", NULL);

    const int32_t vn = browse(PROTOCORE_EM77_NS, values);
    struct
    {
        const char *name;
        uint64_t want;
    } static const U64[] = {
        {"JobCycleCounter", 1234u},   {"MachineCycleCounter", 9876543210ull},
        {"JobPartsCounter", 4936u},   {"JobGoodPartsCounter", 4900u},
        {"JobBadPartsCounter", 36u},
    };
    for (size_t i = 0; i < sizeof(U64) / sizeof(U64[0]); i++)
    {
        const OpcUaReference *r = child(g_refs, vn, U64[i].name);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, U64[i].name);
        OpcUaVariant v;
        TEST_ASSERT_TRUE_MESSAGE(read_value(r->target_id, &v), U64[i].name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(OPCUA_VAR_UINT64, v.type, U64[i].name);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(U64[i].want, v.u64, U64[i].name);
    }

    OpcUaVariant v;
    TEST_ASSERT_TRUE(read_value(child(g_refs, vn, "LastCycleTime")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_DOUBLE, v.type);
    TEST_ASSERT_EQUAL_FLOAT(12.25f, (float)v.f64);

    TEST_ASSERT_TRUE(read_value(child(g_refs, vn, "AverageCycleTime")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_DOUBLE, v.type);
    TEST_ASSERT_EQUAL_FLOAT(12.75f, (float)v.f64);

    const int32_t an = browse(PROTOCORE_EM77_NS, active_job);
    TEST_ASSERT_TRUE(read_value(child(g_refs, an, "NominalParts")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_UINT64, v.type);
    TEST_ASSERT_EQUAL_UINT64(100000u, v.u64);

    TEST_ASSERT_TRUE(read_value(child(g_refs, an, "NumCavities")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_UINT32, v.type);
    TEST_ASSERT_EQUAL_UINT32(4u, v.u32);

    TEST_ASSERT_TRUE(read_value(child(g_refs, an, "ExpectedCycleTime")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_DOUBLE, v.type);
    TEST_ASSERT_EQUAL_FLOAT(12.5f, (float)v.f64);
}

void test_counter_keeps_full_64_bit_range(void)
{
    bind_machine();
    g_imm.active_job_values.machine_cycle_counter = 18446744073709551615ull;
    const int32_t vn = browse(PROTOCORE_EM77_NS, node_at("Jobs", "ActiveJobValues", NULL));

    OpcUaVariant v;
    TEST_ASSERT_TRUE(read_value(child(g_refs, vn, "MachineCycleCounter")->target_id, &v));
    TEST_ASSERT_EQUAL_UINT64(18446744073709551615ull, v.u64);
}

void test_enumeration_values(void)
{
    TEST_ASSERT_EQUAL_INT(0, EM_MODE_OTHER);
    TEST_ASSERT_EQUAL_INT(1, EM_MODE_AUTOMATIC);
    TEST_ASSERT_EQUAL_INT(2, EM_MODE_SEMI_AUTOMATIC);
    TEST_ASSERT_EQUAL_INT(3, EM_MODE_MANUAL);
    TEST_ASSERT_EQUAL_INT(4, EM_MODE_SETUP);
    TEST_ASSERT_EQUAL_INT(5, EM_MODE_SLEEP);

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

    bind_machine();
    g_imm.status.machine_mode = EM_MODE_SETUP;
    g_imm.active_job_values.job_status = EM_JOB_TEAR_DOWN_FINISHED;

    const int32_t sn = browse(PROTOCORE_EM77_NS, node_at("MachineStatus", NULL, NULL));
    OpcUaVariant v;
    TEST_ASSERT_TRUE(read_value(child(g_refs, sn, "MachineMode")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_INT32, v.type);
    TEST_ASSERT_EQUAL_INT32((int32_t)EM_MODE_SETUP, v.i32);

    TEST_ASSERT_TRUE(read_value(child(g_refs, sn, "IsPresent")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_BOOL, v.type);
    TEST_ASSERT_TRUE(v.b);

    const int32_t vn = browse(PROTOCORE_EM77_NS, node_at("Jobs", "ActiveJobValues", NULL));
    TEST_ASSERT_TRUE(read_value(child(g_refs, vn, "JobStatus")->target_id, &v));
    TEST_ASSERT_EQUAL_INT(OPCUA_VAR_INT32, v.type);
    TEST_ASSERT_EQUAL_INT32((int32_t)EM_JOB_TEAR_DOWN_FINISHED, v.i32);
}

void test_reads_follow_the_bound_model(void)
{
    bind_machine();
    const int32_t vn = browse(PROTOCORE_EM77_NS, node_at("Jobs", "ActiveJobValues", NULL));
    const uint32_t cycles = child(g_refs, vn, "JobCycleCounter")->target_id;

    OpcUaVariant v;
    TEST_ASSERT_TRUE(read_value(cycles, &v));
    TEST_ASSERT_EQUAL_UINT64(1234u, v.u64);

    g_imm.active_job_values.job_cycle_counter = 1235u;
    TEST_ASSERT_TRUE(read_value(cycles, &v));
    TEST_ASSERT_EQUAL_UINT64(1235u, v.u64);
}

void test_reads_outside_the_model_are_refused(void)
{
    bind_machine();
    const uint32_t interface_id = node_at(NULL, NULL, NULL);
    const int32_t n = browse(PROTOCORE_EM77_NS, node_at("MachineInformation", NULL, NULL));
    const uint32_t manufacturer = child(g_refs, n, "Manufacturer")->target_id;
    OpcUaVariant v;

    TEST_ASSERT_FALSE(protocore_em77_read(PROTOCORE_EM77_NS, 1u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(protocore_em77_read(PROTOCORE_EM77_NS, interface_id, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(protocore_em77_read((uint16_t)(PROTOCORE_EM77_NS + 1u), manufacturer, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_FALSE(protocore_em77_read(PROTOCORE_EM77_NS, manufacturer, OPCUA_ATTR_VALUE + 1u, &v));
}

void test_browse_outside_the_model(void)
{
    bind_machine();
    const uint32_t interface_id = node_at(NULL, NULL, NULL);
    const int32_t n = browse(PROTOCORE_EM77_NS, node_at("MachineInformation", NULL, NULL));
    const uint32_t manufacturer = child(g_refs, n, "Manufacturer")->target_id;

    TEST_ASSERT_EQUAL_INT32(-1, browse(PROTOCORE_EM77_NS, manufacturer));
    TEST_ASSERT_EQUAL_INT32(-1, browse(PROTOCORE_EM77_NS, 1u));
    TEST_ASSERT_EQUAL_INT32(-1, browse((uint16_t)(PROTOCORE_EM77_NS + 1u), interface_id));
}

void test_unbound_model_serves_nothing(void)
{
    protocore_em77_bind(NULL);
    OpcUaVariant v;
    TEST_ASSERT_FALSE(protocore_em77_read(PROTOCORE_EM77_NS, 7101u, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_INT32(-1, browse(0, OBJECTS_FOLDER_ID));
    TEST_ASSERT_EQUAL_INT32(-1, browse(PROTOCORE_EM77_NS, 7000u));
    bind_machine();
}

void test_browse_respects_the_caller_bound(void)
{
    bind_machine();
    const uint32_t info = node_at("MachineInformation", NULL, NULL);
    OpcUaReference few[3];
    const int32_t n = protocore_em77_browse(PROTOCORE_EM77_NS, info, few, 3u);
    TEST_ASSERT_EQUAL_INT32(3, n);
    TEST_ASSERT_EQUAL_STRING("Manufacturer", few[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("Model", few[1].browse_name);
    TEST_ASSERT_EQUAL_STRING("SerialNumber", few[2].browse_name);

    TEST_ASSERT_EQUAL_INT32(0, protocore_em77_browse(PROTOCORE_EM77_NS, info, few, 0u));
}

void test_references_are_forward_and_in_namespace(void)
{
    bind_machine();
    const uint32_t containers[] = {
        node_at(NULL, NULL, NULL),          node_at("MachineInformation", NULL, NULL),
        node_at("MachineStatus", NULL, NULL), node_at("Jobs", NULL, NULL),
        node_at("Jobs", "ActiveJob", NULL), node_at("Jobs", "ActiveJobValues", NULL),
    };
    for (size_t c = 0; c < sizeof(containers) / sizeof(containers[0]); c++)
    {
        const int32_t n = browse(PROTOCORE_EM77_NS, containers[c]);
        TEST_ASSERT_TRUE(n > 0);
        for (int32_t i = 0; i < n; i++)
        {
            TEST_ASSERT_TRUE(g_refs[i].is_forward);
            TEST_ASSERT_EQUAL_UINT(PROTOCORE_EM77_NS, g_refs[i].target_ns);
            TEST_ASSERT_EQUAL_UINT(PROTOCORE_EM77_NS, g_refs[i].browse_name_ns);
            TEST_ASSERT_EQUAL_STRING(g_refs[i].browse_name, g_refs[i].display_name);
        }
    }
}
