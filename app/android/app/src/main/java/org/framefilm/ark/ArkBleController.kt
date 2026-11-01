package org.framefilm.ark

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.util.UUID

/** Ark BLE command transport. All public calls and listener callbacks run on main. */
@Suppress("DEPRECATION") // API 29-32 GATT write/callback compatibility.
class ArkBleController(context: Context, private val listener: Listener) {
    interface Listener {
        fun onDevices(devices: List<BluetoothDevice>)
        fun onReady()
        fun onPanel(panelId: Int, width: Int, height: Int)
        fun onState(state: String)
        fun onError(message: String)
        fun onPacket(packet: ByteArray) { }
    }

    private val appContext = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val adapter = appContext.getSystemService(BluetoothManager::class.java)?.adapter
    private val serviceUuid = UUID.fromString("00002000-0000-1000-8000-00805f9b34fb")
    private val characteristicUuid = UUID.fromString("00002001-0000-1000-8000-00805f9b34fb")
    private val cccdUuid = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    private val devices = linkedMapOf<String, BluetoothDevice>()
    private val deviceNames = linkedMapOf<String, String>()
    private var scanCallback: ScanCallback? = null
    private var gatt: BluetoothGatt? = null
    private var characteristic: BluetoothGattCharacteristic? = null
    private var generation = 0
    private var mtu = 23
    private var ready = false
    private class Pending(val channel: Int, val callback: (Result<ByteArray>) -> Unit) {
        var writeDone = false
        var response: ByteArray? = null
    }
    private var pending: Pending? = null
    private var writeOnly: ((Result<Unit>) -> Unit)? = null
    private var timeout: Runnable? = null

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    private fun hasPermissions(): Boolean =
        if (Build.VERSION.SDK_INT >= 31) {
            appContext.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
                appContext.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        } else {
            appContext.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        }

    private fun armTimeout(ms: Long, message: String, report: Boolean = true, action: () -> Unit = {}) {
        timeout?.let(main::removeCallbacks)
        val token = generation
        timeout = Runnable {
            if (token == generation) {
                timeout = null
                action()
                if (report) listener.onError(message)
            }
        }.also { main.postDelayed(it, ms) }
    }

    private fun clearTimeout() {
        timeout?.let(main::removeCallbacks)
        timeout = null
    }

    @SuppressLint("MissingPermission")
    fun startScan() = onMain {
        if (!hasPermissions()) { listener.onError("需要附近设备蓝牙权限"); return@onMain }
        val scanner = adapter?.bluetoothLeScanner
        if (adapter?.isEnabled != true || scanner == null) {
            listener.onError("请先开启蓝牙"); return@onMain
        }
        stopScan(false)
        devices.clear()
        deviceNames.clear()
        listener.onDevices(emptyList())
        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                onMain {
                    if (scanCallback !== this) return@onMain
                    val name = result.scanRecord?.deviceName ?: result.device.name
                    if (name == "FRAMEFILMARK" || name?.startsWith("FRAMEFILMARK-") == true) {
                        devices[result.device.address] = result.device
                        deviceNames[result.device.address] = name
                        listener.onDevices(devices.values.toList())
                    }
                }
            }

