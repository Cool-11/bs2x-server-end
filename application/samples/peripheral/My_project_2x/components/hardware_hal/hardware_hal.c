#include "hardware_hal.h"

#include "common_def.h"
#include "errcode.h"
#include "gpio.h"
#include "pinctrl.h"
#include "pwm.h"
#include "adc.h"
#include "adc_porting.h"
#include "soc_osal.h"

#ifndef CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS
#define CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS 15000
#endif

#ifndef CONFIG_MY_PROJECT_2X_LED_GPIO
#define CONFIG_MY_PROJECT_2X_LED_GPIO 9
#endif

#ifndef CONFIG_MY_PROJECT_2X_PWM_PIN
#define CONFIG_MY_PROJECT_2X_PWM_PIN 20
#endif

#ifndef CONFIG_MY_PROJECT_2X_PWM_PIN_MODE
#define CONFIG_MY_PROJECT_2X_PWM_PIN_MODE 40
#endif

#ifndef CONFIG_MY_PROJECT_2X_PWM_CHANNEL
#define CONFIG_MY_PROJECT_2X_PWM_CHANNEL 0
#endif

#ifndef CONFIG_MY_PROJECT_2X_PWM_GROUP_ID
#define CONFIG_MY_PROJECT_2X_PWM_GROUP_ID 0
#endif

#define HW_HAL_LOG "[BS2x_HAL]"

/* 电池ADC采集参数（CR2032纽扣电池） */
#define BATTERY_GADC_CHANNEL      GADC_CHANNEL_0   /* V153 GADC通道0 */
#define BATTERY_VOLTAGE_MAX_MV    3000u  /* 满电电压 3.0V */
#define BATTERY_VOLTAGE_MIN_MV    2000u  /* 截止电压 2.0V */
#define BATTERY_LOW_THRESHOLD     10u    /* 低电量告警阈值 10% */
#define BATTERY_PERCENT_MAX       100u
#define BATTERY_ADC_INVALID_MV    (-1)

/* ADC 是否已初始化 */
static bool g_adc_inited = false;
/* 电量缓存值（ADC 读取失败时使用） */
static uint8_t g_battery_cache = BATTERY_PERCENT_MAX;

/* 无源蜂鸣器PWM参数：目标2kHz，50%占空比
 * PWM时钟=32MHz，freq = 32MHz / (low_time + high_time)
 * 2kHz → 32000000 / 2000 = 16000 → low=8000, high=8000
 */
#define BUZZER_PWM_LOW_TIME  8000u
#define BUZZER_PWM_HIGH_TIME 8000u
#define BUZZER_PWM_OFFSET    0u
#define BUZZER_PWM_CYCLES    0xFFu

/* 间歇鸣叫参数：响200ms → 停200ms，共15秒 */
#define BEEP_TOGGLE_PERIOD_MS   200u
#define BEEP_TOGGLE_TOTAL_MS    15000u
#define BEEP_TOGGLE_MAX_COUNT   (BEEP_TOGGLE_TOTAL_MS / BEEP_TOGGLE_PERIOD_MS)

typedef struct {
    osal_timer timer;
    bool initialized;
    bool beep_on;
    bool led_on;
    bool pwm_inited;
    uint16_t toggle_count;
} hw_hal_ctx_t;

static hw_hal_ctx_t g_hw = {0};

static void hw_hal_pwm_buzzer_stop(void)
{
    if (!g_hw.pwm_inited) {
        return;
    }

#if defined(CONFIG_PWM_USING_V151)
    (void)uapi_pwm_stop_group((uint8_t)CONFIG_MY_PROJECT_2X_PWM_GROUP_ID);
#else
    (void)uapi_pwm_stop((uint8_t)CONFIG_MY_PROJECT_2X_PWM_CHANNEL);
#endif
    osal_printk("%s[BP] pwm_buzzer_stop ch:%u\r\n", HW_HAL_LOG, CONFIG_MY_PROJECT_2X_PWM_CHANNEL);
}

static void hw_hal_pwm_buzzer_start(void)
{
    if (!g_hw.pwm_inited) {
        return;
    }

#if defined(CONFIG_PWM_USING_V151)
    (void)uapi_pwm_start_group((uint8_t)CONFIG_MY_PROJECT_2X_PWM_GROUP_ID);
#else
    (void)uapi_pwm_start((uint8_t)CONFIG_MY_PROJECT_2X_PWM_CHANNEL);
#endif
}

