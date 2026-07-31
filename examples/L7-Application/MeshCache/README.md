# MeshCache - share a warm edge cache across a fleet (mesh sibling cache)

This sketch extends the edge cache ([EdgeCache](../EdgeCache/)) so a **fleet
of boards shares one warm cache**. On a cold local miss a node asks its configured
**sibling peers** before it hits the origin, and pulls a fresh copy from whichever
peer already has the object - so the **origin is fetched once per fleet, not once
per node**. It is the classic "sibling cache" (Squid ICP / HTCP) pattern, on
ESP32s.

You need two boards, but no other special hardware: Part 1 stands up a throwaway
origin with one command.

## What is going on here? (the big picture)

Both boards cache the same origin under the same prefix (say `/cdn/`) and list
**each other** as peers. When a client asks node **B** for `GET /cdn/logo.png`
that B has never seen:

- **B misses locally**, so before touching the origin it sends a small
  content-addressed query to node **A** over the sibling link.
- If **A has a fresh copy**, it returns the object _plus its freshness/age_. B
  stores it and serves it with **`X-Cache: MESH`** - and **the origin never sees
  B**. Because the age travelled with the object (RFC 9111 age propagation), B
  serves it as fresh for its _remaining_ lifetime, no revalidation round trip.
- If **no peer has it**, B fetches the origin normally (`X-Cache: MISS`), exactly
  like the single-node cache.

A node answering a peer's query serves **only from its own local cache** - it never
re-queries its origin or its own peers. That one-hop rule is what keeps a fleet from
looping.

Wiring is the edge-cache setup plus three calls:

```cpp
pc_edge_cache_map("/cdn/", "http://192.168.1.60:8000"); // prefix -> origin
pc_edge_cache_enable(server);                            // install the cache
server.listen(MESH_PORT, ConnProto::PROTO_MESH);         // open the sibling port
pc_edge_cache_mesh_serve();                              // answer peers from the local cache
pc_edge_cache_add_peer("192.168.1.51", MESH_PORT);      // a sibling to ask on a miss
```

> **Pull only.** Nodes never push objects or invalidations to each other; a node
> only pulls, on demand, on a miss. A stale sibling copy simply expires by its own
> TTL and the puller re-checks freshness on arrival, so there is no cross-node
> consistency window to manage.
>
> **Security.** The sibling link is **plaintext** and any peer you list is trusted
> to serve you cache objects. Run the fleet on a trusted LAN; do not list a peer you
> do not control. Requests carrying `Authorization` are never cached or shared.

## What you will need

- **Two** ESP32 boards (node A and node B).
- Something to be the "origin" web server. Part 1 uses any PC / Raspberry Pi.

## Part 1 - Try it with no special hardware

On a machine on your network (note its IP with `hostname -I` / `ipconfig`), serve a
folder as the origin, and make a file to fetch:

```bash
echo "hello from the origin" > logo.txt
python3 -m http.server 8000      # serves the current folder on port 8000
```

Flash **both** boards (see Part 2), noting each board's IP from Serial. Then:

```bash
# 1) Warm node A: A fetches the origin once and caches it.
curl -D - http://<A_IP>/cdn/logo.txt        # X-Cache: MISS

# 2) Ask node B for the SAME object. B has never seen it, so B pulls it from A.
curl -D - http://<B_IP>/cdn/logo.txt        # X-Cache: MESH   <-- served from the peer!

# The origin's terminal shows exactly ONE request (from A). B never hit it.
curl http://<B_IP>/cache/stats              # {"...","mesh_hits":1,"mesh_misses":0,...}
```

Stop the Python origin after step 1 and step 2 **still succeeds** - proof B got the
object from A, not the origin. Ask B for an object neither node has and you will see
`mesh_misses` tick up and B fall through to the origin (`X-Cache: MISS`).

## Part 2 - Configure the sketch

Open `MeshCache.ino` and edit the lines marked **CHANGE ME**. Flash the **same
sketch to both boards**, giving each the _other_ board's IP as `PEER_IP`:

| Line       | Node A                 | Node B                 |
| ---------- | ---------------------- | ---------------------- |
| `SSID`     | your WiFi name         | your WiFi name         |
| `PASSWORD` | your WiFi password     | your WiFi password     |
| `ORIGIN`   | `http://<host>:<port>` | `http://<host>:<port>` |
| `PEER_IP`  | **node B's IP**        | **node A's IP**        |

