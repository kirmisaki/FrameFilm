// 键值设置：屏幕键盘弹窗
//
// 用于设置设备按键（单击/双击/长按）对应的 PC 键盘键值，
// 支持单键与组合键（修饰键可多选，普通键最多 6 个）。
// 协议收发函数位于 bluetooth.js，本文件只负责界面与状态。

var KB_EVENT_NAMES = ['单击', '双击', '长按'];

// 修饰键位（对应 HID 键盘报告的第一个字节）
var KB_MODIFIERS = [
    { label: 'Ctrl', bit: 0x01 },
    { label: 'Shift', bit: 0x02 },
    { label: 'Alt', bit: 0x04 },
    { label: 'Win', bit: 0x08 }
];

// 屏幕键盘布局：{ l: 显示文本, c: HID Usage ID } 或 { l: 显示文本, mod: 修饰键位 }
var KB_LAYOUT = [
    [
        { l: 'Esc', c: 0x29 }, { l: 'F1', c: 0x3A }, { l: 'F2', c: 0x3B }, { l: 'F3', c: 0x3C },
        { l: 'F4', c: 0x3D }, { l: 'F5', c: 0x3E }, { l: 'F6', c: 0x3F }, { l: 'F7', c: 0x40 },
        { l: 'F8', c: 0x41 }, { l: 'F9', c: 0x42 }, { l: 'F10', c: 0x43 }, { l: 'F11', c: 0x44 },
        { l: 'F12', c: 0x45 }
    ],
    [
        { l: '`', c: 0x35 }, { l: '1', c: 0x1E }, { l: '2', c: 0x1F }, { l: '3', c: 0x20 },
        { l: '4', c: 0x21 }, { l: '5', c: 0x22 }, { l: '6', c: 0x23 }, { l: '7', c: 0x24 },
        { l: '8', c: 0x25 }, { l: '9', c: 0x26 }, { l: '0', c: 0x27 }, { l: '-', c: 0x2D },
        { l: '=', c: 0x2E }, { l: 'Backspace', c: 0x2A }
    ],
    [
        { l: 'Tab', c: 0x2B }, { l: 'Q', c: 0x14 }, { l: 'W', c: 0x1A }, { l: 'E', c: 0x08 },
        { l: 'R', c: 0x15 }, { l: 'T', c: 0x17 }, { l: 'Y', c: 0x1C }, { l: 'U', c: 0x18 },
        { l: 'I', c: 0x0C }, { l: 'O', c: 0x12 }, { l: 'P', c: 0x13 }, { l: '[', c: 0x2F },
        { l: ']', c: 0x30 }, { l: '\\', c: 0x31 }
    ],
    [
        { l: 'Caps', c: 0x39 }, { l: 'A', c: 0x04 }, { l: 'S', c: 0x16 }, { l: 'D', c: 0x07 },
        { l: 'F', c: 0x09 }, { l: 'G', c: 0x0A }, { l: 'H', c: 0x0B }, { l: 'J', c: 0x0D },
        { l: 'K', c: 0x0E }, { l: 'L', c: 0x0F }, { l: ';', c: 0x33 }, { l: '\'', c: 0x34 },
        { l: 'Enter', c: 0x28 }
    ],
    [
        { l: 'Z', c: 0x1D }, { l: 'X', c: 0x1B }, { l: 'C', c: 0x06 }, { l: 'V', c: 0x19 },
        { l: 'B', c: 0x05 }, { l: 'N', c: 0x11 }, { l: 'M', c: 0x10 }, { l: ',', c: 0x36 },
        { l: '.', c: 0x37 }, { l: '/', c: 0x38 }, { l: 'Ins', c: 0x49 }, { l: 'Del', c: 0x4C },
        { l: 'Home', c: 0x4A }, { l: 'End', c: 0x4D }
    ],
    [
        { l: 'Ctrl', mod: 0x01 }, { l: 'Shift', mod: 0x02 }, { l: 'Alt', mod: 0x04 },
        { l: 'Win', mod: 0x08 }, { l: 'Space', c: 0x2C, w: 1 }
    ],
    [
        { l: 'PgUp', c: 0x4B }, { l: 'PgDn', c: 0x4E }, { l: '←', c: 0x50 },
        { l: '↑', c: 0x52 }, { l: '↓', c: 0x51 }, { l: '→', c: 0x4F }
    ]
];

// HID Usage ID -> 显示文本
var KB_LABELS = (function() {
    var map = {};
    for (var r = 0; r < KB_LAYOUT.length; r++) {
        var row = KB_LAYOUT[r];
        for (var k = 0; k < row.length; k++) {
            if (typeof row[k].c === 'number') {
                map[row[k].c] = row[k].l;
            }
        }
    }
    return map;
})();

var kbState = {
    event: 0,          // 当前编辑的事件（0 单击 / 1 双击 / 2 长按）
    editing: [createEmptyKeyCombo(), createEmptyKeyCombo(), createEmptyKeyCombo()],
    device: [null, null, null], // 从设备读回的三组键值
    dirty: false       // 编辑区是否已被用户改动
};

