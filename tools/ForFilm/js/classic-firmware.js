/* FrameFilm classic (Pro) firmware release rules shared by the serial flash page. */
(function (root) {
    'use strict';

    const repo = 'kirmisaki/FrameFilm';
    const api = 'https://api.github.com/repos/' + repo + '/releases';
    const project = 'frame_film';
    const model = 'pro';
    const mib = 1024 * 1024;
    const shaPattern = /^[0-9a-f]{64}$/i;
    const digestPattern = /^sha256:[0-9a-f]{64}$/i;
    const appOffset = 0x30000;
    const variants = Object.freeze({
        n4r2: Object.freeze({ variant: 'n4r2', targetId: 'frame-film-pro-n4r2',
            label: 'N4R2 · 4 MB Flash / 2 MB PSRAM', flashBytes: 4 * mib,
            psramBytes: 2 * mib, psramBus: 'quad', otaMax: 1536 * 1024 }),
        n16r8: Object.freeze({ variant: 'n16r8', targetId: 'frame-film-pro-n16r8',
            label: 'N16R8 · 16 MB Flash / 8 MB PSRAM', flashBytes: 16 * mib,
            psramBytes: 8 * mib, psramBus: 'octal', otaMax: 4096 * 1024 })
    });
    const screens = Object.freeze([
        Object.freeze({ screen: 'e6-3.68-792x528', panelId: 1, width: 792, height: 528, label: 'E6 3.68" 792×528' }),
        Object.freeze({ screen: 'e6-3.70-720x480', panelId: 2, width: 720, height: 480, label: 'E6 3.70" 720×480' }),
        Object.freeze({ screen: 'e6-3.60-600x400', panelId: 3, width: 600, height: 400, label: 'E6 3.60" 600×400' }),
        Object.freeze({ screen: 'e6-3.64-760x568', panelId: 6, width: 760, height: 568, label: 'E6 3.64" 760×568' }),
        Object.freeze({ screen: 'e6-3.98-768x552', panelId: 7, width: 768, height: 552, label: 'E6 3.98" 768×552' }),
        Object.freeze({ screen: 'e4-3.98-768x552', panelId: 8, width: 768, height: 552, label: 'E4 3.98" 768×552（四色）' })
    ]);

    const view = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const hex = bytes => Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
    const sha256 = async bytes => hex(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)));

    function text(bytes, start, end) {
        const data = bytes.subarray(start, end);
        const nul = data.indexOf(0);
        if (nul < 1 || data.subarray(0, nul).some(byte => byte < 32 || byte > 126)) {
            throw new Error('固件标识字符串格式无效');
        }
        return String.fromCharCode(...data.subarray(0, nul));
    }

    function screenOf(panelId) {
        return screens.find(item => item.panelId === panelId) || null;
    }

    function target(variantKey, screenKey) {
        const hardware = variants[variantKey];
        const panel = screens.find(item => item.screen === screenKey);
        if (!hardware || !panel) return null;
        const prefix = 'frame_film_pro_' + variantKey + '_' + screenKey;
        return Object.freeze({ ...hardware, ...panel,
            targetId: 'frame-film-pro-' + variantKey + '-' + screenKey,
            otaName: prefix + '-ota.bin', flashName: prefix + '-flash.bin',
            label: 'FrameFilm Pro · ' + hardware.label + ' · ' + panel.label });
    }

    function matches(entry, expected) {
        return expected && ['variant', 'flashBytes', 'psramBytes', 'psramBus',
            'screen', 'panelId', 'width', 'height'].every(key => entry[key] === expected[key]);
    }

    async function inspectApp(bytes) {
        const dv = view(bytes);
        if (bytes.length < 352 || bytes[0] !== 0xe9 || bytes[1] < 1 || bytes[1] > 16 ||
            dv.getUint16(12, true) !== 9 || dv.getUint32(32, true) !== 0xabcd5432 ||
            text(bytes, 80, 112) !== project) {
            throw new Error('请选择 FrameFilm 经典固件（frame_film）的 ESP32-S3 应用镜像');
        }
        const elfSha256 = hex(bytes.subarray(176, 208));
        if (/^0+$/.test(elfSha256)) throw new Error('固件缺少有效的 ELF 指纹');
        return { elfSha256, displayVersion: text(bytes, 48, 80) };
    }

    function latestFormal(releases) {
        if (!Array.isArray(releases)) throw new Error('GitHub 发布列表格式无效');
        return releases.filter(item => item && item.draft !== true && item.prerelease !== true &&
            typeof item.tag_name === 'string' && item.tag_name.startsWith('frame-film-') &&
            Number.isFinite(Date.parse(item.published_at)))
            .sort((a, b) => Date.parse(b.published_at) - Date.parse(a.published_at))[0] || null;
    }

    async function latest() {
        const mirrored = await root.FirmwareMirror?.release('classicFormal');
        if (mirrored) return mirrored;
        const response = await fetch(api + '?per_page=100', { signal: AbortSignal.timeout(20000) });
        if (!response.ok) throw new Error('GitHub 发布查询失败（HTTP ' + response.status + '）');
        const release = latestFormal(await response.json());
        if (!release) throw new Error('暂无 FrameFilm Pro 正式固件发布');
        return release;
    }

    function asset(release, name) {
        const found = Array.isArray(release.assets) ? release.assets.filter(item => item?.name === name) : [];
        if (found.length !== 1 || !Number.isSafeInteger(found[0].size) || found[0].size <= 0 ||
            !Number.isSafeInteger(found[0].id) || found[0].id <= 0) {
            throw new Error('正式发布 ' + release.tag_name + ' 缺少有效的 ' + name);
        }
        const url = 'https://github.com/' + repo + '/releases/download/' +
            encodeURIComponent(release.tag_name) + '/' + name;
        return { ...found[0], url, tag: release.tag_name, mirrored: release.mirrored === true };
    }

    class DownloadUnavailable extends Error {
        constructor(message) { super(message); this.name = 'DownloadUnavailable'; }
    }

    async function readAsset(item) {
        try {
            if (item.mirrored) return await root.FirmwareMirror.download(item);
            const response = await fetch(api + '/assets/' + item.id, {
                headers: { Accept: 'application/octet-stream' },
                signal: AbortSignal.timeout(45000)
            });
            if (!response.ok || (response.url.startsWith('https://api.github.com/') &&
                /json/i.test(response.headers.get('content-type') || ''))) throw new Error();
            return new Uint8Array(await response.arrayBuffer());
        } catch {
            throw new DownloadUnavailable('浏览器无法直接读取附件，请下载后导入');
        }
    }

    async function verifyBytes(bytes, item) {
        if (bytes.length !== item.size || await sha256(bytes) !== item.sha256) {
            throw new Error('下载或导入的文件与发布的大小 / SHA-256 不一致');
        }
    }

    async function readManifest(release) {
        const item = asset(release, 'framefilm-classic-firmware.json');
        const block = /<!-- framefilm-classic-firmware:v1\r?\n([\s\S]*?)\r?\n-->/.exec(release.body || '');
        if (block) return JSON.parse(block[1]);
        let bytes;
        try {
            bytes = await readAsset(item);
        } catch (error) {
            if (!(error instanceof DownloadUnavailable)) throw error;
            throw new Error('该发行版未提供网页可读取的固件清单，可导入对应的本地完整镜像');
        }
        if (digestPattern.test(item.digest || '')) {
            await verifyBytes(bytes, { ...item, sha256: item.digest.slice(7).toLowerCase() });
        }
        return JSON.parse(new TextDecoder().decode(bytes));
    }

    function selectFlash(release, manifest, expected) {
        if (manifest?.schemaVersion !== 1 || manifest.project !== project ||
            manifest.model !== model || !Array.isArray(manifest.firmwares)) {
            throw new Error('固件发布清单格式无效');
        }
        const byTarget = new Map();
        for (const entry of manifest.firmwares) {
            const hardware = target(entry?.variant, entry?.screen);
            if (!hardware || !matches(entry, hardware) || byTarget.has(hardware.targetId) ||
                entry.ota?.asset !== hardware.otaName || entry.flash?.asset !== hardware.flashName ||
                !Number.isSafeInteger(entry.flash.size) || entry.flash.size <= 0 ||
                !shaPattern.test(entry.flash.sha256 || '') ||
                !Number.isSafeInteger(entry.ota.size) || entry.ota.size <= 0 ||
                !shaPattern.test(entry.ota.sha256 || '') ||
                !shaPattern.test(entry.ota.elfSha256 || '') ||
                typeof entry.ota.version !== 'string') throw new Error('固件清单的目标或指纹无效');
            const flash = asset(release, entry.flash.asset);
            const ota = asset(release, entry.ota.asset);
            const flashSha = entry.flash.sha256.toLowerCase();
            const otaSha = entry.ota.sha256.toLowerCase();
            if (flash.size !== entry.flash.size || ota.size !== entry.ota.size ||
                (flash.digest && flash.digest.toLowerCase() !== 'sha256:' + flashSha) ||
                (ota.digest && ota.digest.toLowerCase() !== 'sha256:' + otaSha)) {
                throw new Error('固件清单与 GitHub 附件不一致');
            }
            byTarget.set(hardware.targetId, { ...flash, family: 'classic',
                targetId: hardware.targetId, label: hardware.label,
                variant: hardware.variant, screen: hardware.screen,
                flashBytes: hardware.flashBytes, psramBytes: hardware.psramBytes,
                psramBus: hardware.psramBus, panelId: hardware.panelId,
                width: hardware.width, height: hardware.height,
                sha256: flashSha, elfSha256: entry.ota.elfSha256.toLowerCase(),
                otaSize: entry.ota.size, version: entry.ota.version, tag: release.tag_name });
        }
        const selected = byTarget.get(expected.targetId);
        if (!selected) throw new Error('本次发布没有匹配 ' + expected.label + ' 的完整镜像');
        return selected;
    }

    async function verifyFlash(bytes, item) {
        await verifyBytes(bytes, item);
        if (bytes.length < appOffset + 352 || bytes[0] !== 0xe9 ||
            view(bytes).getUint16(12, true) !== 9 || bytes[0x8000] !== 0xaa || bytes[0x8001] !== 0x50) {
            throw new Error('文件不是 FrameFilm 完整串口镜像');
        }
        if (item.otaSize && bytes.length < appOffset + item.otaSize) {
            throw new Error('完整镜像内的应用分区已截断');
        }
        const image = await inspectApp(bytes.subarray(appOffset));
        if (item.elfSha256 && image.elfSha256 !== item.elfSha256) {
            throw new Error('完整镜像内的应用构建指纹不匹配');
        }
    }

    root.ClassicFirmware = { repo, project, model, variants: Object.freeze(Object.values(variants)),
        screens, screenOf, target, inspectApp, latestFormal, latest, readManifest,
        selectFlash, download: readAsset, verifyBytes, verifyFlash, DownloadUnavailable };
})(window);
