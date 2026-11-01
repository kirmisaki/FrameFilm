package org.framefilm.ark

import java.io.ByteArrayInputStream
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLStreamHandler
import java.net.URLStreamHandlerFactory
import java.util.concurrent.CancellationException

/** Host-only bounded HTTP probe; no network or Android runtime required. */
object FirmwareDownloaderTest {
    @JvmStatic fun main(args: Array<String>) {
        var bytes = byteArrayOf(1, 2, 3)
        var status = 200
        URL.setURLStreamHandlerFactory(URLStreamHandlerFactory { protocol ->
            if (protocol != "https") null else object : URLStreamHandler() {
                override fun openConnection(url: URL) = object : HttpURLConnection(url) {
                    override fun connect() {}
                    override fun disconnect() {}
                    override fun usingProxy() = false
                    override fun getResponseCode() = status
                    override fun getInputStream() = ByteArrayInputStream(bytes)
                }
            }
        })
        val folder = File(System.getProperty("java.io.tmpdir"), "firmware-probe-${System.nanoTime()}").apply { mkdirs() }
        val previous = File(folder, "original.bin").apply { writeText("original") }
        val partial = File(folder, "import-test.bin")
        fun fails(action: () -> Unit) {
            check(runCatching(action).isFailure)
            check(!partial.exists())
            check(previous.readText() == "original")
        }
        try {
            FirmwareDownloader().download("https://github.com/test", partial, 3, null) {}
            check(partial.readBytes().contentEquals(bytes))
            partial.delete()
            fails { FirmwareDownloader().download("https://github.com/test", partial, 2, null) {} }
            fails { FirmwareDownloader().download("https://github.com/test", partial, 4, null) {} }
            bytes = byteArrayOf()
            fails { FirmwareDownloader().download("https://github.com/test", partial, 3, null) {} }
            bytes = byteArrayOf(1, 2, 3)
            fails { FirmwareDownloader().download("https://github.com/test", partial, 3, "0".repeat(64)) {} }
            status = 503
            fails { FirmwareDownloader().download("https://github.com/test", partial, 3, null) {} }
            status = 200
            val cancelled = FirmwareDownloader()
            fails { cancelled.download("https://github.com/test", partial, 3, null) { cancelled.cancel() } }
            check(runCatching { cancelled.checkCancelled() }.exceptionOrNull() is CancellationException)
            println("FirmwareDownloaderTest PASS: success, oversized, truncated, empty, SHA mismatch, HTTP failure, cancellation; original preserved")
        } finally { partial.delete(); previous.delete(); folder.delete() }
    }
}