`MESH_PORT` (default `7645`) must be the same on both. Open Serial Monitor @
**115200**; each board prints its IP and `mesh edge cache in front of ... (peer ...)`.

> Static IPs (or DHCP reservations) make this painless - otherwise re-check each
> board's IP after it joins and update the other's `PEER_IP`.

## Troubleshooting

- **B always MISSes to the origin, never MESH.** Check that (1) A actually has the
  object cached first (`GET /cache/stats` on A shows `hits`/`stores`), (2) B's
  `PEER_IP` is A's current IP and `MESH_PORT` matches, and (3) the object is
  cacheable (a `no-store` / `Vary: *` / non-`200` response is never shared).
- **`mesh_misses` climbs but `mesh_hits` stays 0.** The peer was reachable but did
  not have a fresh matching variant - warm the peer first, or the object may `Vary`
  on a request header that did not fit the snapshot (`PC_MESH_HDRS_MAX`); it falls
  back to the origin safely.
- **`pc_edge_cache_add_peer` returned false.** The peer table is full
  (`PC_MESH_MAX_PEERS`) or the host string is empty / too long
  (`PC_MESH_HOST_MAX`).
- **A cold miss feels slow.** A miss now tries each peer (in series, first hit wins)
  before the origin, bounded by `PC_MESH_QUERY_MS` per peer. Keep the peer list
  short and the timeout tight on a fast LAN.

## Going further

- **More than two nodes.** Call `pc_edge_cache_add_peer()` once per sibling (up to
  `PC_MESH_MAX_PEERS`); a miss asks them in order, first hit wins. Raise
  `PC_MESH_MAX_CONNS` if a node should answer several peers at once.
- **`Vary`.** The puller ships a snapshot of its request headers so the peer matches
  the right variant (e.g. `Vary: Accept-Encoding`); a header past
  `PC_MESH_HDRS_MAX` is dropped, degrading to a safe miss, never wrong content.
- **Bigger caches / S3.** The mesh reuses each fetch slot's origin buffer for the
  peer response (no extra per-slot buffer), but on a classic ESP32 the cache is still
  tuned conservatively - bump `PC_EDGE_CACHE_SLOTS` / `PC_EDGE_BODY_MAX` on an
  S3 / PSRAM board.
- **Not yet.** A TLS sibling link, UDP-broadcast peer auto-discovery, and push
  replication (with invalidation) are follow-ups; v1 is pull-only over a static,
  plaintext peer list.

## Build and run (PlatformIO)

The cache + mesh live inside the library, so the flags must reach the whole build:

```bash
pio ci examples/L7-Application/MeshCache \
  --board esp32dev \
  --lib "." \
  --project-option="build_flags=-DPC_ENABLE_EDGE_CACHE=1 -DPC_ENABLE_HTTP_CACHE=1 -DPC_ENABLE_HTTP_CLIENT=1 -DPC_ENABLE_EDGE_MESH=1 -DPC_EDGE_CACHE_SLOTS=2 -DPC_EDGE_FETCH_SLOTS=1 -DPC_MESH_MAX_PEERS=1"
```

(The Arduino IDE reads the flags from `build_opt.h` beside the sketch automatically.)

The last three flags shrink the cache to fit the classic ESP32's smaller DRAM (the defaults
are 4 slots / 2 fetch slots / 4 peers). Drop them, or raise the counts, on an S3 / PSRAM board.

---

## How it works under the hood (for the curious)

On a full local miss the cache does not go straight to the origin: it builds a
content-addressed mesh request (the object's SHA-256 key + a snapshot of the request
headers for `Vary` matching) and, as a **pre-origin phase of the same async fetch
slot**, queries each peer in turn over a `pc_client` connection to its
`ConnProto::PROTO_MESH` port. The peer's handler looks the key up in its **local**
store only, and if it finds a fresh variant it serializes the entry - the response
metadata + body (the same codec the SD/L2 tier uses) plus a small trailer carrying
the object's freshness and current age - and streams it back. The puller rehydrates
that into a fresh L1 entry, sets its age to the transferred age so
`edge_current_age()` keeps counting from where the peer left off, verifies it is
still fresh, and serves it with `X-Cache: MESH`. A peer `MISS` (or an exhausted peer
list) transitions the same slot to the ordinary origin fetch. The whole thing is
pumped from `server.handle()`, so a sibling query never stalls the worker. The wire
codec and the peer-query state machine are a pure engine unit-tested on the host
(`native_edge_mesh`); this glue binds its seams to the server, `pc_client`, and the
cache store.
