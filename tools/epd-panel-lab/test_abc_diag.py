"""Minimal diagnostic binary/label contract; no hardware operations."""
import json
import struct
import tempfile
import unittest
from pathlib import Path

from PIL import Image

import abc_diag as diag
import panel_lab as lab


class AbcDiagnosticTest(unittest.TestCase):
    def test_first_round_contract(self):
        profiles, _ = lab.load_profiles()
        profile = profiles["ColorQual"]
        labels = set()
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            manifest = diag.generate(output)
            self.assertEqual(len(manifest["assets"]), 4)
            self.assertEqual([(a["mode"], a["B"]) for a in manifest["assets"]],
                             [(0, 0), (1, 0), (2, 0), (2, 7)])
            self.assertEqual(json.loads((output / "abc-manifest.json").read_text("utf-8")), manifest)
            for asset in manifest["assets"]:
                raw = (output / asset["path"]).read_bytes()
                self.assertEqual(struct.unpack("<IHHBBH", raw[:12]), (345600, 720, 480, 0, 2, 1))
                self.assertEqual(len(raw), 345632)
                self.assertEqual(raw[12:32], bytes(20))
                self.assertEqual(raw[32], (asset["mode"] << 3) | asset["B"])
                self.assertLessEqual(max(raw[32:]), 63)
                self.assertEqual(lab.sha256(raw), asset["sha256"])
                image, expected = diag.make_card(profile, asset["mode"], asset["B"])
                self.assertEqual(raw, expected)
                self.assertEqual(image.getpixel((0, 719)), raw[32])
                # Portrait top-left orientation dot survives and is NOT body[0].
                self.assertEqual(raw[32 + 17 * 720 + 719 - 17], profile["dark"])
                self.assertEqual(image.getpixel((17, 17)), profile["dark"])
                labels.add(image.crop((50, 0, 480, 48)).tobytes())
                with Image.open(output / asset["preview"]["path"]) as preview:
                    self.assertEqual(preview.size, (480, 720))
                    for code in range(64):
                        x, y = 16 + (code % 8) * 57, 109 + (code // 8) * 70
                        physical = x * 720 + 719 - y
                        self.assertEqual(raw[32 + physical], code)
                        self.assertEqual(preview.getpixel((x, y)), tuple(profile["palette"][code]))
            self.assertEqual(len(labels), 4)

    def test_all_b_and_invalid_parameters(self):
        with tempfile.TemporaryDirectory() as folder:
            manifest = diag.generate(Path(folder), all_b=True)
            self.assertEqual(len(manifest["assets"]), 10)
            self.assertEqual({a["B"] for a in manifest["assets"] if a["mode"] == 2}, set(range(8)))
        profiles, _ = lab.load_profiles()
        for mode, b in ((3, 0), (-1, 0), (2, 8), (2, -1)):
            with self.assertRaises(ValueError):
                diag.make_card(profiles["ColorQual"], mode, b)


if __name__ == "__main__":
    unittest.main()
