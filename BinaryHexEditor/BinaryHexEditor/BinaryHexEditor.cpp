// BinaryHexEditor.cpp - 修复版本
// 完整的二进制编辑器 - UTF-8全字符支持、新建文件、直接编辑、滚动条修复
#define UNICODE
#define _UNICODE
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#ifndef TB_SETBKCOLOR
#ifdef CCM_SETBKCOLOR
#define TB_SETBKCOLOR CCM_SETBKCOLOR
#else
#define TB_SETBKCOLOR (WM_USER + 0x0400)
#endif
#endif
#include <commdlg.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <stack>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "Resource.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

// ============ 主题颜色 ============
struct Theme {
    static const COLORREF Background = RGB(30, 30, 30);      // 背景色
    static const COLORREF Surface = RGB(45, 45, 45);         // 表面色
    static const COLORREF Text = RGB(220, 220, 220);         // 普通文本
    static const COLORREF TextBright = RGB(255, 255, 255);   // 亮色文本
    static const COLORREF Address = RGB(86, 156, 214);       // 地址颜色
    static const COLORREF HexText = RGB(206, 145, 120);      // 十六进制文本
    static const COLORREF BinText0 = RGB(80, 80, 80);        // 二进制0
    static const COLORREF BinText1 = RGB(100, 220, 200);     // 二进制1
    static const COLORREF AsciiText = RGB(180, 200, 180);    // ASCII文本
    static const COLORREF Cursor = RGB(38, 79, 120);         // 光标颜色
    static const COLORREF CursorHex = RGB(100, 149, 237);    // 十六进制光标
    static const COLORREF CursorBin = RGB(100, 220, 200);    // 二进制光标
    static const COLORREF Selection = RGB(38, 79, 120);      // 选择高亮
    static const COLORREF Header = RGB(128, 128, 128);       // 表头颜色
    static const COLORREF Separator = RGB(64, 64, 64);       // 分隔线
    static const COLORREF StatusBarBg = RGB(37, 37, 38);     // 状态栏背景
    static const COLORREF ToolBarBg = RGB(45, 45, 48);       // 工具栏背景
    static const COLORREF FoundBg = RGB(255, 165, 0);        // 查找结果背景
    static const COLORREF FoundText = RGB(0, 0, 0);          // 查找结果文本
    // 滑块颜色 - 高对比度
    static const COLORREF SliderBg = RGB(30, 30, 30);        // 滑块背景
    static const COLORREF SliderThumb = RGB(200, 200, 200);  // 滑块手柄
    static const COLORREF SliderChannel = RGB(80, 80, 80);   // 滑块轨道
};

// ============ UTF-8 编解码 ============
/**
 * 解码UTF-8字符
 * @param data 数据缓冲区
 * @param offset 起始偏移
 * @param maxLen 最大长度
 * @param consumed 输出：消耗的字节数
 * @return 解码后的宽字符串
 */
