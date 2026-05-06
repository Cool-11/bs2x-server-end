#include "app_init.h"
#include "common_def.h"
#include "errcode.h"
#include "hardware_hal.h"
#include "pm.h"
#include "pm_sys.h"
#include "securec.h"
#include "shared_protocol.h"
#include "sle_slave_mgr.h"
#include "soc_osal.h"
#include "storage_sync.h"
#include "uart.h"
#include "pinctrl.h"
#include "platform_core.h"

#define MY_PROJECT_2X_LOG "[BS2x_APP]"
#define MY_PROJECT_2X_FIND_MS 15000u
#define MY_PROJECT_2X_FIND_STATUS_RESTORE_MS 15000u
#define MY_PROJECT_2X_WORK_TO_STANDBY_MS 5000u
#define MY_PROJECT_2X_STANDBY_TO_SLEEP_MS 30000u
#define MY_PROJECT_2X_DEFAULT_TAG_ID 1u
#define MY_PROJECT_2X_DEFAULT_QTY 0u
#define MY_PROJECT_2X_DEFAULT_BATTERY 100u

#define UART_SELFTEST_BUS UART_BUS_0
#define UART_SELFTEST_RX_BUF_SIZE 32u
#define UART_SELFTEST_BAUDRATE 115200

static osal_timer g_find_status_restore_timer = {0};
static bool g_find_status_timer_inited = false;

static uint8_t g_uart_rx_buf[UART_SELFTEST_RX_BUF_SIZE] = {0};
static volatile uint16_t g_uart_rx_len = 0;
static uint8_t g_uart_rx_buffer[UART_SELFTEST_RX_BUF_SIZE] = {0};

static errcode_t my_project_2x_start_find_status_restore_timer(void);

static void my_project_2x_uart_selftest_exec(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    shared_proto_unicast_cmd_t cmd = {0};
    bool parsed = shared_proto_parse_unicast_cmd(data, len, &cmd);
    if (!parsed) {
        osal_printk("%s[UART_TEST] parse FAIL cmd:0x%02X len:%u\r\n",
                    MY_PROJECT_2X_LOG, data[0], len);
        return;
    }

    (void)uapi_pm_work_state_reset();

    switch (cmd.action) {
        case SHARED_PROTO_ACTION_FIND_ME:
            osal_printk("%s[UART_TEST] >> FIND_ME\r\n", MY_PROJECT_2X_LOG);
            (void)hardware_hal_beep_on_for_ms(MY_PROJECT_2X_FIND_MS);
            (void)hardware_hal_led_on_for_ms(MY_PROJECT_2X_FIND_MS);
            (void)storage_sync_set_find_status(true);
            (void)storage_sync_publish();
            (void)my_project_2x_start_find_status_restore_timer();
            osal_printk("%s[UART_TEST] << FIND_ME done\r\n", MY_PROJECT_2X_LOG);
            break;
        case SHARED_PROTO_ACTION_STOP_FIND:
            osal_printk("%s[UART_TEST] >> STOP_FIND\r\n", MY_PROJECT_2X_LOG);
            (void)hardware_hal_beep_off();
            (void)hardware_hal_led_off();
            (void)storage_sync_set_find_status(false);
            (void)storage_sync_publish();
            osal_printk("%s[UART_TEST] << STOP_FIND done\r\n", MY_PROJECT_2X_LOG);
            break;
        case SHARED_PROTO_ACTION_INVENTORY: {
            osal_printk("%s[UART_TEST] >> INVENTORY\r\n", MY_PROJECT_2X_LOG);
            shared_proto_adv_field_t field = {0};
            storage_sync_get_field(&field);
            osal_printk("%s[UART_TEST] tag:%u qty:%u status:0x%02x bat:%u seq:%u\r\n",
                        MY_PROJECT_2X_LOG, field.tag_id, field.qty, field.status, field.battery, field.seq);
            osal_printk("%s[UART_TEST] << INVENTORY done\r\n", MY_PROJECT_2X_LOG);
            break;
        }
        case SHARED_PROTO_ACTION_UPDATE_QTY:
            osal_printk("%s[UART_TEST] >> UPDATE_QTY qty:%u\r\n", MY_PROJECT_2X_LOG, cmd.qty);
            (void)storage_sync_set_qty(cmd.qty);
            (void)storage_sync_publish();
            osal_printk("%s[UART_TEST] << UPDATE_QTY done\r\n", MY_PROJECT_2X_LOG);
            break;
        case SHARED_PROTO_ACTION_BIND_TAG:
            osal_printk("%s[UART_TEST] >> BIND_TAG tag_id:%u\r\n", MY_PROJECT_2X_LOG, cmd.tag_id);
            (void)storage_sync_set_tag_id(cmd.tag_id);
            (void)storage_sync_publish();
            osal_printk("%s[UART_TEST] << BIND_TAG done\r\n", MY_PROJECT_2X_LOG);
            break;
        default:
            osal_printk("%s[UART_TEST] unknown action:%u\r\n", MY_PROJECT_2X_LOG, cmd.action);
            break;
    }
}

