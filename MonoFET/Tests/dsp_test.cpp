#include "../Source/MonoFETProcessor.hpp"
#include <cstdio>
#include <vector>

static float peak(const std::vector<float>& v, size_t from) {
    float m = 0; for (size_t i = from; i < v.size(); ++i) m = std::max(m, std::fabs(v[i])); return m;
}

int main() {
    const double sr = 48000; const int n = 48000 * 2;
    std::vector<float> in(n), L(n);
    MonoFETProcessor p; p.prepare(sr);
    int fails = 0;

    // 1) Sinal bem abaixo do threshold, Input 0 dB, Output 0 dB: seco - comprimido = ~0
    for (int i = 0; i < n; ++i) in[i] = 0.01f * std::sin(2 * 3.14159265f * 440.f * i / sr);
    p.setParams(0, 0, 200, 400, 4);
    p.process(in.data(), L.data(), n);
    float p1 = peak(L, 0);
    std::printf("1) abaixo do threshold: pico=%g (esperado < 1e-6)\n", p1);
    if (p1 > 1e-6f) ++fails;

    // 2) Sinal a 0 dBFS, ratio 4: sobra algo audivel, L == R
    p.prepare(sr);
    for (int i = 0; i < n; ++i) in[i] = std::sin(2 * 3.14159265f * 440.f * i / sr);
    p.setParams(0, 0, 200, 400, 4);
    p.process(in.data(), L.data(), n);
    float p2 = peak(L, n / 2);
    std::printf("2) 0 dBFS ratio 4: pico=%g (esperado 0.6 a 0.85)\n", p2);
    if (p2 < 0.6f || p2 > 0.85f) ++fails;

    // 3) In-place (in == L) gera o mesmo resultado do caso 2
    std::vector<float> buf = in; p.prepare(sr);
    p.setParams(0, 0, 200, 400, 4);
    p.process(buf.data(), buf.data(), n);
    float diff = 0; for (int i = 0; i < n; ++i) diff = std::max(diff, std::fabs(buf[i] - L[i]));
    std::printf("3) in-place: diferenca max=%g (esperado 0)\n", diff);
    if (diff > 1e-7f) ++fails;

    // 4) Suavizacao: salto de Input 0 -> 24 dB nao pode gerar degrau no 1o sample
    p.prepare(sr);
    for (int i = 0; i < n; ++i) in[i] = 0.05f;
    p.setParams(0, 0, 200, 400, 4);
    p.process(in.data(), L.data(), 1000);   // escorva a linha de atraso (latencia 240 samples)
    p.setParams(24, 0, 200, 400, 4);
    p.process(in.data(), L.data() + 1000, 2000);
    float step = 0; for (int i = 301; i < 3000; ++i) step = std::max(step, std::fabs(L[i] - L[i - 1]));
    std::printf("4) salto de Input: degrau=%g (esperado < 5e-3)\n", step);
    if (step > 5e-3f) ++fails;

    // 5) Threshold ajustavel: threshold alto (0 dB) nao comprime, threshold baixo comprime
    for (int i = 0; i < n; ++i) in[i] = 0.5f * std::sin(2 * 3.14159265f * 440.f * i / sr);
    p.prepare(sr); p.setParams(0, 0, 200, 400, 4, 0.f);
    p.process(in.data(), L.data(), n);
    float hi = peak(L, n / 2);
    p.prepare(sr); p.setParams(0, 0, 200, 400, 4, -40.f);
    p.process(in.data(), L.data(), n);
    float lo = peak(L, n / 2);
    std::printf("5) threshold 0 dB: pico=%g (esperado ~0), -40 dB: pico=%g (esperado > 0.2)\n", hi, lo);
    if (hi > 0.05f || lo < 0.2f) ++fails;

    // 6) Latencia fixa: impulso sai exatamente em 100 + latencySamples
    p.prepare(sr);
    std::vector<float> imp(2000, 0.f), o6(2000, 0.f); imp[100] = 0.5f;
    p.setParams(0, 0, 200, 400, 4, -60.f, 2.f);
    p.process(imp.data(), o6.data(), 2000);
    int am = 0; for (int i = 0; i < 2000; ++i) if (std::fabs(o6[i]) > std::fabs(o6[am])) am = i;
    std::printf("6) latencia: pico em %d (esperado %d)\n", am, 100 + p.latencySamples());
    if (am != 100 + p.latencySamples()) ++fails;

    // 7) Lookahead: o ataque da caixa e pego antes. Burst de 0 dBFS apos silencio, janela de 1 ms
    auto burst = [&](float lookMs) {
        MonoFETProcessor q; q.prepare(sr);
        std::vector<float> a(9600, 0.f), o(9600, 0.f);
        for (int i = 2400; i < 9600; ++i) a[i] = std::sin(2 * 3.14159265f * 440.f * i / sr);
        q.setParams(0, 0, 800, 400, 4, -18.f, lookMs);
        q.process(a.data(), o.data(), 9600);
        int s = 2400 + q.latencySamples();
        float m = 0; for (int i = s; i < s + 48; ++i) m = std::max(m, std::fabs(o[i]));
        return m;
    };
    float m0 = burst(0.f), m5 = burst(5.f);
    std::printf("7) lookahead 0 ms: %g, 5 ms: %g (esperado 5 ms > 1.2x)\n", m0, m5);
    if (m5 < 1.2f * m0) ++fails;

    // 8) Lookahead Off: latencia zero, impulso sai na mesma amostra
    p.prepare(sr);
    std::fill(o6.begin(), o6.end(), 0.f);
    p.setParams(0, 0, 200, 400, 4, -60.f, 2.f, false);
    p.process(imp.data(), o6.data(), 2000);
    am = 0; for (int i = 0; i < 2000; ++i) if (std::fabs(o6[i]) > std::fabs(o6[am])) am = i;
    std::printf("8) off: latencia=%d, pico em %d (esperado 0 e 100)\n", p.latencySamples(), am);
    if (p.latencySamples() != 0 || am != 100) ++fails;

    // 9) Alternar On/Off durante o processamento: saida finita e limitada
    p.prepare(sr);
    std::vector<float> sg(4800), og(4800);
    for (int i = 0; i < 4800; ++i) sg[i] = 0.8f * std::sin(2 * 3.14159265f * 220.f * i / sr);
    bool ok = true;
    for (int k = 0; k < 6; ++k) {
        p.setParams(12, 0, 200, 400, 4, -18.f, 3.f, k % 2 == 0);
        p.process(sg.data(), og.data(), 4800);
        for (float v : og) if (!std::isfinite(v) || std::fabs(v) > 2.f) ok = false;
    }
    std::printf("9) alternar on/off: saida valida=%d (esperado 1)\n", ok);
    if (!ok) ++fails;

    auto tone = [&](float hz, float amp, int len) {
        std::vector<float> v(len);
        for (int i = 0; i < len; ++i) v[i] = amp * std::sin(2 * 3.14159265f * hz * i / sr);
        return v;
    };
    auto run = [&](const std::vector<float>& x, bool eqMode, bool compOn, float hp, float lp, float thr) {
        MonoFETProcessor q; q.prepare(sr);
        std::vector<float> o(x.size());
        q.setParams(0, 0, 200, 400, 4, thr, 2.f, true, hp, lp, eqMode, compOn);
        q.process(x.data(), o.data(), (int)x.size());
        return peak(o, x.size() / 2);
    };

    // 10) Sidechain: LPF 3 kHz faz o detector ignorar um tom de 10 kHz forte (sem compressao => saida ~0)
    auto tHi = tone(10000.f, 0.5f, 48000), tMid = tone(500.f, 0.5f, 48000);
    float scHi = run(tHi, false, true, 20.f, 3000.f, -30.f);
    float scLo = run(tMid, false, true, 20.f, 3000.f, -30.f);
    std::printf("10) sidechain LPF 3k: 10 kHz pico=%g (esperado < 0.15), 500 Hz pico=%g (esperado > 0.15)\n", scHi, scLo);
    if (scHi > 0.15f || scLo < 0.15f) ++fails;

    // 11) Sidechain: HPF 300 Hz faz o detector ignorar um tom de 40 Hz
    auto vlo = tone(40.f, 0.5f, 48000);
    float scHp = run(vlo, false, true, 300.f, 20000.f, -30.f);
    float scMid = run(tHi, false, true, 300.f, 20000.f, -30.f);
    std::printf("11) sidechain HPF 300: 40 Hz pico=%g (esperado < 0.15), 10 kHz pico=%g (esperado > 0.15)\n", scHp, scMid);
    if (scHp > 0.15f || scMid < 0.15f) ++fails;

    // 12) Compressor off, modo Sidechain: saida e silencio (x - x)
    float offSc = run(tMid, false, false, 100.f, 8000.f, -30.f);
    std::printf("12) comp off + sidechain: pico=%g (esperado 0)\n", offSc);
    if (offSc > 1e-6f) ++fails;

    // 13) Compressor off, modo EQ: saida = x - EQ(x); HPF 300 deixa passar 40 Hz quase todo (x - ~0), 10 kHz quase some
    float offEqLow = run(vlo, true, false, 300.f, 20000.f, -30.f);
    float offEqHi  = run(tHi,  true, false, 300.f, 20000.f, -30.f);
    std::printf("13) comp off + EQ HPF 300: 40 Hz pico=%g (esperado > 0.4), 10 kHz pico=%g (esperado < 0.1)\n", offEqLow, offEqHi);
    if (offEqLow < 0.4f || offEqHi > 0.1f) ++fails;

    // 14) Mover os cortes com audio rodando: sem NaN e saida limitada
    {
        MonoFETProcessor q; q.prepare(sr);
        auto sg = tone(1000.f, 0.8f, 4800); std::vector<float> og(4800); bool ok = true;
        for (int k = 0; k < 8; ++k) {
            q.setParams(12, 0, 200, 400, 4, -18.f, 2.f, true, 20.f + 40.f * k, 20000.f - 2000.f * k, k % 2, true);
            q.process(sg.data(), og.data(), 4800);
            for (float v : og) if (!std::isfinite(v) || std::fabs(v) > 2.f) ok = false;
        }
        std::printf("14) cortes em movimento: saida valida=%d (esperado 1)\n", ok);
        if (!ok) ++fails;
    }

    // 15) Compensacao Input/Output: Input 12 dB, Output 0 dB, sinal abaixo do threshold => ainda cancela
    {
        MonoFETProcessor q; q.prepare(sr);
        auto sg = tone(1000.f, 0.01f, 9600); std::vector<float> og(9600);
        q.setParams(12, 0, 200, 400, 4, -18.f);
        q.process(sg.data(), og.data(), 9600);
        float pk = peak(og, 4800);
        std::printf("15) input 12 dB / output 0 dB: pico=%g (esperado < 1e-4; antes da compensacao ~0.03)\n", pk);
        if (pk > 1e-4f) ++fails;
    }

    std::printf(fails ? "FALHOU (%d)\n" : "OK\n", fails);
    return fails;
}
