// 2023-01-03 camera.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "camera.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_camera.h>
#include <esp_timer.h>

#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

typedef struct
{
  camera_fb_t *fb;
  bool flash;
  bool ov3660;
  int64_t tstamp;
  uint8_t fps;
  SemaphoreHandle_t mutex1;
  SemaphoreHandle_t mutex2;
} _camwebsrv_camera_t;

static esp_err_t _camwebsrv_camera_init(_camwebsrv_camera_t *pcam);

esp_err_t camwebsrv_camera_init(camwebsrv_camera_t *cam)
{
  esp_err_t rv;
  _camwebsrv_camera_t *pcam;

  if (cam == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  // create object

  pcam = (_camwebsrv_camera_t *) malloc(sizeof(_camwebsrv_camera_t));

  if (pcam == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_init(): malloc() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  pcam->mutex1 = xSemaphoreCreateMutex();

  if (pcam->mutex1 == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_init(): xSemaphoreCreateMutex(1) failed");
    free(pcam);
    return ESP_FAIL;
  }

  pcam->mutex2 = xSemaphoreCreateMutex();

  if (pcam->mutex2 == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_init(): xSemaphoreCreateMutex(2) failed");
    vSemaphoreDelete(pcam->mutex1);
    free(pcam);
    return ESP_FAIL;
  }

  pcam->fb = NULL;
  pcam->ov3660 = false;
  pcam->tstamp = -1;

  // set flash led gpio

  rv = gpio_set_direction(CAMWEBSRV_PIN_FLASH, GPIO_MODE_OUTPUT);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_init(): gpio_set_direction() failed: [%d]: %s", rv, esp_err_to_name(rv));
    vSemaphoreDelete(pcam->mutex2);
    vSemaphoreDelete(pcam->mutex1);
    free(pcam);
    return rv;
  }

  // initialise

  rv = _camwebsrv_camera_init(pcam);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_init(): _camwebsrv_camera_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    vSemaphoreDelete(pcam->mutex2);
    vSemaphoreDelete(pcam->mutex1);
    free(pcam);
    return rv;
  }

  *cam = (camwebsrv_camera_t) pcam;

  return ESP_OK;
}

