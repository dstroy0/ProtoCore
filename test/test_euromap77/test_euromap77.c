// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the EUROMAP 77 (OPC 40077) IMM_MES_Interface model: the Browse hierarchy shape + the
// Read resolver over a bound EmImm, including the UInt64 production counters.

#include "services/machine_tool/euromap77/euromap77.h"

#include <unity.h>

// Node ids (must track the internal enum in euromap77.cpp).
enum
{
    N_IMM = 7000,
    N_MACHINEINFO = 7100,
    N_MI_MANUFACTURER = 7101,
    N_MI_MODEL = 7102,
    N_MI_SERIAL = 7103,
    N_MI_PRODUCTCODE = 7104,
    N_MI_HWREV = 7105,
    N_MI_SWREV = 7106,
    N_MI_DEVREV = 7107,
    N_MI_MANUFACTURERURI = 7108,
    N_MACHINESTATUS = 7200,
    N_MS_ISPRESENT = 7201,
    N_MS_MACHINEMODE = 7202,
    N_JOBS = 7300,
    N_ACTIVEJOB = 7310,
    N_AJ_JOBNAME = 7311,
    N_AJ_JOBDESC = 7312,
    N_AJ_MATERIAL = 7313,
    N_AJ_PRODUCTNAME = 7314,
    N_AJ_MOULDID = 7315,
    N_AJ_EXPECTEDCYCLE = 7316,
    N_AJ_NUMCAVITIES = 7317,
    N_AJ_NOMINALPARTS = 7318,
    N_ACTIVEJOBVALUES = 7320,
    N_AJV_JOBCYCLECOUNTER = 7321,
    N_AJV_MACHINECYCLECOUNTER = 7322,
    N_AJV_LASTCYCLETIME = 7323,
    N_AJV_AVERAGECYCLETIME = 7324,
    N_AJV_JOBPARTSCOUNTER = 7325,
    N_AJV_JOBGOODPARTSCOUNTER = 7326,
    N_AJV_JOBBADPARTSCOUNTER = 7327,
    N_AJV_JOBSTATUS = 7328,
};

static EmImm g_imm;

void setUp(void)
{
    memset(&g_imm, 0, sizeof(g_imm));
    g_imm.name = "IMM-1";
    g_imm.info.manufacturer = "Acme Plastics";
    g_imm.info.model = "IM-200";
    g_imm.info.serial_number = "SN-IMM-0042";
    g_imm.info.product_code = "IM200-STD";
    g_imm.info.hardware_revision = "H1";
    g_imm.info.software_revision = "3.4.0";
    g_imm.info.device_revision = "D2";
    g_imm.info.manufacturer_uri = "urn:acme:plastics";
    g_imm.status.is_present = PROTO_TRUE;
    g_imm.status.machine_mode = EM_MODE_AUTOMATIC;
    g_imm.active_job.job_name = "JOB-A";
    g_imm.active_job.job_description = "widget run";
    g_imm.active_job.material = "ABS";
    g_imm.active_job.product_name = "Widget";
    g_imm.active_job.mould_id = "MLD-9";
    g_imm.active_job.expected_cycle_time = 12.5;
    g_imm.active_job.num_cavities = 4;
    g_imm.active_job.nominal_parts = 100000ULL;
    g_imm.active_job_values.job_cycle_counter = 5000000001ULL; // > 2^32 to prove UInt64
    g_imm.active_job_values.machine_cycle_counter = 9000000002ULL;
    g_imm.active_job_values.last_cycle_time = 12.7;
    g_imm.active_job_values.average_cycle_time = 12.6;
    g_imm.active_job_values.job_parts_counter = 20000000004ULL;
    g_imm.active_job_values.job_good_parts_counter = 19000000003ULL;
    g_imm.active_job_values.job_bad_parts_counter = 1000000001ULL;
    g_imm.active_job_values.job_status = EM_JOB_IN_PRODUCTION;
    pc_em77_bind(&g_imm);
}
void tearDown(void)
{
}

