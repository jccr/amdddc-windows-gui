#pragma once
#ifndef HOTKEY_H
#define HOTKEY_H

#include <windows.h>
#include <string>
#include "tray_settings.h"

// Format a virtual-key + modifier combination as readable text (e.g. "Alt + Win + I").
std::wstring FormatHotkeyString(WORD vk, WORD mod);

// (Re)register the global hotkey described by the settings on the given window.
void RegisterGlobalHotkey(HWND hwnd, const TraySettings& s);

#endif // !HOTKEY_H
