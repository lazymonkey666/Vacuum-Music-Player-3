#include "manageconfig.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <filesystem>

ManageConfig::ManageConfig()
    : config_path_(std::filesystem::current_path() / "config.json")
    , data_(nlohmann::json::object())
{
}

ManageConfig& ManageConfig::GetInstance() {
    static ManageConfig instance;
    return instance;
}

bool ManageConfig::Load() {
    if (!std::filesystem::exists(config_path_)) {
        data_ = nlohmann::json::object();
        return Save();
    }

    std::ifstream file(config_path_);
    if (!file.is_open()) {
        BackupCorruptedFile();
        data_ = nlohmann::json::object();
        return Save();
    }

    std::string content((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    try {
        data_ = nlohmann::json::parse(content);
        return true;
    }
    catch (const nlohmann::json::parse_error&) {
        // 尝试修复
        nlohmann::json recovered = TryRecoverFromCorrupted(content);
        if (!recovered.empty()) {
            data_ = recovered;
            return Save();
        }
        else {
            // 无法修复，备份并重置
            BackupCorruptedFile();
            data_ = nlohmann::json::object();
            return Save();
        }
    }
}

bool ManageConfig::Save() const {
    // 先写入临时文件，再重命名，保证原子性
    std::filesystem::path temp_path = config_path_;
    temp_path += ".tmp";

    std::ofstream file(temp_path);
    if (!file.is_open()) {
        return false;
    }
    file << data_.dump(4);  // 格式化输出，缩进4空格
    file.close();

    std::error_code ec;
    std::filesystem::rename(temp_path, config_path_, ec);
    if (ec) {
        // 重命名失败，尝试直接覆盖
        std::ofstream direct(config_path_);
        if (!direct.is_open()) return false;
        direct << data_.dump(4);
        direct.close();
        std::filesystem::remove(temp_path, ec);
    }
    return true;
}

bool ManageConfig::HasKey(const std::string& key) const {
    return data_.contains(key);
}

bool ManageConfig::RemoveKey(const std::string& key) {
    if (data_.contains(key)) {
        data_.erase(key);
        return true;
    }
    return false;
}

std::vector<std::string> ManageConfig::GetAllKeys() const {
    std::vector<std::string> keys;
    for (auto& [key, _] : data_.items()) {
        keys.push_back(key);
    }
    return keys;
}

void ManageConfig::Clear() {
    data_.clear();
}

nlohmann::json ManageConfig::TryRecoverFromCorrupted(const std::string& content) {
    nlohmann::json result = nlohmann::json::object();

    // 查找 "music_folder" 键
    std::string key = "\"music_folder\"";
    size_t pos = content.find(key);
    if (pos == std::string::npos) return result;

    // 查找冒号后的第一个双引号
    pos = content.find(':', pos);
    if (pos == std::string::npos) return result;
    pos = content.find('"', pos);
    if (pos == std::string::npos) return result;
    size_t start = pos + 1;
    size_t end = content.find('"', start);
    if (end == std::string::npos) return result;

    std::string folderPath = content.substr(start, end - start);
    result["music_folder"] = folderPath;
    return result;
}

void ManageConfig::BackupCorruptedFile() {
    if (!std::filesystem::exists(config_path_)) return;
    std::filesystem::path backup = config_path_;
    backup += ".backup";
    std::error_code ec;
    std::filesystem::rename(config_path_, backup, ec);
    if (ec) {
        std::filesystem::copy_file(config_path_, backup, ec);
        if (!ec) std::filesystem::remove(config_path_, ec);
    }
}