#include "mining_pause_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_pause_ack   = NULL;
static SemaphoreHandle_t s_pause_done  = NULL;
static SemaphoreHandle_t s_pause_mutex = NULL;

static void ensure_created(void)
{
    if (!s_pause_mutex) s_pause_mutex = xSemaphoreCreateMutex();
    if (!s_pause_ack)   s_pause_ack   = xSemaphoreCreateBinary();
    if (!s_pause_done)  s_pause_done  = xSemaphoreCreateBinary();
}

static bool impl_mutex_take(uint32_t timeout_ms)
{
    ensure_created();
    return xSemaphoreTake(s_pause_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void impl_mutex_give(void)
{
    ensure_created();
    xSemaphoreGive(s_pause_mutex);
}

static bool impl_ack_take(uint32_t timeout_ms)
{
    ensure_created();
    return xSemaphoreTake(s_pause_ack, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void impl_ack_give(void)
{
    ensure_created();
    xSemaphoreGive(s_pause_ack);
}

static bool impl_done_take(uint32_t timeout_ms)
{
    ensure_created();
    return xSemaphoreTake(s_pause_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void impl_done_give(void)
{
    ensure_created();
    xSemaphoreGive(s_pause_done);
}

const mining_pause_sync_ops_t g_mining_pause_sync_ops_default = {
    .mutex_take = impl_mutex_take,
    .mutex_give = impl_mutex_give,
    .ack_take   = impl_ack_take,
    .ack_give   = impl_ack_give,
    .done_take  = impl_done_take,
    .done_give  = impl_done_give,
};
