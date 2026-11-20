// PluginTemplateGenerator.cpp
#define _CRT_SECURE_NO_WARNINGS
#include "PluginTemplateGenerator.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include "winwrap.h"
#include <direct.h>

// 包含 minizip 头文件
#include "../../../zlib-1.3.2/contrib/minizip/zip.h"
#include "../../../zlib-1.3.2/contrib/minizip/unzip.h"
#include "../../../zlib-1.3.2/zlib.h"
#include "..\\..\\lzma\\C\\LzmaLib.h"
#include "../../../zlib-1.3.2/zip.h"


// ====== 链接 minizip 库 ======
//#pragma comment(lib, "../../../zlib-1.3.2/build/Release/z.lib")
//#pragma comment(lib, "../../../zlib-1.3.2/contrib/minizip/minizip.lib")


namespace fs = std::filesystem;

bool PluginTemplateGenerator::GenerateTemplate(const TemplateConfig& config) {
    std::cout << "[Template] Generating plugin template..." << std::endl;
    std::cout << "[Template] Plugin name: " << config.pluginName << std::endl;
    std::cout << "[Template] Plugin ID: " << config.pluginId << std::endl;
    std::cout << "[Template] Version: " << config.version << std::endl;
    std::cout << "[Template] Author: " << config.author << std::endl;
    std::cout << "[Template] Output dir: " << config.outputDir << std::endl;

    if (!CreateDirectoryStructure(config.outputDir)) {
        std::cerr << "[Template] Failed to create directory structure" << std::endl;
        return false;
    }

    bool success = true;
    success = success && GeneratePluginJson(config.outputDir, config);
    success = success && GeneratePluginCpp(config.outputDir, config);
    success = success && GeneratePluginH(config.outputDir, config);
    success = success && GenerateCMakeLists(config.outputDir, config);
    success = success && GenerateReadme(config.outputDir, config);
    success = success && GenerateBuildScript(config.outputDir, config);
    success = success && GenerateResourceFiles(config.outputDir, config);

    if (!success) {
        std::cerr << "[Template] Failed to generate files" << std::endl;
        return false;
    }

    // ZIP 输出到模板目录的父目录，避免依赖未初始化的全局路径。
    std::string zipFilePath;
    {
        fs::path out = fs::absolute(fs::path(config.outputDir));
        // outputDir 末尾可能带分隔符，去掉空文件名后再取父目录。
        if (out.filename().empty()) out = out.parent_path();
        zipFilePath = (out.parent_path() / (config.pluginName + ".zip")).string();
    }

    std::cout << "[Template] Creating ZIP: " << zipFilePath << std::endl;

    if (!PackToZip(config.outputDir, zipFilePath)) {
        std::cerr << "[Template] Failed to pack ZIP" << std::endl;
        return false;
    }

    // 删除源文件夹
    try {
        fs::remove_all(config.outputDir);
        std::cout << "[Template] Removed source directory: " << config.outputDir << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[Template] Warning: Failed to remove source directory: " << e.what() << std::endl;
    }

    std::cout << "[Template] Template generated successfully!" << std::endl;
    std::cout << "[Template] ZIP file: " << zipFilePath << std::endl;
    return true;
}

bool PluginTemplateGenerator::CreateDirectoryStructure(const std::string& baseDir) {
    std::vector<std::string> dirs = {
        baseDir,
        baseDir + "\\src",
        baseDir + "\\include",
        baseDir + "\\resources",
        baseDir + "\\libs",
        baseDir + "\\data"
    };

    for (const auto& dir : dirs) {
        // 先检查是否已存在
        DWORD dwAttrib = GetFileAttributesA(dir.c_str());
        if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
            std::cout << "[Template] Directory already exists: " << dir << std::endl;
            continue;
        }

        // 逐级创建目录
        std::string path = dir;
        std::string currentPath;
        size_t startPos = 0;

        // 处理盘符 (如 D:\)
        if (path.length() >= 2 && path[1] == ':') {
            currentPath = path.substr(0, 3);
            startPos = 3;
        }
        // 处理网络路径 (如 \\server\share\)
        else if (path.length() >= 2 && path[0] == '\\' && path[1] == '\\') {
            size_t pos = path.find('\\', 2);
            if (pos != std::string::npos) {
                pos = path.find('\\', pos + 1);
                if (pos != std::string::npos) {
                    currentPath = path.substr(0, pos + 1);
                    startPos = pos + 1;
                }
            }
        }

        // 逐级创建
        bool success = true;
        while (startPos < path.length() && startPos != std::string::npos) {
            size_t pos = path.find('\\', startPos);
            if (pos == std::string::npos) {
                pos = path.length();
            }

            std::string subPath = path.substr(0, pos);
            if (subPath.length() > 3) {  // 跳过 "D:\" 或 "C:\" 等
                DWORD attr = GetFileAttributesA(subPath.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES) {
                    // 目录不存在，创建
                    if (!CreateDirectoryA(subPath.c_str(), NULL)) {
                        DWORD error = GetLastError();
                        if (error != ERROR_ALREADY_EXISTS) {
                            std::cerr << "[Template] Failed to create directory: " << subPath
                                << " (error: " << error << ")" << std::endl;
                            success = false;
                            break;
                        }
                    }
                    else {
                        std::cout << "[Template] Created directory: " << subPath << std::endl;
                    }
                }
            }
            startPos = pos + 1;
        }

        if (!success) {
            return false;
        }

        // 验证最终目录是否创建成功
        dwAttrib = GetFileAttributesA(dir.c_str());
        if (dwAttrib == INVALID_FILE_ATTRIBUTES || !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
            std::cerr << "[Template] Failed to verify directory: " << dir << std::endl;
            return false;
        }
    }
    return true;
}