std::wstring DecodeUTF8Char(const std::vector<unsigned char>& data, int offset, int maxLen, int& consumed) {
    consumed = 0;
    if (offset >= (int)data.size()) return L"";
    unsigned char c = data[offset];
    std::wstring result;
    // 单字节ASCII字符
    if (c < 0x80) { consumed = 1; result += (WCHAR)c; return result; }

    int bytes = 0; unsigned int cp = 0;
    // 判断UTF-8字节数
    if ((c & 0xE0) == 0xC0) { bytes = 2; cp = c & 0x1F; }          // 双字节
    else if ((c & 0xF0) == 0xE0) { bytes = 3; cp = c & 0x0F; }     // 三字节
    else if ((c & 0xF8) == 0xF0) { bytes = 4; cp = c & 0x07; }     // 四字节
    else { consumed = 1; result += (WCHAR)c; return result; }

    // 检查是否超出数据范围
    if (offset + bytes > (int)data.size() || bytes > maxLen) {
        consumed = 1; result += (WCHAR)c; return result;
    }

    // 处理后续字节
    for (int i = 1; i < bytes; i++) {
        unsigned char b = data[offset + i];
        if ((b & 0xC0) != 0x80) {
            consumed = 1; result += (WCHAR)c; return result;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    consumed = bytes;

    // 转换为Unicode码点
    if (cp <= 0xFFFF) result += (WCHAR)cp;
    else if (cp <= 0x10FFFF) {
        cp -= 0x10000;
        result += (WCHAR)(0xD800 | (cp >> 10));
        result += (WCHAR)(0xDC00 | (cp & 0x3FF));
    }
    else result += (WCHAR)c;
    return result;
}

/**
 * 将宽字符串编码为UTF-8
 * @param wstr 宽字符串
 * @return UTF-8编码的字节数组
 */
std::vector<unsigned char> EncodeUTF8(const std::wstring& wstr) {
    std::vector<unsigned char> result;
    for (size_t i = 0; i < wstr.length(); i++) {
        unsigned int cp = wstr[i];
        // 处理代理对
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wstr.length()) {
            unsigned int lo = wstr[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        // 根据码点范围编码
        if (cp < 0x80) result.push_back((unsigned char)cp);
        else if (cp < 0x800) {
            result.push_back(0xC0 | (cp >> 6));
            result.push_back(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000) {
            result.push_back(0xE0 | (cp >> 12));
            result.push_back(0x80 | ((cp >> 6) & 0x3F));
            result.push_back(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x110000) {
            result.push_back(0xF0 | (cp >> 18));
            result.push_back(0x80 | ((cp >> 12) & 0x3F));
            result.push_back(0x80 | ((cp >> 6) & 0x3F));
            result.push_back(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

// ============ 缩放管理器 ============
/**
 * 缩放管理器 - 管理字体大小缩放和全屏模式
 */
class ZoomManager {
    double m_zoomLevel;           // 当前缩放级别
    double m_minZoom;             // 最小缩放
    double m_maxZoom;             // 最大缩放
    double m_zoomStep;            // 缩放步长
    int m_baseFontSize;           // 基础字体大小
    int m_currentFontSize;        // 当前字体大小
    bool m_isFullScreen;          // 是否全屏
    WINDOWPLACEMENT m_np;         // 窗口位置信息
    LONG m_ns;                    // 窗口样式
public:
    ZoomManager() : m_zoomLevel(1.0), m_minZoom(0.25), m_maxZoom(4.0),
        m_zoomStep(0.1), m_baseFontSize(13), m_currentFontSize(13),
        m_isFullScreen(false), m_ns(0) {
        ZeroMemory(&m_np, sizeof(m_np));
    }

    // 放大
    void ZoomIn() {
        m_zoomLevel = min(m_maxZoom, m_zoomLevel + m_zoomStep);
        m_currentFontSize = max(6, (int)(m_baseFontSize * m_zoomLevel));
    }

    // 缩小
    void ZoomOut() {
        m_zoomLevel = max(m_minZoom, m_zoomLevel - m_zoomStep);
        m_currentFontSize = max(6, (int)(m_baseFontSize * m_zoomLevel));
    }

    // 重置缩放
    void ZoomReset() { m_zoomLevel = 1.0; m_currentFontSize = m_baseFontSize; }

    // 设置缩放级别
    void SetZoom(double l) {
        m_zoomLevel = max(m_minZoom, min(m_maxZoom, l));
        m_currentFontSize = max(6, (int)(m_baseFontSize * m_zoomLevel));
    }

    // 获取缩放级别
    double GetZoomLevel() const { return m_zoomLevel; }
    int GetZoomPercentage() const { return (int)(m_zoomLevel * 100); }
    int GetFontSize() const { return m_currentFontSize; }
    bool IsFullScreen() const { return m_isFullScreen; }

    // 切换全屏模式
    void ToggleFullScreen(HWND hWnd) {
        m_isFullScreen = !m_isFullScreen;
        if (m_isFullScreen) {
            // 进入全屏：保存窗口状态，设置为弹出式窗口，铺满整个显示器
            m_np.length = sizeof(m_np);
            GetWindowPlacement(hWnd, &m_np);
            m_ns = GetWindowLongPtr(hWnd, GWL_STYLE);
            SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            HMONITOR hm = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hm, &mi);
            SetWindowPos(hWnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_FRAMECHANGED);
        }
        else {
            // 退出全屏：恢复窗口状态
            SetWindowLongPtr(hWnd, GWL_STYLE, m_ns);
            SetWindowPlacement(hWnd, &m_np);
            SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        }
    }
};

// ============ 数据管理器 ============
/**
 * 数据管理器 - 管理编辑数据、撤销/重做、导入导出
 */
class DataManager {
    std::vector<unsigned char> m_data;  // 数据缓冲区
public:
    // 编辑操作结构
    struct EditAction {
        int offset;                       // 操作偏移
        std::vector<unsigned char> oldData;  // 旧数据
        std::vector<unsigned char> newData;  // 新数据
        bool isInsert;                    // 是否为插入操作
    };
private:
    std::stack<EditAction> m_undoStack;  // 撤销栈
    std::stack<EditAction> m_redoStack;  // 重做栈
    bool m_modified;                     // 是否已修改

public:
    DataManager() : m_modified(false) {}

    // 设置数据
    void SetData(const std::vector<unsigned char>& d) {
        m_data = d;
        m_modified = false;
        ClearUndoRedo();
    }

    // 获取数据
    std::vector<unsigned char>& GetData() { return m_data; }
    const std::vector<unsigned char>& GetData() const { return m_data; }
    size_t GetSize() const { return m_data.size(); }
    bool IsModified() const { return m_modified; }
    void SetModified() { m_modified = true; }
    void ClearModified() { m_modified = false; }

    // 获取指定字节
    unsigned char GetByte(int o) const {
        return (o >= 0 && o < (int)m_data.size()) ? m_data[o] : 0;
    }

    // 设置指定字节
    void SetByte(int o, unsigned char v) {
        if (o >= 0 && o < (int)m_data.size()) {
            m_data[o] = v;
            m_modified = true;
        }
    }

    // 插入字节
    void InsertByte(int o, unsigned char v) {
        if (o >= 0 && o <= (int)m_data.size()) {
            m_data.insert(m_data.begin() + o, v);
            m_modified = true;
        }
    }

    // 删除字节
    void DeleteByte(int o) {
        if (o >= 0 && o < (int)m_data.size()) {
            m_data.erase(m_data.begin() + o);
            m_modified = true;
        }
    }

    // 插入多个字节
    void InsertBytes(int o, const std::vector<unsigned char>& b) {
        if (o >= 0 && o <= (int)m_data.size() && !b.empty()) {
            m_data.insert(m_data.begin() + o, b.begin(), b.end());
            m_modified = true;
        }
    }

    // 删除多个字节
    void DeleteBytes(int o, int c) {
        if (o >= 0 && c > 0 && o + c <= (int)m_data.size()) {
            m_data.erase(m_data.begin() + o, m_data.begin() + o + c);
            m_modified = true;
        }
    }

    // 撤销操作
    void PushUndo(const EditAction& a) {
        m_undoStack.push(a);
        while (!m_redoStack.empty()) m_redoStack.pop();
    }
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }
    EditAction PopUndo() { EditAction a = m_undoStack.top(); m_undoStack.pop(); return a; }
    void PushRedo(const EditAction& a) { m_redoStack.push(a); }
    EditAction PopRedo() { EditAction a = m_redoStack.top(); m_redoStack.pop(); return a; }
    void ClearUndoRedo() {
        while (!m_undoStack.empty()) m_undoStack.pop();
        while (!m_redoStack.empty()) m_redoStack.pop();
    }

    // 导出为C数组
    std::string ExportAsC() const {
        std::stringstream ss;
        ss << "unsigned char data[] = {\n  ";
        for (size_t i = 0; i < m_data.size(); i++) {
            if (i > 0) ss << ", ";
            if (i % 12 == 0 && i > 0) ss << "\n  ";
            ss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)m_data[i];
        }
        ss << "\n};\n";
        return ss.str();
    }

    // 导出为Python字节数组
    std::string ExportAsPy() const {
        std::stringstream ss;
        ss << "data = bytes([\n  ";
        for (size_t i = 0; i < m_data.size(); i++) {
            if (i > 0) ss << ", ";
            if (i % 12 == 0 && i > 0) ss << "\n  ";
            ss << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)m_data[i];
        }
        ss << "\n])\n";
        return ss.str();
    }

    // 导出为JSON
    std::string ExportAsJSON() const {
        std::stringstream ss;
        ss << "{\n  \"size\": " << m_data.size() << ",\n  \"data\": [\n  ";
        for (size_t i = 0; i < m_data.size(); i++) {
            if (i > 0) ss << ", ";
            if (i % 12 == 0 && i > 0) ss << "\n  ";
            ss << (int)m_data[i];
        }
        ss << "\n  ]\n}\n";
        return ss.str();
    }
};

// ============ 十六进制视图 ============
/**
 * 十六进制视图 - 显示和编辑十六进制数据，支持UTF-8显示
 */
class HexView {
    HWND m_hWnd, m_hParent;           // 窗口句柄
    ZoomManager* m_pZoom;             // 缩放管理器指针
    DataManager* m_pData;             // 数据管理器指针
    int* m_pCursorPos, * m_pNibblePos; // 光标位置和半字节位置
    int m_bpl;                        // 每行字节数
    int m_topLine;                    // 顶部行号
    int m_linesVisible;               // 可见行数
    int m_selStart, m_selEnd;         // 选择范围
    int m_lh, m_aw, m_hw, m_as, m_cw; // 布局尺寸
    HFONT m_hFont, m_hBoldFont;       // 字体句柄
    HBRUSH m_hBgBrush;                // 背景画刷
    HPEN m_hSepPen;                   // 分隔线画笔
    UINT_PTR m_cursorTimer;           // 光标闪烁定时器
    bool m_showCursor, m_visible;     // 光标显示状态和可见性

public:
    HexView() : m_hWnd(NULL), m_hParent(NULL), m_pZoom(NULL), m_pData(NULL),
        m_pCursorPos(NULL), m_pNibblePos(NULL),
        m_bpl(16), m_topLine(0), m_linesVisible(20), m_selStart(-1), m_selEnd(-1),
        m_lh(20), m_aw(100), m_hw(300), m_as(420), m_cw(10),
        m_hFont(NULL), m_hBoldFont(NULL), m_hBgBrush(NULL), m_hSepPen(NULL),
        m_cursorTimer(0), m_showCursor(true), m_visible(true) {
    }

    ~HexView() {
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hBoldFont) DeleteObject(m_hBoldFont);
        if (m_hBgBrush) DeleteObject(m_hBgBrush);
        if (m_hSepPen) DeleteObject(m_hSepPen);
        if (m_cursorTimer) KillTimer(m_hWnd, m_cursorTimer);
    }

    /**
     * 创建字体 - 根据当前缩放级别创建字体
     */
    void CreateFonts() {
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hBoldFont) DeleteObject(m_hBoldFont);

        int fs = m_pZoom ? m_pZoom->GetFontSize() : 13;
        LOGFONT lf = { 0 };
        lf.lfHeight = -fs;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Consolas");
        m_hFont = CreateFontIndirect(&lf);

        lf.lfWeight = FW_BOLD;
        m_hBoldFont = CreateFontIndirect(&lf);
    }

    /**
     * 创建十六进制视图窗口
     */
    HWND Create(HWND hp, HINSTANCE hi, ZoomManager* z, DataManager* d, int* cp, int* np) {
        m_hParent = hp;
        m_pZoom = z;
        m_pData = d;
        m_pCursorPos = cp;
        m_pNibblePos = np;
        CreateFonts();
        m_hBgBrush = CreateSolidBrush(Theme::Background);
        m_hSepPen = CreatePen(PS_SOLID, 1, Theme::Separator);

        // 注册窗口类
        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = HexViewProc;
        wc.cbWndExtra = sizeof(LONG_PTR);
        wc.hInstance = hi;
        // 使用自定义箭头指针 - 整个视图使用箭头
        wc.hCursor = LoadCursor(hi, MAKEINTRESOURCE(IDC_CUSTOM_CURSOR));
        if (!wc.hCursor) wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = m_hBgBrush;
        wc.lpszClassName = L"HexView8";

        static bool reg = false;
        if (!reg) { RegisterClassEx(&wc); reg = true; }

        m_hWnd = CreateWindowEx(0, L"HexView8", NULL,
            WS_CHILD | WS_VSCROLL | WS_TABSTOP,
            0, 0, 0, 0, hp, NULL, hi, this);
        m_cursorTimer = SetTimer(m_hWnd, 1, 500, NULL);
        return m_hWnd;
    }

    // 设置可见性
    void SetVisible(bool v) {
        m_visible = v;
        ShowWindow(m_hWnd, v ? SW_SHOW : SW_HIDE);
        if (v) {
            InvalidateRect(m_hWnd, NULL, TRUE);
            SetFocus(m_hWnd);
        }
    }

    // 更新缩放
    void UpdateZoom() { CreateFonts(); InvalidateRect(m_hWnd, NULL, TRUE); }
    void ClearSelection() { m_selStart = -1; m_selEnd = -1; }

    // 设置光标位置
    void SetCursorPos(int p) {
        if (m_pCursorPos) *m_pCursorPos = p;
        if (m_pNibblePos) *m_pNibblePos = 0;
        ScrollToOffset(p);
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 移动光标
    void MoveCursor(int delta) {
        if (!m_pCursorPos || !m_pData) return;
        int np = *m_pCursorPos + delta;
        if (np < 0) np = 0;
        if (np >= (int)m_pData->GetSize()) np = max(0, (int)m_pData->GetSize());
        *m_pCursorPos = np;
        if (m_pNibblePos) *m_pNibblePos = 0;
        ScrollToOffset(np);
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 全选
    void SelectAll() {
        if (!m_pData || m_pData->GetSize() == 0) return;
        m_selStart = 0;
        m_selEnd = (int)m_pData->GetSize() - 1;
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 复制到剪贴板（十六进制格式）
    void CopyToClipboard(HWND hWnd) {
        if (m_selStart < 0 || !m_pData || m_pData->GetSize() == 0) return;
        int s = min(m_selStart, m_selEnd), e = max(m_selStart, m_selEnd);
        std::string hs;
        for (int i = s; i <= e; i++) {
            char b[4];
            sprintf_s(b, 4, "%02X ", m_pData->GetByte(i));
            hs += b;
        }
        if (!hs.empty() && OpenClipboard(hWnd)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, hs.length() + 1);
            if (hg) {
                char* p = (char*)GlobalLock(hg);
                strcpy_s(p, hs.length() + 1, hs.c_str());
                GlobalUnlock(hg);
                SetClipboardData(CF_TEXT, hg);
            }
            CloseClipboard();
        }
    }

    // 复制为文本（UTF-8解码）
    void CopyAsTextToClipboard(HWND hWnd) {
        if (m_selStart < 0 || !m_pData || m_pData->GetSize() == 0) return;
        int s = min(m_selStart, m_selEnd), e = max(m_selStart, m_selEnd);
        std::wstring ws;
        for (int i = s; i <= e; ) {
            int c = 0;
            ws += DecodeUTF8Char(m_pData->GetData(), i, e - i + 1, c);
            if (c == 0) break;
            i += c;
        }
        if (!ws.empty() && OpenClipboard(hWnd)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (ws.length() + 1) * sizeof(WCHAR));
            if (hg) {
                WCHAR* p = (WCHAR*)GlobalLock(hg);
                wcscpy_s(p, ws.length() + 1, ws.c_str());
                GlobalUnlock(hg);
                SetClipboardData(CF_UNICODETEXT, hg);
            }
            CloseClipboard();
        }
    }

    // 从剪贴板粘贴
    void PasteFromClipboard(HWND hWnd) {
        if (!m_pCursorPos || !m_pData) return;
        if (!OpenClipboard(hWnd)) return;

        // 尝试粘贴Unicode文本
        HANDLE hd = GetClipboardData(CF_UNICODETEXT);
        if (hd) {
            WCHAR* pd = (WCHAR*)GlobalLock(hd);
            std::wstring ws(pd);
            GlobalUnlock(hd);
            CloseClipboard();
            std::vector<unsigned char> bytes = EncodeUTF8(ws);
            if (!bytes.empty()) {
                DataManager::EditAction a;
                a.offset = *m_pCursorPos;
                a.isInsert = true;
                a.oldData.clear();
                a.newData = bytes;
                m_pData->PushUndo(a);
                m_pData->InsertBytes(*m_pCursorPos, bytes);
                UpdateScrollBar();
                InvalidateRect(m_hWnd, NULL, TRUE);
            }
            return;
        }

        // 尝试粘贴十六进制文本
        hd = GetClipboardData(CF_TEXT);
        if (!hd) { CloseClipboard(); return; }
        char* pd = (char*)GlobalLock(hd);
        std::string tx(pd);
        GlobalUnlock(hd);
        CloseClipboard();

        std::vector<unsigned char> bytes;
        std::string hb;
        for (size_t i = 0; i < tx.length(); i++) {
            if (tx[i] == ' ' || tx[i] == '\n' || tx[i] == '\r') {
                if (!hb.empty()) {
                    bytes.push_back((unsigned char)strtol(hb.c_str(), NULL, 16));
                    hb.clear();
                }
                continue;
            }
            hb += tx[i];
            if (hb.length() == 2) {
                bytes.push_back((unsigned char)strtol(hb.c_str(), NULL, 16));
                hb.clear();
            }
        }
        if (!hb.empty()) bytes.push_back((unsigned char)strtol(hb.c_str(), NULL, 16));
        if (bytes.empty()) return;

        DataManager::EditAction a;
        a.offset = *m_pCursorPos;
        a.isInsert = true;
        a.oldData.clear();
        a.newData = bytes;
        m_pData->PushUndo(a);
        m_pData->InsertBytes(*m_pCursorPos, bytes);
        UpdateScrollBar();
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 删除选中的内容
    void DeleteSelection() {
        if (m_selStart < 0 || !m_pCursorPos || !m_pData) return;
        int s = min(m_selStart, m_selEnd), e = max(m_selStart, m_selEnd), c = e - s + 1;
        DataManager::EditAction a;
        a.offset = s;
        a.isInsert = true;
        a.oldData.clear();
        for (int i = 0; i < c; i++) a.newData.push_back(m_pData->GetByte(s + i));
        m_pData->PushUndo(a);
        m_pData->DeleteBytes(s, c);
        *m_pCursorPos = s;
        if (*m_pCursorPos >= (int)m_pData->GetSize() && m_pData->GetSize() > 0)
            *m_pCursorPos = (int)m_pData->GetSize() - 1;
        ClearSelection();
        UpdateScrollBar();
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 删除当前字节
    void DeleteByte() {
        if (!m_pCursorPos || !m_pData || m_pData->GetSize() == 0) return;
        if (*m_pCursorPos >= (int)m_pData->GetSize()) return;
        DataManager::EditAction a;
        a.offset = *m_pCursorPos;
        a.isInsert = true;
        a.oldData.clear();
        a.newData.push_back(m_pData->GetByte(*m_pCursorPos));
        m_pData->PushUndo(a);
        m_pData->DeleteByte(*m_pCursorPos);
        if (*m_pCursorPos >= (int)m_pData->GetSize() && m_pData->GetSize() > 0)
            *m_pCursorPos = (int)m_pData->GetSize() - 1;
        ClearSelection();
        UpdateScrollBar();
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 处理十六进制输入
    void HandleHexInput(WCHAR ch) {
        if (!m_pCursorPos || !m_pNibblePos || !m_pData) return;
        if (!((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'F') || (ch >= L'a' && ch <= L'f'))) return;

        int val = 0;
        if (ch >= L'0' && ch <= L'9') val = ch - L'0';
        else if (ch >= L'A' && ch <= L'F') val = ch - L'A' + 10;
        else val = ch - L'a' + 10;

        // 如果光标在末尾，插入新字节
        if (*m_pCursorPos >= (int)m_pData->GetSize()) {
            DataManager::EditAction a;
            a.offset = *m_pCursorPos;
            a.isInsert = true;
            a.oldData.clear();
            a.newData.push_back(0);
            m_pData->PushUndo(a);
            m_pData->InsertByte(*m_pCursorPos, 0);
            UpdateScrollBar();
        }

        unsigned char byte = m_pData->GetByte(*m_pCursorPos), oldByte = byte;
        byte = (*m_pNibblePos == 0) ? ((byte & 0x0F) | (val << 4)) : ((byte & 0xF0) | val);

        if (byte != oldByte) {
            DataManager::EditAction a;
            a.offset = *m_pCursorPos;
            a.isInsert = false;
            a.oldData.push_back(oldByte);
            a.newData.push_back(byte);
            m_pData->PushUndo(a);
            m_pData->SetByte(*m_pCursorPos, byte);
        }

        if (*m_pNibblePos == 0) *m_pNibblePos = 1;
        else { *m_pNibblePos = 0; MoveCursor(1); }
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 处理字符输入（UTF-8编码）
    void HandleCharInput(WCHAR ch) {
        if (!m_pCursorPos || !m_pData) return;
        std::wstring ws(1, ch);
        std::vector<unsigned char> bytes = EncodeUTF8(ws);
        DataManager::EditAction a;
        a.offset = *m_pCursorPos;
        a.isInsert = true;
        a.oldData.clear();
        a.newData = bytes;
        m_pData->PushUndo(a);
        m_pData->InsertBytes(*m_pCursorPos, bytes);
        UpdateScrollBar();
        MoveCursor((int)bytes.size());
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 鼠标滚轮事件
    void OnMouseWheel(int delta) { ScrollBy(-delta / WHEEL_DELTA * 3); }

    // 鼠标左键按下
    void OnLButtonDown(int x, int y) {
        if (!m_pData || !m_pCursorPos || !m_pNibblePos) return;
        int line = (y - m_lh) / m_lh + m_topLine;
        if (line < 0) line = 0;
        if (line >= GetTotalLines()) line = max(0, GetTotalLines() - 1);

        int col = -1;
        if (x >= m_aw && x < m_as - 5) {
            col = (x - m_aw - 15) / (m_cw * 3);
            *m_pNibblePos = 0;
        }
        else if (x >= m_as) {
            col = (x - m_as - 5) / m_cw;
        }

        if (col >= 0 && col < m_bpl) {
            int np = line * m_bpl + col;
            if (np < 0) np = 0;
            if (np > (int)m_pData->GetSize()) np = (int)m_pData->GetSize();
            *m_pCursorPos = np;
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                if (m_selStart == -1) m_selStart = *m_pCursorPos;
                m_selEnd = *m_pCursorPos;
            }
            else ClearSelection();
            m_showCursor = true;
            InvalidateRect(m_hWnd, NULL, TRUE);
            SetFocus(m_hWnd);
        }
    }

private:
    // 窗口过程
    static LRESULT CALLBACK HexViewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        HexView* p = NULL;
        if (m == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)l;
            p = (HexView*)cs->lpCreateParams;
            SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)p);
            p->m_hWnd = h;
        }
        else p = (HexView*)GetWindowLongPtr(h, GWLP_USERDATA);
        if (p) return p->WndProc(m, w, l);
        return DefWindowProc(h, m, w, l);
    }

    // 消息处理
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_PAINT: OnPaint(); return 0;                      // 绘制
        case WM_SIZE: OnSize(LOWORD(lParam), HIWORD(lParam)); return 0;  // 大小改变
        case WM_VSCROLL: OnVScroll(LOWORD(wParam), HIWORD(wParam)); return 0;  // 垂直滚动
        case WM_MOUSEWHEEL:                                      // 鼠标滚轮
            if (m_visible) OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_LBUTTONDOWN:                                     // 鼠标左键
            if (m_visible) {
                OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                SetFocus(m_hWnd);
            }
            return 0;
        case WM_KEYDOWN: {                                       // 键盘按下
            if (!m_visible) break;
            bool sh = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool ct = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            switch (wParam) {
            case VK_LEFT:                                        // 左箭头
                if (*m_pNibblePos == 1) *m_pNibblePos = 0;
                else MoveCursor(-1);
                if (!sh) ClearSelection();
                break;
            case VK_RIGHT:                                       // 右箭头
                if (*m_pNibblePos == 0) *m_pNibblePos = 1;
                else MoveCursor(1);
                if (!sh) ClearSelection();
                break;
            case VK_UP: MoveCursor(-m_bpl); if (!sh) ClearSelection(); break;  // 上
            case VK_DOWN: MoveCursor(m_bpl); if (!sh) ClearSelection(); break; // 下
            case VK_PRIOR: MoveCursor(-m_bpl * m_linesVisible); if (!sh) ClearSelection(); break; // PageUp
            case VK_NEXT: MoveCursor(m_bpl * m_linesVisible); if (!sh) ClearSelection(); break; // PageDown
            case VK_HOME:                                        // Home键
                *m_pCursorPos = ct ? 0 : (*m_pCursorPos / m_bpl) * m_bpl;
                *m_pNibblePos = 0;
                if (!sh) ClearSelection();
                ScrollToOffset(*m_pCursorPos);
                break;
            case VK_END: {                                        // End键
                int ds = max(0, (int)m_pData->GetSize());
                *m_pCursorPos = ct ? ds : min((*m_pCursorPos / m_bpl + 1) * m_bpl - 1, ds);
                *m_pNibblePos = 0;
                if (!sh) ClearSelection();
                ScrollToOffset(*m_pCursorPos);
                break;
            }
            case VK_DELETE:                                       // Delete键
                if (m_selStart >= 0) DeleteSelection();
                else DeleteByte();
                break;
            case VK_BACK:                                         // Backspace键
                if (*m_pCursorPos > 0) {
                    (*m_pCursorPos)--;
                    *m_pNibblePos = 0;
                    DeleteByte();
                }
                break;
            case VK_INSERT: {                                     // Insert键
                DataManager::EditAction a;
                a.offset = *m_pCursorPos;
                a.isInsert = true;
                a.oldData.clear();
                a.newData.push_back(0);
                m_pData->PushUndo(a);
                m_pData->InsertByte(*m_pCursorPos, 0);
                UpdateScrollBar();
                InvalidateRect(m_hWnd, NULL, TRUE);
                break;
            }
            case VK_RETURN: HandleCharInput(L'\n'); break;       // 回车键
            case VK_TAB:                                          // Tab键 - 切换到二进制视图
                SendMessage(m_hParent, WM_COMMAND, IDM_TAB_BIN, 0);
                return 0;
            case VK_F3:                                           // F3键 - 查找
                SendMessage(m_hParent, WM_COMMAND,
                    sh ? IDM_SEARCH_FINDPREV : IDM_SEARCH_FINDNEXT, 0);
                return 0;
            }
            if (sh) {
                if (m_selStart == -1) m_selStart = *m_pCursorPos;
                m_selEnd = *m_pCursorPos;
            }
            m_showCursor = true;
            InvalidateRect(m_hWnd, NULL, TRUE);
            return 0;
        }
        case WM_CHAR: {                                           // 字符输入
            if (!m_visible) break;
            WCHAR ch = (WCHAR)wParam;
            if ((ch >= L'0' && ch <= L'9') ||
                (ch >= L'A' && ch <= L'F') ||
                (ch >= L'a' && ch <= L'f'))
                HandleHexInput(ch);
            else
                HandleCharInput(ch);
            InvalidateRect(m_hWnd, NULL, TRUE);
            return 0;
        }
        case WM_TIMER:                                            // 定时器 - 光标闪烁
            if (wParam == 1) {
                m_showCursor = !m_showCursor;
                InvalidateRect(m_hWnd, NULL, FALSE);
            }
            return 0;
        case WM_SETFOCUS:                                         // 获得焦点
            m_showCursor = true;
            InvalidateRect(m_hWnd, NULL, FALSE);
            return 0;
        case WM_KILLFOCUS:                                        // 失去焦点
            m_showCursor = false;
            InvalidateRect(m_hWnd, NULL, FALSE);
            return 0;
        case WM_ERASEBKGND: return 1;                            // 擦除背景 - 不处理
        case WM_DESTROY:                                          // 销毁
            if (m_cursorTimer) {
                KillTimer(m_hWnd, m_cursorTimer);
                m_cursorTimer = 0;
            }
            return 0;
        }
        return DefWindowProc(m_hWnd, msg, wParam, lParam);
    }

    /**
     * 绘制函数 - 使用双缓冲避免闪烁
     */
    void OnPaint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        RECT r;
        GetClientRect(m_hWnd, &r);

        // 创建内存DC进行双缓冲绘制
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, r.right, r.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        FillRect(memDC, &r, m_hBgBrush);              // 填充背景
        SelectObject(memDC, m_hFont);                 // 选择字体
        SetBkMode(memDC, TRANSPARENT);                // 透明背景

        // 获取文本度量
        TEXTMETRIC tm;
        GetTextMetrics(memDC, &tm);
        m_cw = tm.tmAveCharWidth;                     // 字符宽度
        m_lh = tm.tmHeight + 4;                       // 行高
        if (m_lh < 16) m_lh = 16;

        // 计算各区域宽度
        m_aw = m_cw * 9 + 20;                         // 地址区宽度
        m_hw = m_cw * (m_bpl * 3 + 2) + 10;           // 十六进制区宽度
        m_as = m_aw + m_hw + 15;                      // ASCII区起始位置

        // 计算可见行数
        int totalLines = GetTotalLines();
        m_linesVisible = max(1, (r.bottom - m_lh) / m_lh);
        if (m_topLine + m_linesVisible > totalLines && totalLines > 0) {
            m_topLine = max(0, totalLines - m_linesVisible);
        }

        // 绘制各个区域
        DrawHeader(memDC, r);                         // 表头
        DrawAddressArea(memDC, r);                    // 地址区
        DrawHexArea(memDC, r);                        // 十六进制区
        DrawAsciiArea(memDC, r);                      // ASCII区
        DrawSeparators(memDC, r);                     // 分隔线

        // 复制到屏幕
        BitBlt(hdc, 0, 0, r.right, r.bottom, memDC, 0, 0, SRCCOPY);

        // 清理资源
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        UpdateScrollBar();
        EndPaint(m_hWnd, &ps);
    }

    /**
     * 绘制表头 - "偏移地址"、"00 01 02..."和"文本"
     */
    void DrawHeader(HDC hdc, RECT& r) {
        SelectObject(hdc, m_hBoldFont);
        SetTextColor(hdc, Theme::Header);
        TextOut(hdc, 10, 2, L"偏移地址", 4);

        int x = m_aw + 15;
        for (int i = 0; i < m_bpl; i++) {
            WCHAR b[4];
            swprintf_s(b, 4, L"%02X", i);
            TextOut(hdc, x, 2, b, 2);
            x += m_cw * 3;
            if (i == 7) x += m_cw;
        }
        TextOut(hdc, m_as + 5, 2, L"文本", 2);

        SelectObject(hdc, m_hFont);
        SelectObject(hdc, m_hSepPen);
        MoveToEx(hdc, 0, m_lh - 2, NULL);
        LineTo(hdc, r.right, m_lh - 2);
    }

    /**
     * 绘制地址区 - 显示每行的起始地址
     */
    void DrawAddressArea(HDC hdc, RECT& r) {
        SetTextColor(hdc, Theme::Address);
        int tl = GetTotalLines();
        if (tl <= 0) {
            // 空数据时显示一行地址
            WCHAR a[16];
            swprintf_s(a, 16, L"%08X", 0);
            TextOut(hdc, 10, m_lh, a, 8);
            return;
        }
        int y = m_lh;
        for (int i = 0; i < m_linesVisible; i++) {
            int li = m_topLine + i;
            if (li >= tl) break;
            WCHAR a[16];
            swprintf_s(a, 16, L"%08X", li * m_bpl);
            TextOut(hdc, 10, y + i * m_lh, a, 8);
        }
    }

    /**
     * 绘制十六进制区 - 显示十六进制数据
     */
    void DrawHexArea(HDC hdc, RECT& r) {
        if (!m_pData || !m_pCursorPos || !m_pNibblePos) return;

        int tl = GetTotalLines();
        if (tl <= 0) {
            // 空数据显示占位符 "--"
            int y = m_lh;
            int x = m_aw + 15;
            SetTextColor(hdc, Theme::HexText);
            SetBkMode(hdc, TRANSPARENT);
            for (int j = 0; j < m_bpl; j++) {
                TextOut(hdc, x, y, L"--", 2);
                x += m_cw * 3;
                if (j == 7) x += m_cw;
            }
            return;
        }

        int y = m_lh;
        int cw = m_cw;

        for (int i = 0; i < m_linesVisible; i++) {
            int li = m_topLine + i;
            if (li >= tl) break;

            int x = m_aw + 15;
            int bo = li * m_bpl;

            for (int j = 0; j < m_bpl; j++) {
                int off = bo + j;
                if (off >= (int)m_pData->GetSize()) break;

                WCHAR hx[4];
                swprintf_s(hx, 4, L"%02X", m_pData->GetByte(off));

                bool isSel = IsSelected(off);
                bool isCur = (off == *m_pCursorPos && m_showCursor);

                if (isSel) {
                    // 选中状态 - 高亮背景
                    SetTextColor(hdc, Theme::TextBright);
                    SetBkColor(hdc, Theme::Selection);
                    SetBkMode(hdc, OPAQUE);
                }
                else if (isCur) {
                    // 光标状态 - 光标背景
                    SetTextColor(hdc, Theme::TextBright);
                    SetBkColor(hdc, Theme::Cursor);
                    SetBkMode(hdc, OPAQUE);

                    HBRUSH hl = CreateSolidBrush(Theme::CursorHex);
                    if (*m_pNibblePos == 0) {
                        RECT hr = { x, y + i * m_lh, x + cw, y + (i + 1) * m_lh };
                        FillRect(hdc, &hr, hl);
                    }
                    else {
                        RECT hr = { x + cw, y + i * m_lh, x + cw * 2, y + (i + 1) * m_lh };
                        FillRect(hdc, &hr, hl);
                    }
                    DeleteObject(hl);
                }
                else {
                    // 正常状态
                    SetTextColor(hdc, Theme::HexText);
                    SetBkMode(hdc, TRANSPARENT);
                }

                TextOut(hdc, x, y + i * m_lh, hx, 2);
                x += cw * 3;
                if (j == 7) x += cw;
                SetBkMode(hdc, TRANSPARENT);
            }
        }
    }

    /**
     * 绘制ASCII区 - 显示UTF-8解码后的字符
     */
    void DrawAsciiArea(HDC hdc, RECT& r) {
        if (!m_pData || !m_pCursorPos) return;

        int tl = GetTotalLines();
        if (tl <= 0) {
            // 空数据时直接返回
            return;
        }

        int y = m_lh;
        int cw = m_cw;

        for (int i = 0; i < m_linesVisible; i++) {
            int li = m_topLine + i;
            if (li >= tl) break;

            int x = m_as + 5;
            int bo = li * m_bpl;

            for (int j = 0; j < m_bpl; ) {
                int off = bo + j;
                if (off >= (int)m_pData->GetSize()) break;

                int consumed = 0;
                std::wstring uc = DecodeUTF8Char(m_pData->GetData(), off, m_bpl - j, consumed);
                if (consumed == 0) {
                    j++;
                    continue;
                }

                // 检查是否在光标位置
                bool isCur = false;
                for (int k = 0; k < consumed; k++) {
                    if (off + k == *m_pCursorPos && m_showCursor) {
                        isCur = true;
                        break;
                    }
                }

                bool isSel = IsSelected(off);

                if (isSel) {
                    // 选中状态
                    SetTextColor(hdc, Theme::TextBright);
                    SetBkColor(hdc, Theme::Selection);
                    SetBkMode(hdc, OPAQUE);
                }
                else if (isCur) {
                    // 光标状态
                    SetTextColor(hdc, Theme::TextBright);
                    SetBkColor(hdc, Theme::Cursor);
                    SetBkMode(hdc, OPAQUE);
                }
                else {
                    // 正常状态
                    SetTextColor(hdc, Theme::AsciiText);
                    SetBkMode(hdc, TRANSPARENT);
                }

                int pw = (int)uc.length() * cw;
                TextOut(hdc, x, y + i * m_lh, uc.c_str(), (int)uc.length());
                x += pw;
                SetBkMode(hdc, TRANSPARENT);
                j += consumed;
            }
        }
    }

    /**
     * 绘制分隔线 - 分隔不同区域
     */
    void DrawSeparators(HDC hdc, RECT& r) {
        SelectObject(hdc, m_hSepPen);
        MoveToEx(hdc, m_aw, 0, NULL);
        LineTo(hdc, m_aw, r.bottom);                  // 地址区与十六进制区分隔线
        MoveToEx(hdc, m_as - 5, 0, NULL);
        LineTo(hdc, m_as - 5, r.bottom);              // 十六进制区与ASCII区分隔线
    }

    // 窗口大小改变
    void OnSize(int w, int h) {
        m_linesVisible = max(1, (h - m_lh) / m_lh);
        UpdateScrollBar();
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 垂直滚动处理
    void OnVScroll(int code, int pos) {
        switch (code) {
        case SB_LINEUP: ScrollBy(-1); break;          // 向上滚动一行
        case SB_LINEDOWN: ScrollBy(1); break;         // 向下滚动一行
        case SB_PAGEUP: ScrollBy(-m_linesVisible); break;   // 向上翻页
        case SB_PAGEDOWN: ScrollBy(m_linesVisible); break;  // 向下翻页
        case SB_THUMBTRACK: ScrollTo(pos); break;     // 拖动滑块
        }
    }

    // 滚动指定行数
    void ScrollBy(int d) { ScrollTo(m_topLine + d); }

    // 滚动到指定行
    void ScrollTo(int line) {
        int totalLines = GetTotalLines();
        if (totalLines <= 0) {
            m_topLine = 0;
            SetScrollPos(m_hWnd, SB_VERT, 0, TRUE);
            InvalidateRect(m_hWnd, NULL, TRUE);
            return;
        }
        int maxLine = max(0, totalLines - m_linesVisible);
        if (line < 0) line = 0;
        if (line > maxLine) line = maxLine;
        if (line != m_topLine) {
            m_topLine = line;
            SetScrollPos(m_hWnd, SB_VERT, m_topLine, TRUE);
            InvalidateRect(m_hWnd, NULL, TRUE);
        }
    }

    // 滚动到指定偏移位置
    void ScrollToOffset(int off) {
        if (m_bpl <= 0) return;
        int totalLines = GetTotalLines();
        if (totalLines <= 0) {
            m_topLine = 0;
            return;
        }
        int line = off / m_bpl;
        if (line >= totalLines) line = totalLines - 1;
        if (line < m_topLine) ScrollTo(line);
        else if (line >= m_topLine + m_linesVisible) ScrollTo(line - m_linesVisible + 1);
    }

    // 获取总行数
    int GetTotalLines() {
        if (!m_pData || m_pData->GetSize() == 0) return 0;
        return ((int)m_pData->GetSize() + m_bpl - 1) / m_bpl;
    }

    // 更新滚动条
    void UpdateScrollBar() {
        if (m_linesVisible <= 0) m_linesVisible = 20;
        int totalLines = GetTotalLines();

        // 当总行数为0时，滚动条范围为0
        if (totalLines <= 0) {
            SCROLLINFO si = { sizeof(si) };
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
            si.nMin = 0;
            si.nMax = 0;
            si.nPage = 1;
            SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);
            m_topLine = 0;
            SetScrollPos(m_hWnd, SB_VERT, 0, TRUE);
            return;
        }

        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = max(0, totalLines - 1);
        si.nPage = m_linesVisible;
        SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);

        // 确保滚动位置有效
        int maxLine = max(0, totalLines - m_linesVisible);
        if (m_topLine > maxLine) {
            m_topLine = maxLine;
            SetScrollPos(m_hWnd, SB_VERT, m_topLine, TRUE);
        }
    }

    // 判断指定偏移是否被选中
    bool IsSelected(int off) {
        if (m_selStart == -1) return false;
        int s = min(m_selStart, m_selEnd), e = max(m_selStart, m_selEnd);
        return off >= s && off <= e;
    }
};

