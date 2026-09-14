#pragma once
#include <d3d9.h>

namespace NeuralUpscale {
struct PresentationPolicy {
    static UINT Interval(unsigned limit) {
        return limit ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    }
    static unsigned TimerRate(unsigned limit, UINT interval, UINT refresh) {
        if (!limit) return 0;
        // Present already limits to the display when the cap reaches its refresh
        // rate. A second free-running timer would periodically miss a vblank.
        // Allow integer reporting of 59.94 Hz as either 59 or 60 Hz.
        if (interval == D3DPRESENT_INTERVAL_ONE && refresh > 1 && limit >= refresh)
            return 0;
        // Retain explicit lower caps, unknown display rates and vsync fallback.
        return limit;
    }
};
}
