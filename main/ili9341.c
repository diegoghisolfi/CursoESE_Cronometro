/************************************************************************************************
Copyright (c) 2025, Esteban Volentini <evolentini@herrera.unt.edu.ar>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

SPDX-License-Identifier: MIT
*************************************************************************************************/

/** @file ili9341.c
 ** @brief Definiciones de la biblioteca para el control de TFT con controlador ILI9341
 **/

/* === Headers files inclusions =============================================================== */

#include "ili9341.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>

/* === Macros definitions ====================================================================== */

// To speed up transfers, every SPI transfer sends a bunch of lines. This define specifies how many.
// More means more memory use, but less overhead for setting up / finishing transfers. Make sure 240
// is dividable by this.
#define PARALLEL_LINES    16

#define SPI_BR            51000000      /*!< Frequency of sck for SPI communication */
#define MAX_PIXEL         320 * 240 * 2 /*!< Maximum number of bytes to write on LCD */
#define MSK_BIT16         0x8000        /*!< 16th bit mask */
#define MAX_VALUE_SIZE    256           /*!< Maximum length of a data array to \  prevent excessive use of memory */
#define LEFT              -1            /*!< Horizontal grow direction */
#define RIGHT             1             /*!< Horizontal grow direction */
#define DOWN              1             /*!< Vertical grow direction */
#define UP                -1            /*!< Vertical grow direction */

/* Command List */
#define SEND_PIXELS       0X00
#define RESET             0x01 /*!< Resets the commands and parameters to their S/W Reset default values */
#define SLEEP_IN          0x10 /*!< Enter to the minimum power consumption mode */
#define SLEEP_OUT         0x11 /*!< Turns off sleep mode */
#define DISPLAY_INV_OFF   0x20 /*!< Recover from display inversion mode */
#define DISPLAY_INV_ON    0x21 /*!< Invert every bit from the frame memory to the display */
#define GAMMA_SET         0x26 /*!< Select the desired Gamma curve for the current display */
#define DISPLAY_OFF       0x28 /*!< The output from Frame Memory is disabled and blank page inserted */
#define DISPLAY_ON        0x29 /*!< Recover from DISPLAY OFF mode */
#define COLUMN_ADDR_SET   0x2A /*!< Define columns of frame memory where MCU can access */
#define PAGE_ADDR_SET     0x2B /*!< Define rows of frame memory where MCU can access */
#define MEM_WRITE         0x2C /*!< Transfer data from MCU to frame memory */
#define MEM_ACC_CTRL      0x36 /*!< Defines read/write scanning direction of frame memory */
#define PIXEL_FORMAT_SET  0x3A /*!< Sets the pixel format for the RGB image data used by the interface */
#define WRITE_DISP_BRIGHT 0x51 /*!< Adjust the brightness value of the display */
#define WRITE_CTRL_DISP   0x53 /*!< Control display brightness */
#define RGB_INTERFACE     0xB0 /*!< Sets the operation status of the display interface */
#define FRAME_CTRL        0xB1 /*!< Sets the division ratio for internal clocks of Normal mode at MCU interface */
#define BLANK_PORCH_CTRL  0xB5 /*!< Blanking porch control */
#define DISP_FUN_CTRL     0xB6 /*!< Display function control */
#define PWR_CTRL1         0xC0 /*!< Set the GVDD level, a reference for the VCOM and the grayscale voltage level */
#define PWR_CTRL2         0xC1 /*!< Sets the factor used in the step-up circuits */
#define VCOM_CTRL1        0xC5 /*!< Set the VCOMH voltage */
#define VCOM_CTRL2        0xC7 /*!< Set the VCOMH voltage */
#define PWR_CTRL_A        0xCB /*!< Vcore control */
#define PWR_CTRL_B        0xCF /*!< Power control */
#define POS_GAMMA         0xE0 /*!< Set the gray scale voltage to adjust the gamma characteristics of the TFT panel */
#define NEG_GAMMA         0xE1 /*!< Set the gray scale voltage to adjust the gamma characteristics of the TFT panel */
#define DRIV_TIM_CTRL_A   0xE8 /*!< Timing control */
#define DRIV_TIM_CTRL_B   0xEA /*!< Timing control */
#define PWR_ON_CTRL       0xED /*!< Power on sequence control */
#define EN_3_GAMMA        0xF2 /*!< 3 gamma control enable */
#define PUMP_RATIO_CTRL   0xF7 /*!< Pump ratio control */

