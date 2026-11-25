// AddonRepo.cpp — HMCL 兼容的 Modrinth 附加组件仓库
#include "AddonRepo.h"

#include "Json.h"
#include "Crypto.h"
#include "MlcData.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace addon {
namespace names {
extern const char* const kData;
}

namespace {

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out += '%';
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string joinStrings(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

std::vector<std::string> jsonStrings(const json::Value& v) {
    std::vector<std::string> out;
    for (size_t i = 0; i < v.size(); ++i)
        if (v.at(i).isString()) out.push_back(v.at(i).asString());
    return out;
}

bool containsChinese(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return true;
    return false;
}

// ---------------------------------------------------------------
// 内嵌 HMCL 数据：中文项目名映射 + 分类本地化
// ---------------------------------------------------------------
struct NameInfo {
    std::string cfId, mrId, main, sub, abbr, ids;
};

struct NameDb {
    std::vector<NameInfo> infos;
    std::map<std::string, int> byMr, byCf, byModId, bySub;
    std::map<std::string, std::string> categories;
    bool loaded = false;

    void load() {
        if (loaded) return;
        loaded = true;
        enum { Mods, Categories, None } section = None;
        std::istringstream in(names::kData);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "[mods]") { section = Mods; continue; }
            if (line == "[categories]") { section = Categories; continue; }
            if (line.empty() || line[0] == '#') continue;

            std::vector<std::string> f;
            size_t start = 0;
            while (true) {
                size_t p = line.find('\t', start);
                std::string v = line.substr(start, p == std::string::npos ? std::string::npos : p - start);
                // generator escapes backslash/tab/newline; unescape the ones we use
                std::string u;
                for (size_t i = 0; i < v.size(); ++i) {
                    if (v[i] == '\\' && i + 1 < v.size()) {
                        char n = v[++i];
                        u += (n == 't') ? '\t' : (n == 'n') ? '\n' : (n == 'r') ? '\r' : n;
                    } else u += v[i];
                }
                f.push_back(u);
                if (p == std::string::npos) break;
                start = p + 1;
            }

            if (section == Mods && f.size() >= 7) {
                NameInfo n{ f[1], f[2], f[3], f[4], f[5], f[6] };
                int idx = (int)infos.size();
                infos.push_back(n);
                auto put = [&](std::map<std::string, int>& m, const std::string& key) {
                    if (!key.empty()) m.emplace(lower(key), idx);
                };
                put(byMr, n.mrId);
                put(byCf, n.cfId);
                put(bySub, n.sub);
                std::string rest = n.ids;
                size_t p = 0;
                while (true) {
                    size_t q = rest.find(';', p);
                    put(byModId, rest.substr(p, q == std::string::npos ? std::string::npos : q - p));
                    if (q == std::string::npos) break;
                    p = q + 1;
                }
            } else if (section == Categories && f.size() >= 2) {
                categories[f[0]] = f[1];
            }
        }
    }

    const NameInfo* find(const std::string& key) const {
        if (key.empty()) return nullptr;
        auto findIn = [&](const std::map<std::string, int>& m) -> const NameInfo* {
            auto it = m.find(lower(key));
            return it == m.end() ? nullptr : &infos[it->second];
        };
        if (const NameInfo* p = findIn(byMr)) return p;
        if (const NameInfo* p = findIn(byCf)) return p;
        if (const NameInfo* p = findIn(bySub)) return p;
        return findIn(byModId);
    }

