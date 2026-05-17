# 阶段四修复报告：BS21E 侧改动

> 日期: 2026-05-17
> 阶段: Phase 4 — BS21E 侧改动 (任务 #45-#52)
> 文件: main.c, sle_slave_mgr.c, shared_protocol.h

---

## 改动清单

### 1. storage_sync_clear_tag_id 保留 tag_id (#45) — 阶段一已完成

### 2. storage_sync_set_tag_id 重复绑定跳过 NV (#46) — 阶段一已完成

### 3. 断开事件：停止声光 + 恢复状态 (#47-48)

**文件**: `app/main.c`

新增 `my_project_2x_on_conn_state_changed` 回调：

```c
static void my_project_2x_on_conn_state_changed(uint16_t conn_id, bool connected)
{
    if (connected) return;
    /* 断开时停止声光 */
    hardware_hal_beep_off();
    hardware_hal_led_off();
    /* 恢复 FINDING → NORMAL */
    storage_sync_set_find_status(false);
    storage_sync_publish();
}
```

注册到 `sle_slave_callbacks_t.on_conn_state_changed`。

**原因**: WS63 断开后 BS21E 的蜂鸣器/LED 可能仍在响（15s 定时器），需要立即停止并恢复广播状态。

### 4. UART 自测逻辑抽公共函数 (#49)

**文件**: `app/main.c`

提取 `my_project_2x_exec_cmd(cmd, conn_id, source)` 公共函数，`uart_selftest_exec` 和 `on_unicast_cmd` 共用。

- `conn_id=0` 时为 UART 自测模式（inventory 打印到日志，bind/unbind 不发 notify）
- `conn_id!=0` 时为 SLE 模式（inventory 发 notify，bind/unbind 发响应）

减少约 80 行重复代码。

### 5. MAC 种子增强 (#50)

**文件**: `components/sle_slave/sle_slave_mgr.c`

```c
uint32_t seed = (uint32_t)(uapi_tcxo_get_ms() & 0xFFFFFFFF);
uint8_t chip_id[8] = {0};
if (uapi_efuse_get_chip_id(chip_id, sizeof(chip_id)) == ERRCODE_SUCC) {
    for (uint8_t i = 0; i < sizeof(chip_id); i++) {
        seed ^= (uint32_t)chip_id[i] << ((i % 4) * 8);
    }
}
```

**原因**: 仅用时间戳做种子，同时上电的多标签可能生成相同 MAC。混入芯片唯一 ID 后冲突概率大幅降低。

### 6. FINDING 重复收到 0x01 加日志 (#51)

**文件**: `app/main.c` — `my_project_2x_exec_cmd`

```c
case SHARED_PROTO_ACTION_FIND_ME:
    /* 如果已在寻物中，记录重复日志 */
    if (storage_sync_get_qty() == 0) {
        shared_proto_adv_field_t cur = {0};
        storage_sync_get_field(&cur);
        if (cur.status == SHARED_PROTO_STATUS_FINDING) {
            osal_printk("%s[%s] FIND_ME repeated, already FINDING\r\n", ...);
        }
    }
```

### 7. unbind_rsp 结构体语义化 (#52)

**文件**: `components/shared_protocol/shared_protocol.h`

```c
typedef shared_proto_bind_rsp_t shared_proto_unbind_rsp_t;
```

`main.c` 中 `my_project_2x_send_unbind_rsp` 使用 `shared_proto_unbind_rsp_t` 类型。

---

## 关联任务

| # | 任务 | 状态 |
|---|------|------|
| 45 | clear_tag_id 保留 tag_id | ✅ (阶段一) |
| 46 | set_tag_id 重复绑定跳过 NV | ✅ (阶段一) |
| 47 | 断开停止声光 | ✅ |
| 48 | 断开恢复状态 | ✅ |
| 49 | UART 自测抽公共函数 | ✅ |
| 50 | MAC 种子混入芯片 ID | ✅ |
| 51 | FINDING 重复日志 | ✅ |
| 52 | unbind_rsp 语义别名 | ✅ |
