# Local Windows build entry: SDKs, caches, output and temporary files live on D:.
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$FlutterArguments
)
$ErrorActionPreference = 'Stop'
# Web is the development preview; APKs default to distribution-signed Release.
if ($FlutterArguments.Count -ge 2 -and $FlutterArguments[0] -eq 'build' -and
    !(@('--debug', '--profile', '--release') | Where-Object { $FlutterArguments -contains $_ })) {
    if ($FlutterArguments[1] -eq 'web') { $FlutterArguments += '--debug' }
    if ($FlutterArguments[1] -eq 'apk') { $FlutterArguments += '--release' }
}
$storage = if ($env:FRAMEFILM_DEV_ROOT) { $env:FRAMEFILM_DEV_ROOT } else { 'D:\dev-tool' }
$app = Split-Path $PSScriptRoot -Parent
$flutter = Join-Path $storage 'FrameFilm-toolchains\flutter'
$sdk = Join-Path $storage 'Android\Sdk'
$output = Join-Path $storage 'FrameFilm-build\flutter'
$temporary = Join-Path $storage 'FrameFilm-build\temp'
if (!(Test-Path -LiteralPath "$flutter\bin\flutter.bat") -or !(Test-Path -LiteralPath $sdk)) {
    throw 'Flutter / Android SDK not found. See README for the local D-drive layout.'
}
$localBuild = Join-Path $app 'build'
if (Test-Path -LiteralPath $localBuild) {
    $buildItem = Get-Item -LiteralPath $localBuild
    if ($buildItem.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath($buildItem.Target) -ne [IO.Path]::GetFullPath($output)) {
        throw "Migrate existing build directory to $output first; no files were removed."
    }
} else {
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    New-Item -ItemType Junction -Path $localBuild -Target $output | Out-Null
}
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$values = @{
    FRAMEFILM_SIGNING_PROPERTIES = $(if ($env:FRAMEFILM_SIGNING_PROPERTIES) { $env:FRAMEFILM_SIGNING_PROPERTIES } else { 'D:\FrameFilm-signing\key.properties' })
    ANDROID_HOME = $sdk
    ANDROID_SDK_ROOT = $sdk
    GRADLE_USER_HOME = (Join-Path $storage 'gradle-home')
    PUB_CACHE = (Join-Path $storage 'pub-cache')
    TEMP = $temporary
    TMP = $temporary
    FLUTTER_SUPPRESS_ANALYTICS = 'true'
}
$saved = @{}
foreach ($key in $values.Keys) {
    $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    [Environment]::SetEnvironmentVariable($key, $values[$key], 'Process')
}
Push-Location $app
try {
    # local.properties is ignored; preserve other generated Flutter properties.
    $properties = Join-Path $app 'android\local.properties'
    $lines = if (Test-Path -LiteralPath $properties) { @(Get-Content -LiteralPath $properties) } else { @() }
    $lines = @($lines | Where-Object { $_ -notmatch '^(sdk.dir|flutter.sdk)=' })
    $lines += 'sdk.dir=' + $sdk.Replace('\', '/')
    $lines += 'flutter.sdk=' + $flutter.Replace('\', '/')
    Set-Content -LiteralPath $properties -Value $lines -Encoding utf8
    & "$flutter\bin\flutter.bat" @FlutterArguments
    $result = $LASTEXITCODE
    if ($result -eq 0 -and $FlutterArguments.Count -ge 2 -and
        $FlutterArguments[0] -eq 'build' -and $FlutterArguments[1] -eq 'apk' -and
        $FlutterArguments -contains '--debug' -and
        $FlutterArguments -notcontains '--split-per-abi' -and
        $FlutterArguments -notcontains '--flavor') {
        # A migrated incremental cache can leave a successfully packaged APK without
        # Dart code. Check the actual archive before reporting this debug build usable.
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $apk = [IO.Compression.ZipFile]::OpenRead((Join-Path $output 'app\outputs\flutter-apk\app-debug.apk'))
        try {
            foreach ($name in @('kernel_blob.bin', 'vm_snapshot_data', 'isolate_snapshot_data')) {
                $entry = $apk.GetEntry("assets/flutter_assets/$name")
                # This SDK permits an empty VM snapshot placeholder.
                if ($null -eq $entry -or ($name -ne 'vm_snapshot_data' -and $entry.Length -eq 0)) {
                    throw "APK missing $name. Remove this project's .dart_tool/flutter_build cache, run pub get and rebuild."
                }
            }
            Write-Output 'Verified debug APK: Dart kernel and runtime snapshots are present.'
        } finally { $apk.Dispose() }
    }
    if ($result -eq 0 -and $FlutterArguments.Count -ge 2 -and
        $FlutterArguments[0] -eq 'build' -and $FlutterArguments[1] -eq 'apk' -and
        $FlutterArguments -contains '--release' -and
        $FlutterArguments -notcontains '--split-per-abi' -and
        $FlutterArguments -notcontains '--flavor') {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $apk = [IO.Compression.ZipFile]::OpenRead((Join-Path $output 'app\outputs\flutter-apk\app-release.apk'))
        try {
            $names = @($apk.Entries | ForEach-Object { $_.FullName })
            if (!($names -like 'lib/*/libapp.so') -or !($names -like 'lib/*/libflutter.so') -or
                $names -contains 'assets/flutter_assets/kernel_blob.bin') {
                throw 'APK is missing its Release AOT runtime or contains a debug Dart kernel.'
            }
            Write-Output 'Verified Release APK: AOT application and Flutter engine are present.'
        } finally { $apk.Dispose() }
    }
} finally {
    Pop-Location
    foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
}
exit $result
