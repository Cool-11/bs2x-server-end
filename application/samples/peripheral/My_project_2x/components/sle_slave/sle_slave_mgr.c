#include "sle_slave_mgr.h"

#include "../shared_protocol/shared_protocol.h"

#include "sle_errcode.h"

#include "common_def.h"
#include "securec.h"
#include "soc_osal.h"

#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_server.h"
#include "nv.h"
#include "tcxo.h"
#include "efuse.h"

#ifndef CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE
#define CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE 1
#endif

#define SLE_SLAVE_LOG "[BS2x_SLE]"

#define NV_ID_BS2X_CUSTOM_MAC 0x3000
#define SLE_ADV_DATA_LEN_MAX_LOCAL 251u
#define SLE_ADV_AD_TYPE_MANUFACTURER_SPECIFIC_DATA 0xFFu
#define SLE_ADV_MANUFACTURER_ID_L 0x5Au
#define SLE_ADV_MANUFACTURER_ID_H 0xA5u
#define SLE_ADV_MANUFACTURER_HEADER_LEN 4u
#define SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET 4u

#define SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL 0x01u
#define SLE_ADV_DATA_TYPE_ACCESS_MODE 0x02u
#define SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME 0x0Bu
#define SLE_ADV_DATA_TYPE_TX_POWER_LEVEL 0x0Cu

#define SLE_LOCAL_NAME "BS2x_Tag"

/* 静态广播载荷偏移量（相对于g_adv_payload）：
 * [0]=AD长度 [1]=AD类型 [2-3]=厂商ID [4-7]=magic
 * [8-9]=tag_id [10-11]=qty [12]=status [13]=battery [14-15]=seq
 */
#define ADV_OFFSET_TAG_ID   8u
#define ADV_OFFSET_QTY      10u
#define ADV_OFFSET_STATUS   12u
#define ADV_OFFSET_BATTERY  13u
#define ADV_OFFSET_SEQ      14u

#define BS21E_APP_UUID_LEN 16
#define BS21E_SERVICE_UUID_LEN 16
#define BS21E_PROP_UUID_LEN 16

static const uint8_t g_app_uuid[BS21E_APP_UUID_LEN] = {
    0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB
};

static const uint8_t g_service_uuid[BS21E_SERVICE_UUID_LEN] = {
    0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB
};

static const uint8_t g_property_uuid[BS21E_PROP_UUID_LEN] = {
    0x00, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB
};

static sle_slave_callbacks_t g_cb = {0};
static uint16_t g_conn_ids[CONFIG_MY_PROJECT_2X_MAX_CONNECTIONS] = {0};
static uint8_t g_active_conn_count = 0;
static uint8_t g_server_id = 0;
static uint16_t g_service_handle = 0;
static uint16_t g_property_handle = 0;

static uint8_t g_adv_payload[SLE_ADV_DATA_LEN_MAX_LOCAL] = {0};
static uint16_t g_adv_payload_len = 0;

/* 静态广播数据缓存（避免每次刷新重新序列化） */
static uint8_t g_announce_data[SLE_ADV_DATA_LEN_MAX_LOCAL] = {0};
static uint16_t g_announce_data_len = 0;
static uint8_t g_seek_rsp_data[SLE_ADV_DATA_LEN_MAX_LOCAL] = {0};
static uint16_t g_seek_rsp_data_len = 0;
static bool g_announce_data_inited = false;

static bool g_sle_stack_ready = false;
static bool g_adv_started = false;
static bool g_adv_configured = false;

/* 大端序写入辅助函数 */
static void adv_write_u16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val & 0xFF);
}

static void adv_write_u32_be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

