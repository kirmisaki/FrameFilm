const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict');
const nodes=new Map(),timers=[];
function node(id){if(!nodes.has(id))nodes.set(id,{value:'0',textContent:'',style:{},hidden:false,disabled:false,checked:false,listeners:{},addEventListener(type,fn){this.listeners[type]=fn;},querySelector(){return {disabled:false};}});return nodes.get(id);}
const context=vm.createContext({console,Math,Uint8Array,Uint16Array,Uint8ClampedArray,Float64Array,DataView,Map,Promise,
 setTimeout:fn=>{timers.push(fn);return timers.length;},clearTimeout:()=>{},document:{getElementById:node,querySelectorAll:()=>[]},
 is8bppAvailable:()=>true,syncSzEnhancedAvailability:()=>{},updateCanvasScale:()=>{},addEventListener:()=>{}});
context.window=context;
for(const file of ['source-fit.js','convert.js'])vm.runInContext(fs.readFileSync(path.join(__dirname,'../js',file),'utf8'),context,{filename:file});
node('ditherType').value='colorQual';node('warmChroma').value='.6';node('ditherStrength').value='1';node('rangeFit').value='0';node('contrast').value='1';node('saturation').value='1';
context.initConvertTool();
const event=(id,type)=>node(id).listeners[type].call(node(id));
function select(value){node('ditherType').value=value;event('ditherType','change');}
select('ark512I006');
assert.equal(Number(node('warmChroma').value),.25);
assert.equal(node('ditherStrengthContainer').style.display,'');
assert.equal(node('sourceFitContainer').style.display,'');
for(const id of ['contrast','saturation','ditherStrength','rangeFit','warmChroma'])assert.equal(node(id).disabled,false);
for(const [id,value] of [['warmChroma','.85'],['rangeFit','.7'],['ditherStrength','2.3'],['contrast','1.4'],['saturation','1.2']]){
 const generation=context.ark512Generation;context.ark512Result=Uint8Array.of(1);node(id).value=value;event(id,'input');
 assert.ok(context.ark512Generation>generation,id+' must invalidate old work');assert.equal(context.ark512Result,null);
}
select('colorQual');assert.equal(Number(node('warmChroma').value),.6);
select('ark512I006');assert.equal(Number(node('warmChroma').value),.85);
assert.equal(Number(node('rangeFit').value),.7);assert.equal(Number(node('ditherStrength').value),2.3);
let captured;
context.Ark512={render:async(image,options)=>{captured={image,options};return {codes:new Uint16Array(image.width*image.height),rgba:new Uint8ClampedArray(image.data)};},pack:()=>new Uint8Array(691232)};
context.ImageData=class {constructor(data,width,height){this.data=data;this.width=width;this.height=height;}};
const drawing={clearRect(){},save(){},restore(){},fillRect(){},drawImage(){},putImageData(){},getImageData(){return new context.ImageData(Uint8ClampedArray.from([100,120,140,255,100,120,140,255,100,120,140,255,100,120,140,255]),2,2);}};
node('canvas').getContext=()=>drawing;context.originalImage={width:2,height:2};node('ark512ContinuousTone').checked=true;
(async()=>{
 context.updateImage();await new Promise(resolve=>setImmediate(resolve));
 assert.equal(captured.options.warmChroma,.85);assert.equal(captured.options.rangeFit,.7);assert.equal(captured.options.ditherStrength,2.3);assert.equal(captured.options.continuousTone,true);
 assert.deepEqual([...captured.image.data.slice(0,4)],[84,118,152,255],'contrast and saturation must reach the I006 input');
 assert.equal(captured.options.cancelled(),false);context.cancelArk512Preview();assert.equal(captured.options.cancelled(),true);
 const imported=Uint8Array.from([9,8,7]),result=Uint8Array.from([6,5,4]);context.ark512Imported=imported;context.ark512Result=result;context.syncSourceFitControls();
 for(const id of ['contrast','saturation','ditherStrength','rangeFit','warmChroma','ark512ContinuousTone'])assert.equal(node(id).disabled,true,id+' imported read-only');
 const generation=context.ark512Generation;
 for(const id of ['contrast','saturation','ditherStrength','rangeFit','warmChroma'])event(id,'input');
 event('ark512ContinuousTone','change');
 assert.equal(context.ark512Imported,imported);assert.equal(context.ark512Result,result);assert.equal(context.ark512Generation,generation,'programmatic imported parameter events must preserve original bytes');
 console.log('PASS I006 controls visible, independent warm caches, option forwarding, actual contrast/saturation input, cancellation, imported readonly events');
})().catch(error=>{console.error(error);process.exitCode=1;});
