"""Camera-relative ABC experiment renderer; no calibrated color-count claims."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageOps
from scipy.spatial import cKDTree
from abc9_lab_io import pack_abc9_film, pack_qual_film


def linear(rgb):
    x = np.asarray(rgb, dtype=float) / 255
    return np.where(x <= .04045, x / 12.92, ((x + .055) / 1.055) ** 2.4)


def preview(x):
    x = np.clip(x, 0, 1)
    return np.rint(255 * np.where(x <= .0031308, 12.92*x, 1.055*x**(1/2.4)-.055)).astype('uint8')


def quantize(target, colors, diffusion=False, limit=.08):
    tree = cKDTree(colors)
    if not diffusion:
        return tree.query(target)[1]
    work = target.copy()
    result = np.empty(target.shape[:2], dtype=np.int32)
    h, w = result.shape
    for y in range(h):
        direction = 1 if y % 2 == 0 else -1
        for x in range(w) if direction == 1 else range(w-1, -1, -1):
            value = np.clip(work[y, x], 0, 1)
            i = int(tree.query(value)[1])
            result[y, x] = i
            error = np.clip(value-colors[i], -limit, limit)
            for dx, dy, weight in ((direction,0,7),(-direction,1,3),(0,1,5),(direction,1,1)):
                xx, yy = x+dx, y+dy
                if 0 <= xx < w and yy < h:
                    work[yy,xx] += error*(weight/16)
    return result


def shared_floor(data, white):
    # Shared observed minimum opens ABC dark range; not a calibrated black point.
    colors = linear([e['cameraRGB'] for e in data['control'] + data['entries']]) / white
    return float(np.min(colors @ np.array([.2126,.7152,.0722])))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--palette', type=Path, required=True)
    p.add_argument('--image', type=Path, required=True)
    p.add_argument('--crop', type=int, nargs=4, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--dither', action='store_true')
    p.add_argument('--error-limit', type=float, default=.08)
    a = p.parse_args()
    if not 0 <= a.error_limit <= 1:
        p.error('error-limit must be 0..1')
    data = json.loads(a.palette.read_text(encoding='utf-8'))
    white = linear(data['normalization']['whiteCameraRGB'])
    dark = linear(data['normalization']['darkCameraRGB']) / white
    # One scalar floor preserves source neutral hue. No measured black is subtracted.
    control_floor = float(dark @ np.array([.2126,.7152,.0722]))
    floor = shared_floor(data, white)
    with Image.open(a.image) as original:
        image = ImageOps.exif_transpose(original).convert('RGB')
        x0,y0,x1,y1 = a.crop
        if not (0 <= x0 < x1 <= image.width and 0 <= y0 < y1 <= image.height):
            p.error('crop outside source')
        image = image.crop(a.crop).resize((480,720), Image.Resampling.LANCZOS)
    target = floor + (1-floor)*linear(np.asarray(image))
    a.output.mkdir(parents=True, exist_ok=True)
    image.save(a.output/'source.png')
    Image.fromarray(preview(target)).save(a.output/'target.png')
    manifest = {'calibratedColorimetry':False,'claims512StableColors':False,
        'mapping':'nominal sRGB linearization; shared control channel white gains, no dark subtraction; source scalar combined observed minimum luminance floor to white',
        'whiteLinearRGB':white.tolist(),'sourceFloor':floor,'controlFloorComparison':control_floor,
        'floorReason':'Shared minimum of first control and ABC measurements opens ABC dark range without subtracting black. Noise sensitive; repeats retained as diagnostics rather than averaged.', 'crop':a.crop,
        'dither':a.dither,'errorLimitLinear':a.error_limit,
        'distance':'Euclidean white-adapted linear camera RGB; approximate, camera uncharacterized',
        'paletteSha256':hashlib.sha256(a.palette.read_bytes()).hexdigest(),
        'sourceSha256':hashlib.sha256(a.image.read_bytes()).hexdigest(),
        'measurementDiagnostics':data['measurementDiagnostics'],'repeatCoverage':data['repeatCoverage'], 'outputs':{}}
    for name, entries, key, pack in [('control64',data['control'],'controlIndex',pack_qual_film),('abc512',data['entries'],'code',pack_abc9_film)]:
        colors = linear([e['cameraRGB'] for e in entries])/white
        selected = quantize(target, colors, a.dither, a.error_limit)
        codes = np.array([e[key] for e in entries])[selected].astype('int32')
        simulated = colors[selected].copy()
        if name == 'control64':
            codes[719,0] = 8
            simulated[719,0] = colors[next(i for i,e in enumerate(entries) if e[key] == 8)]
        film = pack(Image.fromarray(codes))
        (a.output/(name+'.film')).write_bytes(film)
        Image.fromarray(preview(simulated)).save(a.output/(name+'.png'))
        manifest['outputs'][name] = {'sha256':hashlib.sha256(film).hexdigest(),'bytes':len(film),'selectedCodes':np.unique(codes).tolist(),'selectedCodesMeaning':'encoding use only; neither stability nor physical distinct-color count','marker':'portrait (0,719)=8 rewrite A' if name=='control64' else None,'entries':entries,'adaptedLinearRGB':colors.tolist()}
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')


if __name__ == '__main__':
    main()
