// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/opcua/opcua.h"
#include "services/opcua/opcua_client/opcua_client.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static int32_t srv_browse(uint16_t ns, uint32_t id, OpcUaReference *out, uint32_t max)
{
    if (ns == 0 && id == 85 && max >= 2)
    {
        const char *names[2] = {"Uptime", "Temperature"};
        for (int i = 0; i < 2; i++)
        {
            out[i].ref_type_id = OPCUA_REFTYPE_ORGANIZES;
            out[i].is_forward = PROTO_TRUE;
            out[i].target_ns = 1;
            out[i].target_id = i + 1;
            out[i].browse_name_ns = 1;
            out[i].browse_name = names[i];
            out[i].display_name = names[i];
            out[i].node_class = OPCUA_NODECLASS_VARIABLE;
            out[i].type_def_id = OPCUA_TYPEDEF_BASE_DATA_VARIABLE;
        }
        return 2;
    }
    return -1;
}

void test_hello_ack_roundtrip()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);

    uint8_t req[128];
    size_t rn = protocore_opcua_client_hello("opc.tcp://host:4840", req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);

    OpcUaHello hello;
    TEST_ASSERT_TRUE(protocore_opcua_parse_hello(req, rn, &hello));
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_OPCUA_BUF, hello.recv_buf_size);

    uint8_t ack[64];
    size_t an = protocore_opcua_build_ack(&hello, ack, sizeof(ack));
    TEST_ASSERT_TRUE(an > 0);

    OpcUaAckInfo info;
    TEST_ASSERT_TRUE(protocore_opcua_client_on_ack(ack, an, &info));
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_OPCUA_BUF, info.recv_buf_size);
    TEST_ASSERT_EQUAL_UINT32(1, info.max_chunk_count);
}

void test_open_roundtrip()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);

    uint8_t req[256];
    size_t rn = protocore_opcua_client_open(&c, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);

    OpcUaOpenChannel oc;
    TEST_ASSERT_TRUE(protocore_opcua_parse_open(req, rn, &oc));
    TEST_ASSERT_EQUAL_UINT32(1, oc.message_security_mode);

    uint8_t resp[256];
    size_t sn = protocore_opcua_build_open_response(&oc, 77, 88, 1, 0, 3600000, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    TEST_ASSERT_TRUE(protocore_opcua_client_on_open(&c, resp, sn));
    TEST_ASSERT_EQUAL_UINT32(77, c.channel_id);
    TEST_ASSERT_EQUAL_UINT32(88, c.token_id);
}

void test_session_roundtrip()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    c.token_id = 88;

    uint8_t req[256];
    size_t rn = protocore_opcua_client_create_session(&c, "sess", "opc.tcp://host:4840", req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);
    OpcUaMsg m;
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(req, rn, &m));
    TEST_ASSERT_EQUAL_UINT32(OPCUA_ID_CREATE_SESSION_REQ, m.type_id);

    uint8_t resp[512];
    OpcUaServerInfo si = {"opc.tcp://host:4840", "urn:test", "TestServer"};
    size_t sn =
        protocore_opcua_build_create_session_response(&m, 0x500, 0x600, 1200000.0, &si, 1, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);
    TEST_ASSERT_TRUE(protocore_opcua_client_on_create_session(&c, resp, sn));
    TEST_ASSERT_EQUAL_UINT32(0x600, c.session_auth_id);
    TEST_ASSERT_EQUAL_UINT16(1, c.session_auth_ns);

    rn = protocore_opcua_client_activate_session(&c, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(req, rn, &m));
    TEST_ASSERT_EQUAL_UINT32(OPCUA_ID_ACTIVATE_SESSION_REQ, m.type_id);
    sn = protocore_opcua_build_activate_session_response(&m, 2, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);
    TEST_ASSERT_TRUE(protocore_opcua_client_on_activate_session(resp, sn));
}

void test_read_roundtrip()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    c.token_id = 88;

    OpcUaReadItem items[2] = {{1, 1, PROTO_TRUE, OPCUA_ATTR_VALUE}, {1, 2, PROTO_TRUE, OPCUA_ATTR_VALUE}};
    uint8_t req[256];
    size_t rn = protocore_opcua_client_read(&c, items, 2, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);

    OpcUaReadRequest rr;
    TEST_ASSERT_TRUE(protocore_opcua_parse_read(req, rn, &rr));
    TEST_ASSERT_EQUAL_UINT32(2, rr.count);
    TEST_ASSERT_EQUAL_UINT32(1, rr.items[0].id);
    TEST_ASSERT_EQUAL_UINT32(2, rr.items[1].id);

    OpcUaVariant svals[2];
    uint32_t ssts[2];
    memset(svals, 0, sizeof(svals));
    svals[0].type = OPCUA_VAR_UINT32;
    svals[0].u32 = 4242;
    ssts[0] = OPCUA_STATUS_GOOD;
    svals[1].type = OPCUA_VAR_DOUBLE;
    svals[1].f64 = 2.5;
    ssts[1] = OPCUA_STATUS_GOOD;

    uint8_t resp[256];
    size_t sn = protocore_opcua_build_read_response(&rr, svals, ssts, 3, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    OpcUaVariant cvals[2];
    uint32_t csts[2];
    int32_t n = protocore_opcua_client_on_read(resp, sn, cvals, csts, 2);
    TEST_ASSERT_EQUAL_INT32(2, n);
    TEST_ASSERT_EQUAL_HEX8(OPCUA_VAR_UINT32, cvals[0].type);
    TEST_ASSERT_EQUAL_UINT32(4242, cvals[0].u32);
    TEST_ASSERT_EQUAL_HEX8(OPCUA_VAR_DOUBLE, cvals[1].type);
    TEST_ASSERT_TRUE(cvals[1].f64 == 2.5);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_STATUS_GOOD, csts[0]);
}

