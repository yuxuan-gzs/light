#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <regex>
#include <algorithm>

namespace fs = std::filesystem;

class JavaCodeReplacer {
private:
    std::string rootPath;
    int processedFiles = 0;
    int modifiedFiles = 0;

    // 读取文件内容
    std::string readFile(const fs::path& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return "";
        }
        std::string content((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        file.close();
        return content;
    }

    // 写入文件内容
    bool writeFile(const fs::path& filePath, const std::string& content) {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            return false;
        }
        file << content;
        file.close();
        return true;
    }

    // 备份文件
    void backupFile(const fs::path& filePath) {
        fs::path backupPath = filePath;
        backupPath += ".backup";
        if (!fs::exists(backupPath)) {
            fs::copy_file(filePath, backupPath);
            std::cout << "  已备份: " << filePath.filename().string() << ".backup" << std::endl;
        }
    }

    // 替换 ItemStack API
    std::string replaceItemStackAPI(const std::string& content) {
        std::string result = content;

        // 1. stack.hasTag() -> stack.getOrCreateTag() (但我们需要更智能的处理)
        // 将 if (stack.hasTag()) 替换为 CompoundTag tag = stack.getOrCreateTag(); if (!tag.isEmpty())
        std::regex hasTagPattern(R"((\w+)\.hasTag\(\))");
        std::smatch match;

        // 2. 处理常见的 getTag() 模式
        // 先处理 .getTag() 单独使用的情况
        std::regex getTagPattern(R"((\w+)\.getTag\(\))");
        result = std::regex_replace(result, getTagPattern, "$1.getOrCreateTag()");

        // 3. 处理更复杂的模式：if (stack.hasTag() && stack.getTag().contains(...))
        // 这个需要更精细的处理，这里简化处理
        std::regex complexPattern(R"(if\s*\(\s*(\w+)\.hasTag\(\)\s*&&\s*\1\.getTag\(\)\.contains\(([^)]+)\)\s*\))");
        result = std::regex_replace(result, complexPattern,
            "if (!$1.getOrCreateTag().contains($2))");

        // 4. 处理 if (stack.hasTag()) { ... stack.getTag() ... }
        // 这比较复杂，需要更智能的替换，这里做基本处理

        return result;
    }

    // 替换 ResourceLocation
    std::string replaceResourceLocation(const std::string& content) {
        std::string result = content;

        // 替换 new ResourceLocation("modid", "path") -> ResourceLocation.fromNamespaceAndPath("modid", "path")
        std::regex resourceLocPattern(R"(new\s+ResourceLocation\s*\(\s*\"([^\"]+)\"\s*,\s*\"([^\"]+)\"\s*\))");
        result = std::regex_replace(result, resourceLocPattern,
            "ResourceLocation.fromNamespaceAndPath(\"$1\", \"$2\")");

        // 替换 new ResourceLocation("path") -> ResourceLocation.fromNamespaceAndPath("modid", "path")
        // 这个更复杂，需要知道默认的 modid，这里简单处理
        std::regex resourceLocSinglePattern(R"(new\s+ResourceLocation\s*\(\s*\"([^\"]+)\"\s*\))");
        // 注意：这里不能简单替换，因为需要知道 namespace，暂时保留

        return result;
    }

    // 替换 NeoForgeRegistries.ITEMS -> BuiltInRegistries.ITEM
    std::string replaceRegistries(const std::string& content) {
        std::string result = content;

        // NeoForgeRegistries.ITEMS -> BuiltInRegistries.ITEM
        result = std::regex_replace(result,
            std::regex(R"(NeoForgeRegistries\.ITEMS)"),
            "BuiltInRegistries.ITEM");

        // NeoForgeRegistries.SOUND_EVENTS -> BuiltInRegistries.SOUND_EVENT
        result = std::regex_replace(result,
            std::regex(R"(NeoForgeRegistries\.SOUND_EVENTS)"),
            "BuiltInRegistries.SOUND_EVENT");

        return result;
    }

    // 替换事件监听器
    std::string replaceEventListeners(const std::string& content) {
        std::string result = content;

        // 替换 ClientTickEvent.Phase -> TickPhase
        result = std::regex_replace(result,
            std::regex(R"(ClientTickEvent\.Phase\.(START|END))"),
            "TickPhase.$1");

        // 替换 ServerTickEvent.Phase -> TickPhase
        result = std::regex_replace(result,
            std::regex(R"(ServerTickEvent\.Phase\.(START|END))"),
            "TickPhase.$1");

        // 简化处理 getPhase() 检查
        result = std::regex_replace(result,
            std::regex(R"(if\s*\(\s*event\.getPhase\(\)\s*!=\s*TickPhase\.END\s*\)\s*(?:\{\s*return;\s*\})?)"),
            "// 已移除 getPhase() 检查");

        return result;
    }

