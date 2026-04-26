#include "hardware_hal.h"

#include "common_def.h"
#include "errcode.h"
#include "gpio.h"
#include "timer.h"

#include "soc_osal.h"

#ifndef CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS
#define CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS 15000
#endif

#ifndef CONFIG_MY_PROJECT_2X_LED_GPIO
#define CONFIG_MY_PROJECT_2X_LED_GPIO 0
#endif

#ifndef CONFIG_MY_PROJECT_2X_BEEP_GPIO
#define CONFIG_MY_PROJECT_2X_BEEP_GPIO 1
#endif

#define HW_HAL_LOG "[BS2x_HAL]"

typedef struct {
    timer_handle_t timer;
    bool initialized;
    bool beep_on;
    bool led_on;
} hw_hal_ctx_t;

static hw_hal_ctx_t g_hw = {0};

static void hw_hal_force_all_off(void)
{
    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_BEEP_GPIO, GPIO_LEVEL_LOW);
    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_LEVEL_LOW);
    g_hw.beep_on = false;
    g_hw.led_on = false;
    osal_printk("%s gpio forced off\r\n", HW_HAL_LOG);
}

static void hw_hal_auto_off_timer_cb(uintptr_t data)
{
    unused(data);
    osal_printk("%s timer timeout, force off in ISR\r\n", HW_HAL_LOG);
    hw_hal_force_all_off();
}

hw_hal_status_t hardware_hal_init(void)
{
    osal_printk("%s Entering hardware_hal_init\r\n", HW_HAL_LOG);

    if (g_hw.initialized) {
        osal_printk("%s already initialized\r\n", HW_HAL_LOG);
        return HW_HAL_OK;
    }

    uapi_gpio_init();
    (void)uapi_gpio_set_dir((pin_t)CONFIG_MY_PROJECT_2X_BEEP_GPIO, GPIO_DIRECTION_OUTPUT);
    (void)uapi_gpio_set_dir((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_DIRECTION_OUTPUT);

    hw_hal_force_all_off();

    errcode_t ret = uapi_timer_create(TIMER_INDEX_0, &g_hw.timer);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s timer create failed ret=0x%x\r\n", HW_HAL_LOG, ret);
        return HW_HAL_ERR_OS;
    }

    g_hw.initialized = true;
    osal_printk("%s init done led_gpio=%d beep_gpio=%d\r\n",
                HW_HAL_LOG,
                CONFIG_MY_PROJECT_2X_LED_GPIO,
                CONFIG_MY_PROJECT_2X_BEEP_GPIO);
    return HW_HAL_OK;
}

static hw_hal_status_t hw_hal_start_auto_off(uint32_t ms)
{
    if (!g_hw.initialized || g_hw.timer == NULL) {
        return HW_HAL_ERR_OS;
    }

    if (ms == 0) {
        ms = CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS;
    }

    (void)uapi_timer_stop(g_hw.timer);

    uint32_t time_us = ms * 1000u;
    if (time_us == 0) {
        time_us = 1;
    }

    errcode_t ret = uapi_timer_start(g_hw.timer, time_us, hw_hal_auto_off_timer_cb, 0);
    if (ret != ERRCODE_SUCC) {
        hw_hal_force_all_off();
        osal_printk("%s timer start failed ret=0x%x\r\n", HW_HAL_LOG, ret);
        return HW_HAL_ERR_OS;
    }

    osal_printk("%s Timer started for %u ms auto-off.\r\n", HW_HAL_LOG, ms);

    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_beep_on_for_ms(uint32_t ms)
{
    osal_printk("%s beep on request ms=%u\r\n", HW_HAL_LOG, ms);

    if (hardware_hal_init() != HW_HAL_OK) {
        return HW_HAL_ERR_OS;
    }

    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_BEEP_GPIO, GPIO_LEVEL_HIGH);
    g_hw.beep_on = true;

    hw_hal_status_t st = hw_hal_start_auto_off(ms);
    if (st != HW_HAL_OK) {
        hw_hal_force_all_off();
        return st;
    }

    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_beep_off(void)
{
    osal_printk("%s beep off\r\n", HW_HAL_LOG);

    if (!g_hw.initialized) {
        return HW_HAL_OK;
    }

    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_BEEP_GPIO, GPIO_LEVEL_LOW);
    g_hw.beep_on = false;

    if (!g_hw.led_on) {
        (void)uapi_timer_stop(g_hw.timer);
    }

    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_led_on_for_ms(uint32_t ms)
{
    osal_printk("%s led on request ms=%u\r\n", HW_HAL_LOG, ms);

    if (hardware_hal_init() != HW_HAL_OK) {
        return HW_HAL_ERR_OS;
    }

    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_LEVEL_HIGH);
    g_hw.led_on = true;

    hw_hal_status_t st = hw_hal_start_auto_off(ms);
    if (st != HW_HAL_OK) {
        hw_hal_force_all_off();
        return st;
    }

    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_led_off(void)
{
    osal_printk("%s led off\r\n", HW_HAL_LOG);

    if (!g_hw.initialized) {
        return HW_HAL_OK;
    }

    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_LEVEL_LOW);
    g_hw.led_on = false;

    if (!g_hw.beep_on) {
        (void)uapi_timer_stop(g_hw.timer);
    }

    return HW_HAL_OK;
}
