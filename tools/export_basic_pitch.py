#!/usr/bin/env python3
"""Exports the Basic Pitch (Spotify, Apache-2.0) ICASSP 2022 model weights
into a flat binary blob that source/midi/BasicPitch.cpp runs natively.

Usage: python3 tools/export_basic_pitch.py <path/to/basic-pitch/.../nmp.onnx>
Needs: onnx, onnxruntime, numpy.     Writes: assets/models/basic_pitch.bin

Blob format (little endian): magic "BPW1", uint32 count, then per tensor:
uint32 nameLength, name bytes, uint32 numValues, float32 values.
"""
import struct, sys, tempfile, os
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import numpy_helper

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "models" / "basic_pitch.bin"


def optimised_graph(path):
    model = onnx.load(path)
    for io in list(model.graph.input) + list(model.graph.output):
        io.type.tensor_type.shape.dim[0].dim_value = 1
    tmp = tempfile.mkdtemp()
    fixed, opt = os.path.join(tmp, "b1.onnx"), os.path.join(tmp, "opt.onnx")
    onnx.save(model, fixed)
    so = ort.SessionOptions()
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_BASIC
    so.optimized_model_filepath = opt
    ort.InferenceSession(fixed, so, providers=["CPUExecutionProvider"])
    return onnx.load(opt)


def main():
    graph = optimised_graph(sys.argv[1]).graph
    init = {t.name: numpy_helper.to_array(t).astype(np.float32) for t in graph.initializer}
    nodes = list(graph.node)

    def weight(node, i):
        return init[node.input[i]]

    convs = [n for n in nodes if n.op_type == "Conv"]
    # CQT: first octave real / imaginary kernels + bias, then the anti-alias low-pass
    cqt_real, cqt_imag = convs[0], convs[1]
    lowpass = convs[2]
    assert list(weight(cqt_real, 1).shape) == [36, 1, 1, 256]
    assert list(weight(lowpass, 1).shape) == [1, 1, 1, 256]

    mul_scale = [n for n in nodes if n.op_type == "Mul"][0]
    scale = weight(mul_scale, 1).reshape(-1)
    assert scale.size == 309

    bn_mul = [n for n in nodes if n.op_type == "Mul"][-1]
    bn_add = [n for n in nodes if n.op_type == "Add"][-1]

    cnn = convs[-6:]
    tensors = {
        "cqt.real": weight(cqt_real, 1).reshape(36, 256),
        "cqt.imag": weight(cqt_imag, 1).reshape(36, 256),
        "cqt.bias": weight(cqt_real, 2).reshape(36),
        "cqt.lowpass": weight(lowpass, 1).reshape(256),
        "cqt.scale": scale,
        "bn.mul": weight(bn_mul, 1).reshape(1),
        "bn.add": weight(bn_add, 1).reshape(1),
    }
    names = ["onset1", "contour1", "contour2", "note1", "note2", "onset2"]
    for name, node in zip(names, cnn):
        tensors[name + ".w"] = weight(node, 1)
        tensors[name + ".b"] = weight(node, 2).reshape(-1)
        attrs = {a.name: onnx.helper.get_attribute_value(a) for a in node.attribute}
        print(f"{name}: w{list(weight(node, 1).shape)} strides{attrs['strides']} pads{attrs['pads']}")

    assert np.allclose(tensors["cqt.bias"], 0), "expected bias-free CQT kernels"

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"BPW1")
        f.write(struct.pack("<I", len(tensors)))
        for name, values in tensors.items():
            data = np.ascontiguousarray(values, dtype="<f4").reshape(-1)
            f.write(struct.pack("<I", len(name)))
            f.write(name.encode())
            f.write(struct.pack("<I", data.size))
            f.write(data.tobytes())
    print("wrote", OUT, OUT.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
