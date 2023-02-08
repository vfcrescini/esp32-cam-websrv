// 2023-01-26 vbytes.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "vbytes.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <string.h>

#include <esp_log.h>
#include <esp_err.h>

typedef struct
{
  uint8_t *vbs;
  size_t len;
} _camwebsrv_vbytes_t;

static esp_err_t _camwebsrv_vbytes_set(_camwebsrv_vbytes_t *nvb, const uint8_t *bytes, size_t len);
static esp_err_t _camwebsrv_vbytes_append(_camwebsrv_vbytes_t *nvb, const uint8_t *bytes, size_t len);
static esp_err_t _camwebsrv_vbytes_asprintf(char **str, const char *fmt, va_list vlist);

esp_err_t camwebsrv_vbytes_init(camwebsrv_vbytes_t *vb)
{
  _camwebsrv_vbytes_t *nvb;

  if (vb == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  nvb = (_camwebsrv_vbytes_t *) malloc(sizeof(_camwebsrv_vbytes_t));

  if (nvb ==  NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES camwebsrv_vbytes_init(): malloc() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  nvb->vbs = NULL;
  nvb->len = 0;

  *vb = nvb;

  return ESP_OK;
}

esp_err_t camwebsrv_vbytes_destroy(camwebsrv_vbytes_t *vb)
{
  _camwebsrv_vbytes_t *nvb;

  if (vb == NULL || *vb == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  nvb = (_camwebsrv_vbytes_t *) *vb;

  if (nvb->vbs != NULL)
  {
    free(nvb->vbs);
  }

  free(nvb);

  *vb = NULL;

  return ESP_OK;
}

esp_err_t camwebsrv_vbytes_get_bytes(camwebsrv_vbytes_t vb, const uint8_t **bytes, size_t *len)
{
  _camwebsrv_vbytes_t *nvb;

  if (vb == NULL || bytes == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  nvb = (_camwebsrv_vbytes_t *) vb;

  *bytes = nvb->vbs;

  if (len != NULL)
  {
    *len = nvb->len;
  }

  return ESP_OK;
}

esp_err_t camwebsrv_vbytes_set_bytes(camwebsrv_vbytes_t vb, const uint8_t *bytes, size_t len)
{
  if (vb == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (bytes == NULL && len != 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  return _camwebsrv_vbytes_set((_camwebsrv_vbytes_t *) vb, bytes, len);
}

esp_err_t camwebsrv_vbytes_set_str(camwebsrv_vbytes_t vb, const char *fmt, ...)
{
  esp_err_t rv;
  va_list vlist;

  va_start(vlist, fmt);
  rv = camwebsrv_vbytes_set_vlist(vb, fmt, vlist);
  va_end(vlist);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES camwebsrv_vbytes_set_str(): camwebsrv_vbytes_set_vlist() failed");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t camwebsrv_vbytes_set_vlist(camwebsrv_vbytes_t vb, const char *fmt, va_list vlist)
{
  esp_err_t rv;
  int len;
  char *str = NULL;

  if (vb == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  len = _camwebsrv_vbytes_asprintf(&str, fmt, vlist);

  if (len < 0)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES camwebsrv_vbytes_set_vlist(): _camwebsrv_vbytes_asprintf() failed");
    return ESP_FAIL;
  }

  rv = _camwebsrv_vbytes_set((_camwebsrv_vbytes_t *) vb, (uint8_t *) str, (size_t) len);

  if (str != NULL)
  {
    free(str);
  }

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES camwebsrv_vbytes_set_vlist(): _camwebsrv_vbytes_set() failed");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t camwebsrv_vbytes_append_bytes(camwebsrv_vbytes_t vb, const uint8_t *bytes, size_t len)
{
  if (vb == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (bytes == NULL && len != 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  return _camwebsrv_vbytes_append((_camwebsrv_vbytes_t *) vb, bytes, len);
}

esp_err_t camwebsrv_vbytes_append_str(camwebsrv_vbytes_t vb, const char *fmt, ...)
{
  esp_err_t rv;
  va_list vlist;

  va_start(vlist, fmt);
  rv = camwebsrv_vbytes_append_vlist(vb, fmt, vlist);
  va_end(vlist);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES camwebsrv_vbytes_append_str(): camwebsrv_vbytes_append_vlist() failed");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t camwebsrv_vbytes_append_vlist(camwebsrv_vbytes_t vb, const char *fmt, va_list vlist)
{
  esp_err_t rv;
  int len;
  char *str = NULL;

  if (vb == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  len = _camwebsrv_vbytes_asprintf(&str, fmt, vlist);

  if (len < 0)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES camwebsrv_vbytes_append_vlist(): _camwebsrv_vbytes_asprintf() failed");
    return ESP_FAIL;
  }

  rv = _camwebsrv_vbytes_append((_camwebsrv_vbytes_t *) vb, (uint8_t *) str, (size_t) len);

  if (str != NULL)
  {
    free(str);
  }

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES camwebsrv_vbytes_append_vlist(): _camwebsrv_vbytes_append() failed");
    return ESP_FAIL;
  }

  return ESP_OK;
}

size_t camwebsrv_vbytes_length(camwebsrv_vbytes_t vb)
{
  _camwebsrv_vbytes_t *nvb;

  if (vb == NULL)
  {
    return 0;
  }

  nvb = (_camwebsrv_vbytes_t *) vb;

  return nvb->len;
}

static esp_err_t _camwebsrv_vbytes_set(_camwebsrv_vbytes_t *nvb, const uint8_t *bytes, size_t len)
{
  size_t size;
  uint8_t *cpy;
  uint8_t *tmp;

  // copy source in case it overlaps the internal byte array

  if (len > 0)
  {
    cpy = (uint8_t *) malloc(len * sizeof(uint8_t));

    if (cpy == NULL)
    {
      int e = errno;
      ESP_LOGE(CAMWEBSRV_TAG, "VBYTES _camwebsrv_vbytes_set(): malloc() failed: [%d]: %s", e, strerror(e));
      return ESP_FAIL;
    }

    memcpy(cpy, bytes, len);
  }

  // calculate new size, then realloc byte array with extra byte for terminating null

  size = ((len + 1 + CAMWEBSRV_VBYTES_BSIZE) / CAMWEBSRV_VBYTES_BSIZE) * CAMWEBSRV_VBYTES_BSIZE;
  tmp = (uint8_t *) realloc(nvb->vbs, size * sizeof(uint8_t));

  if (tmp == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES _camwebsrv_vbytes_set(): realloc() failed: [%d]: %s", e, strerror(e));
    if (len > 0)
    {
      free(cpy);
    }
    return ESP_FAIL;
  }

  // copy

  if (len > 0)
  {
    memcpy(tmp, cpy, len);
  }

  // the terminating null byte

  tmp[len] = 0x00;

  // set fields

  nvb->vbs = tmp;
  nvb->len = len;

  // cleanup

  if (len > 0)
  {
    free(cpy);
  }

  return ESP_OK;
}

static esp_err_t _camwebsrv_vbytes_append(_camwebsrv_vbytes_t *nvb, const uint8_t *bytes, size_t len)
{
  size_t size;
  uint8_t *tmp;

  // is there anything to append?

  if (len == 0)
  {
    return ESP_OK;
  }

  // calculate new size, then realloc byte array with extra byte for terminating null

  size = ((nvb->len + len + 1 + CAMWEBSRV_VBYTES_BSIZE) / CAMWEBSRV_VBYTES_BSIZE) * CAMWEBSRV_VBYTES_BSIZE;
  tmp = (uint8_t *) realloc(nvb->vbs, size * sizeof(uint8_t));

  if (tmp == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES _camwebsrv_vbytes_append(): realloc() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  // append

  memmove(tmp + nvb->len, bytes, len);

  // the terminating null byte

  tmp[nvb->len + len] = 0x00;

  // set fields

  nvb->vbs = tmp;
  nvb->len = size;

  return ESP_OK;
}

static esp_err_t _camwebsrv_vbytes_asprintf(char **str, const char *fmt, va_list vlist)
{
  int len;
  char *tmp;
  va_list vcopy;
  int rv;

  if (fmt != NULL)
  {
    // calculate how much space is needed

    va_copy(vcopy, vlist);
    rv = vsnprintf(NULL, 0, fmt, vcopy);
    va_end(vcopy);

    if (rv < 0)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "VBYTES _camwebsrv_vbytes_asprintf(): vsnprintf() failed");
      return -1;
    }

    len = rv;
  }
  else
  {
    // non-standard special case: if we're given a null, return zero-length string

    len = 0;
  }

  // allocate buffer with extra byte for terminating null

  tmp = (char *) malloc((len + 1) * sizeof(char));

  if (tmp == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "VBYTES _camwebsrv_vbytes_asprintf(): malloc() failed: [%d]: %s", e, strerror(e));
    return -1;
  }

  // generate string

  if (fmt != NULL)
  {
    // copy n + 1 bytes to include null byte 

    va_copy(vcopy, vlist);
    rv = vsnprintf(tmp, len + 1, fmt, vcopy);
    va_end(vcopy);

    if (rv < 0)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "VBYTES _camwebsrv_vbytes_asprintf(): vsnprintf() failed");
      free(tmp);
      return -1;
    }
  }

  *str = tmp;

  return len;
}
