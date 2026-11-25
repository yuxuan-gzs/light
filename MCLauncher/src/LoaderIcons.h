// LoaderIcons.h — 内嵌加载器图标（PNG，来源于 HMCL 资源）
#pragma once

#include <cstddef>

namespace icons {

// type: 1 Fabric / 2 Quilt / 3 Forge / 4 NeoForge
// 返回 PNG 字节流与长度（找不到返回 nullptr，size=0）
const unsigned char* loaderPng(int type, size_t& size);

} // namespace icons
