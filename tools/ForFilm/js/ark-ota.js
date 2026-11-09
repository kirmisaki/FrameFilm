/* Ark OTA selection. The existing BLE transfer runs after these checks. */
(function (root) {
    'use strict';
    const fw = root.ArkFirmware;
    const el = id => document.getElementById(id);
    let epoch = -1;
    let release = null;
    let selected = null;
    let onlineImport = false;
    let busy = false;
    let pendingBoot = null;
    const connected = () => root.ArkDevice.connected();
    const note = message => { el('ark-ota-online-status').textContent = message; };
    const channel = () => (el('ark-ota-channel').value === 'ci' ? 'ci' : 'formal');

    function clearRelease() {
        release = null;
        const link = el('ark-ota-download-link');
        link.hidden = true;
        link.removeAttribute('href');
        el('ark-ota-import-online').hidden = true;
    }

    function clearSelection() {
        selected = null;
        root.selectedOtaData = null;
        el('ota-file-size').textContent = '未选择';
    }

    function refresh() {
        if (epoch !== connectionEpoch) {
            epoch = connectionEpoch;
            clearRelease();
            if (selected) clearSelection();
            el('ark-ota-legacy').hidden = true;
            el('ark-ota-legacy-target').value = '';
            note('连接 Ark 后可选择更新通道并开始升级。');
        }
        const waiting = busy || root.ArkDevice.busy();
        el('ark-ota-check').disabled = waiting || !connected();
        el('ark-ota-channel').disabled = waiting;
        el('ark-ota-legacy-target').disabled = waiting;
        el('ark-ota-import-online').disabled = waiting;
        el('ota-local-btn').disabled = waiting;
        el('ota-upgrade-btn').disabled = waiting;
    }

    async function optionalQuery(ctx, channel) {
        try {
            return await ctx.request(channel, new Uint8Array(), 2500);
        } catch (error) {
            if (error.message !== '设备未响应' || ctx.signal.aborted || !connected()) throw error;
            return null;
        }
    }

    async function readDevice(ctx) {
        const caps = await optionalQuery(ctx, 0x59);
        if (caps && (caps.length !== 5 || caps[0] !== 1)) throw new Error('无法读取设备的能力信息');
        const advertised = caps && (caps[4] & 0x40) !== 0;
        const identity = advertised
            ? await ctx.request(0x5d, new Uint8Array())
            : await optionalQuery(ctx, 0x5d);
        // Historical upstream NFC returns PARAM to an empty card-data request.
        const legacyNfcReply = caps && (caps[4] & 0x50) === 0x10 &&
            identity?.length === 1 && identity[0] === 2;
        const hardware = identity && !legacyNfcReply ? fw.parseHardware(identity) : null;
        const build = fw.parseBuild(await ctx.request(0x56, new Uint8Array()));
        el('ark-ota-legacy').hidden = !!hardware;
        const expected = hardware || fw.target(el('ark-ota-legacy-target').value);
        return { build, expected, legacy: !hardware };
    }

    function requireSession(session) {
        if (session !== connectionEpoch || !connected()) throw new Error('设备连接已变更，请重新检查');
    }

    function checkCandidate(item, state) {
        if (!state.expected || item.targetId !== state.expected.targetId) {
            throw new Error('固件目标与当前设备硬件不匹配');
        }
        if (item.size > state.build.otaMax) throw new Error('固件超出当前设备的 OTA 分区容量');
        if (item.elfSha256 === state.build.elfSha256) throw new Error('设备已运行同一构建');
    }

    async function findCandidate(state) {
        const current = await fw.latestFor(channel());
        return fw.selectOta(current, await fw.readManifest(current), state.expected);
    }

    async function inspectCandidate(bytes, item) {
        if (item) await fw.verifyBytes(bytes, item);
        const image = await fw.inspectApp(bytes);
        if (item && (image.targetId !== item.targetId || image.elfSha256 !== item.elfSha256)) {
            throw new Error('镜像硬件描述或 ELF 指纹与发布清单不一致');
        }
        return image;
    }

    async function stage(bytes, item) {
        const session = connectionEpoch;
        const image = await inspectCandidate(bytes, item);
        requireSession(session);
        root.selectedOtaData = bytes;
        selected = { bytes, item };
        el('ota-file-size').textContent = fw.target(image.targetId).label + ' · ' + bytes.length + ' 字节';
    }

    async function check() {
        if (busy || !connected()) return;
        const session = connectionEpoch;
        busy = true;
        clearRelease();
        refresh();
        note('正在读取硬件和构建指纹…');
        try {
            const state = await root.ArkDevice.run('读取 Ark 固件信息', readDevice);
            requireSession(session);
            const id = device.id;
            const verification = pendingBoot && pendingBoot.id === id;
            if (verification && pendingBoot.elfSha256 === state.build.elfSha256) {
                pendingBoot = null;
                note('升级成功，已核对重启后的构建指纹。');
                updateOtaTransferStatus('升级成功，构建指纹一致', 100);
                return;
            }
            const prefix = verification ? '重连后构建指纹尚未匹配。' : '';
            if (!state.expected) {
                note(prefix + '旧固件未提供硬件身份。请按实物选择规格后再次检查。');
                return;
            }
            note(prefix + state.expected.label + ' · 正在检查 ' +
                (channel() === 'ci' ? 'CI 最新构建' : 'GitHub 正式发布') + '…');
            const item = await findCandidate(state);
            requireSession(session);
            if (item.elfSha256 === state.build.elfSha256) {
                note(prefix + state.expected.label + ' · 已是 ' + item.tag + ' 的构建');
                return;
            }
            checkCandidate(item, state);
            clearSelection();
            note(prefix + state.expected.label + ' · ' + item.tag + ' · 点击「开始升级」。');
        } catch (error) {
            if (session === connectionEpoch) note('检查更新失败：' + error.message);
        } finally {
            busy = false;
            refresh();
        }
    }

    function choose(fromRelease) {
        if (busy || (fromRelease && !release)) return;
        onlineImport = fromRelease;
        el('ota-file-input').click();
    }

    async function importFile(file) {
        if (busy) return;
        const item = onlineImport ? release : null;
        const fromRelease = onlineImport;
        onlineImport = false;
        if (!connected()) { note('请先连接 Ark，再导入 OTA 文件。'); return; }
        if (fromRelease && !item) { note('请重新检查更新后再导入附件。'); return; }
        const session = connectionEpoch;
        let staged = false;
        busy = true;
        clearSelection();
        refresh();
        try {
            if (file.size > 16 * 1024 * 1024) throw new Error('OTA 文件过大');
            const bytes = new Uint8Array(await file.arrayBuffer());
            requireSession(session);
            await stage(bytes, item);
            staged = true;
            note((item ? item.tag + ' · ' : '') + 'OTA 已导入，开始升级前会重新核对设备。');
        } catch (error) {
            if (session === connectionEpoch) note('导入失败：' + error.message);
        } finally {
            busy = false;
            refresh();
        }
        /* 导入成功后直接开始升级，省掉再点一次「开始升级」；设备已断开则保留已导入状态。 */
        if (staged && session === connectionEpoch && connected()) {
            note((item ? item.tag + ' · ' : '') + '正在开始升级…');
            if (typeof root.startOtaUpgrade === 'function') root.startOtaUpgrade();
        }
    }

    async function prepare(ctx, bytes) {
        if (busy) throw new Error('请等待固件查询或导入完成');
        const session = connectionEpoch;
        const state = await readDevice(ctx);
        requireSession(session);
        if (!state.expected) throw new Error('请按实物选择旧设备的硬件规格');

        bytes = bytes ?? selected?.bytes;
        let item = selected?.bytes === bytes ? selected?.item : null;
        if (!bytes) {
            clearRelease();
            note('正在检查 ' + state.expected.label + ' 的在线固件…');
            item = await findCandidate(state);
            requireSession(session);
            checkCandidate(item, state);
            note(item.tag + ' · 正在下载 OTA…');
            updateOtaTransferStatus('正在下载固件…', 0);
            try {
                bytes = await fw.download(item);
            } catch (error) {
                if (!(error instanceof fw.DownloadUnavailable)) throw error;
                requireSession(session);
                release = item;
                const link = el('ark-ota-download-link');
                link.href = item.url;
                link.hidden = false;
                el('ark-ota-import-online').hidden = false;
                note(item.tag + ' · 在线下载暂不可用。可重试，或下载并导入该 OTA 附件。');
                throw error;
            }
            requireSession(session);
            note(item.tag + ' · 正在校验固件…');
            updateOtaTransferStatus('正在校验固件…', 0);
        }

        const image = await inspectCandidate(bytes, item);
        checkCandidate({ ...image, size: bytes.length }, state);
        requireSession(session);
        if (state.legacy && !confirm('旧固件无法报告硬件身份。请确认设备实物为 ' +
            state.expected.label + '，并开始 OTA 升级？')) throw new Error('已取消升级');
        if (item) note(item.tag + ' · 校验通过，正在升级…');
        return { bytes, image };
    }

    function expectBoot(image) {
        pendingBoot = { id: device.id, elfSha256: image.elfSha256 };
    }

    root.ArkOta = { check, choose, importFile, prepare, expectBoot };
    // Let the shared Ark control lock restore its saved state before refreshing OTA controls.
    root.addEventListener('ark-device-state', () => queueMicrotask(refresh));
    el('ark-ota-legacy-target').addEventListener('change', () => {
        clearRelease();
        if (selected?.item) clearSelection();
        note('已选择硬件规格，可以开始在线升级或导入固件。');
    });
    el('ark-ota-channel').addEventListener('change', () => {
        clearRelease();
        if (selected) clearSelection();
        note(channel() === 'ci'
            ? '已选择 CI 最新构建（开发版），点「开始升级」继续。'
            : '已选择正式发布，点「开始升级」继续。');
    });
    refresh();
})(window);
