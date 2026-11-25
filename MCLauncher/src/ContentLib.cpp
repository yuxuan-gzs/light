// ContentLib.cpp — 内容库实现
#include "ContentLib.h"
#include "MlcData.h"
#include "Json.h"
#include "Http.h"
#include "Zip.h"

#include <zlib.h>

#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace content {

namespace fs = std::filesystem;

// ---------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------
static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string fileStem(const std::string& name) {
    size_t p = name.find_last_of('.');
    return p == std::string::npos ? name : name.substr(0, p);
}

static std::string extOf(const std::string& name) {
    size_t p = name.find_last_of('.');
    if (p == std::string::npos) return std::string();
    return lower(name.substr(p));
}

static std::string humanSize(int64_t n) {
    char buf[64];
    if (n >= 1024LL * 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f GB", (double)n / (1024.0 * 1024 * 1024));
    else if (n >= 1024LL * 1024) snprintf(buf, sizeof(buf), "%.1f MB", (double)n / (1024.0 * 1024));
    else if (n >= 1024) snprintf(buf, sizeof(buf), "%.1f KB", (double)n / 1024.0);
    else snprintf(buf, sizeof(buf), "%lld B", (long long)n);
    return buf;
}

static bool writeBinary(const std::string& path, const std::string& bytes) {
    std::error_code ec;
    fs::path p = fs::u8path(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(bytes.data(), (std::streamsize)bytes.size());
    return f.good();
}

static std::string urlEncode(const std::string& s) {
    std::string o;
    o.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o.push_back((char)c);
        else {
            char b[4];
            snprintf(b, sizeof(b), "%%%02X", c);
            o += b;
        }
    }
    return o;
}

static std::string safePathName(const std::string& in) {
    std::string o;
    o.reserve(in.size());
    for (unsigned char c : in) {
        if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            o.push_back('_');
        } else {
            o.push_back((char)c);
        }
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    if (o.size() > 80) {
        o.resize(80);
        while (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0x80) o.pop_back();
    }
    return o;
}

static bool isArchiveName(const std::string& name) {
    std::string e = extOf(name);
    return e == ".zip" || e == ".jar" || e == ".mrpack";
}

// ---------------------------------------------------------------
// 封面缓存
// ---------------------------------------------------------------
static std::string coverPathFor(const std::string& key, const std::string& ext) {
    std::string safe;
    for (char c : key) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_') safe.push_back(c);
        else safe.push_back('_');
    }
    if (safe.size() > 80) safe = safe.substr(0, 80);
    return mlc::WToUtf8(mlc::CoversDir()) + "\\" + safe + ext;
}

std::string CacheCover(const std::string& url, const std::string& key) {
    if (url.empty()) return std::string();
    std::string dest = coverPathFor(key, ".png");
    if (mlc::FileExists(dest)) return dest;
    std::error_code ec;
    fs::create_directories(fs::u8path(mlc::WToUtf8(mlc::CoversDir())), ec);
    http::DownloadResult r = http::Download(url, dest, nullptr, 20000, nullptr);
    if (!r.ok) return std::string();
    return dest;
}

std::string ExtractCover(const std::string& archive, const std::string& entryName,
                         const std::string& key) {
    if (archive.empty() || entryName.empty()) return std::string();
    std::string dest = coverPathFor(key, ".png");
    if (mlc::FileExists(dest)) return dest;
    std::string bytes;
    if (!zip::ReadEntry(archive, entryName, bytes) || bytes.empty()) return std::string();
    std::error_code ec;
    fs::create_directories(fs::u8path(mlc::WToUtf8(mlc::CoversDir())), ec);
    if (!writeBinary(dest, bytes)) return std::string();
    return dest;
}

// 从 jar 中找一张可用作封面的图片
static std::string coverFromJar(const std::string& jarPath, const std::string& keyBase) {
    // 1) fabric.mod.json: icon 可能是字符串或 {"64": "assets/x.png"}
    std::string txt;
    if (zip::ReadEntry(jarPath, "fabric.mod.json", txt) && !txt.empty()) {
        try {
            json::Value j = json::parse(txt);
            const json::Value& icon = j.get("icon");
            std::string entry;
            if (icon.isString()) entry = icon.asString();
            else if (icon.isObject()) {
                for (const auto& kv : icon.obj) {
                    std::string v = kv.second.asString();
                    if (!v.empty()) entry = v;
                }
            }
            if (!entry.empty()) {
                std::string p = ExtractCover(jarPath, entry, keyBase + "_icon");
                if (!p.empty()) return p;
            }
        } catch (...) {}
    }
    // 2) quilt.mod.json
    if (zip::ReadEntry(jarPath, "quilt.mod.json", txt) && !txt.empty()) {
        try {
            json::Value j = json::parse(txt);
            std::string entry = j.get("quilt_loader").get("metadata").get("icon").asString();
            if (!entry.empty()) {
                std::string p = ExtractCover(jarPath, entry, keyBase + "_qicon");
                if (!p.empty()) return p;
            }
        } catch (...) {}
    }
    // 3) Forge mods.toml: logoFile="logo.png"
    if (zip::ReadEntry(jarPath, "META-INF/mods.toml", txt) && !txt.empty()) {
        size_t p = txt.find("logoFile");
        if (p != std::string::npos) {
            size_t q = txt.find('"', p);
            if (q != std::string::npos) {
                size_t e = txt.find('"', q + 1);
                if (e != std::string::npos) {
                    std::string entry = txt.substr(q + 1, e - q - 1);
                    std::string cp = ExtractCover(jarPath, entry, keyBase + "_logo");
                    if (!cp.empty()) return cp;
                }
            }
        }
    }
    // 4) 扫描 assets 下的 icon.png / logo.png
    std::string found;
    zip::ForEach(jarPath, [&](const zip::Entry& e) -> bool {
        if (e.isDir) return true;
        std::string low = lower(e.name);
        if ((low.size() > 4 && low.substr(low.size() - 4) == ".png") &&
            (low.find("icon") != std::string::npos || low.find("logo") != std::string::npos)) {
            found = e.name;
            return false;
        }
        return true;
    });
    if (!found.empty()) return ExtractCover(jarPath, found, keyBase + "_auto");
    return std::string();
}

// ---------------------------------------------------------------
// 扫描
// ---------------------------------------------------------------
static std::string mtimeKey(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    if (ec) return "0";
    return std::to_string((long long)t.time_since_epoch().count());
}

