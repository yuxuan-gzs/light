// MsAuth.h — 微软正版登录（OAuth 设备码 → XBL → XSTS → Minecraft）
#pragma once

#include <string>
#include <functional>

namespace msauth {

// 设备码授权响应（第一步）
struct DeviceCode {
    std::string userCode;        // 用户需要输入的代码
    std::string verificationUri; // 用户打开此链接（https://www.microsoft.com/link）
    std::string deviceCode;      // 轮询令牌用
    std::string message;
    int expiresIn = 900;
    int interval = 5;
};

// 完整登录结果
struct Account {
    std::string name;          // 玩家名
    std::string uuid;          // 带横线 UUID
    std::string accessToken;   // Minecraft access token
    std::string refreshToken;  // 微软 OAuth refresh_token（仅自定义 Azure 应用 ID 时才有）
    int64_t expiresAt = 0;     // Minecraft token 过期时间（Unix 秒，0 = 未知）
    bool ok = false;
    std::string error;
};

// 自定义 Azure 应用（公共客户端，需开启「允许公共客户端流」并勾选设备码流）。
// 留空 = 用内置的 Minecraft 启动器旧版应用 ID：
//   * 能登录（设备码走 login.live.com），但**拿不到 refresh_token**，令牌约 24 小时有效；
//   * 填了自己的 GUID 后走 Microsoft Entra（consumers v2.0），可以保存 refresh_token 自动续期。
void SetClientId(const std::string& clientId);
std::string ClientId();
bool UsingCustomClient();     // true = 用户自备应用 ID（支持自动续期）

// 用 refresh_token 续期（自动重走 XBL → XSTS → Minecraft），保留 name/uuid
bool Refresh(Account& acc, std::string& err);

// 第一步：获取设备码。返回 false 表示网络/解析失败（err 说明）
bool StartDeviceCodeFlow(DeviceCode& out, std::string& err);

// 轮询令牌（在设备码有效期内反复调用）。
// 返回 0=仍在等待（应稍后重试）；1=成功（out 填充）；-1=失败/过期（err 说明）
int PollForToken(const DeviceCode& code, Account& out, std::string& err);

// 一步到位：请求设备码 + 阻塞轮询直到成功/失败/超时。
// onDeviceCode 用于把 userCode/verificationUri 通知给 UI。
// 返回 true 成功。
bool FullLogin(std::function<void(const DeviceCode&)> onDeviceCode,
               Account& out, std::string& err);

} // namespace msauth
