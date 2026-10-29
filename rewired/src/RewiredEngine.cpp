#include "RewiredEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <queue>
#include <thread>

namespace rw
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
using cpx = std::complex<float>;

// ---------------------------------------------------------------------------------------------------- radix-2 FFT
struct FFT
{
    explicit FFT (int size) : n (size), rev ((size_t) size), tw ((size_t) size / 2)
    {
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));
    }
    void run (cpx* x, bool inverse) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    cpx w = tw[(size_t) (j * step)]; if (inverse) w = std::conj (w);
                    const cpx u = x[i + j], v = x[i + j + half] * w;
                    x[i + j] = u + v; x[i + j + half] = u - v;
                }
        }
    }
    int n; std::vector<int> rev; std::vector<cpx> tw;
};

std::vector<float> hann (int n) { std::vector<float> w ((size_t) n); for (int i = 0; i < n; ++i) w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / n)); return w; }

bool cancelled (const std::atomic<bool>* c) { return c != nullptr && c->load(); }

// mono power spectrogram (frames x bins), frame k centred at k*hop
std::vector<std::vector<float>> powerSpec (const std::vector<float>& x, int nfft, int hop)
{
    FFT fft (nfft); const auto w = hann (nfft);
    const int frames = (int) (x.size() / (size_t) hop) + 1, bins = nfft / 2 + 1;
    std::vector<std::vector<float>> S ((size_t) frames, std::vector<float> ((size_t) bins));
    std::vector<cpx> buf ((size_t) nfft);
    for (int k = 0; k < frames; ++k)
    {
        const long c = (long) k * hop - nfft / 2;
        for (int i = 0; i < nfft; ++i) { const long j = c + i; buf[(size_t) i] = (j >= 0 && j < (long) x.size()) ? x[(size_t) j] * w[(size_t) i] : 0.0f; }
        fft.run (buf.data(), false);
        for (int b = 0; b < bins; ++b) S[(size_t) k][(size_t) b] = std::norm (buf[(size_t) b]);
    }
    return S;
}

double hzToMidi (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0); }
} // namespace

// ---------------------------------------------------------------------------------------------------- resampling (windowed sinc)
std::vector<float> resample (const std::vector<float>& x, double from, double to)
{
    if (x.empty() || std::abs (from - to) < 1e-6) return x;
    const double ratio = to / from, cut = std::min (1.0, ratio) * 0.97;
    const int zc = 24; const double W = zc / cut;
    const size_t nOut = (size_t) std::floor ((double) x.size() * ratio);
    std::vector<float> y (nOut);
    for (size_t i = 0; i < nOut; ++i)
    {
        const double t = (double) i / ratio;
        const long a = (long) std::ceil (t - W), b = (long) std::floor (t + W);
        double s = 0.0;
        for (long k = std::max (0L, a); k <= std::min ((long) x.size() - 1, b); ++k)
        {
            const double d = t - (double) k, u = d / W;
            const double arg = kPi * cut * d;
            const double sinc = std::abs (arg) < 1e-9 ? 1.0 : std::sin (arg) / arg;
            const double win = 0.42 + 0.5 * std::cos (kPi * u) + 0.08 * std::cos (2.0 * kPi * u);
            s += x[(size_t) k] * cut * sinc * win;
        }
        y[i] = (float) s;
    }
    return y;
}

// ---------------------------------------------------------------------------------------------------- models
bool Engine::load (const std::string& dir, std::string& err, int threads)
{
    auto join = [&] (const std::string& f) { const char last = dir.empty() ? '/' : dir.back(); return dir + ((last == '/' || last == '\\') ? "" : "/") + f; };
    auto* rt = ort::loadRuntime (join (ort::runtimeFileName()), err);
    if (rt == nullptr) return false;
    if (threads <= 0) threads = std::max (1, (int) std::thread::hardware_concurrency() - 1);
    if (! spleeter.load (*rt, join ("spleeter4.onnx"), threads, err)) { err = "spleeter4.onnx: " + err; return false; }
    if (! pitch.load (*rt, join ("nmp.onnx"), threads, err)) { err = "nmp.onnx: " + err; return false; }
    return true;
}

