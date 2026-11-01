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

fun main() {
    directTerminalAfterEarlyReady()
    stillBusyRequiresCancel()
    failedStatusDoesNotUnlock()
    otherDeviceCannotSettleOldTransfer()
    appliedFirmwareOnlyConfirms()
    secondDisconnectDuringSettlementReportsError()
    closeDuringSettlementClosesBle()
    println("PASS: seven real ArkSession/DirectTransferCoordinator OTA recovery paths")
}
