/* SPDX-License-Identifier: GPL-3.0-or-later */
(() => {
  'use strict';
  const W = 440, H = 608, AV_W = 128, AV_H = 160;
  const PROFILE_VERSION = 1;
  const DRAFT_KEY = 'framefilm-ark-pass-profile-v1';
  const STUDENT_TEMPLATE = 'rhodes-academy-student';
  const STUDENT_DRAFT_KEY = 'framefilm-ark-pass-rhodes-academy-student-v2';
  const STUDENT_W = 720, STUDENT_H = 480;
  // PSD 1011×638 scaled by 480/638 and center-cropped horizontally to 720.
  const STUDENT_TOP_STRIPE = { left: 218, sourceHeight: 48, visibleHeight: 10 };
  const STUDENT_AVATAR = { x: 41, y: 111, width: 239, height: 299 };
  const STUDENT_CODE = { x: 305, y: 293, size: 35, width: 395 };
  const STUDENT_NUMBER = { x: 308, y: 355, size: 20, width: 235 };
  const STUDENT_RACE = { x: 304, y: 408, size: 20, width: 235 };
  const DEFAULT_STUDENT_PALETTE = 'amiya';
  const DEFAULT_STUDENT_OUTPUT = 'i006';
  const studentPalettes = new Map([
    ['amiya', '阿米娅'], ['sniper', '狙击'], ['medic', '医疗'], ['vanguard', '先锋'],
    ['guard', '近卫'], ['defender', '重装'], ['caster', '术师'], ['supporter', '辅助'], ['specialist', '特种']
  ]);
  const studentBases = new Map();
  const studentBaseUrl = (palette) => `student-palettes/fullscreen/${palette}.png`;
  const CAP_COLOR_PASS = 1 << 0, CAP_FULLSCREEN_PASS = 1 << 10, CAP_I006_PASS = 1 << 11;
  const usesStudentI006 = () => isStudent() && profile.studentOutput === DEFAULT_STUDENT_OUTPUT;
  const SERVICE_UUID = '00002000-0000-1000-8000-00805f9b34fb';
  const CHARACTERISTIC_UUID = '00002001-0000-1000-8000-00805f9b34fb';
  const CH = { start: 0x03, name: 0x00, length: 0x01, data: 0x02, stop: 0x04 };
  const DEVICE_NAME_PREFIX = 'FRAMEFILMARK-';
  const NAME_GET = 0x54, NAME_SET = 0x55;
  const embedded = new URLSearchParams(location.search).get('embedded') === '1' && window.parent !== window;
  const getArkDevice = () => embedded ? window.parent.ArkDevice : null;
  const $ = (id) => document.getElementById(id);
  const canvas = $('preview');
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  const work = document.createElement('canvas');
  work.width = W; work.height = H;
  const g = work.getContext('2d', { willReadFrequently: true });
  const operatorFields = ['codename', 'number', 'affiliation', 'signature'];
  const studentFields = ['codename', 'student-number', 'race'];
  const fields = [...operatorFields, 'student-number', 'race'];
  const fieldKey = (id) => id === 'student-number' ? 'studentNumber' : id;
  const templateOf = (value) => value.version === 2 ? STUDENT_TEMPLATE : 'operator';
  const isStudent = () => templateOf(profile) === STUDENT_TEMPLATE;
  const activeFields = () => isStudent() ? studentFields : operatorFields;
  const draftKey = (template) => template === STUDENT_TEMPLATE ? STUDENT_DRAFT_KEY : DRAFT_KEY;
  const drafts = {
    operator: { profile: emptyProfile(), image: null, edit: null },
    [STUDENT_TEMPLATE]: { profile: emptyProfile(STUDENT_TEMPLATE), image: null, edit: null }
  };
  let profile = drafts.operator.profile;
  let avatarImage = null;
  let avatarEdit = null;
  let renderedBin = null, capabilityWaiter = null;
  let renderGeneration = 0, renderTimer;
  let valid = false;
  let device = null, characteristic = null, transfer = null, readJob = null, readWaiter = null, nameWaiter = null;
  let nameWriteInProgress = false;
  let editingLocked = false;
  let parentState = { connected: false, busy: false, label: '' };

  // The embedded editor follows the host skin; the device canvas uses its selected palette.
  function syncParentTheme() {
    if (!embedded) return;
    const host = window.parent.document.body;
    const style = window.parent.getComputedStyle(host);
    const ark = host.classList.contains('theme-ark');
    document.body.classList.toggle('theme-ark', ark);
    document.documentElement.classList.toggle('embedded-ark', ark);
    document.documentElement.style.colorScheme = style.colorScheme;
    const tokens = {
      '--ink': '--ink', '--muted': '--ink-soft', '--paper': '--paper',
      '--editor-accent': '--sky', '--editor-radius': '--r',
      '--editor-field-radius': '--r-xs', '--editor-button-radius': '--r-sm',
      '--editor-font': '--font-body', '--editor-display': '--font-display',
      '--editor-border-width': '--line', '--editor-shadow': '--sh-1',
      '--editor-shadow-hover': '--sh-2', '--editor-rule': '--ink',
      '--editor-frame-bg': '--cream-2', '--editor-primary-bg': '--lemon',
      '--editor-focus': '--focus-ink', '--editor-input-accent': '--control-accent',
      '--progress-height': '--progress-height', '--progress-radius': '--progress-radius',
      '--progress-track': '--progress-track', '--progress-value': '--progress-value',
      '--progress-border': '--progress-border', '--danger': '--error-ink'
    };
    if (ark) {
      Object.assign(tokens, {
        '--line': '--ark-line', '--ground': '--ark-bg',
        '--editor-surface': '--ark-bg-2', '--danger': '--ark-red',
        '--editor-rule': '--ark-line', '--editor-frame-bg': '--ark-bg'
      });
    } else {
      for (const token of ['--line', '--ground', '--editor-surface']) {
        document.body.style.removeProperty(token);
      }
    }
    for (const [local, parent] of Object.entries(tokens)) {
      document.body.style.setProperty(local, style.getPropertyValue(parent).trim());
    }
  }

  function deviceAvailable() {
    if (!embedded) return !!characteristic;
    const arkDevice = getArkDevice();
    return !!arkDevice?.connected() && !arkDevice.busy();
  }
  function refreshDeviceControls() {
    const ownJob = !!transfer || !!readJob;
    $('send').disabled = !valid || !renderedBin || !deviceAvailable() || ownJob || editingLocked;
    $('read-device').disabled = !deviceAvailable() || ownJob || editingLocked;
    if (embedded) $('cancel').disabled = !ownJob;
  }
  function updateParentState(event) {
    if (!embedded) return;
    parentState = event?.detail || {
      connected: !!getArkDevice()?.connected(), busy: !!getArkDevice()?.busy(), label: ''
    };
    refreshDeviceControls();
    if (!transfer && !readJob) {
      setDeviceStatus(!parentState.connected ? 'Ark 未连接。请返回 ForFilm 连接页连接设备。' :
        parentState.busy ? `设备正在执行${parentState.label || '其他操作'}，请稍候。` :
          'Ark 已连接，可以发送或读取资料。');
    }
  }

  function emptyProfile(template = 'operator') {
    if (template === STUDENT_TEMPLATE) return {
      version: 2, template: STUDENT_TEMPLATE, palette: DEFAULT_STUDENT_PALETTE, studentOutput: DEFAULT_STUDENT_OUTPUT,
      codename: '', codenameUnset: false,
      studentNumber: '', studentNumberUnset: false, race: '', avatar: null, avatarMode: 'mono'
    };
    return { version: PROFILE_VERSION, codename: '', codenameUnset: false,
      number: '', numberUnset: false, affiliation: '', signature: '', avatar: null, avatarMode: 'mono' };
  }
  const cjk = '"Noto Sans CJK SC","Microsoft YaHei","PingFang SC",sans-serif';
  const mono = 'ui-monospace,Consolas,monospace';
  const font = (size, weight = 500, family = cjk) => `${weight} ${size}px ${family}`;
  function txt(value, x, y, size, weight = 500, family = cjk) {
    g.font = font(size, weight, family); g.fillStyle = '#000'; g.fillText(value, x, y);
  }
  function line(x1, y1, x2, y2, width = 1) {
    g.strokeStyle = '#000'; g.lineWidth = width; g.beginPath();
    g.moveTo(x1 + .5, y1 + .5); g.lineTo(x2 + .5, y2 + .5); g.stroke();
  }
  function chip(label, x, y, w) {
    g.fillStyle = '#000'; g.fillRect(x, y, w, 23);
    g.fillStyle = '#fff'; g.font = font(14, 700); g.textAlign = 'center';
    g.fillText(label, x + w / 2, y + 17); g.textAlign = 'left';
  }
  function measure(value, size, weight = 500, family = cjk) {
    g.font = font(size, weight, family);
    return g.measureText(value).width;
  }
  function wrap(value, width, size, weight = 500) {
    const rows = [];
    for (const paragraph of value.split(/\r?\n/)) {
      if (!paragraph) { rows.push(''); continue; }
      let row = '';
      for (const char of paragraph) {
        if (row && measure(row + char, size, weight) > width) {
          rows.push(row); row = char;
        } else row += char;
      }
      rows.push(row);
    }
    return rows;
  }
  function setError(id, message) {
    $(id + '-error').textContent = message;
    $(id).setAttribute('aria-invalid', message ? 'true' : 'false');
  }
  function validMonthDay(month, day, year) {
    const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
    const days = [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    return month >= 1 && month <= 12 && day >= 1 && day <= days[month - 1];
  }
  function validStudentNumber(value) {
    if (!/^\d{12}$/.test(value)) return false;
    const year = Number(value.slice(0, 4));
    return year >= 1 && validMonthDay(Number(value.slice(4, 6)), Number(value.slice(6, 8)), year) &&
      validMonthDay(Number(value.slice(8, 10)), Number(value.slice(10, 12)), 2000);
  }
  function validate() {
    const code = profile.codename.trim();
    let errors;
    if (isStudent()) {
      const num = profile.studentNumber.trim();
      errors = {
        codename: !code && !profile.codenameUnset ? '请输入代号，或勾选“尚未设置”。' :
          code && measure(code, STUDENT_CODE.size, 700) > STUDENT_CODE.width ? '代号超出预览区域，请缩短。' : '',
        'student-number': !num && !profile.studentNumberUnset ? '请输入学号，或勾选“尚未设置”。' :
          num && !validStudentNumber(num) ? '学号须为 12 位有效日期组合：YYYYMMDD + MMDD（生日允许 02-29）。' :
            num && measure(num, STUDENT_NUMBER.size, 600, mono) > STUDENT_NUMBER.width ? '学号超出预览区域，请缩短。' : '',
        race: measure(profile.race.trim(), STUDENT_RACE.size, 600) > STUDENT_RACE.width ? '种族超出预览区域，请缩短。' : ''
      };
    } else {
      const num = profile.number.trim();
      errors = {
        codename: !code && !profile.codenameUnset ? '请输入代号，或勾选“尚未设置”。' :
          code && measure(code, 29, 700) > 247 ? '代号超出预览区域，请缩短。' : '',
        number: !num && !profile.numberUnset ? '请输入编号，或勾选“尚未设置”。' :
          num && measure(num, 17, 600, mono) > 247 ? '编号超出预览区域，请缩短。' : '',
        affiliation: measure(profile.affiliation.trim(), 17, 600) > 376 ? '所属超出预览区域，请缩短。' : '',
        signature: wrap(profile.signature.trim(), 370, 17).length > 3 ? '签名最多占三行，请缩短。' : ''
      };
    }
    for (const id of fields) setError(id, errors[id] || '');
    valid = activeFields().every((id) => !errors[id]);
    const canExport = valid && renderedBin !== null;
    $('download-bin').disabled = !canExport || editingLocked || !!transfer || !!readJob;
    $('download-png').disabled = !canExport || editingLocked || !!transfer || !!readJob;
    refreshDeviceControls();
    return canExport;
  }
  function drawOperator() {
    g.fillStyle = '#fff'; g.fillRect(0, 0, W, H);
    // 只绘制主体，固件负责其余 30 + 28 px。
    txt('RHODES ISLAND', 20, 24, 10, 700, mono);
    chip('档案 / 01', 325, 9, 95);
    line(20, 34, 420, 34);
    txt('干员档案', 20, 79, 31, 700);
    txt('罗德岛终端 / 个人身份档案', 21, 103, 12, 600);
    line(20, 119, 420, 119);

    const ax = 20, ay = 143;
    g.fillStyle = '#fff'; g.fillRect(ax, ay, AV_W, AV_H);
    if (avatarImage) g.drawImage(avatarImage, ax, ay, AV_W, AV_H);
    else {
      g.strokeStyle = '#000'; g.strokeRect(ax + .5, ay + .5, AV_W - 1, AV_H - 1);
      line(ax + 16, ay + 16, ax + AV_W - 16, ay + AV_H - 16);
      line(ax + AV_W - 16, ay + 16, ax + 16, ay + AV_H - 16);
      g.fillStyle = '#fff'; g.fillRect(ax + 18, ay + 66, AV_W - 36, 27);
      txt('未设置头像', ax + 32, ay + 84, 13, 700);
    }
    line(20, 308, 148, 308);
    const right = 173;
    chip('代号', right, 143, 65);
    txt(profile.codename.trim() || '未设置', right, 204, 29, 700);
    line(right, 220, 420, 220);
    txt('档案编号', right, 246, 13, 600);
    txt(profile.number.trim() || '未设置', right, 276, 17, 600, mono);
    line(right, 291, 420, 291);
    txt('档案 01 / 身份', right, 320, 11, 700);

    line(20, 354, 420, 354);
    chip('所属', 20, 371, 65);
    txt(profile.affiliation.trim() || '未设置', 21, 427, 17, 600);
    line(20, 447, 420, 447);
    txt('个人签名', 20, 473, 13, 700);
    const rows = wrap(profile.signature.trim() || '未设置', 370, 17);
    rows.slice(0, 3).forEach((row, index) => txt(row, 21, 506 + index * 27, 17));
    line(20, 580, 420, 580);
    txt('ARKNIGHTS / 个人档案', 20, 597, 10, 700);
    txt('罗德岛', 377, 597, 10, 700);

  }
  async function loadStudentBase(palette) {
    const base = { image: null, failed: false };
    studentBases.set(palette, base);
    const img = new Image();
    img.src = studentBaseUrl(palette);
    try {
      await img.decode();
      if (img.naturalWidth !== STUDENT_W || img.naturalHeight !== STUDENT_H) throw new Error('学生证底图尺寸不符。');
      base.image = img;
    } catch {
      base.failed = true;
    }
    if (isStudent() && profile.palette === palette) draw();
  }

  function drawStudent(base) {
    g.fillStyle = '#fff'; g.fillRect(0, 0, STUDENT_W, STUDENT_H);
    const { left, sourceHeight, visibleHeight } = STUDENT_TOP_STRIPE;
    // Keep just the top edge of the PSD stripe and expose the white card stock below it.
    g.save();
    g.beginPath();
    g.rect(0, 0, STUDENT_W, visibleHeight);
    g.rect(0, visibleHeight, left, sourceHeight - visibleHeight);
    g.rect(0, sourceHeight, STUDENT_W, STUDENT_H - sourceHeight);
    g.clip();
    g.drawImage(base.image, 0, 0);
    g.restore();
    if (usesStudentI006() && !base.pixels) base.pixels = g.getImageData(0, 0, STUDENT_W, STUDENT_H);
    const { x, y, width, height } = STUDENT_AVATAR;
    if (avatarImage) g.drawImage(avatarImage, x, y, width, height);

    g.font = font(STUDENT_CODE.size, 700); g.fillStyle = '#fff';
    g.fillText(profile.codename.trim() || '未设置', STUDENT_CODE.x, STUDENT_CODE.y);
    txt(profile.studentNumber.trim() || '未设置', STUDENT_NUMBER.x, STUDENT_NUMBER.y, STUDENT_NUMBER.size, 600, mono);
    txt(profile.race.trim() || '未设置', STUDENT_RACE.x, STUDENT_RACE.y, STUDENT_RACE.size, 600);
  }
  function encodeStudentFfui(pixels, mode) {
    return PassportPixels.encode(pixels.data, mode, !!avatarImage,
      { width: STUDENT_W, height: STUDENT_H, avatarRect: STUDENT_AVATAR, ditherArtwork: true, rotateClockwise: true });
  }
  async function renderI006(pixels, basePixels, generation) {
    const cancelled = () => generation !== renderGeneration;
    const status = $('student-base-status');
    try {
      const { bin, rgba } = await PassportStudentI006.render(pixels, basePixels, { cancelled, avatarRect: STUDENT_AVATAR });
      if (cancelled()) return;
      pixels.data.set(rgba);
      ctx.putImageData(pixels, 0, 0);
      renderedBin = bin;
      status.textContent = '';
    } catch (error) {
      if (cancelled()) return;
      status.textContent = `无法生成 I006：${error.message}。可选择兼容 FFUI，资料不会丢失。`;
    }
    validate();
  }
  function draw() {
    const generation = ++renderGeneration;
    clearTimeout(renderTimer);
    const student = isStudent(), width = student ? STUDENT_W : W, height = student ? STUDENT_H : H;
    if (work.width !== width || work.height !== height) {
      work.width = width; work.height = height;
      canvas.width = width; canvas.height = height;
    }
    const palette = profile.palette;
    if (student && !studentBases.has(palette)) void loadStudentBase(palette);
    const base = student ? studentBases.get(palette) : null;
    const baseStatus = $('student-base-status');
    if (student && !base.image) {
      renderedBin = null;
      ctx.clearRect(0, 0, width, height);
      const label = studentPalettes.get(palette);
      baseStatus.textContent = base.failed
        ? `「${label}」底图不可用。请确认编辑器目录内有 ${STUDENT_W} × ${STUDENT_H} 的 ${studentBaseUrl(palette)}，然后刷新。仍可编辑、导出 JSON 或切换配色。`
        : `正在加载「${label}」底图…`;
    } else {
      baseStatus.textContent = '';
      if (student) drawStudent(base); else drawOperator();
      const pixels = g.getImageData(0, 0, width, height);
      if (usesStudentI006()) {
        renderedBin = null;
        baseStatus.textContent = '正在生成 I006 设备色彩…';
        renderTimer = setTimeout(() => void renderI006(pixels, base.pixels, generation), 120);
      } else {
        renderedBin = student ? encodeStudentFfui(pixels, profile.avatarMode) :
          PassportPixels.encode(pixels.data, profile.avatarMode, !!avatarImage);
      }
      ctx.putImageData(pixels, 0, 0);
    }
    validate();
    updateAvatarButtons();
  }
  function profileBin() {
    return renderedBin.slice();
  }
  function setFileStatus(message) { $('file-status').textContent = message; }
  function setDeviceStatus(message) { $('device-status').textContent = message; }
  function download(blob, filename) {
    const url = URL.createObjectURL(blob), a = document.createElement('a');
    a.href = url; a.download = filename; document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  function jsonBytes() { return new TextEncoder().encode(JSON.stringify(profile, null, 2) + '\n'); }
  function cacheDraft() {
    drafts[templateOf(profile)] = { profile, image: avatarImage, edit: avatarEdit };
  }
  function persist() {
    cacheDraft();
    try { localStorage.setItem(draftKey(templateOf(profile)), JSON.stringify(avatarEdit ? {draftVersion:2,profile,avatarEditor:avatarEdit} : profile)); }
    catch { setFileStatus('浏览器无法保存本地草稿；请导出 JSON 留存。'); }
  }
  function activateDraft(template) {
    cacheDraft();
    const draft = drafts[template];
    profile = draft.profile; avatarImage = draft.image; avatarEdit = draft.edit;
    syncForm();
  }
  function acceptImported(imported, img) {
    cacheDraft();
    profile = imported; avatarImage = img; avatarEdit = null;
    persist(); syncForm();
  }
  function syncForm() {
    const template = templateOf(profile), student = isStudent();
    $('template-select').value = template;
    for (const section of document.querySelectorAll('[data-template]')) section.hidden = section.dataset.template !== template;
    for (const id of fields) $(id).value = profile[fieldKey(id)] || '';
    $('codename-unset').checked = profile.codenameUnset;
    $('number-unset').checked = !!profile.numberUnset;
    $('student-number-unset').checked = !!profile.studentNumberUnset;
    $('student-palette').value = profile.palette || DEFAULT_STUDENT_PALETTE;
    $('student-output').value = profile.studentOutput || DEFAULT_STUDENT_OUTPUT;
    $('avatar-mode').value = profile.avatarMode;
    $('preview-dimensions').textContent = student ? '720 × 480 / 横向全屏' : '440 × 608 / 设备色彩';
    $('student-orientation').hidden = !student;
    canvas.parentElement.classList.toggle('student-preview', student);
    canvas.setAttribute('aria-label', student ? '学生证横向主体预览' : '通行证主体预览');
    draw();
  }
  function fromForm() {
    if (editingLocked) return;
    for (const id of activeFields()) profile[fieldKey(id)] = $(id).value;
    profile.codenameUnset = $('codename-unset').checked;
    if (isStudent()) profile.studentNumberUnset = $('student-number-unset').checked;
    else profile.numberUnset = $('number-unset').checked;
    profile.avatarMode = $('avatar-mode').value;
    persist(); draw();
  }
  async function imageFromDataUrl(dataUrl) {
    if (dataUrl === null) return null;
    if (!/^data:image\/png;base64,/i.test(dataUrl) || dataUrl.length > 300000) throw new Error('头像必须是裁切后的 PNG data URL（不超过 300 KB）。');
    const img = new Image(); img.src = dataUrl; await img.decode();
    if (img.naturalWidth !== AV_W || img.naturalHeight !== AV_H) throw new Error('头像尺寸必须为 128×160。');
    return img;
  }
  async function avatarFromFile(file) {
    if (!file || editingLocked) return;
    setEditingLocked(true);
    try {
      await editAvatar({source:await PassportAvatarEditor.sourceFromFile(file),options:{...PassportAvatarEditor.defaults}});
    } finally { setEditingLocked(false); }
  }
  async function editAvatar(replacement) {
    const edit=replacement || avatarEdit || (profile.avatar ? {source:profile.avatar,options:{...PassportAvatarEditor.defaults}} : null);
    if(!edit)return;
    setEditingLocked(true);
    try {
      const result=await PassportAvatarEditor.open(edit,profile.avatarMode);
      if(result){avatarImage=await imageFromDataUrl(result.png);profile.avatar=result.png;avatarEdit=result.edit;persist();draw();}
    } finally {setEditingLocked(false);}
  }
  async function changeAvatarMode() {
    const mode=$('avatar-mode').value;
    if(!profile.avatar){profile.avatarMode=mode;persist();draw();return;}
    setEditingLocked(true);
    try {
      const edit=avatarEdit || {source:profile.avatar,options:{...PassportAvatarEditor.defaults}};
      const png=await PassportAvatarEditor.processed(edit,mode);
      avatarImage=await imageFromDataUrl(png);profile.avatar=png;avatarEdit=edit;profile.avatarMode=mode;persist();draw();
    } catch(e){$('avatar-mode').value=profile.avatarMode;setFileStatus(`头像处理失败：${e.message}`);}
    finally {setEditingLocked(false);}
  }
  function parseProfile(raw) {
    const value = JSON.parse(raw);
    if (!value || typeof value !== 'object' || ![PROFILE_VERSION, 2].includes(value.version)) throw new Error('JSON 版本不支持。');
    const student = value.version === 2;
    if (student && value.template !== STUDENT_TEMPLATE) throw new Error('JSON 学生证模板类型不支持。');
    const limits = student ? { codename: 32, studentNumber: 12, race: 60 } :
      { codename: 32, number: 36, affiliation: 60, signature: 200 };
    for (const [id, limit] of Object.entries(limits)) {
      if (typeof value[id] !== 'string' || value[id].length > limit) throw new Error(`${id} 字段格式或长度无效。`);
    }
    if (typeof value.codenameUnset !== 'boolean' || typeof value[student ? 'studentNumberUnset' : 'numberUnset'] !== 'boolean') throw new Error('尚未设置标记格式无效。');
    if (value.avatar !== null && typeof value.avatar !== 'string') throw new Error('avatar 字段格式无效。');
    const avatarMode = value.avatarMode === undefined ? 'mono' : value.avatarMode;
    if (!['mono', 'grayscale', 'color'].includes(avatarMode)) throw new Error('头像显示模式无效。');
    if (student) {
      const palette = value.palette === undefined ? DEFAULT_STUDENT_PALETTE : value.palette;
      if (!studentPalettes.has(palette)) throw new Error('JSON 学生证配色不支持。');
      const studentOutput = value.studentOutput === undefined ? DEFAULT_STUDENT_OUTPUT : value.studentOutput;
      if (![DEFAULT_STUDENT_OUTPUT, 'ffui'].includes(studentOutput)) throw new Error('JSON 学生证输出模式不支持。');
      return { version: 2, template: STUDENT_TEMPLATE, palette, studentOutput, codename: value.codename, codenameUnset: value.codenameUnset,
        studentNumber: value.studentNumber, studentNumberUnset: value.studentNumberUnset, race: value.race,
        avatar: value.avatar, avatarMode };
    }
    return { version: PROFILE_VERSION, codename: value.codename, codenameUnset: value.codenameUnset,
      number: value.number, numberUnset: value.numberUnset, affiliation: value.affiliation,
      signature: value.signature, avatar: value.avatar, avatarMode };
  }
  async function restoreDraft(template) {
    const stored = localStorage.getItem(draftKey(template));
    if (!stored) return;
    const value = JSON.parse(stored), wrapper = value?.draftVersion === 2;
    const restored = parseProfile(wrapper ? JSON.stringify(value.profile) : stored);
    if (templateOf(restored) !== template) throw new Error('本地草稿模板不匹配。');
    const image = await imageFromDataUrl(restored.avatar);
    const edit = wrapper && restored.avatar ? PassportAvatarEditor.validateEdit(value.avatarEditor) : null;
    // Do not replace even the in-memory draft until both JSON and avatar validation finish.
    drafts[template] = { profile: restored, image, edit };
  }
  function packet(channel, payload = new Uint8Array()) {
    const out = new Uint8Array(payload.length + 4);
    out[0] = 0x55; out[1] = channel; out[2] = payload.length; out.set(payload, 3);
    out[out.length - 1] = out.subarray(0, out.length - 1).reduce((sum, byte) => (sum + byte) & 255, 0);
    return out;
  }
  function u32(bytes, at) {
    return (((bytes[at] << 24) >>> 0) + (bytes[at + 1] << 16) + (bytes[at + 2] << 8) + bytes[at + 3]) >>> 0;
  }
  function be32(value) {
    return [(value >>> 24) & 255, (value >>> 16) & 255, (value >>> 8) & 255, value & 255];
  }
  function finishReadWaiter(error, response) {
    const waiter = readWaiter;
    if (!waiter) return;
    readWaiter = null; clearTimeout(waiter.timer);
    if (error) waiter.reject(error); else waiter.resolve(response);
  }
  function finishNameWaiter(error, response) {
    const waiter = nameWaiter;
    if (!waiter) return;
    nameWaiter = null; clearTimeout(waiter.timer);
    if (error) waiter.reject(error); else waiter.resolve(response);
  }
  function updateNameControls() {
    const available = !!characteristic && !!device?.gatt?.connected && !transfer && !readJob &&
      !nameWaiter && !nameWriteInProgress;
    $('device-name-read').disabled = !available;
    $('device-name-save').disabled = !available;
    $('device-name-suffix').disabled = !available;
  }
  function prepareProfileOperation() {
    if (nameWriteInProgress || nameWaiter?.channel === NAME_SET) {
      setDeviceStatus('名称命令正在保存或写入；请稍候再操作资料。');
      return false;
    }
    if (nameWaiter) finishNameWaiter(new Error('已开始资料操作，名称查询已中止。'));
    return true;
  }
  function nameResponse(payload) {
    const errors = { 1: '名称参数无效。', 2: '设备保存失败，请重试。' };
    if (!payload.length) throw new Error('设备名称回包为空。');
    if (payload[0] !== 0) throw new Error(errors[payload[0]] || `设备返回未知状态 ${payload[0]}。`);
    const nameBytes = payload.subarray(1);
    if (!nameBytes.length || nameBytes[nameBytes.length - 1] !== 0) throw new Error('设备名称回包缺少结束符。');
    const name = new TextDecoder('utf-8', { fatal: true }).decode(nameBytes.subarray(0, -1));
    if (!name.startsWith(DEVICE_NAME_PREFIX)) throw new Error('设备返回的名称前缀无效。');
    return name;
  }
  function inspectReadPayload(payload, expectedOffset) {
    if (payload.length < 9 || payload.length > 137) throw new Error('设备返回的读取数据长度无效。');
    const offset = u32(payload, 1);
    if (offset !== expectedOffset) throw new Error('设备返回的资料分块偏移无效。');
    return { status: payload[0], offset, total: u32(payload, 5), data: payload.subarray(9) };
  }
  function parseReadPayload(payload, expectedOffset, count) {
    const part = inspectReadPayload(payload, expectedOffset);
    const failures = { 1: '设备尚未配置个人资料。', 2: '设备读取资料失败。', 3: '设备正忙，请稍后重试。', 4: '设备拒绝了读取参数。' };
    if (part.status !== 0) throw new Error(failures[part.status] || `设备返回未知状态 ${part.status}。`);
    if (part.total > 350000 || part.offset > part.total || part.data.length > count || part.offset + part.data.length > part.total ||
        (part.offset < part.total && part.data.length === 0)) throw new Error('设备返回的资料大小或分块范围无效。');
    return { offset: part.offset, total: part.total, data: new Uint8Array(part.data) };
  }
  function onNotification(event) {
    const value = event.target.value;
    const bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    if (capabilityWaiter && bytes.length >= 2 && bytes[1] === 0x59) {
      const waiter = capabilityWaiter; capabilityWaiter = null; clearTimeout(waiter.timer);
      if (bytes.length !== 9 || bytes[0] !== 0x55 || bytes[2] !== 5 ||
          bytes.subarray(0, -1).reduce((sum, byte) => (sum + byte) & 255, 0) !== bytes[8]) {
        waiter.reject(new Error('彩色能力响应无效；未发送资料文件。'));
      } else waiter.resolve(bytes.slice(3, -1));
      return;
    }
    if (nameWaiter && bytes.length >= 2 && bytes[1] === nameWaiter.channel) {
      if (bytes.length < 4 || bytes[0] !== 0x55 || bytes[2] !== bytes.length - 4 ||
          bytes.subarray(0, -1).reduce((sum, byte) => (sum + byte) & 255, 0) !== bytes[bytes.length - 1]) {
        finishNameWaiter(new Error('设备名称回包格式或校验和无效。'));
      } else {
        try { finishNameWaiter(null, nameResponse(bytes.subarray(3, -1))); }
        catch (error) { finishNameWaiter(error); }
      }
      return;
    }
    if (!readWaiter) return;
    if (bytes.length < 2 || bytes[1] !== 0x53) return; // 其它命令的通知不属于本次读取。
    if (bytes.length < 4 || bytes[0] !== 0x55 || bytes[2] !== bytes.length - 4 ||
        bytes.subarray(0, bytes.length - 1).reduce((sum, byte) => (sum + byte) & 255, 0) !== bytes[bytes.length - 1]) {
      finishReadWaiter(new Error('设备返回的读取帧头、长度或校验和无效。'));
      return;
    }
    const payload = bytes.subarray(3, bytes.length - 1);
    if (payload.length < 9) { finishReadWaiter(new Error('设备返回的读取数据长度无效。')); return; }
    const offset = u32(payload, 1);
    if (offset !== readWaiter.offset) return; // 迟到的上一块；等待当前 offset 的应答。
    finishReadWaiter(null, payload);
  }
  function requestName(channel, suffix = '') {
    if (!characteristic || !device?.gatt?.connected || transfer || readJob || nameWaiter || nameWriteInProgress)
      return Promise.reject(new Error('设备未连接或正在执行其他操作。'));
    const encoded = channel === NAME_SET ? new TextEncoder().encode(suffix) : new Uint8Array();
    const payload = channel === NAME_SET ? new Uint8Array(encoded.length + 1) : encoded;
    if (channel === NAME_SET) payload.set(encoded);
    return new Promise((resolve, reject) => {
      const waiter = { channel, resolve, reject, timer: null };
      nameWaiter = waiter;
      nameWriteInProgress = true;
      characteristic.writeValue(packet(channel, payload)).then(() => {
        nameWriteInProgress = false;
        if (nameWaiter === waiter) waiter.timer = setTimeout(() =>
          finishNameWaiter(new Error('读取或保存超时；设备可能需要 Ark 固件 3.2.5 或更新版本。')), 5000);
        updateNameControls();
      }, (error) => {
        nameWriteInProgress = false;
        if (nameWaiter === waiter) finishNameWaiter(error);
        updateNameControls();
      });
    });
  }
  async function loadName() {
    if (nameWaiter || nameWriteInProgress || transfer || readJob) {
      $('device-name-status').textContent = '设备正在执行其他操作，请稍候再读取名称。';
      return;
    }
    $('device-name-status').textContent = '正在读取设备名称…';
    try {
      const result = requestName(NAME_GET);
      updateNameControls();
      const name = await result;
      $('device-name-current').textContent = name;
      $('device-name-suffix').value = name.slice(DEVICE_NAME_PREFIX.length);
      $('device-name-status').textContent = '已读取设备当前配置。';
    } catch (error) { $('device-name-status').textContent = `读取失败：${error.message}`; }
    finally { updateNameControls(); }
  }
  async function saveName() {
    if (nameWaiter || nameWriteInProgress || transfer || readJob) {
      $('device-name-status').textContent = '设备正在执行其他操作，请稍候再保存名称。';
      return;
    }
    const suffix = $('device-name-suffix').value;
    const byteLength = new TextEncoder().encode(suffix).length;
    if (byteLength < 1 || byteLength > 16 || !suffix.trim() || /[\x00-\x1f\x7f-\x9f]/.test(suffix)) {
      $('device-name-status').textContent = '后缀须为 1–16 个 UTF-8 字节，不能全为空白或包含控制字符。';
      return;
    }
    $('device-name-status').textContent = '正在保存名称…';
    try {
      const result = requestName(NAME_SET, suffix);
      updateNameControls();
      const name = await result;
      $('device-name-current').textContent = name;
      $('device-name-status').textContent = `已保存 ${name}。重启设备后广播名称生效。`;
    } catch (error) { $('device-name-status').textContent = `保存失败：${error.message}`; }
    finally { updateNameControls(); }
  }
  function requestReadChunk(offset, count, job) {
    if (job.cancelled || !characteristic || !device?.gatt?.connected)
      return Promise.reject(new Error('读取已取消或设备已断开。'));
    if (readWaiter) return Promise.reject(new Error('上一块资料仍在读取。'));
    const request = packet(0x53, new Uint8Array([...be32(offset), count]));
    return new Promise((resolve, reject) => {
      const waiter = { offset, count, resolve, reject, timer: null };
      readWaiter = waiter;
      waiter.timer = setTimeout(() => finishReadWaiter(new Error('读取超时：设备可能需要支持 0x53 的新版 Ark 固件。当前草稿未改变。')), 7000);
      characteristic.writeValue(request).catch((error) => {
        if (readWaiter === waiter) finishReadWaiter(error);
      });
    });
  }
  async function readPassportBytes(transportCtx, job, onProgress) {
    let offset = 0, total = null, data = null, retries = 0;
    for (;;) {
      const payload = transportCtx
        ? await transportCtx.request(0x53, new Uint8Array([...be32(offset), 128]), 7000)
        : await requestReadChunk(offset, 128, job);
      if (job.cancelled) throw new Error('读取已取消。');
      const part = inspectReadPayload(payload, offset);
      if (part.status === 3 && retries < 12) {
        retries++;
        await pause(500);
        continue;
      }
      retries = 0;
      const chunk = parseReadPayload(payload, offset, 128);
      if (total === null) { total = chunk.total; data = new Uint8Array(total); }
      else if (chunk.total !== total) throw new Error('设备资料大小在读取期间发生变化，请重试。');
      data.set(chunk.data, offset);
      offset += chunk.data.length;
      if (onProgress) onProgress(offset, total);
      if (offset >= total) return data;
    }
  }
  async function readFromDevice() {
    if (!deviceAvailable() || transfer || readJob || editingLocked) return;
    if (!confirm('读取设备资料会在完整校验成功后切换至对应模板，并覆盖该模板的编辑内容和本地草稿；另一模板草稿保留。是否继续？')) return;
    if (!embedded && !prepareProfileOperation()) return;
    const job = { cancelled: false }; readJob = job;
    updateNameControls();
    setEditingLocked(true);
    if (!embedded) $('connect').disabled = true;
    $('send').disabled = true; $('read-device').disabled = true; $('cancel').disabled = false;
    $('progress').value = 0; setDeviceStatus('正在读取设备上的 profile.json；当前草稿会在完整校验后替换。');
    try {
      const readAll = async (transportCtx) => {
        const data = await readPassportBytes(transportCtx, job, (received, total) => {
          $('progress').value = total ? Math.round(received * 100 / total) : 100;
          setDeviceStatus(`正在读取 profile.json：${received} / ${total} 字节`);
        });
        const imported = parseProfile(new TextDecoder('utf-8', { fatal: true }).decode(data));
        const img = await imageFromDataUrl(imported.avatar);
        if (job.cancelled || readJob !== job || !(embedded ? getArkDevice()?.connected() : device?.gatt?.connected))
          throw new Error('读取已取消或设备已断开。');
        return { imported, img };
      };
      const result = embedded ? await getArkDevice().run('读取通行证', readAll) : await readAll(null);
      if (job.cancelled || readJob !== job || !(embedded ? getArkDevice()?.connected() : device?.gatt?.connected))
        throw new Error('读取已取消或设备已断开。');
      acceptImported(result.imported, result.img);
      setDeviceStatus('已从设备读取资料，切换至对应模板并替换其草稿；另一模板草稿已保留。');
    } catch (error) {
      setDeviceStatus(`${job.cancelled ? '读取已取消' : `读取失败：${error.message}`}。当前编辑资料和本地草稿已保留。`);
    } finally {
      if (readWaiter) finishReadWaiter(new Error('读取已结束。'));
      readJob = null; setEditingLocked(false);
      if (!embedded) $('connect').disabled = false;
      $('cancel').disabled = true; validate(); refreshDeviceControls();
      if (!embedded) updateNameControls();
    }
  }
  const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  function ensureTransfer(job) {
    if (transfer !== job || job.cancelled || !device?.gatt?.connected || !characteristic) throw new Error('传输已取消或设备已断开。');
  }
  async function write(channel, bytes, job, delay = 50) {
    ensureTransfer(job); await characteristic.writeValue(packet(channel, bytes));
    await pause(delay); ensureTransfer(job);
  }
  async function sendFile(name, bytes, job, base, total) {
    // FILE_NAME 使用 ASCII 路径并显式以 NUL 结尾；设备端会再补一个终止字节。
    const encodedName = new TextEncoder().encode(name);
    const nameBytes = new Uint8Array(encodedName.length + 1);
    nameBytes.set(encodedName);
    const len = bytes.length;
    await write(CH.start, new Uint8Array(), job);
    await write(CH.name, nameBytes, job);
    await write(CH.length, new Uint8Array([(len >>> 24) & 255, (len >>> 16) & 255, (len >>> 8) & 255, len & 255]), job);
    for (let i = 0; i < len; i += 192) {
      await write(CH.data, bytes.subarray(i, i + 192), job, 4);
      $('progress').value = Math.round((base + Math.min(i + 192, len)) * 100 / total);
      setDeviceStatus(`发送 ${name}：${Math.min(i + 192, len)} / ${len} 字节`);
    }
    await write(CH.stop, new Uint8Array(), job);
  }
  function disconnected() {
    if (characteristic) characteristic.removeEventListener('characteristicvaluechanged', onNotification);
    characteristic = null;
    if (capabilityWaiter) {
      const waiter = capabilityWaiter; capabilityWaiter = null; clearTimeout(waiter.timer);
      waiter.reject(new Error('设备已断开；未发送资料文件。'));
    }
    if (transfer) transfer.interrupted = true;
    finishReadWaiter(new Error('设备已断开。'));
    finishNameWaiter(new Error('设备已断开。'));
    updateNameControls();
    $('device-name-current').textContent = '未连接';
    $('send').disabled = true; $('read-device').disabled = true; $('cancel').disabled = true;
    $('connect').disabled = false;
    setDeviceStatus(transfer ? transferFailureText(transfer, transfer.cancelled ? '已取消' : '设备已断开') :
      readJob ? '设备已断开，读取中断；当前资料和本地草稿未改变。' : '设备已断开；请重新连接。');
  }
  function transferFailureText(job, reason) {
    if (!job.filesStarted) return `${reason}。资料文件未发送，原资料保留。`;
    if (job.jsonDone) return `${reason}。两个文件已发送，但未能完成回读校验。请在设备上按确认加载。`;
    return `${reason}。${job.binDone ? 'profile.bin 已发送，profile.json 未确认完成。' : 'profile.bin 未确认完成，profile.json 尚未发送。'} 请重新连接后从头发送。`;
  }
  function updateAvatarButtons() {
    const unavailable = editingLocked || !profile.avatar;
    $('edit-avatar').disabled = unavailable;
    $('remove-avatar').disabled = unavailable;
  }
  function setEditingLocked(locked) {
    editingLocked = locked;
    for (const id of [...fields, 'template-select', 'student-palette', 'student-output', 'codename-unset', 'number-unset', 'student-number-unset', 'avatar', 'avatar-mode', 'remove-avatar', 'edit-avatar',
      'import-json', 'export-json', 'download-png', 'download-bin']) $(id).disabled = locked;
    updateAvatarButtons();
    validate();
  }
  async function connect() {
    if (transfer || readJob) return;
    if (!navigator.bluetooth) { setDeviceStatus('浏览器不支持 Web Bluetooth。请使用受支持的浏览器与 localhost/HTTPS。'); return; }
    $('connect').disabled = true; setDeviceStatus('等待手动选择 FRAMEFILMARK…');
    try {
      const selected = await navigator.bluetooth.requestDevice({ filters: [{ namePrefix: 'FRAMEFILMARK' }], optionalServices: [SERVICE_UUID] });
      if (device) {
        device.removeEventListener('gattserverdisconnected', disconnected);
        if (characteristic) characteristic.removeEventListener('characteristicvaluechanged', onNotification);
        if (device !== selected && device.gatt.connected) device.gatt.disconnect();
      }
      device = selected; device.addEventListener('gattserverdisconnected', disconnected);
      const server = await device.gatt.connect();
      const nextCharacteristic = await (await server.getPrimaryService(SERVICE_UUID)).getCharacteristic(CHARACTERISTIC_UUID);
      await nextCharacteristic.startNotifications();
      nextCharacteristic.addEventListener('characteristicvaluechanged', onNotification);
      characteristic = nextCharacteristic;
      updateNameControls();
      setDeviceStatus(`已连接 ${device.name || 'FRAMEFILMARK'}，可以发送或读取。`);
      refreshDeviceControls();
      loadName();
    } catch (error) {
      characteristic = null;
      $('read-device').disabled = true;
      updateNameControls();
      setDeviceStatus(error.name === 'NotFoundError' ? '已取消设备选择；可再次连接。' : `连接失败或无法启用通知：${error.message}。可重试。`);
    } finally { $('connect').disabled = false; }
  }
  async function readPassCapabilities(transportCtx) {
    setDeviceStatus('正在查询设备通行证能力…');
    let data;
    try {
      if (transportCtx) data = await transportCtx.request(0x59, new Uint8Array(), 5000);
      else data = await new Promise((resolve, reject) => {
        const waiter = { resolve, reject, timer: null };
        capabilityWaiter = waiter;
        waiter.timer = setTimeout(() => {
          if (capabilityWaiter === waiter) { capabilityWaiter = null; reject(new Error('未响应')); }
        }, 5000);
        characteristic.writeValue(packet(0x59)).catch((error) => {
          if (capabilityWaiter === waiter) { capabilityWaiter = null; clearTimeout(waiter.timer); reject(error); }
        });
      });
    } catch (_) {
      throw new Error('设备未响应通行证能力查询，请升级固件');
    }
    if (transfer?.cancelled) throw new Error('操作已取消');
    return data.length === 5 && data[0] === 1 ? u32(data, 1) : 0;
  }
  function choosePassportBin(flags, fullscreen, i006, mode, bin) {
    if (i006 && (flags & CAP_I006_PASS)) return { bin, label: 'I006 512 色', fallback: false };
    if (fullscreen && !(flags & CAP_FULLSCREEN_PASS)) throw new Error('设备不支持全屏学生证，请升级固件');
    if (i006) {
      const fallbackMode = flags & CAP_COLOR_PASS ? 'color' : (mode === 'grayscale' ? 'grayscale' : 'mono');
      const pixels = g.getImageData(0, 0, STUDENT_W, STUDENT_H);
      return { bin: encodeStudentFfui(pixels, fallbackMode),
        label: fallbackMode === 'color' ? '六色 ColorFast' : '黑白 FFUI', fallback: true };
    }
    if (mode === 'color' && !(flags & CAP_COLOR_PASS)) throw new Error('设备不支持彩色通行证，请升级固件');
    return { bin, label: mode === 'color' ? '六色 ColorFast' : '黑白 FFUI', fallback: false };
  }
  async function verifyPassport(transportCtx, job, expected) {
    const expectedText = new TextDecoder().decode(expected);
    let lastError = '设备未返回资料';
    for (let attempt = 0; attempt < 5; attempt++) {
      if (job.cancelled) throw new Error('传输已取消。');
      try {
        await pause(attempt === 0 ? 750 : 500);
        const data = await readPassportBytes(transportCtx, job);
        if (new TextDecoder('utf-8', { fatal: true }).decode(data) === expectedText) return { ok: true, error: '' };
        lastError = '回读内容与发送内容不一致';
      } catch (error) {
        if (job.cancelled) throw new Error('传输已取消。');
        lastError = error.message;
        // 设备没有响应 0x53（旧固件）时重试无意义，立即结束回读。
        if (/未响应|超时/.test(lastError)) break;
      }
    }
    return { ok: false, error: lastError };
  }
  async function send() {
    if (editingLocked || !validate() || !deviceAvailable() || transfer || readJob) return;
    if (!embedded && !prepareProfileOperation()) return;
    const job = { cancelled: false, interrupted: false, binDone: false, jsonDone: false, filesStarted: false }; transfer = job;
    updateNameControls();
    setEditingLocked(true);
    $('send').disabled = true; $('read-device').disabled = true;
    if (!embedded) $('connect').disabled = true;
    $('cancel').disabled = false;
    $('progress').value = 0;
    const preferredBin = profileBin(), json = jsonBytes();
    const fullscreen = isStudent(), i006 = usesStudentI006(), mode = profile.avatarMode;
    const upload = async (transportCtx) => {
      const flags = fullscreen || mode === 'color' ? await readPassCapabilities(transportCtx) : 0;
      const { bin, label, fallback } = choosePassportBin(flags, fullscreen, i006, mode, preferredBin);
      const total = bin.length + json.length;
      const sendOne = (name, bytes, base) => transportCtx
        ? transportCtx.uploadFile(name, bytes, (sent) => {
          $('progress').value = Math.round((base + sent) * 100 / total);
          setDeviceStatus(`发送 ${name}：${sent} / ${bytes.length} 字节`);
        })
        : sendFile(name, bytes, job, base, total);
      job.filesStarted = true;
      await sendOne('app/pass/profile.bin', bin, 0);
      if (job.cancelled) throw new Error('传输已取消。');
      job.binDone = true;
      setDeviceStatus('profile.bin 已发送；正在发送 profile.json…');
      await sendOne('app/pass/profile.json', json, bin.length);
      if (job.cancelled) throw new Error('传输已取消。');
      job.jsonDone = true;
      setDeviceStatus('两个文件已发送；正在回读 profile.json 校验…');
      return { ...await verifyPassport(transportCtx, job, json), label, fallback };
    };
    try {
      setDeviceStatus('开始发送 profile.bin…');
      const verification = embedded ? await getArkDevice().run('发送通行证', upload) : await upload(null);
      if (job.cancelled) throw new Error('传输已取消。');
      const output = verification.fallback ? `设备不支持 I006，已改用${verification.label}。` : `已发送${verification.label}位图。`;
      setDeviceStatus(output + (verification.ok
        ? '两个文件已发送，回读一致。请在设备上按确认加载；屏幕位图仍待实体确认。'
        : `两个文件已发送，但回读未确认（${verification.error}）。请在设备上按确认加载。`));
    } catch (error) {
      const prefix = job.cancelled ? '已取消' : `发送中断：${error.message}`;
      setDeviceStatus(transferFailureText(job, prefix));
      if (!embedded && job.filesStarted) {
        if (device?.gatt?.connected) device.gatt.disconnect();
        characteristic = null;
      }
    } finally {
      transfer = null; setEditingLocked(false); $('cancel').disabled = true;
      if (!embedded) $('connect').disabled = false;
      validate(); refreshDeviceControls();
      if (!embedded) updateNameControls();
    }
  }
  async function init() {
    if (embedded) {
      document.body.classList.add('embedded');
      window.parent.addEventListener('ark-theme-changed', syncParentTheme);
      syncParentTheme();
      window.parent.addEventListener('ark-device-state', updateParentState);
      $('back-to-connect').addEventListener('click', () => {
        window.parent.document.querySelector('[data-page="bluetooth-page"]')?.click();
      });
    }
    setEditingLocked(true);
    const paletteSelect = $('student-palette');
    for (const [id, label] of studentPalettes) paletteSelect.add(new Option(label, id));
    syncForm();
    const templates = ['operator', STUDENT_TEMPLATE];
    const restored = await Promise.allSettled(templates.map(restoreDraft));
    profile = drafts.operator.profile; avatarImage = drafts.operator.image; avatarEdit = drafts.operator.edit;
    syncForm();
    setEditingLocked(false);
    if (restored.some((result) => result.status === 'rejected')) {
      setFileStatus('部分本地草稿无法读取（JSON 或头像无效）；未覆盖保存的草稿。请导入有效 JSON 或重新编辑。');
    }
    $('template-select').addEventListener('change', () => {
      if (editingLocked) { $('template-select').value = templateOf(profile); return; }
      const template = $('template-select').value;
      if (!templates.includes(template)) return;
      activateDraft(template);
      setFileStatus('已切换模板；两种模板的资料与头像构图分别保留。');
    });
    for (const id of fields) $(id).addEventListener('input', fromForm);
    for (const id of ['codename-unset', 'number-unset', 'student-number-unset']) $(id).addEventListener('change', fromForm);
    paletteSelect.addEventListener('change', () => {
      if (!isStudent() || editingLocked || !studentPalettes.has(paletteSelect.value)) {
        paletteSelect.value = profile.palette || DEFAULT_STUDENT_PALETTE;
        return;
      }
      profile.palette = paletteSelect.value;
      persist(); draw();
    });
    const outputSelect = $('student-output');
    outputSelect.addEventListener('change', () => {
      if (!isStudent() || editingLocked || ![DEFAULT_STUDENT_OUTPUT, 'ffui'].includes(outputSelect.value)) {
        outputSelect.value = profile.studentOutput || DEFAULT_STUDENT_OUTPUT;
        return;
      }
      profile.studentOutput = outputSelect.value;
      persist(); draw();
    });
    $('avatar-mode').addEventListener('change', changeAvatarMode);
    $('avatar').addEventListener('change', async (event) => {
      try { await avatarFromFile(event.target.files[0]); }
      catch (error) { setFileStatus(`头像处理失败：${error.message}`); }
      event.target.value = '';
    });
    $('edit-avatar').addEventListener('click',()=>editAvatar().catch(e=>setFileStatus(`头像处理失败：${e.message}`)));
    $('remove-avatar').addEventListener('click', () => { profile.avatar = null; avatarImage = null; avatarEdit = null; persist(); draw(); setFileStatus('头像已移除。'); });
    $('export-json').addEventListener('click', () => { download(new Blob([jsonBytes()], { type: 'application/json' }), 'profile.json'); setFileStatus('已导出编辑数据。'); });
    $('import-json').addEventListener('change', async (event) => {
      const file = event.target.files[0];
      if (!file || editingLocked) return;
      setEditingLocked(true);
      try {
        if (file.size > 350000) throw new Error('JSON 超过 350 KB。');
        const imported = parseProfile(await file.text());
        const img = await imageFromDataUrl(imported.avatar);
        acceptImported(imported, img);
        setFileStatus(valid ? '已导入 JSON 并切换至对应模板，可继续编辑。' : '已导入 JSON；请修正标红字段后再导出位图。');
      } catch (error) { setFileStatus(`导入失败：${error.message} 当前编辑资料和两种草稿均未改变。`); }
      finally { setEditingLocked(false); event.target.value = ''; }
    });
    $('download-png').addEventListener('click', () => { if (validate()) canvas.toBlob((blob) => { if (blob) download(blob, 'profile-preview.png'); }, 'image/png'); });
    $('download-bin').addEventListener('click', () => { if (validate()) download(new Blob([profileBin()], { type: 'application/octet-stream' }), 'profile.bin'); });
    $('connect').addEventListener('click', connect);
    $('send').addEventListener('click', send);
    $('read-device').addEventListener('click', readFromDevice);
    $('device-name-read').addEventListener('click', loadName);
    $('device-name-save').addEventListener('click', saveName);
    $('cancel').addEventListener('click', () => {
      if (!transfer && !readJob) return;
      if (transfer) transfer.cancelled = true;
      if (readJob) { readJob.cancelled = true; finishReadWaiter(new Error('读取已取消。')); }
      setDeviceStatus('正在取消；断开连接以终止当前操作。');
      if (embedded) getArkDevice()?.cancel();
      else if (device?.gatt?.connected) device.gatt.disconnect();
    });
    const jumpTo = (selector, delta) => {
      const target = document.querySelector(selector);
      if (!target) return;
      if (window.parent === window) {
        target.scrollIntoView({ behavior: 'smooth', block: 'start' });
        return;
      }
      try {
        const frame = window.frameElement;
        const scroller = window.parent.document.getElementById('page-content');
        const scrollable = scroller && scroller.scrollHeight > scroller.clientHeight;
        const scrollerTop = scrollable ? scroller.getBoundingClientRect().top : 0;
        const offset = frame.getBoundingClientRect().top + target.getBoundingClientRect().top - scrollerTop + delta;
        (scrollable ? scroller : window.parent).scrollBy({ top: offset, behavior: 'smooth' });
      } catch (_) {}
    };
    const jumpButton = $('preview-jump');
    if (jumpButton) jumpButton.addEventListener('click', () => jumpTo('.preview-panel', -12));
    const backButton = $('preview-back');
    if (backButton) backButton.addEventListener('click', () => jumpTo('.editor', -12));
    if (embedded) updateParentState();
  }
  init();
})();
