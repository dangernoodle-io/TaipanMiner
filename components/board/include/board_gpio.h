#pragma once

#include "esp_log.h"
#include "soc/gpio_num.h"

static inline bool board_gpio_valid(int pin, const char *label)
{
    if (pin < 0 || pin >= SOC_GPIO_PIN_COUNT) {
        ESP_LOGE("board", "invalid GPIO %d for %s (max %d)", pin, label, SOC_GPIO_PIN_COUNT - 1);
        return false;
    }
    return true;
}
