// include/PermissionManager.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "winwrap.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>

class PermissionManager {
public:
    static PermissionManager& Instance();

    void SetStorePath(const std::wstring& path);
    const std::wstring& GetStorePath() const { return m_storePath; }

    void Load();
    void Save();

    bool IsPermanentlyGranted(const std::wstring& pluginId) const;
    void SetPermanentlyGranted(const std::wstring& pluginId, bool granted);
    void Revoke(const std::wstring& pluginId);

    std::vector<std::wstring> ListGranted() const;

    // 弹窗询问。返回：0=拒绝 1=仅本次 2=允许并记住
    int Prompt(HWND owner,
        const std::wstring& pluginId,
        const std::wstring& pluginName,
        const std::wstring& actionDesc);

private:
    PermissionManager() = default;
    PermissionManager(const PermissionManager&) = delete;
    PermissionManager& operator=(const PermissionManager&) = delete;

    mutable std::mutex m_mutex;
    std::wstring m_storePath;
    std::map<std::wstring, bool> m_granted;
};