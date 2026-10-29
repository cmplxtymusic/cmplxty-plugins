// CMPLXTY Visualizer - audio analysis (header-only, no JUCE; unit-tested by test/analysis_test.cpp).
//
// Audio thread : Analyzer::process()  - cheap IIR band energies, RMS/peak, onset detection (log-energy flux with
//                an adaptive threshold), host transport. Results go out through two lock-free SPSC FIFOs:
//                frames (one per ~5.5 ms hop) and raw stereo samples (for scopes + FFT). Never allocates, never locks.
// Render thread: Features::consume()  - drains the FIFOs, auto-gains and smooths the bands, runs the FFT for a
//                64-band log spectrum, estimates tempo from onset intervals and keeps a beat clock that follows the
//                host playhead when there is one.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vz { namespace ana {

constexpr float kPi = 3.14159265358979f;

//==============================================================================================================
// Single-producer single-consumer ring buffer. N must be a power of two. Counters wrap naturally (uint32).
template <typename T, int N>
class Spsc
{
    static_assert ((N & (N - 1)) == 0, "N must be a power of two");
public:
    bool push (const T& v) noexcept
    {
        const uint32_t w = wr.load (std::memory_order_relaxed), r = rd.load (std::memory_order_acquire);
        if (w - r >= (uint32_t) N) return false;
        buf[w & (N - 1)] = v;
        wr.store (w + 1, std::memory_order_release);
        return true;
    }
    // pushes as many as fit; returns the number pushed
    int pushMany (const T* v, int n) noexcept
    {
        const uint32_t w = wr.load (std::memory_order_relaxed), r = rd.load (std::memory_order_acquire);
        const int space = N - (int) (w - r);
        const int k = std::min (n, space);
        for (int i = 0; i < k; ++i) buf[(w + (uint32_t) i) & (N - 1)] = v[i];
        wr.store (w + (uint32_t) k, std::memory_order_release);
        return k;
    }
    bool pop (T& v) noexcept
    {
        const uint32_t r = rd.load (std::memory_order_relaxed), w = wr.load (std::memory_order_acquire);
        if (r == w) return false;
        v = buf[r & (N - 1)];
        rd.store (r + 1, std::memory_order_release);
        return true;
    }
    int size() const noexcept { return (int) (wr.load (std::memory_order_acquire) - rd.load (std::memory_order_acquire)); }
    static constexpr int capacity() { return N; }
private:
    std::array<T, N> buf {};
    alignas (64) std::atomic<uint32_t> wr { 0 };
    alignas (64) std::atomic<uint32_t> rd { 0 };
};

