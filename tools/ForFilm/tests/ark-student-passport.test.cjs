// Targeted real-browser regression for the Ark student-pass template.
// Run with the tools static server at 127.0.0.1:8768, Playwright, and Edge available.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
async function mockBluetooth() {
    const mock = window.mockArk = { files: [], starts: 0, profileJson: null, currentFile: null, direct: 3, app: 5, colorSupported: true, fullscreenSupported: true, i006Supported: false };
    const encode = value => new TextEncoder().encode(value);
    const notify = (characteristic, channel, payload) => {
        const frame = new Uint8Array([0x55, channel, payload.length, ...payload, 0]);
        frame[frame.length - 1] = frame.slice(0, -1).reduce((sum, byte) => sum + byte, 0) & 255;
        characteristic.value = new DataView(frame.buffer);
        characteristic.dispatchEvent(new Event('characteristicvaluechanged'));
    };
    class Characteristic extends EventTarget {
        properties = { write: true };
        async startNotifications() { return this; }
        async writeValue(raw) {
            if (!peripheral.gatt.connected) throw new Error('Disconnected');
            const bytes = new Uint8Array(raw), channel = bytes[1], payload = bytes.slice(3, -1);
            let reply;
            if (channel === 0x42) reply = [2, 2, 208, 1, 224];
            else if (channel === 0x23) reply = [82];
            else if ([0x26, 0x28, 0x31, 0x3A].includes(channel)) reply = [0];
            else if (channel === 0x2A) reply = [0, 30];
            else if (channel === 0x54 || channel === 0x55) reply = [0, ...encode('FRAMEFILMARK-测试'), 0];
            else if (channel === 0x4B) mock.app = bytes[3];
            else if (channel === 0x4C) reply = [mock.app];
            else if (channel === 0x51) reply = [mock.direct, 100, 0, 0, 0, 1, 0, 0, 0, 1, 0];
            // General ABC9 playback (bit5) is distinct from I006 passport support (bit11).
            else if (channel === 0x59) reply = [1, 0, 0, (mock.fullscreenSupported ? 4 : 0) | (mock.i006Supported ? 8 : 0), 32 | (mock.colorSupported ? 1 : 0)];
            else if (channel === 0x03) { mock.starts++; mock.currentFile = { name: '', bytes: [] }; }
            else if (channel === 0x00 && mock.currentFile) mock.currentFile.name = new TextDecoder().decode(payload).replace(/\0$/, '');
            else if (channel === 0x02 && mock.currentFile) mock.currentFile.bytes.push(...payload);
            else if (channel === 0x04 && mock.currentFile) {
                const file = mock.currentFile;
                mock.files.push(file);
                if (file.name === 'app/pass/profile.json') mock.profileJson = new TextDecoder().decode(Uint8Array.from(file.bytes));
                mock.currentFile = null;
            } else if (channel === 0x53) {
                const offset = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(3);
                const data = encode(mock.profileJson || '{"version":99}');
                const chunk = data.slice(offset, offset + (payload[4] || 128));
                const header = new Uint8Array(9), view = new DataView(header.buffer);
                view.setUint32(1, offset); view.setUint32(5, data.length);
                reply = [...header, ...chunk];
            }
            if (reply) await new Promise(resolve => setTimeout(() => { notify(this, channel, reply); resolve(); }, 1));
        }
    }
    const characteristic = new Characteristic(), peripheral = new EventTarget();
    peripheral.name = 'FRAMEFILMARK-测试';
    peripheral.gatt = {
        connected: false,
        async connect() { this.connected = true; return this; },
        disconnect() { this.connected = false; peripheral.dispatchEvent(new Event('gattserverdisconnected')); },
        async getPrimaryService() { return { getCharacteristic: async () => characteristic }; }
    };
    Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
        requestDevice: async () => peripheral, getDevices: async () => [peripheral]
    } });
}