// ---------------------------------------------------------------------------------------------------- Spleeter separation
// STFT 4096 / hop 1024 / periodic Hann, model sees |X| below bin 1024 in 512-frame segments and predicts 4 magnitudes;
// ratio masks (power 2) are applied to the complex STFT. Bins >= 1024 (above ~11 kHz) go to drums so hats and cymbals keep their air.
bool Engine::separate (const Audio& song, std::array<std::vector<float>, 4>& out, std::string& err, const std::atomic<bool>* cancel, Progress prog)
{
    constexpr int F = 4096, H = 1024, B = F / 2 + 1, FM = 1024, T = 512;
    const size_t N = song.size();
    const float* ch[2] = { song.left.data(), song.right.empty() ? song.left.data() : song.right.data() };
    const long P0 = F - H;
    const int frames = (int) ((P0 + (long) N + H - 1) / H) + 1;
    const int segs = (frames + T - 1) / T;
    const size_t padLen = (size_t) frames * H + F;
    std::array<std::vector<float>, 4> acc; for (auto& a : acc) a.assign (padLen, 0.0f);
    FFT fft (F); const auto w = hann (F);
    std::vector<cpx> spec ((size_t) 2 * T * B);
    std::vector<float> mag ((size_t) 2 * T * FM);
    std::vector<cpx> buf ((size_t) F);
    std::vector<ort::Tensor> res;
    for (int s = 0; s < segs; ++s)
    {
        if (cancelled (cancel)) { err = "cancelled"; return false; }
        if (prog) prog ((float) s / (float) segs, "Separating stems");
        std::fill (mag.begin(), mag.end(), 0.0f);
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < T; ++t)
            {
                const int k = s * T + t;
                cpx* S = &spec[((size_t) c * T + (size_t) t) * B];
                if (k >= frames) { std::fill (S, S + B, cpx {}); continue; }
                const long start = (long) k * H - P0;
                for (int i = 0; i < F; ++i) { const long j = start + i; buf[(size_t) i] = (j >= 0 && j < (long) N) ? ch[c][j] * w[(size_t) i] : 0.0f; }
                fft.run (buf.data(), false);
                std::copy (buf.begin(), buf.begin() + B, S);
                float* M = &mag[((size_t) c * T + (size_t) t) * FM];
                for (int b = 0; b < FM; ++b) M[b] = std::abs (S[b]);
            }
        if (! spleeter.run ("mag", mag.data(), { 1, 2, T, FM }, { "est" }, res, err)) return false;
        const float* est = res[0].data.data();   // [4,1,2,T,FM]
        const size_t stemStride = (size_t) 2 * T * FM;
        for (int t = 0; t < T; ++t)
        {
            const int k = s * T + t; if (k >= frames) break;
            for (int st = 0; st < 4; ++st)
            {
                for (int b = 0; b < B; ++b)
                {
                    cpx y {};
                    for (int c = 0; c < 2; ++c)
                    {
                        const cpx X = spec[((size_t) c * T + (size_t) t) * B + (size_t) b];
                        float m;
                        if (b < FM)
                        {
                            const size_t o = ((size_t) c * T + (size_t) t) * FM + (size_t) b;
                            float sum = 1e-10f, mine = 0.0f;
                            for (int q = 0; q < 4; ++q) { const float e = est[(size_t) q * stemStride + o]; sum += e * e; if (q == st) mine = e * e; }
                            m = (mine + 1e-10f / 4.0f) / sum;
                        }
                        else m = st == 1 ? 1.0f : 0.0f;
                        y += X * (0.5f * m);
                    }
                    buf[(size_t) b] = y;
                }
                for (int b = B; b < F; ++b) buf[(size_t) b] = std::conj (buf[(size_t) (F - b)]);
                fft.run (buf.data(), true);
                float* a = &acc[(size_t) st][(size_t) k * H];
                const float g = 1.0f / ((float) F * 1.5f);
                for (int i = 0; i < F; ++i) a[i] += buf[(size_t) i].real() * w[(size_t) i] * g;
            }
        }
    }
    for (int st = 0; st < 4; ++st) out[(size_t) st].assign (acc[(size_t) st].begin() + P0, acc[(size_t) st].begin() + P0 + (long) N);
    if (prog) prog (1.0f, "Separating stems");
    return true;
}