/* 初始化静态广播数据缓存（只调用一次） */
static errcode_t sle_slave_init_static_announce_data(void)
{
    if (g_announce_data_inited) {
        return ERRCODE_SLE_SUCCESS;
    }

    uint16_t idx = 0;

    /* Discovery Level */
    g_announce_data[idx++] = 0x02;
    g_announce_data[idx++] = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL;
    g_announce_data[idx++] = SLE_ANNOUNCE_LEVEL_NORMAL;

    /* Access Mode */
    g_announce_data[idx++] = 0x02;
    g_announce_data[idx++] = SLE_ADV_DATA_TYPE_ACCESS_MODE;
    g_announce_data[idx++] = 0x00;

    /* 厂商数据头部 */
    uint16_t total_len = SLE_ADV_MANUFACTURER_HEADER_LEN + SHARED_PROTO_ADV_SERIALIZED_LEN;
    g_announce_data[idx++] = (uint8_t)(total_len - 1u);
    g_announce_data[idx++] = SLE_ADV_AD_TYPE_MANUFACTURER_SPECIFIC_DATA;
    g_announce_data[idx++] = SLE_ADV_MANUFACTURER_ID_L;
    g_announce_data[idx++] = SLE_ADV_MANUFACTURER_ID_H;

    /* magic（固定值） */
    adv_write_u32_be(&g_announce_data[idx], SHARED_PROTO_MAGIC);
    idx += 4;

    /* 默认值：tag_id=0, qty=0, status=UNBOUND, battery=100, seq=0 */
    adv_write_u16_be(&g_announce_data[idx + ADV_OFFSET_TAG_ID - 8], 0);
    adv_write_u16_be(&g_announce_data[idx + ADV_OFFSET_QTY - 8], 0);
    g_announce_data[idx + ADV_OFFSET_STATUS - 8] = SHARED_PROTO_STATUS_UNBOUND;
    g_announce_data[idx + ADV_OFFSET_BATTERY - 8] = 100;
    adv_write_u16_be(&g_announce_data[idx + ADV_OFFSET_SEQ - 8], 0);

    g_announce_data_len = idx + SHARED_PROTO_ADV_SERIALIZED_LEN - 4;  /* idx已含magic 4字节，减去避免重复计算 */

    /* 同步更新g_adv_payload（兼容旧代码） */
    if (memcpy_s(g_adv_payload, sizeof(g_adv_payload),
                 &g_announce_data[6], g_announce_data_len - 6) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    g_adv_payload_len = g_announce_data_len - 6;

    /* 初始化seek_rsp_data（TX功率+设备名，内容固定） */
    uint16_t rsp_idx = 0;
    g_seek_rsp_data[rsp_idx++] = 0x02;
    g_seek_rsp_data[rsp_idx++] = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL;
    g_seek_rsp_data[rsp_idx++] = 0x00;

    uint8_t name_len = (uint8_t)sizeof(SLE_LOCAL_NAME) - 1;
    g_seek_rsp_data[rsp_idx++] = name_len + 1;
    g_seek_rsp_data[rsp_idx++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    if (memcpy_s(&g_seek_rsp_data[rsp_idx], sizeof(g_seek_rsp_data) - rsp_idx,
                 SLE_LOCAL_NAME, name_len) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    rsp_idx += name_len;
    g_seek_rsp_data_len = rsp_idx;

    g_announce_data_inited = true;
    osal_printk("%s[BP] static announce data inited, len=%u\r\n",
                SLE_SLAVE_LOG, g_announce_data_len);

    /* hex dump: 确认 g_announce_data 初始化内容 */
    osal_printk("%s[BP] announce hex dump:\r\n", SLE_SLAVE_LOG);
    for (uint16_t i = 0; i < g_announce_data_len && i < 22; i++) {
        osal_printk("%02X ", g_announce_data[i]);
        if ((i + 1) % 11 == 0) osal_printk("\r\n");
    }
    osal_printk("\r\n");

    return ERRCODE_SLE_SUCCESS;
}

static void sle_slave_announce_enable_cbk(uint32_t announce_id, errcode_t status)
{
    if (announce_id != (uint32_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE) {
        return;
    }

    g_adv_started = (status == ERRCODE_SLE_SUCCESS);
    osal_printk("%s announce enable cb id:%u status:0x%x started:%u\r\n",
                SLE_SLAVE_LOG, announce_id, status, g_adv_started);
}

static void sle_slave_announce_disable_cbk(uint32_t announce_id, errcode_t status)
{
    if (announce_id != (uint32_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE) {
        return;
    }

    if (status == ERRCODE_SLE_SUCCESS) {
        g_adv_started = false;
    }
    osal_printk("%s announce disable cb id:%u status:0x%x started:%u\r\n",
                SLE_SLAVE_LOG, announce_id, status, g_adv_started);
}

static void sle_slave_announce_terminal_cbk(uint32_t announce_id)
{
    if (announce_id != (uint32_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE) {
        return;
    }

    g_adv_started = false;
    osal_printk("%s announce terminal cb id:%u\r\n", SLE_SLAVE_LOG, announce_id);
}

static void sle_slave_announce_remove_cbk(uint32_t announce_id, errcode_t status)
{
    osal_printk("%s announce remove cb id:%u status:0x%x\r\n", SLE_SLAVE_LOG, announce_id, status);
    if (announce_id == (uint32_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE) {
        g_adv_started = false;
        g_adv_configured = false;
    }
}

static void sle_slave_seek_enable_cbk(errcode_t status)
{
    osal_printk("%s seek enable cb status:0x%x\r\n", SLE_SLAVE_LOG, status);
}

static void sle_slave_seek_disable_cbk(errcode_t status)
{
    osal_printk("%s seek disable cb status:0x%x\r\n", SLE_SLAVE_LOG, status);
}

static void sle_slave_seek_result_cbk(sle_seek_result_info_t *seek_result_data)
{
    unused(seek_result_data);
}

static void sle_slave_dfr_cbk(void)
{
    osal_printk("%s sle dfr cb\r\n", SLE_SLAVE_LOG);
}

/* 连接回调存根（防止SDK调用NULL函数指针导致mepc=0x0崩溃） */
static void sle_stub_conn_param_update_req_cbk(uint16_t conn_id, errcode_t status,
    const sle_connection_param_update_req_t *param)
{ unused(conn_id); unused(status); unused(param); }

static void sle_stub_conn_param_update_cbk(uint16_t conn_id, errcode_t status,
    const sle_connection_param_update_evt_t *param)
{ unused(conn_id); unused(status); unused(param); }

static void sle_stub_auth_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
    const sle_auth_info_evt_t *info)
{ unused(conn_id); unused(addr); unused(status); unused(info); }

static void sle_stub_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{ unused(conn_id); unused(addr); unused(status); }

static void sle_stub_read_rssi_cbk(uint16_t conn_id, int8_t rssi, errcode_t status)
{ unused(conn_id); unused(rssi); unused(status); }

static void sle_stub_low_latency_cbk(uint8_t status, sle_addr_t *addr, uint8_t rate)
{ unused(status); unused(addr); unused(rate); }

static void sle_stub_set_phy_cbk(uint16_t conn_id, errcode_t status, const sle_set_phy_t *param)
{ unused(conn_id); unused(status); unused(param); }

static void sle_stub_remote_private_feature_cbk(uint16_t conn_id, errcode_t status,
    const sle_remote_private_feature_t *param)
{ unused(conn_id); unused(status); unused(param); }

static void sle_stub_passkey_req_cbk(uint16_t conn_id)
{ unused(conn_id); }

static void sle_stub_passkey_notify_cbk(uint16_t conn_id, const uint8_t *passkey, const uint8_t len)
{ unused(conn_id); unused(passkey); unused(len); }

/* SSAP回调存根 */
static void ssaps_stub_add_service_cbk(uint8_t server_id, sle_uuid_t *uuid, uint16_t handle, errcode_t status)
{ unused(server_id); unused(uuid); unused(handle); unused(status); }

static void ssaps_stub_add_property_cbk(uint8_t server_id, sle_uuid_t *uuid,
    uint16_t service_handle, uint16_t handle, errcode_t status)
{ unused(server_id); unused(uuid); unused(service_handle); unused(handle); unused(status); }

static void ssaps_stub_add_descriptor_cbk(uint8_t server_id, sle_uuid_t *uuid,
    uint16_t service_handle, uint16_t handle, errcode_t status)
{ unused(server_id); unused(uuid); unused(service_handle); unused(handle); unused(status); }

static void ssaps_stub_start_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{ unused(server_id); unused(handle); unused(status); }

static void ssaps_stub_delete_all_service_cbk(uint8_t server_id, errcode_t status)
{ unused(server_id); unused(status); }

static void ssaps_stub_read_by_uuid_request_cbk(uint8_t server_id, uint16_t conn_id,
    ssaps_req_read_by_uuid_cb_t *read_cb_para, errcode_t status)
{ unused(server_id); unused(conn_id); unused(read_cb_para); unused(status); }

static void ssaps_stub_indicate_cfm_cbk(uint8_t server_id, uint16_t conn_id,
    sle_indication_cfm_result_t cfm_result, errcode_t status)
{ unused(server_id); unused(conn_id); unused(cfm_result); unused(status); }

static void ssaps_stub_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id,
    ssap_exchange_info_t *info, errcode_t status)
{ unused(server_id); unused(conn_id); unused(info); unused(status); }

static errcode_t sle_slave_start_announce_if_needed(void)
{
    if (!g_sle_stack_ready) {
        osal_printk("%s skip start announce before sle ready\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_FAIL;
    }

    if (g_adv_started) {
        osal_printk("%s announce already started, skip duplicate start\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_SUCCESS;
    }

    errcode_t ret = sle_start_announce((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE);
    return ret;
}

static errcode_t sle_slave_stop_announce_if_needed(void)
{
    if (!g_sle_stack_ready) {
        osal_printk("%s skip stop announce before sle ready\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_SUCCESS;
    }

    if (!g_adv_started) {
        osal_printk("%s announce already stopped, skip duplicate stop\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_SUCCESS;
    }

    errcode_t ret = sle_stop_announce((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE);
    return ret;
}

/* 基于偏移量更新广播字段（零拷贝，只修改变化的字节） */
static void sle_slave_update_adv_field_by_offset(const shared_proto_adv_field_t *field)
{
    /* announce_data中厂商数据起始偏移=6（discovery+access），ADV_OFFSET已含header */
    uint16_t base = 6;

    adv_write_u16_be(&g_announce_data[base + ADV_OFFSET_TAG_ID], field->tag_id);
    adv_write_u16_be(&g_announce_data[base + ADV_OFFSET_QTY], field->qty);
    g_announce_data[base + ADV_OFFSET_STATUS] = field->status;
    g_announce_data[base + ADV_OFFSET_BATTERY] = field->battery;
    adv_write_u16_be(&g_announce_data[base + ADV_OFFSET_SEQ], field->seq);

    /* 同步更新g_adv_payload（兼容旧代码） */
    adv_write_u16_be(&g_adv_payload[ADV_OFFSET_TAG_ID], field->tag_id);
    adv_write_u16_be(&g_adv_payload[ADV_OFFSET_QTY], field->qty);
    g_adv_payload[ADV_OFFSET_STATUS] = field->status;
    g_adv_payload[ADV_OFFSET_BATTERY] = field->battery;
    adv_write_u16_be(&g_adv_payload[ADV_OFFSET_SEQ], field->seq);

    osal_printk("%s[BP] update_by_offset tag:%u qty:%u status:0x%02x\r\n",
                SLE_SLAVE_LOG, field->tag_id, field->qty, field->status);
}

static errcode_t sle_slave_update_announce_data(void)
{
    sle_announce_data_t data = {0};
    data.announce_data = g_announce_data;
    data.announce_data_len = g_announce_data_len;
    data.seek_rsp_data = g_seek_rsp_data;
    data.seek_rsp_data_len = g_seek_rsp_data_len;

    osal_printk("%s announce_data_len=%u seek_rsp_data_len=%u\r\n",
                SLE_SLAVE_LOG, g_announce_data_len, g_seek_rsp_data_len);

    errcode_t ret = sle_set_announce_data((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE, &data);

    /* hex dump: 确认传给协议栈的实际数据 */
    osal_printk("%s[BP] adv after set ret=0x%x, hex dump:\r\n", SLE_SLAVE_LOG, ret);
    for (uint16_t i = 0; i < g_announce_data_len && i < 22; i++) {
        osal_printk("%02X ", g_announce_data[i]);
        if ((i + 1) % 11 == 0) osal_printk("\r\n");
    }
    osal_printk("\r\n");

    return ret;
}

static void sle_slave_add_connection(uint16_t conn_id)
{
    for (uint8_t i = 0; i < CONFIG_MY_PROJECT_2X_MAX_CONNECTIONS; i++) {
        if (g_conn_ids[i] == conn_id) {
            osal_printk("%s conn_id 0x%x already in list\r\n", SLE_SLAVE_LOG, conn_id);
            return;
        }
        if (g_conn_ids[i] == 0) {
            g_conn_ids[i] = conn_id;
            g_active_conn_count++;
            osal_printk("%s added conn_id 0x%x at slot %u, active_count=%u\r\n",
                        SLE_SLAVE_LOG, conn_id, i, g_active_conn_count);
            return;
        }
    }
    osal_printk("%s connection list full, rejecting conn_id 0x%x\r\n", SLE_SLAVE_LOG, conn_id);
}

static void sle_slave_remove_connection(uint16_t conn_id)
{
    for (uint8_t i = 0; i < CONFIG_MY_PROJECT_2X_MAX_CONNECTIONS; i++) {
        if (g_conn_ids[i] == conn_id) {
            g_conn_ids[i] = 0;
            if (g_active_conn_count > 0) {
                g_active_conn_count--;
            }
            osal_printk("%s removed conn_id 0x%x from slot %u, active_count=%u\r\n",
                        SLE_SLAVE_LOG, conn_id, i, g_active_conn_count);
            return;
        }
    }
    osal_printk("%s conn_id 0x%x not found in list\r\n", SLE_SLAVE_LOG, conn_id);
}

static void sle_slave_connect_state_changed_cbk(uint16_t conn_id, const sle_addr_t *addr,
                                                sle_acb_state_t conn_state, sle_pair_state_t pair_state,
                                                sle_disc_reason_t disc_reason)
{
    if (addr != NULL) {
        osal_printk("%s[BP] conn_cb id:0x%x state:0x%x pair:0x%x disc:0x%x addr:%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                    SLE_SLAVE_LOG, conn_id, conn_state, pair_state, disc_reason,
                    addr->addr[0], addr->addr[1], addr->addr[2],
                    addr->addr[3], addr->addr[4], addr->addr[5]);
    } else {
        osal_printk("%s[BP] conn_cb id:0x%x state:0x%x pair:0x%x disc:0x%x addr=NULL\r\n",
                    SLE_SLAVE_LOG, conn_id, conn_state, pair_state, disc_reason);
    }

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        sle_slave_add_connection(conn_id);
        osal_printk("%s[BP] CONNECTED server_id:%u svc_hdl:0x%x prop_hdl:0x%x\r\n",
                    SLE_SLAVE_LOG, g_server_id, g_service_handle, g_property_handle);
        if (g_cb.on_conn_state_changed != NULL) {
            g_cb.on_conn_state_changed(conn_id, true);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        sle_slave_remove_connection(conn_id);
        osal_printk("%s[BP] DISCONNECTED disc_reason:0x%x\r\n", SLE_SLAVE_LOG, disc_reason);
        if (g_cb.on_conn_state_changed != NULL) {
            g_cb.on_conn_state_changed(conn_id, false);
        }
    }
}

static void ssaps_server_write_request_cbk(uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_cb_para,
                                           errcode_t status)
{
    unused(status);

    if (write_cb_para == NULL || write_cb_para->value == NULL || write_cb_para->length == 0) {
        osal_printk("%s[BP] write_cb invalid param\r\n", SLE_SLAVE_LOG);
        return;
    }

    osal_printk("%s[BP] SSAP Write srv:%u conn:0x%x hdl:0x%x len:%u data:",
                SLE_SLAVE_LOG, server_id, conn_id, write_cb_para->handle, write_cb_para->length);
    for (uint16_t i = 0; i < write_cb_para->length && i < 16; i++) {
        osal_printk(" %02X", write_cb_para->value[i]);
    }
    osal_printk("\r\n");

    shared_proto_unicast_cmd_t cmd = {0};
    if (!shared_proto_parse_unicast_cmd(write_cb_para->value, write_cb_para->length, &cmd)) {
        osal_printk("%s[BP] parse cmd FAIL len:%u\r\n", SLE_SLAVE_LOG, write_cb_para->length);
        return;
    }
    osal_printk("%s[BP] parse cmd OK action:%u qty:%u tag_id:%u\r\n",
                SLE_SLAVE_LOG, cmd.action, cmd.qty, cmd.tag_id);

    if (g_cb.on_unicast_cmd != NULL) {
        g_cb.on_unicast_cmd(conn_id, &cmd);
    }
}

static void ssaps_server_read_request_cbk(uint8_t server_id, uint16_t conn_id, ssaps_req_read_cb_t *read_cb_para,
                                          errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(read_cb_para);
    unused(status);
}

static errcode_t sle_slave_register_callbacks(void)
{
    osal_printk("%s Entering sle_slave_register_callbacks\r\n", SLE_SLAVE_LOG);

    /* 连接回调：补全所有11个字段 */
    sle_connection_callbacks_t conn_cbks = {0};
    conn_cbks.connect_state_changed_cb = sle_slave_connect_state_changed_cbk;
    conn_cbks.connect_param_update_req_cb = sle_stub_conn_param_update_req_cbk;
    conn_cbks.connect_param_update_cb = sle_stub_conn_param_update_cbk;
    conn_cbks.auth_complete_cb = sle_stub_auth_complete_cbk;
    conn_cbks.pair_complete_cb = sle_stub_pair_complete_cbk;
    conn_cbks.read_rssi_cb = sle_stub_read_rssi_cbk;
    conn_cbks.low_latency_cb = sle_stub_low_latency_cbk;
    conn_cbks.set_phy_cb = sle_stub_set_phy_cbk;
    conn_cbks.remote_private_feature = sle_stub_remote_private_feature_cbk;
    conn_cbks.passkey_req_cb = sle_stub_passkey_req_cbk;
    conn_cbks.passkey_notify_cb = sle_stub_passkey_notify_cbk;
    errcode_t ret = sle_connection_register_callbacks(&conn_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_connection_register_callbacks fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    /* SSAP回调：补全所有10个字段 */
    ssaps_callbacks_t ssaps_cbk = {0};
    ssaps_cbk.add_service_cb = ssaps_stub_add_service_cbk;
    ssaps_cbk.add_property_cb = ssaps_stub_add_property_cbk;
    ssaps_cbk.add_descriptor_cb = ssaps_stub_add_descriptor_cbk;
    ssaps_cbk.start_service_cb = ssaps_stub_start_service_cbk;
    ssaps_cbk.delete_all_service_cb = ssaps_stub_delete_all_service_cbk;
    ssaps_cbk.read_request_cb = ssaps_server_read_request_cbk;
    ssaps_cbk.read_by_uuid_request_cb = ssaps_stub_read_by_uuid_request_cbk;
    ssaps_cbk.write_request_cb = ssaps_server_write_request_cbk;
    ssaps_cbk.indicate_cfm_cb = ssaps_stub_indicate_cfm_cbk;
    ssaps_cbk.mtu_changed_cb = ssaps_stub_mtu_changed_cbk;
    ret = ssaps_register_callbacks(&ssaps_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_register_callbacks fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_slave_ensure_unique_mac(sle_addr_t *addr)
{
    uint16_t read_len = 0;
    errcode_t ret = uapi_nv_read(NV_ID_BS2X_CUSTOM_MAC, SLE_ADDR_LEN, &read_len, addr->addr);

    if (ret == ERRCODE_SUCC && read_len == SLE_ADDR_LEN) {
        bool all_zero = true;
        for (uint8_t i = 0; i < SLE_ADDR_LEN; i++) {
            if (addr->addr[i] != 0) {
                all_zero = false;
                break;
            }
        }
        if (!all_zero) {
            addr->type = 0;
            osal_printk("%s using stored MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                        SLE_SLAVE_LOG, addr->addr[0], addr->addr[1], addr->addr[2],
                        addr->addr[3], addr->addr[4], addr->addr[5]);
            return ERRCODE_SUCC;
        }
    }

    /* 种子 = 时间戳，生成随机MAC（V153不支持efuse_get_chip_id） */
    osal_printk("%s [DBG-MAC-1] generating new mac\r\n", SLE_SLAVE_LOG);
    uint32_t seed = (uint32_t)(uapi_tcxo_get_ms() & 0xFFFFFFFF);
    osal_printk("%s [DBG-MAC-2] tcxo seed=0x%x\r\n", SLE_SLAVE_LOG, seed);
    if (seed == 0) {
        seed = 0xDEADBEEF;
    }
    for (uint8_t i = 0; i < SLE_ADDR_LEN; i++) {
        seed = seed * 1103515245 + 12345;
        addr->addr[i] = (uint8_t)(seed >> 16);
    }
    addr->addr[0] |= 0x02;
    addr->addr[0] &= ~0x01;
    addr->type = 0;

    ret = uapi_nv_write(NV_ID_BS2X_CUSTOM_MAC, addr->addr, SLE_ADDR_LEN);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s nv write custom mac fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    osal_printk("%s generated new MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                SLE_SLAVE_LOG, addr->addr[0], addr->addr[1], addr->addr[2],
                addr->addr[3], addr->addr[4], addr->addr[5]);
    return ERRCODE_SUCC;
}

static errcode_t sle_slave_setup_announce(void)
{
    osal_printk("%s [DBG-1] Entering setup_announce\r\n", SLE_SLAVE_LOG);

    sle_announce_param_t param = {0};
    param.announce_handle = (uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE;
    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = 0x07;
    /* 广播间隔500ms（0x0FA0 × 0.125ms = 500ms），SLE单位是125us不是BLE的625us */
    param.announce_interval_min = 0x0FA0;
    param.announce_interval_max = 0x0FA0;
    param.conn_interval_min = 0x64;
    param.conn_interval_max = 0x64;
    param.conn_max_latency = 0x0F;
    param.conn_supervision_timeout = 0x1F4;
    param.announce_tx_power = 0;
    param.own_addr.type = 0;

    osal_printk("%s [DBG-2] param filled, addr=%p\r\n", SLE_SLAVE_LOG, (void *)&param);

    sle_addr_t local_addr = {0};
    osal_printk("%s [DBG-3] before ensure_unique_mac\r\n", SLE_SLAVE_LOG);
    errcode_t mac_ret = sle_slave_ensure_unique_mac(&local_addr);
    osal_printk("%s [DBG-4] ensure_unique_mac ret=0x%x\r\n", SLE_SLAVE_LOG, mac_ret);
    if (mac_ret == ERRCODE_SUCC) {
        param.own_addr.type = local_addr.type;
        if (memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr.addr, SLE_ADDR_LEN) != EOK) {
            osal_printk("%s memcpy local addr fail\r\n", SLE_SLAVE_LOG);
        }
    } else {
        if (sle_get_local_addr(&local_addr) == ERRCODE_SLE_SUCCESS) {
            param.own_addr.type = local_addr.type;
            if (memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr.addr, SLE_ADDR_LEN) != EOK) {
                osal_printk("%s memcpy local addr fail\r\n", SLE_SLAVE_LOG);
            }
        } else {
            osal_printk("%s all addr methods failed, use default\r\n", SLE_SLAVE_LOG);
        }
    }

    osal_printk("%s [DBG-3] before sle_set_announce_param handle=%u\r\n", SLE_SLAVE_LOG, param.announce_handle);
    errcode_t ret = sle_set_announce_param(param.announce_handle, &param);
    osal_printk("%s [DBG-4] after sle_set_announce_param ret=0x%x\r\n", SLE_SLAVE_LOG, ret);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_set_announce_param fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    osal_printk("%s[BP] adv params handle:%u ch_map:0x%02x interval:%u(~%ums) mode:%u\r\n",
                SLE_SLAVE_LOG, param.announce_handle, param.announce_channel_map,
                param.announce_interval_min,
                param.announce_interval_min * 125 / 1000,
                param.announce_mode);

    ret = sle_slave_update_announce_data();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_set_announce_data fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    g_adv_configured = true;
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_slave_setup_ssap_server(void)
{
    osal_printk("%s Entering sle_slave_setup_ssap_server\r\n", SLE_SLAVE_LOG);

    sle_uuid_t app_uuid = { .len = BS21E_APP_UUID_LEN, .uuid = {0} };
    (void)memcpy_s(app_uuid.uuid, BS21E_APP_UUID_LEN, g_app_uuid, BS21E_APP_UUID_LEN);
    errcode_t ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_register_server fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    sle_uuid_t service_uuid = { .len = BS21E_SERVICE_UUID_LEN, .uuid = {0} };
    (void)memcpy_s(service_uuid.uuid, BS21E_SERVICE_UUID_LEN, g_service_uuid, BS21E_SERVICE_UUID_LEN);
    ret = ssaps_add_service_sync(g_server_id, &service_uuid, true, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_add_service_sync fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    ssaps_property_info_t property = {0};
    sle_uuid_t prop_uuid = { .len = BS21E_PROP_UUID_LEN, .uuid = {0} };
    (void)memcpy_s(prop_uuid.uuid, BS21E_PROP_UUID_LEN, g_property_uuid, BS21E_PROP_UUID_LEN);
    property.uuid = prop_uuid;
    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE |
                                  SSAP_OPERATE_INDICATION_BIT_NOTIFY;
    property.value_len = 0;
    property.value = NULL;

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_add_property_sync fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    ssaps_desc_info_t cccd_desc = {0};
    sle_uuid_t cccd_uuid = { .len = 2, .uuid = {0} };
    cccd_uuid.uuid[0] = 0x02;
    cccd_uuid.uuid[1] = 0x29;
    cccd_desc.uuid = cccd_uuid;
    cccd_desc.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    cccd_desc.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ
                                 | SSAP_OPERATE_INDICATION_BIT_WRITE
                                 | SSAP_OPERATE_INDICATION_BIT_DESCRIPTOR_CLIENT_CONFIGURATION_WRITE;
    cccd_desc.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    ret = ssaps_add_descriptor_sync(g_server_id, g_service_handle, g_property_handle, &cccd_desc);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_add_descriptor_sync(CCCD) fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_start_service fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    return ERRCODE_SLE_SUCCESS;
}

static void sle_slave_power_on_cbk(uint8_t status)
{
    osal_printk("[BS2x_INIT] sle power on: %u\r\n", status);
    enable_sle();
}

static void sle_slave_enable_cbk(uint8_t status)
{
    osal_printk("[BS2x_INIT] sle enable cbk status:%u\r\n", status);
    /* 初始化全部在 sle_slave_init 中完成，此处仅做状态通知 */
}

errcode_t sle_slave_init(const sle_slave_callbacks_t *cb)
{
    osal_printk("[BS2x_INIT] Entering sle_slave_init\r\n");

    if (cb != NULL) {
        g_cb = *cb;
    } else {
        (void)memset_s(&g_cb, sizeof(g_cb), 0, sizeof(g_cb));
    }

    /* 初始化静态广播数据缓存 */
    errcode_t init_ret = sle_slave_init_static_announce_data();
    if (init_ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s init static announce data fail:0x%x\r\n", SLE_SLAVE_LOG, init_ret);
    }

    /* Step1: 注册设备管理回调 */
    sle_dev_manager_callbacks_t dev_cb = {0};
    dev_cb.sle_power_on_cb = sle_slave_power_on_cbk;
    dev_cb.sle_enable_cb = sle_slave_enable_cbk;
    errcode_t ret = sle_dev_manager_register_callbacks(&dev_cb);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_dev_manager_register_callbacks fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }
    osal_printk("[BS2x_INIT] dev_manager registered\r\n");

    /* Step2: 使能SLE协议栈 */
#if (CORE_NUMS < 2)
    if (enable_sle() != ERRCODE_SUCC) {
        osal_printk("[BS2x_INIT] enable_sle fail\r\n");
        return ERRCODE_SLE_FAIL;
    }
    osal_printk("[BS2x_INIT] enable_sle ok\r\n");
#endif

    /* Step3: 注册广播/扫描回调（官方示例：先于连接/SSAP注册） */
    sle_announce_seek_callbacks_t announce_cb = {0};
    announce_cb.announce_enable_cb = sle_slave_announce_enable_cbk;
    announce_cb.announce_disable_cb = sle_slave_announce_disable_cbk;
    announce_cb.announce_terminal_cb = sle_slave_announce_terminal_cbk;
    announce_cb.announce_remove_cb = sle_slave_announce_remove_cbk;
    announce_cb.seek_enable_cb = sle_slave_seek_enable_cbk;
    announce_cb.seek_disable_cb = sle_slave_seek_disable_cbk;
    announce_cb.seek_result_cb = sle_slave_seek_result_cbk;
    announce_cb.sle_dfr_cb = sle_slave_dfr_cbk;
    ret = sle_announce_seek_register_callbacks(&announce_cb);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_announce_seek_register_callbacks fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }
    osal_printk("[BS2x_INIT] announce_seek registered\r\n");

    /* Step4: 注册连接管理回调 */
    ret = sle_slave_register_callbacks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[BS2x_INIT] register_callbacks fail:0x%x\r\n", ret);
        return ret;
    }
    osal_printk("[BS2x_INIT] connection+ssap registered\r\n");

    /* Step5: 设置SSAP服务（官方示例：先于广播参数设置） */
    ret = sle_slave_setup_ssap_server();
    osal_printk("[BS2x_INIT] setup_ssap ret:0x%x server_id:%u svc_hdl:0x%x prop_hdl:0x%x\r\n",
                ret, g_server_id, g_service_handle, g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    /* Step6: 设置广播参数+数据（官方示例：最后设置并启动） */
    ret = sle_slave_setup_announce();
    osal_printk("[BS2x_INIT] setup_announce ret:0x%x configured:%u\r\n", ret, g_adv_configured);
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    /* Step7: 启动广播 */
    g_sle_stack_ready = true;
    ret = sle_start_announce((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE);
    osal_printk("[BS2x_INIT] start_announce ret:0x%x\r\n", ret);
    if (ret == ERRCODE_SLE_SUCCESS) {
        g_adv_started = true;
    }

    osal_printk("[BS2x_INIT] sle_slave_init complete\r\n");
    return ERRCODE_SLE_SUCCESS;
}

errcode_t sle_slave_start(void)
{
    osal_printk("%s start announce\r\n", SLE_SLAVE_LOG);
    return sle_slave_start_announce_if_needed();
}

errcode_t sle_slave_stop(void)
{
    osal_printk("%s stop announce\r\n", SLE_SLAVE_LOG);
    return sle_slave_stop_announce_if_needed();
}

errcode_t sle_slave_refresh_adv_payload(const shared_proto_adv_field_t *field)
{
    if (field == NULL) {
        osal_printk("%s[BP] refresh_adv FAIL field=NULL\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_PARAM_ERR;
    }

    osal_printk("%s[BP] refresh_adv tag:%u qty:%u status:0x%02x seq:%u\r\n",
                SLE_SLAVE_LOG, field->tag_id, field->qty, field->status, field->seq);

    /* 确保静态buffer已初始化 */
    errcode_t ret = sle_slave_init_static_announce_data();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s init static announce data fail ret:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    /* 基于偏移量更新字段（零拷贝，只修改变化的字节） */
    sle_slave_update_adv_field_by_offset(field);

    if (!g_sle_stack_ready) {
        osal_printk("%s sle not ready, cached adv payload only\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_SUCCESS;
    }

    bool restart_after_update = g_adv_started;
    if (restart_after_update) {
        ret = sle_slave_stop_announce_if_needed();
        if (ret != ERRCODE_SLE_SUCCESS) {
            osal_printk("%s stop announce before refresh failed ret:0x%x\r\n", SLE_SLAVE_LOG, ret);
            return ret;
        }
    }

    ret = g_adv_configured ? sle_slave_update_announce_data() : sle_slave_setup_announce();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s update announce data failed ret:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    if (restart_after_update) {
        ret = sle_slave_start_announce_if_needed();
        if (ret != ERRCODE_SLE_SUCCESS) {
            osal_printk("%s restart announce after refresh failed ret:0x%x\r\n", SLE_SLAVE_LOG, ret);
            return ret;
        }
    }

    osal_printk("%s refresh adv payload ok started:%u\r\n", SLE_SLAVE_LOG, g_adv_started);
    return ERRCODE_SLE_SUCCESS;
}

uint16_t sle_slave_get_conn_id(void)
{
    return g_active_conn_count > 0 ? g_conn_ids[0] : 0;
}

bool sle_slave_is_connected(void)
{
    return g_active_conn_count > 0;
}

uint8_t sle_slave_get_active_conn_count(void)
{
    return g_active_conn_count;
}

errcode_t sle_slave_broadcast_notify_all(const uint8_t *data, uint16_t len)
{
    if (g_active_conn_count == 0 || data == NULL || len == 0) {
        return ERRCODE_SLE_PARAM_ERR;
    }

    ssaps_ntf_ind_t param = {0};
    param.handle = g_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = (uint8_t *)data;
    param.value_len = len;

    errcode_t last_ret = ERRCODE_SLE_SUCCESS;
    for (uint8_t i = 0; i < CONFIG_MY_PROJECT_2X_MAX_CONNECTIONS; i++) {
        if (g_conn_ids[i] != 0) {
            errcode_t ret = ssaps_notify_indicate(g_server_id, g_conn_ids[i], &param);
            if (ret != ERRCODE_SLE_SUCCESS) {
                osal_printk("%s notify failed conn_id:0x%x ret:0x%x\r\n",
                            SLE_SLAVE_LOG, g_conn_ids[i], ret);
                last_ret = ret;
            } else {
                osal_printk("%s notify ok conn_id:0x%x\r\n", SLE_SLAVE_LOG, g_conn_ids[i]);
            }
        }
    }
    return last_ret;
}

errcode_t sle_slave_notify_conn(uint16_t conn_id, const uint8_t *data, uint16_t len)
{
    if (conn_id == 0 || data == NULL || len == 0) {
        return ERRCODE_SLE_PARAM_ERR;
    }

    ssaps_ntf_ind_t param = {0};
    param.handle = g_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = (uint8_t *)data;
    param.value_len = len;

    errcode_t ret = ssaps_notify_indicate(g_server_id, conn_id, &param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s[BP] notify FAIL conn_id:0x%x ret:0x%x len:%u\r\n", SLE_SLAVE_LOG, conn_id, ret, len);
    } else {
        osal_printk("%s[BP] notify OK conn_id:0x%x len:%u\r\n", SLE_SLAVE_LOG, conn_id, len);
    }
    return ret;
}

errcode_t sle_slave_reset_mac(void)
{
    osal_printk("%s[BP] RESET_MAC start\r\n", SLE_SLAVE_LOG);

    /* 1. 停止广播 */
    errcode_t ret = sle_slave_stop_announce_if_needed();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s stop announce fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
    }

    /* 2. 清除NV中的MAC，强制重新生成 */
    uint8_t zero[SLE_ADDR_LEN] = {0};
    ret = uapi_nv_write(NV_ID_BS2X_CUSTOM_MAC, zero, SLE_ADDR_LEN);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s nv clear mac fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    /* 3. 生成新MAC并写入NV */
    sle_addr_t new_addr = {0};
    ret = sle_slave_ensure_unique_mac(&new_addr);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s generate new mac fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    /* 4. 重新设置广播参数（含新MAC） */
    ret = sle_slave_setup_announce();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s setup_announce fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    /* 5. 重启广播 */
    g_sle_stack_ready = true;
    ret = sle_start_announce((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE);
    if (ret == ERRCODE_SLE_SUCCESS) {
        g_adv_started = true;
    }

    osal_printk("%s[BP] RESET_MAC done new MAC: %02X:%02X:%02X:%02X:%02X:%02X ret:0x%x\r\n",
                SLE_SLAVE_LOG, new_addr.addr[0], new_addr.addr[1], new_addr.addr[2],
                new_addr.addr[3], new_addr.addr[4], new_addr.addr[5], ret);
    return ret;
}
