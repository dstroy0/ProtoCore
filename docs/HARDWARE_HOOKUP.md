# Hardware Hookup and Settings Guide

Many of this library's codecs do not talk to a web browser. They talk to **real
machines**: programmable logic controllers (PLCs), solar inverters, power-grid
sensors, building controllers, and other ESP32 boards. To use them you have to
connect the ESP32 to that machine with the right wires, the right adapter chip,
and the right communication settings.

This guide is the wiring-and-settings reference for every codec that needs
external hardware. It assumes **no prior electronics knowledge**: it explains
what each connection is, what part you need to buy, how to wire it, and what
numbers to type into your sketch.

> **One rule before anything else.** The ESP32's pins run at **3.3 volts and are
> not 5-volt tolerant.** Connecting a 5 V signal, a 12/24 V industrial signal, or
> an RS-232/RS-485 line **directly** to a GPIO pin can destroy the chip. Every
> serial bus below needs an adapter chip (a "transceiver" or "level shifter")
> between the machine and the ESP32. Never skip it.

**What is universal here, and what is not.** Most of this guide is electrical, and electricity
does not care which microcontroller you bought: RS-485 is RS-485, a twisted pair is a twisted
pair, and a missing common ground breaks a link on any board. That material applies anywhere.

The **concrete** parts - the toolchain commands, `LED_BUILTIN`, the TWAI controller, ESP-NOW -
are ESP32 because ESP32 is the silicon this library currently runs on. They are concrete on
purpose: "your board's UART peripheral" teaches a beginner nothing, and a getting-started guide
that abstracts over hardware it does not yet support would be worse, not more portable. When a
second architecture lands (see [ROADMAP.md](ROADMAP.md)), the platform-specific parts get
factored into per-platform annexes and the electrical material stays exactly where it is.

