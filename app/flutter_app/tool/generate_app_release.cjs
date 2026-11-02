// Release manifest describes the APK actually built with this pubspec version.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [apk, output] = process.argv.slice(2);
if (!apk || !output) throw Error('Usage: node generate_app_release.cjs <apk> <output directory>');
const root = path.resolve(__dirname, '..');
const pubspec = fs.readFileSync(path.join(root, 'pubspec.yaml'), 'utf8');
const version = pubspec.match(/^version:\s*(\S+)\+(\d+)\s*$/m);
if (!version) throw Error('pubspec.yaml must contain version: name+code');
const gradle = fs.readFileSync(path.join(root, 'android/app/build.gradle.kts'), 'utf8');
const packageName = gradle.match(/applicationId\s*=\s*"([^"]+)"/)[1];
const minSdk = Number(gradle.match(/minSdk\s*=\s*(\d+)/)[1]);
const bytes = fs.readFileSync(apk);
if (bytes.length < 4 || bytes.readUInt32LE(0) !== 0x04034b50) throw Error('Expected APK ZIP file');
fs.mkdirSync(output, {recursive: true});
const apkName = 'framefilm-ark-flutter.apk';
fs.copyFileSync(apk, path.join(output, apkName));
const metadata = {
  packageName, versionName: version[1], versionCode: Number(version[2]), minSdk,
  apkName, size: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex'),
};
fs.writeFileSync(path.join(output, 'framefilm-ark-flutter.json'), JSON.stringify(metadata, null, 2) + '\n');
console.log(`Prepared ${apkName}, versionCode ${metadata.versionCode}, ${metadata.size} bytes`);
