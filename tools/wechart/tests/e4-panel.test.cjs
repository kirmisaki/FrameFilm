// Run: node tools/wechart/tests/e4-panel.test.cjs (requires @napi-rs/canvas).
const assert = require('node:assert/strict');
const {createCanvas} = require('@napi-rs/canvas');
global.wx = {createOffscreenCanvas: ({width,height})=>createCanvas(width,height),getStorageSync:()=>''};
const film=require('../miniprogram/utils/film-utils');
const e6=require('../miniprogram/utils/e6pro');
const config=require('../miniprogram/utils/dither-config');
const palette=[[0,0,0],[255,255,255],[255,255,0],[255,0,0]];
function image(w=32,h=48) {
 const data=new Uint8ClampedArray(w*h*4);
 for(let i=0;i<data.length;i+=4) data.set([(i*19)%256,(i*29)%256,(i*37)%256,255],i);
 return {width:w,height:h,data};
}
function checkFour(img) {
 for(let i=0;i<img.data.length;i+=4) {
  if(img.data[i+3]===0) continue;
  assert.ok(palette.some(c=>c.every((v,j)=>v===img.data[i+j])),`nonphysical RGB at ${i}`);
 }
}
film.setDeviceType('FRAMEFILMC3');
assert.ok(film.applyScreenParams(0x08,768,552));
assert.equal(film.getDeviceType(),'FRAMEFILMC3');
assert.equal(film.getCanvasWidth(),552);assert.equal(film.getCanvasHeight(),768);
assert.equal(film.generateFilmHeader()[8],4);
assert.equal(film.getFilmFileTotalSize(),212000);
for(const type of ['e4Photo','e4Portrait','e4Vintage','e4Text','adaptive','floydSteinberg','atkinson','stucki','jarvis','gammaFloydSteinberg','bayer','atkinsonEnhanced','atkinsonSzCalib','szEnhanced']) {
 const img=image();film.applyDitherByType(img,type,1);checkFour(img);
}
for(const mode of ['solid','text','gradient','image']) checkFour(e6.convertZoneImage(image(),mode));
checkFour(e6.convert(image()));
const transparent=image();transparent.data[3]=0;
assert.equal(e6.convertZoneImage(transparent,'text').data[3],0);
assert.equal(config.getCreateDitherType(),'e4Photo');
assert.ok(!config.ditherOptionsFor().types.includes('szEnhanced'));
const canvas=createCanvas(552,768),ctx=canvas.getContext('2d');
palette.forEach((c,i)=>{ctx.fillStyle=`rgb(${c})`;ctx.fillRect((i%2)*276,Math.floor(i/2)*384,276,384);});
const file=e6.canvasToFilmData(canvas);
assert.equal(file.length,212000);assert.equal(file[8],4);
for(const b of file.subarray(32)) {assert.ok((b>>4)<4);assert.ok((b&15)<4);}
// Clockwise display coordinates -> portrait canvas, inverse of -90deg packing.
const decoded=film.decodeFilmFile(file);
assert.ok(decoded);checkFour(decoded);
const packed=film.decodeProcessedData(file.subarray(32),768,552);
function pixel(img,x,y) {return Array.from(img.data.slice((y*img.width+x)*4,(y*img.width+x)*4+3));}
assert.deepEqual(pixel(packed,0,0),palette[1]);
assert.deepEqual(pixel(packed,767,0),palette[3]);
assert.deepEqual(pixel(packed,0,551),palette[0]);
assert.deepEqual(pixel(packed,767,551),palette[2]);
// Same panel on a Pro must stay a Pro; switching back to six colours resets state.
film.setDeviceType('FRAMEFILMPRO');film.applyScreenParams(0x08,768,552);
assert.ok(film.is4ColorMode());
for(const [id,w,h] of [[1,792,528],[2,720,480],[3,600,400],[5,1200,1600],[6,760,568],[7,768,552]]) {
 assert.ok(film.applyScreenParams(id,w,h));assert.equal(film.generateFilmHeader()[8],6);
 assert.equal(film.is4ColorMode(),false);
 assert.equal(film.findClosestColor(0,0,255).code,4);
}
console.log('PASS E4 panel, 14 algorithm paths, templates, transparency, file bytes, four corners and six-colour reconnection');
// Web uses the same board identity; a screen response must not rename C3 Pro.
const vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
const web=vm.createContext({console,document:{querySelectorAll:()=>[],getElementById:()=>null}});
vm.runInContext(fs.readFileSync(path.resolve(__dirname,'../../ForFilm/js/utils.js'),'utf8'),web);
web.setDeviceType('FRAMEFILMC3');web.applyScreenParams(0x08,768,552);
assert.equal(web.getDeviceConfig().displayName,'FrameFilm C3');assert.equal(web.is4ColorMode(),true);
web.setDeviceType('FRAMEFILMPRO');web.applyScreenParams(0x01,792,528);
assert.equal(web.getDeviceConfig().displayName,'FrameFilm Pro');assert.equal(web.is4ColorMode(),false);
console.log('PASS Web C3 identity and Pro reconnection');
