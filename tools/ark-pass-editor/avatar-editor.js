/* SPDX-License-Identifier: GPL-3.0-or-later */
// Keep a bounded, unquantized color source. Quantization belongs to the selected card output.
window.PassportAvatarEditor = (() => {
  const defaults = {turns:0,zoom:1,x:0,y:0,brightness:0,rangeFit:0,warmChroma:0,contrast:1,saturation:1,dither:true,strength:1,algorithm:'adaptive'};
  function validateEdit(edit){
    if(!edit || typeof edit.source!=='string' || !edit.source.startsWith('data:image/png;base64,') || edit.source.length>6000000)throw new Error('头像源图无效');
    const options={...defaults,...edit.options};
    for(const [key,min,max] of [['turns',0,3],['zoom',.1,8],['x',-1024,1024],['y',-1024,1024],['brightness',-1,1],['rangeFit',0,1],['warmChroma',0,1],['contrast',.5,2],['saturation',0,3],['strength',0,3]])if(!Number.isFinite(options[key])||options[key]<min||options[key]>max)throw new Error('头像调整参数无效');
    if(typeof options.dither!=='boolean'||!['adaptive','floydSteinberg','atkinson','bayer'].includes(options.algorithm))throw new Error('头像算法无效');
    return {source:edit.source,options};
  }
  async function image(source) { const img=new Image();img.src=source;await img.decode();return img; }
  function highQualityDraw(g, img, x, y, width, height) {
    let source=img, sw=img.width, sh=img.height;
    // Prefilter large reductions instead of letting a single bilinear sample
    // lose hair/diagonal detail. Final crop is sampled at the actual slot size.
    while(sw>Math.abs(width)*2 && sh>Math.abs(height)*2){
      const stage=document.createElement('canvas');stage.width=Math.max(1,Math.floor(sw/2));stage.height=Math.max(1,Math.floor(sh/2));
      const c=stage.getContext('2d',{alpha:false});c.fillStyle='#fff';c.fillRect(0,0,stage.width,stage.height);c.imageSmoothingEnabled=true;c.imageSmoothingQuality='high';c.drawImage(source,0,0,stage.width,stage.height);
      source=stage;sw=stage.width;sh=stage.height;
    }
    g.imageSmoothingEnabled=true;g.imageSmoothingQuality='high';g.drawImage(source,x,y,width,height);
  }
  async function sourceFromFile(file) {
    if (!file || !file.type.startsWith('image/') || file.size>8*1024*1024) throw new Error('请选择不超过8MiB的图片');
    const url=URL.createObjectURL(file);
    try {
      const img=await image(url), ratio=Math.min(1,2048/Math.max(img.width,img.height));
      const canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(img.width*ratio));canvas.height=Math.max(1,Math.round(img.height*ratio));
      const c=canvas.getContext('2d',{alpha:false});c.fillStyle='#fff';c.fillRect(0,0,canvas.width,canvas.height);highQualityDraw(c,img,0,0,canvas.width,canvas.height);
      let source=canvas.toDataURL('image/png');
      while(source.length>6000000 && Math.max(canvas.width,canvas.height)>640){
        canvas.width=Math.max(1,Math.floor(canvas.width*.8));canvas.height=Math.max(1,Math.floor(canvas.height*.8));
        c.fillStyle='#fff';c.fillRect(0,0,canvas.width,canvas.height);highQualityDraw(c,img,0,0,canvas.width,canvas.height);source=canvas.toDataURL('image/png');
      }
      if(source.length>6000000)throw new Error('头像源图过大');return source;
    } finally {URL.revokeObjectURL(url);}
  }
  function render(img,o,size={width:128,height:160}) {
    const {width,height}=size;
    const c=document.createElement('canvas');c.width=width;c.height=height;
    const g=c.getContext('2d',{willReadFrequently:true,alpha:false});g.fillStyle='#fff';g.fillRect(0,0,width,height);
    const scale=Math.max(width/(o.turns%2?img.height:img.width),height/(o.turns%2?img.width:img.height))*o.zoom;
    g.save();g.translate(width/2+o.x,height/2+o.y);g.rotate(o.turns*Math.PI/2);
    highQualityDraw(g,img,-img.width*scale/2,-img.height*scale/2,img.width*scale,img.height*scale);g.restore();
    const p=g.getImageData(0,0,width,height);
    for(let i=0;i<p.data.length;i+=4)for(let k=0;k<3;k++)p.data[i+k]=Math.round(Math.max(0,Math.min(255,p.data[i+k]+o.brightness*255)));
    adjustContrast(p,o.contrast);adjustSaturation(p,o.saturation);
    FrameFilmSourceFit.apply(p,o.rangeFit,o.warmChroma,FrameFilmSourceFit.profiles.fast);
    g.putImageData(p,0,0);
    const png=c.toDataURL('image/png');
    return {crop:png,processed:png,pixels:p};
  }
  async function processed(edit,size){return render(await image(edit.source),{...defaults,...edit.options},size).processed;}
  async function open(edit,mode,config={width:128,height:160}) {
    const img=await image(edit.source);let options={...defaults,...edit.options}, result;
    const d=document.createElement('dialog');d.className='avatar-edit-dialog';d.setAttribute('aria-labelledby','avatar-edit-title');
    d.innerHTML='<h2 id="avatar-edit-title">调整彩色头像</h2><p class="hint">左侧为未量化彩色构图，可拖动；右侧为所选输出的设备像素（模型色彩）。</p><div class="avatar-edit-previews"><canvas data-crop aria-label="彩色构图"></canvas><canvas data-output aria-label="设备像素预览"></canvas></div><div class="actions"><button class="secondary" data-rotate>旋转90度</button><button class="secondary" data-reset>重置调整</button></div><div data-sliders></div><p class="hint" data-status role="status"></p><div class="actions"><button class="secondary" data-cancel>取消</button><button class="primary" data-apply>应用头像</button></div>';
    const controls={};
    for(const [key,title,min,max] of [['zoom','缩放',.1,8],['brightness','亮度',-1,1],['rangeFit','明暗范围拟合',0,1],['warmChroma','暖色收缩',0,1],['contrast','对比度',.5,2],['saturation','饱和度',0,3]]){
      const label=document.createElement('label');label.innerHTML=`${title} <output></output><input type="range" min="${min}" max="${max}" step="0.05">`;
      controls[key]=label.querySelector('input');controls[key].addEventListener('input',()=>{options[key]=Number(controls[key].value);refresh();});d.querySelector('[data-sliders]').append(label);
    }
    const canvas=d.querySelector('[data-crop]'),g=canvas.getContext('2d'),output=d.querySelector('[data-output]');
    for(const c of [canvas,output]){c.width=config.width;c.height=config.height;}
    let renderGeneration=0,timer,closed=false;
    function refresh(){
      const generation=++renderGeneration;clearTimeout(timer);
      result=render(img,options,config);g.putImageData(result.pixels,0,0);
      output.getContext('2d').clearRect(0,0,output.width,output.height);
      d.querySelector('[data-apply]').disabled=true;d.querySelector('[data-status]').textContent='正在生成设备像素…';
      const pixels=result.pixels,cancelled=()=>closed||generation!==renderGeneration;
      timer=setTimeout(async()=>{
        try{
          const preview=config.preview?await config.preview(pixels,cancelled):await PassportPixels.avatar(pixels,mode,{cancelled});
          if(cancelled())return;
          output.getContext('2d').putImageData(new ImageData(preview.rgba,config.width,config.height),0,0);
          d.querySelector('[data-apply]').disabled=false;d.querySelector('[data-status]').textContent='';
        }catch(error){if(!cancelled())d.querySelector('[data-status]').textContent=error.message;}
      },120);
      for(const key of Object.keys(controls)){controls[key].value=options[key];controls[key].previousElementSibling.value=options[key].toFixed(2);}
    }
    let previous=null;
    canvas.addEventListener('pointerdown',e=>{canvas.setPointerCapture(e.pointerId);previous={x:e.clientX,y:e.clientY};});
    canvas.addEventListener('pointermove',e=>{if(!previous)return;const scale=canvas.width/canvas.clientWidth;options.x=Math.max(-1024,Math.min(1024,options.x+(e.clientX-previous.x)*scale));options.y=Math.max(-1024,Math.min(1024,options.y+(e.clientY-previous.y)*scale));previous={x:e.clientX,y:e.clientY};refresh();});
    for(const event of ['pointerup','pointercancel','lostpointercapture'])canvas.addEventListener(event,()=>previous=null);
    d.querySelector('[data-rotate]').onclick=()=>{options.turns=(options.turns+1)%4;options.x=options.y=0;refresh();};
    d.querySelector('[data-reset]').onclick=()=>{options={...defaults};refresh();};
    document.body.append(d);refresh();d.showModal();
    // iframe 会被宿主拉成整页高度，fixed 弹窗需要贴住当前可见区域而不是整页居中。
    const host=window.parent;let scrollLock=null;
    function place(){
      try{
        if(host===window||!window.frameElement)return;
        const rect=window.frameElement.getBoundingClientRect();
        const narrow=host.matchMedia('(max-width: 899px)').matches;
        const content=host.document.getElementById('page-content');
        const nav=host.document.querySelector('.bottom-nav');
        const top=Math.max(rect.top,narrow&&content?content.getBoundingClientRect().top:0)+12;
        const bottom=Math.min(rect.bottom,narrow&&nav?nav.getBoundingClientRect().top:host.innerHeight)-12;
        d.style.top=(top-rect.top)+'px';
        d.style.bottom='auto';d.style.marginTop='0';
        d.style.maxHeight=Math.max(0,bottom-top)+'px';
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
    if(host!==window&&window.frameElement){
      const frame=window.frameElement;
      if(frame.getBoundingClientRect().top>host.innerHeight/2) frame.scrollIntoView({block:'start'});
    }
    lock(true);place();
    host.addEventListener('scroll',place,true);host.addEventListener('resize',place);
    return new Promise(resolve=>{
      const close=value=>{
        closed=true;++renderGeneration;clearTimeout(timer);
        host.removeEventListener('scroll',place,true);host.removeEventListener('resize',place);
        lock(false);d.close();d.remove();resolve(value);
      };
      d.querySelector('[data-cancel]').onclick=()=>close(null);d.oncancel=e=>{e.preventDefault();close(null);};
      d.querySelector('[data-apply]').onclick=()=>close({edit:{source:edit.source,options:{...options}},png:result.processed});
    });
  }
  return {defaults,sourceFromFile,processed,open,render,validateEdit};
})();
