package org.framefilm.ark

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.TextUtils
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import java.util.TimeZone

data class ImageOptions(
    val rotation: Int = 0,
    val scale: Float = 1f,
    val offsetX: Float = 0f,
    val offsetY: Float = 0f,
    val brightness: Float = 0f,
    val contrast: Float = 1f,
    val saturation: Float = 1f,
    val dither: Boolean = true,
    val monochrome: Boolean = false,
)

/** Native Views for the five ForFilm sections, with Ark's black, white and yellow theme. */
class ArkUi(private val activity: Activity, private val actions: Actions) {
    interface Actions {
        fun onScan()
        fun onConnect(address: String)
        fun onDisconnect()
        fun onPickImage()
        fun onCaptureImage()
        fun onImageOptions(options: ImageOptions)
        fun onUpload(fileName: String)
        fun onExport(fileName: String)
        fun onCancel()
        fun onRetry()
        fun onClear()
        fun onRefreshFiles()
        fun onDisplayFile(id: Int)
        fun onDeleteFile(id: Int)
        fun onSetting(channel: Int, data: ByteArray)
        fun onReadSettings()
        fun onReadDeviceName()
        fun onSaveDeviceName(suffix: String)
        fun onPickFilm()
        fun onQuote(text: String, author: String)
    }

    private val ink = Color.rgb(23, 23, 23)
    private val paper = Color.rgb(247, 247, 243)
    private val yellow = Color.rgb(255, 234, 0)
    private val muted = Color.rgb(105, 105, 102)
    private val line = Color.rgb(210, 210, 205)
    private val root = FrameLayout(activity)
    val view: View get() = root

    private val contentHost = FrameLayout(activity)
    private val tabs = mutableListOf<TextView>()
    private val pages = mutableListOf<View>()
    private lateinit var pageTitle: TextView
    private lateinit var pageCode: TextView
    private lateinit var battery: TextView
    private lateinit var connectionStatus: TextView
    private lateinit var devices: LinearLayout
    private lateinit var disconnectButton: Button
    private lateinit var preview: ImageView
    private lateinit var previewInfo: TextView
    private lateinit var filmName: EditText
    private lateinit var animationName: EditText
    private lateinit var fileList: LinearLayout
    private lateinit var sleepSwitch: Switch
    private lateinit var wifiSwitch: Switch
    private lateinit var wifiSsid: EditText
    private lateinit var wifiPassword: EditText
    private lateinit var deviceNameSuffix: EditText
    private lateinit var deviceNameStatus: TextView
    private lateinit var transferCard: LinearLayout
    private lateinit var transferTitle: TextView
    private lateinit var transferCount: TextView
    private lateinit var transferProgress: ProgressBar
    private lateinit var cancelButton: Button
    private lateinit var retryButton: Button
    private lateinit var clearButton: Button
    private var tabIndex = 0
    private var changingSetting = false
    private var imageOptions = ImageOptions()
    private var resettingImageOptions = false
    private val imageOptionResetters = mutableListOf<() -> Unit>()

    init { build() }

    private fun dp(value: Int): Int = (activity.resources.displayMetrics.density * value + .5f).toInt()

