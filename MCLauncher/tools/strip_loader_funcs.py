import io, re, sys

p = r"D:\YX\MCLauncher\src\LoaderInstall.cpp"
raw = open(p, "rb").read()
bom = raw.startswith(b"\xef\xbb\xbf")
text = raw.decode("utf-8-sig")
text = text.replace("\r\n", "\n")

def find_func_start(text, signature):
    idx = text.find(signature)
    if idx < 0:
        return -1, -1
    # 向前到行首
    ls = text.rfind("\n", 0, idx) + 1
    return ls, idx

def remove_func(text, signature):
    ls, idx = find_func_start(text, signature)
    if ls < 0:
        print("NOT FOUND:", signature)
        return text
    # 从 signature 起找第一个 '{'
    b = text.find("{", idx)
    if b < 0:
        print("NO BRACE:", signature)
        return text
    depth = 0
    i = b
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    end = i + 1
    # 吃掉后面的空行
    while end < len(text) and text[end] == "\n":
        end += 1
    print("removed", signature, "len", end - ls)
    return text[:ls] + text[end:]

for sig in [
    "bool InstallForge(const std::string& fullVersion",
    "bool InstallNeoForge(const std::string& fullVersion",
    "static std::string RunJavaAndWait(",
    "static std::string errSummary(",
    "static void ensureLauncherProfiles(",
    "// 运行 java（installer）并等待结束，返回退出码字符串（失败返回空）。",
    "// 从 Java 输出里挑出最有用的几行（异常 / Caused by / 关键错误）",
    "// ============================================================\n// Forge（运行 installer jar）",
    "// ============================================================\n// NeoForge（1.20.2+ 的 Forge 替代品）",
]:
    if sig.startswith("//"):
        pass
    else:
        text = remove_func(text, sig)

# 清掉遗留的注释块标题（单独处理）
for marker in [
    "// 运行 java（installer）并等待结束，返回退出码字符串（失败返回空）。\n// outTail 非空时回传 Java 输出的最后几 KB —— installer 失败时真正的原因都在里面。\n",
    "// 从 Java 输出里挑出最有用的几行（异常 / Caused by / 关键错误）\n",
]:
    text = text.replace(marker, "")

text = text.replace("\n\n\n\n", "\n\n").replace("\n\n\n", "\n\n")
out = text.replace("\n", "\r\n").encode("utf-8")
if bom:
    out = b"\xef\xbb\xbf" + out
open(p, "wb").write(out)
print("done")
