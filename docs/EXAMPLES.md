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

<div class="pc-examples">

<details open>
<summary>CORE · Foundation · 9 examples</summary>

<p class="pc-desc">Start here: the core tutorial path (Basic -> Advanced -> Expert -> Sysadmin -> Configuration), then the server-architecture examples - the preempting task queue, lanes, and interface forwarding:</p>

- @subpage md_examples_2Foundation_2Advanced_2README "Advanced"
- @subpage md_examples_2Foundation_2Basic_2README "Basic"
- @subpage md_examples_2Foundation_2Configuration_2README "Configuration"
- @subpage md_examples_2Foundation_2Expert_2README "Expert"
- @subpage md_examples_2Foundation_2InterfaceForward_2README "InterfaceForward"
- @subpage md_examples_2Foundation_2IPv6_2README "IPv6"
- @subpage md_examples_2Foundation_2PreemptLanes_2README "PreemptLanes"
- @subpage md_examples_2Foundation_2PreemptQueue_2README "PreemptQueue"
- @subpage md_examples_2Foundation_2Sysadmin_2README "Sysadmin"

</details>

<details>
<summary>HW · Peripherals · 6 examples</summary>

<p class="pc-desc">On-chip and add-on interface hardware - Ethernet (internal + W5500), CAN, Wi-Fi capture, and DMA ingest:</p>

- @subpage md_examples_2Peripherals_2CanCapture_2README "CanCapture"
- @subpage md_examples_2Peripherals_2DmaIngest_2README "DmaIngest"
- @subpage md_examples_2Peripherals_2Ethernet_2README "Ethernet"
- @subpage md_examples_2Peripherals_2EthernetW5500_2README "EthernetW5500"
- @subpage md_examples_2Peripherals_2WifiCapture_2README "WifiCapture"
- @subpage md_examples_2Peripherals_2WifiSniffer_2README "WifiSniffer"

</details>

<details>
<summary>HW · Drivers · 18 examples</summary>

<p class="pc-desc">External chip drivers over I2C / SPI / UART - sensors, ADC / PWM / current monitors, an RTC, and radio-module gateways:</p>

- @subpage md_examples_2Drivers_2Ads1115_2README "Ads1115"
- @subpage md_examples_2Drivers_2EnOceanGateway_2README "EnOceanGateway"
- @subpage md_examples_2Drivers_2Ina219_2README "Ina219"
- @subpage md_examples_2Drivers_2Ld2410_2README "Ld2410"
- @subpage md_examples_2Drivers_2LoRaGateway_2README "LoRaGateway"
- @subpage md_examples_2Drivers_2Mpr121_2README "Mpr121"
- @subpage md_examples_2Drivers_2NfcGateway_2README "NfcGateway"
- @subpage md_examples_2Drivers_2Nrf24Gateway_2README "Nrf24Gateway"
- @subpage md_examples_2Drivers_2Pca9685_2README "Pca9685"
- @subpage md_examples_2Drivers_2RadioGateway_2README "RadioGateway"
- @subpage md_examples_2Drivers_2Rtc_2README "Rtc"
- @subpage md_examples_2Drivers_2Sen0192_2README "Sen0192"
- @subpage md_examples_2Drivers_2Sht3x_2README "Sht3x"
- @subpage md_examples_2Drivers_2SigfoxUplink_2README "SigfoxUplink"
- @subpage md_examples_2Drivers_2ThreadGateway_2README "ThreadGateway"
- @subpage md_examples_2Drivers_2UbloxGnss_2README "UbloxGnss"
- @subpage md_examples_2Drivers_2ZigbeeGateway_2README "ZigbeeGateway"
- @subpage md_examples_2Drivers_2ZWaveGateway_2README "ZWaveGateway"

</details>

<details>
<summary>L4 · Transport · 8 examples</summary>

<p class="pc-desc">Connections, encryption, and flood defense:</p>

- @subpage md_examples_2L4-Transport_2AcceptThrottle_2README "AcceptThrottle"
- @subpage md_examples_2L4-Transport_2DiffServ_2README "DiffServ"
- @subpage md_examples_2L4-Transport_2HTTPS_2README "HTTPS"
- @subpage md_examples_2L4-Transport_2IpAllowlist_2README "IpAllowlist"
- @subpage md_examples_2L4-Transport_2KeepAlive_2README "KeepAlive"
- @subpage md_examples_2L4-Transport_2mTLS_2README "mTLS"
- @subpage md_examples_2L4-Transport_2PerIpThrottle_2README "PerIpThrottle"
- @subpage md_examples_2L4-Transport_2TlsResumption_2README "TlsResumption"

</details>

<details>
<summary>L5 · Session · 7 examples</summary>

<p class="pc-desc">Interactive consoles:</p>

