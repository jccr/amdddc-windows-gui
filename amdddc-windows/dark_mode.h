#pragma once
#ifndef DARK_MODE_H
#define DARK_MODE_H

#include <windows.h>

// Dark theme colors for the settings window.
extern const COLORREF DARK_BKG_COLOR;
extern const COLORREF DARK_CTRL_COLOR;
extern const COLORREF DARK_TEXT_COLOR;

// Brushes used to paint the dark settings window (created by EnsureDarkBrushes).
extern HBRUSH g_hbrDarkBkg;   // settings window background
extern HBRUSH g_hbrDarkCtrl;  // edit/combo/list control background

// True when Windows is configured to use dark mode for applications.
bool IsWindowsDarkModeActive();

// Set the process-wide preferred app mode and refresh menu theming so that
// system-drawn popup menus follow the current Windows theme.
void ApplyThemePreference();

// Opt a specific window into dark mode (menus owned by it, and themed controls).
void AllowDarkModeForWindow(HWND hWnd);

// Lazily create the dark brushes (no-op if already created).
void EnsureDarkBrushes();

// Paint the window title bar dark via DWM (no-op on light mode / older Windows).
void ApplyDarkTitleBar(HWND hWnd);

// Apply the dark immersive theme to every child control of the given window.
void ApplyDarkThemeToControls(HWND hWnd);

#endif // !DARK_MODE_H
