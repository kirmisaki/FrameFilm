"""Publish FrameFilm classic Pro serial/OTA assets from a CI build matrix."""
import argparse
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path

PROJECT = 'frame_film'
MODEL = 'pro'
MIB = 1024 * 1024
VARIANTS = {
    'n4r2': (4 * MIB, 2 * MIB, 'quad', 1536 * 1024),
    'n16r8': (16 * MIB, 8 * MIB, 'octal', 4096 * 1024),
}
SCREENS = {
    'e6-3.68-792x528': (1, 792, 528),
    'e6-3.70-720x480': (2, 720, 480),
    'e6-3.60-600x400': (3, 600, 400),
    'e6-3.64-760x568': (6, 760, 568),
    'e6-3.98-768x552': (7, 768, 552),
    'e4-3.98-768x552': (8, 768, 552),
}
ARTIFACT = re.compile(r'^frame-film-pro-(n4r2|n16r8)-(.+)$')
APP_OFFSET = 0x30000


def text(raw):
    end = raw.find(b'\0')
    if end < 1:
        raise ValueError('Firmware descriptor string is missing or unterminated')
    return raw[:end].decode('ascii')


def asset_names(variant, screen):
    prefix = f'frame_film_pro_{variant}_{screen}'
    return f'{prefix}-ota.bin', f'{prefix}-flash.bin'


def inspect_app(image):
    if len(image) < 352:
        raise ValueError('Truncated app image')
    if image[0] != 0xe9 or struct.unpack_from('<H', image, 12)[0] != 9:
        raise ValueError('Expected an ESP32-S3 app image')
    if struct.unpack_from('<I', image, 32)[0] != 0xabcd5432:
        raise ValueError('Missing ESP app descriptor; merged images are not OTA inputs')
    if text(image[80:112]) != PROJECT:
        raise ValueError('Firmware project is not FrameFilm classic')
    elf = image[176:208]
    if not any(elf):
        raise ValueError('ELF fingerprint is missing')
    version = text(image[48:80])
    return {'version': version, 'elfSha256': elf.hex(),
            'size': len(image), 'sha256': hashlib.sha256(image).hexdigest()}


def inspect_flash(image, app, variant):
    flash, _, _, ota_max = VARIANTS[variant]
    if len(image) > flash:
        raise ValueError(f'{variant} flash image exceeds the hardware capacity')
    if len(image) < APP_OFFSET + len(app) or image[APP_OFFSET:APP_OFFSET + len(app)] != app:
        raise ValueError('Flash image does not contain the matching app image at 0x30000')
    if image[0x8000:0x8002] != b'\xaa\x50':
        raise ValueError('Flash image has no partition table at 0x8000')
    return {'size': len(image), 'sha256': hashlib.sha256(image).hexdigest()}


def generate(input_dir, output, version, model=MODEL):
    input_dir, output = Path(input_dir), Path(output)
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:\.\d+)?', version):
        raise ValueError('Release version must look like 2026.11.07.1')
    found = {}
    for folder in sorted(input_dir.iterdir()):
        match = ARTIFACT.fullmatch(folder.name)
        if match and folder.is_dir() and match.group(2) in SCREENS:
            found[(match.group(1), match.group(2))] = folder
    expected = {(variant, screen) for variant in VARIANTS for screen in SCREENS}
    if set(found) != expected:
        missing = sorted(expected - set(found))
        extra = sorted(set(found) - expected)
        raise ValueError(f'Artifact matrix mismatch; missing {missing}, unexpected {extra}')

    # Validate every build before replacing a published release.
    items = []
    for variant, screen in sorted(expected):
        folder = found[(variant, screen)]
        app_path = folder / 'frame_film.bin'
        ota_name, flash_name = asset_names(variant, screen)
        flash_path = folder / flash_name
        if not app_path.is_file() or not flash_path.is_file():
            raise ValueError(f'{folder.name} is missing frame_film.bin or {flash_name}')
        app = app_path.read_bytes()
        app_item = inspect_app(app)
        flash, psram, bus, ota_max = VARIANTS[variant]
        if app_item['size'] > ota_max:
            raise ValueError(f'{folder.name} app image exceeds the OTA slot')
        flash_item = inspect_flash(flash_path.read_bytes(), app, variant)
        panel, width, height = SCREENS[screen]
        items.append({
            'variant': variant, 'flashBytes': flash, 'psramBytes': psram, 'psramBus': bus,
            'screen': screen, 'panelId': panel, 'width': width, 'height': height,
            'ota': {'asset': ota_name, **app_item},
            'flash': {'asset': flash_name, **flash_item},
            '_sources': {ota_name: app_path, flash_name: flash_path},
        })

    output.mkdir(parents=True, exist_ok=True)
    for item in items:
        for name, source in item.pop('_sources').items():
            destination = output / name
            if source.resolve() != destination.resolve():
                shutil.copyfile(source, destination)
    manifest = {'schemaVersion': 1, 'project': PROJECT, 'model': model,
                'release': version, 'firmwares': items}
    path = output / 'framefilm-classic-firmware.json'
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--model', default=MODEL)
    args = parser.parse_args()
    result = generate(args.input, args.output, args.version, args.model)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
