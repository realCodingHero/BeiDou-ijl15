#include "FramePacer.h"
#include <cstdio>
int main() {
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
    puts("PASS frame pacing: 60 fps timing, unlimited mode, no catch-up burst after stall");
}
