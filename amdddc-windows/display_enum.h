#pragma once
#ifndef DISPLAY_ENUM_H
#define DISPLAY_ENUM_H

#include <vector>
#include <string>

struct DetectedDisplay {
    int adapter_index;
    int display_index;
    std::wstring display_name;
    std::wstring adapter_name;
};

// Enumerate connected and active displays from the ADL SDK.
std::vector<DetectedDisplay> EnumerateDisplays();

#endif // !DISPLAY_ENUM_H
