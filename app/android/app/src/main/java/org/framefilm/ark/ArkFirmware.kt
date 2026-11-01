package org.framefilm.ark

import java.io.File
import java.io.RandomAccessFile
import java.security.MessageDigest

data class ArkFirmware(val version: String, val project: String, val elfSha256: String,
                       val fileSha256: String, val size: Long) {
    companion object {
        fun inspect(file: File): ArkFirmware {
            require(file.length() in 288..(4L * 1024 * 1024)) { "固件大小无效" }
            val header = ByteArray(288)
            RandomAccessFile(file, "r").use { it.readFully(header) }
            fun u8(at: Int) = header[at].toInt() and 255
            fun le32(at: Int) = (0..3).fold(0L) { value, n -> value or (u8(at + n).toLong() shl (8 * n)) }
            require(u8(0) == 0xe9 && u8(1) in 1..16 && u8(12) == 9 && u8(13) == 0) {
                "请选择 ESP32-S3 原始 app.bin，不支持 merged 镜像"
            }
            require(le32(28) >= 256 && le32(32) == 0xabcd5432L) { "缺少 ESP app descriptor" }
            fun text(at: Int): String {
                val end = (at until at + 32).firstOrNull { header[it] == 0.toByte() }
                    ?: throw IllegalArgumentException("固件 descriptor 字符串无效")
                return header.copyOfRange(at, end).toString(Charsets.UTF_8)
            }
            val version = text(48)
            val project = text(80)
            require(project == "frame_film_ark") { "固件项目不匹配：$project" }
            val elf = header.copyOfRange(176, 208)
            require(elf.any { it != 0.toByte() }) { "固件 ELF SHA256 无效" }
            RandomAccessFile(file, "r").use { input ->
                var offset = 24L
                var checksum = 0xef
                val buffer = ByteArray(16384)
                repeat(u8(1)) {
                    require(offset + 8 <= file.length()) { "固件 segment header 已截断" }
                    input.seek(offset + 4)
                    var length = 0L
                    repeat(4) { n -> length = length or (input.readUnsignedByte().toLong() shl (8 * n)) }
                    offset += 8
                    require(length <= file.length() - offset) { "固件 segment 数据已截断" }
                    input.seek(offset)
                    var remaining = length
                    while (remaining > 0) {
                        if (Thread.currentThread().isInterrupted) error("导入已取消")
                        val count = minOf(buffer.size.toLong(), remaining).toInt()
                        input.readFully(buffer, 0, count)
                        for (n in 0 until count) checksum = checksum xor (buffer[n].toInt() and 255)
                        remaining -= count
                    }
                    offset += length
                }
                val checksumAt = offset or 15L
                val imageEnd = checksumAt + 1
                val hashAppended = u8(23)
                require(hashAppended in 0..1 && file.length() == imageEnd + (if (hashAppended == 1) 32 else 0)) {
                    "请选择完整原始 app.bin；镜像长度不符或含 merged/签名附加数据"
                }
                input.seek(checksumAt)
                require(input.readUnsignedByte() == checksum) { "固件 segment checksum 错误" }
                if (hashAppended == 1) {
                    val imageDigest = MessageDigest.getInstance("SHA-256")
                    input.seek(0)
                    var remaining = imageEnd
                    while (remaining > 0) {
                        val count = minOf(buffer.size.toLong(), remaining).toInt()
                        input.readFully(buffer, 0, count)
                        imageDigest.update(buffer, 0, count)
                        remaining -= count
                    }
                    val appended = ByteArray(32)
                    input.readFully(appended)
                    require(imageDigest.digest().contentEquals(appended)) { "固件附加 SHA256 校验失败" }
                }
            }
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().buffered().use { input ->
                val buffer = ByteArray(16384)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (Thread.currentThread().isInterrupted) error("导入已取消")
                    digest.update(buffer, 0, count)
                }
            }
            return ArkFirmware(version, project, hex(elf), hex(digest.digest()), file.length())
        }
        fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it.toInt() and 255) }
        fun bytes(hex: String): ByteArray = hex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }
}

data class ArkBuildInfo(val otaMax: Long, val elfSha256: String, val version: String, val project: String) {
    companion object {
        fun parse(data: ByteArray): ArkBuildInfo {
            require(data.size >= 39 && data[0] == 1.toByte()) { "设备不支持当前 Wi-Fi OTA 能力" }
            val max = (1..4).fold(0L) { value, n -> (value shl 8) or (data[n].toLong() and 255) }
            val versionEnd = (37 until data.size).firstOrNull { data[it] == 0.toByte() }
                ?: error("BUILD_INFO 版本缺少终止符")
            val projectEnd = (versionEnd + 1 until data.size).firstOrNull { data[it] == 0.toByte() }
                ?: error("BUILD_INFO 项目缺少终止符")
            require(projectEnd == data.lastIndex && max > 0) { "BUILD_INFO 格式无效" }
            return ArkBuildInfo(max, ArkFirmware.hex(data.copyOfRange(5, 37)),
                data.copyOfRange(37, versionEnd).toString(Charsets.UTF_8),
                data.copyOfRange(versionEnd + 1, projectEnd).toString(Charsets.UTF_8))
        }
    }
}