    // HMCL AddonProjectInfoRegistry.search: LCS >= max(1, query length - 3)，按分数降序。
    std::vector<const NameInfo*> search(const std::string& query) const {
        std::wstring q;
        for (wchar_t c : mlc::Utf8ToW(query)) if (!std::iswspace(c)) q.push_back(c);
        if (q.empty()) return {};
        struct Match { int score; const NameInfo* info; };
        std::vector<Match> matches;
        for (const auto& n : infos) {
            for (const std::string* field : { &n.main, &n.sub, &n.abbr }) {
                if (field->empty()) continue;
                std::wstring k = mlc::Utf8ToW(*field);
                // LCS (O(n*m)); names are short, registry is built once.
                std::vector<int> prev(k.size() + 1, 0), cur(k.size() + 1, 0);
                for (wchar_t cq : q) {
                    cur[0] = 0;
                    for (size_t j = 0; j < k.size(); ++j)
                        cur[j + 1] = (cq == k[j]) ? prev[j] + 1 : std::max(prev[j + 1], cur[j]);
                    prev.swap(cur);
                }
                int score = prev[k.size()];
                if (score >= std::max(1, (int)q.size() - 3)) matches.push_back({ score, &n });
            }
        }
        std::stable_sort(matches.begin(), matches.end(),
                         [](const Match& a, const Match& b) { return a.score > b.score; });
        std::vector<const NameInfo*> out;
        std::set<const NameInfo*> seen;
        for (auto& m : matches) {
            if (seen.insert(m.info).second) {
                out.push_back(m.info);
                if (out.size() >= 3) break;  // HMCL: 最多 3 个英文查询词
            }
        }
        return out;
    }
};

NameDb& db() {
    static NameDb d;
    d.load();
    return d;
}

std::string displayName(const NameInfo& n) {
    std::string out;
    if (!n.abbr.empty()) out += "[" + n.abbr + "] ";
    out += n.main;
    if (!n.sub.empty()) out += " (" + n.sub + ")";
    return out;
}

int levenshtein(const std::wstring& a, const std::wstring& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (size_t i = 0; i < a.size(); ++i) {
        cur[0] = (int)i + 1;
        for (size_t j = 0; j < b.size(); ++j)
            cur[j + 1] = std::min({ prev[j + 1] + 1, cur[j] + 1,
                                    prev[j] + (a[i] == b[j] ? 0 : 1) });
        prev.swap(cur);
    }
    return prev[b.size()];
}

// HMCL TAG_COMPARATOR：已知加载器/类型标签优先，其余按字典序。
const std::vector<std::string>& tagPriority() {
    static const std::vector<std::string> k = {
        "babric", "bta-babric", "bukkit", "bungeecord", "canvas", "datapack", "fabric",
        "folia", "forge", "geyser", "iris", "java-agent", "legacy-fabric", "liteloader",
        "minecraft", "modloader", "mrpack", "neoforge", "nilloader", "optifine", "ornith",
        "paper", "purpur", "quilt", "rift", "spigot", "sponge", "vanilla", "velocity", "waterfall"
    };
    return k;
}

void sortTags(std::vector<std::string>& tags) {
    const auto& prio = tagPriority();
    std::stable_sort(tags.begin(), tags.end(), [&](const std::string& a, const std::string& b) {
        auto ia = std::find(prio.begin(), prio.end(), a) - prio.begin();
        auto ib = std::find(prio.begin(), prio.end(), b) - prio.begin();
        bool ka = ia < (int)prio.size(), kb = ib < (int)prio.size();
        if (ka && kb) return ia < ib;
        if (ka != kb) return ka;
        return a < b;
    });
}

// ---------------------------------------------------------------
// Modrinth API
// ---------------------------------------------------------------
const char* kPrefix = "https://api.modrinth.com";

const char* sortName(Sort s) {
    switch (s) {
        case Sort::Relevancy: return "relevance";
        case Sort::Popularity: return "follows";
        case Sort::DateCreated: return "newest";
        case Sort::LastUpdated: return "updated";
        case Sort::TotalDownloads: return "downloads";
    }
    return "relevance";
}

