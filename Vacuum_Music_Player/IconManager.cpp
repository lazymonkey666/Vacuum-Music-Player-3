#define NOMINMAX
#include "platform/platform.h"
#include "IconManager.h"
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include <cstdio>       // ← 为了 snprintf
#include <cmath>        // ← 为了 powf

#include "embedded_resources.h"


void IconManager::Initialize() {//空实现
}

void IconManager::Shutdown() {
    for (auto& pair : m_cache) {
        if (pair.second.texture) {
            ReleaseTexture(pair.second.texture);
        }
    }
    m_cache.clear();
}

// 辅助函数：替换 fill="currentColor" 或 fill="#任意"
static std::string ReplaceFillColor(const std::string& svgContent, uint32_t color) {
    char colorStr[8];
    snprintf(colorStr, sizeof(colorStr), "#%02X%02X%02X",
             (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
    std::string result = svgContent;

    // 替换所有 fill="currentColor"
    const std::string target = "fill=\"currentColor\"";
    size_t pos = 0;
    while ((pos = result.find(target, pos)) != std::string::npos) {
        result.replace(pos, target.length(),
                       "fill=\"" + std::string(colorStr) + "\"");
        pos += strlen(colorStr) + 7;   // "fill=\"" + "#XXXXXX" + "\""
    }

    // 替换所有 fill="#XXXXXX"
    const std::string pattern = "fill=\"#";
    pos = 0;
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        size_t end = result.find("\"", pos + 7);
        if (end == std::string::npos) break;
        result.replace(pos + 6, end - pos - 6, colorStr + 1);
        pos += strlen(colorStr);
    }

    return result;
}
#if PLATFORM_WINDOWS
TextureHandle IconManager::LoadIconWithColor(const std::string& svgPath, int width, int height, uint32_t color) {

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

    TextureHandle texture = CreateTextureFromRGBA(imgData, width, height);
    return texture;
}
#endif
#if PLATFORM_LINUX
//程序内嵌资源
TextureHandle IconManager::LoadIconWithColor(const std::string& svgPath,int width, int height, uint32_t color) {
    // ─── 从 "resources/play.svg" 提取 "play" ───
    std::string name = svgPath;
    const std::string prefix = "resources/";
    const std::string suffix = ".svg";
    if (name.compare(0, prefix.size(), prefix) == 0)
    name = name.substr(prefix.size());
    if (name.size() > suffix.size() &&
    name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    name = name.substr(0, name.size() - suffix.size());

    // ─── 从嵌入资源里取内容 ───
    const std::string* svgContentPtr = GetEmbeddedResource(name);
    if (!svgContentPtr) {
    OutputDebugStringA(("[icon] embedded resource not found: " + name + "\n").c_str());
    return nullptr;
    }
    std::string svgContent = *svgContentPtr;

    // ─── 替换颜色 ───
    std::string modifiedSvg = ReplaceFillColor(svgContent, color);

    // ─── 之后的 nanosvg 解析、光栅化、纹理创建 ───
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

    unsigned char targetR = (color >> 16) & 0xFF;
    unsigned char targetG = (color >> 8)  & 0xFF;
    unsigned char targetB = color & 0xFF;
    for (int i = 0; i < width * height; ++i) {
    unsigned char* pixel = imgData.data() + i * 4;
    if (pixel[3] > 0) {
    pixel[0] = targetR;
    pixel[1] = targetG;
    pixel[2] = targetB;
    float a = pixel[3] / 255.0f;
    a = powf(a, 0.85f);
    pixel[3] = (unsigned char)(a * 255);
    } else {
    pixel[0] = pixel[1] = pixel[2] = 0;
    }
    }
    nsvgDeleteRasterizer(rast);
    nsvgDelete(nsvgImage);

    TextureHandle texture = CreateTextureFromRGBA(imgData, width, height);
    return texture;
}

#endif

TextureHandle IconManager::LoadIcon(const std::string& svgPath, int width, int height, uint32_t color) {
    std::string cacheKey = svgPath + "_" + std::to_string(color);
    auto it = m_cache.find(cacheKey);
    if (it != m_cache.end() && it->second.texture) {
        return it->second.texture;
    }
    TextureHandle tex = LoadIconWithColor(svgPath, width, height, color);
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

TextureHandle IconManager::GetIcon(const std::string& svgPath, uint32_t color) {
    std::string cacheKey = svgPath + "_" + std::to_string(color);
    auto it = m_cache.find(cacheKey);
    if (it != m_cache.end()) return it->second.texture;
    return nullptr;
}

void IconManager::SetThemeColor(uint32_t color) {
    if (m_currentThemeColor == color) return;
    m_currentThemeColor = color;
    for (auto& pair : m_cache) {
        if (pair.second.texture) {
            ReleaseTexture(pair.second.texture);
        }
    }
    m_cache.clear();
}