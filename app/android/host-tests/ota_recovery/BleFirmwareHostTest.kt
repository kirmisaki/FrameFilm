package org.framefilm.ark

import android.bluetooth.BluetoothDevice
import android.content.Context
import android.os.Handler
import java.io.File
import java.security.MessageDigest

private val targetElf = "12".repeat(32)
private val originalElf = "34".repeat(32)
private val otaCaps = byteArrayOf(1, 0, 0, 0x10, 0x40)
private fun otaHardware() = ArkHardware(1, 9, 0, 4L * 1024 * 1024, 2L * 1024 * 1024, "quad", ArkHardware.N4R2)
private fun otaBuild(hash: String = originalElf, project: String = "frame_film_ark", max: Long = 4L * 1024 * 1024) =
    ArkBuildInfo(max, hash, "test", project)
private fun otaBe32(n: Long) = ByteArray(4) { (n ushr (24 - it * 8)).toByte() }
private fun otaBuildBytes(info: ArkBuildInfo) = byteArrayOf(1) + otaBe32(info.otaMax) +
    ArkFirmware.bytes(info.elfSha256) + "${info.version}\u0000${info.project}\u0000".toByteArray()
private fun otaHardwareBytes() = byteArrayOf(1, 0, 9, 0, 0) + otaBe32(4L * 1024 * 1024) +
    otaBe32(2L * 1024 * 1024) + byteArrayOf(1) + "${ArkHardware.N4R2}\u0000".toByteArray()

private class BleOtaFixture(mtu: Int = 200, restoredStage: BleFirmwareCoordinator.Stage? = null) {
    val file = File.createTempFile("ble-ota-host-", ".bin").apply {
        writeBytes(ByteArray(2501) { (it * 17).toByte() }); deleteOnExit()
    }
    val image = ArkFirmware("target", "frame_film_ark", targetElf,
        ArkFirmware.hex(MessageDigest.getInstance("SHA-256").digest(file.readBytes())), file.length(), otaHardware())
    var address = "AA:00"
    var saveAllowed = true
    var applySaveAllowed = true
    var clearAllowed = true
    var saved = restoredStage?.let { BleFirmwareCoordinator.Recovery("AA:00", image, it, originalElf) }
    val persistence = mutableListOf<Pair<BleFirmwareCoordinator.Stage?, List<Int>>>()
    val snapshots = mutableListOf<TransferSnapshot>()
    val latest get() = snapshots.last()
    lateinit var coordinator: BleFirmwareCoordinator
    val ble = ArkBleController(Context(), object : ArkBleController.Listener {
        override fun onDevices(devices: List<BluetoothDevice>) { }
        override fun onReady() = coordinator.onBleReady()
        override fun onPanel(panelId: Int, width: Int, height: Int) { }
        override fun onState(state: String) { }
        override fun onError(message: String) = coordinator.onBleDisconnected()
    })
    init {
        Handler.clear()
        ble.negotiatedMtu = mtu
        coordinator = BleFirmwareCoordinator(ble, if (restoredStage == null) file else null, image,
            "AA:00", { address }, { snapshots += it }, ble::forceReady, { record ->
                persistence += record?.stage to ble.channels.toList()
                val allowed = saveAllowed && (record?.stage != BleFirmwareCoordinator.Stage.APPLY_PENDING || applySaveAllowed) &&
                    (record != null || clearAllowed)
                if (allowed) saved = record
                allowed
            }, saved)
        ble.forceReady()
    }
    fun start(build: ArkBuildInfo = otaBuild(), hardware: ByteArray = otaHardwareBytes()) {
        coordinator.start()
        ble.reply(0x59, otaCaps)
        ble.reply(0x5D, hardware)
        if (ble.pending?.channel == 0x56) ble.reply(0x56, otaBuildBytes(build))
    }
    fun ack(channel: Int, offset: Long, status: Int = 0) =
        ble.reply(channel, byteArrayOf(status.toByte()) + otaBe32(offset))
    fun untilFinish() {
        ack(0x6A, 0)
        var offset = 0L
        while (ble.pending?.channel == 0x6B) {
            val chunk = ble.requests.last().payload
            check(chunk.copyOf(4).contentEquals(otaBe32(offset)))
            offset += chunk.size - 4
            ack(0x6B, offset)
            check(Handler.runWithDelay(0))
        }
        check(ble.pending?.channel == 0x6C && offset == image.size)
    }
    fun confirm(info: ArkBuildInfo = otaBuild(targetElf)) = ble.reply(0x56, otaBuildBytes(info))
}

