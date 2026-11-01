// Regenerate parity fixtures using the unmodified web converters in a Node VM.
const fs=require('fs'),vm=require('vm');
class ImageData {constructor(a,b,c){if(typeof a==='number'){this.width=a;this.height=b;this.data=new Uint8ClampedArray(a*b*4);}else{this.data=a;this.width=b;this.height=c;}}}
const context={window:{},ImageData,document:{addEventListener(){},getElementById(){return null;}}};vm.createContext(context);
for(const p of ['convert.js','sz_enhanced.js'])vm.runInContext(fs.readFileSync('tools/ForFilm/js/'+p,'utf8'),context);
const cases=[];const input=[[20,60,180],[40,170,220],[250,240,15],[95,85,80],[120,190,25],[180,40,35],[230,230,230],[70,70,70],[15,120,85],[150,30,190],[210,160,50],[25,25,25]].flatMap(c=>[...c,255]);
function run(name,width,height,source,algorithm,dither=true,strength=1.3){const im=new ImageData(new Uint8ClampedArray(source),width,height);const profile=context.CF_PROFILES[algorithm];const out=profile?context.cfQuantize(im,strength,dither,profile):algorithm==='szEnhanced'?context.window.szEnhancedDither(im):algorithm==='adaptive'?context.adaptiveDither(im):context.atkinsonSzCalibQuantize(im);const codes=[];for(let i=0;i<out.data.length;i+=4){if(profile)codes.push(context.cfClosestIndex(out.data[i],out.data[i+1],out.data[i+2],profile));else {const r=out.data[i],g=out.data[i+1],b=out.data[i+2];codes.push(r===0&&g===0&&b===0?0:r===255&&g===255&&b===255?1:r===255&&g===255?2:r===255?3:b===255?4:5);}}cases.push({name,width,height,source,algorithm,dither,strength,codes});}
for(const a of ['colorFast55','colorQual'])for(const d of [false,true])run(a+':'+d,4,3,input,a,d);
run('calibrated',4,3,input,'atkinsonSzCalib');run('sz-small',4,3,input,'szEnhanced');
for(const kind of ['gradient','structure']){const w=37,h=35,source=[];for(let y=0;y<h;y++)for(let x=0;x<w;x++){if(kind==='gradient')source.push(Math.round(x*255/(w-1)),Math.round(y*255/(h-1)),(x*13+y*7)%256,255);else {const dark=x<6||x>w-7;const text=(y%7===0&&x%3===0);const bright=x>10&&x<26&&y>8&&y<27;source.push(dark?(text?210:5):bright?245:70,dark?(text?160:6):bright?238:80,dark?7:bright?220:160,255);}}run('sz-'+kind,w,h,source,'szEnhanced');}
// Identity-sized sample isolates adaptive search from browser resampling details.
context.downsampleImageData=(im,w,h)=>{if(im.width!==w||im.height!==h)throw Error('identity sample required');return new ImageData(new Uint8ClampedArray(im.data),w,h);};
const adaptiveSource=[];for(let y=0;y<30;y++)for(let x=0;x<30;x++)adaptiveSource.push((x*17+y*13)%256,(x*7+y*19)%256,(x*23+y*3)%256,255);
run('adaptive-identity',30,30,adaptiveSource,'adaptive');
fs.mkdirSync('app/flutter_app/test/fixtures',{recursive:true});fs.writeFileSync('app/flutter_app/test/fixtures/forfilm_remaining.json',JSON.stringify(cases));
