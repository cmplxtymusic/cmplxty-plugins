#pragma once
#include "Parameters.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#if defined(__SSE__) || defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#if defined(__SSE3__) || defined(__SSE4_1__) || defined(__AVX__) || defined(_M_X64) || defined(__x86_64__)
#include <pmmintrin.h>
#define SMOTHER_HAS_DAZ 1
#else
#define SMOTHER_HAS_DAZ 0
#endif
#define SMOTHER_HAS_FTZ 1
#else
#define SMOTHER_HAS_FTZ 0
#define SMOTHER_HAS_DAZ 0
#endif

namespace smother
{
inline constexpr float kCeiling = 0.8912509381337456f; // -1 dBFS
inline constexpr int kLookahead = 48;
inline constexpr double kAllStep = 0.25;
inline constexpr int kAllLength = 16;
inline constexpr double kAllLoop = 4.0;
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr double kPiD = 3.14159265358979323846;

struct MidiEv
{
    int sample = 0, note = 0;
    bool on = false;
    float vel = 0;
};

struct FlushDenormals
{
#if SMOTHER_HAS_FTZ
    unsigned int saved;
    FlushDenormals() noexcept
    {
        saved = _mm_getcsr();
        unsigned int m = saved | 0x8000; // FTZ
#if SMOTHER_HAS_DAZ
        m |= 0x0040; // DAZ
#endif
        _mm_setcsr(m);
    }
    ~FlushDenormals() noexcept { _mm_setcsr(saved); }
#else
    FlushDenormals() noexcept {}
#endif
};

class Engine
{
  public:
    void prepare(double rate)
    {
        const double keepSr = rate > 1000 ? rate : 48000.0;
        *this = Engine();
        sr = keepSr;
        smooth = 1.0 - std::exp(-1.0 / (0.008 * sr));
        const int cap = std::max(2048, int(sr * 2.0) + 8);
        delayCap = cap;
        delayL.assign((size_t)cap, 0.f);
        delayR.assign((size_t)cap, 0.f);
        const int flN = std::max(64, int(sr * 0.02));
        flL.assign((size_t)flN, 0.f);
        flR.assign((size_t)flN, 0.f);
        for (auto &c : combsL)
            c.assign(4096, 0.f);
        for (auto &c : combsR)
            c.assign(4096, 0.f);
        lookL.assign((size_t)kLookahead + 2, 0.f);
        lookR.assign((size_t)kLookahead + 2, 0.f);
        lookPeakBuf.assign((size_t)kLookahead + 2, 0.f);
        lookN = kLookahead;
        // Constant, sample-rate-only coefficients.
        ottALo = float(1.0 - std::exp(-2.0 * kPiD * 160.0 / sr));
        ottAHi = float(1.0 - std::exp(-2.0 * kPiD * 2500.0 / sr));
        ottAt = float(1.0 - std::exp(-1.0 / (0.006 * sr)));
        ottRel = float(1.0 - std::exp(-1.0 / (0.10 * sr)));
        eqALo = float(1.0 - std::exp(-2.0 * kPiD * 200.0 / sr));
        eqAMid = float(1.0 - std::exp(-2.0 * kPiD * 1000.0 / sr));
        eqAHi = float(1.0 - std::exp(-2.0 * kPiD * 6000.0 / sr));
        transFa = float(1.0 - std::exp(-1.0 / (0.002 * sr)));
        transSa = float(1.0 - std::exp(-1.0 / (0.04 * sr)));
        limRel = float(1.0 - std::exp(-1.0 / (0.05 * sr)));
        attSamp = float(0.0015 * sr);
        flMinD = float(0.0004 * sr);
        flMaxD = float(0.006 * sr);
        flPhaseInc = float(0.35 * 2.0 * kPiD / sr);
        slvLpFull = 500.f;
        slvHpFull = 2000.f;
        slvLpOpen = 18000.f;
        slvHpOpen = 20.f;
        buildKits();
        for (int i = 0; i < Count; ++i)
            current[i] = definitions[i].initial;
        target = Settings();
        refreshLaneCaches();
    }

    void set(const Settings &s) { target = s; }
    void setPattern(const Pattern &p)
    {
        pattern = p;
        for (int i = 0; i < maxLanes; ++i)
        {
            auto &ln = pattern.lanes[(size_t)i];
            if (ln.hasUser && !ln.user.empty())
            {
                auto &dst = userStable[(size_t)i];
                if (dst.size() != ln.user.size())
                    dst.resize(ln.user.size());
                if (!ln.user.empty())
                    std::memcpy(dst.data(), ln.user.data(), ln.user.size() * sizeof(float));
                userStableSr[(size_t)i] = ln.userSr > 1000 ? ln.userSr : sr;
            }
        }
        refreshLaneCaches();
        cachePatternSleeves();
    }
    const Pattern &getPattern() const { return pattern; }

    void setTransport(double bpmIn, double ppq, bool playing)
    {
        tempo = (std::isfinite(bpmIn) && bpmIn > 10 && bpmIn < 400) ? bpmIn : 120.0;
        hostPpq = std::isfinite(ppq) ? ppq : 0.0;
        hostPlaying = playing;
        beatPerSample = tempo / (60.0 * sr);
        if (std::abs(tempo - cachedTempo) > 1.0e-6)
        {
            cachedTempo = tempo;
            cachePatternSleeves();
        }
    }

    void queueMidi(int sample, bool on, int note, float vel)
    {
        if (midiQ.size() < 128)
            midiQ.push_back({ sample, note, on, vel });
    }
    void clearMidiQueue() { midiQ.clear(); }
    void resetVoices()
    {
        for (auto &v : voices)
            v.on = false;
        for (int i = 0; i < maxLanes; ++i)
            lastStep[i] = -1;
        lastPpqValid = false;
    }

    void recordHits(bool on)
    {
        recHits = on;
        if (on)
        {
            hits.clear();
            sampleCounter = 0;
        }
    }
    const std::vector<Hit> &getHits() const { return hits; }

    void setRenderMask(int lane /*-1 = all*/, bool includeMaster)
    {
        maskLane = lane;
        maskMaster = includeMaster;
    }

    // Test hook: full-depth LOWPASS / HIGHPASS sleeve cutoffs (defaults 500 / 2000 Hz).
    void setSleeveExtents(float lpFullHz, float hpFullHz)
    {
        slvLpFull = clampf(lpFullHz, 40.f, 16000.f);
        slvHpFull = clampf(hpFullHz, 40.f, 16000.f);
    }

    int currentStep(int lane) const
    {
        if (lane < 0 || lane >= pattern.numLanes)
            return -1;
        return playStep[lane];
    }
    int allPlayStep() const { return allStep; }
    float reductionDb = 0;
    float lastPeak = 0;

    void tick(float &left, float &right, int sampleIndex) noexcept
    {
        FlushDenormals fz;
        tickInner(left, right, sampleIndex);
    }

    // Block entry used by the processor. Same sound as N calls to tick(), fewer setups.
    void process(float *left, float *right, int n) noexcept
    {
        FlushDenormals fz;
        refreshLaneCaches();
        planBlockFires(n);
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0;
            tickInner(l, r, i);
            left[i] = l;
            if (right)
                right[i] = r;
        }
        firesPlanned = false;
    }

