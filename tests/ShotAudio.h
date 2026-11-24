// Optional music for the screenshot modes: --screenshots <dir> [file.wav]. AGPL-3.0-or-later.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

struct ShotAudio
{
    juce::AudioBuffer<float> clip;
    int pos = 0;
    bool loaded() const { return clip.getNumSamples() > 0; }
    static ShotAudio fromArgs (int argc, char** argv)
    {
        ShotAudio s;
        if (argc < 4) return s;
        juce::WavAudioFormat wav;
        auto in = juce::File (juce::String (argv[3])).createInputStream();
        if (in == nullptr) return s;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (in.release(), true));
        if (r == nullptr) return s;
        s.clip.setSize (2, (int) r->lengthInSamples);
        r->read (&s.clip, 0, (int) r->lengthInSamples, 0, true, true);
        return s;
    }
    /** Replaces the buffer with the next stretch of the clip (looping), if one was given. */
    void fill (juce::AudioBuffer<float>& b)
    {
        if (! loaded()) return;
        for (int i = 0; i < b.getNumSamples(); ++i, pos = (pos + 1) % clip.getNumSamples())
            for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, clip.getSample (c % 2, pos));
    }
};
