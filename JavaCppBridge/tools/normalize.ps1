# normalize.ps1 - canonicalise a text file for golden-file comparison.
#
# Generated program output is not byte-stable across platforms: on Windows
# Java's println writes CRLF while the C++ side writes LF, and whether the
# last line carries a terminator depends on how the run was redirected.
# This helper rewrites a file as LF-only with exactly one trailing newline so
# tests/expect/*.txt can be compared with a plain binary diff.
param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out
)

$t = [System.IO.File]::ReadAllText($In)
$t = $t -replace "`r`n", "`n"
$t = $t -replace "`r", "`n"
$t = $t.TrimEnd("`n") + "`n"
$enc = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($Out, $t, $enc)
