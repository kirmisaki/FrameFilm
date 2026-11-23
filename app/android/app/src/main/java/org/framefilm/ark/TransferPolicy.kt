package org.framefilm.ark

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Only running-device evidence may control automatic routing. */
object TransferPolicy {
    enum class WifiSupport { SUPPORTED, UNSUPPORTED, UNKNOWN }
    data class WifiEvidence(val photo: WifiSupport = WifiSupport.UNKNOWN,
                            val firmware: WifiSupport = WifiSupport.UNKNOWN)
    data class Decision(val transport: String, val reason: String)

    // Exact running ELF -> operation actually verified. docs/wifi/wifi_doc.md §2026-10-27
    // records the shared film HTTP receiver completing after this build booted.
    // An unlisted build selects BLE photos only when its live Ark identity is verified.
    // Firmware OTA routing stays independent until BLE OTA is actually eligible.
    private val verifiedBuilds = mapOf(
        "be8728ea6e57521f08f36c19bc967f10b04237ed923827dfb82e88590fcdfd84" to
            WifiEvidence(photo = WifiSupport.SUPPORTED))

    fun fromRunningFirmware(info: ArkBuildInfo?, hardware: ArkHardware?, capabilities: ByteArray?,
                            table: Map<String, WifiEvidence> = verifiedBuilds): WifiEvidence {
        if (info != null && (info.project != "frame_film_ark" ||
            !info.elfSha256.matches(Regex("[0-9a-f]{64}")) || info.elfSha256.all { it == '0' }))
            return WifiEvidence()
        val flags = capabilities?.takeIf { it.size == 5 && it[0] == 1.toByte() }?.get(4)?.toInt() ?: 0
        // An older Ark may have BUILD_INFO but no supported 5D. A claimed but
        // unreadable hardware identity is contradictory, not a known legacy build.
        if ((flags and 0x40 != 0 && hardware == null) ||
            (hardware != null && !ArkHardware.knownTarget(hardware.targetId))) return WifiEvidence()
        val entry = info?.let { table[it.elfSha256] }
        val knownPhoto = entry?.photo ?: if (info != null) WifiSupport.UNSUPPORTED else WifiSupport.UNKNOWN
        // Ark's 0x5A and 0x50 share the direct file receiver. A positive capability
        // is evidence; a missing bit or malformed/no reply isn't an unsupported reply.
        val directFile = flags and 2 != 0
        val photo = when {
            directFile && entry?.photo == WifiSupport.UNSUPPORTED -> WifiSupport.UNKNOWN
            directFile -> WifiSupport.SUPPORTED
            else -> knownPhoto
        }
        return WifiEvidence(photo, entry?.firmware ?: WifiSupport.UNKNOWN)
    }

    fun decide(preference: String?, support: WifiSupport = WifiSupport.UNKNOWN): Decision {
        require(preference == null || preference in setOf("auto", "ble", "wifi")) { "无效传输模式" }
        return when {
            preference == "ble" -> Decision("ble", "已手动选择蓝牙兼容模式")
            preference == "wifi" -> Decision("wifi", "已手动选择 Wi-Fi")
            preference == "auto" && support == WifiSupport.UNSUPPORTED ->
                Decision("ble", "当前运行构建已确认不支持此操作的 Wi-Fi 传输")
            else -> Decision("wifi", if (support == WifiSupport.SUPPORTED) "当前运行构建已验证 Wi-Fi 传输" else
                "当前运行构建未纳入已验证支持表，保留 Wi-Fi 优先；可手动选择蓝牙兼容")
        }
    }

    const val BLE_FIRMWARE_MIN_MTU = 43
    const val BLE_FIRMWARE_REASON = "当前 Ark 连接缺少可验证 BLE OTA 能力、硬件/运行身份或所需 MTU"

    fun supportsVerifiedBleFirmware(capabilities: ByteArray?): Boolean =
        capabilities != null && capabilities.size == 5 && capabilities[0] == 1.toByte() &&
            (capabilities[3].toInt() and 0x10) != 0

    const val BLE_FILE_MIN_MTU = 29
    fun supportsVerifiedBleFile(capabilities: ByteArray?): Boolean =
        capabilities != null && capabilities.size == 5 && capabilities[0] == 1.toByte() &&
            (capabilities[3].toInt() and 0x20) != 0
}

/** The exact subset accepted by the ordinary-photo BLE path, not the broader Film importer. */
object BlePhotoFormat {
    fun requireCompatible(file: File) {
        val header = ByteArray(32)
        file.inputStream().use { require(it.read(header) == 32) { "film 文件头不完整" } }
        val buffer = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
        val body = buffer.int.toLong() and 0xffffffffL
        val width = buffer.short.toInt() and 0xffff
        val height = buffer.short.toInt() and 0xffff
        val colors = buffer.get().toInt() and 255
        val format = buffer.get().toInt() and 255
        val frames = buffer.short.toInt() and 0xffff
        val frameBytes = when {
            format == 0 && colors in setOf(2, 6) -> 720L * 480 / 2
            format == 1 && colors == 2 -> 720L * 480 / 8
            else -> -1L
        }
        require(width == 720 && height == 480 && frames in 0..1 && frameBytes > 0 &&
            body == frameBytes && file.length() == body + 32) {
            "当前连接只支持普通单帧二值或六色 film 的旧蓝牙上传"
        }
    }
    fun isCompatible(file: File): Boolean = runCatching { requireCompatible(file) }.isSuccess
}

/** Shared by read-only browsing and the occupied transfer-side verifier. */
object ArkCatalog {
    data class Page(val count: Long, val name: String?)
    fun be32(value: Int) = byteArrayOf((value ushr 24).toByte(), (value ushr 16).toByte(),
        (value ushr 8).toByte(), value.toByte())
    fun parse(data: ByteArray, index: Int, expectedCount: Long?): Page {
        require(data.size >= 5) { "目录响应无效" }
        val count = (1..4).fold(0L) { value, n -> (value shl 8) or (data[n].toLong() and 255) }
        require(expectedCount == null || count == expectedCount) { "读取期间目录已变化，请刷新重试" }
        return when (data[0].toInt() and 255) {
            4 -> { require(index.toLong() == count && data.size == 5) { "目录结束位置无效" }; Page(count, null) }
            0 -> {
                require(data.size > 6 && data.last() == 0.toByte() && index.toLong() < count) { "目录文件名无效" }
                Page(count, data.copyOfRange(5, data.lastIndex).toString(Charsets.UTF_8))
            }
            else -> error("目录不可读取（状态 ${data[0].toInt() and 255}），不能确认 SD 保存")
        }
    }
}
