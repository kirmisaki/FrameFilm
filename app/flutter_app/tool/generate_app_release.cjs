// Inspect the actual signed APK; never infer a channel from an arbitrary fork.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const APK_NAME = 'framefilm-ark-flutter.apk';
const METADATA_NAME = 'framefilm-ark-flutter.json';
const channels = {
  'kirmisaki/framefilm': {
    repository: 'kirmisaki/FrameFilm', packageName: 'org.framefilm.ark.flutter',
    // Public certificate documented in docs/ci-builds.md, 2026-10-31.
    certificate: '9a623e563ef3198ee0b0bd34e3b017af69a20d013bfae093906fdf8b3de79299',
  },
  'rafael-ikaros/framefilm': {
    repository: 'Rafael-Ikaros/FrameFilm', packageName: 'org.framefilm.ark.flutter.dev',
    // Verified against this channel's public 0.4.10+14 APK (ark-2026.11.24.3).
    certificate: 'e5c1b5305670858656fce38cea82614838992aa3326ef3166546af4f9da7245e',
  },
};
function channelFor(repository) {
  const channel = channels[String(repository).toLowerCase()];
  if (!channel) throw Error('Unsupported release repository');
  return channel;
}
function validateApkInfo(info, signature, channel, versionName, versionCode, certificate) {
  const identity = info.match(/^package: name='([^']+)' versionCode='(\d+)' versionName='([^']+)'/m);
  const minSdk = Number(info.match(/^sdkVersion:'(\d+)'/m)?.[1]);
  const targetSdk = Number(info.match(/^targetSdkVersion:'(\d+)'/m)?.[1]);
  if (!identity || identity[1] !== channel.packageName || Number(identity[2]) !== versionCode ||
      identity[3] !== versionName || minSdk !== 28 || targetSdk !== 35 || /application-debuggable/.test(info)) {
    throw Error(`APK must be non-debuggable ${channel.packageName} ${versionName}+${versionCode}, minSDK 28 / targetSDK 35`);
  }
  const signers = [...signature.matchAll(/^Signer #\d+ certificate SHA-256 digest:\s*([a-fA-F0-9]{64})\s*$/gm)]
    .map((match) => match[1].toLowerCase());
  if (signers.length !== 1 || signers[0] !== certificate) {
    throw Error('APK signing certificate does not match the public distribution baseline; do not change the key');
  }
  return { packageName: identity[1], versionName, versionCode, minSdk };
}
function validateHistory(current, previous) {
  for (const old of previous) {
    if (old.packageName !== current.packageName) continue; // Historical opposite-package fork assets.
    if (!Number.isSafeInteger(old.versionCode) || old.versionCode <= 0 ||
        !Number.isSafeInteger(old.size) || old.size <= 0 || !/^[a-fA-F0-9]{64}$/.test(old.sha256 || '')) {
      throw Error('Published same-channel metadata is invalid');
    }
    if (current.versionCode < old.versionCode ||
        (current.versionCode === old.versionCode &&
         (current.sha256 !== old.sha256.toLowerCase() || current.size !== old.size ||
          current.versionName !== old.versionName || current.minSdk !== old.minSdk))) {
      throw Error('Release must use a newer versionCode; an existing versionCode may only reuse identical published APK bytes');
    }
  }
}
function buildTool(sdk, name) {
  const directory = path.join(sdk, 'build-tools');
  const executable = process.platform === 'win32' ? `${name}.${name === 'apksigner' ? 'bat' : 'exe'}` : name;
  const found = fs.readdirSync(directory).sort((a, b) => b.localeCompare(a, 'en', { numeric: true }))
    .map((dir) => path.join(directory, dir, executable)).find((file) => fs.existsSync(file));
  if (!found) throw Error(`Android build-tools is missing ${name}`);
  return found;
}
function generate(apk, output) {
  const repository = process.env.GITHUB_REPOSITORY ?? process.env.FRAMEFILM_APP_RELEASE_REPO ?? 'Rafael-Ikaros/FrameFilm';
  const channel = channelFor(repository);
  const certificate = channel.certificate;
  const root = path.resolve(__dirname, '..');
  const version = fs.readFileSync(path.join(root, 'pubspec.yaml'), 'utf8').match(/^version:\s*(\S+)\+(\d+)\s*$/m);
  if (!version) throw Error('pubspec.yaml must contain version: name+code');
  const bytes = fs.readFileSync(apk);
  if (bytes.length < 4 || bytes.readUInt32LE(0) !== 0x04034b50) throw Error('Expected APK ZIP file');
  const sdk = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT;
  if (!sdk) throw Error('Set ANDROID_HOME to inspect the APK');
  const info = execFileSync(buildTool(sdk, 'aapt'), ['dump', 'badging', apk], { encoding: 'utf8' });
  const signature = execFileSync(buildTool(sdk, 'apksigner'), ['verify', '--print-certs', apk], {
    encoding: 'utf8', shell: process.platform === 'win32',
  });
  const identity = validateApkInfo(info, signature, channel, version[1], Number(version[2]), certificate);
  const metadata = {
    ...identity, repository: channel.repository,
    ...(process.env.TAG ? { tag: process.env.TAG } : {}),
    apkName: APK_NAME, size: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex'),
    certificateSha256: certificate,
  };
  fs.mkdirSync(output, { recursive: true });
  fs.copyFileSync(apk, path.join(output, APK_NAME));
  fs.writeFileSync(path.join(output, METADATA_NAME), JSON.stringify(metadata, null, 2) + '\n');
  console.log(`Prepared ${APK_NAME}, ${channel.repository}, versionCode ${metadata.versionCode}, ${metadata.size} bytes`);
}
if (require.main === module) {
  const [apk, output] = process.argv.slice(2);
  if (!apk || !output) throw Error('Usage: node generate_app_release.cjs <apk> <output directory>');
  generate(apk, output);
}
module.exports = { APK_NAME, METADATA_NAME, channelFor, validateApkInfo, validateHistory };
