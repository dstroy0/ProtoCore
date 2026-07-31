# PackML - the OMAC packaging-machine state model over HTTP

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_PACKML`

## What this example teaches

**PackML** (OMAC / ISA-88) is the state model packaging and process machines expose so a line
controller - usually over OPC UA - can command and observe any machine the same way. It is defined
by **ISA-TR88.00.02** ("Machine and Unit States: An Implementation Example of ISA-88", ISBN
978-1-64331-224-8). `services/packml` is a pure, fixed-BSS implementation of it: a state engine plus
an owned PackTags service. This sketch runs it as a simulated machine and serves it over HTTP.

The machine lives in one of **17 states**. Seven are stable **wait** states (the machine waits for a
command or condition): `Stopped(2)`, `Idle(4)`, `Execute(6)`, `Held(11)`, `Suspended(5)`,
`Complete(17)`, `Aborted(9)`. Ten are transient **acting** states that auto-advance when their action
finishes: `Resetting`, `Starting`, `Holding`, `Unholding`, `Suspending`, `Unsuspending`,
`Completing`, `Stopping`, `Aborting`, `Clearing`. The numbers are the ISA-TR88.00.02
`StateCurrent` wire values, so a PackTags `Status.StateCurrent` needs no translation.

Commands drive the transitions. **Stop** and **Abort** are near-universal (Abort from any state
into the fault branch; Stop into the shutdown branch); the rest are state-specific:

```
Stopped --Reset--> Resetting --SC--> Idle --Start--> Starting --SC--> Execute
Execute --(run done)--> Completing --SC--> Complete --Reset--> Resetting --> Idle
Execute --Hold--> Holding --SC--> Held --Unhold--> Unholding --SC--> Execute
Execute --Suspend--> Suspending --SC--> Suspended --Unsuspend--> Unsuspending --SC--> Execute
any --Abort--> Aborting --SC--> Aborted --Clear--> Clearing --SC--> Stopped
any --Stop--> Stopping --SC--> Stopped
```

(`SC` = State-Complete: the machine signals it finished the acting state's action.) The pure engine
is three functions plus a couple of predicates:

```cpp
PackMlState s = PackMlState::STOPPED;
s = pc_packml_command(s, PackMlCommand::RESET);   // -> Resetting  (invalid command -> unchanged)
s = pc_packml_state_complete(s);                  // -> Idle       (acting states auto-advance)
s = pc_packml_command(s, PackMlCommand::START);   // -> Starting
s = pc_packml_state_complete(s);                  // -> Execute
s = pc_packml_execute_complete(s);                // -> Completing (production run finished)
pc_packml_command_valid(s, PackMlCommand::HOLD);  // is a command legal here?
pc_packml_is_acting(s);                           // transient (auto-advancing) vs wait state?
```

On top, the **owned service** (`pc_packml_svc_*`) carries the PackTags a real machine reports: the
current state + unit mode, `MachSpeedActual`, the `ProdProcessedCount` / `ProdDefectiveCount`
production counters, and the `StateCurrentTime` / `AccTimeSinceReset` timers - so the sketch just
drives commands and reads a status snapshot.

## Run it

No hardware beyond the ESP32 - the "machine" is simulated in `loop()` (acting states complete after
a short dwell; a unit is produced each second while executing). Endpoints:

```
GET /packml            -> live PackTags JSON
GET /packml/reset      GET /packml/start     GET /packml/stop
GET /packml/hold       GET /packml/unhold    GET /packml/suspend
GET /packml/unsuspend  GET /packml/abort     GET /packml/clear   GET /packml/complete
```

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_PACKML=1" \
  --lib="." examples/L7-Application/PackML/PackML.ino
```

Flash, then drive it:

```
$ curl http://192.168.1.42/packml
{"stateCurrent":2,"state":"Stopped","unitMode":1,"machSpeedActual":0.0,...}
$ curl http://192.168.1.42/packml/reset      # Stopped -> Resetting -> (dwell) -> Idle
$ curl http://192.168.1.42/packml/start      # Idle -> Starting -> Execute
$ curl http://192.168.1.42/packml            # now Execute, counters climbing
{"stateCurrent":6,"state":"Execute","unitMode":1,"machSpeedActual":120.0,"prodProcessedCount":7,...}
$ curl http://192.168.1.42/packml/complete   # end the run -> Completing -> Complete
```

An illegal command in the current state answers `409` with `"accepted":false` and leaves the state
put (e.g. `start` while `Stopped`).

## Annotated source

The complete sketch is [PackML.ino](PackML.ino). The state engine + service are in
[src/services/machine_tool/packml/packml.h](../../../src/services/machine_tool/packml/packml.h); the 17-state model, the
`StateCurrent` numbers, the command set, and the transitions follow ISA-TR88.00.02. Pure codec,
host-tested (`native_packml`, 16 cases). The real deployment surfaces the same PackTags over OPC UA;
here they ride the library's HTTP server. Pairs with the [Umati](../Umati/README.md),
[HaasMdc](../HaasMdc/README.md), and Fanuc FOCAS machine-tool examples.
