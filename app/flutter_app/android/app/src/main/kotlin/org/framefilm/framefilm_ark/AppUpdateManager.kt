package org.framefilm.framefilm_ark

import android.app.Activity
import android.content.ClipData
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.concurrent.CancellationException
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** App APK updates are independent of the film and firmware transfer sessions. */
internal class AppUpdateManager(private val activity: Activity, private val changed: () -> Unit) {
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()
    private val directory = File(activity.cacheDir, "app-update").apply { mkdirs() }
    private val apk = File(directory, "update.apk")
    @Volatile private var closed = false
    private class Job() {
        val cancelled = AtomicBoolean(false)
        @Volatile var connection: HttpURLConnection? = null
        @Volatile var file: File? = null
        fun cancel() {
            cancelled.set(true)
            connection?.let { active -> Thread({ active.disconnect() }, "app-update-cancel").apply { isDaemon = true; start() } }
        }
        fun check() {
            if (cancelled.get() || Thread.currentThread().isInterrupted) throw CancellationException("App 下载已取消")
        }
    }
    private var job: Job? = null
    private var ready = false
    private var state: Map<String, Any?> = state("idle", 0, 0, "从 GitHub Release 检查 App 更新")

    init {
        directory.listFiles()?.filter { it.name.endsWith(".part") }?.forEach { it.delete() }
        if (apk.exists()) {
            runCatching { inspect(apk) }.onSuccess { info ->
                ready = true
                state = state("ready", apk.length(), apk.length(), "已恢复下载的 App 安装包") + info
            }.onFailure { apk.delete() }
        }
    }

    fun snapshot(): Map<String, Any?> = state

    @Suppress("DEPRECATION")
    fun appInfo(): Map<String, Any?> {
        val info = activity.packageManager.getPackageInfo(activity.packageName, 0)
        return mapOf("packageName" to info.packageName, "versionName" to info.versionName,
            "versionCode" to info.longVersionCode, "canRequestPackageInstalls" to activity.packageManager.canRequestPackageInstalls())
    }

    private fun state(phase: String, received: Long, total: Long, message: String,
                      canCancel: Boolean = false): Map<String, Any?> = mapOf(
        "phase" to phase, "received" to received, "total" to total, "message" to message,
        "canCancel" to canCancel, "canInstall" to ready)

    @Suppress("DEPRECATION")
    private fun inspect(file: File): Map<String, Any?> {
        val pm = activity.packageManager
        val archive = requireNotNull(pm.getPackageArchiveInfo(file.absolutePath, PackageManager.GET_SIGNING_CERTIFICATES)) { "下载文件不是有效 APK" }
        val installed = pm.getPackageInfo(activity.packageName, PackageManager.GET_SIGNING_CERTIFICATES)
        require(archive.packageName == installed.packageName) { "APK 包名与当前 App 不一致（${installed.packageName} → ${archive.packageName}）；开发版 .dev 与正式版不能覆盖更新，请使用对应渠道安装包" }
        require((archive.applicationInfo?.minSdkVersion ?: 28) <= android.os.Build.VERSION.SDK_INT) { "新版本不支持当前 Android 系统" }
        require(archive.longVersionCode > installed.longVersionCode) { "当前 App 已是相同或更新版本" }
        val signing = requireNotNull(archive.signingInfo) { "APK 缺少签名信息" }
        val current = requireNotNull(installed.signingInfo) { "无法读取当前 App 签名" }
        val currentSigners = current.apkContentsSigners.toSet()
        val compatible = if (current.hasMultipleSigners() || signing.hasMultipleSigners()) {
            currentSigners == signing.apkContentsSigners.toSet()
        } else {
            currentSigners.all { it in signing.signingCertificateHistory.toSet() }
        }
        require(compatible) { "APK 签名与当前 App 不兼容，无法覆盖安装；若项目已更换发行签名，请先卸载旧版（会清除 App 本地资料）再安装，或使用与已安装版本相同签名的 Release" }
        return mapOf("packageName" to archive.packageName, "versionName" to archive.versionName, "versionCode" to archive.longVersionCode)
    }

