#include "helpers.h"
#include "esp_sntp.h"
#include <stdlib.h>
#include "freertos/task.h"
//-------------------------------------------------------------------
unsigned int rand_interval(unsigned int min, unsigned int max) {
    int r;
    const unsigned int range = 1 + max - min;
    const unsigned int buckets = RAND_MAX / range;
    const unsigned int limit = buckets * range;

    /* Create equal size buckets all in a row, then fire randomly towards
     * the buckets until you land in one of them. All buckets are equally
     * likely. If you land off the end of the line of buckets, try again. */
    do {
        r = rand();
    } while (r >= limit);

    return min + (r / buckets);
}

// Generate random color
//-----------------------------
color_t random_color() {
    color_t color;
    color.r  = (uint8_t)rand_interval(8,252);
    color.g  = (uint8_t)rand_interval(8,252);
    color.b  = (uint8_t)rand_interval(8,252);
    return color;
}

//---------------------
int Wait(int ms) {
    uint8_t tm = 1;
    if (ms < 0) {
        tm = 0;
        ms *= -1;
    }
    if (ms <= 50) {
        vTaskDelay(ms / portTICK_RATE_MS);
    } else {
        for (int n=0; n<ms; n += 50) {
            vTaskDelay(50 / portTICK_RATE_MS);
            // if (tm) _checkTime();
        }
    }
    return 1;
}

uint32_t fsm_calc_next(uint32_t current_state) {
    static struct tm* tm_info;
    static time_t time_now, time_last = 0;
    time(&time_now);
    time_last = time_now;
    tm_info = localtime(&time_now);
    uint32_t next_state;
    int this_min = tm_info->tm_min;
    static int last_min = 0;
    printf("Current State: %d  at min: %d", current_state, this_min);
    switch (current_state) {
    case STATE_TFT_OFF:
        if ((this_min % 10) == 0 ||(this_min % 10) == 5 ) {
            next_state = TRANSITION_OFF_TO_ON;
        } else {
            next_state = STATE_TFT_OFF;
        }
        break;
    case TRANSITION_OFF_TO_ON:
        last_min = this_min;
        next_state = STATE_TFT_ON;
        break;
    case TRANSITION_ON_TO_OFF:
        next_state = STATE_TFT_OFF;
        break;
    default:
    case STATE_TFT_ON:
        // Initial state
        if (last_min == 0) {
            last_min = this_min;
        }
        if (tm_info->tm_min > last_min) {
            next_state = TRANSITION_ON_TO_OFF;
        } else {
            next_state = STATE_TFT_ON;
        }
        break;
    }
    printf(" last min: %d Next state: %d\n", last_min, next_state);
    return next_state;
}