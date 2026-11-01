package org.framefilm.ark

import java.io.File
import java.security.MessageDigest

/** Dependency-free focused probe, runnable with kotlinc + java, no Android runtime. */
object ArkFirmwareTest {
    @JvmStatic fun main(args: Array<String>) {
        val file = File.createTempFile("ark-firmware-test", ".bin")
        try {
            val raw = ByteArray(304)
            raw[0] = 0xe9.toByte(); raw[1] = 1; raw[12] = 9; raw[23] = 1
            raw[29] = 1 // first segment length = 256 LE
            byteArrayOf(0x32, 0x54, 0xcd.toByte(), 0xab.toByte()).copyInto(raw, 32)
            "3.2.5".toByteArray().copyInto(raw, 48)
            "frame_film_ark".toByteArray().copyInto(raw, 80)
            ByteArray(32) { it.toByte() }.copyInto(raw, 176)
            raw[303] = (32 until 288).fold(0xef) { sum, at -> sum xor (raw[at].toInt() and 255) }.toByte()
            fun image() = raw + MessageDigest.getInstance("SHA-256").digest(raw)
            val valid = image()
            if (args.contentEquals(arrayOf("--new-cases"))) {
                fun sealed(bytes: ByteArray): ByteArray {
                    bytes[303] = (32 until 288).fold(0xef) { sum, at -> sum xor (bytes[at].toInt() and 255) }.toByte()
                    return bytes + MessageDigest.getInstance("SHA-256").digest(bytes)
                }
                val emptyVersion = raw.clone().apply { fill(0, 48, 80) }
                file.writeBytes(sealed(emptyVersion))
                check(ArkFirmware.inspect(file).version.isEmpty())

                file.writeBytes(valid)
                val first = ArkFirmware.inspect(file)
                file.writeBytes(sealed(raw.clone().apply { this[176] = (this[176].toInt() xor 0x80).toByte() }))
                val second = ArkFirmware.inspect(file)
                check(first.version == second.version && first.elfSha256 != second.elfSha256)
                println("ArkFirmwareTest PASS: empty version and same-version distinct ELF identities")
                return
            }
            file.writeBytes(valid)
            val info = ArkFirmware.inspect(file)
            check(info.project == "frame_film_ark" && info.version == "3.2.5")
            check(info.elfSha256 == ArkFirmware.hex(ByteArray(32) { it.toByte() }))
            check(info.fileSha256 == ArkFirmware.hex(MessageDigest.getInstance("SHA-256").digest(valid)))
            fun rejects(bytes: ByteArray) {
                file.writeBytes(bytes)
                check(runCatching { ArkFirmware.inspect(file) }.isFailure)
            }
            rejects(valid.copyOf(288))
            rejects(valid + byteArrayOf(0)) // merged/padded tail
            rejects(valid.clone().apply { this[12] = 0 }) // wrong chip
            rejects(valid.clone().apply { this[80] = 'x'.code.toByte() }) // wrong project
            rejects(valid.clone().apply { this[220] = 1 }) // bad segment checksum/hash
            rejects(valid.clone().apply { this[lastIndex] = (this[lastIndex].toInt() xor 1).toByte() })
            val build = byteArrayOf(1, 0, 0x20, 0, 0) + ByteArray(32) { it.toByte() } +
                "3.2.5\u0000frame_film_ark\u0000".toByteArray()
            check(ArkBuildInfo.parse(build).otaMax == 0x200000L)
            check(ArkBuildInfo.parse(build).elfSha256 == info.elfSha256)
            check(runCatching { ArkBuildInfo.parse(build.dropLast(1).toByteArray()) }.isFailure)
            println("ArkFirmwareTest PASS: raw app, descriptor, hashes, target and truncation")
        } finally { file.delete() }
    }
}
