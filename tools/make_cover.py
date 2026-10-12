"""Shop cover for TrapWin Sampler (1600 x 1600), in the style of the other TrapWin covers:
official logo, chrome SAMPLER, tagline, the plugin UI with an ice-blue glow, feature line.

usage: python3 -I tools/make_cover.py <ui_2x.png> <out.png>
The UI render comes from the test tool (ui_cover.png, 2x).
"""
import sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ui_p, out_p = sys.argv[1:3]
W = H = 1600

# ---- background: near-black with the web's blue haze
y, x = np.mgrid[0:H, 0:W].astype(np.float32)
bg = np.zeros((H, W, 3), np.float32) + np.array([4, 6, 10], np.float32)
haze = np.exp(-(((x - W / 2) / 760) ** 2 + ((y - 120) / 420) ** 2))
bg += haze[..., None] * np.array([20, 38, 78], np.float32)
img = Image.fromarray(np.clip(bg, 0, 255).astype(np.uint8)).convert("RGBA")


def glow_layer(mask, colour, radius, strength):
    g = mask.filter(ImageFilter.GaussianBlur(radius))
    a = np.asarray(g, np.float32) / 255.0 * strength
    layer = np.zeros((mask.size[1], mask.size[0], 4), np.uint8)
    layer[..., :3] = colour
    layer[..., 3] = np.clip(a * 255, 0, 255).astype(np.uint8)
    return Image.fromarray(layer, "RGBA")


def chrome_text(text, font, box_w):
    """Chrome-gradient text (white top, steel middle band, bright bottom) with a blue glow."""
    l, t, r, b = font.getbbox(text)
    w, h = r - l + 40, b - t + 40
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).text((20 - l, 20 - t), text, font=font, fill=255)
    yy = np.linspace(0, 1, h)[:, None]
    stops = [(0.0, (250, 252, 255)), (0.42, (205, 215, 232)), (0.5, (120, 136, 165)), (0.58, (178, 192, 216)),
             (0.8, (238, 244, 255)), (1.0, (160, 176, 204))]
    grad = np.zeros((h, 1, 3), np.float32)
    for c in range(3):
        grad[..., c] = np.interp(yy, [s[0] for s in stops], [s[1][c] for s in stops])
    rgb = np.repeat(grad, w, axis=1)
    layer = np.dstack([rgb, np.asarray(mask, np.float32)]).astype(np.uint8)
    text_img = Image.fromarray(layer, "RGBA")
    out = Image.new("RGBA", (w + 120, h + 120), (0, 0, 0, 0))
    big_mask = Image.new("L", out.size, 0)
    big_mask.paste(mask, (60, 60))
    out.alpha_composite(glow_layer(big_mask, (70, 118, 196), 28, 0.9))
    out.alpha_composite(glow_layer(big_mask, (159, 195, 255), 6, 0.35))
    out.alpha_composite(text_img, (60, 60))
    scale = min(1.0, box_w / out.size[0])
    return out.resize((int(out.size[0] * scale), int(out.size[1] * scale)), Image.LANCZOS)


def spaced(draw, centre_x, top, text, font, fill, spacing):
    widths = [font.getlength(ch) for ch in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x0 = centre_x - total / 2
    for ch, w in zip(text, widths):
        draw.text((x0, top), ch, font=font, fill=fill)
        x0 += w + spacing


fonts = os.path.join(root, "assets", "fonts")
display = ImageFont.truetype(os.path.join(fonts, "ArchivoDisplay-BlackItalic.ttf"), 210)
mono = ImageFont.truetype(os.path.join(fonts, "IBMPlexMono-SemiBold.ttf"), 44)
mono_small = ImageFont.truetype(os.path.join(fonts, "IBMPlexMono-Medium.ttf"), 40)

# ---- logo + SAMPLER + tagline
logo = Image.open(os.path.join(root, "assets", "brand", "trapwin_logo.png")).convert("RGBA")
lw = 760
logo = logo.resize((lw, int(logo.size[1] * lw / logo.size[0])), Image.LANCZOS)
img.alpha_composite(logo, ((W - lw) // 2, 18))

title = chrome_text("SAMPLER", display, 1300)
img.alpha_composite(title, ((W - title.size[0]) // 2, 250))

d = ImageDraw.Draw(img)
spaced(d, W / 2, 506, "DROP A SONG. GET THE KIT.", mono, (159, 195, 255, 255), 6)

# ---- the plugin UI, framed, with a soft glow
ui = Image.open(ui_p).convert("RGBA")
uw = 1300
ui = ui.resize((uw, int(ui.size[1] * uw / ui.size[0])), Image.LANCZOS)
ux, uy = (W - uw) // 2, 584
frame_mask = Image.new("L", (W, H), 0)
ImageDraw.Draw(frame_mask).rectangle([ux - 6, uy - 6, ux + uw + 6, uy + ui.size[1] + 6], fill=255)
img.alpha_composite(glow_layer(frame_mask, (30, 70, 150), 46, 0.55))
fd = ImageDraw.Draw(img)
fd.rectangle([ux - 3, uy - 3, ux + uw + 2, uy + ui.size[1] + 2], outline=(88, 107, 140, 255), width=2)
fd.rectangle([ux - 1, uy - 1, ux + uw, uy + ui.size[1]], outline=(143, 175, 229, 255), width=1)
img.alpha_composite(ui, (ux, uy))

# ---- features
by = uy + ui.size[1] + 46
spaced(d, W / 2, by, "STEMS  ·  LOOPS  ·  ONE-SHOTS  ·  MIDI", mono, (230, 236, 247, 255), 5)
spaced(d, W / 2, by + 70, "VST3 / AU  ·  WINDOWS & macOS", mono_small, (127, 166, 232, 255), 5)

img.convert("RGB").save(out_p, quality=95)
print("wrote", out_p)
