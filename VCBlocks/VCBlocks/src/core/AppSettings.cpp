// AppSettings.cpp
// 全局设置持久化：读写 Cache\vC\settings.json
#include "AppSettings.h"
#include "Json.h"
#include "ProjectStore.h"
#include "util.h"

namespace vcb {

AppSettings& AppSettings::Instance() {
    static AppSettings s;
    return s;
}

std::wstring AppSettings::Path() {
    return ProjectStore::RootDir() + L"settings.json";
}

static std::string LangToString(TargetLang lang) {
    if (lang == TargetLang::Cpp) return "cpp";
    if (lang == TargetLang::Java) return "java";
    return "c";
}
static TargetLang LangFromString(const std::string& s) {
    if (s == "cpp") return TargetLang::Cpp;
    if (s == "java") return TargetLang::Java;
    return TargetLang::C;
}

bool AppSettings::Load(std::string& err) {
    std::string text;
    if (!ReadFileText(Path(), text)) {
        // 首次运行无设置文件，用默认值，不算错误
        return true;
    }
    JsonValue root;
    if (!ParseJson(text, root, err)) return false;
    if (!root.IsObject()) { err = "settings.json 根不是对象"; return false; }

    locale = root.Get("locale").AsString(locale);
    backgroundStyle = root.Get("backgroundStyle").AsInt(backgroundStyle);
    backgroundColor = root.Get("backgroundColor").AsString(backgroundColor);
    backgroundImage = Utf8ToWide(root.Get("backgroundImage").AsString(""));
    defaultLang = LangFromString(root.Get("defaultLang").AsString("c"));
    lastProject = root.Get("lastProject").AsString("");

    // 编译配置
    if (root.Has("compile") && root.Get("compile").IsObject()) {
        const JsonValue& c = root.Get("compile");
        compile.config = (c.Get("config").AsInt(0) == 1) ? BuildConfig::Debug : BuildConfig::Release;
        compile.runtime = (c.Get("runtime").AsInt(0) == 1) ? RuntimeLib::StaticDebug : RuntimeLib::StaticRelease;
        compile.output = (c.Get("output").AsInt(0) == 1) ? OutputType::Dll : OutputType::Exe;
        compile.pluginDll = c.Get("pluginDll").AsBool(false);
        compile.optimize = c.Get("optimize").AsInt(compile.optimize);
        compile.warnLevel = c.Get("warnLevel").AsInt(compile.warnLevel);
        compile.runAfterBuild = c.Get("runAfterBuild").AsBool(false);
        compile.symbols = c.Get("symbols").AsBool(false);
    }

    recentProjects.clear();
    if (root.Has("recentProjects") && root.Get("recentProjects").IsArray()) {
        for (const auto& p : root.Get("recentProjects").arr) {
            std::string n = p.AsString();
            if (!n.empty()) recentProjects.push_back(n);
        }
    }
    return true;
}

bool AppSettings::Save(std::string& err) const {
    JsonValue root = JsonValue::MakeObject();
    root.Set("locale", JsonValue::MakeString(locale));
    root.Set("backgroundStyle", JsonValue::MakeNumber(backgroundStyle));
    root.Set("backgroundColor", JsonValue::MakeString(backgroundColor));
    root.Set("backgroundImage", JsonValue::MakeString(WideToUtf8(backgroundImage)));
    root.Set("defaultLang", JsonValue::MakeString(LangToString(defaultLang)));
    root.Set("lastProject", JsonValue::MakeString(lastProject));

    JsonValue c = JsonValue::MakeObject();
    c.Set("config", JsonValue::MakeNumber(compile.config == BuildConfig::Debug ? 1 : 0));
    c.Set("runtime", JsonValue::MakeNumber(compile.runtime == RuntimeLib::StaticDebug ? 1 : 0));
    c.Set("output", JsonValue::MakeNumber(compile.output == OutputType::Dll ? 1 : 0));
    c.Set("pluginDll", JsonValue::MakeBool(compile.pluginDll));
    c.Set("optimize", JsonValue::MakeNumber(compile.optimize));
    c.Set("warnLevel", JsonValue::MakeNumber(compile.warnLevel));
    c.Set("runAfterBuild", JsonValue::MakeBool(compile.runAfterBuild));
    c.Set("symbols", JsonValue::MakeBool(compile.symbols));
    root.Set("compile", c);

    JsonValue arr = JsonValue::MakeArray();
    for (const auto& p : recentProjects) arr.Push(JsonValue::MakeString(p));
    root.Set("recentProjects", arr);

    if (!WriteFileText(Path(), DumpJson(root))) {
        err = "写入 settings.json 失败";
        return false;
    }
    return true;
}

void AppSettings::PushRecent(const std::string& project, int maxCount) {
    if (project.empty()) return;
    for (auto it = recentProjects.begin(); it != recentProjects.end(); ++it) {
        if (*it == project) { recentProjects.erase(it); break; }
    }
    recentProjects.insert(recentProjects.begin(), project);
    if ((int)recentProjects.size() > maxCount) recentProjects.resize(maxCount);
    lastProject = project;
}

} // namespace vcb
