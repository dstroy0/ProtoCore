# RadioGateway - bridge a southbound radio to the northbound stack

**Layer:** Foundation · **Build flags:** `PC_ENABLE_DMA`,
`PC_ENABLE_PREEMPT_QUEUE`, `PC_ENABLE_GATEWAY`, `PC_DMA_SIMULATE`

## What this example teaches

The capstone of the v5 ingest pipeline: a **wireless gateway**. A southbound radio
(LoRa / nRF24 / Zigbee / ... reached over SPI / I2C / UART) receives frames, and the
device **bridges** them to the web stack - MQTT / HTTP / WebSocket.

```
radio RX --DMA--> callback --post--> FORWARD lane --> codec --> pc_gateway_uplink()
                                                                     |
                                            envelope + topic  lora/<port>/<addr>
                                                                     |
                                                     northbound publish (MQTT/HTTP/WS)
```

Everything below the codec is a feature from earlier examples:

- **[DMA](../../Peripherals/DmaIngest/README.md)** reads the radio's bytes into a static buffer and
  fires a completion.
- The **[FORWARD lane](../../Foundation/PreemptLanes/README.md)** carries the frame off the
  interrupt to a task.
- A tiny **per-radio codec** (here: the first two bytes are the source node address, the
  rest is payload) turns raw bytes into an addressed frame.
- The **gateway** (`services/gateway`) envelopes and publishes it.

## The gateway

Register each radio as a **port** (with an optional transmit callback for downlink), and
install the **uplink** - the northbound publish:

```cpp
pc_gateway_port_config p = {};
p.port_id = 0; p.kind = pc_gateway_kind::PC_GW_LORA; p.tx = radio_tx; // tx = the radio's send()
pc_gateway_add_port(&p);
pc_gateway_set_uplink_cb(northbound_publish, nullptr);       // e.g. mqtt.publish(...)
pc_gateway_set_topic_prefix("lora");

// a frame arrived from node 0x42 on port 0:
pc_gateway_uplink(0, 0x42, payload, len, rssi);           // -> publishes lora/0/66
```

- **Envelope**: each uplink builds a `pc_gateway_msg` with the source address, port, RSSI,
  and a sequence number. `pc_gateway_topic()` formats a routing key `<prefix>/<port>/<addr>`.
- **Downlink**: `pc_gateway_downlink(port, dst_addr, payload, len)` transmits a northbound
  command back out the radio via the port's transmit callback.
- **Rate cap**: a per-port `rate_cap` (frames/second) throttles a chatty radio; excess is
  dropped, not blocked.
- **Fail-closed**: no installed sink, an unknown port, an exceeded cap, or a callback
  refusing drops the frame and is counted (`pc_gateway_get_stats()`), never blocks.

The radio TX and the northbound publish are **callbacks**, so this runs with no radio
hardware: the sketch feeds simulated frames through the DMA simulator and prints the
publishes. A real build swaps the feed for the module's SPI RX and the publish for an
MQTT client - the pipeline is unchanged.

Storage is static (zero heap): `PC_GW_MAX_PORTS` ports.

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_DMA=1 -DPC_ENABLE_PREEMPT_QUEUE=1 -DPC_ENABLE_GATEWAY=1 -DPC_DMA_SIMULATE=1" \
  --lib="." examples/Drivers/RadioGateway/RadioGateway.ino
```
