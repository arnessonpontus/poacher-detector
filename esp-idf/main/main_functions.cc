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

#include "main_functions.h"
#include "image_util.h"
#include "detection_responder.h"
#include "image_provider.h"
#include "model_settings.h"
#include "person_detect_model_data.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

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

#ifdef CONFIG_IDF_TARGET_ESP32
#include "driver/sdmmc_host.h"
#endif

#define MOUNT_POINT "/sdcard"

// DMA channel to be used by the SPI peripheral
#ifndef SPI_DMA_CHAN
#define SPI_DMA_CHAN 1
#endif //SPI_DMA_CHAN

#define MODEL_INPUT_W 96
#define MODEL_INPUT_H 96

#define WIDTH 320
#define HEIGHT 240
#define BLOCK_SIZE 10
#define W (WIDTH / BLOCK_SIZE)
#define H (HEIGHT / BLOCK_SIZE)
#define BLOCK_DIFF_THRESHOLD 0.25
#define IMAGE_DIFF_THRESHOLD 0.01

#define MAXIMUM(x, y) (((x) > (y)) ? (x) : (y))
#define MINIMUM(x, y) (((x) < (y)) ? (x) : (y))

const char *nvs_errors[] = {"OTHER", "NOT_INITIALIZED", "NOT_FOUND", "TYPE_MISMATCH", "READ_ONLY", "NOT_ENOUGH_SPACE", "INVALID_NAME", "INVALID_HANDLE", "REMOVE_FAILED", "KEY_TOO_LONG", "PAGE_FULL", "INVALID_STATE", "INVALID_LENGTH"};
#define nvs_error(e) (((e) > ESP_ERR_NVS_BASE) ? nvs_errors[(e) & ~(ESP_ERR_NVS_BASE)] : nvs_errors[0])

// Globals, used for compatibility with Arduino-style sketches.
namespace
{
  tflite::ErrorReporter *error_reporter = nullptr;
  const tflite::Model *model = nullptr;
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *input = nullptr;

  // An area of memory to use for input, output, and intermediate arrays.
  constexpr int kTensorArenaSize = 93 * 1024;
  static uint8_t tensor_arena[kTensorArenaSize];
} // namespace

static const char *TAG = "MAIN_FUNCTIONS";
esp_websocket_client_handle_t client;
unsigned int pictureNumber = 0;
uint16_t prev_frame[H][W] = { 0 };
uint16_t current_frame[H][W] = { 0 };

