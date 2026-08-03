# Pentesting suite - live adversarial tester

**What this is:** `pc_pentest.py`, a stdlib-only Python driver that attacks a
**running** ProtoCore device the way a hostile client would,
and asserts the guarantees that keep a deterministic, zero-heap device safe. It is
the on-device companion to the host-side parser fuzzer (`env:native_pentest`, see
[docs/PENTEST.md](../docs/PENTEST.md)).

> ## ⚖️ Authorized use only
>
> This is an **active network attack tool**. It sends malformed, oversized, and
> high-volume traffic to whatever target you name. Run it **only** against a device
> you **own** or have **explicit, written permission** to test. Unauthorized access
> to or interference with computer systems is a crime in most jurisdictions (US
> CFAA, UK Computer Misuse Act, and equivalents). You, the operator, are solely
> responsible for staying within the law and the scope of your authorization. The
> software is provided **as is, with no warranty and no liability**. We build
> defensive tooling - use it for good.
>
> The tool will not attack anything until you pass `--authorized` (or type the
> confirmation phrase at the prompt).

## Why a separate live tool

The host fuzzer (`native_pentest`) proves the **parsers** are safe in isolation: it
feeds malformed bytes straight into `http_parser_feed`, `coap_server_process`,
`snmp_agent_process`, the BER/CBOR readers, and so on, off-device, and checks that
no fixed buffer overflows and every input lands in a defined state. That is fast,
reproducible, and runs in CI.

But a real device is more than its parsers: it has a fixed connection pool, accept
and per-IP throttles, auth lockout, TLS, idle-timeout reaping, and a strict "no
heap after `begin()`" promise. Those only exist on the wire. `pc_pentest.py`
drives a flashed board and confirms the **live** behavior:

- **It fails closed** - oversized/malformed requests get 4xx, never a crash.
- **It stays up** - after every attack a legitimate request still succeeds.
- **Its footprint is fixed** - free heap does not drift across the whole run.
- **Its defenses engage** - throttles, lockout, and CSRF return what they should.

## The three oracles

Every attack is judged against the same invariants the device promises. Two of them
are checked automatically around **every** test, so you get them even on attacks
that do not assert anything protocol-specific:

```python
# Liveness oracle - a legitimate GET / must still get a response.
def is_alive(tgt: Target) -> bool:
    r = http_request(tgt, "GET", "/", timeout=tgt.timeout)
    return r is not None and r.status > 0
```

```python
# Heap-drift oracle (the determinism promise): sample free heap before and after
# each attack; flag any drop beyond a 1% / 512-byte noise floor.
if res.heap_before and res.heap_after:
    drift = res.heap_before - res.heap_after
    thresh = max(512, res.heap_before // 100)
    if drift > thresh:
        res.add_finding(Severity.HIGH,
                        f"free heap dropped {drift} bytes ...")
```

The heap sample is pulled from whatever observability endpoint the build exposes -
the tool tries them in order and scrapes the number out of JSON **or** Prometheus
text:

```python
HEALTH_PATHS = ("/health", "/stats", "/metrics", "/diag")
```

So if you build with `PC_ENABLE_GUARDRAILS` (`/health`), `PC_ENABLE_STATS`
(`/stats`), or `PC_ENABLE_METRICS` (`/metrics`), heap-drift detection lights up
for free. Without any of them the tool still runs; it just reports `heap=n/a`.

The third oracle, **fail-closed**, is asserted per-attack (e.g. an oversized
request line must return 414, a wrong password must never return 200).

## Feature-aware test selection

You do not want to run the SNMP fuzzer against a build with SNMP compiled out. The
tool picks the applicable attacks three ways, in increasing manual control:

1. **Auto-detect (`--diag`)** - read the device's own `/diag` endpoint
   (`PC_ENABLE_DIAG`) and enable the matching attacks. `/diag` reports a
   `features` map and a `config` block; the tool maps the booleans to flags and
   uses the sizes (e.g. `MAX_CONNS`) to tune the attack intensity:

    ```python
    DIAG_FEATURE_FLAG = {
        "websocket": "PC_ENABLE_WEBSOCKET",
        "sse": "PC_ENABLE_SSE",
        "multipart": "PC_ENABLE_MULTIPART",
        "file_serving": "PC_ENABLE_FILE_SERVING",
        "auth": "PC_ENABLE_AUTH",
    }
    ```

