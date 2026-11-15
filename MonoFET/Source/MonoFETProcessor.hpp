#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Biquad Butterworth de 2a ordem (12 dB/oitava), forma transposta II.
class Biquad {
public:
    void setHP(float f, double sr) { set(f, sr, true); }
    void setLP(float f, double sr) { set(f, sr, false); }
    void reset() { z1 = z2 = 0.f; }
    float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
private:
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
    void set(float f, double sr, bool hp) {
        f = std::min(std::max(f, 10.f), 0.45f * (float)sr);
        const float w = 2.f * 3.14159265f * f / (float)sr;
        const float c = std::cos(w), al = std::sin(w) / (2.f * 0.70710678f);
        const float a0 = 1.f + al;
        if (hp) { b0 = (1.f + c) / 2.f; b1 = -(1.f + c); b2 = b0; }
        else    { b0 = (1.f - c) / 2.f; b1 =   1.f - c;  b2 = b0; }
        a1 = -2.f * c / a0; a2 = (1.f - al) / a0;
        b0 /= a0; b1 /= a0; b2 /= a0;
    }
};

// Compressor FET simplificado (estilo 1176): threshold fixo, o Input empurra o sinal contra ele.
class FETCompressor {
public:
    void prepare(double sr) {
        sampleRate = sr; grDb = 0.f; snap = true;
        smoothCoef = std::exp(-1.f / (kSmoothSec * (float)sr));
        update();
    }

    void setParams(float inDb, float outDb, float attackUs, float releaseMs, float ratio, float thrDb = -18.f) {
        tgtThr = thrDb;
        tgtIn  = std::pow(10.f, inDb / 20.f);
        tgtOut = std::pow(10.f, outDb / 20.f);
        tgtInvRatio = 1.f / ratio;
        if (snap) { thr = tgtThr; inGain = tgtIn; outGain = tgtOut; invRatio = tgtInvRatio; snap = false; }
        attackSec  = attackUs * 1e-6f;
        releaseSec = releaseMs * 1e-3f;
        update();
    }

    // xDet: sinal visto pelo detector (adiantado). xAudio: sinal que recebe o ganho (atrasado).
    void reset() { grDb = 0.f; }

    float process(float xDet, float xAudio) {
        inGain   = smoothCoef * inGain   + (1.f - smoothCoef) * tgtIn;
        outGain  = smoothCoef * outGain  + (1.f - smoothCoef) * tgtOut;
        invRatio = smoothCoef * invRatio + (1.f - smoothCoef) * tgtInvRatio;
        thr = smoothCoef * thr + (1.f - smoothCoef) * tgtThr;
        const float v = xDet * inGain;
        const float levelDb = 20.f * std::log10(std::max(std::fabs(v), 1e-9f));
        const float over = levelDb - thr;
        const float target = over > 0.f ? over * (1.f - invRatio) : 0.f;
        const float c = target > grDb ? attackCoef : releaseCoef;
        grDb = c * grDb + (1.f - c) * target;
        const float c2 = xAudio * inGain * std::pow(10.f, -grDb / 20.f);
        return std::tanh(c2) * outGain;   // saturacao leve
    }

private:
    static constexpr float kSmoothSec = 0.01f;
    double sampleRate = 44100.0;
    float inGain = 1.f, outGain = 1.f, invRatio = 0.25f;
    float thr = -18.f, tgtThr = -18.f;
    float tgtIn = 1.f, tgtOut = 1.f, tgtInvRatio = 0.25f, smoothCoef = 0.f;
    bool snap = true;
    float attackSec = 2e-4f, releaseSec = 0.4f;
    float attackCoef = 0.f, releaseCoef = 0.f, grDb = 0.f;

    void update() {
        attackCoef  = std::exp(-1.f / (attackSec  * (float)sampleRate));
        releaseCoef = std::exp(-1.f / (releaseSec * (float)sampleRate));
    }
};

// mono -> L=R -> (R: polaridade invertida + FET) -> L + R -> saida mono
class MonoFETProcessor {
public:
    // Latencia fixa: o audio sempre sai atrasado kLatencySec. O Lookahead (0 a kLatencySec)
    // define quanto o detector enxerga o sinal antes do audio, sem mudar a latencia reportada.
    static constexpr float kLatencySec = 0.005f;

