#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <set>
#include <regex>
#include <future>
#include <miniaudio.h>
#include <platform/platform.h>

// TagLib 全家桶
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/id3v2framefactory.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>
#include <taglib/flacpicture.h>
#include <AppState.h>

std::string Base64Encode(const std::string& input);
std::string Base64Decode(const std::string& input);
std::vector<std::pair<int, std::string>> ParseLrc(const std::string& lrcContent);
std::string FormatTime(uint64_t milliseconds);
std::string U8StringToString(const std::u8string& u8str);
MusicMetadata GetMusicMetadata(const std::string& filepath_utf8);
PathType GetCacheDirectory();