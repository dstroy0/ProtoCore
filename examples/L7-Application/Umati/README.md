# Umati - OPC UA for Machine Tools (umati) MachineTool model

**Layer:** L7-Application · **Flags:** `PC_ENABLE_OPCUA` + `PC_ENABLE_UMATI`

Turns the board into a **umati** machine-tool server. umati ("universal machine technology interface",
VDW / OPC Foundation, **OPC 40501-1**) is the OPC UA companion specification for machine tools: it
standardizes how a machine reports its identity and live state so any umati / OPC UA client reads the
**same structure across every vendor**. This example exposes that model over OPC UA on TCP/4840.

## What it exposes

A `MachineTool` object under the Objects folder, with the standard umati components (faithful
BrowseNames per OPC 40501-1):

```
MachineTool
  Identification   Manufacturer, Model, SerialNumber, YearOfConstruction, SoftwareRevision,
                   ProductInstanceUri
  Monitoring
    MachineTool    OperationMode, PowerOnDuration
    Channel        ChannelState, FeedOverride, RapidOverride, ActiveProgram
    Spindle        RotationSpeed, OverrideValue, IsRotating
    Axis_X/Y/Z     ActualPosition
  Production       ActiveProgram, ProducedPartCount
  Notification     ActiveMessage, Severity
```

You fill a `UmatiMachineTool` struct and refresh its live fields each `loop()` from your machine I/O;
the server reads straight out of it. `pc_umati_install(&mt)` binds the struct and registers the OPC UA
Browse + Read resolvers - that is the whole wiring.

## What you teach

One companion-spec model, one struct. The values carry the standard umati BrowseNames, so a generic
OPC UA client discovers and reads them the same way it would on a real CNC. It is a **read-only
monitoring model** (the machine reports, the client observes), built on the OPC UA Binary server from
example **OpcUa**.

## Configure and flash

1. Set `SSID` / `PASSWORD` at the top of `Umati.ino`.
2. Build with both flags set (already in `build_opt.h` for the Arduino IDE):
    ```sh
    pio ci examples/L7-Application/Umati --board esp32dev --lib "." \
      --project-option="build_flags=-DPC_ENABLE_OPCUA=1 -DPC_ENABLE_UMATI=1"
    ```
3. Flash, open Serial @115200, note the IP.

## Verify with an OPC UA client

Any OPC UA client works. With python `asyncua`:

```python
import asyncio
from asyncua import Client

async def main():
    async with Client("opc.tcp://<BOARD_IP>:4840") as c:
        objects = c.nodes.objects
        for machine in await objects.get_children():          # MachineTool
            print("Machine:", await machine.read_browse_name())
            for comp in await machine.get_children():          # Identification / Monitoring / ...
                print(" ", await comp.read_browse_name())

asyncio.run(main())
```

Browse from `Objects` to `MachineTool`, then into `Monitoring -> Spindle -> RotationSpeed` and read it
a few times - it tracks the simulated machine. UaExpert and the umati dashboard browse the same tree.

## Scope

A single Channel / Spindle and three linear axes are exposed (the common embedded machine). The values
are faithfully named per OPC 40501-1; full companion-spec **TypeDefinitions** and advertising the
MachineTool namespace in the server's `NamespaceArray` (which needs array-Variant support in the base
OPC UA server) are a documented follow-on - a generic OPC UA client still browses the structure and
reads every value by BrowseName today.
