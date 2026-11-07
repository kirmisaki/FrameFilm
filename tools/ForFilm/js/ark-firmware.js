/* Ark firmware identity and GitHub release rules shared by Web OTA and serial flash. */
(function (root) {
    'use strict';

    const repo = 'kirmisaki/FrameFilm';
    const api = 'https://api.github.com/repos/' + repo + '/releases';
    const project = 'frame_film_ark';
    const mib = 1024 * 1024;
    const targets = ['n4r2', 'n16r8'].map((variant, i) => Object.freeze({
        targetId: 'ark-esp32s3-' + variant,
        label: 'Ark ' + variant.toUpperCase(),
        chipId: 9,
        flashBytes: (i ? 16 : 4) * mib,
        psramBytes: (i ? 8 : 2) * mib,
        psramBus: i ? 'octal' : 'quad',
        otaName: 'frame_film_ark-' + variant + '.bin',
        flashName: 'frame_film_ark-' + variant + '-flash.bin'
    }));
    const shaPattern = /^[0-9a-f]{64}$/i;
    const digestPattern = /^sha256:[0-9a-f]{64}$/i;
    const bus = ['', 'quad', 'octal'];
    const target = id => targets.find(item => item.targetId === id);
    const matches = (data, expected) => expected &&
        ['targetId', 'chipId', 'flashBytes', 'psramBytes', 'psramBus']
            .every(key => data[key] === expected[key]);
    const view = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const hex = bytes => Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
    const sha256 = async bytes => hex(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)));

    function text(bytes, start, end, padded = false) {
        const data = bytes.subarray(start, end);
        const nul = data.indexOf(0);
        if (nul < 1 || data.subarray(0, nul).some(byte => byte < 32 || byte > 126) ||
            (padded && data.subarray(nul).some(byte => byte !== 0))) {
            throw new Error('固件标识字符串格式无效');
        }
        return String.fromCharCode(...data.subarray(0, nul));
    }

    function parseHardware(bytes) {
        if (bytes.length < 16 || bytes.length > 46 || bytes[0] !== 1) {
            throw new Error('设备硬件信息格式无效');
        }
        const dv = view(bytes);
        const hardware = {
            chipId: dv.getUint16(1), flashBytes: dv.getUint32(5), psramBytes: dv.getUint32(9),
            psramBus: bus[bytes[13]], targetId: text(bytes, 14, bytes.length, true)
        };
        const expected = target(hardware.targetId);
        if (!matches(hardware, expected)) throw new Error('无法确认设备的 Flash / PSRAM 硬件规格');
        return expected;
    }

    function parseBuild(bytes) {
        const versionEnd = bytes.indexOf(0, 37);
        if (bytes.length < 39 || bytes[0] !== 1 || versionEnd < 38 ||
            bytes.indexOf(0, versionEnd + 1) !== bytes.length - 1) {
            throw new Error('设备 BUILD_INFO 格式无效');
        }
        const otaMax = view(bytes).getUint32(1);
        const elfSha256 = hex(bytes.subarray(5, 37));
        if (!otaMax || /^0+$/.test(elfSha256) ||
            text(bytes, versionEnd + 1, bytes.length) !== project) {
            throw new Error('无法确认设备的 OTA 容量或构建指纹');
        }
        return { otaMax, elfSha256 };
    }

    async function inspectApp(bytes) {
        const dv = view(bytes);
        if (bytes.length < 352 || bytes[0] !== 0xe9 || bytes[1] < 1 || bytes[1] > 16 ||
            dv.getUint16(12, true) !== 9 || dv.getUint32(28, true) < 320 ||
            dv.getUint32(32, true) !== 0xabcd5432 || text(bytes, 80, 112) !== project) {
            throw new Error('请选择带硬件标识的 Ark ESP32-S3 OTA 应用镜像');
        }
        const desc = bytes.subarray(288, 352);
        const hw = view(desc);
        if (String.fromCharCode(...desc.subarray(0, 8)) !== 'FFARKHW1' ||
            desc[8] !== 1 || desc[9] !== 64 ||
            desc.subarray(21, 24).some(Boolean) || desc.subarray(56).some(Boolean)) {
            throw new Error('固件缺少有效的 FFARKHW1 硬件描述');
        }
        const hardware = {
            chipId: hw.getUint16(10, true), flashBytes: hw.getUint32(12, true),
            psramBytes: hw.getUint32(16, true), psramBus: bus[desc[20]],
            targetId: text(desc, 24, 56, true)
        };
        if (!matches(hardware, target(hardware.targetId))) throw new Error('固件的硬件描述不匹配');
        let offset = 24, checksum = 0xef;
        for (let i = 0; i < bytes[1]; i++) {
            if (offset + 8 > bytes.length) throw new Error('固件数据段头已截断');
            const length = dv.getUint32(offset + 4, true);
            offset += 8;
            if (offset + length > bytes.length) throw new Error('固件数据段已截断');
            const end = offset + length;
            for (; offset < end; offset++) checksum ^= bytes[offset];
        }
        const checksumAt = offset | 15;
        const imageEnd = checksumAt + 1;
        const hashAppended = bytes[23];
        if (hashAppended > 1 || bytes.length !== imageEnd + 32 * hashAppended ||
            bytes[checksumAt] !== checksum) {
            throw new Error('固件长度或数据段校验失败');
        }
        if (hashAppended && await sha256(bytes.subarray(0, imageEnd)) !== hex(bytes.subarray(imageEnd))) {
            throw new Error('固件内部 SHA-256 校验失败');
        }
        const elfSha256 = hex(bytes.subarray(176, 208));
        if (/^0+$/.test(elfSha256)) throw new Error('固件缺少有效的 ELF 指纹');
        return { targetId: hardware.targetId, elfSha256, displayVersion: text(bytes, 48, 80) };
    }

    function latestFormal(releases) {
        if (!Array.isArray(releases)) throw new Error('GitHub 发布列表格式无效');
        return releases.filter(item => item && item.draft !== true && item.prerelease !== true &&
            typeof item.tag_name === 'string' && item.tag_name &&
            !item.tag_name.startsWith('frame-film-') &&
            Number.isFinite(Date.parse(item.published_at)))
            .sort((a, b) => Date.parse(b.published_at) - Date.parse(a.published_at))[0] || null;
    }

    async function latest() {
        const response = await fetch(api + '?per_page=100', { signal: AbortSignal.timeout(20000) });
        if (!response.ok) throw new Error('GitHub 发布查询失败（HTTP ' + response.status + '）');
        const release = latestFormal(await response.json());
        if (!release) throw new Error('暂无正式固件发布');
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
        return { ...found[0], url, tag: release.tag_name };
    }

    class DownloadUnavailable extends Error {}

    async function readAsset(item) {
        try {
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
        const item = asset(release, 'framefilm-ark-firmware.json');
        // The Releases API exposes body cross-origin. The asset remains the App's source.
        const block = /<!-- framefilm-ark-firmware:v1\r?\n([\s\S]*?)\r?\n-->/.exec(release.body || '');
        if (block) return JSON.parse(block[1]);
        let bytes;
        try {
            bytes = await readAsset(item);
        } catch (error) {
            if (!(error instanceof DownloadUnavailable)) throw error;
            throw new Error('该发行版未提供网页可读取的固件清单，可导入对应硬件的本地 OTA 文件');
        }
        if (digestPattern.test(item.digest || '')) {
            await verifyBytes(bytes, { ...item, sha256: item.digest.slice(7).toLowerCase() });
        }
        return JSON.parse(new TextDecoder().decode(bytes));
    }

    function selectOta(release, manifest, expected) {
        if (manifest?.schemaVersion !== 1 || manifest.project !== project ||
            !Array.isArray(manifest.firmwares)) throw new Error('固件发布清单格式无效');
        const byTarget = new Map();
        for (const entry of manifest.firmwares) {
            const hardware = target(entry?.targetId);
            if (!matches(entry, hardware) || byTarget.has(entry.targetId) ||
                entry.asset !== hardware.otaName || !Number.isSafeInteger(entry.size) || entry.size <= 0 ||
                !shaPattern.test(entry.sha256 || '') || !shaPattern.test(entry.elfSha256 || '') ||
                typeof entry.displayVersion !== 'string') throw new Error('固件清单的目标或指纹无效');
            const item = asset(release, entry.asset);
            const sha = entry.sha256.toLowerCase();
            if (item.size !== entry.size || (item.digest && item.digest.toLowerCase() !== 'sha256:' + sha)) {
                throw new Error('固件清单与 GitHub 附件不一致');
            }
            byTarget.set(entry.targetId, { ...item, targetId: entry.targetId, sha256: sha,
                elfSha256: entry.elfSha256.toLowerCase() });
        }
        const selected = byTarget.get(expected.targetId);
        if (!selected) throw new Error('本次发布没有匹配 ' + expected.label + ' 的 OTA 固件');
        return selected;
    }

    function selectFlash(release, expected) {
        const item = asset(release, expected.flashName);
        if (!digestPattern.test(item.digest || '') || item.size > expected.flashBytes) {
            throw new Error('完整镜像缺少有效的 GitHub SHA-256 或大小不匹配');
        }
        return { ...item, targetId: expected.targetId, sha256: item.digest.slice(7).toLowerCase() };
    }

    async function verifyFlash(bytes, item) {
        await verifyBytes(bytes, item);
        if (bytes.length < 0x30000 + 352 || bytes[0] !== 0xe9 ||
            view(bytes).getUint16(12, true) !== 9 || bytes[0x8000] !== 0xaa || bytes[0x8001] !== 0x50) {
            throw new Error('文件不是 Ark 完整串口镜像');
        }
        const image = await inspectApp(bytes.subarray(0x30000));
        if (image.targetId !== item.targetId) throw new Error('完整镜像内的应用硬件目标不匹配');
    }

    root.ArkFirmware = { targets, target, parseHardware, parseBuild, inspectApp, latestFormal, latest,
        readManifest, selectOta, selectFlash, download: readAsset, verifyBytes, verifyFlash, DownloadUnavailable };
})(window);
