# BS21E Server端 修改汇报与测试文档 v3

## 修改日期: 2026-05-08

---

## 一、问题清单与修复方案

### 问题1: 广播数据端序错误 (adv payload 0xDD 0xCC... 而非 0xAA开头)

**现象**: 63端扫描到的adv payload前4字节为 `0xDD 0xCC 0xBB 0xAA`，而协议约定magic为 `0xAABBCCDD`，期望前4字节为 `0xAA 0xBB 0xCC 0xDD`。

**根因**: 代码使用 `memcpy` 直接将结构体拷贝到广播buffer，ARM Cortex-M为小端模式，`uint32_t magic = 0xAABBCCDD` 在内存中存放为 `DD CC BB AA`，导致传输端序反转。

**修复方案**: 实现序列化/反序列化函数，显式按大端序（网络字节序）写入多字节字段。

**修改文件**:
| 文件 | 修改内容 |
|------|----------|
| `shared_protocol.h` | 新增 `shared_proto_serialize_adv_field()`, `shared_proto_serialize_inventory_rsp()`, `shared_proto_serialize_bind_rsp()` 声明及长度宏 |
| `shared_protocol.c` | 实现 `proto_write_u16_be()`, `proto_write_u32_be()` 辅助函数和3个序列化函数 |
| `sle_slave_mgr.c` | `sle_slave_encode_manufacturer_adv()` 改用 `shared_proto_serialize_adv_field()` 替代 `memcpy` |
| `main.c` | `my_project_2x_send_inventory_rsp()` 和 `my_project_2x_send_bind_rsp()` 改用序列化函数 |

**验证方法**:
1. 烧录后查看串口日志，确认 `[BP] adv payload first 4 bytes: 0xAA 0xBB 0xCC 0xDD`
2. 63端扫描解析后magic字段应为 `0xAABBCCDD`
3. 63端Write 0x02后，21e端Notify回复的inventory数据端序正确

---

### 问题2: SSAP服务连接后无法交换数据

**现象**: 63端获取MAC地址后能建立SLE连接，但无法发现SSAP服务或写入特征值。

**根因分析**: 端序错误导致63端解析广播数据时magic校验失败，可能影响后续连接流程。同时原有日志不够详细，无法定位具体卡在哪一步。

**修复方案**: 增强连接状态回调日志，打印对端地址、配对状态、服务/特征handle。

**修改文件**:
| 文件 | 修改内容 |
|------|----------|
| `sle_slave_mgr.c` | `sle_slave_connect_state_changed_cbk()` 增加对端MAC地址、pair_state、server_id/service_handle/property_handle日志 |
| `sle_slave_mgr.c` | `ssaps_server_write_request_cbk()` 增加server_id、handle字段日志 |

**验证方法**:
1. 63端连接后，21e串口应打印 `[BP] conn_cb id:0x1 state:0x1 pair:0x0 addr:XX:XX:XX:XX:XX:XX`
2. 63端Write时，21e串口应打印 `[BP] SSAP Write srv:0 conn:0x1 hdl:0xXXXX len:N data:...`
3. 若仍无法Write，检查handle值是否与63端discovery结果匹配

---

### 问题3: 无源蜂鸣器配置错误

**现象**: 原代码使用GPIO输出驱动蜂鸣器，但实际硬件为无源蜂鸣器，需要PWM方波驱动才能发声。原配置GPIO8、pin_mode 0不正确。

**正确配置**: pin9, pin_mode 40, channel 0, group ID 0

**修复方案**: 将蜂鸣器驱动从GPIO改为PWM，参考官方pwm_demo实现。

**修改文件**:
| 文件 | 修改内容 |
|------|----------|
| `Kconfig` | 新增 `CONFIG_MY_PROJECT_2X_PWM_PIN`(默认9)、`CONFIG_MY_PROJECT_2X_PWM_PIN_MODE`(默认40)、`CONFIG_MY_PROJECT_2X_PWM_CHANNEL`(默认0)、`CONFIG_MY_PROJECT_2X_PWM_GROUP_ID`(默认0) |
| `hardware_hal.c` | 移除GPIO蜂鸣器控制，新增 `hw_hal_pwm_buzzer_init()` 和 `hw_hal_pwm_buzzer_stop()`；`hardware_hal_beep_on_for_ms()` 改用 `uapi_pwm_start()`/`uapi_pwm_start_group()`；`hardware_hal_beep_off()` 改用 `uapi_pwm_stop()`/`uapi_pwm_stop_group()`；`hw_hal_force_all_off()` 改用PWM stop |

