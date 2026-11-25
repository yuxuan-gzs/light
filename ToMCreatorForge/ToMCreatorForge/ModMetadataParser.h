// ModMetadataParser.h
#pragma once
#include <string>
#include <map>
#include <vector>

struct ModMetadata {
    std::string modId;
    std::string modName;
    std::string version;
    std::string mcVersion;
    std::string description;
    std::string author;
    std::vector<std::string> dependencies;
};

struct RegisteredItem {
    std::string name;
    std::string className;
    std::string registryName;
    bool isBlock;
    bool hasModel;
};

struct RegisteredRecipe {
    std::string name;
    std::string type; // shaped, shapeless, smelting
    std::string output;
    int outputCount;
    std::vector<std::string> ingredients;
};

class ModMetadataParser {
public:
    // 解析 mods.toml 文件
    static bool ParseModsToml(const std::string& tomlPath, ModMetadata& metadata);

    // 解析 Java 源码中的注册信息
    static std::vector<RegisteredItem> ParseRegistries(const std::string& javaContent);

    // 解析配方
    static std::vector<RegisteredRecipe> ParseRecipes(const std::string& javaContent);

    // 提取 @Mod 主类信息
    static std::string ExtractMainModClass(const std::string& javaContent);

private:
    static std::string Trim(const std::string& str);
    static std::string ExtractQuoted(const std::string& str);
};
