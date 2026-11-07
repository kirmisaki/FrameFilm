"""Focused app/flash-to-release-manifest contracts; no device access."""
import json
import struct
import tempfile
import unittest
from pathlib import Path

import classic_firmware_manifest as m


def app_image(version=b'1.0.0\0', project=b'frame_film\0'):
    value = bytearray(512)
    value[0] = 0xe9
    struct.pack_into('<H', value, 12, 9)
    struct.pack_into('<I', value, 32, 0xabcd5432)
    value[48:48 + len(version)] = version
    value[80:80 + len(project)] = project
    value[176:208] = bytes(range(1, 33))
    return value


def flash_image(app):
    value = bytearray(m.APP_OFFSET + len(app))
    value[0:len(app)] = app
    value[0x8000:0x8002] = b'\xaa\x50'
    value[m.APP_OFFSET:] = app
    return value


def build_tree(root):
    for variant in m.VARIANTS:
        for screen in m.SCREENS:
            folder = root / f'frame-film-pro-{variant}-{screen}'
            folder.mkdir(parents=True)
            app = app_image()
            (folder / 'frame_film.bin').write_bytes(app)
            (folder / f'frame_film_pro_{variant}_{screen}-flash.bin').write_bytes(flash_image(app))
    return root


class ManifestTest(unittest.TestCase):
    def test_matrix_metadata_and_idempotence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = build_tree(Path(folder) / 'input')
            (root / 'frame-film-pro-n4r2-e6-3.68-792x528-logs').mkdir()
            out = Path(folder) / 'release'
            first = m.generate(root, out, '2026.11.07.1')
            self.assertEqual(first, m.generate(root, out, '2026.11.07.1'))
            self.assertEqual(first['project'], 'frame_film')
            self.assertEqual(first['model'], 'pro')
            self.assertEqual(len(first['firmwares']), len(m.VARIANTS) * len(m.SCREENS))
            for item in first['firmwares']:
                self.assertEqual(item['flashBytes'], m.VARIANTS[item['variant']][0])
                self.assertEqual(item['panelId'], m.SCREENS[item['screen']][0])
                self.assertGreater(item['ota']['size'], 0)
                self.assertGreater(item['flash']['size'], 0)
                ota, flash = out / item['ota']['asset'], out / item['flash']['asset']
                self.assertEqual(m.inspect_app(ota.read_bytes()), {k: item['ota'][k] for k in ('version', 'elfSha256', 'size', 'sha256')})
                self.assertEqual(flash.read_bytes()[m.APP_OFFSET:], ota.read_bytes())
            self.assertEqual(json.loads((out / 'framefilm-classic-firmware.json').read_text()), first)

    def test_invalid_inputs_fail_before_publishing(self):
        with tempfile.TemporaryDirectory() as folder:
            root = build_tree(Path(folder) / 'input')
            out = Path(folder) / 'release'
            with self.assertRaises(ValueError):
                m.generate(root, out, 'not-a-version')
            self.assertFalse(out.exists())

            missing = root / 'frame-film-pro-n4r2-e4-3.98-768x552'
            for item in missing.iterdir():
                item.unlink()
            missing.rmdir()
            with self.assertRaises(ValueError):
                m.generate(root, out, '2026.11.07.1')
            self.assertFalse(out.exists())

            root = build_tree(Path(folder) / 'input2')
            (root / 'frame-film-pro-n4r2-e6-3.68-792x528' / 'frame_film.bin').write_bytes(
                app_image(project=b'frame_film_ark\0'))
            with self.assertRaises(ValueError):
                m.generate(root, out, '2026.11.07.1')

    def test_mismatched_flash_and_oversized_app_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = build_tree(Path(folder) / 'input')
            flash = root / 'frame-film-pro-n16r8-e6-3.68-792x528' / 'frame_film_pro_n16r8_e6-3.68-792x528-flash.bin'
            value = bytearray(flash.read_bytes())
            value[0x8000:0x8002] = b'\x00\x00'
            flash.write_bytes(value)
            with self.assertRaises(ValueError):
                m.generate(root, Path(folder) / 'out', '2026.11.07.1')

            root = build_tree(Path(folder) / 'input2')
            app = root / 'frame-film-pro-n4r2-e6-3.68-792x528' / 'frame_film.bin'
            oversized = bytearray(app.read_bytes()) + bytes(1536 * 1024)
            self.assertGreater(len(oversized), m.VARIANTS['n4r2'][3])
            app.write_bytes(oversized)
            with self.assertRaises(ValueError):
                m.generate(root, Path(folder) / 'out2', '2026.11.07.1')

    def test_app_descriptor_contract(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'app.bin'
            mutations = [(0, 0), (12, 0), (32, 0), (80, ord('x'))]
            for offset, value in mutations:
                with self.subTest(offset=offset):
                    data = app_image()
                    data[offset] = value
                    path.write_bytes(data)
                    with self.assertRaises(ValueError):
                        m.inspect_app(path.read_bytes())
            with self.subTest(offset=176):
                data = app_image()
                data[176:208] = bytes(32)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    m.inspect_app(path.read_bytes())


if __name__ == '__main__':
    unittest.main()