#define HighByte(x)       x >> 8   /*!< High byte of a 16 bits data */
#define LowByte(x)        x & 0xFF /*!< Low byte of a 16 bits data */

/* === Private data type declarations ==========================================================
 */

/**
 * @brief  Structure with LCD orientation properties
 */
typedef struct {
    uint16_t width;                    /*!< LCD width */
    uint16_t height;                   /*!< LCD height */
    ili9341_orientation_t orientation; /*!< LCD Orientation */
} orientation_properties_t;

/**
 * @brief Structure to configure or write LCD
 */
typedef struct {
    uint8_t cmd;        /*!< Command */
    uint32_t databytes; /*!< Number of bytes of data to transmit */
    uint8_t * data;     /*!< Pointer to data or parameters array */
} lcd_cmd_t;

/*
 The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct.
*/
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; // No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} lcd_init_cmd_t;

/* === Private variable declarations =========================================================== */

/* === Private function declarations =========================================================== */

/**
 * @brief  		Set CS pin output state
 * @param[in]  	state: CS pin output state (HIGH / LOW)
 * @retval 		None
 */
void SetChipSelect(uint8_t state);

/**
 * @brief  		Send command and parameters/data to LCD
 * @param[in]  	data: Structure with the command and parameters/data to send
 * @retval 		None
 */
void WriteLCD(lcd_cmd_t * data);

/**
 * @brief  		Define an area of frame memory where MCU can access
 * @param[in]  	x1: Start column
 * @param[in]  	y1: Start row
 * @param[in]  	x2: End column
 * @param[in]  	y2: End row
 * @retval 		None
 */
