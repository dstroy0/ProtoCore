# GraphQL - a GraphQL query endpoint

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_GRAPHQL`

## What this example teaches

GraphQL lets the client say exactly which fields it wants in one request. POST a
query to `/graphql` and the device resolves the selected fields and returns a
`{"data":{...}}` response shaped by the query - so the same endpoint serves a
minimal phone client and a rich dashboard without new routes.

**A resolver answers one scalar per dotted path.** Nested selections become dotted
paths (`net.rssi`); arguments are visible to the resolver for that field:

```cpp
static bool resolver(const char *path, const pc_gql_args *args, pc_gql_value *out) {
    if (!strcmp(path, "heap"))     { out->type = pc_gql_type::PC_GQL_INT; out->i = ESP.getFreeHeap(); return true; }
    if (!strcmp(path, "net.rssi")) { out->type = pc_gql_type::PC_GQL_INT; out->i = Physical.wifi->rssi();       return true; }
    if (!strcmp(path, "greet")) {
        const char *who = "?";
        pc_gql_arg_str(args, "name", &who);   // read the field's argument
        static char b[64]; snprintf(b, sizeof(b), "hi %s", who);
        out->type = pc_gql_type::PC_GQL_STR; out->s = b; return true;
    }
    return false; // -> null
}
```

A field with a sub-selection (like `net`) is an object the engine builds by
recursing into the resolver for each child; returning false yields `null`.

**One call executes a query.** The engine parses the body, walks the selection set,
and writes the response envelope:

```cpp
pc_gql_result rc = pc_graphql_execute(req->body, req->body_len, resolver, body, sizeof(body));
server.send(id, rc == pc_gql_result::PC_GQL_OK ? 200 : 400, "application/json", body);
```

It writes `{"data":...}` on success or `{"errors":...}` on a parse error;
answering `200` with the GraphQL error envelope is the conventional reply.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_GRAPHQL=1" \
  --lib="." examples/L7-Application/GraphQL/GraphQL.ino
```

```sh
curl -s --data '{ heap uptime net { rssi } }' http://<ip>/graphql
# {"data":{"heap":210000,"uptime":42,"net":{"rssi":-50}}}
curl -s --data '{ greet(name: "world") }' http://<ip>/graphql
# {"data":{"greet":"hi world"}}
```

## Annotated source

The complete sketch ([GraphQL.ino](GraphQL.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_GRAPHQL 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/graphql/graphql.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// One scalar per dotted path; nested objects recurse, arguments are per-field.
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
        out->i = Physical.wifi->rssi();
        return true;
    }
    if (!strcmp(path, "net.ip"))
    {
        static char ip[20];
        uint32_t v4 = Physical.link->egress_ip();
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
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/graphql", HttpMethod::HTTP_POST, [](uint8_t id, HttpReq *req) {
        char body[512];
        pc_gql_result rc = pc_graphql_execute((const char *)req->body, req->body_len, resolver, body, sizeof(body));
        // The engine writes {"data":...} on success or {"errors":...} on a parse
        // error; 200 with the GraphQL error envelope is the conventional reply.
        server.send(id, rc == pc_gql_result::PC_GQL_OK ? 200 : 400, "application/json", body);
    });

    server.begin(80);
}

void loop()
{
    server.handle();
}
```