void test_browse_roundtrip()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    c.token_id = 88;

    uint8_t req[256];
    size_t rn = protocore_opcua_client_browse(&c, 0, 85, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);

    OpcUaBrowseRequest br;
    TEST_ASSERT_TRUE(protocore_opcua_parse_browse(req, rn, &br));
    TEST_ASSERT_EQUAL_UINT32(1, br.count);
    TEST_ASSERT_EQUAL_UINT32(85, br.items[0].id);

    uint8_t resp[512];
    size_t sn = protocore_opcua_build_browse_response(&br, srv_browse, 4, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    OpcUaClientRef refs[4];
    int32_t n = protocore_opcua_client_on_browse(resp, sn, refs, 4);
    TEST_ASSERT_EQUAL_INT32(2, n);
    TEST_ASSERT_EQUAL_STRING("Uptime", refs[0].browse_name);
    TEST_ASSERT_EQUAL_UINT32(1, refs[0].target_id);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_VARIABLE, refs[0].node_class);
    TEST_ASSERT_EQUAL_STRING("Temperature", refs[1].browse_name);
    TEST_ASSERT_EQUAL_UINT32(2, refs[1].target_id);
}

void test_get_endpoints_roundtrip()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    c.token_id = 88;

    uint8_t req[256];
    size_t rn = protocore_opcua_client_get_endpoints(&c, "opc.tcp://host:4840", req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);
    OpcUaMsg m;
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(req, rn, &m));
    TEST_ASSERT_EQUAL_UINT32(OPCUA_ID_GET_ENDPOINTS_REQ, m.type_id);

    OpcUaServerInfo si = {"opc.tcp://host:4840", "urn:test", "TestServer"};
    uint8_t resp[512];
    size_t sn = protocore_opcua_build_get_endpoints_response(&m, &si, 7, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    int32_t ep = protocore_opcua_client_on_get_endpoints(resp, sn);
    TEST_ASSERT_EQUAL_INT32(1, ep);
}

void test_service_fault_rejected_by_parsers()
{

    OpcUaMsg m;
    memset(&m, 0, sizeof(m));
    m.token_id = 88;
    m.request_id = 9;
    m.request_handle = 3;
    uint8_t resp[64];
    size_t sn = protocore_opcua_build_service_fault(&m, OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED, 1, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    OpcUaVariant vals[1];
    uint32_t sts[1];
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, sn, vals, sts, 1));
    TEST_ASSERT_FALSE(protocore_opcua_client_on_activate_session(resp, sn));
}

void test_write_roundtrip()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    c.token_id = 88;

    OpcUaWriteItem items[1];
    memset(items, 0, sizeof(items));
    items[0].ns = 1;
    items[0].id = 10;
    items[0].numeric = PROTO_TRUE;
    items[0].attribute = OPCUA_ATTR_VALUE;
    items[0].value.type = OPCUA_VAR_UINT32;
    items[0].value.u32 = 4242;

    uint8_t req[128];
    size_t rn = protocore_opcua_client_write(&c, items, 1, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);

    OpcUaWriteRequest wr;
    TEST_ASSERT_TRUE(protocore_opcua_parse_write(req, rn, &wr));
    TEST_ASSERT_EQUAL_UINT32(1, wr.count);
    TEST_ASSERT_EQUAL_UINT32(10, wr.items[0].id);
    TEST_ASSERT_EQUAL_HEX8(OPCUA_VAR_UINT32, wr.items[0].value.type);
    TEST_ASSERT_EQUAL_UINT32(4242, wr.items[0].value.u32);

    uint32_t res[1] = {OPCUA_STATUS_GOOD};
    uint8_t resp[64];
    size_t sn = protocore_opcua_build_write_response(&wr, res, 9, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    uint32_t got[1] = {0xFFFFFFFFu};
    int32_t nres = protocore_opcua_client_on_write(resp, sn, got, 1);
    TEST_ASSERT_EQUAL_INT32(1, nres);
    TEST_ASSERT_EQUAL_HEX32(OPCUA_STATUS_GOOD, got[0]);
}

void test_close_session_roundtrip()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    c.token_id = 88;

    uint8_t req[128];
    size_t rn = protocore_opcua_client_close_session(&c, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);
    OpcUaMsg m;
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(req, rn, &m));
    TEST_ASSERT_EQUAL_UINT32(OPCUA_ID_CLOSE_SESSION_REQ, m.type_id);

    uint8_t resp[64];
    size_t sn = protocore_opcua_build_close_session_response(&m, 5, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);
    UaMsgHeader h;
    TEST_ASSERT_TRUE(protocore_opcua_parse_header(resp, sn, &h));
    TEST_ASSERT_EQUAL_MEMORY("MSG", h.type, 3);
}

