// MsAuth.cpp — 微软 OAuth 设备码登录实现
//
// 两套端点（实测）：
//  1) 内置 legacy（client_id = 00000000402b5328，即 Minecraft 启动器旧应用）
//     * login.live.com/oauth20_connect.srf  -> 设备码（scope = service::user.auth.xboxlive.com::MBI_SSL）
//     * login.live.com/oauth20_token.srf    -> 轮询
//     这条能直接用，但 scope 不能叠加 wl.offline_access（微软会报 invalid_scope），
//     所以**没有 refresh_token**，令牌约 24h，过期需要重新登录。
//     注意：绝不能用 login.microsoftonline.com/consumers/oauth2/v2.0/devicecode，
//     那个端点只认 Azure AD 注册的应用，传 00000000402b5328 会返回
//     AADSTS700016（Application ... was not found）——这正是旧版「正版登录用不了」的原因。
//  2) 自定义 Azure 应用（用户在设置里填 GUID）
//     * login.microsoftonline.com/consumers/oauth2/v2.0/devicecode / token
//     * scope 可带 offline_access，拿得到 refresh_token，可自动续期。
#include "MsAuth.h"
#include "Json.h"
#include "Http.h"

#include <thread>
#include <chrono>
#include <cctype>
#include <ctime>
#include <mutex>

namespace msauth {

namespace {

const char* kLegacyClientId = "00000000402b5328";
const char* kLegacyConnectUrl = "https://login.live.com/oauth20_connect.srf";
const char* kLegacyTokenUrl   = "https://login.live.com/oauth20_token.srf";
const char* kLegacyScope      = "service::user.auth.xboxlive.com::MBI_SSL";

const char* kAadDeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
const char* kAadTokenUrl      = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
const char* kAadScope         = "XboxLive.signin offline_access";

std::mutex g_mu;
std::string g_clientId;      // 空 = 用 legacy

// form 值编码（device_code 里可能出现 ! $ 等字符，必须转义）
std::string formEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string effectiveClientId() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_clientId.empty() ? std::string(kLegacyClientId) : g_clientId;
}

bool usingCustom() {
    std::lock_guard<std::mutex> lk(g_mu);
    return !g_clientId.empty();
}

const char* tokenUrl() { return usingCustom() ? kAadTokenUrl : kLegacyTokenUrl; }
const char* scopeValue() { return usingCustom() ? kAadScope : kLegacyScope; }

// XSTS 错误码 → 人话
std::string xerrText(const std::string& xerr) {
    if (xerr == "2148916233") return "该微软账号还没有 Xbox 档案，请先到 xbox.com 创建一个";
    if (xerr == "2148916235") return "你所在的国家/地区不支持 Xbox Live";
    if (xerr == "2148916236" || xerr == "2148916237")
        return "该账号需要完成成人验证（到 account.microsoft.com 处理）";
    if (xerr == "2148916238") return "未成年账号需要先加入 Microsoft 家庭组才能登录";
    if (xerr == "2148916227") return "该账号已被 Xbox Live 封禁";
    return "Xbox 授权失败（XErr=" + xerr + "）";
}

} // namespace

void SetClientId(const std::string& clientId) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_clientId = clientId;
}

std::string ClientId() { return effectiveClientId(); }

bool UsingCustomClient() { return usingCustom(); }

bool StartDeviceCodeFlow(DeviceCode& out, std::string& err) {
    const bool custom = usingCustom();
    const std::string clientId = effectiveClientId();

    std::string body = "client_id=" + formEncode(clientId) +
                       "&scope=" + formEncode(scopeValue());
    if (custom) body += "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
    else body += "&response_type=device_code";

    const char* url = custom ? kAadDeviceCodeUrl : kLegacyConnectUrl;
    http::Response r = http::Post(url, body, "application/x-www-form-urlencoded", 30000);
    if (r.status < 0) { err = "网络错误: " + r.error; return false; }
    if (r.status >= 400) {
        // 尽量把微软的错误码带出来，便于用户判断（比如自定义 ID 没开公共客户端）
        std::string detail = r.body;
        try {
            json::Value e = json::parse(r.body);
            std::string code = e.get("error").asString();
            std::string desc = e.get("error_description").asString();
            if (!code.empty()) detail = code + ": " + desc;
        } catch (...) {}
        err = "获取设备码失败 (HTTP " + std::to_string(r.status) + "): " + detail;
        return false;
    }

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
    if (out.verificationUri.empty()) out.verificationUri = "https://www.microsoft.com/link";
    if (out.interval <= 0) out.interval = 5;
    if (out.expiresIn <= 0) out.expiresIn = 900;
    return true;
}

