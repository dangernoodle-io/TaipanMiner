#include "display.h"

#ifdef ESP_PLATFORM

#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "font8x16.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel;

// Shared formatting helpers
static void fmt_hashrate(double hr, char *buf, size_t len)
{
    const char *unit;
    if (hr >= 1e12) { hr /= 1e12; unit = "TH/s"; }
    else if (hr >= 1e9) { hr /= 1e9; unit = "GH/s"; }
    else if (hr >= 1e6) { hr /= 1e6; unit = "MH/s"; }
    else if (hr >= 1e3) { hr /= 1e3; unit = "kH/s"; }
    else { unit = "H/s"; }
    snprintf(buf, len, "%.1f %s", hr, unit);
}

static void fmt_uptime(int64_t us, char *buf, size_t len)
{
    int secs = (int)(us / 1000000);
    int days = secs / 86400;
    int hours = (secs % 86400) / 3600;
    int mins = (secs % 3600) / 60;

    if (days > 0) snprintf(buf, len, "%dd %dh", days, hours);
    else if (hours > 0) snprintf(buf, len, "%dh %dm", hours, mins);
    else snprintf(buf, len, "%dm %ds", mins, secs % 60);
}

#if defined(BOARD_TDONGLE_S3)

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "logo_bits.h"

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

// Double-buffer to avoid DMA race: spi_device_queue_trans returns before
// DMA completes, but drains inflight at the start of the next tx_color call.
// By alternating buffers, the previous buffer's DMA is always drained before reuse.
static esp_err_t draw_text_st7735(int x, int y, const char *text,
                                   uint16_t fg, uint16_t bg, bool bold)
{
    static uint16_t s_char_buf[2][FONT_W * FONT_H];
    static int s_buf_idx;
    uint16_t fg_s = SWAP16(fg);
    uint16_t bg_s = SWAP16(bg);

    for (int ci = 0; text[ci] != '\0'; ci++) {
        uint8_t ch = (uint8_t)text[ci];
        if (ch < 0x20 || ch > 0x7E) ch = 0x20;
        const uint8_t *glyph = g_font8x16[ch - 0x20];

        uint16_t *buf = s_char_buf[s_buf_idx];
        for (int row = 0; row < FONT_H; row++) {
            uint8_t bits = glyph[row];
            if (bold) bits |= (bits >> 1);
            for (int col = 0; col < FONT_W; col++) {
                buf[row * FONT_W + col] = (bits & (0x80 >> col)) ? fg_s : bg_s;
            }
        }

        int cx = x + ci * FONT_W;
        if (cx + FONT_W > LCD_WIDTH) break;
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, cx, y, cx + FONT_W, y + FONT_H, buf),
            TAG, "draw char");
        s_buf_idx ^= 1;
    }
    return ESP_OK;
}

static esp_err_t draw_logo_st7735(int x, int y)
{
    // Double-buffer to avoid DMA race (same pattern as draw_text)
    static uint16_t s_logo_row[2][LOGO_W];
    int buf_idx = 0;
    for (int row = 0; row < LOGO_H; row++) {
        uint16_t *buf = s_logo_row[buf_idx];
        for (int col = 0; col < LOGO_W; col++) {
            int idx = row * LOGO_W + col;
            bool fg = (g_logo_bits[idx / 8] >> (7 - (idx % 8))) & 1;
            buf[col] = fg ? SWAP16(LOGO_FG_COLOR) : 0x0000;
        }
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, x, y + row,
                                      x + LOGO_W, y + row + 1,
                                      buf),
            TAG, "draw logo row");
        buf_idx ^= 1;
    }
    return ESP_OK;
}

// 2x scaled text: each font pixel becomes a 2x2 block
static esp_err_t draw_text_2x_st7735(int x, int y, const char *text,
                                      uint16_t fg, uint16_t bg)
{
    static uint16_t s_char2x[2][FONT_W * 2 * FONT_H * 2];
    static int s_buf2x_idx;
    uint16_t fg_s = SWAP16(fg);
    uint16_t bg_s = SWAP16(bg);
    int stride = FONT_W * 2;

    for (int ci = 0; text[ci] != '\0'; ci++) {
        uint8_t ch = (uint8_t)text[ci];
        if (ch < 0x20 || ch > 0x7E) ch = 0x20;
        const uint8_t *glyph = g_font8x16[ch - 0x20];

        uint16_t *buf = s_char2x[s_buf2x_idx];
        for (int row = 0; row < FONT_H; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_W; col++) {
                uint16_t px = (bits & (0x80 >> col)) ? fg_s : bg_s;
                buf[(row * 2) * stride + col * 2] = px;
                buf[(row * 2) * stride + col * 2 + 1] = px;
                buf[(row * 2 + 1) * stride + col * 2] = px;
                buf[(row * 2 + 1) * stride + col * 2 + 1] = px;
            }
        }

        int cx = x + ci * FONT_W * 2;
        if (cx + FONT_W * 2 > LCD_WIDTH) break;
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, cx, y,
                                      cx + FONT_W * 2, y + FONT_H * 2,
                                      buf),
            TAG, "draw char 2x");
        s_buf2x_idx ^= 1;
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
                         DISPLAY_COLOR_AMBER, DISPLAY_COLOR_BLACK, false),
        TAG, "draw splash text");

    return ESP_OK;
}

