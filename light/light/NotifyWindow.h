// NotifyWindow.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "winwrap.h"
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

class NotifyWindow {
public:
    struct NotificationMsg {
        std::string processName;
        std::string domain;
        std::string ipAddress;
        uint16_t port;
        uint64_t timestamp;
    };

    ~NotifyWindow();
    static NotifyWindow* GetInstance();

    void ShowBlockNotification(const std::string& processName, const std::string& domain);
    void ShowBlockNotificationEx(const std::string& processName,
        const std::string& domain,
        const std::string& ipAddress,
        uint16_t port);

    void SetBatchMode(bool enabled);
    void FlushNotifications();
    void SetDisplayDuration(uint32_t milliseconds);
    void SetMaxQueueSize(size_t maxSize);
    void HideWindow();
    void ShowWindowNow();
    void ProcessPendingMessages();

private:
    void StartSlideIn();
    void StartSlideOut();
    void UpdateAnimation();

    NotifyWindow();
    NotifyWindow(const NotifyWindow&) = delete;
    NotifyWindow& operator=(const NotifyWindow&) = delete;

    static NotifyWindow* instance;
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateNotifyWindow();
    void MessageLoop();
    void ShowNotificationWindow();
    void HideNotificationWindow();
    std::wstring FormatNotification(const NotificationMsg& msg);
    std::wstring Utf8ToWide(const std::string& utf8);
    std::wstring AnsiToWide(const std::string& ansi);
    std::wstring StringToWide(const std::string& str);

    HWND hWnd;
    std::thread messageThread;
    std::atomic<bool> threadRunning;
    std::atomic<bool> isVisible;

    std::mutex mtx;
    bool batchMode;
    size_t maxQueueSize;
    uint32_t displayDuration;

    int slideOffset;
    int animDirection;
    int slideStartX;
    int slideStartY;
    int slideEndX;
    int slideEndY;

    // 存储当前显示的通知文本
    std::wstring currentNotificationText;
};

extern NotifyWindow* g_NotifyWindow;