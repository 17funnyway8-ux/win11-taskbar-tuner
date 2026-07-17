#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

#include "config.h"
#include "logger.h"

// ============================================================
// 常量定义
// ============================================================

// 应用标识
#define APP_NAME        L"Win11TaskbarTuner"
#define APP_NAME_CN     L"任务栏调节器"
#define APP_VERSION     L"0.1.0"
#define APP_MUTEX_NAME  L"Global\\Win11TaskbarTuner_SingleInstance"
#define WM_TRAYICON     (WM_APP + 1)

// StuckRects3 偏移量
// 经过多方交叉验证：偏移 0x0C（第13字节，从0开始）在 Win10/11 上更常见
// 但 0x0B 也有来源支持。代码中同时尝试两个偏移。
#define STUCKRECTS_EDGE_OFFSET_PRIMARY   0x0C
#define STUCKRECTS_EDGE_OFFSET_FALLBACK  0x0B

// 24H2 锚点字节范围
#define STUCKRECTS_ANCHOR_START  0x06
#define STUCKRECTS_ANCHOR_END    0x08

// 托盘菜单命令 ID
enum TrayMenuCommand : UINT {
    IDM_POSITION_BASE   = 1000,
    IDM_POSITION_LEFT   = IDM_POSITION_BASE + 0,
    IDM_POSITION_TOP    = IDM_POSITION_BASE + 1,
    IDM_POSITION_RIGHT  = IDM_POSITION_BASE + 2,
    IDM_POSITION_BOTTOM = IDM_POSITION_BASE + 3,
    IDM_SEPARATOR_1     = 2000,
    IDM_TRANSPARENCY_BASE = 3000,
    IDM_TRANSPARENCY_OPAQUE    = IDM_TRANSPARENCY_BASE + 0,
    IDM_TRANSPARENCY_SEMI      = IDM_TRANSPARENCY_BASE + 1,
    IDM_TRANSPARENCY_HIGH      = IDM_TRANSPARENCY_BASE + 2,
    IDM_TRANSPARENCY_CUSTOM    = IDM_TRANSPARENCY_BASE + 3,
    IDM_SEPARATOR_2     = 4000,
    IDM_AUTOSTART       = 5000,
    IDM_SETTINGS        = 5001,
    IDM_RESTART_EXPLORER = 5002,
    IDM_SEPARATOR_3     = 6000,
    IDM_ABOUT           = 6001,
    IDM_EXIT            = 6002,
};

// 注册表路径
#define REG_KEY_STUCK_RECTS    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StuckRects3"
#define REG_KEY_RUN            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

// 任务栏窗口类名
#define TASKBAR_CLASS_NAME     L"Shell_TrayWnd"
#define TASKBAR_SECONDARY_CLASS L"Shell_SecondaryTrayWnd"

// 图标资源 ID
#define IDI_APP_ICON    101
#define IDI_APP_ICON_SM 102

// ============================================================
// 工具函数声明
// ============================================================

namespace TaskbarUtil {

// --- 系统检测 ---
bool IsWindows11();
std::wstring GetWinVersion();
DWORD GetBuildNumber();

// --- 注册表操作 ---
bool ReadRegistryBinary(HKEY hKeyRoot, const std::wstring& subKey,
                        const std::wstring& valueName,
                        std::vector<BYTE>& outData);
bool WriteRegistryBinary(HKEY hKeyRoot, const std::wstring& subKey,
                         const std::wstring& valueName,
                         const std::vector<BYTE>& data);
bool WriteRegistryString(HKEY hKeyRoot, const std::wstring& subKey,
                         const std::wstring& valueName, const std::wstring& value);
bool ReadRegistryString(HKEY hKeyRoot, const std::wstring& subKey,
                        const std::wstring& valueName, std::wstring& outValue);
bool DeleteRegistryValue(HKEY hKeyRoot, const std::wstring& subKey,
                         const std::wstring& valueName);

// --- 任务栏位置 ---
TaskbarEdge GetCurrentEdge();
bool SetTaskbarEdge(TaskbarEdge edge);
bool RestartExplorer();
std::wstring DumpStuckRects3();  // 导出注册表值用于诊断

// --- 透明度 ---
bool SetTransparency(int level);  // 0-100
bool ResetTransparency();

// --- 开机自启 ---
bool IsAutoStartEnabled();
bool SetAutoStart(bool enable);

} // namespace TaskbarUtil

// ============================================================
// 应用状态
// ============================================================

struct AppState {
    HINSTANCE hInstance;
    HWND hMainWnd;
    NOTIFYICONDATAW nid;
    AppConfig config;
    HANDLE hMutex;  // 单实例锁
};

extern AppState g_state;
