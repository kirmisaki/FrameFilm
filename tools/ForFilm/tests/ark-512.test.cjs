const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict');
const context=vm.createContext({window:{},Math,Uint8ClampedArray,Uint8Array,Uint16Array,Float64Array,DataView,Map,Promise,setTimeout,console});
context.window=context;
for(const file of ['ark-512-profile.js','ark-512.js'])vm.runInContext(fs.readFileSync(path.join(__dirname,'../js',file),'utf8'),context);
const api=context.window.Ark512;
(async()=>{
 const native=Uint16Array.from({length:720*480},(_,i)=>i%512),bytes=api.pack(native,720,480);
 assert.equal(bytes.length,691232);assert.equal(bytes[9],127);assert.deepEqual([...bytes.slice(12,16)],[65,66,67,57]);
 assert.deepEqual([...api.parse(bytes).codes],[...native]);
 api.resetCapabilities();assert.throws(()=>api.assertUpload(bytes),/512 色预览文件/);
 api.setCapabilities(Uint8Array.from([1,0,0,0,31]));assert.equal(api.supported(),false);
 api.setCapabilities(Uint8Array.from([1,0,0,0,63]));assert.doesNotThrow(()=>api.assertUpload(bytes));
 api.setCapabilities(Uint8Array.from([2,0,0,0,63]));assert.equal(api.supported(),false);
 api.setCapabilities(Uint8Array.from([1,0,0,0,32]));assert.equal(api.supported(),true);
 api.resetCapabilities();assert.equal(api.supported(),false);
 for(const offset of [9,10,12]){const bad=bytes.slice();bad[offset]=0;assert.throws(()=>api.parse(bad));}
 const badCode=bytes.slice();badCode[32]=0;badCode[33]=2;assert.throws(()=>api.parse(badCode),/0..511/);
 let ticks=0;const cancelImage={width:8,height:8,data:new Uint8ClampedArray(8*8*4)};
 await assert.rejects(api.render(cancelImage,{cancelled:()=>++ticks>1}),/取消/);
 // Identical simulated RGB does not make different physical ABC tuples interchangeable.
 // Nearest still chooses the first palette index; only explicit archived aliases change its code.
 const realProfile=context.Ark512Profile;
 const synthetic={floor:0,center:[0,0,0],hullPlanes:[],recipeLinear:[[.2,.1,.3]],
   outputCodes:[16,468,387,6,407],outputLinear:[[.2,.1,.3],[.2,.1,.3],[0,0,0],[.5,.5,.5],[1,1,1]],
   shadowCodes:[387,6,407],codeAliases:{16:468}};
 context.Ark512Profile=synthetic;
 const one={width:1,height:1,data:Uint8ClampedArray.from([0,0,255,255])};
 const historical=await api.render(one);
 assert.equal(historical.codes[0],468);
 synthetic.codeAliases={};
 const firstIndex=await api.render(one);
 assert.equal(firstIndex.codes[0],16);
 assert.deepEqual([...historical.rgba],[...firstIndex.rgba]);
 context.Ark512Profile=realProfile;
 const continuousFixture=JSON.parse(fs.readFileSync(path.join(__dirname,'../../../app/flutter_app/test/fixtures/ark512_continuous_tone.json'),'utf8'));
 const sample={width:continuousFixture.width,height:continuousFixture.height,data:Uint8ClampedArray.from(continuousFixture.rgba)};
 const defaultResult=await api.render(sample), explicitFrozen=await api.render(sample,{continuousTone:false});
 const continuousResult=await api.render(sample,{continuousTone:true});
 assert.deepEqual([...defaultResult.codes],continuousFixture.expectedCodes);
 assert.deepEqual([...explicitFrozen.codes],continuousFixture.expectedCodes);
 assert.deepEqual([...continuousResult.codes],continuousFixture.continuousCodes);
 assert.notDeepEqual([...continuousResult.codes],[...defaultResult.codes]);
 assert.deepEqual([...sample.data],continuousFixture.rgba);
 assert.deepEqual([...continuousResult.rgba],[...api.preview(continuousResult.codes,sample.width,sample.height)]);
 const controlsFixture=JSON.parse(fs.readFileSync(path.join(__dirname,'../../../app/flutter_app/test/fixtures/ark512_controls.json'),'utf8'));
 const controlImage={width:controlsFixture.width,height:controlsFixture.height,data:Uint8ClampedArray.from(controlsFixture.rgba)};
 for(const item of controlsFixture.cases){
  const result=await api.render(controlImage,item.options);
  assert.deepEqual([...result.codes],item.expectedCodes,item.name);
 }
 for(const key of ['warmChroma','rangeFit','ditherStrength']) for(const value of [NaN,Infinity,-.1,key==='ditherStrength'?5.1:1.1]) {
  await assert.rejects(api.render(controlImage,{[key]:value}),/无效 I006/);
 }
 const independent=await api.render(controlImage,{continuousTone:true,ditherStrength:0});
 for(let i=0;i<independent.codes.length;i++){
  const onePixel=await api.render({width:1,height:1,data:controlImage.data.slice(i*4,i*4+4)},{continuousTone:true,ditherStrength:0});
  assert.equal(independent.codes[i],onePixel.codes[0],'zero strength must not propagate RGB or shadow errors');
 }
 // Root creates this shared Python-reference fixture for both JS and Dart.
 if(process.argv[2]){
  const fixture=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
  const image={width:fixture.width,height:fixture.height,data:Uint8ClampedArray.from(fixture.rgba)};
  const result=await api.render(image);
  assert.deepEqual([...result.codes],fixture.expectedCodes);
  if(fixture.previewRgba)assert.deepEqual([...result.rgba],fixture.previewRgba);
 }
 console.log('PASS exact nearest historical tuple aliases, ABC9 codec, capability gating/reset, malformed inputs, asynchronous cancellation'+(process.argv[2]?', shared I006 reference fixture':''));
})().catch(error=>{console.error(error);process.exitCode=1;});
