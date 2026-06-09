#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "tray_app.h"
#include "resource.h"
#include "adl.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Dark theme colors for the settings window
static const COLORREF DARK_BKG_COLOR = RGB(32, 32, 32);
static const COLORREF DARK_CTRL_COLOR = RGB(50, 50, 50);
static const COLORREF DARK_TEXT_COLOR = RGB(240, 240, 240);

#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 1

#define VCP_CODE_SWITCH_INPUT 0xF4

// Declared in amdddc-windows.cpp
extern void vSetVcpCommand(unsigned int subaddress, unsigned char ucVcp, unsigned int ulVal, int iAdapterIndex, int iDisplayIndex);

// Global Variables
TraySettings g_settings;
HWND g_hHiddenWnd = NULL;
HWND g_hSettingsWnd = NULL;
HFONT g_hFont = NULL;
HBRUSH g_hbrDarkBkg = NULL;   // settings window background
HBRUSH g_hbrDarkCtrl = NULL;  // edit/combo/list control background

enum PreferredAppMode {
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
    Max
};

using SetPreferredAppModeProc = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemesProc = void(WINAPI*)();
using RefreshImmersiveColorPolicyStateProc = void(WINAPI*)();
using AllowDarkModeForWindowProc = bool(WINAPI*)(HWND, bool);

bool IsWindowsDarkModeActive() {
    HKEY hKey;
    DWORD value = 1; // Default to Light Mode
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);
    }
    return value == 0;
}

void ApplyThemePreference() {
    bool isDarkMode = IsWindowsDarkModeActive();
    HMODULE hUxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxTheme) {
        auto pSetPreferredAppMode = reinterpret_cast<SetPreferredAppModeProc>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
        auto pRefreshImmersiveColorPolicyState = reinterpret_cast<RefreshImmersiveColorPolicyStateProc>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(104)));
        auto pFlushMenuThemes = reinterpret_cast<FlushMenuThemesProc>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136)));
        if (pSetPreferredAppMode) {
            pSetPreferredAppMode(isDarkMode ? ForceDark : ForceLight);
            // SetPreferredAppMode only updates an internal preference; the immersive
            // color policy (which menu theming reads) must be refreshed for it to take
            // effect, then the cached menu themes flushed.
            if (pRefreshImmersiveColorPolicyState) pRefreshImmersiveColorPolicyState();
            if (pFlushMenuThemes) pFlushMenuThemes();
        }
        FreeLibrary(hUxTheme);
    }
}

// Opt a specific window into dark mode. Used for the window that owns popup menus
// (so menus tracked from it pick up the dark theme) and for the settings controls.
void AllowDarkModeForWindow(HWND hWnd) {
    HMODULE hUxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxTheme) {
        auto pAllowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowProc>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
        if (pAllowDarkModeForWindow) {
            pAllowDarkModeForWindow(hWnd, IsWindowsDarkModeActive());
        }
        FreeLibrary(hUxTheme);
    }
}

// Lazily create the brushes used to paint the dark settings window.
void EnsureDarkBrushes() {
    if (!g_hbrDarkBkg) g_hbrDarkBkg = CreateSolidBrush(DARK_BKG_COLOR);
    if (!g_hbrDarkCtrl) g_hbrDarkCtrl = CreateSolidBrush(DARK_CTRL_COLOR);
}

// Paint the window title bar dark via DWM (no-op on light mode / older Windows).
void ApplyDarkTitleBar(HWND hWnd) {
    BOOL dark = IsWindowsDarkModeActive() ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

// Group boxes ignore dark theming and always draw a light etched frame, so we
// owner-draw them: dark fill, a subtle gray border, and a light caption.
LRESULT CALLBACK DarkGroupBoxSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hbrDarkBkg);

        wchar_t text[128] = {};
        GetWindowTextW(hWnd, text, ARRAYSIZE(text));
        int textLen = (int)wcslen(text);

        HFONT hFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        SIZE sz = {};
        GetTextExtentPoint32W(hdc, text, textLen, &sz);

        // Draw the frame starting halfway down the caption height.
        RECT frame = rc;
        frame.top += sz.cy / 2;
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, frame.left, frame.top, frame.right, frame.bottom);
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        // Caption: erase the frame line behind it, then draw the text.
        int textX = rc.left + 9;
        RECT textRc = { textX - 2, rc.top, textX + sz.cx + 4, rc.top + sz.cy };
        FillRect(hdc, &textRc, g_hbrDarkBkg);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, DARK_TEXT_COLOR);
        TextOutW(hdc, textX, rc.top, text, textLen);

        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, DarkGroupBoxSubclassProc, 0);
        break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// Apply the dark immersive theme to every child control of the settings window.
