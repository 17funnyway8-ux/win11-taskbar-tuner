#include "logger.h"
#include <shlobj.h>
#include <ctime>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace Logger {

static std::wstring g_logFilePath;
static CRITICAL_SECTION g_logCS;
static bool g_initialized = false;

std::wstring GetLogFilePath() {
    return g_logFilePath;
}

void Init() {
    if (g_initialized) return;
    
    InitializeCriticalSection(&g_logCS);
    
    // 获取 %APPDATA% 路径
    WCHAR appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::wstring dir = std::wstring(appDataPath) + L"\\Win11TaskbarTuner";
        CreateDirectoryW(dir.c_str(), nullptr);
        g_logFilePath = dir + L"\\app.log";
    } else {
        g_logFilePath = L"Win11TaskbarTuner.log";
    }
    
    g_initialized = true;
    
    // 写入启动分隔线
    WriteLog("INFO", L"========== Win11TaskbarTuner 启动 ==========");
}

void WriteLog(const char* level, const std::wstring& message) {
    if (!g_initialized) return;
    
    EnterCriticalSection(&g_logCS);
    
    FILE* fp = nullptr;
    _wfopen_s(&fp, g_logFilePath.c_str(), L"a");
    if (fp) {
        // 获取当前时间
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        
        fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec, level);
        
        // 写入宽字符串（转换为 UTF-8）
        int len = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1,
                                       nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string utf8(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1,
                               utf8.data(), len, nullptr, nullptr);
            fprintf(fp, "%s\n", utf8.c_str());
        } else {
            fprintf(fp, "(log conversion error)\n");
        }
        
        fclose(fp);
    }
    
    LeaveCriticalSection(&g_logCS);
    
    // 同时输出到调试器
    std::string levelStr(level);
    std::wstring levelW(levelStr.begin(), levelStr.end());
    OutputDebugStringW((L"[" + levelW + L"] " + message + L"\n").c_str());
}

} // namespace Logger