bool PluginTemplateGenerator::GeneratePluginJson(const std::string& outputDir, const TemplateConfig& config) {
    std::string filePath = outputDir + "\\plugin.json";
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Template] Cannot create file: " << filePath << std::endl;
        return false;
    }

    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    char dateBuffer[32];
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", tm);

    file << "{\n";
    file << "    \"id\": \"" << config.pluginId << "\",\n";
    file << "    \"name\": \"" << config.pluginName << "\",\n";
    file << "    \"version\": \"" << config.version << "\",\n";
    file << "    \"author\": \"" << config.author << "\",\n";
    file << "    \"description\": \"" << config.description << "\",\n";
    file << "    \"website\": \"https://github.com/" << config.author << "/" << config.pluginName << "\",\n";
    file << "    \"min_light_version\": \"1.0.0\",\n";
    file << "    \"date\": \"" << dateBuffer << "\",\n";
    file << "    \"commands\": [\n";
    file << "        {\n";
    file << "            \"name\": \"" << config.pluginName << "_hello\",\n";
    file << "            \"description\": \"Say hello from plugin\",\n";
    file << "            \"usage\": \"" << config.pluginName << "_hello [name]\"\n";
    file << "        },\n";
    file << "        {\n";
    file << "            \"name\": \"" << config.pluginName << "_info\",\n";
    file << "            \"description\": \"Show plugin information\",\n";
    file << "            \"usage\": \"" << config.pluginName << "_info\"\n";
    file << "        }\n";
    file << "    ],\n";
    file << "    \"config\": {\n";
    file << "        \"enable_logging\": true,\n";
    file << "        \"timeout\": 30,\n";
    file << "        \"theme\": \"dark\"\n";
    file << "    },\n";
    file << "    \"dependencies\": []\n";
    file << "}\n";

    file.close();
    std::cout << "[Template] Created: plugin.json" << std::endl;
    return true;
}

