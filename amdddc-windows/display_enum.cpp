#include "display_enum.h"
#include "adl.h"
#include <windows.h>
#include <cstdio>

// Enumerate connected and active displays from ADL SDK
std::vector<DetectedDisplay> EnumerateDisplays() {
    std::vector<DetectedDisplay> list;
    FILE* dbg = nullptr;
    fopen_s(&dbg, "C:\\Users\\jccr\\Projects\\AMDLG-InputSwitch\\tray_debug.log", "w");
    if (dbg) {
        fprintf(dbg, "EnumerateDisplays called\n");
    }

    int iNumberAdapters = 0;
    if (!InitADL()) {
        if (dbg) {
            fprintf(dbg, "InitADL failed\n");
            fclose(dbg);
        }
        return list;
    }

    adlprocs.ADL_Adapter_NumberOfAdapters_Get(&iNumberAdapters);
    if (dbg) {
        fprintf(dbg, "iNumberAdapters: %d\n", iNumberAdapters);
    }
    if (iNumberAdapters <= 0) {
        if (dbg) fclose(dbg);
        return list;
    }

    LPAdapterInfo lpInfo = (LPAdapterInfo)malloc(sizeof(AdapterInfo) * iNumberAdapters);
    if (!lpInfo) {
        if (dbg) fclose(dbg);
        return list;
    }
    memset(lpInfo, '\0', sizeof(AdapterInfo) * iNumberAdapters);
    adlprocs.ADL_Adapter_AdapterInfo_Get(lpInfo, sizeof(AdapterInfo) * iNumberAdapters);

    for (int i = 0; i < iNumberAdapters; i++) {
        int iAdapterIndex = lpInfo[i].iAdapterIndex;
        int iNumberDisplays = 0;
        LPADLDisplayInfo lpDisp = nullptr;

        int ADL_Err = adlprocs.ADL_Display_DisplayInfo_Get(iAdapterIndex, &iNumberDisplays, &lpDisp, 0);
        if (dbg) {
            fprintf(dbg, "Adapter %d (Index: %d) Name: %s, ADL_Err: %d, iNumberDisplays: %d\n",
                i, iAdapterIndex, lpInfo[i].strAdapterName, ADL_Err, iNumberDisplays);
        }

        if (ADL_OK == ADL_Err && lpDisp != nullptr) {
            for (int j = 0; j < iNumberDisplays; j++) {
                if (dbg) {
                    fprintf(dbg, "  Display %d: Name: %s, LogicalAdapterIndex: %d, LogicalIndex: %d, InfoValue: %d\n",
                        j, lpDisp[j].strDisplayName,
                        lpDisp[j].displayID.iDisplayLogicalAdapterIndex,
                        lpDisp[j].displayID.iDisplayLogicalIndex,
                        lpDisp[j].iDisplayInfoValue);
                }

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
                        if (dbg) {
                            fprintf(dbg, "    -> Display added successfully!\n");
                        }
                    }
                }
            }
            ADL_Main_Memory_Free((void**)&lpDisp);
        }
    }
    free(lpInfo);
    if (dbg) {
        fprintf(dbg, "EnumerateDisplays finished, returning %zu displays\n", list.size());
        fclose(dbg);
    }
    return list;
}
