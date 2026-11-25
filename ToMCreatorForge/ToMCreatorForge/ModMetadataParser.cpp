// ModMetadataParser.cpp
#include "ModMetadataParser.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

std::string ModMetadataParser::Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::string ModMetadataParser::ExtractQuoted(const std::string& str) {
    std::regex quoteRegex(R"(["']([^"']*)["'])");
    std::smatch match;
    if (std::regex_search(str, match, quoteRegex) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

bool ModMetadataParser::ParseModsToml(const std::string& tomlPath, ModMetadata& metadata) {
    if (!fs::exists(tomlPath)) {
        return false;
    }

    std::ifstream file(tomlPath);
    if (!file.is_open()) return false;

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        // 检测节标题 [mods] 或 [dependencies]
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

        std::string key = Trim(line.substr(0, eqPos));
        std::string value = Trim(line.substr(eqPos + 1));

        // 移除注释
        size_t commentPos = value.find('#');
        if (commentPos != std::string::npos) {
            value = Trim(value.substr(0, commentPos));
        }

        // 提取引号内的值
        std::string extracted = ExtractQuoted(value);
        if (extracted.empty() && !value.empty() && value[0] != '"' && value[0] != '\'') {
            // 可能是数字或布尔值
            extracted = value;
        }

        // 只处理 mods 节的内容
        if (currentSection == "mods" || currentSection.empty()) {
            if (key == "modId" || key == "modId\"") {
                metadata.modId = extracted;
            }
            else if (key == "version") {
                metadata.version = extracted;
            }
            else if (key == "displayName" || key == "name") {
                metadata.modName = extracted;
            }
            else if (key == "description") {
                metadata.description = extracted;
            }
            else if (key == "author") {
                metadata.author = extracted;
            }
        }

        // 解析 dependencies
        if (currentSection.find("dependencies") != std::string::npos) {
            if (key == "modId") {
                metadata.dependencies.push_back(extracted);
            }
        }
    }

    file.close();
    return !metadata.modId.empty();
}

std::vector<RegisteredItem> ModMetadataParser::ParseRegistries(const std::string& javaContent) {
    std::vector<RegisteredItem> items;

    // 1. 查找 DeferredRegister 模式
    // 例如: public static final DeferredRegister<Item> ITEMS = DeferredRegister.create(ForgeRegistries.ITEMS, MOD_ID);
    std::regex deferredRegex(R"(public\s+static\s+final\s+DeferredRegister<(\w+)>\s+(\w+)\s*=\s*DeferredRegister\.create\([^,]+,\s*([^)]+)\))");
    std::smatch match;
    std::string content = javaContent;

    auto begin = std::sregex_iterator(content.begin(), content.end(), deferredRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::smatch m = *it;
        if (m.size() >= 4) {
            std::string type = m[1].str();
            std::string regName = m[2].str();
            std::string modIdVar = Trim(m[3].str());

            // 继续查找这个注册器中的具体条目
            std::string regPattern = regName + "\\.register\\s*\\(\\s*\"([^\"]+)\"\\s*,\\s*()\\s*->\\s*new\\s+(\\w+)";
            std::regex itemRegex(regPattern);
            std::string subContent = content;

            auto subBegin = std::sregex_iterator(subContent.begin(), subContent.end(), itemRegex);
            auto subEnd = std::sregex_iterator();

            for (auto subIt = subBegin; subIt != subEnd; ++subIt) {
                std::smatch subM = *subIt;
                if (subM.size() >= 3) {
                    RegisteredItem item;
                    item.registryName = subM[1].str();
                    item.className = subM[2].str();
                    item.isBlock = (type == "Block" || type.find("Block") != std::string::npos);
                    item.hasModel = true;
                    items.push_back(item);
                }
            }
        }
    }

    // 2. 查找 @ObjectHolder 注解
    std::regex objectHolderRegex(R"(@ObjectHolder\s*\(\s*[\"']([^\"']+)[\"']\s*\)\s*public\s+static\s+final\s+(\w+)\s+(\w+);)");
    auto ohBegin = std::sregex_iterator(content.begin(), content.end(), objectHolderRegex);
    auto ohEnd = std::sregex_iterator();

    for (auto it = ohBegin; it != ohEnd; ++it) {
        std::smatch m = *it;
        if (m.size() >= 4) {
            RegisteredItem item;
            item.registryName = m[1].str();
            item.className = m[3].str();
            item.isBlock = (m[2].str().find("Block") != std::string::npos);
            item.hasModel = true;
            items.push_back(item);
        }
    }

    // 3. 查找简单的 Item 注册 (普通模式)
    std::regex simpleItemRegex(R"(public\s+static\s+final\s+(\w+)\s+(\w+)\s*=\s*new\s+(\w+)\([^;]*\))");
    auto siBegin = std::sregex_iterator(content.begin(), content.end(), simpleItemRegex);
    auto siEnd = std::sregex_iterator();

    for (auto it = siBegin; it != siEnd; ++it) {
        std::smatch m = *it;
        if (m.size() >= 4) {
            std::string type = m[1].str();
            // 只捕获明显是物品或方块的
            if (type.find("Item") != std::string::npos ||
                type.find("Block") != std::string::npos ||
                type.find("BlockItem") != std::string::npos) {
                RegisteredItem item;
                item.registryName = m[2].str();
                item.className = m[3].str();
                item.isBlock = (type.find("Block") != std::string::npos);
                item.hasModel = true;
                items.push_back(item);
            }
        }
    }

    return items;
}

