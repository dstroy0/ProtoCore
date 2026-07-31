# Euromap77 - OPC UA for injection moulding machines (IMM <-> MES)

**Layer:** L7-Application · **Flags:** `PC_ENABLE_OPCUA` + `PC_ENABLE_EUROMAP77`

Turns the board into an **EUROMAP 77** injection-molding-machine (IMM) server. **EUROMAP 77** (published
as **OPC 40077**, with shared types/enums from **EUROMAP 83** / OPC 40083) is the OPC UA companion
specification for injection molding machines talking to a MES: it standardizes how an IMM reports its
identity, status, and the active job's live production counters so any OPC UA / MES client reads the
**same structure across every machine vendor**. This example exposes that model over OPC UA on TCP/4840 -
the plastics-industry twin of example **Umati** (machine tools) and **Robotics**.

## What it exposes

An `IMM_MES_Interface` object under the Objects folder, with the standard EUROMAP 77 components (faithful
BrowseNames):

```
IMM_MES_Interface
  MachineInformation  Manufacturer, Model, SerialNumber, ProductCode, HardwareRevision,
                      SoftwareRevision, DeviceRevision, ManufacturerUri
  MachineStatus       IsPresent, MachineMode
  Jobs
    ActiveJob         JobName, JobDescription, Material, ProductName, MouldId, ExpectedCycleTime,
                      NumCavities, NominalParts
    ActiveJobValues   JobCycleCounter, MachineCycleCounter, LastCycleTime, AverageCycleTime,
                      JobPartsCounter, JobGoodPartsCounter, JobBadPartsCounter, JobStatus
```

You fill an `EmImm` struct and refresh its live fields each `loop()` from your machine I/O; the server
reads straight out of it. `pc_em77_install(&imm)` binds the struct and registers the OPC UA Browse +
Read resolvers - that is the whole wiring. The production counters are faithful **UInt64** (EUROMAP 77
defines them 64-bit), served through the OPC UA Variant's UInt64 encoding.

## What you teach

One companion-spec model, one struct. The values carry the standard EUROMAP 77 BrowseNames, so a generic
OPC UA / MES client discovers and reads them the same way it would on a real molding machine. It is a
**read-only monitoring model** (the machine reports, the MES observes), built on the OPC UA Binary server
from example **OpcUa**.

## Configure and flash

1. Set `SSID` / `PASSWORD` at the top of `Euromap77.ino`.
2. Build with both flags set (already in `build_opt.h` for the Arduino IDE):
    ```sh
    pio ci examples/L7-Application/Euromap77 --board esp32dev --lib "." \
      --project-option="build_flags=-DPC_ENABLE_OPCUA=1 -DPC_ENABLE_EUROMAP77=1"
    ```
3. Flash, open Serial @115200, note the IP.

## Verify with an OPC UA client

Any OPC UA client works. With python `asyncua`:

```python
import asyncio
from asyncua import Client

async def main():
    async with Client("opc.tcp://<BOARD_IP>:4840") as c:
        for iface in await c.nodes.objects.get_children_descriptions():   # IMM_MES_Interface
            print("Interface:", iface.BrowseName.Name)

asyncio.run(main())
```

Browse from `Objects` to `IMM_MES_Interface`, then into `Jobs -> ActiveJobValues -> JobCycleCounter` and
read it a few times - it climbs as the simulated machine runs. UaExpert and open62541 browse the same
tree; the UInt64 counters decode without narrowing.

## Scope

One IMM with its active job is exposed (the common single-machine MES feed). The values are faithfully
named per EUROMAP 77; full companion-spec **TypeDefinitions**, methods, file/dataset transfer, and the
multi-cardinality arrays (InjectionUnits, Moulds) are a documented follow-on - a generic OPC UA client
still browses the structure and reads every value by BrowseName today. Like Umati / Robotics, one
companion model is active per server build (they share the single OPC UA read/browse handler).
