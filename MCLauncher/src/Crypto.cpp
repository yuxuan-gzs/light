// Crypto.cpp — 使用 Windows CNG (BCrypt) 计算 MD5/SHA1，及离线 UUID
#include "Crypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")

namespace crypto {

namespace {

std::string toHex(const unsigned char* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0xF]);
    }
    return out;
}

// 通用 CNG 摘要
bool hashBytes(const wchar_t* alg, const void* data, size_t len, std::vector<unsigned char>& out) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, alg, nullptr, 0) != 0) return false;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
    DWORD objLen = 0, got = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &got, 0);
    out.resize(objLen);
    BCryptFinishHash(hHash, out.data(), objLen, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return true;
}

} // namespace

std::string md5Hex(const std::string& data) {
    std::vector<unsigned char> out;
    if (!hashBytes(BCRYPT_MD5_ALGORITHM, data.data(), data.size(), out)) return "";
    return toHex(out.data(), out.size());
}

std::string sha1Hex(const std::string& data) {
    std::vector<unsigned char> out;
    if (!hashBytes(BCRYPT_SHA1_ALGORITHM, data.data(), data.size(), out)) return "";
    return toHex(out.data(), out.size());
}

std::string sha1File(const std::string& path) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return "";
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) { BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg, 0); return ""; }

    std::vector<char> buf(256 * 1024);
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = f.gcount();
        if (n > 0) BCryptHashData(hHash, (PUCHAR)buf.data(), (ULONG)n, 0);
    }

    DWORD objLen = 0, got = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &got, 0);
    std::vector<unsigned char> out(objLen);
    BCryptFinishHash(hHash, out.data(), objLen, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return toHex(out.data(), out.size());
}

std::string offlineUuid(const std::string& name) {
    std::string input = "OfflinePlayer:" + name;
    std::vector<unsigned char> md5;
    if (!hashBytes(BCRYPT_MD5_ALGORITHM, input.data(), input.size(), md5)) return "";
    if (md5.size() != 16) return "";

    // version-3 UUID：byte6 高位半字节置 3；byte8 高两位置 10
    md5[6] = (unsigned char)((md5[6] & 0x0F) | 0x30);
    md5[8] = (unsigned char)((md5[8] & 0x3F) | 0x80);

    std::string h = toHex(md5.data(), 16);
    // 8-4-4-4-12
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" +
           h.substr(16, 4) + "-" + h.substr(20, 12);
}

} // namespace crypto
