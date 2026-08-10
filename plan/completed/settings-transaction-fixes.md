# Settings transaction fixes

状态：实施完成

## 范围

- 完整恢复 General/DSP 页面快照，修复 Cancel、重新打开和 Reset 语义。
- 提交失败时恢复 DSP 预览原子量。
- 严格校验端口，修复托盘开关保存失败后的运行态回滚。
- 清理 dead code、重复取消路径和处理链状态文案。
- 保留旧 config.ini 在缺少新键时的旧默认行为。

## 验证

- [x] RNNoise-only CMake/Ninja 构建通过。
- [x] 完成 diff、状态和运行入口检查。
