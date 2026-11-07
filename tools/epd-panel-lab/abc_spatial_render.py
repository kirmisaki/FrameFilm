"""Compare joint-state FS and geometric spatial mixing without changing waveforms."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageOps
from scipy.ndimage import gaussian_filter

from abc512_render import linear, preview, quantize, shared_floor
from abc9_lab_io import pack_abc9_film
from abc_spatial_mix import decompose, sample_mixture, split_phases
from epd_source_fit import fit_source


def logical_chart():
    """512 nominal sRGB targets, not 512 measured physical colors."""
    image = Image.new('RGB', (480, 720), 'white')
    draw = ImageDraw.Draw(image)
    draw.text((24, 20), 'ABC JOINT SPATIAL MIX / TOP', fill='black')
    draw.text((24, 42), '512 sRGB targets; not a physical color count', fill='black')
    # Each flat patch contains exactly six whole Bayer periods.
    for code in range(512):
        r, g, b = (code >> 6) & 7, (code >> 3) & 7, code & 7
        x, y = 48 + (code % 16)*24, 136 + (code // 16)*16
        color = tuple(round(v*255/7) for v in (r, g, b))
        draw.rectangle((x, y, x+23, y+15), fill=color)
    for x in range(432):
        value = round(x*255/431)
        draw.line((24+x, 78, 24+x, 111), fill=(value,)*3)
    draw.text((24, 676), 'A/B/C are LUT selectors, not RGB channels', fill='black')
    return image


def rmse(a, b, sigma):
    if sigma:
        a = gaussian_filter(a, (sigma, sigma, 0))
        b = gaussian_filter(b, (sigma, sigma, 0))
    return float(np.sqrt(np.mean((a-b)**2)))


def write_output(out, name, selected, entries, colors, target, mapped):
    codes = np.asarray([e['code'] for e in entries])[selected].astype('int32')
    phases = split_phases(codes)
    np.testing.assert_array_equal(
        (phases[0].astype('int32') << 6) | (phases[1].astype('int32') << 3) | phases[2], codes)
    film = pack_abc9_film(Image.fromarray(codes))
    (out / f'{name}.film').write_bytes(film)
    np.save(out / f'{name}-codes.npy', codes, allow_pickle=False)
    result = colors[selected]
    Image.fromarray(preview(result)).save(out / f'{name}.png')
    Image.fromarray(preview(gaussian_filter(result, (1.5, 1.5, 0)))).save(out / f'{name}-mean.png')
    for label, plane in zip('abc', phases):
        # Exact L-mode values 0..7; separate scaled diagnostic is NOT an intermediate optical prediction.
        Image.fromarray(plane).save(out / f'{name}-phase-{label}-codes.png')
        Image.fromarray(np.rint(plane.astype(float)*255/7).astype('uint8')).save(out / f'{name}-phase-{label}.png')
    return {'sha256': hashlib.sha256(film).hexdigest(), 'bytes': len(film),
            'selectedTupleCount': int(len(np.unique(codes))),
            'phaseValuesUsed': [np.unique(p).tolist() for p in phases],
            'phaseRoundTrip': True,
            'simulatedLinearRMSE': {
                str(s): {'mappedTarget': rmse(result, mapped, s),
                         'originalTarget': rmse(result, target, s)} for s in (0, 1.5, 4)},
            'meaning': 'Tuple usage and camera-relative simulation only; no physical distinct-color claim'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--palette', required=True, type=Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--image', type=Path)
    source.add_argument('--chart', action='store_true')
    parser.add_argument('--crop', type=int, nargs=4)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--fs-error-limit', type=float, default=.08)
    parser.add_argument('--bayer-size', type=int, choices=(8,16), default=8)
    parser.add_argument('--range-strength', type=float, default=0., help='CIE lightness range fit, 0..1; zero preserves baseline')
    parser.add_argument('--warm-strength', type=float, default=0., help='Optional warm mid/highlight chroma reduction, 0..1')
    args = parser.parse_args()
    if not 0 <= args.fs_error_limit <= 1:
        parser.error('fs-error-limit must be 0..1')
    if not 0 <= args.range_strength <= 1 or not 0 <= args.warm_strength <= 1:
        parser.error('range-strength and warm-strength must be 0..1')
    if args.image:
        with Image.open(args.image) as original:
            image = ImageOps.exif_transpose(original).convert('RGB')
        if args.crop:
            x0, y0, x1, y1 = args.crop
            if not (0 <= x0 < x1 <= image.width and 0 <= y0 < y1 <= image.height):
                parser.error('crop is outside the source image')
            image = image.crop(args.crop)
        image = image.resize((480, 720), Image.Resampling.LANCZOS)
    else:
        if args.crop:
            parser.error('crop requires image')
        image = logical_chart()
    data = json.loads(args.palette.read_text(encoding='utf-8'))
    white = linear(data['normalization']['whiteCameraRGB'])
    floor = shared_floor(data, white)
    colors = linear([e['cameraRGB'] for e in data['entries']])/white
    target, source_fit = fit_source(np.asarray(image), colors, floor, args.range_strength, args.warm_strength)
    vertices, weights, mapped, diagnostics = decompose(target, colors)
    selected = sample_mixture(vertices, weights, args.bayer_size)
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    image.save(out/'source.png')
    Image.fromarray(preview(target)).save(out/'target.png')
    Image.fromarray(preview(mapped)).save(out/'gamut-mapped.png')
    manifest = {'kind': 'joint-ABC-spatial-dither', 'calibratedColorimetry': False,
                'claims512StableColors': False, 'logicalChartTargets': 512 if args.chart else None,
                'paletteSha256': hashlib.sha256(args.palette.read_bytes()).hexdigest(),
                'sourceSha256': hashlib.sha256(args.image.read_bytes()).hexdigest() if args.image else None,
                'sourceCrop': args.crop, 'sourceFloor': floor, 'whiteLinearRGB': white.tolist(),
                'sourceFit': source_fit,
                'fsErrorLimit': args.fs_error_limit,
                'geometry': diagnostics, 'outputs': {},
                'sampling': f'Deterministic standard Bayer{args.bayer_size}, one joint tuple selected per pixel before splitting',
                'bayerSize': args.bayer_size,
                'phasePreviewMeaning': '0..7 RAM/LUT selector; scaled grayscale is not the optical appearance of that phase',
                'physicalRefresh': 'Existing full clear and original three Qual phases; no added REF or LUT changes',
                'simulationLimits': ['Uncharacterized UVC camera RGB; nominal sRGB linearization',
                    'Spatial linear area-average model is approximate; no calibrated spectral colorimetry',
                    'Measured per-state uniform patches do not establish absence of local neighbor effects',
                    'Bayer textures and gamut compression require physical comparison, not only model error'],
                'references': ['https://github.com/Frans-Willem/epd-dither/blob/main/docs/decomposition.md',
                    'https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.Delaunay.html']}
    # Same palette, source, mapping and gamut target isolate the sampling algorithm.
    for name, indices in [('nearest', quantize(mapped, colors)),
                          ('fs', quantize(mapped, colors, True, args.fs_error_limit)), ('mixture', selected)]:
        manifest['outputs'][name] = write_output(out, name, indices, data['entries'], colors, target, mapped)
    board = Image.new('RGB', (480*3, 756), '#e8e8e8')
    draw = ImageDraw.Draw(board)
    for n, (name, title) in enumerate([('nearest', 'Joint nearest'), ('fs', 'Joint FS'),
                                      ('mixture', f'Joint tetrahedra + Bayer{args.bayer_size}')]):
        draw.text((n*480+16, 12), title + ' / SIMULATION', fill='black')
        with Image.open(out/f'{name}.png') as picture:
            board.paste(picture, (n*480, 36))
    board.save(out/'comparison.png')
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps({'geometry': diagnostics, 'outputs': manifest['outputs']}, indent=2))


if __name__ == '__main__':
    main()
