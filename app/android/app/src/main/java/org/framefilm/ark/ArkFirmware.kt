package org.framefilm.ark

import java.io.File
import java.io.RandomAccessFile
import java.security.MessageDigest

data class ArkFirmware(val version: String, val project: String, val elfSha256: String,
                       val fileSha256: String, val size: Long, val hardware: ArkHardware? = null) {
    fun requireTarget(expectedTarget: String) {
        require(ArkHardware.knownTarget(expectedTarget)) { "缺少明确硬件目标，不能猜测型号" }
        require(hardware?.targetId == expectedTarget) {
            "下载镜像的实际硬件目标 ${hardware?.targetId ?: "未描述"} 与所选 $expectedTarget 不匹配"
        }
    }
    companion object {
        fun inspect(file: File): ArkFirmware {
            require(file.length() in 288..FirmwareDownloader.MAX_SIZE) { "固件大小无效" }
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
            val hardware = if (le32(28) >= 320 && file.length() >= 352) {
                val descriptor = ByteArray(64)
                RandomAccessFile(file, "r").use { it.seek(288); it.readFully(descriptor) }
                ArkHardware.fromDescriptor(descriptor)
            } else null
            return ArkFirmware(version, project, hex(elf), hex(digest.digest()), file.length(), hardware)
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

/** Physical identity is queried from the connected board, never inferred from its name/version. */
data class ArkHardware(val schemaVersion: Int, val chipId: Int, val chipRevision: Int,
                       val flashBytes: Long, val psramBytes: Long, val psramBus: String,
                       val targetId: String) {
    fun snapshot(): Map<String, Any> = mapOf("schemaVersion" to schemaVersion,
        "chipId" to chipId, "chipRevision" to chipRevision, "flashBytes" to flashBytes,
        "psramBytes" to psramBytes, "psramBus" to psramBus, "targetId" to targetId)

    fun requireMatches(image: ArkFirmware) {
        val target = requireNotNull(image.hardware) {
            "固件缺少硬件目标描述，无法确认兼容性；请选择带 FFARKHW1 描述的对应型号镜像"
        }
        require(knownTarget(targetId) && knownTarget(target.targetId)) { "硬件型号尚未识别，不能自动升级；请先完成一次型号识别升级" }
        require(chipId == target.chipId && flashBytes == target.flashBytes &&
            psramBytes == target.psramBytes && psramBus == target.psramBus && targetId == target.targetId) {
            "固件硬件目标不匹配：设备 $targetId（$flashBytes/$psramBytes/$psramBus），镜像 ${target.targetId}（${target.flashBytes}/${target.psramBytes}/${target.psramBus}）"
        }
    }

    companion object {
        const val N4R2 = "ark-esp32s3-n4r2"
        const val N16R8 = "ark-esp32s3-n16r8"
        fun knownTarget(id: String) = id == N4R2 || id == N16R8
        private fun bus(code: Int) = when(code) { 1 -> "quad"; 2 -> "octal"; else -> "unknown" }
        private fun ascii(data: ByteArray, start: Int, end: Int): String {
            val nul = (start until end).firstOrNull { data[it] == 0.toByte() }
                ?: error("硬件 targetId 缺少终止符")
            require((start until nul).all { data[it].toInt() in 0x21..0x7e } && nul > start &&
                (nul until end).all { data[it] == 0.toByte() }) { "硬件 targetId 格式无效" }
            return data.copyOfRange(start, nul).toString(Charsets.US_ASCII)
        }
        private fun validateTarget(hardware: ArkHardware) {
            val memory = when(hardware.targetId) {
                N4R2 -> Triple(4L*1024*1024, 2L*1024*1024, "quad")
                N16R8 -> Triple(16L*1024*1024, 8L*1024*1024, "octal")
                else -> error("未知固件硬件目标：${hardware.targetId}")
            }
            require(hardware.chipId == 9 && hardware.flashBytes == memory.first &&
                hardware.psramBytes == memory.second && hardware.psramBus == memory.third) {
                "硬件 targetId 与 Flash / PSRAM / 总线描述不一致"
            }
        }
        fun parse(data: ByteArray): ArkHardware {
            require(data.size in 16..46 && data[0] == 1.toByte()) { "设备缺少硬件身份；请先完成一次对应型号的识别升级，图片功能仍可使用" }
            fun u8(i:Int)=data[i].toInt() and 255
            fun be16(i:Int)=(u8(i) shl 8) or u8(i+1)
            fun be32(i:Int)=(0..3).fold(0L){v,n->(v shl 8) or u8(i+n).toLong()}
            return ArkHardware(1,be16(1),be16(3),be32(5),be32(9),bus(u8(13)),ascii(data,14,data.size)).also {
                if(knownTarget(it.targetId)) validateTarget(it)
            }
        }
        fun fromDescriptor(data: ByteArray): ArkHardware? {
            require(data.size == 64)
            if(!data.copyOfRange(0,8).contentEquals("FFARKHW1".toByteArray(Charsets.US_ASCII))) return null
            fun u8(i:Int)=data[i].toInt() and 255
            fun le16(i:Int)=u8(i) or (u8(i+1) shl 8)
            fun le32(i:Int)=(0..3).fold(0L){v,n->v or (u8(i+n).toLong() shl (8*n))}
            require(u8(8)==1 && u8(9)==64 && (21..23).all{u8(it)==0} && (56..63).all{u8(it)==0}) {
                "固件硬件 descriptor 版本、长度或保留字段无效"
            }
            return ArkHardware(1,le16(10),0,le32(12),le32(16),bus(u8(20)),ascii(data,24,56)).also(::validateTarget)
        }
    }
}
