#define NOMINMAX
#include "IconManager.h"
#include "texture_utils.h"  // 确保提供了 CreateTextureFromRGBA 的声明
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

extern ID3D11ShaderResourceView* CreateTextureFromRGBA(const std::vector<unsigned char>& rgbaData, int width, int height, ID3D11Device* device);



void IconManager::Initialize(ID3D11Device* device) {
    m_device = device;
}

void IconManager::Shutdown() {
    for (auto& pair : m_cache) {
        if (pair.second.texture) {
            pair.second.texture->Release();
        }
    }
    m_cache.clear();
}

// 辅助函数：替换 fill="currentColor" 或 fill="#任意"
static std::string ReplaceFillColor(const std::string& svgContent, DWORD color) {
    char colorStr[8];
    snprintf(colorStr, sizeof(colorStr), "#%02X%02X%02X", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
    std::string result = svgContent;
    std::string target = "fill=\"currentColor\"";
    size_t pos = result.find(target);
    if (pos != std::string::npos) {
        result.replace(pos, target.length(), "fill=\"" + std::string(colorStr) + "\"");
    }
    else {
        // 尝试替换 fill="#任意6位"
        std::string pattern = "fill=\"#";
        pos = result.find(pattern);
        if (pos != std::string::npos) {
            size_t end = result.find("\"", pos + 7);
            if (end != std::string::npos) {
                result.replace(pos + 6, end - pos - 6, colorStr + 1); // 跳过 '#'
            }
        }
    }
    return result;
}

ID3D11ShaderResourceView* IconManager::LoadIconWithColor(const std::string& svgPath, int width, int height, DWORD color) {
    if (!m_device) return nullptr;

    // 读取文件
    std::ifstream file(svgPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return nullptr;
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size + 1);
    if (!file.read(buffer.data(), size)) return nullptr;
    buffer[size] = '\0';
    std::string svgContent(buffer.data(), size);
    // 修改颜色
    std::string modifiedSvg = ReplaceFillColor(svgContent, color);

    // 准备可修改的缓冲区
    std::vector<char> mutableBuffer(modifiedSvg.begin(), modifiedSvg.end());
    mutableBuffer.push_back('\0');

    NSVGimage* nsvgImage = nsvgParse(mutableBuffer.data(), "px", 96.0f);
    if (!nsvgImage) return nullptr;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    float scaleX = (float)width / nsvgImage->width;
    float scaleY = (float)height / nsvgImage->height;
    float scale = (std::min)(scaleX, scaleY);
    float tx = (width - nsvgImage->width * scale) * 0.5f;
    float ty = (height - nsvgImage->height * scale) * 0.5f;

    std::vector<unsigned char> imgData(width * height * 4, 0);
    nsvgRasterize(rast, nsvgImage, tx, ty, scale, imgData.data(), width, height, width * 4);
    // 栅格化后，处理像素数据
    unsigned char targetR = (color >> 16) & 0xFF;
    unsigned char targetG = (color >> 8) & 0xFF;
    unsigned char targetB = color & 0xFF;

    for (int i = 0; i < width * height; ++i) {
        unsigned char* pixel = imgData.data() + i * 4;
        // 原始 RGBA 中，RGB 已经是填充色（但可能因抗锯齿而有偏差）
        if (pixel[3] > 0) {
            // 强制 RGB 为目标纯色
            pixel[0] = targetR;
            pixel[1] = targetG;
            pixel[2] = targetB;

            // 调整 alpha：提高对比度，减少边缘透明度对颜色的稀释
            // 使用幂函数，指数 <1 使半透明区域更偏向不透明，指数 >1 则更透明
            // 建议 0.7 ~ 0.9 之间，越大保留越多的透明渐变
            float a = pixel[3] / 255.0f;
            a = powf(a, 0.85f);   // 指数可以调节，0.85 保留一定抗锯齿但颜色更纯
            pixel[3] = (unsigned char)(a * 255);
        }
        else {
            // 完全透明的像素，确保 RGB 为 0（可选）
            pixel[0] = 0; pixel[1] = 0; pixel[2] = 0;
        }
    }
    nsvgDeleteRasterizer(rast);
    nsvgDelete(nsvgImage);

    ID3D11ShaderResourceView* texture = CreateTextureFromRGBA(imgData, width, height, m_device);
    return texture;
}

ID3D11ShaderResourceView* IconManager::LoadIcon(const std::string& svgPath, int width, int height, DWORD color) {
    std::string cacheKey = svgPath + "_" + std::to_string(color);
    auto it = m_cache.find(cacheKey);
    if (it != m_cache.end() && it->second.texture) {
        return it->second.texture;
    }
    ID3D11ShaderResourceView* tex = LoadIconWithColor(svgPath, width, height, color);
    if (tex) {
        IconCacheEntry entry;
        entry.texture = tex;
        entry.width = width;
        entry.height = height;
        entry.lastColor = color;
        entry.svgPath = svgPath;
        m_cache[cacheKey] = entry;
    }
    return tex;
}

ID3D11ShaderResourceView* IconManager::GetIcon(const std::string& svgPath, DWORD color) {
    std::string cacheKey = svgPath + "_" + std::to_string(color);
    auto it = m_cache.find(cacheKey);
    if (it != m_cache.end()) return it->second.texture;
    return nullptr;
}

void IconManager::SetThemeColor(DWORD color) {
    if (m_currentThemeColor == color) return;
    m_currentThemeColor = color;
    for (auto& pair : m_cache) {
        if (pair.second.texture) {
            pair.second.texture->Release();
        }
    }
    m_cache.clear();
}