function createEmptyKeyCombo() {
    return { modifier: 0, keycodes: [] };
}

// ==================== 界面 ====================

function initKeyboardSetting() {
    renderKeyboardBoard();
    renderKeyboardSummary();
    syncKeyboardAvailability();

    var modal = document.getElementById('keyboard-modal');
    if (modal) {
        document.addEventListener('keydown', function(e) {
            if (e.key === 'Escape' && modal.classList.contains('show')) {
                closeKeyboardSetting();
            }
        });
    }
}

// 按键键值设置仅支持机型（FrameFilm Dock）可见，切换机型时同步显隐
function syncKeyboardAvailability() {
    var cfg = getDeviceConfig();
    var supported = !!(cfg && cfg.hasKeyboard);

    var section = document.getElementById('keyboard-settings-section');
    if (section) {
        section.style.display = supported ? '' : 'none';
    }

    // 切换到不支持的机型时关闭弹窗
    if (!supported) {
        closeKeyboardSetting();
    }
}

function renderKeyboardBoard() {
    var board = document.getElementById('kb-board');
    if (!board) return;

    var html = '';
    for (var r = 0; r < KB_LAYOUT.length; r++) {
        html += '<div class="kb-row">';
        var row = KB_LAYOUT[r];
        for (var k = 0; k < row.length; k++) {
            var key = row[k];
            var cls = 'kb-key';
            var attr;
            if (typeof key.mod === 'number') {
                cls += ' kb-key-mod';
                attr = ' data-mod="' + key.mod + '"';
            } else {
                attr = ' data-code="' + key.c + '"';
            }
            if (key.w) cls += ' kb-key-space';
            html += '<button type="button" class="' + cls + '"' + attr + '>' + key.l + '</button>';
        }
        html += '</div>';
    }
    board.innerHTML = html;
    board.onclick = onKeyboardBoardClick;
}

function onKeyboardBoardClick(e) {
    var btn = e.target && e.target.closest ? e.target.closest('.kb-key') : null;
    if (!btn) return;

    var modAttr = btn.getAttribute('data-mod');
    if (modAttr !== null) {
        toggleKeyboardModifier(parseInt(modAttr, 10));
        return;
    }

    var codeAttr = btn.getAttribute('data-code');
    if (codeAttr !== null) {
        toggleKeyboardKey(parseInt(codeAttr, 10));
    }
}

function toggleKeyboardModifier(bit) {
    var cur = kbState.editing[kbState.event];
    cur.modifier = (cur.modifier ^ bit) & 0xFF;
    kbState.dirty = true;
    syncKeyboardBoardActive();
    renderKeyboardChips();
}

function toggleKeyboardKey(code) {
    var cur = kbState.editing[kbState.event];
    var idx = cur.keycodes.indexOf(code);
    if (idx !== -1) {
        cur.keycodes.splice(idx, 1);
    } else {
        if (cur.keycodes.length >= 6) {
            showMessage('最多同时设置 6 个按键', 'error');
            return;
        }
        cur.keycodes.push(code);
    }
    kbState.dirty = true;
    syncKeyboardBoardActive();
    renderKeyboardChips();
}

function clearKeyboardSelection() {
    kbState.editing[kbState.event] = createEmptyKeyCombo();
    kbState.dirty = true;
    syncKeyboardBoardActive();
    renderKeyboardChips();
}

function syncKeyboardBoardActive() {
    var cur = kbState.editing[kbState.event];
    var keys = document.querySelectorAll('#kb-board .kb-key');
    for (var i = 0; i < keys.length; i++) {
        var el = keys[i];
        var active = false;
        var modAttr = el.getAttribute('data-mod');
        if (modAttr !== null) {
            active = (cur.modifier & parseInt(modAttr, 10)) !== 0;
        } else {
            active = cur.keycodes.indexOf(parseInt(el.getAttribute('data-code'), 10)) !== -1;
        }
        if (active) {
            el.classList.add('active');
        } else {
            el.classList.remove('active');
        }
    }
}

function renderKeyboardChips() {
    var el = document.getElementById('kb-current-chips');
    if (!el) return;

    var cur = kbState.editing[kbState.event];
    var names = keyComboToNames(cur.modifier, cur.keycodes);
    if (names.length === 0) {
        el.innerHTML = '<span class="kb-chip-empty">未设置</span>';
        return;
    }

    var html = '';
    for (var i = 0; i < names.length; i++) {
        html += '<span class="kb-chip">' + names[i] + '</span>';
    }
    el.innerHTML = html;
}

function syncKeyboardTabs() {
    var tabs = document.querySelectorAll('#kb-event-tabs .kb-event-tab');
    for (var i = 0; i < tabs.length; i++) {
        var idx = parseInt(tabs[i].getAttribute('data-event'), 10);
        if (idx === kbState.event) {
            tabs[i].classList.add('active');
        } else {
            tabs[i].classList.remove('active');
        }
    }
}