bool motion_detect(uint8_t* original_image, uint8_t* resized_img) {
    uint16_t changes = 0;
    const uint16_t blocks = (WIDTH * HEIGHT) / (BLOCK_SIZE * BLOCK_SIZE);
    
    uint16_t accumelated_x = 0;
    uint16_t accumelated_y = 0;
    float mean_x = 0;
    float mean_y = 0;
    uint16_t diff_sum_x = 0;
    uint16_t diff_sum_y = 0;
    float variance_x = 0;
    float variance_y = 0;
    uint16_t pixel_counter = 0;
    
    uint8_t *bg_image = (uint8_t *)heap_caps_malloc(W*H, MALLOC_CAP_SPIRAM);
    uint8_t* cropped_img = (uint8_t*) heap_caps_malloc(WIDTH*HEIGHT, MALLOC_CAP_SPIRAM);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t i = x + y * W;
            float current = current_frame[y][x];
            float prev = prev_frame[y][x];
            float delta = abs(current - prev) / prev;

            if (delta >= BLOCK_DIFF_THRESHOLD) {
                changes += 1;
                bg_image[i] = 0;
                pixel_counter++;
                accumelated_x += i % W;
                accumelated_y += floor(i / W);
            } else {
                bg_image[x + y * W] = 255;
            }
        }
    }
    
    if (pixel_counter < 5) {
      heap_caps_free(bg_image);
      heap_caps_free(cropped_img);
      return false;
    }
    
    mean_x = (float) accumelated_x/pixel_counter;
    mean_y = (float) accumelated_y/pixel_counter;
    
    for (int j = 0; j < W * H; j++) {
      if (bg_image[j] == 0) {
        diff_sum_x += abs(j % 32 - mean_x);
        diff_sum_y += abs(floor(j / 32) - mean_y);
      }
    }

    variance_x = ((float) diff_sum_x / pixel_counter) * 2.5;
    variance_y = ((float) diff_sum_y / pixel_counter) * 2.5;

    float half_width = MAXIMUM(variance_x, variance_y) * 1.2;

    // Mult by 10 to get pixel in original img
    mean_x *= 10;
    mean_y *= 10;
    half_width *= 10;
    half_width = MINIMUM(half_width, (float) (HEIGHT / 2));

    float p1_x = (mean_x - half_width);
    float p1_y = (mean_y - half_width);
    
    float p2_x = (mean_x + half_width);
    float p2_y = (mean_y + half_width);

    // Shift if square is outside image border
    if (p1_x < 0) {
      p1_x = 0;
      p2_x = 2 * half_width;
    }
    
    if (p1_y < 0) {
       p1_y = 0;
       p2_y = 2 * half_width;
    }

    if (p2_x > WIDTH) {
       p2_x = WIDTH;
       p1_x = WIDTH - 2 * half_width;
    }

    if (p2_y > HEIGHT) {
      p2_y = HEIGHT;
      p1_y = HEIGHT - 2 * half_width;
    }

    int i_p1_x = (int) p1_x;
    int i_p1_y = (int) p1_y;
    
    int i_p2_x = (int) p2_x;
    int i_p2_y = (int) p2_y;

    if (i_p2_x - i_p1_x > i_p2_y - i_p1_y) {
      i_p2_y++;
    } else if (i_p2_x - i_p1_x < i_p2_y - i_p1_y) {
      i_p2_x++;
    }

    uint16_t counter = 0;
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
      uint16_t x = i % WIDTH;
      uint16_t y = floor(i / WIDTH);
   
      if (x >= i_p1_x && x < i_p2_x && y >= i_p1_y && y < i_p2_y) {
        cropped_img[counter] = original_image[i];
        counter++;
      }
    }
    
    // uint8_t* resized_img = (uint8_t*) heap_caps_malloc(96 * 96, MALLOC_CAP_SPIRAM);
    
    image_resize_linear(resized_img, cropped_img, 96, 96, 1, sqrt(counter), sqrt(counter));

    uint8_t* jpeg;
    size_t len;
    fmt2jpg(bg_image, W * H, W, H, PIXFORMAT_GRAYSCALE, 100, &jpeg, &len);

    // if (esp_websocket_client_is_connected(client))
    // {
    //   esp_websocket_client_send(client, (const char *)jpeg, len, portMAX_DELAY);
    // }
 
    heap_caps_free(jpeg);
    heap_caps_free(cropped_img);
    heap_caps_free(bg_image);
    ESP_LOGI(TAG, "Changed %d", changes);
    ESP_LOGI(TAG, "out of %d blocks", blocks);
    return (1.0 * changes / blocks) > IMAGE_DIFF_THRESHOLD;
}

void update_frame() {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            prev_frame[y][x] = current_frame[y][x];
        }
    }
}

bool downscale(uint8_t* image) {
    // set all 0s in current frame
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            current_frame[y][x] = 0;


    // down-sample image in blocks
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) {
        const uint16_t x = i % WIDTH;
        const uint16_t y = floor(i / WIDTH);
        const uint8_t block_x = floor(x / BLOCK_SIZE);
        const uint8_t block_y = floor(y / BLOCK_SIZE);
        const uint8_t pixel = image[i];
        const uint16_t current = current_frame[block_y][block_x];

        // average pixels in block (accumulate)
        current_frame[block_y][block_x] += pixel;
    }

    // average pixels in block (rescale)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            current_frame[y][x] /= BLOCK_SIZE * BLOCK_SIZE;

    return true;
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
  switch (event_id)
  {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
    break;
  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
    break;
  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
    break;
  }
}

static void websocket_app_start(void)
{
  esp_websocket_client_config_t websocket_cfg = {};

  websocket_cfg.uri = CONFIG_WEBSOCKET_URI;

  ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

  client = esp_websocket_client_init(&websocket_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

  esp_websocket_client_start(client);
}

void setup_sdcard()
{
  esp_err_t ret;
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024};
  sdmmc_card_t *card;
  const char mount_point[] = MOUNT_POINT;
  ESP_LOGI(TAG, "Initializing SD card");

  ESP_LOGI(TAG, "Using SDMMC peripheral");
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();

  // This initializes the slot without card detect (CD) and write protect (WP) signals.
  // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

  // To use 1-line SD mode, uncomment the following line:
  slot_config.width = 1;

  gpio_set_pull_mode((gpio_num_t)15, GPIO_PULLUP_ONLY); // CMD, needed in 4- and 1- line modes
  gpio_set_pull_mode((gpio_num_t)2, GPIO_PULLUP_ONLY);  // D0, needed in 4- and 1-line modes
  //gpio_set_pull_mode((gpio_num_t)4, GPIO_PULLUP_ONLY);  // D1, needed in 4-line mode only
  //gpio_set_pull_mode((gpio_num_t)12, GPIO_PULLUP_ONLY); // D2, needed in 4-line mode only
  gpio_set_pull_mode((gpio_num_t)13, GPIO_PULLUP_ONLY); // D3, needed in 4- and 1-line modes

  ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

  if (ret != ESP_OK)
  {
    if (ret == ESP_FAIL)
    {
      ESP_LOGE(TAG, "Failed to mount filesystem. "
                    "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
    }
    else
    {
      ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                    "Make sure SD card lines have pull-up resistors in place.",
               esp_err_to_name(ret));
    }
    return;
  }

  // Card has been initialized, print its properties
  sdmmc_card_print_info(stdout, card);
}

