# 76. NTRIP Caster (GNSS RTK base) + rover

Turn an ESP32 with a GPS module into a **GNSS RTK base station and NTRIP caster**: it surveys in its own
antenna position and serves RTCM 3.x corrections to rovers over the network. A second board runs the
**rover** role — an NTRIP client that subscribes to the base and decodes its position stream.

This one sketch builds either role. Flip `NTRIP_ROLE_BASE` at the top:

- `#define NTRIP_ROLE_BASE 1` → **base**: survey-in + caster on port `2101`, mountpoint `BASE1`.
- `#define NTRIP_ROLE_BASE 0` → **rover**: NTRIP client (set `BASE_IP` to the base board's IP).

Enable the feature for the whole build with `-DPC_ENABLE_NTRIP_CASTER=1` (already in `build_opt.h`
here for the Arduino IDE). It implies `PC_ENABLE_NMEA0183` (the base surveys from GGA fixes). With
PlatformIO:

```sh
pio ci examples/L7-Application/NtripCaster --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_NTRIP_CASTER=1"
```

## Hardware

Two ESP32-S3 boards, each with a GT-U7 (u-blox 6/7-class) GPS:

| GPS pin | ESP32-S3 pin | Notes                                         |
| ------- | ------------ | --------------------------------------------- |
| TX      | GPIO18       | GPS → board RX (Serial1 RX); NMEA at 9600 8N1 |
| RX      | GPIO17       | GPS ← board TX (Serial1 TX)                   |
| PPS     | GPIO4        | 1 pulse/second; counted via interrupt         |
| VCC/GND | 3V3 / GND    |                                               |

## Run it

1. Flash **board A** with `NTRIP_ROLE_BASE 1`. Open Serial @ 115200. It connects to Wi-Fi, prints
   `BASE IP: x.x.x.x`, then surveys in — averaging GT-U7 fixes until it has `SURVEY_MIN_OBS` observations
   with a 3-D spread under `SURVEY_ACC_LIMIT_M`. It prints progress, then `survey COMPLETE` and begins
   broadcasting a `1005` station-reference message once per second.
2. Flash **board B** with `NTRIP_ROLE_BASE 0` and `BASE_IP` set to board A's IP. It connects to the
   caster, subscribes to `/BASE1`, and prints each decoded frame:

    ```
    stream started; decoding RTCM...
    RTCM 1005 sta=2003  ECEF -2706196.881, -4261094.185, 3885757.343 m  ->  37.7749000, -122.4194000  h=52.00 m  [pps=42]
    ```

## Inspect the caster with standard tools

The caster speaks NTRIP 1.0 and 2.0. From any machine on the network:

```sh
# Source table (list of mountpoints):
curl -s http://<BASE_IP>:2101/

# Subscribe to the RTCM stream (raw bytes; Ctrl-C to stop):
curl -s http://<BASE_IP>:2101/BASE1 | xxd | head
```

`str2str` (RTKLIB) and any NTRIP client (e.g. `ntripclient`, u-center) can also connect to
`<BASE_IP>:2101/BASE1`.

## Why both boards still won't get a cm-level RTK fix

RTK needs the base to broadcast **observation** messages (RTCM MSM: 1074/1077/1084/…), which are built
from the receiver's raw carrier-phase / pseudorange measurements. The GT-U7 (like the NEO-6/7) does **not**
output raw measurements (no `UBX-RXM-RAWX`), so it can only serve the surveyed **reference point** (1005/ 1006) — not the observations a rover needs to fix ambiguities. A true RTK base needs an F9P/M8T-class
receiver; with one, the base would add MSM generation and the rover would compute a real fix.

What this example **does** prove, end to end on real hardware: survey-in position averaging →
geodetic→ECEF → RTCM3 framing/CRC → NTRIP caster → NTRIP client → RTCM3 decode. The correction-transport
pipeline is complete; only the raw-measurement source is missing.

## Notes

- **PPS (GPIO4)** is counted via a rising-edge interrupt and printed for observability. It marks the GPS
  second boundary and can gate precise survey sampling or a stratum-1 NTP tie-in (see `NtpServer`).
- **Security**: the caster supports optional HTTP Basic auth per mountpoint — pass the base64 of
  `user:pass` as the third argument to `pc_ntrip_caster_add_mount()` (null = open access).
- Tune `SURVEY_MIN_OBS` / `SURVEY_ACC_LIMIT_M` for how long/tight the survey-in must be before serving.
