//IconManager.h
#pragma once

#include <platform/platform.h>

#include <string>
#include <unordered_map>
#include <vector>

class IconManager {
public:
    static IconManager& GetInstance() {
        static IconManager instance;
        return instance;
    }

    // 初始化（不再需要 D3D11 设备）
    void Initialize();

    // 加载一个单色 SVG 图标
    TextureHandle LoadIcon(const std::string& svgPath,
        int width, int height,
        uint32_t color = 0xFFFFFF);

    // 获取已缓存图标
    TextureHandle GetIcon(const std::string& svgPath, uint32_t color);

    // 主题切换
    void SetThemeColor(uint32_t color);

    // 释放所有纹理
    void Shutdown();

private:
    struct IconCacheEntry {
        TextureHandle texture = nullptr;      // ← ID3D11ShaderResourceView* → TextureHandle
        int width = 0;
        int height = 0;
        uint32_t lastColor = 0;               // ← DWORD → uint32_t
        std::string svgPath;
    };

    std::unordered_map<std::string, IconCacheEntry> m_cache;
    uint32_t m_currentThemeColor = 0xFFFFFF;

    TextureHandle LoadIconWithColor(const std::string& svgPath,
        int width, int height,
        uint32_t color);
};