#pragma once

#include "IntegraSession.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace integra
{
    /**
        Connects JUCE's AudioDeviceManager to libIntegra's DSP engine (libpd).

        JUCE delivers buffers of whatever size the device uses; libpd processes fixed
        64-frame blocks of interleaved audio. AudioHost re-blocks through one
        64-frame buffer in each direction, adding 64 frames of latency - the same
        block-size latency the original PortAudio engine had.

        Device selection replaces Integra Live's old AudioSettings dialog; the
        chosen device setup is persisted by the app (see MainWindow).
    */
    class AudioHost  : public juce::AudioIODeviceCallback,
                      private juce::ChangeListener
    {
    public:
        explicit AudioHost (IntegraSession&);
        ~AudioHost() override;

        /** Opens the audio device (restoring saved state if given) and starts processing. */
        juce::String start (const juce::XmlElement* savedState);

        /** Stops calling into libIntegra. Must happen before the session stops. */
        void stop();

        juce::AudioDeviceManager& getDeviceManager() noexcept  { return deviceManager; }

        /** Peak output level since last call, for the transport meter (message thread). */
        float getAndResetOutputPeak() noexcept                  { return outputPeak.exchange (0.0f); }
        float getAndResetInputPeak() noexcept                   { return inputPeak.exchange (0.0f); }

        /** How many engine blocks have been silenced because they held a NaN or infinity
            (which would otherwise reach the audio interface as full-scale noise). */
        int getNonFiniteBlockCount() const noexcept            { return nonFiniteBlocks.load(); }

        double getCpuUsage() const                              { return deviceManager.getCpuUsage(); }

        /** Sets the channel layout the callback expects (called when a device starts;
            public so tests can drive the callback without a device). */
        void configure (int numInputs, int numOutputs, int sampleRate);

        /** For tests that drive audioDeviceIOCallbackWithContext themselves: stops the
            thread that otherwise runs the engine when there is no audio device, so only
            the test advances it (deterministic output). */
        void stopNoDeviceThread();

        // AudioIODeviceCallback
        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                               float* const* outputChannelData, int numOutputChannels,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;

    private:
        class NoDeviceThread;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void updateNoDeviceThread();

        IntegraSession& session;
        std::unique_ptr<NoDeviceThread> noDeviceThread;
        bool manualDrive = false;
        juce::AudioDeviceManager deviceManager;

        const int blockSize;
        std::vector<float> inputBlock, outputBlock;
        int blockPosition = 0;
        int currentInputs = 0, currentOutputs = 0;
        int sampleRate = 44100;
        std::atomic<bool> processing { false };

        std::atomic<float> outputPeak { 0.0f }, inputPeak { 0.0f };
        std::atomic<int> nonFiniteBlocks { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioHost)
    };
}
