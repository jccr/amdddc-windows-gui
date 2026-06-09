#pragma once
#ifndef TRAY_APP_H
#define TRAY_APP_H

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

// Main function to run the system tray application
int RunTrayApp(HINSTANCE hInstance);

#endif // !TRAY_APP_H
