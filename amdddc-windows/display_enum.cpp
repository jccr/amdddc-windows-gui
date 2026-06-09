#include "display_enum.h"
#include "adl.h"
#include <windows.h>

// Enumerate connected and active displays from ADL SDK
std::vector<DetectedDisplay> EnumerateDisplays() {
    std::vector<DetectedDisplay> list;

    int iNumberAdapters = 0;
    if (!InitADL()) {
        return list;
    }

    adlprocs.ADL_Adapter_NumberOfAdapters_Get(&iNumberAdapters);
    if (iNumberAdapters <= 0) {
        return list;
    }

    LPAdapterInfo lpInfo = (LPAdapterInfo)malloc(sizeof(AdapterInfo) * iNumberAdapters);
    if (!lpInfo) {
        return list;
    }
    memset(lpInfo, '\0', sizeof(AdapterInfo) * iNumberAdapters);
    adlprocs.ADL_Adapter_AdapterInfo_Get(lpInfo, sizeof(AdapterInfo) * iNumberAdapters);

    for (int i = 0; i < iNumberAdapters; i++) {
        int iAdapterIndex = lpInfo[i].iAdapterIndex;
        int iNumberDisplays = 0;
        LPADLDisplayInfo lpDisp = nullptr;

        int ADL_Err = adlprocs.ADL_Display_DisplayInfo_Get(iAdapterIndex, &iNumberDisplays, &lpDisp, 0);

        if (ADL_OK == ADL_Err && lpDisp != nullptr) {
            for (int j = 0; j < iNumberDisplays; j++) {
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
                    }
                }
            }
            ADL_Main_Memory_Free((void**)&lpDisp);
        }
    }
    free(lpInfo);
    return list;
}
