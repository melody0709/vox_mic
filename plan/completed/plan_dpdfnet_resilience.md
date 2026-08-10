# VoxMic DPDFNet 后端稳健性优化

创建日期：2026-08-09  
目标版本：`0.6.4`

## 目标

1. 先同步桌面端 `APP_VERSION`、Android `versionName`/`versionCode`。
2. 修复 epoch reset 时 worker 清空新 epoch 首块的竞态，并丢弃推理期间变旧的结果。
3. 增加 DPDFNet underflow/liveness watchdog：短暂预热允许有限静音；稳态连续无输出时切换到 RNNoise，并暴露 degraded 状态。
4. 使用仓库固定的 sherpa-onnx C API 头文件替代手写 ABI 镜像，移除未使用的 Flush symbol 依赖，并缓存 QPC 频率。
5. 增加 RNNoise ↔ DPDFNet 切换、epoch reset 和 watchdog fallback smoke 覆盖。

## 验收

- RNNoise-only 与 DPDFNet payload 均可构建、安装、运行。
- DPDFNet smoke、缺资源 fallback smoke、pipeline 切换 smoke 通过。
- 无 input/output drops、无非有限音频；watchdog 故障路径有效。
- runtime manifest/layout、`git diff --check`、Android debug 和 0.6.4 Portable/MSI 验证通过。

## 实施结果（2026-08-09）

- 版本升级为桌面 `APP_VERSION=0.6.4`、Android `versionName=0.6.4`、`versionCode=10`。
- 修复 epoch reset 首块竞态；processor smoke 的重复 reset 最低输出为 `28/30`，input/output drops 均为 0。
- Pipeline watchdog 在故意增加 100ms worker 延迟时，于第 5 个连续空输出 block 降级到 RNNoise；ready-but-stalled 可通过 Settings 的 OK/reset 重试，硬 session failure 保持 unavailable。
- DPDFNet pipeline switch smoke 通过：RNNoise ↔ DPDFNet、7 次 reset、watchdog fallback、无非有限输出；最终测试 underflows=21、FIFO drops=0。
- RNNoise-only 构建运行目录校验为 1 个 payload 文件；DPDFNet 构建运行目录校验为 6 个 payload 文件。
- Android debug、Portable、MSI 和 runtime manifest/layout 均通过；0.6.3 旧包未被覆盖，0.6.4 包已生成。
