#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <memory>
#include <map>
#include <cmath>
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace std;

// ==================== 常量 ====================
const int BYTES_PER_ROW_DEFAULT = 16;
const int MAX_FILE_SIZE = 100 * 1024 * 1024; // 100MB

// ==================== 数据结构 ====================
struct Selection {
    int start;
    int end;
    bool active;
    Selection() : start(0), end(0), active(false) {}
};

// 修正：使用枚举类避免命名冲突
enum UndoType {
    UNDO_INSERT,
    UNDO_DELETE,
    UNDO_MODIFY
};

struct UndoAction {
    UndoType type;  // 使用枚举类型
    int offset;
    vector<unsigned char> oldData;
    vector<unsigned char> newData;

    UndoAction() : type(UNDO_MODIFY), offset(0) {}
};

// ==================== 全局变量 ====================
HWND g_hWnd = NULL;
HWND g_hEditHex = NULL;
HWND g_hEditAscii = NULL;
HWND g_hStatusBar = NULL;
HWND g_hToolbar = NULL;
HINSTANCE g_hInst = NULL;
HFONT g_hFont = NULL;
HACCEL g_hAccel = NULL;

vector<unsigned char> g_data;
vector<UndoAction> g_undoHistory;
int g_undoIndex = -1;
string g_fileName;
bool g_modified = false;
Selection g_selection;
int g_bytesPerRow = BYTES_PER_ROW_DEFAULT;
int g_currentOffset = 0;
int g_firstVisibleRow = 0;
bool g_showAscii = true;
bool g_showHex = true;
bool g_showOffset = true;
COLORREF g_hexColor = RGB(0, 0, 0);
COLORREF g_asciiColor = RGB(0, 0, 0);
COLORREF g_offsetColor = RGB(128, 128, 128);
COLORREF g_selectionColor = RGB(192, 192, 255);

// ==================== 函数声明 ====================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HexEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK FindDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ReplaceDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK GoToDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AboutDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

void UpdateDisplay();
void UpdateStatusBar();
void SaveUndoState(int offset, const vector<unsigned char>& oldData, const vector<unsigned char>& newData, UndoType type);
bool Undo();
bool Redo();
bool OpenFile(const string& path);
bool SaveFile(const string& path);
vector<int> FindPattern(const vector<unsigned char>& pattern, int startPos, bool forward = true);
bool ReplacePattern(const vector<unsigned char>& findPattern, const vector<unsigned char>& replacePattern);
bool ExportToC(const string& path);
bool ExportToPython(const string& path);
void ShowFindDialog();
void ShowReplaceDialog();
void ShowGoToDialog();
string InputBox(HWND hParent, const string& title, const string& prompt, const string& defaultText);

// ==================== 工具函数 ====================
string FormatHex(int value, int width = 8) {
    char buf[16];
    sprintf_s(buf, "%0*X", width, value);
    return string(buf);
}

string ByteToHex(unsigned char byte) {
    char buf[3];
    sprintf_s(buf, "%02X", byte);
    return string(buf);
}

unsigned char HexToByte(char c1, char c2) {
    unsigned char result = 0;
    if (c1 >= '0' && c1 <= '9') result = (c1 - '0') << 4;
    else if (c1 >= 'A' && c1 <= 'F') result = (c1 - 'A' + 10) << 4;
    else if (c1 >= 'a' && c1 <= 'f') result = (c1 - 'a' + 10) << 4;

    if (c2 >= '0' && c2 <= '9') result |= (c2 - '0');
    else if (c2 >= 'A' && c2 <= 'F') result |= (c2 - 'A' + 10);
    else if (c2 >= 'a' && c2 <= 'f') result |= (c2 - 'a' + 10);

    return result;
}

bool IsHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

string GetFileSizeString(size_t size) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unitIndex = 0;
    double dsize = (double)size;
    while (dsize >= 1024 && unitIndex < 4) {
        dsize /= 1024;
        unitIndex++;
    }
    char buf[64];
    sprintf_s(buf, "%.1f %s", dsize, units[unitIndex]);
    return string(buf);
}

