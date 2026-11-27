<#
  FrameFilmInfo 构建脚本（单机型）
    硬件：FrameFilmCommon 主板
          ESP32-S3-WROOM-1-N16R8（16MB Flash + Octal PSRAM）
          GDEM075T41WT 7.5" 800×480 黑白 1bpp
    配置：sdkconfig（partitions_16mb.csv）

  用法：
    .\build.ps1                       # 编译
    .\build.ps1 flash COM15           # 编译并烧录
    .\build.ps1 monitor COM15         # 编译、烧录并打开串口监视
    .\build.ps1 menuconfig            # 修改配置
    .\build.ps1 size                  # 查看镜像大小
    .\build.ps1 fullclean             # 清理构建目录
#>
param(
    [ValidateSet('build', 'flash', 'monitor', 'menuconfig', 'size', 'fullclean')]
    [string]$Action = 'build',

    [string]$Port = ''
)

$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "未找到 idf.py，请先加载 ESP-IDF 环境（export.ps1 或 ESP-IDF 终端）"
}

$sdkconfig = Join-Path $projectDir 'sdkconfig'
if (-not (Test-Path $sdkconfig)) {
    throw "找不到配置文件：$sdkconfig"
}

$buildDir = Join-Path $projectDir 'build'
$idfArgs = @('-B', $buildDir, '-D', "SDKCONFIG=$sdkconfig")
if ($Port) {
    $idfArgs += @('-p', $Port)
}
$idfArgs += $Action

Write-Host "board    : FrameFilmCommon (ESP32-S3-WROOM-1-N16R8)" -ForegroundColor Cyan
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