void test_close_channel_is_clo()
{
    uint8_t req[16];
    size_t rn = protocore_opcua_client_close_channel(req, sizeof(req));
    TEST_ASSERT_EQUAL_size_t(8, rn);
    UaMsgHeader h;
    TEST_ASSERT_TRUE(protocore_opcua_parse_header(req, rn, &h));
    TEST_ASSERT_EQUAL_MEMORY("CLO", h.type, 3);
}

void test_seq_and_request_id_increment()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    uint8_t req[256];
    protocore_opcua_client_open(&c, req, sizeof(req));
    protocore_opcua_client_create_session(&c, "s", "u", req, sizeof(req));
    TEST_ASSERT_EQUAL_UINT32(2, c.seq);
    TEST_ASSERT_EQUAL_UINT32(2, c.request_id);
    TEST_ASSERT_TRUE(c.request_handle >= 2);
}

void test_on_read_all_variant_types()
{
    OpcUaReadRequest rr;
    memset(&rr, 0, sizeof(rr));
    rr.count = 5;

    OpcUaVariant sv[5];
    uint32_t ss[5];
    memset(sv, 0, sizeof(sv));
    sv[0].type = OPCUA_VAR_BOOL;
    sv[0].b = PROTO_TRUE;
    sv[1].type = OPCUA_VAR_INT32;
    sv[1].i32 = -5;
    sv[2].type = OPCUA_VAR_FLOAT;
    sv[2].f32 = 1.5f;
    sv[3].type = OPCUA_VAR_STRING;
    sv[3].str = "hi";
    sv[3].str_len = 2;
    sv[4].type = OPCUA_VAR_INT32;
    sv[4].i32 = 7;
    ss[0] = ss[1] = ss[2] = ss[3] = OPCUA_STATUS_GOOD;
    ss[4] = OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED;

    uint8_t resp[512];
    size_t sn = protocore_opcua_build_read_response(&rr, sv, ss, 5, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    OpcUaVariant cv[5];
    uint32_t cs[5];
    int32_t n = protocore_opcua_client_on_read(resp, sn, cv, cs, 5);
    TEST_ASSERT_EQUAL_INT32(5, n);
    TEST_ASSERT_TRUE(cv[0].b);
    TEST_ASSERT_EQUAL_INT32(-5, cv[1].i32);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, cv[2].f32);
    TEST_ASSERT_EQUAL_INT32(2, cv[3].str_len);
    TEST_ASSERT_EQUAL_MEMORY("hi", cv[3].str, 2);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED, cs[4]);
}

void test_client_parsers_reject_fault()
{
    OpcUaMsg m;
    memset(&m, 0, sizeof(m));
    uint8_t resp[128];
    size_t sn = protocore_opcua_build_service_fault(&m, OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED, 1, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    OpcUaClient c;
    protocore_opcua_client_init(&c);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_get_endpoints(resp, sn));
    TEST_ASSERT_FALSE(protocore_opcua_client_on_create_session(&c, resp, sn));
    uint32_t results[1];
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_write(resp, sn, results, 1));
    OpcUaClientRef refs[1];
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_browse(resp, sn, refs, 1));
}

void test_client_parsers_reject_malformed()
{
    OpcUaAckInfo ai;
    uint8_t ack[28] = {'A', 'C', 'K', 'F', 28, 0, 0, 0};
    ack[0] = 'X';
    TEST_ASSERT_FALSE(protocore_opcua_client_on_ack(ack, sizeof(ack), &ai));
    uint8_t shortack[12] = {'A', 'C', 'K', 'F', 12, 0, 0, 0};
    TEST_ASSERT_FALSE(protocore_opcua_client_on_ack(shortack, sizeof(shortack), &ai));

    OpcUaClient c;
    protocore_opcua_client_init(&c);
    uint8_t opn[64];
    memset(opn, 0, sizeof(opn));
    opn[0] = 'M';
    opn[1] = 'S';
    opn[2] = 'G';
    opn[3] = 'F';
    opn[4] = 64;
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, opn, sizeof(opn)));

    OpcUaReadRequest rr;
    memset(&rr, 0, sizeof(rr));
    rr.count = 0;
    uint8_t resp[128];
    size_t sn = protocore_opcua_build_read_response(&rr, NULL, NULL, 0, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);
    uint8_t save = resp[24];
    resp[24] = 0x06;
    OpcUaVariant v[1];
    uint32_t s[1];
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, sn, v, s, 1));
    resp[24] = save;

    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, sn - 4, v, s, 1));
}