static int32_t browse(uint16_t ns, uint32_t id, OpcUaReference *refs, uint32_t cap)
{
    return pc_em77_browse(ns, id, refs, cap);
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
    return NULL;
}

// --- Browse hierarchy -------------------------------------------------------
static void test_browse_objects_folder_has_interface(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(0, 85, refs, 8); // Objects folder
    TEST_ASSERT_EQUAL_INT32(1, n);
    TEST_ASSERT_EQUAL_UINT32(N_IMM, refs[0].target_id);
    TEST_ASSERT_EQUAL_UINT32(PC_EM77_NS, refs[0].target_ns);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, refs[0].node_class);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_REFTYPE_ORGANIZES, refs[0].ref_type_id);
    TEST_ASSERT_EQUAL_STRING("IMM-1", refs[0].browse_name); // uses imm->name
}

static void test_browse_interface_components(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_EM77_NS, N_IMM, refs, 8);
    TEST_ASSERT_EQUAL_INT32(3, n); // MachineInformation, MachineStatus, Jobs
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "MachineInformation"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "MachineStatus"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "Jobs"));
}

static void test_browse_machineinformation(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_EM77_NS, N_MACHINEINFO, refs, 8);
    TEST_ASSERT_EQUAL_INT32(8, n);
    const OpcUaReference *man = find_ref(refs, n, "Manufacturer");
    TEST_ASSERT_NOT_NULL(man);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_VARIABLE, man->node_class);
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "SerialNumber"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ProductCode"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ManufacturerUri"));
}

static void test_browse_status_and_jobs(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_EM77_NS, N_MACHINESTATUS, refs, 8);
    TEST_ASSERT_EQUAL_INT32(2, n); // IsPresent, MachineMode
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "IsPresent"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "MachineMode"));

    n = browse(PC_EM77_NS, N_JOBS, refs, 8);
    TEST_ASSERT_EQUAL_INT32(2, n); // ActiveJob, ActiveJobValues
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ActiveJob"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ActiveJobValues"));
}

static void test_browse_activejob_and_values(void)
{
    OpcUaReference refs[8];
    int32_t n = browse(PC_EM77_NS, N_ACTIVEJOB, refs, 8);
    TEST_ASSERT_EQUAL_INT32(8, n);
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "JobName"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "ExpectedCycleTime"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "NumCavities"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "NominalParts"));

    n = browse(PC_EM77_NS, N_ACTIVEJOBVALUES, refs, 8);
    TEST_ASSERT_EQUAL_INT32(8, n); // every container <= PC_OPCUA_REF_MAX
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "JobCycleCounter"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "MachineCycleCounter"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "JobGoodPartsCounter"));
    TEST_ASSERT_NOT_NULL(find_ref(refs, n, "JobStatus"));
}

static void test_browse_leaf_and_unknown_return_negative(void)
{
    OpcUaReference refs[4];
    TEST_ASSERT_EQUAL_INT32(-1, browse(PC_EM77_NS, N_MI_MANUFACTURER, refs, 4)); // a leaf Variable
    TEST_ASSERT_EQUAL_INT32(-1, browse(PC_EM77_NS, 999999, refs, 4));            // unknown node
    TEST_ASSERT_EQUAL_INT32(-1, browse(7, N_IMM, refs, 4));                      // wrong namespace
}

// --- Read resolver ----------------------------------------------------------
static void test_read_identity_and_status(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_MANUFACTURER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_STRING("Acme Plastics", v.str);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MS_ISPRESENT, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_BOOL, v.type);
    TEST_ASSERT_TRUE(v.b);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MS_MACHINEMODE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_INT32, v.type);
    TEST_ASSERT_EQUAL_INT32((int32_t)EM_MODE_AUTOMATIC, v.i32);
}

