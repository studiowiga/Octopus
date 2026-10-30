# OCTOPUS (Studio Wiga) : fabrication des images de la pédale (hors plugin)
#   Repère du plugin : 1240 x 928 unités (même rapport que Ring Think), images livrées en 2x.
#   Lumière unique : en haut à gauche, élévation ~45° (comme sur toutes les pédales Studio Wiga).
#   Sorties (à la racine du dépôt) :
#     background.png : boîtier (aluminium anodisé graphite brossé), lunette de l'écran, logo, marquages (sans jacks)
#     fs_off.png / fs_on.png : footswitch d'ECHOFLEX (photo réelle), détouré, ombre portée ; fs_on = léger reflet de la lettre lumineuse
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
BOX = (14, 12, 1226, 916, 46)                   # boîtier x0 y0 x1 y1 rayon (sans jacks : il occupe toute la fenêtre)
BEZEL = (53, 90, 1187, 767, 22)                 # lunette de l'écran (presque toute la façade, comme un Quad Cortex)
GLASS = (67, 104, 1173, 753, 12)                # vitre (zone affichée par le plugin) : 1106 x 649 (écran 1064 x 624 x 1.0395)
FS_Y = 846.0                                    # remonté (place pour l'anneau lumineux autour de l'écrou)
FS_X = (202.1, 480.7, 759.3, 1037.9)            # alignés sur les 4 cases du bas de l'écran
LED_DX, LED_Y = 0.0, 787.0                      # lettre lumineuse (A-D) au-dessus du footswitch, face à un plat de l'écrou
FS_ROT = 30.0                                   # écrou tourné de 30° : un plat en haut, sous la LED
FS_FLATS = 80.0                                 # largeur entre plats de l'écrou du footswitch
RING_R0, RING_R1 = 27.4, 29.5                   # anneau lumineux (diffuseur) dans le footswitch, autour du bouton qui s'enfonce

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
MATERIAL = os.environ.get("OCTO_MATERIAL", "dark")
if MATERIAL == "dark":
    # design fourni (oct. 2026) : acier brossé anthracite, brossage fin bien visible, reflets larges très doux
    scratch = cv2.GaussianBlur(rng.standard_normal((H, W)), (0, 0), sigmaX=18, sigmaY=0.45)
    scratch = scratch / (scratch.std() + 1e-9)
    sheen = cv2.GaussianBlur(rng.standard_normal((H // 8, W // 8)), (0, 0), sigmaX=34, sigmaY=22)
    sheen = cv2.resize(sheen / (sheen.std() + 1e-9), (W, H), interpolation=cv2.INTER_CUBIC)
    base = np.array([54.0, 56.0, 61.0])
    tone = base[None, None, :] * (1 + 0.085 * brush[..., None] + 0.075 * scratch[..., None] + 0.05 * sheen[..., None]
                                  + 0.03 * fine[..., None])
elif MATERIAL == "steel":
    # essai : alu brossé acier (photo fournie) : gris acier plus clair, brossage plus marqué, rayures fines, reflets larges
    scratch = cv2.GaussianBlur(rng.standard_normal((H, W)), (0, 0), sigmaX=22, sigmaY=0.45)
    scratch = scratch / (scratch.std() + 1e-9)
    sheen = cv2.GaussianBlur(rng.standard_normal((H // 8, W // 8)), (0, 0), sigmaX=34, sigmaY=22)
    sheen = cv2.resize(sheen / (sheen.std() + 1e-9), (W, H), interpolation=cv2.INTER_CUBIC)
    base = np.array([80.0, 83.0, 90.0])
    tone = base[None, None, :] * (1 + 0.075 * brush[..., None] + 0.045 * scratch[..., None] + 0.06 * sheen[..., None]
                                  + 0.03 * fine[..., None])
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

if MATERIAL == "dark":
    # arêtes usinées claires (design fourni) : fin liseré brillant sur le pourtour du boîtier et autour du creux de l'écran,
    # plus lumineux vers la lumière (haut gauche) pour le boîtier, vers le bas pour la lèvre de l'écran
    side_box = 0.55 + 0.45 * smooth(-1, 1, -((ux - (BOX[0] + BOX[2]) / 2) / 600 + (uy - (BOX[1] + BOX[3]) / 2) / 450))
    rim = np.exp(-((d_box + 2.2) / 0.7) ** 2) * side_box
    side_bez = 0.45 + 0.55 * smooth(-1, 1, (uy - (BEZEL[1] + BEZEL[3]) / 2) / 300)
    lip = np.exp(-((d_bez - 0.9) / 0.55) ** 2) * side_bez
    lit = lit + (rim * 120 + lip * 135)[..., None] * np.array([1.0, 1.0, 1.04])
    lit = np.where(inside[..., None], lit, lit)

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

# logo OCTOPUS (pieuvre + « fx chain controller ») : en symétrie du logo Studio Wiga (même marge au bord, 66),
# centré en hauteur dans la bande entre le haut du boîtier (12) et la lunette (90), avec 12 de marge en haut et en bas
olog = Image.open(os.path.join(ROOT, "design", "logo_octopus_blanc.png")).convert("RGBA")
ohh = int(54 * AS); oww = round(olog.width * ohh / olog.height)
olog = olog.resize((oww, ohh), Image.LANCZOS)
oa = np.asarray(olog).astype(np.float64); oa[..., 3] *= 0.94
# (le logo OCTOPUS n'est plus incrusté dans le fond : le plugin le dessine lui-même à la taille exacte de l'écran, pour qu'il reste net ;
#  position : Layout.h, logoOctopus = bord droit 1174, 54 de haut, centré à 51)
_ = (olog, oa)

logo = Image.open(os.path.join(ROOT, "design", "logo_studio_wiga_blanc.png")).convert("RGBA")
lw = int(140 * AS); lh = round(logo.height * lw / logo.width)
logo = logo.resize((lw, lh), Image.LANCZOS)
la = np.asarray(logo).astype(np.float64); la[..., 3] *= 0.94
im.alpha_composite(Image.fromarray(la.astype(np.uint8), "RGBA"), (int(66 * AS), int(51 * AS) - lh // 2))     # centré en hauteur entre le haut du boîtier (12) et la lunette (90)

# (l'anneau lumineux est dans le footswitch lui-même, autour du bouton : voir footswitch())

# lettres des footswitchs : lettres rétroéclairées dessinées par le plugin au-dessus de chaque footswitch (à la place des LED)

# (plus de jacks ni d'inscriptions IN / OUT sur les tranches : le boîtier occupe toute la fenêtre)

im.save(os.path.join(ROOT, "background.png"), optimize=True)
print("background.png", im.size)

# ------------------------------------------------------------------ 5) footswitch d'ECHOFLEX (même procédé que Crycat)
EF_PTS = [(1.7, -56.7), (51.7, -26.7), (51.7, 31.7), (5.0, 58.3), (-48.3, 30.0), (-49.3, -26.7)]
EF_CENTROID = (1.5, 1.5)
EF_FLATS = 100.5
FS_K = FS_FLATS / EF_FLATS
FS_W = 150.0                                    # sprite carré (unités Octopus), ombre comprise

def rotated_source():
    """ Photo d'Echoflex avec l'écrou tourné de FS_ROT degrés (un plat en haut) sans fausser la lumière :
        - le bouton et la bague moletée (ronds) restent ceux de la photo, avec leurs reflets d'origine ;
        - seul l'hexagone est tourné, puis chaque face est rééclairée pour sa nouvelle orientation :
          luminance d'une face = a + b cos(normale - lambda), modèle ajusté sur les 6 faces de la photo. """
    src = np.asarray(Image.open(os.path.join(ROOT, "design", "echoflex_footswitch_source.png")).convert("RGB")).astype(np.float64)
    cx, cy = 150 + 2 * EF_CENTROID[0], 150 + 2 * EF_CENTROID[1]
    rot = cv2.warpAffine(src, cv2.getRotationMatrix2D((cx, cy), FS_ROT, 1.0), (src.shape[1], src.shape[0]),
                         flags=cv2.INTER_LANCZOS4, borderMode=cv2.BORDER_REFLECT)
    yy_, xx_ = np.mgrid[0:src.shape[0], 0:src.shape[1]].astype(np.float64)
    r = np.hypot(xx_ - cx, yy_ - cy)
    phi = np.degrees(np.arctan2(-(yy_ - cy), xx_ - cx))                     # angle (degrés, y vers le haut)
    lum = src.mean(2)
    # faces de la photo : sommet en haut -> normales à 0, 60, ..., 300 degrés ; mesure sur le plat (r 92..99)
    ring = (r > 92) & (r < 99)
    nrm = np.arange(6) * 60.0
    meas = []
    for k_ in range(6):
        d = (phi - nrm[k_] + 180) % 360 - 180
        meas.append(lum[ring & (np.abs(d) < 20)].mean())
    A = np.column_stack([np.ones(6), np.cos(np.radians(nrm)), np.sin(np.radians(nrm))])
    c0, c1, c2 = np.linalg.lstsq(A, np.array(meas), rcond=None)[0]
    shade = lambda ang: c0 + c1 * np.cos(np.radians(ang)) + c2 * np.sin(np.radians(ang))
    print("faces de la photo :", [round(v) for v in meas], " lumière vers", round(np.degrees(np.arctan2(c2, c1))), "degrés")
    # pixel tourné : vient de l'angle phi - FS_ROT de la photo (face d'origine) ; nouvelle face : normales à 30, 90, ...
    old_n = np.round((phi - FS_ROT) / 60.0) * 60.0
    new_n = np.round((phi - 30.0) / 60.0) * 60.0 + 30.0
    gain = np.clip(shade (new_n) / np.maximum(shade (old_n), 1.0), 0.6, 1.6)
    rot = rot * gain[..., None]
    w = np.clip((r - 90.0) / 4.0, 0, 1)[..., None]                          # bague moletée : photo d'origine
    return src * (1 - w) + rot * w

# footswitch inox (photo fournie, vue de dessus) : écrou hexagonal avec un plat en haut, bouton brossé.
# Repères mesurés sur la photo (1261 x 1247) : centre (625.5, 607), 1047 px entre les plats haut et bas.
INOX_C = (625.5, 607.0)
INOX_FLATS = 1047.0

def footswitch(led_on):
    src = np.asarray(Image.open(os.path.join(ROOT, "design", "footswitch_inox_source.png")).convert("RGB")).astype(np.float64)
    n = round(FS_W * AS)
    sc = FS_FLATS * AS / INOX_FLATS                                         # même taille d'écrou que l'ancien footswitch
    M = np.float32([[sc, 0, n / 2 - INOX_C[0] * sc], [0, sc, n / 2 - INOX_C[1] * sc]])
    rgb = cv2.warpAffine(src, M, (n, n), flags=cv2.INTER_AREA, borderMode=cv2.BORDER_REPLICATE)
    # silhouette : hexagone régulier (plats en haut et en bas, sommets à gauche et à droite), bord légèrement rentré
    # anneau diffuseur éteint (verre dépoli gris) dans la gorge entre le bouton et la bague, comme la Phase 90
    yy_, xx_ = np.mgrid[0:n, 0:n].astype(np.float64)
    du = np.hypot(xx_ - n / 2, yy_ - n / 2) / AS
    gl = smooth(RING_R0 - 0.5, RING_R0 + 0.3, du) * (1 - smooth(RING_R1 - 0.3, RING_R1 + 0.5, du))
    lit_ = 0.5 - 0.5 * np.sin(np.arctan2(yy_ - n / 2, xx_ - n / 2) + np.radians(45))
    diff = 70 + 30 * lit_
    rgb = rgb * (1 - 0.8 * gl[..., None]) + np.dstack([diff, diff * 1.01, diff * 1.05]) * (0.8 * gl[..., None])
    fl = FS_FLATS * AS * 0.5 * 0.985; rv = fl * 2.0 / np.sqrt(3.0)
    pts = np.array([[n / 2 + rv * np.cos(np.radians(k * 60)), n / 2 - rv * np.sin(np.radians(k * 60))] for k in range(6)])
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