**The library never dictates a pinout.** Every driver takes its wiring through the API - an I2C
address, explicit pin numbers, or a caller-supplied bus struct - so _you_ decide what connects
where and tell the call. That is why there is no per-module wiring diagram below: there is
nothing to prescribe. What each module needs is in the generated
[module reference](#module-reference).

## Contents

- [Start here: get a board blinking](#start-here-get-a-board-blinking)
    - [What you need](#what-you-need)
    - [Step 1: does the computer see the board?](#step-1-does-the-computer-see-the-board)
    - [Step 2: upload blink](#step-2-upload-blink)
    - [Step 3: when the upload fails](#step-3-when-the-upload-fails)
- [General practices](#general-practices)
- [How these codecs work](#how-these-codecs-work)
- [The three ways a codec reaches its device](#the-three-ways-a-codec-reaches-its-device)
- [Quick-reference table](#quick-reference-table)
- [Serial field-bus codecs (you wire a transceiver)](#serial-field-bus-codecs-you-wire-a-transceiver)
    - [RS-485 wiring (the common case)](#rs-485-wiring-the-common-case)
    - [RS-232 wiring](#rs-232-wiring)
    - [Choosing and using the UART](#choosing-and-using-the-uart)
    - [Modbus RTU](#modbus-rtu)
    - [DF1 (Allen-Bradley)](#df1-allen-bradley)
    - [Host Link (Omron)](#host-link-omron)
    - [DNP3 and C37.118 over serial](#dnp3-and-c37118-over-serial)
    - [M-Bus (meters)](#m-bus-meters)
    - [SDI-12 (environmental sensors)](#sdi-12-environmental-sensors)
    - [DMX512 / RDM (lighting)](#dmx512--rdm-lighting)
    - [NMEA 0183 (GPS / marine)](#nmea-0183-gps--marine)
    - [IO-Link (smart sensors)](#io-link-smart-sensors)
- [CAN field-bus codecs (you wire a transceiver)](#can-field-bus-codecs-you-wire-a-transceiver)
    - [CAN wiring](#can-wiring)
    - [CANopen](#canopen)
    - [J1939](#j1939)
    - [DeviceNet](#devicenet)
    - [NMEA 2000](#nmea-2000)
- [Networked industrial codecs (over Wi-Fi or Ethernet)](#networked-industrial-codecs-over-wi-fi-or-ethernet)
    - [Getting on the network](#getting-on-the-network)
    - [Modbus TCP and Modbus master](#modbus-tcp-and-modbus-master)
    - [SunSpec](#sunspec)
    - [S7comm (Siemens)](#s7comm-siemens)
    - [MELSEC (Mitsubishi)](#melsec-mitsubishi)
    - [FINS (Omron)](#fins-omron)
    - [BACnet/IP](#bacnetip)
    - [EtherNet/IP and CIP](#ethernetip-and-cip)
    - [DNP3 and C37.118 over IP](#dnp3-and-c37118-over-ip)
    - [IEC 60870-5 (-104 and -101)](#iec-60870-5--104-and--101)
    - [OPC UA (server and client)](#opc-ua-server-and-client)
    - [SNMP (agent and traps)](#snmp-agent-and-traps)
- [Radio: ESP-NOW](#radio-esp-now)
- [Sensor and peripheral breakouts (I2C / UART, no transceiver)](#sensor-and-peripheral-breakouts-i2c--uart-no-transceiver)
    - [The shared I2C bus](#the-shared-i2c-bus)
    - [Time: RTC and GPS](#time-rtc-and-gps)
    - [Sensing: radar, touch, temperature/humidity, voltage, current](#sensing-radar-touch-temperaturehumidity-voltage-current)
    - [Actuation: servos and LEDs](#actuation-servos-and-leds)
- [Payload codecs that ride an existing link](#payload-codecs-that-ride-an-existing-link)
- [Electrical safety and reliability](#electrical-safety-and-reliability)

## Start here: get a board blinking

Before wiring anything to a machine, prove the boring part works: your computer can talk to
your board, and your board runs your code. Almost every "the codec doesn't work" problem
turns out to be here, not in the protocol. Ten minutes now saves an evening later.

### What you need

- **A board.** Any ESP32-class board with a USB socket. A DevKitC-style board is the usual
  starting point.
- **A data USB cable.** This is the single most common failure. Many cables that came with a
  phone or a battery pack are **charge-only**: power runs, data does not. If the board's power
  LED lights but the computer never shows a serial port, suspect the cable first and try
  another before you change anything else.
- **A computer** with the toolchain installed (Arduino IDE, `arduino-cli`, or PlatformIO).

### Step 1: does the computer see the board?

Plug the board in and list serial ports:

```sh
arduino-cli board list          # or: pio device list
```

You want a line naming a port. What it looks like depends on the USB chip on your board:

| Board's USB chip | Typical port name       | Note                                |
| ---------------- | ----------------------- | ----------------------------------- |
| CP210x           | `COM3` / `/dev/ttyUSB0` | Silicon Labs; may need a driver     |
| CH340 / CH343    | `COM8` / `/dev/ttyUSB0` | WCH; very common on low-cost boards |
| Native USB       | `COM4` / `/dev/ttyACM0` | The chip itself is the USB device   |

**Nothing appears?** Work through these in order, because they are ordered by how often they
are the cause:

1. **Try a different USB cable.** See above. This is the most likely cause by a wide margin.
2. **Try a different USB port**, ideally one directly on the computer rather than through a hub.
3. **Install the USB-serial driver** for your board's chip (CP210x or CH340). Native-USB boards
   need no driver.
4. On Linux, **add yourself to the `dialout` group** and log out and back in, otherwise the port
   exists but you cannot open it:
    ```sh
    sudo usermod -aG dialout "$USER"
    ```

### Step 2: upload blink

Blink is the "hello world" of hardware: it proves the toolchain compiles, the upload path
works, and the board runs your code. Nothing else can be trusted until it does.

```cpp
void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
```

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 blink
arduino-cli upload  --fqbn esp32:esp32:esp32 -p <your-port> blink
```

If the LED blinks, the boring part works and every later problem is a real problem.

> `LED_BUILTIN` is not defined on every board, and some boards have no user LED at all. If it
> does not compile, replace it with a pin number your board's pinout diagram shows as free, and
> watch that pin with a multimeter or an LED and resistor instead.

### Step 3: when the upload fails

These are the failures you will actually hit, and what each one means.

**"Failed to connect to ESP32: Timed out waiting for packet header"**
The board is not in download mode. Most boards enter it automatically; some cannot.

- Hold the **BOOT** (or **IO0**) button, tap **EN** (or **RESET**), then release BOOT, and start
  the upload again.
- If your board has only one button, hold it while plugging the USB cable in.

**"Serial port busy" / "Access denied" / "Resource temporarily unavailable"**
Something else already has the port open. A serial monitor in another window is the usual
culprit; close it. On Linux, `fuser /dev/ttyUSB0` names the process holding it.

**Upload succeeds, then the board reboots forever**
Open the serial monitor and read the message. Two common ones:

- `Brownout detector was triggered` - the board is not getting enough power. Use a better cable,
  a powered hub, or an external supply. Wi-Fi and radios draw hundreds of milliamps in bursts,
  far more than idle.
- A **panic / backtrace** - your code crashed. The backtrace names the fault; it is not a
  hardware problem.

**Serial monitor shows nothing, or shows garbage**
The monitor's baud rate does not match the sketch's. `Serial.begin(115200)` needs the monitor
at 115200. Mismatched baud produces plausible-looking nonsense rather than silence, which is
what makes it confusing.

**It worked yesterday and does not today**
Check what changed physically first: a nudged jumper wire, a different USB port, a different
cable. Hardware problems are usually mechanical.

## General practices

The habits below prevent most of the damage and most of the confusing failures. They are worth
adopting before you need them.

**Power off before you rewire.** Make every connection with the board unpowered. Moving a live
wire is how the short circuits happen.

**Check before you power.** Look at every connection once more before applying power,
especially power and ground. A swapped 3.3 V and GND destroys parts instantly and silently.

**Everything shares a ground.** Two devices that exchange signals must have their grounds
connected. Without a common ground, a "signal" has no reference and the link either does not
work or behaves randomly. This is the most common wiring mistake after the USB cable.

**3.3 V is not 5 V tolerant.** Repeated because it matters: any 5 V, 12 V, or 24 V signal needs
a level shifter or transceiver between it and a pin. Industrial gear is rarely 3.3 V.

**Keep signal wires short, and keep pairs together.** Long unshielded runs pick up noise. For
differential buses (RS-485, CAN) the two wires of a pair must be twisted together; that pairing
is what rejects interference.

**Label both ends of every wire.** A ten-wire harness is unreadable an hour after you build it.

**Change one thing at a time.** When something stops working, undo the last single change. Two
simultaneous changes make it impossible to tell which one broke it.

**Suspect the connection first.** Loose jumpers, a not-quite-seated header, a cold solder joint,
and a charge-only cable cause more failures than code does.

## How these codecs work

Every protocol codec in this library is **pure**: it turns a message into bytes
and bytes back into a message. It does **not** open the serial port or the network
socket for you. **Your sketch owns the wire.** That keeps the library deterministic
and lets the same codec run over a UART, a radio, Wi-Fi, or a host test.

In practice that means three jobs are yours:

1. **The hardware**: wire the adapter chip or join the network (this guide).
2. **The transport**: open the UART or socket and move the bytes (Arduino
   `HardwareSerial`, or this library's UDP/TCP transport where noted).
3. **The settings**: baud rate, parity, port number, device addresses.

The codec does the hard part in the middle: framing, checksums/CRCs, and the
wire format, all verified against the published spec. Each section below links to
the matching header in `src/services/` and its description in
[FEATURES.md](FEATURES.md).

## The three ways a codec reaches its device

| Family               | What carries the bytes                           | Extra hardware you supply                                   |
| -------------------- | ------------------------------------------------ | ----------------------------------------------------------- |
| **Serial field bus** | A UART pin pair, through an adapter chip         | An RS-485 or RS-232 transceiver, wiring, termination        |
| **CAN field bus**    | The ESP32 TWAI controller (or MCP2515 over SPI)  | A CAN transceiver (SN65HVD230), wiring, 120 ohm terminators |
| **Networked**        | TCP or UDP over the ESP32's Wi-Fi (or wired PHY) | Usually none beyond Wi-Fi; the device and a shared network  |
| **Radio (ESP-NOW)**  | The ESP32's built-in 2.4 GHz radio               | None; just a second ESP-class board as the peer             |

## Quick-reference table

"Flag" is the `PC_ENABLE_*` macro you set to compile the codec in (all default
**off**). "Talks to" is the machine on the other end.

| Codec         | Flag                      | Transport                  | External hardware               | Typical settings                | Talks to                           |
| ------------- | ------------------------- | -------------------------- | ------------------------------- | ------------------------------- | ---------------------------------- |
| Modbus RTU    | `PC_ENABLE_MODBUS_RTU`    | Serial (RS-485/232)        | RS-485 transceiver              | 19200 8E1, unit 1-247           | Modbus sensors, drives, meters     |
| Modbus TCP    | `PC_ENABLE_MODBUS`        | TCP                        | Wi-Fi/Ethernet                  | port 502                        | Modbus PLCs and gateways           |
| Modbus master | `PC_ENABLE_MODBUS_MASTER` | TCP (client)               | Wi-Fi/Ethernet                  | port 502                        | Modbus slave devices               |
| SunSpec       | `PC_ENABLE_SUNSPEC`       | Modbus (RTU or TCP)        | per Modbus row                  | model base reg 40000            | Solar inverters, meters, batteries |
| DF1           | `PC_ENABLE_DF1`           | Serial (RS-232/485)        | RS-232 or RS-485 transceiver    | 19200, full-duplex              | Allen-Bradley (Rockwell) PLCs      |
| Host Link     | `PC_ENABLE_HOSTLINK`      | Serial (RS-232/422/485)    | transceiver                     | 9600 7E2, unit 00-31            | Omron PLCs (C-mode)                |
| FINS          | `PC_ENABLE_FINS`          | UDP                        | Wi-Fi/Ethernet                  | UDP port 9600                   | Omron PLCs (FINS)                  |
| MELSEC        | `PC_ENABLE_MELSEC`        | TCP or UDP                 | Wi-Fi/Ethernet                  | port set on the PLC             | Mitsubishi PLCs (MC 3E)            |
| S7comm        | `PC_ENABLE_S7COMM`        | TCP (ISO-on-TCP)           | Wi-Fi/Ethernet                  | port 102, rack/slot             | Siemens S7-300/400/1200/1500       |
| BACnet/IP     | `PC_ENABLE_BACNET`        | UDP                        | Wi-Fi/Ethernet                  | UDP port 47808                  | Building automation controllers    |
| EtherNet/IP   | `PC_ENABLE_ENIP`          | TCP + UDP                  | Wi-Fi/Ethernet                  | TCP 44818, UDP 2222             | Allen-Bradley / ODVA devices       |
| CIP           | `PC_ENABLE_CIP`           | over EtherNet/IP           | Wi-Fi/Ethernet                  | (rides ENIP)                    | CIP objects on ODVA devices        |
| DNP3          | `PC_ENABLE_DNP3`          | Serial or TCP              | transceiver or Wi-Fi            | TCP 20000, 16-bit addresses     | SCADA / utility outstations        |
| C37.118       | `PC_ENABLE_C37118`        | Serial, TCP or UDP         | transceiver or Wi-Fi            | no fixed port (often 4712/4713) | Power-grid PMUs / PDCs             |
| M-Bus         | `PC_ENABLE_MBUS`          | Serial (M-Bus bus)         | M-Bus level converter (TSS721)  | 2400 8E1, primary addr 1-250    | Water / gas / heat / power meters  |
| SDI-12        | `PC_ENABLE_SDI12`         | Serial (1-wire SDI-12)     | level / direction circuit       | 1200 7E1, sensor addr 0-9/A-Z   | Soil / water / weather sensors     |
| DMX512 / RDM  | `PC_ENABLE_DMX`           | Serial (RS-485)            | RS-485 transceiver (MAX485)     | 250k 8N2, up to 512 channels    | Stage / architectural lighting     |
| NMEA 0183     | `PC_ENABLE_NMEA0183`      | Serial (UART / RS-422)     | none (TTL) or RS-422 receiver   | 4800 or 9600 8N1                | GPS / marine instruments           |
| IO-Link       | `PC_ENABLE_IOLINK`        | Serial (3-wire SDCI)       | IO-Link transceiver (MAX14819)  | 4.8 / 38.4 / 230.4 kbit/s       | Smart sensors / actuators          |
| IEC 60870     | `PC_ENABLE_IEC60870`      | TCP (-104) / serial (-101) | Wi-Fi or RS-232/485 transceiver | TCP 2404; -101 link address     | Utility RTUs / SCADA outstations   |
| CANopen       | `PC_ENABLE_CANOPEN`       | CAN (TWAI or SPI)          | CAN transceiver                 | 125k-1M bit/s, node 1-127       | Motion drives, I/O, CANopen nodes  |
| J1939         | `PC_ENABLE_J1939`         | CAN (TWAI or SPI)          | CAN transceiver                 | 250k bit/s, 29-bit ids          | Trucks, tractors, gensets, marine  |
| DeviceNet     | `PC_ENABLE_DEVICENET`     | CAN (TWAI or SPI)          | CAN transceiver (+ 24 V bus)    | 125/250/500k, MAC 0-63          | DeviceNet I/O, drives (CIP/CAN)    |
| NMEA 2000     | `PC_ENABLE_NMEA2000`      | CAN (TWAI or SPI)          | CAN transceiver (+ N2K tap)     | 250k bit/s, Fast Packet         | Marine GPS / wind / depth / engine |
| OPC UA        | `PC_ENABLE_OPCUA`         | TCP                        | Wi-Fi/Ethernet                  | port 4840, SecurityPolicy None  | OPC UA clients / SCADA             |
| OPC UA client | `PC_ENABLE_OPCUA_CLIENT`  | TCP (client)               | Wi-Fi/Ethernet                  | port 4840                       | OPC UA servers                     |
| SNMP          | `PC_ENABLE_SNMP`          | UDP                        | Wi-Fi/Ethernet                  | UDP 161 (agent), 162 (trap)     | Network monitoring systems         |
| ESP-NOW       | `PC_ENABLE_ESPNOW`        | 2.4 GHz radio              | none (a peer ESP board)         | shared channel, peer MAC        | Other ESP32 / ESP8266 boards       |

## Serial field-bus codecs (you wire a transceiver)

A "serial field bus" sends data one bit at a time down a wire, the way the
classic serial port did. Industrial buses use a tougher electrical standard than
a plain UART so the signal survives long noisy cable runs. The ESP32 cannot drive
those wires directly; a small **transceiver** chip sits between the ESP32's UART
and the bus.

### RS-485 wiring (the common case)

RS-485 is the workhorse of factory wiring. It sends each bit as the **difference**
between two wires (called **A**/**D-** and **B**/**D+**), which rejects electrical
noise and reaches over a kilometer. Many devices share one pair of wires (a
"multidrop" bus), so only one device may transmit at a time.

**What you need:** an RS-485 transceiver. Prefer a **3.3 V** part so its logic
pins match the ESP32 directly: `MAX3485`, `SP3485`, `ADM3485`, or `THVD1450`.
The classic `MAX485` is a 5 V part; if you use one, add a logic-level shifter on
its data pins. Cheap "RS-485 to TTL" breakout modules bundle the chip and the
screw terminals.

**The connections:**

| Transceiver pin             | Connect to                                     |
| --------------------------- | ---------------------------------------------- |
| `RO` (receiver out)         | ESP32 UART **RX**                              |
| `DI` (driver in)            | ESP32 UART **TX**                              |
| `DE` + `RE` (tied together) | One ESP32 **GPIO** (the direction-control pin) |
| `A` / `B`                   | The bus pair, to the same A/B on every device  |
| `VCC` / `GND`               | 3.3 V and ground, shared with the ESP32        |

Because two wires carry traffic both directions, you must tell the transceiver
when to talk and when to listen. That is the **direction-control pin**: drive the
GPIO **high** to transmit, **low** to receive. (Some modules auto-switch and need
no GPIO; they are simpler but slightly less reliable at high baud.)

**Two more parts that matter on a real bus:**

- **Termination**: a **120 ohm** resistor across A and B at **each of the two far
  ends** of the cable (not in the middle). It stops signal reflections.
- **Fail-safe bias**: one set of pull resistors (roughly 560-680 ohm: a pull-up
  on B to VCC and a pull-down on A to GND) somewhere on the bus, so an idle bus
  reads as a clean logic level. Many modules include these.

A 2-wire ("half-duplex") bus is by far the most common. A 4-wire ("full-duplex")
RS-485 uses two pairs (one to transmit, one to receive) and does not need the
direction pin.

### RS-232 wiring

RS-232 is the old PC serial port: one wire per direction, swinging between about
+/-3 and +/-15 volts, point-to-point (one device per port), short cables. You need
a **level shifter** such as the **3.3 V `MAX3232`** between the device's RS-232
connector and the ESP32 UART:

| MAX3232 side          | Connect to         |
| --------------------- | ------------------ |
| TTL `T1IN`            | ESP32 UART **TX**  |
| TTL `R1OUT`           | ESP32 UART **RX**  |
| RS-232 `T1OUT`/`R1IN` | the device's RX/TX |
| `VCC`/`GND`           | 3.3 V and ground   |

Cross TX to RX and RX to TX (a "null-modem" crossover) if both ends are devices
rather than a PC-and-modem pair.

### Choosing and using the UART

The ESP32 has three hardware UARTs. **UART0 is the USB programming/console port;
leave it alone.** Use **UART1** or **UART2** for the field bus. On the classic
ESP32, `Serial2` defaults to **GPIO16 (RX)** and **GPIO17 (TX)**, but you can map
a UART to almost any free GPIO. Avoid the boot **strapping pins** (GPIO0, 2, 5,
12, 15) and the **flash pins** (GPIO6-11), and on ESP32-S3/C3 pick any free GPIO
through the pin matrix.

Open the port with the baud rate and framing your device expects, then pump bytes
through the codec. A half-duplex RS-485 send looks like this:

```cpp
#include "services/fieldbus/modbus/modbus.h"

HardwareSerial &bus = Serial2;
const int DE_RE = 4; // RS-485 direction-control GPIO

void setup() {
  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, LOW);                // listen by default
  bus.begin(19200, SERIAL_8E1, 16, 17);    // Modbus default: 19200, 8 data, even, 1 stop
}

// after the codec builds a reply of length n into resp[]:
//   digitalWrite(DE_RE, HIGH);            // take the bus
//   bus.write(resp, n);
//   bus.flush();                          // wait for the last bit out
//   digitalWrite(DE_RE, LOW);             // release, back to listening
```

Framing (deciding where one message ends and the next begins) is the transport's
job. For Modbus RTU that is the **3.5-character idle gap** between frames; collect
a frame, then hand it to the codec.

### Modbus RTU

- **Flag:** `PC_ENABLE_MODBUS_RTU` (turns on `PC_ENABLE_MODBUS` too).
- **Hardware:** RS-485 transceiver (occasionally RS-232 point-to-point).
- **Settings:** the Modbus spec default is **19200 baud, 8 data bits, even
  parity, 1 stop bit (8E1)**; **9600 8N1** is also very common. Every device on a
  bus must use the **same** baud and framing. Each slave has a **unit address**
  from **1 to 247** (0 is a broadcast with no reply).
- **Codec:** `pc_modbus_rtu_process_adu()` validates the CRC-16 and the unit address
  and dispatches to the host-tested PDU layer; a bad CRC or a non-matching
  address is dropped silently, exactly as the spec requires. See
  `src/services/fieldbus/modbus/modbus.h`.

### DF1 (Allen-Bradley)

- **Flag:** `PC_ENABLE_DF1`.
- **Hardware:** RS-232 to a PLC serial port (a DB9), or RS-485 for multidrop;
  use the matching transceiver above.
- **Settings:** commonly **19200** (also 9600/4800), full-duplex DF1. The codec
  supports both check types: **BCC** (a simple checksum) and **CRC-16/ARC**;
  match what the PLC channel is configured for. Node/station addressing lives in
  the PCCC application header carried inside the frame.
- **Codec:** `pc_df1_build_frame` / `pc_df1_parse_frame` handle the `DLE STX ... DLE
ETX` framing and byte-stuffing. See `src/services/fieldbus/df1/df1.h`.

### Host Link (Omron)

- **Flag:** `PC_ENABLE_HOSTLINK`.
- **Hardware:** RS-232, RS-422, or RS-485 to an Omron PLC host-link port.
- **Settings:** a typical Omron host-link port is **9600 baud, 7 data bits, even
  parity, 2 stop bits (7E2)**; confirm the PLC's setting. Each PLC has a **unit
  number 00-31** that begins every command frame.
- **Codec:** `pc_hostlink_build` / `pc_hostlink_parse` build the `@` + unit + code +
  text + FCS + `*`CR ASCII frame and validate the XOR checksum (FCS). See
  `src/services/fieldbus/hostlink/hostlink.h`.

### DNP3 and C37.118 over serial

Both can run over serial as well as IP. Wire them like Modbus RTU (RS-485 or
RS-232 transceiver) and see their entries under
[networked codecs](#dnp3-and-c37118-over-ip) for addressing and the codec calls;
the framing is transport-independent.

### M-Bus (meters)

`PC_ENABLE_MBUS`. Wired M-Bus is the European utility-meter bus (water, gas,
heat, electricity). Electrically it is **not** RS-485: it is its own powered
two-wire bus where the master sends by voltage modulation and the meter replies
by current modulation. So you need an **M-Bus master level converter** (a
TSS721-based module is the cheap option) between an ESP32 UART and the bus. Wire
the ESP32 `TX`/`RX` to the module's UART side and the two M-Bus terminals to the
meters. Typical line settings are **2400 baud, 8E1**; meters have a primary
address 1-250.

The codec is the framing + record layer:

- Wake / address a meter: `pc_mbus_build_snd_nke(buf, cap, addr)` (link reset), then
  `pc_mbus_build_req_ud2(buf, cap, addr, fcb)` to request data (toggle `fcb` each
  poll).
- Parse the reply: `pc_mbus_parse()` validates the frame and gives you C / A / CI +
  the user data; then walk the values with `pc_mbus_record_next()` (each record's
  DIF gives the data type/length, the VIF the unit).

A natural **wireless meter gateway**: poll meters over the M-Bus and publish the
readings over MQTT / HTTP. Only the LVAR raw/ASCII variable form is decoded; the
BCD-length LVAR variants are not. See `src/services/fieldbus/mbus/mbus.h`.

### SDI-12 (environmental sensors)

`PC_ENABLE_SDI12`. SDI-12 is the **1200-baud single-wire** bus for soil,
water, and weather sensors. One data line carries both directions (half-duplex,
**7E1**); the recorder addresses a sensor by one character (0-9, A-Z, a-z). The
electrical layer is a 5 V line with a marking/break convention, so on a 3.3 V
ESP32 you add a small level + direction circuit (a couple of transistors, or one
of the ready-made SDI-12 interface boards) between a UART and the bus.

A measurement is two steps: start it, wait, then fetch:

- `pc_sdi12_build_measure(buf, cap, addr, false)` sends `aM!`; parse the reply with
  `pc_sdi12_parse_measure()` to learn how many seconds to wait and how many values
  to expect.
- After the wait, `pc_sdi12_build_data(buf, cap, addr, 0)` sends `aD0!`; split the
  reply into floats with `pc_sdi12_parse_values()`.
- For the CRC-protected forms (`aMC!` / `aCC!`), check the reply with
  `pc_sdi12_check_crc()`.

Poll a sensor string and publish the readings over Wi-Fi. See
`src/services/peripherals/sdi12/sdi12.h`.

### DMX512 / RDM (lighting)

`PC_ENABLE_DMX`. DMX512 drives stage and architectural lighting over **RS-485**
at **250 kbit/s, 8N2** - so wire the same RS-485 transceiver (a `MAX485` is the
classic cheap part) as in the RS-485 section above. DMX is one-way and positional:
each fixture listens on a start address and reads N consecutive channel slots.

- Send a universe: fill a channel array and `pc_dmx_build(buf, cap, DMX_SC_DIMMER,
channels, n)`; your transport sends a **break** then the returned bytes.
- `pc_dmx_get_channel()` reads a 1-based channel from a received packet.

**RDM** (ANSI E1.20) adds two-way device management on the same pair (the
transceiver must be switched to receive for the reply, and RDM needs proper
direction timing). Build a request with `pc_rdm_build()` (set the destination /
source `pc_rdm_uid()`, command class, and PID) and read a reply with `pc_rdm_parse()`,
which checks the RDM checksum. Discover, address, and configure fixtures from a
web UI - a tidy **wireless lighting controller**. See `src/services/peripherals/dmx/dmx.h`.

### NMEA 0183 (GPS / marine)

`PC_ENABLE_NMEA0183`. The classic GPS / marine sentence protocol. A hobby GPS
breakout outputs **3.3 V TTL serial** that wires straight to an ESP32 RX pin (no
transceiver) - typically **9600 baud, 8N1** (older units 4800). Full-size marine
instruments use **RS-422**, so for those add an RS-422 receiver. It is mostly
one-way (the receiver talks; you listen).

- Read a fix: accumulate a line, then `pc_nmea0183_parse(line, len, &m)`. Check
  `m.type` ("GGA", "RMC", "VTG", ...) and pull fields with
  `pc_nmea0183_field_float()` / `pc_nmea0183_field_int()` (field 0 is the address;
  data fields are 1..n).
- To send (e.g. configuration to a receiver), `pc_nmea0183_build()` adds the `$`,
  checksum, and CR/LF around your comma-separated body.

Decode position / speed / heading and republish it over Wi-Fi (a web map, MQTT,
or an NMEA-0183-to-NMEA-2000 bridge with the codec next door). See
`src/services/timing_position/nmea0183/nmea0183.h`.

### IO-Link (smart sensors)

`PC_ENABLE_IOLINK`. IO-Link (SDCI) is the point-to-point link to modern smart
sensors and actuators - a single device per port over a 3-wire cable (L+, L-, and
the C/Q data line). The data line is **not** plain UART levels: it swings to the
24 V supply, so you need a dedicated **IO-Link master transceiver** (a `MAX14819`
or `L6360`-class chip) between an ESP32 UART and the port. The transceiver also
generates the wake-up pulse and handles the line driving; the UART runs at one of
the three SDCI rates (**COM1 4.8 / COM2 38.4 / COM3 230.4 kbit/s**).

This codec is the data-link **message** layer - in particular the SDCI checksum,
which is the easy thing to get wrong:

- Master message: lay out the M-sequence (the `pc_iol_mc()` control octet, any
  on-request / process octets, and an `pc_iol_ckt()` checksum/type octet), then
  `pc_iol_finalize(msg, len, check_index)` fills the checksum.
- Device reply: `pc_iol_verify(msg, len, check_index)` checks the reply's
  checksum/status octet; read its Event and PD-valid flags.

The per-device M-sequence and ISDU layout come from the device's IODD profile.
This gets a sensor's data onto Wi-Fi via a single transceiver. See
`src/services/fieldbus/iolink/iolink.h`.

## CAN field-bus codecs (you wire a transceiver)

CAN (Controller Area Network) is a two-wire differential bus (CAN_H / CAN_L)
used across factory automation and vehicles. It is cheap and robust, and the
ESP32 already has a CAN controller built in (called **TWAI**), so you only add a
**transceiver** chip to drive the differential pair.

### CAN wiring

You need a CAN transceiver between the ESP32 and the bus. Two cheap options:

- **SN65HVD230 breakout (~$1)**: 3.3 V, pairs directly with the ESP32's TWAI
  controller. Connect ESP32 `TX` GPIO -> transceiver `D` (TXD), transceiver `R`
  (RXD) -> ESP32 `RX` GPIO, plus 3V3 and GND. CAN_H / CAN_L go to the bus.
- **MCP2515 + TJA1050 module (~$2)**: a standalone CAN controller you talk to
  over **SPI** (use this if you would rather not use the internal TWAI, or need a
  second CAN channel). Wire SPI (SCK/MOSI/MISO/CS) + an interrupt GPIO.

Bus rules that matter: terminate **both ends** of the bus with a **120 ohm**
resistor (many breakouts have a jumper for it), keep stubs short, and set every
node to the **same bit rate** (125 kbit/s, 250 k, 500 k, and 1 Mbit/s are
common). All nodes on a CAN bus must agree on the bit rate.

### CANopen

`PC_ENABLE_CANOPEN`. CANopen (CiA 301) is the dominant higher-layer protocol
for CAN in factory automation: motion drives, I/O blocks, sensors. Each node has
an id 1-127. The codec builds and parses the messages; your sketch moves the
frames with the TWAI driver (or the MCP2515):

- Bring a node up: `pc_canopen_build_nmt(&frame, CANOPEN_NMT_START, node)`.
- Read an object (expedited SDO): `pc_canopen_build_sdo_read(&frame, node, index,
sub)`, send it, then `pc_canopen_parse_sdo_response()` on the reply.
- Write an object: `pc_canopen_build_sdo_write(...)`.
- Watch liveness: `pc_canopen_parse_heartbeat()` on each `0x700+node` frame;
  `pc_canopen_parse_emcy()` on emergencies.
- Receive process data: `pc_canopen_parse()` classifies each frame, and a TPDO's
  `data[]` is the raw mapped payload.

This is the classic **wireless bridge**: poll CANopen drives over the wire and
publish their state over MQTT / HTTP / a WebSocket. SDO transfers are expedited
(<= 4 octets); segmented / block transfer is a future addition. See
`src/services/fieldbus/canopen/canopen.h`.

### J1939

`PC_ENABLE_J1939`. The CAN higher-layer protocol for heavy-duty vehicles,
agriculture, marine, and gensets - same wiring as above (transceiver +
terminators), but it uses **29-bit extended** ids, almost always at **250
kbit/s**. The codec packs and unpacks the id (priority / PGN / source /
destination) and handles multi-packet messages:

- Decode a received frame: `pc_j1939_decode_id(frame.id, &id)` gives you the PGN,
  source, and destination; the 8 data octets are the parameter group's signals
  (SPNs), which you scale per the PGN definition.
- Ask a device for a PGN: `pc_j1939_build_request(&frame, my_addr, dest, pgn)`.
- Announce your address: `pc_j1939_build_address_claim(&frame, my_addr,
pc_j1939_build_name(...))`.
- Long messages (> 8 octets, e.g. diagnostics): feed every received frame to
  `pc_j1939_tp_feed(&rx, &frame)`; when it returns `J1939_TP_COMPLETE`, `rx.buf`
  holds the reassembled message for `rx.pgn`. To send one, `pc_j1939_build_bam_cm()`
  then a `pc_j1939_build_tp_dt()` per 7-octet chunk.

A classic **wireless gateway**: decode engine / transmission / genset PGNs off
the bus and publish them over MQTT or a web dashboard. See
`src/services/fieldbus/j1939/j1939.h`.

### DeviceNet

`PC_ENABLE_DEVICENET`. DeviceNet is **CIP over CAN** (the same CIP objects as
EtherNet/IP, but on a CAN wire). Electrically it is CAN with a twist: a DeviceNet
cable carries **24 V power** alongside CAN_H / CAN_L, so a real drop also needs
the power conductors and the standard 5-pin connector - but the signalling is
ordinary CAN, so the **same transceiver wiring** as above applies (use 125, 250,
or 500 kbit/s; each node has a MAC id 0-63).

This module supplies the DeviceNet-specific link layer; you build the CIP message
body with the `pc_cip_*` functions (enable `PC_ENABLE_CIP`):

- Address a frame: `pc_devicenet_encode_id(&id, DEVICENET_GROUP_2, msg_id, mac)`
  picks the message group + MAC id; `pc_devicenet_decode_id()` reverses it.
- Send a short explicit request: build the CIP body with `pc_cip_*`, then
  `pc_devicenet_build_explicit(&frame, group, msg_id, mac, body, len)` prepends the
  message-header octet (fits when the body is <= 7 octets).
- Receive a long response: feed each frame's data octets to
  `pc_devicenet_frag_feed(&rx, body, len)`; on `DEVICENET_FRAG_COMPLETE`, `rx.buf`
  holds the reassembled CIP response to parse with `pc_cip_*`.

Bridge a DeviceNet segment onto Wi-Fi the same way as the other CAN buses. See
`src/services/fieldbus/devicenet/devicenet.h`.

### NMEA 2000

`PC_ENABLE_NMEA2000`. The marine instrumentation backbone (GPS, wind, depth,
AIS, engine data) - electrically it is CAN at **250 kbit/s**, and protocol-wise
it is J1939, so the wiring is the same transceiver setup. A real N2K backbone
uses a powered trunk with drop "tees"; tap a drop with your transceiver (it only
needs CAN_H / CAN_L / GND - leave the N2K power to the backbone).

NMEA 2000 carries most data either in a single 8-octet frame or via **Fast
Packet** (up to 223 octets). The codec reuses the J1939 id functions and adds
Fast Packet:

- Receive: decode the id with `pc_j1939_decode_id()`; for a single-frame PGN read
  the 8 octets directly, and for a Fast Packet PGN feed each frame to
  `pc_n2k_fastpacket_feed(&rx, &frame)` until `N2K_FP_COMPLETE`, then parse
  `rx.buf` per the PGN definition.
- Send: `pc_n2k_build_single()` for short PGNs, or loop
  `pc_n2k_fastpacket_build_frame()` over `pc_n2k_fastpacket_num_frames()` for long ones.

Bridge the backbone onto Wi-Fi: turn boat data into a web dashboard or MQTT feed.
See `src/services/timing_position/nmea2000/nmea2000.h`.

## Networked industrial codecs (over Wi-Fi or Ethernet)

These codecs reach a device over a network instead of a dedicated wire. The
"external hardware" is usually just the **target device plus a shared network**;
the ESP32's built-in Wi-Fi supplies the link.

### Getting on the network

- **Wi-Fi (the verified path):** join the ESP32 (station mode) to the **same
  network** as the industrial device, or to a network that can route to it. The
  device needs a reachable IP address; many PLCs ship with a fixed default IP you
  set with the vendor's tool.
- **Wired Ethernet (advanced):** for a hard-wired link, add an external Ethernet
  PHY (an RMII part such as the LAN8720, or an SPI part such as the W5500). The
  **W5500 SPI path is hardware-verified** on an ESP32-S3 (arduino-esp32 3.x): a
  200 MB streamed download completes byte-exact with a flat heap. Wired Ethernet
  removes the Wi-Fi RTT, so it suits large transfers; Wi-Fi remains the default.
    - **Throughput is SPI-bound, not PHY-bound.** The W5500's 100 Mbit PHY is fed
      over SPI, and the per-frame register protocol caps real throughput well below
      line rate. Measured on an ESP32-S3 (`send_chunked`, breadboard wiring):
      ~7.2 Mbit/s at the default 20 MHz SPI clock, ~8.2 Mbit/s at 24 MHz, plateauing
      near the W5500's internal ~8.3 Mbit/s ceiling around 30 MHz. For near-100-Mbit
      speed use an RMII PHY (LAN8720) instead.
    - **SPI clock and signal integrity.** `PC_ETH_W5500_SPI_MHZ` sets the SPI
      clock (default 20; the W5500 allows up to 33.3 MHz). Higher clocks need clean,
      short wiring: on breadboard jumpers, sustained transfers stay reliable to about
      24 MHz, and above ~33 MHz the SPI reads corrupt (a mis-read chip ID or truncated
      transfers) - keep the clock conservative unless the W5500 is on a short, clean
      PCB trace.
- **Ports and firewalls:** open the protocol's TCP/UDP port (table above) between
  the ESP32 and the device. On a flat factory LAN this is automatic; across
  routers you may need a firewall rule.

### Modbus TCP and Modbus master

- **Flags:** `PC_ENABLE_MODBUS` (the ESP32 is the **slave/server**, using this
  library's TCP server with the `PROTO_MODBUS` listener) or
  `PC_ENABLE_MODBUS_MASTER` (the ESP32 is the **master/client**, your sketch
  owns the TCP connection).
- **Settings:** **TCP port 502.** The request carries a **unit identifier**
  (often 1, or 255/0xFF for a native TCP device). No serial parity or baud here;
  Modbus TCP wraps the same PDU in an MBAP header.
- **Codec:** the same `pc_modbus_*` data model and PDU dispatch as RTU, minus the
  CRC (TCP already guarantees integrity). See `src/services/fieldbus/modbus/modbus.h`.

### SunSpec

- **Flag:** `PC_ENABLE_SUNSPEC`.
- **Hardware:** whatever your Modbus link uses (RS-485 for RTU, Wi-Fi/Ethernet
  for TCP). SunSpec is a **map on top of Modbus holding registers**, not a new
  wire.
- **Settings:** the device exposes a chain of "models" beginning at a base
  holding register (commonly **40000**, sometimes 50000) marked by the ASCII
  **`SunS`** signature. Read that register block over Modbus, then walk it.
- **Codec:** `pc_sunspec_check_marker` / `pc_sunspec_begin` / `pc_sunspec_next_model` plus
  typed point readers. Makes a solar inverter, meter, or battery interoperable.
  See `src/services/energy/sunspec/sunspec.h`.

### S7comm (Siemens)

- **Flag:** `PC_ENABLE_S7COMM` (needs `PC_ENABLE_COTP`).
- **Hardware:** Wi-Fi/Ethernet to a Siemens S7 PLC.
- **Settings:** **TCP port 102** (ISO-on-TCP). The connection is addressed by a
  **rack and slot** number that identify the CPU (for example rack 0 / slot 1 or
  slot 2 on an S7-300, rack 0 / slot 1 on an S7-1200/1500); the PLC must also
  permit "PUT/GET" access for external reads.
- **Codec:** `pc_s7_build_setup` / `pc_s7_build_read_request` / `pc_s7_parse_header` /
  `pc_s7_read_next_item`, wrapped with `pc_cotp_build_dt` + `pc_tpkt_build`. See
  `src/services/fieldbus/s7comm/s7comm.h`.

### MELSEC (Mitsubishi)

- **Flag:** `PC_ENABLE_MELSEC`.
- **Hardware:** Wi-Fi/Ethernet to a Mitsubishi PLC with an MC-protocol port open.
- **Settings:** **TCP or UDP**, on the **port you configure on the PLC** (MC
  protocol has no fixed IANA port). This codec speaks the **binary 3E** frame.
  Pick word or bit devices (D, M, X, Y, R, ...) by their device code.
- **Codec:** `pc_melsec_build_read` / `pc_melsec_parse_response`. See
  `src/services/fieldbus/melsec/melsec.h`.

### FINS (Omron)

- **Flag:** `PC_ENABLE_FINS`.
- **Hardware:** Wi-Fi/Ethernet to an Omron PLC. (FINS also has a serial sibling;
  for serial use [Host Link](#host-link-omron).)
- **Settings:** **FINS/UDP, port 9600** by default. Addressing is a
  **network / node / unit** triple for both source and destination; the node
  number usually matches the last octet of the PLC's IP by convention.
- **Codec:** `pc_fins_build_command` / `pc_fins_build_memory_area_read` /
  `pc_fins_parse_response`, carried over this library's UDP transport
  (`Udp.client->sendto`). See `src/services/fieldbus/fins/fins.h`.

### BACnet/IP

- **Flag:** `PC_ENABLE_BACNET`.
- **Hardware:** Wi-Fi/Ethernet to a building-automation network.
- **Settings:** **UDP port 47808** (hex 0xBAC0). Devices are identified by a
  **device instance** number; multi-subnet sites use a BBMD/`network number`. The
  codec builds the BVLC + NPDU envelope; the APDU object/service layer rides on
  top.
- **Codec:** see `src/services/fieldbus/bacnet/bacnet.h`.

### EtherNet/IP and CIP

- **Flags:** `PC_ENABLE_ENIP` (the encapsulation) and `PC_ENABLE_CIP` (the
  object messages, which turn ENIP on automatically).
- **Hardware:** Wi-Fi/Ethernet to an ODVA / Allen-Bradley device.
- **Settings:** **TCP port 44818** for explicit messaging (register a session,
  then send requests); **UDP port 2222** carries implicit (cyclic I/O) data. CIP
  addresses objects by a **class / instance / attribute** path (an EPATH).
- **Codec:** `enip_*` builds the 24-byte encapsulation header and CPF items;
  `pc_cip_build_epath` + `pc_cip_build_get_attribute_single` build the CIP request. See
  `src/services/fieldbus/enip/enip.h` and `src/services/fieldbus/cip/cip.h`.

### DNP3 and C37.118 over IP

- **DNP3** (`PC_ENABLE_DNP3`): the IEEE 1815 SCADA outstation link. Over IP it
  uses **TCP (or UDP) port 20000**; data-link **source and destination addresses**
  are 16-bit. The codec frames and CRC-checks the `0x0564` data-link layer; the
  transport-function and application (objects/function codes) layer on top. See
  `src/services/energy/dnp3/dnp3.h`.
- **C37.118** (`PC_ENABLE_C37118`): the IEEE C37.118.2 synchrophasor stream
  from a power-grid PMU/PDC. There is **no IANA-assigned port** (TCP/UDP 4712 and
  4713 are common conventions). Each PMU/PDC is identified by an **IDCODE**; a
  Command frame starts/stops the data stream. See `src/services/energy/c37118/c37118.h`.

### IEC 60870-5 (-104 and -101)

`PC_ENABLE_IEC60870`. The European utility-SCADA protocol. It has two
transports and this codec does both:

- **-104 over TCP (port 2404)** - no extra hardware beyond Wi-Fi. Open the link
  with a U-format `STARTDT act` (`pc_iec104_build_u`), then exchange I-format APDUs
  (`pc_iec104_build_i` wraps an ASDU; `pc_iec104_parse` reads one back) and acknowledge
  with S-format. Build the ASDU with `pc_iec_asdu_build_header` + `pc_iec_put_ioa` and
  the type-specific information elements.
- **-101 over serial** - wire an RS-232 or RS-485 transceiver as in the serial
  section. Frame with `pc_iec101_build_fixed` / `pc_iec101_build_variable` and parse
  with `pc_iec101_parse` (the ASDU payload is identical to -104's).

This makes a clean **protocol-converting gateway**: terminate -101 from an old
RTU on the serial side and re-expose it as -104 over Wi-Fi (or vice-versa), since
both share the ASDU layer. See `src/services/energy/iec60870/iec60870.h`.

### OPC UA (server and client)

- **Flags:** `PC_ENABLE_OPCUA` (the ESP32 is the **server**, using this
  library's TCP listener) and/or `PC_ENABLE_OPCUA_CLIENT` (the ESP32 connects
  out to another server; your sketch owns the socket).
- **Hardware:** Wi-Fi/Ethernet.
- **Settings:** **TCP port 4840** (the default OPC UA Binary endpoint, URL
  `opc.tcp://<ip>:4840`). This library implements **SecurityPolicy `None`** (no
  message encryption), so configure the peer to allow an unencrypted endpoint, or
  put the link on a trusted/segmented network.
- **Codec:** see `src/services/fieldbus/opcua/` and `src/services/fieldbus/opcua_client/`.

### SNMP (agent and traps)

- **Flags:** `PC_ENABLE_SNMP` (agent), `PC_ENABLE_SNMP_V3` (USM
  authentication/privacy), `PC_ENABLE_SNMP_TRAP` (outbound traps/informs).
- **Hardware:** Wi-Fi/Ethernet to your monitoring system.
- **Settings:** the agent listens on **UDP port 161**; traps and informs go to
  the manager on **UDP port 162**. v1/v2c use a **community string** (a shared
  password, sent in clear), so prefer **v3** (USM user with authentication and
  privacy keys) on untrusted networks.
- **Codec:** see `src/services/net/snmp/`.

## Radio: ESP-NOW

- **Flag:** `PC_ENABLE_ESPNOW`.
- **Hardware:** **none beyond a second board.** ESP-NOW uses the ESP32's built-in
  2.4 GHz radio to talk directly to other ESP32/ESP8266 boards with no router or
  access point in between.
- **Settings:** peers must be on the **same Wi-Fi channel**; you address a peer by
  its **6-byte MAC address** and register it before sending. Optional payload
  encryption uses a 16-byte primary key (PMK) and per-peer local key (LMK).
  Messages are short (up to ~250 bytes), so larger data must be chunked.
- **Codec:** a typed envelope plus a peer registry; the send side binds to the
  ESP-IDF `esp_now` API. See `src/services/radio/espnow/`.

## Sensor and peripheral breakouts (I2C / UART, no transceiver)

Not everything the ESP32 talks to is an industrial machine. This library also drives
the cheap **sensor and actuator breakout boards** you solder onto a breadboard: a
clock, a presence radar, touch pads, a thermometer, a servo driver, and two kinds of
meter. Unlike the field buses above, these run at **3.3 V and wire directly** - no
transceiver, no level shifter. Most are **I2C** (two shared wires); the LD2410 and a
GPS are plain **UART**.

Each is a `PC_ENABLE_*` flag (default off) with a pure, host-tested codec and a
hand-held beginner example under `examples/L7-Application/`.

| Breakout         | Flag                 | Bus  | Address | Wiring                              | Measures / does                    |
| ---------------- | -------------------- | ---- | ------- | ----------------------------------- | ---------------------------------- |
| DS3231 / DS1307  | `PC_ENABLE_RTC`      | I2C  | 0x68    | SDA 21, SCL 22 (+ coin cell)        | Battery-backed wall-clock time     |
| SHT3x (GY-SHT31) | `PC_ENABLE_SHT3X`    | I2C  | 0x44    | SDA 21, SCL 22                      | Temperature + humidity             |
| MPR121           | `PC_ENABLE_MPR121`   | I2C  | 0x5A    | SDA 21, SCL 22, ADDR->GND           | 12 capacitive touch buttons        |
| ADS1115          | `PC_ENABLE_ADS1115`  | I2C  | 0x48    | SDA 21, SCL 22, ADDR->GND           | 16-bit analog voltage (4 channels) |
| INA219           | `PC_ENABLE_INA219`   | I2C  | 0x40    | SDA 21, SCL 22, Vin+/Vin- in series | Current + power of a load          |
| PCA9685          | `PC_ENABLE_PCA9685`  | I2C  | 0x40    | SDA 21, SCL 22, V+ = servo supply   | 16 PWM / servo outputs             |
| LD2410           | `PC_ENABLE_LD2410`   | UART | -       | module TX -> GPIO 16, RX -> GPIO 17 | mmWave human presence / motion     |
| GT-U7 GPS (NMEA) | `PC_ENABLE_NMEA0183` | UART | -       | module TX -> GPIO 16 (9600 baud)    | Position / time (NMEA 0183)        |

### The shared I2C bus

I2C lets many chips share just **two** wires: **SDA** (data) and **SCL** (clock). On
the classic ESP32 these default to **GPIO 21 (SDA)** and **GPIO 22 (SCL)**. Wire every
I2C breakout's SDA to 21 and SCL to 22 (they all share the pair), give each `3V3` and
`GND`, and the ESP32 tells them apart by their distinct **address**. Most breakouts
already include the small pull-up resistors I2C needs. If two boards share an address
(the PCA9685 and INA219 both default to 0x40), change one with its address solder pads.

All the I2C drivers bring the bus up and address it through one shared owner
(`services/peripherals/i2c.h`), so there is a single place to move the pins:
**`PC_I2C_SDA_PIN` / `PC_I2C_SCL_PIN`** (default `-1` = the platform default 21 /
22). Set both to free GPIOs to relocate the whole bus.

Two more knobs live with them. **`PC_I2C_HZ`** (default `100000`) is the bus clock;
100 kHz standard mode is what every driver here is rated for, and a device that
supports 400 kHz fast mode will take it if the wiring is short and well pulled up.
**`PC_I2C_TIMEOUT_MS`** (default `50`) bounds one transfer, so a device that stops
clocking stalls that read instead of the main loop.

> **Running alongside wired Ethernet.** Only the **classic ESP32 (WROOM/WROVER)** and
> the **ESP32-P4** have the built-in RMII Ethernet MAC that a **LAN8720** PHY needs;
> the **ESP32-S3 / C3 have no RMII MAC** and do wired Ethernet over **SPI** with a
> **W5500** instead. On an RMII board the LAN8720 uses **GPIO 21 (TX_EN)** and
> **GPIO 22 (TXD1)** - the very pins I2C defaults to - plus, on boards that route the
> 50 MHz RMII clock there, GPIO 16/17. So with an RMII PHY enabled you **must** move
> the peripheral buses off those pins: set `-DPC_I2C_SDA_PIN=32
-DPC_I2C_SCL_PIN=33` (or any free pair) to relocate the I2C sensors, and pass
> **alternate RX/TX** to `pc_ld2410_begin(rx, tx)` / your GPS
> `Serial.begin(baud, SERIAL_8N1, rx, tx)` (e.g. GPIO 4 / 2). Avoid the other RMII
> pins (19, 23, 25, 26, 27, 18) and the strapping pins (0, 2, 5, 12, 15). On an S3 with
> a W5500 there is no RMII clash, but still keep the I2C/UART pins clear of the SPI pins
> the W5500 uses.

### Time: RTC and GPS

- **RTC (DS3231 / DS1307), `PC_ENABLE_RTC`** - a battery-backed clock chip at I2C
  0x68 so the board knows the time the instant it boots, offline. Wire SDA/SCL, fit its
  coin cell, and register `pc_rtc_time_source` with the time-source chain;
  `pc_rtc_read_epoch()` / `pc_rtc_set_epoch()` read and set it. Example Rtc.
- **GPS (u-blox GT-U7), `PC_ENABLE_NMEA0183`** - the GT-U7 is a u-blox-7 GPS that
  streams **NMEA 0183** sentences over a plain 3.3 V UART at **9600 baud** (it also has
  a **PPS** pin that pulses once a second for precise timing). Wire its **TX to an
  ESP32 RX** (its RX is optional - only needed to send it config). Accumulate a line
  and `pc_nmea0183_parse()` it, then read the fix with the field helpers - see
  [NMEA 0183](#nmea-0183-gps--marine) above and the TimeSourceFallback example. In a time-source
  chain it is the best source: **GPS -> RTC -> upstream NTP** (feed all three to the
  NTP server to serve time to your whole LAN).

### Sensing: radar, touch, temperature/humidity, voltage, current

- **LD2410 mmWave radar, `PC_ENABLE_LD2410`** - a 24 GHz presence sensor that sees a
  still person (breathing), in the dark, through thin walls - over a UART at **256000
  baud**. Cross the data wires (module TX -> ESP32 RX). `pc_ld2410_poll()` decodes each
  frame; `pc_ld2410_present()` / `pc_ld2410_distance_cm()` act on it. Example Ld2410.
- **MPR121 capacitive touch, `PC_ENABLE_MPR121`** - turns 12 wires or pads into
  touch buttons (I2C 0x5A). `pc_mpr121_read_touched()` returns a 12-bit mask. Example
  Mpr121.
- **SHT3x temperature / humidity, `PC_ENABLE_SHT3X`** - a CRC-checked Sensirion
  sensor (I2C 0x44). `pc_sht3x_read()` returns temperature and humidity in integer
  milli-units. Example Sht3x.
- **ADS1115 16-bit ADC, `PC_ENABLE_ADS1115`** - four precise analog inputs with a
  programmable gain (I2C 0x48). `pc_ads1115_read_uv()` gives a channel's voltage in
  microvolts. Example Ads1115.
- **INA219 current / power, `PC_ENABLE_INA219`** - sits **in series** with a load
  (Vin+ -> shunt -> Vin-) and reports voltage, current, and power (I2C 0x40).
  `pc_ina219_read_current_ua()` / `pc_ina219_read_power_uw()`. Example Ina219.

### Actuation: servos and LEDs

- **PCA9685 16-channel PWM, `PC_ENABLE_PCA9685`** - drives up to 16 servos or LEDs
  from the I2C bus (0x40), with its own precise PWM timer. Power the **servos** from the
  board's `V+` screw terminal (a separate 5-6 V supply), never the ESP32.
  `pc_pca9685_set_servo_us()` positions a servo; `pc_pca9685_set_pwm()` dims an LED. Example
  Pca9685.

These all follow the same shape as the field-bus codecs: a pure codec you can unit-test
on a PC, and a thin binding that does the actual I2C / UART transfer on the ESP32.

## Payload codecs that ride an existing link

These format data but do **not** add a new physical connection: they ride
whatever transport already carries the data (MQTT, CoAP, HTTP), so their hardware
needs are simply that transport's.

- **SenML** (`PC_ENABLE_SENML`, implies CBOR): a standard JSON/CBOR shape for
  sensor measurements. The "hardware" is whatever sensor produces the readings;
  send the encoded payload over MQTT/CoAP/HTTP. See `src/services/iot/senml/`.
- **Sparkplug B** (`PC_ENABLE_SPARKPLUG`, implies Protobuf): an MQTT payload
  and topic convention for SCADA. Needs an **MQTT broker** and a Sparkplug host
  (see the MQTT client setup); no new wiring. See `src/services/iot/sparkplug/`.
- **LwM2M TLV** (`PC_ENABLE_LWM2M`): the device-management value encoding
  carried over **CoAP** (this library's CoAP/UDP service); no new wiring. See
  `src/services/iot/lwm2m/`.
- **Protobuf, CBOR, MessagePack, gRPC-Web, NATS, STOMP, Redis, WAMP, AMQP,
  CloudEvents, flow export, Proxy protocol**: pure data/format codecs over Wi-Fi
  sockets or as building blocks; no special hardware beyond the network.

## Electrical safety and reliability

Field wiring lives in a harsher world than a breadboard. A few habits protect the
ESP32 and keep the bus reliable:

- **Match the voltage.** Use a 3.3 V transceiver, or add a level shifter. Never
  wire an RS-232/RS-485 line or a 5/12/24 V signal straight to a GPIO.
- **Share a ground.** Serial links need a common ground reference between the
  ESP32 and the device, in addition to the data wires.
- **Isolate when grounds differ.** If the ESP32 and the machine sit on different
  power systems (very common in industry), use an **isolated** RS-485 transceiver
  (for example the ADM2483/ADM2587) or a digital isolator. This breaks ground
  loops and protects against surges.
- **Terminate and bias the bus** (the 120 ohm and bias resistors above). Most
  "it works on the bench, fails on the long cable" problems are missing
  termination.
- **Protect the inputs.** On long outdoor or motor-heavy runs, add TVS diodes
  across the data lines for surge protection, and keep data cable away from power
  cable.
- **Power the transceiver properly.** A bus driver can draw more than a sensor;
  do not starve it off a weak 3.3 V rail.
- **One talker at a time.** On a 2-wire RS-485 bus, make sure the direction pin
  returns to receive promptly after every transmit, or two devices will collide.

For the security posture of each protocol (many industrial protocols have **no
authentication or encryption** and must run on a trusted network), see
[SECURITY.md](SECURITY.md). For the full description and spec reference of each
codec, see [FEATURES.md](FEATURES.md) and [STANDARDS.md](STANDARDS.md).

## Module reference

<!-- BEGIN GENERATED HARDWARE TABLE (tools/ci_tooling/generate/gen_hardware_ref.py) -->

<!-- prettier-ignore-start -->

**73 modules** attach to something physical. Every one takes its wiring **through the API** -
an I2C address, explicit pins, or a caller-supplied bus struct - so the library never dictates a
pinout and none is documented here. Pure codecs have no bring-up call at all: you own the link.

### Foundation

| Module | Attaches via | Bring-up call | Feature flag |
| ------ | ------------ | ------------- | ------------ |
| `rtc` | I2C | `pc_rtc_begin(void)` | `PC_ENABLE_RTC` |
| `time_source` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_TIME_SOURCE` |

### Physical & Data Link (L1-L2)

| Module | Attaches via | Bring-up call | Feature flag |
| ------ | ------------ | ------------- | ------------ |
| `ads1115` | I2C | `pc_ads1115_begin(uint8_t addr)` | `PC_ENABLE_ADS1115` |
| `dshot` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_DSHOT` |
| `fdc2214` | I2C | `pc_fdc2214_begin(uint8_t addr, uint16_t rcount, uint16_t settlecount)` | `PC_ENABLE_FDC2214` |
| `ina219` | I2C | `pc_ina219_begin(uint8_t addr, uint32_t current_lsb_ua, uint32_t shunt_mohm)` | `PC_ENABLE_INA219` |
| `ld2410` | UART | `pc_ld2410_begin(int rx_pin, int tx_pin)` | `PC_ENABLE_LD2410` |
| `ldc1614` | I2C | `pc_ldc1614_begin(uint8_t addr, uint16_t rcount, uint16_t settlecount)` | `PC_ENABLE_LDC1614` |
| `mpr121` | I2C | `pc_mpr121_begin(uint8_t addr)` | `PC_ENABLE_MPR121` |
| `pca9685` | I2C | `pc_pca9685_begin(uint8_t addr, uint32_t freq_hz)` | `PC_ENABLE_PCA9685` |
| `pn532` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_PN532` |
| `sht3x` | I2C | `pc_sht3x_begin(uint8_t addr)` | `PC_ENABLE_SHT3X` |
| `vl53l0x` | I2C | `pc_vl53l0x_begin(uint8_t addr)` | `PC_ENABLE_VL53L0X` |
| `ble_gatt` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_BLE_GATT` |
| `cc1101` | SPI (caller-supplied bus) | `pc_cc1101_init(const pc_cc1101_bus *bus, const pc_cc1101_config *cfg)` | `PC_ENABLE_CC1101` |
| `enocean` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_ENOCEAN` |
| `espnow` | caller-supplied link | `pc_espnow_begin(uint8_t channel, pc_espnow_recv_fn cb)` | `PC_ENABLE_ESPNOW` |
| `lora` | SPI (caller-supplied bus) | `pc_lora_init(const pc_lora_bus *bus, const pc_lora_config *cfg)` | `PC_ENABLE_LORA` |
| `nrf24` | SPI (caller-supplied bus) | `pc_nrf24_init(const nrf_bus *bus, const nrf_config *cfg)` | `PC_ENABLE_NRF24` |
| `promisc` | caller-supplied link | `pc_promisc_begin(uint8_t channel, pc_promisc_sink_fn sink)` | `PC_ENABLE_PROMISC` |
| `radio_sniff` | CAN | _none (pure codec)_ | `PC_ENABLE_RADIO_SNIFF` |
| `sigfox` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_SIGFOX` |
| `thread` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_THREAD` |
| `wifi_sniffer` | caller-supplied link | `pc_wifi_sniffer_begin(uint8_t first_chan, uint8_t last_chan, uint16_t dwell_ms)` | `PC_ENABLE_WIFI_SNIFFER` |
| `wisun` | caller-supplied link | `pc_wisun_init(WisunFan *fan, const pc_ip *border_router, WisunNode *storage, size_t cap)` | `PC_ENABLE_WISUN` |
| `zigbee` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_ZIGBEE` |
| `zwave` | CAN | _none (pure codec)_ | `PC_ENABLE_ZWAVE` |
| `rawl2` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_RAWL2` |

### Industrial & Fieldbus

| Module | Attaches via | Bring-up call | Feature flag |
| ------ | ------------ | ------------- | ------------ |
| `dmx` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_DMX` |
| `sdi12` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_SDI12` |
| `ads` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_ADS` |
| `bacnet` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_BACNET` |
| `canopen` | CAN | `pc_canopen_sdo_reasm_init(CanopenSdoReasm *r, uint8_t *buf, size_t cap)` | `PC_ENABLE_CANOPEN` |
| `cclink` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_CCLINK` |
| `cia402` | CAN | _none (pure codec)_ | `PC_ENABLE_CIA402` |
| `cip` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_CIP` |
| `cotp` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_COTP` |
| `devicenet` | CAN | _none (pure codec)_ | `PC_ENABLE_DEVICENET` |
| `df1` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_DF1` |
| `directnet` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_DIRECTNET` |
| `enip` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_ENIP` |
| `fins` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_FINS` |
| `hart` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_HART` |
| `hostlink` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_HOSTLINK` |
| `interbus` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_INTERBUS` |
| `iolink` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_IOLINK` |
| `lonworks` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_LONWORKS` |
| `mbplus` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_MBPLUS` |
| `melsec` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_MELSEC` |
| `modbus` | caller-supplied link | `pc_modbus_server_init()` | `PC_ENABLE_MODBUS` |
| `powerlink` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_POWERLINK` |
| `profibus` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_PROFIBUS` |
| `profinet` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_PROFINET` |
| `s7comm` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_S7COMM` |
| `sercos` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_SERCOS` |
| `snp` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_SNP` |

### SCADA, Energy & Monitoring

| Module | Attaches via | Bring-up call | Feature flag |
| ------ | ------------ | ------------- | ------------ |
| `mbus` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_MBUS` |

### Machine Tools & OT

| Module | Attaches via | Bring-up call | Feature flag |
| ------ | ------------ | ------------- | ------------ |
| `opcua` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_OPCUA` |
| `opcua_client` | caller-supplied link | `pc_opcua_client_init(OpcUaClient *c)` | `PC_ENABLE_OPCUA_CLIENT` |

### Transportation & ITS

| Module | Attaches via | Bring-up call | Feature flag |
| ------ | ------------ | ------------- | ------------ |
| `j1939` | CAN | _none (pure codec)_ | `PC_ENABLE_J1939` |
| `gnss` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_NMEA0183` |
| `nmea0183` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_NMEA0183` |
| `nmea2000` | CAN | _none (pure codec)_ | `PC_ENABLE_NMEA2000` |

### Application (L7) - Other

| Module | Attaches via | Bring-up call | Feature flag |
| ------ | ------------ | ------------- | ------------ |
| `ad9238` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_AD9238` |
| `hmmd` | UART | `pc_hmmd_begin(int rx_pin, int tx_pin)` | `PC_ENABLE_HMMD` |
| `rcwl0516` | caller-supplied link | `pc_rcwl0516_core_init(PresenceCore *c, uint32_t now)` | `PC_ENABLE_RCWL0516` |
| `sen0192` | caller-supplied link | `pc_sen0192_motion_init(Sen0192Motion *m, uint32_t hold_ms, proto_bool active_high)` | `PC_ENABLE_SEN0192` |
| `gpib` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_GPIB` |
| `hislip` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_HISLIP` |
| `scpi` | caller-supplied link | `pc_scpi_status_init(ScpiStatus *s)` | `PC_ENABLE_SCPI` |
| `vxi11` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_VXI11` |
| `simatic` | codec only - caller owns the link | _none (pure codec)_ | `PC_ENABLE_SIMATIC` |
| `ubx` | caller-supplied link | `pc_ubx_stream_init(pc_ubx_stream *st)` | `PC_ENABLE_UBX` |

<!-- prettier-ignore-end -->

<!-- END GENERATED HARDWARE TABLE -->
