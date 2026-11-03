package org.framefilm.ark

import android.content.Context
import android.os.Handler
import java.io.File

private val elf = "11".repeat(32)
private val otherElf = "00".repeat(32)

private class Recorder : ArkSession.Listener {
    val connections = mutableListOf<Boolean>()
    val transfers = mutableListOf<TransferSnapshot>()
    override fun onConnection(connected: Boolean, message: String) { connections += connected }
    override fun onTransfer(snapshot: TransferSnapshot) { transfers += snapshot }
    fun latest() = transfers.last()
}

private class Fixture {
    val recorder = Recorder()
    val session = ArkSession(Context(), recorder)
    val ble get() = ArkBleController.current
    val p2p get() = ArkP2pController.current
    val file: File = File.createTempFile("ota-host-", ".bin").apply {
        writeBytes(ByteArray(64) { it.toByte() })
        deleteOnExit()
    }
    val image = ArkFirmware("3.2.5", "frame_film_ark", elf, "22".repeat(32), file.length(), boardHardware())

    init {
        Handler.clear()
        ArkApNetworkController.supported = false
        session.scan()
        session.connect("AA:00")
    }

    fun startOta() {
        session.startFirmwareTransfer(file, image)
        ble.reply(0x5D, hardwareInfo())
        ble.reply(0x56, buildInfo(otherElf))
        ble.reply(0x42, byteArrayOf(2, 2, 0xd0.toByte(), 1, 0xe0.toByte()))
        for (channel in listOf(0x31, 0x33, 0x35, 0x37)) ble.reply(channel, byteArrayOf(1))
        check(ble.pending?.channel == 0x57)
        ble.reply(0x57, byteArrayOf(0))
        check(ble.pending?.channel == 0x51)
        recorder.connections.clear()
    }

    fun disconnectAtErase() {
        ble.disconnectNow()
        check(p2p.closeCallback != null)
        check(recorder.latest().phase == "cleanup")
    }

    fun terminal(state: Int): ByteArray {
        val n = image.size.toInt()
        return byteArrayOf(state.toByte(), 100.toByte(), 0,
            (n ushr 24).toByte(), (n ushr 16).toByte(), (n ushr 8).toByte(), n.toByte(),
            (n ushr 24).toByte(), (n ushr 16).toByte(), (n ushr 8).toByte(), n.toByte())
    }

    fun startAp(ota: Boolean = false, generic: Boolean = false) {
        ArkApNetworkController.supported = true
        if (ota) {
            session.startFirmwareTransfer(file, image)
            ble.reply(0x5D, hardwareInfo())
        ble.reply(0x56, buildInfo(otherElf))
        } else if (generic) session.startFileTransfer(file, "apps/passport/data/fixture.bin")
        else session.startTransfer(file, "fixture.film")
        ble.reply(0x59, byteArrayOf(1, 0, 0, 0, 0x12))
        ble.reply(0x42, byteArrayOf(2, 2, 0xd0.toByte(), 1, 0xe0.toByte()))
        for (channel in listOf(0x31, 0x33, 0x35, 0x37)) ble.reply(channel, byteArrayOf(1))
        ble.reply(0x5C, byteArrayOf(0) + "ARK-FILE-TEST\u000012345678\u0000192.168.4.1\u0000".toByteArray())
        ble.reply(if (ota) 0x57 else if (generic) 0x5A else 0x50, byteArrayOf(0))
        recorder.connections.clear()
    }

    fun finishSnapshot() {
        for (channel in listOf(0x31, 0x33, 0x35, 0x37)) ble.reply(channel, byteArrayOf(1))
    }
}

private fun buildInfo(hash: String, max: Int = 4 * 1024 * 1024): ByteArray {
    return byteArrayOf(1, (max ushr 24).toByte(), (max ushr 16).toByte(),
        (max ushr 8).toByte(), max.toByte()) + ArkFirmware.bytes(hash) +
        "3.2.5\u0000frame_film_ark\u0000".toByteArray()
}