// ---------------------------------------------------------------
// level.dat（gzip 压缩的 NBT）解析 —— 像 HMCL 一样显示世界真实名字 / 版本 / 最后游玩时间
// ---------------------------------------------------------------
static std::string readBinaryFile(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::string gunzip(const std::string& in) {
    if (in.size() < 2 || (unsigned char)in[0] != 0x1f || (unsigned char)in[1] != 0x8b)
        return in;                       // 有些工具写的是未压缩 NBT
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 16) != Z_OK) return "";   // 15+16 = gzip
    zs.next_in = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();
    std::string out;
    char buf[8192];
    int r = Z_OK;
    do {
        zs.next_out = (Bytef*)buf;
        zs.avail_out = sizeof(buf);
        r = inflate(&zs, Z_NO_FLUSH);
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (r == Z_OK && out.size() < 32 * 1024 * 1024);
    inflateEnd(&zs);
    return r == Z_STREAM_END ? out : "";
}

struct NbtCursor {
    const std::string& d;
    size_t p = 0;
    bool ok = true;
    explicit NbtCursor(const std::string& x) : d(x) {}

    uint8_t u8() {
        if (p >= d.size()) { ok = false; return 0; }
        return (uint8_t)d[p++];
    }
    int16_t i16() {
        if (p + 2 > d.size()) { ok = false; return 0; }
        int16_t v = (int16_t)(((uint16_t)(uint8_t)d[p] << 8) | (uint8_t)d[p + 1]);
        p += 2;
        return v;
    }
    int32_t i32() {
        if (p + 4 > d.size()) { ok = false; return 0; }
        uint32_t v = ((uint32_t)(uint8_t)d[p] << 24) | ((uint32_t)(uint8_t)d[p + 1] << 16) |
                     ((uint32_t)(uint8_t)d[p + 2] << 8) | (uint8_t)d[p + 3];
        p += 4;
        return (int32_t)v;
    }
    int64_t i64() {
        if (p + 8 > d.size()) { ok = false; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | (uint8_t)d[p + i];
        p += 8;
        return (int64_t)v;
    }
    std::string str() {
        int16_t n = i16();
        if (!ok || n < 0 || p + (size_t)n > d.size()) { ok = false; return ""; }
        std::string s = d.substr(p, (size_t)n);
        p += (size_t)n;
        return s;
    }
    void skip(uint8_t type, int depth = 0) {
        if (!ok || depth > 32) return;
        switch (type) {
            case 1: p += 1; break;
            case 2: p += 2; break;
            case 3: p += 4; break;
            case 4: p += 8; break;
            case 5: p += 4; break;
            case 6: p += 8; break;
            case 7: { int32_t n = i32(); p += (size_t)std::max(0, n); break; }
            case 8: str(); break;
            case 9: {
                uint8_t et = u8();
                int32_t n = i32();
                for (int i = 0; i < n && ok; ++i) skip(et, depth + 1);
                break;
            }
            case 10: {
                while (ok) {
                    uint8_t t = u8();
                    if (!ok || t == 0) break;
                    str();
                    skip(t, depth + 1);
                }
                break;
            }
            case 11: { int32_t n = i32(); p += (size_t)std::max(0, n) * 4; break; }
            case 12: { int32_t n = i32(); p += (size_t)std::max(0, n) * 8; break; }
            default: ok = false; break;
        }
        if (p > d.size()) ok = false;
    }
};

struct WorldNbt { std::string name, version; int64_t played = 0; };

static WorldNbt ReadWorldNbt(const std::string& levelDat) {
    WorldNbt w;
    std::string raw = readBinaryFile(levelDat);
    if (raw.empty()) return w;
    std::string nbt = gunzip(raw);
    if (nbt.empty()) return w;

    NbtCursor c(nbt);
    if (c.u8() != 10) return w;           // root 必须是 TAG_Compound
    c.str();                              // root name
    while (c.ok) {
        uint8_t t = c.u8();
        if (!c.ok || t == 0) break;
        std::string key = c.str();
        if (key == "Data" && t == 10) {
            while (c.ok) {
                uint8_t dt = c.u8();
                if (!c.ok || dt == 0) break;
                std::string dk = c.str();
                if (dk == "LevelName" && dt == 8) {
                    w.name = c.str();
                } else if (dk == "Version" && dt == 10) {
                    while (c.ok) {
                        uint8_t vt = c.u8();
                        if (!c.ok || vt == 0) break;
                        std::string vk = c.str();
                        if (vk == "Name" && vt == 8) w.version = c.str();
                        else c.skip(vt);
                    }
                } else if (dk == "LastPlayed" && dt == 4) {
                    w.played = c.i64();
                } else {
                    c.skip(dt);
                }
            }
            break;
        }
        c.skip(t);
    }

    // 有些世界带 Forge 的 fml 数据，嵌套结构会让严格跳读错位。
    // 像 HMCL 一样兜底：直接按 NBT tag 头（[类型][名字长度][名字]）定位字段。
    if (w.name.empty()) {
        auto findStr = [&](const std::string& pat, size_t from) -> std::string {
            size_t p = nbt.find(pat, from);
            if (p == std::string::npos) return "";
            p += pat.size();
            if (p + 2 > nbt.size()) return "";
            int n = ((uint8_t)nbt[p] << 8) | (uint8_t)nbt[p + 1];
            if (n <= 0 || n > 1024 || p + 2 + (size_t)n > nbt.size()) return "";
            return nbt.substr(p + 2, (size_t)n);
        };
        w.name = findStr(std::string("\x08\x00\x09LevelName", 12), 0);

        // Data.Version 就在 LevelName 前面几十字节；必须从 LevelName 往前找最近的，
        // 从后往前找会命中 fml 模组数据里的 Version。
        size_t ln = nbt.find(std::string("\x08\x00\x09LevelName", 12));
        if (ln != std::string::npos) {
            size_t vp = nbt.rfind(std::string("\x0a\x00\x07Version", 10), ln);
            if (vp != std::string::npos && ln - vp < 512) {
                size_t np = nbt.find(std::string("\x08\x00\x04Name", 8), vp);
                if (np != std::string::npos && np < ln && ln - np < 256) {
                    size_t q = np + 8;
                    if (q + 2 <= nbt.size()) {
                        int n = ((uint8_t)nbt[q] << 8) | (uint8_t)nbt[q + 1];
                        if (n > 0 && n <= 64 && q + 2 + (size_t)n <= nbt.size())
                            w.version = nbt.substr(q + 2, (size_t)n);
                    }
                }
            }
        }
        size_t lp = nbt.find(std::string("\x04\x00\x0aLastPlayed", 14));
        if (lp != std::string::npos) {
            size_t q = lp + 14;
            if (q + 8 <= nbt.size()) {
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i) v = (v << 8) | (uint8_t)nbt[q + i];
                w.played = (int64_t)v;
            }
        }
    }
    return w;
}

static std::string fmtPlayed(int64_t ms) {
    if (ms <= 0) return "";
    time_t t = (time_t)(ms / 1000);
    struct tm lt{};
    if (!localtime_s(&lt, &t)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", lt.tm_year + 1900,
                 lt.tm_mon + 1, lt.tm_mday);
        return buf;
    }
    return "";
}

