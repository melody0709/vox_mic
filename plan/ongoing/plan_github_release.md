# Phase 11: GitHub v0.5.1 发布准备

## 目标

发布 v0.5.1 GitHub Release，包含 Windows exe + Android 签名 release APK。

## 任务清单

| # | 类别 | 任务 | 文件 |
|---|------|------|------|
| 1 | 底盘 | 添加 BSD-3-Clause LICENSE | 根目录 |
| 2 | 修复 | README 修正 exe 名称 `audiosource.exe` → `voxmic.exe` | `README.md` |
| 3 | 清理 | `plan_optimize.md` 归档到 completed | `plan/ongoing/` |
| 4 | Android | 生成 release keystore | `android_app/voxmic.keystore` |
| 5 | Android | 配置 `keystore.properties` (排除 git) | `android_app/keystore.properties` |
| 6 | Android | `build.gradle` signingConfig + APK 重命名 | `android_app/app/build.gradle` |
| 7 | Android | `.gitignore` 排除 `*.keystore`, `*.jks`, `keystore.properties`, `*.apk` | `.gitignore` |
| 8 | 构建 | `build.bat` 编译 exe | `build\voxmic.exe` |
| 9 | 构建 | `assembleRelease` 生成 APK | `VoxMic_Source-v0.5.1.apk` |
| 10 | 发布 | git tag v0.5.1, 创建 GitHub Release 上传两个 artifacts | |

## APK 输出名规则

```
VoxMic_Source-v0.5.1.apk
```

自动从 `APP_VERSION` 宏 (src/version.h) 生成，无需手动同步。

## 注意事项

- keystore 密码/别名写进 `keystore.properties`，该文件不提交 git
- APK 签名后可用于 ADB 侧载，Google Play 发布另需 Play App Signing
- release APK 与 debug APK 不冲突，两个构建产物独立存在
