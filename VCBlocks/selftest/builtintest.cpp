// builtintest.cpp - 验证内置标准库 + 安装
#include "core/LibraryStore.h"
#include "core/ProjectStore.h"
#include "core/util.h"
#include <cstdio>

int main() {
    using namespace vcb;

    auto libs = LibraryStore::BuiltinLibraries();
    printf("内置库数量: %d\n", (int)libs.size());
    int totalFuncs = 0;
    for (const auto& l : libs) totalFuncs += (int)l.funcs.size();
    printf("内置函数总数: %d\n", totalFuncs);

    // 安装前 3 个内置库
    int installed = 0;
    std::string err;
    for (int i = 0; i < 3 && i < (int)libs.size(); ++i) {
        if (LibraryStore::InstallBuiltin(libs[i], err)) installed++;
        else printf("安装 %s 失败: %s\n", libs[i].name.c_str(), err.c_str());
    }
    printf("已安装 %d 个内置库\n", installed);

    // 列出本地库
    auto locals = LibraryStore::ListLocal();
    printf("本地库数量: %d\n", (int)locals.size());
    for (const auto& l : locals) {
        printf("  [%s] %s\n", l.lang.c_str(), l.name.c_str());
    }

    // 验证 Cache\vC 位置
    printf("Cache\\vC 根目录: %ls\n", ProjectStore::RootDir().c_str());

    printf("=== builtintest 完成 ===\n");
    return 0;
}
