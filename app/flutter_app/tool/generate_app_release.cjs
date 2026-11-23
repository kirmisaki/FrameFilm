// Release manifest describes the APK actually built with this pubspec version.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const [apk, output] = process.argv.slice(2);
if (!apk || !output) throw Error('Usage: node generate_app_release.cjs <apk> <output directory>');
const root = path.resolve(__dirname, '..');
const pubspec = fs.readFileSync(path.join(root, 'pubspec.yaml'), 'utf8');
const version = pubspec.match(/^version:\s*(\S+)\+(\d+)\s*$/m);
if (!version) throw Error('pubspec.yaml must contain version: name+code');
const bytes = fs.readFileSync(apk);
if (bytes.length < 4 || bytes.readUInt32LE(0) !== 0x04034b50) throw Error('Expected APK ZIP file');
const sdk = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT;
if (!sdk) throw Error('Set ANDROID_HOME to the Android SDK to inspect the APK');
const buildTools = path.join(sdk, 'build-tools');
const toolName = process.platform === 'win32' ? 'aapt.exe' : 'aapt';
const aapt = fs.readdirSync(buildTools)
  .sort((a, b) => b.localeCompare(a, 'en', { numeric: true }))
  .map((dir) => path.join(buildTools, dir, toolName))
  .find((file) => fs.existsSync(file));
if (!aapt) throw Error('Android build-tools is missing aapt');
const info = execFileSync(aapt, ['dump', 'badging', apk], { encoding: 'utf8' });
const identity = info.match(/^package: name='([^']+)' versionCode='(\d+)' versionName='([^']+)'/m);
const minSdk = Number(info.match(/^sdkVersion:'(\d+)'/m)?.[1]);
const repository = process.env.GITHUB_REPOSITORY || process.env.FRAMEFILM_APP_RELEASE_REPO || 'Rafael-Ikaros/FrameFilm';
const expectedId = repository.toLowerCase() === 'kirmisaki/framefilm'
  ? 'org.framefilm.ark.flutter' : 'org.framefilm.ark.flutter.dev';
if (!identity || identity[1] !== expectedId || identity[2] !== version[2] ||
    identity[3] !== version[1] || !Number.isInteger(minSdk) || minSdk <= 0) {
  throw Error(`APK manifest must match ${expectedId} ${version[1]}+${version[2]} and declare its minimum SDK`);
}
const [, packageName, versionCode, versionName] = identity;
fs.mkdirSync(output, {recursive: true});
const apkName = 'framefilm-ark-flutter.apk';
fs.copyFileSync(apk, path.join(output, apkName));
const metadata = {
  packageName, versionName, versionCode: Number(versionCode), minSdk,
  apkName, size: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex'),
};
fs.writeFileSync(path.join(output, 'framefilm-ark-flutter.json'), JSON.stringify(metadata, null, 2) + '\n');
console.log(`Prepared ${apkName}, versionCode ${metadata.versionCode}, ${metadata.size} bytes`);