static void my_project_2x_uart_rx_callback(const void *buffer, uint16_t length, bool error)
{
    if (error || buffer == NULL || length == 0) {
        return;
    }

    uint16_t copy_len = length;
    if (g_uart_rx_len + copy_len > UART_SELFTEST_RX_BUF_SIZE) {
        copy_len = UART_SELFTEST_RX_BUF_SIZE - g_uart_rx_len;
    }

    if (copy_len > 0) {
        (void)memcpy_s(g_uart_rx_buf + g_uart_rx_len, UART_SELFTEST_RX_BUF_SIZE - g_uart_rx_len,
                        buffer, copy_len);
        g_uart_rx_len += copy_len;
    }
}

static errcode_t my_project_2x_uart_selftest_init(void)
{
    osal_printk("%s[UART_TEST] init enter bus=%d baud=%u\r\n",
                MY_PROJECT_2X_LOG, UART_SELFTEST_BUS, UART_SELFTEST_BAUDRATE);

    uart_buffer_config_t buffer_config = {
        .rx_buffer = g_uart_rx_buffer,
        .rx_buffer_size = UART_SELFTEST_RX_BUF_SIZE,
    };

    uart_attr_t attr = {
        .baud_rate = UART_SELFTEST_BAUDRATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE,
    };

    uart_pin_config_t pin_config = {
        .tx_pin = S_MGPIO26,
        .rx_pin = S_MGPIO27,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE,
    };

    uapi_uart_deinit(UART_SELFTEST_BUS);
    errcode_t ret = uapi_uart_init(UART_SELFTEST_BUS, &pin_config, &attr, NULL, &buffer_config);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[UART_TEST] uart_init FAIL ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
        return ret;
    }

    ret = uapi_uart_register_rx_callback(UART_SELFTEST_BUS,
                                          UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE,
                                          1, my_project_2x_uart_rx_callback);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[UART_TEST] register_rx_cb FAIL ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
        return ret;
    }

    osal_printk("%s[UART_TEST] init OK\r\n", MY_PROJECT_2X_LOG);
    osal_printk("%s[UART_TEST] 命令表(HEX模式发送):\r\n", MY_PROJECT_2X_LOG);
    osal_printk("%s[UART_TEST]   01       = 寻物(FIND_ME)\r\n", MY_PROJECT_2X_LOG);
    osal_printk("%s[UART_TEST]   00       = 停止寻物(STOP_FIND)\r\n", MY_PROJECT_2X_LOG);
    osal_printk("%s[UART_TEST]   02       = 盘点(INVENTORY)\r\n", MY_PROJECT_2X_LOG);
    osal_printk("%s[UART_TEST]   10 XX XX = 更新数量(UPDATE_QTY)\r\n", MY_PROJECT_2X_LOG);
    osal_printk("%s[UART_TEST]   20 XX XX = 绑定标签(BIND_TAG)\r\n", MY_PROJECT_2X_LOG);
    return ERRCODE_SUCC;
}

