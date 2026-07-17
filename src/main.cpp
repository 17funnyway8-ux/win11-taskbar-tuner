#include "app.h"
#include <commctrl.h>
#include <shellapi.h>
#include <string>

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ============================================================
// 前向声明
// ============================================================

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
ATOM RegisterWindowClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance);
void CreateTrayIcon(HWND hWnd);
void DeleteTrayIcon();
void ShowTrayMenu(HWND hWnd, bool leftClick = false);
void UpdateTrayMenuChecks(HMENU hMenu);
void OnChangeEdge(TaskbarEdge edge);
void OnChangeTransparency(int level);
void OnRestartExplorer();
bool CheckSingleInstance();
void ParseCommandLine();

// ============================================================
// WinMain 入口
// ============================================================

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine, int nCmdShow) {
    // 初始化日志系统
    Logger::Init();
    LOG_INFO(L"=== Win11TaskbarTuner 启动 ===");
    LOG_INFO(L"系统版本: " + TaskbarUtil::GetWinVersion());
    LOG_INFO(L"StuckRects3 诊断: " + TaskbarUtil::DumpStuckRects3());
    
    // 单实例检查
    if (!CheckSingleInstance()) {
        LOG_WARN(L"已有实例运行，退出");
        MessageBoxW(nullptr, L"任务栏调节器已在运行中。", APP_NAME_CN, MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    
    // 检查 Win11
    if (!TaskbarUtil::IsWindows11()) {
        std::wstring msg = L"此工具仅支持 Windows 11 系统。\n当前系统版本：" + TaskbarUtil::GetWinVersion();
        MessageBoxW(nullptr, msg.c_str(), APP_NAME_CN, MB_OK | MB_ICONINFORMATION);
        return 1;
    }
    
    // 解析命令行
    bool startMinimized = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (_wcsicmp(argv[i], L"--minimized") == 0 || _wcsicmp(argv[i], L"-m") == 0) {
                startMinimized = true;
            }
        }
        LocalFree(argv);
    }
    LOG_INFO(L"启动模式: " + std::wstring(startMinimized ? L"最小化" : L"正常"));
    
    // 初始化公共控件
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);
    
    // 注册窗口类
    RegisterWindowClass(hInstance);
    
    // 创建主窗口（始终隐藏，托盘程序）
    if (!InitInstance(hInstance)) {
        LOG_ERROR(L"窗口创建失败");
        return 1;
    }
    
    // 加载配置
    g_state.config = Config::Load();
    LOG_INFO(L"配置加载完成: Edge=" + std::to_wstring(static_cast<int>(g_state.config.lastEdge)) +
             L" Transparency=" + std::to_wstring(g_state.config.transparencyLevel));
    
    // 创建托盘图标
    CreateTrayIcon(g_state.hMainWnd);
    
    // 恢复透明度设置
    if (g_state.config.transparencyLevel < 100) {
        TaskbarUtil::SetTransparency(g_state.config.transparencyLevel);
    }
    
    // 如果不是最小化启动，首次运行显示提示
    if (!startMinimized && g_state.config.firstRun) {
        Shell_NotifyIconW(NIM_MODIFY, &g_state.nid); // 确保图标已显示
        MessageBoxW(nullptr,
            L"任务栏调节器已启动！\n\n"
            L"右键点击托盘图标可以：\n"
            L"  • 切换任务栏位置（左/右/上/下）\n"
            L"  • 调节任务栏透明度\n"
            L"  • 设置开机自启\n\n"
            L"切换位置时会自动重启资源管理器。",
            APP_NAME_CN, MB_OK | MB_ICONINFORMATION);
        g_state.config.firstRun = false;
        Config::Save(g_state.config);
    }
    
    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    // 清理
    DeleteTrayIcon();
    
    // 恢复透明度
    TaskbarUtil::ResetTransparency();
    
    // 释放单实例锁
    if (g_state.hMutex) {
        ReleaseMutex(g_state.hMutex);
        CloseHandle(g_state.hMutex);
    }
    
    LOG_INFO(L"=== 程序退出 ===");
    return static_cast<int>(msg.wParam);
}

// ============================================================
// 单实例检查
// ============================================================

bool CheckSingleInstance() {
    g_state.hMutex = CreateMutexW(nullptr, TRUE, APP_MUTEX_NAME);
    if (g_state.hMutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_state.hMutex) {
            CloseHandle(g_state.hMutex);
            g_state.hMutex = nullptr;
        }
        return false;
    }
    return true;
}

// ============================================================
// 窗口注册
// ============================================================

ATOM RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex{};
    wcex.cbSize        = sizeof(WNDCLASSEXW);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = WndProc;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcex.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"Win11TaskbarTuner_MainWnd";
    wcex.hIconSm       = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON_SM));
    
    ATOM result = RegisterClassExW(&wcex);
    if (result == 0) {
        LOG_ERROR(L"RegisterClassEx 失败 code=" + std::to_wstring(GetLastError()));
    }
    return result;
}

