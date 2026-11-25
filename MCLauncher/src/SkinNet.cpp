// SkinNet.cpp — 正版皮肤上传 / 拉取
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "SkinNet.h"
#include "Http.h"
#include "Json.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace skin {

namespace {

// ---------------- base64 ----------------
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string Base64Decode(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 3 / 4 + 4);
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' ) break;
        int v = b64val(c);
        if (v < 0) continue;                  // 跳过换行等
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------------- 小工具 ----------------
bool ReadAll(const std::wstring& path, std::string& out) {
    std::ifstream f(std::wstring(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    f.read(&out[0], n);
    return f.good() || f.eof();
}

std::string RandomBoundary() {
    static uint32_t seed = (uint32_t)GetTickCount64() ^ (uint32_t)GetCurrentProcessId();
    seed = seed * 1103515245u + 12345u;
    static const char* hex = "0123456789abcdef";
    std::string s = "----YXGSkinBoundary";
    for (int i = 0; i < 16; ++i) {
        s.push_back(hex[(seed >> ((i % 8) * 4)) & 0xF]);
        seed = seed * 1103515245u + 12345u;
    }
    return s;
}

// 从 textures 的 base64 JSON 里取某个纹理（SKIN / CAPE）
bool TextureUrl(const json::Value& textures, const char* kind, std::string& url,
                std::string& model) {
    const json::Value& t = textures.get(kind);
    if (!t.isObject()) return false;
    url = t.get("url").asString();
    const json::Value& md = t.get("metadata");
    if (md.isObject()) model = md.get("model").asString();
    return !url.empty();
}

// 拉 profile，解析出 SKIN / CAPE 的 URL
bool LoadProfileTextures(const std::string& uuid, json::Value& textures, std::string& err) {
    std::string id = CompactUuid(uuid);
    if (id.size() != 32) { err = "账号 UUID 无效"; return false; }
    std::string url = "https://sessionserver.mojang.com/session/minecraft/profile/" + id;
    http::Response r = http::Get(url, 20000);
    if (r.status == 204) { err = "该账号没有皮肤信息"; return false; }
    if (r.status != 200) {
        err = "查询皮肤失败（HTTP " + std::to_string(r.status) + "）：" +
              (r.error.empty() ? r.body.substr(0, 200) : r.error);
        return false;
    }
    try {
        json::Value j = json::parse(r.body);
        const json::Value& props = j.get("properties");
        for (size_t i = 0; i < props.size(); ++i) {
            const json::Value& p = props.at(i);
            if (p.get("name").asString() != "textures") continue;
            std::string inner = Base64Decode(p.get("value").asString());
            if (inner.empty()) { err = "皮肤数据为空"; return false; }
            json::Value tj = json::parse(inner);
            textures = tj.get("textures");
            return textures.isObject();
        }
        err = "返回里没有 textures 属性";
        return false;
    } catch (const std::exception& e) {
        err = std::string("解析皮肤数据失败：") + e.what();
        return false;
    }
}

// 下载一张纹理到本地（先删旧文件，避免失败后留着坏图）
bool DownloadTexture(const std::string& urlIn, const std::wstring& destPng, std::string& err) {
    // Mojang 返回的纹理 URL 常是 http://，统一走 https，免得被 301 打断
    std::string url = urlIn;
    if (url.rfind("http://", 0) == 0) url = "https://" + url.substr(7);
    ::DeleteFileW(destPng.c_str());
    http::DownloadResult r = http::Download(url, http::WideToUtf8(destPng), nullptr, 30000);
    if (!r.ok) { err = "下载皮肤图失败：" + r.error; return false; }
    return true;
}

} // namespace

std::string CompactUuid(const std::string& uuid) {
    std::string o;
    o.reserve(32);
    for (char c : uuid) {
        if (c == '-') continue;
        o.push_back(c);
    }
    std::transform(o.begin(), o.end(), o.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return o;
}

bool UploadToMojang(const std::string& accessToken, const std::wstring& pngPath,
                    bool slim, std::string& err) {
    if (accessToken.empty()) { err = "当前账号没有 Minecraft 令牌，请先重新登录"; return false; }

    std::string bytes;
    if (!ReadAll(pngPath, bytes)) { err = "读取皮肤文件失败"; return false; }

    const std::string boundary = RandomBoundary();
    std::string body;
    body.reserve(bytes.size() + 512);
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"variant\"\r\n\r\n";
    body += slim ? "slim" : "classic";
    body += "\r\n--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"skin.png\"\r\n";
    body += "Content-Type: image/png\r\n\r\n";
    body += bytes;
    body += "\r\n--" + boundary + "--\r\n";

    http::Response r = http::PostWithHeader(
        "https://api.minecraftservices.com/minecraft/profile/skins", body,
        "multipart/form-data; boundary=" + boundary,
        "Authorization: Bearer " + accessToken, 60000);

    if (r.status == 401 || r.status == 403) {
        err = "令牌无效或已过期，请重新登录正版账号（HTTP " + std::to_string(r.status) + "）";
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        // Mojang 的错误响应形如 {"errorType":"...","error":"...","errorMessage":"..."}
        std::string detail;
        try {
            json::Value j = json::parse(r.body);
            detail = j.get("errorMessage").asString();
            if (detail.empty()) detail = j.get("error").asString();
        } catch (...) {}
        if (detail.empty()) detail = r.body.substr(0, 200);
        err = "上传失败（HTTP " + std::to_string(r.status) + "）" +
              (detail.empty() ? "" : "：" + detail);
        return false;
    }
    return true;
}

bool FetchFromMojang(const std::string& uuid, const std::wstring& destPng,
                     const std::wstring& capeDest, bool* slimOut, bool* hasCapeOut,
                     std::string& err) {
    if (hasCapeOut) *hasCapeOut = false;
    json::Value textures;
    if (!LoadProfileTextures(uuid, textures, err)) return false;

    // 披风（可选，失败不影响皮肤）
    if (!capeDest.empty()) {
        std::string capeUrl, capeModel;
        if (TextureUrl(textures, "CAPE", capeUrl, capeModel)) {
            std::string tmpErr;
            if (DownloadTexture(capeUrl, capeDest, tmpErr) && hasCapeOut) *hasCapeOut = true;
        }
    }

    std::string url, model;
    if (!TextureUrl(textures, "SKIN", url, model)) {
        err = "该正版账号还没有设置皮肤";
        return false;
    }
    if (slimOut) *slimOut = (model == "slim");
    if (!DownloadTexture(url, destPng, err)) return false;
    return true;
}

bool FetchCapeFromMojang(const std::string& uuid, const std::wstring& destPng,
                         bool* found, std::string& err) {
    if (found) *found = false;
    json::Value textures;
    if (!LoadProfileTextures(uuid, textures, err)) return false;

    std::string url, model;
    if (!TextureUrl(textures, "CAPE", url, model)) {
        err = "该正版账号没有披风";
        return false;
    }
    if (!DownloadTexture(url, destPng, err)) return false;
    if (found) *found = true;
    return true;
}

} // namespace skin
