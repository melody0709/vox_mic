# 文档项目命名统一方案

## 问题

README.md 标题 `# AudioSource Win (Raw WASAPI)` 是旧项目名，应改为项目名 `VoxMic`。同时需排查所有文档中遗留的旧命名。

## 当前命名状态

| 名称 | 用途 | 是否需改 |
|------|------|---------|
| `VoxMic` | 品牌/产品名 (PascalCase) | ✅ 正确，无需改 |
| `voxmic` | 技术标识符 (全小写) | ✅ 正确，无需改 |
| `vox_mic` | 仓库/目录名 (snake_case) | ✅ 正确，无需改 |
| `AudioSource Win (Raw WASAPI)` | README 标题旧名 | ❌ 需改为 VoxMic |
| `audiosource.exe` | ARCHITECTURE.md 中的旧 exe 名 | ❌ 需改为 `voxmic.exe` |
| `vox_mic` (描述性文本中) | CHANGELOG 中指代程序 | ❌ 需改为 VoxMic |

## 不改的部分（正确引用）

| 名称 | 原因 |
|------|------|
| `setupAudioSource()` | 代码函数名，不改文档引用 |
| `MediaRecorder.AudioSource` | Android API 枚举，不改 |
| `gdzx/audiosource` | 上游项目名，不改 |
| `fr.dzx.audiosource` | 旧包名（对比表中），不改 |
| `audiosource` (Socket 名对比) | 旧 Socket 名（对比表中），不改 |
| `com.voxmic.source` | 当前包名，正确 |
| `voxmicsource` | 当前 Socket 名，正确 |
| `voxmic.exe` | 当前 exe 名，正确 |
| `voxmic.keystore` / `voxmic` (alias) | 当前签名配置，正确 |

## 需要修改的文件和位置

### 1. README.md (EN) — 标题

- **行 1**: `# AudioSource Win (Raw WASAPI)` → `# VoxMic`
- 这是项目入口，标题应使用品牌名

### 2. README.md (ZH) — 标题

- **行 1**: `# AudioSource Win (Raw WASAPI)` → `# VoxMic`

### 3. ARCHITECTURE.md (EN) — 数据流图中的旧 exe 名

- **行 14**: `audiosource.exe` → `voxmic.exe`

### 4. ARCHITECTURE.md (ZH) — 数据流图中的旧 exe 名

- **行 14**: `audiosource.exe` → `voxmic.exe`

### 5. CHANGELOG.md (EN) — 描述性文本中的旧项目名

- **行 13**: `requiring vox_mic restart` → `requiring VoxMic restart`

### 6. CHANGELOG.md (ZH) — 描述性文本中的旧项目名

- **行 13**: `需重启 vox_mic` → `需重启 VoxMic`

## 不修改的文件

| 文件 | 原因 |
|------|------|
| AGENTS.md (EN/ZH) | 已使用 `voxmic.exe`，无旧名 |
| FUTURE_ROADMAP.md (EN/ZH) | `AudioSource` 仅出现在 Android API 上下文，正确 |
| Audio_Settings_Guide.md (EN/ZH) | 无旧名问题 |
| android_app/README.md (EN/ZH) | `audiosource` 仅出现在对比表/上游引用中，正确 |
| plan/ 目录 | 历史文档，不修改 |

## 实施步骤

1. 修改 README.md (EN) 标题: `AudioSource Win (Raw WASAPI)` → `VoxMic`
2. 修改 README.md (ZH) 标题: `AudioSource Win (Raw WASAPI)` → `VoxMic`
3. 修改 ARCHITECTURE.md (EN) 数据流图: `audiosource.exe` → `voxmic.exe`
4. 修改 ARCHITECTURE.md (ZH) 数据流图: `audiosource.exe` → `voxmic.exe`
5. 修改 CHANGELOG.md (EN): `vox_mic restart` → `VoxMic restart`
6. 修改 CHANGELOG.md (ZH): `重启 vox_mic` → `重启 VoxMic`
7. 验证所有文件无旧名遗留
