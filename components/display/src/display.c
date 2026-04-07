#include "display.h"

#ifdef ESP_PLATFORM

#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "font8x16.h"

#include <string.h>

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel;

#if defined(BOARD_TDONGLE_S3)

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "logo_rgb565.h"

#define LCD_SPI_HOST    SPI2_HOST
#define LCD_PIXEL_CLK   20000000

// ST7735 expects big-endian RGB565 over SPI; ESP32 is little-endian
#define SWAP16(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))

// Store the IO handle for sending vendor-specific commands after init
static esp_lcd_panel_io_handle_t s_panel_io;

// ST7735-specific vendor init commands (from official LilyGo T-Dongle S3 repo).
// Sent after esp_lcd_panel_init() which handles basic ST7789-compatible setup.
static esp_err_t st7735_vendor_init(void)
{
    // Frame rate control (normal/idle/partial)
    uint8_t frmctr1[] = {0x05, 0x3A, 0x3A};
    uint8_t frmctr2[] = {0x05, 0x3A, 0x3A};
    uint8_t frmctr3[] = {0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xB1, frmctr1, sizeof(frmctr1)), TAG, "frmctr1");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xB2, frmctr2, sizeof(frmctr2)), TAG, "frmctr2");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xB3, frmctr3, sizeof(frmctr3)), TAG, "frmctr3");

    // Display inversion control
    uint8_t invctr[] = {0x03};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xB4, invctr, sizeof(invctr)), TAG, "invctr");

    // Power control
    uint8_t pwctr1[] = {0x62, 0x02, 0x04};
    uint8_t pwctr2[] = {0xC0};
    uint8_t pwctr3[] = {0x0D, 0x00};
    uint8_t pwctr4[] = {0x8D, 0x6A};
    uint8_t pwctr5[] = {0x8D, 0xEE};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xC0, pwctr1, sizeof(pwctr1)), TAG, "pwctr1");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xC1, pwctr2, sizeof(pwctr2)), TAG, "pwctr2");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xC2, pwctr3, sizeof(pwctr3)), TAG, "pwctr3");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xC3, pwctr4, sizeof(pwctr4)), TAG, "pwctr4");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xC4, pwctr5, sizeof(pwctr5)), TAG, "pwctr5");

    // VCOM control
    uint8_t vmctr1[] = {0x0E};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xC5, vmctr1, sizeof(vmctr1)), TAG, "vmctr1");

    // Positive gamma
    uint8_t gmctrp1[] = {0x10, 0x0E, 0x02, 0x03, 0x0E, 0x07, 0x02, 0x07,
                         0x0A, 0x12, 0x27, 0x37, 0x00, 0x0D, 0x0E, 0x10};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xE0, gmctrp1, sizeof(gmctrp1)), TAG, "gmctrp1");

    // Negative gamma
    uint8_t gmctrn1[] = {0x10, 0x0E, 0x03, 0x03, 0x0F, 0x06, 0x02, 0x08,
                         0x0A, 0x13, 0x26, 0x36, 0x00, 0x0D, 0x0E, 0x10};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_panel_io, 0xE1, gmctrn1, sizeof(gmctrn1)), TAG, "gmctrn1");

    return ESP_OK;
}

static esp_err_t init_st7735(void)
{
    gpio_set_direction(PIN_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_BL, 1);  // active-low: 1 = off during init

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_LCD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLK,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                                  &io_cfg, &s_panel_io),
                        TAG, "spi io init");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_panel_io, &panel_cfg, &s_panel),
                        TAG, "st7789 panel init");

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    // Send ST7735-specific vendor commands (gamma, power, frame rate)
    ESP_ERROR_CHECK(st7735_vendor_init());

    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_X, LCD_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    gpio_set_level(PIN_LCD_BL, 0);  // active-low: 0 = on
    ESP_LOGI(TAG, "ST7735 ready (%dx%d)", LCD_WIDTH, LCD_HEIGHT);
    return ESP_OK;
}