private fun successfulOta(mtu: Int) {
    val f = BleOtaFixture(mtu)
    f.start()
    check(f.ble.channels == listOf(0x59, 0x5D, 0x56, 0x6A))
    check(f.ble.requests.last().payload.contentEquals(otaBe32(f.image.size) + ArkFirmware.bytes(f.image.fileSha256)))
    f.untilFinish()
    val data = f.ble.requests.filter { it.channel == 0x6B }
    check(data.all { it.payload.size in 5..minOf(192, mtu - 7) })
    check(data.flatMap { it.payload.drop(4) }.toByteArray().contentEquals(f.file.readBytes()))
    check(f.latest.received == f.image.size && !f.latest.success && f.latest.canCancel)
    f.ack(0x6C, f.image.size)
    check(f.ble.pending?.channel == 0x6D && !f.latest.success && !f.latest.canCancel)
    check(f.persistence[0] == BleFirmwareCoordinator.Stage.STAGING to listOf(0x59, 0x5D, 0x56))
    check(f.persistence[1].first == BleFirmwareCoordinator.Stage.APPLY_PENDING && 0x6D !in f.persistence[1].second)
    f.ack(0x6D, f.image.size)
    check(!f.latest.success && Handler.runWithDelay(3000))
    f.confirm()
    check(f.latest.success && f.latest.cleanupCompleted && f.latest.confirmation == "elf_sha256" && f.saved == null)
    f.coordinator.start()
    check(f.ble.channels.count { it == 0x6D } == 1 && f.ble.sent.isEmpty())
}

private fun preflightRejectsIneligibleFirmware() {
    val missingCap = BleOtaFixture()
    missingCap.coordinator.start()
    missingCap.ble.reply(0x59, byteArrayOf(1, 0, 0, 8, 0x40))
    check(missingCap.latest.phase == "error" && missingCap.ble.channels == listOf(0x59))
    val smallMtu = BleOtaFixture(42)
    smallMtu.coordinator.start()
    check(smallMtu.latest.phase == "error" && smallMtu.ble.channels.isEmpty())
    val wrongHardware = BleOtaFixture()
    wrongHardware.start(hardware = otaHardwareBytes().also { it[13] = 2 })
    check(wrongHardware.latest.phase == "error" && 0x6A !in wrongHardware.ble.channels)
    val tooLarge = BleOtaFixture()
    tooLarge.start(otaBuild(max = tooLarge.image.size - 1))
    check(tooLarge.latest.phase == "error" && 0x6A !in tooLarge.ble.channels)
    val same = BleOtaFixture()
    same.start(otaBuild(targetElf))
    check(same.latest.success && 0x6A !in same.ble.channels)
}

