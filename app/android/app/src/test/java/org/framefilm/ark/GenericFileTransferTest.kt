package org.framefilm.ark

import java.io.File
import java.net.Inet4Address
import java.net.Socket
import java.net.URI

/** Dependency-free host probe for the raw-file HTTP leg and destination path rules. */
object GenericFileTransferTest {
    @JvmStatic fun main(args: Array<String>) {
        check(FileTransferPath.isValid("apps/passport/data/photo.bin"))
        listOf("", "/absolute.bin", "apps//file.bin", "apps/../file.bin", "./file.bin",
            "apps\\file.bin", "apps/file.bin\u0000", "apps/通行证/data.bin")
            .forEach { check(!FileTransferPath.isValid(it)) }

        val file = File.createTempFile("ark-generic", ".dat")
        val bytes = ByteArray(257) { ((it * 37) and 255).toByte() }
        file.writeBytes(bytes)
        val address = Inet4Address.getByName("127.0.0.1") as Inet4Address
        try {
            FilmHttpServer(file, "../../anything.dat", address, generic = true).use { server ->
                server.start()
                val uri = URI(server.url)
                check(uri.path == "/upload.bin")
                Socket(uri.host, uri.port).use { socket ->
                    socket.soTimeout = 5_000
                    socket.getOutputStream().write("GET ${uri.path} HTTP/1.1\r\nHost: localhost\r\n\r\n"
                        .toByteArray(Charsets.US_ASCII))
                    val response = socket.getInputStream().readBytes()
                    val delimiter = "\r\n\r\n".toByteArray(Charsets.US_ASCII)
                    val bodyAt = response.indices.firstOrNull { at ->
                        at + delimiter.size <= response.size && delimiter.indices.all { response[at + it] == delimiter[it] }
                    } ?: error("No HTTP headers")
                    check(response.copyOfRange(0, bodyAt).toString(Charsets.US_ASCII).startsWith("HTTP/1.1 200 OK"))
                    check(response.copyOfRange(bodyAt + delimiter.size, response.size).contentEquals(bytes))
                }
            }
        } finally { file.delete() }
        println("GenericFileTransferTest PASS")
    }
}
