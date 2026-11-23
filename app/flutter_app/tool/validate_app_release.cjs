// CI-only release-history check. Local unit tests never query GitHub.
const fs = require('node:fs');
const crypto = require('node:crypto');
const { APK_NAME, METADATA_NAME, channelFor, validateHistory } = require('./generate_app_release.cjs');
function assetUrl(raw, repository, tag, name) {
  const url = new URL(raw);
  const expected = new URL(`https://github.com/${repository}/releases/download/${encodeURIComponent(tag)}/${name}`);
  if (url.protocol !== 'https:' || url.hostname !== 'github.com' || url.port || url.username || url.password ||
      url.search || url.hash || url.pathname.toLowerCase().split('/').slice(1, 3).join('/') !== repository.toLowerCase() ||
      url.pathname.split('/').slice(3).join('/') !== expected.pathname.split('/').slice(3).join('/')) {
    throw Error('Release asset URL does not belong to the exact repository/tag/name');
  }
  return url;
}
function allowedCdn(url) {
  return url.protocol === 'https:' && !url.port && !url.username && !url.password && !url.hash &&
    ((url.hostname === 'release-assets.githubusercontent.com' && url.pathname.startsWith('/github-production-release-asset/')) ||
     (['objects.githubusercontent.com', 'github-releases.githubusercontent.com'].includes(url.hostname) &&
      url.pathname.startsWith('/github-production-release-asset-2e65be/')));
}
async function metadataAsset(raw, repository, tag) {
  const origin = assetUrl(raw, repository, tag, METADATA_NAME);
  let url = origin;
  for (let count = 0; count <= 8; count++) {
    const response = await fetch(url, { redirect: 'manual', signal: AbortSignal.timeout(20000) });
    if ([301, 302, 303, 307, 308].includes(response.status)) {
      await response.body?.cancel();
      const location = response.headers.get('location');
      if (!location || count === 8) throw Error('Invalid release redirect');
      url = new URL(location, url);
      if (url.href !== origin.href && !allowedCdn(url)) throw Error('Unexpected release CDN');
      continue;
    }
    if (!response.ok) throw Error(`GitHub metadata lookup failed: ${response.status}`);
    return response.json();
  }
}
async function history(repository) {
  const previous = [];
  for (let page = 1; ; page++) {
    const response = await fetch(`https://api.github.com/repos/${repository}/releases?per_page=100&page=${page}`, {
      redirect: 'error', headers: { Accept: 'application/vnd.github+json' }, signal: AbortSignal.timeout(20000),
    });
    if (!response.ok) throw Error(`GitHub history lookup failed: ${response.status}`);
    const releases = await response.json();
    if (!Array.isArray(releases)) throw Error('Invalid GitHub release list');
    for (const release of releases) {
      if (release.draft || release.prerelease) continue;
      const assets = release.assets.filter((a) => a.name === METADATA_NAME);
      if (assets.length > 1) throw Error('Duplicate published app metadata');
      if (!assets.length) continue;
      previous.push(await metadataAsset(assets[0].browser_download_url, repository, release.tag_name));
    }
    if (releases.length < 100) return previous;
  }
}
async function validate(apk, file, online = false) {
  const channel = channelFor(process.env.GITHUB_REPOSITORY ?? process.env.FRAMEFILM_APP_RELEASE_REPO ?? 'Rafael-Ikaros/FrameFilm');
  const metadata = JSON.parse(fs.readFileSync(file, 'utf8'));
  const bytes = fs.readFileSync(apk);
  if (metadata.packageName !== channel.packageName || metadata.repository !== channel.repository ||
      metadata.apkName !== APK_NAME || metadata.minSdk !== 28 ||
      !Number.isSafeInteger(metadata.versionCode) || metadata.versionCode <= 0 ||
      metadata.size !== bytes.length || metadata.sha256 !== crypto.createHash('sha256').update(bytes).digest('hex') ||
      metadata.certificateSha256 !== channel.certificate ||
      (process.env.TAG && metadata.tag !== process.env.TAG) ||
      (process.env.VERSION_CODE && String(metadata.versionCode) !== process.env.VERSION_CODE) ||
      (process.env.VERSION_NAME && metadata.versionName !== process.env.VERSION_NAME)) {
    throw Error('App artifact identity, version, SDK, signing baseline or SHA/size does not match release metadata');
  }
  if (online) validateHistory(metadata, await history(channel.repository));
  console.log(`Validated own-channel app artifact: ${channel.repository} ${metadata.versionCode}`);
}
if (require.main === module) {
  const [apk, metadata, online] = process.argv.slice(2);
  if (!apk || !metadata || (online && online !== '--history')) throw Error('Usage: node validate_app_release.cjs <apk> <metadata> [--history]');
  validate(apk, metadata, online === '--history').catch((error) => { console.error(error.message); process.exitCode = 1; });
}
module.exports = { assetUrl, allowedCdn, validate };