string GetFileExtension(const string& path) {
    size_t pos = path.find_last_of('.');
    if (pos != string::npos) {
        return path.substr(pos + 1);
    }
    return "";
}

// ==================== 显示更新 ====================
void UpdateDisplay() {
    if (!g_hEditHex) return;

    if (g_data.empty()) {
        SetWindowTextA(g_hEditHex, "");
        if (g_hEditAscii) SetWindowTextA(g_hEditAscii, "");
        UpdateStatusBar();
        return;
    }

    int totalRows = (g_data.size() + g_bytesPerRow - 1) / g_bytesPerRow;
    int visibleRows = 50;

    string hexText;
    string asciiText;

    int startRow = max(0, g_firstVisibleRow);
    int endRow = min(totalRows, startRow + visibleRows);

    int maxOffsetWidth = 8;

    for (int row = startRow; row < endRow; row++) {
        int offset = row * g_bytesPerRow;

        if (g_showOffset) {
            char offsetStr[16];
            sprintf_s(offsetStr, "%0*X", maxOffsetWidth, offset);
            hexText += offsetStr;
            hexText += ": ";
        }

        if (g_showHex) {
            for (int col = 0; col < g_bytesPerRow; col++) {
                int index = offset + col;
                if (index < (int)g_data.size()) {
                    hexText += ByteToHex(g_data[index]);
                    hexText += " ";
                }
                else {
                    hexText += "   ";
                }
            }
        }

        if (g_showAscii) {
            hexText += " | ";
            for (int col = 0; col < g_bytesPerRow; col++) {
                int index = offset + col;
                if (index < (int)g_data.size()) {
                    unsigned char c = g_data[index];
                    hexText += (c >= 32 && c <= 126) ? (char)c : '.';
                }
                else {
                    hexText += " ";
                }
            }
        }

        hexText += "\r\n";
    }

    SetWindowTextA(g_hEditHex, hexText.c_str());
    UpdateStatusBar();
}

void UpdateStatusBar() {
    if (!g_hStatusBar) return;

    char statusText[512];
    string fileInfo = g_fileName.empty() ? "未命名" : g_fileName;

    sprintf_s(statusText,
        "偏移: 0x%08X (%d) | 大小: %s (%d 字节) | 文件: %s%s | 行: %d",
        g_currentOffset,
        g_currentOffset,
        GetFileSizeString(g_data.size()).c_str(),
        g_data.size(),
        fileInfo.c_str(),
        g_modified ? " [*]" : "",
        g_firstVisibleRow + 1);

    SendMessageA(g_hStatusBar, SB_SETTEXT, 0, (LPARAM)statusText);

    if (g_selection.active) {
        int selStart = min(g_selection.start, g_selection.end);
        int selEnd = max(g_selection.start, g_selection.end);
        int selSize = selEnd - selStart;
        char selText[128];
        sprintf_s(selText, "选择: %d 字节", selSize);
        SendMessageA(g_hStatusBar, SB_SETTEXT, 1, (LPARAM)selText);
    }
    else {
        SendMessageA(g_hStatusBar, SB_SETTEXT, 1, (LPARAM)"");
    }
}

// ==================== 撤销/重做 ====================
void SaveUndoState(int offset, const vector<unsigned char>& oldData,
    const vector<unsigned char>& newData, UndoType type) {
    // 删除当前位置之后的所有撤销记录
    if (g_undoIndex < (int)g_undoHistory.size() - 1) {
        g_undoHistory.erase(g_undoHistory.begin() + g_undoIndex + 1, g_undoHistory.end());
    }

    UndoAction action;
    action.type = type;
    action.offset = offset;
    action.oldData = oldData;
    action.newData = newData;
    g_undoHistory.push_back(action);
    g_undoIndex = g_undoHistory.size() - 1;

    // 限制撤销历史大小
    if (g_undoHistory.size() > 100) {
        g_undoHistory.erase(g_undoHistory.begin());
        g_undoIndex--;
    }
}

