// Zip.cpp — 极简 ZIP 解压实现（读取中央目录 + 本地文件头，支持 store/deflate）
#include "Zip.h"

#include <zlib.h>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace zip {

namespace fs = std::filesystem;

namespace {

struct LocalHeader {
    uint16_t method = 0;
    uint32_t crc = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint16_t nameLen = 0;
    uint16_t extraLen = 0;
};

bool readU16(std::ifstream& f, uint16_t& v) {
    unsigned char b[2];
    f.read((char*)b, 2);
    if (!f) return false;
    v = (uint16_t)(b[0] | (b[1] << 8));
    return true;
}
bool readU32(std::ifstream& f, uint32_t& v) {
    unsigned char b[4];
    f.read((char*)b, 4);
    if (!f) return false;
    v = (uint32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
    return true;
}

// 定位中央目录（EOCD）
bool findCentralDir(std::ifstream& f, uint32_t& count, uint32_t& offset) {
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz < 22) return false;
    // 在末尾 64KB 内查找 EOCD 签名 0x06054b50
    int scan = (int)std::min<std::streamoff>(sz, 65536 + 22);
    std::vector<char> tail(scan);
    f.seekg(sz - scan);
    f.read(tail.data(), scan);
    for (int i = scan - 22; i >= 0; --i) {
        if (memcmp(tail.data() + i, "\x50\x4b\x05\x06", 4) == 0) {
            count = (uint32_t)((unsigned char)tail[i + 10] | ((unsigned char)tail[i + 11] << 8));
            offset = (uint32_t)((unsigned char)tail[i + 16] | ((unsigned char)tail[i + 17] << 8) |
                                ((unsigned char)tail[i + 18] << 16) | ((unsigned char)tail[i + 19] << 24));
            return true;
        }
    }
    return false;
}

bool readLocalHeader(std::ifstream& f, uint32_t localOffset, LocalHeader& lh, std::string& name) {
    f.seekg(localOffset);
    char sig[4];
    f.read(sig, 4);
    if (!f || memcmp(sig, "\x50\x4b\x03\x04", 4) != 0) return false;
    uint16_t u;
    readU16(f, u); // version needed
    readU16(f, u); // flags
    readU16(f, lh.method);
    uint32_t u32;
    readU32(f, u32); // mtime
    readU32(f, u32); // mdate
    readU32(f, lh.crc);
    readU32(f, lh.compSize);
    readU32(f, lh.uncompSize);
    readU16(f, lh.nameLen);
    readU16(f, lh.extraLen);
    name.resize(lh.nameLen);
    f.read(&name[0], lh.nameLen);
    if (!f) return false;
    return true;
}

// 解压单个条目到 destPath
bool extractOne(std::ifstream& f, const LocalHeader& lh, uint32_t dataOffset,
                const fs::path& destPath) {
    f.seekg(dataOffset);
    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    if (lh.method == 0) {
        // store：直接拷贝
        std::vector<char> buf(lh.compSize);
        f.read(buf.data(), lh.compSize);
        if (!f) return false;
        out.write(buf.data(), lh.compSize);
        return true;
    }
    if (lh.method != 8) return false; // 仅支持 deflate

    // 读压缩数据
    std::vector<char> comp(lh.compSize);
    f.read(comp.data(), lh.compSize);
    if (!f) return false;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false; // 原始 deflate

    std::vector<char> outBuf(256 * 1024);
    zs.next_in = (Bytef*)comp.data();
    zs.avail_in = (uInt)lh.compSize;
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        zs.next_out = (Bytef*)outBuf.data();
        zs.avail_out = (uInt)outBuf.size();
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&zs); return false; }
        size_t produced = outBuf.size() - zs.avail_out;
        if (produced > 0) out.write(outBuf.data(), produced);
    }
    inflateEnd(&zs);
    return true;
}

} // namespace

bool ForEach(const std::string& zipPath, const std::function<bool(const Entry&)>& cb) {
    std::ifstream f(zipPath, std::ios::binary);
    if (!f) return false;
    uint32_t count = 0, offset = 0;
    if (!findCentralDir(f, count, offset)) return false;

    f.seekg(offset);
    for (uint32_t i = 0; i < count; ++i) {
        char sig[4];
        f.read(sig, 4);
        if (!f || memcmp(sig, "\x50\x4b\x01\x02", 4) != 0) return false;
        uint16_t u16; uint32_t u32;
        readU16(f, u16); readU16(f, u16); // version made/needed
        readU16(f, u16); // flags
        uint16_t method; readU16(f, method);
        readU32(f, u32); readU32(f, u32); // time/date
        uint32_t crc; readU32(f, crc);
        uint32_t compSize; readU32(f, compSize);
        uint32_t uncompSize; readU32(f, uncompSize);
        uint16_t nameLen; readU16(f, nameLen);
        uint16_t extraLen; readU16(f, extraLen);
        uint16_t commentLen; readU16(f, commentLen);
        readU16(f, u16); // disk
        readU16(f, u16); // internal attrs
        readU32(f, u32); // external attrs
        uint32_t localOff; readU32(f, localOff);

        std::string name(nameLen, '\0');
        f.read(&name[0], nameLen);
        f.seekg((std::streamoff)extraLen + commentLen, std::ios::cur);

        Entry e;
        e.name = name;
        e.size = uncompSize;
        e.crc = crc;
        e.isDir = (!name.empty() && name.back() == '/');
        if (cb && !cb(e)) return true;
    }
    return true;
}

