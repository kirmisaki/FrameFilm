package org.framefilm.framefilm_ark

/** Plain JVM probe: no Android device, network, Gradle or Flutter SDK required. */
fun main() {
    fun rejects(block: () -> Unit) { check(runCatching(block).isFailure) }
    check(AppReleasePolicy.repositoryForPackage("org.framefilm.ark.flutter") == AppReleasePolicy.STABLE)
    check(AppReleasePolicy.repositoryForPackage("org.framefilm.ark.flutter.dev") == AppReleasePolicy.DEVELOPMENT)
    check(AppReleasePolicy.repositoryForPackage("org.framefilm.ark.flutter.dev.debug") == null)
    check(AppReleasePolicy.repositoryForPackage("unknown") == null)
    check(AppReleasePolicy.canonicalRepository("RAFAEL-IKAROS/FRAMEFILM") == AppReleasePolicy.DEVELOPMENT)
    rejects { AppReleasePolicy.canonicalRepository("Rafael-ban/FrameFilm") }
    rejects { AppReleasePolicy.canonicalRepository("") }
    val repo = AppReleasePolicy.DEVELOPMENT
    val tag = "ark/v1+2"
    val name = "framefilm-ark-flutter.apk"
    val origin = "https://github.com/$repo/releases/download/ark%2Fv1%2B2/$name"
    AppReleasePolicy.requireAsset(origin, repo, tag, name)
    val invalid = listOf(
        origin.replace(repo, AppReleasePolicy.STABLE), origin.replace("https:", "http:"),
        origin.replace("github.com", "github.com.evil.org"), origin.replace("github.com", "user@github.com"),
        origin.replace("github.com", "github.com:444"), "$origin?x=1", "$origin#x",
        origin.replace("v1%2B2", "v2"), origin.replace(name, "other.apk"),
    )
    invalid.forEach { address -> rejects { AppReleasePolicy.requireAsset(address, repo, tag, name) } }
    AppReleasePolicy.requireRedirect(origin, repo, tag, name)
    AppReleasePolicy.requireRedirect("https://release-assets.githubusercontent.com/github-production-release-asset/1/id?sig=ok", repo, tag, name)
    AppReleasePolicy.requireRedirect("https://objects.githubusercontent.com/github-production-release-asset-2e65be/1/id?sig=ok", repo, tag, name)
    listOf("https://example.com/file", "http://release-assets.githubusercontent.com/github-production-release-asset/1/id",
        "https://release-assets.githubusercontent.com/unexpected/1/id", "https://user@release-assets.githubusercontent.com/github-production-release-asset/1/id",
        origin.replace(repo, AppReleasePolicy.STABLE)).forEach { address ->
        rejects { AppReleasePolicy.requireRedirect(address, repo, tag, name) }
    }
    println("AppReleasePolicy: package/channel, exact asset, encoded tag and redirect probes passed")
}
