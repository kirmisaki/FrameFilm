package org.framefilm.ark

data class TransferSnapshot(
    val phase: String,
    val message: String,
    val received: Long = 0,
    val total: Long = 0,
    val canCancel: Boolean = false,
    val canRetry: Boolean = false,
    val cleanupCompleted: Boolean = false,
    val success: Boolean = false,
    val kind: String = "film",
    val targetVersion: String? = null,
    val targetBuild: String? = null,
    val canConfirm: Boolean = false,
)
