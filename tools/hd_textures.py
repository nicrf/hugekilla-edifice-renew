#!/usr/bin/env python3
"""The HD textures of the Ultra graphics (data/hd/, roadmap "Ultra"):

    python3 tools/hd_textures.py generate <rawdir> [name ...]
    python3 tools/hd_textures.py process <rawdir> [data/hd]

generate asks Replicate's black-forest-labs/flux-schnell (outputs under Apache-2.0)
for each texture's albedo, one image per name, into <rawdir>/<name>.png; the API
token comes from REPLICATE_API_TOKEN in the environment and is never written
anywhere. process turns them into what the game loads: the albedo made tileable
(blended with itself shifted by half, so the edges meet), 1024x1024 JPEG, and two
maps derived from it, as no generator gives them:
  <name>_n.jpg    tangent-space normal (RGB = XYZ*0.5+0.5), Sobel of a height
                  taken from the luminance (blurred a little, so the noise of the
                  photo doesn't become bumps)
  <name>_r.jpg    R ambient occlusion (the height against its blurred self),
                  G roughness (darker and smoother than its surroundings: glossier),
                  B height
Needs numpy and Pillow (pip install numpy pillow). Records nothing: data/hd/README.md
lists each file's prompt, model, date and license by hand.
"""
import json
import os
import sys
import time
import urllib.request

MODEL = "black-forest-labs/flux-schnell"

COMMON = ("seamless tileable texture, orthographic top-down view, flat even diffuse lighting, "
          "no shadows, no perspective, no objects, photorealistic PBR albedo, highly detailed")

# name: (prompt, seed)
TEXTURES = {
    # (the first grass, "dense short green meadow grass with a few clover leaves and
    # dry blades", seed 11, looked drawn: replaced by two photographic variants)
    "grass": ("flat lay texture photograph of uniform short mown lawn grass, evenly distributed tiny blades, "
              "no focal point, no center, some dry yellowish blades and small weeds, overcast diffuse light, "
              "seamless tileable, no objects, no text, high resolution", 71),
    "grass2": ("flat lay texture photograph of uniform rough pasture ground seen straight down, short sparse "
               "grass, moss, dry straw and brown earth evenly mixed, no focal point, overcast diffuse light, "
               "seamless tileable, no objects, no text, high resolution", 72),
    "rock": ("natural rough weathered mountain rock, grey granite boulders surface with fine grain, small moss patches, " + COMMON, 32),
    "sand": ("close-up photograph of coarse beach sand grains with small shells and pebbles, " + COMMON, 33),
    "snow": ("fresh white snow surface with soft wind ripples, " + COMMON, 14),
    "dirt": ("close-up photograph of dry brown soil ground with small stones and fine cracks, " + COMMON, 35),
    "foam": ("white sea foam bubbles on dark water, " + COMMON, 16),
    # the blocks (textures/block/wall<i>.bmp), each as its classic look
    "wall0": ("grey stone tile wall made of rectangular slate blocks of different sizes with dark mortar, " + COMMON, 20),
    "wall1": ("close-up photograph of rough pale olive green painted stucco wall with grain and small cracks, " + COMMON, 41),
    "wall2": ("close-up photograph of light grey concrete wall with pores, small holes and water stains, " + COMMON, 42),
    "wall3": ("weathered grey wooden planks, horizontal grain, " + COMMON, 23),
    "wall4": ("rusty orange corroded steel plate, " + COMMON, 24),
    "wall5": ("grey stone tile wall made of rectangular slate blocks of different sizes with dark mortar, " + COMMON, 25),
    "wall6": ("ancient carved sandstone blocks with glyphs, olive and ochre, " + COMMON, 26),
    "wall7": ("close-up photograph of dark grey rough cast concrete with visible aggregate stones, " + COMMON, 47),
    "wall8": ("close-up photograph of dark grey concrete panel crossed by one horizontal black rubber stripe, " + COMMON, 48),
    "wall9": ("olive brown wet mud and stone, " + COMMON, 29),
}