    // 替换 FriendlyByteBuf 方法
    std::string replaceFriendlyByteBuf(const std::string& content) {
        std::string result = content;

        // m_130130_(int) -> writeInt(int)
        result = std::regex_replace(result,
            std::regex(R"(buffer\.m_130130_\()"),
            "buffer.writeInt(");

        // m_130242_() -> readInt()
        result = std::regex_replace(result,
            std::regex(R"(buffer\.m_130242_\(\))"),
            "buffer.readInt()");

        return result;
    }

    // 替换 playSound 方法
    std::string replacePlaySound(const std::string& content) {
        std::string result = content;

        // 处理 SoundEvents.GENERIC_EXPLODE -> SoundEvents.GENERIC_EXPLODE.value()
        std::regex soundPattern(R"(SoundEvents\.([A-Z_]+)(?!\.value\(\)))");
        result = std::regex_replace(result, soundPattern,
            "SoundEvents.$1.value()");

        return result;
    }

public:
    JavaCodeReplacer(const std::string& path) : rootPath(path) {}

    void processFile(const fs::path& filePath) {
        if (filePath.extension() != ".java") {
            return;
        }

        std::cout << "处理文件: " << filePath.string() << std::endl;

        std::string content = readFile(filePath);
        if (content.empty()) {
            return;
        }

        std::string newContent = content;
        bool modified = false;

        // 应用所有替换
        std::string tempContent = replaceItemStackAPI(newContent);
        if (tempContent != newContent) {
            newContent = tempContent;
            modified = true;
        }

        tempContent = replaceResourceLocation(newContent);
        if (tempContent != newContent) {
            newContent = tempContent;
            modified = true;
        }

        tempContent = replaceRegistries(newContent);
        if (tempContent != newContent) {
            newContent = tempContent;
            modified = true;
        }

        tempContent = replaceEventListeners(newContent);
        if (tempContent != newContent) {
            newContent = tempContent;
            modified = true;
        }

        tempContent = replaceFriendlyByteBuf(newContent);
        if (tempContent != newContent) {
            newContent = tempContent;
            modified = true;
        }

        tempContent = replacePlaySound(newContent);
        if (tempContent != newContent) {
            newContent = tempContent;
            modified = true;
        }

        if (modified) {
            backupFile(filePath);
            if (writeFile(filePath, newContent)) {
                modifiedFiles++;
                std::cout << "  ✓ 已修改" << std::endl;
            }
            else {
                std::cout << "  ✗ 写入失败" << std::endl;
            }
        }
        else {
            std::cout << "  - 无需修改" << std::endl;
        }

        processedFiles++;
    }

    void processDirectory(const fs::path& dirPath) {
        try {
            for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
                if (fs::is_regular_file(entry.path())) {
                    processFile(entry.path());
                }
            }
        }
        catch (const fs::filesystem_error& e) {
            std::cerr << "遍历目录错误: " << e.what() << std::endl;
        }
    }

    void run() {
        std::cout << "开始处理目录: " << rootPath << std::endl;
        std::cout << "========================================" << std::endl;

        fs::path root(rootPath);
        if (!fs::exists(root)) {
            std::cerr << "错误: 目录不存在!" << std::endl;
            return;
        }

        processDirectory(root);

        std::cout << "========================================" << std::endl;
        std::cout << "处理完成!" << std::endl;
        std::cout << "总文件数: " << processedFiles << std::endl;
        std::cout << "已修改文件: " << modifiedFiles << std::endl;
        std::cout << "备份文件以 .backup 结尾" << std::endl;
    }
};

int main() {
    std::string path;

    std::cout << "Java 代码批量替换工具" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "请选择操作:" << std::endl;
    std::cout << "1. 使用默认路径 (D:\\Desktop\\verity\\src)" << std::endl;
    std::cout << "2. 输入自定义路径" << std::endl;
    std::cout << "请输入选择 (1 或 2): ";

    int choice;
    std::cin >> choice;

    if (choice == 2) {
        std::cout << "请输入Java源代码目录路径: ";
        std::cin >> path;
    }
    else {
        path = "D:\\Desktop\\verity\\src";
    }

    // 检查路径是否存在
    if (!fs::exists(path)) {
        std::cerr << "错误: 路径 '" << path << "' 不存在!" << std::endl;
        return 1;
    }

    std::cout << "使用路径: " << path << std::endl;
    std::cout << "是否继续? (y/n): ";
    char confirm;
    std::cin >> confirm;

    if (confirm != 'y' && confirm != 'Y') {
        std::cout << "已取消" << std::endl;
        return 0;
    }

    JavaCodeReplacer replacer(path);
    replacer.run();

    std::cout << "\n按 Enter 键退出...";
    std::cin.ignore();
    std::cin.get();

    return 0;
}