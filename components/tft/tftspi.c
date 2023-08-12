/*
 *  Author: LoBo (loboris@gmail.com, loboris.github)
 *
 *  Module supporting SPI TFT displays based on ILI9341 & ILI9488 controllers
 *
 * HIGH SPEED LOW LEVEL DISPLAY FUNCTIONS
 * USING DIRECT or DMA SPI TRANSFER MODEs
 *
*/

#include <string.h>
#include "tftspi.h"
#include "freertos/task.h"
#include "soc/spi_reg.h"
#include "driver/gpio.h"
#include "esp_attr.h"

//TODO: remove all disableINTERRUPTS (and corresponding enable interrupts)

// ====================================================
// ==== Global variables, default values ==============

// Converts colors to grayscale if set to 1
uint8_t tft_gray_scale = 0; //TODO: put a macro in place that sets this value to a constant, which will enable many small automatic optimizations
// Spi clock for reading data from display memory in Hz
uint32_t tft_max_rdclock = 8000000;

// Default display dimensions
int tft_width = DEFAULT_TFT_DISPLAY_WIDTH;
int tft_height = DEFAULT_TFT_DISPLAY_HEIGHT;

// Display type, DISP_TYPE_ILI9488 or DISP_TYPE_ILI9341
uint8_t tft_disp_type = DEFAULT_DISP_TYPE;

// Spi device handles for display and touch screen
spi_device_handle_t tft_disp_spi = NULL;
spi_device_handle_t tft_ts_spi = NULL;

// ====================================================


static color_t *trans_cline = NULL;

// RGB to GRAYSCALE constants
// 0.2989  0.5870  0.1140
#define GS_FACT_R 0.2989
#define GS_FACT_G 0.4870
#define GS_FACT_B 0.2140

// === transmission callback definitions ==============

// these functions are put into IRAM to speed up transmissions,
// for efficiency setting CONFIG_SPU_MASTER_IN_IRAM in menuconfig might be desired

// struct for transaction callback information, it is a struct in order to be somewhat easily extensible
//TODO: check if it is viable to put all instantiations of this into DRAM somehow
typedef struct {
    WORD_ALIGNED_ATTR uint32_t dc_level; // Display command/data pin level to set
} tft_spi_user_t;

static DMA_ATTR tft_spi_user_t tft_spi_user_command = {
    .dc_level = 0,
};

static DMA_ATTR tft_spi_user_t tft_spi_user_data = {
    .dc_level = 1,
};

static DRAM_ATTR uint32_t last_DC_state = 42;
void IRAM_ATTR TFT_transaction_begin_callback(spi_transaction_t* transaction){
    uint32_t state_to_set = ((tft_spi_user_t*)transaction->user)->dc_level;
    gpio_set_level(PIN_NUM_DC, state_to_set);
}

// ==== Functions =====================

//-------------------------------
inline esp_err_t IRAM_ATTR disp_select() {
    //TODO: check necessity for this function
    return spi_device_acquire_bus(tft_disp_spi, portMAX_DELAY);
}

//---------------------------------
inline esp_err_t IRAM_ATTR disp_deselect() {
    //TODO: check necessity for this function
    spi_device_release_bus(tft_disp_spi);
    return ESP_OK;
}

// Send 1 byte display command, display must be selected
//------------------------------------------------
//Due to this being implemented as a blocking polling transaction,
//you must not call this function while any other transaction is still unfinished.
//As with the other functions, this function may never be called while it has not returned in another context.
void IRAM_ATTR disp_spi_transfer_cmd(int8_t cmd) {
    static spi_transaction_t command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {0},
        .rx_buffer = NULL,
    };
    command_transaction.tx_data[0] = cmd;

    esp_err_t ret = spi_device_polling_transmit(tft_disp_spi, &command_transaction);
    ESP_ERROR_CHECK(ret);
}

// Send command with data to display, display must be selected
//----------------------------------------------------------------------------------
//This is implemented with blocking polling transactions,
//as such it may only be called when all prior transactions are finished.
void IRAM_ATTR disp_spi_transfer_cmd_data(int8_t cmd, uint8_t *data, uint32_t len) {
    esp_err_t ret;

    //Command sending transaction
    static spi_transaction_t command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {0},
        .rx_buffer = NULL,
    };
    command_transaction.tx_data[0] = (uint8_t) cmd;
    ret = spi_device_polling_transmit(tft_disp_spi, &command_transaction);
    ESP_ERROR_CHECK(ret);

    //Data sending transaction
    if ((len == 0) || (data == NULL)) return; //skip if no data is to be sent
    static spi_transaction_t data_transaction = {
        .flags = 0,
        .user = &tft_spi_user_data,
        .rx_buffer = NULL,
    };
    data_transaction.length = 8 * len;
    data_transaction.tx_buffer = data;

    ret = spi_device_polling_transmit(tft_disp_spi, &data_transaction);
}

