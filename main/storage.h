// 2023-01-08 storage.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_STORAGE_H
#define _CAMWEBSRV_STORAGE_H

#include <stddef.h>
#include <stdbool.h>

#include <esp_err.h>

typedef bool (*camwebsrv_storage_cb_t)(const char *, size_t, void *);

esp_err_t camwebsrv_storage_init();
esp_err_t camwebsrv_storage_get(const char *filename, camwebsrv_storage_cb_t cb, void *arg);

#endif