def generate(rawdir, names):
    token = os.environ.get("REPLICATE_API_TOKEN")
    if not token:
        sys.exit("REPLICATE_API_TOKEN is not set")
    os.makedirs(rawdir, exist_ok=True)
    for name in names:
        prompt, seed = TEXTURES[name]
        body = json.dumps({"input": {"prompt": prompt, "seed": seed, "aspect_ratio": "1:1",
                                     "output_format": "png", "num_outputs": 1, "megapixels": "1",
                                     "go_fast": False}}).encode()
        req = urllib.request.Request("https://api.replicate.com/v1/models/%s/predictions" % MODEL, body,
                                     {"Authorization": "Bearer " + token, "Content-Type": "application/json",
                                      "Prefer": "wait"})
        pred = json.load(urllib.request.urlopen(req, timeout=120))
        while pred.get("status") not in ("succeeded", "failed", "canceled"):
            time.sleep(2)
            req = urllib.request.Request(pred["urls"]["get"], headers={"Authorization": "Bearer " + token})
            pred = json.load(urllib.request.urlopen(req, timeout=60))
        if pred["status"] != "succeeded":
            print("%s: %s %s" % (name, pred["status"], pred.get("error")))
            continue
        url = pred["output"][0] if isinstance(pred["output"], list) else pred["output"]
        with urllib.request.urlopen(url, timeout=120) as r, open(os.path.join(rawdir, name + ".png"), "wb") as f:
            f.write(r.read())
        print("%s: ok (%s)" % (name, pred.get("id")))


def blur(a, r):
    """box blur, wrapping (the texture tiles), radius r, three times: about a gaussian"""
    import numpy as np
    for _ in range(3):
        for axis in (0, 1):
            acc = np.zeros_like(a)
            for d in range(-r, r + 1):
                acc += np.roll(a, d, axis=axis)
            a = acc / (2 * r + 1)
    return a


def process(rawdir, outdir):
    import numpy as np
    from PIL import Image
    os.makedirs(outdir, exist_ok=True)
    for name in TEXTURES:
        src = os.path.join(rawdir, name + ".png")
        if not os.path.isfile(src):
            continue
        img = np.asarray(Image.open(src).convert("RGB").resize((1024, 1024), Image.LANCZOS), dtype=np.float32) / 255
        n = img.shape[0]
        # tileable: the image where its middle is, its half-shifted copy near the edges
        t = np.abs(np.linspace(-1, 1, n))
        edge = np.maximum(t[:, None], t[None, :])
        w = np.clip((edge - 0.55) / 0.4, 0, 1)
        w = (w * w * (3 - 2 * w))[..., None]
        shifted = np.roll(np.roll(img, n // 2, 0), n // 2, 1)
        albedo = img * (1 - w) + shifted * w
        # low-frequency light left in the photo: flatten it towards the mean
        lum = albedo @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
        low = blur(lum, 24)
        albedo *= (lum.mean() / np.maximum(low, 1e-3))[..., None] ** 0.6
        albedo = np.clip(albedo, 0, 1)
        lum = albedo @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
        height = blur(lum, 1)
        height = (height - height.min()) / max(height.max() - height.min(), 1e-6)
        # Sobel, wrapping
        def sh(a, dy, dx):
            return np.roll(np.roll(a, dy, 0), dx, 1)
        gx = (sh(height, 1, -1) + 2 * sh(height, 0, -1) + sh(height, -1, -1)
              - sh(height, 1, 1) - 2 * sh(height, 0, 1) - sh(height, -1, 1))
        gy = (sh(height, -1, 1) + 2 * sh(height, -1, 0) + sh(height, -1, -1)
              - sh(height, 1, 1) - 2 * sh(height, 1, 0) - sh(height, 1, -1))
        strength = 3.0
        nx, ny, nz = gx * strength, gy * strength, np.ones_like(gx)
        ln = np.sqrt(nx * nx + ny * ny + nz * nz)
        normal = np.stack([nx / ln, ny / ln, nz / ln], -1) * 0.5 + 0.5
        cavity = height - blur(height, 6)
        ao = np.clip(1 + cavity * 2.5, 0.3, 1)
        local = lum - blur(lum, 4)
        rough = np.clip(0.82 - local * 1.5 + (0.5 - lum) * 0.1, 0.35, 1)
        if name == "snow":
            rough = np.clip(rough * 0.75, 0.3, 1)
        if name == "wall4":
            rough = np.clip(rough * 0.8, 0.3, 1)
        orm = np.stack([ao, rough, height], -1)
        def save(a, suffix):
            Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)).save(
                os.path.join(outdir, name + suffix + ".jpg"), quality=88, optimize=True)
        save(albedo, "")
        save(normal, "_n")
        save(orm, "_r")
        print(name)


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("generate", "process"):
        sys.exit(__doc__)
    if sys.argv[1] == "generate":
        generate(sys.argv[2], sys.argv[3:] or list(TEXTURES))
    else:
        process(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "hd"))


if __name__ == "__main__":
    main()
