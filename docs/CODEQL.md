# CodeQL static analysis

The C/C++ sources are scanned with [CodeQL](https://codeql.github.com/) for
security and quality issues.

## In CI

The [CodeQL workflow](../.github/workflows/codeql.yml) runs on every push and
pull request to `main` (and weekly). It is an **advanced** setup: the database is
built by tracing a host/native PlatformIO compile of the security-critical cores
(HTTP parser, WebSocket, multipart, base64 / JSON, the SSH crypto stack, SNMP,
and the protocol / codec services), then analyzed with the `security-and-quality`
query suite. Results appear under the repository's **Security -> Code scanning**
tab.

The workflow publishes a code-scanning check named **`CodeQL`** - require that
context in the branch-protection rule / ruleset for `main`. If GitHub "default"
code scanning is enabled for C/C++, disable it (Settings -> Code security -> Code
scanning) so this advanced workflow runs instead.

## Running locally

Create a database by tracing a native build, then analyze it. Use the same env
set as the workflow so local coverage matches CI - `native_codeql` is the full
app compiled with every feature flag on, so the integration paths (CSRF /
lockout / allowlist gates, permessage-deflate) and the new service modules are
traced, not just the per-feature cores:

```sh
codeql database create cpp-db --language=cpp \
  --command="pio test --without-testing -e native_codeql -e native_ssh \
    -e native_snmp_v3 -e native_inflate -e native_deflate -e native_coap \
    -e native_webdav -e native_modbus -e native_mqtt -e native_ws_client \
    -e native_http_client -e native_jwt"

codeql database analyze cpp-db \
  codeql/cpp-queries:codeql-suites/cpp-security-and-quality.qls \
  --format=sarif-latest --output=cpp.sarif --download
```

The native environments compile the host-portable code (parsers, codecs, the
software crypto paths). The ESP32-only wrappers - NVS, the mbedTLS / lwIP glue,
mDNS - are thin SDK adapters not traced here. A local run reproduces the hosted
GitHub CodeQL result byte-for-byte (same engine, suite, and traced build).

## Findings

_Latest scan: `security-and-quality` suite over the full CI env set
(`native_codeql` + 10 protocol / codec envs), 2026-06-26; 113 of 240 C/C++ files
analyzed. Verified byte-for-byte against the hosted GitHub CodeQL run._

**No security (CWE) findings.** The memory-safety queries (buffer overflow,
integer overflow, use-after-free, tainted path, invalid pointer deref, etc.) all
ran and reported nothing in `src/`. The remaining results are low-severity
quality / maintainability items - 11 in `src/`, the rest in tests, mocks, and the
vendored Unity headers.

### `src/` results and disposition

| Rule                             | Location                                                            | Disposition                                                                                                                                                                                                                            |
| -------------------------------- | ------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `cpp/useless-expression`         | `transport/listener.cpp:197`, `:370`                                | **False positive** - `xQueueSend` / `tcp_close` are no-op mocks in the host build (no side effects); on ESP32 they do real work.                                                                                                       |
| `cpp/unused-static-function`     | `presentation/http_parser/http_parser.cpp:40`                       | **False positive** - `fnv1a` is consumed at compile time by the `constexpr HASH_HTTP10` / `HASH_HTTP11` initializers, so no runtime call exists to see.                                                                                |
| `cpp/constant-comparison`        | `presentation/sse/sse.cpp:79`                                       | **Benign** - the first `pos < rem` guard is trivially true (`pos == 0`, buffer non-empty); kept for defensive consistency with the later, real checks.                                                                                 |
| `cpp/constant-comparison`        | `services/security/auth_lockout/auth_lockout.cpp:105`               | **Benign (defensive)** - the `dur > (0xFFFFFFFFu >> 1)` overflow guard in the backoff loop is dead under the default `PC_AUTH_LOCKOUT_MAX_MS` (the `>= MAX_MS` break fires first) but protects a pathologically large `MAX_MS` config. |
| `cpp/long-switch`                | `http_parser.cpp:187`, `ssh/ssh_server.cpp:39`, `websocket.cpp:279` | **Won't fix (style)** - protocol state machines / message dispatchers; a long `switch` is the clearest form.                                                                                                                           |
| `cpp/poorly-documented-function` | `services/fieldbus/modbus/modbus.cpp:112`                           | **Won't fix (style)** - `pc_modbus_process_pdu` is a function-code dispatcher (documented with a header comment); the heuristic dislikes the size / comment ratio.                                                                     |
| `cpp/loop-variable-changed`      | `services/file_transfer/webdav/webdav.cpp:166`                      | **Intentional** - the percent-decode loop does `p += 2` to consume a `%XX` escape; standard and correct.                                                                                                                               |

### Tests / mocks / vendored (not production code)

`cpp/irregular-enum-init` (vendored `Unity/unity_internals.h`),
`cpp/stack-address-escape` (`test/mocks/FS.h`, a test double),
`cpp/offset-use-before-range-check` and the remaining `cpp/unused-*` (under
`test/`). No action - test / third-party code.
