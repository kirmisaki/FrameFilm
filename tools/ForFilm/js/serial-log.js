/* ForFilm 设备终端：Web Serial 串口日志源
 *
 * 与蓝牙日志共用 #device-log-view / appendDeviceLog()。
 * 串口只读；打开后持续读取，断开或页面卸载时释放端口。
 */
(function (root) {
    'use strict';

    var port = null;
    var reader = null;
    var reading = false;
    var decoder = new TextDecoder('utf-8');
    var pending = '';

    function $(id) { return document.getElementById(id); }

    function supported() {
        return !!(navigator.serial && navigator.serial.requestPort);
    }

    function append(text) {
        if (typeof root.appendDeviceLog === 'function') root.appendDeviceLog(text);
    }

    function setStatus(text, error) {
        var el = $('serial-log-status');
        if (!el) return;
        el.textContent = text;
        el.classList.toggle('is-error', !!error);
    }

    function syncUI() {
        var btn = $('serial-log-toggle');
        var sel = $('serial-log-baud');
        if (btn) btn.textContent = port ? '断开串口' : '连接串口';
        if (sel) sel.disabled = !!port;
    }

    function flushPending(force) {
        if (!pending) return;
        var parts = pending.split(/\r?\n/);
        pending = parts.pop();
        if (parts.length) append(parts.join('\n'));
        if (force && pending) {
            append(pending);
            pending = '';
        } else if (pending.length > 8192) {
            append(pending);
            pending = '';
        }
    }

    async function readLoop() {
        reading = true;
        while (port && reading) {
            try {
                reader = port.readable.getReader();
                while (true) {
                    var r = await reader.read();
                    if (r.done) break;
                    if (!r.value || !r.value.length) continue;
                    pending += decoder.decode(r.value, { stream: true });
                    flushPending(false);
                }
            } catch (e) {
                if (reading) append('[串口读取中断] ' + e.message);
            } finally {
                if (reader) {
                    try { reader.releaseLock(); } catch (_) { /* 已释放 */ }
                    reader = null;
                }
            }
            if (!port || !reading) break;
        }
        if (port) await disconnect();
    }

    async function connect() {
        if (port) { await disconnect(); return; }
        if (!supported()) {
            setStatus('浏览器不支持 Web Serial', true);
            return;
        }

        var selected = null;
        try {
            selected = await navigator.serial.requestPort();
            var baud = parseInt(($('serial-log-baud') || {}).value, 10) || 115200;
            await selected.open({ baudRate: baud });
            port = selected;
            reading = true;
            pending = '';
            decoder = new TextDecoder('utf-8');
            syncUI();
            setStatus('串口已连接 ' + baud);
            append('[串口已连接 ' + baud + '，等待设备日志...]');
            readLoop();
        } catch (e) {
            append('[串口打开失败] ' + e.message);
            setStatus('串口打开失败', true);
            if (selected) {
                try { await selected.close(); } catch (_) { /* 可能未打开 */ }
            }
            port = null;
            syncUI();
        }
    }

    async function disconnect() {
        reading = false;
        var active = port;
        port = null;
        try {
            if (reader) await reader.cancel();
        } catch (_) { /* 端口可能已断开 */ }
        try {
            if (active) await active.close();
        } catch (_) { /* 端口可能已断开 */ }
        reader = null;
        flushPending(true);
        syncUI();
        setStatus('串口未连接');
        append('[串口已断开]');
    }

    function init() {
        var btn = $('serial-log-toggle');
        if (btn) {
            btn.addEventListener('click', function () {
                if (port) disconnect();
                else connect();
            });
        }
        if (!supported()) {
            if (btn) {
                btn.disabled = true;
                btn.title = '当前浏览器不支持 Web Serial';
            }
            setStatus('浏览器不支持 Web Serial', true);
        }
        syncUI();
        root.addEventListener('beforeunload', function () {
            if (port) {
                try { port.close(); } catch (_) { /* 页面卸载 */ }
            }
        });
    }

    root.addEventListener('DOMContentLoaded', init);
})(window);
