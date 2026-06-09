#pragma once
#ifndef SWITCH_INPUT_H
#define SWITCH_INPUT_H

// Run the DDC/CI input switch command on a background worker thread.
void ExecuteSwitchAsync(unsigned int i2c_subaddress, unsigned int input_value, int adapter_index, int display_index);

#endif // !SWITCH_INPUT_H