void setup()
{
  // Setup wifi and ws
  ESP_LOGI(TAG, "[APP] Startup..");
  ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
  ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("WEBSOCKET_CLIENT", ESP_LOG_DEBUG);
  esp_log_level_set("TRANSPORT_WS", ESP_LOG_DEBUG);
  esp_log_level_set("TRANS_TCP", ESP_LOG_DEBUG);

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  pref_begin("poach_det", false);
  pictureNumber = pref_getUInt("camera_counter", 0);

  setup_sdcard();

  /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
  ESP_ERROR_CHECK(example_connect());

  websocket_app_start();

  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION)
  {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Model provided is schema version %d not equal "
                         "to supported version %d.",
                         model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  static tflite::MicroMutableOpResolver<3> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_AVERAGE_POOL_2D,
                               tflite::ops::micro::Register_AVERAGE_POOL_2D());

  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk)
  {
    TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
    return;
  }

  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);
}

// The name of this function is important for Arduino compatibility.
void loop()
{
  uint8_t* input_image = (uint8_t*) heap_caps_malloc(WIDTH * HEIGHT, MALLOC_CAP_SPIRAM);

  // Get image from provider.
  if (kTfLiteOk != GetImage(error_reporter, kNumCols, kNumRows, kNumChannels,
                            input_image))
  {
    TF_LITE_REPORT_ERROR(error_reporter, "Image capture failed.");
  }
  input->data.uint8 = input_image;

  downscale(input->data.uint8);

  uint8_t* jpeg;
  size_t len;
  uint8_t* resized_img = (uint8_t*) heap_caps_malloc(96*96, MALLOC_CAP_SPIRAM);
  if (motion_detect(input->data.uint8, resized_img)) {
      ESP_LOGI(TAG, "Motion detected");
      fmt2jpg(resized_img, 96 * 96, 96, 96, PIXFORMAT_GRAYSCALE, 100, &jpeg, &len);

      if (esp_websocket_client_is_connected(client))
      {
        esp_websocket_client_send(client, (const char *)jpeg, len, portMAX_DELAY);
      }
  }

  update_frame();

  /*
  // Copy because invoke changes the input. Uses normal malloc since heap_caps_malloc gives NULL
  uint8_t *temp_input = (uint8_t *)heap_caps_malloc(MODEL_INPUT_W * MODEL_INPUT_H, MALLOC_CAP_SPIRAM);
  if (temp_input == NULL)
    ESP_LOGI(TAG, "NULL TEMP_INPUT");
  memcpy(temp_input, input->data.uint8, MODEL_INPUT_W * MODEL_INPUT_H);

  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke())
  {
    TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed.");
  }

  TfLiteTensor *output = interpreter->output(0);

  // Process the inference results.
  uint8_t person_score = output->data.uint8[kPersonIndex];
  uint8_t no_person_score = output->data.uint8[kNotAPersonIndex];
  bool human_detected = RespondToDetection(error_reporter, person_score, no_person_score);

  if (human_detected)
  {
    uint8_t *jpeg;
    size_t len;

    fmt2jpg(temp_input, MODEL_INPUT_W * MODEL_INPUT_H, MODEL_INPUT_W, MODEL_INPUT_H, PIXFORMAT_GRAYSCALE, 100, &jpeg, &len);

    if (esp_websocket_client_is_connected(client))
    {
      esp_websocket_client_send(client, (const char *)jpeg, len, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "Opening file");
    char buf[0x100];
    snprintf(buf, sizeof(buf), "/sdcard/esp/%d.jpg", pictureNumber);
    FILE *f = fopen(buf, "w");
    if (f == NULL)
    {
      ESP_LOGE(TAG, "Failed to open file for writing");
      return;
    }
    fwrite((const char *)jpeg, 1, len, f);
    fflush(f);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    pref_putUInt("camera_counter", ++pictureNumber);

    heap_caps_free(jpeg);
    
  }
  
  heap_caps_free(temp_input);
  */
 heap_caps_free(jpeg);
 heap_caps_free(input_image);
 //heap_caps_free(resized_img);
}
