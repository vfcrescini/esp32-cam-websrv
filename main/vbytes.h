// 2023-01-26 vbytes.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_VBYTES_H
#define _CAMWEBSRV_VBYTES_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>

typedef void *camwebsrv_vbytes_t;

esp_err_t camwebsrv_vbytes_init(camwebsrv_vbytes_t *vb);
esp_err_t camwebsrv_vbytes_destroy(camwebsrv_vbytes_t *vb);
esp_err_t camwebsrv_vbytes_get_bytes(camwebsrv_vbytes_t vb, const uint8_t **bytes, size_t *len);
esp_err_t camwebsrv_vbytes_set_bytes(camwebsrv_vbytes_t vb, const uint8_t *bytes, size_t len);
esp_err_t camwebsrv_vbytes_set_str(camwebsrv_vbytes_t vb, const char *fmt, ...);
esp_err_t camwebsrv_vbytes_append_bytes(camwebsrv_vbytes_t vb, const uint8_t *bytes, size_t len);
esp_err_t camwebsrv_vbytes_append_str(camwebsrv_vbytes_t vb, const char *fmt, ...);
size_t camwebsrv_vbytes_length(camwebsrv_vbytes_t vb);

#endif

