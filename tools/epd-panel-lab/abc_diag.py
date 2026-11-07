#!/usr/bin/env python3
"""Generate local A/B/C diagnostic films; never connect or upload to a device."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import ImageDraw

import panel_lab as lab

FIRST_ROUND = (("reuse-a", 0, 0), ("rewrite-a", 1, 0),
               ("constant-b0", 2, 0), ("constant-b7", 2, 7))


def make_card(profile: dict, mode: int, b: int):
    if mode not in (0, 1, 2) or not 0 <= b <= 7:
        raise ValueError("Expected diagnostic mode 0..2 and B 0..7")
    image = lab.color_card("ABC diagnostic", profile, 0, 64)
    draw = ImageDraw.Draw(image)
    white, dark = profile["white"], profile["dark"]
    # Keep the orientation dot and all original 64-code swatches untouched.
    draw.rectangle((50, 0, 479, 47), fill=white)
    draw.rectangle((12, 48, 479, 99), fill=white)
    action = ("REUSE A", "REWRITE A", f"CONSTANT B={b}")[mode]
    draw.text((53, 9), f"ABC MODE={mode} {action}", fill=dark, font=lab.font(18))
    draw.text((12, 52), f"A=code>>3 C=code&7 / B={b}", fill=dark, font=lab.font(18))
    draw.text((12, 78), "64 A/C CODES; NOT 64 VALID COLORS", fill=dark, font=lab.font(16))
    draw.rectangle((0, 673, 479, 696), fill=white)
    draw.text((12, 677), "PNG: A/C GUIDE ONLY; B COLORS UNKNOWN", fill=dark, font=lab.font(14))
    raw = bytearray(lab.film_bytes([image], 2))
    raw[32] = (mode << 3) | b  # NATIVE body[0], portrait (0,719).
    # PNG is the same encoded index canvas; its RGB mapping is only the existing
    # A/C estimate. In particular it cannot predict a different B phase.
    image.putpixel((0, 719), raw[32])
    return image, bytes(raw)


def generate(output: Path, all_b: bool = False) -> dict:
    profiles, source = lab.load_profiles()
    output.mkdir(parents=True, exist_ok=True)
    cases = list(FIRST_ROUND)
    if all_b:
        cases.extend((f"constant-b{b}", 2, b) for b in range(1, 7))
    assets = []
    for name, mode, b in cases:
        image, raw = make_card(profiles["ColorQual"], mode, b)
        stem = f"abc-{name}"
        record = lab.write_asset(output / f"{stem}.film", raw)
        record.update({"mode": mode, "B": b, "marker": (mode << 3) | b,
                       "label": f"MODE={mode} B={b} {name}", "format": 2,
                       "frameCount": 1, "encodedAC": list(range(64)),
                       "preview": lab.write_png(output / f"{stem}.png", image)})
        assets.append(record)
    manifest = {
        "schemaVersion": 1, "kind": "experimental-abc-diagnostic",
        "requiresKconfig": "CONFIG_ARK_EPD_ABC_DIAG=y",
        "panelIdentity": lab.PANEL_IDENTITY, "paletteSource": source,
        "previewMeaning": "Existing ColorQual A/C RGB estimates only; B response is unknown",
        "nativeMarker": {"bodyOffset": 0, "fileOffset": 32,
                         "portraitPixel": [0, 719], "encoding": "(mode << 3) | B"},
        "sequence": ["full_clear", "qual_phase1(A)", "qual_phase2(A or B)", "qual_phase3(C)"],
        "refreshCount": 4, "extraRamBytes": {"0": 0, "1": 172800, "2": 172800},
        "timingNote": "Modes 1/2 add a RAM transfer slot; complete timing is not unchanged",
        "samplingNote": "Avoid metadata pixel, text, borders and X; 512 drive combinations are not 512 proven colors",
        "assets": assets,
    }
    lab.write_asset(output / "abc-manifest.json",
                    (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=lab.ROOT / ".output/abc-diag")
    parser.add_argument("--all-b", action="store_true", help="Generate all eight B pages plus two controls; no upload")
    args = parser.parse_args()
    manifest = generate(args.output, args.all_b)
    print(f"Generated {len(manifest['assets'])} local films in {args.output.resolve()}")
    print("Experimental firmware only. PNG colors do not predict B response. No device accessed.")


if __name__ == "__main__":
    main()