void SetCursorPosition(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief  		Fill an srea of LCD with a determined color
 * @param[in]  	x1: Start column
 * @param[in]  	y1: Start row
 * @param[in]  	x2: End column
 * @param[in]  	y2: End row
 * @param[in]	color: color
 * @retval 		None
 */
void Fill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* === Public variable definitions ============================================================= */

static spi_device_handle_t spi;

/* === Private variable definitions ============================================================ */

/**
 * @brief Initial LCD configuration parameters
 */
uint8_t pwr_ctrl_a[] = {0x39, 0x2C, 0x00, 0x34, 0x02}; /*!< Default configuration after RST */
uint8_t pwr_ctrl_b[] = {0x00, 0xC1, 0x30};             /*!< Discharge path enable */
uint8_t driv_tim_ctrl_a[] = {0x85, 0x00, 0x78};        /*!< Default configuration after RST */
uint8_t driv_tim_ctrl_b[] = {0x00, 0x00};              /*!< Gate driver timing control: 0 unit */
uint8_t pwr_on_ctrl[] = {0x64, 0x03, 0x12, 0x81};      /*!< CP1 keeps 1 frame, 1st frame enable, vcl = 0,
                                                          ddvdh = 3, vgh =1 , vgl = , DDVDH_ENH = 1 */
uint8_t pump_ratio_ctrl[] = {0x20};                    /*!< DDVDH = 2xVCI */
uint8_t pwr_ctrl1[] = {0x23};                          /*!< GVDD = 4.6V */
uint8_t pwr_ctrl2[] = {0x10};                          /*!< Default configuration after RST */
uint8_t vcom_ctrl1[] = {0x3E, 0x28};                   /*!< VCOMH = 4.25V, VCOML = -1.5V */
uint8_t vcom_ctrl2[] = {0x86};                         /*!< VCOMH = VMH - 58, VCOML = VML - 58 */
uint8_t mem_acc_ctrl[] = {0x48};                       /*!< MY = 0, MX = 1, MV = 0, ML = 0, BGR order, MH = 0 */
uint8_t pixel_format_set[] = {0x55};                   /*!< 16 bits/pixel */
uint8_t frame_ctrl[] = {0x00, 0x18};                   /*!< Frame Rate = 79Hz */
uint8_t disp_fun_ctrl[] = {0x0A, 0x82, 0x27};          /*!< Default configuration after RST */
uint8_t en_3_gamma[] = {0x02};                         /*!< Default configuration after RST */
uint8_t column_addr_set[] = {0x00, 0x00, 0x00, 0xEF};  /*!< Start Column = 0, End Column = 239 */
uint8_t page_addr_set[] = {0x00, 0x00, 0x01, 0x3F};    /*!< Start Page = 0, End Page = 319 */
uint8_t gamma_set[] = {0x01};                          /*!< Default configuration after RST */

uint8_t pos_gamma[] = {
    0x0F, 0X31, 0X2B, 0X0C, 0X0E, 0X08, 0X4E, 0XF1, 0X37, 0X07, 0X10, 0X03, 0X0E, 0X09, 0X00,
}; /*!< Positive gamma correction */

uint8_t neg_gamma[] = {
    0x00, 0X0E, 0X14, 0X03, 0X11, 0X07, 0X31, 0XC1, 0X48, 0X08, 0X0F, 0X0C, 0X31, 0X36, 0X0F,
}; /*!< Negative gamma correction */

/**
 * @brief Initial LCD configuration
 */
lcd_cmd_t lcd_init[] = {
    {PWR_CTRL_A, 5, pwr_ctrl_a},
    {PWR_CTRL_B, 3, pwr_ctrl_b},
    {DRIV_TIM_CTRL_A, 3, driv_tim_ctrl_a},
    {DRIV_TIM_CTRL_B, 2, driv_tim_ctrl_b},
    {PWR_ON_CTRL, 4, pwr_on_ctrl},
    {PUMP_RATIO_CTRL, 1, pump_ratio_ctrl},
    {PWR_CTRL1, 1, pwr_ctrl1},
    {PWR_CTRL2, 1, pwr_ctrl2},
    {VCOM_CTRL1, 2, vcom_ctrl1},
    {VCOM_CTRL2, 1, vcom_ctrl2},
    {MEM_ACC_CTRL, 1, mem_acc_ctrl},
    {PIXEL_FORMAT_SET, 1, pixel_format_set},
    {FRAME_CTRL, 2, frame_ctrl},
    {DISP_FUN_CTRL, 3, disp_fun_ctrl},
    {EN_3_GAMMA, 1, en_3_gamma},
    {COLUMN_ADDR_SET, 4, column_addr_set},
    {PAGE_ADDR_SET, 4, page_addr_set},
    {GAMMA_SET, 1, gamma_set},
    {POS_GAMMA, 15, pos_gamma},
    {NEG_GAMMA, 15, neg_gamma},
};

lcd_cmd_t lcd_reset = {RESET, 0, NULL};         /*!< SW reset */
lcd_cmd_t lcd_sleep_out = {SLEEP_OUT, 0, NULL}; /*!< Exit sleep mode */
lcd_cmd_t lcd_on = {DISPLAY_ON, 0, NULL};       /*!< Exit sleep mode */

orientation_properties_t lcd_orientation = {
    ILI9341_WIDTH,
    ILI9341_HEIGHT,
    ILI9341_Portrait_1,
}; /*!< Default orientation configuration */

/* === Private function definitions ============================================================ */

/* Send a command to the LCD. Uses spi_device_polling_transmit, which waits
 * until the transfer is complete.
 *
 * Since command transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
void lcd_cmd(const uint8_t cmd, bool keep_cs_active) {
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t)); // Zero out the transaction
    t.length = 8;             // Command is 8 bits
    t.tx_buffer = &cmd;       // The data is the cmd itself
    t.user = (void *)0;       // D/C needs to be set to 0
    if (keep_cs_active) {
        t.flags = SPI_TRANS_CS_KEEP_ACTIVE; // Keep CS active after data transfer
    }
    ret = spi_device_polling_transmit(spi, &t); // Transmit!
    assert(ret == ESP_OK);                      // Should have had no issues.
}

/* Send data to the LCD. Uses spi_device_polling_transmit, which waits until the
 * transfer is complete.
 *
 * Since data transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
void lcd_data(const uint8_t * data, int len) {
    esp_err_t ret;
    spi_transaction_t t;
    if (len == 0) {
        return; // no need to send anything
    }
    memset(&t, 0, sizeof(t));                   // Zero out the transaction
    t.length = len * 8;                         // Len is in bytes, transaction length is in bits.
    t.tx_buffer = data;                         // Data
    t.user = (void *)1;                         // D/C needs to be set to 1
    ret = spi_device_polling_transmit(spi, &t); // Transmit!
    assert(ret == ESP_OK);                      // Should have had no issues.
}

// This function is called (in irq context!) just before a transmission starts. It will
// set the D/C line to the value indicated in the user field.
void lcd_spi_pre_transfer_callback(spi_transaction_t * t) {
    int dc = (int)t->user;
    gpio_set_level(ILI9341_PIN_NUM_DC, dc);
}

void spi_config() {
    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .miso_io_num = ILI9341_PIN_NUM_MISO,
        .mosi_io_num = ILI9341_PIN_NUM_MOSI,
        .sclk_io_num = ILI9341_PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PARALLEL_LINES * 320 * 2 + 8,
    };

    spi_device_interface_config_t devcfg = {
#ifdef CONFIG_LCD_OVERCLOCK
        .clock_speed_hz = 26 * 1000 * 1000, // Clock out at 26 MHz
#else
        .clock_speed_hz = 10 * 1000 * 1000, // Clock out at 10 MHz
#endif
        .mode = 0,                               // SPI mode 0
        .spics_io_num = ILI9341_PIN_NUM_CS,      // CS pin
        .queue_size = 7,                         // We want to be able to queue 7 transactions at a time
        .pre_cb = lcd_spi_pre_transfer_callback, // Specify pre-transfer callback to handle D/C line
    };

    // Initialize the SPI bus
    ret = spi_bus_initialize(ILI9341_SPI_PORT, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    // Attach the LCD to the SPI bus
    ret = spi_bus_add_device(ILI9341_SPI_PORT, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
}

void WriteLCD(lcd_cmd_t * data) {
    /* If command is NULL don't send command */
    if (data->cmd != 0) {
        /* Send command */
        lcd_cmd(data->cmd, false);
    }
    /* If there are parameters or data to send */
    if (data->databytes != 0) {
        /* Send parameters or data */
        lcd_data(data->data, data->databytes);
    }
}

