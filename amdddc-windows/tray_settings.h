#pragma once
#ifndef TRAY_SETTINGS_H
#define TRAY_SETTINGS_H

#include <windows.h>
#include <string>
#include <vector>

// A global hotkey bound to a specific target input value.
struct HotkeyBinding {
    unsigned int input_value = 0; // VCP input value this hotkey switches to (e.g. 0xD1)
    unsigned int vk = 0;          // Virtual Key code
    unsigned int mod = 0;         // Modifiers (MOD_CONTROL, MOD_ALT, etc.)
};

struct TraySettings {
    int adapter_index = 0;
    int display_index = 0;
    unsigned int i2c_subaddress = 0x50; // default to LG alt mode
    unsigned int input_value = 0xD1;    // last-selected target input (dialog default)
    std::vector<HotkeyBinding> hotkeys; // one optional hotkey per target input

    std::wstring usb_device_path = L"";
    std::wstring usb_device_name = L"";
    unsigned int usb_arrival_input = 0;  // 0 means disabled/none
    unsigned int usb_removal_input = 0;  // 0 means disabled/none
};

// Path to settings.ini, stored alongside the executable.
std::wstring GetIniPath();

// Persist / restore the tray settings via the Win32 Profile (INI) APIs.
TraySettings LoadTraySettings();
void SaveTraySettings(const TraySettings& s);

#endif // !TRAY_SETTINGS_H
