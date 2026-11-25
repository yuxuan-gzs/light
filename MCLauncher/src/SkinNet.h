// SkinNet.h — 正版皮肤的上传与拉取
//
// 上传：POST https://api.minecraftservices.com/minecraft/profile/skins
//       multipart/form-data，variant=classic|slim，file=<png>
//       Authorization: Bearer <Minecraft access token>
// 拉取：GET https://sessionserver.mojang.com/session/minecraft/profile/<uuid32>
//       → properties[0].value（base64）→ textures.SKIN.url / textures.CAPE.url
//       → 下载 PNG 到本地
#pragma once

#include <string>

namespace skin {

// 把 pngPath 上传为该正版账号的皮肤。slim=true 用纤细（Alex）模型。
bool UploadToMojang(const std::string& accessToken, const std::wstring& pngPath,
                    bool slim, std::string& err);

// 从 Mojang 拉取该正版账号当前的皮肤，存到 destPng。
// slimOut 返回皮肤模型（是否纤细）；found=false 表示这个账号没设置皮肤。
// capeDest 非空时顺便拉披风（没披风不算失败，hasCapeOut 返回 false）。
bool FetchFromMojang(const std::string& uuid, const std::wstring& destPng,
                     const std::wstring& capeDest, bool* slimOut, bool* hasCapeOut,
                     std::string& err);

// 同上，拉披风（没披风时 found=false）
bool FetchCapeFromMojang(const std::string& uuid, const std::wstring& destPng,
                         bool* found, std::string& err);

// UUID 带横线 → 32 位无横线（sessionserver 要紧凑格式）
std::string CompactUuid(const std::string& uuid);

} // namespace skin
