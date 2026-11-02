package org.framefilm.ark

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothDevice
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.*
import android.media.ExifInterface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.provider.OpenableColumns
import android.view.WindowInsets
import android.widget.FrameLayout
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.max

/** Native Android host. No HTML or WebView is used by the application. */
class MainActivity : Activity(), ArkUi.Actions {
    internal lateinit var ui: ArkUi
        private set
    internal lateinit var session: ArkSession
        private set
    private val worker = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    private val imageGeneration = AtomicInteger()
    private lateinit var cache: File
    private var source: Bitmap? = null
    private var options = ImageOptions()
    private var currentFile: File? = null
    private var preview: Bitmap? = null
    private var converting = false
    private var contentAppId = 0
    private var uploadedName: String? = null
    private var displayWhenListed: String? = null
    private var destroyed = false
    private var wasConnected = false
    private class NameRequest(val channel: Int) {
        var writeDone = false
        var response: ByteArray? = null
        var timeout: Runnable? = null
    }
    private var nameRequest: NameRequest? = null
    private var permissionAction: (() -> Unit)? = null
    private var exportFile: File? = null
    private var convertPending: Runnable? = null
    private val files = sortedMapOf<Int, String>()
    internal var latestTransfer: TransferSnapshot? = null
        private set
    internal var discoveredDevices: List<String> = emptyList()
        private set

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        cache = File(cacheDir, "ark-transfer").apply { mkdirs() }
        cache.listFiles()?.forEach { it.delete() }
        session = ArkSession(this, object : ArkSession.Listener {
            override fun onDevices(devices: List<BluetoothDevice>) {
                discoveredDevices = devices.map { it.address }
                ui.showDevices(devices.map { it.address to (session.scannedName(it.address) ?: "FRAMEFILMARK") })
            }
            override fun onConnection(connected: Boolean, message: String) {
                ui.updateConnection(connected, message)
                if (!connected && nameRequest != null) finishNameRequest("蓝牙已断开，请重新连接后重试")
                if (connected && !wasConnected) main.postDelayed({ if (!session.transferActive) onReadSettings() }, 250)
                wasConnected = connected
            }
            override fun onPacket(packet: ByteArray) = receivePacket(packet)
            override fun onTransfer(snapshot: TransferSnapshot) {
                latestTransfer = snapshot
                ui.showTransfer(snapshot)
                if (snapshot.phase == "done" && snapshot.success) {
                    displayWhenListed = uploadedName
                    send(0x4B, byteArrayOf(contentAppId.toByte()))
                    main.postDelayed({ onRefreshFiles() }, 500)
                }
            }
        })
        ui = ArkUi(this, this)
        val root = FrameLayout(this).apply { addView(ui.view, FrameLayout.LayoutParams(-1, -1)) }
        root.setOnApplyWindowInsetsListener { view, insets ->
            if (Build.VERSION.SDK_INT >= 30) {
                val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime())
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            } else view.setPadding(0, insets.systemWindowInsetTop, 0, insets.systemWindowInsetBottom)
            insets
        }
        setContentView(root)
    }

    override fun onScan() = devicePermission { session.scan() }
    override fun onConnect(address: String) = devicePermission { session.connect(address) }
    override fun onDisconnect() = session.disconnect()
    override fun onPickImage() { if (canReplace()) pick("image/*", PICK_IMAGE) }
    override fun onPickFilm() { if (canReplace()) pick("*/*", PICK_FILM) }
    private fun pick(type: String, code: Int) {
        try { startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType(type), code) }
        catch (_: Exception) { ui.showMessage("系统文件选择器不可用") }
    }
    override fun onCaptureImage() {
        if (!canReplace()) return
        permission(arrayOf(Manifest.permission.CAMERA)) {
            val photo = File(cacheDir, "ark-camera/photo.jpg").apply { parentFile?.mkdirs() }
            val uri = Uri.parse("content://$packageName.camera/photo.jpg")
            try { startActivityForResult(Intent(MediaStore.ACTION_IMAGE_CAPTURE).putExtra(MediaStore.EXTRA_OUTPUT, uri)
                .addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_READ_URI_PERMISSION), CAPTURE) }
            catch (_: Exception) { ui.showMessage("系统相机不可用，请从相册选择图片") }
        }
    }

    override fun onImageOptions(options: ImageOptions) {
        if (!canReplace()) return
        this.options = options
        convertPending?.let(main::removeCallbacks)
        convertPending = Runnable { convertCurrent() }.also { main.postDelayed(it, 180) }
    }

    private fun canReplace(): Boolean {
        if (session.transferActive || exportFile != null) { ui.showMessage("请等待当前传输、清理或导出完成"); return false }
        if (latestTransfer?.cleanupCompleted == false && latestTransfer?.canRetry == true) {
            ui.showMessage("上一轮清理尚未确认，请先重试清理"); return false
        }
        return true
    }

    internal fun acceptImage(bitmap: Bitmap) {
        source = bitmap
        contentAppId = 0
        options = ImageOptions()
        ui.resetImageOptions()
        convertCurrent()
    }

    internal fun preparedFilmFile(): File? = if (converting) null else currentFile

    private fun convertCurrent() {
        val image = source ?: return
        val token = imageGeneration.incrementAndGet()
        val selectedOptions = options
        converting = true
        ui.showMessage("正在生成电子纸预览…")
        worker.execute {
            try {
                val output = FilmConverter.convert(image, selectedOptions)
                val file = File(cache, "image-$token.film")
                file.writeBytes(output.bytes)
                runOnUiThread {
                    if (destroyed || token != imageGeneration.get()) { output.preview.recycle(); file.delete(); return@runOnUiThread }
                    currentFile?.delete(); currentFile = file
                    preview = output.preview; converting = false
                    ui.showPreview(output.preview, "720×480 · ${if (selectedOptions.monochrome) "黑白" else "标准六色"} · ${output.bytes.size} 字节")
                }
            } catch (error: Exception) {
                runOnUiThread { if (!destroyed && token == imageGeneration.get()) { converting = false; ui.showMessage(error.message ?: "转换失败") } }
            }
        }
    }

    override fun onQuote(text: String, author: String) {
        if (!canReplace()) return
        val bitmap = Bitmap.createBitmap(720, 480, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap).apply { drawColor(Color.WHITE) }
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.BLACK; textSize = 34f }
        var y = 110f
        var line = ""
        for (character in text.take(150)) {
            if (character == '\n' || paint.measureText(line + character) > 600) {
                canvas.drawText(line, 60f, y, paint); y += 48; line = ""
            }
            if (character != '\n') line += character
            if (y > 350) break
        }
        canvas.drawText(line, 60f, y, paint)
        paint.textSize = 24f; paint.textAlign = Paint.Align.RIGHT
        canvas.drawText(author.take(30), 660f, 426f, paint)
        acceptImage(bitmap)
    }

    override fun onUpload(fileName: String) {
        if (nameRequest != null) { ui.showMessage("请等待设备名称操作完成后再传输"); return }
        if (converting) { ui.showMessage("图片仍在转换，请稍候"); return }
        val file = currentFile
        if (file == null || !file.exists()) { ui.showMessage("请先选择图片或导入 film 文件"); return }
        val name = normalizedName(fileName) ?: return
        uploadedName = name
        devicePermission { session.startTransfer(file, name) }
    }
    override fun onExport(fileName: String) {
        if (!canReplace() || converting) return
        val file = currentFile ?: return ui.showMessage("请先选择图片或导入 film 文件")
        val name = normalizedName(fileName) ?: return
        exportFile = file
        try { startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
            .setType("application/octet-stream").putExtra(Intent.EXTRA_TITLE, name), EXPORT) }
        catch (_: Exception) { exportFile = null; ui.showMessage("无法打开保存位置") }
    }
    private fun normalizedName(input: String): String? {
        var name = input.trim().ifEmpty { "ark_${System.currentTimeMillis()}" }
        if (!name.endsWith(".film", true)) name += ".film"
        if (!name.matches(Regex("[A-Za-z0-9_.-]{1,100}\\.film"))) {
            ui.showMessage("文件名请使用英文、数字、点、横线或下划线"); return null
        }
        return name
    }
    override fun onCancel() = session.cancelTransfer()
    override fun onRetry() {
        if (nameRequest != null) { ui.showMessage("请等待设备名称操作完成后再重试传输"); return }
        devicePermission { session.retryTransfer() }
    }
    override fun onClear() {
        if (!canReplace()) return
        imageGeneration.incrementAndGet(); converting = false
        source = null; preview = null; currentFile?.delete(); currentFile = null
        latestTransfer = null
        ui.showPreview(null, "请选择一张照片")
        ui.showTransfer(TransferSnapshot("idle", "已清除本次图片", cleanupCompleted = true))
    }
    override fun onRefreshFiles() {
        if (!session.isReady || session.transferActive) return
        files.clear(); ui.showFiles(emptyList()); send(0x06)
    }
    override fun onDisplayFile(id: Int) { send(0x07, byteArrayOf(id.toByte())) }
    override fun onDeleteFile(id: Int) {
        send(0x05, byteArrayOf(id.toByte()))
        main.postDelayed({ onRefreshFiles() }, 700)
    }
    override fun onSetting(channel: Int, data: ByteArray) { send(channel, data) }
    override fun onReadDeviceName() = sendNameCommand(0x54)
    override fun onSaveDeviceName(suffix: String) {
        if (suffix.isBlank() || !Charsets.UTF_8.newEncoder().canEncode(suffix) ||
            suffix.toByteArray(Charsets.UTF_8).size !in 1..16 ||
            suffix.any { Character.isISOControl(it) }) {
            ui.showDeviceNameStatus("后缀须为 1–16 个 UTF-8 字节，且不能包含控制字符")
            return
        }
        sendNameCommand(0x55, suffix.toByteArray(Charsets.UTF_8) + byteArrayOf(0))
    }

    private fun sendNameCommand(channel: Int, payload: ByteArray = byteArrayOf()) {
        if (!session.isReady || session.transferActive) {
            ui.showDeviceNameStatus("请先连接 Ark，并等待传输结束")
            return
        }
        if (nameRequest != null) {
            ui.showDeviceNameStatus("上一条设备名命令仍在等待响应")
            return
        }
        val request = NameRequest(channel)
        nameRequest = request
        ui.showDeviceNameStatus(if (channel == 0x54) "正在读取设备名…" else "正在保存设备名…")
        val frame = byteArrayOf(0x55, channel.toByte(), payload.size.toByte()) + payload
        val packet = frame + byteArrayOf((frame.sumOf { it.toInt() and 255 } and 255).toByte())
        session.writePacket(packet) { result ->
            if (nameRequest !== request) return@writePacket
            val error = result.exceptionOrNull()
            if (error != null) {
                finishNameRequest(error.message ?: "设备名命令发送失败")
            } else {
                request.writeDone = true
                request.response?.let { completeNameRequest(request, it) } ?: run {
                    request.timeout = Runnable {
                        if (nameRequest === request) finishNameRequest("设备未响应；可能需要更新固件，请重试")
                    }.also { main.postDelayed(it, 7_000) }
                }
            }
        }
    }

    private fun finishNameRequest(message: String) {
        nameRequest?.timeout?.let(main::removeCallbacks)
        nameRequest = null
        ui.showDeviceNameStatus(message)
    }

    private fun completeNameRequest(request: NameRequest, data: ByteArray) {
        if (nameRequest !== request || !request.writeDone) return
        if (data.size == 1 && data[0].toInt() != 0) {
            finishNameRequest(when (data[0].toInt() and 255) {
                1 -> "设备名参数无效，请检查后缀"
                2 -> "设备保存失败，请重试"
                else -> "设备返回未知状态"
            })
            return
        }
        if (data.size !in 3..31 || data[0].toInt() != 0 || data.last() != 0.toByte()) {
            finishNameRequest("设备名响应格式错误")
            return
        }
        val nameBytes = data.copyOfRange(1, data.lastIndex)
        val name = String(nameBytes, Charsets.UTF_8)
        if (!nameBytes.contentEquals(name.toByteArray(Charsets.UTF_8)) ||
            !name.startsWith("FRAMEFILMARK-") || nameBytes.size > 29) {
            finishNameRequest("设备名响应格式错误")
            return
        }
        request.timeout?.let(main::removeCallbacks)
        nameRequest = null
        ui.showDeviceName(name, saved = request.channel == 0x55)
    }
    override fun onReadSettings() {
        listOf(0x42, 0x23, 0x26, 0x28, 0x2A, 0x31, 0x33, 0x35).forEach { send(it) }
        onRefreshFiles()
    }
    private fun send(channel: Int, data: ByteArray = byteArrayOf()) {
        val packet = byteArrayOf(0x55, channel.toByte(), data.size.toByte()) + data
        session.writePacket(packet + byteArrayOf((packet.sumOf { it.toInt() and 255 } and 255).toByte())) { result ->
            result.exceptionOrNull()?.let { if (!destroyed) ui.showMessage(it.message ?: "命令发送失败") }
        }
    }
    private fun receivePacket(packet: ByteArray) {
        if (packet.size < 4) return
        val channel = packet[1].toInt() and 255
        val size = packet[2].toInt() and 255
        if (packet.size < size + 4) return
        val data = packet.copyOfRange(3, 3 + size)
        if (channel == 0x54 || channel == 0x55) {
            val request = nameRequest
            if (request?.channel == channel) {
                request.response = data
                completeNameRequest(request, data)
            }
            return
        }
        when {
            channel == 0x23 && size == 1 -> ui.showBattery(data[0].toInt() and 255)
            channel == 0x06 && size >= 2 -> {
                val nameLength = data[1].toInt() and 255
                if (nameLength in 1..size - 2) {
                    files[data[0].toInt() and 255] = data.copyOfRange(2, 2 + nameLength).toString(Charsets.US_ASCII).substringBefore('\u0000')
                    ui.showFiles(files.map { it.key to it.value })
                    if (files[data[0].toInt() and 255] == displayWhenListed) {
                        displayWhenListed = null
                        send(0x07, byteArrayOf(data[0]))
                    }
                }
            }
            else -> ui.showSetting(channel, data)
        }
    }

    private fun devicePermission(action: () -> Unit) = permission(if (Build.VERSION.SDK_INT >= 33)
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.NEARBY_WIFI_DEVICES)
        else if (Build.VERSION.SDK_INT >= 31) arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.ACCESS_FINE_LOCATION)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION), action)
    private fun permission(required: Array<String>, action: () -> Unit) {
        val missing = required.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) action()
        else if (permissionAction == null) { permissionAction = action; requestPermissions(missing.toTypedArray(), PERMISSIONS) }
        else ui.showMessage("请先处理当前权限请求")
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, results)
        if (requestCode != PERMISSIONS) return
        val action = permissionAction; permissionAction = null
        if (results.isNotEmpty() && results.all { it == PackageManager.PERMISSION_GRANTED }) action?.invoke()
        else ui.showMessage("权限未授予，可在系统设置中允许后重试")
    }

    @Deprecated("Platform activity results for the native picker")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == EXPORT) {
            val file = exportFile ?: return
            val uri = data?.data
            if (resultCode != RESULT_OK || uri == null) { exportFile = null; ui.showMessage("已取消保存"); return }
            worker.execute {
                try {
                    checkNotNull(contentResolver.openOutputStream(uri, "wt")).use { output -> file.inputStream().use { it.copyTo(output) } }
                    runOnUiThread { ui.showMessage("film 文件已保存") }
                } catch (error: Exception) { runOnUiThread { ui.showMessage(error.message ?: "保存失败") } }
                finally { runOnUiThread { exportFile = null } }
            }
            return
        }
        if (resultCode != RESULT_OK) return
        val uri = if (requestCode == CAPTURE) Uri.parse("content://$packageName.camera/photo.jpg") else data?.data ?: return
        worker.execute {
            try {
                if (requestCode == PICK_FILM) {
                    val file = File(cache, "import-${System.currentTimeMillis()}.film")
                    try {
                        checkNotNull(contentResolver.openInputStream(uri)).use { input -> file.outputStream().use { out ->
                            val buffer = ByteArray(8192); var count: Int; var total = 0L
                            while (input.read(buffer).also { count = it } != -1) {
                                total += count; require(total <= FilmConverter.MAX_FILM_BYTES) { "film 超过 128 MiB" }; out.write(buffer, 0, count)
                            }
                        } }
                        val info = FilmConverter.validate(file)
                        val header = ByteArray(12)
                        file.inputStream().use { it.read(header) }
                        val appId = if (((header[10].toInt() and 255) or ((header[11].toInt() and 255) shl 8)) > 1) 3 else 0
                        runOnUiThread {
                            imageGeneration.incrementAndGet(); converting = false; source = null
                            currentFile?.delete(); currentFile = file; preview = null
                            contentAppId = appId
                            ui.showPreview(null, "已导入 film · $info")
                        }
                    } catch (error: Exception) { file.delete(); throw error }
                } else {
                    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    contentResolver.openInputStream(uri).use { BitmapFactory.decodeStream(it, null, bounds) }
                    require(bounds.outWidth > 0 && bounds.outHeight > 0) { "无法读取这张图片" }
                    val decode = BitmapFactory.Options().apply {
                        inSampleSize = 1
                        while (max(bounds.outWidth, bounds.outHeight) / inSampleSize > 2048) inSampleSize *= 2
                    }
                    var bitmap = contentResolver.openInputStream(uri).use { BitmapFactory.decodeStream(it, null, decode) } ?: error("图片解码失败")
                    val orientation = try { contentResolver.openInputStream(uri).use { ExifInterface(checkNotNull(it)).getAttributeInt(ExifInterface.TAG_ORIENTATION, 1) } } catch (_: Exception) { 1 }
                    val matrix = Matrix().apply { when (orientation) {
                        2 -> setScale(-1f, 1f); 3 -> setRotate(180f); 4 -> setScale(1f, -1f)
                        5 -> { setRotate(90f); postScale(-1f, 1f) }; 6 -> setRotate(90f)
                        7 -> { setRotate(270f); postScale(-1f, 1f) }; 8 -> setRotate(270f)
                    } }
                    if (!matrix.isIdentity) bitmap = Bitmap.createBitmap(bitmap, 0, 0, bitmap.width, bitmap.height, matrix, true)
                    runOnUiThread { if (!destroyed) acceptImage(bitmap) }
                }
            } catch (error: Exception) { runOnUiThread { if (!destroyed) ui.showMessage(error.message ?: "读取文件失败") } }
        }
    }

    @Deprecated("Native page navigation")
    override fun onBackPressed() {
        if (ui.handleBack()) return
        if (session.transferActive) AlertDialog.Builder(this).setMessage("取消当前传输并退出？")
            .setPositiveButton("取消并退出") { _, _ -> finish() }.setNegativeButton("继续传输", null).show()
        else finish()
    }
    override fun onDestroy() {
        destroyed = true; imageGeneration.incrementAndGet(); main.removeCallbacksAndMessages(null)
        session.close(); worker.shutdownNow()
        super.onDestroy()
    }
    companion object {
        private const val PICK_IMAGE = 101; private const val PICK_FILM = 102
        private const val CAPTURE = 103; private const val EXPORT = 104; private const val PERMISSIONS = 105
    }
}