bool PluginTemplateGenerator::GeneratePluginCpp(const std::string& outputDir, const TemplateConfig& config) {
    std::string filePath = outputDir + "\\src\\" + config.pluginName + ".cpp";
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Template] Cannot create file: " << filePath << std::endl;
        return false;
    }

    std::string className = config.pluginName;
    className[0] = std::toupper(className[0]);

    file << "// " << config.pluginName << ".cpp\n";
    file << "// Plugin: " << config.pluginName << "\n";
    file << "// Author: " << config.author << "\n";
    file << "// Version: " << config.version << "\n\n";

    file << "#include \"../include/" << config.pluginName << ".h\"\n";
    file << "#include <iostream>\n";
    file << "#include <chrono>\n";
    file << "#include <sstream>\n\n";

    file << config.pluginName << "Plugin::" << config.pluginName << "Plugin() \n";
    file << "    : m_initialized(false), m_pluginName(\"" << config.pluginName << "\"), \n";
    file << "      m_version(\"" << config.version << "\"), m_author(\"" << config.author << "\") {\n";
    file << "}\n\n";

    file << config.pluginName << "Plugin::~" << config.pluginName << "Plugin() {\n";
    file << "    if (m_initialized) {\n";
    file << "        Shutdown();\n";
    file << "    }\n";
    file << "}\n\n";

    file << "PluginInfo " << config.pluginName << "Plugin::GetInfo() {\n";
    file << "    PluginInfo info;\n";
    file << "    info.name = m_pluginName;\n";
    file << "    info.version = m_version;\n";
    file << "    info.author = m_author;\n";
    file << "    info.description = \"" << config.description << "\";\n";
    file << "    info.id = \"" << config.pluginId << "\";\n";
    file << "    return info;\n";
    file << "}\n\n";

    file << "bool " << config.pluginName << "Plugin::Initialize() {\n";
    file << "    if (m_initialized) {\n";
    file << "        return true;\n";
    file << "    }\n\n";
    file << "    std::cout << \"[Plugin] \" << m_pluginName << \" initializing...\" << std::endl;\n\n";
    file << "    try {\n";
    file << "        // TODO: Initialize plugin resources here\n";
    file << "        // e.g., load config, establish connections, init database\n\n";
    file << "        m_initialized = true;\n";
    file << "        std::cout << \"[Plugin] \" << m_pluginName << \" initialized successfully!\" << std::endl;\n";
    file << "        return true;\n";
    file << "    } catch (const std::exception& e) {\n";
    file << "        std::cerr << \"[Plugin] Initialize failed: \" << e.what() << std::endl;\n";
    file << "        return false;\n";
    file << "    }\n";
    file << "}\n\n";

    file << "void " << config.pluginName << "Plugin::Shutdown() {\n";
    file << "    if (!m_initialized) {\n";
    file << "        return;\n";
    file << "    }\n\n";
    file << "    std::cout << \"[Plugin] \" << m_pluginName << \" shutting down...\" << std::endl;\n\n";
    file << "    // TODO: Clean up resources here\n";
    file << "    // e.g., save config, close connections, free memory\n\n";
    file << "    m_initialized = false;\n";
    file << "    std::cout << \"[Plugin] \" << m_pluginName << \" shut down\" << std::endl;\n";
    file << "}\n\n";

    file << "void " << config.pluginName << "Plugin::OnLoad() {\n";
    file << "    std::cout << \"[Plugin] \" << m_pluginName << \" v\" << m_version << \" loaded!\" << std::endl;\n";
    file << "}\n\n";

    file << "void " << config.pluginName << "Plugin::OnUnload() {\n";
    file << "    std::cout << \"[Plugin] \" << m_pluginName << \" unloaded!\" << std::endl;\n";
    file << "}\n\n";

    file << "std::vector<PluginCommand> " << config.pluginName << "Plugin::GetCommands() {\n";
    file << "    std::vector<PluginCommand> commands;\n\n";

    file << "    // Hello command\n";
    file << "    PluginCommand helloCmd;\n";
    file << "    helloCmd.name = \"" << config.pluginName << "_hello\";\n";
    file << "    helloCmd.description = \"Say hello from plugin\";\n";
    file << "    helloCmd.usage = \"" << config.pluginName << "_hello [name]\";\n";
    file << "    helloCmd.callback = [this](const std::vector<std::string>& args) {\n";
    file << "        if (!m_initialized) {\n";
    file << "            std::cout << \"[Error] Plugin not initialized\" << std::endl;\n";
    file << "            return;\n";
    file << "        }\n";
    file << "        std::string name = args.empty() ? \"World\" : args[0];\n";
    file << "        std::cout << \"[Plugin] Hello, \" << name << \"! from \" << m_pluginName << std::endl;\n";
    file << "    };\n";
    file << "    commands.push_back(helloCmd);\n\n";

    file << "    // Info command\n";
    file << "    PluginCommand infoCmd;\n";
    file << "    infoCmd.name = \"" << config.pluginName << "_info\";\n";
    file << "    infoCmd.description = \"Show plugin information\";\n";
    file << "    infoCmd.usage = \"" << config.pluginName << "_info\";\n";
    file << "    infoCmd.callback = [this](const std::vector<std::string>& args) {\n";
    file << "        if (!m_initialized) {\n";
    file << "            std::cout << \"[Error] Plugin not initialized\" << std::endl;\n";
    file << "            return;\n";
    file << "        }\n";
    file << "        std::cout << \"=== Plugin Information ===\" << std::endl;\n";
    file << "        std::cout << \"Name: \" << m_pluginName << std::endl;\n";
    file << "        std::cout << \"Version: \" << m_version << std::endl;\n";
    file << "        std::cout << \"Author: \" << m_author << std::endl;\n";
    file << "        std::cout << \"Description: " << config.description << "\" << std::endl;\n";
    file << "        std::cout << \"ID: \"" << config.pluginId << "\" << std::endl;\n";
    file << "        std::cout << \"Status: \" << (m_initialized ? \"Initialized\" : \"Not initialized\") << std::endl;\n";
    file << "    };\n";
    file << "    commands.push_back(infoCmd);\n\n";

    file << "    return commands;\n";
    file << "}\n\n";

    file << "void " << config.pluginName << "Plugin::OnCommand(const std::string& cmd, const std::vector<std::string>& args) {\n";
    file << "    // Handle unregistered commands (optional)\n";
    file << "    if (cmd == \"" << config.pluginName << "_test\") {\n";
    file << "        std::cout << \"[Plugin] Test command received, args count: \" << args.size() << std::endl;\n";
    file << "        for (const auto& arg : args) {\n";
    file << "            std::cout << \"  - \" << arg << std::endl;\n";
    file << "        }\n";
    file << "        return;\n";
    file << "    }\n";
    file << "    std::cout << \"[Plugin] Unknown command: \" << cmd << std::endl;\n";
    file << "}\n\n";

    file << "// ==================== Export Functions ====================\n\n";
    file << "PLUGIN_API IPlugin* CreatePlugin() {\n";
    file << "    return new " << config.pluginName << "Plugin();\n";
    file << "}\n\n";
    file << "PLUGIN_API void DestroyPlugin(IPlugin* plugin) {\n";
    file << "    delete plugin;\n";
    file << "}\n";

    file.close();
    std::cout << "[Template] Created: src\\" << config.pluginName << ".cpp" << std::endl;
    return true;
}

