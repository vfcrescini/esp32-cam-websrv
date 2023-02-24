// 2023-01-03 camera.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_CAMERA_H
#define _CAMWEBSRV_CAMERA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <esp_err.h>

typedef void *camwebsrv_camera_t;

esp_err_t camwebsrv_camera_init(camwebsrv_camera_t *cam);
esp_err_t camwebsrv_camera_destroy(camwebsrv_camera_t *cam);
esp_err_t camwebsrv_camera_reset(camwebsrv_camera_t cam);
esp_err_t camwebsrv_camera_frame_grab(camwebsrv_camera_t cam, uint8_t **fbuf, size_t *flen, int64_t *tstamp);
esp_err_t camwebsrv_camera_frame_dispose(camwebsrv_camera_t cam);
esp_err_t camwebsrv_camera_ctrl_set(camwebsrv_camera_t cam, const char *name, int value);
int camwebsrv_camera_ctrl_get(camwebsrv_camera_t cam, const char *name);
bool camwebsrv_camera_is_ov3660(camwebsrv_camera_t cam);

#endif
