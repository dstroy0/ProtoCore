// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file MediaStreaming.ino
 * @brief Live camera video (MJPEG) + microphone audio (WAV) through the server's streaming TX path.
 *
 * Board: **Seeed XIAO ESP32-S3 Sense** (ESP32-S3 + 8 MB PSRAM, OV2640 camera, PDM microphone). The
 * media code needs an ESP32-S3 on arduino-esp32 3.x (the `esp_camera` + `ESP_I2S` components); on any
 * other target it compiles to a stub that says so, so the example still builds everywhere.
 *
 * Everything streams through one library call - `send_chunked(slot, code, type, source, ctx)` - which
 * pages an unbounded body out in constant memory as the TCP window drains. The two `source` callbacks
 * here are live hardware producers:
 *   - the camera source frames each JPEG into a `multipart/x-mixed-replace` MJPEG stream (a webcam), and
 *   - the microphone source emits a WAV header once then continuous 16 kHz PCM (an audio stream).
 * The library owns the HTTP framing and flow control; you supply the bytes.
 *
 * Routes:  /  (a page with the video + an audio player) · /video (MJPEG) · /photo.jpg (one frame)
 *          /audio.wav (live audio) · /clip.wav (a 2 s recording)
 *
 * Edit the two CHANGE ME Wi-Fi lines, flash with **PSRAM enabled** (XIAO ESP32-S3, PSRAM: "OPI PSRAM"),
 * open Serial @ 115200 for the board's IP, and browse to it.
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

// The camera + mic drivers are ESP32-S3 / arduino-esp32-3.x only; guard so the sketch builds anywhere.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0) && defined(CONFIG_IDF_TARGET_ESP32S3)
#define MEDIA_SUPPORTED 1
#include "esp_camera.h"
#include <ESP_I2S.h>
#include <esp_heap_caps.h>
#include <string.h>
#else
#define MEDIA_SUPPORTED 0
#endif

// --- CHANGE ME: your Wi-Fi ---
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


#if MEDIA_SUPPORTED

// --- XIAO ESP32-S3 Sense pin map (OV2640 over the DVP bus; PDM mic on I2S) ---
#define CAM_XCLK 10
#define CAM_SIOD 40
#define CAM_SIOC 39
#define CAM_Y9 48
#define CAM_Y8 11
#define CAM_Y7 12
#define CAM_Y6 14
#define CAM_Y5 16
#define CAM_Y4 18
#define CAM_Y3 17
#define CAM_Y2 15
#define CAM_VSYNC 38
#define CAM_HREF 47
#define CAM_PCLK 13
#define MIC_CLK 42
#define MIC_DATA 41

static const uint32_t AUDIO_RATE = 16000; // 16 kHz / 16-bit / mono PCM
I2SClass I2S;
static bool g_cam = false, g_mic = false;

static bool camera_init()
{
    camera_config_t c;
    memset(&c, 0, sizeof(c));
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer = LEDC_TIMER_0;
    c.pin_d0 = CAM_Y2;
    c.pin_d1 = CAM_Y3;
    c.pin_d2 = CAM_Y4;
    c.pin_d3 = CAM_Y5;
    c.pin_d4 = CAM_Y6;
    c.pin_d5 = CAM_Y7;
    c.pin_d6 = CAM_Y8;
    c.pin_d7 = CAM_Y9;
    c.pin_xclk = CAM_XCLK;
    c.pin_pclk = CAM_PCLK;
    c.pin_vsync = CAM_VSYNC;
    c.pin_href = CAM_HREF;
    c.pin_sccb_sda = CAM_SIOD;
    c.pin_sccb_scl = CAM_SIOC;
    c.pin_pwdn = -1;
    c.pin_reset = -1;
    c.xclk_freq_hz = 20000000;
    c.frame_size = FRAMESIZE_VGA; // 640x480
    c.pixel_format = PIXFORMAT_JPEG;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    c.fb_location = CAMERA_FB_IN_PSRAM; // frame buffers live in PSRAM
    c.jpeg_quality = 12;
    c.fb_count = 2;
    return esp_camera_init(&c) == ESP_OK;
}

