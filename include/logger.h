#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

// ============================================================
// 轻量级日志系统
// 写入 %APPDATA%\Win11TaskbarTuner\app.log
// ============================================================

namespace Logger {

// 初始化日志系统（创建日志目录和文件）
void Init();

// 写日志（内部使用宏调用）
void WriteLog(const char* level, const std::wstring& message);

// 便捷宏
#define LOG_INFO(msg)  Logger::WriteLog("INFO",  msg)
#define LOG_WARN(msg)  Logger::WriteLog("WARN",  msg)
#define LOG_ERROR(msg) Logger::WriteLog("ERROR", msg)

// 获取日志文件路径
std::wstring GetLogFilePath();

} // namespace Logger
