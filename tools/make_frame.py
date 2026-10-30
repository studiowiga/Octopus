# OCTOPUS (Studio Wiga) : fond de la pédale à partir du visuel fourni (design/frame_photo.jpg, 1415 x 981).
# 1. les marquages de la photo sont effacés (logos, lettres A-D) en recopiant le brossage horizontal voisin ;
#    la vitre est vidée (le plugin y dessine son écran) ;
# 2. 148 colonnes sont retirées en 5 bandes verticales (bords, entre les footswitchs) pour que l'ouverture de l'écran
#    ait exactement les proportions de l'écran du plugin (1064 x 624) : le brossage horizontal rend les raccords invisibles ;
# 3. image en 2x, boîtier détouré (coins arrondis), logo Studio Wiga net (le logo OCTOPUS est dessiné par le plugin).
# Repère obtenu (unités du plugin = pixels de l'image recadrée) : 1267 x 981. Valeurs reprises dans Layout.h.
import os
import numpy as np, cv2
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AS = 2
src = np.asarray(Image.open(os.path.join(ROOT, "design", "frame_photo.jpg")).convert("RGB")).astype(np.float64)
H, W = src.shape[:2]

# redressement : la photo est prise un peu en plongée, la pédale y est plus large en bas qu'en haut. Les arêtes claires
# des côtés (gauche x 28 -> 17, droite x 1387 -> 1399 entre y 40 et y 900) deviennent verticales : chaque ligne est
# remise à la largeur du milieu de la pédale, autour de son axe.
def edges(y):
    return 28.5 - 0.0107 * (y - 40.0), 1387.0 + 0.01325 * (y - 40.0)
_l0, _r0 = edges(470.0); C0, W0 = (_l0 + _r0) / 2, _r0 - _l0
_yy, _xx = np.mgrid[0:H, 0:W].astype(np.float32)
_l, _r = edges(_yy)
map_x = ((_l + _r) / 2 + (_xx - C0) * (_r - _l) / W0).astype(np.float32)
src = cv2.remap(src.astype(np.float32), map_x, _yy, cv2.INTER_LANCZOS4, borderMode=cv2.BORDER_REPLICATE).astype(np.float64)
if os.environ.get("OCTO_MEASURE"):
    cv2.imwrite("/tmp/frame_rectified.png", np.clip(src[..., ::-1], 0, 255).astype(np.uint8)); raise SystemExit

def smooth(e0, e1, v):
    t = np.clip((v - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)

def patch_rows(img, x0, y0, x1, y1, dx, feather=4, detail=1.0):
    """ efface un marquage : chaque ligne reçoit le brossage de la même ligne décalée de dx (bords fondus) """
    yy, xx = np.mgrid[y0 - feather:y1 + feather, x0 - feather:x1 + feather]
    d = np.minimum.reduce([xx - (x0 - feather), (x1 + feather - 1) - xx, yy - (y0 - feather), (y1 + feather - 1) - yy])
    w = smooth(0, feather, d.astype(np.float64))[..., None]
    reg = img[y0 - feather:y1 + feather, x0 - feather:x1 + feather]
    srcp = img[y0 - feather:y1 + feather, x0 - feather + dx:x1 + feather + dx].copy()
    # même niveau de lumière que les bords de la zone, ligne par ligne (le reflet du métal varie le long de la façade)
    rows = slice(y0 - feather, y1 + feather)
    lev_l = img[rows, x0 - feather - 10:x0 - feather].mean(axis=1)
    lev_r = img[rows, x1 + feather:x1 + feather + 10].mean(axis=1)
    lev_l = cv2.GaussianBlur(lev_l, (1, 0), sigmaX=0.1, sigmaY=3).reshape(lev_l.shape) if lev_l.ndim == 2 else lev_l
    lev_r = cv2.GaussianBlur(lev_r, (1, 0), sigmaX=0.1, sigmaY=3).reshape(lev_r.shape) if lev_r.ndim == 2 else lev_r
    tx = np.linspace(0, 1, srcp.shape[1])[None, :, None]
    target = lev_l[:, None, :] * (1 - tx) + lev_r[:, None, :] * tx
    smean = cv2.GaussianBlur(srcp, (0, 0), sigmaX=40, sigmaY=3)
    srcp = (srcp - smean) * detail + target
    img[y0 - feather:y1 + feather, x0 - feather:x1 + feather] = reg * (1 - w) + srcp * w

img = src.copy()
patch_rows(img, 59, 40, 241, 68, 260)                          # logo Studio Wiga
patch_rows(img, 1103, 16, 1356, 90, -258, feather=14, detail=0.8)   # logo OCTOPUS
for cx in (226.5, 543.0, 864.5, 1183.0):                      # lettres A-D gravées (le plugin les dessine, allumées ou non)
    patch_rows(img, int(cx - 15), 792, int(cx + 15), 822, 70)

# vitre : vidée (presque noire), coins arrondis comme l'ouverture de la photo
GX0, GY0, GX1, GY1, GR = 71.0, 108.0, 1345.0, 769.0, 14.0
yy, xx = np.mgrid[0:H, 0:W].astype(np.float64) + 0.5
def sd_rr(x, y, x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    qx, qy = np.abs(x - cx) - ((x1 - x0) / 2 - r), np.abs(y - cy) - ((y1 - y0) / 2 - r)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r
g = np.clip(0.5 - sd_rr(xx, yy, GX0, GY0, GX1, GY1, GR), 0, 1)[..., None]
img = img * (1 - g) + np.array([7.0, 8.0, 10.0]) * g

# bandes retirées (de droite à gauche pour garder les positions) : [début, fin) en pixels de la photo
CUTS = [(101, 119), (364, 401), (686, 724), (1010, 1047), (1290, 1308)]
BLEND = 8
for a, b in sorted(CUTS, reverse=True):
    t = np.linspace(0, 1, BLEND)[None, :, None]
    mix = img[:, a - BLEND:a] * (1 - t) + img[:, b - BLEND:b] * t
    img = np.concatenate([img[:, :a - BLEND], mix, img[:, b:]], axis=1)
removed = sum(b - a for a, b in CUTS)
W2 = W - removed
assert img.shape[1] == W2, img.shape

# 2x, détourage du boîtier (photo redressée : boîtier x 12..1403, y 3..968, coins de rayon 42)
big = cv2.resize(img, (W2 * AS, H * AS), interpolation=cv2.INTER_LANCZOS4)
yy, xx = (np.mgrid[0:H * AS, 0:W2 * AS].astype(np.float64) + 0.5) / AS
alpha = np.clip(0.5 - sd_rr(xx, yy, 12.0, 3.0, 1403.0 - removed, 968.0, 42.0) * AS, 0, 1)
out = Image.fromarray(np.dstack([np.clip(big, 0, 255), alpha * 255]).astype(np.uint8), "RGBA")

# logo Studio Wiga net, à la place de celui de la photo (x 64..235, centré à y 53.5)
logo = Image.open(os.path.join(ROOT, "design", "logo_studio_wiga_blanc.png")).convert("RGBA")
lw = int(171 * AS); lh = round(logo.height * lw / logo.width)
logo = logo.resize((lw, lh), Image.LANCZOS)
la = np.asarray(logo).astype(np.float64); la[..., 3] *= 0.95
out.alpha_composite(Image.fromarray(la.astype(np.uint8), "RGBA"), (int(64 * AS), int(53.5 * AS) - lh // 2))

out.save(os.path.join(ROOT, "background.png"), optimize=True)
print("background.png", out.size, "repère", W2, "x", H)