static void my_project_2x_find_status_restore_handler(unsigned long data)
{
    unused(data);
    osal_printk("%s[BP] find status auto-restore triggered\r\n", MY_PROJECT_2X_LOG);
    errcode_t ret = storage_sync_set_find_status(false);
    osal_printk("%s[BP] set_find_status ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
    ret = storage_sync_publish();
    osal_printk("%s[BP] publish ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
}

static errcode_t my_project_2x_start_find_status_restore_timer(void)
{
    if (!g_find_status_timer_inited) {
        g_find_status_restore_timer.handler = my_project_2x_find_status_restore_handler;
        g_find_status_restore_timer.data = 0;
        g_find_status_restore_timer.interval = MY_PROJECT_2X_FIND_STATUS_RESTORE_MS;
        int ret = osal_timer_init(&g_find_status_restore_timer);
        if (ret != OSAL_SUCCESS) {
            osal_printk("%s[BP] find timer init FAIL ret=%d\r\n", MY_PROJECT_2X_LOG, ret);
            return ERRCODE_FAIL;
        }
        g_find_status_timer_inited = true;
        osal_printk("%s[BP] find timer inited OK\r\n", MY_PROJECT_2X_LOG);
    }

    (void)osal_timer_stop(&g_find_status_restore_timer);
    int ret = osal_timer_mod(&g_find_status_restore_timer, MY_PROJECT_2X_FIND_STATUS_RESTORE_MS);
    if (ret != OSAL_SUCCESS) {
        osal_printk("%s[BP] find timer mod FAIL ret=%d\r\n", MY_PROJECT_2X_LOG, ret);
        return ERRCODE_FAIL;
    }

    ret = osal_timer_start(&g_find_status_restore_timer);
    if (ret != OSAL_SUCCESS) {
        osal_printk("%s[BP] find timer start FAIL ret=%d\r\n", MY_PROJECT_2X_LOG, ret);
        return ERRCODE_FAIL;
    }

    osal_printk("%s[BP] find timer started %ums\r\n", MY_PROJECT_2X_LOG, MY_PROJECT_2X_FIND_STATUS_RESTORE_MS);
    return ERRCODE_SUCC;
}

static void my_project_2x_send_inventory_rsp(uint16_t conn_id)
{
    osal_printk("%s[BP] send_inventory_rsp enter conn_id:0x%x\r\n", MY_PROJECT_2X_LOG, conn_id);

    shared_proto_adv_field_t field = {0};
    storage_sync_get_field(&field);

    shared_proto_inventory_rsp_t rsp = {
        .cmd = SHARED_PROTO_RSP_INVENTORY,
        .tag_id = field.tag_id,
        .qty = field.qty,
        .status = field.status,
        .battery = field.battery,
        .seq = field.seq,
    };

    osal_printk("%s[BP] inventory data tag:%u qty:%u status:0x%02x bat:%u seq:%u\r\n",
                MY_PROJECT_2X_LOG, rsp.tag_id, rsp.qty, rsp.status, rsp.battery, rsp.seq);

    errcode_t ret = sle_slave_notify_conn(conn_id, (const uint8_t *)&rsp, sizeof(rsp));
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] inventory notify FAIL ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
    } else {
        osal_printk("%s[BP] inventory notify OK\r\n", MY_PROJECT_2X_LOG);
    }
}

static void my_project_2x_send_bind_rsp(uint16_t conn_id, uint16_t tag_id, bool success)
{
    shared_proto_bind_rsp_t rsp = {
        .cmd = success ? SHARED_PROTO_RSP_BIND_OK : SHARED_PROTO_RSP_BIND_FAIL,
        .tag_id = tag_id,
    };

    osal_printk("%s[BP] send_bind_rsp conn_id:0x%x cmd:0x%02x tag_id:%u\r\n",
                MY_PROJECT_2X_LOG, conn_id, rsp.cmd, rsp.tag_id);

    errcode_t ret = sle_slave_notify_conn(conn_id, (const uint8_t *)&rsp, sizeof(rsp));
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] bind notify FAIL ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
    } else {
        osal_printk("%s[BP] bind notify OK\r\n", MY_PROJECT_2X_LOG);
    }
}

