// albumimgtagreader.h
#pragma once

#include <platform/platform.h>

#include <string>
#include <vector>

// ─── 专辑封面读取 ───
// 从音频文件中提取封面原始数据（JPEG/PNG 字节流）
// 支持 .mp3 / .flac / .m4a / .aac
std::vector<unsigned char> ExtractAlbumArt(const PathType& filePath);

// ─── 歌词读取 ───
// 从 ID3v2 (USLT) 或 FLAC (Xiph LYRICS/UNSYNCEDLYRICS) 标签读取歌词
std::string GetLyricsFromFile(const PathType& filePath);

// 查找同名 .lrc 文件（同目录或 lyrics 子目录）
// 找不到返回空 PathType
PathType FindLrcFile(const PathType& musicFilePath);

// ─── 封面图像处理 ───
// 解码 + 缩放 + 方向性模糊 + 右侧 alpha 渐变
// 输入：任意格式的封面原始数据
// 输出：targetWidth × targetHeight 的 RGBA 像素数据
std::vector<unsigned char> ProcessAlbumArtWithGradient(
    const std::vector<unsigned char>& imageData,
    int targetWidth,
    int targetHeight);