private fun directTerminalAfterEarlyReady() {
    val f = Fixture()
    f.startOta()
    val before = f.recorder.latest()
    f.disconnectAtErase()
    f.ble.forceReady() // Ready precedes the asynchronous P2P close callback.
    check(f.recorder.connections.none { it })
    check(f.ble.pending == null)
    f.p2p.finishClose(true)
    check(f.recorder.latest().phase == "cleanup" && !f.recorder.latest().cleanupCompleted)
    check(f.session.transferActive && !f.recorder.latest().canRetry)
    check(f.recorder.connections.none { it })
    check(f.ble.pending?.channel == 0x51)
    f.ble.reply(0x51, f.terminal(4))
    val after = f.recorder.latest()
    check(after.phase == "error" && after.cleanupCompleted && after.canRetry)
    check(after.kind == "firmware" && after.total == before.total && after.received == before.received)
    check(after.targetBuild == elf && f.recorder.connections.last() == true)
    check(0x52 !in f.ble.channels)
}

private fun stillBusyRequiresCancel() {
    val f = Fixture()
    f.startOta()
    f.disconnectAtErase()
    f.p2p.finishClose(true)
    f.ble.forceReady()
    check(f.ble.pending?.channel == 0x51)
    f.ble.reply(0x51, f.terminal(2))
    check(f.ble.pending?.channel == 0x52)
    f.ble.reply(0x52, byteArrayOf(0))
    check(f.ble.pending?.channel == 0x51)
    f.ble.reply(0x51, f.terminal(4))
    check(f.recorder.latest().cleanupCompleted)
    check(f.recorder.connections.last() == true)
}

private fun failedStatusDoesNotUnlock() {
    val f = Fixture()
    f.startOta()
    f.disconnectAtErase()
    f.p2p.finishClose(true)
    f.ble.forceReady()
    f.ble.fail(0x51)
    check(f.recorder.latest().phase == "error")
    check(!f.recorder.latest().cleanupCompleted)
    check(f.recorder.latest().canRetry)
    check(0x52 !in f.ble.channels)
}

private fun otherDeviceCannotSettleOldTransfer() {
    val f = Fixture()
    f.startOta()
    f.disconnectAtErase()
    f.p2p.finishClose(true)
    val count = f.ble.connectCount
    f.session.scan()
    f.session.connect("BB:00")
    check(f.ble.connectCount == count)
    check(!f.ble.isReady)
    f.session.connect("AA:00")
    check(f.ble.pending?.channel == 0x51)
    f.ble.reply(0x51, f.terminal(4))
    check(f.recorder.latest().cleanupCompleted)
}

private fun appliedFirmwareOnlyConfirms() {
    val f = Fixture()
    f.startOta()
    f.ble.reply(0x51, f.terminal(7))
    for (channel in listOf(0x31, 0x33, 0x35, 0x37)) f.ble.reply(channel, byteArrayOf(1))
    check(f.p2p.closeCallback != null)
    f.p2p.finishClose(true)
    check(f.ble.pending?.channel == 0x58)
    f.ble.reply(0x58, byteArrayOf(0))
    check(f.recorder.latest().phase == "rebooting")
    f.ble.disconnectNow()
    f.ble.forceReady()
    check(f.ble.pending?.channel == 0x56)
    check(0x52 !in f.ble.channels)
    f.ble.reply(0x56, buildInfo(elf))
    check(f.p2p.closeCallback != null)
    f.p2p.finishClose(true)
    check(f.recorder.latest().phase == "done" && f.recorder.latest().success)
    check(0x52 !in f.ble.channels)
}

private fun secondDisconnectDuringSettlementReportsError() {
    val f = Fixture()
    f.startOta()
    f.disconnectAtErase()
    f.p2p.finishClose(true)
    f.ble.forceReady()
    check(f.recorder.latest().phase == "cleanup")
    check(f.ble.pending?.channel == 0x51)
    f.ble.disconnectNow()
    check(f.recorder.latest().phase == "error")
    check(!f.recorder.latest().cleanupCompleted)
    check(f.recorder.latest().canRetry)
}

private fun closeDuringSettlementClosesBle() {
    val f = Fixture()
    f.startOta()
    f.disconnectAtErase()
    f.p2p.finishClose(true)
    f.ble.forceReady()
    check(f.recorder.latest().phase == "cleanup")
    check(f.ble.pending?.channel == 0x51)
    f.session.close()
    check(f.ble.closeCount == 0)
    f.ble.reply(0x51, f.terminal(4))
    check(f.ble.closeCount == 1)
    check(!f.ble.isReady)
    check(f.recorder.connections.none { it })
}

