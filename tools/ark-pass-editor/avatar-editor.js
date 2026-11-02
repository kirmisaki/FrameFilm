/* SPDX-License-Identifier: GPL-3.0-or-later */
// Uses the same Film six-color algorithms as ForFilm; never 46/55-color indices.
window.PassportAvatarEditor = (() => {
  const defaults = {turns:0,zoom:1,x:0,y:0,brightness:0,contrast:1,saturation:1,dither:true,strength:1,algorithm:'adaptive'};
  function validateEdit(edit){
    if(!edit || typeof edit.source!=='string' || !edit.source.startsWith('data:image/png;base64,') || edit.source.length>6000000)throw new Error('头像源图无效');
    const options={...defaults,...edit.options};
    for(const [key,min,max] of [['turns',0,3],['zoom',.1,8],['x',-1024,1024],['y',-1024,1024],['brightness',-1,1],['contrast',.5,2],['saturation',0,3],['strength',0,3]])if(!Number.isFinite(options[key])||options[key]<min||options[key]>max)throw new Error('头像调整参数无效');
    if(typeof options.dither!=='boolean'||!['adaptive','floydSteinberg','atkinson','bayer'].includes(options.algorithm))throw new Error('头像算法无效');
    return {source:edit.source,options};
  }
  async function image(source) { const img=new Image();img.src=source;await img.decode();return img; }
  // Film MonoFast：与 App 一致，按 RGB 到黑/白的距离量化；开启抖动时使用 Floyd-Steinberg。
  function monoFast(imageData,dither){
    const width=imageData.width,height=imageData.height,data=imageData.data;
    let current=new Float64Array((width+2)*3),next=new Float64Array((width+2)*3);
    for(let y=0;y<height;y++){
      for(let x=0;x<width;x++){
        const pixel=(y*width+x)*4,offset=(x+1)*3,channels=[0,0,0];
        for(let c=0;c<3;c++)channels[c]=Math.max(0,Math.min(255,data[pixel+c]+current[offset+c]));
        const black=channels[0]*channels[0]+channels[1]*channels[1]+channels[2]*channels[2];
        const white=(channels[0]-255)*(channels[0]-255)+(channels[1]-255)*(channels[1]-255)+(channels[2]-255)*(channels[2]-255);
        const color=black<white?0:255;
        for(let c=0;c<3;c++){
          data[pixel+c]=color;
          if(dither){
            const error=channels[c]-color;
            current[offset+3+c]+=error*7/16;
            next[offset-3+c]+=error*3/16;
            next[offset+c]+=error*5/16;
            next[offset+3+c]+=error/16;
          }
        }
        data[pixel+3]=255;
      }
      const old=current;current=next;next=old;next.fill(0);
    }
  }
  async function sourceFromFile(file) {
    if (!file || !file.type.startsWith('image/') || file.size>8*1024*1024) throw new Error('请选择不超过8MiB的图片');
    const url=URL.createObjectURL(file);
    try {
      const img=await image(url), ratio=Math.min(1,640/Math.max(img.width,img.height));
      const canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(img.width*ratio));canvas.height=Math.max(1,Math.round(img.height*ratio));
      const c=canvas.getContext('2d');c.fillStyle='#fff';c.fillRect(0,0,canvas.width,canvas.height);c.drawImage(img,0,0,canvas.width,canvas.height);
      const source=canvas.toDataURL('image/png');if(source.length>6000000)throw new Error('头像源图过大');return source;
    } finally {URL.revokeObjectURL(url);}
  }
  function render(img,o,mode) {
    const c=document.createElement('canvas');c.width=128;c.height=160;
    const g=c.getContext('2d',{willReadFrequently:true});g.fillStyle='#fff';g.fillRect(0,0,128,160);
    const scale=Math.max(128/(o.turns%2?img.height:img.width),160/(o.turns%2?img.width:img.height))*o.zoom;
    g.save();g.translate(64+o.x,80+o.y);g.rotate(o.turns*Math.PI/2);g.scale(scale,scale);g.drawImage(img,-img.width/2,-img.height/2);g.restore();
    const crop=c.toDataURL('image/png'), p=g.getImageData(0,0,128,160);
    for(let i=0;i<p.data.length;i+=4)for(let k=0;k<3;k++)p.data[i+k]=Math.round(Math.max(0,Math.min(255,p.data[i+k]+o.brightness*255)));
    adjustContrast(p,o.contrast);adjustSaturation(p,mode==='color'?o.saturation:0);
    if(mode==='mono') monoFast(p,o.dither);
    else if(o.dither){
      if(o.algorithm==='adaptive')adaptiveDither(p);
      else applyDitherByType(p,o.algorithm,o.strength);
    }
    else if(mode==='color'){
      for(let i=0;i<p.data.length;i+=4){
        const rgb=findClosestColor(p.data[i],p.data[i+1],p.data[i+2]);
        p.data[i]=rgb.r;p.data[i+1]=rgb.g;p.data[i+2]=rgb.b;
      }
    }
    else monoFast(p,false);
    g.putImageData(p,0,0);return {crop,processed:c.toDataURL('image/png')};
  }
  async function processed(edit,mode){return render(await image(edit.source),{...defaults,...edit.options},mode).processed;}
  async function open(edit,mode) {
    const img=await image(edit.source);let options={...defaults,...edit.options}, result;
    const d=document.createElement('dialog');d.style.cssText='max-width:440px;width:calc(100% - 32px);max-height:90vh;overflow:auto';
    d.innerHTML='<h2>调整头像</h2><p>左侧原图构图可拖动；右侧为设备像素</p><div style="display:flex;justify-content:center;gap:8px"><canvas data-crop width="128" height="160" style="touch-action:none;width:128px;height:160px"></canvas><img data-output width="128" height="160"></div><div><button data-rotate>旋转90度</button><button data-reset>重置调整</button></div><div data-sliders></div><label><input data-dither type="checkbox">抖动</label><select data-algorithm><option value="adaptive">自适应（自动选择算法与强度）</option><option value="floydSteinberg">Floyd-Steinberg</option><option value="atkinson">Atkinson</option><option value="bayer">Bayer 4×4</option></select><p data-adaptive-hint>自适应会根据头像自动选择抖动算法与强度。</p><p data-mono-hint>黑白头像使用 Film MonoFast：开启抖动为 Floyd-Steinberg，关闭时直接黑白量化。</p><p><button data-cancel>取消</button><button data-apply>应用头像</button></p>';
    const controls={};
    for(const [key,title,min,max] of [['zoom','缩放',.1,8],['brightness','亮度',-1,1],['contrast','对比度',.5,2],['saturation','饱和度',0,3],['strength','抖动强度',0,3]]){
      const label=document.createElement('label');label.style.display='block';label.innerHTML=`${title} <output></output><input type="range" min="${min}" max="${max}" step="0.05" style="width:100%">`;
      if(key==='strength')label.dataset.strength='';
      controls[key]=label.querySelector('input');controls[key].addEventListener('input',()=>{options[key]=Number(controls[key].value);refresh();});d.querySelector('[data-sliders]').append(label);
    }
    const canvas=d.querySelector('[data-crop]'),g=canvas.getContext('2d');let renderGeneration=0;
    function refresh(){
      result=render(img,options,mode);d.querySelector('[data-output]').src=result.processed;
      const generation=++renderGeneration,preview=new Image();preview.onload=()=>{if(generation===renderGeneration)g.drawImage(preview,0,0);};preview.src=result.crop;
      for(const key of Object.keys(controls)){controls[key].value=options[key];controls[key].previousElementSibling.value=options[key].toFixed(2);}
      d.querySelector('[data-dither]').checked=options.dither;d.querySelector('[data-algorithm]').value=options.algorithm;
      const mono=mode==='mono';
      d.querySelector('[data-algorithm]').style.display=mono?'none':'';
      d.querySelector('[data-mono-hint]').hidden=!mono;
      d.querySelector('[data-strength]').style.display=!mono&&options.dither&&options.algorithm!=='adaptive'?'block':'none';
      d.querySelector('[data-adaptive-hint]').hidden=mono||!options.dither||options.algorithm!=='adaptive';
    }
    let previous=null;
    canvas.addEventListener('pointerdown',e=>{canvas.setPointerCapture(e.pointerId);previous={x:e.clientX,y:e.clientY};});
    canvas.addEventListener('pointermove',e=>{if(!previous)return;options.x=Math.max(-1024,Math.min(1024,options.x+e.clientX-previous.x));options.y=Math.max(-1024,Math.min(1024,options.y+e.clientY-previous.y));previous={x:e.clientX,y:e.clientY};refresh();});
    for(const event of ['pointerup','pointercancel','lostpointercapture'])canvas.addEventListener(event,()=>previous=null);
    d.querySelector('[data-rotate]').onclick=()=>{options.turns=(options.turns+1)%4;options.x=options.y=0;refresh();};
    d.querySelector('[data-reset]').onclick=()=>{options={...defaults};refresh();};
    d.querySelector('[data-dither]').onchange=e=>{options.dither=e.target.checked;refresh();};
    d.querySelector('[data-algorithm]').onchange=e=>{options.algorithm=e.target.value;refresh();};
    document.body.append(d);refresh();d.showModal();
    // iframe 会被宿主拉成整页高度，fixed 弹窗需要贴住当前可见区域而不是整页居中。
    const host=window.parent;let scrollLock=null;
    function place(){
      try{
        if(host===window||!window.frameElement)return;
        const rect=window.frameElement.getBoundingClientRect();
        d.style.top=Math.max(0,-rect.top+12)+'px';
        d.style.bottom='auto';d.style.marginTop='0';
        d.style.maxHeight=Math.max(240,host.innerHeight-110)+'px';
      }catch(_){}
    }
    function lock(active){
      try{
        if(host===window)return;
        const root=host.document.documentElement,content=host.document.getElementById('page-content');
        if(active){
          scrollLock={root:root.style.overflow,content:content?content.style.overflow:null};
          root.style.overflow='hidden';if(content)content.style.overflow='hidden';
        }else if(scrollLock){
          root.style.overflow=scrollLock.root;
          if(content&&scrollLock.content!==null)content.style.overflow=scrollLock.content;
          scrollLock=null;
        }
      }catch(_){}
    }
    place();lock(true);
    host.addEventListener('scroll',place,true);host.addEventListener('resize',place);
    return new Promise(resolve=>{
      const close=value=>{
        host.removeEventListener('scroll',place,true);host.removeEventListener('resize',place);
        lock(false);d.close();d.remove();resolve(value);
      };
      d.querySelector('[data-cancel]').onclick=()=>close(null);d.oncancel=e=>{e.preventDefault();close(null);};
      d.querySelector('[data-apply]').onclick=()=>close({edit:{source:edit.source,options:{...options}},png:result.processed});
    });
  }
  return {defaults,sourceFromFile,processed,open,render,validateEdit};
})();