namespace {

// 解析微软 token 响应中的 access_token（可能是错误响应）
bool parseToken(const std::string& body, std::string& accessToken, std::string& refreshToken,
                std::string& err, int* expiresIn = nullptr) {
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
    if (expiresIn) *expiresIn = j.has("expires_in") ? (int)j.get("expires_in").asInt() : 0;
    if (accessToken.empty()) { err = "响应缺少 access_token"; return false; }
    return true;
}

// XBL 认证：用微软 access token 换 XBL token + uhs
bool xblAuthenticate(const std::string& msToken, std::string& xblToken, std::string& uhs,
                     std::string& err) {
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
    if (r.status >= 400) {
        err = "XBL 认证失败 (HTTP " + std::to_string(r.status) + ")";
        if (r.body.find("InvalidToken") != std::string::npos)
            err += "：微软令牌无效或已过期，请重新登录";
        return false;
    }

    json::Value j;
    try { j = json::parse(r.body); }
    catch (const std::exception& e) { err = std::string("XBL JSON 解析失败: ") + e.what(); return false; }
    xblToken = j.get("Token").asString();
    uhs = j.get("DisplayClaims").get("xui").at(0).get("uhs").asString();
    if (xblToken.empty()) { err = "XBL 响应缺少 Token"; return false; }
    return true;
}

// XSTS 授权：用 XBL token 换 XSTS token + uhs（RelyingParty 必须是 Minecraft 服务）
bool xstsAuthorize(const std::string& xblToken, const std::string& existingUhs,
                   std::string& xstsToken, std::string& uhs, std::string& err) {
    json::Value req = json::makeObject();
    json::Value props = json::makeObject();
    props.obj["SandboxId"] = json::makeString("RETAIL");
    json::Value tokens = json::makeArray();
    tokens.arr.push_back(json::makeString(xblToken));
    props.obj["UserTokens"] = tokens;
    req.obj["Properties"] = props;
    req.obj["RelyingParty"] = json::makeString("rp://api.minecraftservices.com/");
    req.obj["TokenType"] = json::makeString("JWT");

    http::Response r = http::Post("https://xsts.auth.xboxlive.com/xsts/authorize",
                                  json::dump(req), "application/json", 30000);
    if (r.status < 0) { err = "XSTS 网络错误: " + r.error; return false; }

    json::Value j;
    try { j = json::parse(r.body); }
    catch (const std::exception& e) { err = std::string("XSTS JSON 解析失败: ") + e.what(); return false; }

    // XErr 是数字，不能当字符串取
    if (j.has("XErr")) {
        std::string code = j.get("XErr").isNumber()
            ? std::to_string((long long)j.get("XErr").asInt())
            : j.get("XErr").asString();
        err = xerrText(code);
        return false;
    }
    if (r.status >= 400) { err = "XSTS HTTP " + std::to_string(r.status); return false; }

    xstsToken = j.get("Token").asString();
    uhs = j.get("DisplayClaims").get("xui").at(0).get("uhs").asString();
    if (xstsToken.empty() || uhs.empty()) { err = "XSTS 响应缺少 Token/uhs"; return false; }
    if (!existingUhs.empty() && existingUhs != uhs) {
        err = "XSTS 返回的 user hash 与 XBL 不一致（账号状态异常）";
        return false;
    }
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
    if (r.status >= 400) {
        err = "Minecraft 登录失败 (HTTP " + std::to_string(r.status) + ")";
        if (r.status == 401 || r.status == 403)
            err += "：Xbox 身份未被 Minecraft 接受（账号可能没有 Java 版）";
        return false;
    }

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
    if (r.status == 404) {
        err = "该微软账号没有 Minecraft: Java 版（未购买，或档案还没创建）";
        return false;
    }
    if (r.status >= 400) { err = "获取玩家信息失败 (HTTP " + std::to_string(r.status) + ")"; return false; }

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

// 微软 live token → Minecraft token + profile
bool completeLogin(const std::string& liveToken, const std::string& liveRefresh,
                   int expiresIn, Account& out, std::string& err) {
    std::string xbl, xblUhs, xsts, xstsUhs, mcToken;
    if (!xblAuthenticate(liveToken, xbl, xblUhs, err)) return false;
    if (!xstsAuthorize(xbl, xblUhs, xsts, xstsUhs, err)) return false;
    if (!minecraftLogin(xstsUhs, xsts, mcToken, err)) return false;

    out.accessToken = mcToken;
    out.refreshToken = liveRefresh;
    out.expiresAt = (int64_t)time(nullptr) + (expiresIn > 0 ? expiresIn : 86400) - 120;
    if (!FetchProfile(mcToken, out, err)) return false;
    out.ok = true;
    return true;
}

} // namespace

int PollForToken(const DeviceCode& code, Account& out, std::string& err) {
    std::string body = "client_id=" + formEncode(effectiveClientId()) +
                       "&device_code=" + formEncode(code.deviceCode) +
                       "&grant_type=" + formEncode("urn:ietf:params:oauth:grant-type:device_code");
    http::Response r = http::Post(tokenUrl(), body, "application/x-www-form-urlencoded", 30000);
    if (r.status < 0) { err = "网络错误: " + r.error; return -1; }

    std::string accessToken, refreshToken, perr;
    int expiresIn = 0;
    if (!parseToken(r.body, accessToken, refreshToken, perr, &expiresIn)) {
        // 错误码可能带 error_description，这里只看前缀
        std::string low = perr;
        for (auto& c : low) c = (char)tolower((unsigned char)c);
        if (low.rfind("authorization_pending", 0) == 0) return 0;
        if (low.rfind("slow_down", 0) == 0) return 0;
        if (low.rfind("authorization_declined", 0) == 0) { err = "用户拒绝了授权"; return -1; }
        if (low.rfind("expired_token", 0) == 0 || low.rfind("code_expired", 0) == 0) {
            err = "设备码已过期，请重新获取";
            return -1;
        }
        if (low.rfind("bad_verification_code", 0) == 0) { err = "设备码无效，请重新获取"; return -1; }
        err = perr.empty() ? ("HTTP " + std::to_string(r.status)) : perr;
        return -1;
    }

    if (!completeLogin(accessToken, refreshToken, expiresIn, out, err)) return -1;
    return 1;
}

bool Refresh(Account& acc, std::string& err) {
    if (acc.refreshToken.empty()) {
        if (usingCustom())
            err = "该账号没有 refresh_token，请重新登录";
        else
            err = "内置登录方式不保存 refresh_token（微软限制该应用无法叠加 offline_access），"
                  "令牌过期后需要重新点一次「微软登录」；也可以在「全局设置」里填自己的 Azure 应用 ID 以支持自动续期";
        return false;
    }

    std::string body = "grant_type=refresh_token&client_id=" + formEncode(effectiveClientId()) +
                       "&refresh_token=" + formEncode(acc.refreshToken);
    if (usingCustom()) body += "&scope=" + formEncode(scopeValue());

    http::Response r = http::Post(tokenUrl(), body, "application/x-www-form-urlencoded", 30000);
    if (r.status < 0) { err = "网络错误: " + r.error; return false; }

    std::string accessToken, refreshToken, perr;
    int expiresIn = 0;
    if (!parseToken(r.body, accessToken, refreshToken, perr, &expiresIn)) {
        err = perr.empty() ? ("HTTP " + std::to_string(r.status)) : perr;
        return false;
    }
    if (!refreshToken.empty()) acc.refreshToken = refreshToken;

    Account fresh;
    fresh.name = acc.name;
    fresh.uuid = acc.uuid;
    if (!completeLogin(accessToken, acc.refreshToken, expiresIn, fresh, err)) return false;
    acc = fresh;
    return true;
}

bool FullLogin(std::function<void(const DeviceCode&)> onDeviceCode,
               Account& out, std::string& err) {
    DeviceCode dc;
    if (!StartDeviceCodeFlow(dc, err)) return false;
    if (onDeviceCode) onDeviceCode(dc);

    int waited = 0;
    int interval = dc.interval > 0 ? dc.interval : 5;
    int limit = dc.expiresIn > 0 ? dc.expiresIn : 900;
    if (limit > 900) limit = 900;          // 最多等 15 分钟（HMCL 也是这个上限）
    while (waited < limit) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        waited += interval;
        int r = PollForToken(dc, out, err);
        if (r == 1) return true;
        if (r == -1) return false;
    }
    err = "登录超时（15 分钟内没有完成授权）";
    return false;
}

} // namespace msauth