(async () => {
    const browser = await chromium.launch({ headless: true, channel: 'msedge' });
    try {
        const page = await browser.newPage({ viewport: { width: 1280, height: 960 }, reducedMotion: 'reduce' });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
        // A synthetic fixture keeps the regression independent of privately supplied PSD artwork.
        const fixture = extra => ({ contentType: 'image/svg+xml', body:
            '<svg xmlns="http://www.w3.org/2000/svg" width="720" height="480">' +
            '<rect width="720" height="480" fill="white"/><rect width="32" height="32" fill="#153c87"/>' +
            `<rect y="448" width="32" height="32" fill="#b44720"/><rect x="290" y="258" width="415" height="48"/>${extra}</svg>` });
        let deliverBase, deliverSniper;
        const allowBase = new Promise(resolve => { deliverBase = resolve; });
        const allowSniper = new Promise(resolve => { deliverSniper = resolve; });
        await page.route('**/student-palettes/fullscreen/amiya.png', async route => {
            await allowBase;
            await route.fulfill(fixture(''));
        });
        await page.route('**/student-palettes/fullscreen/sniper.png', async route => {
            await allowSniper;
            await route.fulfill(fixture('<rect x="688" y="448" width="32" height="32" fill="yellow"/>'));
        });
        await page.route('**/student-palettes/fullscreen/guard.png', route =>
            route.fulfill(fixture('<rect x="688" y="448" width="32" height="32"/>')));
        await page.route('**/student-palettes/fullscreen/medic.png', route => route.fulfill({ status: 404 }));
        await page.addInitScript(mockBluetooth);
        await page.goto('http://127.0.0.1:8768/ForFilm/', { waitUntil: 'networkidle' });
        await page.locator('[data-page="bluetooth-page"]').click();
        await page.locator('#scan-button').click();
        await page.waitForFunction(() => ArkDevice.connected());
        await page.locator('#ark-login[open] .ark-login-skip').click();
        await page.locator('[data-page="pass-page"]').click();

        const hostFrame = page.locator('#ark-pass-editor');
        const editor = page.frameLocator('#ark-pass-editor');
        await editor.locator('#template-select').selectOption('rhodes-academy-student');
        await editor.locator('#codename').fill('学生档案测试');
        await editor.locator('#student-number').fill('202411020229');
        await editor.locator('#race').fill('菲林');
        await editor.locator('#student-number-unset').uncheck();
        await editor.locator('#student-base-status').filter({ hasText: '正在加载' }).waitFor();
        for (const id of ['send', 'download-png', 'download-bin']) assert(await editor.locator(`#${id}`).isDisabled());
        assert(await editor.locator('#export-json').isEnabled());
        // Completing the image request after switching back must not paint over the other template.
        await editor.locator('#template-select').selectOption('operator');
        const baseResponse = page.waitForResponse('**/student-palettes/fullscreen/amiya.png');
        deliverBase();
        await baseResponse;
        assert.equal(await editor.locator('#preview').evaluate(canvas => canvas.width), 440);
        await editor.locator('#template-select').selectOption('rhodes-academy-student');
        const frame = await (await hostFrame.elementHandle()).contentFrame();
        await frame.waitForFunction(() => document.getElementById('student-base-status').textContent === '');
        assert.deepEqual(await editor.locator('#preview').evaluate(canvas => [canvas.width, canvas.height]), [720, 480]);
        const output = editor.locator('#student-output');
        assert.equal(await output.inputValue(), 'i006');
        await output.selectOption('ffui');

        // Switching templates retains each template's data, then restores the student draft.
        await editor.locator('#template-select').selectOption('operator');
        await editor.locator('#codename').fill('干员草稿');
        await editor.locator('#template-select').selectOption('rhodes-academy-student');
        assert.equal(await editor.locator('#codename').inputValue(), '学生档案测试');
        assert.equal(await editor.locator('#student-number').inputValue(), '202411020229');
        assert.equal(await editor.locator('#race').inputValue(), '菲林');

        // Import an editable avatar; only the photo window should change.
        const preview = editor.locator('#preview');
        const palette = editor.locator('#student-palette');
        assert.equal(await palette.inputValue(), 'amiya');
        assert.deepEqual(await palette.locator('option').evaluateAll(options => options.map(option => option.value)),
            ['amiya', 'sniper', 'medic', 'vanguard', 'guard', 'defender', 'caster', 'supporter', 'specialist']);
        const beforeAvatar = await preview.evaluate(canvas => canvas.toDataURL());
        const avatar = await frame.evaluate(() => {
            const canvas = document.createElement('canvas'); canvas.width = 128; canvas.height = 160;
            const ctx = canvas.getContext('2d'); ctx.fillRect(0, 0, 128, 160);
            return canvas.toDataURL('image/png');
        });
        await editor.locator('#import-json').setInputFiles({ name: 'profile.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify({
            version: 2, template: 'rhodes-academy-student', studentOutput: 'ffui', codename: '学生档案测试', codenameUnset: false,
            studentNumber: '202411020229', studentNumberUnset: false, race: '菲林', avatar,
        })) });
        await editor.locator('#file-status').filter({ hasText: '已导入' }).waitFor();
        assert.deepEqual(await preview.evaluate(canvas => {
            const ctx = canvas.getContext('2d');
            return [[100, 200], [285, 200]].map(([x, y]) => ctx.getImageData(x, y, 1, 1).data[0]);
        }), [0, 255]);
        assert.equal(await palette.inputValue(), 'amiya', 'earlier v2 JSON defaults to 阿米娅');
        await palette.selectOption('sniper');
        await editor.locator('#student-base-status').filter({ hasText: '正在加载「狙击」' }).waitFor();
        for (const id of ['send', 'download-png', 'download-bin']) assert(await editor.locator(`#${id}`).isDisabled());
        await palette.selectOption('guard');
        const samplePixel = (x, y) => preview.evaluate((canvas, [x, y]) =>
            Array.from(canvas.getContext('2d').getImageData(x, y, 1, 1).data), [x, y]);
        await frame.waitForFunction(() => document.getElementById('student-base-status').textContent === '');
        const sniperResponse = page.waitForResponse('**/student-palettes/fullscreen/sniper.png');
        deliverSniper();
        await (await sniperResponse).finished();
        assert.deepEqual(await samplePixel(700, 465), [0, 0, 0, 255]);
        assert.deepEqual(await samplePixel(100, 200), [0, 0, 0, 255]);
        assert.equal(await editor.locator('#race').inputValue(), '菲林');
        await palette.selectOption('medic');
        await editor.locator('#student-base-status').filter({ hasText: '「医疗」底图不可用' }).waitFor();
        assert.deepEqual(await samplePixel(700, 465), [0, 0, 0, 0]);
        for (const id of ['send', 'download-png', 'download-bin']) assert(await editor.locator(`#${id}`).isDisabled());
        assert(await editor.locator('#export-json').isEnabled());
        await palette.selectOption('amiya');
        await editor.locator('#remove-avatar').click();
        assert.equal(await preview.evaluate(canvas => canvas.toDataURL()), beforeAvatar);
        await palette.selectOption('guard');

        // Check an invalid birthday and then upload the valid leap-day example.
        await editor.locator('#student-number').fill('202411020230');
        assert(await editor.locator('#send').isDisabled());
        await editor.locator('#student-number').fill('202411020229');
        // Even mono full-screen must not touch files if only the old color capability is present.
        await page.evaluate(() => { mockArk.fullscreenSupported = false; });
        await editor.locator('#send').click();
        await editor.locator('#device-status').filter({ hasText: '不支持全屏学生证' }).waitFor();
        assert.equal(await page.evaluate(() => mockArk.starts), 0);
        await page.evaluate(() => { mockArk.fullscreenSupported = true; });
        await editor.locator('#send').click();
        assert(await editor.locator('#template-select').isDisabled());
        assert(await palette.isDisabled());
        await editor.locator('#device-status').filter({ hasText: '回读一致' }).waitFor();
        const received = await page.evaluate(() => mockArk.files);
        assert.deepEqual(received.map(file => file.name || file), ['app/pass/profile.bin', 'app/pass/profile.json']);
        const [receivedBin, receivedJson] = received;
        assert.equal(receivedBin.bytes.length, 43216);
        assert.deepEqual(receivedBin.bytes.slice(0, 16), [70, 70, 85, 73, 1, 1, 224, 1, 208, 2, 0, 0, 0, 0, 0, 0]);
        const studentProfile = JSON.parse(new TextDecoder().decode(Uint8Array.from(receivedJson.bytes)));
        assert.deepEqual(studentProfile, { version: 2, template: 'rhodes-academy-student', palette: 'guard', studentOutput: 'ffui', codename: '学生档案测试',
            codenameUnset: false, studentNumber: '202411020229', studentNumberUnset: false, race: '菲林', avatar: null, avatarMode: 'mono' });
        const i = 700 * 480 + 479 - 465;
        assert(receivedBin.bytes[16 + (i >> 3)] & (0x80 >> (i & 7)), 'selected guard marker is black in the transmitted BIN');

        // A valid read restores the received palette and template without changing the other draft.
        await palette.selectOption('amiya');
        await editor.locator('#template-select').selectOption('operator');
        assert(await palette.isHidden());
        page.once('dialog', dialog => dialog.accept());
        await editor.locator('#read-device').click();
        await editor.locator('#device-status').filter({ hasText: '已从设备读取资料' }).waitFor();
        assert.equal(await editor.locator('#template-select').inputValue(), 'rhodes-academy-student');
        assert.equal(await palette.inputValue(), 'guard');
        await editor.locator('#template-select').selectOption('operator');
        assert.equal(await editor.locator('#codename').inputValue(), '干员草稿');
        await editor.locator('#template-select').selectOption('rhodes-academy-student');

        // Unsupported color must be rejected before writing either file.
        await page.evaluate(() => { mockArk.colorSupported = false; });
        await editor.locator('#avatar-mode').selectOption('color');
        const sample = await editor.locator('#preview').evaluate(canvas => {
            const ctx = canvas.getContext('2d');
            return [[10, 20], [10, 460], [295, 270], [719, 40]].map(([x, y]) =>
                Array.from(ctx.getImageData(x, y, 1, 1).data));
        });
        assert.deepEqual(sample, [[21, 60, 135, 255], [180, 71, 32, 255], [0, 0, 0, 255], [255, 255, 255, 255]]);
        await editor.locator('#send').click();
        await editor.locator('#device-status').filter({ hasText: '不支持彩色通行证' }).waitFor();
        assert.equal(await page.evaluate(() => mockArk.files.length), 2);
        await editor.locator('#avatar-mode').selectOption('mono');

        // New default uses bit11, not the I006 image-app bit5. The latter alone negotiated FFUI.
        await output.selectOption('i006');
        await frame.waitForFunction(() => !document.getElementById('send').disabled);
        await page.evaluate(() => { mockArk.files.length = 0; mockArk.colorSupported = true; });
        await editor.locator('#send').click();
        await editor.locator('#device-status').filter({ hasText: '已改用六色 ColorFast' }).waitFor({ timeout: 45000 });
        assert.equal(await page.evaluate(() => mockArk.files[0]?.bytes.length), 172816);
        await page.evaluate(() => { mockArk.files.length = 0; mockArk.i006Supported = true; });
        await editor.locator('#send').click();
        await editor.locator('#device-status').filter({ hasText: '已发送I006 512 色位图' }).waitFor({ timeout: 90000 });
        const abc9 = await page.evaluate(() => ({ names: mockArk.files.map(file => file.name),
            length: mockArk.files[0]?.bytes.length, header: mockArk.files[0]?.bytes.slice(4, 16),
            output: JSON.parse(mockArk.profileJson).studentOutput }));
        assert.deepEqual(abc9, { names: ['app/pass/profile.bin', 'app/pass/profile.json'], length: 691232,
            header: [208, 2, 224, 1, 0, 127, 1, 0, 65, 66, 67, 57], output: 'i006' });

        // Device data with an unsupported version or invalid JSON must not replace the draft.
        await editor.locator('#codename').fill('本地保留草稿');
        await page.evaluate(() => { mockArk.profileJson = '{"version":99}'; });
        page.once('dialog', dialog => dialog.accept());
        await editor.locator('#read-device').click();
        await editor.locator('#device-status').filter({ hasText: '已保留' }).waitFor();
        assert.equal(await editor.locator('#codename').inputValue(), '本地保留草稿');
        await page.evaluate(() => { mockArk.profileJson = '{broken'; });
        page.once('dialog', dialog => dialog.accept());
        await editor.locator('#read-device').click();
        await editor.locator('#device-status').filter({ hasText: '已保留' }).waitFor();
        assert.equal(await editor.locator('#codename').inputValue(), '本地保留草稿');

        await page.setViewportSize({ width: 360, height: 740 });
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'narrow viewport must not overflow horizontally');
        assert(await hostFrame.isVisible());

        // Reload restores the palette from the draft, even outside the iframe.
        await page.route('**/student-palettes/fullscreen/amiya.png', route => route.fulfill({ status: 404 }));
        await page.goto('http://127.0.0.1:8768/ark-pass-editor/', { waitUntil: 'networkidle' });
        await page.locator('#template-select').selectOption('rhodes-academy-student');
        assert.equal(await page.locator('#student-palette').inputValue(), 'guard');
        const unknown = { ...studentProfile, palette: 'unknown' };
        await page.locator('#import-json').setInputFiles({ name: 'profile.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(unknown)) });
        await page.locator('#file-status').filter({ hasText: '配色不支持' }).waitFor();
        assert.equal(await page.locator('#student-palette').inputValue(), 'guard');
        assert.equal(await page.locator('#codename').inputValue(), '本地保留草稿');
        // A checkout without local artwork still supports editing and JSON export.
        await page.locator('#student-palette').selectOption('amiya');
        await page.locator('#student-base-status').filter({ hasText: '「阿米娅」底图不可用' }).waitFor();
        await page.locator('#codename').fill('离线草稿');
        await page.locator('#student-number').fill('202411020229');
        for (const id of ['send', 'download-png', 'download-bin']) assert(await page.locator(`#${id}`).isDisabled());
        assert(await page.locator('#export-json').isEnabled());
        assert.deepEqual(errors, []);
        console.log('PASS Ark student-pass template selection, two-file BLE transfer/readback, invalid device data preservation, and narrow layout');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
