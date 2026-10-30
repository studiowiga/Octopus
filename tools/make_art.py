# OCTOPUS (Studio Wiga) : fabrication des images de la pédale (hors plugin)
#   Repère du plugin : 1240 x 928 unités (même rapport que Ring Think), images livrées en 2x.
#   Lumière unique : en haut à gauche, élévation ~45° (comme sur toutes les pédales Studio Wiga).
#   Sorties (à la racine du dépôt) :
#     background.png : boîtier (aluminium anodisé graphite brossé), lunette de l'écran, logo, marquages, jacks IN / OUT
#     fs_off.png / fs_on.png : footswitch d'ECHOFLEX (photo réelle), détouré, ombre portée ; fs_on = léger reflet de la LED
#   Les positions sont reprises telles quelles dans Layout.h (même nom, mêmes valeurs).
import os, math
import numpy as np, cv2
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W_U, H_U = 1240, 928
AS = 2                                          # pixels par unité (image 2x)
W, H = W_U * AS, H_U * AS
rng = np.random.default_rng(7)

L = np.array([-0.46, -0.62, 0.64]); L /= np.linalg.norm(L)        # vers la lumière (x droite, y bas, z vers l'œil)
Hv = (L + np.array([0, 0, 1.0])); Hv /= np.linalg.norm(Hv)

# ------------------------------------------------------------------ positions (unités) : identiques à Layout.h
BOX = (46, 24, 1194, 904, 46)                   # boîtier x0 y0 x1 y1 rayon
BEZEL = (74, 98, 1166, 750, 22)                 # lunette de l'écran (presque toute la façade, comme un Quad Cortex)
GLASS = (88, 112, 1152, 736, 12)                # vitre (zone affichée par le plugin) : 1064 x 624
FS_Y = 832.0
FS_X = (218.0, 486.0, 754.0, 1022.0)            # alignés sur les 4 cases du bas de l'écran
LED_DX, LED_Y = -92.0, 814.0                    # LED à gauche du footswitch, lettre dessous
LETTER_Y = 852.0
FS_FLATS = 80.0                                 # largeur entre plats de l'écrou du footswitch
JACK_Y = 424.0

def sd_round_rect(x, y, r):
    """ distance signée (unités) à un rectangle arrondi (négative à l'intérieur) """
    x0, y0, x1, y1, rad = r
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - rad, (y1 - y0) / 2 - rad
    qx, qy = np.abs(x - cx) - hx, np.abs(y - cy) - hy
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - rad

