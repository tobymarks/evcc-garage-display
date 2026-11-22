"""Firmware-Version aus Git: Datum und Kurz-Hash des letzten Commits, der die Firmware ändert, z. B. 2026-10-15-f4f97ad.

Maßgeblich sind nur die Dateien in FIRMWARE_PATHS. Ein Push, der nur Website, README oder Doku ändert,
behält dadurch die bisherige Version, und die Displays installieren nichts neu.
Lokal und in GitHub Actions entsteht für denselben Stand dieselbe Version – ein Display mit
dieser Version holt sich dann kein Update. Ungespeicherte Änderungen an der Firmware hängen "-dirty" an.
In GitHub Actions braucht das den ganzen Verlauf (actions/checkout mit fetch-depth: 0).

Wird als PlatformIO-Skript (extra_scripts) und vom Website-Skript genutzt.
"""
import subprocess
from pathlib import Path

try:
    ROOT = Path(__file__).resolve().parent.parent
except NameError:  # PlatformIO führt das Skript ohne __file__ aus
    Import("env")  # noqa: F821
    ROOT = Path(env["PROJECT_DIR"])  # noqa: F821


def _git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()


# Alles, was in die Firmware eingeht. Änderungen woanders (site/, README.md, Doku) erzeugen keine neue Version.
FIRMWARE_PATHS = ("src", "include", "platformio.ini", "partitions.csv", "scripts/version.py")


def firmware_version() -> str:
    try:
        last = _git("log", "-1", "--format=%cd %h", "--abbrev=7", "--date=format:%Y-%m-%d", "--", *FIRMWARE_PATHS)
        if not last:  # Verlauf ohne passenden Commit (z. B. flacher Checkout): wie bisher HEAD nehmen
            last = _git("log", "-1", "--format=%cd %h", "--abbrev=7", "--date=format:%Y-%m-%d")
        date, sha = last.split()
        dirty = _git("status", "--porcelain", "--untracked-files=no", "--", *FIRMWARE_PATHS)
    except (OSError, subprocess.CalledProcessError, ValueError):
        return "dev"
    return f"{date}-{sha}" + ("-dirty" if dirty else "")


HEADER = ROOT / "include" / "fw_version.h"


def built_version() -> str:
    """Version, mit der die Firmware zuletzt gebaut wurde."""
    return HEADER.read_text().split('"')[1]


def write_header() -> None:
    # Eigene Header-Datei statt Build-Flag: bei neuer Version wird nur übersetzt, was sie einbindet
    content = f'#pragma once\n#define FW_VERSION "{firmware_version()}"\n'
    if not HEADER.exists() or HEADER.read_text() != content:
        HEADER.write_text(content)


if __name__ == "__main__":
    print(firmware_version())
elif __name__ == "SCons.Script":
    write_header()
