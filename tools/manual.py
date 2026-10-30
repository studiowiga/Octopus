# OCTOPUS (Studio Wiga) - notice PDF bilingue (anglais puis français), illustrée avec le rendu réel du plugin.
# Usage : python3 tools/manual.py <dossier des captures (design/renders)> <fichier PDF de sortie>
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, NextPageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, KeepTogether)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = sys.argv[1]
OUT = sys.argv[2]
VERSION = "1.1.2"
TMP = os.path.join(os.path.dirname(os.path.abspath(OUT)), "manual_tmp")
os.makedirs(TMP, exist_ok=True)

for n, f in (("Pop", "Poppins-Regular"), ("PopL", "Poppins-Light"), ("PopM", "Poppins-Medium"), ("PopB", "Poppins-Bold")):
    pdfmetrics.registerFont(TTFont(n, os.path.join(ROOT, "fonts", f + ".ttf")))

ACC = colors.HexColor("#1E9BD0"); DARK = colors.HexColor("#1A1C22"); GREY = colors.HexColor("#646B76"); LIGHT = colors.HexColor("#EAF4FA")

# ---------- capture annotée (repère 1240 x 928 du plugin) ----------
src = Image.open(os.path.join(SHOTS, "octopus_100pc.png")).convert("RGB")
k = src.height / 928.0
marks = [(1, 136, 97), (2, 275, 97), (3, 698, 97), (4, 892, 97), (5, 1034, 97), (6, 1120, 97),
         (7, 175, 196), (8, 612, 353), (9, 1138, 376), (10, 202, 674), (11, 480.7, 858), (12, 440, 792)]
ann = src.copy(); dr = ImageDraw.Draw(ann)
fnt = ImageFont.truetype(os.path.join(ROOT, "fonts", "Poppins-Bold.ttf"), 26)
for n, x, y in marks:
    cx, cy, r = x * k, y * k, 20
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(30, 155, 208), width=5)
    dr.text((cx, cy + 1), str(n), font=fnt, fill=(10, 80, 120), anchor="mm")
annotated = os.path.join(TMP, "annotated.png"); ann.save(annotated)

# gros plans de l'écran (vitre : 88, 112, 1064 x 624)
def screen_crop(name, out):
    im = Image.open(os.path.join(SHOTS, name)).convert("RGB")
    kk = im.height / 928.0
    im.crop((int(67 * kk), int(104 * kk), int(1173 * kk), int(753 * kk))).save(os.path.join(TMP, out))
    return os.path.join(TMP, out)
scr_stomp = screen_crop ("octopus_stomp.png", "stomp.png")
scr_add = screen_crop ("octopus_add_effect.png", "add.png")
scr_params = screen_crop ("octopus_params.png", "params.png")
scr_banks = screen_crop ("octopus_banks.png", "banks.png")
scr_edit = screen_crop ("octopus_edited_hover.png", "edited.png")
scr_presets = screen_crop ("octopus_presets_user.png", "presets.png")
scr_tmono = screen_crop ("octopus_tuner_mono.png", "tuner_mono.png")
scr_tpoly = screen_crop ("octopus_tuner_tuning.png", "tuner_poly.png")

# couverture : pédale détourée (alpha de background.png : boîtier + jacks) sur fond noir, reflet au sol, halo bleu très doux
shot = Image.open(os.path.join(SHOTS, "octopus_100pc.png")).convert("RGB")
msk = Image.open(os.path.join(ROOT, "background.png")).split()[3].resize(shot.size, Image.LANCZOS)
cut = Image.new("RGBA", shot.size, (0, 0, 0, 0)); cut.paste(shot, (0, 0), msk)
cut = cut.crop(cut.getbbox())
cw, ch = cut.size
pad = int(cw * 0.10)
canvas = Image.new("RGBA", (cw + 2 * pad, int(ch * 1.30) + pad), (0, 0, 0, 255))
yy, xx = np.mgrid[0:canvas.height, 0:canvas.width].astype(np.float64)
gcx, gcy = canvas.width / 2, pad + ch * 0.45
glow = np.exp(-(((xx - gcx) / (cw * 0.48)) ** 2 + ((yy - gcy) / (ch * 0.42)) ** 2)) * 38
edge = np.clip(np.minimum.reduce([xx, canvas.width - 1 - xx, yy, canvas.height - 1 - yy]) / (pad * 0.9), 0, 1)
glow *= edge ** 2
halo = np.dstack([glow * 0.18, glow * 0.62, glow * 1.0, np.full_like(glow, 255)]).astype(np.uint8)
canvas = Image.alpha_composite(canvas, Image.fromarray(halo, "RGBA"))
refl = cut.transpose(Image.FLIP_TOP_BOTTOM)
ra = np.array(refl).astype(np.float64)
fade = np.clip(1 - np.arange(refl.height) / (refl.height * 0.22), 0, 1) ** 1.6 * 0.22
ra[..., 3] *= fade[:, None]
canvas.alpha_composite(Image.fromarray(ra.astype(np.uint8), "RGBA"), (pad, pad + ch + 6))
canvas.alpha_composite(cut, (pad, pad))
canvas = canvas.crop((0, 0, canvas.width, pad + int(ch * 1.20)))
cover = os.path.join(TMP, "cover_cutout.png"); canvas.convert("RGB").save(cover)

def img(path, width):
    im = Image.open(path); h = width * im.height / im.width
    return RLImage(path, width=width, height=h)

