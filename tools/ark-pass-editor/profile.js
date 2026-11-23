/* SPDX-License-Identifier: GPL-3.0-or-later */
(function (root) {
  'use strict';
  const template = 'rhodes-academy-student';
  const palettes = ['amiya', 'sniper', 'medic', 'vanguard', 'guard', 'defender', 'caster', 'supporter', 'specialist'];
  function parse(raw) {
    if (new TextEncoder().encode(raw).length > 350000) throw new Error('JSON 超过 350,000 字节');
    const value = JSON.parse(raw);
    if (!value || ![1, 2, 3].includes(value.version)) throw new Error('JSON 版本不支持');
    const student = value.version === 2 || (value.version === 3 && value.kind === 'student');
    if (value.version === 3 && !['student', 'operator'].includes(value.kind)) throw new Error('JSON kind 无效');
    if (student && value.template !== template) throw new Error('JSON 学生证模板类型不支持');
    const limits = student ? { codename: 32, studentNumber: 12, race: 60 } : { codename: 32, number: 36, affiliation: 60, signature: 200 };
    const result = { version: 3, kind: student ? 'student' : 'operator', output: 'i006' };
    for (const [key, limit] of Object.entries(limits)) {
      if (typeof value[key] !== 'string' || value[key].length > limit) throw new Error(`${key} 字段格式或长度无效`);
      result[key] = value[key];
    }
    for (const key of ['codenameUnset', student ? 'studentNumberUnset' : 'numberUnset']) {
      if (typeof value[key] !== 'boolean') throw new Error('尚未设置标记格式无效');
      result[key] = value[key];
    }
    if (value.avatar !== null && typeof value.avatar !== 'string') throw new Error('avatar 字段格式无效');
    result.avatar = value.avatar;
    if (student) {
      result.template = template; result.palette = value.palette === undefined ? 'amiya' : value.palette;
      if (!palettes.includes(result.palette)) throw new Error('JSON 学生证配色不支持');
    }
    if (value.version === 3) {
      if (value.avatarMode !== undefined && value.avatarMode !== 'color') throw new Error('v3 不支持黑白/灰阶头像声明');
      if (value.studentOutput !== undefined && value.studentOutput !== value.output) throw new Error('v3 输出声明不一致');
      if (!['i006', 'colorfast'].includes(value.output)) throw new Error('JSON 输出模式无效');
      result.output = value.output;
    } else {
      const mode = value.avatarMode === undefined ? 'mono' : value.avatarMode;
      if (!['mono', 'grayscale', 'color'].includes(mode)) throw new Error('头像显示模式无效');
      if (student && value.studentOutput !== undefined && !['i006', 'ffui'].includes(value.studentOutput)) throw new Error('JSON 学生证输出模式不支持');
      // Migration state is local only. Never mislabel six-color FFUI as ColorFast.
      result._legacyAvatar = !!value.avatar && mode !== 'color';
      result._legacySize = !!value.avatar;
      result._legacyOutput = !student || value.studentOutput === 'ffui';
    }
    return result;
  }
  function wire(profile) {
    const clean = {};
    for (const [key, value] of Object.entries(profile)) if (!key.startsWith('_')) clean[key] = value;
    const bytes = new TextEncoder().encode(JSON.stringify(clean, null, 2) + '\n');
    if (bytes.length > 350000) throw new Error('JSON 超过 350,000 字节；请重新裁切头像');
    return bytes;
  }
  function capability(flags, kind, output) {
    const bit = kind === 'operator' ? 15 : output === 'i006' ? 11 : 14;
    if (!(flags & (1 << bit))) throw new Error(`设备不支持${kind === 'student' ? '学生证' : '紧凑档案'} ${output === 'i006' ? 'I006' : 'ColorFast'}，请升级固件；未降级或发送文件`);
  }
  const api = { parse, wire, capability };
  root.PassportProfile = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
