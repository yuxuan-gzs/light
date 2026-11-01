// Crypto.h — CNG 摘要与离线 UUID（MD5/SHA1）
#pragma once

#include <string>

namespace crypto {

// MD5（hex，小写）
std::string md5Hex(const std::string& data);

// SHA1（hex，小写）
std::string sha1Hex(const std::string& data);

// 文件 SHA1（流式，hex，小写）；失败返回空串
std::string sha1File(const std::string& path);

// Minecraft 离线 UUID：MD5("OfflinePlayer:" + name) 的 version-3 UUID（8-4-4-4-12，小写）
std::string offlineUuid(const std::string& name);

} // namespace crypto
