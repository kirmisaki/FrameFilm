package org.framefilm.ark

import android.content.Context
import android.os.Handler
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

private val panel = byteArrayOf(2, 2, 0xd0.toByte(), 1, 0xe0.toByte())

/** Byte-for-byte header produced by packFramePixels's default portrait 480x720 input. */
private fun photo(mono: Boolean = true): File = File.createTempFile("ble-photo-", ".film").apply {
    val size = 720 * 480 / if (mono) 8 else 2
    val bytes = ByteArray(32 + size)
    ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).apply {
        putInt(size); putShort(720.toShort()); putShort(480.toShort())
        put(if (mono) 2.toByte() else 6.toByte()); put(if (mono) 1.toByte() else 0.toByte()); putShort(1)
    }
    if (!mono) byteArrayOf(0, 255.toByte(), 252.toByte(), 224.toByte(), 3, 28).copyInto(bytes, 16)
    for (n in 32 until bytes.size) bytes[n] = if (mono) (n and 255).toByte() else 0x01
    writeBytes(bytes); deleteOnExit()
}
private fun absent() = byteArrayOf(4, 0, 0, 0, 0)
private fun entry(name: String) = byteArrayOf(0, 0, 0, 0, 1) + name.toByteArray() + byteArrayOf(0)

private class PhotoFixture(val file: File = photo()) {
    val snapshots = mutableListOf<TransferSnapshot>()
    val session: ArkSession
    val ble get() = ArkBleController.current
    val latest get() = snapshots.last()
    init {
        Handler.clear()
        ArkApNetworkController.supported = false
        session = ArkSession(Context(), object : ArkSession.Listener {
            override fun onTransfer(snapshot: TransferSnapshot) { snapshots += snapshot }
        })
        session.scan(); session.connect("AA:00")
    }
    fun start(catalog: Boolean = true, mtu: Int = 200, before: Boolean = true) {
        ble.negotiatedMtu = mtu
        session.startTransfer(file, "original.film", "ble")
        ble.reply(0x42, panel)
        ble.reply(0x4C, byteArrayOf(0))
        ble.reply(0x59, byteArrayOf(1, 0, 0, 0, if (catalog) 4 else 0))
        if (catalog && before) ble.reply(0x5B, absent())
    }
    val name get() = ble.sent.firstOrNull { it.channel == 0x00 }?.payload
        ?.dropLast(1)?.toByteArray()?.toString(Charsets.US_ASCII)
        ?: "b%04x.film".format(latest.attemptId!! and 0xffff)

    fun untilStop(stopBeforeFinalPage: Boolean = false) {
        var rounds = 0
        while (ble.sent.none { it.channel == 0x04 }) {
            check(++rounds < 40_000)
            when (ble.pending?.channel) {
                0x42 -> ble.reply(0x42, panel)
                0x4C -> {
                    if (stopBeforeFinalPage) return
                    ble.reply(0x4C, byteArrayOf(0))
                }
                null -> check(Handler.runWithDelay(12) || Handler.runWithDelay(50)) { "No data pacing work at round $rounds" }
                else -> error("Unexpected command ${ble.pending?.channel}")
            }
        }
    }
    fun beginVerify() {
        check(Handler.runWithDelay(50))
        ble.reply(0x42, panel)
        check(Handler.runWithDelay(800))
        check(ble.pending?.channel == 0x5B)
    }
}

private fun normalPhoto(mono: Boolean, mtu: Int) {
    val f = PhotoFixture(photo(mono))
    f.start(mtu = mtu)
    val attempt = f.latest.attemptId
    f.untilStop()
    val sent = f.ble.sent
    check(sent.take(3).map { it.channel } == listOf(3, 0, 1))
    check(sent.last().channel == 4 && sent.count { it.channel == 4 } == 1)
    val bytes = sent.filter { it.channel == 2 }.flatMap { it.payload.toList() }.toByteArray()
    check(bytes.contentEquals(f.file.readBytes()))
    check(sent.all { it.payload.size <= minOf(192, mtu - 7) })
    check(f.ble.channels.count { it == 0x42 } >= bytes.size / minOf(192, mtu - 7) / 32)
    check(!f.latest.success && f.latest.phase == "verifying")
    f.beginVerify()
    f.ble.reply(0x5B, entry(f.name))
    check(f.latest.phase == "done" && f.latest.success && f.latest.cleanupCompleted)
    check(f.latest.confirmation == "directory_presence" && f.latest.transport == "ble")
    check(f.snapshots.all { it.attemptId == attempt })
    check(f.ble.channels.none { it in listOf(0x50, 0x57, 0x5C, 0x10, 0x11, 0x12, 0x13) })
}