bool PluginTemplateGenerator::GeneratePluginH(const std::string& outputDir, const TemplateConfig& config) {
    std::string filePath = outputDir + "\\include\\" + config.pluginName + ".h";
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Template] Cannot create file: " << filePath << std::endl;
        return false;
    }

    std::string className = config.pluginName;
    className[0] = std::toupper(className[0]);

    file << "// " << config.pluginName << ".h\n";
    file << "// Plugin: " << config.pluginName << "\n";
    file << "// Author: " << config.author << "\n";
    file << "// Version: " << config.version << "\n\n";

    file << "#pragma once\n";
    file << "#include \"PluginInterface.h\"\n";
    file << "#include <string>\n";
    file << "#include <vector>\n\n";

    file << "class " << className << "Plugin : public IPlugin {\n";
    file << "public:\n";
    file << "    " << className << "Plugin();\n";
    file << "    ~" << className << "Plugin();\n\n";

    file << "    // IPlugin interface implementation\n";
    file << "    PluginInfo GetInfo() override;\n";
    file << "    bool Initialize() override;\n";
    file << "    void Shutdown() override;\n";
    file << "    void OnLoad() override;\n";
    file << "    void OnUnload() override;\n";
    file << "    std::vector<PluginCommand> GetCommands() override;\n";
    file << "    void OnCommand(const std::string& cmd, const std::vector<std::string>& args) override;\n\n";

    file << "private:\n";
    file << "    bool m_initialized;\n";
    file << "    std::string m_pluginName;\n";
    file << "    std::string m_version;\n";
    file << "    std::string m_author;\n";
    file << "};\n";

    file.close();
    std::cout << "[Template] Created: include\\" << config.pluginName << ".h" << std::endl;
    return true;
}

bool PluginTemplateGenerator::GenerateCMakeLists(const std::string& outputDir, const TemplateConfig& config) {
    std::string filePath = outputDir + "\\CMakeLists.txt";
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Template] Cannot create file: " << filePath << std::endl;
        return false;
    }

    file << "cmake_minimum_required(VERSION 3.10)\n";
    file << "project(" << config.pluginName << "Plugin VERSION " << config.version << ")\n\n";
    file << "set(CMAKE_CXX_STANDARD 17)\n";
    file << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";
    file << "# Set output directory\n";
    file << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)\n";
    file << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)\n\n";
    file << "# Include directories\n";
    file << "include_directories(\n";
    file << "    ${CMAKE_SOURCE_DIR}/include\n";
    file << "    ${LIGHT_PLUGIN_API_DIR}\n";
    file << ")\n\n";
    file << "# Source files\n";
    file << "set(SOURCES\n";
    file << "    src/" << config.pluginName << ".cpp\n";
    file << ")\n\n";
    file << "# Create shared library\n";
    file << "add_library(" << config.pluginName << " SHARED ${SOURCES})\n\n";
    file << "# Set target properties\n";
    file << "set_target_properties(" << config.pluginName << " PROPERTIES\n";
    file << "    PREFIX \"\"\n";
    file << "    SUFFIX \".dll\"\n";
    file << ")\n\n";
    file << "# Link libraries\n";
    file << "target_link_libraries(" << config.pluginName << "\n";
    file << "    ${LIGHT_PLUGIN_API_LIB}\n";
    file << ")\n\n";
    file << "# Export symbols\n";
    file << "if(MSVC)\n";
    file << "    target_compile_definitions(" << config.pluginName << " PRIVATE _CRT_SECURE_NO_WARNINGS)\n";
    file << "endif()\n";

    file.close();
    std::cout << "[Template] Created: CMakeLists.txt" << std::endl;
    return true;
}

