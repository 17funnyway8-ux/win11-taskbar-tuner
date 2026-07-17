#include "app.h"
#include <shlobj.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <new>
#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shlwapi.lib")

AppState g_state{};

// ============================================================
// DWM 窗口合成属性（未公开 API，用于透明效果）
// ============================================================

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_INVALID_STATE = 5
};

struct ACCENTPOLICY {
    int nAccentState;
    int nFlags;
    int nColor;       // 0xAABBGGRR
    int nAnimationId;
};

struct WINCOMPATTRDATA {
    int nAttribute;
    PVOID pData;
    ULONG ulDataSize;
};

#define WCA_ACCENT_POLICY 19

typedef BOOL (WINAPI *pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);

static pSetWindowCompositionAttribute GetSetWindowCompositionAttribute() {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) return nullptr;
    return reinterpret_cast<pSetWindowCompositionAttribute>(
        GetProcAddress(hUser, "SetWindowCompositionAttribute"));
}

// ============================================================
// 注册表工具函数实现
// ============================================================

namespace TaskbarUtil {

// --- 系统检测 ---

bool IsWindows11() {
    return GetBuildNumber() >= 22000;
}

DWORD GetBuildNumber() {
    using RtlGetVersionPtr = NTSTATUS (WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    
    auto RtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!RtlGetVersion) return 0;
    
    RTL_OSVERSIONINFOW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    if (RtlGetVersion(&osvi) != 0) return 0;
    
    return osvi.dwBuildNumber;
}

std::wstring GetWinVersion() {
    using RtlGetVersionPtr = NTSTATUS (WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return L"Unknown";
    
    auto RtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!RtlGetVersion) return L"Unknown";
    
    RTL_OSVERSIONINFOW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    RtlGetVersion(&osvi);
    
    std::wstringstream ss;
    ss << osvi.dwMajorVersion << L"." << osvi.dwMinorVersion 
       << L"." << osvi.dwBuildNumber;
    return ss.str();
}

// --- 注册表操作 ---

bool ReadRegistryBinary(HKEY hKeyRoot, const std::wstring& subKey,
                        const std::wstring& valueName,
                        std::vector<BYTE>& outData) {
    HKEY hKey;
    if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(hKey, valueName.c_str(), nullptr, nullptr, nullptr, &size);
    
    if (status != ERROR_SUCCESS || size == 0) {
        RegCloseKey(hKey);  // 先关闭再返回
        return false;
    }
    
    // 先关闭句柄，再分配内存（异常安全）
    RegCloseKey(hKey);
    
    try {
        outData.resize(size);
    } catch (const std::bad_alloc&) {
        LOG_ERROR(L"ReadRegistryBinary: 内存分配失败 size=" + std::to_wstring(size));
        return false;
    }
    
    // 重新打开读取数据
    if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    status = RegQueryValueExW(hKey, valueName.c_str(), nullptr, nullptr,
                              outData.data(), &size);
    RegCloseKey(hKey);
    
    if (status != ERROR_SUCCESS) {
        LOG_ERROR(L"ReadRegistryBinary: RegQueryValueEx 失败 code=" + std::to_wstring(status));
        return false;
    }
    
    return true;
}

bool WriteRegistryBinary(HKEY hKeyRoot, const std::wstring& subKey,
                         const std::wstring& valueName,
                         const std::vector<BYTE>& data) {
    HKEY hKey;
    // 使用 RegCreateKeyExW 确保键存在
    if (RegCreateKeyExW(hKeyRoot, subKey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        LOG_ERROR(L"WriteRegistryBinary: RegCreateKeyEx 失败 key=" + subKey);
        return false;
    }
    LSTATUS status = RegSetValueExW(hKey, valueName.c_str(), 0, REG_BINARY,
                                    data.data(), static_cast<DWORD>(data.size()));
    RegCloseKey(hKey);
    
    if (status != ERROR_SUCCESS) {
        LOG_ERROR(L"WriteRegistryBinary: RegSetValueEx 失败 code=" + std::to_wstring(status));
        return false;
    }
    return true;
}

bool WriteRegistryString(HKEY hKeyRoot, const std::wstring& subKey,
                         const std::wstring& valueName, const std::wstring& value) {
    HKEY hKey;
    if (RegCreateKeyExW(hKeyRoot, subKey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    DWORD dataSize = static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t));
    LSTATUS status = RegSetValueExW(hKey, valueName.c_str(), 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(value.c_str()), dataSize);
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

bool ReadRegistryString(HKEY hKeyRoot, const std::wstring& subKey,
                        const std::wstring& valueName, std::wstring& outValue) {
    HKEY hKey;
    if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    DWORD size = 0;
    DWORD type = 0;
    LSTATUS status = RegQueryValueExW(hKey, valueName.c_str(), nullptr, &type,
                                       nullptr, &size);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        RegCloseKey(hKey);
        return false;
    }
    
    // 先关闭，再分配（异常安全）
    RegCloseKey(hKey);
    
    try {
        std::wstring buf(size / sizeof(wchar_t), L'\0');
        
        if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return false;
        }
        status = RegQueryValueExW(hKey, valueName.c_str(), nullptr, nullptr,
                                  reinterpret_cast<LPBYTE>(buf.data()), &size);
        RegCloseKey(hKey);
        
        if (status != ERROR_SUCCESS) {
            return false;
        }
        
        // 去除末尾 null
        outValue = buf.c_str();
        return true;
    } catch (const std::bad_alloc&) {
        LOG_ERROR(L"ReadRegistryString: 内存分配失败");
        return false;
    }
}

bool DeleteRegistryValue(HKEY hKeyRoot, const std::wstring& subKey,
                         const std::wstring& valueName) {
    HKEY hKey;
    if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status = RegDeleteValueW(hKey, valueName.c_str());
    RegCloseKey(hKey);
    
    // ERROR_SUCCESS 或 ERROR_FILE_NOT_FOUND 都算成功
    return (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
}

// --- 任务栏位置 ---

TaskbarEdge GetCurrentEdge() {
    std::vector<BYTE> data;
    if (ReadRegistryBinary(HKEY_CURRENT_USER, REG_KEY_STUCK_RECTS, L"Settings", data)) {
        // 尝试偏移 0x0C（主）
        if (data.size() > STUCKRECTS_EDGE_OFFSET_PRIMARY) {
            BYTE edge = data[STUCKRECTS_EDGE_OFFSET_PRIMARY];
            if (edge <= 3) {
                return static_cast<TaskbarEdge>(edge);
            }
        }
        // 尝试偏移 0x0B（备用）
        if (data.size() > STUCKRECTS_EDGE_OFFSET_FALLBACK) {
            BYTE edge = data[STUCKRECTS_EDGE_OFFSET_FALLBACK];
            if (edge <= 3) {
                return static_cast<TaskbarEdge>(edge);
            }
        }
    }
    
    // Fallback：通过任务栏窗口位置推断
    HWND hTaskbar = FindWindowW(TASKBAR_CLASS_NAME, nullptr);
    if (hTaskbar) {
        RECT rc;
        if (GetWindowRect(hTaskbar, &rc)) {
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;
            
            // 判断任务栏方向
            if (width < height) {
                // 窄+高 = 左侧或右侧
                return (rc.left == 0) ? TaskbarEdge::Left : TaskbarEdge::Right;
            } else {
                // 宽+矮 = 顶部或底部
                return (rc.top == 0) ? TaskbarEdge::Top : TaskbarEdge::Bottom;
            }
        }
    }
    
    LOG_WARN(L"GetCurrentEdge: 无法确定任务栏位置，返回默认(底部)");
    return TaskbarEdge::Bottom;
}

bool SetTaskbarEdge(TaskbarEdge edge) {
    BYTE edgeValue = static_cast<BYTE>(edge);
    LOG_INFO(L"SetTaskbarEdge: 请求切换到位置 " + std::to_wstring(edgeValue));
    
    bool registrySuccess = false;
    
    // ========================================
    // 方法 1：修改 StuckRects3 注册表
    // ========================================
    std::vector<BYTE> stuckRectsData;
    if (ReadRegistryBinary(HKEY_CURRENT_USER, REG_KEY_STUCK_RECTS, L"Settings", stuckRectsData)) {
        LOG_INFO(L"StuckRects3 原始数据大小: " + std::to_wstring(stuckRectsData.size()) + L" 字节");
        
        bool modified = false;
        
        // 设置位置字节 — 同时修改 0x0B 和 0x0C 确保覆盖
        if (stuckRectsData.size() > STUCKRECTS_EDGE_OFFSET_PRIMARY) {
            stuckRectsData[STUCKRECTS_EDGE_OFFSET_PRIMARY] = edgeValue;
            modified = true;
        }
        if (stuckRectsData.size() > STUCKRECTS_EDGE_OFFSET_FALLBACK) {
            stuckRectsData[STUCKRECTS_EDGE_OFFSET_FALLBACK] = edgeValue;
            modified = true;
        }
        
        // 对于 24H2+，校准锚点字节
        for (size_t i = STUCKRECTS_ANCHOR_START; i <= STUCKRECTS_ANCHOR_END && i < stuckRectsData.size(); i++) {
            stuckRectsData[i] = 0x00;
        }
        
        if (modified) {
            registrySuccess = WriteRegistryBinary(HKEY_CURRENT_USER, REG_KEY_STUCK_RECTS,
                                                   L"Settings", stuckRectsData);
            if (registrySuccess) {
                LOG_INFO(L"StuckRects3 注册表修改成功");
            } else {
                LOG_ERROR(L"StuckRects3 注册表修改失败");
            }
        }
    } else {
        LOG_ERROR(L"无法读取 StuckRects3 注册表");
    }
    
    // ========================================
    // 方法 2：广播 WM_SETTINGCHANGE 通知 Shell
    // ========================================
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"TraySettings"),
                        SMTO_ABORTIFHUNG, 2000, &result);
    
