"""Firmware-Version aus Git: Datum des Commits und Kurz-Hash, z. B. 2026-10-15-f4f97ad.

Lokal und in GitHub Actions entsteht für denselben Commit dieselbe Version – ein Display mit
dieser Version holt sich dann kein Update. Ungespeicherte Änderungen hängen "-dirty" an.

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


def firmware_version() -> str:
    try:
        date = _git("log", "-1", "--format=%cd", "--date=format:%Y-%m-%d")
        sha = _git("rev-parse", "--short=7", "HEAD")
        dirty = _git("status", "--porcelain", "--untracked-files=no")
    except (OSError, subprocess.CalledProcessError):
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