void ApplyDarkThemeToControls(HWND hWnd) {
    if (!IsWindowsDarkModeActive()) return;
    EnumChildWindows(hWnd, [](HWND child, LPARAM) -> BOOL {
        AllowDarkModeForWindow(child);
        wchar_t cls[64] = {};
        GetClassNameW(child, cls, ARRAYSIZE(cls));
        // Comboboxes need the "CFD" variant to darken both the field and dropdown.
        if (_wcsicmp(cls, L"COMBOBOX") == 0) {
            SetWindowTheme(child, L"DarkMode_CFD", nullptr);
        } else if (_wcsicmp(cls, L"BUTTON") == 0 &&
                   (GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) {
            // Owner-draw group boxes instead of relying on the theme.
            SetWindowSubclass(child, DarkGroupBoxSubclassProc, 0, 0);
        } else {
            SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
        }
        return TRUE;
    }, 0);
}

// Control IDs for Settings Window
enum ControlIDs {
    ID_GRP_DISPLAY = 100,
    ID_LBL_DISPLAY,
    ID_CB_DISPLAY,
    ID_LBL_I2C,
    ID_CB_I2C,
    ID_GRP_SWITCH,
    ID_LBL_INPUT,
    ID_CB_INPUT,
    ID_LBL_CUSTOM,
    ID_TXT_CUSTOM,
    ID_GRP_HOTKEY,
    ID_LBL_HOTKEY,
    ID_HK_HOTKEY,
    ID_BTN_TEST,
    ID_BTN_SAVE,
    ID_BTN_CANCEL
};

struct DetectedDisplay {
    int adapter_index;
    int display_index;
    std::wstring display_name;
    std::wstring adapter_name;
};

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

// Enumerate connected and active displays from ADL SDK
std::vector<DetectedDisplay> EnumerateDisplays() {
    std::vector<DetectedDisplay> list;
    FILE* dbg = nullptr;
    fopen_s(&dbg, "C:\\Users\\jccr\\Projects\\AMDLG-InputSwitch\\tray_debug.log", "w");
    if (dbg) {
        fprintf(dbg, "EnumerateDisplays called\n");
    }

    int iNumberAdapters = 0;
    if (!InitADL()) {
        if (dbg) {
            fprintf(dbg, "InitADL failed\n");
            fclose(dbg);
        }
        return list;
    }

    adlprocs.ADL_Adapter_NumberOfAdapters_Get(&iNumberAdapters);
    if (dbg) {
        fprintf(dbg, "iNumberAdapters: %d\n", iNumberAdapters);
    }
    if (iNumberAdapters <= 0) {
        if (dbg) fclose(dbg);
        return list;
    }

    LPAdapterInfo lpInfo = (LPAdapterInfo)malloc(sizeof(AdapterInfo) * iNumberAdapters);
    if (!lpInfo) {
        if (dbg) fclose(dbg);
        return list;
    }
    memset(lpInfo, '\0', sizeof(AdapterInfo) * iNumberAdapters);
    adlprocs.ADL_Adapter_AdapterInfo_Get(lpInfo, sizeof(AdapterInfo) * iNumberAdapters);

    for (int i = 0; i < iNumberAdapters; i++) {
        int iAdapterIndex = lpInfo[i].iAdapterIndex;
        int iNumberDisplays = 0;
        LPADLDisplayInfo lpDisp = nullptr;

        int ADL_Err = adlprocs.ADL_Display_DisplayInfo_Get(iAdapterIndex, &iNumberDisplays, &lpDisp, 0);
        if (dbg) {
            fprintf(dbg, "Adapter %d (Index: %d) Name: %s, ADL_Err: %d, iNumberDisplays: %d\n", 
                i, iAdapterIndex, lpInfo[i].strAdapterName, ADL_Err, iNumberDisplays);
        }

        if (ADL_OK == ADL_Err && lpDisp != nullptr) {
            for (int j = 0; j < iNumberDisplays; j++) {
                if (dbg) {
                    fprintf(dbg, "  Display %d: Name: %s, LogicalAdapterIndex: %d, LogicalIndex: %d, InfoValue: %d\n",
                        j, lpDisp[j].strDisplayName, 
                        lpDisp[j].displayID.iDisplayLogicalAdapterIndex,
                        lpDisp[j].displayID.iDisplayLogicalIndex,
                        lpDisp[j].iDisplayInfoValue);
                }

                // Connected and active check (matching original CLI mapping-only filter)
                if (lpDisp[j].iDisplayInfoValue & ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED) {
                    if (iAdapterIndex == lpDisp[j].displayID.iDisplayLogicalAdapterIndex) {
                        DetectedDisplay dd;
                        dd.adapter_index = iAdapterIndex;
                        dd.display_index = lpDisp[j].displayID.iDisplayLogicalIndex;

                        int len = MultiByteToWideChar(CP_ACP, 0, lpInfo[i].strAdapterName, -1, nullptr, 0);
                        std::wstring wAdapterName(len, L'\0');
                        MultiByteToWideChar(CP_ACP, 0, lpInfo[i].strAdapterName, -1, &wAdapterName[0], len);

                        len = MultiByteToWideChar(CP_ACP, 0, lpDisp[j].strDisplayName, -1, nullptr, 0);
                        std::wstring wDisplayName(len, L'\0');
                        MultiByteToWideChar(CP_ACP, 0, lpDisp[j].strDisplayName, -1, &wDisplayName[0], len);

                        if (!wAdapterName.empty() && wAdapterName.back() == L'\0') wAdapterName.pop_back();
                        if (!wDisplayName.empty() && wDisplayName.back() == L'\0') wDisplayName.pop_back();

                        dd.adapter_name = wAdapterName;
                        dd.display_name = wDisplayName;
                        list.push_back(dd);
                        if (dbg) {
                            fprintf(dbg, "    -> Display added successfully!\n");
                        }
                    }
                }
            }
            ADL_Main_Memory_Free((void**)&lpDisp);
        }
    }
    free(lpInfo);
    if (dbg) {
        fprintf(dbg, "EnumerateDisplays finished, returning %zu displays\n", list.size());
        fclose(dbg);
    }
    return list;
}

// Run the DDC/CI Input Switch command on a background worker thread (delay = 5 seconds)
void ExecuteSwitchAsync(unsigned int i2c_subaddress, unsigned int input_value, int adapter_index, int display_index) {
    std::thread t([=]() {
        if (InitADL()) {
            vSetVcpCommand(i2c_subaddress, VCP_CODE_SWITCH_INPUT, input_value, adapter_index, display_index);
        }
    });
    t.detach();
}

// Global Register Hotkey helper
void RegisterGlobalHotkey(HWND hwnd, const TraySettings& s) {
    UnregisterHotKey(hwnd, 1);
    if (s.hotkey_vk != 0) {
        if (!RegisterHotKey(hwnd, 1, s.hotkey_mod, s.hotkey_vk)) {
            MessageBoxW(hwnd, L"Could not register the global hotkey. It might be in use by another program.", L"Hotkey Error", MB_OK | MB_ICONWARNING);
        }
    }
}

// Structure to keep track of current recorded hotkey
struct RecordedHotkey {
    WORD vk = 0;
    WORD mod = 0;
};
static RecordedHotkey g_recordedHotkey;

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

// Subclass window procedure for the hotkey edit control
LRESULT CALLBACK HotkeyEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_SETFOCUS:
        HideCaret(hWnd);
        break;

    case WM_KILLFOCUS: {
        std::wstring text = FormatHotkeyString(g_recordedHotkey.vk, g_recordedHotkey.mod);
        SetWindowTextW(hWnd, text.c_str());
        break;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

        int vk = (int)wParam;

        // Escape or Backspace clears the hotkey
        if (vk == VK_ESCAPE || vk == VK_BACK) {
            if (g_recordedHotkey.vk == 0 && vk == VK_ESCAPE) {
                // If hotkey is already empty, let Escape close the settings dialog
                PostMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(ID_BTN_CANCEL, BN_CLICKED), (LPARAM)GetDlgItem(GetParent(hWnd), ID_BTN_CANCEL));
            } else {
                g_recordedHotkey.vk = 0;
                g_recordedHotkey.mod = 0;
                SetWindowTextW(hWnd, L"None");
            }
            return 0;
        }

        // If it's a modifier key itself, update the text to show the modifier combinations
        if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
            std::wstring text = L"";
            if (ctrl) text += L"Ctrl + ";
            if (shift) text += L"Shift + ";
            if (alt) text += L"Alt + ";
            if (win) text += L"Win + ";
            if (!text.empty()) {
                text = text.substr(0, text.size() - 3);
            } else {
                text = L"None";
            }
            SetWindowTextW(hWnd, text.c_str());
            return 0;
        }

        // It's a valid non-modifier key. Record it!
        WORD mod = 0;
        if (ctrl) mod |= MOD_CONTROL;
        if (alt)  mod |= MOD_ALT;
        if (shift) mod |= MOD_SHIFT;
        if (win)   mod |= MOD_WIN;

        g_recordedHotkey.vk = (WORD)vk;
        g_recordedHotkey.mod = mod;

        std::wstring text = FormatHotkeyString(g_recordedHotkey.vk, g_recordedHotkey.mod);
        SetWindowTextW(hWnd, text.c_str());
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        int vk = (int)wParam;
        // Eat keyup for modifiers to prevent them triggering system actions
        if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_MENU) {
            return 0;
        }
        break;
    }

    case WM_CHAR:
    case WM_SYSCHAR:
        return 0;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// DPI Awareness Helper Functions