// ---- video: MJPEG (multipart/x-mixed-replace) ----
static const char *MJPEG_BOUNDARY = "detframe";
struct VideoCtx
{
    camera_fb_t *fb;
    size_t off;
    int phase; // 0 grab frame, 1 part header, 2 jpeg body
    char hdr[96];
    size_t hlen, hoff;
};
static VideoCtx g_vid;
static size_t video_source(uint8_t *buf, size_t cap, void *v)
{
    VideoCtx *s = (VideoCtx *)v;
    if (s->phase == 0)
    {
        s->fb = esp_camera_fb_get();
        if (!s->fb)
        {
            return 0; // camera stall ends the stream
        }
        s->hlen = (size_t)snprintf(s->hdr, sizeof(s->hdr),
                                   "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", MJPEG_BOUNDARY,
                                   (unsigned)s->fb->len);
        s->hoff = 0;
        s->off = 0;
        s->phase = 1;
    }
    if (s->phase == 1)
    {
        size_t n = (s->hlen - s->hoff) < cap ? (s->hlen - s->hoff) : cap;
        memcpy(buf, s->hdr + s->hoff, n);
        s->hoff += n;
        if (s->hoff >= s->hlen)
        {
            s->phase = 2;
        }
        return n;
    }
    size_t n = (s->fb->len - s->off) < cap ? (s->fb->len - s->off) : cap;
    memcpy(buf, s->fb->buf + s->off, n);
    s->off += n;
    if (s->off >= s->fb->len)
    {
        esp_camera_fb_return(s->fb);
        s->fb = nullptr;
        s->phase = 0; // next frame; the leading CRLF of its header closes this part
    }
    return n;
}
void handle_video(uint8_t slot, HttpReq *)
{
    if (!g_cam)
    {
        send_text(slot, 503, "text/plain", "camera not ready");
        return;
    }
    // If the previous stream was cut mid-frame (a browser navigated away), the pump stopped before the
    // source could return its in-flight frame buffer; reclaim it now, or the camera runs out of buffers.
    if (g_vid.fb)
    {
        esp_camera_fb_return(g_vid.fb);
    }
    memset(&g_vid, 0, sizeof(g_vid));
    send_chunked(slot, 200, "multipart/x-mixed-replace; boundary=detframe", video_source, &g_vid);
}

// ---- photo: one JPEG ----
struct PhotoCtx
{
    camera_fb_t *fb;
    size_t off;
};
static PhotoCtx g_photo;
static size_t photo_source(uint8_t *buf, size_t cap, void *v)
{
    PhotoCtx *c = (PhotoCtx *)v;
    if (!c->fb)
    {
        return 0;
    }
    size_t rem = c->fb->len - c->off;
    if (!rem)
    {
        esp_camera_fb_return(c->fb);
        c->fb = nullptr;
        return 0;
    }
    size_t n = rem < cap ? rem : cap;
    memcpy(buf, c->fb->buf + c->off, n);
    c->off += n;
    return n;
}
void handle_photo(uint8_t slot, HttpReq *)
{
    if (!g_cam)
    {
        send_text(slot, 503, "text/plain", "camera not ready");
        return;
    }
    if (g_photo.fb)
    {
        esp_camera_fb_return(g_photo.fb); // reclaim a buffer left by an aborted /photo.jpg
    }
    g_photo.fb = esp_camera_fb_get();
    g_photo.off = 0;
    if (!g_photo.fb)
    {
        send_text(slot, 500, "text/plain", "capture failed");
        return;
    }
    send_chunked(slot, 200, "image/jpeg", photo_source, &g_photo);
}

// ---- audio: WAV (44-byte RIFF header, little-endian) ----
static void wav_header(uint8_t *h, uint32_t data_len)
{
    uint16_t ch = 1, bits = 16;
    uint32_t byte_rate = AUDIO_RATE * ch * bits / 8, riff = 36 + data_len, fmtlen = 16;
    uint16_t block_align = ch * bits / 8, pcm = 1;
    memcpy(h, "RIFF", 4);
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    memcpy(h + 16, &fmtlen, 4);
    memcpy(h + 20, &pcm, 2);
    memcpy(h + 22, &ch, 2);
    memcpy(h + 24, &AUDIO_RATE, 4);
    memcpy(h + 28, &byte_rate, 4);
    memcpy(h + 32, &block_align, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_len, 4);
}