static std::vector<Item> scanMods(const std::string& dir) {
    std::vector<Item> out;
    std::error_code ec;
    fs::path d = fs::u8path(dir);
    if (!fs::is_directory(d, ec)) return out;

    for (auto& e : fs::directory_iterator(d, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string name = mlc::WToUtf8(e.path().filename().wstring());
        std::string low = lower(name);
        bool disabled = false;
        std::string key = name;
        if (low.size() > 9 && low.substr(low.size() - 9) == ".disabled") {
            disabled = true;
            key = name.substr(0, name.size() - 9);
            low = lower(key);
        }
        if (low.size() < 4 || low.substr(low.size() - 4) != ".jar") continue;

        Item it;
        it.key = key;
        it.enabled = !disabled;
        it.toggleable = true;
        it.title = fileStem(key);
        it.badge = disabled ? "已停用" : "";

        std::string full = mlc::WToUtf8(e.path().wstring());
        std::error_code szec;
        auto sz = fs::file_size(e.path(), szec);
        it.size = szec ? 0 : (int64_t)sz;

        // 解析模组元数据（对齐 HMCL 的 Fabric / Quilt / NeoForge(Forge) / 老版 mcmod.info）
        std::string txt;
        bool got = false;
        if (zip::ReadEntry(full, "fabric.mod.json", txt) && !txt.empty()) {
            try {
                json::Value j = json::parse(txt);
                std::string nm = j.get("name").asString();
                std::string ver = j.get("version").asString();
                std::string sum = j.get("summary").asString();
                std::string env = j.get("environment").asString();
                if (!nm.empty()) it.title = nm;
                it.badge = "Fabric";
                it.desc = (sum.empty() ? std::string("Fabric") : sum);
                if (!ver.empty()) it.desc += " · " + ver;
                if (!env.empty() && env != "*") it.desc += " · " + env;
                got = true;
            } catch (...) {}
        }
        if (!got && zip::ReadEntry(full, "quilt.mod.json", txt) && !txt.empty()) {
            try {
                json::Value j = json::parse(txt);
                const json::Value& ldr = j.get("quilt_loader");
                const json::Value& md = ldr.get("metadata");
                std::string nm = md.get("name").asString();
                std::string ver = ldr.get("version").asString();
                std::string sum = md.get("description").asString();
                if (nm.empty()) nm = ldr.get("id").asString();
                if (!nm.empty()) it.title = nm;
                it.badge = "Quilt";
                it.desc = (sum.empty() ? std::string("Quilt") : sum);
                if (!ver.empty()) it.desc += " · " + ver;
                got = true;
            } catch (...) {}
        }
        if (!got && zip::ReadEntry(full, "META-INF/mods.toml", txt) && !txt.empty()) {
            auto tomlVal = [&](const char* key) {
                std::string k(key);
                size_t p = txt.find(k + "=");
                if (p == std::string::npos) return std::string();
                p = txt.find('"', p);
                if (p == std::string::npos) return std::string();
                size_t q = txt.find('"', p + 1);
                return q == std::string::npos ? std::string() : txt.substr(p + 1, q - p - 1);
            };
            std::string nm = tomlVal("displayName");
            if (nm.empty()) nm = tomlVal("name");
            std::string ver = tomlVal("version");
            std::string desc = tomlVal("description");
            if (!nm.empty()) it.title = nm;
            it.badge = "Forge";
            it.desc = (desc.empty() ? std::string("Forge") : desc);
            if (!ver.empty()) it.desc += " · " + ver;
            got = true;
        }
        if (!got && zip::ReadEntry(full, "mcmod.info", txt) && !txt.empty()) {
            try {
                json::Value j = json::parse(txt);
                const json::Value& e = j.isArray() ? j.at(0) : j;
                std::string nm = e.get("name").asString();
                std::string ver = e.get("version").asString();
                std::string mc = e.get("mcversion").asString();
                std::string desc = e.get("description").asString();
                if (!nm.empty()) it.title = nm;
                it.badge = "Forge";
                it.desc = (desc.empty() ? std::string("Forge") : desc);
                if (!mc.empty()) it.desc += " · MC " + mc;
                if (!ver.empty()) it.desc += " · " + ver;
                got = true;
            } catch (...) {}
        }
        if (it.desc.empty()) it.desc = humanSize(it.size);

        std::string ck = "mod_" + std::to_string(std::hash<std::string>{}(full + "#" + mtimeKey(e.path())));
        it.cover = coverFromJar(full, ck);
        out.push_back(std::move(it));
    }
    // 名称排序（启用的在前）
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) {
        if (a.enabled != b.enabled) return a.enabled;
        return lower(a.title) < lower(b.title);
    });
    return out;
}

// options.txt 里 resourcePacks 列表的读取（HMCL 用这个顺序决定优先级）
static std::vector<std::string> readPackOrder(const std::string& packsDir) {
    fs::path dataDir = fs::u8path(packsDir).parent_path();
    std::string optPath = mlc::WToUtf8((dataDir / L"options.txt").wstring());
    std::string text = mlc::ReadTextFile(optPath);
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("resourcePacks:", 0) != 0) continue;
        std::vector<std::string> arr;
        try {
            json::Value j = json::parse(line.substr(strlen("resourcePacks:")));
            for (size_t i = 0; i < j.size(); ++i) arr.push_back(j.at(i).asString());
        } catch (...) {}
        return arr;
    }
    return {};
}

