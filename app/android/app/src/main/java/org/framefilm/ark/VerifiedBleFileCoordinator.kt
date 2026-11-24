package org.framefilm.ark

import android.os.Handler
import android.os.Looper
import java.io.DataInputStream
import java.io.File

/** Acknowledged Ark SD upload. Each successful DATA reply reports the bytes the device accepted. */
internal class VerifiedBleFileCoordinator(
    private val ble: ArkBleController,
    private val file: File,
    private val path: String,
    private val catalogDir: Int?,
    private val requiresAbc9: Boolean,
    private val listener: (TransferSnapshot) -> Unit,
    private val reconnect: () -> Unit,
) : TransferCoordinator {
    private val main = Handler(Looper.getMainLooper())
    private val total = file.length()
    private val name = path.substringAfterLast('/')
    // Media basenames use the firmware's frame-count routing and refresh the
    // active gallery. Passport files must retain their exact explicit paths.
    private val pathBytes = (if (catalogDir == null) path else name)
        .toByteArray(Charsets.UTF_8) + byteArrayOf(0)
    private var started = false
    private var active = false
    private var clean = true
    private var staging = false
    private var stopSubmitted = false
    private var stopped = false
    private var confirmed = false
    private var dismissRequested = false
    private var cancelRequested = false
    private var wasAbsent = false
    private var received = 0L
    private var reported = 0L
    private var input: DataInputStream? = null
    private var work: Runnable? = null
    private var deadline: Runnable? = null

    override val isActive get() = active
    override val cleanupConfirmed get() = clean && !active
    override val needsConfirmation get() = false // Media can reconnect for read-only catalog confirmation.
    override val expectsBleResume get() = false

    private fun maxPayload() = minOf(192, ble.negotiatedMtu - 7)

    private fun emit(phase: String, detail: String, retry: Boolean = false) {
        listener(TransferSnapshot(phase, detail, received, total,
            canCancel = active && !cancelRequested && !stopSubmitted,
            canRetry = retry && !stopSubmitted,
            cleanupCompleted = cleanupConfirmed, success = confirmed,
            canConfirm = !active && stopSubmitted && !confirmed && !dismissRequested && wasAbsent,
            transport = "ble", confirmation = when {
                confirmed && stopped -> "sd_commit"
                confirmed -> "directory_presence"
                stopSubmitted -> "pending"
                else -> null
            }))
    }

    override fun start() {
        if (started) return
        started = true
        val error = runCatching {
            require(ble.isReady && !ble.isBusy) { "请先连接 Ark 并等待蓝牙空闲" }
            require(file.isFile && total in 1..0xffffffffL) { "本地文件不存在或大小无效" }
            require(FileTransferPath.isValid(path) && pathBytes.size <= maxPayload()) {
                "SD 目标路径无效或超过当前 BLE MTU"
            }
        }.exceptionOrNull()
        if (error != null) { emit("error", error.message ?: "文件无法发送"); return }
        active = true
        val timeout = 180_000L + ((total + maxPayload() - 1L) / maxPayload()) * 150L
        setDeadline(timeout, "BLE 文件发送超时")
        emit("validating", "正在核对 Ark 蓝牙 SD 上传能力")
        query(0x59) { capabilities ->
            if (!TransferPolicy.supportsVerifiedBleFile(capabilities)) fail("当前固件不支持带回执的 BLE SD 上传")
            else if (requiresAbc9 && !Abc9Preview.supported(capabilities)) fail("当前 Ark 不支持 ABC9 图片")
            else if (catalogDir == null) begin()
            else if ((capabilities[4].toInt() and 4) == 0) fail("当前固件没有可用的 SD 目录查询")
            else containsName { present ->
                if (present) fail("SD 卡已有同名文件 $name，请重新导入后发送")
                else { wasAbsent = true; begin() }
            }
        }
    }

    private fun query(channel: Int, payload: ByteArray = byteArrayOf(), waitMs: Long = 10_000,
                      done: (ByteArray) -> Unit) {
        if (!active) return
        if (!ble.isReady) { fail("Ark 蓝牙已断开"); return }
        ble.command(channel, payload, waitMs) { result ->
            if (!active) return@command
            result.fold(done) { fail("蓝牙命令 0x${channel.toString(16)} 未确认：${it.message}") }
        }
    }

    private fun acknowledged(channel: Int, payload: ByteArray = byteArrayOf(), expected: Long? = null,
                             waitMs: Long = 10_000, done: () -> Unit) {
        query(channel, payload, waitMs) { data ->
            if (data.size != 5) { fail("设备返回的 SD 回执格式无效"); return@query }
            val status = data[0].toInt() and 255
            if (status != 0) {
                fail(when (status) {
                    1 -> "设备正忙或 SD 上传状态已变化"
                    2 -> "设备拒绝文件路径、长度或分块"
                    3 -> "SD 文件写入或提交失败"
                    else -> "未知 SD 上传错误 $status"
                })
                return@query
            }
            val count = (1..4).fold(0L) { acc, i -> (acc shl 8) or (data[i].toLong() and 255) }
            if (expected != null && count != expected) {
                fail("设备确认的文件长度 $count 与预期 $expected 不符"); return@query
            }
            if (cancelRequested && !stopSubmitted && channel != START_CANCEL) cancelStaging() else done()
        }
    }

    private fun begin() {
        input = runCatching { DataInputStream(file.inputStream().buffered()) }.getOrElse {
            fail("读取本地文件失败：${it.message}"); return
        }
        staging = true
        acknowledged(START_CANCEL, byteArrayOf(1), 0) {
            if (cancelRequested) cancelStaging()
            else acknowledged(0x00, pathBytes, 0) {
                acknowledged(0x01, ArkCatalog.be32(total.toInt()), 0) { sendChunk() }
            }
        }
    }

    private fun sendChunk() {
        if (!active) return
        if (received == total) { submit(); return }
        if (maxPayload() < 1) { fail("当前 BLE MTU 不足"); return }
        val chunk = ByteArray(minOf(maxPayload().toLong(), total - received).toInt())
        runCatching { checkNotNull(input).readFully(chunk) }.getOrElse {
            fail("本地文件读取不完整"); return
        }
        acknowledged(0x02, chunk, received + chunk.size) {
            received += chunk.size
            if (reported == 0L || received == total || received - reported >= maxOf(4096, total / 100)) {
                reported = received
                emit("sending", "设备已确认接收 $received / $total 字节")
            }
            if (!cancelRequested) schedule(0, ::sendChunk)
        }
    }

    private fun submit() {
        stopSubmitted = true
        emit("verifying", "正在提交 SD 文件")
        val silent = if (catalogDir == 1) 0 else 1
        acknowledged(0x04, byteArrayOf(silent.toByte()), total, 30_000) {
            staging = false
            stopped = true
            if (catalogDir == null) complete("设备已确认保存 $path")
            else verifyCatalog()
        }
    }

    private fun containsName(done: (Boolean) -> Unit) {
        val dir = checkNotNull(catalogDir)
        fun read(index: Int, count: Long?) {
            query(0x5B, byteArrayOf(dir.toByte()) + ArkCatalog.be32(index)) { data ->
                val page = runCatching { ArkCatalog.parse(data, index, count) }.getOrElse {
                    fail("SD 目录核对失败：${it.message}"); return@query
                }
                when {
                    page.name == null -> done(false)
                    page.name.equals(name, ignoreCase = true) -> done(true)
                    else -> schedule(0) { read(index + 1, page.count) }
                }
            }
        }
        read(0, null)
    }

    private fun verifyCatalog() {
        emit("verifying", "正在核对 SD 目录中的 $name")
        containsName { present ->
            if (present) complete("SD 目录已确认 $path")
            else finish("unconfirmed", "SD 目录暂未找到 $path，请稍后重新核对")
        }
    }

    private fun complete(message: String) {
        confirmed = true
        finish("done", message)
    }

    private fun schedule(delay: Long, action: () -> Unit) {
        work?.let(main::removeCallbacks)
        work = Runnable { work = null; if (active) action() }.also { main.postDelayed(it, delay) }
    }

    private fun setDeadline(delay: Long, message: String) {
        deadline?.let(main::removeCallbacks)
        deadline = Runnable { if (active) fail(message) }.also { main.postDelayed(it, delay) }
    }

    private fun clearWork() {
        runCatching { input?.close() }; input = null
        work?.let(main::removeCallbacks); work = null
        deadline?.let(main::removeCallbacks); deadline = null
    }

    private fun finish(phase: String, detail: String) {
        active = false
        clean = true
        clearWork()
        emit(phase, detail, retry = phase == "cancelled")
    }

    private fun fail(detail: String) {
        if (!active) return
        active = false
        clearWork()
        clean = !staging
        if (staging || ble.isBusy) ble.close()
        emit(if (stopSubmitted) "unconfirmed" else if (cancelRequested) "cancelled" else "error",
            when {
                stopSubmitted -> "$detail；文件提交结果待核对，请勿重复发送"
                staging -> "$detail；请重连原 Ark 完成暂存清理"
                else -> detail
            }, retry = !stopSubmitted)
        if (!clean) main.postDelayed(reconnect, 1000)
    }

    private fun cancelStaging() {
        work?.let(main::removeCallbacks); work = null
        acknowledged(START_CANCEL, byteArrayOf(2)) {
            staging = false
            finish("cancelled", "设备已清理未提交的 SD 暂存")
        }
    }

    override fun cancel() {
        if (!active || stopSubmitted || cancelRequested) return
        cancelRequested = true
        if (!staging) fail("已取消 BLE 文件发送")
        else if (!ble.isBusy) cancelStaging()
        else emit("cancelling", "等待当前分块回执后取消暂存")
    }

    override fun onBleDisconnected() { if (active) fail("Ark 蓝牙已断开") }
    override fun onBleReady() { }
    override fun ensureSettled(callback: (Boolean, String) -> Unit) {
        if (!active && ble.isReady && !ble.isBusy) {
            staging = false
            clean = true
        }
        callback(cleanupConfirmed, if (cleanupConfirmed) "" else "请重连原 Ark 确认 SD 暂存清理")
    }

    fun dismissConfirmation() {
        if (active || confirmed || !stopSubmitted || !wasAbsent) return
        dismissRequested = true
        emit("unconfirmed", "已退出目录核对，设备上的文件可能已保存")
    }

    override fun confirmFirmware() {
        if (active || !stopSubmitted || confirmed || dismissRequested || !wasAbsent || !ble.isReady || ble.isBusy) return
        active = true
        setDeadline(30_000, "SD 目录核对超时")
        verifyCatalog()
    }

    private companion object { const val START_CANCEL = 0x03 }
}