// ============ 二进制视图 ============
// ... (由于篇幅限制，BinaryView类的注释会类似，核心功能相同)
// 主要区别：显示二进制位(0/1)而不是十六进制

class BinaryView {
    HWND m_hWnd, m_hParent;                    // 窗口句柄
    ZoomManager* m_pZoom;                      // 缩放管理器指针
    DataManager* m_pData;                      // 数据管理器指针
    int* m_pCursorPos, * m_pBitPos;            // 光标位置和位位置
    int m_bpl;                                 // 每行字节数
    int m_topLine;                             // 顶部行号
    int m_linesVisible;                        // 可见行数
    int m_selStart, m_selEnd;                  // 选择范围
    int m_lh, m_aw, m_bw, m_as, m_cw, m_bpg;   // 布局尺寸
    HFONT m_hFont, m_hBoldFont;                // 字体句柄
    HBRUSH m_hBgBrush;                         // 背景画刷
    HPEN m_hSepPen;                            // 分隔线画笔
    UINT_PTR m_cursorTimer;                    // 光标闪烁定时器
    bool m_showCursor, m_visible;              // 光标显示状态和可见性

public:
    // 构造函数
    BinaryView() : m_hWnd(NULL), m_hParent(NULL), m_pZoom(NULL), m_pData(NULL),
        m_pCursorPos(NULL), m_pBitPos(NULL),
        m_bpl(8), m_topLine(0), m_linesVisible(20), m_selStart(-1), m_selEnd(-1),
        m_lh(20), m_aw(100), m_bw(400), m_as(520), m_cw(10), m_bpg(4),
        m_hFont(NULL), m_hBoldFont(NULL), m_hBgBrush(NULL), m_hSepPen(NULL),
        m_cursorTimer(0), m_showCursor(true), m_visible(false) {
    }

