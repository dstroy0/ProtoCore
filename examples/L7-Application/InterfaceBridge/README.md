# InterfaceBridge - a network<->hardware-bus device server (UART / SPI / I2C)

This sketch turns the board into a **device server**: map a listen `x.x.x.x:nnnn`
to a **UART**, an **SPI** chip-select, or an **I2C** address, and a network
client that connects to the port is transparently bridged onto that bus. It is
the "put a serial/SPI/I2C peripheral on the network" pattern - reach a device
that only speaks a wire protocol from anywhere on your LAN.

You can try the UART side with no extra parts: a jumper from TX to RX makes a
loopback you can talk to over the network.

## Build

`PC_ENABLE_IFACE_BRIDGE` must be set for the whole build; an in-sketch
`#define` does not reach the separately compiled library, so pass it as a
`build_flag` (see the [build_flags gotcha](../../../docs/EXAMPLES.md)). The
Arduino IDE reads it from `build_opt.h`; with PlatformIO:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_IFACE_BRIDGE=1" \
  --lib="." examples/L7-Application/InterfaceBridge/InterfaceBridge.ino
```

## What is going on here? (the big picture)

The board opens a **port per bus endpoint**. When a client connects, the bridge
pipes bytes between the socket and the bus. There are two payload models, chosen
per rule:

- **STREAM** (UART): raw bidirectional passthrough - a classic serial-device
  server / `ser2net`. Whatever the client sends goes out the UART; whatever the
  UART receives comes back to the client. No framing, no interpretation.
- **TRANSACTION** (SPI / I2C, also usable for UART): master-initiated buses are
  request/response, so the client sends a small **frame** and gets the reply
  back:

    ```
    uint16 write_len (big-endian) || uint16 read_len (big-endian) || write_bytes[write_len]
    ```

    The bridge clocks `write_bytes` out on the bus, reads `read_len` bytes back,
    and returns exactly those bytes. The bus address (I2C 7-bit addr) or
    chip-select (SPI CS gpio) plus clock/mode come from the rule, so the frame
    stays generic across devices.

Wiring is two calls per endpoint: `server.listen(port, ConnProto::PROTO_BRIDGE)`
opens the port, and `pc_iface_bridge_publish()` binds it to a `BridgeTarget` and
brings the bus up. The server's own poll loop pumps everything.

```
   client  ──TCP──►  ESP32 :2323  ──UART1 TX/RX──►  serial device   (STREAM)
   client  ──TCP──►  ESP32 :2324  ──SPI CS/SCK/MOSI/MISO──►  SPI chip (TRANSACTION)
```

## The `BridgeTarget`

```c
struct BridgeTarget {
    BridgeBus  bus;       // uart | spi | i2c
    BridgeMode mode;      // stream | transaction
    uint8_t    unit;      // UART port # (0=Serial, 1=Serial1, 2=Serial2) / SPI host / I2C bus
    uint16_t   addr_cs;   // I2C 7-bit address, or SPI chip-select GPIO
    uint32_t   rate;      // UART baud, or SPI/I2C clock (Hz)
    uint8_t    spi_mode;  // SPI mode 0..3 (SPI only)
    uint8_t    bit_order; // 0 = MSB-first, 1 = LSB-first (SPI only)
};
```

## Part 1 - UART, no extra hardware (loopback)

1. In `InterfaceBridge.ino`, set your WiFi `SSID` / `PASSWORD`.
2. Jumper **Serial1 TX to Serial1 RX** on your board (e.g. on many classic
   ESP32 dev boards, GPIO 17 to GPIO 16). This loops the UART back to itself.
3. Flash and open Serial @ 115200. Note the printed IP.
4. From another machine, connect to the UART port and type:

    ```sh
    nc <board-ip> 2323
    hello
    ```

    Because TX is wired to RX, every line you send comes straight back. That is
    the raw stream path working end to end. Point `unit` / the jumper at a real
    serial device instead and you are talking to it over the network.

## Part 2 - a transaction client (SPI / I2C)

Transactions are one short frame out, `read_len` bytes back. Here is the whole
client in a few lines of Python - it reads 2 bytes from an SPI device after
sending a 1-byte command `0x9F` (a common "read ID"):

```python
import socket, struct

def txn(host, port, write_bytes, read_len):
    hdr = struct.pack(">HH", len(write_bytes), read_len)   # write_len, read_len (big-endian)
    s = socket.create_connection((host, port))
    s.sendall(hdr + write_bytes)
    data = b""
    while len(data) < read_len:
        chunk = s.recv(read_len - len(data))
        if not chunk:
            break
        data += chunk
    s.close()
    return data

print(txn("<board-ip>", 2324, b"\x9f", 2).hex())   # -> the 2 ID bytes clocked back
```

For I2C, change the rule in the sketch to
`{BridgeBus::i2c, BridgeMode::transaction, 0, 0x40 /*7-bit addr*/, 400000, 0, 0}`
and the same client works: `write_bytes` is the register pointer you write, and
the bridge does a repeated-start read of `read_len` bytes (no stop between the
write and read), exactly what most I2C sensors expect.

## Frame limits

`write_len` and `read_len` are each capped at `PC_BRIDGE_TXN_MAX` (default
256). A frame that exceeds the cap closes the connection - device-server
transactions are small register accesses, so bump the cap in `protocore_config.h`
only if a device genuinely needs larger bursts (keep it under the transport RX
ring so a whole frame can buffer before it is parsed).

## Configuration

| Macro                    | Default | Meaning                                          |
| ------------------------ | ------- | ------------------------------------------------ |
| `PC_ENABLE_IFACE_BRIDGE` | `0`     | compile the bridge in (required)                 |
| `PC_BRIDGE_MAX_RULES`    | `8`     | max concurrent address:port -> bus rules         |
| `PC_BRIDGE_TXN_MAX`      | `256`   | max write / read payload per transaction (bytes) |
| `PC_BRIDGE_STREAM_CHUNK` | `256`   | UART stream pipe chunk (bytes)                   |
| `PC_BRIDGE_UART_TXN_MS`  | `50`    | UART write-then-read reply window (ms)           |

## Security

A published port is a **direct pipe to the bus** with no authentication at this
layer. Only expose it on a trusted interface, keep the ports off untrusted
networks, and gate them behind an upstream ACL if the segment is shared. Note
that `unit = 0` (`Serial`) is usually the USB console - bridging it will fight
your debug prints; use `Serial1` / `Serial2` for a dedicated device.