static void test_read_job_and_counters(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJ_JOBNAME, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("JOB-A", v.str);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJ_EXPECTEDCYCLE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_DOUBLE, v.type);
    TEST_ASSERT_TRUE(v.f64 == 12.5);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJ_NUMCAVITIES, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_UINT32, v.type);
    TEST_ASSERT_EQUAL_UINT32(4, v.u32);

    // UInt64 fields (prove the new Variant type carries a value > 2^32)
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJ_NOMINALPARTS, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_UINT64, v.type);
    TEST_ASSERT_TRUE(v.u64 == 100000ULL);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_JOBCYCLECOUNTER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_UINT64, v.type);
    TEST_ASSERT_TRUE(v.u64 == 5000000001ULL);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_JOBPARTSCOUNTER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.u64 == 20000000004ULL);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_LASTCYCLETIME, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.f64 == 12.7);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_JOBSTATUS, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_INT32((int32_t)EM_JOB_IN_PRODUCTION, v.i32);
}

static void test_read_null_string_served_as_empty(void)
{
    g_imm.info.model = NULL; // a null field must not crash - served as ""
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_MODEL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_STRING("", v.str);
    TEST_ASSERT_EQUAL_INT32(0, v.str_len);
}

static void test_read_rejects_unknown_ns_attr_and_node(void)
{
    OpcUaVariant v;
    TEST_ASSERT_FALSE(pc_em77_read(PC_EM77_NS, 999999, OPCUA_ATTR_VALUE, &v));         // unknown node
    TEST_ASSERT_FALSE(pc_em77_read(7, N_MI_MANUFACTURER, OPCUA_ATTR_VALUE, &v));       // wrong namespace
    TEST_ASSERT_FALSE(pc_em77_read(PC_EM77_NS, N_MI_MANUFACTURER, 12 /*!Value*/, &v)); // not the Value attribute
}

static void test_read_before_bind_is_a_clean_miss(void)
{
    pc_em77_bind(NULL); // no model bound
    OpcUaVariant v;
    TEST_ASSERT_FALSE(pc_em77_read(PC_EM77_NS, N_MI_MANUFACTURER, OPCUA_ATTR_VALUE, &v));
    OpcUaReference refs[4];
    TEST_ASSERT_EQUAL_INT32(-1, pc_em77_browse(0, 85, refs, 4));
}

// Every remaining MachineInformation String leaf resolves to its own EmMachineInformation field (an
// off-by-one in the case table would cross-wire two identity strings).
static void test_read_every_machineinformation_string(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_SERIAL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("SN-IMM-0042", v.str);
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_PRODUCTCODE, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("IM200-STD", v.str);
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_HWREV, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("H1", v.str);
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_SWREV, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("3.4.0", v.str);
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_DEVREV, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("D2", v.str);
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_MANUFACTURERURI, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_STRING("urn:acme:plastics", v.str);
}

// Every remaining ActiveJob String leaf resolves to its own EmActiveJob field.
static void test_read_every_activejob_string(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJ_JOBDESC, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_STRING, v.type);
    TEST_ASSERT_EQUAL_STRING("widget run", v.str);
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJ_MATERIAL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("ABS", v.str);
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJ_PRODUCTNAME, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("Widget", v.str);
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJ_MOULDID, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("MLD-9", v.str);
}

// The remaining ActiveJobValues leaves: the counters stay UInt64 (values > 2^32 survive) and the cycle
// times stay Double.
static void test_read_remaining_activejobvalues(void)
{
    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_MACHINECYCLECOUNTER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_UINT64, v.type);
    TEST_ASSERT_TRUE(v.u64 == 9000000002ULL);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_AVERAGECYCLETIME, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_DOUBLE, v.type);
    TEST_ASSERT_TRUE(v.f64 == 12.6);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_JOBGOODPARTSCOUNTER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_UINT64, v.type);
    TEST_ASSERT_TRUE(v.u64 == 19000000003ULL);

    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_JOBBADPARTSCOUNTER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL(OPCUA_VAR_UINT64, v.type);
    TEST_ASSERT_TRUE(v.u64 == 1000000001ULL);
}