    // 析构函数
    ~BinaryView() {
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hBoldFont) DeleteObject(m_hBoldFont);
        if (m_hBgBrush) DeleteObject(m_hBgBrush);
        if (m_hSepPen) DeleteObject(m_hSepPen);
        if (m_cursorTimer) KillTimer(m_hWnd, m_cursorTimer);
    }

    // 创建字体
    void CreateFonts() {
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hBoldFont) DeleteObject(m_hBoldFont);
        int fs = m_pZoom ? m_pZoom->GetFontSize() : 13;
        LOGFONT lf = { 0 };
        lf.lfHeight = -fs;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Consolas");
        m_hFont = CreateFontIndirect(&lf);
        lf.lfWeight = FW_BOLD;
        m_hBoldFont = CreateFontIndirect(&lf);
    }

    // 创建二进制视图窗口
    HWND Create(HWND hp, HINSTANCE hi, ZoomManager* z, DataManager* d, int* cp, int* bp) {
        m_hParent = hp;
        m_pZoom = z;
        m_pData = d;
        m_pCursorPos = cp;
        m_pBitPos = bp;
        CreateFonts();
        m_hBgBrush = CreateSolidBrush(Theme::Background);
        m_hSepPen = CreatePen(PS_SOLID, 1, Theme::Separator);

        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = BinaryViewProc;
        wc.cbWndExtra = sizeof(LONG_PTR);
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(hi, MAKEINTRESOURCE(IDC_CUSTOM_CURSOR));
        if (!wc.hCursor) wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = m_hBgBrush;
        wc.lpszClassName = L"BinView6";

        static bool reg = false;
        if (!reg) { RegisterClassEx(&wc); reg = true; }

        m_hWnd = CreateWindowEx(0, L"BinView6", NULL,
            WS_CHILD | WS_VSCROLL | WS_TABSTOP,
            0, 0, 0, 0, hp, NULL, hi, this);
        m_cursorTimer = SetTimer(m_hWnd, 1, 500, NULL);
        return m_hWnd;
    }

    // 设置可见性
    void SetVisible(bool v) {
        m_visible = v;
        ShowWindow(m_hWnd, v ? SW_SHOW : SW_HIDE);
        if (v) {
            InvalidateRect(m_hWnd, NULL, TRUE);
            SetFocus(m_hWnd);
        }
    }

    // 更新缩放
    void UpdateZoom() { CreateFonts(); InvalidateRect(m_hWnd, NULL, TRUE); }

    // 清除选择
    void ClearSelection() { m_selStart = -1; m_selEnd = -1; }

    // 设置光标位置
    void SetCursorPos(int p) {
        if (m_pCursorPos) *m_pCursorPos = p;
        if (m_pBitPos) *m_pBitPos = 7;
        ScrollToOffset(p);
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 移动光标
    void MoveCursor(int delta) {
        if (!m_pCursorPos || !m_pData) return;
        int np = *m_pCursorPos + delta;
        if (np < 0) np = 0;
        if (np >= (int)m_pData->GetSize()) np = max(0, (int)m_pData->GetSize());
        *m_pCursorPos = np;
        if (m_pBitPos) *m_pBitPos = 7;
        ScrollToOffset(np);
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 处理二进制输入 (0/1)
    void HandleBinaryInput(WCHAR ch) {
        if (!m_pCursorPos || !m_pBitPos || !m_pData) return;
        if (ch != L'0' && ch != L'1') return;

        if (*m_pCursorPos >= (int)m_pData->GetSize()) {
            DataManager::EditAction a;
            a.offset = *m_pCursorPos;
            a.isInsert = true;
            a.oldData.clear();
            a.newData.push_back(0);
            m_pData->PushUndo(a);
            m_pData->InsertByte(*m_pCursorPos, 0);
            UpdateScrollBar();
        }

        unsigned char byte = m_pData->GetByte(*m_pCursorPos), oldByte = byte;
        if (ch == L'1') byte |= (1 << *m_pBitPos);
        else byte &= ~(1 << *m_pBitPos);

        if (byte != oldByte) {
            DataManager::EditAction a;
            a.offset = *m_pCursorPos;
            a.isInsert = false;
            a.oldData.push_back(oldByte);
            a.newData.push_back(byte);
            m_pData->PushUndo(a);
            m_pData->SetByte(*m_pCursorPos, byte);
        }

        if (*m_pBitPos > 0) (*m_pBitPos)--;
        else { *m_pBitPos = 7; MoveCursor(1); }
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 鼠标滚轮
    void OnMouseWheel(int delta) { ScrollBy(-delta / WHEEL_DELTA * 3); }

    // 鼠标左键按下
    void OnLButtonDown(int x, int y) {
        if (!m_pData || !m_pCursorPos || !m_pBitPos) return;
        int line = (y - m_lh) / m_lh + m_topLine;
        if (line < 0) line = 0;
        if (line >= GetTotalLines()) line = max(0, GetTotalLines() - 1);

        int col = -1;
        if (x >= m_aw && x < m_as - 5) {
            int rx = x - m_aw - 15;
            col = rx / (m_cw * 10);
            int bx = rx % (m_cw * 10);
            *m_pBitPos = 7 - (bx / m_cw);
            if (*m_pBitPos < 0) *m_pBitPos = 0;
            if (*m_pBitPos > 7) *m_pBitPos = 7;
        }
        else if (x >= m_as) {
            col = (x - m_as - 5) / m_cw;
            *m_pBitPos = 7;
        }

        if (col >= 0 && col < m_bpl) {
            int np = line * m_bpl + col;
            if (np < 0) np = 0;
            if (np > (int)m_pData->GetSize()) np = (int)m_pData->GetSize();
            *m_pCursorPos = np;
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                if (m_selStart == -1) m_selStart = *m_pCursorPos;
                m_selEnd = *m_pCursorPos;
            }
            else ClearSelection();
            m_showCursor = true;
            InvalidateRect(m_hWnd, NULL, TRUE);
            SetFocus(m_hWnd);
        }
    }

    // 滚动到指定行
    void ScrollTo(int line) {
        int totalLines = GetTotalLines();
        if (totalLines <= 0) {
            m_topLine = 0;
            SetScrollPos(m_hWnd, SB_VERT, 0, TRUE);
            InvalidateRect(m_hWnd, NULL, TRUE);
            return;
        }
        int maxLine = max(0, totalLines - m_linesVisible);
        if (line < 0) line = 0;
        if (line > maxLine) line = maxLine;
        if (line != m_topLine) {
            m_topLine = line;
            SetScrollPos(m_hWnd, SB_VERT, m_topLine, TRUE);
            InvalidateRect(m_hWnd, NULL, TRUE);
        }
    }

    // 获取总行数
    int GetTotalLines() {
        if (!m_pData || m_pData->GetSize() == 0) return 0;
        return ((int)m_pData->GetSize() + m_bpl - 1) / m_bpl;
    }

    // 更新滚动条
    void UpdateScrollBar() {
        if (m_linesVisible <= 0) m_linesVisible = 20;
        int totalLines = GetTotalLines();

        if (totalLines <= 0) {
            SCROLLINFO si = { sizeof(si) };
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
            si.nMin = 0;
            si.nMax = 0;
            si.nPage = 1;
            SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);
            m_topLine = 0;
            SetScrollPos(m_hWnd, SB_VERT, 0, TRUE);
            return;
        }

        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = max(0, totalLines - 1);
        si.nPage = m_linesVisible;
        SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);

        int maxLine = max(0, totalLines - m_linesVisible);
        if (m_topLine > maxLine) {
            m_topLine = maxLine;
            SetScrollPos(m_hWnd, SB_VERT, m_topLine, TRUE);
        }
    }

