const { test } = require('node:test');
const assert = require('node:assert/strict');
const { X509Certificate } = require('node:crypto');
const { rootCertificates } = require('node:tls');
const {
  channelFor, validateApkInfo, validateHistory,
} = require('./generate_app_release.cjs');
const { assetUrl, allowedCdn } = require('./validate_app_release.cjs');
const stable = channelFor('kirmisaki/FrameFilm');
const dev = channelFor('Rafael-Ikaros/FrameFilm');
const sha = 'a'.repeat(64);
const app = {
  packageName: dev.packageName, versionName: '0.4.11', versionCode: 15,
  size: 100, sha256: sha, minSdk: 28,
};
function badging(channel = dev, { code = 15, debug = false, min = 28, target = 35 } = {}) {
  return `package: name='${channel.packageName}' versionCode='${code}' versionName='0.4.11'\nsdkVersion:'${min}'\ntargetSdkVersion:'${target}'\n${debug ? 'application-debuggable' : ''}`;
}
const signature = rootCertificates[0];
const signerFingerprint = new X509Certificate(signature).fingerprint256.replaceAll(':', '').toLowerCase();

test('fixed canonical repositories, explicit unknown is not development', () => {
  assert.equal(channelFor('KIRMISAKI/FRAMEFILM'), stable);
  assert.equal(channelFor('rafael-ikaros/framefilm'), dev);
  for (const value of ['', 'foo/FrameFilm', 'Rafael-ban/FrameFilm', ' kirmisaki/FrameFilm', 'kirmisaki/FrameFilm/']) {
    assert.throws(() => channelFor(value), /Unsupported/);
  }
});
test('each release channel pins its existing public certificate', () => {
  assert.equal(stable.certificate, '9a623e563ef3198ee0b0bd34e3b017af69a20d013bfae093906fdf8b3de79299');
  assert.equal(dev.certificate, 'e5c1b5305670858656fce38cea82614838992aa3326ef3166546af4f9da7245e');
});
test('actual APK package, SDK, version and signer must match distribution', () => {
  const verifierOutput = `Verified APK certificate:\n${signature}\n`;
  assert.equal(validateApkInfo(badging(), verifierOutput, dev, '0.4.11', 15, signerFingerprint).packageName, dev.packageName);
  for (const info of [badging(stable), badging(dev, { code: 14 }), badging(dev, { min: 27 }),
    badging(dev, { target: 34 }), badging(dev, { debug: true })]) {
    assert.throws(() => validateApkInfo(info, signature, dev, '0.4.11', 15, signerFingerprint), /APK must/);
  }
  assert.throws(() => validateApkInfo(badging(), '', dev, '0.4.11', 15, sha), /found 0/);
  assert.throws(() => validateApkInfo(badging(), signature, dev, '0.4.11', 15, 'b'.repeat(64)), /does not match/);
  assert.throws(() => validateApkInfo(badging(), `${signature}\n${rootCertificates[1]}`, dev, '0.4.11', 15, signerFingerprint), /found 2/);
});
test('published version continuity allows only identical-byte reuse, skips opposite historical package', () => {
  validateHistory(app, [{ ...app, versionCode: 14 }]);
  validateHistory(app, [app]);
  validateHistory(app, [{ ...app, packageName: stable.packageName, versionCode: 99 }]);
  for (const old of [{ ...app, versionCode: 16 }, { ...app, sha256: 'b'.repeat(64) },
    { ...app, size: 101 }, { ...app, minSdk: 29 }, { ...app, versionName: 'other' }, { ...app, sha256: 'bad' }]) {
    assert.throws(() => validateHistory(app, [old]));
  }
});
test('exact repository, tag, asset URL and expected release CDN only', () => {
  const url = `https://github.com/${dev.repository}/releases/download/ark%2Fv1%2B2/framefilm-ark-flutter.json`;
  assetUrl(url, dev.repository, 'ark/v1+2', 'framefilm-ark-flutter.json');
  for (const bad of [url.replace('https:', 'http:'), url.replace(dev.repository, stable.repository),
    url.replace('github.com', 'github.com.evil.org'), `${url}?x=1`, `${url}#x`,
    url.replace('github.com', 'user@github.com'), url.replace('v1%2B2', 'v2')]) {
    assert.throws(() => assetUrl(bad, dev.repository, 'ark/v1+2', 'framefilm-ark-flutter.json'));
  }
  assert.equal(allowedCdn(new URL('https://release-assets.githubusercontent.com/github-production-release-asset/1/id?sig=ok')), true);
  assert.equal(allowedCdn(new URL('https://objects.githubusercontent.com/github-production-release-asset-2e65be/1/id?sig=ok')), true);
  for (const bad of ['https://evil.org/file', 'http://release-assets.githubusercontent.com/github-production-release-asset/1/id',
    'https://release-assets.githubusercontent.com/other/file', 'https://user@release-assets.githubusercontent.com/github-production-release-asset/1/id']) {
    assert.equal(allowedCdn(new URL(bad)), false);
  }
});
