// 2023-01-03 wifi.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "wifi.h"

#include <stdint.h>
#include <string.h>

#include <esp_log.h>
#include <esp_event.h>
#include <esp_event_base.h>
#include <esp_wifi.h>
#include <esp_netif_ip_addr.h>

#include <freertos/event_groups.h>

#define _CAMWEBSRV_WIFI_ATTEMPT_LIMIT 5
#define _CAMWEBSRV_WIFI_CONNECTED_BIT 0x00000001
#define _CAMWEBSRV_WIFI_FAILED_BIT    0x00000002

static void _camwebsrv_wifi_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static unsigned int _camwebsrv_wifi_attempts = 0;

esp_err_t camwebsrv_wifi_init(camwebsrv_cfgman_t cfgman)
{
  esp_err_t rv;
  EventBits_t bits;
  EventGroupHandle_t eg;
  esp_event_handler_instance_t handler_1;
  esp_event_handler_instance_t handler_2;
  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  wifi_config_t wifi_config =
  {
    .sta =
    {
      .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
      .sae_pwe_h2e = WPA3_SAE_PWE_BOTH
    }
  };
  const char *ssid = NULL;
  const char *pass = NULL;

  if (cfgman == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  // get ssid

  rv = camwebsrv_cfgman_get(cfgman, CAMWEBSRV_CFGMAN_KEY_WIFI_SSID, &ssid);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): camwebsrv_cfgman_get(%s) failed: [%d]: %s", CAMWEBSRV_CFGMAN_KEY_WIFI_SSID, rv, esp_err_to_name(rv));
    return rv;
  }

  strncpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));

  // get password

  rv = camwebsrv_cfgman_get(cfgman, CAMWEBSRV_CFGMAN_KEY_WIFI_PASS, &pass);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): camwebsrv_cfgman_get(%s) failed: [%d]: %s", CAMWEBSRV_CFGMAN_KEY_WIFI_SSID, rv, esp_err_to_name(rv));
    return rv;
  }

  strncpy((char *) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

  // init wifi
 
  eg = xEventGroupCreate();

  rv = esp_netif_init();

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_netif_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  esp_netif_create_default_wifi_sta();

  rv = esp_wifi_init(&init_config);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  rv = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, _camwebsrv_wifi_handler, (void *) &eg, &handler_1);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_event_handler_instance_register(ESP_EVENT_ANY_ID) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  rv = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, _camwebsrv_wifi_handler, (void *) &eg, &handler_2);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_event_handler_instance_register(ESP_EVENT_GOT_ID) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  rv = esp_wifi_set_mode(WIFI_MODE_STA);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_set_mode(WIFI_MODE_STA) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  rv = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_set_config(WIFI_MODE_STA) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  rv = esp_wifi_start();

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): esp_wifi_start() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): initialisation completed");

  bits = xEventGroupWaitBits(eg, _CAMWEBSRV_WIFI_CONNECTED_BIT | _CAMWEBSRV_WIFI_FAILED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & _CAMWEBSRV_WIFI_CONNECTED_BIT)
  {
    ESP_LOGI(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): connection to %s successful", wifi_config.sta.ssid);
  }
  else if (bits & _CAMWEBSRV_WIFI_FAILED_BIT)
  {
    ESP_LOGI(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): connection to %s failed", wifi_config.sta.ssid);
  }
  else
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI camwebsrv_wifi_init(): setup failed");
  }

  return ESP_OK;
}

static void _camwebsrv_wifi_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  EventGroupHandle_t *eg = (EventGroupHandle_t *) arg;
  ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();	  
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    if (_camwebsrv_wifi_attempts < _CAMWEBSRV_WIFI_ATTEMPT_LIMIT)
    {
      esp_wifi_connect();

      _camwebsrv_wifi_attempts++;

      ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): connection attempt %d", _camwebsrv_wifi_attempts);
    }
    else
    {
      xEventGroupSetBits(*eg, _CAMWEBSRV_WIFI_FAILED_BIT);
    }

    ESP_LOGI(CAMWEBSRV_TAG,"WIFI _camwebsrv_wifi_handler(): connection attempt failed");
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): connection attempt successful");
    ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): IP: %d.%d.%d.%d", IP2STR(&event->ip_info.ip));
    ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): Netmask: %d.%d.%d.%d", IP2STR(&event->ip_info.netmask));
    ESP_LOGI(CAMWEBSRV_TAG, "WIFI _camwebsrv_wifi_handler(): Gateway: %d.%d.%d.%d", IP2STR(&event->ip_info.gw));

    _camwebsrv_wifi_attempts = 0;

    xEventGroupSetBits(*eg, _CAMWEBSRV_WIFI_CONNECTED_BIT);
  }
}
