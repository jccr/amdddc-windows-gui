#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "tray_app.h"
#include "tray_settings.h"
#include "dark_mode.h"
#include "settings_dialog.h"
#include "switch_input.h"
#include "hotkey.h"
#include "resource.h"
#include "adl.h"
#include <commctrl.h>
#include <vector>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 1

// Global Variables
TraySettings g_settings;
HWND g_hHiddenWnd = NULL;

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

            // Helper to add a custom (non-standard) input value to the menu once.
            UINT nextId = 1104;
            auto addCustom = [&](unsigned int value) {
                for (const auto& item : menuInputs) {
                    if (item.value == value) return;
                }
                wchar_t buf[16];
                swprintf_s(buf, L"0x%X", value);
                menuInputs.push_back({ nextId++, value, buf });
            };

            // Keep the last-selected target input clickable even if it's a custom value.
            addCustom(g_settings.input_value);

            // Add a menu entry for any hotkey-bound input that isn't a known input.
            for (const auto& hk : g_settings.hotkeys) {
                addCustom(hk.input_value);
            }

            for (const auto& item : menuInputs) {
                std::wstring text = item.name;
                // Show the hotkey hint next to whichever input it switches to.
                for (const auto& hk : g_settings.hotkeys) {
                    if (hk.input_value == item.value && hk.vk != 0) {
                        std::wstring hkText = FormatHotkeyString((WORD)hk.vk, (WORD)hk.mod);
                        if (!hkText.empty()) {
                            text += L"\t" + hkText;
                        }
                        break;
                    }
                }
                AppendMenuW(hMenu, MF_STRING, item.id, text.c_str());
            }

            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 1002, L"Settings...");
            AppendMenuW(hMenu, MF_STRING, 1003, L"Exit");

            SetForegroundWindow(hWnd);
            int trackResult = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);

            bool switched = false;
            for (const auto& item : menuInputs) {
                if (item.id == (UINT)trackResult) {
                    ExecuteSwitchAsync(g_settings.i2c_subaddress, item.value, g_settings.adapter_index, g_settings.display_index);
                    switched = true;
                    break;
                }
            }
            if (switched) {
                // handled above
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
        // Hotkey id is the binding index + 1; switch to that binding's target input.
        size_t idx = (size_t)wParam - 1;
        if (wParam >= 1 && idx < g_settings.hotkeys.size()) {
            ExecuteSwitchAsync(g_settings.i2c_subaddress, g_settings.hotkeys[idx].input_value, g_settings.adapter_index, g_settings.display_index);
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
    UnregisterGlobalHotkeys(g_hHiddenWnd);
    FreeADL();

    return (int)msg.wParam;
}
