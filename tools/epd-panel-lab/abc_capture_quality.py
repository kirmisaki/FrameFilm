"""Offline gate: --manifest --page --image --corners --output [--baseline] [--exposure .05].
Corners: {innerScreenCorners:[[TL],[TR],[BR],[BL]]}, EXIF-transposed pixels.
Baseline is a prior report with identical referenceCodes. Exit2 means retake.
"""
import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageOps
from camera_palette_lab import exif_metadata
from abc_spatial_measure import sample_original,encode_mean

def assess(manifest,page_name,image_path,quad,baseline=None,exposure=.05,gain_bounds=(.9,1.1)):
 p=next(p for p in manifest['pages'] if p['name']==page_name);reasons=[];warnings=[];refs=[];patches=[];gain=None;residual=None
 with Image.open(image_path) as im: exif=exif_metadata(im);rgb=np.asarray(ImageOps.exif_transpose(im).convert('RGB'))
 for key,value in [('ISO',100),('exposureSeconds',exposure),('whiteBalance',1),('exposureMode',1)]:
  if exif.get(key) is None or abs(float(exif[key])-value)>1e-6:reasons.append('unexpected EXIF '+key)
 try:
  refs=[sample_original(rgb,quad,[x+8,y+8,w-16,h-16]) for x,y,w,h in p['referenceRects']]
  patches=[{'id':q['id'],**sample_original(rgb,quad,q['sampleRect'])} for q in p['patches']]
 except Exception as e:reasons.append('invalid geometry '+str(e))
 if refs:
  for i,r in enumerate(refs):
   if max(r['allChannelBlackFraction'],r['allChannelWhiteFraction'],max(r['maxFraction']))>=.01:reasons.append('reference clipping '+str(i))
   if max(r['zeroFraction'])>=.01:warnings.append('reference dark channel clipping '+str(i))
  warnings += ['patch clipping '+str(q['id']) for q in patches if q['maximumPerChannelClipFraction']>=.01]
  if baseline:
   if baseline.get('referenceCodes')!=p['referenceCodes'] or len(baseline.get('references',[]))!=6:reasons.append('baseline reference mismatch')
   else:
    a=np.array([r['linearMeanCameraRGB'] for r in refs]);b=np.array([r['linearMeanCameraRGB'] for r in baseline['references']]);gain=(a*b).sum(axis=0)/np.maximum((a*a).sum(axis=0),1e-12);residual=(encode_mean(a*gain)-encode_mean(b)).tolist()
    if gain_bounds is not None and np.any((gain<gain_bounds[0])|(gain>gain_bounds[1])):reasons.append('gain outside fixed bounds')
 return dict(protocol='v3-reference-normalized-diagnostic-gain' if gain_bounds is None else 'v2-absolute-gain-gate',gainBounds=gain_bounds,legacyGainRangeExceeded=bool(gain is not None and np.any((gain<.9)|(gain>1.1))),page=page_name,image=str(image_path),EXIF=exif,referenceCodes=p['referenceCodes'],references=refs,patches=patches,gainToBaseline=None if gain is None else gain.tolist(),encodedReferenceResidual=residual,needsRetake=bool(reasons),reasons=reasons,warnings=warnings,policy='Reference >=1% all-channel black/white or any-channel white clipping blocks; dark single-channel warns. Gain bounds configurable; None diagnostic-only; legacy range flagged. No candidate fitting or color-count claim.')

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('manifest','page','image','corners','output'):p.add_argument('--'+k,required=True)
 p.add_argument('--gain-diagnostic-only',action='store_true');p.add_argument('--baseline');p.add_argument('--exposure',type=float,default=.05);a=p.parse_args();read=lambda f:json.loads(Path(f).read_text(encoding='utf-8-sig'));c=read(a.corners)
 r=assess(read(a.manifest),a.page,a.image,c.get('innerScreenCorners',c.get('corners',c.get('pages',{}).get(a.page))),read(a.baseline) if a.baseline else None,a.exposure,gain_bounds=None if a.gain_diagnostic_only else (.9,1.1))
 Path(a.output).write_text(json.dumps(r,indent=2),encoding='utf-8');print(json.dumps({'protocol':r['protocol'],'legacyGainRangeExceeded':r['legacyGainRangeExceeded'],'needsRetake':r['needsRetake'],'reasons':r['reasons']}))
 if r['needsRetake']:raise SystemExit(2)
if __name__=='__main__':main()

