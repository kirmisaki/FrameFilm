"""Measure spatial chart JPEGs without claiming calibrated or human color counts.

CLI: py -3 tools/epd-panel-lab/abc_spatial_measure.py --manifest chart/manifest.json
 --captures capture-run.json --corners corners.json --output measurement.json
capture-run schema: {"groups":[{"id":"p0-r1","page":"SC00", "panelRepeat":"refresh-001",
 "photos":["relative/to/capture-run/photo.jpg"]}]}. panelRepeat identifies one actual
completed panel refresh: multiple JPEGs/groups with the same id are NOT repeats.
corners schema: {"coordinateSpace":"exif-transposed", "groups":{"p0-r1":[[TLx,TLy],
 [TRx,TRy],[BRx,BRy],[BLx,BLy]]}, "pages":{"SC00":[...]}}.
Group corners override page corners. Coordinates use EXIF-transposed image pixels.
Repeat IDs, exposure and completed refresh evidence are caller-supplied facts.
ROI median/MAD are diagnostics only; color response uses arithmetic mean of
nominal-sRGB-linearized ROI pixels, encoded back for approximate Lab. This camera
linearity assumption is uncalibrated. Gain only fits six fixed linear reference RGBs to the first group; no black subtraction,
histogram adjustment or per-patch fit. All distances are nominal-sRGB approximate
Lab applied to uncalibrated camera RGB, not human DeltaE or statistical guarantees.
"""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np
from PIL import Image, ImageOps
from camera_palette_lab import approximate_lab, exif_metadata, linear_rgb


def encode_mean(value):
    """Nominal sRGB encoding without clipping measured/corrected values."""
    value=np.asarray(value)
    return 255*np.where(value <= .0031308,12.92*value,1.055*np.maximum(value,0)**(1/2.4)-.055)


def sample(array, rect):
    x,y,w,h=rect
    roi=array[y:y+h,x:x+w].reshape(-1,3).astype(float)
    median=np.median(roi,axis=0)
    mean_linear=linear_rgb(roi).mean(axis=0)
    return {'cameraRGB':median.tolist(),'encodedArithmeticMeanRGB':roi.mean(axis=0).tolist(),
            'linearMeanCameraRGB':mean_linear.tolist(),'areaMeanCameraRGB':encode_mean(mean_linear).tolist(),'spatialMAD':(1.4826*np.median(abs(roi-median),axis=0)).tolist(),
            'zeroFraction':(roi==0).mean(axis=0).tolist(),'maxFraction':(roi==255).mean(axis=0).tolist()}


def warp(path, quad):
    with Image.open(path) as image:
        metadata=exif_metadata(image)
        rgb=np.asarray(ImageOps.exif_transpose(image).convert('RGB'))
    corners=np.asarray(quad,dtype=np.float32)
    if corners.shape!=(4,2) or not np.isfinite(corners).all() or (corners<0).any() or (corners[:,0]>=rgb.shape[1]).any() or (corners[:,1]>=rgb.shape[0]).any():
        raise ValueError('Invalid EXIF-transposed corners: '+str(path))
    if not cv2.isContourConvex(corners) or abs(cv2.contourArea(corners))<100:
        raise ValueError('Corners must form convex TL/TR/BR/BL quadrilateral')
    transform=cv2.getPerspectiveTransform(corners,np.float32([[0,0],[479,0],[479,719],[0,719]]))
    return cv2.warpPerspective(rgb,transform,(480,720)),metadata


def summarize(observations):
    ids=sorted(observations)
    eligible=[]; entries=[]
    for identity in ids:
        repetitions=observations[identity]
        values=np.array([np.median(v,axis=0) for v in repetitions.values()])
        labs=approximate_lab(values)
        center=np.median(labs,axis=0)
        differences=[float(np.linalg.norm(a-b)) for i,a in enumerate(labs) for b in labs[i+1:]]
        residual=max(differences,default=0.)
        entry={'id':identity,'independentPanelRepeatCount':len(values),'perRepeatRGB':{k:np.median(v,axis=0).tolist() for k,v in repetitions.items()},'approxLab':center.tolist(),'maximumPanelRepeatDistance':residual,'panelRepeatDistances':differences}
        entries.append(entry)
        if len(values)>=2: eligible.append(entry)
    parent=list(range(len(eligible)))
    def root(i):
        while parent[i]!=i:
            parent[i]=parent[parent[i]];i=parent[i]
        return i
    pairs=[]; ambiguous=[]; nearest={e['id']:None for e in eligible}
    for i,a in enumerate(eligible):
        for j in range(i+1,len(eligible)):
            b=eligible[j]; d=float(np.linalg.norm(np.array(a['approxLab'])-b['approxLab']))
            threshold=3*max(a['maximumPanelRepeatDistance'],b['maximumPanelRepeatDistance'],1e-9)
            pair={'ids':[a['id'],b['id']],'distance':d,'threeTimesRepeatThreshold':threshold}
            pairs.append(pair)
            for e,other in ((a,b),(b,a)):
                old=nearest[e['id']]
                if old is None or d<old['distance']: nearest[e['id']]={'id':other['id'],'distance':d}
            if d<=threshold:
                ambiguous.append(pair);parent[root(i)]=root(j)
    components={}
    for i,e in enumerate(eligible): components.setdefault(root(i),[]).append(e['id'])
    return {'entries':entries,'eligibleWithTwoPanelRepeats':len(eligible),'allPairs':pairs,'nearestNeighbors':nearest,'pairsBelowEngineeringThreshold':ambiguous,'candidateGroups':list(components.values()),'candidateGroupCount':len(components) if eligible else None,'all512PassEngineeringThreshold':len(eligible)==512 and not ambiguous,'meaning':'Engineering connected components of unresolved pairs using 3x maximum independent panel repeat distance; not calibrated color count, human DeltaE or confidence guarantee'}


