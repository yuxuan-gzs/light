// YXGuiToggleLoader.h
// light.exe 加载 YXGuiToggle.dll 的封装
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "winwrap.h"
#include <string>

class YXGuiToggleLoader {
public:
    bool Load();
    void Unload();

    int IsEnabled();
    int HandleCommand(const wchar_t* cmd);
    const wchar_t* GetConfigPath();

    bool IsLoaded() const { return m_hDll != nullptr; }

private:
    HMODULE m_hDll = nullptr;

    typedef int (__cdecl *FnIsEnabled)();
    typedef int (__cdecl *FnSetEnabled)(int);
    typedef int (__cdecl *FnHandleCommand)(const wchar_t*);
    typedef const wchar_t* (__cdecl *FnGetConfigPath)();

    FnIsEnabled      m_fnIsEnabled      = nullptr;
    FnSetEnabled     m_fnSetEnabled     = nullptr;
    FnHandleCommand  m_fnHandleCommand  = nullptr;
    FnGetConfigPath  m_fnGetConfigPath  = nullptr;
};