function renderKeyboardSummary() {
    var el = document.getElementById('keyboard-key-summary');
    if (!el) return;

    var html = '';
    for (var i = 0; i < KB_EVENT_NAMES.length; i++) {
        var group = kbState.device[i] || kbState.editing[i];
        html += '<div class="kbs-row"><span class="kbs-name">' + KB_EVENT_NAMES[i] +
                '</span><span class="kbs-val">' + formatKeyCombo(group.modifier, group.keycodes) + '</span></div>';
    }
    el.innerHTML = html;
}

// ==================== 交互 ====================

function openKeyboardSetting() {
    var modal = document.getElementById('keyboard-modal');
    if (!modal) return;

    kbState.event = 0;
    kbState.dirty = false;
    for (var i = 0; i < 3; i++) {
        var group = kbState.device[i];
        kbState.editing[i] = group
            ? { modifier: group.modifier, keycodes: group.keycodes.slice() }
            : createEmptyKeyCombo();
    }

    syncKeyboardTabs();
    syncKeyboardBoardActive();
    renderKeyboardChips();
    modal.classList.add('show');

    // 打开时主动回读，保证与设备一致
    readKeyboardSetting();
}

function closeKeyboardSetting() {
    var modal = document.getElementById('keyboard-modal');
    if (modal) modal.classList.remove('show');
}

function switchKeyboardEvent(idx) {
    if (idx < 0 || idx >= KB_EVENT_NAMES.length) return;
    kbState.event = idx;
    syncKeyboardTabs();
    syncKeyboardBoardActive();
    renderKeyboardChips();
}

function readKeyboardSetting() {
    if (typeof device === 'undefined' || !device || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }
    sendBleKeyboardKeyGet();
}

async function saveKeyboardSetting() {
    if (typeof device === 'undefined' || !device || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    // 收集需要下发的组：编辑后有内容的组，以及原本有值但被清空的组（下发空值以清除）
    var pending = [];
    for (var i = 0; i < KB_EVENT_NAMES.length; i++) {
        var editEmpty = isKeyComboEmpty(kbState.editing[i]);
        var devEmpty = isKeyComboEmpty(kbState.device[i]);
        if (!editEmpty || !devEmpty) {
            pending.push(i);
        }
    }

    if (pending.length === 0) {
        showMessage('请先选择按键', 'error');
        return;
    }

    try {
        var sentNames = [];
        for (var k = 0; k < pending.length; k++) {
            var idx = pending[k];
            var cur = kbState.editing[idx];
            await sendBleKeyboardKeySet(idx, cur.modifier, cur.keycodes);
            kbState.device[idx] = { modifier: cur.modifier, keycodes: cur.keycodes.slice() };
            sentNames.push(KB_EVENT_NAMES[idx]);
        }
        kbState.dirty = false;
        renderKeyboardSummary();
        showMessage(sentNames.join('/') + ' 键值已下发', 'success');
    } catch (error) {
        showMessage('键值下发失败: ' + error.message, 'error');
    }
}

// 由 bluetooth.js 收到 KEYBOARD_KEY_GET 应答后回调
function onKeyboardKeyReceived(groups) {
    if (!groups || groups.length === 0) return;

    for (var i = 0; i < groups.length && i < 3; i++) {
        kbState.device[i] = {
            modifier: groups[i].modifier,
            keycodes: groups[i].keycodes.slice()
        };
    }
    renderKeyboardSummary();

    // 弹窗打开且用户尚未改动时，同步到编辑区
    var modal = document.getElementById('keyboard-modal');
    if (modal && modal.classList.contains('show') && !kbState.dirty) {
        for (var j = 0; j < 3; j++) {
            var group = kbState.device[j];
            kbState.editing[j] = group
                ? { modifier: group.modifier, keycodes: group.keycodes.slice() }
                : createEmptyKeyCombo();
        }
        syncKeyboardTabs();
        syncKeyboardBoardActive();
        renderKeyboardChips();
    }
}

// ==================== 工具 ====================

// 判断一组键值是否为空（无修饰键且无键码）
function isKeyComboEmpty(combo) {
    return !combo || (combo.modifier === 0 && (!combo.keycodes || combo.keycodes.length === 0));
}

function keyComboToNames(modifier, keycodes) {
    var names = [];
    for (var i = 0; i < KB_MODIFIERS.length; i++) {
        if (modifier & KB_MODIFIERS[i].bit) {
            names.push(KB_MODIFIERS[i].label);
        }
    }
    for (var j = 0; keycodes && j < keycodes.length; j++) {
        names.push(KB_LABELS[keycodes[j]] || ('0x' + keycodes[j].toString(16)));
    }
    return names;
}

function formatKeyCombo(modifier, keycodes) {
    var names = keyComboToNames(modifier, keycodes);
    return names.length ? names.join(' + ') : '未设置';
}
