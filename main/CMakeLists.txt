# 2023-01-03 CMakeLists.txt
# Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
  SRCS "main.c" "camera.c" "cfgman.c" "httpd.c" "storage.c" "vbytes.c" "wifi.c"
  PRIV_REQUIRES "esp_event" "esp_http_server" "esp_timer" "esp_wifi" "fatfs" "freertos" "nvs_flash" "vfs"
  PRIV_INCLUDE_DIRS "."
)

fatfs_create_rawflash_image("storage" "../storage" FLASH_IN_PROJECT PRESERVE_TIME)