private fun absenceIsNotSuccess() {
    val f = PhotoFixture()
    f.start(); f.untilStop(); f.beginVerify()
    repeat(4) {
        f.ble.reply(0x5B, absent())
        if (it < 3) check(Handler.runWithDelay(1000))
    }
    check(f.latest.phase == "unconfirmed" && !f.latest.success && !f.latest.canRetry)
    val writes = f.ble.sent.size
    f.session.retryTransfer()
    f.session.startTransfer(f.file, "repeat.film", "ble")
    check(f.ble.sent.size == writes && f.ble.pending == null)
    // Only the explicit confirmation operation may query again; it never resends bytes.
    f.session.confirmFirmwareTransfer()
    f.ble.reply(0x5B, entry(f.name))
    check(f.latest.success && f.ble.sent.size == writes)
}

private fun unavailableCatalogIsUnconfirmed() {
    val f = PhotoFixture()
    f.start(catalog = false); f.untilStop(); check(Handler.runWithDelay(50)); f.ble.reply(0x42, panel)
    check(f.latest.phase == "unconfirmed" && !f.latest.success && !f.latest.canRetry && !f.latest.canConfirm)
    check(0x5B !in f.ble.channels)
}

private fun oldNameCannotConfirm() {
    val f = PhotoFixture()
    f.start(before = false)
    f.ble.reply(0x5B, entry(f.name))
    check(f.ble.sent.isEmpty() && f.latest.phase == "error" && !f.latest.success)
}

private fun templateIsBlocked() {
    val f = PhotoFixture()
    f.session.startTransfer(f.file, "photo.film", "ble")
    f.ble.reply(0x42, panel); f.ble.reply(0x4C, byteArrayOf(1))
    check(f.latest.phase == "error" && f.ble.sent.isEmpty())
}

private fun cancelAndLateWriteCannotAdvance() {
    val f = PhotoFixture()
    f.ble.autoCompleteWrites = false
    f.start()
    val late = requireNotNull(f.ble.writePending)
    val oldId = f.latest.attemptId
    f.session.cancelTransfer()
    check(f.latest.phase == "cancelled" && !f.latest.cleanupCompleted)
    check(f.ble.sent.map { it.channel } == listOf(3))
    late(Result.success(Unit))
    check(f.ble.sent.size == 1)
    f.ble.forceReady()
    check(f.latest.cleanupCompleted)
    f.session.retryTransfer()
    check(f.latest.transport == "ble" && f.latest.attemptId != oldId)
    check(f.ble.pending?.channel == 0x42)
}

private fun stopWriteFailureNeverResends() {
    val f = PhotoFixture()
    f.start(); f.untilStop(stopBeforeFinalPage = true)
    f.ble.autoCompleteWrites = false
    f.ble.reply(0x4C, byteArrayOf(0)) // STOP might have reached the device, even if its callback fails.
    check(f.ble.sent.last().channel == 4)
    f.ble.completeWrite(Result.failure(IllegalStateException("STOP callback lost")))
    check(f.latest.phase == "unconfirmed" && !f.latest.canRetry && !f.latest.success)
    val writes = f.ble.sent.size
    f.session.retryTransfer()
    check(f.ble.sent.size == writes)
}

private fun busyPacketStopsStream() {
    val f = PhotoFixture()
    f.start()
    f.ble.notifyPacket(byteArrayOf(0x55, 2, 1, 1, 0x59))
    check(f.latest.phase == "error" && !f.latest.success && !f.latest.cleanupCompleted)
    val count = f.ble.sent.size
    Handler.runWithDelay(12)
    check(f.ble.sent.size == count && f.ble.sent.none { it.channel == 4 })
}

