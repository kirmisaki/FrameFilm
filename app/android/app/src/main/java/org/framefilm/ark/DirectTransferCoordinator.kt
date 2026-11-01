package org.framefilm.ark

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.ByteArrayOutputStream
import java.io.File

/** One transfer attempt. The BLE connection belongs to ArkSession; this owns only its P2P group. */
class DirectTransferCoordinator(
    context: Context,
    private val ble: ArkBleController,
    private val file: File,
    private val fileName: String,
    private val listener: Listener,
    private val firmware: ArkFirmware? = null,
    private val reconnect: (() -> Unit)? = null,
    private val targetPath: String? = null,
) {
    interface Listener { fun onSnapshot(snapshot: TransferSnapshot) }

    private val main = Handler(Looper.getMainLooper())
    private val getterChannels = intArrayOf(0x31, 0x33, 0x35, 0x37)
    private val before = mutableListOf<ByteArray>()
    private val after = mutableListOf<ByteArray>()
    private val p2p: ArkP2pController = ArkP2pController(context, object : ArkP2pController.Listener {
        override fun onGroupReady(frequencyMhz: Int) {
            if (active && !stopping) emit("connecting", if (frequencyMhz > 0) "2.4GHz GO 已就绪（${frequencyMhz}MHz）" else "直连组已就绪，Android 9 由系统选择频段")
        }
        override fun onSessionReady(session: ArkP2pController.Session) {
            if (!active || stopping) return
            try {
                server = FilmHttpServer(file, fileName, session.ownerAddress,
                    firmware = firmware != null, generic = targetPath != null).also { it.start() }
                val payload = ByteArrayOutputStream().apply {
                    firmware?.let { image ->
                        for (shift in intArrayOf(24, 16, 8, 0)) write((image.size shr shift).toInt() and 255)
                        write(ArkFirmware.bytes(image.fileSha256))
                    }
                    write(session.ssid.toByteArray(Charsets.US_ASCII)); write(0)
                    write(session.passphrase.toByteArray(Charsets.US_ASCII)); write(0)
                    write(server!!.url.toByteArray(Charsets.US_ASCII)); write(0)
                    targetPath?.let { write(it.toByteArray(Charsets.UTF_8)); write(0) }
                }.toByteArray()
                if (payload.size > 192) { stop("直传参数超过 BLE 单包上限"); return }
                // Saving already routes film files by frame count. Do not enter the image
                // app here: entering it would replay the previous image before upload.
                startSubmitted = true
                val channel = when { targetPath != null -> 0x5A; firmware != null -> 0x57; else -> 0x50 }
                ble.command(channel, payload, 8_000) { reply ->
                    if (!active || stopping) return@command
                    val code = reply.getOrElse { stop(it.message ?: "直传启动失败"); return@command }
                    if (code.size != 1 || code[0].toInt() != 0) {
                        startSubmitted = false
                        stop("Ark 拒绝直传（代码 ${code.firstOrNull()?.toInt()?.and(0xff) ?: -1}）")
                    } else {
                        emit("connecting", "Ark 已接受直传；文件 ${server!!.size} 字节")
                        poll()
                    }
                }
            } catch (error: Exception) { stop("手机 HTTP 服务启动失败：${error.message}") }
        }
        override fun onState(state: String) {
            if (active && !stopping) emit("connecting", "Wi-Fi Direct：$state")
        }
        override fun onError(message: String) {
            if (active && !stopping) stop("Wi-Fi Direct：$message")
            else if (cleaning) cleanupError = message
        }
    })

    private var server: FilmHttpServer? = null
    private var active = false
    private var stopping = false
    private var cleaning = false
    private var startSubmitted = false
    private var terminal = false
    private var p2pClean = false
    private var cleanupError: String? = null
    private var resultPhase = "error"
    private var resultMessage = "直传未完成"
    private var resultSuccess = false
    private var received = 0L
    private var total = 0L
    private var deadline: Runnable? = null

    private var applied = false
    private var awaitingConfirmation = false
    private var reconnectTask: Runnable? = null
    val needsConfirmation: Boolean get() = awaitingConfirmation
    val isActive: Boolean get() = active || cleaning
    val cleanupConfirmed: Boolean get() = p2pClean && (!startSubmitted || terminal)

    fun start() = onMain {
        if (active || cleaning) return@onMain
        if (!ble.isReady) { emit("error", "请先连接 Ark", canRetry = true); return@onMain }
        if (!file.isFile || file.length() !in 1..0xffffffffL) {
            emit("error", when { targetPath != null -> "文件不存在或大小无效";
                firmware != null -> "固件文件不存在或大小无效"; else -> "film 文件不存在或大小无效" }, canRetry = false)
            return@onMain
        }
        active = true
        total = file.length()
        val timeoutMs = if (firmware != null) 180_000L else total * 1000L / 32768L + 180_000L
        deadline = Runnable { stop("直传超过 ${timeoutMs / 1000} 秒") }
            .also { main.postDelayed(it, timeoutMs) }
        emit("preparing", "核对 Ark 及原有 Wi-Fi 配置")
        if (firmware != null) {
            emit("validating", "核对 Wi-Fi OTA 能力与目标分区")
            ble.command(0x56) { result ->
                if (!active || stopping) return@command
                val info = runCatching { ArkBuildInfo.parse(result.getOrThrow()) }.getOrElse {
                    stop("设备不支持 Wi-Fi OTA；请先使用已有 BLE OTA 升级固件：${it.message}"); return@command
                }
                if (info.project != firmware.project || firmware.size > info.otaMax) {
                    stop("固件目标或大小不匹配 OTA 分区（${info.otaMax} 字节）"); return@command
                }
                if (info.elfSha256 == firmware?.elfSha256) {
                    terminal = true
                    finish("done", "设备已运行相同构建，无需升级", true)
                } else prepareNetwork()
            }
        } else if (targetPath != null) checkFileCapability()
        else prepareNetwork()
    }

    private fun checkFileCapability() {
        ble.command(0x59) { result ->
            if (!active || stopping) return@command
            val data = result.getOrElse { stop("无法确认设备支持通用 Wi-Fi 文件传输：${it.message}"); return@command }
            if (data.size != 5 || (data[0].toInt() and 0xff) != 1 || (data[4].toInt() and 0x02) == 0) {
                stop("设备固件不支持通用 Wi-Fi 文件传输，请先升级 Ark 固件")
            } else prepareNetwork()
        }
    }

    private fun prepareNetwork() {
        ble.command(0x42) { result ->
            if (!active || stopping) return@command
            val data = result.getOrElse { stop(it.message ?: "读取屏幕参数失败"); return@command }
            val width = if (data.size == 5) ((data[1].toInt() and 0xff) shl 8) or (data[2].toInt() and 0xff) else -1
            val height = if (data.size == 5) ((data[3].toInt() and 0xff) shl 8) or (data[4].toInt() and 0xff) else -1
            if (data.size != 5 || data[0].toInt() and 0xff != 2 || width != 720 || height != 480) {
                stop("设备不是预期 Ark 720×480 面板")
            } else snapshot(0, before) {
                if (active && !stopping) {
                    emit("connecting", "创建 2.4GHz 直连组")
                    p2p.createGroup2Ghz()
                }
            }
        }
    }

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    private fun emit(phase: String, message: String, canRetry: Boolean = false, cleanupCompleted: Boolean = false,
                     success: Boolean = false) {
        listener.onSnapshot(TransferSnapshot(phase, message, received, total,
            canCancel = active && !stopping && !applied, canRetry = canRetry && !applied, cleanupCompleted = cleanupCompleted,
            success = success, kind = when { targetPath != null -> "file"; firmware != null -> "firmware"; else -> "film" },
            targetVersion = firmware?.version, targetBuild = firmware?.elfSha256,
            canConfirm = awaitingConfirmation && !active))
    }

    private fun snapshot(index: Int, target: MutableList<ByteArray>, complete: () -> Unit) {
        if (!active || stopping) return
        if (index == getterChannels.size) { complete(); return }
        ble.command(getterChannels[index]) { result ->
            if (!active || stopping) return@command
            target += result.getOrElse { stop("读取原有 Wi-Fi 配置失败：${it.message}"); return@command }
            snapshot(index + 1, target, complete)
        }
    }

    private fun poll() {
        if (!active || stopping || terminal) return
        ble.command(0x51) { result ->
            if (!active || stopping) return@command
            val bytes = result.getOrElse { stop(it.message ?: "读取直传状态失败"); return@command }
            if (bytes.size != 11) { stop("直传状态长度错误"); return@command }
            val state = bytes[0].toInt() and 0xff
            val error = bytes[2].toInt() and 0xff
            fun u32(offset: Int): Long = (0..3).fold(0L) { value, n -> (value shl 8) or (bytes[offset + n].toLong() and 0xff) }
            received = u32(3)
            val reportedTotal = u32(7)
            if (reportedTotal > 0) total = reportedTotal
            val phase = when (state) { 1 -> "connecting"; 2 -> "downloading"; 3 -> "restoring"; else -> "downloading" }
            emit(phase, "Ark 状态 $state，${bytes[1].toInt() and 0xff}%，$received/$total 字节")
            if (state == 7 && firmware != null) {
                if (received != firmware.size || reportedTotal != firmware.size) {
                    stop("OTA 校验后的字节数不符"); return@command
                }
                emit("ready", "镜像已校验，设备已恢复原 Wi-Fi")
                snapshot(0, after) {
                    if (before.size != after.size || before.indices.any { !before[it].contentEquals(after[it]) })
                        stop("原有 Wi-Fi 配置前后不一致")
                    else applyFirmware()
                }
                return@command
            }
            if (state in 4..6) {
                terminal = true
                if (state != 4) {
                    val reason = when (error) {
                        1 -> "设备连接直连网络失败；Android 9 请先断开手机的 5GHz Wi-Fi 后重试"
                        2 -> "设备下载文件失败"
                        3 -> "设备无法保存文件，请检查 SD 卡剩余空间和写入状态"
                        4 -> "设备已取消传输"
                        5 -> "设备恢复原有 Wi-Fi 连接失败"
                        6 -> "设备可用资源不足"
                        7 -> "OTA 写入、镜像或目标错误"
                        8 -> "OTA SHA256 或长度不匹配"
                        9 -> "OTA READY 等待超时"
                        else -> "设备未完成传输"
                    }
                    stop("$reason（状态 $state，错误 $error）")
                    return@command
                }
                if (firmware != null) { stop("OTA 未进入 READY，不能确认升级"); return@command }
                val expected = server?.size ?: -1L
                if (received != expected || reportedTotal != expected) {
                    stop("传输字节数不符：$received/$reportedTotal，预期 $expected")
                    return@command
                }
                snapshot(0, after) {
                    if (before.size != after.size || before.indices.any { !before[it].contentEquals(after[it]) })
                        stop("原有 Wi-Fi 配置前后不一致")
                    else finish("done", "Ark 已保存 $received 字节，原有 Wi-Fi 配置未变", true)
                }
            } else main.postDelayed({ poll() }, 800)
        }
    }

    fun cancel() = onMain { if (active && !applied) stop("用户取消直传", cancelled = true) }

    fun onBleDisconnected() = onMain {
        if (applied && awaitingConfirmation) {
            if (active) { emit("rebooting", "Ark 重启中，等待同一设备重连"); scheduleReconnect() }
        } else if (active && !stopping) stop("Ark 蓝牙断开；需重连后确认固件恢复终态")
    }

    private fun applyFirmware() {
        if (!active || stopping) return
        emit("cleanup", "镜像已就绪，先关闭 HTTP 服务并释放直连组")
        server?.close()
        server = null
        cleaning = true
        p2p.close { clean ->
            cleaning = false
            p2pClean = clean
            if (!active) return@close
            if (stopping) { cleanup(); return@close }
            if (!clean) { stop("手机直连组清理未确认，未提交升级"); return@close }
            // From this point a lost reply may still mean activation succeeded. Never cancel/reflash.
            applied = true
            awaitingConfirmation = true
            emit("applying", "提交启动分区；随后重连核对实际 ELF SHA256")
            ble.command(0x58) { result ->
                if (!active) return@command
                val reply = result.getOrNull()
                if (reply?.size == 1 && (reply[0] == 1.toByte() || reply[0] == 2.toByte())) {
                    applied = false
                    awaitingConfirmation = false
                    stop("设备拒绝提交升级（${reply.firstOrNull()?.toInt()?.and(255) ?: -1}）")
                    return@command
                }
                emit("rebooting", "等待 Ark 重启，传输完成尚不代表升级成功")
                // Force a fresh GATT generation even if Android misses the disconnect callback.
                scheduleReconnect()
            }
            deadline?.let(main::removeCallbacks)
            deadline = Runnable { unconfirmed("重连确认超时；请重连同一 Ark 核对升级结果") }
                .also { main.postDelayed(it, 90_000) }
        }
    }

    fun onBleReady() = onMain {
        if (!awaitingConfirmation || !active) return@onMain
        reconnectTask?.let(main::removeCallbacks)
        reconnectTask = null
        emit("confirming", "核对重启后的实际 ELF SHA256")
        ble.command(0x56) { result ->
            if (!active || !awaitingConfirmation) return@command
            val info = runCatching { ArkBuildInfo.parse(result.getOrThrow()) }.getOrElse {
                unconfirmed("无法读取实际运行镜像：${it.message}"); return@command
            }
            if (info.project == firmware?.project && info.elfSha256 == firmware?.elfSha256) {
                awaitingConfirmation = false
                terminal = true
                finish("done", "升级成功：${info.version}，实际 ELF SHA256 已匹配", true)
            } else unconfirmed("实际运行镜像与导入固件不匹配；请检查设备，不会自动重刷")
        }
    }

    fun confirmFirmware() = onMain {
        if (!awaitingConfirmation || active) return@onMain
        active = true
        stopping = false
        emit("confirming", "重连原 Ark 并核对实际运行镜像")
        deadline = Runnable { unconfirmed("重连确认超时，请稍后再次确认") }
            .also { main.postDelayed(it, 60_000) }
        reconnect?.invoke()
    }

    private fun scheduleReconnect() {
        reconnectTask?.let(main::removeCallbacks)
        reconnectTask = Runnable {
            reconnectTask = null
            if (active && awaitingConfirmation) reconnect?.invoke()
        }.also { main.postDelayed(it, 3000) }
    }

    private fun unconfirmed(message: String) {
        reconnectTask?.let(main::removeCallbacks)
        reconnectTask = null
        deadline?.let(main::removeCallbacks)
        deadline = null
        active = false
        cleaning = false
        emit("unconfirmed", message, cleanupCompleted = p2pClean)
    }
    private fun stop(message: String, cancelled: Boolean = false) {
        if (!active || stopping) return
        stopping = true
        resultPhase = if (cancelled) "cancelled" else "error"
        resultMessage = message
        resultSuccess = false
        if (applied) { unconfirmed(message); return }
        if (startSubmitted && !terminal && ble.isReady) {
            emit("cancelling", "请求 Ark 取消并等待 Wi-Fi 恢复")
            requestCancel(System.currentTimeMillis() + 40_000)
        } else cleanup()
    }

    private fun requestCancel(until: Long) {
        if (!active) return
        if (System.currentTimeMillis() >= until || !ble.isReady) { cleanup(); return }
        if (ble.isBusy) { main.postDelayed({ requestCancel(until) }, 200); return }
        ble.command(0x52) { waitTerminal(until) }
    }

    private fun waitTerminal(until: Long) {
        if (!active) return
        if (System.currentTimeMillis() >= until || !ble.isReady) { cleanup(); return }
        if (ble.isBusy) { main.postDelayed({ waitTerminal(until) }, 200); return }
        ble.command(0x51) { result ->
            val bytes = result.getOrNull()
            val state = if (bytes?.size == 11) bytes[0].toInt() and 0xff else -1
            if (state == 0 || state in 4..6) { terminal = true; cleanup() }
            else main.postDelayed({ waitTerminal(until) }, 800)
        }
    }

    private fun finish(phase: String, message: String, success: Boolean) {
        resultPhase = phase
        resultMessage = message
        resultSuccess = success
        cleanup()
    }

    private fun cleanup() {
        if (!active || cleaning) return
        cleaning = true
        active = false
        deadline?.let(main::removeCallbacks)
        deadline = null
        emit("cleanup", "关闭临时 HTTP 服务并释放手机直连组")
        server?.close()
        server = null
        p2p.close { clean ->
            cleaning = false
            p2pClean = clean
            val complete = cleanupConfirmed
            val success = resultSuccess && complete
            val phase = if (!complete) "error" else resultPhase
            val detail = when {
                !clean -> "; 手机直连组清理未确认：${cleanupError ?: "原因未报告"}"
                startSubmitted && !terminal -> "; Ark 恢复终态未确认"
                else -> ""
            }
            emit(phase, resultMessage + detail, canRetry = !success && file.isFile,
                cleanupCompleted = complete, success = success)
        }
    }

    /** Called before another attempt; never creates a new group until the old session converges. */
    fun ensureSettled(callback: (Boolean, String) -> Unit) = onMain {
        if (isActive) { callback(false, "上一轮仍在进行"); return@onMain }
        fun settleGroup() {
            if (p2pClean && !p2p.hasPendingCleanup) { callback(true, "上一轮已清理"); return }
            p2p.close { clean ->
                p2pClean = clean
                callback(clean, if (clean) "上一轮直连组已释放" else "上一轮直连组仍未清理")
            }
        }
        if (!startSubmitted || terminal) { settleGroup(); return@onMain }
        if (!ble.isReady) {
            // The firmware state is unknown, but the phone-owned group can still be released.
            p2p.close { clean ->
                p2pClean = clean
                callback(false, "请先重连 Ark，再确认上一轮 Wi-Fi 已恢复")
            }
            return@onMain
        }
        settleFirmware(System.currentTimeMillis() + 40_000) { restored ->
            if (restored) settleGroup()
            else callback(false, "上一轮 Ark Wi-Fi 恢复终态未确认")
        }
    }

    private fun settleFirmware(until: Long, callback: (Boolean) -> Unit) {
        if (!ble.isReady || System.currentTimeMillis() >= until) { callback(false); return }
        if (ble.isBusy) { main.postDelayed({ settleFirmware(until, callback) }, 200); return }
        ble.command(0x51) { result ->
            val bytes = result.getOrNull()
            val state = if (bytes?.size == 11) bytes[0].toInt() and 0xff else -1
            when {
                state == 0 || state in 4..6 -> { terminal = true; callback(true) }
                state in 1..3 || state == 7 -> ble.command(0x52) { waitSettledStatus(until, callback) }
                else -> callback(false)
            }
        }
    }

    private fun waitSettledStatus(until: Long, callback: (Boolean) -> Unit) {
        if (!ble.isReady || System.currentTimeMillis() >= until) { callback(false); return }
        if (ble.isBusy) { main.postDelayed({ waitSettledStatus(until, callback) }, 200); return }
        ble.command(0x51) { result ->
            val bytes = result.getOrNull()
            val state = if (bytes?.size == 11) bytes[0].toInt() and 0xff else -1
            if (state == 0 || state in 4..6) { terminal = true; callback(true) }
            else main.postDelayed({ waitSettledStatus(until, callback) }, 800)
        }
    }
}
