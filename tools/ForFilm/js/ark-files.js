/* Ark 文件管理：目录列表 / 按名删除 / 分块读取 / 动图按名播放
 *
 * 与固件 0x5B（只读目录）、0x60（按名删除）、0x61（分块读取）、0x4F（动图按名播放）
 * 配套。目录号：1=/sdcard/film 2=/sdcard/animation 3=/sdcard/screenshot。
 */
(function (root) {
    'use strict';

    var DIR_FILM = 1;
    var DIR_ANIM = 2;
    var DIR_SHOT = 3;

    var CH_ANIM_PARAM = 0x49;
    var CH_ANIM_PLAY = 0x4F;
    var CH_CAPS = 0x59;
    var CH_CATALOG = 0x5B;
    var CH_DELETE = 0x60;
    var CH_READ = 0x61;

    var ANIM_TAG_PLAY_MODE = 0x01;
    var ANIM_TAG_FRAME_MS = 0x02;
    var ANIM_TAG_LOOP_SEC = 0x03;

    var READ_CHUNK = 180;
    var CAP_FILE_MGMT = 8;
    var CAP_SCREENSHOT = 9;

    function delay(ms) { return new Promise(function (resolve) { setTimeout(resolve, ms); }); }
    function connected() { return !!(root.ArkDevice && root.ArkDevice.connected()); }
    function esc(s) {
        return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) {
            return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
        });
    }
    function be32(b, o) {
        return (((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0);
    }
    function u32(v) {
        return new Uint8Array([(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255]);
    }
    function bytesOf(text) { return new TextEncoder().encode(text); }

    function capsFlags(caps) {
        if (!caps || caps.length !== 5 || caps[0] !== 1) return -1;
        return (((caps[1] << 24) | (caps[2] << 16) | (caps[3] << 8) | caps[4]) >>> 0);
    }
    function hasCap(caps, bit) {
        var flags = capsFlags(caps);
        return flags >= 0 && (flags & (1 << bit)) !== 0;
    }
    function hasFileMgmt(caps) { return hasCap(caps, CAP_FILE_MGMT); }
    function hasScreenshot(caps) { return hasCap(caps, CAP_SCREENSHOT); }

    function catalogText(status) {
        return ['OK', 'SD 卡未挂载', '读取失败', '参数错误', '索引越界', '文件名过长'][status] || ('ERR ' + status);
    }
    function fileText(status) {
        return ['OK', '文件不存在', '读写失败', '设备忙', '参数错误'][status] || ('ERR ' + status);
    }
    function playText(status) {
        return ['OK', '文件不存在或无法播放', '当前固件不支持', '设备忙或正在休眠', '参数错误'][status] || ('ERR ' + status);
    }

    async function requireCap(ctx, bit, label) {
        var caps = await ctx.request(CH_CAPS, new Uint8Array(), 3500).catch(function () { return null; });
        if (capsFlags(caps) < 0) throw new Error('设备未返回能力信息，请重连后重试');
        if (!hasCap(caps, bit)) throw new Error('当前固件不支持' + label + '，请升级 Ark 固件');
        return caps;
    }

    /* 读取一个目录的全部文件名（只读，不切换设备当前目录） */
    async function list(ctx, dir, needCaps) {
        if (needCaps !== false) await requireCap(ctx, CAP_FILE_MGMT, '文件管理');

        var out = [];
        var count = 0;
        for (var i = 0; i < 4096; i++) {
            var req = new Uint8Array(5);
            req[0] = dir;
            req.set(u32(i), 1);
            var rsp = await ctx.request(CH_CATALOG, req, 5000);
            if (rsp.length < 5) throw new Error('目录列表回包无效');
            var status = rsp[0];
            count = be32(rsp, 1);
            if (status === 4) break;      /* index >= count：正常结束 */
            if (status !== 0) throw new Error('读取目录失败：' + catalogText(status));

            var end = 5;
            while (end < rsp.length && rsp[end] !== 0) end++;
            if (end === 5 && count > 0) throw new Error('目录列表文件名无效');
            out.push(new TextDecoder('utf-8').decode(rsp.slice(5, end)));
            if (i + 1 >= count) break;
        }
        return out;
    }

    async function remove(ctx, dir, name) {
        await requireCap(ctx, CAP_FILE_MGMT, '文件管理');
        var enc = bytesOf(name);
        var payload = new Uint8Array(1 + enc.length + 1);
        payload[0] = dir;
        payload.set(enc, 1);
        var rsp = await ctx.request(CH_DELETE, payload, 8000);
        if (rsp.length < 1) throw new Error('删除回包无效');
        if (rsp[0] !== 0) throw new Error('删除失败：' + fileText(rsp[0]));
    }

    async function read(ctx, dir, name, onProgress) {
        await requireCap(ctx, CAP_FILE_MGMT, '文件读取');
        var enc = bytesOf(name);
        var parts = [];
        var offset = 0;
        var total = 0;

        for (var guard = 0; guard < 100000; guard++) {
            var payload = new Uint8Array(1 + 4 + 1 + enc.length + 1);
            payload[0] = dir;
            payload.set(u32(offset), 1);
            payload[5] = READ_CHUNK;
            payload.set(enc, 6);

            var rsp = await ctx.request(CH_READ, payload, 8000);
            if (rsp.length < 9) throw new Error('文件读取回包无效');
            if (rsp[0] !== 0) throw new Error('读取失败：' + fileText(rsp[0]));

            total = be32(rsp, 5);
            var chunk = rsp.slice(9);
            if (!chunk.length) {
                if (offset < total) throw new Error('设备返回空数据');
                break;
            }
            parts.push(chunk);
            offset += chunk.length;
            if (onProgress) onProgress(offset, total);
            if (offset >= total) break;
        }

        var out = new Uint8Array(total || offset);
        var pos = 0;
        for (var i = 0; i < parts.length; i++) {
            out.set(parts[i], pos);
            pos += parts[i].length;
        }
        return out;
    }

    /* 按文件名播放动图：先下发 0x49 播放参数，再发 0x4F 切到动图 app */
    async function playAnimation(ctx, name, options) {
        options = options || {};
        await requireCap(ctx, CAP_FILE_MGMT, '文件管理');

        var tlv = [];
        if (typeof options.playMode === 'number') {
            tlv.push(ANIM_TAG_PLAY_MODE, 1, options.playMode & 0xFF);
        }
        if (typeof options.frameMs === 'number') {
            var ms = Math.max(100, Math.min(2000, Math.round(options.frameMs)));
            tlv.push(ANIM_TAG_FRAME_MS, 2, (ms >> 8) & 0xFF, ms & 0xFF);
        }
        if (typeof options.loopSeconds === 'number') {
            var sec = Math.max(0, Math.min(600, Math.round(options.loopSeconds)));
            tlv.push(ANIM_TAG_LOOP_SEC, 2, (sec >> 8) & 0xFF, sec & 0xFF);
        }
        if (tlv.length) {
            await ctx.send(CH_ANIM_PARAM, Uint8Array.from(tlv));
            await delay(80);
        }

        var enc = bytesOf(name);
        var payload = new Uint8Array(enc.length + 1);
        payload.set(enc, 0);
        var rsp = await ctx.request(CH_ANIM_PLAY, payload, 20000);
        if (rsp.length < 1) throw new Error('播放回包无效');
        if (rsp[0] !== 0) throw new Error('播放失败：' + playText(rsp[0]));
    }

    function run(label, fn) {
        if (!root.ArkDevice) return Promise.reject(new Error('ArkDevice 未加载'));
        return root.ArkDevice.run(label, fn);
    }

    /* ==================== 设置页：截图管理 ==================== */

    var m_shots = [];
    var m_shotBusy = false;
    var m_shotLoadedFor = false;
    var m_connState = null;

    function shotStatus(text, error) {
        var el = document.getElementById('screenshot-status');
        if (!el) return;
        el.textContent = text;
        el.classList.toggle('is-error', !!error);
    }

    function renderShots() {
        var list = document.getElementById('screenshot-list');
        if (!list) return;
        if (!m_shots.length) {
            list.innerHTML = '<p class="field-hint">还没有截图。在设备上双击上键即可截图。</p>';
            return;
        }
        var html = '<div class="wallet-rows">';
        for (var i = 0; i < m_shots.length; i++) {
            html += '<div class="wallet-row">' +
                '<div class="wallet-row__body">' +
                '<div class="wallet-row__name">' + esc(m_shots[i]) + '</div>' +
                '<div class="wallet-row__meta">/sdcard/screenshot · MonoFast .film</div>' +
                '</div>' +
                '<div class="wallet-row-actions">' +
                '<button type="button" class="secondary-button" data-shot-action="download" data-shot-index="' + i + '">下载</button>' +
                '<button type="button" class="secondary-button" data-shot-action="delete" data-shot-index="' + i + '">删除</button>' +
                '</div></div>';
        }
        html += '</div>';
        list.innerHTML = html;
    }

    function syncShotVisibility() {
        var card = document.getElementById('screenshot-card');
        if (!card) return;
        var ark = typeof currentDeviceType !== 'undefined' && currentDeviceType === 'FRAMEFILMARK';
        card.hidden = !(connected() && ark);
        if (card.hidden) m_shotLoadedFor = false;
    }

    async function refreshShots() {
        if (m_shotBusy) return;
        if (!connected()) { shotStatus('请先连接 Ark（通行证版）。', true); return; }

        m_shotBusy = true;
        shotStatus('正在读取截图…');
        try {
            await run('读取设备截图', async function (ctx) {
                await requireCap(ctx, CAP_SCREENSHOT, '截图功能');
                m_shots = await list(ctx, DIR_SHOT, false);
                renderShots();
                m_shotLoadedFor = true;
            });
            shotStatus('已读取 ' + m_shots.length + ' 张截图');
        } catch (e) {
            shotStatus('读取失败：' + e.message, true);
        } finally {
            m_shotBusy = false;
        }
    }

    function downloadBlob(name, bytes) {
        var blob = new Blob([bytes], { type: 'application/octet-stream' });
        var url = URL.createObjectURL(blob);
        var a = document.createElement('a');
        a.href = url;
        a.download = name;
        document.body.appendChild(a);
        a.click();
        a.remove();
        setTimeout(function () { URL.revokeObjectURL(url); }, 5000);
    }

    async function downloadShot(index) {
        var name = m_shots[index];
        if (!name || m_shotBusy) return;
        m_shotBusy = true;
        shotStatus('正在下载 ' + name + '…');
        try {
            var bytes = await run('下载截图', async function (ctx) {
                return read(ctx, DIR_SHOT, name, function (done, total) {
                    var pct = total ? Math.round(done / total * 100) : 0;
                    shotStatus('正在下载 ' + name + ' · ' + pct + '%');
                });
            });
            downloadBlob(name, bytes);
            shotStatus('已下载 ' + name + '（' + bytes.length + ' 字节）');
        } catch (e) {
            shotStatus('下载失败：' + e.message, true);
        } finally {
            m_shotBusy = false;
        }
    }

    async function deleteShot(index) {
        var name = m_shots[index];
        if (!name || m_shotBusy) return;
        if (!confirm('从设备 SD 卡删除截图 ' + name + '？')) return;

        m_shotBusy = true;
        shotStatus('正在删除 ' + name + '…');
        try {
            await run('删除截图', function (ctx) { return remove(ctx, DIR_SHOT, name); });
            shotStatus('已删除 ' + name);
            m_shots.splice(index, 1);
            renderShots();
        } catch (e) {
            shotStatus('删除失败：' + e.message, true);
        } finally {
            m_shotBusy = false;
        }
    }

    function initScreenshotUI() {
        var card = document.getElementById('screenshot-card');
        var list = document.getElementById('screenshot-list');
        var refresh = document.getElementById('screenshot-refresh');
        if (!card || !list || !refresh) return;

        refresh.addEventListener('click', refreshShots);
        list.addEventListener('click', function (ev) {
            var btn = ev.target.closest ? ev.target.closest('button[data-shot-action]') : null;
            if (!btn) return;
            var index = parseInt(btn.getAttribute('data-shot-index'), 10);
            if (!(index >= 0 && index < m_shots.length)) return;
            if (btn.getAttribute('data-shot-action') === 'download') downloadShot(index);
            else deleteShot(index);
        });

        window.addEventListener('ark-device-state', function () {
            var now = connected();
            syncShotVisibility();
            /* 只处理"连接状态翻转"；run 过程中的 busy 事件不能触发重试，
               否则失败后会在每次操作结束时再自动刷新，形成循环 */
            if (now === m_connState) return;
            m_connState = now;
            if (!now) {
                m_shotLoadedFor = false;
                shotStatus('设备已断开');
                return;
            }
            if (!card.hidden) refreshShots();
        });

        document.querySelectorAll('.nav-item').forEach(function (item) {
            item.addEventListener('click', function () {
                setTimeout(function () {
                    syncShotVisibility();
                    if (!card.hidden && !m_shotLoadedFor && !(root.ArkDevice && root.ArkDevice.busy())) {
                        refreshShots();
                    }
                }, 0);
            });
        });

        m_connState = connected();
        syncShotVisibility();
        if (!card.hidden && m_connState) refreshShots();
    }

    root.ArkFiles = {
        DIR_FILM: DIR_FILM,
        DIR_ANIM: DIR_ANIM,
        DIR_SHOT: DIR_SHOT,
        connected: connected,
        hasFileMgmt: hasFileMgmt,
        hasScreenshot: hasScreenshot,
        list: list,
        remove: remove,
        read: read,
        playAnimation: playAnimation,
        run: run
    };

    window.addEventListener('DOMContentLoaded', initScreenshotUI);
})(window);
