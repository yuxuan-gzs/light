// PluginCenter.h
// 现代化插件中心（设置面板内嵌控件）
// 统一管理：内置功能（光语 AI）、JS 扩展、api_*.dll 指令集、外置 GUI 插件。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <functional>

struct PluginCenterItem {
    std::wstring id;
    std::wstring name;
    std::wstring version;
    std::wstring author;
    std::wstring description;
    std::wstring path;
    std::wstring kind;          // 内置功能 / JS 扩展 / API 指令集 / GUI 插件
    bool enabled = true;
    bool builtin = false;
    bool canToggle = true;
    bool canOpenFolder = true;
    bool canReload = true;
};

class PluginCenterPanel {
public:
    using ToggleCallback =
        std::function<void(const std::wstring& id, bool enabled)>;
    using ActionCallback =
        std::function<void(const std::wstring& id, const std::wstring& action)>;

    PluginCenterPanel();
    ~PluginCenterPanel();

    bool Create(HWND parent, HINSTANCE instance, int controlId);
    HWND Hwnd() const { return m_hWnd; }
    void Destroy();

    void SetItems(std::vector<PluginCenterItem> items);
    void SetToggleCallback(ToggleCallback cb) { m_onToggle = std::move(cb); }
    void SetActionCallback(ActionCallback cb) { m_onAction = std::move(cb); }

    void Move(int x, int y, int width, int height);
    void Show(bool show);
    void Refresh();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static void EnsureClass(HINSTANCE instance);

    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    void Paint(HDC hdc);
    void DrawCard(void* graphics, const PluginCenterItem& item,
        int index, int y);
    void DrawScrollbar(void* graphics);
    int ItemAt(int x, int y) const;
    int ButtonAt(int x, int y, int& itemIndex) const;
    int ContentHeight() const;
    int VisibleHeight() const;
    void ClampScroll();
    void UpdateScrollInfo();
    void InvalidateAll();
    void NotifyToggle(int index);
    void NotifyAction(int index, const wchar_t* action);
    void ShowContextMenu(int index, POINT pt);
    void UpdateHover(int x, int y);
    void HandleClick(int x, int y);
    void ScrollBy(int delta);

    HWND m_hWnd = nullptr;
    HWND m_hParent = nullptr;
    HINSTANCE m_hInstance = nullptr;
    int m_controlId = 0;

    std::vector<PluginCenterItem> m_items;
    ToggleCallback m_onToggle;
    ActionCallback m_onAction;

    int m_scrollY = 0;
    int m_hoverItem = -1;
    int m_hoverButton = -1;   // 0=toggle 1=open 2=reload
    int m_pressedItem = -1;
    int m_pressedButton = -1;
    bool m_trackingLeave = false;
};