void SetCursorPosition(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    static uint16_t aux;
    /* The lower column must be send first */
    if (x0 > x1) {
        aux = x0;
        x0 = x1;
        x1 = aux;
    }
    /* The lower row must be send first */
    if (y0 > y1) {
        aux = y0;
        y0 = y1;
        y1 = aux;
    }
    uint8_t columns[] = {HighByte(x0), LowByte(x0), HighByte(x1), LowByte(x1)};
    lcd_cmd_t lcd_columns = {COLUMN_ADDR_SET, 4, columns};
    uint8_t rows[] = {HighByte(y0), LowByte(y0), HighByte(y1), LowByte(y1)};
    lcd_cmd_t lcd_rows = {PAGE_ADDR_SET, 4, rows};
    WriteLCD(&lcd_columns);
    WriteLCD(&lcd_rows);
}

void Fill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    static uint16_t i;
    static int32_t bytes_count;
    static int16_t x_dist, y_dist;
    static uint8_t pixel[MAX_VALUE_SIZE];

    x_dist = x1 - x0;
    y_dist = y1 - y0;
    if (x0 > x1) {
        x_dist = -x_dist;
    }
    if (y0 > y1) {
        y_dist = -y_dist;
    }
    /* Number of bytes to write. We have to write 2 bytes/pixel (16bits color) */
    bytes_count = (x_dist + 1) * (y_dist + 1) * 2;
    /* Define area to fill */
    SetCursorPosition(x0, y0, x1, y1);

    for (i = 0; i < MAX_VALUE_SIZE; i += 2) {
        pixel[i] = HighByte(color);
        pixel[i + 1] = LowByte(color);
    }
    /* Start writing LCD memory */
    lcd_cmd_t lcd_write = {MEM_WRITE, 0, NULL};
    WriteLCD(&lcd_write);

    while (bytes_count - MAX_VALUE_SIZE > 0) {
        lcd_cmd_t lcd_pixel = {SEND_PIXELS, MAX_VALUE_SIZE, pixel};
        WriteLCD(&lcd_pixel);
        bytes_count -= MAX_VALUE_SIZE;
    }
    lcd_cmd_t lcd_pixel = {SEND_PIXELS, bytes_count, pixel};
    WriteLCD(&lcd_pixel);
}

/* === Public function implementation ========================================================== */

