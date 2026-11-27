package org.framefilm.framefilm_ark

import org.framefilm.ark.TransferPolicy
import org.framefilm.ark.TransferPolicy.PassportRead

fun main() {
    // 0x53: payload <= MTU-16 and never above the firmware frame limit (176).
    val sizes = mapOf(23 to 7, 128 to 112, 191 to 175, 192 to 176, 200 to 176, 500 to 176)
    for ((mtu, expected) in sizes) {
        check(PassportRead.chunkSize(mtu) == expected) { "chunk for MTU $mtu" }
    }
    check(PassportRead.needsReconnect(23))
    check(PassportRead.needsReconnect(79))
    check(!PassportRead.needsReconnect(80))
    check(!PassportRead.needsReconnect(192))

    // Old firmware (max 128) answers a larger count with status 4, once.
    check(PassportRead.shouldFallbackToLegacy(176, 4))
    check(!PassportRead.shouldFallbackToLegacy(128, 4))
    check(!PassportRead.shouldFallbackToLegacy(176, 0))
    check(!PassportRead.shouldFallbackToLegacy(176, 3))

    // Shared pacing helpers keep every BLE loop on the same budget.
    check(TransferPolicy.progressStep(0) == 4096L)
    check(TransferPolicy.progressStep(400_000) == 4096L)
    check(TransferPolicy.progressStep(800_000) == 8000L)
    val total = 200_000L
    val chunk = 176
    val chunks = (total + chunk - 1) / chunk
    check(TransferPolicy.transferDeadlineMs(total, chunk) == 180_000L + chunks * 150L)
    check(TransferPolicy.transferDeadlineMs(1_937_280L, 181) ==
        180_000L + ((1_937_280L + 180) / 181) * 150L)
    println("Passport readback chunk/fallback/pacing host checks passed")
}
