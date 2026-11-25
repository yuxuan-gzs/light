// lines_test.cpp
// 验证：GenerateProgram 的行号映射（编译错误跳转的基础）
#include <cstdio>
#include <string>
#include <vector>
#include "../VCBlocks/src/core/BlockLibrary.h"
#include "../VCBlocks/src/core/CodeGenerator.h"

using namespace vcb;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++g_fail; } else { printf("OK:   %s\n", msg); } } while (0)

// 复制 MainWindow 的 ParseErrorLine 逻辑，验证解析
static int ParseErrorLine(const std::string& log) {
    for (size_t i = 0; i + 1 < log.size(); ++i) {
        char c = log[i];
        if ((c == ':' || c == '(') && isdigit((unsigned char)log[i + 1])) {
            size_t j = i + 1;
            int num = 0;
            while (j < log.size() && isdigit((unsigned char)log[j])) {
                num = num * 10 + (log[j] - '0');
                if (num > 1000000) break;
                ++j;
            }
            if (num > 0 && num < 1000000) return num;
            i = j - 1;
        }
    }
    return -1;
}

int main() {
    // 1. 行号解析
    CHECK(ParseErrorLine("tcc: error: MyProject.c:7: expected ';'") == 7, "TCC 格式行号");
    CHECK(ParseErrorLine("MyProject.java:3: error: ';' expected") == 3, "javac 格式行号");
    CHECK(ParseErrorLine("MyProject.cpp(12) : error C2065") == 12, "MSVC 格式行号");

    // 2. 行号映射
    Program p = BlockLibrary::MakeSampleProgram();  // 3 个顶层积木
    std::vector<int> lines;
    std::string err;
    std::string code = CodeGenerator::GenerateProgram(p, TargetLang::C, err, &lines);
    CHECK(err.empty(), "C 生成无错误");
    CHECK(lines.size() == p.blocks.size(), "lineStarts 数量 = 顶层积木数");

    bool mono = true;
    for (size_t i = 1; i < lines.size(); ++i) if (lines[i] <= lines[i-1]) mono = false;
    CHECK(mono && !lines.empty() && lines[0] > 0, "lineStarts 单调递增且 > 0");

    // 3. 验证 lineStarts[0] 对应代码行是 print 语句
    if (!lines.empty()) {
        int ln = lines[0];
        int cur = 1;
        std::string target;
        for (char ch : code) {
            if (ch == '\n') ++cur;
            else if (cur == ln) target += ch;
        }
        CHECK(target.find("VBC_PRINT_TEXT") != std::string::npos, "第 1 个积木行号对应 print 语句");
    }

    printf("\n%s (fail=%d)\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
