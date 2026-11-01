package org.framefilm.framefilm_ark

import android.Manifest
import android.app.Activity
import android.content.ClipData
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.MediaStore
import android.provider.OpenableColumns
import java.io.File
import java.io.InputStream
import java.util.concurrent.Executors
import org.framefilm.ark.FilmConverter
import org.framefilm.ark.FirmwareDownloader
import org.framefilm.ark.ArkFirmware
import org.framefilm.ark.TransferSnapshot
import android.bluetooth.BluetoothDevice
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import org.framefilm.ark.ArkSession

/** One activity-owned session; all state and callbacks run on the Android main thread. */
class ArkBridge(private val activity: Activity, messenger: BinaryMessenger) :
    MethodChannel.MethodCallHandler, EventChannel.StreamHandler {
    private val main = Handler(Looper.getMainLooper())
    private val methods = MethodChannel(messenger, "org.framefilm.ark/methods")
    private val events = EventChannel(messenger, "org.framefilm.ark/events")
    private var sink: EventChannel.EventSink? = null
    private val appUpdates by lazy { AppUpdateManager(activity) { emit() } }
    private var closed = false
    private val worker = Executors.newSingleThreadExecutor()
    private val cache = File(activity.cacheDir, "flutter-film").apply {
        mkdirs()
        // Once per process: a previous process cannot still serve these cached files.
        if (!cacheInitialized) {
            listFiles()?.filter { it.isFile && (it.name.startsWith("import-") ||
                it.name.startsWith("passport-")) }?.forEach { it.delete() }
            cacheInitialized = true
        }
    }
    private var currentFile: File? = null
    private var importedName: String? = null
    private var firmwareFile: File? = null
    private var importedFirmware: ArkFirmware? = null
    private var firmwareName: String? = null
    private class Download(val downloader: FirmwareDownloader, val result: MethodChannel.Result) {
        @Volatile var file: File? = null
    }
    private var download: Download? = null
    private var firmwareDownload: Map<String, Any> = downloadState("idle", 0, 0, "", false)
    private var pickingFirmware = false
    private var importing = false
    private var importResult: MethodChannel.Result? = null
    private var transferPermission = false
    private var passportPermission = false
    private var transfer = TransferSnapshot("idle", "请导入 film 文件", cleanupCompleted = true)
    private var connected = false
    private var message = "请扫描并连接 Ark"
    private var devices: List<Map<String, String>> = emptyList()
    private var advertisedName: String? = null
    private var name: String? = null
    private var battery: Int? = null
    private var width: Int? = null
    private var height: Int? = null
    private var autoSleep: Boolean? = null
    private var timedWake: Boolean? = null
    private var wakeMinutes: Int? = null
    private var syncedAt: Long? = null
    private var settingsMessage: String? = null
    private var permissionAction: (() -> Unit)? = null
    private var permissionResult: MethodChannel.Result? = null
    private var refreshResult: MethodChannel.Result? = null
    private var refreshing = false
    private var readIndex = 0
    private val channels = intArrayOf(0x42, 0x23, 0x54, 0x26, 0x28, 0x2A)
    private class Read(val channel: Int, val done: ((ByteArray) -> Unit)? = null) {
        var writeDone = false
        var response: ByteArray? = null
        var timeout: Runnable? = null
    }
    private var fileListing = false
    private val deviceFiles = linkedMapOf<Int, String>()
    private var fileDirectory = "film"
    private var catalogSupported: Boolean? = null
    private var storage: Map<String, Any?> = mapOf("phase" to "idle")
    private var canDeleteFiles = false
    private var filesLoaded = false
    private var currentFileId: Int? = null
    private var filesMessage = "刷新设备当前目录"
    private var pending: Read? = null
    private data class PassportFiles(val bin: File, val json: File, val expected: String, val color: Boolean) {
        fun delete() { bin.delete(); json.delete() }
    }
    private class PassportJob(val result: MethodChannel.Result, val expected: String? = null) {
        val bytes = java.io.ByteArrayOutputStream()
        var total = -1
        var binDone = false
        var filesStarted = false
        var jsonDone = false
        var retries = 0
        var awaitingFile = false
        var fileIndex = 0
        var cancelRequested = false
        var interrupted: String? = null
    }
    private var passportJob: PassportJob? = null
    private var passportFiles: PassportFiles? = null
    private var passportJson: String? = null
    private var passportRevision = 0
    private var passportPhase = "idle"
    private var passportCompleted = 0
    private var passportTotal = 0
    private var passportMessage = ""
    private var avatarResult: MethodChannel.Result? = null
    private var frameResult: MethodChannel.Result? = null
    private var saveFilmResult: MethodChannel.Result? = null
    private var saveFilmBytes: ByteArray? = null
    private var frameCameraUri: Uri? = null
    private val frameCameraFile = File(activity.cacheDir, "ark-camera/photo.jpg")
    private val drafts get() = activity.getSharedPreferences("ark-passport", Activity.MODE_PRIVATE)
    private fun passportState(): Map<String, Any?> = mapOf(
        "phase" to passportPhase, "completed" to passportCompleted, "total" to passportTotal,
        "message" to passportMessage, "canCancel" to (passportJob != null),
        "canRetry" to (passportJob == null && passportPhase in setOf("error", "cancelled") &&
            passportFiles?.let { it.bin.isFile && it.json.isFile } == true),
        "json" to passportJson, "revision" to passportRevision)

    private val session: ArkSession = ArkSession(activity, object : ArkSession.Listener {
        override fun onTransfer(snapshot: TransferSnapshot) {
            transfer = snapshot
            if (snapshot.kind == "file") onPassportTransfer(snapshot)
            if (closed) {
                if (snapshot.phase in setOf("done", "cancelled", "error", "unconfirmed")) {
                    deleteCurrent(); deleteFirmware()
                    if (passportJob?.awaitingFile != true) deletePassportFiles()
                }
                return
            }
            emit()
        }
        override fun onDevices(found: List<BluetoothDevice>) {
            if (closed) return
            devices = found.map { device ->
                mapOf("address" to device.address,
                    "name" to (session.scannedName(device.address) ?: "Ark"))
            }
            emit()
        }
        override fun onConnection(ready: Boolean, state: String) {
            if (closed) return
            val becameReady = ready && !connected
            connected = ready
            message = state
            if (!ready) {
                passportJob?.takeIf { it.awaitingFile }?.interrupted = state
                clearRead("disconnected", state)
                clearDetails()
            }
            emit()
            if (becameReady && !session.transferActive) beginRefresh(null)
        }
        override fun onPacket(packet: ByteArray) {
            if (fileListing && packet.size >= 6 && (packet[1].toInt() and 255) == 0x06) {
                val length = packet[2].toInt() and 255
                val nameLength = packet[4].toInt() and 255
                if (packet.size == length + 4 && nameLength > 0 && nameLength + 2 == length) {
                    val id = packet[3].toInt() and 255
                    deviceFiles[id] = packet.copyOfRange(5, 5 + nameLength).toString(Charsets.UTF_8).trimEnd('\u0000')
                }
                return
            }
            val read = pending ?: return
            if (closed || !connected || packet.size < 4) return
            val size = packet[2].toInt() and 255
            if ((packet[1].toInt() and 255) != read.channel || packet.size != size + 4) return
            read.response = packet.copyOfRange(3, size + 3)
            completeRead(read)
        }
    })

    init {
        methods.setMethodCallHandler(this)
        events.setStreamHandler(this)
    }

    private fun snapshot(): Map<String, Any?> = mapOf(
        "appUpdate" to appUpdates.snapshot(),
        "files" to mapOf("loaded" to filesLoaded, "directory" to fileDirectory,
            "currentId" to currentFileId, "message" to filesMessage,
            "storage" to storage, "canDelete" to canDeleteFiles,
            "entries" to deviceFiles.map { mapOf("id" to it.key, "name" to it.value) }),
        "connected" to connected, "message" to message, "devices" to devices,
        "advertisedName" to if (connected) advertisedName else null,
        "name" to name, "battery" to battery, "width" to width, "height" to height,
        "autoSleep" to autoSleep, "timedWake" to timedWake, "wakeMinutes" to wakeMinutes,
        "syncedAt" to syncedAt, "settingsMessage" to settingsMessage, "settingsBusy" to refreshing,
        "importing" to importing,
        "firmwareDownload" to firmwareDownload, "passport" to passportState(),
        "hasFirmwareState" to true,
        "importedFirmware" to importedFirmware?.let { mapOf("name" to firmwareName,
            "size" to it.size, "version" to it.version, "project" to it.project,
            "elfSha256" to it.elfSha256, "fileSha256" to it.fileSha256) },
        "importedFile" to currentFile?.let { mapOf("name" to importedName,
            "size" to it.length(), "width" to FilmConverter.WIDTH, "height" to FilmConverter.HEIGHT) },
        "transfer" to mapOf("phase" to transfer.phase, "message" to transfer.message,
            "received" to transfer.received, "total" to transfer.total,
            "canCancel" to transfer.canCancel, "canRetry" to transfer.canRetry,
            "kind" to transfer.kind, "targetVersion" to transfer.targetVersion,
            "targetBuild" to transfer.targetBuild, "canConfirm" to transfer.canConfirm,
            "success" to transfer.success, "cleanupCompleted" to transfer.cleanupCompleted))

    private fun emit() { if (!closed) sink?.success(snapshot()) }

    override fun onListen(arguments: Any?, eventSink: EventChannel.EventSink) {
        sink = eventSink
        emit()
    }

    override fun onCancel(arguments: Any?) { sink = null }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        if (closed) { result.error("closed", "蓝牙会话已关闭", null); return }
        try {
            when (call.method) {
                "loadInterfaceTheme" -> { result.success(activity.getSharedPreferences("ark-interface", Activity.MODE_PRIVATE).getString("theme", null)); return }
                "saveInterfaceTheme" -> {
                    val theme = call.argument<String>("theme")
                    require(theme in setOf("1", "0")) { "无效主题" }
                    check(activity.getSharedPreferences("ark-interface", Activity.MODE_PRIVATE).edit().putString("theme", theme).commit()) { "主题保存失败" }
                    result.success(null); return
                }
                "openExternalLink" -> {
                    val uri = Uri.parse(requireNotNull(call.argument<String>("url")) { "缺少链接地址" })
                    require(uri.scheme.equals("https", true) && !uri.host.isNullOrBlank()) { "仅支持 HTTPS 链接" }
                    try {
                        activity.startActivity(Intent(Intent.ACTION_VIEW, uri).addCategory(Intent.CATEGORY_BROWSABLE))
                        result.success(null)
                    } catch (error: android.content.ActivityNotFoundException) {
                        result.error("no_browser", "没有可用于打开此链接的浏览器", null)
                    }
                    return
                }
                "getAppInfo", "getAppUpdateInfo" -> { result.success(appUpdates.appInfo()); return }
                "getAppUpdateState" -> { result.success(appUpdates.snapshot()); return }
                "downloadAppUpdate" -> { appUpdates.download(call, result); return }
                "cancelAppUpdate" -> { appUpdates.cancel(); result.success(appUpdates.snapshot()); return }
                "installAppUpdate" -> { appUpdates.install(result); return }
            }
            if (call.method in setOf("scan", "connect", "refresh") && session.transferActive) {
                result.error("busy", "直传期间请等待传输结束", null)
                return
            }
            if (refreshing && call.method !in setOf("snapshot", "refresh", "saveGeneratedFilm", "disconnect", "cancelTransfer", "cancelFirmwareDownload", "cancelPassport", "loadPassportDraft", "savePassportDraft")) {
                result.error("busy", "设备读写正在进行，请稍后重试", null)
                return
            }
            when (call.method) {
                "saveGeneratedFilm" -> {
                    check(saveFilmResult == null) { "正在保存文件" }
                    val bytes = requireNotNull(call.argument<ByteArray>("bytes")) { "缺少 film 数据" }
                    require(bytes.size in 32..FilmConverter.MAX_FILM_BYTES) { "film 文件大小无效" }
                    val name = (call.argument<String>("name") ?: "frame.film").substringAfterLast('/').substringAfterLast('\\')
                    saveFilmResult = result; saveFilmBytes = bytes
                    try {
                        activity.startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE); type = "application/octet-stream"
                            putExtra(Intent.EXTRA_TITLE, if (name.endsWith(".film")) name else "$name.film")
                        }, SAVE_FILM)
                    } catch (error: Exception) { saveFilmResult = null; saveFilmBytes = null; throw error }
                }
                "loadPassportDraft" -> result.success(drafts.getString("json", null))
                "savePassportDraft" -> {
                    val json = requireNotNull(call.argument<String>("json"))
                    require(json.toByteArray(Charsets.UTF_8).size <= 8000000) { "本地草稿超过8MB" }
                    val draft = org.json.JSONObject(json)
                    if (draft.optInt("draftVersion") == 2) {
                        validatePassport(draft.getJSONObject("profile").toString())
                        val editor = draft.getJSONObject("avatarEditor")
                        val source = editor.getString("source")
                        require(source.startsWith("data:image/png;base64,") && source.length <= 6000000) { "头像源图无效" }
                        editor.getJSONObject("options")
                    } else validatePassport(json)
                    check(drafts.edit().putString("json", json).commit()) { "草稿保存失败" }
                    result.success(null)
                }
                "pickPassportAvatar" -> {
                    checkReplace(); check(avatarResult == null && frameResult == null) { "正在选择图片" }
                    avatarResult = result
                    try {
                        activity.startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE); type = "image/*"
                        }, PICK_AVATAR)
                    } catch (error: Exception) { avatarResult = null; throw error }
                }
                "pickFramePhotos" -> {
                    checkReplace(); check(avatarResult == null && frameResult == null) { "正在选择图片" }
                    frameResult = result
                    try {
                        activity.startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = "image/*"
                            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, call.argument<Boolean>("multiple") == true)
                        }, PICK_FRAME_PHOTOS)
                    } catch (error: Exception) { frameResult = null; throw error }
                }
                "captureFramePhoto" -> {
                    checkReplace(); check(avatarResult == null && frameResult == null) { "正在选择图片" }
                    frameResult = result
                    try {
                        frameCameraFile.parentFile?.mkdirs()
                        frameCameraFile.delete()
                        val uri = Uri.parse("content://${activity.packageName}.camera/photo.jpg")
                        frameCameraUri = uri
                        activity.startActivityForResult(Intent(MediaStore.ACTION_IMAGE_CAPTURE).apply {
                            putExtra(MediaStore.EXTRA_OUTPUT, uri)
                            clipData = ClipData.newRawUri("photo", uri)
                            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                        }, CAPTURE_FRAME_PHOTO)
                    } catch (error: Exception) {
                        frameResult = null
                        cleanupFrameCamera()
                        throw error
                    }
                }
                "refreshDeviceStorage", "listDeviceFiles", "displayDeviceFile", "deleteDeviceFile", "setDeviceFileDirectory", "playAnimation" -> filesCommand(call, result)
                "readPassport" -> beginPassport(result)
                "savePassport" -> savePassport(call, result)
                "retryPassport" -> retryPassport(result)
                "cancelPassport" -> {
                    passportJob?.let { job ->
                        job.cancelRequested = true
                        passportMessage = "正在取消通行证操作并清理传输"; emit()
                        if (job.awaitingFile) session.cancelTransfer()
                        else {
                            val hadPendingRead = pending != null
                            pending?.timeout?.let(main::removeCallbacks); pending = null
                            finishPassport("cancelled", "操作已取消")
                            if (hadPendingRead) {
                                // A late 0x53/0x59 reply must not satisfy a later read.
                                session.disconnect(); connected = false; clearDetails()
                            }
                        }
                    }
                    emit(); result.success(snapshot())
                }
                "remoteKey", "openDevicePage" -> remoteCommand(call, result)
                "setAutoSleep", "setTimedWake", "setWakeMinutes", "renameDevice", "syncTime" -> settingsCommand(call, result)
                "downloadFirmware" -> downloadFirmware(call, result)
                "cancelFirmwareDownload" -> {
                    cancelFirmwareDownload()
                    result.success(snapshot())
                }
                "importGeneratedFilm" -> importGeneratedFilm(call, result)
                "pickFilm" -> pickFilm(result)
                "pickFirmware", "importFirmware" -> pickFilm(result, firmware = true)
                "clearFirmware" -> {
                    checkReplace()
                    deleteFirmware()
                    transfer = TransferSnapshot("idle", "已清除固件", cleanupCompleted = true, kind = "firmware")
                    emit(); result.success(snapshot())
                }
                "startFirmwareTransfer" -> {
                    check(!importing && !refreshing && !session.transferActive) { "请等待当前操作完成" }
                    val file = checkNotNull(firmwareFile) { "请先导入 app.bin 固件" }
                    val image = checkNotNull(importedFirmware)
                    withPermissions(result, direct = true) {
                        transferPermission = false
                        session.startFirmwareTransfer(file, image)
                        result.success(snapshot())
                    }
                }
                "confirmFirmwareTransfer" -> withPermissions(result) {
                    session.confirmFirmwareTransfer()
                    result.success(snapshot())
                }
                "clearFilm" -> {
                    checkReplace()
                    deleteCurrent()
                    transfer = TransferSnapshot("idle", "已清除 film 文件", cleanupCompleted = true)
                    emit()
                    result.success(snapshot())
                }
                "retryTransfer" -> {
                    check(!importing && !refreshing && !session.transferActive) { "请等待当前操作完成" }
                    withPermissions(result, direct = true) {
                        transferPermission = false
                        session.retryTransfer()
                        result.success(snapshot())
                    }
                }
                "startTransfer" -> {
                    check(!importing && !refreshing && !session.transferActive) { "请等待当前操作完成" }
                    val file = checkNotNull(currentFile) { "请先导入 film 文件" }
                    withPermissions(result, direct = true) {
                        transferPermission = false
                        session.startTransfer(file, checkNotNull(importedName))
                        result.success(snapshot())
                    }
                }
                "cancelTransfer" -> {
                    session.cancelTransfer()
                    result.success(snapshot())
                }
                "snapshot" -> { emit(); result.success(snapshot()) }
                "scan" -> withPermissions(result) {
                    session.scan()
                    result.success(snapshot())
                }
                "connect" -> {
                    val address = call.argument<String>("address")
                    require(!address.isNullOrBlank()) { "请选择 Ark 设备" }
                    val selectedName = devices.firstOrNull { it["address"] == address }?.get("name")
                    require(selectedName != null) { "请先扫描并选择 Ark" }
                    withPermissions(result) {
                        advertisedName = selectedName
                        clearRead("cancelled", "已切换设备")
                        clearDetails()
                        connected = false
                        session.connect(address)
                        result.success(snapshot())
                    }
                }
                "disconnect" -> {
                    clearRead("cancelled", "已断开连接")
                    session.disconnect()
                    connected = false
                    clearDetails()
                    message = "已断开连接"
                    emit()
                    result.success(snapshot())
                }
                "refresh" -> withPermissions(result) { beginRefresh(result) }
                else -> result.notImplemented()
            }
        } catch (error: Exception) {
            result.error("operation_failed", error.message ?: "操作失败", null)
        }
    }

    private fun withPermissions(result: MethodChannel.Result, direct: Boolean = false, action: () -> Unit) {
        val required = when {
            Build.VERSION.SDK_INT >= 31 -> arrayOf(Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT)
            else -> arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val requested = if (!direct) required else required + when {
            Build.VERSION.SDK_INT >= 33 -> arrayOf(Manifest.permission.NEARBY_WIFI_DEVICES)
            else -> arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val missing = requested.filter { activity.checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) { action(); return }
        if (permissionResult != null) {
            passportPermission = false
            result.error("busy", "请先完成蓝牙权限授权", null)
            return
        }
        transferPermission = direct
        permissionResult = result
        permissionAction = action
        try { activity.requestPermissions(missing.toTypedArray(), PERMISSION_REQUEST) }
        catch (error: Exception) {
            permissionResult = null
            permissionAction = null
            transferPermission = false
            throw error
        }
    }

    fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>,
                                   grantResults: IntArray): Boolean {
        if (requestCode != PERMISSION_REQUEST) return false
        val result = permissionResult
        val action = permissionAction
        permissionResult = null
        permissionAction = null
        if (closed || result == null) return true
        if (grantResults.isEmpty() || grantResults.any { it != PackageManager.PERMISSION_GRANTED }) {
            message = "需要附近设备 / Wi-Fi 或定位权限，请允许后重试"
            if (transferPermission && !passportPermission) transfer = TransferSnapshot("error", message,
                total = currentFile?.length() ?: 0, canRetry = currentFile != null,
                cleanupCompleted = transfer.cleanupCompleted)
            transferPermission = false
            passportPermission = false
            emit()
            result.error("permission_denied", message, null)
        } else {
            try { transferPermission = false; passportPermission = false; action?.invoke() }
            catch (error: Exception) { result.error("operation_failed", error.message, null) }
        }
        return true
    }

    private fun beginRefresh(result: MethodChannel.Result?) {
        if (session.transferActive || importing || transfer.canConfirm || (transfer.canRetry && !transfer.cleanupCompleted)) {
            result?.error("busy", "请等待导入、传输和清理完成后读取设备信息", null)
            return
        }
        if (!connected || !session.isReady) {
            result?.error("not_connected", "请先连接 Ark", null)
            return
        }
        if (passportJob != null) {
            result?.error("busy", "通行证操作正在进行", null); return
        }
        if (refreshing) {
            // A user refresh may join the automatic first read.
            if (result != null && refreshResult == null) refreshResult = result
            else result?.error("busy", "设备信息正在读取", null)
            return
        }
        refreshing = true
        refreshResult = result
        readIndex = 0
        readNext()
    }

    private fun readNext() {
        if (readIndex == channels.size) {
            refreshing = false
            message = "设备信息已更新"
            emit()
            val result = refreshResult
            refreshResult = null
            result?.success(snapshot())
            return
        }
        request(channels[readIndex])
    }

    private fun request(channel: Int, payload: ByteArray = byteArrayOf(), done: ((ByteArray) -> Unit)? = null) {
        val read = Read(channel, done)
        pending = read
        message = "正在读取设备信息"
        emit()
        read.timeout = Runnable {
            if (pending === read) failRead("timeout", "设备响应超时，请重新连接 Ark")
        }.also { main.postDelayed(it, 7_000) }
        val bytes = byteArrayOf(0x55, read.channel.toByte(), payload.size.toByte()) + payload
        session.writePacket(bytes + byteArrayOf((bytes.sumOf { it.toInt() and 255 } and 255).toByte())) { outcome ->
            if (closed || pending !== read) return@writePacket
            outcome.fold(onSuccess = {
                read.writeDone = true
                completeRead(read)
            }, onFailure = { failRead("write_failed", it.message ?: "蓝牙写入失败") })
        }
    }

    private fun completeRead(read: Read) {
        if (pending !== read || !read.writeDone) return
        val data = read.response ?: return
        if (read.done != null) {
            read.timeout?.let(main::removeCallbacks)
            pending = null
            try { read.done.invoke(data) }
            catch (error: Exception) { failRead("invalid_response", error.message ?: "设备返回格式无效") }
            return
        }
        val valid = when (read.channel) {
            0x42 -> data.size == 5 && u16(data, 1) > 0 && u16(data, 3) > 0
            0x23 -> data.size == 1 && (data[0].toInt() and 255) in 0..100
            0x54 -> data.size in 3..31 && data[0] == 0.toByte() && data.last() == 0.toByte()
            0x26, 0x28 -> data.size == 1 && data[0].toInt() in 0..1
            0x2A -> data.size == 2 && u16(data, 0) in 10..2880
            else -> false
        }
        if (!valid) { failRead("invalid_response", "设备返回的信息格式无效"); return }
        when (read.channel) {
            0x42 -> { width = u16(data, 1); height = u16(data, 3) }
            0x23 -> battery = data[0].toInt() and 255
            0x54 -> name = data.copyOfRange(1, data.lastIndex).toString(Charsets.UTF_8)
            0x26 -> autoSleep = data[0].toInt() == 1
            0x28 -> timedWake = data[0].toInt() == 1
            0x2A -> wakeMinutes = u16(data, 0)
        }
        read.timeout?.let(main::removeCallbacks)
        pending = null
        emit()
        readIndex++
        readNext()
    }

    private fun failRead(code: String, detail: String) {
        clearRead(code, detail)
        // Reconnect creates a new GATT generation, so a late reply cannot satisfy a retry.
        session.disconnect()
        connected = false
        clearDetails()
        message = detail
        emit()
    }

    private fun clearRead(code: String, detail: String) {
        fileListing = false
        passportJob?.let { job ->
            if (job.awaitingFile) job.interrupted = detail else finishPassport("error", detail)
        }
        pending?.timeout?.let(main::removeCallbacks)
        pending = null
        refreshing = passportJob != null
        val result = refreshResult
        refreshResult = null
        result?.error(code, detail, null)
    }

    private fun clearDetails() {
        fileListing = false; deviceFiles.clear(); filesLoaded = false; currentFileId = null; fileDirectory = "film"
        catalogSupported = null; storage = mapOf("phase" to "idle"); canDeleteFiles = false
        name = null; battery = null; width = null; height = null
        autoSleep = null; timedWake = null; wakeMinutes = null; syncedAt = null; settingsMessage = null
    }

    private fun finishSettings(detail: String, failure: Boolean = false) {
        refreshing = false
        settingsMessage = detail
        val result = refreshResult
        refreshResult = null
        emit()
        if (failure) result?.error("settings_failed", detail, null) else result?.success(snapshot())
    }

    private fun settingsCommand(call: MethodCall, result: MethodChannel.Result) {
        check(connected && session.isReady) { "请先连接 Ark" }
        check(!refreshing && !importing && !session.transferActive && !transferPermission &&
            !transfer.canConfirm && (!transfer.canRetry || transfer.cleanupCompleted)) { "请等待当前操作或清理完成" }
        val channel: Int
        val payload: ByteArray
        var getChannel: Int? = null
        when (call.method) {
            "setAutoSleep", "setTimedWake" -> {
                check(if (call.method == "setAutoSleep") autoSleep != null else timedWake != null) { "请先读取设备设置" }
                val enabled = requireNotNull(call.argument<Boolean>("enabled")) { "缺少开关值" }
                channel = if (call.method == "setAutoSleep") 0x25 else 0x27
                getChannel = channel + 1
                payload = byteArrayOf(if (enabled) 1 else 0)
            }
            "setWakeMinutes" -> {
                check(wakeMinutes != null) { "请先读取设备设置" }
                val value = requireNotNull(call.argument<Number>("minutes")).toInt()
                require(value in 10..2880) { "唤醒间隔必须为 10–2880 分钟" }
                channel = 0x29; getChannel = 0x2A
                payload = byteArrayOf((value shr 8).toByte(), value.toByte())
            }
            "renameDevice" -> {
                check(name != null) { "请先读取设备名称" }
                val suffix = requireNotNull(call.argument<String>("suffix"))
                val bytes = suffix.toByteArray(Charsets.UTF_8)
                require(bytes.size in 1..16 && suffix.codePoints().allMatch {
                    it >= 0x20 && it !in 0x7F..0x9F && it !in 0xD800..0xDFFF
                }) { "名称后缀需为 1–16 UTF-8 字节，且不能包含控制字符" }
                channel = 0x55; payload = bytes + byteArrayOf(0)
            }
            else -> {
                val now = System.currentTimeMillis()
                val seconds = now / 1000
                val offset = java.util.TimeZone.getDefault().getOffset(now) / 60000
                require(seconds in 1577836800L..4102444800L && offset in -840..840) { "手机时间或时区超出设备范围" }
                channel = 0x4D
                payload = byteArrayOf((seconds shr 24).toByte(), (seconds shr 16).toByte(),
                    (seconds shr 8).toByte(), seconds.toByte(), (offset shr 8).toByte(), offset.toByte())
            }
        }
        refreshing = true
        refreshResult = result
        settingsMessage = "正在保存设备设置"
        if (getChannel != null) {
            val readChannel = getChannel
            // SET has no protocol ACK. Keep the same operation lock through GET verification.
            val marker = Read(channel)
            pending = marker
            marker.timeout = Runnable { if (pending === marker) failRead("timeout", "设备写入超时") }
                .also { main.postDelayed(it, 7_000) }
            val bytes = byteArrayOf(0x55, channel.toByte(), payload.size.toByte()) + payload
            session.writePacket(bytes + byteArrayOf((bytes.sumOf { it.toInt() and 255 } and 255).toByte())) { outcome ->
                if (closed || pending !== marker) return@writePacket
                marker.timeout?.let(main::removeCallbacks)
                pending = null
                outcome.fold(onSuccess = {
                    request(readChannel) { data ->
                        require(if (readChannel == 0x2A) data.size == 2 && u16(data, 0) in 10..2880
                            else data.size == 1 && data[0].toInt() in 0..1) { "设备返回设置格式无效" }
                        when (readChannel) {
                            0x26 -> autoSleep = data[0].toInt() == 1
                            0x28 -> timedWake = data[0].toInt() == 1
                            0x2A -> wakeMinutes = u16(data, 0)
                        }
                        val matches = data.contentEquals(payload)
                        finishSettings(if (matches) "已保存并回读确认" else "保存后回读与请求不一致，请刷新后重试", !matches)
                    }
                }, onFailure = { failRead("write_failed", it.message ?: "蓝牙写入失败") })
            }
        } else {
            request(channel, payload) { data ->
                if (channel == 0x55) {
                    if (data.size == 1 && data[0].toInt() in 1..2) {
                        finishSettings(if (data[0].toInt() == 1) "设备拒绝名称参数" else "设备名称存储失败", true)
                    } else {
                        require(data.size in 3..31 && data[0] == 0.toByte() && data.last() == 0.toByte()) { "名称回包无效" }
                        name = data.copyOfRange(1, data.lastIndex).toString(Charsets.UTF_8)
                        val expected = "FRAMEFILMARK-" + payload.copyOfRange(0, payload.lastIndex).toString(Charsets.UTF_8)
                        val matches = name == expected
                        finishSettings(if (matches) "已保存，设备重启后广播名生效" else "设备返回的名称与请求不一致", !matches)
                    }
                } else {
                    val matches = data.contentEquals(payload)
                    if (matches) syncedAt = System.currentTimeMillis()
                    finishSettings(if (matches) "时间同步完成" else "时间同步回显与请求不一致", !matches)
                }
            }
        }
        emit()
    }
    private fun validatePassport(raw: String) {
        require(raw.toByteArray(Charsets.UTF_8).size in 1..350000) { "资料大小无效" }
        val value = org.json.JSONObject(raw)
        require(value.get("version") is Number && (value.get("version") as Number).toDouble() == 1.0) { "资料版本不支持" }
        for ((field, limit) in mapOf("codename" to 32, "number" to 36, "affiliation" to 60, "signature" to 200)) {
            val text = value.get(field)
            require(text is String && text.length <= limit) { "$field 格式或长度无效" }
        }
        require(value.get("codenameUnset") is Boolean && value.get("numberUnset") is Boolean) { "未设置标记无效" }
        require(value.has("avatar") && (value.isNull("avatar") || value.get("avatar") is String)) { "头像字段无效" }
        require(!value.has("avatarMode") || value.get("avatarMode") in setOf("mono", "grayscale", "color")) { "头像显示模式无效" }
    }

    private fun checkPassportAvailable() {
        check(connected && session.isReady) { "请先连接 Ark" }
        checkReplace()
        check(!transferPermission && !transfer.canConfirm && (!transfer.canRetry || transfer.cleanupCompleted)) { "请等待传输清理完成" }
    }

    private fun beginPassport(result: MethodChannel.Result) {
        checkPassportAvailable()
        val job = PassportJob(result)
        passportJob = job; refreshing = true
        passportPhase = "reading"; passportCompleted = 0; passportTotal = 0
        passportMessage = "正在读取设备通行证资料"; emit()
        readPassportChunk(job)
    }

    private fun finishPassport(phase: String, detail: String) {
        val job = passportJob ?: return
        passportJob = null; refreshing = false; passportPhase = phase
        passportMessage = if (phase != "done" && job.expected != null) {
            if (!job.filesStarted) {
                "$detail；资料文件未发送，原资料保留。"
            } else {
                val progress = when {
                    job.jsonDone -> "两个文件已确认保存，但资料回读未确认，位图未校验。"
                    job.binDone -> "profile.bin 已确认保存，profile.json 未确认完成。"
                    else -> "profile.bin 未确认完成，profile.json 尚未发送。"
                }
                "$detail。$progress 可从头重试。"
            }
        } else if (phase != "done") "$detail；之前读取的资料保留。" else detail
        if (closed || (phase == "done" && job.expected != null)) deletePassportFiles()
        emit(); job.result.success(snapshot())
    }

    private fun deletePassportFiles() {
        passportFiles?.delete()
        passportFiles = null
    }

    private fun be32(value: Int) = byteArrayOf((value ushr 24).toByte(), (value ushr 16).toByte(),
        (value ushr 8).toByte(), value.toByte())
    private fun u32(bytes: ByteArray, offset: Int): Long = (0..3).fold(0L) { value, i ->
        (value shl 8) or (bytes[offset + i].toLong() and 255) }
    private fun packet(channel: Int, payload: ByteArray): ByteArray {
        val bytes = byteArrayOf(0x55, channel.toByte(), payload.size.toByte()) + payload
        return bytes + byteArrayOf((bytes.sumOf { it.toInt() and 255 } and 255).toByte())
    }

    private fun readPassportChunk(job: PassportJob) {
        if (passportJob !== job || closed) return
        val offset = job.bytes.size()
        request(0x53, be32(offset) + byteArrayOf(128.toByte())) { data ->
            if (passportJob !== job) return@request
            require(data.size in 9..137 && u32(data, 1) == offset.toLong()) { "资料响应位置或长度无效" }
            val status = data[0].toInt() and 255
            if (status == 3 && job.expected != null && job.retries++ < 12) {
                main.postDelayed({ if (passportJob === job) readPassportChunk(job) }, 500)
                return@request
            }
            if (status != 0) {
                finishPassport("error", when (status) { 1 -> "设备尚未配置资料"; 3 -> "设备忙，请稍后重试"; else -> "设备读取失败（状态 $status）" })
                return@request
            }
            job.retries = 0
            val total = u32(data, 5)
            require(total in 1..350000 && (job.total == -1 || job.total.toLong() == total)) { "资料长度变化或超出范围" }
            job.total = total.toInt()
            val count = data.size - 9
            require(offset + count <= job.total && (count > 0 || offset == job.total)) { "资料分块不完整" }
            job.bytes.write(data, 9, count)
            if (job.expected == null) { passportCompleted = job.bytes.size(); passportTotal = job.total }
            emit()
            if (job.bytes.size() == job.total) {
                val raw = Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(job.bytes.toByteArray())).toString()
                validatePassport(raw)
                if (job.expected != null && raw != job.expected) {
                    finishPassport("error", "资料回读与发送内容不一致")
                } else {
                    passportJson = raw; passportRevision++
                    finishPassport("done", if (job.expected == null) "设备资料读取完成" else
                        "资料回读一致；屏幕位图仍待实体确认。请回到通行证页或按确认刷新。")
                }
            } else main.postDelayed({ if (passportJob === job) readPassportChunk(job) }, 50)
        }
    }

    private fun savePassport(call: MethodCall, result: MethodChannel.Result) {
        checkPassportAvailable()
        val raw = requireNotNull(call.argument<String>("json")) { "缺少资料 JSON" }
        validatePassport(raw)
        val bin = requireNotNull(call.argument<ByteArray>("bin")) { "缺少位图" }
        val color = org.json.JSONObject(raw).optString("avatarMode", "mono") == "color"
        val version = if (color) 2.toByte() else 1.toByte()
        require(bin.size == (if (color) 133776 else 33456) && bin.copyOfRange(0, 6).contentEquals(byteArrayOf(70,70,85,73,version,version)) &&
            (bin[6].toInt() and 255) == 184 && (bin[7].toInt() and 255) == 1 &&
            (bin[8].toInt() and 255) == 96 && (bin[9].toInt() and 255) == 2 &&
            bin.copyOfRange(10,16).all { it == 0.toByte() }) { "位图必须为 440×608 FFUI v1 单色或 v2 六色文件，且与头像模式一致" }
        val colorCodes = setOf(0, 1, 2, 3, 5, 6)
        if (color) require(bin.asSequence().drop(16).all {
            val b = it.toInt() and 255
            (b ushr 4) in colorCodes && (b and 15) in colorCodes
        }) { "彩色资料包含无效颜色编码" }
        val json = raw.toByteArray(Charsets.UTF_8)
        passportPermission = true
        withPermissions(result, direct = true) {
            passportPermission = false
            val binFile = File.createTempFile("passport-bin-", ".bin", cache)
            var jsonFile: File? = null
            val files: PassportFiles
            try {
                binFile.writeBytes(bin)
                val createdJson = File.createTempFile("passport-json-", ".json", cache)
                jsonFile = createdJson
                createdJson.writeBytes(json)
                files = PassportFiles(binFile, createdJson, raw, color)
            } catch (error: Exception) {
                binFile.delete(); jsonFile?.delete()
                throw error
            }
            deletePassportFiles()
            passportFiles = files
            startPassportSave(result, files)
        }
    }

    private fun retryPassport(result: MethodChannel.Result) {
        val files = checkNotNull(passportFiles) { "没有可重试的通行证文件" }
        check(passportJob == null && !refreshing && !importing && !session.transferActive &&
            !transferPermission && !transfer.canConfirm && connected && session.isReady) { "请先连接 Ark 并等待当前操作结束" }
        check(files.bin.isFile && files.json.isFile) { "通行证缓存已失效，请重新保存" }
        passportPermission = true
        withPermissions(result, direct = true) {
            passportPermission = false
            startPassportSave(result, files)
        }
    }

    private fun startPassportSave(result: MethodChannel.Result, files: PassportFiles) {
        if (!connected || !session.isReady || session.transferActive) {
            passportPhase = "error"; passportMessage = "请先连接 Ark 并等待当前操作结束"
            emit(); result.error("busy", passportMessage, null)
            return
        }
        val job = PassportJob(result, files.expected)
        passportJob = job; refreshing = true; passportPhase = "sending"
        passportCompleted = 0; passportTotal = (files.bin.length() + files.json.length()).toInt()
        passportMessage = "正在查询通行证 Wi-Fi 传输能力"; emit()
        request(0x59) { capabilities ->
            if (passportJob !== job || job.cancelRequested) return@request
            if (capabilities.size != 5 || capabilities[0].toInt() != 1 ||
                (u32(capabilities, 1) and 2L) == 0L) {
                finishPassport("error", "设备不支持通行证 Wi-Fi 上传，请升级固件")
            } else if (files.color && (u32(capabilities, 1) and 1L) == 0L) {
                finishPassport("error", "设备不支持彩色通行证，请升级固件")
            } else startPassportFile(job, files, 0)
        }
        pending?.let { read ->
            read.timeout?.let(main::removeCallbacks)
            read.timeout = Runnable {
                if (pending === read) {
                    pending = null
                    finishPassport("error", "设备未响应通行证 Wi-Fi 能力查询，请升级固件")
                }
            }.also { main.postDelayed(it, 5000) }
        }
    }

    private fun startPassportFile(job: PassportJob, files: PassportFiles, index: Int) {
        if (passportJob !== job || job.cancelRequested || closed) return
        if (!connected || !session.isReady) {
            finishPassport("error", "蓝牙已断开")
            return
        }
        job.filesStarted = true; job.fileIndex = index; job.awaitingFile = true
        val filename = if (index == 0) "profile.bin" else "profile.json"
        passportMessage = "正在发送 $filename"; emit()
        session.startFileTransfer(if (index == 0) files.bin else files.json, "app/pass/$filename")
    }

    private fun onPassportTransfer(snapshot: TransferSnapshot) {
        val job = passportJob ?: return
        if (!job.awaitingFile) return
        val files = passportFiles ?: return
        val base = if (job.fileIndex == 0) 0 else files.bin.length().toInt()
        val size = if (job.fileIndex == 0) files.bin.length() else files.json.length()
        passportCompleted = base + snapshot.received.coerceIn(0, size).toInt()
        if (snapshot.phase !in setOf("done", "cancelled", "error", "unconfirmed")) {
            passportMessage = "正在发送 ${if (job.fileIndex == 0) "profile.bin" else "profile.json"}：${snapshot.message}"
            return
        }
        job.awaitingFile = false
        if (job.cancelRequested) {
            finishPassport("cancelled", if (snapshot.cleanupCompleted) "操作已取消" else "取消后设备清理未确认：${snapshot.message}")
        } else if (job.interrupted != null || !connected || !session.isReady) {
            finishPassport("error", "连接中断：${job.interrupted ?: snapshot.message}")
        } else if (snapshot.phase != "done" || !snapshot.success || !snapshot.cleanupCompleted) {
            finishPassport("error", "${if (job.fileIndex == 0) "profile.bin" else "profile.json"} 上传失败：${snapshot.message}")
        } else if (job.fileIndex == 0) {
            job.binDone = true; passportCompleted = files.bin.length().toInt()
            passportMessage = "profile.bin 已确认保存，正在准备 profile.json"
            main.post { if (passportJob === job && !job.cancelRequested) startPassportFile(job, files, 1) }
        } else {
            job.jsonDone = true; passportCompleted = passportTotal
            passportPhase = "verifying"
            passportMessage = "两个文件已确认保存，正在通过 BLE 回读资料"; emit()
            main.postDelayed({ if (passportJob === job && !job.cancelRequested) readPassportChunk(job) }, 750)
        }
    }

    // FILE_LIST has no terminal packet. The subsequent 0x08 reply is the
    // ordered end marker, including for an empty directory.
    private fun listActiveDeviceFiles(done: (() -> Unit)? = null) {
        deviceFiles.clear(); filesLoaded = false; currentFileId = null
        fileListing = true; filesMessage = "正在读取当前目录"
        writeFilesCommand(0x06, byteArrayOf()) {
            request(0x08) { data ->
                require(data.size == 1) { "显示文件响应无效" }
                currentFileId = (data[0].toInt() and 255).takeIf { it != 255 }
                fileListing = false; filesLoaded = true
                filesMessage = "已读取当前目录；文件 ID 会随删除或设备操作变化"
                if (done == null) finishSettings(filesMessage) else done()
            }
            pending?.let { read ->
                read.timeout?.let(main::removeCallbacks)
                read.timeout = Runnable { if (pending === read) failRead("timeout", "文件列表读取超时，请重新连接后刷新") }
                    .also { main.postDelayed(it, 30_000) }
            }
        }
    }

    private fun writeFilesCommand(channel: Int, payload: ByteArray, done: () -> Unit) {
        val marker = Read(channel); pending = marker
        marker.timeout = Runnable { if (pending === marker) failRead("timeout", "文件命令写入超时") }
            .also { main.postDelayed(it, 7000) }
        session.writePacket(packet(channel, payload)) { outcome ->
            if (pending !== marker || closed) return@writePacket
            marker.timeout?.let(main::removeCallbacks); pending = null
            outcome.fold(onSuccess = { done() }, onFailure = { failRead("write_failed", it.message ?: "文件命令写入失败") })
        }
    }

    private fun filesCommand(call: MethodCall, result: MethodChannel.Result) {
        checkPassportAvailable()
        val id = call.argument<Number>("id")?.toInt()
        if (call.method in setOf("displayDeviceFile", "deleteDeviceFile")) {
            val imageByName = call.method == "displayDeviceFile" && fileDirectory == "film"
            require(id != null && id >= 0 && (imageByName || id <= 255) &&
                filesLoaded && deviceFiles[id] == call.argument<String>("name")) {
                "文件列表已变化，请刷新后重试"
            }
        }
        if (call.method == "displayDeviceFile") require(fileDirectory in setOf("film", "animation")) {
            "请先选择图片目录或动画目录，再选择要显示的文件"
        }
        val appId = call.argument<Number>("appId")?.toInt()
        if (call.method == "setDeviceFileDirectory") require(appId == 0 || appId == 3) { "仅支持图片或动画目录" }
        val intervalMs = call.argument<Number>("intervalMs")?.toInt()
        val loopSeconds = call.argument<Number>("loopSeconds")?.toInt()
        val playMode = call.argument<Number>("playMode")?.toInt() ?: 0
        require(playMode in 0..1) { "动画播放模式无效" }
        if (call.method == "playAnimation") require(intervalMs != null && intervalMs in 100..2000 && loopSeconds != null && loopSeconds in 0..600) { "动画参数需为100–2000毫秒、循环间隔0–600秒" }
        refreshing = true; refreshResult = result
        when (call.method) {
            "playAnimation" -> writeFilesCommand(0x49, byteArrayOf(2, 2, (intervalMs!! ushr 8).toByte(), intervalMs.toByte(), 3, 2, (loopSeconds!! ushr 8).toByte(), loopSeconds.toByte())) {
                main.postDelayed({ if (connected && refreshing) writeFilesCommand(0x49, byteArrayOf(1, 1, playMode.toByte())) {
                    main.postDelayed({ if (connected && refreshing) request(0x4A) { data ->
                        val values = mutableMapOf<Int, Int>(); var offset = 0
                        while (offset + 2 <= data.size) {
                            val tag = data[offset++].toInt() and 255
                            val length = data[offset++].toInt() and 255
                            require(length in 1..4 && offset + length <= data.size) { "动画参数响应无效" }
                            var value = 0
                            repeat(length) { value = (value shl 8) or (data[offset++].toInt() and 255) }
                            values[tag] = value
                        }
                        require(offset == data.size && values[1] == playMode && values[2] == intervalMs && values[3] == loopSeconds) { "动画参数回读不一致，请重试" }
                        writeFilesCommand(0x4B, byteArrayOf(3)) {
                            main.postDelayed({ if (connected && refreshing) request(0x4C) { page ->
                                require(page.size == 1) { "当前页面响应无效" }
                                if (page[0].toInt() == 3) {
                                    fileDirectory = "animation"; filesLoaded = false; deviceFiles.clear()
                                    finishSettings("动画参数已回读确认，设备已进入动画 app")
                                } else finishSettings("参数已保存，但动画 app 未进入；请先上传至少一个多帧 film", true)
                            } }, 400)
                        }
                    } }, 150)
                } }, 150)
            }

            "listDeviceFiles" -> listDeviceFiles()
            "displayDeviceFile" -> displayDeviceFile(id!!, call.argument<String>("name")!!)
            "setDeviceFileDirectory" -> {
                fileDirectory = if (appId == 3) "animation" else "film"
                listDeviceFiles()
            }
            "refreshDeviceStorage" -> readStorage { finishSettings(storage["message"] as? String ?: "SD 卡统计已更新") }
            else -> deleteCatalogFile(call.argument<String>("name")!!)

        }
        emit()
    }

    private fun displayDeviceFile(@Suppress("UNUSED_PARAMETER") id: Int, expectedName: String) {
        if (fileDirectory == "film") { displayImageByName(expectedName); return }
        val targetApp = 3
        writeFilesCommand(0x4B, byteArrayOf(targetApp.toByte())) {
            main.postDelayed({ if (connected && refreshing) request(0x4C) { page ->
                require(page.size == 1 && (page[0].toInt() and 255) == targetApp) { "设备未进入目标 app，请等待设备就绪后重试" }
                listActiveDeviceFiles {
                    val activeId = deviceFiles.entries.firstOrNull { it.value == expectedName }?.key
                    if (activeId == null || activeId !in 0..255) { finishSettings("文件列表已变化，请刷新重选", true); return@listActiveDeviceFiles }
                    val channel = 0x49
                    val tag = 4
                    writeFilesCommand(channel, byteArrayOf(tag.toByte(), 4, 0, 0, 0, activeId.toByte())) {
                        request(channel + 1) { parameters ->
                            var offset = 0
                            var confirmed: Long? = null
                            while (offset + 2 <= parameters.size) {
                                val itemTag = parameters[offset++].toInt() and 255
                                val length = parameters[offset++].toInt() and 255
                                require(offset + length <= parameters.size) { "显示参数响应无效" }
                                if (itemTag == tag) { require(length == 4); confirmed = u32(parameters, offset) }
                                offset += length
                            }
                            require(offset == parameters.size && confirmed == activeId.toLong()) { "显示文件参数回读不一致，请刷新重试" }
                            listDeviceFiles()
                        }
                    }
                }
            } }, 400)
        }
    }

    private fun displayImageByName(expectedName: String) {
        val filename = expectedName.toByteArray(Charsets.UTF_8)
        if (filename.size !in 1..189 || expectedName.any { it == '\u0000' || it == '/' || it == '\\' } ||
            expectedName == "." || expectedName == "..") {
            finishSettings("图片文件名无效或超过 189 字节，请重命名后刷新重试", true)
            return
        }
        request(0x59) { capabilities ->
            require(capabilities.size == 5 && capabilities[0].toInt() == 1) { "设备能力响应无效，请升级 Ark 固件后重试" }
            if ((u32(capabilities, 1) and 8L) == 0L) {
                finishSettings("此固件不支持按文件名直接打开图片，请升级 Ark 固件后重试", true)
                return@request
            }
            request(0x4B, byteArrayOf(0) + filename + byteArrayOf(0)) { response ->
                require(response.size == 2 && response[0] == 0.toByte()) { "打开图片响应无效，请重新连接后重试" }
                when (response[1].toInt() and 255) {
                    // Success confirms that the target first frame was submitted, not displayed.
                    0 -> listDeviceFiles()
                    1 -> finishSettings("设备不支持此图片打开请求，请升级 Ark 固件并刷新后重试", true)
                    2 -> finishSettings("图片已不存在，请刷新目录后重新选择", true)
                    3 -> finishSettings("设备正在开机、休眠或处理目录，请等待设备就绪后重试", true)
                    else -> finishSettings("设备返回未知的图片打开状态，请刷新后重试", true)
                }
            }
            pending?.takeIf { it.channel == 0x4B }?.let { read ->
                read.timeout?.let(main::removeCallbacks)
                // A previous full-screen waveform may still be completing.
                read.timeout = Runnable {
                    if (pending === read) failRead("timeout", "打开图片响应超时，请重新连接并刷新后重试")
                }.also { main.postDelayed(it, 45_000) }
            }
        }
        pending?.takeIf { it.channel == 0x59 }?.let { read ->
            read.timeout?.let(main::removeCallbacks)
            read.timeout = Runnable {
                if (pending === read) failRead("unsupported_firmware", "无法读取图片打开能力，请升级 Ark 固件后重新连接重试")
            }.also { main.postDelayed(it, 7_000) }
        }
    }

    private fun deleteCatalogFile(expectedName: String) {
        val targetApp = if (fileDirectory == "film") 0 else 3
        request(0x4C) { page ->
            require(page.size == 1) { "当前页面响应无效" }
            if ((page[0].toInt() and 255) != targetApp) {
                canDeleteFiles = false
                finishSettings("为避免切换设备画面，请先在设备打开对应 app 后删除", true)
            } else listActiveDeviceFiles {
                val activeId = deviceFiles.entries.firstOrNull { it.value == expectedName }?.key
                if (activeId == null || activeId !in 0..255) { finishSettings("文件列表已变化，请刷新重选", true); return@listActiveDeviceFiles }
                request(0x4C) { current ->
                    if (current.size != 1 || (current[0].toInt() and 255) != targetApp) {
                        canDeleteFiles = false; finishSettings("设备页面已变化，请刷新后重试删除", true)
                    } else writeFilesCommand(0x05, byteArrayOf(activeId.toByte())) {
                        main.postDelayed({ if (connected && refreshing) listDeviceFiles() }, 400)
                    }
                }
            }
        }
    }

    private fun ensureCatalog(done: () -> Unit) {
        if (catalogSupported == true) { done(); return }
        if (catalogSupported == false) { unsupportedCatalog(); return }
        request(0x59) { data ->
            catalogSupported = data.size == 5 && data[0].toInt() == 1 && (u32(data, 1) and 4L) != 0L
            if (catalogSupported == true) done() else unsupportedCatalog()
        }
        pending?.let { read ->
            read.timeout?.let(main::removeCallbacks)
            read.timeout = Runnable { if (pending === read) {
                pending = null; catalogSupported = false; unsupportedCatalog()
            } }.also { main.postDelayed(it, 7000) }
        }
    }

    private fun unsupportedCatalog() {
        deviceFiles.clear(); filesLoaded = false; canDeleteFiles = false
        filesMessage = "此固件不支持只读目录和 SD 统计，请升级 Ark 固件"
        storage = mapOf("phase" to "unsupported", "message" to filesMessage)
        finishSettings(filesMessage)
    }

    private fun readStorage(done: () -> Unit) {
        storage = mapOf("phase" to "loading"); emit()
        ensureCatalog {
            request(0x5B, byteArrayOf(0)) { data ->
                require(data.size == 26) { "SD 统计响应无效" }
                val valid = data[1].toInt() and 7
                fun u64(offset: Int): Long = (u32(data, offset) shl 32) or u32(data, offset + 4)
                val ok = data[0].toInt() == 0
                storage = mapOf("phase" to if (!ok) "error" else if (valid == 7) "ready" else "partial",
                    "totalBytes" to if (ok && (valid and 1) != 0) u64(2) else null,
                    "freeBytes" to if (ok && (valid and 1) != 0) u64(10) else null,
                    "filmCount" to if (ok && (valid and 2) != 0) u32(data, 18) else null,
                    "animationCount" to if (ok && (valid and 4) != 0) u32(data, 22) else null,
                    "message" to if (!ok) "SD 卡未挂载，尚未读取" else if (valid != 7) "部分统计未读取，可刷新重试" else "来自设备 SD 卡的实际统计")
                emit(); done()
            }
        }
    }

    private fun listDeviceFiles() {
        deviceFiles.clear(); filesLoaded = false; currentFileId = null; canDeleteFiles = false
        filesMessage = "正在只读浏览目录"; emit()
        readStorage { readCatalogPage(0, null) }
    }

    private fun readCatalogPage(index: Int, expectedCount: Long?) {
        val dir = if (fileDirectory == "animation") 2 else 1
        request(0x5B, byteArrayOf(dir.toByte()) + be32(index)) { data ->
            require(data.size >= 5) { "目录响应无效" }
            val status = data[0].toInt() and 255
            val count = u32(data, 1)
            if (expectedCount != null && count != expectedCount) {
                deviceFiles.clear(); filesLoaded = false
                finishSettings("读取期间目录已变化，请刷新重试", true); return@request
            }
            if (status == 4) {
                require(index.toLong() == count) { "目录结束位置无效" }
                filesLoaded = true
                request(0x4C) { page ->
                    require(page.size == 1) { "当前页面响应无效" }
                    canDeleteFiles = (page[0].toInt() and 255) == if (dir == 1) 0 else 3
                    filesMessage = "只读目录已更新；浏览不会切换 app 或刷新设备画面"
                    finishSettings(filesMessage)
                }
            } else if (status == 0) {
                require(data.size > 6 && data.last() == 0.toByte() && index.toLong() < count) { "目录文件名无效" }
                deviceFiles[index] = data.copyOfRange(5, data.lastIndex).toString(Charsets.UTF_8)
                readCatalogPage(index + 1, count)
            } else {
                deviceFiles.clear(); filesLoaded = false
                finishSettings(if (status == 1) "SD 卡未挂载" else if (status == 5) "文件名过长，无法通过 BLE 浏览" else "读取目录失败，请刷新重试", true)
            }
        }
    }

    private fun remoteCommand(call: MethodCall, result: MethodChannel.Result) {
        checkPassportAvailable()
        val remote = call.method == "remoteKey"
        if (!remote) { filesLoaded = false; deviceFiles.clear(); currentFileId = null; canDeleteFiles = false }
        val value = requireNotNull(call.argument<Number>(if (remote) "key" else "appId")).toInt()
        require(value in if (remote) 0..4 else 0..6) { "不支持的按键或页面 ID" }
        refreshing = true; refreshResult = result
        if (remote) request(0x4E, byteArrayOf(value.toByte())) { data ->
            require(data.contentEquals(byteArrayOf(value.toByte()))) { "遥控按键回显不一致" }
            finishSettings("设备已收到按键；开机或休眠页面可能忽略操作")
        } else {
            val marker = Read(0x4B); pending = marker
            marker.timeout = Runnable { if (pending === marker) failRead("timeout", "页面切换写入超时") }
                .also { main.postDelayed(it, 7000) }
            session.writePacket(packet(0x4B, byteArrayOf(value.toByte()))) { outcome ->
                if (pending !== marker || closed) return@writePacket
                marker.timeout?.let(main::removeCallbacks)
                outcome.fold(onSuccess = {
                    main.postDelayed({
                        if (pending === marker && connected) {
                            pending = null
                            request(0x4C) { data ->
                                require(data.size == 1) { "当前页面响应无效" }
                                val matches = (data[0].toInt() and 255) == value
                                finishSettings(if (matches) "设备当前页面已确认" else "设备未切换到请求页面，可能未注册或正在忙", !matches)
                            }
                        }
                    }, 300)
                }, onFailure = { failRead("write_failed", it.message ?: "蓝牙写入失败") })
            }
        }
        emit()
    }

    private fun u16(data: ByteArray, offset: Int) =
        ((data[offset].toInt() and 255) shl 8) or (data[offset + 1].toInt() and 255)

    private fun checkReplace() {
        check(!importing && !refreshing && frameResult == null && !session.transferActive && !transferPermission &&
            transfer.cleanupCompleted && !transfer.canConfirm) { "请等待导入或直传清理结束" }
    }

    private fun deleteCurrent() {
        currentFile?.delete()
        currentFile = null
        importedName = null
    }

    private fun deleteFirmware() {
        firmwareFile?.delete()
        firmwareFile = null
        importedFirmware = null
        firmwareName = null
    }

    private fun downloadState(phase: String, received: Long, total: Long, detail: String,
                              canCancel: Boolean): Map<String, Any> = mapOf(
        "phase" to phase, "received" to received, "total" to total,
        "message" to detail, "canCancel" to canCancel)

    private fun replaceFirmware(file: File, image: ArkFirmware, displayName: String) {
        val previous = firmwareFile
        firmwareFile = file
        importedFirmware = image
        firmwareName = displayName
        if (previous != file) previous?.delete()
    }

    private fun cancelFirmwareDownload() {
        val job = download ?: return
        download = null
        job.downloader.cancel()
        job.file?.delete()
        importing = false
        firmwareDownload = downloadState("cancelled",
            firmwareDownload["received"] as Long, firmwareDownload["total"] as Long, "固件下载已取消", false)
        emit()
        job.result.success(snapshot())
    }

    private fun downloadFirmware(call: MethodCall, result: MethodChannel.Result) {
        checkReplace()
        val url = requireNotNull(call.argument<String>("url")) { "缺少固件下载地址" }
        val displayName = call.argument<String>("name")
        require(displayName == "frame_film_ark.bin") { "请选择原始 frame_film_ark.bin 资产" }
        val target = java.net.URI(url)
        require(target.scheme == "https" && target.host.equals("github.com", true) &&
            (target.rawPath?.startsWith("/kirmisaki/FrameFilm/releases/download/") == true ||
                target.rawPath?.startsWith("/Rafael-Ikaros/FrameFilm/releases/download/") == true ||
                target.rawPath?.startsWith("/Rafael-ban/FrameFilm/releases/download/") == true) &&
            target.rawPath?.endsWith("/frame_film_ark.bin") == true) { "固件必须来自 kirmisaki/FrameFilm（兼容旧 Rafael-Ikaros / Rafael-ban）Release" }
        val size = requireNotNull(call.argument<Number>("size")) { "缺少固件大小" }.toLong()
        require(size in 1..FirmwareDownloader.MAX_SIZE) { "固件大小必须在 1 B 至 4 MiB 之间" }
        val sha256 = call.argument<String>("sha256")
        require(sha256 == null || sha256.matches(Regex("[a-fA-F0-9]{64}"))) { "固件 SHA256 格式无效" }
        val job = Download(FirmwareDownloader(), result)
        download = job
        importing = true
        firmwareDownload = downloadState("downloading", 0, size, "正在从 GitHub 下载固件", true)
        emit()
        worker.execute {
            var temporary: File? = null
            try {
                job.downloader.checkCancelled()
                val file = File.createTempFile("import-", ".bin", cache)
                temporary = file
                job.file = file
                var lastProgress = 0L
                job.downloader.download(url, file, size, sha256) { received ->
                    val now = android.os.SystemClock.elapsedRealtime()
                    if (received == size || now - lastProgress >= 150) {
                        lastProgress = now
                        main.post {
                            if (!closed && download === job) {
                                firmwareDownload = downloadState("downloading", received, size, "正在从 GitHub 下载固件", true)
                                emit()
                            }
                        }
                    }
                }
                main.post {
                    if (!closed && download === job) {
                        firmwareDownload = downloadState("validating", size, size, "正在校验 Ark 固件", true)
                        emit()
                    }
                }
                val image = ArkFirmware.inspect(file)
                job.downloader.checkCancelled()
                main.post {
                    if (closed || download !== job) file.delete()
                    else {
                        replaceFirmware(file, image, "frame_film_ark.bin")
                        download = null
                        importing = false
                        firmwareDownload = downloadState("done", size, size, "固件下载并校验完成", false)
                        transfer = TransferSnapshot("idle", "固件已导入：${image.version}", cleanupCompleted = true,
                            kind = "firmware", targetVersion = image.version, targetBuild = image.elfSha256)
                        emit()
                        result.success(snapshot())
                    }
                }
            } catch (error: Exception) {
                temporary?.delete()
                main.post {
                    if (!closed && download === job) {
                        download = null
                        importing = false
                        firmwareDownload = downloadState("error", firmwareDownload["received"] as Long,
                            size, error.message ?: "固件下载失败", false)
                        emit()
                        result.success(snapshot())
                    }
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    /** Receives the exact Dart-generated preview file; conversion is shared with Web. */
    private fun importGeneratedFilm(call: MethodCall, result: MethodChannel.Result) {
        checkReplace()
        val bytes = requireNotNull(call.argument<ByteArray>("bytes")) { "缺少 film 数据" }
        require(bytes.size in 32..FilmConverter.MAX_FILM_BYTES) { "film 文件大小无效" }
        val requestedName = call.argument<String>("name") ?: "ark_frame.film"
        importing = true; importResult = result; emit()
        worker.execute {
            var temporary: File? = null
            try {
                val file = File.createTempFile("import-", ".film", cache)
                temporary = file
                file.writeBytes(bytes)
                if (Thread.currentThread().isInterrupted) error("导入已取消")
                FilmConverter.validate(file)
                val sendName = org.framefilm.ark.FilmTransferName.forFile(requestedName, file)
                main.post {
                    if (closed) file.delete()
                    else {
                        val previous = currentFile
                        currentFile = file; importedName = sendName
                        previous?.delete()
                        importing = false; importResult = null
                        transfer = TransferSnapshot("idle", "转换结果已导入，请在 Film 页发送",
                            cleanupCompleted = true, kind = "film")
                        emit(); result.success(snapshot())
                    }
                }
            } catch (error: Exception) {
                temporary?.delete()
                main.post {
                    if (!closed) {
                        importing = false; importResult = null
                        message = error.message ?: "转换结果导入失败"
                        emit(); result.error("import_failed", message, null)
                    }
                }
            }
        }
    }
    @Suppress("DEPRECATION")
    private fun pickFilm(result: MethodChannel.Result, firmware: Boolean = false) {
        checkReplace()
        pickingFirmware = firmware
        importing = true
        importResult = result
        emit()
        try {
            activity.startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
            }, PICK_FILM)
        } catch (error: Exception) {
            importing = false
            importResult = null
            emit()
            throw error
        }
    }

    private fun readFramePhoto(input: InputStream, limit: Int): ByteArray {
        val output = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) break
            require(output.size() + count <= limit) { "图片超过大小限制" }
            output.write(buffer, 0, count)
        }
        require(output.size() > 0) { "图片为空" }
        return output.toByteArray()
    }

    private fun cleanupFrameCamera() {
        frameCameraUri?.let {
            activity.revokeUriPermission(it,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        }
        frameCameraUri = null
        frameCameraFile.delete()
    }

    fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        if (requestCode == SAVE_FILM) {
            val result = saveFilmResult ?: return true
            val bytes = saveFilmBytes
            val uri = data?.data
            saveFilmBytes = null
            if (resultCode != Activity.RESULT_OK || uri == null || bytes == null) {
                saveFilmResult = null
                result.error("cancelled", "保存已取消", null)
                return true
            }
            worker.execute {
                try {
                    check(!closed) { "会话已关闭" }
                    activity.contentResolver.openOutputStream(uri, "wt").use { stream ->
                        checkNotNull(stream) { "无法打开目标文件" }
                        var offset = 0
                        while (offset < bytes.size) {
                            check(!closed && !Thread.currentThread().isInterrupted) { "保存已中断" }
                            val count = minOf(65536, bytes.size - offset)
                            stream.write(bytes, offset, count); offset += count
                        }
                        stream.flush()
                    }
                    main.post { if (!closed && saveFilmResult === result) { saveFilmResult = null; result.success(null) } }
                } catch (error: Exception) {
                    // ACTION_CREATE_DOCUMENT creates a new document: discard an incomplete export.
                    runCatching { DocumentsContract.deleteDocument(activity.contentResolver, uri) }
                    main.post { if (!closed && saveFilmResult === result) { saveFilmResult = null; result.error("save_failed", error.message, null) } }
                }
            }
            return true
        }
        if (requestCode == PICK_FRAME_PHOTOS) {
            val result = frameResult ?: return true
            if (resultCode != Activity.RESULT_OK) {
                frameResult = null
                result.success(emptyList<Map<String, Any>>())
                return true
            }
            val uris = data?.clipData?.let { clip ->
                (0 until clip.itemCount).map { clip.getItemAt(it).uri }
            } ?: listOfNotNull(data?.data)
            if (uris.isEmpty()) {
                frameResult = null
                result.success(emptyList<Map<String, Any>>())
                return true
            }
            worker.execute {
                val photos = ArrayList<Map<String, Any>>(uris.size)
                try {
                    require(uris.size <= MAX_FRAME_PHOTOS) { "最多选择 20 张图片" }
                    var total = 0
                    for ((index, uri) in uris.withIndex()) {
                        val displayName = activity.contentResolver.query(uri,
                            arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                            if (it.moveToFirst()) it.getString(0) else null
                        }?.takeIf { it.isNotBlank() } ?: "photo_${index + 1}.jpg"
                        val bytes = checkNotNull(activity.contentResolver.openInputStream(uri)).use {
                            readFramePhoto(it, minOf(MAX_FRAME_PHOTO_BYTES, MAX_FRAME_TOTAL_BYTES - total))
                        }
                        total += bytes.size
                        photos.add(mapOf("name" to displayName, "bytes" to bytes))
                    }
                    main.post {
                        if (!closed && frameResult === result) {
                            frameResult = null
                            result.success(photos)
                        }
                    }
                } catch (error: Exception) {
                    photos.clear()
                    main.post {
                        if (!closed && frameResult === result) {
                            frameResult = null
                            result.error("frame_pick_failed", error.message ?: "读取图片失败", null)
                        }
                    }
                }
            }
            return true
        }
        if (requestCode == CAPTURE_FRAME_PHOTO) {
            val result = frameResult ?: run { cleanupFrameCamera(); return true }
            if (resultCode != Activity.RESULT_OK) {
                frameResult = null
                cleanupFrameCamera()
                result.success(null)
                return true
            }
            worker.execute {
                try {
                    val bytes = frameCameraFile.inputStream().use {
                        readFramePhoto(it, MAX_FRAME_PHOTO_BYTES)
                    }
                    main.post {
                        cleanupFrameCamera()
                        if (!closed && frameResult === result) {
                            frameResult = null
                            result.success(mapOf("name" to "camera.jpg", "bytes" to bytes))
                        }
                    }
                } catch (error: Exception) {
                    main.post {
                        cleanupFrameCamera()
                        if (!closed && frameResult === result) {
                            frameResult = null
                            result.error("frame_capture_failed", error.message ?: "读取照片失败", null)
                        }
                    }
                }
            }
            return true
        }
        if (requestCode == PICK_AVATAR) {
            val result = avatarResult ?: return true
            avatarResult = null
            val uri = data?.data
            if (resultCode != Activity.RESULT_OK || uri == null) { result.success(null); return true }
            worker.execute {
                try {
                    val bytes = checkNotNull(activity.contentResolver.openInputStream(uri)).use { input ->
                        val output = java.io.ByteArrayOutputStream()
                        val buffer = ByteArray(8192)
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            require(output.size() + count <= 8 * 1024 * 1024) { "头像文件超过 8 MiB" }
                            output.write(buffer, 0, count)
                        }
                        output.toByteArray()
                    }
                    main.post { if (!closed) result.success(bytes) else result.error("closed", "会话已关闭", null) }
                } catch (error: Exception) { main.post { result.error("avatar_failed", error.message, null) } }
            }
            return true
        }
        if (requestCode != PICK_FILM) return false
        if (closed) return true
        val result = importResult
        val uri = data?.data
        if (resultCode != Activity.RESULT_OK || uri == null) {
            importing = false
            importResult = null
            emit()
            result?.success(snapshot())
            return true
        }
        val firmware = pickingFirmware
        worker.execute {
            var temporary: File? = null
            try {
                val displayName = activity.contentResolver.query(uri,
                    arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) it.getString(0) else null
                } ?: if (firmware) "app.bin" else "import.film"
                val file = File.createTempFile("import-", if (firmware) ".bin" else ".film", cache)
                temporary = file
                checkNotNull(activity.contentResolver.openInputStream(uri)).use { input ->
                    file.outputStream().use { output ->
                        val buffer = ByteArray(8192)
                        var total = 0L
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            if (Thread.currentThread().isInterrupted) error("导入已取消")
                            total += count
                            val limit = if (firmware) 4L * 1024 * 1024 else FilmConverter.MAX_FILM_BYTES.toLong()
                            require(total <= limit) { if (firmware) "固件超过 Ark 4 MiB Flash 容量" else "film 超过 128 MiB" }
                            output.write(buffer, 0, count)
                        }
                    }
                }
                val image = if (firmware) ArkFirmware.inspect(file) else null
                if (!firmware) FilmConverter.validate(file)
                val sendName = if (firmware) displayName else org.framefilm.ark.FilmTransferName.forFile(displayName, file)
                main.post {
                    if (closed) file.delete()
                    else {
                        if (firmware) {
                            replaceFirmware(file, checkNotNull(image), sendName)
                        } else {
                            deleteCurrent()
                            currentFile = file
                            importedName = sendName
                        }
                        importing = false
                        importResult = null
                        transfer = TransferSnapshot("idle", if (firmware) "固件已导入：${image?.version}" else "film 文件已导入，发送名 $sendName",
                            cleanupCompleted = true, kind = if (firmware) "firmware" else "film",
                            targetVersion = image?.version, targetBuild = image?.elfSha256)
                        emit()
                        result?.success(snapshot())
                    }
                }
            } catch (error: Exception) {
                temporary?.delete()
                main.post {
                    if (!closed) {
                        importing = false
                        importResult = null
                        message = error.message ?: if (firmware) "固件导入失败" else "film 导入失败"
                        emit()
                        result?.error("import_failed", message, null)
                    }
                }
            }
        }
        return true
    }

    fun close() {
        appUpdates.close()
        if (closed) return
        closed = true
        download?.let {
            it.downloader.cancel()
            it.file?.delete()
            it.result.error("closed", "会话已关闭", null)
        }
        download = null
        avatarResult?.error("closed", "会话已关闭", null); avatarResult = null
        saveFilmResult?.error("closed", "会话已关闭", null); saveFilmResult = null; saveFilmBytes = null
        frameResult?.error("closed", "会话已关闭", null); frameResult = null
        cleanupFrameCamera()
        clearRead("closed", "蓝牙会话已关闭")
        permissionResult?.error("closed", "蓝牙会话已关闭", null)
        permissionResult = null
        permissionAction = null
        transferPermission = false
        passportPermission = false
        methods.setMethodCallHandler(null)
        events.setStreamHandler(null)
        sink = null
        importResult?.error("closed", "会话已关闭", null)
        importResult = null
        worker.shutdownNow()
        val active = session.transferActive
        if (passportJob?.awaitingFile == true) {
            passportJob?.cancelRequested = true
            session.cancelTransfer()
        }
        session.close()
        if (!active) { deleteCurrent(); deleteFirmware(); deletePassportFiles() }
    }

    private companion object {
        const val PERMISSION_REQUEST = 4207
        const val SAVE_FILM = 4212
        const val PICK_FILM = 4208
        const val PICK_AVATAR = 4209
        const val PICK_FRAME_PHOTOS = 4210
        const val CAPTURE_FRAME_PHOTO = 4211
        const val MAX_FRAME_PHOTOS = 20
        const val MAX_FRAME_PHOTO_BYTES = 8 * 1024 * 1024
        const val MAX_FRAME_TOTAL_BYTES = 64 * 1024 * 1024
        var cacheInitialized = false
    }
}
