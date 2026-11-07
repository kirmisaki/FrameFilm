package org.framefilm.ark

import android.annotation.SuppressLint
import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.net.Inet4Address

/** Owns an app-scoped local Wi-Fi request; leaves the process default network alone. */
class ArkApNetworkController(context: Context, private val listener: Listener) {
    interface Listener {
        fun onReady(address: Inet4Address)
        fun onLost()
        fun onError(message: String)
    }

    companion object { val supported: Boolean get() = Build.VERSION.SDK_INT >= 29 }
    private val connectivity = context.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private val main = Handler(Looper.getMainLooper())
    private var callback: ConnectivityManager.NetworkCallback? = null
    private var generation = 0
    private var network: Network? = null
    private var delivered = false

    @SuppressLint("MissingPermission")
    fun connect(session: ArkApSession) {
        check(supported)
        check(callback == null)
        val token = ++generation
        delivered = false
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(WifiNetworkSpecifier.Builder()
                .setSsid(session.ssid).setWpa2Passphrase(session.passphrase).build())
            .build()
        val pending = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(available: Network) {
                if (token == generation) network = available
            }
            override fun onLinkPropertiesChanged(available: Network, properties: LinkProperties) {
                if (token != generation || available != network || delivered) return
                val address = properties.linkAddresses.map { it.address }.filterIsInstance<Inet4Address>()
                    .firstOrNull { !it.isLoopbackAddress && !it.isLinkLocalAddress && !it.isAnyLocalAddress } ?: return
                delivered = true
                listener.onReady(address)
            }
            override fun onLost(lost: Network) {
                if (token != generation || lost != network) return
                network = null
                listener.onLost()
            }
            override fun onUnavailable() {
                if (token == generation) listener.onError("未能连接 Ark 热点，或系统连接请求已取消")
            }
        }
        callback = pending
        try {
            connectivity.requestNetwork(request, pending, main, 60_000)
        } catch (_: SecurityException) {
            close()
            listener.onError("缺少 Wi-Fi 网络连接权限")
        } catch (_: RuntimeException) {
            close()
            listener.onError("无法请求 Ark 热点连接")
        }
    }

    fun close(): Boolean {
        generation++
        val old = callback
        callback = null
        network = null
        if (old != null) {
            try { connectivity.unregisterNetworkCallback(old) }
            catch (_: IllegalArgumentException) { /* A timed-out request may already be released. */ }
            catch (_: RuntimeException) { return false }
        }
        return true
    }
}
