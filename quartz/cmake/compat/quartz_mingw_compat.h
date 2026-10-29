// Force-included for the llvm-mingw JUCE build: constants JUCE expects from the MS SDK that mingw-w64 lacks.
#pragma once
#ifdef __cplusplus
constexpr int CaretPosition_Unknown = 0;
constexpr int CaretPosition_EndOfLine = 1;
constexpr int CaretPosition_BeginningOfLine = 2;
constexpr int D2D1_SATURATION_PROP_SATURATION = 0;
#endif
