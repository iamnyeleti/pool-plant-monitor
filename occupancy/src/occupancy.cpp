#include <Arduino.h>
#include "occupancy.h"
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   38
#define SIOD_GPIO_NUM   8
#define SIOC_GPIO_NUM   7

#define Y9_GPIO_NUM     21
#define Y8_GPIO_NUM     39
#define Y7_GPIO_NUM     40
#define Y6_GPIO_NUM     42
#define Y5_GPIO_NUM     46
#define Y4_GPIO_NUM     48
#define Y3_GPIO_NUM     47
#define Y2_GPIO_NUM     45

#define VSYNC_GPIO_NUM  17
#define HREF_GPIO_NUM   18
#define PCLK_GPIO_NUM   41

static bool cameraReady = false;

void occupancySetup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1500);
  Serial.println("Occupancy module starting...");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;

  // Your sensor doesn't support JPEG (confirmed by the error we just saw),
  // GRAYSCALE is a raw format it can actually produce, and it's genuinely
  // well suited to presence detection, no colour needed for that at all.
  config.pixel_format = PIXFORMAT_GRAYSCALE;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA; // 640x480
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED, error 0x%x\n", err);
    cameraReady = false;
    return;
  }

  cameraReady = true;
  Serial.println("Camera initialised successfully.");
}

void occupancyUpdate() {
  if (!cameraReady) return;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Frame capture FAILED.");
    return;
  }

  Serial.printf("Captured frame: %d x %d, %d bytes\n", fb->width, fb->height, fb->len);
  esp_camera_fb_return(fb);
}