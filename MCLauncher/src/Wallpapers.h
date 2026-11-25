// Wallpapers.h — 内置壁纸（编译进 DLL）
#pragma once

#include <cstddef>
#include <vector>

namespace wallpaper {

struct Entry {
    const char* name;
    const unsigned char* data;
    size_t size;
};

// 返回内置壁纸列表（3 张 HMCL 壁纸，jpg 字节）
const std::vector<Entry>& Wallpapers();

} // namespace wallpaper