// ---------------------------------------------------------------------------------------------------- basic-pitch
namespace bp
{
constexpr int kSr = 22050, kHop = 256, kWin = 43844, kOverlapFrames = 30, kOverlap = kOverlapFrames * kHop, kStep = kWin - kOverlap,
              kFramesPerWin = 172, kFps = kSr / kHop, kBins = 88, kMidiOffset = 21;

double frameTime (int f)
{
    const double winOffset = ((double) kHop / kSr) * (kFramesPerWin - (double) kWin / kHop) + 0.0018;
    return (double) f * kHop / kSr - winOffset * std::floor ((double) f / kFramesPerWin);
}

// port of basic_pitch.note_creation.output_to_notes_polyphonic (infer_onsets, melodia_trick, energy_tol 11)
std::vector<Note> decode (std::vector<float> frames, std::vector<float> onsets, int n, float onsetThresh, float frameThresh, int minLen,
                          int minIdx, int maxIdx)
{
    auto at = [] (std::vector<float>& m, int t, int f) -> float& { return m[(size_t) t * kBins + (size_t) f]; };
    for (int t = 0; t < n; ++t)
        for (int f = 0; f < kBins; ++f)
            if (f < minIdx || f >= maxIdx) { at (frames, t, f) = 0; at (onsets, t, f) = 0; }
    // inferred onsets
    {
        std::vector<float> diff ((size_t) n * kBins, 0.0f);
        float maxDiff = 0.0f, maxOn = 0.0f;
        for (int t = 0; t < n; ++t)
            for (int f = 0; f < kBins; ++f)
            {
                maxOn = std::max (maxOn, at (onsets, t, f));
                if (t < 2) continue;
                const float d = std::min (at (frames, t, f) - at (frames, t - 1, f), at (frames, t, f) - at (frames, t - 2, f));
                diff[(size_t) t * kBins + (size_t) f] = std::max (0.0f, d);
                maxDiff = std::max (maxDiff, std::max (0.0f, d));
            }
        if (maxDiff > 0.0f)
            for (size_t i = 0; i < diff.size(); ++i) onsets[i] = std::max (onsets[i], maxOn * diff[i] / maxDiff);
    }
    struct On { int t, f; };
    std::vector<On> ons;
    for (int t = 1; t < n - 1; ++t)
        for (int f = 0; f < kBins; ++f)
        {
            const float v = at (onsets, t, f);
            if (v > at (onsets, t - 1, f) && v > at (onsets, t + 1, f) && v >= onsetThresh) ons.push_back ({ t, f });
        }
    std::reverse (ons.begin(), ons.end());
    std::vector<float> rem = frames;
    struct Ev { int s, e, p; float amp; };
    std::vector<Ev> evs;
    const int energyTol = 11, maxF = kBins - 1;
    auto mean = [&] (int s, int e, int f) { double a = 0; for (int t = s; t < e; ++t) a += at (frames, t, f); return (float) (e > s ? a / (e - s) : 0.0); };
    for (const auto& o : ons)
    {
        if (o.t >= n - 1) continue;
        int i = o.t + 1, k = 0;
        while (i < n - 1 && k < energyTol) { if (at (rem, i, o.f) < frameThresh) ++k; else k = 0; ++i; }
        i -= k;
        if (i - o.t <= minLen) continue;
        for (int t = o.t; t < i; ++t)
        {
            at (rem, t, o.f) = 0;
            if (o.f < maxF) at (rem, t, o.f + 1) = 0;
            if (o.f > 0) at (rem, t, o.f - 1) = 0;
        }
        evs.push_back ({ o.t, i, o.f + kMidiOffset, mean (o.t, i, o.f) });
    }
    // melodia trick: grow notes from the strongest leftover frames (priority queue instead of repeated argmax; values only ever drop to 0)
    struct Q { float v; int idx; bool operator< (const Q& b) const { return v < b.v || (v == b.v && idx > b.idx); } };
    std::priority_queue<Q> pq;
    for (int i = 0; i < n * kBins; ++i) if (rem[(size_t) i] > frameThresh) pq.push ({ rem[(size_t) i], i });
    while (! pq.empty())
    {
        const Q q = pq.top(); pq.pop();
        if (rem[(size_t) q.idx] != q.v || q.v <= frameThresh) continue;
        const int iMid = q.idx / kBins, f = q.idx % kBins;
        rem[(size_t) q.idx] = 0;
        auto clear = [&] (int t) { at (rem, t, f) = 0; if (f < maxF) at (rem, t, f + 1) = 0; if (f > 0) at (rem, t, f - 1) = 0; };
        int i = iMid + 1, k = 0;
        while (i < n - 1 && k < energyTol) { if (at (rem, i, f) < frameThresh) ++k; else k = 0; clear (i); ++i; }
        const int iEnd = i - 1 - k;
        i = iMid - 1; k = 0;
        while (i > 0 && k < energyTol) { if (at (rem, i, f) < frameThresh) ++k; else k = 0; clear (i); --i; }
        const int iStart = i + 1 + k;
        if (iEnd - iStart <= minLen) continue;
        evs.push_back ({ iStart, iEnd, f + kMidiOffset, mean (iStart, iEnd, f) });
    }
    std::vector<Note> out;
    for (const auto& e : evs)
        out.push_back ({ frameTime (e.s), frameTime (e.e), e.p, std::clamp ((int) std::lround (127.0 * e.amp), 1, 127) });
    std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    return out;
}
} // namespace bp

