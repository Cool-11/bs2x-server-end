/**
 *******************************************************************************
 * @file pm.c
 * @brief Smart Inventory Tag - Power Management Module Implementation
 *
 *  Description:
 *   本模块负责星闪标签的电源管理(Power Management)和低功耗控制。
 *   包括：深度睡眠、浅睡眠、唤醒源配置、功耗状态转换。
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "pm.h"
#include "common.h"
#include "adv.h"
#include "conn.h"

#if defined(CONFIG_PM_SYS_SUPPORT)
#include "pm_sys.h"
#include "ulp_gpio.h"
#endif

#define PM_WAKEUP_GPIO_NUM     2
#define PM_VETO_MAX           8

static pm_mode_t g_pm_current_mode = PM_MODE_WORK;
static uint8_t g_pm_is_sleep_allowed = 1;
static uint8_t g_pm_veto_count = 0;
static uint32_t g_pm_last_sleep_time = 0;
static pm_sleep_history_t g_pm_sleep_history = {0};
static pm_state_trans_handler_t g_pm_trans_handler = {0};

static uint8_t g_pm_initialized = 0;

#if defined(CONFIG_PM_SYS_SUPPORT)
static void pm_gpio_irq_handler(pin_t pin)
{
    (void)pin;
    LOGI("GPIO interrupt wakeup");
    uapi_gpio_clear_interrupt(pin);
    uapi_pm_work_state_reset();
}

static void pm_ulpgpio_wakeup_handler(uint8_t ulp_gpio)
{
    (void)ulp_gpio;
    LOGI("ULP GPIO wakeup");
    uapi_pm_wkup_process(0);
}

#if defined(CONFIG_PM_SYS_SUPPORT)
static ulp_gpio_int_wkup_cfg_t g_pm_wkup_cfg[] = {
    { 0, PM_WAKEUP_GPIO_NUM, true, ULP_GPIO_INTERRUPT_FALLING_EDGE, pm_ulpgpio_wakeup_handler },
};
#endif

static void pm_gpio_sleep_config(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    uapi_gpio_deinit();
    ulp_gpio_init();
    ulp_gpio_int_wkup_config(g_pm_wkup_cfg, sizeof(g_pm_wkup_cfg) / sizeof(ulp_gpio_int_wkup_cfg_t));
#endif
}

static void pm_gpio_wakeup_config(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    ulp_gpio_deinit();
    uapi_gpio_init();
    uapi_pin_set_mode(PM_WAKEUP_GPIO_NUM, 0);
    uapi_pin_set_pull(PM_WAKEUP_GPIO_NUM, PIN_PULL_UP);
    uapi_gpio_set_dir(PM_WAKEUP_GPIO_NUM, GPIO_DIRECTION_INPUT);
    uapi_gpio_register_isr_func(PM_WAKEUP_GPIO_NUM, GPIO_INTERRUPT_FALLING_EDGE,
                                (gpio_callback_t)pm_gpio_irq_handler);
#endif
}

static int32_t pm_state_work_to_standby(uintptr_t arg)
{
    (void)arg;
    uint32_t irq_status = common_irq_lock();
    g_pm_current_mode = PM_MODE_STANDBY;
    pm_gpio_sleep_config();
    common_irq_restore(irq_status);
    LOGI("PM state: WORK -> STANDBY");
    return 0;
}

static int32_t pm_state_standby_to_sleep(uintptr_t arg)
{
    (void)arg;
    uint32_t irq_status = common_irq_lock();
    adv_stop();
    conn_disconnect();
    g_pm_current_mode = PM_MODE_SLEEP;
    g_pm_last_sleep_time = common_get_tick_ms();
    common_irq_restore(irq_status);
    LOGI("PM state: STANDBY -> SLEEP");
    return 0;
}

static int32_t pm_state_standby_to_work(uintptr_t arg)
{
    (void)arg;
    uint32_t irq_status = common_irq_lock();
    pm_gpio_wakeup_config();
    g_pm_current_mode = PM_MODE_WORK;
    common_irq_restore(irq_status);
    LOGI("PM state: STANDBY -> WORK");
    return 0;
}

static int32_t pm_state_sleep_to_work(uintptr_t arg)
{
    (void)arg;
    uint32_t irq_status = common_irq_lock();
    pm_gpio_wakeup_config();
    g_pm_current_mode = PM_MODE_WORK;
    g_pm_sleep_history.wakeup_count++;
    common_irq_restore(irq_status);
    LOGI("PM state: SLEEP -> WORK (wakeup #%u)", g_pm_sleep_history.wakeup_count);
    return 0;
}
#endif

errcode_t pm_init(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    g_pm_trans_handler.work_to_standby = pm_state_work_to_standby;
    g_pm_trans_handler.standby_to_sleep = pm_state_standby_to_sleep;
    g_pm_trans_handler.standby_to_work = pm_state_standby_to_work;
    g_pm_trans_handler.sleep_to_work = pm_state_sleep_to_work;

    uapi_pm_state_trans_handler_register(&g_pm_trans_handler);
    uapi_pm_work_state_reset();
    uapi_pm_set_state_trans_duration(0xFFFFFFFF, 0xFFFFFFFF);

    g_pm_initialized = 1;
    g_pm_current_mode = PM_MODE_WORK;
    LOGI("PM module initialized");
#else
    LOGW("PM sys support not enabled");
    g_pm_initialized = 0;
#endif
    return 0;
}

errcode_t pm_enter_deep_sleep(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    if (!g_pm_initialized) {
        LOGE("PM not initialized");
        return ERR_COMMON_INVALID_PARAM;
    }

    if (g_pm_veto_count > 0) {
        LOGW("Sleep vetoed, veto_count=%u", g_pm_veto_count);
        return ERR_COMMON_BUSY;
    }

    LOGI("Entering deep sleep...");
    pm_gpio_sleep_config();

    g_pm_sleep_history.sleep_count++;
    g_pm_last_sleep_time = common_get_tick_ms();

    uapi_pm_work_state_reset();
    uapi_pm_set_state_trans_duration(0, 0);

    uapi_pm_enter_udsleep();

    return 0;
#else
    LOGW("Deep sleep not supported");
    return ERR_COMMON_INVALID_PARAM;
#endif
}

errcode_t pm_enter_light_sleep(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    if (!g_pm_initialized) {
        LOGE("PM not initialized");
        return ERR_COMMON_INVALID_PARAM;
    }

    LOGI("Light sleep not fully implemented, using deep sleep instead");
    return pm_enter_deep_sleep();
#else
    LOGW("Sleep not supported");
    return ERR_COMMON_INVALID_PARAM;
#endif
}

errcode_t pm_wakeup(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    if (g_pm_current_mode == PM_MODE_WORK) {
        LOGW("Already in WORK mode");
        return 0;
    }

    pm_gpio_wakeup_config();
    g_pm_current_mode = PM_MODE_WORK;
    g_pm_sleep_history.wakeup_count++;

    LOGI("Wakeup complete, wakeup_count=%u", g_pm_sleep_history.wakeup_count);
    return 0;
#else
    return ERR_COMMON_INVALID_PARAM;
#endif
}

errcode_t pm_enter_standby(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    if (!g_pm_initialized) {
        LOGE("PM not initialized");
        return ERR_COMMON_INVALID_PARAM;
    }

    if (g_pm_veto_count > 0) {
        LOGW("Standby vetoed, veto_count=%u", g_pm_veto_count);
        return ERR_COMMON_BUSY;
    }

    LOGI("Entering standby...");
    pm_gpio_sleep_config();
    g_pm_current_mode = PM_MODE_STANDBY;

    return 0;
#else
    return ERR_COMMON_INVALID_PARAM;
#endif
}

errcode_t pm_set_wakeup_timer(uint32_t timeout_ms)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    (void)timeout_ms;
    LOGI("Wakeup timer not fully implemented");
    return 0;
#else
    (void)timeout_ms;
    return ERR_COMMON_INVALID_PARAM;
#endif
}

errcode_t pm_cancel_wakeup_timer(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    LOGI("Wakeup timer cancelled");
    return 0;
#else
    return ERR_COMMON_INVALID_PARAM;
#endif
}

errcode_t pm_add_sleep_veto(uint8_t veto_id)
{
    if (g_pm_veto_count >= PM_VETO_MAX) {
        LOGE("Too many sleep vetoes");
        return ERR_COMMON_NO_RESOURCE;
    }
    g_pm_veto_count++;
    g_pm_is_sleep_allowed = 0;
    LOGI("Sleep veto added: id=%u, count=%u", veto_id, g_pm_veto_count);
    return 0;
}

errcode_t pm_remove_sleep_veto(uint8_t veto_id)
{
    (void)veto_id;
    if (g_pm_veto_count == 0) {
        LOGW("No sleep veto to remove");
        return 0;
    }
    g_pm_veto_count--;
    if (g_pm_veto_count == 0) {
        g_pm_is_sleep_allowed = 1;
    }
    LOGI("Sleep veto removed: count=%u", g_pm_veto_count);
    return 0;
}

void pm_reset_work_timer(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    uapi_pm_work_state_reset();
#endif
}

pm_mode_t pm_get_current_mode(void)
{
    return g_pm_current_mode;
}

pm_runtime_info_t *pm_get_runtime_info(void)
{
    static pm_runtime_info_t info = {0};
    info.current_mode = g_pm_current_mode;
    info.is_sleep_allowed = g_pm_is_sleep_allowed;
    info.veto_count = g_pm_veto_count;
    info.last_sleep_time = g_pm_last_sleep_time;
    return &info;
}

errcode_t pm_register_gpio_wakeup(uint8_t gpio_num, uint8_t edge)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    (void)gpio_num;
    (void)edge;
    LOGI("GPIO wakeup registration not fully implemented");
    return 0;
#else
    (void)gpio_num;
    (void)edge;
    return ERR_COMMON_INVALID_PARAM;
#endif
}

pm_sleep_history_t *pm_get_sleep_history(void)
{
    return &g_pm_sleep_history;
}

errcode_t pm_register_state_trans_handler(pm_state_trans_handler_t *handler)
{
    if (handler == NULL) {
        return ERR_COMMON_NULL_PTR;
    }
    (void)memcpy_s(&g_pm_trans_handler, sizeof(pm_state_trans_handler_t),
                   handler, sizeof(pm_state_trans_handler_t));
    return 0;
}

errcode_t pm_prepare_for_sleep(void)
{
    adv_stop();
    conn_disconnect();
    LOGI("Prepared for sleep: adv stopped, conn disconnected");
    return 0;
}