static esp_err_t clear_st7735(uint16_t color)
{
    static uint16_t s_line[LCD_WIDTH];
    uint16_t swapped = SWAP16(color);
    for (int x = 0; x < LCD_WIDTH; x++) {
        s_line[x] = swapped;
    }
    for (int y = 0; y < LCD_HEIGHT; y++) {
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_WIDTH, y + 1, s_line),
            TAG, "clear line");
    }
    return ESP_OK;
}

static esp_err_t draw_text_st7735(int x, int y, const char *text, uint16_t fg, uint16_t bg)
{
    static uint16_t s_char_buf[FONT_W * FONT_H];

    for (int ci = 0; text[ci] != '\0'; ci++) {
        uint8_t ch = (uint8_t)text[ci];
        if (ch < 0x20 || ch > 0x7E) ch = 0x20;
        const uint8_t *glyph = g_font8x16[ch - 0x20];

        for (int row = 0; row < FONT_H; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_W; col++) {
                s_char_buf[row * FONT_W + col] = (bits & (0x80 >> col)) ? SWAP16(fg) : SWAP16(bg);
            }
        }

        int cx = x + ci * FONT_W;
        if (cx + FONT_W > LCD_WIDTH) break;
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, cx, y, cx + FONT_W, y + FONT_H, s_char_buf),
            TAG, "draw char");
    }
    return ESP_OK;
}

static esp_err_t draw_logo_st7735(int x, int y)
{
    // Byte-swap logo row into temp buffer before sending
    static uint16_t s_logo_row[LOGO_W];
    for (int row = 0; row < LOGO_H; row++) {
        const uint16_t *src = &g_logo_data[row * LOGO_W];
        for (int col = 0; col < LOGO_W; col++) {
            s_logo_row[col] = SWAP16(src[col]);
        }
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, x, y + row,
                                      x + LOGO_W, y + row + 1,
                                      s_logo_row),
            TAG, "draw logo row");
    }
    return ESP_OK;
}

// 2x scaled text: each font pixel becomes a 2x2 block
static esp_err_t draw_text_2x_st7735(int x, int y, const char *text,
                                      uint16_t fg, uint16_t bg)
{
    static uint16_t s_char2x[FONT_W * 2 * FONT_H * 2];
    uint16_t fg_s = SWAP16(fg);
    uint16_t bg_s = SWAP16(bg);
    int stride = FONT_W * 2;

    for (int ci = 0; text[ci] != '\0'; ci++) {
        uint8_t ch = (uint8_t)text[ci];
        if (ch < 0x20 || ch > 0x7E) ch = 0x20;
        const uint8_t *glyph = g_font8x16[ch - 0x20];

        for (int row = 0; row < FONT_H; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_W; col++) {
                uint16_t px = (bits & (0x80 >> col)) ? fg_s : bg_s;
                s_char2x[(row * 2) * stride + col * 2] = px;
                s_char2x[(row * 2) * stride + col * 2 + 1] = px;
                s_char2x[(row * 2 + 1) * stride + col * 2] = px;
                s_char2x[(row * 2 + 1) * stride + col * 2 + 1] = px;
            }
        }

        int cx = x + ci * FONT_W * 2;
        if (cx + FONT_W * 2 > LCD_WIDTH) break;
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, cx, y,
                                      cx + FONT_W * 2, y + FONT_H * 2,
                                      s_char2x),
            TAG, "draw char 2x");
    }
    return ESP_OK;
}

static esp_err_t show_splash_st7735(void)
{
    ESP_RETURN_ON_ERROR(clear_st7735(DISPLAY_COLOR_BLACK), TAG, "clear");

    // Logo on left, vertically centered
    int logo_x = 2;
    int logo_y = (LCD_HEIGHT - LOGO_H) / 2;
    ESP_RETURN_ON_ERROR(draw_logo_st7735(logo_x, logo_y), TAG, "draw logo");

    // "TaipanMiner" text to the right of logo, vertically centered
    int text_x = logo_x + LOGO_W + 4;
    int text_y = (LCD_HEIGHT - FONT_H) / 2;
    ESP_RETURN_ON_ERROR(
        draw_text_st7735(text_x, text_y, "TaipanMiner",
                         DISPLAY_COLOR_AMBER, DISPLAY_COLOR_BLACK),
        TAG, "draw splash text");

    return ESP_OK;
}

