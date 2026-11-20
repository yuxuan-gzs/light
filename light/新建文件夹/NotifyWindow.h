// NotifyWindow.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <string>
#include <queue>
#include <mutex>
#include <chrono>
// 全局指针声明
class SecurityCore;
extern SecurityCore* g_SecurityCore;

class NotifyWindow {
private:
    std::thread messageThread;
    std::atomic<bool> threadRunning;
    void MessageLoop();

    static NotifyWindow* instance;
    HWND hWnd;
    bool isVisible;
    std::mutex mtx;
    std::chrono::steady_clock::time_point lastShowTime;
    int consecutiveBlocks;

    struct NotificationMsg {
        std::string processName;
        std::string domain;
        std::string ipAddress;
        uint16_t port;
        uint64_t timestamp;
    };
    std::queue<NotificationMsg> msgQueue;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void CreateNotifyWindow();
    void UpdateWindowContent(const NotificationMsg& msg);
    std::wstring FormatNotification(const NotificationMsg& msg);

public:
    NotifyWindow();
    ~NotifyWindow();

    static NotifyWindow* GetInstance();

    void ShowBlockNotification(const std::string& processName, const std::string& domain);
    void ShowBlockNotificationEx(const std::string& processName, const std::string& domain,
        const std::string& ipAddress, uint16_t port);

    void ProcessPendingMessages();
    void HideWindow();
    void ShowWindowNow();

    void SetBatchMode(bool enabled);
    void FlushNotifications();

    void SetDisplayDuration(uint32_t milliseconds);
    void SetMaxQueueSize(size_t maxSize);

private:
    bool batchMode;
    size_t maxQueueSize;
    uint32_t displayDuration;
    std::queue<NotificationMsg> pendingBatch;
};

extern NotifyWindow* g_NotifyWindow;