bool PluginTemplateGenerator::GenerateReadme(const std::string& outputDir, const TemplateConfig& config) {
    std::string filePath = outputDir + "\\README.md";
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Template] Cannot create file: " << filePath << std::endl;
        return false;
    }

    file << "# " << config.pluginName << " Plugin\n\n";
    file << "## Description\n";
    file << config.description << "\n\n";
    file << "## Version\n";
    file << "- Current: v" << config.version << "\n";
    file << "- Author: " << config.author << "\n\n";
    file << "## Commands\n\n";
    file << "| Command | Description | Usage |\n";
    file << "|---------|-------------|-------|\n";
    file << "| `" << config.pluginName << "_hello` | Say hello | `" << config.pluginName << "_hello [name]` |\n";
    file << "| `" << config.pluginName << "_info` | Show plugin info | `" << config.pluginName << "_info` |\n\n";
    file << "## Build\n\n";
    file << "### Using CMake\n";
    file << "```bash\n";
    file << "mkdir build && cd build\n";
    file << "cmake .. -DLIGHT_PLUGIN_API_DIR=../LightPluginAPI\n";
    file << "cmake --build . --config Release\n";
    file << "```\n\n";
    file << "### Using Visual Studio\n";
    file << "1. Open Visual Studio\n";
    file << "2. Open CMakeLists.txt\n";
    file << "3. Select Release configuration\n";
    file << "4. Build the solution\n\n";
    file << "## Installation\n\n";
    file << "Copy `" << config.pluginName << ".dll` to Light's `plugins/` directory.\n\n";
    file << "## License\n\n";
    file << "MIT License\n";

    file.close();
    std::cout << "[Template] Created: README.md" << std::endl;
    return true;
}

bool PluginTemplateGenerator::GenerateBuildScript(const std::string& outputDir, const TemplateConfig& config) {
    std::string filePath = outputDir + "\\build.bat";
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Template] Cannot create file: " << filePath << std::endl;
        return false;
    }

    file << "@echo off\n";
    file << "echo Building " << config.pluginName << " Plugin...\n";
    file << "echo.\n\n";
    file << "if not exist build mkdir build\n";
    file << "cd build\n\n";
    file << "echo Generating Visual Studio solution...\n";
    file << "cmake .. -G \"Visual Studio 17 2022\" -A x64\n";
    file << "if errorlevel 1 goto error\n\n";
    file << "echo.\n";
    file << "echo Building plugin...\n";
    file << "cmake --build . --config Release\n";
    file << "if errorlevel 1 goto error\n\n";
    file << "echo.\n";
    file << "echo Build successful!\n";
    file << "echo Output: build\\bin\\Release\\" << config.pluginName << ".dll\n";
    file << "pause\n";
    file << "exit /b 0\n\n";
    file << ":error\n";
    file << "echo.\n";
    file << "echo Build failed!\n";
    file << "pause\n";
    file << "exit /b 1\n";

    file.close();
    std::cout << "[Template] Created: build.bat" << std::endl;

    std::string gitignorePath = outputDir + "\\.gitignore";
    std::ofstream gitignore(gitignorePath);
    if (gitignore.is_open()) {
        gitignore << "# Build directories\n";
        gitignore << "build/\n";
        gitignore << "cmake-build-*/\n";
        gitignore << "*.sln\n";
        gitignore << "*.vcxproj\n";
        gitignore << "*.vcxproj.filters\n";
        gitignore << "*.user\n\n";
        gitignore << "# Output files\n";
        gitignore << "*.dll\n";
        gitignore << "*.exe\n";
        gitignore << "*.pdb\n";
        gitignore << "*.ilk\n\n";
        gitignore << "# IDE files\n";
        gitignore << ".vs/\n";
        gitignore << ".vscode/\n";
        gitignore << "*.suo\n";
        gitignore << "*.userprefs\n";
        gitignore.close();
        std::cout << "[Template] Created: .gitignore" << std::endl;
    }

    return true;
}

