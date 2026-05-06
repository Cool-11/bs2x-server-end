#include "hardware_hal.h"

#include "common_def.h"
#include "errcode.h"
#include "gpio.h"
#include "pinctrl.h"

#include "soc_osal.h"

#ifndef CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS
#define CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS 15000
#endif

#ifndef CONFIG_MY_PROJECT_2X_LED_GPIO
#define CONFIG_MY_PROJECT_2X_LED_GPIO 9
#endif

#ifndef CONFIG_MY_PROJECT_2X_BEEP_GPIO
#define CONFIG_MY_PROJECT_2X_BEEP_GPIO 8
#endif

#define HW_HAL_LOG "[BS2x_HAL]"

typedef struct {
    osal_timer timer;
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
    osal_printk("%s[BP] force_all_off beep_gpio=%d led_gpio=%d\r\n",
                HW_HAL_LOG, CONFIG_MY_PROJECT_2X_BEEP_GPIO, CONFIG_MY_PROJECT_2X_LED_GPIO);
}

static void hw_hal_auto_off_timer_handler(unsigned long data)
{
    unused(data);
    osal_printk("%s[BP] auto_off_timer timeout beep_on=%d led_on=%d\r\n",
                HW_HAL_LOG, g_hw.beep_on, g_hw.led_on);
    hw_hal_force_all_off();
}

hw_hal_status_t hardware_hal_init(void)
{
    osal_printk("%s[BP] init enter inited=%d\r\n", HW_HAL_LOG, g_hw.initialized);

    if (g_hw.initialized) {
        osal_printk("%s[BP] init already done\r\n", HW_HAL_LOG);
        return HW_HAL_OK;
    }

    uapi_pin_init();
    uapi_gpio_init();

    (void)uapi_pin_set_mode((pin_t)CONFIG_MY_PROJECT_2X_BEEP_GPIO, HAL_PIO_FUNC_GPIO);
    (void)uapi_pin_set_mode((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, HAL_PIO_FUNC_GPIO);

    (void)uapi_gpio_set_dir((pin_t)CONFIG_MY_PROJECT_2X_BEEP_GPIO, GPIO_DIRECTION_OUTPUT);
    (void)uapi_gpio_set_dir((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_DIRECTION_OUTPUT);

    hw_hal_force_all_off();

    g_hw.timer.handler = hw_hal_auto_off_timer_handler;
    g_hw.timer.data = 0;
    g_hw.timer.interval = CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS;

    int ret = osal_timer_init(&g_hw.timer);
    if (ret != OSAL_SUCCESS) {
        osal_printk("%s[BP] init FAIL timer_init ret=%d\r\n", HW_HAL_LOG, ret);
        return HW_HAL_ERR_OS;
    }

    g_hw.initialized = true;
    osal_printk("%s[BP] init OK led_gpio=%d beep_gpio=%d auto_off=%dms\r\n",
                HW_HAL_LOG,
                CONFIG_MY_PROJECT_2X_LED_GPIO,
                CONFIG_MY_PROJECT_2X_BEEP_GPIO,
                CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS);
    return HW_HAL_OK;
}

static hw_hal_status_t hw_hal_start_auto_off(uint32_t ms)
{
    if (!g_hw.initialized) {
        osal_printk("%s[BP] start_auto_off FAIL not inited\r\n", HW_HAL_LOG);
        return HW_HAL_ERR_OS;
    }

    if (ms == 0) {
        ms = CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS;
    }

    (void)osal_timer_stop(&g_hw.timer);

    int ret = osal_timer_mod(&g_hw.timer, ms);
    if (ret != OSAL_SUCCESS) {
        hw_hal_force_all_off();
        osal_printk("%s[BP] start_auto_off FAIL timer_mod ret=%d ms=%u\r\n", HW_HAL_LOG, ret, ms);
        return HW_HAL_ERR_OS;
    }

    ret = osal_timer_start(&g_hw.timer);
    if (ret != OSAL_SUCCESS) {
        hw_hal_force_all_off();
        osal_printk("%s[BP] start_auto_off FAIL timer_start ret=%d ms=%u\r\n", HW_HAL_LOG, ret, ms);
        return HW_HAL_ERR_OS;
    }

    osal_printk("%s[BP] start_auto_off OK ms=%u\r\n", HW_HAL_LOG, ms);
    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_beep_on_for_ms(uint32_t ms)
{
    osal_printk("%s[BP] beep_on request ms=%u\r\n", HW_HAL_LOG, ms);

    if (hardware_hal_init() != HW_HAL_OK) {
        osal_printk("%s[BP] beep_on FAIL hal_init error\r\n", HW_HAL_LOG);
        return HW_HAL_ERR_OS;
    }

    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_BEEP_GPIO, GPIO_LEVEL_HIGH);
    g_hw.beep_on = true;

    hw_hal_status_t st = hw_hal_start_auto_off(ms);
    if (st != HW_HAL_OK) {
        osal_printk("%s[BP] beep_on FAIL auto_off error st=%d\r\n", HW_HAL_LOG, st);
        hw_hal_force_all_off();
        return st;
    }

    osal_printk("%s[BP] beep_on OK gpio=%d\r\n", HW_HAL_LOG, CONFIG_MY_PROJECT_2X_BEEP_GPIO);
    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_beep_off(void)
{
    osal_printk("%s[BP] beep_off\r\n", HW_HAL_LOG);

    if (!g_hw.initialized) {
        osal_printk("%s[BP] beep_off skip not inited\r\n", HW_HAL_LOG);
        return HW_HAL_OK;
    }

    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_BEEP_GPIO, GPIO_LEVEL_LOW);
    g_hw.beep_on = false;

    if (!g_hw.led_on) {
        (void)osal_timer_stop(&g_hw.timer);
        osal_printk("%s[BP] beep_off timer_stopped led_off too\r\n", HW_HAL_LOG);
    }

    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_led_on_for_ms(uint32_t ms)
{
    osal_printk("%s[BP] led_on request ms=%u\r\n", HW_HAL_LOG, ms);

    if (hardware_hal_init() != HW_HAL_OK) {
        osal_printk("%s[BP] led_on FAIL hal_init error\r\n", HW_HAL_LOG);
        return HW_HAL_ERR_OS;
    }

    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_LEVEL_HIGH);
    g_hw.led_on = true;

    hw_hal_status_t st = hw_hal_start_auto_off(ms);
    if (st != HW_HAL_OK) {
        osal_printk("%s[BP] led_on FAIL auto_off error st=%d\r\n", HW_HAL_LOG, st);
        hw_hal_force_all_off();
        return st;
    }

    osal_printk("%s[BP] led_on OK gpio=%d\r\n", HW_HAL_LOG, CONFIG_MY_PROJECT_2X_LED_GPIO);
    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_led_off(void)
{
    osal_printk("%s[BP] led_off\r\n", HW_HAL_LOG);

    if (!g_hw.initialized) {
        osal_printk("%s[BP] led_off skip not inited\r\n", HW_HAL_LOG);
        return HW_HAL_OK;
    }

    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_LEVEL_LOW);
    g_hw.led_on = false;

    if (!g_hw.beep_on) {
        (void)osal_timer_stop(&g_hw.timer);
        osal_printk("%s[BP] led_off timer_stopped beep_off too\r\n", HW_HAL_LOG);
    }

    return HW_HAL_OK;
}