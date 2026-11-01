// LibraryStore.h
// 库商城：内置 C/C++/Java 标准库（中文翻译）+ 本地已装库 + 远程仓库下载。
#pragma once
#include <string>
#include <vector>

namespace vcb {

struct StoreFile {
    std::string path;  // 相对路径（含文件名），保存到 <libs>\<lang>\<path>
    std::string url;   // 下载地址
};

struct StoreLibrary {
    std::string name;
    std::string lang;        // "c" / "cpp" / "java"
    std::string description;
    bool builtin = false;    // 内置库：无文件，直接生成翻译 json
    std::vector<StoreFile> files;                       // 远程库：下载文件
    std::vector<std::pair<std::string, std::string>> funcs; // 内置库：函数名 -> 中文翻译
};

class LibraryStore {
public:
    // 默认远程仓库索引地址（可选，拉取失败不影响内置库）
    static std::string DefaultIndexUrl();

    // 内置 C/C++/Java 标准库（中文翻译，不依赖网络）
    static std::vector<StoreLibrary> BuiltinLibraries();

    // 列出本地已安装的库（Cache\vC\libs\<lang>\ 下的 *.json）
    static std::vector<StoreLibrary> ListLocal();

    // 从索引地址拉取远程库列表（索引为 JSON）
    static bool FetchList(const std::string& indexUrl, std::vector<StoreLibrary>& out, std::string& err);

    // 下载远程库到对应语言目录
    static bool Download(const StoreLibrary& lib, std::string& err);

    // 安装内置库：生成 <libs>\<lang>\<name>.json 翻译文件
    static bool InstallBuiltin(const StoreLibrary& lib, std::string& err);
};

} // namespace vcb
