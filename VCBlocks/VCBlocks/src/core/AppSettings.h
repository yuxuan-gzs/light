// AppSettings.h
// 全局设置：界面语言、背景样式、编译配置、最近项目。持久化到 Cache\vC\settings.json，
// 下次启动恢复。单例。
#pragma once
#include <string>
#include <vector>
#include "CompileConfig.h"
#include "CodeGenerator.h"

namespace vcb {

struct AppSettings {
    // 界面
    std::string locale = "zh-CN";          // 界面语言
    int backgroundStyle = 0;               // 0=纯色, 1=网格, 2=图片
    std::string backgroundColor = "#1E1E1E"; // 背景色（0xRRGGBB）
    std::wstring backgroundImage;          // 背景图片路径

    // 编译
    CompileConfig compile;                 // 正式/测试版、/MT /MTd、输出 .dll/.exe
    TargetLang defaultLang = TargetLang::C; // 新建项目默认语言

    // 项目
    std::string lastProject;               // 最近打开的项目名
    std::vector<std::string> recentProjects; // 最近项目列表

    // 单例
    static AppSettings& Instance();

    // 读写 settings.json
    bool Load(std::string& err);
    bool Save(std::string& err) const;
    static std::wstring Path();

    // 记录最近项目（去重、限制数量）
    void PushRecent(const std::string& project, int maxCount = 10);
};

} // namespace vcb
