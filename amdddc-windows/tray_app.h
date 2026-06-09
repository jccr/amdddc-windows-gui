#pragma once
#ifndef TRAY_APP_H
#define TRAY_APP_H

#include <windows.h>
#include "tray_settings.h"

// Runtime application state, defined in tray_app.cpp.
extern TraySettings g_settings;   // current effective settings
extern HWND g_hHiddenWnd;         // hidden listener window (tray icon owner)

// Main function to run the system tray application
int RunTrayApp(HINSTANCE hInstance);

#endif // !TRAY_APP_H