            override fun onScanFailed(errorCode: Int) = onMain {
                if (scanCallback === this) {
                    stopScan(false)
                    listener.onError("蓝牙扫描失败：$errorCode")
                }
            }
        }
        scanCallback = callback
        try {
            scanner.startScan(callback)
            listener.onState("扫描 Ark 中")
            main.postDelayed({ if (scanCallback === callback) stopScan() }, 10_000)
        } catch (_: SecurityException) {
            scanCallback = null
            listener.onError("蓝牙扫描权限不可用")
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopScan(notify: Boolean = true) {
        val callback = scanCallback ?: return
        scanCallback = null
        try { adapter?.bluetoothLeScanner?.stopScan(callback) } catch (_: SecurityException) { }
        if (notify) listener.onState("扫描结束：发现 ${devices.size} 台 Ark")
    }

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) = onMain {
        if (!hasPermissions()) { listener.onError("需要附近设备蓝牙权限"); return@onMain }
        stopScan(false)
        closeGatt()
        val token = generation
        listener.onState("连接 Ark 中")
        try {
            val callback = object : BluetoothGattCallback() {
                override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) = onMain {
                    if (token != generation || g !== gatt) return@onMain
                    if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                        listener.onState("发现 GATT 服务中")
                        if (!g.discoverServices()) fail("无法发现 GATT 服务")
                        else armTimeout(12_000, "GATT 服务发现超时") { closeGatt() }
                    } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                        fail("Ark 蓝牙已断开 ($status)")
                    } else if (status != BluetoothGatt.GATT_SUCCESS) {
                        fail("Ark 蓝牙连接失败 ($status)")
                    }
                }

                override fun onServicesDiscovered(g: BluetoothGatt, status: Int) = onMain {
                    if (token != generation || g !== gatt) return@onMain
                    clearTimeout()
                    val ch = g.getService(serviceUuid)?.getCharacteristic(characteristicUuid)
                    val descriptor = ch?.getDescriptor(cccdUuid)
                    if (status != BluetoothGatt.GATT_SUCCESS || ch == null || descriptor == null) {
                        fail("Ark GATT 2000/2001 或通知描述符不可用")
                        return@onMain
                    }
                    characteristic = ch
                    if (!g.setCharacteristicNotification(ch, true)) {
                        fail("无法启用 Ark 通知")
                        return@onMain
                    }
                    val started = if (Build.VERSION.SDK_INT >= 33) {
                        g.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothGatt.GATT_SUCCESS
                    } else {
                        descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        g.writeDescriptor(descriptor)
                    }
                    if (!started) fail("无法写入 Ark 通知配置")
                    else armTimeout(8_000, "启用 Ark 通知超时") { closeGatt() }
                }

                override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) = onMain {
                    if (token != generation || g !== gatt || descriptor.uuid != cccdUuid) return@onMain
                    clearTimeout()
                    if (status != BluetoothGatt.GATT_SUCCESS) fail("Ark 通知配置失败 ($status)")
                    else if (try { !g.requestMtu(200) } catch (_: SecurityException) { true }) readyWithMtu()
                    else armTimeout(6_000, "MTU 协商超时", false) { readyWithMtu() }
                }

                override fun onMtuChanged(g: BluetoothGatt, negotiated: Int, status: Int) = onMain {
                    if (token != generation || g !== gatt) return@onMain
                    mtu = if (status == BluetoothGatt.GATT_SUCCESS) negotiated else 23
                    // A late MTU callback may arrive after fallback readiness while a command is pending.
                    if (!ready) readyWithMtu()
                }

                override fun onCharacteristicWrite(g: BluetoothGatt, ch: BluetoothGattCharacteristic, status: Int) = onMain {
                    if (token != generation || g !== gatt || ch.uuid != characteristicUuid) return@onMain
                    val callback = writeOnly
                    if (callback != null) {
                        writeOnly = null
                        clearTimeout()
                        if (status == BluetoothGatt.GATT_SUCCESS) callback(Result.success(Unit))
                        else callback(Result.failure(IllegalStateException("BLE 写入失败 ($status)")))
                        return@onMain
                    }
                    val request = pending ?: return@onMain
                    if (status != BluetoothGatt.GATT_SUCCESS) {
                        pending = null
                        clearTimeout()
                        request.callback(Result.failure(IllegalStateException("BLE 写入失败 ($status)")))
                    } else {
                        request.writeDone = true
                        completePending(request)
                    }
                }

                override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray) {
                    onMain { if (token == generation && g === gatt && ch.uuid == characteristicUuid) acceptResponse(value) }
                }

                @Deprecated("Pre-Android 13 callback")
                override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
                    if (Build.VERSION.SDK_INT < 33) {
                        val value = ch.value?.clone() ?: return
                        onMain { if (token == generation && g === gatt && ch.uuid == characteristicUuid) acceptResponse(value) }
                    }
                }
            }
            gatt = device.connectGatt(appContext, false, callback, BluetoothDevice.TRANSPORT_LE)
            if (gatt == null) fail("无法启动 Ark 蓝牙连接")
            else armTimeout(12_000, "连接 Ark 超时") { closeGatt() }
        } catch (_: SecurityException) { fail("蓝牙连接权限不可用") }
    }

    private fun readyWithMtu() {
        if (ready) return
        clearTimeout()
        ready = true
        listener.onState("Ark GATT 已就绪，MTU $mtu")
        listener.onReady()
    }

    val isReady: Boolean get() = ready
    fun scannedName(address: String): String? = deviceNames[address]
    val isBusy: Boolean get() = pending != null || writeOnly != null
    val negotiatedMtu: Int get() = mtu

    @SuppressLint("MissingPermission")
    fun command(channel: Int, payload: ByteArray = byteArrayOf(), waitMs: Long = 5_000,
                callback: (Result<ByteArray>) -> Unit) = onMain {
        val g = gatt
        val ch = characteristic
        val error = when {
            !hasPermissions() -> "需要附近设备蓝牙权限"
            !ready || g == null || ch == null -> "请先连接 Ark 并等待 GATT 就绪"
            pending != null || writeOnly != null -> "上一条 BLE 命令仍在等待响应"
            channel !in 0..255 || payload.size > 192 -> "BLE 命令长度无效"
            payload.size + 4 > mtu - 3 -> "BLE MTU $mtu 不足以发送 ${payload.size + 4} 字节；请重新连接"
            else -> null
        }
        if (error != null) { callback(Result.failure(IllegalStateException(error))); return@onMain }
        val packet = ByteArray(payload.size + 4)
        packet[0] = 0x55
        packet[1] = channel.toByte()
        packet[2] = payload.size.toByte()
        payload.copyInto(packet, 3)
        packet[packet.lastIndex] = (packet.dropLast(1).sumOf { it.toInt() and 0xff } and 0xff).toByte()
        val request = Pending(channel, callback)
        pending = request
        val started = try {
            if (Build.VERSION.SDK_INT >= 33) {
                g!!.writeCharacteristic(ch!!, packet, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
            } else {
                ch!!.value = packet
                ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                g!!.writeCharacteristic(ch)
            }
        } catch (_: SecurityException) { false }
        if (!started) {
            pending = null
            callback(Result.failure(IllegalStateException("BLE 命令 0x${channel.toString(16)} 发送失败")))
        } else {
            armTimeout(waitMs, "BLE 命令响应超时", false) {
                if (pending === request) {
                    pending = null
                    callback(Result.failure(IllegalStateException("BLE 0x${channel.toString(16)} 响应超时")))
                }
            }
        }
    }

    fun sendNoReply(channel: Int, payload: ByteArray, callback: (Result<Unit>) -> Unit) = onMain {
        // 0x4B has no application response; wait for the GATT write completion.
        if (!ready || pending != null || writeOnly != null) {
            callback(Result.failure(IllegalStateException("BLE 尚未就绪或命令忙")))
            return@onMain
        }
        val g = gatt ?: return@onMain callback(Result.failure(IllegalStateException("BLE 已断开")))
        val ch = characteristic ?: return@onMain callback(Result.failure(IllegalStateException("BLE 已断开")))
        if (payload.size + 4 > mtu - 3) {
            callback(Result.failure(IllegalStateException("BLE MTU 不足")))
            return@onMain
        }
        val packet = byteArrayOf(0x55, channel.toByte(), payload.size.toByte()) + payload
        val framed = packet + byteArrayOf((packet.sumOf { it.toInt() and 0xff } and 0xff).toByte())
        writeOnly = callback
        val started = try {
            if (Build.VERSION.SDK_INT >= 33) g.writeCharacteristic(ch, framed, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
            else { ch.value = framed; ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT; g.writeCharacteristic(ch) }
        } catch (_: SecurityException) { false }
        if (!started) {
            writeOnly = null
            callback(Result.failure(IllegalStateException("BLE 写入未启动")))
        } else armTimeout(5_000, "BLE 写入超时", false) {
            if (writeOnly === callback) {
                writeOnly = null
                callback(Result.failure(IllegalStateException("BLE 写入超时")))
            }
        }
    }

    fun readPanel() = command(0x42) { result ->
        val data = result.getOrElse { listener.onError(it.message ?: "屏幕参数读取失败"); return@command }
        if (data.size != 5) { listener.onError("屏幕参数响应长度错误"); return@command }
        val panelId = data[0].toInt() and 0xff
        val width = ((data[1].toInt() and 0xff) shl 8) or (data[2].toInt() and 0xff)
        val height = ((data[3].toInt() and 0xff) shl 8) or (data[4].toInt() and 0xff)
        listener.onPanel(panelId, width, height)
    }

    private fun acceptResponse(data: ByteArray) {
        if (data.size < 4 || data[0].toInt() and 0xff != 0x55) return
        val length = data[2].toInt() and 0xff
        // Existing 0x31 getter sends one uninitialized trailing byte after a valid frame.
        if (length + 4 != data.size && !(requestChannel(data) == 0x31 && length == 1 && data.size == 6)) return
        val validSum = data.copyOfRange(0, length + 3).sumOf { it.toInt() and 0xff } and 0xff
        if (validSum != (data[length + 3].toInt() and 0xff)) return
        listener.onPacket(data.copyOfRange(0, length + 4))
        val request = pending ?: return
        if (data[1].toInt() and 0xff != request.channel) return
        request.response = data.copyOfRange(3, 3 + length)
        completePending(request)
    }

    private fun completePending(request: Pending) {
        if (pending !== request || !request.writeDone) return
        val response = request.response ?: return
        pending = null
        clearTimeout()
        request.callback(Result.success(response))
    }

    private fun requestChannel(data: ByteArray): Int = data[1].toInt() and 0xff

    private fun fail(message: String) {
        closeGatt()
        listener.onError(message)
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt() {
        generation++
        clearTimeout()
        ready = false
        mtu = 23
        val interrupted = pending
        pending = null
        val interruptedWrite = writeOnly
        writeOnly = null
        characteristic = null
        val old = gatt
        gatt = null
        try { old?.disconnect() } catch (_: SecurityException) { }
        old?.close()
        interrupted?.callback?.invoke(Result.failure(IllegalStateException("Ark 蓝牙已断开")))
        interruptedWrite?.invoke(Result.failure(IllegalStateException("Ark 蓝牙已断开")))
    }

    fun close() = onMain {
        stopScan(false)
        closeGatt()
        listener.onState("Ark 蓝牙已关闭")
    }
}