private fun apSentBytesAreNotCommitAndNetworkLossIsNotFailure() {
    val f = Fixture()
    f.startAp()
    f.ble.disconnectNow()
    check(f.session.transferActive && f.recorder.connections.isEmpty())
    FilmHttpServer.current.writtenBytes = f.file.length()
    FilmHttpServer.current.transferFinished = true
    check(Handler.runWithDelay(800))
    check(!f.recorder.latest().success && "已发送" in f.recorder.latest().message)
    ArkApNetworkController.current.loseNetwork()
    check(f.session.transferActive && !FilmHttpServer.current.closed)
    f.ble.forceReady()
    f.ble.reply(0x51, f.terminal(4))
    f.finishSnapshot()
    check(f.recorder.latest().success && f.recorder.latest().cleanupCompleted)
    check(ArkApNetworkController.current.closed)
    check(0x52 !in f.ble.channels)
}

private fun apLateCancelReportsCommittedFile() {
    val f = Fixture()
    f.startAp()
    f.ble.disconnectNow()
    f.session.cancelTransfer()
    check(FilmHttpServer.current.closed && !ArkApNetworkController.current.closed)
    f.ble.forceReady()
    f.ble.reply(0x51, f.terminal(4))
    f.finishSnapshot()
    check(f.recorder.latest().success && "未撤销" in f.recorder.latest().message)
}

private fun apCancelledTransferWaitsForDevice() {
    val f = Fixture()
    f.startAp()
    f.ble.disconnectNow()
    f.session.cancelTransfer()
    check(f.session.transferActive && !ArkApNetworkController.current.closed)
    f.ble.forceReady()
    f.ble.reply(0x51, f.terminal(6))
    check(f.recorder.latest().phase == "cancelled" && f.recorder.latest().cleanupCompleted)
}

private fun apReconnectDoesNotRestartConnectionInFlight() {
    val f = Fixture()
    f.startAp()
    f.ble.disconnectNow()
    FilmHttpServer.current.transferFinished = true
    f.ble.autoReady = false
    check(Handler.runWithDelay(800))
    val initial = f.ble.connectCount
    check(Handler.runWithDelay(3000))
    check(f.ble.isConnecting && f.ble.connectCount == initial + 1)
    repeat(3) { check(Handler.runWithDelay(800)) }
    check(f.ble.connectCount == initial + 1)
}

private fun apCloseStillReconnectsToConfirmCleanup() {
    val f = Fixture()
    f.startAp()
    f.ble.disconnectNow()
    f.ble.autoReady = false
    f.session.close()
    check(FilmHttpServer.current.closed && !ArkApNetworkController.current.closed)
    check(Handler.runWithDelay(800))
    check(Handler.runWithDelay(3000))
    check(f.ble.isConnecting)
    f.ble.forceReady()
    f.ble.reply(0x51, f.terminal(6))
    check(f.recorder.latest().cleanupCompleted && ArkApNetworkController.current.closed)
    check(f.ble.closeCount == 1 && f.recorder.connections.none { it })
}

private fun apOtaRestoresBleBeforeApplyAndElfConfirmation() {
    val f = Fixture()
    f.startAp(ota = true)
    f.ble.disconnectNow()
    f.ble.forceReady()
    f.ble.reply(0x51, f.terminal(7))
    f.finishSnapshot()
    check(f.ble.pending?.channel == 0x58 && ArkApNetworkController.current.closed)
    f.ble.reply(0x58, byteArrayOf(0))
    f.ble.disconnectNow()
    f.ble.forceReady()
    f.ble.reply(0x56, buildInfo(elf))
    check(f.recorder.latest().success)
    check(0x52 !in f.ble.channels)
}

private fun apGenericFileUsesExistingStartAndTerminal() {
    val f = Fixture()
    f.startAp(generic = true)
    check(0x5A in f.ble.channels && 0x50 !in f.ble.channels)
    f.ble.disconnectNow()
    f.ble.forceReady()
    f.ble.reply(0x51, f.terminal(4))
    f.finishSnapshot()
    check(f.recorder.latest().success && f.recorder.latest().kind == "file")
}

private fun oldFirmwareOnNewAndroidStillUsesP2p() {
    val f = Fixture()
    ArkApNetworkController.supported = true
    f.session.startTransfer(f.file, "fixture.film")
    f.ble.reply(0x59, byteArrayOf(1, 0, 0, 0, 2))
    f.ble.reply(0x42, byteArrayOf(2, 2, 0xd0.toByte(), 1, 0xe0.toByte()))
    for (channel in listOf(0x31, 0x33, 0x35, 0x37)) f.ble.reply(channel, byteArrayOf(1))
    check(f.ble.pending?.channel == 0x50 && 0x5C !in f.ble.channels)
}

