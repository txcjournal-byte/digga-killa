# Basic Pitch model weights

`assets/models/basic_pitch.bin` contains the weights of the Basic Pitch
ICASSP 2022 model by Spotify (https://github.com/spotify/basic-pitch,
Apache License 2.0, see `LICENSE`), extracted from `nmp.onnx` with
`tools/export_basic_pitch.py`. The inference and note decoding in
`source/midi/BasicPitch.cpp` are a C++ re-implementation of the original
Python code; results match the reference implementation note for note.