2. **Declare (`--flags`)** - list the flags your firmware was built with, for
   features `/diag` does not enumerate (CoAP, SNMP, Modbus, TLS, throttles, ...):

    ```sh
    --flags PC_ENABLE_AUTH,PC_ENABLE_ACCEPT_THROTTLE,PC_ENABLE_COAP
    ```

3. **Everything (`--all`)** - run every attack whose port is reachable, regardless
   of detected flags (unreachable protocols `SKIP` themselves cleanly).

On top of that, `--only` / `--skip` filter by **tag** or **name**:

```sh
--only http,auth        # only HTTP and auth-tagged attacks
--skip coap,snmp,udp    # leave the UDP protocols alone
--only http_slowloris   # one attack by name
```

An attack is included when all of its required features are in the enabled set:

```python
if not run_all and atk.features and not atk.features.issubset(ctx.enabled_features):
    continue   # this build does not have the feature -> skip
```

## The attack catalog

Each attack declares its category **tags** and the **feature(s)** it needs, then
gets a `TestResult` it fills with a status and any findings. They fall into groups:

| Group               | Attacks                                                                                           | Asserts                                           |
| :------------------ | :------------------------------------------------------------------------------------------------ | :------------------------------------------------ |
| HTTP limits         | `http_oversized_request_line`, `http_huge_content_length`, `http_header_flood`, `upload_oversize` | 414/413/clamp, bounded, no heap                   |
| HTTP framing        | `http_smuggling`                                                                                  | ambiguous TE/CL never accepted (no desync)        |
| HTTP availability   | `http_slowloris`, `http_conn_saturation`, `http_random_fuzz`                                      | pool caps, idle reap, server stays up             |
| Content / traversal | `http_path_traversal`, `range_fuzz`                                                               | no escape from the served root; 416 not overflow  |
| Defenses            | `accept_throttle_flood`, `per_ip_throttle`, `auth_bruteforce`, `auth_lockout`, `csrf_guard`       | throttle/RST, 401, 429+Retry-After, 403           |
| WebSocket / TLS     | `websocket_fuzz`, `tls_handshake`                                                                 | malformed upgrades survive; no deprecated TLS     |
| Binary protocols    | `coap_fuzz` (UDP), `snmp_fuzz` (UDP), `modbus_fuzz` (TCP), `opcua_fuzz` (TCP)                     | malformed PDUs answered or dropped, never a crash |

A representative module - path traversal - shows the shape: probe a set of
encoded escapes and only flag a finding if the filesystem actually leaks:

```python
@attack("http_path_traversal", ["http", "traversal"], ["PC_ENABLE_FILE_SERVING"], "http",
        "../ and encoded traversal must never escape the served root.")
def a_traversal(ctx, res):
    probes = ["/../../../../etc/passwd", "/%2e%2e/%2e%2e/%2e%2e/etc/passwd",
              "/..%2f..%2f..%2fetc%2fpasswd", "/....//....//etc/passwd"]
    for p in probes:
        r = http_request(ctx.target, "GET", p)
        if r and r.status == 200 and (b"root:" in r.body or b"/bin/" in r.body):
            res.fail(Severity.CRITICAL, "path traversal leaked filesystem content: " + p)
```

The slowloris module opens a wave of half-open connections, dribbles a byte into
each, and then asks the device whether it is still serving real traffic:

```python
for _ in range(n):
    s = socket.create_connection((tgt.host, tgt.http_port), timeout=tgt.timeout)
    s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n")  # headers, never terminated
    socks.append(s)
...
if not is_alive(tgt):
    res.fail(Severity.HIGH, "server stopped answering legitimate requests during slowloris")
```

`run_all` aside, every result also runs through the two global oracles in
`run_attack()`, so a heap leak or a wedge is caught even if the module itself was
satisfied.

## Reading the results

Findings carry a severity (`INFO` < `LOW` < `MEDIUM` < `HIGH` < `CRITICAL`). A test
is:

- `PASS` - the device behaved safely (failed closed / defense engaged / stayed up).
- `FAIL` - a finding: unsafe behavior (a crash, a leak, a defense that did not fire,
  a traversal that worked).
- `WARN` - it ran but the outcome could not be fully confirmed (e.g. the connection
  dropped with no status line, which can be a legitimately freed slot).
