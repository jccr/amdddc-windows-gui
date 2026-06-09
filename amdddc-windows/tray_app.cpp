#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "tray_app.h"
#include "adl.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

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

// Convert Hotkey Control Modifiers to RegisterHotKey Modifiers
UINT TranslateModifiersFromHK(BYTE hkMod) {
    UINT rMod = 0;
    if (hkMod & HOTKEYF_ALT)     rMod |= MOD_ALT;
    if (hkMod & HOTKEYF_CONTROL) rMod |= MOD_CONTROL;
    if (hkMod & HOTKEYF_SHIFT)   rMod |= MOD_SHIFT;
    if (hkMod & HOTKEYF_EXT)     rMod |= MOD_WIN;
    return rMod;
}

// Convert RegisterHotKey Modifiers to Hotkey Control Modifiers
BYTE TranslateModifiersToHK(UINT rMod) {
    BYTE hkMod = 0;
    if (rMod & MOD_ALT)     hkMod |= HOTKEYF_ALT;
    if (rMod & MOD_CONTROL) hkMod |= HOTKEYF_CONTROL;
    if (rMod & MOD_SHIFT)   hkMod |= HOTKEYF_SHIFT;
    if (rMod & MOD_WIN)     hkMod |= HOTKEYF_EXT;
    return hkMod;
}

