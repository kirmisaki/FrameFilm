// Compare the changed palette owner against the unmodified upstream commit.
const assert=require('node:assert/strict'),path=require('node:path'),cp=require('node:child_process'),Module=require('node:module');
const filename=path.resolve(__dirname,'../miniprogram/utils/film-utils.js');
const old=new Module(filename,module);old.filename=filename;old.paths=Module._nodeModulePaths(path.dirname(filename));
old._compile(cp.execFileSync('git',['show','a4209adc6bf53153208e2a05cb9b450b8e881abb:tools/wechart/miniprogram/utils/film-utils.js'],{encoding:'utf8'}),filename);
const baseline=old.exports,current=require(filename);
function image(){const data=new Uint8ClampedArray(24*32*4);for(let i=0;i<data.length;i++)data[i]=(i*29+17)%256;return {width:24,height:32,data};}
for(const [id,w,h] of [[1,792,528],[2,720,480],[3,600,400],[5,1200,1600],[6,760,568],[7,768,552]]) {
 baseline.applyScreenParams(id,w,h);current.applyScreenParams(id,w,h);
 assert.deepEqual(current.generateFilmHeader(),baseline.generateFilmHeader());
 for(const algorithm of ['floydSteinberg','atkinson','stucki','jarvis']) {
  const a=image(),b=image();baseline.applyDitherByType(a,algorithm,1);current.applyDitherByType(b,algorithm,1);
  assert.deepEqual(a,b);
 }
 assert.deepEqual(current.processImageData(image()),baseline.processImageData(image()));
}
console.log('PASS six-colour byte-for-byte regression vs upstream: 6 panels x 4 algorithms, headers and packed data');
