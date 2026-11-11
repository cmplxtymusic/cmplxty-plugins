# FALLOUT 1.0.0 design notes

FALLOUT is an original monophonic acid-bass instrument. It borrows the familiar 303-style combination of a sliding mono voice, a resonant ladder, accents and a step grid, then uses CMPLXTY names, layout and code throughout. It is not a model or recreation of 22method NUKE or of the Roland TB-303 circuit.

The voice is one oscillator (six types, polyBLEP on the discontinuous waves, 2x oversampled tanh ladder + ADAA drive), an exponential filter/amp envelope, and portamento when a step or a held MIDI note is marked/played as a slide. The sequencer works in musical time (host PPQ, or an internal clock in the standalone app and in tests). Per-step micro (cents) and rubato (step-fraction timing) are scaled by the global RUBATO amount.

Pattern data lives beside APVTS in the FALLOUT state tree so project recall and `.fallout` user presets keep notes, accents, slides, micro and rubato. Ash and the grid octave are UI settings in that same state, not automatable parameters.

Drag-out writes a Standard MIDI File or an offline render of one loop through a fresh engine at the current sound settings, then uses `performExternalDragDropOfFiles` like Drum Player.
