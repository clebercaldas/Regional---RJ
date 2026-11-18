#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MonoFETProcessor.hpp"

// Camada JUCE: parametros, presets, estado e latencia. O DSP fica em MonoFETProcessor.hpp.
class MonoFETAudioProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    MonoFETAudioProcessor();
    ~MonoFETAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kNumParams = 16;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;   // atualiza a latencia reportada (thread de mensagens)
    void updateLatency();

    MonoFETProcessor dsp;
    std::atomic<float>* p[kNumParams] {};
    std::atomic<double> rate { 0.0 };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonoFETAudioProcessor)
};