//==============================================================================================================
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { LP, HP, BP };
    void set (Type t, double sr, double f, double q = 0.7071)
    {
        const double w = 2.0 * 3.14159265358979 * std::min (f, sr * 0.45) / sr, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        double B0, B1, B2, A0 = 1 + al, A1 = -2 * c, A2 = 1 - al;
        if (t == LP)      { B0 = (1 - c) / 2; B1 = 1 - c; B2 = (1 - c) / 2; }
        else if (t == HP) { B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2; }
        else              { B0 = al; B1 = 0; B2 = -al; }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    inline float tick (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

struct Stereo { float l = 0, r = 0; };

struct Host
{
    bool valid = false, playing = false, hasTempo = false;
    double bpm = 120.0, ppq = 0.0;
};

// One analysis hop (~5.5 ms)
struct Frame
{
    float rms = 0, peak = 0;            // full band, linear
    float bass = 0, mid = 0, high = 0;  // band RMS, linear
    float flux = 0;                     // onset detection function (dB rise, weighted)
    uint8_t onset = 0;
    uint8_t playing = 0, hostTempo = 0;
    float bpm = 0;                      // host bpm (if hostTempo)
    double ppq = 0;                     // host ppq at the end of the hop
    float hopSec = 0;                   // hop length in seconds
};

inline float toDb (float lin) noexcept { return 20.0f * std::log10 (std::max (lin, 1.0e-7f)); }

//==============================================================================================================
class Analyzer
{
public:
    static constexpr int kFrameFifo = 1024;
    static constexpr int kSampleFifo = 32768;
    Spsc<Frame, kFrameFifo> frames;
    Spsc<Stereo, kSampleFifo> samples;

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        hop = std::max (64, (int) std::lround (sr * 0.0055));
        for (int c = 0; c < 2; ++c)
        {
            lpB[c].set (Biquad::LP, sr, 150.0);  lpB2[c].set (Biquad::LP, sr, 150.0);
            hpM[c].set (Biquad::HP, sr, 150.0);  lpM[c].set (Biquad::LP, sr, 2500.0);
            hpH[c].set (Biquad::HP, sr, 4000.0); hpH2[c].set (Biquad::HP, sr, 4000.0);
            lpB[c].reset(); lpB2[c].reset(); hpM[c].reset(); lpM[c].reset(); hpH[c].reset(); hpH2[c].reset();
        }
        resetHop();
        static constexpr double relMs[4] = { 45.0, 25.0, 20.0, 30.0 };
        for (int k = 0; k < 4; ++k) { relC[k] = (float) std::exp (-1.0 / (relMs[k] * 0.001 * sr)); env[k] = 0; for (auto& p : past[k]) p = -100.0f; }
        pastPos = 0;
        fluxHist.fill (0.0f); fluxPos = 0; sinceOnset = 1 << 30; prevFlux = 0; armed = true;
        refractory = (int) (0.11 * sr / hop);
    }

    // Reads the input only; the plug-in passes audio through untouched.
    void process (const float* const* ch, int numCh, int n, const Host& host) noexcept
    {
        if (numCh <= 0 || n <= 0) return;
        const float* L = ch[0];
        const float* R = numCh > 1 ? ch[1] : ch[0];
        Stereo tmp[256];
        int t = 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i], m = 0.5f * (l + r);
            tmp[t].l = l; tmp[t].r = r;
            if (++t == 256) { samples.pushMany (tmp, t); t = 0; }
            const float b = lpB2[0].tick (lpB[0].tick (m));
            const float md = lpM[0].tick (hpM[0].tick (m));
            const float h = hpH2[0].tick (hpH[0].tick (m));
            accFull += m * m; accB += b * b; accM += md * md; accH += h * h;
            const float in4[4] = { std::abs (b) * 1.4f, std::abs (md), std::abs (h) * 2.0f, std::abs (m) };
            for (int k = 0; k < 4; ++k) env[k] = in4[k] > env[k] ? in4[k] : env[k] * relC[k];
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            if (++count >= hop)
            {
                Host hh = host;
                if (hh.valid && hh.hasTempo && hh.playing) hh.ppq = host.ppq + (double) (i + 1) / sr * host.bpm / 60.0;
                finishHop (hh);
            }
        }
        if (t > 0) samples.pushMany (tmp, t);
    }

    int hopSize() const { return hop; }
    double sampleRate() const { return sr; }