static void my_project_2x_on_unicast_cmd(uint16_t conn_id, const shared_proto_unicast_cmd_t *cmd)
{
    if (cmd == NULL) {
        osal_printk("%s[BP] on_unicast_cmd FAIL cmd=NULL\r\n", MY_PROJECT_2X_LOG);
        return;
    }

    osal_printk("%s[BP] on_unicast_cmd conn_id:0x%x action:%u qty:%u tag_id:%u\r\n",
                MY_PROJECT_2X_LOG, conn_id, cmd->action, cmd->qty, cmd->tag_id);

    (void)uapi_pm_work_state_reset();

    switch (cmd->action) {
        case SHARED_PROTO_ACTION_FIND_ME:
            osal_printk("%s[BP] >> FIND_ME\r\n", MY_PROJECT_2X_LOG);
            (void)hardware_hal_beep_on_for_ms(MY_PROJECT_2X_FIND_MS);
            (void)hardware_hal_led_on_for_ms(MY_PROJECT_2X_FIND_MS);
            (void)storage_sync_set_find_status(true);
            (void)storage_sync_publish();
            (void)my_project_2x_start_find_status_restore_timer();
            osal_printk("%s[BP] << FIND_ME done\r\n", MY_PROJECT_2X_LOG);
            break;
        case SHARED_PROTO_ACTION_STOP_FIND:
            osal_printk("%s[BP] >> STOP_FIND\r\n", MY_PROJECT_2X_LOG);
            (void)hardware_hal_beep_off();
            (void)hardware_hal_led_off();
            (void)storage_sync_set_find_status(false);
            (void)storage_sync_publish();
            osal_printk("%s[BP] << STOP_FIND done\r\n", MY_PROJECT_2X_LOG);
            break;
        case SHARED_PROTO_ACTION_INVENTORY:
            osal_printk("%s[BP] >> INVENTORY\r\n", MY_PROJECT_2X_LOG);
            my_project_2x_send_inventory_rsp(conn_id);
            osal_printk("%s[BP] << INVENTORY done\r\n", MY_PROJECT_2X_LOG);
            break;
        case SHARED_PROTO_ACTION_UPDATE_QTY:
            osal_printk("%s[BP] >> UPDATE_QTY qty:%u\r\n", MY_PROJECT_2X_LOG, cmd->qty);
            (void)storage_sync_set_qty(cmd->qty);
            (void)storage_sync_publish();
            osal_printk("%s[BP] << UPDATE_QTY done\r\n", MY_PROJECT_2X_LOG);
            break;
        case SHARED_PROTO_ACTION_BIND_TAG: {
            osal_printk("%s[BP] >> BIND_TAG tag_id:%u\r\n", MY_PROJECT_2X_LOG, cmd->tag_id);
            errcode_t bind_ret = storage_sync_set_tag_id(cmd->tag_id);
            if (bind_ret == ERRCODE_SUCC) {
                (void)storage_sync_publish();
                my_project_2x_send_bind_rsp(conn_id, cmd->tag_id, true);
            } else {
                my_project_2x_send_bind_rsp(conn_id, cmd->tag_id, false);
            }
            osal_printk("%s[BP] << BIND_TAG done ret:0x%x\r\n", MY_PROJECT_2X_LOG, bind_ret);
            break;
        }
        default:
            osal_printk("%s[BP] unknown cmd action:%u\r\n", MY_PROJECT_2X_LOG, cmd->action);
            break;
    }
}

static int32_t my_project_2x_work_to_standby(uintptr_t arg)
{
    unused(arg);
    osal_printk("%s[BP] PM work->standby\r\n", MY_PROJECT_2X_LOG);
    return 0;
}

static int32_t my_project_2x_standby_to_sleep(uintptr_t arg)
{
    unused(arg);
    osal_printk("%s[BP] PM standby->sleep\r\n", MY_PROJECT_2X_LOG);
    (void)sle_slave_stop();
    return 0;
}

