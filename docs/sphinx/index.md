# ProtoCore

A deterministic, OSI-layered protocol core - RFC-compliant HTTP/1.1, HTTP/2 and HTTP/3, plus a large
opt-in protocol suite (WebSocket, SSE, MQTT, CoAP, SNMP, SSH, OPC UA, Modbus, and many more), all
zero-heap and fixed-buffer, so behavior is bounded and repeatable rather than typical-case.

**Where it runs today.** Constrained hardware and POSIX hosts from the same sources. Silicon support is
Espressif, both Xtensa (ESP32-S3) and RISC-V (ESP32-C6, ESP32-P4), hardware-verified on those parts; the
host build is exercised by the full native test suite.

**Landing next.** The core is going architecture-agnostic behind a vendor-neutral HAL that auto-configures
per variant capability, extending it to **Arm**, **TI C2000**, and RISC-V beyond Espressif. The seams are
designed and the work is active; until a target appears in the list above it is not shipped yet. See the
[roadmap](https://github.com/dstroy0/ProtoCore/blob/main/docs/ROADMAP.md).

This site brings the hand-written guides and the generated API reference together: the guides (below) are
rendered from the project's Markdown via MyST, and the **API reference** is rendered from the Doxygen XML
via Breathe. The styling is "squirty" - the Furo theme carrying the project's Retro TTY Green Screen brand
and [Squirty the Injection Squid](https://github.com/dstroy0/ProtoCore) as the mascot.

```{toctree}
:maxdepth: 2
:caption: Guides

architecture
symbols
src-law
osi-model
tcp-ip
languages
```

```{toctree}
:maxdepth: 2
:caption: Reference

api
```

## About this site

- The guides stay authored in `docs/` and `docs/learn/`; the pages here `include` them, so there is one
  source of truth.
- The API reference is generated: `docs/Doxyfile` emits XML into `docs/sphinx/xml`, and Breathe renders it.
- To rebuild locally, see the header of `docs/sphinx/conf.py`.
