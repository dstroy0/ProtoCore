# SpaFallback - a single-page UI that still works when the single-page UI does not

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_SPA_ROUTER`

## What this example teaches

A device UI that is only a JavaScript bundle has a failure mode nobody plans for: the shell asset is
missing after a half-finished upload, the filesystem got wiped, the browser will not run scripts, or
the device came up degraded. On a machine you can actuate, "the page did not load" is not an
acceptable state - an operator still has to see what is happening and be able to stop it.

`pc_spa_route_ex()` routes for that:

| Path         | Action           | When                                                      |
| ------------ | ---------------- | --------------------------------------------------------- |
| `/api/...`   | `PASSTHROUGH`    | always - **including in fallback mode**                   |
| `/app.js`    | `SERVE_FILE`     | always - a missing asset is a real 404, not a rewrite     |
| `/dashboard` | `SERVE_SHELL`    | the SPA can serve it                                      |
| `/dashboard` | `SERVE_FALLBACK` | shell missing, client will not script, or device degraded |

Two of those rows are the whole design:

- **The API keeps passing through.** The fallback page's own STOP button posts to `/api/stop`. A
  fallback whose endpoints have stopped answering is decoration.
- **Assets are never rewritten.** Serving the fallback HTML to a request for `/style.css` would hand
  the browser a document where it asked for a stylesheet.

## Conditional UI streaming

The fallback page is a fragment table, each entry with a predicate:

```cpp
static const pc_ui_fragment HMI_FRAGMENTS[] = {
    {"head",     "<!doctype html>...<h1>Device HMI</h1>", nullptr},      // always
    {"degraded", "<p><b>DEGRADED MODE</b>...</p>",        when_degraded},
    {"alarm",    "<p style=\"color:red\">ALARM ACTIVE</p>", when_alarm},
    {"ok",       "<p>Status: normal</p>",                 when_ok},
    {"controls", "<form method=POST action=/api/stop>...", nullptr},
};
```

`pc_ui_stream_next()` emits only the fragments whose predicate holds, into a buffer of **any** size -
it resumes mid-fragment, so a page far larger than the buffer streams out in pieces. Predicates run
as the stream reaches each fragment, not all up front, so a long render reflects the state that holds
when it gets there.

This example deliberately streams through a 48-byte window to prove that.

## Verified on hardware

**HW-verified (2026-07-19)** on an **ESP32-S3**:

```
1. healthy                GET /          -> SHELL (js bundle)
                          GET /dashboard -> SHELL (js bundle)

2. non-scripting client   GET /?nojs=1   -> FALLBACK
                          panels: Status: normal

3. shell asset missing    GET /          -> FALLBACK

4. device degraded        GET /          -> FALLBACK
                          panels: DEGRADED MODE | Status: normal

5. API still passes through in fallback mode
                          GET /api/state -> {"alarm":false,"degraded":true,"shell":true}
                          GET /api/stop  -> alarm set
                          panels now:    DEGRADED MODE | ALARM ACTIVE

6. back to normal         GET /          -> SHELL (js bundle)
```

Step 5 is the one to read closely. Actuating through the API in fallback mode flipped the alarm, and
on the next render the `ALARM ACTIVE` panel **appeared** while `Status: normal` **disappeared** - two
predicates changing in opposite directions. That is conditional streaming doing its job, and it is
only observable because the API kept working when the SPA did not.

## Deciding `client_scripting`

There is no reliable header for "will this client run my JavaScript", so it is the application's
call. This example uses an explicit `?nojs=1` because that is testable; a real build might also weigh
the `Accept` header, a stored user preference, or a probe endpoint the shell hits on load. What the
router guarantees is only that whatever you decide is applied consistently.

## Routes

| HttpRoute    | What it does                                         |
| ------------ | ---------------------------------------------------- |
| `/`          | shell, or the fallback page                          |
| `/dashboard` | same (a client-side route)                           |
| `/?nojs=1`   | force the fallback                                   |
| `/api/state` | passes through, in either mode                       |
| `/api/stop`  | toggles the alarm - actuation from the fallback page |
| `/degrade`   | toggle the degraded flag                             |
| `/shell`     | toggle whether the shell asset is "present"          |

## Build footprint

| Board    | Flash           |
| -------- | --------------- |
| ESP32    | 754,565 B (57%) |
| ESP32-S3 | 908,447 B (69%) |

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SPA_ROUTER=1" \
  --lib="." examples/L7-Application/SpaFallback/SpaFallback.ino
```
