// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file GraphQL.ino
 * @brief GraphQL query endpoint (PC_ENABLE_GRAPHQL).
 *
 * POST a GraphQL query to /graphql; the device resolves the selected fields and
 * returns a `{"data":{...}}` response shaped exactly by the query - one endpoint,
 * the client decides what it needs.
 *
 *   curl -s --data '{ heap uptime net { rssi } }' http://<ip>/graphql
 *     -> {"data":{"heap":210000,"uptime":42,"net":{"rssi":-50}}}
 *   curl -s --data '{ greet(name: "world") }' http://<ip>/graphql
 *     -> {"data":{"greet":"hi world"}}
 *
 * The resolver answers one scalar per dotted path; a field with a sub-selection
 * (like `net`) is an object the engine builds by recursing. Arguments on a field
 * are visible to the resolver for that field and its descendants.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_GRAPHQL=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_GRAPHQL 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/graphql/graphql.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static bool resolver(const char *path, const pc_gql_args *args, pc_gql_value *out)
{
    if (!strcmp(path, "heap"))
    {
        out->type = pc_gql_type::PC_GQL_INT;
        out->i = ESP.getFreeHeap();
        return true;
    }
    if (!strcmp(path, "uptime"))
    {
        out->type = pc_gql_type::PC_GQL_INT;
        out->i = millis() / 1000;
        return true;
    }
    if (!strcmp(path, "net.rssi"))
    {
        out->type = pc_gql_type::PC_GQL_INT;
        out->i = pc_net_rssi();
        return true;
    }
    if (!strcmp(path, "net.ip"))
    {
        static char ip[20];
        uint32_t v4 = pc_net_egress_ip();
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (unsigned)(v4 & 0xFF), (unsigned)((v4 >> 8) & 0xFF),
                 (unsigned)((v4 >> 16) & 0xFF), (unsigned)((v4 >> 24) & 0xFF));
        out->type = pc_gql_type::PC_GQL_STR;
        out->s = ip;
        return true;
    }
    if (!strcmp(path, "greet"))
    {
        const char *who = "?";
        pc_gql_arg_str(args, "name", &who);
        static char b[64];
        snprintf(b, sizeof(b), "hi %s", who);
        out->type = pc_gql_type::PC_GQL_STR;
        out->s = b;
        return true;
    }
    return false; // -> null
}

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/graphql", HTTP_POST, [](uint8_t id, HttpReq *req) {
        char body[512];
        pc_gql_result rc = pc_graphql_execute((const char *)req->body, req->body_len, resolver, body, sizeof(body));
        // The engine writes {"data":...} on success or {"errors":...} on a parse
        // error; 200 with the GraphQL error envelope is the conventional reply.
        send_text(id, rc == pc_gql_result::PC_GQL_OK ? 200 : 400, "application/json", body);
    });

    begin_http(80, NULL);
}

void loop()
{
    handle();
}
