package org.framefilm.ark

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.max

/** Native 6-color v1 encoder. Header and Ark 180-degree packing match ForFilm. */
object FilmConverter {
    const val MAX_FILM_BYTES = 128 * 1024 * 1024
    const val WIDTH = 720
    const val HEIGHT = 480
    data class Output(val preview: Bitmap, val bytes: ByteArray)
    private val palette = intArrayOf(Color.BLACK, Color.WHITE, Color.YELLOW, Color.RED, Color.BLUE, Color.rgb(41, 204, 20))

    fun convert(source: Bitmap, options: ImageOptions): Output {
        val canvasBitmap = Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(canvasBitmap)
        canvas.drawColor(Color.WHITE)
        val swap = ((options.rotation % 180) + 180) % 180 != 0
        val fit = max(WIDTH.toFloat() / if (swap) source.height else source.width,
            HEIGHT.toFloat() / if (swap) source.width else source.height) * options.scale.coerceIn(.2f, 3f)
        canvas.save()
        canvas.translate(WIDTH / 2f + options.offsetX * WIDTH, HEIGHT / 2f + options.offsetY * HEIGHT)
        canvas.rotate(options.rotation.toFloat())
        canvas.scale(fit, fit)
        canvas.drawBitmap(source, -source.width / 2f, -source.height / 2f, Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG))
        canvas.restore()
        val pixels = IntArray(WIDTH * HEIGHT)
        canvasBitmap.getPixels(pixels, 0, WIDTH, 0, 0, WIDTH, HEIGHT)
        val bytes = ByteArray(32 + WIDTH * HEIGHT / 2)
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(WIDTH * HEIGHT / 2); putShort(WIDTH.toShort()); putShort(HEIGHT.toShort())
            put(if (options.monochrome) 2.toByte() else 6.toByte())
        }
        byteArrayOf(0, 0xff.toByte(), 0xfc.toByte(), 0xe0.toByte(), 3, 0x1c).copyInto(bytes, 16)
        var current = FloatArray((WIDTH + 2) * 3)
        var next = FloatArray((WIDTH + 2) * 3)
        val channels = FloatArray(3)
        for (y in 0 until HEIGHT) {
            if (Thread.currentThread().isInterrupted) throw InterruptedException("转换已取消")
            for (x in 0 until WIDTH) {
                val index = y * WIDTH + x
                val original = pixels[index]
                val luminance = .2126f * Color.red(original) + .7152f * Color.green(original) + .0722f * Color.blue(original)
                val errorOffset = (x + 1) * 3
                for (c in 0..2) {
                    val value = when (c) { 0 -> Color.red(original); 1 -> Color.green(original); else -> Color.blue(original) }
                    val saturated = luminance + (value - luminance) * options.saturation
                    channels[c] = ((saturated - 128) * options.contrast + 128 + options.brightness * 255 + current[errorOffset + c]).coerceIn(0f, 255f)
                }
                var nearest = 0
                var distance = Float.MAX_VALUE
                for (p in 0 until if (options.monochrome) 2 else palette.size) {
                    val color = palette[p]
                    val dr = channels[0] - Color.red(color)
                    val dg = channels[1] - Color.green(color)
                    val db = channels[2] - Color.blue(color)
                    val candidate = dr * dr + dg * dg + db * db
                    if (candidate < distance) { distance = candidate; nearest = p }
                }
                val color = palette[nearest]
                pixels[index] = color
                // utils.js getPixelIndex: rotated-180 for Ark's 720x480 panel.
                val packedIndex = WIDTH * HEIGHT - 1 - index
                val byteIndex = 32 + packedIndex / 2
                val old = bytes[byteIndex].toInt() and 0xff
                bytes[byteIndex] = if (packedIndex % 2 == 0) ((old and 15) or (nearest shl 4)).toByte()
                    else ((old and 240) or nearest).toByte()
                if (options.dither) for (c in 0..2) {
                    val selected = when (c) { 0 -> Color.red(color); 1 -> Color.green(color); else -> Color.blue(color) }
                    val residual = channels[c] - selected
                    current[errorOffset + 3 + c] += residual * 7f / 16
                    next[errorOffset - 3 + c] += residual * 3f / 16
                    next[errorOffset + c] += residual * 5f / 16
                    next[errorOffset + 3 + c] += residual / 16
                }
            }
            val old = current; current = next; next = old; next.fill(0f)
        }
        canvasBitmap.setPixels(pixels, 0, WIDTH, 0, 0, WIDTH, HEIGHT)
        return Output(canvasBitmap, bytes)
    }

    fun validate(file: File): String {
        require(file.length() in 32..MAX_FILM_BYTES.toLong()) { "film 文件大小无效" }
        val header = ByteArray(32)
        file.inputStream().use { require(it.read(header) == 32) { "film 文件头不完整" } }
        val buffer = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
        val body = buffer.int.toLong() and 0xffffffffL
        val width = buffer.short.toInt() and 0xffff
        val height = buffer.short.toInt() and 0xffff
        val colors = buffer.get().toInt() and 0xff
        val format = buffer.get().toInt() and 0xff
        val frames = max(1, buffer.short.toInt() and 0xffff)
        require(width == WIDTH && height == HEIGHT) { "请选择 Ark 720×480 的 film 文件" }
        val frameSize = when (format) {
            0 -> { require(colors in 2..6) { "无效的六色文件" }; WIDTH * HEIGHT / 2 }
            1 -> WIDTH * HEIGHT / 8
            2, 3 -> WIDTH * HEIGHT
            else -> error("不支持的 film 编码：$format")
        }
        require(body == frameSize.toLong() * frames && file.length() == body + 32) { "film 长度与文件头不一致" }
        return "720×480 · $frames 帧 · ${file.length()} 字节"
    }
}
