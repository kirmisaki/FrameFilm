package org.framefilm.ark

import android.os.Handler
import android.os.Looper
import java.io.File
import java.io.InputStream
import java.util.concurrent.TimeoutException
import kotlin.math.min

/** Legacy photo protocol only. GATT completion is NOT an SD save acknowledgement. */
class BleTransferCoordinator(
    private val ble: ArkBleController,
    private val file: File,
    private val fileName: String,
    private val listener: (TransferSnapshot) -> Unit,
    private val reconnect: () -> Unit,
) : TransferCoordinator {
    private val total = file.length()
    private val main = Handler(Looper.getMainLooper())
    private var active = false
    private var clean = true
    private var input: InputStream? = null
    private var received = 0L
    private var chunks = 0
    private var stopMayHaveBeenSent = false
    private var catalog = false
    private var absentBefore = false
    private var confirmed = false
    private var confirmationDismissed = false
    private var budget: Runnable? = null
    private var work: Runnable? = null
    private var verificationRound = 0

    override val isActive get() = active
    override val cleanupConfirmed get() = clean
    override val needsConfirmation get() = false // Read-only reconnection must remain possible.
    override val expectsBleResume get() = false

    private fun emit(phase: String, message: String, retry: Boolean = false) = listener(
        TransferSnapshot(phase, message, received, total,
            canCancel = active && !stopMayHaveBeenSent, canRetry = retry && !stopMayHaveBeenSent,
            cleanupCompleted = clean && !active, success = confirmed,
            canConfirm = !active && stopMayHaveBeenSent && !confirmed && !confirmationDismissed && catalog && absentBefore,
            transport = "ble", confirmation = if (confirmed) "directory_presence" else if (stopMayHaveBeenSent) "pending" else null))

    override fun start() {
        if (active) return
        val error = runCatching {
            BlePhotoFormat.requireCompatible(file)
            require(ble.isReady) { "请先连接 Ark" }
            require(fileName.matches(Regex("[A-Za-z0-9_-]+\\.film")) && fileName.length <= 50) { "BLE 照片名无效" }
            require(fileName.length + 1 <= payloadSize()) { "MTU 太小，不能发送完整照片名，请重新连接" }
        }.exceptionOrNull()
        if (error != null) { emit("error", error.message ?: "照片无效"); return }
        active = true
        clean = false
        emit("validating", "蓝牙兼容：核对屏幕、当前页面与 SD 目录；请保持 App 前台")
        val timeoutMs = 180_000L + ((total + payloadSize() - 1) / payloadSize()) * 150L
        budget = Runnable { fail("BLE 照片发送超过本次字节预算，请保持前台后重试") }
            .also { main.postDelayed(it, timeoutMs) }
        barrier {
            checkPage {
                ble.command(0x59) capability@{ result ->
                    if (!active) return@capability
                    val data = result.getOrNull()
                    val error = result.exceptionOrNull()
                    if (error != null && (error !is TimeoutException || !ble.isReady)) {
                        fail("读取能力失败：${error.message}"); return@capability
                    }
                    if (data != null && (data.size != 5 || data[0] != 1.toByte())) {
                        fail("设备能力响应无效"); return@capability
                    }
                    catalog = data != null && data[4].toInt() and 4 != 0
                    if (catalog) {
                        // A whole 5B reply (status/count/name/NUL) must fit one notification.
                        if (fileName.length + 6 > payloadSize()) {
                            fail("MTU 太小，不能完整核对照片目录，请重新连接"); return@capability
                        }
                        containsName { present ->
                            if (present) fail("设备已有同名照片 $fileName；为避免把旧文件当作保存成功，请重新导入后发送")
                            else { absentBefore = true; beginFile() }
                        }
                    } else beginFile()
                }
            }
        }
    }

    private fun payloadSize(): Int = min(192, ble.negotiatedMtu - 7).coerceAtLeast(1)

    private fun checkPage(done: () -> Unit) {
        ble.command(0x4C) { result ->
            if (!active) return@command
            val page = result.getOrNull()
            if (page == null || page.size != 1 || (page[0].toInt() and 255) !in 0..7) {
                fail("无法确认当前页面；不能将模板 RAM 显示误认为 SD 保存")
            } else if (page[0] == 1.toByte()) {
                fail("设备正在模板页面，请先手动离开模板后重试；App 不会自动切页")
            } else done()
        }
    }

    private fun barrier(done: () -> Unit) {
        ble.command(0x42, waitMs = 15_000) { result ->
            if (!active) return@command
            val data = result.getOrNull()
            if (data == null || !data.contentEquals(byteArrayOf(2, 2, 0xd0.toByte(), 1, 0xe0.toByte())))
                fail("BLE 屏障或 Ark 720×480 屏幕核对失败：${result.exceptionOrNull()?.message ?: "响应无效"}")
            else done()
        }
    }

    private fun beginFile() {
        input = runCatching { file.inputStream() }.getOrElse { fail("读取本地照片失败：${it.message}"); return }
        writePaced(0x03, byteArrayOf()) {
            writePaced(0x00, fileName.toByteArray(Charsets.US_ASCII) + byteArrayOf(0)) {
                writePaced(0x01, ArkCatalog.be32(total.toInt())) { sendChunk() }
            }
        }
    }

    private fun writePaced(channel: Int, data: ByteArray, done: () -> Unit) =
        write(channel, data) { schedule(50, done) }

    private fun write(channel: Int, data: ByteArray, done: () -> Unit) {
        if (!active) return
        ble.sendNoReply(channel, data) { result ->
            if (!active) return@sendNoReply
            if (result.isFailure) fail("BLE 发送失败：${result.exceptionOrNull()?.message}") else done()
        }
    }

    private fun sendChunk() {
        if (!active) return
        if (received == total) {
            input?.close(); input = null
            barrier { checkPage { submit() } }
            return
        }
        val buffer = ByteArray(min(payloadSize().toLong(), total - received).toInt())
        val count = runCatching { input!!.read(buffer) }.getOrElse { fail("读取照片失败：${it.message}"); return }
        if (count != buffer.size) { fail("本地照片在发送期间已变化或读取不完整"); return }
        write(0x02, buffer) {
            received += count
            chunks++
            emit("sending", "蓝牙正在发送 $fileName：$received / ${total} 字节（非落盘回执）")
            schedule(12) { if (chunks % 32 == 0) barrier { sendChunk() } else sendChunk() }
        }
    }

    private fun submit() {
        stopMayHaveBeenSent = true
        emit("verifying", "照片字节已发送，正在提交并核对 SD 保存；此时不能撤回或自动补发")
        writePaced(0x04, byteArrayOf()) {
            barrier {
                if (catalog && absentBefore) schedule(800) { verify() }
                else finishUnconfirmed("已发送，保存待确认：此固件不提供足够的只读目录证据，请手动核对；不自动重发")
            }
        }
    }

    private fun containsName(done: (Boolean) -> Unit) {
        fun read(index: Int, count: Long?) {
            if (!active) return
            ble.command(0x5B, byteArrayOf(1) + ArkCatalog.be32(index)) { result ->
                if (!active) return@command
                val page = runCatching { ArkCatalog.parse(result.getOrThrow(), index, count) }.getOrElse {
                    fail("无法核对 SD 目录：${it.message}"); return@command
                }
                if (page.name == null) done(false)
                else if (page.name == fileName) done(true)
                else read(index + 1, page.count)
            }
        }
        read(0, null)
    }

    private fun verify() {
        if (!active) return
        emit("verifying", "正在只读核对 SD 图片目录中的新文件 $fileName")
        containsName { present ->
            if (present) {
                confirmed = true
                finish("done", "SD 目录已出现新照片 $fileName；仅确认存在，未校验文件字节或实屏显示")
            } else if (++verificationRound < 4) schedule(1000) { verify() }
            else finishUnconfirmed("已发送，SD 目录尚未出现新照片；保存待确认，不自动重发")
        }
    }

    private fun schedule(delay: Long, action: () -> Unit) {
        work?.let(main::removeCallbacks)
        work = Runnable { work = null; if (active) action() }.also { main.postDelayed(it, delay) }
    }

    /** Receives valid legacy busy replies without pretending legacy writes have business ACKs. */
    fun onPacket(packet: ByteArray) {
        if (!active || packet.size != 5) return
        if ((packet[1].toInt() and 255) in 0x00..0x04 && packet[3] == 1.toByte()) fail("设备拒绝照片传输：当前传输通道忙")
    }

    private fun clearWork() {
        input?.close(); input = null
        budget?.let(main::removeCallbacks); budget = null
        work?.let(main::removeCallbacks); work = null
    }

    private fun finish(phase: String, message: String) {
        active = false
        clearWork()
        clean = true
        emit(phase, message)
    }

    private fun finishUnconfirmed(message: String) = finish("unconfirmed", message)

    private fun fail(message: String, cancelled: Boolean = false) {
        if (!active) return
        active = false // Fence callbacks before closing GATT.
        clearWork()
        clean = false
        ble.close()
        emit(if (stopMayHaveBeenSent) "unconfirmed" else if (cancelled) "cancelled" else "error",
            if (stopMayHaveBeenSent) "$message；提交可能已发生，只能核对结果，不可自动重发"
            else if (!clean) "$message；已停止发送，请重连同一设备确认断连清理后再重试" else message,
            retry = !stopMayHaveBeenSent)
        // Reconnect starts a fresh GATT generation; no stale callbacks may advance this attempt.
        main.postDelayed({ reconnect() }, 1000)
    }

    override fun cancel() {
        if (!stopMayHaveBeenSent) fail("已取消蓝牙照片发送", cancelled = true)
    }
    override fun onBleDisconnected() { if (active) fail("Ark 蓝牙已断开") }
    override fun onBleReady() { }
    override fun ensureSettled(callback: (Boolean, String) -> Unit) {
        if (!active && ble.isReady && !ble.isBusy) clean = true
        callback(clean && !active, if (clean && !active) "" else "请重连原 Ark，确认上一轮 BLE 照片已断连清理")
    }
    fun dismissConfirmation() {
        if (active || confirmed || !stopMayHaveBeenSent) return
        confirmationDismissed = true
        emit("unconfirmed", "已保留本地照片并退出结果核对；设备仍可能已保存，不会重发同一缓存。重新导入是一次新的明确发送操作")
    }
    override fun confirmFirmware() {
        // Common session confirmation entry point; this path only reads the photo directory.
        if (active || confirmed || confirmationDismissed || !stopMayHaveBeenSent || !catalog || !absentBefore || !ble.isReady || ble.isBusy) return
        active = true; clean = false; verificationRound = 0
        budget = Runnable { fail("照片目录核对超时") }.also { main.postDelayed(it, 30_000) }
        verify()
    }
}