esp_err_t camwebsrv_camera_destroy(camwebsrv_camera_t *cam)
{
  _camwebsrv_camera_t *pcam;

  if (cam == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pcam = (_camwebsrv_camera_t *) *cam;

  if (pcam == NULL)
  {
    return ESP_OK;
  }

  if (xSemaphoreTake(pcam->mutex1, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_destroy(): xSemaphoreTake(1) failed");
    return ESP_FAIL;
  }

  if (xSemaphoreTake(pcam->mutex2, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_destroy(): xSemaphoreTake(2) failed");
    return ESP_FAIL;
  }

  // we have the mutexes, so clear the caller's reference to this object before giving it back

  *cam = NULL;

  xSemaphoreGive(pcam->mutex1);
  vSemaphoreDelete(pcam->mutex1);

  xSemaphoreGive(pcam->mutex2);
  vSemaphoreDelete(pcam->mutex2);

  free(pcam);

  return ESP_OK;
}

esp_err_t camwebsrv_camera_reset(camwebsrv_camera_t cam)
{
  _camwebsrv_camera_t *pcam;
  esp_err_t rv;

  if (cam == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pcam = (_camwebsrv_camera_t *) cam;

  // get both locks

  if (xSemaphoreTake(pcam->mutex1, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_reset(): xSemaphoreTake(1) failed");
    return ESP_FAIL;
  }

  if (xSemaphoreTake(pcam->mutex2, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_reset(): xSemaphoreTake(2) failed");
    xSemaphoreGive(pcam->mutex1);
    return ESP_FAIL;
  }

  // if we're still holding on to a frame, give it back

  if (pcam->fb != NULL)
  {
    esp_camera_fb_return(pcam->fb);
    pcam->fb = NULL;
  }

  // de-init

  rv = esp_camera_deinit();

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_reset(): esp_camera_deinit() failed: [%d]: %s", rv, esp_err_to_name(rv));
    xSemaphoreGive(pcam->mutex2);
    xSemaphoreGive(pcam->mutex1);
    return ESP_FAIL;
  }

  // re-init

  rv = _camwebsrv_camera_init(pcam);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_reset(): _camwebsrv_camera_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    xSemaphoreGive(pcam->mutex2);
    xSemaphoreGive(pcam->mutex1);
    return ESP_FAIL;
  }

  // reset timestamp

  pcam->tstamp = -1;

  // unnlock

  xSemaphoreGive(pcam->mutex2);
  xSemaphoreGive(pcam->mutex1);

  return ESP_OK;
}

esp_err_t camwebsrv_camera_frame_grab(camwebsrv_camera_t cam, uint8_t **fbuf, size_t *flen, int64_t *tstamp)
{
  _camwebsrv_camera_t *pcam;
  int64_t now;

  if (cam == NULL || fbuf == NULL || flen == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pcam = (_camwebsrv_camera_t *) cam;

  // lock

  if (xSemaphoreTake(pcam->mutex2, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_frame_grab(): xSemaphoreTake() failed");
    return ESP_FAIL;
  }

  // do we need a new frame?

  now = esp_timer_get_time();

  if ((now - pcam->tstamp) >= (1000000 / pcam->fps))
  {
    sensor_t *sensor = NULL;
    uint8_t i;

    // return previous frame

    if (pcam->fb != NULL)
    {
      esp_camera_fb_return(pcam->fb);

      pcam->fb = NULL;
      pcam->tstamp = 0;
    }

    // get sensor

    sensor = esp_camera_sensor_get();

    if (sensor == NULL)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "CAM (): camwebsrv_camera_frame_grab(): esp_camera_sensor_get() failed");
      xSemaphoreGive(pcam->mutex2);
      return ESP_FAIL;
    }

    // get frame, but skip the first couple of frames if this is the first grab

    for (i = 0; i < (pcam->tstamp >= 0 ? 1 : CAMWEBSRV_CAMERA_INITIAL_FRAME_SKIP); i++)
    {
      if (i > 0)
      {
        esp_camera_fb_return(pcam->fb);
        vTaskDelay((1000 / pcam->fps) / portTICK_PERIOD_MS);
      }

      pcam->fb = esp_camera_fb_get();

      if (pcam->fb == NULL)
      {
        ESP_LOGE(CAMWEBSRV_TAG, "CAM (): camwebsrv_camera_frame_grab(): esp_camera_fb_get() failed");
        xSemaphoreGive(pcam->mutex2);
        return ESP_FAIL;
      }
    }

    pcam->tstamp = now;
  }

  // give out reference to the current frame

  *fbuf = pcam->fb->buf;
  *flen = pcam->fb->len;

  if (tstamp != NULL)
  {
    *tstamp = pcam->tstamp;
  }

  return ESP_OK;
}

esp_err_t camwebsrv_camera_frame_dispose(camwebsrv_camera_t cam)
{
  _camwebsrv_camera_t *pcam;

  if (cam == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pcam = (_camwebsrv_camera_t *) cam;

  // do we hold the lock?

  if (xSemaphoreGetMutexHolder(pcam->mutex2) != xTaskGetCurrentTaskHandle())
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_frame_dispose(): frame not held");
    return ESP_FAIL;
  }

  // unlock

  xSemaphoreGive(pcam->mutex2);

  return ESP_OK;
}

esp_err_t camwebsrv_camera_ctrl_set(camwebsrv_camera_t cam, const char *name, int value)
{
  sensor_t *sensor = NULL;
  _camwebsrv_camera_t *pcam;

  if (cam == NULL || name == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pcam = (_camwebsrv_camera_t *) cam;

  // lock

  if (xSemaphoreTake(pcam->mutex1, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(): xSemaphoreTake() failed");
    return ESP_FAIL;
  }

  // get sensor

  sensor = esp_camera_sensor_get();

  if (sensor == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): esp_camera_sensor_get() failed", name, value);
    xSemaphoreGive(pcam->mutex1);
    return ESP_FAIL;
  }

  // set stuff

  if (strcmp(name, "aec") == 0)
  {
    if (sensor->set_exposure_ctrl(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_exposure_ctrl() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "aec2") == 0)
  {
    if (sensor->set_aec2(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_aec2() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "aec_value") == 0)
  {
    if (sensor->set_aec_value(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.aec_value() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "ae_level") == 0)
  {
    if (sensor->set_ae_level(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.ae_level() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "agc") == 0)
  {
    if (sensor->set_gain_ctrl(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_gain_ctrl() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "agc_gain") == 0)
  {
    if (sensor->set_agc_gain(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.agc_gain() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "awb") == 0)
  {
    if (sensor->set_whitebal(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_whitebal() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "awb_gain") == 0)
  {
    if (sensor->set_awb_gain(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.awb_gain() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "bpc") == 0)
  {
    if (sensor->set_bpc(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_bpc() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "brightness") == 0)
  {
    if (sensor->set_brightness(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_brightness() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "colorbar") == 0)
  {
    if (sensor->set_colorbar(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_colorbal() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "contrast") == 0)
  {
    if (sensor->set_contrast(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_contrast() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "dcw") == 0)
  {
    if (sensor->set_dcw(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_dcw() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "flash") == 0)
  {
    pcam->flash = value != 0;

    if (gpio_set_level(CAMWEBSRV_PIN_FLASH, pcam->flash) != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): gpio_set_level() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    }
  }
  else if (strcmp(name, "fps") == 0)
  {
    pcam->fps = (value < CAMWEBSRV_CAMERA_FPS_MIN ? CAMWEBSRV_CAMERA_FPS_MIN : (value > CAMWEBSRV_CAMERA_FPS_MAX ? CAMWEBSRV_CAMERA_FPS_MAX : value));
  }
  else if (strcmp(name, "framesize") == 0)
  {
    if (sensor->pixformat == PIXFORMAT_JPEG)
    {
      if (sensor->set_framesize(sensor, (framesize_t) value))
      {
        ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_framesize() failed", name, value);
        xSemaphoreGive(pcam->mutex1);
        return ESP_FAIL;
      }
    }
  }
  else if (strcmp(name, "gainceiling") == 0)
  {
    if (sensor->set_gainceiling(sensor, (gainceiling_t) value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_gainceiling() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "hmirror") == 0)
  {
    if (sensor->set_hmirror(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_hmirror() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "lenc") == 0)
  {
    if (sensor->set_lenc(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_lenc() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "quality") == 0)
  {
    if (sensor->set_quality(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_quality() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "raw_gma") == 0)
  {
    if (sensor->set_raw_gma(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_raw_gma() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "saturation") == 0)
  {
    if (sensor->set_saturation(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_saturation() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "sharpness") == 0)
  {
    if (sensor->set_sharpness(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_sharpness() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "special_effect") == 0)
  {
    if (sensor->set_special_effect(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_special_effect() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "vflip") == 0)
  {
    if (sensor->set_vflip(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_vflip() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "wb_mode") == 0)
  {
    if (sensor->set_wb_mode(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_wb_mode() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "wpc") == 0)
  {
    if (sensor->set_wpc(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d): sensor.set_wpc() failed", name, value);
      xSemaphoreGive(pcam->mutex1);
      return ESP_FAIL;
    } 
  }
  else
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\"): failed; invalid parameter", name);
    xSemaphoreGive(pcam->mutex1);
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_set(\"%s\", %d)", name, value);

  xSemaphoreGive(pcam->mutex1);

  return ESP_OK;
}

int camwebsrv_camera_ctrl_get(camwebsrv_camera_t cam, const char *name)
{
  sensor_t *sensor = NULL;
  _camwebsrv_camera_t *pcam;
  int rv;

  if (cam == NULL || name == NULL)
  {
    return -1;
  }

  pcam = (_camwebsrv_camera_t *) cam;

  // lock

  if (xSemaphoreTake(pcam->mutex1, portMAX_DELAY) != pdTRUE)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_get(): xSemaphoreTake() failed");
    return -1;
  }

  // get sensor

  sensor = esp_camera_sensor_get();

  if (sensor == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_get(\"%s\"): esp_camera_sensor_get() failed", name);
    xSemaphoreGive(pcam->mutex1);
    return -1;
  }

  // get stuff

  if (strcmp(name, "aec") == 0)
  {
    rv = sensor->status.aec;
  }
  else if (strcmp(name, "aec2") == 0)
  {
    rv = sensor->status.aec2;
  }
  else if (strcmp(name, "aec_value") == 0)
  {
    rv = sensor->status.aec_value;
  }
  else if (strcmp(name, "ae_level") == 0)
  {
    rv = sensor->status.ae_level;
  }
  else if (strcmp(name, "agc") == 0)
  {
    rv = sensor->status.agc;
  }
  else if (strcmp(name, "agc_gain") == 0)
  {
    rv = sensor->status.agc_gain;
  }
  else if (strcmp(name, "awb") == 0)
  {
    rv = sensor->status.awb;
  }
  else if (strcmp(name, "awb_gain") == 0)
  {
    rv = sensor->status.awb_gain;
  }
  else if (strcmp(name, "bpc") == 0)
  {
    rv = sensor->status.bpc;
  }
  else if (strcmp(name, "brightness") == 0)
  {
    rv = sensor->status.brightness;
  }
  else if (strcmp(name, "colorbar") == 0)
  {
    rv = sensor->status.colorbar;
  }
  else if (strcmp(name, "contrast") == 0)
  {
    rv = sensor->status.contrast;
  }
  else if (strcmp(name, "dcw") == 0)
  {
    rv = sensor->status.dcw;
  }
  else if (strcmp(name, "flash") == 0)
  {
    rv = pcam->flash;
  }
  else if (strcmp(name, "fps") == 0)
  {
    rv = pcam->fps;
  }
  else if (strcmp(name, "framesize") == 0)
  {
    rv = sensor->status.framesize;
  }
  else if (strcmp(name, "gainceiling") == 0)
  {
    rv = sensor->status.gainceiling;
  }
  else if (strcmp(name, "hmirror") == 0)
  {
    rv = sensor->status.hmirror;
  }
  else if (strcmp(name, "lenc") == 0)
  {
    rv = sensor->status.lenc;
  }
  else if (strcmp(name, "quality") == 0)
  {
    rv = sensor->status.quality;
  }
  else if (strcmp(name, "raw_gma") == 0)
  {
    rv = sensor->status.raw_gma;
  }
  else if (strcmp(name, "saturation") == 0)
  {
    rv = sensor->status.saturation;
  }
  else if (strcmp(name, "sharpness") == 0)
  {
    rv = sensor->status.sharpness;
  }
  else if (strcmp(name, "special_effect") == 0)
  {
    rv = sensor->status.special_effect;
  }
  else if (strcmp(name, "vflip") == 0)
  {
    rv = sensor->status.vflip;
  }
  else if (strcmp(name, "wb_mode") == 0)
  {
    rv = sensor->status.wb_mode;
  }
  else if (strcmp(name, "wpc") == 0)
  {
    rv = sensor->status.wpc;
  }
  else
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_ctrl_get(\"%s\"): failed; invalid parameter", name);
    rv = -1;
  }

  xSemaphoreGive(pcam->mutex1);

  return rv;
}

bool camwebsrv_camera_is_ov3660(camwebsrv_camera_t cam)
{
  _camwebsrv_camera_t *pcam;

  if (cam == NULL)
  {
    return false;
  }

  pcam = (_camwebsrv_camera_t *) cam;

  return pcam->ov3660;
}

uint8_t camwebsrv_camera_fps_get(camwebsrv_camera_t cam)
{
  _camwebsrv_camera_t *pcam;

  if (cam == NULL)
  {
    return 0;
  }

  pcam = (_camwebsrv_camera_t *) cam;

  return pcam->fps;
}

static esp_err_t _camwebsrv_camera_init(_camwebsrv_camera_t *pcam)
{
  esp_err_t rv;
  camera_config_t config;
  sensor_t *sensor;

  memset(&config, 0x00, sizeof(config));

  config.pin_pwdn = CAMWEBSRV_PIN_PWDN;
  config.pin_reset = CAMWEBSRV_PIN_RESET;
  config.pin_xclk = CAMWEBSRV_PIN_XCLK;
  config.pin_sccb_sda = CAMWEBSRV_PIN_SIOD;
  config.pin_sccb_scl = CAMWEBSRV_PIN_SIOC;

  config.pin_d7 = CAMWEBSRV_PIN_D7;
  config.pin_d6 = CAMWEBSRV_PIN_D6;
  config.pin_d5 = CAMWEBSRV_PIN_D5;
  config.pin_d4 = CAMWEBSRV_PIN_D4;
  config.pin_d3 = CAMWEBSRV_PIN_D3;
  config.pin_d2 = CAMWEBSRV_PIN_D2;
  config.pin_d1 = CAMWEBSRV_PIN_D1;
  config.pin_d0 = CAMWEBSRV_PIN_D0;
  config.pin_vsync = CAMWEBSRV_PIN_VSYNC;
  config.pin_href = CAMWEBSRV_PIN_HREF;
  config.pin_pclk = CAMWEBSRV_PIN_PCLK;

  config.xclk_freq_hz = 20000000;
  config.ledc_timer = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;

  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA;

  config.jpeg_quality = 10;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  rv = esp_camera_init(&config);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM _camwebsrv_camera_init(): _esp_camera_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  // this returns a static pointer from the esp32-camera library, so there is
  // no point in holding on to a reference to that

  sensor = esp_camera_sensor_get();

  if (sensor == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM _camwebsrv_camera_init(): esp_camera_sensor_get() failed");
    return ESP_FAIL;
  }

  pcam->ov3660 = (sensor->id.PID == OV3660_PID);

  if (pcam->ov3660)
  {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -2);
  }

  // assert pixformat

  if (sensor->pixformat != PIXFORMAT_JPEG)
  {
      ESP_LOGE(CAMWEBSRV_TAG, "CAM _camwebsrv_camera_init(): sensor.pixformat is not JPEG");
      return ESP_FAIL;
  }

  // set framesize

  if (sensor->set_framesize(sensor, (framesize_t) CAMWEBSRV_CAMERA_DEFAULT_FS))
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM _camwebsrv_camera_init(): sensor.set_framesize(%d) failed", CAMWEBSRV_CAMERA_DEFAULT_FS);
    return ESP_FAIL;
  }

  // set fps

  pcam->fps = CAMWEBSRV_CAMERA_DEFAULT_FPS;

  // set flash

  pcam->flash = CAMWEBSRV_CAMERA_DEFAULT_FLASH;

  if (gpio_set_level(CAMWEBSRV_PIN_FLASH, pcam->flash) != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM _camwebsrv_camera_init(): failed to set flash %s", pcam->flash ? "on" : "off");
    return ESP_FAIL;
  }

  return ESP_OK;
}
