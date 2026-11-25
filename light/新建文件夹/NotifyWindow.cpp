#include "NotifyWindow.h"
#include <sstream>
#include <iomanip>

NotifyWindow* NotifyWindow::instance = nullptr;

// ============== 构造函数/析构函数 ==============

NotifyWindow::NotifyWindow()
    : hWnd(nullptr)
    , isVisible(false)
    , consecutiveBlocks(0)
    , batchMode(false)
    , maxQueueSize(10)
    , displayDuration(3000)
    , threadRunning(false)
    , lastShowTime(std::chrono::steady_clock::now()) {
    CreateNotifyWindow();
}

NotifyWindow::~NotifyWindow() {
    threadRunning = false;
    if (hWnd) {
        PostMessageW(hWnd, WM_CLOSE, 0, 0);
        PostMessageW(hWnd, WM_QUIT, 0, 0);
    }
    if (messageThread.joinable()) {
        messageThread.join();
    }
    if (hWnd) {
        DestroyWindow(hWnd);
        hWnd = nullptr;
    }
}

// ============== 单例 ==============

NotifyWindow* NotifyWindow::GetInstance() {
    if (!instance) {
        instance = new NotifyWindow();
    }
    return instance;
}

// ============== 消息循环线程 ==============

void NotifyWindow::MessageLoop() {
    threadRunning = true;
    MSG msg;
    while (threadRunning) {
        // 使用 PeekMessage 避免阻塞
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else {
            // 没有消息时短暂休眠，降低CPU占用
            Sleep(10);
        }
    }
    threadRunning = false;
}

// ============== 创建窗口 ==============

void NotifyWindow::CreateNotifyWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"LightNotifyWindow";
    wc.hIcon = LoadIcon(NULL, IDI_SHIELD);

    // 检查是否已注册
    WNDCLASSEXW existing;
    if (!GetClassInfoExW(GetModuleHandle(NULL), L"LightNotifyWindow", &existing)) {
        RegisterClassExW(&wc);
    }

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    hWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"LightNotifyWindow",
        L"安全拦截",
        WS_POPUP,
        screenWidth - 420,
        screenHeight - 120,
        400,
        90,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (hWnd) {
        SetLayeredWindowAttributes(hWnd, 0, 220, LWA_ALPHA);
        ::ShowWindow(hWnd, SW_HIDE);

        // ====== 启动消息循环线程 ======
        messageThread = std::thread(&NotifyWindow::MessageLoop, this);
    }
}

// ============== 显示通知（线程安全） ==============

void NotifyWindow::ShowBlockNotification(const std::string& processName, const std::string& domain) {
    ShowBlockNotificationEx(processName, domain, "", 0);
}

void NotifyWindow::ShowBlockNotificationEx(const std::string& processName,
    const std::string& domain,
    const std::string& ipAddress,
    uint16_t port) {
    std::lock_guard<std::mutex> lock(mtx);

    if (msgQueue.size() >= maxQueueSize) {
        msgQueue.pop();
    }

    NotificationMsg msg;
    msg.processName = processName;
    msg.domain = domain;
    msg.ipAddress = ipAddress;
    msg.port = port;
    msg.timestamp = GetTickCount64();

    msgQueue.push(msg);

    if (batchMode) {
        pendingBatch.push(msg);
        return;
    }

    // ====== 发送消息到窗口线程 ======
    if (hWnd) {
        PostMessageW(hWnd, WM_USER + 100, 0, 0);
    }
}

// ============== 批量模式控制 ==============

void NotifyWindow::SetBatchMode(bool enabled) {
    std::lock_guard<std::mutex> lock(mtx);
    batchMode = enabled;
    if (!enabled && !pendingBatch.empty()) {
        FlushNotifications();
    }
}

void NotifyWindow::FlushNotifications() {
    std::lock_guard<std::mutex> lock(mtx);

    if (pendingBatch.empty()) return;

    NotificationMsg msg = pendingBatch.back();
    while (!pendingBatch.empty()) {
        pendingBatch.pop();
    }

    if (msgQueue.size() >= maxQueueSize) {
        msgQueue.pop();
    }
    msgQueue.push(msg);

    if (hWnd) {
        PostMessageW(hWnd, WM_USER + 100, 0, 0);
    }
}

// ============== 配置 ==============

void NotifyWindow::SetDisplayDuration(uint32_t milliseconds) {
    displayDuration = milliseconds;
}

void NotifyWindow::SetMaxQueueSize(size_t maxSize) {
    maxQueueSize = maxSize;
}

