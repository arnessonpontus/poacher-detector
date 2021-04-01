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

#include "../model_settings.h"
#include "../person_detect_model_data.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "esp_websocket_client.h"
#include "../esp_wifi_handler.h"
#include "../detection_responder.h"
#include "../constants.h"
#include "../secrets.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../stb_image_resize.h"

static const char *TAG = "EVAL";

uint16_t num_images = 221;
uint16_t image_number = 0;
uint16_t offset_number = 0;
uint16_t tp = 0;
uint16_t tn = 0;
uint16_t fp = 0;
uint16_t fn = 0;
uint8_t ground_truth[] = {1,1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1,1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1,1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1,1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1,1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1};

// Globals, used for compatibility with Arduino-style sketches.
namespace
{
  tflite::ErrorReporter *error_reporter = nullptr;
  const tflite::Model *model = nullptr;
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *input = nullptr;

  // An area of memory to use for input, output, and intermediate arrays.
  constexpr int kTensorArenaSize = 93 * 1024 * 4;
  static uint8_t* tensor_arena = (uint8_t*) heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_SPIRAM);
} // namespace

esp_websocket_client_handle_t client;

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

void loop()
{
  if (image_number == num_images) {
    ESP_LOGI(TAG, "True Positives: %d", tp);
    ESP_LOGI(TAG, "True Negatives: %d", tn);
    ESP_LOGI(TAG, "False Positives: %d", fp);
    ESP_LOGI(TAG, "False Negatives: %d", fn);
    image_number++;
    return;
  } else if (image_number > num_images) {
    return;
  }

  uint8_t *input_image = (uint8_t *)heap_caps_malloc(WIDTH * HEIGHT, MALLOC_CAP_SPIRAM);

  get_stored_image(input_image, image_number + offset_number);

  // Update downsampled current_frame
  downscale(input_image);

  uint8_t *resized_img = (uint8_t *)heap_caps_malloc(MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MALLOC_CAP_SPIRAM);
  uint8_t *cropped_image = (uint8_t *)heap_caps_malloc(HEIGHT * HEIGHT * NUM_CHANNELS, MALLOC_CAP_SPIRAM);

  uint16_t changes;
  uint16_t accumelated_x;
  uint16_t accumelated_y;

  bg_subtraction(changes, accumelated_x, accumelated_y);

  bool motion_detected = (1.0 * changes / BLOCKS) > IMAGE_DIFF_THRESHOLD;

  if (motion_detected)
  {
    ESP_LOGI(TAG, "Motion detected");

    // Allocate enough for maximum crop (Height x Height)
    uint32_t cropped_len = 0;
    crop_image(input_image, cropped_image, changes, cropped_len, accumelated_x, accumelated_y);

    image_resize_linear(resized_img, cropped_image, MODEL_INPUT_W, MODEL_INPUT_H, NUM_CHANNELS, sqrt(cropped_len), sqrt(cropped_len));
  }
  else
  {
    //stbir_resize_uint8(input->data.uint8, WIDTH, HEIGHT, 0, resized_img, 96, 96, 0, 1);
    crop_image_center(input_image, cropped_image);

    image_resize_linear(resized_img, cropped_image, MODEL_INPUT_W, MODEL_INPUT_H, NUM_CHANNELS, HEIGHT, HEIGHT);
  }

  // Set prev_frame values to current_frame values
  update_frame();

  // Copy because invoke changes the input. Uses normal malloc since heap_caps_malloc gives NULL
  uint8_t *temp_input = (uint8_t *)heap_caps_malloc(MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MALLOC_CAP_SPIRAM);
  if (temp_input == NULL)
    ESP_LOGI(TAG, "NULL TEMP_INPUT");
  memcpy(temp_input, resized_img, MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS);

  // Set tensorflow input
  for (int i=0; i<MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS; i++) {
    input->data.int8[i] = resized_img[i] - 128;
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

  uint8_t *jpeg;
  size_t len;

  fmt2jpg(temp_input, MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MODEL_INPUT_W, MODEL_INPUT_H, PIXFORMAT_GRAYSCALE, 100, &jpeg, &len);

  if (esp_websocket_client_is_connected(client))
  {
    esp_websocket_client_send(client, (const char *)jpeg, len, portMAX_DELAY);
  }

  if (human_detected)
  {
    ESP_LOGI(TAG, "********** HUMAN detected ***********");
    if (ground_truth[image_number] == 1) tp++;
    else fp++;
  } else {
    if (ground_truth[image_number] == 1) fn++;
    else tn++;
  }

  image_number++;

  heap_caps_free(jpeg);
  heap_caps_free(temp_input);
  heap_caps_free(input_image);
  heap_caps_free(resized_img);
  heap_caps_free(cropped_image);
}

int tf_main(int argc, char *argv[])
{
  setup();
  while (true)
  {
    loop();
  }
}

extern "C" void app_main()
{
  xTaskCreate((TaskFunction_t)&tf_main, "tensorflow", 32 * 1024, NULL, 8, NULL);
  vTaskDelete(NULL);
}