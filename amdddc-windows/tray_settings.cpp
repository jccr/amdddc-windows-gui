#include "tray_settings.h"

// Retrieve path to settings.ini in the same directory as the executable
std::wstring GetIniPath() {
    wchar_t szPath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    std::wstring path(szPath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        path = path.substr(0, pos);
    }
    return path + L"\\settings.ini";
}

// Load configurations using Win32 Profile APIs
TraySettings LoadTraySettings() {
    TraySettings s;
    std::wstring iniPath = GetIniPath();

    s.adapter_index = GetPrivateProfileIntW(L"Settings", L"AdapterIndex", 0, iniPath.c_str());
    s.display_index = GetPrivateProfileIntW(L"Settings", L"DisplayIndex", 0, iniPath.c_str());

    wchar_t buf[32];
    GetPrivateProfileStringW(L"Settings", L"I2CSubAddress", L"0x50", buf, 32, iniPath.c_str());
    s.i2c_subaddress = wcstoul(buf, nullptr, 16);

    GetPrivateProfileStringW(L"Settings", L"InputValue", L"0xD1", buf, 32, iniPath.c_str());
    s.input_value = wcstoul(buf, nullptr, 16);

    s.hotkey_vk = GetPrivateProfileIntW(L"Settings", L"HotkeyVK", 0, iniPath.c_str());
    s.hotkey_mod = GetPrivateProfileIntW(L"Settings", L"HotkeyMod", 0, iniPath.c_str());

    return s;
}

// Save configurations using Win32 Profile APIs
void SaveTraySettings(const TraySettings& s) {
    std::wstring iniPath = GetIniPath();

    WritePrivateProfileStringW(L"Settings", L"AdapterIndex", std::to_wstring(s.adapter_index).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"DisplayIndex", std::to_wstring(s.display_index).c_str(), iniPath.c_str());

    wchar_t buf[32];
    swprintf_s(buf, L"0x%X", s.i2c_subaddress);
    WritePrivateProfileStringW(L"Settings", L"I2CSubAddress", buf, iniPath.c_str());

    swprintf_s(buf, L"0x%X", s.input_value);
    WritePrivateProfileStringW(L"Settings", L"InputValue", buf, iniPath.c_str());

    WritePrivateProfileStringW(L"Settings", L"HotkeyVK", std::to_wstring(s.hotkey_vk).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"HotkeyMod", std::to_wstring(s.hotkey_mod).c_str(), iniPath.c_str());
}
