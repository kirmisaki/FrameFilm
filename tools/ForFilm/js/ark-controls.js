/* ForFilm Ark actions. Keep unsupported firmware settings on the device. */
(() => {
    'use strict';
    const $ = id => document.getElementById(id);
    const locks = new Map();
    let wasBusy = false;
    let wasConnected = false;
    let directSession = 0;
    let sawArk = false;
    const DIRECT_STATES = ['空闲', '正在连接临时 WiFi', '正在下载', '正在保存或恢复网络', '已完成', '失败', '已取消'];
    const DIRECT_ERRORS = ['', 'WiFi 连接失败', 'HTTP 下载失败', '文件保存失败', '已请求取消', '网络恢复失败', '设备资源不足'];

    // Async producers update availability without releasing the current operation lock.
    window.arkSetControlDisabled = (el, disabled) => {
        if (locks.has(el)) {
            locks.set(el, disabled);
            el.disabled = true;
        } else el.disabled = disabled;
    };

    function status(message, error = false) {
        $('ark-operation-status').textContent = message;
        if (error && typeof showMessage === 'function') showMessage(message, 'error');
    }

    function sync() {
        const api = window.ArkDevice;
        const connected = !!api?.connected();
        const busy = !!api?.busy();
        if (connected !== wasConnected) {
            wasConnected = connected;
            directSession++;
            $('ark-direct-status').textContent = connected ? '尚未查询当前设备' : '设备已断开，请连接后重新查询';
            $('ark-direct-progress').value = 0;
        }
        sawArk ||= connected;
        // Keep navigation available; cancel remains usable while other actions lock.
        if (busy && !wasBusy) {
            document.querySelectorAll('button,input,select,textarea').forEach(el => {
                if (el.matches('[data-ark-cancel], [data-ark-local]')) return;
                locks.set(el, el.disabled);
                el.disabled = true;
            });
        } else if (!busy && wasBusy) {
            locks.forEach((disabled, el) => { el.disabled = disabled; });
            locks.clear();
        }
        wasBusy = busy;
        // 通行证 / 卡包只对 Ark 设备开放：连接后显示入口，断开时隐藏并退回 Film 页
        document.body.classList.toggle('show-ark-tabs', connected);
        if (!connected) {
            const activeNav = document.querySelector('.nav-item.active');
            if (activeNav) {
                const page = activeNav.getAttribute('data-page');
                if (page === 'pass-page' || page === 'wallet-page') {
                    const fallback = document.querySelector('.nav-item[data-page="convert-page"]');
                    if (fallback) fallback.click();
                }
            }
        }
        document.querySelectorAll('[data-ark-connected]').forEach(el => {
            el.disabled = !connected || busy;
        });
        $('ark-system-section').hidden = !sawArk || (typeof currentDeviceType !== 'undefined' && currentDeviceType !== 'FRAMEFILMARK' && !!server?.connected);
        $('ark-reconnect-button').disabled = busy || connected || typeof device === 'undefined' || !device?.gatt || !/^FRAMEFILMARK/i.test(device.name || '');
        $('ark-operation-cancel').hidden = !busy;
        $('ark-pass-connection').textContent = connected
            ? (busy ? '设备正在处理操作；草稿会保留。' : '已共享 ForFilm 的 Ark 蓝牙连接，可读取或发送资料。')
            : '离线可编辑、预览和导出；读取或发送前请先连接 Ark。';
        if (!busy) status(connected ? 'Ark 已连接。通行证、照片发送和设备设置共用此连接。' : 'Ark 未连接，通行证编辑草稿仍会保留。');
    }

    async function action(task) {
        try { await task(); } catch (error) { status(error.message || '操作失败，请重试', true); }
    }

    async function openDevicePage(id, title) {
        await window.ArkDevice.run(`打开${title}`, async ctx => {
            await ctx.send(0x4B, Uint8Array.of(id));
            await new Promise(resolve => setTimeout(resolve, 300));
            const current = await ctx.request(0x4C, new Uint8Array());
            if (current.length !== 1 || current[0] !== id) throw new Error('设备未切换页面，可能正在刷新或处于休眠流程，请稍后重试。');
        });
        status(`设备已进入${title}，电子纸刷新可能稍有延迟。`);
    }

    function renderDirect(payload) {
        if (payload.length !== 11 || payload[0] > 6 || payload[1] > 100) throw new Error('直传状态回包无效');
        const data = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
        const state = payload[0], progress = payload[1], error = payload[2];
        const received = data.getUint32(3), total = data.getUint32(7);
        let text = `${DIRECT_STATES[state]} · ${progress}%`;
        if (total) text += ` · ${received.toLocaleString()} / ${total.toLocaleString()} 字节`;
        if (error) text += ` · ${DIRECT_ERRORS[error] || '未知错误 ' + error}`;
        if (state === 3) text += '（100% 不代表会话已结束）';
        $('ark-direct-status').textContent = text;
        $('ark-direct-progress').value = progress;
        return state;
    }

    async function queryDirect(cancel) {
        const session = directSession;
        try {
            await window.ArkDevice.run(cancel ? '取消设备 WiFi 直传' : '查询 WiFi 直传', async ctx => {
                if (cancel) {
                    const reply = await ctx.request(0x52, new Uint8Array());
                    if (reply.length !== 1 || reply[0] !== 0) throw new Error('设备未接受取消，请查询状态后重试。');
                }
                const until = Date.now() + 45000;
                do {
                    const payload = await ctx.request(0x51, new Uint8Array());
                    if (session !== directSession) return;
                    const state = renderDirect(payload);
                    if (!cancel || state === 0 || state >= 4) return;
                    await new Promise(resolve => setTimeout(resolve, 1000));
                } while (Date.now() < until);
                throw new Error('取消已请求，但设备尚未确认清理完成；请稍后重新查询状态。');
            });
        } catch (error) {
            if (session !== directSession) return;
            $('ark-direct-status').textContent = error.message;
            $('ark-direct-progress').value = 0;
            throw error;
        }
    }

    function init() {
        $('ark-reconnect-button').addEventListener('click', () => action(() => window.ArkDevice.reconnect()));
        $('ark-operation-cancel').addEventListener('click', () => window.ArkDevice.cancel());
        $('ark-pass-connect').addEventListener('click', () => document.querySelector('[data-page="bluetooth-page"]').click());
        $('ark-pass-open').addEventListener('click', () => action(() => openDevicePage(6, '通行证')));
        $('ark-open-settings').addEventListener('click', () => action(() => openDevicePage(4, '系统设置')));
        $('ark-open-menu').addEventListener('click', () => action(() => openDevicePage(5, '主菜单')));
        $('ark-direct-refresh').addEventListener('click', () => action(() => queryDirect(false)));
        $('ark-direct-cancel').addEventListener('click', () => action(() => queryDirect(true)));
        const name = $('ark-device-name-suffix');
        name.addEventListener('input', () => {
            $('ark-name-byte-count').textContent = `${new TextEncoder().encode(name.value).length} / 16 字节`;
        });
        new MutationObserver(() => name.dispatchEvent(new Event('input')))
            .observe($('ark-device-name-current'), { childList: true, characterData: true, subtree: true });
        window.addEventListener('ark-device-state', event => {
            sync();
            if (event.detail.busy) status(`正在${event.detail.label}…`);
        });
        const editor = $('ark-pass-editor');
        editor.addEventListener('load', () => {
            const body = editor.contentDocument?.body;
            if (!body) return;
            const resize = () => { editor.style.height = Math.ceil(body.getBoundingClientRect().height + 24) + 'px'; };
            new ResizeObserver(resize).observe(body);
            resize();
        });
        sync();
    }
    window.addEventListener('DOMContentLoaded', init);
})();
