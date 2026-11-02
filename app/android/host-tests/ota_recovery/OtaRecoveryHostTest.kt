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
    val image = ArkFirmware("3.2.5", "frame_film_ark", elf, "22".repeat(32), file.length())

    init {
        Handler.clear()
        ArkApNetworkController.supported = false
        session.scan()
        session.connect("AA:00")
    }

    fun startOta() {
        session.startFirmwareTransfer(file, image)
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

private fun buildInfo(hash: String): ByteArray {
    val max = 4 * 1024 * 1024
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

fun main() {
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
    println("PASS: seven legacy OTA paths plus AP progress, loss, cancel, reconnect, OTA and credentials")
}
