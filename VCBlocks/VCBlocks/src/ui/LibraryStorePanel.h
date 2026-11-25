// LibraryStorePanel.h
// 库商城面板：模态窗口，展示远程库列表并支持下载到本地语言目录。
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "core/LibraryStore.h"

namespace vcb {

class LibraryStorePanel {
public:
    // 模态显示库商城窗口；owner 为父窗口
    void ShowModal(HINSTANCE inst, HWND owner);

private:
    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    HWND m_status = nullptr;
    std::vector<StoreLibrary> m_libs;
    std::string m_indexUrl;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    void Refresh();
    void DownloadSelected();
    void Layout(int w, int h);
    void SetStatus(const std::wstring& s);
};

} // namespace vcb
