#ifndef _WIFI_SETUP_
#define _WIFI_SETUP_

#include "esp_log.h"
#include "esp_err.h"
#include <time.h>
#include "dirent.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "nvs_flash/include/nvs_flash.h"
#include <stdlib.h>

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
void initialise_wifi();
void initialize_sntp();
int obtain_time();
#endif //  _WIFI_SETUP_