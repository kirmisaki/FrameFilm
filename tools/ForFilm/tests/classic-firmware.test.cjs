const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '../js/classic-firmware.js'), 'utf8');
const arkSource = fs.readFileSync(path.join(__dirname, '../js/ark-firmware.js'), 'utf8');
const MIB = 1024 * 1024;
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const elfSha = crypto.createHash('sha256').update('elf').digest();

function load(code) {
    const window = {};
    const context = vm.createContext({
        window, crypto: crypto.webcrypto, Uint8Array, DataView, TextDecoder,
        Object, Number, Map, Array, Date, JSON, Error, Promise, RegExp, String, ArrayBuffer
    });
    vm.runInContext(code, context);
    return window;
}

function appImage(project = 'frame_film') {
    const value = new Uint8Array(512);
    value[0] = 0xe9;
    value[1] = 4;
    new DataView(value.buffer).setUint16(12, 9, true);
    new DataView(value.buffer).setUint32(32, 0xabcd5432, true);
    value.set(new TextEncoder().encode('1.0.0'), 48);
    value.set(new TextEncoder().encode(project), 80);
    value.set(elfSha, 176);
    return value;
}

function flashImage(app) {
    const value = new Uint8Array(0x30000 + app.length);
    value[0] = 0xe9;
    new DataView(value.buffer).setUint16(12, 9, true);
    value[0x8000] = 0xaa;
    value[0x8001] = 0x50;
    value.set(app, 0x30000);
    return value;
}

const app = appImage();
const flash = flashImage(app);
const screen = 'e6-3.68-792x528';
const prefix = 'frame_film_pro_n4r2_' + screen;

function entry() {
    return {
        variant: 'n4r2', flashBytes: 4 * MIB, psramBytes: 2 * MIB, psramBus: 'quad',
        screen, panelId: 1, width: 792, height: 528,
        ota: { asset: prefix + '-ota.bin', size: app.length, sha256: sha(app),
            elfSha256: elfSha.toString('hex'), version: '1.0.0' },
        flash: { asset: prefix + '-flash.bin', size: flash.length, sha256: sha(flash) }
    };
}

function release(extra = {}) {
    return {
        tag_name: 'frame-film-v2026.11.07.1', draft: false, prerelease: false,
        published_at: '2026-11-07T00:00:00Z', body: '',
        assets: [
            { id: 1, name: prefix + '-ota.bin', size: app.length, digest: 'sha256:' + sha(app) },
            { id: 2, name: prefix + '-flash.bin', size: flash.length, digest: 'sha256:' + sha(flash) }
        ],
        ...extra
    };
}

function manifest(item = entry()) {
    return { schemaVersion: 1, project: 'frame_film', model: 'pro', release: '2026.11.07.1',
        firmwares: [item] };
}

test('targets map the CI asset names and panel metadata', () => {
    const { ClassicFirmware: fw } = load(source);
    const target = fw.target('n4r2', screen);
    assert.equal(target.flashName, 'frame_film_pro_n4r2_e6-3.68-792x528-flash.bin');
    assert.equal(target.otaName, 'frame_film_pro_n4r2_e6-3.68-792x528-ota.bin');
    assert.equal(target.flashBytes, 4 * MIB);
    assert.equal(target.panelId, 1);
    assert.equal(fw.screenOf(8).screen, 'e4-3.98-768x552');
    assert.equal(fw.target('n4r2', 'missing'), null);
});

test('classic releases are filtered and Ark releases never win', () => {
    const { ClassicFirmware: fw } = load(source);
    const newest = fw.latestFormal([
        { ...release(), tag_name: 'ark-2026.12.01', published_at: '2026-12-01T00:00:00Z' },
        { ...release(), tag_name: 'frame-film-v2026.11.20.1', published_at: '2026-11-20T00:00:00Z' },
        { ...release(), tag_name: 'frame-film-v2026.11.25.1', prerelease: true, published_at: '2026-11-25T00:00:00Z' },
        release()
    ]);
    assert.equal(newest.tag_name, 'frame-film-v2026.11.20.1');
    assert.equal(fw.latestFormal([{ ...release(), tag_name: 'ark-2026.12.01', published_at: '2026-12-01T00:00:00Z' }]), null);
});

test('selectFlash validates the manifest, assets and requested hardware', () => {
    const { ClassicFirmware: fw } = load(source);
    const rel = release();
    const expected = fw.target('n4r2', screen);
    const item = fw.selectFlash(rel, manifest(), expected);
    assert.equal(item.family, 'classic');
    assert.equal(item.label, expected.label);
    assert.equal(item.elfSha256, entry().ota.elfSha256);
    assert.equal(item.otaSize, app.length);

    const other = fw.target('n16r8', screen);
    assert.throws(() => fw.selectFlash(rel, manifest(), other), /没有匹配/);

    const tampered = entry();
    tampered.flash.size += 1;
    assert.throws(() => fw.selectFlash(rel, manifest(tampered), expected), /不一致|无效/);

    const wrongVariant = entry();
    wrongVariant.flashBytes = 16 * MIB;
    assert.throws(() => fw.selectFlash(rel, manifest(wrongVariant), expected), /目标或指纹无效/);
});

test('verifyFlash checks merged layout, app header and build fingerprint', async () => {
    const { ClassicFirmware: fw } = load(source);
    const item = fw.selectFlash(release(), manifest(), fw.target('n4r2', screen));
    await fw.verifyFlash(flash, item);

    const wrongProject = flashImage(appImage('frame_film_ark'));
    const wrongProjectItem = { ...item, size: wrongProject.length, sha256: sha(wrongProject) };
    await assert.rejects(fw.verifyFlash(wrongProject, wrongProjectItem), /FrameFilm/);

    const wrongElf = entry();
    wrongElf.ota.elfSha256 = '0'.repeat(63) + '1';
    const wrongItem = { ...item, elfSha256: wrongElf.ota.elfSha256 };
    await assert.rejects(fw.verifyFlash(flash, wrongItem), /构建指纹/);

    await assert.rejects(fw.verifyFlash(flash.subarray(0, 0x8000), item), /大小|截断/);
});

test('Ark release selection ignores classic frame-film releases', () => {
    const { ArkFirmware: fw } = load(arkSource);
    const newest = fw.latestFormal([
        { ...release(), published_at: '2026-12-01T00:00:00Z' },
        {
            tag_name: 'ark-2026.11.20', draft: false, prerelease: false,
            published_at: '2026-11-20T00:00:00Z', assets: []
        }
    ]);
    assert.equal(newest.tag_name, 'ark-2026.11.20');
});
