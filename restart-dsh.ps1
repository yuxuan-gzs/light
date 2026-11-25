# Restart DSH Desktop so the newly installed dsh-whale-widget bundle loads.
$log = 'D:\YX\whale-restart.log'
function Log($m) { "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $m" | Out-File -LiteralPath $log -Append -Encoding utf8 }

Log 'restarter started; waiting 30s'
Start-Sleep -Seconds 30

$exe = 'D:\yuxuangzs\DSH Desktop\DSH Desktop.exe'
if (-not (Test-Path -LiteralPath $exe)) { Log "FATAL missing $exe"; exit 1 }

Log 'stopping DSH Desktop'
Get-Process -Name 'DSH Desktop' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 6

Log "starting $exe"
Start-Process -FilePath $exe

for ($i = 1; $i -le 40; $i++) {
  Start-Sleep -Seconds 3
  $up = Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | Where-Object { $_.LocalPort -ge 40000 -and $_.LocalPort -le 60000 -and $_.OwningProcess -in (Get-Process -Name 'DSH Desktop' -ErrorAction SilentlyContinue).Id }
  if ($up) { Log ("listening on " + (($up.LocalPort | Sort-Object -Unique) -join ',')); break }
}
$alive = (Get-Process -Name 'DSH Desktop' -ErrorAction SilentlyContinue | Measure-Object).Count
Log "done; DSH Desktop processes: $alive"
