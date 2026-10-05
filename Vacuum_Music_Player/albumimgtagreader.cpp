// albumimgtagreader.cpp
#include "albumimgtagreader.h"
#include <platform/platform.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4item.h>
#include <taglib/id3v2frame.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/fileref.h>
#include <taglib/xiphcomment.h>
#include <taglib/attachedpictureframe.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>
#include <filesystem>

// 预计算高斯权重表（由于半径随 x 变化，最多 81 种半径）
static std::vector<float> ComputeGaussianWeights(float radius) {
    if (radius < 1.0f) return { 1.0f };
    float sigma = radius / 3.0f;
    int kernelSize = (int)(sigma * 6) + 1;  // 覆盖 6σ
    int half = kernelSize / 2;
    std::vector<float> weights(kernelSize);
    float sum = 0.0f;
    for (int i = 0; i < kernelSize; ++i) {
        float x = static_cast<float>(i - half);
        float w = expf(-(x * x) / (2.0f * sigma * sigma));
        weights[i] = w;
        sum += w;
    }
    for (float& w : weights) w /= sum;
    return weights;
}

// 方向性变半径模糊（direction: 0=水平, 1=垂直）
static void VariableDirectionalBlur(std::vector<unsigned char>& pixels, int width, int height, bool vertical) {
    std::vector<unsigned char> result(pixels.size());
    const float maxRadius = 80.0f;
    const float fadeEnd = width * 0.55f;  // 172.5 像素

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // 计算当前像素的模糊半径（基于 x 坐标）
            float r = 0.0f;
            if (x <= fadeEnd) {
                r = maxRadius * (1.0f - (float)x / fadeEnd);
                if (r < 0.0f) r = 0.0f;
            }
            else {
                r = 0.0f;
            }

            if (r < 1.0f) {
                // 无需模糊，直接复制原像素
                int idx = (y * width + x) * 4;
                result[idx] = pixels[idx];
                result[idx + 1] = pixels[idx + 1];
                result[idx + 2] = pixels[idx + 2];
                result[idx + 3] = pixels[idx + 3];
                continue;
            }

            // 获取该半径的高斯权重表
            auto weights = ComputeGaussianWeights(r);
            int kernelSize = (int)weights.size();
            int half = kernelSize / 2;

            float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumA = 0.0f;
            float totalWeight = 0.0f;

            if (!vertical) {
                // 水平模糊
                int startX = (std::max)(0, x - half);
                int endX = (std::min)(width - 1, x + half);
                for (int sx = startX; sx <= endX; ++sx) {
                    int weightIdx = sx - (x - half);
                    if (weightIdx < 0 || weightIdx >= kernelSize) continue;
                    float w = weights[weightIdx];
                    int idx = (y * width + sx) * 4;
                    sumR += pixels[idx] * w;
                    sumG += pixels[idx + 1] * w;
                    sumB += pixels[idx + 2] * w;
                    sumA += pixels[idx + 3] * w;
                    totalWeight += w;
                }
            }
            else {
                // 垂直模糊
                int startY = ((std::max))(0, y - half);
                int endY = (std::min)(height - 1, y + half);
                for (int sy = startY; sy <= endY; ++sy) {
                    int weightIdx = sy - (y - half);
                    if (weightIdx < 0 || weightIdx >= kernelSize) continue;
                    float w = weights[weightIdx];
                    int idx = (sy * width + x) * 4;
                    sumR += pixels[idx] * w;
                    sumG += pixels[idx + 1] * w;
                    sumB += pixels[idx + 2] * w;
                    sumA += pixels[idx + 3] * w;
                    totalWeight += w;
                }
            }

            if (totalWeight > 0.0f) {
                int outIdx = (y * width + x) * 4;
                result[outIdx] = (unsigned char)(sumR / totalWeight);
                result[outIdx + 1] = (unsigned char)(sumG / totalWeight);
                result[outIdx + 2] = (unsigned char)(sumB / totalWeight);
                result[outIdx + 3] = (unsigned char)(sumA / totalWeight);
            }
            else {
                // 回退
                int idx = (y * width + x) * 4;
                result[idx] = pixels[idx];
                result[idx + 1] = pixels[idx + 1];
                result[idx + 2] = pixels[idx + 2];
                result[idx + 3] = pixels[idx + 3];
            }
        }
    }
    pixels = std::move(result);
}


