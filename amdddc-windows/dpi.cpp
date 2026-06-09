#include "dpi.h"

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
