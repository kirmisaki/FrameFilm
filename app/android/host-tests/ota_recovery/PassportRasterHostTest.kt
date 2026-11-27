package org.framefilm.framefilm_ark

import java.nio.ByteBuffer
import java.nio.ByteOrder

private fun capabilitiesAreValidAndLinkScoped() {
    val cache = ArkCapabilityCache()
    val i006 = PassportRasterFormat.STUDENT_I006.capability
    val colorFast = PassportRasterFormat.STUDENT_COLOR_FAST.capability
    val invalid = listOf(byteArrayOf(), byteArrayOf(1, 0, 0, 0x48),
        byteArrayOf(1, 0, 0, 0x48, 0, 0), byteArrayOf(2, 0, 0, 0x48, 0))
    check(cache[1] == null && !ArkCapabilityCache.hasBit(cache[1], i006))
    for (reply in invalid) {
        cache.update(1, reply)
        check(cache[1] == null && !ArkCapabilityCache.hasBit(reply, colorFast))
    }
    // A later read of the same channel recovers the missing evidence, even with no transfer bits.
    cache.update(1, byteArrayOf(1, 0, 0, 0x08, 0))
    check(cache[1] != null && ArkCapabilityCache.hasBit(cache[1], i006))
    check(!ArkCapabilityCache.hasBit(cache[1], colorFast))
    for (reply in invalid) {
        cache.update(1, reply)
        check(ArkCapabilityCache.hasBit(cache[1], i006) && !ArkCapabilityCache.hasBit(cache[1], colorFast))
    }
    check(cache[null] == null && cache[2] == null)
    cache.update(2, invalid.last())
    check(cache[2] == null)
    cache.update(2, byteArrayOf(1, 0, 0, 0x48, 0))
    check(ArkCapabilityCache.hasBit(cache[2], i006) && ArkCapabilityCache.hasBit(cache[2], colorFast))
    cache.update(2, byteArrayOf(1, 0, 0, 0, 0))
    check(cache[2] != null && !ArkCapabilityCache.hasBit(cache[2], i006) && !ArkCapabilityCache.hasBit(cache[2], colorFast))
    cache.clear()
    check(cache[2] == null)
}

fun main() {
    capabilitiesAreValidAndLinkScoped()
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
