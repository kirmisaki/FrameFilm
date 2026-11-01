const {test} = require('node:test');
const assert = require('node:assert/strict');
const {plan} = require('./ark_release_plan.cjs');

const pubspec = 'name: framefilm_ark\nversion: 0.4.1+5\n';
const current = '# Ark 更新日志\n\n## [Unreleased]\n\n准备中。\n\n## [2026.10.16.1] - 2026-10-16\n\nApp: 0.4.1+5\n\n- 修复固件显示。\n';
const previous = '# Ark 更新日志\n\n## [Unreleased]\n\n准备中。\n';

test('push releases only a newly introduced concrete heading', () => {
  const result = plan(current, previous, pubspec, 'push');
  assert.equal(result.tag, 'ark-2026.10.16.1');
  assert.equal(result.title, 'Ark 2026.10.16.1 · App 0.4.1+5');
  assert.match(result.notes, /修复固件显示/);
  assert.equal(plan(current, current, pubspec, 'push').publish, false);
  assert.equal(plan(previous, previous, pubspec, 'push').publish, false);
});

test('manual retry may select an existing concrete heading', () => {
  assert.equal(plan(current, current, pubspec, 'workflow_dispatch').publish, true);
  assert.equal(plan(current, previous, pubspec, 'pull_request').publish, false);
  assert.equal(plan(current, current, 'version: 0.4.2+6', 'workflow_dispatch', false).tag, 'ark-2026.10.16.1');
  assert.throws(() => plan(current, current, 'version: 0.4.2+6', 'workflow_dispatch'), /differs from pubspec/);
});

test('APK version must match the source pubspec, and firmware-only releases may reuse it', () => {
  const next = current.replace('2026.10.16.1', '2026.10.16.2');
  assert.equal(plan(next, current, pubspec, 'push').versionCode, '5');
  assert.throws(() => plan(next.replace('0.4.1+5', '0.4.2+6'), current, pubspec, 'push'), /differs from pubspec/);
  assert.throws(() => plan(current + current.slice(current.indexOf('## [2026.')), previous, pubspec, 'push'), /duplicate release IDs/);
});
