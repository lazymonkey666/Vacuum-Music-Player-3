#pragma once

#include <platform/platform.h>

#include "miniaudio.h"
#include <stdio.h>
#include <random>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <iostream>

// ImGui 核心
#include <imgui/imgui.h>

// 项目内部（逻辑层）
#include <TagReader.h>
#include <ManageConfig.h>
#include <AMLLWebSocketV2.h>
#include <SettingItem.h>
#include <NeteaseAPIManager.h>

#include <IconManager.h>
#include <ThemePack.h>

//模块化播放源
#include <MusicSource.h>

//模块化播放后端
#include <PlaybackBackend.h>
#include <AsyncBackendBase.h>

//模块化播放服务
#include <PlaybackService.h>

//异步事件管理器
#include <AsyncEventManager.h>

//加载sonos模块
#include <SonosService.h>

#include <PlaybackStateMachine.h>

#include <AppState.h>
#include <initialize.h>
#include <utils.h>
#include <MainUI.h>
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
void WriteMetadataToFile(const std::string& filePath,
    const std::string& title,
    const std::vector<std::string>& artist,
    const std::string& album,
    const std::string& coverPath,
    const std::string& lyricsText = "");
uint64_t GetCacheSize(const PathType& cacheDir);
void TrimCache(const PathType& cacheDir, uint64_t limitBytes);
void EnforceCacheLimit(uint64_t limitMB);
std::string DownloadAndProcessTrack(const PlaybackInfo& info);
uint64_t GetAudioFileDurationMs(const std::string& utf8Path);