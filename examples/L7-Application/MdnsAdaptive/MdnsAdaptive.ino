// MdnsAdaptive - keep the device discoverable on a crowded 2.4 GHz channel without adding to the noise.
//
// An mDNS record lapses from network caches at its TTL, so a device has to re-announce to stay
// discoverable. But hammering announces on a busy channel just adds collisions. This ties three
// shipped pieces together so the announce cadence tracks the air:
//
//   promiscuous capture (services/radio/promisc)  -> a live frame count = RF contention
//   beacon scheduler (network_drivers/application/mdns_adaptive) -> backs the interval off when busy, recovers when quiet
//   mDNS TXT re-apply (network_drivers/application/mdns_service) -> re-announces with no goodbye (a refresh, not an evict)
//
// The contention signal comes from promiscuous mode - a radio-layer callback pinned to the station's
// OWN channel - not from a second socket on UDP 5353. That distinction matters: a second 5353 bind
// turns the ESP-IDF responder announce-only (it appears in a browse but stops resolving). Promiscuous
// capture does not touch the responder's sockets, so the record keeps resolving while the count runs.
//
// GET /mdns  -> {"interval_ms":N,"contention":N,"announces":N,"channel":N}
//
// Watch it from a Linux box on the same LAN:
//   avahi-resolve -n adaptive.local            # the A record
//   avahi-browse -rt _http._tcp                # SRV + TXT still resolve while capture runs
//
// This owns promiscuous mode, so it cannot run alongside the wifi_sniffer live channel-hop binding.
//
// Build flags (whole build): PC_ENABLE_MDNS=1 PC_ENABLE_PROMISC=1 PC_ENABLE_WIFI_SNIFFER=1
//                            PC_ENABLE_MDNS_ADAPTIVE=1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/application/mdns_adaptive/mdns_adaptive.h"
#include "network_drivers/application/mdns_service/mdns_service.h"
#include "shared_primitives/mime.h"

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";


static void mdns_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char json[128];
    snprintf(json, sizeof(json), "{\"interval_ms\":%lu,\"contention\":%u,\"announces\":%lu,\"channel\":%u}",
             (unsigned long)pc_mdns_adaptive_interval_ms(), (unsigned)pc_mdns_adaptive_contention(),
             (unsigned long)pc_mdns_adaptive_announces(), (unsigned)Physical.wifi->channel());
    send_text(slot_id, 200, PC_MIME_JSON, json);
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    Physical.wifi->init(WIFI_SSID, WIFI_PASS);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }

    on_http("/mdns", HTTP_GET, mdns_handler);
    begin_http(80, NULL);

    // Bring up the responder and seed the TXT record the refresher re-applies.
    if (pc_mdns_begin("adaptive", 80))
    {
        pc_mdns_txt("role", "sensor");
        Serial.println("mDNS up: adaptive.local");
    }

    // A short TTL (30 s) makes the refresher cadence easy to watch; production would use minutes.
    // The adaptive range is fundamentally [TTL/2, ~TTL): you cannot back off past the TTL without the
    // record lapsing from caches, so a short TTL is a narrow range. A longer TTL buys more room.
    MdnsAdaptiveCfg cfg;
    cfg.key = "role"; // an existing TXT key, re-applied unchanged to re-announce
    cfg.value = "sensor";
    cfg.ttl_s = 30;              // base cadence = TTL/2 = 15 s
    cfg.max_interval_ms = 25000; // back off toward 25 s (< the 30 s TTL, so the record never lapses)
    cfg.hi_contention = 40;      // >= 40 frames/window (1 s) counts as "busy"
    cfg.window_ms = 1000;

    if (pc_mdns_adaptive_begin(&cfg))
    {
        Serial.printf("adaptive announcing on channel %u\n", (unsigned)Physical.wifi->channel());
    }
    else
    {
        Serial.println("adaptive begin FAILED (not associated, or promiscuous unavailable)");
    }

    uint32_t ip = Physical.link->egress_ip();
    Serial.printf("http://%u.%u.%u.%u/mdns  - resolve adaptive.local from the LAN\n", (unsigned)(ip & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    handle();
    pc_mdns_adaptive_tick(); // samples, adapts, re-announces when due - rate-limited internally

    static uint32_t next = 0;
    if (millis() >= next)
    {
        next = millis() + 5000;
        Serial.printf("interval=%lums contention=%u announces=%lu ch=%u\n",
                      (unsigned long)pc_mdns_adaptive_interval_ms(), (unsigned)pc_mdns_adaptive_contention(),
                      (unsigned long)pc_mdns_adaptive_announces(), (unsigned)Physical.wifi->channel());
    }
}
