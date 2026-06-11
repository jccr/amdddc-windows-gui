#include "usb_monitor.h"
#include "tray_app.h"
#include "switch_input.h"
#include <initguid.h>
#include <windows.h>
#include <dbt.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <vector>
#include <string>

#pragma comment(lib, "cfgmgr32.lib")

// {A5DCBF10-6530-11D2-901F-00C04FB951ED}
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE, 0xA5DCBF10, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);
// {f18a0e88-c30c-11d0-8815-00a0c906bed8}
DEFINE_GUID(GUID_DEVINTERFACE_USB_HUB, 0xf18a0e88, 0xc30c, 0x11d0, 0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8);

static std::vector<HDEVNOTIFY> g_hUsbNotificationHandles;

void ParseVidPid(const std::wstring& path, std::wstring& vid, std::wstring& pid) {
    std::wstring pathUpper = path;
    for (auto& c : pathUpper) {
        c = towupper(c);
    }

    size_t vidPos = pathUpper.find(L"VID_");
    if (vidPos != std::wstring::npos && vidPos + 8 <= pathUpper.length()) {
        vid = pathUpper.substr(vidPos + 4, 4);
    }

    size_t pidPos = pathUpper.find(L"PID_");
    if (pidPos != std::wstring::npos && pidPos + 8 <= pathUpper.length()) {
        pid = pathUpper.substr(pidPos + 4, 4);
    }
}

static std::wstring GetDevNodePropertyString(DEVINST devInst, const DEVPROPKEY* propertyKey) {
    DEVPROPTYPE propType;
    ULONG bufferSize = 0;

    CONFIGRET cr = CM_Get_DevNode_PropertyW(
        devInst,
        propertyKey,
        &propType,
        NULL,
        &bufferSize,
        0
    );

    if (cr == CR_BUFFER_SMALL || cr == CR_SUCCESS) {
        std::vector<BYTE> buffer(bufferSize);
        cr = CM_Get_DevNode_PropertyW(
            devInst,
            propertyKey,
            &propType,
            buffer.data(),
            &bufferSize,
            0
        );
        if (cr == CR_SUCCESS && propType == DEVPROP_TYPE_STRING) {
            std::wstring value((wchar_t*)buffer.data());
            return value;
        }
    }
    return L"";
}

static std::wstring GetDeviceFriendlyName(const std::wstring& interfacePath) {
    ULONG bufSize = 0;
    DEVPROPTYPE propType;

    // 1. Get the Device Instance ID string associated with this interface
    CM_Get_Device_Interface_PropertyW(
        interfacePath.c_str(),
        &DEVPKEY_Device_InstanceId,
        &propType,
        nullptr,
        &bufSize,
        0
    );

    if (bufSize == 0) return L"Unknown Device";

    std::vector<BYTE> buffer(bufSize);
    CONFIGRET cr = CM_Get_Device_Interface_PropertyW(
        interfacePath.c_str(),
        &DEVPKEY_Device_InstanceId,
        &propType,
        buffer.data(),
        &bufSize,
        0
    );

    if (cr != CR_SUCCESS) return L"Unknown Device";

    // 2. Locate the DevNode using the Instance ID (CM_LOCATE_DEVNODE_PHANTOM to support devices in transition)
    DEVINST devInst = 0;
    cr = CM_Locate_DevNodeW(&devInst, (DEVINSTID)buffer.data(), CM_LOCATE_DEVNODE_PHANTOM);
    if (cr != CR_SUCCESS) return L"Unknown Device";

    // 3. Try to query the properties of the device node
    // A. Friendly Name
    std::wstring friendlyName = GetDevNodePropertyString(devInst, &DEVPKEY_Device_FriendlyName);
    if (!friendlyName.empty()) return friendlyName;

    // B. Bus Reported Device Description (reported directly by hardware)
    std::wstring busDesc = GetDevNodePropertyString(devInst, &DEVPKEY_Device_BusReportedDeviceDesc);
    if (!busDesc.empty()) return busDesc;

    // C. Device Description (generic Windows fallback description)
    std::wstring deviceDesc = GetDevNodePropertyString(devInst, &DEVPKEY_Device_DeviceDesc);
    if (!deviceDesc.empty()) return deviceDesc;

    return L"Unknown Device";
}

