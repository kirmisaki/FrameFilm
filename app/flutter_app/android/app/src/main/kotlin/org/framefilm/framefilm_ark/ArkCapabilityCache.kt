package org.framefilm.framefilm_ark

/** Only validated v1 0x59 replies are evidence, and only on their own GATT link. */
internal class ArkCapabilityCache {
    private var linkId: Int? = null
    private var payload: ByteArray? = null

    operator fun get(currentLinkId: Int?): ByteArray? =
        payload.takeIf { currentLinkId != null && linkId == currentLinkId }

    fun update(currentLinkId: Int, data: ByteArray) {
        if (!isV1(data)) return
        linkId = currentLinkId
        payload = data
    }

    fun clear() {
        linkId = null
        payload = null
    }

    companion object {
        fun isV1(data: ByteArray) = data.size == 5 && data[0] == 1.toByte()

        fun hasBit(data: ByteArray?, bit: Long): Boolean {
            if (data == null || !isV1(data)) return false
            val flags = (1..4).fold(0L) { value, index -> (value shl 8) or (data[index].toLong() and 255) }
            return (flags and bit) != 0L
        }
    }
}