    private fun shape(color: Int, radius: Int = 0, stroke: Int = 0): GradientDrawable =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = dp(radius).toFloat()
            if (stroke != 0) setStroke(dp(1), stroke)
        }

    private fun text(value: String, size: Float = 14f, color: Int = ink, bold: Boolean = false): TextView =
        TextView(activity).apply {
            this.text = value
            textSize = size
            setTextColor(color)
            if (bold) typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER_VERTICAL
        }

    private fun column(): LinearLayout = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL
    }

    private fun row(): LinearLayout = LinearLayout(activity).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
    }

    private fun space(height: Int): View = View(activity).apply {
        layoutParams = LinearLayout.LayoutParams(1, dp(height))
    }

    private fun sectionTitle(index: String, name: String): View = row().apply {
        val marker = text(index, 11f, ink, true).apply {
            gravity = Gravity.CENTER
            background = shape(yellow, 2)
        }
        addView(marker, LinearLayout.LayoutParams(dp(27), dp(27)))
        addView(text(name, 17f, ink, true), LinearLayout.LayoutParams(0, dp(30), 1f).apply { leftMargin = dp(10) })
    }

    private fun card(title: String, index: String, block: LinearLayout.() -> Unit): LinearLayout =
        column().apply {
            background = shape(Color.WHITE, 2, line)
            setPadding(dp(16), dp(16), dp(16), dp(16))
            addView(sectionTitle(index, title))
            addView(space(12))
            block()
        }

    private fun LinearLayout.addCard(card: View) {
        addView(card, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            bottomMargin = dp(12)
        })
    }

    private fun button(label: String, emphasized: Boolean = false, action: () -> Unit): Button =
        Button(activity).apply {
            text = label
            textSize = 14f
            isAllCaps = false
            setTextColor(if (emphasized) ink else ink)
            background = shape(if (emphasized) yellow else Color.WHITE, 2, ink)
            minHeight = dp(44)
            setPadding(dp(10), dp(6), dp(10), dp(6))
            setOnClickListener { action() }
        }

    private fun field(hint: String, value: String = ""): EditText = EditText(activity).apply {
        setSingleLine(true)
        setText(value)
        this.hint = hint
        textSize = 15f
        setTextColor(ink)
        setHintTextColor(muted)
        background = shape(Color.WHITE, 2, line)
        setPadding(dp(12), dp(10), dp(12), dp(10))
    }

    private fun LinearLayout.addField(label: String, input: EditText) {
        addView(text(label, 12f, muted, true))
        addView(space(6))
        addView(input, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46)))
        addView(space(12))
    }

    private fun LinearLayout.addButtons(vararg items: Button) {
        val bar = row()
        items.forEachIndexed { index, item ->
            bar.addView(item, LinearLayout.LayoutParams(0, dp(46), 1f).apply {
                if (index > 0) leftMargin = dp(8)
            })
        }
        addView(bar)
    }

    private fun page(): LinearLayout {
        val body = column().apply { setPadding(dp(16), dp(14), dp(16), dp(130)) }
        val scroll = ScrollView(activity).apply {
            isFillViewport = true
            addView(body)
        }
        pages += scroll
        return body
    }

    private fun build() {
        root.setBackgroundColor(paper)
        val shell = column()
        root.addView(shell, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        val head = column().apply {
            setPadding(dp(17), dp(12), dp(17), dp(11))
            background = shape(ink)
        }
        val brand = row()
        brand.addView(text("▌ FRAMEFILM", 18f, Color.WHITE, true), LinearLayout.LayoutParams(0, dp(29), 1f))
        brand.addView(text("ARK", 12f, ink, true).apply {
            gravity = Gravity.CENTER
            background = shape(yellow, 2)
        }, LinearLayout.LayoutParams(dp(49), dp(24)))
        battery = text("--%", 12f, Color.WHITE, true).apply {
            gravity = Gravity.RIGHT or Gravity.CENTER_VERTICAL
            setOnClickListener { actions.onSetting(0x23, byteArrayOf()) }
        }
        brand.addView(battery, LinearLayout.LayoutParams(dp(60), dp(27)))
        head.addView(brand)
        val titleRow = row().apply { setPadding(0, dp(12), 0, 0) }
        titleRow.addView(text("◆", 20f, yellow, true), LinearLayout.LayoutParams(dp(30), dp(35)))
        pageTitle = text("[ 连接 ]", 24f, Color.WHITE, true)
        titleRow.addView(pageTitle, LinearLayout.LayoutParams(0, dp(35), 1f))
        pageCode = text("LINK", 11f, yellow, true).apply { gravity = Gravity.RIGHT or Gravity.CENTER_VERTICAL }
        titleRow.addView(pageCode, LinearLayout.LayoutParams(dp(75), dp(35)))
        head.addView(titleRow)
        shell.addView(head)

        shell.addView(contentHost, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        buildConnectionPage()
        buildFramePage()
        buildFilmPage()
        buildAnimationPage()
        buildSettingsPage()

        val nav = row().apply {
            setBackgroundColor(ink)
            setPadding(dp(4), dp(4), dp(4), dp(4))
        }
        listOf("连接", "Frame", "Film", "动画", "设置").forEachIndexed { index, label ->
            val item = text(label, 13f, Color.WHITE, true).apply {
                gravity = Gravity.CENTER
                setOnClickListener { selectTab(index) }
            }
            tabs += item
            nav.addView(item, LinearLayout.LayoutParams(0, dp(53), 1f))
        }
        shell.addView(nav)
        buildTransferCard()
        selectTab(0)
    }

    private fun selectTab(index: Int) {
        tabIndex = index
        contentHost.removeAllViews()
        val parent = pages[index].parent as? ViewGroup
        parent?.removeView(pages[index])
        contentHost.addView(pages[index], FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        val names = listOf("连接", "Frame", "Film", "动画", "设置")
        val codes = listOf("LINK", "CAPTURE", "PIPELINE", "MOTION", "SYSTEM")
        pageTitle.text = "[ ${names[index]} ]"
        pageCode.text = codes[index]
        tabs.forEachIndexed { at, tab ->
            tab.setTextColor(if (at == index) ink else Color.WHITE)
            tab.background = if (at == index) shape(yellow, 2) else null
        }
    }

    private fun buildConnectionPage() {
        val body = page()
        body.addCard(card("连接状态", "01") {
            connectionStatus = text("未连接", 16f, ink, true)
            addView(connectionStatus)
            addView(space(14))
            disconnectButton = button("断开设备") { actions.onDisconnect() }.apply { visibility = View.GONE }
            addButtons(button("扫描 Ark 设备", true) { actions.onScan() }, disconnectButton)
        })
        body.addCard(card("附近设备", "02") {
            devices = column()
            devices.addView(text("点击扫描按钮查找 FrameFilm Ark", 14f, muted))
            addView(devices)
        })
        body.addCard(card("网络直传", "03") {
            addView(text("连接后可通过 WiFi Direct 发送 Film；蓝牙保持连接以管理设备和读取状态。", 13f, muted))
        })
    }

    private fun featureCard(body: LinearLayout, title: String, subtitle: String, index: String, action: (() -> Unit)?) {
        body.addCard(card(title, index) {
            addView(text(subtitle, 13f, muted))
            if (action != null) {
                addView(space(12))
                addView(button("进入", true, action), LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
            }
        })
    }

    private fun buildFramePage() {
        val body = page()
        featureCard(body, "拾光", "从手机相册挑选，进入 Film 调整后上屏", "01") {
            selectTab(2)
            actions.onPickImage()
        }
        featureCard(body, "定影", "拍一张照片，进入 Film 调整后上屏", "02") {
            selectTab(2)
            actions.onCaptureImage()
        }
        body.addCard(card("一言", "03") {
            addView(text("把文字与署名排成一张照片", 13f, muted))
            addView(space(12))
            val quoteText = field("输入名言")
            val author = field("作者", "佚名")
            addField("内容", quoteText)
            addField("署名", author)
            addView(button("生成并进入 Film", true) {
                actions.onQuote(quoteText.text.toString(), author.text.toString())
                selectTab(2)
            }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
        })
        featureCard(body, "批量上传", "本阶段可在 Film 页逐张上传；批量队列将在后续接入", "04", null)
    }

    private fun slider(label: String, initial: Int, value: (Int) -> String, changed: (Int) -> Unit): View {
        val wrapper = column()
        val caption = text("$label · ${value(initial)}", 13f, ink, true)
        wrapper.addView(caption)
        val seek = SeekBar(activity).apply {
            max = 100
            progress = initial
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                    caption.text = "$label · ${value(progress)}"
                    if (fromUser) changed(progress)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: SeekBar?) {}
            })
        }
        imageOptionResetters += { seek.progress = initial }
        wrapper.addView(seek)
        return wrapper
    }

    private fun changeOptions(change: (ImageOptions) -> ImageOptions) {
        if (resettingImageOptions) return
        imageOptions = change(imageOptions)
        actions.onImageOptions(imageOptions)
    }

    private fun buildFilmPage() {
        val body = page()
        body.addCard(card("上传与算法", "01") {
            addView(button("选择图像", true) { actions.onPickImage() }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46)))
            addView(space(12))
            addView(text("标准六色 / 黑白，Floyd–Steinberg 抖动；旋转、裁剪与颜色在下方调整", 13f, muted))
        })
        body.addCard(card("预览", "02") {
            preview = ImageView(activity).apply {
                scaleType = ImageView.ScaleType.FIT_CENTER
                setBackgroundColor(Color.rgb(239, 239, 235))
            }
            addView(preview, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(230)))
            addView(space(8))
            previewInfo = text("尚未选择图片", 13f, muted)
            addView(previewInfo)
        })
        body.addCard(card("画面调整", "03") {
            addView(button("旋转 90°") {
                changeOptions { it.copy(rotation = (it.rotation + 90) % 360) }
            }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
            addView(space(8))
            addView(slider("缩放", 29, { "%.2f×".format(.2f + 2.8f * it / 100f) }) {
                changeOptions { old -> old.copy(scale = .2f + 2.8f * it / 100f) }
            })
            addView(slider("左右位置", 50, { "%.2f".format((it - 50) / 50f) }) {
                changeOptions { old -> old.copy(offsetX = (it - 50) / 50f) }
            })
            addView(slider("上下位置", 50, { "%.2f".format((it - 50) / 50f) }) {
                changeOptions { old -> old.copy(offsetY = (it - 50) / 50f) }
            })
            addView(slider("亮度", 50, { "%.2f".format((it - 50) / 50f) }) {
                changeOptions { old -> old.copy(brightness = (it - 50) / 50f) }
            })
            addView(slider("对比度", 33, { "%.2f".format(.5f + 1.5f * it / 100f) }) {
                changeOptions { old -> old.copy(contrast = .5f + 1.5f * it / 100f) }
            })
            addView(slider("饱和度", 50, { "%.2f".format(2f * it / 100f) }) {
                changeOptions { old -> old.copy(saturation = 2f * it / 100f) }
            })
            val dither = Switch(activity).apply {
                text = "启用抖动"
                isChecked = true
                setOnCheckedChangeListener { _, checked -> changeOptions { it.copy(dither = checked) } }
            }
            imageOptionResetters += { dither.isChecked = true }
            addView(dither)
            val mono = Switch(activity).apply {
                text = "黑白模式"
                setOnCheckedChangeListener { _, checked -> changeOptions { it.copy(monochrome = checked) } }
            }
            imageOptionResetters += { mono.isChecked = false }
            addView(mono)
        })
        body.addCard(card("输出", "04") {
            filmName = field("文件名", "output.film")
            addField("Film 文件名", filmName)
            addButtons(button("导出 Film") { actions.onExport(filmName.text.toString()) },
                button("发送到设备", true) { actions.onUpload(filmName.text.toString()) })
        })
    }

    private fun buildAnimationPage() {
        val body = page()
        body.addCard(card("动画工坊", "01") {
            addView(text("本阶段支持导入现成的多帧 .film 文件并传到 Ark。逐帧绘制与 GIF 时间轴尚未接入。", 14f, muted))
            addView(space(12))
            addView(button("导入 Film", true) { actions.onPickFilm() }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
        })
        body.addCard(card("输出", "02") {
            animationName = field("文件名", "anim.film")
            addField("动画文件名", animationName)
            addView(button("发送到设备", true) { actions.onUpload(animationName.text.toString()) },
                LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
        })
    }

    private fun buildSettingsPage() {
        val body = page()
        body.addCard(card("蓝牙设备名", "00") {
            addView(text("固定前缀 FRAMEFILMARK- · 后缀最多 16 个 UTF-8 字节", 13f, muted))
            addView(space(10))
            deviceNameSuffix = field("设备名后缀")
            addField("FRAMEFILMARK-", deviceNameSuffix)
            addButtons(button("读取设备名") { actions.onReadDeviceName() },
                button("保存设备名", true) { actions.onSaveDeviceName(deviceNameSuffix.text.toString()) })
            addView(space(8))
            deviceNameStatus = text("保存后重启设备或重新初始化蓝牙才会生效", 12f, muted)
            addView(deviceNameStatus)
        })
        body.addCard(card("设备文件", "01") {
            addView(button("刷新文件列表") { actions.onRefreshFiles() }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
            addView(space(12))
            fileList = column()
            fileList.addView(text("尚未读取文件列表", 13f, muted))
            addView(fileList)
        })
        body.addCard(card("休眠与电量", "02") {
            sleepSwitch = Switch(activity).apply {
                text = "自动休眠"
                setOnCheckedChangeListener { _, checked ->
                    if (!changingSetting) actions.onSetting(0x25, byteArrayOf(if (checked) 1 else 0))
                }
            }
            addView(sleepSwitch)
            addView(space(8))
            addView(button("读取设备设置") { actions.onReadSettings() }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
        })
        body.addCard(card("网络配置", "03") {
            wifiSwitch = Switch(activity).apply {
                text = "设备 WiFi"
                setOnCheckedChangeListener { _, checked ->
                    if (!changingSetting) actions.onSetting(0x30, byteArrayOf(if (checked) 1 else 0))
                }
            }
            addView(wifiSwitch)
            wifiSsid = field("WiFi 名称")
            wifiPassword = field("WiFi 密码")
            addField("SSID", wifiSsid)
            addField("密码", wifiPassword)
            addButtons(button("保存网络") {
                actions.onSetting(0x32, asciiTerminated(wifiSsid.text.toString()))
                actions.onSetting(0x34, asciiTerminated(wifiPassword.text.toString()))
            }, button("连接") { actions.onSetting(0x38, byteArrayOf()) })
            addView(space(8))
            addView(button("断开 WiFi") { actions.onSetting(0x39, byteArrayOf()) },
                LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
        })
        body.addCard(card("遥控按键", "04") {
            addView(text("与设备实体按键保持相同交互", 13f, muted))
            addView(space(10))
            addButtons(button("上") { actions.onSetting(0x4E, byteArrayOf(0x03)) },
                button("下") { actions.onSetting(0x4E, byteArrayOf(0x02)) },
                button("确认", true) { actions.onSetting(0x4E, byteArrayOf(0x00)) })
            addView(space(8))
            addButtons(button("返回菜单") { actions.onSetting(0x4E, byteArrayOf(0x04)) },
                button("休眠") { actions.onSetting(0x4E, byteArrayOf(0x01)) })
        })
        body.addCard(card("时间同步", "05") {
            addView(text("使用手机当前时间与时区同步到 Ark", 13f, muted))
            addView(space(10))
            addView(button("同步时间", true) {
                val sec = System.currentTimeMillis() / 1000
                val minutes = TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 60000
                actions.onSetting(0x4D, byteArrayOf(
                    (sec ushr 24).toByte(), (sec ushr 16).toByte(), (sec ushr 8).toByte(), sec.toByte(),
                    (minutes ushr 8).toByte(), minutes.toByte(),
                ))
            }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
        })
    }

    private fun buildTransferCard() {
        transferCard = column().apply {
            background = shape(ink, 3)
            setPadding(dp(16), dp(12), dp(16), dp(12))
            visibility = View.GONE
            elevation = dp(12).toFloat()
        }
        transferTitle = text("准备传输", 15f, Color.WHITE, true).apply {
            maxLines = 2
            ellipsize = TextUtils.TruncateAt.END
        }
        transferCard.addView(transferTitle)
        transferCount = text("0 / 0 字节", 12f, yellow)
        transferCard.addView(transferCount)
        transferProgress = ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000 }
        transferCard.addView(transferProgress, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(6)).apply {
            topMargin = dp(10); bottomMargin = dp(10)
        })
        val buttons = row()
        cancelButton = button("取消") { actions.onCancel() }
        retryButton = button("重试", true) { actions.onRetry() }
        clearButton = button("清除") { actions.onClear() }
        listOf(cancelButton, retryButton, clearButton).forEachIndexed { index, item ->
            buttons.addView(item, LinearLayout.LayoutParams(0, dp(44), 1f).apply {
                if (index > 0) leftMargin = dp(8)
            })
        }
        transferCard.addView(buttons)
        root.addView(transferCard, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM).apply {
            leftMargin = dp(12); rightMargin = dp(12); bottomMargin = dp(64)
        })
    }

    fun updateConnection(connected: Boolean, message: String) {
        connectionStatus.text = message
        disconnectButton.visibility = if (connected) View.VISIBLE else View.GONE
    }

    fun showDevices(items: List<Pair<String, String>>) {
        devices.removeAllViews()
        if (items.isEmpty()) {
            devices.addView(text("没有发现设备，请点击扫描", 13f, muted))
            return
        }
        items.forEach { (address, name) ->
            devices.addView(button("$name  ·  $address") { actions.onConnect(address) },
                LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48)).apply { bottomMargin = dp(8) })
        }
    }

    fun showPreview(bitmap: Bitmap?, info: String) {
        preview.setImageBitmap(bitmap)
        previewInfo.text = info
    }

    /** Used when a new image is accepted; leaves navigation, file name and transfer state intact. */
    fun resetImageOptions() {
        resettingImageOptions = true
        try {
            imageOptions = ImageOptions()
            imageOptionResetters.forEach { it() }
        } finally {
            resettingImageOptions = false
        }
    }

    fun showTransfer(snapshot: TransferSnapshot) {
        transferCard.visibility = if (snapshot.phase == "idle") View.GONE else View.VISIBLE
        val active = snapshot.phase in setOf(
            "preparing", "staging", "connecting", "downloading", "transferring", "restoring", "cancelling", "cleanup"
        )
        pages.forEach { setPageInputsEnabled(it, !active) }
        transferTitle.text = snapshot.message
        transferCount.text = "${snapshot.received} / ${snapshot.total} 字节"
        transferProgress.progress = if (snapshot.total > 0) {
            ((snapshot.received.coerceAtLeast(0) * 1000) / snapshot.total).coerceIn(0, 1000).toInt()
        } else 0
        cancelButton.visibility = if (snapshot.canCancel) View.VISIBLE else View.GONE
        retryButton.visibility = if (snapshot.canRetry) View.VISIBLE else View.GONE
        retryButton.text = if (snapshot.cleanupCompleted) "重试" else "重试清理"
        clearButton.visibility = if (snapshot.cleanupCompleted && snapshot.phase in listOf("done", "error", "cancelled")) View.VISIBLE else View.GONE
    }

    private fun setPageInputsEnabled(node: View, enabled: Boolean) {
        if (node is Button || node is EditText || node is SeekBar || node is Switch) node.isEnabled = enabled
        if (node is ViewGroup) {
            for (index in 0 until node.childCount) setPageInputsEnabled(node.getChildAt(index), enabled)
        }
    }

    fun showFiles(items: List<Pair<Int, String>>) {
        fileList.removeAllViews()
        if (items.isEmpty()) {
            fileList.addView(text("设备中没有 Film 文件", 13f, muted))
            return
        }
        items.forEach { (id, name) ->
            fileList.addView(text(name, 14f, ink, true))
            val buttons = row()
            buttons.addView(button("显示") { actions.onDisplayFile(id) }, LinearLayout.LayoutParams(0, dp(40), 1f))
            buttons.addView(button("删除") {
                AlertDialog.Builder(activity).setTitle("删除 $name？")
                    .setMessage("此操作会删除设备上的文件。")
                    .setNegativeButton("取消", null)
                    .setPositiveButton("删除") { _, _ -> actions.onDeleteFile(id) }
                    .show()
            }, LinearLayout.LayoutParams(0, dp(40), 1f).apply { leftMargin = dp(8) })
            fileList.addView(buttons)
            fileList.addView(space(10))
        }
    }

    fun showBattery(level: Int) { battery.text = "${level.coerceIn(0, 100)}%" }

    fun showDeviceName(name: String, saved: Boolean) {
        deviceNameSuffix.setText(name.removePrefix("FRAMEFILMARK-"))
        deviceNameStatus.text = if (saved) "已保存 $name；重启设备或重新初始化蓝牙后生效" else "当前配置：$name"
    }

    fun showDeviceNameStatus(message: String) { deviceNameStatus.text = message }

    fun showSetting(channel: Int, data: ByteArray) {
        if (channel == 0x26 && data.isNotEmpty()) {
            changingSetting = true
            try { sleepSwitch.isChecked = data[0].toInt() != 0 } finally { changingSetting = false }
        }
        if (channel == 0x31 && data.isNotEmpty()) {
            changingSetting = true
            try { wifiSwitch.isChecked = data[0].toInt() != 0 } finally { changingSetting = false }
        }
        if (channel == 0x33) wifiSsid.setText(asciiValue(data))
        if (channel == 0x35) wifiPassword.setText(asciiValue(data))
    }

    private fun asciiTerminated(value: String): ByteArray =
        value.toByteArray(Charsets.US_ASCII) + byteArrayOf(0)

    private fun asciiValue(data: ByteArray): String =
        String(data.takeWhile { it != 0.toByte() }.toByteArray(), Charsets.US_ASCII)

    fun showMessage(message: String) { Toast.makeText(activity, message, Toast.LENGTH_SHORT).show() }
    fun selectedFileName(): String = if (tabIndex == 3) animationName.text.toString() else filmName.text.toString()

    fun handleBack(): Boolean {
        if (tabIndex == 0) return false
        selectTab(0)
        return true
    }
}
