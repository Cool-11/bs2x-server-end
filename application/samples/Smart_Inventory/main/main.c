/**
 *******************************************************************************
 * @file main.c
 * @brief Smart Inventory Tag - Main Entry and State Machine Implementation
 *
 *  Description:
 *   本模块是星闪无感盘点标签(Smart Inventory Tag)的主入口和状态机控制中心。
 *   负责：系统初始化、状态机管理、业务任务调度。
 *
 *  状态机:
 *   - STATE_INIT      : 系统初始化
 *   - STATE_ADV       : 广播阶段（阶段一：无连接极速广播）
 *   - STATE_CONNECTED : 连接阶段（阶段二：定向连接与重传）
 *   - STATE_SLEEP     : 深度休眠阶段（阶段三）
 *   - STATE_MESH_RELAY: Mesh转发阶段（阶段四，预留）
 *
 * @version V1.0
 * @date 2024
 *******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "app_init.h"
#include "main.h"
#include "common.h"
#include "data.h"
#include "callback.h"
#include "adv.h"
#include "conn.h"
#include "pm.h"
#include "sle_common.h"
#include "sle_device_manager.h"

#define TASK_NAME_INVENTORY "inventory_task"
#define TASK_STACK_SIZE      0x1000
#define TASK_PRIORITY        20

#define ADV_DURATION_MS      5000
#define REPORT_DELAY_MS      100
#define SLEEP_DELAY_MS        500

static tag_state_t g_current_state = STATE_INIT;
static tag_state_t g_previous_state = STATE_INIT;
static tag_runtime_info_t g_runtime_info = {0};
static osal_task *g_main_task = NULL;

static void tag_state_machine(void *arg);
static errcode_t tag_init_all_modules(void);
static errcode_t tag_report_data_via_adv(void);
static errcode_t tag_report_data_via_conn(void);
static errcode_t tag_enter_sleep(void);
static errcode_t tag_handle_wakeup(void);

const char *tag_state_get_name(tag_state_t state)
{
    switch (state) {
        case STATE_INIT:       return "INIT";
        case STATE_ADV:        return "ADV";
        case STATE_CONNECTED:  return "CONNECTED";
        case STATE_SLEEP:      return "SLEEP";
        case STATE_MESH_RELAY: return "MESH_RELAY";
        default:               return "UNKNOWN";
    }
}

tag_state_t tag_get_current_state(void)
{
    return g_current_state;
}

tag_runtime_info_t *tag_get_runtime_info(void)
{
    return &g_runtime_info;
}

static errcode_t tag_init_all_modules(void)
{
    errcode_t ret;

    LOGI("=== Initializing Smart Inventory Tag ===");

    common_log_set_level(LOG_LEVEL_INFO);

    ret = data_init();
    if (ret != 0) {
        LOGE("Data module init failed: 0x%08X", ret);
        return ret;
    }

    ret = callback_register_all();
    if (ret != 0) {
        LOGE("Callback register failed: 0x%08X", ret);
        return ret;
    }

    ret = adv_init();
    if (ret != 0) {
        LOGE("Adv module init failed: 0x%08X", ret);
        return ret;
    }

    ret = conn_init();
    if (ret != 0) {
        LOGE("Conn module init failed: 0x%08X", ret);
        return ret;
    }

    ret = conn_register_server();
    if (ret != 0) {
        LOGE("GATT server register failed: 0x%08X", ret);
        return ret;
    }

    ret = pm_init();
    if (ret != 0) {
        LOGE("PM module init failed: 0x%08X", ret);
        return ret;
    }

    LOGI("=== All modules initialized ===");
    return 0;
}

static void tag_connect_callback(void)
{
    LOGI("Connection established callback");
    g_runtime_info.conn_count++;
}

static void tag_disconnect_callback(void)
{
    LOGI("Disconnected callback");
}

static errcode_t tag_report_data_via_adv(void)
{
    inventory_info_t info = {0};
    errcode_t ret = data_get_inventory_info(&info);
    if (ret != 0) {
        LOGE("Get inventory info failed: 0x%08X", ret);
        return ret;
    }

    char mac_str[32] = {0};
    data_mac_to_string(info.mac, mac_str);
    LOGI("Reporting via ADV: MAC=%s, Battery=%d%%, GoodsID=0x%08X",
         mac_str, info.battery, info.goods_id);

    ret = adv_update_broadcast_data((const uint8_t *)&info,
                                     sizeof(inventory_info_t) > 50 ? 50 : sizeof(inventory_info_t));
    if (ret != 0) {
        LOGE("Update broadcast data failed: 0x%08X", ret);
    }

    return ret;
}

static errcode_t tag_report_data_via_conn(void)
{
    if (!conn_is_connected()) {
        LOGE("Not connected, cannot report via conn");
        return ERR_COMMON_INVALID_PARAM;
    }

    adv_goods_data_t mock_data = {0};
    errcode_t ret = data_get_mock_goods_data(&mock_data);
    if (ret != 0) {
        LOGE("Get mock goods data failed: 0x%08X", ret);
        return ret;
    }

    char mac_str[32] = {0};
    data_mac_to_string(mock_data.mac, mac_str);
    LOGI("Reporting via CONN: MAC=%s, Battery=%d%%, GoodsID=0x%02X%02X%02X%02X, Status=0x%02X",
         mac_str, mock_data.battery,
         mock_data.goods_id[0], mock_data.goods_id[1],
         mock_data.goods_id[2], mock_data.goods_id[3],
         mock_data.status);

    ret = conn_send_notify((const uint8_t *)&mock_data, sizeof(mock_data));
    if (ret != 0) {
        LOGE("Send notify failed: 0x%08X", ret);
    }

    return ret;
}

static errcode_t tag_enter_sleep(void)
{
    LOGI("Entering sleep mode...");

    errcode_t ret = adv_stop();
    if (ret != 0) {
        LOGW("Stop adv failed before sleep: 0x%08X", ret);
    }

    ret = conn_disconnect();
    if (ret != 0) {
        LOGW("Disconnect failed before sleep: 0x%08X", ret);
    }

    common_msleep(SLEEP_DELAY_MS);

    ret = pm_enter_deep_sleep();
    if (ret != 0) {
        LOGE("Enter deep sleep failed: 0x%08X", ret);
        return ret;
    }

    return 0;
}

static errcode_t tag_handle_wakeup(void)
{
    LOGI("Handling wakeup event");
    g_runtime_info.wakeup_count++;

    pm_wakeup();

    return 0;
}

static void tag_state_machine(void *arg)
{
    (void)arg;
    uint32_t start_time;
    errcode_t ret;

    LOGI("Tag state machine started");

    while (1) {
        switch (g_current_state) {
            case STATE_INIT:
                LOGI("[STATE_MACHINE] Current state: INIT");
                ret = tag_init_all_modules();
                if (ret != 0) {
                    LOGE("Module init failed, retrying in 1s...");
                    common_msleep(1000);
                    break;
                }

                conn_set_connect_callback(tag_connect_callback);
                conn_set_disconnect_callback(tag_disconnect_callback);

                g_runtime_info.is_initialized = 1;
                LOGI("Initialization complete, transitioning to ADV");
                tag_state_transition(STATE_ADV);
                break;

            case STATE_ADV:
                LOGI("[STATE_MACHINE] Current state: ADV");
                start_time = common_get_tick_ms();
                g_runtime_info.adv_count++;

                data_generate_mock_battery();
                data_generate_mock_goods_id();

                ret = adv_start(ADV_DURATION_MS);
                if (ret != 0) {
                    LOGE("Start adv failed: 0x%08X, retrying...", ret);
                    common_msleep(1000);
                    break;
                }

                while (!common_is_timeout(start_time, ADV_DURATION_MS)) {
                    if (conn_is_connected()) {
                        LOGI("Connection established during ADV, transitioning to CONNECTED");
                        tag_state_transition(STATE_CONNECTED);
                        break;
                    }
                    common_msleep(100);
                }

                if (g_current_state == STATE_ADV) {
                    LOGI("ADV duration complete, reporting data");
                    tag_report_data_via_adv();
                    common_msleep(REPORT_DELAY_MS);

                    if (conn_is_connected()) {
                        tag_state_transition(STATE_CONNECTED);
                    } else {
                        LOGI("No connection, entering sleep");
                        tag_state_transition(STATE_SLEEP);
                    }
                }
                break;

            case STATE_CONNECTED:
                LOGI("[STATE_MACHINE] Current state: CONNECTED");

                tag_report_data_via_conn();

                start_time = common_get_tick_ms();
                while (!common_is_timeout(start_time, 3000)) {
                    if (!conn_is_connected()) {
                        LOGI("Connection lost, transitioning to ADV");
                        tag_state_transition(STATE_ADV);
                        break;
                    }
                    common_msleep(100);
                }

                if (g_current_state == STATE_CONNECTED) {
                    if (conn_is_connected()) {
                        ret = conn_update_param(CONN_INTERVAL_MIN, CONN_INTERVAL_MAX, 0, CONN_SUPERVISION_TIMEOUT);
                        if (ret != 0) {
                            LOGW("Update conn param failed: 0x%08X", ret);
                        }
                    }

                    LOGI("Connection phase complete, entering sleep");
                    tag_state_transition(STATE_SLEEP);
                }
                break;

            case STATE_SLEEP:
                LOGI("[STATE_MACHINE] Current state: SLEEP");

                ret = tag_enter_sleep();
                if (ret != 0) {
                    LOGE("Enter sleep failed: 0x%08X, retrying...", ret);
                    common_msleep(1000);
                    break;
                }

                LOGI("Woke up from sleep, transitioning to ADV");
                tag_handle_wakeup();
                tag_state_transition(STATE_ADV);
                break;

            case STATE_MESH_RELAY:
                LOGI("[STATE_MACHINE] Current state: MESH_RELAY (reserved)");
                LOGI("Mesh relay not implemented, entering sleep");
                tag_state_transition(STATE_SLEEP);
                break;

            default:
                LOGE("[STATE_MACHINE] Unknown state: %d, resetting to INIT", g_current_state);
                tag_state_transition(STATE_INIT);
                break;
        }
    }
}

errcode_t tag_state_machine_init(void)
{
    errcode_t ret;

    g_current_state = STATE_INIT;
    g_previous_state = STATE_INIT;
    (void)memset_s(&g_runtime_info, sizeof(g_runtime_info), 0, sizeof(g_runtime_info));

    LOGI("Creating tag main task...");
    g_main_task = common_task_create(TASK_NAME_INVENTORY,
                                      (osal_kthread_handler)tag_state_machine,
                                      0,
                                      TASK_STACK_SIZE,
                                      TASK_PRIORITY);
    if (g_main_task == NULL) {
        LOGE("Create task failed");
        return ERR_COMMON_NO_RESOURCE;
    }

    LOGI("Tag main task created successfully");
    return 0;
}

errcode_t tag_state_transition(tag_state_t new_state)
{
    if (new_state >= STATE_MAX) {
        LOGE("Invalid state: %d", new_state);
        return ERR_COMMON_INVALID_PARAM;
    }

    if (g_current_state == new_state) {
        return 0;
    }

    const char *old_name = tag_state_get_name(g_current_state);
    const char *new_name = tag_state_get_name(new_state);

    LOGI("[STATE_TRANS] %s -> %s", old_name, new_name);

    g_previous_state = g_current_state;
    g_current_state = new_state;
    g_runtime_info.current_state = new_state;
    g_runtime_info.previous_state = g_previous_state;

    return 0;
}

void tag_application_entry(void)
{
    LOGI("========================================");
    LOGI("  Smart Inventory Tag Application");
    LOGI("  Version: V1.0");
    LOGI("  Build: %s %s", __DATE__, __TIME__);
    LOGI("========================================");

    errcode_t ret = tag_state_machine_init();
    if (ret != 0) {
        LOGE("Tag state machine init failed: 0x%08X", ret);
        return;
    }
}

app_run(tag_application_entry);
