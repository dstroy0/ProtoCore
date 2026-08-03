// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// HTTP/3 dispatch-bridge test: proves an HTTP/3 request served by a *real PC route*. A
// QUIC client in the test completes the TLS 1.3 handshake and sends an HTTP/3 GET; pc_quic_server routes
// it to the reserved dispatch slot; PC::match_and_execute runs the registered "/hello"
// handler; and the handler's send() is routed back onto the request stream via pc_quic_server_respond.
// The whole Layer-7 app is built with PC_ENABLE_HTTP3=1, so this is the end-to-end wiring: the
// same routes serve HTTP/1.1, HTTP/2, and HTTP/3.
//
// The client extracts the server's ephemeral X25519 public key and its chosen connection ID from the
// ServerHello, so it needs no knowledge of the server's RNG.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_server.h"
#include "network_drivers/presentation/http/http3/quic_tp.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "network_drivers/tls/tls13_kdf.h"
#include "protocore.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static const uint8_t CERT[48] = {0x30, 0x2e, 0x02, 0x01, 0x02};
static uint8_t SERVER_SEED[32], CLIENT_PRIV[32];
static const uint8_t ODCID[8] = {0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8};
static const uint8_t CLIENT_SCID[4] = {0xc1, 0xc2, 0xc3, 0xc4};

// The route handler under test: it answers on the reserved HTTP/3 dispatch slot exactly as it would
// for HTTP/1.1 or HTTP/2 - send() routes the response to the right transport.
static bool g_handler_ran = false;
static void h_hello(uint8_t slot, HttpReq *req)
{
    g_handler_ran = true;
    TEST_ASSERT_EQUAL_STRING("/hello", req->path);
    send_text(slot, 200, "text/plain", "bridged h3");
}

// Captured server -> client datagrams from the current poll.
static uint8_t g_out[16][1500];
static size_t g_out_len[16];
static int g_out_n;
static void out_sink(void *, const uint8_t *dg, size_t len, const char *, uint16_t)
{
    if (g_out_n < 16 && len <= sizeof(g_out[0]))
    {
        memcpy(g_out[g_out_n], dg, len);
        g_out_len[g_out_n] = len;
        g_out_n++;
    }
}

static void fill()
{
    for (int i = 0; i < 32; i++)
    {
        SERVER_SEED[i] = (uint8_t)(0x80 + i);
        CLIENT_PRIV[i] = (uint8_t)(0x01 + i);
    }
    g_handler_ran = false;
    g_out_n = 0;
}