private fun apPreparePayloadValidation() {
    val valid = byteArrayOf(0) + "ARK-FILE-TEST\u000012345678\u0000192.168.4.1\u0000".toByteArray()
    check(ArkApSession.parse(valid).deviceAddress.hostAddress == "192.168.4.1")
    for (invalid in listOf(byteArrayOf(), byteArrayOf(1), valid.dropLast(1).toByteArray(),
        byteArrayOf(0) + "SSID\u000012345678\u0000256.1.1.1\u0000".toByteArray())) {
        check(runCatching { ArkApSession.parse(invalid) }.isFailure)
    }
}

private fun abc9Bytes(): ByteArray = ByteArray(Abc9Preview.FILE_BYTES).also { bytes ->
    java.nio.ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.LITTLE_ENDIAN).apply {
        putInt(Abc9Preview.BODY_BYTES); putShort(720); putShort(480)
        put(0); put(0x7F); putShort(1); put(byteArrayOf(65, 66, 67, 57))
    }
    for (pixel in 0 until 720 * 480) {
        bytes[32 + pixel * 2] = (pixel % 512).toByte()
        bytes[33 + pixel * 2] = ((pixel % 512) ushr 8).toByte()
    }
}

private fun abc9ContainerValidation() {
    val file = File.createTempFile("abc9-host-", ".film")
    try {
        val bytes = abc9Bytes()
        file.writeBytes(bytes)
        check(Abc9Preview.isFilm(file))
        Abc9Preview.validate(file)
        val invalid = listOf<Pair<Int, Int>>(0 to 1, 4 to 1, 6 to 1, 9 to 4,
            10 to 0, 10 to 2, 11 to 1, 12 to 88, 33 to 2, bytes.lastIndex to 0x80)
        for ((offset, value) in invalid) {
            val changed = bytes.copyOf().also { it[offset] = value.toByte() }
            file.writeBytes(changed)
            check(runCatching { Abc9Preview.validate(file) }.isFailure) { "Accepted invalid offset $offset" }
        }
        for (changed in listOf(bytes.copyOf(bytes.size - 2), bytes.copyOf(bytes.size + 2))) {
            file.writeBytes(changed)
            check(runCatching { Abc9Preview.validate(file) }.isFailure)
        }
        check(Abc9Preview.supported(byteArrayOf(1, 0, 0, 0, 0x20)))
        check(Abc9Preview.supported(byteArrayOf(1, 0, 0, 0, 0x3F)))
        for (cap in listOf(null, byteArrayOf(), byteArrayOf(1, 0, 0, 0, 31),
            byteArrayOf(2, 0, 0, 0, 0x20), byteArrayOf(1, 0, 0, 0x20))) {
            check(!Abc9Preview.supported(cap))
        }
    } finally { file.delete() }
}

private fun abc9TransfersAndRetriesQueryLiveCapability() {
    for (apSupported in listOf(false, true)) {
        val f = Fixture()
        ArkApNetworkController.supported = apSupported
        f.file.writeBytes(abc9Bytes())
        f.session.startTransfer(f.file, "preview.film")
        check(f.ble.pending?.channel == 0x59)
        f.ble.reply(0x59, byteArrayOf(1, 0, 0, 0, 31))
        f.p2p.finishClose(true)
        check(f.recorder.latest().phase == "error" && f.recorder.latest().cleanupCompleted)
        check(f.ble.channels == listOf(0x59)) // No panel/network/start command before confirmation.
        f.session.retryTransfer()
        check(f.ble.pending?.channel == 0x59 && f.ble.channels.count { it == 0x59 } == 2)
        f.ble.reply(0x59, byteArrayOf(1, 0, 0, 0, 0x3F))
        check(f.ble.pending?.channel == 0x42)
        check(f.ble.channels.count { it == 0x59 } == 2) // Reuse this reply for AP selection.
    }
    val old = Fixture()
    ArkApNetworkController.supported = false
    old.file.writeBytes(abc9Bytes())
    old.session.startTransfer(old.file, "preview.film")
    old.ble.fail(0x59)
    old.p2p.finishClose(true)
    check(old.recorder.latest().phase == "error" && old.recorder.latest().cleanupCompleted)
    check(old.ble.channels == listOf(0x59))
}


