// Zip.cpp — 极简 ZIP 解压实现（读取中央目录 + 本地文件头，支持 store/deflate）
//
// 注意：ZIP 的「最后修改时间 / 日期」在本地文件头与中央目录里都是各 2 字节
// （共 4 字节），不是 4+4。早期版本按 4+4 读取导致后续字段整体错位，
// ForEach/ReadEntry/ExtractAll 全部失效（模组元数据、封面、世界导入都受影响）。
#include "Zip.h"

#include <zlib.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace zip {

namespace fs = std::filesystem;

namespace {

// 统一分隔符：部分打包工具（如 PowerShell Compress-Archive）写 '\'
inline void normalizeSlashes(std::string& s) {
    for (char& c : s) if (c == '\\') c = '/';
}

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
    v = (uint32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                   ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
    return true;
}

struct CEntry {
    std::string name;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint32_t localOff = 0;
    bool isDir = false;
};

struct LocalHeader {
    uint16_t method = 0;
    uint32_t crc = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint16_t nameLen = 0;
    uint16_t extraLen = 0;
};

// 定位中央目录（EOCD），必要时回退到 ZIP64 目录结束记录
bool findCentralDir(std::ifstream& f, uint32_t& count, uint32_t& offset) {
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz < 22) return false;
    const int scan = (int)std::min<std::streamoff>(sz, 65536 + 22);
    std::vector<char> tail((size_t)scan);
    f.seekg(sz - scan);
    f.read(tail.data(), scan);
    if ((int)f.gcount() != scan) return false;

    for (int i = scan - 22; i >= 0; --i) {
        if (memcmp(tail.data() + i, "\x50\x4b\x05\x06", 4) != 0) continue;
        count = (uint32_t)((unsigned char)tail[i + 10] | ((unsigned char)tail[i + 11] << 8));
        offset = (uint32_t)((unsigned char)tail[i + 16] | ((unsigned char)tail[i + 17] << 8) |
                            ((unsigned char)tail[i + 18] << 16) | ((unsigned char)tail[i + 19] << 24));
        if ((count != 0xFFFF && offset != 0xFFFFFFFF) || i < 20 ||
            memcmp(tail.data() + i - 20, "\x50\x4b\x06\x07", 4) != 0) {
            return true;
        }
        // ZIP64
        const unsigned char* loc = (const unsigned char*)tail.data() + i - 20;
        auto rd64 = [](const unsigned char* p) -> uint64_t {
            uint64_t v = 0;
            for (int k = 7; k >= 0; --k) v = (v << 8) | p[k];
            return v;
        };
        uint64_t z64off = rd64(loc + 8);
        if (z64off >= (uint64_t)sz) return true;
        std::streamoff cur = f.tellg();
        f.clear();
        f.seekg((std::streamoff)z64off);
        char sig[4] = { 0 };
        f.read(sig, 4);
        if (memcmp(sig, "\x50\x4b\x06\x06", 4) == 0) {
            unsigned char buf[56] = { 0 };
            f.read((char*)buf, 56);
            if (f) {
                count = (uint32_t)rd64(buf + 32);
                offset = (uint32_t)rd64(buf + 48);
            }
        }
        f.clear();
        f.seekg(cur);
        return true;
    }
    return false;
}

// 读取整个中央目录
bool readCentralDir(std::ifstream& f, std::vector<CEntry>& out) {
    uint32_t count = 0, offset = 0;
    if (!findCentralDir(f, count, offset)) return false;
    f.clear();
    f.seekg((std::streamoff)offset);
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        char sig[4];
        f.read(sig, 4);
        if (!f || memcmp(sig, "\x50\x4b\x01\x02", 4) != 0) return false;
        uint16_t verMade, verNeed, flags, method, mtime, mdate;
        uint16_t nameLen, extraLen, commentLen, disk, iattr;
        uint32_t crc, compSize, uncompSize, eattr, localOff;
        if (!readU16(f, verMade) || !readU16(f, verNeed) || !readU16(f, flags) ||
            !readU16(f, method) || !readU16(f, mtime) || !readU16(f, mdate) ||
            !readU32(f, crc) || !readU32(f, compSize) || !readU32(f, uncompSize) ||
            !readU16(f, nameLen) || !readU16(f, extraLen) || !readU16(f, commentLen) ||
            !readU16(f, disk) || !readU16(f, iattr) || !readU32(f, eattr) ||
            !readU32(f, localOff)) {
            return false;
        }
        (void)verMade; (void)verNeed; (void)flags; (void)mtime; (void)mdate;
        (void)disk; (void)iattr; (void)eattr;

        std::string name(nameLen, '\0');
        if (nameLen) f.read(&name[0], nameLen);
        if (!f) return false;
        normalizeSlashes(name);
        f.seekg((std::streamoff)extraLen + commentLen, std::ios::cur);

        CEntry e;
        e.name = name;
        e.method = method;
        e.crc = crc;
        e.compSize = compSize;
        e.uncompSize = uncompSize;
        e.localOff = localOff;
        e.isDir = (!name.empty() && name.back() == '/');
        out.push_back(std::move(e));
    }
    return true;
}

