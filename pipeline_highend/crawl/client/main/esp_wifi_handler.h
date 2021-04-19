#ifndef ESP_WIFI_HANDLER_
#define ESP_WIFI_HANDLER_

#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include <string.h>
#include "constants.h"
#include "secrets.h"

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data);

esp_err_t wifi_init_sta();

#endif  // ESP_WIFI_HANDLER_