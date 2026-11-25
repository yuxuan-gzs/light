// LibraryStore.cpp
// 库商城：内置标准库中文翻译 + 本地库 + 远程下载
#include "LibraryStore.h"
#include "Json.h"
#include "Http.h"
#include "ProjectStore.h"
#include "util.h"
#include <windows.h>

namespace vcb {

std::string LibraryStore::DefaultIndexUrl() {
    return "https://raw.githubusercontent.com/yuxuan-gzs/light/main/vcblocks-libs/index.json";
}

// ---- 内置标准库（中文翻译） ----
static StoreLibrary MakeBuiltin(const std::string& name, const std::string& lang,
                                const std::string& desc,
                                std::initializer_list<std::pair<const char*, const char*>> funcs) {
    StoreLibrary lib;
    lib.name = name;
    lib.lang = lang;
    lib.description = desc;
    lib.builtin = true;
    for (auto& f : funcs) lib.funcs.push_back({ f.first, f.second });
    return lib;
}

std::vector<StoreLibrary> LibraryStore::BuiltinLibraries() {
    std::vector<StoreLibrary> out;
    // ---- C 标准库 ----
    out.push_back(MakeBuiltin("stdio.h", "c", "标准输入输出", {
        {"printf", "打印输出"}, {"scanf", "读取输入"}, {"fopen", "打开文件"},
        {"fclose", "关闭文件"}, {"fread", "读取文件"}, {"fwrite", "写入文件"},
        {"fprintf", "格式化写入文件"}, {"fgets", "读取一行"}, {"fputs", "写入字符串"},
        {"puts", "输出字符串"}, {"putchar", "输出字符"}, {"getchar", "读取字符"},
        {"sprintf", "格式化到字符串"}, {"sscanf", "从字符串读取"}, {"fflush", "刷新缓冲区"},
    }));
    out.push_back(MakeBuiltin("stdlib.h", "c", "标准库（内存/转换/随机）", {
        {"malloc", "分配内存"}, {"free", "释放内存"}, {"calloc", "分配并清零"},
        {"realloc", "重新分配内存"}, {"atoi", "字符串转整数"}, {"atof", "字符串转浮点数"},
        {"rand", "随机数"}, {"srand", "设置随机种子"}, {"exit", "退出程序"},
        {"system", "执行系统命令"}, {"abs", "整数绝对值"}, {"qsort", "快速排序"},
        {"bsearch", "二分查找"}, {"strtol", "字符串转长整数"},
    }));
    out.push_back(MakeBuiltin("string.h", "c", "字符串与内存操作", {
        {"strcpy", "复制字符串"}, {"strncpy", "复制字符串(限长)"}, {"strcat", "连接字符串"},
        {"strlen", "字符串长度"}, {"strcmp", "比较字符串"}, {"strchr", "查找字符"},
        {"strstr", "查找子串"}, {"memcpy", "复制内存"}, {"memset", "填充内存"},
        {"memcmp", "比较内存"}, {"strtok", "分割字符串"}, {"strdup", "复制字符串(新内存)"},
    }));
    out.push_back(MakeBuiltin("math.h", "c", "数学函数", {
        {"sin", "正弦"}, {"cos", "余弦"}, {"tan", "正切"}, {"sqrt", "平方根"},
        {"pow", "幂运算"}, {"fabs", "浮点绝对值"}, {"floor", "向下取整"},
        {"ceil", "向上取整"}, {"log", "自然对数"}, {"log10", "常用对数"},
        {"exp", "指数"}, {"fmod", "取模"}, {"round", "四舍五入"}, {"asin", "反正弦"},
        {"acos", "反余弦"}, {"atan", "反正切"},
    }));

    // ---- C++ 标准库 ----
    out.push_back(MakeBuiltin("iostream", "cpp", "标准输入输出流", {
        {"cout", "标准输出"}, {"cin", "标准输入"}, {"endl", "换行"},
        {"cerr", "错误输出"}, {"clog", "日志输出"}, {"flush", "刷新流"},
    }));
    out.push_back(MakeBuiltin("string", "cpp", "字符串类", {
        {"length", "长度"}, {"size", "大小"}, {"substr", "取子串"},
        {"find", "查找"}, {"append", "追加"}, {"erase", "删除"},
        {"insert", "插入"}, {"compare", "比较"}, {"empty", "是否为空"},
        {"clear", "清空"}, {"replace", "替换"},
    }));
    out.push_back(MakeBuiltin("vector", "cpp", "动态数组", {
        {"push_back", "追加元素"}, {"pop_back", "删除末尾"}, {"size", "元素个数"},
        {"clear", "清空"}, {"insert", "插入元素"}, {"erase", "删除元素"},
        {"at", "访问元素"}, {"empty", "是否为空"}, {"front", "首元素"}, {"back", "末元素"},
    }));
    out.push_back(MakeBuiltin("algorithm", "cpp", "算法库", {
        {"sort", "排序"}, {"find", "查找"}, {"reverse", "反转"},
        {"min", "最小值"}, {"max", "最大值"}, {"swap", "交换"},
        {"count", "计数"}, {"binary_search", "二分查找"}, {"lower_bound", "下界"},
    }));

    // ---- Java 标准库 ----
    out.push_back(MakeBuiltin("System", "java", "系统类", {
        {"out.println", "打印并换行"}, {"out.print", "打印(不换行)"}, {"in", "标准输入"},
        {"currentTimeMillis", "当前毫秒时间"}, {"exit", "退出程序"}, {"gc", "垃圾回收"},
        {"getProperty", "获取系统属性"}, {"arraycopy", "复制数组"},
    }));
    out.push_back(MakeBuiltin("String", "java", "字符串类", {
        {"length", "长度"}, {"substring", "取子串"}, {"indexOf", "查找"},
        {"charAt", "取字符"}, {"equals", "比较相等"}, {"toUpperCase", "转大写"},
        {"toLowerCase", "转小写"}, {"trim", "去首尾空格"}, {"split", "分割"},
        {"replace", "替换"}, {"contains", "是否包含"}, {"startsWith", "是否以..开头"},
    }));
    out.push_back(MakeBuiltin("Math", "java", "数学类", {
        {"sqrt", "平方根"}, {"pow", "幂运算"}, {"abs", "绝对值"},
        {"max", "最大值"}, {"min", "最小值"}, {"random", "随机数"},
        {"floor", "向下取整"}, {"ceil", "向上取整"}, {"round", "四舍五入"},
        {"sin", "正弦"}, {"cos", "余弦"}, {"tan", "正切"},
    }));
    out.push_back(MakeBuiltin("ArrayList", "java", "动态列表", {
        {"add", "添加元素"}, {"get", "获取元素"}, {"size", "元素个数"},
        {"remove", "删除元素"}, {"clear", "清空"}, {"contains", "是否包含"},
        {"indexOf", "查找下标"}, {"isEmpty", "是否为空"},
    }));
    return out;
}

// ---- 本地已安装库 ----
std::vector<StoreLibrary> LibraryStore::ListLocal() {
    std::vector<StoreLibrary> out;
    const char* langs[] = { "c", "cpp", "java" };
    for (const char* lang : langs) {
        std::wstring dir = ProjectStore::LibsDirById(lang);
        std::wstring search = dir + L"*.json";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(search.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring fn = fd.cFileName;
            std::string name = WideToUtf8(fn);
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json") name = name.substr(0, name.size() - 5);
            StoreLibrary lib;
            lib.name = name;
            lib.lang = lang;
            lib.description = "本地库";
            out.push_back(lib);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}

// ---- 远程索引 ----
bool LibraryStore::FetchList(const std::string& indexUrl, std::vector<StoreLibrary>& out, std::string& err) {
    out.clear();
    std::string body;
    if (!HttpGet(indexUrl, body, err)) return false;

    JsonValue root;
    if (!ParseJson(body, root, err)) return false;
    if (!root.IsObject() || !root.Has("libraries")) {
        err = "索引缺少 libraries 字段";
        return false;
    }
    const JsonValue& arr = root.Get("libraries");
    if (!arr.IsArray()) { err = "libraries 不是数组"; return false; }

    for (const auto& item : arr.arr) {
        StoreLibrary lib;
        lib.name = item.Get("name").AsString();
        lib.lang = item.Get("lang").AsString("c");
        lib.description = item.Get("description").AsString();
        const JsonValue& files = item.Get("files");
        if (files.IsArray()) {
            for (const auto& f : files.arr) {
                StoreFile sf;
                sf.path = f.Get("path").AsString();
                sf.url = f.Get("url").AsString();
                if (!sf.url.empty()) lib.files.push_back(sf);
            }
        }
        if (!lib.name.empty()) out.push_back(lib);
    }
    return true;
}

// 把路径安全化：只保留文件名（去掉目录与 ../ 等）
static std::string SafeBaseName(const std::string& path) {
    std::string name = path;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::string out;
    for (char c : name) {
        if (c == '.' || c == '_' || c == '-' || c == ' ' ||
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out += c;
        }
    }
    if (out.empty()) out = "lib";
    return out;
}

bool LibraryStore::Download(const StoreLibrary& lib, std::string& err) {
    if (lib.files.empty()) { err = "库没有可下载的文件"; return false; }
    std::wstring dir = ProjectStore::LibsDirById(lib.lang);
    for (const auto& f : lib.files) {
        std::vector<unsigned char> bytes;
        std::string e;
        if (!HttpGetBytes(f.url, bytes, e)) {
            err = "下载失败 " + f.path + ": " + e;
            return false;
        }
        std::wstring outPath = dir + Utf8ToWide(SafeBaseName(f.path));
        if (!WriteFileBytes(outPath, bytes)) {
            err = "写入失败 " + WideToUtf8(outPath);
            return false;
        }
    }
    return true;
}

bool LibraryStore::InstallBuiltin(const StoreLibrary& lib, std::string& err) {
    if (!lib.builtin || lib.funcs.empty()) { err = "空的内置库"; return false; }

    JsonValue root = JsonValue::MakeObject();
    root.Set("name", JsonValue::MakeString(lib.name));
    root.Set("language", JsonValue::MakeString("中文"));
    root.Set("locale", JsonValue::MakeString("zh-CN"));

    JsonValue strings = JsonValue::MakeObject();
    JsonValue funcs = JsonValue::MakeArray();
    for (const auto& f : lib.funcs) {
        strings.Set(f.first, JsonValue::MakeString(f.second));
        JsonValue o = JsonValue::MakeObject();
        o.Set("name", JsonValue::MakeString(f.first));
        o.Set("translation", JsonValue::MakeString(f.second));
        o.Set("description", JsonValue::MakeString(f.second));
        funcs.Push(o);
    }
    root.Set("strings", strings);
    root.Set("functions", funcs);

    std::string json = DumpJson(root);
    std::wstring outPath = ProjectStore::LibsDirById(lib.lang) + Utf8ToWide(lib.name) + L".json";
    if (!WriteFileText(outPath, json)) {
        err = "写入失败 " + WideToUtf8(outPath);
        return false;
    }
    return true;
}

} // namespace vcb