bool readLocalHeader(std::ifstream& f, uint32_t localOffset, LocalHeader& lh, std::string& name) {
    f.clear();
    f.seekg((std::streamoff)localOffset);
    char sig[4];
    f.read(sig, 4);
    if (!f || memcmp(sig, "\x50\x4b\x03\x04", 4) != 0) return false;
    uint16_t u;
    if (!readU16(f, u)) return false;          // version needed
    if (!readU16(f, u)) return false;          // flags
    if (!readU16(f, lh.method)) return false;
    if (!readU16(f, u)) return false;          // mtime（2 字节）
    if (!readU16(f, u)) return false;          // mdate（2 字节）
    if (!readU32(f, lh.crc)) return false;
    if (!readU32(f, lh.compSize)) return false;
    if (!readU32(f, lh.uncompSize)) return false;
    if (!readU16(f, lh.nameLen)) return false;
    if (!readU16(f, lh.extraLen)) return false;
    name.assign(lh.nameLen, '\0');
    if (lh.nameLen) f.read(&name[0], lh.nameLen);
    if (!f) return false;
    normalizeSlashes(name);
    return true;
}

// 用中央目录的值补全本地头（有些打包器把本地头尺寸写成 0，改用数据描述符）
void fixupFromCentral(const CEntry& ce, LocalHeader& lh) {
    lh.method = ce.method;
    if (lh.compSize == 0) lh.compSize = ce.compSize;
    if (lh.uncompSize == 0) lh.uncompSize = ce.uncompSize;
    if (lh.crc == 0) lh.crc = ce.crc;
}

bool inflateAll(std::ifstream& f, const LocalHeader& lh, std::string& out) {
    out.clear();
    if (lh.method == 0) {
        out.resize(lh.compSize);
        if (lh.compSize) f.read(&out[0], lh.compSize);
        return (bool)f;
    }
    if (lh.method != 8) return false;

    std::vector<char> comp(lh.compSize);
    if (lh.compSize) f.read(comp.data(), lh.compSize);
    if (!f) return false;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = (Bytef*)comp.data();
    zs.avail_in = (uInt)lh.compSize;
    if (lh.uncompSize) out.reserve(lh.uncompSize);

    std::vector<char> buf(64 * 1024);
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        zs.next_out = (Bytef*)buf.data();
        zs.avail_out = (uInt)buf.size();
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&zs); return false; }
        size_t produced = buf.size() - zs.avail_out;
        if (produced) out.append(buf.data(), produced);
    }
    inflateEnd(&zs);
    return true;
}

// 路径穿越防护：拒绝绝对路径与 .. 段
bool safeEntryName(const std::string& name) {
    if (name.empty()) return false;
    if (name[0] == '/' || (name.size() > 1 && name[1] == ':')) return false;
    size_t pos = 0;
    while (pos <= name.size()) {
        size_t next = name.find('/', pos);
        std::string seg = name.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (seg == "..") return false;
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------
// 公开接口
// ---------------------------------------------------------------
bool ForEach(const std::string& zipPath, const std::function<bool(const Entry&)>& cb) {
    std::ifstream f(zipPath, std::ios::binary);
    if (!f) return false;
    std::vector<CEntry> entries;
    if (!readCentralDir(f, entries)) return false;
    for (const auto& ce : entries) {
        Entry e;
        e.name = ce.name;
        e.size = ce.uncompSize;
        e.crc = ce.crc;
        e.isDir = ce.isDir;
        if (cb && !cb(e)) return true;
    }
    return true;
}

bool ExtractAll(const std::string& zipPath, const std::string& destDir,
                const std::string& excludePrefix) {
    return ExtractFiltered(zipPath, destDir,
        [&](const std::string& rel) {
            return excludePrefix.empty() || rel.rfind(excludePrefix, 0) != 0;
        }, false);
}

bool ExtractFiltered(const std::string& zipPath, const std::string& destDir,
                     const std::function<bool(const std::string& relPath)>& keep,
                     bool skipSameSize) {
    std::ifstream f(zipPath, std::ios::binary);
    if (!f) return false;
    std::vector<CEntry> entries;
    if (!readCentralDir(f, entries)) return false;

    fs::path destRoot = fs::u8path(destDir);
    for (const auto& ce : entries) {
        if (ce.isDir) continue;
        if (!safeEntryName(ce.name)) continue;
        if (keep && !keep(ce.name)) continue;

        fs::path dest = destRoot / fs::u8path(ce.name);
        if (skipSameSize) {
            std::error_code ec;
            auto sz = fs::file_size(dest, ec);
            if (!ec && (uint64_t)sz == ce.uncompSize) continue;
        }

        LocalHeader lh;
        std::string lname;
        if (!readLocalHeader(f, ce.localOff, lh, lname)) return false;
        fixupFromCentral(ce, lh);
        uint32_t dataOffset = ce.localOff + 30 + lh.nameLen + lh.extraLen;
        f.clear();
        f.seekg((std::streamoff)dataOffset);

        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        std::string data;
        if (!inflateAll(f, lh, data)) return false;
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (!data.empty()) out.write(data.data(), (std::streamsize)data.size());
        if (!out) return false;
    }
    return true;
}

bool ReadEntry(const std::string& zipPath, const std::string& entryName, std::string& out) {
    std::ifstream f(zipPath, std::ios::binary);
    if (!f) return false;
    std::vector<CEntry> entries;
    if (!readCentralDir(f, entries)) return false;

    std::string want = entryName;
    normalizeSlashes(want);
    while (!want.empty() && want.front() == '/') want.erase(want.begin());

    for (const auto& ce : entries) {
        if (ce.isDir) continue;
        if (ce.name != want) continue;
        LocalHeader lh;
        std::string lname;
        if (!readLocalHeader(f, ce.localOff, lh, lname)) return false;
        fixupFromCentral(ce, lh);
        uint32_t dataOffset = ce.localOff + 30 + lh.nameLen + lh.extraLen;
        f.clear();
        f.seekg((std::streamoff)dataOffset);
        return inflateAll(f, lh, out);
    }
    return false;
}

} // namespace zip
