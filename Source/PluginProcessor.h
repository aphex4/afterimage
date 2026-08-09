#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/ParameterSmoother.h"
#include "DSP/SpectralEngine.h"
#include "DSP/SpectralModes.h"
#include "DSP/DryWetMixer.h"
#include "DSP/GainMatch.h"
#include "DSP/FormantShifter.h"
#include "DSP/DeEsser.h"
#include "DSP/PostChainReverb.h"
#include "DSP/PitchTune.h"
#include "DSP/ScaleTheory.h"
#include "DSP/ParametricEQ.h"
#include "DSP/VisualizationAtomics.h"
#include "Utilities/Constants.h"
#include "Utilities/FactoryPresets.h"

#if defined (AFTERIMAGE_ENABLE_LICENSING)
#include "Licensing/LicenseManager.h"
#endif

#include <atomic>
#include <array>
#include <vector>

/**
    AFTERIMAGE spectral memory processor (Shadow / Erase).

    Routing:
      INPUT → SPECTRAL MEMORY → latency-aligned dry/wet Mix
            → TUNE (opt, fixed latency always) → FORMANT (opt) → DE-ESSER (opt)
            → REVERB (opt) → PARAMETRIC EQ (opt)
            → GAIN MATCH → BYPASS → OUTPUT GAIN → OUTPUT
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
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override
    {
        return afterimage::constants::pluginTailLengthSec;
    }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    bool isCustomProgram() const noexcept { return customProgram_; }
    void refreshProgramStatus() noexcept { syncProgramIndexFromParameters(); }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    afterimage::SpectralEngine& getEngine() noexcept { return engine; }
    afterimage::PostChainReverb& getPostReverb() noexcept { return postReverb_; }
    afterimage::ParametricEQ& getParametricEQ() noexcept { return parametricEq_; }

    afterimage::SpectralMode getCurrentMode() const noexcept;

    const afterimage::VisualizationAtomics& getVisualization() const noexcept { return engine.getVisualization(); }
    afterimage::SnapshotPublisher& getSnapshotPublisher() noexcept { return engine.getSnapshotPublisher(); }

    float getInputLevel() const noexcept  { return engine.getVisualization().loadInputPeak(); }
    float getOutputLevel() const noexcept { return engine.getVisualization().loadOutputPeak(); }
    float getHistoryFill() const noexcept { return engine.getVisualization().loadHistoryFill(); }
    float getGainMatchCorrectionDb() const noexcept { return gainMatch_.loadDebugCorrectionDb(); }

    std::uint16_t getMidiScaleMask() const noexcept
    {
        return midiScaleMask_.load (std::memory_order_relaxed);
    }

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    afterimage::licensing::LicenseManager& getLicenseManager() noexcept { return licenseManager_; }
#endif

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void resetAdaptiveProcessingState() noexcept;
    void applyRestoredParameterTree (const juce::ValueTree& tree);
    static void migrateLegacyParameterTree (juce::ValueTree& tree);

private:
    void updateParameterTargets();
    void syncProgramIndexFromParameters() noexcept;
    void handleMidi (const juce::MidiBuffer& midi) noexcept;
    void processChunk (juce::AudioBuffer<float>& wetChunk,
                       juce::AudioBuffer<float>& dryInChunk,
                       juce::AudioBuffer<float>& delayedDryChunk) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    afterimage::SpectralEngine engine;
    afterimage::ParameterSmoother smoothers;
    afterimage::DryWetMixer dryWetMixer;
    afterimage::GainMatchController gainMatch_;
    afterimage::PitchTune tune_;
    afterimage::FormantShifter formant_;
    afterimage::DeEsser deEsser_;
    afterimage::PostChainReverb postReverb_;
    afterimage::ParametricEQ parametricEq_;

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
    std::atomic<float>* pReverbType = nullptr;
    std::atomic<float>* pReverbWet = nullptr;
    std::atomic<float>* pReverbSafeBass = nullptr;
    std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pDeEsser = nullptr;
    std::atomic<float>* pTuneEnabled = nullptr;
    std::atomic<float>* pFormantEnabled = nullptr;
    std::atomic<float>* pDeEsserEnabled = nullptr;
    std::atomic<float>* pReverbEnabled = nullptr;
    std::atomic<float>* pEqEnabled = nullptr;
    std::atomic<float>* pScaleRoot = nullptr;
    std::atomic<float>* pScaleType = nullptr;
    std::atomic<float>* pRetune = nullptr;
    std::atomic<float>* pHumanize = nullptr;
    std::atomic<float>* pTuneAmount = nullptr;

    // Obsolete params kept bound for session restore only.
    std::atomic<float>* pHarmonicsEnabled = nullptr;
    std::atomic<float>* pScaleColor = nullptr;
    std::atomic<float>* pScaleTransient = nullptr;

    std::array<std::atomic<float>*, afterimage::constants::eqBandsPerStage> pPreEqFreq {};
    std::array<std::atomic<float>*, afterimage::constants::eqBandsPerStage> pPreEqGain {};
    std::array<std::atomic<float>*, afterimage::constants::eqBandsPerStage> pPostEqFreq {};
    std::array<std::atomic<float>*, afterimage::constants::eqBandsPerStage> pPostEqGain {};

    std::array<std::atomic<float>*, afterimage::constants::parametricEqBands> pEqOn {};
    std::array<std::atomic<float>*, afterimage::constants::parametricEqBands> pEqType {};
    std::array<std::atomic<float>*, afterimage::constants::parametricEqBands> pEqFreq {};
    std::array<std::atomic<float>*, afterimage::constants::parametricEqBands> pEqGain {};
    std::array<std::atomic<float>*, afterimage::constants::parametricEqBands> pEqQ {};
    std::array<std::atomic<float>*, afterimage::constants::parametricEqBands> pEqX4 {};
    std::array<std::atomic<float>*, afterimage::constants::parametricEqBands> pEqSolo {};

    std::array<bool, 128> midiNoteHeld_ {};
    std::atomic<std::uint16_t> midiScaleMask_ { 0 };

    int currentProgram_ = 0;
    bool customProgram_ = false;

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    afterimage::licensing::LicenseManager licenseManager_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> entitlementDryAmount_;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterimageAudioProcessor)
};
