// storetest.cpp - 验证库商城：HTTP 下载 + 索引解析
#include "core/Http.h"
#include "core/LibraryStore.h"
#include "core/Json.h"
#include "core/ProjectStore.h"
#include <cstdio>

int main() {
    using namespace vcb;

    // 1. 测试 HTTP 下载（WinHTTP）
    std::string body, err;
    bool ok = HttpGet("https://httpbin.org/get", body, err);
    printf("HttpGet httpbin.org/get: %s (err=%s)\n", ok ? "OK" : "FAIL", err.c_str());
    if (ok) {
        printf("  body 前 80 字符: %.80s\n", body.c_str());
    }

    // 2. 测试索引 JSON 解析（本地字符串，不依赖网络）
    const char* indexJson =
        "{\"libraries\":["
        "{\"name\":\"demo-c\",\"lang\":\"c\",\"description\":\"C demo lib\","
        " \"files\":[{\"path\":\"demo.h\",\"url\":\"https://example.com/demo.h\"}]},"
        "{\"name\":\"demo-java\",\"lang\":\"java\",\"description\":\"Java demo lib\","
        " \"files\":[{\"path\":\"Demo.java\",\"url\":\"https://example.com/Demo.java\"}]}"
        "]}";
    JsonValue root;
    std::string perr;
    if (!ParseJson(indexJson, root, perr)) { printf("ParseJson FAIL: %s\n", perr.c_str()); return 1; }
    printf("ParseJson OK: %d 个库\n", (int)root.Get("libraries").arr.size());

    // 3. 验证库目录按语言分开
    std::wstring cDir = ProjectStore::LibsDir(TargetLang::C);
    std::wstring cppDir = ProjectStore::LibsDir(TargetLang::Cpp);
    std::wstring javaDir = ProjectStore::LibsDir(TargetLang::Java);
    printf("LibsDir(c)=%ls\n", cDir.c_str());
    printf("LibsDir(cpp)=%ls\n", cppDir.c_str());
    printf("LibsDir(java)=%ls\n", javaDir.c_str());

    printf("=== storetest 完成 ===\n");
    return 0;
}