bool Undo() {
    if (g_undoIndex < 0) return false;

    const UndoAction& action = g_undoHistory[g_undoIndex];

    // 根据操作类型恢复
    if (action.type == UNDO_MODIFY) {
        for (size_t i = 0; i < action.oldData.size() && i + action.offset < g_data.size(); i++) {
            g_data[action.offset + i] = action.oldData[i];
        }
    }
    else if (action.type == UNDO_INSERT) {
        g_data.erase(g_data.begin() + action.offset,
            g_data.begin() + action.offset + action.newData.size());
    }
    else if (action.type == UNDO_DELETE) {
        g_data.insert(g_data.begin() + action.offset,
            action.oldData.begin(), action.oldData.end());
    }

    g_undoIndex--;
    g_modified = true;
    UpdateDisplay();
    return true;
}

bool Redo() {
    if (g_undoIndex >= (int)g_undoHistory.size() - 1) return false;

    g_undoIndex++;
    const UndoAction& action = g_undoHistory[g_undoIndex];

    if (action.type == UNDO_MODIFY) {
        for (size_t i = 0; i < action.newData.size() && i + action.offset < g_data.size(); i++) {
            g_data[action.offset + i] = action.newData[i];
        }
    }
    else if (action.type == UNDO_INSERT) {
        g_data.insert(g_data.begin() + action.offset,
            action.newData.begin(), action.newData.end());
    }
    else if (action.type == UNDO_DELETE) {
        g_data.erase(g_data.begin() + action.offset,
            g_data.begin() + action.offset + action.oldData.size());
    }

    g_modified = true;
    UpdateDisplay();
    return true;
}

// ==================== 文件操作 ====================
bool OpenFile(const string& path) {
    ifstream file(path, ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0, ios::beg);

    if (size > MAX_FILE_SIZE) {
        MessageBoxA(g_hWnd, "文件太大，请选择小于100MB的文件", "错误", MB_OK | MB_ICONERROR);
        return false;
    }

    g_data.resize(size);
    file.read((char*)g_data.data(), size);
    file.close();

    g_fileName = path;
    g_modified = false;
    g_currentOffset = 0;
    g_firstVisibleRow = 0;
    g_selection.active = false;
    g_undoHistory.clear();
    g_undoIndex = -1;

    UpdateDisplay();
    return true;
}

