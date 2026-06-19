//IconManager.h
#pragma once
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <vector>




class IconManager {
public:
    static IconManager& GetInstance() {
        static IconManager instance;
        return instance;
    }

    // 初始化（传入 D3D 设备）
    void Initialize(ID3D11Device* device);

    // 加载一个单色 SVG 图标（文件路径，期望的渲染尺寸，颜色 0xRRGGBB 格式）
    // 返回纹理 ID，如果失败返回 nullptr
    ID3D11ShaderResourceView* LoadIcon(const std::string& svgPath, int width, int height, DWORD color = 0xFFFFFF);

    // 获取已加载的图标纹理（如果已加载，且颜色匹配缓存）
    ID3D11ShaderResourceView* GetIcon(const std::string& svgPath, DWORD color);

    // 主题切换时重新生成所有已加载的图标（根据新主题颜色）
    void SetThemeColor(DWORD color);

    // 释放所有纹理
    void Shutdown();

private:
    ID3D11Device* m_device = nullptr;
    struct IconCacheEntry {
        ID3D11ShaderResourceView* texture = nullptr;
        int width = 0;
        int height = 0;
        DWORD lastColor = 0;
        std::string svgPath;
    };
    std::unordered_map<std::string, IconCacheEntry> m_cache;
    DWORD m_currentThemeColor = 0xFFFFFF; // 默认白色（深色主题用）

    // 内部加载（指定颜色）
    ID3D11ShaderResourceView* LoadIconWithColor(const std::string& svgPath, int width, int height, DWORD color);
};