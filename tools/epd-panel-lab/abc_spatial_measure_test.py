import cv2
import json
import numpy as np
from PIL import Image
from abc_spatial_measure import measure, sample, sample_original, summarize


def test_synthetic_coordinates_ids_and_real_repeats(tmp_path):
    # Area integration must preserve minority coverage, unlike the pixel median.
    checker=np.indices((48,48)).sum(axis=0)%2
    gray=np.repeat((checker*255).astype(np.uint8)[...,None],3,axis=2)
    half=sample(gray,[0,0,48,48])
    assert np.allclose(half['linearMeanCameraRGB'],.5)
    assert np.allclose(half['areaMeanCameraRGB'],187.516,atol=.01)
    levels=[]
    for fraction in (.25,.75):
        gray=np.zeros((48,48,3),np.uint8)
        gray.reshape(-1,3)[:int(48*48*fraction)]=255
        response=sample(gray,[0,0,48,48])
        levels.append(response['areaMeanCameraRGB'][0])
        assert np.allclose(response['linearMeanCameraRGB'],fraction)
    assert levels[1]-levels[0]>80
    high=np.repeat((((np.indices((1440,960)).sum(axis=0)%2)*255).astype(np.uint8))[...,None],3,axis=2)
    quad=[[0,0],[958,0],[958,1438],[0,1438]]
    integral=sample_original(high,quad,[30,110,48,48])
    assert np.allclose(integral['linearMeanCameraRGB'],.5,atol=.002)
    assert integral['samplePixelCount']>48*48*3
    transform=cv2.getPerspectiveTransform(np.float32(quad),np.float32([[0,0],[479,0],[479,719],[0,719]]))
    alias=sample(cv2.warpPerspective(high,transform,(480,720)),[30,110,48,48])
    assert abs(alias['linearMeanCameraRGB'][0]-.5)>.4
    def write(name,data):
        path=tmp_path/name;path.write_text(json.dumps(data));return path
    rects=[[10+i*76,20,64,32] for i in range(6)]
    page={'name':'SC07','referenceRects':rects,'patches':[{'id':336,'sampleRect':[30,110,48,48]},{'id':337,'sampleRect':[110,110,48,48]},{'id':338,'sampleRect':[190,110,48,48]}]}
    manifest=write('manifest.json',{'pages':[page],'recipes':[{'id':i} for i in [336,337,338,339]]})
    groups=[]
    for repeat in range(2):
        image=np.full((720,480,3),180,np.uint8)
        image[100:170,20:90]=[50+repeat,70,90]
        image[100:170,100:170]=[50+repeat,70,90]
        image[100:170,180:250]=[180+repeat,190,200]
        name=f'r{repeat}.jpg';Image.fromarray(image).save(tmp_path/name,quality=100,subsampling=0)
        groups.append({'id':str(repeat),'page':'SC07','panelRepeat':'actual-'+str(repeat),'photos':[name,name]})
    corners=write('corners.json',{'coordinateSpace':'exif-transposed','pages':{'SC07':[[0,0],[479,0],[479,719],[0,719]]}})
    capture=write('captures.json',{'groups':groups[:1]})
    first=measure(manifest,capture,corners,tmp_path/'first.json')
    assert first['raw']['eligibleWithTwoPanelRepeats']==0
    assert first['raw']['candidateGroupCount'] is None
    capture=write('captures.json',{'groups':groups})
    result=measure(manifest,capture,corners,tmp_path/'result.json')
    assert result['coverage']['missingPatchIds']==[339]
    assert result['raw']['eligibleWithTwoPanelRepeats']==3
    assert result['raw']['candidateGroupCount']==2
    assert result['raw']['pairsBelowEngineeringThreshold'][0]['ids']==[336,337]
    assert np.allclose(result['raw']['entries'][0]['perRepeatRGB']['actual-0'],[50,70,90],atol=2)
    assert np.allclose(result['groups'][1]['gain'],1)
    assert len(result['raw']['allPairs'])==3
    assert not result['raw']['all512PassEngineeringThreshold']


    # Unexpected IDs must not pass exact manifest identity, even with enough repeats.
    wrong=summarize({999:{'r1':[[30,30,30]],'r2':[[30,30,30]]}}, {336})
    assert not wrong['allExpectedPassEngineeringThreshold']
    assert wrong['extraObservedIds']==[999]
    reused=[dict(groups[0]),dict(groups[1])]
    reused[1]['photos']=reused[0]['photos']
    capture=write('captures.json',{'groups':reused})
    audit=measure(manifest,capture,corners,tmp_path/'reuse.json')['evidenceAudit']
    assert len(audit['crossPanelRepeatDuplicatePhotoHashes'])==1
    assert not audit['verifiedIndependentPanelRefreshes']

    # Per-photo corners override group/page while old page-only inputs above work.
    shifted=[[1,1],[478,1],[478,718],[1,718]]
    mapped=write('photo-corners.json',{'coordinateSpace':'exif-transposed',
        'pages':{'SC07':[[0,0],[479,0],[479,719],[0,719]]},
        'photos':{'r0.jpg':shifted,str((tmp_path/'r1.jpg').resolve()):[[0,0],[479,0],[479,719],[0,719]]}})
    capture=write('captures.json',{'groups':groups})
    report=measure(manifest,capture,mapped,tmp_path/'mapped.json')
    assert report['groups'][0]['photos'][0]['actualInnerScreenCorners']==shifted
    assert report['groups'][1]['photos'][0]['actualInnerScreenCorners']==[[0,0],[479,0],[479,719],[0,719]]
