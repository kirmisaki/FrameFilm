"""Offline camera-relative spatial recipe charts; never a measured color count."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from scipy.spatial import cKDTree
from abc512_render import linear, preview
from abc_spatial_mix import sample_mixture, split_phases
from abc9_lab_io import pack_abc9_film


def digest(data):
    return hashlib.sha256(data).hexdigest()


def bases(data, count=12):
    candidates = []
    for e in data['entries']:
        repeats = e.get('repeatDifferences', [])
        if not repeats or e.get('allChannelsShadowClippedFraction', 0) > 0 or e.get('allChannelsHighlightClippedFraction', 0) > 0:
            continue
        drift = max(float(np.linalg.norm(r['cameraRGBDelta'])) for r in repeats)
        if drift <= 6:
            candidates.append((e, drift))
    if len(candidates) < 4:
        raise ValueError('Not enough repeated, unclipped provisional bases')
    colors = linear([e['cameraRGB'] for e, _ in candidates])
    chosen = [int(np.argmin(colors.sum(axis=1)))]
    distance = np.full(len(colors), np.inf)
    for _ in range(min(count, len(colors))-1):
        distance = np.minimum(distance, np.linalg.norm(colors-colors[chosen[-1]], axis=1))
        chosen.append(int(np.argmax(distance)))
    selected = [candidates[i] for i in chosen]
    if np.linalg.matrix_rank(colors[chosen]-colors[chosen].mean(axis=0)) < 3:
        raise ValueError('Selected bases do not span three dimensions')
    return selected


def recipes(selected, count=512):
    colors = linear([e['cameraRGB'] for e, _ in selected])
    rng = np.random.default_rng(512)
    # Actual Bayer16 area counts, not arbitrarily different pixel patterns.
    vertices = rng.integers(0, len(selected), (16000, 4))
    weights = rng.dirichlet(np.ones(4), len(vertices))
    counts = np.zeros((len(vertices), len(selected)), dtype=np.int16)
    for i, (v, w) in enumerate(zip(vertices, weights)):
        chosen = sample_mixture(np.broadcast_to(v, (16, 16, 4)), np.broadcast_to(w, (16, 16, 4)), 16)
        counts[i] = np.bincount(chosen.ravel(), minlength=len(selected))
    counts = np.unique(counts, axis=0)
    means = counts @ colors / 256
    chosen = [int(np.argmin(means.sum(axis=1)))]
    distance = np.full(len(means), np.inf)
    for _ in range(count-1):
        distance = np.minimum(distance, np.linalg.norm(means-means[chosen[-1]], axis=1))
        chosen.append(int(np.argmax(distance)))
    counts, means = counts[chosen], means[chosen]
    nearest = cKDTree(means).query(means, k=2)[0][:, 1]
    records, patches = [], []
    codes = np.array([e['code'] for e, _ in selected])
    for i, (c, mean) in enumerate(zip(counts, means)):
        active = np.flatnonzero(c)
        v = np.pad(active, (0, 4-len(active)), constant_values=active[0])
        w = np.pad(c[active]/256, (0, 4-len(active)))
        plane = codes[sample_mixture(np.broadcast_to(v, (64,64,4)), np.broadcast_to(w, (64,64,4)), 16)]
        actual = {str(int(codes[j])): int(c[j])*16 for j in active}
        records.append({'id': i, 'counts4096': actual, 'weights': {str(int(codes[j])): float(c[j]/256) for j in active}, 'predictedLinearCameraRGB': mean.tolist(), 'predictedCameraRGB': preview(mean).tolist(), 'nearestDistanceLinear': float(nearest[i]), 'patternSha256': digest(plane.astype('<u2').tobytes())})
        patches.append(plane)
    return records, patches


def generate(palette, output):
    data = json.loads(palette.read_text(encoding='utf-8'))
    selected = bases(data)
    records, patches = recipes(selected)
    output.mkdir(parents=True, exist_ok=True)
    colors = {e['code']: preview(linear(e['cameraRGB'])) for e, _ in selected}
    dark = min(colors, key=lambda c: sum(colors[c]))
    white = max(colors, key=lambda c: sum(colors[c]))
    references = [e['code'] for e, _ in selected[:6]]
    pages = []
    def page(name, ids):
        array = np.full((720,480), white, dtype=np.int32)
        labels = Image.new('1', (480,720), 1)
        draw = ImageDraw.Draw(labels)
        draw.text((16,8), name+' / provisional camera seed', fill=0)
        positions = []
        for slot, identity in enumerate(ids):
            identity = int(identity)
            x, y = 16+(slot%6)*76, 84+(slot//6)*76
            array[y:y+64,x:x+64] = patches[identity]
            draw.text((x,y+64), str(identity), fill=0)
            positions.append({'id': identity, 'rect': [x,y,64,64], 'sampleRect': [x+8,y+8,48,48]})
        for i, code in enumerate(references):
            x=16+i*76
            array[28:60,x:x+64]=code
            draw.text((x,62), 'R'+str(code),fill=0)
        mask=np.asarray(labels)==0
        array[mask]=dark
        film=pack_abc9_film(Image.fromarray(array))
        (output/(name+'.film')).write_bytes(film)
        np.save(output/(name+'-codes.npy'),array,allow_pickle=False)
        simulation=np.empty((720,480,3),dtype=np.uint8)
        for code,rgb in colors.items(): simulation[array==code]=rgb
        Image.fromarray(simulation).save(output/(name+'.png'))
        phases=split_phases(array)
        hashes=[]
        for label,phase in zip('abc',phases):
            Image.fromarray(phase).save(output/(name+'-'+label+'.png'))
            hashes.append(digest(phase.tobytes()))
        assert np.array_equal((phases[0].astype(int)<<6)|(phases[1].astype(int)<<3)|phases[2],array)
        pages.append({'name':name,'patches':positions,'referenceCodes':references,'referenceRects':[[16+i*76,28,64,32] for i in range(6)],'filmSha256':digest(film),'filmBytes':len(film),'phaseRawSha256':hashes})
    for n in range(11): page('SC%02d'%n,list(range(n*48,min((n+1)*48,512))))
    pilot=list(np.linspace(0,511,48,dtype=int))
    page('PILOT_A',pilot)
    page('PILOT_B',pilot[24:]+pilot[:24])
    manifest={'kind':'provisional-spatial512-camera-seed','claims512OpticalColors':False,'paletteSha256':digest(palette.read_bytes()),'baseSelection':'Repeated, no all-channel clipping, max repeat RGB Euclidean drift <=6; farthest linear camera RGB; heuristic not certification','bases':[{'entry':e,'maximumRepeatRGBDrift':d} for e,d in selected],'distanceMetric':'Euclidean linearized uncalibrated camera RGB, not DeltaE','minimumPredictedNeighbor':min(r['nearestDistanceLinear'] for r in records),'recipes':records,'pages':pages,'measurementStrategy':'Pilot A/B/A/B independent full refreshes; nine fixed-exposure frames median each; swap cancels approximate position offsets. Then SC00..SC10 twice, second run reverse page order. Keep fixed references, reject saturation, compare every pair against repeated observation residual; recipe uniqueness is not optical distinction.','layout':'portrait 480x720, 6 columns x8 rows, 64px patches, Bayer16 full periods; packer rotates to native720x480'}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--palette',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=generate(args.palette,args.output)
    print(json.dumps({'recipes':len(result['recipes']),'pages':len(result['pages']),'minNeighbor':result['minimumPredictedNeighbor'],'claims512OpticalColors':False}))

if __name__=='__main__': main()