static std::vector<Item> scanPacks(const std::string& dir, Kind kind) {
    std::vector<Item> out;
    // 资源包的启用状态/顺序都由 options.txt 决定（和 HMCL 一样）
    const std::vector<std::string> packOrder =
        (kind == Kind::ResourcePacks) ? readPackOrder(dir) : std::vector<std::string>();
    std::error_code ec;
    fs::path d = fs::u8path(dir);
    if (!fs::is_directory(d, ec)) return out;
    for (auto& e : fs::directory_iterator(d, ec)) {
        std::string name = mlc::WToUtf8(e.path().filename().wstring());
        bool isDir = e.is_directory(ec);
        if (!isDir && !isArchiveName(name)) continue;
        Item it;
        it.key = name;
        it.title = isDir ? name : fileStem(name);
        it.toggleable = true;

        std::string full = mlc::WToUtf8(e.path().wstring());
        // 像 HMCL 一样读 pack.mcmeta（pack_format + description）
        std::string meta;
        if (isDir) meta = readBinaryFile(mlc::WToUtf8((e.path() / L"pack.mcmeta").wstring()));
        else zip::ReadEntry(full, "pack.mcmeta", meta);
        bool hasMeta = false;
        if (!meta.empty()) {
            try {
                json::Value j = json::parse(meta);
                const json::Value& pk = j.get("pack");
                int fmt = (int)pk.get("pack_format").asInt();
                const json::Value& ds = pk.get("description");
                std::string d = ds.isObject() ? ds.get("text").asString() : ds.asString();
                // 去掉 MC 的颜色/格式符 §x（JSON 里可能是 \u00a7x）
                std::string clean;
                for (size_t i = 0; i < d.size();) {
                    if (d[i] == '\\' && d.compare(i, 6, "\\u00a7") == 0 && i + 7 < d.size()) { i += 8; continue; }
                    if ((unsigned char)d[i] == 0xc2 && i + 1 < d.size() && (unsigned char)d[i + 1] == 0xa7) { i += 3; continue; }
                    clean += d[i++];
                }
                if (!clean.empty()) it.desc = clean;
                if (fmt > 0) it.badge = "pack " + std::to_string(fmt);
                hasMeta = true;
            } catch (...) {}
        }
        if (isDir) {
            // 目录形式的资源包/光影包
            fs::path png = e.path() / L"pack.png";
            if (fs::is_regular_file(png, ec)) it.cover = mlc::WToUtf8(png.wstring());
            if (!hasMeta) it.desc = "文件夹";
        } else {
            std::error_code szec;
            auto sz = fs::file_size(e.path(), szec);
            it.size = szec ? 0 : (int64_t)sz;
            if (!hasMeta) it.desc = humanSize(it.size);
            std::string ck = std::string(kind == Kind::Shaders ? "shd_" : "rp_") +
                             std::to_string(std::hash<std::string>{}(full + "#" + mtimeKey(e.path())));
            it.cover = ExtractCover(full, "pack.png", ck);
        }
        if (kind == Kind::ResourcePacks) {
            it.enabled = std::find(packOrder.begin(), packOrder.end(),
                                   "file/" + name) != packOrder.end();
        }
        out.push_back(std::move(it));
    }
    // HMCL：资源包按 options.txt 的 resourcePacks 顺序排（越靠前优先级越高），
    // 没在列表里的（停用状态）排到最后。
    if (kind == Kind::ResourcePacks) {
        auto idx = [&](const std::string& k) {
            std::string e = "file/" + k;
            auto it = std::find(packOrder.begin(), packOrder.end(), e);
            return it == packOrder.end() ? (int)packOrder.size() : (int)(it - packOrder.begin());
        };
        std::sort(out.begin(), out.end(), [&](const Item& a, const Item& b) {
            int ia = idx(a.key), ib = idx(b.key);
            if (ia != ib) return ia < ib;
            if (a.enabled != b.enabled) return a.enabled;
            return lower(a.title) < lower(b.title);
        });
    } else {
        std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) {
            return lower(a.title) < lower(b.title);
        });
    }
    return out;
}

static std::vector<Item> scanWorlds(const std::string& dir) {
    std::vector<Item> out;
    std::error_code ec;
    fs::path d = fs::u8path(dir);
    if (!fs::is_directory(d, ec)) return out;
    for (auto& e : fs::directory_iterator(d, ec)) {
        if (!e.is_directory(ec)) continue;
        Item it;
        it.key = mlc::WToUtf8(e.path().filename().wstring());
        it.title = it.key;
        it.toggleable = false;
        it.enabled = true;
        fs::path icon = e.path() / L"icon.png";
        if (fs::is_regular_file(icon, ec)) it.cover = mlc::WToUtf8(icon.wstring());
        // 像 HMCL 一样从 level.dat 读真实世界名 / MC 版本 / 最后游玩时间
        fs::path lvl = e.path() / L"level.dat";
        if (!fs::is_regular_file(lvl, ec)) lvl = e.path() / L"special_level.dat";  // 20w14infinite
        if (fs::is_regular_file(lvl, ec)) {
            WorldNbt nbt = ReadWorldNbt(mlc::WToUtf8(lvl.wstring()));
            if (!nbt.name.empty()) it.title = nbt.name;
            std::string d = nbt.version;
            std::string played = fmtPlayed(nbt.played);
            if (!played.empty()) d += (d.empty() ? "" : " · ") + played;
            it.desc = d.empty() ? "存档" : d;
            it.badge = nbt.version;
        } else {
            it.desc = "（无 level.dat）";
        }
        out.push_back(std::move(it));
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.title < b.title; });
    return out;
}

static std::vector<Item> scanSchematics(const std::string& dir) {
    std::vector<Item> out;
    std::error_code ec;
    fs::path d = fs::u8path(dir);
    if (!fs::is_directory(d, ec)) return out;
    for (auto& e : fs::directory_iterator(d, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string name = mlc::WToUtf8(e.path().filename().wstring());
        std::string ext = extOf(name);
        if (ext != ".schematic" && ext != ".litematic" && ext != ".nbt" && ext != ".schem") continue;
        Item it;
        it.key = name;
        it.title = fileStem(name);
        it.toggleable = false;
        it.enabled = true;
        std::error_code szec;
        auto sz = fs::file_size(e.path(), szec);
        it.size = szec ? 0 : (int64_t)sz;
        it.desc = (ext == ".litematic" ? "Litematica" : (ext == ".schem" ? "Sponge" : "Schematic")) +
                  std::string(" · ") + humanSize(it.size);
        out.push_back(std::move(it));
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.title < b.title; });
    return out;
}

std::vector<Item> Scan(const std::string& dir, Kind kind) {
    mlc::EnsureDirs();
    switch (kind) {
        case Kind::Mods: return scanMods(dir);
        case Kind::ResourcePacks: return scanPacks(dir, kind);
        case Kind::Shaders: return scanPacks(dir, kind);
        case Kind::Worlds: return scanWorlds(dir);
        case Kind::Schematics: return scanSchematics(dir);
    }
    return {};
}

