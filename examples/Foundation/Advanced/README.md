# Advanced - a RESTful CRUD API

**Layer:** Foundation (tutorial) · **Build flags:** none (core features only)

## What this example teaches

This builds a complete REST API for a small in-memory "sensor database": list,
fetch-by-id, create, partial-update, and delete, with real HTTP semantics
(bearer-token auth, `Content-Type` checking, and a spread of status codes). It is
the step up from [Basic](../Basic): same three-part structure
(global server, `setup()` registers routes, `loop()` pumps `handle()`), but now
the handlers do meaningful work.

**A zero-heap "database".** There is no `malloc` and no growable container. The
table is a fixed array of structs with an `in_use` slot flag - the same
BSS-only pattern the library itself uses:

```cpp
struct SensorDevice { int id; char name[16]; float temperature; bool active; bool in_use; };
#define MAX_SENSORS 4
static SensorDevice sensor_db[MAX_SENSORS] = { /* ... */ };
```

Create finds the first `!in_use` slot; delete just clears the flag. Capacity is
fixed at compile time, so behavior is predictable under load.

**Method + path routing.** The same path is registered under different methods,
and a trailing `*` makes a prefix (wildcard) route for the per-id endpoints:

```cpp
server.on("/api/sensors", HttpMethod::HTTP_GET, handle_get_sensors);
server.on("/api/sensors/*", HttpMethod::HTTP_GET, handle_get_sensor_by_id);
server.on("/api/sensors", HttpMethod::HTTP_POST, handle_create_sensor);
server.on("/api/sensors/*", HttpMethod::HTTP_PATCH, handle_patch_sensor);
server.on("/api/sensors/*", HttpMethod::HTTP_DELETE, handle_delete_sensor);
```

The id is parsed out of the path by skipping the known prefix (`"/api/sensors/"`
is 13 characters), which is why the handlers test `strlen(req->path) <= 13`
before reading the id:

```cpp
int id = atoi(req->path + 13);   // /api/sensors/2  ->  2
```

**Request validation before mutation.** Write operations check an
`Authorization` header against an expected bearer token, and `POST`/`PATCH`
check the `Content-Type`, using `http_get_header()`:

```cpp
const char *auth_hdr = http_get_header(req, "Authorization");
return (auth_hdr && strcmp(auth_hdr, EXPECTED_TOKEN) == 0);
```

A missing/wrong token is a `401`; a wrong content type is a `400`; a full table
is a `409`; an unknown id is a `404`. This is the example's main lesson: pick the
right status code for each failure rather than returning `200` with an error
string.

**Zero-heap JSON scanning.** Incoming bodies are parsed by hand with small
`strstr`-based helpers (`json_get_string` / `json_get_float` / `json_get_bool`)
that find `"key":` and read the value in place - no allocation, no parser object:

```cpp
const char *ptr = strstr(json, "\"temp\":");   // locate the key
out_val = (float)atof(ptr + strlen("\"temp\":"));
```

> These hand-rolled scanners keep the example self-contained. For real parsing,
> the library ships a zero-heap JSON reader (`json_get_*`); see
> [Json](../../L6-Presentation/Json).

**Empty-body responses.** `DELETE` returns `204 No Content` with
`server.send_empty(slot_id, 204)` - a status line and headers, no body.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/Foundation/Advanced/Advanced.ino
```

Set `SSID`/`PASSWORD`, flash, then drive the API with `curl` (the write calls
need the token):

```sh
curl http://<ip>/api/sensors
curl "http://<ip>/api/sensors?active=1"          # query filter
curl http://<ip>/api/sensors/1
curl -X POST http://<ip>/api/sensors \
  -H "Authorization: Bearer secret_admin_token" \
  -H "Content-Type: application/json" \
  -d '{"name":"Attic","temp":19.5,"active":true}'
curl -X PATCH http://<ip>/api/sensors/1 \
  -H "Authorization: Bearer secret_admin_token" -d '{"temp":25.0}'
