package org.framefilm.ark

import java.io.DataInputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Existing experimental ABC9 container; independent of firmware display versions. */
object Abc9Preview {
    const val FORMAT = 0x7F
    const val BODY_BYTES = 720 * 480 * 2
    const val FILE_BYTES = 32 + BODY_BYTES

    fun supported(capabilities: ByteArray?): Boolean = capabilities?.size == 5 &&
        capabilities[0] == 1.toByte() && capabilities[4].toInt() and 0x20 != 0

    fun isFilm(file: File): Boolean = file.inputStream().use { input ->
        input.skip(9) == 9L && input.read() == FORMAT
    }

    fun validate(file: File) {
        require(file.length() == FILE_BYTES.toLong()) { "ABC9 实验 film 必须为 $FILE_BYTES 字节" }
        DataInputStream(file.inputStream().buffered()).use { input ->
            val header = ByteArray(32)
            input.readFully(header)
            val buffer = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
            require(buffer.int == BODY_BYTES && buffer.short.toInt() == 720 && buffer.short.toInt() == 480) {
                "ABC9 实验 film 必须为 720×480，主体 $BODY_BYTES 字节"
            }
            require(header[9].toInt() and 255 == FORMAT && buffer.getShort(10).toInt() == 1 &&
                header.copyOfRange(12, 16).contentEquals(byteArrayOf(65, 66, 67, 57))) {
                "ABC9 实验 film 标记或单帧文件头无效"
            }
            val chunk = ByteArray(8192)
            var remaining = BODY_BYTES
            while (remaining > 0) {
                if (Thread.currentThread().isInterrupted) throw InterruptedException("导入已取消")
                val count = minOf(chunk.size, remaining)
                input.readFully(chunk, 0, count)
                for (i in 1 until count step 2) require(chunk[i].toInt() and 0xFE == 0) {
                    "ABC9 实验 film 像素必须为 0..511，高位必须为零"
                }
                remaining -= count
            }
        }
    }
}