// --- packet helpers (client side), identical construction to the engine's -------------------
static void wr_pn(uint8_t *o, uint64_t pn, uint8_t pn_len)
{
    for (uint8_t i = 0; i < pn_len; i++)
    {
        o[i] = (uint8_t)(pn >> (8 * (pn_len - 1 - i)));
    }
}
static size_t build_long(uint8_t *out, size_t cap, uint8_t type, const uint8_t *dcid, uint8_t dcl, const uint8_t *scid,
                         uint8_t scl, uint64_t pn, const QuicPacketKeys *keys, const uint8_t *frames, size_t frame_len)
{
    uint8_t pn_len = pc_quic_pn_length(pn, -1);
    size_t p = pc_quic_build_long_header(out, cap, type, QUIC_VERSION_1, dcid, dcl, scid, scl, pn_len);
    if (type == QUIC_LP_INITIAL)
    {
        p += pc_quic_varint_encode(out + p, cap - p, 0);
    }
    p += pc_quic_varint_encode(out + p, cap - p, (uint64_t)pn_len + frame_len + 16);
    size_t pn_off = p;
    wr_pn(out + p, pn, pn_len);
    p += pn_len;
    memcpy(out + p, frames, frame_len);
    return pc_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, true);
}
static size_t build_short(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcl, uint64_t pn,
                          const QuicPacketKeys *keys, const uint8_t *frames, size_t frame_len)
{
    uint8_t pn_len = pc_quic_pn_length(pn, -1);
    out[0] = (uint8_t)(0x40 | (pn_len - 1));
    memcpy(out + 1, dcid, dcl);
    size_t pn_off = 1 + dcl;
    wr_pn(out + pn_off, pn, pn_len);
    memcpy(out + pn_off + pn_len, frames, frame_len);
    return pc_quic_packet_protect(out, cap, pn_off, pn_len, pn, frame_len, keys, false);
}
static size_t open_long(const uint8_t *dg, size_t len, const QuicPacketKeys *keys, uint8_t *plain, size_t *wire,
                        uint8_t *type)
{
    QuicLongHeader h;
    TEST_ASSERT_TRUE(pc_quic_parse_long_header(dg, len, &h));
    *type = h.type;
    size_t off = h.hdr_len;
    if (h.type == QUIC_LP_INITIAL)
    {
        uint64_t tl = 0;
        size_t c = 0;
        pc_quic_varint_decode(dg + off, len - off, &tl, &c);
        off += c + (size_t)tl;
    }
    uint64_t length = 0;
    size_t c = 0;
    pc_quic_varint_decode(dg + off, len - off, &length, &c);
    off += c;
    *wire = off + (size_t)length;
    static uint8_t work[2048];
    memcpy(work, dg, *wire);
    uint64_t pn = 0;
    return pc_quic_packet_unprotect(work, off, (size_t)length, 0, keys, true, plain, &pn);
}
static size_t open_short(const uint8_t *dg, size_t len, uint8_t dcl, const QuicPacketKeys *keys, uint8_t *plain)
{
    static uint8_t work[2048];
    memcpy(work, dg, len);
    uint64_t pn = 0;
    return pc_quic_packet_unprotect(work, 1 + dcl, len - (1 + dcl), 0, keys, false, plain, &pn);
}
static size_t extract_crypto(const uint8_t *p, size_t len, uint8_t *out)
{
    size_t off = 0, got = 0;
    while (off < len)
    {
        if (p[off] == QUIC_FT_PADDING)
        {
            off++;
            continue;
        }
        QuicFrame f;
        size_t n = pc_quic_frame_parse(p + off, len - off, &f);
        if (!n)
        {
            break;
        }
        off += n;
        if (f.type == QUIC_FT_CRYPTO)
        {
            memcpy(out + got, f.crypto.data, (size_t)f.crypto.length);
            got += (size_t)f.crypto.length;
        }
    }
    return got;
}
// The server's ephemeral X25519 public key is the key_share in its ServerHello: scan for the x25519
// group id (0x001d) + key length (0x0020) and take the 32 bytes that follow.
static bool server_pub_from_sh(const uint8_t *sh, size_t shl, uint8_t out[32])
{
    for (size_t i = 0; i + 4 + 32 <= shl; i++)
    {
        if (sh[i] == 0x00 && sh[i + 1] == 0x1d && sh[i + 2] == 0x00 && sh[i + 3] == 0x20)
        {
            memcpy(out, sh + i + 4, 32);
            return true;
        }
    }
    return false;
}
static size_t build_client_hello(uint8_t *out, const uint8_t client_pub[32], const uint8_t *tp, size_t tp_len)
{
    size_t p = 0;
    out[p++] = TLS_HS_CLIENT_HELLO;
    size_t hs = p;
    p += 3;
    out[p++] = 0x03;
    out[p++] = 0x03;
    for (int i = 0; i < 32; i++)
    {
        out[p++] = (uint8_t)i;
    }
    out[p++] = 0x00;
    out[p++] = 0x00;
    out[p++] = 0x02;
    out[p++] = 0x13;
    out[p++] = 0x01;
    out[p++] = 0x01;
    out[p++] = 0x00;
    size_t ext = p;
    p += 2;
    static const uint8_t sv[] = {0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04};
    static const uint8_t sg[] = {0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d};
    static const uint8_t sa[] = {0x00, 0x0d, 0x00, 0x04, 0x00, 0x02, 0x08, 0x07};
    static const uint8_t ks[] = {0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20};
    static const uint8_t al[] = {0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 'h', '3'};
    memcpy(out + p, sv, sizeof(sv));
    p += sizeof(sv);
    memcpy(out + p, sg, sizeof(sg));
    p += sizeof(sg);
    memcpy(out + p, sa, sizeof(sa));
    p += sizeof(sa);
    memcpy(out + p, ks, sizeof(ks));
    p += sizeof(ks);
    memcpy(out + p, client_pub, 32);
    p += 32;
    memcpy(out + p, al, sizeof(al));
    p += sizeof(al);
    out[p++] = 0x00;
    out[p++] = 0x39;
    out[p++] = (uint8_t)(tp_len >> 8);
    out[p++] = (uint8_t)tp_len;
    memcpy(out + p, tp, tp_len);
    p += tp_len;
    uint16_t el = (uint16_t)(p - ext - 2);
    out[ext] = (uint8_t)(el >> 8);
    out[ext + 1] = (uint8_t)el;
    uint32_t hl = (uint32_t)(p - hs - 3);
    out[hs] = (uint8_t)(hl >> 16);
    out[hs + 1] = (uint8_t)(hl >> 8);
    out[hs + 2] = (uint8_t)hl;
    return p;
}