bool PluginTemplateGenerator::GenerateResourceFiles(const std::string& outputDir, const TemplateConfig& config) {
    std::string configPath = outputDir + "\\resources\\config.default.ini";
    std::ofstream configFile(configPath);
    if (configFile.is_open()) {
        configFile << "; " << config.pluginName << " Plugin Configuration\n";
        configFile << "; Generated: " << config.author << "\n\n";
        configFile << "[General]\n";
        configFile << "EnableLogging=true\n";
        configFile << "Timeout=30\n";
        configFile << "Theme=dark\n\n";
        configFile << "[Plugin]\n";
        configFile << "Name=" << config.pluginName << "\n";
        configFile << "Version=" << config.version << "\n";
        configFile.close();
        std::cout << "[Template] Created: resources\\config.default.ini" << std::endl;
    }

    std::string dataPath = outputDir + "\\data\\example.txt";
    std::ofstream dataFile(dataPath);
    if (dataFile.is_open()) {
        dataFile << "This is an example data file for " << config.pluginName << " plugin.\n";
        dataFile << "You can put any data files your plugin needs here.\n";
        dataFile.close();
        std::cout << "[Template] Created: data\\example.txt" << std::endl;
    }

    return true;
}

bool PluginTemplateGenerator::GenerateApiTemplate(const TemplateConfig& config) {
    std::string name = config.pluginName;
    if (name.empty()) name = "MyApiPlugin";

    fs::path root = fs::absolute(fs::path(config.outputDir));
    if (root.filename().empty()) root = root.parent_path();
    fs::path srcDir = root / "src";
    fs::path incDir = root / "include";

    std::error_code ec;
    fs::create_directories(srcDir, ec);
    fs::create_directories(incDir, ec);
    if (ec) {
        std::cerr << "[Template] Cannot create API template dir: "
            << ec.message() << std::endl;
        return false;
    }

    const std::string dllName = "api_" + name;

    // ------------------------------------------------------------
    // 1) 最小 ABI 头文件（与 light.exe 的 PluginInterface.h 保持一致）
    // ------------------------------------------------------------
    {
        std::ofstream out(incDir / "PluginInterface.h", std::ios::binary);
        if (!out.is_open()) return false;
        out << R"HEADER(// Light API plugin ABI (auto-generated template)
#pragma once
#include <string>
#include <vector>
#include <functional>

struct PluginInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string id;
};

struct PluginCommand {
    std::string name;
    std::string description;
    std::string usage;
    std::function<void(const std::vector<std::string>& args)> callback;
};

class PluginUIManager;

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual PluginInfo GetInfo() = 0;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual std::vector<PluginCommand> GetCommands() { return {}; }
    virtual void OnLoad() {}
    virtual void OnUnload() {}
    virtual void OnCommand(const std::string& cmd, const std::vector<std::string>& args) {}
    virtual void SetUIManager(PluginUIManager* ui) { m_ui = ui; }
protected:
    PluginUIManager* m_ui = nullptr;
};

#define LIGHT_PLUGIN_API extern "C" __declspec(dllexport)
#define PLUGIN_API LIGHT_PLUGIN_API

