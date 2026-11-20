// ConfigManager.h
#pragma once
#include <string>
#include <vector>
#include <map>
#include "ConverterCore.h"

struct AppConfig {
    // 最近使用的路径
    std::string lastSourcePath;
    std::string lastOutputPath;
    std::vector<std::string> recentPaths;

    // 转换选项
    ConvertOptions options;

    // 窗口设置
    int windowWidth = 650;
    int windowHeight = 500;
    bool rememberPaths = true;
};

class ConfigManager {
public:
    static bool LoadConfig(AppConfig& config);
    static bool SaveConfig(const AppConfig& config);
    static std::string GetConfigPath();

private:
    static std::string EncodeBase64(const std::string& input);
    static std::string DecodeBase64(const std::string& input);
};