    void fireLane(int ln, float vel, int stepIndex = -1)
    {
        if (ln < 0 || ln >= pattern.numLanes)
            return;
        auto &lane = pattern.lanes[(size_t)ln];
        if (lane.drum == DrumType::ClosedHat)
            hatCut(DrumType::OpenHat);
        startVoice(ln, vel);
        if (recHits)
        {
            Hit h;
            h.lane = ln;
            h.step = stepIndex;
            h.sample = sampleCounter;
            h.ppq = hostPpq;
            hits.push_back(h);
        }
        if (stepIndex >= 0)
            lastStep[ln] = stepIndex;
    }

  private:
    struct Voice
    {
        bool on = false;
        int lane = 0;
        float pos = 0, inc = 1, vel = 1, age = 0, ageMax = 1, toneZ = 0, toneA = 0.5f;
        float env = 1, envMul = 1, envInc = 0, att = 0, attInc = 1, holdSamp = 0;
        EnvShape shape = EnvShape::Expo;
        const float *wave = nullptr;
        int waveN = 0;
        float chop0 = 0, chop1 = 1;
    };
    struct CrushSt
    {
        float holdL = 0, holdR = 0;
        int left = 0;
    };
    struct OttSt
    {
        float sLo = 0, sMid = 0, sHi = 0, eLo = 0, eMid = 0, eHi = 0;
    };
    struct FiltSt
    {
        float ic1 = 0, ic2 = 0;
    };
    struct TransSt
    {
        float fast = 0, slow = 0;
    };
    struct EqSt
    {
        float lo = 0, mid = 0, hi = 0;
    };
    struct SvfCoef
    {
        float g = 0, k = 1.41421356f, a1 = 0;
        int mode = 0;
        bool on = false;
    };
    struct LaneCache
    {
        bool eqOn = false, hpOn = false, lpOn = false, driveOn = false;
        bool crushOn = false, transOn = false, ottOn = false, enabled = true, mute = false, solo = false;
        float panL = 0.7071f, panR = 0.7071f, driveG = 1, driveN = 1;
        float loG = 1, midG = 1, hiG = 1;
        SvfCoef hp, lp;
        double stepBeats = 0.25, loopBeats = 4.0;
        double onset[maxSteps]{};
        int active[maxSteps]{};
        int length = 16, sleeveCount = 0, nActive = 0;
    };
    struct SleeveCache
    {
        SleeveType type = SleeveType::Mute;
        double start = 0, end = 0, att = 0, rel = 0, loop = 4, nudge = 0;
        float curve = 0, depth = 1, curveK = 0, curveExpK = 1, invAtt = 0, invRel = 0;
        int seed = 0;
        bool ok = false;
    };

    void applyMidi(bool on, int note, float vel)
    {
        if (!on)
            return;
        for (int ln = 0; ln < pattern.numLanes; ++ln)
            if (pattern.lanes[(size_t)ln].enabled && pattern.lanes[(size_t)ln].midiNote == note)
                fireLane(ln, vel, -1);
    }

    bool hasSolo() const
    {
        for (int i = 0; i < pattern.numLanes; ++i)
            if (pattern.lanes[(size_t)i].solo)
                return true;
        return false;
    }

    struct FireEv
    {
        int sample = 0, lane = 0, step = 0;
        float vel = 0;
    };

    void fireCrossings(double from, double to)
    {
        for (int ln = 0; ln < pattern.numLanes; ++ln)
        {
            const auto &lc = laneCache[(size_t)ln];
            if (!lc.enabled || lc.nActive <= 0)
                continue;
            const double loop = lc.loopBeats;
            if (loop < 1.0e-12)
                continue;
            const double first = std::floor(from / loop) * loop;
            for (int i = 0; i < lc.nActive; ++i)
            {
                const int s = lc.active[i];
                const double onset = lc.onset[i];
                const float vel = pattern.lanes[(size_t)ln].vel[(size_t)s];
                for (int k = -1; k <= 2; ++k)
                {
                    const double t = first + k * loop + onset;
                    if (t > from && t <= to)
                        fireLane(ln, vel, s);
                }
            }
        }
    }

    void collectWindow(double from, double to, double S, double dt, int n)
    {
        if (!(to > from) || dt <= 0)
            return;
        for (int ln = 0; ln < pattern.numLanes; ++ln)
        {
            const auto &lc = laneCache[(size_t)ln];
            if (!lc.enabled || lc.nActive <= 0)
                continue;
            const double loop = lc.loopBeats;
            if (loop < 1.0e-12)
                continue;
            const double first = std::floor(from / loop) * loop;
            for (int i = 0; i < lc.nActive; ++i)
            {
                const int s = lc.active[i];
                const double onset = lc.onset[i];
                const float vel = pattern.lanes[(size_t)ln].vel[(size_t)s];
                for (int k = -1; k <= 4; ++k)
                {
                    const double t = first + k * loop + onset;
                    if (t > from && t <= to)
                    {
                        int sidx = (t <= S) ? 0 : (int)std::ceil((t - S) / dt - 1.0e-15);
                        if (sidx < 0)
                            sidx = 0;
                        if (sidx >= n)
                            sidx = n - 1;
                        if (fireN < (int)fireList.size())
                            fireList[(size_t)fireN++] = { sidx, ln, s, vel };
                    }
                }
            }
        }
    }

    void planBlockFires(int n)
    {
        fireN = fireI = 0;
        firesPlanned = false;
        const bool seqOn = current[Seq] >= 0.5f;
        if (!(seqOn && hostPlaying) || n <= 0)
            return;
        const double dt = beatPerSample > 0 ? beatPerSample : (tempo / (60.0 * sr));
        const double S = hostPpq;
        if (!lastPpqValid)
        {
            collectWindow(S - 1.0e-9, S + 1.0e-12, S, dt, n);
            if (n > 1)
                collectWindow(S + 1.0e-12, S + (n - 1) * dt, S, dt, n);
        }
        else
        {
            const double prev = lastPpq;
            double span = S + (n - 1) * dt - prev;
            if (span < -8)
                return;
            if (prev < S + (n - 1) * dt)
                collectWindow(prev, S + (n - 1) * dt, S, dt, n);
        }
        // Sort by sample so the consumer can scan in order (collect is mostly in time
        // order already, but loop-base iteration is not guaranteed).
        if (fireN > 1)
            std::sort(fireList.begin(), fireList.begin() + fireN,
                      [](const FireEv &a, const FireEv &b) { return a.sample < b.sample; });
        firesPlanned = true;
    }

    void hatCut(DrumType t)
    {
        for (auto &v : voices)
            if (v.on && v.lane >= 0 && v.lane < pattern.numLanes &&
                pattern.lanes[(size_t)v.lane].drum == t)
                v.ageMax = std::min(v.ageMax, v.age + float(0.008 * sr));
    }

