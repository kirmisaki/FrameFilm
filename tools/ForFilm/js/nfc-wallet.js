/* ForFilm · NFC 卡包（仅 Ark n16r8）
 * 协议：0x5C~0x5E 建卡，0x5F 管理，0x60~0x62 封面。
 */
(function () {
    'use strict';

    var CH_CARD_BEGIN = 0x5C;
    var CH_CARD_DATA = 0x5D;
    var CH_CARD_COMMIT = 0x5E;
    var CH_WALLET_CTRL = 0x5F;
    var CH_COVER_BEGIN = 0x60;
    var CH_COVER_DATA = 0x61;
    var CH_COVER_COMMIT = 0x62;
    var CHUNK = 180;
    var COVER_W = 440;
    var COVER_H = 200;
    var COVER_PAYLOAD = COVER_W * COVER_H / 8;

    var m_cards = [];
    var m_current = 0;

    function $(id) { return document.getElementById(id); }

    function esc(s) {
        return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) {
            return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
        });
    }

    function crc32(bytes) {
        var c = 0xFFFFFFFF;
        for (var i = 0; i < bytes.length; i++) {
            c ^= bytes[i];
            for (var j = 0; j < 8; j++) c = (c >>> 1) ^ (0xEDB88320 & -(c & 1));
        }
        return (c ^ 0xFFFFFFFF) >>> 0;
    }

    function be16(v) { return new Uint8Array([(v >> 8) & 0xFF, v & 0xFF]); }
    function be32(v) {
        return new Uint8Array([(v >>> 24) & 0xFF, (v >>> 16) & 0xFF, (v >>> 8) & 0xFF, v & 0xFF]);
    }
    function get16(b, o) { return ((b[o] << 8) | b[o + 1]) >>> 0; }
    function u8(b, o) { return b[o]; }

    function hexToBytes(text, len) {
        var clean = String(text || '').replace(/[^0-9a-fA-F]/g, '');
        if (clean.length !== len * 2) throw new Error('UID 需要 ' + (len * 2) + ' 位十六进制');
        var out = new Uint8Array(len);
        for (var i = 0; i < len; i++) out[i] = parseInt(clean.substr(i * 2, 2), 16);
        return out;
    }

    function uidText(uid, len) {
        var s = '';
        for (var i = 0; i < len; i++) s += (uid[i] < 16 ? '0' : '') + uid[i].toString(16).toUpperCase();
        return s;
    }

    function kindName(kind) {
        return ['M1 UID', 'NTAG213', 'NTAG215', 'NTAG216', 'M1 1K', 'M1 2K', 'M1 4K'][kind] || ('KIND ' + kind);
    }

    function randomUid() {
        var uid = new Uint8Array(7);
        uid[0] = 0x04;
        for (var i = 1; i < 7; i++) uid[i] = Math.floor(Math.random() * 256);
        return uid;
    }

    function statusText(status) {
        return ['OK', 'BUSY', 'PARAM', 'NO SD', 'NOT FOUND', 'NO MEM', 'SAVE FAIL', 'EMU FAIL'][status] || ('ERR ' + status);
    }

    function connected() {
        return !!(window.ArkDevice && window.ArkDevice.connected());
    }

    function setConnection(text) {
        var el = $('wallet-connection');
        if (el) el.textContent = text;
    }

    function run(label, fn) {
        if (!window.ArkDevice) return Promise.reject(new Error('ArkDevice 未加载'));
        return window.ArkDevice.run(label, async function (ctx) {
            var caps = await ctx.request(0x59, new Uint8Array(), 2500).catch(function () { return null; });
            if (caps && caps.length === 5 && caps[0] === 1) {
                if ((caps[4] & 0x80) !== 0) return fn(ctx);
                // Old NFC and old SoftAP both used bit4. Only probe the wallet's read command.
                if ((caps[4] & 0x50) === 0x10) {
                    var list = await ctx.request(CH_WALLET_CTRL, new Uint8Array([0]), 2500)
                        .catch(function () { return null; });
                    if (list && list.length >= 5 && list[0] === 0) return fn(ctx);
                }
            }
            throw new Error('此固件未确认支持 NFC 卡包（需 N16R8）');
        });
    }

    async function walletCtrl(ctx, payload) {
        var rsp = await ctx.request(CH_WALLET_CTRL, new Uint8Array(payload), 10000);
        if (!rsp || rsp.length < 1) throw new Error('卡包无响应');
        if (rsp[0] !== 0) throw new Error('卡包错误：' + statusText(rsp[0]));
        return rsp;
    }

    /* 解析一页 LIST 回复并追加到 out；返回本页卡片数。
       固件一帧最多 192 字节，卡片多/名字长时会分页，靠 start 参数翻页。 */
    function parsePage(rsp, out) {
        var n = rsp[4] || 0;
        var pos = 5;
        for (var i = 0; i < n; i++) {
            if (pos + 15 > rsp.length) break;
            var id = rsp[pos++];
            var kind = rsp[pos++];
            var uidLen = rsp[pos++];
            var sak = rsp[pos++];
            var atqa = [rsp[pos++], rsp[pos++]];
            var uid = rsp.slice(pos, pos + 7); pos += 7;
            var flags = rsp[pos++];
            var nameLen = rsp[pos++];
            if (pos + nameLen > rsp.length) nameLen = rsp.length - pos;
            var name = '';
            for (var k = 0; k < nameLen && pos < rsp.length; k++) name += String.fromCharCode(rsp[pos++]);
            out.push({ id: id, kind: kind, uidLen: uidLen, sak: sak, atqa: atqa, uid: uid, flags: flags, name: name });
        }
        return n;
    }

    function renderList() {
        var list = $('wallet-list');
        if (!list) return;
        if (!m_cards.length) {
            list.innerHTML = '<p class="field-hint">卡包为空。先创建一张 NTAG 卡，或在设备上读卡复制。</p>';
            return;
        }
        var html = '<div class="wallet-rows">';
        for (var i = 0; i < m_cards.length; i++) {
            var c = m_cards[i];
            var active = i === m_current ? ' style="font-weight:700"' : '';
            html += '<div class="wallet-row"' + active + '>' +
                '<div><strong>' + esc(c.name || 'CARD') + '</strong>' +
                '<div class="field-hint">' + esc(kindName(c.kind)) + ' · ' + uidText(c.uid, c.uidLen) + '</div></div>' +
                '<div class="wallet-row-actions">' +
                '<button type="button" class="secondary-button" data-wallet-select="' + c.id + '">选中</button>' +
                '<button type="button" class="secondary-button" data-wallet-rename="' + c.id + '">改名</button>' +
                '<button type="button" class="secondary-button" data-wallet-delete="' + c.id + '">删除</button>' +
                '<button type="button" class="secondary-button" data-wallet-cover="' + c.id + '">封面</button>' +
                '</div></div>';
        }
        html += '</div>';
        list.innerHTML = html;
    }

    async function refresh() {
        if (!connected()) { setConnection('请先连接 Ark（n16r8）。'); return; }
        setConnection('正在读取卡包…');
        try {
            await run('刷新 NFC 卡包', async function (ctx) {
                var all = [];
                var current = 0;
                var total = 0;
                var start = 0;
                for (var guard = 0; guard < 256; guard++) {
                    var rsp = await walletCtrl(ctx, [0x00, start]);
                    total = rsp[1] || 0;
                    current = rsp[2] || 0;
                    var cnt = parsePage(rsp, all);
                    if (!cnt) break;               // 没有更多卡
                    start += cnt;
                    if (start >= total) break;
                }
                m_cards = all;
                m_current = current;
                renderList();
            });
            setConnection('卡包已刷新 · ' + m_cards.length + ' 张卡');
        } catch (e) {
            setConnection('刷新失败：' + e.message);
        }
    }

    async function selectCard(id) {
        try {
            await run('选中卡', async function (ctx) {
                await walletCtrl(ctx, [0x01, id]);
            });
            await refresh();
        } catch (e) { setConnection('选中失败：' + e.message); }
    }

    async function deleteCard(id) {
        if (!confirm('删除这张卡？设备上的封面也会删除。')) return;
        try {
            await run('删除卡', async function (ctx) {
                await walletCtrl(ctx, [0x02, id]);
            });
            await refresh();
        } catch (e) { setConnection('删除失败：' + e.message); }
    }

    async function renameCard(id) {
        var old = '';
        for (var i = 0; i < m_cards.length; i++) if (m_cards[i].id === id) old = m_cards[i].name;
        var name = prompt('新的卡片名称（1~23 字节）', old);
        if (name == null) return;
        name = name.trim();
        if (!name) { setConnection('名称不能为空'); return; }
        var bytes = new TextEncoder().encode(name);
        if (bytes.length > 23) { setConnection('名称最多 23 字节'); return; }
        try {
            await run('重命名卡', async function (ctx) {
                var payload = [0x05, id, bytes.length];
                for (var j = 0; j < bytes.length; j++) payload.push(bytes[j]);
                await walletCtrl(ctx, payload);
            });
            await refresh();
        } catch (e) { setConnection('重命名失败：' + e.message); }
    }

    function fileToCover(file) {
        return new Promise(function (resolve, reject) {
            var img = new Image();
            var url = URL.createObjectURL(file);
            img.onload = function () {
                try {
                    var canvas = document.createElement('canvas');
                    canvas.width = COVER_W;
                    canvas.height = COVER_H;
                    var ctx = canvas.getContext('2d');
                    ctx.fillStyle = '#fff';
                    ctx.fillRect(0, 0, COVER_W, COVER_H);
                    var scale = Math.min(COVER_W / img.width, COVER_H / img.height);
                    var w = Math.round(img.width * scale);
                    var h = Math.round(img.height * scale);
                    ctx.drawImage(img, (COVER_W - w) / 2, (COVER_H - h) / 2, w, h);
                    var data = ctx.getImageData(0, 0, COVER_W, COVER_H).data;
                    var payload = new Uint8Array(COVER_PAYLOAD);
                    for (var i = 0; i < COVER_W * COVER_H; i++) {
                        var r = data[i * 4], g = data[i * 4 + 1], b = data[i * 4 + 2];
                        var luma = 0.299 * r + 0.587 * g + 0.114 * b;
                        if (luma < 128) payload[i >> 3] |= 0x80 >> (i & 7);
                    }
                    resolve(payload);
                } catch (e) { reject(e); }
                finally { URL.revokeObjectURL(url); }
            };
            img.onerror = function () { URL.revokeObjectURL(url); reject(new Error('图片读取失败')); };
            img.src = url;
        });
    }

    async function uploadCover(ctx, id, payload, onStatus) {
        var crc = crc32(payload);
        var begin = new Uint8Array(12);
        begin[0] = id;
        begin[1] = 1;
        begin.set(be16(COVER_W), 2);
        begin.set(be16(COVER_H), 4);
        begin.set(be16(COVER_PAYLOAD), 6);
        begin.set(be32(crc), 8);
        var rsp = await ctx.request(CH_COVER_BEGIN, begin, 10000);
        if (!rsp || rsp[0] !== 0) throw new Error('封面开始失败：' + statusText(rsp ? rsp[0] : 0xFF));
        for (var off = 0; off < payload.length; off += CHUNK) {
            var part = payload.slice(off, Math.min(off + CHUNK, payload.length));
            var frame = new Uint8Array(2 + part.length);
            frame.set(be16(off), 0);
            frame.set(part, 2);
            rsp = await ctx.request(CH_COVER_DATA, frame, 10000);
            if (!rsp || rsp[0] !== 0) throw new Error('封面上传失败：' + statusText(rsp ? rsp[0] : 0xFF));
            if (onStatus) onStatus(Math.min(100, Math.round((off + part.length) * 100 / payload.length)));
        }
        rsp = await ctx.request(CH_COVER_COMMIT, new Uint8Array(), 10000);
        if (!rsp || rsp[0] !== 0) throw new Error('封面提交失败：' + statusText(rsp ? rsp[0] : 0xFF));
    }

    async function createCard() {
        if (!connected()) { setConnection('请先连接 Ark（n16r8）。'); return; }
        var kind = parseInt($('wallet-kind').value, 10);
        var uid;
        try {
            var uidField = $('wallet-uid').value.trim();
            uid = uidField ? hexToBytes(uidField, 7) : randomUid();
        } catch (e) { setConnection(e.message); return; }
        var text = $('wallet-ndef').value || '';
        var textBytes = new TextEncoder().encode(text);
        if (textBytes.length > 500) { setConnection('NDEF 文本最多 500 字节'); return; }
        var name = ($('wallet-name').value || '').trim() || kindName(kind);
        var nameBytes = new TextEncoder().encode(name);
        if (nameBytes.length > 23) { setConnection('名称最多 23 字节'); return; }

        var crc = crc32(textBytes);
        var begin = [];
        begin.push(kind, 1, 7, 0x00, 0x44, 0x00);
        for (var i = 0; i < 7; i++) begin.push(uid[i]);
        begin.push(nameBytes.length);
        for (var j = 0; j < nameBytes.length; j++) begin.push(nameBytes[j]);
        var total = textBytes.length;
        begin.push((total >> 8) & 0xFF, total & 0xFF);
        begin.push((crc >>> 24) & 0xFF, (crc >>> 16) & 0xFF, (crc >>> 8) & 0xFF, crc & 0xFF);

        var file = $('wallet-cover').files && $('wallet-cover').files[0];
        var cover = null;
        if (file) {
            try { cover = await fileToCover(file); }
            catch (e) { setConnection('封面转换失败：' + e.message); return; }
        }

        setConnection('正在创建 ' + kindName(kind) + '…');
        try {
            var cardId = await run('创建 NTAG 卡', async function (ctx) {
                var rsp = await ctx.request(CH_CARD_BEGIN, new Uint8Array(begin), 10000);
                if (!rsp || rsp[0] !== 0) throw new Error('开始失败：' + statusText(rsp ? rsp[0] : 0xFF));
                for (var off = 0; off < textBytes.length; off += CHUNK) {
                    var part = textBytes.slice(off, Math.min(off + CHUNK, textBytes.length));
                    var frame = new Uint8Array(2 + part.length);
                    frame.set(be16(off), 0);
                    frame.set(part, 2);
                    rsp = await ctx.request(CH_CARD_DATA, frame, 10000);
                    if (!rsp || rsp[0] !== 0) throw new Error('数据失败：' + statusText(rsp ? rsp[0] : 0xFF));
                }
                rsp = await ctx.request(CH_CARD_COMMIT, new Uint8Array(), 10000);
                if (!rsp || rsp[0] !== 0) throw new Error('提交失败：' + statusText(rsp ? rsp[0] : 0xFF));
                return rsp[1];
            });
            if (cover) {
                setConnection('卡片已创建，正在上传封面…');
                await run('上传封面', async function (ctx) {
                    await uploadCover(ctx, cardId, cover, function (pct) {
                        setConnection('正在上传封面… ' + pct + '%');
                    });
                });
            }
            setConnection('创建成功 · 已选中新卡');
            await refresh();
        } catch (e) {
            setConnection('创建失败：' + e.message);
        }
    }

    function bindButtons() {
        var refreshBtn = $('wallet-refresh');
        if (refreshBtn) refreshBtn.addEventListener('click', refresh);
        var createBtn = $('wallet-create');
        if (createBtn) createBtn.addEventListener('click', createCard);
        var connectBtn = $('wallet-connect');
        if (connectBtn) connectBtn.addEventListener('click', function () {
            var nav = document.querySelector('.nav-item[data-page="bluetooth-page"]');
            if (nav) nav.click();
        });
        var list = $('wallet-list');
        if (list) list.addEventListener('click', function (e) {
            var b = e.target.closest('button');
            if (!b) return;
            if (b.dataset.walletSelect) selectCard(parseInt(b.dataset.walletSelect, 10));
            else if (b.dataset.walletRename) renameCard(parseInt(b.dataset.walletRename, 10));
            else if (b.dataset.walletDelete) deleteCard(parseInt(b.dataset.walletDelete, 10));
            else if (b.dataset.walletCover) uploadCoverForCard(parseInt(b.dataset.walletCover, 10));
        });
        var navWallet = document.querySelector('.nav-item[data-page="wallet-page"]');
        if (navWallet) navWallet.addEventListener('click', function () {
            if (connected()) refresh();
        });
        document.addEventListener('ark-device-state', function (e) {
            var on = e.detail && e.detail.connected;
            document.querySelectorAll('#wallet-page [data-ark-connected]').forEach(function (el) {
                el.disabled = !on;
            });
            if (on && document.getElementById('wallet-page').classList.contains('active')) refresh();
        });
    }

    async function uploadCoverForCard(id) {
        var input = document.createElement('input');
        input.type = 'file';
        input.accept = 'image/*';
        input.onchange = async function () {
            if (!input.files || !input.files[0]) return;
            try {
                var payload = await fileToCover(input.files[0]);
                await run('上传封面', async function (ctx) {
                    await uploadCover(ctx, id, payload, function (pct) {
                        setConnection('正在上传封面… ' + pct + '%');
                    });
                });
                setConnection('封面已上传');
                await refresh();
            } catch (e) { setConnection('封面上传失败：' + e.message); }
        };
        input.click();
    }

    document.addEventListener('DOMContentLoaded', bindButtons);
    window.NfcWallet = { refresh: refresh };
})();
