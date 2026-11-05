#pragma once
// SmoothBypass: host-visible bypass with a short crossfade and a latency-matched dry path (the Quartz pattern).
// Usage in a processor:
//   juce::AudioParameterBool* bypassParam (added to the layout as "bypass"), getBypassParameter() returns it,
//   processBlockBypassed() forwards to processBlock(), and processBlock does:
//       bypass.pushDry (buffer);   // before processing (copies the input into the delay line)
//       ... process buffer in place ...
//       bypass.mix (buffer, bypassParam->get());
//   prepare (sr, maxBlock, channels, latency) in prepareToPlay; setLatency (L) whenever the reported latency changes.
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace cmx
{
class SmoothBypass
{
public:
    void prepare (double sr, int maxBlock, int channels, int latency, double fadeMs = 12.0)
    {
        chans = juce::jmax (1, channels);
        step = (float) (1.0 / juce::jmax (1.0, fadeMs * 0.001 * sr));
        dry.setSize (chans, juce::jmax (1, maxBlock), false, true, false);
        setLatency (latency, true);
        primed = false;
    }
    void setLatency (int latency, bool force = false)
    {
        latency = juce::jmax (0, latency);
        if (latency == L && ! force && ! ring.empty()) return;
        L = latency; size = L + 1; w = 0;
        ring.assign ((size_t) chans, std::vector<float> ((size_t) size, 0.0f));
    }
    void reset() { for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f); w = 0; }
    // snap the fade to a state (e.g. on load) so there is no fade on the very first block
    void snap (bool bypassed) { g = bypassed ? 0.0f : 1.0f; primed = true; }

    void pushDry (const juce::AudioBuffer<float>& in)
    {
        const int n = in.getNumSamples(), nc = juce::jmin (chans, in.getNumChannels());
        if (dry.getNumSamples() < n) dry.setSize (chans, n, false, true, true);   // only if the host lied about maxBlock
        int w0 = w;
        for (int c = 0; c < chans; ++c)
        {
            const float* x = in.getReadPointer (juce::jmin (c, juce::jmax (0, nc - 1)));
            float* d = dry.getWritePointer (c); auto& r = ring[(size_t) c];
            int wp = w0;
            for (int i = 0; i < n; ++i)
            {
                r[(size_t) wp] = nc > 0 ? x[i] : 0.0f;
                int rp = wp - L; if (rp < 0) rp += size;
                d[i] = r[(size_t) rp];
                if (++wp == size) wp = 0;
            }
            if (c == chans - 1) w = wp;
        }
        lastN = n;
    }

    void mix (juce::AudioBuffer<float>& io, bool bypassed)
    {
        const float target = bypassed ? 0.0f : 1.0f;
        if (! primed) { g = target; primed = true; }   // first block after prepare: start in the current state, no fade
        const int n = juce::jmin (lastN, io.getNumSamples()), nc = juce::jmin (chans, io.getNumChannels());
        if (g == target && target == 1.0f) return;   // fully active: wet only, bit-exact
        float gEnd = g;
        for (int c = 0; c < nc; ++c)
        {
            float* y = io.getWritePointer (c); const float* d = dry.getReadPointer (c);
            float gg = g;
            for (int i = 0; i < n; ++i)
            {
                if (gg < target) gg = juce::jmin (target, gg + step); else if (gg > target) gg = juce::jmax (target, gg - step);
                // raised-cosine shape: no slope step at either end of the fade
                const float s = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * gg);
                y[i] = d[i] + s * (y[i] - d[i]);
            }
            gEnd = gg;
        }
        g = gEnd;
    }
    bool fullyBypassed() const noexcept { return g == 0.0f; }

private:
    juce::AudioBuffer<float> dry;
    std::vector<std::vector<float>> ring;
    int chans = 2, L = 0, size = 1, w = 0, lastN = 0;
    float g = 1.0f, step = 0.002f;
    bool primed = false;
};
} // namespace cmx
