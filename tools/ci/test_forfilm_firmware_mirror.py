import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

import forfilm_firmware_mirror as mirror


def release(tag, published, files, prerelease=False):
    return {
        'tag_name': tag, 'published_at': published, 'draft': False, 'prerelease': prerelease,
        'assets': [
            {'id': index, 'name': name, 'size': len(name),
             'digest': 'sha256:' + hashlib.sha256(name.encode()).hexdigest()}
            for index, name in enumerate(sorted(files), start=1)
        ],
    }


class FirmwareMirrorTest(unittest.TestCase):
    def test_catalog_contains_the_matching_downloads_for_each_channel(self):
        ark = release('ark-2026.11.09.1', '2026-11-09T00:00:00Z', mirror.ARK_FILES | {'app.apk'})
        ci = release(mirror.ARK_CI_TAG, '2026-11-10T00:00:00Z', mirror.ARK_FILES, prerelease=True)
        flash = 'frame_film_pro_n4r2_e6-3.70-720x480-flash.bin'
        ota = flash.replace('-flash.bin', '-ota.bin')
        classic = release('frame-film-v2026.11.07.1', '2026-11-07T00:00:00Z',
                          {mirror.CLASSIC_MANIFEST, flash, ota})
        with TemporaryDirectory() as temporary, \
                patch.object(mirror, 'get_release_json', side_effect=[[ci, classic, ark], ci]), \
                patch.object(mirror, 'download_asset', side_effect=lambda tag, asset: asset['name'].encode()):
            output = Path(temporary)
            mirror.build_mirror(output)
            catalog = json.loads((output / 'catalog.json').read_text(encoding='utf-8'))
            for key, expected in [('arkFormal', ark), ('arkCi', ci), ('classicFormal', classic)]:
                self.assertEqual(catalog[key]['tag_name'], expected['tag_name'])
                for asset in catalog[key]['assets']:
                    file = output / expected['tag_name'] / asset['name']
                    if key == 'classicFormal' and asset['name'] == ota:
                        self.assertFalse(file.exists())
                    else:
                        self.assertEqual(file.read_bytes(), asset['name'].encode())
            self.assertFalse((output / ark['tag_name'] / 'app.apk').exists())

    def test_bad_bytes_prevent_new_catalog(self):
        ark = release('ark-2026.11.09.1', '2026-11-09T00:00:00Z', mirror.ARK_FILES)
        with TemporaryDirectory() as temporary, \
                patch.object(mirror, 'get_release_json', return_value=[ark]), \
                patch.object(mirror, 'download_asset', side_effect=lambda tag, asset: b'X' * asset['size']):
            output = Path(temporary)
            with self.assertRaisesRegex(ValueError, 'failed size or SHA-256 verification'):
                mirror.build_mirror(output)
            self.assertFalse((output / 'catalog.json').exists())


if __name__ == '__main__':
    unittest.main()
