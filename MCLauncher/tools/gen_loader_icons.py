import os

SRC = r"D:\Desktop\HMCL-main\HMCL\src\main\resources\assets\img"
OUT = r"D:\YX\MCLauncher\src\LoaderIcons.cpp"
HDR = r"D:\YX\MCLauncher\src\LoaderIcons.h"

files = [
    ("1", "fabric@2x.png", "kFabric"),
    ("2", "quilt@2x.png", "kQuilt"),
    ("3", "forge@2x.png", "kForge"),
    ("4", "neoforge@2x.png", "kNeoForge"),
]

lines = []
lines.append("// LoaderIcons.cpp — 内嵌加载器图标（直接取自 HMCL 的 PNG 资源，GPL-3.0 同源）")
lines.append("// 由脚本从 HMCL-main/HMCL/src/main/resources/assets/img/*@2x.png 生成，勿手工编辑。")
lines.append('#include "LoaderIcons.h"')
lines.append("")
lines.append("namespace icons {")
lines.append("")

for t, fn, name in files:
    p = os.path.join(SRC, fn)
    data = open(p, "rb").read()
    lines.append("// %s (%d bytes)" % (fn, len(data)))
    lines.append("static const unsigned char %s[] = {" % name)
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    lines.append("};")
    lines.append("")

lines.append("const unsigned char* loaderPng(int type, size_t& size) {")
lines.append("    switch (type) {")
for t, fn, name in files:
    lines.append("        case %s: size = sizeof(%s); return %s;" % (t, name, name))
lines.append("        default: break;")
lines.append("    }")
lines.append("    size = 0;")
lines.append("    return nullptr;")
lines.append("}")
lines.append("")
lines.append("} // namespace icons")
lines.append("")

open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(lines))

hdr = """// LoaderIcons.h — 内嵌加载器图标（PNG，来源于 HMCL 资源）
#pragma once

#include <cstddef>

namespace icons {

// type: 1 Fabric / 2 Quilt / 3 Forge / 4 NeoForge
// 返回 PNG 字节流与长度（找不到返回 nullptr，size=0）
const unsigned char* loaderPng(int type, size_t& size);

} // namespace icons
"""
open(HDR, "w", encoding="utf-8", newline="\n").write(hdr)

print("ok", os.path.getsize(OUT), os.path.getsize(HDR))
