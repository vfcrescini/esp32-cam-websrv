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
#include <esp_event.h>

#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

void app_main()
{
  esp_err_t rv;
  SemaphoreHandle_t sema;
  camwebsrv_cfgman_t cfgman = NULL;
  camwebsrv_wifi_t wifi = NULL;
  camwebsrv_httpd_t httpd = NULL;

  // initialise NVS

  rv = nvs_flash_init();

  if (rv == ESP_ERR_NVS_NO_FREE_PAGES || rv == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    rv = nvs_flash_erase();

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): nvs_flash_erase() failed: [%d]: %s", rv, esp_err_to_name(rv));
      goto camwebsrv_main_error;
    }

    rv = nvs_flash_init();
  }

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): nvs_flash_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // create default event loop

  rv = esp_event_loop_create_default();
  
  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "WIFI app_main(): esp_event_loop_create_default() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // initialise storage

  rv = camwebsrv_storage_init();

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_storage_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // initialise config manager

  rv = camwebsrv_cfgman_init(&cfgman);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_cfgman_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // load config

  rv = camwebsrv_cfgman_load(cfgman, CAMWEBSRV_CFGMAN_FILENAME);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_cfgman_load(%s) failed: [%d]: %s", CAMWEBSRV_CFGMAN_FILENAME, rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // initialise wifi

  rv = camwebsrv_wifi_init(&wifi, cfgman);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_wifi_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // initialise sema

  sema = xSemaphoreCreateBinary();

  if (sema == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): xSemaphoreCreateBinary() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // initialise web server

  rv = camwebsrv_httpd_init(&httpd, sema);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_httpd_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // start web server

  rv = camwebsrv_httpd_start(httpd);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_httpd_start() failed: [%d]: %s", rv, esp_err_to_name(rv));
    goto camwebsrv_main_error;
  }

  // process stream requests indefinitely

  while(1)
  {
    uint16_t nextevent = UINT16_MAX;

    rv = camwebsrv_httpd_process(httpd, &nextevent);

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): camwebsrv_httpd_process() failed: [%d]: %s", rv, esp_err_to_name(rv));
      goto camwebsrv_main_error;
    }

    // block until there is actually something to do

    xSemaphoreTake(sema, (nextevent == UINT16_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(nextevent));
  }

  camwebsrv_main_error:

  ESP_LOGE(CAMWEBSRV_TAG, "MAIN app_main(): Rebooting in %d seconds", CAMWEBSRV_MAIN_REBOOT_DELAY_MSEC / 1000);
  vTaskDelay(pdMS_TO_TICKS(CAMWEBSRV_MAIN_REBOOT_DELAY_MSEC));
  esp_restart();
  return;
}
