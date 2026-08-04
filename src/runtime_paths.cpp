#include "runtime_paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace runtime_paths {

constexpr wchar_t kPortableFlagName[] = L"portable.flag";
constexpr wchar_t kAppName[] = L"VoxMic";
constexpr wchar_t kConfigFileName[] = L"config.ini";

static std::wstring CurrentExecutablePath() {
    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (length == 0) return {};
        if (length < capacity - 1) return std::wstring(buffer.data(), length);
    }
    return {};
}

static bool PathExistsAsFile(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool EnsureDirectory(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    return CreateDirectoryW(path.c_str(), nullptr) != 0;
}

std::wstring ExecutableDir() {
    const std::wstring exePath = CurrentExecutablePath();
    if (exePath.empty()) return L".";
    const size_t pos = exePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return exePath.substr(0, pos);
}

bool IsPortableMode() {
    return PathExistsAsFile(ExecutableDir() + L"\\" + kPortableFlagName);
}

std::wstring MutableDataDir() {
    if (IsPortableMode()) {
        return ExecutableDir();
    }
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        result = raw;
        CoTaskMemFree(raw);
    }
    if (result.empty()) return ExecutableDir();
    result += L"\\";
    result += kAppName;
    EnsureDirectory(result);
    return result;
}

std::wstring ConfigPath() {
    return MutableDataDir() + L"\\" + kConfigFileName;
}

// 旧配置迁移（一次性）：仅非 Portable 模式执行。
// 若 MutableDataDir()\config.ini 不存在且 EXE 旁有旧 config.ini，则复制到 LocalAppData。
// 保留源文件（不删除、不覆盖已有目标）。EXE 旁无 config.ini（全新安装）时跳过。
void MigrateLegacyConfigIfNeeded() {
    if (IsPortableMode()) return;

    const std::wstring target = ConfigPath();
    if (PathExistsAsFile(target)) return;

    const std::wstring legacy = ExecutableDir() + L"\\" + kConfigFileName;
    if (!PathExistsAsFile(legacy)) return;

    CopyFileW(legacy.c_str(), target.c_str(), TRUE);
}

} // namespace runtime_paths
