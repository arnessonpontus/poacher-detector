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
#include "../image_provider.h"
#include "../detection_responder.h"
#include "../constants.h"
#include "../secrets.h"
#include "../FtpClient.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT	   BIT1

static const char *TAG = "MAIN";

uint32_t filename_number = 0;

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
uint8_t *resized_img;
uint8_t detection_counter = 0;
unsigned long last_detection_time;
struct timeval current_time;
static NetBuf_t* ftpClientNetBuf = NULL;
FtpClient* ftpClient;
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
bool run_tf = false;

SemaphoreHandle_t mux;

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

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG,"Disconnected from wifi, restarting ESP...");
    esp_restart();
		// if (s_retry_num < 3) {
		// 	esp_wifi_connect();
		// 	s_retry_num++;
		// 	ESP_LOGI(TAG, "retry to connect to the AP");
		// } else {
		// 	xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		// }
		// ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		// s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
} 

esp_err_t wifi_init_sta()
{
	esp_err_t ret_value = ESP_OK;
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

  wifi_config_t wifi_config = {};
  strcpy((char*)wifi_config.sta.ssid, (const char*)CONFIG_ESP_WIFI_SSID);
  strcpy((char*)wifi_config.sta.password, (const char*)CONFIG_ESP_WIFI_PASSWORD);

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,			// xClearOnExit
		pdFALSE,			// xWaitForAllBits
		portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
			 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGE(TAG, "Failed to connect to SSID:%s, password:%s",
			 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
		ret_value = ESP_FAIL;
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
		ret_value = ESP_ERR_INVALID_STATE;
	}

	ESP_LOGI(TAG, "wifi_init_sta finished.");
	ESP_LOGI(TAG, "connect to ap SSID:%s", CONFIG_ESP_WIFI_SSID);
	vEventGroupDelete(s_wifi_event_group); 
	return ret_value; 
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
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	if (wifi_init_sta() != ESP_OK) {
		ESP_LOGE(TAG, "Connection failed");
		while(1) { vTaskDelay(1); }
	}
  
  setup_mf();

  /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */

  websocket_app_start();

  ftpClient = getFtpClient();
  int ftp_err = ftpClient->ftpClientConnect(FTP_HOST, 21, &ftpClientNetBuf);

  ftpClient->ftpClientLogin(FTP_USER, FTP_PASSWORD, ftpClientNetBuf);
  ftpClient->ftpClientChangeDir("/thesis/office", ftpClientNetBuf);

  pref_begin("poach_det", false);
  filename_number = pref_getUInt("filename_number", 0);

  ESP_LOGI(TAG, "filename_number %d", filename_number);

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

void pre_process_loop() {
  uint8_t *input_image = (uint8_t *)heap_caps_malloc(WIDTH * HEIGHT * NUM_CHANNELS, MALLOC_CAP_SPIRAM);
  uint8_t *cropped_image = (uint8_t *)heap_caps_malloc(HEIGHT * HEIGHT * NUM_CHANNELS, MALLOC_CAP_SPIRAM);

  // Get image from provider.
  if (kTfLiteOk != GetImage(error_reporter, kNumCols, kNumRows, kNumChannels,
                            input_image))
  {
    TF_LITE_REPORT_ERROR(error_reporter, "Image capture failed.");
  }

  // Update downsampled current_frame
  downscale(input_image);

  uint16_t changes;
  uint16_t accumelated_x;
  uint16_t accumelated_y;

  bg_subtraction(changes, accumelated_x, accumelated_y);

  run_tf = false;

  bool motion_detected = (1.0 * changes / BLOCKS) > IMAGE_DIFF_THRESHOLD;

  int cropped_height;

  if (motion_detected)
  {
    ESP_LOGI(TAG, "Motion detected");

    // Allocate enough for maximum crop (Height x Height)
    uint32_t cropped_len = 0;
    crop_image(input_image, cropped_image, changes, cropped_len, accumelated_x, accumelated_y);

    cropped_height = sqrt(cropped_len);

    if(xSemaphoreTake(mux, portMAX_DELAY) == pdTRUE) {
      image_resize_linear(resized_img, cropped_image, MODEL_INPUT_W, MODEL_INPUT_H, NUM_CHANNELS, cropped_height, cropped_height);
      run_tf = true;
      xSemaphoreGive(mux);
    }

    // uint8_t *jpeg;
    // size_t len;

    // fmt2jpg(resized_img, MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MODEL_INPUT_W, MODEL_INPUT_H, PIXFORMAT_GRAYSCALE, 100, &jpeg, &len);

    // if (esp_websocket_client_is_connected(client))
    // {
    //   esp_websocket_client_send(client, (const char *)jpeg, len, portMAX_DELAY);
    // }

    // heap_caps_free(jpeg);
  }

  // Set prev_frame values to current_frame values
  update_frame();

  vTaskDelay(200 / portTICK_PERIOD_MS);

  heap_caps_free(input_image);
  heap_caps_free(cropped_image);
}

void handle_detection(uint8_t* resized_img_copy) {
  gettimeofday(&current_time, NULL);

  if ((unsigned long) current_time.tv_sec - last_detection_time > 6){
    detection_counter = 0;
  }
  detection_counter++;

  uint8_t *jpeg;
  size_t len;
  fmt2jpg(resized_img_copy, MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MODEL_INPUT_W, MODEL_INPUT_H, PIXFORMAT_GRAYSCALE, 100, &jpeg, &len);

  // char local_filename[0x100];
  // snprintf(local_filename, sizeof(local_filename), "/sdcard/esp/%04d.jpg", filename_number);
  // timeit("Save to sd card", save_to_sdcard(jpeg, len, local_filename));
  
  if (detection_counter == 3) {
    char remote_filename[0x100];
    snprintf(remote_filename, sizeof(remote_filename), "image%04d.jpg", filename_number);

    NetBuf_t* nData;
    int connection_response = ftpClient->ftpClientAccess(remote_filename, FTP_CLIENT_FILE_WRITE, FTP_CLIENT_BINARY, ftpClientNetBuf, &nData);
    if (!connection_response) {
      ESP_LOGI(TAG, "Could not send file to FTP, reconnecting to FTP...");

      int ftp_err = ftpClient->ftpClientConnect(FTP_HOST, 21, &ftpClientNetBuf);
      ftpClient->ftpClientLogin(FTP_USER, FTP_PASSWORD, ftpClientNetBuf);
      ftpClient->ftpClientChangeDir("/thesis/office", ftpClientNetBuf);
      ftpClient->ftpClientAccess(remote_filename, FTP_CLIENT_FILE_WRITE, FTP_CLIENT_BINARY, ftpClientNetBuf, &nData);
    }
    int write_len = ftpClient->ftpClientWrite(jpeg, len, nData);
    ftpClient->ftpClientClose(nData);

    if (write_len) {
      ESP_LOGI(TAG, "SENT TO FTP AS: %s", remote_filename);
    } else {
      ESP_LOGI(TAG, "COULD NOT WRITE DATA");
    }
  }

  last_detection_time = (unsigned long) current_time.tv_sec;
  pref_putUInt("filename_number", ++filename_number);
  heap_caps_free(jpeg);
}

void tf_main_loop()
{
  if (run_tf == false) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    return;
  }

  uint8_t *resized_img_copy = (uint8_t *)heap_caps_malloc(MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS, MALLOC_CAP_SPIRAM);
  if(xSemaphoreTake(mux, portMAX_DELAY) == pdTRUE) {
    // Set tensorflow input
    for (int i=0; i<MODEL_INPUT_W * MODEL_INPUT_H * NUM_CHANNELS; i++) {
      input->data.int8[i] = resized_img[i] - 128;
      resized_img_copy[i] = resized_img[i];
    }

    xSemaphoreGive(mux);
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
  
int pre_process_main(int argc, char *argv[]) {
  while (true)
  {
    timeit("pre process took: ", pre_process_loop());
  }
}

int tf_main(int argc, char *argv[])
{
  while (true)
  {
    timeit("tf took: ", tf_main_loop());
  }
}

extern "C" void app_main()
{
  setup();
  mux = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore((TaskFunction_t)&pre_process_main, "pre processing", 4 * 1024, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore((TaskFunction_t)&tf_main, "tensorflow", 32 * 1024, NULL, 8, NULL, 1);

  vTaskDelete(NULL);
}