// ============== 主线程处理 ==============

void NotifyWindow::ProcessPendingMessages() {
    // 消息循环线程会自动处理，此方法保留为空
    // 但为了兼容，保留调用
}

// ============== 更新窗口内容 ==============

void NotifyWindow::UpdateWindowContent(const NotificationMsg& msg) {
    std::wstring displayText = FormatNotification(msg);
    SetWindowTextW(hWnd, displayText.c_str());
    InvalidateRect(hWnd, NULL, TRUE);
}

std::wstring NotifyWindow::FormatNotification(const NotificationMsg& msg) {
    std::wstringstream ss;

    std::string procName = msg.processName;
    if (procName.length() > 20) {
        procName = procName.substr(0, 18) + "...";
    }

    std::string domain = msg.domain;
    if (domain.length() > 35) {
        domain = domain.substr(0, 33) + "...";
    }

    std::wstring wProcName(procName.begin(), procName.end());
    std::wstring wDomain(domain.begin(), domain.end());

    ss << L"🛡 " << wProcName << L"  →  " << wDomain;

    if (!msg.ipAddress.empty()) {
        std::wstring wIP(msg.ipAddress.begin(), msg.ipAddress.end());
        ss << L"\n   📍 " << wIP;
        if (msg.port > 0) {
            ss << L":" << msg.port;
        }
    }

    return ss.str();
}

// ============== 窗口过程 ==============

LRESULT CALLBACK NotifyWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NotifyWindow* pThis = GetInstance();

    switch (msg) {
    case WM_USER + 100:
        if (pThis && pThis->hWnd == hWnd) {
            std::lock_guard<std::mutex> lock(pThis->mtx);
            if (!pThis->msgQueue.empty()) {
                NotificationMsg notifyMsg = pThis->msgQueue.back();
                while (!pThis->msgQueue.empty()) {
                    pThis->msgQueue.pop();
                }
                pThis->UpdateWindowContent(notifyMsg);
                if (!pThis->isVisible) {
                    ::ShowWindow(hWnd, SW_SHOW);
                    pThis->isVisible = true;
                    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    SetLayeredWindowAttributes(hWnd, 0, 220, LWA_ALPHA);
                }
                KillTimer(hWnd, 1);
                SetTimer(hWnd, 1, pThis->displayDuration, NULL);
            }
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rect;
        GetClientRect(hWnd, &rect);

        HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 35));
        FillRect(hdc, &rect, bgBrush);
        DeleteObject(bgBrush);

        RECT highlightRect = { 0, 0, 4, rect.bottom };
        HBRUSH highlightBrush = CreateSolidBrush(RGB(255, 60, 60));
        FillRect(hdc, &highlightRect, highlightBrush);
        DeleteObject(highlightBrush);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 70));
        SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, rect.right - 1, rect.bottom - 1);
        DeleteObject(pen);

        wchar_t text[512] = { 0 };
        GetWindowTextW(hWnd, text, 512);

        SetTextColor(hdc, RGB(230, 230, 240));
        SetBkMode(hdc, TRANSPARENT);

        HFONT hFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SelectObject(hdc, hFont);

        RECT textRect = { 15, 8, rect.right - 10, rect.bottom - 5 };
        DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_WORDBREAK | DT_VCENTER);

        DeleteObject(hFont);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_TIMER:
        if (wParam == 1 && pThis) {
            pThis->HideWindow();
            KillTimer(hWnd, 1);
        }
        break;

    case WM_LBUTTONDOWN:
        KillTimer(hWnd, 1);
        SetTimer(hWnd, 1, 5000, NULL);
        break;

    case WM_RBUTTONDOWN:
        if (pThis) {
            pThis->HideWindow();
        }
        KillTimer(hWnd, 1);
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && pThis) {
            pThis->HideWindow();
            KillTimer(hWnd, 1);
        }
        break;

    case WM_CLOSE:
        if (pThis) {
            pThis->HideWindow();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============== 显示/隐藏 ==============

void NotifyWindow::HideWindow() {
    if (hWnd && isVisible) {
        ::ShowWindow(hWnd, SW_HIDE);
        isVisible = false;
        KillTimer(hWnd, 1);
    }
}

void NotifyWindow::ShowWindowNow() {
    if (hWnd && !isVisible) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!msgQueue.empty()) {
            PostMessageW(hWnd, WM_USER + 100, 0, 0);
        }
    }
}

// ============== 全局变量 ==============

//NotifyWindow* g_NotifyWindow = nullptr;