// Scan captured datagrams for the 1-RTT stream-0 response HEADERS(:status 200) + DATA("bridged h3").
static bool response_ok(const QuicPacketKeys *ap_s)
{
    uint8_t plain[2048];
    for (int d = 0; d < g_out_n; d++)
    {
        if (pc_quic_is_long_header(g_out[d][0]))
        {
            continue;
        }
        size_t p2 = open_short(g_out[d], g_out_len[d], sizeof(CLIENT_SCID), ap_s, plain);
        if (p2 == SIZE_MAX)
        {
            continue;
        }
        size_t fo = 0;
        while (fo < p2)
        {
            if (plain[fo] == QUIC_FT_PADDING)
            {
                fo++;
                continue;
            }
            QuicFrame f;
            size_t n = pc_quic_frame_parse(plain + fo, p2 - fo, &f);
            if (!n)
            {
                break;
            }
            fo += n;
            if (!(f.type >= QUIC_FT_STREAM && f.type <= QUIC_FT_STREAM + 7 &&
                  f.stream.id == 0))
            {
                continue;
            }
            const uint8_t *sp = f.stream.data;
            size_t so = 0, sn = (size_t)f.stream.length;
            char status[8] = {0};
            bool data_ok = false;
            while (so < sn)
            {
                H3Frame hf;
                if (!pc_h3_frame_parse(sp + so, sn - so, &hf))
                {
                    break;
                }
                const uint8_t *hp = sp + so + hf.header_len;
                if (hf.type == H3_HEADERS)
                {
                    char sc[128];
                    struct E
                    {
                        char *s;
                    } e = {status};
                    pc_qpack_decode(
                        hp, (size_t)hf.length, sc, sizeof(sc),
                        [](void *c, const char *nm, size_t nl, const char *v, size_t vl) -> bool {
                            if (nl == 7 && memcmp(nm, ":status", 7) == 0)
                            {
                                memcpy(((E *)c)->s, v, vl);
                                ((E *)c)->s[vl] = 0;
                            }
                            return true;
                        },
                        &e);
                }
                else if (hf.type == H3_DATA)
                {
                    if (hf.length == 10 && memcmp(hp, "bridged h3", 10) == 0)
                    {
                        data_ok = true;
                    }
                }
                so += hf.header_len + (size_t)hf.length;
            }
            if (strcmp(status, "200") == 0 && data_ok)
            {
                return true;
            }
        }
    }
    return false;
}

