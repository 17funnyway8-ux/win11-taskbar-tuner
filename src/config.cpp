#include "config.h"
#include "logger.h"
#include <sstream>

namespace Config {

// 注册表工具（内部使用）
static bool RegReadDWORD(HKEY hKeyRoot, const std::wstring& subKey,
                         const std::wstring& valueName, DWORD& outValue) {
    HKEY hKey;
    if (RegOpenKeyExW(hKeyRoot, subKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD size = sizeof(DWORD);
    LSTATUS status = RegQueryValueExW(hKey, valueName.c_str(), nullptr, nullptr,
                                       reinterpret_cast<LPBYTE>(&outValue), &size);
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

static bool RegWriteDWORD(HKEY hKeyRoot, const std::wstring& subKey,
                          const std::wstring& valueName, DWORD value) {
    HKEY hKey;
    if (RegCreateKeyExW(hKeyRoot, subKey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS status = RegSetValueExW(hKey, valueName.c_str(), 0, REG_DWORD,
                                    reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

std::wstring GetConfigKeyPath() {
    return REG_KEY_APP_SETTINGS;
}

AppConfig Load() {
    AppConfig config{};
    config.lastEdge = TaskbarEdge::Bottom;
    config.transparencyLevel = 100;
    config.autoStart = false;
    config.firstRun = true;
    
    DWORD value = 0;
    if (RegReadDWORD(HKEY_CURRENT_USER, REG_KEY_APP_SETTINGS, L"LastEdge", value)) {
        if (value >= 0 && value <= 3) {
            config.lastEdge = static_cast<TaskbarEdge>(value);
            config.firstRun = false;
        }
    }
    
    if (RegReadDWORD(HKEY_CURRENT_USER, REG_KEY_APP_SETTINGS, L"Transparency", value)) {
        if (value >= 0 && value <= 100) {
            config.transparencyLevel = static_cast<int>(value);
        }
    }
    
    if (RegReadDWORD(HKEY_CURRENT_USER, REG_KEY_APP_SETTINGS, L"AutoStart", value)) {
        config.autoStart = (value != 0);
    }
    
    LOG_INFO(L"配置已加载: Edge=" + std::to_wstring(static_cast<int>(config.lastEdge)) +
             L" Transparency=" + std::to_wstring(config.transparencyLevel) +
             L" AutoStart=" + std::to_wstring(config.autoStart));
    
    return config;
}

bool Save(const AppConfig& config) {
    bool ok = true;
    ok = RegWriteDWORD(HKEY_CURRENT_USER, REG_KEY_APP_SETTINGS, L"LastEdge",
                       static_cast<DWORD>(config.lastEdge)) && ok;
    ok = RegWriteDWORD(HKEY_CURRENT_USER, REG_KEY_APP_SETTINGS, L"Transparency",
                       static_cast<DWORD>(config.transparencyLevel)) && ok;
    ok = RegWriteDWORD(HKEY_CURRENT_USER, REG_KEY_APP_SETTINGS, L"AutoStart",
                       config.autoStart ? 1 : 0) && ok;
    
    if (ok) {
        LOG_INFO(L"配置已保存");
    } else {
        LOG_ERROR(L"配置保存失败");
    }
    return ok;
}

bool SaveEdge(TaskbarEdge edge) {
    return RegWriteDWORD(HKEY_CURRENT_USER, REG_KEY_APP_SETTINGS, L"LastEdge",
                         static_cast<DWORD>(edge));
}

bool SaveTransparency(int level) {
    return RegWriteDWORD(HKEY_CURRENT_USER, REG_KEY_APP_SETTINGS, L"Transparency",
                         static_cast<DWORD>(level));
}

} // namespace Config
