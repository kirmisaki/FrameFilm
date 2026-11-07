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
Numerical ROIs integrate original-resolution camera pixels via inverse homography,
not a 480x720 resampled screen. ROI median/MAD are diagnostics only; color response uses arithmetic mean of
nominal-sRGB-linearized ROI pixels, encoded back for approximate Lab. This camera
linearity assumption is uncalibrated. Gain only fits six fixed linear reference RGBs to the first group; no black subtraction,
histogram adjustment or per-patch fit. All distances are nominal-sRGB approximate
Lab applied to uncalibrated camera RGB, not human DeltaE or statistical guarantees.
"""
import hashlib
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


def sample_original(rgb, quad, rect):
    """Integrate original camera pixels in inverse-projected panel ROI.

    A local bbox mask uses 8-bit fixed-point AA polygon coverage; only the
    linear area mean uses fractional coverage, median/MAD describe covered pixels.
    """
    corners=np.asarray(quad,dtype=np.float32)
    if corners.shape!=(4,2) or not np.isfinite(corners).all() or (corners<0).any() or (corners[:,0]>=rgb.shape[1]).any() or (corners[:,1]>=rgb.shape[0]).any() or not cv2.isContourConvex(corners):
        raise ValueError('Invalid original-camera inner-screen quadrilateral')
    matrix=cv2.getPerspectiveTransform(np.float32([[0,0],[479,0],[479,719],[0,719]]),corners)
    x,y,w,h=rect
    polygon=cv2.perspectiveTransform(np.float32([[[x-.5,y-.5],[x+w-.5,y-.5],[x+w-.5,y+h-.5],[x-.5,y+h-.5]]]),matrix)[0]
    lo=np.maximum(np.floor(polygon.min(axis=0)).astype(int)-2,0)
    hi=np.minimum(np.ceil(polygon.max(axis=0)).astype(int)+3,[rgb.shape[1],rgb.shape[0]])
    mask=np.zeros((hi[1]-lo[1],hi[0]-lo[0]),np.uint8)
    cv2.fillConvexPoly(mask,np.rint((polygon-lo)*256).astype(np.int32),255,lineType=cv2.LINE_AA,shift=8)
    weights=mask.astype(float)/255
    crop=rgb[lo[1]:hi[1],lo[0]:hi[0]]
    pixels=crop[mask>0].astype(float)
    coverage=weights[mask>0]
    median=np.median(pixels,axis=0)
    mean_linear=np.average(linear_rgb(pixels),axis=0,weights=coverage)
    return {'cameraRGB':median.tolist(),'spatialMAD':(1.4826*np.median(abs(pixels-median),axis=0)).tolist(),
            'encodedArithmeticMeanRGB':np.average(pixels,axis=0,weights=coverage).tolist(),
            'linearMeanCameraRGB':mean_linear.tolist(),'areaMeanCameraRGB':encode_mean(mean_linear).tolist(),
            'zeroFraction':np.average(pixels==0,axis=0,weights=coverage).tolist(),
            'maxFraction':np.average(pixels==255,axis=0,weights=coverage).tolist(),
            'allChannelBlackFraction':float(np.average(np.all(pixels==0,axis=1),weights=coverage)),'allChannelWhiteFraction':float(np.average(np.all(pixels==255,axis=1),weights=coverage)),'maximumPerChannelClipFraction':float(max(np.average(pixels==0,axis=0,weights=coverage).max(),np.average(pixels==255,axis=0,weights=coverage).max())),'samplePixelCount':len(pixels),'effectivePixelCoverage':float(coverage.sum()),
            'samplingMethod':'original EXIF-transposed camera pixels; inverse homography; local AA fixed-point polygon area weights',
            'cameraPolygon':polygon.tolist()}


def summarize(observations, expected_ids=None):
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
    return {'entries':entries,'eligibleWithTwoPanelRepeats':len(eligible),'allPairs':pairs,'nearestNeighbors':nearest,'pairsBelowEngineeringThreshold':ambiguous,'candidateGroups':list(components.values()),'candidateGroupCount':len(components) if eligible else None,'all512PassEngineeringThreshold':expected_ids is not None and len(expected_ids)==512 and set(ids)==set(expected_ids) and {e['id'] for e in eligible}==set(expected_ids) and not ambiguous,'allExpectedPassEngineeringThreshold':expected_ids is not None and set(ids)==set(expected_ids) and {e['id'] for e in eligible}==set(expected_ids) and not ambiguous,'missingObservedIds':sorted(set(expected_ids or [])-set(ids)),'extraObservedIds':sorted(set(ids)-set(expected_ids or ids)),'missingEligibleIds':sorted(set(expected_ids or [])-{e['id'] for e in eligible}),'meaning':'Fixed-layout engineering screening: connected components are unresolved groups, never physical color counts; cross-layout observations mix position effects. Components of unresolved pairs using 3x maximum independent panel repeat distance; not calibrated color count, human DeltaE or confidence guarantee'}


def measure(manifest_path, captures_path, corners_path, output):
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    captures=json.loads(captures_path.read_text(encoding='utf-8'))
    corners=json.loads(corners_path.read_text(encoding='utf-8'))
    if corners.get('coordinateSpace')!='exif-transposed': raise ValueError('Explicit EXIF-transposed corner coordinates required')
    pages={p['name']:p for p in manifest['pages']}
    raw={};corrected={};groups=[];reference_target=None
    hashes={};locations={};clipped=[];missing_logs=[]
    expected={int(r['id']) for r in manifest['recipes']}
    for group in captures['groups']:
        page=pages[group['page']];repeat=str(group['panelRepeat'])
        if not group.get('HALLog') and not group.get('halLog'): missing_logs.append(group['id'])
        for slot,p in enumerate(page['patches']):
            location={'page':group['page'],'slot':slot,'rect':p.get('rect',p['sampleRect'])}
            known=locations.setdefault(int(p['id']),[])
            if location not in known: known.append(location)
        quad=corners.get('groups',{}).get(group['id'],corners.get('pages',{}).get(group['page']))
        # Per-photo corners may supply all geometry even without a page fallback.
        photos=[]
        for filename in group['photos']:
            path=captures_path.parent/filename
            photo_map=corners.get('photos',{})
            keys=[str(filename),str(path),str(path.resolve())]
            photo_quad=next((photo_map[k] for k in keys if k in photo_map),quad)
            if photo_quad is None: raise ValueError('Missing corners for photo '+str(filename))
            sha=hashlib.sha256(path.read_bytes()).hexdigest()
            hashes.setdefault(sha,set()).add(repeat)
            with Image.open(path) as original:
                exif=exif_metadata(original)
                image=np.asarray(ImageOps.exif_transpose(original).convert('RGB'))
            refs=[sample_original(image,photo_quad,[x+8,y+8,w-16,h-16]) for x,y,w,h in page['referenceRects']]
            patches=[{'id':p['id'],**sample_original(image,photo_quad,p['sampleRect'])} for p in page['patches']]
            for patch in patches:
                if patch['maximumPerChannelClipFraction']>=.01:
                    clipped.append({'file':str(path),'id':patch['id'],'maximumClipFraction':patch['maximumPerChannelClipFraction']})
            for i,ref in enumerate(refs):
                if ref['maximumPerChannelClipFraction']>=.01:
                    clipped.append({'file':str(path),'referenceIndex':i,'maximumClipFraction':ref['maximumPerChannelClipFraction']})
            photos.append({'file':str(path),'actualInnerScreenCorners':photo_quad,'sha256':sha,'photoExif':exif,'references':refs,'patches':patches})
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
    report={'kind':'uncalibrated-spatial-chart-measurement','coverage':{'observedPatchCount':len(raw),'expectedPatchCount':len(manifest['recipes']),'missingPatchIds':sorted(set(r['id'] for r in manifest['recipes'])-set(raw))},'referenceTargetLinearRGB':reference_target.tolist() if reference_target is not None else None,'groups':groups,'raw':summarize(raw,expected),'fixedReferenceGainCorrected':summarize(corrected,expected),'correction':'Per-group diagonal gain in nominal-linear camera RGB from six fixed references to first group; no clipping or black subtraction. ROI arithmetic mean is computed after nominal sRGB linearization; re-encoded for approximate Lab. Camera response is uncalibrated, this linearity assumption is provisional.','limits':'Caller must assign genuine independent panelRepeat IDs; photographs of one display never create extra repeats. Camera RGB is uncalibrated; no human color discrimination claim.'}
    report['coverage']['extraPatchIds']=sorted(set(raw)-expected)
    report['layoutAudit']={'locationsById':locations,'idsWithMultipleLocations':[i for i,v in locations.items() if len(v)>1], 'meaning':'Multiple locations mix position effects with refresh residual; fixed-layout engineering screening only'}
    for field in ('raw','fixedReferenceGainCorrected'):
        for entry in report[field]['entries']: entry['locations']=locations[entry['id']]
    report['evidenceAudit']={'crossPanelRepeatDuplicatePhotoHashes':[{'sha256':h,'panelRepeats':sorted(v)} for h,v in hashes.items() if len(v)>1], 'groupsMissingHALLog':missing_logs,'verifiedIndependentPanelRefreshes':False,'meaning':'SHA reuse and missing log presence audit only; caller-supplied HALLog contents/timestamps and real refreshes are not verified here'}
    report['clippingWarnings']={'thresholdFraction':.01,'policy':'Fixed conservative retake warning at >=1% any-channel exact 0/255 weighted area; does not alter 3x distance rule', 'affectedPatchIds':sorted({c['id'] for c in clipped if 'id' in c}),'affectedPhotoCount':len({c['file'] for c in clipped}),'events':clipped}
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2,allow_nan=False),encoding='utf-8')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('manifest','captures','corners','output'): p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args();r=measure(args.manifest,args.captures,args.corners,args.output)
    print(json.dumps({'coverage':r['coverage'],'rawCandidateGroups':r['raw']['candidateGroupCount'],'correctedCandidateGroups':r['fixedReferenceGainCorrected']['candidateGroupCount']}))

if __name__=='__main__':main()