static esp_err_t show_prov_st7735(const char *ssid, const char *password)
{
    ESP_RETURN_ON_ERROR(clear_st7735(DISPLAY_COLOR_BLACK), TAG, "clear");

    ESP_RETURN_ON_ERROR(
        draw_text_st7735(0, 4, "WiFi Setup",
                         DISPLAY_COLOR_YELLOW, DISPLAY_COLOR_BLACK, false),
        TAG, "header");

    ESP_RETURN_ON_ERROR(
        draw_text_st7735(0, 24, "SSID:",
                         DISPLAY_COLOR_CYAN, DISPLAY_COLOR_BLACK, false),
        TAG, "ssid label");
    ESP_RETURN_ON_ERROR(
        draw_text_st7735(0, 40, ssid,
                         DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK, false),
        TAG, "ssid value");

    ESP_RETURN_ON_ERROR(
        draw_text_st7735(0, 56, "Pass:",
                         DISPLAY_COLOR_CYAN, DISPLAY_COLOR_BLACK, false),
        TAG, "pass label");
    ESP_RETURN_ON_ERROR(
        draw_text_st7735(40, 56, password,
                         DISPLAY_COLOR_WHITE, DISPLAY_COLOR_BLACK, false),
        TAG, "pass value");

    return ESP_OK;
}

// Render one pixel row of a text string into a line buffer.
// Fills pixels from x to x+(strlen*8), leaving other pixels untouched.
static void render_text_row(uint16_t *line, int x, const char *text,
                            int font_row, uint16_t fg_s, uint16_t bg_s)
{
    for (int ci = 0; text[ci] != '\0'; ci++) {
        uint8_t ch = (uint8_t)text[ci];
        if (ch < 0x20 || ch > 0x7E) ch = 0x20;
        uint8_t bits = g_font8x16[ch - 0x20][font_row];
        int cx = x + ci * FONT_W;
        if (cx + FONT_W > LCD_WIDTH) break;
        for (int col = 0; col < FONT_W; col++) {
            line[cx + col] = (bits & (0x80 >> col)) ? fg_s : bg_s;
        }
    }
}

// Render one pixel row of the logo into a line buffer at the given x offset.
static void render_logo_row_buf(uint16_t *line, int logo_x, int logo_row)
{
    for (int col = 0; col < LOGO_W; col++) {
        int idx = logo_row * LOGO_W + col;
        bool fg = (g_logo_bits[idx / 8] >> (7 - (idx % 8))) & 1;
        line[logo_x + col] = fg ? SWAP16(LOGO_FG_COLOR) : 0x0000;
    }
}

// Render one pixel row of page content into a line buffer.
// page: 0=splash, 1=stats. content_y: 0-79.
// stat_text: pre-formatted stat strings [4][21] (only used for page 1).
static void render_page_row(uint16_t *line, int page, int content_y,
                            char stat_text[4][21])
{
    // Clear line to black
    for (int x = 0; x < LCD_WIDTH; x++) line[x] = 0x0000;

    if (page == 0) {
        // Splash page: logo at (2, 12), text at (62, 32)
        int logo_x = 2;
        int logo_y = (LCD_HEIGHT - LOGO_H) / 2;  // 12
        int text_x = logo_x + LOGO_W + 4;        // 62
        int text_y = (LCD_HEIGHT - FONT_H) / 2;   // 32

        if (content_y >= logo_y && content_y < logo_y + LOGO_H) {
            render_logo_row_buf(line, logo_x, content_y - logo_y);
        }
        if (content_y >= text_y && content_y < text_y + FONT_H) {
            render_text_row(line, text_x, "TaipanMiner",
                           content_y - text_y,
                           SWAP16(DISPLAY_COLOR_AMBER), 0x0000);
        }
    } else {
        // Stats page: 4 text lines at y=0,16,32,48
        int text_idx = content_y / FONT_H;
        int font_row = content_y % FONT_H;
        if (text_idx < 4) {
            uint16_t fg = (text_idx == 0)
                ? SWAP16(DISPLAY_COLOR_AMBER)
                : SWAP16(DISPLAY_COLOR_WHITE);
            render_text_row(line, 0, stat_text[text_idx], font_row, fg, 0x0000);
        }
    }
}