- `SKIP` - not applicable to this build, or the port was unreachable.
- `ERROR` - the test harness itself errored (reported, never aborts the run).

The process exit code is **non-zero when any finding exists**, so the tool drops
straight into a CI gate or a cron job.

## Intensity, determinism, and safety

- `--intensity {low,medium,high}` scales connection counts, iteration counts, and
  durations. Start `low`. `high` is genuinely aggressive (hundreds of connections,
  thousands of fuzz iterations).
- `--seed N` seeds the PRNG so a run - and any finding it produced - reproduces
  byte-for-byte.
- The tool throttles its own concurrency (bounded thread pools, per-socket
  timeouts) so it stresses the target deliberately rather than accidentally
  flooding your whole network.

## Install / requirements

Nothing to install: **Python 3.8+ standard library only** (`socket`, `ssl`,
`struct`, `threading`, `concurrent.futures`, `json`, `argparse`). This matches the
library's zero-dependency ethos and means it runs anywhere Python does.

## Usage

List everything the tool can do (no target needed):

```sh
python3 pc_pentest.py --list
```

Auto-detect features from `/diag` and run the applicable suite at medium intensity:

```sh
python3 pc_pentest.py --host 192.168.1.85 --diag --authorized
```

Declare your flags explicitly (when `/diag` is off or for non-HTTP features):

```sh
python3 pc_pentest.py --host 192.168.1.85 \
  --flags PC_ENABLE_AUTH,PC_ENABLE_AUTH_LOCKOUT,PC_ENABLE_ACCEPT_THROTTLE \
  --secure-path /admin --username admin --password hunter2 --authorized
```

Throw everything reachable at it, hard, and save a machine-readable report:

```sh
python3 pc_pentest.py --host 192.168.1.85 --all --intensity high \
  --json report.json --authorized
```

Target the binary protocols on their own ports:

```sh
python3 pc_pentest.py --host 192.168.1.85 \
  --flags PC_ENABLE_COAP,PC_ENABLE_SNMP,PC_ENABLE_MODBUS \
  --coap-port 5683 --snmp-port 161 --modbus-port 502 --authorized
```

### Key options

| Option                                                           | Meaning                                          |
| :--------------------------------------------------------------- | :----------------------------------------------- |
| `--host`                                                         | target IP / hostname (required unless `--list`)  |
| `--port` / `--https-port` / `--tls`                              | HTTP port, HTTPS port, run HTTP attacks over TLS |
| `--coap-port` / `--snmp-port` / `--modbus-port` / `--opcua-port` | protocol ports                                   |
| `--diag` / `--flags` / `--all`                                   | feature selection (auto / declared / everything) |
| `--only` / `--skip`                                              | filter by tag or attack name                     |
| `--intensity {low,medium,high}`                                  | how hard to push                                 |
| `--seed N`                                                       | PRNG seed for reproducibility                    |
| `--username` / `--password` / `--secure-path`                    | credentials + the route expected to need auth    |
| `--json FILE`                                                    | write a JSON report                              |
| `--authorized`                                                   | affirm permission (skips the interactive prompt) |
| `-v` / `--verbose`                                               | per-step logging                                 |

## Extending it

Adding an attack is one decorated function. Declare its tags and required feature,
fill the `TestResult`, and let the global oracles handle liveness + heap:

```python
@attack("my_attack", ["http", "fuzz"], ["PC_ENABLE_THING"], "http",
        "One-line description of what must hold.")
def a_my_attack(ctx, res):
    r = http_request(ctx.target, "GET", "/thing")
    res.detail = f"status={r.status if r else 'none'}"
    if r and r.status == 200 and b"secret" in r.body:
        res.fail(Severity.HIGH, "leaked secret via /thing")
```

The transport helpers (`http_request`, `tcp_send_recv`, `udp_send_recv`) and the
`Context` (target config, seeded RNG, detected features + sizes, intensity knobs)
are all available to it.

## See also

- [docs/PENTEST.md](../docs/PENTEST.md) - the full pentesting guide, including the
  host-side parser fuzzer (`env:native_pentest`) and sanitizer runs.
- [test/test_pentest/test_pentest.c](../test/test_pentest/test_pentest.c) - the
  off-device fuzz harness this tool complements.
