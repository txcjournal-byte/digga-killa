#!/usr/bin/env python3
"""Builds the UI skin from docs/design.png.

Static artwork (paper, title, logo, headings, record, knob bodies, labels,
rules) is taken 1:1 from the design. Everything that changes at runtime is
erased from the background and drawn live by the plugin:

    tracklists, tempo text, knob pointers, REVERSE switch, KILL STRENGTH
    slider, "DROP SAMPLE" on the record label

Erased areas are refilled with paper: low-frequency tone diffused in from the
surroundings (ink excluded) + synthetic grain matched to the design's paper.

Usage:  python3 tools/make_skin.py      (needs Pillow + numpy)
Writes: assets/skin/*.png
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "design.png"
OUT = ROOT / "assets" / "skin"

# (x0, y0, x1, y1) in design pixels ----------------------------------------
LOOPS_LIST   = (24, 260, 531, 687)
SHOTS_LIST   = (975, 260, 1321, 687)
TEMPO_TEXT   = (1032, 50, 1247, 76)
REVERSE_SW   = (993, 760, 1060, 797)
STRENGTH     = (1100, 754, 1312, 792)
DROP_SAMPLE  = (612, 332, 872, 472)
KNOB_CENTRES = [(124, 770), (270.5, 770), (420.5, 770), (569.5, 770), (719.5, 770), (873.5, 770)]
KILL_STAMP   = (477, 263, 526, 302)     # A1 row stamp



def lowpass_fill(arr, known_full, factor=8, iterations=600):
    """Diffuse known paper pixels into unknown ones at low resolution."""
    h, w, _ = arr.shape
    sh, sw = h // factor, w // factor
    weights = known_full.astype(float)
    # box-downsample value*weight and weight separately (normalised average)
    blocks = arr[:sh * factor, :sw * factor].reshape(sh, factor, sw, factor, 3)
    wblocks = weights[:sh * factor, :sw * factor].reshape(sh, factor, sw, factor, 1)
    wsum = wblocks.sum(axis=(1, 3))
    small = (blocks * wblocks).sum(axis=(1, 3)) / np.maximum(wsum, 1e-6)
    known = wsum[..., 0] > factor * factor * 0.6
    val = small.copy()
    val[~known] = small[known].mean(axis=0)
    for _ in range(iterations):
        p = np.pad(val, ((1, 1), (1, 1), (0, 0)), mode="edge")
        avg = (p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:]) / 4
        val = np.where(known[..., None], small, avg)
    up = Image.fromarray(np.clip(val, 0, 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    return np.asarray(up.filter(ImageFilter.GaussianBlur(10))).astype(float)


def grain(w, h, sigma, seed):
    """Paper grain: fine noise, soft mottling and sparse dirt specks."""
    rng = np.random.default_rng(seed)
    fine = rng.normal(0, 1, (h, w))
    fine = np.asarray(Image.fromarray(((fine * 40) + 128).clip(0, 255).astype(np.uint8))
                      .filter(ImageFilter.GaussianBlur(0.6))).astype(float) - 128
    mottle = rng.normal(0, 1, (h // 12 + 2, w // 12 + 2))
    mottle = np.asarray(Image.fromarray(((mottle * 40) + 128).clip(0, 255).astype(np.uint8))
                        .resize((w, h), Image.BICUBIC)).astype(float) - 128
    g = fine / max(fine.std(), 1e-6) * sigma * 0.8 + mottle / max(mottle.std(), 1e-6) * sigma * 0.3
    specks = rng.random((h, w)) < 0.0015
    g[specks] -= rng.uniform(15, 45, specks.sum())
    g = np.asarray(Image.fromarray((g + 128).clip(0, 255).astype(np.uint8))
                   .filter(ImageFilter.GaussianBlur(0.4))).astype(float) - 128
    tint = np.array([1.0, 0.97, 0.9])      # dirt is warm, not grey
    return g[..., None] * tint


def paper_sigma(arr, known):
    lum = arr.mean(axis=2)
    base = np.asarray(Image.fromarray(lum.astype(np.uint8)).filter(ImageFilter.GaussianBlur(3))).astype(float)
    hp = (lum - base)[known]
    return float(np.clip(hp.std(), 2.0, 9.0))


def erase(img, boxes, seed):
    arr = np.asarray(img).astype(float)
    lum = arr.mean(axis=2)
    known = lum > 150                     # paper only, ink doesn't tint the fill
    for x0, y0, x1, y1 in boxes:
        known[y0:y1, x0:x1] = False
    low = lowpass_fill(arr, known)
    sigma = paper_sigma(arr, known)
    out = arr.copy()
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        fill = low[y0:y1, x0:x1] + grain(x1 - x0, y1 - y0, sigma, seed + i)
        fx = np.minimum(np.arange(x1 - x0), np.arange(x1 - x0)[::-1])
        fy = np.minimum(np.arange(y1 - y0), np.arange(y1 - y0)[::-1])
        a = np.clip(np.minimum.outer(fy, fx) / 3.0, 0, 1)[..., None]
        out[y0:y1, x0:x1] = a * fill + (1 - a) * arr[y0:y1, x0:x1]
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def erase_knob_pointers(img):
    arr = np.asarray(img).astype(float)
    for cx, cy in KNOB_CENTRES:
        x0, x1 = int(cx) - 5, int(cx) + 6
        y0, y1 = int(cy) - 47, int(cy) - 18
        left = arr[y0:y1, x0 - 11:x1 - 11]
        right = arr[y0:y1, x0 + 11:x1 + 11]
        arr[y0:y1, x0:x1] = (left + right[:, ::-1]) / 2
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def ink_sprite(img, box, colour, channel="dark"):
    """Crop a printed element and turn it into colour + alpha."""
    patch = np.asarray(img.crop(box)).astype(float)
    r, g, b = patch[..., 0], patch[..., 1], patch[..., 2]
    if channel == "red":
        a = np.clip((r - (g + b) / 2 - 30) / 80, 0, 1)
    else:
        lum = patch.mean(axis=2)
        a = np.clip((200 - lum) / 150, 0, 1)
    out = np.zeros(patch.shape[:2] + (4,), np.uint8)
    out[..., :3] = colour
    out[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    design = Image.open(SRC).convert("RGB")

    ink_sprite(design, DROP_SAMPLE, (22, 21, 19)).save(OUT / "drop_sample.png")
    ink_sprite(design, KILL_STAMP, (200, 32, 42), "red").save(OUT / "kill_stamp.png")

    bg = erase_knob_pointers(design)
    bg = erase(bg, [LOOPS_LIST, SHOTS_LIST, TEMPO_TEXT, REVERSE_SW, STRENGTH, DROP_SAMPLE], 1)
    bg.save(OUT / "background.png", optimize=True)
    print("skin written to", OUT)


if __name__ == "__main__":
    main()
