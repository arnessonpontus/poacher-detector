/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "FtpClient.h"
#include "esp_wifi_handler.h"
#include "esp_log.h"
#include "main_functions.h"
#include "esp_sleep_handler.h"

#define CAMERA_PIXEL_FORMAT PIXFORMAT_JPEG
#define CAMERA_FRAME_SIZE FRAMESIZE_SVGA
#define XCLK_FREQ 20000000

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

static const char *TAG = "MAIN";

uint32_t filename_number = 0;
static NetBuf_t *ftpClientNetBuf = NULL;
FtpClient *ftpClient;

void send_to_ftp(uint8_t *img, size_t len)
{
  // char local_filename[0x100];
  // snprintf(local_filename, sizeof(local_filename), "/sdcard/esp/%04d.jpg", filename_number);
  // timeit("Save to sd card", save_to_sdcard(jpeg, len, local_filename));

  char remote_filename[0x100];
  snprintf(remote_filename, sizeof(remote_filename), "image%04d.jpg", filename_number);

  NetBuf_t *nData;
  int connection_response = ftpClient->ftpClientAccess(remote_filename, FTP_CLIENT_FILE_WRITE, FTP_CLIENT_BINARY, ftpClientNetBuf, &nData);
  if (!connection_response)
  {
    ESP_LOGI(TAG, "Could not send file to FTP, reconnecting to FTP...");

    int ftp_err = ftpClient->ftpClientConnect(FTP_HOST, 21, &ftpClientNetBuf);
    ftpClient->ftpClientLogin(FTP_USER, FTP_PASSWORD, ftpClientNetBuf);
    ftpClient->ftpClientChangeDir(FTP_DIR, ftpClientNetBuf);
    ftpClient->ftpClientAccess(remote_filename, FTP_CLIENT_FILE_WRITE, FTP_CLIENT_BINARY, ftpClientNetBuf, &nData);
  }
  int write_len = ftpClient->ftpClientWrite(img, len, nData);
  ftpClient->ftpClientClose(nData);

  if (write_len)
  {
    ESP_LOGI(TAG, "SENT TO FTP AS: %s", remote_filename);
  }
  else
  {
    ESP_LOGI(TAG, "COULD NOT WRITE DATA");
  }
}

void setup() {
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
        ESP_LOGE(TAG, "Connection failed, restarting ESP...");
        esp_restart();
    }

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
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
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = -1;  // RESET_GPIO_NUM;
    config.xclk_freq_hz = XCLK_FREQ;
    config.pixel_format = CAMERA_PIXEL_FORMAT;
    config.frame_size = CAMERA_FRAME_SIZE;
    config.jpeg_quality = 10;
    config.fb_count = 1;

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }

    sleep_handler_setup();

    setup_mf();

    ftpClient = getFtpClient();
    int ftp_err = ftpClient->ftpClientConnect(FTP_HOST, 21, &ftpClientNetBuf);

    ftpClient->ftpClientLogin(FTP_USER, FTP_PASSWORD, ftpClientNetBuf);
    ftpClient->ftpClientChangeDir(FTP_DIR, ftpClientNetBuf);

    pref_begin("poach_det", false);
    filename_number = pref_getUInt("filename_number", 0);

    ESP_LOGI(TAG, "filename_number %d", filename_number);
}

void loop() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        return;
    }

    sleep_handler_update();

    if (!get_wifi_connected()) {
        ESP_LOGI(TAG, "Wifi disconnected, restarting ESP...");
        esp_restart();
    }

    uint8_t* rgb_img = (uint8_t*) heap_caps_malloc(WIDTH * HEIGHT * NUM_CHANNELS, MALLOC_CAP_SPIRAM);
    fmt2rgb888(fb->buf, fb->len, CAMERA_PIXEL_FORMAT, rgb_img);

    downscale(rgb_img);

    uint16_t changes;
    uint16_t accumelated_x;
    uint16_t accumelated_y;

    bg_subtraction(changes, accumelated_x, accumelated_y);

    bool motion_detected = (1.0 * changes / BLOCKS) > IMAGE_DIFF_THRESHOLD;

    if (motion_detected) {
        send_to_ftp(fb->buf, fb->len);
        pref_putUInt("filename_number", ++filename_number);
    }

    update_frame();

    esp_camera_fb_return(fb);
    heap_caps_free(rgb_img);
}

extern "C" void app_main(void)
{
    setup();
    while (true) {
        loop();
    }
}