private fun dismissAndWrongDeviceCannotResend() {
    val f = PhotoFixture()
    f.start(); f.untilStop(); f.beginVerify()
    repeat(4) {
        f.ble.reply(0x5B, absent())
        if (it < 3) check(Handler.runWithDelay(1000))
    }
    check(f.latest.canConfirm)
    f.session.connect("BB:00")
    check(f.ble.pending == null)
    f.session.confirmFirmwareTransfer()
    check(f.ble.pending == null) // Not the attempt owner.
    val count = f.ble.sent.size
    f.session.dismissPhotoConfirmation()
    check(!f.latest.canConfirm && f.latest.phase == "unconfirmed" && !f.latest.success)
    // An unrelated rejected operation must not erase the same-cache duplicate fence.
    val imageFile = photo()
    f.session.startFirmwareTransfer(imageFile,
        ArkFirmware("test", "frame_film_ark", "ab".repeat(32), "cd".repeat(32), imageFile.length()),
        transportPreference = "ble")
    check(f.latest.phase == "error")
    f.session.retryTransfer()
    f.session.startTransfer(f.file, "same.film", "ble")
    check(f.ble.sent.size == count && f.ble.pending == null)
    val replacement = photo()
    f.session.startTransfer(replacement, "explicit-reimport.film", "ble")
    check(f.ble.pending?.channel == 0x42 && f.latest.attemptId != null)
}

private fun disconnectSuppressesDelayedReconnect() {
    val f = PhotoFixture()
    f.start()
    val count = f.ble.connectCount
    f.session.disconnect()
    check(f.latest.phase == "cancelled")
    check(Handler.runWithDelay(1000))
    check(f.ble.connectCount == count && !f.ble.isReady)
}

private fun formatAndPolicy() {
    val color = photo(false)
    val mono = photo(true)
    check(BlePhotoFormat.isCompatible(color) && BlePhotoFormat.isCompatible(mono))
    fun reject(change: (ByteArray) -> Unit) {
        val invalid = File.createTempFile("ble-invalid-", ".film").apply { deleteOnExit() }
        val bytes = color.readBytes(); change(bytes); invalid.writeBytes(bytes)
        check(!BlePhotoFormat.isCompatible(invalid))
    }
    reject { it[4] = 0xe0.toByte(); it[5] = 1; it[6] = 0xd0.toByte(); it[7] = 2 }
    reject { it[9] = 0x7f }
    reject { it[9] = 2 }
    reject { it[10] = 2 }
    reject { it[8] = 3 }
    check(TransferPolicy.decide(null).transport == "wifi")
    check(TransferPolicy.decide("auto").transport == "wifi")
    check(TransferPolicy.decide("auto", TransferPolicy.WifiSupport.UNKNOWN).transport == "wifi")
    check(TransferPolicy.decide("auto", TransferPolicy.WifiSupport.UNSUPPORTED).transport == "ble")
    check(TransferPolicy.decide("auto", TransferPolicy.WifiSupport.SUPPORTED).transport == "wifi")
    check(TransferPolicy.decide("ble").transport == "ble")
    check(TransferPolicy.decide("wifi", TransferPolicy.WifiSupport.UNSUPPORTED).transport == "wifi")
    val hardware = ArkHardware(1, 9, 0, 4_194_304, 2_097_152, "quad", ArkHardware.N4R2)
    val running = ArkBuildInfo(1_945_600, "ab".repeat(32), "3.2.5", "frame_film_ark")
    val bare = byteArrayOf(1, 0, 0, 0, 0x40)
    val direct = byteArrayOf(1, 0, 0, 0, 0x42)
    check(TransferPolicy.fromRunningFirmware(null, hardware, bare).photo == TransferPolicy.WifiSupport.UNKNOWN)
    check(TransferPolicy.fromRunningFirmware(running, null, bare).photo == TransferPolicy.WifiSupport.UNKNOWN)
    check(TransferPolicy.fromRunningFirmware(running, null, null).photo == TransferPolicy.WifiSupport.UNSUPPORTED)
    check(TransferPolicy.fromRunningFirmware(running.copy(elfSha256 = "0".repeat(64)), hardware, bare).photo == TransferPolicy.WifiSupport.UNKNOWN)
    val outside = TransferPolicy.fromRunningFirmware(running, hardware, bare)
    check(outside.photo == TransferPolicy.WifiSupport.UNSUPPORTED && outside.firmware == TransferPolicy.WifiSupport.UNKNOWN)
    check(TransferPolicy.fromRunningFirmware(running, hardware, direct).photo == TransferPolicy.WifiSupport.SUPPORTED)
    val baseline = running.copy(elfSha256 = "be8728ea6e57521f08f36c19bc967f10b04237ed923827dfb82e88590fcdfd84")
    check(TransferPolicy.fromRunningFirmware(baseline, hardware, bare).photo == TransferPolicy.WifiSupport.SUPPORTED)
    val f = PhotoFixture()
    f.session.startTransfer(f.file, "selected.film", "auto", outside.photo)
    check(f.latest.transport == "ble" && f.ble.channels.firstOrNull() == 0x42)
    f.session.cancelTransfer()
}

