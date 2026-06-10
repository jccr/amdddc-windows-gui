#pragma once
#ifndef HOTKEY_H
#define HOTKEY_H

#include <windows.h>
#include <string>
#include "tray_settings.h"

// Hotkey ids 1..MAX_HOTKEY_ID map to the index (+1) of a binding in TraySettings::hotkeys.
#define MAX_HOTKEY_ID 32

// Format a virtual-key + modifier combination as readable text (e.g. "Alt + Win + I").
std::wstring FormatHotkeyString(WORD vk, WORD mod);

// (Re)register every global hotkey described by the settings on the given window.
void RegisterGlobalHotkey(HWND hwnd, const TraySettings& s);

// Unregister all global hotkeys previously registered on the window.
void UnregisterGlobalHotkeys(HWND hwnd);

#endif // !HOTKEY_H
