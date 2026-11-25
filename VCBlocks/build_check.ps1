# build_check.ps1 - 手动编译 VCBlocks 全部源码做语法/类型自检
$ErrorActionPreference = 'Continue'

$msvc = 'I:\Microsoft Visual Studio\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$kit  = 'I:\Windows Kits\10'
$sdk  = '10.0.26100.0'

$env:INCLUDE = "$msvc\include;$msvc\atlmfc\include;$kit\Include\$sdk\ucrt;$kit\Include\$sdk\shared;$kit\Include\$sdk\um;$kit\Include\$sdk\winrt;$kit\Include\$sdk\cppwinrt"
$env:LIB     = "$msvc\lib\x64;$msvc\atlmfc\lib\x64;$kit\Lib\$sdk\ucrt\x64;$kit\Lib\$sdk\um\x64"
$env:PATH    = "$msvc\bin\Hostx64\x64;$kit\bin\$sdk\x64;$env:PATH"

$cl = "$msvc\bin\Hostx64\x64\cl.exe"
$src = 'D:\YX\VCBlocks\VCBlocks\src'
$out = 'D:\YX\VCBlocks\build'
if (!(Test-Path $out)) { New-Item -ItemType Directory -Force -Path $out | Out-Null }

$files = @(
  'core\util.cpp','core\Json.cpp','core\BlockModel.cpp','core\BlockLibrary.cpp',
  'core\Translator.cpp','core\ProjectStore.cpp','core\CodeGenerator.cpp',
  'core\Decompiler.cpp','core\LibraryScanner.cpp','core\TccCompiler.cpp',
  'core\JdkCompiler.cpp','core\MsvcCompiler.cpp','core\Http.cpp','core\LibraryStore.cpp',
  'core\AppSettings.cpp','core\DllBuilder.cpp','ui\Theme.cpp','ui\MainWindow.cpp',
  'ui\LibraryStorePanel.cpp','ui\StartupDialog.cpp','PluginEntry.cpp'
)

$failed = @()
foreach ($f in $files) {
  $base = [System.IO.Path]::GetFileNameWithoutExtension($f)
  $obj = Join-Path $out ($base + '.obj')
  $args = @('/c','/nologo','/utf-8','/EHsc','/MT','/W3','/std:c++20','/permissive-',
            '/D','NOMINMAX','/D','NDEBUG',
            "/I$src", "/ID:\YX\VCBlocks\VCBlocks\include",
            "/Fo$obj", (Join-Path $src $f))
  $msg = & $cl @args 2>&1
  if ($LASTEXITCODE -ne 0) {
    $failed += $f
    Write-Output "=== FAIL: $f ==="
    Write-Output ($msg -join "`n")
  } else {
    Write-Output "OK: $f"
  }
}
Write-Output "-----"
Write-Output ("Failed: " + $failed.Count)
$failed | ForEach-Object { Write-Output ("  " + $_) }
