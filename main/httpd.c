// 2023-01-03 httpd.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "httpd.h"
#include "camera.h"
#include "storage.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <esp_log.h>
#include <esp_http_server.h>

#define _CAMWEBSRV_HTTPD_SERVER_PORT_1  80
#define _CAMWEBSRV_HTTPD_SERVER_PORT_2  81
#define _CAMWEBSRV_HTTPD_CONTROL_PORT_1 32768
#define _CAMWEBSRV_HTTPD_CONTROL_PORT_2 32769

#define _CAMWEBSRV_HTTPD_PATH_ROOT    "/"
#define _CAMWEBSRV_HTTPD_PATH_STYLE   "/style.css"
#define _CAMWEBSRV_HTTPD_PATH_SCRIPT  "/script.js"
#define _CAMWEBSRV_HTTPD_PATH_STATUS  "/status"
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

#define _CAMWEBSRV_HTTPD_MULTIPART_HEADER_LEN 256
#define _CAMWEBSRV_HTTPD_MULTIPART_HEADER_BOUNDARY "U8dOTFrG6WId0hT/TDkN2gx+0TvJCcSMFl7b/3B/B1j86B1GYo3LnEh491bJJ7/BwkRWQXX"

static esp_err_t _camwebsrv_httpd_handler_static(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_status(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_control(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_capture(httpd_req_t *req);
static esp_err_t _camwebsrv_httpd_handler_stream(httpd_req_t *req);
static bool _camwebsrv_httpd_static_cb(const char *buf, size_t len, void *arg);
static bool _camwebsrv_httpd_capture_cb(const char *buf, size_t len, void *arg);
static bool _camwebsrv_httpd_stream_cb(const char *buf, size_t len, void *arg);

esp_err_t camwebsrv_httpd_init(camwebsrv_httpd_t *httpd)
{
  httpd_handle_t *handles = NULL;

  if (httpd == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  // allocate mem

  handles = (httpd_handle_t *) malloc(sizeof(httpd_handle_t) * 2);

  if (handles ==  NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_init(): malloc() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  *httpd = (void *) handles;

  return ESP_OK;
}

esp_err_t camwebsrv_httpd_destroy(camwebsrv_httpd_t *httpd)
{
  esp_err_t rv;
  uint8_t i;
  httpd_handle_t *handles = NULL;

  if (httpd == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  handles = (httpd_handle_t *) *httpd;

  for(i = 0; i < 2; i++)
  {
    if (handles[i] != NULL)
    {
      rv = httpd_stop(handles[i]);

      if (rv != ESP_OK)
      {
        ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_destroy(): httpd_stop(%d) failed: [%d]: %s", i, rv, esp_err_to_name(rv));
      }
    }
  }

  free(handles);

  *httpd = NULL;

  return ESP_OK;
}

esp_err_t camwebsrv_httpd_start(camwebsrv_httpd_t httpd)
{
  esp_err_t rv;
  httpd_uri_t uri;
  httpd_handle_t *handles;
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();

  if (httpd == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  handles = (httpd_handle_t *) httpd;

  // instance 1

  c.server_port = _CAMWEBSRV_HTTPD_SERVER_PORT_1;
  c.ctrl_port = _CAMWEBSRV_HTTPD_CONTROL_PORT_1;
  
  rv = httpd_start(&(handles[0]), &c);

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

  httpd_register_uri_handler(handles[0], &uri);

  // register style

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_STYLE;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_static;

  httpd_register_uri_handler(handles[0], &uri);

  // register script

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_SCRIPT;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_static;

  httpd_register_uri_handler(handles[0], &uri);

  // register status

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_STATUS;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_status;

  httpd_register_uri_handler(handles[0], &uri);

  // register control

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_CONTROL;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_control;

  httpd_register_uri_handler(handles[0], &uri);

  // register capture

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_CAPTURE;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_capture;

  httpd_register_uri_handler(handles[0], &uri);

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_start(): started server 1 on port %d", _CAMWEBSRV_HTTPD_SERVER_PORT_1);

  // instance 2

  c.server_port = _CAMWEBSRV_HTTPD_SERVER_PORT_2;
  c.ctrl_port = _CAMWEBSRV_HTTPD_CONTROL_PORT_2;

  rv = httpd_start(&(handles[1]), &c);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_start(): httpd_start() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  // register stream

  memset(&uri, 0x00, sizeof(uri));

  uri.uri     = _CAMWEBSRV_HTTPD_PATH_STREAM;
  uri.method  = HTTP_GET;
  uri.handler = _camwebsrv_httpd_handler_stream;

  httpd_register_uri_handler(handles[1], &uri);

  ESP_LOGI(CAMWEBSRV_TAG, "HTTPD camwebsrv_httpd_start(): started server 2 on port %d", _CAMWEBSRV_HTTPD_SERVER_PORT_2);

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
    httpd_resp_set_type(req, "text/html");
    rv = camwebsrv_storage_get(camwebsrv_camera_is_ov3660() ? "ov3660.htm" : "ov2640.htm", _camwebsrv_httpd_static_cb, (void *) req);
  }

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_static(): camwebsrv_storage_get() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_status(httpd_req_t *req)
{
  esp_err_t rv = ESP_OK;
  char buf[_CAMWEBSRV_HTTPD_RESP_STATUS_LEN];

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
    camwebsrv_camera_get("aec"),
    camwebsrv_camera_get("aec2"),
    camwebsrv_camera_get("aec_value"),
    camwebsrv_camera_get("ae_level"),
    camwebsrv_camera_get("agc"),
    camwebsrv_camera_get("agc_gain"),
    camwebsrv_camera_get("awb"),
    camwebsrv_camera_get("awb_gain"),
    camwebsrv_camera_get("bpc"),
    camwebsrv_camera_get("brightness"),
    camwebsrv_camera_get("colorbar"),
    camwebsrv_camera_get("contrast"),
    camwebsrv_camera_get("dcw"),
    camwebsrv_camera_get("flash"),
    camwebsrv_camera_get("framesize"),
    camwebsrv_camera_get("gainceiling"),
    camwebsrv_camera_get("hmirror"),
    camwebsrv_camera_get("lenc"),
    camwebsrv_camera_get("quality"),
    camwebsrv_camera_get("raw_gma"),
    camwebsrv_camera_get("saturation"),
    camwebsrv_camera_get("sharpness"),
    camwebsrv_camera_get("special_effect"),
    camwebsrv_camera_get("vflip"),
    camwebsrv_camera_get("wb_mode"),
    camwebsrv_camera_get("wpc")
  );

  // send response

  rv = httpd_resp_sendstr(req, buf);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_status(): httpd_resp_sendstr() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_control(httpd_req_t *req)
{
  esp_err_t rv;
  size_t len;
  char *buf; 
  char bvar[_CAMWEBSRV_HTTPD_PARAM_LEN];
  char bval[_CAMWEBSRV_HTTPD_PARAM_LEN];

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

  rv = camwebsrv_camera_set(bvar, atoi(bval));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_control(): camwebsrv_camera_set(\"%s\", %s) failed", bvar, bval);

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

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_capture(httpd_req_t *req)
{
  esp_err_t rv;

  // response type/header status

  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_status(req, "200 OK");

  // set frame callback

  rv = camwebsrv_camera_frame(_camwebsrv_httpd_capture_cb, (void *) req);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_capture(): camwebsrv_camera_frame() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

  return ESP_OK;
}

static esp_err_t _camwebsrv_httpd_handler_stream(httpd_req_t *req)
{
  esp_err_t rv;

  // response type/header status

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" _CAMWEBSRV_HTTPD_MULTIPART_HEADER_BOUNDARY);
  httpd_resp_set_status(req, "200 OK");

  // set frame callback

  rv = camwebsrv_camera_frame(_camwebsrv_httpd_stream_cb, (void *) req);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_handler_stream(): camwebsrv_camera_frame() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return rv;
  }

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

static bool _camwebsrv_httpd_capture_cb(const char *buf, size_t len, void *arg)
{
  esp_err_t rv;
  httpd_req_t *req = (httpd_req_t *) arg;

  rv = httpd_resp_send(req, buf, len);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_capture_cb(): httpd_resp_send() failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
  }

  return false;
}

static bool _camwebsrv_httpd_stream_cb(const char *buf, size_t len, void *arg)
{
  esp_err_t rv;
  char hdr[_CAMWEBSRV_HTTPD_MULTIPART_HEADER_LEN];
  httpd_req_t *req = (httpd_req_t *) arg;

  // multipart headers

  memset(hdr, 0x00, sizeof(hdr));

  snprintf(hdr, sizeof(hdr) - 1, "\r\n");

  rv = httpd_resp_send_chunk(req, hdr, strlen(hdr));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_stream_cb(): httpd_resp_send_chunk(1) failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return false;
  }

  snprintf(hdr, sizeof(hdr) - 1, "--%s\r\n", _CAMWEBSRV_HTTPD_MULTIPART_HEADER_BOUNDARY);

  rv = httpd_resp_send_chunk(req, hdr, strlen(hdr));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_stream_cb(): httpd_resp_send_chunk(2) failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return false;
  }

  snprintf(hdr, sizeof(hdr) - 1, "Content-Type: image/jpeg\r\n");

  rv = httpd_resp_send_chunk(req, hdr, strlen(hdr));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_stream_cb(): httpd_resp_send_chunk(3) failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return false;
  }

  snprintf(hdr, sizeof(hdr) - 1, "Content-Length: %u\r\n", len);

  rv = httpd_resp_send_chunk(req, hdr, strlen(hdr));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_stream_cb(): httpd_resp_send_chunk(4) failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return false;
  }

  snprintf(hdr, sizeof(hdr) - 1, "\r\n");

  rv = httpd_resp_send_chunk(req, hdr, strlen(hdr));

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_stream_cb(): httpd_resp_send_chunk(5) failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return false;
  }

  // body

  rv = httpd_resp_send_chunk(req, buf, len);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD _camwebsrv_httpd_stream_cb(): httpd_resp_send_chunk(6) failed: [%d]: %s", rv, esp_err_to_name(rv));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    return false;
  }

  return true;
}
