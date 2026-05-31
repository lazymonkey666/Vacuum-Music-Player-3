//albumimgtagreader.h
#pragma once
#include <vector>
#include <string>

std::vector<unsigned char> ExtractAlbumArt(const std::wstring& filePath);
std::vector<unsigned char> ConvertImageToJPEG(const std::vector<unsigned char>& inputImageData);
std::vector<unsigned char> ProcessAlbumArtWithGradient(const std::vector<unsigned char>& imageData, int targetWidth, int targetHeight);
std::string GetLyricsFromFile(const std::wstring& filePath);      // 从 ID3 读取
std::wstring FindLrcFile(const std::wstring& musicFilePath);    // 查找 .lrc 文件（同目录或子目录）