void test_h3_request_served_by_route()
{
    fill();

    // Bring up an HTTP/3-only PC with one route.
    on_http("/hello", HTTP_GET, h_hello);
    TEST_ASSERT_TRUE(pc_h3_cert(CERT, sizeof(CERT), SERVER_SEED, 443));
    TEST_ASSERT_EQUAL_INT32(PC_OK, begin());
    pc_quic_server_set_out_sink_cb(out_sink, NULL);

    QuicInitialSecrets init;
    pc_quic_derive_initial_secrets(ODCID, sizeof(ODCID), &init);

    // Client Initial(ClientHello) padded to the 1200-byte minimum.
    QuicTransportParams ctp;
    pc_quic_tp_defaults(&ctp);
    ctp.initial_max_data = 524288;
    ctp.initial_max_sd_bidi_local = 131072;
    uint8_t ctpe[128];
    size_t ctpl = pc_quic_tp_encode(&ctp, ctpe, sizeof(ctpe));
    uint8_t client_pub[32];
    pc_x25519_base(client_pub, CLIENT_PRIV);
    uint8_t ch[512];
    size_t chl = build_client_hello(ch, client_pub, ctpe, ctpl);
    uint8_t frames[1200];
    size_t fl = pc_quic_build_crypto(frames, sizeof(frames), 0, ch, chl);
    memset(frames + fl, 0, 1100 - fl);
    fl = 1100;
    uint8_t dg[1500];
    size_t dl = build_long(dg, sizeof(dg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID,
                           sizeof(CLIENT_SCID), 0, &init.client, frames, fl);
    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(dg, dl, "192.0.2.10", 40000));
    service_once(); // -> pc_quic_server_poll(): opens the connection, emits the flight
    TEST_ASSERT_GREATER_THAN(0, g_out_n);

    // Learn the server's chosen SCID (for the 1-RTT short header) from the flight's long header.
    QuicLongHeader sh_hdr;
    TEST_ASSERT_TRUE(pc_quic_parse_long_header(g_out[0], g_out_len[0], &sh_hdr));
    uint8_t server_scid[QUIC_MAX_CID_LEN];
    uint8_t server_scid_len = sh_hdr.scid_len;
    memcpy(server_scid, sh_hdr.scid, server_scid_len);

    // Derive the client-side handshake + 1-RTT keys from the captured flight.
    uint8_t plain[2048], sh[512], hsf[1024];
    size_t wire = 0;
    uint8_t ty = 0;
    size_t pt = open_long(g_out[0], g_out_len[0], &init.server, plain, &wire, &ty);
    size_t shl = extract_crypto(plain, pt, sh);
    uint8_t server_pub[32], ecdhe[32];
    TEST_ASSERT_TRUE(server_pub_from_sh(sh, shl, server_pub));
    pc_x25519(ecdhe, CLIENT_PRIV, server_pub);
    pc_sha256_ctx t;
    uint8_t chsh[32], chsf[32];
    pc_sha256_init(&t);
    pc_sha256_update(&t, ch, chl);
    pc_sha256_update(&t, sh, shl);
    {
        pc_sha256_ctx tmp = t;
        pc_sha256_final(&tmp, chsh);
    }
    Tls13KeySchedule cks;
    pc_tls13_ks_early(&TLS13_KDF, &cks);
    pc_tls13_ks_handshake(&cks, ecdhe, chsh);
    QuicPacketKeys hs_s, hs_c, ap_s, ap_c;
    pc_quic_keys_from_secret(cks.server_hs_traffic, &hs_s);
    pc_quic_keys_from_secret(cks.client_hs_traffic, &hs_c);
    size_t hw = 0;
    uint8_t hty = 0;
    size_t hpt = open_long(g_out[0] + wire, g_out_len[0] - wire, &hs_s, plain, &hw, &hty);
    size_t hsfl = extract_crypto(plain, hpt, hsf);
    pc_sha256_update(&t, hsf, hsfl);
    pc_sha256_final(&t, chsf);
    pc_tls13_ks_master(&cks, chsf);
    pc_quic_keys_from_secret(cks.server_ap_traffic, &ap_s);
    pc_quic_keys_from_secret(cks.client_ap_traffic, &ap_c);

    // Client Initial(ACK) + Handshake(ACK + Finished) -> server completes the handshake.
    uint8_t ifr[64];
    size_t ifl = pc_quic_build_ack(ifr, sizeof(ifr), 0, 0, 0);
    uint8_t idg[256];
    size_t idl = build_long(idg, sizeof(idg), QUIC_LP_INITIAL, ODCID, sizeof(ODCID), CLIENT_SCID,
                            sizeof(CLIENT_SCID), 1, &init.client, ifr, ifl);
    uint8_t cfin[36] = {TLS_HS_FINISHED, 0x00, 0x00, 0x20};
    pc_tls13_finished_mac(&TLS13_KDF, cks.client_hs_traffic, chsf, cfin + 4);
    uint8_t hfr[64];
    size_t hfl = pc_quic_build_ack(hfr, sizeof(hfr), 0, 0, 0);
    hfl += pc_quic_build_crypto(hfr + hfl, sizeof(hfr) - hfl, 0, cfin, sizeof(cfin));
    size_t hdl = build_long(idg + idl, sizeof(idg) - idl, QUIC_LP_HANDSHAKE, ODCID, sizeof(ODCID),
                            CLIENT_SCID, sizeof(CLIENT_SCID), 0, &hs_c, hfr, hfl);
    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(idg, idl + hdl, "192.0.2.10", 40000));
    service_once();

    // Client HTTP/3 GET on request stream 0 (1-RTT); DCID is the server's SCID.
    uint8_t block[128];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":method", 7, "GET", 3);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":path", 5, "/hello", 6);
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":authority", 10, "h3.test", 7);
    uint8_t h3req[256];
    size_t h3l = pc_h3_build_headers(h3req, sizeof(h3req), block, bp);
    uint8_t sfr[300];
    size_t sfrl = pc_quic_build_stream(sfr, sizeof(sfr), 0, 0, h3req, h3l, true);
    uint8_t s1[512];
    size_t s1l = build_short(s1, sizeof(s1), server_scid, server_scid_len, 0, &ap_c, sfr, sfrl);

    g_out_n = 0;
    TEST_ASSERT_TRUE(pc_quic_server_ingest(s1, s1l, "192.0.2.10", 40000));
    service_once(); // -> pc_quic_server_poll -> dispatch_h3_request -> h_hello -> send -> respond

    TEST_ASSERT_TRUE(g_handler_ran);                               // the registered route actually ran
    TEST_ASSERT_TRUE(response_ok(&ap_s));                          // its 200 + body came back on the request stream
    TEST_ASSERT_EQUAL_UINT8(0, conn_pool[PC_H3_DISPATCH_SLOT].h3); // dispatch slot released

    pc_quic_server_stop();
}

