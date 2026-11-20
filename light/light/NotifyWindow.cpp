// NotifyWindow.cpp
#include "NotifyWindow.h"
#include <sstream>
#include <iomanip>
#include <cmath>

NotifyWindow* NotifyWindow::instance = nullptr;

NotifyWindow::NotifyWindow()
    : hWnd(nullptr)
    , isVisible(false)
    , batchMode(false)
    , maxQueueSize(10)
    , displayDuration(3000)
    , threadRunning(false)
    , slideOffset(0)
    , animDirection(0)
    , slideStartX(0)
    , slideStartY(0)
    , slideEndX(0)
    , slideEndY(0) {

    threadRunning = true;
    messageThread = std::thread(&NotifyWindow::MessageLoop, this);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
}

NotifyWindow* NotifyWindow::GetInstance() {
    if (!instance) {
        instance = new NotifyWindow();
        g_NotifyWindow = instance;
    }
    return instance;
}

void NotifyWindow::MessageLoop() {
    CreateNotifyWindow();

    MSG msg;
    while (threadRunning) {
        if (GetMessageW(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else {
            break;
        }
    }

    if (hWnd) {
        DestroyWindow(hWnd);
        hWnd = nullptr;
    }
}

void NotifyWindow::CreateNotifyWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"LightNotifyWindow";
    wc.hIcon = LoadIcon(NULL, IDI_SHIELD);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int winWidth = 340;
    int winHeight = 80;

    slideEndX = screenWidth - winWidth;
    slideEndY = screenHeight - winHeight - 50;
    slideStartX = screenWidth + 10;
    slideStartY = slideEndY;

    hWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"LightNotifyWindow",
        L"安全核心",
        WS_POPUP,
        slideStartX,
        slideStartY,
        winWidth,
        winHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (hWnd) {
        SetLayeredWindowAttributes(hWnd, 0, 220, LWA_ALPHA);
        ::ShowWindow(hWnd, SW_HIDE);
    }
}

void NotifyWindow::ShowBlockNotification(const std::string& processName, const std::string& domain) {
    std::lock_guard<std::mutex> lock(mtx);

    NotificationMsg msg;
    msg.processName = processName;
    msg.domain = domain;
    msg.ipAddress = "";
    msg.port = 0;
    msg.timestamp = GetTickCount64();

    if (hWnd) {
        NotificationMsg* pMsg = new NotificationMsg(msg);
        PostMessageW(hWnd, WM_USER + 100, (WPARAM)pMsg, 0);
    }
}

void NotifyWindow::StartSlideIn() {
    if (!hWnd) return;

    // 设置窗口位置到屏幕外
    SetWindowPos(hWnd, HWND_TOPMOST, slideStartX, slideStartY, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

    SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
    ::ShowWindow(hWnd, SW_SHOW);
    isVisible = true;

    slideOffset = 0;
    animDirection = 1;

    KillTimer(hWnd, 2);
    SetTimer(hWnd, 2, 16, NULL);
}

void NotifyWindow::StartSlideOut() {
    if (!hWnd || !isVisible) return;

    // 获取当前位置
    RECT rect;
    GetWindowRect(hWnd, &rect);
    slideStartX = rect.left;
    slideStartY = rect.top;

    slideOffset = 0;
    animDirection = -1;

    KillTimer(hWnd, 2);
    SetTimer(hWnd, 2, 16, NULL);
}

void NotifyWindow::UpdateAnimation() {
    if (!hWnd || !isVisible) {
        KillTimer(hWnd, 2);
        return;
    }

    const int SLIDE_SPEED = 15;

    if (animDirection == 1) {
        // 滑入：从右往左
        slideOffset += SLIDE_SPEED;
        int currentX = slideStartX - slideOffset;
        int currentY = slideStartY;

        if (currentX <= slideEndX) {
            currentX = slideEndX;
            slideOffset = slideStartX - slideEndX;
            SetLayeredWindowAttributes(hWnd, 0, 220, LWA_ALPHA);
            SetWindowPos(hWnd, HWND_TOPMOST, currentX, currentY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            KillTimer(hWnd, 2);
            SetTimer(hWnd, 1, displayDuration, NULL);
            return;
        }

        int progress = (int)((float)slideOffset / (slideStartX - slideEndX) * 220);
        if (progress > 220) progress = 220;
        SetLayeredWindowAttributes(hWnd, 0, progress, LWA_ALPHA);
        SetWindowPos(hWnd, HWND_TOPMOST, currentX, currentY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    else if (animDirection == -1) {
        // 滑出：从左往右
        slideOffset += SLIDE_SPEED;
        int currentX = slideStartX + slideOffset;
        int currentY = slideStartY;

        int maxOffset = slideEndX + 340;  // 滑出屏幕
        if (currentX >= maxOffset) {
            ::ShowWindow(hWnd, SW_HIDE);
            isVisible = false;
            slideOffset = 0;
            KillTimer(hWnd, 2);
            return;
        }

        int progress = 220 - (int)((float)slideOffset / 340 * 220);
        if (progress < 0) progress = 0;
        SetLayeredWindowAttributes(hWnd, 0, progress, LWA_ALPHA);
        SetWindowPos(hWnd, HWND_TOPMOST, currentX, currentY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

void NotifyWindow::ShowNotificationWindow() {
    if (!hWnd) return;
    StartSlideIn();
}

void NotifyWindow::HideNotificationWindow() {
    if (!hWnd) return;
    StartSlideOut();
}

std::wstring NotifyWindow::StringToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &result[0], len);
    return result;
}

LRESULT CALLBACK NotifyWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NotifyWindow* pThis = g_NotifyWindow;

    switch (msg) {
    case WM_USER + 100:
        if (pThis && wParam) {
            NotificationMsg* notifyMsg = (NotificationMsg*)wParam;

            std::wstring wDomain = pThis->StringToWide(notifyMsg->domain);
            std::wstring displayText = L"[安全核心]拦截" + wDomain;

            SetWindowTextW(hWnd, displayText.c_str());

            pThis->ShowNotificationWindow();

            delete notifyMsg;
        }
        return 0;

    case WM_TIMER:
        if (wParam == 1 && pThis) {
            pThis->StartSlideOut();
            KillTimer(hWnd, 1);
        }
        else if (wParam == 2 && pThis) {
            pThis->UpdateAnimation();
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rect;
        GetClientRect(hWnd, &rect);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 35));
        FillRect(memDC, &rect, bgBrush);
        DeleteObject(bgBrush);

        RECT highlightRect = { 0, 0, 4, rect.bottom };
        HBRUSH highlightBrush = CreateSolidBrush(RGB(255, 60, 60));
        FillRect(memDC, &highlightRect, highlightBrush);
        DeleteObject(highlightBrush);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 70));
        HPEN oldPen = (HPEN)SelectObject(memDC, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
        Rectangle(memDC, 0, 0, rect.right - 1, rect.bottom - 1);
        SelectObject(memDC, oldPen);
        SelectObject(memDC, oldBrush);
        DeleteObject(pen);

        wchar_t text[512] = { 0 };
        GetWindowTextW(hWnd, text, 512);

        SetTextColor(memDC, RGB(230, 230, 240));
        SetBkMode(memDC, TRANSPARENT);

        HFONT hFont = CreateFontW(
            16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"SimSun"
        );
        HFONT oldFont = (HFONT)SelectObject(memDC, hFont);

        RECT titleRect = { 15, 8, rect.right - 10, 30 };
        DrawTextW(memDC, L"安全核心", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT textRect = { 0, 35, rect.right, rect.bottom - 5 };
        DrawTextW(memDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(memDC, oldFont);
        DeleteObject(hFont);

        BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (pThis) {
            pThis->HideNotificationWindow();
        }
        return 0;

    case WM_RBUTTONDOWN:
        if (pThis) {
            pThis->HideNotificationWindow();
        }
        return 0;

    case WM_NCHITTEST:
        return HTCAPTION;

    case WM_CLOSE:
        if (pThis) {
            pThis->HideNotificationWindow();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void NotifyWindow::ShowBlockNotificationEx(const std::string& processName,
    const std::string& domain,
    const std::string& ipAddress,
    uint16_t port) {
    std::lock_guard<std::mutex> lock(mtx);

    NotificationMsg* msg = new NotificationMsg();
    msg->processName = processName;
    msg->domain = domain;
    msg->ipAddress = ipAddress;
    msg->port = port;
    msg->timestamp = GetTickCount64();

    if (hWnd) {
        PostMessageW(hWnd, WM_USER + 100, (WPARAM)msg, 0);
    }
}

void NotifyWindow::SetBatchMode(bool enabled) {
    batchMode = enabled;
}

void NotifyWindow::FlushNotifications() {
}

void NotifyWindow::SetDisplayDuration(uint32_t milliseconds) {
    displayDuration = milliseconds;
}

void NotifyWindow::SetMaxQueueSize(size_t maxSize) {
    maxQueueSize = maxSize;
}

void NotifyWindow::ProcessPendingMessages() {
}

void NotifyWindow::HideWindow() {
    HideNotificationWindow();
}

void NotifyWindow::ShowWindowNow() {
    ShowNotificationWindow();
}

NotifyWindow* g_NotifyWindow = nullptr;