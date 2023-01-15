// 2022-01-11 cfgman.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_CFGMAN_H
#define _CAMWEBSRV_CFGMAN_H

#include <esp_err.h>

typedef void *camwebsrv_cfgman_t;

esp_err_t camwebsrv_cfgman_init(camwebsrv_cfgman_t *cfg);
esp_err_t camwebsrv_cfgman_destroy(camwebsrv_cfgman_t *cfg);
esp_err_t camwebsrv_cfgman_load(camwebsrv_cfgman_t cfg, const char *filename);
esp_err_t camwebsrv_cfgman_get(camwebsrv_cfgman_t cfg, const char *kstr, const char **vstr);

#endif