    fun download(call: MethodCall, result: MethodChannel.Result) {
        check(job == null) { "App 更新下载正在进行" }
        val url = requireNotNull(call.argument<String>("url")) { "缺少 APK 下载地址" }
        val uri = java.net.URI(url)
        require(uri.scheme == "https" && uri.host.equals("github.com", true) &&
            (uri.rawPath?.startsWith("/kirmisaki/FrameFilm/releases/download/") == true ||
                uri.rawPath?.startsWith("/Rafael-Ikaros/FrameFilm/releases/download/") == true ||
                uri.rawPath?.startsWith("/Rafael-ban/FrameFilm/releases/download/") == true) &&
            uri.path?.endsWith(".apk", true) == true) { "App 必须来自 kirmisaki/FrameFilm（兼容旧 Rafael-Ikaros / Rafael-ban）GitHub Release APK" }
        val size = requireNotNull(call.argument<Number>("size")).toLong()
        require(size in 1..MAX_SIZE) { "APK 大小必须在 1 B 至 100 MiB 之间" }
        val sha256 = call.argument<String>("sha256")
        require(sha256 == null || sha256.matches(Regex("[a-fA-F0-9]{64}"))) { "APK SHA256 格式无效" }
        ready = false; apk.delete()
        val active = Job(); job = active
        state = state("downloading", 0, size, "正在从 GitHub 下载 App", true); changed()
        result.success(state)
        worker.execute {
            var temporary: File? = null
            try {
                active.check()
                val file = File.createTempFile("app-", ".part", directory); temporary = file; active.file = file
                copy(active, url, file, size, sha256)
                main.post { if (!closed && job === active) { state = state("validating", size, size, "正在检查 APK 包名、版本与签名", true); changed() } }
                val info = inspect(file)
                active.check()
                main.post {
                    if (closed || job !== active) file.delete()
                    else {
                        if (!file.renameTo(apk)) {
                            file.delete(); job = null
                            state = state("error", size, size, "无法保存下载的 APK，请重试")
                        } else {
                            job = null; ready = true
                            state = state("ready", size, size, "App 安装包已准备好，请确认安装") + info
                        }
                        changed()
                    }
                }
            } catch (error: Exception) {
                temporary?.delete()
                main.post { if (!closed && job === active) {
                    job = null; ready = false
                    state = state("error", (state["received"] as? Long) ?: 0, size, error.message ?: "App 下载失败")
                    changed()
                } }
            }
        }
    }

    private fun copy(active: Job, address: String, file: File, size: Long, expected: String?) {
        var target = URL(address)
        for (redirect in 0..8) {
            active.check()
            require(target.protocol == "https") { "APK 下载必须使用 HTTPS" }
            val http = target.openConnection() as HttpURLConnection
            active.connection = http
            http.connectTimeout = 15_000; http.readTimeout = 20_000; http.instanceFollowRedirects = false
            http.setRequestProperty("Accept", "application/octet-stream")
            http.setRequestProperty("User-Agent", "FrameFilm-Ark")
            try {
                val status = http.responseCode
                if (status in listOf(301, 302, 303, 307, 308)) {
                    require(redirect < 8) { "APK 下载重定向过多" }
                    target = URL(target, requireNotNull(http.getHeaderField("Location"))); continue
                }
                require(status == 200) { "GitHub App 下载失败：HTTP $status" }
                val length = http.getHeaderField("Content-Length")?.toLongOrNull()
                require(length == null || length == size) { "APK 响应长度与 Release 不一致" }
                val digest = MessageDigest.getInstance("SHA-256")
                var received = 0L; var lastProgress = 0L
                http.inputStream.use { input -> file.outputStream().use { output ->
                    val buffer = ByteArray(65536)
                    while (true) {
                        active.check(); val count = input.read(buffer); active.check()
                        if (count < 0) break
                        received += count
                        require(received <= size && received <= MAX_SIZE) { "APK 下载超过 Release 声明大小" }
                        output.write(buffer, 0, count); digest.update(buffer, 0, count)
                        val now = android.os.SystemClock.elapsedRealtime()
                        if (received == size || now - lastProgress >= 150) {
                            lastProgress = now; val progress = received
                            main.post { if (!closed && job === active) { state = state("downloading", progress, size, "正在从 GitHub 下载 App", true); changed() } }
                        }
                    }
                } }
                require(received == size) { "APK 下载不完整，请重试" }
                val actual = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
                require(expected == null || actual.equals(expected, true)) { "APK SHA256 与 Release 不一致" }
                active.check(); return
            } finally { http.disconnect(); active.connection = null }
        }
        error("APK 下载重定向过多")
    }

    fun cancel() {
        val active = job ?: return
        job = null; active.cancel(); active.file?.delete(); ready = false
        state = state("cancelled", (state["received"] as? Long) ?: 0, (state["total"] as? Long) ?: 0, "App 下载已取消，可重新下载")
        changed()
    }

    fun install(result: MethodChannel.Result) {
        check(ready && apk.isFile && job == null) { "请先下载兼容的新版本 APK" }
        inspect(apk)
        if (!activity.packageManager.canRequestPackageInstalls()) {
            activity.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${activity.packageName}")))
            state = state + mapOf("phase" to "permission_required", "message" to "请在系统设置允许此 App 安装应用，返回后点击安装")
        } else {
            val uri = Uri.parse("content://${activity.packageName}.updates/update.apk")
            activity.startActivity(Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, "application/vnd.android.package-archive")
                clipData = ClipData.newRawUri("App APK", uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            })
            state = state + mapOf("phase" to "installer_opened", "message" to "系统安装器已打开；请确认安装。返回 App 后可再次安装")
        }
        changed(); result.success(state)
    }

    fun close() {
        closed = true
        job?.let { active -> active.cancel(); active.file?.delete() }
        job = null; worker.shutdownNow()
        // A prepared APK remains readable while the system installer is open.
        // Next launch restores compatible ready APKs and deletes obsolete files.
    }
    companion object { const val MAX_SIZE = 100L * 1024 * 1024 }
}
