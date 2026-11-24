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
    private var generation = 0
    val readyLinkId get() = generation.takeIf { isReady }
    val isBusy: Boolean get() = pending != null || writePending != null
    var isConnecting = false
    var autoReady = true
    var negotiatedMtu = 200
    data class Sent(val channel: Int, val payload: ByteArray)
    val sent = mutableListOf<Sent>()
    val requests = mutableListOf<Sent>()
    var autoCompleteWrites = true
    var writePending: ((Result<Unit>) -> Unit)? = null
        private set
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
        generation++
        isReady = false
        isConnecting = true
        if (autoReady) forceReady()
    }
    fun forceReady() {
        isReady = true
        isConnecting = false
        listener.onState("ready")
        listener.onReady()
    }
    fun reportState(message: String) = listener.onState(message)
    fun disconnectNow() {
        generation++
        isReady = false
        isConnecting = false
        val interrupted = pending
        pending = null
        interrupted?.callback?.invoke(Result.failure(IllegalStateException("BLE disconnected")))
        val write = writePending; writePending = null
        write?.invoke(Result.failure(IllegalStateException("BLE disconnected")))
        listener.onError("BLE disconnected")
    }
    var closeCount = 0
        private set
    fun close() {
        closeCount++; generation++; isReady = false; isConnecting = false
        val command = pending; pending = null
        val write = writePending; writePending = null
        command?.callback?.invoke(Result.failure(IllegalStateException("closed")))
        write?.invoke(Result.failure(IllegalStateException("closed")))
        listener.onState("closed")
    }
    fun sendNoReply(channel: Int, payload: ByteArray, callback: (Result<Unit>) -> Unit) {
        check(isReady && !isBusy) { "Fake BLE write overlap or disconnected" }
        check(payload.size <= minOf(192, negotiatedMtu - 7))
        channels += channel
        sent += Sent(channel, payload.copyOf())
        if (autoCompleteWrites) callback(Result.success(Unit)) else writePending = callback
    }
    fun completeWrite(result: Result<Unit> = Result.success(Unit)) {
        val write = requireNotNull(writePending); writePending = null; write(result)
    }
    fun notifyPacket(packet: ByteArray) = listener.onPacket(packet)
    fun command(channel: Int, payload: ByteArray = byteArrayOf(),
                @Suppress("UNUSED_PARAMETER") waitMs: Long = 5_000,
                callback: (Result<ByteArray>) -> Unit) {
        check(isReady && pending == null) { "Fake BLE command overlap or disconnected" }
        check(payload.size <= minOf(192, negotiatedMtu - 7))
        channels += channel
        requests += Sent(channel, payload.copyOf())
        pending = Pending(channel, callback)
    }
    fun reply(channel: Int, bytes: ByteArray) {
        val request = requireNotNull(pending)
        check(request.channel == channel) { "Expected ${request.channel}, got $channel" }
        pending = null
        request.callback(Result.success(bytes))
    }
    fun fail(channel: Int, error: Exception = IllegalStateException("synthetic BLE failure")) {
        val request = requireNotNull(pending)
        check(request.channel == channel)
        pending = null
        request.callback(Result.failure(error))
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
    companion object { lateinit var current: FilmHttpServer }
    init { current = this }
    val size = file.length()
    val url = "http://192.168.49.1/firmware.bin"
    var writtenBytes = 0L
    var transferFinished = false
    var closed = false
    fun start() { }
    fun close() { closed = true }
}

class ArkApNetworkController(@Suppress("UNUSED_PARAMETER") context: Context, private val listener: Listener) {
    interface Listener {
        fun onReady(address: Inet4Address)
        fun onLost()
        fun onError(message: String)
    }
    companion object {
        var supported = false
        lateinit var current: ArkApNetworkController
    }
    init { current = this }
    var closed = false
    fun connect(@Suppress("UNUSED_PARAMETER") session: ArkApSession) {
        listener.onReady(InetAddress.getByName("192.168.4.2") as Inet4Address)
    }
    fun loseNetwork() = listener.onLost()
    fun close(): Boolean { closed = true; return true }
}
