package org.framefilm.framefilm_ark

import java.net.URI

/** Shared source rules; runtime package identity, never request arguments, picks a channel. */
internal object AppReleasePolicy {
    const val STABLE = "kirmisaki/FrameFilm"
    const val DEVELOPMENT = "Rafael-Ikaros/FrameFilm"
    const val REPOSITORY_METADATA = "org.framefilm.ark.releaseRepository"
    private val repositories = mapOf(
        "org.framefilm.ark.flutter" to STABLE,
        "org.framefilm.ark.flutter.dev" to DEVELOPMENT,
    )

    fun canonicalRepository(value: String): String = repositories.values.firstOrNull {
        it.equals(value, ignoreCase = true)
    } ?: error("不支持的发布仓库")

    fun repositoryForPackage(packageName: String): String? = repositories[packageName]

    fun requireAsset(address: String, repository: String, tag: String, name: String): URI {
        val expected = canonicalRepository(repository).split('/')
        val uri = URI(address)
        val path = uri.rawPath?.split('/') ?: emptyList()
        require(safeHttps(uri) && uri.host.equals("github.com", true) &&
            uri.rawQuery == null && path.size == 7 && path[0].isEmpty() &&
            path[1].equals(expected[0], true) && path[2].equals(expected[1], true) &&
            path[3] == "releases" && path[4] == "download" && tag.isNotEmpty() &&
            // URI.path decodes percent escapes, without treating '+' as a space.
            URI("https://github.com/" + path[5]).path.substring(1) == tag &&
            path[6] == name) { "文件必须来自当前应用仓库的精确 Release 标签与资产" }
        return uri
    }

    fun requireRedirect(address: String, repository: String, tag: String, name: String) {
        if (runCatching { requireAsset(address, repository, tag, name) }.isSuccess) return
        val uri = URI(address)
        require(safeHttps(uri) && when (uri.host?.lowercase()) {
            "release-assets.githubusercontent.com" -> uri.path.startsWith("/github-production-release-asset/")
            "objects.githubusercontent.com", "github-releases.githubusercontent.com" ->
                uri.path.startsWith("/github-production-release-asset-2e65be/")
            else -> false
        }) { "GitHub 资产重定向到了非预期地址" }
    }

    private fun safeHttps(uri: URI) = uri.scheme == "https" &&
        (uri.port == -1 || uri.port == 443) && uri.rawUserInfo == null && uri.rawFragment == null
}
