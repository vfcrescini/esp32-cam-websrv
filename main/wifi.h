// 2023-01-03 wifi.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_WIFI_H
#define _CAMWEBSRV_WIFI_H

#include "cfgman.h"

#include <esp_err.h>

typedef void *camwebsrv_wifi_t;

esp_err_t camwebsrv_wifi_init(camwebsrv_wifi_t *wifi, camwebsrv_cfgman_t cfgman);
esp_err_t camwebsrv_wifi_destroy(camwebsrv_wifi_t *wifi);

#endif
