"""Loopback-only, exact full-resolution tone-fitting lab preview and film export."""
import argparse
import base64
import hashlib
from functools import lru_cache
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
from pathlib import Path
import threading
import time

import numpy as np
from PIL import Image, ImageDraw, ImageOps

from abc512_render import linear, preview, shared_floor
from abc9_lab_io import pack_abc9_film
from abc_spatial_mix import decompose, sample_mixture
from epd_source_fit import fit_source


def tone_card(photo):
    image = Image.new('RGB', (480, 720), 'white')
    draw = ImageDraw.Draw(image)
    draw.ellipse((12, 16, 22, 26), fill='black')
    draw.text((32, 16), 'ARK TONE / TOP', fill='black')
    image.paste(photo.crop((0,240,480,600)),(0,38))
    for y,values,title in [(426,[0,4,8,12,16,20,24,32],'SHADOW 0..32'),
                           (484,[96,104,112,120,128,140,150,160],'MID 96..160'),
                           (542,[223,231,237,243,247,251,253,255],'HIGHLIGHT 223..255')]:
        draw.text((20,y-17),title,fill='black')
        for i,v in enumerate(values):
            x=48+i*48
            draw.rectangle((x,y,x+47,y+31),fill=(v,v,v))
    skin=[(245,220,200),(232,190,150),(210,153,110),(200,200,200),(100,150,210),(200,220,240)]
    draw.text((20,594),'WARM TARGETS / NEUTRAL / COOL CONTROLS',fill='black')
    for i,color in enumerate(skin):
        x=48+i*64
        draw.rectangle((x,616,x+63,679),fill=color)
    draw.text((20,697),'SOURCE RGB TARGETS / CAMERA-RELATIVE LAB',fill='black')
    return image


def png_data(array):
    stream=BytesIO()
    Image.fromarray(array).save(stream,format='PNG')
    return 'data:image/png;base64,'+base64.b64encode(stream.getvalue()).decode('ascii')


