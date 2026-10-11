#pragma once
#include <string>

// 获取嵌入的 SVG 内容。找不到返回 nullptr。
// name 是文件名去扩展名（例如 "play" 对应 resources/play.svg）
const std::string* GetEmbeddedResource(const std::string& name);