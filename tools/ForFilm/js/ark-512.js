(function(root){
'use strict';
const Y=[.2126,.7152,.0722],clip=x=>Math.max(0,Math.min(1,x)),dot=c=>c[0]*Y[0]+c[1]*Y[1]+c[2]*Y[2];
const smooth=(a,b,x)=>{const t=clip((x-a)/(b-a));return t*t*(3-2*t);};
const linear=x=>x/255<=.04045?x/255/12.92:((x/255+.055)/1.055)**2.4;
const srgb=x=>Math.round(255*(clip(x)<=.0031308?12.92*clip(x):1.055*clip(x)**(1/2.4)-.055));
const light=y=>y>(6/29)**3?116*y**(1/3)-16:(29/3)**3*y;
const inverseLight=l=>{const f=(l+16)/116;return f>6/29?f*f*f:3*(6/29)**2*(f-4/29);};
// Exact nearest neighbor; equal distances always prefer the original palette index.
// The far branch includes equality so a lower-index tie cannot be pruned.
class ColorTree {
    constructor(colors) {
        this.colors = colors;
        const build = (ids, depth) => {
            if (!ids.length) return null;
            const axis = depth % 3;
            ids.sort((a, b) => colors[a][axis] - colors[b][axis]);
            const mid = Math.floor(ids.length / 2);
            return {index: ids[mid], axis,
                left: build(ids.slice(0, mid), depth + 1),
                right: build(ids.slice(mid + 1), depth + 1)};
        };
        this.root = build(colors.map((_, i) => i), 0);
    }
    nearest(value) {
        let best = 0, distance = Infinity;
        const visit = node => {
            if (!node) return;
            const color = this.colors[node.index];
            const dr = value[0] - color[0], dg = value[1] - color[1], db = value[2] - color[2];
            const d = dr * dr + dg * dg + db * db;
            if (d < distance || (d === distance && node.index < best)) {
                distance = d; best = node.index;
            }
            const delta = value[node.axis] - color[node.axis];
            visit(delta < 0 ? node.left : node.right);
            if (delta * delta <= distance) visit(delta < 0 ? node.right : node.left);
        };
        visit(this.root);
        return best;
    }
}
// Input and output are logical portrait pixels; serpentine order is part of I006.
async function render(image,options){
options=options||{};const p=root.Ark512Profile,w=image.width,h=image.height,n=w*h;
if(!p)throw new Error('I006 配色资料未加载');
const warmStrength=options.warmChroma === undefined ? .25 : options.warmChroma;
const rangeFit=options.rangeFit===undefined?0:options.rangeFit;
const ditherStrength=options.ditherStrength===undefined?1:options.ditherStrength;
if(![warmStrength,rangeFit,ditherStrength].every(Number.isFinite)||warmStrength<0||warmStrength>1||rangeFit<0||rangeFit>1||ditherStrength<0||ditherStrength>5)throw new Error('无效 I006 调整参数');
const blackL=light(p.floor);
const recipeTree = new ColorTree(p.recipeLinear), outputTree = new ColorTree(p.outputLinear);
const check=()=>{if(options.cancelled&&options.cancelled())throw new Error('预览已取消');};
const pause=async()=>{check();await new Promise(r=>setTimeout(r,0));check();};
const commonY=new Float64Array(n),mask=new Uint8Array(n),work=new Float64Array(n*3),codes=new Uint16Array(n);
for(let y=0;y<h;y++){
 for(let x=0;x<w;x++){
 const i=y*w+x,a=i*4;let c=[0,1,2].map(k=>p.floor+(1-p.floor)*linear(image.data[a+k]));
 if(rangeFit>0){
 const rgb=[0,1,2].map(k=>linear(image.data[a+k])),sourceY=dot(rgb),mappedY=inverseLight(blackL+(100-blackL)*light(sourceY)/100),chroma=rgb.map(v=>(v-sourceY)*(1-p.floor));let k=1;
 for(const v of chroma)if(Math.abs(v)>1e-15)k=Math.min(k,(v>=0?1-mappedY:mappedY)/Math.abs(v));
 c=c.map((v,j)=>v*(1-rangeFit)+(mappedY+chroma[j]*k)*rangeFit);
 }
 const sy=dot(c),spread=Math.max(...c)-Math.min(...c),warm=Math.min(c[0]-c[2],(c[0]+c[1])/2-c[2]);
 const gate=smooth(.015,.10,warm)*smooth(.02,.12,spread)*smooth(.10,.45,sy);
 if(gate>0&&warmStrength>0)c=c.map(v=>sy+(v-sy)*(1-warmStrength*gate));
 const cy=dot(c),sp=Math.max(...c)-Math.min(...c),wa=Math.min(c[0]-c[2],(c[0]+c[1])/2-c[2]);
 mask[i]=cy>=.035&&cy<=.14&&wa>=.015&&sp>=.02&&sp<=.12&&c[0]>=c[1]&&c[1]>=c[2]&&c[0]-c[1]<=.4*cy?1:0;
 const dir=c.map((v,k)=>v-p.center[k]);let scale=1;
 for(const plane of p.hullPlanes){const denominator=plane[0]*dir[0]+plane[1]*dir[1]+plane[2]*dir[2];if(denominator>0)scale=Math.min(scale,-(plane[0]*p.center[0]+plane[1]*p.center[1]+plane[2]*p.center[2]+plane[3])/denominator);}
 if(scale<1)scale*=1-1e-10;
 const common=dir.map((v,k)=>p.center[k]+v*scale);commonY[i]=dot(common);work.set(options.continuousTone === true ? common : p.recipeLinear[recipeTree.nearest(common)],i*3);
 }
 if(y%4===3)await pause();
}
for(let y=0;y<h;y++){
 const d=y%2===0?1:-1;
 for(let x=d===1?0:w-1;x>=0&&x<w;x+=d){
 const i=y*w+x,c=[clip(work[i*3]),clip(work[i*3+1]),clip(work[i*3+2])],j=outputTree.nearest(c);// Duplicate camera-model coordinates retain the archived physical tuple representative.
 // Error diffusion still uses the exact originally selected color below.
 const code=p.outputCodes[j];codes[i]=p.codeAliases?.[code] ?? code;
 for(const [dx,dy,weight] of [[d,0,7],[-d,1,3],[0,1,5],[d,1,1]]){const xx=x+dx,yy=y+dy;if(xx>=0&&xx<w&&yy<h)for(let k=0;k<3;k++)work[(yy*w+xx)*3+k]+=Math.max(-1,Math.min(1,c[k]-p.outputLinear[j][k]))*(weight/16)*ditherStrength;}
 }
 if(y%4===3)await pause();
}
const shadow=p.shadowCodes,ys=shadow.map(code=>dot(p.outputLinear[p.outputCodes.indexOf(code)]));
for(let y=0;y<h;y++){
 const d=y%2===0?1:-1;
 for(let x=d===1?0:w-1;x>=0&&x<w;x+=d){
 const i=y*w+x;if(!mask[i])continue;const value=clip(commonY[i]);let j=0;
 for(let k=1;k<ys.length;k++)if(Math.abs(value-ys[k])<Math.abs(value-ys[j]))j=k;
 codes[i]=shadow[j];const error=Math.max(-1,Math.min(1,value-ys[j]));
 for(const [dx,dy,weight] of [[d,0,7],[-d,1,3],[0,1,5],[d,1,1]]){const xx=x+dx,yy=y+dy;if(xx>=0&&xx<w&&yy<h&&mask[yy*w+xx])commonY[yy*w+xx]+=error*(weight/16)*ditherStrength;}
 }
 if(y%8===7)await pause();
}
check();return {codes,width:w,height:h,rgba:preview(codes,w,h)};
}
function preview(codes,w,h){const p=root.Ark512Profile,table=new Map(p.outputCodes.map((code,i)=>[code,p.outputLinear[i].map(srgb)])),rgba=new Uint8ClampedArray(w*h*4);for(let i=0;i<codes.length;i++){const c=table.get(codes[i]);if(!c)throw new Error('实验文件含未标定的 ABC9 码');rgba.set([...c,255],i*4);}return rgba;}
function isExperimental(bytes){return !!bytes&&bytes.length>=32&&bytes[9]===0x7f;}
function parse(bytes){
if(!isExperimental(bytes)||bytes[12]!==65||bytes[13]!==66||bytes[14]!==67||bytes[15]!==57)throw new Error('需要 ABC9 实验 film');
const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),w=v.getUint16(4,true),h=v.getUint16(6,true);
if(w!==720||h!==480||v.getUint16(10,true)!==1||bytes.length!==691232||v.getUint32(0,true)!==691200)throw new Error('ABC9 仅支持 720×480 单帧 16bitLE（691232 字节）');
const codes=new Uint16Array(w*h);for(let i=0;i<codes.length;i++){codes[i]=v.getUint16(32+i*2,true);if(codes[i]>511)throw new Error('ABC9 码必须在 0..511');}return {codes,width:w,height:h};
}
function pack(codes,w,h){if(w!==720||h!==480||codes.length!==w*h)throw new Error('I006 实验 film 固定 720×480 单帧');const bytes=new Uint8Array(32+w*h*2),v=new DataView(bytes.buffer);v.setUint32(0,w*h*2,true);v.setUint16(4,w,true);v.setUint16(6,h,true);bytes[9]=0x7f;v.setUint16(10,1,true);bytes.set([65,66,67,57],12);for(let i=0;i<codes.length;i++){if(codes[i]>511)throw new Error('ABC9 码必须在 0..511');v.setUint16(32+i*2,codes[i],true);}return bytes;}
let abc9Preview=false;
root.Ark512={render,preview,parse,pack,isExperimental,resetCapabilities(){abc9Preview=false;},setCapabilities(payload){abc9Preview=payload.length===5&&payload[0]===1&&!!(payload[4]&0x20);},supported(){return abc9Preview;},assertUpload(bytes){if(isExperimental(bytes)){parse(bytes);if(!abc9Preview)throw new Error('当前设备固件尚未确认支持 512 色预览文件，请更新配套固件后发送。图片仍可在本地预览和导出。');}}};
})(window);
