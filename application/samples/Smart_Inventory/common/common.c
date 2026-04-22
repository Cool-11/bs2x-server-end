/**
 *******************************************************************************
 * @file common.c
 * @brief Smart Inventory Tag - Common Utilities Module Implementation
 *
 *  Description:
 *   本模块是公共工具库实现，提供所有模块共用的基础功能函数。
 *   包括：日志打印、延时函数、内存操作、任务操作等。
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#include <stdio.h>
#include <stdarg.h>
#include "common.h"
#include "osal_time.h"
#include "osal_task.h"
#include "osal_mem.h"
#include "soc_osal.h"

static log_level_t g_log_level = LOG_LEVEL_INFO;

#define LOG_COLOR_ERROR   "\033[31m"
#define LOG_COLOR_WARN    "\033[33m"
#define LOG_COLOR_INFO    "\033[32m"
#define LOG_COLOR_DEBUG   "\033[36m"
#define LOG_COLOR_RESET   "\033[0m"

void common_log_set_level(log_level_t level)
{
    g_log_level = level;
}

log_level_t common_log_get_level(void)
{
    return g_log_level;
}

void common_log_print(log_level_t level, const char *tag, const char *fmt, va_list args)
{
    if (level > g_log_level) {
        return;
    }

    const char *color = LOG_COLOR_RESET;
    const char *level_str = "UNKNOWN";

    switch (level) {
        case LOG_LEVEL_ERROR:
            color = LOG_COLOR_ERROR;
            level_str = "E";
            break;
        case LOG_LEVEL_WARN:
            color = LOG_COLOR_WARN;
            level_str = "W";
            break;
        case LOG_LEVEL_INFO:
            color = LOG_COLOR_INFO;
            level_str = "I";
            break;
        case LOG_LEVEL_DEBUG:
            color = LOG_COLOR_DEBUG;
            level_str = "D";
            break;
        default:
            break;
    }

    printf("%s[%s]%s [%s] ", color, level_str, LOG_COLOR_RESET, tag);
    vprintf(fmt, args);
    printf("\r\n");
}

void common_log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    common_log_print(LOG_LEVEL_ERROR, TAG, fmt, args);
    va_end(args);
}

void common_log_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    common_log_print(LOG_LEVEL_WARN, TAG, fmt, args);
    va_end(args);
}

void common_log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    common_log_print(LOG_LEVEL_INFO, TAG, fmt, args);
    va_end(args);
}

void common_log_debug(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    common_log_print(LOG_LEVEL_DEBUG, TAG, fmt, args);
    va_end(args);
}

void common_msleep(uint32_t ms)
{
    osal_msleep(ms);
}

void common_usleep(uint32_t us)
{
    osal_usleep(us);
}

uint32_t common_get_tick_ms(void)
{
    return osal_get_jiffies() * (1000 / OSAL_HZ);
}

uint64_t common_get_tick_us(void)
{
    return (uint64_t)osal_get_jiffies() * (1000000 / OSAL_HZ);
}

bool common_is_timeout(uint32_t start_time, uint32_t timeout_ms)
{
    uint32_t elapsed = common_get_tick_ms() - start_time;
    return (elapsed >= timeout_ms);
}

void *common_malloc(uint32_t size)
{
    return osal_malloc(size);
}

void common_free(void *ptr)
{
    if (ptr != NULL) {
        osal_free(ptr);
    }
}

void *common_malloc_align(uint32_t size, uint32_t align)
{
    (void)align;
    return osal_malloc(size);
}

void common_free_align(void *ptr)
{
    common_free(ptr);
}

int common_memcpy_s(void *dest, uint32_t dest_max, const void *src, uint32_t src_len)
{
    if (dest == NULL || src == NULL || dest_max < src_len) {
        return -1;
    }
    memcpy(dest, src, src_len);
    return 0;
}

int common_memset_s(void *dest, uint32_t dest_max, uint8_t value, uint32_t count)
{
    if (dest == NULL || dest_max < count) {
        return -1;
    }
    memset(dest, value, count);
    return 0;
}

int common_strcpy_s(char *dest, uint32_t dest_max, const char *src)
{
    if (dest == NULL || src == NULL || dest_max == 0) {
        return -1;
    }
    strcpy(dest, src);
    return 0;
}

int common_strcat_s(char *dest, uint32_t dest_max, const char *src)
{
    if (dest == NULL || src == NULL || dest_max == 0) {
        return -1;
    }
    strcat(dest, src);
    return 0;
}

osal_task *common_task_create(const char *name, osal_kthread_handler entry,
                              int32_t arg, uint32_t stack_size, uint32_t priority)
{
    osal_task *task = osal_kthread_create(entry, (void *)(intptr_t)arg, name, stack_size);
    if (task != NULL) {
        osal_kthread_set_priority(task, priority);
    }
    return task;
}

void common_task_delete(osal_task *task)
{
    if (task != NULL) {
        osal_kthread_delete(task);
    }
}

void common_task_suspend(osal_task *task)
{
    if (task != NULL) {
        osal_kthread_suspend(task);
    }
}

void common_task_resume(osal_task *task)
{
    if (task != NULL) {
        osal_kthread_resume(task);
    }
}

osal_task *common_task_get_current(void)
{
    return osal_kthread_get_current();
}

uint32_t common_irq_lock(void)
{
    return osal_irq_lock();
}

void common_irq_restore(uint32_t irq_state)
{
    osal_irq_restore(irq_state);
}

uint32_t common_get_boot_reason(void)
{
    return soc_osal_get_boot_reason();
}

int common_hex_to_string(const uint8_t *data, uint32_t len, char *out, uint32_t out_max)
{
    if (data == NULL || out == NULL || out_max < len * 2 + 1) {
        return -1;
    }

    static const char hex_chars[] = "0123456789ABCDEF";
    uint32_t i;
    for (i = 0; i < len; i++) {
        out[i * 2] = hex_chars[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex_chars[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
    return len * 2;
}

int common_string_to_hex(const char *str, uint8_t *out, uint32_t out_max)
{
    if (str == NULL || out == NULL || out_max == 0) {
        return -1;
    }

    uint32_t len = strlen(str);
    if (len % 2 != 0 || len / 2 > out_max) {
        return -1;
    }

    for (uint32_t i = 0; i < len / 2; i++) {
        char high = str[i * 2];
        char low = str[i * 2 + 1];
        if (!common_is_hex_string(&high) || !common_is_hex_string(&low)) {
            return -1;
        }
        out[i] = (common_hex_char_to_val(high) << 4) | common_hex_char_to_val(low);
    }
    return len / 2;
}

bool common_is_hex_string(const char *str)
{
    if (str == NULL || *str == '\0') {
        return false;
    }
    while (*str) {
        if (!((*str >= '0' && *str <= '9') ||
              (*str >= 'A' && *str <= 'F') ||
              (*str >= 'a' && *str <= 'f'))) {
            return false;
        }
        str++;
    }
    return true;
}

static uint8_t common_hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}