void test_builder_overflow_guard()
{

    uint8_t tiny[6];
    TEST_ASSERT_EQUAL_size_t(0, protocore_opcua_client_hello("opc.tcp://host:4840", tiny, sizeof(tiny)));
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    TEST_ASSERT_EQUAL_size_t(0, protocore_opcua_client_open(&c, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_opcua_client_activate_session(&c, tiny, sizeof(tiny)));
}

void test_on_read_unknown_variant_rejected()
{

    OpcUaClient c;
    protocore_opcua_client_init(&c);
    OpcUaReadItem items[1] = {{1, 1, PROTO_TRUE, OPCUA_ATTR_VALUE}};
    uint8_t req[128];
    size_t rn = protocore_opcua_client_read(&c, items, 1, req, sizeof(req));
    OpcUaReadRequest rr;
    TEST_ASSERT_TRUE(protocore_opcua_parse_read(req, rn, &rr));
    OpcUaVariant sv[1];
    uint32_t ss[1];
    memset(sv, 0, sizeof(sv));
    sv[0].type = OPCUA_VAR_UINT32;
    sv[0].u32 = 0xA1B2C3D4;
    ss[0] = OPCUA_STATUS_GOOD;
    uint8_t resp[128];
    size_t sn = protocore_opcua_build_read_response(&rr, sv, ss, 1, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);
    int type_off = -1;
    for (size_t i = 1; i + 4 < sn; i++)
    {
        if (resp[i] == (uint8_t)OPCUA_VAR_UINT32 && resp[i + 1] == 0xD4 && resp[i + 2] == 0xC3 && resp[i + 3] == 0xB2 &&
            resp[i + 4] == 0xA1)
        {
            type_off = (int)i;
            break;
        }
    }
    TEST_ASSERT_TRUE(type_off > 0);
    resp[type_off] = 200;
    OpcUaVariant cv[1];
    uint32_t cs[1];
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, sn, cv, cs, 1));
}

static size_t build_min_response(uint8_t *out, size_t cap, uint32_t type_id, int32_t count_field, int32_t str_table)
{
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    protocore_ua_w_u8(&w, 'M');
    protocore_ua_w_u8(&w, 'S');
    protocore_ua_w_u8(&w, 'G');
    protocore_ua_w_u8(&w, 'F');
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_nodeid_numeric(&w, 0, type_id);
    protocore_ua_w_u64(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, OPCUA_STATUS_GOOD);
    protocore_ua_w_u8(&w, 0);
    protocore_ua_w_i32(&w, str_table);
    for (int32_t i = 0; i < str_table; i++)
    {
        protocore_ua_w_i32(&w, -1);
    }
    protocore_ua_w_nodeid_numeric(&w, 0, 0);
    protocore_ua_w_u8(&w, 0);
    protocore_ua_w_i32(&w, count_field);
    out[4] = (uint8_t)w.n;
    out[5] = (uint8_t)(w.n >> 8);
    out[6] = (uint8_t)(w.n >> 16);
    out[7] = (uint8_t)(w.n >> 24);
    return w.ok ? w.n : 0;
}

void test_response_parsers_reject_negative_count()
{
    uint8_t resp[128];
    OpcUaVariant v[1];
    uint32_t s[1];
    size_t n = build_min_response(resp, sizeof(resp), OPCUA_ID_READ_RESP, -1, 0);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, n, v, s, 1));
    uint32_t wr[1];
    n = build_min_response(resp, sizeof(resp), OPCUA_ID_WRITE_RESP, -1, 0);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_write(resp, n, wr, 1));
    OpcUaClientRef refs[1];
    n = build_min_response(resp, sizeof(resp), OPCUA_ID_BROWSE_RESP, -1, 0);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_browse(resp, n, refs, 1));
}

static size_t build_opn(uint8_t *out, size_t cap, uint32_t type_id, int32_t policy_len, proto_bool with_rh,
                        uint32_t svc, proto_bool with_body, proto_bool string_type_id)
{
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    protocore_ua_w_u8(&w, 'O');
    protocore_ua_w_u8(&w, 'P');
    protocore_ua_w_u8(&w, 'N');
    protocore_ua_w_u8(&w, 'F');
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_i32(&w, policy_len);
    for (int32_t i = 0; i < policy_len; i++)
    {
        protocore_ua_w_u8(&w, 'x');
    }
    protocore_ua_w_i32(&w, -1);
    protocore_ua_w_i32(&w, -1);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    if (string_type_id)
    {
        protocore_ua_w_u8(&w, 0x03);
        protocore_ua_w_u16(&w, 0);
        protocore_ua_w_string(&w, "OpenResp", 8);
    }
    else
    {
        protocore_ua_w_nodeid_numeric(&w, 0, type_id);
    }
    if (with_rh)
    {
        protocore_ua_w_u64(&w, 0);
        protocore_ua_w_u32(&w, 0);
        protocore_ua_w_u32(&w, svc);
        protocore_ua_w_u8(&w, 0);
        protocore_ua_w_i32(&w, 0);
        protocore_ua_w_nodeid_numeric(&w, 0, 0);
        protocore_ua_w_u8(&w, 0);
    }
    if (with_body)
    {
        protocore_ua_w_u32(&w, 0);
        protocore_ua_w_u32(&w, 0x2A);
        protocore_ua_w_u32(&w, 0x2B);
        protocore_ua_w_u64(&w, 0);
        protocore_ua_w_u32(&w, 1000);
    }
    out[4] = (uint8_t)w.n;
    out[5] = (uint8_t)(w.n >> 8);
    out[6] = (uint8_t)(w.n >> 16);
    out[7] = (uint8_t)(w.n >> 24);
    return w.ok ? w.n : 0;
}