// ---------------------------------------------------------------
// 启停 / 删除 / 导入
// ---------------------------------------------------------------
// 修改 options.txt 的 resourcePacks 列表
static bool setPackEnabled(const std::string& packsDir, const std::string& key, bool enable,
                           std::string& err) {
    fs::path dataDir = fs::u8path(packsDir).parent_path();
    std::string optPath = mlc::WToUtf8((dataDir / L"options.txt").wstring());
    std::string entry = "file/" + key;

    std::string text = mlc::ReadTextFile(optPath);
    std::vector<std::string> lines;
    {
        std::istringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    if (lines.empty()) lines.push_back("version:3465");

    bool found = false;
    for (auto& line : lines) {
        if (line.rfind("resourcePacks:", 0) != 0) continue;
        found = true;
        std::string val = line.substr(strlen("resourcePacks:"));
        std::vector<std::string> arr;
        try {
            json::Value j = json::parse(val);
            for (size_t i = 0; i < j.size(); ++i) arr.push_back(j.at(i).asString());
        } catch (...) {}
        if (enable) {
            if (std::find(arr.begin(), arr.end(), entry) == arr.end()) arr.push_back(entry);
        } else {
            arr.erase(std::remove(arr.begin(), arr.end(), entry), arr.end());
        }
        json::Value a = json::makeArray();
        for (auto& s : arr) a.arr.push_back(json::makeString(s));
        line = "resourcePacks:" + json::dump(a);
        break;
    }
    if (!found && enable) {
        json::Value a = json::makeArray();
        a.arr.push_back(json::makeString("vanilla"));
        a.arr.push_back(json::makeString(entry));
        lines.push_back("resourcePacks:" + json::dump(a));
    }

    std::string out;
    for (auto& l : lines) { out += l; out += "\r\n"; }
    if (!mlc::WriteTextFile(optPath, out)) { err = "无法写入 options.txt"; return false; }
    return true;
}

bool Move(const std::string& dir, Kind kind, const std::string& key, bool up,
          std::string& err) {
    if (kind != Kind::ResourcePacks) { err = "只有资源包支持排序"; return false; }

    fs::path dataDir = fs::u8path(dir).parent_path();
    std::string optPath = mlc::WToUtf8((dataDir / L"options.txt").wstring());
    std::string text = mlc::ReadTextFile(optPath);
    std::vector<std::string> lines;
    {
        std::istringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    if (lines.empty()) lines.push_back("version:3465");

    size_t at = std::string::npos;
    std::vector<std::string> arr;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].rfind("resourcePacks:", 0) != 0) continue;
        at = i;
        try {
            json::Value j = json::parse(lines[i].substr(strlen("resourcePacks:")));
            for (size_t k = 0; k < j.size(); ++k) arr.push_back(j.at(k).asString());
        } catch (...) {}
        break;
    }
    if (at == std::string::npos) {
        arr.push_back("vanilla");
        lines.push_back("");
        at = lines.size() - 1;
    }

    const std::string entry = "file/" + key;
    auto it = std::find(arr.begin(), arr.end(), entry);
    if (it == arr.end()) {
        err = "这个资源包还没启用：先打开开关，再调整顺序";
        return false;
    }
    int idx = (int)(it - arr.begin());
    int dst = up ? idx - 1 : idx + 1;
    // "vanilla" 是内置默认包，保持它在末尾，不参与交换
    while (dst >= 0 && dst < (int)arr.size() && arr[dst] == "vanilla")
        dst += up ? -1 : 1;
    if (dst < 0 || dst >= (int)arr.size()) return true;   // 已经在边界

    std::swap(arr[idx], arr[dst]);
    json::Value a = json::makeArray();
    for (const auto& s : arr) a.arr.push_back(json::makeString(s));
    lines[at] = "resourcePacks:" + json::dump(a);

    std::string out;
    for (const auto& l : lines) { out += l; out += "\r\n"; }
    if (!mlc::WriteTextFile(optPath, out)) { err = "无法写入 options.txt"; return false; }
    return true;
}

bool Toggle(const std::string& dir, Kind kind, const std::string& key, bool enable,
            std::string& err) {
    if (kind == Kind::Mods) {
        fs::path on = fs::u8path(dir) / fs::u8path(key);
        fs::path off = fs::u8path(dir) / fs::u8path(key + ".disabled");
        std::error_code ec;
        if (enable) {
            if (fs::exists(off, ec)) {
                if (fs::exists(on, ec)) fs::remove(on, ec);
                fs::rename(off, on, ec);
            }
        } else {
            if (fs::exists(on, ec)) fs::rename(on, off, ec);
        }
        if (ec) { err = "重命名失败: " + ec.message(); return false; }
        return true;
    }
    if (kind == Kind::ResourcePacks || kind == Kind::Shaders) {
        // 光影包没有 options 启停，仅资源包支持
        if (kind == Kind::Shaders) { err = "光影包在游戏内选择，无需启停"; return false; }
        return setPackEnabled(dir, key, enable, err);
    }
    err = "该类型不支持启停";
    return false;
}

bool Remove(const std::string& dir, Kind kind, const std::string& key, std::string& err) {
    fs::path p = fs::u8path(dir) / fs::u8path(key);
    std::error_code ec;
    if (kind == Kind::Mods && !fs::exists(p, ec)) {
        fs::path off = fs::u8path(dir) / fs::u8path(key + ".disabled");
        if (fs::exists(off, ec)) p = off;
    }
    if (!fs::exists(p, ec)) { err = "文件不存在: " + key; return false; }
    fs::remove_all(p, ec);
    if (ec) { err = "删除失败: " + ec.message(); return false; }
    return true;
}

bool Import(const std::string& dir, Kind kind, const std::wstring& srcFile, std::string& outKey,
            std::string& err, const std::string& preferName) {
    std::error_code ec;
    fs::path src(srcFile);
    if (!fs::is_regular_file(src, ec)) { err = "源文件不存在"; return false; }
    fs::create_directories(fs::u8path(dir), ec);

    if (kind == Kind::Worlds) {
        std::string stem = preferName.empty() ? mlc::WToUtf8(src.stem().wstring())
                                              : safePathName(preferName);
        if (stem.empty()) stem = mlc::WToUtf8(src.stem().wstring());
        fs::path dest = fs::u8path(dir) / fs::u8path(stem);
        int n = 2;
        while (fs::exists(dest, ec)) dest = fs::u8path(dir) / fs::u8path(stem + " (" + std::to_string(n++) + ")");
        if (lower(mlc::WToUtf8(src.extension().wstring())) == ".zip") {
            fs::create_directories(dest, ec);
            if (!zip::ExtractAll(mlc::WToUtf8(src.wstring()), mlc::WToUtf8(dest.wstring()), "")) {
                err = "解压世界失败";
                return false;
            }
            // 若 zip 只包了一层文件夹，向内再探一层
            int subdirs = 0; fs::path only;
            for (auto& e : fs::directory_iterator(dest, ec)) {
                if (e.is_directory(ec)) { ++subdirs; only = e.path(); }
            }
            if (subdirs == 1 && fs::is_regular_file(only / L"level.dat", ec)) {
                fs::path tmp = dest.parent_path() / (dest.filename().wstring() + L"_tmp");
                fs::rename(only, tmp, ec);
                if (!ec) {
                    fs::remove_all(dest, ec);
                    fs::rename(tmp, dest, ec);
                }
            }
            outKey = mlc::WToUtf8(dest.filename().wstring());
            return true;
        }
        fs::copy(src, dest, ec);
        if (ec) { err = "复制失败: " + ec.message(); return false; }
        outKey = mlc::WToUtf8(dest.filename().wstring());
        return true;
    }

    fs::path dest = fs::u8path(dir) / src.filename();
    if (fs::exists(dest, ec)) {
        std::string stem = mlc::WToUtf8(src.stem().wstring());
        std::string ext = mlc::WToUtf8(src.extension().wstring());
        int n = 2;
        while (fs::exists(dest, ec))
            dest = fs::u8path(dir) / fs::u8path(stem + " (" + std::to_string(n++) + ")" + ext);
    }
    fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "复制失败: " + ec.message(); return false; }
    outKey = mlc::WToUtf8(dest.filename().wstring());
    return true;
}