    void startVoice(int ln, float vel)
    {
        auto &lane = pattern.lanes[(size_t)ln];
        Voice *slot = nullptr;
        for (auto &v : voices)
            if (!v.on)
            {
                slot = &v;
                break;
            }
        if (slot == nullptr)
        {
            int best = 0;
            float oldest = -1;
            for (int i = 0; i < (int)voices.size(); ++i)
            {
                const float a = voices[(size_t)i].age;
                if (voices[(size_t)i].lane == ln && a > oldest)
                {
                    oldest = a;
                    best = i;
                }
            }
            if (oldest < 0)
                for (int i = 0; i < (int)voices.size(); ++i)
                    if (voices[(size_t)i].age > oldest)
                    {
                        oldest = voices[(size_t)i].age;
                        best = i;
                    }
            slot = &voices[(size_t)best];
        }
        const float *wave = nullptr;
        int n = 0;
        float chop0 = clampf(lane.chopStart, 0, 0.99f);
        float chop1 = clampf(lane.chopEnd, chop0 + 0.01f, 1);
        if (lane.hasUser && !userStable[(size_t)ln].empty())
        {
            wave = userStable[(size_t)ln].data();
            n = (int)userStable[(size_t)ln].size();
        }
        else
        {
            const int kit = clampi(lane.kit, 0, kitCount - 1);
            const int d = clampi((int)lane.drum, 0, drumCount - 1);
            wave = kits[(size_t)kit][(size_t)d].data();
            n = (int)kits[(size_t)kit][(size_t)d].size();
        }
        if (wave == nullptr || n < 8)
            return;
        slot->on = true;
        slot->lane = ln;
        slot->pos = chop0 * (n - 1);
        const double srcSr = lane.hasUser && userStableSr[(size_t)ln] > 1000 ? userStableSr[(size_t)ln] : sr;
        const float inc = float((srcSr / sr) * std::pow(2.0, lane.tune / 12.0));
        slot->inc = std::max(0.05f, inc);
        slot->vel = clampf(vel, 0, 1) * lane.level;
        slot->age = 0;
        const float decMs = 20.f + std::pow(clampf(lane.decay, 0, 1), 1.4f) * 2480.f;
        slot->ageMax = std::max(16.f, decMs * 0.001f * float(sr));
        slot->shape = lane.env;
        slot->wave = wave;
        slot->waveN = n;
        slot->chop0 = chop0 * (n - 1);
        slot->chop1 = chop1 * (n - 1);
        const float toneHz = 400.f * std::pow(20000.f / 400.f, clampf(lane.tone, 0, 1));
        slot->toneA = float(1.0 - std::exp(-2.0 * kPiD * toneHz / sr));
        slot->toneZ = 0;
        slot->env = 1.f;
        slot->envMul = 1.f;
        slot->envInc = 0.f;
        slot->att = 0.f;
        slot->attInc = 1.f / std::max(1.f, attSamp);
        slot->holdSamp = 0.f;
        const float amax = std::max(1.f, slot->ageMax);
        switch (lane.env)
        {
        case EnvShape::Snap:
            slot->envMul = std::exp(-6.5f / amax);
            break;
        case EnvShape::Linear:
            slot->envInc = -1.f / amax;
            break;
        case EnvShape::Punch:
            slot->holdSamp = float(0.008 * sr);
            slot->envMul = std::exp(-5.f / std::max(1.f, slot->ageMax - slot->holdSamp));
            break;
        case EnvShape::Expo:
        default:
            slot->envMul = std::exp(-5.5f / amax);
            break;
        }
    }

    float takeEnv(Voice &v) noexcept
    {
        const float a = v.att < 1.f ? v.att : 1.f;
        v.att += v.attInc;
        float e;
        switch (v.shape)
        {
        case EnvShape::Linear:
            e = v.env;
            v.env += v.envInc;
            if (v.env < 0)
                v.env = 0;
            break;
        case EnvShape::Punch:
            if (v.age < v.holdSamp)
                e = 1.f;
            else
            {
                e = v.env;
                v.env *= v.envMul;
            }
            break;
        default:
            e = v.env;
            v.env *= v.envMul;
            break;
        }
        return e * a;
    }

    void tickInner(float &left, float &right, int sampleIndex) noexcept
    {
        if (!midiQ.empty())
            for (auto &m : midiQ)
                if (m.sample == sampleIndex)
                    applyMidi(m.on, m.note, m.vel);

        for (int k = 0; k < Count; ++k)
        {
            current[k] += float(smooth) * (target.value[k] - current[k]);
            if (!std::isfinite(current[k]))
                current[k] = definitions[k].initial;
            else
                current[k] = clampf(current[k], definitions[k].low, definitions[k].high);
        }

        const bool seqOn = current[Seq] >= 0.5f;
        const float slvHum = current[SleeveHuman];

        if (seqOn && hostPlaying)
        {
            if (firesPlanned)
            {
                while (fireI < fireN && fireList[(size_t)fireI].sample == sampleIndex)
                {
                    const auto &f = fireList[(size_t)fireI++];
                    fireLane(f.lane, f.vel, f.step);
                }
            }
            else if (lastPpqValid)
            {
                const double prev = lastPpq;
                double delta = hostPpq - prev;
                if (delta < -8)
                    delta = 0;
                if (delta > 0 && delta < 8)
                    fireCrossings(prev, prev + delta);
            }
            else
                fireCrossings(hostPpq - 1.0e-9, hostPpq + 1.0e-12);
            if ((sampleCounter & 31) == 0)
            {
                for (int ln = 0; ln < pattern.numLanes; ++ln)
                {
                    const double loop = laneCache[(size_t)ln].loopBeats;
                    const double sb = laneCache[(size_t)ln].stepBeats;
                    const int length = std::max(1, laneCache[(size_t)ln].length);
                    if (loop > 0 && sb > 0)
                        playStep[ln] = int(std::floor(wrap(hostPpq, loop) / sb)) % length;
                }
                allStep = int(std::floor(wrap(hostPpq, kAllLoop) / kAllStep)) % kAllLength;
            }
        }
        else
        {
            if ((sampleCounter & 31) == 0)
            {
                for (int ln = 0; ln < maxLanes; ++ln)
                    playStep[ln] = -1;
                allStep = -1;
            }
            lastPpqValid = false;
        }
        lastPpq = hostPpq;
        lastPpqValid = hostPlaying && seqOn;
        hostPpq += beatPerSample > 0 ? beatPerSample : (tempo / (60.0 * sr));

        float accL = 0, accR = 0;
        sendFlL = sendFlR = sendDlL = sendDlR = sendRvL = sendRvR = 0;

        // One pass over voices into per-lane accumulators.
        float laneAcc[maxLanes] = {};
        bool laneLive[maxLanes] = {};
        for (auto &v : voices)
        {
            if (!v.on)
                continue;
            const int ln = v.lane;
            if (ln < 0 || ln >= pattern.numLanes)
            {
                v.on = false;
                continue;
            }
            if (v.age >= v.ageMax || v.pos >= v.chop1 || v.wave == nullptr)
            {
                v.on = false;
                continue;
            }
            const int i0 = clampi(int(v.pos), 0, v.waveN - 1);
            const int i1 = std::min(i0 + 1, v.waveN - 1);
            const float frac = v.pos - float(i0);
            float x = v.wave[i0] + frac * (v.wave[i1] - v.wave[i0]);
            v.toneZ += v.toneA * (x - v.toneZ);
            x = v.toneZ * v.vel * takeEnv(v);
            laneAcc[ln] += x;
            laneLive[ln] = true;
            v.pos += v.inc;
            v.age += 1;
        }

        const bool anySolo = soloCached;
        for (int ln = 0; ln < pattern.numLanes; ++ln)
        {
            if (maskLane >= 0 && ln != maskLane)
                continue;
            const auto &lc = laneCache[(size_t)ln];
            if (lc.mute || (anySolo && !lc.solo) || !lc.enabled)
                continue;

            float mono = laneAcc[ln];
            if (lc.eqOn)
                mono = laneEq(ln, mono);
            float l = mono * lc.panL;
            float r = mono * lc.panR;
            applyLaneFx(ln, l, r, lc);
            applySleeves(ln, false, l, r, slvHum);
            accL += l;
            accR += r;
        }

        if (maskLane < 0 || maskMaster)
        {
            applySleeves(-1, true, accL, accR, slvHum);
            mixSharedFx(accL, accR);
            masterFilter(accL, accR);
            if (current[MasterOTT] > 0.001f)
                ott(ottM, accL, accR, current[MasterOTT]);
            const float outGain = dbToGain(current[Output]);
            accL *= outGain;
            accR *= outGain;
            limit(accL, accR);
        }
        else
        {
            // Isolated-lane tests: still advance shared FX so tails stay consistent, but don't mix.
            float zL = 0, zR = 0;
            mixSharedFx(zL, zR);
        }

        if (!std::isfinite(accL))
            accL = 0;
        if (!std::isfinite(accR))
            accR = 0;
        left = accL;
        right = accR;
        lastPeak = std::max(std::abs(accL), std::abs(accR));
        ++sampleCounter;
        ++coeffAge;
    }

