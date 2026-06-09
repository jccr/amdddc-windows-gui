#pragma once
#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include <windows.h>

// Handle of the settings window while it is open (NULL otherwise). Used by the
// main message loop to forward dialog navigation messages.
extern HWND g_hSettingsWnd;

// Show the settings window, or bring it to the foreground if already open.
void ShowSettingsDialog(HWND hParentWnd, HINSTANCE hInst);

#endif // !SETTINGS_DIALOG_H
