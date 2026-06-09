#include "dark_mode.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

const COLORREF DARK_BKG_COLOR = RGB(32, 32, 32);
const COLORREF DARK_CTRL_COLOR = RGB(50, 50, 50);
const COLORREF DARK_TEXT_COLOR = RGB(240, 240, 240);

HBRUSH g_hbrDarkBkg = NULL;
HBRUSH g_hbrDarkCtrl = NULL;

// Undocumented uxtheme.dll entry points (accessed by ordinal).
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
static LRESULT CALLBACK DarkGroupBoxSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
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
