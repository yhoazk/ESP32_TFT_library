
#include "mqtt_client.h"
#include "esp_err.h"
#include "esp_log.h"

typedef struct {
    int32_t unit;
    uint8_t frac;
} currency;


float get_btc_usd();
float get_eur_mxn();
void mqtt_register_start();