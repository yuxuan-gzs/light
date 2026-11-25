// SecurityCore.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <iostream>
// ====== 前置声明：类定义在 light.cpp 中 ======
class SecurityCore; // forward declaration only (do not include .cpp here)  
extern SecurityCore* g_SecurityCore; // declared here to keep header self-contained

// Wrapper functions implemented in SecurityCore.cpp (which should include SecurityCore.cpp implementation)
bool IsDomainInFixedBlacklist(const std::string& domain);
bool IsUrlSafe(const std::string& url);
void LogBlockedUrl(const std::string& url);
std::vector<std::string> GetRiskDomains();

