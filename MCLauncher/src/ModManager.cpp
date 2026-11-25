// ModManager.cpp — Mod 前置检测与补充实现
#include "ModManager.h"
#include "Zip.h"
#include "Json.h"
#include "Http.h"
#include "Theme.h"

#include <filesystem>
#include <set>
#include <algorithm>

namespace modmgr {

namespace fs = std::filesystem;

// 已知前置库（常见依赖 id → 搜索提示）
static const std::vector<Preset> kPresets = {
    {"fabric-api", "Fabric API"},
    {"fabric", "Fabric API"},
    {"cloth-config", "Cloth Config"},
    {"cloth_config", "Cloth Config"},
    {"modmenu", "Mod Menu"},
    {"sodium", "Sodium"},
    {"iris", "Iris Shaders"},
    {"lithium", "Lithium"},
    {"indium", "Indium"},
    {"roughlyenoughitems", "Roughly Enough Items"},
    {"jei", "Just Enough Items"},
    {"architectury", "Architectury API"},
    {"geckolib", "GeckoLib"},
    {"forge", "Forge"},
    {"optifine", "OptiFine"},
    {"fabricloader", ""},
    {"fabric_loader", ""},
};
const std::vector<Preset>& Presets() { return kPresets; }

// 排除的内置/非 mod 依赖 id
static bool isBuiltin(const std::string& id) {
    return id == "minecraft" || id == "java" || id == "fabricloader" ||
           id == "forge" || id == "fabric" || id == "quilt_loader" ||
           id == "neoforge" || id.empty();
}

static std::string lowerId(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// 解析 fabric.mod.json：提取 id / name / depends
static bool parseFabric(const std::string& text, ModInfo& m) {
    try {
        json::Value j = json::parse(text);
        m.id = j.get("id").asString();
        m.name = j.get("name").asString();
        if (m.name.empty()) m.name = m.id;
        m.loader = "fabric";
        const json::Value& dep = j.get("depends");
        if (dep.isObject()) {
            for (const auto& [k, v] : dep.obj) {
                (void)v;
                m.depends.push_back(k);
            }
        }
        return !m.id.empty();
    } catch (...) { return false; }
}

// 解析 Forge META-INF/mods.toml：提取 modId / 依赖
static bool parseForge(const std::string& text, ModInfo& m) {
    m.loader = "forge";
    std::istringstream ss(text);
    std::string line;
    bool inMods = false, inDeps = false;
    while (std::getline(ss, line)) {
        // 去注释和首尾空白
        size_t c = line.find('#');
        if (c != std::string::npos) line = line.substr(0, c);
        auto trim = [](std::string s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return std::string();
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        };
        line = trim(line);
        if (line.empty()) continue;
        if (line.rfind("[[mods]]", 0) == 0) { inMods = true; inDeps = false; continue; }
        if (line.rfind("[[dependencies.", 0) == 0) { inMods = false; inDeps = true; continue; }
        if (line.rfind("[", 0) == 0) { inMods = false; inDeps = false; continue; }

        auto keyVal = [&](const std::string& key, std::string& out) {
            size_t p = line.find(key);
            if (p == std::string::npos) return false;
            p += key.size();
            p = line.find('=', p);
            if (p == std::string::npos) return false;
            std::string v = trim(line.substr(p + 1));
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                v = v.substr(1, v.size() - 2);
            }
            out = v;
            return true;
        };

        if (inMods) {
            std::string id;
            if (keyVal("modId", id) && m.id.empty()) { m.id = id; if (m.name.empty()) m.name = id; }
        } else if (inDeps) {
            std::string depId;
            if (keyVal("modId", depId) && !depId.empty()) m.depends.push_back(depId);
        }
    }
    return !m.id.empty();
}

std::vector<ModInfo> ScanMods(const std::wstring& modsDir) {
    std::vector<ModInfo> out;
    std::error_code ec;
    if (modsDir.empty() || !fs::exists(modsDir, ec)) return out;
    for (auto& e : fs::directory_iterator(modsDir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::wstring ext = e.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext != L".jar") continue;

        ModInfo m;
        m.file = e.path().filename().wstring();
        std::string zip = e.path().u8string();

        std::string fabricJson;
        if (zip::ReadEntry(zip, "fabric.mod.json", fabricJson)) {
            if (parseFabric(fabricJson, m)) { out.push_back(m); continue; }
        }
        std::string toml;
        if (zip::ReadEntry(zip, "META-INF/mods.toml", toml)) {
            if (parseForge(toml, m)) { out.push_back(m); continue; }
        }
        // 无法识别的 jar：仅保留文件名，id 用文件名
        m.id = "unknown." + theme::WToUtf8(e.path().stem().wstring());
        m.name = theme::WToUtf8(e.path().stem().wstring());
        out.push_back(m);
    }
    return out;
}

std::vector<std::string> DetectMissing(const std::wstring& modsDir) {
    auto mods = ScanMods(modsDir);
    std::set<std::string> installed;
    for (const auto& m : mods) installed.insert(lowerId(m.id));

    std::vector<std::string> missing;
    std::set<std::string> seen;
    for (const auto& m : mods) {
        for (const auto& dep : m.depends) {
            std::string d = lowerId(dep);
            if (isBuiltin(d)) continue;
            if (installed.count(d)) continue;
            if (seen.count(d)) continue;
            seen.insert(d);
            missing.push_back(dep);
        }
    }
    return missing;
}

std::vector<SearchResult> SearchModrinth(const std::string& query, const std::string& projectType, std::string& err) {
    std::string q = query;
    std::replace(q.begin(), q.end(), ' ', '+');
    std::string url = "https://api.modrinth.com/v2/search?query=" + q +
        "&limit=10&facets=%5B%5B%22project_type%3A" + projectType + "%22%5D%5D";

    http::Response r = http::Get(url, 20000);
    std::vector<SearchResult> out;
    if (r.status < 0) { err = "网络错误: " + r.error; return out; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return out; }

    try {
        json::Value j = json::parse(r.body);
        const json::Value& hits = j.get("hits");
        if (hits.isArray()) {
            for (size_t i = 0; i < hits.size(); ++i) {
                SearchResult s;
                s.slug = hits.at(i).get("slug").asString();
                s.title = hits.at(i).get("title").asString();
                s.description = hits.at(i).get("description").asString();
                s.iconUrl = hits.at(i).get("icon_url").asString();
                if (!s.slug.empty()) out.push_back(s);
            }
        }
    } catch (const std::exception& e) { err = e.what(); }
    return out;
}

bool DownloadMod(const std::string& slug, const std::wstring& modsDir,
                 const std::string& loader, const std::string& mcVersion,
                 std::wstring& outFile, std::string& err) {
    std::string url = "https://api.modrinth.com/v2/project/" + slug + "/version";
    if (!loader.empty() || !mcVersion.empty()) {
        url += "?loaders=%5B%22" + loader + "%22%5D&game_versions=%5B%22" + mcVersion + "%22%5D";
    }
    http::Response r = http::Get(url, 20000);
    if (r.status < 0) { err = "网络错误: " + r.error; return false; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return false; }

    try {
        json::Value j = json::parse(r.body);
        if (!j.isArray() || j.size() == 0) { err = "未找到匹配版本"; return false; }
        // 选第一个版本的第一个文件（主 jar，排除 sources/javadoc）
        for (size_t vi = 0; vi < j.size(); ++vi) {
            const json::Value& files = j.at(vi).get("files");
            if (!files.isArray()) continue;
            for (size_t fi = 0; fi < files.size(); ++fi) {
                std::string filename = files.at(fi).get("filename").asString();
                std::string dl = files.at(fi).get("url").asString();
                if (dl.empty()) continue;
                std::string lower = filename;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return (char)std::tolower(c); });
                if (lower.find("sources") != std::string::npos ||
                    lower.find("javadoc") != std::string::npos) continue;

                fs::path dest = fs::path(modsDir) / theme::Utf8ToW(filename);
                std::error_code ec;
                fs::create_directories(dest.parent_path(), ec);
                http::DownloadResult d = http::Download(dl, dest.u8string(), nullptr, 120000, nullptr);
                if (!d.ok) { err = "下载失败: " + d.error; return false; }
                outFile = dest.wstring();
                return true;
            }
        }
        err = "未找到可下载文件";
        return false;
    } catch (const std::exception& e) { err = e.what(); return false; }
}

} // namespace modmgr
