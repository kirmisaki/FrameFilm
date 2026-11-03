"""Focused descriptor-to-release-manifest contracts; no device access."""
import json
import struct
import tempfile
import unittest
from pathlib import Path

import ark_firmware_manifest as m


def image(variant):
    target, flash, psram, bus = m.TARGETS[variant]
    value = bytearray(512)
    value[0] = 0xe9
    struct.pack_into('<H', value, 12, 9)
    struct.pack_into('<I', value, 28, 320)
    struct.pack_into('<I', value, 32, 0xabcd5432)
    value[48:54] = b'3.2.5\0'
    value[80:95] = b'frame_film_ark\0'
    value[176:208] = bytes(range(1, 33))
    value[288:296] = b'FFARKHW1'
    value[296:298] = bytes([1, 64])
    struct.pack_into('<HII', value, 298, 9, flash, psram)
    value[308] = bus
    value[312:312+len(target)+1] = target.encode() + b'\0'
    return value


class ManifestTest(unittest.TestCase):
    def test_variants_asset_metadata_and_idempotence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paths = {v: root / (v+'.bin') for v in m.TARGETS}
            for v, p in paths.items():
                p.write_bytes(image(v))
            out = root / 'release'
            first = m.generate(paths, out)
            self.assertEqual(first, m.generate(paths, out))
            self.assertEqual([x['targetId'] for x in first['firmwares']], [x[0] for x in m.TARGETS.values()])
            for item in first['firmwares']:
                self.assertEqual(item, m.inspect_image(out/item['asset']))
            self.assertEqual(json.loads((out/'framefilm-ark-firmware.json').read_text()), first)

    def test_wrong_missing_truncated_and_conflicting_targets(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / 'test.bin'
            mutations = [(288, 0), (296, 2), (297, 63), (298, 0), (308, 2), (309, 1), (344, 1), (12, 0)]
            for offset, value in mutations:
                with self.subTest(offset=offset):
                    data = image('n4r2'); data[offset] = value; p.write_bytes(data)
                    with self.assertRaises(ValueError):
                        m.inspect_image(p)
            p.write_bytes(image('n4r2')[:351])
            with self.assertRaises(ValueError):
                m.inspect_image(p)
            p.write_bytes(image('n16r8'))
            with self.assertRaises(ValueError):
                m.generate({'n4r2': p, 'n16r8': p}, Path(folder)/'out')
            self.assertFalse((Path(folder)/'out').exists())


if __name__ == '__main__':
    unittest.main()
