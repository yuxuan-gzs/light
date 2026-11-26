// Skin.cpp — 皮肤纹理切分与 3D 小人绘制（GDI+）
#include "Skin.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace skin {

// 一个部件的 6 面纹理（必须在 skin 作用域，否则和 Skin.h 的前向声明不是同一个类型）
struct Tex6 {
    Gdiplus::Bitmap* f[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    void Free() {
        for (auto& p : f) { delete p; p = nullptr; }
    }
};

struct Model {
    Gdiplus::Bitmap* src = nullptr;
    int unit = 1;
    bool legacy = false;      // 64×32
    bool slim = false;        // 3px 手臂（Alex）
    bool isCape = false;      // 披风图（只有 capeOuter / capeInner）
    Tex6 head, hat, body, armR, armL, legR, legL;
    Gdiplus::Bitmap* capeOuter = nullptr;
    Gdiplus::Bitmap* capeInner = nullptr;

    ~Model() {
        head.Free(); hat.Free(); body.Free(); armR.Free();
        armL.Free(); legR.Free(); legL.Free();
        delete capeOuter;
        delete capeInner;
        delete src;
    }
};

namespace {

// 面的顺序：0 前(+z) 1 后(-z) 2 左(-x) 3 右(+x) 4 上(+y) 5 下(-y)
enum { F_FRONT = 0, F_BACK, F_LEFT, F_RIGHT, F_TOP, F_BOTTOM };

struct Rect { int x, y, w, h; };

// 64×64 标准布局
const Rect kHead[6]  = { {8,8,8,8}, {24,8,8,8}, {16,8,8,8}, {0,8,8,8}, {8,0,8,8}, {16,0,8,8} };
const Rect kHat[6]   = { {40,8,8,8}, {56,8,8,8}, {48,8,8,8}, {32,8,8,8}, {40,0,8,8}, {48,0,8,8} };
const Rect kBody[6]  = { {20,20,8,12}, {32,20,8,12}, {28,20,4,12}, {16,20,4,12}, {20,16,8,4}, {28,16,8,4} };
const Rect kArmR[6]  = { {44,20,4,12}, {52,20,4,12}, {48,20,4,12}, {40,20,4,12}, {44,16,4,4}, {48,16,4,4} };
const Rect kArmL[6]  = { {36,52,4,12}, {44,52,4,12}, {40,52,4,12}, {32,52,4,12}, {36,48,4,4}, {40,48,4,4} };
const Rect kLegR[6]  = { {4,20,4,12}, {12,20,4,12}, {8,20,4,12}, {0,20,4,12}, {4,16,4,4}, {8,16,4,4} };
const Rect kLegL[6]  = { {20,52,4,12}, {28,52,4,12}, {24,52,4,12}, {16,52,4,12}, {20,48,4,4}, {24,48,4,4} };

// 披风（64×32）：外侧 (1,1) 10×16，内侧 (12,1) 10×16
const Rect kCapeOut = { 1, 1, 10, 16 };
const Rect kCapeIn  = { 12, 1, 10, 16 };

Gdiplus::Bitmap* Crop(Gdiplus::Bitmap* src, const Rect& r, int unit) {
    int x = r.x * unit, y = r.y * unit, w = r.w * unit, h = r.h * unit;
    if (!src || x + w > (int)src->GetWidth() || y + h > (int)src->GetHeight()) return nullptr;
    Gdiplus::Rect cr(x, y, w, h);
    Gdiplus::Bitmap* out = src->Clone(cr, PixelFormat32bppARGB);
    if (out && out->GetLastStatus() != Gdiplus::Ok) { delete out; return nullptr; }
    return out;
}

void CropSet(Gdiplus::Bitmap* src, const Rect (&table)[6], int unit, Tex6& out) {
    for (int i = 0; i < 6; ++i) out.f[i] = Crop(src, table[i], unit);
}

std::map<std::wstring, Model*>& Cache() {
    static std::map<std::wstring, Model*> m;
    return m;
}

// 缓存键：路径 + 变体标记（同路径不同模型 / 皮肤与披风要分开）
std::wstring CacheKey(const std::wstring& p, const wchar_t* tag) {
    std::wstring k = p;
    k += L'\x01';
    k += tag;
    return k;
}

} // namespace

const Model* Get(const std::wstring& pngPath, bool slim) {
    if (pngPath.empty()) return nullptr;
    const std::wstring key = CacheKey(pngPath, slim ? L"slim" : L"wide");
    auto& cache = Cache();
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    auto* m = new Model();
    m->src = new Gdiplus::Bitmap(pngPath.c_str());
    if (!m->src || m->src->GetLastStatus() != Gdiplus::Ok ||
        m->src->GetWidth() < 64 || m->src->GetHeight() < 32 ||
        (m->src->GetWidth() % 64) != 0) {
        delete m;
        cache[key] = nullptr;
        return nullptr;
    }

    const int W = (int)m->src->GetWidth();
    const int H = (int)m->src->GetHeight();
    m->unit = W / 64;
    m->legacy = (H / m->unit) < 64;
    m->slim = slim;

    CropSet(m->src, kHead, m->unit, m->head);
    CropSet(m->src, kBody, m->unit, m->body);
    CropSet(m->src, kArmR, m->unit, m->armR);
    CropSet(m->src, kLegR, m->unit, m->legR);
    if (!m->legacy) {
        CropSet(m->src, kHat, m->unit, m->hat);
        CropSet(m->src, kArmL, m->unit, m->armL);
        CropSet(m->src, kLegL, m->unit, m->legL);
    }

    if (!m->head.f[F_FRONT]) { delete m; cache[key] = nullptr; return nullptr; }
    cache[key] = m;
    return m;
}

const Model* GetCape(const std::wstring& pngPath) {
    if (pngPath.empty()) return nullptr;
    const std::wstring key = CacheKey(pngPath, L"cape");
    auto& cache = Cache();
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    auto* m = new Model();
    m->isCape = true;
    m->src = new Gdiplus::Bitmap(pngPath.c_str());
    if (!m->src || m->src->GetLastStatus() != Gdiplus::Ok ||
        m->src->GetWidth() < 64 || (m->src->GetWidth() % 64) != 0 ||
        m->src->GetHeight() < 32) {
        delete m;
        cache[key] = nullptr;
        return nullptr;
    }
    m->unit = (int)m->src->GetWidth() / 64;
    m->capeOuter = Crop(m->src, kCapeOut, m->unit);
    m->capeInner = Crop(m->src, kCapeIn, m->unit);
    if (!m->capeOuter) { delete m; cache[key] = nullptr; return nullptr; }
    cache[key] = m;
    return m;
}

void Clear() {
    for (auto& kv : Cache()) delete kv.second;
    Cache().clear();
}

bool Probe(const std::wstring& pngPath) {
    if (pngPath.empty()) return false;
    Gdiplus::Bitmap bmp(pngPath.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    int w = (int)bmp.GetWidth(), h = (int)bmp.GetHeight();
    if (w < 64 || w % 64 != 0) return false;
    int unit = w / 64;
    return h == 32 * unit || h == 64 * unit;
}

bool ProbeCape(const std::wstring& pngPath) {
    if (pngPath.empty()) return false;
    Gdiplus::Bitmap bmp(pngPath.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    int w = (int)bmp.GetWidth(), h = (int)bmp.GetHeight();
    if (w < 64 || w % 64 != 0) return false;
    int unit = w / 64;
    return h == 32 * unit;          // 披风固定 64×32
}

// ---------------------------------------------------------------
// 2D 头像（头 + 帽子层）
// ---------------------------------------------------------------
void DrawAvatar(Gdiplus::Graphics& g, const Model* m, const Gdiplus::RectF& rc, float radius) {
    if (!m || !m->head.f[F_FRONT]) return;
    Gdiplus::GraphicsState st = g.Save();
    // 皮肤是 8×8 的像素画，放大必须用最近邻，否则放大到 40px 会糊成一团（HMCL 也是硬边）
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath* clip = theme::RoundRectPath(rc.X, rc.Y, rc.Width, rc.Height, radius);
    g.SetClip(clip);

    // 底色（皮肤有透明像素时不至于漏底）
    Gdiplus::SolidBrush bg(Gdiplus::Color(255, 214, 216, 226));
    g.FillPath(&bg, clip);
    g.DrawImage(m->head.f[F_FRONT], rc);
    if (m->hat.f[F_FRONT]) g.DrawImage(m->hat.f[F_FRONT], rc);
    delete clip;
    g.Restore(st);
}

// ---------------------------------------------------------------
// 3D 小人
// ---------------------------------------------------------------
namespace {

struct V3 { float x, y, z; };

struct FaceDraw {
    Gdiplus::Bitmap* tex;
    V3 p[4];        // tl, tr, br, bl（世界坐标）
    V3 n;           // 法线
    float depth;
    bool noCull = false;   // 披风这类薄片：两面都要画
};

void Rotate(V3& p, float yaw, float pitch) {
    // yaw 绕 Y：x' = x*cos + z*sin, z' = -x*sin + z*cos
    float cy = std::cos(yaw), sy = std::sin(yaw);
    float x = p.x * cy + p.z * sy;
    float z = -p.x * sy + p.z * cy;
    p.x = x; p.z = z;
    // pitch 绕 X：y' = y*cos - z*sin, z' = y*sin + z*cos
    float cp = std::cos(pitch), sp = std::sin(pitch);
    float y = p.y * cp - p.z * sp;
    float z2 = p.y * sp + p.z * cp;
    p.y = y; p.z = z2;
}

// 生成一个盒子的 6 个面（尺寸 sx,sy,sz，中心 cx,cy,cz，单位 = 皮肤像素）
void AddBox(std::vector<FaceDraw>& out, const Tex6& t, float sx, float sy, float sz,
            float cx, float cy, float cz) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    auto push = [&](int idx, V3 a, V3 b, V3 c, V3 d, V3 n) {
        if (!t.f[idx]) return;
        FaceDraw fd{ t.f[idx], { a, b, c, d }, n, 0.0f };
        for (auto& q : fd.p) { q.x += cx; q.y += cy; q.z += cz; }
        out.push_back(fd);
    };
    // 前 (+z)
    push(F_FRONT, { -hx, hy, hz }, { hx, hy, hz }, { hx, -hy, hz }, { -hx, -hy, hz }, { 0, 0, 1 });
    // 后 (-z)
    push(F_BACK, { hx, hy, -hz }, { -hx, hy, -hz }, { -hx, -hy, -hz }, { hx, -hy, -hz }, { 0, 0, -1 });
    // 左 (-x)
    push(F_LEFT, { -hx, hy, -hz }, { -hx, hy, hz }, { -hx, -hy, hz }, { -hx, -hy, -hz }, { -1, 0, 0 });
    // 右 (+x)
    push(F_RIGHT, { hx, hy, hz }, { hx, hy, -hz }, { hx, -hy, -hz }, { hx, -hy, hz }, { 1, 0, 0 });
    // 上 (+y)
    push(F_TOP, { -hx, hy, -hz }, { hx, hy, -hz }, { hx, hy, hz }, { -hx, hy, hz }, { 0, 1, 0 });
    // 下 (-y)
    push(F_BOTTOM, { -hx, -hy, hz }, { hx, -hy, hz }, { hx, -hy, -hz }, { -hx, -hy, -hz }, { 0, -1, 0 });
}

} // namespace

void DrawCharacter(Gdiplus::Graphics& g, const Model* m, const Model* cape,
                   const Gdiplus::RectF& rc, float yawDeg, float pitchDeg, float zoom) {
    if (!m) return;
    // 皮肤纹理是像素画：贴图放大用最近邻，否则 3D 小人身上会一片糊
    const Gdiplus::GraphicsState gs0 = g.Save();
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    const float yaw = yawDeg * 3.14159265f / 180.0f;
    const float pitch = pitchDeg * 3.14159265f / 180.0f;

    std::vector<FaceDraw> faces;
    // 单位：1 = 皮肤像素。人物高 32、宽 16（含手臂）
    AddBox(faces, m->head, 8, 8, 8, 0, 28, 0);
    if (!m->legacy || m->hat.f[F_FRONT]) AddBox(faces, m->hat, 8.4f, 8.4f, 8.4f, 0, 28, 0);
    AddBox(faces, m->body, 8, 12, 4, 0, 18, 0);
    // 纤细模型：手臂 3×3，向外收 0.5，视觉上明显更细（与游戏内 Alex 一致）
    const float aw = m->slim ? 3.0f : 4.0f;
    const float ax = 4.0f + aw * 0.5f;
    AddBox(faces, m->armR, aw, 12, aw, -ax, 18, 0);
    AddBox(faces, m->legacy ? m->armR : m->armL, aw, 12, aw, ax, 18, 0);
    AddBox(faces, m->legR, 4, 12, 4, -2, 6, 0);
    AddBox(faces, m->legacy ? m->legR : m->legL, 4, 12, 4, 2, 6, 0);

    // 披风：贴背后（z 负方向），底部略后飘；薄片两面都要画
    if (cape && cape->capeOuter) {
        const float w = 10.0f, hgt = 16.0f;
        const float topY = 24.0f;
        const float zTop = -2.3f, zBot = -3.6f;
        auto quad = [&](Gdiplus::Bitmap* tex, V3 tl, V3 tr, V3 br, V3 bl, V3 n) {
            if (!tex) return;
            FaceDraw fd{ tex, { tl, tr, br, bl }, n, 0.0f, true };
            faces.push_back(fd);
        };
        // 外侧（法线朝 -z）：从背后看，纹理左侧 = 世界 +x
        quad(cape->capeOuter,
             { w * 0.5f, topY, zTop }, { -w * 0.5f, topY, zTop },
             { -w * 0.5f, topY - hgt, zBot }, { w * 0.5f, topY - hgt, zBot }, { 0, 0, -1 });
        // 内侧（朝 +z）
        if (cape->capeInner)
            quad(cape->capeInner,
                 { -w * 0.5f, topY, zTop }, { w * 0.5f, topY, zTop },
                 { w * 0.5f, topY - hgt, zBot }, { -w * 0.5f, topY - hgt, zBot }, { 0, 0, 1 });
    }

    // 旋转 + 深度排序 + 剔除背面
    std::vector<FaceDraw> visible;
    for (auto& f : faces) {
        for (auto& p : f.p) Rotate(p, yaw, pitch);
        Rotate(f.n, yaw, pitch);
        if (!f.noCull && f.n.z <= 0.01f) continue;      // 背面剔除（薄片除外）
        f.depth = (f.p[0].z + f.p[1].z + f.p[2].z + f.p[3].z) * 0.25f;
        visible.push_back(f);
    }
    std::sort(visible.begin(), visible.end(),
              [](const FaceDraw& a, const FaceDraw& b) { return a.depth < b.depth; });

    // 投影：人物高 32 → 适配 rc；zoom 可缩放
    float scale = (rc.Height / 34.0f) * zoom;
    float cx = rc.X + rc.Width * 0.5f;
    float cy = rc.Y + rc.Height * 0.5f + 16.0f * scale;   // 人物中心在 y=16（0..32）
    auto proj = [&](const V3& p) { return Gdiplus::PointF(cx + p.x * scale, cy - p.y * scale); };

    for (auto& f : visible) {
        Gdiplus::PointF p0 = proj(f.p[0]), p1 = proj(f.p[1]);
        Gdiplus::PointF p3 = proj(f.p[3]);
        // 退化（侧看几乎成线）就跳过，避免除零
        float area = (p1.X - p0.X) * (p3.Y - p0.Y) - (p3.X - p0.X) * (p1.Y - p0.Y);
        if (std::fabs(area) < 0.5f) continue;

        float tw = (float)f.tex->GetWidth();
        float th = (float)f.tex->GetHeight();
        Gdiplus::Matrix mat((p1.X - p0.X) / tw, (p1.Y - p0.Y) / tw,
                            (p3.X - p0.X) / th, (p3.Y - p0.Y) / th, p0.X, p0.Y);

        Gdiplus::TextureBrush brush(f.tex, Gdiplus::WrapModeClamp);
        brush.SetTransform(&mat);

        Gdiplus::GraphicsPath path;
        Gdiplus::PointF quad[4] = { p0, p1, proj(f.p[2]), p3 };
        path.AddPolygon(quad, 4);
        g.FillPath(&brush, &path);
    }
    g.Restore(gs0);
}

} // namespace skin
