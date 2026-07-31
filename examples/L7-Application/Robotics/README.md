# Robotics - OPC UA for Robotics MotionDeviceSystem model

**Layer:** L7-Application · **Flags:** `PC_ENABLE_OPCUA` + `PC_ENABLE_ROBOTICS`

Turns the board into an **OPC UA for Robotics** server. **OPC 40010-1** ("OPC UA for Robotics - Part 1:
Vertical Integration", VDMA / OPC Foundation) is the OPC UA companion specification for industrial
robots: it standardizes how a motion device reports its identity and live motion state so any OPC UA
client reads the **same structure across every vendor**. This example exposes that model over OPC UA on
TCP/4840 - the twin of example **Umati** (machine tools).

## What it exposes

A `MotionDeviceSystem` object under the Objects folder, with the standard OPC 40010-1 components
(faithful BrowseNames):

```
MotionDeviceSystem
  MotionDevices (Folder)
    MotionDevice     Manufacturer, Model, ProductCode, SerialNumber, MotionDeviceCategory
      ParameterSet   OnPath, InControl, SpeedOverride
      Axes (Folder)
        Axis_1..N    ActualPosition, ActualSpeed, ActualAcceleration, MotionProfile
  Controllers (Folder)
    Controller       Manufacturer, Model, ProductCode, SerialNumber
      Software       Manufacturer, Model, SoftwareRevision
  SafetyStates (Folder)
    SafetyState
      ParameterSet   OperationalMode, EmergencyStop, ProtectiveStop
```

You fill a `RoboticsMotionDeviceSystem` struct and refresh its live fields each `loop()` from your robot
I/O; the server reads straight out of it. `pc_robotics_install(&mds)` binds the struct and registers
the OPC UA Browse + Read resolvers - that is the whole wiring. The axis count is parametric
(`PC_ROBOTICS_AXES`, default 6): each bound axis becomes an `Axis_k` object with its four live variables.

## What you teach

One companion-spec model, one struct. The values carry the standard OPC 40010-1 BrowseNames, so a
generic OPC UA client discovers and reads them the same way it would on a real robot cell. It is a
**read-only monitoring model** (the robot reports, the client observes), built on the OPC UA Binary
server from example **OpcUa**.

## Configure and flash

1. Set `SSID` / `PASSWORD` at the top of `Robotics.ino`.
2. Build with both flags set (already in `build_opt.h` for the Arduino IDE):
    ```sh
    pio ci examples/L7-Application/Robotics --board esp32dev --lib "." \
      --project-option="build_flags=-DPC_ENABLE_OPCUA=1 -DPC_ENABLE_ROBOTICS=1"
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
        for system in await objects.get_children():            # MotionDeviceSystem
            print("System:", await system.read_browse_name())
            for folder in await system.get_children():         # MotionDevices / Controllers / SafetyStates
                print(" ", await folder.read_browse_name())

asyncio.run(main())
```

Browse from `Objects` to `MotionDeviceSystem`, then into `MotionDevices -> MotionDevice -> Axes ->
Axis_1 -> ActualPosition` and read it a few times - it tracks the simulated robot. UaExpert and
open62541 browse the same tree.

## Scope

A single MotionDevice / Controller / SafetyState and `PC_ROBOTICS_AXES` parametric axes are exposed (the
common embedded robot). The values are faithfully named per OPC 40010-1; full companion-spec
**TypeDefinitions**, **PowerTrains**, and advertising the Robotics namespace in the server's
`NamespaceArray` (which needs array-Variant support in the base OPC UA server) are a documented
follow-on - a generic OPC UA client still browses the structure and reads every value by BrowseName
today.
