/* TFT demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tftspi.h"
#include "tft.h"
#include "esp_spiffs.h"
#include "dirent.h"
#include "mqtt_handler.h"
#include "helpers.h"
#include "wifi_setup.h"

// ==========================================================
// Define which spi bus to use VSPI_HOST or HSPI_HOST
#define SPI_BUS HSPI_HOST
// ==========================================================


static const char tag[] = "[TFT Demo]";
static int _demo_pass = 0;
static uint8_t doprint = 1;
static uint8_t run_gs_demo = 0; // Run gray scale demo if set to 1
static struct tm* tm_info;
static char tmp_buff[64];
static time_t time_now, time_last = 0;
static const char *file_fonts[3] = {"/spiffs/fonts/DotMatrix_M.fon", "/spiffs/fonts/Ubuntu.fon", "/spiffs/fonts/Grotesk24x48.fon"};
static int spiffs_is_mounted = 0;
#define SPIFFS_BASE_PATH "/spiffs"

#define GDEMO_TIME 1000
#define GDEMO_INFO_TIME 5000

//==================================================================================

/* Selects font size depending on the width of the sring */
static void set_font () {
    if (tft_width < 240) {
        TFT_setFont(DEF_SMALL_FONT, NULL);
    } else  {
        TFT_setFont(DEJAVU24_FONT, NULL);
    }
}

