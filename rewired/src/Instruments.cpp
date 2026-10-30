#include "Instruments.h"
#include "RewiredEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <map>

namespace rw
{
namespace
{
constexpr double kPiI = 3.14159265358979323846;
constexpr double kHop = 0.48;
// AudioSet (YAMNet class_map) indices pooled per group (max)
const std::vector<int>& members (int g)
{
    static const std::vector<int> m[kNumInst] = {
        { 180, 181, 182, 183, 192 },                                  // horns: brass, french horn, trumpet, trombone, saxophone
        { 190, 191, 193, 203, 204 },                                  // winds: woodwind, flute, clarinet, harmonica, accordion
        { 184, 185, 186, 187, 188 },                                  // strings: bowed, section, violin, pizzicato, cello
        { 147, 148, 149, 155 },                                       // keys: keyboard, piano, e-piano, harpsichord
        { 150, 151, 152 },                                            // organ
        { 134, 135, 136, 138, 139, 141, 142, 143, 144, 145, 146, 194 },  // guitar + plucked (banjo, mandolin, ukulele, harp ...)
        { 174, 175, 176, 177, 178, 195, 200 },                        // mallets / bells
        { 153, 154 },                                                 // synthesizer, sampler
        { 24, 25, 26, 27, 29, 30, 31, 32, 249 },                      // singing, choir, ..., rapping, humming, vocal music
        { 137 },                                                      // bass guitar
        { 189 },                                                      // double bass
    };
    return m[g];
}
// lead (melody) vs comping (chords) prior, used only when melody and chords come out of the same audio
constexpr float kLead[kNumInst]  = { 1.25f, 1.25f, 0.9f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f, 1.3f, 1.0f, 1.0f };
constexpr float kComp[kNumInst]  = { 0.9f, 0.8f, 1.15f, 1.25f, 1.2f, 1.1f, 1.0f, 1.0f, 0.8f, 1.0f, 1.0f };
constexpr float kMinScore = 0.04f;   // below this nothing in the group list is really there

void fft512 (std::complex<float>* x)
{
    constexpr int n = 512;
    static std::vector<std::complex<float>> tw = [] { std::vector<std::complex<float>> t (n / 2); for (int i = 0; i < n / 2; ++i) t[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPiI * i / n)); return t; }();
    for (int i = 1, j = 0; i < n; ++i) { int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) std::swap (x[i], x[j]); }
    for (int len = 2; len <= n; len <<= 1)
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; ++k)
            {
                const auto w = tw[(size_t) (k * (n / len))];
                const auto u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v; x[i + k + len / 2] = u - v;
            }
}
} // namespace

bool instAllowed (int g, int part)
{
    if (isDrumPart (part)) return false;
    if (part == kBass) return g == kInstBassGuitar || g == kInstUpright || g == kInstSynth;
    return g != kInstBassGuitar && g != kInstUpright;
}

const char* instName (int g, int part, double med)
{
    switch (g)
    {
        case kInstHorns: return "Horns";
        case kInstWinds: return "Winds";
        case kInstStrings: return "Strings";
        case kInstKeys: return "Piano / Keys";
        case kInstOrgan: return "Organ";
        case kInstGuitar: return part == kBass ? "Bass Guitar" : "Guitar";
        case kInstMallets: return "Mallets / Bells";
        case kInstSynth: return part == kBass ? "Synth Bass" : part == kMelody ? "Synth Lead" : (med >= 0.6 ? "Synth Pad" : "Synth");
        case kInstVocals: return "Vocals";
        case kInstBassGuitar: return "Bass Guitar";
        case kInstUpright: return "Upright Bass";
        default: return part == kBass ? "Bass" : "Instrument";
    }
}

int instGmProgram (int g, int part)
{
    switch (g)
    {
        case kInstHorns: return 61; case kInstWinds: return 73; case kInstStrings: return 48; case kInstKeys: return 0; case kInstOrgan: return 16;
        case kInstGuitar: return 25; case kInstMallets: return 11; case kInstSynth: return part == kBass ? 38 : part == kMelody ? 80 : 89;
        case kInstVocals: return 52; case kInstBassGuitar: return 33; case kInstUpright: return 32;
        default: return part == kBass ? 33 : part == kMelody ? 80 : 89;
    }
}

