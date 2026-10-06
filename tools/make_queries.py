#!/usr/bin/env python3
"""
Generate query variants for the workflow scenarios (all 32x32 PNGs):
  exact_<id>.png  - unmodified copy of a database image
  histeq_<id>.png - histogram-equalised grayscale version
  gauss_<id>.png  - Gaussian-smoothed grayscale version (sigma=1.0)
The "same domain, different image" scenario uses query.png (not in the database).

Usage: python3 tools/make_queries.py [image_index ...]   (default: 0 1 2)
Index = position in the sorted dataset/train listing (same order as the C programs).
"""
import os, sys
import numpy as np
from PIL import Image

DB = "dataset/train"
OUT = "queries"
SIGMA = 1.0

def to_gray(img):
    # same weights libpng uses by default (rgb_to_gray_fixed with -1,-1)
    a = np.asarray(img.convert("RGB"), dtype=np.float64)
    g = 0.21267 * a[..., 0] + 0.71516 * a[..., 1] + 0.07217 * a[..., 2]
    return np.clip(np.rint(g), 0, 255).astype(np.uint8)

def hist_equalize(g):
    hist = np.bincount(g.ravel(), minlength=256)
    cdf = hist.cumsum()
    nz = cdf[cdf > 0]
    cdf_min = nz[0]
    total = g.size
    if total == cdf_min:                      # flat image: nothing to equalise
        return g.copy()
    lut = np.rint((cdf - cdf_min) / (total - cdf_min) * 255.0)
    return np.clip(lut, 0, 255).astype(np.uint8)[g]

def gaussian_kernel(sigma):
    r = max(1, int(np.ceil(3 * sigma)))
    x = np.arange(-r, r + 1)
    k = np.exp(-(x ** 2) / (2 * sigma ** 2))
    return k / k.sum()

def gaussian_blur(g, sigma):
    k = gaussian_kernel(sigma)
    r = len(k) // 2
    a = g.astype(np.float64)
    p = np.pad(a, r, mode="reflect")
    # separable: rows then columns
    rows = sum(k[i] * p[:, i:i + a.shape[1]] for i in range(len(k)))
    cols = sum(k[i] * rows[i:i + a.shape[0], :] for i in range(len(k)))
    return np.clip(np.rint(cols), 0, 255).astype(np.uint8)

def main():
    files = sorted(f for f in os.listdir(DB) if f.endswith(".png"))
    if not files:
        sys.exit(f"No PNGs in {DB}. Run: python3 src/convert_cifar10.py")
    idxs = [int(a) for a in sys.argv[1:]] or [0, 1, 2]
    os.makedirs(OUT, exist_ok=True)
    for i in idxs:
        name = files[i]
        img = Image.open(os.path.join(DB, name))
        if img.size != (32, 32):
            sys.exit(f"{name} is not 32x32")
        tag = f"{i:04d}"
        img.convert("RGB").save(os.path.join(OUT, f"exact_{tag}.png"))
        g = to_gray(img)
        Image.fromarray(hist_equalize(g), "L").save(os.path.join(OUT, f"histeq_{tag}.png"))
        Image.fromarray(gaussian_blur(g, SIGMA), "L").save(os.path.join(OUT, f"gauss_{tag}.png"))
        print(f"index {i} ({name}) -> exact/histeq/gauss written to {OUT}/")

if __name__ == "__main__":
    main()