    void prepare(double sr) {
        comp.prepare(sr);
        sampleRate = sr;
        delay = (int)std::lround(kLatencySec * sr);
        buf.assign(delay + 2, 0.f);
        bufF.assign(delay + 2, 0.f);
        hpf.reset(); lpf.reset(); filtSnap = true;
        filtCoef = std::exp(-1.f / (0.01f * (float)sr));
        pos = 0; look = tgtLook = 0.f; lookSnap = true; prevOn = true; activeDelay = delay;
        lookCoef = std::exp(-1.f / (0.01f * (float)sr));
    }
    int latencySamples() const { return activeDelay; }

    void setParams(float inDb, float outDb, float attackUs, float releaseMs, float ratio,
                   float thrDb = -18.f, float lookMs = 2.f, bool lookOn = true,
                   float hpfHz = 20.f, float lpfHz = 20000.f, bool eqMode = false, bool compOn = true) {
        comp.setParams(inDb, outDb, attackUs, releaseMs, ratio, thrDb);
        tgtHp = hpfHz; tgtLp = lpfHz; eq = eqMode; compEnabled = compOn;
        hpOff = hpfHz <= 20.5f; lpOff = lpfHz >= 19990.f;   // nas pontas o filtro sai do caminho (sem fase)
        if (filtSnap) { curHp = tgtHp; curLp = tgtLp; hpf.setHP(curHp, sampleRate); lpf.setLP(curLp, sampleRate); filtSnap = false; }
        if (lookOn != prevOn) { prevOn = lookOn; lookSnap = true; }   // troca de modo: sem rampa
        activeDelay = lookOn ? delay : 0;                             // desligado: latencia zero
        tgtLook = lookOn ? std::min(std::max(lookMs, 0.f) * 1e-3f * (float)sampleRate, (float)delay) : 0.f;
        if (lookSnap) { look = tgtLook; lookSnap = false; }
    }
    // Seguro em processamento in-place (in == out).
    void process(const float* in, float* out, int n) {
        const int size = (int)buf.size();
        for (int i = 0; i < n; ++i) {
            buf[pos] = in[i];
            curHp = filtCoef * curHp + (1.f - filtCoef) * tgtHp;     // cortes suavizados
            curLp = filtCoef * curLp + (1.f - filtCoef) * tgtLp;
            if (std::fabs(curHp - tgtHp) > 1e-3f * tgtHp) hpf.setHP(curHp, sampleRate);
            if (std::fabs(curLp - tgtLp) > 1e-3f * tgtLp) lpf.setLP(curLp, sampleRate);
            float f = in[i];
            if (hpOff) hpf.reset(); else f = hpf.process(f);
            if (lpOff) lpf.reset(); else f = lpf.process(f);
            bufF[pos] = f;                                           // sinal filtrado (HPF + LPF)
            look = lookCoef * look + (1.f - lookCoef) * tgtLook;
            const float dDet = std::max(0.f, (float)activeDelay - look);   // atraso do detector em samples
            const int i0 = (int)dDet;
            const float fr = dDet - (float)i0;
            const float xDet = tapF(bufF, i0, size) * (1.f - fr) + tapF(bufF, i0 + 1, size) * fr;   // detector: sempre filtrado
            const float l = tapF(buf, activeDelay, size);                  // audio atrasado (ramo seco, sem filtro)
            const float rIn = eq ? tapF(bufF, activeDelay, size) : l;      // modo EQ: ramo R recebe o audio filtrado
            float r;
            if (compEnabled) r = -comp.process(xDet, rIn);                 // ramo invertido e comprimido
            else { comp.reset(); r = -rIn; }                               // compressor off: so inversao (e EQ)
            out[i] = l + r;
            pos = (pos + 1) % size;
        }
    }
private:
    FETCompressor comp;
    std::vector<float> buf, bufF;
    Biquad hpf, lpf;
    float curHp = 20.f, tgtHp = 20.f, curLp = 20000.f, tgtLp = 20000.f, filtCoef = 0.f;
    bool filtSnap = true, eq = false, compEnabled = true, hpOff = true, lpOff = true;
    double sampleRate = 44100.0;
    int delay = 0, activeDelay = 0, pos = 0;
    bool prevOn = true;
    float look = 0.f, tgtLook = 0.f, lookCoef = 0.f;
    bool lookSnap = true;
    float tapF(const std::vector<float>& b, int d, int size) const { return b[(pos - d + size) % size]; }
};