    // ========================================
    // 方法 3：重启 Explorer 使注册表生效
    // ========================================
    // Win11 的 XAML 任务栏可能不响应注册表变更，
    // 需要重启 Explorer 才能让设置生效
    if (registrySuccess) {
        LOG_INFO(L"正在重启 Explorer 使设置生效...");
        if (RestartExplorer()) {
            LOG_INFO(L"Explorer 重启成功");
        } else {
            LOG_ERROR(L"Explorer 重启失败");
        }
    }
    
    return registrySuccess;
}

bool RestartExplorer() {
    // 终止 explorer.exe
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        LOG_ERROR(L"RestartExplorer: CreateToolhelp32Snapshot 失败");
        return false;
    }
    
    PROCESSENTRY32W pe32{};
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    
    std::vector<DWORD> pids;
    
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, L"explorer.exe") == 0) {
                pids.push_back(pe32.th32ProcessID);
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    
    if (pids.empty()) {
        LOG_WARN(L"RestartExplorer: 未找到 explorer.exe 进程");
        return false;
    }
    
    // 终止所有 explorer.exe 进程
    for (DWORD pid : pids) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess) {
            if (!TerminateProcess(hProcess, 0)) {
                LOG_WARN(L"TerminateProcess 失败 pid=" + std::to_wstring(pid) +
                         L" code=" + std::to_wstring(GetLastError()));
            }
            CloseHandle(hProcess);
        }
    }
    
    // 等待 Explorer 完全退出
    Sleep(500);
    
    // 重新启动 Explorer
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    
    WCHAR sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring explorerPath = std::wstring(sysDir) + L"\\explorer.exe";
    
    if (CreateProcessW(explorerPath.c_str(), nullptr, nullptr, nullptr, FALSE,
                       0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        LOG_INFO(L"Explorer 已重新启动");
        return true;
    } else {
        LOG_ERROR(L"CreateProcess(explorer.exe) 失败 code=" + std::to_wstring(GetLastError()));
        return false;
    }
}

