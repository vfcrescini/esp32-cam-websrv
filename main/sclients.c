// 2023-02-06 sclients.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "sclients.h"
#include "vbytes.h"

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define _CAMWEBSRV_SCLIENTS_RESP_HDR_MAIN_STR "\
HTTP/1.1 200 OK\r\n\
Content-Type: multipart/x-mixed-replace;boundary=0123456789ABCDEF\r\n\
Transfer-Encoding: chunked\r\n\
Access-Control-Allow-Origin: *\r\n\
\r\n\
"

#define _CAMWEBSRV_SCLIENTS_RESP_HDR_CHUNK_STR "\
14\r\n--0123456789ABCDEF\r\n\r\n\
1A\r\nContent-Type: image/jpeg\r\n\r\n\
%x\r\nContent-Length: %u\r\n\r\n\
2\r\n\r\n\r\n\
%x\r\n\
"

#if CONFIG_LWIP_IPV6
  #define _CAMWEBSRV_SCLIIENTS_SOCKADDR_IN_T   struct sockaddr_in6
  #define _CAMWEBSRV_SCLIENTS_AF               AF_INET6
  #define _CAMWEBSRV_SCLIENTS_ADDRSTRLEN       INET6_ADDRSTRLEN
  #define _CAMWEBSRV_SCLIENTS_ADDR(X)          ((X).sin6_addr)
  #define _CAMWEBSRV_SCLIENTS_PORT(X)          ((X).sin6_port)
#else
  #define _CAMWEBSRV_SCLIIENTS_SOCKADDR_IN_T  struct sockaddr_in
  #define _CAMWEBSRV_SCLIENTS_AF              AF_INET
  #define _CAMWEBSRV_SCLIENTS_ADDRSTRLEN      INET_ADDRSTRLEN
  #define _CAMWEBSRV_SCLIENTS_ADDR(X)         ((X).sin_addr)
  #define _CAMWEBSRV_SCLIENTS_PORT(X)         ((X).sin_port)
#endif

typedef struct _camwebsrv_sclients_node_t
{
  int sockfd;
  camwebsrv_vbytes_t sockbuf;
  struct _camwebsrv_sclients_node_t *next;
  int64_t tframelast;
  int64_t twritelast;
} _camwebsrv_sclients_node_t;

typedef struct
{
  _camwebsrv_sclients_node_t *list;
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
esp_err_t _camwebsrv_sclients_sock_get_peer(int sockfd, char *caddr);
esp_err_t _camwebsrv_sclients_purge(_camwebsrv_sclients_node_t **plist, httpd_handle_t handle);

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

  *clients = pclients;

  return ESP_OK;
}

esp_err_t camwebsrv_sclients_destroy(camwebsrv_sclients_t *clients, httpd_handle_t handle)
{
  _camwebsrv_sclients_t *pclients;
  esp_err_t rv;

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

  rv = _camwebsrv_sclients_purge(&(pclients->list), handle);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_destroy(): _camwebsrv_sclients_purge() failed: [%d]: %s", rv, esp_err_to_name(rv));
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
  char caddr[_CAMWEBSRV_SCLIENTS_ADDRSTRLEN + 6];
  esp_err_t rv;

  if (clients == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pclients = (_camwebsrv_sclients_t *) clients;

  rv = _camwebsrv_sclients_sock_get_peer(sockfd, caddr);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(%d): _camwebsrv_sclients_sock_get_peer failed: [%d]: %s", sockfd, rv, esp_err_to_name(rv));
    return rv;
  }

  // get mutex

  if (xSemaphoreTake(pclients->mutex, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(%d): xSemaphoreTake() failed", sockfd);
    return ESP_FAIL;
  }

  // does it already exist?

  if (_camwebsrv_sclients_sock_exists(pclients->list, sockfd))
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(%d): failed: already in the client list", sockfd);
    xSemaphoreGive(pclients->mutex);
    return ESP_FAIL;
  }

  // create new node

  pnode = (_camwebsrv_sclients_node_t *) malloc(sizeof(_camwebsrv_sclients_node_t));

  if (pnode == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(%d): malloc() failed: [%d]: %s", sockfd, e, strerror(e));
    xSemaphoreGive(pclients->mutex);
    return ESP_FAIL;
  }

  pnode->sockfd = sockfd;
  pnode->next = pclients->list;
  pnode->tframelast = 0;
  pnode->twritelast = esp_timer_get_time();

  rv = camwebsrv_vbytes_init(&(pnode->sockbuf));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(%d): camwebsrv_vbytes_init() failed: [%d]: %s", sockfd, rv, esp_err_to_name(rv));
    free(pnode);
    xSemaphoreGive(pclients->mutex);
    return rv;
  }

  // load http headers in buffer
  // XXX: instead of loading into the buffer, consider attempting to write to the socket instead

  rv = camwebsrv_vbytes_append_str(pnode->sockbuf, _CAMWEBSRV_SCLIENTS_RESP_HDR_MAIN_STR);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(%d): camwebsrv_vbytes_append_str() failed: [%d]: %s", sockfd, rv, esp_err_to_name(rv));
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

  ESP_LOGI(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_add(%d): Added client %s", sockfd, caddr);

  return ESP_OK;
}

