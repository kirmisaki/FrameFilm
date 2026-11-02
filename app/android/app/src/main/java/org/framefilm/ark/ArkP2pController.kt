package org.framefilm.ark

import android.Manifest
import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.net.wifi.p2p.WifiP2pConfig
import android.net.wifi.p2p.WifiP2pGroup
import android.net.wifi.p2p.WifiP2pManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.net.Inet4Address
import java.security.SecureRandom

/** Creates a temporary Wi-Fi Direct group owned by this controller session. */
class ArkP2pController(context: Context, private val listener: Listener) {
    interface Listener {
        fun onGroupReady(frequencyMhz: Int)
        fun onState(state: String)
        fun onError(message: String)
        fun onSessionReady(session: Session) { }
    }

    data class Session(val ssid: String, val passphrase: String, val ownerAddress: Inet4Address)

    private val appContext = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private val manager = appContext.getSystemService(Context.WIFI_P2P_SERVICE) as? WifiP2pManager
    private var channel: WifiP2pManager.Channel? = null
    private var channelGeneration = 0
    private var receiver: BroadcastReceiver? = null
    private var sessionName: String? = null
    private var sessionPassphrase: String? = null
    private var generation = 0
    private var checking = false
    private var creating = false
    private var createAccepted = false
    private var legacyCreatePending = false
    private var legacyCreateId = 0
    private var cleanupRequested = false
    private var owned = false
    private var closing = false
    private var removing = false
    private var timeout: Runnable? = null
    private val closeCallbacks = mutableListOf<(Boolean) -> Unit>()
    private val random = SecureRandom()

    /** A failed cleanup retains the exact group identity for a later retry. */
    val hasPendingCleanup: Boolean get() = sessionName != null || legacyCreatePending

    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    private fun hasPermission(): Boolean = if (Build.VERSION.SDK_INT >= 33) {
        appContext.checkSelfPermission(Manifest.permission.NEARBY_WIFI_DEVICES) == PackageManager.PERMISSION_GRANTED
    } else {
        appContext.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
    }

    private fun clearTimeout() {
        timeout?.let(main::removeCallbacks)
        timeout = null
    }

    private fun setTimeout(ms: Long, action: () -> Unit) {
        clearTimeout()
        timeout = Runnable { timeout = null; action() }.also { main.postDelayed(it, ms) }
    }