static size_t forge_msg(uint8_t *out, size_t cap, uint32_t type_id, uint32_t svc, const uint8_t *body, size_t body_len,
                        proto_bool string_type_id, const char *magic)
{
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    protocore_ua_w_u8(&w, (uint8_t)magic[0]);
    protocore_ua_w_u8(&w, (uint8_t)magic[1]);
    protocore_ua_w_u8(&w, (uint8_t)magic[2]);
    protocore_ua_w_u8(&w, 'F');
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    if (string_type_id)
    {
        protocore_ua_w_u8(&w, 0x03);
        protocore_ua_w_u16(&w, 0);
        protocore_ua_w_string(&w, "Resp", 4);
    }
    else
    {
        protocore_ua_w_nodeid_numeric(&w, 0, type_id);
    }
    protocore_ua_w_u64(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, svc);
    protocore_ua_w_u8(&w, 0);
    protocore_ua_w_i32(&w, 0);
    protocore_ua_w_nodeid_numeric(&w, 0, 0);
    protocore_ua_w_u8(&w, 0);
    for (size_t i = 0; i < body_len; i++)
    {
        protocore_ua_w_u8(&w, body[i]);
    }
    out[4] = (uint8_t)w.n;
    out[5] = (uint8_t)(w.n >> 8);
    out[6] = (uint8_t)(w.n >> 16);
    out[7] = (uint8_t)(w.n >> 24);
    return w.ok ? w.n : 0;
}

void test_on_open_guards()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    uint8_t buf[128];

    size_t n =
        build_opn(buf, sizeof(buf), OPCUA_ID_OPEN_RESP, 10, PROTO_FALSE, OPCUA_STATUS_GOOD, PROTO_FALSE, PROTO_FALSE);
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, buf, n));

    n = build_opn(buf, sizeof(buf), 999, 0, PROTO_TRUE, OPCUA_STATUS_GOOD, PROTO_FALSE, PROTO_FALSE);
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, buf, n));

    uint8_t ovf[16];
    UaWriter w = {ovf, sizeof(ovf), 0, PROTO_TRUE};
    protocore_ua_w_u8(&w, 'O');
    protocore_ua_w_u8(&w, 'P');
    protocore_ua_w_u8(&w, 'N');
    protocore_ua_w_u8(&w, 'F');
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_i32(&w, 1000000);
    ovf[4] = (uint8_t)w.n;
    ovf[5] = ovf[6] = ovf[7] = 0;
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, ovf, w.n));
}

void test_response_header_string_table_skip()
{

    uint8_t resp[128];
    size_t n = build_min_response(resp, sizeof(resp), OPCUA_ID_READ_RESP, 0, 1);
    OpcUaVariant v[1];
    uint32_t s[1];
    TEST_ASSERT_EQUAL_INT32(0, protocore_opcua_client_on_read(resp, n, v, s, 1));
}

static size_t build_browse_with_locale(uint8_t *out, size_t cap)
{
    UaWriter w = {out, cap, 0, PROTO_TRUE};
    protocore_ua_w_u8(&w, 'M');
    protocore_ua_w_u8(&w, 'S');
    protocore_ua_w_u8(&w, 'G');
    protocore_ua_w_u8(&w, 'F');
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_BROWSE_RESP);
    protocore_ua_w_u64(&w, 0);
    protocore_ua_w_u32(&w, 0);
    protocore_ua_w_u32(&w, OPCUA_STATUS_GOOD);
    protocore_ua_w_u8(&w, 0);
    protocore_ua_w_i32(&w, 0);
    protocore_ua_w_nodeid_numeric(&w, 0, 0);
    protocore_ua_w_u8(&w, 0);

    protocore_ua_w_i32(&w, 1);
    protocore_ua_w_u32(&w, OPCUA_STATUS_GOOD);
    protocore_ua_w_i32(&w, -1);
    protocore_ua_w_i32(&w, 1);
    protocore_ua_w_nodeid_numeric(&w, 0, OPCUA_REFTYPE_ORGANIZES);
    protocore_ua_w_bool(&w, PROTO_TRUE);
    protocore_ua_w_nodeid_numeric(&w, 1, 7);
    protocore_ua_w_u16(&w, 1);
    protocore_ua_w_string(&w, "Node7", 5);
    protocore_ua_w_u8(&w, 0x03);
    protocore_ua_w_string(&w, "en-US", 5);
    protocore_ua_w_string(&w, "Node 7", 6);
    protocore_ua_w_u32(&w, OPCUA_NODECLASS_VARIABLE);
    protocore_ua_w_nodeid_numeric(&w, 0, OPCUA_TYPEDEF_BASE_DATA_VARIABLE);
    out[4] = (uint8_t)w.n;
    out[5] = (uint8_t)(w.n >> 8);
    out[6] = (uint8_t)(w.n >> 16);
    out[7] = (uint8_t)(w.n >> 24);
    return w.ok ? w.n : 0;
}

void test_browse_display_name_locale()
{
    uint8_t resp[256];
    size_t n = build_browse_with_locale(resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);

    OpcUaClientRef refs[2];
    int32_t got = protocore_opcua_client_on_browse(resp, n, refs, 2);
    TEST_ASSERT_EQUAL_INT32(1, got);
    TEST_ASSERT_EQUAL_STRING("Node7", refs[0].browse_name);
    TEST_ASSERT_EQUAL_UINT32(7, refs[0].target_id);
    TEST_ASSERT_EQUAL_UINT16(1, refs[0].target_ns);
}

