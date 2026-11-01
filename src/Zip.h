// Zip.h — 极简 ZIP 读取/解压（仅支持 store(0) 与 deflate(8)，依赖 zlib）
#pragma once

#include <string>
#include <vector>
#include <functional>

namespace zip {

struct Entry {
    std::string name;      // 条目名（可能含目录前缀）
    uint64_t size = 0;     // 解压后大小
    uint32_t crc = 0;
    bool isDir = false;
};

// 遍历 zip 内条目；cb 返回 false 停止
bool ForEach(const std::string& zipPath,
             const std::function<bool(const Entry&)>& cb);

// 解压所有非目录条目到 destDir（保留目录结构）；
// excludePrefix：跳过以该前缀开头的条目（如 "META-INF/"）
bool ExtractAll(const std::string& zipPath, const std::string& destDir,
                const std::string& excludePrefix = "");

// 读取单个条目的内容到 out（返回 false 表示未找到或解压失败）
bool ReadEntry(const std::string& zipPath, const std::string& entryName, std::string& out);

} // namespace zip
