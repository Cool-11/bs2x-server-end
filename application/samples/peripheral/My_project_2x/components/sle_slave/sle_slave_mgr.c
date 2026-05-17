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
static bool g_sle_stack_ready = false;
static bool g_adv_started = false;
static bool g_adv_configured = false;

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

static errcode_t sle_slave_update_announce_data(void)
{
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX_LOCAL] = {0};
    uint16_t idx = 0;

    announce_data[idx++] = 0x02;
    announce_data[idx++] = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL;
    announce_data[idx++] = SLE_ANNOUNCE_LEVEL_NORMAL;

    announce_data[idx++] = 0x02;
    announce_data[idx++] = SLE_ADV_DATA_TYPE_ACCESS_MODE;
    announce_data[idx++] = 0x00;

    if (g_adv_payload_len > 0 && idx + g_adv_payload_len <= SLE_ADV_DATA_LEN_MAX_LOCAL) {
        if (memcpy_s(&announce_data[idx], SLE_ADV_DATA_LEN_MAX_LOCAL - idx,
                     g_adv_payload, g_adv_payload_len) != EOK) {
            osal_printk("%s memcpy adv payload fail\r\n", SLE_SLAVE_LOG);
            return ERRCODE_SLE_FAIL;
        }
        idx += g_adv_payload_len;
    }

    uint8_t seek_rsp_data[SLE_ADV_DATA_LEN_MAX_LOCAL] = {0};
    uint16_t rsp_idx = 0;

    seek_rsp_data[rsp_idx++] = 0x02;
    seek_rsp_data[rsp_idx++] = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL;
    seek_rsp_data[rsp_idx++] = 0x00;

    uint8_t name_len = (uint8_t)sizeof(SLE_LOCAL_NAME) - 1;
    seek_rsp_data[rsp_idx++] = name_len + 1;
    seek_rsp_data[rsp_idx++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    if (memcpy_s(&seek_rsp_data[rsp_idx], SLE_ADV_DATA_LEN_MAX_LOCAL - rsp_idx,
                 SLE_LOCAL_NAME, name_len) != EOK) {
        osal_printk("%s memcpy local name fail\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_FAIL;
    }
    rsp_idx += name_len;

    sle_announce_data_t data = {0};
    data.announce_data = announce_data;
    data.announce_data_len = idx;
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = rsp_idx;

    osal_printk("%s announce_data_len=%u seek_rsp_data_len=%u\r\n",
                SLE_SLAVE_LOG, idx, rsp_idx);

    return sle_set_announce_data((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE, &data);
}

static errcode_t sle_slave_encode_manufacturer_adv(const shared_proto_adv_field_t *field)
{
    osal_printk("%s Entering sle_slave_encode_manufacturer_adv\r\n", SLE_SLAVE_LOG);

    if (field == NULL || !shared_proto_adv_field_is_valid(field)) {
        osal_printk("%s adv field invalid\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_PARAM_ERR;
    }

    uint16_t total_len = SLE_ADV_MANUFACTURER_HEADER_LEN + SHARED_PROTO_ADV_SERIALIZED_LEN;
    if (total_len > sizeof(g_adv_payload)) {
        return ERRCODE_SLE_PARAM_ERR;
    }

    g_adv_payload[0] = (uint8_t)(total_len - 1u);
    g_adv_payload[1] = SLE_ADV_AD_TYPE_MANUFACTURER_SPECIFIC_DATA;
    g_adv_payload[2] = SLE_ADV_MANUFACTURER_ID_L;
    g_adv_payload[3] = SLE_ADV_MANUFACTURER_ID_H;

    uint16_t serialized = shared_proto_serialize_adv_field(
        field, &g_adv_payload[SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET],
        sizeof(g_adv_payload) - SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET);
    if (serialized != SHARED_PROTO_ADV_SERIALIZED_LEN) {
        osal_printk("%s serialize adv FAIL got:%u expect:%u\r\n",
                    SLE_SLAVE_LOG, serialized, SHARED_PROTO_ADV_SERIALIZED_LEN);
        return ERRCODE_SLE_FAIL;
    }

    g_adv_payload_len = total_len;

    osal_printk("%s adv payload first 4 bytes: 0x%02X 0x%02X 0x%02X 0x%02X (expect 0xAA 0xBB 0xCC 0xDD)\r\n",
                SLE_SLAVE_LOG,
                g_adv_payload[SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET],
                g_adv_payload[SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET + 1],
                g_adv_payload[SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET + 2],
                g_adv_payload[SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET + 3]);

    return ERRCODE_SLE_SUCCESS;
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

    sle_connection_callbacks_t conn_cbks = {0};
    conn_cbks.connect_state_changed_cb = sle_slave_connect_state_changed_cbk;
    errcode_t ret = sle_connection_register_callbacks(&conn_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_connection_register_callbacks fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    ssaps_callbacks_t ssaps_cbk = {0};
    ssaps_cbk.write_request_cb = ssaps_server_write_request_cbk;
    ssaps_cbk.read_request_cb = ssaps_server_read_request_cbk;
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

    /* 种子 = 时间戳 XOR 芯片唯一ID，增加随机性 */
    uint32_t seed = (uint32_t)(uapi_tcxo_get_ms() & 0xFFFFFFFF);
    if (seed == 0) {
        seed = 0xDEADBEEF;
    }
    /* 混入 chip_id（如果可用） */
    uint8_t chip_id[8] = {0};
    if (uapi_efuse_get_chip_id(chip_id, sizeof(chip_id)) == ERRCODE_SUCC) {
        for (uint8_t i = 0; i < sizeof(chip_id); i++) {
            seed ^= (uint32_t)chip_id[i] << ((i % 4) * 8);
        }
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
    osal_printk("%s Entering sle_slave_setup_announce\r\n", SLE_SLAVE_LOG);

    sle_announce_param_t param = {0};
    param.announce_handle = (uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE;
    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = 0x07;
    param.announce_interval_min = 0xC8;
    param.announce_interval_max = 0xC8;
    param.conn_interval_min = 0x64;
    param.conn_interval_max = 0x64;
    param.conn_max_latency = 0x0F;
    param.conn_supervision_timeout = 0x1F4;
    param.announce_tx_power = 0;
    param.own_addr.type = 0;

    sle_addr_t local_addr = {0};
    if (sle_slave_ensure_unique_mac(&local_addr) == ERRCODE_SUCC) {
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

    errcode_t ret = sle_set_announce_param(param.announce_handle, &param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_set_announce_param fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

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
    osal_printk("[BS2x_INIT][BP] sle enable cbk status:%u\r\n", status);

    errcode_t ret;
    ret = sle_slave_register_callbacks();
    osal_printk("[BS2x_INIT][BP] register_callbacks ret:0x%x\r\n", ret);

    ret = sle_slave_setup_ssap_server();
    osal_printk("[BS2x_INIT][BP] setup_ssap ret:0x%x server_id:%u svc_hdl:0x%x prop_hdl:0x%x\r\n",
                ret, g_server_id, g_service_handle, g_property_handle);

    ret = sle_slave_setup_announce();
    osal_printk("[BS2x_INIT][BP] setup_announce ret:0x%x configured:%u\r\n", ret, g_adv_configured);

    g_sle_stack_ready = true;
    ret = sle_slave_start_announce_if_needed();
    osal_printk("[BS2x_INIT][BP] start_announce ret:0x%x started:%u\r\n", ret, g_adv_started);
}

errcode_t sle_slave_init(const sle_slave_callbacks_t *cb)
{
    osal_printk("[BS2x_INIT] Entering sle_slave_init\r\n");

    if (cb != NULL) {
        g_cb = *cb;
    } else {
        (void)memset_s(&g_cb, sizeof(g_cb), 0, sizeof(g_cb));
    }

    shared_proto_adv_field_t default_field = {
        .magic = SHARED_PROTO_MAGIC,
        .tag_id = 0,
        .qty = 0,
        .status = 0,
        .battery = 100,
        .seq = 0,
    };
    (void)sle_slave_encode_manufacturer_adv(&default_field);

    sle_dev_manager_callbacks_t dev_cb = {0};
    dev_cb.sle_power_on_cb = sle_slave_power_on_cbk;
    dev_cb.sle_enable_cb = sle_slave_enable_cbk;

    errcode_t ret = sle_dev_manager_register_callbacks(&dev_cb);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_dev_manager_register_callbacks fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    sle_announce_seek_callbacks_t announce_cb = {0};
    announce_cb.announce_enable_cb = sle_slave_announce_enable_cbk;
    announce_cb.announce_disable_cb = sle_slave_announce_disable_cbk;
    announce_cb.announce_terminal_cb = sle_slave_announce_terminal_cbk;
    ret = sle_announce_seek_register_callbacks(&announce_cb);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_announce_seek_register_callbacks fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

#if (CORE_NUMS < 2)
    enable_sle();
#endif

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

    errcode_t ret = sle_slave_encode_manufacturer_adv(field);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s encode adv failed ret:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

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
