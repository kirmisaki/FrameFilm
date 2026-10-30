const fs=require('node:fs');const vm=require('node:vm');const assert=require('node:assert/strict');const path=require('node:path');
const context=vm.createContext({window:{},console,Uint8ClampedArray,Uint8Array,Float32Array,Math});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../ForFilm/js/source-fit.js'),'utf8'),context);
context.FrameFilmSourceFit=context.window.FrameFilmSourceFit;
vm.runInContext(fs.readFileSync(path.join(__dirname,'../ForFilm/js/convert.js'),'utf8'),context);
vm.runInContext(fs.readFileSync(path.join(__dirname,'avatar-editor.js'),'utf8'),context);
const editor=context.window.PassportAvatarEditor;
assert.equal(editor.defaults.dither,true);assert.equal(editor.defaults.algorithm,'adaptive');
const valid=editor.validateEdit({source:'data:image/png;base64,AA==',options:{zoom:2,turns:1}});
assert.equal(valid.options.zoom,2);assert.equal(valid.options.turns,1);
assert.equal(valid.options.algorithm,'adaptive');
assert.equal(editor.validateEdit({source:valid.source,options:{algorithm:'floydSteinberg'}}).options.algorithm,'floydSteinberg');
assert.throws(()=>editor.validateEdit({source:valid.source,options:{algorithm:'colorQual'}}));
assert.throws(()=>editor.validateEdit({source:valid.source,options:{zoom:NaN}}));
// Exercise the exact Film algorithms the editor uses on a skin-tone field.
const pixels=()=>({width:128,height:160,data:Uint8ClampedArray.from({length:128*160*4},(_,i)=>[213,165,140,255][i%4])});
for(const algorithm of ['floydSteinberg','atkinson','bayer']){
  const p=pixels();context.applyDitherByType(p,algorithm,1);
  const colors=new Set();for(let i=0;i<p.data.length;i+=4)colors.add([...p.data.slice(i,i+3)].join(','));
  assert.ok(colors.size>1,algorithm+' retains tonal detail via spatial dithering');
}
// The editor must invoke Film's adaptive implementation without applying the stored strength.
const calls=[];
context.adaptiveDither=p=>{calls.push(['adaptive',p.width,p.height]);return p;};
context.applyDitherByType=(p,type,strength)=>{calls.push([type,strength]);return p;};
context.document={createElement:()=>({width:0,height:0,toDataURL:()=>valid.source,getContext:()=>({fillRect(){},save(){},translate(){},rotate(){},scale(){},drawImage(){},restore(){},putImageData(){},getImageData:()=>pixels()})})};
const img={width:128,height:160};
editor.render(img,{...editor.defaults,strength:2},'color');
assert.deepEqual(calls.pop(),['adaptive',128,160]);
editor.render(img,{...editor.defaults,algorithm:'floydSteinberg',strength:2},'color');
assert.deepEqual(calls.pop(),['floydSteinberg',2]);
// Source fitting consumes the manually adjusted pixels, before quantization.
const beforeFit=[];
context.FrameFilmSourceFit.apply=p=>{beforeFit.push([...p.data.slice(0,3)]);return p;};
editor.render(img,{...editor.defaults,brightness:.1,contrast:1.5,saturation:1,rangeFit:.5,warmChroma:.5},'color');
assert.deepEqual(beforeFit.pop(),[255,222,185]);
assert.deepEqual(calls.pop(),['adaptive',128,160]);
console.log('Avatar options and Film dithering dispatch passed');
