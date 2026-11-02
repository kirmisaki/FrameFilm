package org.framefilm.ark

import java.io.Closeable
import java.io.File
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.util.concurrent.Executors
import java.util.logging.Level
import java.util.logging.Logger

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
/** Serves one local file through the phone's selected Wi-Fi interface address. */
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
    private val logger = Logger.getLogger("FilmHttpServer")
    private class RequestTrace {
        val startedNanos = System.nanoTime()
        @Volatile var written = 0L
        @Volatile var phase = "request"
    }
    @Volatile private var closed = false
    @Volatile private var client: Socket? = null
    @Volatile private var activeRequest: RequestTrace? = null
    @Volatile var writtenBytes: Long = 0
        private set
    @Volatile var transferFinished: Boolean = false
        private set

    val size: Long get() = length
    val url: String = "http://${address.hostAddress}:${server.localPort}/$filename"

    fun start() {
        worker.execute {
            while (!closed) {
                var trace: RequestTrace? = null
                try {
                    server.accept().also {
                        client = it
                        trace = RequestTrace().also { request -> activeRequest = request }
                        log("request-start", trace)
                        if (closed) { it.close(); throw SocketException("server closed") }
                    }.use { socket ->
                        val request = checkNotNull(trace)
                        socket.soTimeout = 5_000
                        val input = socket.getInputStream().bufferedReader(Charsets.US_ASCII)
                        val first = input.readLine() ?: return@use
                        var line: String?
                        do { line = input.readLine() } while (line != null && line.isNotEmpty())
                        val output = socket.getOutputStream()
                        if (first == "GET /$filename HTTP/1.1" || first == "GET /$filename HTTP/1.0") {
                            writtenBytes = 0
                            transferFinished = false
                            request.phase = "header"
                            val header = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: $length\r\nConnection: close\r\n\r\n"
                            output.write(header.toByteArray(Charsets.US_ASCII))
                            request.phase = "body"
                            source.inputStream().buffered().use { film ->
                                val buffer = ByteArray(16 * 1024)
                                var remaining = length
                                while (remaining > 0 && !closed) {
                                    val count = film.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                                    if (count < 0) {
                                        log("source-eof", trace, level = Level.WARNING)
                                        break
                                    }
                                    output.write(buffer, 0, count)
                                    // A completed write only confirms socket acceptance, not peer receipt.
                                    request.written += count
                                    writtenBytes = request.written
                                    remaining -= count
                                }
                            }
                            request.phase = if (request.written == length) "body-written" else "body-incomplete"
                        } else {
                            request.phase = "not-found"
                            output.write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".toByteArray(Charsets.US_ASCII))
                        }
                        output.flush()
                    }
                } catch (error: java.net.SocketTimeoutException) {
                    // An idle accept timeout is expected; a request timeout needs diagnostics.
                    if (trace != null) log("request-exception", trace, error, Level.WARNING)
                }
                catch (error: SocketException) {
                    log("socket-exception", trace, error, if (closed) Level.INFO else Level.WARNING)
                    if (!closed) break
                }
                catch (error: Exception) {
                    log("request-exception", trace, error, if (closed) Level.INFO else Level.WARNING)
                    if (!closed) break
                }
                finally {
                    if (trace != null) log("request-end", trace)
                    if (trace?.phase?.startsWith("body") == true || trace?.phase == "header") transferFinished = true
                    client = null
                    activeRequest = null
                }
            }
        }
    }

    override fun close() {
        closed = true
        log("close-requested", activeRequest)
        try { server.close() } catch (error: Exception) {
            log("close-exception", activeRequest, error, Level.WARNING)
            throw error
        }
        try { client?.close() } catch (error: Exception) {
            log("close-exception", activeRequest, error, Level.WARNING)
        }
        worker.shutdownNow()
    }

    private fun log(event: String, trace: RequestTrace?, error: Exception? = null,
                    level: Level = Level.INFO) {
        val elapsedMs = trace?.let { (System.nanoTime() - it.startedNanos) / 1_000_000 } ?: 0
        // File exceptions can include a private source path; never log requests, URLs or filenames.
        val reason = error?.message?.replace(source.absolutePath, "<source>")
            ?.replace(source.path, "<source>")?.replace(filename, "<name>")
            ?.replace('\n', ' ')?.replace('\r', ' ')?.take(240)
        logger.log(level, "HTTP $event port=${server.localPort} expected=$length " +
            "written=${trace?.written ?: 0} elapsedMs=$elapsedMs closed=$closed phase=${trace?.phase ?: "idle"}" +
            if (error == null) "" else " error=${error.javaClass.simpleName} reason=$reason")
    }
}