- @subpage md_examples_2L5-Session_2IKEv2_2README "IKEv2"
- @subpage md_examples_2L5-Session_2SSH_2README "SSH"
- @subpage md_examples_2L5-Session_2SSHCryptoSelfTest_2README "SSHCryptoSelfTest"
- @subpage md_examples_2L5-Session_2SSHHostKey_2README "SSHHostKey"
- @subpage md_examples_2L5-Session_2SSHReverseTunnel_2README "SSHReverseTunnel"
- @subpage md_examples_2L5-Session_2SSHSftp_2README "SSHSftp"
- @subpage md_examples_2L5-Session_2Telnet_2README "Telnet"

</details>

<details>
<summary>L6 · Presentation · 15 examples</summary>

<p class="pc-desc">Parsing, codecs, auth, WebSocket/SSE:</p>

- @subpage md_examples_2L6-Presentation_2AuthLockout_2README "AuthLockout"
- @subpage md_examples_2L6-Presentation_2BasicAuth_2README "BasicAuth"
- @subpage md_examples_2L6-Presentation_2Cbor_2README "Cbor"
- @subpage md_examples_2L6-Presentation_2DigestAuth_2README "DigestAuth"
- @subpage md_examples_2L6-Presentation_2FormParams_2README "FormParams"
- @subpage md_examples_2L6-Presentation_2ForwardedTrust_2README "ForwardedTrust"
- @subpage md_examples_2L6-Presentation_2Json_2README "Json"
- @subpage md_examples_2L6-Presentation_2JWTAuth_2README "JWTAuth"
- @subpage md_examples_2L6-Presentation_2MsgPack_2README "MsgPack"
- @subpage md_examples_2L6-Presentation_2Multipart_2README "Multipart"
- @subpage md_examples_2L6-Presentation_2SecureWebSocket_2README "SecureWebSocket"
- @subpage md_examples_2L6-Presentation_2ServerSentEvents_2README "ServerSentEvents"
- @subpage md_examples_2L6-Presentation_2WebSocket_2README "WebSocket"
- @subpage md_examples_2L6-Presentation_2WebSocketCompression_2README "WebSocketCompression"
- @subpage md_examples_2L6-Presentation_2WebTerminal_2README "WebTerminal"

</details>

<details>
<summary>L7 · Application · 89 examples</summary>

<p class="pc-desc">Routing, protocols, services, and clients:</p>

