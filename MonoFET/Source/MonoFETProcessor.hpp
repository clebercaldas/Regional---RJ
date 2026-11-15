#pragma once
#include <algorithm>
#include <cmath>

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

    float process(float x) {
        inGain   = smoothCoef * inGain   + (1.f - smoothCoef) * tgtIn;
        outGain  = smoothCoef * outGain  + (1.f - smoothCoef) * tgtOut;
        invRatio = smoothCoef * invRatio + (1.f - smoothCoef) * tgtInvRatio;
        thr = smoothCoef * thr + (1.f - smoothCoef) * tgtThr;
        const float v = x * inGain;
        const float levelDb = 20.f * std::log10(std::max(std::fabs(v), 1e-9f));
        const float over = levelDb - thr;
        const float target = over > 0.f ? over * (1.f - invRatio) : 0.f;
        const float c = target > grDb ? attackCoef : releaseCoef;
        grDb = c * grDb + (1.f - c) * target;
        const float c2 = v * std::pow(10.f, -grDb / 20.f);
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
    void prepare(double sr) { comp.prepare(sr); }
    void setParams(float inDb, float outDb, float attackUs, float releaseMs, float ratio, float thrDb = -18.f) {
        comp.setParams(inDb, outDb, attackUs, releaseMs, ratio, thrDb);
    }
    // Seguro em processamento in-place (in == out).
    void process(const float* in, float* out, int n) {
        for (int i = 0; i < n; ++i) {
            const float l = in[i];
            const float r = -comp.process(in[i]);
            out[i] = l + r;
        }
    }
private:
    FETCompressor comp;
};