void test_builders_encode_null_strings()
{
    uint8_t req[256];
    size_t rn = protocore_opcua_client_hello(NULL, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);
    OpcUaHello hello;
    TEST_ASSERT_TRUE(protocore_opcua_parse_hello(req, rn, &hello));
    TEST_ASSERT_EQUAL_MEMORY("\xFF\xFF\xFF\xFF", req + rn - 4, 4);

    OpcUaClient c;
    protocore_opcua_client_init(&c);
    rn = protocore_opcua_client_get_endpoints(&c, NULL, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);
    OpcUaMsg m;
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(req, rn, &m));
    TEST_ASSERT_EQUAL_UINT32(OPCUA_ID_GET_ENDPOINTS_REQ, m.type_id);

    rn = protocore_opcua_client_create_session(&c, NULL, NULL, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);
    TEST_ASSERT_TRUE(protocore_opcua_parse_msg(req, rn, &m));
    TEST_ASSERT_EQUAL_UINT32(OPCUA_ID_CREATE_SESSION_REQ, m.type_id);
}

void test_on_ack_header_guards()
{
    OpcUaAckInfo ai;
    uint8_t tiny[4] = {'A', 'C', 'K', 'F'};
    TEST_ASSERT_FALSE(protocore_opcua_client_on_ack(tiny, sizeof(tiny), &ai));

    uint8_t ack[28];
    memset(ack, 0, sizeof(ack));
    memcpy(ack, "ACKF", 4);
    ack[4] = 28;
    ack[12] = 0x11;
    TEST_ASSERT_TRUE(protocore_opcua_client_on_ack(ack, sizeof(ack), &ai));
    TEST_ASSERT_EQUAL_UINT32(0x11, ai.recv_buf_size);

    ack[4] = 27;
    TEST_ASSERT_FALSE(protocore_opcua_client_on_ack(ack, sizeof(ack), &ai));
    ack[4] = 28;
    ack[1] = 'X';
    TEST_ASSERT_FALSE(protocore_opcua_client_on_ack(ack, sizeof(ack), &ai));
    ack[1] = 'C';
    ack[2] = 'X';
    TEST_ASSERT_FALSE(protocore_opcua_client_on_ack(ack, sizeof(ack), &ai));
}

void test_msg_envelope_guards()
{
    OpcUaVariant v[1];
    uint32_t s[1];
    uint8_t tiny[4] = {'M', 'S', 'G', 'F'};
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(tiny, sizeof(tiny), v, s, 1));

    uint8_t resp[128];
    const uint8_t cnt0[4] = {0, 0, 0, 0};
    const char *wrong[3] = {"XSG", "MXG", "MSX"};
    for (int i = 0; i < 3; i++)
    {
        size_t bad = forge_msg(resp, sizeof(resp), OPCUA_ID_READ_RESP, OPCUA_STATUS_GOOD, cnt0, sizeof(cnt0),
                               PROTO_FALSE, wrong[i]);
        TEST_ASSERT_TRUE(bad > 0);
        TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, bad, v, s, 1));
    }

    size_t sn =
        forge_msg(resp, sizeof(resp), OPCUA_ID_READ_RESP, OPCUA_STATUS_GOOD, cnt0, sizeof(cnt0), PROTO_FALSE, "MSG");
    TEST_ASSERT_TRUE(sn > 0);
    TEST_ASSERT_EQUAL_INT32(0, protocore_opcua_client_on_read(resp, sn, v, s, 1));
    uint8_t save = resp[4];
    resp[4] = (uint8_t)(save + 1);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, sn, v, s, 1));
    resp[4] = save;

    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_READ_RESP, OPCUA_STATUS_GOOD, cnt0, sizeof(cnt0), PROTO_TRUE, "MSG");
    TEST_ASSERT_TRUE(sn > 0);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, sn, v, s, 1));
}

void test_on_open_envelope_and_result_guards()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    uint8_t tiny[4] = {'O', 'P', 'N', 'F'};
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, tiny, sizeof(tiny)));

    uint8_t buf[192];
    size_t n =
        build_opn(buf, sizeof(buf), OPCUA_ID_OPEN_RESP, 0, PROTO_TRUE, OPCUA_STATUS_GOOD, PROTO_TRUE, PROTO_FALSE);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(protocore_opcua_client_on_open(&c, buf, n));
    TEST_ASSERT_EQUAL_UINT32(0x2A, c.channel_id);
    TEST_ASSERT_EQUAL_UINT32(0x2B, c.token_id);

    buf[1] = 'X';
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, buf, n));
    buf[1] = 'P';
    buf[2] = 'X';
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, buf, n));

    n = build_opn(buf, sizeof(buf), OPCUA_ID_OPEN_RESP, 0, PROTO_TRUE, OPCUA_STATUS_BAD_SERVICE_UNSUPPORTED, PROTO_TRUE,
                  PROTO_FALSE);
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, buf, n));

    n = build_opn(buf, sizeof(buf), OPCUA_ID_OPEN_RESP, 0, PROTO_TRUE, OPCUA_STATUS_GOOD, PROTO_FALSE, PROTO_FALSE);
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, buf, n));

    n = build_opn(buf, sizeof(buf), 0, 0, PROTO_TRUE, OPCUA_STATUS_GOOD, PROTO_TRUE, PROTO_TRUE);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, buf, n));
}

