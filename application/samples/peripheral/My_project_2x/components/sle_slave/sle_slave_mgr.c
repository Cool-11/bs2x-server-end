#include "sle_slave_mgr.h"

#include "common_def.h"
#include "securec.h"
#include "soc_osal.h"

#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_server.h"

#ifndef CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE
#define CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE 1
#endif

#define SLE_SLAVE_LOG "[BS2x_SLE]"
#define SLE_ADV_DATA_LEN_MAX_LOCAL 251u
#define SLE_ADV_AD_TYPE_MANUFACTURER_SPECIFIC_DATA 0xFFu
#define SLE_ADV_MANUFACTURER_ID_L 0x5Au
#define SLE_ADV_MANUFACTURER_ID_H 0xA5u
#define SLE_ADV_MANUFACTURER_HEADER_LEN 4u
#define SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET 4u

static sle_slave_callbacks_t g_cb = {0};
static uint16_t g_conn_ids[CONFIG_MY_PROJECT_2X_MAX_CONNECTIONS] = {0};
static uint8_t g_active_conn_count = 0;
static uint8_t g_server_id = 0;
static uint16_t g_service_handle = 0;
static uint16_t g_property_handle = 0;

static uint8_t g_adv_payload[SLE_ADV_DATA_LEN_MAX_LOCAL] = {0};
static uint16_t g_adv_payload_len = 0;

static errcode_t sle_slave_encode_manufacturer_adv(const shared_proto_adv_field_t *field)
{
    osal_printk("%s Entering sle_slave_encode_manufacturer_adv\r\n", SLE_SLAVE_LOG);

    if (field == NULL || !shared_proto_adv_field_is_valid(field)) {
        osal_printk("%s adv field invalid\r\n", SLE_SLAVE_LOG);
        return ERRCODE_SLE_PARAM_ERR;
    }

    uint16_t total_len = SLE_ADV_MANUFACTURER_HEADER_LEN + (uint16_t)sizeof(*field);
    if (total_len > sizeof(g_adv_payload)) {
        return ERRCODE_SLE_PARAM_ERR;
    }

    g_adv_payload[0] = (uint8_t)(total_len - 1u);
    g_adv_payload[1] = SLE_ADV_AD_TYPE_MANUFACTURER_SPECIFIC_DATA;
    g_adv_payload[2] = SLE_ADV_MANUFACTURER_ID_L;
    g_adv_payload[3] = SLE_ADV_MANUFACTURER_ID_H;

    if (memcpy_s(&g_adv_payload[SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET],
                 sizeof(g_adv_payload) - SLE_ADV_MANUFACTURER_PAYLOAD_OFFSET,
                 field, sizeof(*field)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }

    g_adv_payload_len = total_len;
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
    unused(addr);
    unused(pair_state);
    osal_printk("%s conn_id:0x%x state:0x%x disc:0x%x\r\n", SLE_SLAVE_LOG, conn_id, conn_state, disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        sle_slave_add_connection(conn_id);
        if (g_cb.on_conn_state_changed != NULL) {
            g_cb.on_conn_state_changed(conn_id, true);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        sle_slave_remove_connection(conn_id);
        if (g_cb.on_conn_state_changed != NULL) {
            g_cb.on_conn_state_changed(conn_id, false);
        }
    }
}

static void ssaps_server_write_request_cbk(uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_cb_para,
                                           errcode_t status)
{
    unused(server_id);
    unused(status);

    if (write_cb_para == NULL || write_cb_para->value == NULL || write_cb_para->length == 0) {
        osal_printk("%s write cb invalid param\r\n", SLE_SLAVE_LOG);
        return;
    }

    osal_printk("%s Received SSAP Write conn_id:0x%x len:%u first:0x%02x\r\n",
                SLE_SLAVE_LOG, conn_id, write_cb_para->length, write_cb_para->value[0]);

    shared_proto_unicast_cmd_t cmd = {0};
    if (!shared_proto_parse_unicast_cmd(write_cb_para->value, write_cb_para->length, &cmd)) {
        osal_printk("%s invalid ssap write cmd len:%u\r\n", SLE_SLAVE_LOG, write_cb_para->length);
        return;
    }

    if (g_cb.on_unicast_cmd != NULL) {
        g_cb.on_unicast_cmd(&cmd);
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
    param.conn_max_latency = 0x1F3;
    param.conn_supervision_timeout = 0x1F4;
    param.own_addr.type = 0;

    errcode_t ret = sle_set_announce_param(param.announce_handle, &param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_set_announce_param fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    sle_announce_data_t data = {0};
    data.announce_data = g_adv_payload;
    data.announce_data_len = g_adv_payload_len;
    data.seek_rsp_data = NULL;
    data.seek_rsp_data_len = 0;

    ret = sle_set_announce_data(param.announce_handle, &data);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_set_announce_data fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_slave_setup_ssap_server(void)
{
    osal_printk("%s Entering sle_slave_setup_ssap_server\r\n", SLE_SLAVE_LOG);

    sle_uuid_t app_uuid = { .len = 2, .uuid = {0x12, 0x34} };
    errcode_t ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_register_server fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    sle_uuid_t service_uuid = { .len = 2, .uuid = {0x22, 0x22} };
    ret = ssaps_add_service_sync(g_server_id, &service_uuid, true, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_add_service_sync fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    ssaps_property_info_t property = {0};
    sle_uuid_t prop_uuid = { .len = 2, .uuid = {0x23, 0x23} };
    property.uuid = prop_uuid;
    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    property.value_len = 0;
    property.value = NULL;

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_add_property_sync fail:0x%x\r\n", SLE_SLAVE_LOG, ret);
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
    osal_printk("[BS2x_INIT] sle enable: %u\r\n", status);

    (void)sle_slave_register_callbacks();
    (void)sle_slave_setup_ssap_server();
    (void)sle_slave_setup_announce();
    (void)sle_start_announce((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE);
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

#if (CORE_NUMS < 2)
    enable_sle();
#endif

    return ERRCODE_SLE_SUCCESS;
}

errcode_t sle_slave_start(void)
{
    osal_printk("%s start announce\r\n", SLE_SLAVE_LOG);
    return sle_start_announce((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE);
}

errcode_t sle_slave_stop(void)
{
    osal_printk("%s stop announce\r\n", SLE_SLAVE_LOG);
    return sle_stop_announce((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE);
}

errcode_t sle_slave_refresh_adv_payload(const shared_proto_adv_field_t *field)
{
    osal_printk("%s refresh adv payload\r\n", SLE_SLAVE_LOG);

    errcode_t ret = sle_slave_encode_manufacturer_adv(field);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s encode adv failed ret:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    ret = sle_slave_setup_announce();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s setup announce failed ret:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    ret = sle_start_announce((uint8_t)CONFIG_MY_PROJECT_2X_SLE_ADV_HANDLE);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start announce failed ret:0x%x\r\n", SLE_SLAVE_LOG, ret);
        return ret;
    }

    osal_printk("%s refresh adv payload ok\r\n", SLE_SLAVE_LOG);
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
