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

    int count = GetPrivateProfileIntW(L"Settings", L"HotkeyCount", 0, iniPath.c_str());
    for (int i = 0; i < count; i++) {
        wchar_t key[32];
        HotkeyBinding hk;

        swprintf_s(key, L"Hotkey%dInput", i);
        GetPrivateProfileStringW(L"Settings", key, L"0x0", buf, 32, iniPath.c_str());
        hk.input_value = wcstoul(buf, nullptr, 16);

        swprintf_s(key, L"Hotkey%dVK", i);
        hk.vk = GetPrivateProfileIntW(L"Settings", key, 0, iniPath.c_str());

        swprintf_s(key, L"Hotkey%dMod", i);
        hk.mod = GetPrivateProfileIntW(L"Settings", key, 0, iniPath.c_str());

        if (hk.vk != 0) {
            s.hotkeys.push_back(hk);
        }
    }

    // Migrate the legacy single-hotkey format (HotkeyVK/HotkeyMod bound to InputValue).
    if (s.hotkeys.empty()) {
        unsigned int legacyVk = GetPrivateProfileIntW(L"Settings", L"HotkeyVK", 0, iniPath.c_str());
        if (legacyVk != 0) {
            HotkeyBinding hk;
            hk.input_value = s.input_value;
            hk.vk = legacyVk;
            hk.mod = GetPrivateProfileIntW(L"Settings", L"HotkeyMod", 0, iniPath.c_str());
            s.hotkeys.push_back(hk);
        }
    }

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

    WritePrivateProfileStringW(L"Settings", L"HotkeyCount", std::to_wstring(s.hotkeys.size()).c_str(), iniPath.c_str());
    for (size_t i = 0; i < s.hotkeys.size(); i++) {
        const HotkeyBinding& hk = s.hotkeys[i];
        wchar_t key[32];

        swprintf_s(key, L"Hotkey%zuInput", i);
        swprintf_s(buf, L"0x%X", hk.input_value);
        WritePrivateProfileStringW(L"Settings", key, buf, iniPath.c_str());

        swprintf_s(key, L"Hotkey%zuVK", i);
        WritePrivateProfileStringW(L"Settings", key, std::to_wstring(hk.vk).c_str(), iniPath.c_str());

        swprintf_s(key, L"Hotkey%zuMod", i);
        WritePrivateProfileStringW(L"Settings", key, std::to_wstring(hk.mod).c_str(), iniPath.c_str());
    }

    // Remove any trailing entries left over from a previously larger list.
    for (size_t i = s.hotkeys.size(); i < s.hotkeys.size() + 16; i++) {
        wchar_t key[32];
        swprintf_s(key, L"Hotkey%zuInput", i);
        WritePrivateProfileStringW(L"Settings", key, NULL, iniPath.c_str());
        swprintf_s(key, L"Hotkey%zuVK", i);
        WritePrivateProfileStringW(L"Settings", key, NULL, iniPath.c_str());
        swprintf_s(key, L"Hotkey%zuMod", i);
        WritePrivateProfileStringW(L"Settings", key, NULL, iniPath.c_str());
    }

    // Drop the obsolete legacy single-hotkey keys.
    WritePrivateProfileStringW(L"Settings", L"HotkeyVK", NULL, iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"HotkeyMod", NULL, iniPath.c_str());
}
