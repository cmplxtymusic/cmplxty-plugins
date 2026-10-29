// Offline renderer for the ALTER section: alter_render in.wav out.wav [pitch=x formant=x gender=x link=0/1 mode=0..2 key=n scale=n amix=x]
// in.wav: 16-bit PCM mono 48 kHz. Writes latency-compensated 16-bit mono 48 kHz.
#include "../Source/Engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace qz;
static bool readWav (const char* fn, std::vector<float>& x)
{
    FILE* f = std::fopen (fn, "rb"); if (!f) return false;
    std::vector<unsigned char> b; unsigned char tmp[65536]; size_t n;
    while ((n = std::fread (tmp, 1, sizeof tmp, f)) > 0) b.insert (b.end(), tmp, tmp + n);
    std::fclose (f);
    size_t p = 12; 
    while (p + 8 <= b.size())
    {
        unsigned sz = b[p+4] | (b[p+5] << 8) | (b[p+6] << 16) | ((unsigned) b[p+7] << 24);
        if (!std::memcmp (&b[p], "data", 4))
        {
            const size_t cnt = std::min<size_t> (sz, b.size() - p - 8) / 2;
            x.resize (cnt);
            for (size_t i = 0; i < cnt; ++i) x[i] = (float) (short) (b[p+8+2*i] | (b[p+9+2*i] << 8)) / 32768.0f;
            return true;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}
static void writeWav (const char* fn, const std::vector<float>& x)
{
    FILE* f = std::fopen (fn, "wb"); const unsigned n = (unsigned) x.size() * 2, sr = 48000;
    auto w32 = [&] (unsigned v) { std::fwrite (&v, 4, 1, f); }; auto w16 = [&] (unsigned short v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + n); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (1); w32 (sr); w32 (sr * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (n);
    for (float v : x) { const short s = (short) std::lround (clampf (v, -1.0f, 1.0f) * 32767.0f); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}
int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage\n"); return 2; }
    std::vector<float> in; if (!readWav (argv[1], in)) { std::fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }
    ParamValues pv; pv.v[pAon] = 1; pv.v[pQon] = 0; pv.v[pAMix] = 100;
    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i]; const auto eq = a.find ('='); if (eq == std::string::npos) continue;
        const std::string k = a.substr (0, eq); const float v = (float) std::atof (a.c_str() + eq + 1);
        if (k == "pitch") pv.v[pPitch] = v; else if (k == "formant") pv.v[pFormant] = v; else if (k == "gender") pv.v[pGender] = v;
        else if (k == "link") pv.v[pLink] = v; else if (k == "mode") pv.v[pMode] = v; else if (k == "key") pv.v[pKey] = v;
        else if (k == "scale") pv.v[pScale] = v; else if (k == "amix") pv.v[pAMix] = v; else if (k == "speed") pv.v[pSpeed] = v;
    }
    Engine e; e.prepare (48000); const int D = e.latency();
    const size_t N = in.size() + (size_t) D + 4800;
    std::vector<float> L (N, 0.0f), R (N, 0.0f);
    for (size_t i = 0; i < in.size(); ++i) L[i] = R[i] = in[i];
    for (size_t i = 0; i < N; i += 480) e.process (&L[i], &R[i], (int) std::min<size_t> (480, N - i), pv);
    std::vector<float> out (in.size()); for (size_t i = 0; i < in.size(); ++i) out[i] = L[i + (size_t) D];
    writeWav (argv[2], out);
    return 0;
}
