#pragma once
#include "Parameters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace fallout
{
class Engine
{
  public:
    void prepare(double rate)
    {
        *this = Engine();
        sr = rate > 1000 ? rate : 48000;
        smooth = 1 - std::exp(-1.0 / (.012 * sr));
        const int cap = std::max(256, int(sr * 2.5) + 8);
        delayL.assign((size_t)cap, 0.f);
        delayR.assign((size_t)cap, 0.f);
        delayCap = cap;
        for (int i = 0; i < Count; ++i)
            current[i] = definitions[i].initial;
        cutoffNow = definitions[Cutoff].initial;
    }
    void set(const Settings &s) { target = s; }
    void setPattern(const Pattern &p)
    {
        pattern = p;
        if (pattern.length < 1)
            pattern.length = 1;
        if (pattern.length > maxSteps)
            pattern.length = maxSteps;
    }
    const Pattern &getPattern() const { return pattern; }
    void setTransport(double bpm, double ppq, bool playing)
    {
        tempo = (std::isfinite(bpm) && bpm > 10 && bpm < 400) ? bpm : 120.0;
        hostPpq = std::isfinite(ppq) ? ppq : 0.0;
        hostPlaying = playing;
    }
    void resetVoice()
    {
        gate = false;
        env = amp = 0;
        midiHeld = 0;
        liveNote = -1;
        transpose = 0;
        lastSeqStep = -1;
        pendingOff = -1;
    }
    // Incoming MIDI at the start of a block, sample-accurate offsets applied in tick via queue.
    void queueMidi(int sample, bool on, int note, float vel)
    {
        if (midiQ.size() < 64)
            midiQ.push_back({sample, note, on, vel});
    }
    void clearMidiQueue() { midiQ.clear(); }

    void tick(float &left, float &right, int sampleIndex) noexcept
    {
        for (auto &m : midiQ)
            if (m.sample == sampleIndex)
                applyMidi(m.on, m.note, m.vel);
        for (int k = 0; k < Count; ++k)
        {
            current[k] += float(smooth) * (target.value[k] - current[k]);
            if (!std::isfinite(current[k]))
                current[k] = definitions[k].initial;
            else
                current[k] = std::clamp(current[k], definitions[k].low, definitions[k].high);
        }
        const bool seqOn = current[Seq] >= .5f;
        const int rateIdx = clampi(int(std::lround(current[Rate])), 0, 7);
        const int length = clampi(int(std::lround(current[Length])), 1, maxSteps);
        pattern.length = length;
        const double stepBeats = rateBeats[rateIdx];
        const double loopBeats = length * stepBeats;
        const double human = std::clamp(current[Human], 0.f, 1.f);

        if (seqOn && hostPlaying && loopBeats > 1.e-6)
        {
            // Detect step crossings between the previous and current ppq, including wrap.
            const double pos = wrap(hostPpq, loopBeats);
            if (lastPpqValid)
            {
                double prev = wrap(lastPpq, loopBeats);
                double delta = pos - prev;
                if (delta < -loopBeats * .5)
                    delta += loopBeats;
                if (delta > 0 && delta < loopBeats * .75)
                    triggerCrossings(prev, prev + delta, loopBeats, stepBeats, length, human);
            }
            else
            {
                // First sample of a run: fire a step whose trigger sits on this beat (usually step 0).
                triggerCrossings(pos - 1.0e-9, pos + 1.0e-12, loopBeats, stepBeats, length, human);
            }
            playStep = int(std::floor(wrap(pos, loopBeats) / stepBeats)) % length;
            playPpq = pos;
        }
        else
        {
            playStep = -1;
            lastPpqValid = false;
        }
        lastPpq = hostPpq;
        lastPpqValid = hostPlaying && seqOn;
        hostPpq += tempo / (60.0 * sr); // advance one sample of musical time for the next tick
                                          // (processor overwrites from the host at block start)

        // Pitch
        float targetMidi = voiceMidi + current[Tune] + voiceMicro * .01f;
        if (!gate && liveNote < 0)
            targetMidi = currentMidi;
        const float glideSec = .003f + current[Glide] * .45f;
        const float glideA = float(1 - std::exp(-1.0 / (glideSec * sr)));
        if (sliding && gate)
            currentMidi += (targetMidi - currentMidi) * glideA;
        else if (gate)
            currentMidi = targetMidi;
        const float hz = std::clamp(midiToHz(currentMidi), 8.f, float(sr * .42));

        // Envelopes
        const float decSec = std::max(.005f, current[Decay] * .001f);
        const float decA = float(std::exp(-1.0 / (decSec * sr)));
        if (gate)
        {
            env *= decA;
            amp += float(1 - std::exp(-1.0 / (.003 * sr))) * (ampTarget - amp);
        }
        else
        {
            env *= decA;
            amp *= float(std::exp(-1.0 / (.01 * sr)));
        }
        if (env < 1.e-6f)
            env = 0;
        if (amp < 1.e-6f)
            amp = 0;

        const float envAmt = current[EnvMod] * (voiceAccent ? (1.f + current[Accent]) : 1.f);
        const float cutHz = std::clamp(current[Cutoff] * std::exp2(env * envAmt * 4.8f), 20.f,
                                       float(sr * .42));
        cutoffNow += float(smooth) * (cutHz - cutoffNow);

        // 2x oversampled nonlinear path (osc + ladder + drive)
        float acc = 0;
        const double sr2 = sr * 2;
        const float hz2 = hz;
        for (int s = 0; s < 2; ++s)
        {
            const float osc = oscillator(hz2, sr2);
            const float filtered = ladder(osc * (.7f + current[Drive] * .9f), cutoffNow, current[Resonance], sr2);
            const float driven = filtered * (1.f + current[Drive] * 3.6f);
            const float sat = adaa(driven, prevDrive);
            prevDrive = driven;
            const float mixed = filtered * (1.f - current[Drive]) + sat * current[Drive];
            acc += mixed;
        }
        float wet = clean(acc * .5f * amp);

        // Delay
        const int ddiv = clampi(int(std::lround(current[DelayDiv])), 0, 7);
        const float delayBeats = float(rateBeats[ddiv]);
        const float delaySamp = std::clamp(float(delayBeats * 60.0 / tempo * sr), 1.f, float(delayCap - 4));
        if (std::abs(delaySamp - delaySamples) > float(sr * .15))
            delaySamples = delaySamp; // snap on start-up or a large tempo/division jump
        else
            delaySamples += float(smooth) * (delaySamp - delaySamples);
        const float toneA = float(std::exp(-2 * pi * std::min(current[DelayTone], float(sr * .4)) / sr));
        float delayed = readDelay(delaySamples);
        delayLp += (1.f - toneA) * (delayed - delayLp);
        delayed = delayLp;
        const int wi = delayWrite % delayCap;
        const float fb = std::clamp(current[DelayFb], 0.f, .95f);
        const float inject = wet + delayed * fb;
        delayL[(size_t)wi] = inject;
        delayR[(size_t)wi] = inject;
        delayWrite = (delayWrite + 1) % delayCap;
        const float mix = std::clamp(current[DelayMix], 0.f, 1.f);
        float l = wet * (1 - mix) + delayed * mix;
        float r = l;

        const float out = std::pow(10.f, current[Output] / 20.f);
        l *= out;
        r *= out;
        const float peak = std::max(std::abs(l), std::abs(r));
        const float need = std::min(1.f, .89125094f / std::max(peak, 1.e-12f));
        limiter = need < limiter ? need : limiter + float(1 - std::exp(-1 / (.080 * sr))) * (need - limiter);
        left = clean(std::clamp(l * limiter, -.89125094f, .89125094f));
        right = clean(std::clamp(r * limiter, -.89125094f, .89125094f));
        reductionDb = -20 * std::log10(std::max(limiter, 1.e-8f));
        outPeak = std::max(std::abs(left), std::abs(right));
    }
    // After a block, the processor sets host transport again; we also keep lastPpq in beats.

    int currentStep() const { return playStep; }
    float meter() const { return outPeak; }
    float reductionDb = 0;
    float outPeak = 0;
    int pendingOff = -1; // last released live note, for tests
    int transpose = 0;
    bool gate = false;

    // Pitch currently heading to (for glide tests)
    float currentMidi = 36, voiceMidi = 36, voiceMicro = 0, cutoffNow = 720;

  private:
    struct MidiEv
    {
        int sample, note;
        bool on;
        float vel;
    };
    static constexpr double pi = 3.14159265358979323846;
    static float clean(float x)
    {
        return std::isfinite(x) && std::abs(x) > 1.e-25f ? std::clamp(x, -32.f, 32.f) : 0.f;
    }
    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static double wrap(double x, double m)
    {
        if (m <= 0)
            return 0;
        x = std::fmod(x, m);
        if (x < 0)
            x += m;
        return x;
    }
    static float logcosh(float x)
    {
        const float a = std::abs(x);
        return a + std::log1p(std::exp(-2 * a)) - .69314718056f;
    }
    static float adaa(float x, float old)
    {
        return std::abs(x - old) > 1.e-3f ? (logcosh(x) - logcosh(old)) / (x - old) : std::tanh((x + old) * .5f);
    }
    static float polyblep(float t, float dt)
    {
        if (dt <= 0)
            return 0;
        if (t < dt)
        {
            float x = t / dt;
            return x + x - x * x - 1.f;
        }
        if (t > 1.f - dt)
        {
            float x = (t - 1.f) / dt;
            return x + x + x * x + 1.f;
        }
        return 0.f;
    }
    static float saw(float t, float dt) { return (2.f * t - 1.f) - polyblep(t, dt); }
    static float pulse(float t, float dt, float pw)
    {
        float x = t < pw ? 1.f : -1.f;
        x += polyblep(t, dt);
        float s = t - pw;
        if (s < 0)
            s += 1.f;
        x -= polyblep(s, dt);
        return x;
    }
    float random()
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return float(double(rng) / 2147483648.0 - 1);
    }
    float oscillator(float hz, double rate)
    {
        const int kind = clampi(int(std::lround(current[Osc])), 0, 5);
        const float dt0 = std::clamp(hz / float(rate), 1.e-6f, .49f);
        auto advance = [&](float &ph, float dt)
        {
            ph += dt;
            if (ph >= 1.f)
                ph -= std::floor(ph);
            if (ph < 0)
                ph = 0;
        };
        float out = 0;
        switch (kind)
        {
        case 0: // EDGE saw
            advance(phase[0], dt0);
            out = saw(phase[0], dt0);
            break;
        case 1: // SLAB square
            advance(phase[0], dt0);
            out = pulse(phase[0], dt0, .5f);
            break;
        case 2: // DUTY pulse
            advance(phase[0], dt0);
            out = pulse(phase[0], dt0, std::clamp(current[Pwm], .05f, .95f));
            break;
        case 3: // HORDE detuned saws
        {
            const float spread[5] = {-1.f, -.47f, 0.f, .53f, 1.09f};
            const float cents = current[Detune] * 28.f;
            out = 0;
            for (int i = 0; i < 5; ++i)
            {
                const float dti = std::clamp(dt0 * std::exp2(spread[i] * cents / 1200.f), 1.e-6f, .49f);
                advance(phase[i], dti);
                out += saw(phase[i], dti);
            }
            out *= .22f;
            break;
        }
        case 4: // FOLD sine-fold
            advance(phase[0], dt0);
            {
                const float s = std::sin(phase[0] * float(2 * pi));
                const float g = 1.f + current[Fold] * 8.f;
                out = std::sin(s * g);
            }
            break;
        default: // SNARL noise-ring
            advance(phase[0], dt0);
            out = saw(phase[0], dt0) * (random() * .85f);
            // mild lowpass so it is not only hiss
            snarlLp += .35f * (out - snarlLp);
            out = snarlLp;
            break;
        }
        return clean(out);
    }
    float ladder(float in, float cutoff, float res, double rate)
    {
        const float ny = float(rate * .42);
        cutoff = std::clamp(cutoff, 20.f, ny);
        const float wc = std::tan(float(pi) * cutoff / float(rate));
        const float g = wc / (1.f + wc);
        const float k = std::clamp(res, 0.f, 1.f) * 3.9f * (1.f - .25f * g);
        float x = std::tanh(in - k * lp[3]);
        for (int i = 0; i < 4; ++i)
        {
            const float y = lp[i] + g * (x - lp[i]);
            lp[i] = clean(y);
            x = y;
        }
        return clean(x);
    }
    float readDelay(float samples)
    {
        samples = std::clamp(samples, 1.f, float(delayCap - 4));
        float read = float(delayWrite) - samples;
        while (read < 0)
            read += float(delayCap);
        const int i0 = int(read) % delayCap;
        const int i1 = (i0 + 1) % delayCap;
        const float f = read - std::floor(read);
        return delayL[(size_t)i0] * (1 - f) + delayL[(size_t)i1] * f;
    }
    void applyMidi(bool on, int note, float vel)
    {
        const int mode = clampi(int(std::lround(current[MidiMode])), 0, 2);
        if (mode == 0)
            return;
        note = clampNote(note);
        if (note < 0)
            return;
        if (mode == 2) // Transpose: held key relative to C3
        {
            if (on)
            {
                if (midiHeld < (int)held.size())
                    held[(size_t)midiHeld++] = note;
                transpose = held[0] - 60;
            }
            else
            {
                for (int i = 0; i < midiHeld; ++i)
                    if (held[(size_t)i] == note)
                    {
                        for (int j = i; j < midiHeld - 1; ++j)
                            held[(size_t)j] = held[(size_t)j + 1];
                        --midiHeld;
                        --i;
                    }
                transpose = midiHeld > 0 ? held[0] - 60 : 0;
            }
            return;
        }
        // Live: a note while another is held is a legato slide, like a 303 tie.
        if (on)
        {
            const bool legato = midiHeld > 0 && gate;
            liveNote = note;
            startVoice(float(note), 0, vel > .85f, legato);
            if (midiHeld < (int)held.size())
                held[(size_t)midiHeld++] = note;
        }
        else
        {
            for (int i = 0; i < midiHeld; ++i)
                if (held[(size_t)i] == note)
                {
                    for (int j = i; j < midiHeld - 1; ++j)
                        held[(size_t)j] = held[(size_t)j + 1];
                    --midiHeld;
                    --i;
                }
            if (midiHeld > 0)
            {
                liveNote = held[midiHeld - 1];
                startVoice(float(liveNote), 0, false, false);
            }
            else
            {
                liveNote = -1;
                pendingOff = note;
                // If the sequencer is running, do not choke a seq note mid-step.
                if (!(current[Seq] >= .5f && hostPlaying))
                    gate = false;
            }
        }
    }
    void startVoice(float midi, float micro, bool accented, bool slide)
    {
        voiceMidi = midi;
        voiceMicro = micro;
        voiceAccent = accented;
        sliding = slide;
        gate = true;
        const float peak = accented ? (1.f + current[Accent] * .75f) : 1.f;
        if (!slide)
        {
            env = peak;
            ampTarget = peak;
            amp = std::max(amp, peak * .35f);
            currentMidi = midi + current[Tune] + micro * .01f;
        }
        else
            ampTarget = peak;
    }
    void triggerCrossings(double from, double to, double loopBeats, double stepBeats, int length, double human)
    {
        // Scan steps whose trigger points fall in (from, to], handling a single wrap already
        // flattened by the caller (to may be > loopBeats).
        for (int n = 0; n < length; ++n)
        {
            const auto &st = pattern.steps[(size_t)n];
            const double nudge = std::clamp((double)st.rubato, -.45, .45) * human * stepBeats;
            double t = n * stepBeats + nudge;
            double t2 = t + loopBeats;
            auto hit = [&](double trig)
            {
                if (trig > from && trig <= to)
                    fireStep(n);
            };
            hit(t);
            hit(t2);
        }
    }
    void fireStep(int n)
    {
        if (liveNote >= 0)
            return; // live MIDI has priority
        lastSeqStep = n;
        const auto &st = pattern.steps[(size_t)n];
        const bool prevSlide = n > 0 ? pattern.steps[(size_t)n - 1].slide
                                     : pattern.steps[(size_t)(pattern.length - 1)].slide;
        // Slide is on the source step: the previous step's slide glides into this one.
        if (st.note < 0)
        {
            if (!prevSlide)
                gate = false;
            return;
        }
        const float midi = float(clampNote(st.note + transpose));
        startVoice(midi, st.micro, st.accent, prevSlide && gate);
        playStep = n;
    }

    double sr = 48000, smooth = .001, tempo = 120, hostPpq = 0, lastPpq = 0, playPpq = 0;
    bool hostPlaying = false, lastPpqValid = false, sliding = false, voiceAccent = false;
    Settings target;
    std::array<float, Count> current{};
    Pattern pattern;
    std::array<float, 5> phase{};
    std::array<float, 4> lp{};
    float env = 0, amp = 0, ampTarget = 0, prevDrive = 0, snarlLp = 0, delayLp = 0, delaySamples = 1000,
          limiter = 1;
    std::vector<float> delayL, delayR;
    int delayCap = 1, delayWrite = 0, playStep = -1, lastSeqStep = -1, liveNote = -1, midiHeld = 0;
    std::array<int, 16> held{};
    uint32_t rng = 0xC0FFEE11;
    std::vector<MidiEv> midiQ;
};
} // namespace fallout
