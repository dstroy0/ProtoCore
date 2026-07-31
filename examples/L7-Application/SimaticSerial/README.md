# SimaticSerial - Siemens 3964R + RK512 point-to-point link

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_SIMATIC`

## What this example teaches

Before PROFINET, Siemens controls talked to computers over a serial point-to-point link: **3964R**
(the byte-oriented link protocol on the S5/S7 PtP CP modules - CP 341 / CP 441 / CP 524 / CP 525)
carrying **RK512** computer-link telegrams. `services/simatic` is a zero-heap codec for both; the
RS-232 / RS-485 UART is normally the application's.

**3964R** frames a block as `STX <payload, DLE bytes doubled> DLE ETX [BCC]` with an _interactive_
handshake - the sender emits `STX` and waits for the receiver's connect `DLE`, sends the block, waits
for the end `DLE`/`NAK`, and retries on timeout; the receiver arbitrates a simultaneous-`STX` collision
by station priority. The "R" variant's BCC is the longitudinal **XOR** of the block (a doubled DLE pair
cancels). **RK512** rides inside as a fixed-header **SEND** (write words) / **FETCH** (read words)
telegram addressing a DB / flag / I-O area, big-endian words, answered by a reaction telegram.

```cpp
// 3964R block framing (pure)
size_t n = pc_3964r_build_block(buf, sizeof(buf), data, len, /*with_bcc=*/true);
pc_3964r_parse_block(buf, n, true, out, sizeof(out), &olen);   // un-stuff + verify BCC

// the interactive link state machine (drives STX/DLE + retries via a tx sink)
pc_3964r_init(&ctx, /*high_priority=*/true, /*with_bcc=*/true, tx_sink, on_rx, user);
pc_3964r_send(&ctx, telegram, tn, pc_millis());
pc_3964r_rx_byte(&ctx, incoming, pc_millis());   // feed UART bytes
pc_3964r_tick(&ctx, pc_millis());                // drive QVZ/ZVZ timeouts

// RK512 telegrams (big-endian words)
pc_rk512_build_fetch(t, sizeof(t), Rk512Area::DB, 5, 0x0000, 2);  // read DB5.DBW0, 2 words
pc_rk512_build_send(t, sizeof(t), Rk512Area::MB, 0, 0x0010, words, 4);
```

## Run it (no second device needed)

The sketch runs **two** link stations on one board - A (high priority) and B (low priority) - and
cross-wires their byte sinks, so it drives the whole 3964R handshake + an RK512 FETCH round-trip against
itself. A completed round increments a counter served at `GET /simatic`:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SIMATIC=1" \
  --lib="." examples/L7-Application/SimaticSerial/SimaticSerial.ino
```

```
$ curl http://192.168.1.42/simatic
{"rounds":37,"lastFetchTelegram":"0100010500000002","lastReactionTelegram":"020000"}
```

On a real installation station A is this device and station B a Siemens PtP CP over an RS-232/RS-485
UART - feed the UART's received bytes to `pc_3964r_rx_byte` and let the state machine's tx sink write
the UART.

## Annotated source

The complete sketch is [SimaticSerial.ino](SimaticSerial.ino). The codec is in
[src/services/fieldbus/simatic/simatic.h](../../../src/services/fieldbus/simatic/simatic.h); the 3964R handshake, XOR-BCC,
QVZ/ZVZ timeouts and the RK512 header follow the Siemens "3964(R) transmission protocol" and "RK 512
computer link" CP-module manuals, cross-checked byte-for-byte against an independent python reference
peer (`test/servers/peers/simatic_peer.py`). Pairs with the [HaasMdc](../HaasMdc/README.md)
machine-tool example. The MELSEC and FANUC FOCAS codecs ship as services
(`src/services/fieldbus/melsec`, `src/services/machine_tool/focas`) but have no example yet.
