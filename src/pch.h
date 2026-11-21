#pragma once
#define NOMINMAX 

// ── 平台 ──
#include <platform/platform.h>

// ── STL ──
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <random>
#include <cstdint>
#include <cmath>
#include <regex>
#include <future>
#include <queue>

// ── 第三方（注意：不含 miniaudio.h）──
#include <imgui/imgui.h>
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
