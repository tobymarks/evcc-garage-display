# Baut tools/fonts/evcc-icons.ttf aus den SVGs in tools/icons (Aufruf über tools/gen_fonts.sh).
# Shopicons (Apache-2.0, wie in der evcc-Weboberfläche) und Material Symbols (Apache-2.0).
# Ausführen mit: fontforge -lang=py -script tools/build_icon_font.py
import os
import fontforge

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ICONS = os.path.join(ROOT, "tools", "icons")
OUT = os.path.join(ROOT, "tools", "fonts", "evcc-icons.ttf")

# Codepoint → SVG; muss zu src/icons.h passen
GLYPHS = {
    0xE000: "sun.svg",
    0xE001: "lightning.svg",
    0xE002: "cablecharge.svg",
    0xE003: "car3.svg",
    0xE004: "exclamationtriangle.svg",
    0xE005: "settings.svg",
    0xE006: "always-charge.svg",
    0xE007: "wifi-off.svg",
    0xE008: "cloud-offline.svg",
}

EM = 1000
font = fontforge.font()
font.fontname = "evcc-icons"
font.familyname = "evcc-icons"
font.em = EM
font.ascent = 800
font.descent = 200

for code, name in GLYPHS.items():
    g = font.createChar(code)
    g.importOutlines(os.path.join(ICONS, name))
    # FontForge bildet die viewBox auf die Em-Box ab (Grundlinie bei 20 %), so behalten alle Icons
    # die Proportionen und Innenabstände der Vorlage
    g.removeOverlap()
    g.correctDirection()
    g.width = EM

font.generate(OUT)
print("Icon-Schrift erzeugt:", OUT)