BOOL InitInstance(HINSTANCE hInstance) {
    g_state.hInstance = hInstance;
    
    HWND hWnd = CreateWindowExW(
        0,
        L"Win11TaskbarTuner_MainWnd",
        APP_NAME_CN,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 400, 300,
        nullptr, nullptr, hInstance, nullptr
    );
    
    if (!hWnd) {
        LOG_ERROR(L"CreateWindowEx 失败 code=" + std::to_wstring(GetLastError()));
        return FALSE;
    }
    
    g_state.hMainWnd = hWnd;
    // 始终隐藏主窗口（托盘程序）
    return TRUE;
}

// ============================================================
// 系统托盘
// ============================================================

void CreateTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW& nid = g_state.nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize           = sizeof(NOTIFYICONDATAW);
    nid.hWnd             = hWnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = LoadIconW(g_state.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    
    if (!nid.hIcon) {
        // 回退到系统图标
        nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    
    wcscpy_s(nid.szTip, L"任务栏调节器 - 右键切换位置");
    
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        LOG_ERROR(L"Shell_NotifyIcon(NIM_ADD) 失败 code=" + std::to_wstring(GetLastError()));
    } else {
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
}

void DeleteTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd   = g_state.hMainWnd;
    nid.uID    = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void UpdateTrayMenuChecks(HMENU hMenu) {
    // 位置 radio 选中
    CheckMenuRadioItem(hMenu, IDM_POSITION_LEFT, IDM_POSITION_BOTTOM,
                       IDM_POSITION_BASE + static_cast<UINT>(g_state.config.lastEdge),
                       MF_BYCOMMAND);
    
    // 透明度 radio 选中
    int transLevel = g_state.config.transparencyLevel;
    UINT transId = (transLevel >= 80) ? IDM_TRANSPARENCY_OPAQUE :
                   (transLevel >= 40) ? IDM_TRANSPARENCY_SEMI :
                   IDM_TRANSPARENCY_HIGH;
    CheckMenuRadioItem(hMenu, IDM_TRANSPARENCY_OPAQUE, IDM_TRANSPARENCY_HIGH,
                       transId, MF_BYCOMMAND);
    
    // 自动启动勾选
    CheckMenuItem(hMenu, IDM_AUTOSTART,
                  g_state.config.autoStart ? MF_CHECKED : MF_UNCHECKED);
}

void ShowTrayMenu(HWND hWnd, bool leftClick) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) {
        LOG_ERROR(L"CreatePopupMenu 失败");
        return;
    }
    
    // === 位置菜单 ===
    HMENU hPosMenu = CreatePopupMenu();
    AppendMenuW(hPosMenu, MF_STRING, IDM_POSITION_LEFT,   L"\u2190 左侧");
    AppendMenuW(hPosMenu, MF_STRING, IDM_POSITION_TOP,    L"\u2191 顶部");
    AppendMenuW(hPosMenu, MF_STRING, IDM_POSITION_RIGHT,  L"\u2192 右侧");
    AppendMenuW(hPosMenu, MF_STRING, IDM_POSITION_BOTTOM, L"\u2193 底部");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hPosMenu), L"任务栏位置");
    
    // === 透明度菜单 ===
    HMENU hTransMenu = CreatePopupMenu();
    AppendMenuW(hTransMenu, MF_STRING, IDM_TRANSPARENCY_OPAQUE, L"不透明");
    AppendMenuW(hTransMenu, MF_STRING, IDM_TRANSPARENCY_SEMI,  L"半透明 (50%)");
    AppendMenuW(hTransMenu, MF_STRING, IDM_TRANSPARENCY_HIGH,  L"高透明 (20%)");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hTransMenu), L"透明度");
    
    AppendMenuW(hMenu, MF_SEPARATOR, IDM_SEPARATOR_1, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_AUTOSTART, L"开机自启");
    AppendMenuW(hMenu, MF_STRING, IDM_RESTART_EXPLORER, L"重启资源管理器");
    AppendMenuW(hMenu, MF_SEPARATOR, IDM_SEPARATOR_3, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_ABOUT, L"关于");
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT,  L"退出");
    
    UpdateTrayMenuChecks(hMenu);
    
    // 确保菜单能正常关闭（Windows 托盘菜单的已知问题）
    SetForegroundWindow(hWnd);
    
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hWnd, nullptr);
    
    // 修复菜单不消失的 bug
    PostMessageW(hWnd, WM_NULL, 0, 0);
    
    DestroyMenu(hMenu);
}

// ============================================================
// 功能函数
// ============================================================

