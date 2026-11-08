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

    /* 轻量结果提示：不受卡包列表状态文本影响 */
    var m_toastTimer = 0;

    function walletToast(text, isError) {
        var el = $('wallet-toast');
        if (!el) return;
        el.textContent = text;
        el.classList.toggle('is-error', !!isError);
        el.classList.add('is-show');
        if (m_toastTimer) clearTimeout(m_toastTimer);
        m_toastTimer = setTimeout(function () {
            el.classList.remove('is-show');
            m_toastTimer = 0;
        }, 3200);
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
            var cur = i === m_current;
            html += '<div class="wallet-row' + (cur ? ' is-current' : '') + '">' +
                '<div class="wallet-row__body">' +
                '<div class="wallet-row__name">' + esc(c.name || 'CARD') +
                (cur ? '<span class="wallet-row__badge">当前</span>' : '') + '</div>' +
                '<div class="wallet-row__meta">' + esc(kindName(c.kind)) + ' · ' + uidText(c.uid, c.uidLen) + '</div>' +
                '</div>' +
                '<div class="wallet-row-actions">' +
                '<button type="button" class="secondary-button' + (cur ? ' is-current' : '') + '" data-wallet-select="' + c.id + '">选中</button>' +
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
            walletToast('已切换当前卡');
            await refresh();
        } catch (e) { setConnection('选中失败：' + e.message); walletToast('选中失败：' + e.message, true); }
    }

    async function deleteCard(id) {
        if (!confirm('删除这张卡？设备上的封面也会删除。')) return;
        try {
            await run('删除卡', async function (ctx) {
                await walletCtrl(ctx, [0x02, id]);
            });
            walletToast('已删除卡片');
            await refresh();
        } catch (e) { setConnection('删除失败：' + e.message); walletToast('删除失败：' + e.message, true); }
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
            walletToast('卡片已改名');
            await refresh();
        } catch (e) { setConnection('重命名失败：' + e.message); walletToast('重命名失败：' + e.message, true); }
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
    var m_coverFitMode = 'contain';   /* contain = 完整显示不裁切，cover = 铺满裁切 */
    var m_coverPayload = null;
    var m_coverTargetId = 0;
    var m_coverDialogMode = 'create';   /* create = 新卡封面草稿，target = 改已有卡封面 */
    var m_coverSnapshot = null;
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

    /* 取图片四角的平均色当“边底色”：白底、米白底、浅灰底都能认出来。
       四角颜色不一致（比如照片直达边缘）或颜色偏暗时返回 null，不做裁边 */
    function coverBorderBg(d, cw, ch) {
        var s = Math.max(2, Math.round(Math.min(cw, ch) * 0.04));
        var corners = [[0, 0], [cw - s, 0], [0, ch - s], [cw - s, ch - s]];
        var lo = [255, 255, 255], hi = [0, 0, 0];
        var sum = [0, 0, 0];
        for (var i = 0; i < corners.length; i++) {
            var cx = corners[i][0], cy = corners[i][1];
            var avg = [0, 0, 0], n = 0;
            for (var y = cy; y < cy + s; y++) {
                for (var x = cx; x < cx + s; x++) {
                    var o = (y * cw + x) * 4;
                    if (d[o + 3] < 12) continue;
                    avg[0] += d[o]; avg[1] += d[o + 1]; avg[2] += d[o + 2];
                    n++;
                }
            }
            if (!n) return null;
            avg[0] /= n; avg[1] /= n; avg[2] /= n;
            for (var c = 0; c < 3; c++) {
                if (avg[c] < 200) return null;      /* 角上是深色，说明图直达边缘 */
                if (avg[c] < lo[c]) lo[c] = avg[c];
                if (avg[c] > hi[c]) hi[c] = avg[c];
                sum[c] += avg[c] / corners.length;
            }
        }
        for (var k = 0; k < 3; k++) {
            if (hi[k] - lo[k] > 24) return null;    /* 四角颜色差太多，不是统一底色 */
        }
        return sum;
    }

    /* 找出图片里真正的画面范围（忽略底色边），用于导入时自动取景。
       逐行逐列统计非背景像素，避免 JPEG 噪点或角落杂色把包围盒撑满整张图。 */
    function coverContentBox(img) {
        var iw = img.width, ih = img.height;
        if (!iw || !ih) return null;
        var max = 480;
        var k = Math.min(1, max / Math.max(iw, ih));
        var cw = Math.max(1, Math.round(iw * k));
        var ch = Math.max(1, Math.round(ih * k));
        var c = document.createElement('canvas');
        c.width = cw; c.height = ch;
        var x = c.getContext('2d');
        x.drawImage(img, 0, 0, cw, ch);
        var d;
        try { d = x.getImageData(0, 0, cw, ch).data; } catch (e) { return null; }
        var border = coverBorderBg(d, cw, ch);
        var rowHits = [], colHits = [];
        for (var i = 0; i < ch; i++) rowHits[i] = 0;
        for (var j = 0; j < cw; j++) colHits[j] = 0;
        for (var y = 0; y < ch; y++) {
            for (var px = 0; px < cw; px++) {
                var o = (y * cw + px) * 4;
                var bg = d[o + 3] < 12;
                if (!bg) {
                    if (border) {
                        bg = Math.abs(d[o] - border[0]) <= 12 &&
                            Math.abs(d[o + 1] - border[1]) <= 12 &&
                            Math.abs(d[o + 2] - border[2]) <= 12;
                    } else {
                        bg = d[o] >= 240 && d[o + 1] >= 240 && d[o + 2] >= 240;
                    }
                }
                if (bg) continue;
                rowHits[y]++;
                colHits[px]++;
            }
        }
        var rowMin = Math.max(2, Math.round(cw * 0.01));
        var colMin = Math.max(2, Math.round(ch * 0.01));
        var minX = -1, minY = -1, maxX = -1, maxY = -1;
        for (var y2 = 0; y2 < ch; y2++) {
            if (rowHits[y2] >= rowMin) { if (minY < 0) minY = y2; maxY = y2; }
        }
        for (var x2 = 0; x2 < cw; x2++) {
            if (colHits[x2] >= colMin) { if (minX < 0) minX = x2; maxX = x2; }
        }
        if (minX < 0 || minY < 0) return null;
        /* 留 1 像素余量，避免把贴边的锐利轮廓切掉 */
        minX = Math.max(0, minX - 1); minY = Math.max(0, minY - 1);
        maxX = Math.min(cw - 1, maxX + 1); maxY = Math.min(ch - 1, maxY + 1);
        return { x: minX / k, y: minY / k, w: (maxX - minX + 1) / k, h: (maxY - minY + 1) / k };
    }

    /* 导入时把画面（去掉白边后）铺满画布；没有明显白边就保持完整显示 */
    function coverAutoFrame() {
        var img = m_coverImage;
        if (!img) return false;
        var box = coverContentBox(img);
        if (!box || box.w < 8 || box.h < 8) return false;
        if (box.w >= img.width * 0.94 && box.h >= img.height * 0.94) return false;
        var coverFit = Math.max(COVER_W / img.width, COVER_H / img.height);
        if (!coverFit) return false;
        /* 取较大的放大倍数：保证画布不留白（多出来的部分裁掉），再把画面中心对到画布中心。
           多放 3% 是为了把画面边缘的抗锯齿过渡推出画布，否则缩放到 1-bit 后边上会留一条白缝 */
        var fitBox = Math.max(COVER_W / box.w, COVER_H / box.h) * 1.03;
        var scale = coverClampScale(fitBox / coverFit);
        var eff = coverFit * scale;
        /* 以“铺满”为基准缩放，缩放滑杆的 100% 即铺满画布，之后还能继续放大 */
        m_coverFitMode = 'cover';
        m_coverScale = scale;
        m_coverOffX = -((box.x + box.w / 2) - img.width / 2) * eff;
        m_coverOffY = -((box.y + box.h / 2) - img.height / 2) * eff;
        return true;
    }

    function coverReset() {
        m_coverRot = 0;
        m_coverScale = 1;
        m_coverOffX = 0;
        m_coverOffY = 0;
        m_coverFitMode = 'contain';
    }

    function coverSyncMode() {
        var btn = $('wallet-cover-mode');
        if (!btn) return;
        btn.textContent = m_coverFitMode === 'cover' ? '铺满裁切' : '完整显示';
        btn.title = m_coverFitMode === 'cover'
            ? '当前铺满画幅：超出部分会被裁掉，点击改为完整显示'
            : '当前完整显示：整张图都能看到，点击改为铺满裁切';
    }

    /* 缩放滑杆跟着实际比例走（滚轮/双指缩放也会回写） */
    function coverSyncZoom() {
        var pct = Math.round(m_coverScale * 100);
        var el = $('wallet-cover-zoom');
        var val = $('wallet-cover-zoom-val');
        if (el) el.value = pct;
        if (val) val.textContent = pct + '%';
    }

    function coverSyncSummary() {
        var has = !!m_coverPayload;
        var thumb = $('wallet-cover-thumb');
        var editBtn = $('wallet-cover-edit');
        var removeBtn = $('wallet-cover-remove');
        var hint = $('wallet-cover-summary-hint');
        if (thumb) thumb.hidden = !has;
        if (editBtn) editBtn.textContent = has ? '编辑封面' : '添加封面';
        if (removeBtn) removeBtn.hidden = !has;
        if (hint) {
            hint.textContent = has
                ? '封面已就绪，创建卡片时一起上传。'
                : '440×200 1-bit · 封面可选，不选也能创建。';
        }
        if (!has) return;
        var thumbCanvas = $('wallet-cover-thumb-canvas');
        var canvas = $('wallet-cover-canvas');
        if (thumbCanvas && canvas) {
            thumbCanvas.getContext('2d').drawImage(canvas, 0, 0, thumbCanvas.width, thumbCanvas.height);
        }
    }

    function coverSyncControls() {
        var has = !!m_coverImage;
        ['wallet-cover-rotate', 'wallet-cover-mode', 'wallet-cover-fit', 'wallet-cover-clear',
         'wallet-cover-zoom', 'wallet-cover-threshold', 'wallet-cover-dither'].forEach(function (id) {
            var el = $(id);
            if (el) el.disabled = !has;
        });
        var confirm = $('wallet-cover-upload');
        if (confirm) {
            confirm.disabled = !has || (m_coverDialogMode === 'target' && !connected());
            confirm.textContent = m_coverDialogMode === 'target' ? '上传封面' : '完成';
        }
        var hint = $('wallet-cover-hint');
        if (hint) {
            hint.textContent = has
                ? '导入时会自动裁掉白边并铺满画幅；可拖动 / 缩放微调，或切到「完整显示」看整张图'
                : '导入图片后可预览 1-bit 效果；有白边会自动裁掉并铺满画布';
        }
        coverSyncMode();
        coverSyncZoom();
    }

    /* 打开弹窗前记下新建卡的封面草稿，取消/上传后回滚，互不影响 */
    function coverSnapshotCapture() {
        return {
            image: m_coverImage,
            rot: m_coverRot,
            scale: m_coverScale,
            offX: m_coverOffX,
            offY: m_coverOffY,
            threshold: m_coverThreshold,
            dither: m_coverDither,
            fitMode: m_coverFitMode,
            payload: m_coverPayload,
            targetId: m_coverTargetId
        };
    }

    function coverSnapshotRestore(s) {
        if (!s) return;
        m_coverImage = s.image;
        m_coverRot = s.rot;
        m_coverScale = s.scale;
        m_coverOffX = s.offX;
        m_coverOffY = s.offY;
        m_coverThreshold = s.threshold;
        m_coverDither = s.dither;
        m_coverFitMode = s.fitMode;
        m_coverPayload = s.payload;
        m_coverTargetId = s.targetId;
        var th = $('wallet-cover-threshold');
        if (th) th.value = m_coverThreshold;
        var thv = $('wallet-cover-threshold-val');
        if (thv) thv.textContent = m_coverThreshold;
        var dither = $('wallet-cover-dither');
        if (dither) dither.checked = m_coverDither;
        coverRender();
        coverSyncControls();
    }

    /* 把源图按“铺满/完整显示 + 用户缩放/位移/旋转”画到 440×200，再按阈值（可选
       Bayer 抖动）量化成 1bpp。画布直接显示量化结果，保证预览与上传内容一致。 */
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
            coverSyncSummary();
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
        var fit = m_coverFitMode === 'cover'
            ? Math.max(COVER_W / rw, COVER_H / rh)
            : Math.min(COVER_W / rw, COVER_H / rh);
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
        coverSyncSummary();
        coverSyncZoom();
    }

    function coverLoad(file) {
        if (!file) return;
        var reader = new FileReader();
        reader.onload = function (ev) {
            var img = new Image();
            img.onload = function () {
                m_coverImage = img;
                coverReset();
                var framed = coverAutoFrame();
                coverRender();
                coverSyncControls();
                walletToast(framed
                    ? '封面已导入：已自动裁掉白边并铺满画幅，可拖动 / 缩放微调'
                    : '封面已导入，可拖动 / 缩放调整构图');
            };
            img.onerror = function () { walletToast('封面图片读取失败', true); };
            img.src = ev.target.result;
        };
        reader.onerror = function () { walletToast('封面图片读取失败', true); };
        reader.readAsDataURL(file);
    }

    function coverClear() {
        m_coverImage = null;
        m_coverPayload = null;
        coverReset();
        coverRender();
        coverSyncControls();
    }

    function coverCardName(id) {
        for (var i = 0; i < m_cards.length; i++) {
            if (m_cards[i].id === id) return m_cards[i].name || 'CARD';
        }
        return 'CARD';
    }

    /* 新卡封面与已有卡换封面共用同一个弹窗；mode 决定确认按钮语义 */
    function coverOpenDialog(mode, id) {
        var dlg = $('wallet-cover-dialog');
        if (!dlg) return;
        m_coverSnapshot = coverSnapshotCapture();
        m_coverDialogMode = mode;
        m_coverTargetId = 0;
        var title = $('wallet-cover-dialog-title');
        var sub = $('wallet-cover-dialog-sub');
        if (mode === 'target') {
            m_coverTargetId = id;
            /* 设备上的旧封面读不回来，每次从空白开始 */
            m_coverImage = null;
            m_coverPayload = null;
            coverReset();
            if (title) title.textContent = '「' + coverCardName(id) + '」的封面';
            if (sub) sub.textContent = '导入图片并上传后，会覆盖设备上这张卡的封面';
        } else {
            if (title) title.textContent = '新卡封面';
            if (sub) sub.textContent = '440×200 · 1-bit · 创建卡片时一起上传';
        }
        if (!dlg.open) dlg.showModal();
        coverRender();
        coverSyncControls();
    }

    function coverCloseDialog() {
        var dlg = $('wallet-cover-dialog');
        if (dlg && dlg.open) dlg.close();
    }

    async function coverUploadTarget() {
        if (!m_coverTargetId) return;
        if (!m_coverPayload) { walletToast('请先导入封面图片', true); return; }
        var id = m_coverTargetId;
        var name = coverCardName(id);
        var payload = m_coverPayload;
        try {
            await run('上传封面', async function (ctx) {
                await uploadCover(ctx, id, payload, function (pct) {
                    setConnection('正在上传封面… ' + pct + '%');
                });
            });
            /* 成功后恢复新建卡封面草稿，避免把这张卡的封面带到新卡 */
            var snap = m_coverSnapshot;
            m_coverSnapshot = null;
            m_coverTargetId = 0;
            coverCloseDialog();
            coverSnapshotRestore(snap);
            walletToast('封面已上传到「' + name + '」');
            await refresh();
        } catch (e) {
            walletToast('封面上传失败：' + e.message, true);
            setConnection('封面上传失败：' + e.message);
        }
    }

    function coverConfirm() {
        if (m_coverDialogMode === 'target') { coverUploadTarget(); return; }
        if (!m_coverPayload) { walletToast('请先导入封面图片', true); return; }
        m_coverSnapshot = null;
        coverCloseDialog();
        coverSyncSummary();
        walletToast('封面已就绪，创建卡片时一起上传');
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
        var modeBtn = $('wallet-cover-mode');
        if (modeBtn) modeBtn.addEventListener('click', function () {
            if (!m_coverImage) return;
            /* 切换适配方式时回到该方式的 100%，否则两种基准混在一起会放大过头 */
            m_coverFitMode = m_coverFitMode === 'cover' ? 'contain' : 'cover';
            m_coverScale = 1;
            m_coverOffX = 0;
            m_coverOffY = 0;
            coverRender();
            coverSyncMode();
        });
        var fitBtn = $('wallet-cover-fit');
        if (fitBtn) fitBtn.addEventListener('click', function () {
            if (!m_coverImage) return;
            coverReset();
            coverRender();
            coverSyncMode();
        });
        var clearBtn = $('wallet-cover-clear');
        if (clearBtn) clearBtn.addEventListener('click', coverClear);
        var zoom = $('wallet-cover-zoom');
        if (zoom) zoom.addEventListener('input', function () {
            m_coverScale = coverClampScale((+this.value) / 100);
            coverRender();
            coverSyncZoom();
        });
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
        var confirmBtn = $('wallet-cover-upload');
        if (confirmBtn) confirmBtn.addEventListener('click', coverConfirm);
        var cancelBtn = $('wallet-cover-cancel');
        if (cancelBtn) cancelBtn.addEventListener('click', coverCloseDialog);
        var closeBtn = $('wallet-cover-close');
        if (closeBtn) closeBtn.addEventListener('click', coverCloseDialog);
        var editBtn = $('wallet-cover-edit');
        if (editBtn) editBtn.addEventListener('click', function () { coverOpenDialog('create', 0); });
        var removeBtn = $('wallet-cover-remove');
        if (removeBtn) removeBtn.addEventListener('click', function () { coverClear(); });

        /* 关闭弹窗（取消 / Esc）时回滚到打开前的草稿 */
        var dialog = $('wallet-cover-dialog');
        if (dialog) dialog.addEventListener('close', function () {
            m_coverTargetId = 0;
            var snap = m_coverSnapshot;
            m_coverSnapshot = null;
            coverSnapshotRestore(snap);
            coverSyncControls();
            coverSyncSummary();
        });

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
            coverClear();
            walletToast('创建成功 · 已选中新卡');
            await refresh();
        } catch (e) {
            setConnection('创建失败：' + e.message);
            walletToast('创建失败：' + e.message, true);
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
        coverOpenDialog('target', id);
    }

    document.addEventListener('DOMContentLoaded', bindButtons);
    window.NfcWallet = { refresh: refresh };
})();
