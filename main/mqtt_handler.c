#include "mqtt_handler.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "MQTT_EXAMPLE";

static void log_error_if_nonzero(const char * message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static float btc_usd;
char topic_usd_btc_buff[255];
char btc_usd_data_buf[255];

static float eur_mxn;
char topic_mxn_eur_buff[255];
char eur_mxn_data_buff[255];

const char* topic_usd_btc = "/curr/usd_btc";
const char* topic_mxn_eur = "/curr/mxn_eur";

float get_btc_usd() {
    return btc_usd;
}
float get_eur_mxn() {
    return eur_mxn;
}
static void update_btc_usd(esp_mqtt_event_handle_t ev) {
    if (memcmp(topic_usd_btc, ev->topic, ev->topic_len) == 0) {
        printf("is topic btc_usd\n");
        btc_usd = atof(ev->data);
        printf("Price %f", btc_usd);
    } else {
        printf("Different topic\n");
    }
}
static void update_eur_mxn(esp_mqtt_event_handle_t ev) {
    if (memcmp(topic_mxn_eur, ev->topic, ev->topic_len) == 0) {
        printf("is topic eur_mxn\n");
        eur_mxn = atof(ev->data);
    } else {
        printf("Different topic\n");
    }
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            msg_id = esp_mqtt_client_subscribe(client, topic_usd_btc, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            msg_id = esp_mqtt_client_subscribe(client, topic_mxn_eur, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            msg_id = esp_mqtt_client_publish(client, "/ttgo/ack", "data", 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            update_btc_usd(event);
            update_eur_mxn(event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

esp_mqtt_client_config_t mqtt_cfg = {
    .uri = CONFIG_BROKER_URL,
};


void mqtt_register_start() {
    ESP_LOGI(TAG, "Connection to:");
    ESP_LOGI(TAG, CONFIG_BROKER_URL);
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}