class Engine:
    def __init__(self,palette,image,crop,output):
        self.palette_path=palette
        data=json.loads(palette.read_text(encoding='utf-8'))
        white=linear(data['normalization']['whiteCameraRGB'])
        self.colors=linear([e['cameraRGB'] for e in data['entries']])/white
        self.codes=np.asarray([e['code'] for e in data['entries']],dtype=np.int32)
        self.floor=shared_floor(data,white)
        with Image.open(image) as original:
            photo=ImageOps.exif_transpose(original).convert('RGB')
            x0,y0,x1,y1=crop
            if not (0<=x0<x1<=photo.width and 0<=y0<y1<=photo.height):
                raise ValueError('crop is outside the source image')
            photo=photo.crop(crop).resize((480,720),Image.Resampling.LANCZOS)
        self.images={'photo':np.asarray(photo),'card':np.asarray(tone_card(photo))}
        self.unique_pixels={}
        for name,rgb in self.images.items():
            _,first,inverse=np.unique(rgb.reshape(-1,3),axis=0,return_index=True,return_inverse=True)
            self.unique_pixels[name]=(first,inverse)
        self.output=output
        self.lock=threading.Lock()
        self.counter=0

    def compute(self,source,strength,warm,bayer):
        target,fit=fit_source(self.images[source],self.colors,self.floor,strength,warm)
        first,inverse=self.unique_pixels[source]
        # Exact RGB duplicates have identical preprocessing and geometry. Reuse
        # their mixture, then expand BEFORE coordinate-dependent Bayer sampling.
        vertices,weights,mapped,geo=decompose(target.reshape(-1,3)[first][None,:,:],self.colors)
        shape=target.shape[:2]
        vertices=vertices.reshape(-1,4)[inverse].reshape(*shape,4)
        weights=weights.reshape(-1,4)[inverse].reshape(*shape,4)
        mapped=mapped.reshape(-1,3)[inverse].reshape(target.shape)
        selected=sample_mixture(vertices,weights,bayer)
        codes=self.codes[selected]
        result={'target':png_data(preview(target)),'screen':png_data(preview(self.colors[selected])),
                'fit':fit,'gamut':{'outsidePixels':int(np.count_nonzero(np.max(np.abs(mapped-target),axis=-1)>1e-9)),
                    'uniqueSourceColors':len(first),
                    'minimumRadialScale':geo['minimumRadialScale']}}
        return result,codes

    @lru_cache(maxsize=12)
    def cached(self,source,strength,warm,bayer):
        return self.compute(source,strength,warm,bayer)[0]

    def render(self,params):
        started=time.perf_counter()
        source,strength,warm,bayer=params
        with self.lock:
            panels=[self.cached(source,0.,0.,bayer),self.cached(source,strength,0.,bayer),
                    self.cached(source,strength,warm,bayer)]
        return {'panels':panels,'source':png_data(self.images[source]),
                'elapsedMs':round((time.perf_counter()-started)*1000),
                'params':{'source':source,'range':strength,'warm':warm,'bayer':bayer},
                'meaning':'Full 480x720 exact experimental pipeline; camera-relative simulation, not a photograph'}

    def export(self,params):
        source,strength,warm,bayer=params
        with self.lock:
            self.counter+=1
            directory=self.output/f'run-{time.strftime("%Y%m%d-%H%M%S")}-{self.counter:02d}'
            directory.mkdir(parents=True)
            records=[]
            for label,r,w in [('B',0.,0.),('L',strength,0.),('W',strength,warm)]:
                result,codes=self.compute(source,r,w,bayer)
                name=f'T{time.strftime("%m%d")}{label}{self.counter:02d}.film'
                blob=pack_abc9_film(Image.fromarray(codes))
                (directory/name).write_bytes(blob)
                records.append({'name':name,'range':r,'warm':w,'bayer':bayer,'bytes':len(blob),
                                'sha256':hashlib.sha256(blob).hexdigest(),'fit':result['fit']})
            manifest={'source':source,'paletteSha256':hashlib.sha256(self.palette_path.read_bytes()).hexdigest(),
                      'imageRgbSha256':hashlib.sha256(self.images[source].tobytes()).hexdigest(),
                      'privateFormat':'ABC9; requires the archived diagnostic firmware',
                      'versionDisplay':'3.2.5','files':records,'claims512StableColors':False}
            (directory/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
            return {'directory':str(directory),'files':records}


def parameters(body):
    source=body.get('source','photo')
    strength=float(body.get('range',1.))
    warm=float(body.get('warm',0.))
    bayer=int(body.get('bayer',16))
    if source not in ('photo','card') or not np.isfinite([strength,warm]).all() or not 0<=strength<=1 or not 0<=warm<=1 or bayer not in (8,16):
        raise ValueError('Invalid source, strengths (0..1), or Bayer size (8/16)')
    return source,strength,warm,bayer


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--palette',required=True,type=Path)
    parser.add_argument('--image',required=True,type=Path)
    parser.add_argument('--crop',required=True,nargs=4,type=int)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--port',default=8775,type=int)
    args=parser.parse_args()
    try: engine=Engine(args.palette,args.image,args.crop,args.output)
    except ValueError as exc: parser.error(str(exc))
    assets=Path(__file__).parent/'tone-preview'

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self,*a,**kw):
            super().__init__(*a,directory=str(assets),**kw)

        def do_POST(self):
            try:
                body=json.loads(self.rfile.read(int(self.headers.get('Content-Length',0))))
                if self.path=='/api/render': result=engine.render(parameters(body))
                elif self.path=='/api/export': result=engine.export(parameters(body))
                elif self.path=='/api/clear-cache':
                    with engine.lock: engine.cached.cache_clear()
                    result={'cleared':True}
                else:
                    self.send_error(404);return
                status=200
            except (ValueError,TypeError,KeyError) as exc:
                status=400;result={'error':str(exc)}
            except Exception as exc:
                status=500;result={'error':str(exc)}
            encoded=json.dumps(result).encode()
            self.send_response(status)
            self.send_header('Content-Type','application/json')
            self.send_header('Content-Length',str(len(encoded)))
            self.end_headers()
            try: self.wfile.write(encoded)
            except (BrokenPipeError,ConnectionResetError): pass

    print(f'Tone lab preview http://127.0.0.1:{args.port}/preview.html',flush=True)
    with ThreadingHTTPServer(('127.0.0.1',args.port),Handler) as server:
        try: server.serve_forever()
        except KeyboardInterrupt: pass


if __name__=='__main__': main()
