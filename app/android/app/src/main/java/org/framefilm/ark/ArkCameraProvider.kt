package org.framefilm.ark

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import java.io.File

/** Grants the system camera temporary access to one capture file. */
class ArkCameraProvider : ContentProvider() {
    override fun onCreate() = true
    private fun file(uri: Uri): File {
        require(uri.path == "/photo.jpg") { "Unknown camera file" }
        return File(context!!.cacheDir, "ark-camera/photo.jpg").apply { parentFile?.mkdirs() }
    }
    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor =
        ParcelFileDescriptor.open(file(uri), ParcelFileDescriptor.parseMode(mode))
    override fun getType(uri: Uri) = "image/jpeg"
    override fun query(uri: Uri, projection: Array<out String>?, selection: String?, selectionArgs: Array<out String>?, sortOrder: String?): Cursor {
        val file = file(uri)
        val columns = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        return MatrixCursor(columns).apply { addRow(columns.map { if (it == OpenableColumns.SIZE) file.length() else file.name }) }
    }
    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?) = 0
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?) = 0
}
