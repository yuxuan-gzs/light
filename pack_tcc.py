# pack_tcc.py - 把 TCC 目录打包成单个 bundle 文件（供 .rc 资源嵌入）
# bundle 格式：u32 文件数 N，然后 N 个 [u32 路径长度][路径 utf8][u32 大小][内容]
import os
import struct
import sys

SRC = r"D:\YX\VCBlocks\third_party\tcc"
OUT = r"D:\YX\VCBlocks\VCBlocks\tcc_bundle.bin"

def collect(root):
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace("\\", "/")
            # 只打包编译必需的：libtcc.dll、include 头文件、x64 运行时、导入库 def
            if rel == "libtcc.dll":
                files.append((rel, full))
            elif rel.startswith("include/"):
                files.append((rel, full))
            elif rel == "lib/libtcc1-64.a":
                files.append((rel, full))
            elif rel.startswith("lib/") and rel.endswith(".def"):
                files.append((rel, full))
    return files

def main():
    files = collect(SRC)
    files.sort(key=lambda x: x[0])
    with open(OUT, "wb") as f:
        f.write(struct.pack("<I", len(files)))
        for rel, full in files:
            data = open(full, "rb").read()
            relb = rel.encode("utf-8")
            f.write(struct.pack("<I", len(relb)))
            f.write(relb)
            f.write(struct.pack("<I", len(data)))
            f.write(data)
    total = os.path.getsize(OUT)
    print(f"打包完成: {OUT} ({total} 字节, {len(files)} 个文件)")
    for rel, _ in files:
        print("  " + rel)

if __name__ == "__main__":
    main()
