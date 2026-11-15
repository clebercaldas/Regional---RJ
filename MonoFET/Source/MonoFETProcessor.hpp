#pragma once
#include <algorithm>
#include <cmath>

// Compressor FET simplificado (estilo 1176): threshold fixo, o Input empurra o sinal contra ele.
class FETCompressor {
public:
    void prepare(double sr) { sampleRate = sr; grDb = 0.f; update(); }

    void setParams(float inDb, float outDb, float attackUs, float releaseMs, float ratio) {
        inGain  = std::pow(10.f, inDb / 20.f);
        outGain = std::pow(10.f, outDb / 20.f);
        invRatio = 1.f / ratio;
        attackSec  = attackUs * 1e-6f;
        releaseSec = releaseMs * 1e-3f;
        update();
    }

    float process(float x) {
        const float v = x * inGain;
        const float levelDb = 20.f * std::log10(std::max(std::fabs(v), 1e-9f));
        const float over = levelDb - kThresholdDb;
        const float target = over > 0.f ? over * (1.f - invRatio) : 0.f;
        const float c = target > grDb ? attackCoef : releaseCoef;
        grDb = c * grDb + (1.f - c) * target;
        return v * std::pow(10.f, -grDb / 20.f) * outGain;
    }

private:
    static constexpr float kThresholdDb = -18.f;
    double sampleRate = 44100.0;
    float inGain = 1.f, outGain = 1.f, invRatio = 0.25f;
    float attackSec = 2e-4f, releaseSec = 0.4f;
    float attackCoef = 0.f, releaseCoef = 0.f, grDb = 0.f;

    void update() {
        attackCoef  = std::exp(-1.f / (attackSec  * (float)sampleRate));
        releaseCoef = std::exp(-1.f / (releaseSec * (float)sampleRate));
    }
};

// mono -> L=R -> (R: polaridade invertida + FET) -> L + R -> duplicado em dois canais de saida
class MonoFETProcessor {
public:
    void prepare(double sr) { comp.prepare(sr); }
    void setParams(float inDb, float outDb, float attackUs, float releaseMs, float ratio) {
        comp.setParams(inDb, outDb, attackUs, releaseMs, ratio);
    }
    // Seguro em processamento in-place quando in == outL.
    void process(const float* in, float* outL, float* outR, int n) {
        for (int i = 0; i < n; ++i) {
            const float l = in[i];
            const float r = -comp.process(in[i]);
            const float y = l + r;
            outL[i] = y;
            outR[i] = y;
        }
    }
private:
    FETCompressor comp;
};
