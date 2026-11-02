import numpy as np
from PIL import Image
import abc_capture_quality as gate

def test_quality_errors_clipping_drift(tmp_path,monkeypatch):
 p=tmp_path/'photo.jpg';Image.fromarray(np.full((720,480,3),120,np.uint8)).save(p)
 manifest={'pages':[{'name':'SC00','referenceCodes':list(range(6)),'referenceRects':[[i*76,20,64,32] for i in range(6)],'patches':[{'id':0,'sampleRect':[20,100,48,48]}]}]};quad=[[0,0],[479,0],[479,719],[0,719]]
 monkeypatch.setattr(gate,'exif_metadata',lambda _:dict(ISO=100,exposureSeconds=.05,whiteBalance=1,exposureMode=1))
 baseline=gate.assess(manifest,'SC00',p,quad);assert not baseline['needsRetake']
 assert gate.assess(manifest,'SC00',p,[[0,0]] )['needsRetake']
 monkeypatch.setattr(gate,'exif_metadata',lambda _:dict(ISO=200,exposureSeconds=.05,whiteBalance=1,exposureMode=1))
 assert gate.assess(manifest,'SC00',p,quad)['needsRetake']
 monkeypatch.setattr(gate,'exif_metadata',lambda _:dict(ISO=100,exposureSeconds=.05,whiteBalance=1,exposureMode=1))
 Image.fromarray(np.full((720,480,3),180,np.uint8)).save(p)
 assert gate.assess(manifest,'SC00',p,quad,baseline)['needsRetake']
 diagnostic=gate.assess(manifest,'SC00',p,quad,baseline,gain_bounds=None)
 assert not diagnostic['needsRetake'] and diagnostic['legacyGainRangeExceeded']
 assert diagnostic['protocol'].startswith('v3')
 Image.fromarray(np.full((720,480,3),255,np.uint8)).save(p)
 assert any('clipping' in r for r in gate.assess(manifest,'SC00',p,quad)['reasons'])
