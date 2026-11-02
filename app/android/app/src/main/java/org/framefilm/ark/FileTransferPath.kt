package org.framefilm.ark

internal object FileTransferPath {
    fun isValid(path: String): Boolean = path.isNotEmpty() && !path.startsWith('/') &&
        !path.contains('\\') && !path.any { it.code !in 0x20..0x7e } &&
        path.split('/').all { it.isNotEmpty() && it != "." && it != ".." }
}
