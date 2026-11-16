#!/usr/bin/env python3
"""Prüft die Übersetzungstabellen (src/lang_*.cpp) gegen die Texte im Quelltext.

Aufruf aus dem Projektverzeichnis: python3 tools/check_lang.py [--keys]
Läuft auch vor jedem Build (extra_scripts in platformio.ini) und bricht ihn bei einem Fehler ab.

Gefunden werden tr("…"), TR_KEY("…") und in den Seitenvorlagen {{…}}. Geprüft wird je Tabelle:
- jeder Text aus dem Quelltext ist übersetzt, und kein Eintrag ist überflüssig oder doppelt
- die Platzhalter (%s, %d …) stimmen in Art und Reihenfolge überein
- Texte, die das Display mit seiner Schrift zeigen kann, bleiben auch übersetzt darstellbar
- Übersetzungen von Vorlagentexten enthalten kein ' (sie stehen in JavaScript-Zeichenketten)
  und so viele " wie das Original (HTML-Attribute)
"""
import glob
import os
import re
import sys

try:  # von PlatformIO als extra_script geladen
    Import("env")  # noqa: F821
    ROOT = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

STRING = r'"((?:[^"\\\n]|\\.)*)"'
CALL = re.compile(r"\b(?:tr|TR_KEY)\(\s*((?:" + STRING + r"\s*)+)\)")
TEMPLATE = re.compile(r"\{\{(.+?)\}\}", re.S)
PAIR = re.compile(r"\{\s*((?:" + STRING + r"\s*)+),\s*((?:" + STRING + r"\s*)+)\}")
PLACEHOLDER = re.compile(r"%[-+0#]*\d*(?:\.\d+)?(?:ll|l|h|z)?[sdufxXc%]")
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'"}


def unescape(text):
    return re.sub(r"\\(.)", lambda m: ESCAPES.get(m.group(1), m.group(1)), text)


def literal(code):
    """Aufeinanderfolgende C-Zeichenketten zu einem Text zusammensetzen."""
    return "".join(unescape(part) for part in re.findall(STRING, code))


def strip_comments(code):
    """//-Kommentare entfernen, ohne Zeichenketten anzutasten (dort steht z. B. https://)."""
    out = []
    for line in code.split("\n"):
        in_string = False
        i = 0
        while i < len(line):
            c = line[i]
            if c == "\\" and in_string:
                i += 2
                continue
            if c == '"':
                in_string = not in_string
            elif c == "/" and not in_string and line[i : i + 2] == "//":
                line = line[:i]
                break
            i += 1
        out.append(line)
    return "\n".join(out)


def font_ranges():
    """Zeichen der Display-Schrift aus tools/gen_fonts.sh (TEXT_RANGE)."""
    text = open(os.path.join(ROOT, "tools", "gen_fonts.sh"), encoding="utf-8").read()
    spec = re.search(r'TEXT_RANGE="([^"]+)"', text).group(1)
    chars = set()
    for part in spec.split(","):
        lo, _, hi = part.partition("-")
        chars.update(range(int(lo, 16), int(hi or lo, 16) + 1))
    return chars


def source_keys():
    keys = {}  # Text -> True, wenn er (auch) in einer Seitenvorlage steht
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "*.cpp")) + glob.glob(os.path.join(ROOT, "src", "*.h"))):
        if os.path.basename(path).startswith("lang"):
            continue
        raw = open(path, encoding="utf-8").read()
        for match in TEMPLATE.finditer(raw):
            if not match.group(1).startswith("@"):
                keys[match.group(1)] = True
        for match in CALL.finditer(strip_comments(raw)):
            keys.setdefault(literal(match.group(1)), False)
    return keys


def check():
    keys = source_keys()
    if "--keys" in sys.argv:
        for key in sorted(keys):
            print(repr(key))
        return []
    font = font_ranges()
    errors = []
    tables = sorted(glob.glob(os.path.join(ROOT, "src", "lang_*.cpp")))
    if not tables:
        errors.append("keine Tabelle src/lang_*.cpp gefunden")
    for path in tables:
        name = os.path.basename(path)
        seen = {}
        for match in PAIR.finditer(strip_comments(open(path, encoding="utf-8").read())):
            de, text = literal(match.group(1)), literal(match.group(3))
            if de in seen:
                errors.append(f"{name}: doppelt: {de!r}")
            seen[de] = text
        for key in sorted(keys):
            if key not in seen:
                errors.append(f"{name}: fehlt: {key!r}")
        for de, text in sorted(seen.items()):
            if de not in keys:
                errors.append(f"{name}: wird im Quelltext nicht verwendet: {de!r}")
                continue
            if PLACEHOLDER.findall(de) != PLACEHOLDER.findall(text):
                errors.append(f"{name}: Platzhalter weichen ab: {de!r} -> {text!r}")
            if all(ord(c) in font for c in de):
                bad = sorted({c for c in text if ord(c) not in font})
                if bad:
                    errors.append(f"{name}: Zeichen {bad} fehlen in der Display-Schrift: {text!r}")
            if keys[de]:
                if "'" in text:
                    errors.append(f"{name}: ' in einem Vorlagentext: {text!r}")
                if text.count('"') != de.count('"'):
                    errors.append(f"{name}: Anzahl der \" weicht ab: {text!r}")
    return errors


problems = check()
for problem in problems:
    print("check_lang: " + problem, file=sys.stderr)
if problems:
    sys.exit(1)
if "--keys" not in sys.argv:
    print("check_lang: Übersetzungen vollständig")
