// 2023-01-03 camera.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_CAMERA_H
#define _CAMWEBSRV_CAMERA_H

#include <stddef.h>
#include <stdbool.h>

#include <esp_err.h>

typedef bool (*camwebsrv_camera_cb_t)(const char *, size_t, void *);

esp_err_t camwebsrv_camera_init();
esp_err_t camwebsrv_camera_frame(camwebsrv_camera_cb_t onframe, void *arg);
esp_err_t camwebsrv_camera_set(const char *name, int value);
int camwebsrv_camera_get(const char *name);
bool camwebsrv_camera_is_ov3660();

#endif