private:
    // 窗口过程
    static LRESULT CALLBACK BinaryViewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        BinaryView* p = NULL;
        if (m == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)l;
            p = (BinaryView*)cs->lpCreateParams;
            SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)p);
            p->m_hWnd = h;
        }
        else {
            p = (BinaryView*)GetWindowLongPtr(h, GWLP_USERDATA);
        }
        if (p) return p->WndProc(m, w, l);
        return DefWindowProc(h, m, w, l);
    }

    // 消息处理
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_PAINT: OnPaint(); return 0;
        case WM_SIZE: OnSize(LOWORD(lParam), HIWORD(lParam)); return 0;
        case WM_VSCROLL: OnVScroll(LOWORD(wParam), HIWORD(wParam)); return 0;
        case WM_MOUSEWHEEL:
            if (m_visible) OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_LBUTTONDOWN:
            if (m_visible) {
                OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                SetFocus(m_hWnd);
            }
            return 0;
        case WM_KEYDOWN: {
            if (!m_visible) break;
            bool sh = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool ct = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            switch (wParam) {
            case VK_LEFT:
                if (*m_pBitPos < 7) (*m_pBitPos)++;
                else MoveCursor(-1);
                if (!sh) ClearSelection();
                break;
            case VK_RIGHT:
                if (*m_pBitPos > 0) (*m_pBitPos)--;
                else MoveCursor(1);
                if (!sh) ClearSelection();
                break;
            case VK_UP: MoveCursor(-m_bpl); if (!sh) ClearSelection(); break;
            case VK_DOWN: MoveCursor(m_bpl); if (!sh) ClearSelection(); break;
            case VK_PRIOR: MoveCursor(-m_bpl * m_linesVisible); if (!sh) ClearSelection(); break;
            case VK_NEXT: MoveCursor(m_bpl * m_linesVisible); if (!sh) ClearSelection(); break;
            case VK_HOME:
                *m_pCursorPos = ct ? 0 : (*m_pCursorPos / m_bpl) * m_bpl;
                *m_pBitPos = 7;
                if (!sh) ClearSelection();
                ScrollToOffset(*m_pCursorPos);
                break;
            case VK_END: {
                int ds = max(0, (int)m_pData->GetSize());
                *m_pCursorPos = ct ? ds : min((*m_pCursorPos / m_bpl + 1) * m_bpl - 1, ds);
                *m_pBitPos = 0;
                if (!sh) ClearSelection();
                ScrollToOffset(*m_pCursorPos);
                break;
            }
            case VK_DELETE:
                if (m_pData->GetSize() == 0 || *m_pCursorPos >= (int)m_pData->GetSize()) break;
                {
                    DataManager::EditAction a;
                    a.offset = *m_pCursorPos;
                    a.isInsert = true;
                    a.oldData.clear();
                    a.newData.push_back(m_pData->GetByte(*m_pCursorPos));
                    m_pData->PushUndo(a);
                    m_pData->DeleteByte(*m_pCursorPos);
                    if (*m_pCursorPos >= (int)m_pData->GetSize() && m_pData->GetSize() > 0)
                        *m_pCursorPos = (int)m_pData->GetSize() - 1;
                    ClearSelection();
                    UpdateScrollBar();
                    InvalidateRect(m_hWnd, NULL, TRUE);
                }
                break;
            case VK_BACK:
                if (*m_pCursorPos > 0) {
                    (*m_pCursorPos)--;
                    *m_pBitPos = 7;
                    DataManager::EditAction a;
                    a.offset = *m_pCursorPos;
                    a.isInsert = true;
                    a.oldData.clear();
                    a.newData.push_back(m_pData->GetByte(*m_pCursorPos));
                    m_pData->PushUndo(a);
                    m_pData->DeleteByte(*m_pCursorPos);
                    UpdateScrollBar();
                    InvalidateRect(m_hWnd, NULL, TRUE);
                }
                break;
            case VK_INSERT: {
                DataManager::EditAction a;
                a.offset = *m_pCursorPos;
                a.isInsert = true;
                a.oldData.clear();
                a.newData.push_back(0);
                m_pData->PushUndo(a);
                m_pData->InsertByte(*m_pCursorPos, 0);
                UpdateScrollBar();
                InvalidateRect(m_hWnd, NULL, TRUE);
                break;
            }
            case VK_TAB:
                SendMessage(m_hParent, WM_COMMAND, IDM_TAB_HEX, 0);
                return 0;
            case VK_F3:
                SendMessage(m_hParent, WM_COMMAND, sh ? IDM_SEARCH_FINDPREV : IDM_SEARCH_FINDNEXT, 0);
                return 0;
            }
            if (sh) {
                if (m_selStart == -1) m_selStart = *m_pCursorPos;
                m_selEnd = *m_pCursorPos;
            }
            m_showCursor = true;
            InvalidateRect(m_hWnd, NULL, TRUE);
            return 0;
        }
        case WM_CHAR: {
            if (!m_visible) break;
            WCHAR ch = (WCHAR)wParam;
            HandleBinaryInput(ch);
            InvalidateRect(m_hWnd, NULL, TRUE);
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1) {
                m_showCursor = !m_showCursor;
                InvalidateRect(m_hWnd, NULL, FALSE);
            }
            return 0;
        case WM_SETFOCUS:
            m_showCursor = true;
            InvalidateRect(m_hWnd, NULL, FALSE);
            return 0;
        case WM_KILLFOCUS:
            m_showCursor = false;
            InvalidateRect(m_hWnd, NULL, FALSE);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY:
            if (m_cursorTimer) {
                KillTimer(m_hWnd, m_cursorTimer);
                m_cursorTimer = 0;
            }
            return 0;
        }
        return DefWindowProc(m_hWnd, msg, wParam, lParam);
    }

    // 绘制函数
    void OnPaint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        RECT r;
        GetClientRect(m_hWnd, &r);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP mb = CreateCompatibleBitmap(hdc, r.right, r.bottom);
        HBITMAP ob = (HBITMAP)SelectObject(memDC, mb);

        FillRect(memDC, &r, m_hBgBrush);
        SelectObject(memDC, m_hFont);
        SetBkMode(memDC, TRANSPARENT);

        TEXTMETRIC tm;
        GetTextMetrics(memDC, &tm);
        m_cw = tm.tmAveCharWidth;
        m_lh = tm.tmHeight + 4;
        if (m_lh < 16) m_lh = 16;
        m_aw = m_cw * 9 + 20;
        m_bw = m_cw * (m_bpl * 10 + max(0, m_bpl / m_bpg - 1) * 2) + 20;
        m_as = m_aw + m_bw + 15;

        int totalLines = GetTotalLines();
        m_linesVisible = max(1, (r.bottom - m_lh) / m_lh);
        if (m_topLine + m_linesVisible > totalLines && totalLines > 0) {
            m_topLine = max(0, totalLines - m_linesVisible);
        }

        DrawHeader(memDC, r);
        DrawAddressArea(memDC, r);
        DrawBinaryData(memDC, r);
        DrawAsciiArea(memDC, r);
        DrawSeparators(memDC, r);

        BitBlt(hdc, 0, 0, r.right, r.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, ob);
        DeleteObject(mb);
        DeleteDC(memDC);
        UpdateScrollBar();
        EndPaint(m_hWnd, &ps);
    }

    // 绘制表头
    void DrawHeader(HDC hdc, RECT& r) {
        SelectObject(hdc, m_hBoldFont);
        SetTextColor(hdc, Theme::Header);
        TextOut(hdc, 10, 2, L"偏移地址", 4);

        int x = m_aw + 15;
        for (int i = 0; i < m_bpl; i++) {
            WCHAR b[10];
            swprintf_s(b, 10, L" 字节%02d ", i);
            TextOut(hdc, x, 2, b, 7);
            x += m_cw * 10;
            if (i == m_bpg - 1) x += m_cw * 2;
        }
        TextOut(hdc, m_as + 5, 2, L"ASCII", 5);

        SelectObject(hdc, m_hFont);
        SelectObject(hdc, m_hSepPen);
        MoveToEx(hdc, 0, m_lh - 2, NULL);
        LineTo(hdc, r.right, m_lh - 2);
    }

    // 绘制地址区
    void DrawAddressArea(HDC hdc, RECT& r) {
        SetTextColor(hdc, Theme::Address);
        int tl = GetTotalLines();
        if (tl <= 0) {
            WCHAR a[16];
            swprintf_s(a, 16, L"%08X", 0);
            TextOut(hdc, 10, m_lh, a, 8);
            return;
        }
        int y = m_lh;
        for (int i = 0; i < m_linesVisible; i++) {
            int li = m_topLine + i;
            if (li >= tl) break;
            WCHAR a[16];
            swprintf_s(a, 16, L"%08X", li * m_bpl);
            TextOut(hdc, 10, y + i * m_lh, a, 8);
        }
    }

    // 绘制二进制数据
    void DrawBinaryData(HDC hdc, RECT& r) {
        if (!m_pData || !m_pCursorPos || !m_pBitPos) return;
        int tl = GetTotalLines();
        if (tl <= 0) {
            int y = m_lh;
            int x = m_aw + 15;
            SetTextColor(hdc, Theme::BinText0);
            SetBkMode(hdc, TRANSPARENT);
            for (int j = 0; j < m_bpl; j++) {
                for (int bit = 7; bit >= 0; bit--) {
                    TextOut(hdc, x, y, L"-", 1);
                    x += m_cw;
                }
                x += m_cw * 2;
                if (j == m_bpg - 1 && m_bpg > 0) x += m_cw * 2;
            }
            return;
        }

        int y = m_lh;
        int cw = m_cw;
        for (int i = 0; i < m_linesVisible; i++) {
            int li = m_topLine + i;
            if (li >= tl) break;
            int x = m_aw + 15;
            int bo = li * m_bpl;

            for (int j = 0; j < m_bpl; j++) {
                int off = bo + j;
                if (off >= (int)m_pData->GetSize()) break;
                unsigned char byte = m_pData->GetByte(off);
                bool isCur = (off == *m_pCursorPos && m_showCursor);

                for (int bit = 7; bit >= 0; bit--) {
                    bool bv = (byte >> bit) & 1;
                    WCHAR bc = bv ? L'1' : L'0';
                    int bx = x + (7 - bit) * cw;

                    if (isCur && bit == *m_pBitPos) {
                        SetTextColor(hdc, Theme::TextBright);
                        SetBkColor(hdc, Theme::CursorBin);
                        SetBkMode(hdc, OPAQUE);
                    }
                    else {
                        SetTextColor(hdc, bv ? Theme::BinText1 : Theme::BinText0);
                        SetBkMode(hdc, TRANSPARENT);
                    }
                    WCHAR s[2] = { bc, 0 };
                    TextOut(hdc, bx, y + i * m_lh, s, 1);
                    SetBkMode(hdc, TRANSPARENT);
                }
                x += cw * 10;
                if (j == m_bpg - 1 && m_bpg > 0) x += cw * 2;
            }
        }
    }

    // 绘制ASCII区
    void DrawAsciiArea(HDC hdc, RECT& r) {
        if (!m_pData || !m_pCursorPos) return;
        int tl = GetTotalLines();
        if (tl <= 0) return;

        int y = m_lh;
        int cw = m_cw;
        for (int i = 0; i < m_linesVisible; i++) {
            int li = m_topLine + i;
            if (li >= tl) break;
            int x = m_as + 5;
            int bo = li * m_bpl;

            for (int j = 0; j < m_bpl; j++) {
                int off = bo + j;
                if (off >= (int)m_pData->GetSize()) break;
                unsigned char c = m_pData->GetByte(off);
                WCHAR ch = (WCHAR)c;
                bool isCur = (off == *m_pCursorPos && m_showCursor);

                if (isCur) {
                    SetTextColor(hdc, Theme::TextBright);
                    SetBkColor(hdc, Theme::Cursor);
                    SetBkMode(hdc, OPAQUE);
                }
                else {
                    SetTextColor(hdc, Theme::AsciiText);
                    SetBkMode(hdc, TRANSPARENT);
                }
                WCHAR s[2] = { ch, 0 };
                TextOut(hdc, x + j * cw, y + i * m_lh, s, 1);
                SetBkMode(hdc, TRANSPARENT);
            }
        }
    }

    // 绘制分隔线
    void DrawSeparators(HDC hdc, RECT& r) {
        SelectObject(hdc, m_hSepPen);
        MoveToEx(hdc, m_aw, 0, NULL);
        LineTo(hdc, m_aw, r.bottom);
        MoveToEx(hdc, m_as - 5, 0, NULL);
        LineTo(hdc, m_as - 5, r.bottom);
    }

    // 窗口大小改变
    void OnSize(int w, int h) {
        m_linesVisible = max(1, (h - m_lh) / m_lh);
        UpdateScrollBar();
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    // 垂直滚动处理
    void OnVScroll(int code, int pos) {
        switch (code) {
        case SB_LINEUP: ScrollBy(-1); break;
        case SB_LINEDOWN: ScrollBy(1); break;
        case SB_PAGEUP: ScrollBy(-m_linesVisible); break;
        case SB_PAGEDOWN: ScrollBy(m_linesVisible); break;
        case SB_THUMBTRACK: ScrollTo(pos); break;
        }
    }

    // 滚动指定行数
    void ScrollBy(int d) { ScrollTo(m_topLine + d); }

    // 滚动到指定偏移
    void ScrollToOffset(int off) {
        if (m_bpl <= 0) return;
        int line = off / m_bpl;
        int totalLines = GetTotalLines();
        if (totalLines <= 0) { m_topLine = 0; return; }
        if (line >= totalLines) line = totalLines - 1;
        if (line < m_topLine) ScrollTo(line);
        else if (line >= m_topLine + m_linesVisible) ScrollTo(line - m_linesVisible + 1);
    }

    // 判断是否选中
    bool IsSelected(int off) {
        if (m_selStart == -1) return false;
        int s = min(m_selStart, m_selEnd), e = max(m_selStart, m_selEnd);
        return off >= s && off <= e;
    }
};

// ============ 滑块子类化 - 高对比度样式 ============
/**
 * 滑块自定义绘制 - 高对比度黑白样式
 */
LRESULT CALLBACK ZoomSliderProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (msg) {
    case WM_ERASEBKGND: {
        // 自定义绘制滑块背景
        RECT rc;
        GetClientRect(hWnd, &rc);
        HDC hdc = (HDC)wParam;
        // 黑色背景
        HBRUSH hBrush = CreateSolidBrush(Theme::SliderBg);
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);

        // 绘制滑块轨道 - 灰色
        RECT trackRc = rc;
        trackRc.top += (rc.bottom - rc.top) / 2 - 2;
        trackRc.bottom = trackRc.top + 4;
        trackRc.left += 6;
        trackRc.right -= 6;
        HBRUSH trackBrush = CreateSolidBrush(Theme::SliderChannel);
        FillRect(hdc, &trackRc, trackBrush);
        DeleteObject(trackBrush);
        return TRUE;
    }
    case WM_PAINT: {
        // 调用默认绘制，但我们已经处理了背景
        DefSubclassProc(hWnd, msg, wParam, lParam);
        return 0;
    }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ============ 状态栏子类化 - 确保文字颜色为白色 ============
/**
 * 状态栏子类化 - 强制所有文字为白色
 */
LRESULT CALLBACK StatusBarProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));           // 文字设为白色
        SetBkMode(hdc, TRANSPARENT);                     // 透明背景
        static HBRUSH hBrush = NULL;
        if (!hBrush) hBrush = CreateSolidBrush(Theme::StatusBarBg);
        return (LRESULT)hBrush;
    }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ============ 主窗口类 ============
/**
 * 主窗口 - 管理所有子控件和整体逻辑
 */
