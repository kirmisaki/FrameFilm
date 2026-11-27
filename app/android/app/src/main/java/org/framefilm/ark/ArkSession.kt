package org.framefilm.ark

import android.bluetooth.BluetoothDevice
import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.File
import java.util.ArrayDeque

/** Owns one Ark BLE connection and serializes ordinary writes around direct transfers. */
class ArkSession(context: Context, private val listener: Listener,
                 private val persistBleFirmware: (BleFirmwareCoordinator.Recovery?) -> Boolean = { false }) {
    interface Listener {
        fun onDevices(devices: List<BluetoothDevice>) { }
        fun onConnection(connected: Boolean, message: String) { }
        /** Once per actual GATT link, when ordinary settings reads may begin. */
        fun onReadyForSettings() { }
        fun onPacket(packet: ByteArray) { }
        fun onTransfer(snapshot: TransferSnapshot) { }
    }

    private data class QueuedWrite(val channel: Int, val payload: ByteArray,
                                   val callback: (Result<Unit>) -> Unit)
    private data class TransferInput(val file: File, val fileName: String,
                                     val firmware: ArkFirmware? = null, val targetPath: String? = null,
                                     val legacyHardwareTarget: String? = null,
                                     val transport: String = "wifi", val attemptId: Int = 0,
                                     val mediaDirectory: String? = null,
                                     val verifiedBleFile: Boolean = false, val requiresAbc9: Boolean = false)

    private val main = Handler(Looper.getMainLooper())
    private val appContext = context.applicationContext
    private val devices = linkedMapOf<String, BluetoothDevice>()
    private val writes = ArrayDeque<QueuedWrite>()
    private var writing = false
    private var pendingStart: TransferInput? = null
    private var cancelAfterSettle = false
    private var lastInput: TransferInput? = null
    private var settling = false
    private var disconnectAfterTransfer = false
    private var reconnectSuppressed = false
    private var closing = false
    private var closed = false
    private var connectedDevice: BluetoothDevice? = null
    private var coordinator: TransferCoordinator? = null
    private var attemptSequence = (System.currentTimeMillis() and 0x3fffffff).toInt()
    val retryTransport: String get() = lastInput?.transport ?: "wifi"
    private var transferOwnerAddress: String? = null
    private var lastTransferSnapshot: TransferSnapshot? = null
    private var uncertainPhotoFile: File? = null
    private var readyAfterCleanup = false
    private var pendingSettingsLink: Int? = null
    private val ble: ArkBleController = ArkBleController(context, object : ArkBleController.Listener {
        override fun onDevices(found: List<BluetoothDevice>) {
            devices.clear()
            found.forEach { devices[it.address] = it }
            if (!closed) listener.onDevices(found)
        }
        override fun onReady() {
            if (closed) return
            pendingSettingsLink = ble.readyLinkId
            readyAfterCleanup = true
            resumeReadyAfterCleanup()
        }
        override fun onPanel(panelId: Int, width: Int, height: Int) { }
        override fun onState(state: String) {
            // Ready status can precede onReady; keep it hidden during transfer recovery.
            if (!closed && coordinator?.expectsBleResume != true && !(ble.isReady && (readyAfterCleanup || needsTransferRecovery())))
                listener.onConnection(ble.isReady, state)
        }
        override fun onError(message: String) {
            readyAfterCleanup = false
            if (!closed && coordinator?.expectsBleResume != true) listener.onConnection(false, message)
            if (!ble.isReady) {
                coordinator?.onBleDisconnected()
                failQueuedWrites("Ark 蓝牙已断开")
            }
        }
        override fun onPacket(packet: ByteArray) {
            (coordinator as? BleTransferCoordinator)?.onPacket(packet)
            // Transfer-side Wi-Fi snapshot getters can contain saved credentials.
            if (!closed && !transferActive) listener.onPacket(packet)
        }
    })

    val isReady: Boolean get() = ble.isReady
    val readyLinkId: Int? get() = ble.readyLinkId
    val negotiatedMtu: Int get() = ble.negotiatedMtu
    val connectedAddress: String? get() = if (isReady) connectedDevice?.address else null
    fun scannedName(address: String): String? = ble.scannedName(address)
    val transferActive: Boolean get() = pendingStart != null || settling || coordinator?.isActive == true

    private fun publishTransfer(snapshot: TransferSnapshot, input: TransferInput? = lastInput) {
        val image = input?.firmware
        val published = snapshot.copy(
            kind = when { input?.targetPath != null -> "file"; image != null -> "firmware"; input != null -> "film"; else -> snapshot.kind },
            targetVersion = image?.version ?: snapshot.targetVersion,
            targetBuild = image?.elfSha256 ?: snapshot.targetBuild,
            transport = input?.transport ?: snapshot.transport, attemptId = input?.attemptId ?: snapshot.attemptId)
        if (published.transport == "ble" && published.kind == "film") {
            if (published.phase == "unconfirmed") uncertainPhotoFile = input?.file
            else if (published.success && uncertainPhotoFile == input?.file) uncertainPhotoFile = null
        }
        lastTransferSnapshot = published
        listener.onTransfer(published)
    }

    private fun needsTransferRecovery(): Boolean = coordinator?.let {
        !it.needsConfirmation && (it.isActive || !it.cleanupConfirmed)
    } == true

    private fun resumeReadyAfterCleanup() {
        if (!readyAfterCleanup || closed || !ble.isReady || settling) return
        val previous = coordinator
        if (closing && previous?.isActive != true && previous?.cleanupConfirmed != false) {
            readyAfterCleanup = false
            finishClose()
            return
        }
        if (previous?.expectsBleResume == true) {
            previous.onBleReady()
            return
        }
        if (previous != null && !previous.needsConfirmation && previous.isActive) return
        if (previous != null && !previous.needsConfirmation && !previous.cleanupConfirmed) {
            settling = true
            val last = lastTransferSnapshot
            last?.let {
                publishTransfer(it.copy(phase = "cleanup", message = "已重连，正在确认上一轮传输已结束",
                    canCancel = false, canRetry = false, cleanupCompleted = false))
            }
            previous.ensureSettled { complete, message ->
                settling = false
                if (closed || coordinator !== previous) return@ensureSettled
                last?.let {
                    publishTransfer(it.copy(
                        phase = if (complete) it.phase else "error",
                        message = if (complete)
                            "${it.message.substringBefore("; Ark 恢复终态未确认").substringBefore("; 手机直连组清理未确认")}；重连后已确认上一轮清理"
                        else "${it.message}；$message",
                        cleanupCompleted = complete, success = it.success && complete))
                }
                cancelAfterSettle = false
                if (closing) {
                    readyAfterCleanup = false
                    finishClose()
                } else if (disconnectAfterTransfer) {
                    readyAfterCleanup = false
                    disconnectAfterTransfer = false
                    ble.close()
                } else finishReadyAfterCleanup()
            }
        } else finishReadyAfterCleanup()
    }

    private fun finishReadyAfterCleanup() {
        if (!readyAfterCleanup || !ble.isReady || closed) return
        readyAfterCleanup = false
        listener.onConnection(true, "Ark GATT 已就绪，MTU ${ble.negotiatedMtu}")
        coordinator?.onBleReady()
        notifyReadyForSettings()
        pump()
    }

    private fun notifyReadyForSettings() {
        val link = pendingSettingsLink ?: return
        if (link != readyLinkId || readyAfterCleanup || closed || closing || disconnectAfterTransfer ||
            transferActive || coordinator?.cleanupConfirmed == false || coordinator?.needsConfirmation == true) return
        pendingSettingsLink = null
        listener.onReadyForSettings()
    }

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    fun scan() = onMain {
        if (closed || closing || transferActive) {
            listener.onConnection(isReady, "直传或关闭期间不能扫描")
        } else ble.startScan()
    }

    fun connect(address: String) = onMain {
        val restoringOriginal = coordinator is BleFirmwareCoordinator && address == transferOwnerAddress
        if (closed || closing || transferActive || (coordinator?.needsConfirmation == true && !restoringOriginal)) {
            listener.onConnection(isReady, "直传、关闭或升级结果待确认期间不能切换设备")
            return@onMain
        }
        if (coordinator?.cleanupConfirmed == false && address != transferOwnerAddress) {
            listener.onConnection(isReady, "请先连接上一台 Ark，确认直传已恢复终态")
            return@onMain
        }
        val device = devices[address]
        if (device == null) listener.onConnection(false, "请先扫描并选择 Ark")
        else { connectedDevice = device; ble.connect(device) }
    }

    fun disconnect() = onMain {
        reconnectSuppressed = true
        if (transferActive) {
            disconnectAfterTransfer = true
            cancelTransfer()
        } else {
            failQueuedWrites("Ark 蓝牙已断开")
            val previous = coordinator
            if (previous != null && !previous.cleanupConfirmed) {
                settling = true
                previous.ensureSettled { complete, message ->
                    settling = false
                    cancelAfterSettle = false
                    disconnectAfterTransfer = false
                    if (!complete) publishTransfer(TransferSnapshot("error", message,
                        canRetry = lastInput?.file?.isFile == true, cleanupCompleted = false))
                    if (closing) finishClose() else ble.close()
                }
            } else ble.close()
        }
    }

    /** Callback confirms GATT write completion; notifications arrive separately via onPacket. */
    fun writePacket(packet: ByteArray, callback: (Result<Unit>) -> Unit) = onMain {
        val error = when {
            closed || closing -> "会话已关闭"
            transferActive || coordinator?.cleanupConfirmed == false -> "传输或断连清理期间不能发送普通 BLE 命令"
            !ble.isReady -> "请先连接 Ark"
            packet.size !in 4..196 || packet[0].toInt() and 0xff != 0x55 -> "BLE 帧无效"
            (packet[2].toInt() and 0xff) + 4 != packet.size -> "BLE 帧长度不匹配"
            (packet.dropLast(1).sumOf { it.toInt() and 0xff } and 0xff) != (packet.last().toInt() and 0xff) -> "BLE 校验和错误"
            else -> null
        }
        if (error != null) { callback(Result.failure(IllegalArgumentException(error))); return@onMain }
        writes.addLast(QueuedWrite(packet[1].toInt() and 0xff, packet.copyOfRange(3, packet.lastIndex), callback))
        pump()
    }

    private fun pump() {
        if (closed || closing || writing || !ble.isReady) return
        if (writes.isNotEmpty()) {
            val next = writes.removeFirst()
            writing = true
            ble.sendNoReply(next.channel, next.payload) { result ->
                writing = false
                next.callback(result)
                pump()
            }
            return
        }
        val input = pendingStart ?: return
        if (ble.isBusy) { main.postDelayed({ pump() }, 100); return }
        pendingStart = null
        settling = true
        val previous = coordinator
        val begin: (Boolean, String) -> Unit = { ready, message ->
            settling = false
            if (cancelAfterSettle) {
                cancelAfterSettle = false
                publishTransfer(TransferSnapshot(if (ready) "cancelled" else "error",
                    if (ready) "重试前已取消" else message, total = input.file.length(),
                    canRetry = input.file.isFile, cleanupCompleted = previous?.cleanupConfirmed ?: true))
                if (closing) finishClose()
                else if (disconnectAfterTransfer) { disconnectAfterTransfer = false; ble.close() }
            } else if (!ready || !ble.isReady || closing) {
                publishTransfer(TransferSnapshot("error", message,
                    canRetry = input.file.isFile, cleanupCompleted = previous?.cleanupConfirmed ?: true))
                if (closing) finishClose()
            } else {
                val report: (TransferSnapshot) -> Unit = report@{ snapshot ->
                    if (lastInput?.attemptId != input.attemptId) return@report
                    if (!closed) publishTransfer(snapshot, input)
                    if (snapshot.phase in setOf("done", "cancelled", "error", "unconfirmed", "interrupted") &&
                        coordinator?.isActive == false) {
                        resumeReadyAfterCleanup()
                        if (closing) finishClose()
                        else if (disconnectAfterTransfer) {
                            disconnectAfterTransfer = false
                            ble.close()
                        } else notifyReadyForSettings()
                    }
                }
                val reconnect: () -> Unit = {
                    val device = connectedDevice
                    if (!closed && !reconnectSuppressed && device != null && device.address == transferOwnerAddress &&
                        lastInput?.attemptId == input.attemptId && !ble.isReady && !ble.isConnecting &&
                        (!closing || coordinator?.expectsBleResume == true)) ble.connect(device)
                }
                val next: TransferCoordinator = if (input.transport == "ble" && input.firmware != null) {
                    BleFirmwareCoordinator(ble, input.file, input.firmware, checkNotNull(transferOwnerAddress),
                        { connectedAddress }, report, reconnect, persistBleFirmware)
                } else if (input.transport == "ble" && input.verifiedBleFile) {
                    val path = input.targetPath ?: "${checkNotNull(input.mediaDirectory)}/${input.fileName}"
                    val directory = when (input.mediaDirectory) { "film" -> 1; "animation" -> 2; else -> null }
                    VerifiedBleFileCoordinator(ble, input.file, path, directory, input.requiresAbc9, report, reconnect)
                } else if (input.transport == "ble") {
                    BleTransferCoordinator(ble, input.file, input.fileName, report, reconnect)
                } else DirectTransferCoordinator(appContext, ble, input.file, input.fileName,
                    object : DirectTransferCoordinator.Listener {
                        override fun onSnapshot(snapshot: TransferSnapshot) = report(snapshot)
                    }, input.firmware, reconnect = reconnect, targetPath = input.targetPath,
                    legacyHardwareTarget = input.legacyHardwareTarget)
                coordinator = next
                next.start()
            }
        }
        if (previous == null) begin(true, "") else previous.ensureSettled(begin)
    }

    fun startTransfer(file: File, fileName: String, transportPreference: String? = null,
                      wifiSupport: TransferPolicy.WifiSupport = TransferPolicy.WifiSupport.UNKNOWN,
                      mediaDirectory: String? = null, verifiedBleFile: Boolean = false, requiresAbc9: Boolean = false) =
        startInput(file, fileName, null, transportPreference = transportPreference, wifiSupport = wifiSupport,
            mediaDirectory = mediaDirectory, verifiedBleFile = verifiedBleFile, requiresAbc9 = requiresAbc9)

    fun startFirmwareTransfer(file: File, firmware: ArkFirmware, legacyHardwareTarget: String? = null,
                              transportPreference: String? = null,
                              wifiSupport: TransferPolicy.WifiSupport = TransferPolicy.WifiSupport.UNKNOWN) =
        startInput(file, "firmware.bin", firmware, legacyHardwareTarget = legacyHardwareTarget,
            transportPreference = transportPreference, wifiSupport = wifiSupport)

    /** Transfer raw bytes to a path relative to the Ark SD-card root. */
    fun startFileTransfer(file: File, targetPath: String, transportPreference: String? = null) =
        startInput(file, file.name, null, targetPath, transportPreference = transportPreference,
            verifiedBleFile = transportPreference == "ble")

    fun confirmFirmwareTransfer() = onMain {
        if (connectedDevice?.address != transferOwnerAddress || !ble.isReady)
            listener.onConnection(ble.isReady, "请重连原 Ark 后只读核对结果")
        else coordinator?.confirmFirmware()
    }

    fun dismissPhotoConfirmation() = onMain {
        when (val attempt = coordinator) {
            is BleTransferCoordinator -> attempt.dismissConfirmation()
            is VerifiedBleFileCoordinator -> attempt.dismissConfirmation()
            else -> Unit
        }
    }

    fun restoreBleFirmware(recovery: BleFirmwareCoordinator.Recovery) = onMain {
        if (closed || coordinator != null || transferActive) return@onMain
        transferOwnerAddress = recovery.deviceAddress
        val attempt = BleFirmwareCoordinator(ble, null, recovery.firmware, recovery.deviceAddress,
            { connectedAddress }, {
                publishTransfer(it, null)
                if (coordinator?.isActive == false) {
                    resumeReadyAfterCleanup()
                    notifyReadyForSettings()
                }
            }, {
                val device = connectedDevice
                if (!closed && !reconnectSuppressed && device?.address == recovery.deviceAddress &&
                    !ble.isReady && !ble.isConnecting) ble.connect(device)
            }, persistBleFirmware, recovery)
        coordinator = attempt
        attempt.start()
    }

    fun acknowledgeBleFirmwareRestart(callback: (Boolean) -> Unit) = onMain {
        val attempt = coordinator as? BleFirmwareCoordinator
        if (attempt == null || connectedAddress != transferOwnerAddress) callback(false)
        else attempt.acknowledgeManualRestart(callback)
    }

    private fun startInput(file: File, fileName: String, firmware: ArkFirmware?, targetPath: String? = null,
                           legacyHardwareTarget: String? = null, transportPreference: String? = null,
                           retrying: Boolean = false,
                           wifiSupport: TransferPolicy.WifiSupport = TransferPolicy.WifiSupport.UNKNOWN,
                           mediaDirectory: String? = null, verifiedBleFile: Boolean = false,
                           requiresAbc9: Boolean = false) = onMain {
        val transport = TransferPolicy.decide(transportPreference, wifiSupport).transport
        val id = ++attemptSequence
        val name = if (transport == "ble" && firmware == null && targetPath == null && !retrying)
            "b%04x.film".format(id and 0xffff) else fileName
        val input = TransferInput(file, name, firmware, targetPath, legacyHardwareTarget, transport, id,
            mediaDirectory, verifiedBleFile, requiresAbc9)
        if (closed || closing) {
            publishTransfer(TransferSnapshot("error", "会话已关闭",
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true), input)
            return@onMain
        }
        if (transferActive || coordinator?.needsConfirmation == true) {
            listener.onConnection(ble.isReady, "已有直传正在进行或升级结果待确认")
            return@onMain
        }
        if (uncertainPhotoFile == file) {
            listener.onConnection(ble.isReady, "上一轮可能已保存此照片，请先核对结果；不能重复发送同一任务")
            return@onMain
        }
        if (transport == "ble" && targetPath != null && !verifiedBleFile) {
            publishTransfer(TransferSnapshot("error", "通用文件/通行证只支持 Wi-Fi",
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true), input)
            return@onMain
        }
        if (transport == "ble" && firmware == null && !verifiedBleFile) {
            val invalid = runCatching { BlePhotoFormat.requireCompatible(file) }.exceptionOrNull()
            if (invalid != null) {
                publishTransfer(TransferSnapshot("error", invalid.message ?: "不支持的 BLE 照片",
                    cleanupCompleted = coordinator?.cleanupConfirmed ?: true), input)
                return@onMain
            }
        }
        if (!file.isFile || file.length() !in 1..0xffffffffL) {
            publishTransfer(TransferSnapshot("error", when { targetPath != null -> "文件不存在或大小无效";
                firmware != null -> "固件文件不存在或大小无效"; else -> "film 文件不存在或大小无效" },
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true), input)
            return@onMain
        }
        if (targetPath != null && !FileTransferPath.isValid(targetPath)) {
            publishTransfer(TransferSnapshot("error", "目标路径须为非空 ASCII SD 相对路径，不能包含 .、.. 或反斜杠",
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true), input)
            return@onMain
        }
        // The confirmation belongs only to this attempt. Retry must supply it again.
        lastInput = input.copy(legacyHardwareTarget = null)
        transferOwnerAddress = connectedDevice?.address
        if (!ble.isReady) {
            publishTransfer(TransferSnapshot("error", "请先连接 Ark", total = file.length(),
                canRetry = true, cleanupCompleted = coordinator?.cleanupConfirmed ?: true))
            return@onMain
        }
        reconnectSuppressed = false
        pendingStart = input
        publishTransfer(TransferSnapshot("preparing", "等待普通 BLE 写入完成", total = file.length(), canCancel = true))
        pump()
    }

    fun cancelTransfer() = onMain {
        val pending = pendingStart
        if (pending != null) {
            pendingStart = null
            publishTransfer(TransferSnapshot("cancelled", "发送直传命令前已取消", total = pending.file.length(),
                canRetry = pending.file.isFile, cleanupCompleted = coordinator?.cleanupConfirmed ?: true))
            if (closing) finishClose()
            else if (disconnectAfterTransfer) { disconnectAfterTransfer = false; ble.close() }
        } else if (settling) cancelAfterSettle = true
        else coordinator?.cancel()
    }

    fun retryTransfer(legacyHardwareTarget: String? = null) = onMain {
        val input = lastInput
        if (input == null || !input.file.isFile) {
            publishTransfer(TransferSnapshot("error", "没有可重试的本地文件",
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true))
        } else if (connectedDevice?.address != transferOwnerAddress || !ble.isReady) {
            listener.onConnection(ble.isReady, "请重连原设备后重试同一任务")
        } else if (lastTransferSnapshot?.canRetry != true) {
            listener.onConnection(ble.isReady, "此任务不能重发；若提交可能已发生，请只核对结果")
        } else startInput(input.file, input.fileName, input.firmware, input.targetPath, legacyHardwareTarget,
            transportPreference = input.transport, retrying = true, mediaDirectory = input.mediaDirectory,
            verifiedBleFile = input.verifiedBleFile, requiresAbc9 = input.requiresAbc9)
    }

    private fun failQueuedWrites(message: String) {
        while (writes.isNotEmpty()) writes.removeFirst().callback(Result.failure(IllegalStateException(message)))
    }

    fun close() = onMain {
        if (closed || closing) return@onMain
        closing = true
        failQueuedWrites("会话已关闭")
        if (settling) { cancelAfterSettle = true; return@onMain }
        pendingStart?.let { pending ->
            pendingStart = null
            publishTransfer(TransferSnapshot("cancelled", "会话关闭前已取消直传", total = pending.file.length(),
                cleanupCompleted = coordinator?.cleanupConfirmed ?: true), pending)
        }
        val attempt = coordinator
        if (attempt?.needsConfirmation == true) finishClose()
        else if (attempt?.isActive == true) attempt.cancel()
        else if (attempt != null && !attempt.cleanupConfirmed) {
            attempt.ensureSettled { _, _ -> finishClose() }
        } else finishClose()
    }

    private fun finishClose() {
        if (closed) return
        closed = true
        ble.close()
    }
}