esp_err_t camwebsrv_sclients_purge(camwebsrv_sclients_t clients, httpd_handle_t handle)
{
  _camwebsrv_sclients_t *pclients;
  esp_err_t rv;

  if (clients == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pclients = (_camwebsrv_sclients_t *) clients;

  xSemaphoreTake(pclients->mutex, portMAX_DELAY);

  rv = _camwebsrv_sclients_purge(&(pclients->list), handle);

  xSemaphoreGive(pclients->mutex);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_purge(): _camwebsrv_sclients_purge() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_purge(): Removed all clients");

  return ESP_OK;
}

esp_err_t camwebsrv_sclients_process(camwebsrv_sclients_t clients, camwebsrv_camera_t cam, httpd_handle_t handle, uint16_t *nextevent)
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
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(): xSemaphoreTake(mutex) failed");
    return ESP_FAIL;
  }

  // traverse list

  curr = pclients->list;
  prev = NULL;

  while(curr != NULL)
  {
    bool flushed = false;
    int sockfd = curr->sockfd;
    int64_t tnow = esp_timer_get_time();

    // check the idle timer

    if ((tnow - curr->twritelast) > (CAMWEBSRV_SCLIENTS_IDLE_TMOUT * 1000))
    {
      ESP_LOGW(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(%d): exceeded idle time limit", sockfd);
      goto rm_client;
    }

    // attempt to flush out the socket buffer

    rv = _camwebsrv_sclients_node_flush(curr, &flushed);

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(%d): _camwebsrv_sclients_node_flush() failed: [%d]: %s", sockfd, rv, esp_err_to_name(rv));
      goto rm_client;
    }

    // is the buffer empty?

    if (flushed)
    {
      // has enough time lapsed since the last frame?

      if (tnow > (curr->tframelast + (1000000 / camwebsrv_camera_fps_get(cam))))
      {
        uint8_t *fbuf = NULL;
        size_t flen = 0;
        int64_t ftstamp = 0;

        // get, send and dispose new frame

        rv = camwebsrv_camera_frame_grab(cam, &fbuf, &flen, &ftstamp);

        if (rv != ESP_OK)
        {
          ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(%d): camwebsrv_camera_frame_grab() failed: [%d]: %s", sockfd, rv, esp_err_to_name(rv));
          goto rm_client;
        }

        rv = _camwebsrv_sclients_node_frame(curr, fbuf, flen);

        camwebsrv_camera_frame_dispose(cam);

        if (rv != ESP_OK)
        {
          ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(%d): _camwebsrv_sclients_node_frame() failed: [%d]: %s", sockfd, rv, esp_err_to_name(rv));
          goto rm_client;
        }

        curr->tframelast = ftstamp;
      }
    }

    // the next event for this client is ASAP if there is something in the
    // buffer, or whenever the next frame is due, otherwise

    if (nextevent != NULL)
    {
      if (camwebsrv_vbytes_length(curr->sockbuf) > 0)
      {
        *nextevent = CAMWEBSRV_MAIN_MIN_CYCLE_MSEC;
      }
      else
      {
        *nextevent = 1000 / camwebsrv_camera_fps_get(cam);
      }
    }

    prev = curr;
    curr = curr->next;

    continue;

    // on error, close socket, then delete node

    rm_client:

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

      ESP_LOGI(CAMWEBSRV_TAG, "SCLIENTS camwebsrv_sclients_process(%d): Removed client", sockfd);
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
  TickType_t started = xTaskGetTickCount();

  while(bytes_left > 0)
  {
    ssize_t rv;

    // have we exceeded the time limit?

    if ((xTaskGetTickCount() - started) > pdMS_TO_TICKS(CAMWEBSRV_SCLIENTS_SEND_TMOUT))
    {
      ESP_LOGW(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_send_bytes(%d): exceeded send time limit", sockfd);
      break;
    }

    // how much to send this time?

    bytes_blck = bytes_left < CAMWEBSRV_SCLIENTS_BSIZE ? bytes_left : CAMWEBSRV_SCLIENTS_BSIZE;

    // send a block at a time
    // XXX we really should use httpd_socket_send() here, but we can't until
    // IDFGH-9275 is fixed

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
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_send_bytes(%d): send() failed", sockfd);
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
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_bytes(%d): _camwebsrv_sclients_sock_send_bytes() failed", pnode->sockfd);
      return ESP_FAIL;
    }

    // update idle timer

    pnode->twritelast = esp_timer_get_time();

    // increment stuff

    bytes = bytes + sent;
    len = len - sent;
  }

  // enbuffer unsent data

  rv = camwebsrv_vbytes_append_bytes(pnode->sockbuf, bytes, len);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_bytes(%d): camwebsrv_vbytes_append_bytes() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
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
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_vbytes(%d): camwebsrv_vbytes_get_bytes() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  rv = _camwebsrv_sclients_node_send_bytes(pnode, bbytes, blen);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_vbytes(%d): _camwebsrv_sclients_node_send_bytes() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
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
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_str(%d): camwebsrv_vbytes_init() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  va_start(vlist, fmt);
  rv = camwebsrv_vbytes_set_vlist(vbytes, fmt, vlist);
  va_end(vlist);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_str(%d): camwebsrv_vbytes_set_vlist() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  rv = _camwebsrv_sclients_node_send_vbytes(pnode, vbytes);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_send_str(%d): _camwebsrv_sclients_node_send_vbytes() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
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
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_flush(%d): camwebsrv_vbytes_get_bytes() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // nothing to do if the buffer is empty

  if (blen > 0)
  {
    // make one attempt to send buffered data to socket

    sent = _camwebsrv_sclients_sock_send_bytes(pnode->sockfd, bbytes, blen);

    if (sent < 0)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_flush(%d): _camwebsrv_sclients_sock_send_bytes() failed", pnode->sockfd);
      return ESP_FAIL;
    }

    // update idle timer

    pnode->twritelast = esp_timer_get_time();

    // reset buffer to whatever remains unsent

    rv = camwebsrv_vbytes_set_bytes(pnode->sockbuf, bbytes + sent, blen - sent);

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_flush(%d): camwebsrv_vbytes_set_bytes() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
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

  // chunk header

  rv = _camwebsrv_sclients_node_send_str(
    pnode,
    _CAMWEBSRV_SCLIENTS_RESP_HDR_CHUNK_STR,
    18 + _camwebsrv_sclients_count_digits(flen),
    flen,
    flen
  );

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(%d): _camwebsrv_sclients_node_send_str(1) failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // chunk data

  rv = _camwebsrv_sclients_node_send_bytes(pnode, fbuf, flen);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(%d): _camwebsrv_sclients_node_send_bytes() failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  // chunk end

  rv = _camwebsrv_sclients_node_send_str(pnode, "\r\n");

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_node_frame(%d): _camwebsrv_sclients_node_send_str(1) failed: [%d]: %s", pnode->sockfd, rv, esp_err_to_name(rv));
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t _camwebsrv_sclients_sock_get_peer(int sockfd, char *caddr)
{
  _CAMWEBSRV_SCLIIENTS_SOCKADDR_IN_T addr;
  socklen_t len = sizeof(addr);
  char tip[_CAMWEBSRV_SCLIENTS_ADDRSTRLEN];

  memset(&addr, 0x00, sizeof(addr));

  if (getpeername(sockfd, (struct sockaddr *) &addr, &len) != 0)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_sock_get_peer(): getpeername() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  if (inet_ntop(_CAMWEBSRV_SCLIENTS_AF, &_CAMWEBSRV_SCLIENTS_ADDR(addr), tip, _CAMWEBSRV_SCLIENTS_ADDRSTRLEN) == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_sock_get_peer(): inet_ntop() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  sprintf(caddr, "%s:%d", tip, ntohs(_CAMWEBSRV_SCLIENTS_PORT(addr)));

  return ESP_OK;
}

esp_err_t _camwebsrv_sclients_purge(_camwebsrv_sclients_node_t **plist, httpd_handle_t handle)
{
  _camwebsrv_sclients_node_t *cnode;
  _camwebsrv_sclients_node_t *tnode;
  esp_err_t rv;

  cnode = *plist;

  while(cnode != NULL)
  {
    // be graceful and try to flush out the buffer first

    rv = _camwebsrv_sclients_node_flush(cnode, NULL);

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_purge(%d): _camwebsrv_sclients_node_flush() failed: [%d]: %s", cnode->sockfd, rv, esp_err_to_name(rv));
    }

    // kill session

    if (cnode->sockfd > 0)
    {
      httpd_sess_trigger_close(handle, cnode->sockfd);
    }

    // destroy buffer

    if (cnode->sockbuf != NULL)
    {
      camwebsrv_vbytes_destroy(&(cnode->sockbuf));
    }

    tnode = cnode;
    cnode = cnode->next;

    ESP_LOGI(CAMWEBSRV_TAG, "SCLIENTS _camwebsrv_sclients_purge(%d): Removed client", tnode->sockfd);

    free(tnode);
  }

  *plist = NULL;

  return ESP_OK;
}