class MainWindow {
    HWND m_hWnd;                    // 主窗口句柄
    HINSTANCE m_hInstance;          // 实例句柄
    HWND m_hToolBar;                // 工具栏
    HWND m_hStatusBar;              // 状态栏
    HWND m_hTabHex, m_hTabBin;      // 选项卡按钮
    HWND m_hZoomLabel, m_hZoomSlider;  // 缩放控件
    HWND m_hHexViewWnd, m_hBinViewWnd; // 视图窗口
    HexView m_hexView;              // 十六进制视图对象
    BinaryView m_binView;           // 二进制视图对象
    DataManager m_data;             // 数据管理器
    ZoomManager m_zoom;             // 缩放管理器
    int m_cursorPos, m_nibblePos, m_bitPos, m_currentPage;  // 光标和页面状态
    std::wstring m_filePath, m_fileName;  // 文件路径和名称
    HCURSOR m_hCursor;              // 自定义光标
    HACCEL m_hAccel;                // 快捷键
    std::vector<unsigned char> m_findPattern;  // 查找模式
    std::vector<int> m_findResults;           // 查找结果
    int m_currentFindIndex;         // 当前查找索引

public:
    MainWindow() : m_hWnd(NULL), m_hInstance(NULL),
        m_hToolBar(NULL), m_hStatusBar(NULL),
        m_hTabHex(NULL), m_hTabBin(NULL),
        m_hZoomLabel(NULL), m_hZoomSlider(NULL),
        m_hHexViewWnd(NULL), m_hBinViewWnd(NULL),
        m_cursorPos(0), m_nibblePos(0), m_bitPos(7),
        m_currentPage(0), m_currentFindIndex(-1),
        m_hCursor(NULL), m_hAccel(NULL) {
    }

    ~MainWindow() { if (m_hCursor) DestroyCursor(m_hCursor); }

    /**
     * 创建主窗口
     */
    bool Create(HINSTANCE hi, int nCmdShow) {
        m_hInstance = hi;
        // 加载自定义光标
        m_hCursor = LoadCursor(hi, MAKEINTRESOURCE(IDC_CUSTOM_CURSOR));
        if (!m_hCursor) m_hCursor = LoadCursor(NULL, IDC_ARROW);
        m_hAccel = LoadAccelerators(hi, MAKEINTRESOURCE(IDC_BINARYHEXEDITOR));

        // 注册主窗口类
        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = MainWndProc;
        wc.hInstance = hi;
        // 主窗口使用自定义光标
        wc.hCursor = m_hCursor;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"HexEditorMainWnd8";
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(IDI_BINARYHEXEDITOR));
        if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hIconSm = LoadIcon(hi, MAKEINTRESOURCE(IDI_SMALL));
        if (!wc.hIconSm) wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
        wc.lpszMenuName = MAKEINTRESOURCE(IDC_BINARYHEXEDITOR);

        if (!RegisterClassEx(&wc)) {
            MessageBox(NULL, L"注册窗口类失败！", L"错误", MB_ICONERROR);
            return false;
        }

        // 加载标题字符串
        WCHAR title[256] = L"BinaryHexEditor Pro";
        LoadString(hi, IDS_APP_TITLE, title, 256);
        if (title[0] == 0) wcscpy_s(title, 256, L"BinaryHexEditor Pro");

        // 创建主窗口
        m_hWnd = CreateWindowEx(
            WS_EX_ACCEPTFILES,          // 支持拖放文件
            L"HexEditorMainWnd8",       // 窗口类名
            title,                      // 窗口标题
            WS_OVERLAPPEDWINDOW,        // 标准窗口样式
            CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,  // 位置和大小
            NULL, NULL, hi, this);

        if (!m_hWnd) {
            MessageBox(NULL, L"创建窗口失败！", L"错误", MB_ICONERROR);
            return false;
        }

