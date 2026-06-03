/**
 * @file test_shared_protocol.c
 * @brief shared_protocol 层单元测试（Unity 框架）
 *
 * 测试目标：覆盖 shared_protocol 的序列化/反序列化/命令解析
 * 覆盖率目标：90%
 */

#include "unity.h"
#include "shared_protocol.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ========== 命令解析测试 ========== */

/** 正常解析：寻物命令 0x01 */
void test_parse_cmd_find_me(void)
{
    uint8_t data[] = {0x01};
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, sizeof(data), &cmd);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(SHARED_PROTO_ACTION_FIND_ME, cmd.action);
}

/** 正常解析：停止寻物命令 0x00 */
void test_parse_cmd_stop_find(void)
{
    uint8_t data[] = {0x00};
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, sizeof(data), &cmd);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(SHARED_PROTO_ACTION_STOP_FIND, cmd.action);
}

/** 正常解析：盘点命令 0x02 */
void test_parse_cmd_inventory(void)
{
    uint8_t data[] = {0x02};
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, sizeof(data), &cmd);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(SHARED_PROTO_ACTION_INVENTORY, cmd.action);
}

/** 正常解析：更新数量命令 0x10 + qty */
void test_parse_cmd_update_qty(void)
{
    uint8_t data[] = {0x10, 0x00, 0x0A}; /* qty = 10 */
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, sizeof(data), &cmd);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(SHARED_PROTO_ACTION_UPDATE_QTY, cmd.action);
    TEST_ASSERT_EQUAL(10, cmd.qty);
}

/** 正常解析：绑定命令 0x20 + tag_id */
void test_parse_cmd_bind_tag(void)
{
    uint8_t data[] = {0x20, 0x00, 0x05}; /* tag_id = 5 */
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, sizeof(data), &cmd);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(SHARED_PROTO_ACTION_BIND_TAG, cmd.action);
    TEST_ASSERT_EQUAL(5, cmd.tag_id);
}

/** 正常解析：解绑命令 0x21 */
void test_parse_cmd_unbind_tag(void)
{
    uint8_t data[] = {0x21};
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, sizeof(data), &cmd);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(SHARED_PROTO_ACTION_UNBIND_TAG, cmd.action);
}

/** 边界：NULL 数据 */
void test_parse_cmd_null_data(void)
{
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(NULL, 0, &cmd);
    TEST_ASSERT_FALSE(result);
}

/** 边界：长度为 0 */
void test_parse_cmd_zero_length(void)
{
    uint8_t data[] = {0x01};
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, 0, &cmd);
    TEST_ASSERT_FALSE(result);
}

/** 边界：非法命令码 */
void test_parse_cmd_invalid_code(void)
{
    uint8_t data[] = {0xFF};
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, sizeof(data), &cmd);
    TEST_ASSERT_FALSE(result);
}

/** 边界：update_qty 数据不足 */
void test_parse_cmd_update_qty_short(void)
{
    uint8_t data[] = {0x10}; /* 缺少 qty 字段 */
    shared_proto_unicast_cmd_t cmd = {0};
    bool result = shared_proto_parse_unicast_cmd(data, sizeof(data), &cmd);
    TEST_ASSERT_FALSE(result);
}

/* ========== 广播字段序列化测试 ========== */

