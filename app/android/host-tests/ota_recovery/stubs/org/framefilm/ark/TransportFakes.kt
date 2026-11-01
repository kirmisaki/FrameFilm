package org.framefilm.ark

import android.bluetooth.BluetoothDevice
import android.content.Context
import java.io.File
import java.net.Inet4Address
import java.net.InetAddress

class ArkBleController(@Suppress("UNUSED_PARAMETER") context: Context, private val listener: Listener) {
    interface Listener {
        fun onDevices(devices: List<BluetoothDevice>)
        fun onReady()
        fun onPanel(panelId: Int, width: Int, height: Int)
        fun onState(state: String)
        fun onError(message: String)
        fun onPacket(packet: ByteArray) { }
    }

    data class Pending(val channel: Int, val callback: (Result<ByteArray>) -> Unit)
    companion object { lateinit var current: ArkBleController }
    init { current = this }

    var isReady = false
        private set
    val isBusy: Boolean get() = pending != null
    val negotiatedMtu = 200
    var pending: Pending? = null
        private set
    val channels = mutableListOf<Int>()
    var connectCount = 0
        private set
    private val devices = listOf(BluetoothDevice("AA:00"), BluetoothDevice("BB:00"))

    fun startScan() { listener.onDevices(devices) }
    fun scannedName(address: String): String? = devices.find { it.address == address }?.address
    fun connect(@Suppress("UNUSED_PARAMETER") device: BluetoothDevice) {
        connectCount++
        forceReady()
    }
    fun forceReady() {
        isReady = true
        listener.onState("ready")
        listener.onReady()
    }
    fun disconnectNow() {
        isReady = false
        val interrupted = pending
        pending = null
        interrupted?.callback?.invoke(Result.failure(IllegalStateException("BLE disconnected")))
        listener.onError("BLE disconnected")
    }
    var closeCount = 0
        private set
    fun close() { closeCount++; isReady = false }
    fun sendNoReply(@Suppress("UNUSED_PARAMETER") channel: Int,
                    @Suppress("UNUSED_PARAMETER") payload: ByteArray,
                    callback: (Result<Unit>) -> Unit) = callback(Result.success(Unit))
    fun command(channel: Int, @Suppress("UNUSED_PARAMETER") payload: ByteArray = byteArrayOf(),
                @Suppress("UNUSED_PARAMETER") waitMs: Long = 5_000,
                callback: (Result<ByteArray>) -> Unit) {
        check(isReady && pending == null) { "Fake BLE command overlap or disconnected" }
        channels += channel
        pending = Pending(channel, callback)
    }
    fun reply(channel: Int, bytes: ByteArray) {
        val request = requireNotNull(pending)
        check(request.channel == channel) { "Expected ${request.channel}, got $channel" }
        pending = null
        request.callback(Result.success(bytes))
    }
    fun fail(channel: Int) {
        val request = requireNotNull(pending)
        check(request.channel == channel)
        pending = null
        request.callback(Result.failure(IllegalStateException("synthetic BLE failure")))
    }
}

class ArkP2pController(@Suppress("UNUSED_PARAMETER") context: Context, private val listener: Listener) {
    interface Listener {
        fun onGroupReady(frequencyMhz: Int)
        fun onState(state: String)
        fun onError(message: String)
        fun onSessionReady(session: Session) { }
    }
    data class Session(val ssid: String, val passphrase: String, val ownerAddress: Inet4Address)
    companion object { lateinit var current: ArkP2pController }
    init { current = this }
    var hasPendingCleanup = false
        private set
    var closeCallback: ((Boolean) -> Unit)? = null
        private set
    fun createGroup2Ghz() {
        hasPendingCleanup = true
        listener.onGroupReady(2412)
        listener.onSessionReady(Session("DIRECT-OTA", "12345678",
            InetAddress.getByName("192.168.49.1") as Inet4Address))
    }
    fun close(callback: (Boolean) -> Unit) {
        check(closeCallback == null) { "Fake P2P close overlap" }
        closeCallback = callback
    }
    fun finishClose(clean: Boolean) {
        val callback = requireNotNull(closeCallback)
        closeCallback = null
        if (clean) hasPendingCleanup = false
        callback(clean)
    }
}

class FilmHttpServer(
    file: File,
    @Suppress("UNUSED_PARAMETER") fileName: String,
    @Suppress("UNUSED_PARAMETER") address: Inet4Address,
    @Suppress("UNUSED_PARAMETER") firmware: Boolean = false,
    @Suppress("UNUSED_PARAMETER") generic: Boolean = false,
) {
    val size = file.length()
    val url = "http://192.168.49.1/firmware.bin"
    fun start() { }
    fun close() { }
}
