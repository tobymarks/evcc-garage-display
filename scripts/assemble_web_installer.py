#!/usr/bin/env python3
"""Legt Firmware, Manifest (ESP Web Tools) und Schriften in die Website (site/)."""
import argparse
import json
import shutil
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--site-dir", required=True, type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    firmware_dir = args.site_dir / "firmware"
    firmware_dir.mkdir(parents=True, exist_ok=True)
    # firmware.factory.bin enthält Bootloader, Partitionstabelle und App ab Adresse 0
    factory = args.build_dir / "firmware.factory.bin"
    if not factory.exists():
        raise FileNotFoundError(f"Build-Ergebnis fehlt: {factory}")
    shutil.copy2(factory, firmware_dir / "evcc-garage-display.bin")

    manifest = {
        "name": "EVCC-Garagendisplay",
        "version": args.version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": "evcc-garage-display.bin", "offset": 0}]}],
    }
    (firmware_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    fonts_dir = args.site_dir / "fonts"
    fonts_dir.mkdir(exist_ok=True)
    for weight in ("Medium", "SemiBold", "Bold"):
        shutil.copy2(Path("tools/fonts") / f"Montserrat-{weight}.ttf", fonts_dir)


if __name__ == "__main__":
    main()
