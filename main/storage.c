// 2023-01-08 storage.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <esp_vfs.h>
#include <esp_vfs_fat.h>

#define _CAMWEBSRV_STORAGE_PARTITION_LABEL "storage"
#define _CAMWEBSRV_STORAGE_MOUNT_PATH "/storage"
#define _CAMWEBSRV_STORAGE_BLOCK_LEN 128
#define _CAMWEBSRV_STORAGE_PATH_LEN 32

esp_err_t camwebsrv_storage_init()
{
  esp_err_t rv;
  esp_vfs_fat_mount_config_t config;

  memset(&config, 0x00, sizeof(config));

  config.format_if_mount_failed = false;
  config.max_files = 2;
  config.allocation_unit_size = 0;
  config.disk_status_check_enable = false;

  // mount

  rv = esp_vfs_fat_spiflash_mount_ro(_CAMWEBSRV_STORAGE_MOUNT_PATH, _CAMWEBSRV_STORAGE_PARTITION_LABEL, &config);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "STORAGE camwebsrv_storage_init(): esp_vfs_fat_spiflash_mount_ro(%s, %s) failed: [%d]: %s", _CAMWEBSRV_STORAGE_MOUNT_PATH, _CAMWEBSRV_STORAGE_PARTITION_LABEL, rv, esp_err_to_name(rv));
    return rv;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "STORAGE camwebsrv_storage_init(): partition %s mounted on %s", _CAMWEBSRV_STORAGE_PARTITION_LABEL, _CAMWEBSRV_STORAGE_MOUNT_PATH);

  return ESP_OK;
}

esp_err_t camwebsrv_storage_get(const char *filename, camwebsrv_storage_cb_t cb, void *arg)
{
  int fd;
  uint8_t *tbuf = NULL;
  size_t tlen = 0;
  char path[_CAMWEBSRV_STORAGE_PATH_LEN];

  if (filename == NULL || cb == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  // compose path

  snprintf(path, _CAMWEBSRV_STORAGE_PATH_LEN, "%s/%s", _CAMWEBSRV_STORAGE_MOUNT_PATH, filename);

  // open

  fd = open(path, O_RDONLY);

  if (fd == -1)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "STORAGE camwebsrv_storage_get(): open(%s) failed: [%d]: %s", path, e, strerror(e));
    return ESP_FAIL;
  }

  // read into buffer

  while(1)
  {
    uint8_t block[_CAMWEBSRV_STORAGE_BLOCK_LEN];
    uint8_t *tptr = NULL;
    ssize_t n;

    n = read(fd, block, _CAMWEBSRV_STORAGE_BLOCK_LEN);

    // error?

    if (n < 0)
    {
      int e = errno;
      ESP_LOGE(CAMWEBSRV_TAG, "STORAGE camwebsrv_storage_get(): read(%s) failed: [%d]: %s", path, e, strerror(e));
      close(fd);
      return ESP_FAIL;
    }

    // EOF?

    if (n == 0)
    {
      break;
    }

    // we actually read in something

    // allocate space for the read-in block plus an extra null byte

    tptr = (uint8_t *) realloc(tbuf, tlen + n + 1);

    if (tptr == NULL)
    {
      int e = errno;
      ESP_LOGE(CAMWEBSRV_TAG, "STORAGE camwebsrv_storage_get(): realloc() failed: [%d]: %s", e, strerror(e));
      free(tbuf);
      close(fd);
      return ESP_FAIL;
    }

    // copy the block, and append a null byte

    memcpy(tptr + tlen, block, n);
    tptr[tlen + n] = 0x00;

    tbuf = tptr;
    tlen = tlen + n;

    ESP_LOGV(CAMWEBSRV_TAG, "STORAGE camwebsrv_storage_get(): read(%s): read in %u bytes", path, n);
  }

  // close

  close(fd);

  // call callback

  cb((const char *) tbuf, tlen, arg);

  // cleanup

  free(tbuf);

  ESP_LOGI(CAMWEBSRV_TAG, "STORAGE camwebsrv_storage_get(%s): read %u bytes", path, tlen);

  return ESP_OK;
}