private fun stagingFailureAndCancel() {
    val noBegin = BleOtaFixture()
    noBegin.saveAllowed = false
    noBegin.start()
    check(0x6A !in noBegin.ble.channels && noBegin.latest.cleanupCompleted)
    val cancel = BleOtaFixture()
    cancel.start(); cancel.ack(0x6A, 0)
    cancel.coordinator.cancel()
    check(cancel.ble.pending?.channel == 0x6B && !cancel.latest.canCancel)
    cancel.ack(0x6B, 188)
    cancel.ack(0x6E, 0)
    check(cancel.latest.phase == "cancelled" && cancel.latest.cleanupCompleted && cancel.saved == null)
    check(0x6C !in cancel.ble.channels && 0x6D !in cancel.ble.channels)
    val wrongOffset = BleOtaFixture()
    wrongOffset.start(); wrongOffset.ack(0x6A, 0); wrongOffset.ack(0x6B, 1)
    check(wrongOffset.latest.phase == "interrupted" && wrongOffset.saved?.stage == BleFirmwareCoordinator.Stage.STAGING)
    check(wrongOffset.ble.channels.count { it == 0x6B } == 1 && 0x6D !in wrongOffset.ble.channels)
    wrongOffset.ble.forceReady()
    wrongOffset.coordinator.ensureSettled { ready, _ -> check(ready && wrongOffset.saved == null) }
    val rejectedFinish = BleOtaFixture()
    rejectedFinish.start(); rejectedFinish.untilFinish(); rejectedFinish.ack(0x6C, rejectedFinish.image.size, status = 3)
    check(rejectedFinish.latest.phase == "interrupted" && 0x6D !in rejectedFinish.ble.channels)
    val noApply = BleOtaFixture()
    noApply.applySaveAllowed = false
    noApply.start(); noApply.untilFinish(); noApply.ack(0x6C, noApply.image.size)
    check(noApply.latest.phase == "interrupted" && 0x6D !in noApply.ble.channels)
}

private fun applyFailureAndRecovery() {
    val failed = BleOtaFixture()
    failed.start(); failed.untilFinish(); failed.ack(0x6C, failed.image.size)
    failed.ack(0x6D, failed.image.size, status = 3)
    check(failed.latest.phase == "interrupted" && !failed.latest.canConfirm)
    val lost = BleOtaFixture()
    lost.start(); lost.untilFinish(); lost.ack(0x6C, lost.image.size)
    lost.ble.fail(0x6D)
    check(lost.latest.phase == "unconfirmed" && lost.latest.canConfirm && !lost.latest.canRetry)
    lost.address = "BB:00"; lost.ble.forceReady(); lost.coordinator.confirmFirmware()
    check(lost.ble.pending == null && !lost.latest.success)
    lost.address = "AA:00"; lost.ble.forceReady(); lost.coordinator.confirmFirmware()
    lost.confirm(otaBuild())
    check(!lost.latest.success && lost.saved?.stage == BleFirmwareCoordinator.Stage.APPLY_PENDING)
    lost.ble.forceReady(); lost.coordinator.confirmFirmware(); lost.confirm()
    check(lost.latest.success && lost.saved == null && lost.ble.channels.count { it == 0x6D } == 1)
    val staged = BleOtaFixture(restoredStage = BleFirmwareCoordinator.Stage.STAGING)
    staged.coordinator.start()
    staged.coordinator.ensureSettled { ready, _ -> check(ready && staged.saved == null) }
    check(staged.ble.channels.isEmpty())
    val applied = BleOtaFixture(restoredStage = BleFirmwareCoordinator.Stage.APPLY_PENDING)
    applied.coordinator.start(); applied.coordinator.confirmFirmware(); applied.confirm()
    check(applied.latest.success && applied.ble.channels == listOf(0x56))
    val legacy = BleOtaFixture(restoredStage = BleFirmwareCoordinator.Stage.LEN_STARTED)
    legacy.coordinator.start()
    legacy.coordinator.ensureSettled { ready, _ -> check(!ready) }
    legacy.coordinator.acknowledgeManualRestart { check(it) }
    legacy.confirm(otaBuild())
    check(legacy.latest.cleanupCompleted && legacy.saved == null)
}

fun main() {
    successfulOta(200)
    successfulOta(43)
    preflightRejectsIneligibleFirmware()
    stagingFailureAndCancel()
    applyFailureAndRecovery()
    println("Verified BLE OTA ordering, failure and recovery host checks passed")
}
