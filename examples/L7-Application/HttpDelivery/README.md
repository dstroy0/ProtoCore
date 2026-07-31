# HttpDelivery - an app shell that loads instantly and refreshes in the background

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_HTTP_DELIVERY`, `PC_ENABLE_FILE_SERVING`, `PC_ENABLE_RANGE`

## What this example teaches

A constrained device is a **slow origin**: it may be busy serving someone else, throttled, or
asleep. Three standards make that acceptable to a browser, and this wires all three together.

**1. Stale-while-revalidate (RFC 5861).** `set_cache_control_swr(60, 300)` emits
`Cache-Control: public, max-age=60, stale-while-revalidate=300` on every served file. For 60 s the
client just uses its copy; for 300 s after that it may serve the stale copy **and** refresh in the
background. The page never blocks waiting on this device. The header is built by the same
`pc_delivery_cache_control` core that backs the `pc_delivery_swr` decision, so the two cannot
drift apart.

**2. A service worker.** `pc_delivery_serve_sw()` registers two routes:

| Route            | Serves                                                  |
| ---------------- | ------------------------------------------------------- |
| `/sw.js`         | the worker (flash-resident asset)                       |
| `/precache.json` | the versioned manifest from `pc_delivery_sw_manifest()` |

The worker precaches the listed shell, then serves it stale-while-revalidate **client-side** - so a
repeat visit paints without touching the device at all, and still works while it is offline or
asleep. The cache is named after the manifest `version`, so bumping it invalidates the old shell
exactly once.

**3. Byte ranges (RFC 7233).** Already handled by the file server (`PC_ENABLE_RANGE`) - a client
fetches only the new tail of a growing log with `Range: bytes=N-` and gets a `206`. Note this lives
in `server/http_range.h`, which is the single owner of the range math (shared with the edge cache);
`services/http_delivery` deliberately does **not** carry a second parser.

## Verified on hardware

**HW-verified (2026-07-19)** on an **ESP32-P4** (Waveshare, wired Ethernet) serving real files from
its **SD card** - a 1000-byte file of repeating `0123456789`:

```
GET /files/pc_test.txt
  HTTP/1.1 200 OK
  Accept-Ranges: bytes
  Cache-Control: public, max-age=60, stale-while-revalidate=300

Range: bytes=10-19    -> 206, "0123456789", Content-Range: bytes 10-19/1000, Content-Length: 10
Range: bytes=-5       -> 206, "56789"          (suffix form, last N)
Range: bytes=995-     -> 206, "56789"          (open-ended tail)
Range: bytes=5000-6000-> 416 Range Not Satisfiable, Content-Range: bytes */1000

GET /precache.json -> {"version":"1.0.0","precache":["/","/index.html","/app.css"]}
GET /sw.js         -> 3350 bytes, Content-Type: application/javascript
```

Every range form returned the exact expected bytes, and the unsatisfiable one returned 416 with the
`bytes */size` form RFC 7233 requires rather than a truncated 206.

## Bumping the shell

`SHELL_VERSION` is the cache key. Change a precached file **and** bump the version, or clients keep
the old shell until their cache is evicted:

```cpp
static const char *const SHELL[] = {"/", "/index.html", "/app.css"};
static const char *SHELL_VERSION = "1.0.0";   // bump when SHELL contents change
```

The manifest is rebuilt per request, so the list and version can change at runtime with no stale
copy surviving. If it would not fit `PC_DELIVERY_MANIFEST_BUF`, the route answers **500** rather
than serving truncated JSON a worker would choke on.

## Tunables

| Flag                       | Default | Meaning                                |
| -------------------------- | ------- | -------------------------------------- |
| `PC_DELIVERY_PRECACHE_MAX` | 16      | most paths a manifest may list         |
| `PC_DELIVERY_MANIFEST_BUF` | 512     | buffer the manifest JSON is built into |

## Build footprint

| Board    | Flash           | RAM |
| -------- | --------------- | --- |
| ESP32-S3 | 967,120 B (73%) | -   |
| ESP32-P4 | 740,230 B (56%) | -   |

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_HTTP_DELIVERY=1 -DPC_ENABLE_FILE_SERVING=1 -DPC_ENABLE_RANGE=1" \
  --lib="." examples/L7-Application/HttpDelivery/HttpDelivery.ino
```
