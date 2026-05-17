# 阶段一修复报告：存储层优化

> 日期: 2026-05-17
> 阶段: Phase 1 — 协议层同步 (任务 #7-#10)
> 文件: storage_sync.c, Kconfig

---

## 改动清单

### Fix P1-1: 出库保留 tag_id

**文件**: `storage_sync.c` — `storage_sync_clear_tag_id()`
**任务**: #7

```c
/* 修复前：清零 tag_id + qty + status */
g_sync.field.tag_id = 0;
g_sync.field.qty = 0;
g_sync.field.status = STORAGE_SYNC_STATUS_NORMAL;
storage_sync_save_tag_id_nv(0);  // NV 也写 0

/* 修复后：保留 tag_id，只清 qty/status */
g_sync.field.qty = 0;
g_sync.field.status = STORAGE_SYNC_STATUS_NORMAL;
// 不再写 NV tag_id=0
```

**原因**: 出库后标签仍需保留 tag_id 身份，以便下次入库时无需重新绑定。tag_id 是系统全局标识，清零会导致 WS63 映射表和 ESP32 TF 卡索引失效。

### Fix P1-2: 重复绑定跳过 NV 写入

**文件**: `storage_sync.c` — `storage_sync_set_tag_id()`
**任务**: #8

```c
/* 新增：相同 tag_id 跳过 NV 写入 */
if (g_sync.field.tag_id == tag_id) {
    osal_printk("%s[BP] set_tag_id=%u same, skip NV write\r\n", STORAGE_SYNC_LOG, tag_id);
    return ERRCODE_SUCC;
}
```

**原因**: NV 写入次数有限（~10万次），重复绑定相同 tag_id 时跳过写入可延长 flash 寿命。

### Fix P1-3: Kconfig 暴露 MAX_CONNECTIONS

**文件**: `components/sle_slave/Kconfig`
**任务**: #10

```kconfig
config MY_PROJECT_2X_MAX_CONNECTIONS
    int "Maximum SLE connections"
    default 4
    range 1 8
    depends on MY_PROJECT_2X_SLE_SLAVE_ENABLE
```

**原因**: 原先硬编码在头文件中，无法通过 menuconfig 配置。暴露后可灵活调整最大连接数。

---

## 验证方法

1. BS21E 设置 tag_id=5，入库，出库 → 确认广播中 tag_id 仍为 5
2. 重复发送 BIND_TAG tag_id=5 → 确认日志显示 "same, skip NV write"
3. menuconfig 中可看到 MY_PROJECT_2X_MAX_CONNECTIONS 选项

---

## 关联任务

- #7 出库保留 tag_id ✅
- #8 重复绑定跳过 NV 写入 ✅
- #10 Kconfig MAX_CONNECTIONS ✅
