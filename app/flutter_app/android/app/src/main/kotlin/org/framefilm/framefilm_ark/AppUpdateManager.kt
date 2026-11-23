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
    private val prepared = activity.getSharedPreferences("app-update", Activity.MODE_PRIVATE)
    private data class Release(
        val packageName: String,
        val versionCode: Long,
        val versionName: String,
        val size: Long,
    )
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
    private var target: Release? = null
    private var state: Map<String, Any?> = state("idle", 0, 0, "从 GitHub Release 检查 App 更新")

    init {
        directory.listFiles()?.filter { it.name.endsWith(".part") }?.forEach { it.delete() }
        if (apk.exists()) {
            runCatching {
                val release = Release(
                    requireNotNull(prepared.getString("packageName", null)),
                    prepared.getLong("versionCode", 0),
                    requireNotNull(prepared.getString("versionName", null)),
                    prepared.getLong("size", 0),
                )
                val info = inspect(apk, release)
                target = release
                info
            }.onSuccess { info ->
                ready = true
                state = state("ready", apk.length(), apk.length(), "已恢复下载的 App 安装包") + info
            }.onFailure { clearPrepared() }
        }
    }

    fun snapshot(): Map<String, Any?> = state

    @Suppress("DEPRECATION")
    fun appInfo(): Map<String, Any?> {
        val info = activity.packageManager.getPackageInfo(activity.packageName, 0)
        return mapOf("packageName" to info.packageName, "versionName" to info.versionName,
            "versionCode" to info.longVersionCode,
            "channelVersions" to repositories.keys.mapNotNull { name ->
                installedPackage(name, 0)?.let { name to it.longVersionCode }
            }.toMap(),
            "canRequestPackageInstalls" to activity.packageManager.canRequestPackageInstalls())
    }

    private fun state(phase: String, received: Long, total: Long, message: String,
                      canCancel: Boolean = false): Map<String, Any?> = mapOf(
        "phase" to phase, "received" to received, "total" to total, "message" to message,
        "canCancel" to canCancel, "canInstall" to ready, "packageName" to target?.packageName)

    private fun clearPrepared() {
        apk.delete()
        prepared.edit().clear().apply()
    }

    @Suppress("DEPRECATION")
    private fun installedPackage(packageName: String, flags: Int) = try {
        activity.packageManager.getPackageInfo(packageName, flags)
    } catch (_: PackageManager.NameNotFoundException) {
        null
    }

    @Suppress("DEPRECATION")
    private fun inspect(file: File, release: Release): Map<String, Any?> {
        val pm = activity.packageManager
        require(file.length() == release.size) { "APK 大小与 Release 不一致" }
        val archive = requireNotNull(pm.getPackageArchiveInfo(file.absolutePath, PackageManager.GET_SIGNING_CERTIFICATES)) { "下载文件不是有效 APK" }
        require(archive.packageName == release.packageName) { "APK 包名与所选渠道不一致（${release.packageName} → ${archive.packageName}）" }
        require(archive.longVersionCode == release.versionCode && archive.versionName == release.versionName) { "APK 版本与 Release 不一致" }
        require((archive.applicationInfo?.minSdkVersion ?: 28) <= android.os.Build.VERSION.SDK_INT) { "新版本不支持当前 Android 系统" }
        val signing = requireNotNull(archive.signingInfo) { "APK 缺少签名信息" }
        require(signing.apkContentsSigners.isNotEmpty()) { "APK 缺少签名证书" }
        installedPackage(release.packageName, PackageManager.GET_SIGNING_CERTIFICATES)?.let { installed ->
            require(archive.longVersionCode > installed.longVersionCode) { "所选渠道已安装相同或更新版本" }
            val current = requireNotNull(installed.signingInfo) { "无法读取已安装应用的签名" }
            val currentSigners = current.apkContentsSigners.toSet()
            val compatible = if (current.hasMultipleSigners() || signing.hasMultipleSigners()) {
                currentSigners == signing.apkContentsSigners.toSet()
            } else {
                currentSigners.all { it in signing.signingCertificateHistory.toSet() }
            }
            require(compatible) { "APK 签名与所选渠道已安装应用的签名不兼容，请使用相同签名的 Release" }
        }
        return mapOf("packageName" to archive.packageName, "versionName" to archive.versionName, "versionCode" to archive.longVersionCode)
    }

    fun download(call: MethodCall, result: MethodChannel.Result) {
        check(job == null) { "App 更新下载正在进行" }
        val packageName = requireNotNull(call.argument<String>("packageName")) { "缺少渠道包名" }
        val repository = requireNotNull(repositories[packageName]) { "应用更新渠道无效" }
        val tag = requireNotNull(call.argument<String>("tag")) { "缺少 Release 标签" }
        val url = requireNotNull(call.argument<String>("url")) { "缺少 APK 下载地址" }
        val uri = java.net.URI(url)
        val path = uri.rawPath?.split("/") ?: emptyList()
        require(uri.scheme == "https" && uri.host.equals("github.com", true) &&
            (uri.port == -1 || uri.port == 443) && uri.rawUserInfo == null &&
            uri.rawQuery == null && uri.rawFragment == null && path.size == 7 &&
            "${path[1]}/${path[2]}".equals(repository, true) &&
            path[3] == "releases" && path[4] == "download" &&
            tag.isNotEmpty() && Uri.decode(path[5]) == tag &&
            path[6] == "framefilm-ark-flutter.apk") { "APK 必须来自所选渠道的 GitHub Release" }
        val size = requireNotNull(call.argument<Number>("size")).toLong()
        require(size in 1..MAX_SIZE) { "APK 大小必须在 1 B 至 100 MiB 之间" }
        val versionCode = requireNotNull(call.argument<Number>("versionCode")).toLong()
        val versionName = requireNotNull(call.argument<String>("versionName"))
        require(versionCode > 0 && versionName.isNotEmpty()) { "Release 版本无效" }
        val sha256 = requireNotNull(call.argument<String>("sha256"))
        require(sha256.matches(Regex("[a-fA-F0-9]{64}"))) { "APK SHA256 格式无效" }
        val release = Release(packageName, versionCode, versionName, size)
        target = release
        ready = false
        clearPrepared()
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
                val info = inspect(file, release)
                active.check()
                main.post {
                    if (closed || job !== active) file.delete()
                    else {
                        if (!file.renameTo(apk)) {
                            file.delete(); job = null
                            state = state("error", size, size, "无法保存下载的 APK，请重试")
                        } else {
                            prepared.edit()
                                .putString("packageName", release.packageName)
                                .putLong("versionCode", release.versionCode)
                                .putString("versionName", release.versionName)
                                .putLong("size", release.size)
                                .apply()
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

    private fun copy(active: Job, address: String, file: File, size: Long, expected: String) {
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
                require(actual.equals(expected, true)) { "APK SHA256 与 Release 不一致" }
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
        check(ready && job == null) { "请先下载兼容的新版本 APK" }
        val release = checkNotNull(target)
        try {
            inspect(apk, release)
        } catch (error: Exception) {
            ready = false
            clearPrepared()
            state = state("error", 0, 0, error.message ?: "安装包无法使用，请重新下载")
            changed()
            result.success(state)
            return
        }
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
    companion object {
        const val MAX_SIZE = 100L * 1024 * 1024
        private val repositories = mapOf(
            "org.framefilm.ark.flutter" to "kirmisaki/FrameFilm",
            "org.framefilm.ark.flutter.dev" to "Rafael-Ikaros/FrameFilm",
        )
    }
}
