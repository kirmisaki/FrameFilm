/* Firmware catalog and images deployed with this ForFilm page. */
(function (root) {
    'use strict';

    async function release(channel) {
        try {
            const response = await fetch('firmware/catalog.json', {
                cache: 'no-cache', signal: AbortSignal.timeout(10000)
            });
            if (!response.ok) return null;
            const item = (await response.json())?.[channel];
            return item && Array.isArray(item.assets) ? { ...item, mirrored: true } : null;
        } catch {
            return null; // Older deployments can still query GitHub Releases.
        }
    }

    async function download(item) {
        const path = 'firmware/' + encodeURIComponent(item.tag) + '/' + encodeURIComponent(item.name);
        const revision = item.sha256 || (item.digest || '').slice(7);
        const response = await fetch(path + '?sha256=' + encodeURIComponent(revision), {
            signal: AbortSignal.timeout(45000)
        });
        if (!response.ok) throw new Error('固件下载失败（HTTP ' + response.status + '）');
        return new Uint8Array(await response.arrayBuffer());
    }

    root.FirmwareMirror = { release, download };
})(window);
