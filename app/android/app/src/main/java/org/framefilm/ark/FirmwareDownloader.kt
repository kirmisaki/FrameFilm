package org.framefilm.ark

import java.io.File
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean

/** A single bounded download. The caller owns publishing the validated cache file. */
class FirmwareDownloader {
    private val cancelled = AtomicBoolean(false)
    @Volatile private var connection: HttpURLConnection? = null

    fun cancel() {
        cancelled.set(true)
        // Disconnect can synchronize with a blocking read; never block the UI thread on it.
        val active = connection
        if (active != null) Thread({ active.disconnect() }, "firmware-download-cancel").apply {
            isDaemon = true
            start()
        }
    }

    fun checkCancelled() {
        if (cancelled.get() || Thread.currentThread().isInterrupted) throw CancellationException("固件下载已取消")
    }

    fun download(url: String, destination: File, size: Long, sha256: String?, progress: (Long) -> Unit) {
        try {
            var target = URL(url)
            for (redirect in 0..8) {
                checkCancelled()
                require(target.protocol == "https") { "固件下载必须使用 HTTPS" }
                val http = target.openConnection() as HttpURLConnection
                connection = http
                http.connectTimeout = 15_000
                http.readTimeout = 20_000
                http.instanceFollowRedirects = false
                http.setRequestProperty("Accept", "application/octet-stream")
                http.setRequestProperty("User-Agent", "FrameFilm-Ark")
                try {
                    checkCancelled()
                    val status = http.responseCode
                    if (status in listOf(301, 302, 303, 307, 308)) {
                        require(redirect < 8) { "固件下载重定向过多" }
                        target = URL(target, requireNotNull(http.getHeaderField("Location")) { "下载重定向缺少地址" })
                        continue
                    }
                    require(status == 200) { "GitHub 固件下载失败：HTTP $status" }
                    val length = http.getHeaderField("Content-Length")?.toLongOrNull()
                    require(length == null || length == size) { "固件响应长度与 Release 不一致" }
                    http.inputStream.use { copy(it, destination, size, sha256, progress) }
                    checkCancelled()
                    return
                } finally {
                    http.disconnect()
                    connection = null
                }
            }
            error("固件下载重定向过多")
        } catch (error: Exception) {
            destination.delete()
            checkCancelled()
            throw error
        }
    }

    internal fun copy(input: InputStream, destination: File, size: Long, sha256: String?, progress: (Long) -> Unit) {
        require(size in 1..MAX_SIZE) { "固件大小必须在 1 B 至 4 MiB 之间" }
        val digest = MessageDigest.getInstance("SHA-256")
        var received = 0L
        destination.outputStream().use { output ->
            val buffer = ByteArray(8192)
            while (true) {
                checkCancelled()
                val count = input.read(buffer)
                checkCancelled()
                if (count < 0) break
                received += count
                require(received <= size && received <= MAX_SIZE) { "固件下载超过 Release 声明大小" }
                output.write(buffer, 0, count)
                digest.update(buffer, 0, count)
                progress(received)
            }
        }
        require(received > 0 && received == size) { "固件下载不完整，与 Release 大小不一致" }
        val actual = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
        require(sha256 == null || actual.equals(sha256, ignoreCase = true)) { "固件 SHA256 与 Release 不一致" }
    }

    companion object { const val MAX_SIZE = 4L * 1024 * 1024 }
}
