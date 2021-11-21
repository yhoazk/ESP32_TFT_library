#include "helpers.h"
#include <stdlib.h>
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