// Set the address window for display write & read commands
//---------------------------------------------------------------------------------------------------
static void IRAM_ATTR disp_spi_transfer_addrwin_start(uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2) {
    // This function sets the address window by first sending a command to set the collumn range
    // and then a command to send the row range
    // separate transactions are used, to be able to set the controllers command pin
    // through the use of a custom callback

    esp_err_t ret;

    // -- column setting command

    static const spi_transaction_t column_setting_command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {TFT_CASET, 0, 0, 0},
        .rx_buffer = NULL,
    };

    ret = spi_device_queue_trans(tft_disp_spi, &column_setting_command_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);

    // -- column setting data
    // Arrange x coordinate window values
    static spi_transaction_t column_setting_data_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_data,
        .length = 32,
        .rx_buffer = NULL,
    };
    column_setting_data_transaction.tx_data[0] = x1 >> 8;
    column_setting_data_transaction.tx_data[1] = x1 &  0xff;
    column_setting_data_transaction.tx_data[2] = x2 >> 8;
    column_setting_data_transaction.tx_data[3] = x2 &  0xff;
    
    ret = spi_device_queue_trans(tft_disp_spi, &column_setting_data_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);;

    // -- row setting command

    static const spi_transaction_t row_setting_command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {TFT_PASET, 0, 0, 0},
        .rx_buffer = NULL,
    };

    ret = spi_device_queue_trans(tft_disp_spi, &row_setting_command_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);

    // -- row setting data
    // Arrange x coordinate window values
    static spi_transaction_t row_setting_data_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_data,
        .length = 32,
        .rx_buffer = NULL,
    };
    row_setting_data_transaction.tx_data[0] = y1 >> 8;
    row_setting_data_transaction.tx_data[1] = y1 &  0xff;
    row_setting_data_transaction.tx_data[2] = y2 >> 8;
    row_setting_data_transaction.tx_data[3] = y2 &  0xff;
    
    ret = spi_device_queue_trans(tft_disp_spi, &row_setting_data_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
}

static void IRAM_ATTR disp_spi_transfer_addrwin_polling(uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2) {
    // This function sets the address window by first sending a command to set the collumn range
    // and then a command to send the row range
    // separate transactions are used, to be able to set the controllers command pin
    // through the use of a custom callback

    esp_err_t ret;

    // -- column setting command

    static const spi_transaction_t column_setting_command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {TFT_CASET, 0, 0, 0},
        .rx_buffer = NULL,
    };

    ret = spi_device_polling_transmit(tft_disp_spi, &column_setting_command_transaction);
    ESP_ERROR_CHECK(ret);

    // -- column setting data
    // Arrange x coordinate window values
    static spi_transaction_t column_setting_data_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_data,
        .length = 32,
        .rx_buffer = NULL,
    };
    column_setting_data_transaction.tx_data[0] = x1 >> 8;
    column_setting_data_transaction.tx_data[1] = x1 &  0xff;
    column_setting_data_transaction.tx_data[2] = x2 >> 8;
    column_setting_data_transaction.tx_data[3] = x2 &  0xff;
    
    ret = spi_device_polling_transmit(tft_disp_spi, &column_setting_data_transaction);
    ESP_ERROR_CHECK(ret);;

    // -- row setting command

    static const spi_transaction_t row_setting_command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {TFT_PASET, 0, 0, 0},
        .rx_buffer = NULL,
    };

    ret = spi_device_polling_transmit(tft_disp_spi, &row_setting_command_transaction);
    ESP_ERROR_CHECK(ret);

    // -- row setting data
    // Arrange x coordinate window values
    static spi_transaction_t row_setting_data_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_data,
        .length = 32,
        .rx_buffer = NULL,
    };
    row_setting_data_transaction.tx_data[0] = y1 >> 8;
    row_setting_data_transaction.tx_data[1] = y1 &  0xff;
    row_setting_data_transaction.tx_data[2] = y2 >> 8;
    row_setting_data_transaction.tx_data[3] = y2 &  0xff;
    
    ret = spi_device_polling_transmit(tft_disp_spi, &row_setting_data_transaction);
    ESP_ERROR_CHECK(ret);
}

