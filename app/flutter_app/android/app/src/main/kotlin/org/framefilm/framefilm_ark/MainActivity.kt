package org.framefilm.framefilm_ark

import android.content.Intent
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine

class MainActivity : FlutterActivity() {
    private var bridge: ArkBridge? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        bridge = ArkBridge(this, flutterEngine.dartExecutor.binaryMessenger)
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        if (bridge?.onRequestPermissionsResult(requestCode, permissions, grantResults) != true) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        }
    }

    @Deprecated("Platform activity results for the document picker")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (bridge?.onActivityResult(requestCode, resultCode, data) != true) {
            super.onActivityResult(requestCode, resultCode, data)
        }
    }

    override fun cleanUpFlutterEngine(flutterEngine: FlutterEngine) {
        bridge?.close()
        bridge = null
        super.cleanUpFlutterEngine(flutterEngine)
    }
}
