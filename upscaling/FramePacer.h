#pragma once
#include <windows.h>
#include <algorithm>

namespace NeuralUpscale {
// Keep the game's CPU/GPU from rebuilding hundreds of frames for a 60 Hz window.
// A high-resolution waitable timer avoids changing the process-wide timer period.
class FramePacer {
    HANDLE timer_ = nullptr;
    double next_ = 0;
    unsigned rate_ = 0;
public:
    static double Now() {
        static const double frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
        LARGE_INTEGER count; QueryPerformanceCounter(&count);
        return count.QuadPart * 1000. / frequency;
    }
    ~FramePacer() { if (timer_) CloseHandle(timer_); }
    void Reset() { next_ = 0; rate_ = 0; }
    void Wait(unsigned rate) {
        if (!rate) { Reset(); return; }
        const double interval = 1000. / rate;
        double now = Now();
        if (rate != rate_ || !next_) { rate_ = rate; next_ = now + interval; return; }
        if (!timer_) {
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* HIGH_RESOLUTION */,
                TIMER_MODIFY_STATE | SYNCHRONIZE);
            if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        }
        if (next_ > now && timer_) {
            LARGE_INTEGER due; due.QuadPart = -static_cast<LONGLONG>((next_ - now) * 10000.);
            if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(timer_, static_cast<DWORD>(interval * 2 + 10));
        }
        now = Now();
        // Do not render a burst of catch-up frames after a loading/dragging stall.
        next_ = now > next_ + interval ? now + interval : next_ + interval;
    }
};
}