// begin() shape-checks that the HTTP/3 end-to-end path never takes: a server with no listeners and no
// HTTP/3 is rejected; a plain TCP server (a listener, no HTTP/3) is accepted and its service_once() gate
// runs with pc_h3_running still false. Must run BEFORE the h3 test, which sets pc_h3_running true for
// the rest of the process. Separate instances so the global h3 server the other tests use is untouched.
void test_h3_begin_edges()
{
    // No listeners, no HTTP/3 -> rejected (listener_count==0 && !_h3_enabled true side).
    TEST_ASSERT_EQUAL_INT32((int32_t)PC_ERR_NO_LISTENERS, begin());

    // A TCP listener, no HTTP/3 -> accepted (listener_count!=0) and the h3 bind is skipped (_h3_enabled false).
    TEST_ASSERT_EQUAL_INT32((int32_t)PC_OK, begin(8080));

    // Pump the gate with h3 not running (pc_h3_running still false here) and on a non-zero worker id ->
    // the otherwise-untaken sides of `worker_id == 0 && s_inst.pc_h3_running` in service_once().
    service_once(0);
    service_once(1);
}

// A route whose handler answers with a no-body response, exercising send_empty()'s HTTP/3 sink path.
static bool g_empty_ran = false;
static void h_empty(uint8_t slot, HttpReq *)
{
    g_empty_ran = true;
    send_empty(slot, 204); // -> conn->pc_resp_sink -> pc_quic_server_respond (send_empty sink branch)
}

