package org.framefilm.ark

import android.app.Activity
import android.app.Instrumentation
import android.bluetooth.BluetoothDevice
import android.os.Bundle
import android.util.Log
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Debug-only hardware probe. Uses the same controllers as the visible app. */
class ConnectivityProbe : Instrumentation() {
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        start()
    }

    override fun onStart() {
        val completed = CountDownLatch(1)
        val cleaned = CountDownLatch(1)
        var ble: ArkBleController? = null
        var p2p: ArkP2pController? = null
        var connecting = false
        var groupReady = false
        var ended = false
        var passed = false
        var cleanupSucceeded = false
        var detail = "Probe timed out"

        fun report(message: String) {
            Log.i("ArkProbe", message)
            sendStatus(0, Bundle().apply { putString("progress", message) })
        }

        fun end(success: Boolean, message: String) {
            if (ended) return
            ended = true
            passed = success
            detail = message
            report(message)
            completed.countDown()
        }

        try {
            runOnMainSync {
                p2p = ArkP2pController(targetContext, object : ArkP2pController.Listener {
                    override fun onState(message: String) = report("P2P: $message")
                    override fun onError(message: String) = end(false, "P2P: $message")
                    override fun onGroupReady(frequencyMhz: Int) {
                        if (ended) return
                        if (frequencyMhz !in 2400..2500) {
                            end(false, "Unexpected P2P frequency: $frequencyMhz MHz")
                            return
                        }
                        groupReady = true
                        report("P2P group ready at $frequencyMhz MHz; checking BLE again")
                        ble?.readPanel()
                    }
                })
                ble = ArkBleController(targetContext, object : ArkBleController.Listener {
                    override fun onState(message: String) = report("BLE: $message")
                    override fun onError(message: String) = end(false, "BLE: $message")
                    override fun onDevices(devices: List<BluetoothDevice>) {
                        if (ended || connecting || devices.isEmpty()) return
                        if (devices.size != 1) {
                            end(false, "More than one Ark found; choose a device in the app")
                            return
                        }
                        connecting = true
                        ble?.connect(devices.single())
                    }
                    override fun onReady() {
                        if (!ended) ble?.readPanel()
                    }
                    override fun onPanel(panelId: Int, width: Int, height: Int) {
                        if (ended) return
                        if (panelId != 2 || width != 720 || height != 480) {
                            end(false, "Unexpected panel: id=$panelId ${width}x$height")
                            return
                        }
                        if (groupReady) {
                            end(true, "PASS: Ark panel 720x480 responds before and during 2.4GHz P2P group")
                        } else {
                            report("Ark panel verified: id=2 720x480; creating P2P group")
                            p2p?.createGroup2Ghz()
                        }
                    }
                })
                ble?.startScan()
            }
            if (!completed.await(70, TimeUnit.SECONDS)) {
                runOnMainSync { end(false, "Timed out waiting for the connectivity probe") }
            }
        } catch (error: Exception) {
            passed = false
            detail = "Probe failed: ${error.javaClass.simpleName}: ${error.message}"
            report(detail)
        } finally {
            runOnMainSync {
                ended = true
                ble?.close()
                val controller = p2p
                if (controller == null) {
                    cleanupSucceeded = true
                    cleaned.countDown()
                } else controller.close { success ->
                    cleanupSucceeded = success
                    cleaned.countDown()
                }
            }
            if (!cleaned.await(12, TimeUnit.SECONDS)) {
                passed = false
                detail += "; P2P cleanup did not complete"
            } else if (!cleanupSucceeded) {
                passed = false
                detail += "; P2P cleanup failed"
            }
        }

        finish(if (passed) Activity.RESULT_OK else Activity.RESULT_CANCELED, Bundle().apply {
            putString("result", if (passed) "PASS" else "FAIL")
            putString("detail", detail)
            putBoolean("cleanupCompleted", cleaned.count == 0L && cleanupSucceeded)
        })
    }
}
