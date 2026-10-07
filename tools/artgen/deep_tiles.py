# Tileable texture sets for The Deep's Nautilus (Verne's: riveted iron outside, walnut and brass within), made with
# numpy like tiles.py (whose helpers they share): base colour, roughness, metallic and a height turned into a normal.
import numpy as np
import tiles as T


def hull_iron(name, w, h, size_m, seed, fouling=0.6):
    """The Nautilus's skin: overlapping riveted iron plates, dark and oily, rust running from the seams and rivets,
    and the derelict's fouling: green-brown algae film and pale barnacle crusts thickest low on the hull."""
    t = T.painted_metal(name, w, h, size_m, (0.15, 0.16, 0.15), seed, rust=0.55, gloss=0.35, metal_bare=0.6, streaks=0.8, plates=2)
    base, rough, hgt = t.base, t.rough, np.zeros((h, w))
    alg = T.fbm(h, w, 40, seed + 21, 4)
    film = T.smoothstep(1 - fouling * 0.6, 1 - fouling * 0.25, alg)
    algae = T.mix(np.ones((h, w, 3)) * np.array([0.10, 0.13, 0.06]), np.ones((h, w, 3)) * np.array([0.20, 0.22, 0.10]), T.fbm(h, w, 10, seed + 22, 3))
    base = T.mix(base, algae, film * 0.85)
    rough = rough * (1 - film) + 0.8 * film
    # barnacles: small raised pale cones, clustered where the film is thick
    rng = np.random.default_rng(seed + 23)
    yy, xx = np.ogrid[:h, :w]
    for _ in range(int(220 * fouling)):
        cx, cy = rng.integers(0, w), rng.integers(0, h)
        if film[cy, cx] < 0.4:
            continue
        r = rng.uniform(2.0, 5.5)
        dx = np.minimum(np.abs(xx - cx), w - np.abs(xx - cx)); dy = np.minimum(np.abs(yy - cy), h - np.abs(yy - cy))
        d = np.sqrt(dx ** 2 + dy ** 2)
        m = d < r
        ring = (d > r * 0.45) & m
        base[m] = base[m] * 0.3 + np.array([0.62, 0.6, 0.52]) * 0.7
        base[d < r * 0.3] *= 0.4                     # the dark mouth
        hgt = np.where(ring, hgt + 0.5 * (1 - d / r), hgt)
        rough = np.where(m, 0.9, rough)
    tt = T.Tile(name, base, rough, t.metal * (1 - film), hgt, 6.0, size_m)
    # the plates' and rivets' normals with the barnacles' on top (a whiteout-style blend of the two maps)
    n1 = t.normal * 2 - 1; n2 = tt.normal * 2 - 1
    n = np.stack([n1[..., 0] + n2[..., 0], n1[..., 1] + n2[..., 1], n1[..., 2] * n2[..., 2]], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    tt.normal = n * 0.5 + 0.5
    return tt


def walnut_panel(name, w, h, size_m, seed):
    """Wainscot and wall panels: vertical walnut boards, deep and glossy, with a raised panel moulding."""
    t = T.planks(name, w, h, 4, size_m, ((0.30, 0.15, 0.07), (0.13, 0.06, 0.03)), (0.05, 0.025, 0.012), seed, gloss=0.5, butts=1, nails=False)
    return t


def teak_deck(name, w, h, size_m, seed):
    return T.planks(name, w, h, 6, size_m, ((0.42, 0.33, 0.22), (0.24, 0.17, 0.10)), (0.05, 0.04, 0.03), seed, weather=0.45, gloss=0.2)


def build_all():
    S = {}
    S["hull"] = hull_iron("hull", 512, 512, (3.0, 3.0), 301)
    S["hull_clean"] = hull_iron("hull_clean", 512, 512, (3.0, 3.0), 303, fouling=0.15)
    S["iron_in"] = T.painted_metal("iron_in", 512, 512, (2.0, 2.0), (0.20, 0.23, 0.21), 305, rust=0.5, gloss=0.35, plates=2)
    S["walnut"] = walnut_panel("walnut", 512, 512, (1.2, 1.2), 307)
    S["teak"] = teak_deck("teak", 512, 512, (2.4, 1.2), 309)
    S["chequer"] = T.chequer_plate("chequer", 512, 512, (1.0, 1.0), 311)
    return S
