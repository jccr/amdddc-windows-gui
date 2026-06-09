#pragma once
#ifndef TRAY_SETTINGS_H
#define TRAY_SETTINGS_H

#include <windows.h>
#include <string>

struct TraySettings {
    int adapter_index = 0;
    int display_index = 0;
    unsigned int i2c_subaddress = 0x50; // default to LG alt mode
    unsigned int input_value = 0xD1;    // default to USB-C
    unsigned int hotkey_vk = 0;         // Virtual Key code (0 = none)
    unsigned int hotkey_mod = 0;        // Modifiers (MOD_CONTROL, MOD_ALT, etc.)
};

// Path to settings.ini, stored alongside the executable.
std::wstring GetIniPath();

// Persist / restore the tray settings via the Win32 Profile (INI) APIs.
TraySettings LoadTraySettings();
void SaveTraySettings(const TraySettings& s);

#endif // !TRAY_SETTINGS_H