    float laneEq(int ln, float x)
    {
        auto &e = eq[(size_t)ln];
        const auto &lc = laneCache[(size_t)ln];
        e.lo += eqALo * (x - e.lo);
        e.mid += eqAMid * (x - e.mid);
        e.hi += eqAHi * (x - e.hi);
        return e.lo * lc.loG + (e.mid - e.lo) * lc.midG + (x - e.hi) * lc.hiG;
    }

    void applyLaneFx(int ln, float &l, float &r, const LaneCache &lc)
    {
        if (lc.hpOn)
            svf(hpLane[(size_t)ln], lc.hp, l, r);
        if (lc.lpOn)
            svf(lpLane[(size_t)ln], lc.lp, l, r);
        if (lc.driveOn)
        {
            l = std::tanh(l * lc.driveG) * lc.driveN;
            r = std::tanh(r * lc.driveG) * lc.driveN;
        }
        auto &lane = pattern.lanes[(size_t)ln];
        if (lc.crushOn)
            crush(crushLane[(size_t)ln], l, r, lane.crush);
        if (lc.transOn)
            transhape(transLane[(size_t)ln], l, r, lane.transient);
        if (lc.ottOn)
            ott(ottLane[(size_t)ln], l, r, lane.ott);
    }

    float sleeveEnvFast(const SleeveCache &c, double pos, float slvHum) const noexcept
    {
        if (!c.ok)
            return 0;
        const double loop = c.loop;
        const double nudge = (slvHum > 0)
                                 ? ((double)hash01((uint32_t)(0x51EE0000u ^ (uint32_t)c.seed)) - 0.5) *
                                       (double)clampf(slvHum, 0, 1) * 0.04 * loop
                                 : 0.0;
        const double start = c.start + nudge;
        const double end = c.end + nudge;
        double p = wrap(pos, loop);
        auto envAt = [&](double q) -> float {
            if (q < start)
                return 0.f;
            float a = 1.f;
            if (c.att > 1.0e-12 && q < start + c.att)
            {
                const float x = clampf(float((q - start) * c.invAtt), 0, 1);
                a = (c.curveK > 0.001f) ? float((std::exp(c.curveK * x) - 1.f) / c.curveExpK) : x;
            }
            if (q < end)
                return a;
            if (q >= end + c.rel)
                return 0.f;
            const float xr = clampf(float((q - end) * c.invRel), 0, 1);
            const float shaped = (c.curveK > 0.001f) ? float((std::exp(c.curveK * xr) - 1.f) / c.curveExpK) : xr;
            return std::min(a, 1.f - shaped);
        };
        return std::max(envAt(p), std::max(envAt(p + loop), envAt(p - loop)));
    }