// 新增函数：应用渐变透明效果
std::vector<unsigned char> ProcessAlbumArtWithGradient(const std::vector<unsigned char>& imageData, int targetWidth, int targetHeight) {
    // 1. 解码并缩放至目标尺寸
    std::vector<unsigned char> pixelData = DecodeAndScaleImage(imageData, targetWidth, targetHeight);
    if (pixelData.empty()) return {};

    VariableDirectionalBlur(pixelData, targetWidth, targetHeight, false);
    VariableDirectionalBlur(pixelData, targetWidth, targetHeight, true);

    // 3. 应用 alpha 渐变透明，使用 smoothstep 曲线，并增加一个起始偏移（前 10% 完全透明）
    const float startFade = 0.05f;    // 前 5% 宽度完全透明
    const float endFade = 0.75f;      // 75% 处完全不透明
    for (int y = 0; y < targetHeight; ++y) {
        for (int x = 0; x < targetWidth; ++x) {
            size_t idx = (y * targetWidth + x) * 4 + 3;
            float t = static_cast<float>(x) / targetWidth;  // 0..1
            float alpha = 0.0f;
            if (t <= startFade) {
                alpha = 0.0f;
            }
            else if (t >= endFade) {
                alpha = 255.0f;
            }
            else {
                // 在 [startFade, endFade] 之间平滑插值，使用 smoothstep
                float s = (t - startFade) / (endFade - startFade);
                // smoothstep: 3*s^2 - 2*s^3
                float smooth = s * s * (3.0f - 2.0f * s);
                alpha = 255.0f * smooth;
            }
            pixelData[idx] = static_cast<unsigned char>(alpha);
        }
    }

    return pixelData;
}


// 获取扩展名（小写）
static std::string GetExtension(const PathType& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);   // ← 去掉前导点
    return ext;
}

// ---------- MP3 ----------
static std::vector<unsigned char> ExtractFromMP3(const PathType& widePath) {
    TagLib::MPEG::File file(widePath.c_str());
    if (!file.isValid() || !file.ID3v2Tag()) return {};

    auto* tag = file.ID3v2Tag();
    auto frames = tag->frameListMap()["APIC"];
    if (frames.isEmpty()) return {};

    // 优先取 FrontCover，否则取第一个 APIC 帧
    for (auto* frame : frames) {
        auto* pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame);
        if (pic && pic->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
            TagLib::ByteVector data = pic->picture();
            return std::vector<unsigned char>(data.begin(), data.end());
        }
    }
    // 如果没有 FrontCover，返回第一个 APIC 帧
    if (!frames.isEmpty()) {
        auto* firstPic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
        if (firstPic) {
            TagLib::ByteVector data = firstPic->picture();
            return std::vector<unsigned char>(data.begin(), data.end());
        }
    }
    return {};
}

// ---------- FLAC ----------
static std::vector<unsigned char> ExtractFromFLAC(const PathType& widePath) {
    TagLib::FLAC::File file(widePath.c_str());
    if (!file.isValid()) return {};

    auto pictures = file.pictureList();
    if (pictures.isEmpty()) return {};

    // 优先取 FrontCover
    for (auto* pic : pictures) {
        if (pic->type() == TagLib::FLAC::Picture::FrontCover) {
            TagLib::ByteVector data = pic->data();
            return std::vector<unsigned char>(data.begin(), data.end());
        }
    }
    // 否则取第一张
    TagLib::ByteVector data = pictures[0]->data();
    return std::vector<unsigned char>(data.begin(), data.end());
}

// ---------- M4A / MP4 ----------
static std::vector<unsigned char> ExtractFromM4A(const PathType& widePath) {
    TagLib::MP4::File file(widePath.c_str());
    if (!file.isValid() || !file.tag()) return {};

    TagLib::MP4::Tag* tag = file.tag();

#if defined(TAGLIB_MAJOR_VERSION) && TAGLIB_MAJOR_VERSION >= 2
    auto items = tag->itemMap();
    auto it = items.find("covr");
    if (it == items.end()) return {};
    TagLib::MP4::Item coverItem = it->second;
#else
    auto items = tag->itemListMap();
    if (!items.contains("covr")) return {};
    TagLib::MP4::Item coverItem = items["covr"];
#endif

    auto coverList = coverItem.toCoverArtList();
    if (coverList.isEmpty()) return {};
    TagLib::ByteVector data = coverList.front().data();
    return std::vector<unsigned char>(data.begin(), data.end());
}