void test_on_open_rejects_message_size_mismatch(void)
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    uint8_t buf[192];
    size_t n =
        build_opn(buf, sizeof(buf), OPCUA_ID_OPEN_RESP, 0, PROTO_TRUE, OPCUA_STATUS_GOOD, PROTO_TRUE, PROTO_FALSE);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(protocore_opcua_client_on_open(&c, buf, n));

    c.channel_id = 0;
    c.token_id = 0;
    buf[4] = (uint8_t)(n + 1);
    TEST_ASSERT_FALSE(protocore_opcua_client_on_open(&c, buf, n));
    TEST_ASSERT_EQUAL_UINT32(0, c.channel_id);
    TEST_ASSERT_EQUAL_UINT32(0, c.token_id);
}

void test_parsers_reject_bad_service_result()
{
    const uint8_t cnt0[4] = {0, 0, 0, 0};
    const uint32_t bad = OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
    uint8_t resp[128];

    size_t sn = forge_msg(resp, sizeof(resp), OPCUA_ID_GET_ENDPOINTS_RESP, bad, cnt0, sizeof(cnt0), PROTO_FALSE, "MSG");
    TEST_ASSERT_TRUE(sn > 0);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_get_endpoints(resp, sn));

    OpcUaVariant v[1];
    uint32_t s[1];
    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_READ_RESP, bad, cnt0, sizeof(cnt0), PROTO_FALSE, "MSG");
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_read(resp, sn, v, s, 1));

    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_WRITE_RESP, bad, cnt0, sizeof(cnt0), PROTO_FALSE, "MSG");
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_write(resp, sn, s, 1));

    OpcUaClientRef refs[1];
    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_BROWSE_RESP, bad, cnt0, sizeof(cnt0), PROTO_FALSE, "MSG");
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_browse(resp, sn, refs, 1));

    const uint8_t ids[4] = {0x00, 0x11, 0x00, 0x22};
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_CREATE_SESSION_RESP, bad, ids, sizeof(ids), PROTO_FALSE, "MSG");
    TEST_ASSERT_FALSE(protocore_opcua_client_on_create_session(&c, resp, sn));
    TEST_ASSERT_EQUAL_UINT32(0x22, c.session_auth_id);
}

void test_parsers_reject_truncated_body()
{
    uint8_t resp[96];
    size_t sn =
        forge_msg(resp, sizeof(resp), OPCUA_ID_GET_ENDPOINTS_RESP, OPCUA_STATUS_GOOD, NULL, 0, PROTO_FALSE, "MSG");
    TEST_ASSERT_TRUE(sn > 0);
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_get_endpoints(resp, sn));

    const uint8_t neg[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_GET_ENDPOINTS_RESP, OPCUA_STATUS_GOOD, neg, sizeof(neg), PROTO_FALSE,
                   "MSG");
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_get_endpoints(resp, sn));

    OpcUaClient c;
    protocore_opcua_client_init(&c);
    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_CREATE_SESSION_RESP, OPCUA_STATUS_GOOD, NULL, 0, PROTO_FALSE, "MSG");
    TEST_ASSERT_FALSE(protocore_opcua_client_on_create_session(&c, resp, sn));

    const uint8_t one[4] = {1, 0, 0, 0};
    uint32_t got[2];
    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_WRITE_RESP, OPCUA_STATUS_GOOD, one, sizeof(one), PROTO_FALSE, "MSG");
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_write(resp, sn, got, 2));

    OpcUaClientRef refs[2];
    sn = forge_msg(resp, sizeof(resp), OPCUA_ID_BROWSE_RESP, OPCUA_STATUS_GOOD, one, sizeof(one), PROTO_FALSE, "MSG");
    TEST_ASSERT_EQUAL_INT32(-1, protocore_opcua_client_on_browse(resp, sn, refs, 2));
}

