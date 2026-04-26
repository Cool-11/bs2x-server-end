# hardware_hal

## 功能描述
- 封装 GPIO 与定时器控制，提供蜂鸣器与指示灯开关能力。
- 收到寻物后启动自动关闭定时器，15 秒超时后在定时器回调内强制关断 GPIO，防止电池耗尽。

## 依赖关系
- 依赖底层驱动：`gpio.h`、`timer.h`。
- 由 `app/main.c` 在寻物指令分支直接调用。

## 逻辑验证
- 串口应出现：`[BS2x_HAL] Timer started for 15000 ms auto-off.`。
- 15 秒到期后应出现：`[BS2x_HAL] timer timeout, force off in ISR`。
- 若提前收到停止命令，应出现 `beep off`、`led off`。