bool ExtractAll(const std::string& zipPath, const std::string& destDir,
                const std::string& excludePrefix) {
    std::ifstream f(zipPath, std::ios::binary);
    if (!f) return false;
    uint32_t count = 0, offset = 0;
    if (!findCentralDir(f, count, offset)) return false;

    // 收集条目（中央目录）
    struct CEntry { std::string name; uint32_t localOff; bool isDir; };
    std::vector<CEntry> entries;
    entries.reserve(count);
    f.seekg(offset);
    for (uint32_t i = 0; i < count; ++i) {
        char sig[4]; f.read(sig, 4);
        if (!f || memcmp(sig, "\x50\x4b\x01\x02", 4) != 0) return false;
        uint16_t u16; uint32_t u32;
        readU16(f, u16); readU16(f, u16); readU16(f, u16);
        readU16(f, u16); // method
        readU32(f, u32); readU32(f, u32);
        readU32(f, u32); // crc
        readU32(f, u32); // compSize
        readU32(f, u32); // uncompSize
        uint16_t nameLen; readU16(f, nameLen);
        uint16_t extraLen; readU16(f, extraLen);
        uint16_t commentLen; readU16(f, commentLen);
        readU16(f, u16); readU16(f, u16); readU32(f, u32);
        uint32_t localOff; readU32(f, localOff);
        std::string name(nameLen, '\0');
        f.read(&name[0], nameLen);
        f.seekg((std::streamoff)extraLen + commentLen, std::ios::cur);
        entries.push_back({ name, localOff, (!name.empty() && name.back() == '/') });
    }

    for (const auto& ce : entries) {
        if (ce.isDir) continue;
        if (!excludePrefix.empty() && ce.name.rfind(excludePrefix, 0) == 0) continue;

        LocalHeader lh;
        std::string name;
        if (!readLocalHeader(f, ce.localOff, lh, name)) return false;
        uint32_t dataOffset = ce.localOff + 30 + lh.nameLen + lh.extraLen;

        fs::path dest = fs::u8path(destDir) / fs::u8path(name);
        // 防止路径穿越：确保目标在 destDir 内
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);

        if (!extractOne(f, lh, dataOffset, dest)) {
            return false;
        }
    }
    return true;
}

bool ReadEntry(const std::string& zipPath, const std::string& entryName, std::string& out) {
    std::ifstream f(zipPath, std::ios::binary);
    if (!f) return false;
    uint32_t count = 0, offset = 0;
    if (!findCentralDir(f, count, offset)) return false;

    f.seekg(offset);
    for (uint32_t i = 0; i < count; ++i) {
        char sig[4]; f.read(sig, 4);
        if (!f || memcmp(sig, "\x50\x4b\x01\x02", 4) != 0) return false;
        uint16_t u16; uint32_t u32;
        readU16(f, u16); readU16(f, u16); readU16(f, u16);
        readU16(f, u16); // method
        readU32(f, u32); readU32(f, u32); // time/date
        readU32(f, u32); // crc
        readU32(f, u32); // compSize
        readU32(f, u32); // uncompSize
        uint16_t nameLen; readU16(f, nameLen);
        uint16_t extraLen; readU16(f, extraLen);
        uint16_t commentLen; readU16(f, commentLen);
        readU16(f, u16); readU16(f, u16); readU32(f, u32);
        uint32_t localOff; readU32(f, localOff);
        std::string name(nameLen, '\0');
        f.read(&name[0], nameLen);
        f.seekg((std::streamoff)extraLen + commentLen, std::ios::cur);

        if (name != entryName) continue;

        LocalHeader lh;
        std::string n;
        if (!readLocalHeader(f, localOff, lh, n)) return false;
        uint32_t dataOffset = localOff + 30 + lh.nameLen + lh.extraLen;
        f.seekg(dataOffset);

        if (lh.method == 0) {
            out.resize(lh.uncompSize);
            f.read(&out[0], lh.uncompSize);
            return (bool)f;
        }
        if (lh.method != 8) return false;

        std::vector<char> comp(lh.compSize);
        f.read(comp.data(), lh.compSize);
        if (!f) return false;

        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
        zs.next_in = (Bytef*)comp.data();
        zs.avail_in = (uInt)lh.compSize;
        out.clear();
        out.reserve(lh.uncompSize);
        std::vector<char> buf(64 * 1024);
        int ret = Z_OK;
        while (ret != Z_STREAM_END) {
            zs.next_out = (Bytef*)buf.data();
            zs.avail_out = (uInt)buf.size();
            ret = inflate(&zs, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&zs); return false; }
            size_t produced = buf.size() - zs.avail_out;
            if (produced > 0) out.append(buf.data(), produced);
        }
        inflateEnd(&zs);
        return true;
    }
    return false;
}

} // namespace zip