static void hw_hal_force_all_off(void)
{
    hw_hal_pwm_buzzer_stop();
    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_LEVEL_LOW);
    g_hw.beep_on = false;
    g_hw.led_on = false;
    g_hw.toggle_count = 0;
    osal_printk("%s[BP] force_all_off led_gpio=%d\r\n", HW_HAL_LOG, CONFIG_MY_PROJECT_2X_LED_GPIO);
}

static void hw_hal_auto_off_timer_handler(unsigned long data)
{
    unused(data);

    g_hw.toggle_count++;
    if (g_hw.toggle_count >= BEEP_TOGGLE_MAX_COUNT) {
        osal_printk("%s[BP] beep pattern done, count=%u\r\n", HW_HAL_LOG, g_hw.toggle_count);
        hw_hal_force_all_off();
        return;
    }

    /* 交替开/关蜂鸣器：偶数次开，奇数次关 */
    if (g_hw.toggle_count % 2 == 0) {
        hw_hal_pwm_buzzer_start();
        g_hw.beep_on = true;
    } else {
        hw_hal_pwm_buzzer_stop();
        g_hw.beep_on = false;
    }
}

static hw_hal_status_t hw_hal_pwm_buzzer_init(void)
{
    if (g_hw.pwm_inited) {
        osal_printk("%s[BP] pwm already inited\r\n", HW_HAL_LOG);
        return HW_HAL_OK;
    }

    /* 设置PWM引脚复用模式 */
    uapi_pin_set_mode((pin_t)CONFIG_MY_PROJECT_2X_PWM_PIN, CONFIG_MY_PROJECT_2X_PWM_PIN_MODE);

    /* 初始化PWM控制器 */
    uapi_pwm_deinit();
    errcode_t ret = uapi_pwm_init();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] pwm_init FAIL ret:0x%x\r\n", HW_HAL_LOG, ret);
        return HW_HAL_ERR_OS;
    }

    /* 配置PWM通道：50%占空比，连续输出 */
    pwm_config_t cfg = {
        .low_time = BUZZER_PWM_LOW_TIME,
        .high_time = BUZZER_PWM_HIGH_TIME,
        .offset_time = BUZZER_PWM_OFFSET,
        .cycles = BUZZER_PWM_CYCLES,
        .repeat = true,
    };

    ret = uapi_pwm_open((uint8_t)CONFIG_MY_PROJECT_2X_PWM_CHANNEL, &cfg);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] pwm_open FAIL ch:%u ret:0x%x\r\n",
                    HW_HAL_LOG, CONFIG_MY_PROJECT_2X_PWM_CHANNEL, ret);
        return HW_HAL_ERR_OS;
    }

    /* V151版本需要设置分组 */
#if defined(CONFIG_PWM_USING_V151)
    uint8_t channel_id = (uint8_t)CONFIG_MY_PROJECT_2X_PWM_CHANNEL;
    ret = uapi_pwm_set_group((uint8_t)CONFIG_MY_PROJECT_2X_PWM_GROUP_ID, &channel_id, 1);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] pwm_set_group FAIL grp:%u ret:0x%x\r\n",
                    HW_HAL_LOG, CONFIG_MY_PROJECT_2X_PWM_GROUP_ID, ret);
        return HW_HAL_ERR_OS;
    }