std::wstring DumpStuckRects3() {
    std::vector<BYTE> data;
    if (!ReadRegistryBinary(HKEY_CURRENT_USER, REG_KEY_STUCK_RECTS, L"Settings", data)) {
        return L"(无法读取 StuckRects3)";
    }
    
    std::wstringstream ss;
    ss << L"Size=" << data.size() << L" | ";
    
    // 显示前 16 字节的十六进制
    ss << L"Hex: ";
    for (size_t i = 0; i < data.size() && i < 16; i++) {
        ss << std::hex << std::setfill(L'0') << std::setw(2) 
           << static_cast<int>(data[i]) << L" ";
    }
    
    // 标注关键偏移
    ss << L"\n偏移 0x0B=" << std::hex << std::setfill(L'0') << std::setw(2)
       << static_cast<int>(data.size() > 0x0B ? data[0x0B] : 0xFF);
    ss << L" 偏移 0x0C=" << std::hex << std::setfill(L'0') << std::setw(2)
       << static_cast<int>(data.size() > 0x0C ? data[0x0C] : 0xFF);
    
    return ss.str();
}

// --- 透明度 ---

bool SetTransparency(int level) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    
    HWND hTaskbar = FindWindowW(TASKBAR_CLASS_NAME, nullptr);
    if (!hTaskbar) {
        LOG_ERROR(L"SetTransparency: 找不到任务栏窗口");
        return false;
    }
    
    LOG_INFO(L"SetTransparency: level=" + std::to_wstring(level));
    
    if (level >= 100) {
        // 恢复不透明：禁用 Accent
        return ResetTransparency();
    }
    
    // 使用 SetWindowCompositionAttribute 设置透明效果
    auto pSetWindowCompositionAttr = GetSetWindowCompositionAttribute();
    if (!pSetWindowCompositionAttr) {
        LOG_ERROR(L"SetWindowCompositionAttribute 不可用");
        return false;
    }
    
    // 计算颜色：alpha = level 映射到 0-255
    // 使用 AccentPolicy 的 ACCENT_ENABLE_TRANSPARENTGRADIENT
    BYTE alpha = static_cast<BYTE>((level * 255) / 100);
    
    ACCENTPOLICY policy{};
    policy.nAccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT;
    policy.nFlags = 0;
    // 颜色格式 0xAABBGGRR，使用深色背景 + alpha
    policy.nColor = (alpha << 24) | 0x00000000;
    policy.nAnimationId = 0;
    
    WINCOMPATTRDATA data{};
    data.nAttribute = WCA_ACCENT_POLICY;
    data.pData = &policy;
    data.ulDataSize = sizeof(ACCENTPOLICY);
    
    BOOL result = pSetWindowCompositionAttr(hTaskbar, &data);
    if (!result) {
        LOG_ERROR(L"SetWindowCompositionAttribute 失败 code=" + std::to_wstring(GetLastError()));
        return false;
    }
    
    LOG_INFO(L"透明度设置成功");
    return true;
}

