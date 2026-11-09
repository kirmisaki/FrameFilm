const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { File } = require('node:buffer');

function runtime(detectedSize) {
    const nodes = new Map();
    const node = () => ({ style: {}, value: '', checked: false, options: [],
        appendChild(item) { this.options.push(item); }, removeAttribute() {} });
    const document = { getElementById(id) {
        if (!nodes.has(id)) nodes.set(id, node());
        return nodes.get(id);
    }, createElement: node };
    document.getElementById('fw-baud').value = '460800';
    const writes = [], messages = [], prompts = [];
    let ports = 0, disconnected = false;
    const context = vm.createContext({
        document, crypto: crypto.webcrypto, Uint8Array, DataView, TextDecoder, TextEncoder, File,
        setTimeout: callback => { callback(); return 0; },
        navigator: { serial: { requestPort: async () => { ports++; return {}; } } },
        confirm: message => { prompts.push(message); return true; },
        showMessage: (message, kind) => messages.push({ message, kind }),
        ESPTOOL: {
            Transport: class {
                async setDTR() {} async setRTS() {}
                async disconnect() { disconnected = true; }
            },
            ESPLoader: class {
                async main() { return 'ESP32-S3 (QFN56) (revision v0.2)'; }
                async detectFlashSize() { return detectedSize; }
                async writeFlash(options) {
                    for (const { data } of options.fileArray) {
                        assert.equal(options.calculateMD5Hash(data),
                            crypto.createHash('md5').update(data).digest('hex'));
                    }
                    writes.push(options);
                }
            }
        }
    });
    context.window = context;
    for (const name of ['ark-firmware.js', 'classic-firmware.js', 'serial-flash.js']) {
        vm.runInContext(fs.readFileSync(path.join(__dirname, '../js', name), 'utf8'), context);
    }
    return { context, document, writes, messages, prompts,
        get ports() { return ports; }, get disconnected() { return disconnected; } };
}

// Small valid app with the same ESP image / FFARKHW1 headers as the real builds.
function mergedImage(hardware) {
    const app = new Uint8Array(368), view = new DataView(app.buffer);
    app[0] = 0xe9; app[1] = 1;
    view.setUint16(12, 9, true);
    view.setUint32(28, 320, true);
    view.setUint32(32, 0xabcd5432, true);
    const text = (s, offset) => app.set(new TextEncoder().encode(s), offset);
    text('3.2.5', 48); text('frame_film_ark', 80);
    app.set(new Uint8Array(32).fill(17), 176);
    text('FFARKHW1', 288); app[296] = 1; app[297] = 64;
    view.setUint16(298, 9, true);
    view.setUint32(300, hardware.flashBytes, true);
    view.setUint32(304, hardware.psramBytes, true);
    app[308] = hardware.psramBus === 'octal' ? 2 : 1;
    text(hardware.targetId, 312);
    app[367] = app.subarray(32, 352).reduce((sum, byte) => sum ^ byte, 0xef);
    const bytes = new Uint8Array(0x30000 + app.length);
    bytes[0] = 0xe9; bytes[12] = 9;
    bytes[0x8000] = 0xaa; bytes[0x8001] = 0x50;
    bytes.set(app, 0x30000);
    return bytes;
}

async function stage(run, variant) {
    const fw = run.context.ArkFirmware;
    const hardware = fw.target('ark-esp32s3-' + variant);
    const bytes = mergedImage(hardware);
    const item = fw.selectFlash({ tag_name: 'ark-test', assets: [{ id: 1,
        name: hardware.flashName, size: bytes.length,
        digest: 'sha256:' + crypto.createHash('sha256').update(bytes).digest('hex')
    }] }, hardware);
    await run.context.fwStageOnline(bytes, item);
    return { hardware, item, bytes };
}

for (const [variant, flash] of [['n4r2', '4MB'], ['n16r8', '16MB']]) {
    test('Ark ' + variant + ' online image retains hardware through serial flashing', async () => {
        const run = runtime(flash);
        const { hardware, item, bytes } = await stage(run, variant);
        assert.equal(item.family, 'ark');
        for (const key of ['targetId', 'label', 'flashBytes', 'psramBytes', 'psramBus']) {
            assert.equal(item[key], hardware[key]);
        }
        await run.context.startSerialFlash();
        assert.equal(run.writes.length, 1);
        assert.equal(run.writes[0].fileArray[0].address, 0);
        assert.deepEqual(run.writes[0].fileArray[0].data, bytes);
        assert.equal(run.writes[0].eraseAll, false);
        assert.equal(run.disconnected, true);
        assert.match(run.prompts[0], new RegExp(hardware.label));
        assert.equal(run.messages.at(-1).kind, 'success');
        assert.doesNotMatch(run.document.getElementById('fw-log').textContent, /NaN|undefined/);
    });
}

test('Ark N16R8 image is refused on the detected 4MB device before write', async () => {
    const run = runtime('4MB');
    await stage(run, 'n16r8');
    await run.context.startSerialFlash();
    assert.equal(run.writes.length, 0);
    assert.equal(run.disconnected, true);
    assert.match(run.messages.at(-1).message, /4MB.*Ark N16R8.*16MB/);
    assert.doesNotMatch(run.messages.at(-1).message, /NaN|undefined/);
});

test('incomplete online hardware reports a configuration error without opening serial', async () => {
    const run = runtime('4MB');
    const { item, bytes } = await stage(run, 'n4r2');
    delete item.flashBytes;
    await run.context.fwStageOnline(bytes, item);
    await run.context.startSerialFlash();
    assert.equal(run.ports, 0);
    assert.equal(run.writes.length, 0);
    assert.match(run.messages.at(-1).message, /缺少硬件规格/);
});
