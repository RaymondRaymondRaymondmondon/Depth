# Tileable PBR texture sets for large surfaces (the Trawl Visual Overhaul Spec, phase 6: the boat and the dock).
#
# A 22 m boat on one baked atlas would be a blur up close, so big surfaces use small textures that repeat, with UVs
# in metres, and their ambient occlusion baked into vertex colours instead. Every texture here is made with numpy:
# noise is white noise filtered in the frequency domain, so it wraps perfectly at the tile's edges. A tile set is
# (base RGB in display space, roughness, metallic, height); the height becomes a tangent-space normal map.

import numpy as np


def _rng(seed):
    return np.random.default_rng(seed)


def noise(h, w, feat_u, feat_v=None, seed=0):
    """Tileable smooth noise in 0..1 with features about feat_u x feat_v pixels (u across, v down)."""
    feat_v = feat_v or feat_u
    wn = _rng(seed).standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    filt = np.exp(-((fx * feat_u) ** 2 + (fy * feat_v) ** 2) * 6.0)
    n = np.real(np.fft.ifft2(np.fft.fft2(wn) * filt))
    n -= n.min()
    return n / max(n.max(), 1e-9)


def fbm(h, w, feat, seed=0, octaves=4, aniso=1.0):
    acc = np.zeros((h, w)); amp, tot = 1.0, 0.0
    for o in range(octaves):
        f = feat / (2 ** o)
        acc += amp * noise(h, w, f * aniso, f, seed + o * 17)
        tot += amp; amp *= 0.5
    return acc / tot


def lerp(a, b, t):
    t = np.asarray(t)[..., None] if np.ndim(t) == 2 and np.ndim(a) == 1 else t
    return a + (b - a) * t


def colour(base, tone):
    """Broadcasts an RGB triple over a 2D tone map (tone multiplies)."""
    return np.asarray(base)[None, None, :] * tone[..., None]


def mix(c0, c1, m):
    return c0 * (1 - m[..., None]) + c1 * m[..., None]


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def normal_from_height(hgt, strength):
    """Tangent-space normal (OpenGL convention) from a height map, wrapping at the edges. Rows are written to
    Blender as they stand (row 0 at the bottom, v = 0), so a row's index runs with +v."""
    dx = (np.roll(hgt, -1, axis=1) - np.roll(hgt, 1, axis=1)) * 0.5
    dy = (np.roll(hgt, -1, axis=0) - np.roll(hgt, 1, axis=0)) * 0.5
    nx, ny, nz = -dx * strength, -dy * strength, np.ones_like(hgt)
    l = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx / l * 0.5 + 0.5, ny / l * 0.5 + 0.5, nz / l * 0.5 + 0.5], axis=-1)


class Tile:
    def __init__(self, name, base, rough, metal, height, nstrength, size_m):
        self.name, self.base, self.rough, self.metal = name, np.clip(base, 0, 1), np.clip(rough, 0, 1), np.clip(metal, 0, 1)
        self.normal = normal_from_height(height, nstrength)
        self.size_m = size_m   # (u metres, v metres) one tile covers