bool SaveFile(const string& path) {
    ofstream file(path, ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write((char*)g_data.data(), g_data.size());
    file.close();

    g_fileName = path;
    g_modified = false;
    UpdateDisplay();
    return true;
}

// ==================== 搜索功能 ====================
vector<int> FindPattern(const vector<unsigned char>& pattern, int startPos, bool forward) {
    vector<int> positions;
    if (pattern.empty() || g_data.empty() || startPos < 0 || startPos >= (int)g_data.size()) {
        return positions;
    }

    if (forward) {
        for (int i = startPos; i <= (int)g_data.size() - (int)pattern.size(); i++) {
            bool found = true;
            for (size_t j = 0; j < pattern.size(); j++) {
                if (g_data[i + j] != pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) positions.push_back(i);
        }
    }
    else {
        for (int i = startPos; i >= 0; i--) {
            bool found = true;
            for (size_t j = 0; j < pattern.size(); j++) {
                if (i + j >= g_data.size() || g_data[i + j] != pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) positions.push_back(i);
        }
    }

    return positions;
}

bool ReplacePattern(const vector<unsigned char>& findPattern,
    const vector<unsigned char>& replacePattern) {
    if (findPattern.empty() || g_data.empty()) {
        return false;
    }

    bool modified = false;
    int offset = 0;

    while (offset <= (int)g_data.size() - (int)findPattern.size()) {
        bool found = true;
        for (size_t i = 0; i < findPattern.size(); i++) {
            if (g_data[offset + i] != findPattern[i]) {
                found = false;
                break;
            }
        }

        if (found) {
            // 保存撤销状态
            vector<unsigned char> oldData(g_data.begin() + offset,
                g_data.begin() + offset + min(findPattern.size(), replacePattern.size()));
            vector<unsigned char> newData = replacePattern;

            // 执行替换
            for (size_t i = 0; i < min(findPattern.size(), replacePattern.size()); i++) {
                g_data[offset + i] = replacePattern[i];
            }

            SaveUndoState(offset, oldData, newData, UNDO_MODIFY);
            modified = true;
            offset += replacePattern.size();
        }
        else {
            offset++;
        }
    }

    if (modified) {
        g_modified = true;
        UpdateDisplay();
    }

    return modified;
}

// ==================== 导出功能 ====================
bool ExportToC(const string& path) {
    ofstream file(path);
    if (!file.is_open()) return false;

    string name = g_fileName;
    size_t pos = name.find_last_of("\\/");
    if (pos != string::npos) name = name.substr(pos + 1);
    pos = name.find_last_of('.');
    if (pos != string::npos) name = name.substr(0, pos);
    if (name.empty()) name = "data";

    file << "// Generated by Hex Editor Pro" << endl;
    file << "// Source: " << g_fileName << endl;
    file << "// Size: " << g_data.size() << " bytes" << endl;
    file << endl;
    file << "const unsigned char " << name << "[] = {" << endl;

    for (size_t i = 0; i < g_data.size(); i++) {
        if (i % 16 == 0) file << "    ";
        file << "0x" << ByteToHex(g_data[i]);
        if (i < g_data.size() - 1) file << ", ";
        if ((i + 1) % 16 == 0 && i < g_data.size() - 1) file << endl;
    }
    if (g_data.size() % 16 != 0) file << endl;
    file << "};" << endl;
    file << "const size_t " << name << "_size = sizeof(" << name << ");" << endl;

    return true;
}

bool ExportToPython(const string& path) {
    ofstream file(path);
    if (!file.is_open()) return false;

    file << "# Generated by Hex Editor Pro" << endl;
    file << "# Source: " << g_fileName << endl;
    file << "# Size: " << g_data.size() << " bytes" << endl;
    file << endl;
    file << "data = bytes([" << endl;

    for (size_t i = 0; i < g_data.size(); i++) {
        if (i % 16 == 0) file << "    ";
        file << (int)g_data[i];
        if (i < g_data.size() - 1) file << ", ";
        if ((i + 1) % 16 == 0 && i < g_data.size() - 1) file << endl;
    }
    if (g_data.size() % 16 != 0) file << endl;
    file << "])" << endl;

    return true;
}

// ==================== 对话框处理 ====================
INT_PTR CALLBACK FindDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        CheckDlgButton(hDlg, IDC_RADIO_DOWN, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_CASE, BST_UNCHECKED);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_FIND_NEXT:
        case IDOK: {
            char findBuf[256] = { 0 };
            GetDlgItemTextA(hDlg, IDC_EDIT_FIND, findBuf, 256);
            string findStr(findBuf);

            if (findStr.empty()) {
                MessageBoxA(hDlg, "请输入要查找的内容", "提示", MB_OK);
                return TRUE;
            }

            vector<unsigned char> pattern;
            for (size_t i = 0; i < findStr.length(); i += 2) {
                if (i + 1 < findStr.length()) {
                    char c1 = findStr[i];
                    char c2 = findStr[i + 1];
                    if (!IsHexChar(c1) || !IsHexChar(c2)) {
                        MessageBoxA(hDlg, "请输入有效的十六进制字符 (0-9, A-F)",
                            "错误", MB_OK | MB_ICONERROR);
                        return TRUE;
                    }
                    pattern.push_back(HexToByte(c1, c2));
                }
            }

            if (pattern.empty()) {
                MessageBoxA(hDlg, "请输入至少一个字节", "提示", MB_OK);
                return TRUE;
            }

            bool forward = (IsDlgButtonChecked(hDlg, IDC_RADIO_DOWN) == BST_CHECKED);
            int startPos = g_currentOffset;
            if (!forward && startPos > 0) startPos--;

            vector<int> positions = FindPattern(pattern, startPos, forward);

            if (!positions.empty()) {
                g_currentOffset = positions[0];
                g_firstVisibleRow = g_currentOffset / g_bytesPerRow;
                UpdateDisplay();
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBoxA(hDlg, "未找到匹配", "查找结果", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}

INT_PTR CALLBACK ReplaceDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        CheckDlgButton(hDlg, IDC_CHECK_CASE, BST_UNCHECKED);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_FIND_NEXT: {
            char findBuf[256] = { 0 };
            GetDlgItemTextA(hDlg, IDC_EDIT_FIND, findBuf, 256);
            string findStr(findBuf);

            if (findStr.empty()) {
                MessageBoxA(hDlg, "请输入要查找的内容", "提示", MB_OK);
                return TRUE;
            }

            vector<unsigned char> pattern;
            for (size_t i = 0; i < findStr.length(); i += 2) {
                if (i + 1 < findStr.length()) {
                    char c1 = findStr[i];
                    char c2 = findStr[i + 1];
                    if (!IsHexChar(c1) || !IsHexChar(c2)) {
                        MessageBoxA(hDlg, "请输入有效的十六进制字符", "错误", MB_OK);
                        return TRUE;
                    }
                    pattern.push_back(HexToByte(c1, c2));
                }
            }

            if (!pattern.empty()) {
                vector<int> positions = FindPattern(pattern, g_currentOffset, true);
                if (!positions.empty()) {
                    g_currentOffset = positions[0];
                    g_firstVisibleRow = g_currentOffset / g_bytesPerRow;
                    UpdateDisplay();
                }
                else {
                    MessageBoxA(hDlg, "未找到匹配", "查找结果", MB_OK | MB_ICONINFORMATION);
                }
            }
            return TRUE;
        }

        case IDC_BTN_REPLACE: {
            char findBuf[256] = { 0 };
            char replaceBuf[256] = { 0 };
            GetDlgItemTextA(hDlg, IDC_EDIT_FIND, findBuf, 256);
            GetDlgItemTextA(hDlg, IDC_EDIT_REPLACE, replaceBuf, 256);

            if (strlen(findBuf) == 0) {
                MessageBoxA(hDlg, "请输入要查找的内容", "提示", MB_OK);
                return TRUE;
            }

            vector<unsigned char> findPattern;
            for (size_t i = 0; i < strlen(findBuf); i += 2) {
                if (i + 1 < strlen(findBuf)) {
                    char c1 = findBuf[i];
                    char c2 = findBuf[i + 1];
                    if (!IsHexChar(c1) || !IsHexChar(c2)) {
                        MessageBoxA(hDlg, "查找内容格式错误", "错误", MB_OK);
                        return TRUE;
                    }
                    findPattern.push_back(HexToByte(c1, c2));
                }
            }

            vector<unsigned char> replacePattern;
            for (size_t i = 0; i < strlen(replaceBuf); i += 2) {
                if (i + 1 < strlen(replaceBuf)) {
                    char c1 = replaceBuf[i];
                    char c2 = replaceBuf[i + 1];
                    if (!IsHexChar(c1) || !IsHexChar(c2)) {
                        MessageBoxA(hDlg, "替换内容格式错误", "错误", MB_OK);
                        return TRUE;
                    }
                    replacePattern.push_back(HexToByte(c1, c2));
                }
            }

            vector<int> positions = FindPattern(findPattern, g_currentOffset, true);
            if (!positions.empty()) {
                vector<unsigned char> oldData(findPattern.begin(), findPattern.end());
                vector<unsigned char> newData = replacePattern;
                SaveUndoState(positions[0], oldData, newData, UNDO_MODIFY);

                for (size_t i = 0; i < min(findPattern.size(), replacePattern.size()); i++) {
                    g_data[positions[0] + i] = replacePattern[i];
                }

                g_currentOffset = positions[0] + replacePattern.size();
                g_modified = true;
                UpdateDisplay();
                MessageBoxA(hDlg, "替换完成", "成功", MB_OK);
            }
            else {
                MessageBoxA(hDlg, "未找到匹配", "替换结果", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        }

        case IDC_BTN_REPLACE_ALL: {
            char findBuf[256] = { 0 };
            char replaceBuf[256] = { 0 };
            GetDlgItemTextA(hDlg, IDC_EDIT_FIND, findBuf, 256);
            GetDlgItemTextA(hDlg, IDC_EDIT_REPLACE, replaceBuf, 256);

            if (strlen(findBuf) == 0) {
                MessageBoxA(hDlg, "请输入要查找的内容", "提示", MB_OK);
                return TRUE;
            }

            vector<unsigned char> findPattern;
            for (size_t i = 0; i < strlen(findBuf); i += 2) {
                if (i + 1 < strlen(findBuf)) {
                    char c1 = findBuf[i];
                    char c2 = findBuf[i + 1];
                    if (!IsHexChar(c1) || !IsHexChar(c2)) {
                        MessageBoxA(hDlg, "查找内容格式错误", "错误", MB_OK);
                        return TRUE;
                    }
                    findPattern.push_back(HexToByte(c1, c2));
                }
            }

            vector<unsigned char> replacePattern;
            for (size_t i = 0; i < strlen(replaceBuf); i += 2) {
                if (i + 1 < strlen(replaceBuf)) {
                    char c1 = replaceBuf[i];
                    char c2 = replaceBuf[i + 1];
                    if (!IsHexChar(c1) || !IsHexChar(c2)) {
                        MessageBoxA(hDlg, "替换内容格式错误", "错误", MB_OK);
                        return TRUE;
                    }
                    replacePattern.push_back(HexToByte(c1, c2));
                }
            }

            if (ReplacePattern(findPattern, replacePattern)) {
                MessageBoxA(hDlg, "替换全部完成", "成功", MB_OK);
            }
            else {
                MessageBoxA(hDlg, "未找到匹配", "替换结果", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}

INT_PTR CALLBACK GoToDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        CheckDlgButton(hDlg, IDC_RADIO_HEX, BST_CHECKED);
        SetDlgItemTextA(hDlg, IDC_EDIT_OFFSET, "0");
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            char buf[32] = { 0 };
            GetDlgItemTextA(hDlg, IDC_EDIT_OFFSET, buf, 32);

            int offset = 0;
            bool isHex = (IsDlgButtonChecked(hDlg, IDC_RADIO_HEX) == BST_CHECKED);

            if (isHex) {
                offset = strtol(buf, NULL, 16);
            }
            else {
                offset = atoi(buf);
            }

            if (offset >= 0 && offset < (int)g_data.size()) {
                g_currentOffset = offset;
                g_firstVisibleRow = offset / g_bytesPerRow;
                UpdateDisplay();
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBoxA(hDlg, "偏移超出范围", "错误", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}

INT_PTR CALLBACK AboutDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static char* resultBuf = NULL;

    switch (msg) {
    case WM_INITDIALOG: {
        resultBuf = (char*)lParam;
        SetDlgItemTextA(hDlg, IDC_EDIT_OFFSET, resultBuf);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextA(hDlg, IDC_EDIT_OFFSET, resultBuf, 256);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void ShowFindDialog() {
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_FIND), g_hWnd, FindDialogProc);
}

void ShowReplaceDialog() {
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_REPLACE), g_hWnd, ReplaceDialogProc);
}

void ShowGoToDialog() {
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_GOTO), g_hWnd, GoToDialogProc);
}

string InputBox(HWND hParent, const string& title, const string& prompt, const string& defaultText) {
    char buf[256] = { 0 };
    strcpy_s(buf, defaultText.c_str());

    int result = DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_GOTO), hParent,
        InputDialogProc, (LPARAM)buf);

    if (result == IDOK) {
        return string(buf);
    }
    return "";
}

// ==================== 十六进制编辑控件 ====================
LRESULT CALLBACK HexEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_UP: {
            if (g_firstVisibleRow > 0) {
                g_firstVisibleRow--;
                UpdateDisplay();
            }
            return 0;
        }
        case VK_DOWN: {
            int totalRows = (g_data.size() + g_bytesPerRow - 1) / g_bytesPerRow;
            if (g_firstVisibleRow < totalRows - 1) {
                g_firstVisibleRow++;
                UpdateDisplay();
            }
            return 0;
        }
        case VK_PRIOR: {
            g_firstVisibleRow = max(0, g_firstVisibleRow - 20);
            UpdateDisplay();
            return 0;
        }
        case VK_NEXT: {
            int totalRows = (g_data.size() + g_bytesPerRow - 1) / g_bytesPerRow;
            g_firstVisibleRow = min(totalRows - 1, g_firstVisibleRow + 20);
            UpdateDisplay();
            return 0;
        }
        case VK_HOME: {
            g_firstVisibleRow = 0;
            UpdateDisplay();
            return 0;
        }
        case VK_END: {
            int totalRows = (g_data.size() + g_bytesPerRow - 1) / g_bytesPerRow;
            g_firstVisibleRow = max(0, totalRows - 1);
            UpdateDisplay();
            return 0;
        }
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int rows = -delta / 120;
        g_firstVisibleRow = max(0, g_firstVisibleRow + rows);
        UpdateDisplay();
        return 0;
    }
    }

    return CallWindowProc(DefWindowProc, hWnd, msg, wParam, lParam);
}

// ==================== 主窗口 ====================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hWnd;

        // 创建字体
        g_hFont = CreateFontA(
            14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

        // 创建编辑框
        g_hEditHex = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_AUTOHSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
            0, 0, 800, 500,
            hWnd, (HMENU)IDC_EDIT_HEX, g_hInst, NULL);

        SendMessageA(g_hEditHex, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 子类化编辑框以捕获键盘事件
        SetWindowLongPtrA(g_hEditHex, GWLP_WNDPROC, (LONG_PTR)HexEditProc);

        // 创建状态栏（2部分）
        g_hStatusBar = CreateWindowExA(
            0, STATUSCLASSNAMEA, "",
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_STATUSBAR, g_hInst, NULL);

        int statWidths[] = { 400, -1 };    

        // 如果在 UNICODE 下仍有警告，建议使用 CreateWindowExW/宽字符版本。
        SendMessageA(g_hStatusBar, SB_SETPARTS, 2, (LPARAM)statWidths);

        // 加载加速键
        g_hAccel = LoadAccelerators(g_hInst, MAKEINTRESOURCE(IDR_ACCELERATOR));

        break;
    }

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        RECT rcStatus;
        GetWindowRect(g_hStatusBar, &rcStatus);
        int statusHeight = rcStatus.bottom - rcStatus.top;

        if (g_hEditHex) {
            SetWindowPos(g_hEditHex, NULL, 0, 0,
                width, height - statusHeight,
                SWP_NOZORDER);
        }

        if (g_hStatusBar) {
            SendMessageA(g_hStatusBar, WM_SIZE, 0, 0);
        }

        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_FILE_OPEN: {
            OPENFILENAMEA ofn = { 0 };
            char path[MAX_PATH] = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = "所有文件\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

            if (GetOpenFileNameA(&ofn)) {
                if (OpenFile(path)) {
                    SetWindowTextA(hWnd, ("Hex Editor - " + g_fileName).c_str());
                }
                else {
                    MessageBoxA(hWnd, "无法打开文件", "错误", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case ID_FILE_SAVE: {
            if (g_fileName.empty()) {
                SendMessageA(hWnd, WM_COMMAND, ID_FILE_SAVEAS, 0);
            }
            else if (SaveFile(g_fileName)) {
                SetWindowTextA(hWnd, ("Hex Editor - " + g_fileName).c_str());
                MessageBoxA(hWnd, "文件保存成功", "成功", MB_OK);
            }
            else {
                MessageBoxA(hWnd, "保存文件失败", "错误", MB_OK | MB_ICONERROR);
            }
            break;
        }

        case ID_FILE_SAVEAS: {
            OPENFILENAMEA ofn = { 0 };
            char path[MAX_PATH] = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = "所有文件\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT;

            if (GetSaveFileNameA(&ofn)) {
                if (SaveFile(path)) {
                    SetWindowTextA(hWnd, ("Hex Editor - " + g_fileName).c_str());
                    MessageBoxA(hWnd, "文件保存成功", "成功", MB_OK);
                }
                else {
                    MessageBoxA(hWnd, "保存文件失败", "错误", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case ID_FILE_EXPORT_C: {
            OPENFILENAMEA ofn = { 0 };
            char path[MAX_PATH] = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = "C文件\0*.c;*.h\0所有文件\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = "h";
            ofn.Flags = OFN_OVERWRITEPROMPT;

            if (GetSaveFileNameA(&ofn)) {
                if (ExportToC(path)) {
                    MessageBoxA(hWnd, "导出成功", "成功", MB_OK);
                }
                else {
                    MessageBoxA(hWnd, "导出失败", "错误", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case ID_FILE_EXPORT_PY: {
            OPENFILENAMEA ofn = { 0 };
            char path[MAX_PATH] = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = "Python文件\0*.py\0所有文件\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = "py";
            ofn.Flags = OFN_OVERWRITEPROMPT;

            if (GetSaveFileNameA(&ofn)) {
                if (ExportToPython(path)) {
                    MessageBoxA(hWnd, "导出成功", "成功", MB_OK);
                }
                else {
                    MessageBoxA(hWnd, "导出失败", "错误", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case ID_FILE_EXIT:
            PostMessageA(hWnd, WM_CLOSE, 0, 0);
            break;

        case ID_EDIT_UNDO:
            Undo();
            break;

        case ID_EDIT_REDO:
            Redo();
            break;

        case ID_EDIT_CUT:
        case ID_EDIT_COPY:
        case ID_EDIT_PASTE:
            break;

        case ID_EDIT_SELECTALL:
            if (!g_data.empty()) {
                g_selection.start = 0;
                g_selection.end = g_data.size();
                g_selection.active = true;
                UpdateDisplay();
            }
            break;

        case ID_SEARCH_FIND:
            ShowFindDialog();
            break;

        case ID_SEARCH_FINDNEXT:
            break;

        case ID_SEARCH_FINDPREV:
            break;

        case ID_SEARCH_REPLACE:
            ShowReplaceDialog();
            break;

        case ID_SEARCH_REPLACEALL:
            break;

        case ID_SEARCH_GOTO:
            ShowGoToDialog();
            break;

        case ID_VIEW_BYTESPERROW: {
            char buf[32];
            sprintf_s(buf, "%d", g_bytesPerRow);
            string input = InputBox(hWnd, "字节每行", "请输入每行显示的字节数:", buf);
            if (!input.empty()) {
                int newVal = atoi(input.c_str());
                if (newVal >= 4 && newVal <= 64) {
                    g_bytesPerRow = newVal;
                    UpdateDisplay();
                }
            }
            break;
        }

        case ID_VIEW_REFRESH:
            UpdateDisplay();
            break;

        case ID_HELP_ABOUT:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUT), hWnd, AboutDialogProc);
            break;
        }
        break;
    }

    case WM_CLOSE: {
        if (g_modified) {
            int result = MessageBoxA(hWnd, "文件已修改，是否保存？", "提示",
                MB_YESNOCANCEL | MB_ICONQUESTION);
            if (result == IDYES) {
                SendMessageA(hWnd, WM_COMMAND, ID_FILE_SAVE, 0);
                return 0;
            }
            else if (result == IDCANCEL) {
                return 0;
            }
        }
        DestroyWindow(hWnd);
        break;
    }

    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ==================== 主函数 ====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;

    // 初始化公共控件
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // 注册窗口类
    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "HexEditorClass";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "窗口注册失败", "错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 创建窗口
    HWND hWnd = CreateWindowExA(
        0, "HexEditorClass", "Hex Editor Pro",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        MessageBoxA(NULL, "窗口创建失败", "错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 加载菜单
    HMENU hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MAINMENU));
    SetMenu(hWnd, hMenu);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(hWnd, g_hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return msg.wParam;
}