    private fun ensureChannel(): WifiP2pManager.Channel? {
        if (channel != null) return channel
        val p2p = manager ?: return null
        val token = ++channelGeneration
        channel = p2p.initialize(appContext, Looper.getMainLooper()) {
            onMain {
                if (token != channelGeneration || channel == null) return@onMain
                channel = null
                if (checking || creating || owned) listener.onError("Wi-Fi Direct 通道已断开")
                finishClose(false)
            }
        }
        if (receiver == null) {
            receiver = object : BroadcastReceiver() {
                override fun onReceive(context: Context, intent: Intent) {
                    if (intent.action == WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION &&
                        (creating || closing)) requestGroup()
                }
            }.also {
                val filter = IntentFilter(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
                if (Build.VERSION.SDK_INT >= 33) appContext.registerReceiver(it, filter, Context.RECEIVER_EXPORTED)
                else appContext.registerReceiver(it, filter)
            }
        }
        return channel
    }

    @SuppressLint("MissingPermission")
    fun createGroup2Ghz() = onMain {
        if (!hasPermission()) { listener.onError("需要附近 Wi-Fi 设备权限"); return@onMain }
        if (checking || creating || owned || closing || hasPendingCleanup) {
            listener.onError("直连组操作正在进行"); return@onMain
        }
        val p2p = manager
        val ch = ensureChannel()
        if (p2p == null || ch == null) { listener.onError("设备不支持 Wi-Fi Direct"); return@onMain }
        checking = true
        val token = ++generation
        listener.onState("检查现有直连组")
        try {
            p2p.requestGroupInfo(ch) { existing -> onMain {
                if (token != generation || !checking || closing) return@onMain
                checking = false
                if (existing != null) {
                    listener.onError("已有直连组，未修改该组")
                    return@onMain
                }
                val legacy = Build.VERSION.SDK_INT < 29
                val config = if (legacy) null else create2GhzConfig()
                sessionName = config?.networkName
                sessionPassphrase = config?.passphrase
                legacyCreatePending = legacy
                val legacyId = if (legacy) ++legacyCreateId else 0
                createAccepted = false
                cleanupRequested = false
                creating = true
                listener.onState(if (legacy) "创建直连组中（系统选择频段）" else "创建 2.4GHz 直连组中")
                setTimeout(20_000) {
                    if (creating) {
                        listener.onError("创建直连组超时，正在检查并清理")
                        cleanupRequested = true
                        closing = true
                        requestGroup()
                        setTimeout(5_000) { finishClose(false) }
                    }
                }
                try {
                    val action = object : WifiP2pManager.ActionListener {
                        override fun onSuccess() = onMain {
                            if (legacy && legacyId != legacyCreateId) return@onMain
                            if (!legacy && token != generation) return@onMain
                            createAccepted = true
                            if (cleanupRequested && !closing) {
                                closing = true
                                setTimeout(5_000) { finishClose(false) }
                            }
                            requestGroup()
                        }
                        override fun onFailure(reason: Int) = onMain {
                            if (legacy && legacyId != legacyCreateId) return@onMain
                            if (!legacy && token != generation) return@onMain
                            creating = false
                            createAccepted = false
                            legacyCreatePending = false
                            clearTimeout()
                            if (cleanupRequested) finishClose(true)
                            else {
                                sessionName = null
                                sessionPassphrase = null
                                listener.onError("创建直连组失败：$reason")
                            }
                        }
                    }
                    if (config == null) p2p.createGroup(ch, action)
                    else p2p.createGroup(ch, config, action)
                } catch (_: SecurityException) {
                    creating = false
                    createAccepted = false
                    legacyCreatePending = false
                    sessionName = null
                    sessionPassphrase = null
                    clearTimeout()
                    if (cleanupRequested) finishClose(true)
                    else listener.onError("Wi-Fi Direct 权限不可用")
                }
            } }
        } catch (_: SecurityException) {
            checking = false
            listener.onError("Wi-Fi Direct 权限不可用")
        }
    }

    private fun create2GhzConfig(): WifiP2pConfig {
        val alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        fun token(length: Int): String = buildString {
            repeat(length) { append(alphabet[random.nextInt(alphabet.length)]) }
        }
        return WifiP2pConfig.Builder()
            .setNetworkName("DIRECT-${token(2)}-${token(8)}")
            .setPassphrase(token(16))
            .setGroupOperatingBand(WifiP2pConfig.GROUP_OWNER_BAND_2GHZ)
            .build()
    }

    @SuppressLint("MissingPermission")
    private fun requestGroup() {
        val p2p = manager ?: return
        val ch = channel ?: ensureChannel() ?: return
        val token = generation
        try {
            p2p.requestGroupInfo(ch) { group -> onMain { if (token == generation) handleGroup(group) } }
        } catch (_: SecurityException) { listener.onError("无法查询直连组") }
    }

    private fun handleGroup(group: WifiP2pGroup?) {
        if (!createAccepted && !owned && (sessionName == null || !closing)) return
        if (sessionName == null && legacyCreatePending && group?.isGroupOwner == true) {
            sessionName = group.networkName
            sessionPassphrase = group.passphrase
            legacyCreatePending = false
        }
        val name = sessionName ?: return
        if (group == null) {
            if (closing && !creating && !removing) finishClose(true)
            return
        }
        if (group.networkName != name) {
            if (closing) finishClose(false)
            else listener.onError("出现其他直连组，未修改该组")
            return
        }
        if (owned && !closing) return
        owned = true
        creating = false
        if (closing) { removeOwnedGroup(); return }
        val frequency = if (Build.VERSION.SDK_INT >= 29) group.frequency else 0
        if (!group.isGroupOwner || (Build.VERSION.SDK_INT >= 29 && frequency !in 2400..2500)) {
            listener.onError("直连组不是 2.4GHz Group Owner，正在释放")
            closing = true
            removeOwnedGroup()
            return
        }
        clearTimeout()
        listener.onState(if (frequency == 0) "直连组已就绪（频段由系统选择）" else "2.4GHz 直连组已就绪")
        listener.onGroupReady(frequency)
        requestSessionInfo(group, 0)
    }

    @SuppressLint("MissingPermission")
    private fun requestSessionInfo(group: WifiP2pGroup, attempt: Int) {
        val p2p = manager ?: return
        val ch = channel ?: return
        val token = generation
        try {
            p2p.requestConnectionInfo(ch) { info -> onMain {
                if (token != generation || !owned || closing) return@onMain
                val address = info?.groupOwnerAddress as? Inet4Address
                val groupPassphrase = group.passphrase
                val passphrase = groupPassphrase.takeUnless { it.isNullOrEmpty() } ?: sessionPassphrase
                if (info?.isGroupOwner == true && info.groupFormed && address != null && !passphrase.isNullOrEmpty()) {
                    listener.onSessionReady(Session(group.networkName, passphrase, address))
                    return@onMain
                }
                val diagnostic = "owner=${info?.isGroupOwner == true}, groupFormed=${info?.groupFormed == true}, " +
                    "addressPresent=${address != null}, passphrasePresent=${!groupPassphrase.isNullOrEmpty()}"
                if (attempt == 0) listener.onState("直连连接信息未就绪 ($diagnostic)，等待 IP")
                if (attempt < 19) main.postDelayed({
                    if (token == generation && owned && !closing) requestSessionInfo(group, attempt + 1)
                }, 500)
                else listener.onError("无法取得直连组连接信息 ($diagnostic)")
            } }
        } catch (_: SecurityException) { listener.onError("无法查询直连组连接信息：权限不可用") }
    }

    @SuppressLint("MissingPermission")
    private fun removeOwnedGroup() {
        val p2p = manager
        val ch = channel ?: ensureChannel()
        val name = sessionName
        if (!owned || p2p == null || ch == null || name == null) { finishClose(false); return }
        if (removing) return
        removing = true
        val token = generation
        try {
            p2p.requestGroupInfo(ch) { group -> onMain {
                if (token != generation) return@onMain
                if (group?.networkName != name) {
                    finishClose(true)
                } else {
                    removeVerifiedGroup(p2p, ch, 0)
                }
            } }
        } catch (_: SecurityException) {
            listener.onError("无法确认本次直连组：权限不可用")
            finishClose(false)
        }
    }

    @SuppressLint("MissingPermission")
    private fun removeVerifiedGroup(p2p: WifiP2pManager, ch: WifiP2pManager.Channel, attempt: Int) {
        listener.onState("释放本次创建的直连组中")
        val token = generation
        try {
            p2p.removeGroup(ch, object : WifiP2pManager.ActionListener {
                override fun onSuccess() = onMain {
                    if (token != generation) return@onMain
                    clearTimeout()
                    // The action was accepted; confirm the owned group actually disappeared.
                    main.postDelayed({
                        if (token != generation) return@postDelayed
                        p2p.requestGroupInfo(ch) { group -> onMain {
                            if (token != generation) return@onMain
                            if (group?.networkName != sessionName) finishClose(true)
                            else retryRemove(p2p, ch, attempt, "组仍存在")
                        } }
                    }, 500)
                }
                override fun onFailure(reason: Int) = onMain {
                    if (token != generation) return@onMain
                    clearTimeout()
                    retryRemove(p2p, ch, attempt, "失败码 $reason")
                }
            })
            setTimeout(8_000) {
                if (token != generation) return@setTimeout
                retryRemove(p2p, ch, attempt, "超时")
            }
        } catch (_: SecurityException) {
            listener.onError("无法释放直连组：权限不可用")
            finishClose(false)
        }
    }

    private fun retryRemove(p2p: WifiP2pManager, ch: WifiP2pManager.Channel, attempt: Int, reason: String) {
        if (attempt < 3) main.postDelayed({
            if (closing && channel === ch) removeVerifiedGroup(p2p, ch, attempt + 1)
        }, 600)
        else {
            listener.onError("释放直连组未完成（$reason，已尝试 ${attempt + 1} 次）")
            finishClose(false)
        }
    }

    fun close(onComplete: (Boolean) -> Unit = {}) = onMain {
        closeCallbacks += onComplete
        if (closing) return@onMain
        closing = true
        cleanupRequested = true
        clearTimeout()
        if (owned) removeOwnedGroup()
        else if (hasPendingCleanup) {
            requestGroup()
            setTimeout(if (creating) 20_000 else 5_000) {
                requestGroup()
                setTimeout(3_000) {
                    listener.onError("无法确认本次创建的直连组已清理")
                    finishClose(false)
                }
            }
        } else finishClose(true)
    }

    private fun finishClose(success: Boolean) {
        generation++
        clearTimeout()
        if (!success && hasPendingCleanup) {
            checking = false
            creating = false
            closing = false
            removing = false
            listener.onState("Wi-Fi Direct 清理未确认，可重试释放本次组")
            val callbacks = closeCallbacks.toList()
            closeCallbacks.clear()
            callbacks.forEach { it(false) }
            return
        }
        receiver?.let { try { appContext.unregisterReceiver(it) } catch (_: IllegalArgumentException) { } }
        receiver = null
        val oldChannel = channel
        channel = null
        channelGeneration++ // 先作废，再 close；迟到回调不得覆盖本次清理结果或新通道。
        oldChannel?.close()
        sessionName = null
        sessionPassphrase = null
        createAccepted = false
        legacyCreatePending = false
        legacyCreateId++
        cleanupRequested = false
        checking = false
        creating = false
        owned = false
        closing = false
        removing = false
        listener.onState(if (success) "Wi-Fi Direct 已关闭" else "Wi-Fi Direct 清理未确认")
        val callbacks = closeCallbacks.toList()
        closeCallbacks.clear()
        callbacks.forEach { it(success) }
    }
}