SearchResult searchOnce(const std::string& query, const std::string& gameVersion,
                        const std::string& category, int page, Sort sort, std::string& err) {
    SearchResult out;
    std::string facets = "[[\"project_type:mod\"]";
    if (!gameVersion.empty()) facets += ",[\"versions:" + gameVersion + "\"]";
    if (!category.empty()) facets += ",[\"categories:" + category + "\"]";
    facets += "]";

    std::string url = std::string(kPrefix) + "/v2/search?query=" + urlEncode(query) +
                      "&facets=" + urlEncode(facets) +
                      "&offset=" + std::to_string(page * 50) +
                      "&limit=50&index=" + sortName(sort);
    http::Response r = http::Get(url, 25000);
    if (r.status < 0) { err = "网络错误: " + r.error; return out; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return out; }
    try {
        json::Value j = json::parse(r.body);
        out.totalPages = (int)((j.get("total_hits").asInt() + 49) / 50);
        const json::Value& hits = j.get("hits");
        for (size_t i = 0; i < hits.size(); ++i) {
            const json::Value& e = hits.at(i);
            Hit h;
            h.slug = e.get("slug").asString();
            h.projectId = e.get("project_id").asString();
            h.title = e.get("title").asString();
            h.desc = e.get("description").asString();
            h.iconUrl = e.get("icon_url").asString();
            h.author = e.get("author").asString();
            h.categories = jsonStrings(e.get("display_categories"));
            sortTags(h.categories);
            if (h.projectId.empty()) h.projectId = h.slug;
            h.pageUrl = std::string("https://modrinth.com/mod/") + h.projectId;
            if (!h.title.empty()) out.hits.push_back(std::move(h));
        }
    } catch (const std::exception& ex) { err = ex.what(); }
    return out;
}

} // namespace

SearchResult Search(const std::string& query, const std::string& gameVersion,
                    const std::string& category, int page, Sort sort, std::string& err) {
    if (page < 0) page = 0;
    if (!containsChinese(query)) return searchOnce(query, gameVersion, category, page, sort, err);

    // HMCL LocalizedRemoteAddonRepository：中文查询 → 最多 3 个英文名，逐个搜到第一个非空结果，
    // 再把有中文名的结果按编辑距离（每个命中字符 -10）排前面。
    auto& d = db();
    SearchResult out;
    std::string firstErr;
    for (const NameInfo* n : d.search(query)) {
        std::string english = n->sub.empty() ? n->main : n->sub;
        std::istringstream is(english);
        std::vector<std::string> parts;
        std::string w;
        while (is >> w) parts.push_back(w);
        std::string tokenized = joinStrings(parts, " ");
        if (tokenized.empty()) continue;

        std::string e;
        SearchResult r = searchOnce(tokenized, gameVersion, category, page, Sort::Relevancy, e);
        if (firstErr.empty()) firstErr = e;
        if (r.hits.empty()) continue;
        out.totalPages = r.totalPages;

        std::wstring q;
        for (wchar_t c : mlc::Utf8ToW(query)) if (!std::iswspace(c)) q.push_back(c);
        struct Scored { Hit hit; int score; };
        std::vector<Scored> zh;
        std::vector<Hit> en;
        for (Hit& h : r.hits) {
            const NameInfo* info = d.find(h.projectId);
            if (!info) info = d.find(h.slug);
            bool isChinese = info && containsChinese(info->main);
            if (isChinese) {
                std::wstring cn = mlc::Utf8ToW(info->main);
                int score = levenshtein(q, cn);
                for (wchar_t c : q) if (cn.find(c) != std::wstring::npos) score -= 10;
                zh.push_back({ std::move(h), score });
            } else {
                en.push_back(std::move(h));
            }
        }
        std::stable_sort(zh.begin(), zh.end(),
                         [](const Scored& a, const Scored& b) { return a.score < b.score; });
        for (auto& s : zh) out.hits.push_back(std::move(s.hit));
        for (auto& h : en) out.hits.push_back(std::move(h));
        return out;
    }
    if (out.hits.empty() && !firstErr.empty()) err = firstErr;
    return out;
}

std::vector<Category> GetCategories(std::string& err) {
    std::vector<Category> out;
    http::Response r = http::Get(std::string(kPrefix) + "/v2/tag/category", 25000);
    if (r.status < 0) { err = "网络错误: " + r.error; return out; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return out; }
    try {
        json::Value j = json::parse(r.body);
        auto& d = db();
        for (size_t i = 0; i < j.size(); ++i) {
            const json::Value& e = j.at(i);
            if (e.get("project_type").asString() != "mod") continue;
            Category c;
            c.id = e.get("name").asString();
            auto it = d.categories.find(c.id);
            c.localized = it == d.categories.end() ? c.id : it->second;
            if (!c.id.empty()) out.push_back(std::move(c));
        }
        std::stable_sort(out.begin(), out.end(), [](const Category& a, const Category& b) {
            return a.localized < b.localized;
        });
    } catch (const std::exception& ex) { err = ex.what(); }
    return out;
}

