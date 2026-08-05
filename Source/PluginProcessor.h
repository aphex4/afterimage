#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/ParameterSmoother.h"
#include "DSP/SpectralEngine.h"
#include "DSP/SpectralModes.h"
#include "Utilities/Constants.h"

#include <atomic>

/**
    AFTERIMAGE — Phase 1 foundation.

    Pass-through audio with APVTS, smoothed output gain / mix / bypass,
    and a prepared (but inactive) SpectralEngine for later phases.
*/
class AfterimageAudioProcessor : public juce::AudioProcessor
{
public:
    AfterimageAudioProcessor();
    ~AfterimageAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    afterimage::SpectralEngine& getEngine() noexcept { return engine; }

    afterimage::SpectralMode getCurrentMode() const noexcept;

    float getInputLevel() const noexcept { return inputLevel.load (std::memory_order_relaxed); }
    float getOutputLevel() const noexcept { return outputLevel.load (std::memory_order_relaxed); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void updateSmoothedTargets();

    juce::AudioProcessorValueTreeState apvts;
    afterimage::SpectralEngine engine;
    afterimage::ParameterSmoother smoothers;
    afterimage::DryWetMixer dryWetMixer;

    juce::AudioBuffer<float> dryBuffer;

    std::atomic<float> inputLevel  { 0.0f };
    std::atomic<float> outputLevel { 0.0f };

    // Cached raw parameter pointers (audio-thread safe reads).
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pMemoryLength = nullptr;
    std::atomic<float>* pRecallPosition = nullptr;
    std::atomic<float>* pInfluence = nullptr;
    std::atomic<float>* pForget = nullptr;
    std::atomic<float>* pBlur = nullptr;
    std::atomic<float>* pTransientPreserve = nullptr;
    std::atomic<float>* pFreeze = nullptr;
    std::atomic<float>* pRandomRecall = nullptr;
    std::atomic<float>* pOutputGain = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pBypass = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageAudioProcessor)
};
