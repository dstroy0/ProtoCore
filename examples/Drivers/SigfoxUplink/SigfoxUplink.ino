// SigfoxUplink - send a tiny reading over the Sigfox 0G network.
//
// Sigfox is the opposite direction from the gateway examples: instead of bridging a
// southbound radio northbound, the device itself uplinks a small message (<= 12 bytes,
// ~140/day) over the Sigfox network to the cloud - ultra-low-power telemetry from places
// with no Wi-Fi. A Wisol / Murata Sigfox modem is driven by AT commands over UART; the
// only hardware-specific code is the UART carry.
//
//   reading --pc_sigfox_build_uplink()--> "AT$SF=<hex>" --UART--> modem --> Sigfox cloud
//                                                  modem reply --> pc_sigfox_parse_response()
//
// Needs a Sigfox modem + subscription to actually transmit; the AT-command codec is host-
// tested in test/test_sigfox.
//
// Build flag (whole build): PC_ENABLE_SIGFOX=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "services/radio/sigfox/sigfox.h"

static const int PIN_RX = 16, PIN_TX = 17; // UART2 to the Sigfox modem (9600 baud)

// Read the modem reply for up to timeout_ms and classify it.
static pc_sigfox_result read_reply(uint32_t timeout_ms)
{
    char buf[64];
    uint16_t n = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms && n < sizeof(buf) - 1)
    {
        while (Serial2.available() && n < sizeof(buf) - 1)
        {
            buf[n++] = (char)Serial2.read();
        }
        pc_sigfox_result r = pc_sigfox_parse_response(buf, n);
        if (r != pc_sigfox_result::SIGFOX_PENDING)
        {
            return r;
        }
        delay(5);
    }
    return pc_sigfox_result::SIGFOX_PENDING;
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX); // Wisol default is 9600 8N1
    delay(300);
    Serial.println("Sigfox uplink: reading -> AT$SF -> 0G network");
}

static uint16_t g_seq = 0;

void loop()
{
    // A tiny 4-byte reading: a sequence + a fake sensor value.
    uint16_t value = (uint16_t)(2000 + (g_seq % 500)); // e.g. millivolts
    uint8_t payload[4] = {(uint8_t)(g_seq >> 8), (uint8_t)g_seq, (uint8_t)(value >> 8), (uint8_t)value};

    char cmd[32];
    uint16_t n = pc_sigfox_build_uplink(payload, sizeof(payload), cmd, sizeof(cmd));
    if (n > 0)
    {
        Serial2.write((const uint8_t *)cmd, n); // "AT$SF=xxxxxxxx\r\n"
        pc_sigfox_result r = read_reply(10000); // an uplink takes ~6 s
        Serial.printf("uplink #%u: %s\n", g_seq,
                      r == pc_sigfox_result::SIGFOX_OK      ? "OK"
                      : r == pc_sigfox_result::SIGFOX_ERROR ? "ERROR"
                                                            : "timeout");
        g_seq++;
    }

    delay(600000); // Sigfox caps ~140 messages/day - keep it sparse
}
