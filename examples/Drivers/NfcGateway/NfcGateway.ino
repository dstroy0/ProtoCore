// NfcGateway - a PN532 NFC reader bridged to the gateway (tag read -> web event).
//
// A PN532 NFC/RFID reader over I2C: scanning a tag becomes a northbound event. The only
// hardware-specific code is the I2C carry of the frame bytes; the PN532 command-frame
// protocol (build a command, verify the ACK, parse the response) is services/pn532.
//
//   tag scan --PN532/I2C--> pc_pn532_parse_frame() -> UID -> pc_gateway_uplink()
//                                                          |
//                                       envelope + topic  nfc/0/<target>
//                                                          |
//                                              northbound publish (MQTT/HTTP/WS)
//
// Needs a PN532 on I2C to actually read; the frame codec is host-tested in test/test_pn532.
//
// Build flags (whole build): PC_ENABLE_PN532=1 PC_ENABLE_GATEWAY=1

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "services/net/gateway/gateway.h"
#include "services/peripherals/pn532/pn532.h"
#include <Wire.h>

static const uint8_t PN532_I2C_ADDR = 0x24;
static const uint8_t RADIO_PORT = 0;

// I2C carry: write a frame, and read one (the PN532 prefixes a ready-status byte on reads).
static bool pc_pn532_write(const uint8_t *frame, uint8_t n)
{
    Wire.beginTransmission(PN532_I2C_ADDR);
    Wire.write(frame, n);
    return Wire.endTransmission() == 0;
}
static int pc_pn532_read(uint8_t *buf, uint8_t cap)
{
    Wire.requestFrom(PN532_I2C_ADDR, (uint8_t)(cap + 1));
    if (!Wire.available())
    {
        return -1;
    }
    if (!(Wire.read() & 0x01)) // status byte: bit0 = ready
    {
        return -1;
    }
    int i = 0;
    while (Wire.available() && i < cap)
    {
        buf[i++] = (uint8_t)Wire.read();
    }
    return i;
}

// Send a command and read past the ACK to the response frame; return its parsed PData.
static int pc_pn532_command(const uint8_t *cmd, uint8_t cmd_len, uint8_t *resp, uint8_t pc_resp_cap,
                            const uint8_t **pdata, uint8_t *pdata_len)
{
    uint8_t frame[16 + 8];
    uint16_t n = pc_pn532_build_frame(PN532_TFI_HOST, cmd, cmd_len, frame, sizeof(frame));
    if (n == 0 || !pc_pn532_write(frame, (uint8_t)n))
    {
        return -1;
    }
    delay(2);
    uint8_t ack[8];
    if (pc_pn532_read(ack, 6) < 6 || !pc_pn532_is_ack(ack, 6))
    {
        return -1;
    }
    delay(5);
    int r = pc_pn532_read(resp, pc_resp_cap);
    if (r <= 0)
    {
        return -1;
    }
    uint8_t tfi = 0;
    return pc_pn532_parse_frame(resp, (uint16_t)r, &tfi, pdata, pdata_len) > 0 ? 0 : -1;
}

static bool northbound_publish(const pc_gateway_msg *m, void *)
{
    char topic[48];
    pc_gateway_topic(m, topic, sizeof(topic));
    Serial.printf("PUBLISH %s  (UID %u bytes)\n", topic, m->len);
    return true;
}

void setup()
{
    Serial.begin(115200);
    Wire.begin();
    delay(300);

    pc_gateway_reset();
    pc_gateway_port_config p = {};
    p.port_id = RADIO_PORT;
    p.kind = pc_gateway_kind::PC_GW_NFC;
    pc_gateway_add_port(&p);
    pc_gateway_set_uplink_cb(northbound_publish, nullptr);
    pc_gateway_set_topic_prefix("nfc");

    const uint8_t getver[1] = {0x02}; // GetFirmwareVersion
    uint8_t resp[32];
    const uint8_t *pd = nullptr;
    uint8_t pdlen = 0;
    if (pc_pn532_command(getver, 1, resp, sizeof(resp), &pd, &pdlen) == 0 && pdlen >= 2)
    {
        Serial.printf("PN532 firmware %u.%u ready\n", pd[1], pd[2]);
    }
    else
    {
        Serial.println("no PN532 on I2C - check wiring");
    }

    Serial.println("NFC gateway: scan a tag -> publish (nfc/0/<target>)");
}

void loop()
{
    // InListPassiveTarget: 1 target, 106 kbps type A.
    const uint8_t detect[3] = {0x4A, 0x01, 0x00};
    uint8_t resp[48];
    const uint8_t *pd = nullptr;
    uint8_t pdlen = 0;
    if (pc_pn532_command(detect, 3, resp, sizeof(resp), &pd, &pdlen) == 0 && pdlen >= 7 && pd[1] >= 1)
    {
        // Response PData: 4B | NbTg | Tg | SENS_RES(2) | SEL_RES | IDLen | NFCID...
        uint8_t id_len = pd[6];
        if (7u + id_len <= pdlen)
        {
            pc_gateway_uplink(RADIO_PORT, pd[2], &pd[7], id_len, 0); // pd[2] = target number
        }
    }
    delay(500);
}