# ---------------------------------------------------------------- the sets
def planks(name, w, h, rows, size_m, wood, seam_col, seed, weather=0.0, gloss=0.25, butts=2, nails=True, paint=None):
    """Boards along u, `rows` of them down the tile, butt joints staggered per row, caulked seams.
    wood: (light RGB, dark RGB); paint: optional (RGB, chip amount) painted over the boards."""
    rng = _rng(seed)
    v = np.arange(h)[:, None] / h * rows
    u = np.arange(w)[None, :] / w
    row = np.floor(v).astype(int) % rows
    fv = v - np.floor(v)
    offs = rng.random(rows)
    seg = np.floor(((u - offs[row]) % 1.0) * butts).astype(int)
    fu = ((u - offs[row]) % 1.0) * butts - seg
    pid = row * 7 + seg
    tones = 0.82 + 0.3 * rng.random(rows * 7 + butts + 1)
    tone = tones[pid]
    grain = fbm(h, w, 6, seed + 1, 3, aniso=14.0)
    streak = noise(h, w, 220, 3, seed + 2)
    figure = 0.75 + 0.25 * np.sin((grain * 9 + fv * 3 + pid * 1.7) * 3.1)
    t = np.clip(tone * figure * (0.85 + 0.3 * streak), 0, 1.6)
    base = mix(np.ones((h, w, 3)) * np.asarray(wood[1]), np.ones((h, w, 3)) * np.asarray(wood[0]), np.clip(t - 0.55, 0, 1) * 1.5)
    if weather > 0:   # silvered, scrubbed grey where the deck is walked
        silver = fbm(h, w, 60, seed + 3, 3)
        base = mix(base, np.ones((h, w, 3)) * np.array([0.52, 0.5, 0.46]) * (0.8 + 0.3 * grain[..., None]), np.clip(weather * (0.6 + 0.6 * silver), 0, 1))
    hgt = 0.6 + 0.25 * grain + 0.1 * np.sin(fv * np.pi)
    rough = np.full((h, w), 1 - gloss) - 0.12 * grain
    # seams between rows and the butt joints
    sv = 1.6 / (h / rows)
    seam = (fv < sv) | (fv > 1 - sv)
    su = 1.2 / (w / butts)
    butt = (fu < su) | (fu > 1 - su)
    groove = seam | butt
    if paint is not None:
        pc, chip = paint
        chips = (fbm(h, w, 18, seed + 9, 4) > 1 - chip * 0.55) | ((fbm(h, w, 6, seed + 10, 3) > 1 - chip * 0.9) & (groove | (fv < 0.12) | (fv > 0.88)))
        ptone = 0.88 + 0.16 * fbm(h, w, 40, seed + 11, 3)
        painted = colour(pc, ptone)
        base = np.where(chips[..., None], base * 0.7, painted)
        rough = np.where(chips, 0.85, 0.5 + 0.15 * fbm(h, w, 30, seed + 12, 2))
        hgt = np.where(chips, hgt - 0.15, hgt + 0.05)
    base = np.where(groove[..., None], np.asarray(seam_col)[None, None, :] * (0.8 + 0.3 * grain[..., None]), base)
    hgt = np.where(groove, 0.0, hgt)
    rough = np.where(groove, 0.7, rough)
    if nails:   # two trenails at each end of a board
        for r in range(rows):
            for s in range(butts):
                for e in (0.04, 0.96):
                    cu = ((offs[r] + (s + e) / butts) % 1.0) * w
                    for fvn in (0.3, 0.7):
                        cv = (r + fvn) / rows * h
                        yy, xx = np.ogrid[:h, :w]
                        dxx = np.minimum(np.abs(xx - cu), w - np.abs(xx - cu))
                        d = np.sqrt(dxx ** 2 + (yy - cv) ** 2)
                        m = d < 2.6
                        base[m] *= 0.62
                        hgt[m] -= 0.08
    return Tile(name, base, rough, np.zeros((h, w)), hgt, 6.0, size_m)


