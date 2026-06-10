#include "settings_dialog.h"
#include "tray_app.h"
#include "tray_settings.h"
#include "dark_mode.h"
#include "dpi.h"
#include "display_enum.h"
#include "switch_input.h"
#include "hotkey.h"
#include "resource.h"
#include <commctrl.h>
#include <vector>
#include <string>

#pragma comment(lib, "comctl32.lib")

HWND g_hSettingsWnd = NULL;
static HFONT g_hFont = NULL;

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

// Structure to keep track of current recorded hotkey
struct RecordedHotkey {
    WORD vk = 0;
    WORD mod = 0;
};
static RecordedHotkey g_recordedHotkey;

// Working copy of the per-input hotkeys edited while the dialog is open. The single
// "Global Hotkey" field reflects whichever target input is currently selected; switching
// the input commits the field to its input and loads the newly-selected input's hotkey.
static std::vector<HotkeyBinding> g_workingHotkeys;
static unsigned int g_currentHotkeyInput = 0;

// Store the recorded hotkey for a given input in the working copy (vk == 0 clears it).
static void CommitHotkey(unsigned int input, WORD vk, WORD mod) {
    for (auto& hk : g_workingHotkeys) {
        if (hk.input_value == input) {
            hk.vk = vk;
            hk.mod = mod;
            return;
        }
    }
    g_workingHotkeys.push_back({ input, vk, mod });
}

// Read the stored hotkey for an input from the working copy (0/0 if none).
static void LoadHotkeyFor(unsigned int input, WORD& vk, WORD& mod) {
    for (const auto& hk : g_workingHotkeys) {
        if (hk.input_value == input) {
            vk = (WORD)hk.vk;
            mod = (WORD)hk.mod;
            return;
        }
    }
    vk = 0;
    mod = 0;
}

// Commit the current hotkey field to its input and load the hotkey for the newly-selected input.
static void SwitchToInput(HWND hWnd, unsigned int newInput) {
    if (newInput == g_currentHotkeyInput) return;
    CommitHotkey(g_currentHotkeyInput, g_recordedHotkey.vk, g_recordedHotkey.mod);
    g_currentHotkeyInput = newInput;
    LoadHotkeyFor(newInput, g_recordedHotkey.vk, g_recordedHotkey.mod);
    SetWindowTextW(GetDlgItem(hWnd, ID_HK_HOTKEY),
        FormatHotkeyString(g_recordedHotkey.vk, g_recordedHotkey.mod).c_str());
}

// Read the currently-shown target input value from the input combobox.
static unsigned int ReadInputComboValue(HWND hWnd) {
    wchar_t valBuf[64];
    GetWindowTextW(GetDlgItem(hWnd, ID_CB_INPUT), valBuf, 64);
    return wcstoul(valBuf, nullptr, 16);
}

// Subclass window procedure for the hotkey edit control
static LRESULT CALLBACK HotkeyEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
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

static void UpdateLayout(HWND hWnd, UINT dpi) {
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
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
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

        // Setup Hotkey Control: load the per-input hotkeys and show the one for the
        // currently-selected target input.
        g_workingHotkeys = g_settings.hotkeys;
        g_currentHotkeyInput = g_settings.input_value;
        LoadHotkeyFor(g_currentHotkeyInput, g_recordedHotkey.vk, g_recordedHotkey.mod);
        std::wstring hkText = FormatHotkeyString(g_recordedHotkey.vk, g_recordedHotkey.mod);
        SetWindowTextW(hHkHotkey, hkText.c_str());

        break;
    }

    case WM_COMMAND: {
        // Target input changed: move the hotkey field to the newly-selected input.
        if (LOWORD(wParam) == ID_CB_INPUT) {
            WORD code = HIWORD(wParam);
            if (code == CBN_SELCHANGE) {
                HWND hCbInput = GetDlgItem(hWnd, ID_CB_INPUT);
                int sel = (int)SendMessageW(hCbInput, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    wchar_t buf[64];
                    SendMessageW(hCbInput, CB_GETLBTEXT, sel, (LPARAM)buf);
                    SwitchToInput(hWnd, wcstoul(buf, nullptr, 16));
                }
            } else if (code == CBN_KILLFOCUS) {
                SwitchToInput(hWnd, ReadInputComboValue(hWnd));
            }
        }

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

            int dispSel = (int)SendMessageW(hCbDisplay, CB_GETCURSEL, 0, 0);
            if (dispSel != CB_ERR) {
                LPARAM data = SendMessageW(hCbDisplay, CB_GETITEMDATA, dispSel, 0);
                g_settings.adapter_index = LOWORD(data);
                g_settings.display_index = HIWORD(data);
            }

            wchar_t i2cBuf[32];
            GetWindowTextW(hCbI2C, i2cBuf, 32);
            g_settings.i2c_subaddress = wcstoul(i2cBuf, nullptr, 16);

            g_settings.input_value = ReadInputComboValue(hWnd);

            // Commit the hotkey shown in the field to whichever input it belongs to,
            // then persist all bindings (dropping any that were cleared).
            CommitHotkey(g_currentHotkeyInput, g_recordedHotkey.vk, g_recordedHotkey.mod);
            g_settings.hotkeys.clear();
            for (const auto& hk : g_workingHotkeys) {
                if (hk.vk != 0) g_settings.hotkeys.push_back(hk);
            }

            // Save to settings.ini
            SaveTraySettings(g_settings);

            // Re-register hotkeys
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