# ---------- styles ----------
st = {
    "h1": ParagraphStyle("h1", fontName="PopB", fontSize=20, leading=24, textColor=ACC, spaceAfter=8, spaceBefore=4, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="PopM", fontSize=13, leading=17, textColor=DARK, spaceAfter=4, spaceBefore=10, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="Pop", fontSize=9.6, leading=14, textColor=DARK, spaceAfter=5),
    "small": ParagraphStyle("small", fontName="Pop", fontSize=8.2, leading=11.5, textColor=GREY),
    "cell": ParagraphStyle("cell", fontName="Pop", fontSize=8.4, leading=11, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="PopM", fontSize=8.4, leading=11, textColor=DARK),
    "cover": ParagraphStyle("cover", fontName="PopL", fontSize=40, leading=46, textColor=colors.white, alignment=TA_CENTER),
    "coversub": ParagraphStyle("coversub", fontName="Pop", fontSize=12, leading=16, textColor=colors.HexColor("#B4BCC6"), alignment=TA_CENTER),
    "coverlang": ParagraphStyle("coverlang", fontName="PopM", fontSize=8.5, leading=11, textColor=colors.HexColor("#4FC3F7")),
}
def P(t, s="p"): return Paragraph(t, st[s])
def bullets(items): return [P("&bull;&nbsp;&nbsp;" + i) for i in items]
def table(rows, widths, header=True):
    data = [[P(c, "cellb" if (header and r == 0) else "cell") for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#D5DDE5")),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACC)]
    t.setStyle(TableStyle(style)); return t

# ---------- textes (conformes au code livré : PluginEditor.cpp, OctopusEngine.cpp, PluginProcessor.cpp) ----------
T = {
"en": dict(
 lang="ENGLISH", sub="FX chain controller for REAPER &nbsp;·&nbsp; VST3 for Windows and macOS", ver="User manual &nbsp;·&nbsp; version " + VERSION,
 intro_t="1. Introduction",
 intro=["<b>OCTOPUS</b> is not an effect: it is a <b>control screen for the FX chain of a REAPER track</b>, laid out like the screen of a modern floor modeler (in the spirit of the Quad Cortex mini). Insert it on your guitar track and its screen shows every effect of that track as a coloured block, from IN to OUT.",
        "Whatever you do on the Octopus screen happens <b>at the same time in the track's FX chain</b>: switching a block on or off, adding an effect, removing it, moving it, changing its parameters. And whatever you do directly in REAPER appears on the Octopus screen a moment later.",
        "On top of that, Octopus stores <b>16 banks of 4 presets</b> (1A to 16D) - each preset is a complete chain with its plug-ins, their order and all their settings - and gives you <b>4 footswitches</b> to recall presets (PRESET mode) or to switch groups of blocks (STOMP mode), with the mouse or a MIDI foot controller such as the M-Vave Chocolate Plus. When the CHROMA tuner block is switched on, Octopus shows CHROMA's screen and becomes the tuner.",
        "The sound passes through Octopus untouched (zero latency). Octopus works <b>in REAPER only</b>: REAPER gives VST3 plug-ins access to their track and to its API, which other DAWs do not. In another DAW the screen simply says so."],
 inst_t="2. Installation",
 inst=[("Windows", "Copy <b>Octopus.vst3</b> (the whole folder) into <b>C:\\Program Files\\Common Files\\VST3</b>."),
       ("macOS", "Copy <b>Octopus.vst3</b> into <b>/Library/Audio/Plug-Ins/VST3</b> (or ~/Library/Audio/Plug-Ins/VST3). The build is signed ad hoc: if macOS blocks it, open System Settings &gt; Privacy &amp; Security and allow it, or run <i>xattr -cr</i> on the file."),
       ("REAPER", "Options &gt; Preferences &gt; Plug-ins &gt; VST &gt; <b>Re-scan</b>. Octopus appears as <b>VST3: Octopus (Studio Wiga)</b>. Insert it on the track you want to control, ideally in first position. REAPER 6.37 or newer is needed; the list of installed plug-ins used by the ADD screen needs REAPER 7.")],
 ov_t="3. Overview",
 ov_rows=[["#", "Element", "Role"],
   ["1", "Preset", "Preset currently loaded (1A ... 16D), or -- for a live chain."],
   ["2", "Name / status line", "Preset name (EDITED when the chain no longer matches it); below: the track name, or the last action and messages."],
   ["3", "BANK", "Bank shown by the footswitches (arrows: previous / next bank)."],
   ["4", "PRESET / STOMP", "Mode of the footswitches."],
   ["5", "BANKS", "Opens the banks panel (load, save, rename, copy, export, footswitch colours...)."],
   ["6", "SAVE", "Save the current chain in a preset."],
   ["7", "Block", "One effect of the track. Click: ON / OFF. Double-click: parameters. Drag: move. Right-click: menu."],
   ["8", "+ Add", "Adds an effect (list of the plug-ins installed in REAPER)."],
   ["9", "IN / OUT", "Signal flow of the track, from left to right, row after row."],
   ["10", "Footswitch cells", "What each footswitch does in the current mode; a click works like the footswitch."],
   ["11", "Footswitches A-D", "Recall presets A-D of the bank shown (PRESET) or switch their blocks (STOMP)."],
   ["12", "Letter A-D", "Backlit letter of the footswitch: lit for the loaded preset (in the colour chosen for that footswitch, blue by default) or for blocks that are ON (colour of the block)."],
   ],
 ctl_t="4. The screen in detail",
 ctl=[("Blocks", "Each effect of the track is a block whose colour and icon show its family, guessed from the plug-in name: tuner, amp, cab / IR, drive, dynamics, EQ, wah / filter, delay, reverb, modulation, pitch, special, utility. A lit block with a coloured corner dot is ON; a grey block is bypassed. Small marks show a plug-in window that is open (square, top left), processing in parallel with the previous effect (||) and an offline plug-in (red line). The utility family is shown in white (never grey, so it cannot be mistaken for a block that is off). Octopus itself is not shown. Seven blocks per row, four rows; with more effects, scroll with the mouse wheel (the row count appears bottom right)."),
      ("Mouse", "<b>Click</b> a block: ON / OFF (applied 0.4 s after you release it). <b>Double-click</b> (two presses on the same block within 0.65 s) never switches the effect: if a slow double-click arrives after the first click has already switched it, Octopus switches it back. Double-click: parameters on screen. <b>Drag</b> a block: a blue bar shows where it will go, release to move it (the screen scrolls when you drag near its top or bottom edge). <b>Right-click</b>: Bypass / Enable, Parameters on screen, Open / Close plug-in window, Assign to footswitch (STOMP) A-D, Process in parallel with previous, Move (left, right, to start, to end), Insert an effect before / after, Remove. The <b>Delete</b> key removes the block under the mouse. Every action is an undo point in REAPER (Edit &gt; Undo: <i>Octopus: ...</i>)."),
      ("Adding an effect", "Click the <b>+</b> block (or Insert before / after). The ADD AN EFFECT panel lists the plug-ins installed in REAPER (instruments hidden). Type in the search field and filter with <b>All, Studio Wiga, VST3, VST, CLAP, JS, AU</b>. <b>Click the plug-ins one after another</b>: each one is numbered in the order of your clicks (click again to remove it, CLEAR to start over), then press <b>ADD</b> (or Enter): they are all inserted in the track, in that order, where shown at the top (before a block or at the end), as a single undo point. A double-click adds right away. RESCAN re-reads REAPER's list after installing new plug-ins; the mouse wheel scrolls the list."),
      ("Parameters", "The parameters panel shows 8 parameters per page with their name in large letters and the value as the plug-in displays it. Click or drag on a bar to set it; the wheel or the arrows change page. A continuous parameter moves smoothly; a parameter with positions (delay or amp modes, switches, MIDI channel...) is shown as a row of notches and moves <b>notch by notch</b>, never in between. Everything about MIDI (MIDI channel, MIDI learn, controllers...) is hidden: the <b>MIDI</b> button shows those parameters only, press it again to come back. <b>WINDOW</b> opens or closes the plug-in's own window; <b>BYPASS</b>, next to the close cross, switches the effect off (lit while bypassed). Values follow the plug-in live (also when you turn its knobs). Close with X or Escape."),
      ("Plug-in presets", "In the parameters panel, <b>PRESETS</b> lists the presets of the plug-in as REAPER knows them: its <b>FACTORY</b> presets and your <b>USER</b> presets (saved in REAPER), numbered, with the current one highlighted and shown at the top. Click a preset to load it; the arrows load the previous / next one. To read the names, Octopus briefly loads each preset and then restores the plug-in exactly as it was (settings and displayed preset): do it while not playing. RELOAD LIST reads the list again after you save new presets. PRESETS again returns to the parameters. <b>SAVE USER</b> saves the current settings of the plug-in as a user preset of your own: type a name and press OK (or Enter; Escape cancels). These presets are kept by Octopus for that plug-in (in every project, file <i>Studio Wiga/Octopus/Plugin presets</i> in your user application data folder), appear at the top of the list with a star and the USER tag, and load with a click. A name already used is never replaced without confirmation. Right-click one of them: Load, Replace with the current settings, Rename, Delete. (REAPER does not let a plug-in add presets to its own preset menu, so these presets are listed in Octopus only.)")],
 scr_cap_add="ADD AN EFFECT panel, filtered on Studio Wiga plug-ins.",
 scr_cap_par="Parameters of a block.",
 scr_cap_pre="Presets of a block: your user presets saved by Octopus (star) on top, then REAPER's factory and user presets; SAVE USER, naming a new preset.",
 tun_t="Tuner (CHROMA)",
 tun=["When a <b>CHROMA</b> block is switched on (click, footswitch in STOMP mode, MIDI...), <b>Octopus becomes the tuner</b>: its screen shows CHROMA's own screen, with the same tuning engine applied to the guitar that goes through Octopus. Mode (MONO, POLY, STROBE), reference A and tuning are read from CHROMA (the tuning chosen in CHROMA, for example G standard on a 7-string, gives the strings of the POLY view and the STRING number in MONO).",
      "The tuner screen is slightly reduced and centred, with the buttons in a narrower column on the right. <b>TUNINGS</b> (or a click on the tuning name in the screen) opens CHROMA's tuning presets: your favourites, the factory tunings by instrument (guitar 6, 7, 8 strings, bass...) and your own tunings made in CHROMA. The chosen tuning is used by Octopus right away and saved with the project; <i>Use the tuning set in CHROMA</i> goes back to CHROMA's own tuning, and changing the tuning in CHROMA itself does the same. On the right: <b>MUTE</b> mutes the output through CHROMA (button red while muted), <b>MODE</b> switches CHROMA to its next mode (or click the mode name), <b>EXIT</b> switches the CHROMA block off and brings the chain back. Switching CHROMA off any other way does the same. Tip: keep Octopus first on the track so that it sees the guitar before any effect."],
 tun_cap="Octopus as a tuner: CHROMA screen in MONO, guitar 7-string in G (left), and POLY with the Drop D tuning chosen with TUNINGS (right).",
 pre_t="5. Banks and presets",
 pre=["A preset is a <b>complete FX chain</b>: plug-ins, order, every setting, ON / OFF states, parallel processing and the STOMP assignments of the footswitches. It is stored in REAPER's own format, so every plug-in (VST, VST3, CLAP, JS, AU) comes back exactly as saved. Octopus keeps 64 presets (16 banks x 4: 1A to 16D) <b>inside the REAPER project</b>.",
      "<b>SAVE</b>: saves the loaded preset, or <i>Save as...</i> a slot chosen by bank. An existing preset is never replaced without confirmation. You can also right-click a footswitch &gt; <i>Save the current chain in ...</i>.",
      "<b>Recall</b>: footswitch (PRESET mode), click on a footswitch cell, LOAD in the banks panel, double-click a preset card, or MIDI Program Change. <b>Fast switching</b>: when the preset uses the same plug-ins in the same order as the current chain, Octopus only reloads the blocks whose settings differ and simply switches the others ON / OFF: changing between presets of one rig is almost instant. Otherwise the whole chain is replaced (Octopus itself is never touched). A plug-in that is no longer installed is reported on the status line; the other effects are loaded in their right place.",
      "<b>EDITED</b> appears next to the preset name when the chain has changed since it was loaded or saved; pressing the footswitch of that preset again restores it.",
      "<b>BANKS panel</b>: the 16 banks on the left (dots = saved presets), the 4 presets of the selected bank on the right with <b>LOAD, SAVE, RENAME, COPY, PASTE, CLEAR</b>. <b>EXPORT...</b> writes all banks to an <b>.octobank</b> file, <b>IMPORT...</b> replaces the 64 presets with a file (to share banks between projects). <b>PROGRAM CHANGE = PRESETS</b> switches MIDI Program Change on or off. <b>FOOTSWITCH COLOURS</b>: COLOUR A to COLOUR D choose the colour of each footswitch in PRESET mode (letter and screen cell): blue, cyan, green, lime, yellow, amber, orange, red, pink, purple or white. The colours are saved with the project."],
 scr_cap_banks="BANKS panel.",
 fs_t="6. Footswitches, letters and modes",
 fs=["<b>PRESET mode</b>: footswitches A, B, C, D recall presets A-D of the bank shown in the header (arrows or MIDI Bank down / up change the bank shown; the bank is loaded when you press a footswitch). The letter of the loaded preset lights up in the colour of its footswitch (blue by default, chosen in BANKS &gt; FOOTSWITCH COLOURS).",
     "<b>STOMP mode</b>: each footswitch switches one or several blocks (right-click a block &gt; Assign to footswitch, or right-click a footswitch &gt; STOMP : blocks switched by...). If one of its blocks is ON, a press switches them all OFF; otherwise all ON. The footswitch cell shows the family and names of its blocks, its letter takes the colour of the first block and lights up when one of them is ON. Assignments are saved with the presets.",
     "Like on Echoflex, a footswitch acts as soon as it is pressed, its button darkens while it is held. Above it, the letter A, B, C or D is backlit: engraved and dark when off, the letter itself lights up in colour, fading in and out.",
     "Right-click a footswitch: save / load the preset of that footswitch, STOMP assignments, MIDI learn of the footswitch, latching mode, MIDI menu and plug-in size."],
 stomp_cap="STOMP mode: footswitch A switches NGATE + Crycat; the letters take the colour of their blocks.",
 midi_t="7. MIDI",
 midi=["Octopus listens to the MIDI of its track (in REAPER: MIDI input on the track, record arm or monitoring on). The MIDI then continues to the following effects (for example your expression pedal for Crycat or Wharry)."],
 midi_hdr=["Command", "Default", "How"],
 midi_rows=[["Footswitch A, B, C, D", "none", "MIDI learn (right-click the footswitch &gt; MIDI Learn, then press the controller's switch)"],
            ["Bank down / Bank up", "none", "Right-click &gt; MIDI &gt; Learn"], ["PRESET / STOMP mode", "none", "Right-click &gt; MIDI &gt; Learn"],
            ["Presets 1A ... 16D", "Program Change 1-64", "On by default (PROGRAM CHANGE = PRESETS)"]],
 midi2=["<b>Momentary or latching</b>: a CC of 64 or more is a press. Momentary (default): an action when the switch goes ON (127 on press, 0 on release). Latching (right-click &gt; <i>MIDI footswitch latching</i>): for switches that alternate 127 / 0, every change is a press.",
        "A CC number drives only one command (learning it again moves it). Right-click &gt; MIDI &gt; <i>Reset MIDI</i> clears every assignment. Assignments are saved with the project.",
        "The plug-in parameters <b>Footswitch A-D, Bank down, Bank up, PRESET / STOMP mode</b> can also be driven by REAPER's own MIDI learn or automation, with the same momentary / latching rule. The <b>Octopus ID</b> parameter is internal (it lets each Octopus find itself in the chain): never automate it.",
        "<b>M-Vave Chocolate Plus</b>: in Program Change mode its 4 switches recall presets directly (bank 1 = PC 1-4 = 1A-1D...). In CC mode, learn its switches on footswitches A-D and use STOMP / PRESET as on a hardware modeler."],
 win_t="8. Window",
 win=["The pedal fills the whole window (no jacks on the sides). Right-click outside the screen (or in the menus of the screen) &gt; <b>Plugin size</b>: <b>60 % (default)</b> (993 x 743 pixels), <b>75 %</b> and <b>100 %</b>, which are larger. The window opens at its default size and can never be made smaller; you can also drag the bottom-right corner (proportions are kept). The size is saved with the project.",
      "The <b>status line</b> under the preset name shows the track name, or the last action (preset loaded, effect added, MIDI learn...)."],
 tips_t="9. Tips",
 tips=["Insert Octopus <b>first</b> on the guitar track; your tuner (CHROMA), noise gate, pedals, Two Notes Genome and ambiences follow. Octopus passes the sound unchanged, so its position does not change the tone.",
       "For a live rig, build one chain with all the effects you need and save variations of it (blocks ON / OFF, different settings): switching between these presets is almost instant because only what differs is reloaded.",
       "Use STOMP mode inside a song to switch drive, delay or reverb groups, and PRESET mode between songs.",
       "Keep a backup of your banks with EXPORT... and IMPORT them in a new project.",
       "Use REAPER's undo freely: every Octopus action, including a preset recall, is a single undo point."],
 tech_t="10. Specifications",
 tech=[["Format", "VST3 (Windows 64-bit, macOS universal)"], ["Version", VERSION], ["Host", "REAPER 6.37 or newer (installed plug-in list: REAPER 7); inactive in other DAWs"],
       ["Audio", "Mono or stereo, passed through unchanged, 0 samples latency"], ["Chain", "Track FX chain or record input FX chain of the track where Octopus is inserted"],
       ["Presets", "16 banks x 4 presets, complete chains in REAPER's format, saved in the project, .octobank export / import; factory / user presets of each plug-in"],
       ["Tuner", "CHROMA screen and engine (MONO, POLY, STROBE) when a CHROMA block is on; mode, reference and tuning read from CHROMA; TUNINGS menu with CHROMA's tuning presets"],
       ["MIDI", "MIDI learn of footswitches A-D, bank down / up, mode; momentary or latching; Program Change 1-64; all channels; MIDI passed to the following effects"],
       ["Window", "60 % (default and minimum, 993 x 743), 75 % (1241 x 929), 100 % (1655 x 1239); anti-freeze v2 on Windows (display caches rebuilt by a timer, watchdog, no host call from the audio thread)"]]),
"fr": dict(
 lang="FRANÇAIS", sub="Contrôleur de chaîne d'effets pour REAPER &nbsp;·&nbsp; VST3 pour Windows et macOS", ver="Notice d'utilisation &nbsp;·&nbsp; version " + VERSION,
 intro_t="1. Présentation",
 intro=["<b>OCTOPUS</b> n'est pas un effet : c'est un <b>écran de commande de la chaîne d'effets d'une piste REAPER</b>, présenté comme l'écran d'un multi-effet moderne au sol (dans l'esprit du Quad Cortex mini). Insérez-le sur votre piste guitare : son écran affiche chaque effet de cette piste sous forme de bloc coloré, de IN à OUT.",
        "Tout ce que vous faites sur l'écran d'Octopus se fait <b>en même temps dans la chaîne d'effets de la piste</b> : allumer ou couper un bloc, ajouter un effet, le supprimer, le déplacer, régler ses paramètres. Et ce que vous faites directement dans REAPER apparaît un instant plus tard sur l'écran d'Octopus.",
        "Octopus garde en plus <b>16 banks de 4 presets</b> (1A à 16D) - chaque preset est une chaîne complète avec ses plugins, leur ordre et tous leurs réglages - et vous donne <b>4 footswitchs</b> pour rappeler les presets (mode PRESET) ou basculer des groupes de blocs (mode STOMP), à la souris ou avec un pédalier MIDI comme la M-Vave Chocolate Plus. Quand le bloc accordeur CHROMA est allumé, Octopus affiche l'écran de CHROMA et devient l'accordeur.",
        "Le son traverse Octopus sans modification (latence nulle). Octopus fonctionne <b>uniquement dans REAPER</b> : REAPER donne aux plugins VST3 l'accès à leur piste et à son API, ce que les autres DAW ne font pas. Dans un autre DAW, l'écran l'indique simplement."],
 inst_t="2. Installation",
 inst=[("Windows", "Copiez <b>Octopus.vst3</b> (tout le dossier) dans <b>C:\\Program Files\\Common Files\\VST3</b>."),
       ("macOS", "Copiez <b>Octopus.vst3</b> dans <b>/Library/Audio/Plug-Ins/VST3</b> (ou ~/Library/Audio/Plug-Ins/VST3). Le plugin est signé localement : si macOS le bloque, autorisez-le dans Réglages Système &gt; Confidentialité et sécurité, ou lancez <i>xattr -cr</i> sur le fichier."),
       ("REAPER", "Options &gt; Preferences &gt; Plug-ins &gt; VST &gt; <b>Re-scan</b>. Octopus apparaît sous le nom <b>VST3: Octopus (Studio Wiga)</b>. Insérez-le sur la piste à piloter, de préférence en première position. Il faut REAPER 6.37 ou plus récent ; la liste des plugins installés utilisée par l'écran d'ajout demande REAPER 7.")],
 ov_t="3. Vue d'ensemble",
 ov_rows=[["#", "Élément", "Rôle"],
   ["1", "Preset", "Preset chargé (1A ... 16D), ou -- pour une chaîne libre."],
   ["2", "Nom / ligne d'état", "Nom du preset (EDITED quand la chaîne ne lui correspond plus) ; dessous : le nom de la piste, ou la dernière action et les messages."],
   ["3", "BANK", "Bank affichée sur les footswitchs (flèches : bank précédente / suivante)."],
   ["4", "PRESET / STOMP", "Mode des footswitchs."],
   ["5", "BANKS", "Ouvre le panneau des banks (charger, enregistrer, renommer, copier, exporter, couleurs des footswitchs...)."],
   ["6", "SAVE", "Enregistre la chaîne actuelle dans un preset."],
   ["7", "Bloc", "Un effet de la piste. Clic : ON / OFF. Double-clic : paramètres. Glisser : déplacer. Clic droit : menu."],
   ["8", "+ Add", "Ajoute un effet (liste des plugins installés dans REAPER)."],
   ["9", "IN / OUT", "Trajet du signal de la piste, de gauche à droite, rangée après rangée."],
   ["10", "Cases des footswitchs", "Ce que fait chaque footswitch dans le mode actuel ; un clic agit comme le footswitch."],
   ["11", "Footswitchs A-D", "Rappellent les presets A-D de la bank affichée (PRESET) ou basculent leurs blocs (STOMP)."],
   ["12", "Lettre A-D", "Lettre rétroéclairée du footswitch : allumée pour le preset chargé (dans la couleur choisie pour ce footswitch, bleu par défaut) ou pour les blocs allumés (couleur du bloc)."],
   ],
 ctl_t="4. L'écran en détail",
 ctl=[("Blocs", "Chaque effet de la piste est un bloc dont la couleur et l'icône indiquent la famille, devinée d'après le nom du plugin : accordeur, ampli, baffle / IR, drive, dynamique, EQ, wah / filtre, delay, reverb, modulation, pitch, spécial, utilitaire. Un bloc coloré avec son point allumé est ON ; un bloc gris est contourné. De petits repères signalent une fenêtre de plugin ouverte (carré en haut à gauche), un traitement en parallèle avec l'effet précédent (||) et un plugin hors ligne (trait rouge). La famille utilitaire est en blanc (jamais en gris, pour ne pas la confondre avec un bloc éteint). Octopus lui-même n'est pas affiché. Sept blocs par rangée, quatre rangées ; au-delà, faites défiler avec la molette (le nombre de rangées s'affiche en bas à droite)."),
      ("Souris", "<b>Clic</b> sur un bloc : ON / OFF (appliqué 0,4 s après le relâchement). Le <b>double-clic</b> (deux appuis sur le même bloc en moins de 0,65 s) ne bascule jamais l'effet : si un double-clic lent arrive après la bascule du premier clic, Octopus la défait. <b>Double-clic</b> : paramètres à l'écran. <b>Glisser</b> un bloc : une barre bleue montre où il ira, relâchez pour le déplacer (l'écran défile si vous glissez près de son bord haut ou bas). <b>Clic droit</b> : Bypass / Enable, Parameters on screen, Open / Close plug-in window, Assign to footswitch (STOMP) A-D, Process in parallel with previous, Move (gauche, droite, au début, à la fin), Insert an effect before / after, Remove. La touche <b>Suppr</b> supprime le bloc sous la souris. Chaque action est un point d'annulation dans REAPER (Edit &gt; Undo : <i>Octopus: ...</i>)."),
      ("Ajouter un effet", "Cliquez sur le bloc <b>+</b> (ou Insert before / after). Le panneau ADD AN EFFECT liste les plugins installés dans REAPER (instruments masqués). Tapez dans le champ de recherche et filtrez avec <b>All, Studio Wiga, VST3, VST, CLAP, JS, AU</b>. <b>Cliquez les plugins les uns après les autres</b> : chacun est numéroté dans l'ordre de vos clics (nouveau clic pour le retirer, CLEAR pour tout recommencer), puis appuyez sur <b>ADD</b> (ou Entrée) : ils sont tous insérés dans la piste, dans cet ordre, à l'endroit indiqué en haut (devant un bloc ou à la fin), en un seul point d'annulation. Un double-clic ajoute tout de suite. RESCAN relit la liste de REAPER après l'installation de nouveaux plugins ; la molette fait défiler la liste."),
      ("Paramètres", "Le panneau des paramètres montre 8 paramètres par page avec leur nom en grand et leur valeur telle que le plugin l'affiche. Cliquez ou glissez sur une barre pour régler ; la molette ou les flèches changent de page. Un paramètre continu se règle en douceur ; un paramètre à positions (modes d'un delay ou d'un ampli, interrupteurs, canal MIDI...) s'affiche en une rangée de crans et se règle <b>cran par cran</b>, jamais entre deux. Tout ce qui concerne le MIDI (canal MIDI, MIDI learn, contrôleurs...) est caché : le bouton <b>MIDI</b> affiche seulement ces paramètres, un nouvel appui revient aux autres. <b>WINDOW</b> ouvre ou ferme la fenêtre du plugin lui-même ; <b>BYPASS</b>, à côté de la croix de fermeture, coupe l'effet (allumé tant qu'il est contourné). Les valeurs suivent le plugin en direct (même quand vous tournez ses boutons). Fermez avec X ou Échap."),
      ("Presets du plugin", "Dans le panneau des paramètres, <b>PRESETS</b> liste les presets du plugin tels que REAPER les connaît : ses presets d'usine (<b>FACTORY</b>) et vos presets utilisateur (<b>USER</b>, enregistrés dans REAPER), numérotés, le preset actif surligné et rappelé en haut. Cliquez sur un preset pour le charger ; les flèches chargent le précédent / le suivant. Pour lire les noms, Octopus charge un instant chaque preset puis rétablit le plugin exactement comme il était (réglages et preset affiché) : faites-le hors lecture. RELOAD LIST relit la liste après l'enregistrement de nouveaux presets. Un nouveau clic sur PRESETS revient aux paramètres. <b>SAVE USER</b> enregistre les réglages actuels du plugin comme preset utilisateur à vous : tapez un nom puis OK (ou Entrée ; Échap annule). Ces presets sont gardés par Octopus pour ce plugin (dans tous les projets, fichier <i>Studio Wiga/Octopus/Plugin presets</i> du dossier de données utilisateur), s'affichent en tête de liste avec une étoile et l'étiquette USER, et se chargent d'un clic. Un nom déjà utilisé n'est jamais remplacé sans confirmation. Clic droit sur l'un d'eux : Load, Replace with the current settings, Rename, Delete. (REAPER ne permet pas d'ajouter un preset au menu de presets du plugin lui-même : ces presets sont listés dans Octopus seulement.)")],
 scr_cap_add="Panneau ADD AN EFFECT, filtré sur les plugins Studio Wiga.",
 scr_cap_par="Paramètres d'un bloc.",
 scr_cap_pre="Presets d'un bloc : vos presets enregistrés par Octopus (étoile) en tête, puis les presets d'usine et utilisateur de REAPER ; SAVE USER, saisie du nom d'un nouveau preset.",
 tun_t="Accordeur (CHROMA)",
 tun=["Quand un bloc <b>CHROMA</b> est allumé (clic, footswitch en mode STOMP, MIDI...), <b>Octopus devient l'accordeur</b> : son écran affiche l'écran de CHROMA lui-même, avec le même moteur d'accordage appliqué à la guitare qui traverse Octopus. Le mode (MONO, POLY, STROBE), le La de référence et l'accordage sont lus dans CHROMA (l'accordage choisi dans CHROMA, par exemple G standard sur une 7 cordes, donne les cordes de la vue POLY et le numéro de corde STRING en MONO).",
      "L'écran de l'accordeur est un peu réduit et centré, avec les boutons dans une colonne plus étroite à droite. <b>TUNINGS</b> (ou un clic sur le nom de l'accordage dans l'écran) ouvre les presets d'accordage de CHROMA : vos favoris, les accordages d'usine par instrument (guitare 6, 7, 8 cordes, basse...) et vos accordages personnels créés dans CHROMA. L'accordage choisi est utilisé tout de suite par Octopus et enregistré avec le projet ; <i>Use the tuning set in CHROMA</i> revient à l'accordage de CHROMA, et changer l'accordage dans CHROMA lui-même fait de même. À droite : <b>MUTE</b> coupe la sortie par CHROMA (bouton rouge tant que c'est coupé), <b>MODE</b> passe CHROMA au mode suivant (ou cliquez sur le nom du mode), <b>EXIT</b> éteint le bloc CHROMA et ramène la chaîne. Éteindre CHROMA autrement fait la même chose. Conseil : gardez Octopus en premier sur la piste pour qu'il entende la guitare avant tout effet."],
 tun_cap="Octopus en accordeur : écran de CHROMA en MONO, guitare 7 cordes en G (à gauche), et en POLY avec l'accordage Drop D choisi par TUNINGS (à droite).",
 pre_t="5. Banks et presets",
 pre=["Un preset est une <b>chaîne d'effets complète</b> : plugins, ordre, tous les réglages, états ON / OFF, traitement parallèle et assignations STOMP des footswitchs. Il est gardé au format de REAPER lui-même : chaque plugin (VST, VST3, CLAP, JS, AU) revient exactement comme il a été enregistré. Octopus garde 64 presets (16 banks x 4 : 1A à 16D) <b>dans le projet REAPER</b>.",
      "<b>SAVE</b> : enregistre le preset chargé, ou <i>Save as...</i> dans un emplacement choisi par bank. Un preset existant n'est jamais remplacé sans confirmation. Vous pouvez aussi faire un clic droit sur un footswitch &gt; <i>Save the current chain in ...</i>.",
      "<b>Rappel</b> : footswitch (mode PRESET), clic sur une case de footswitch, LOAD dans le panneau des banks, double-clic sur une carte de preset, ou Program Change MIDI. <b>Changement rapide</b> : si le preset utilise les mêmes plugins dans le même ordre que la chaîne actuelle, Octopus ne recharge que les blocs dont les réglages diffèrent et se contente d'allumer ou couper les autres : passer d'un preset à l'autre d'un même rig est quasi instantané. Sinon toute la chaîne est remplacée (Octopus lui-même n'est jamais touché). Un plugin qui n'est plus installé est signalé sur la ligne d'état ; les autres effets sont chargés à leur place.",
      "<b>EDITED</b> s'affiche à côté du nom quand la chaîne a changé depuis le chargement ou l'enregistrement du preset ; appuyer de nouveau sur le footswitch de ce preset le rétablit.",
      "<b>Panneau BANKS</b> : les 16 banks à gauche (points = presets enregistrés), les 4 presets de la bank choisie à droite avec <b>LOAD, SAVE, RENAME, COPY, PASTE, CLEAR</b>. <b>EXPORT...</b> écrit toutes les banks dans un fichier <b>.octobank</b>, <b>IMPORT...</b> remplace les 64 presets par ceux d'un fichier (pour partager des banks entre projets). <b>PROGRAM CHANGE = PRESETS</b> active ou coupe les Program Change MIDI. <b>FOOTSWITCH COLOURS</b> : COLOUR A à COLOUR D choisissent la couleur de chaque footswitch en mode PRESET (lettre et case de l'écran) : bleu, cyan, vert, vert clair, jaune, ambre, orange, rouge, rose, violet ou blanc. Les couleurs sont enregistrées avec le projet."],
 scr_cap_banks="Panneau BANKS.",
 fs_t="6. Footswitchs, lettres et modes",
 fs=["<b>Mode PRESET</b> : les footswitchs A, B, C, D rappellent les presets A-D de la bank affichée en haut de l'écran (les flèches ou les commandes MIDI Bank down / up changent la bank affichée ; elle est chargée quand vous appuyez sur un footswitch). La lettre du preset chargé s'allume dans la couleur de son footswitch (bleu par défaut, à choisir dans BANKS &gt; FOOTSWITCH COLOURS).",
     "<b>Mode STOMP</b> : chaque footswitch bascule un ou plusieurs blocs (clic droit sur un bloc &gt; Assign to footswitch, ou clic droit sur un footswitch &gt; STOMP : blocks switched by...). Si l'un de ses blocs est ON, un appui les coupe tous ; sinon il les allume tous. La case du footswitch affiche la famille et les noms de ses blocs, sa lettre prend la couleur du premier bloc et s'allume quand l'un d'eux est ON. Les assignations sont enregistrées avec les presets.",
     "Comme sur Echoflex, un footswitch agit dès l'appui, son bouton s'assombrit tant qu'on appuie. Au-dessus, la lettre A, B, C ou D est rétroéclairée : gravée et sombre quand elle est éteinte, c'est la lettre elle-même qui s'allume en couleur, en fondu.",
     "Clic droit sur un footswitch : enregistrer / charger le preset de ce footswitch, assignations STOMP, MIDI learn du footswitch, mode à accrochage, menu MIDI et taille du plugin."],
 stomp_cap="Mode STOMP : le footswitch A bascule NGATE + Crycat ; les lettres prennent la couleur de leurs blocs.",
 midi_t="7. MIDI",
 midi=["Octopus écoute le MIDI de sa piste (dans REAPER : entrée MIDI sur la piste, armement ou monitoring actif). Le MIDI continue ensuite vers les effets suivants (par exemple votre pédale d'expression pour Crycat ou Wharry)."],
 midi_hdr=["Commande", "Par défaut", "Comment"],
 midi_rows=[["Footswitch A, B, C, D", "aucun", "MIDI learn (clic droit sur le footswitch &gt; MIDI Learn, puis appui sur le commutateur du contrôleur)"],
            ["Bank down / Bank up", "aucun", "Clic droit &gt; MIDI &gt; Learn"], ["Mode PRESET / STOMP", "aucun", "Clic droit &gt; MIDI &gt; Learn"],
            ["Presets 1A ... 16D", "Program Change 1-64", "Actif par défaut (PROGRAM CHANGE = PRESETS)"]],
 midi2=["<b>Momentané ou à accrochage</b> : un CC à 64 ou plus compte comme un appui. Momentané (par défaut) : action quand le commutateur passe à ON (127 à l'appui, 0 au relâchement). À accrochage (clic droit &gt; <i>MIDI footswitch latching</i>) : pour les commutateurs qui alternent 127 / 0, chaque changement est un appui.",
        "Un numéro de CC ne pilote qu'une commande (l'apprendre de nouveau le déplace). Clic droit &gt; MIDI &gt; <i>Reset MIDI</i> efface toutes les assignations. Les assignations sont enregistrées avec le projet.",
        "Les paramètres du plugin <b>Footswitch A-D, Bank down, Bank up, PRESET / STOMP mode</b> peuvent aussi être pilotés par le MIDI learn ou l'automation de REAPER, avec la même règle momentané / accrochage. Le paramètre <b>Octopus ID</b> est interne (il permet à chaque Octopus de se retrouver dans la chaîne) : ne l'automatisez jamais.",
        "<b>M-Vave Chocolate Plus</b> : en mode Program Change, ses 4 commutateurs rappellent directement les presets (bank 1 = PC 1-4 = 1A-1D...). En mode CC, apprenez ses commutateurs sur les footswitchs A-D et utilisez STOMP / PRESET comme sur un multi-effet matériel."],
 win_t="8. Fenêtre",
 win=["La pédale occupe toute la fenêtre (pas de jacks sur les côtés). Clic droit hors de l'écran (ou dans les menus de l'écran) &gt; <b>Plugin size</b> : <b>60 % (default)</b> (993 x 743 pixels), puis <b>75 %</b> et <b>100 %</b>, plus grandes. La fenêtre s'ouvre à sa taille par défaut et ne peut jamais être réduite en dessous ; on peut aussi tirer le coin en bas à droite (proportions conservées). La taille est mémorisée avec le projet.",
      "La <b>ligne d'état</b>, sous le nom du preset, affiche le nom de la piste ou la dernière action (preset chargé, effet ajouté, MIDI learn...)."],
 tips_t="9. Conseils d'utilisation",
 tips=["Insérez Octopus <b>en premier</b> sur la piste guitare ; votre accordeur (CHROMA), le noise gate, les pédales, Two Notes Genome et les ambiances suivent. Octopus laisse passer le son tel quel : sa position ne change pas le son.",
       "Pour la scène, construisez une chaîne avec tous les effets dont vous avez besoin et enregistrez-en des variantes (blocs ON / OFF, réglages différents) : passer d'un de ces presets à l'autre est quasi instantané car seul ce qui diffère est rechargé.",
       "Utilisez le mode STOMP dans un morceau pour basculer des groupes drive, delay ou reverb, et le mode PRESET entre les morceaux.",
       "Gardez une copie de vos banks avec EXPORT... et importez-les dans un nouveau projet avec IMPORT....",
       "L'annulation de REAPER reste votre amie : chaque action d'Octopus, rappel de preset compris, est un seul point d'annulation."],
 tech_t="10. Caractéristiques techniques",
 tech=[["Format", "VST3 (Windows 64 bits, macOS universel)"], ["Version", VERSION], ["Hôte", "REAPER 6.37 ou plus récent (liste des plugins installés : REAPER 7) ; inactif dans les autres DAW"],
       ["Audio", "Mono ou stéréo, transmis sans modification, latence de 0 échantillon"], ["Chaîne", "Chaîne d'effets ou effets d'entrée (enregistrement) de la piste où Octopus est inséré"],
       ["Presets", "16 banks x 4 presets, chaînes complètes au format de REAPER, enregistrés dans le projet, export / import .octobank ; presets usine / utilisateur de chaque plugin"],
       ["Accordeur", "écran et moteur de CHROMA (MONO, POLY, STROBE) quand un bloc CHROMA est allumé ; mode, référence et accordage lus dans CHROMA ; menu TUNINGS avec les presets d'accordage de CHROMA"],
       ["MIDI", "MIDI learn des footswitchs A-D, bank - / +, mode ; momentané ou à accrochage ; Program Change 1-64 ; tous canaux ; MIDI transmis aux effets suivants"],
       ["Fenêtre", "60 % (par défaut et minimum, 993 x 743), 75 % (1241 x 929), 100 % (1655 x 1239) ; anti-gel v2 sous Windows (caches d'affichage refaits par un timer, chien de garde, aucun appel à l'hôte depuis le thread audio)"]]),
}

W = A4[0] - 36 * mm
def captioned(path, width, cap):
    return KeepTogether([Spacer(1, 2 * mm), img(path, width), Spacer(1, 1 * mm), P(cap, "small")])

def section(lang):
    t = T[lang]; f = []
    f.append(NextPageTemplate("p"))
    f.append(Spacer(1, 4 * mm))
    f.append(P(t["lang"], "coverlang"))
    f.append(Spacer(1, 3 * mm))
    f.append(img(cover, W))
    f.append(Spacer(1, 2 * mm))
    f.append(P("O C T O P U S", "cover"))
    f.append(P(t["sub"], "coversub"))
    f.append(Spacer(1, 2 * mm))
    f.append(P(t["ver"], "coversub"))
    f.append(Spacer(1, 7 * mm))
    f.append(img(os.path.join(ROOT, "design", "logo_studio_wiga_blanc.png"), 52 * mm))
    f.append(PageBreak())
    f.append(P(t["intro_t"], "h1")); f += [P(x) for x in t["intro"]]
    f.append(P(t["inst_t"], "h1"))
    for h, x in t["inst"]: f.append(P(h, "h2")); f.append(P(x))
    f.append(PageBreak())
    f.append(P(t["ov_t"], "h1")); f.append(img(annotated, W * 0.80)); f.append(Spacer(1, 3 * mm))
    f.append(table(t["ov_rows"], [10 * mm, 38 * mm, W - 48 * mm]))
    f.append(PageBreak())
    f.append(P(t["ctl_t"], "h1"))
    for h, x in t["ctl"]: f.append(P(h, "h2")); f.append(P(x))
    pair = Table([[img(scr_add, W * 0.48), img(scr_params, W * 0.48)], [P(t["scr_cap_add"], "small"), P(t["scr_cap_par"], "small")]],
                 colWidths=[W * 0.5, W * 0.5])
    pair.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0)]))
    f.append(KeepTogether([Spacer(1, 3 * mm), pair]))
    f.append(captioned(scr_presets, W * 0.48, t["scr_cap_pre"]))
    f.append(P(t["tun_t"], "h2")); f += [P(x) for x in t["tun"]]
    tpair = Table([[img(scr_tmono, W * 0.48), img(scr_tpoly, W * 0.48)]], colWidths=[W * 0.5, W * 0.5])
    tpair.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0)]))
    f.append(KeepTogether([Spacer(1, 2 * mm), tpair, P(t["tun_cap"], "small")]))
    f.append(PageBreak())
    f.append(P(t["pre_t"], "h1")); f += [P(x) for x in t["pre"]]
    f.append(captioned(scr_banks, W * 0.62, t["scr_cap_banks"]))
    f.append(P(t["fs_t"], "h1")); f += bullets(t["fs"])
    f.append(captioned(scr_stomp, W * 0.62, t["stomp_cap"]))
    f.append(P(t["midi_t"], "h1")); f += [P(x) for x in t["midi"]]
    f.append(Spacer(1, 2 * mm)); f.append(table([t["midi_hdr"]] + t["midi_rows"], [40 * mm, 32 * mm, W - 72 * mm])); f.append(Spacer(1, 3 * mm))
    f += bullets(t["midi2"])
    f.append(P(t["win_t"], "h1")); f += [P(x) for x in t["win"]]
    f.append(P(t["tips_t"], "h1")); f += bullets(t["tips"])
    f.append(KeepTogether([P(t["tech_t"], "h1"), table(t["tech"], [30 * mm, W - 30 * mm], header=False)]))
    f.append(PageBreak())
    return f

def deco(c, doc):
    c.saveState()
    c.setFillColor(DARK); c.rect(0, A4[1] - 6 * mm, A4[0], 6 * mm, stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 6.8 * mm, A4[0], 0.8 * mm, stroke=0, fill=1)
    c.setFont("Pop", 7.5); c.setFillColor(GREY)
    c.drawString(18 * mm, 10 * mm, "Octopus (Studio Wiga) · v" + VERSION)
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))
    c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=18 * mm,
                      title="OCTOPUS - Manual / Notice v" + VERSION, author="Studio Wiga")
def cover_bg(c, doc):
    c.saveState(); c.setFillColor(colors.black); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 3 * mm, A4[0], 3 * mm, stroke=0, fill=1); c.restoreState()
doc.addPageTemplates([PageTemplate(id="cover", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="fc")], onPage=cover_bg),
                      PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")], onPage=deco)])
en = section("en"); fr = section("fr")
en[-1:] = [NextPageTemplate("cover"), PageBreak()]
story = en + fr[:-1]
doc.build(story)
print(OUT)
