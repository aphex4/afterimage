#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/ParameterSmoother.h"
#include "DSP/SpectralEngine.h"
#include "DSP/SpectralModes.h"
#include "DSP/DryWetMixer.h"
#include "DSP/GainMatch.h"
#include "DSP/VisualizationAtomics.h"
#include "Utilities/Constants.h"
#include "Utilities/FactoryPresets.h"

#if defined (AFTERIMAGE_ENABLE_LICENSING)
#include "Licensing/LicenseManager.h"
#endif

#include <atomic>
#include <vector>

/**
    AFTERIMAGE spectral memory processor (Shadow / Erase / Merge).

    Routing (documented):
      1) latency-aligned dry + processed wet (identity STFT + spectral modes)
      2) equal-power dry/wet Mix → mixed
      3) Gain Match: one broadband scalar on mixed (dry vs mixed power, not wet-only)
      4) bypass crossfade (toward latency-aligned dry)
      5) trial-entitlement dry crossfade (if licensing enabled)
      6) final Output Gain  ← after bypass so it always trims the audible output

    Mix=0% → GM measures dry vs dry → ~unity. Bypass=100% → dry unaltered by GM.
    Host callbacks larger than maxInternalBlockSize are processed in fixed chunks
    using preallocated scratch (no audio-thread allocation).
*/
class AfterimageAudioProcessor : public juce::AudioProcessor
{
public:
    AfterimageAudioProcessor();
    ~AfterimageAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /**
        Finite tail for hosts: max memory + FFT latency @ 44.1 kHz + margin.
        Freeze can sustain indefinitely; the host API requires a finite value —
        that limitation is intentional (see constants::pluginTailLengthSec).
    */
    double getTailLengthSeconds() const override { return afterimage::constants::pluginTailLengthSec; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    /** True when APVTS does not match the reported factory program index. */
    bool isCustomProgram() const noexcept { return customProgram_; }

    /** Re-evaluate factory vs Custom from live APVTS (UI timer / after edits). */
    void refreshProgramStatus() noexcept { syncProgramIndexFromParameters(); }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    afterimage::SpectralEngine& getEngine() noexcept { return engine; }

    afterimage::SpectralMode getCurrentMode() const noexcept;

    /** UI-safe visualization reads (atomics / snapshot publisher only). */
    const afterimage::VisualizationAtomics& getVisualization() const noexcept { return engine.getVisualization(); }
    afterimage::SnapshotPublisher& getSnapshotPublisher() noexcept { return engine.getSnapshotPublisher(); }

    float getInputLevel() const noexcept  { return engine.getVisualization().loadInputPeak(); }
    float getOutputLevel() const noexcept { return engine.getVisualization().loadOutputPeak(); }
    float getHistoryFill() const noexcept { return engine.getVisualization().loadHistoryFill(); }

    /** Debug / tests: current Gain Match detector correction in dB (runs even when GM is off). */
    float getGainMatchCorrectionDb() const noexcept { return gainMatch_.loadDebugCorrectionDb(); }

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    afterimage::licensing::LicenseManager& getLicenseManager() noexcept { return licenseManager_; }
#endif

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Reset Gain Match detector / correction (presets & state load). */
    void resetAdaptiveProcessingState() noexcept;

    /** Apply a restored APVTS ValueTree (setStateInformation + migration tests). */
    void applyRestoredParameterTree (const juce::ValueTree& tree);

private:
    void updateParameterTargets();
    void syncProgramIndexFromParameters() noexcept;
    void processChunk (juce::AudioBuffer<float>& wetChunk,
                       juce::AudioBuffer<float>& dryInChunk,
                       juce::AudioBuffer<float>& delayedDryChunk) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    afterimage::SpectralEngine engine;
    afterimage::ParameterSmoother smoothers;
    afterimage::DryWetMixer dryWetMixer;
    afterimage::GainMatchController gainMatch_;

    juce::AudioBuffer<float> inputScratch;
    juce::AudioBuffer<float> delayedDry;
    std::vector<float*> wetPtrs_;
    std::vector<float*> dryInPtrs_;
    std::vector<float*> delayedPtrs_;

    int maxChannels_ = 2;
    int maxChunk_ = afterimage::constants::maxInternalBlockSize;

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
    std::atomic<float>* pGainMatch = nullptr;

    int currentProgram_ = 0;
    bool customProgram_ = false;

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    afterimage::licensing::LicenseManager licenseManager_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> entitlementDryAmount_;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageAudioProcessor)
};