private fun otaGateAndManifest() {
    val f = PhotoFixture()
    val image = ArkFirmware("test", "frame_film_ark", "ab".repeat(32), "cd".repeat(32), f.file.length())
    image.requireManifestElf(null)
    image.requireManifestElf("AB".repeat(32))
    check(runCatching { image.requireManifestElf("00".repeat(32)) }.isFailure)
    check(runCatching { image.requireManifestElf("ab") }.isFailure)
    f.session.startFirmwareTransfer(f.file, image, transportPreference = "ble")
    check(!TransferPolicy.supportsVerifiedBleFirmware(byteArrayOf(1, 0, 0, 8, 0)))
    check(f.latest.phase == "error" && f.latest.kind == "firmware" && f.latest.transport == "ble")
    check(!f.latest.success && f.latest.cleanupCompleted && f.ble.channels.isEmpty())
}

private fun verifiedSdUpload(directory: String?, cancel: Boolean = false, lostCommit: Boolean = false) {
    val host = PhotoFixture()
    if (directory == null) host.session.startFileTransfer(host.file, "app/pass/profile.json", "ble")
    else host.session.startTransfer(host.file, "picture.film", "ble", mediaDirectory = directory,
        verifiedBleFile = true, requiresAbc9 = true)
    host.ble.reply(0x59, byteArrayOf(1, 0, 0, 0x20, 0x24))
    val catalog = if (directory == "animation") 2 else 1
    if (directory != null) {
        check(host.ble.requests.last().payload.contentEquals(byteArrayOf(catalog.toByte(), 0, 0, 0, 0)))
        host.ble.reply(0x5B, byteArrayOf(4, 0, 0, 0, 0))
    }
    fun ack(channel: Int, received: Int = 0) =
        host.ble.reply(channel, byteArrayOf(0) + ArkCatalog.be32(received))
    check(host.ble.requests.last().payload.contentEquals(byteArrayOf(1)))
    ack(0x03)
    val path = host.ble.requests.last().payload.dropLast(1).toByteArray().toString(Charsets.UTF_8)
    check(if (directory == null) path == "app/pass/profile.json"
        else path.startsWith("b") && !path.contains('/') && path.endsWith(".film"))
    ack(0x00); ack(0x01)
    if (cancel) host.session.cancelTransfer()
    var received = 0
    while (host.ble.pending?.channel == 0x02) {
        val size = host.ble.requests.last().payload.size
        received += size
        ack(0x02, received)
        if (!cancel) check(Handler.runWithDelay(0))
    }
    if (cancel) {
        check(host.ble.requests.last().payload.contentEquals(byteArrayOf(2)))
        ack(0x03)
        check(host.latest.phase == "cancelled" && host.latest.cleanupCompleted)
        return
    }
    check(received.toLong() == host.file.length() && !host.latest.success)
    check(host.ble.requests.last().payload.contentEquals(byteArrayOf(if (directory == "film") 0 else 1)))
    if (lostCommit) {
        host.ble.fail(0x04)
        check(host.latest.phase == "unconfirmed" && !host.latest.canRetry)
        host.ble.forceReady()
        host.session.confirmFirmwareTransfer()
    } else ack(0x04, received)
    if (directory != null) {
        check(host.ble.requests.last().payload.contentEquals(byteArrayOf(catalog.toByte(), 0, 0, 0, 0)))
        host.ble.reply(0x5B, byteArrayOf(0, 0, 0, 0, 1) + path.substringAfter('/').toByteArray() + byteArrayOf(0))
    }
    check(host.latest.success && host.latest.cleanupCompleted && host.ble.requests.count { it.channel == 0x04 } == 1)
    check(host.latest.kind == if (directory == null) "file" else "film")
}

fun main() {
    formatAndPolicy()
    normalPhoto(mono = true, mtu = 200)
    normalPhoto(mono = false, mtu = 200)
    normalPhoto(mono = true, mtu = 23)
    absenceIsNotSuccess()
    unavailableCatalogIsUnconfirmed()
    oldNameCannotConfirm()
    templateIsBlocked()
    cancelAndLateWriteCannotAdvance()
    stopWriteFailureNeverResends()
    busyPacketStopsStream()
    dismissAndWrongDeviceCannotResend()
    disconnectSuppressesDelayedReconnect()
    otaGateAndManifest()
    verifiedSdUpload("film")
    verifiedSdUpload("animation", lostCommit = true)
    verifiedSdUpload(null)
    verifiedSdUpload(null, cancel = true)
    println("BLE photo/policy/manifest host checks passed")
}