static std::string ExtractLyricsFromFLAC(const PathType& widePath) {
    TagLib::FLAC::File file(widePath.c_str());
    if (!file.isValid()) return "";

    TagLib::Ogg::XiphComment* xiph = file.xiphComment();
    if (!xiph) return "";

    // Vorbis注释中歌词通常存储在 "LYRICS" 字段
    auto lyricsList = xiph->fieldListMap()["LYRICS"];
    if (!lyricsList.isEmpty()) {
        return lyricsList.front().to8Bit(true);
    }
    // 备选：有些软件也用 "UNSYNCEDLYRICS"
    lyricsList = xiph->fieldListMap()["UNSYNCEDLYRICS"];
    if (!lyricsList.isEmpty()) {
        return lyricsList.front().to8Bit(true);
    }
    return "";
}

std::string GetLyricsFromFile(const PathType& widePath) {
    std::string ext = GetExtension(widePath);

    // FLAC 走 Xiph 注释路径
    if (ext == "flac") {
        return ExtractLyricsFromFLAC(widePath);
    }

    // MP3 / 其他可能带ID3v2的格式
    TagLib::FileRef f(widePath.c_str());
    if (f.isNull() || !f.tag()) return "";

    TagLib::ID3v2::Tag* id3v2tag = nullptr;
    TagLib::MPEG::File* mpegFile = dynamic_cast<TagLib::MPEG::File*>(f.file());
    if (mpegFile) {
        id3v2tag = mpegFile->ID3v2Tag();
    }
    else {
        // 部分 FLAC 嵌入 ID3v2 的情况（可选保留）
        TagLib::FLAC::File* flacFile = dynamic_cast<TagLib::FLAC::File*>(f.file());
        if (flacFile) {
            id3v2tag = flacFile->ID3v2Tag();
        }
    }
    if (!id3v2tag) return "";

    TagLib::ID3v2::FrameList frames = id3v2tag->frameListMap()["USLT"];
    if (frames.isEmpty()) return "";

    for (auto* frame : frames) {
        auto* uslt = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame*>(frame);
        if (uslt && !uslt->text().isEmpty()) {
            return uslt->text().to8Bit(true);
        }
    }
    return "";
}
PathType FindLrcFile(const PathType& musicFilePath) {
    namespace fs = std::filesystem;
    fs::path musicPath(musicFilePath);
    fs::path musicDir = musicPath.parent_path();
    fs::path baseName = musicPath.stem();

    // 1. 同目录下找 .lrc / .LRC
    for (const char* ext : { ".lrc", ".LRC" }) {
        fs::path lrcPath = musicDir / baseName;
        lrcPath += ext;
        if (fs::exists(lrcPath)) {
            return lrcPath.native();   // fs::path → PathType（平台原生类型）
        }
    }

    // 2. 常见的歌词文件夹名（UTF-8 字面量，两平台通用）
    const std::vector<fs::path> lyricFolderNames = {
        fs::path("lyrics"),
        fs::path("Lyrics"),
        fs::path("LYRICS"),
        fs::path("歌词"),
        fs::path("LYRIC"),
        fs::path("Lyric"),
    };

    for (const auto& folderName : lyricFolderNames) {
        fs::path lyricDir = musicDir / folderName;
        if (!fs::exists(lyricDir) || !fs::is_directory(lyricDir)) {
            continue;
        }

        for (const auto& entry : fs::directory_iterator(lyricDir)) {
            if (!entry.is_regular_file()) continue;

            // 扩展名比较（小写）
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            if (ext != ".lrc") continue;

            // 主文件名比较
            if (entry.path().stem() == baseName) {
                return entry.path().native();
            }
        }
    }

    return {};   // 空 PathType
}
// ---------- 统一入口 ----------
std::vector<unsigned char> ExtractAlbumArt(const PathType& filePath) {
    std::string ext = GetExtension(filePath);
    if (ext == "mp3")   return ExtractFromMP3(filePath);
    if (ext == "flac")  return ExtractFromFLAC(filePath);
    if (ext == "m4a" || ext == "aac") return ExtractFromM4A(filePath);
    return {};
}