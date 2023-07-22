// 2023-01-03 wifi.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "wifi.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <esp_log.h>
#include <esp_event.h>
#include <esp_event_base.h>
#include <esp_wifi.h>
#include <esp_netif_ip_addr.h>

#include <freertos/event_groups.h>

#define _CAMWEBSRV_WIFI_TIMEOUT_START   30000
#define _CAMWEBSRV_WIFI_TIMEOUT_CONNECT 30000

#define _CAMWEBSRV_WIFI_STATE_ALL       0xFF
#define _CAMWEBSRV_WIFI_STATE_STARTED   0x01
#define _CAMWEBSRV_WIFI_STATE_CONNECTED 0x02
#define _CAMWEBSRV_WIFI_STATE_RUNNING   0x04

typedef struct
{
  wifi_config_t config;
  EventGroupHandle_t events;
  esp_event_handler_instance_t handler;
} _camwebsrv_wifi_t;

static void _camwebsrv_wifi_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

esp_err_t camwebsrv_wifi_init(camwebsrv_wifi_t *wifi, camwebsrv_cfgman_t cfgman)
{
  esp_err_t rv;
  _camwebsrv_wifi_t *pwifi;
  EventBits_t bits;
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  const char *tmp = NULL;

  if (wifi == NULL || cfgman == NULL)
  {
    rv = ESP_ERR_INVALID_ARG;
    goto _camwebsrv_wifi_start_return0;
  }

  // new structure

  pwifi = (_camwebsrv_wifi_t *) malloc(sizeof(_camwebsrv_wifi_t));

  if (pwifi == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): malloc() failed: [%d]: %s", e, strerror(e));
    rv = ESP_FAIL;  
    goto _camwebsrv_wifi_start_return0;
  }

  memset(pwifi, 0x00, sizeof(_camwebsrv_wifi_t));

  // it's not really clear from the official docs whether this will still be
  // needed after a call to esp_wifi_set_config(), but in examples from the
  // official github repo, the scope of the config structure extends up to at
  // least after connection is established (i.e. after IP_EVENT_STA_GOT_IP is
  // triggered). So we keep it in our main structure, just in case.

  pwifi->config = (wifi_config_t) { .sta = { .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK, .sae_pwe_h2e = WPA3_SAE_PWE_BOTH } };

  // get ssid

  rv = camwebsrv_cfgman_get(cfgman, CAMWEBSRV_CFGMAN_KEY_WIFI_SSID, &tmp);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): camwebsrv_cfgman_get(%s) failed: [%d]: %s", CAMWEBSRV_CFGMAN_KEY_WIFI_SSID, rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return1;
  }

  strncpy((char *) pwifi->config.sta.ssid, tmp, sizeof(pwifi->config.sta.ssid) - 1);

  // get password

  rv = camwebsrv_cfgman_get(cfgman, CAMWEBSRV_CFGMAN_KEY_WIFI_PASS, &tmp);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): camwebsrv_cfgman_get(%s) failed: [%d]: %s", CAMWEBSRV_CFGMAN_KEY_WIFI_SSID, rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return1;
  }

  strncpy((char *) pwifi->config.sta.password, tmp, sizeof(pwifi->config.sta.password) - 1);

  // init netif
 
  rv = esp_netif_init();

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_netif_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return1;
  }

  // init wifi
  // official docs say that the pointer passed to esp_wifi_init() can point to
  // a temporary variable

  rv = esp_wifi_init(&config);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return2;
  }

  // set as station

  if (esp_netif_create_default_wifi_sta() == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_netif_create_default_wifi_sta() failed");
    goto _camwebsrv_wifi_start_return3;
  }

  rv = esp_wifi_set_mode(WIFI_MODE_STA);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_set_mode(WIFI_MODE_STA) failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return3;
  }

  // set config

  rv = esp_wifi_set_config(WIFI_IF_STA, &(pwifi->config));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_set_config(WIFI_MODE_STA) failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return3;
  }

  // init event group

  pwifi->events = xEventGroupCreate();
  xEventGroupClearBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_ALL);

  // set event handler

  rv = esp_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, _camwebsrv_wifi_handler, (void *) pwifi, &(pwifi->handler));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID) failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return4;
  }

  // start

  ESP_LOGD(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): starting");

  rv = esp_wifi_start();

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_start() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return5;
  }

  // wait until started

  ESP_LOGD(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): waiting for _CAMWEBSRV_WIFI_STATE_STARTED");

  bits = xEventGroupWaitBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_STARTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(_CAMWEBSRV_WIFI_TIMEOUT_START));

  if ((bits & _CAMWEBSRV_WIFI_STATE_STARTED) == 0x00)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): Timed out while waiting for _CAMWEBSRV_WIFI_STATE_STARTED");
    rv = ESP_ERR_TIMEOUT;
    goto _camwebsrv_wifi_start_return6;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): started");

  // connect

  ESP_LOGD(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): connecting");

  rv = esp_wifi_connect();

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_connect() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto _camwebsrv_wifi_start_return6;
  }

  ESP_LOGD(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): waiting for _CAMWEBSRV_WIFI_STATE_CONNECTED");

  // wait until connected

  bits = xEventGroupWaitBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(_CAMWEBSRV_WIFI_TIMEOUT_CONNECT));

  if ((bits & _CAMWEBSRV_WIFI_STATE_CONNECTED) == 0x00)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): Timed out while waiting for _CAMWEBSRV_WIFI_STATE_CONNECTED");
    rv = ESP_ERR_TIMEOUT;
    goto _camwebsrv_wifi_start_return7;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): connected");

  xEventGroupSetBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_RUNNING);

  // give back reference

  *wifi = (camwebsrv_wifi_t) pwifi;

  rv = ESP_OK;

  goto _camwebsrv_wifi_start_return0;

  // some error occurred; do cleanup

  _camwebsrv_wifi_start_return7:
    esp_wifi_disconnect();
  _camwebsrv_wifi_start_return6:
    esp_wifi_stop();
  _camwebsrv_wifi_start_return5:
    esp_event_handler_instance_unregister(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, pwifi->handler);
  _camwebsrv_wifi_start_return4:
    vEventGroupDelete(pwifi->events);
  _camwebsrv_wifi_start_return3:
    esp_wifi_deinit();
  _camwebsrv_wifi_start_return2:
    esp_netif_deinit();
  _camwebsrv_wifi_start_return1:
    free(pwifi);
  _camwebsrv_wifi_start_return0:
    return rv;
}

