// ProjectStore.h
// 项目数据统一存储在 <exeDir>\Cache\vC\<项目名称>\ 目录下
#pragma once
#include <string>
#include <vector>
#include "CodeGenerator.h"

namespace vcb {

class ProjectStore {
public:
    // <exeDir>\Cache\vC
    static std::wstring RootDir();
    // <root>\<project>
    static std::wstring ProjectDir(const std::string& project);
    static std::wstring LangDir(const std::string& project);   // 翻译文件目录
    static std::wstring SrcDir(const std::string& project);    // 生成 C 源码
    static std::wstring OutDir(const std::string& project);    // 编译产物（exe/dll）

    static std::wstring BlocksPath(const std::string& project);        // blocks.json
    static std::wstring SettingsPath(const std::string& project);      // settings.json
    static std::wstring LangPath(const std::string& project, const std::string& library); // <库名称>.json

    // ---- 库（按语言分开放）----
    // 语言标识："c"/"cpp"/"java"
    static std::string LangId(TargetLang lang);
    // <root>\libs
    static std::wstring LibsRoot();
    // <root>\libs\<lang>
    static std::wstring LibsDir(TargetLang lang);
    static std::wstring LibsDirById(const std::string& langId);

    // 列出已有项目名
    static std::vector<std::string> ListProjects();

    // 项目名合法性：仅允许字母数字、下划线、连字符、点、空格、CJK
    static bool IsValidProjectName(const std::string& name);
    // 从库文件名/路径推导库名称（不含扩展名、无非法字符）
    static std::string SanitizeLibraryName(const std::string& raw);
};

} // namespace vcb
