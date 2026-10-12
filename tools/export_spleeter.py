"""Export the Spleeter 4-stem model (Deezer, MIT) to the compact binary TrapWin Sampler loads.

usage: python3 tools/export_spleeter.py <spleeter/4stems/model> <out.bin> [reference.npz <song.wav>]

Weights come from https://github.com/deezer/spleeter/releases/download/v1.4.0/4stems.tar.gz.
Layout (little endian): "SPL4", u32 version=1, then for each stem (vocals, drums, bass, other):
  6 encoder layers:  conv kernel [out][in][5][5] f16, bias f32[out], bn scale f32[out], bn shift f32[out]
  6 decoder layers:  convT kernel [in][out][5][5] f16, bias f32[out], bn scale/shift f32[out]
  final conv:        kernel [2][1][4][4] f16, bias f32[2]
Batch norm is stored folded: y = x * scale + shift (eps 1e-3, inference statistics).
With a reference path it also runs the original TensorFlow graph on the song and stores the
network input / outputs of the first 512-frame segment for the C++ test.
"""
import sys, struct
import numpy as np
import tensorflow as tf

ckpt, out_path = sys.argv[1], sys.argv[2]
r = tf.train.load_checkpoint(ckpt)
get = r.get_tensor

def bn(i):
    g, b = get(f"batch_normalization{'' if i == 0 else '_%d' % i}/gamma"), get(f"batch_normalization{'' if i == 0 else '_%d' % i}/beta")
    m, v = get(f"batch_normalization{'' if i == 0 else '_%d' % i}/moving_mean"), get(f"batch_normalization{'' if i == 0 else '_%d' % i}/moving_variance")
    s = g / np.sqrt(v + 1e-3)
    return s.astype(np.float32), (b - m * s).astype(np.float32)

def name(base, i):
    return base if i == 0 else f"{base}_{i}"

with open(out_path, "wb") as f:
    f.write(b"SPL4"); f.write(struct.pack("<I", 1))
    for stem in range(4):
        for j in range(6):
            k = get(name("conv2d", 7 * stem + j) + "/kernel")          # [kh, kw, in, out]
            f.write(np.ascontiguousarray(k.transpose(3, 2, 0, 1)).astype(np.float16).tobytes())
            f.write(get(name("conv2d", 7 * stem + j) + "/bias").astype(np.float32).tobytes())
            s, t = bn(12 * stem + j); f.write(s.tobytes()); f.write(t.tobytes())
        for j in range(6):
            k = get(name("conv2d_transpose", 6 * stem + j) + "/kernel")  # [kh, kw, out, in]
            f.write(np.ascontiguousarray(k.transpose(3, 2, 0, 1)).astype(np.float16).tobytes())
            f.write(get(name("conv2d_transpose", 6 * stem + j) + "/bias").astype(np.float32).tobytes())
            s, t = bn(12 * stem + 6 + j); f.write(s.tobytes()); f.write(t.tobytes())
        k = get(name("conv2d", 7 * stem + 6) + "/kernel")               # [4, 4, 1, 2]
        f.write(np.ascontiguousarray(k.transpose(3, 2, 0, 1)).astype(np.float16).tobytes())
        f.write(get(name("conv2d", 7 * stem + 6) + "/bias").astype(np.float32).tobytes())
print("wrote", out_path)

if len(sys.argv) > 4:
    import wave
    ref_path, wav_path = sys.argv[3], sys.argv[4]
    w = wave.open(wav_path)
    assert w.getsampwidth() == 2 and w.getframerate() == 44100
    a = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32) / 32768.0
    a = a.reshape(-1, w.getnchannels())
    if a.shape[1] == 1: a = np.repeat(a, 2, axis=1)
    tf.compat.v1.disable_eager_execution()
    g = tf.Graph()
    with g.as_default():
        saver = tf.compat.v1.train.import_meta_graph(ckpt + ".meta")
        with tf.compat.v1.Session(graph=g) as sess:
            saver.restore(sess, ckpt)
            fetch = ["strided_slice_3:0"] + [f"{s}_spectrogram/mul:0" for s in ("vocals", "drums", "bass", "other")]
            res = sess.run(fetch, {"waveform:0": a[: 44100 * 14], "keras_learning_phase:0": False})
    np.savez(ref_path, input=res[0][0], vocals=res[1][0], drums=res[2][0], bass=res[3][0], other=res[4][0])
    print("reference", res[0].shape)
