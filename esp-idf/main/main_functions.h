/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#ifndef TENSORFLOW_LITE_MICRO_EXAMPLES_PERSON_DETECTION_MAIN_FUNCTIONS_H_
#define TENSORFLOW_LITE_MICRO_EXAMPLES_PERSON_DETECTION_MAIN_FUNCTIONS_H_

#include <sys/time.h>
#include "image_util.h"
// #define STB_IMAGE_RESIZE_IMPLEMENTATION
// #include "stb_image_resize.h"
// #define STBIR_ASSERT()
#include "image_provider.h"

#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "img_converters.h"

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "preferences.h"
#include <errno.h>

#ifdef CONFIG_IDF_TARGET_ESP32
#include "driver/sdmmc_host.h"
#endif

#define timeit(label, code)                                                                                            \
{                                                                                                                      \
  struct timeval stop, start;                                                                                          \
  gettimeofday(&start, NULL);                                                                                          \
  code;                                                                                                                \
  gettimeofday(&stop, NULL);                                                                                           \
  ESP_LOGI(TAG, "%s took %f us", label, (float)(stop.tv_sec - start.tv_sec) * 1000000 + stop.tv_usec - start.tv_usec); \
}
//#define timeit(label, code) code;

// Expose a C friendly interface for main functions.
#ifdef __cplusplus
extern "C" {
#endif

// Initializes all data needed for the example. The name is important, and needs
// to be setup() for Arduino compatibility.
void setup();


// Runs one iteration of data gathering and inference. This should be called
// repeatedly from the application code. The name needs to be loop() for Arduino
// compatibility.
void loop();

void crop_image_center(uint8_t *src, uint8_t *dst);
void crop_image(uint8_t *src, uint8_t *dst, uint16_t changes, uint32_t &cropped_len, uint16_t &accumelated_x, uint16_t &accumelated_y);
void bg_subtraction(uint16_t &changes, uint16_t &accumelated_x, uint16_t &accumelated_y);
void update_frame();
bool downscale(uint8_t *image);
void setup_sdcard();
void save_to_sdcard(uint8_t *image, size_t len);
void get_stored_image(uint8_t* input_image);
void setup_mf();

#ifdef __cplusplus
}
#endif

#endif  // TENSORFLOW_LITE_MICRO_EXAMPLES_PERSON_DETECTION_MAIN_FUNCTIONS_H_