static void IRAM_ATTR disp_spi_transfer_addrwin_finish() {
    spi_transaction_t* result_transaction;
    esp_err_t ret;
    ret = spi_device_get_trans_result(tft_disp_spi, &result_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
    ret = spi_device_get_trans_result(tft_disp_spi, &result_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
    ret = spi_device_get_trans_result(tft_disp_spi, &result_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
    ret = spi_device_get_trans_result(tft_disp_spi, &result_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
}

// Convert color to gray scale
//----------------------------------------------
static color_t IRAM_ATTR color2gs(color_t color) {
    color_t _color;
    float gs_clr = GS_FACT_R * color.r + GS_FACT_G * color.g + GS_FACT_B * color.b;
    if (gs_clr > 255) gs_clr = 255;

    _color.r = (uint8_t)gs_clr;
    _color.g = (uint8_t)gs_clr;
    _color.b = (uint8_t)gs_clr;

    return _color;
}

// Set display pixel at given coordinates to given color
//------------------------------------------------------------------------
//IMPORTANT: this function assumes half duplex operation,
//           in previous versions this was checked, now it is assumed
//this function is fairly inefficient, as it resets the operation window every time
void IRAM_ATTR drawPixel(int16_t x, int16_t y, color_t color)
{
    esp_err_t ret;
    color_t _color = color;
    if (tft_gray_scale) _color = color2gs(color);
    disp_spi_transfer_addrwin_polling(x, x+1, y, y+1);
    //writing command transaction
    static const spi_transaction_t command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {TFT_RAMWR, 0, 0, 0},
        .rx_buffer = NULL,
    };

    ret = spi_device_polling_transmit(tft_disp_spi, &command_transaction);
    ESP_ERROR_CHECK(ret);
    
    //color transaction
    static spi_transaction_t color_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_data,
        .length = 24,
        .rx_buffer = NULL,
    };
    color_transaction.tx_data[0] = _color.r;
    color_transaction.tx_data[1] = _color.g;
    color_transaction.tx_data[2] = _color.b;

    ret = spi_device_polling_transmit(tft_disp_spi, &color_transaction);
    ESP_ERROR_CHECK(ret);
}

//DMA buffer for repeating filling
#define tft_repeat_buffer_size TFT_REPEAT_BUFFER_SIZE
static DMA_ATTR color_t tft_repeat_buffer[tft_repeat_buffer_size] = {0};

// Write 'len' color data to TFT 'window' (x1,y2),(x2,y2)
//-------------------------------------------------------------------------------------------
// This implementation relies on filling the dma queue with transactions from tft_repeat_buffer
// if two consecutive calls of this function use the same fill color, the filling of the buffer
// can be omitted, which speeds up the operation
void IRAM_ATTR TFT_pushColorRep(int x1, int y1, int x2, int y2, color_t color, uint32_t len)
{
    assert(sizeof(color_t) == 3);
    esp_err_t ret;

    disp_spi_transfer_addrwin_start(x1, x2, y1, y2);

    static const spi_transaction_t command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {TFT_RAMWR, 0},
        .rx_buffer = NULL,
    };
    ret = spi_device_queue_trans(tft_disp_spi, &command_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);

    color_t _color = color;
    if(tft_gray_scale) {
        _color = color2gs(color);
    }
    
    if(len <= 10){
        static spi_transaction_t color_transaction_single = {
            .flags = SPI_TRANS_USE_TXDATA,
            .user = &tft_spi_user_data,
            .length = 24,
            .tx_data = {0},
            .rx_buffer = NULL,
        };
        color_transaction_single.tx_data[0] = _color.r;
        color_transaction_single.tx_data[1] = _color.g;
        color_transaction_single.tx_data[2] = _color.b;

        disp_spi_transfer_addrwin_finish();
        spi_transaction_t* result_transaction;
        ret = spi_device_get_trans_result(tft_disp_spi, &result_transaction, portMAX_DELAY);
        ESP_ERROR_CHECK(ret);

        for(uint32_t count = 0; count < len; count++){
            ret = spi_device_polling_transmit(tft_disp_spi, &color_transaction_single);
            ESP_ERROR_CHECK(ret);
        }
    } else {
        //Set up the dma buffer to fill the screen from
        //if the buffer is already filled with the color we want, we can skip this
        if(tft_repeat_buffer[0].r != _color.r || tft_repeat_buffer[0].g != _color.g || tft_repeat_buffer[0].b != _color.b){
            for(size_t i = 0; i < tft_repeat_buffer_size; i++){
                tft_repeat_buffer[i] = _color;
            }
        }

        uint32_t queued_transactions = 1; //start off with one, for the command transaction
        uint32_t still_to_send = len;
        static const spi_transaction_t color_transaction = {
            .flags = 0,
            .user = &tft_spi_user_data,
            .length = 3*8*tft_repeat_buffer_size,
            .tx_buffer = &tft_repeat_buffer,
            .rx_buffer = NULL,
        };
        while (still_to_send >= tft_repeat_buffer_size) {
            still_to_send -= tft_repeat_buffer_size;

            queued_transactions++;
            ret = spi_device_queue_trans(tft_disp_spi, &color_transaction, portMAX_DELAY);
            ESP_ERROR_CHECK(ret);
        }

        if (still_to_send > 0) {
            static spi_transaction_t color_transaction_partial = {
                .flags = 0,
                .user = &tft_spi_user_data,
                .tx_buffer = &tft_repeat_buffer,
                .rx_buffer = NULL,
            };
            color_transaction_partial.length = 3*8*still_to_send;
            queued_transactions++;
            ret = spi_device_queue_trans(tft_disp_spi, &color_transaction_partial, portMAX_DELAY);
            ESP_ERROR_CHECK(ret);
        }

        disp_spi_transfer_addrwin_finish();
        while(queued_transactions-- > 0){
            spi_transaction_t* result_transaction;
            ret = spi_device_get_trans_result(tft_disp_spi, &result_transaction, portMAX_DELAY);
            ESP_ERROR_CHECK(ret);
        }
    }
}
#undef tft_repeat_buffer_size

// Write 'len' color data to TFT 'window' (x1,y2),(x2,y2) from given buffer
//-----------------------------------------------------------------------------------
void IRAM_ATTR send_data_start(int x1, int y1, int x2, int y2, uint32_t len, color_t *buf) {
    esp_err_t ret;

    disp_spi_transfer_addrwin_start(x1, x2, y1, y2);

    static const spi_transaction_t command_transaction = {
        .flags = SPI_TRANS_USE_TXDATA,
        .user = &tft_spi_user_command,
        .length = 8,
        .tx_data = {TFT_RAMWR, 0},
        .rx_buffer = NULL,
    };
    ret = spi_device_queue_trans(tft_disp_spi, &command_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);

    static spi_transaction_t data_transaction = {
        .flags = 0,
        .user = &tft_spi_user_data,
        .rx_buffer = NULL,
    };
    data_transaction.length = 8 * 3 * len;
    data_transaction.tx_buffer = (uint8_t*) buf;

    if (tft_gray_scale) {
        for (int n=0; n<len; n++) {
            buf[n] = color2gs(buf[n]);
        }
    }

    ret = spi_device_queue_trans(tft_disp_spi, &data_transaction, portMAX_DELAY);
}

void IRAM_ATTR send_data_finish() {
    disp_spi_transfer_addrwin_finish();
    esp_err_t ret;
    spi_transaction_t* result_transaction;
    //Waiting for the command transaciton
    ret = spi_device_get_trans_result(tft_disp_spi, &result_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
    //waiting for the data transaction
    ret = spi_device_get_trans_result(tft_disp_spi, &result_transaction, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
}

// Reads 'len' pixels/colors from the TFT's GRAM 'window'
// 'buf' is an array of bytes with 1st byte reserved for reading 1 dummy byte
// and the rest is actually an array of color_t values
//--------------------------------------------------------------------------------------------
int IRAM_ATTR read_data(int x1, int y1, int x2, int y2, int len, uint8_t *buf, uint8_t set_sp) {
    spi_transaction_t t;
    uint32_t current_clock = 0;

    memset(&t, 0, sizeof(t));  //Zero out the transaction
    memset(buf, 0, len*sizeof(color_t));

    //TODO: check if this functionality can be recovered
    // if (set_sp) {
    //     if (disp_deselect() != ESP_OK) return -1;
    //     // Change spi clock if needed
    //     current_clock = spi_get_speed(tft_disp_spi);
    //     if (tft_max_rdclock < current_clock) spi_set_speed(tft_disp_spi, tft_max_rdclock);
    // }

    if (disp_select() != ESP_OK) {
        return -2;
    }

    // ** Send address window **
    disp_spi_transfer_addrwin_start(x1, x2, y1, y2);
    disp_spi_transfer_addrwin_finish();

    // ** GET pixels/colors **
    disp_spi_transfer_cmd(TFT_RAMRD);

    t.length    = 0;                   //Send nothing
    t.tx_buffer = NULL;
    t.rxlength  = 8 * ((len * 3) + 1); //Receive size in bits
    t.rx_buffer = buf;
    t.user      = &tft_spi_user_data;

    esp_err_t res = spi_device_polling_transmit(tft_disp_spi, &t);
    // esp_err_t res = spi_transfer_data(tft_disp_spi, &t); // Receive using direct mode

    disp_deselect();

    // if (set_sp) {
    //     // Restore spi clock if needed
    //     if (tft_max_rdclock < current_clock) spi_set_speed(tft_disp_spi, current_clock);
    // }

    return res;
}

// Reads one pixel/color from the TFT's GRAM at position (x,y)
//-----------------------------------------------
color_t IRAM_ATTR readPixel(int16_t x, int16_t y) {
    uint8_t color_buf[sizeof(color_t)+1] = {0};

    read_data(x, y, x+1, y+1, 1, color_buf, 1);

    color_t color;
    color.r = color_buf[1];
    color.g = color_buf[2];
    color.b = color_buf[3];
    return color;
}

// ==== STMPE610 ===========================================================================

//---------------------------------------------------------------------------
// Companion code to the initialization table.
// Reads and issues a series of LCD commands stored in byte array
//---------------------------------------------------------------------------
static void commandList(spi_device_handle_t spi, const uint8_t *addr) {
    uint8_t  numCommands, numArgs, cmd;
    uint16_t ms;

    numCommands = *addr++;                // Number of commands to follow
    while(numCommands--) {                // For each command...
        cmd = *addr++;                        // save command
        numArgs  = *addr++;                    // Number of args to follow
        ms       = numArgs & TFT_CMD_DELAY;    // If high bit set, delay follows args
        numArgs &= ~TFT_CMD_DELAY;            // Mask out delay bit

        disp_spi_transfer_cmd_data(cmd, (uint8_t *)addr, numArgs);

        addr += numArgs;

        if(ms) {
            ms = *addr++;              // Read post-command delay time (ms)
            if(ms == 255) {
                ms = 500;    // If 255, delay for 500 ms
            }
            vTaskDelay(ms / portTICK_RATE_MS);
        }
    }
}

//==================================
void _tft_setRotation(uint8_t rot) {
    uint8_t rotation = rot & 3; // can't be higher than 3
    uint8_t send = 1;
    static uint8_t madctl = 0;
    uint16_t tmp;

    if ((rotation & 1)) {
        // in landscape modes must be width > height
        if (tft_width < tft_height) {
            tmp = tft_width;
            tft_width  = tft_height;
            tft_height = tmp;
        }
    }
    else {
        // in portrait modes must be width < height
        if (tft_width > tft_height) {
            tmp = tft_width;
            tft_width  = tft_height;
            tft_height = tmp;
        }
    }
    #if TFT_INVERT_ROTATION
    switch (rotation) {
        case PORTRAIT:
        madctl = (MADCTL_MV | TFT_RGB_BGR);
        break;
        case LANDSCAPE:
        madctl = (MADCTL_MX | TFT_RGB_BGR);
        break;
        case PORTRAIT_FLIP:
        madctl = (MADCTL_MV | TFT_RGB_BGR);
        break;
        case LANDSCAPE_FLIP:
        madctl = (MADCTL_MY | TFT_RGB_BGR);
        break;
    }
    #elif TFT_INVERT_ROTATION1
    switch (rotation) {
        case PORTRAIT:
        madctl = (MADCTL_MY | MADCTL_MX | TFT_RGB_BGR);
        break;
        case LANDSCAPE:
        madctl = (MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
        break;
        case PORTRAIT_FLIP:
        madctl = (TFT_RGB_BGR);
        break;
        case LANDSCAPE_FLIP:
        madctl = (MADCTL_MX | MADCTL_MV | TFT_RGB_BGR);
        break;
    }
    #elif TFT_INVERT_ROTATION2
    switch (rotation) {
        case PORTRAIT:
        madctl = (MADCTL_MX | MADCTL_MV | TFT_RGB_BGR);
        break;
        case LANDSCAPE:
        madctl = (TFT_RGB_BGR);
        break;
        case PORTRAIT_FLIP:
        madctl = (MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
        break;
        case LANDSCAPE_FLIP:
        madctl = (MADCTL_MY | MADCTL_MX | TFT_RGB_BGR);
        break;
    }
    #else
    switch (rotation) {
        case PORTRAIT:
        madctl = (MADCTL_MX | TFT_RGB_BGR);
        break;
        case LANDSCAPE:
        madctl = (MADCTL_MV | TFT_RGB_BGR);
        break;
        case PORTRAIT_FLIP:
        madctl = (MADCTL_MY | TFT_RGB_BGR);
        break;
        case LANDSCAPE_FLIP:
        madctl = (MADCTL_MX | MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
        break;
    }
    #endif
    if (send) {
        if (disp_select() == ESP_OK) {
            disp_spi_transfer_cmd_data(TFT_MADCTL, &madctl, 1);
            disp_deselect();
        }
    }

}

//=================
void TFT_PinsInit() {
    // Route all used pins to GPIO control
    gpio_pad_select_gpio(PIN_NUM_CS);
    gpio_pad_select_gpio(PIN_NUM_MOSI);
    gpio_pad_select_gpio(PIN_NUM_CLK);
    gpio_pad_select_gpio(PIN_NUM_DC);

    //because MISO is optional, only set it when it is actuall needed
    if(PIN_NUM_MISO >= 0){
        gpio_pad_select_gpio(PIN_NUM_MISO);
        gpio_set_direction(PIN_NUM_MISO, GPIO_MODE_INPUT);
        gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);
    }

    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_CLK, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_DC, 0);
#if USE_TOUCH
    gpio_pad_select_gpio(PIN_NUM_TCS);
    gpio_set_direction(PIN_NUM_TCS, GPIO_MODE_OUTPUT);
#endif
#if PIN_NUM_BCKL
    gpio_pad_select_gpio(PIN_NUM_BCKL);
    gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_BCKL, PIN_BCKL_OFF);
#endif

#if PIN_NUM_RST
    gpio_pad_select_gpio(PIN_NUM_RST);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_RST, 0);
#endif
}

// Initialize the display
// ====================
void TFT_display_init() {
    esp_err_t ret;

#if PIN_NUM_RST
    //Reset the display
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(20 / portTICK_RATE_MS);
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(150 / portTICK_RATE_MS);
#endif

    ret = disp_select();
    ESP_ERROR_CHECK(ret);
    //Send all the initialization commands
    if (tft_disp_type == DISP_TYPE_ILI9341) {
        commandList(tft_disp_spi, ILI9341_init);
    }
    else if (tft_disp_type == DISP_TYPE_ILI9488) {
        commandList(tft_disp_spi, ILI9488_init);
    }
    else if (tft_disp_type == DISP_TYPE_ST7789V) {
        commandList(tft_disp_spi, ST7789V_init);
    }
    else if (tft_disp_type == DISP_TYPE_ST7735) {
        commandList(tft_disp_spi, STP7735_init);
    }
    else if (tft_disp_type == DISP_TYPE_ST7735R) {
        commandList(tft_disp_spi, STP7735R_init);
        commandList(tft_disp_spi, Rcmd2green);
        commandList(tft_disp_spi, Rcmd3);
    }
    else if (tft_disp_type == DISP_TYPE_ST7735B) {
        commandList(tft_disp_spi, STP7735R_init);
        commandList(tft_disp_spi, Rcmd2red);
        commandList(tft_disp_spi, Rcmd3);
        uint8_t dt = 0xC0;
        disp_spi_transfer_cmd_data(TFT_MADCTL, &dt, 1);
    }
    else assert(0);

    ret = disp_deselect();
    ESP_ERROR_CHECK(ret);

    // Clear screen
    _tft_setRotation(PORTRAIT);

    TFT_pushColorRep(TFT_STATIC_WIDTH_OFFSET, TFT_STATIC_HEIGHT_OFFSET,
                     tft_width + TFT_STATIC_WIDTH_OFFSET -1, tft_height + TFT_STATIC_HEIGHT_OFFSET -1,
                     (color_t){0,0,0},
                     (uint32_t)(tft_height * tft_width));
    ///Enable backlight
#if PIN_NUM_BCKL
    gpio_set_level(PIN_NUM_BCKL, PIN_BCKL_ON);
#endif
}
