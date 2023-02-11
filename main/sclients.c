// 2023-02-06 sclients.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "sclients.h"
#include "vbytes.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <esp_log.h>
#include <esp_err.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define _CAMWEBSRV_SCLIENTS_MULTIPART_HEADER_BOUNDARY "U8dOTFrG6WId0hT/TDkN2gx+0TvJCcSMFl7b/3B/B1j86B1GYo3LnEh491bJJ7/BwkRWQXX"

typedef struct _camwebsrv_sclients_node_t
{
  int sockfd;
  camwebsrv_vbytes_t sockbuf;
  struct _camwebsrv_sclients_node_t *next;
} _camwebsrv_sclients_node_t;

typedef struct
{
  _camwebsrv_sclients_node_t *list;
  unsigned int len;
  SemaphoreHandle_t mutex;
} _camwebsrv_sclients_t;

size_t _camwebsrv_sclients_count_digits(size_t n);
bool _camwebsrv_sclients_sock_exists(_camwebsrv_sclients_node_t *pnode, int sockfd);
ssize_t _camwebsrv_sclients_sock_send_bytes(int sockfd, uint8_t *bytes, size_t len);
esp_err_t _camwebsrv_sclients_node_send_bytes(_camwebsrv_sclients_node_t *pnode, uint8_t *bytes, size_t len);
esp_err_t _camwebsrv_sclients_node_send_vbytes(_camwebsrv_sclients_node_t *pnode, camwebsrv_vbytes_t *vbytes);
esp_err_t _camwebsrv_sclients_node_send_str(_camwebsrv_sclients_node_t *pnode, const char *fmt, ...);
esp_err_t _camwebsrv_sclients_node_flush(_camwebsrv_sclients_node_t *pnode, bool *flushed);
esp_err_t _camwebsrv_sclients_node_frame(_camwebsrv_sclients_node_t *pnode, uint8_t *fbuf, size_t flen);

