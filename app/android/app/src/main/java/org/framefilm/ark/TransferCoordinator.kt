package org.framefilm.ark

/** One attempt owns the shared GATT transport until its operation has settled. */
interface TransferCoordinator {
    val needsConfirmation: Boolean
    val isActive: Boolean
    val expectsBleResume: Boolean
    val cleanupConfirmed: Boolean
    fun start()
    fun cancel()
    fun onBleDisconnected()
    fun onBleReady()
    fun confirmFirmware()
    fun ensureSettled(callback: (Boolean, String) -> Unit)
}
