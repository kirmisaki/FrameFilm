"""One contract: container, LE16, orientation, boundary checks, existing Qual reuse."""
import struct
import unittest

from PIL import Image

import abc9_lab_io as io
import panel_lab as lab


class ABC9Contract(unittest.TestCase):
    def test_pack_contract(self):
        image = Image.new("I", lab.PORTRAIT, 0)
        image.putpixel((0, 719), 511)
        image.putpixel((479, 0), 0x123)
        raw = io.pack_abc9_film(image)
        self.assertEqual(len(raw), 691232)
        self.assertEqual(struct.unpack("<IHHBBH4s16x", raw[:32]),
                         (691200, 720, 480, 0, 0x7F, 1, b"ABC9"))
        self.assertEqual(raw[32:34], b"\xff\x01")
        self.assertEqual(raw[-2:], b"\x23\x01")
        for bad in (-1, 512):
            image.putpixel((0, 0), bad)
            with self.assertRaises(ValueError):
                io.pack_abc9_film(image)
        with self.assertRaises(ValueError):
            io.pack_abc9_film(Image.new("I", (720, 480)))
        with self.assertRaises(ValueError):
            io.pack_abc9_film(Image.new("P", lab.PORTRAIT))
        qual = Image.new("I", lab.PORTRAIT, 63)
        qual.putpixel((0, 719), 0)
        expected = Image.new("P", lab.PORTRAIT, 63)
        expected.putpixel((0, 719), 0)
        self.assertEqual(io.pack_qual_film(qual), lab.film_bytes([expected], 2))
        qual.putpixel((0, 0), 64)
        with self.assertRaises(ValueError):
            io.pack_qual_film(qual)


if __name__ == "__main__":
    unittest.main()
