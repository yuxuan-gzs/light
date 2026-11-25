// selftest.cpp - 控制台自检：验证核心逻辑
#include <cstdio>
#include <string>
#include "core/util.h"
#include "core/Json.h"
#include "core/BlockModel.h"
#include "core/BlockLibrary.h"
#include "core/Translator.h"
#include "core/CodeGenerator.h"
#include "core/Decompiler.h"
#include "core/LibraryScanner.h"
#include "core/DllBuilder.h"

using namespace vcb;

static int g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("[PASS] %s\n", name); } \
    else { printf("[FAIL] %s\n", name); ++g_fail; } \
} while (0)

int main() {
    // ---- 1. JSON 往返 ----
    {
        std::string src = "{\"name\":\"core\",\"language\":\"简体中文\",\"locale\":\"zh-CN\","
                          "\"strings\":{\"print {0}\":\"打印 {0}\",\"if {0} then\":\"如果 {0} 那么\"}}";
        JsonValue v; std::string err;
        bool ok = ParseJson(src, v, err);
        CHECK(ok, "JSON parse");
        if (ok) {
            std::string dumped = DumpJson(v);
            JsonValue v2; std::string err2;
            bool ok2 = ParseJson(dumped, v2, err2);
            CHECK(ok2 && v2.Get("strings").Get("print {0}").AsString() == "打印 {0}", "JSON round-trip");
        }
    }

    // ---- 2. 翻译 + 英文回退 ----
    {
        Translator t;
        t.locale = "zh-CN";
        CHECK(t.Translate("print {0}") == "打印 {0}", "zh-CN translate print");
        CHECK(t.Translate("if {0} then") == "如果 {0} 那么", "zh-CN translate if");
        CHECK(t.Translate("some unknown text") == "some unknown text", "fallback to English");
    }

    // ---- 3. 代码生成 ----
    Program p = BlockLibrary::MakeSampleProgram();
    {
        std::string err;
        std::string c = CodeGenerator::GenerateProgram(p, err);
        CHECK(!c.empty() && err.empty(), "codegen produces C");
        printf("---- generated C ----\n%s\n---- end ----\n", c.c_str());
        WriteFileText(L"D:\\YX\\VCBlocks\\build\\test_program.c", c);
        CHECK(c.find("int main(void)") != std::string::npos, "codegen has main");
        CHECK(c.find("VBC_PRINT_TEXT") != std::string::npos, "codegen has print macro");
    }

    // ---- 4. 反编译往返 ----
    {
        std::string err;
        std::string c = CodeGenerator::GenerateProgram(p, err);
        Program back = Decompiler::Decompile(c, err);
        printf("decompiled top-level blocks: %zu\n", back.blocks.size());
        CHECK(back.blocks.size() == p.blocks.size(), "decompile round-trip block count");
        if (!back.blocks.empty()) {
            CHECK(back.blocks[0].typeId == "print", "decompile print");
        }
        if (back.blocks.size() >= 3) {
            CHECK(back.blocks[2].typeId == "repeat", "decompile repeat");
            CHECK(!back.blocks[2].children.empty() && back.blocks[2].children[0].typeId == "printNum", "decompile nested printNum");
        }
    }

    // ---- 5. 库扫描 ----
    {
        std::string lib =
            "// Get the file size\n"
            "int get_file_size(const char* path);\n\n"
            "// Open and read all data\n"
            "char* open_and_read(const char* name);\n\n"
            "void init_buffer(void* buf, int size);\n";
        LibraryScanner::Result r; std::string err;
        bool ok = LibraryScanner::Scan(lib, "mylib", "简体中文", "zh-CN", r, err);
        CHECK(ok, "library scan ok");
        if (ok) {
            printf("found %zu functions\n", r.functions.size());
            CHECK(r.functions.size() == 3, "scan finds 3 functions");
            std::string json = LibraryScanner::BuildTranslationJson(r);
            printf("---- translation json ----\n%s\n---- end ----\n", json.c_str());
            WriteFileText(L"D:\\YX\\VCBlocks\\build\\mylib.json", json);
            CHECK(json.find("get_file_size") != std::string::npos, "json contains func name");
            if (r.translations.count("get_file_size")) {
                printf("get_file_size -> %s\n", r.translations["get_file_size"].c_str());
            }
        }
    }

    // ---- 6. DLL 源码生成 ----
    {
        DllBuilder::Options opt;
        opt.dllName = "YXG_TestPlugin";
        opt.description = "test";
        std::string err;
        std::string src = DllBuilder::GenerateSource(p, opt, err);
        CHECK(!src.empty() && err.empty(), "dll source generated");
        WriteFileText(L"D:\\YX\\VCBlocks\\build\\test_plugin.c", src);
        CHECK(src.find("YXG_RegisterPlugin") != std::string::npos, "dll exports register");
        CHECK(src.find("YXG_GetExtensionInfo") != std::string::npos, "dll exports info");
        CHECK(src.find("YXG_UnregisterPlugin") != std::string::npos, "dll exports unregister");
        CHECK(src.find("vbc_run") != std::string::npos, "dll has run func");
    }

    printf("==== %s ====\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    return g_fail;
}
