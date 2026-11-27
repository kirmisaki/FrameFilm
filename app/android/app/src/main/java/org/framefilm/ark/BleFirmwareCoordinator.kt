package org.framefilm.ark

import android.os.Handler
import android.os.Looper
import java.io.DataInputStream
import java.io.File
import kotlin.math.min

/** Staged BLE OTA with device-acknowledged writes and verified activation.
 * firmware must come from ArkFirmware.inspect() (plus any release-manifest checks) at import.
 * All entry points and the optional preflight callback run on the main looper.
 */
class BleFirmwareCoordinator(
    private val ble: ArkBleController,
    private val file: File?,
    private val firmware: ArkFirmware,
    private val deviceAddress: String,
    private val currentDeviceAddress: () -> String?,
    private val listener: (TransferSnapshot) -> Unit,
    private val reconnect: () -> Unit,
    private val persistRecovery: (Recovery?) -> Boolean,
    private val recovery: Recovery? = null,
    private val preflight: ((ArkFirmware, (Result<VerifiedTarget>) -> Unit) -> Unit)? = null,
) : TransferCoordinator {
    // Retain legacy stage names so older persisted recovery remains readable.
    enum class Stage { LEN_STARTED, STOP_PENDING, STAGING, APPLY_PENDING }
    data class Recovery(val deviceAddress: String, val firmware: ArkFirmware, val stage: Stage,
                        val originalElf: String? = null)
    data class VerifiedTarget(val hardware: ArkHardware, val build: ArkBuildInfo)

    private val main = Handler(Looper.getMainLooper())
    private var started = false
    private var active = false
    private var originalElf = recovery?.originalElf
    private var clean = true
    private var stage = recovery?.stage
    private var cancelRequested = false
    private var confirmed = false
    private var epoch = 0
    private var received = 0L
    private var reported = 0L
    private var input: DataInputStream? = null
    private var work: Runnable? = null
    private var deadline: Runnable? = null

    init {
        require(deviceAddress.isNotBlank()) { "缺少原 Ark 身份" }
        require(recovery == null || (recovery.deviceAddress == deviceAddress && recovery.firmware == firmware)) {
            "恢复记录与升级目标不匹配"
        }
        clean = stage == null || needsConfirmation
    }

    override val isActive get() = active
    override val cleanupConfirmed get() = clean && !active
    override val needsConfirmation get() = stage in setOf(Stage.APPLY_PENDING, Stage.STOP_PENDING) && !confirmed
    override val expectsBleResume get() = active && needsConfirmation

    private fun onMain(action: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) action() else main.post(action)
    }
    private fun ownerReady() = ble.isReady && currentDeviceAddress() == deviceAddress
    private fun alive(token: Int) = active && epoch == token
    private fun payloadSize() = min(188, ble.negotiatedMtu - 11)

    private fun emit(phase: String, message: String) = listener(TransferSnapshot(
        phase, message, received, firmware.size, canCancel = active && !needsConfirmation && !cancelRequested,
        cleanupCompleted = cleanupConfirmed, success = confirmed, kind = "firmware",
        targetVersion = firmware.version, targetBuild = firmware.elfSha256,
        canConfirm = !active && needsConfirmation, transport = "ble",
        confirmation = when {
            confirmed -> "elf_sha256"
            needsConfirmation -> "pending"
            stage == Stage.STAGING && !active -> "staging_cleanup"
            stage == Stage.LEN_STARTED -> "legacy_restart"
            else -> null
        }))

    override fun start() = onMain {
        if (started) return@onMain
        started = true
        if (recovery != null) {
            emit(if (needsConfirmation) "unconfirmed" else "interrupted", when (recovery.stage) {
                Stage.APPLY_PENDING, Stage.STOP_PENDING -> "升级提交可能已发生；请重连原 Ark 核对实际 ELF"
                Stage.LEN_STARTED -> "旧版 BLE 升级在 LEN 后中断；请手动重启原 Ark"
                Stage.STAGING -> "BLE 升级在提交前中断；请重连原 Ark 确认暂存已清理"
            })
            return@onMain
        }
        val error = runCatching {
            require(ownerReady() && !ble.isBusy) { "请先连接原 Ark，并等待 BLE 空闲" }
            require(file?.isFile == true && file.length() == firmware.size && firmware.size in 1..0xffffffffL) {
                "固件文件不存在、大小已变化或超出 u32"
            }
            require(firmware.project == "frame_film_ark" && firmware.hardware != null) { "固件缺少 Ark 硬件目标描述" }
            require(firmware.fileSha256.matches(Regex("[a-fA-F0-9]{64}")) &&
                firmware.elfSha256.matches(Regex("[a-fA-F0-9]{64}"))) { "固件 SHA256 无效" }
            require(ble.negotiatedMtu >= TransferPolicy.BLE_FIRMWARE_MIN_MTU) { "BLE MTU 不足以发送升级校验信息" }
        }.exceptionOrNull()
        if (error != null) { emit("error", error.message ?: "固件目标无效"); return@onMain }
        active = true
        val timeoutMs = TransferPolicy.transferDeadlineMs(firmware.size, payloadSize())
        setDeadline(timeoutMs, "BLE 固件发送超时")
        emit("validating", "核对原 Ark 硬件身份、运行项目与 OTA 分区；请保持 App 前台")
        if (!active) return@onMain
        val token = epoch
        var preflightSettled = false
        val checked: (Result<VerifiedTarget>) -> Unit = { result -> onMain {
            if (alive(token) && !preflightSettled) {
                preflightSettled = true
                val target = runCatching {
                    require(ownerReady()) { "原 Ark 已断开或设备身份已变化" }
                    val verified = result.getOrThrow()
                    verified.hardware.requireMatches(firmware)
                    require(verified.build.project == firmware.project && firmware.size <= verified.build.otaMax) {
                        "固件项目或大小与 OTA 分区不匹配"
                    }
                    require(verified.build.elfSha256.any { it != '0' }) { "设备未提供有效运行 ELF SHA256" }
                    verified
                }.getOrElse { fail("固件预检失败：${it.message}"); return@onMain }
                if (target.build.elfSha256.equals(firmware.elfSha256, ignoreCase = true)) {
                    confirmed = true
                    finish("done", "原 Ark 已运行相同项目及完整 ELF SHA256，无需升级")
                } else {
                    originalElf = target.build.elfSha256
                    beginFile()
                }
            }
        } }
        val validate = preflight
        if (validate != null) {
            runCatching { validate(firmware, checked) }.onFailure { if (alive(token)) fail("固件预检失败：${it.message}") }
        } else query(0x59) { capabilities ->
            if (!TransferPolicy.supportsVerifiedBleFirmware(capabilities)) fail("当前运行固件未提供可验证 BLE OTA")
            else query(0x5D) { hardware ->
                val parsed = runCatching { ArkHardware.parse(hardware) }.getOrElse {
                    fail("无法核对硬件身份：${it.message}"); return@query
                }
                query(0x56) { build -> checked(runCatching { VerifiedTarget(parsed, ArkBuildInfo.parse(build)) }) }
            }
        }
    }

    /** Must synchronously commit durable state; false/throw means no next destructive write. */
    private fun persist(stage: Stage?): Boolean = runCatching {
            persistRecovery(stage?.let { Recovery(deviceAddress, firmware, it, originalElf) })
    }.getOrDefault(false)

    private fun beginFile() {
        if (!active) return
        input = runCatching { DataInputStream(requireNotNull(file).inputStream().buffered()) }.getOrElse {
            fail("读取本地固件失败：${it.message}"); return
        }
        if (!persist(Stage.STAGING)) { fail("无法保存升级恢复记录，未开始暂存"); return }
        stage = Stage.STAGING
        clean = false
        emit("erasing", "等待设备准备固件暂存分区")
        if (!active || cancelRequested) return
        stageCommand(0x6A, be32(firmware.size) + ArkFirmware.bytes(firmware.fileSha256), 0, 30_000) {
            sendChunk()
        }
    }

    private fun query(channel: Int, payload: ByteArray = byteArrayOf(), waitMs: Long = 5_000, done: (ByteArray) -> Unit) {
        if (!active) return
        if (!ownerReady()) { fail("原 Ark 已断开或设备身份已变化"); return }
        val token = epoch
        ble.command(channel, payload, waitMs = waitMs) { result -> onMain {
            if (alive(token)) {
                if (!ownerReady()) fail("原 Ark 已断开或设备身份已变化")
                else result.fold(done) { fail("BLE 命令 0x${channel.toString(16)} 未确认：${it.message}", cancelRequested) }
            }
        } }
    }

    private fun stageCommand(channel: Int, payload: ByteArray = byteArrayOf(), expected: Long? = null,
                             waitMs: Long = 10_000, done: () -> Unit) {
        query(channel, payload, waitMs) { data ->
            if (data.size != 5) {
                fail("设备返回的 BLE OTA 回执格式无效", cancelRequested); return@query
            }
            val status = data[0].toInt() and 255
            if (status != 0) {
                if (channel == 0x6D && status in 1..3 && persist(Stage.STAGING)) stage = Stage.STAGING
                fail("设备拒绝 BLE OTA 命令 0x${channel.toString(16)}（状态 $status）", cancelRequested)
                return@query
            }
            val offset = (1..4).fold(0L) { value, index -> (value shl 8) or (data[index].toLong() and 255) }
            if (expected != null && offset != expected) {
                fail("设备确认的固件偏移 $offset 与预期 $expected 不符", cancelRequested); return@query
            }
            if (cancelRequested && channel != 0x6E) cancelStaging() else done()
        }
    }

    private fun sendChunk() {
        if (!active) return
        if (received == firmware.size) {
            runCatching { input?.close() }; input = null
            emit("verifying", "等待设备验证固件长度、SHA256 和硬件目标")
            if (!active || cancelRequested) return
            stageCommand(0x6C, expected = firmware.size, waitMs = 30_000) { submit() }
            return
        }
        if (payloadSize() < 1) { fail("BLE MTU 无效"); return }
        val buffer = ByteArray(min(payloadSize().toLong(), firmware.size - received).toInt())
        runCatching { requireNotNull(input).readFully(buffer) }.getOrElse {
            fail("读取本地固件失败：${it.message ?: "文件不完整"}"); return
        }
        stageCommand(0x6B, be32(received) + buffer, received + buffer.size) {
            received += buffer.size
            if (received == firmware.size || reported == 0L ||
                received - reported >= TransferPolicy.progressStep(firmware.size)) {
                reported = received
                emit("sending", "设备已确认接收 $received / ${firmware.size} 字节")
            }
            if (!cancelRequested) schedule(0) { sendChunk() }
        }
    }

    private fun submit() {
        if (!active || needsConfirmation || received != firmware.size) return
        if (!persist(Stage.APPLY_PENDING)) { fail("无法保存升级提交恢复记录，未激活固件"); return }
        stage = Stage.APPLY_PENDING
        clean = true
        emit("applying", "设备已验证固件，正在提交并等待重启")
        stageCommand(0x6D, expected = firmware.size) {
            clearWork()
            setDeadline(90_000, "重连确认超时，请稍后重连原 Ark 核对结果")
            emit("rebooting", "设备已接受固件激活，等待原 Ark 重启后核对实际镜像")
            ble.close() // Require a fresh GATT generation before reading the running build.
            scheduleReconnect()
        }
    }

    private fun schedule(delay: Long, action: () -> Unit) {
        if (!active) return
        work?.let(main::removeCallbacks)
        val token = epoch
        work = Runnable { work = null; if (alive(token)) action() }.also { main.postDelayed(it, delay) }
    }
    private fun scheduleReconnect() = schedule(3000) {
        if (currentDeviceAddress() == deviceAddress || currentDeviceAddress() == null) reconnect()
        else fail("当前不是原 Ark，请重连原设备后仅核对升级结果")
    }
    private fun setDeadline(delay: Long, message: String) {
        deadline?.let(main::removeCallbacks)
        val token = epoch
        deadline = Runnable { if (alive(token)) fail(message) }.also { main.postDelayed(it, delay) }
    }
    private fun clearWork() {
        runCatching { input?.close() }; input = null
        work?.let(main::removeCallbacks); work = null
        deadline?.let(main::removeCallbacks); deadline = null
    }
    private fun finish(phase: String, message: String) {
        active = false
        epoch++ // Late callbacks cannot advance a subsequent confirmation round.
        clearWork()
        emit(phase, message)
    }
    private fun fail(message: String, cancelled: Boolean = false) {
        if (!active) return
        active = false
        epoch++
        clearWork()
        if (stage != null || ble.isBusy) ble.close()
        clean = stage == null || needsConfirmation
        emit(when {
            needsConfirmation -> "unconfirmed"
            stage == Stage.STAGING -> "interrupted"
            cancelled -> "cancelled"
            else -> "error"
        }, when {
            needsConfirmation -> "$message；激活结果待确认，请重连原 Ark 核对实际 ELF"
            stage == Stage.STAGING -> "$message；请重连原 Ark 确认暂存已清理"
            else -> message
        })
    }

    private fun cancelStaging() {
        work?.let(main::removeCallbacks); work = null
        stageCommand(0x6E) {
            if (!persist(null)) { fail("设备已取消，恢复记录暂未清除", cancelled = true); return@stageCommand }
            stage = null
            clean = true
            finish("cancelled", "设备已取消暂存固件")
        }
    }

    override fun cancel() = onMain {
        if (!active || needsConfirmation || cancelRequested) return@onMain
        cancelRequested = true
        if (stage == null) fail("已停止 BLE 固件发送", cancelled = true)
        else if (!ble.isBusy) cancelStaging()
        else emit("cancelling", "等待当前固件分块回执后取消暂存")
    }
    override fun onBleDisconnected() = onMain {
        if (!active) return@onMain
        if (needsConfirmation) {
            emit("rebooting", "原 Ark 已断开，等待重连后只读核对实际 ELF")
            scheduleReconnect()
        } else fail("BLE 固件发送中断")
    }
    override fun onBleReady() = onMain {
        if (active && needsConfirmation) {
            work?.let(main::removeCallbacks); work = null
            verifyBuild()
        }
    }
    override fun confirmFirmware() = onMain {
        if (active || !needsConfirmation) return@onMain
        active = true
        epoch++
        setDeadline(60_000, "升级结果确认超时，请重连原 Ark 后再次确认")
        emit("confirming", "仅核对原 Ark 实际运行项目与完整 ELF SHA256，不会重发固件")
        if (!active) return@onMain
        if (ownerReady() && !ble.isBusy) verifyBuild()
        else if (currentDeviceAddress() == deviceAddress || currentDeviceAddress() == null) scheduleReconnect()
        else fail("当前不是原 Ark")
    }
    private fun verifyBuild() {
        if (!ownerReady() || ble.isBusy) { fail("原 Ark 尚未就绪，稍后仅重试结果核对"); return }
        emit("confirming", "读取原 Ark 的实际运行项目与完整 ELF SHA256")
        query(0x56) { data ->
            val info = runCatching { ArkBuildInfo.parse(data) }.getOrElse {
                fail("无法读取实际运行镜像：${it.message}"); return@query
            }
            if (info.project != firmware.project || !info.elfSha256.equals(firmware.elfSha256, ignoreCase = true)) {
                fail("实际运行项目或完整 ELF SHA256 不匹配，升级仍未确认"); return@query
            }
            if (!persist(null)) { fail("运行镜像已匹配，但恢复记录清理未完成"); return@query }
            confirmed = true
            stage = null
            finish("done", "升级成功：${info.version}，原 Ark 实际项目及完整 ELF SHA256 已匹配")
        }
    }

    /** After the user confirms a physical restart, the original running build can release
     * a legacy LEN record or an unconfirmed staged activation. This only reads build info.
     */
    fun acknowledgeManualRestart(callback: (Boolean) -> Unit) = onMain {
        if (active || stage !in setOf(Stage.LEN_STARTED, Stage.APPLY_PENDING) ||
            !ownerReady() || ble.isBusy || originalElf == null) {
            callback(false)
            return@onMain
        }
        ble.command(0x56) { result -> onMain {
            val info = runCatching { ArkBuildInfo.parse(result.getOrThrow()) }.getOrNull()
            if (!ownerReady() || info?.project != firmware.project ||
                !info.elfSha256.equals(originalElf, ignoreCase = true) || !persist(null)) callback(false)
            else {
                stage = null
                clean = true
                emit("cancelled", "已确认用户手动重启且设备仍运行原构建；旧升级不再发送")
                callback(true)
            }
        } }
    }
    override fun ensureSettled(callback: (Boolean, String) -> Unit) = onMain {
        if (stage == Stage.STAGING && !active && ownerReady()) {
            // New GATT generation: firmware drops pre-activation staging at disconnect.
            if (persist(null)) {
                stage = null
                clean = true
            }
        }
        callback(cleanupConfirmed, when {
            cleanupConfirmed -> ""
            stage == Stage.LEN_STARTED -> "旧版 BLE 升级中断，请手动重启原 Ark 并明确确认"
            stage == Stage.STAGING -> "请重连原 Ark 确认暂存已清理"
            else -> "BLE 固件操作仍在进行"
        })
    }
    private fun be32(value: Long) = ByteArray(4) { n -> (value ushr (24 - n * 8)).toByte() }
}
