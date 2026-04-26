#include "app_init.h"
#include "common_def.h"
#include "errcode.h"
#include "hardware_hal.h"
#include "pm.h"
#include "pm_sys.h"
#include "shared_protocol.h"
#include "sle_slave_mgr.h"
#include "soc_osal.h"
#include "storage_sync.h"

#define MY_PROJECT_2X_LOG "[BS2x_INIT]"
#define MY_PROJECT_2X_FIND_MS 15000u
#define MY_PROJECT_2X_WORK_TO_STANDBY_MS 0xFFFFFFFFu
#define MY_PROJECT_2X_STANDBY_TO_SLEEP_MS 0xFFFFFFFFu
#define MY_PROJECT_2X_DEFAULT_TAG_ID 1u
#define MY_PROJECT_2X_DEFAULT_QTY 0u
#define MY_PROJECT_2X_DEFAULT_BATTERY 100u

static void my_project_2x_on_unicast_cmd(const shared_proto_unicast_cmd_t *cmd)
{
    if (cmd == NULL) {
        return;
    }

    (void)uapi_pm_work_state_reset();

    switch (cmd->action) {
        case SHARED_PROTO_ACTION_FIND_ME:
            osal_printk("[BS2x_SLE] Received SSAP Write, Value: 0x%02x\r\n", SHARED_PROTO_CMD_FIND_ME);
            (void)hardware_hal_beep_on_for_ms(MY_PROJECT_2X_FIND_MS);
            (void)hardware_hal_led_on_for_ms(MY_PROJECT_2X_FIND_MS);
            (void)storage_sync_set_find_status(true);
            (void)storage_sync_publish();
            break;
        case SHARED_PROTO_ACTION_STOP_FIND:
            osal_printk("[BS2x_SLE] Received SSAP Write, Value: 0x%02x\r\n", SHARED_PROTO_CMD_STOP_FIND);
            (void)hardware_hal_beep_off();
            (void)hardware_hal_led_off();
            (void)storage_sync_set_find_status(false);
            (void)storage_sync_publish();
            break;
        case SHARED_PROTO_ACTION_UPDATE_QTY:
            osal_printk("[BS2x_SLE] Received SSAP Write, Value: 0x%02x qty:%u\r\n",
                        SHARED_PROTO_CMD_UPDATE_QTY,
                        cmd->qty);
            (void)storage_sync_set_qty(cmd->qty);
            (void)storage_sync_publish();
            break;
        default:
            osal_printk("[BS2x_SLE] unknown cmd action:%u\r\n", cmd->action);
            break;
    }
}

static int32_t my_project_2x_work_to_standby(uintptr_t arg)
{
    unused(arg);
    osal_printk("%s work->standby\r\n", MY_PROJECT_2X_LOG);
    return 0;
}

static int32_t my_project_2x_standby_to_sleep(uintptr_t arg)
{
    unused(arg);
    osal_printk("%s standby->sleep\r\n", MY_PROJECT_2X_LOG);
    (void)sle_slave_stop();
    return 0;
}

static int32_t my_project_2x_standby_to_work(uintptr_t arg)
{
    unused(arg);
    osal_printk("%s standby->work\r\n", MY_PROJECT_2X_LOG);
    (void)sle_slave_start();
    return 0;
}

static int32_t my_project_2x_sleep_to_work(uintptr_t arg)
{
    unused(arg);
    osal_printk("%s sleep->work\r\n", MY_PROJECT_2X_LOG);
    (void)sle_slave_start();
    return 0;
}

static void my_project_2x_pm_init(void)
{
#if defined(CONFIG_PM_SYS_SUPPORT)
    pm_state_trans_handler_t handler = {
        .work_to_standby = my_project_2x_work_to_standby,
        .standby_to_sleep = my_project_2x_standby_to_sleep,
        .standby_to_work = my_project_2x_standby_to_work,
        .sleep_to_work = my_project_2x_sleep_to_work,
    };

    (void)uapi_pm_state_trans_handler_register(&handler);
    (void)uapi_pm_work_state_reset();
    (void)uapi_pm_set_state_trans_duration(MY_PROJECT_2X_WORK_TO_STANDBY_MS,
                                           MY_PROJECT_2X_STANDBY_TO_SLEEP_MS);
#else
    unused(my_project_2x_work_to_standby);
    unused(my_project_2x_standby_to_sleep);
    unused(my_project_2x_standby_to_work);
    unused(my_project_2x_sleep_to_work);
#endif
}

static void my_project_2x_entry(void)
{
    osal_printk("%s Entering my_project_2x_entry\r\n", MY_PROJECT_2X_LOG);

    my_project_2x_pm_init();

    if (hardware_hal_init() != HW_HAL_OK) {
        osal_printk("%s hardware init fail\r\n", MY_PROJECT_2X_LOG);
    }

    sle_slave_callbacks_t slave_cb = {
        .on_unicast_cmd = my_project_2x_on_unicast_cmd,
    };

    errcode_t ret = sle_slave_init(&slave_cb);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle init fail:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
        return;
    }

    storage_sync_adapter_t adapter = {
        .refresh_adv_cb = sle_slave_refresh_adv_payload,
    };
    ret = storage_sync_init(MY_PROJECT_2X_DEFAULT_TAG_ID,
                            MY_PROJECT_2X_DEFAULT_QTY,
                            MY_PROJECT_2X_DEFAULT_BATTERY,
                            &adapter);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s storage_sync_init fail:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
        return;
    }

    ret = storage_sync_publish();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s initial publish fail:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
    }
}

app_run(my_project_2x_entry);
