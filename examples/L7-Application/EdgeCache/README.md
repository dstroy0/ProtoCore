# EdgeCache - cache an upstream origin at the edge (CDN reverse-proxy cache)

This sketch turns the board into a **caching reverse-proxy edge**: a `GET`/`HEAD`
under a mapped path prefix is fetched from an upstream **origin** once, cached in
RAM, and every subsequent hit is served straight from the board - honoring
`Cache-Control` / `Expires` / `ETag` / `Last-Modified`, revalidating stale entries
with a conditional request, and **never stalling the server** (the origin fetch
runs asynchronously while the client request is suspended). It is the "be a CDN
edge, not just an origin" pattern.

You can try it with no special hardware: Part 1 stands up a throwaway origin with
one command.

## What is going on here? (the big picture)

The board maps a **prefix** (say `/cdn/`) to an **origin** (say a web server on
`192.168.1.60:8000`). When a client requests `GET /cdn/logo.png`:

- **Miss** - the board fetches `http://192.168.1.60:8000/cdn/logo.png` from the
  origin (asynchronously), stores it if it is cacheable, and serves it with
  `X-Cache: MISS`.
- **Hit** - the next request for the same object is answered from RAM with
  `X-Cache: HIT`, no origin round trip.
- **Stale** - once the object's freshness lifetime passes, the board revalidates
  it with the origin (`If-None-Match` / `If-Modified-Since`); a `304` refreshes it
  in place (`X-Cache: REVALIDATED`), a `200` replaces it.

Wiring is two calls:

```cpp
pc_edge_cache_map("/cdn/", "http://192.168.1.60:8000"); // prefix -> origin
pc_edge_cache_enable(server);                            // install the cache
```

The server's normal `handle()` loop drives the async origin fetch and the cached
send - no extra task. Anything the cache cannot serve (a non-cacheable response,
an origin error, an unmapped path) **fails open** to the origin or a `502`.

> **Security.** The cache fetches from whatever origin you map and serves it to
> anyone who can reach the board. Requests carrying `Authorization` are never
> cached. Only map trusted origins, and keep the board off untrusted networks.

## What you will need

- An ESP32 board.
- Something to be the "origin" web server. Part 1 uses any PC / Raspberry Pi.

## Part 1 - Try it with no special hardware

On a machine on your network (note its IP with `hostname -I` / `ipconfig`), serve
a folder as the origin:

```bash
python3 -m http.server 8000      # serves the current folder on port 8000
```

Point the sketch's `ORIGIN` at it (see Part 2: `http://<that-IP>:8000`). Flash the
board, then request an object **through the board** twice and watch `X-Cache`:

```bash
curl -D - http://<BOARD_IP>/cdn/somefile.txt   # 1st: X-Cache: MISS (fetched from origin)
curl -D - http://<BOARD_IP>/cdn/somefile.txt   # 2nd: X-Cache: HIT  (served from RAM)
curl http://<BOARD_IP>/cache/stats             # {"hits":1,"misses":1,...}
curl -X POST http://<BOARD_IP>/cache/purge     # {"purged":N}  -> next request misses again
```

The second request never touches the origin (stop the Python server after the
first and the second still succeeds from cache, until it expires).

## Part 2 - Configure the sketch

Open `EdgeCache.ino` and edit the lines marked **CHANGE ME**:

| Line       | Set it to                                                    |
| ---------- | ------------------------------------------------------------ |
| `SSID`     | your WiFi network name                                       |
| `PASSWORD` | your WiFi password                                           |
| `ORIGIN`   | the upstream base URL, `http://<host>:<port>` (plaintext v1) |

Flash and open Serial Monitor @ **115200**; you should see:

```
IP: 192.168.1.77
edge cache in front of http://192.168.1.60:8000
GET http://192.168.1.77/cdn/<path> - X-Cache: MISS then HIT
GET /cache/stats for counters; POST /cache/purge to invalidate /cdn/
```

## Troubleshooting

- **Every request is a MISS.** The response is not cacheable - the origin sent
  `Cache-Control: no-store`/`private`, `Vary: *`, a non-`200` status, or a body
  larger than `PC_EDGE_BODY_MAX`. Non-cacheable responses are proxied through
  uncached.
- **`502 Bad Gateway`.** The origin was unreachable or timed out
  (`PC_EDGE_FETCH_TIMEOUT_MS`), or the response exceeded the fetch buffer
  (`PC_EDGE_FETCH_BUF`). Check `ORIGIN` and that the origin is up.