def painted_metal(name, w, h, size_m, paint, seed, rust=0.4, gloss=0.45, metal_bare=0.8, streaks=0.5, plates=0):
    """Paint over steel or iron: rust blooms and runs, scuffed bare metal, optionally plate seams with rivets."""
    rng = _rng(seed)
    n1 = fbm(h, w, 70, seed, 4)
    n2 = fbm(h, w, 14, seed + 1, 3)
    run = noise(h, w, 6, 90, seed + 2)
    ptone = 0.9 + 0.12 * n1
    base = colour(paint, ptone)
    rough = np.full((h, w), 1 - gloss) + 0.1 * n2
    metal = np.zeros((h, w))
    hgt = 0.5 + 0.08 * n2
    rmask = smoothstep(1 - rust * 0.55, 1 - rust * 0.35, n1 * 0.6 + n2 * 0.4)
    rcol = mix(np.ones((h, w, 3)) * np.array([0.32, 0.13, 0.05]), np.ones((h, w, 3)) * np.array([0.5, 0.24, 0.08]), n2)
    base = mix(base, rcol, rmask)
    rough = rough * (1 - rmask) + 0.9 * rmask
    hgt += rmask * 0.12 * n2
    if streaks > 0:   # rust running down from the blooms
        smear = np.copy(rmask)
        for k in range(1, 40):
            smear = np.maximum(smear, np.roll(rmask, k, axis=0) * (1 - k / 40.0) * (run > 0.45))
        s = np.clip(smear - rmask, 0, 1) * streaks
        base = mix(base, base * np.array([0.75, 0.55, 0.42]), s)
    scuff = fbm(h, w, 8, seed + 5, 2) > 0.8
    base = np.where(scuff[..., None], np.array([0.4, 0.4, 0.4]) * (0.8 + 0.3 * n2[..., None]), base)
    metal = np.where(scuff, metal_bare, metal)
    rough = np.where(scuff, 0.45, rough)
    if plates:
        yy, xx = np.ogrid[:h, :w]
        pu, pv = w // plates, h // plates
        edge = ((xx % pu) < 2) | ((yy % pv) < 2)
        hgt = np.where(edge, hgt - 0.3, hgt)
        base = np.where(edge[..., None], base * 0.55, base)
        for cy in range(0, h, pv):
            for cx in range(6, w, 22):
                d = np.sqrt((xx - cx) ** 2 + (yy - (cy + 7) % h) ** 2)
                m = d < 3.2
                hgt = np.where(m, hgt + 0.35 * (1 - d / 3.2), hgt)
        for cx in range(0, w, pu):
            for cy in range(6, h, 22):
                d = np.sqrt((xx - (cx + 7) % w) ** 2 + (yy - cy) ** 2)
                m = d < 3.2
                hgt = np.where(m, hgt + 0.35 * (1 - d / 3.2), hgt)
    return Tile(name, base, rough, metal, hgt, 5.0, size_m)


def chequer_plate(name, w, h, size_m, seed):
    """Engine-room floor plate: raised diagonal lozenges, oily and worn bright where it's walked."""
    yy, xx = np.mgrid[:h, :w] / np.array([h, w])[:, None, None]
    cells = 8
    a = (xx * cells + yy * cells) % 1.0
    b = (xx * cells - yy * cells) % 1.0
    lozA = (np.abs(a - 0.5) < 0.08) & (np.abs(((xx + yy) * cells * 2) % 2 - 1) < 0.6)
    lozB = (np.abs(b - 0.5) < 0.08) & (np.abs(((xx - yy) * cells * 2) % 2 - 1) < 0.6)
    hgt = np.where(lozA | lozB, 1.0, 0.0) + 0.05 * fbm(h, w, 20, seed, 3)
    oil = fbm(h, w, 60, seed + 1, 4)
    base = colour([0.34, 0.33, 0.31], 0.8 + 0.3 * oil)
    base = np.where((lozA | lozB)[..., None], base * 1.35, base)
    rough = 0.55 - 0.3 * smoothstep(0.55, 0.8, oil)
    metal = np.full((h, w), 0.7)
    return Tile(name, base, rough, metal, hgt, 4.0, size_m)


def antifouling(name, w, h, size_m, seed):
    """Below the waterline: dark copper-red antifouling, slime and weed streaks, a few barnacles."""
    n = fbm(h, w, 50, seed, 4)
    weed = noise(h, w, 8, 60, seed + 1)
    base = colour([0.16, 0.07, 0.05], 0.8 + 0.4 * n)
    base = mix(base, np.ones((h, w, 3)) * np.array([0.1, 0.13, 0.07]), smoothstep(0.55, 0.75, weed))
    rough = 0.65 + 0.2 * n
    hgt = 0.4 * n
    rng = _rng(seed + 2)
    yy, xx = np.ogrid[:h, :w]
    for _ in range(26):
        cx, cy, r = rng.random() * w, rng.random() * h, 2 + rng.random() * 4
        d = np.sqrt(np.minimum(np.abs(xx - cx), w - np.abs(xx - cx)) ** 2 + np.minimum(np.abs(yy - cy), h - np.abs(yy - cy)) ** 2)
        m = d < r
        base[m] = np.array([0.55, 0.52, 0.46])
        hgt = np.where(m, hgt + 0.8 * (1 - d / r), hgt)
    return Tile(name, base, rough, np.zeros((h, w)), hgt, 6.0, size_m)


