// Skin.h — 皮肤加载与绘制（头像 + 可旋转的 3D 小人预览 + 披风）
//
// 支持 64×32（旧版）与 64×64（现代），以及它们的整数倍高清皮肤（128×128 等）。
// 纹理坐标按 64×64 标准布局取，unit = 图宽 / 64。
// 模型：classic（4px 手臂）/ slim（3px 手臂，Alex）
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>     // IStream（GDI+ 头文件需要）
#include <propidl.h>    // PROPID
#include <gdiplus.h>
#include <string>

namespace skin {

struct Model;   // 不透明句柄（内部缓存，按 png 路径 + 模型复用）

// 按路径加载（失败返回 nullptr）。同一路径重复调用会复用缓存。
// slim = true 时手臂按 3px 宽渲染（纤细模型）。
const Model* Get(const std::wstring& pngPath, bool slim = false);

// 披风纹理（64×32 PNG）。失败返回 nullptr。
const Model* GetCape(const std::wstring& pngPath);

// 清空缓存（切换账号存储位置 / 覆盖皮肤文件后调用）
void Clear();

// 校验：是不是可用的皮肤图（宽是 64 的倍数、高是 64/32 的倍数）
bool Probe(const std::wstring& pngPath);

// 校验披风图（64×32 或整数倍）
bool ProbeCape(const std::wstring& pngPath);

// 头像：只画头（含帽子层），裁成圆角
void DrawAvatar(Gdiplus::Graphics& g, const Model* m, const Gdiplus::RectF& rc, float radius);

// 全身预览：yaw/pitch 单位度，可鼠标拖动；zoom 1.0 = 适配
// cape 可为 nullptr（不画披风）
void DrawCharacter(Gdiplus::Graphics& g, const Model* m, const Model* cape,
                   const Gdiplus::RectF& rc, float yawDeg, float pitchDeg, float zoom);

} // namespace skin
