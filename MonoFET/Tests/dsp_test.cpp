#include "../Source/MonoFETProcessor.hpp"
#include <cstdio>
#include <vector>

static float peak(const std::vector<float>& v, size_t from) {
    float m = 0; for (size_t i = from; i < v.size(); ++i) m = std::max(m, std::fabs(v[i])); return m;
}

int main() {
    const double sr = 48000; const int n = 48000 * 2;
    std::vector<float> in(n), L(n), R(n);
    MonoFETProcessor p; p.prepare(sr);
    int fails = 0;

    // 1) Sinal bem abaixo do threshold, Input 0 dB, Output 0 dB: seco - comprimido = ~0
    for (int i = 0; i < n; ++i) in[i] = 0.01f * std::sin(2 * 3.14159265f * 440.f * i / sr);
    p.setParams(0, 0, 200, 400, 4);
    p.process(in.data(), L.data(), R.data(), n);
    float p1 = peak(L, 0);
    std::printf("1) abaixo do threshold: pico=%g (esperado < 1e-6)\n", p1);
    if (p1 > 1e-6f) ++fails;

    // 2) Sinal a 0 dBFS, ratio 4: sobra algo audivel, L == R
    p.prepare(sr);
    for (int i = 0; i < n; ++i) in[i] = std::sin(2 * 3.14159265f * 440.f * i / sr);
    p.setParams(0, 0, 200, 400, 4);
    p.process(in.data(), L.data(), R.data(), n);
    float p2 = peak(L, n / 2);
    bool same = true; for (int i = 0; i < n; ++i) if (L[i] != R[i]) same = false;
    std::printf("2) 0 dBFS ratio 4: pico=%g (esperado 0.6 a 0.85), L==R: %d\n", p2, same);
    if (p2 < 0.6f || p2 > 0.85f || !same) ++fails;

    // 3) In-place (in == L) gera o mesmo resultado do caso 2
    std::vector<float> buf = in, R2(n); p.prepare(sr);
    p.setParams(0, 0, 200, 400, 4);
    p.process(buf.data(), buf.data(), R2.data(), n);
    float diff = 0; for (int i = 0; i < n; ++i) diff = std::max(diff, std::fabs(buf[i] - L[i]));
    std::printf("3) in-place: diferenca max=%g (esperado 0)\n", diff);
    if (diff > 1e-7f) ++fails;

    // 4) Suavizacao: salto de Input 0 -> 24 dB nao pode gerar degrau no 1o sample
    p.prepare(sr);
    for (int i = 0; i < n; ++i) in[i] = 0.05f;
    p.setParams(0, 0, 200, 400, 4);
    p.process(in.data(), L.data(), R.data(), 100);
    p.setParams(24, 0, 200, 400, 4);
    p.process(in.data(), L.data() + 100, R.data() + 100, 2000);
    float step = 0; for (int i = 1; i < 2100; ++i) step = std::max(step, std::fabs(L[i] - L[i - 1]));
    std::printf("4) salto de Input: degrau=%g (esperado < 5e-3)\n", step);
    if (step > 5e-3f) ++fails;

    std::printf(fails ? "FALHOU (%d)\n" : "OK\n", fails);
    return fails;
}
