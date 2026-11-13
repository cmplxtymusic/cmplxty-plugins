#pragma once
#include "Parameters.h"
#include <initializer_list>
#include <utility>

namespace glaze
{
inline constexpr int presetCount = 16;
inline constexpr const char *presetNames[presetCount] = {
    "Init",
    "Bass House Loud",
    "Bass House Clean",
    "Midtempo Weight",
    "Midtempo Open",
    "Cyberpunk Edge",
    "Cyberpunk Wide",
    "Heavy Drop",
    "Gentle Glue",
    "Warm Tape",
    "Tight Low End",
    "Air and Sparkle",
    "Streaming Safe",
    "Loud and Punchy",
    "Mono Bass, Wide Top",
    "Final Check (Limiter Only)",
};

using Overrides = std::initializer_list<std::pair<int, float>>;

inline Settings makePreset(int index)
{
    Settings s;
    auto apply = [&s](Overrides o) {
        for (const auto &p : o)
            s[p.first] = p.second;
    };
    switch (clampi(index, 0, presetCount - 1))
    {
    case 0: // Init: transparent, limiter at -1 dB only
        break;
    case 1: // Bass House Loud
        apply({ { Hpf, 25 }, { LowF, 60 }, { LowG, 1.5f }, { LmF, 300 }, { LmG, -1.5f }, { HmF, 3500 }, { HmG, 1.0f },
                { HighF, 11000 }, { HighG, 1.5f }, { GThr, -16 }, { GRatio, 2 }, { GAtk, 10 }, { GRel, 120 },
                { GMakeup, 1 }, { Mb1, 0.35f }, { Mb2, 0.25f }, { Mb3, 0.2f }, { Drive, 5 }, { SatType, 0 },
                { SatMix, 0.35f }, { Width, 1.1f }, { BassMono, 110 }, { Push, 9 }, { Ceiling, -0.8f }, { LimRel, 120 } });
        break;
    case 2: // Bass House Clean
        apply({ { Hpf, 22 }, { LowF, 70 }, { LowG, 1.0f }, { LmF, 280 }, { LmG, -1.0f }, { HighG, 1.0f },
                { GThr, -14 }, { GRatio, 1.8f }, { GAtk, 30 }, { GRel, 200 }, { Mb1, 0.25f }, { Mb2, 0.15f },
                { Drive, 3 }, { SatMix, 0.25f }, { Width, 1.05f }, { BassMono, 100 }, { Push, 3 }, { Ceiling, -1.0f } });
        break;
    case 3: // Midtempo Weight
        apply({ { Hpf, 24 }, { LowF, 70 }, { LowG, 2.0f }, { LmF, 250 }, { LmG, -2.0f }, { HmF, 2500 }, { HmG, 0.8f },
                { HighG, 1.0f }, { GThr, -18 }, { GRatio, 2.5f }, { GAtk, 30 }, { GRel, 300 }, { GMakeup, 1.5f },
                { Mb1, 0.5f }, { Mb2, 0.3f }, { Mb3, 0.2f }, { Drive, 6 }, { SatType, 1 }, { SatMix, 0.4f },
                { Width, 1.15f }, { BassMono, 120 }, { Push, 6 }, { Ceiling, -1.0f }, { LimRel, 200 } });
        break;
    case 4: // Midtempo Open
        apply({ { Hpf, 22 }, { LowF, 80 }, { LowG, 1.0f }, { LmF, 400 }, { LmG, -1.5f }, { HmF, 4000 }, { HmG, 1.0f },
                { HighF, 12000 }, { HighG, 2.0f }, { GThr, -15 }, { GRatio, 2 }, { GAtk, 30 }, { GRel, 250 },
                { Mb3, 0.25f }, { Drive, 3 }, { SatType, 1 }, { SatMix, 0.3f }, { Width, 1.25f }, { BassMono, 110 },
                { Push, 3 }, { Ceiling, -1.0f } });
        break;
    case 5: // Cyberpunk Edge
        apply({ { Hpf, 28 }, { LowF, 60 }, { LowG, 1.0f }, { LmF, 350 }, { LmG, -2.0f }, { HmF, 2800 }, { HmG, 2.0f },
                { HighF, 9000 }, { HighG, 2.5f }, { GThr, -17 }, { GRatio, 3 }, { GAtk, 3 }, { GRel, 100 },
                { GMakeup, 1 }, { Mb1, 0.3f }, { Mb2, 0.4f }, { Mb3, 0.45f }, { Drive, 8 }, { SatType, 2 },
                { SatMix, 0.3f }, { Tone, 0.25f }, { Width, 1.2f }, { BassMono, 100 }, { Push, 7 }, { Ceiling, -0.8f },
                { LimRel, 90 } });
        break;
    case 6: // Cyberpunk Wide
        apply({ { Hpf, 26 }, { LowG, 0.5f }, { LmF, 400 }, { LmG, -1.5f }, { HmF, 5000 }, { HmG, 1.5f },
                { HighF, 10000 }, { HighG, 2.0f }, { GThr, -15 }, { GRatio, 2 }, { GAtk, 10 }, { GRel, 150 },
                { Mb2, 0.2f }, { Mb3, 0.3f }, { Drive, 5 }, { SatType, 1 }, { SatMix, 0.3f }, { Tone, 0.15f },
                { Width, 1.45f }, { BassMono, 130 }, { Push, 4 }, { Ceiling, -1.0f } });
        break;
    case 7: // Heavy Drop
        apply({ { Hpf, 28 }, { LowF, 55 }, { LowG, 2.0f }, { LmF, 220 }, { LmG, -2.0f }, { HmF, 2200 }, { HmG, 1.0f },
                { GThr, -20 }, { GRatio, 3 }, { GAtk, 10 }, { GRel, 150 }, { GMakeup, 2 }, { Mb1, 0.6f },
                { Mb2, 0.4f }, { Mb3, 0.25f }, { Drive, 8 }, { SatType, 0 }, { SatMix, 0.45f }, { Width, 1.05f },
                { BassMono, 140 }, { Push, 11 }, { Ceiling, -0.6f }, { LimRel, 100 } });
        break;
    case 8: // Gentle Glue
        apply({ { GThr, -12 }, { GRatio, 1.6f }, { GAtk, 30 }, { GRel, 400 }, { Push, 1 }, { Ceiling, -1.0f } });
        break;
    case 9: // Warm Tape
        apply({ { LowG, 1.0f }, { HighG, -0.5f }, { GThr, -10 }, { GRatio, 1.5f }, { Drive, 8 }, { SatType, 0 },
                { SatMix, 0.6f }, { Tone, -0.2f }, { Push, 2 }, { Ceiling, -1.0f } });
        break;
    case 10: // Tight Low End
        apply({ { Hpf, 30 }, { LowF, 50 }, { LowG, 0.5f }, { LmF, 200 }, { LmG, -2.0f }, { Mb1, 0.7f },
                { Mb2, 0.2f }, { Ml1, -0.5f }, { BassMono, 150 }, { Push, 2 }, { Ceiling, -1.0f } });
        break;
    case 11: // Air and Sparkle
        apply({ { HmF, 6000 }, { HmG, 1.0f }, { HighF, 12000 }, { HighG, 3.0f }, { Mb3, 0.3f }, { Ml3, 0.5f },
                { Drive, 4 }, { SatType, 1 }, { SatMix, 0.25f }, { Tone, 0.3f }, { Width, 1.2f }, { Push, 2 },
                { Ceiling, -1.0f } });
        break;
    case 12: // Streaming Safe (about -14 LUFS short term on dense bass music)
        apply({ { Hpf, 22 }, { GThr, -10 }, { GRatio, 1.6f }, { GAtk, 30 }, { GRel, 300 }, { BassMono, 100 },
                { Push, 4 }, { Ceiling, -1.5f }, { LimRel, 250 } });
        break;
    case 13: // Loud and Punchy
        apply({ { Hpf, 25 }, { LowF, 65 }, { LowG, 1.0f }, { LmF, 320 }, { LmG, -1.0f }, { HmG, 1.0f },
                { GThr, -18 }, { GRatio, 4 }, { GAtk, 30 }, { GRel, 100 }, { GMakeup, 1 }, { Mb1, 0.4f },
                { Mb2, 0.3f }, { Mb3, 0.2f }, { Drive, 6 }, { SatMix, 0.4f }, { BassMono, 110 }, { Push, 10 },
                { Ceiling, -0.8f }, { LimRel, 80 } });
        break;
    case 14: // Mono Bass, Wide Top
        apply({ { Hpf, 25 }, { HighF, 11000 }, { HighG, 1.5f }, { Width, 1.5f }, { BassMono, 180 }, { Push, 2 },
                { Ceiling, -1.0f } });
        break;
    case 15: // Final Check: only the true-peak limiter is active
        apply({ { EqOn, 0 }, { GlueOn, 0 }, { MbOn, 0 }, { SatOn, 0 }, { WidthOn, 0 }, { Push, 3 }, { Ceiling, -1.0f } });
        break;
    }
    return s;
}

} // namespace glaze
