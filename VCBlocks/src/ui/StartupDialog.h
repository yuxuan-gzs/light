// StartupDialog.h
// 启动弹窗：无项目时提供「创建项目」，有项目时提供「创建 + 打开」。
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "core/CodeGenerator.h"

namespace vcb {

struct StartupResult {
    bool ok = false;             // 是否确认（false = 取消/关闭）
    bool createNew = false;      // true=创建新项目，false=打开已有项目
    std::string projectName;     // 项目名
    TargetLang lang = TargetLang::C;  // 项目语言
    std::wstring location;       // 保存位置（默认 Cache\vC）
};

class StartupDialog {
public:
    // 模态显示启动弹窗；recent 为最近项目列表，返回用户选择
    static StartupResult Show(HINSTANCE inst, HWND owner, const std::vector<std::string>& recent);
};

} // namespace vcb
