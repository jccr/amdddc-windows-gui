#pragma once
#include <windows.h>
#include <vector>
#include <string>

// Initializes USB monitoring for the given window handle.
// Registers for GUID_DEVINTERFACE_USB_HUB and GUID_DEVINTERFACE_USB_DEVICE notifications.
bool InitializeUsbMonitor(HWND hWnd);

// Cleans up the registered device notifications.
void CleanupUsbMonitor();

struct UsbDeviceInfo {
    std::wstring path;
    std::wstring label;
    std::wstring friendly_name;
};

// Handles the WM_DEVICECHANGE message.
void HandleUsbDeviceChange(WPARAM wParam, LPARAM lParam);

// Enumerates all currently connected USB devices.
std::vector<UsbDeviceInfo> EnumerateConnectedUsbDevices();

// Helper to parse VID and PID from a device path or display label.
void ParseVidPid(const std::wstring& path, std::wstring& vid, std::wstring& pid);