int instFromFileName (const std::string& fileName)
{
    std::string n; for (char c : fileName) n += (char) std::tolower ((unsigned char) c);
    auto has = [&] (std::initializer_list<const char*> ws) { for (auto* w : ws) if (n.find (w) != std::string::npos) return true; return false; };
    if (has ({ "horn", "brass", "trumpet", "trombone", "tuba", "sax" })) return kInstHorns;
    if (has ({ "flute", "clarinet", "oboe", "bassoon", "woodwind", "harmonica", "accordion" })) return kInstWinds;
    if (has ({ "string", "violin", "viola", "cello", "fiddle" })) return kInstStrings;
    if (has ({ "organ" })) return kInstOrgan;
    if (has ({ "piano", "keys", "rhodes", "wurli", "epiano", "harpsi" })) return kInstKeys;
    if (has ({ "upright", "contrabass", "double bass", "doublebass" })) return kInstUpright;
    if (has ({ "gtr", "guitar", "banjo", "mandolin", "ukulele", "harp" })) return kInstGuitar;
    if (has ({ "bell", "mallet", "marimba", "xylo", "vibra", "glock" })) return kInstMallets;
    if (has ({ "synth", "pad", "pluck", "arp" })) return kInstSynth;
    if (has ({ "vocal", "vox", "choir", "voice", "sing" })) return kInstVocals;
    return -1;
}

bool InstrumentTagger::load (ort::Runtime& rt, const std::string& path, int threads, std::string& err) { return sess.load (rt, path, threads, err); }

bool InstrumentTagger::tagRaw (const std::vector<float>& x16, std::vector<std::vector<float>>& out, std::string& err,
                               std::vector<std::vector<float>>* groups)
{
    out.clear();
    if (! loaded()) { err = "instrument model not loaded"; return false; }
    constexpr int W = 400, H = 160, B = 257, PF = 96, PH = 48;
    const size_t need = (size_t) (16000 * (0.96 + 0.015)), hopS = (size_t) (0.48 * 16000);
    size_t n = std::max (x16.size(), need);
    n = need + (size_t) std::ceil ((double) (n - need) / (double) hopS) * hopS;   // same padding as yamnet.pad_waveform
    std::vector<float> w (n, 0.0f); std::copy (x16.begin(), x16.end(), w.begin());
    const int frames = 1 + (int) ((n - W) / H);
    std::vector<float> mag ((size_t) frames * B);
    std::vector<float> win (W); for (int i = 0; i < W; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPiI * i / W));
    std::vector<std::complex<float>> buf (512);
    for (int f = 0; f < frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), std::complex<float>());
        for (int i = 0; i < W; ++i) buf[(size_t) i] = w[(size_t) f * H + (size_t) i] * win[(size_t) i];
        fft512 (buf.data());
        for (int b = 0; b < B; ++b) mag[(size_t) f * B + (size_t) b] = std::abs (buf[(size_t) b]);
    }
    const int patches = (frames - PF) / PH + 1;
    constexpr int kChunk = 64;
    std::vector<float> in;
    std::vector<ort::Tensor> res;
    for (int p0 = 0; p0 < patches; p0 += kChunk)
    {
        const int np = std::min (kChunk, patches - p0);
        in.resize ((size_t) np * PF * B);
        for (int p = 0; p < np; ++p)
            std::copy_n (mag.begin() + (ptrdiff_t) ((size_t) (p0 + p) * PH * B), PF * B, in.begin() + (ptrdiff_t) ((size_t) p * PF * B));
        if (! sess.run ("mag", in.data(), { np, PF, B }, groups ? std::vector<const char*> { "scores", "groups" } : std::vector<const char*> { "scores" }, res, err)) return false;
        for (size_t o = 0; o < res.size(); ++o)
        {
            const auto& s = res[o].data;
            const size_t C = s.size() / (size_t) np;
            auto& dst = o == 0 ? out : *groups;
            for (int p = 0; p < np; ++p) dst.emplace_back (s.begin() + (ptrdiff_t) (p * C), s.begin() + (ptrdiff_t) ((p + 1) * C));
        }
    }
    return true;
}