- **`pc_edge_cache_map` returned false.** The map table is full
  (`PC_EDGE_MAP_MAX`), or you passed an `https://` origin (v1 is plaintext
  only - TLS origins are a follow-up).

## Going further

- **Size the cache.** Defaults are conservative to fit a classic ESP32:
  `PC_EDGE_CACHE_SLOTS` entries of up to `PC_EDGE_BODY_MAX` bytes each. On an
  S3 / PSRAM board, bump both up for a bigger, more useful cache.
- **`Vary`.** Responses that `Vary` on request headers (e.g. `Accept-Encoding`)
  are cached as separate variants and matched per request; `Vary: *` is uncached.
- **HTTPS origins.** Build with `-DPC_ENABLE_TLS=1 -DPC_ENABLE_EDGE_ORIGIN_TLS=1`
  and map an `https://` origin - it is fetched over TLS (S3 / PSRAM recommended: the
  TLS engine adds a ~48 KB arena). Verification is **off by default** (encrypt-only,
  no authentication); call `pc_edge_cache_set_origin_ca(pem, len)` to verify the
  origin's chain + hostname, or `pc_edge_cache_set_origin_pin(sha256)` to pin it.
  One TLS origin fetch runs at a time (the client-TLS session is shared with
  MQTTS / wss). Note: the trust store is shared across all client-TLS users, and on
  the espressif32-default mbedtls v2 an **IP-address** origin must use a CN-matching
  cert (IP-address SANs are not matched; DNS-named origins work normally).
- **Range / `206`.** Build with `-DPC_ENABLE_RANGE=1` and a cached object serves
  a `Range: bytes=...` request as `206 Partial Content` with a `Content-Range`
  header, streaming just the requested window; every full hit then advertises
  `Accept-Ranges: bytes`, and an unsatisfiable range answers `416`. Single-range
  only (a multi-range request falls back to a full `200`, per RFC 7233 §3.1).

    ```bash
    curl -r 10-40  http://<board>/cdn/test.txt   # -> 206, Content-Range: bytes 10-40/<len>
    curl -r 999999- http://<board>/cdn/test.txt  # -> 416 Range Not Satisfiable
    ```

- **Persistence (SD).** Build with `-DPC_ENABLE_DBM=1 -DPC_ENABLE_WAL=1` and
  the sketch mounts a WAL-backed dbm store on an SD card and binds it as an **L2
  tier**: an entry evicted from the RAM (L1) tier spills to SD, and after a reboot
  the log is replayed so the cached set survives. A persisted entry is served by
  revalidating it once (a cheap conditional GET → `304`) rather than re-downloading,
  because its freshness can't be trusted across a reboot. Only entries carrying a
  validator (`ETag` / `Last-Modified`) are spilled - those are exactly the ones a
  `304` can refresh. Raise `PC_DBM_VAL_MAX` toward `PC_EDGE_BODY_MAX + ~470`
  so full-size bodies fit one SD record; larger entries just stay L1-only. Watch
  `l2_spills` / `l2_promotes` in `GET /cache/stats`. The dbm index is fixed RAM
  (`PC_DBM_SLOTS` keys); on a classic ESP32 the default 256 will not fit
  alongside the cache + SD driver, so lower it (e.g. `-DPC_DBM_SLOTS=32`) or use
  an S3 / PSRAM board.

## Build and run (PlatformIO)

The cache lives inside the library, so the flags must reach the whole build:

```bash
pio ci examples/L7-Application/EdgeCache \
  --board esp32dev \
  --lib "." \
  --project-option="build_flags=-DPC_ENABLE_EDGE_CACHE=1 -DPC_ENABLE_HTTP_CACHE=1 -DPC_ENABLE_HTTP_CLIENT=1"
```

(The Arduino IDE reads the flags from `build_opt.h` beside the sketch automatically.)

---

## How it works under the hood (for the curious)

`pc_edge_cache_enable()` registers a **middleware** that runs before route
matching and installs an async-fetch **poll hook** in the HTTP slot pump. On a
request under a mapped prefix the middleware computes a canonical cache key
(method + host + path, SHA-256 digested) and looks it up. A fresh hit is served
immediately with the constant-memory chunked send-pump. A miss or a stale entry
opens a `pc_client` connection to the origin, sends the (conditional) request,
and **suspends** the client request - returning without a response. Each
`server.handle()` tick the poll hook drains the origin response into a bounded
buffer; when it is complete the entry is stored (or the 304 refreshes the stale
copy) and the cached body is streamed to the waiting client. The freshness /
validator / key / store logic is a pure engine unit-tested on the host
(`native_edge_cache`); this glue binds its seams to the server and `pc_client`.
