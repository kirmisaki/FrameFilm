package org.framefilm.framefilm_ark

import java.nio.ByteBuffer
import java.nio.ByteOrder

fun main() {
    fun rejects(format: PassportRasterFormat, bin: ByteArray, hasAvatar: Boolean) {
        check(runCatching { format.validate(bin, hasAvatar) }.exceptionOrNull() is IllegalArgumentException)
    }
    val full = ByteArray(32 + 720 * 480).apply {
        ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(720 * 480).putShort(720).putShort(480).put(0).put(3).putShort(1)
        this[lastIndex] = 63
    }
    val student = PassportRasterFormat.STUDENT_COLOR_FAST
    student.validate(full, true)
    student.validate(full.copyOf().apply { this[10] = 0 }, false)
    rejects(student, full.copyOf().apply { this[10] = 2 }, true)
    rejects(student, full.copyOf().apply { this[lastIndex] = 64 }, true)
    rejects(student, full.copyOf().apply { this[9] = 2 }, true)
    rejects(student, full.copyOf(full.size + 1), true)
    val plain = 16 + 440 * 608 / 8
    for ((format, codec, stride) in listOf(
        Triple(PassportRasterFormat.OPERATOR_COLOR_FAST, 3, 1),
        Triple(PassportRasterFormat.OPERATOR_I006, 0x7f, 2),
    )) {
        val bin = ByteArray(plain + 128 * 160 * stride).apply {
            "FFUI".toByteArray().copyInto(this)
            this[4] = 3
            this[5] = codec.toByte()
            ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putShort(6, 440).putShort(8, 608)
            this[10] = 1
            this[lastIndex] = if (stride == 1) 63 else 1
        }
        format.validate(bin, true)
        format.validate(bin.copyOf(plain).apply { this[10] = 0 }, false)
        rejects(format, bin, false)
        rejects(format, bin.copyOf(plain), true)
        rejects(format, bin.copyOf().apply { this[11] = 1 }, true)
        rejects(format, bin.copyOf().apply { this[lastIndex] = if (stride == 1) 64 else 2 }, true)
    }
    println("Passport raster header, length, codec, and range host checks passed")
}
