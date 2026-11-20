// YXGuiToggle.h
// GUI 开关模块：读写 Cache/gui.xayx，提供 <开>/<关> 指令
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "winwrap.h"
#include <string>

extern "C" {
    __declspec(dllexport) int __cdecl YXGui_IsEnabled();
    __declspec(dllexport) int __cdecl YXGui_SetEnabled(int enabled);
    __declspec(dllexport) int __cdecl YXGui_HandleCommand(const wchar_t* command);
    __declspec(dllexport) const wchar_t* __cdecl YXGui_GetConfigPath();
}