// Settings Dialog / Window Procedure
LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // Create Segoe UI Font
        g_hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Target Display Group
        HWND hGrpDisplay = CreateWindowExW(0, L"BUTTON", L"Target Display", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 10, 365, 100, hWnd, (HMENU)ID_GRP_DISPLAY, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Select Display:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 30, 100, 18, hWnd, (HMENU)ID_LBL_DISPLAY, NULL, NULL);

        HWND hCbDisplay = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            20, 50, 345, 150, hWnd, (HMENU)ID_CB_DISPLAY, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"I2C Source Address:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 80, 120, 18, hWnd, (HMENU)ID_LBL_I2C, NULL, NULL);

        HWND hCbI2C = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP,
            150, 77, 100, 100, hWnd, (HMENU)ID_CB_I2C, NULL, NULL);

        // Switch Command Group
        CreateWindowExW(0, L"BUTTON", L"Switch Command", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 120, 365, 105, hWnd, (HMENU)ID_GRP_SWITCH, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Target Input:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 140, 100, 18, hWnd, (HMENU)ID_LBL_INPUT, NULL, NULL);

        HWND hCbInput = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            20, 160, 345, 150, hWnd, (HMENU)ID_CB_INPUT, NULL, NULL);

        HWND hLblCustom = CreateWindowExW(0, L"STATIC", L"Custom Value (Hex):", WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 193, 120, 18, hWnd, (HMENU)ID_LBL_CUSTOM, NULL, NULL);

        HWND hTxtCustom = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            150, 190, 100, 20, hWnd, (HMENU)ID_TXT_CUSTOM, NULL, NULL);

        // Global Hotkey Group
        CreateWindowExW(0, L"BUTTON", L"Global Hotkey", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            10, 235, 365, 70, hWnd, (HMENU)ID_GRP_HOTKEY, NULL, NULL);

        CreateWindowExW(0, L"STATIC", L"Record hotkey to trigger input switch:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 252, 340, 18, hWnd, (HMENU)ID_LBL_HOTKEY, NULL, NULL);

        HWND hHkHotkey = CreateWindowExW(0, HOTKEY_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            20, 272, 345, 22, hWnd, (HMENU)ID_HK_HOTKEY, NULL, NULL);

        // Action Buttons
        CreateWindowExW(0, L"BUTTON", L"Test Switch", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            10, 320, 100, 26, hWnd, (HMENU)ID_BTN_TEST, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            175, 320, 95, 26, hWnd, (HMENU)ID_BTN_SAVE, NULL, NULL);

        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            280, 320, 95, 26, hWnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);

        // Apply Segoe UI Font to all child controls
        EnumChildWindows(hWnd, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            return TRUE;
        }, 0);

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
        SendMessageW(hCbInput, CB_ADDSTRING, 0, (LPARAM)L"Custom Input (Hex)...");

        int inputSelect = 4; // Default to custom
        if (g_settings.input_value == 0xD0) inputSelect = 0;
        else if (g_settings.input_value == 0xD1) inputSelect = 1;
        else if (g_settings.input_value == 0x90) inputSelect = 2;
        else if (g_settings.input_value == 0x91) inputSelect = 3;

        SendMessageW(hCbInput, CB_SETCURSEL, inputSelect, 0);

        if (inputSelect == 4) {
            wchar_t valStr[16];
            swprintf_s(valStr, L"0x%X", g_settings.input_value);
            SetWindowTextW(hTxtCustom, valStr);
            EnableWindow(hTxtCustom, TRUE);
            EnableWindow(hLblCustom, TRUE);
        } else {
            EnableWindow(hTxtCustom, FALSE);
            EnableWindow(hLblCustom, FALSE);
        }

        // Setup Hotkey Control
        if (g_settings.hotkey_vk != 0) {
            BYTE hkMod = TranslateModifiersToHK(g_settings.hotkey_mod);
            SendMessageW(hHkHotkey, HKM_SETHOTKEY, MAKEWORD(g_settings.hotkey_vk, hkMod), 0);
        }

        break;
    }

    case WM_COMMAND: {
        HWND hTxtCustom = GetDlgItem(hWnd, ID_TXT_CUSTOM);
        HWND hLblCustom = GetDlgItem(hWnd, ID_LBL_CUSTOM);

        if (LOWORD(wParam) == ID_CB_INPUT && HIWORD(wParam) == CBN_SELCHANGE) {
            HWND hCbInput = (HWND)lParam;
            int sel = (int)SendMessageW(hCbInput, CB_GETCURSEL, 0, 0);
            if (sel == 4) { // Custom
                EnableWindow(hTxtCustom, TRUE);
                EnableWindow(hLblCustom, TRUE);
            } else {
                EnableWindow(hTxtCustom, FALSE);
                EnableWindow(hLblCustom, FALSE);
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

                unsigned int inputVal = 0xD1;
                int inputSel = (int)SendMessageW(hCbInput, CB_GETCURSEL, 0, 0);
                if (inputSel == 0) inputVal = 0xD0;
                else if (inputSel == 1) inputVal = 0xD1;
                else if (inputSel == 2) inputVal = 0x90;
                else if (inputSel == 3) inputVal = 0x91;
                else {
                    wchar_t valBuf[32];
                    GetWindowTextW(hTxtCustom, valBuf, 32);
                    inputVal = wcstoul(valBuf, nullptr, 16);
                }

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

            int inputSel = (int)SendMessageW(hCbInput, CB_GETCURSEL, 0, 0);
            if (inputSel == 0) g_settings.input_value = 0xD0;
            else if (inputSel == 1) g_settings.input_value = 0xD1;
            else if (inputSel == 2) g_settings.input_value = 0x90;
            else if (inputSel == 3) g_settings.input_value = 0x91;
            else {
                wchar_t valBuf[32];
                GetWindowTextW(hTxtCustom, valBuf, 32);
                g_settings.input_value = wcstoul(valBuf, nullptr, 16);
            }

            WORD hk = (WORD)SendMessageW(hHkHotkey, HKM_GETHOTKEY, 0, 0);
            g_settings.hotkey_vk = LOBYTE(hk);
            g_settings.hotkey_mod = TranslateModifiersFromHK(HIBYTE(hk));

            // Save to settings.ini
            SaveTraySettings(g_settings);

            // Re-register hotkey
            RegisterGlobalHotkey(g_hHiddenWnd, g_settings);

            DestroyWindow(hWnd);
        }
        break;
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
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = CLASS_NAME;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        RegisterClassW(&wc);
        classRegistered = true;
    }

    // Dialog layout coordinates
    int w = 400;
    int h = 400;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - w) / 2;
    int y = (screenHeight - h) / 2;

    g_hSettingsWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        CLASS_NAME,
        L"AMD DDC Input Switcher Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
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

    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1001, L"Switch Input Now");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 1002, L"Settings...");
            AppendMenuW(hMenu, MF_STRING, 1003, L"Exit");

            SetForegroundWindow(hWnd);
            int trackResult = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);

            if (trackResult == 1001) {
                ExecuteSwitchAsync(g_settings.i2c_subaddress, g_settings.input_value, g_settings.adapter_index, g_settings.display_index);
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

    // Load Settings
    g_settings = LoadTraySettings();

    // Register Hidden Window
    const wchar_t CLASS_NAME[] = L"AMDDDCTrayHiddenWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    g_hHiddenWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"AMD DDC Monitor Switcher Listener",
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        HWND_MESSAGE, // Hidden Message-only window
        NULL,
        hInstance,
        NULL
    );

    if (g_hHiddenWnd == NULL) {
        FreeADL();
        return 1;
    }

    // Setup System Tray Icon
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hHiddenWnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    // Load standard application icon
    nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
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
