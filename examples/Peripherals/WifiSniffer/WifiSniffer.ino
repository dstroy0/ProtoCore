// WifiSniffer - channel-hopping 802.11 traffic analyzer + channel-agility roaming decision.
//
// A passive RF-diagnostics panel: sweep the 2.4 GHz channels, decode every 802.11 MAC header the
// radio hears, tally frames by type, and keep a per-channel survey of the strongest AP. That
// survey is what a channel-agility roam decides on - "is another channel enough better than mine
// to be worth moving?" (WifiSniffer.should_roam's RSSI hysteresis).
//
//   Wi-Fi radio --protocore_promisc_begin--> sink --protocore_wifi_parse--> stats tally
//                                                            \-> per-channel survey -> roam decision
//                     ^                                                                    |
//                     +----------------- protocore_wifi_sniffer_tick() hops on the dwell --------+
//
// The capture itself is owned by services/radio/promisc (one owner for the radio); services/radio/wifi_sniffer
// adds the decode, the tally, the channel-hop schedule, and the survey. Everything except the thin
// radio binding is pure and host-tested in test/test_wifi_sniffer.
//
// Strictly passive: no injection, no association. Sniffing a network you do not administer may be
// unlawful where you are - point it at your own.
//
// Build flags (whole build): PROTOCORE_ENABLE_WIFI_SNIFFER=1 PROTOCORE_ENABLE_PROMISC=1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/radio/wifi_sniffer/wifi_sniffer.h"
#include "server/clock/clock.h" // Clock.millis - the library's monotonic source

static const uint8_t CHAN_FIRST = 1; // sweep 1..11 (the US 2.4 GHz plan)
static const uint8_t CHAN_LAST = 11;
static const uint16_t DWELL_MS = 250; // per-channel dwell; a beacon interval is ~102 ms
static const uint8_t ROAM_HYSTERESIS_DB = 8;

// The library's monotonic milliseconds: Clock.millis() leaves its reading in Clock.ms.
static uint32_t protocore_millis(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    // Radio up for capture only - promiscuous mode does not associate.
    PhysicalV.wifi.channel = 0;
    Physical.wifi_radio_init(protocore_physical_span());

    WifiSnifferV.begin_args.first_chan = CHAN_FIRST;
    WifiSnifferV.begin_args.last_chan = CHAN_LAST;
    WifiSnifferV.begin_args.dwell_ms = DWELL_MS;
    WifiSniffer.begin(protocore_wifi_sniffer_span());
    if (!WifiSnifferV.ok)
    {
        Serial.println("sniffer: failed to start promiscuous capture");
        return;
    }
    Serial.printf("Sniffing channels %u-%u, %u ms dwell\n", CHAN_FIRST, CHAN_LAST, DWELL_MS);
}

void loop()
{
    WifiSniffer.tick(protocore_wifi_sniffer_span()); // hops to the next channel when the dwell elapses

    static uint32_t last_report = 0;
    if (protocore_millis() - last_report < 5000)
    {
        return;
    }
    last_report = protocore_millis();

    uint8_t *work = protocore_wifi_sniffer_span();
    WifiSniffer.stats(work);
    WifiSniffer.survey(work);
    WifiSniffer.scan(work);
    const WifiStats *st = WifiSnifferV.stats_out;
    const WifiSurvey *sv = WifiSnifferV.survey_out;
    const WifiScan *sc = WifiSnifferV.scan_out;

    Serial.printf("\n-- ch %u, sweep %lu -- frames %lu (mgmt %lu, ctrl %lu, data %lu, other %lu)\n", sc->channel,
                  (unsigned long)sc->sweeps, (unsigned long)st->total, (unsigned long)st->mgmt, (unsigned long)st->ctrl,
                  (unsigned long)st->data, (unsigned long)st->other);

    for (uint8_t ch = CHAN_FIRST; ch <= CHAN_LAST; ch++)
    {
        WifiSnifferV.survey_get_args.s = sv;
        WifiSnifferV.survey_get_args.channel = ch;
        WifiSniffer.survey_get(work);
        const WifiChannelSurvey *e = WifiSnifferV.ptr;
        if (!e || e->frames == 0)
        {
            continue;
        }
        Serial.printf("  ch %2u: %6lu frames, best %d dBm from %02X:%02X:%02X:%02X:%02X:%02X\n", ch,
                      (unsigned long)e->frames, (int)e->best_rssi, e->best_bssid[0], e->best_bssid[1], e->best_bssid[2],
                      e->best_bssid[3], e->best_bssid[4], e->best_bssid[5]);
    }

    // Channel-agility: is any other channel enough stronger than the one we are on?
    WifiSnifferV.survey_get_args.s = sv;
    WifiSnifferV.survey_get_args.channel = sc->channel;
    WifiSniffer.survey_get(work);
    const WifiChannelSurvey *cur = WifiSnifferV.ptr;
    uint8_t cand_ch = 0;
    int8_t cand_rssi = 0;
    bool have_cand = false;
    if (cur && cur->best_rssi != PROTOCORE_WIFI_RSSI_NONE)
    {
        WifiSnifferV.survey_best_args.s = sv;
        WifiSnifferV.survey_best_args.exclude_channel = sc->channel;
        WifiSnifferV.survey_best_args.out_channel = &cand_ch;
        WifiSnifferV.survey_best_args.out_rssi = &cand_rssi;
        WifiSniffer.survey_best(work);
        have_cand = WifiSnifferV.ok;
    }
    if (have_cand)
    {
        WifiSnifferV.should_roam_args.cur_rssi = cur->best_rssi;
        WifiSnifferV.should_roam_args.cand_rssi = cand_rssi;
        WifiSnifferV.should_roam_args.hysteresis_db = ROAM_HYSTERESIS_DB;
        WifiSniffer.should_roam(work);
        bool roam = WifiSnifferV.ok;
        Serial.printf("  roam? ch %u (%d dBm) -> ch %u (%d dBm): %s\n", sc->channel, (int)cur->best_rssi, cand_ch,
                      (int)cand_rssi, roam ? "YES" : "no");
    }
}