def measure(manifest_path, captures_path, corners_path, output):
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    captures=json.loads(captures_path.read_text(encoding='utf-8'))
    corners=json.loads(corners_path.read_text(encoding='utf-8'))
    if corners.get('coordinateSpace')!='exif-transposed': raise ValueError('Explicit EXIF-transposed corner coordinates required')
    pages={p['name']:p for p in manifest['pages']}
    raw={};corrected={};groups=[];reference_target=None
    for group in captures['groups']:
        page=pages[group['page']];repeat=str(group['panelRepeat'])
        quad=corners.get('groups',{}).get(group['id'],corners.get('pages',{}).get(group['page']))
        if quad is None: raise ValueError('Missing corners for '+group['id'])
        photos=[]
        for filename in group['photos']:
            path=captures_path.parent/filename
            image,exif=warp(path,quad)
            refs=[sample(image,[x+8,y+8,w-16,h-16]) for x,y,w,h in page['referenceRects']]
            patches=[{'id':p['id'],**sample(image,p['sampleRect'])} for p in page['patches']]
            photos.append({'file':str(path),'photoExif':exif,'references':refs,'patches':patches})
        if not photos: raise ValueError('Empty photograph group')
        reference=np.median([[r['linearMeanCameraRGB'] for r in p['references']] for p in photos],axis=0)
        if reference_target is None: reference_target=reference.copy()
        gain=np.sum(reference*reference_target,axis=0)/np.maximum(np.sum(reference*reference,axis=0),1e-12)
        for photo in photos:
            for patch in photo['patches']:
                identity=int(patch['id']);rgb=np.array(patch['linearMeanCameraRGB'])
                raw.setdefault(identity,{}).setdefault(repeat,[]).append(encode_mean(rgb))
                corrected.setdefault(identity,{}).setdefault(repeat,[]).append(encode_mean(rgb*gain))
        groups.append({'id':group['id'],'page':group['page'],'panelRepeat':repeat,'gain':gain.tolist(),'referenceLinearRGB':reference.tolist(),'referenceCorrectedResidualLinearRGB':(reference*gain-reference_target).tolist(),'photos':photos})
    report={'kind':'uncalibrated-spatial-chart-measurement','coverage':{'observedPatchCount':len(raw),'expectedPatchCount':len(manifest['recipes']),'missingPatchIds':sorted(set(r['id'] for r in manifest['recipes'])-set(raw))},'referenceTargetLinearRGB':reference_target.tolist() if reference_target is not None else None,'groups':groups,'raw':summarize(raw),'fixedReferenceGainCorrected':summarize(corrected),'correction':'Per-group diagonal gain in nominal-linear camera RGB from six fixed references to first group; no clipping or black subtraction. ROI arithmetic mean is computed after nominal sRGB linearization; re-encoded for approximate Lab. Camera response is uncalibrated, this linearity assumption is provisional.','limits':'Caller must assign genuine independent panelRepeat IDs; photographs of one display never create extra repeats. Camera RGB is uncalibrated; no human color discrimination claim.'}
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2,allow_nan=False),encoding='utf-8')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('manifest','captures','corners','output'): p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args();r=measure(args.manifest,args.captures,args.corners,args.output)
    print(json.dumps({'coverage':r['coverage'],'rawCandidateGroups':r['raw']['candidateGroupCount'],'correctedCandidateGroups':r['fixedReferenceGainCorrected']['candidateGroupCount']}))

if __name__=='__main__':main()

