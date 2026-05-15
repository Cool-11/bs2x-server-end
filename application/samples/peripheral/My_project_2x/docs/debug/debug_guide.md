# BS21E 调试指南（大白话版）

> 写给谁：第一次拿到板子、不知道怎么测试的人
> 更新日期：2026-05-15

---

## 一、你需要准备什么

| 工具 | 用途 | 备注 |
|------|------|------|
| 串口工具 | 和 BS21E 板子对话 | SSCOM / PuTTY / VSCode 串口插件都行 |
| USB 转 TTL | 连接电脑和板子 | TX→RX，RX→TX，GND→GND |
| BS21E 开发板 | 已烧录固件 | 确认编译通过 |

### 串口工具设置（重要！）

```
波特率：115200
数据位：8
停止位：1
校验位：无
发送模式：HEX          ← 一定要选 HEX，不要选 ASCII
追加换行：关掉！        ← 不勾选"发送新行"，否则会多出 0D 0A 导致解析失败
```

**为什么不能开追加换行？**
```
你想发送：01（寻物命令，1个字节）
串口工具偷偷加了：0D 0A（回车+换行，2个字节）
实际到达板子：01 0D 0A（3个字节）
板子以为你发了3个字节，不是1个 → 解析失败
```

---

## 二、命令速查表

直接在串口工具的 HEX 发送框里输入以下内容：

### 基础命令

| 你要做什么 | HEX 输入 | 点击发送后板子会怎样 |
|-----------|----------|-------------------|
| 停止寻物 | `00` | 如果正在响，立即停 |
| 寻物 | `01` | 蜂鸣器响 + LED亮，15秒后自动停 |
| 盘点 | `02` | 返回当前 tag_id、数量、状态、电量 |
| 更新数量为50 | `10 00 32` | 广播里的 qty 变成 50 |
| 绑定标签为3号 | `20 00 03` | tag_id 变成 3，写入 NV 永久保存 |
| 解绑标签 | `21` | tag_id 清零，回到未配网状态 |

### 数量换算（HEX 怎么写）

```
数量 1  → HEX: 10 00 01
数量 5  → HEX: 10 00 05
数量 10 → HEX: 10 00 0A   （十进制10 = 十六进制0A）
数量 50 → HEX: 10 00 32   （十进制50 = 十六进制32）
数量 100 → HEX: 10 00 64  （十进制100 = 十六进制64）
数量 256 → HEX: 10 01 00  （256 = 0x0100，高字节在前）

怎么算：打开电脑计算器 → 切换到程序员模式 → 输入十进制数 → 看十六进制
```

### tag_id 换算

```
绑定1号标签 → HEX: 20 00 01
绑定2号标签 → HEX: 20 00 02
绑定10号标签 → HEX: 20 00 0A
绑定100号标签 → HEX: 20 00 64
```

---

## 三、按顺序调试（从零开始）

### 第1步：确认板子能跑

```
上电后串口应该看到：
  [BS2x_APP][BP] ===== APP ENTRY START =====
  [BS2x_HAL][BP] init OK
  [BS2x_SLE] sle enable cbk status:0
  [BS2x_SLE] announce enable cb id:1 status:0x0 started:1
  [BS2x_SYNC][BP] init OK tag:1 qty:0 battery:100
  [BS2x_APP][BP] ===== APP ENTRY DONE, entering main loop =====

如果没看到这些日志 → 检查烧录和串口连接
```

### 第2步：绑定一个标签

```
发送 HEX：20 00 05    （绑定为5号标签）

应该看到：
  [BS2x_PROTO][BP] parse OK action=BIND_TAG(0x20) tag_id=5
  [BS2x_SYNC][BP] NV write tag_id OK val:5
  [BS2x_SYNC][BP] set_tag_id=5 seq=1
  [BS2x_APP][UART_TEST] << BIND_TAG done

如果看到 FAIL → 看下面的"常见问题"章节
```

### 第3步：确认绑定成功

```
发送 HEX：02    （盘点）

应该看到：
  [BS2x_APP][UART_TEST] tag:5 qty:0 status:0x00 bat:100 seq:1
                                ↑
                            tag_id 应该是 5

如果 tag_id 不是 5 → 绑定没成功，回去看第2步的日志
```

### 第4步：更新数量

```
发送 HEX：10 00 0A    （更新数量为10）

应该看到：
  [BS2x_PROTO][BP] parse OK action=UPDATE_QTY(0x10) qty=10
  [BS2x_SYNC][BP] set_qty=10 status=0x00 seq=2

再盘点确认：发送 02
  [BS2x_APP][UART_TEST] tag:5 qty:10 status:0x00 bat:100 seq:2
                                ↑
                            qty 应该是 10
```

### 第5步：测试寻物

```
发送 HEX：01    （寻物）

应该发生：
  1. 蜂鸣器开始响
  2. LED 开始亮
  3. 日志显示：
     [BS2x_APP][UART_TEST] >> FIND_ME
     [BS2x_HAL][BP] beep_on OK
     [BS2x_HAL][BP] led_on OK

15秒后应该自动停止：
  [BS2x_HAL][BP] auto_off_timer timeout
```

