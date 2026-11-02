package org.framefilm.ark

import android.app.Activity
import android.app.Instrumentation
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.webkit.WebView
import android.widget.Button
import android.widget.TextView
import java.util.concurrent.atomic.AtomicReference

/** One real-device canary: native UI + encoding + cancel + retry on the same connection. */
class NativeAppProbe : Instrumentation() {
    override fun onCreate(arguments: Bundle?) { super.onCreate(arguments); start() }
    private fun report(value: String) = sendStatus(0, Bundle().apply { putString("progress", value) })
    private fun <T> mainValue(action: () -> T): T {
        val value = AtomicReference<Result<T>>()
        runOnMainSync { value.set(runCatching(action)) }
        return value.get().getOrThrow()
    }
    private fun await(message: String, timeout: Long = 20_000, predicate: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + timeout
        while (!predicate()) {
            check(SystemClock.elapsedRealtime() < deadline) { message }
            SystemClock.sleep(100)
        }
    }
    private fun all(view: View): List<View> = listOf(view) + if (view is ViewGroup) (0 until view.childCount).flatMap { all(view.getChildAt(it)) } else emptyList()
    override fun onStart() {
        var activity: MainActivity? = null
        var result = "FAIL"
        var detail = "未开始"
        try {
            report("启动原生 MainActivity")
            val monitor = addMonitor(MainActivity::class.java.name, null, false)
            targetContext.startActivity(Intent(targetContext, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            activity = waitForMonitorWithTimeout(monitor, 15_000) as? MainActivity ?: error("原生界面启动超时")
            removeMonitor(monitor)
            val app = activity
            check(mainValue { all(app.ui.view).none { it is WebView } }) { "界面中存在 WebView" }
            for (tab in listOf("连接", "Frame", "Film", "动画", "设置")) mainValue {
                val view = all(app.ui.view).filterIsInstance<TextView>().first { it.text.toString() == tab }
                check(view.performClick())
            }
            report("五个页面均为原生控件，导航通过")
            val colors = intArrayOf(Color.BLACK, Color.WHITE, Color.YELLOW, Color.RED, Color.BLUE, Color.rgb(41, 204, 20))
            val bitmap = Bitmap.createBitmap(720, 480, Bitmap.Config.ARGB_8888)
            val pixels = IntArray(720 * 480) { colors[(it % 720) / 120] }
            bitmap.setPixels(pixels, 0, 720, 0, 0, 720, 480)
            val output = FilmConverter.convert(bitmap, ImageOptions(dither = false))
            check(output.bytes.size == 172832 && output.bytes[8].toInt() == 6)
            for (color in colors.indices) {
                val x = color * 120 + 20
                val packed = 720 * 480 - 1 - (240 * 720 + x)
                val value = output.bytes[32 + packed / 2].toInt() and 255
                check((if (packed % 2 == 0) value ushr 4 else value and 15) == color) { "颜色/180度像素排列不符" }
            }
            report("原生六色编码：文件头、172832字节、颜色码和180度排列通过")
            mainValue { app.acceptImage(bitmap) }
            await("原生图片转换未完成") { mainValue { app.preparedFilmFile()?.length() == 172832L } }
            mainValue {
                all(app.ui.view).filterIsInstance<TextView>().first { it.text.toString() == "连接" }.performClick()
                app.onScan()
            }
            await("未发现 Ark") { mainValue { app.discoveredDevices.isNotEmpty() || app.session.isReady } }
            mainValue { if (!app.session.isReady) app.onConnect(app.discoveredDevices.single()) }
            await("BLE 未连接") { mainValue { app.session.isReady } }
            val name = "native_${System.currentTimeMillis()}.film"
            mainValue { app.onUpload(name) }
            await("Ark 未接受直传", 40_000) { mainValue {
                val state = app.latestTransfer
                check(state?.phase != "error") { state?.message ?: "传输失败" }
                state?.message?.startsWith("Ark 已接受直传") == true
            } }
            mainValue { app.onCancel() }
            await("取消未完成", 55_000) { mainValue { app.latestTransfer?.phase in listOf("cancelled", "error", "done") } }
            val cancelled = mainValue { app.latestTransfer!! }
            check(cancelled.phase == "cancelled" && cancelled.cleanupCompleted && cancelled.canRetry) { "取消结果：$cancelled" }
            check(mainValue { app.session.isReady }) { "取消后 BLE 未保留" }
            report("已接受START后的取消通过：固件/手机清理确认，BLE仍连接，可重试")
            mainValue { app.onRetry() }
            await("重试未结束", 180_000) { mainValue { app.latestTransfer?.phase in listOf("done", "error") } }
            val finished = mainValue { app.latestTransfer!! }
            check(finished.success && finished.cleanupCompleted && finished.received == 172832L) { "重试结果：$finished" }
            check(mainValue { app.session.isReady }) { "传输后 BLE 未保留" }
            report("同一文件重试成功：172832字节，清理确认，BLE保持连接")
            mainValue { all(app.ui.view).filterIsInstance<TextView>().first { it.text.toString() == "Film" }.performClick() }
            result = "PASS"
            detail = "原生五页/六色转换/接受START后取消/同文件重试保存通过；屏幕待目视确认"
        } catch (error: Throwable) {
            detail = error.message ?: error.javaClass.simpleName
            activity?.let { mainValue { it.onCancel() } }
        }
        finish(if (result == "PASS") Activity.RESULT_OK else Activity.RESULT_CANCELED, Bundle().apply {
            putString("result", result); putString("detail", detail)
        })
    }
}
