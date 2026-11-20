#include <windows.h>
#include <iostream>
#include <string>
#include <wtsapi32.h>
#include <userenv.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")

// XOR 加密
const std::string PIPE_KEY = "LightPipeKey2024!@#";

std::string XorEncryptDecrypt(const std::string& data) {
    std::string result = data;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] ^= PIPE_KEY[i % PIPE_KEY.size()];
    }
    return result;
}

// ============ 服务端 ============
SERVICE_STATUS g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;

void WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    if (CtrlCode == SERVICE_CONTROL_STOP) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    }
}

// 获取当前活动用户的令牌
HANDLE GetActiveUserToken() {
    HANDLE hToken = NULL;
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) return NULL;
    WTSQueryUserToken(sessionId, &hToken);
    return hToken;
}

// 以用户身份启动程序（可显示窗口）
bool RunAsUser(const std::string& exePath) {
    HANDLE hToken = GetActiveUserToken();
    if (!hToken) return false;

    HANDLE hUserToken = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hUserToken)) {
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);

    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.lpDesktop = (LPSTR)"winsta0\\default";

    LPVOID envBlock = NULL;
    CreateEnvironmentBlock(&envBlock, hUserToken, FALSE);

    BOOL result = CreateProcessAsUserA(
        hUserToken, NULL, (LPSTR)exePath.c_str(),
        NULL, NULL, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE,
        envBlock, NULL, &si, &pi);

    if (envBlock) DestroyEnvironmentBlock(envBlock);

    if (result) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    CloseHandle(hUserToken);
    return result;
}

void WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    g_StatusHandle = RegisterServiceCtrlHandlerA("LightService", ServiceCtrlHandler);
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    HANDLE hPipe = CreateNamedPipeA(
        "\\\\.\\pipe\\LightPipe",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);

    while (g_ServiceStatus.dwCurrentState == SERVICE_RUNNING) {
        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            char buffer[4096] = { 0 };
            DWORD bytesRead;

            if (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL)) {
                std::string encrypted(buffer, bytesRead);
                std::string cmd = XorEncryptDecrypt(encrypted);

                std::string response = "FAIL";

                if (cmd.find("run:") == 0) {
                    std::string exePath = cmd.substr(4);
                    if (RunAsUser(exePath)) {
                        response = "OK";
                    }
                }

                std::string encResponse = XorEncryptDecrypt(response);
                DWORD bytesWritten;
                WriteFile(hPipe, encResponse.c_str(), encResponse.size(), &bytesWritten, NULL);
            }
            FlushFileBuffers(hPipe);
            DisconnectNamedPipe(hPipe);
        }
    }
}

int main() {
    SERVICE_TABLE_ENTRYA ServiceTable[] = {
        { (LPSTR)"LightService", (LPSERVICE_MAIN_FUNCTIONA)ServiceMain },
        { NULL, NULL }
    };
    StartServiceCtrlDispatcherA(ServiceTable);
    return 0;
}