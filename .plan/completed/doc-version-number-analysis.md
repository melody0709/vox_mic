# 文档版本号保留分析方案

## 现状调查

### 各文档标题中的版本号

| 文件 | 当前标题 | 版本号 | 中文版标题 | 中文版版本号 |
|------|---------|--------|-----------|-------------|
| README.md | `# AudioSource Win (Raw WASAPI) v0.5.3` | v0.5.3 | `# AudioSource Win (Raw WASAPI) v0.5.3` | v0.5.3 |
| ARCHITECTURE.md | `# Architecture -- v0.5.3` | v0.5.3 | `# 架构说明 — v0.5.3` | v0.5.3 |
| AGENTS.md | `# AGENTS.md -- v0.5.3` | v0.5.3 | `# AGENTS.md — v0.5.3` | v0.5.3 |
| FUTURE_ROADMAP.md | `# Future Roadmap -- v0.5.3+` | v0.5.3+ | `# 未来路线图 — v0.5.3+` | v0.5.3+ |
| CHANGELOG.md | `# Changelog` | 无 | `# Changelog` | 无 |
| Audio_Settings_Guide.md | `# Microphone Audio Parameter Tuning Guide` | 无 | `# 🎙️ 麦克风音频参数精调指南` | 无 |
| android_app/README.md | `# VoxMic Source — Android App` | 无 | `# VoxMic Source — Android App` | 无 |

### AGENTS.md 中的版本号更新清单

AGENTS.md 的 "Version Bump" 部分列出了每次版本升级需要修改的文件清单，其中第 2-5 项要求修改 README.md、AGENTS.md、ARCHITECTURE.md、FUTURE_ROADMAP.md 的标题版本号。

### 真实版本号来源

- **唯一权威来源**: `src/version.h` 中的 `#define APP_VERSION "0.5.3"`
- **自动消费**: `src/tray_icon.cpp` 通过 `#include "version.h"` 自动显示版本号
- **Android**: `android_app/app/build.gradle` 中的 `versionName`

---

## 分析：各文档版本号的必要性

### 1. README.md — **建议移除**

- **现状**: 标题含 `v0.5.3`
- **理由**: README 是项目入口文档，描述的是项目整体功能，不是某个特定版本。版本号信息已经在 CHANGELOG.md 中详细记录。每次发版都需手动更新 README 标题，容易遗漏或不同步。
- **替代方案**: 在 README 的 Features 表格或单独一行标注 "Latest: v0.5.3"，但标题本身不含版本号。
- **业界惯例**: 大多数开源项目 README 标题不含版本号（如 React、VSCode、rust 等）

### 2. ARCHITECTURE.md — **建议移除**

- **现状**: 标题含 `v0.5.3`
- **理由**: 架构文档描述的是当前代码结构，天然与最新版本一致。标题中的版本号是冗余的——用户看到的是最新代码，架构文档就是最新的。如果架构有重大变更，CHANGELOG 会记录。
- **业界惯例**: 架构文档通常不含版本号，因为它们始终描述当前状态

### 3. AGENTS.md — **建议移除**

- **现状**: 标题含 `v0.5.3`
- **理由**: AGENTS.md 是 AI Agent 的操作手册，描述的是当前代码库的工作方式。它应该始终反映最新状态，标题版本号冗余。
- **副作用**: 移除后需同步更新 AGENTS.md 中 "Version Bump" 清单，减少版本升级时的手动修改项

### 4. FUTURE_ROADMAP.md — **建议保留但调整**

- **现状**: 标题含 `v0.5.3+`
- **理由**: Roadmap 的 `v0.5.3+` 有实际语义——表示"从 v0.5.3 开始的后续计划"。但这个信息在文档内容中已有体现（Completed 部分列出了到 v0.5.3 为止的所有完成项）。
- **建议**: 可以移除标题中的版本号，因为文档内容已自解释。如果想保留，改为 `v0.5.3+` 仅表示"当前版本之后的计划"。

### 5. CHANGELOG.md — **无需改动（已无版本号）**

- 标题本身就是 `# Changelog`，版本号在内容中的各版本标题里，这是正确的做法。

### 6. Audio_Settings_Guide.md — **无需改动（已无版本号）**

- 功能指南类文档，与版本无关。

### 7. android_app/README.md — **无需改动（已无版本号）**

- 子项目说明文档，与版本无关。

---

## 方案：移除文档标题版本号

### 改动清单

| 文件 | 当前标题 | 新标题 | 涉及语言 |
|------|---------|--------|----------|
| README.md (EN) | `# AudioSource Win (Raw WASAPI) v0.5.3` | `# AudioSource Win (Raw WASAPI)` | EN |
| README.md (ZH) | `# AudioSource Win (Raw WASAPI) v0.5.3` | `# AudioSource Win (Raw WASAPI)` | ZH |
| ARCHITECTURE.md (EN) | `# Architecture -- v0.5.3` | `# Architecture` | EN |
| ARCHITECTURE.md (ZH) | `# 架构说明 — v0.5.3` | `# 架构说明` | ZH |
| AGENTS.md (EN) | `# AGENTS.md -- v0.5.3` | `# AGENTS.md` | EN |
| AGENTS.md (ZH) | `# AGENTS.md — v0.5.3` | `# AGENTS.md` | ZH |
| FUTURE_ROADMAP.md (EN) | `# Future Roadmap -- v0.5.3+` | `# Future Roadmap` | EN |
| FUTURE_ROADMAP.md (ZH) | `# 未来路线图 — v0.5.3+` | `# 未来路线图` | ZH |

### AGENTS.md Version Bump 清单更新

移除第 2-5 项（文档标题版本号），简化为：

| # | File | Location/Line | Format |
|---|------|--------------|--------|
| 1 | `src/version.h` | `#define APP_VERSION` | `"x.y.z"` |
| 2 | `android_app/app/build.gradle` | `versionName` | Sync `APP_VERSION` string |
| 3 | `android_app/app/build.gradle` | `versionCode` | +1 |

### 实施步骤

1. 修改 4 个英文文档标题（README, ARCHITECTURE, AGENTS, FUTURE_ROADMAP）
2. 修改 4 个中文文档标题（同上）
3. 更新 AGENTS.md (EN) 的 Version Bump 清单
4. 更新 AGENTS.md (ZH) 的版本号更新清单
5. 验证所有文件标题一致性