bool InstrumentTagger::tag (const std::vector<float>& mono44k, std::vector<InstScores>& out, std::string& err, bool rawPool)
{
    out.clear();
    std::vector<std::vector<float>> raw, head;
    if (! tagRaw (resample (mono44k, 44100.0, 16000.0), raw, err, &head)) return false;
    // the shipped model carries a small trained head ("groups", softmax over the 11 groups) that beats max-pooling the raw
    // AudioSet classes on held-out audio (78 % vs 59 % clip top-1); max-pooling stays as the fallback for a bare YAMNet export,
    // and is used for the bass part, where it measured better (73 % vs 38 % of bass notes on the stems test: the head over-calls upright)
    // silent hops score nothing (YAMNet says "Silence"; we only care about who is playing)
    std::vector<InstScores> pooled (raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
        for (int g = 0; g < kNumInst; ++g)
        {
            float m = 0.0f;
            if (! rawPool && i < head.size() && head[i].size() == (size_t) kNumInst) m = head[i][(size_t) g];
            else for (int c : members (g)) if ((size_t) c < raw[i].size()) m = std::max (m, raw[i][(size_t) c]);
            pooled[i][(size_t) g] = m;
        }
    // 3-hop smoothing (1.44 s) so one odd patch doesn't flip the label mid-phrase
    out.resize (pooled.size());
    for (size_t i = 0; i < pooled.size(); ++i)
        for (int g = 0; g < kNumInst; ++g)
        {
            float s = 0, wsum = 0;
            for (int d = -1; d <= 1; ++d)
            {
                const long j = (long) i + d; if (j < 0 || j >= (long) pooled.size()) continue;
                const float wt = d == 0 ? 2.0f : 1.0f; s += wt * pooled[(size_t) j][(size_t) g]; wsum += wt;
            }
            out[i][(size_t) g] = s / wsum;
        }
    return true;
}

std::vector<SubPart> labelNotes (const std::vector<Note>& notes, const std::vector<InstScores>& sc, int part, int forced, bool shared)
{
    std::vector<SubPart> subs;
    if (notes.empty() || isDrumPart (part)) return subs;
    std::vector<int> lab (notes.size(), -1);
    std::vector<float> conf (notes.size(), 0.0f);
    if (forced >= 0 && instAllowed (forced, part))
        std::fill (lab.begin(), lab.end(), forced);
    else if (! sc.empty())
    {
        for (size_t k = 0; k < notes.size(); ++k)
        {
            const auto& n = notes[k];
            InstScores acc {}; int cnt = 0;
            for (size_t i = 0; i < sc.size(); ++i)
            {
                const double c = kHop * (double) (i + 1);
                if (c >= n.start - kHop * 0.5 && c <= n.end + kHop * 0.5) { for (int g = 0; g < kNumInst; ++g) acc[(size_t) g] += sc[i][(size_t) g]; ++cnt; }
            }
            if (cnt == 0)
            {
                const size_t i = (size_t) std::clamp ((long) std::lround ((n.start + n.end) * 0.5 / kHop) - 1, 0L, (long) sc.size() - 1);
                acc = sc[i]; cnt = 1;
            }
            int best = -1; float bv = 0.0f, braw = 0.0f;
            for (int g = 0; g < kNumInst; ++g)
            {
                if (! instAllowed (g, part)) continue;
                const float raw = acc[(size_t) g] / (float) cnt;
                if (raw < kMinScore) continue;
                const float v = raw * (shared ? (part == kMelody ? kLead[g] : kComp[g]) : 1.0f);
                if (v > bv) { bv = v; best = g; braw = raw; }
            }
            lab[k] = best; conf[k] = braw;
        }
        // unlabelled notes take the label of the nearest labelled note in time
        for (size_t k = 0; k < notes.size(); ++k)
            if (lab[k] < 0)
            {
                double bd = 1e9; int bl = -1;
                for (size_t j = 0; j < notes.size(); ++j) if (lab[j] >= 0 && conf[j] > 0) { const double d = std::abs (notes[j].start - notes[k].start); if (d < bd) { bd = d; bl = lab[j]; } }
                if (bl >= 0 && bd < 4.0) lab[k] = bl;
            }
    }
    // fold tiny groups into the biggest one
    std::map<int, int> count; for (int l : lab) ++count[l];
    int big = lab[0], bc = 0; for (auto& [l, c] : count) if (l >= 0 && c > bc) { bc = c; big = l; }
    if (bc == 0) big = -1;
    for (auto& l : lab) { const int c = count[l]; if (l != big && (l < 0 || (c < 4 && c * 100 < (int) notes.size() * 8))) l = big; }
    std::map<int, size_t> idx;
    std::vector<float> csum;
    for (size_t k = 0; k < notes.size(); ++k)
    {
        auto it = idx.find (lab[k]);
        if (it == idx.end()) { it = idx.emplace (lab[k], subs.size()).first; subs.push_back ({}); subs.back().inst = lab[k]; csum.push_back (0.0f); }
        subs[it->second].notes.push_back (notes[k]); csum[it->second] += conf[k];
    }
    for (size_t i = 0; i < subs.size(); ++i)
    {
        auto& s = subs[i];
        std::vector<double> d; for (auto& n : s.notes) d.push_back (n.end - n.start);
        std::nth_element (d.begin(), d.begin() + (ptrdiff_t) (d.size() / 2), d.end());
        s.name = instName (s.inst, part, d[d.size() / 2]);
        s.fromName = forced >= 0;
        s.confidence = forced >= 0 ? 1.0f : csum[i] / (float) s.notes.size();
    }
    std::sort (subs.begin(), subs.end(), [] (const SubPart& a, const SubPart& b) { return a.notes.size() > b.notes.size(); });
    // keep at most 4 per part: the rest join the biggest
    while (subs.size() > 4) { auto& last = subs.back(); subs[0].notes.insert (subs[0].notes.end(), last.notes.begin(), last.notes.end()); subs.pop_back(); }
    for (auto& s : subs) std::sort (s.notes.begin(), s.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return subs;
}
} // namespace rw