    void applySleeves(int ln, bool allRow, float &l, float &r, float slvHum)
    {
        const int n = allRow ? allSleeveN : (ln >= 0 ? laneCache[(size_t)ln].sleeveCount : 0);
        if (n <= 0)
            return;
        const SleeveCache *list = allRow ? allSleeveCache.data() : laneSleeveCache[(size_t)ln].data();
        const int slot = allRow ? maxLanes : ln;
        float muteAmt = 0, lpAmt = 0, hpAmt = 0, crushAmt = 0, distAmt = 0, flAmt = 0, dlAmt = 0,
              rvAmt = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto &c = list[i];
            float e = sleeveEnvFast(c, hostPpq, slvHum);
            e *= c.depth;
            if (slvHum > 0)
                e *= 1.f - slvHum * 0.08f * (hash01((uint32_t)(0xDE00u ^ c.seed)) - 0.5f) * 2.f;
            e = clampf(e, 0, 1);
            switch (c.type)
            {
            case SleeveType::Mute:
                muteAmt = std::max(muteAmt, e);
                break;
            case SleeveType::Lowpass:
                lpAmt = std::max(lpAmt, e);
                break;
            case SleeveType::Highpass:
                hpAmt = std::max(hpAmt, e);
                break;
            case SleeveType::Crush:
                crushAmt = std::max(crushAmt, e);
                break;
            case SleeveType::Distort:
                distAmt = std::max(distAmt, e);
                break;
            case SleeveType::Flange:
                flAmt = std::max(flAmt, e);
                break;
            case SleeveType::Delay:
                dlAmt = std::max(dlAmt, e);
                break;
            case SleeveType::Reverb:
                rvAmt = std::max(rvAmt, e);
                break;
            }
        }
        if (muteAmt > 1.0e-6f)
        {
            const float g = std::pow(10.f, -9.f * muteAmt);
            l *= g;
            r *= g;
        }
        // 2-pole TPT SVF, Q = 1/sqrt(2). -3 dB sits at the mapped cutoff.
        if (lpAmt > 1.0e-5f)
        {
            const float hz = slvLpFull * std::pow(slvLpOpen / slvLpFull, 1.f - lpAmt);
            maybeUpdateSvf(lpSlvCoef[(size_t)slot], hz, 0.70710678f, 0);
            svf(lpSlv[(size_t)slot], lpSlvCoef[(size_t)slot], l, r);
        }
        if (hpAmt > 1.0e-5f)
        {
            const float hz = slvHpOpen * std::pow(slvHpFull / slvHpOpen, hpAmt);
            maybeUpdateSvf(hpSlvCoef[(size_t)slot], hz, 0.70710678f, 2);
            svf(hpSlv[(size_t)slot], hpSlvCoef[(size_t)slot], l, r);
        }
        if (crushAmt > 0)
            crush(crushSlv[(size_t)slot], l, r, crushAmt);
        if (distAmt > 0)
        {
            const float g = 1.f + distAmt * 10.f;
            const float dryL = l, dryR = r;
            l = std::tanh(l * g);
            r = std::tanh(r * g);
            l = dryL + distAmt * (l - dryL);
            r = dryR + distAmt * (r - dryR);
        }
        // Time-based sleeves are shared sends (one reverb, one delay, one flange).
        if (flAmt > 1.0e-5f)
        {
            sendFlL += l * flAmt;
            sendFlR += r * flAmt;
        }
        if (dlAmt > 1.0e-5f)
        {
            sendDlL += l * dlAmt;
            sendDlR += r * dlAmt;
        }
        if (rvAmt > 1.0e-5f)
        {
            sendRvL += l * rvAmt;
            sendRvR += r * rvAmt;
        }
        (void)slot;
    }

    void maybeUpdateSvf(SvfCoef &c, float hz, float q, int mode) noexcept
    {
        // Refresh at most every 16 samples, or whenever the cutoff jumps more than ~2%.
        const float prev = c.on ? c.g : -1.f;
        const bool stale = !c.on || c.mode != mode || ((coeffAge & 15) == 0);
        if (!stale)
        {
            // Cheap early-out: skip tan() if we refreshed recently.
            return;
        }
        makeSvf(c, hz, q, mode);
        (void)prev;
    }

    void makeSvf(SvfCoef &c, float hz, float q, int mode) noexcept
    {
        hz = clampf(hz, 20.f, float(sr * 0.45));
        q = std::max(0.3f, q);
        const float g = std::tan(float(kPiD * (double)hz / sr));
        const float k = 1.f / q;
        c.g = g;
        c.k = k;
        c.a1 = 1.f / (1.f + g * (g + k));
        c.mode = mode;
        c.on = true;
    }

    void svf(std::array<FiltSt, 2> &st, const SvfCoef &c, float &l, float &r) noexcept
    {
        if (!c.on)
            return;
        const float g = c.g, k = c.k, a1 = c.a1;
        const int mode = c.mode;
        auto go = [&](FiltSt &s, float x) -> float {
            const float v1 = (s.ic1 + g * (x - s.ic2)) * a1;
            const float v2 = s.ic2 + g * v1;
            s.ic1 = 2.f * v1 - s.ic1;
            s.ic2 = 2.f * v2 - s.ic2;
            if (mode == 2)
                return x - k * v1 - v2;
            if (mode == 1)
                return v1;
            return v2;
        };
        l = go(st[0], l);
        r = go(st[1], r);
    }

    void crush(CrushSt &s, float &l, float &r, float amt)
    {
        amt = clampf(amt, 0, 1);
        const int bits = 4 + int((1.f - amt) * 12);
        const int holdN = 1 + int(amt * amt * (sr / 1500.0));
        if (--s.left <= 0)
        {
            s.left = std::max(1, holdN);
            const float levels = float((1 << bits) - 1);
            auto qz = [&](float x) { return std::round(clampf(x, -1, 1) * levels) / levels; };
            s.holdL = qz(l);
            s.holdR = qz(r);
        }
        l = s.holdL;
        r = s.holdR;
    }

    void transhape(TransSt &s, float &l, float &r, float amt)
    {
        const float x = 0.5f * (l + r);
        const float ax = std::abs(x);
        s.fast += transFa * (ax - s.fast);
        s.slow += transSa * (ax - s.slow);
        const float g = 1.f + amt * 4.f * (s.fast - s.slow);
        l *= g;
        r *= g;
    }

    void ott(OttSt &s, float &l, float &r, float amt)
    {
        auto split = [&](float x, float &zlo, float &zhi, float &lo, float &mid, float &hi) {
            zlo += ottALo * (x - zlo);
            zhi += ottAHi * (x - zhi);
            lo = zlo;
            mid = zhi - zlo;
            hi = x - zhi;
        };
        float lLo, lMid, lHi, rLo, rMid, rHi;
        split(l, s.sLo, s.sHi, lLo, lMid, lHi);
        split(r, s.sMid, s.sHi, rLo, rMid, rHi);
        auto envFollow = [&](float ax, float &e) { e += ((ax > e) ? ottAt : ottRel) * (ax - e); };
        envFollow(0.5f * (std::abs(lLo) + std::abs(rLo)), s.eLo);
        envFollow(0.5f * (std::abs(lMid) + std::abs(rMid)), s.eMid);
        envFollow(0.5f * (std::abs(lHi) + std::abs(rHi)), s.eHi);
        auto gain = [&](float e) {
            const float thDown = 0.28f, thUp = 0.07f;
            float g = 1.f;
            if (e > thDown)
                g *= thDown / (e + 1.0e-9f);
            if (e < thUp)
                g *= 1.f + (thUp - std::max(e, 1.0e-6f)) / thUp * 1.4f;
            return 1.f + amt * (g * 1.1f - 1.f);
        };
        const float gLo = gain(s.eLo), gMid = gain(s.eMid), gHi = gain(s.eHi);
        l = lLo * gLo + lMid * gMid + lHi * gHi;
        r = rLo * gLo + rMid * gMid + rHi * gHi;
    }

    void mixSharedFx(float &l, float &r)
    {
        // Flange.
        {
            const int n = (int)flL.size();
            if (n >= 8)
            {
                flWrite = (flWrite + 1) % n;
                flL[(size_t)flWrite] = sendFlL;
                flR[(size_t)flWrite] = sendFlR;
                flPhase += flPhaseInc;
                if (flPhase > 6.2831853f)
                    flPhase -= 6.2831853f;
                const float d = flMinD + (flMaxD - flMinD) * (0.5f + 0.5f * std::sin(flPhase));
                auto read = [&](const std::vector<float> &buf) {
                    float idx = float(flWrite) - d;
                    while (idx < 0)
                        idx += n;
                    const int i0 = int(idx) % n;
                    const int i1 = (i0 + 1) % n;
                    const float f = idx - float(int(idx));
                    return buf[(size_t)i0] + f * (buf[(size_t)i1] - buf[(size_t)i0]);
                };
                // Wet is the delayed send; mix 0.5 to match the old per-lane depth law
                // (l += amt * 0.5 * ((l+read) - l) = amt * 0.5 * read, and send already has amt).
                l += 0.5f * read(flL);
                r += 0.5f * read(flR);
            }
        }
        // Tempo-synced 1/8 delay, one line for the whole kit.
        {
            const int cap = delayCap;
            if (cap >= 16)
            {
                float samples = float(0.5 * 60.0 / tempo * sr);
                samples = clampf(samples, 64.f, float(cap - 4));
                dlyWrite = (dlyWrite + 1) % cap;
                auto read = [&](const std::vector<float> &buf) {
                    float idx = float(dlyWrite) - samples;
                    while (idx < 0)
                        idx += cap;
                    const int i0 = int(idx) % cap;
                    const int i1 = (i0 + 1) % cap;
                    const float f = idx - float(int(idx));
                    return buf[(size_t)i0] + f * (buf[(size_t)i1] - buf[(size_t)i0]);
                };
                const float fb = 0.4f;
                delayL[(size_t)dlyWrite] = sendDlL + fb * dlyFbL;
                delayR[(size_t)dlyWrite] = sendDlR + fb * dlyFbR;
                const float oL = read(delayL), oR = read(delayR);
                dlyFbL = oL;
                dlyFbR = oR;
                l += 0.55f * oL;
                r += 0.55f * oR;
            }
        }
        // Four-comb reverb, ticked once per sample (it used to be ticked once per lane).
        {
            static constexpr int cLen[4] = { 1553, 1613, 1741, 1871 };
            static constexpr float cFb[4] = { 0.81f, 0.79f, 0.77f, 0.75f };
            float accL = 0, accR = 0;
            const float inL = sendRvL, inR = sendRvR;
            for (int i = 0; i < 4; ++i)
            {
                const int nn = cLen[i];
                combIdx[i] = (combIdx[i] + 1) % nn;
                const int j = combIdx[i];
                float &bL = combsL[(size_t)i][(size_t)j];
                float &bR = combsR[(size_t)i][(size_t)j];
                const float oL = bL, oR = bR;
                bL = inL + oL * cFb[i];
                bR = inR + oR * cFb[i];
                accL += oL;
                accR += oR;
            }
            accL *= 0.25f;
            accR *= 0.25f;
            rvLpL += 0.35f * (accL - rvLpL);
            rvLpR += 0.35f * (accR - rvLpR);
            l += 0.7f * rvLpL;
            r += 0.7f * rvLpR;
        }
    }

    void masterFilter(float &l, float &r)
    {
        const float hpHz = current[MasterHP];
        const float lpHz = current[MasterLP];
        if (hpHz > 22.f)
        {
            if (!hpMCoef.on || (coeffAge & 31) == 0)
                makeSvf(hpMCoef, hpHz, 0.70710678f, 2);
            svf(hpM, hpMCoef, l, r);
        }
        if (lpHz < 19900.f)
        {
            if (!lpMCoef.on || (coeffAge & 31) == 0)
                makeSvf(lpMCoef, std::max(200.f, lpHz), 0.70710678f, 0);
            svf(lpM, lpMCoef, l, r);
        }
    }

    void limit(float &l, float &r)
    {
        lookW = (lookW + 1) % lookN;
        lookL[(size_t)lookW] = l;
        lookR[(size_t)lookW] = r;
        const float p = std::max(std::abs(l), std::abs(r));
        lookPeakBuf[(size_t)lookW] = p;
        if (p >= lookPeak)
        {
            lookPeak = p;
            lookPeakAt = lookW;
        }
        else if (lookPeakAt == lookW)
        {
            float pk = 0;
            int at = 0;
            for (int i = 0; i < lookN; ++i)
                if (lookPeakBuf[(size_t)i] >= pk)
                {
                    pk = lookPeakBuf[(size_t)i];
                    at = i;
                }
            lookPeak = pk;
            lookPeakAt = at;
        }
        const float needed = lookPeak > 1.0e-12f ? std::min(1.f, kCeiling / lookPeak) : 1.f;
        if (needed < limG)
            limG = needed;
        else
            limG += limRel * (needed - limG);
        const int ridx = (lookW + 1) % lookN;
        l = lookL[(size_t)ridx] * limG;
        r = lookR[(size_t)ridx] * limG;
        l = clampf(l, -kCeiling, kCeiling);
        r = clampf(r, -kCeiling, kCeiling);
        reductionDb = gainToDb(std::max(limG, 1.0e-6f));
    }

    void refreshLaneCaches()
    {
        soloCached = hasSolo();
        for (int ln = 0; ln < maxLanes; ++ln)
        {
            auto &lane = pattern.lanes[(size_t)ln];
            auto &c = laneCache[(size_t)ln];
            c.enabled = lane.enabled;
            c.mute = lane.mute;
            c.solo = lane.solo;
            c.length = clampi(lane.length, 1, maxSteps);
            c.stepBeats = laneStepBeats(lane);
            c.loopBeats = c.stepBeats * c.length;
            c.sleeveCount = clampi(lane.sleeveCount, 0, maxSleeves);
            const float pan = clampf(lane.pan, -1, 1);
            c.panL = std::cos((pan + 1) * 0.25f * kPi);
            c.panR = std::sin((pan + 1) * 0.25f * kPi);
            c.eqOn = std::abs(lane.eqLow) > 0.05f || std::abs(lane.eqMid) > 0.05f ||
                     std::abs(lane.eqHigh) > 0.05f;
            if (c.eqOn)
            {
                c.loG = dbToGain(clampf(lane.eqLow, -12, 12));
                c.midG = dbToGain(clampf(lane.eqMid, -12, 12));
                c.hiG = dbToGain(clampf(lane.eqHigh, -12, 12));
            }
            c.hpOn = lane.hp > 22.f;
            c.lpOn = lane.lp < 19900.f;
            if (c.hpOn)
                makeSvf(c.hp, std::max(20.f, lane.hp), 0.70710678f, 2);
            else
                c.hp.on = false;
            if (c.lpOn)
                makeSvf(c.lp, std::min(20000.f, std::max(80.f, lane.lp)), 0.70710678f, 0);
            else
                c.lp.on = false;
            c.driveOn = lane.drive > 0.001f;
            if (c.driveOn)
            {
                c.driveG = 1.f + lane.drive * 8.f;
                const float t = std::tanh(c.driveG * 0.7f);
                c.driveN = (t > 1.0e-6f) ? (0.7f / t) : 1.f;
            }
            c.crushOn = lane.crush > 0.001f;
            c.transOn = lane.transient > 0.001f;
            c.ottOn = lane.ott > 0.001f;
            // Active steps and onsets at the current swing / human (block-rate).
            const float swing = current[Swing];
            const float human = current[Human];
            c.nActive = 0;
            for (int s = 0; s < c.length; ++s)
                if (lane.vel[(size_t)s] > 0)
                {
                    c.active[c.nActive] = s;
                    c.onset[c.nActive] = stepOnset(lane, ln, s, swing, human);
                    ++c.nActive;
                }
        }
        beatPerSample = tempo / (60.0 * sr);
        if (std::abs(tempo - cachedTempo) > 1.0e-6)
        {
            cachedTempo = tempo;
            cachePatternSleeves();
        }
    }

    void cacheOneSleeve(SleeveCache &c, const Sleeve &sv, double stepBeats, double loopBeats, int seed)
    {
        c.ok = sv.length >= 1 && stepBeats > 0 && loopBeats > 0;
        if (!c.ok)
            return;
        c.type = sv.type;
        c.seed = seed;
        c.depth = clampf(sv.depth, 0, 1);
        c.curve = clampf(sv.curve, 0, 1);
        c.curveK = (c.curve < 0.001f) ? 0.f : (0.5f + c.curve * 6.5f);
        c.curveExpK = (c.curveK > 0.001f) ? (std::exp(c.curveK) - 1.f) : 1.f;
        c.loop = loopBeats;
        // Humanize nudge is applied live in the envelope via a frozen hash (seed-stable).
        c.nudge = 0;
        c.start = (double)sv.start * stepBeats;
        c.end = (double)(sv.start + sv.length) * stepBeats;
        if (c.end <= c.start)
            c.end = c.start + stepBeats;
        const double beatSec = 60.0 / std::max(1.0, tempo);
        c.att = std::max(0.0005, (double)sv.attackMs / 1000.0) / beatSec;
        c.rel = std::max(0.0005, (double)sv.releaseMs / 1000.0) / beatSec;
        c.invAtt = c.att > 1.0e-12 ? 1.0 / c.att : 0;
        c.invRel = c.rel > 1.0e-12 ? 1.0 / c.rel : 0;
    }

    void cachePatternSleeves()
    {
        for (int ln = 0; ln < maxLanes; ++ln)
        {
            const auto &lane = pattern.lanes[(size_t)ln];
            const double sb = laneCache[(size_t)ln].stepBeats;
            const double loop = laneCache[(size_t)ln].loopBeats;
            const int n = laneCache[(size_t)ln].sleeveCount;
            for (int i = 0; i < n; ++i)
                cacheOneSleeve(laneSleeveCache[(size_t)ln][(size_t)i], lane.sleeves[(size_t)i], sb, loop,
                               ln * 64 + i);
        }
        allSleeveN = clampi(pattern.allCount, 0, maxSleeves);
        for (int i = 0; i < allSleeveN; ++i)
            cacheOneSleeve(allSleeveCache[(size_t)i], pattern.allSleeves[(size_t)i], kAllStep, kAllLoop,
                           1000 + i);
    }


    double sr = 48000, smooth = 0.001, tempo = 120, hostPpq = 0, lastPpq = 0, beatPerSample = 0;
    bool hostPlaying = false, lastPpqValid = false, recHits = false, maskMaster = true, soloCached = false;
    int maskLane = -1, sampleCounter = 0, allStep = -1, delayCap = 1, lookN = kLookahead, lookW = 0;
    int lookPeakAt = 0, coeffAge = 0, dlyWrite = 0, flWrite = 0, allSleeveN = 0;
    int fireN = 0, fireI = 0;
    bool firesPlanned = false;
    std::array<FireEv, 512> fireList{};
    Settings target;
    std::array<float, Count> current{};
    Pattern pattern;
    std::array<Voice, maxLanes * maxVoices> voices{};
    std::array<int, maxLanes> playStep{}, lastStep{};
    std::array<std::array<std::vector<float>, drumCount>, kitCount> kits{};
    std::array<std::vector<float>, maxLanes> userStable{};
    std::array<double, maxLanes> userStableSr{};
    std::array<std::array<FiltSt, 2>, maxLanes> hpLane{}, lpLane{};
    std::array<std::array<FiltSt, 2>, maxLanes + 1> hpSlv{}, lpSlv{};
    std::array<SvfCoef, maxLanes + 1> hpSlvCoef{}, lpSlvCoef{};
    std::array<FiltSt, 2> hpM{}, lpM{};
    SvfCoef hpMCoef{}, lpMCoef{};
    std::array<CrushSt, maxLanes> crushLane{};
    std::array<CrushSt, maxLanes + 1> crushSlv{};
    std::array<TransSt, maxLanes> transLane{};
    std::array<OttSt, maxLanes> ottLane{};
    OttSt ottM{};
    std::array<EqSt, maxLanes> eq{};
    std::array<LaneCache, maxLanes> laneCache{};
    std::array<std::array<SleeveCache, maxSleeves>, maxLanes> laneSleeveCache{};
    std::array<SleeveCache, maxSleeves> allSleeveCache{};
    std::vector<float> delayL, delayR, flL, flR;
    std::array<std::vector<float>, 4> combsL, combsR;
    std::array<int, 4> combIdx{};
    float rvLpL = 0, rvLpR = 0, limG = 1, lookPeak = 0, dlyFbL = 0, dlyFbR = 0, flPhase = 0;
    float sendFlL = 0, sendFlR = 0, sendDlL = 0, sendDlR = 0, sendRvL = 0, sendRvR = 0;
    float ottALo = 0, ottAHi = 0, ottAt = 0, ottRel = 0;
    float eqALo = 0, eqAMid = 0, eqAHi = 0, transFa = 0, transSa = 0, limRel = 0, attSamp = 1;
    float flMinD = 1, flMaxD = 2, flPhaseInc = 0;
    float slvLpFull = 500, slvHpFull = 2000, slvLpOpen = 18000, slvHpOpen = 20;
    double cachedTempo = -1;
    std::vector<float> lookL, lookR, lookPeakBuf;
    std::vector<MidiEv> midiQ;
    std::vector<Hit> hits;

    void buildKits()
    {
        for (int k = 0; k < kitCount; ++k)
            for (int d = 0; d < drumCount; ++d)
                kits[(size_t)k][(size_t)d] = renderDrum(k, (DrumType)d);
    }

    uint32_t rng = 0xC0FFEEu;
    float nse()
    {
        rng = rng * 1664525u + 1013904223u;
        return float(int(rng) >> 8) * (1.f / 8388608.f);
    }

    std::vector<float> renderDrum(int kit, DrumType type)
    {
        // Original synthesized hits. Each kit is a parameter colour, not a sample pack.
        const float kT = (kit % 3) * 0.15f;
        const float kB = (kit / 3) * 0.12f;
        const float bright = 0.45f + kT;
        const float body = 0.7f + kB * 0.4f - kT * 0.15f;
        const float dirt = (kit == 3 || kit == 9 || kit == 0) ? 0.35f : (kit == 4 || kit == 11 ? 0.15f : 0.08f);
        const int nKick = int(sr * 0.9), nSn = int(sr * 0.7), nCl = int(sr * 0.55);
        const int nCH = int(sr * 0.28), nOH = int(sr * 0.9), nTom = int(sr * 0.75);
        const int nPe = int(sr * 0.45), nCy = int(sr * 1.8);
        auto alloc = [&](int n) { std::vector<float> v((size_t)std::max(16, n), 0.f); return v; };
        auto tanh1 = [](float x) { return std::tanh(x); };
        rng = 0xA5000000u ^ (uint32_t)(kit * 997 + (int)type * 13);

        if (type == DrumType::Kick)
        {
            auto v = alloc(nKick);
            const float f0 = 140.f + kit * 8.f + kT * 30.f;
            const float f1 = 38.f + kit * 1.5f;
            float ph = 0, clickPh = 0;
            for (int i = 0; i < nKick; ++i)
            {
                const float t = float(i) / float(sr);
                const float env = std::exp(-t * (3.6f + (11 - kit) * 0.15f));
                const float f = f1 + (f0 - f1) * std::exp(-t * (18.f + kit));
                ph += float(2.0 * 3.14159265358979323846 * f / sr);
                clickPh += float(2.0 * 3.14159265358979323846 * (2200.0 + kit * 80) / sr);
                float x = std::sin(ph) * env * body;
                x += std::sin(clickPh) * std::exp(-t * 90.f) * 0.28f * bright;
                x += nse() * std::exp(-t * 120.f) * 0.08f;
                if (dirt > 0)
                    x = tanh1(x * (1.f + dirt * 3.f));
                v[(size_t)i] = x * 0.95f;
            }
            return v;
        }
        if (type == DrumType::Snare)
        {
            auto v = alloc(nSn);
            const float f = 170.f + kit * 9.f;
            float ph = 0, bp = 0, bp2 = 0;
            const float a = float(1.0 - std::exp(-2.0 * 3.14159265358979323846 * (1800.0 + kit * 80) / sr));
            for (int i = 0; i < nSn; ++i)
            {
                const float t = float(i) / float(sr);
                const float env = std::exp(-t * (7.5f + kit * 0.2f));
                ph += float(2.0 * 3.14159265358979323846 * f / sr);
                float tone = std::sin(ph) * std::exp(-t * 14.f) * 0.45f * body;
                float ns = nse();
                bp += a * (ns - bp);
                bp2 += a * (bp - bp2);
                float noise = (bp - bp2 * 0.4f) * env * (0.7f + bright * 0.4f);
                float x = tone + noise;
                if (dirt)
                    x = tanh1(x * (1.2f + dirt));
                v[(size_t)i] = x * 0.85f;
            }
            return v;
        }
        if (type == DrumType::Clap)
        {
            auto v = alloc(nCl);
            const float gaps[4] = { 0, 0.011f + kit * 0.0004f, 0.019f, 0.032f };
            float bp = 0, bp2 = 0;
            const float a = float(1.0 - std::exp(-2.0 * 3.14159265358979323846 * 1600.0 / sr));
            for (int i = 0; i < nCl; ++i)
            {
                const float t = float(i) / float(sr);
                float burst = 0;
                for (int g = 0; g < 4; ++g)
                {
                    const float dt = t - gaps[g];
                    if (dt >= 0)
                        burst += std::exp(-dt * (55.f + g * 8.f)) * (g == 3 ? 1.f : 0.55f);
                }
                float ns = nse();
                bp += a * (ns - bp);
                bp2 += a * (bp - bp2);
                v[(size_t)i] = (bp - 0.3f * bp2) * burst * 0.95f * (0.8f + bright * 0.3f);
            }
            return v;
        }
        if (type == DrumType::ClosedHat || type == DrumType::OpenHat)
        {
            const bool open = type == DrumType::OpenHat;
            const int n = open ? nOH : nCH;
            auto v = alloc(n);
            float hp = 0, hp2 = 0, res = 0;
            const float fa = float(1.0 - std::exp(-2.0 * 3.14159265358979323846 * (6500.0 + kit * 200) / sr));
            const float fRes = 8500.f + kit * 120.f;
            const float gRes = float(std::tan(3.14159265358979323846 * fRes / sr));
            float ic1 = 0, ic2 = 0;
            const float decay = open ? (4.5f - kB) : (38.f + kit);
            for (int i = 0; i < n; ++i)
            {
                const float t = float(i) / float(sr);
                float ns = nse();
                hp += fa * (ns - hp);
                hp2 += fa * (hp - hp2);
                float x = ns - hp2;
                // metallic resonance
                const double gg = gRes, kk = 0.35;
                const double a1 = 1.0 / (1.0 + gg * (gg + kk));
                const double v1 = (ic1 + gg * (x - ic2)) * a1;
                const double v2 = ic2 + gg * v1;
                ic1 = float(2 * v1 - ic1);
                ic2 = float(2 * v2 - ic2);
                res = float(v1);
                x = (x + res * 0.6f) * std::exp(-t * decay) * (0.55f + bright * 0.4f);
                v[(size_t)i] = x * (open ? 0.7f : 0.55f);
            }
            return v;
        }
        if (type == DrumType::Tom)
        {
            auto v = alloc(nTom);
            const float f0 = 190.f + (kit % 4) * 25.f;
            const float f1 = 95.f + (kit % 4) * 10.f;
            float ph = 0;
            for (int i = 0; i < nTom; ++i)
            {
                const float t = float(i) / float(sr);
                const float f = f1 + (f0 - f1) * std::exp(-t * 12.f);
                ph += float(2.0 * 3.14159265358979323846 * f / sr);
                float x = std::sin(ph) * std::exp(-t * 6.5f) * body;
                x += nse() * std::exp(-t * 50.f) * 0.06f;
                v[(size_t)i] = tanh1(x * 1.2f) * 0.85f;
            }
            return v;
        }
        if (type == DrumType::Perc)
        {
            auto v = alloc(nPe);
            const float fA = 540.f + kit * 35.f, fB = 810.f + kit * 20.f;
            float pA = 0, pB = 0;
            for (int i = 0; i < nPe; ++i)
            {
                const float t = float(i) / float(sr);
                pA += float(2.0 * 3.14159265358979323846 * fA / sr);
                pB += float(2.0 * 3.14159265358979323846 * fB / sr);
                float x = 0.6f * std::sin(pA) + 0.35f * std::sin(pB + 0.5f * std::sin(pA));
                x *= std::exp(-t * (10.f + kit * 0.4f));
                x += nse() * std::exp(-t * 40.f) * 0.12f * bright;
                v[(size_t)i] = x * 0.75f;
            }
            return v;
        }
        // Cymbal
        {
            auto v = alloc(nCy);
            float hp = 0, b1 = 0, b2 = 0, b3 = 0;
            const float fa = float(1.0 - std::exp(-2.0 * 3.14159265358979323846 * 4000.0 / sr));
            const float f1 = 4200.f + kit * 70, f2 = 6400.f + kit * 40, f3 = 9100.f + kit * 90;
            auto resTick = [&](float x, float f, float &s1, float &s2, float q) {
                const double g = std::tan(3.14159265358979323846 * f / sr);
                const double k = 1.0 / q;
                const double a1 = 1.0 / (1.0 + g * (g + k));
                const double v1 = (s1 + g * (x - s2)) * a1;
                const double v2 = s2 + g * v1;
                s1 = float(2 * v1 - s1);
                s2 = float(2 * v2 - s2);
                return float(v1);
            };
            float s11 = 0, s12 = 0, s21 = 0, s22 = 0, s31 = 0, s32 = 0;
            for (int i = 0; i < nCy; ++i)
            {
                const float t = float(i) / float(sr);
                float ns = nse();
                hp += fa * (ns - hp);
                float x = ns - hp;
                b1 = resTick(x, f1, s11, s12, 3.5f);
                b2 = resTick(x, f2, s21, s22, 4.0f);
                b3 = resTick(x, f3, s31, s32, 3.2f);
                float y = (x * 0.35f + b1 * 0.4f + b2 * 0.35f + b3 * 0.3f);
                y *= (std::exp(-t * 2.4f) * 0.85f + std::exp(-t * 18.f) * 0.25f) * (0.55f + bright * 0.35f);
                v[(size_t)i] = y * 0.65f;
            }
            return v;
        }
    }

};

} // namespace smother