esp_err_t camwebsrv_sclients_init(camwebsrv_sclients_t *clients)
{
  _camwebsrv_sclients_t *pclients;

  if (clients == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pclients = (_camwebsrv_sclients_t *) malloc(sizeof(_camwebsrv_sclients_t));

  if (pclients == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_init(): malloc() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  pclients->mutex = xSemaphoreCreateMutex();

  if (pclients->mutex == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_init(): xSemaphoreCreateMutex() failed");
    free(pclients);
    return ESP_FAIL;
  }

  pclients->list = NULL;
  pclients->len = 0;

  *clients = pclients;

  return ESP_OK;
}

esp_err_t camwebsrv_sclients_destroy(camwebsrv_sclients_t *clients, httpd_handle_t handle)
{
   _camwebsrv_sclients_t *pclients;
   _camwebsrv_sclients_node_t *plist;

  if (clients == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pclients = (_camwebsrv_sclients_t *) *clients;

  if (pclients == NULL)
  {
    return ESP_OK;
  }

  xSemaphoreTake(pclients->mutex, portMAX_DELAY);

  plist = pclients->list;

  while(plist != NULL)
  {
    if (plist->sockfd > 0)
    {
      httpd_sess_trigger_close(handle, plist->sockfd);
    }
    if (plist->sockbuf != NULL)
    {
      camwebsrv_vbytes_destroy(&(plist->sockbuf));
    }
    plist = plist->next;
  }

  *clients = NULL;

  xSemaphoreGive(pclients->mutex);
  vSemaphoreDelete(pclients->mutex);

  free(pclients);

  return ESP_OK;
}

esp_err_t camwebsrv_sclients_add(camwebsrv_sclients_t clients, int sockfd)
{
  _camwebsrv_sclients_t *pclients;
  _camwebsrv_sclients_node_t *pnode;
  esp_err_t rv;

  if (clients == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pclients = (_camwebsrv_sclients_t *) clients;

  // get mutex

  if (xSemaphoreTake(pclients->mutex, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): xSemaphoreTake() failed");
    return ESP_FAIL;
  }

  // does it already exist?

  if (_camwebsrv_sclients_sock_exists(pclients->list, sockfd))
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): sockfd %d is already in the client list", sockfd);
    xSemaphoreGive(pclients->mutex);
    return ESP_FAIL;
  }

  // create new node

  pnode = (_camwebsrv_sclients_node_t *) malloc(sizeof(_camwebsrv_sclients_node_t));

  if (pnode == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): malloc() failed: [%d]: %s", e, strerror(e));
    xSemaphoreGive(pclients->mutex);
    return ESP_FAIL;
  }

  pnode->sockfd = sockfd;
  pnode->next = pclients->list;

  rv = camwebsrv_vbytes_init(&(pnode->sockbuf));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): camwebsrv_vbytes_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    free(pnode);
    xSemaphoreGive(pclients->mutex);
    return rv;
  }

  // load http headers in buffer
  // XXX: instead of loading into the buffer, consider attempting to write to the socket instead

  rv = camwebsrv_vbytes_append_str(pnode->sockbuf, "HTTP/1.1 200 OK\r\n");

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): camwebsrv_vbytes_append(1) failed: [%d]: %s", rv, esp_err_to_name(rv));
    camwebsrv_vbytes_destroy(&(pnode->sockbuf));
    free(pnode);
    xSemaphoreGive(pclients->mutex);
    return rv;
  }

  rv = camwebsrv_vbytes_append_str(pnode->sockbuf, "Content-Type: multipart/x-mixed-replace;boundary=%s\r\n", _CAMWEBSRV_SCLIENTS_MULTIPART_HEADER_BOUNDARY);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): camwebsrv_vbytes_append(2) failed: [%d]: %s", rv, esp_err_to_name(rv));
    camwebsrv_vbytes_destroy(&(pnode->sockbuf));
    free(pnode);
    xSemaphoreGive(pclients->mutex);
    return rv;
  }

  rv = camwebsrv_vbytes_append_str(pnode->sockbuf, "Transfer-Encoding: chunked\r\n");

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): camwebsrv_vbytes_append(3) failed: [%d]: %s", rv, esp_err_to_name(rv));
    camwebsrv_vbytes_destroy(&(pnode->sockbuf));
    free(pnode);
    xSemaphoreGive(pclients->mutex);
    return rv;
  }

  rv = camwebsrv_vbytes_append_str(pnode->sockbuf, "Access-Control-Allow-Origin: *\r\n");

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): camwebsrv_vbytes_append(4) failed: [%d]: %s", rv, esp_err_to_name(rv));
    camwebsrv_vbytes_destroy(&(pnode->sockbuf));
    free(pnode);
    xSemaphoreGive(pclients->mutex);
    return rv;
  }

  rv = camwebsrv_vbytes_append_str(pnode->sockbuf, "\r\n");

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): camwebsrv_vbytes_append(5) failed: [%d]: %s", rv, esp_err_to_name(rv));
    camwebsrv_vbytes_destroy(&(pnode->sockbuf));
    free(pnode);
    xSemaphoreGive(pclients->mutex);
    return rv;
  }

  // attach to list

  pclients->list = pnode;

  // release mutex

  xSemaphoreGive(pclients->mutex);

  // done

  ESP_LOGI(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(): Added client sock fd %d", sockfd);

  return ESP_OK;
}

