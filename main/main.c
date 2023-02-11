// 2023-01-03 main.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "cfgman.h"
#include "httpd.h"
#include "storage.h"
#include "wifi.h"

#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void app_main()
{
  esp_err_t rv;
  camwebsrv_cfgman_t cfgman;
  camwebsrv_httpd_t httpd;
  TickType_t last;

  // initialise storage

  rv = camwebsrv_storage_init();

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_storage_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return;
  }

  // initialise config manager

  rv = camwebsrv_cfgman_init(&cfgman);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_cfgman_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return;
  }

  // load config

  rv = camwebsrv_cfgman_load(cfgman, CAMWEBSRV_CFGMAN_FILENAME);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_cfgman_load(%s) failed: [%d]: %s", CAMWEBSRV_CFGMAN_FILENAME, rv, esp_err_to_name(rv));
    return;
  }

  // initialise wifi

  rv = camwebsrv_wifi_init(cfgman);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_wifi_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return;
  }

  // initialise web server

  rv = camwebsrv_httpd_init(&httpd);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_httpd_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return;
  }

  // start web server

  rv = camwebsrv_httpd_start(httpd);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_httpd_start() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return;
  }

  // process stream requests indefinitely

  last = xTaskGetTickCount();

  while(1)
  {
    rv = camwebsrv_httpd_process(httpd);

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_httpd_process() failed: [%d]: %s", rv, esp_err_to_name(rv));
      return;
    }

    xTaskDelayUntil(&last, pdMS_TO_TICKS(1000 / CAMWEBSRV_CAMERA_STREAM_FPS));
  }
}
