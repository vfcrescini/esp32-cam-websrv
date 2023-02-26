// 2023-01-03 httpd.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "httpd.h"
#include "camera.h"
#include "sclients.h"
#include "storage.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <esp_log.h>
#include <esp_http_server.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define _CAMWEBSRV_HTTPD_SERVER_PORT 80
#define _CAMWEBSRV_HTTPD_CONTROL_PORT 32768

#define _CAMWEBSRV_HTTPD_PATH_ROOT    "/"
#define _CAMWEBSRV_HTTPD_PATH_STYLE   "/style.css"
#define _CAMWEBSRV_HTTPD_PATH_SCRIPT  "/script.js"
#define _CAMWEBSRV_HTTPD_PATH_STATUS  "/status"
#define _CAMWEBSRV_HTTPD_PATH_RESET   "/reset"
#define _CAMWEBSRV_HTTPD_PATH_CONTROL "/control"
#define _CAMWEBSRV_HTTPD_PATH_CAPTURE "/capture"
#define _CAMWEBSRV_HTTPD_PATH_STREAM  "/stream"

#define _CAMWEBSRV_HTTPD_RESP_STATUS_LEN 1024
#define _CAMWEBSRV_HTTPD_RESP_STATUS_STR "\
{\n\
  \"aec\": %u,\n\
  \"aec2\": %u,\n\
  \"aec_value\": %u,\n\
  \"ae_level\": %d,\n\
  \"agc\": %u,\n\
  \"agc_gain\": %u,\n\
  \"awb\": %u,\n\
  \"awb_gain\": %u,\n\
  \"bpc\": %u,\n\
  \"brightness\": %d,\n\
  \"colorbar\": %u,\n\
  \"contrast\": %d,\n\
  \"dcw\": %u,\n\
  \"flash\": %d,\n\
  \"framesize\": %u,\n\
  \"gainceiling\": %u,\n\
  \"hmirror\": %u,\n\
  \"lenc\": %u,\n\
  \"quality\": %u,\n\
  \"raw_gma\": %u,\n\
  \"saturation\": %d,\n\
  \"sharpness\": %d,\n\
  \"special_effect\": %u,\n\
  \"vflip\": %u,\n\
  \"wb_mode\": %u,\n\
  \"wpc\": %u\n\
}\n \
"

#define _CAMWEBSRV_HTTPD_PARAM_LEN 32

typedef struct
{
  httpd_handle_t handle;
  camwebsrv_camera_t cam;
  camwebsrv_sclients_t sclients;
} _camwebsrv_httpd_t;

typedef struct
{
  int sockfd;
  _camwebsrv_httpd_t *phttpd;
} _camwebsrv_httpd_worker_arg_t;

static esp_err_t _camwebsrv_httpd_handler_static(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_status(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_reset(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_control(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_capture(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_stream(httpd_req_t *req);
static bool _camwebsrv_httpd_static_cb(const char *buf, size_t len, void *arg);
static void _camwebsrv_httpd_worker(void *arg);
static void _camwebsrv_httpd_noop(void *arg);

esp_err_t camwebsrv_httpd_init(camwebsrv_httpd_t *httpd)
{
  _camwebsrv_httpd_t *phttpd;
  esp_err_t rv;

  if (httpd == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  phttpd = (_camwebsrv_httpd_t *) malloc(sizeof(_camwebsrv_httpd_t));

  if (phttpd == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_init(): malloc() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  memset(phttpd, 0x00, sizeof(_camwebsrv_httpd_t));

  rv = camwebsrv_camera_init(&(phttpd->cam));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_init(): camwebsrv_camera_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    free(phttpd);
    return ESP_FAIL;
  }

  rv = camwebsrv_sclients_init(&(phttpd->sclients));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_init(): camwebsrv_sclients_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    camwebsrv_camera_destroy(&(phttpd->cam));
    free(phttpd);
    return ESP_FAIL;
  }

  *httpd = (camwebsrv_httpd_t) phttpd;

  return ESP_OK;
}

esp_err_t camwebsrv_httpd_destroy(camwebsrv_httpd_t *httpd)
{
  _camwebsrv_httpd_t *phttpd;
  esp_err_t rv;

  if (httpd == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  phttpd = (_camwebsrv_httpd_t *) *httpd;

  rv = camwebsrv_sclients_destroy(&(phttpd->sclients), phttpd->handle);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_destroy(): camwebsrv_sclients_destroy() failed: [%d]: %s", rv, esp_err_to_name(rv));
  }

  rv = camwebsrv_camera_destroy(&(phttpd->cam));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_destroy(): camwebsrv_camera_destroy() failed: [%d]: %s", rv, esp_err_to_name(rv));
  }

  if (phttpd->handle != NULL)
  {
    rv = httpd_stop(phttpd->handle);

    if (rv != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_destroy(): httpd_stop() failed: [%d]: %s", rv, esp_err_to_name(rv));
    }
  }

  free(phttpd);

  *httpd = NULL;

  return ESP_OK;
}

esp_err_t camwebsrv_httpd_start(camwebsrv_httpd_t httpd)
{
  _camwebsrv_httpd_t *phttpd;
  esp_err_t rv;
  httpd_uri_t uri;
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();

  if (httpd == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  phttpd = (_camwebsrv_httpd_t *) httpd;

  // configuration overrides

  c.server_port = _CAMWEBSRV_HTTPD_SERVER_PORT;
  c.ctrl_port = _CAMWEBSRV_HTTPD_CONTROL_PORT;
  c.global_user_ctx = (void *) phttpd;
  c.global_user_ctx_free_fn = _camwebsrv_httpd_noop;

  rv = httpd_start(&(phttpd->handle), &c);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_start(): httpd_start() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  // register root

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_ROOT;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_static;

  httpd_register_uri_handler(phttpd->handle, &uri);

  // register style

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_STYLE;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_static;

  httpd_register_uri_handler(phttpd->handle, &uri);

  // register script

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_SCRIPT;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_static;

  httpd_register_uri_handler(phttpd->handle, &uri);

  // register status

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_STATUS;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_status;

  httpd_register_uri_handler(phttpd->handle, &uri);

  // register reset

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_RESET;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_reset;

  httpd_register_uri_handler(phttpd->handle, &uri);

  // register control

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_CONTROL;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_control;

  httpd_register_uri_handler(phttpd->handle, &uri);

  // register capture

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_CAPTURE;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_capture;

  httpd_register_uri_handler(phttpd->handle, &uri);

  // register stream

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_STREAM;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_stream;

  httpd_register_uri_handler(phttpd->handle, &uri);

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_start(): started server on port %d", _CAMWEBSRV_HTTPD_SERVER_PORT);

  return ESP_OK;
}

esp_err_t camwebsrv_httpd_process(camwebsrv_httpd_t httpd)
{
  _camwebsrv_httpd_t *phttpd;
  esp_err_t rv;

  if (httpd == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  phttpd = (_camwebsrv_httpd_t *) httpd;

  rv = camwebsrv_sclients_process(phttpd->sclients, phttpd->cam, phttpd->handle);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_process(): camwebsrv_sclients_process() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_static(httpd_req_t *req)
{
  esp_err_t rv;

  httpd_resp_set_status(req, "200 OK");

  // content and type depends on what the request was

  if (strcmp(req->uri, _CAMWEBSRV_HTTPD_PATH_STYLE) == 0)
  {
    httpd_resp_set_type(req, "text/css");
    rv = camwebsrv_storage_get("style.css", _camwebsrv_httpd_static_cb, (void *) req);
  }
  else if (strcmp(req->uri, _CAMWEBSRV_HTTPD_PATH_SCRIPT) == 0)
  {
    httpd_resp_set_type(req, "application/javascript");
    rv = camwebsrv_storage_get("script.js", _camwebsrv_httpd_static_cb, (void *) req);
  }
  else
  {
    _camwebsrv_httpd_t *phttpd;

    phttpd = (_camwebsrv_httpd_t *) httpd_get_global_user_ctx(req->handle);

    httpd_resp_set_type(req, "text/html");
    rv = camwebsrv_storage_get(camwebsrv_camera_is_ov3660(phttpd->cam) ? "ov3660.htm" : "ov2640.htm", _camwebsrv_httpd_static_cb, (void *) req);
  }

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_static(): camwebsrv_storage_get() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_static(%d): served %s", httpd_req_to_sockfd(req), req->uri);

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_status(httpd_req_t *req)
{
  esp_err_t rv = ESP_OK;
  char buf[_CAMWEBSRV_HTTPD_RESP_STATUS_LEN];
  _camwebsrv_httpd_t *phttpd;

  phttpd = (_camwebsrv_httpd_t *) httpd_get_global_user_ctx(req->handle);

  // response type/header status

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, "200 OK");

  // initialise and compose response buffer

  memset(buf, 0x00, sizeof(buf));

  snprintf(
    buf,
    _CAMWEBSRV_HTTPD_RESP_STATUS_LEN - 1,
    _CAMWEBSRV_HTTPD_RESP_STATUS_STR,
    camwebsrv_camera_ctrl_get(phttpd->cam, "aec"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "aec2"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "aec_value"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "ae_level"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "agc"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "agc_gain"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "awb"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "awb_gain"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "bpc"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "brightness"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "colorbar"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "contrast"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "dcw"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "flash"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "framesize"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "gainceiling"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "hmirror"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "lenc"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "quality"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "raw_gma"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "saturation"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "sharpness"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "special_effect"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "vflip"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "wb_mode"),
    camwebsrv_camera_ctrl_get(phttpd->cam, "wpc")
  );

  // send response

  rv = httpd_resp_sendstr(req, buf);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_status(): httpd_resp_sendstr() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_status(%d): served %s", httpd_req_to_sockfd(req), req->uri);

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_reset(httpd_req_t *req)
{
  esp_err_t rv = ESP_OK;
  _camwebsrv_httpd_t *phttpd;

  phttpd = (_camwebsrv_httpd_t *) httpd_get_global_user_ctx(req->handle);

  // response type/header status

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, "200 OK");

  // boot out all clients

  rv = camwebsrv_sclients_purge(phttpd->sclients, phttpd->handle);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_reset(): camwebsrv_sclients_purge() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  // reset

  rv = camwebsrv_camera_reset(phttpd->cam);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_reset(): camwebsrv_camera_reset() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  // send response

  rv = httpd_resp_send(req, NULL, 0);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_reset(): httpd_resp_send() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_reset(%d): served %s", httpd_req_to_sockfd(req), req->uri);

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_control(httpd_req_t *req)
{
  esp_err_t rv;
  size_t len;
  char *buf;
  char bvar[_CAMWEBSRV_HTTPD_PARAM_LEN];
  char bval[_CAMWEBSRV_HTTPD_PARAM_LEN];
  _camwebsrv_httpd_t *phttpd;

  phttpd = (_camwebsrv_httpd_t *) httpd_get_global_user_ctx(req->handle);

  // response type/header status

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_status(req, "200 OK");

  // how long is the query string?

  len = httpd_req_get_url_query_len(req) + 1;

  if (len == 0)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(): failed; zero-length query string");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return ESP_FAIL;
  }

  // initialise buffer for query string

  buf = (char *) malloc(len + 1);

  if (buf == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(): malloc() failed: [%d]: %s", e, strerror(e));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return ESP_FAIL;
  }

  memset(buf, 0x00, len + 1);
  memset(bvar, 0x00, sizeof(bvar));
  memset(bval, 0x00, sizeof(bval));

  // retrieve query string

  rv = httpd_req_get_url_query_str(req, buf, len);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(): httpd_req_get_url_query_str() failed: [%d]: %s", rv, esp_err_to_name(rv));
    free(buf);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    return rv;
  }

  // get variable name from query string

  rv = httpd_query_key_value(buf, "var", bvar, sizeof(bvar) - 1);

  if (rv != ESP_OK)
  {
    free(buf);
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(): httpd_query_key_value(\"var\") failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    return rv;
  }

  // get variable value from query string

  rv = httpd_query_key_value(buf, "val", bval, sizeof(bval) - 1);

  if (rv != ESP_OK)
  {
    free(buf);
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(): httpd_query_key_value(\"val\") failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    return rv;
  }

  // we don't need the query string buffer anymore

  free(buf);

  // set camera variable

  rv = camwebsrv_camera_ctrl_set(phttpd->cam, bvar, atoi(bval));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(): camwebsrv_camera_ctrl_set(\"%s\", %s) failed", bvar, bval);

    if (rv == ESP_ERR_INVALID_ARG)
    {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }
    else
    {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }

    return rv;
  }

  // send response

  rv = httpd_resp_send(req, NULL, 0);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(): httpd_resp_send() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(%d): served %s", httpd_req_to_sockfd(req), req->uri);

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_capture(httpd_req_t *req)
{
  esp_err_t rv;
  uint8_t *fbuf = NULL;
  size_t flen = 0;
  _camwebsrv_httpd_t *phttpd;

  phttpd = (_camwebsrv_httpd_t *) httpd_get_global_user_ctx(req->handle);

  // response type/header status

  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_status(req, "200 OK");

  rv = camwebsrv_camera_frame_grab(phttpd->cam, &fbuf, &flen, NULL);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_capture(): camwebsrv_camera_frame_grab() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  rv = httpd_resp_send(req, (const char *) fbuf, (ssize_t) flen);

  camwebsrv_camera_frame_dispose(phttpd->cam);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_capture(): httpd_resp_send() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_capture(%d): served %s", httpd_req_to_sockfd(req), req->uri);

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_stream(httpd_req_t *req)
{
  esp_err_t rv;
  _camwebsrv_httpd_t *phttpd;
  _camwebsrv_httpd_worker_arg_t *parg;

  phttpd = (_camwebsrv_httpd_t *) httpd_get_global_user_ctx(req->handle);

  parg = (_camwebsrv_httpd_worker_arg_t *) malloc(sizeof(_camwebsrv_httpd_worker_arg_t));

  if (parg == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_stream(): malloc() failed: [%d]: %s", e, strerror(e));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return ESP_FAIL;
  }

  parg->phttpd = phttpd;
  parg->sockfd = httpd_req_to_sockfd(req);

  rv = httpd_queue_work(req->handle, _camwebsrv_httpd_worker, parg);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_stream(): httpd_queue_work() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    free(parg);
    return ESP_FAIL;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_stream(%d): served %s", httpd_req_to_sockfd(req), req->uri);

  return ESP_OK;
}

static bool _camwebsrv_httpd_static_cb(const char *buf, size_t len, void *arg)
{
  esp_err_t rv;
  httpd_req_t *req = (httpd_req_t *) arg;

  rv = httpd_resp_send(req, buf, len);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_static_cb(): httpd_resp_send() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
  }

  return true;
}

static void _camwebsrv_httpd_worker(void *arg)
{
  _camwebsrv_httpd_worker_arg_t *parg;
  esp_err_t rv;

  parg = (_camwebsrv_httpd_worker_arg_t *) arg;

  rv = camwebsrv_sclients_add(parg->phttpd->sclients, parg->sockfd);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_worker(): camwebsrv_sclients_add() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_sess_trigger_close(parg->phttpd->handle, parg->sockfd);
  }

  free(parg);
}

static void _camwebsrv_httpd_noop(void *arg)
{
}