LIGHT_PLUGIN_API IPlugin* CreatePlugin();
LIGHT_PLUGIN_API void DestroyPlugin(IPlugin* plugin);
)HEADER";
    }

    // ------------------------------------------------------------
    // 2) 插件实现
    // ------------------------------------------------------------
    {
        std::ofstream out(srcDir / (dllName + ".cpp"), std::ios::binary);
        if (!out.is_open()) return false;
        out << "// " << dllName << ".cpp\n";
        out << "// Light api_*.dll command-set template\n";
        out << "// Build the DLL, then put " << dllName
            << ".dll next to light.exe.\n\n";
        out << "#include \"../include/PluginInterface.h\"\n";
        out << "#include <iostream>\n";
        out << "#include <string>\n";
        out << "#include <vector>\n\n";
        out << "namespace {\n";
        out << "constexpr const char* kName = \"" << dllName << "\";\n";
        out << "constexpr const char* kId = \"" << config.pluginId << "\";\n";
        out << "constexpr const char* kVersion = \"" << config.version << "\";\n";
        out << "constexpr const char* kAuthor = \"" << config.author << "\";\n";
        out << "constexpr const char* kDescription = \"" << config.description << "\";\n";
        out << "}\n\n";
        out << "class " << name << "Plugin final : public IPlugin {\n";
        out << "public:\n";
        out << "    PluginInfo GetInfo() override {\n";
        out << "        return PluginInfo{ kName, kVersion, kAuthor, kDescription, kId };\n";
        out << "    }\n\n";
        out << "    bool Initialize() override { m_ready = true; return true; }\n";
        out << "    void Shutdown() override { m_ready = false; }\n\n";
        out << "    std::vector<PluginCommand> GetCommands() override {\n";
        out << "        std::vector<PluginCommand> cmds;\n\n";
        out << "        PluginCommand hello;\n";
        out << "        hello.name = \"" << dllName << "_hello\";\n";
        out << "        hello.description = \"Say hello from " << dllName << "\";\n";
        out << "        hello.usage = \"" << dllName << "_hello [name]\";\n";
        out << "        hello.callback = [](const std::vector<std::string>& args) {\n";
        out << "            std::string who = args.empty() ? \"World\" : args[0];\n";
        out << "            std::cout << \"[ " << dllName << " ] Hello, \" << who << \"!\" << std::endl;\n";
        out << "        };\n";
        out << "        cmds.push_back(hello);\n\n";
        out << "        PluginCommand info;\n";
        out << "        info.name = \"" << dllName << "_info\";\n";
        out << "        info.description = \"Show command-set information\";\n";
        out << "        info.usage = \"" << dllName << "_info\";\n";
        out << "        info.callback = [](const std::vector<std::string>&) {\n";
        out << "            std::cout << \"" << dllName << " v\" << kVersion\n";
        out << "                      << \" by \" << kAuthor << std::endl;\n";
        out << "        };\n";
        out << "        cmds.push_back(info);\n";
        out << "        return cmds;\n";
        out << "    }\n\n";
        out << "private:\n";
        out << "    bool m_ready = false;\n";
        out << "};\n\n";
        out << "LIGHT_PLUGIN_API IPlugin* CreatePlugin() {\n";
        out << "    return new " << name << "Plugin();\n";
        out << "}\n\n";
        out << "LIGHT_PLUGIN_API void DestroyPlugin(IPlugin* plugin) {\n";
        out << "    delete plugin;\n";
        out << "}\n";
    }

    // ------------------------------------------------------------
    // 3) CMake 工程
    // ------------------------------------------------------------
    {
        std::ofstream out(root / "CMakeLists.txt", std::ios::binary);
        if (!out.is_open()) return false;
        out << "cmake_minimum_required(VERSION 3.20)\n";
        out << "project(" << dllName << " LANGUAGES CXX)\n\n";
        out << "set(CMAKE_CXX_STANDARD 20)\n";
        out << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";
        out << "if(MSVC)\n";
        out << "    # light.exe uses the static CRT (/MT). Keep the same runtime.\n";
        out << "    set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded$<$<CONFIG:Debug>:Debug>\")\n";
        out << "endif()\n\n";
        out << "add_library(" << dllName << " SHARED src/" << dllName << ".cpp)\n";
        out << "target_include_directories(" << dllName << " PRIVATE include)\n";
        out << "set_target_properties(" << dllName << " PROPERTIES\n";
        out << "    PREFIX \"\"\n";
        out << "    SUFFIX \".dll\"\n";
        out << "    OUTPUT_NAME \"" << dllName << "\")\n";
    }

    // ------------------------------------------------------------
    // 4) 构建脚本 + 说明
    // ------------------------------------------------------------
    {
        std::ofstream out(root / "build.bat", std::ios::binary);
        if (!out.is_open()) return false;
        out << "@echo off\r\n";
        out << "setlocal\r\n";
        out << "if not exist build mkdir build\r\n";
        out << "cmake -S . -B build -A x64\r\n";
        out << "if errorlevel 1 goto :error\r\n";
        out << "cmake --build build --config Release\r\n";
        out << "if errorlevel 1 goto :error\r\n";
        out << "echo.\r\n";
        out << "echo Build OK: build\\Release\\" << dllName << ".dll\r\n";
        out << "echo Copy it next to light.exe.\r\n";
        out << "pause\r\n";
        out << "exit /b 0\r\n";
        out << ":error\r\n";
        out << "echo Build failed.\r\n";
        out << "pause\r\n";
        out << "exit /b 1\r\n";
    }

    {
        std::ofstream out(root / "README.md", std::ios::binary);
        if (!out.is_open()) return false;
        out << "# " << dllName << "\n\n";
        out << "Generated Light api_*.dll command-set template.\n\n";
        out << "## Build\n\n";
        out << "```bat\nbuild.bat\n```\n\n";
        out << "Output: `build/Release/" << dllName << ".dll`.\n\n";
        out << "## Install\n\n";
        out << "Copy the DLL into the same directory as `light.exe`. "
            "Light silently scans and validates every `api_*.dll` at startup.\n\n";
        out << "## Commands\n\n";
        out << "- `" << dllName << "_hello [name]`\n";
        out << "- `" << dllName << "_info`\n\n";
        out << "## Sub-plugins / dependencies\n\n";
        out << "If this DLL imports helper DLLs, place them next to `light.exe` "
            "(or in `plugins/` or `api_deps/`). Light scans imported DLLs "
            "recursively before loading.\n";
    }

    std::cout << "[Template] API template generated: " << root.string() << std::endl;
    return true;
}