bool BackupWorld(const std::string& worldDir, const std::string& tag, std::string& outPath,
                 std::string& err) {
    std::error_code ec;
    fs::path src = fs::u8path(worldDir);
    if (!fs::is_directory(src, ec)) { err = "世界目录不存在"; return false; }
    SYSTEMTIME st; GetLocalTime(&st);
    char ts[64];
    snprintf(ts, sizeof(ts), "%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    std::string base = mlc::WToUtf8(mlc::BackupsDir());
    std::string dir = base + "\\" + (tag.empty() ? "world" : tag);
    fs::create_directories(fs::u8path(dir), ec);
    fs::path dst = fs::u8path(dir) / (src.filename().wstring() + L"_" + mlc::Utf8ToW(ts));
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    if (ec) { err = "备份失败: " + ec.message(); return false; }
    outPath = mlc::WToUtf8(dst.wstring());
    return true;
}

void OpenFolder(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    ShellExecuteW(nullptr, L"open", mlc::Utf8ToW(dir).c_str(), nullptr, nullptr, SW_SHOW);
}

// ---------------------------------------------------------------
// 中英关键词映射（用于 Modrinth 英文检索）
// ---------------------------------------------------------------
bool HasNonAscii(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return true;
    return false;
}

std::string TranslateQuery(const std::string& q) {
    // 完整的中文模组名优先：「机械动力」要搜 Create，不能拆成「机械」→mechanical。
    static const std::pair<const char*, const char*> kPhrases[] = {
        { "机械动力", "create" }, { "等价交换", "ee" }, { "暮色森林", "twilight forest" },
        { "神秘时代", "thaumcraft" }, { "植物魔法", "botania" }, { "以太", "aether" },
        { "匠魂", "tinkers construct" }, { "通用机械", "mekanism" },
        { "沉浸工程", "immersive engineering" }, { "热力系列", "thermal" },
        { "星辉魔法", "astral sorcery" }, { "考古与化石", "fossil" },
        { "我的世界", "minecraft" }, { "bow", "bow" },
    };
    for (const auto& kv : kPhrases) {
        if (q.find(kv.first) != std::string::npos) return kv.second;
    }

    static const std::pair<const char*, const char*> kDict[] = {
        {"优化", "optimization"}, {"性能", "performance"}, {"帧率", "fps"},
        {"光影", "shader"}, {"着色器", "shader"}, {"资源包", "resource pack"},
        {"材质包", "resource pack"}, {"材质", "texture"}, {"高清", "hd"},
        {"小地图", "minimap"}, {"地图", "map"}, {"世界地图", "world map"},
        {"背包", "inventory"}, {"物品栏", "inventory"}, {"合成", "crafting"},
        {"配方", "recipe"}, {"界面", "ui"}, {"菜单", "menu"},
        {"科技", "tech"}, {"工业", "industrial"}, {"能源", "energy"},
        {"电力", "electric"}, {"发电机", "generator"}, {"机器", "machine"},
        {"管道", "pipe"}, {"存储", "storage"}, {"箱子", "chest"},
        {"魔法", "magic"}, {"法术", "spell"}, {"符文", "rune"},
        {"冒险", "adventure"}, {"地牢", "dungeon"}, {"维度", "dimension"},
        {"生物", "mob"}, {"怪物", "mob"}, {"动物", "animal"},
        {"农业", "farming"}, {"食物", "food"}, {"作物", "crop"},
        {"建筑", "building"}, {"家具", "furniture"}, {"装饰", "decoration"},
        {"工具", "tool"}, {"武器", "weapon"}, {"装备", "equipment"},
        {"附魔", "enchantment"}, {"药水", "potion"}, {"盔甲", "armor"},
        {"自动化", "automation"}, {"红石", "redstone"}, {"机械", "mechanical"},
        {"载具", "vehicle"}, {"飞机", "plane"}, {"汽车", "car"},
        {"枪械", "gun"}, {"战斗", "combat"}, {"boss", "boss"},
        {"汉化", "chinese"}, {"中文", "chinese"}, {"翻译", "translation"},
        {"辅助", "utility"}, {"信息", "info"}, {"显示", "display"},
        {"血量", "health"}, {"伤害", "damage"}, {"统计", "stats"},
        {"存档", "save"}, {"备份", "backup"}, {"服务器", "server"},
        {"多人", "multiplayer"}, {"联机", "multiplayer"}, {"整合包", "modpack"},
        {"光影包", "shaderpack"}, {"原版", "vanilla"}, {"生存", "survival"},
        {"创造", "creative"}, {"空岛", "skyblock"}, {"跑酷", "parkour"},
        {"迷宫", "maze"}, {"解谜", "puzzle"}, {"剧情", "story"},
        {"音效", "sound"}, {"音乐", "music"}, {"画面", "visual"},
        {"水面", "water"}, {"天气", "weather"}, {"季节", "season"},
        {"村庄", "village"}, {"城市", "city"}, {"中世纪", "medieval"},
        {"现代", "modern"}, {"未来", "futuristic"}, {"科幻", "sci-fi"},
        {"可爱", "cute"}, {"写实", "realistic"}, {"卡通", "cartoon"},
        {"低配", "low end"}, {"兼容", "compatibility"}, {"修复", "fix"},
        {"错误", "bug"}, {"库", "library"}, {"前置", "api"}, {"核心", "core"},
    };

    std::string q2 = q;
    // 逐个关键词替换（只在包含中文时处理）
    std::vector<std::string> parts;
    for (const auto& kv : kDict) {
        if (q2.find(kv.first) != std::string::npos) {
            parts.push_back(kv.second);
        }
    }
    if (parts.empty()) return std::string();
    std::string out;
    for (size_t i = 0; i < parts.size() && i < 4; ++i) {
        if (i) out += " ";
        out += parts[i];
    }
    return out;
}

// ---------------------------------------------------------------
// Modrinth 搜索 / 下载
// ---------------------------------------------------------------
static std::vector<Hit> searchOnce(const std::string& query, const std::string& projectType,
                                   const std::string& loader, const std::string& mcVersion,
                                   int limit, int offset, const std::string& facetsIn,
                                   std::string& err) {
    std::vector<Hit> out;
    std::string facets = facetsIn;
    if (facets.empty()) {
        facets = "[[\"project_type:" + projectType + "\"]";
        if (!loader.empty()) facets += ",[\"categories:" + loader + "\"]";
        if (!mcVersion.empty()) facets += ",[\"versions:" + mcVersion + "\"]";
        facets += "]";
    }

    // 空查询 = 浏览模式（HMCL 的「全部」），按下载量排；有词才按相关性排
    std::string url = "https://api.modrinth.com/v2/search?query=" + urlEncode(query) +
                      "&limit=" + std::to_string(limit) +
                      "&offset=" + std::to_string(offset) +
                      "&index=" + std::string(query.empty() ? "downloads" : "relevance") +
                      "&facets=" + urlEncode(facets);
    http::Response r = http::Get(url, 20000);
    if (r.status < 0) { err = "网络错误: " + r.error; return out; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return out; }

    try {
        json::Value j = json::parse(r.body);
        const json::Value& hits = j.get("hits");
        for (size_t i = 0; i < hits.size(); ++i) {
            Hit h;
            h.slug = hits.at(i).get("slug").asString();
            h.title = hits.at(i).get("title").asString();
            h.desc = hits.at(i).get("description").asString();
            h.iconUrl = hits.at(i).get("icon_url").asString();
            h.author = hits.at(i).get("author").asString();
            h.downloads = hits.at(i).get("downloads").asInt();
            if (!h.slug.empty()) out.push_back(std::move(h));
        }
    } catch (const std::exception& e) { err = e.what(); }
    return out;
}

std::vector<Hit> Search(const std::string& query, const std::string& projectType,
                        const std::string& loader, const std::string& mcVersion,
                        int limit, int offset, std::string& err) {
    if (limit <= 0) limit = 20;
    if (offset < 0) offset = 0;
    std::vector<Hit> all;
    std::set<std::string> seen;

    auto merge = [&](std::vector<Hit>&& hits) {
        for (auto& h : hits) {
            if (seen.insert(h.slug).second) all.push_back(std::move(h));
        }
    };

    // 没输入关键词：像 HMCL 一样显示该分类的全部热门项目，只按类型过滤，
    // 不加「当前实例的 MC 版本 / 加载器」过滤（否则 1.21.11 这种新版本会一页都没有）。
    if (query.empty()) {
        merge(searchOnce("", projectType, "", "", limit, offset, "", err));
        return all;
    }

    std::string firstErr;
    merge(searchOnce(query, projectType, loader, mcVersion, limit, offset, "", firstErr));

    // 多语言：含中文时翻译后追加搜索（英文可补充更多结果）
    std::string tr = TranslateQuery(query);
    if (!tr.empty() && lower(tr) != lower(query)) {
        std::string e2;
        merge(searchOnce(tr, projectType, loader, mcVersion, limit, offset, "", e2));
    }
    // 中文查询但结果过少：用更长关键词兜底
    if (all.size() < 5 && HasNonAscii(query)) {
        std::string e3;
        merge(searchOnce(tr.empty() ? "chinese" : tr + " chinese", projectType, loader,
                         mcVersion, limit, offset, "", e3));
    }
    // 带版本 / 加载器过滤后太少：放开过滤再搜一次（HMCL 也会提示「没有兼容版本，
    // 显示全部」）。分页时不再追加，避免页内混进重复项。
    if (all.size() < 5 && offset == 0 && (!loader.empty() || !mcVersion.empty())) {
        std::string loose = "[[\"project_type:" + projectType + "\"]]";
        std::string e4;
        merge(searchOnce(query, projectType, "", "", limit, 0, loose, e4));
        if (!tr.empty() && lower(tr) != lower(query)) {
            std::string e5;
            merge(searchOnce(tr, projectType, "", "", limit, 0, loose, e5));
        }
    }
    if (all.empty() && !firstErr.empty()) err = firstErr;
    return all;
}

bool Download(const std::string& slug, Kind kind, const std::string& destDir,
              const std::string& loader, const std::string& mcVersion,
              std::string& outFile, std::string& err) {
    std::string url = "https://api.modrinth.com/v2/project/" + urlEncode(slug) + "/version";
    std::string q;
    if (!loader.empty()) q += "loaders=" + urlEncode("[\"" + loader + "\"]");
    if (!mcVersion.empty()) {
        if (!q.empty()) q += "&";
        q += "game_versions=" + urlEncode("[\"" + mcVersion + "\"]");
    }
    if (!q.empty()) url += "?" + q;

    http::Response r = http::Get(url, 25000);
    if (r.status < 0) { err = "网络错误: " + r.error; return false; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return false; }

    try {
        json::Value j = json::parse(r.body);
        if (!j.isArray() || j.size() == 0) { err = "未找到兼容的游戏版本"; return false; }

        std::error_code ec;
        fs::create_directories(fs::u8path(destDir), ec);

        // 优先 primary 文件，其次非 sources/javadoc
        for (size_t vi = 0; vi < j.size() && vi < 6; ++vi) {
            const json::Value& files = j.at(vi).get("files");
            std::string fallbackUrl, fallbackName;
            for (size_t fi = 0; fi < files.size(); ++fi) {
                std::string fn = files.at(fi).get("filename").asString();
                std::string dl = files.at(fi).get("url").asString();
                if (dl.empty() || fn.empty()) continue;
                std::string low = lower(fn);
                if (low.find("sources") != std::string::npos ||
                    low.find("javadoc") != std::string::npos ||
                    low.find("-dev") != std::string::npos) continue;
                bool primary = files.at(fi).get("primary").asBool();
                if (primary) { fallbackUrl = dl; fallbackName = fn; break; }
                if (fallbackUrl.empty()) { fallbackUrl = dl; fallbackName = fn; }
            }
            if (fallbackUrl.empty()) continue;
            fs::path dest = fs::u8path(destDir) / mlc::Utf8ToW(fallbackName);
            if (fs::exists(dest, ec)) {
                std::string stem = mlc::WToUtf8(dest.stem().wstring());
                std::string ext = mlc::WToUtf8(dest.extension().wstring());
                int n = 2;
                while (fs::exists(dest, ec))
                    dest = fs::u8path(destDir) / mlc::Utf8ToW(stem + " (" + std::to_string(n++) + ")" + ext);
            }
            http::DownloadResult d = http::Download(fallbackUrl, mlc::WToUtf8(dest.wstring()),
                                                    nullptr, 180000, nullptr);
            if (!d.ok) { err = "下载失败: " + d.error; return false; }
            // 模组/整合包必须能跑：解压整合包交给调用方处理（这里只落文件）
            (void)kind;
            outFile = mlc::WToUtf8(dest.wstring());
            return true;
        }
        err = "该版本没有可下载文件";
        return false;
    } catch (const std::exception& e) { err = e.what(); return false; }
}

// ---------------------------------------------------------------
// CurseForge（世界 / 地图在线下载）
// ---------------------------------------------------------------
namespace cf {

// 收尾斜杠并兜底官方地址
static std::string apiBase(const std::string& custom) {
    std::string b = custom;
    while (!b.empty() && (b.back() == '/' || b.back() == ' ')) b.pop_back();
    if (b.empty()) return "https://api.curseforge.com/v1";
    return b;
}

static http::Response apiGet(const std::string& url, const std::string& apiKey) {
    if (!apiKey.empty()) return http::GetWithHeader(url, "x-api-key: " + apiKey, 25000);
    return http::Get(url, 25000);
}

std::string CdnUrl(int fileId, const std::string& fileName) {
    if (fileId <= 0 || fileName.empty()) return std::string();
    char a[24], b[24];
    snprintf(a, sizeof(a), "%d", fileId / 1000);
    snprintf(b, sizeof(b), "%03d", fileId % 1000);
    // forgecdn 的文件直链规则：files/<id/1000>/<id%1000>/<urlencoded 文件名>
    return std::string("https://mediafilez.forgecdn.net/files/") + a + "/" + b + "/" +
           urlEncode(fileName);
}

// 从搜索结果里挑一个可下载的文件（优先正式版 + zip）
static void pickFile(const json::Value& idxs, int& fileId, std::string& fileName,
                     std::string& gameVersion) {
    fileId = 0;
    fileName.clear();
    gameVersion.clear();
    std::string fallbackName;
    int fallbackId = 0;
    std::string fallbackVer;
    for (size_t i = 0; i < idxs.size(); ++i) {
        const json::Value& f = idxs.at(i);
        std::string fn = f.get("filename").asString();
        int fid = (int)f.get("fileId").asInt();
        if (fn.empty() || fid <= 0) continue;
        std::string low = fn;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (low.size() < 4 || low.substr(low.size() - 4) != ".zip") continue;
        if (fileId == 0) {
            fileId = fid;
            fileName = fn;
            gameVersion = f.get("gameVersion").asString();
        }
        if (fallbackId == 0) {
            fallbackId = fid;
            fallbackName = fn;
            fallbackVer = f.get("gameVersion").asString();
        }
        if ((int)f.get("releaseType").asInt() == 1) {
            fileId = fid;
            fileName = fn;
            gameVersion = f.get("gameVersion").asString();
            return;
        }
    }
    if (fileId == 0 && fallbackId > 0) {
        fileId = fallbackId;
        fileName = fallbackName;
        gameVersion = fallbackVer;
    }
}

static void parseHits(const std::string& body, std::vector<WorldHit>& out) {
    json::Value j = json::parse(body);
    const json::Value& arr = j.get("data");
    for (size_t i = 0; i < arr.size(); ++i) {
        const json::Value& m = arr.at(i);
        WorldHit h;
        h.modId = (int)m.get("id").asInt();
        if (h.modId <= 0) continue;
        h.name = m.get("name").asString();
        h.summary = m.get("summary").asString();
        h.downloads = m.get("downloadCount").asInt();
        h.author = m.get("authors").at(0).get("name").asString();
        h.website = m.get("links").get("websiteUrl").asString();
        h.coverUrl = m.get("logo").get("thumbnailUrl").asString();
        pickFile(m.get("latestFilesIndexes"), h.fileId, h.fileName, h.gameVersion);
        out.push_back(std::move(h));
    }
}

std::vector<WorldHit> SearchWorlds(const std::string& query, int index, int pageSize,
                                   const std::string& base, const std::string& apiKey,
                                   std::string& err) {
    if (pageSize <= 0 || pageSize > 50) pageSize = 24;
    if (index < 0) index = 0;
    std::vector<WorldHit> all;
    std::set<int> seen;

    auto runQuery = [&](const std::string& q, std::string& qerr) {
        std::string url = apiBase(base) +
            "/mods/search?gameId=" + std::to_string(kGameId) +
            "&classId=" + std::to_string(kClassWorlds) +
            "&sortField=2&sortOrder=desc" +
            "&index=" + std::to_string(index) +
            "&pageSize=" + std::to_string(pageSize);
        if (!q.empty()) url += "&searchFilter=" + urlEncode(q);
        http::Response r = apiGet(url, apiKey);
        if (r.status < 0) { qerr = "网络错误: " + r.error; return; }
        if (r.status == 403) { qerr = "CurseForge 拒绝访问（官方接口需要 API Key，见「全局设置」）"; return; }
        if (r.status >= 400) { qerr = "HTTP " + std::to_string(r.status); return; }
        try {
            std::vector<WorldHit> tmp;
            parseHits(r.body, tmp);
            for (auto& h : tmp) {
                if (seen.insert(h.modId).second) all.push_back(std::move(h));
            }
        } catch (const std::exception& e) { qerr = e.what(); }
    };

    std::string firstErr;
    runQuery(query, firstErr);

    // 中文关键词：翻译后补一次（CurseForge 只认英文，效果更好）
    std::string tr = TranslateQuery(query);
    if (!tr.empty() && lower(tr) != lower(query)) {
        std::string e2;
        runQuery(tr, e2);
    }
    if (all.empty() && !firstErr.empty()) err = firstErr;
    return all;
}

bool InstallWorld(const WorldHit& hit, const std::string& savesDir,
                  const std::string& base, const std::string& apiKey,
                  std::string& outName, std::string& err) {
    if (hit.modId <= 0 || hit.fileId <= 0) { err = "该世界没有可下载的 zip 文件"; return false; }
    std::string low = lower(hit.fileName);
    if (low.size() < 4 || low.substr(low.size() - 4) != ".zip") {
        err = "该世界不是 zip 格式（" + hit.fileName + "），暂不支持自动安装";
        return false;
    }

    std::error_code ec;
    std::string tmpDir = mlc::WToUtf8(mlc::BaseDir()) + "\\cache\\tmp";
    fs::create_directories(fs::u8path(tmpDir), ec);
    std::string tmp = tmpDir + "\\cf_world_" + std::to_string(hit.fileId) + ".zip";

    // 1) forgecdn 直链（大多数文件可用）
    http::DownloadResult r = http::Download(CdnUrl(hit.fileId, hit.fileName), tmp,
                                            nullptr, 600000, nullptr);
    // 2) 失败时问 API 要 download-url（项目允许第三方分发时才有值）
    if (!r.ok) {
        std::string url = apiBase(base) + "/mods/" + std::to_string(hit.modId) +
                          "/files/" + std::to_string(hit.fileId) + "/download-url";
        http::Response resp = apiGet(url, apiKey);
        if (resp.status == 200) {
            std::string du;
            try { du = json::parse(resp.body).get("data").asString(); } catch (...) {}
            if (!du.empty()) {
                r = http::Download(du, tmp, nullptr, 600000, nullptr);
            }
        }
        if (!r.ok) {
            err = "下载失败：" + (r.error.empty() ? "作者禁止第三方下载" : r.error);
            return false;
        }
    }

    bool ok = Import(savesDir, Kind::Worlds, mlc::Utf8ToW(tmp), outName, err, hit.name);
    fs::remove(fs::u8path(tmp), ec);
    return ok;
}

} // namespace cf

} // namespace content
