#!/usr/bin/env python3
"""Legt Firmware, Manifest (ESP Web Tools), Update-Info für die Displays und Schriften in die Website (site/)."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

from version import built_version


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--site-dir", required=True, type=Path)
    # Standard: genau die Version, die in die Firmware eingebaut wurde (include/fw_version.h)
    parser.add_argument("--version", default=None)
    args = parser.parse_args()
    args.version = args.version or built_version()

    firmware_dir = args.site_dir / "firmware"
    firmware_dir.mkdir(parents=True, exist_ok=True)
    # firmware.factory.bin enthält Bootloader, Partitionstabelle und App ab Adresse 0
    factory = args.build_dir / "firmware.factory.bin"
    if not factory.exists():
        raise FileNotFoundError(f"Build-Ergebnis fehlt: {factory}")
    shutil.copy2(factory, firmware_dir / "evcc-garage-display.bin")

    manifest = {
        "name": "evcc-Garagendisplay",
        "version": args.version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": "evcc-garage-display.bin", "offset": 0}]}],
    }
    (firmware_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    # Automatische Updates: Die Displays lesen update.json und laden nur die App (ohne Bootloader)
    app = args.build_dir / "firmware.bin"
    shutil.copy2(app, firmware_dir / "evcc-garage-display-app.bin")
    data = app.read_bytes()
    update = {
        "version": args.version,
        "file": "evcc-garage-display-app.bin",
        "size": len(data),
        "md5": hashlib.md5(data).hexdigest(),
    }
    (firmware_dir / "update.json").write_text(json.dumps(update, indent=2) + "\n", encoding="utf-8")

    fonts_dir = args.site_dir / "fonts"
    fonts_dir.mkdir(exist_ok=True)
    for weight in ("Medium", "SemiBold", "Bold"):
        shutil.copy2(Path("tools/fonts") / f"Montserrat-{weight}.ttf", fonts_dir)


if __name__ == "__main__":
    main()
