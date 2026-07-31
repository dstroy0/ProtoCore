# Building with ESP-IDF (CMake / idf.py)

ProtoCore ships three build paths that all compile the same `src/`:

| Toolchain           | Build file it uses                     | How you build           |
| ------------------- | -------------------------------------- | ----------------------- |
| Arduino IDE / CLI   | `library.properties`                   | open the `.ino`, Upload |
| PlatformIO          | `library.json`                         | `pio run`               |
| **ESP-IDF (CMake)** | `CMakeLists.txt` + `idf_component.yml` | `idf.py build`          |

This folder documents the **ESP-IDF** path. The library calls a few Arduino APIs (Wi-Fi, `millis`,
`Serial`), so under ESP-IDF it is built with **arduino-esp32 as a component** - the officially
supported way to use Arduino libraries from `idf.py`. The ESP Component Manager pulls arduino-esp32
in for you (it is declared in the repo-root `idf_component.yml`).

## What you need

- **ESP-IDF 5.3, 5.4, or 5.5** (arduino-esp32 3.3.x targets that range). Install it the usual way:
  <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/>.
- A target of **esp32** or **esp32s3** (what this library is tested on).

Two sdkconfig options are required for Arduino-as-a-component (already set in
[`minimal/sdkconfig.defaults`](minimal/sdkconfig.defaults)):

```
CONFIG_FREERTOS_HZ=1000     # Arduino needs a 1 kHz tick
CONFIG_AUTOSTART_ARDUINO=y  # runs setup()/loop() like an .ino sketch
```

## Build the examples in this repo

Two projects here build the library straight from this checkout (no download of the library itself):

- [`minimal/`](minimal/) - a one-route web server, the smallest possible sketch.
- [`Industrial_ESPIDF/`](Industrial_ESPIDF/) - an **industrial edge gateway**: a web dashboard **plus** a
  Modbus TCP slave **plus** an SNMP agent, one device fronting a control network. It also shows how to
  turn `PC_ENABLE_*` features on for an `idf.py` build (`add_compile_definitions`, see below).

Build `minimal/` (the industrial one is identical, just `cd` into its folder instead):

```sh
cd examples/esp-idf/minimal
idf.py set-target esp32          # or: idf.py set-target esp32s3
idf.py build                     # first build also fetches arduino-esp32 (a few minutes)
# set your Wi-Fi SSID/PASSWORD in main/main.cpp, then:
idf.py -p <PORT> flash monitor
```

You should see the device join Wi-Fi, print its IP, and serve `Hello ...` at `http://<ip>/`.

## Use the library in your own ESP-IDF project

**Option A - local checkout (what the example does).** Point IDF at the repo root, which is itself
the component, in your project's top-level `CMakeLists.txt` (before `project()`):

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/ProtoCore")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_app)
```

**Option B - from the ESP Component Registry** (once published):

```sh
idf.py add-dependency "dstroy0/ProtoCore^5.101.0"
```

Either way, have your `main` component require it:

```cmake
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "."
                       REQUIRES arduino-esp32 ProtoCore)
```

(`ProtoCore` is the component name; with Option A it is the repo-root folder
name, so keep the folder named that or derive it as the example's `main/CMakeLists.txt` shows.)

## Turning features on

Every subsystem is behind a `PC_ENABLE_*` flag and defaults off (only the core HTTP server is on).
Set the flags for a build by adding compile definitions in your project's top `CMakeLists.txt`, before
`project()`:

```cmake
add_compile_definitions(PC_ENABLE_WEBSOCKET=1 PC_ENABLE_TLS=1)
```

The full flag list and what each pulls in is in [`src/protocore_config.h`](../../src/protocore_config.h) and
the online docs. TLS, HTTP/2, and HTTP/3 additionally need PSRAM (build for `esp32s3` with PSRAM
enabled in `menuconfig`).