bool ResetTransparency() {
    HWND hTaskbar = FindWindowW(TASKBAR_CLASS_NAME, nullptr);
    if (!hTaskbar) return false;
    
    auto pSetWindowCompositionAttr = GetSetWindowCompositionAttribute();
    if (!pSetWindowCompositionAttr) return false;
    
    ACCENTPOLICY policy{};
    policy.nAccentState = ACCENT_DISABLED;
    policy.nFlags = 0;
    policy.nColor = 0;
    policy.nAnimationId = 0;
    
    WINCOMPATTRDATA data{};
    data.nAttribute = WCA_ACCENT_POLICY;
    data.pData = &policy;
    data.ulDataSize = sizeof(ACCENTPOLICY);
    
    BOOL result = pSetWindowCompositionAttr(hTaskbar, &data);
    LOG_INFO(L"ResetTransparency: result=" + std::to_wstring(result));
    return result != FALSE;
}

// --- 开机自启 ---

bool IsAutoStartEnabled() {
    std::wstring value;
    if (ReadRegistryString(HKEY_CURRENT_USER, REG_KEY_RUN, APP_NAME, value)) {
        return !value.empty();
    }
    return false;
}

bool SetAutoStart(bool enable) {
    if (enable) {
        WCHAR exePath[MAX_PATH] = {0};
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            LOG_ERROR(L"SetAutoStart: GetModuleFileName 失败 code=" + std::to_wstring(GetLastError()));
            return false;
        }
        
        std::wstring cmdLine = std::wstring(L"\"") + exePath + L"\" --minimized";
        bool ok = WriteRegistryString(HKEY_CURRENT_USER, REG_KEY_RUN, APP_NAME, cmdLine);
        LOG_INFO(L"SetAutoStart(true): " + std::wstring(ok ? L"成功" : L"失败") + 
                 L" path=" + exePath);
        return ok;
    } else {
        bool ok = DeleteRegistryValue(HKEY_CURRENT_USER, REG_KEY_RUN, APP_NAME);
        LOG_INFO(L"SetAutoStart(false): " + std::wstring(ok ? L"成功" : L"失败"));
        return ok;
    }
}

} // namespace TaskbarUtil