def smooth(e0, e1, v):
    t = np.clip((v - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)

yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
ux, uy = (xx + 0.5) / AS, (yy + 0.5) / AS

# ------------------------------------------------------------------ 1) hauteur : boîtier biseauté + lunette en creux
d_box = sd_round_rect(ux, uy, BOX)
d_bez = sd_round_rect(ux, uy, BEZEL)
d_gl = sd_round_rect(ux, uy, GLASS)
inside = d_box < 0
hgt = 10.0 * smooth(0, 9, -d_box)                                   # biseau de 9 unités sur le pourtour
hgt -= 5.0 * smooth(-1.5, 3.0, -d_bez)                              # chanfrein de la lunette (vers l'intérieur)
hgt -= 3.0 * smooth(-0.5, 2.0, -d_gl)                               # la vitre est encore un peu plus bas
gy, gx = np.gradient(cv2.GaussianBlur(hgt, (0, 0), 0.9 * AS), 1.0 / AS)
n = np.dstack([-gx, -gy, np.ones_like(hgt)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
ndl = np.clip((n * L).sum(2), 0, 1)
spec = np.clip((n * Hv).sum(2), 0, 1) ** 38

# ------------------------------------------------------------------ 2) matière : aluminium anodisé graphite, brossé horizontalement
noise = rng.standard_normal((H, W))
brush = cv2.GaussianBlur(noise, (0, 0), sigmaX=60, sigmaY=0.7)
brush = brush / (brush.std() + 1e-9)
fine = cv2.GaussianBlur(rng.standard_normal((H, W)), (0, 0), 0.8)
base = np.array([46.0, 48.0, 56.0])                                 # graphite légèrement bleuté
tone = base[None, None, :] * (1 + 0.055 * brush[..., None] + 0.025 * fine[..., None])
# éclairage : ambiance + diffus + reflet (anodisé : reflet un peu bleuté)
lit = tone * (0.55 + 0.62 * ndl[..., None]) + spec[..., None] * np.array([150, 158, 175])
# léger dégradé d'éclairage de la façade (plus clair en haut à gauche)
grad = 1.06 - 0.12 * ((ux - BOX[0]) / (BOX[2] - BOX[0]) * 0.55 + (uy - BOX[1]) / (BOX[3] - BOX[1]) * 0.45)
lit *= grad[..., None]

# lunette : noir mat légèrement granuleux, vitre presque noire (le plugin dessine l'écran dessus)
bez = (d_bez < 0) & (d_gl >= 0)
bez_col = np.array([20.0, 21.0, 25.0]) * (0.75 + 0.5 * ndl[..., None]) + spec[..., None] * np.array([60, 62, 70])
lit = np.where(bez[..., None], bez_col, lit)
glass = d_gl < 0
lit = np.where(glass[..., None], np.array([8.0, 9.0, 11.0]), lit)
# ombre portée de la lèvre supérieure / gauche de la lunette sur la vitre
shadow_in = np.clip(1 - smooth(0, 10, -(sd_round_rect(ux - 3.0, uy - 4.0, GLASS))), 0, 1) * glass
lit = lit * (1 - 0.55 * shadow_in[..., None])

alpha = np.clip(0.5 - d_box * AS, 0, 1)                              # bord net antialiasé
# ombre du boîtier sur le fond (le fond du plugin est noir : ombre douce vers le bas à droite)
rgb = lit

img = np.dstack([np.clip(rgb, 0, 255), alpha * 255]).astype(np.uint8)
im = Image.fromarray(img, "RGBA")

# ------------------------------------------------------------------ 3) marquages (sérigraphie)
draw = ImageDraw.Draw(im)
fonts = os.path.join(ROOT, "fonts")
f_word = ImageFont.truetype(os.path.join(fonts, "Poppins-Bold.ttf"), int(34 * AS))
f_small = ImageFont.truetype(os.path.join(fonts, "Poppins-Medium.ttf"), int(11 * AS))
f_side = ImageFont.truetype(os.path.join(fonts, "Poppins-Medium.ttf"), int(12 * AS))
f_letter = ImageFont.truetype(os.path.join(fonts, "Poppins-Bold.ttf"), int(22 * AS))
ink = (226, 229, 236, 255)
ink_dim = (150, 156, 170, 255)

def spaced(text, font, x_right, y_top, track, fill):
    widths = [draw.textlength(c, font=font) for c in text]
    total = sum(widths) + track * (len(text) - 1)
    x = x_right - total
    for c, w_ in zip(text, widths):
        draw.text((x, y_top), c, font=font, fill=fill)
        x += w_ + track
    return total

spaced("OCTOPUS", f_word, 1150 * AS, 30 * AS, 6 * AS, ink)
spaced("FX CHAIN CONTROLLER", f_small, 1150 * AS, 73 * AS, 3.0 * AS, ink_dim)

logo = Image.open(os.path.join(ROOT, "design", "logo_studio_wiga_blanc.png")).convert("RGBA")
lw = int(232 * AS); lh = round(logo.height * lw / logo.width)
logo = logo.resize((lw, lh), Image.LANCZOS)
la = np.asarray(logo).astype(np.float64); la[..., 3] *= 0.94
im.alpha_composite(Image.fromarray(la.astype(np.uint8), "RGBA"), (int(90 * AS), int(58 * AS) - lh // 2))     # centré à y = 58

# lettres des footswitchs : sous la LED, à gauche du footswitch
for i, x in enumerate(FS_X):
    tw = draw.textlength("ABCD"[i], font=f_letter)
    draw.text(((x + LED_DX) * AS - tw / 2, (LETTER_Y - 15) * AS), "ABCD"[i], font=f_letter, fill=ink)

# IN / OUT centrés à la hauteur des jacks (OUT à gauche, IN à droite), écrits le long de la tranche
for txt, xc, ang in (("OUT", 60.0, 90), ("IN", 1180.0, -90)):
    tw = int(draw.textlength(txt, font=f_side)) + 8
    th = int(18 * AS)
    lab = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    ImageDraw.Draw(lab).text((4, 0), txt, font=f_side, fill=ink_dim)
    lab = lab.rotate(ang, expand=True, resample=Image.BICUBIC)
    im.alpha_composite(lab, (int(xc * AS - lab.width / 2), int(JACK_Y * AS - lab.height / 2)))

# ------------------------------------------------------------------ 4) jacks du kit Studio Wiga (OUT tranche gauche, IN tranche droite)
for fname, x_u in (("embase_jack_gauche_OUT.png", BOX[0] - 22), ("embase_jack_droite_IN.png", BOX[2])):
    j = Image.open(os.path.join(ROOT, "design", fname)).convert("RGBA")
    jw, jh = int(22 * AS), int(120 * AS)
    j = j.resize((jw, jh), Image.LANCZOS)
    im.alpha_composite(j, (int(x_u * AS), int((JACK_Y - 60) * AS)))

im.save(os.path.join(ROOT, "background.png"), optimize=True)
print("background.png", im.size)

# ------------------------------------------------------------------ 5) footswitch d'ECHOFLEX (même procédé que Crycat)
EF_PTS = [(1.7, -56.7), (51.7, -26.7), (51.7, 31.7), (5.0, 58.3), (-48.3, 30.0), (-49.3, -26.7)]
EF_CENTROID = (1.5, 1.5)
EF_FLATS = 100.5
FS_K = FS_FLATS / EF_FLATS
FS_W = 150.0                                    # sprite carré (unités Octopus), ombre comprise

def footswitch(led_on):
    src = np.asarray(Image.open(os.path.join(ROOT, "design", "echoflex_footswitch_source.png")).convert("RGB")).astype(np.float64)
    n = round(FS_W * AS)
    sc = FS_K * AS / 2.0
    M = np.float32([[sc, 0, n / 2 - (150 + 2 * EF_CENTROID[0]) * sc], [0, sc, n / 2 - (150 + 2 * EF_CENTROID[1]) * sc]])
    rgb = cv2.warpAffine(src, M, (n, n), flags=cv2.INTER_LANCZOS4, borderMode=cv2.BORDER_REFLECT)
    pts = np.array([[n / 2 + (x - EF_CENTROID[0]) * FS_K * AS * 0.992, n / 2 + (y - EF_CENTROID[1]) * FS_K * AS * 0.992] for x, y in EF_PTS])
    m = np.zeros((n * 4, n * 4), np.uint8); cv2.fillPoly(m, [np.round(pts * 4).astype(np.int32)], 255)
    a = cv2.resize(m, (n, n), interpolation=cv2.INTER_AREA).astype(np.float64) / 255.0
    a = cv2.GaussianBlur(a, (0, 0), 0.6)
    if led_on:
        # la LED est au-dessus du footswitch : léger reflet clair sur les flancs tournés vers elle (couleur neutre,
        # la couleur de la LED change selon le bloc assigné)
        yy_, xx_ = np.mgrid[0:n, 0:n].astype(np.float64)
        uy_ = -(yy_ - n / 2) / (AS * FS_K * 50.0)
        w = np.clip(uy_, 0, 1) ** 2 * (rgb.mean(2) / 255.0)
        rgb = rgb + (w * 46.0)[..., None] * np.array([1.0, 0.97, 0.92])
    def shift(img_, dx, dy): return cv2.warpAffine(img_, np.float32([[1, 0, dx], [0, 1, dy]]), (n, n))
    cast = cv2.GaussianBlur(shift(a, 4.5 * AS, 6.5 * AS), (0, 0), 4.5 * AS) * 0.55
    contact = cv2.GaussianBlur(shift(a, 1.0 * AS, 1.5 * AS), (0, 0), 1.6 * AS) * 0.45
    sh = np.clip(cast + contact, 0, 0.8) * (1 - a)
    al = np.clip(a + sh, 0, 1)
    col = np.where(al[..., None] > 1e-4, rgb * (a / np.maximum(al, 1e-4))[..., None], 0)
    out = np.dstack([np.clip(col, 0, 255), al * 255]).astype(np.uint8)
    name = "fs_on.png" if led_on else "fs_off.png"
    Image.fromarray(out, "RGBA").save(os.path.join(ROOT, name))
    print(name, out.shape[:2])

for l_ in (False, True):
    footswitch(l_)