void ILI9341Init(void) {
    spi_config();

    // Initialize non-SPI GPIOs
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask =
        ((1ULL << ILI9341_PIN_NUM_DC) | (1ULL << ILI9341_PIN_NUM_RST) | (1ULL << ILI9341_PIN_NUM_BCKL));
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = true;
    gpio_config(&io_conf);

    /* Reset the display */
    gpio_set_level(ILI9341_PIN_NUM_RST, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(ILI9341_PIN_NUM_RST, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    /* It will be necessary to wait 5msec before sending new command following software reset */
    WriteLCD(&lcd_reset);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    /* Send initial configuration to LCD */
    for (uint8_t i = 0; i < sizeof(lcd_init) / sizeof(lcd_cmd_t); i++) {
        WriteLCD(&lcd_init[i]);
    }
    /* It will be necessary to wait 5msec before sending next command after sleep out */
    WriteLCD(&lcd_sleep_out);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    WriteLCD(&lcd_on);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    /* Enable backlight */
    gpio_set_level(ILI9341_PIN_NUM_BCKL, ILI9341_BK_LIGHT_ON_LEVEL);

    /* Start screen on White */
    ILI9341Fill(ILI9341_BLACK);
}

void ILI9341DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
    /* Define area (pixel) to fill */
    SetCursorPosition(x, y, x, y);
    uint8_t pixels[] = {HighByte(color), LowByte(color)};
    lcd_cmd_t lcd_pixels = {MEM_WRITE, sizeof(pixels), pixels};
    WriteLCD(&lcd_pixels);
}

void ILI9341Fill(uint16_t color) {
    Fill(0, 0, lcd_orientation.width, lcd_orientation.height, color);
}

void ILI9341Rotate(ili9341_orientation_t orientation) {
    uint8_t mem_acc[1];
    switch (orientation) {
    case ILI9341_Portrait_1:
        mem_acc[0] = 0x48; /*!< Row Address Order (MY) = 0, Column Address Order (MX) = 1,
                              Row/Column Exchange (MV) = 0 */
        lcd_orientation.width = ILI9341_WIDTH;
        lcd_orientation.height = ILI9341_HEIGHT;
        lcd_orientation.orientation = ILI9341_Portrait_1;
        break;

    case ILI9341_Portrait_2:
        mem_acc[0] = 0x88; /*!< Row Address Order (MY) = 1, Column Address Order (MX) = 1,
                              Row/Column Exchange (MV) = 0 */
        lcd_orientation.width = ILI9341_WIDTH;
        lcd_orientation.height = ILI9341_HEIGHT;
        lcd_orientation.orientation = ILI9341_Portrait_2;
        break;

    case ILI9341_Landscape_1:
        mem_acc[0] = 0x28; /*!< Row Address Order (MY) = 0, Column Address Order (MX) = 0,
                              Row/Column Exchange (MV) = 1 */
        lcd_orientation.width = ILI9341_HEIGHT;
        lcd_orientation.height = ILI9341_WIDTH;
        lcd_orientation.orientation = ILI9341_Landscape_1;
        break;

    case ILI9341_Landscape_2:
        mem_acc[0] = 0xE8; /*!< Row Address Order (MY) = 1, Column Address Order (MX) = 1,
                              Row/Column Exchange (MV) = 1 */
        lcd_orientation.width = ILI9341_HEIGHT;
        lcd_orientation.height = ILI9341_WIDTH;
        lcd_orientation.orientation = ILI9341_Landscape_2;
        break;
    }
    lcd_cmd_t lcd_mem_acc = {MEM_ACC_CTRL, 1, mem_acc};
    WriteLCD(&lcd_mem_acc);
}

void ILI9341DrawChar(uint16_t x, uint16_t y, char data, Font_t * font, uint16_t foreground, uint16_t background) {
    static uint16_t i, j, k;
    static uint16_t char_row;
    static uint16_t lcd_x, lcd_y;
    static int32_t bytes_count;
    static uint8_t pixel[MAX_VALUE_SIZE];

    /* Set coordinates */
    lcd_x = x;
    lcd_y = y;

    /* If at the end of a line of display, go to new line and set x to 0 position */
    if ((lcd_x + font->FontWidth) > lcd_orientation.width) {
        lcd_y += font->FontHeight;
        lcd_x = 0;
    }

    SetCursorPosition(lcd_x, lcd_y, lcd_x + font->FontWidth - 1, lcd_y + font->FontHeight - 1);

    /* Number of bytes to write. We have to write 2 bytes/pixel */
    bytes_count = font->FontHeight * font->FontWidth * 2;

    /* Start writing LCD memory */
    lcd_cmd_t lcd_write = {MEM_WRITE, 0, NULL};
    WriteLCD(&lcd_write);

    /* Draw font data */
    /* go through character rows */
    k = 0;
    for (i = 0; i < font->FontHeight; i++) {
        /* each 16bits data of a font character draws a full row of that character */
        char_row = font->data[(data - ' ') * font->FontHeight + i];
        /* go through character columns */
        for (j = 0; j < font->FontWidth; j++) {
            /* If exceed buffer size, send buffer */
            if ((2 * j + i * font->FontWidth * 2 - k * MAX_VALUE_SIZE + 1) > MAX_VALUE_SIZE) {
                lcd_cmd_t lcd_pixels = {SEND_PIXELS, MAX_VALUE_SIZE, pixel};
                WriteLCD(&lcd_pixels);
                bytes_count -= MAX_VALUE_SIZE;
                k++;
            }
            /* The n=FontWidth first bits of the 16bits row data draws the corresponding part of a
             * character */
            if (char_row & (MSK_BIT16 >> j)) {
                /* if bit = 1, draw put foreground color */
                pixel[2 * j + i * font->FontWidth * 2 - k * MAX_VALUE_SIZE] = HighByte(foreground);
                pixel[2 * j + i * font->FontWidth * 2 - k * MAX_VALUE_SIZE + 1] = LowByte(foreground);
            } else {
                pixel[2 * j + i * font->FontWidth * 2 - k * MAX_VALUE_SIZE] = HighByte(background);
                pixel[2 * j + i * font->FontWidth * 2 - k * MAX_VALUE_SIZE + 1] = LowByte(background);
            }
        }
    }
    /* Send the rest of the buffer */
    lcd_cmd_t lcd_pixels = {SEND_PIXELS, bytes_count, pixel};
    WriteLCD(&lcd_pixels);
}

void ILI9341DrawString(uint16_t x, uint16_t y, char * str, Font_t * font, uint16_t foreground, uint16_t background) {
    static uint16_t lcd_x, lcd_y;

    /* Set coordinates */
    lcd_x = x;
    lcd_y = y;

    while (*str != '\0') /* End of string */
    {
        /* New line */
        if (*str == '\n') {
            lcd_y += font->FontHeight + 1;
            /* if after \n is also \r, than go to the left of the screen */
            if (*(str + 1) == '\r') {
                lcd_x = 0;
                str++;
            } else {
                lcd_x = x;
            }
            str++;
        } else if (*str == '\r') {
            str++;
        }

        /* Put character to LCD */
        ILI9341DrawChar(lcd_x, lcd_y, *str, font, foreground, background);
        /* Next character */
        str++;
        lcd_x += font->FontWidth;
    }
}

void ILI9341GetStringSize(char * str, Font_t * font, uint16_t * width, uint16_t * height) {
    static uint16_t w;

    *height = font->FontHeight;
    w = 0;
    while (*str != '\0') /* End of string */
    {
        w += font->FontWidth;
        str++;
    }
    *width = w;
}

void ILI9341DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    static int16_t x_dist, y_dist, x_grow, y_grow, error, error_2;

    /* Check for overflow */
    if (x0 >= lcd_orientation.width) {
        x0 = lcd_orientation.width - 1;
    }
    if (x1 >= lcd_orientation.width) {
        x1 = lcd_orientation.width - 1;
    }
    if (y0 >= lcd_orientation.height) {
        y0 = lcd_orientation.height - 1;
    }
    if (y1 >= lcd_orientation.height) {
        y1 = lcd_orientation.height - 1;
    }

    /* Calculate x y distances and determine grow direction */
    x_dist = x1 - x0;
    y_dist = y1 - y0;
    if (x0 > x1) {
        x_dist = -x_dist;
        x_grow = LEFT;
    } else {
        x_grow = RIGHT;
    }
    if (y0 > y1) {
        y_dist = -y_dist;
        y_grow = UP;
    } else {
        y_grow = DOWN;
    }

    /* Vertical or horizontal line */
    if (x_dist == 0 || y_dist == 0) {
        Fill(x0, y0, x1, y1, color);
    }
    /* Diagonal line */
    else {
        if (x_dist > y_dist) {
            error = x_dist / 2;
        } else {
            error = y_dist / 2;
        }

        while (1) {
            /* Draw start point */
            ILI9341DrawPixel(x0, y0, color);
            /* Loop ends when start point reaches end point */
            if (x0 == x1 || y0 == y1) {
                break;
            }
            error_2 = error;
            /* Determine if line must grow in x direction */
            if (error_2 > -x_dist) {
                error -= y_dist;
                x0 += x_grow; /* Move start point */
            }
            /* Determine if line must grow in y direction */
            if (error_2 < y_dist) {
                error += x_dist;
                y0 += y_grow; /* Move start point */
            }
        }
    }
}

