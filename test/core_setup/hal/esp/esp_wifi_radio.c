// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_wifi_radio.c
 * @brief The Espressif answer to the radio seam of network_drivers/physical/wifi_link: one 802.11 station on
 *        esp_wifi, with no esp_netif.
 *
 * No network interface of ESP-IDF exists. No lwIP stack thus sees a frame. The receive callback of the
 * station gives each frame to the link and frees the buffer of the driver. The send copies each frame
 * into the driver before it returns. The events of the station record the association in the link, and
 * a station that leaves its network joins it again.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#include "protocore_config.h" // the entry point: the vendor axis and the enable gate below

#if PROTOCORE_VENDOR_ESP && PROTOCORE_ENABLE_IDEMIP

#include "network_drivers/physical/wifi_link/wifi_link.h"

#include "esp_event.h"        // esp_event_loop_create_default, esp_event_handler_register: the station events
#include "esp_private/wifi.h" // esp_wifi_internal_reg_rxcb, esp_wifi_internal_free_rx_buffer, esp_wifi_internal_tx
#include "esp_wifi.h"         // esp_wifi_init, esp_wifi_set_config, esp_wifi_start, esp_wifi_connect
#include "nvs_flash.h"        // nvs_flash_init: the calibration data of the radio lives in NVS

// The station gives each received frame here, in the task of the driver. The link copies the frame, and
// the buffer goes back to the driver whether the link took it or dropped it.
static esp_err_t esp_wifi_radio_receive(void *buffer, uint16_t len, void *driver_buffer)
{
    const WifiLinkFrameArgs frame = {.frame = (const uint8_t *)buffer, .len = len};
    (void)protocore_wifi_link_frame_receive(&frame);
    esp_wifi_internal_free_rx_buffer(driver_buffer);
    return ESP_OK;
}

// The events of the station. The receive callback goes in at the start, because the driver takes one
// only for an interface that runs. A station that leaves its network joins it again.
static void esp_wifi_radio_event(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_arg;
    (void)base;
    (void)event_data;
    if (id == WIFI_EVENT_STA_START)
    {
        (void)esp_wifi_internal_reg_rxcb(WIFI_IF_STA, esp_wifi_radio_receive);
        (void)esp_wifi_connect();
    }
    else if (id == WIFI_EVENT_STA_CONNECTED)
    {
        const WifiLinkStateArgs state = {.up = EMBED_TRUE};
        protocore_wifi_link_state_set(&state);
    }
    else if (id == WIFI_EVENT_STA_DISCONNECTED)
    {
        const WifiLinkStateArgs state = {.up = EMBED_FALSE};
        protocore_wifi_link_state_set(&state);
        (void)esp_wifi_connect();
    }
}

// Copies the string at text into the field at field, of capacity octets. The string has no alignment
// that an MMgr copy needs. The copy thus moves one octet at a time. False where the string does not fit.
static embed_bool esp_wifi_radio_text(uint8_t *field, size_t capacity, const char *text)
{
    size_t at = 0u;
    while (text[at] != '\0')
    {
        if (at == capacity)
        {
            return EMBED_FALSE;
        }
        // Explicit cast reads the character as the octet that the field holds
        field[at] = (uint8_t)text[at];
        at++;
    }
    return EMBED_TRUE;
}

embed_bool protocore_wifi_radio_start(const WifiLinkStartArgs *args)
{
    esp_err_t status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES || status == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        (void)nvs_flash_erase();
        status = nvs_flash_init();
    }
    if (status != ESP_OK)
    {
        return EMBED_FALSE;
    }
    // The loop exists already where the core of Arduino made it.
    status = esp_event_loop_create_default();
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE)
    {
        return EMBED_FALSE;
    }
    wifi_config_t config = {0};
    if (!esp_wifi_radio_text(config.sta.ssid, sizeof config.sta.ssid, args->ssid) ||
        !esp_wifi_radio_text(config.sta.password, sizeof config.sta.password, args->password))
    {
        return EMBED_FALSE;
    }
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&init) == ESP_OK &&
           esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, esp_wifi_radio_event, NULL) == ESP_OK &&
           esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK && esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK &&
           esp_wifi_start() == ESP_OK;
}

embed_bool protocore_wifi_radio_frame_send(const WifiLinkFrameArgs *args)
{
    // Explicit casts give the driver the pointer it takes without const, and the length that the link
    // holds at IDEMIP_DMA_FRAME_MAX. The driver copies the frame and writes nothing into it.
    return esp_wifi_internal_tx(WIFI_IF_STA, (void *)(uintptr_t)args->frame, (uint16_t)args->len) == ESP_OK;
}

embed_bool protocore_wifi_radio_mac_read(uint8_t *mac)
{
    return esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK;
}

#endif // PROTOCORE_VENDOR_ESP && PROTOCORE_ENABLE_IDEMIP