UINT GetWindowDpi(HWND hwnd) {
    typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        GetDpiForWindowProc pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
        if (pGetDpiForWindow) {
            return pGetDpiForWindow(hwnd);
        }
    }
    // Fallback to system DPI
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi;
}

int ScaleDpi(int val, UINT dpi) {
    return MulDiv(val, dpi, 96);
}

HFONT CreateDpiFont(UINT dpi) {
    // 9pt Segoe UI scaled to DPI
    int height = -MulDiv(9, dpi, 72);
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void UpdateLayout(HWND hWnd, UINT dpi) {
    HWND hGrpDisplay = GetDlgItem(hWnd, ID_GRP_DISPLAY);
    HWND hCbDisplay = GetDlgItem(hWnd, ID_CB_DISPLAY);
    HWND hLblI2C = GetDlgItem(hWnd, ID_LBL_I2C);
    HWND hCbI2C = GetDlgItem(hWnd, ID_CB_I2C);

    HWND hGrpSwitch = GetDlgItem(hWnd, ID_GRP_SWITCH);
    HWND hLblInput = GetDlgItem(hWnd, ID_LBL_INPUT);
    HWND hCbInput = GetDlgItem(hWnd, ID_CB_INPUT);

    HWND hLblHotkey = GetDlgItem(hWnd, ID_LBL_HOTKEY);
    HWND hHkHotkey = GetDlgItem(hWnd, ID_HK_HOTKEY);

    HWND hBtnTest = GetDlgItem(hWnd, ID_BTN_TEST);
    HWND hBtnSave = GetDlgItem(hWnd, ID_BTN_SAVE);
    HWND hBtnCancel = GetDlgItem(hWnd, ID_BTN_CANCEL);

    // GroupBox 1 (Display Configuration)
    if (hGrpDisplay) MoveWindow(hGrpDisplay, ScaleDpi(10, dpi), ScaleDpi(10, dpi), ScaleDpi(365, dpi), ScaleDpi(90, dpi), TRUE);
    if (hCbDisplay) MoveWindow(hCbDisplay, ScaleDpi(20, dpi), ScaleDpi(30, dpi), ScaleDpi(345, dpi), ScaleDpi(150, dpi), TRUE);
    if (hLblI2C) MoveWindow(hLblI2C, ScaleDpi(20, dpi), ScaleDpi(60, dpi), ScaleDpi(95, dpi), ScaleDpi(18, dpi), TRUE);
    if (hCbI2C) MoveWindow(hCbI2C, ScaleDpi(120, dpi), ScaleDpi(57, dpi), ScaleDpi(100, dpi), ScaleDpi(100, dpi), TRUE);

    // GroupBox 2 (Switch Configuration)
    if (hGrpSwitch) MoveWindow(hGrpSwitch, ScaleDpi(10, dpi), ScaleDpi(110, dpi), ScaleDpi(365, dpi), ScaleDpi(95, dpi), TRUE);
    if (hLblInput) MoveWindow(hLblInput, ScaleDpi(20, dpi), ScaleDpi(130, dpi), ScaleDpi(95, dpi), ScaleDpi(18, dpi), TRUE);
    if (hCbInput) MoveWindow(hCbInput, ScaleDpi(120, dpi), ScaleDpi(127, dpi), ScaleDpi(245, dpi), ScaleDpi(150, dpi), TRUE);
    if (hLblHotkey) MoveWindow(hLblHotkey, ScaleDpi(20, dpi), ScaleDpi(165, dpi), ScaleDpi(95, dpi), ScaleDpi(18, dpi), TRUE);
    if (hHkHotkey) MoveWindow(hHkHotkey, ScaleDpi(120, dpi), ScaleDpi(162, dpi), ScaleDpi(245, dpi), ScaleDpi(22, dpi), TRUE);

    // Action Buttons
    if (hBtnTest) MoveWindow(hBtnTest, ScaleDpi(10, dpi), ScaleDpi(215, dpi), ScaleDpi(100, dpi), ScaleDpi(26, dpi), TRUE);
    if (hBtnSave) MoveWindow(hBtnSave, ScaleDpi(175, dpi), ScaleDpi(215, dpi), ScaleDpi(95, dpi), ScaleDpi(26, dpi), TRUE);
    if (hBtnCancel) MoveWindow(hBtnCancel, ScaleDpi(280, dpi), ScaleDpi(215, dpi), ScaleDpi(95, dpi), ScaleDpi(26, dpi), TRUE);
}

// Settings Dialog / Window Procedure
LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // Target Display Group
        HWND hGrpDisplay = CreateWindowExW(0, L"BUTTON", L"Display Configuration", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            0, 0, 0, 0, hWnd, (HMENU)ID_GRP_DISPLAY, NULL, NULL);

        HWND hCbDisplay = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            0, 0, 0, 0, hWnd, (HMENU)ID_CB_DISPLAY, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"I2C Address:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hWnd, (HMENU)ID_LBL_I2C, NULL, NULL);

        HWND hCbI2C = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP,
            0, 0, 0, 0, hWnd, (HMENU)ID_CB_I2C, NULL, NULL);

        // Switch Command Group
        CreateWindowExW(0, L"BUTTON", L"Switch Configuration", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            0, 0, 0, 0, hWnd, (HMENU)ID_GRP_SWITCH, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Target Input:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hWnd, (HMENU)ID_LBL_INPUT, NULL, NULL);

        HWND hCbInput = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP,
            0, 0, 0, 0, hWnd, (HMENU)ID_CB_INPUT, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Global Hotkey:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hWnd, (HMENU)ID_LBL_HOTKEY, NULL, NULL);

        HWND hHkHotkey = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_CENTER | WS_TABSTOP,
            0, 0, 0, 0, hWnd, (HMENU)ID_HK_HOTKEY, NULL, NULL);
        SetWindowSubclass(hHkHotkey, HotkeyEditSubclassProc, ID_HK_HOTKEY, 0);

        // Action Buttons
        CreateWindowExW(0, L"BUTTON", L"Test Switch", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            0, 0, 0, 0, hWnd, (HMENU)ID_BTN_TEST, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            0, 0, 0, 0, hWnd, (HMENU)ID_BTN_SAVE, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            0, 0, 0, 0, hWnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);

        // Get window DPI and initialize scaled font
        UINT dpi = GetWindowDpi(hWnd);
        g_hFont = CreateDpiFont(dpi);

        // Apply Segoe UI Font to all child controls
        EnumChildWindows(hWnd, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            return TRUE;
        }, 0);

        // Layout controls scaled by DPI
        UpdateLayout(hWnd, dpi);

        // Apply dark theme (title bar + child controls) when Windows is in dark mode
        EnsureDarkBrushes();
        ApplyDarkTitleBar(hWnd);
        ApplyDarkThemeToControls(hWnd);

        // Populate Displays
        std::vector<DetectedDisplay> displays = EnumerateDisplays();
        int selectedIndex = 0;
        for (size_t i = 0; i < displays.size(); i++) {
            std::wstring label = displays[i].display_name + L" (" + displays[i].adapter_name + L")";
            int idx = (int)SendMessageW(hCbDisplay, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            SendMessageW(hCbDisplay, CB_SETITEMDATA, idx, MAKELPARAM(displays[i].adapter_index, displays[i].display_index));

            if (displays[i].adapter_index == g_settings.adapter_index &&
                displays[i].display_index == g_settings.display_index) {
                selectedIndex = idx;
            }
        }
        if (displays.size() > 0) {
            SendMessageW(hCbDisplay, CB_SETCURSEL, selectedIndex, 0);
        }

        // Populate I2C Source Address Options
        SendMessageW(hCbI2C, CB_ADDSTRING, 0, (LPARAM)L"0x50");
        SendMessageW(hCbI2C, CB_ADDSTRING, 0, (LPARAM)L"0x51");
        wchar_t i2cStr[16];
        swprintf_s(i2cStr, L"0x%X", g_settings.i2c_subaddress);
        SetWindowTextW(hCbI2C, i2cStr);

        // Populate Target Input Options
        SendMessageW(hCbInput, CB_ADDSTRING, 0, (LPARAM)L"0xD0 (DP1)");
        SendMessageW(hCbInput, CB_ADDSTRING, 0, (LPARAM)L"0xD1 (USB-C / DP2)");
        SendMessageW(hCbInput, CB_ADDSTRING, 0, (LPARAM)L"0x90 (HDMI1)");
        SendMessageW(hCbInput, CB_ADDSTRING, 0, (LPARAM)L"0x91 (HDMI2)");

        int inputSelect = -1;
        if (g_settings.input_value == 0xD0) inputSelect = 0;
        else if (g_settings.input_value == 0xD1) inputSelect = 1;
        else if (g_settings.input_value == 0x90) inputSelect = 2;
        else if (g_settings.input_value == 0x91) inputSelect = 3;

        if (inputSelect != -1) {
            SendMessageW(hCbInput, CB_SETCURSEL, inputSelect, 0);
        } else {
            wchar_t valStr[16];
            swprintf_s(valStr, L"0x%X", g_settings.input_value);
            SetWindowTextW(hCbInput, valStr);
        }

        // Setup Hotkey Control
        g_recordedHotkey.vk = g_settings.hotkey_vk;
        g_recordedHotkey.mod = g_settings.hotkey_mod;
        std::wstring hkText = FormatHotkeyString(g_recordedHotkey.vk, g_recordedHotkey.mod);
        SetWindowTextW(hHkHotkey, hkText.c_str());

        break;
    }

    case WM_COMMAND: {
        // Test Switch action
        if (LOWORD(wParam) == ID_BTN_TEST) {
            // Read inputs temporarily and trigger
            HWND hCbDisplay = GetDlgItem(hWnd, ID_CB_DISPLAY);
            HWND hCbI2C = GetDlgItem(hWnd, ID_CB_I2C);
            HWND hCbInput = GetDlgItem(hWnd, ID_CB_INPUT);

            int dispSel = (int)SendMessageW(hCbDisplay, CB_GETCURSEL, 0, 0);
            if (dispSel != CB_ERR) {
                LPARAM data = SendMessageW(hCbDisplay, CB_GETITEMDATA, dispSel, 0);
                int adapterIdx = LOWORD(data);
                int displayIdx = HIWORD(data);

                wchar_t i2cBuf[32];
                GetWindowTextW(hCbI2C, i2cBuf, 32);
                unsigned int i2cAddr = wcstoul(i2cBuf, nullptr, 16);

                wchar_t valBuf[64];
                GetWindowTextW(hCbInput, valBuf, 64);
                unsigned int inputVal = wcstoul(valBuf, nullptr, 16);

                ExecuteSwitchAsync(i2cAddr, inputVal, adapterIdx, displayIdx);
            }
        }

        // Cancel action
        if (LOWORD(wParam) == ID_BTN_CANCEL) {
            DestroyWindow(hWnd);
        }

        // Save action
        if (LOWORD(wParam) == ID_BTN_SAVE) {
            HWND hCbDisplay = GetDlgItem(hWnd, ID_CB_DISPLAY);
            HWND hCbI2C = GetDlgItem(hWnd, ID_CB_I2C);
            HWND hCbInput = GetDlgItem(hWnd, ID_CB_INPUT);
            HWND hHkHotkey = GetDlgItem(hWnd, ID_HK_HOTKEY);

            int dispSel = (int)SendMessageW(hCbDisplay, CB_GETCURSEL, 0, 0);
            if (dispSel != CB_ERR) {
                LPARAM data = SendMessageW(hCbDisplay, CB_GETITEMDATA, dispSel, 0);
                g_settings.adapter_index = LOWORD(data);
                g_settings.display_index = HIWORD(data);
            }

            wchar_t i2cBuf[32];
            GetWindowTextW(hCbI2C, i2cBuf, 32);
            g_settings.i2c_subaddress = wcstoul(i2cBuf, nullptr, 16);

            wchar_t valBuf[64];
            GetWindowTextW(hCbInput, valBuf, 64);
            g_settings.input_value = wcstoul(valBuf, nullptr, 16);

            g_settings.hotkey_vk = g_recordedHotkey.vk;
            g_settings.hotkey_mod = g_recordedHotkey.mod;

            // Save to settings.ini
            SaveTraySettings(g_settings);

            // Re-register hotkey
            RegisterGlobalHotkey(g_hHiddenWnd, g_settings);

            DestroyWindow(hWnd);
        }
        break;
    }

    case WM_DPICHANGED: {
        UINT newDpi = LOWORD(wParam);
        RECT* prcSuggestedWindow = (RECT*)lParam;

        // Resize window to suggested size
        SetWindowPos(hWnd, NULL,
            prcSuggestedWindow->left,
            prcSuggestedWindow->top,
            prcSuggestedWindow->right - prcSuggestedWindow->left,
            prcSuggestedWindow->bottom - prcSuggestedWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // Recreate font matching new DPI
        if (g_hFont) DeleteObject(g_hFont);
        g_hFont = CreateDpiFont(newDpi);

        // Apply new font to children
        EnumChildWindows(hWnd, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            return TRUE;
        }, 0);

        // Recalculate positions
        UpdateLayout(hWnd, newDpi);
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HWND hwndChild = (HWND)lParam;
        HDC hdc = (HDC)wParam;
        bool dark = IsWindowsDarkModeActive();
        // The hotkey field is a read-only EDIT, which sends WM_CTLCOLORSTATIC;
        // paint it like an input field rather than a label.
        if (GetDlgCtrlID(hwndChild) == ID_HK_HOTKEY) {
            if (dark) {
                SetTextColor(hdc, DARK_TEXT_COLOR);
                SetBkColor(hdc, DARK_CTRL_COLOR);
                return (LRESULT)g_hbrDarkCtrl;
            }
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        // Labels and group-box captions sit on the window background.
        if (dark) {
            SetTextColor(hdc, DARK_TEXT_COLOR);
            SetBkColor(hdc, DARK_BKG_COLOR);
            return (LRESULT)g_hbrDarkBkg;
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        if (IsWindowsDarkModeActive()) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DARK_TEXT_COLOR);
            SetBkColor(hdc, DARK_CTRL_COLOR);
            return (LRESULT)g_hbrDarkCtrl;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    case WM_CTLCOLORBTN: {
        if (IsWindowsDarkModeActive()) {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, DARK_BKG_COLOR);
            return (LRESULT)g_hbrDarkBkg;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (g_hFont) {
            DeleteObject(g_hFont);
            g_hFont = NULL;
        }
        g_hSettingsWnd = NULL;
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Show/Focus the Settings dialog window
void ShowSettingsDialog(HWND hParentWnd, HINSTANCE hInst) {
    if (g_hSettingsWnd != NULL) {
        // If already open, bring to foreground
        SetForegroundWindow(g_hSettingsWnd);
        return;
    }

    const wchar_t CLASS_NAME[] = L"AMDDDCTraySettingsWindow";
    static bool classRegistered = false;

    if (!classRegistered) {
        WNDCLASSEXW wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.lpfnWndProc = SettingsWndProc;
        wcex.hInstance = hInst;
        wcex.lpszClassName = CLASS_NAME;
        if (IsWindowsDarkModeActive()) {
            EnsureDarkBrushes();
            wcex.hbrBackground = g_hbrDarkBkg;
        } else {
            wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        }
        wcex.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wcex.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        wcex.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        RegisterClassExW(&wcex);
        classRegistered = true;
    }

    // Scale initial window size based on system DPI
    UINT dpi = GetWindowDpi(GetDesktopWindow());
    int w = ScaleDpi(400, dpi);
    int h = ScaleDpi(290, dpi);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    g_hSettingsWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        CLASS_NAME,
        L"AMD DDC Input Switcher Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h,
        hParentWnd,
        NULL,
        hInst,
        NULL
    );

    ShowWindow(g_hSettingsWnd, SW_SHOW);
    UpdateWindow(g_hSettingsWnd);
    SetForegroundWindow(g_hSettingsWnd);
}

// Hidden Listener Window Procedure
LRESULT CALLBACK HiddenWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static HINSTANCE hInst = NULL;

    switch (message) {
    case WM_CREATE: {
        CREATESTRUCT* pcs = (CREATESTRUCT*)lParam;
        hInst = pcs->hInstance;
        break;
    }

    case WM_SETTINGCHANGE: {
        if (lParam == 0 || wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
            ApplyThemePreference();
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();

            struct MenuItemInput {
                UINT id;
                unsigned int value;
                std::wstring name;
            };

            std::vector<MenuItemInput> menuInputs = {
                { 1100, 0xD0, L"DP1" },
                { 1101, 0xD1, L"USB-C" },
                { 1102, 0x90, L"HDMI1" },
                { 1103, 0x91, L"HDMI2" }
            };

            // Check if current input_value is custom
            bool isCustom = true;
            for (const auto& item : menuInputs) {
                if (item.value == g_settings.input_value) {
                    isCustom = false;
                    break;
                }
            }

            if (isCustom) {
                wchar_t buf[16];
                swprintf_s(buf, L"0x%X", g_settings.input_value);
                menuInputs.push_back({ 1104, g_settings.input_value, buf });
            }

            for (const auto& item : menuInputs) {
                std::wstring text = item.name;
                UINT flags = MF_STRING;
                if (item.value == g_settings.input_value) {
                    if (g_settings.hotkey_vk != 0) {
                        std::wstring hkText = FormatHotkeyString(g_settings.hotkey_vk, g_settings.hotkey_mod);
                        if (!hkText.empty()) {
                            text += L"\t" + hkText;
                        }
                    }
                }
                AppendMenuW(hMenu, flags, item.id, text.c_str());
            }

            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 1002, L"Settings...");
            AppendMenuW(hMenu, MF_STRING, 1003, L"Exit");

            SetForegroundWindow(hWnd);
            int trackResult = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);

            if (trackResult >= 1100 && trackResult <= 1104) {
                unsigned int targetVal = 0;
                for (const auto& item : menuInputs) {
                    if (item.id == (UINT)trackResult) {
                        targetVal = item.value;
                        break;
                    }
                }
                ExecuteSwitchAsync(g_settings.i2c_subaddress, targetVal, g_settings.adapter_index, g_settings.display_index);
            } else if (trackResult == 1002) {
                ShowSettingsDialog(hWnd, hInst);
            } else if (trackResult == 1003) {
                PostQuitMessage(0);
            }
        } else if (lParam == WM_LBUTTONDBLCLK) {
            ShowSettingsDialog(hWnd, hInst);
        }
        break;
    }

    case WM_HOTKEY: {
        if (wParam == 1) {
            ExecuteSwitchAsync(g_settings.i2c_subaddress, g_settings.input_value, g_settings.adapter_index, g_settings.display_index);
        }
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Main execution function
int RunTrayApp(HINSTANCE hInstance) {
    // Set DPI Awareness context (Per-Monitor Aware V2) dynamically
    typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContextProc pSetProcessDpiAwarenessContext =
            (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetProcessDpiAwarenessContext) {
            pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    // Initialize Common Controls (Hotkey & standard classes)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // Initialize ADL
    if (!InitADL()) {
        MessageBoxW(NULL, L"Failed to initialize AMD Display Library (ADL). Make sure you have an AMD GPU installed with active display drivers.", L"ADL Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Apply initial Dark/Light theme mode preference
    ApplyThemePreference();

    // Load Settings
    g_settings = LoadTraySettings();

    // Register Hidden Window
    const wchar_t CLASS_NAME[] = L"AMDDDCTrayHiddenWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    // Create a hidden top-level window so it can receive system-wide broadcasts (e.g. WM_SETTINGCHANGE)
    g_hHiddenWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        CLASS_NAME,
        L"AMD DDC Monitor Switcher Listener",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, // No parent (top-level)
        NULL,
        hInstance,
        NULL
    );

    if (g_hHiddenWnd == NULL) {
        FreeADL();
        return 1;
    }

    // Opt the menu owner window into dark mode so tray popup menus render dark.
    AllowDarkModeForWindow(g_hHiddenWnd);

    // Setup System Tray Icon
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hHiddenWnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    // Load custom application icon from resources (specifically requested 16x16 size for the tray)
    nid.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wcscpy_s(nid.szTip, L"AMD DDC Input Switcher");

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Register Global Hotkey
    RegisterGlobalHotkey(g_hHiddenWnd, g_settings);

    // Message Loop (Handles Dialog Messages for Keyboard Tab Navigation)
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (g_hSettingsWnd == NULL || !IsDialogMessageW(g_hSettingsWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Clean up
    Shell_NotifyIconW(NIM_DELETE, &nid);
    UnregisterHotKey(g_hHiddenWnd, 1);
    FreeADL();

    return (int)msg.wParam;
}
