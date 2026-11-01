<#
  FrameFilm Ark 硬件变体构建脚本

    n4r2  : 4MB Flash  + Quad  2MB PSRAM  -> sdkconfig + partitions.csv
    n16r8 : 16MB Flash + Octal 8MB PSRAM  -> sdkconfig_n16r8 + partitions_16mb.csv

  每个变体使用独立的 build_<variant> 目录，来回切换不会触发全量重编。

  用法：
    .\build.ps1                          # 编译 n4r2
    .\build.ps1 n16r8                    # 编译 n16r8
    .\build.ps1 n16r8 flash COM15        # 编译并烧录到 COM15
    .\build.ps1 n4r2 monitor COM15       # 编译、烧录并打开串口监视
    .\build.ps1 n16r8 menuconfig         # 修改该变体的配置
#>
param(
    [ValidateSet('n4r2', 'n16r8')]
    [string]$Variant = 'n4r2',

    [ValidateSet('build', 'flash', 'monitor', 'menuconfig', 'size', 'fullclean')]
    [string]$Action = 'build',

    [string]$Port = ''
)

$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "未找到 idf.py，请先加载 ESP-IDF 环境（export.ps1 或 ESP-IDF 终端）"
}

# n4r2 沿用仓库根部的 sdkconfig，避免再复制一份 100KB 配置
$sdkconfig = if ($Variant -eq 'n4r2') {
    Join-Path $projectDir 'sdkconfig'
} else {
    Join-Path $projectDir "sdkconfig_$Variant"
}
if (-not (Test-Path $sdkconfig)) {
    throw "找不到配置文件：$sdkconfig"
}

$buildDir = Join-Path $projectDir "build_$Variant"
$idfArgs = @('-B', $buildDir, '-D', "SDKCONFIG=$sdkconfig")
if ($Port) {
    $idfArgs += @('-p', $Port)
}
$idfArgs += $Action

Write-Host "variant  : $Variant" -ForegroundColor Cyan
Write-Host "sdkconfig: $sdkconfig" -ForegroundColor Cyan
Write-Host "build dir: $buildDir" -ForegroundColor Cyan
Write-Host "action   : $Action" -ForegroundColor Cyan

Push-Location $projectDir
try {
    & idf.py @idfArgs
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $code