/** 正常序列化 */
void test_serialize_adv_field_normal(void)
{
    shared_proto_adv_field_t field = {
        .magic = 0xAABBCCDD,
        .tag_id = 1,
        .qty = 100,
        .status = 0x02,
        .battery = 85,
        .seq = 1
    };
    uint8_t buf[32] = {0};
    uint16_t len = shared_proto_serialize_adv_field(&field, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(SHARED_PROTO_ADV_SERIALIZED_LEN, len);
    /* 验证 magic 大端序 */
    TEST_ASSERT_EQUAL(0xAA, buf[0]);
    TEST_ASSERT_EQUAL(0xBB, buf[1]);
    TEST_ASSERT_EQUAL(0xCC, buf[2]);
    TEST_ASSERT_EQUAL(0xDD, buf[3]);
}

/** 边界：buffer 不足 */
void test_serialize_adv_field_buffer_too_small(void)
{
    shared_proto_adv_field_t field = {0};
    uint8_t buf[4] = {0}; /* 远小于 12 字节 */
    uint16_t len = shared_proto_serialize_adv_field(&field, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, len); /* 应返回 0 或错误 */
}

/** 边界：NULL 参数 */
void test_serialize_adv_field_null(void)
{
    uint8_t buf[32] = {0};
    uint16_t len = shared_proto_serialize_adv_field(NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, len);
}

/* ========== 广播字段反序列化测试 ========== */

/** 正常反序列化 */
void test_deserialize_adv_field_normal(void)
{
    /* 构造大端序数据：magic=0xAABBCCDD, tag_id=1, qty=100, status=0x02, battery=85, seq=1 */
    uint8_t buf[] = {0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x01, 0x00, 0x64, 0x02, 0x55, 0x00, 0x01};
    shared_proto_adv_field_t field = {0};
    bool result = shared_proto_deserialize_adv_field(buf, sizeof(buf), &field);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0xAABBCCDD, field.magic);
    TEST_ASSERT_EQUAL(1, field.tag_id);
    TEST_ASSERT_EQUAL(100, field.qty);
    TEST_ASSERT_EQUAL(0x02, field.status);
    TEST_ASSERT_EQUAL(85, field.battery);
    TEST_ASSERT_EQUAL(1, field.seq);
}

/** 边界：数据长度不足 */
void test_deserialize_adv_field_short(void)
{
    uint8_t buf[] = {0xAA, 0xBB}; /* 只有 2 字节 */
    shared_proto_adv_field_t field = {0};
    bool result = shared_proto_deserialize_adv_field(buf, sizeof(buf), &field);
    TEST_ASSERT_FALSE(result);
}

/** 边界：NULL 参数 */
void test_deserialize_adv_field_null(void)
{
    shared_proto_adv_field_t field = {0};
    bool result = shared_proto_deserialize_adv_field(NULL, 0, &field);
    TEST_ASSERT_FALSE(result);
}

/** 边界：magic 不匹配 */
void test_deserialize_adv_field_bad_magic(void)
{
    uint8_t buf[] = {0x11, 0x22, 0x33, 0x44, 0x00, 0x01, 0x00, 0x64, 0x02, 0x55, 0x00, 0x01};
    shared_proto_adv_field_t field = {0};
    bool result = shared_proto_deserialize_adv_field(buf, sizeof(buf), &field);
    TEST_ASSERT_FALSE(result);
}

/* ========== 测试入口 ========== */

int main(void)
{
    UNITY_BEGIN();

    /* 命令解析 */
    RUN_TEST(test_parse_cmd_find_me);
    RUN_TEST(test_parse_cmd_stop_find);
    RUN_TEST(test_parse_cmd_inventory);
    RUN_TEST(test_parse_cmd_update_qty);
    RUN_TEST(test_parse_cmd_bind_tag);
    RUN_TEST(test_parse_cmd_unbind_tag);
    RUN_TEST(test_parse_cmd_null_data);
    RUN_TEST(test_parse_cmd_zero_length);
    RUN_TEST(test_parse_cmd_invalid_code);
    RUN_TEST(test_parse_cmd_update_qty_short);

    /* 序列化 */
    RUN_TEST(test_serialize_adv_field_normal);
    RUN_TEST(test_serialize_adv_field_buffer_too_small);
    RUN_TEST(test_serialize_adv_field_null);

    /* 反序列化 */
    RUN_TEST(test_deserialize_adv_field_normal);
    RUN_TEST(test_deserialize_adv_field_short);
    RUN_TEST(test_deserialize_adv_field_null);
    RUN_TEST(test_deserialize_adv_field_bad_magic);

    return UNITY_END();
}
