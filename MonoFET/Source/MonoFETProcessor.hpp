#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

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
        pos = 0; look = tgtLook = 0.f; lookSnap = true;
        lookCoef = std::exp(-1.f / (0.01f * (float)sr));
    }
    int latencySamples() const { return delay; }

    void setParams(float inDb, float outDb, float attackUs, float releaseMs, float ratio,
                   float thrDb = -18.f, float lookMs = 2.f) {
        comp.setParams(inDb, outDb, attackUs, releaseMs, ratio, thrDb);
        tgtLook = std::min(std::max(lookMs, 0.f) * 1e-3f * (float)sampleRate, (float)delay);
        if (lookSnap) { look = tgtLook; lookSnap = false; }
    }
    // Seguro em processamento in-place (in == out).
    void process(const float* in, float* out, int n) {
        const int size = (int)buf.size();
        for (int i = 0; i < n; ++i) {
            buf[pos] = in[i];
            look = lookCoef * look + (1.f - lookCoef) * tgtLook;
            const float dDet = (float)delay - look;          // atraso do detector em samples
            const int i0 = (int)dDet;
            const float fr = dDet - (float)i0;
            const float xDet = tap(i0, size) * (1.f - fr) + tap(i0 + 1, size) * fr;
            const float l = tap(delay, size);                // audio atrasado (ramo seco)
            const float r = -comp.process(xDet, l);          // ramo invertido e comprimido
            out[i] = l + r;
            pos = (pos + 1) % size;
        }
    }
private:
    FETCompressor comp;
    std::vector<float> buf;
    double sampleRate = 44100.0;
    int delay = 0, pos = 0;
    float look = 0.f, tgtLook = 0.f, lookCoef = 0.f;
    bool lookSnap = true;
    float tap(int d, int size) const { return buf[(pos - d + size) % size]; }
};
