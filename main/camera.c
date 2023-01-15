// 2023-01-03 camera.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "camera.h"

#include <stdint.h>
#include <string.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_camera.h>

#include <driver/gpio.h>

static bool _camwebsrv_camera_flash_enabled = 0;
static bool _camwebsrv_camera_ov3660 = false;

esp_err_t camwebsrv_camera_init()
{
  esp_err_t rv;
  sensor_t *sensor;
  camera_config_t config;

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
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_init(): esp_camera_init() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  sensor = esp_camera_sensor_get();

  if (sensor == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_init(): esp_camera_sensor_get() failed");
    return ESP_FAIL;
  }

  if (sensor->id.PID == OV3660_PID)
  {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -2);

    _camwebsrv_camera_ov3660 = true;
  }

  rv = gpio_set_direction(CAMWEBSRV_PIN_FLASH, GPIO_MODE_OUTPUT);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_init(): gpio_set_direction() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  return ESP_OK;
}

esp_err_t camwebsrv_camera_frame(camwebsrv_camera_cb_t onframe, void *arg)
{
  sensor_t *sensor = NULL;
  bool go_on = true;

  if (onframe == NULL)
  {
    return ESP_FAIL;
  }

  // get camera

  sensor = esp_camera_sensor_get();

  if (sensor == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_frame(): esp_camera_sensor_get() failed");
    return ESP_FAIL;
  }

  while(go_on)
  {
    camera_fb_t *fb = NULL;

    // get frame

    fb = esp_camera_fb_get();

    if (fb == NULL)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_frame(): esp_camera_fb_get() failed");
      return ESP_FAIL;
    }

    // JPEG?

    if (fb->format != PIXFORMAT_JPEG)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_frame(): failed; unsupported camera pixel format");
      esp_camera_fb_return(fb);
      return ESP_FAIL;
    }

    go_on = onframe((const char *) fb->buf, fb->len, arg);

    esp_camera_fb_return(fb);
  }

  return ESP_OK;
}

