const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const assert = require('node:assert/strict');
const context = vm.createContext({window: {}, Math, Uint8ClampedArray, Uint8Array, Float32Array, console});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../js/source-fit.js'), 'utf8'), context);
const fit = context.window.FrameFilmSourceFit;
const nominal = {yb: .04, yw: .64};
const measured = fit.measured([40,48,56], [200,208,216]);
for (const [name, yb] of [['native6',.08143727637491299],['fast',.1034168376244516],['legacyQual',.09957615092384647]]) {
    assert.equal(fit.profiles[name].yb,yb);
    assert.equal(fit.profiles[name].yw,1);
    assert.ok(fit.profiles[name].yb>0 && fit.profiles[name].yb<fit.profiles[name].yw);
}
function pixel(rgb, r, w, profile = nominal) {
    const p = {width: 1, height: 1, data: Uint8ClampedArray.from([...rgb, 73])};
    fit.apply(p, r, w, profile);
    assert.equal(p.data[3], 73);
    return [...p.data.slice(0, 3)];
}
for (const [rgb,r,w,expected] of [
    [[128,128,128],1,0,[156,156,156]], [[128,128,128],.5,.5,[143,143,143]],
    [[230,175,105],1,0,[243,193,136]], [[230,175,105],1,1,[202,202,202]],
    [[230,175,105],0,1,[185,185,185]], [[65,120,235],1,0,[114,149,249]],
    [[65,120,235],1,1,[114,149,249]]
]) assert.deepEqual(pixel(rgb,r,w),expected);
for (const [rgb,w,expected] of [
    [[0,0,0],0,[40,48,56]], [[255,255,255],0,[200,208,216]],
    [[128,128,128],0,[122,129,135]], [[230,175,105],0,[191,158,125]],
    [[230,175,105],1,[158,165,172]]
]) assert.deepEqual(pixel(rgb,1,w,measured),expected);
for (const name of ['native6','fast']) {
    assert.deepEqual(pixel([0,0,0],1,1,fit.profiles[name]),[0,0,0]);
    assert.deepEqual(pixel([255,255,255],1,1,fit.profiles[name]),[255,255,255]);
}
const original = Uint8ClampedArray.from({length: 4096}, (_, i) => (i * 73 + 17) % 256);
const off = {data: original.slice()};
fit.apply(off,0,0,nominal);
assert.deepEqual(off.data,original);
let last = -1;
for(let gray=0;gray<256;gray++) {
    const result=pixel([gray,gray,gray],1,1);
    assert.equal(result[0],result[1]);assert.equal(result[1],result[2]);
    assert.ok(result[0]>=last);last=result[0];
}
// Both shared modules must load in the avatar VM without a DOM.
vm.runInContext(fs.readFileSync(path.join(__dirname, '../js/convert.js'), 'utf8'), context);
vm.runInContext(fs.readFileSync(path.join(__dirname, '../../ark-pass-editor/avatar-editor.js'), 'utf8'), context);
const legacy=context.window.PassportAvatarEditor.validateEdit({source:'data:image/png;base64,AA==',options:{brightness:.2}});
assert.equal(legacy.options.brightness,.2);
assert.equal(legacy.options.rangeFit,0);assert.equal(legacy.options.warmChroma,0);
assert.throws(()=>context.window.PassportAvatarEditor.validateEdit({source:legacy.source,options:{rangeFit:2}}));
console.log('Source fit golden pixels, off bytes, alpha, monotonic gray and legacy avatar options passed');
