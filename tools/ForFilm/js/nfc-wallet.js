/* ForFilm · NFC 卡包（仅 Ark n16r8）
 * 协议：0x63~0x65 建卡，0x66 管理，0x67~0x69 封面。
 */
(function () {
    'use strict';

    var CH_CARD_BEGIN = 0x63;
    var CH_CARD_DATA = 0x64;
    var CH_CARD_COMMIT = 0x65;
    var CH_WALLET_CTRL = 0x66;
    var CH_COVER_BEGIN = 0x67;
    var CH_COVER_DATA = 0x68;
    var CH_COVER_COMMIT = 0x69;
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
            if (caps && caps.length === 5 && caps[0] === 1 && (caps[4] & 0x80) !== 0) return fn(ctx);
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

    /* ---------- 封面编辑器：导入图片 → 1-bit 预览 / 调整 ---------- */
    var COVER_BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]];
    var COVER_SCALE_MIN = 0.2;
    var COVER_SCALE_MAX = 8;

    var m_coverImage = null;
    var m_coverRot = 0;
    var m_coverScale = 1;
    var m_coverOffX = 0;
    var m_coverOffY = 0;
    var m_coverThreshold = 128;
    var m_coverDither = false;
    var m_coverPayload = null;
    var m_coverTargetId = 0;
    var m_coverPointers = {};
    var m_coverDrag = null;
    var m_coverPinch = null;
    var m_coverOut = null;
    var m_coverWork = document.createElement('canvas');
    m_coverWork.width = COVER_W;
    m_coverWork.height = COVER_H;

    function coverClampScale(s) {
        return Math.max(COVER_SCALE_MIN, Math.min(COVER_SCALE_MAX, s));
    }

    function coverReset() {
        m_coverRot = 0;
        m_coverScale = 1;
        m_coverOffX = 0;
        m_coverOffY = 0;
    }

    function coverSyncControls() {
        var has = !!m_coverImage;
        ['wallet-cover-rotate', 'wallet-cover-fit', 'wallet-cover-clear',
         'wallet-cover-threshold', 'wallet-cover-dither'].forEach(function (id) {
            var el = $(id);
            if (el) el.disabled = !has;
        });
        var upload = $('wallet-cover-upload');
        if (upload) upload.disabled = !has || !connected();
        var hint = $('wallet-cover-hint');
        if (hint) {
            hint.textContent = has
                ? '拖动移动 · 滚轮 / 双指缩放 · 旋转调整构图；预览即上传的 1-bit 结果'
                : '导入图片后可预览 1-bit 效果；拖动/缩放只影响封面构图';
        }
    }

    /* 把源图按“适应画幅 + 用户缩放/位移/旋转”画到 440×200，再按阈值（可选 Bayer
       抖动）量化成 1bpp。画布直接显示量化结果，保证预览与上传内容完全一致。 */
    function coverRender() {
        var canvas = $('wallet-cover-canvas');
        if (!canvas) return;
        var ctx = canvas.getContext('2d');
        var empty = $('wallet-cover-empty');
        if (!m_coverImage) {
            ctx.fillStyle = '#fff';
            ctx.fillRect(0, 0, COVER_W, COVER_H);
            m_coverPayload = null;
            if (empty) empty.hidden = false;
            return;
        }
        if (empty) empty.hidden = true;

        var work = m_coverWork.getContext('2d');
        work.setTransform(1, 0, 0, 1, 0, 0);
        work.fillStyle = '#fff';
        work.fillRect(0, 0, COVER_W, COVER_H);

        var rot = ((m_coverRot % 4) + 4) % 4;
        var rw = (rot % 2 === 0) ? m_coverImage.width : m_coverImage.height;
        var rh = (rot % 2 === 0) ? m_coverImage.height : m_coverImage.width;
        var fit = Math.min(COVER_W / rw, COVER_H / rh);
        var s = fit * coverClampScale(m_coverScale);
        work.save();
        work.translate(COVER_W / 2 + m_coverOffX, COVER_H / 2 + m_coverOffY);
        work.rotate(rot * Math.PI / 2);
        work.imageSmoothingEnabled = true;
        work.imageSmoothingQuality = 'high';
        work.drawImage(m_coverImage,
            -m_coverImage.width * s / 2, -m_coverImage.height * s / 2,
            m_coverImage.width * s, m_coverImage.height * s);
        work.restore();

        var src = work.getImageData(0, 0, COVER_W, COVER_H);
        if (!m_coverOut) m_coverOut = ctx.createImageData(COVER_W, COVER_H);
        var out = m_coverOut;
        var payload = new Uint8Array(COVER_PAYLOAD);
        var threshold = m_coverThreshold;
        var dither = m_coverDither;
        for (var y = 0; y < COVER_H; y++) {
            var row = COVER_BAYER[y & 3];
            for (var x = 0; x < COVER_W; x++) {
                var i = y * COVER_W + x;
                var o = i * 4;
                var lum = 0.299 * src.data[o] + 0.587 * src.data[o + 1] + 0.114 * src.data[o + 2];
                if (dither) lum += (row[x & 3] - 8) * 8;
                var black = lum < threshold;
                var v = black ? 0 : 255;
                out.data[o] = out.data[o + 1] = out.data[o + 2] = v;
                out.data[o + 3] = 255;
                if (black) payload[i >> 3] |= 0x80 >> (i & 7);
            }
        }
        ctx.putImageData(out, 0, 0);
        m_coverPayload = payload;
    }

    function coverLoad(file) {
        if (!file) return;
        var reader = new FileReader();
        reader.onload = function (ev) {
            var img = new Image();
            img.onload = function () {
                m_coverImage = img;
                coverReset();
                coverRender();
                coverSyncControls();
                setConnection('封面已导入，可拖动/缩放并调整阈值');
            };
            img.onerror = function () { setConnection('封面图片读取失败'); };
            img.src = ev.target.result;
        };
        reader.onerror = function () { setConnection('封面图片读取失败'); };
        reader.readAsDataURL(file);
    }

    function coverClear() {
        m_coverImage = null;
        m_coverPayload = null;
        coverReset();
        coverRender();
        coverSyncControls();
    }

    function coverSetTarget(id) {
        m_coverTargetId = id;
        var box = $('wallet-cover-target');
        var text = $('wallet-cover-target-text');
        if (id) {
            var name = 'CARD';
            for (var i = 0; i < m_cards.length; i++) {
                if (m_cards[i].id === id) name = m_cards[i].name || name;
            }
            if (text) text.textContent = '正在为「' + name + '」设置封面';
            if (box) box.hidden = false;
            var editor = $('wallet-cover-editor');
            if (editor) editor.scrollIntoView({ behavior: 'smooth', block: 'center' });
        } else if (box) {
            box.hidden = true;
        }
        coverSyncControls();
    }

    async function coverUploadTarget() {
        if (!m_coverTargetId) return;
        if (!m_coverPayload) { setConnection('请先导入封面图片'); return; }
        var id = m_coverTargetId;
        var payload = m_coverPayload;
        try {
            await run('上传封面', async function (ctx) {
                await uploadCover(ctx, id, payload, function (pct) {
                    setConnection('正在上传封面… ' + pct + '%');
                });
            });
            setConnection('封面已上传');
            coverSetTarget(0);
            await refresh();
        } catch (e) { setConnection('封面上传失败：' + e.message); }
    }

    function coverPoint(ev) {
        var canvas = $('wallet-cover-canvas');
        var rect = canvas.getBoundingClientRect();
        return {
            x: (ev.clientX - rect.left) * COVER_W / Math.max(1, rect.width),
            y: (ev.clientY - rect.top) * COVER_H / Math.max(1, rect.height)
        };
    }

    function coverDist(a, b) {
        var dx = a.x - b.x;
        var dy = a.y - b.y;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function bindCoverEditor() {
        var canvas = $('wallet-cover-canvas');
        var importBtn = $('wallet-cover-import');
        var fileInput = $('wallet-cover-file');
        if (importBtn && fileInput) importBtn.addEventListener('click', function () { fileInput.click(); });
        if (fileInput) fileInput.addEventListener('change', function () {
            coverLoad(this.files && this.files[0]);
            this.value = '';
        });

        var rotateBtn = $('wallet-cover-rotate');
        if (rotateBtn) rotateBtn.addEventListener('click', function () {
            if (!m_coverImage) return;
            m_coverRot = (m_coverRot + 1) % 4;
            coverRender();
        });
        var fitBtn = $('wallet-cover-fit');
        if (fitBtn) fitBtn.addEventListener('click', function () {
            if (!m_coverImage) return;
            coverReset();
            coverRender();
        });
        var clearBtn = $('wallet-cover-clear');
        if (clearBtn) clearBtn.addEventListener('click', coverClear);
        var threshold = $('wallet-cover-threshold');
        if (threshold) threshold.addEventListener('input', function () {
            m_coverThreshold = +this.value;
            var label = $('wallet-cover-threshold-val');
            if (label) label.textContent = this.value;
            coverRender();
        });
        var dither = $('wallet-cover-dither');
        if (dither) dither.addEventListener('change', function () {
            m_coverDither = this.checked;
            coverRender();
        });
        var uploadBtn = $('wallet-cover-upload');
        if (uploadBtn) uploadBtn.addEventListener('click', coverUploadTarget);
        var cancelBtn = $('wallet-cover-cancel');
        if (cancelBtn) cancelBtn.addEventListener('click', function () { coverSetTarget(0); });

        if (!canvas) return;
        canvas.addEventListener('pointerdown', function (e) {
            if (!m_coverImage) return;
            e.preventDefault();
            if (canvas.setPointerCapture) canvas.setPointerCapture(e.pointerId);
            m_coverPointers[e.pointerId] = coverPoint(e);
            var ids = Object.keys(m_coverPointers);
            if (ids.length === 1) {
                m_coverDrag = { id: e.pointerId, x: m_coverPointers[e.pointerId].x, y: m_coverPointers[e.pointerId].y };
                m_coverPinch = null;
            } else if (ids.length === 2) {
                m_coverDrag = null;
                m_coverPinch = {
                    dist: coverDist(m_coverPointers[ids[0]], m_coverPointers[ids[1]]),
                    scale: m_coverScale
                };
            }
        });
        canvas.addEventListener('pointermove', function (e) {
            if (!m_coverImage || !m_coverPointers[e.pointerId]) return;
            m_coverPointers[e.pointerId] = coverPoint(e);
            var ids = Object.keys(m_coverPointers);
            if (ids.length >= 2 && m_coverPinch) {
                var d = coverDist(m_coverPointers[ids[0]], m_coverPointers[ids[1]]);
                if (m_coverPinch.dist > 0) {
                    m_coverScale = coverClampScale(m_coverPinch.scale * d / m_coverPinch.dist);
                    coverRender();
                }
                e.preventDefault();
                return;
            }
            if (m_coverDrag && m_coverDrag.id === e.pointerId) {
                var p = m_coverPointers[e.pointerId];
                m_coverOffX += p.x - m_coverDrag.x;
                m_coverOffY += p.y - m_coverDrag.y;
                m_coverDrag.x = p.x;
                m_coverDrag.y = p.y;
                coverRender();
                e.preventDefault();
            }
        });
        function endPointer(e) {
            delete m_coverPointers[e.pointerId];
            if (m_coverDrag && m_coverDrag.id === e.pointerId) m_coverDrag = null;
            if (Object.keys(m_coverPointers).length < 2) m_coverPinch = null;
        }
        canvas.addEventListener('pointerup', endPointer);
        canvas.addEventListener('pointercancel', endPointer);
        canvas.addEventListener('wheel', function (e) {
            if (!m_coverImage) return;
            e.preventDefault();
            m_coverScale = coverClampScale(m_coverScale * (e.deltaY > 0 ? 0.9 : 1.1));
            coverRender();
        }, { passive: false });

        coverRender();
        coverSyncControls();
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

        var cover = m_coverPayload;

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
        bindCoverEditor();
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
            else if (b.dataset.walletCover) editCardCover(parseInt(b.dataset.walletCover, 10));
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
            coverSyncControls();
            if (on && document.getElementById('wallet-page').classList.contains('active')) refresh();
        });
    }

    function editCardCover(id) {
        coverSetTarget(id);
    }

    document.addEventListener('DOMContentLoaded', bindButtons);
    window.NfcWallet = { refresh: refresh };
})();
