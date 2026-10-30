# Acier brossé réaliste (Octopus) : lignes de brossage multi-échelles sur toute la largeur, rares rayures,
# reflet anisotrope en larges bandes verticales (le métal brossé horizontalement étire la lumière perpendiculairement).
import numpy as np, cv2
def streaks(H, W, rng, cells, ysig):
    """ lignes de brossage : un bruit par ligne, prolongé sur toute la largeur (cellules interpolées) """
    n = rng.standard_normal((H, cells + 1))
    s = cv2.resize(n, (W, H), interpolation=cv2.INTER_CUBIC)
    if ysig > 0: s = cv2.GaussianBlur(s, (0, 0), sigmaX=0.01, sigmaY=ysig)
    return s / (s.std() + 1e-9)
def brushed(H, W, seed=7, base=(56, 58, 63), px_per_unit=2.0, light_x=0.32):
    rng = np.random.default_rng(seed)
    # brossage à plusieurs échelles : longues lignes (toute la largeur), moyennes, fines et quelques rayures
    long_ = streaks(H, W, rng, 6, 0.6)
    mid = streaks(H, W, rng, 40, 0.5)
    fine = streaks(H, W, rng, 220, 0.4)
    scr = rng.standard_normal((H, W)); scr = cv2.GaussianBlur(scr, (0, 0), sigmaX=40, sigmaY=0.35); scr /= scr.std()
    scr = np.clip(scr - 2.2, 0, None) * 1.0                                     # rares rayures plus claires
    tex = 0.55 * long_ + 0.45 * mid + 0.35 * fine
    tex = tex / tex.std()
    # reflet anisotrope : le métal brossé horizontalement renvoie la lumière en larges bandes verticales douces
    xx = np.linspace(0, 1, W)[None, :]; yy = np.linspace(0, 1, H)[:, None]
    sheen = 0.55 * np.exp(-((xx - light_x) / 0.22) ** 2) + 0.25 * np.exp(-((xx - 0.78) / 0.12) ** 2) - 0.15
    sheen = sheen * (1.0 - 0.25 * yy)                                           # un peu moins de lumière vers le bas
    b = np.array(base, dtype=np.float64)[None, None, :]
    k = 1 + 0.30 * sheen + (0.06 + 0.08 * np.clip(sheen, 0, None)) * tex + 0.10 * scr
    return np.clip(b * k[..., None], 0, 255), k
if __name__ == "__main__":
    img, _ = brushed(400, 1200)
    cv2.imwrite("patch.png", img[..., ::-1].astype(np.uint8))