private:
    void resetHop() { accFull = accB = accM = accH = 0; peak = 0; count = 0; }

    void finishHop (const Host& host) noexcept
    {
        const float inv = 1.0f / (float) std::max (1, count);
        Frame f;
        f.rms = std::sqrt (accFull * inv);
        f.bass = std::sqrt (accB * inv) * 1.4f;   // the 150 Hz LP keeps less energy than the others; rough equalisation
        f.mid = std::sqrt (accM * inv);
        f.high = std::sqrt (accH * inv) * 2.0f;
        f.peak = peak;
        f.hopSec = (float) (count / sr);
        // onset detection function: weighted positive dB rise of each band's peak envelope over the max of the
        // previous few hops (a max filter keeps level wobble in noisy material from looking like onsets)
        const float db[4] = { toDb (env[0]), toDb (env[1]), toDb (env[2]), toDb (env[3]) };
        static constexpr float w[4] = { 1.0f, 0.55f, 0.35f, 0.6f };
        float flux = 0;
        for (int b = 0; b < 4; ++b)
        {
            float ref = -100.0f;
            for (int k = 0; k < kRefHops; ++k) ref = std::max (ref, past[b][k]);
            const float d = db[b] > -75.0f ? std::max (0.0f, db[b] - ref - 0.5f) : 0.0f;
            flux += w[b] * std::min (d, 30.0f);
            past[b][pastPos] = db[b];
        }
        pastPos = (pastPos + 1) % kRefHops;
        f.flux = flux;
        // adaptive threshold: mean + 2 sd of the last ~0.35 s of flux, with a floor
        float mean = 0, sq = 0;
        for (float v : fluxHist) { mean += v; sq += v * v; }
        mean /= (float) fluxHist.size();
        const float sd = std::sqrt (std::max (0.0f, sq / (float) fluxHist.size() - mean * mean));
        const float thr = std::max (6.0f, mean + 2.5f * sd + 1.0f);
        ++sinceOnset;
        if (flux < thr * 0.5f) armed = true;
        if (armed && flux > thr && toDb (f.peak) > -60.0f && sinceOnset > refractory)
        {
            f.onset = 1; sinceOnset = 0; armed = false;
        }
        fluxHist[(size_t) fluxPos] = flux;
        fluxPos = (fluxPos + 1) % (int) fluxHist.size();
        prevFlux = flux;
        if (host.valid)
        {
            f.playing = host.playing ? 1 : 0;
            f.hostTempo = host.hasTempo ? 1 : 0;
            f.bpm = (float) host.bpm;
            f.ppq = host.ppq;
        }
        frames.push (f);
        resetHop();
    }

    double sr = 48000.0;
    int hop = 256, count = 0;
    Biquad lpB[2], lpB2[2], hpM[2], lpM[2], hpH[2], hpH2[2];
    float accFull = 0, accB = 0, accM = 0, accH = 0, peak = 0;
    static constexpr int kRefHops = 5;
    float env[4] {}, relC[4] {};
    float past[4][kRefHops] {};
    int pastPos = 0;
    std::array<float, 64> fluxHist {};
    int fluxPos = 0, sinceOnset = 1 << 30, refractory = 20;
    float prevFlux = 0;
    bool armed = true;
};

