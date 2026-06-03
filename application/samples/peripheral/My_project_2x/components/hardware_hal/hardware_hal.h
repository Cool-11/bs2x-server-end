#ifndef MY_PROJECT_2X_HARDWARE_HAL_H
#define MY_PROJECT_2X_HARDWARE_HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HW_HAL_OK = 0,
    HW_HAL_ERR_PARAM = -1,
    HW_HAL_ERR_OS = -2,
} hw_hal_status_t;

hw_hal_status_t hardware_hal_init(void);

hw_hal_status_t hardware_hal_beep_on_for_ms(uint32_t ms);
hw_hal_status_t hardware_hal_beep_off(void);

hw_hal_status_t hardware_hal_led_on_for_ms(uint32_t ms);
hw_hal_status_t hardware_hal_led_off(void);

/**
 * @brief 读取电池电量百分比
 * @return 0~100 的电量百分比，读取失败返回上次缓存值
 */
uint8_t hw_hal_battery_read_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* MY_PROJECT_2X_HARDWARE_HAL_H */
