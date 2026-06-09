#include "hotkey.h"

// Global Register Hotkey helper
void RegisterGlobalHotkey(HWND hwnd, const TraySettings& s) {
    UnregisterHotKey(hwnd, 1);
    if (s.hotkey_vk != 0) {
        if (!RegisterHotKey(hwnd, 1, s.hotkey_mod, s.hotkey_vk)) {
            MessageBoxW(hwnd, L"Could not register the global hotkey. It might be in use by another program.", L"Hotkey Error", MB_OK | MB_ICONWARNING);
        }
    }
}

// Helper to format hotkey combinations cleanly as text
std::wstring FormatHotkeyString(WORD vk, WORD mod) {
    if (vk == 0) return L"None";
    std::wstring text = L"";
    if (mod & MOD_CONTROL) text += L"Ctrl + ";
    if (mod & MOD_SHIFT)   text += L"Shift + ";
    if (mod & MOD_ALT)     text += L"Alt + ";
    if (mod & MOD_WIN)     text += L"Win + ";

    wchar_t keyName[64] = { 0 };
    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);

    // Add extended key bit if necessary for correct display (arrows, navigation keys)
    LONG lParamKey = (scanCode << 16);
    if (vk >= VK_PRIOR && vk <= VK_HELP) lParamKey |= (1 << 24);
    if (vk >= VK_INSERT && vk <= VK_DELETE) lParamKey |= (1 << 24);

    if (GetKeyNameTextW(lParamKey, keyName, 64) > 0) {
        text += keyName;
    } else {
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
            text += (wchar_t)vk;
        } else {
            text += L"Key " + std::to_wstring(vk);
        }
    }
    return text;
}
