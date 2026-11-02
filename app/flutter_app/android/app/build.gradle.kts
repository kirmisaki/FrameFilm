import java.util.Properties

plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

val releaseProperties = Properties()
val releasePropertiesFile = System.getenv("FRAMEFILM_SIGNING_PROPERTIES")
    ?.let { file(it) } ?: rootProject.file("key.properties")
if (releasePropertiesFile.isFile) {
    releasePropertiesFile.inputStream().use { releaseProperties.load(it) }
}
val releaseStoreFile = System.getenv("ANDROID_KEYSTORE_PATH") ?: releaseProperties.getProperty("storeFile")
val releaseStorePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD") ?: releaseProperties.getProperty("storePassword")
val releaseKeyAlias = System.getenv("ANDROID_KEY_ALIAS") ?: releaseProperties.getProperty("keyAlias")
val releaseKeyPassword = System.getenv("ANDROID_KEY_PASSWORD") ?: releaseProperties.getProperty("keyPassword")
val hasReleaseSigning = listOf(releaseStoreFile, releaseStorePassword, releaseKeyAlias, releaseKeyPassword)
    .all { !it.isNullOrBlank() }
gradle.taskGraph.whenReady {
    if (allTasks.any { it.project == project && it.name in listOf("packageRelease", "signReleaseBundle") }) {
        require(hasReleaseSigning) {
            "Release requires a distribution keystore. Set ANDROID_KEYSTORE_* / ANDROID_KEY_* or FRAMEFILM_SIGNING_PROPERTIES."
        }
    }
}

android {
    namespace = "org.framefilm.framefilm_ark"
    compileSdk = 35

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        // TODO: Specify your own unique Application ID (https://developer.android.com/studio/build/application-id.html).
        applicationId = "org.framefilm.ark.flutter"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = 28
        targetSdk = 35
        // Uses the version code from pubspec.yaml. When using split APKs, 1000 * ABI_VERSION
        // is added automatically by Flutter. (https://developer.android.com/studio/build/configure-apk-splits#configure-APK-versions)
        // You can force using the value of versionCode by specifying the `-P force-version-code-ignoring-abi=true`
        // flag during build.
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    signingConfigs {
        create("release") {
            if (hasReleaseSigning) {
                storeFile = file(requireNotNull(releaseStoreFile))
                storePassword = releaseStorePassword
                keyAlias = releaseKeyAlias
                keyPassword = releaseKeyPassword
            }
        }
    }
    buildTypes {
        debug {
            applicationIdSuffix = ".dev"
            versionNameSuffix = "-dev"
        }
        release {
            signingConfig = signingConfigs.getByName("release")
        }
    }
}

kotlin {
    sourceSets.getByName("main").kotlin.apply {
        srcDir(rootProject.file("../../android/app/src/main/java"))
        exclude("org/framefilm/ark/MainActivity.kt", "org/framefilm/ark/ArkUi.kt")
    }
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}
