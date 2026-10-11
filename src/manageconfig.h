#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

class ManageConfig {
public:
    // 获取单例实例
    static ManageConfig& GetInstance();

    // 禁止拷贝和赋值
    ManageConfig(const ManageConfig&) = delete;
    ManageConfig& operator=(const ManageConfig&) = delete;

    // 加载配置（若文件损坏则尝试修复，失败则重置）
    bool Load();

    // 保存当前配置到文件
    bool Save() const;

    // 获取整个 JSON 对象（只读）
    const nlohmann::json& GetJson() const { return data_; }

    // 设置键值对（支持任意可序列化类型）
    template<typename T>
    void SetValue(const std::string& key, const T& value) {
        data_[key] = value;
    }

    // 获取值，若不存在则返回默认值
    template<typename T>
    T GetValue(const std::string& key, const T& defaultValue = T{}) const {
        if (data_.contains(key)) {
            return data_[key].get<T>();
        }
        return defaultValue;
    }

    // 判断键是否存在
    bool HasKey(const std::string& key) const;

    // 删除键
    bool RemoveKey(const std::string& key);

    // 获取所有键
    std::vector<std::string> GetAllKeys() const;

    // 清空所有配置
    void Clear();

    // 获取配置文件路径
    std::filesystem::path GetConfigPath() const { return config_path_; }

private:
    ManageConfig();
    ~ManageConfig() = default;

    // 尝试从损坏的 JSON 内容中恢复有效数据
    nlohmann::json TryRecoverFromCorrupted(const std::string& content);

    // 备份损坏的文件
    void BackupCorruptedFile();

    std::filesystem::path config_path_;
    nlohmann::json data_;
};