private fun boardHardware(id: String = ArkHardware.N4R2) = ArkHardware(1, 9, 3,
    if(id == ArkHardware.N16R8) 16777216 else 4194304,
    if(id == ArkHardware.N16R8) 8388608 else 2097152,
    if(id == ArkHardware.N16R8) "octal" else "quad", id)
private fun hardwareInfo(id: String = ArkHardware.N4R2): ByteArray {
    val board=boardHardware(id)
    return java.nio.ByteBuffer.allocate(14).put(1).putShort(9).putShort(3)
        .putInt(board.flashBytes.toInt()).putInt(board.psramBytes.toInt())
        .put(if(board.psramBus == "octal") 2 else 1).array() + id.toByteArray() + byteArrayOf(0)
}
private fun hardwareTargetsAndRetriesCannotGuess() {
    for(id in listOf(ArkHardware.N4R2, ArkHardware.N16R8)) {
        val f=Fixture()
        val image=f.image.copy(hardware=boardHardware(id))
        val wrong=if(id == ArkHardware.N4R2) ArkHardware.N16R8 else ArkHardware.N4R2
        f.session.startFirmwareTransfer(f.file,image)
        check(f.ble.pending?.channel == 0x5D)
        f.ble.reply(0x5D,hardwareInfo(wrong))
        f.p2p.finishClose(true)
        check(f.recorder.latest().phase == "error" && f.recorder.latest().message.contains("目标不匹配"))
        check(f.ble.channels == listOf(0x5D))
        f.session.retryTransfer()
        check(f.ble.pending?.channel == 0x5D && f.ble.channels.count{it==0x5D}==2)
        f.ble.reply(0x5D,hardwareInfo(id))
        check(f.ble.pending?.channel == 0x56)
        // otaMax must also be obtained afresh, even after matching target on retry.
        f.ble.reply(0x56,buildInfo(otherElf,max=1))
        f.p2p.finishClose(true)
        check(f.recorder.latest().message.contains("OTA 分区"))
        check(f.ble.channels.none{it in listOf(0x57,0x5C)})
    }
    val old=Fixture()
    old.session.startFirmwareTransfer(old.file,old.image)
    old.ble.fail(0x5D)
    old.p2p.finishClose(true)
    check(old.recorder.latest().message.contains("一次对应型号的识别升级"))
    old.session.retryTransfer()
    check(old.ble.pending?.channel == 0x5D)
    old.ble.disconnectNow()
    old.p2p.finishClose(true)
    check(0x56 !in old.ble.channels)
    val missing=Fixture()
    missing.session.startFirmwareTransfer(missing.file,missing.image.copy(hardware=null))
    missing.p2p.finishClose(true)
    check(missing.ble.channels.isEmpty() && missing.recorder.latest().message.contains("FFARKHW1"))
    missing.session.startTransfer(missing.file,"plain.film")
    check(missing.ble.pending?.channel == 0x42) // Ordinary photo path remains available.
}

fun main() {
    hardwareTargetsAndRetriesCannotGuess()
    directTerminalAfterEarlyReady()
    stillBusyRequiresCancel()
    failedStatusDoesNotUnlock()
    otherDeviceCannotSettleOldTransfer()
    appliedFirmwareOnlyConfirms()
    secondDisconnectDuringSettlementReportsError()
    closeDuringSettlementClosesBle()
    apSentBytesAreNotCommitAndNetworkLossIsNotFailure()
    apLateCancelReportsCommittedFile()
    apCancelledTransferWaitsForDevice()
    apReconnectDoesNotRestartConnectionInFlight()
    apCloseStillReconnectsToConfirmCleanup()
    apOtaRestoresBleBeforeApplyAndElfConfirmation()
    apGenericFileUsesExistingStartAndTerminal()
    oldFirmwareOnNewAndroidStillUsesP2p()
    apPreparePayloadValidation()
    abc9ContainerValidation()
    abc9TransfersAndRetriesQueryLiveCapability()
    println("PASS: live hardware target/otaMax checks, cross-target rejection/retry/disconnect, missing-descriptor refusal, legacy OTA/AP recovery and ABC9 capability gating")
}
