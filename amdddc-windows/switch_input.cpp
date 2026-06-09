#include "switch_input.h"
#include "adl.h"
#include <thread>

#define VCP_CODE_SWITCH_INPUT 0xF4

// Declared in amdddc-windows.cpp
extern void vSetVcpCommand(unsigned int subaddress, unsigned char ucVcp, unsigned int ulVal, int iAdapterIndex, int iDisplayIndex);

// Run the DDC/CI Input Switch command on a background worker thread (delay = 5 seconds)
void ExecuteSwitchAsync(unsigned int i2c_subaddress, unsigned int input_value, int adapter_index, int display_index) {
    std::thread t([=]() {
        if (InitADL()) {
            vSetVcpCommand(i2c_subaddress, VCP_CODE_SWITCH_INPUT, input_value, adapter_index, display_index);
        }
    });
    t.detach();
}
