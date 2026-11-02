package org.framefilm.ark

import java.net.Inet4Address
import java.net.InetAddress

/** RAM-only credentials returned by DIRECT_AP_PREPARE. Never include these in logs. */
data class ArkApSession(val ssid: String, val passphrase: String, val deviceAddress: Inet4Address) {
    companion object {
        fun parse(data: ByteArray): ArkApSession {
            require(data.isNotEmpty() && data[0] == 0.toByte()) { "Ark 未接受热点准备请求" }
            val fields = mutableListOf<String>()
            var start = 1
            for (at in 1 until data.size) {
                require(data[at].toInt() and 0x80 == 0) { "Ark 热点参数编码无效" }
                if (data[at] == 0.toByte()) {
                    fields += data.copyOfRange(start, at).toString(Charsets.US_ASCII)
                    start = at + 1
                }
            }
            require(fields.size == 3 && start == data.size) { "Ark 热点参数长度无效" }
            require(fields[0].length in 1..32 && fields[1].length in 8..63) { "Ark 热点凭据长度无效" }
            val octets = fields[2].split('.')
            require(octets.size == 4 && octets.all { it.isNotEmpty() && it.all(Char::isDigit) && it.toIntOrNull() in 0..255 }) {
                "Ark 热点 IPv4 地址无效"
            }
            val address = InetAddress.getByAddress(octets.map { it.toInt().toByte() }.toByteArray()) as Inet4Address
            require(!address.isAnyLocalAddress && !address.isLoopbackAddress) { "Ark 热点 IPv4 地址无效" }
            return ArkApSession(fields[0], fields[1], address)
        }
    }
}
