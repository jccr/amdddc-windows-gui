#pragma once
#ifndef DPI_H
#define DPI_H

#include <windows.h>

// Per-Monitor DPI helpers used to scale the settings window and its controls.
UINT GetWindowDpi(HWND hwnd);
int ScaleDpi(int val, UINT dpi);
HFONT CreateDpiFont(UINT dpi);

#endif // !DPI_H
