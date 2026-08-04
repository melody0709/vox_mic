# 文档国际化方案

## 目标

将项目文档从中文默认改为英文默认，原中文版本移至 `/doc/zh-CN/`，中英版本互相引用链接，适应国际化。

## 涉及文件

| # | 当前文件 (中文) | 英文默认位置 | 中文归档位置 |
|---|----------------|-------------|-------------|
| 1 | `README.md` | `README.md` | `doc/zh-CN/README.md` |
| 2 | `CHANGELOG.md` | `CHANGELOG.md` | `doc/zh-CN/CHANGELOG.md` |
| 3 | `ARCHITECTURE.md` | `ARCHITECTURE.md` | `doc/zh-CN/ARCHITECTURE.md` |
| 4 | `Audio_Settings_Guide.md` | `Audio_Settings_Guide.md` | `doc/zh-CN/Audio_Settings_Guide.md` |
| 5 | `FUTURE_ROADMAP.md` | `FUTURE_ROADMAP.md` | `doc/zh-CN/FUTURE_ROADMAP.md` |
| 6 | `AGENTS.md` | `AGENTS.md` | `doc/zh-CN/AGENTS.md` |
| 7 | `android_app/README.md` | `android_app/README.md` | `doc/zh-CN/android_app/README.md` |

## 目录结构

```
vox_mic/
├── README.md                          ← 英文 (默认)
├── CHANGELOG.md                       ← 英文 (默认)
├── ARCHITECTURE.md                    ← 英文 (默认)
├── Audio_Settings_Guide.md            ← 英文 (默认)
├── FUTURE_ROADMAP.md                  ← 英文 (默认)
├── AGENTS.md                          ← 英文 (默认)
├── android_app/
│   └── README.md                      ← 英文 (默认)
└── doc/
    └── zh-CN/
        ├── README.md                  ← 中文版
        ├── CHANGELOG.md               ← 中文版
        ├── ARCHITECTURE.md            ← 中文版
        ├── Audio_Settings_Guide.md    ← 中文版
        ├── FUTURE_ROADMAP.md          ← 中文版
        ├── AGENTS.md                  ← 中文版
        └── android_app/
            └── README.md              ← 中文版
```

## 语言切换链接规范

### 英文版文件头部（根目录）

每个英文版 `.md` 文件顶部添加语言切换行：

```markdown
[简体中文](doc/zh-CN/README.md) | **English**
```

### 中文版文件头部（doc/zh-CN/）

每个中文版 `.md` 文件顶部添加语言切换行：

```markdown
**简体中文** | [English](../../README.md)
```

对于 `android_app/README.md` 的中文版，路径为 `../../../README.md`（因为多一层 `android_app/`）。

### 内部交叉引用更新

- 英文版文件间的相对链接保持不变（同目录）
- 中文版文件间的相对链接需要调整（同在 `doc/zh-CN/` 下，互相引用不变）
- 中文版引用源代码路径需加 `../../` 前缀（如 `../../src/main.cpp`）
- 中文版引用 `plan/` 目录需加 `../../plan/` 前缀

## 实施步骤

### Step 1: 创建 `doc/zh-CN/` 目录结构

创建目录 `doc/zh-CN/` 和 `doc/zh-CN/android_app/`。

### Step 2: 将现有中文文档复制到 `doc/zh-CN/`

将 7 个中文文档原样复制到对应位置：
- `README.md` → `doc/zh-CN/README.md`
- `CHANGELOG.md` → `doc/zh-CN/CHANGELOG.md`
- `ARCHITECTURE.md` → `doc/zh-CN/ARCHITECTURE.md`
- `Audio_Settings_Guide.md` → `doc/zh-CN/Audio_Settings_Guide.md`
- `FUTURE_ROADMAP.md` → `doc/zh-CN/FUTURE_ROADMAP.md`
- `AGENTS.md` → `doc/zh-CN/AGENTS.md`
- `android_app/README.md` → `doc/zh-CN/android_app/README.md`

### Step 3: 为中文版文件添加语言切换链接 + 修正路径

每个中文版文件：
1. 在标题下方添加 `**简体中文** | [English](<相对路径>)` 行
2. 修正所有源代码/项目文件的相对路径引用（加 `../../` 前缀）
3. 修正内部文档交叉引用路径

### Step 4: 创建英文版文档

将每个中文文档翻译为英文，覆盖写入根目录对应文件：
1. `README.md` — 英文翻译
2. `CHANGELOG.md` — 英文翻译
3. `ARCHITECTURE.md` — 英文翻译
4. `Audio_Settings_Guide.md` — 英文翻译
5. `FUTURE_ROADMAP.md` — 英文翻译
6. `AGENTS.md` — 英文翻译
7. `android_app/README.md` — 英文翻译

每个英文版文件在标题下方添加 `[简体中文](doc/zh-CN/<filename>) | **English**` 行。

### Step 5: 更新 `.gitignore`（如需要）

确保 `doc/zh-CN/` 目录不被忽略。

### Step 6: 验证

- 检查所有语言切换链接是否正确
- 检查中文版内部路径引用是否正确
- 检查英文版内部路径引用是否正确

## 翻译原则

1. **技术术语保留英文**：如 WASAPI、ADB、RNNoise、BiQuad、DSP 等
2. **代码路径/命令不翻译**：如 `src/main.cpp`、`build.bat`
3. **表格结构保持一致**：表头翻译为英文，内容对应翻译
4. **ASCII 流程图保留**：图内注释翻译为英文
5. **版本号/数值不翻译**：如 v0.5.3、~40ms
6. **链接路径适配**：英文版中内部链接指向同目录文件