def stone(name, w, h, size_m, seed, course=4, tone=(0.42, 0.4, 0.37)):
    """Dressed stone blocks in courses (the quay's face and top), mortar, salt and weed toward the bottom."""
    rng = _rng(seed)
    v = np.arange(h)[:, None] / h * course
    u = np.arange(w)[None, :] / w
    row = np.floor(v).astype(int)
    off = rng.random(course)
    blocks = 2
    fu = ((u - off[row]) % 1.0) * blocks
    seg = np.floor(fu).astype(int)
    fu -= seg
    fv = v - np.floor(v)
    n = fbm(h, w, 30, seed + 1, 4)
    t = 0.85 + 0.25 * rng.random(course * blocks + 2)[row * blocks + seg] + 0.2 * (n - 0.5)
    base = colour(tone, t)
    joint = (fv < 0.035) | (fv > 0.965) | (fu < 0.018) | (fu > 0.982)
    base = np.where(joint[..., None], base * 0.45, base)
    edge = np.minimum(np.minimum(fv, 1 - fv) * 8, np.minimum(fu, 1 - fu) * 16)
    hgt = np.where(joint, 0.0, np.clip(edge, 0, 1) * 0.6 + 0.3 * n)
    rough = 0.85 - 0.1 * n
    return Tile(name, base, rough, np.zeros((h, w)), hgt, 5.0, size_m)


def shingles(name, w, h, size_m, seed, col=(0.2, 0.16, 0.14)):
    """Tarred roofing boards / shingles for the sheds."""
    return planks(name, w, h, 6, size_m, ((col[0] * 1.4, col[1] * 1.4, col[2] * 1.4), col), (0.04, 0.035, 0.03), seed, butts=3, nails=False, gloss=0.2)


def build_all():
    """Every tile set the boat and the dock use, by name."""
    T = {}
    T["deck"] = planks("deck", 1024, 512, 4, (2.0, 0.6), ((0.62, 0.52, 0.38), (0.36, 0.27, 0.18)), (0.04, 0.035, 0.03), 11, weather=0.55, gloss=0.2)
    T["hull_red"] = planks("hull_red", 512, 512, 3, (2.0, 0.6), ((0.5, 0.38, 0.26), (0.3, 0.22, 0.14)), (0.08, 0.03, 0.02), 21, nails=False, paint=((0.36, 0.07, 0.05), 0.35))
    T["boot"] = planks("boot", 512, 256, 1, (2.0, 0.2), ((0.5, 0.38, 0.26), (0.3, 0.22, 0.14)), (0.3, 0.27, 0.2), 23, nails=False, paint=((0.74, 0.7, 0.6), 0.25))
    T["bottom"] = antifouling("bottom", 512, 512, (2.0, 2.0), 31)
    T["house"] = planks("house", 512, 512, 6, (1.5, 0.75), ((0.5, 0.28, 0.14), (0.26, 0.12, 0.05)), (0.06, 0.03, 0.02), 41, nails=False, gloss=0.55, butts=1)
    T["cream"] = planks("cream", 512, 512, 6, (1.5, 0.75), ((0.5, 0.38, 0.26), (0.3, 0.22, 0.14)), (0.2, 0.18, 0.14), 43, nails=False, butts=1, paint=((0.72, 0.68, 0.58), 0.3))
    T["iron"] = painted_metal("iron", 512, 512, (1.0, 1.0), (0.07, 0.07, 0.07), 51, rust=0.45, gloss=0.4)
    T["bulk"] = painted_metal("bulk", 512, 512, (2.0, 2.0), (0.46, 0.5, 0.44), 53, rust=0.35, gloss=0.35, plates=2)
    T["chequer"] = chequer_plate("chequer", 512, 512, (1.0, 1.0), 61)
    T["stone"] = stone("stone", 512, 512, (2.4, 1.6), 71)
    T["shingle"] = shingles("shingle", 512, 512, (1.5, 1.5), 81)
    T["shed"] = planks("shed", 512, 512, 5, (2.0, 1.0), ((0.46, 0.36, 0.26), (0.24, 0.17, 0.11)), (0.05, 0.04, 0.03), 91, weather=0.5, butts=1, nails=False)
    return T
