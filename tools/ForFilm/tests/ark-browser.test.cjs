// Focused integration with a simulated BLE peripheral; never writes real hardware.
// Run with a tools/ HTTP server at 127.0.0.1:8768 and Playwright available.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

async function mockBluetooth() {
    const profile = { version: 3, kind: 'operator', output: 'i006', codename: '设备档案', codenameUnset: false,
        number: 'ARK-001', numberUnset: false, affiliation: '罗德岛', signature: '测试资料', avatar: null };
    const mock = window.mockArk = { packets: [], holdProfile: false, badProfile: false, direct: 3, restoring: 0, app: 5, files: [] };
    const encode = value => new TextEncoder().encode(value);
    class Characteristic extends EventTarget {
        properties = { write: true };
        async startNotifications() { return this; }
        async writeValue(raw) {
            if (!peripheral.gatt.connected) throw new Error('Disconnected');
            const bytes = new Uint8Array(raw); const ch = bytes[1];
            mock.packets.push([...bytes]);
            await new Promise(resolve => setTimeout(resolve, 3));
            let payload;
            if (ch === 0x42) payload = [2, 2, 208, 1, 224];
            else if (ch === 0x23) payload = [82];
            else if ([0x26, 0x28, 0x31, 0x3A].includes(ch)) payload = [0];
            else if (ch === 0x2A) payload = [0, 30];
            else if (ch === 0x54 || ch === 0x55) payload = [0, ...encode('FRAMEFILMARK-测试'), 0];
            else if (ch === 0x4B) mock.app = bytes[3];
            else if (ch === 0x4C) payload = [mock.app];
            else if (ch === 0x59) payload = [1, 0, 0, 128, 0]; // compact I006 + ColorFast (bit15)
            else if (ch === 0x52) { mock.restoring = 2; payload = [0]; }
            else if (ch === 0x51) {
                if (mock.restoring && --mock.restoring === 0) mock.direct = 6;
                payload = [mock.direct, 100, 0, 0, 0, 1, 0, 0, 0, 1, 0];
            } else if (ch === 0x53 && !mock.holdProfile) {
                const data = encode(JSON.stringify(profile));
                const offset = new DataView(bytes.buffer).getUint32(3);
                const header = new Uint8Array(9); const view = new DataView(header.buffer);
                header[0] = mock.badProfile ? 3 : 0;
                view.setUint32(1, offset); view.setUint32(5, data.length);
                payload = [...header, ...data.slice(offset, offset + bytes[7])];
            } else if (ch === 0) mock.files.push(new TextDecoder().decode(bytes.slice(3, -1)).replace(/\0$/, ''));
            if (payload) {
                const reply = new Uint8Array([0x55, ch, payload.length, ...payload, 0]);
                reply[reply.length - 1] = reply.slice(0, -1).reduce((sum, v) => sum + v, 0) & 255;
                this.value = new DataView(reply.buffer);
                this.dispatchEvent(new Event('characteristicvaluechanged'));
            }
        }
    }
    const characteristic = new Characteristic();
    const peripheral = new EventTarget();
    peripheral.name = 'FRAMEFILMARK-测试';
    peripheral.id = 'mock-ark';
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

module.exports = { mockBluetooth };
if (require.main === module) (async () => {
    const browser = await chromium.launch({ headless: true, channel: 'msedge' });
    try {
        const page = await browser.newPage({ viewport: { width: 1280, height: 960 }, reducedMotion: 'reduce' });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
        await page.addInitScript(mockBluetooth);
        await page.goto('http://127.0.0.1:8768/ForFilm/', { waitUntil: 'networkidle' });
        await page.locator('[data-page="pass-page"]').click();
        const editor = page.frameLocator('#ark-pass-editor');
        await editor.locator('#codename').fill('未保存草稿');
        await editor.locator('#number').fill('LOCAL-1');
        assert(await editor.locator('#send').isDisabled());
        await page.locator('[data-page="bluetooth-page"]').click();
        await page.locator('#scan-button').click();
        await page.waitForFunction(() => ArkDevice.connected());
        assert.equal(await page.evaluate(() => currentDeviceType), 'FRAMEFILMARK');
        await page.locator('[data-page="pass-page"]').click();
        await editor.locator('#read-device').click();
        await editor.locator('#codename').filter({}).waitFor();
        await page.waitForFunction(() => document.querySelector('#ark-pass-editor').contentDocument.querySelector('#codename').value === '设备档案');
        await editor.locator('#codename').fill('保留当前草稿');
        await page.evaluate(() => { mockArk.badProfile = true; });
        await editor.locator('#read-device').click();
        await editor.locator('#device-status').filter({ hasText: '已保留' }).waitFor();
        assert.equal(await editor.locator('#codename').inputValue(), '保留当前草稿');
        await page.evaluate(() => { mockArk.badProfile = false; mockArk.holdProfile = true; });
        await editor.locator('#read-device').click();
        await page.waitForFunction(() => ArkDevice.busy());
        assert(await page.locator('#ark-pass-open').isDisabled());
        await editor.locator('#cancel').click();
        await page.waitForFunction(() => !ArkDevice.busy());
        assert.equal(await editor.locator('#codename').inputValue(), '保留当前草稿');
        await page.evaluate(() => { mockArk.holdProfile = false; });
        await page.locator('[data-page="bluetooth-page"]').click();
        await page.locator('#ark-reconnect-button').click();
        await page.waitForFunction(() => ArkDevice.connected());
        await page.locator('[data-page="pass-page"]').click();
        await editor.locator('#send').click();
        await editor.locator('#device-status').filter({ hasText: '两个文件已发送' }).waitFor();
        assert.deepEqual(await page.evaluate(() => mockArk.files), ['app/pass/profile.bin', 'app/pass/profile.json']);
        await page.locator('#ark-pass-open').click();
        await page.waitForFunction(() => mockArk.app === 6 && !ArkDevice.busy());
        await page.locator('[data-page="config-page"]').click();
        await page.locator('#ark-open-settings').click();
        await page.waitForFunction(() => mockArk.app === 4 && !ArkDevice.busy());
        await page.locator('#ark-direct-refresh').click();
        await page.locator('#ark-direct-status').filter({ hasText: '不代表会话已结束' }).waitFor();
        await page.locator('#ark-direct-cancel').click();
        await page.locator('#ark-direct-status').filter({ hasText: '已取消' }).waitFor();
        await page.waitForFunction(() => !ArkDevice.busy());
        const countBefore = await page.evaluate(() => mockArk.packets.filter(p => p[1] === 0x10).length);
        await page.evaluate(() => uploadOtaFileViaBle(new Uint8Array(64)));
        assert.equal(await page.evaluate(() => mockArk.packets.filter(p => p[1] === 0x10).length), countBefore);
        const out = path.join(process.cwd(), '.output/ark/web-adaptation');
        fs.mkdirSync(out, { recursive: true });
        await page.screenshot({ path: path.join(out, 'settings-desktop.png'), fullPage: true });
        await page.setViewportSize({ width: 390, height: 844 });
        await page.locator('[data-page="pass-page"]').click();
        await page.screenshot({ path: path.join(out, 'pass-mobile.png'), fullPage: true });
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
        assert.deepEqual(errors, []);
        console.log('PASS simulated BLE integration: shared connection, draft/read/cancel/reconnect/upload, device navigation, direct state/cancel, OTA preflight, mobile layout');
    } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
