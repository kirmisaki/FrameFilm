$ErrorActionPreference = 'Stop'
$testRoot = $PSScriptRoot
$buildRoot = Join-Path $testRoot '.build'
$includeRoot = Join-Path $buildRoot 'include'
New-Item -ItemType Directory -Force -Path $buildRoot,$includeRoot | Out-Null
foreach ($header in @('freertos/FreeRTOS.h','freertos/semphr.h','esp_partition.h','esp_crc.h','esp_log.h','nvs.h')) {
    $stub = Join-Path $includeRoot $header
    New-Item -ItemType Directory -Force -Path (Split-Path $stub) | Out-Null
    Set-Content -LiteralPath $stub -Value '#include "host_sdk.h"' -Encoding ascii
}
$serviceInc = Join-Path $testRoot '../../frame_film/components/film_service/inc'
$source = Join-Path $testRoot '../components/film_service/service_file_c3.c'
$testSource = Join-Path $testRoot 'file_store_test.c'
$exe = Join-Path $buildRoot 'file_store_test.exe'
if (Get-Command cl -ErrorAction SilentlyContinue) {
    & cl /nologo /std:c11 /W4 /WX "/I$testRoot" "/I$includeRoot" "/I$serviceInc" $testSource $source "/Fe$exe" "/Fo$buildRoot\"
    if ($LASTEXITCODE -ne 0) { throw 'Host compile failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test failed' }
} else { throw 'Run from a Visual Studio x64 native tools shell (cl required).' }