static esp_err_t show_prov_st7735(const char *ssid, const char *password)
{
    ESP_RETURN_ON_ERROR(clear_st7735(DISPLAY_COLOR_BLACK), TAG, "clear");

    ESP_RETURN_ON_ERROR(
        draw_text_st7735(0, 4, "WiFi Setup",
                         DISPLAY_COLOR_YELLOW, DISPLAY_COLOR_BLACK),
        TAG, "header");

    ESP_RETURN_ON_ERROR(
        draw_text_st7735(0, 24, "SSID:",
                         DISPLAY_COLOR_CYAN, DISPLAY_COLOR_BLACK),
        TAG, "ssid label");
    ESP_RETURN_ON_ERROR(
        draw_text_st7735(0, 40, ssid,
                         DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK),
        TAG, "ssid value");

    ESP_RETURN_ON_ERROR(
        draw_text_st7735(0, 56, "Pass:",
                         DISPLAY_COLOR_CYAN, DISPLAY_COLOR_BLACK),
        TAG, "pass label");
    ESP_RETURN_ON_ERROR(
        draw_text_st7735(40, 56, password,
                         DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK),
        TAG, "pass value");

    return ESP_OK;
}

#elif defined(BOARD_BITAXE_601)

#include "asic.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ssd1306.h"

#define OLED_WIDTH   128
#define OLED_HEIGHT  32

static esp_err_t init_ssd1306(void)
{
    i2c_master_bus_handle_t i2c_bus = asic_get_i2c_bus();

    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = SSD1306_I2C_ADDR,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .scl_speed_hz = I2C_BUS_SPEED_HZ,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(i2c_bus, &io_cfg, &io),
                        TAG, "i2c io init");

    esp_lcd_panel_ssd1306_config_t ssd1306_cfg = {
        .height = OLED_HEIGHT,
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .bits_per_pixel = 1,
        .vendor_config = &ssd1306_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &s_panel),
                        TAG, "ssd1306 panel init");

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_LOGI(TAG, "SSD1306 ready (%dx%d)", OLED_WIDTH, OLED_HEIGHT);
    return ESP_OK;
}

static esp_err_t clear_ssd1306(uint16_t color)
{
    static uint8_t s_fb[OLED_WIDTH * (OLED_HEIGHT / 8)];
    memset(s_fb, color ? 0xFF : 0x00, sizeof(s_fb));
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, OLED_WIDTH, OLED_HEIGHT, s_fb);
    return ESP_OK;
}

#endif

// --- Public API ---

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "initializing display");

#if defined(BOARD_TDONGLE_S3)
    ESP_RETURN_ON_ERROR(init_st7735(), TAG, "st7735 init");
#elif defined(BOARD_BITAXE_601)
    ESP_RETURN_ON_ERROR(init_ssd1306(), TAG, "ssd1306 init");
#else
    ESP_LOGW(TAG, "no display configured for this board");
#endif

    return ESP_OK;
}

esp_err_t display_clear(uint16_t color)
{
#if defined(BOARD_TDONGLE_S3)
    return clear_st7735(color);
#elif defined(BOARD_BITAXE_601)
    return clear_ssd1306(color);
#else
    (void)color;
    return ESP_OK;
#endif
}

esp_err_t display_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg)
{
#if defined(BOARD_TDONGLE_S3)
    return draw_text_st7735(x, y, text, fg, bg);
#else
    (void)x; (void)y; (void)text; (void)fg; (void)bg;
    return ESP_OK;
#endif
}

esp_err_t display_show_splash(void)
{
#if defined(BOARD_TDONGLE_S3)
    return show_splash_st7735();
#else
    return ESP_OK;
#endif
}

esp_err_t display_show_prov(const char *ssid, const char *password)
{
#if defined(BOARD_TDONGLE_S3)
    return show_prov_st7735(ssid, password);
#else
    (void)ssid; (void)password;
    return ESP_OK;
#endif
}

#endif
