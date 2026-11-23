const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { File } = require('node:buffer');

function runtime(detectedSize, options = {}) {
    const nodes = new Map();
    const node = () => ({ style: {}, value: '', checked: false, options: [],
        appendChild(item) { this.options.push(item); }, removeAttribute() {} });
    const document = { getElementById(id) {
        if (!nodes.has(id)) nodes.set(id, node());
        return nodes.get(id);
    }, createElement: node };
    document.getElementById('fw-baud').value = String(options.rate ?? 460800);
    const writes = [], messages = [], prompts = [], events = [];
    let ports = 0, disconnected = false, sessions = 0;
    const port = {
        connected: true, readable: null, writable: null,
        getInfo() { return {}; },
        async open({ baudRate }) {
            events.push(['open', baudRate]);
            if (this.readable) throw new DOMException('The port is already open.', 'InvalidStateError');
            options.open?.(baudRate, this);
            this.rate = baudRate;
            this.readable = new ReadableStream();
            this.writable = new WritableStream();
        },
        async close() {
            events.push(['close']);
            if (!this.readable) throw new DOMException('The port is already closed.', 'InvalidStateError');
            assert.equal(this.readable.locked, false);
            this.readable = this.writable = null;
            disconnected = true;
        },
        async setSignals() { options.reset?.(); }
    };
    const context = vm.createContext({
        document, crypto: crypto.webcrypto, Uint8Array, DataView, TextDecoder, TextEncoder, File,
        DOMException, console: { log() {} },
        setTimeout: callback => { callback(); return 0; },
        navigator: { serial: {
            requestPort: async () => { ports++; return port; },
            getPorts: async () => port.connected && !options.portMissing ? [port] : []
        } },
        confirm: message => { prompts.push(message); return true; },
        showMessage: (message, kind) => messages.push({ message, kind })
    });
    context.window = context;
    const load = name => vm.runInContext(fs.readFileSync(path.join(__dirname, '../js', name), 'utf8'), context);
    load('esptool-js.js');
    const { ESPLoader, ESPError } = context.ESPTOOL;
    context.ESPTOOL.ESPLoader = class extends ESPLoader {
        async detectChip() {
            sessions++;
            assert.equal(this.baudrate, 115200);
            await this.transport.connect(this.romBaudrate);
            this.transport.readLoop();
            options.sync?.(sessions);
            await this.identifyChip();
            this.chip = { CHIP_NAME: 'ESP32-S3' };
        }
        async main() {
            await this.detectChip();
            await this.runStub();
            await this.readFlashId();
            return 'ESP32-S3 (QFN56) (revision v0.2)';
        }
        async identifyChip() { options.identify?.(); }
        async runStub() { options.stub?.(); this.IS_STUB = true; }
        async readFlashId() { return options.flashId?.(sessions) ?? 0x1640ef; }
        async command(op, args) {
            assert.equal(op, this.ESP_CHANGE_BAUDRATE);
            const view = new DataView(args.buffer);
            events.push(['change', view.getUint32(0, true), view.getUint32(4, true)]);
            if (options.changeFailure) throw new ESPError('No serial data received.');
            return [0, new Uint8Array(options.changeRejected ? [1, 5] : [0, 0])];
        }
        async readReg(addr, timeout) {
            events.push(['probe', port.rate]);
            assert.equal(addr, this.CHIP_DETECT_MAGIC_REG_ADDR);
            assert.equal(timeout, 500);
            if (options.probeFailure) throw new ESPError('No serial data received.');
        }
        async detectFlashSize() { events.push(['size', port.rate]); return detectedSize; }
        async writeFlash(flashOptions) {
            writes.push(flashOptions);
            events.push(['write', port.rate]);
            if (flashOptions.eraseAll) await this.eraseFlash();
            for (const [index, { data }] of flashOptions.fileArray.entries()) {
                await this.flashDeflBegin();
                flashOptions.reportProgress(index, 0, data.length);
                if (options.writeFailure) throw new ESPError('No serial data received.');
                assert.equal(flashOptions.calculateMD5Hash(data),
                    crypto.createHash('md5').update(data).digest('hex'));
                await this.flashMd5sum();
            }
        }
        async eraseFlash() { if (options.eraseFailure) throw new ESPError('No serial data received.'); }
        async flashDeflBegin() {}
        async flashMd5sum() { if (options.verifyFailure) throw new ESPError('MD5 mismatch'); }
    };
    for (const name of ['ark-firmware.js', 'classic-firmware.js', 'serial-flash.js']) load(name);
    return { context, document, writes, messages, prompts, events,
        get log() { return document.getElementById('fw-log').textContent; },
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

const networkError = () => new DOMException('Failed to open serial port.', 'NetworkError');
const ratesOpened = run => run.events.filter(([kind]) => kind === 'open').map(([, rate]) => rate);

async function flashWith(options) {
    const run = runtime('4MB', { rate: 921600, ...options });
    await stage(run, 'n4r2');
    await run.context.startSerialFlash();
    return run;
}

test('detection ignores the burn rate and reports reset failure independently', async () => {
    for (const resetFails of [false, true]) {
        const run = runtime('4MB', { rate: 921600,
            stub() { assert.fail('detection must not start a stub'); },
            flashId() { assert.fail('detection does not need Flash ID'); },
            reset() { if (resetFails) throw networkError(); }
        });
        await run.context.detectSerialDevice();
        assert.deepEqual(run.events, [['open', 115200], ['close']]);
        assert.equal(run.writes.length, 0);
        assert.equal(run.messages.at(-1).kind, resetFails ? 'warning' : 'success');
        assert.match(run.messages.at(-1).message, /串口正常（115200）：ESP32-S3/);
        if (resetFails) assert.match(run.messages.at(-1).message, /检测后复位失败/);
    }
});

test('high-rate burn opens at 115200, switches and proves the new rate before writing', async () => {
    const run = await flashWith();
    assert.deepEqual(run.events, [
        ['open', 115200], ['change', 921600, 115200], ['close'], ['open', 921600],
        ['probe', 921600], ['size', 921600], ['write', 921600], ['close']
    ]);
    assert.match(run.log, /当前速率 921600/);
    assert.equal(run.messages.at(-1).kind, 'success');
});

test('failed high-speed reopen falls back without trying to close a closed port', async () => {
    const run = await flashWith({ open(rate) { if (rate === 921600) throw networkError(); } });
    assert.deepEqual(run.events, [
        ['open', 115200], ['change', 921600, 115200], ['close'], ['open', 921600],
        ['open', 115200], ['size', 115200], ['write', 115200], ['close']
    ]);
    assert.equal(run.messages.at(-1).kind, 'success');
    assert.match(run.log, /NetworkError: Failed to open serial port.*重新握手一次/);
    assert.match(run.log, /当前速率 115200/);
});

test('switch-response or probe timeouts allow only one low-rate attempt', async () => {
    for (const options of [{ changeFailure: true }, { probeFailure: true }]) {
        const run = await flashWith(options);
        assert.deepEqual(ratesOpened(run), options.probeFailure ? [115200, 921600, 115200] : [115200, 115200]);
        assert.equal(run.events.filter(([event]) => event === 'probe').length, options.probeFailure ? 2 : 0);
        assert.equal(run.writes.length, 1);
        assert.match(run.log, /当前速率 115200/);
    }
    const run = await flashWith({ probeFailure: true, sync(session) {
        if (session === 2) throw new Error('No SYNC after fallback');
    } });
    assert.deepEqual(ratesOpened(run), [115200, 921600, 115200]);
    assert.equal(run.writes.length, 0);
    assert.match(run.messages.at(-1).message, /ROM SYNC.*No SYNC after fallback/);
});

test('fallback must still match the original chip and Flash ID', async () => {
    const run = await flashWith({ probeFailure: true, flashId: session => 0x1640ef + session });
    assert.deepEqual(ratesOpened(run), [115200, 921600, 115200]);
    assert.equal(run.writes.length, 0);
    assert.match(run.messages.at(-1).message, /Flash ID 与初次结果不一致/);
});

test('initial open, ROM sync, and stub failures do not trigger a baud fallback', async () => {
    for (const [options, expected] of [
        [{ open() { throw networkError(); } }, /打开串口.*NetworkError/],
        [{ sync() { throw new Error('no reply'); } }, /ROM SYNC.*no reply/],
        [{ stub() { throw new Error('no OHAI'); } }, /stub.*no OHAI/]
    ]) {
        const run = await flashWith(options);
        assert.deepEqual(ratesOpened(run), [115200]);
        assert.equal(run.writes.length, 0);
        assert.match(run.messages.at(-1).message, expected);
    }
});

test('an explicit rejection of the baud command does not reopen the port', async () => {
    const run = await flashWith({ changeRejected: true });
    assert.deepEqual(ratesOpened(run), [115200]);
    assert.equal(run.writes.length, 0);
    assert.match(run.messages.at(-1).message, /发送切速命令.*status 1,5/);
});

test('missing port or a permission failure during reopen is not downgraded', async () => {
    for (const [options, name] of [
        [{ portMissing: true, open(rate) { if (rate === 921600) throw networkError(); } }, 'NetworkError'],
        [{ open(rate) { if (rate === 921600) throw new DOMException('Denied', 'NotAllowedError'); } }, 'NotAllowedError']
    ]) {
        const run = await flashWith(options);
        assert.deepEqual(ratesOpened(run), [115200, 921600]);
        assert.equal(run.writes.length, 0);
        assert.match(run.messages.at(-1).message, new RegExp('切换主机串口速率.*' + name));
    }
});

test('no automatic retries after erase, write, verification, or post-write reset errors', async () => {
    for (const [options, message, kind] of [
        [{ eraseFailure: true }, /整片擦除 Flash.*未自动重试/, 'error'],
        [{ writeFailure: true }, /写入 Flash.*未自动重试/, 'error'],
        [{ verifyFailure: true }, /校验 Flash MD5.*未自动重试/, 'error'],
        [{ reset() { throw networkError(); } }, /已写入并校验，但复位失败/, 'warning']
    ]) {
        const run = runtime('4MB', options);
        await stage(run, 'n4r2');
        run.document.getElementById('fw-erase-all').checked = !!options.eraseFailure;
        await run.context.startSerialFlash();
        assert.deepEqual(ratesOpened(run), [115200, 460800]);
        assert.equal(run.writes.length, 1);
        assert.equal(run.messages.at(-1).kind, kind);
        assert.match(run.messages.at(-1).message, message);
    }
});