static int32_t my_project_2x_standby_to_work(uintptr_t arg)
{
    unused(arg);
    osal_printk("%s[BP] PM standby->work\r\n", MY_PROJECT_2X_LOG);
    (void)sle_slave_start();
    return 0;
}

static int32_t my_project_2x_sleep_to_work(uintptr_t arg)
{
    unused(arg);
    osal_printk("%s[BP] PM sleep->work\r\n", MY_PROJECT_2X_LOG);
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
    osal_printk("%s[BP] PM init OK work_to_standby:%ums standby_to_sleep:%ums\r\n",
                MY_PROJECT_2X_LOG, MY_PROJECT_2X_WORK_TO_STANDBY_MS, MY_PROJECT_2X_STANDBY_TO_SLEEP_MS);
#else
    unused(my_project_2x_work_to_standby);
    unused(my_project_2x_standby_to_sleep);
    unused(my_project_2x_standby_to_work);
    unused(my_project_2x_sleep_to_work);
    osal_printk("%s[BP] PM not supported\r\n", MY_PROJECT_2X_LOG);
#endif
}

static void my_project_2x_entry(void)
{
    osal_printk("%s[BP] ===== APP ENTRY START =====\r\n", MY_PROJECT_2X_LOG);

    my_project_2x_pm_init();
    osal_printk("%s[BP] pm_init done\r\n", MY_PROJECT_2X_LOG);

    if (hardware_hal_init() != HW_HAL_OK) {
        osal_printk("%s[BP] hardware_hal_init FAIL\r\n", MY_PROJECT_2X_LOG);
    } else {
        osal_printk("%s[BP] hardware_hal_init OK\r\n", MY_PROJECT_2X_LOG);
    }

    sle_slave_callbacks_t slave_cb = {
        .on_unicast_cmd = my_project_2x_on_unicast_cmd,
    };

    errcode_t ret = sle_slave_init(&slave_cb);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] sle_slave_init FAIL ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
        return;
    }
    osal_printk("%s[BP] sle_slave_init OK\r\n", MY_PROJECT_2X_LOG);

    storage_sync_adapter_t adapter = {
        .refresh_adv_cb = sle_slave_refresh_adv_payload,
    };
    ret = storage_sync_init(MY_PROJECT_2X_DEFAULT_TAG_ID,
                            MY_PROJECT_2X_DEFAULT_QTY,
                            MY_PROJECT_2X_DEFAULT_BATTERY,
                            &adapter);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] storage_sync_init FAIL ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
        return;
    }
    osal_printk("%s[BP] storage_sync_init OK\r\n", MY_PROJECT_2X_LOG);

    ret = storage_sync_publish();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] initial publish FAIL ret:0x%x\r\n", MY_PROJECT_2X_LOG, ret);
    } else {
        osal_printk("%s[BP] initial publish OK\r\n", MY_PROJECT_2X_LOG);
    }

    osal_printk("%s[BP] ===== APP ENTRY DONE, entering main loop =====\r\n", MY_PROJECT_2X_LOG);

    errcode_t uart_ret = my_project_2x_uart_selftest_init();
    if (uart_ret != ERRCODE_SUCC) {
        osal_printk("%s[BP] uart_selftest_init FAIL ret:0x%x\r\n", MY_PROJECT_2X_LOG, uart_ret);
    }

    while (1) {
        if (g_uart_rx_len > 0) {
            uint16_t len = g_uart_rx_len;
            uint8_t local_buf[UART_SELFTEST_RX_BUF_SIZE] = {0};
            (void)memcpy_s(local_buf, UART_SELFTEST_RX_BUF_SIZE, g_uart_rx_buf, len);
            g_uart_rx_len = 0;

            osal_printk("%s[UART_TEST] recv %u bytes:", MY_PROJECT_2X_LOG, len);
            for (uint16_t i = 0; i < len; i++) {
                osal_printk(" %02X", local_buf[i]);
            }
            osal_printk("\r\n");

            my_project_2x_uart_selftest_exec(local_buf, len);
        }

        osal_msleep(50);
    }
}

app_run(my_project_2x_entry);
