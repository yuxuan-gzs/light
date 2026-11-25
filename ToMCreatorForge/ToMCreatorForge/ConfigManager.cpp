// ConfigManager.cpp
#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <windows.h>
#include <shlobj.h>

namespace fs = std::filesystem;

std::string ConfigManager::GetConfigPath() {
    char appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        fs::path configDir = fs::path(appDataPath) / "ForgeToMCreatorConverter";
        fs::create_directories(configDir);
        return (configDir / "config.ini").string();
    }
    return "config.ini";
}

bool ConfigManager::LoadConfig(AppConfig& config) {
    std::string configPath = GetConfigPath();
    if (!fs::exists(configPath)) {
        return false;
    }

    std::ifstream file(configPath);
    if (!file.is_open()) return false;

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        // 去除首尾空格
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // 跳过注释
        if (line[0] == '#' || line[0] == ';') continue;

        // 检测节标题
        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) {
                currentSection = line.substr(1, end - 1);
            }
            continue;
        }

        // 解析键值对
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        // 去除键和值的首尾空格
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        if (currentSection == "Paths") {
            if (key == "lastSource") config.lastSourcePath = value;
            else if (key == "lastOutput") config.lastOutputPath = value;
        }
        else if (currentSection == "Options") {
            if (key == "generateEvents") config.options.generateEvents = (value == "true");
            else if (key == "generateCaps") config.options.generateCaps = (value == "true");
            else if (key == "generateNetwork") config.options.generateNetwork = (value == "true");
            else if (key == "exportZip") config.options.exportZip = (value == "true");
            else if (key == "includeSource") config.options.includeSource = (value == "true");
            else if (key == "generateDocs") config.options.generateDocs = (value == "true");
        }
        else if (currentSection == "Recent") {
            if (key.find("path") != std::string::npos) {
                config.recentPaths.push_back(value);
            }
        }
    }

    file.close();
    return true;
}

bool ConfigManager::SaveConfig(const AppConfig& config) {
    std::string configPath = GetConfigPath();
    std::ofstream file(configPath);
    if (!file.is_open()) return false;

    file << "; ForgeToMCreatorConverter Configuration File\n";
    file << "; Generated automatically\n\n";

    file << "[Paths]\n";
    file << "lastSource=" << config.lastSourcePath << "\n";
    file << "lastOutput=" << config.lastOutputPath << "\n\n";

    file << "[Options]\n";
    file << "generateEvents=" << (config.options.generateEvents ? "true" : "false") << "\n";
    file << "generateCaps=" << (config.options.generateCaps ? "true" : "false") << "\n";
    file << "generateNetwork=" << (config.options.generateNetwork ? "true" : "false") << "\n";
    file << "exportZip=" << (config.options.exportZip ? "true" : "false") << "\n";
    file << "includeSource=" << (config.options.includeSource ? "true" : "false") << "\n";
    file << "generateDocs=" << (config.options.generateDocs ? "true" : "false") << "\n\n";

    file << "[Recent]\n";
    for (size_t i = 0; i < config.recentPaths.size() && i < 10; i++) {
        file << "path" << i << "=" << config.recentPaths[i] << "\n";
    }

    file.close();
    return true;
}