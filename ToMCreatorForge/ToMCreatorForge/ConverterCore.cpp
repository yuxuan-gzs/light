// ConverterCore.cpp - 最终完整版
#include "ConverterCore.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <set>
#include <map>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <windows.h>

namespace fs = std::filesystem;

// ========== 工具函数 ==========
static std::string EscapeJSON(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += c; break;
        }
    }
    return result;
}

static std::string TrimString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

static std::string GetCurrentTimeString() {
    std::time_t now = std::time(nullptr);
    char buf[64];
    struct tm localTm;
    localtime_s(&localTm, &now);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTm);
    return std::string(buf);
}

static std::string GetExeDirectory() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exePath(path);
    size_t pos = exePath.find_last_of("\\/");
    if (pos != std::string::npos) {
        return exePath.substr(0, pos);
    }
    return ".";
}    


// ========== 从 JSON 解析元素 ==========
struct JsonElement {
    std::string name;
    std::string type;
    std::string registryName;
    std::vector<std::string> files;
    std::map<std::string, std::string> metadata;
    bool isBlock = false;
    bool isEvent = false;
};

static std::vector<JsonElement> ParseJsonElements(const std::string& json) {
    std::vector<JsonElement> elements;

    size_t elementsStart = json.find("\"elements\"");
    if (elementsStart == std::string::npos) {
        elementsStart = json.find("\"mod_elements\"");
    }
    if (elementsStart == std::string::npos) return elements;

    size_t arrayStart = json.find('[', elementsStart);
    if (arrayStart == std::string::npos) return elements;

    int braceDepth = 0;
    size_t pos = arrayStart + 1;
    size_t elemStart = std::string::npos;

    while (pos < json.length()) {
        char c = json[pos];
        if (c == '{') {
            if (braceDepth == 0) {
                elemStart = pos;
            }
            braceDepth++;
        }
        else if (c == '}') {
            braceDepth--;
            if (braceDepth == 0 && elemStart != std::string::npos) {
                std::string elemJson = json.substr(elemStart, pos - elemStart + 1);

                JsonElement elem;

                std::regex nameRegex("\"name\"\\s*:\\s*\"([^\"]*)\"");
                std::smatch match;
                if (std::regex_search(elemJson, match, nameRegex) && match.size() > 1) {
                    elem.name = match[1].str();
                }

                std::regex typeRegex("\"type\"\\s*:\\s*\"([^\"]*)\"");
                if (std::regex_search(elemJson, match, typeRegex) && match.size() > 1) {
                    elem.type = match[1].str();
                    elem.isBlock = (elem.type == "block");
                    elem.isEvent = (elem.type == "event");
                }

                std::regex registryRegex("\"registryName\"\\s*:\\s*\"([^\"]*)\"");
                if (std::regex_search(elemJson, match, registryRegex) && match.size() > 1) {
                    elem.registryName = match[1].str();
                }

                std::regex filesRegex("\"files\"\\s*:\\s*\\[([^\\]]*)\\]");
                if (std::regex_search(elemJson, match, filesRegex) && match.size() > 1) {
                    std::string filesStr = match[1].str();
                    std::regex fileRegex("\"([^\"]*)\"");
                    auto fBegin = std::sregex_iterator(filesStr.begin(), filesStr.end(), fileRegex);
                    auto fEnd = std::sregex_iterator();
                    for (auto it = fBegin; it != fEnd; ++it) {
                        elem.files.push_back((*it)[1].str());
                    }
                }

                std::regex metaRegex("\"metadata\"\\s*:\\s*\\{([^}]*)\\}");
                if (std::regex_search(elemJson, match, metaRegex) && match.size() > 1) {
                    std::string metaStr = match[1].str();
                    std::regex pairRegex("\"([^\"]*)\"\\s*:\\s*\"([^\"]*)\"");
                    auto pBegin = std::sregex_iterator(metaStr.begin(), metaStr.end(), pairRegex);
                    auto pEnd = std::sregex_iterator();
                    for (auto it = pBegin; it != pEnd; ++it) {
                        elem.metadata[(*it)[1].str()] = (*it)[2].str();
                    }
                }

                if (!elem.name.empty() && !elem.registryName.empty()) {
                    elements.push_back(elem);
                }

                elemStart = std::string::npos;
            }
        }
        pos++;
    }

    return elements;
}