bool PluginTemplateGenerator::PackToZip(const std::string& sourceDir, const std::string& zipPath) {
    if (fs::exists(zipPath)) {
        fs::remove(zipPath);
    }

    std::string absSourceDir = fs::absolute(fs::path(sourceDir)).string();
    std::string absZipPath = fs::absolute(fs::path(zipPath)).string();

    if (absSourceDir.back() != '\\' && absSourceDir.back() != '/') {
        absSourceDir += '\\';
    }

    std::cout << "[Template] Source Dir: " << absSourceDir << std::endl;
    std::cout << "[Template] Zip Path: " << absZipPath << std::endl;

    zipFile zf = zipOpen(absZipPath.c_str(), APPEND_STATUS_CREATE);
    if (!zf) {
        std::cerr << "[Template] zipOpen failed!" << std::endl;
        return false;
    }

    std::vector<fs::path> files;
    std::string zipFileName = fs::path(absZipPath).filename().string();

    for (const auto& entry : fs::recursive_directory_iterator(absSourceDir)) {
        if (entry.is_regular_file()) {
            std::string fileName = entry.path().filename().string();
            if (fileName != zipFileName && fileName != ".zip") {
                files.push_back(entry.path());
            }
        }
    }

    if (files.empty()) {
        std::cerr << "[Template] No files found to zip!" << std::endl;
        zipClose(zf, NULL);
        return false;
    }

    std::cout << "[Template] Found " << files.size() << " files to zip" << std::endl;

    bool success = true;

    for (const auto& file : files) {
        std::string fullPath = file.string();
        std::string relativePath = fs::relative(file, fs::path(absSourceDir)).string();

        if (relativePath.empty() || relativePath.find("..") != std::string::npos) {
            std::string tempPath = fullPath;
            size_t pos = tempPath.find(absSourceDir);
            if (pos != std::string::npos) {
                relativePath = tempPath.substr(pos + absSourceDir.length());
            }
        }

        if (relativePath.empty()) {
            relativePath = file.filename().string();
        }

        std::replace(relativePath.begin(), relativePath.end(), '/', '\\');

        if (!relativePath.empty() && relativePath[0] == '\\') {
            relativePath = relativePath.substr(1);
        }

        std::cout << "[Template] Adding: " << relativePath << std::endl;

        FILE* inFile = nullptr;
        fopen_s(&inFile, fullPath.c_str(), "rb");
        if (!inFile) {
            std::cerr << "[Template] Failed to open: " << fullPath << std::endl;
            success = false;
            continue;
        }

        fseek(inFile, 0, SEEK_END);
        long fileSize = ftell(inFile);
        fseek(inFile, 0, SEEK_SET);

        std::vector<char> buffer(fileSize);
        fread(buffer.data(), 1, fileSize, inFile);
        fclose(inFile);

        zip_fileinfo zi;
        memset(&zi, 0, sizeof(zi));
        zi.dosDate = 0;

        int result = zipOpenNewFileInZip(zf, relativePath.c_str(), &zi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
        if (result != ZIP_OK) {
            std::cerr << "[Template] zipOpenNewFileInZip failed for: " << relativePath << std::endl;
            success = false;
            continue;
        }

        if (zipWriteInFileInZip(zf, buffer.data(), fileSize) != ZIP_OK) {
            std::cerr << "[Template] zipWriteInFileInZip failed for: " << relativePath << std::endl;
            success = false;
        }

        zipCloseFileInZip(zf);
    }

    zipClose(zf, NULL);

    if (success && fs::exists(absZipPath) && fs::file_size(absZipPath) > 0) {
        std::cout << "[Template] ZIP created successfully! Size: " << fs::file_size(absZipPath) << " bytes" << std::endl;
        return true;
    }

    std::cerr << "[Template] Failed to create ZIP file" << std::endl;
    return false;
}