void test_on_read_optional_fields_and_limits()
{
    OpcUaReadRequest rr;
    memset(&rr, 0, sizeof(rr));
    rr.count = 4;

    OpcUaVariant sv[4];
    memset(sv, 0, sizeof(sv));
    sv[0].type = OPCUA_VAR_STRING;
    sv[0].str = NULL;
    sv[0].str_len = -1;
    sv[1].type = OPCUA_VAR_INT32;
    sv[1].i32 = 11;
    sv[2].type = OPCUA_VAR_INT32;
    sv[2].i32 = 22;

    uint8_t resp[256];
    size_t sn = protocore_opcua_build_read_response(&rr, sv, NULL, 1, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    OpcUaVariant cv[4];
    uint32_t cs[4];
    memset(cv, 0, sizeof(cv));
    TEST_ASSERT_EQUAL_INT32(4, protocore_opcua_client_on_read(resp, sn, cv, cs, 4));
    TEST_ASSERT_EQUAL_HEX8(OPCUA_VAR_STRING, cv[0].type);
    TEST_ASSERT_EQUAL_INT32(-1, cv[0].str_len);
    TEST_ASSERT_NULL(cv[0].str);
    TEST_ASSERT_EQUAL_INT32(22, cv[2].i32);
    TEST_ASSERT_EQUAL_HEX8(OPCUA_VAR_NULL, cv[3].type);

    memset(cv, 0, sizeof(cv));
    TEST_ASSERT_EQUAL_INT32(2, protocore_opcua_client_on_read(resp, sn, cv, cs, 2));
    TEST_ASSERT_EQUAL_INT32(11, cv[1].i32);
    TEST_ASSERT_EQUAL_HEX8(OPCUA_VAR_NULL, cv[2].type);

    TEST_ASSERT_EQUAL_INT32(4, protocore_opcua_client_on_read(resp, sn, NULL, cs, 4));
    TEST_ASSERT_EQUAL_INT32(4, protocore_opcua_client_on_read(resp, sn, cv, NULL, 4));
}

void test_on_write_limits_and_null_sink()
{
    OpcUaWriteRequest wr;
    memset(&wr, 0, sizeof(wr));
    wr.count = 3;
    uint32_t res[3] = {OPCUA_STATUS_GOOD, OPCUA_STATUS_BAD_NOT_WRITABLE, OPCUA_STATUS_BAD_NODE_ID_UNKNOWN};

    uint8_t resp[128];
    size_t sn = protocore_opcua_build_write_response(&wr, res, 1, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    uint32_t got[3] = {0, 0, 0};
    TEST_ASSERT_EQUAL_INT32(3, protocore_opcua_client_on_write(resp, sn, got, 3));
    TEST_ASSERT_EQUAL_HEX32(OPCUA_STATUS_BAD_NOT_WRITABLE, got[1]);

    memset(got, 0, sizeof(got));
    TEST_ASSERT_EQUAL_INT32(2, protocore_opcua_client_on_write(resp, sn, got, 2));
    TEST_ASSERT_EQUAL_HEX32(0, got[2]);

    TEST_ASSERT_EQUAL_INT32(3, protocore_opcua_client_on_write(resp, sn, NULL, 3));
}

void test_on_browse_limits_and_null_sink()
{
    OpcUaClient c;
    protocore_opcua_client_init(&c);
    uint8_t req[256];
    size_t rn = protocore_opcua_client_browse(&c, 0, 85, req, sizeof(req));
    TEST_ASSERT_TRUE(rn > 0);
    OpcUaBrowseRequest br;
    TEST_ASSERT_TRUE(protocore_opcua_parse_browse(req, rn, &br));

    uint8_t resp[512];
    size_t sn = protocore_opcua_build_browse_response(&br, srv_browse, 4, 0, resp, sizeof(resp));
    TEST_ASSERT_TRUE(sn > 0);

    OpcUaClientRef refs[2];
    memset(refs, 0, sizeof(refs));
    TEST_ASSERT_EQUAL_INT32(1, protocore_opcua_client_on_browse(resp, sn, refs, 1));
    TEST_ASSERT_EQUAL_STRING("Uptime", refs[0].browse_name);
    TEST_ASSERT_EQUAL_UINT32(0, refs[1].target_id);
    TEST_ASSERT_EQUAL_INT32(2, protocore_opcua_client_on_browse(resp, sn, NULL, 2));
}

void test_on_browse_display_name_empty_mask()
{
    uint8_t body[128];
    UaWriter b = {body, sizeof(body), 0, PROTO_TRUE};
    protocore_ua_w_i32(&b, 1);
    protocore_ua_w_u32(&b, OPCUA_STATUS_GOOD);
    protocore_ua_w_i32(&b, -1);
    protocore_ua_w_i32(&b, 1);
    protocore_ua_w_nodeid_numeric(&b, 0, OPCUA_REFTYPE_HAS_COMPONENT);
    protocore_ua_w_bool(&b, PROTO_FALSE);
    protocore_ua_w_nodeid_numeric(&b, 2, 9);
    protocore_ua_w_u16(&b, 2);
    protocore_ua_w_string(&b, "Inv", 3);
    protocore_ua_w_u8(&b, 0x00);
    protocore_ua_w_u32(&b, OPCUA_NODECLASS_OBJECT);
    protocore_ua_w_nodeid_numeric(&b, 0, OPCUA_TYPEDEF_BASE_OBJECT);
    TEST_ASSERT_TRUE(b.ok);

    uint8_t resp[256];
    size_t sn = forge_msg(resp, sizeof(resp), OPCUA_ID_BROWSE_RESP, OPCUA_STATUS_GOOD, body, b.n, PROTO_FALSE, "MSG");
    TEST_ASSERT_TRUE(sn > 0);

    OpcUaClientRef refs[2];
    TEST_ASSERT_EQUAL_INT32(1, protocore_opcua_client_on_browse(resp, sn, refs, 2));
    TEST_ASSERT_EQUAL_STRING("Inv", refs[0].browse_name);
    TEST_ASSERT_FALSE(refs[0].is_forward);
    TEST_ASSERT_EQUAL_UINT32(9, refs[0].target_id);
    TEST_ASSERT_EQUAL_UINT16(2, refs[0].target_ns);
    TEST_ASSERT_EQUAL_UINT32(OPCUA_NODECLASS_OBJECT, refs[0].node_class);
}

