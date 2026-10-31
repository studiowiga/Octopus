# Aluminium brossé réaliste (Octopus), généré à la résolution finale.
# Ce qui fait le réalisme d'un brossage :
#  - des stries de longueurs très variées (quelques px à toute la largeur), chacune avec sa propre intensité,
#    qui commencent et finissent en fondu (pas de lignes infinies ni de motif répété) ;
#  - une poignée de rayures plus fines et plus claires, de longueur finie ;
#  - un grain très fin ;
#  - une lumière anisotrope : le métal brossé horizontalement étire les reflets verticalement
#    (larges bandes verticales douces, sans taches) ;
#  - un ton légèrement froid, contraste du brossage plus fort là où la lumière frappe.
import numpy as np, cv2

def _streak_layer(H, W, rng, length, ywidth):
    n = rng.standard_normal((H, W)).astype(np.float32)
    k = max(3, int(length * 3) | 1)
    s = cv2.GaussianBlur(n, (k, 1), sigmaX=length, sigmaY=0)
    if ywidth > 0: s = cv2.GaussianBlur(s, (1, 0), sigmaX=0.01, sigmaY=ywidth)
    return s / (s.std() + 1e-6)

def _scratches(H, W, rng, count, px):
    img = np.zeros((H, W), np.float32)
    for _ in range(count):
        y = rng.uniform(0, H); x0 = rng.uniform(-200, W); L = rng.uniform(40, 700) * px / 2
        a = rng.uniform(0.25, 1.0) * (1 if rng.random() < 0.8 else -0.7)
        xs = np.arange(int(max(0, x0)), int(min(W, x0 + L)))
        if len(xs) < 2: continue
        t = (xs - x0) / L
        prof = a * np.sin(np.pi * np.clip(t, 0, 1)) ** 0.7
        yi = int(y + rng.uniform(-0.3, 0.3))
        if 0 <= yi < H: img[yi, xs] += prof.astype(np.float32)
    return cv2.GaussianBlur(img, (0, 0), sigmaX=0.6, sigmaY=0.45)

def brushed_alu(H, W, px=2.0, seed=3, level=58.0, tint=(0.97, 1.0, 1.06), light=None):
    """ H x W pixels, px pixels par unité ; renvoie une image RGB float (0..255) """
    rng = np.random.default_rng(seed)
    tex = (0.40 * _streak_layer(H, W, rng, 5 * px, 0.4) +
           0.34 * _streak_layer(H, W, rng, 25 * px, 0.5) +
           0.30 * _streak_layer(H, W, rng, 90 * px, 0.55) +
           0.22 * _streak_layer(H, W, rng, 300 * px, 0.7))
    tex /= tex.std()
    grain = cv2.GaussianBlur(rng.standard_normal((H, W)).astype(np.float32), (0, 0), 0.5)
    scr = _scratches(H, W, rng, int(H * W / 2600), px)
    xx = np.linspace(0, 1, W, dtype=np.float32)[None, :]; yy = np.linspace(0, 1, H, dtype=np.float32)[:, None]
    if light is None:
        # reflets anisotropes : bandes verticales larges et douces + légère baisse vers le bas et la droite
        light = (1.0 + 0.16 * np.exp(-((xx - 0.30) / 0.20) ** 2) + 0.07 * np.exp(-((xx - 0.74) / 0.09) ** 2)
                 - 0.06 * np.exp(-((xx - 0.55) / 0.10) ** 2) - 0.10 * yy - 0.05 * xx)
    contrast = 0.10 * light
    k = light * (1 + contrast * tex + 0.018 * grain) + 0.22 * scr * light
    rgb = level * k[..., None] * np.array(tint, np.float32)[None, None, :]
    return np.clip(rgb, 0, 255)

if __name__ == "__main__":
    im = brushed_alu(500, 1600)
    cv2.imwrite("/tmp/alu.png", im[..., ::-1].astype(np.uint8))
