# 🎵 Vacuum Music Player
---

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL-V3)](https://www.gnu.org/licenses/gpl-3.0.html)

**Vacuum Music Player** 是一个**轻量级、高性能**的本地音乐播放器，采用 Visual C++ 构建，图形由ImGui库搭建，基于 miniaudio 音频引擎。支持常见音频格式，内置歌词显示、专辑封面、系统媒体控件（SMTC）以及与 AMLL 服务的 WebSocket 同步。支持远程投射到SONOS音响。支持亚克力效果。

---

## ✨ 特性

- 🎶 **音频格式支持**：MP3、FLAC、WAV ~~、OGG、M4A、AAC~~（由于Mini Audio不支持 所以这几种音频格式暂不支持）【FLAC测试3219kbps 96kHZ音频通过】
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

Apple Music Like Lyrics（AMLL）服务端（可接受本程序提供的连接AMLL播放器，放在副屏或者其他设备上提供更好的歌词显示功能）：[amll-dev/amll-player: Repo for standalone AMLL Player](https://github.com/amll-dev/amll-player)

---
## 运行说明
本程序有丰富的功能，为了~~防止你把本程序搞丢~~让你更好的使用程序，这里提供一些快捷键参考：

`ctrl+alt+l`用于显示或者隐藏窗口

`ctrl+alt+>`下一首

`ctrl+alt+<`上一首

`ctrl+alt+/`暂停音乐

`ctrl+alt+x`**退出程序**

目前暂不支持修改快捷键

---
## 🛠️ 构建指南

### 环境要求（编辑项目）

- **Visual Studio 2026**（或 2022） C++ Windows SDK
- **Windows 10 / 11**（64 位）
- Git（用于克隆仓库）

### 仅运行需求
若想获得更好的兼容性，可以使用我之前的版本（Windows 7 x64以上，Linux支持（Maybe），Python语言）->[lazymonkey666/Vacuum-Music-Controller-2: A simple music controller for Windows 10/11.](https://github.com/lazymonkey666/Vacuum-Music-Controller-2)
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


## 说明：本程序是人和AI一起完成的，但是因为时间优先，不能很好的维护仓库，代码上有不妥请见谅。