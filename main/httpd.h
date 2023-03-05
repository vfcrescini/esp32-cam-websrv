// 2023-01-03 httpd.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_HTTPD_H
#define _CAMWEBSRV_HTTPD_H

#include <stdint.h>

#include <esp_err.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

typedef void *camwebsrv_httpd_t;

esp_err_t camwebsrv_httpd_init(camwebsrv_httpd_t *httpd, SemaphoreHandle_t sema);
esp_err_t camwebsrv_httpd_destroy(camwebsrv_httpd_t *httpd);
esp_err_t camwebsrv_httpd_start(camwebsrv_httpd_t httpd);
esp_err_t camwebsrv_httpd_process(camwebsrv_httpd_t httpd, uint16_t *nextevent);

#endif
