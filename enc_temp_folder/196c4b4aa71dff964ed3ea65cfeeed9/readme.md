# 🎵 Vacuum Music Player
---

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://mit-license.org/)

**Vacuum Music Player** 是一个轻量级、高性能的本地音乐播放器，采用 C++ 和 ImGui 构建，基于 miniaudio 音频引擎。支持常见音频格式，内置歌词显示、专辑封面、系统媒体控件（SMTC）以及与 AMLL 服务的 WebSocket 同步。支持SONOS音响。

---

## ✨ 特性

- 🎶 **音频格式支持**：MP3、FLAC、WAV、OGG、M4A、AAC
- 📃 **歌词显示**：支持 LRC 文件以及嵌入在 ID3 标签中的歌词
- 🖼️ **专辑封面**：自动提取音频文件封面，并生成模糊渐变背景
- 🎛️ **系统媒体控件（SMTC）**：与 Windows 系统控制中心集成，支持键盘多媒体键
- 🌐 **AMLL WebSocket 同步**：实时发送歌曲信息、封面、进度和歌词到 AMLL 服务端
- ⚙️ **可配置设置**：主题（深色/浅色）、亚克力透明度、音乐文件夹等，设置保存在 `config.json`
- 🔍 **歌单搜索高亮**：快速定位歌曲
- 🔄 **播放模式**：顺序播放、单曲循环、随机播放
- 🖥️ **窗口行为**：边缘贴靠、隐藏动画、可拖动标题栏
- ⌨️ **全局热键**：支持后台控制
- 可连接SONOS音响（单设备）

---

## 📦 依赖库


[miniaudio](https://github.com/mackron/miniaudio) 

[Dear ImGui](https://github.com/ocornut/imgui) 

[nlohmann/json](https://github.com/nlohmann/json)

[yhirose/cpp-httplib: A C++ header-only HTTP/HTTPS server and client library](https://github.com/yhirose/cpp-httplib)

[janbar/noson: C++ library for accessing SONOS devices.](https://github.com/janbar/noson)

等等...

Windows SDK（WIC、D3D11）由 Visual Studio 提供。

Apple Music Like Lyrics（AMLL）服务端：

---

## 🛠️ 构建指南

### 环境要求（编辑项目）

- **Visual Studio 2026**（或 2022） C++ Windows SDK
- **Windows 10 / 11**（64 位）
- Git（用于克隆仓库）

### 仅运行需求
- **Windows 10 / 11**（64 位）
- 程序将会占用20MB（左右）的运行内存
- 程序会占用10MB的硬盘空间
- D3D11
### 项目结构
```
\-Vacuum_Music_Player（项目主文件夹）
|-Vacuum_Music_Player.sln（Visual Studio 解决方案文件）
|-其他库的文件（直接在此根目录下）

```

### 使用步骤

1. **克隆仓库**（如果尚未下载）
   ```bash
   git clone https://github.com/lazymonkey666/Vacuum-Music-Player-3.git
   ```
2. **打开解决方案** `Vacuum_Music_Player.sln`