// Post-processing on basic-pitch output (tuned on the eval set, see test/eval.py):
//  1. drop overtone ghosts: a quieter note a harmonic above a sounding note that is either short,
//     or starts with it and dies far earlier;
//  2. merge a same-pitch note split in two by a spurious re-onset (one half very short);
//  3. when a held note runs into its own re-strike, cut it at the chord onset everyone else used.
static void cleanNotes (std::vector<Note>& v)
{
    std::sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    std::vector<Note> keep;
    for (const auto& n : v)
    {
        const double d = n.end - n.start; bool ghost = false;
        for (const auto& m : v)
        {
            const int iv = n.pitch - m.pitch;
            if (&m == &n || ! (iv == 12 || iv == 19 || iv == 24 || iv == 28 || iv == 31 || iv == 36)) continue;
            const double ov = std::min (n.end, m.end) - std::max (n.start, m.start);
            if (ov > 0.5 * d && ((d < 0.35 && n.velocity < 0.6 * m.velocity)
                                 || (std::abs (n.start - m.start) < 0.05 && d < 0.4 * (m.end - m.start) && n.velocity < 0.8 * m.velocity)))
            { ghost = true; break; }
        }
        if (! ghost) keep.push_back (n);
    }
    std::vector<double> starts; for (auto& n : keep) starts.push_back (n.start);
    std::sort (starts.begin(), starts.end());
    std::sort (keep.begin(), keep.end(), [] (const Note& a, const Note& b) { return a.pitch < b.pitch || (a.pitch == b.pitch && a.start < b.start); });
    std::vector<Note> out;
    for (auto n : keep)
    {
        if (! out.empty() && out.back().pitch == n.pitch && n.start - out.back().end < 0.03)
        {
            auto& a = out.back();
            if (a.end - a.start < 0.25 || n.end - n.start < 0.15) { a.end = n.end; a.velocity = std::max (a.velocity, n.velocity); continue; }
            for (double c : starts)
            {
                if (! (c > a.start + 0.15 && c < a.end && a.end - c < 0.25)) continue;
                int cnt = 0; for (double t : starts) cnt += std::abs (t - c) < 0.05;
                if (cnt >= 3) { a.end = c; n.start = c; break; }
            }
        }
        out.push_back (n);
    }
    std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    v = std::move (out);
}

bool Engine::notes (const std::vector<float>& mono, const Settings& s, float minHz, float maxHz, std::vector<Note>& out, std::string& err)
{
    using namespace bp;
    out.clear();
    const auto y = resample (mono, kSampleRate, kSr);
    const size_t M = y.size();
    if (M < (size_t) kSr / 4) return true;
    std::vector<float> padded ((size_t) kOverlap / 2, 0.0f); padded.insert (padded.end(), y.begin(), y.end());
    const int nWin = (int) ((padded.size() + kStep - 1) / kStep);
    const int nOut = (int) std::floor ((double) M * kFps / kSr);
    std::vector<float> frames ((size_t) nWin * (kFramesPerWin - kOverlapFrames) * kBins), onsets (frames.size());
    constexpr int batch = 8;
    std::vector<float> in;
    std::vector<ort::Tensor> res;
    int written = 0;
    for (int w0 = 0; w0 < nWin; w0 += batch)
    {
        const int nb = std::min (batch, nWin - w0);
        in.assign ((size_t) nb * kWin, 0.0f);
        for (int b = 0; b < nb; ++b)
        {
            const size_t st = (size_t) (w0 + b) * kStep;
            for (int i = 0; i < kWin && st + (size_t) i < padded.size(); ++i) in[(size_t) b * kWin + (size_t) i] = padded[st + (size_t) i];
        }
        if (! pitch.run ("serving_default_input_2:0", in.data(), { nb, kWin, 1 }, { "StatefulPartitionedCall:1", "StatefulPartitionedCall:2" }, res, err)) return false;
        const int keep = kFramesPerWin - kOverlapFrames, lo = kOverlapFrames / 2;
        for (int b = 0; b < nb; ++b)
            for (int t = 0; t < keep; ++t)
            {
                const size_t src = ((size_t) b * kFramesPerWin + (size_t) (t + lo)) * kBins, dst = (size_t) (written + b * keep + t) * kBins;
                std::copy_n (res[0].data.begin() + (long) src, kBins, frames.begin() + (long) dst);
                std::copy_n (res[1].data.begin() + (long) src, kBins, onsets.begin() + (long) dst);
            }
        for (int b = 0; b < nb; ++b) written += keep;
    }
    const int n = std::min (nOut, written);
    frames.resize ((size_t) n * kBins); onsets.resize ((size_t) n * kBins);
    const int minLen = (int) std::lround (s.minNoteMs / 1000.0 * ((double) kSr / kHop));
    const int minIdx = minHz > 0 ? std::max (0, (int) std::lround (hzToMidi (minHz) - kMidiOffset)) : 0;
    const int maxIdx = maxHz > 0 ? std::min (kBins, (int) std::lround (hzToMidi (maxHz) - kMidiOffset)) : kBins;
    out = decode (std::move (frames), std::move (onsets), n, s.onsetThreshold, s.frameThreshold, minLen, minIdx, maxIdx);
    if (minHz > 50.0f) cleanNotes (out);   // bass is cleaned by monophonic(); merging hurts repeated bass notes
    return true;
}

