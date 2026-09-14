#include "FramePacer.h"
#include "PresentationPolicy.h"
#include <cstdio>
int main() {
    using NeuralUpscale::PresentationPolicy;
    struct Case { unsigned cap, refresh, interval, timer; };
    const Case cases[]{
        {60,60,D3DPRESENT_INTERVAL_ONE,0}, {60,59,D3DPRESENT_INTERVAL_ONE,0},
        {59,60,D3DPRESENT_INTERVAL_ONE,59}, {60,61,D3DPRESENT_INTERVAL_ONE,60},
        {60,120,D3DPRESENT_INTERVAL_ONE,60}, {30,60,D3DPRESENT_INTERVAL_ONE,30},
        {120,60,D3DPRESENT_INTERVAL_ONE,0}, {240,240,D3DPRESENT_INTERVAL_ONE,0},
        {60,0,D3DPRESENT_INTERVAL_ONE,60}, {60,1,D3DPRESENT_INTERVAL_ONE,60},
        {60,60,D3DPRESENT_INTERVAL_IMMEDIATE,60}, {0,60,D3DPRESENT_INTERVAL_IMMEDIATE,0}
    };
    for (const auto& test : cases) {
        if (PresentationPolicy::TimerRate(test.cap,test.interval,test.refresh)!=test.timer) {
            printf("FAIL pacing policy cap=%u refresh=%u interval=%u\n",test.cap,test.refresh,test.interval);
            return 4;
        }
    }
    if (PresentationPolicy::Interval(60)!=D3DPRESENT_INTERVAL_ONE ||
        PresentationPolicy::Interval(0)!=D3DPRESENT_INTERVAL_IMMEDIATE) return 5;
    NeuralUpscale::FramePacer pacer;
    pacer.Wait(60);
    double start = NeuralUpscale::FramePacer::Now();
    for (int i = 0; i < 30; ++i) pacer.Wait(60);
    double duration = NeuralUpscale::FramePacer::Now() - start;
    if (duration < 470 || duration > 750) { printf("FAIL 60 fps timer: %.2fms\n",duration); return 1; }
    pacer.Wait(0); start = NeuralUpscale::FramePacer::Now();
    for (int i = 0; i < 50; ++i) pacer.Wait(0);
    if (NeuralUpscale::FramePacer::Now()-start > 30) return 2;
    pacer.Wait(60); Sleep(80); pacer.Wait(60); start = NeuralUpscale::FramePacer::Now();
    pacer.Wait(60);
    if (NeuralUpscale::FramePacer::Now()-start < 8) return 3;
    puts("PASS frame pacing: display-sync policy, lower/unknown/fallback caps, 60 fps timer, unlimited, no catch-up burst");
}
