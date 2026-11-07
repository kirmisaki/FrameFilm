"""Publish Ark OTA assets by the hardware descriptor in each built app image."""
import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

DESCRIPTOR_OFFSET = 288
DESCRIPTOR_SIZE = 64
TARGETS = {
    'n4r2': ('ark-esp32s3-n4r2', 4 * 1024 * 1024, 2 * 1024 * 1024, 1),
    'n16r8': ('ark-esp32s3-n16r8', 16 * 1024 * 1024, 8 * 1024 * 1024, 2),
}


def text(raw):
    end = raw.find(b'\0')
    if end < 0:
        raise ValueError('Descriptor string is not NUL-terminated')
    return raw[:end].decode('ascii')


def inspect_image(path):
    image = Path(path).read_bytes()
    if len(image) < DESCRIPTOR_OFFSET + DESCRIPTOR_SIZE:
        raise ValueError('Truncated Ark app image')
    if image[0] != 0xe9 or struct.unpack_from('<H', image, 12)[0] != 9:
        raise ValueError('Expected an ESP32-S3 app image')
    if struct.unpack_from('<I', image, 32)[0] != 0xabcd5432:
        raise ValueError('Missing ESP app descriptor; merged images are not OTA inputs')
    if struct.unpack_from('<I', image, 28)[0] < 320:
        raise ValueError('First segment does not contain the hardware descriptor')
    desc = image[DESCRIPTOR_OFFSET:DESCRIPTOR_OFFSET + DESCRIPTOR_SIZE]
    if desc[:8] != b'FFARKHW1' or desc[8:10] != bytes([1, 64]):
        raise ValueError('Missing or unsupported Ark hardware descriptor')
    chip, flash, psram = struct.unpack_from('<HII', desc, 10)
    bus = desc[20]
    target = text(desc[24:56])
    if chip != 9 or desc[21:24] != bytes(3) or desc[56:64] != bytes(8):
        raise ValueError('Invalid Ark hardware descriptor')
    variant = next((name for name, values in TARGETS.items() if values == (target, flash, psram, bus)), None)
    if variant is None:
        raise ValueError('Hardware target and memory requirements do not match a supported variant')
    project = text(image[80:112])
    if project != 'frame_film_ark':
        raise ValueError('Firmware project is not FrameFilm Ark')
    elf = image[176:208]
    if not any(elf):
        raise ValueError('ELF fingerprint is missing')
    return {'targetId': target, 'chipId': chip, 'flashBytes': flash, 'psramBytes': psram,
            'psramBus': {1: 'quad', 2: 'octal'}[bus], 'asset': f'frame_film_ark-{variant}.bin',
            'size': len(image), 'sha256': hashlib.sha256(image).hexdigest(),
            'elfSha256': elf.hex(), 'displayVersion': text(image[48:80])}


def generate(inputs, output):
    # Validate both builds before replacing any published asset.
    items = []
    for variant in TARGETS:
        path = Path(inputs[variant])
        item = inspect_image(path)
        if item['targetId'] != TARGETS[variant][0]:
            raise ValueError(f'{variant} input contains a different hardware target')
        items.append((path, item))
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    for path, item in items:
        destination = output / item['asset']
        if path.resolve() != destination.resolve():
            shutil.copyfile(path, destination)
    manifest = {'schemaVersion': 1, 'project': 'frame_film_ark', 'firmwares': [item for _, item in items]}
    path = output / 'framefilm-ark-firmware.json'
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--n4r2', type=Path, required=True)
    parser.add_argument('--n16r8', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = generate({'n4r2': args.n4r2, 'n16r8': args.n16r8}, args.output)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
