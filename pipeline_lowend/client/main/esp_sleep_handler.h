#ifndef ESP_SLEEP_HANDLER_
#define ESP_SLEEP_HANDLER_

#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "constants.h"

void sleep_handler_setup();
void sleep_handler_update();

#endif // ESP_SLEEP_HANDLER_