esp_err_t camwebsrv_sclients_process(camwebsrv_sclients_t clients, camwebsrv_camera_t cam, httpd_handle_t handle)
{
  esp_err_t rv;
  _camwebsrv_sclients_t *pclients;
  _camwebsrv_sclients_node_t *curr;
  _camwebsrv_sclients_node_t *prev;
  _camwebsrv_sclients_node_t *temp;

  if (clients == NULL || cam == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }


  pclients = (_camwebsrv_sclients_t *) clients;

  // get mutex

  if (xSemaphoreTake(pclients->mutex, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(): xSemaphoreTake() failed");
    return ESP_FAIL;
  }

  // traverse list

  curr = pclients->list;
  prev = NULL;

  while(curr != NULL)
  {
    bool flushed = false;
    int sockfd;

    sockfd = curr->sockfd;

    // attempt to flush out the socket buffer

    rv = _camwebsrv_sclients_node_flush(curr, &flushed);

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(): _camwebsrv_sclients_node_flush() failed: [%d]: %s", rv, esp_err_to_name(rv));
      goto error;
    }

    // if the socket buffer is empty, get, send and dispose new frame

    if (flushed)
    {
      uint8_t *fbuf = NULL;
      size_t flen = 0;

      rv = camwebsrv_camera_frame_grab(cam, &fbuf, &flen);

      if (rv != ESP_OK)
      {
        ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(): camwebsrv_camera_frame_grab() failed: [%d]: %s", rv, esp_err_to_name(rv));
	goto error;
      }

      rv = _camwebsrv_sclients_node_frame(curr, fbuf, flen);

      camwebsrv_camera_frame_dispose(cam);

      if (rv != ESP_OK)
      {
        ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(): _camwebsrv_sclients_node_frame() failed: [%d]: %s", rv, esp_err_to_name(rv));
	goto error;
      }
    }

    prev = curr;
    curr = curr->next;

    continue;

    // on error, close socket, then delete node

    error:

      httpd_sess_trigger_close(handle, sockfd);
      camwebsrv_vbytes_destroy(&(curr->sockbuf));

      temp = curr;

      if (prev == NULL)
      {
        pclients->list = curr->next;
	curr = pclients->list;
      }
      else
      {
	prev->next = curr->next;
        curr = prev->next;
      }

      free(temp);

      ESP_LOGI(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(): Removed client sock fd %d", sockfd);
  }

  // release mutex

  xSemaphoreGive(pclients->mutex);

  return ESP_OK;
}

size_t _camwebsrv_sclients_count_digits(size_t n)
{
  size_t i;

  for(i = 0; n > 0; i++)
  {
    n = n / 10;
  }

  return i;
}

bool _camwebsrv_sclients_sock_exists(_camwebsrv_sclients_node_t *pnode, int sockfd)
{
  while(pnode != NULL)
  {
    if (pnode->sockfd == sockfd)
    {
      return true;
    }
    pnode = pnode->next;
  }
  return false;
}

ssize_t _camwebsrv_sclients_sock_send_bytes(int sockfd, uint8_t *bytes, size_t len)
{
  size_t bytes_left = len;
  size_t bytes_sent = 0;
  size_t bytes_blck;

  while(bytes_left > 0)
  {
    ssize_t rv;

    // how much to send this time?

    bytes_blck = bytes_left < CAMWEBSRV_SCLIENTS_BSIZE ? bytes_left : CAMWEBSRV_SCLIENTS_BSIZE;

    // send a block at a time

    rv = send(sockfd, (const void *) bytes, bytes_blck, MSG_DONTWAIT | MSG_NOSIGNAL);

    // error?

    if (rv < 0)
    {
      int e = errno;

      if (e == EAGAIN || e == EWOULDBLOCK)
      {
        ESP_LOGD(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_send_bytes(%d): send() would block", sockfd);
	break;
      }

      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_send_bytes(%d): send() failed: [%d]: %s", sockfd, e, strerror(e));
      return -1;
    }

    // this should never happen, given that we're sendig with the NONBLOCK flag

    if (rv == 0)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_send_bytes(): send() failed");
      return -2;
    }

    // success!

    bytes_left = bytes_left - rv;
    bytes_sent = bytes_sent + rv;
    bytes = bytes + rv;
  }

  ESP_LOGD(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_send_bytes(%d): sent %u bytes", sockfd, bytes_sent);

  return bytes_sent;
}

esp_err_t _camwebsrv_sclients_node_send_bytes(_camwebsrv_sclients_node_t *pnode, uint8_t *bytes, size_t len)
{
  esp_err_t rv;

  // if the buffer is empty, make one attempt to send data to the socket first

  if (camwebsrv_vbytes_length(pnode->sockbuf) == 0)
  {
    ssize_t sent;

    sent = _camwebsrv_sclients_sock_send_bytes(pnode->sockfd, bytes, len);

    if (sent < 0)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_bytes(): _camwebsrv_sclients_sock_send_bytes() failed");
      return ESP_FAIL;
    }

    bytes = bytes + sent;
    len = len - sent;
  }

  // enbuffer unsent data

  rv = camwebsrv_vbytes_append_bytes(pnode->sockbuf, bytes, len);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_bytes(): camwebsrv_vbytes_append_bytes() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t _camwebsrv_sclients_node_send_vbytes(_camwebsrv_sclients_node_t *pnode, camwebsrv_vbytes_t *vbytes)
{
  esp_err_t rv;
  uint8_t *bbytes = NULL;
  size_t blen = 0;

  rv = camwebsrv_vbytes_get_bytes(vbytes, (const uint8_t **) &bbytes, &blen);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_vbytes(): camwebsrv_vbytes_get_bytes() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  rv = _camwebsrv_sclients_node_send_bytes(pnode, bbytes, blen);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_vbytes(): _camwebsrv_sclients_node_send_bytes() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t _camwebsrv_sclients_node_send_str(_camwebsrv_sclients_node_t *pnode, const char *fmt, ...)
{
  va_list vlist;
  esp_err_t rv;
  camwebsrv_vbytes_t vbytes;

  rv = camwebsrv_vbytes_init(&vbytes);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_str(): camwebsrv_vbytes_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  va_start(vlist, fmt);
  rv = camwebsrv_vbytes_set_vlist(vbytes, fmt, vlist);
  va_end(vlist);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_str(): camwebsrv_vbytes_set_vlist() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  rv = _camwebsrv_sclients_node_send_vbytes(pnode, vbytes);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_str(): _camwebsrv_sclients_node_send_vbytes() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  camwebsrv_vbytes_destroy(&vbytes);

  return ESP_OK;
}

esp_err_t _camwebsrv_sclients_node_flush(_camwebsrv_sclients_node_t *pnode, bool *flushed)
{
  esp_err_t rv;
  ssize_t sent = 0;
  uint8_t *bbytes = NULL;
  size_t blen = 0;

  // get internal buffer

  rv = camwebsrv_vbytes_get_bytes(pnode->sockbuf, (const uint8_t **) &bbytes, &blen);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_flush(): camwebsrv_vbytes_get_bytes() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // nothing to do if the buffer is empty

  if (blen > 0)
  {
    // make one attempt to send buffered data to socket

    sent = _camwebsrv_sclients_sock_send_bytes(pnode->sockfd, bbytes, blen);

    if (sent < 0)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_flush(): _camwebsrv_sclients_sock_send_bytes() failed");
      return ESP_FAIL;
    }

    // reset buffer to whatever remains unsent

    rv = camwebsrv_vbytes_set_bytes(pnode->sockbuf, bbytes + sent, blen - sent);

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_flush(): camwebsrv_vbytes_set_bytes() failed: [%d]: %s", rv, esp_err_to_name(rv));
      return ESP_FAIL;
    }
  }

  // set flag, if supplied

  if (flushed != NULL)
  {
    *flushed = (blen == sent);
  }

  return ESP_OK;
}

esp_err_t _camwebsrv_sclients_node_frame(_camwebsrv_sclients_node_t *pnode, uint8_t *fbuf, size_t flen)
{
  esp_err_t rv;

  // boundary

  rv = _camwebsrv_sclients_node_send_str(pnode, "%x\r\n--%s\r\n\r\n", strlen(_CAMWEBSRV_SCLIENTS_MULTIPART_HEADER_BOUNDARY) + 2 + 2, _CAMWEBSRV_SCLIENTS_MULTIPART_HEADER_BOUNDARY);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(): _camwebsrv_sclients_node_send_str(1) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // content-type

  rv = _camwebsrv_sclients_node_send_str(pnode, "%x\r\nContent-Type: image/jpeg\r\n\r\n", 26);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(): _camwebsrv_sclients_node_send_str(2) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // content-length

  rv = _camwebsrv_sclients_node_send_str(pnode, "%x\r\nContent-Length: %u\r\n\r\n", 18 + _camwebsrv_sclients_count_digits(flen), flen);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(): _camwebsrv_sclients_node_send_str(3) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // blank

  rv = _camwebsrv_sclients_node_send_str(pnode, "%x\r\n\r\n\r\n", 2);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(): _camwebsrv_sclients_node_send_str(4) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // frame length

  rv = _camwebsrv_sclients_node_send_str(pnode, "%x\r\n", flen);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(): _camwebsrv_sclients_node_send_str(5) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // frame data

  rv = _camwebsrv_sclients_node_send_bytes(pnode, fbuf, flen);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(): _camwebsrv_sclients_node_send_bytes(6) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // end

  rv = _camwebsrv_sclients_node_send_str(pnode, "\r\n");

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(): _camwebsrv_sclients_node_send_str(7) failed: [%d]: %s", rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  return ESP_OK;
}