//----------------------
static void _checkTime()
{
    time(&time_now);
    if (time_now > time_last) {
        color_t last_fg, last_bg;
        time_last = time_now;
        tm_info = localtime(&time_now);
        sprintf(tmp_buff, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

        TFT_saveClipWin();
        TFT_resetclipwin();

        Font curr_font = tft_cfont;
        last_bg = tft_bg;
        last_fg = tft_fg;
        tft_fg = TFT_YELLOW;
        tft_bg = (color_t){ 64, 64, 64 };
        set_font();

        TFT_fillRect(1, tft_height-TFT_getfontheight()-8, tft_width-3, TFT_getfontheight()+6, tft_bg);
        TFT_print(tmp_buff, CENTER, tft_height-TFT_getfontheight()-5);

        tft_cfont = curr_font;
        tft_fg = last_fg;
        tft_bg = last_bg;

        TFT_restoreClipWin();
    }
}

//---------------------
static void _dispTime() {
    Font curr_font = tft_cfont;
    set_font();

    time(&time_now);
    time_last = time_now;
    tm_info = localtime(&time_now);
    sprintf(tmp_buff, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    TFT_print(tmp_buff, CENTER, tft_height-TFT_getfontheight()-5);

    tft_cfont = curr_font;
}


//---------------------------------
static void disp_header(char *info) {
    TFT_fillScreen(TFT_BLACK);
    TFT_resetclipwin();

    tft_fg = TFT_DARKGREY;
    tft_bg = TFT_BLACK; // (color_t){ 64, 64, 64 };

    set_font();
    TFT_fillRect(0, 0, tft_width-1, TFT_getfontheight()+8, tft_bg);
    TFT_drawRect(0, 0, tft_width-1, TFT_getfontheight()+8, TFT_BLACK);

    TFT_fillRect(0, tft_height-TFT_getfontheight()-9, tft_width-1, TFT_getfontheight()+8, tft_bg);
    TFT_drawRect(0, tft_height-TFT_getfontheight()-9, tft_width-1, TFT_getfontheight()+8, TFT_BLACK);

    TFT_print(info, CENTER, 4);
    _dispTime();

    tft_bg = TFT_BLACK;
    TFT_setclipwin(0,TFT_getfontheight()+9, tft_width-1, tft_height-TFT_getfontheight()-10);
}

static void btc_usd_demo() {
    printf("Demo: %s\r\n", __func__);
    disp_header("USD/BTC");
    TFT_setFont(FONT_7SEG, NULL);
    set_7seg_font_atrib(23, 2, 1, TFT_YELLOW);
    char msg[30];
    memset(msg, 0, 30);
    float usd_btc = get_btc_usd();
    printf("USD/BTC: %f\n", usd_btc);
    sprintf(msg, "%.2f", usd_btc);
    TFT_print(msg, 0, 2);
    Wait(-GDEMO_INFO_TIME);
}

static void eur_mxn_demo() {
    printf("Demo: %s\r\n", __func__);
    disp_header("MXN/EUR");
    TFT_setFont(FONT_7SEG, NULL);
    set_7seg_font_atrib(23, 2, 1, TFT_BLUE);
    char msg[30];
    memset(msg, 0, 30);
    float mxn_eur = get_eur_mxn();
    printf("MXN/EUR: %f\n", mxn_eur);
    sprintf(msg, "%.2f", mxn_eur);
    TFT_print(msg, 0, 2);
    Wait(-GDEMO_INFO_TIME);
}

//---------------------------------------------
static void update_header(char *hdr, char *ftr) {
    color_t last_fg, last_bg;

    TFT_saveClipWin();
    TFT_resetclipwin();

    Font curr_font = tft_cfont;
    last_bg = tft_bg;
    last_fg = tft_fg;
    tft_fg = TFT_YELLOW;
    tft_bg = (color_t){ 64, 64, 64 };
    set_font();

    if (hdr) {
        TFT_fillRect(1, 1, tft_width-3, TFT_getfontheight()+6, tft_bg);
        TFT_print(hdr, RIGHT, 4);
    }

    if (ftr) {
        TFT_fillRect(1, tft_height-TFT_getfontheight()-8, tft_width-3, TFT_getfontheight()+6, tft_bg);
        if (strlen(ftr) == 0) _dispTime();
        else TFT_print(ftr, RIGHT, tft_height-TFT_getfontheight()-5);
    }

    tft_cfont = curr_font;
    tft_fg = last_fg;
    tft_bg = last_bg;

    TFT_restoreClipWin();
}

//------------------------
static void test_times() {

    printf("Demo: %s\r\n", __func__);
    if (doprint) {
        uint32_t tstart, t1, t2;
        disp_header("TIMINGS");
        // ** Show Fill screen and send_line timings
        tstart = clock();
        TFT_fillWindow(TFT_BLACK);
        t1 = clock() - tstart;
        printf("     Clear screen time: %u ms\r\n", t1);
        TFT_setFont(SMALL_FONT, NULL);
        sprintf(tmp_buff, "Clear screen: %u ms", t1);
        TFT_print(tmp_buff, 0, 140);

        color_t *color_line = heap_caps_malloc((tft_width*3), MALLOC_CAP_DMA);
        color_t *gsline = NULL;
        if (tft_gray_scale) gsline = malloc(tft_width*3);
        if (color_line) {
            float hue_inc = (float)((10.0 / (float)(tft_height-1) * 360.0));
            for (int x=0; x<tft_width; x++) {
                color_line[x] = HSBtoRGB(hue_inc, 1.0, (float)x / (float)tft_width);
                if (gsline) gsline[x] = color_line[x];
            }
            disp_select();
            tstart = clock();
            for (int n=0; n<1000; n++) {
                if (gsline) memcpy(color_line, gsline, tft_width*3);
                send_data(0 + TFT_STATIC_X_OFFSET, 40+(n&63) + TFT_STATIC_Y_OFFSET, tft_dispWin.x2-tft_dispWin.x1 + TFT_STATIC_X_OFFSET , 40+(n&63) + TFT_STATIC_Y_OFFSET, (uint32_t)(tft_dispWin.x2-tft_dispWin.x1+1), color_line);
            }
            t2 = clock() - tstart;
            disp_deselect();

            printf("Send color buffer time: %u us (%d pixels)\r\n", t2, tft_dispWin.x2-tft_dispWin.x1+1);
            free(color_line);

            sprintf(tmp_buff, "   Send line: %u us", t2);
            TFT_print(tmp_buff, 0, 144+TFT_getfontheight());
        }
        Wait(GDEMO_INFO_TIME);
    }
}

// Image demo
//-------------------------
static void disp_images() {
    uint32_t tstart;

    printf("Demo: %s\r\n", __func__);
    disp_header("JPEG IMAGES");

    if (spiffs_is_mounted) {
        // ** Show scaled (1/8, 1/4, 1/2 size) JPG images
        TFT_jpg_image(CENTER, CENTER, 3, SPIFFS_BASE_PATH"/images/test1.jpg", NULL, 0);
        Wait(500);

        TFT_jpg_image(CENTER, CENTER, 2, SPIFFS_BASE_PATH"/images/test2.jpg", NULL, 0);
        Wait(500);

        TFT_jpg_image(CENTER, CENTER, 1, SPIFFS_BASE_PATH"/images/test4.jpg", NULL, 0);
        Wait(500);

        // ** Show full size JPG image
        tstart = clock();
        TFT_jpg_image(CENTER, CENTER, 0, SPIFFS_BASE_PATH"/images/test3.jpg", NULL, 0);
        tstart = clock() - tstart;
        if (doprint) printf("       JPG Decode time: %u ms\r\n", tstart);
        sprintf(tmp_buff, "Decode time: %u ms", tstart);
        update_header(NULL, tmp_buff);
        Wait(-GDEMO_INFO_TIME);

        // ** Show BMP image
        update_header("BMP IMAGE", "");
        for (int scale=5; scale >= 0; scale--) {
            tstart = clock();
            TFT_bmp_image(CENTER, CENTER, scale, SPIFFS_BASE_PATH"/images/tiger.bmp", NULL, 0);
            tstart = clock() - tstart;
            if (doprint) printf("    BMP time, scale: %d: %u ms\r\n", scale, tstart);
            sprintf(tmp_buff, "Decode time: %u ms", tstart);
            update_header(NULL, tmp_buff);
            Wait(-500);
        }
        Wait(-GDEMO_INFO_TIME);
    }
    else if (doprint) printf("  No file system found.\r\n");
}

//---------------------
static void font_demo() {
    int x, y, n;
    uint32_t end_time;

    printf("Demo: %s\r\n", __func__);
    disp_header("FONT DEMO");

    end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        y = 4;
        for (int f=DEFAULT_FONT; f<FONT_7SEG; f++) {
            tft_fg = random_color();
            TFT_setFont(f, NULL);
            TFT_print("Welcome to ESP32", 4, y);
            y += TFT_getfontheight() + 4;
            n++;
        }
    }
    sprintf(tmp_buff, "%d STRINGS", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    if (spiffs_is_mounted) {
        disp_header("FONT FROM FILE DEMO");

        tft_text_wrap = 1;
        for (int f=0; f<3; f++) {
            TFT_fillWindow(TFT_BLACK);
            update_header(NULL, "");

            TFT_setFont(USER_FONT, file_fonts[f]);
            if (f == 0) tft_font_line_space = 4;
            end_time = clock() + GDEMO_TIME;
            n = 0;
            while ((clock() < end_time) && (Wait(0))) {
                tft_fg = random_color();
                TFT_print("Welcome to ESP32\nThis is user font.", 0, 8);
                n++;
            }
            if ((tft_width < 240) || (tft_height < 240)) TFT_setFont(DEF_SMALL_FONT, NULL);
            else TFT_setFont(DEFAULT_FONT, NULL);
            tft_fg = TFT_YELLOW;
            TFT_print((char *)file_fonts[f], 0, (tft_dispWin.y2-tft_dispWin.y1)-TFT_getfontheight()-4);

            tft_font_line_space = 0;
            sprintf(tmp_buff, "%d STRINGS", n);
            update_header(NULL, tmp_buff);
            Wait(-GDEMO_INFO_TIME);
        }
        tft_text_wrap = 0;
    }

    disp_header("ROTATED FONT DEMO");

    end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        for (int f=DEFAULT_FONT; f<FONT_7SEG; f++) {
            tft_fg = random_color();
            TFT_setFont(f, NULL);
            x = rand_interval(8, tft_dispWin.x2-8);
            y = rand_interval(0, (tft_dispWin.y2-tft_dispWin.y1)-TFT_getfontheight()-2);
            tft_font_rotate = rand_interval(0, 359);

            TFT_print("Welcome to ESP32", x, y);
            n++;
        }
    }
    tft_font_rotate = 0;
    sprintf(tmp_buff, "%d STRINGS", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    disp_header("7-SEG FONT DEMO");

    int ms = 0;
    int last_sec = 0;
    uint32_t ctime = clock();
    end_time = clock() + GDEMO_TIME*2;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        y = 12;
        ms = clock() - ctime;
        time(&time_now);
        tm_info = localtime(&time_now);
        if (tm_info->tm_sec != last_sec) {
            last_sec = tm_info->tm_sec;
            ms = 0;
            ctime = clock();
        }

        tft_fg = TFT_ORANGE;
        sprintf(tmp_buff, "%02d:%02d:%03d", tm_info->tm_min, tm_info->tm_sec, ms);
        TFT_setFont(FONT_7SEG, NULL);
        if ((tft_width < 240) || (tft_height < 240)) set_7seg_font_atrib(8, 1, 1, TFT_DARKGREY);
        else set_7seg_font_atrib(12, 2, 1, TFT_DARKGREY);
        //TFT_clearStringRect(12, y, tmp_buff);
        TFT_print(tmp_buff, CENTER, y);
        n++;

        tft_fg = TFT_GREEN;
        y += TFT_getfontheight() + 12;
        if ((tft_width < 240) || (tft_height < 240)) set_7seg_font_atrib(9, 1, 1, TFT_DARKGREY);
        else set_7seg_font_atrib(14, 3, 1, TFT_DARKGREY);
        sprintf(tmp_buff, "%02d:%02d", tm_info->tm_sec, ms / 10);
        //TFT_clearStringRect(12, y, tmp_buff);
        TFT_print(tmp_buff, CENTER, y);
        n++;

        tft_fg = random_color();
        y += TFT_getfontheight() + 8;
        set_7seg_font_atrib(6, 1, 1, TFT_DARKGREY);
        getFontCharacters((uint8_t *)tmp_buff);
        //TFT_clearStringRect(12, y, tmp_buff);
        TFT_print(tmp_buff, CENTER, y);
        n++;
    }
    sprintf(tmp_buff, "%d STRINGS", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    disp_header("WINDOW DEMO");

    TFT_saveClipWin();
    TFT_resetclipwin();
    TFT_drawRect(38, 48, (tft_width*3/4) - 36, (tft_height*3/4) - 46, TFT_WHITE);
    TFT_setclipwin(40, 50, tft_width*3/4, tft_height*3/4);

    if ((tft_width < 240) || (tft_height < 240)) TFT_setFont(DEF_SMALL_FONT, NULL);
    else TFT_setFont(UBUNTU16_FONT, NULL);
    tft_text_wrap = 1;
    end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        tft_fg = random_color();
        TFT_print("This text is printed inside the window.\nLong line can be wrapped to the next line.\nWelcome to ESP32", 0, 0);
        n++;
    }
    tft_text_wrap = 0;
    sprintf(tmp_buff, "%d STRINGS", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    TFT_restoreClipWin();
}

//---------------------
static void rect_demo() {
    int x, y, w, h, n;

    disp_header("RECTANGLE DEMO");
    printf("Demo: %s\r\n", __func__);

    uint32_t end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x = rand_interval(4, tft_dispWin.x2-4);
        y = rand_interval(4, tft_dispWin.y2-2);
        w = rand_interval(2, tft_dispWin.x2-x);
        h = rand_interval(2, tft_dispWin.y2-y);
        TFT_drawRect(x,y,w,h,random_color());
        n++;
    }
    sprintf(tmp_buff, "%d RECTANGLES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    update_header("FILLED RECTANGLE", "");
    TFT_fillWindow(TFT_BLACK);
    end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x = rand_interval(4, tft_dispWin.x2-4);
        y = rand_interval(4, tft_dispWin.y2-2);
        w = rand_interval(2, tft_dispWin.x2-x);
        h = rand_interval(2, tft_dispWin.y2-y);
        TFT_fillRect(x,y,w,h,random_color());
        TFT_drawRect(x,y,w,h,random_color());
        n++;
    }
    sprintf(tmp_buff, "%d RECTANGLES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//----------------------
static void pixel_demo() {
    int x, y, n;

    printf("Demo: %s\r\n", __func__);
    disp_header("DRAW PIXEL DEMO");

    uint32_t end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x = rand_interval(0, tft_dispWin.x2);
        y = rand_interval(0, tft_dispWin.y2);
        TFT_drawPixel(x,y,random_color());
        n++;
    }
    sprintf(tmp_buff, "%d PIXELS", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//---------------------
static void line_demo() {
    int x1, y1, x2, y2, n;

    disp_header("LINE DEMO");
    printf("Demo: %s\r\n", __func__);

    uint32_t end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x1 = rand_interval(0, tft_dispWin.x2);
        y1 = rand_interval(0, tft_dispWin.y2);
        x2 = rand_interval(0, tft_dispWin.x2);
        y2 = rand_interval(0, tft_dispWin.y2);
        TFT_drawLine(x1,y1,x2,y2,random_color());
        n++;
    }
    sprintf(tmp_buff, "%d LINES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//----------------------
static void aline_demo() {
    int x, y, len, angle, n;

    printf("Demo: %s\r\n", __func__);
    disp_header("LINE BY ANGLE DEMO");

    x = (tft_dispWin.x2 - tft_dispWin.x1) / 2;
    y = (tft_dispWin.y2 - tft_dispWin.y1) / 2;
    if (x < y) len = x - 8;
    else len = y -8;

    uint32_t end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        for (angle=0; angle < 360; angle++) {
            TFT_drawLineByAngle(x,y, 0, len, angle, random_color());
            n++;
        }
    }

    TFT_fillWindow(TFT_BLACK);
    end_time = clock() + GDEMO_TIME;
    while ((clock() < end_time) && (Wait(0))) {
        for (angle=0; angle < 360; angle++) {
            TFT_drawLineByAngle(x, y, len/4, len/4,angle, random_color());
            n++;
        }
        for (angle=0; angle < 360; angle++) {
            TFT_drawLineByAngle(x, y, len*3/4, len/4,angle, random_color());
            n++;
        }
    }
    sprintf(tmp_buff, "%d LINES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//--------------------
static void arc_demo() {
    uint16_t x, y, r, th, n, i;
    float start, end;
    color_t color, fillcolor;
    printf("Demo: %s\r\n", __func__);

    disp_header("ARC DEMO");

    x = (tft_dispWin.x2 - tft_dispWin.x1) / 2;
    y = (tft_dispWin.y2 - tft_dispWin.y1) / 2;

    th = 6;
    uint32_t end_time = clock() + GDEMO_TIME;
    i = 0;
    while ((clock() < end_time) && (Wait(0))) {
        if (x < y) r = x - 4;
        else r = y - 4;
        start = 0;
        end = 20;
        n = 1;
        while (r > 10) {
            color = random_color();
            TFT_drawArc(x, y, r, th, start, end, color, color);
            r -= (th+2);
            n++;
            start += 30;
            end = start + (n*20);
            i++;
        }
    }
    sprintf(tmp_buff, "%d ARCS", i);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    update_header("OUTLINED ARC", "");
    TFT_fillWindow(TFT_BLACK);
    th = 8;
    end_time = clock() + GDEMO_TIME;
    i = 0;
    while ((clock() < end_time) && (Wait(0))) {
        if (x < y) r = x - 4;
        else r = y - 4;
        start = 0;
        end = 350;
        n = 1;
        while (r > 10) {
            color = random_color();
            fillcolor = random_color();
            TFT_drawArc(x, y, r, th, start, end, color, fillcolor);
            r -= (th+2);
            n++;
            start += 20;
            end -= n*10;
            i++;
        }
    }
    sprintf(tmp_buff, "%d ARCS", i);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//-----------------------
static void circle_demo() {
    int x, y, r, n;

    printf("Demo: %s\r\n", __func__);
    disp_header("CIRCLE DEMO");

    uint32_t end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x = rand_interval(8, tft_dispWin.x2-8);
        y = rand_interval(8, tft_dispWin.y2-8);
        if (x < y) r = rand_interval(2, x/2);
        else r = rand_interval(2, y/2);
        TFT_drawCircle(x,y,r,random_color());
        n++;
    }
    sprintf(tmp_buff, "%d CIRCLES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    update_header("FILLED CIRCLE", "");
    TFT_fillWindow(TFT_BLACK);
    end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x = rand_interval(8, tft_dispWin.x2-8);
        y = rand_interval(8, tft_dispWin.y2-8);
        if (x < y) r = rand_interval(2, x/2);
        else r = rand_interval(2, y/2);
        TFT_fillCircle(x,y,r,random_color());
        TFT_drawCircle(x,y,r,random_color());
        n++;
    }
    sprintf(tmp_buff, "%d CIRCLES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//------------------------
static void ellipse_demo() {
    int x, y, rx, ry, n;

    printf("Demo: %s\r\n", __func__);
    disp_header("ELLIPSE DEMO");

    uint32_t end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x = rand_interval(8, tft_dispWin.x2-8);
        y = rand_interval(8, tft_dispWin.y2-8);
        if (x < y) rx = rand_interval(2, x/4);
        else rx = rand_interval(2, y/4);
        if (x < y) ry = rand_interval(2, x/4);
        else ry = rand_interval(2, y/4);
        TFT_drawEllipse(x,y,rx,ry,random_color(),15);
        n++;
    }
    sprintf(tmp_buff, "%d ELLIPSES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    update_header("FILLED ELLIPSE", "");
    TFT_fillWindow(TFT_BLACK);
    end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x = rand_interval(8, tft_dispWin.x2-8);
        y = rand_interval(8, tft_dispWin.y2-8);
        if (x < y) rx = rand_interval(2, x/4);
        else rx = rand_interval(2, y/4);
        if (x < y) ry = rand_interval(2, x/4);
        else ry = rand_interval(2, y/4);
        TFT_fillEllipse(x,y,rx,ry,random_color(),15);
        TFT_drawEllipse(x,y,rx,ry,random_color(),15);
        n++;
    }
    sprintf(tmp_buff, "%d ELLIPSES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    update_header("ELLIPSE SEGMENTS", "");
    TFT_fillWindow(TFT_BLACK);
    end_time = clock() + GDEMO_TIME;
    n = 0;
    int k = 1;
    while ((clock() < end_time) && (Wait(0))) {
        x = rand_interval(8, tft_dispWin.x2-8);
        y = rand_interval(8, tft_dispWin.y2-8);
        if (x < y) rx = rand_interval(2, x/4);
        else rx = rand_interval(2, y/4);
        if (x < y) ry = rand_interval(2, x/4);
        else ry = rand_interval(2, y/4);
        TFT_fillEllipse(x,y,rx,ry,random_color(), (1<<k));
        TFT_drawEllipse(x,y,rx,ry,random_color(), (1<<k));
        k = (k+1) & 3;
        n++;
    }
    sprintf(tmp_buff, "%d SEGMENTS", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//-------------------------
static void triangle_demo()
{
    int x1, y1, x2, y2, x3, y3, n;

    printf("Demo: %s\r\n", __func__);
    disp_header("TRIANGLE DEMO");

    uint32_t end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x1 = rand_interval(4, tft_dispWin.x2-4);
        y1 = rand_interval(4, tft_dispWin.y2-2);
        x2 = rand_interval(4, tft_dispWin.x2-4);
        y2 = rand_interval(4, tft_dispWin.y2-2);
        x3 = rand_interval(4, tft_dispWin.x2-4);
        y3 = rand_interval(4, tft_dispWin.y2-2);
        TFT_drawTriangle(x1,y1,x2,y2,x3,y3,random_color());
        n++;
    }
    sprintf(tmp_buff, "%d TRIANGLES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    update_header("FILLED TRIANGLE", "");
    TFT_fillWindow(TFT_BLACK);
    end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        x1 = rand_interval(4, tft_dispWin.x2-4);
        y1 = rand_interval(4, tft_dispWin.y2-2);
        x2 = rand_interval(4, tft_dispWin.x2-4);
        y2 = rand_interval(4, tft_dispWin.y2-2);
        x3 = rand_interval(4, tft_dispWin.x2-4);
        y3 = rand_interval(4, tft_dispWin.y2-2);
        TFT_fillTriangle(x1,y1,x2,y2,x3,y3,random_color());
        TFT_drawTriangle(x1,y1,x2,y2,x3,y3,random_color());
        n++;
    }
    sprintf(tmp_buff, "%d TRIANGLES", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//---------------------
static void poly_demo() {
    uint16_t x, y, rot, oldrot;
    int i, n, r;
    uint8_t sides[6] = {3, 4, 5, 6, 8, 10};
    color_t color[6] = {TFT_WHITE, TFT_CYAN, TFT_RED,       TFT_BLUE,     TFT_YELLOW,     TFT_ORANGE};
    color_t fill[6]  = {TFT_BLUE,  TFT_NAVY,   TFT_DARKGREEN, TFT_DARKGREY, TFT_LIGHTGREY, TFT_OLIVE};

    printf("Demo: %s\r\n", __func__);
    disp_header("POLYGON DEMO");

    x = (tft_dispWin.x2 - tft_dispWin.x1) / 2;
    y = (tft_dispWin.y2 - tft_dispWin.y1) / 2;

    rot = 0;
    oldrot = 0;
    uint32_t end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        if (x < y) r = x - 4;
        else r = y - 4;
        for (i=5; i>=0; i--) {
            TFT_drawPolygon(x, y, sides[i], r, TFT_BLACK, TFT_BLACK, oldrot, 1);
            TFT_drawPolygon(x, y, sides[i], r, color[i], color[i], rot, 1);
            r -= 16;
            if (r <= 0) { break; };
            n += 2;
        }
        Wait(100);
        oldrot = rot;
        rot = (rot + 15) % 360;
    }
    sprintf(tmp_buff, "%d POLYGONS", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);

    update_header("FILLED POLYGON", "");
    rot = 0;
    end_time = clock() + GDEMO_TIME;
    n = 0;
    while ((clock() < end_time) && (Wait(0))) {
        if (x < y) r = x - 4;
        else r = y - 4;
        TFT_fillWindow(TFT_BLACK);
        for (i=5; i>=0; i--) {
            TFT_drawPolygon(x, y, sides[i], r, color[i], fill[i], rot, 2);
            r -= 16;
            if (r <= 0) { break; }
            n += 2;
        }
        Wait(500);
        rot = (rot + 15) % 360;
    }
    sprintf(tmp_buff, "%d POLYGONS", n);
    update_header(NULL, tmp_buff);
    Wait(-GDEMO_INFO_TIME);
}

//===============
void tft_demo() {

    tft_font_rotate = 0;
    tft_text_wrap = 0;
    tft_font_transparent = 0;
    tft_font_forceFixed = 0;
    printf("Demo: %s\n", __func__);
    TFT_resetclipwin();

    tft_image_debug = 0;

    char dtype[16];
    
    switch (tft_disp_type) {
        case DISP_TYPE_ILI9341:
            sprintf(dtype, "ILI9341");
            break;
        case DISP_TYPE_ILI9488:
            sprintf(dtype, "ILI9488");
            break;
        case DISP_TYPE_ST7789V:
            sprintf(dtype, "ST7789V");
            break;
        case DISP_TYPE_ST7735:
            sprintf(dtype, "ST7735");
            break;
        case DISP_TYPE_ST7735R:
            sprintf(dtype, "ST7735R");
            break;
        case DISP_TYPE_ST7735B:
            sprintf(dtype, "ST7735B");
            break;
        default:
            sprintf(dtype, "Unknown");
    }
    
    uint8_t disp_rot = LANDSCAPE_FLIP;
    _demo_pass = 0;
    tft_gray_scale = 0;
    doprint = 1;

    TFT_setRotation(disp_rot);
    disp_header("Currency track");
    TFT_setFont(COMIC24_FONT, NULL);
    int tempy = TFT_getfontheight() + 4;
    tft_fg = TFT_ORANGE;
    TFT_print("USD/BTC-MXN/EUR", CENTER, (tft_dispWin.y2-tft_dispWin.y1)/2 - tempy);
    TFT_setFont(UBUNTU16_FONT, NULL);
    tft_fg = TFT_CYAN;
    TFT_print("------", CENTER, LASTY+tempy);
    tempy = TFT_getfontheight() + 4;
    TFT_setFont(DEFAULT_FONT, NULL);
    tft_fg = TFT_GREEN;
    sprintf(tmp_buff, "Read speed: %5.2f MHz", (float)tft_max_rdclock/1000000.0);
    TFT_print(tmp_buff, CENTER, LASTY+tempy);

    Wait(4000);

    TFT_setRotation(disp_rot);
    if (doprint) {
        if (disp_rot == PORTRAIT) sprintf(tmp_buff, "PORTRAIT");
        if (disp_rot == LANDSCAPE) sprintf(tmp_buff, "LANDSCAPE");
        if (disp_rot == PORTRAIT_FLIP) sprintf(tmp_buff, "PORTRAIT FLIP");
        if (disp_rot == LANDSCAPE_FLIP) sprintf(tmp_buff, "LANDSCAPE FLIP");
        printf("\r\n==========================================\r\nDisplay: %s: %s %d,%d %s\r\n\r\n",
                dtype, tmp_buff, tft_width, tft_height, ((tft_gray_scale) ? "Gray" : "Color"));
    }
    while (1) {

        // disp_header("Welcome to ESP32");

        // test_times();
        // font_demo();
        // line_demo();
        // aline_demo();
        // rect_demo();
        // circle_demo();
        // ellipse_demo();
        // arc_demo();
        // triangle_demo();
        // poly_demo();
        // pixel_demo();
        // disp_images();
        btc_usd_demo();
        eur_mxn_demo();

        _demo_pass++;
    }
}

void file_listing() {
    DIR *dir;
    printf("Listing contents of /spiffs/ fs\n");
    struct dirent *ent;
    bool opendir_successful = (dir = opendir("/spiffs/")) != NULL;
    printf("Directory read error code: %s\n", strerror(errno));
    if (opendir_successful) {
      while ((ent = readdir(dir)) != NULL) {
        printf ("File found: /spiffs/%s\n", ent->d_name);
      }
      closedir (dir);
    } else {
      printf("Error reading spiffs directory.\n");
    }
    
}

//=============
void app_main() {
    // ========  PREPARE DISPLAY INITIALIZATION  =========

    ESP_LOGI(tag, "Initializing SNTP");
    esp_err_t ret;

    // === SET GLOBAL VARIABLES ==========================
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);


    // ===================================================
    // ==== Set maximum spi clock for display read    ====
    //      operations, function 'find_rd_speed()'    ====
    //      can be used after display initialization  ====
    tft_max_rdclock = 8000000;
    // ===================================================

    // ====================================================================
    // === Pins MUST be initialized before SPI interface initialization ===
    // ====================================================================
    TFT_PinsInit();

    // ====  CONFIGURE SPI DEVICES(s)  ====================================

    spi_device_handle_t spi;
    
    spi_bus_config_t buscfg={
        .miso_io_num=PIN_NUM_MISO,                // set SPI MISO pin
        .mosi_io_num=PIN_NUM_MOSI,                // set SPI MOSI pin
        .sclk_io_num=PIN_NUM_CLK,                // set SPI CLK pin
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz = 6*1024,
    };
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        //TODO: check what amount of dummy bits are needed for reading, i have no board to test this on
        .dummy_bits = 0,
        .mode=0,                                // SPI mode 0
        //the following timings are based on the ST7789V datasheet,
        //and the assumption that the circuit does not add too much delay
        //your milage may vary heavily, adjust accordingly
        .cs_ena_pretrans = 2,                   // set CS 2 Cycles before starting transmission
        .cs_ena_posttrans = 2,                  // hold CS for 2 Cycles after transmissions
        .clock_speed_hz=DEFAULT_SPI_CLOCK,      // Initial clock out at 8 MHz
        .spics_io_num=PIN_NUM_CS,               // external CS pin
        //TODO: check if .input_delay_ns is needed
        .flags=SPI_DEVICE_HALFDUPLEX,           // ALWAYS SET  to HALF DUPLEX MODE!! for display spi
        .queue_size = (                            // transmission queue size
            (DEFAULT_TFT_DISPLAY_HEIGHT
             *DEFAULT_TFT_DISPLAY_WIDTH)
             /TFT_REPEAT_BUFFER_SIZE)
             + 10, //just a bit more for good measure
        //the queue size has to be large enougth to fit at least
        //((resolution_h*resolution_v)/tft_repeat_buffer_size)
        //transmissions
        .pre_cb = &TFT_transaction_begin_callback,
        .post_cb = NULL,
    };

#if USE_TOUCH == TOUCH_TYPE_XPT2046
    spi_device_handle_t tsspi = NULL;

    spi_device_interface_config_t tsdevcfg={
        .clock_speed_hz=2500000,                //Clock out at 2.5 MHz
        .mode=0,                                //SPI mode 0
        .spics_io_num=PIN_NUM_TCS,              //Touch CS pin
        .spics_ext_io_num=-1,                   //Not using the external CS
        //.command_bits=8,                      //1 byte command
    };
#elif USE_TOUCH == TOUCH_TYPE_STMPE610
    spi_device_handle_t tsspi = NULL;

    spi_device_interface_config_t tsdevcfg={
        .clock_speed_hz=1000000,                //Clock out at 1 MHz
        .mode=STMPE610_SPI_MODE,                //SPI mode 0
        .spics_io_num=PIN_NUM_TCS,              //Touch CS pin
        .spics_ext_io_num=-1,                   //Not using the external CS
        .flags = 0,
    };
#endif

    // ====================================================================================================================


    vTaskDelay(500 / portTICK_RATE_MS);
    printf("\r\n==============================\r\n");
    printf("TFT display DEMO, LoBo 11/2017\r\n");
    printf("==============================\r\n");
    printf("Pins used: miso=%d, mosi=%d, sck=%d, cs=%d\r\n", PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
    printf("==============================\r\n\r\n");

    // ==================================================================
    // ==== Initialize the SPI bus and attach the LCD to the SPI bus ====

    ret = spi_bus_initialize(SPI_BUS, &buscfg, 1 /* dma chan */); //TODO: check if this use of the dma channel is valid
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_add_device(SPI_BUS, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
    printf("SPI: display device added to spi bus (%d)\r\n", SPI_BUS);
    tft_disp_spi = spi;

    //TODO: find replacement for gettting these values
    //printf("SPI: attached display device, speed=%u\r\n", spi_get_speed(spi));
    //printf("SPI: bus uses native pins: %s\r\n", spi_uses_native_pins(spi) ? "true" : "false");

#if USE_TOUCH > TOUCH_TYPE_NONE
    // =====================================================
    // ==== Attach the touch screen to the same SPI bus ====

    ret=spi_bus_add_device(SPI_BUS, &tsdevcfg, &tsspi);
    ESP_ERROR_CHECK(ret);
    printf("SPI: touch screen device added to spi bus (%d)\r\n", SPI_BUS);
    tft_ts_spi = tsspi;

    printf("SPI: attached TS device, speed=%u\r\n", spi_get_speed(tsspi));
#endif

    // ================================
    // ==== Initialize the Display ====

    printf("SPI: display init...\r\n");
    TFT_display_init();
#ifdef TFT_START_COLORS_INVERTED
    TFT_invertDisplay(1);
#endif
    printf("OK\r\n");
    #if USE_TOUCH == TOUCH_TYPE_STMPE610
    stmpe610_Init();
    vTaskDelay(10 / portTICK_RATE_MS);
    uint32_t tver = stmpe610_getID();
    printf("STMPE touch initialized, ver: %04x - %02x\r\n", tver >> 8, tver & 0xFF);
    #endif

    printf("\r\n---------------------\r\n");
    printf("Graphics demo started\r\n");
    printf("---------------------\r\n");

    tft_font_rotate = 0;
    tft_text_wrap = 0;
    tft_font_transparent = 0;
    tft_font_forceFixed = 0;
    tft_gray_scale = 0;
    TFT_setGammaCurve(DEFAULT_GAMMA_CURVE);
    TFT_setRotation(LANDSCAPE_FLIP);
    TFT_setFont(DEFAULT_FONT, NULL);
    TFT_resetclipwin();

#ifdef CONFIG_ESP_EXAMPLE_USE_WIFI

    ESP_ERROR_CHECK( nvs_flash_init() );

    ESP_LOGI(tag, "-------------------------------------- NTP.");
    // ===== Set time zone ======
    setenv("TZ", "CET-1CEST", 0);
    tzset();
    // ==========================

    disp_header("GET NTP TIME");

    time(&time_now);
    tm_info = localtime(&time_now);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if (tm_info->tm_year < (2016 - 1900)) {
        ESP_LOGI(tag, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        tft_fg = TFT_CYAN;
        TFT_print("Time is not set yet", CENTER, CENTER);
        TFT_print("Connecting to WiFi", CENTER, LASTY+TFT_getfontheight()+2);
        TFT_print("Getting time over NTP", CENTER, LASTY+TFT_getfontheight()+2);
        tft_fg = TFT_YELLOW;
        TFT_print("Wait", CENTER, LASTY+TFT_getfontheight()+2);
        if (obtain_time()) {
            tft_fg = TFT_GREEN;
            TFT_print("System time is set.", CENTER, LASTY);
        }
        else {
            tft_fg = TFT_RED;
            TFT_print("ERROR.", CENTER, LASTY);
        }
        time(&time_now);
        update_header(NULL, "");
        Wait(-2000);
    }
#endif

    disp_header("File system INIT");
    tft_fg = TFT_CYAN;
    TFT_print("Initializing SPIFFS...", CENTER, CENTER);
    // ==== Initialize the file system ====
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = "storage",
        .max_files = 20,
        .format_if_mount_failed = false
    };
    esp_err_t spiffs_ret = esp_vfs_spiffs_register(&conf);
    spiffs_is_mounted = spiffs_ret == ESP_OK;
    printf("\r\n\nSpiffs mount status: %s\n", esp_err_to_name(spiffs_ret));
    if (!spiffs_is_mounted) {
        tft_fg = TFT_RED;
        TFT_print("SPIFFS not mounted !", CENTER, LASTY+TFT_getfontheight()+2);
    }
    else {
        tft_fg = TFT_GREEN;
        TFT_print("SPIFFS Mounted.", CENTER, LASTY+TFT_getfontheight()+2);
    }
    
    file_listing();
    
    Wait(-2000);

    mqtt_register_start();
    //=========
    // Run demo
    //=========
    tft_demo();
}