// ---------------------------------------------------------------------------------------------------- part logic
std::vector<Note> monophonic (std::vector<Note> v, bool lowest)
{
    std::sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    std::vector<Note> out;
    for (size_t i = 0; i < v.size();)
    {
        size_t j = i; int vmax = 0;
        while (j < v.size() && v[j].start - v[i].start < 0.04) vmax = std::max (vmax, v[j++].velocity);
        const Note* pick = nullptr;
        for (size_t k = i; k < j; ++k)
            if (v[k].velocity * 2 >= vmax && (pick == nullptr || (lowest ? v[k].pitch < pick->pitch : v[k].pitch > pick->pitch))) pick = &v[k];
        // a note that starts inside a longer, louder held note of the "wrong" side is an artefact (harmonic / bleed)
        if (! out.empty() && pick->start < out.back().end)
        {
            const Note& prev = out.back();
            const bool wrongSide = lowest ? pick->pitch > prev.pitch : pick->pitch < prev.pitch;
            if (wrongSide && pick->end <= prev.end + 0.03 && pick->velocity < prev.velocity) { i = j; continue; }
            out.back().end = pick->start;
            if (out.back().end - out.back().start < 0.04) out.pop_back();
        }
        out.push_back (*pick);
        i = j;
    }
    return out;
}

// Separated "other" stems pump with the drums, so basic-pitch re-onsets held notes on every hit.
// Re-join a same-pitch fragment unless it starts with a real chord onset (3+ notes together).
static void mergeFragments (std::vector<Note>& v, double maxGap)
{
    std::vector<double> starts; for (auto& n : v) starts.push_back (n.start);
    std::sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.pitch < b.pitch || (a.pitch == b.pitch && a.start < b.start); });
    std::vector<Note> out;
    for (const auto& n : v)
    {
        if (! out.empty() && out.back().pitch == n.pitch && n.start - out.back().end < maxGap)
        {
            int cnt = 0; for (double t : starts) cnt += std::abs (t - n.start) < 0.05;
            if (cnt < 3) { out.back().end = std::max (out.back().end, n.end); out.back().velocity = std::max (out.back().velocity, n.velocity); continue; }
        }
        out.push_back (n);
    }
    std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    v = std::move (out);
}

