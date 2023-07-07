// 2023-07-04 ping.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_PING_H
#define _CAMWEBSRV_PING_H

#include "cfgman.h"

#include <esp_err.h>

typedef void *camwebsrv_ping_t;

esp_err_t camwebsrv_ping_init(camwebsrv_ping_t *ping, camwebsrv_cfgman_t cfgman);
esp_err_t camwebsrv_ping_destroy(camwebsrv_ping_t *ping);
esp_err_t camwebsrv_ping_process(camwebsrv_ping_t ping, uint16_t *nextevent);

#endif