- @subpage md_examples_2L7-Application_2AdsClient_2README "AdsClient"
- @subpage md_examples_2L7-Application_2AuditLog_2README "AuditLog"
- @subpage md_examples_2L7-Application_2ChunkedResponse_2README "ChunkedResponse"
- @subpage md_examples_2L7-Application_2CoAP_2README "CoAP"
- @subpage md_examples_2L7-Application_2CoapBlock_2README "CoapBlock"
- @subpage md_examples_2L7-Application_2CoapObserve_2README "CoapObserve"
- @subpage md_examples_2L7-Application_2CoapSecure_2README "CoapSecure"
- @subpage md_examples_2L7-Application_2ConfigExport_2README "ConfigExport"
- @subpage md_examples_2L7-Application_2CoreDump_2README "CoreDump"
- @subpage md_examples_2L7-Application_2CORS_2README "CORS"
- @subpage md_examples_2L7-Application_2Csrf_2README "Csrf"
- @subpage md_examples_2L7-Application_2Dashboard_2README "Dashboard"
- @subpage md_examples_2L7-Application_2DeviceUuid_2README "DeviceUuid"
- @subpage md_examples_2L7-Application_2Diagnostics_2README "Diagnostics"
- @subpage md_examples_2L7-Application_2DnsResolver_2README "DnsResolver"
- @subpage md_examples_2L7-Application_2DnsServer_2README "DnsServer"
- @subpage md_examples_2L7-Application_2EdgeCache_2README "EdgeCache"
- @subpage md_examples_2L7-Application_2EspNow_2README "EspNow"
- @subpage md_examples_2L7-Application_2ETag_2README "ETag"
- @subpage md_examples_2L7-Application_2EthernetDnc_2README "EthernetDnc"
- @subpage md_examples_2L7-Application_2Euromap77_2README "Euromap77"
- @subpage md_examples_2L7-Application_2FileServing_2README "FileServing"
- @subpage md_examples_2L7-Application_2FileUpload_2README "FileUpload"
- @subpage md_examples_2L7-Application_2Gpib_2README "Gpib"
- @subpage md_examples_2L7-Application_2GpioMap_2README "GpioMap"
- @subpage md_examples_2L7-Application_2GraphQL_2README "GraphQL"
- @subpage md_examples_2L7-Application_2Guardrails_2README "Guardrails"
- @subpage md_examples_2L7-Application_2HaasMdc_2README "HaasMdc"
- @subpage md_examples_2L7-Application_2HeidenhainLsv2_2README "HeidenhainLsv2"
- @subpage md_examples_2L7-Application_2HiSlip_2README "HiSlip"
- @subpage md_examples_2L7-Application_2HotSwapStorage_2README "HotSwapStorage"
- @subpage md_examples_2L7-Application_2HttpClient_2README "HttpClient"
- @subpage md_examples_2L7-Application_2HttpDelivery_2README "HttpDelivery"
- @subpage md_examples_2L7-Application_2InterfaceBridge_2README "InterfaceBridge"
- @subpage md_examples_2L7-Application_2InterfaceFilter_2README "InterfaceFilter"
- @subpage md_examples_2L7-Application_2LogBuffer_2README "LogBuffer"
- @subpage md_examples_2L7-Application_2mDNS_2README "mDNS"
- @subpage md_examples_2L7-Application_2MdnsAdaptive_2README "MdnsAdaptive"
- @subpage md_examples_2L7-Application_2MediaStreaming_2README "MediaStreaming"
- @subpage md_examples_2L7-Application_2MeshCache_2README "MeshCache"
- @subpage md_examples_2L7-Application_2Middleware_2README "Middleware"
- @subpage md_examples_2L7-Application_2ModbusScan_2README "ModbusScan"
- @subpage md_examples_2L7-Application_2ModbusTcp_2README "ModbusTcp"
- @subpage md_examples_2L7-Application_2MqttClient_2README "MqttClient"
- @subpage md_examples_2L7-Application_2NetEgress_2README "NetEgress"
- @subpage md_examples_2L7-Application_2NtpServer_2README "NtpServer"
- @subpage md_examples_2L7-Application_2NtripCaster_2README "NtripCaster"
- @subpage md_examples_2L7-Application_2OAuth2_2README "OAuth2"
- @subpage md_examples_2L7-Application_2OidcAuth_2README "OidcAuth"
- @subpage md_examples_2L7-Application_2OpcUa_2README "OpcUa"
- @subpage md_examples_2L7-Application_2OpcUaClient_2README "OpcUaClient"
- @subpage md_examples_2L7-Application_2OTA_2README "OTA"
- @subpage md_examples_2L7-Application_2OtaRollback_2README "OtaRollback"
- @subpage md_examples_2L7-Application_2PackML_2README "PackML"
- @subpage md_examples_2L7-Application_2PartitionMonitor_2README "PartitionMonitor"
- @subpage md_examples_2L7-Application_2PathParams_2README "PathParams"
- @subpage md_examples_2L7-Application_2PidTuning_2README "PidTuning"
- @subpage md_examples_2L7-Application_2PortForward_2README "PortForward"
- @subpage md_examples_2L7-Application_2PowerGovernor_2README "PowerGovernor"
- @subpage md_examples_2L7-Application_2PrometheusMetrics_2README "PrometheusMetrics"
- @subpage md_examples_2L7-Application_2Provisioning_2README "Provisioning"
- @subpage md_examples_2L7-Application_2Ptp_2README "Ptp"
- @subpage md_examples_2L7-Application_2RadioPower_2README "RadioPower"
- @subpage md_examples_2L7-Application_2Range_2README "Range"
- @subpage md_examples_2L7-Application_2RegexRoutes_2README "RegexRoutes"
- @subpage md_examples_2L7-Application_2ResponseHeaders_2README "ResponseHeaders"
- @subpage md_examples_2L7-Application_2Robotics_2README "Robotics"
- @subpage md_examples_2L7-Application_2Scpi_2README "Scpi"
- @subpage md_examples_2L7-Application_2SimaticSerial_2README "SimaticSerial"
- @subpage md_examples_2L7-Application_2SmbFileClient_2README "SmbFileClient"
- @subpage md_examples_2L7-Application_2SmtpAlert_2README "SmtpAlert"
- @subpage md_examples_2L7-Application_2SNMP_2README "SNMP"
- @subpage md_examples_2L7-Application_2SnmpTrap_2README "SnmpTrap"
- @subpage md_examples_2L7-Application_2SNTP_2README "SNTP"
- @subpage md_examples_2L7-Application_2SpaFallback_2README "SpaFallback"
- @subpage md_examples_2L7-Application_2Stats_2README "Stats"
- @subpage md_examples_2L7-Application_2StatsdMetrics_2README "StatsdMetrics"
- @subpage md_examples_2L7-Application_2Syslog_2README "Syslog"
- @subpage md_examples_2L7-Application_2Telemetry_2README "Telemetry"
- @subpage md_examples_2L7-Application_2Templating_2README "Templating"
- @subpage md_examples_2L7-Application_2TimeSourceFallback_2README "TimeSourceFallback"
- @subpage md_examples_2L7-Application_2Totp_2README "Totp"
- @subpage md_examples_2L7-Application_2UdpTelemetry_2README "UdpTelemetry"
- @subpage md_examples_2L7-Application_2Umati_2README "Umati"
- @subpage md_examples_2L7-Application_2Vfs_2README "Vfs"
- @subpage md_examples_2L7-Application_2Vxi11_2README "Vxi11"
- @subpage md_examples_2L7-Application_2WebDav_2README "WebDav"
- @subpage md_examples_2L7-Application_2Webhook_2README "Webhook"
- @subpage md_examples_2L7-Application_2WebSocketClient_2README "WebSocketClient"

</details>

</div>

<!-- prettier-ignore-end -->

<!-- END GENERATED EXAMPLE INDEX -->