void splitMelodyChords (const std::vector<Note>& all, std::vector<Note>& melody, std::vector<Note>& chords, float chordMinNotes, int melodyGap)
{
    melody.clear(); chords.clear();
    std::vector<Note> v = all;
    std::sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    const size_t n = v.size();
    std::vector<int> grp (n, 0);
    for (size_t i = 0, g = 0; i < n; ++g)
    {
        size_t j = i; while (j < n && v[j].start - v[i].start < 0.05) grp[j++] = (int) g;
        i = j;
    }
    auto dur = [&] (size_t i) { return std::max (1e-3, v[i].end - v[i].start); };
    std::vector<char> isMel (n, 0);
    for (size_t i = 0; i < n; ++i)
    {
        double hi = 0.0; int similar = 1;
        for (size_t k = 0; k < n; ++k)
        {
            if (k == i) continue;
            if (v[k].start > v[i].end) break;
            const double ov = std::min (v[i].end, v[k].end) - std::max (v[i].start, v[k].start);
            if (ov > 0 && v[k].pitch > v[i].pitch) hi += ov;
            if (grp[k] == grp[i] && std::abs (dur (k) - dur (i)) < 0.35 * std::max (dur (k), dur (i))) ++similar;
        }
        // top of a chord hit still counts as melody when it sits clear above the voicing
        int below = -1000;
        for (size_t k = 0; k < n; ++k)
            if (k != i && grp[k] == grp[i] && v[k].pitch < v[i].pitch) below = std::max (below, v[k].pitch);
        const bool clearTop = below > -1000 && v[i].pitch - below >= melodyGap;
        isMel[i] = (hi / dur (i) < 0.25 && ((float) similar < chordMinNotes || clearTop)) ? 1 : 0;
    }
    std::vector<Note> cand;
    for (size_t i = 0; i < n; ++i) (isMel[i] ? cand : chords).push_back (v[i]);
    melody = monophonic (cand, false);
    // notes dropped by the monophonic pass are harmony, not lost
    for (const auto& c : cand)
    {
        bool kept = false;
        for (const auto& m : melody) if (m.pitch == c.pitch && std::abs (m.start - c.start) < 1e-9) { kept = true; break; }
        if (! kept) chords.push_back (c);
    }
    std::sort (chords.begin(), chords.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    // chord hits start together: snap each onset cluster to its earliest note
    for (size_t i = 0; i < chords.size();)
    {
        size_t j = i; while (j < chords.size() && chords[j].start - chords[i].start < 0.05) ++j;
        for (size_t k = i; k < j; ++k) chords[k].start = chords[i].start;
        i = j;
    }
}

void quantizeNotes (std::vector<Note>& v, double bpm, int division)
{
    if (division <= 0 || bpm <= 0) return;
    const double grid = 60.0 / bpm * (division == 1 ? 0.25 : division == 2 ? 0.5 : 1.0);
    for (auto& nt : v)
    {
        const double len = nt.end - nt.start;
        nt.start = std::round (nt.start / grid) * grid;
        nt.end = nt.start + std::max (grid * 0.5, std::round (len / grid) * grid);
    }
}

// ---------------------------------------------------------------------------------------------------- tempo
double detectTempo (const std::vector<float>& x)
{
    constexpr int nfft = 2048, hop = 512;
    if (x.size() < (size_t) Engine::kSampleRate * 4) return 120.0;
    const auto S = powerSpec (x, nfft, hop);
    const size_t T = S.size(), Bn = S[0].size();
    std::vector<double> env (T, 0.0);
    double gmax = 1e-20; for (auto& fr : S) for (float p : fr) gmax = std::max (gmax, (double) p);
    std::vector<double> prev (Bn, 0.0);
    for (size_t t = 0; t < T; ++t)
    {
        double s = 0.0;
        for (size_t b = 1; b < Bn; ++b)
        {
            const double l = std::log10 (1e-6 + S[t][b] / gmax);
            if (t > 0) s += std::max (0.0, l - prev[b]);
            prev[b] = l;
        }
        env[t] = s;
    }
    const double mu = std::accumulate (env.begin(), env.end(), 0.0) / (double) T;
    for (auto& e : env) e -= mu;
    const double fps = Engine::kSampleRate / hop;
    auto ac = [&] (double lag)
    {
        const int l0 = (int) lag; const double fr = lag - l0;
        double a = 0, b = 0;
        for (size_t t = (size_t) l0 + 1; t < T; ++t) { a += env[t] * env[t - (size_t) l0]; b += env[t] * env[t - (size_t) l0 - 1]; }
        return (1.0 - fr) * a + fr * b;
    };
    auto score = [&] (double bpm)
    {
        const double lag = fps * 60.0 / bpm; double s = 0;
        for (int k = 1; k <= 4; ++k) if (lag * k < (double) T / 2) s += ac (lag * k) / k;
        const double oct = std::log2 (bpm / 120.0);
        return s * std::exp (-0.5 * oct * oct / (0.9 * 0.9));
    };
    double best = 120.0, bs = -1e300;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.5) { const double sc = score (bpm); if (sc > bs) { bs = sc; best = bpm; } }
    const double c = best;
    for (double bpm = c - 1.0; bpm <= c + 1.0; bpm += 0.02) { const double sc = score (bpm); if (sc > bs) { bs = sc; best = bpm; } }
    if (std::abs (best - std::round (best)) < 0.3) best = std::round (best);
    return best;
}