**关键PWM配置参数**:
```c
pwm_config_t cfg = {
    .low_time   = 100,    // 低电平周期计数
    .high_time  = 100,    // 高电平周期计数（50%占空比）
    .offset_time = 0,     // 无相位偏移
    .cycles     = 0xFF,   // 重复周期数
    .repeat     = true,   // 连续输出
};
```

**V151/V150兼容**:
- V151: 使用 `uapi_pwm_set_group()` + `uapi_pwm_start_group()` / `uapi_pwm_stop_group()`
- V150: 使用 `uapi_pwm_start()` / `uapi_pwm_stop()`

**验证方法**:
1. 烧录后串口应打印 `[BP] pwm_buzzer_init OK pin:20 mode:40 ch:0 freq:XXXXHz`
2. 63端发送寻物命令(0x01)后，蜂鸣器应发出持续蜂鸣声
3. 超时后自动停止，串口打印 `[BP] auto_off_timer timeout`
4. 手动停止寻物时，蜂鸣器立即停止

---

### 问题4: 低功耗状态切换时PWM未关闭

**现象**: 设备进入Standby/Sleep状态时，PWM可能仍在输出，导致额外功耗。

**修复方案**: 在PM状态转换回调中增加蜂鸣器和LED的关闭操作。

**修改文件**:
| 文件 | 修改内容 |
|------|----------|
| `main.c` | `my_project_2x_work_to_standby()` 增加 `hardware_hal_beep_off()` + `hardware_hal_led_off()` |
| `main.c` | `my_project_2x_standby_to_sleep()` 增加 `hardware_hal_beep_off()` + `hardware_hal_led_off()` |

---

## 二、边界处理

| 场景 | 处理方式 |
|------|----------|
| 序列化buffer不足 | `shared_proto_serialize_*()` 返回0，调用方跳过发送并打印FAIL日志 |
| 序列化参数NULL | 返回0，打印FAIL日志 |
| PWM init失败 | `hardware_hal_init()` 打印FAIL但继续初始化LED和定时器 |
| PWM start失败 | `hardware_hal_beep_on_for_ms()` 返回 `HW_HAL_ERR_OS` |
| 未初始化时调用beep_off/led_off | 直接返回 `HW_HAL_OK`，不操作硬件 |
| 自动关闭定时器启动失败 | 调用 `hw_hal_force_all_off()` 确保硬件安全状态 |
| PM进入低功耗 | 主动关闭蜂鸣器和LED，防止睡眠期间PWM持续输出 |
| 63端连接后断开 | `sle_slave_remove_connection()` 清理连接，打印断开原因 |

---

## 三、二次排查验证清单

### 3.1 端序验证
- [ ] 烧录后串口日志 `adv payload first 4 bytes` 显示 `0xAA 0xBB 0xCC 0xDD`
- [ ] 63端扫描到广播后magic字段解析为 `0xAABBCCDD`
- [ ] 63端Write 0x02后，21e端Notify回复数据端序正确

### 3.2 SSAP连接验证
- [ ] 63端连接后，21e串口打印 `[BP] CONNECTED` 及server_id/svc_hdl/prop_hdl
- [ ] 63端Write时，21e串口打印 `[BP] SSAP Write` 及handle和数据
- [ ] 若Write无响应，对比63端discovery的handle与21e端日志中的prop_hdl

### 3.3 PWM蜂鸣器验证
- [ ] 初始化日志显示 `[BP] pwm_buzzer_init OK pin:20 mode:40 ch:0 freq:XXXHz`
- [ ] 发送寻物命令后蜂鸣器发声
- [ ] 超时或手动停止后蜂鸣器静音
- [ ] freq值合理（应在1kHz~10kHz范围内，对应无源蜂鸣器谐振频率）

### 3.4 低功耗验证
- [ ] Work→Standby时蜂鸣器和LED关闭
- [ ] Standby→Sleep时蜂鸣器和LED关闭
- [ ] 唤醒后SLE广播恢复正常

---

## 四、编译验证结果

```
Build target:standard_bs21e_1100e success
```

全部4个阶段编译验证通过，无warning无error。