esp_err_t camwebsrv_camera_set(const char *name, int value)
{
  sensor_t *sensor;

  sensor = esp_camera_sensor_get();

  if (sensor == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): esp_camera_sensor_get() failed", name, value);
    return ESP_FAIL;
  }

  if (strcmp(name, "aec") == 0)
  {
    if (sensor->set_exposure_ctrl(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_exposure_ctrl() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "aec2") == 0)
  {
    if (sensor->set_aec2(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_aec2() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "aec_value") == 0)
  {
    if (sensor->set_aec_value(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.aec_value() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "ae_level") == 0)
  {
    if (sensor->set_ae_level(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.ae_level() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "agc") == 0)
  {
    if (sensor->set_gain_ctrl(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_gain_ctrl() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "agc_gain") == 0)
  {
    if (sensor->set_agc_gain(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.agc_gain() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "awb") == 0)
  {
    if (sensor->set_whitebal(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_whitebal() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "awb_gain") == 0)
  {
    if (sensor->set_awb_gain(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.awb_gain() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "bpc") == 0)
  {
    if (sensor->set_bpc(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_bpc() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "brightness") == 0)
  {
    if (sensor->set_brightness(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_brightness() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "colorbar") == 0)
  {
    if (sensor->set_colorbar(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_colorbal() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "contrast") == 0)
  {
    if (sensor->set_contrast(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_contrast() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "dcw") == 0)
  {
    if (sensor->set_dcw(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_dcw() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "flash") == 0)
  {
    _camwebsrv_camera_flash_enabled = value != 0;

    if (gpio_set_level(CAMWEBSRV_PIN_FLASH, _camwebsrv_camera_flash_enabled) != ESP_OK)
    {
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): gpio_set_level() failed", name, value);
      return ESP_FAIL;
    }
  }
  else if (strcmp(name, "framesize") == 0)
  {
    if (sensor->pixformat == PIXFORMAT_JPEG)
    {
      if (sensor->set_framesize(sensor, (framesize_t) value))
      {
        ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_framesize() failed", name, value);
        return ESP_FAIL;
      }
    }
  }
  else if (strcmp(name, "gainceiling") == 0)
  {
    if (sensor->set_gainceiling(sensor, (gainceiling_t) value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_gainceiling() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "hmirror") == 0)
  {
    if (sensor->set_hmirror(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_hmirror() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "lenc") == 0)
  {
    if (sensor->set_lenc(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_lenc() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "quality") == 0)
  {
    if (sensor->set_quality(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_quality() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "raw_gma") == 0)
  {
    if (sensor->set_raw_gma(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_raw_gma() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "saturation") == 0)
  {
    if (sensor->set_saturation(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_saturation() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "sharpness") == 0)
  {
    if (sensor->set_sharpness(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_sharpness() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "special_effect") == 0)
  {
    if (sensor->set_special_effect(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_special_effect() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "vflip") == 0)
  {
    if (sensor->set_vflip(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_vflip() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "wb_mode") == 0)
  {
    if (sensor->set_wb_mode(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_wb_mode() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else if (strcmp(name, "wpc") == 0)
  {
    if (sensor->set_wpc(sensor, value))
    { 
      ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d): sensor.set_wpc() failed", name, value);
      return ESP_FAIL;
    } 
  }
  else
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\"): failed; invalid parameter", name);
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(CAMWEBSRV_TAG, "CAM camwebsrv_camera_set(\"%s\", %d)", name, value);

  return ESP_OK;
}

int camwebsrv_camera_get(const char *name)
{
  sensor_t *sensor;

  sensor = esp_camera_sensor_get();

  if (sensor == NULL)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM esp_camera_sensor_get(): failed; invalid parameter");
    return -1;
  }

  if (strcmp(name, "aec") == 0)
  {
    return sensor->status.aec;
  }
  else if (strcmp(name, "aec2") == 0)
  {
    return sensor->status.aec2;
  }
  else if (strcmp(name, "aec_value") == 0)
  {
    return sensor->status.aec_value;
  }
  else if (strcmp(name, "ae_level") == 0)
  {
    return sensor->status.ae_level;
  }
  else if (strcmp(name, "agc") == 0)
  {
    return sensor->status.agc;
  }
  else if (strcmp(name, "agc_gain") == 0)
  {
    return sensor->status.agc_gain;
  }
  else if (strcmp(name, "awb") == 0)
  {
    return sensor->status.awb;
  }
  else if (strcmp(name, "awb_gain") == 0)
  {
    return sensor->status.awb_gain;
  }
  else if (strcmp(name, "bpc") == 0)
  {
    return sensor->status.bpc;
  }
  else if (strcmp(name, "brightness") == 0)
  {
    return sensor->status.brightness;
  }
  else if (strcmp(name, "colorbar") == 0)
  {
    return sensor->status.colorbar;
  }
  else if (strcmp(name, "contrast") == 0)
  {
    return sensor->status.contrast;
  }
  else if (strcmp(name, "dcw") == 0)
  {
    return sensor->status.dcw;
  }
  else if (strcmp(name, "flash") == 0)
  {
    return _camwebsrv_camera_flash_enabled;
  }
  else if (strcmp(name, "framesize") == 0)
  {
    return sensor->status.framesize;
  }
  else if (strcmp(name, "gainceiling") == 0)
  {
    return sensor->status.gainceiling;
  }
  else if (strcmp(name, "hmirror") == 0)
  {
    return sensor->status.hmirror;
  }
  else if (strcmp(name, "lenc") == 0)
  {
    return sensor->status.lenc;
  }
  else if (strcmp(name, "quality") == 0)
  {
    return sensor->status.quality;
  }
  else if (strcmp(name, "raw_gma") == 0)
  {
    return sensor->status.raw_gma;
  }
  else if (strcmp(name, "saturation") == 0)
  {
    return sensor->status.saturation;
  }
  else if (strcmp(name, "sharpness") == 0)
  {
    return sensor->status.sharpness;
  }
  else if (strcmp(name, "special_effect") == 0)
  {
    return sensor->status.special_effect;
  }
  else if (strcmp(name, "vflip") == 0)
  {
    return sensor->status.vflip;
  }
  else if (strcmp(name, "wb_mode") == 0)
  {
    return sensor->status.wb_mode;
  }
  else if (strcmp(name, "wpc") == 0)
  {
    return sensor->status.wpc;
  }
  else
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CAM camwebsrv_camera_get(\"%s\"): failed; invalid parameter", name);
    return -1;
  }

  return -1;
}

bool camwebsrv_camera_is_ov3660()
{
  return _camwebsrv_camera_ov3660;
}