// ---------------------------------------------------------------------------------------------------- drums
std::array<std::vector<Note>, 4> detectDrums (const std::vector<float>& xIn, float sens)
{
    // Band-wise log spectral flux on a 1024 / 128 STFT (5.8 ms hop): a per-bin rise copes with hits that land while an earlier hit
    // still rings, where a plain band-level rise does not. Tuned on test/make_songs.py drum stems and Spleeter drum stems.
    std::array<std::vector<Note>, 4> out;
    constexpr int nfft = 1024, hop = 128;
    const size_t pad = (size_t) (0.1 * Engine::kSampleRate);
    std::vector<float> x (pad, 0.0f); x.insert (x.end(), xIn.begin(), xIn.end());
    if (xIn.size() < (size_t) nfft) return out;
    const auto S = powerSpec (x, nfft, hop);
    const size_t T = S.size();
    const double fps = Engine::kSampleRate / hop, binHz = Engine::kSampleRate / nfft;
    enum { LOW, BODY, NOISE, HIGH, NB };
    const double edges[NB][2] = { { 30, 120 }, { 150, 400 }, { 1000, 5000 }, { 7000, 16000 } };
    std::vector<std::vector<double>> E (NB, std::vector<double> (T, 0.0)), L = E, F = E;
    for (int b = 0; b < NB; ++b)
    {
        const int b0 = (int) std::ceil (edges[b][0] / binHz), b1 = (int) std::floor (edges[b][1] / binHz), nb = b1 - b0 + 1;
        for (size_t t = 0; t < T; ++t) { double s = 0; for (int k = b0; k <= b1; ++k) s += S[t][(size_t) k]; E[(size_t) b][t] = s; }
        std::vector<double> sorted = E[(size_t) b]; std::sort (sorted.begin(), sorted.end());
        const double ref = std::max (1e-20, sorted[(size_t) (0.99 * (double) (T - 1))]), perBin = ref / nb;
        auto db = [&] (size_t t, int k) { return std::max (-60.0, 10.0 * std::log10 (S[t][(size_t) k] / perBin + 1e-6)); };
        for (size_t t = 0; t < T; ++t)
        {
            L[(size_t) b][t] = 10.0 * std::log10 (E[(size_t) b][t] / ref + 1e-6);
            if (t < 2) continue;
            double fl = 0; for (int k = b0; k <= b1; ++k) fl += std::max (0.0, db (t, k) - db (t - 2, k));
            F[(size_t) b][t] = fl / nb;
        }
    }
    const double thr = 6.0 * (1.5 - sens), lvl = 10.0 * (sens - 0.5);   // more sensitivity: lower flux and level floors
    auto peaks = [&] (int b)
    {
        std::vector<size_t> p; const auto& r = F[(size_t) b];
        for (size_t t = 3; t + 3 < T; ++t)
        {
            if (r[t] < thr) continue;
            bool mx = true; for (size_t k = t - 3; k <= t + 3; ++k) if (r[k] > r[t] || (r[k] == r[t] && k < t)) { mx = false; break; }
            if (! mx) continue;
            // Spleeter smears a little energy ~50 ms ahead of a hit; keep the stronger of two close peaks
            if (! p.empty() && (double) (t - p.back()) < 0.06 * fps) { if (r[t] > r[p.back()]) p.back() = t; continue; }
            p.push_back (t);
        }
        return p;
    };
    auto level = [&] (int b, size_t t) { double m = -120; for (size_t k = t; k < std::min (T, t + 12); ++k) m = std::max (m, L[(size_t) b][k]); return m; };   // 70 ms: the level peaks after the flux
    auto vel = [&] (int b, size_t t) { return std::clamp ((int) std::lround (127.0 * std::pow (10.0, level (b, t) / 30.0)), 30, 127); };
    auto timeOf = [&] (size_t t) { return std::max (0.0, ((double) t - 1.0) / fps - (double) pad / Engine::kSampleRate); };
    for (auto t : peaks (LOW))
        if (level (LOW, t) > -12.0 - lvl) out[0].push_back ({ timeOf (t), timeOf (t) + 0.1, 36, vel (LOW, t) });
    for (auto t : peaks (NOISE))
    {
        double body = 0; for (size_t k = t - 2; k <= t + 2; ++k) body = std::max (body, F[BODY][k]);
        if (body >= thr * 0.5 && level (NOISE, t) > -10.0 - lvl) out[1].push_back ({ timeOf (t), timeOf (t) + 0.1, 38, vel (NOISE, t) });
    }
    for (auto t : peaks (HIGH))
    {
        if (level (HIGH, t) < -35.0 - lvl) continue;
        size_t pk = t; for (size_t k = t; k < std::min (T, t + 8); ++k) if (E[HIGH][k] > E[HIGH][pk]) pk = k;
        // ring-out: lowest energy 60..200 ms after the peak (the minimum ignores the next hit), above what was there before
        const double before = E[HIGH][t >= 3 ? t - 3 : 0];
        double lm = 1e300; for (size_t k = pk + (size_t) (0.06 * fps); k <= std::min (T - 1, pk + (size_t) (0.2 * fps)); ++k) lm = std::min (lm, E[HIGH][k]);
        if (lm == 1e300) lm = 0;
        const bool cym = (lm - before) / std::max (1e-20, E[HIGH][pk] - before) > 0.2;
        out[cym ? 3 : 2].push_back ({ timeOf (t), timeOf (t) + (cym ? 0.5 : 0.05), cym ? 49 : 42, vel (HIGH, t) });
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------- full pipelines
namespace
{
double rms (const std::vector<float>& x) { double s = 0; for (float v : x) s += (double) v * v; return x.empty() ? 0.0 : std::sqrt (s / (double) x.size()); }
std::vector<float> mono (const Audio& a)
{
    std::vector<float> m = a.left;
    if (! a.right.empty()) for (size_t i = 0; i < m.size() && i < a.right.size(); ++i) m[i] = 0.5f * (m[i] + a.right[i]);
    return m;
}
constexpr float kBassLo = 27.0f, kBassHi = 420.0f, kTonalLo = 60.0f, kTonalHi = 4200.0f;
} // namespace

static void finish (Result& r, const Settings& s, const std::vector<float>& tempoSource)
{
    r.bpm = s.bpm > 0 ? s.bpm : detectTempo (tempoSource);
    if (s.quantize > 0) for (auto& p : r.parts) quantizeNotes (p, r.bpm, s.quantize);
    r.ok = true;
}

Result Engine::transcribeSong (const Audio& song, const Settings& s, const std::atomic<bool>* cancel, Progress prog)
{
    Result r; r.seconds = (double) song.size() / kSampleRate;
    if (! loaded()) { r.error = "models not loaded"; return r; }
    if (song.size() < (size_t) kSampleRate) { r.error = "audio is shorter than a second"; return r; }
    std::array<std::vector<float>, 4> st;   // vocals, drums, bass, other
    auto sub = [&] (float a, float b) { return [prog, a, b] (float p, const char* m) { if (prog) prog (a + (b - a) * p, m); }; };
    if (! separate (song, st, r.error, cancel, sub (0.0f, 0.55f))) return r;
    if (cancelled (cancel)) { r.error = "cancelled"; return r; }
    if (prog) prog (0.6f, "Bass notes");
    std::vector<Note> bass, voc, oth;
    if (! notes (st[2], s, kBassLo, kBassHi, bass, r.error)) return r;
    if (cancelled (cancel)) { r.error = "cancelled"; return r; }
    if (prog) prog (0.7f, "Melody / chord notes");
    if (! notes (st[3], s, kTonalLo, kTonalHi, oth, r.error)) return r;
    mergeFragments (oth, 0.04);
    const double vr = rms (st[0]), orr = rms (st[3]);
    const bool useVocals = s.melodySource == 1 || (s.melodySource == 0 && vr > 0.35 * orr && vr > 1e-3);
    if (useVocals)
    {
        if (prog) prog (0.8f, "Vocal melody");
        if (! notes (st[0], s, kTonalLo, kTonalHi, voc, r.error)) return r;
        if (voc.size() < 4 && s.melodySource == 0) voc.clear();
    }
    if (cancelled (cancel)) { r.error = "cancelled"; return r; }
    r.parts[kBass] = monophonic (bass, true);
    if (useVocals && ! voc.empty())
    {
        r.melodyFrom = "vocals";
        r.parts[kMelody] = monophonic (voc, false);
        std::vector<Note> m; splitMelodyChords (oth, m, r.parts[kChords], s.chordMinNotes, s.melodyGap);
        // with a sung melody, everything else from the instruments is harmony
        for (auto& x : m) r.parts[kChords].push_back (x);
        std::sort (r.parts[kChords].begin(), r.parts[kChords].end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    }
    else
    {
        r.melodyFrom = "instruments";
        splitMelodyChords (oth, r.parts[kMelody], r.parts[kChords], s.chordMinNotes, s.melodyGap);
    }
    if (prog) prog (0.9f, "Drums");
    auto d = detectDrums (st[1], s.drumSensitivity);
    for (int i = 0; i < 4; ++i) r.parts[(size_t) (kKick + i)] = std::move (d[(size_t) i]);
    finish (r, s, st[1]);
    if (prog) prog (1.0f, "Done");
    return r;
}

Result Engine::transcribeStems (const std::array<Audio, kNumStemSlots>& stems, const Settings& s, const std::atomic<bool>* cancel, Progress prog)
{
    Result r;
    for (auto& a : stems) r.seconds = std::max (r.seconds, (double) a.size() / kSampleRate);
    if (! loaded()) { r.error = "models not loaded"; return r; }
    std::vector<float> tempoSrc;
    r.melodyFrom = "stem";
    for (int slot = 0; slot < kNumStemSlots; ++slot)
    {
        if (cancelled (cancel)) { r.error = "cancelled"; return r; }
        if (stems[(size_t) slot].empty()) continue;
        if (prog) prog ((float) slot / kNumStemSlots, slot == kStemDrums ? "Drums" : "Notes");
        const auto m = mono (stems[(size_t) slot]);
        std::vector<Note> v;
        switch (slot)
        {
            case kStemBass:   if (! notes (m, s, kBassLo, kBassHi, v, r.error)) return r; r.parts[kBass] = monophonic (v, true); break;
            case kStemMelody: if (! notes (m, s, kTonalLo, kTonalHi, v, r.error)) return r; r.parts[kMelody] = monophonic (v, false); break;
            case kStemChords: if (! notes (m, s, kTonalLo, kTonalHi, v, r.error)) return r;
                              { std::vector<Note> mm; splitMelodyChords (v, mm, r.parts[kChords], 1.0f); }   // keeps every note, aligns chord onsets
                              break;
            case kStemDrums:
            {
                auto d = detectDrums (m, s.drumSensitivity);
                for (int i = 0; i < 4; ++i) r.parts[(size_t) (kKick + i)] = std::move (d[(size_t) i]);
                tempoSrc = m; break;
            }
            default: break;
        }
        if (tempoSrc.empty() && slot == kNumStemSlots - 1) tempoSrc = m;
    }
    if (tempoSrc.empty()) for (auto& a : stems) if (! a.empty()) { tempoSrc = mono (a); break; }
    finish (r, s, tempoSrc);
    if (prog) prog (1.0f, "Done");
    return r;
}
} // namespace rw