// A Browse into a buffer smaller than the child count fills exactly the capacity and drops the rest -
// no write past out[max-1].
static void test_browse_stops_at_caller_capacity(void)
{
    OpcUaReference refs[8];
    memset(refs, 0, sizeof(refs));
    int32_t n = browse(PC_EM77_NS, N_MACHINEINFO, refs, 3); // 8 children, room for 3
    TEST_ASSERT_EQUAL_INT32(3, n);
    TEST_ASSERT_EQUAL_STRING("Manufacturer", refs[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("SerialNumber", refs[2].browse_name);
    TEST_ASSERT_NULL(refs[3].browse_name); // the 4th child was dropped, not written

    TEST_ASSERT_EQUAL_INT32(0, browse(PC_EM77_NS, N_ACTIVEJOBVALUES, refs, 0)); // zero capacity writes nothing
}

// ns0 is only special for the Objects folder: any other ns0 node is outside the model.
static void test_browse_other_ns0_node_is_a_miss(void)
{
    OpcUaReference refs[4];
    TEST_ASSERT_EQUAL_INT32(-1, browse(0, 86, refs, 4)); // Types folder
    TEST_ASSERT_EQUAL_INT32(-1, browse(0, 84, refs, 4)); // Root folder
}

// With no name set on the IMM, the Objects->interface reference falls back to the spec BrowseName.
static void test_browse_objects_folder_default_name(void)
{
    g_imm.name = NULL;
    OpcUaReference refs[4];
    int32_t n = browse(0, 85, refs, 4);
    TEST_ASSERT_EQUAL_INT32(1, n);
    TEST_ASSERT_EQUAL_STRING("IMM_MES_Interface", refs[0].browse_name);
    TEST_ASSERT_EQUAL_STRING("IMM_MES_Interface", refs[0].display_name);
}

// install() binds the model as well as registering the resolvers with the OPC UA server, so a Read
// right after install resolves against the newly bound IMM.
static void test_install_binds_the_model(void)
{
    static EmImm other;
    memset(&other, 0, sizeof(other));
    other.name = "IMM-2";
    other.info.serial_number = "SN-IMM-9999";
    other.active_job_values.job_bad_parts_counter = 7ULL;

    pc_em77_install(&other);

    OpcUaVariant v;
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_MI_SERIAL, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_EQUAL_STRING("SN-IMM-9999", v.str); // the installed model, not the setUp one
    TEST_ASSERT_TRUE(pc_em77_read(PC_EM77_NS, N_AJV_JOBBADPARTSCOUNTER, OPCUA_ATTR_VALUE, &v));
    TEST_ASSERT_TRUE(v.u64 == 7ULL);
    OpcUaReference refs[4];
    TEST_ASSERT_EQUAL_INT32(1, pc_em77_browse(0, 85, refs, 4));
    TEST_ASSERT_EQUAL_STRING("IMM-2", refs[0].browse_name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_browse_objects_folder_has_interface);
    RUN_TEST(test_browse_interface_components);
    RUN_TEST(test_browse_machineinformation);
    RUN_TEST(test_browse_status_and_jobs);
    RUN_TEST(test_browse_activejob_and_values);
    RUN_TEST(test_browse_leaf_and_unknown_return_negative);
    RUN_TEST(test_read_identity_and_status);
    RUN_TEST(test_read_job_and_counters);
    RUN_TEST(test_read_null_string_served_as_empty);
    RUN_TEST(test_read_rejects_unknown_ns_attr_and_node);
    RUN_TEST(test_read_before_bind_is_a_clean_miss);
    RUN_TEST(test_read_every_machineinformation_string);
    RUN_TEST(test_read_every_activejob_string);
    RUN_TEST(test_read_remaining_activejobvalues);
    RUN_TEST(test_browse_stops_at_caller_capacity);
    RUN_TEST(test_browse_other_ns0_node_is_a_miss);
    RUN_TEST(test_browse_objects_folder_default_name);
    RUN_TEST(test_install_binds_the_model);
    return UNITY_END();
}