// Render one frame of the virtual content strip (splash + stats, 160px total).
// offset: pixel offset into the virtual strip (0 = splash visible, wraps at 160).
static esp_err_t render_frame(int offset, char stat_text[4][21])
{
    static uint16_t s_row_buf[2][LCD_WIDTH];
    static int s_row_idx;
    int total = LCD_HEIGHT * 2;

    for (int sy = 0; sy < LCD_HEIGHT; sy++) {
        uint16_t *line = s_row_buf[s_row_idx];
        int content_y = (sy + offset) % total;

        if (content_y < LCD_HEIGHT) {
            render_page_row(line, 0, content_y, stat_text);
        } else {
            render_page_row(line, 1, content_y - LCD_HEIGHT, stat_text);
        }

        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, 0, sy, LCD_WIDTH, sy + 1, line),
            TAG, "scroll row");
        s_row_idx ^= 1;
    }
    return ESP_OK;
}

// Two-state scroll: hold splash (5s), scroll through stats and back (8s at 1px/tick).
static esp_err_t show_status_st7735(const display_status_t *status)
{
    static int s_state;   // 0=hold_splash, 1=scroll
    static int s_tick;
    static int s_offset;

    char stat_text[4][21];
    fmt_hashrate(status->hashrate, stat_text[0], 21);
    snprintf(stat_text[1], 21, "Shares: %" PRIu32 "/%" PRIu32,
             status->shares, status->rejected);
    snprintf(stat_text[2], 21, "Temp: %.1fC", status->temp_c);
    {
        char up[16];
        fmt_uptime(status->uptime_us, up, sizeof(up));
        snprintf(stat_text[3], 21, "Up: %s", up);
    }

    switch (s_state) {
    case 0:  // Hold splash
        if (s_tick == 0) {
            ESP_RETURN_ON_ERROR(render_frame(0, stat_text), TAG, "splash");
        }
        if (++s_tick >= 100) { s_state = 1; s_tick = 0; s_offset = 0; }
        break;

    case 1:  // Scroll through stats back to splash
        s_offset++;
        ESP_RETURN_ON_ERROR(render_frame(s_offset, stat_text), TAG, "scroll");
        if (s_offset >= LCD_HEIGHT * 2) { s_state = 0; s_tick = 0; }
        break;
    }

    return ESP_OK;
}

#elif defined(BOARD_BITAXE_601)

#include "asic.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ssd1306.h"

#define OLED_WIDTH   128
#define OLED_HEIGHT  32

static esp_err_t init_ssd1306(void)
{
    i2c_master_bus_handle_t i2c_bus = asic_get_i2c_bus();
    if (i2c_bus == NULL) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = I2C_BUS_NUM,
            .sda_io_num = PIN_I2C_SDA,
            .scl_io_num = PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "i2c bus");
        asic_set_i2c_bus(i2c_bus);
        ESP_LOGI(TAG, "I2C bus created by display (no ASIC init)");
    }

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

static uint8_t s_fb[OLED_WIDTH * (OLED_HEIGHT / 8)];

static esp_err_t clear_ssd1306(uint16_t color)
{
    memset(s_fb, color ? 0xFF : 0x00, sizeof(s_fb));
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, OLED_WIDTH, OLED_HEIGHT, s_fb);
    return ESP_OK;
}

// Write text into s_fb without flushing to display
static void fb_draw_text(int x, int y, const char *text)
{
    int page_start = y / 8;

    for (int ci = 0; text[ci] != '\0'; ci++) {
        uint8_t ch = (uint8_t)text[ci];
        if (ch < 0x20 || ch > 0x7E) ch = 0x20;
        const uint8_t *glyph = g_font8x16[ch - 0x20];

        int cx = x + ci * FONT_W;
        if (cx + FONT_W > OLED_WIDTH) break;

        // Each 8x16 character occupies 2 pages vertically
        for (int p = 0; p < 2; p++) {
            int page = page_start + p;
            if (page >= (OLED_HEIGHT / 8)) break;

            for (int col = 0; col < FONT_W; col++) {
                uint8_t col_byte = 0;
                for (int bit = 0; bit < 8; bit++) {
                    int font_row = p * 8 + bit;
                    if (glyph[font_row] & (0x80 >> col)) {
                        col_byte |= (1 << bit);
                    }
                }
                s_fb[page * OLED_WIDTH + cx + col] = col_byte;
            }
        }
    }
}

