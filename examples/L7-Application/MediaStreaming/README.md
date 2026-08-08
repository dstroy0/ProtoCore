# MediaStreaming - live camera video + microphone audio (XIAO ESP32-S3 Sense)

**Layer:** L7-Application · **Board:** Seeed XIAO ESP32-S3 Sense · **Core:** arduino-esp32 3.x · **PSRAM required**

Turn the board into a tiny webcam-with-a-mic: browse to it and you get a **live MJPEG video** from the
OV2640 camera and a **live audio player** fed by the onboard PDM microphone - both streamed by the server.

## What this example teaches

Every response here goes out through **one** library call:

```cpp
server.send_chunked(slot, 200, content_type, source, ctx);
```

`send_chunked` pages an **unbounded** body onto the wire in **constant memory** - it pulls a few KB from
your `source` callback each time the TCP send window has room, and resumes on the next loop as it drains. So
the body can be far larger than any buffer, and it is never truncated at the window. The two sources here are
**live hardware producers**:

- **Video** (`/video`): each call frames one JPEG into a `multipart/x-mixed-replace` MJPEG stream - the exact
  format a browser `<img>` plays as motion video. The `source` is a small state machine: emit the part
  boundary + headers, then the JPEG bytes, then grab the next frame. It never returns 0, so the stream runs
  until the browser disconnects.
- **Audio** (`/audio.wav`): emit a 44-byte WAV header once, then hand out continuous 16 kHz / 16-bit PCM read
  straight from the microphone. It streams at the microphone's real-time rate (~32 KB/s), which is exactly
  right for audio.

The library owns the HTTP framing and flow control; you only supply the bytes. The same `send_chunked` path
also serves large files, generated reports, and firmware images.

## Routes

| HttpRoute    | What it is                                        |
| ------------ | ------------------------------------------------- |
| `/`          | a page showing the live video and an audio player |
| `/video`     | MJPEG video stream (`multipart/x-mixed-replace`)  |
| `/photo.jpg` | a single still JPEG                               |
| `/audio.wav` | continuous live audio (a never-ending WAV)        |
| `/clip.wav`  | a fixed 2-second WAV recording                    |

## What you will need

- A **Seeed XIAO ESP32-S3 Sense** - the "Sense" expansion carries the OV2640 camera and the PDM microphone,
  and the module has the 8 MB PSRAM the camera frame buffers need.
- The **arduino-esp32 3.x** core (it ships the `esp_camera` and `ESP_I2S` components this example uses). On
  any other board / core the sketch still compiles, but the media routes are replaced by a short "needs a
  XIAO ESP32-S3 Sense" message so it builds everywhere.

The camera and microphone are wired on the board; the pins are already set in the sketch:

| Signal                 | GPIO                           |
| ---------------------- | ------------------------------ |
| Camera XCLK            | 10                             |
| Camera SDA/SCL         | 40 / 39                        |
| Camera VSYNC/HREF/PCLK | 38 / 47 / 13                   |
| Camera data Y2-Y9      | 15, 17, 18, 16, 14, 12, 11, 48 |
| Mic PDM CLK/DATA       | 42 / 41                        |

## Configure and flash

1. Open `MediaStreaming.ino` and set the two **CHANGE ME** lines to your Wi-Fi `SSID` / `PASSWORD`.
2. In the Arduino IDE select **XIAO_ESP32S3** and set **PSRAM: "OPI PSRAM"** (the camera fails to start
   without it). Flash, then open **Serial Monitor @ 115200**; you will see:

```
camera OK, mic OK
Open http://192.168.1.232
```

3. Browse to that address - the page shows live video with an audio player, or open `/video` and
   `/audio.wav` directly.

With `arduino-cli` the same build is:

```sh
arduino-cli compile --upload -p <PORT> \
  --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi \
  --library . examples/L7-Application/MediaStreaming
```

## Verify without a browser

```sh
curl -s http://<BOARD_IP>/photo.jpg  -o photo.jpg          # a JPEG (starts FF D8, ends FF D9)
curl -s http://<BOARD_IP>/clip.wav   -o clip.wav           # a 2 s WAV (RIFF/WAVE, 16 kHz PCM)
curl -s -m 5 http://<BOARD_IP>/video -o video.mjpeg        # several JPEG frames + --detframe boundaries
curl -s -m 5 http://<BOARD_IP>/audio.wav -o audio.wav      # ~160 KB in 5 s = the 32 KB/s real-time rate
```

On real hardware this streams ~21 fps VGA video and real-time audio; the camera's JPEG rate and the Wi-Fi
link set the pace, not the send path. See [FEATURE_PERFORMANCE.md](../../../docs/FEATURE_PERFORMANCE.md) for
the measured numbers.

## Build note (PlatformIO)

The example uses only the base server (no `PC_ENABLE_*` flag), so a plain compile check needs no build
flags:

```sh
pio ci examples/L7-Application/MediaStreaming --board esp32-s3-devkitc-1 --lib "."
```

On a non-S3 board this compiles the web-only stub; build it for the XIAO ESP32-S3 Sense (PSRAM on) to get the
camera and microphone.