curl -X DELETE http://<ip>/api/sensors/2 -H "Authorization: Bearer secret_admin_token"
```

## Annotated source

The complete sketch ([Advanced.ino](Advanced.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h" // init_wifi_physical / wifi_ready

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// The bearer token required for any write/delete. Compared verbatim below.
static const char *EXPECTED_TOKEN = "Bearer secret_admin_token";

// Zero-allocation "database": a fixed array of slots. `in_use` marks a live row;
// `id` doubles as the slot index. No heap, no growth - capacity is MAX_SENSORS.
struct SensorDevice
{
    int id;
    char name[16];
    float temperature;
    bool active;
    bool in_use; // Slot flag
};

#define MAX_SENSORS 4
static SensorDevice sensor_db[MAX_SENSORS] = {
    {0, "Living Room", 22.4, true, true},
    {1, "Kitchen", 24.1, true, true},
    {2, "Garage", 15.8, false, true},
    {3, "", 0.0, false, false} // Empty slot (in_use = false)
};

// --- Helper Functions ---

/** @brief True if the request carries the exact expected Authorization header. */
bool is_authorized(const HttpReq *req)
{
    const char *auth_hdr = http_get_header(req, "Authorization");
    return (auth_hdr && strcmp(auth_hdr, EXPECTED_TOKEN) == 0);
}

// The three json_get_* helpers below all work the same way: build the `"key":`
// pattern, strstr() for it in the body, skip whitespace, then read the value in
// place. No heap and no parser object - just enough to read a flat JSON object.

/** @brief Scan a float value out of a simple JSON body (no heap). */
bool json_get_float(const char *json, const char *key, float &out_val)
{
    char key_pattern[32];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\":", key); // -> "temp":
    const char *ptr = strstr(json, key_pattern);
    if (!ptr)
        return false;
    ptr += strlen(key_pattern);          // step past the key
    while (*ptr == ' ' || *ptr == '\t')  // skip whitespace before the value
        ptr++;
    out_val = (float)atof(ptr);
    return true;
}

/** @brief Scan a string value out of a simple JSON body (no heap). */
bool json_get_string(const char *json, const char *key, char *out_buf, size_t max_len)
{
    char key_pattern[32];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\":", key);
    const char *ptr = strstr(json, key_pattern);
    if (!ptr)
        return false;
    ptr += strlen(key_pattern);
    while (*ptr == ' ' || *ptr == '\t' || *ptr == '"') // skip ws and the opening quote
        ptr++;
    size_t i = 0;
    // copy until the closing quote / separator / buffer limit; always terminate
    while (*ptr && *ptr != '"' && *ptr != ',' && *ptr != '}' && i < (max_len - 1))
        out_buf[i++] = *ptr++;
    out_buf[i] = '\0';
    return true;
}

/** @brief Scan a boolean value out of a simple JSON body (no heap). */
bool json_get_bool(const char *json, const char *key, bool &out_val)
{
    char key_pattern[32];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\":", key);
    const char *ptr = strstr(json, key_pattern);
    if (!ptr)
        return false;
    ptr += strlen(key_pattern);
    while (*ptr == ' ' || *ptr == '\t')
        ptr++;
    if (strncmp(ptr, "true", 4) == 0) { out_val = true;  return true; }
    if (strncmp(ptr, "false", 5) == 0) { out_val = false; return true; }
    return false;
}

// --- HttpRoute Handlers ---

/**
 * GET /api/sensors  - list all live sensors as a JSON array.
 * Optional query filter ?active=1 / ?active=0 selects by the active flag.
 */
void handle_get_sensors(uint8_t slot_id, HttpReq *req)
{
    const char *active_filter = http_get_query(req, "active");
    bool filter_by_active = (active_filter != nullptr);
    bool active_target_val = (filter_by_active && strcmp(active_filter, "1") == 0);

    char response_buf[512]; // fixed response buffer; sized for MAX_SENSORS rows
    int len = snprintf(response_buf, sizeof(response_buf), "[");

    bool first = true;
    for (int i = 0; i < MAX_SENSORS; i++)
    {
        if (!sensor_db[i].in_use)
            continue;                                       // skip empty slots
        if (filter_by_active && (sensor_db[i].active != active_target_val))
            continue;                                       // skip filtered-out rows
        if (!first)
            len += snprintf(response_buf + len, sizeof(response_buf) - len, ",");
        first = false;
        len += snprintf(response_buf + len, sizeof(response_buf) - len,
                        "{\"id\":%d,\"name\":\"%s\",\"temp\":%.1f,\"active\":%s}", sensor_db[i].id, sensor_db[i].name,
                        sensor_db[i].temperature, sensor_db[i].active ? "true" : "false");
    }
    snprintf(response_buf + len, sizeof(response_buf) - len, "]");
    server.send(slot_id, 200, "application/json", response_buf);
}

/**
 * GET /api/sensors/*  - fetch one sensor. The id follows the 13-char prefix
 * "/api/sensors/", so anything shorter is a malformed request (400).
 */
void handle_get_sensor_by_id(uint8_t slot_id, HttpReq *req)
{
    if (strlen(req->path) <= 13)
    {
        server.send(slot_id, 400, "text/plain", "Missing sensor ID");
        return;
    }
    int id = atoi(req->path + 13);
    if (id < 0 || id >= MAX_SENSORS || !sensor_db[id].in_use)
    {
        server.send(slot_id, 404, "application/json", "{\"error\":\"Sensor not found\"}");
        return;
    }
    char response_buf[192];
    snprintf(response_buf, sizeof(response_buf), "{\"id\":%d,\"name\":\"%s\",\"temp\":%.1f,\"active\":%s}",
             sensor_db[id].id, sensor_db[id].name, sensor_db[id].temperature, sensor_db[id].active ? "true" : "false");
    server.send(slot_id, 200, "application/json", response_buf);
}

/**
 * POST /api/sensors  - create a sensor. Requires the bearer token (401) and a
 * JSON content type (400); a full table is 409; bad JSON is 400; success is 201.
 */
void handle_create_sensor(uint8_t slot_id, HttpReq *req)
{
    if (!is_authorized(req))
    {
        server.send(slot_id, 401, "text/plain", "401 Unauthorized: Invalid token");
        return;
    }
    const char *content_type = http_get_header(req, "Content-Type");
    if (!content_type || strstr(content_type, "application/json") == nullptr)
    {
        server.send(slot_id, 400, "text/plain", "400 Bad Request: Content-Type must be application/json");
        return;
    }
    int empty_slot = -1; // find the first free row
    for (int i = 0; i < MAX_SENSORS; i++)
        if (!sensor_db[i].in_use) { empty_slot = i; break; }
    if (empty_slot == -1)
    {
        server.send(slot_id, 409, "application/json", "{\"error\":\"Database table full\"}");
        return;
    }
    const char *body = (const char *)req->body;
    char name[16] = "";
    float temp = 0.0;
    bool active = false;
    // All three fields must be present and parseable, or it is a 400.
    if (!json_get_string(body, "name", name, sizeof(name)) || !json_get_float(body, "temp", temp) ||
        !json_get_bool(body, "active", active))
    {
        server.send(slot_id, 400, "application/json", "{\"error\":\"Invalid JSON format or missing keys\"}");
        return;
    }
    sensor_db[empty_slot].id = empty_slot;
    strncpy(sensor_db[empty_slot].name, name, sizeof(sensor_db[empty_slot].name) - 1);
    sensor_db[empty_slot].temperature = temp;
    sensor_db[empty_slot].active = active;
    sensor_db[empty_slot].in_use = true;
    char response_buf[192];
    snprintf(response_buf, sizeof(response_buf), "{\"id\":%d,\"name\":\"%s\",\"status\":\"created\"}", empty_slot,
             sensor_db[empty_slot].name);
    server.send(slot_id, 201, "application/json", response_buf); // 201 Created
}

/**
 * PATCH /api/sensors/*  - partial update. Only the fields present in the body
 * are changed (temp and/or active). Requires auth.
 */
void handle_patch_sensor(uint8_t slot_id, HttpReq *req)
{
    if (!is_authorized(req))
    {
        server.send(slot_id, 401, "text/plain", "401 Unauthorized: Invalid token");
        return;
    }
    if (strlen(req->path) <= 13)
    {
        server.send(slot_id, 400, "text/plain", "Missing sensor ID");
        return;
    }
    int id = atoi(req->path + 13);
    if (id < 0 || id >= MAX_SENSORS || !sensor_db[id].in_use)
    {
        server.send(slot_id, 404, "application/json", "{\"error\":\"Sensor not found\"}");
        return;
    }
    const char *body = (const char *)req->body;
    float new_temp;
    bool new_active;
    if (json_get_float(body, "temp", new_temp))   // each field is optional
        sensor_db[id].temperature = new_temp;
    if (json_get_bool(body, "active", new_active))
        sensor_db[id].active = new_active;
    char response_buf[192];
    snprintf(response_buf, sizeof(response_buf), "{\"id\":%d,\"name\":\"%s\",\"temp\":%.1f,\"active\":%s}",
             sensor_db[id].id, sensor_db[id].name, sensor_db[id].temperature, sensor_db[id].active ? "true" : "false");
    server.send(slot_id, 200, "application/json", response_buf);
}

/**
 * DELETE /api/sensors/*  - free the slot. Returns 204 No Content (no body).
 */
void handle_delete_sensor(uint8_t slot_id, HttpReq *req)
{
    if (!is_authorized(req))
    {
        server.send(slot_id, 401, "text/plain", "401 Unauthorized: Invalid token");
        return;
    }
    if (strlen(req->path) <= 13)
    {
        server.send(slot_id, 400, "text/plain", "Missing sensor ID");
        return;
    }
    int id = atoi(req->path + 13);
    if (id < 0 || id >= MAX_SENSORS || !sensor_db[id].in_use)
    {
        server.send(slot_id, 404, "application/json", "{\"error\":\"Sensor not found\"}");
        return;
    }
    sensor_db[id].in_use = false;
    server.send_empty(slot_id, 204); // 204 No Content carries no body
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- PC Advanced REST CRUD Example ---");

    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Associated!");
    Serial.print("Local IP: ");
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.set_cors("*");

    // Same path, different methods; the "/*" routes match "/api/sensors/<id>".
    server.on("/api/sensors", HttpMethod::HTTP_GET, handle_get_sensors);
    server.on("/api/sensors/*", HttpMethod::HTTP_GET, handle_get_sensor_by_id);
    server.on("/api/sensors", HttpMethod::HTTP_POST, handle_create_sensor);
    server.on("/api/sensors/*", HttpMethod::HTTP_PATCH, handle_patch_sensor);
    server.on("/api/sensors/*", HttpMethod::HTTP_DELETE, handle_delete_sensor);

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("REST API server running on port 80");
    Serial.println("Admin token: 'Authorization: Bearer secret_admin_token'");
}

void loop()
{
    server.handle();
}
```
