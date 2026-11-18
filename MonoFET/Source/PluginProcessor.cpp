#include "PluginProcessor.h"
#include "MonoFETPresets.hpp"

namespace {
// Mesma ordem dos campos de MonoFETPreset.
const char* const kIds[] = { "input", "output", "attack", "release", "ratio", "threshold",
                             "lookahead", "lookon", "hpf", "lpf", "filtermode", "compon",
                             "mix", "peqfreq", "peqgain", "peqq" };
enum { kInput, kOutput, kAttack, kRelease, kRatio, kThreshold, kLookahead, kLookOn,
       kHpf, kLpf, kFilterMode, kCompOn, kMix, kPeqFreq, kPeqGain, kPeqQ };

std::unique_ptr<juce::AudioParameterFloat> makeFloat(const char* id, const char* name, float lo, float hi,
                                                     float def, float centre, const char* label) {
    juce::NormalisableRange<float> r(lo, hi);
    if (centre > 0.f) r.setSkewForCentre(centre);
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, r, def,
                                                       juce::AudioParameterFloatAttributes().withLabel(label));
}
}

juce::AudioProcessorValueTreeState::ParameterLayout MonoFETAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add(makeFloat("input",     "Input",     0.f,   48.f,    12.f,   0.f,    "dB"));
    l.add(makeFloat("output",    "Output",    -24.f, 24.f,    0.f,    0.f,    "dB"));
    l.add(makeFloat("attack",    "Attack",    20.f,  800.f,   200.f,  150.f,  "us"));
    l.add(makeFloat("release",   "Release",   50.f,  1100.f,  400.f,  300.f,  "ms"));
    l.add(makeFloat("ratio",     "Ratio",     1.5f,  20.f,    4.f,    4.f,    ":1"));
    l.add(makeFloat("threshold", "Threshold", -60.f, 0.f,     -18.f,  0.f,    "dB"));
    l.add(makeFloat("lookahead", "Lookahead", 0.f,   5.f,     2.f,    0.f,    "ms"));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "lookon", 1 }, "Lookahead On", true));
    l.add(makeFloat("hpf",       "HPF",       20.f,  300.f,   20.f,   80.f,   "Hz"));
    l.add(makeFloat("lpf",       "LPF",       300.f, 20000.f, 20000.f, 3000.f, "Hz"));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "filtermode", 1 }, "Filter Mode",
                                                       juce::StringArray { "Sidechain", "EQ" }, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "compon", 1 }, "Compressor On", true));
    l.add(makeFloat("mix",       "Mix",       0.f,   100.f,   100.f,  0.f,    "%"));
    l.add(makeFloat("peqfreq",   "PEQ Freq",  100.f, 10000.f, 1000.f, 1000.f, "Hz"));
    l.add(makeFloat("peqgain",   "PEQ Gain",  -18.f, 18.f,    0.f,    0.f,    "dB"));
    l.add(makeFloat("peqq",      "PEQ Q",     0.3f,  10.f,    1.f,    1.5f,   ""));
    return l;
}

MonoFETAudioProcessor::MonoFETAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::mono(), true)
                         .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "MonoFET", createLayout()) {
    for (int i = 0; i < kNumParams; ++i) {
        p[i] = apvts.getRawParameterValue(kIds[i]);
        jassert(p[i] != nullptr);
    }
    startTimerHz(20);
}

MonoFETAudioProcessor::~MonoFETAudioProcessor() { stopTimer(); }

void MonoFETAudioProcessor::prepareToPlay(double sampleRate, int) {
    rate = sampleRate;
    dsp.prepare(sampleRate);
    updateLatency();
}

// Latencia: 5 ms com Lookahead ligado, 0 com ele desligado (a mesma conta do DSP).
void MonoFETAudioProcessor::updateLatency() {
    const double sr = rate.load();
    if (sr <= 0.0) return;
    const int want = p[kLookOn]->load() >= 0.5f ? (int)std::lround(MonoFETProcessor::kLatencySec * sr) : 0;
    if (want != getLatencySamples()) setLatencySamples(want);
}

void MonoFETAudioProcessor::timerCallback() { updateLatency(); }

bool MonoFETAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void MonoFETAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 1 || n == 0) return;

    auto v = [this](int i) { return p[i]->load(); };
    dsp.setParams(v(kInput), v(kOutput), v(kAttack), v(kRelease), v(kRatio), v(kThreshold),
                  v(kLookahead), v(kLookOn) >= 0.5f, v(kHpf), v(kLpf), v(kFilterMode) >= 0.5f,
                  v(kCompOn) >= 0.5f, v(kMix) * 0.01f, v(kPeqFreq), v(kPeqGain), v(kPeqQ));
    float* io = buffer.getWritePointer(0);
    dsp.process(io, io, n);   // in-place
    for (int ch = 1; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);
}

juce::AudioProcessorEditor* MonoFETAudioProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

int MonoFETAudioProcessor::getNumPrograms() { return kMonoFETNumPresets; }

const juce::String MonoFETAudioProcessor::getProgramName(int index) {
    return (index >= 0 && index < kMonoFETNumPresets) ? juce::String(kMonoFETPresets[index].name) : juce::String();
}

void MonoFETAudioProcessor::setCurrentProgram(int index) {
    if (index < 0 || index >= kMonoFETNumPresets) return;
    currentProgram = index;
    const MonoFETPreset& q = kMonoFETPresets[index];
    const float vals[kNumParams] = { q.inDb, q.outDb, q.attackUs, q.releaseMs, q.ratio, q.thrDb,
                                     q.lookMs, q.lookOn, q.hpfHz, q.lpfHz, q.eqMode, q.compOn,
                                     q.mixPct, q.peqHz, q.peqDb, q.peqQ };
    for (int i = 0; i < kNumParams; ++i)
        if (auto* prm = apvts.getParameter(kIds[i]))
            prm->setValueNotifyingHost(prm->convertTo0to1(vals[i]));
}

void MonoFETAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void MonoFETAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MonoFETAudioProcessor(); }