static std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"\\s*:\\s*\"([^\"]*)\"";
    std::regex regexPattern(pattern);
    std::smatch match;
    if (std::regex_search(json, match, regexPattern) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

// ========== 主转换函数 ==========
bool ConverterCore::Convert(const std::string& sourcePath,
    const std::string& outputPath,
    LogCallback logCb,
    ProgressCallback progressCb,
    const ConvertOptions& options) {
    auto startTime = std::chrono::high_resolution_clock::now();

    if (!fs::exists(sourcePath) || !fs::is_directory(sourcePath)) {
        logCb("[ERROR] Source folder does not exist or is not a directory");
        return false;
    }

    logCb("[INFO] ========================================");
    logCb("[INFO] Starting conversion...");
    logCb("[INFO] Source: " + sourcePath);
    logCb("[INFO] Output: " + outputPath);
    logCb("[INFO] Time: " + GetCurrentTimeString());
    progressCb(5);

    // ========== Step 1: 直接读取 mods.toml ==========
    logCb("[INFO] Step 1: Reading mods.toml...");
    std::string modId = "converted_mod";
    std::string modName = "Converted Mod";
    std::string modVersion = "1.0.0";
    std::string modDescription = "Converted from Forge/NeoForge";
    std::string modAuthor = "Unknown";
    std::string mcVersion = "1.20.1";
    std::string modLoader = "forge";

    std::vector<fs::path> possibleTomlPaths = {
        fs::path(sourcePath) / "src" / "main" / "resources" / "META-INF" / "mods.toml",
        fs::path(sourcePath) / "src" / "main" / "resources" / "META-INF" / "neoforge.mods.toml",
        fs::path(sourcePath) / "mods.toml"
    };

    fs::path tomlPath;
    for (const auto& p : possibleTomlPaths) {
        if (fs::exists(p)) {
            tomlPath = p;
            break;
        }
    }

    if (!tomlPath.empty()) {
        logCb("[INFO] Found mods.toml: " + tomlPath.string());
        std::ifstream file(tomlPath);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        std::regex idRegex(R"(modId\s*=\s*\"([^\"]+)\")");
        std::regex nameRegex(R"(displayName\s*=\s*\"([^\"]+)\")");
        std::regex versionRegex(R"(version\s*=\s*\"([^\"]+)\")");
        std::regex descRegex(R"(description\s*=\s*\"([^\"]+)\")");
        std::regex authorRegex(R"(authors\s*=\s*\"([^\"]+)\")");
        std::regex mcRegex(R"(mcversion\s*=\s*\"([^\"]+)\")");
        std::regex loaderRegex(R"(modLoader\s*=\s*\"([^\"]+)\")");

        std::smatch match;
        if (std::regex_search(content, match, idRegex) && match.size() > 1) {
            modId = match[1].str();
            logCb("[INFO] Mod ID from toml: " + modId);
        }
        if (std::regex_search(content, match, nameRegex) && match.size() > 1) {
            modName = match[1].str();
            logCb("[INFO] Mod Name from toml: " + modName);
        }
        if (std::regex_search(content, match, versionRegex) && match.size() > 1) {
            modVersion = match[1].str();
        }
        if (std::regex_search(content, match, descRegex) && match.size() > 1) {
            modDescription = match[1].str();
        }
        if (std::regex_search(content, match, authorRegex) && match.size() > 1) {
            modAuthor = match[1].str();
        }
        if (std::regex_search(content, match, mcRegex) && match.size() > 1) {
            mcVersion = match[1].str();
        }
        if (std::regex_search(content, match, loaderRegex) && match.size() > 1) {
            modLoader = match[1].str();
            if (modLoader.find("lowcodefml") != std::string::npos) modLoader = "neoforge";
            else if (modLoader.find("javafml") != std::string::npos) modLoader = "forge";
        }
    }
    else {
        logCb("[WARN] mods.toml not found");
    }

    if (modId.empty()) modId = "converted_mod";
    if (modName.empty()) modName = modId;
    if (modVersion.empty()) modVersion = "1.0.0";
    if (mcVersion.empty()) mcVersion = "1.20.1";

    std::string generator = modLoader + "-" + mcVersion;
    logCb("[INFO] Generator: " + generator);
    progressCb(10);

    // ========== Step 2: 运行 Java 解析器 ==========
    logCb("[INFO] Step 2: Running Java parser...");

    std::string exeDir = GetExeDirectory();
    fs::path jsonPath = fs::path(outputPath) / "parsed_result.json";

    // 检查 Main.class 是否存在
    fs::path mainClass = fs::path(exeDir) / "Main.class";
    if (!fs::exists(mainClass)) {
        logCb("[ERROR] Main.class not found in " + exeDir);
        logCb("[INFO] Please compile Java parser first:");
        logCb("[INFO] javac -cp \"javaparser-core-3.26.2.jar;javaparser-symbol-solver-core-3.26.2.jar;gson-2.11.0.jar;guava-33.0.0-jre.jar\" Main.java");
        return false;
    }

    // 创建临时 bat 文件
    std::string batPath = (fs::path(outputPath) / "run_java.bat").string();
    std::ofstream batFile(batPath);
    if (batFile.is_open()) {
        batFile << "@echo off\n";
        batFile << "java -cp \"" << exeDir << "\\javaparser-core-3.26.2.jar;"
            << exeDir << "\\javaparser-symbol-solver-core-3.26.2.jar;"
            << exeDir << "\\gson-2.11.0.jar;"
            << exeDir << "\\guava-33.0.0-jre.jar;"
            << exeDir << "\" Main \""
            << sourcePath << "\" \"" << jsonPath.string() << "\" 2>&1\n";
        batFile.close();
    }

    std::string cmd = "\"" + batPath + "\"";
    logCb("[INFO] Running Java parser...");

    int result = std::system(cmd.c_str());

    if (result != 0) {
        logCb("[ERROR] Java parser failed with code: " + std::to_string(result));
        return false;
    }

    logCb("[INFO] Java parser completed");
    progressCb(40);

    // ========== Step 3: 读取 JSON ==========
    logCb("[INFO] Step 3: Reading parsed results...");

    if (!fs::exists(jsonPath)) {
        logCb("[WARN] JSON file not found, continuing without element data");
    }
    else {
        std::ifstream jsonFile(jsonPath.string());
        std::string jsonContent;
        if (jsonFile.is_open()) {
            std::stringstream buffer;
            buffer << jsonFile.rdbuf();
            jsonContent = buffer.str();
            jsonFile.close();
        }

        if (!jsonContent.empty()) {
            // 从 JSON 读取元素
            auto elements = ParseJsonElements(jsonContent);

            // 如果 JSON 中有 modId，优先使用
            std::string jsonModId = ExtractJsonValue(jsonContent, "modId");
            if (!jsonModId.empty() && jsonModId != "converted_mod") {
                modId = jsonModId;
                logCb("[INFO] Mod ID from JSON: " + modId);
            }

            std::string jsonModName = ExtractJsonValue(jsonContent, "modName");
            if (!jsonModName.empty()) {
                modName = jsonModName;
            }

            logCb("[INFO] Elements from JSON: " + std::to_string(elements.size()));

            // ========== Step 4: 生成 .mcreator 文件 ==========
            logCb("[INFO] Step 4: Generating .mcreator file...");

            fs::path workspaceDir = fs::path(outputPath);

            // 创建目录
            std::vector<fs::path> dirsToCreate = {
                workspaceDir / "src" / "main" / "java",
                workspaceDir / "src" / "main" / "resources",
                workspaceDir / "models",
                workspaceDir / "run",
                workspaceDir / "build",
                workspaceDir / "gradle" / "wrapper",
                workspaceDir / "elements"
            };
            for (auto& dir : dirsToCreate) {
                try { fs::create_directories(dir); }
                catch (...) {}
            }

            // 生成 JSON
            std::string mcreatorJson;
            mcreatorJson += "{\n";
            mcreatorJson += "  \"_fv\": 2,\n";
            mcreatorJson += "  \"mod_elements\": [\n";

            bool firstElement = true;
            int elementIndex = 0;

            for (const auto& elem : elements) {
                if (!firstElement) mcreatorJson += ",\n";
                firstElement = false;

                std::string safeName = elem.registryName;
                std::replace(safeName.begin(), safeName.end(), ':', '_');

                std::string elementType = elem.type.empty() ? "item" : elem.type;

                mcreatorJson += "    {\n";
                mcreatorJson += "      \"name\": \"" + EscapeJSON(elem.name) + "\",\n";
                mcreatorJson += "      \"type\": \"" + elementType + "\",\n";
                mcreatorJson += "      \"compiles\": true,\n";
                mcreatorJson += "      \"locked_code\": false,\n";
                mcreatorJson += "      \"registry_name\": \"" + EscapeJSON(safeName) + "\",\n";
                mcreatorJson += "      \"metadata\": {\n";
                mcreatorJson += "        \"files\": [\n";

                for (size_t j = 0; j < elem.files.size(); j++) {
                    std::string relPath = elem.files[j];
                    std::replace(relPath.begin(), relPath.end(), '\\', '/');
                    mcreatorJson += "          \"" + EscapeJSON(relPath) + "\"";
                    if (j < elem.files.size() - 1) mcreatorJson += ",";
                    mcreatorJson += "\n";
                }
                mcreatorJson += "        ]\n";
                mcreatorJson += "      }\n";
                mcreatorJson += "    }";
                elementIndex++;
            }

            mcreatorJson += "\n  ],\n";
            mcreatorJson += "  \"variable_elements\": [],\n";
            mcreatorJson += "  \"sound_elements\": [],\n";
            mcreatorJson += "  \"tag_elements\": {},\n";
            mcreatorJson += "  \"tab_element_order\": {},\n";

            mcreatorJson += "  \"language_map\": {\n";
            mcreatorJson += "    \"en_us\": {\n";
            mcreatorJson += "      \"item_group." + EscapeJSON(modId) + "." + EscapeJSON(modId) + "\": \"" + EscapeJSON(modName) + "\"\n";
            mcreatorJson += "    }\n";
            mcreatorJson += "  },\n";

            mcreatorJson += "  \"metadata\": {\n";
            mcreatorJson += "    \"files\": []\n";
            mcreatorJson += "  },\n";
            mcreatorJson += "  \"foldersRoot\": {\n";
            mcreatorJson += "    \"name\": \"~\",\n";
            mcreatorJson += "    \"children\": []\n";
            mcreatorJson += "  },\n";

            mcreatorJson += "  \"workspaceSettings\": {\n";
            mcreatorJson += "    \"modid\": \"" + EscapeJSON(modId) + "\",\n";
            mcreatorJson += "    \"modName\": \"" + EscapeJSON(modName) + "\",\n";
            mcreatorJson += "    \"version\": \"" + EscapeJSON(modVersion) + "\",\n";
            mcreatorJson += "    \"description\": \"" + EscapeJSON(modDescription) + "\",\n";
            mcreatorJson += "    \"author\": \"" + EscapeJSON(modAuthor) + "\",\n";
            mcreatorJson += "    \"websiteURL\": \"\",\n";
            mcreatorJson += "    \"license\": \"MIT\",\n";
            mcreatorJson += "    \"serverSideOnly\": false,\n";
            mcreatorJson += "    \"modPicture\": \"\",\n";
            mcreatorJson += "    \"requiredMods\": [],\n";
            mcreatorJson += "    \"dependencies\": [],\n";
            mcreatorJson += "    \"dependants\": [],\n";
            mcreatorJson += "    \"mcreatorDependencies\": [],\n";
            mcreatorJson += "    \"currentGenerator\": \"" + generator + "\",\n";
            mcreatorJson += "    \"credits\": \"Converted from Forge/NeoForge using Converter v0.4\",\n";
            mcreatorJson += "    \"modElementsPackage\": \"net.mcreator." + EscapeJSON(modId) + "\"\n";
            mcreatorJson += "  },\n";

            mcreatorJson += "  \"mcreatorVersion\": 202600229418\n";
            mcreatorJson += "}\n";

            fs::path mcreatorFile = workspaceDir / (modId + ".mcreator");
            std::ofstream mcf(mcreatorFile);
            if (mcf.is_open()) {
                mcf << mcreatorJson;
                mcf.close();
                logCb("[GENERATED] " + mcreatorFile.filename().string());
                logCb("[INFO] Elements in .mcreator: " + std::to_string(elementIndex));
            }
            else {
                logCb("[ERROR] Failed to write .mcreator file");
                return false;
            }
        }
    }

    progressCb(70);

    // ========== Step 5: 复制源代码 ==========
    if (options.includeSource) {
        logCb("[INFO] Step 5: Copying source files...");
        fs::path srcJavaDir2 = fs::path(sourcePath) / "src" / "main" / "java";
        if (fs::exists(srcJavaDir2)) {
            int copyCount = 0;
            for (auto& entry : fs::recursive_directory_iterator(srcJavaDir2)) {
                if (entry.is_regular_file()) {
                    fs::path relPath = fs::relative(entry.path(), srcJavaDir2);
                    fs::path targetPath = fs::path(outputPath) / "src" / "main" / "java" / relPath;
                    fs::create_directories(targetPath.parent_path());
                    fs::copy(entry.path(), targetPath, fs::copy_options::overwrite_existing);
                    copyCount++;
                }
            }
            logCb("[COPY] " + std::to_string(copyCount) + " Java files");
        }
    }

    // ========== Step 6: 复制资源文件 ==========
    logCb("[INFO] Step 6: Copying resource files...");

    fs::path srcResourcesDir = fs::path(sourcePath) / "src" / "main" / "resources";
    if (fs::exists(srcResourcesDir)) {
        int copyCount = 0;
        for (auto& entry : fs::recursive_directory_iterator(srcResourcesDir)) {
            if (entry.is_regular_file()) {
                fs::path relPath = fs::relative(entry.path(), srcResourcesDir);
                fs::path targetPath = fs::path(outputPath) / "src" / "main" / "resources" / relPath;
                fs::create_directories(targetPath.parent_path());
                fs::copy(entry.path(), targetPath, fs::copy_options::overwrite_existing);
                copyCount++;
            }
        }
        logCb("[COPY] " + std::to_string(copyCount) + " resource files");
    }
    else {
        logCb("[WARN] No resources directory found");
    }

    // ========== Step 7: 复制 Gradle 文件 ==========
    logCb("[INFO] Step 7: Copying Gradle files...");

    std::vector<std::string> gradleFiles = { "build.gradle", "gradle.properties", "settings.gradle", "mcreator.gradle" };
    for (auto& f : gradleFiles) {
        fs::path srcPath = fs::path(sourcePath) / f;
        if (fs::exists(srcPath)) {
            fs::path dstPath = fs::path(outputPath) / f;
            fs::copy(srcPath, dstPath, fs::copy_options::overwrite_existing);
            logCb("[COPY] " + f);
        }
    }

    fs::path srcGradlew = fs::path(sourcePath) / "gradlew";
    if (fs::exists(srcGradlew)) {
        fs::copy(srcGradlew, fs::path(outputPath) / "gradlew", fs::copy_options::overwrite_existing);
        logCb("[COPY] gradlew");
    }

    fs::path srcGradlewBat = fs::path(sourcePath) / "gradlew.bat";
    if (fs::exists(srcGradlewBat)) {
        fs::copy(srcGradlewBat, fs::path(outputPath) / "gradlew.bat", fs::copy_options::overwrite_existing);
        logCb("[COPY] gradlew.bat");
    }

    fs::path srcGradleDir = fs::path(sourcePath) / "gradle";
    if (fs::exists(srcGradleDir) && fs::is_directory(srcGradleDir)) {
        for (auto& entry : fs::recursive_directory_iterator(srcGradleDir)) {
            if (entry.is_regular_file()) {
                fs::path relPath = fs::relative(entry.path(), srcGradleDir);
                fs::path targetPath = fs::path(outputPath) / "gradle" / relPath;
                fs::create_directories(targetPath.parent_path());
                fs::copy(entry.path(), targetPath, fs::copy_options::overwrite_existing);
            }
        }
        logCb("[COPY] gradle/");
    }

    progressCb(90);

    // ========== Step 8: 生成 ZIP ==========
    if (options.exportZip) {
        logCb("[INFO] Step 8: Generating ZIP package...");
        std::string zipPath = (fs::path(outputPath) / (modId + "_" + modVersion + ".zip")).string();
        std::string psCommand = "powershell -Command \"Compress-Archive -Path '" + outputPath + "\\*' -DestinationPath '" + zipPath + "' -Force\"";
        int result = std::system(psCommand.c_str());
        if (result == 0 && fs::exists(zipPath)) {
            logCb("[SUCCESS] ZIP generated: " + zipPath);
        }
        else {
            logCb("[WARN] ZIP generation failed");
        }
    }

    progressCb(100);

    // ========== Summary ==========
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    logCb("");
    logCb("========================================");
    logCb("[COMPLETE] MCreator workspace generated!");
    logCb("========================================");
    logCb("[INFO] Workspace: " + outputPath);
    logCb("[INFO] Mod ID: " + modId);
    logCb("[INFO] Mod Name: " + modName);
    logCb("[INFO] Mod Version: " + modVersion);
    logCb("[INFO] Generator: " + generator);
    logCb("[INFO] Time taken: " + std::to_string(duration.count()) + " ms");
    logCb("");
    logCb("[TIP] Open " + modId + ".mcreator with MCreator");

    return true;
}