void ILI9341DrawRectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    ILI9341DrawLine(x0, y0, x1, y0, color); /* Draw top line */
    ILI9341DrawLine(x1, y0, x1, y1, color); /* Draw right line */
    ILI9341DrawLine(x0, y1, x1, y1, color); /* Draw bottom line */
    ILI9341DrawLine(x0, y0, x0, y1, color); /* Draw left line */
}

void ILI9341DrawFilledRectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    Fill(x0, y0, x1, y1, color);
}

void ILI9341DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    static int16_t f, ddF_x, ddF_y, x, y;

    f = 1 - r;
    ddF_x = 1;
    ddF_y = -2 * r;
    x = 0;
    y = r;

    ILI9341DrawPixel(x0, y0 + r, color);
    ILI9341DrawPixel(x0, y0 - r, color);
    ILI9341DrawPixel(x0 + r, y0, color);
    ILI9341DrawPixel(x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        ILI9341DrawPixel(x0 + x, y0 + y, color);
        ILI9341DrawPixel(x0 - x, y0 + y, color);
        ILI9341DrawPixel(x0 + x, y0 - y, color);
        ILI9341DrawPixel(x0 - x, y0 - y, color);

        ILI9341DrawPixel(x0 + y, y0 + x, color);
        ILI9341DrawPixel(x0 - y, y0 + x, color);
        ILI9341DrawPixel(x0 + y, y0 - x, color);
        ILI9341DrawPixel(x0 - y, y0 - x, color);
    }
}

