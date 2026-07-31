# Industrial gateway (ESP-IDF)

An **industrial edge gateway** built on ProtoCore with the **ESP-IDF** CMake
toolchain (`idf.py`) - the third build path alongside Arduino and PlatformIO. One device fronts a
control network with three industrial-facing services at once, all zero-heap:

| Face          | Protocol    | Port    | What it does                                          |
| ------------- | ----------- | ------- | ----------------------------------------------------- |
| Web dashboard | HTTP/1.1    | TCP 80  | a status page (uptime, free heap, live Modbus reg)    |
| Fieldbus      | Modbus TCP  | TCP 502 | coil/register model a SCADA/PLC client reads + writes |
| Management    | SNMP v1/v2c | UDP 161 | MIB-II system group + a private free-heap gauge       |

All three see one shared state: the loop publishes uptime into a Modbus **input register**, and the
dashboard + SNMP read the same live values. This is the shape of a real industrial edge device - a
fieldbus face for the PLC, an SNMP face for the NMS, and a small web face for humans.

> Modbus and SNMP have **no authentication or encryption**. Run them only on a trusted control
> network (and front them with the library's per-IP accept throttle).

## Build & flash

```sh
cd examples/esp-idf/Industrial_ESPIDF
idf.py set-target esp32          # or: idf.py set-target esp32s3
idf.py build                     # first build also fetches arduino-esp32 (a few minutes)
# set your Wi-Fi SSID/PASSWORD in main/main.cpp, then:
idf.py -p <PORT> flash monitor
```

Needs **ESP-IDF 5.3-5.5** (what arduino-esp32 3.3.x targets). The library is built with
**arduino-esp32 as a component**; the ESP Component Manager pulls it in automatically (declared in the
repo-root `idf_component.yml`). See [`../README.md`](../README.md) for the general ESP-IDF path.

## Try it

```sh
# Web dashboard
curl http://<ip>/

# Modbus TCP (with mbpoll) - read the uptime input register, write the setpoint holding register
mbpoll -m tcp -t 3 -r 1 -c 1 <ip>       # read input reg 0 (uptime seconds)
mbpoll -m tcp -t 4 -r 1 1234 <ip>       # write holding reg 0 (setpoint) = 1234

# SNMP (with net-snmp)
snmpwalk -v2c -c public <ip> system
snmpget  -v2c -c public <ip> 1.3.6.1.4.1.49374.10.0   # free heap (Gauge32)
```

## How the feature flags are set

Every subsystem is behind a `PC_ENABLE_*` flag that defaults **off**; only the core HTTP server is
on. The flags are checked inside the library's own `.cpp` sources (compiled by the repo-root
component), so a `#define` in `main.cpp` alone would **not** reach them - `pc_modbus_server_init()` /
`pc_snmp_agent_init()` would fail to link. The ESP-IDF way (the equivalent of Arduino's `build_opt.h` or
PlatformIO's `build_flags`) is a global compile definition in the project's top `CMakeLists.txt`,
before `project()`:

```cmake
add_compile_definitions(
    PC_ENABLE_MODBUS=1
    PC_ENABLE_SNMP=1
)
```

Add more services (OPC UA, MQTT/Sparkplug, MTConnect, ...) the same way - see the full flag list in
[`src/protocore_config.h`](../../../src/protocore_config.h). TLS, HTTP/2, and HTTP/3 additionally need PSRAM
(build for `esp32s3` with PSRAM enabled in `menuconfig`).
