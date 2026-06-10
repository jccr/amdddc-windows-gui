#include "hotkey.h"

// Unregister every global hotkey we may have registered.
void UnregisterGlobalHotkeys(HWND hwnd) {
    for (int id = 1; id <= MAX_HOTKEY_ID; id++) {
        UnregisterHotKey(hwnd, id);
    }
}

// (Re)register one global hotkey per binding. Hotkey id == binding index + 1.
void RegisterGlobalHotkey(HWND hwnd, const TraySettings& s) {
    UnregisterGlobalHotkeys(hwnd);

    std::wstring failed;
    for (size_t i = 0; i < s.hotkeys.size() && i < MAX_HOTKEY_ID; i++) {
        const HotkeyBinding& hk = s.hotkeys[i];
        if (hk.vk == 0) continue;
        if (!RegisterHotKey(hwnd, (int)(i + 1), hk.mod, hk.vk)) {
            if (!failed.empty()) failed += L"\n";
            failed += L"  • " + FormatHotkeyString((WORD)hk.vk, (WORD)hk.mod);
        }
    }

    if (!failed.empty()) {
        std::wstring msg = L"Could not register the following hotkey(s). They may be in use by another program or duplicated:\n\n" + failed;
        MessageBoxW(hwnd, msg.c_str(), L"Hotkey Error", MB_OK | MB_ICONWARNING);
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
