package org.framefilm.framefilm_ark

import org.framefilm.ark.Abc9Preview

/** Wire encodings for app/pass/profile.bin; separate from generic Film capabilities. */
internal enum class PassportRasterFormat(val capability: Long, val label: String) {
    LEGACY_MONO(0, "单色干员档案"),
    LEGACY_SIX_COLOR(1L, "六色干员档案"),
    STUDENT_I006(1L shl 11, "I006 全屏学生证"),
    STUDENT_COLOR_FAST(1L shl 14, "ColorFast 全屏学生证"),
    OPERATOR_I006(1L shl 15, "I006 干员档案"),
    OPERATOR_COLOR_FAST(1L shl 15, "ColorFast 干员档案");

    fun validate(bin: ByteArray, hasAvatar: Boolean) {
        fun u8(offset: Int) = bin[offset].toInt() and 255
        fun le16(offset: Int) = u8(offset) or (u8(offset + 1) shl 8)
        fun le32(offset: Int) = le16(offset) or (le16(offset + 2) shl 16)
        when (this) {
            STUDENT_I006 -> {
                // The existing ABC9 validator checks the header and every code on the staged file.
                require(bin.size == Abc9Preview.FILE_BYTES) { "I006 学生证文件大小无效" }
            }
            STUDENT_COLOR_FAST -> {
                val pixels = 720 * 480
                require(bin.size == 32 + pixels && le32(0) == pixels && le16(4) == 720 && le16(6) == 480 &&
                    u8(8) == 0 && u8(9) == 0x03 && le16(10) in 0..1) { "ColorFast 学生证文件头或尺寸无效" }
                for (i in 32 until bin.size) require(u8(i) <= 63) { "ColorFast 学生证索引超出 0..63" }
            }
            else -> {
                val compact = this == OPERATOR_I006 || this == OPERATOR_COLOR_FAST
                val version = if (compact) 3 else if (this == LEGACY_SIX_COLOR) 2 else 1
                val codec = if (!compact) version else if (this == OPERATOR_I006) 0x7f else 0x03
                val monoBytes = 440 * 608 / 8
                val avatarBytes = if (!compact || !hasAvatar) 0 else 128 * 160 * if (codec == 0x7f) 2 else 1
                val size = 16 + if (this == LEGACY_SIX_COLOR) 440 * 608 / 2 else monoBytes + avatarBytes
                require(bin.size == size && u8(0) == 70 && u8(1) == 70 && u8(2) == 85 && u8(3) == 73 &&
                    u8(4) == version && u8(5) == codec && le16(6) == 440 && le16(8) == 608 &&
                    u8(10) == if (compact && hasAvatar) 1 else 0) { "440×608 干员档案位图与资料不一致" }
                require((11..15).all { u8(it) == 0 }) { "干员档案保留字段无效" }
                if (this == LEGACY_SIX_COLOR) {
                    val valid = setOf(0, 1, 2, 3, 5, 6)
                    for (i in 16 until bin.size) require((u8(i) ushr 4) in valid && (u8(i) and 15) in valid) {
                        "彩色资料包含无效颜色编码"
                    }
                } else if (this == OPERATOR_I006) {
                    for (i in 16 + monoBytes + 1 until bin.size step 2) require(u8(i) <= 1) { "I006 头像码超出 0..511" }
                } else if (this == OPERATOR_COLOR_FAST) {
                    for (i in 16 + monoBytes until bin.size) require(u8(i) <= 63) { "ColorFast 头像索引超出 0..63" }
                }
            }
        }
    }
}