std::vector<RegisteredRecipe> ModMetadataParser::ParseRecipes(const std::string& javaContent) {
    std::vector<RegisteredRecipe> recipes;

    // 解析有序配方 (ShapedRecipe)
    std::regex shapedRegex(R"(ShapedRecipe\.shaped\s*\([^,]*,\s*([^,]*),\s*([^)]*)\))");
    auto begin = std::sregex_iterator(javaContent.begin(), javaContent.end(), shapedRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::smatch m = *it;
        if (m.size() >= 3) {
            RegisteredRecipe recipe;
            recipe.type = "shaped";
            recipe.output = Trim(m[1].str());
            recipe.outputCount = 1;
            // 解析配方模式（简化处理）
            std::string patternStr = m[2].str();
            std::regex itemRegex(R"(\"([^\"]+)\")");
            auto itemBegin = std::sregex_iterator(patternStr.begin(), patternStr.end(), itemRegex);
            auto itemEnd = std::sregex_iterator();
            for (auto itemIt = itemBegin; itemIt != itemEnd; ++itemIt) {
                recipe.ingredients.push_back(itemIt->str(1));
            }
            recipes.push_back(recipe);
        }
    }

    // 解析无序配方 (ShapelessRecipe)
    std::regex shapelessRegex(R"(ShapelessRecipe\.shapeless\s*\([^,]*,\s*([^)]*)\))");
    auto slBegin = std::sregex_iterator(javaContent.begin(), javaContent.end(), shapelessRegex);
    auto slEnd = std::sregex_iterator();

    for (auto it = slBegin; it != slEnd; ++it) {
        std::smatch m = *it;
        if (m.size() >= 2) {
            RegisteredRecipe recipe;
            recipe.type = "shapeless";
            recipe.output = "minecraft:air";
            recipe.outputCount = 1;
            // 提取材料
            std::string ingredientsStr = m[1].str();
            std::regex itemRegex(R"(\"([^\"]+)\")");
            auto itemBegin = std::sregex_iterator(ingredientsStr.begin(), ingredientsStr.end(), itemRegex);
            auto itemEnd = std::sregex_iterator();
            for (auto itemIt = itemBegin; itemIt != itemEnd; ++itemIt) {
                recipe.ingredients.push_back(itemIt->str(1));
            }
            recipes.push_back(recipe);
        }
    }

    // 解析烧炼配方 (SmeltingRecipe)
    std::regex smeltingRegex(R"(SmeltingRecipe\.smelting\s*\(([^,]*),\s*([^,]*),\s*([^)]*)\))");
    auto smBegin = std::sregex_iterator(javaContent.begin(), javaContent.end(), smeltingRegex);
    auto smEnd = std::sregex_iterator();

    for (auto it = smBegin; it != smEnd; ++it) {
        std::smatch m = *it;
        if (m.size() >= 4) {
            RegisteredRecipe recipe;
            recipe.type = "smelting";
            recipe.output = Trim(m[1].str());
            recipe.outputCount = 1;
            // 输入物品
            std::string inputStr = m[2].str();
            std::regex itemRegex(R"(\"([^\"]+)\")");
            auto itemBegin = std::sregex_iterator(inputStr.begin(), inputStr.end(), itemRegex);
            auto itemEnd = std::sregex_iterator();
            for (auto itemIt = itemBegin; itemIt != itemEnd; ++itemIt) {
                recipe.ingredients.push_back(itemIt->str(1));
            }
            recipes.push_back(recipe);
        }
    }

    return recipes;
}

std::string ModMetadataParser::ExtractMainModClass(const std::string& javaContent) {
    // 查找 @Mod 注解
    std::regex modRegex(R"(@Mod\s*\(\s*[\"']([^\"']+)[\"']\s*\)\s*public\s+class\s+(\w+))");
    std::smatch match;
    if (std::regex_search(javaContent, match, modRegex) && match.size() >= 3) {
        return match[2].str();
    }

    // 查找 @Mod 的另一种写法
    std::regex modRegex2(R"(@Mod\s*\(\s*(\w+)\s*\)\s*public\s+class\s+(\w+))");
    if (std::regex_search(javaContent, match, modRegex2) && match.size() >= 3) {
        return match[2].str();
    }

    return "";
}