#endif

    g_hw.pwm_inited = true;
    uint32_t freq = uapi_pwm_get_frequency((uint8_t)CONFIG_MY_PROJECT_2X_PWM_CHANNEL);
    osal_printk("%s[BP] pwm_buzzer_init OK pin:%u mode:%u ch:%u freq:%uHz\r\n",
                HW_HAL_LOG, CONFIG_MY_PROJECT_2X_PWM_PIN, CONFIG_MY_PROJECT_2X_PWM_PIN_MODE,
                CONFIG_MY_PROJECT_2X_PWM_CHANNEL, freq);
    return HW_HAL_OK;
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

    /* LED使用GPIO驱动 */
    (void)uapi_pin_set_mode((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, HAL_PIO_FUNC_GPIO);
    (void)uapi_gpio_set_dir((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_DIRECTION_OUTPUT);
    (void)uapi_gpio_set_val((pin_t)CONFIG_MY_PROJECT_2X_LED_GPIO, GPIO_LEVEL_LOW);

    /* 蜂鸣器使用PWM驱动（无源蜂鸣器） */
    hw_hal_status_t pwm_ret = hw_hal_pwm_buzzer_init();
    if (pwm_ret != HW_HAL_OK) {
        osal_printk("%s[BP] pwm_buzzer_init FAIL st=%d\r\n", HW_HAL_LOG, pwm_ret);
    }

    g_hw.timer.handler = hw_hal_auto_off_timer_handler;
    g_hw.timer.data = 0;
    g_hw.timer.interval = BEEP_TOGGLE_PERIOD_MS;

    int ret = osal_timer_init(&g_hw.timer);
    if (ret != OSAL_SUCCESS) {
        osal_printk("%s[BP] init FAIL timer_init ret=%d\r\n", HW_HAL_LOG, ret);
        return HW_HAL_ERR_OS;
    }

    g_hw.initialized = true;
    osal_printk("%s[BP] init OK led_gpio=%d pwm_pin=%u auto_off=%dms\r\n",
                HW_HAL_LOG, CONFIG_MY_PROJECT_2X_LED_GPIO,
                CONFIG_MY_PROJECT_2X_PWM_PIN, CONFIG_MY_PROJECT_2X_BEEP_AUTO_OFF_MS);
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
    unused(ms);
    osal_printk("%s[BP] beep_on request (intermittent pattern)\r\n", HW_HAL_LOG);

    if (hardware_hal_init() != HW_HAL_OK) {
        osal_printk("%s[BP] beep_on FAIL hal_init error\r\n", HW_HAL_LOG);
        return HW_HAL_ERR_OS;
    }

    /* 启动PWM方波驱动无源蜂鸣器 */
    hw_hal_pwm_buzzer_start();
    g_hw.beep_on = true;
    g_hw.toggle_count = 0;

    /* 启动200ms周期定时器，交替开/关蜂鸣器 */
    (void)osal_timer_stop(&g_hw.timer);
    int ret = osal_timer_mod(&g_hw.timer, BEEP_TOGGLE_PERIOD_MS);
    if (ret != OSAL_SUCCESS) {
        hw_hal_force_all_off();
        osal_printk("%s[BP] beep_on FAIL timer_mod ret=%d\r\n", HW_HAL_LOG, ret);
        return HW_HAL_ERR_OS;
    }

    ret = osal_timer_start(&g_hw.timer);
    if (ret != OSAL_SUCCESS) {
        hw_hal_force_all_off();
        osal_printk("%s[BP] beep_on FAIL timer_start ret=%d\r\n", HW_HAL_LOG, ret);
        return HW_HAL_ERR_OS;
    }

    osal_printk("%s[BP] beep_on OK intermittent %ums on/%ums off, total %ums\r\n",
                HW_HAL_LOG, BEEP_TOGGLE_PERIOD_MS, BEEP_TOGGLE_PERIOD_MS, BEEP_TOGGLE_TOTAL_MS);
    return HW_HAL_OK;
}

hw_hal_status_t hardware_hal_beep_off(void)
{
    osal_printk("%s[BP] beep_off\r\n", HW_HAL_LOG);

    if (!g_hw.initialized) {
        osal_printk("%s[BP] beep_off skip not inited\r\n", HW_HAL_LOG);
        return HW_HAL_OK;
    }

    hw_hal_pwm_buzzer_stop();
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

/* 电压转百分比：线性映射 2000~3000mV → 0~100% */
static uint8_t hw_hal_mv_to_percent(int mv)
{
    if (mv >= (int)BATTERY_VOLTAGE_MAX_MV) {
        return BATTERY_PERCENT_MAX;
    }
    if (mv <= (int)BATTERY_VOLTAGE_MIN_MV) {
        return 0;
    }
    return (uint8_t)((mv - (int)BATTERY_VOLTAGE_MIN_MV) / 10);
}

uint8_t hw_hal_battery_read_percent(void)
{
    /* 首次调用时初始化 ADC */
    if (!g_adc_inited) {
        errcode_t ret = uapi_adc_init(ADC_CLOCK_NONE);
        if (ret != ERRCODE_SUCC) {
            osal_printk("%s[BP] adc_init FAIL ret:0x%x\r\n", HW_HAL_LOG, ret);
            return g_battery_cache;
        }
        g_adc_inited = true;
        osal_printk("%s[BP] adc_init OK\r\n", HW_HAL_LOG);
    }

    /* 单次采样，返回毫伏值 */
    int mv = adc_port_gadc_entirely_sample(BATTERY_GADC_CHANNEL);
    if (mv == BATTERY_ADC_INVALID_MV || mv < 0) {
        osal_printk("%s[BP] adc_sample FAIL mv=%d, use cache=%u\r\n",
                    HW_HAL_LOG, mv, g_battery_cache);
        return g_battery_cache;
    }

    uint8_t percent = hw_hal_mv_to_percent(mv);
    g_battery_cache = percent;

    osal_printk("%s[BP] battery read mv=%d pct=%u\r\n", HW_HAL_LOG, mv, percent);
    return percent;
}
