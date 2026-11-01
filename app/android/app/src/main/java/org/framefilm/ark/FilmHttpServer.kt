package org.framefilm.ark

import java.io.Closeable
import java.io.File
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.util.concurrent.Executors

/** The firmware appends ".ffupload.part" while saving, within its 64-character FAT limit. */
internal object FilmTransferName {
    private val validName = Regex("[A-Za-z0-9_.-]{1,45}\\.film")

    fun forFile(displayName: String, file: File): String {
        if (validName.matches(displayName)) return displayName
        // This import's temporary ID stays fixed across retries.
        val id = file.name.removePrefix("import-").removeSuffix(".film")
            .filter { it in 'A'..'Z' || it in 'a'..'z' || it in '0'..'9' }
            .takeLast(40)
        return "ark_${id.ifEmpty { "film" }}.film"
    }
}
/** Serves one local file through the phone's Wi-Fi Direct GO address. */
class FilmHttpServer(file: File, fileName: String, address: Inet4Address,
                     firmware: Boolean = false, generic: Boolean = false) : Closeable {
    private val source = file.also {
        require(it.isFile && it.length() in 1..0xffffffffL) { "文件不存在或大小无效" }
    }
    private val length = source.length()
    // The requested destination is carried separately in the BLE command. Keep the URL basename fixed.
    private val filename: String = when {
        firmware -> "firmware.bin"
        generic -> "upload.bin"
        else -> FilmTransferName.forFile(fileName, source)
    }
    private val server = ServerSocket().apply {
        reuseAddress = true
        bind(InetSocketAddress(address, 0))
        soTimeout = 1000
    }
    private val worker = Executors.newSingleThreadExecutor()
    @Volatile private var closed = false
    @Volatile private var client: Socket? = null

    val size: Long get() = length
    val url: String = "http://${address.hostAddress}:${server.localPort}/$filename"

    fun start() {
        worker.execute {
            while (!closed) {
                try {
                    server.accept().also {
                        client = it
                        if (closed) { it.close(); throw SocketException("server closed") }
                    }.use { socket ->
                        socket.soTimeout = 5_000
                        val input = socket.getInputStream().bufferedReader(Charsets.US_ASCII)
                        val first = input.readLine() ?: return@use
                        var line: String?
                        do { line = input.readLine() } while (line != null && line.isNotEmpty())
                        val output = socket.getOutputStream()
                        if (first == "GET /$filename HTTP/1.1" || first == "GET /$filename HTTP/1.0") {
                            val header = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: $length\r\nConnection: close\r\n\r\n"
                            output.write(header.toByteArray(Charsets.US_ASCII))
                            source.inputStream().buffered().use { film ->
                                val buffer = ByteArray(16 * 1024)
                                var remaining = length
                                while (remaining > 0 && !closed) {
                                    val count = film.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                                    if (count < 0) break
                                    output.write(buffer, 0, count)
                                    remaining -= count
                                }
                            }
                        } else output.write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".toByteArray(Charsets.US_ASCII))
                        output.flush()
                    }
                } catch (_: java.net.SocketTimeoutException) { }
                catch (_: SocketException) { if (!closed) break }
                catch (_: Exception) { if (!closed) break }
                finally { client = null }
            }
        }
    }

    override fun close() {
        closed = true
        server.close()
        try { client?.close() } catch (_: Exception) { }
        worker.shutdownNow()
    }
}
