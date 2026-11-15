#pragma once

// Presets de fabrica (pontos de partida para caixa; ajuste de ouvido).
struct MonoFETPreset {
    const char* name;
    float inDb, outDb, attackUs, releaseMs, ratio, thrDb, lookMs;
};

static const MonoFETPreset kMonoFETPresets[] = {
    // nome            in   out  atk   rel  ratio thr   look
    {"Suave",           6.f, 0.f, 400.f, 600.f,  2.f, -18.f, 1.f},
    {"Equilibrado",    12.f, 0.f, 200.f, 400.f,  4.f, -18.f, 2.f},
    {"Agressivo",      18.f, 0.f,  50.f, 150.f, 12.f, -24.f, 3.f},
};
static const int kMonoFETNumPresets = (int)(sizeof(kMonoFETPresets) / sizeof(kMonoFETPresets[0]));
