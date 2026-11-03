<#
  FrameFilm 经典固件（三机型）构建脚本

    std  : 16MB Flash + Octal PSRAM  -> sdkconfig_std
    pro  : n4r2  4MB Flash  + Quad   PSRAM -> sdkconfig_pro
           n16r8 16MB Flash + Octal  PSRAM -> sdkconfig_pro_n16r8
    max  : 16MB Flash + Octal PSRAM  -> sdkconfig_max

  机型通过 -DFRAMEFILM_MODEL 传给 CMake（无需手动改 sys_cfg.h），
  每个“机型 + 硬件变体”组合使用独立 build_<机型>_<变体> 目录，来回切换不会触发全量重编。

  用法：
    .\build.ps1                          # 编译 Pro n4r2
    .\build.ps1 pro n16r8                # 编译 Pro n16r8
    .\build.ps1 std                      # 编译 STD（仅 n16r8）
    .\build.ps1 max                      # 编译 Max（仅 n16r8）
    .\build.ps1 pro n16r8 flash COM15    # 编译并烧录到 COM15
    .\build.ps1 max n16r8 monitor COM15  # 编译、烧录并打开串口监视
    .\build.ps1 pro n4r2 menuconfig      # 修改该组合的配置
#>
param(
    [ValidateSet('std', 'pro', 'max')]
    [string]$Model = 'pro',

    [ValidateSet('', 'n4r2', 'n16r8')]
    [string]$Variant = '',

    [ValidateSet('build', 'flash', 'monitor', 'menuconfig', 'size', 'fullclean')]
    [string]$Action = 'build',

    [string]$Port = ''
)

$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "未找到 idf.py，请先加载 ESP-IDF 环境（export.ps1 或 ESP-IDF 终端）"
}

# 各机型支持的硬件变体（STD / Max 只有 16MB + Octal 这一种）
$modelVariants = @{
    'std' = @{ 'n16r8' = 'sdkconfig_std' }
    'pro' = @{ 'n4r2' = 'sdkconfig_pro'; 'n16r8' = 'sdkconfig_pro_n16r8' }
    'max' = @{ 'n16r8' = 'sdkconfig_max' }
}

if (-not $Variant) {
    $Variant = if ($Model -eq 'pro') { 'n4r2' } else { 'n16r8' }
}
$variants = $modelVariants[$Model]
if (-not $variants.ContainsKey($Variant)) {
    $supported = $variants.Keys -join ' / '
    throw "$Model 机型不支持 $Variant 变体，可选：$supported"
}

$sdkconfig = Join-Path $projectDir $variants[$Variant]
if (-not (Test-Path $sdkconfig)) {
    throw "找不到配置文件：$sdkconfig"
}

$buildDir = Join-Path $projectDir "build_${Model}_${Variant}"
$idfArgs = @('-B', $buildDir, '-D', "SDKCONFIG=$sdkconfig", '-D', "FRAMEFILM_MODEL=$Model")
if ($Port) {
    $idfArgs += @('-p', $Port)
}
$idfArgs += $Action

Write-Host "model    : $Model" -ForegroundColor Cyan
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