// Directly drive PC::dispatch_h3_request / pc_h3_cert / send_empty over the edge inputs that the
// full end-to-end path never produces: bad cert args, oversized method/path/query/authority, a query
// string, an empty/absent :authority, and a request body. There is no live QUIC connection here, so
// pc_quic_server_respond() harmlessly returns false for the bogus conn id; we only care that the
// dispatch bookkeeping runs every branch.
void test_h3_dispatch_edges()
{
    fill();
    on_http("/hello", HTTP_GET, h_hello);
    on_http("/empty", HTTP_GET, h_empty);

    const uint32_t CID = 0xDEADBEEF; // no such QUIC slot -> respond() returns false, no transport needed

    // pc_h3_cert argument validation: each bad arg returns false (covers the || guard + return false).
    TEST_ASSERT_FALSE(pc_h3_cert(NULL, sizeof(CERT), SERVER_SEED, 443));
    TEST_ASSERT_FALSE(pc_h3_cert(CERT, 0, SERVER_SEED, 443));
    TEST_ASSERT_FALSE(pc_h3_cert(CERT, sizeof(CERT), NULL, 443));

    // Oversized method (> PC_METHOD_BUF_SIZE) -> truncated; unknown method -> 501 via sink.
    char long_method[32];
    memset(long_method, 'A', sizeof(long_method) - 1);
    long_method[sizeof(long_method) - 1] = 0;
    dispatch_h3_request(CID, 0, long_method, "/hello", "h3.test", NULL, 0);

    // Path carrying a short query string: '?' split + query copied (short, no truncation).
    g_handler_ran = false;
    dispatch_h3_request(CID, 0, "GET", "/hello?a=1&b=2", "h3.test", NULL, 0);
    TEST_ASSERT_TRUE(g_handler_ran); // route still matched on the query-stripped path

    // Oversized query (> MAX_QUERY_LEN) -> query truncated.
    char long_query[256];
    long_query[0] = '/';
    memcpy(long_query + 1, "hello?", 6);
    memset(long_query + 7, 'q', sizeof(long_query) - 8);
    long_query[sizeof(long_query) - 1] = 0;
    dispatch_h3_request(CID, 0, "GET", long_query, "h3.test", NULL, 0);

    // Oversized path (> MAX_PATH_LEN, no query) -> path truncated; no route matches -> 404 via sink.
    char long_path[256];
    long_path[0] = '/';
    memset(long_path + 1, 'p', sizeof(long_path) - 2);
    long_path[sizeof(long_path) - 1] = 0;
    dispatch_h3_request(CID, 0, "GET", long_path, "h3.test", NULL, 0);

    // Oversized :authority (> MAX_VAL_LEN) -> Host header value truncated.
    char long_auth[128];
    memset(long_auth, 'h', sizeof(long_auth) - 1);
    long_auth[sizeof(long_auth) - 1] = 0;
    dispatch_h3_request(CID, 0, "GET", "/hello", long_auth, NULL, 0);

    // Absent and empty :authority -> the authority guard's short-circuit branches.
    dispatch_h3_request(CID, 0, "GET", "/hello", NULL, NULL, 0);
    dispatch_h3_request(CID, 0, "GET", "/hello", "", NULL, 0);

    // Request body present (non-empty) -> body copied into the slot's HttpReq.
    static const uint8_t BODY[16] = {'h', 'e', 'l', 'l', 'o', '-', 'b', 'o', 'd', 'y', '!', '!', '!', '!', '!', '!'};
    dispatch_h3_request(CID, 0, "GET", "/hello", "h3.test", BODY, sizeof(BODY));
    // Non-null body pointer but zero length -> the body_len half of the guard is false.
    dispatch_h3_request(CID, 0, "GET", "/hello", "h3.test", BODY, 0);

    // A handler that calls send_empty() -> exercises send_empty()'s pc_resp_sink branch.
    g_empty_ran = false;
    dispatch_h3_request(CID, 0, "GET", "/empty", "h3.test", NULL, 0);
    TEST_ASSERT_TRUE(g_empty_ran);

    // send() / send_empty() on a slot with NO HTTP/3 sink -> the non-sink (TCP-transport) branch of the
    // pc_resp_sink guard. The slot has no pcb, so both fall straight through to http_reset().
    conn_pool[0].pc_resp_sink = NULL;
    conn_pool[0].pcb = NULL;
    send_text(0, 200, "text/plain", "x"); // send(): pc_resp_sink == NULL
    send_empty(0, 204);                   // send_empty(): pc_resp_sink == NULL
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_h3_begin_edges); // first: before the h3 test latches pc_h3_running true
    RUN_TEST(test_h3_request_served_by_route);
    RUN_TEST(test_h3_dispatch_edges);
    return UNITY_END();
}
