#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

// ============================================================
// 配置管理 — 持久化用户设置
// 存储路径：HKCU\Software\Win11TaskbarTuner
// ============================================================

// 注册表路径
#define REG_KEY_APP_SETTINGS   L"Software\\Win11TaskbarTuner"

// 任务栏位置枚举（与 app.h 中一致，但 config 模块独立）
enum class TaskbarEdge : int {
    Left   = 0,
    Top    = 1,
    Right  = 2,
    Bottom = 3
};

struct AppConfig {
    TaskbarEdge lastEdge;        // 上次选择的任务栏位置
    int transparencyLevel;       // 透明度 0-100
    bool autoStart;              // 开机自启
    bool firstRun;               // 是否首次运行
};

namespace Config {

// 加载配置（从注册表读取，如果不存在则使用默认值）
AppConfig Load();

// 保存配置（写入注册表）
bool Save(const AppConfig& config);

// 仅保存单个字段
bool SaveEdge(TaskbarEdge edge);
bool SaveTransparency(int level);

// 获取配置注册表路径
std::wstring GetConfigKeyPath();

} // namespace Config