static esp_err_t fb_flush(void)
{
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, OLED_WIDTH, OLED_HEIGHT, s_fb);
}

static esp_err_t draw_text_ssd1306(int x, int y, const char *text)
{
    fb_draw_text(x, y, text);
    ESP_RETURN_ON_ERROR(fb_flush(), TAG, "draw text");
    return ESP_OK;
}

static esp_err_t show_splash_ssd1306(void)
{
    memset(s_fb, 0x00, sizeof(s_fb));
    fb_draw_text(20, 8, "TaipanMiner");
    ESP_RETURN_ON_ERROR(fb_flush(), TAG, "splash");
    return ESP_OK;
}

static esp_err_t show_prov_ssd1306(const char *ssid, const char *password)
{
    memset(s_fb, 0x00, sizeof(s_fb));

    // Line 0: SSID (truncated to 16 chars)
    char ssid_buf[17];
    strncpy(ssid_buf, ssid, 16);
    ssid_buf[16] = '\0';
    fb_draw_text(0, 0, ssid_buf);

    // Line 1: "PW:" + password (truncated to fit)
    char pw_buf[14];
    snprintf(pw_buf, sizeof(pw_buf), "PW: %s", password);
    fb_draw_text(0, 16, pw_buf);

    ESP_RETURN_ON_ERROR(fb_flush(), TAG, "prov");
    return ESP_OK;
}

static esp_err_t show_status_ssd1306(const display_status_t *status)
{
    static int s_page;
    static int s_tick;

    if (++s_tick < 100) return ESP_OK;
    s_tick = 0;

    memset(s_fb, 0x00, sizeof(s_fb));

    char buf[17];

    if (s_page == 0) {
        // Page 0: hashrate + shares
        fmt_hashrate(status->hashrate, buf, sizeof(buf));
        fb_draw_text(0, 0, buf);

        snprintf(buf, sizeof(buf), "Shares:%" PRIu32 "/%" PRIu32, status->shares, status->rejected);
        fb_draw_text(0, 16, buf);
    } else {
        // Page 1: temp + uptime
        snprintf(buf, sizeof(buf), "Temp: %.1fC", status->temp_c);
        fb_draw_text(0, 0, buf);

        char up[12];
        fmt_uptime(status->uptime_us, up, sizeof(up));
        snprintf(buf, sizeof(buf), "Up: %s", up);
        fb_draw_text(0, 16, buf);
    }

    s_page ^= 1;
    ESP_RETURN_ON_ERROR(fb_flush(), TAG, "status");
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
    return draw_text_st7735(x, y, text, fg, bg, false);
#elif defined(BOARD_BITAXE_601)
    (void)fg; (void)bg;
    return draw_text_ssd1306(x, y, text);
#else
    (void)x; (void)y; (void)text; (void)fg; (void)bg;
    return ESP_OK;
#endif
}

esp_err_t display_show_splash(void)
{
#if defined(BOARD_TDONGLE_S3)
    return show_splash_st7735();
#elif defined(BOARD_BITAXE_601)
    return show_splash_ssd1306();
#else
    return ESP_OK;
#endif
}

esp_err_t display_show_prov(const char *ssid, const char *password)
{
#if defined(BOARD_TDONGLE_S3)
    return show_prov_st7735(ssid, password);
#elif defined(BOARD_BITAXE_601)
    return show_prov_ssd1306(ssid, password);
#else
    (void)ssid; (void)password;
    return ESP_OK;
#endif
}

esp_err_t display_off(void)
{
#if defined(BOARD_TDONGLE_S3)
    ESP_RETURN_ON_ERROR(clear_st7735(DISPLAY_COLOR_BLACK), TAG, "clear");
    gpio_set_level(PIN_LCD_BL, 1);  // active-low: 1 = off
#elif defined(BOARD_BITAXE_601)
    ESP_RETURN_ON_ERROR(clear_ssd1306(0x00), TAG, "clear");
    esp_lcd_panel_disp_on_off(s_panel, false);
#endif
    return ESP_OK;
}

esp_err_t display_show_status(const display_status_t *status)
{
#if defined(BOARD_TDONGLE_S3)
    return show_status_st7735(status);
#elif defined(BOARD_BITAXE_601)
    return show_status_ssd1306(status);
#else
    (void)status;
    return ESP_OK;
#endif
}

#endif
