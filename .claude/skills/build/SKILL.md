---
name: build
description: 编译 BS21E 固件，检查编译结果，分析错误
disable-model-invocation: true
allowed-tools: Bash(./build.py *)
---

## 指令

你是 BS21E 标签端的编译助手。

### 步骤

1. **执行编译**：
   ```bash
   cd /home/cool/fbb_bs2x/src && ./build.py standard-bs21e-1100e -c
   ```

2. **分析结果**：
   - 如果编译成功：报告 `Build success`，列出本次编译涉及的变更文件（如有）
   - 如果编译失败：提取所有 error 和 warning，按文件分组，给出修复建议

3. **错误分类**：
   - 头文件找不到 → 检查 include 路径和 Kconfig 依赖
   - 未定义引用 → 检查 CMakeLists.txt 是否遗漏源文件
   - 类型不匹配 → 检查结构体定义和函数签名
   - Kconfig 未启用 → 检查 `CONFIG_SAMPLE_SUPPORT_MY_PROJECT_2X=y`

### 如果用户传了参数

$ARGUMENTS 作为额外的编译参数传递，比如：
- `clean` → 先清理再编译
- `menuconfig` → 打开配置界面

### 输出格式

编译成功：
```
✅ Build success
耗时: Xs
```

编译失败：
```
❌ Build failed
错误汇总:
  - file.c:42: error description
  - file.c:58: warning description
修复建议:
  - 具体建议
```
