package org.framefilm.framefilm_ark

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import java.io.File

/** Exposes only the prepared APK, read-only, via a temporary installer grant. */
class AppUpdateProvider : ContentProvider() {
    override fun onCreate() = true
    private fun file(uri: Uri): File {
        require(uri.path == "/update.apk") { "Unknown update file" }
        return File(requireNotNull(context).cacheDir, "app-update/update.apk")
    }
    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        require(mode == "r") { "Update APK is read-only" }
        return ParcelFileDescriptor.open(file(uri), ParcelFileDescriptor.MODE_READ_ONLY)
    }
    override fun getType(uri: Uri) = "application/vnd.android.package-archive"
    override fun query(uri: Uri, projection: Array<out String>?, selection: String?, selectionArgs: Array<out String>?, sortOrder: String?): Cursor {
        val target = file(uri)
        val columns = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        return MatrixCursor(columns).apply { addRow(columns.map {
            when (it) { OpenableColumns.SIZE -> target.length(); OpenableColumns.DISPLAY_NAME -> "FrameFilm-update.apk"; else -> null }
        }) }
    }
    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?) = 0
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?) = 0
}
