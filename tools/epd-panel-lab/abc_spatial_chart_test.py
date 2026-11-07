"""Contract checks for generated camera-relative spatial charts."""
import json
import struct
import numpy as np
from abc_spatial_mix import split_phases
from abc_spatial_chart import generate


def test_generated_chart_contract(tmp_path):
    # Synthetic cube: no ignored local captures or private measured palette.
    entries = []
    for code in range(8):
        entries.append({'code': code, 'A': 0, 'B': 0, 'C': code,
                        'cameraRGB': [220 if code & bit else 30 for bit in (4, 2, 1)],
                        'repeatDifferences': [{'cameraRGBDelta': [1, 0, -1]}],
                        'allChannelsShadowClippedFraction': 0,
                        'allChannelsHighlightClippedFraction': 0})
    palette = tmp_path/'synthetic-palette.json'
    palette.write_text(json.dumps({'entries': entries}), encoding='utf-8')
    root = tmp_path/'chart'
    generate(palette, root)
    m = json.loads((root/'manifest.json').read_text())
    assert len(m['recipes']) == 512 and len(m['pages']) == 13
    assert not m['claims512OpticalColors']
    signatures = {tuple(sorted(r['counts4096'].items())) for r in m['recipes']}
    assert len(signatures) == 512
    assert m['minimumPredictedNeighbor'] > 0
    for page in m['pages']:
        codes = np.load(root/(page['name']+'-codes.npy'))
        a,b,c = split_phases(codes)
        assert np.array_equal((a.astype(int)<<6)|(b.astype(int)<<3)|c,codes)
        film = (root/(page['name']+'.film')).read_bytes()
        assert len(film) == 691232
        assert struct.unpack_from('<HH',film,4) == (720,480)
        for patch in page['patches']:
            x,y,w,h=patch['rect']
            block=codes[y:y+h,x:x+w]
            values,counts=np.unique(block,return_counts=True)
            assert {str(int(v)):int(n) for v,n in zip(values,counts)} == m['recipes'][patch['id']]['counts4096']
            assert np.array_equal(block[:16,:16],block[16:32,16:32])
