#include "esp_wifi_handler.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "ESP_WIFI";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

bool wifi_connected = false;

bool get_wifi_connected() {
	return wifi_connected;
}

static void event_handler(void *arg, esp_event_base_t event_base,
						  int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		esp_wifi_connect();
	}
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		ESP_LOGI(TAG, "Disconnected from wifi...");
		wifi_connected = false;
	}
	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		// s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
		wifi_connected = true;
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
	strcpy((char *)wifi_config.sta.ssid, (const char *)CONFIG_ESP_WIFI_SSID);
	strcpy((char *)wifi_config.sta.password, (const char *)CONFIG_ESP_WIFI_PASSWORD);

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
										   WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
										   pdFALSE, // xClearOnExit
										   pdFALSE, // xWaitForAllBits
										   4000 / portTICK_PERIOD_MS);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT)
	{
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
				 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	}
	else if (bits & WIFI_FAIL_BIT)
	{
		ESP_LOGE(TAG, "Failed to connect to SSID:%s, password:%s",
				 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
		ret_value = ESP_FAIL;
	}
	else
	{
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
		ret_value = ESP_ERR_INVALID_STATE;
	}

	ESP_LOGI(TAG, "wifi_init_sta finished.");
	ESP_LOGI(TAG, "connect to ap SSID:%s", CONFIG_ESP_WIFI_SSID);
	vEventGroupDelete(s_wifi_event_group);
	return ret_value;
}