void ILI9341DrawFilledCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    static int16_t f, ddF_x, ddF_y, x, y;

    f = 1 - r;
    ddF_x = 1;
    ddF_y = -2 * r;
    x = 0;
    y = r;

    ILI9341DrawPixel(x0, y0 + r, color);
    ILI9341DrawPixel(x0, y0 - r, color);
    ILI9341DrawPixel(x0 + r, y0, color);
    ILI9341DrawPixel(x0 - r, y0, color);
    ILI9341DrawLine(x0 - r, y0, x0 + r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        ILI9341DrawLine(x0 - x, y0 + y, x0 + x, y0 + y, color);
        ILI9341DrawLine(x0 + x, y0 - y, x0 - x, y0 - y, color);

        ILI9341DrawLine(x0 + y, y0 + x, x0 - y, y0 + x, color);
        ILI9341DrawLine(x0 + y, y0 - x, x0 - y, y0 - x, color);
    }
}

void ILI9341DrawPicture(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t * pic) {
    static uint16_t i, j;
    static int32_t bytes_count;
    static uint8_t pixel[MAX_VALUE_SIZE];

    SetCursorPosition(x, y, x + width - 1, y + height - 1);

    /* Number of bytes to write. We have to write 2 bytes/pixel */
    bytes_count = width * height * 2;

    /* Start writing LCD memory */
    lcd_cmd_t lcd_write = {MEM_WRITE, 0, NULL};
    WriteLCD(&lcd_write);

    j = 0;
    while (bytes_count - MAX_VALUE_SIZE > 0) {
        for (i = 0; i < MAX_VALUE_SIZE; i++) {
            pixel[i] = pic[j * MAX_VALUE_SIZE + i];
        }
        lcd_cmd_t lcd_pixel = {SEND_PIXELS, MAX_VALUE_SIZE, pixel};
        WriteLCD(&lcd_pixel);
        bytes_count -= MAX_VALUE_SIZE;
        j++;
    }
    for (i = 0; i < bytes_count; i++) {
        pixel[i] = pic[j * MAX_VALUE_SIZE + i];
    }
    lcd_cmd_t lcd_pixel = {SEND_PIXELS, bytes_count, pixel};
    WriteLCD(&lcd_pixel);
}

/* === End of documentation ==================================================================== */
