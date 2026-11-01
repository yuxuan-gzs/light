// settest.cpp - 验证 AppSettings 持久化 + CompileConfig
#include "core/AppSettings.h"
#include "core/CompileConfig.h"
#include "core/ProjectStore.h"
#include <cstdio>

int main() {
    using namespace vcb;
    AppSettings& s = AppSettings::Instance();

    // 设置测试值
    s.locale = "zh-CN";
    s.backgroundStyle = 2;
    s.compile.config = BuildConfig::Debug;
    s.compile.runtime = RuntimeLib::StaticDebug;
    s.compile.output = OutputType::Dll;
    s.compile.pluginDll = true;
    s.compile.runAfterBuild = true;
    s.defaultLang = TargetLang::Cpp;
    s.PushRecent("ProjectA");
    s.PushRecent("ProjectB");
    s.PushRecent("ProjectA"); // 去重
    s.PushRecent("ProjectC");

    // 保存
    std::string err;
    if (!s.Save(err)) { printf("SAVE FAIL: %s\n", err.c_str()); return 1; }
    printf("SAVE OK -> %ls\n", AppSettings::Path().c_str());

    // 清空再加载
    AppSettings t;
    // 用新实例验证：直接重新 Load 到同一单例（先重置）
    AppSettings& r = AppSettings::Instance();
    r.locale = ""; r.recentProjects.clear();
    if (!r.Load(err)) { printf("LOAD FAIL: %s\n", err.c_str()); return 1; }
    printf("LOAD OK\n");
    printf("  locale=%s\n", r.locale.c_str());
    printf("  backgroundStyle=%d\n", r.backgroundStyle);
    printf("  config=%d runtime=%d output=%d pluginDll=%d\n",
        (int)r.compile.config, (int)r.compile.runtime, (int)r.compile.output, (int)r.compile.pluginDll);
    printf("  defaultLang=%s\n", (r.defaultLang == TargetLang::Cpp) ? "cpp" : "?");
    printf("  recentProjects=%d [", (int)r.recentProjects.size());
    for (auto& p : r.recentProjects) printf("%s ", p.c_str());
    printf("]\n");

    // CompileConfig 参数
    printf("MsBuildFlags(Debug+MTd) = %s\n", r.compile.MsBuildFlags().c_str());
    CompileConfig rel; rel.config = BuildConfig::Release; rel.runtime = RuntimeLib::StaticRelease;
    printf("MsBuildFlags(Release+MT) = %s\n", rel.MsBuildFlags().c_str());

    printf("=== settest 完成 ===\n");
    return 0;
}
