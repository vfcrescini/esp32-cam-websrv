// 2023-02-06 sclients.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_SCLIENTS_H
#define _CAMWEBSRV_SCLIENTS_H

#include "camera.h"

#include <stdint.h>

#include <esp_err.h>
#include <esp_http_server.h>

typedef void *camwebsrv_sclients_t;

esp_err_t camwebsrv_sclients_init(camwebsrv_sclients_t *clients);
esp_err_t camwebsrv_sclients_destroy(camwebsrv_sclients_t *clients, httpd_handle_t handle);
esp_err_t camwebsrv_sclients_add(camwebsrv_sclients_t clients, int sockfd);
esp_err_t camwebsrv_sclients_purge(camwebsrv_sclients_t clients, httpd_handle_t handle);
esp_err_t camwebsrv_sclients_process(camwebsrv_sclients_t clients, camwebsrv_camera_t cam, httpd_handle_t handle, uint16_t *nextevent);

#endif