### 第6步：手动停止寻物

```
如果不想等15秒，发送 HEX：00    （停止寻物）

应该发生：
  1. 蜂鸣器立即停止
  2. LED 立即熄灭
```

### 第7步：解绑标签

```
发送 HEX：21    （解绑）

应该看到：
  [BS2x_PROTO][BP] parse OK action=UNBIND_TAG(0x21)
  [BS2x_SYNC][BP] clear_tag_id OK old=5 new=0 qty=0 seq=4
  [BS2x_APP][UART_TEST] << UNBIND_TAG done
```

### 第8步：确认解绑成功

```
发送 HEX：02    （盘点）

应该看到：
  [BS2x_APP][UART_TEST] tag:0 qty:0 status:0x00 bat:100 seq:4
                                ↑
                            tag_id 应该是 0（未配网）

如果 tag_id 不是 0 → 解绑没成功
```

### 第9步：断电重启，验证 NV 存储

```
1. 拔掉电源
2. 重新插上
3. 看串口日志：

解绑后重启应该看到：
  [BS2x_SYNC] NV read tag_id OK val:0
  → 说明解绑已经保存到 NV 了，重启不丢

如果之前绑定了没解绑就重启：
  [BS2x_SYNC] restored tag_id=5 from NV
  → 说明绑定也保存到 NV 了，重启不丢
```

---

## 四、常见问题

### 问题1：发送命令后没反应

```
原因：串口工具没选 HEX 模式，选成了 ASCII
解决：切换到 HEX 模式

原因：串口线没接好
解决：检查 TX/RX 是否交叉连接，GND 是否接了
```

### 问题2：parse FAIL len mismatch

```
原因：串口工具开了"追加换行"
解决：关掉"追加换行"或"发送新行"选项

原因：手动输入了多余的字符
解决：HEX 框里只输命令字节，不要有空格以外的字符
```

### 问题3：NV write FAIL

```
原因：Flash 写入失败
解决：可能是 NV 分区损坏，需要全量烧录（会丢失 MAC 和 tag_id）

预防：不要频繁写 NV（绑定/解绑不要太快，间隔至少 1 秒）
```

### 问题4：蜂鸣器不响

```
原因：PWM 配置不对
检查：menuconfig 里的 PWM_PIN、PWM_PIN_MODE、PWM_CHANNEL 是否正确

原因：蜂鸣器是无源的，需要 PWM 驱动
检查：确认用的是无源蜂鸣器，不是有源的
```

### 问题5：广播数据看不到

```
原因：SLE 广播没启动
检查日志里有没有：
  [BS2x_SLE] announce enable cb id:1 status:0x0 started:1

如果没有 → SLE 协议栈初始化失败
检查：CONFIG_SAMPLE_SUPPORT_MY_PROJECT_2X=y 是否开了
```

### 问题6：绑定后断电重启，tag_id 丢了

```
原因：NV 写入没成功
检查日志里有没有：
  [BS2x_SYNC] NV write tag_id OK val:X

如果没有 OK → NV 写入失败，参考问题3

原因：全量烧录了（擦除了 NV 分区）
正常烧录 application 不会影响 NV，只有全量烧录才会丢
```

---

## 五、WS63 端联调流程

当 UART 本地测试都通过后，可以和 WS63 联调：

```
1. WS63 扫描
   → 应该能看到 BS2x_Tag 设备
   → 广播数据里有 magic=0xAABBCCDD

2. WS63 连接
   → 用 MAC 地址连接（不是 tag_id）
   → 连接成功后 BS21E 日志显示 CONNECTED

3. WS63 写 CCCD
   → BS21E 日志显示 SSAP Write data: 01 00
   → 这步不做的话收不到 Notify 回复

4. WS63 发送绑定命令 0x20
   → BS21E 日志显示 BIND_TAG
   → WS63 应该收到 Notify 0xA0 确认

5. WS63 发送解绑命令 0x21
   → BS21E 日志显示 UNBIND_TAG
   → WS63 应该收到 Notify 0xA1 确认

6. WS63 断开连接
   → BS21E 日志显示 DISCONNECTED
```

---

## 六、日志前缀速查

看到日志不知道是哪个模块？看前缀：

| 前缀 | 模块 | 干什么的 |
|------|------|---------|
| `[BS2x_APP]` | 主程序 | 命令分发、业务逻辑 |
| `[BS2x_PROTO]` | 协议解析 | 把收到的字节翻译成命令 |
| `[BS2x_SLE]` | SLE通信 | 广播、连接、Notify |
| `[BS2x_SYNC]` | 数据存储 | NV读写、广播数据更新 |
| `[BS2x_HAL]` | 硬件驱动 | LED、蜂鸣器、PWM |
| `[UART_TEST]` | 串口测试 | 通过串口发送的测试命令 |
| `[BP]` | 断点日志 | 每个关键步骤都有，搜 BP 看全链路 |
