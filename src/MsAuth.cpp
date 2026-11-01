// MsAuth.cpp — 微软 OAuth2 设备码流程实现
#include "MsAuth.h"
#include "Json.h"
#include "Http.h"

#include <thread>
#include <chrono>

namespace msauth {

namespace {
// Minecraft Launcher 的 Azure 客户端 ID（公开常量）
const char* kClientId = "00000000402b5328";
const char* kScope = "XboxLive.signin offline_access";
}

bool StartDeviceCodeFlow(DeviceCode& out, std::string& err) {
    std::string url = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
    std::string body = "client_id=" + std::string(kClientId) + "&scope=" + std::string(kScope);
    http::Response r = http::Post(url, body, "application/x-www-form-urlencoded", 30000);
    if (r.status < 0) { err = "网络错误: " + r.error; return false; }
    if (r.status >= 400) { err = "HTTP " + std::to_string(r.status); return false; }

    json::Value j;
    try { j = json::parse(r.body); }
    catch (const std::exception& e) { err = std::string("JSON 解析失败: ") + e.what(); return false; }

    out.userCode = j.get("user_code").asString();
    out.verificationUri = j.get("verification_uri").asString();
    out.deviceCode = j.get("device_code").asString();
    out.message = j.get("message").asString();
    if (j.has("expires_in")) out.expiresIn = (int)j.get("expires_in").asInt();
    if (j.has("interval")) out.interval = (int)j.get("interval").asInt();
    if (out.deviceCode.empty()) { err = "响应缺少 device_code"; return false; }
    return true;
}

namespace {

// 解析微软 token 响应中的 access_token（可能是错误响应）
bool parseToken(const std::string& body, std::string& accessToken, std::string& refreshToken, std::string& err) {
    json::Value j;
    try { j = json::parse(body); }
    catch (const std::exception& e) { err = std::string("JSON 解析失败: ") + e.what(); return false; }

    if (j.has("error")) {
        err = j.get("error").asString();
        if (j.has("error_description")) err += ": " + j.get("error_description").asString();
        return false;
    }
    accessToken = j.get("access_token").asString();
    refreshToken = j.get("refresh_token").asString();
    if (accessToken.empty()) { err = "响应缺少 access_token"; return false; }
    return true;
}

// XBL 认证：用微软 access token 换 XBL token
bool xblAuthenticate(const std::string& msToken, std::string& xblToken, std::string& err) {
    json::Value req = json::makeObject();
    json::Value props = json::makeObject();
    props.obj["AuthMethod"] = json::makeString("RPS");
    props.obj["SiteName"] = json::makeString("user.auth.xboxlive.com");
    props.obj["RpsTicket"] = json::makeString("d=" + msToken);
    req.obj["Properties"] = props;
    req.obj["RelyingParty"] = json::makeString("http://auth.xboxlive.com");
    req.obj["TokenType"] = json::makeString("JWT");

    http::Response r = http::Post("https://user.auth.xboxlive.com/user/authenticate",
                                  json::dump(req), "application/json", 30000);
    if (r.status < 0) { err = "XBL 网络错误: " + r.error; return false; }
    if (r.status >= 400) { err = "XBL HTTP " + std::to_string(r.status); return false; }

    json::Value j;
    try { j = json::parse(r.body); }
    catch (const std::exception& e) { err = std::string("XBL JSON 解析失败: ") + e.what(); return false; }
    xblToken = j.get("Token").asString();
    if (xblToken.empty()) { err = "XBL 响应缺少 Token"; return false; }
    return true;
}

// XSTS 授权：用 XBL token 换 XSTS token + user hash
bool xstsAuthorize(const std::string& xblToken, std::string& xstsToken, std::string& uhs, std::string& err) {
    json::Value req = json::makeObject();
    json::Value props = json::makeObject();
    props.obj["SandboxId"] = json::makeString("RETAIL");
    props.obj["UserTokens"] = json::makeArray();
    props.obj["UserTokens"].arr.push_back(json::makeString(xblToken));
    req.obj["Properties"] = props;
    req.obj["RelyingParty"] = json::makeString("rp://api.minecraftservices.com/");
    req.obj["TokenType"] = json::makeString("JWT");

    http::Response r = http::Post("https://xsts.auth.xboxlive.com/xsts/authorize",
                                  json::dump(req), "application/json", 30000);
    if (r.status < 0) { err = "XSTS 网络错误: " + r.error; return false; }
    if (r.status >= 400) { err = "XSTS HTTP " + std::to_string(r.status); return false; }

    json::Value j;
    try { j = json::parse(r.body); }
    catch (const std::exception& e) { err = std::string("XSTS JSON 解析失败: ") + e.what(); return false; }

    // 若错误（如未成年/无游戏），XErr 说明原因
    if (j.has("XErr")) {
        err = "XSTS 错误 XErr=" + j.get("XErr").asString() + " (" + j.get("Message").asString() + ")";
        return false;
    }
    xstsToken = j.get("Token").asString();
    uhs = j.get("DisplayClaims").get("xui").at(0).get("uhs").asString();
    if (xstsToken.empty() || uhs.empty()) { err = "XSTS 响应缺少 Token/uhs"; return false; }
    return true;
}

// Minecraft 登录：用 XSTS token 换 Minecraft access token
bool minecraftLogin(const std::string& uhs, const std::string& xstsToken,
                    std::string& mcToken, std::string& err) {
    json::Value req = json::makeObject();
    req.obj["identityToken"] = json::makeString("XBL3.0 x=" + uhs + ";" + xstsToken);

    http::Response r = http::Post("https://api.minecraftservices.com/authentication/login_with_xbox",
                                  json::dump(req), "application/json", 30000);
    if (r.status < 0) { err = "Minecraft 登录网络错误: " + r.error; return false; }
    if (r.status >= 400) { err = "Minecraft 登录 HTTP " + std::to_string(r.status); return false; }

    json::Value j;
    try { j = json::parse(r.body); }
    catch (const std::exception& e) { err = std::string("JSON 解析失败: ") + e.what(); return false; }
    mcToken = j.get("access_token").asString();
    if (mcToken.empty()) { err = "未获取到 Minecraft access_token"; return false; }
    return true;
}

// 获取玩家 profile（name + uuid），需带 Bearer 头
bool FetchProfile(const std::string& mcToken, Account& out, std::string& err) {
    http::Response r = http::GetWithHeader(
        "https://api.minecraftservices.com/minecraft/profile",
        "Authorization: Bearer " + mcToken, 30000);
    if (r.status < 0) { err = "获取玩家信息失败: " + r.error; return false; }
    if (r.status >= 400) { err = "获取玩家信息 HTTP " + std::to_string(r.status); return false; }

    json::Value j;
    try { j = json::parse(r.body); }
    catch (const std::exception& e) { err = std::string("JSON 解析失败: ") + e.what(); return false; }

    out.name = j.get("name").asString();
    out.uuid = j.get("id").asString();
    if (out.name.empty() || out.uuid.empty()) { err = "玩家信息不完整"; return false; }
    if (out.uuid.size() == 32 && out.uuid.find('-') == std::string::npos) {
        out.uuid = out.uuid.substr(0, 8) + "-" + out.uuid.substr(8, 4) + "-" + out.uuid.substr(12, 4) +
                   "-" + out.uuid.substr(16, 4) + "-" + out.uuid.substr(20, 12);
    }
    return true;
}

} // namespace

int PollForToken(const DeviceCode& code, Account& out, std::string& err) {
    std::string url = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
    std::string body = "grant_type=urn:ietf:params:oauth:grant-type:device_code"
                       "&client_id=" + std::string(kClientId) +
                       "&device_code=" + code.deviceCode;
    http::Response r = http::Post(url, body, "application/x-www-form-urlencoded", 30000);
    if (r.status < 0) { err = "网络错误: " + r.error; return -1; }

    std::string accessToken, refreshToken;
    std::string perr;
    if (!parseToken(r.body, accessToken, refreshToken, perr)) {
        // 错误可能是 authorization_pending（继续等）或其它
        if (perr == "authorization_pending") return 0;
        if (perr == "slow_down") return 0;
        if (perr == "authorization_declined") { err = "用户拒绝了授权"; return -1; }
        if (perr == "expired_token") { err = "设备码已过期"; return -1; }
        err = perr.empty() ? ("HTTP " + std::to_string(r.status)) : perr;
        return -1;
    }

    // 拿到微软 access token → XBL → XSTS → Minecraft
    std::string xbl, xsts, uhs, mcToken;
    if (!xblAuthenticate(accessToken, xbl, err)) return -1;
    if (!xstsAuthorize(xbl, xsts, uhs, err)) return -1;
    if (!minecraftLogin(uhs, xsts, mcToken, err)) return -1;

    // 获取 profile（用带 Bearer 头的 GET）
    out.accessToken = mcToken;
    if (!FetchProfile(mcToken, out, err)) return -1;
    out.ok = true;
    return 1;
}

bool FullLogin(std::function<void(const DeviceCode&)> onDeviceCode,
               Account& out, std::string& err) {
    DeviceCode dc;
    if (!StartDeviceCodeFlow(dc, err)) return false;
    if (onDeviceCode) onDeviceCode(dc);

    int waited = 0;
    while (waited < dc.expiresIn) {
        int r = PollForToken(dc, out, err);
        if (r == 1) return true;
        if (r == -1) return false;
        int sleepSec = dc.interval > 0 ? dc.interval : 5;
        std::this_thread::sleep_for(std::chrono::seconds(sleepSec));
        waited += sleepSec;
    }
    err = "登录超时";
    return false;
}

} // namespace msauth