//==============================================================================================================
// In-place iterative radix-2 complex FFT; n must be a power of two.
inline void fft (float* re, float* im, int n) noexcept
{
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979 / len;
        const float wr = (float) std::cos (ang), wi = (float) std::sin (ang);
        for (int i = 0; i < n; i += len)
        {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k)
            {
                const int a = i + k, b = i + k + len / 2;
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

//==============================================================================================================
// What the renderer sees each frame. All "0..1" values are auto-gained and smoothed.
struct Snapshot
{
    static constexpr int kBands = 64;
    static constexpr int kWave = 1024;
    float level = 0, bass = 0, mid = 0, high = 0;     // 0..1 (x react)
    float rawLevelDb = -100;                           // for meters
    float beat = 0;                                    // pulse: 1 on a beat, decays to 0
    bool onset = false;                                // a beat happened since the last frame
    int beatCount = 0;                                 // number of beats seen
    double beatPos = 0;                                // continuous beat clock (host ppq or internal)
    float phase = 0;                                   // frac(beatPos)
    float bpm = 0;                                     // current tempo (host or detected; 0 = unknown)
    bool hostClock = false, playing = false;
    float tempoConfidence = 0;
    std::array<float, kBands> spec {};                 // 0..1, log-spaced 30 Hz .. 16 kHz
    std::array<float, kWave> waveL {}, waveR {};       // latest samples, -1..1 (x display gain)
};

class Features
{
public:
    struct Settings
    {
        float gainDb = 0;       // input sensitivity
        float smooth = 0.5f;    // 0 = jumpy, 1 = very smooth
        float react = 1.0f;     // output scaling of 0..1 values
        int beatMode = 0;       // 0 auto, 1 onsets only, 2 tempo clock only
        bool sync = true;       // follow host playhead
    };

    Features()
    {
        histL.assign (kHist, 0.0f); histR.assign (kHist, 0.0f);
        re.resize (kFft); im.resize (kFft); win.resize (kFft);
        for (int i = 0; i < kFft; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) (kFft - 1));
        lastOnsets.reserve (64);
        buildBands();
    }

    void setSampleRate (double s) { sr = s > 0 ? s : 48000.0; buildBands(); }

    // dt = seconds since the previous call (wall clock). Consumes everything available.
    template <class FrameFifo, class SampleFifo>
    const Snapshot& consume (FrameFifo& fr, SampleFifo& sm, double dt, const Settings& st)
    {
        snap.onset = false;
        bool gotOnset = false, gotFrame = false;
        Frame f, last;
        float bassPk = 0, midPk = 0, highPk = 0, rmsPk = 0;
        while (fr.pop (f))
        {
            gotFrame = true; last = f;
            bassPk = std::max (bassPk, f.bass); midPk = std::max (midPk, f.mid); highPk = std::max (highPk, f.high); rmsPk = std::max (rmsPk, f.rms);
            audioTime += f.hopSec;
            if (f.onset) { gotOnset = true; registerOnset (audioTime); }
        }
        Stereo s;
        while (sm.pop (s))
        {
            histL[(size_t) histPos] = s.l; histR[(size_t) histPos] = s.r;
            histPos = (histPos + 1) & (kHist - 1);
        }
        update (gotFrame, last, bassPk, midPk, highPk, rmsPk, gotOnset, dt, st);
        return snap;
    }

    // test hook: feed a single frame list directly
    const Snapshot& snapshot() const { return snap; }
    float detectedBpm() const { return estBpm; }

private:
    static constexpr int kHist = 4096;   // stereo history (power of 2)
    static constexpr int kFft = 2048;

    void buildBands()
    {
        // 64 log bands from 30 Hz to 16 kHz -> FFT bin ranges
        const double f0 = 30.0, f1 = 16000.0;
        for (int b = 0; b <= Snapshot::kBands; ++b)
        {
            const double fr = f0 * std::pow (f1 / f0, (double) b / Snapshot::kBands);
            edge[(size_t) b] = std::max (1, std::min (kFft / 2 - 1, (int) std::lround (fr * kFft / sr)));
        }
    }

    void registerOnset (double t)
    {
        if (! lastOnsets.empty() && t - lastOnsets.back() > 4.0) lastOnsets.clear();   // a break: start over
        if (lastOnsets.size() >= 48) lastOnsets.erase (lastOnsets.begin());
        lastOnsets.push_back (t);
        lastOnsetTime = t;
        estimateTempo();
    }

    void estimateTempo()
    {
        const size_t n = lastOnsets.size();
        if (n < 4) return;
        std::array<float, 81> hist {};   // 80..160 bpm
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n && j <= i + 4; ++j)
            {
                const double ivl = lastOnsets[j] - lastOnsets[i];
                if (ivl < 0.2 || ivl > 3.0) continue;
                for (int div = 1; div <= 4; ++div)   // an interval may span 1..4 beats
                {
                    double bpm = 60.0 * div / ivl;
                    while (bpm < 80.0) bpm *= 2.0;
                    while (bpm >= 160.0) bpm *= 0.5;
                    const float wgt = (div == 1 ? 1.0f : div == 2 ? 0.6f : 0.35f) / (float) (j - i);
                    for (int k = -2; k <= 2; ++k)
                    {
                        const int bin = (int) std::lround (bpm) - 80 + k;
                        if (bin >= 0 && bin <= 80) hist[(size_t) bin] += wgt * std::exp (-0.5f * (float) (k * k) / 1.0f) * (1.0f - std::abs ((float) (bpm - std::lround (bpm))) * 0.5f);
                    }
                }
            }
        int best = 0; float bv = 0, total = 0;
        for (int i = 0; i <= 80; ++i) { total += hist[(size_t) i]; if (hist[(size_t) i] > bv) { bv = hist[(size_t) i]; best = i; } }
        if (bv <= 0) return;
        // refine with the neighbours (parabolic)
        float bpm = 80.0f + (float) best;
        if (best > 0 && best < 80)
        {
            const float a = hist[(size_t) best - 1], b = hist[(size_t) best], c = hist[(size_t) best + 1];
            const float den = a - 2 * b + c;
            if (std::abs (den) > 1e-6f) bpm += 0.5f * (a - c) / den;
        }
        const float conf = std::min (1.0f, bv / std::max (1e-6f, total) * 4.0f);
        estBpm = estBpm <= 0 || std::abs (bpm - estBpm) > 4.0f ? bpm : estBpm + (bpm - estBpm) * 0.3f;
        confidence = conf;
    }

    float agc (float lin, float& peakDb, float gainDb, float dt)
    {
        const float db = toDb (lin) + gainDb;
        if (db > peakDb) peakDb = db;
        else peakDb = std::max (-50.0f, peakDb - 4.0f * dt);   // running peak falls 4 dB/s
        const float floorDb = std::max (peakDb - 36.0f, -72.0f);
        return std::clamp ((db - floorDb) / std::max (6.0f, peakDb - floorDb), 0.0f, 1.0f);
    }

    void update (bool gotFrame, const Frame& last, float bassPk, float midPk, float highPk, float rmsPk, bool gotOnset, double dt, const Settings& st)
    {
        const float fdt = (float) std::clamp (dt, 0.0005, 0.25);
        if (! gotFrame) { bassPk = midPk = highPk = rmsPk = 0; }
        const float tb = agc (bassPk, pkB, st.gainDb, fdt), tm = agc (midPk, pkM, st.gainDb, fdt), th = agc (highPk, pkH, st.gainDb, fdt), tl = agc (rmsPk, pkL, st.gainDb, fdt);
        const float rel = std::exp (-fdt / (0.04f + 0.6f * st.smooth));
        const float att = std::exp (-fdt / (0.004f + 0.05f * st.smooth));
        auto follow = [&] (float& v, float target) { v = target > v ? target + (v - target) * att : target + (v - target) * rel; };
        follow (sB, tb); follow (sM, tm); follow (sH, th); follow (sL, tl);
        snap.bass = std::min (1.0f, sB * st.react); snap.mid = std::min (1.0f, sM * st.react);
        snap.high = std::min (1.0f, sH * st.react); snap.level = std::min (1.0f, sL * st.react);
        snap.rawLevelDb = toDb (rmsPk);

        // ---- clock
        if (gotFrame) { hostPlaying = last.playing && last.hostTempo; if (hostPlaying) { hostBpm = last.bpm; hostPpq = last.ppq; } }
        const bool host = st.sync && hostPlaying;
        double prevPos = beatPos;
        if (host && hostPlaying)
        {
            beatPos = gotFrame ? hostPpq : beatPos + dt * hostBpm / 60.0;
            snap.bpm = hostBpm; snap.hostClock = true;
        }
        else
        {
            snap.hostClock = false;
            const float bpm = estBpm > 0 && confidence > 0.05f && (audioTime - lastOnsetTime) < 8.0 ? estBpm : 0.0f;
            snap.bpm = bpm;
            if (bpm > 0)
            {
                beatPos += dt * bpm / 60.0;
                if (gotOnset)   // phase-lock the free clock to the onsets
                {
                    const double fr = beatPos - std::floor (beatPos);
                    const double err = fr < 0.5 ? -fr : 1.0 - fr;
                    beatPos += err * 0.35;
                }
            }
            else beatPos += dt * 2.0 * 0.25;   // idle drift (no tempo): slow quarter-speed clock
        }
        if (beatPos < prevPos - 1.0) prevPos = beatPos;   // host jumped back
        const bool tick = std::floor (beatPos) > std::floor (prevPos) && (snap.bpm > 0);
        snap.beatPos = beatPos;
        snap.phase = (float) (beatPos - std::floor (beatPos));
        snap.playing = hostPlaying;
        snap.tempoConfidence = snap.hostClock ? 1.0f : confidence;

        bool beatNow = false;
        const bool recentOnsets = (audioTime - lastOnsetTime) < 2.0;
        if (st.beatMode == 1) beatNow = gotOnset;
        else if (st.beatMode == 2) beatNow = tick;
        else beatNow = gotOnset || (! recentOnsets && tick && snap.hostClock);
        if (beatNow) { snap.beat = 1.0f; snap.onset = true; ++snap.beatCount; }
        else snap.beat *= std::exp (-fdt / 0.12f);

        // ---- wave + spectrum
        const int nW = Snapshot::kWave;
        for (int i = 0; i < nW; ++i)
        {
            const int k = (histPos - nW + i) & (kHist - 1);
            snap.waveL[(size_t) i] = histL[(size_t) k];
            snap.waveR[(size_t) i] = histR[(size_t) k];
        }
        for (int i = 0; i < kFft; ++i)
        {
            const int k = (histPos - kFft + i) & (kHist - 1);
            re[(size_t) i] = 0.5f * (histL[(size_t) k] + histR[(size_t) k]) * win[(size_t) i];
            im[(size_t) i] = 0;
        }
        fft (re.data(), im.data(), kFft);
        float pk = -200;
        std::array<float, Snapshot::kBands> db {};
        for (int b = 0; b < Snapshot::kBands; ++b)
        {
            const int a = edge[(size_t) b], e = std::max (a + 1, edge[(size_t) b + 1]);
            float m = 0;
            for (int i = a; i < e; ++i) m = std::max (m, re[(size_t) i] * re[(size_t) i] + im[(size_t) i] * im[(size_t) i]);
            // tilt +3 dB/oct so a pink-ish mix looks flat
            const float tilt = 3.0f * std::log2 ((float) (a + 1) / 8.0f);
            db[(size_t) b] = 10.0f * std::log10 (m + 1e-12f) + tilt + st.gainDb;
            pk = std::max (pk, db[(size_t) b]);
        }
        if (pk > specPk) specPk = pk; else specPk = std::max (-30.0f, specPk - 6.0f * fdt);
        const float floorDb = specPk - 54.0f;
        const float srel = std::exp (-fdt / (0.05f + 0.35f * st.smooth));
        for (int b = 0; b < Snapshot::kBands; ++b)
        {
            const float v = std::clamp ((db[(size_t) b] - floorDb) / 54.0f, 0.0f, 1.0f);
            float& o = specS[(size_t) b];
            o = v > o ? v : v + (o - v) * srel;
            snap.spec[(size_t) b] = std::min (1.0f, o * o * (0.6f + 0.4f * st.react) * 1.15f);
        }
    }

    double sr = 48000.0;
    std::vector<float> histL, histR, re, im, win;
    int histPos = 0;
    std::array<int, Snapshot::kBands + 1> edge {};
    std::array<float, Snapshot::kBands> specS {};
    float specPk = -30;
    float pkB = -50, pkM = -50, pkH = -50, pkL = -50;
    float sB = 0, sM = 0, sH = 0, sL = 0;
    std::vector<double> lastOnsets;
    double audioTime = 0, lastOnsetTime = -100;
    float estBpm = 0, confidence = 0;
    double beatPos = 0, hostPpq = 0;
    float hostBpm = 120;
    bool hostPlaying = false;
    Snapshot snap;
};

}} // namespace vz::ana