// live audio: header once, then continuous mic PCM forever
struct AudioCtx
{
    bool hdr_sent;
    size_t hoff;
    uint8_t hdr[44];
};
static AudioCtx g_aud;
static size_t audio_source(uint8_t *buf, size_t cap, void *v)
{
    AudioCtx *s = (AudioCtx *)v;
    if (!s->hdr_sent)
    {
        size_t n = (44 - s->hoff) < cap ? (44 - s->hoff) : cap;
        memcpy(buf, s->hdr + s->hoff, n);
        s->hoff += n;
        if (s->hoff >= 44)
        {
            s->hdr_sent = true;
        }
        return n;
    }
    return I2S.readBytes((char *)buf, cap); // continuous 16-bit PCM, paced by the mic (1x realtime)
}
void handle_audio(uint8_t slot, HttpReq *)
{
    if (!g_mic)
    {
        send_text(slot, 503, "text/plain", "mic not ready");
        return;
    }
    wav_header(g_aud.hdr, 0xFFFFFF00u); // a large data size: a never-ending streaming WAV
    g_aud.hdr_sent = false;
    g_aud.hoff = 0;
    send_chunked(slot, 200, "audio/wav", audio_source, &g_aud);
}

// a fixed 2 s WAV recording
static const uint32_t CLIP_DATA = AUDIO_RATE * 2 * 2; // 2 s of 16-bit mono
static uint8_t *g_clip = nullptr;                     // 44 + CLIP_DATA in PSRAM
struct ClipCtx
{
    size_t total, off;
};
static ClipCtx g_cc;
static size_t clip_source(uint8_t *buf, size_t cap, void *v)
{
    ClipCtx *c = (ClipCtx *)v;
    size_t rem = c->total - c->off;
    if (!rem)
    {
        return 0;
    }
    size_t n = rem < cap ? rem : cap;
    memcpy(buf, g_clip + c->off, n);
    c->off += n;
    return n;
}
void handle_clip(uint8_t slot, HttpReq *)
{
    if (!g_mic || !g_clip)
    {
        send_text(slot, 503, "text/plain", "mic not ready");
        return;
    }
    wav_header(g_clip, CLIP_DATA);
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < CLIP_DATA && millis() - t0 < 5000)
    {
        size_t n = I2S.readBytes((char *)(g_clip + 44 + got), CLIP_DATA - got);
        if (!n)
        {
            break;
        }
        got += n;
    }
    g_cc.total = 44 + got;
    g_cc.off = 0;
    send_chunked(slot, 200, "audio/wav", clip_source, &g_cc);
}

static void media_begin()
{
    g_cam = camera_init();
    I2S.setPinsPdmRx(MIC_CLK, MIC_DATA);
    g_clip = (uint8_t *)heap_caps_malloc(44 + CLIP_DATA, MALLOC_CAP_SPIRAM);
    g_mic = I2S.begin(I2S_MODE_PDM_RX, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO) && g_clip;
    Serial.printf("camera %s, mic %s\n", g_cam ? "OK" : "FAILED", g_mic ? "OK" : "FAILED");
    on_http("/video", HttpMethod::HTTP_GET, handle_video);
    on_http("/photo.jpg", HttpMethod::HTTP_GET, handle_photo);
    on_http("/audio.wav", HttpMethod::HTTP_GET, handle_audio);
    on_http("/clip.wav", HttpMethod::HTTP_GET, handle_clip);
}

#endif // MEDIA_SUPPORTED

void handle_root(uint8_t slot, HttpReq *)
{
#if MEDIA_SUPPORTED
    send_text(slot, 200, "text/html",
                "<html><body style=\"font-family:sans-serif\"><h3>XIAO ESP32-S3 Sense</h3>"
                "<img src=\"/video\" style=\"max-width:100%\"><br>"
                "<audio controls src=\"/audio.wav\"></audio><br>"
                "<a href=\"/photo.jpg\">photo</a> &middot; <a href=\"/clip.wav\">2s clip</a></body></html>");
#else
    send_text(slot, 200, "text/plain",
                "This example streams camera + mic and needs a XIAO ESP32-S3 Sense "
                "(ESP32-S3 + arduino-esp32 3.x). Build it for that board to see /video and /audio.wav.");
#endif
}

void setup()
{
    Serial.begin(115200);
    delay(300);
#if !MEDIA_SUPPORTED
    Serial.println("Media (camera/mic) needs an ESP32-S3 on arduino-esp32 3.x - running web-only stub.");
#endif
    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Wi-Fi connecting");
    uint32_t t0 = millis();
    while (!wifi_ready() && millis() - t0 < 20000)
    {
        delay(250);
        Serial.print('.');
    }
    if (!wifi_ready())
    {
        Serial.println(" failed - check SSID/PASSWORD");
        return;
    }
    uint32_t ip = pc_net_egress_ip();
    Serial.printf("\nOpen http://%u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    on_http("/", HttpMethod::HTTP_GET, handle_root);
#if MEDIA_SUPPORTED
    media_begin();
#endif
    begin_http(80);
}

void loop()
{
    handle();
}
