# Examples

The library ships 152 runnable examples under `examples/`, grouped by
the OSI layer the feature lives at and numbered within each group. **Each example
has its own README** with a detailed walkthrough, the build flags it needs, how
to build and run it, and the full source reproduced with teaching comments - so
this page is just the index. Click any example below.

## Building and running an example

Most examples need WiFi: open the `.ino` and set `SSID` / `PASSWORD` before
flashing. Compile one for an ESP32 board with `pio ci`:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_WEBSOCKET=1" \
  --lib="." examples/L6-Presentation/WebSocket/WebSocket.ino
```

> **The build_flags gotcha.** A sketch's `#define PC_ENABLE_X 1` only affects
> the sketch's own translation unit; the library is compiled separately and will
> not see it, producing link errors like `undefined reference to begin_tls`. When
> building with `pio ci`, pass each feature's flag as `build_flags` (the `-D...`
> form above) so the **library** is compiled with it too. Each example's README
> lists the exact flags. In the Arduino IDE the library compiles with your sketch,
> so the in-sketch `#define` (or setting it in `protocore_config.h`) is enough.

## Troubleshooting

- **`undefined reference to ...`** - the build_flags gotcha above; pass the flags to the library build.
- **`#error "... requires ..."`** - an illegal flag combination; see the [build-flag dependency tree](../README.md#build-flag-dependencies).
- **No WiFi** - set `SSID`/`PASSWORD`; TLS examples also need wall-clock time (pair with the SNTP example).
- **`begin()` returns negative** - a capacity constant is too small for the configured pools (the compile-time checks in `protocore_config.h` catch most first).
- **Built but not flashed** - `pio ci` only compiles; use `pio run -t upload` from a project containing the sketch.

<!-- BEGIN GENERATED EXAMPLE INDEX (ci_tooling/generate/gen_examples.py) -->

<!-- prettier-ignore-start -->

## Foundation

Start here: the core tutorial path (Basic -> Advanced -> Expert -> Sysadmin -> Configuration), then the server-architecture examples - the preempting task queue, lanes, and interface forwarding:

[Advanced](../examples/Foundation/Advanced/README.md) ·
[Basic](../examples/Foundation/Basic/README.md) ·
[Configuration](../examples/Foundation/Configuration/README.md) ·
[Expert](../examples/Foundation/Expert/README.md) ·
[InterfaceForward](../examples/Foundation/InterfaceForward/README.md) ·
[IPv6](../examples/Foundation/IPv6/README.md) ·
[PreemptLanes](../examples/Foundation/PreemptLanes/README.md) ·
[PreemptQueue](../examples/Foundation/PreemptQueue/README.md) ·
[Sysadmin](../examples/Foundation/Sysadmin/README.md)

## Peripherals

On-chip and add-on interface hardware - Ethernet (internal + W5500), CAN, Wi-Fi capture, and DMA ingest:

[CanCapture](../examples/Peripherals/CanCapture/README.md) ·
[DmaIngest](../examples/Peripherals/DmaIngest/README.md) ·
[Ethernet](../examples/Peripherals/Ethernet/README.md) ·
[EthernetW5500](../examples/Peripherals/EthernetW5500/README.md) ·
[WifiCapture](../examples/Peripherals/WifiCapture/README.md) ·
[WifiSniffer](../examples/Peripherals/WifiSniffer/README.md)

## Drivers

External chip drivers over I2C / SPI / UART - sensors, ADC / PWM / current monitors, an RTC, and radio-module gateways:

[Ads1115](../examples/Drivers/Ads1115/README.md) ·
[EnOceanGateway](../examples/Drivers/EnOceanGateway/README.md) ·
[Ina219](../examples/Drivers/Ina219/README.md) ·
[Ld2410](../examples/Drivers/Ld2410/README.md) ·
[LoRaGateway](../examples/Drivers/LoRaGateway/README.md) ·
[Mpr121](../examples/Drivers/Mpr121/README.md) ·
[NfcGateway](../examples/Drivers/NfcGateway/README.md) ·
[Nrf24Gateway](../examples/Drivers/Nrf24Gateway/README.md) ·
[Pca9685](../examples/Drivers/Pca9685/README.md) ·
[RadioGateway](../examples/Drivers/RadioGateway/README.md) ·
[Rtc](../examples/Drivers/Rtc/README.md) ·
[Sen0192](../examples/Drivers/Sen0192/README.md) ·
[Sht3x](../examples/Drivers/Sht3x/README.md) ·
[SigfoxUplink](../examples/Drivers/SigfoxUplink/README.md) ·
[ThreadGateway](../examples/Drivers/ThreadGateway/README.md) ·
[UbloxGnss](../examples/Drivers/UbloxGnss/README.md) ·
[ZigbeeGateway](../examples/Drivers/ZigbeeGateway/README.md) ·
[ZWaveGateway](../examples/Drivers/ZWaveGateway/README.md)

## L4 Transport

Connections, encryption, and flood defense:

[AcceptThrottle](../examples/L4-Transport/AcceptThrottle/README.md) ·
[DiffServ](../examples/L4-Transport/DiffServ/README.md) ·
[HTTPS](../examples/L4-Transport/HTTPS/README.md) ·
[IpAllowlist](../examples/L4-Transport/IpAllowlist/README.md) ·
[KeepAlive](../examples/L4-Transport/KeepAlive/README.md) ·
[mTLS](../examples/L4-Transport/mTLS/README.md) ·
[PerIpThrottle](../examples/L4-Transport/PerIpThrottle/README.md) ·
[TlsResumption](../examples/L4-Transport/TlsResumption/README.md)

## L5 Session

Interactive consoles:

[IKEv2](../examples/L5-Session/IKEv2/README.md) ·
[SSH](../examples/L5-Session/SSH/README.md) ·
[SSHCryptoSelfTest](../examples/L5-Session/SSHCryptoSelfTest/README.md) ·
[SSHHostKey](../examples/L5-Session/SSHHostKey/README.md) ·
[SSHReverseTunnel](../examples/L5-Session/SSHReverseTunnel/README.md) ·
[SSHSftp](../examples/L5-Session/SSHSftp/README.md) ·
[Telnet](../examples/L5-Session/Telnet/README.md)

## L6 Presentation

Parsing, codecs, auth, WebSocket/SSE:

[AuthLockout](../examples/L6-Presentation/AuthLockout/README.md) ·
[BasicAuth](../examples/L6-Presentation/BasicAuth/README.md) ·
[Cbor](../examples/L6-Presentation/Cbor/README.md) ·
[DigestAuth](../examples/L6-Presentation/DigestAuth/README.md) ·
[FormParams](../examples/L6-Presentation/FormParams/README.md) ·
[ForwardedTrust](../examples/L6-Presentation/ForwardedTrust/README.md) ·
[Json](../examples/L6-Presentation/Json/README.md) ·
[JWTAuth](../examples/L6-Presentation/JWTAuth/README.md) ·
[MsgPack](../examples/L6-Presentation/MsgPack/README.md) ·
[Multipart](../examples/L6-Presentation/Multipart/README.md) ·
[SecureWebSocket](../examples/L6-Presentation/SecureWebSocket/README.md) ·
[ServerSentEvents](../examples/L6-Presentation/ServerSentEvents/README.md) ·
[WebSocket](../examples/L6-Presentation/WebSocket/README.md) ·
[WebSocketCompression](../examples/L6-Presentation/WebSocketCompression/README.md) ·
[WebTerminal](../examples/L6-Presentation/WebTerminal/README.md)

## L7 Application

Routing, protocols, services, and clients:

[AdsClient](../examples/L7-Application/AdsClient/README.md) ·
[AuditLog](../examples/L7-Application/AuditLog/README.md) ·
[ChunkedResponse](../examples/L7-Application/ChunkedResponse/README.md) ·
[CoAP](../examples/L7-Application/CoAP/README.md) ·
[CoapBlock](../examples/L7-Application/CoapBlock/README.md) ·
[CoapObserve](../examples/L7-Application/CoapObserve/README.md) ·
[CoapSecure](../examples/L7-Application/CoapSecure/README.md) ·
[ConfigExport](../examples/L7-Application/ConfigExport/README.md) ·
[CoreDump](../examples/L7-Application/CoreDump/README.md) ·
[CORS](../examples/L7-Application/CORS/README.md) ·
[Csrf](../examples/L7-Application/Csrf/README.md) ·
[Dashboard](../examples/L7-Application/Dashboard/README.md) ·
[DeviceUuid](../examples/L7-Application/DeviceUuid/README.md) ·
[Diagnostics](../examples/L7-Application/Diagnostics/README.md) ·
[DnsResolver](../examples/L7-Application/DnsResolver/README.md) ·
[DnsServer](../examples/L7-Application/DnsServer/README.md) ·
[EdgeCache](../examples/L7-Application/EdgeCache/README.md) ·
[EspNow](../examples/L7-Application/EspNow/README.md) ·
[ETag](../examples/L7-Application/ETag/README.md) ·
[EthernetDnc](../examples/L7-Application/EthernetDnc/README.md) ·
[Euromap77](../examples/L7-Application/Euromap77/README.md) ·
[FileServing](../examples/L7-Application/FileServing/README.md) ·
[FileUpload](../examples/L7-Application/FileUpload/README.md) ·
[Gpib](../examples/L7-Application/Gpib/README.md) ·
[GpioMap](../examples/L7-Application/GpioMap/README.md) ·
[GraphQL](../examples/L7-Application/GraphQL/README.md) ·
[Guardrails](../examples/L7-Application/Guardrails/README.md) ·
[HaasMdc](../examples/L7-Application/HaasMdc/README.md) ·
[HeidenhainLsv2](../examples/L7-Application/HeidenhainLsv2/README.md) ·
[HiSlip](../examples/L7-Application/HiSlip/README.md) ·
[HotSwapStorage](../examples/L7-Application/HotSwapStorage/README.md) ·
[HttpClient](../examples/L7-Application/HttpClient/README.md) ·
[HttpDelivery](../examples/L7-Application/HttpDelivery/README.md) ·
[InterfaceBridge](../examples/L7-Application/InterfaceBridge/README.md) ·
[InterfaceFilter](../examples/L7-Application/InterfaceFilter/README.md) ·
[LogBuffer](../examples/L7-Application/LogBuffer/README.md) ·
[mDNS](../examples/L7-Application/mDNS/README.md) ·
[MdnsAdaptive](../examples/L7-Application/MdnsAdaptive/README.md) ·
[MediaStreaming](../examples/L7-Application/MediaStreaming/README.md) ·
[MeshCache](../examples/L7-Application/MeshCache/README.md) ·
[Middleware](../examples/L7-Application/Middleware/README.md) ·
[ModbusScan](../examples/L7-Application/ModbusScan/README.md) ·
[ModbusTcp](../examples/L7-Application/ModbusTcp/README.md) ·
[MqttClient](../examples/L7-Application/MqttClient/README.md) ·
[NetEgress](../examples/L7-Application/NetEgress/README.md) ·
[NtpServer](../examples/L7-Application/NtpServer/README.md) ·
[NtripCaster](../examples/L7-Application/NtripCaster/README.md) ·
[OAuth2](../examples/L7-Application/OAuth2/README.md) ·
[OidcAuth](../examples/L7-Application/OidcAuth/README.md) ·
[OpcUa](../examples/L7-Application/OpcUa/README.md) ·
[OpcUaClient](../examples/L7-Application/OpcUaClient/README.md) ·
[OTA](../examples/L7-Application/OTA/README.md) ·
[OtaRollback](../examples/L7-Application/OtaRollback/README.md) ·
[PackML](../examples/L7-Application/PackML/README.md) ·
[PartitionMonitor](../examples/L7-Application/PartitionMonitor/README.md) ·
[PathParams](../examples/L7-Application/PathParams/README.md) ·
[PidTuning](../examples/L7-Application/PidTuning/README.md) ·
[PortForward](../examples/L7-Application/PortForward/README.md) ·
[PowerGovernor](../examples/L7-Application/PowerGovernor/README.md) ·
[PrometheusMetrics](../examples/L7-Application/PrometheusMetrics/README.md) ·
[Provisioning](../examples/L7-Application/Provisioning/README.md) ·
[Ptp](../examples/L7-Application/Ptp/README.md) ·
[RadioPower](../examples/L7-Application/RadioPower/README.md) ·
[Range](../examples/L7-Application/Range/README.md) ·
[RegexRoutes](../examples/L7-Application/RegexRoutes/README.md) ·
[ResponseHeaders](../examples/L7-Application/ResponseHeaders/README.md) ·
[Robotics](../examples/L7-Application/Robotics/README.md) ·
[Scpi](../examples/L7-Application/Scpi/README.md) ·
[SimaticSerial](../examples/L7-Application/SimaticSerial/README.md) ·
[SmbFileClient](../examples/L7-Application/SmbFileClient/README.md) ·
[SmtpAlert](../examples/L7-Application/SmtpAlert/README.md) ·
[SNMP](../examples/L7-Application/SNMP/README.md) ·
[SnmpTrap](../examples/L7-Application/SnmpTrap/README.md) ·
[SNTP](../examples/L7-Application/SNTP/README.md) ·
[SpaFallback](../examples/L7-Application/SpaFallback/README.md) ·
[Stats](../examples/L7-Application/Stats/README.md) ·
[StatsdMetrics](../examples/L7-Application/StatsdMetrics/README.md) ·
[Syslog](../examples/L7-Application/Syslog/README.md) ·
[Telemetry](../examples/L7-Application/Telemetry/README.md) ·
[Templating](../examples/L7-Application/Templating/README.md) ·
[TimeSourceFallback](../examples/L7-Application/TimeSourceFallback/README.md) ·
[Totp](../examples/L7-Application/Totp/README.md) ·
[UdpTelemetry](../examples/L7-Application/UdpTelemetry/README.md) ·
[Umati](../examples/L7-Application/Umati/README.md) ·
[Vfs](../examples/L7-Application/Vfs/README.md) ·
[Vxi11](../examples/L7-Application/Vxi11/README.md) ·
[WebDav](../examples/L7-Application/WebDav/README.md) ·
[Webhook](../examples/L7-Application/Webhook/README.md) ·
[WebSocketClient](../examples/L7-Application/WebSocketClient/README.md)

<!-- prettier-ignore-end -->

<!-- END GENERATED EXAMPLE INDEX -->
