#pragma once

#include <string>

namespace runtime_paths {

// EXE 所在目录（不可变运行时资产目录）。
std::wstring ExecutableDir();

// 检测 EXE 旁是否存在 portable.flag 空文件。
// 存在则视为 Portable 模式，可变数据随 EXE。
bool IsPortableMode();

// 可变数据目录：Portable→EXE 目录；Installed→%LOCALAPPDATA%\VoxMic。
// 安装版目录不存在时自动创建。
std::wstring MutableDataDir();

// config.ini 完整路径 = MutableDataDir() + "\config.ini"。
std::wstring ConfigPath();

// 旧配置迁移（一次性）：仅非 Portable 模式执行。
// 若 LocalAppData 无 config.ini 且 EXE 旁有旧 config.ini，则复制到 LocalAppData。
// 保留源文件、不覆盖已有目标、全新安装时跳过。
void MigrateLegacyConfigIfNeeded();

} // namespace runtime_paths
