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

#include "../main_functions.h"
#include "../esp_wifi_handler.h"
#include "../model_settings.h"
#include "../person_detect_model_data.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "esp_event.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../image_provider.h"
#include "../detection_responder.h"
#include "../constants.h"
#include "../secrets.h"

static const char *TAG = "EVAL_PIPELINE";

size_t num_images[] = {66, 47, 54, 27, 32, 68, 36, 72, 48, 33, 36, 47, 53, 59, 68, 81, 54, 48, 128, 128, 121};
size_t num_sequences = sizeof(num_images) / sizeof(num_images[0]);
uint16_t image_number = 0;

int tp = 0;
bool event_created = false;

// Globals, used for compatibility with Arduino-style sketches.
namespace
{
  tflite::ErrorReporter *error_reporter = nullptr;
  const tflite::Model *model = nullptr;
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *input = nullptr;

  // An area of memory to use for input, output, and intermediate arrays.
  constexpr int kTensorArenaSize = 93 * 1024 * 4;
  static uint8_t *tensor_arena = (uint8_t *)heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_SPIRAM);
} // namespace

esp_websocket_client_handle_t client;
uint8_t *resized_img;
uint8_t detection_counter = 0;
unsigned long last_detection_time = 0;
unsigned long last_event_time = -999999999;
struct timeval current_time;

bool run_tf = false;

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

  websocket_cfg.uri = CONFIG_ESP_WEBSOCKET_URI;

  ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

  client = esp_websocket_client_init(&websocket_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

  esp_websocket_client_start(client);
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

  esp_err_t ret;
  ret = nvs_flash_init();
  ESP_ERROR_CHECK(esp_netif_init());
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  if (wifi_init_sta() != ESP_OK)
  {
    ESP_LOGE(TAG, "Connection failed");
    while (1)
    {
      vTaskDelay(1);
    }
  }

  setup_mf();

  websocket_app_start();

  resized_img = (uint8_t *)heap_caps_malloc(MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MALLOC_CAP_SPIRAM);

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

  static tflite::MicroMutableOpResolver<5> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_AVERAGE_POOL_2D,
                               tflite::ops::micro::Register_AVERAGE_POOL_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());

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

void pre_process_loop(size_t sequence_number)
{
  uint8_t *input_image = (uint8_t *)heap_caps_malloc(WIDTH * HEIGHT * NUM_CHANNELS, MALLOC_CAP_SPIRAM);
  uint8_t *cropped_image = (uint8_t *)heap_caps_malloc(HEIGHT * HEIGHT * NUM_CHANNELS, MALLOC_CAP_SPIRAM);

  get_stored_image(input_image, sequence_number, image_number);

  // Update downsampled current_frame
  downscale(input_image);

  uint16_t changes;
  uint16_t accumelated_x;
  uint16_t accumelated_y;

  bg_subtraction(changes, accumelated_x, accumelated_y);

  uint8_t *jpeg;
  size_t len;

  fmt2jpg(resized_img, MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MODEL_INPUT_W, MODEL_INPUT_H, PIXFORMAT_GRAYSCALE, 100, &jpeg, &len);

  if (esp_websocket_client_is_connected(client))
  {
    esp_websocket_client_send(client, (const char *)jpeg, len, portMAX_DELAY);
  }

  heap_caps_free(jpeg);

  run_tf = false;
  bool motion_detected = (1.0 * changes / BLOCKS) > IMAGE_DIFF_THRESHOLD;
  int cropped_height;

  if (motion_detected)
  {
    //ESP_LOGI(TAG, "Motion detected");

    // Allocate enough for maximum crop (Height x Height)
    uint32_t cropped_len = 0;
    crop_image(input_image, cropped_image, changes, cropped_len, accumelated_x, accumelated_y);

    cropped_height = sqrt(cropped_len);

    image_resize_linear(resized_img, cropped_image, MODEL_INPUT_W, MODEL_INPUT_H, NUM_CHANNELS, cropped_height, cropped_height);
    run_tf = true;
  }

  // Set prev_frame values to current_frame values
  update_frame();

  vTaskDelay(200 / portTICK_PERIOD_MS);

  heap_caps_free(input_image);
  heap_caps_free(cropped_image);
}

void handle_detection(uint8_t *resized_img_copy)
{
  gettimeofday(&current_time, NULL);

  if ((unsigned long)current_time.tv_sec - last_detection_time > 10)
  {
    detection_counter = 0;
  }
  detection_counter++;

  uint8_t *jpeg;
  size_t len;
  fmt2jpg(resized_img_copy, MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MODEL_INPUT_W, MODEL_INPUT_H, PIXFORMAT_GRAYSCALE, 100, &jpeg, &len);

  if (detection_counter == 2 && (unsigned long)current_time.tv_sec - last_event_time > 120)
  {
    last_event_time = (unsigned long)current_time.tv_sec;

    event_created = true;

    ESP_LOGI(TAG, "Event created");
  }

  last_detection_time = (unsigned long)current_time.tv_sec;

  heap_caps_free(jpeg);
}

void tf_main_loop()
{
  if (run_tf == false)
  {
    return;
  }

  uint8_t *resized_img_copy = (uint8_t *)heap_caps_malloc(MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MALLOC_CAP_SPIRAM);

  // Set tensorflow input
  for (int i = 0; i < MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS; i++)
  {
    input->data.int8[i] = resized_img[i] - 128;
    resized_img_copy[i] = resized_img[i];
  }

  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke())
  {
    TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed.");
  }

  TfLiteTensor *output = interpreter->output(0);

  // Process the inference results.
  uint8_t person_score = output->data.int8[kPersonIndex] + 128;
  uint8_t no_person_score = output->data.int8[kNotAPersonIndex] + 128;
  bool human_detected = RespondToDetection(error_reporter, person_score, no_person_score);

  if (human_detected)
  {
    ESP_LOGI(TAG, "********** HUMAN detected ***********");
    handle_detection(resized_img_copy);
  }

  heap_caps_free(resized_img_copy);
}

int tf_main(int argc, char *argv[])
{
  for (size_t i = 0; i < num_sequences; i++)
  {
    ESP_LOGI(TAG, "---- STARTING SEQUENCE %d ----", i);

    event_created = false;
    for (size_t j = 0; j < num_images[i]; j++)
    {
      image_number = j;
      timeit("pre process took: ", pre_process_loop(i));
      if (image_number % 5 == 0)
      {
        timeit("tf took: ", tf_main_loop());
      }
    }

    if (event_created)
      tp++;

    last_detection_time = 0;
    last_event_time = -999999;
    detection_counter = 0;
  }

  ESP_LOGI(TAG, "True Positives: %d", tp);

  vTaskDelay(1000000 / portTICK_PERIOD_MS);

  return 0;
}

extern "C" void app_main()
{
  setup();
  xTaskCreate((TaskFunction_t)&tf_main, "tensorflow", 32 * 1024, NULL, 8, NULL);
  vTaskDelete(NULL);
}
