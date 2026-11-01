// util.h
// 基础工具：UTF-8/UTF-16 转换、文件读写、路径处理
#pragma once
#include <string>
#include <vector>
#include <windows.h>

namespace vcb {

// UTF-8 <-> UTF-16
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);
// 本地 ANSI 代码页（GBK 等）-> UTF-16（用于 cl.exe 输出等）
std::wstring AnsiToWide(const std::string& s);

// 窄字符串（UTF-8）-> 宽（ANSI 回退保留）
std::wstring ToWide(const std::string& s);

// 文件
bool FileExists(const std::wstring& path);
bool ReadFileText(const std::wstring& path, std::string& out);   // 返回 UTF-8 文本
bool WriteFileText(const std::wstring& path, const std::string& text); // 写 UTF-8（无 BOM）
bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out);
bool WriteFileBytes(const std::wstring& path, const std::vector<unsigned char>& data);

// 路径
std::wstring ExeDir();                                   // 当前 exe 所在目录（含结尾 '\'）
std::wstring ExePath();                                  // 当前 exe 完整路径
void EnsureDir(const std::wstring& dir);                 // 递归创建目录
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring BaseName(const std::wstring& path);         // 不含扩展名的文件名
std::wstring DirOf(const std::wstring& path);            // 目录（含结尾 '\'）
std::wstring ReplaceExt(const std::wstring& path, const std::wstring& ext);
std::string  ToLowerAscii(std::string s);
std::string  ToUpperAscii(std::string s);

// 字符串
std::string Trim(const std::string& s);
bool StartsWith(const std::string& s, const std::string& prefix);
bool EndsWith(const std::string& s, const std::string& suffix);
std::vector<std::string> SplitLines(const std::string& s);
std::string Join(const std::vector<std::string>& v, const std::string& sep);
std::string ReplaceAll(std::string s, const std::string& from, const std::string& to);

// C 字符串转义/反转义（用于生成代码与 JSON）
std::string EscapeCString(const std::string& s);

} // namespace vcb
