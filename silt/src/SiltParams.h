// Silt parameter list (one X-macro: the table in PluginProcessor.cpp and the index enum below stay in sync).
// X (id, name, type, lo, hi, step, skew, default, unit, choices)
#pragma once
#define SLT_PARAM_LIST(X) \
    X (pos, "Position", 'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.25f, "%", "") \
    X (scan, "Scan", 'f', -2.0f, 2.0f, 0.01f, 1.0f, 0.0f, "x", "") \
    X (density, "Density", 'f', 0.5f, 200.0f, 0.1f, 0.35f, 24.0f, "/s", "") \
    X (size, "Grain Size", 'f', 10.0f, 500.0f, 0.1f, 0.45f, 120.0f, "ms", "") \
    X (sizeRand, "Size Random", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.2f, "%", "") \
    X (pitch, "Pitch", 'f', -24.0f, 24.0f, 1.0f, 1.0f, 0.0f, "st", "") \
    X (fine, "Fine Tune", 'f', -100.0f, 100.0f, 1.0f, 1.0f, 0.0f, "ct", "") \
    X (pjit, "Pitch Jitter", 'f', 0.0f, 12.0f, 0.01f, 0.5f, 0.0f, "st", "") \
    X (spray, "Spray", 'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.1f, "%", "") \
    X (spread, "Stereo Spread", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.5f, "%", "") \
    X (shape, "Grain Shape", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.5f, "%", "") \
    X (reverse, "Reverse", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (magnet, "Magnet", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (gcomp, "Level Comp", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.5f, "%", "") \
    X (tune, "Tune", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, "", "Off|Auto|Manual") \
    X (root, "Root Key", 'f', 24.0f, 96.0f, 0.01f, 1.0f, 60.0f, "note", "") \
    X (scale, "Scale", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::scaleChoices()) \
    X (key, "Scale Key", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::keyChoices()) \
    X (cohere, "Cohere", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (glide, "Glide", 'f', 0.0f, 2.0f, 0.001f, 0.4f, 0.0f, "s", "") \
    X (voices, "Voices", 'f', 1.0f, 16.0f, 1.0f, 1.0f, 8.0f, "int", "") \
    X (bend, "Bend Range", 'f', 0.0f, 24.0f, 1.0f, 1.0f, 2.0f, "st", "") \
    X (mpe, "MPE", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", "") \
    X (bOn, "Layer B", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", "") \
    X (morph, "Morph", 'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%", "") \
    X (morphMode, "Morph Mode", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", "Blend|Swap|Ring|Cross") \
    X (bOffset, "B Offset", 'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%", "") \
    X (bPitch, "B Pitch", 'f', -24.0f, 24.0f, 1.0f, 1.0f, 0.0f, "st", "") \
    X (keyMatch, "Key Match", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, "", "") \
    X (fOn, "Filter", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", "") \
    X (fType, "Filter Type", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", "Low Pass|Band Pass|High Pass|Notch") \
    X (cutoff, "Cutoff", 'f', 20.0f, 20000.0f, 1.0f, 0.25f, 6000.0f, "Hz", "") \
    X (res, "Resonance", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.15f, "%", "") \
    X (fRand, "Cutoff Random", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (fKey, "Key Track", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (aA, "Attack", 'f', 0.001f, 10.0f, 0.001f, 0.3f, 0.02f, "s", "") \
    X (aD, "Decay", 'f', 0.001f, 10.0f, 0.001f, 0.3f, 0.4f, "s", "") \
    X (aS, "Sustain", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 1.0f, "%", "") \
    X (aR, "Release", 'f', 0.003f, 10.0f, 0.001f, 0.3f, 0.6f, "s", "") \
    X (velSens, "Velocity", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.6f, "%", "") \
    X (level, "Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB", "") \
    X (l1Rate, "LFO 1 Rate", 'f', 0.01f, 20.0f, 0.01f, 0.3f, 0.5f, "Hz", "") \
    X (l1Sync, "LFO 1 Sync", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::syncChoices()) \
    X (l1Shape, "LFO 1 Shape", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::lfoShapeChoices()) \
    X (l2Rate, "LFO 2 Rate", 'f', 0.01f, 20.0f, 0.01f, 0.3f, 3.0f, "Hz", "") \
    X (l2Sync, "LFO 2 Sync", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::syncChoices()) \
    X (l2Shape, "LFO 2 Shape", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 5.0f, "", slt::lfoShapeChoices()) \
    X (e2A, "Mod Env Attack", 'f', 0.001f, 10.0f, 0.001f, 0.3f, 0.5f, "s", "") \
    X (e2D, "Mod Env Decay", 'f', 0.001f, 10.0f, 0.001f, 0.3f, 1.0f, "s", "") \
    X (e2S, "Mod Env Sustain", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (e2R, "Mod Env Release", 'f', 0.003f, 10.0f, 0.001f, 0.3f, 0.8f, "s", "") \
    X (m1, "Macro 1", 'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%", "") \
    X (m2, "Macro 2", 'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%", "") \
    X (m3, "Macro 3", 'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%", "") \
    X (m4, "Macro 4", 'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%", "") \
    X (ms1, "Mod 1 Source", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modSrcChoices()) \
    X (ms2, "Mod 2 Source", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modSrcChoices()) \
    X (ms3, "Mod 3 Source", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modSrcChoices()) \
    X (ms4, "Mod 4 Source", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modSrcChoices()) \
    X (ms5, "Mod 5 Source", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modSrcChoices()) \
    X (ms6, "Mod 6 Source", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modSrcChoices()) \
    X (ms7, "Mod 7 Source", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modSrcChoices()) \
    X (ms8, "Mod 8 Source", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modSrcChoices()) \
    X (md1, "Mod 1 Target", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modDstChoices()) \
    X (md2, "Mod 2 Target", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modDstChoices()) \
    X (md3, "Mod 3 Target", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modDstChoices()) \
    X (md4, "Mod 4 Target", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modDstChoices()) \
    X (md5, "Mod 5 Target", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modDstChoices()) \
    X (md6, "Mod 6 Target", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modDstChoices()) \
    X (md7, "Mod 7 Target", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modDstChoices()) \
    X (md8, "Mod 8 Target", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", slt::modDstChoices()) \
    X (ma1, "Mod 1 Amount", 'f', -1.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (ma2, "Mod 2 Amount", 'f', -1.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (ma3, "Mod 3 Amount", 'f', -1.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (ma4, "Mod 4 Amount", 'f', -1.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (ma5, "Mod 5 Amount", 'f', -1.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (ma6, "Mod 6 Amount", 'f', -1.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (ma7, "Mod 7 Amount", 'f', -1.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (ma8, "Mod 8 Amount", 'f', -1.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (mode, "Mode", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", "Granular|Slice") \
    X (slPlay, "Slice Play", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", "One Shot|Gate|Granular") \
    X (slNote, "First Slice Key", 'f', 0.0f, 100.0f, 1.0f, 1.0f, 36.0f, "note", "") \
    X (slSrc, "Slices", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", sliceSrcChoices()) \
    X (sens, "Transient Sens", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.5f, "%", "") \
    X (seq, "Sequencer", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", "") \
    X (seqRate, "Seq Rate", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, "", slt::divChoices()) \
    X (seqLen, "Seq Length", 'f', 1.0f, 16.0f, 1.0f, 1.0f, 16.0f, "int", "") \
    X (seqSwing, "Seq Swing", 'f', 0.0f, 0.75f, 0.01f, 1.0f, 0.0f, "%", "") \
    X (seqGate, "Seq Gate", 'f', 0.05f, 1.0f, 0.01f, 1.0f, 0.6f, "%", "") \
    X (seqTrig, "Seq Trigger", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", "Hold Key|Host Play") \
    X (st1, "Step 1", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, "", stepChoices()) \
    X (st2, "Step 2", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, "", stepChoices()) \
    X (st3, "Step 3", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 3.0f, "", stepChoices()) \
    X (st4, "Step 4", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 4.0f, "", stepChoices()) \
    X (st5, "Step 5", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 5.0f, "", stepChoices()) \
    X (st6, "Step 6", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 6.0f, "", stepChoices()) \
    X (st7, "Step 7", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 7.0f, "", stepChoices()) \
    X (st8, "Step 8", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 8.0f, "", stepChoices()) \
    X (st9, "Step 9", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 9.0f, "", stepChoices()) \
    X (st10, "Step 10", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 10.0f, "", stepChoices()) \
    X (st11, "Step 11", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 11.0f, "", stepChoices()) \
    X (st12, "Step 12", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 12.0f, "", stepChoices()) \
    X (st13, "Step 13", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 13.0f, "", stepChoices()) \
    X (st14, "Step 14", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 14.0f, "", stepChoices()) \
    X (st15, "Step 15", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 15.0f, "", stepChoices()) \
    X (st16, "Step 16", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 16.0f, "", stepChoices()) \
    X (fxOrder, "FX Order", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, "", fxOrderChoices()) \
    X (rpOn, "Beat Repeat", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", "") \
    X (rpLen, "Repeat Length", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, "", slt::divChoices()) \
    X (rpChance, "Repeat Chance", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.3f, "%", "") \
    X (rpMix, "Repeat Mix", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 1.0f, "%", "") \
    X (cpOn, "Compressor", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", "") \
    X (cpThr, "Comp Threshold", 'f', -48.0f, 0.0f, 0.1f, 1.0f, -18.0f, "dB", "") \
    X (cpRatio, "Comp Ratio", 'f', 1.0f, 20.0f, 0.1f, 0.5f, 3.0f, ":1", "") \
    X (cpGain, "Comp Makeup", 'f', 0.0f, 24.0f, 0.1f, 1.0f, 3.0f, "dB", "") \
    X (dlOn, "Delay", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", "") \
    X (dlTime, "Delay Time", 'c', 0.0f, 0.0f, 1.0f, 1.0f, 3.0f, "", slt::dlyChoices()) \
    X (dlFb, "Delay Feedback", 'f', 0.0f, 0.95f, 0.01f, 1.0f, 0.35f, "%", "") \
    X (dlMix, "Delay Mix", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.25f, "%", "") \
    X (rvOn, "Reverb", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, "", "") \
    X (rvSize, "Reverb Size", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.6f, "%", "") \
    X (rvDark, "Reverb Dark", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.5f, "%", "") \
    X (rvMix, "Reverb Mix", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.25f, "%", "") \
    X (keepSmp, "Keep My Sample", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, "", "") \
    X (bypass, "Bypass", 'b', 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, "", "")

namespace slt::pid
{
enum Index
{
#define SLT_X(id, ...) id,
    SLT_PARAM_LIST (SLT_X)
#undef SLT_X
    kNumParams
};
}

// the built-in source each factory preset loads on layer A / B (-1 = leave the layer alone); Presets.cpp
int siltFactorySource (int presetIndex, int layer);