bool InitializeUsbMonitor(HWND hWnd) {
    DEV_BROADCAST_DEVICEINTERFACE_W notificationFilter;
    ZeroMemory(&notificationFilter, sizeof(notificationFilter));
    notificationFilter.dbcc_size = sizeof(notificationFilter);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    // Register GUID_DEVINTERFACE_USB_HUB (for Hubs)
    notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_HUB;
    HDEVNOTIFY hNotifyHub = RegisterDeviceNotificationW(
        hWnd,
        &notificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );
    if (hNotifyHub) {
        g_hUsbNotificationHandles.push_back(hNotifyHub);
    }

    // Register GUID_DEVINTERFACE_USB_DEVICE (for normal USB devices)
    notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;
    HDEVNOTIFY hNotifyDev = RegisterDeviceNotificationW(
        hWnd,
        &notificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );
    if (hNotifyDev) {
        g_hUsbNotificationHandles.push_back(hNotifyDev);
    }

    return !g_hUsbNotificationHandles.empty();
}

void CleanupUsbMonitor() {
    for (HDEVNOTIFY hNotify : g_hUsbNotificationHandles) {
        if (hNotify) {
            UnregisterDeviceNotification(hNotify);
        }
    }
    g_hUsbNotificationHandles.clear();
}

void HandleUsbDeviceChange(WPARAM wParam, LPARAM lParam) {
    if (wParam != DBT_DEVICEARRIVAL && wParam != DBT_DEVICEREMOVECOMPLETE) {
        return;
    }

    if (lParam == 0) {
        return;
    }

    PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
    if (pHdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) {
        return;
    }

    PDEV_BROADCAST_DEVICEINTERFACE_W pDevInterface = (PDEV_BROADCAST_DEVICEINTERFACE_W)pHdr;
    std::wstring devName = pDevInterface->dbcc_name;

    // Case-insensitive match check helper for device paths
    auto pathsMatch = [](const std::wstring& p1, const std::wstring& p2) -> bool {
        if (p1.length() != p2.length()) return false;
        for (size_t i = 0; i < p1.length(); i++) {
            if (towupper(p1[i]) != towupper(p2[i])) return false;
        }
        return true;
    };

    // Check if this matches the bound device path in settings
    if (!g_settings.usb_device_path.empty() && pathsMatch(devName, g_settings.usb_device_path)) {
        if (wParam == DBT_DEVICEARRIVAL && g_settings.usb_arrival_input != 0) {
            ExecuteSwitchAsync(g_settings.i2c_subaddress, g_settings.usb_arrival_input, g_settings.adapter_index, g_settings.display_index);
        } else if (wParam == DBT_DEVICEREMOVECOMPLETE && g_settings.usb_removal_input != 0) {
            ExecuteSwitchAsync(g_settings.i2c_subaddress, g_settings.usb_removal_input, g_settings.adapter_index, g_settings.display_index);
        }
    }
}

std::vector<UsbDeviceInfo> EnumerateConnectedUsbDevices() {
    std::vector<UsbDeviceInfo> devices;

    auto addFromGuid = [&](const GUID& guid) {
        ULONG bufferLen = 0;
        CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(
            &bufferLen,
            const_cast<GUID*>(&guid),
            NULL,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT
        );
        if (cr == CR_SUCCESS && bufferLen > 0) {
            std::vector<wchar_t> buffer(bufferLen);
            cr = CM_Get_Device_Interface_ListW(
                const_cast<GUID*>(&guid),
                NULL,
                buffer.data(),
                bufferLen,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT
            );
            if (cr == CR_SUCCESS) {
                wchar_t* p = buffer.data();
                while (*p) {
                    std::wstring path(p);
                    p += path.length() + 1;

                    std::wstring vid = L"";
                    std::wstring pid = L"";
                    ParseVidPid(path, vid, pid);

                    if (!vid.empty() && !pid.empty()) {
                        std::wstring friendlyName = GetDeviceFriendlyName(path);
                        std::wstring label = friendlyName + L" (VID:" + vid + L", PID:" + pid + L")";
                        // Prevent duplicates
                        bool found = false;
                        for (const auto& dev : devices) {
                            if (dev.path == path) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            devices.push_back({ path, label, friendlyName });
                        }
                    }
                }
            }
        }
    };

    addFromGuid(GUID_DEVINTERFACE_USB_DEVICE);
    addFromGuid(GUID_DEVINTERFACE_USB_HUB);

    return devices;
}
