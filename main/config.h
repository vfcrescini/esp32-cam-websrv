// 2023-01-03 config.h
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _CAMWEBSRV_CONFIG_H
#define _CAMWEBSRV_CONFIG_H

#define CAMWEBSRV_TAG "camwebsrv"
#define CAMWEBSRV_CFGMAN_FILENAME "config.cfg"
#define CAMWEBSRV_CFGMAN_KEY_WIFI_SSID "wifi_ssid"
#define CAMWEBSRV_CFGMAN_KEY_WIFI_PASS "wifi_pass"

#define CAMWEBSRV_CAMERA_STREAM_FPS 4

// ESP32Cam (AiThinker) PIN Map

#define CAMWEBSRV_PIN_PWDN 32
#define CAMWEBSRV_PIN_RESET -1
#define CAMWEBSRV_PIN_XCLK 0
#define CAMWEBSRV_PIN_SIOD 26
#define CAMWEBSRV_PIN_SIOC 27

#define CAMWEBSRV_PIN_D7 35
#define CAMWEBSRV_PIN_D6 34
#define CAMWEBSRV_PIN_D5 39
#define CAMWEBSRV_PIN_D4 36
#define CAMWEBSRV_PIN_D3 21
#define CAMWEBSRV_PIN_D2 19
#define CAMWEBSRV_PIN_D1 18
#define CAMWEBSRV_PIN_D0 5
#define CAMWEBSRV_PIN_VSYNC 25
#define CAMWEBSRV_PIN_HREF 23
#define CAMWEBSRV_PIN_PCLK 22

#define CAMWEBSRV_PIN_FLASH 4

#endif
