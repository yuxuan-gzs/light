// PluginTemplateGenerator.h
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

class PluginTemplateGenerator {
public:
    struct TemplateConfig {
        std::string pluginName;      // 插件名称
        std::string pluginId;        // 插件ID (如: com.example.myplugin)
        std::string version;         // 版本号
        std::string author;          // 作者
        std::string description;     // 描述
        std::string outputDir;
    };

    // 生成完整的插件模板项目（JS / 通用 .zip 插件）
    static bool GenerateTemplate(const TemplateConfig& config);

    // 生成 api_*.dll 新增指令集模板（输出目录工程，不打包 ZIP）
    static bool GenerateApiTemplate(const TemplateConfig& config);

private:
    // 生成各个文件
    static bool GeneratePluginJson(const std::string& outputDir, const TemplateConfig& config);
    static bool GeneratePluginCpp(const std::string& outputDir, const TemplateConfig& config);
    static bool GeneratePluginH(const std::string& outputDir, const TemplateConfig& config);
    static bool GenerateCMakeLists(const std::string& outputDir, const TemplateConfig& config);
    static bool GenerateReadme(const std::string& outputDir, const TemplateConfig& config);
    static bool GenerateBuildScript(const std::string& outputDir, const TemplateConfig& config);
    static bool GenerateResourceFiles(const std::string& outputDir, const TemplateConfig& config);

    // 创建目录结构
    static bool CreateDirectoryStructure(const std::string& baseDir);

    // 打包成 ZIP
    static bool PackToZip(const std::string& sourceDir, const std::string& zipPath);
};