esp_err_t camwebsrv_wifi_destroy(camwebsrv_wifi_t *wifi)
{
  _camwebsrv_wifi_t *pwifi;

  if (wifi == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pwifi = (_camwebsrv_wifi_t *) *wifi;

  xEventGroupClearBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_ALL);

  esp_event_handler_instance_unregister(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, pwifi->handler);

  esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_netif_deinit();

  vEventGroupDelete(pwifi->events);

  free(pwifi);

  *wifi = NULL;

  return ESP_OK;
}

static void _camwebsrv_wifi_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  _camwebsrv_wifi_t *pwifi = (_camwebsrv_wifi_t *) arg;

  if (event_base == WIFI_EVENT)
  {
    wifi_event_sta_connected_t *event;
    EventBits_t bits;
    switch(event_id)
    {
      case WIFI_EVENT_STA_START:
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): WIFI_EVENT_STA_START");
        xEventGroupSetBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_STARTED);
        break;
      case WIFI_EVENT_STA_STOP:
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): WIFI_EVENT_STA_STOP");
        xEventGroupClearBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_STARTED | _CAMWEBSRV_WIFI_STATE_CONNECTED);
        break;
      case WIFI_EVENT_STA_CONNECTED:
        event = (wifi_event_sta_connected_t *) event_data;
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): WIFI_EVENT_STA_CONNECTED");
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): SSID: %s", event->ssid);
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): Channel: %u", event->channel);
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): WIFI_EVENT_STA_DISCONNECTED");
        bits = xEventGroupClearBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_CONNECTED);

        // attempt to reconnect only if we are in running state

        if ((bits & _CAMWEBSRV_WIFI_STATE_RUNNING) != 0x00)
        {
          esp_wifi_connect();
        }
        break;
      default:
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): WIFI_EVENT: %ld", event_id);
        break;
    }
  }
  else if (event_base == IP_EVENT)
  {
    ip_event_got_ip_t *event;
    switch(event_id)
    {
      case IP_EVENT_STA_GOT_IP:
        event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): IP_EVENT_STA_GOT_IP");
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        xEventGroupSetBits(pwifi->events, _CAMWEBSRV_WIFI_STATE_CONNECTED);
        break;
      default:
        ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): IP_EVENT: %ld", event_id);
        break;
    }
  }
}
