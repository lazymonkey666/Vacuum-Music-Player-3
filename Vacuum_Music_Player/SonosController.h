#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WIN32_WINNT 0x0A00

#include <WinSock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>

// 前置声明（不暴露 noson 细节给外部）
#include <sonosplayer.h>   // 需要这个来获得准确的 PlayerPtr 类型

// 给 UI 使用的设备条目
struct DeviceEntry {
    int index;              // 编号
    std::string name;       // 显示名称（IP 或设备名）
    std::string location;   // 连接用的 URL
};


class SonosController {
public:
    SonosController();
    ~SonosController();

    // 初始化 Winsock（必须首先调用）
    bool Initialize();

    // 同步搜索所有 Sonos 设备（内部并发扫描各网卡），返回设备列表
    std::vector<DeviceEntry> SearchDevices(int totalTimeoutSec = 10);

    // 连接到指定设备
    bool ConnectToDevice(const std::string& location);

    // 启动本地 HTTP 文件服务器（挂载音乐文件夹）
    bool StartHttpServer(const std::wstring& musicFolder, int port);
    void SetPlayMode();

    // 停止 HTTP 服务器
    void StopHttpServer();

    // 获取播放器（连接成功后才能用）
    NSROOT::PlayerPtr GetPlayer();

    // 播放指定文件名（只需文件名，程序自动拼 URL）
    bool PlayFile(const std::string& filename);

    // 列出音乐目录下所有支持的音频文件（返回 UTF-8 文件名）
    std::vector<std::string> ListMusicFiles();

    // 获取当前使用的本机 IP 和端口
    std::string GetLocalIP() const;
    int GetHttpPort() const;

    void SetPlayPos(int pos);

    int GetPosition();
    int GetSonosTrackDuration();

    // 停止所有资源
    void Stop();

private:
    // 内部实现细节隐藏
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};