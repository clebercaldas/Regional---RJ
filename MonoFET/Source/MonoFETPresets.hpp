#pragma once

// Presets de fabrica (pontos de partida para caixa; ajuste de ouvido).
// Filtros em modo Sidechain: HPF tira bumbo/grave do detector, LPF deixa o detector surdo aos pratos.
struct MonoFETPreset {
    const char* name;
    float inDb, outDb, attackUs, releaseMs, ratio, thrDb, lookMs, lookOn, hpfHz, lpfHz, eqMode, compOn, sat;
};

static const MonoFETPreset kMonoFETPresets[] = {
    // nome            in   out  atk   rel  ratio thr   look on  hpf    lpf   eq comp sat
    {"Suave",           6.f, 0.f, 400.f, 600.f,  2.f, -18.f, 1.f, 1.f, 150.f, 4000.f, 0.f, 1.f, 1.f},
    {"Equilibrado",    12.f, 0.f, 200.f, 400.f,  4.f, -18.f, 2.f, 1.f, 175.f, 3500.f, 0.f, 1.f, 1.f},
    {"Agressivo",      18.f, 0.f,  50.f, 150.f, 12.f, -24.f, 3.f, 1.f, 200.f, 3000.f, 0.f, 1.f, 1.f},
};
static const int kMonoFETNumPresets = (int)(sizeof(kMonoFETPresets) / sizeof(kMonoFETPresets[0]));
