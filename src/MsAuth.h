// MsAuth.h — 微软正版登录（OAuth2 设备码流程 → XBL → XSTS → Minecraft）
#pragma once

#include <string>
#include <functional>

namespace msauth {

// 设备码授权响应（第一步）
struct DeviceCode {
    std::string userCode;        // 用户需要输入的代码
    std::string verificationUri; // 用户打开此链接
    std::string deviceCode;      // 轮询令牌用
    std::string message;
    int expiresIn = 900;
    int interval = 5;
};

// 完整登录结果
struct Account {
    std::string name;         // 玩家名
    std::string uuid;         // 带横线 UUID
    std::string accessToken;  // Minecraft access token
    bool ok = false;
    std::string error;
};

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
