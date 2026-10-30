# Instrument tagger (models/instruments.onnx)

YAMNet (Apache-2.0, tensorflow/models research/audioset/yamnet) exported to ONNX
(`export.py`; needs yamnet.py / params.py / features.py + yamnet.h5 from that repo),
input `mag` [N,96,257], outputs `scores` [N,521] and `groups` [N,11].

`groups` is a small logistic-regression head (`add_head.py`) trained by
`train_head.py` / `fit_head.py` on clips rendered with FluidR3_GM and TimGM6mb
soundfonts (used for training only, not shipped). `make_inst.py` renders the
held-out GeneralUser GS test set; `eval_inst.py <RewiredCli>` scores it.
Groups: Horns, Winds, Strings, Piano / Keys, Organ, Guitar, Mallets / Bells,
Synth, Vocals, Bass Guitar, Upright Bass.
