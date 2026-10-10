#include "AudioHost.h"
#include <algorithm>
#include <cmath>

namespace integra
{
    /** Keeps libpd running in real time when no audio device can be opened, the way
            libIntegra's PortAudio engine ran a "no device" thread. Pd only processes
            control messages (module init, value feedback, ping replies...) while DSP
            ticks, so without this the patch would freeze. */
        class AudioHost::NoDeviceThread  : public juce::Thread
        {
        public:
            explicit NoDeviceThread (IntegraSession& s)
                : juce::Thread ("Integra no-device DSP"), session (s) {}

            ~NoDeviceThread() override { stopThread (2000); }

            void run() override
            {
                const int block = CIntegraSession::get_audio_block_size();
                const int channels = 2, rate = 44100;
                std::vector<float> in ((size_t) (block * channels), 0.0f), out ((size_t) (block * channels), 0.0f);

                const double blockMs = 1000.0 * block / rate;
                double next = juce::Time::getMillisecondCounterHiRes();

                while (! threadShouldExit())
                {
                    session.raw().process_audio (in.data(), out.data(), channels, channels, rate);
                    next += blockMs;

                    const double remaining = next - juce::Time::getMillisecondCounterHiRes();
                    if (remaining > 1.0)
                        wait ((int) remaining);
                    else if (remaining < -200.0)
                        next = juce::Time::getMillisecondCounterHiRes();  // fell far behind; don't try to catch up
                }
            }

        private:
            IntegraSession& session;
        };

    AudioHost::AudioHost (IntegraSession& s)
        : session (s), blockSize (CIntegraSession::get_audio_block_size())
    {
    }

    AudioHost::~AudioHost()
    {
        stop();
    }

    juce::String AudioHost::start (const juce::XmlElement* savedState)
    {
        auto error = deviceManager.initialise (2, 2, savedState, true);

        processing = true;
        deviceManager.addAudioCallback (this);
        deviceManager.addChangeListener (this);
        updateNoDeviceThread();

        if (deviceManager.getCurrentAudioDevice() == nullptr && error.isEmpty())
            error = "No audio device available - running without audio I/O";

        return error;
    }

    void AudioHost::stop()
    {
        processing = false;
        deviceManager.removeChangeListener (this);
        deviceManager.removeAudioCallback (this);
        deviceManager.closeAudioDevice();
        noDeviceThread.reset();
    }

    void AudioHost::stopNoDeviceThread()
    {
        manualDrive = true;
        noDeviceThread.reset();
    }

    void AudioHost::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateNoDeviceThread();
    }

    void AudioHost::updateNoDeviceThread()
    {
        const bool needThread = processing && ! manualDrive && deviceManager.getCurrentAudioDevice() == nullptr;

        if (needThread && noDeviceThread == nullptr)
        {
            noDeviceThread = std::make_unique<NoDeviceThread> (session);
            noDeviceThread->startThread (juce::Thread::Priority::high);
        }
        else if (! needThread && noDeviceThread != nullptr)
        {
            noDeviceThread.reset();
        }
    }

    void AudioHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
    {
        configure (device->getActiveInputChannels().countNumberOfSetBits(),
                   device->getActiveOutputChannels().countNumberOfSetBits(),
                   juce::roundToInt (device->getCurrentSampleRate()));
    }

    void AudioHost::configure (int numInputs, int numOutputs, int newSampleRate)
    {
        currentInputs = numInputs;
        currentOutputs = numOutputs;
        sampleRate = newSampleRate;

        inputBlock.assign ((size_t) (blockSize * juce::jmax (1, currentInputs)), 0.0f);
        outputBlock.assign ((size_t) (blockSize * juce::jmax (1, currentOutputs)), 0.0f);
        blockPosition = 0;
    }

    void AudioHost::audioDeviceStopped()
    {
    }

    void AudioHost::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                      float* const* outputChannelData, int numOutputChannels,
                                                      int numSamples, const juce::AudioIODeviceCallbackContext&)
    {
        if (! processing.load() || numInputChannels != currentInputs || numOutputChannels != currentOutputs)
        {
            for (int c = 0; c < numOutputChannels; ++c)
                if (outputChannelData[c] != nullptr)
                    juce::FloatVectorOperations::clear (outputChannelData[c], numSamples);
            return;
        }

        float inPeak = 0.0f, outPeak = 0.0f;
        const int inChannels = currentInputs, outChannels = currentOutputs;

        for (int i = 0; i < numSamples; ++i)
        {
            float* inFrame = inputBlock.data() + blockPosition * inChannels;
            for (int c = 0; c < inChannels; ++c)
            {
                const float sample = inputChannelData[c] != nullptr ? inputChannelData[c][i] : 0.0f;
                inFrame[c] = sample;
                inPeak = juce::jmax (inPeak, std::abs (sample));
            }

            const float* outFrame = outputBlock.data() + blockPosition * outChannels;
            for (int c = 0; c < outChannels; ++c)
            {
                if (outputChannelData[c] != nullptr)
                    outputChannelData[c][i] = outFrame[c];
                outPeak = juce::jmax (outPeak, std::abs (outFrame[c]));
            }

            if (++blockPosition == blockSize)
            {
                blockPosition = 0;
                if (session.raw().process_audio (inputBlock.data(), outputBlock.data(), inChannels, outChannels, sampleRate) != CError::SUCCESS)
                    std::fill (outputBlock.begin(), outputBlock.end(), 0.0f);

                // a NaN or infinity from a misbehaving module must never reach the speakers:
                // the whole block is silenced (its other samples are suspect too) and counted
                if (! std::all_of (outputBlock.begin(), outputBlock.end(), [] (float x) { return std::isfinite (x); }))
                {
                    std::fill (outputBlock.begin(), outputBlock.end(), 0.0f);
                    nonFiniteBlocks.fetch_add (1, std::memory_order_relaxed);
                }
            }
        }

        if (inPeak > inputPeak.load())   inputPeak = inPeak;
        if (outPeak > outputPeak.load()) outputPeak = outPeak;
    }
}
