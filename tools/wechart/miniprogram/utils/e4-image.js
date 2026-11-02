// InkSight (MIT): backend/core/native/eink_dither.cpp, ported to incremental JS.
// Default matches four-color RGB Atkinson, without photo enhancement.
// Reference appearance is not a measured calibration of this panel.
var TARGET = [[0,0,0],[255,255,255],[232,176,0],[200,0,0]];
var CODES = [[0,0,0],[255,255,255],[255,255,0],[255,0,0]];
var KERNELS = {
 atkinson:[[1,0,1/8],[2,0,1/8],[-1,1,1/8],[0,1,1/8],[1,1,1/8],[0,2,1/8]],
 floydSerpentine:[[1,0,7/16],[-1,1,3/16],[0,1,5/16],[1,1,1/16]],
 jarvis:[[1,0,7/48],[2,0,5/48],[-2,1,3/48],[-1,1,5/48],[0,1,7/48],[1,1,5/48],[2,1,3/48],[-2,2,1/48],[-1,2,3/48],[0,2,5/48],[1,2,3/48],[2,2,1/48]],
 burkes:[[1,0,8/32],[2,0,4/32],[-2,1,2/32],[-1,1,4/32],[0,1,8/32],[1,1,4/32],[2,1,2/32]],
 sierra:[[1,0,.5],[-1,1,.25],[0,1,.25]],
 stucki:[[1,0,8/42],[2,0,4/42],[-2,1,2/42],[-1,1,4/42],[0,1,8/42],[1,1,4/42],[2,1,2/42],[-2,2,1/42],[-1,2,2/42],[0,2,4/42],[1,2,2/42],[2,2,1/42]]
};
function clamp(v){return Math.max(0,Math.min(255,v));}
function createProcessor(image,options){
 options=options||{};
 var w=image.width,h=image.height,data=image.data;
 if(!Number.isInteger(w)||!Number.isInteger(h)||w<1||h<1||data.length!==w*h*4)throw Error('像素尺寸错误');
 var contrast=options.contrast===undefined?1:options.contrast;
 var saturation=options.saturation===undefined?1:options.saturation;
 var preset=options.preset||'photo';
 var algorithm=options.algorithm||(preset==='text'?'none':'atkinson');
 if(!KERNELS[algorithm]&&algorithm!=='none'&&algorithm!=='bayer')throw Error('未知抖动算法');
 // All supported kernels reach at most two future rows. A three-row ring
 // preserves Float64 ordering while avoiding a full-frame allocation/prepare.
 var stride=w*3,buf=new Float64Array(stride*3),row=0;
 var ct=contrast*(preset==='portrait'?1.12:1),plain=ct===1&&saturation===1&&preset!=='vintage';
 function prepare(y){
  if(y>=h)return;
  var base=(y%3)*stride;
  for(var x=0;x<w;x++){
  var i=y*w+x,o=base+x*3;
  if(plain&&data[i*4+3]===255){buf[o]=data[i*4];buf[o+1]=data[i*4+1];buf[o+2]=data[i*4+2];continue;}
  var a=data[i*4+3]/255,r=data[i*4]*a+255*(1-a),g=data[i*4+1]*a+255*(1-a),b=data[i*4+2]*a+255*(1-a);
  var l=.2126*r+.7152*g+.0722*b;
  if(preset==='vintage'){r=g=b=l;}else if(saturation!==1){r=l+(r-l)*saturation;g=l+(g-l)*saturation;b=l+(b-l)*saturation;}
  buf[o]=clamp((r-128)*ct+128);buf[o+1]=clamp((g-128)*ct+128);buf[o+2]=clamp((b-128)*ct+128);
  }
 }
 prepare(0);prepare(1);prepare(2);
 var diffusionKernel=KERNELS[algorithm],rowOffsets=diffusionKernel?new Int32Array(diffusionKernel.length):null;
 function step(rows){
  var end=Math.min(h,row+(rows||12)),kernel=diffusionKernel,offsets=rowOffsets;
  for(;row<end;row++){
   var dir=algorithm==='floydSerpentine'&&row%2?-1:1;
   var currentBase=(row%3)*stride;
   if(kernel)for(var index=0;index<kernel.length;index++)offsets[index]=((row+kernel[index][1])%3)*stride;
   for(var col=0;col<w;col++){
   var x=dir>0?col:w-1-col,o=currentBase+x*3;
   if(algorithm==='bayer'){
    var rank=0;for(var bit=0;bit<3;bit++){var xb=(x>>bit)&1,yb=(row>>bit)&1;rank=rank*4+((xb^yb)*2+yb);}
    var shift=(rank/64-.5)*64;for(var c=0;c<3;c++)buf[o+c]=clamp(buf[o+c]+shift);
   }
   var best=0,r=buf[o],g=buf[o+1],b=buf[o+2],distance=r*r+g*g+b*b;
   var dr=r-255,dg=g-255,db=b-255,d=dr*dr+dg*dg+db*db;
   if(d<distance){distance=d;best=1;}
   if(preset!=='vintage'){
    dr=r-232;dg=g-176;d=dr*dr+dg*dg+b*b;
    if(d<distance){distance=d;best=2;}
    dr=r-200;d=dr*dr+g*g+b*b;
    if(d<distance){best=3;}
   }
   var pixel=(row*w+x)*4;for(c=0;c<3;c++)data[pixel+c]=CODES[best][c];data[pixel+3]=255;
   var er=r-TARGET[best][0],eg=g-TARGET[best][1],eb=b-TARGET[best][2];
   if(kernel)for(var t=0;t<kernel.length;t++){
    var xx=x+dir*kernel[t][0],yy=row+kernel[t][1];if(xx<0||xx>=w||yy>=h)continue;
    var next=offsets[t]+xx*3,weight=kernel[t][2];
    buf[next]=clamp(buf[next]+er*weight);
    buf[next+1]=clamp(buf[next+1]+eg*weight);
    buf[next+2]=clamp(buf[next+2]+eb*weight);
   }
   }
   prepare(row+3);
  }
  return row===h;
 }
 return {step:step,image:image,stats:function(){return {algorithm:algorithm,preset:preset,profile:'inksight-reference',calibrated:false,rows:row,errorBufferBytes:buf.byteLength};}};
}
module.exports={createProcessor:createProcessor,referencePalette:TARGET};