void OnChangeEdge(TaskbarEdge edge) {
    LOG_INFO(L"用户请求切换任务栏位置到: " + std::to_wstring(static_cast<int>(edge)));
    
    // 显示提示
    const wchar_t* edgeNames[] = { L"左侧", L"顶部", L"右侧", L"底部" };
    int idx = static_cast<int>(edge);
    
    // 提前更新通知
    g_state.nid.uFlags = NIF_INFO;
    wcscpy_s(g_state.nid.szInfoTitle, APP_NAME_CN);
    g_state.nid.dwInfoFlags = NIIF_INFO;
    
    if (TaskbarUtil::SetTaskbarEdge(edge)) {
        g_state.config.lastEdge = edge;
        Config::SaveEdge(edge);
        
        if (idx >= 0 && idx < 4) {
            std::wstring msg = L"任务栏已移至" + std::wstring(edgeNames[idx]) + L"\n(资源管理器已重启)";
            wcscpy_s(g_state.nid.szInfo, msg.c_str());
        }
    } else {
        std::wstring msg = L"位置切换可能未生效。\n请尝试手动重启资源管理器。";
        wcscpy_s(g_state.nid.szInfo, msg.c_str());
        g_state.nid.dwInfoFlags = NIIF_WARNING;
    }
    Shell_NotifyIconW(NIM_MODIFY, &g_state.nid);
}

void OnChangeTransparency(int level) {
    if (TaskbarUtil::SetTransparency(level)) {
        g_state.config.transparencyLevel = level;
        Config::SaveTransparency(level);
        LOG_INFO(L"透明度已设置: " + std::to_wstring(level));
    } else {
        LOG_ERROR(L"透明度设置失败");
        MessageBoxW(nullptr, L"透明度设置失败，可能不支持当前系统版本。",
                    APP_NAME_CN, MB_OK | MB_ICONWARNING);
    }
}

void OnRestartExplorer() {
    int result = MessageBoxW(nullptr,
        L"确定要重启资源管理器吗？\n\n"
        L"这会暂时关闭所有资源管理器窗口（包括文件管理器），\n"
        L"任务栏会短暂消失然后重新出现。",
        APP_NAME_CN, MB_YESNO | MB_ICONQUESTION);
    
    if (result == IDYES) {
        TaskbarUtil::RestartExplorer();
    }
}

// ============================================================
// 窗口过程
// ============================================================

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            return 0;
        
        case WM_TRAYICON: {
            // 托盘图标消息
            switch (LOWORD(lParam)) {
                case WM_RBUTTONUP:
                    ShowTrayMenu(hWnd, false);
                    break;
                case WM_LBUTTONUP:
                    // 左键也显示菜单（用户直觉操作）
                    ShowTrayMenu(hWnd, true);
                    break;
                case WM_LBUTTONDBLCLK:
                    // 双击：快速切换到上次位置
                    OnChangeEdge(g_state.config.lastEdge);
                    break;
            }
            return 0;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                // 位置
                case IDM_POSITION_LEFT:   OnChangeEdge(TaskbarEdge::Left);   break;
                case IDM_POSITION_TOP:    OnChangeEdge(TaskbarEdge::Top);    break;
                case IDM_POSITION_RIGHT:  OnChangeEdge(TaskbarEdge::Right);  break;
                case IDM_POSITION_BOTTOM: OnChangeEdge(TaskbarEdge::Bottom); break;
                
                // 透明度
                case IDM_TRANSPARENCY_OPAQUE: OnChangeTransparency(100); break;
                case IDM_TRANSPARENCY_SEMI:   OnChangeTransparency(50);  break;
                case IDM_TRANSPARENCY_HIGH:   OnChangeTransparency(20);  break;
                
                // 其他
                case IDM_AUTOSTART:
                    g_state.config.autoStart = !g_state.config.autoStart;
                    if (TaskbarUtil::SetAutoStart(g_state.config.autoStart)) {
                        Config::Save(g_state.config);
                    } else {
                        g_state.config.autoStart = !g_state.config.autoStart; // 回滚
                        MessageBoxW(nullptr, L"设置开机自启失败。", APP_NAME_CN, MB_OK | MB_ICONWARNING);
                    }
                    break;
                    
                case IDM_RESTART_EXPLORER:
                    OnRestartExplorer();
                    break;
                    
                case IDM_ABOUT: {
                    std::wstring diagInfo = TaskbarUtil::DumpStuckRects3();
                    std::wstring msg = 
                        L"Win11 任务栏调节器 v" APP_VERSION L"\n\n"
                        L"轻量级工具，恢复 Win11 任务栏位置自由切换\n\n"
                        L"系统版本：" + TaskbarUtil::GetWinVersion() + L"\n"
                        L"注册表诊断：\n" + diagInfo + L"\n\n"
                        L"日志文件：\n" + Logger::GetLogFilePath() + L"\n\n"
                        L"---\n"
                        L"技术：Win32 API + DWM\n"
                        L"开发：GStack 软件工坊";
                    MessageBoxW(hWnd, msg.c_str(), L"关于 " APP_NAME_CN, MB_OK | MB_ICONINFORMATION);
                    break;
                }
                
                case IDM_EXIT:
                    DestroyWindow(hWnd);
                    break;
            }
            return 0;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        
        case WM_QUERYENDSESSION:
            // 系统关机时保存配置
            Config::Save(g_state.config);
            return TRUE;
        
        case WM_ENDSESSION:
            if (wParam == TRUE) {
                Config::Save(g_state.config);
                TaskbarUtil::ResetTransparency();
            }
            return 0;
        
        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}