        ShowWindow(m_hWnd, nCmdShow);
        UpdateWindow(m_hWnd);
        return true;
    }

    /**
     * 消息循环
     */
    void Run() {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            if (!m_hAccel || !TranslateAccelerator(m_hWnd, m_hAccel, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

private:
    // 主窗口过程
    static LRESULT CALLBACK MainWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        MainWindow* p = NULL;
        if (m == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)l;
            p = (MainWindow*)cs->lpCreateParams;
            if (p) {
                SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)p);
                p->m_hWnd = h;
                p->OnCreate();
            }
            return 0;
        }
        p = (MainWindow*)GetWindowLongPtr(h, GWLP_USERDATA);
        if (p) return p->WndProc(m, w, l);
        return DefWindowProc(h, m, w, l);
    }

    // 消息处理
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_SIZE: OnSize(LOWORD(lParam), HIWORD(lParam)); return 0;  // 窗口大小改变
        case WM_COMMAND: OnCommand(LOWORD(wParam), HIWORD(wParam)); return 0;  // 命令
        case WM_DROPFILES: OnDropFiles((HDROP)wParam); return 0;          // 拖放文件
        case WM_HSCROLL: OnHScroll(HIWORD(wParam), LOWORD(wParam), (HWND)lParam); return 0;  // 水平滚动
        case WM_LBUTTONDOWN: {                                            // 鼠标左键
            HWND t = (m_currentPage == 0) ? m_hHexViewWnd : m_hBinViewWnd;
            if (t && IsWindow(t)) SetFocus(t);
            return 0;
        }
        case WM_MOUSEWHEEL: {                                             // 鼠标滚轮
            short d = GET_WHEEL_DELTA_WPARAM(wParam);
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl) {                                                   // Ctrl+滚轮 = 缩放
                if (d > 0) m_zoom.ZoomIn();
                else m_zoom.ZoomOut();
                m_hexView.UpdateZoom();
                m_binView.UpdateZoom();
                UpdateZoomControls();
            }
            return 0;
        }
        case WM_NOTIFY: {                                                 // 通知消息
            NMHDR* nm = (NMHDR*)lParam;
            if (nm && nm->hwndFrom == m_hZoomSlider && nm->code == NM_RELEASEDCAPTURE) {
                int zp = (int)SendMessage(m_hZoomSlider, TBM_GETPOS, 0, 0);
                m_zoom.SetZoom(zp / 100.0);
                m_hexView.UpdateZoom();
                m_binView.UpdateZoom();
                UpdateZoomControls();
                UpdateStatusBar();
                if (m_hHexViewWnd) InvalidateRect(m_hHexViewWnd, NULL, TRUE);
                if (m_hBinViewWnd) InvalidateRect(m_hBinViewWnd, NULL, TRUE);
            }
            return 0;
        }
                      // 处理状态栏和标签的文本颜色
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));          // 文字设为白色
            SetBkMode(hdc, TRANSPARENT);                    // 透明背景
            static HBRUSH hBrush = NULL;
            if (!hBrush) hBrush = CreateSolidBrush(Theme::StatusBarBg);
            return (LRESULT)hBrush;
        }
        case WM_CLOSE:                                        // 关闭窗口
            if (m_data.IsModified()) {
                int r = MessageBox(m_hWnd, L"文件已修改，是否保存？", L"确认",
                    MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDYES) SaveFile();
                else if (r == IDCANCEL) return 0;
            }
            DestroyWindow(m_hWnd);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;       // 销毁
        case WM_SETCURSOR: {                                  // 设置光标
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(m_hCursor);
                return TRUE;
            }
            break;
        }
        }
        return DefWindowProc(m_hWnd, msg, wParam, lParam);
    }

    /**
     * 创建子控件
     */
    void OnCreate() {
        CreateToolBar();            // 创建工具栏
        CreateStatusBar();          // 创建状态栏
        CreateTabButtons();         // 创建选项卡
        CreateViews();              // 创建视图
        CreateZoomControls();       // 创建缩放控件
        DragAcceptFiles(m_hWnd, TRUE);  // 启用拖放
        UpdateStatusBar();
        SwitchPage(0);
    }

    /**
     * 创建工具栏
     */
    void CreateToolBar() {
        m_hToolBar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER,
            0, 0, 0, 0, m_hWnd, NULL, m_hInstance, NULL);
        if (!m_hToolBar) return;

        SendMessage(m_hToolBar, TB_SETBKCOLOR, 0, Theme::ToolBarBg);
        SendMessage(m_hToolBar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

        // 创建图像列表
        HIMAGELIST himl = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 12, 0);
        if (himl) {
            HICON hi = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_SMALL));
            if (hi) ImageList_AddIcon(himl, hi);
            SendMessage(m_hToolBar, TB_SETIMAGELIST, 0, (LPARAM)himl);
        }

        // 工具栏按钮
        TBBUTTON tb[] = {
            {0, IDM_FILE_NEW, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"新建"},
            {0, IDM_FILE_OPEN, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"打开"},
            {0, IDM_FILE_SAVE, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"保存"},
            {0, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0, 0},
            {0, IDM_EDIT_UNDO, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"撤销"},
            {0, IDM_EDIT_REDO, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"重做"},
            {0, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0, 0},
            {0, IDM_SEARCH_FIND, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"查找"},
            {0, IDM_SEARCH_GOTO, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0, (INT_PTR)L"转到"},
        };
        SendMessage(m_hToolBar, TB_ADDBUTTONS, sizeof(tb) / sizeof(TBBUTTON), (LPARAM)tb);
        SendMessage(m_hToolBar, TB_AUTOSIZE, 0, 0);
    }

    /**
     * 创建状态栏
     */
    void CreateStatusBar() {
        m_hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, m_hWnd, (HMENU)IDC_STATUSBAR, m_hInstance, NULL);
        if (!m_hStatusBar) return;

        SendMessage(m_hStatusBar, SB_SETBKCOLOR, 0, Theme::StatusBarBg);
        int parts[] = { 300, 500, 700, -1 };
        SendMessage(m_hStatusBar, SB_SETPARTS, 4, (LPARAM)parts);
        // 子类化状态栏以处理文字颜色
        SetWindowSubclass(m_hStatusBar, StatusBarProc, 0, 0);
    }

    /**
     * 创建选项卡按钮
     */
    void CreateTabButtons() {
        m_hTabHex = CreateWindow(L"BUTTON", L"十六进制 / 结果",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            0, 0, 140, 30, m_hWnd, (HMENU)IDM_TAB_HEX, m_hInstance, NULL);
        m_hTabBin = CreateWindow(L"BUTTON", L"二进制 / 结果",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            140, 0, 140, 30, m_hWnd, (HMENU)IDM_TAB_BIN, m_hInstance, NULL);
    }

    /**
     * 创建视图窗口
     */
    void CreateViews() {
        RECT cr;
        GetClientRect(m_hWnd, &cr);

        RECT tr = { 0 };
        if (m_hToolBar) GetWindowRect(m_hToolBar, &tr);
        int th = tr.bottom - tr.top, tabh = 32;

        RECT sr = { 0 };
        if (m_hStatusBar) GetWindowRect(m_hStatusBar, &sr);
        int sh = sr.bottom - sr.top;

        int vy = th + tabh, vh = max(1, cr.bottom - vy - sh);

        m_hHexViewWnd = m_hexView.Create(m_hWnd, m_hInstance, &m_zoom, &m_data,
            &m_cursorPos, &m_nibblePos);
        m_hBinViewWnd = m_binView.Create(m_hWnd, m_hInstance, &m_zoom, &m_data,
            &m_cursorPos, &m_bitPos);

        if (m_hHexViewWnd) SetWindowPos(m_hHexViewWnd, NULL, 0, vy, cr.right, vh, SWP_NOZORDER);
        if (m_hBinViewWnd) SetWindowPos(m_hBinViewWnd, NULL, 0, vy, cr.right, vh, SWP_NOZORDER);
    }

    /**
     * 创建缩放控件
     */
    void CreateZoomControls() {
        if (!m_hStatusBar) return;
        RECT sr;
        GetClientRect(m_hStatusBar, &sr);
        int sh = sr.bottom - sr.top;

        // 缩放百分比标签
        m_hZoomLabel = CreateWindow(L"STATIC", L"100%",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 2, 50, max(1, sh - 4),
            m_hStatusBar, (HMENU)IDC_ZOOMLABEL, m_hInstance, NULL);

        // 创建更长的滑块 (180像素)
        m_hZoomSlider = CreateWindow(TRACKBAR_CLASS, NULL,
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_BOTH,
            0, 2, 180, max(1, sh - 4),
            m_hStatusBar, (HMENU)IDC_ZOOMSLIDER, m_hInstance, NULL);

        if (m_hZoomSlider) {
            SendMessage(m_hZoomSlider, TBM_SETRANGE, TRUE, MAKELONG(25, 400));
            SendMessage(m_hZoomSlider, TBM_SETPOS, TRUE, 100);
            SendMessage(m_hZoomSlider, TBM_SETPAGESIZE, 0, 10);
            SendMessage(m_hZoomSlider, TBM_SETLINESIZE, 0, 1);
            // 子类化滑块以实现高对比度自定义绘制
            SetWindowSubclass(m_hZoomSlider, ZoomSliderProc, 0, 0);
        }
    }

    /**
     * 切换页面
     */
    void SwitchPage(int pg) {
        m_currentPage = pg;
        m_hexView.SetVisible(pg == 0);
        m_binView.SetVisible(pg == 1);

        if (pg == 0 && m_hHexViewWnd) SetFocus(m_hHexViewWnd);
        else if (pg == 1 && m_hBinViewWnd) SetFocus(m_hBinViewWnd);

        if (m_hTabHex) InvalidateRect(m_hTabHex, NULL, TRUE);
        if (m_hTabBin) InvalidateRect(m_hTabBin, NULL, TRUE);
        UpdateStatusBar();
    }

    /**
     * 窗口大小改变
     */
    void OnSize(int w, int h) {
        if (m_hToolBar) SendMessage(m_hToolBar, TB_AUTOSIZE, 0, 0);
        if (m_hStatusBar) SendMessage(m_hStatusBar, WM_SIZE, 0, 0);

        RECT tr = { 0 }, sr = { 0 };
        if (m_hToolBar) GetWindowRect(m_hToolBar, &tr);
        int th = tr.bottom - tr.top, tabh = 32;
        if (m_hStatusBar) GetWindowRect(m_hStatusBar, &sr);
        int sh = sr.bottom - sr.top;

        if (m_hTabHex) SetWindowPos(m_hTabHex, NULL, 0, th, 140, tabh, SWP_NOZORDER);
        if (m_hTabBin) SetWindowPos(m_hTabBin, NULL, 140, th, 140, tabh, SWP_NOZORDER);

        int vy = th + tabh, vh = max(1, h - vy - sh);
        if (m_hHexViewWnd) SetWindowPos(m_hHexViewWnd, NULL, 0, vy, w, vh, SWP_NOZORDER);
        if (m_hBinViewWnd) SetWindowPos(m_hBinViewWnd, NULL, 0, vy, w, vh, SWP_NOZORDER);

        UpdateZoomControls();
    }

    /**
     * 更新缩放控件
     */
    void UpdateZoomControls() {
        if (!m_hStatusBar) return;
        RECT sr;
        GetClientRect(m_hStatusBar, &sr);
        int r = sr.right, b = sr.bottom;

        if (m_hZoomLabel) {
            SetWindowPos(m_hZoomLabel, NULL, r - 70, 2, 50, max(1, b - 4), SWP_NOZORDER);
            WCHAR zt[16];
            swprintf_s(zt, 16, L"%d%%", m_zoom.GetZoomPercentage());
            SetWindowText(m_hZoomLabel, zt);
        }
        if (m_hZoomSlider) {
            SetWindowPos(m_hZoomSlider, NULL, r - 200, 2, 180, max(1, b - 4), SWP_NOZORDER);
        }
    }

    /**
     * 命令处理
     */
    void OnCommand(WORD cmd, WORD) {
        switch (cmd) {
        case IDM_TAB_HEX: SwitchPage(0); break;
        case IDM_TAB_BIN: SwitchPage(1); break;
        case IDM_FILE_NEW: NewFile(); break;
        case IDM_FILE_OPEN: OpenFile(); break;
        case IDM_FILE_SAVE: SaveFile(); break;
        case IDM_FILE_SAVEAS: SaveFileAs(); break;
        case IDM_FILE_CLOSE: CloseFile(); break;
        case IDM_FILE_EXPORT_C: ExportAsC(); break;
        case IDM_FILE_EXPORT_PY: ExportAsPython(); break;
        case IDM_FILE_EXPORT_JSON: ExportAsJSON(); break;
        case IDM_EXIT: SendMessage(m_hWnd, WM_CLOSE, 0, 0); break;
        case IDM_EDIT_UNDO: Undo(); break;
        case IDM_EDIT_REDO: Redo(); break;
        case IDM_EDIT_CUT: Cut(); break;
        case IDM_EDIT_COPY: Copy(); break;
        case IDM_EDIT_PASTE: Paste(); break;
        case IDM_EDIT_DELETE: DeleteCurrent(); break;
        case IDM_EDIT_SELECTALL: SelectAll(); break;
        case IDM_EDIT_DUPLICATE: DuplicateLine(); break;
        case IDM_SEARCH_FIND: ShowFindDialog(); break;
        case IDM_SEARCH_FINDNEXT: FindNext(); break;
        case IDM_SEARCH_FINDPREV: FindPrev(); break;
        case IDM_SEARCH_REPLACE: ShowReplaceDialog(); break;
        case IDM_SEARCH_REPLACEALL: ReplaceAll(); break;
        case IDM_SEARCH_GOTO: ShowGotoDialog(); break;
        case IDM_VIEW_ZOOMIN:
            m_zoom.ZoomIn();
            m_hexView.UpdateZoom();
            m_binView.UpdateZoom();
            UpdateZoomControls();
            UpdateStatusBar();
            break;
        case IDM_VIEW_ZOOMOUT:
            m_zoom.ZoomOut();
            m_hexView.UpdateZoom();
            m_binView.UpdateZoom();
            UpdateZoomControls();
            UpdateStatusBar();
            break;
        case IDM_VIEW_ZOOMRESET:
            m_zoom.ZoomReset();
            m_hexView.UpdateZoom();
            m_binView.UpdateZoom();
            UpdateZoomControls();
            UpdateStatusBar();
            break;
        case IDM_VIEW_FULLSCREEN: m_zoom.ToggleFullScreen(m_hWnd); break;
        case IDM_VIEW_BYTESPERROW: ShowBytesPerRowDialog(); break;
        case IDM_TOOL_HASH: ShowHashDialog(); break;
        case IDM_TOOL_STATS: ShowStatsDialog(); break;
        case IDM_ABOUT: DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), m_hWnd, AboutDlgProc); break;
        }
    }

    /**
     * 水平滚动 - 实时更新缩放
     */
    void OnHScroll(int, int, HWND hw) {
        if (hw == m_hZoomSlider && m_hZoomSlider) {
            int zp = (int)SendMessage(m_hZoomSlider, TBM_GETPOS, 0, 0);
            m_zoom.SetZoom(zp / 100.0);
            m_hexView.UpdateZoom();
            m_binView.UpdateZoom();
            UpdateZoomControls();
            UpdateStatusBar();
            // 强制立即重绘视图
            if (m_hHexViewWnd) InvalidateRect(m_hHexViewWnd, NULL, TRUE);
            if (m_hBinViewWnd) InvalidateRect(m_hBinViewWnd, NULL, TRUE);
        }
    }

    /**
     * 拖放文件处理
     */
    void OnDropFiles(HDROP hd) {
        WCHAR fp[MAX_PATH] = { 0 };
        DragQueryFile(hd, 0, fp, MAX_PATH);
        DragFinish(hd);
        LoadFile(fp);
    }

    // ============ 文件操作 ============

    /**
     * 新建文件
     */
    void NewFile() {
        if (m_data.IsModified()) {
            int r = MessageBox(m_hWnd, L"当前文件已修改，是否保存？", L"新建文件",
                MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDYES) SaveFile();
            else if (r == IDCANCEL) return;
        }
        std::vector<unsigned char> ed;
        m_data.SetData(ed);
        m_filePath.clear();
        m_fileName.clear();
        m_cursorPos = 0;
        m_nibblePos = 0;
        m_bitPos = 7;
        m_findResults.clear();
        m_currentFindIndex = -1;
        if (m_currentPage == 0) m_hexView.SetCursorPos(0);
        else m_binView.SetCursorPos(0);
        UpdateTitle();
        UpdateStatusBar();
    }

    /**
     * 打开文件
     */
    void OpenFile() {
        WCHAR fp[MAX_PATH] = { 0 };
        OPENFILENAME of = { 0 };
        of.lStructSize = sizeof(of);
        of.hwndOwner = m_hWnd;
        of.lpstrFile = fp;
        of.nMaxFile = MAX_PATH;
        of.lpstrFilter = L"所有文件(*.*)\0*.*\0资源文件(*.rc)\0*.rc\0文本文件(*.txt)\0*.txt\0\0";
        of.nFilterIndex = 1;
        of.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        if (GetOpenFileName(&of)) LoadFile(fp);
    }

    /**
     * 加载文件
     */
    void LoadFile(const std::wstring& fp) {
        std::ifstream f(fp, std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            MessageBox(m_hWnd, L"无法打开文件！", L"错误", MB_ICONERROR);
            return;
        }

        std::streamsize sz = f.tellg();
        f.seekg(0, std::ios::beg);

        if (sz <= 0) {
            std::vector<unsigned char> ed;
            m_data.SetData(ed);
        }
        else {
            std::vector<unsigned char> d(sz);
            if (!f.read((char*)d.data(), sz)) {
                MessageBox(m_hWnd, L"读取失败！", L"错误", MB_ICONERROR);
                f.close();
                return;
            }
            m_data.SetData(d);
        }
        f.close();

        m_filePath = fp;
        size_t p = fp.find_last_of(L"\\/");
        m_fileName = (p != std::wstring::npos) ? fp.substr(p + 1) : fp;

        m_cursorPos = 0;
        m_nibblePos = 0;
        m_bitPos = 7;
        m_findResults.clear();
        m_currentFindIndex = -1;

        if (m_currentPage == 0) m_hexView.SetCursorPos(0);
        else m_binView.SetCursorPos(0);
        UpdateTitle();
        UpdateStatusBar();
    }

    /**
     * 保存文件
     */
    void SaveFile() {
        if (m_filePath.empty()) {
            SaveFileAs();
            return;
        }
        std::ofstream f(m_filePath, std::ios::binary);
        if (!f.is_open()) {
            MessageBox(m_hWnd, L"无法保存！", L"错误", MB_ICONERROR);
            return;
        }
        f.write((const char*)m_data.GetData().data(), m_data.GetSize());
        f.close();
        m_data.ClearModified();
        UpdateTitle();
    }

    /**
     * 另存为
     */
    void SaveFileAs() {
        WCHAR fp[MAX_PATH] = { 0 };
        wcscpy_s(fp, MAX_PATH, m_filePath.c_str());
        OPENFILENAME of = { 0 };
        of.lStructSize = sizeof(of);
        of.hwndOwner = m_hWnd;
        of.lpstrFile = fp;
        of.nMaxFile = MAX_PATH;
        of.lpstrFilter = L"所有文件(*.*)\0*.*\0\0";
        of.nFilterIndex = 1;
        of.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        if (GetSaveFileName(&of)) {
            m_filePath = fp;
            size_t p = m_filePath.find_last_of(L"\\/");
            m_fileName = (p != std::wstring::npos) ? m_filePath.substr(p + 1) : m_filePath;
            SaveFile();
        }
    }

    /**
     * 关闭文件
     */
    void CloseFile() {
        if (m_data.IsModified()) {
            int r = MessageBox(m_hWnd, L"关闭前是否保存？", L"关闭",
                MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDYES) SaveFile();
            else if (r == IDCANCEL) return;
        }
        std::vector<unsigned char> ed;
        m_data.SetData(ed);
        m_filePath.clear();
        m_fileName.clear();
        m_cursorPos = 0;
        UpdateTitle();
        UpdateStatusBar();
    }

    // ============ 导出功能 ============

    void ExportAsC() {
        std::string code = m_data.ExportAsC();
        if (!code.empty() && OpenClipboard(m_hWnd)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, code.length() + 1);
            if (hg) {
                char* p = (char*)GlobalLock(hg);
                strcpy_s(p, code.length() + 1, code.c_str());
                GlobalUnlock(hg);
                SetClipboardData(CF_TEXT, hg);
            }
            CloseClipboard();
            MessageBox(m_hWnd, L"C数组已复制到剪贴板！", L"导出", MB_OK | MB_ICONINFORMATION);
        }
    }

    void ExportAsPython() {
        std::string code = m_data.ExportAsPy();
        if (!code.empty() && OpenClipboard(m_hWnd)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, code.length() + 1);
            if (hg) {
                char* p = (char*)GlobalLock(hg);
                strcpy_s(p, code.length() + 1, code.c_str());
                GlobalUnlock(hg);
                SetClipboardData(CF_TEXT, hg);
            }
            CloseClipboard();
            MessageBox(m_hWnd, L"Python字节数组已复制到剪贴板！", L"导出", MB_OK | MB_ICONINFORMATION);
        }
    }

    void ExportAsJSON() {
        std::string code = m_data.ExportAsJSON();
        if (!code.empty() && OpenClipboard(m_hWnd)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, code.length() + 1);
            if (hg) {
                char* p = (char*)GlobalLock(hg);
                strcpy_s(p, code.length() + 1, code.c_str());
                GlobalUnlock(hg);
                SetClipboardData(CF_TEXT, hg);
            }
            CloseClipboard();
            MessageBox(m_hWnd, L"JSON已复制到剪贴板！", L"导出", MB_OK | MB_ICONINFORMATION);
        }
    }

    // ============ 编辑操作 ============

    void Undo() {
        if (!m_data.CanUndo()) return;
        DataManager::EditAction a = m_data.PopUndo();
        DataManager::EditAction ra;
        ra.offset = a.offset;
        ra.isInsert = !a.isInsert;

        if (a.isInsert) {
            ra.oldData.assign(m_data.GetData().begin() + a.offset,
                m_data.GetData().begin() + a.offset + a.newData.size());
            ra.newData = a.newData;
            m_data.DeleteBytes(a.offset, (int)a.newData.size());
        }
        else {
            ra.oldData = a.oldData;
            ra.newData = a.newData;
            for (size_t i = 0; i < a.oldData.size(); i++) {
                m_data.SetByte(a.offset + (int)i, a.oldData[i]);
            }
        }
        m_data.PushRedo(ra);
        m_cursorPos = a.offset;
        m_nibblePos = 0;
        m_bitPos = 7;
        if (m_currentPage == 0) m_hexView.SetCursorPos(a.offset);
        else m_binView.SetCursorPos(a.offset);
        UpdateStatusBar();
    }

    void Redo() {
        if (!m_data.CanRedo()) return;
        DataManager::EditAction a = m_data.PopRedo();
        DataManager::EditAction ua;
        ua.offset = a.offset;
        ua.isInsert = !a.isInsert;

        if (a.isInsert) {
            ua.oldData.assign(m_data.GetData().begin() + a.offset,
                m_data.GetData().begin() + a.offset + a.newData.size());
            ua.newData = a.newData;
            m_data.DeleteBytes(a.offset, (int)a.newData.size());
        }
        else {
            ua.oldData = a.oldData;
            ua.newData = a.newData;
            for (size_t i = 0; i < a.oldData.size(); i++) {
                m_data.SetByte(a.offset + (int)i, a.oldData[i]);
            }
        }
        m_data.PushUndo(ua);
        m_cursorPos = a.offset;
        m_nibblePos = 0;
        m_bitPos = 7;
        if (m_currentPage == 0) m_hexView.SetCursorPos(a.offset);
        else m_binView.SetCursorPos(a.offset);
        UpdateStatusBar();
    }

    void Cut() { Copy(); DeleteCurrent(); }

    void Copy() {
        if (m_currentPage == 0) {
            if (GetKeyState(VK_CONTROL) & 0x8000 && GetKeyState(VK_SHIFT) & 0x8000)
                m_hexView.CopyAsTextToClipboard(m_hWnd);
            else
                m_hexView.CopyToClipboard(m_hWnd);
        }
    }

    void Paste() {
        if (m_currentPage == 0) {
            m_hexView.PasteFromClipboard(m_hWnd);
            UpdateStatusBar();
        }
    }

    void DeleteCurrent() {
        if (m_data.GetSize() == 0 || m_cursorPos >= (int)m_data.GetSize()) return;
        DataManager::EditAction a;
        a.offset = m_cursorPos;
        a.isInsert = true;
        a.oldData.clear();
        a.newData.push_back(m_data.GetByte(m_cursorPos));
        m_data.PushUndo(a);
        m_data.DeleteByte(m_cursorPos);
        if (m_cursorPos >= (int)m_data.GetSize() && m_data.GetSize() > 0)
            m_cursorPos = (int)m_data.GetSize() - 1;
        if (m_currentPage == 0) m_hexView.SetCursorPos(m_cursorPos);
        else m_binView.SetCursorPos(m_cursorPos);
        UpdateStatusBar();
    }

    void SelectAll() {
        if (m_currentPage == 0) m_hexView.SelectAll();
    }

    void DuplicateLine() {
        if (!m_data.GetSize()) return;
        int ls = (m_cursorPos / 16) * 16;
        int le = min(ls + 15, (int)m_data.GetSize() - 1);
        std::vector<unsigned char> dup(m_data.GetData().begin() + ls,
            m_data.GetData().begin() + le + 1);
        DataManager::EditAction a;
        a.offset = le + 1;
        a.isInsert = true;
        a.oldData.clear();
        a.newData = dup;
        m_data.PushUndo(a);
        m_data.InsertBytes(le + 1, dup);
        UpdateStatusBar();
    }

    // ============ 查找功能 ============

    bool Find(const std::vector<unsigned char>& pat) {
        m_findPattern = pat;
        m_findResults.clear();
        m_currentFindIndex = -1;
        if (pat.empty() || m_data.GetSize() == 0) return false;

        for (size_t i = 0; i <= m_data.GetSize() - pat.size(); i++) {
            bool ok = true;
            for (size_t j = 0; j < pat.size(); j++) {
                if (m_data.GetByte((int)(i + j)) != pat[j]) { ok = false; break; }
            }
            if (ok) m_findResults.push_back((int)i);
        }

        if (!m_findResults.empty()) {
            m_currentFindIndex = 0;
            m_cursorPos = m_findResults[0];
            m_nibblePos = 0;
            m_bitPos = 7;
            if (m_currentPage == 0) m_hexView.SetCursorPos(m_cursorPos);
            else m_binView.SetCursorPos(m_cursorPos);
            UpdateStatusBar();
            return true;
        }
        return false;
    }

    bool FindNext() {
        if (m_findResults.empty()) return false;
        m_currentFindIndex = (m_currentFindIndex + 1) % (int)m_findResults.size();
        m_cursorPos = m_findResults[m_currentFindIndex];
        m_nibblePos = 0;
        m_bitPos = 7;
        if (m_currentPage == 0) m_hexView.SetCursorPos(m_cursorPos);
        else m_binView.SetCursorPos(m_cursorPos);
        UpdateStatusBar();
        return true;
    }

    bool FindPrev() {
        if (m_findResults.empty()) return false;
        m_currentFindIndex--;
        if (m_currentFindIndex < 0) m_currentFindIndex = (int)m_findResults.size() - 1;
        m_cursorPos = m_findResults[m_currentFindIndex];
        m_nibblePos = 0;
        m_bitPos = 7;
        if (m_currentPage == 0) m_hexView.SetCursorPos(m_cursorPos);
        else m_binView.SetCursorPos(m_cursorPos);
        UpdateStatusBar();
        return true;
    }

    void ReplaceAll() {
        if (!m_findPattern.empty()) {
            DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_INPUT), m_hWnd, InputDlgProc);
        }
    }

    // ============ 对话框 ============

    void ShowFindDialog() {
        DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_FIND), m_hWnd, FindDlgProc);
    }
    void ShowReplaceDialog() {
        DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_REPLACE), m_hWnd, ReplaceDlgProc);
    }
    void ShowGotoDialog() {
        DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_GOTO), m_hWnd, GotoDlgProc);
    }
    void ShowBytesPerRowDialog() {
        DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_INPUT), m_hWnd, BytesPerRowDlgProc);
    }
    void ShowHashDialog() {
        MessageBox(m_hWnd, L"哈希功能即将推出！", L"哈希", MB_OK);
    }

    void ShowStatsDialog() {
        WCHAR msg[256];
        swprintf_s(msg, 256, L"文件大小：%zu 字节\n总行数：%d",
            m_data.GetSize(), (int)(m_data.GetSize() + 15) / 16);
        MessageBox(m_hWnd, msg, L"统计信息", MB_OK);
    }

    // ============ 更新界面 ============

    void UpdateTitle() {
        std::wstring t = m_fileName.empty() ? L"未命名" : m_fileName;
        if (m_data.IsModified()) t += L" *";
        t += L" - BinaryHexEditor Pro";
        SetWindowText(m_hWnd, t.c_str());
    }

    void UpdateStatusBar() {
        if (!m_hStatusBar) return;
        WCHAR tx[256];
        wcscpy_s(tx, 256, m_currentPage == 0 ? L"十六进制 + UTF-8" : L"二进制");
        SendMessage(m_hStatusBar, SB_SETTEXT, 0, (LPARAM)tx);
        swprintf_s(tx, 256, L"偏移: 0x%08X (%d)", m_cursorPos, m_cursorPos);
        SendMessage(m_hStatusBar, SB_SETTEXT, 1, (LPARAM)tx);
        swprintf_s(tx, 256, L"大小: %zu 字节", m_data.GetSize());
        SendMessage(m_hStatusBar, SB_SETTEXT, 2, (LPARAM)tx);
        swprintf_s(tx, 256, L"缩放: %d%%", m_zoom.GetZoomPercentage());
        SendMessage(m_hStatusBar, SB_SETTEXT, 3, (LPARAM)tx);
        InvalidateRect(m_hStatusBar, NULL, TRUE);
    }

    // ============ 工具函数 ============

    static std::vector<unsigned char> ParseHexString(const std::wstring& s) {
        std::vector<unsigned char> r;
        std::wstring cl;
        for (WCHAR c : s) if (c != L' ') cl += c;
        if (cl.empty()) return r;

        bool hx = true;
        for (WCHAR c : cl) {
            if (!((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F') ||
                (c >= L'a' && c <= L'f'))) {
                hx = false;
                break;
            }
        }

        if (hx && cl.length() % 2 == 0) {
            for (size_t i = 0; i < cl.length(); i += 2) {
                WCHAR h[3] = { cl[i], cl[i + 1], 0 };
                r.push_back((unsigned char)wcstol(h, NULL, 16));
            }
        }
        else {
            r = EncodeUTF8(s);
        }
        return r;
    }

    // ============ 对话框过程 ============

    static INT_PTR CALLBACK FindDlgProc(HWND d, UINT m, WPARAM w, LPARAM) {
        static MainWindow* p = NULL;
        if (m == WM_INITDIALOG) {
            p = (MainWindow*)GetWindowLongPtr(GetParent(d), GWLP_USERDATA);
            return TRUE;
        }
        if (m == WM_COMMAND && LOWORD(w) == IDC_BTN_FIND_NEXT) {
            WCHAR ft[256] = { 0 };
            GetDlgItemText(d, IDC_EDIT_FIND, ft, 256);
            if (wcslen(ft) > 0 && p && !p->Find(ParseHexString(ft)))
                MessageBox(d, L"未找到！", L"查找", MB_OK | MB_ICONINFORMATION);
        }
        if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) EndDialog(d, 0);
        return FALSE;
    }

    static INT_PTR CALLBACK ReplaceDlgProc(HWND d, UINT m, WPARAM w, LPARAM) {
        static MainWindow* p = NULL;
        if (m == WM_INITDIALOG) {
            p = (MainWindow*)GetWindowLongPtr(GetParent(d), GWLP_USERDATA);
            return TRUE;
        }
        if (m == WM_COMMAND) {
            switch (LOWORD(w)) {
            case IDC_BTN_FIND_NEXT: {
                WCHAR ft[256] = { 0 };
                GetDlgItemText(d, IDC_EDIT_FIND, ft, 256);
                if (wcslen(ft) > 0 && p) p->Find(ParseHexString(ft));
                break;
            }
            case IDC_BTN_REPLACE:
                if (p && p->m_currentFindIndex >= 0 &&
                    p->m_currentFindIndex < (int)p->m_findResults.size()) {
                    WCHAR rt[256] = { 0 };
                    GetDlgItemText(d, IDC_EDIT_REPLACE, rt, 256);
                    auto rp = ParseHexString(rt);
                    int off = p->m_findResults[p->m_currentFindIndex];

                    DataManager::EditAction a;
                    a.offset = off;
                    a.isInsert = false;
                    for (size_t i = 0; i < p->m_findPattern.size(); i++)
                        a.oldData.push_back(p->m_data.GetByte(off + (int)i));
                    a.newData = rp;

                    p->m_data.PushUndo(a);
                    p->m_data.DeleteBytes(off, (int)p->m_findPattern.size());
                    p->m_data.InsertBytes(off, rp);
                    p->Find(p->m_findPattern);
                }
                break;
            case IDC_BTN_REPLACE_ALL:
                if (p) {
                    WCHAR ft[256] = { 0 }, rt[256] = { 0 };
                    GetDlgItemText(d, IDC_EDIT_FIND, ft, 256);
                    GetDlgItemText(d, IDC_EDIT_REPLACE, rt, 256);
                    auto pt = ParseHexString(ft), rp = ParseHexString(rt);

                    if (!pt.empty()) {
                        DataManager::EditAction a;
                        a.offset = 0;
                        a.isInsert = false;
                        a.oldData = p->m_data.GetData();
                        std::vector<unsigned char> nd;
                        size_t i = 0;

                        while (i < p->m_data.GetSize()) {
                            if (i <= p->m_data.GetSize() - pt.size()) {
                                bool ok = true;
                                for (size_t j = 0; j < pt.size(); j++) {
                                    if (p->m_data.GetByte((int)(i + j)) != pt[j]) {
                                        ok = false;
                                        break;
                                    }
                                }
                                if (ok) {
                                    nd.insert(nd.end(), rp.begin(), rp.end());
                                    i += pt.size();
                                    continue;
                                }
                            }
                            nd.push_back(p->m_data.GetByte((int)i));
                            i++;
                        }
                        a.newData = nd;
                        p->m_data.PushUndo(a);
                        p->m_data.SetData(nd);
                        p->m_cursorPos = 0;
                        p->m_nibblePos = 0;
                        p->m_bitPos = 7;
                        if (p->m_currentPage == 0) p->m_hexView.SetCursorPos(0);
                        else p->m_binView.SetCursorPos(0);
                    }
                }
                break;
            case IDCANCEL: EndDialog(d, 0); break;
            }
        }
        return FALSE;
    }

    static INT_PTR CALLBACK GotoDlgProc(HWND d, UINT m, WPARAM w, LPARAM) {
        static MainWindow* p = NULL;
        if (m == WM_INITDIALOG) {
            p = (MainWindow*)GetWindowLongPtr(GetParent(d), GWLP_USERDATA);
            return TRUE;
        }
        if (m == WM_COMMAND && LOWORD(w) == IDOK) {
            WCHAR ot[64] = { 0 };
            GetDlgItemText(d, IDC_EDIT_OFFSET, ot, 64);
            int off = IsDlgButtonChecked(d, IDC_RADIO_HEX) ?
                wcstol(ot, NULL, 16) : _wtoi(ot);
            if (p) {
                int mo = max(0, (int)p->m_data.GetSize());
                p->m_cursorPos = max(0, min(off, mo));
                p->m_nibblePos = 0;
                p->m_bitPos = 7;
                if (p->m_currentPage == 0) p->m_hexView.SetCursorPos(p->m_cursorPos);
                else p->m_binView.SetCursorPos(p->m_cursorPos);
                p->UpdateStatusBar();
            }
            EndDialog(d, IDOK);
        }
        if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) EndDialog(d, 0);
        return FALSE;
    }

    static INT_PTR CALLBACK BytesPerRowDlgProc(HWND d, UINT m, WPARAM w, LPARAM) {
        if (m == WM_COMMAND && LOWORD(w) == IDOK) { EndDialog(d, IDOK); }
        if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) EndDialog(d, 0);
        return FALSE;
    }

    static INT_PTR CALLBACK InputDlgProc(HWND d, UINT m, WPARAM w, LPARAM) {
        if (m == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL))
            EndDialog(d, LOWORD(w));
        return FALSE;
    }

    static INT_PTR CALLBACK AboutDlgProc(HWND d, UINT m, WPARAM w, LPARAM) {
        if (m == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL))
            EndDialog(d, 0);
        return FALSE;
    }
};

// ============ 程序入口点 ============
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    // 初始化公共控件
    INITCOMMONCONTROLSEX icex = { sizeof(icex) };
    icex.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // 初始化GDI+
    GdiplusStartupInput gsi;
    ULONG_PTR gt;
    GdiplusStartup(&gt, &gsi, NULL);

    // 创建并运行主窗口
    MainWindow mw;
    if (!mw.Create(hInstance, nCmdShow)) {
        MessageBox(NULL, L"失败！", L"错误", MB_ICONERROR);
        return 1;
    }
    mw.Run();

    // 关闭GDI+
    GdiplusShutdown(gt);
    return 0;
}