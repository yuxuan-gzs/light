param(
    [switch]$EnableGui,
    [switch]$Console
)

if (($EnableGui -and $Console) -or (-not $EnableGui -and -not $Console)) {
    Write-Host '用法：SetGuiMode.ps1 -EnableGui  或  SetGuiMode.ps1 -Console' -ForegroundColor Yellow
    exit 2
}

$baseDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cacheDir = Join-Path $baseDir 'Cache'
New-Item -ItemType Directory -Path $cacheDir -Force | Out-Null
$configPath = Join-Path $cacheDir 'g.xayx'

if ($EnableGui) {
    $plainText = 'dkg33h4x2f24hdg5x2f2g4:Tdsg2'
} else {
    $plainText = 'dkg33h4x2f24hdg5x2f2g4:FT12sf'
}

$keyBytes = [System.Text.Encoding]::ASCII.GetBytes('sr5r4g464dry8k78665sglnjtr4m5z578tk78q')
$dataBytes = [System.Text.Encoding]::ASCII.GetBytes($plainText)
for ($i = 0; $i -lt $dataBytes.Length; $i++) {
    $dataBytes[$i] = $dataBytes[$i] -bxor $keyBytes[$i % $keyBytes.Length]
}
[System.IO.File]::WriteAllBytes($configPath, $dataBytes)

if ($EnableGui) {
    Write-Host "已启用 GUI 模式：$configPath" -ForegroundColor Green
} else {
    Write-Host "已切换到纯控制台模式：$configPath" -ForegroundColor Green
}