std::vector<Version> GetVersions(const std::string& projectId, std::string& err) {
    std::vector<Version> out;
    if (projectId.empty()) { err = "项目 id 为空"; return out; }
    http::Response r = http::Get(std::string(kPrefix) + "/v2/project/" + urlEncode(projectId) +
                                 "/version?include_changelog=false", 25000);
    if (r.status < 0) { err = "网络错误: " + r.error; return out; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return out; }
    try {
        json::Value j = json::parse(r.body);
        for (size_t i = 0; i < j.size(); ++i) {
            const json::Value& e = j.at(i);
            const json::Value& files = e.get("files");
            if (files.size() == 0) continue;   // HMCL: files 为空的版本直接丢弃
            const json::Value& f = files.at(0);
            Version v;
            v.id = e.get("id").asString();
            v.projectId = e.get("project_id").asString();
            v.name = e.get("name").asString();
            v.versionNumber = e.get("version_number").asString();
            v.datePublished = e.get("date_published").asString();
            v.versionType = e.get("version_type").asString();
            v.url = f.get("url").asString();
            v.fileName = f.get("filename").asString();
            const json::Value& hashes = f.get("hashes");
            v.sha1 = hashes.get("sha1").asString();
            v.md5 = hashes.get("md5").asString();
            v.sha256 = hashes.get("sha256").asString();
            v.sha512 = hashes.get("sha512").asString();
            v.gameVersions = jsonStrings(e.get("game_versions"));
            v.loaders = jsonStrings(e.get("loaders"));
            const json::Value& deps = e.get("dependencies");
            for (size_t k = 0; k < deps.size(); ++k) {
                const json::Value& dv = deps.at(k);
                Dependency dep;
                std::string t = dv.get("dependency_type").asString();
                if (t == "required") dep.type = Dependency::Type::Required;
                else if (t == "optional") dep.type = Dependency::Type::Optional;
                else if (t == "embedded") dep.type = Dependency::Type::Embedded;
                else if (t == "incompatible") dep.type = Dependency::Type::Incompatible;
                else dep.type = Dependency::Type::Broken;
                dep.projectId = dv.get("project_id").asString();
                dep.versionId = dv.get("version_id").asString();
                if (dep.projectId.empty()) dep.type = Dependency::Type::Broken;
                v.dependencies.push_back(std::move(dep));
            }
            if (!v.fileName.empty() && !v.url.empty()) out.push_back(std::move(v));
        }
        std::stable_sort(out.begin(), out.end(), [](const Version& a, const Version& b) {
            return a.datePublished > b.datePublished;
        });
    } catch (const std::exception& ex) { err = ex.what(); }
    return out;
}

std::string ResolveProjectName(const std::string& projectId) {
    static std::mutex mu;
    static std::map<std::string, std::string> cache;
    if (projectId.empty()) return {};
    {
        std::lock_guard<std::mutex> lk(mu);
        auto it = cache.find(projectId);
        if (it != cache.end()) return it->second;
    }
    http::Response r = http::Get(std::string(kPrefix) + "/v2/project/" + urlEncode(projectId), 20000);
    std::string name;
    if (r.status == 200) {
        try { name = json::parse(r.body).get("title").asString(); } catch (...) {}
    }
    if (name.empty()) name = LocalizedTitle(projectId, projectId);
    std::lock_guard<std::mutex> lk(mu);
    cache[projectId] = name;
    return name;
}

std::string GetChangelog(const std::string& versionId, std::string& err) {
    http::Response r = http::Get(std::string(kPrefix) + "/v2/version/" + urlEncode(versionId), 20000);
    if (r.status < 0) { err = "网络错误: " + r.error; return {}; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return {}; }
    try { return json::parse(r.body).get("changelog").asString(); }
    catch (const std::exception& ex) { err = ex.what(); return {}; }
}

