// MainWindow.h
// 主窗口：现代化图形化编程界面（工具栏 / 积木调色板 / 画布 / 状态栏）
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include "core/BlockModel.h"
#include "core/BlockLibrary.h"
#include "core/Translator.h"
#include "core/TccCompiler.h"
#include "core/CodeGenerator.h"
#include "core/CompileConfig.h"

namespace vcb {

// 布局节点（绘制时重建，供命中测试/拖拽/编辑）
struct ReporterHit { Gdiplus::RectF rect; Block* block; };
struct Node {
    Block* block = nullptr;
    std::vector<Block>* list = nullptr;
    size_t index = 0;
    Gdiplus::RectF rect;                 // 头部矩形
    Gdiplus::RectF total;                // 整体（含子块）
    Gdiplus::RectF body;                 // 容器体矩形（无则空）
    std::vector<std::pair<Gdiplus::RectF, int>> paramRects; // 字面量参数槽 + 参数下标
    std::vector<ReporterHit> reporterRects;                  // reporter 参数
    std::vector<Node> children;
    std::vector<Node> elseChildren;
};

class MainWindow {
public:
    // 创建图形化编程面板（WS_CHILD，嵌入 light 插件宿主）
    bool CreatePanel(HINSTANCE inst, HWND parent);
    HWND Hwnd() const { return m_hwnd; }
    ~MainWindow();

private:
    HWND m_hwnd = nullptr;
    HINSTANCE m_inst = nullptr;
    HWND m_comboLang = nullptr;
    HWND m_comboTarget = nullptr;
    HWND m_editParam = nullptr;
    HWND m_tree = nullptr;         // 资源管理器 TreeView
    int m_leftTab = 1;             // 0=项目(资源管理器), 1=积木(调色板)

    Program m_program;
    std::string m_project = "MyProject";
    Translator m_translator;
    std::string m_locale = "zh-CN";
    bool m_tccReady = false;
    bool m_jdkReady = false;
    bool m_msvcReady = false;
    TargetLang m_target = TargetLang::C;
    CompileConfig m_compile;
    std::wstring m_status;

    // 面板几何
    int m_toolbarH = 52;
    int m_paletteW = 236;
    int m_statusH = 28;
    std::vector<RECT> m_buttons; // 工具栏按钮矩形
    int m_hoverButton = -1;

    // 输出面板（编译日志）
    bool m_outputVisible = true;
    int m_outputH = 110;
    std::wstring m_outputText;
    bool m_scrollToSelection = false;   // 错误跳转后滚动到选中积木

    // 调色板
    int m_palCat = 0;
    float m_palScroll = 0;
    std::vector<RECT> m_palBlockRects;

    // 画布
    float m_canvasScrollY = 0;
    float m_canvasContentH = 0;

    // 布局
    std::vector<Node> m_nodes;
    std::vector<Node*> m_flat;

    // 选中
    Block* m_selBlock = nullptr;

    // 拖拽
    bool m_dragging = false;
    bool m_dragValid = false;
    POINT m_dragStart{};
    POINT m_dragCur{};
    Block m_dragBlock;
    std::vector<Block>* m_dragSrcList = nullptr;
    size_t m_dragSrcIndex = 0;
    Block* m_dragTarget = nullptr;
    bool m_dragIntoBody = false;
    std::vector<Block*> m_dragExcluded;

    // 编辑参数
    bool m_editing = false;
    Block* m_editBlock = nullptr;
    int m_editParamIndex = -1;
    bool m_editIsReporter = false;
    Block* m_editReporterBlock = nullptr;

    Gdiplus::Font* m_fontUi = nullptr;
    Gdiplus::Font* m_fontBlock = nullptr;
    Gdiplus::Font* m_fontSmall = nullptr;
    Gdiplus::Font* m_fontTitle = nullptr;
    Gdiplus::Image* m_bgImage = nullptr;   // 背景图片缓存（背景样式=图片时）
    std::wstring m_bgImagePath;            // 已加载的图片路径（变化时重建缓存）

    // 消息处理
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    void OnPaint();
    void OnSize(int w, int h);
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnLButtonDblClk(int x, int y);
    void OnKeyDown(WPARAM key);

    // 绘制
    void DrawToolbar(Gdiplus::Graphics& g, int w);
    void DrawPalette(Gdiplus::Graphics& g, int h);
    void DrawCanvas(Gdiplus::Graphics& g, int x, int y, int w, int h);
    void DrawStatus(Gdiplus::Graphics& g, int w, int h);
    void DrawOutput(Gdiplus::Graphics& g, int w, int h);
    void DrawNode(Gdiplus::Graphics& g, Node& n);
    void DrawHeaderContent(Gdiplus::Graphics& g, Block& b, const Gdiplus::RectF& rect,
                           uint32_t col, Node& out, bool isReporter);
    float DrawReporterInline(Gdiplus::Graphics& g, Block& rep, float x, float y, Node& out);
    void DrawBlockGhost(Gdiplus::Graphics& g, const Block& b, float x, float y);

    // 布局
    void BuildLayout(Gdiplus::Graphics& g);
    void Flatten(Node& n);
    float MeasureHeader(Gdiplus::Graphics& g, const Block& b);
    void LayoutDraw(Gdiplus::Graphics& g, Block& b, std::vector<Block>* list, size_t index,
                    float x, float y, Node& out);

    // 命中
    Node* HitBlock(int x, int y);
    bool HitParam(int x, int y, Block*& block, int& paramIndex, bool& isReporter, Block*& reporterBlock);
    int HitToolbar(int x, int y);
    int HitPalette(int x, int y, int h);

    // 树操作
    void ClearSelection();
    void DeleteSelected();
    void MoveBlock(Block b, Node& target, bool intoBody);
    bool IsExcluded(Block* b);
    void CollectExcluded(Block* b);

    // 动作
    void RunAction(int id);
    void ActionCompile();
    void ActionCompileC();
    void ActionCompileCpp();
    void ActionCompileJava();
    void ActionCompileAndRun();
    void ActionCompileSettings();
    void ActionDecompile();
    void ActionBuildDll();
    void ActionScanLibrary();
    void ActionStoreLibrary();
    void ActionSave();
    void ActionNewProject();
    void ActionGenerateCoreTranslation();

    // 辅助
    void ReloadTranslation();
    void RebuildCoreTranslation();
    void SetStatus(const std::wstring& s);
    void SaveProgram();
    void LoadProgram();
    void RefreshProjectTree();      // 刷新资源管理器树
    void RefreshHeaderBlocks();     // 扫描已装库，刷新「头文件」积木分类
    void SetOutput(const std::wstring& text);          // 写编译日志到输出面板
    void SelectBlockByError(const std::string& log,    // 从编译日志解析错误行号并跳转到积木
                            const std::vector<int>& blockLines);
    std::wstring PickOpenFile(const wchar_t* filter);
    uint32_t CategoryColor(const std::string& cat);
    std::wstring Tr(const std::string& english);
};

} // namespace vcb
