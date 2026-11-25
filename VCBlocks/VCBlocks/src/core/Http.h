// Http.h
// 轻量 HTTP 下载（WinHTTP），用于库商城下载库文件
#pragma once
#include <string>
#include <vector>

namespace vcb {

// GET 请求，返回文本（UTF-8）；失败返回 false 并填充 err
bool HttpGet(const std::string& url, std::string& outBody, std::string& err);
// GET 请求，返回原始字节
bool HttpGetBytes(const std::string& url, std::vector<unsigned char>& outBytes, std::string& err);

} // namespace vcb