bool Download(const Version& v, const std::string& destPath, std::string& err,
              http::ProgressFn progress) {
    if (v.url.empty()) { err = "下载地址为空"; return false; }
    // HMCL FetchTask.DEFAULT_RETRY = 5
    for (int attempt = 0; attempt < 5; ++attempt) {
        const std::string* sha1 = v.sha1.empty() ? nullptr : &v.sha1;
        http::DownloadResult d = http::Download(v.url, destPath, progress, 180000, sha1);
        if (!d.ok) {
            err = d.error;
            continue;
        }
        // HMCL 的哈希优先级 md5 > sha1 > sha256 > sha512；这里实现了 md5/sha1，
        // 其余两种 Modrinth 通常不返回，跳过时不算校验失败。
        if (!v.md5.empty()) {
            std::ifstream f(destPath, std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::string got = crypto::md5Hex(data);
            if (!got.empty() && lower(got) != lower(v.md5)) { err = "MD5 校验失败"; return false; }
        }
        return true;
    }
    if (err.empty()) err = "下载失败（已重试 5 次）";
    return false;
}

std::string LocalizedTitle(const std::string& projectIdOrSlug, const std::string& fallback) {
    const NameInfo* n = db().find(projectIdOrSlug);
    if (n && containsChinese(n->main)) return displayName(*n);
    return fallback;
}

std::string LocalizedCategory(const std::string& id) {
    auto& d = db();
    auto it = d.categories.find(id);
    return it == d.categories.end() ? id : it->second;
}

std::string TranslateLoader(const std::string& loader) {
    std::string l = lower(loader);
    if (l == "forge") return "Forge";
    if (l == "neoforge") return "NeoForge";
    if (l == "fabric") return "Fabric";
    if (l == "quilt") return "Quilt";
    if (l == "legacy-fabric") return "Legacy Fabric";
    if (l == "liteloader") return "LiteLoader";
    if (l == "cleanroom") return "Cleanroom";
    return loader;
}

bool IsKnownLoader(const std::string& s) {
    std::string l = lower(s);
    static const char* k[] = { "forge", "neoforge", "fabric", "quilt", "legacy-fabric",
                               "liteloader", "cleanroom", "bukkit", "spigot", "paper",
                               "purpur", "sponge", "velocity", "bungeecord", "waterfall",
                               "folia", "geyser", "iris", "optifine" };
    for (const char* x : k) if (l == x) return true;
    return false;
}

std::vector<std::string> SupportedLoaders(const std::string& loader,
                                          const std::string& gameVersion,
                                          const std::string& modsDir) {
    std::set<std::string> out;
    std::string l = lower(loader);
    if (!l.empty()) out.insert(l);
    // HMCL ModManager.updateSupportedLoaders 的兼容特例
    if (l == "cleanroom") out.insert("forge");
    if (l == "quilt" || l == "legacy-fabric") out.insert("fabric");
    if (l == "neoforge" && gameVersion == "1.20.1") out.insert("forge");

    bool hasKilt = false, hasConnector = false;
    if (!modsDir.empty()) {
        std::wstring w = mlc::Utf8ToW(modsDir) + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(w.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::string n = lower(mlc::WToUtf8(fd.cFileName));
                if (n.find("kilt") != std::string::npos) hasKilt = true;
                if (n.find("connector") != std::string::npos) hasConnector = true;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    if (l == "fabric" && hasKilt) { out.insert("forge"); out.insert("neoforge"); }
    if ((l == "neoforge" && hasConnector) ||
        (l == "forge" && gameVersion == "1.20.1" && hasConnector)) out.insert("fabric");
    return std::vector<std::string>(out.begin(), out.end());
}

bool IsSnapshotVersion(const std::string& gameVersion) {
    std::string l = lower(gameVersion);
    return l.find("snapshot") != std::string::npos || l.find("-pre") != std::string::npos ||
           l.find("-rc") != std::string::npos || l.find("experimental") != std::string::npos;
}

} // namespace addon
